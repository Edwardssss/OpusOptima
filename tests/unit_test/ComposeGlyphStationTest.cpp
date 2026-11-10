#include "AssemblySolver.hpp"
#include "ComposeAssembly.hpp"
#include "IO/BinaryWriter.hpp"
#include "IO/SolutionVerifier.hpp"
#include "RepoRoot.hpp"
#include <filesystem>
#include <set>
#include <utility>
#include <gtest/gtest.h>

namespace opusoptima::solvers {
namespace {
constexpr const char* kFacePowder = "asset/puzzle/campaign/ch1-and-prologue/P009.puzzle";
constexpr const char* kStabilizedWater = "asset/puzzle/campaign/ch1-and-prologue/P007.puzzle";
constexpr const char* kRefinedGold = "asset/puzzle/campaign/ch1-and-prologue/P010.puzzle";

std::size_t Count(const io::SolutionData& machine, const char* type) {
    std::size_t count = 0;
    for (const auto& object : machine.objects)
        if (object.typeName == type) ++count;
    return count;
}
} // namespace

TEST(ComposeGlyphStationTest, CalcificationAndProjectionAreInDomain) {
    const auto root = opusoptima::test::RepoRoot(__FILE__);
    for (const auto* file : {kFacePowder, kStabilizedWater, kRefinedGold}) {
        SCOPED_TRACE(file);
        const auto puzzle = io::BinaryParser::ParsePuzzleFile(root / file);
        EXPECT_TRUE(SupportsComposedAssembly(puzzle));
        EXPECT_TRUE(ComposedAssemblyRequiresChemistry(puzzle));
    }
}

TEST(ComposeGlyphStationTest, CalcificationPutsTheGlyphOnTheProductCell) {
    const auto root = opusoptima::test::RepoRoot(__FILE__);
    const auto puzzle = io::BinaryParser::ParsePuzzleFile(root / kFacePowder);
    const auto variants = ComposeAssemblyVariants(puzzle);
    ASSERT_FALSE(variants.empty());
    const auto& machine = variants.front();
    ASSERT_EQ(Count(machine, "glyph-calcification"), 1U);
    // The salt station is a calcification glyph with its own input area on the same
    // cell; the second product atom is lifted straight from a fire source.
    EXPECT_EQ(Count(machine, "input"), 2U);
    EXPECT_EQ(Count(machine, "bonder"), 1U);
    EXPECT_EQ(Count(machine, "out-std"), 1U);
    EXPECT_EQ(Count(machine, "arm1"), 0U);
    bool glyphOnInput = false;
    for (const auto& object : machine.objects) {
        if (object.typeName != "glyph-calcification") continue;
        for (const auto& other : machine.objects)
            if (other.typeName == "input" && other.position == object.position) glyphOnInput = true;
    }
    EXPECT_TRUE(glyphOnInput);
}

TEST(ComposeGlyphStationTest, ProjectionFeedsQuicksilverBesideTheMetalCell) {
    const auto root = opusoptima::test::RepoRoot(__FILE__);
    const auto puzzle = io::BinaryParser::ParsePuzzleFile(root / kRefinedGold);
    const auto variants = ComposeAssemblyVariants(puzzle);
    ASSERT_FALSE(variants.empty());
    const auto& machine = variants.front();
    ASSERT_EQ(Count(machine, "glyph-projection"), 1U);
    ASSERT_EQ(Count(machine, "input"), 2U);
    EXPECT_EQ(Count(machine, "arm1"), 0U);
    const io::SolutionObject* glyph = nullptr;
    for (const auto& object : machine.objects)
        if (object.typeName == "glyph-projection") glyph = &object;
    ASSERT_NE(glyph, nullptr);
    // Projection consumes quicksilver on the glyph cell and transforms the metal on
    // its (1, 0) cell; both are input areas, the metal one is the product cell.
    std::set<std::pair<int, int>> cells;
    for (const auto& object : machine.objects)
        if (object.typeName == "input")
            cells.insert({object.position.q, object.position.r});
    EXPECT_TRUE(cells.count({glyph->position.q, glyph->position.r}));
    EXPECT_TRUE(cells.count({glyph->position.q + 1, glyph->position.r}));
}

TEST(ComposeGlyphStationTest, DirectPuzzlesKeepTheArmMachine) {
    const auto root = opusoptima::test::RepoRoot(__FILE__);
    const auto puzzle = io::BinaryParser::ParsePuzzleFile(root / "asset/puzzle/easy/just-salt.puzzle");
    EXPECT_FALSE(ComposedAssemblyRequiresChemistry(puzzle));
    const auto variants = ComposeAssemblyVariants(puzzle);
    ASSERT_FALSE(variants.empty());
    for (const auto& machine : variants) EXPECT_GE(Count(machine, "arm1"), 1U);
}

#ifdef HAS_LIBVERIFY
TEST(ComposeGlyphStationTest, CalcificationAndProjectionMachinesVerifyTwice) {
    const auto root = opusoptima::test::RepoRoot(__FILE__);
    const auto path = root / "build/compose-glyph-test.solution";
    struct Cleanup {
        std::filesystem::path path;
        ~Cleanup() {
            std::error_code ec;
            std::filesystem::remove(path, ec);
        }
    } cleanup{path};
    for (const auto* file : {kFacePowder, kStabilizedWater, kRefinedGold}) {
        SCOPED_TRACE(file);
        const auto puzzlePath = root / file;
        const auto puzzle = io::BinaryParser::ParsePuzzleFile(puzzlePath);
        auto solution = ComposeAssemblyMachine(puzzle);
        io::BinaryWriter::WriteSolutionFile(path, solution);
        auto report = io::VerifySolutionFiles(puzzlePath, path, 200000, 10000);
        ASSERT_TRUE(report.Passed()) << report.detail << " cycle=" << report.errorCycle;
        solution.metrics = report.metrics;
        io::BinaryWriter::WriteSolutionFile(path, solution);
        EXPECT_TRUE(io::VerifySolutionFiles(puzzlePath, path, 200000, 10000).Passed());
    }
}
#endif

} // namespace opusoptima::solvers
