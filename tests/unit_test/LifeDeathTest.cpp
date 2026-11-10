#include "LifeDeath.hpp"
#include "BinaryWriter.hpp"
#include "RepoRoot.hpp"
#include "SolutionVerifier.hpp"
#include <algorithm>
#include <filesystem>
#include <gtest/gtest.h>
#include <stdexcept>

namespace {
using namespace opusoptima;
TEST(LifeDeathTest, CompactRelayExperimentalAdmission) {
    io::PuzzleData puzzle;
    puzzle.version = 3;
    puzzle.name = "compact-relay-admission";
    puzzle.permissionBits = (1ULL << 32) - 1;
    puzzle.outputMultiplier = 1;
    puzzle.inputs = {
        {{{core::AtomType::Air, {0, 0}}, {core::AtomType::Air, {1, 0}}}, {{1, {0, 0}, {1, 0}}}}};
    puzzle.outputs = {
        {{{core::AtomType::Salt, {0, 0}}, {core::AtomType::Salt, {1, 0}}}, {{1, {0, 0}, {1, 0}}}}};
    EXPECT_THROW(solvers::CompileLifeDeath(puzzle), std::invalid_argument);
    EXPECT_NO_THROW(solvers::CompileCompactChemistryRelay(puzzle, false));
    EXPECT_NO_THROW(solvers::CompileCompactChemistryRelay(puzzle));
    puzzle.outputs.push_back(puzzle.outputs.front());
    EXPECT_THROW(solvers::CompileCompactChemistryRelay(puzzle), std::invalid_argument);
    puzzle.outputs.resize(1);
    puzzle.inputs.front().atoms.resize(1);
    puzzle.inputs.front().bonds.clear();
    EXPECT_THROW(solvers::CompileCompactChemistryRelay(puzzle), std::invalid_argument);
}
TEST(LifeDeathTest, UnsupportedInputsAndPermissions) {
    const auto root = opusoptima::test::RepoRoot(__FILE__);
    const auto original =
        io::BinaryParser::ParsePuzzleFile(root / "asset/puzzle/campaign/ch3/P030b.puzzle");
    for (const int bit : {0, 2, 3, 8, 12, 16, 22, 23}) {
        auto puzzle = original;
        puzzle.permissionBits &= ~(1ULL << bit);
        EXPECT_THROW(solvers::CompileLifeDeath(puzzle), std::invalid_argument) << bit;
    }
    auto puzzle = original;
    puzzle.outputs.clear();
    EXPECT_THROW(solvers::CompileLifeDeath(puzzle), std::invalid_argument);
    puzzle = original;
    puzzle.outputMultiplier = 3;
    EXPECT_THROW(solvers::CompileLifeDeath(puzzle), std::invalid_argument);
    puzzle = original;
    puzzle.inputs.front().atoms.push_back(puzzle.inputs.front().atoms.front());
    EXPECT_THROW(solvers::CompileLifeDeath(puzzle), std::invalid_argument);
    puzzle = original;
    puzzle.inputs.front().atoms.front().type = core::AtomType::Vitae;
    EXPECT_NO_THROW(solvers::CompileLifeDeath(puzzle));
    puzzle = original;
    puzzle.outputs.front().atoms.front().type = core::AtomType::Repeat;
    EXPECT_THROW(solvers::CompileLifeDeath(puzzle), std::invalid_argument);
    puzzle = original;
    for (auto& atom : puzzle.outputs.front().atoms)
        atom.type = core::AtomType::Salt;
    EXPECT_THROW(solvers::CompileLifeDeath(puzzle), std::invalid_argument);
}
TEST(LifeDeathTest, CombinedSaltChemistryPermissions) {
    const auto root = opusoptima::test::RepoRoot(__FILE__);
    const auto original =
        io::BinaryParser::ParsePuzzleFile(root / "asset/puzzle/journal/journal4/P072.puzzle");
    for (int bit : {13, 18, 28}) {
        auto puzzle = original;
        puzzle.permissionBits &= ~(1ULL << bit);
        EXPECT_THROW(solvers::CompileLifeDeath(puzzle), std::invalid_argument) << bit;
    }
}
#ifdef HAS_LIBVERIFY
TEST(LifeDeathVerifierTest, SelectiveCardinalUnification) {
    const auto root = opusoptima::test::RepoRoot(__FILE__);
    for (const auto* file : {"asset/puzzle/tourney-2020/OM2020_W4_VolatilityAndTranquility.puzzle",
                             "asset/puzzle/workshop-versions/w2501727977.puzzle"}) {
        SCOPED_TRACE(file);
        const auto puzzlePath = root / file;
        auto solution = solvers::CompileLifeDeath(io::BinaryParser::ParsePuzzleFile(puzzlePath));
        const auto path =
            root / "build" / (puzzlePath.stem().string() + "-selective-unification-test.solution");
        struct Cleanup {
            std::filesystem::path path;
            ~Cleanup() {
                std::error_code ec;
                std::filesystem::remove(path, ec);
            }
        } cleanup{path};
        EXPECT_LT(solution.objects.size(), 10000U);
        for (const auto& part : solution.objects) {
            if (!part.instructions.empty()) {
                const auto [first, last] = std::minmax_element(
                    part.instructions.begin(), part.instructions.end(),
                    [](const auto& a, const auto& b) { return a.parameter < b.parameter; });
                ASSERT_LE(last->parameter - first->parameter, 99999);
            }
            ASSERT_LT(part.instructions.size(), 100000U)
                << part.typeName << " at " << part.position.q << ',' << part.position.r;
            if (part.typeName == "track") {
                ASSERT_LE(part.bodyData[4], 9999);
            }
            for (const auto& ins : part.instructions)
                ASSERT_GE(ins.parameter, 0) << part.typeName;
        }
        for (int pass = 0; pass < 2; ++pass) {
            io::BinaryWriter::WriteSolutionFile(path, solution);
            const auto report = io::VerifySolutionFiles(puzzlePath, path, 150000, 10000);
            ASSERT_TRUE(report.Passed()) << report.detail << " cycle=" << report.errorCycle
                                         << " at " << report.errorU << ',' << report.errorV;
            solution.metrics = report.metrics;
        }
    }
}
TEST(LifeDeathVerifierTest, DispersionWithoutDisposal) {
    const auto root = opusoptima::test::RepoRoot(__FILE__);
    for (const auto* file : {"asset/puzzle/tourney-2019/week7.puzzle",
                             "asset/puzzle/workshop-versions/w1698787731.puzzle"}) {
        SCOPED_TRACE(file);
        const auto puzzlePath = root / file;
        const auto puzzle = io::BinaryParser::ParsePuzzleFile(puzzlePath);
        ASSERT_FALSE(puzzle.permissionBits & (1ULL << 17));
        auto solution = solvers::CompileLifeDeath(puzzle);
        const auto path =
            root / "build" / (puzzlePath.stem().string() + "-dispersion-storage-test.solution");
        struct Cleanup {
            std::filesystem::path path;
            ~Cleanup() {
                std::error_code ec;
                std::filesystem::remove(path, ec);
            }
        } cleanup{path};
        for (int pass = 0; pass < 2; ++pass) {
            io::BinaryWriter::WriteSolutionFile(path, solution);
            const auto report = io::VerifySolutionFiles(puzzlePath, path, 150000, 10000);
            ASSERT_TRUE(report.Passed()) << report.detail << " cycle=" << report.errorCycle
                                         << " at " << report.errorU << ',' << report.errorV;
            solution.metrics = report.metrics;
        }
    }
}
TEST(LifeDeathVerifierTest, MolecularMetalChemistry) {
    const auto root = opusoptima::test::RepoRoot(__FILE__);
    const auto puzzlePath = root / "asset/puzzle/workshop-versions/w2788067677.puzzle";
    auto solution = solvers::CompileLifeDeath(io::BinaryParser::ParsePuzzleFile(puzzlePath));
    const auto path = root / "build" / "fulmination-metal-feed-test.solution";
    struct Cleanup {
        std::filesystem::path path;
        ~Cleanup() {
            std::error_code ec;
            std::filesystem::remove(path, ec);
        }
    } cleanup{path};
    for (int pass = 0; pass < 2; ++pass) {
        io::BinaryWriter::WriteSolutionFile(path, solution);
        const auto report = io::VerifySolutionFiles(puzzlePath, path, 150000, 10000);
        ASSERT_TRUE(report.Passed()) << report.detail << " cycle=" << report.errorCycle << " at "
                                     << report.errorU << ',' << report.errorV;
        solution.metrics = report.metrics;
    }
}
TEST(LifeDeathVerifierTest, MolecularSaltChemistry) {
    const auto root = opusoptima::test::RepoRoot(__FILE__);
    for (const auto* file :
         {"asset/puzzle/workshop-versions/w2788066865.puzzle",
          "asset/puzzle/tourney-2021/OM2021_W2.puzzle",
          "asset/puzzle/workshop-versions/w2450511665.puzzle",
          "asset/puzzle/edge-cases/OM2025_6_Bulk_Transmutation.puzzle",
          "asset/puzzle/tourney-2020/OM2020_W6_ActivePolymerase.puzzle",
          "asset/puzzle/workshop-versions/w2501728219.puzzle",
          "asset/puzzle/workshop-versions/w2946682999.puzzle",
          "asset/puzzle/tourney-2019/week8.puzzle",
          "asset/puzzle/workshop-versions/w1698788220.puzzle",
          "asset/puzzle/journal/journal9-p/P108.puzzle",
          "asset/puzzle/journal/journal7/P098.puzzle", "asset/puzzle/journal/journal8/P102.puzzle",
          "asset/puzzle/tourney-2021/OM2021_W7.puzzle", "asset/puzzle/campaign/ch5/P043.puzzle"}) {
        SCOPED_TRACE(file);
        const auto puzzlePath = root / file;
        auto solution = solvers::CompileLifeDeath(io::BinaryParser::ParsePuzzleFile(puzzlePath));
        const auto path =
            root / "build" / (puzzlePath.stem().string() + "-molecular-life-test.solution");
        struct Cleanup {
            std::filesystem::path path;
            ~Cleanup() {
                std::error_code ec;
                std::filesystem::remove(path, ec);
            }
        } cleanup{path};
        for (int pass = 0; pass < 2; ++pass) {
            io::BinaryWriter::WriteSolutionFile(path, solution);
            const auto report = io::VerifySolutionFiles(puzzlePath, path, 150000, 10000);
            ASSERT_TRUE(report.Passed()) << report.detail << " cycle=" << report.errorCycle
                                         << " at " << report.errorU << ',' << report.errorV;
            solution.metrics = report.metrics;
        }
    }
}
TEST(LifeDeathVerifierTest, CombinedSaltChemistryOriginalProducts) {
    const auto root = opusoptima::test::RepoRoot(__FILE__);
    const auto directory = std::filesystem::current_path();
    std::filesystem::create_directories(directory);
    for (const auto* file :
         {"asset/puzzle/journal/journal4/P072.puzzle", "asset/puzzle/tourney-2019/week6.puzzle",
          "asset/puzzle/tourney-2019/week4.puzzle"}) {
        SCOPED_TRACE(file);
        const auto path = root / file;
        const auto puzzle = io::BinaryParser::ParsePuzzleFile(path);
        auto solution = solvers::CompileLifeDeath(puzzle);
        const auto output = directory / (path.stem().string() + "-combined-test.solution");
        struct Cleanup {
            std::filesystem::path path;
            ~Cleanup() {
                std::error_code ec;
                std::filesystem::remove(path, ec);
            }
        } cleanup{output};
        for (int pass = 0; pass < 2; ++pass) {
            io::BinaryWriter::WriteSolutionFile(output, solution);
            const auto report = io::VerifySolutionFiles(path, output, 150000, 10000);
            ASSERT_TRUE(report.Passed()) << report.detail << " cycle=" << report.errorCycle
                                         << " at " << report.errorU << ',' << report.errorV;
            solution.metrics = report.metrics;
        }
        RecordProperty(path.stem().string() + "_cycles", solution.metrics.cycles);
    }
}
TEST(LifeDeathVerifierTest, ExistingLocalProducts) {
    const auto root = opusoptima::test::RepoRoot(__FILE__);
    for (const auto* file :
         {"asset/puzzle/campaign/ch3/P030b.puzzle", "asset/puzzle/campaign/ch4/P032.puzzle",
          "asset/puzzle/journal/journal5/P086.puzzle",
          "asset/puzzle/workshop-versions/w2946693353.puzzle"}) {
        SCOPED_TRACE(file);
        const auto puzzlePath = root / file;
        auto puzzle = io::BinaryParser::ParsePuzzleFile(puzzlePath);
        auto solution = solvers::CompileLifeDeath(puzzle);
        const auto solutionPath =
            root / "build" / (puzzlePath.stem().string() + "-lifedeath.solution");
        io::BinaryWriter::WriteSolutionFile(solutionPath, solution);
        auto report = io::VerifySolutionFiles(puzzlePath, solutionPath, 150000, 10000);
        ASSERT_TRUE(report.Passed()) << report.detail << " cycle=" << report.errorCycle << " at "
                                     << report.errorU << "," << report.errorV;
        RecordProperty(puzzlePath.stem().string() + "_cycles", report.metrics.cycles);
        RecordProperty(puzzlePath.stem().string() + "_cost", report.metrics.cost);
        RecordProperty(puzzlePath.stem().string() + "_area", report.metrics.area);
        solution.metrics = report.metrics;
        io::BinaryWriter::WriteSolutionFile(solutionPath, solution);
        EXPECT_TRUE(io::VerifySolutionFiles(puzzlePath, solutionPath, 150000, 10000).Passed());
    }
}
#endif
} // namespace
