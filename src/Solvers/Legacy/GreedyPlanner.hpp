#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "BaseSolver.hpp"
#include "BinaryParser.hpp"

namespace opusoptima::solvers {

/// High-level planner that generates a valid (but non-optimal) solution
/// for a given puzzle using greedy atom assignment and direct path planning.
///
/// Strategy:
///   1. Place a single arm at a position that can reach both input and output.
///   2. For each output atom, greedily assign the nearest unused input atom
///      of matching type.
///   3. Plan direct move sequences (extend/grab/retract/rotate/drop).
class GreedyPlannerSolver : public BaseSolver {
    public:
    explicit GreedyPlannerSolver();

    // Run the planner and produce a SolutionData.
    [[nodiscard]] io::SolutionData Solve(const io::PuzzleData& puzzle) override;

    // Estimate how many cycles the generated solution will take.
    [[nodiscard]] std::int32_t EstimateCycles(const io::PuzzleData& puzzle) override;

    private:
    struct AtomAssignment {
        std::size_t inputIdx;
        core::HexCoord inputPos;
        std::size_t outputIdx;
        core::HexCoord outputPos;
        core::AtomType type;
    };

    struct PositionSummary {
        std::vector<core::HexCoord> inputPositions;
        std::vector<core::HexCoord> outputPositions;
    };

    struct CandidateScore {
        bool occupied;
        int distanceToInputs;
        int distanceToOutputs;
    };

    // Find a suitable arm base position (high-level heuristic).
    [[nodiscard]] static core::HexCoord ChooseArmPosition(const io::PuzzleData& puzzle);

    // Assign input atoms to output atoms by type (greedy nearest-match).
    [[nodiscard]] static std::vector<AtomAssignment> AssignAtoms(const io::PuzzleData& puzzle);

    // Generate arm instructions for a single atom move.
    [[nodiscard]] static std::vector<io::ArmInstruction> PlanAtomMove(
        core::HexCoord armBase, const AtomAssignment& assignment);

    [[nodiscard]] static PositionSummary CollectAtomPositions(const io::PuzzleData& puzzle);
    [[nodiscard]] static core::HexCoord ComputeCentroid(const PositionSummary& positions);
    [[nodiscard]] static CandidateScore EvaluateCandidate(core::HexCoord candidate,
                                                          const PositionSummary& positions);
    [[nodiscard]] static bool IsOccupied(core::HexCoord candidate,
                                         const std::vector<core::HexCoord>& positions);
    [[nodiscard]] static int MinDistanceToSet(core::HexCoord origin,
                                              const std::vector<core::HexCoord>& positions);

    // Count the instruction bytes to estimate cycles.
    [[nodiscard]] static std::int32_t EstimateCyclesFromInstrs(
        const std::vector<io::ArmInstruction>& instrs);
};

} // namespace opusoptima::solvers
