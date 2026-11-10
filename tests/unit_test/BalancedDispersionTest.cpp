#include "AssemblySolver.hpp"
#include "IO/BinaryWriter.hpp"
#include "IO/SolutionVerifier.hpp"
#include "RepoRoot.hpp"
#include <filesystem>
#include <gtest/gtest.h>

namespace opusoptima::solvers {
io::SolutionData CompileBalancedDispersion(const io::PuzzleData&);
namespace {
const auto root = opusoptima::test::RepoRoot(__FILE__);
TEST(BalancedDispersionTest, RejectsMalformedProductsAndPermissions) {
    const auto original =
        io::BinaryParser::ParsePuzzleFile(root / "asset/puzzle/journal/journal4/P069.puzzle");
    for (int bit : {0, 3, 8, 12, 18, 22, 23}) {
        auto p = original;
        p.permissionBits &= ~(1ULL << bit);
        EXPECT_THROW(CompileBalancedDispersion(p), std::invalid_argument);
    }
    auto p = original;
    p.outputs[0].bonds[0].from = {50, 50};
    EXPECT_THROW(CompileBalancedDispersion(p), std::invalid_argument);
    p = original;
    p.outputs[0].bonds[0].to = p.outputs[0].bonds[0].from;
    EXPECT_THROW(CompileBalancedDispersion(p), std::invalid_argument);
    p = original;
    p.outputs[1] = p.outputs[0];
    EXPECT_THROW(CompileBalancedDispersion(p), std::invalid_argument);
    for (int multiplier : {0, 9}) {
        p = original;
        p.outputMultiplier = multiplier;
        EXPECT_THROW(CompileBalancedDispersion(p), std::invalid_argument);
    }
}
#ifdef HAS_LIBVERIFY
TEST(BalancedDispersionTest, ExistingJournalProductSetVerifiesTwice) {
    const auto input = root / "asset/puzzle/journal/journal4/P069.puzzle";
    auto solution = CompileBalancedDispersion(io::BinaryParser::ParsePuzzleFile(input));
    const auto output = std::filesystem::temp_directory_path() / "opus-balanced-P069.solution";
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
