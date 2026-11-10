#include <gtest/gtest.h>

#include <algorithm>
#include <fstream>
#include <limits>

#include "BinaryWriter.hpp"
#include "DiatomicSolver.hpp"
#include "SolutionVerifier.hpp"
#include "RepoRoot.hpp"

namespace {
using namespace opusoptima;

io::PuzzleData PairPuzzle() {
    io::PuzzleData puzzle;
    puzzle.version = 3;
    puzzle.name = "generated-diatomic";
    puzzle.permissionBits = (1ULL << 0) | (1ULL << 22) | (1ULL << 23);
    puzzle.outputMultiplier = 1;
    puzzle.inputs = {
        {{{core::AtomType::Salt, {0, 0}}, {core::AtomType::Air, {1, 0}}}, {{1, {0, 0}, {1, 0}}}}};
    puzzle.outputs = puzzle.inputs;
    return puzzle;
}

void Transform(io::Molecule& molecule, int turns, core::HexCoord offset) {
    const auto transform = [&](core::HexCoord p) {
        for (int i = 0; i < turns; ++i)
            p = core::RotateClockwise(p);
        return core::Add(p, offset);
    };
    for (auto& atom : molecule.atoms)
        atom.position = transform(atom.position);
    for (auto& bond : molecule.bonds) {
        bond.from = transform(bond.from);
        bond.to = transform(bond.to);
    }
}

#ifdef HAS_LIBVERIFY
// Puzzle serialization is deliberately local to these independent verifier tests.
void WritePuzzle(const std::filesystem::path& path, const io::PuzzleData& puzzle) {
    std::ofstream out(path, std::ios::binary);
    const auto word = [&](std::uint64_t value, int bytes) {
        for (int i = 0; i < bytes; ++i)
            out.put(static_cast<char>(value >> (8 * i)));
    };
    word(3, 4);
    word(puzzle.name.size(), 1);
    out.write(puzzle.name.data(), puzzle.name.size());
    word(0, 8);
    word(puzzle.permissionBits, 8);
    const auto molecules = [&](const std::vector<io::Molecule>& list) {
        word(list.size(), 4);
        for (const auto& molecule : list) {
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
    molecules(puzzle.inputs);
    molecules(puzzle.outputs);
    word(puzzle.outputMultiplier, 4);
    word(0, 1);
}

class DiatomicVerifierTest : public testing::Test {
    protected:
    std::filesystem::path puzzlePath, solutionPath;
    void SetUp() override {
        const auto root = opusoptima::test::RepoRoot(__FILE__);
        const auto stem = std::string("diatomic-verifier-") +
                          testing::UnitTest::GetInstance()->current_test_info()->name();
        puzzlePath = root / "build" / (stem + ".puzzle");
        solutionPath = root / "build" / (stem + ".solution");
    }
    void TearDown() override {
        std::error_code error;
        std::filesystem::remove(puzzlePath, error);
        std::filesystem::remove(solutionPath, error);
    }
    void Verify(const io::PuzzleData& puzzle) {
        ASSERT_TRUE(solvers::DiatomicSolver::Supports(puzzle));
        WritePuzzle(puzzlePath, puzzle);
        const auto parsed = io::BinaryParser::ParsePuzzleFile(puzzlePath);
        const auto solution = solvers::DiatomicSolver{}.Solve(parsed);
        io::BinaryWriter::WriteSolutionFile(solutionPath, solution);
        const auto report = io::VerifySolutionFiles(puzzlePath, solutionPath, 1000, 2000);
        ASSERT_TRUE(report.Passed()) << report.detail << " cycle=" << report.errorCycle;
        EXPECT_EQ(report.metrics.cycles, solution.metrics.cycles);
        EXPECT_EQ(report.metrics.cost, solution.metrics.cost);
        EXPECT_EQ(report.metrics.area, solution.metrics.area);
        EXPECT_EQ(report.metrics.instructionCount, solution.metrics.instructionCount);
        std::cout << "diatomic metrics: " << report.metrics.cycles << " cycles, "
                  << report.metrics.cost << " cost, " << report.metrics.area << " area, "
                  << report.metrics.instructionCount << " instructions\n";
    }
};
#endif
} // namespace

TEST(DiatomicSolverTest, RegistersCompleteMachine) {
    ASSERT_NE(solvers::BaseSolver::Create("DiatomicSolver"), nullptr);
    auto solver = solvers::BaseSolver::Create("diatomic");
    ASSERT_NE(solver, nullptr);
    const auto solution = solver->Solve(PairPuzzle());
    ASSERT_EQ(solution.objects.size(), 3U);
    EXPECT_EQ(solution.objects[0].bodyData, (std::vector<std::int32_t>{3, 0, 0, 4, 0}));
    EXPECT_EQ(solution.objects[0].instructions.size(), 4U);
    EXPECT_EQ(solution.objects[1].typeName, "input");
    EXPECT_EQ(solution.objects[2].typeName, "out-std");
    EXPECT_EQ(solution.metrics.cycles, 23);
}

TEST(DiatomicSolverTest, RejectsUnsupportedAndMalformedPairs) {
    const auto rejects = [](const io::PuzzleData& puzzle) {
        EXPECT_FALSE(solvers::DiatomicSolver::Supports(puzzle));
        EXPECT_THROW((void)solvers::DiatomicSolver{}.Solve(puzzle), std::invalid_argument);
        EXPECT_THROW((void)solvers::DiatomicSolver{}.EstimateCycles(puzzle), std::invalid_argument);
    };
    auto p = PairPuzzle();
    p.productionInfo.emplace();
    rejects(p);
    p = PairPuzzle();
    p.inputs.clear();
    rejects(p);
    p = PairPuzzle();
    p.outputs.clear();
    rejects(p);
    p = PairPuzzle();
    p.inputs.push_back(p.inputs[0]);
    rejects(p);
    p = PairPuzzle();
    p.outputs.push_back(p.outputs[0]);
    rejects(p);
    p = PairPuzzle();
    p.inputs[0].atoms.pop_back();
    rejects(p);
    p = PairPuzzle();
    p.outputs[0].atoms.push_back(p.outputs[0].atoms[0]);
    rejects(p);
    p = PairPuzzle();
    p.inputs[0].bonds.clear();
    rejects(p);
    p = PairPuzzle();
    p.outputs[0].bonds.push_back(p.outputs[0].bonds[0]);
    rejects(p);
    p = PairPuzzle();
    p.inputs[0].bonds[0].from = {5, 5};
    rejects(p);
    p = PairPuzzle();
    p.outputs[0].atoms[1].position = {0, 0};
    rejects(p);
    p = PairPuzzle();
    p.inputs[0].atoms[1].position = {2, 0};
    p.inputs[0].bonds[0].to = {2, 0};
    rejects(p);
    for (int bits : {0, 2, 14, 15, 255}) {
        p = PairPuzzle();
        p.outputs[0].bonds[0].bits = bits;
        rejects(p);
    }
    p = PairPuzzle();
    p.outputs[0].atoms[0].type = core::AtomType::Water;
    rejects(p);
    for (auto type : {core::AtomType::Repeat, static_cast<core::AtomType>(0),
                      static_cast<core::AtomType>(255)}) {
        p = PairPuzzle();
        p.inputs[0].atoms[0].type = type;
        p.outputs = p.inputs;
        rejects(p);
    }
    for (auto bit : {0, 22, 23}) {
        p = PairPuzzle();
        p.permissionBits &= ~(1ULL << bit);
        rejects(p);
    }
    for (int multiplier : {0, -1, std::numeric_limits<std::int32_t>::max()}) {
        p = PairPuzzle();
        p.outputMultiplier = multiplier;
        rejects(p);
    }
    p = PairPuzzle();
    p.inputs[0].atoms[0].position.q = std::numeric_limits<int>::min();
    rejects(p);
}

TEST(DiatomicSolverTest, TransformsAndReordersWithoutChangingAtomIdentity) {
    auto puzzle = PairPuzzle();
    Transform(puzzle.inputs[0], 2, {7, -4});
    Transform(puzzle.outputs[0], 5, {-9, 6});
    std::swap(puzzle.outputs[0].atoms[0], puzzle.outputs[0].atoms[1]);
    std::swap(puzzle.outputs[0].bonds[0].from, puzzle.outputs[0].bonds[0].to);
    ASSERT_TRUE(solvers::DiatomicSolver::Supports(puzzle));
    const auto solution = solvers::DiatomicSolver{}.Solve(puzzle);
    for (int side = 0; side < 2; ++side) {
        const auto& molecule = side ? puzzle.outputs[0] : puzzle.inputs[0];
        const auto& object = solution.objects[side + 1];
        for (const auto& atom : molecule.atoms) {
            auto position = atom.position;
            for (int i = 0; i < object.bodyData[1]; ++i)
                position = core::RotateClockwise(position);
            position = core::Add(position, object.position);
            const int distance = atom.type == core::AtomType::Salt ? 3 : 4;
            EXPECT_EQ(position, (side ? core::HexCoord{0, distance} : core::HexCoord{distance, 0}));
        }
    }
}

#ifdef HAS_LIBVERIFY
TEST_F(DiatomicVerifierTest, AllRotationsTranslationsAndOrderings) {
    for (int inputTurns = 0; inputTurns < 6; ++inputTurns) {
        for (int outputTurns = 0; outputTurns < 6; ++outputTurns) {
            for (bool reverse : {false, true}) {
                SCOPED_TRACE(testing::Message()
                             << inputTurns << ',' << outputTurns << ',' << reverse);
                auto puzzle = PairPuzzle();
                Transform(puzzle.inputs[0], inputTurns, {7, -4});
                Transform(puzzle.outputs[0], outputTurns, {-9, 6});
                if (reverse) {
                    std::reverse(puzzle.outputs[0].atoms.begin(), puzzle.outputs[0].atoms.end());
                    std::swap(puzzle.outputs[0].bonds[0].from, puzzle.outputs[0].bonds[0].to);
                }
                Verify(puzzle);
            }
        }
    }
}

TEST_F(DiatomicVerifierTest, IdenticalElementsAndMultipleOutputs) {
    auto puzzle = PairPuzzle();
    puzzle.inputs[0].atoms[1].type = core::AtomType::Salt;
    puzzle.outputs = puzzle.inputs;
    puzzle.outputMultiplier = 3;
    Transform(puzzle.outputs[0], 1, {-3, 5});
    Verify(puzzle);
}

TEST_F(DiatomicVerifierTest, AllSupportedElements) {
    for (int id = 1; id <= 16; ++id) {
        if (id == 15) continue;
        SCOPED_TRACE(id);
        auto puzzle = PairPuzzle();
        puzzle.inputs[0].atoms[0].type = static_cast<core::AtomType>(id);
        puzzle.outputs = puzzle.inputs;
        Verify(puzzle);
    }
}

#endif
