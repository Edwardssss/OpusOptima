#include "AssemblySolver.hpp"
#include "ComposeAssembly.hpp"
#include "IO/BinaryWriter.hpp"
#include "IO/SolutionVerifier.hpp"
#include "RepoRoot.hpp"
#include <filesystem>
#include <fstream>
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

// A bonded salt lattice larger than the bounded arm search: 3 x 3 cells with every
// adjacent pair bonded.
io::PuzzleData SaltLattice() {
    io::PuzzleData p;
    p.version = 3;
    p.name = "generated-salt-lattice";
    p.permissionBits = (1ULL << 0) | (1ULL << 8) | (1ULL << 22) | (1ULL << 23);
    p.outputMultiplier = 1;
    p.inputs.push_back({{{core::AtomType::Salt, {0, 0}}}, {}});
    static constexpr core::HexCoord kSteps[3] = {{1, 0}, {0, 1}, {1, -1}};
    std::set<std::pair<int, int>> cells;
    p.outputs.emplace_back();
    for (int q = 0; q < 3; ++q)
        for (int r = 0; r < 3; ++r) {
            cells.insert({q, r});
            p.outputs.front().atoms.push_back({core::AtomType::Salt, {q, r}});
        }
    for (const auto& [q, r] : cells)
        for (const auto& step : kSteps) {
            const std::pair<int, int> next{q + step.q, r + step.r};
            if (cells.count(next) && std::make_pair(q, r) < next)
                p.outputs.front().bonds.push_back(
                    {1, {q, r}, {next.first, next.second}});
        }
    return p;
}

void WritePuzzle(const std::filesystem::path& path, const io::PuzzleData& p) {
    std::ofstream out(path, std::ios::binary);
    const auto word = [&](std::uint64_t value, int bytes) {
        for (int i = 0; i < bytes; ++i) out.put(static_cast<char>(value >> (8 * i)));
    };
    word(3, 4);
    word(p.name.size(), 1);
    out.write(p.name.data(), static_cast<std::streamsize>(p.name.size()));
    word(0, 8);
    word(p.permissionBits, 8);
    const auto list = [&](const std::vector<io::Molecule>& molecules) {
        word(molecules.size(), 4);
        for (const auto& m : molecules) {
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
    };
    list(p.inputs);
    list(p.outputs);
    word(p.outputMultiplier, 4);
    word(0, 1);
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
    EXPECT_FALSE(ComposedAssemblyUsesInPlaceLayout(puzzle));
    const auto variants = ComposeAssemblyVariants(puzzle);
    ASSERT_FALSE(variants.empty());
    for (const auto& machine : variants) EXPECT_GE(Count(machine, "arm1"), 1U);
}

TEST(ComposeGlyphStationTest, LargeBondedLatticeUsesInPlaceStations) {
    const auto puzzle = SaltLattice();
    EXPECT_TRUE(SupportsComposedAssembly(puzzle));
    EXPECT_TRUE(ComposedAssemblyUsesInPlaceLayout(puzzle));
    const auto machine = ComposeAssemblyMachine(puzzle);
    EXPECT_EQ(Count(machine, "arm1"), 0U);
    EXPECT_EQ(Count(machine, "input"), puzzle.outputs.front().atoms.size());
    EXPECT_EQ(Count(machine, "bonder"), puzzle.outputs.front().bonds.size());
    EXPECT_EQ(Count(machine, "out-std"), 1U);
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
    // Large direct lattice: the in-place layout has no arms and bonds everything in
    // parallel, so it must verify natively as well.
    {
        const auto puzzlePath = root / "build/compose-glyph-lattice.puzzle";
        WritePuzzle(puzzlePath, SaltLattice());
        auto solution = ComposeAssemblyMachine(SaltLattice());
        io::BinaryWriter::WriteSolutionFile(path, solution);
        auto report = io::VerifySolutionFiles(puzzlePath, path, 200000, 10000);
        ASSERT_TRUE(report.Passed()) << report.detail << " cycle=" << report.errorCycle;
        solution.metrics = report.metrics;
        io::BinaryWriter::WriteSolutionFile(path, solution);
        EXPECT_TRUE(io::VerifySolutionFiles(puzzlePath, path, 200000, 10000).Passed());
        std::error_code ec;
        std::filesystem::remove(puzzlePath, ec);
    }
}
#endif

} // namespace opusoptima::solvers
