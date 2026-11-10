#pragma once

#include <cstdint>
#include <stdexcept>

#include "Core/MachineState.hpp"
#include "IO/BinaryParser.hpp"

namespace opusoptima::engine {

/// Exception thrown when simulation encounters an invalid state.
class SimulationError : public std::runtime_error {
    public:
    using std::runtime_error::runtime_error;
};

/// Deterministic engine that simulates a solution's arm instructions
class HexSimulator {
    public:
    /// Run the complete simulation for a given puzzle + solution.
    [[nodiscard]] static std::int32_t Run(const io::PuzzleData& puzzle,
                                          const io::SolutionData& solution,
                                          core::MachineState& outState);

    /// Initialise a machine state from puzzle inputs.
    [[nodiscard]] static core::MachineState InitialiseFromPuzzle(const io::PuzzleData& puzzle);

    /// Initialise arms from solution objects.
    static void PlaceSolutionObjects(core::MachineState& state, const io::SolutionData& solution);

    private:
    /// Execute the next instruction for an arm.
    static void ExecuteInstruction(core::MachineState& state, core::ArmState& arm,
                                   const io::ArmInstruction& instr);

    /// Attempt to grab an atom at the arm tip.
    static void TryGrab(core::MachineState& state, core::ArmState& arm);

    /// Attempt to drop the held atom at the arm tip.
    static void TryDrop(core::MachineState& state, core::ArmState& arm, std::uint8_t owner);

    /// Rotate with collision sweep (positive steps = CW, negative = CCW).
    static void TryRotate(int steps, core::MachineState& state, core::ArmState& arm,
                          std::uint8_t owner);

    /// Attempt to extend the arm by @p amount hexes.
    static void TryExtend(int amount, core::MachineState& state, core::ArmState& arm,
                          std::uint8_t owner);

    /// Attempt to retract the arm by 1 hex.
    static void TryRetract(core::ArmState& arm);

    /// Pivot the arm direction (no collision check).
    static void TryPivot(core::ArmState& arm, int steps);

    /// Advance the instruction pointer and return the new index.
    static std::size_t AdvanceIp(core::ArmState& arm);
};

} // namespace opusoptima::engine
