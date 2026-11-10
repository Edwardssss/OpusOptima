#include <filesystem>
#include <vector>

#include <gtest/gtest.h>

#include "BinaryParser.hpp"
#include "GreedyPlanner.hpp"
#include "HexSimulator.hpp"
#include "MachineState.hpp"
#include "RecipeMilp.hpp"
#include "RepoRoot.hpp"

namespace {

std::filesystem::path GetRepoRoot() {
    return opusoptima::test::RepoRoot(__FILE__);}

} // namespace

// Functional test: solve easy-infinite-product with A* planner

TEST(SolverFunctionalTest, GreedyPlannerSolvesEasyInfiniteProduct) {
    const auto repo = GetRepoRoot();
    const auto puzzlePath = repo / "asset/puzzle/easy/easy-infinite-product.puzzle";

    // 1. Parse the puzzle
    const auto puzzle = opusoptima::io::BinaryParser::ParsePuzzleFile(puzzlePath);

    // 2. Run the solver
    opusoptima::solvers::GreedyPlannerSolver solver;
    auto plan = solver.Solve(puzzle);

    // 3. Verify the plan has the right structure
    ASSERT_FALSE(plan.objects.empty());
    EXPECT_EQ(plan.objects[0].typeName, "arm1");
    EXPECT_FALSE(plan.objects[0].instructions.empty());

    // 4. Run the plan through the simulator
    opusoptima::core::MachineState state;
    const std::int32_t cycles = opusoptima::engine::HexSimulator::Run(puzzle, plan, state);

    // The simulation should complete without crashing
    EXPECT_GT(cycles, 0);
    EXPECT_EQ(state.cycle, cycles);

    // The solution should produce output atoms
    // Check if output atoms are present on the grid
    std::size_t outputAtomsPlaced = 0;
    for (const auto& [pos, cell] : state.grid) {
        if (cell.occupant == opusoptima::core::CellOccupant::Atom) {
            ++outputAtomsPlaced;
        }
    }
    EXPECT_GT(outputAtomsPlaced, 0U) << "The solver should have placed at least one output atom";

    // Log some info about the solution
    std::cout << "\n=== Solver Result ===\n"
              << "Cycles: " << plan.metrics.cycles << "\n"
              << "Instructions: " << plan.metrics.instructionCount << "\n"
              << "Arms: " << plan.objects.size() << "\n"
              << "Grid atoms at end: " << outputAtomsPlaced << "\n"
              << "====================\n";
}

// HiGHS MILP integration test

TEST(SolverFunctionalTest, RecipeMilpMatchesPuzzleTypes) {
    const auto repo = GetRepoRoot();
    const auto puzzlePath = repo / "asset/puzzle/easy/easy-infinite-product.puzzle";
    const auto puzzle = opusoptima::io::BinaryParser::ParsePuzzleFile(puzzlePath);

    // Collect input types
    std::vector<opusoptima::core::AtomType> inputTypes;
    for (const auto& mol : puzzle.inputs) {
        for (const auto& atom : mol.atoms) {
            inputTypes.push_back(atom.type);
        }
    }

    // Collect output types
    std::vector<opusoptima::core::AtomType> outputTypes;
    for (const auto& mol : puzzle.outputs) {
        for (const auto& atom : mol.atoms) {
            outputTypes.push_back(atom.type);
        }
    }

    // Run MILP solver
    auto result = opusoptima::solvers::RecipeMilpSolver::SolveTypes(inputTypes, outputTypes);

    // Should find a solution (6 inputs should cover 1 Salt + 1 Repeat)
    EXPECT_TRUE(result.success);

    std::cout << "\n=== MILP Result ===\n"
              << "Success: " << (result.success ? "yes" : "no") << "\n"
              << "Description: " << result.description << "\n"
              << "Input atoms: " << inputTypes.size() << "\n"
              << "Output atoms: " << outputTypes.size() << "\n"
              << "====================\n";
}
