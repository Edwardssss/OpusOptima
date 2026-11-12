#include "MoleculePipeline.hpp"

#include "IO/BinaryWriter.hpp"
#include "IO/SolutionVerifier.hpp"
#include "PipelineCompiler.hpp"
#include "RepoRoot.hpp"

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <gtest/gtest.h>

namespace opusoptima::solvers::architecture {
namespace {

// Arm, bonder, unbonder, and the tape instruction bits the swings use.
constexpr std::uint64_t kMoleculeBits = (1ULL << 0) | (1ULL << 8) | (1ULL << 9) |
                                        (1ULL << 22) | (1ULL << 23) | (1ULL << 24) |
                                        (1ULL << 25);

io::PuzzleData BasePuzzle() {
    io::PuzzleData puzzle;
    puzzle.version = 3;
    puzzle.name = "generated-molecule";
    puzzle.permissionBits = kMoleculeBits;
    puzzle.outputMultiplier = 1;
    return puzzle;
}

/// Two bonded salt atoms as the product frame: the smallest admissible product.
io::PuzzleData BondedPairProduct() {
    auto puzzle = BasePuzzle();
    puzzle.outputs.emplace_back();
    puzzle.outputs.front().atoms = {{core::AtomType::Salt, {0, 0}},
                                    {core::AtomType::Salt, {1, 0}}};
    puzzle.outputs.front().bonds = {{1, {0, 0}, {1, 0}}};
    return puzzle;
}

void WritePuzzle(const std::filesystem::path& path, const io::PuzzleData& puzzle) {
    std::ofstream out(path, std::ios::binary);
    const auto word = [&](std::uint64_t value, int bytes) {
        for (int i = 0; i < bytes; ++i) out.put(static_cast<char>(value >> (8 * i)));
    };
    word(3, 4);
    word(puzzle.name.size(), 1);
    out.write(puzzle.name.data(), static_cast<std::streamsize>(puzzle.name.size()));
    word(0, 8);
    word(puzzle.permissionBits, 8);
    const auto list = [&](const std::vector<io::Molecule>& molecules) {
        word(molecules.size(), 4);
        for (const auto& molecule : molecules) {
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
    };
    list(puzzle.inputs);
    list(puzzle.outputs);
    word(puzzle.outputMultiplier, 4);
    word(0, 1);
}

} // namespace

TEST(MoleculePipelineTest, DomainRequiresAMolecularFeed) {
    // Two single-atom ports and no molecule: the pipeline emitter's domain, not
    // this one's.
    auto puzzle = BondedPairProduct();
    puzzle.inputs.push_back({{{core::AtomType::Salt, {0, 0}}}, {}});
    puzzle.inputs.push_back({{{core::AtomType::Salt, {0, 0}}}, {}});
    EXPECT_FALSE(SupportsMoleculeAssembly(puzzle));
}

TEST(MoleculePipelineTest, DomainAcceptsABondedProductFedByAMolecule) {
    auto puzzle = BondedPairProduct();
    puzzle.inputs.push_back({{{core::AtomType::Salt, {0, 0}}, {core::AtomType::Salt, {1, 0}}},
                             {{1, {0, 0}, {1, 0}}}});
    EXPECT_TRUE(SupportsMoleculeAssembly(puzzle));
}

TEST(MoleculePipelineTest, DomainRejectsAProductLargerThanSixteenAtoms) {
    auto puzzle = BasePuzzle();
    puzzle.inputs.push_back({{{core::AtomType::Salt, {0, 0}}, {core::AtomType::Salt, {1, 0}}},
                             {{1, {0, 0}, {1, 0}}}});
    puzzle.outputs.emplace_back();
    for (int i = 0; i < 17; ++i)
        puzzle.outputs.front().atoms.push_back({core::AtomType::Salt, {i, 0}});
    for (int i = 0; i + 1 < 17; ++i)
        puzzle.outputs.front().bonds.push_back({1, {i, 0}, {i + 1, 0}});
    EXPECT_FALSE(SupportsMoleculeAssembly(puzzle));
}

TEST(MoleculePipelineTest, DomainRejectsAProductAtomNoInputCarries) {
    auto puzzle = BondedPairProduct();
    puzzle.outputs.front().atoms[1].type = core::AtomType::Water;
    puzzle.inputs.push_back({{{core::AtomType::Salt, {0, 0}}, {core::AtomType::Salt, {1, 0}}},
                             {{1, {0, 0}, {1, 0}}}});
    EXPECT_FALSE(SupportsMoleculeAssembly(puzzle));
}

// The builder does emit a complete machine inside its domain: one unbonder for
// the feed bond, one arm per displaced atom, one bonder per product bond and a
// single output area.
TEST(MoleculePipelineTest, EmitsOneArmPerDisplacedAtomAndOneOutputArea) {
    auto puzzle = BondedPairProduct();
    puzzle.inputs.push_back({{{core::AtomType::Salt, {0, 0}}, {core::AtomType::Salt, {1, 0}}},
                             {{1, {0, 0}, {1, 0}}}});
    ASSERT_TRUE(SupportsMoleculeAssembly(puzzle));
    MoleculeVariant detail;
    const auto machine = CompileMoleculeMachine(puzzle, true, &detail);
    const auto count = [&](const std::string& type) {
        std::size_t total = 0;
        for (const auto& object : machine.objects)
            if (object.typeName == type) ++total;
        return total;
    };
    EXPECT_EQ(detail.fromMolecule, 2U);
    EXPECT_EQ(detail.fromPort, 0U);
    EXPECT_EQ(detail.unbonders, 1U);
    EXPECT_EQ(count("unbonder"), 1U);
    EXPECT_EQ(count("arm1"), 2U);
    EXPECT_EQ(count("bonder"), 1U);
    EXPECT_EQ(count("out-std"), 1U);
    EXPECT_EQ(count("input"), 1U);
    EXPECT_GT(detail.period, 0);
}

TEST(MoleculePipelineTest, EmittedMachinePassesTheNativeGate) {
    const auto root = opusoptima::test::RepoRoot(__FILE__);
    const auto puzzlePath = root / "build/molecule-settlement.puzzle";
    const auto solutionPath = root / "build/molecule-settlement.solution";
    struct Cleanup {
        std::filesystem::path puzzle, solution;
        ~Cleanup() {
            std::error_code ec;
            std::filesystem::remove(puzzle, ec);
            std::filesystem::remove(solution, ec);
        }
    } cleanup{puzzlePath, solutionPath};

    auto puzzle = BondedPairProduct();
    puzzle.inputs.push_back({{{core::AtomType::Salt, {0, 0}}, {core::AtomType::Salt, {1, 0}}},
                             {{1, {0, 0}, {1, 0}}}});
    WritePuzzle(puzzlePath, puzzle);
    const auto machine = CompileMoleculeMachine(puzzle, true);
    io::BinaryWriter::WriteSolutionFile(solutionPath, machine);
    const auto report = io::VerifySolutionFiles(puzzlePath, solutionPath, 400000, 20000);
    EXPECT_TRUE(report.Passed())
        << report.detail << " cycle=" << report.errorCycle << " at=(" << report.errorU << ","
        << report.errorV << ")";
    // The molecule lands, both bonds are cut and both atoms arrive in the same
    // period, so the machine is a 34-cycle loop rather than a relay chain.
    EXPECT_EQ(report.metrics.cycles, 34);
}

TEST(MoleculePipelineTest, VariantsStayOffUnlessTheFamilyIsSwitchedOn) {
    auto puzzle = BondedPairProduct();
    puzzle.inputs.push_back({{{core::AtomType::Salt, {0, 0}}, {core::AtomType::Salt, {1, 0}}},
                             {{1, {0, 0}, {1, 0}}}});
    std::vector<PipelineVariant> variants;
    unsetenv("OPUSOPTIMA_MOLECULE_ARCHITECTURE");
    AppendMoleculeVariants(puzzle, false, variants);
    EXPECT_TRUE(variants.empty());
}

} // namespace opusoptima::solvers::architecture
