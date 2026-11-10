#include "MolecularMetalSplit.hpp"
#include "AssemblySolver.hpp"
#include "ChemicalPlanner.hpp"
#include "IO/BinaryWriter.hpp"
#include "IO/SolutionVerifier.hpp"
#include "RepoRoot.hpp"
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>

namespace opusoptima::solvers { namespace {
const auto root = opusoptima::test::RepoRoot(__FILE__);
io::PuzzleData Fixture() {
    auto p = io::BinaryParser::ParsePuzzleFile(root / "asset/puzzle/tourney-2019/weekEX.puzzle");
    p.outputs.resize(2);
    p.outputs[0].atoms = {{core::AtomType::Copper, {0, 0}}};
    p.outputs[1].atoms = {{core::AtomType::Water, {0, 0}}};
    for (auto& o : p.outputs)
        o.bonds.clear();
    return p;
}
TEST(MolecularMetalSplitTest, PanaceaSixProductExactMaterialBalance) {
    const auto p =
        io::BinaryParser::ParsePuzzleFile(root / "asset/puzzle/tourney-2019/weekEX.puzzle");
    const auto recipe = ChemicalPlanner::Plan(p, 6, false);
    ASSERT_EQ(recipe.inputBatches.size(), 1U);
    EXPECT_EQ(recipe.inputBatches[0], 24U);
    EXPECT_EQ(recipe.calcifications, 18U);
    EXPECT_EQ(recipe.purifications, 12U);
    EXPECT_EQ(recipe.animismus, 6U);
    EXPECT_EQ(recipe.projections, 0U);
    EXPECT_EQ(recipe.duplications, 0U);
    for (auto surplus : recipe.surplus)
        EXPECT_EQ(surplus, 0U);
    for (int bit : {12, 15, 16}) {
        auto invalid = p;
        invalid.permissionBits &= ~(1ULL << bit);
        EXPECT_THROW((void)ChemicalPlanner::Plan(invalid, 6, false), std::invalid_argument);
    }
}

TEST(MolecularMetalSplitTest, RejectsUnsupportedChemistryAndPermissions) {
    const auto p = Fixture();
    for (int bit : {0, 2, 9, 15, 22, 23}) {
        auto invalid = p;
        invalid.permissionBits &= ~(1ULL << bit);
        EXPECT_THROW((void)CompileMolecularMetalSplit(invalid), std::invalid_argument);
    }
    auto invalid = p;
    for (int multiplier : {0, 2}) {
        invalid.outputMultiplier = multiplier;
        EXPECT_THROW((void)CompileMolecularMetalSplit(invalid), std::invalid_argument);
    }
    invalid = p;
    invalid.inputs[0].bonds[0].to = {4, 0};
    EXPECT_THROW((void)CompileMolecularMetalSplit(invalid), std::invalid_argument);
    invalid = p;
    invalid.inputs[0].atoms[1].position = {2, 0};
    invalid.inputs[0].bonds[0].to = {2, 0};
    EXPECT_THROW((void)CompileMolecularMetalSplit(invalid), std::invalid_argument);
    invalid = p;
    invalid.inputs[0].bonds[0].bits = 14;
    EXPECT_THROW((void)CompileMolecularMetalSplit(invalid), std::invalid_argument);
    invalid = p;
    invalid.outputs[0].atoms[0].type = core::AtomType::Gold;
    EXPECT_THROW((void)CompileMolecularMetalSplit(invalid), std::invalid_argument);
}
#ifdef HAS_LIBVERIFY
TEST(MolecularMetalSplitTest, SixProductsWithoutTracksOrDisposal) {
    for (int rotation = 0; rotation < 6; ++rotation) {
        SCOPED_TRACE(rotation);
        auto p = Fixture();
        p.permissionBits &= ~((1ULL << 3) | (1ULL << 17));
        auto rotate = [&](core::HexCoord c) {
            for (int i = 0; i < rotation; ++i)
                c = {-c.r, c.q + c.r};
            return core::HexCoord{c.q + 2, c.r - 3};
        };
        for (auto& a : p.inputs[0].atoms)
            a.position = rotate(a.position);
        for (auto& b : p.inputs[0].bonds) {
            b.from = rotate(b.from);
            b.to = rotate(b.to);
        }
        if (rotation % 2) {
            std::swap(p.inputs[0].atoms[0], p.inputs[0].atoms[1]);
            std::swap(p.outputs[0], p.outputs[1]);
        }
        const auto puzzle = std::filesystem::current_path() / "metal-split-test.puzzle";
        const auto path = std::filesystem::current_path() / "metal-split-test.solution";
        struct Cleanup {
            std::filesystem::path puzzle, solution;
            ~Cleanup() {
                std::error_code ec;
                std::filesystem::remove(puzzle, ec);
                std::filesystem::remove(solution, ec);
            }
        } cleanup{puzzle, path};
        {
            std::ofstream out(puzzle, std::ios::binary);
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
                        word(static_cast<int>(a.type), 1);
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
        auto solution = AssemblySolver{}.Solve(p);
        for (const auto& object : solution.objects)
            EXPECT_NE(object.typeName, "track");
        io::BinaryWriter::WriteSolutionFile(path, solution);
        auto report = io::VerifySolutionFiles(puzzle, path);
        ASSERT_TRUE(report.Passed()) << report.detail << " cycle=" << report.errorCycle
                                     << " at=" << report.errorU << "," << report.errorV;
        solution.metrics = report.metrics;
        io::BinaryWriter::WriteSolutionFile(path, solution);
        report = io::VerifySolutionFiles(puzzle, path);
        ASSERT_TRUE(report.Passed()) << report.detail;
    }
}
#endif
}} // namespace opusoptima::solvers
