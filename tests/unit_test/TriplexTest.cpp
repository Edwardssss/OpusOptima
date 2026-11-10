#include "Triplex.hpp"
#include "BinaryWriter.hpp"
#include "RepoRoot.hpp"
#include "SolutionVerifier.hpp"
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <iostream>

namespace {
using namespace opusoptima;
const auto root = opusoptima::test::RepoRoot(__FILE__);
io::PuzzleData Phial() {
    return io::BinaryParser::ParsePuzzleFile(root / "asset/puzzle/campaign/ch2/P017.puzzle");
}
TEST(TriplexTest, RejectsUnsupportedInputsAndPermissions) {
    auto p = Phial();
    for (const int bit : {0, 2, 3, 8, 11, 12, 22, 23}) {
        auto missing = p;
        missing.permissionBits &= ~(1ULL << bit);
        EXPECT_THROW((void)solvers::CompileTriplex(missing), std::invalid_argument) << bit;
    }
    auto partial = p;
    for (auto& b : partial.outputs[0].bonds)
        if (b.bits == 14) b.bits = 3;
    EXPECT_THROW((void)solvers::CompileTriplex(partial), std::invalid_argument);
    auto ordinary = p;
    for (auto& b : ordinary.outputs[0].bonds)
        b.bits = 1;
    EXPECT_THROW((void)solvers::CompileTriplex(ordinary), std::invalid_argument);
    p.outputs.push_back(p.outputs[0]);
    EXPECT_THROW((void)solvers::CompileTriplex(p), std::invalid_argument);
    auto production = Phial();
    production.productionInfo.emplace();
    EXPECT_THROW((void)solvers::CompileTriplex(production), std::invalid_argument);
    auto repeat = Phial();
    repeat.outputs[0].atoms[0].type = core::AtomType::Repeat;
    EXPECT_THROW((void)solvers::CompileTriplex(repeat), std::invalid_argument);
    auto bent = Phial();
    for (auto& atom : bent.outputs[0].atoms)
        if (atom.position == core::HexCoord{1, 0}) atom.position = {0, 1};
    for (auto& bond : bent.outputs[0].bonds) {
        if (bond.from == core::HexCoord{1, 0}) bond.from = {0, 1};
        if (bond.to == core::HexCoord{1, 0}) bond.to = {0, 1};
    }
    EXPECT_NO_THROW((void)solvers::CompileTriplex(bent));
    auto nonSingleton = Phial();
    nonSingleton.inputs[0].atoms.push_back(nonSingleton.inputs[0].atoms[0]);
    EXPECT_THROW((void)solvers::CompileTriplex(nonSingleton), std::invalid_argument);
    auto normalFire = Phial();
    for (auto& atom : normalFire.outputs[0].atoms)
        atom.type = core::AtomType::Fire;
    EXPECT_NO_THROW((void)solvers::CompileTriplex(normalFire));
}
TEST(TriplexTest, OriginalPostBondCalcification) {
    const auto p =
        io::BinaryParser::ParsePuzzleFile(root / "asset/puzzle/tourney-2021/OM2021_W1.puzzle");
    EXPECT_NO_THROW((void)solvers::CompileTriplex(p));
}
TEST(TriplexTest, NormalizesAllSixChainOrientations) {
    auto p = Phial();
    for (int rotation = 0; rotation < 6; ++rotation) {
        SCOPED_TRACE(rotation);
        EXPECT_NO_THROW((void)solvers::CompileTriplex(p));
        for (auto& a : p.outputs[0].atoms)
            a.position = core::RotateClockwise(a.position);
        for (auto& b : p.outputs[0].bonds) {
            b.from = core::RotateClockwise(b.from);
            b.to = core::RotateClockwise(b.to);
        }
    }
}
TEST(TriplexTest, CompilesMixedOrdinaryAndTriplexFireEdges) {
    const auto puzzle =
        io::BinaryParser::ParsePuzzleFile(root / "asset/puzzle/journal/journal8/P100.puzzle");
    EXPECT_NO_THROW((void)solvers::CompileTriplex(puzzle));
}
#ifdef HAS_LIBVERIFY
TEST(TriplexVerifierTest, EveryColorSubsetInAllSixOrientations) {
    const auto puzzlePath = root / "build/triplex-colors.puzzle";
    const auto solutionPath = root / "build/triplex-colors.solution";
    for (const int bits : {2, 4, 6, 8, 10, 12, 14}) {
        auto puzzle = Phial();
        for (auto& bond : puzzle.outputs[0].bonds)
            if (bond.bits == 14) bond.bits = bits;
        for (int rotation = 0; rotation < 6; ++rotation) {
            SCOPED_TRACE(std::to_string(bits) + ":" + std::to_string(rotation));
            {
                std::ofstream out(puzzlePath, std::ios::binary);
                auto word = [&](std::uint64_t v, int n) {
                    for (int i = 0; i < n; ++i)
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
                word(puzzle.outputMultiplier, 4);
                word(0, 1);
            }
            auto solution = solvers::CompileTriplex(puzzle);
            io::BinaryWriter::WriteSolutionFile(solutionPath, solution);
            const auto first = io::VerifySolutionFiles(puzzlePath, solutionPath, 150000, 10000);
            ASSERT_TRUE(first.Passed()) << first.detail << " cycle=" << first.errorCycle;
            solution.metrics = first.metrics;
            io::BinaryWriter::WriteSolutionFile(solutionPath, solution);
            ASSERT_TRUE(io::VerifySolutionFiles(puzzlePath, solutionPath, 150000, 10000).Passed());
            for (auto& atom : puzzle.outputs[0].atoms)
                atom.position = core::RotateClockwise(atom.position);
            for (auto& bond : puzzle.outputs[0].bonds) {
                bond.from = core::RotateClockwise(bond.from);
                bond.to = core::RotateClockwise(bond.to);
            }
        }
    }
    std::filesystem::remove(puzzlePath);
    std::filesystem::remove(solutionPath);
}
TEST(TriplexVerifierTest, RealBranchedAndDisassembledTargetsTwiceWithMetrics) {
    for (const auto& relative :
         {"asset/puzzle/journal/journal8/P100.puzzle", "asset/puzzle/campaign/ch5/P040.puzzle",
          "asset/puzzle/journal/journal9-p/P106.puzzle"}) {
        const auto puzzlePath = root / relative;
        SCOPED_TRACE(relative);
        auto puzzle = io::BinaryParser::ParsePuzzleFile(puzzlePath);
        auto solution = solvers::CompileTriplex(puzzle);
        const auto path = std::filesystem::current_path() /
                          ("triplex-regression-" + puzzlePath.stem().string() + ".solution");
        io::BinaryWriter::WriteSolutionFile(path, solution);
        const auto first = io::VerifySolutionFiles(puzzlePath, path, 150000, 10000);
        ASSERT_TRUE(first.Passed()) << first.detail << " cycle=" << first.errorCycle;
        solution.metrics = first.metrics;
        io::BinaryWriter::WriteSolutionFile(path, solution);
        const auto second = io::VerifySolutionFiles(puzzlePath, path, 150000, 10000);
        ASSERT_TRUE(second.Passed()) << second.detail;
        EXPECT_EQ(second.metrics.cycles, first.metrics.cycles);
        EXPECT_EQ(second.metrics.cost, first.metrics.cost);
        EXPECT_EQ(second.metrics.area, first.metrics.area);
        EXPECT_EQ(second.metrics.instructionCount, first.metrics.instructionCount);
        EXPECT_EQ(io::BinaryParser::ParseSolutionFile(path).metrics.cycles, first.metrics.cycles);
        std::cout << relative << " cycles=" << first.metrics.cycles
                  << " cost=" << first.metrics.cost << " area=" << first.metrics.area
                  << " instructions=" << first.metrics.instructionCount << '\n';
        std::filesystem::remove(path);
    }
}
TEST(TriplexVerifierTest, RealExplosivePhialTwiceWithMetrics) {
    auto puzzle = Phial();
    auto solution = solvers::CompileTriplex(puzzle);
    const auto path = std::filesystem::current_path() / "triplex-real-P017.solution";
    const auto puzzlePath = root / "asset/puzzle/campaign/ch2/P017.puzzle";
    io::BinaryWriter::WriteSolutionFile(path, solution);
    const auto report = io::VerifySolutionFiles(puzzlePath, path, 150000, 10000);
    ASSERT_TRUE(report.Passed()) << report.detail << " cycle=" << report.errorCycle << " at "
                                 << report.errorU << ',' << report.errorV;
    solution.metrics = report.metrics;
    io::BinaryWriter::WriteSolutionFile(path, solution);
    const auto second = io::VerifySolutionFiles(puzzlePath, path, 150000, 10000);
    ASSERT_TRUE(second.Passed()) << second.detail;
    EXPECT_EQ(second.metrics.cycles, report.metrics.cycles);
    EXPECT_EQ(second.metrics.cost, report.metrics.cost);
    EXPECT_EQ(second.metrics.area, report.metrics.area);
    EXPECT_EQ(second.metrics.instructionCount, report.metrics.instructionCount);
    EXPECT_EQ(io::BinaryParser::ParseSolutionFile(path).metrics.cycles, report.metrics.cycles);
    std::filesystem::remove(path);
    std::cout << "Triplex P017 cycles=" << report.metrics.cycles << " cost=" << report.metrics.cost
              << " area=" << report.metrics.area
              << " instructions=" << report.metrics.instructionCount << '\n';
}
#endif
} // namespace
