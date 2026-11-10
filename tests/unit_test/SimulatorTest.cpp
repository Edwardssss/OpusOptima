#include <cstdint>
#include <filesystem>
#include <vector>

#include <gtest/gtest.h>

#include "BinaryParser.hpp"
#include "HexGrid.hpp"
#include "HexSimulator.hpp"
#include "MachineState.hpp"
#include "PuzzleSpecs.hpp"
#include "RepoRoot.hpp"

// Type-level using declarations (NOT using namespace)
using opusoptima::core::ArmState;
using opusoptima::core::AtomType;
using opusoptima::core::Cell;
using opusoptima::core::CellOccupant;
using opusoptima::core::HexCoord;
using opusoptima::core::MachineState;

namespace {

std::filesystem::path GetRepoRoot() {
    return opusoptima::test::RepoRoot(__FILE__);
}

} // namespace

// MachineState unit tests

TEST(MachineStateTest, InitialStateHasZeroCycle) {
    MachineState state;
    EXPECT_EQ(state.cycle, 0);
    EXPECT_TRUE(state.grid.empty());
    EXPECT_TRUE(state.arms.empty());
}

TEST(MachineStateTest, PlaceAtomOnGrid) {
    MachineState state;
    const HexCoord pos{.q = 2, .r = -1};
    state.grid[pos] = Cell{
        .occupant = CellOccupant::Atom,
        .ownerId = 0,
        .atomType = AtomType::Salt,
    };
    ASSERT_TRUE(state.grid.contains(pos));
    EXPECT_EQ(state.grid[pos].occupant, CellOccupant::Atom);
    EXPECT_EQ(state.grid[pos].atomType, AtomType::Salt);
}

// HexSimulator unit tests

TEST(HexSimulatorTest, InitialiseFromPuzzlePlacesInputAtoms) {
    const auto repo = GetRepoRoot();
    const auto path = repo / "asset/puzzle/easy/just-salt.puzzle";
    const auto puzzle = opusoptima::io::BinaryParser::ParsePuzzleFile(path);

    const auto state = opusoptima::engine::HexSimulator::InitialiseFromPuzzle(puzzle);

    EXPECT_EQ(state.grid.size(), 1U);
    const HexCoord origin{.q = 0, .r = 0};
    ASSERT_TRUE(state.grid.contains(origin));
    EXPECT_EQ(state.grid.at(origin).atomType, AtomType::Salt);
}

TEST(HexSimulatorTest, PlaceSolutionArmsAddsArmStates) {
    const auto repo = GetRepoRoot();
    const auto puzzlePath = repo / "asset/puzzle/easy/easy-conduit.puzzle";
    const auto solPath = repo / "asset/solution/easy/easy-conduit-easy-conduit-1.solution";

    const auto puzzle = opusoptima::io::BinaryParser::ParsePuzzleFile(puzzlePath);
    const auto solution = opusoptima::io::BinaryParser::ParseSolutionFile(solPath);

    auto state = opusoptima::engine::HexSimulator::InitialiseFromPuzzle(puzzle);
    opusoptima::engine::HexSimulator::PlaceSolutionObjects(state, solution);

    EXPECT_EQ(state.arms.size(), 2U);
}

TEST(HexSimulatorTest, RunProducesCorrectCycleCount) {
    const auto repo = GetRepoRoot();
    const auto puzzlePath = repo / "asset/puzzle/easy/easy-conduit.puzzle";
    const auto solPath = repo / "asset/solution/easy/easy-conduit-easy-conduit-1.solution";

    const auto puzzle = opusoptima::io::BinaryParser::ParsePuzzleFile(puzzlePath);
    const auto solution = opusoptima::io::BinaryParser::ParseSolutionFile(solPath);

    MachineState finalState;
    const std::int32_t cycles = opusoptima::engine::HexSimulator::Run(puzzle, solution, finalState);

    EXPECT_EQ(cycles, solution.metrics.cycles);
    EXPECT_EQ(finalState.cycle, solution.metrics.cycles);
}

TEST(HexSimulatorTest, RunOnEasyConduitCompletes) {
    const auto repo = GetRepoRoot();
    const auto puzzlePath = repo / "asset/puzzle/easy/easy-conduit.puzzle";
    const auto solPath = repo / "asset/solution/easy/easy-conduit-easy-conduit-1.solution";

    const auto puzzle = opusoptima::io::BinaryParser::ParsePuzzleFile(puzzlePath);
    const auto solution = opusoptima::io::BinaryParser::ParseSolutionFile(solPath);

    MachineState finalState;
    (void)opusoptima::engine::HexSimulator::Run(puzzle, solution, finalState);

    for (const auto& arm : finalState.arms) {
        EXPECT_FALSE(arm.active) << "Arm at " << arm.basePosition << " did not finish";
    }
}

// Simulation functional test

TEST(SimulatorFunctionalTest, SimulatesEasyConduitSolution) {
    const auto repo = GetRepoRoot();
    const auto puzzlePath = repo / "asset/puzzle/easy/easy-conduit.puzzle";
    const auto solPath = repo / "asset/solution/easy/easy-conduit-easy-conduit-1.solution";

    const auto puzzle = opusoptima::io::BinaryParser::ParsePuzzleFile(puzzlePath);
    const auto solution = opusoptima::io::BinaryParser::ParseSolutionFile(solPath);

    EXPECT_EQ(solution.puzzleName, "easy-conduit");
    EXPECT_EQ(solution.metrics.cycles, 26);
    EXPECT_EQ(solution.metrics.cost, 40);
    EXPECT_EQ(solution.metrics.area, 29);

    MachineState finalState;
    const std::int32_t simCycles =
        opusoptima::engine::HexSimulator::Run(puzzle, solution, finalState);

    EXPECT_GT(simCycles, 0);
    EXPECT_EQ(simCycles, solution.metrics.cycles);
}

TEST(SimulatorFunctionalTest, SimulatesDoubleConduitSolution) {
    const auto repo = GetRepoRoot();
    const auto puzzlePath = repo / "asset/puzzle/easy/double-conduit.puzzle";
    const auto solPath = repo / "asset/solution/easy/double-conduit-double-conduit-1.solution";

    const auto puzzle = opusoptima::io::BinaryParser::ParsePuzzleFile(puzzlePath);
    const auto solution = opusoptima::io::BinaryParser::ParseSolutionFile(solPath);

    MachineState finalState;
    const std::int32_t simCycles =
        opusoptima::engine::HexSimulator::Run(puzzle, solution, finalState);

    EXPECT_EQ(simCycles, solution.metrics.cycles);
    EXPECT_GT(simCycles, 0);
}
