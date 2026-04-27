#include "GreedyPlanner.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>

#include "LogManager.hpp"
#include <spdlog/spdlog.h>

namespace opusoptima::solvers {

namespace {

[[nodiscard]] int DirectionToward(core::HexCoord base, core::HexCoord target) {
    const auto delta = core::Subtract(target, base);
    int bestDir = 0;
    int bestDot = std::numeric_limits<int>::min();
    for (std::size_t dirIdx = 0; dirIdx < core::kDirectionCount; ++dirIdx) {
        const auto& dirVec = core::kAxialDirections[dirIdx];
        const int dot = (delta.q * dirVec.q) + (delta.r * dirVec.r);
        if (dot > bestDot) {
            bestDot = dot;
            bestDir = static_cast<int>(dirIdx);
        }
    }
    return bestDir;
}

struct ArmReach {
    int direction{0};
    int length{0};
};

struct AssignmentEntry {
    std::size_t idx;
    core::HexCoord pos;
    core::AtomType type;
};

[[nodiscard]] ArmReach ReachTarget(core::HexCoord base, core::HexCoord target) {
    const auto delta = core::Subtract(target, base);
    const int dir = DirectionToward(base, target);
    const auto& dirVec = core::kAxialDirections[static_cast<std::size_t>(dir)];

    if (dirVec.q != 0) {
        const int len = delta.q / dirVec.q;
        if (len > 0 && delta.q == dirVec.q * len && delta.r == dirVec.r * len) {
            return {.direction = dir, .length = len};
        }
    } else if (dirVec.r != 0) {
        const int len = delta.r / dirVec.r;
        if (len > 0 && delta.r == dirVec.r * len && delta.q == dirVec.q * len) {
            return {.direction = dir, .length = len};
        }
    }
    return {.direction = dir, .length = std::max(std::abs(delta.q), std::abs(delta.r))};
}

[[nodiscard]] std::vector<char> RotateSeq(int currentDir, int targetDir) {
    std::vector<char> result;
    int diff = targetDir - currentDir;
    if (diff > 3) diff -= core::kDirectionCount;
    if (diff < -3) diff += core::kDirectionCount;
    if (diff > 0) {
        for (int i = 0; i < diff; ++i)
            result.push_back('R');
    } else if (diff < 0) {
        for (int i = 0; i < -diff; ++i)
            result.push_back('r');
    }
    return result;
}

constexpr std::size_t kSearchRadius = 2U;
constexpr int kCycleCostRotate = 2;
constexpr int kCycleCostGrabDrop = 2;
constexpr int kCycleCostExtendRetract = 1;
constexpr int kCycleCostDefault = 1;
constexpr int kArea = 2;
constexpr int kCostPerArea = 100;

void AppendRotationInstructions(std::vector<io::ArmInstruction>& instructions, int fromDirection,
                                int toDirection) {
    for (const char action : RotateSeq(fromDirection, toDirection)) {
        instructions.push_back({action, 0});
    }
}

struct LengthInstructionConfig {
    int length;
    char action;
};

void AppendLengthInstructions(std::vector<io::ArmInstruction>& instructions,
                              LengthInstructionConfig config) {
    for (int step_idx = 0; step_idx < config.length; ++step_idx) {
        instructions.push_back({config.action, 0});
    }
}

[[nodiscard]] std::size_t InvalidIndex() {
    return static_cast<std::size_t>(-1);
}

} // namespace

GreedyPlannerSolver::GreedyPlannerSolver() : BaseSolver("GreedyPlannerSolver") {
    auto logger = opusoptima::GetLoggerOrFallback("solver");
    logger->debug("GreedyPlannerSolver constructed");
}

core::HexCoord GreedyPlannerSolver::ChooseArmPosition(const io::PuzzleData& puzzle) {
    const PositionSummary positions = CollectAtomPositions(puzzle);
    if (positions.inputPositions.empty() && positions.outputPositions.empty()) {
        return {.q = 0, .r = 0};
    }

    const core::HexCoord center = ComputeCentroid(positions);
    core::HexCoord best = center;
    int bestDist = std::numeric_limits<int>::max();
    for (int delta_q = -static_cast<int>(kSearchRadius); delta_q <= static_cast<int>(kSearchRadius);
         ++delta_q) {
        for (int delta_r = -static_cast<int>(kSearchRadius);
             delta_r <= static_cast<int>(kSearchRadius); ++delta_r) {
            if (std::abs(-delta_q - delta_r) > static_cast<int>(kSearchRadius)) {
                continue;
            }

            const core::HexCoord candidate{.q = center.q + delta_q, .r = center.r + delta_r};
            const CandidateScore score = EvaluateCandidate(candidate, positions);
            if (score.occupied) {
                continue;
            }

            const int total_distance = score.distanceToInputs + score.distanceToOutputs;
            if (total_distance < bestDist) {
                bestDist = total_distance;
                best = candidate;
            }
        }
    }

    return best;
}

auto GreedyPlannerSolver::AssignAtoms(const io::PuzzleData& puzzle) -> std::vector<AtomAssignment> {
    std::vector<AssignmentEntry> inputs;
    std::vector<AssignmentEntry> outputs;
    for (const auto& molecule : puzzle.inputs) {
        for (const auto& atom : molecule.atoms) {
            inputs.push_back({inputs.size(), atom.position, atom.type});
        }
    }
    for (const auto& molecule : puzzle.outputs) {
        for (const auto& atom : molecule.atoms) {
            outputs.push_back({outputs.size(), atom.position, atom.type});
        }
    }

    std::vector<AtomAssignment> result;
    std::vector<bool> used(inputs.size(), false);
    for (const auto& output_entry : outputs) {
        std::size_t best_input_idx = InvalidIndex();
        int best_distance = std::numeric_limits<int>::max();
        for (std::size_t input_idx = 0; input_idx < inputs.size(); ++input_idx) {
            if (used[input_idx] || inputs[input_idx].type != output_entry.type) {
                continue;
            }

            const int distance = core::Distance(inputs[input_idx].pos, output_entry.pos);
            if (distance < best_distance) {
                best_distance = distance;
                best_input_idx = input_idx;
            }
        }

        if (best_input_idx != InvalidIndex()) {
            used[best_input_idx] = true;
            result.push_back({best_input_idx, inputs[best_input_idx].pos, output_entry.idx,
                              output_entry.pos, output_entry.type});
        } else {
            result.push_back({InvalidIndex(),
                              {.q = 0, .r = 0},
                              output_entry.idx,
                              output_entry.pos,
                              output_entry.type});
        }
    }

    return result;
}

std::vector<io::ArmInstruction> GreedyPlannerSolver::PlanAtomMove(
    core::HexCoord armBase, const AtomAssignment& assignment) {
    std::vector<io::ArmInstruction> instructions;

    const ArmReach from_reach = ReachTarget(armBase, assignment.inputPos);
    AppendRotationInstructions(instructions, 0, from_reach.direction);
    AppendLengthInstructions(instructions, {.length = from_reach.length, .action = 'g'});
    instructions.push_back({'G', 0});
    AppendLengthInstructions(instructions, {.length = from_reach.length, .action = 'e'});

    const ArmReach to_reach = ReachTarget(armBase, assignment.outputPos);
    AppendRotationInstructions(instructions, from_reach.direction, to_reach.direction);
    AppendLengthInstructions(instructions, {.length = to_reach.length, .action = 'g'});
    instructions.push_back({'X', 0});
    AppendLengthInstructions(instructions, {.length = to_reach.length, .action = 'e'});

    return instructions;
}

std::int32_t GreedyPlannerSolver::EstimateCyclesFromInstrs(
    const std::vector<io::ArmInstruction>& instrs) {
    std::int32_t cycles = 0;
    for (const auto& instruction : instrs) {
        switch (instruction.action) {
        case 'R':
        case 'r':
            cycles += kCycleCostRotate;
            break;
        case 'G':
        case 'X':
            cycles += kCycleCostGrabDrop;
            break;
        case 'g':
        case 'e':
            cycles += kCycleCostExtendRetract;
            break;
        default:
            cycles += kCycleCostDefault;
            break;
        }
    }
    return cycles;
}

GreedyPlannerSolver::PositionSummary GreedyPlannerSolver::CollectAtomPositions(
    const io::PuzzleData& puzzle) {
    PositionSummary positions;
    for (const auto& molecule : puzzle.inputs) {
        for (const auto& atom : molecule.atoms) {
            positions.inputPositions.push_back(atom.position);
        }
    }
    for (const auto& molecule : puzzle.outputs) {
        for (const auto& atom : molecule.atoms) {
            positions.outputPositions.push_back(atom.position);
        }
    }
    return positions;
}

core::HexCoord GreedyPlannerSolver::ComputeCentroid(const PositionSummary& positions) {
    int sum_q = 0;
    int sum_r = 0;

    for (const auto& position : positions.inputPositions) {
        sum_q += position.q;
        sum_r += position.r;
    }
    for (const auto& position : positions.outputPositions) {
        sum_q += position.q;
        sum_r += position.r;
    }

    const std::size_t point_count =
        positions.inputPositions.size() + positions.outputPositions.size();
    const int center_q = static_cast<int>(
        std::llround(static_cast<double>(sum_q) / static_cast<double>(point_count)));
    const int center_r = static_cast<int>(
        std::llround(static_cast<double>(sum_r) / static_cast<double>(point_count)));
    return {.q = center_q, .r = center_r};
}

GreedyPlannerSolver::CandidateScore GreedyPlannerSolver::EvaluateCandidate(
    core::HexCoord candidate, const PositionSummary& positions) {
    const bool occupied = IsOccupied(candidate, positions.inputPositions) ||
                          IsOccupied(candidate, positions.outputPositions);
    if (occupied) {
        return {
            .occupied = true,
            .distanceToInputs = 0,
            .distanceToOutputs = 0,
        };
    }

    return {
        .occupied = false,
        .distanceToInputs = MinDistanceToSet(candidate, positions.inputPositions),
        .distanceToOutputs = MinDistanceToSet(candidate, positions.outputPositions),
    };
}

bool GreedyPlannerSolver::IsOccupied(core::HexCoord candidate,
                                     const std::vector<core::HexCoord>& positions) {
    return std::ranges::any_of(
        positions, [candidate](const core::HexCoord& position) { return position == candidate; });
}

int GreedyPlannerSolver::MinDistanceToSet(core::HexCoord origin,
                                          const std::vector<core::HexCoord>& positions) {
    if (positions.empty()) {
        return 0;
    }

    int min_distance = std::numeric_limits<int>::max();
    for (const auto& position : positions) {
        min_distance = std::min(min_distance, core::Distance(origin, position));
    }
    return min_distance;
}

io::SolutionData GreedyPlannerSolver::Solve(const io::PuzzleData& puzzle) {
    auto logger = opusoptima::GetLoggerOrFallback("solver");
    logger->info("GreedyPlanner solving puzzle '{}' ({} inputs, {} outputs)", puzzle.name,
                 puzzle.inputs.size(), puzzle.outputs.size());

    io::SolutionData result;
    result.puzzleName = puzzle.name;
    result.solutionName = "GreedyPlanner Generated Solution";

    auto armBase = ChooseArmPosition(puzzle);
    logger->debug("Chose arm position: ({},{})", armBase.q, armBase.r);

    auto assignments = AssignAtoms(puzzle);
    logger->info("Assigned {} atom pairs", assignments.size());

    std::vector<io::ArmInstruction> allInstrs;
    for (const auto& assignment : assignments) {
        if (assignment.inputIdx == InvalidIndex()) {
            continue;
        }
        auto move = PlanAtomMove(armBase, assignment);
        allInstrs.insert(allInstrs.end(), move.begin(), move.end());
    }
    logger->info("Generated {} instructions for 1 arm", allInstrs.size());

    result.metrics.cycles = EstimateCyclesFromInstrs(allInstrs);
    result.metrics.instructionCount = static_cast<std::int32_t>(allInstrs.size());
    io::SolutionObject obj;
    obj.typeName = "arm1";
    obj.position = armBase;
    obj.instructions = allInstrs;
    result.objects.push_back(obj);

    result.metrics.area = kArea;
    result.metrics.cost = result.metrics.area * kCostPerArea;
    logger->info("Solution ready — {} cycles, {} instrs, area={}, cost={}", result.metrics.cycles,
                 result.metrics.instructionCount, result.metrics.area, result.metrics.cost);
    return result;
}

std::int32_t GreedyPlannerSolver::EstimateCycles(const io::PuzzleData& puzzle) {
    return Solve(puzzle).metrics.cycles;
}

// ── Self-registration ─────────────────────────────────────────

namespace {
const bool kRegistered = [] {
    BaseSolver::Register("GreedyPlannerSolver",
                         [] { return std::make_unique<GreedyPlannerSolver>(); });
    // aliases for CLI convenience
    BaseSolver::Register("greedy", [] { return std::make_unique<GreedyPlannerSolver>(); });
    BaseSolver::Register("default", [] { return std::make_unique<GreedyPlannerSolver>(); });
    return true;
}();
} // namespace

} // namespace opusoptima::solvers
