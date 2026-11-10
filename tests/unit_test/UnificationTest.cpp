#include "Unification.hpp"
#include "AssemblySolver.hpp"
#include "BinaryWriter.hpp"
#include "SolutionVerifier.hpp"
#include <filesystem>
#include <gtest/gtest.h>
#include <stdexcept>
#include "RepoRoot.hpp"

namespace {
using namespace opusoptima;
const auto root = opusoptima::test::RepoRoot(__FILE__);
template <typename Legacy, typename Timed>
void ExpectDeadlineEquivalent(Legacy legacy, Timed timed) {
    const auto expected = io::BinaryWriter::WriteSolution(legacy());
    EXPECT_EQ(io::BinaryWriter::WriteSolution(timed(std::chrono::steady_clock::time_point::max())),
              expected);
    EXPECT_EQ(io::BinaryWriter::WriteSolution(
                  timed(std::chrono::steady_clock::now() + std::chrono::minutes(5))),
              expected);
    try {
        timed(std::chrono::steady_clock::now() - std::chrono::seconds(1));
        FAIL() << "expired unification deadline was accepted";
    } catch (const std::invalid_argument& error) {
        EXPECT_STREQ(error.what(), "unification deadline");
    }
}
void ExpectFiniteDeadlineEquivalent(const io::PuzzleData& puzzle, int products) {
    std::vector<int> expectedEnds;
    const auto expected = solvers::CompileUnificationFiniteBatch(puzzle, expectedEnds, products);
    ExpectDeadlineEquivalent([&] { return expected; }, [&](auto deadline) {
        std::vector<int> ends{123};
        auto result = solvers::CompileUnificationFiniteBatch(puzzle, ends, products, deadline);
        EXPECT_EQ(ends, expectedEnds);
        return result;
    });
}
TEST(UnificationTest, RejectsUnsupportedShapesAndMissingPermissions) {
    const auto original =
        io::BinaryParser::ParsePuzzleFile(root / "asset/puzzle/journal/journal4/P071.puzzle");
    ExpectDeadlineEquivalent([&] { return solvers::CompileUnification(original); },
                             [&](auto deadline) {
                                 return solvers::CompileUnification(original, deadline);
                             });
    for (int bit : {0, 2, 3, 9, 13, 18, 22, 23, 28}) {
        auto puzzle = original;
        puzzle.permissionBits &= ~(1ULL << bit);
        EXPECT_THROW(solvers::CompileUnification(puzzle), std::invalid_argument) << bit;
    }
    auto puzzle = original;
    puzzle.inputs[0].atoms.pop_back();
    EXPECT_THROW(solvers::CompileUnification(puzzle), std::invalid_argument);
    puzzle = original;
    puzzle.outputs[0].atoms[0].type = core::AtomType::Salt;
    EXPECT_THROW(solvers::CompileUnification(puzzle), std::invalid_argument);
    puzzle = original;
    puzzle.inputs[0].atoms[0].type = core::AtomType::Quintessence;
    EXPECT_THROW(solvers::CompileUnification(puzzle), std::invalid_argument);
    puzzle = original;
    puzzle.inputs[0].atoms[0].type = core::AtomType::Salt;
    EXPECT_NO_THROW(solvers::CompileUnification(puzzle));
    puzzle = original;
    puzzle.inputs[0].atoms[0].position = puzzle.inputs[0].atoms[1].position;
    EXPECT_THROW(solvers::CompileUnification(puzzle), std::invalid_argument);
    puzzle = original;
    puzzle.inputs[0].bonds[0].bits = 2;
    EXPECT_THROW(solvers::CompileUnification(puzzle), std::invalid_argument);
    puzzle = original;
    puzzle.inputs[0].bonds[0].from = {40, 40};
    EXPECT_THROW(solvers::CompileUnification(puzzle), std::invalid_argument);
    puzzle = original;
    puzzle.outputMultiplier = 9;
    EXPECT_THROW(solvers::CompileUnification(puzzle), std::invalid_argument);
    puzzle = original;
    puzzle.inputs.clear();
    EXPECT_THROW(solvers::CompileUnification(puzzle), std::invalid_argument);
    puzzle = original;
    puzzle.outputs.clear();
    EXPECT_THROW(solvers::CompileUnification(puzzle), std::invalid_argument);
}
TEST(UnificationTest, BulkRequiresNormalizationAndCannotBeASingleBatch) {
    auto puzzle = io::BinaryParser::ParsePuzzleFile(
        root / "asset/puzzle/edge-cases/OM2025_6_Bulk_Transmutation.puzzle");
    ExpectDeadlineEquivalent([&] { return solvers::CompileUnification(puzzle); },
                             [&](auto deadline) {
                                 return solvers::CompileUnification(puzzle, deadline);
                             });
    ExpectFiniteDeadlineEquivalent(puzzle, 1);
    ExpectFiniteDeadlineEquivalent(puzzle, 2);
    EXPECT_THROW(solvers::CompileUnificationBatch(puzzle), std::invalid_argument);
    puzzle.permissionBits &= ~(1ULL << 12);
    EXPECT_THROW(solvers::CompileUnification(puzzle), std::invalid_argument);
}
TEST(UnificationTest, BatchSelectsCompatibleWholeInputsAndPreservesIndices) {
    auto puzzle =
        io::BinaryParser::ParsePuzzleFile(root / "asset/puzzle/journal/journal4/P071.puzzle");
    puzzle.inputs = {
        {{{core::AtomType::Gold, {0, 0}}}, {}},
        {{{core::AtomType::Salt, {0, 0}}, {core::AtomType::Salt, {1, 0}}}, {{1, {0, 0}, {1, 0}}}}};
    const auto solution = solvers::CompileUnificationBatch(puzzle);
    int inputs = 0;
    for (const auto& part : solution.objects)
        if (part.typeName == "input") {
            EXPECT_EQ(part.bodyData[2], 1);
            ++inputs;
        }
    EXPECT_EQ(inputs, 2);
    ExpectDeadlineEquivalent([&] { return solvers::CompileUnificationBatch(puzzle); },
                             [&](auto deadline) {
                                 return solvers::CompileUnificationBatch(puzzle, deadline);
                             });
    ExpectFiniteDeadlineEquivalent(puzzle, 1);
    puzzle.inputs[1] = {{{core::AtomType::Salt, {0, 0}}}, {}};
    ExpectDeadlineEquivalent([&] { return solvers::CompileSaltQuintessenceBatch(puzzle, 1); },
                             [&](auto deadline) {
                                 return solvers::CompileSaltQuintessenceBatch(puzzle, 1, deadline);
                             });
    puzzle.inputs.erase(puzzle.inputs.begin() + 1);
    EXPECT_THROW(solvers::CompileUnificationBatch(puzzle), std::invalid_argument);
}
#ifdef HAS_LIBVERIFY
TEST(UnificationVerifierTest, MixedImpuritiesFitInstructionLimitAndVerifyTwice) {
    const auto path = root / "asset/puzzle/journal/journal7/P099.puzzle";
    auto solution = solvers::AssemblySolver{}.Solve(io::BinaryParser::ParsePuzzleFile(path));
    for (const auto& part : solution.objects) {
        EXPECT_LE(part.instructions.size(), 99999u);
        for (const auto& instruction : part.instructions)
            EXPECT_LT(instruction.parameter, 99999);
    }
    const auto output = std::filesystem::current_path() / "opus-unification-P099.solution";
    struct Cleanup {
        std::filesystem::path path;
        ~Cleanup() {
            std::filesystem::remove(path);
        }
    } cleanup{output};
    io::BinaryWriter::WriteSolutionFile(output, solution);
    auto report = io::VerifySolutionFiles(path, output);
    ASSERT_TRUE(report.Passed()) << report.detail;
    solution.metrics = report.metrics;
    io::BinaryWriter::WriteSolutionFile(output, solution);
    report = io::VerifySolutionFiles(path, output);
    EXPECT_TRUE(report.Passed()) << report.detail;
}
TEST(UnificationVerifierTest, RealAlcoholProductTwice) {
    const auto path = root / "asset/puzzle/journal/journal4/P071.puzzle";
    auto solution = solvers::CompileUnification(io::BinaryParser::ParsePuzzleFile(path));
    const auto output = std::filesystem::current_path() / "opus-unification-P071.solution";
    std::filesystem::create_directories(output.parent_path());
    struct Cleanup {
        std::filesystem::path path;
        ~Cleanup() {
            std::filesystem::remove(path);
        }
    } cleanup{output};
    io::BinaryWriter::WriteSolutionFile(output, solution);
    auto report = io::VerifySolutionFiles(path, output, 150000, 10000);
    ASSERT_TRUE(report.Passed()) << report.detail << " cycle=" << report.errorCycle << " at "
                                 << report.errorU << ',' << report.errorV;
    RecordProperty("cycles", report.metrics.cycles);
    RecordProperty("cost", report.metrics.cost);
    RecordProperty("area", report.metrics.area);
    solution.metrics = report.metrics;
    io::BinaryWriter::WriteSolutionFile(output, solution);
    report = io::VerifySolutionFiles(path, output, 150000, 10000);
    EXPECT_TRUE(report.Passed()) << report.detail;
}
#endif
} // namespace
