#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>

#include "AssemblySolver.hpp"
#include "BinaryWriter.hpp"
#include "RepeatingOutput.hpp"
#include "RepoRoot.hpp"
#include "SolutionVerifier.hpp"

namespace {
using namespace opusoptima;
io::Molecule Chain() {
    return {{{core::AtomType::Salt, {0, 0}}, {core::AtomType::Repeat, {1, 0}}},
            {{1, {0, 0}, {1, 0}}}};
}
TEST(RepeatingOutputTest, ExpandsRealAtomsAndIntermonomerBonds) {
    const auto expanded = solvers::ExpandRepeatingOutput(Chain());
    ASSERT_EQ(expanded.atoms.size(), 6U);
    ASSERT_EQ(expanded.bonds.size(), 5U);
    for (int i = 0; i < 6; ++i) {
        EXPECT_EQ(expanded.atoms[i].type, core::AtomType::Salt);
        EXPECT_EQ(expanded.atoms[i].position, (core::HexCoord{i, 0}));
    }
}
TEST(RepeatingOutputTest, UsesMarkerRelativeToPuzzleOriginNotFirstAtom) {
    auto molecule = Chain();
    molecule.atoms = {{core::AtomType::Salt, {1, 0}}, {core::AtomType::Repeat, {0, 1}}};
    molecule.bonds = {{1, {1, 0}, {0, 1}}, {1, {1, 0}, {1, 1}}};
    EXPECT_THROW(solvers::ExpandRepeatingOutput(molecule), std::invalid_argument);
    // A two-atom monomer whose first atom is not the repeat origin.
    molecule.atoms = {{core::AtomType::Salt, {1, 0}},
                      {core::AtomType::Salt, {0, 0}},
                      {core::AtomType::Repeat, {2, 0}}};
    molecule.bonds = {{1, {0, 0}, {1, 0}}, {1, {1, 0}, {2, 0}}};
    const auto expanded = solvers::ExpandRepeatingOutput(molecule);
    EXPECT_EQ(expanded.atoms.size(), 12U);
    EXPECT_EQ(expanded.bonds.size(), 11U);
    EXPECT_EQ(expanded.atoms[2].position, (core::HexCoord{3, 0}));
}
TEST(RepeatingOutputTest, SupportsRotatedPolymer) {
    auto molecule = Chain();
    molecule.atoms[1].position = {0, 1};
    molecule.bonds[0].to = {0, 1};
    const auto expanded = solvers::ExpandRepeatingOutput(molecule);
    EXPECT_EQ(expanded.atoms.back().position, (core::HexCoord{0, 5}));
    EXPECT_EQ(expanded.bonds.size(), 5U);
}
TEST(RepeatingOutputTest, ReversedBondEndpointsPreserveBoundaryConnectivity) {
    auto molecule = Chain();
    std::swap(molecule.bonds[0].from, molecule.bonds[0].to);
    const auto expanded = solvers::ExpandRepeatingOutput(molecule);
    EXPECT_EQ(expanded.atoms.size(), 6U);
    EXPECT_EQ(expanded.bonds.size(), 5U);
}
TEST(RepeatingOutputTest, PreservesAndMergesColoredBoundaryBonds) {
    for (const int bits : {2, 4, 6, 8, 10, 12, 14}) {
        auto molecule = Chain();
        molecule.atoms[0].type = core::AtomType::Fire;
        molecule.bonds[0].bits = bits;
        const auto expanded = solvers::ExpandRepeatingOutput(molecule);
        ASSERT_EQ(expanded.bonds.size(), 5U);
        for (const auto& bond : expanded.bonds)
            EXPECT_EQ(bond.bits, bits);
    }
    auto molecule = Chain();
    molecule.bonds = {{2, {0, 0}, {1, 0}}, {8, {0, 0}, {1, 0}}};
    for (const auto& bond : solvers::ExpandRepeatingOutput(molecule).bonds)
        EXPECT_EQ(bond.bits, 10);
    molecule.bonds.push_back({1, {0, 0}, {1, 0}});
    EXPECT_THROW((void)solvers::ExpandRepeatingOutput(molecule), std::invalid_argument);
    for (const int bits : {0, 3, 16}) {
        molecule = Chain();
        molecule.bonds[0].bits = bits;
        EXPECT_THROW((void)solvers::ExpandRepeatingOutput(molecule), std::invalid_argument);
    }
    molecule = Chain();
    molecule.atoms[0].type = core::AtomType::Water;
    molecule.bonds[0].bits = 2;
    EXPECT_THROW((void)solvers::ExpandRepeatingOutput(molecule), std::invalid_argument);
}
TEST(RepeatingOutputTest, RejectsMalformedAndDisconnectedTargets) {
    auto molecule = Chain();
    molecule.atoms[1].position = {0, 0};
    EXPECT_THROW(solvers::ExpandRepeatingOutput(molecule), std::invalid_argument);
    molecule = Chain();
    molecule.bonds.clear();
    EXPECT_THROW(solvers::ExpandRepeatingOutput(molecule), std::invalid_argument);
    molecule = Chain();
    molecule.bonds[0].bits = 3;
    EXPECT_THROW(solvers::ExpandRepeatingOutput(molecule), std::invalid_argument);
    molecule = Chain();
    molecule.atoms.push_back({core::AtomType::Repeat, {2, 0}});
    EXPECT_THROW(solvers::ExpandRepeatingOutput(molecule), std::invalid_argument);
    molecule = Chain();
    molecule.atoms.push_back({core::AtomType::Salt, {2, 0}});
    EXPECT_THROW(solvers::ExpandRepeatingOutput(molecule), std::invalid_argument);
    molecule = Chain();
    molecule.atoms[1].position = {30, 0};
    molecule.bonds.clear();
    EXPECT_THROW(solvers::ExpandRepeatingOutput(molecule), std::invalid_argument);
}
#ifdef HAS_LIBVERIFY
TEST(RepeatingOutputVerifierTest, ExistingLocalPuzzlesAndMixedMultiplierOutput) {
    const auto root = opusoptima::test::RepoRoot(__FILE__);
    const auto solutionPath = root / "build/repeating-output-test.solution";
    for (const auto* file :
         {"asset/puzzle/easy/easy-infinite-product.puzzle",
          "asset/puzzle/easy/infinite-row-condition.puzzle",
          "asset/puzzle/easy/infinite-throughput.puzzle",
          "asset/puzzle/edge-cases/infinite-internal-bonds.puzzle",
          "asset/puzzle/workshop-versions/w2839120106.puzzle",
          "asset/puzzle/campaign/ch2/P020.puzzle", "asset/puzzle/journal/journal3/P067.puzzle",
          "asset/puzzle/tourney-2021/OM2021_W5.puzzle",
          "asset/puzzle/workshop-versions/w2946684529.puzzle"}) {
        SCOPED_TRACE(file);
        const auto puzzlePath = root / file;
        const auto puzzle = io::BinaryParser::ParsePuzzleFile(puzzlePath);
        auto solution = solvers::AssemblySolver{}.Solve(puzzle);
        EXPECT_TRUE(std::any_of(solution.objects.begin(), solution.objects.end(),
                                [](const auto& o) { return o.typeName == "out-rep"; }));
        for (const auto& object : solution.objects) {
            ASSERT_LT(object.instructions.size(), 100000U) << object.typeName;
            for (const auto& instruction : object.instructions)
                ASSERT_LT(instruction.parameter, 100000) << object.typeName;
        }
        io::BinaryWriter::WriteSolutionFile(solutionPath, solution);
        auto report = io::VerifySolutionFiles(puzzlePath, solutionPath, 150000, 10000);
        ASSERT_TRUE(report.Passed()) << report.detail << " cycle=" << report.errorCycle;
        solution.metrics = report.metrics;
        io::BinaryWriter::WriteSolutionFile(solutionPath, solution);
        EXPECT_TRUE(io::VerifySolutionFiles(puzzlePath, solutionPath, 150000, 10000).Passed());
    }
    std::error_code error;
    std::filesystem::remove(solutionPath, error);
}
#endif
} // namespace
