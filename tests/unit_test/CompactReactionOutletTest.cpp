#include "TestOutputDirectory.hpp"
#include "IO/BinaryWriter.hpp"
#include "IO/SolutionVerifier.hpp"
#include "LifeDeath.hpp"
#include "RowAssembly.hpp"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include "RepoRoot.hpp"
namespace opusoptima::solvers { namespace {
const auto root = opusoptima::test::RepoRoot(__FILE__);
void GateCompact(const io::PuzzleData& puzzle, const std::string& stem, bool nativeGate) {
    const test::ScopedTestDirectory scratch;
    const auto& dir = scratch.Path();
    std::filesystem::create_directories(dir);
    const auto path = dir / (stem + ".puzzle");
    {
        std::ofstream out(path, std::ios::binary);
        auto word = [&](std::uint64_t v, int bytes) {
            for (int i = 0; i < bytes; ++i) out.put(static_cast<char>(v >> (8 * i)));
        };
        word(3, 4); word(puzzle.name.size(), 1);
        out.write(puzzle.name.data(), puzzle.name.size());
        word(0, 8); word(puzzle.permissionBits, 8);
        for (const auto* list : {&puzzle.inputs, &puzzle.outputs}) {
            word(list->size(), 4);
            for (const auto& molecule : *list) {
                word(molecule.atoms.size(), 4);
                for (const auto& atom : molecule.atoms) {
                    word(static_cast<std::uint8_t>(atom.type), 1);
                    word(atom.position.q, 1); word(atom.position.r, 1);
                }
                word(molecule.bonds.size(), 4);
                for (const auto& bond : molecule.bonds) {
                    word(bond.bits, 1); word(bond.from.q, 1); word(bond.from.r, 1);
                    word(bond.to.q, 1); word(bond.to.r, 1);
                }
            }
        }
        word(1, 4); word(0, 1);
    }
    const auto parsed = io::BinaryParser::ParsePuzzleFile(path);
    for (int mode = 0; mode < 2; ++mode) {
        const auto solution = mode ? CompileCompactSpareChemistry(parsed) : CompileLifeDeath(parsed);
        const auto file = dir / (stem + "-" + std::to_string(mode) + ".solution");
        io::BinaryWriter::WriteSolutionFile(file, solution);
        if (nativeGate) {
            const auto report = io::VerifySolutionFiles(path, file, 150000, 10000);
            std::ofstream(dir / (stem + "-" + std::to_string(mode) + ".txt"))
                << "passed=" << report.Passed() << " cost=" << report.metrics.cost
                << " cycles=" << report.metrics.cycles << " area=" << report.metrics.area
                << " " << report.detail << '\n';
            EXPECT_TRUE(report.Passed()) << stem << " mode=" << mode << " " << report.detail;
        }
    }
}
void RunActualChangedDemandAndHoldout(bool nativeGate) {
    auto puzzle = io::BinaryParser::ParsePuzzleFile(root / "asset/puzzle/campaign/ch4/P032.puzzle");
    GateCompact(puzzle, "actual", nativeGate);
    auto ratio = puzzle;
    for (auto& output : ratio.outputs) {
        auto anchor = output.atoms.front().position;
        auto extra = anchor;
        --extra.q;
        while (std::any_of(output.atoms.begin(), output.atoms.end(), [&](const auto& a) {
            return a.position.q == extra.q && a.position.r == extra.r;
        })) --extra.q;
        anchor = extra; ++anchor.q;
        output.atoms.push_back({core::AtomType::Water, extra});
        output.bonds.push_back({1, anchor, extra});
    }
    GateCompact(ratio, "changed-demand", nativeGate);
    auto holdout = puzzle;
    auto rotate = [](auto& p) { for (int i = 0; i < 2; ++i) { int q = p.q; p.q = -p.r; p.r += q; } };
    for (auto& output : holdout.outputs) {
        for (auto& a : output.atoms) rotate(a.position);
        for (auto& b : output.bonds) { rotate(b.from); rotate(b.to); }
    }
    std::reverse(holdout.outputs.begin(), holdout.outputs.end());
    GateCompact(holdout, "holdout", nativeGate);
}
TEST(CompactReactionOutletTest, ActualChangedDemandAndHoldoutSemantics) { RunActualChangedDemandAndHoldout(false); }
#ifdef HAS_LIBVERIFY
TEST(CompactReactionOutletTest, ActualChangedDemandAndHoldout) { RunActualChangedDemandAndHoldout(true); }
#endif
TEST(CompactReactionOutletTest, MolecularExtractionDoesNotRebuildCompactPortfolio) {
    auto puzzle = io::BinaryParser::ParsePuzzleFile(root / "asset/puzzle/journal/journal8/P102.puzzle");
    puzzle.name = "renamed-molecular-extraction";
    EXPECT_THROW(CompileCompactSpareChemistry(puzzle), std::invalid_argument);
}
TEST(CompactReactionOutletTest, SemanticEligibilityIgnoresNamesAndGeometry) {
    auto puzzle = io::BinaryParser::ParsePuzzleFile(root / "asset/puzzle/campaign/ch4/P032.puzzle");
    ASSERT_TRUE(SupportsCompactSpareChemistry(puzzle));
    puzzle.name = "unrelated-name";
    std::reverse(puzzle.outputs.begin(), puzzle.outputs.end());
    for (auto& output : puzzle.outputs) {
        for (auto& atom : output.atoms) atom.position.q += 7;
        for (auto& bond : output.bonds) { bond.from.q += 7; bond.to.q += 7; }
    }
    EXPECT_TRUE(SupportsCompactSpareChemistry(puzzle));
    puzzle.outputs.front().atoms.push_back({core::AtomType::Water, {99, 99}});
    EXPECT_TRUE(SupportsCompactSpareChemistry(puzzle));
    for (auto& output : puzzle.outputs)
        for (auto& atom : output.atoms)
            if (atom.type == core::AtomType::Vitae || atom.type == core::AtomType::Mors)
                atom.type = core::AtomType::Water;
    EXPECT_FALSE(SupportsCompactSpareChemistry(puzzle));
}
TEST(CompactReactionOutletTest, PermissionsRemainMandatory) {
    const auto puzzle = io::BinaryParser::ParsePuzzleFile(root / "asset/puzzle/campaign/ch4/P032.puzzle");
    for (int bit : {2, 3, 8, 12, 16}) {
        auto denied = puzzle;
        denied.permissionBits &= ~(1ULL << bit);
        EXPECT_THROW(CompileCompactSpareChemistry(denied), std::invalid_argument) << bit;
    }
}
} }
