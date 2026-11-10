#include <gtest/gtest.h>

#include <limits>
#include <memory>

#include "BinaryWriter.hpp"
#include "SingleAtomSolver.hpp"
#include "RepoRoot.hpp"
#ifdef HAS_LIBVERIFY
#    include "libverify.h"
#endif

namespace {
using namespace opusoptima;

io::PuzzleData SimplePuzzle() {
    io::PuzzleData puzzle;
    puzzle.name = "generated-single-atom";
    puzzle.permissionBits = (1ULL << 0) | (1ULL << 22) | (1ULL << 23);
    puzzle.outputMultiplier = 1;
    puzzle.inputs.push_back({{{core::AtomType::Salt, {0, 0}}}, {}});
    puzzle.outputs = puzzle.inputs;
    return puzzle;
}
} // namespace

TEST(SingleAtomSolverTest, RegistersAndBuildsCompleteMachine) {
    auto solver = solvers::BaseSolver::Create("SingleAtomSolver");
    ASSERT_NE(solver, nullptr);
    const auto solution = solver->Solve(SimplePuzzle());
    ASSERT_EQ(solution.objects.size(), 3U);
    EXPECT_EQ(solution.objects[0].typeName, "arm1");
    EXPECT_EQ(solution.objects[1].typeName, "input");
    EXPECT_EQ(solution.objects[2].typeName, "out-std");
    EXPECT_EQ(solution.objects[0].bodyData, (std::vector<std::int32_t>{1, 0, 0, 4, 0}));
    ASSERT_EQ(solution.objects[0].instructions.size(), 4U);
    for (int i = 0; i < 4; ++i) {
        EXPECT_EQ(solution.objects[0].instructions[i].parameter, i);
    }
    EXPECT_EQ(solution.metrics.cycles, solver->EstimateCycles(SimplePuzzle()));
}

TEST(SingleAtomSolverTest, TranslatesLocalAtomCoordinates) {
    auto puzzle = SimplePuzzle();
    puzzle.inputs[0].atoms[0].position = {3, -2};
    puzzle.outputs[0].atoms[0].position = {-4, 6};
    const auto solution = solvers::SingleAtomSolver{}.Solve(puzzle);
    EXPECT_EQ(solution.objects[1].position, (core::HexCoord{-2, 2}));
    EXPECT_EQ(solution.objects[2].position, (core::HexCoord{4, -5}));
}

TEST(SingleAtomSolverTest, RejectsUnsupportedPuzzles) {
    const auto rejects = [](const io::PuzzleData& puzzle) {
        EXPECT_FALSE(solvers::SingleAtomSolver::Supports(puzzle));
        EXPECT_THROW((void)solvers::SingleAtomSolver{}.Solve(puzzle), std::invalid_argument);
    };
    auto puzzle = SimplePuzzle();
    puzzle.productionInfo.emplace();
    rejects(puzzle);
    puzzle = SimplePuzzle();
    puzzle.inputs.clear();
    rejects(puzzle);
    puzzle = SimplePuzzle();
    puzzle.outputs.push_back(puzzle.outputs[0]);
    rejects(puzzle);
    puzzle = SimplePuzzle();
    puzzle.inputs[0].atoms.push_back(puzzle.inputs[0].atoms[0]);
    rejects(puzzle);
    puzzle = SimplePuzzle();
    puzzle.outputs[0].bonds.push_back({});
    rejects(puzzle);
    puzzle = SimplePuzzle();
    puzzle.outputs[0].atoms[0].type = core::AtomType::Air;
    rejects(puzzle);
    puzzle = SimplePuzzle();
    puzzle.inputs[0].atoms[0].type = core::AtomType::Repeat;
    puzzle.outputs = puzzle.inputs;
    rejects(puzzle);
    puzzle = SimplePuzzle();
    puzzle.permissionBits = 0;
    rejects(puzzle);
    puzzle = SimplePuzzle();
    puzzle.outputMultiplier = 0;
    rejects(puzzle);
    puzzle = SimplePuzzle();
    puzzle.outputMultiplier = std::numeric_limits<std::int32_t>::max();
    rejects(puzzle);
    puzzle = SimplePuzzle();
    puzzle.inputs[0].atoms[0].position.q = std::numeric_limits<std::int32_t>::min() + 1;
    rejects(puzzle);
}

TEST(SingleAtomWriterTest, PreservesHeaderAndWritesTimestampBeforeOpcode) {
    io::SolutionData solution;
    io::SolutionObject arm;
    arm.typeName = "arm1";
    arm.position = {0, 0};
    arm.bodyData = {2, 5, 7, 99, 9};
    arm.instructions = {{'G', 3}, {'r', 8}};
    solution.objects.push_back(arm);
    const auto bytes = io::BinaryWriter::WriteSolution(solution);
    // Empty names, four metrics, object count, name, marker, and position.
    constexpr std::size_t body = 4 + 1 + 1 + 4 + 32 + 4 + 5 + 1 + 8;
    const auto word = [&](std::size_t offset) {
        std::uint32_t value = 0;
        for (int i = 0; i < 4; ++i) {
            value |= std::to_integer<std::uint32_t>(bytes.at(offset + i)) << (8 * i);
        }
        return value;
    };
    EXPECT_EQ(word(body), 2U);
    EXPECT_EQ(word(body + 4), 5U);
    EXPECT_EQ(word(body + 8), 7U);
    EXPECT_EQ(word(body + 12), 2U);
    EXPECT_EQ(word(body + 16), 3U);
    EXPECT_EQ(std::to_integer<char>(bytes.at(body + 20)), 'G');
    EXPECT_EQ(word(body + 21), 8U);
    EXPECT_EQ(std::to_integer<char>(bytes.at(body + 25)), 'r');
    EXPECT_EQ(word(body + 26), 9U);
    EXPECT_EQ(bytes.size(), body + 30);
    const auto parsed = io::BinaryParser::ParseSolution(bytes);
    ASSERT_EQ(parsed.objects.size(), 1U);
    EXPECT_EQ(parsed.objects[0].bodyData, (std::vector<std::int32_t>{2, 5, 7, 2, 9}));
    ASSERT_EQ(parsed.objects[0].instructions.size(), 2U);
    EXPECT_EQ(parsed.objects[0].instructions[0].action, 'G');
    EXPECT_EQ(parsed.objects[0].instructions[0].parameter, 3);
    EXPECT_EQ(parsed.objects[0].instructions[1].action, 'r');
    EXPECT_EQ(parsed.objects[0].instructions[1].parameter, 8);
}

#ifdef HAS_LIBVERIFY
TEST(SingleAtomSolverTest, VerifiesJustSaltFromGeneratedMachine) {
    const auto repo = opusoptima::test::RepoRoot(__FILE__);
    const auto path = repo / "asset/puzzle/easy/just-salt.puzzle";
    const auto puzzleBytes = io::BinaryParser::ReadAllBytes(path);
    const auto puzzle = io::BinaryParser::ParsePuzzle(puzzleBytes);
    ASSERT_TRUE(solvers::SingleAtomSolver::Supports(puzzle));
    const auto solution = solvers::SingleAtomSolver{}.Solve(puzzle);
    const auto solutionBytes = io::BinaryWriter::WriteSolution(solution);
    std::unique_ptr<void, decltype(&verifier_destroy)> verifier(
        verifier_create_from_bytes(
            reinterpret_cast<const char*>(puzzleBytes.data()), puzzleBytes.size(),
            reinterpret_cast<const char*>(solutionBytes.data()), solutionBytes.size()),
        verifier_destroy);
    ASSERT_NE(verifier, nullptr);
    ASSERT_EQ(verifier_error(verifier.get()), nullptr);
    verifier_set_cycle_limit(verifier.get(), 1000);
    const auto cycles = verifier_evaluate_metric(verifier.get(), "cycles");
    ASSERT_EQ(verifier_error(verifier.get()), nullptr);
    EXPECT_EQ(cycles, solution.metrics.cycles);
    EXPECT_EQ(verifier_evaluate_metric(verifier.get(), "cost"), solution.metrics.cost);
    EXPECT_EQ(verifier_evaluate_metric(verifier.get(), "area"), solution.metrics.area);
    EXPECT_EQ(verifier_evaluate_metric(verifier.get(), "instructions"),
              solution.metrics.instructionCount);
    ASSERT_EQ(verifier_error(verifier.get()), nullptr);
}
#endif
