#include "PurificationAssembly.hpp"
#include "IO/BinaryWriter.hpp"
#include "IO/SolutionVerifier.hpp"
#include "AssemblySolver.hpp"
#include "RowAssembly.hpp"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include "RepoRoot.hpp"

namespace opusoptima::solvers { namespace {
const auto root = opusoptima::test::RepoRoot(__FILE__);
TEST(PurificationAssemblyTest, RejectsMissingPermissionsAndMalformedFeeds) {
    auto p = io::BinaryParser::ParsePuzzleFile(root / "asset/puzzle/campaign/ch4/P034.puzzle");
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    EXPECT_EQ(io::BinaryWriter::WriteSolution(CompilePurificationAssembly(p)),
              io::BinaryWriter::WriteSolution(CompilePurificationAssembly(p, false, deadline)));
    EXPECT_THROW((void)CompilePurificationAssembly(p, false, std::chrono::steady_clock::now()),
                 std::invalid_argument);
    for (int bit : {0, 2, 3, 8, 15, 22, 23}) {
        auto invalid = p;
        invalid.permissionBits &= ~(1ULL << bit);
        EXPECT_THROW((void)CompilePurificationAssembly(invalid), std::invalid_argument);
    }
    p.inputs[0].atoms.push_back(p.inputs[0].atoms[0]);
    EXPECT_THROW((void)CompilePurificationAssembly(p), std::invalid_argument);
}
TEST(PurificationAssemblyTest, FiniteMolecularFeedRequiresExplicitOptInAndPositiveCount) {
    auto p = io::BinaryParser::ParsePuzzleFile(root / "asset/puzzle/campaign/ch4/P034.puzzle");
    p.permissionBits |= 1ULL << 9;
    p.permissionBits &= ~(1ULL << 17);
    p.inputs.resize(1);
    p.inputs[0].atoms = {{core::AtomType::Lead, {0, 0}}, {core::AtomType::Salt, {1, 0}}};
    p.inputs[0].bonds = {{1, {0, 0}, {1, 0}}};
    p.outputs.resize(1);
    p.outputs[0].atoms = {{core::AtomType::Tin, {0, 0}}};
    p.outputs[0].bonds.clear();
    EXPECT_THROW((void)CompilePurificationAssembly(p), std::invalid_argument);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    EXPECT_EQ(io::BinaryWriter::WriteSolution(CompilePurificationAssembly(p, true)),
              io::BinaryWriter::WriteSolution(CompilePurificationAssembly(p, true, deadline)));
    EXPECT_THROW((void)CompilePurificationAssembly(p, true, std::chrono::steady_clock::now()),
                 std::invalid_argument);
    p.outputs[0].atoms[0].type = core::AtomType::Lead;
    const auto plan = AssemblyPlanner::Plan(p);
    std::vector<int> ends;
    EXPECT_THROW((void)CompileRowAssemblyFiniteBatch(p, plan, ends, 0), std::invalid_argument);
    EXPECT_NO_THROW((void)CompileRowAssemblyFiniteBatch(p, plan, ends, 2));
    EXPECT_EQ(ends.size(), 2U);
    const auto rowBytes = [](const auto& solution) { return io::BinaryWriter::WriteSolution(solution); };
    EXPECT_EQ(rowBytes(CompileRowAssemblySingleProduct(p, plan)),
              rowBytes(CompileRowAssemblySingleProduct(p, plan, deadline)));
    EXPECT_THROW((void)CompileRowAssemblySingleProduct(p, plan, std::chrono::steady_clock::now()),
                 std::invalid_argument);
    EXPECT_LT(ends[0], ends[1]);
    p.permissionBits |= 1ULL << 17;
    const auto batchPlan = AssemblyPlanner::Plan(p);
    std::vector<int> oldEnds, newEnds;
    EXPECT_EQ(rowBytes(CompileRowAssemblyBatch(p, batchPlan, oldEnds)),
              rowBytes(CompileRowAssemblyBatch(p, batchPlan, newEnds, deadline)));
    EXPECT_EQ(oldEnds, newEnds);
    EXPECT_THROW((void)CompileRowAssemblyBatch(p, batchPlan, newEnds,
                 std::chrono::steady_clock::now()), std::invalid_argument);
}
#ifdef HAS_LIBVERIFY
TEST(PurificationAssemblyTest, MolecularMetalSupplyVerifies) {
    for (const bool shared : {false, true})
        for (const bool disposal : {true, false})
            for (const bool surplus : {false, true})
                for (const auto target :
                     {core::AtomType::Gold, core::AtomType::Silver, core::AtomType::Copper,
                      core::AtomType::Iron, core::AtomType::Tin}) {
                    if (!shared && target != core::AtomType::Tin && target != core::AtomType::Iron)
                        continue;
                    if (surplus && (!shared || !disposal)) continue;
                    SCOPED_TRACE(shared);
                    SCOPED_TRACE(surplus);
                    SCOPED_TRACE(disposal);
                    SCOPED_TRACE(static_cast<int>(target));
                    auto p = io::BinaryParser::ParsePuzzleFile(
                        root / "asset/puzzle/campaign/ch4/P034.puzzle");
                    p.permissionBits |= (1ULL << 9) | (1ULL << 17);
                    if (!disposal) p.permissionBits &= ~(1ULL << 17);
                    p.inputs.resize(1);
                    p.inputs[0].atoms = {{core::AtomType::Lead, {0, 0}},
                                         {core::AtomType::Salt, {1, 0}}};
                    p.inputs[0].bonds = {{1, {0, 0}, {1, 0}}};
                    if (shared)
                        p.inputs[0].atoms[0].type =
                            static_cast<core::AtomType>(static_cast<int>(target) + 1);
                    if (surplus) {
                        p.inputs[0].atoms.push_back({core::AtomType::Salt, {2, 0}});
                        p.inputs[0].bonds.push_back({1, {1, 0}, {2, 0}});
                    }
                    p.outputs.resize(1);
                    p.outputs[0].atoms = {{target, {0, 0}}};
                    p.outputs[0].bonds.clear();
                    if (shared) {
                        p.outputs[0].atoms.push_back({core::AtomType::Salt, {1, 0}});
                        p.outputs[0].atoms.push_back({core::AtomType::Salt, {2, 0}});
                        p.outputs[0].bonds = {{1, {0, 0}, {1, 0}}, {1, {1, 0}, {2, 0}}};
                    }
                    const auto puzzlePath =
                        std::filesystem::current_path() / "molecular-purification-test.puzzle";
                    const auto solutionPath =
                        std::filesystem::current_path() / "molecular-purification-test.solution";
                    struct Cleanup {
                        std::filesystem::path puzzle, solution;
                        ~Cleanup() {
                            std::error_code ec;
                            std::filesystem::remove(puzzle, ec);
                            std::filesystem::remove(solution, ec);
                        }
                    } cleanup{puzzlePath, solutionPath};
                    {
                        std::ofstream out(puzzlePath, std::ios::binary);
                        auto word = [&](std::uint64_t v, int n) {
                            for (int i = 0; i < n; ++i)
                                out.put(static_cast<char>(v >> (8 * i)));
                        };
                        word(3, 4);
                        word(p.name.size(), 1);
                        out.write(p.name.data(), p.name.size());
                        word(0, 8);
                        word(p.permissionBits, 8);
                        for (const auto* list : {&p.inputs, &p.outputs}) {
                            word(list->size(), 4);
                            for (const auto& m : *list) {
                                word(m.atoms.size(), 4);
                                for (const auto& a : m.atoms) {
                                    word(static_cast<std::uint8_t>(a.type), 1);
                                    word(a.position.q, 1);
                                    word(a.position.r, 1);
                                }
                                word(m.bonds.size(), 4);
                                for (const auto& b : m.bonds) {
                                    word(b.bits, 1);
                                    word(b.from.q, 1);
                                    word(b.from.r, 1);
                                    word(b.to.q, 1);
                                    word(b.to.r, 1);
                                }
                            }
                        }
                        word(p.outputMultiplier, 4);
                        word(0, 1);
                    }
                    auto solution = shared ? CompileSharedPurificationAssembly(p)
                                           : CompilePurificationAssembly(p, !disposal);
                    io::BinaryWriter::WriteSolutionFile(solutionPath, solution);
                    auto report = io::VerifySolutionFiles(puzzlePath, solutionPath, 1000000, 10000);
                    ASSERT_TRUE(report.Passed()) << report.detail << " cycle=" << report.errorCycle
                                                 << " at=" << report.errorU << "," << report.errorV;
                    solution.metrics = report.metrics;
                    EXPECT_GT(solution.metrics.cycles, 0);
                    io::BinaryWriter::WriteSolutionFile(solutionPath, solution);
                    report = io::VerifySolutionFiles(puzzlePath, solutionPath, 1000000, 10000);
                    EXPECT_TRUE(report.Passed()) << report.detail << " cycle=" << report.errorCycle;
                }
}
TEST(PurificationAssemblyTest, FiniteMixedFeedClearsEscapeArmAndVerifiesTwice) {
    const auto puzzlePath = root / "asset/puzzle/workshop-versions/w2788066279.puzzle";
    const auto path = std::filesystem::current_path() / "finite-mixed-purification-test.solution";
    struct Cleanup {
        std::filesystem::path path;
        ~Cleanup() {
            std::error_code ec;
            std::filesystem::remove(path, ec);
        }
    } cleanup{path};
    auto solution = AssemblySolver{}.Solve(io::BinaryParser::ParsePuzzleFile(puzzlePath));
    io::BinaryWriter::WriteSolutionFile(path, solution);
    auto report = io::VerifySolutionFiles(puzzlePath, path);
    ASSERT_TRUE(report.Passed()) << report.detail << " cycle=" << report.errorCycle
                                 << " at=" << report.errorU << "," << report.errorV;
    solution.metrics = report.metrics;
    EXPECT_GT(solution.metrics.cycles, 0);
    EXPECT_GT(solution.metrics.cost, 0);
    io::BinaryWriter::WriteSolutionFile(path, solution);
    report = io::VerifySolutionFiles(puzzlePath, path);
    EXPECT_TRUE(report.Passed()) << report.detail << " cycle=" << report.errorCycle;
}
TEST(PurificationAssemblyTest, SharedInventoryRealMolecularFeedVerifiesTwice) {
    const auto puzzlePath = root / "asset/puzzle/journal/journal3/P068.puzzle";
    const auto puzzle = io::BinaryParser::ParsePuzzleFile(puzzlePath);
    const auto path = std::filesystem::current_path() / "shared-purification-real-test.solution";
    struct Cleanup {
        std::filesystem::path path;
        ~Cleanup() {
            std::error_code ec;
            std::filesystem::remove(path, ec);
        }
    } cleanup{path};
    auto solution = AssemblySolver{}.Solve(puzzle);
    int maxInstruction = 0;
    for (const auto& part : solution.objects) {
        EXPECT_LE(part.instructions.size(), 99999U);
        for (const auto& instruction : part.instructions)
            maxInstruction = std::max(maxInstruction, instruction.parameter);
    }
    EXPECT_LT(maxInstruction, 99999);
    io::BinaryWriter::WriteSolutionFile(path, solution);
    auto report = io::VerifySolutionFiles(puzzlePath, path, 1000000, 10000);
    ASSERT_TRUE(report.Passed()) << report.detail << " cycle=" << report.errorCycle
                                 << " at=" << report.errorU << "," << report.errorV;
    solution.metrics = report.metrics;
    EXPECT_GT(solution.metrics.cycles, 0);
    EXPECT_GT(solution.metrics.cost, 0);
    io::BinaryWriter::WriteSolutionFile(path, solution);
    report = io::VerifySolutionFiles(puzzlePath, path, 1000000, 10000);
    EXPECT_TRUE(report.Passed()) << report.detail << " cycle=" << report.errorCycle;
}
TEST(PurificationAssemblyTest, RealBondedProductsVerifyTwiceWithMeasuredMetrics) {
    const auto path = std::filesystem::current_path() / "purification-assembly-test.solution";
    std::filesystem::create_directories(path.parent_path());
    struct Cleanup {
        std::filesystem::path path;
        ~Cleanup() {
            std::error_code ec;
            std::filesystem::remove(path, ec);
        }
    } cleanup{path};
    for (const auto* file :
         {"asset/puzzle/campaign/ch4/P033.puzzle", "asset/puzzle/campaign/ch4/P034.puzzle",
          "asset/puzzle/campaign/ch5/P038.puzzle", "asset/puzzle/tourney-2019/week3.puzzle",
          "asset/puzzle/journal/journal3/P068.puzzle", "asset/puzzle/journal/journal7/P095.puzzle",
          "asset/puzzle/workshop-versions/w2788066771.puzzle"}) {
        SCOPED_TRACE(file);
        const auto puzzlePath = root / file;
        auto solution = AssemblySolver{}.Solve(io::BinaryParser::ParsePuzzleFile(puzzlePath));
        io::BinaryWriter::WriteSolutionFile(path, solution);
        auto report = io::VerifySolutionFiles(puzzlePath, path, 1000000, 10000);
        ASSERT_TRUE(report.Passed()) << report.detail << " cycle=" << report.errorCycle;
        solution.metrics = report.metrics;
        EXPECT_GT(solution.metrics.cycles, 0);
        EXPECT_GT(solution.metrics.cost, 0);
        io::BinaryWriter::WriteSolutionFile(path, solution);
        report = io::VerifySolutionFiles(puzzlePath, path, 1000000, 10000);
        EXPECT_TRUE(report.Passed()) << report.detail << " cycle=" << report.errorCycle;
    }
}
#endif
}} // namespace opusoptima::solvers
