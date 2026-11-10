#include "TracklessPairChemistry.hpp"
#include "IO/SolutionVerifier.hpp"
#include <filesystem>
#include <gtest/gtest.h>
#include "RepoRoot.hpp"

namespace opusoptima::solvers {
#ifdef HAS_LIBVERIFY
TEST(TracklessPairChemistryTest, PanaceaSixProducts) {
    const auto root = opusoptima::test::RepoRoot(__FILE__);
    for (const auto* relative :
         {"tourney-2019/weekEX.puzzle", "workshop-versions/w1698789743.puzzle"}) {
        SCOPED_TRACE(relative);
        const auto puzzle = root / "asset/puzzle" / relative;
        const auto p = io::BinaryParser::ParsePuzzleFile(puzzle);
        const auto solution = CompileTracklessPairChemistry(p);
        for (const auto& object : solution.objects) {
            EXPECT_NE(object.typeName, "track");
            EXPECT_NE(object.typeName, "piston");
            EXPECT_NE(object.typeName, "glyph-disposal");
        }
        const auto path = root / "build/panacea-relay-test.solution";
        const auto report = io::PublishVerifiedSolution(puzzle, path, solution);
        ASSERT_TRUE(report.Passed()) << report.detail << " cycle=" << report.errorCycle;
        EXPECT_TRUE(io::VerifySolutionFiles(puzzle, path).Passed());
        std::filesystem::remove(path);
    }
}
#endif
TEST(TracklessPairChemistryTest, RejectsUnsupportedStructures) {
    const auto root = opusoptima::test::RepoRoot(__FILE__);
    const auto puzzle =
        io::BinaryParser::ParsePuzzleFile(root / "asset/puzzle/tourney-2019/weekEX.puzzle");
    auto p = puzzle;
    p.outputMultiplier = 2;
    EXPECT_THROW((void)CompileTracklessPairChemistry(p), std::invalid_argument);
    p = puzzle;
    p.inputs[0].bonds.clear();
    EXPECT_THROW((void)CompileTracklessPairChemistry(p), std::invalid_argument);
    p = puzzle;
    p.inputs[0].bonds[0].bits = 2;
    EXPECT_THROW((void)CompileTracklessPairChemistry(p), std::invalid_argument);
    p = puzzle;
    p.outputs.pop_back();
    EXPECT_THROW((void)CompileTracklessPairChemistry(p), std::invalid_argument);
}
TEST(TracklessPairChemistryTest, RejectsMissingChemistryPermissions) {
    const auto root = opusoptima::test::RepoRoot(__FILE__);
    const auto puzzle =
        io::BinaryParser::ParsePuzzleFile(root / "asset/puzzle/tourney-2019/weekEX.puzzle");
    for (const int bit : {12, 15, 16}) {
        auto p = puzzle;
        p.permissionBits &= ~(uint64_t{1} << bit);
        EXPECT_THROW((void)CompileTracklessPairChemistry(p), std::invalid_argument);
    }
}
} // namespace opusoptima::solvers
