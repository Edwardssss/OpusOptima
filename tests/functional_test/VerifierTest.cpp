#include "../unit_test/UnitTestConstants.hpp"
#include "SolutionVerifier.hpp"
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <vector>

#include <gtest/gtest.h>

#include "BinaryParser.hpp"
#include "BinaryWriter.hpp"
#include "CollisionLUT.hpp"
#include "HexSimulator.hpp"
#include "MachineState.hpp"
#include "RepoRoot.hpp"
#include "SingleAtomSolver.hpp"

#ifdef HAS_LIBVERIFY
namespace {

std::filesystem::path GetRepoRoot() {
    return opusoptima::test::RepoRoot(__FILE__);
}

} // namespace

TEST(ExternalVerifier, KnownSolutionsComplete) {
    const auto repo = GetRepoRoot();
    for (const auto* stem : {"easy-conduit", "double-conduit"}) {
        const auto report = opusoptima::io::VerifySolutionFiles(
            repo / (std::string("asset/puzzle/easy/") + stem + ".puzzle"),
            repo / (std::string("asset/solution/easy/") + stem + "-" + stem + "-1.solution"));
        ASSERT_TRUE(report.Passed()) << report.detail;
        EXPECT_GT(report.metrics.cycles, 0);
    }
}

TEST(ExternalVerifier, UnverifiedVialRulesCannotPublishOrReplaceOutput) {
    const auto puzzle = GetRepoRoot() / "asset/puzzle/workshop-versions/w2591419339.puzzle";
    const auto parsed = opusoptima::io::BinaryParser::ParsePuzzleFile(puzzle);
    ASSERT_TRUE(parsed.productionInfo);
    ASSERT_FALSE(parsed.productionInfo->vials.empty());
    const auto output =
        std::filesystem::temp_directory_path() /
        ("opusoptima-vial-boundary-" + std::to_string(opusoptima::test::ProcessId()) + ".solution");
    struct Cleanup {
        std::filesystem::path path;
        ~Cleanup() {
            std::filesystem::remove(path);
        }
    } cleanup{output};
    {
        std::ofstream stream(output);
        stream << "preserve existing output";
    }
    opusoptima::io::SolutionData candidate;
    candidate.puzzleName = parsed.name;
    for (const bool publish : {false, true}) {
        const auto report = publish
                                ? opusoptima::io::PublishVerifiedSolution(puzzle, output, candidate)
                                : opusoptima::io::VerifySolutionFiles(puzzle, output);
        EXPECT_FALSE(report.Passed());
        EXPECT_TRUE(report.status == opusoptima::io::VerificationStatus::Unavailable ||
                    report.status == opusoptima::io::VerificationStatus::Rejected ||
                    report.status == opusoptima::io::VerificationStatus::CycleLimit);
        std::ifstream stream(output);
        std::string contents;
        std::getline(stream, contents);
        EXPECT_EQ(contents, "preserve existing output");
    }
}

TEST(ExternalVerifier, CycleBudgetIsEnforced) {
    const auto repo = GetRepoRoot();
    const auto report = opusoptima::io::VerifySolutionFiles(
        repo / "asset/puzzle/easy/easy-conduit.puzzle",
        repo / "asset/solution/easy/easy-conduit-easy-conduit-1.solution", 1);
    EXPECT_EQ(report.status, opusoptima::io::VerificationStatus::CycleLimit) << report.detail;
}

TEST(ExternalVerifier, RequiresAllSixProducts) {
    const auto repo = GetRepoRoot();
    const auto puzzlePath = repo / "asset/puzzle/easy/just-salt.puzzle";
    const auto puzzleBytes = opusoptima::io::BinaryParser::ReadAllBytes(puzzlePath);
    const auto puzzle = opusoptima::io::BinaryParser::ParsePuzzle(puzzleBytes);
    const auto output =
        std::filesystem::temp_directory_path() /
        ("opusoptima-six-products-" + std::to_string(opusoptima::test::ProcessId()) + ".solution");
    struct Cleanup {
        std::filesystem::path path;
        ~Cleanup() {
            std::filesystem::remove(path);
        }
    } cleanup{output};
    for (const int count : {1, 2, 5, 6}) {
        SCOPED_TRACE(count);
        auto solution = opusoptima::solvers::SingleAtomSolver{}.Solve(puzzle);
        auto& arm = solution.objects.front();
        const auto period = arm.instructions;
        arm.instructions.clear();
        for (int i = 0; i < count; ++i)
            for (auto instruction : period) {
                instruction.parameter += 4 * i;
                arm.instructions.push_back(instruction);
            }
        arm.instructions.push_back({'B', 4 * count});
        opusoptima::io::BinaryWriter::WriteSolutionFile(output, solution);
        const auto report = opusoptima::io::VerifySolutionFiles(puzzlePath, output, 100);
        EXPECT_EQ(report.Passed(), count == 6) << report.detail;
        if (count < 6) {
            EXPECT_EQ(report.status, opusoptima::io::VerificationStatus::CycleLimit)
                << report.detail;
        }
    }
}

TEST(ExternalVerifier, RejectsCollisionAfterTwoSuccessfulProducts) {
    const auto repo = GetRepoRoot();
    const auto puzzlePath = repo / "asset/puzzle/easy/just-salt.puzzle";
    const auto puzzle = opusoptima::io::BinaryParser::ParsePuzzleFile(puzzlePath);
    auto solution = opusoptima::solvers::SingleAtomSolver{}.Solve(puzzle);
    auto& arm = solution.objects.front();
    arm.instructions = {{'G', 0}, {'r', 1}, {'g', 2}, {'R', 3}, {'G', 4},  {'r', 5},
                        {'g', 6}, {'R', 7}, {'G', 8}, {'r', 9}, {'R', 10}, {'B', 11}};
    const auto output = std::filesystem::temp_directory_path() /
                        ("opusoptima-late-collision-" +
                         std::to_string(opusoptima::test::ProcessId()) + ".solution");
    struct Cleanup {
        std::filesystem::path path;
        ~Cleanup() {
            std::filesystem::remove(path);
        }
    } cleanup{output};
    opusoptima::io::BinaryWriter::WriteSolutionFile(output, solution);
    const auto report = opusoptima::io::VerifySolutionFiles(puzzlePath, output, 100);
    EXPECT_EQ(report.status, opusoptima::io::VerificationStatus::Rejected) << report.detail;
    EXPECT_GE(report.errorCycle, 8);
}

TEST(ExternalVerifier, EmptyMachineCannotCompleteOrReplaceExistingOutput) {
    const auto repo = GetRepoRoot();
    const auto puzzle = repo / "asset/puzzle/easy/just-salt.puzzle";
    auto solution = opusoptima::io::SolutionData{};
    solution.puzzleName = opusoptima::io::BinaryParser::ParsePuzzleFile(puzzle).name;
    const auto output =
        std::filesystem::temp_directory_path() /
        ("opusoptima-verifier-" + std::to_string(opusoptima::test::ProcessId()) + ".solution");
    struct Cleanup {
        std::filesystem::path path;
        ~Cleanup() {
            std::filesystem::remove(path);
        }
    } cleanup{output};
    {
        std::ofstream stream(output);
        stream << "preserve existing output";
    }
    const auto report = opusoptima::io::PublishVerifiedSolution(puzzle, output, solution);
    EXPECT_FALSE(report.Passed());
    std::ifstream stream(output);
    std::string contents;
    std::getline(stream, contents);
    EXPECT_EQ(contents, "preserve existing output");
    EXPECT_FALSE(opusoptima::io::VerifySolutionFiles(puzzle, output).Passed());
    EXPECT_FALSE(opusoptima::io::VerifySolutionFiles(puzzle, output, 0).Passed());
}
#else
TEST(ExternalVerifier, MissingLibraryFailsClosed) {
    EXPECT_EQ(opusoptima::io::VerifySolutionFiles("missing", "missing").status,
              opusoptima::io::VerificationStatus::Unavailable);
}
#endif

// Collision detection tests

TEST(CollisionLUTTest, SweepCellsReturnsCorrectCount) {
    const auto& lut = opusoptima::engine::CollisionLUT::Instance();
    const opusoptima::core::HexCoord base{.q = 0, .r = 0};

    // An arm of length 2 pointing direction 0 occupies cells (1,0) and (2,0)
    auto cells = lut.SweepCells(base, opusoptima::core::ArmDirection::D0, 2);
    EXPECT_EQ(cells.size(), 2U);
    if (cells.size() >= 2) {
        EXPECT_EQ(cells[0].q, 1);
        EXPECT_EQ(cells[0].r, 0);
        EXPECT_EQ(cells[1].q, 2);
        EXPECT_EQ(cells[1].r, 0);
    }
}

TEST(CollisionLUTTest, SweepLengthZeroReturnsEmpty) {
    const auto& lut = opusoptima::engine::CollisionLUT::Instance();
    auto cells = lut.SweepCells({0, 0}, opusoptima::core::ArmDirection::D0, 0);
    EXPECT_TRUE(cells.empty());
}
