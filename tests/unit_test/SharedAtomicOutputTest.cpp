#include "AssemblySolver.hpp"
#include "IO/BinaryWriter.hpp"
#include "IO/SolutionVerifier.hpp"
#include "RepoRoot.hpp"
#include <filesystem>
#include <gtest/gtest.h>

namespace opusoptima::solvers {
io::SolutionData CompileSharedAtomicOutputs(const io::PuzzleData&);
namespace {
const auto root = opusoptima::test::RepoRoot(__FILE__);
TEST(SharedAtomicOutputTest, RejectsUnsupportedOutputSets) {
    const auto original =
        io::BinaryParser::ParsePuzzleFile(root / "asset/puzzle/campaign/ch4/P031b.puzzle");
    auto puzzle = original;
    puzzle.outputs[1] = puzzle.outputs[0];
    EXPECT_THROW(CompileSharedAtomicOutputs(puzzle), std::invalid_argument);
    puzzle = original;
    puzzle.outputs[0].atoms[0].type = core::AtomType::Gold;
    EXPECT_THROW(CompileSharedAtomicOutputs(puzzle), std::invalid_argument);
    puzzle = original;
    puzzle.outputs.pop_back();
    EXPECT_THROW(CompileSharedAtomicOutputs(puzzle), std::invalid_argument);
    puzzle = original;
    puzzle.permissionBits &= ~(1ULL << 3);
    EXPECT_THROW(CompileSharedAtomicOutputs(puzzle), std::invalid_argument);
}
#ifdef HAS_LIBVERIFY
TEST(SharedAtomicOutputTest, ExistingNineFeedWheelVerifiesTwice) {
    const auto input = root / "asset/puzzle/tourney-2019/week10a.puzzle";
    auto solution = AssemblySolver{}.Solve(io::BinaryParser::ParsePuzzleFile(input));
    const auto output = std::filesystem::temp_directory_path() / "opus-nine-feed-wheel.solution";
    struct Cleanup {
        std::filesystem::path path;
        ~Cleanup() {
            std::filesystem::remove(path);
        }
    } cleanup{output};
    io::BinaryWriter::WriteSolutionFile(output, solution);
    auto report = io::VerifySolutionFiles(input, output, 150000, 10000);
    ASSERT_TRUE(report.Passed()) << report.detail << " cycle=" << report.errorCycle;
    solution.metrics = report.metrics;
    io::BinaryWriter::WriteSolutionFile(output, solution);
    EXPECT_TRUE(io::VerifySolutionFiles(input, output, 150000, 10000).Passed());
}
TEST(SharedAtomicOutputTest, ExistingLithargeSeparationVerifiesTwice) {
    const auto input = root / "asset/puzzle/campaign/ch4/P031b.puzzle";
    auto solution = CompileSharedAtomicOutputs(io::BinaryParser::ParsePuzzleFile(input));
    const auto output =
        std::filesystem::temp_directory_path() / "opus-shared-atomic-P031b.solution";
    struct Cleanup {
        std::filesystem::path path;
        ~Cleanup() {
            std::filesystem::remove(path);
        }
    } cleanup{output};
    io::BinaryWriter::WriteSolutionFile(output, solution);
    auto report = io::VerifySolutionFiles(input, output, 150000, 10000);
    ASSERT_TRUE(report.Passed()) << report.detail << " cycle=" << report.errorCycle;
    solution.metrics = report.metrics;
    io::BinaryWriter::WriteSolutionFile(output, solution);
    EXPECT_TRUE(io::VerifySolutionFiles(input, output, 150000, 10000).Passed());
}
#endif
} // namespace
} // namespace opusoptima::solvers
