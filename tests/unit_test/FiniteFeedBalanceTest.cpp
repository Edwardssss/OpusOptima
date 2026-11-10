#include "IO/BinaryWriter.hpp"
#include "IO/SolutionVerifier.hpp"
#include "RepoRoot.hpp"
#include "RowAssembly.hpp"
#include "TestOutputDirectory.hpp"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>

namespace opusoptima::solvers { namespace {
const auto root = opusoptima::test::RepoRoot(__FILE__);
void GateVariant(const io::PuzzleData& puzzle, const std::string& stem, bool nativeGate) {
    const test::ScopedTestDirectory scratch;
    const auto& dir = scratch.Path();
    std::filesystem::create_directories(dir);
    const auto path = dir / (stem + ".puzzle");
    {
        std::ofstream out(path, std::ios::binary);
        auto word = [&](std::uint64_t v, int bytes) {
            for (int i = 0; i < bytes; ++i)
                out.put(static_cast<char>(v >> (8 * i)));
        };
        word(3, 4);
        word(puzzle.name.size(), 1);
        out.write(puzzle.name.data(), puzzle.name.size());
        word(0, 8);
        word(puzzle.permissionBits, 8);
        for (const auto* list : {&puzzle.inputs, &puzzle.outputs}) {
            word(list->size(), 4);
            for (const auto& molecule : *list) {
                word(molecule.atoms.size(), 4);
                for (const auto& atom : molecule.atoms) {
                    word(static_cast<std::uint8_t>(atom.type), 1);
                    word(atom.position.q, 1);
                    word(atom.position.r, 1);
                }
                word(molecule.bonds.size(), 4);
                for (const auto& bond : molecule.bonds) {
                    word(bond.bits, 1);
                    word(bond.from.q, 1);
                    word(bond.from.r, 1);
                    word(bond.to.q, 1);
                    word(bond.to.r, 1);
                }
            }
        }
        word(1, 4);
        word(0, 1);
    }
    const auto parsed = io::BinaryParser::ParsePuzzleFile(path);
    const auto batch = CompileMixedInventoryAssembly(parsed, true, true, true, true);
    ExecutionIR::RequireComplete(batch.ledger.Replay(parsed), {6, 6});
    const auto solution = dir / (stem + ".solution");
    io::BinaryWriter::WriteSolutionFile(solution, batch.solution);
    if (nativeGate) {
        const auto report = io::VerifySolutionFiles(path, solution, 150000, 10000);
        std::ofstream(dir / (stem + ".txt"))
            << "passed=" << report.Passed() << " cost=" << report.metrics.cost
            << " cycles=" << report.metrics.cycles << " area=" << report.metrics.area << " "
            << report.detail << '\n';
        EXPECT_TRUE(report.Passed()) << stem << report.detail;
    }
}
void RunActualAndDifferentOriginCompleteGate(bool nativeGate) {
    const test::ScopedTestDirectory scratch;
    const auto& dir = scratch.Path();
    std::filesystem::create_directories(dir);
    for (const auto& relative :
         {"asset/puzzle/campaign/ch4/P032.puzzle", "asset/puzzle/journal/journal8/P102.puzzle"}) {
        const auto path = root / relative;
        const auto puzzle = io::BinaryParser::ParsePuzzleFile(path);
        for (int mode = 0; mode < 2; ++mode) {
            const auto stem = path.stem().string() + "-" + std::to_string(mode);
            const auto batch = CompileMixedInventoryAssembly(puzzle, mode, mode, mode, mode);
            const auto state = batch.ledger.Replay(puzzle);
            ExecutionIR::RequireComplete(state, {6, 6});
            EXPECT_EQ(state.discarded, 0);
            EXPECT_TRUE(state.stored.empty());
            if (path.stem() == "P032") {
                EXPECT_EQ(state.batches[0] + state.batches[1], 48);
                EXPECT_EQ(batch.emissions.size(), 48);
            }
            const auto solution = dir / (stem + ".solution");
            io::BinaryWriter::WriteSolutionFile(solution, batch.solution);
            if (nativeGate) {
                const auto report = io::VerifySolutionFiles(path, solution, 150000, 10000);
                std::ofstream(dir / (stem + ".txt"))
                    << "passed=" << report.Passed() << " cost=" << report.metrics.cost
                    << " cycles=" << report.metrics.cycles << " area=" << report.metrics.area
                    << "\n"
                    << report.detail << " cycle=" << report.errorCycle << '\n';
                EXPECT_TRUE(report.Passed()) << stem << ": " << report.detail;
            }
        }
    }
}
TEST(FiniteFeedBalanceTest, ActualAndDifferentOriginCompleteGateSemantics) {
    RunActualAndDifferentOriginCompleteGate(false);
}
#ifdef HAS_LIBVERIFY
TEST(FiniteFeedBalanceTest, ActualAndDifferentOriginCompleteGate) {
    RunActualAndDifferentOriginCompleteGate(true);
}
#endif
void RunAllocationTransformPermissionsAndClosedStock(bool nativeGate) {
    auto puzzle = io::BinaryParser::ParsePuzzleFile(root / "asset/puzzle/campaign/ch4/P032.puzzle");
    auto transformed = puzzle;
    std::reverse(transformed.inputs.begin(), transformed.inputs.end());
    std::reverse(transformed.outputs.begin(), transformed.outputs.end());
    for (auto& output : transformed.outputs) {
        for (auto& atom : output.atoms)
            atom.position = core::RotateClockwise(atom.position);
        for (auto& bond : output.bonds) {
            bond.from = core::RotateClockwise(bond.from);
            bond.to = core::RotateClockwise(bond.to);
        }
    }
    GateVariant(transformed, "transformed", nativeGate);
    for (int bit : {2, 3, 9, 12, 16}) {
        auto denied = puzzle;
        denied.permissionBits &= ~(1ULL << bit);
        EXPECT_THROW(CompileMixedInventoryAssembly(denied), std::invalid_argument);
    }
    auto unbalanced = puzzle;
    unbalanced.outputs.pop_back();
    EXPECT_THROW(CompileMixedInventoryAssembly(unbalanced), std::invalid_argument);
    auto metal = puzzle;
    metal.inputs[0].atoms[0].type = core::AtomType::Gold;
    EXPECT_THROW(CompileMixedInventoryAssembly(metal), std::invalid_argument);
    auto absent = puzzle;
    for (auto& source : absent.inputs)
        source.atoms[0].type = core::AtomType::Earth;
    EXPECT_THROW(CompileMixedInventoryAssembly(absent), std::invalid_argument);
    auto coproduct = puzzle;
    for (auto& source : coproduct.inputs) {
        source.atoms.push_back({core::AtomType::Earth, {1, 0}});
        source.bonds.push_back({1, {0, 0}, {1, 0}});
    }
    GateVariant(coproduct, "consumed-coproduct", nativeGate);
    for (auto& source : coproduct.inputs) {
        source.atoms.push_back({core::AtomType::Earth, {2, 0}});
        source.bonds.push_back({1, {1, 0}, {2, 0}});
    }
    EXPECT_THROW(CompileMixedInventoryAssembly(coproduct), std::invalid_argument);
    auto oversized = puzzle;
    oversized.outputs[0].atoms.resize(513);
    EXPECT_THROW(CompileMixedInventoryAssembly(oversized), std::invalid_argument);
    // Distinct bonded sources and ratio: eight waters and two salts per pair.
    auto ratio = puzzle;
    ratio.inputs[0].atoms.push_back({core::AtomType::Salt, {1, 0}});
    ratio.inputs[0].bonds.push_back({1, {0, 0}, {1, 0}});
    ratio.inputs[1].atoms.push_back({core::AtomType::Water, {1, 0}});
    ratio.inputs[1].bonds.push_back({1, {0, 0}, {1, 0}});
    GateVariant(ratio, "different-ratio", nativeGate);
}
TEST(FiniteFeedBalanceTest, AllocationTransformPermissionsAndClosedStockSemantics) {
    RunAllocationTransformPermissionsAndClosedStock(false);
}
#ifdef HAS_LIBVERIFY
TEST(FiniteFeedBalanceTest, AllocationTransformPermissionsAndClosedStock) {
    RunAllocationTransformPermissionsAndClosedStock(true);
}
#endif
}} // namespace opusoptima::solvers
