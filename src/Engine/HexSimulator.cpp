#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

#include "CollisionLUT.hpp"
#include "HexSimulator.hpp"
#include "LogManager.hpp"

#include <spdlog/spdlog.h>

namespace opusoptima::engine {

namespace {

/// Direction offset vectors for the 6 hex directions.
constexpr std::array<core::HexCoord, core::kDirectionCount> kDirVec = {{
    {.q = 1, .r = 0},
    {.q = 1, .r = -1},
    {.q = 0, .r = -1},
    {.q = -1, .r = 0},
    {.q = -1, .r = 1},
    {.q = 0, .r = 1},
}};

/// Compute the hex cell at the tip of an arm.
[[nodiscard]] core::HexCoord ArmTip(const core::HexCoord base, const core::ArmDirection dir,
                                    const int length) {
    const std::size_t dirIdx = static_cast<std::size_t>(dir) % core::kDirectionCount;
    return {.q = base.q + (kDirVec[dirIdx].q * length), .r = base.r + (kDirVec[dirIdx].r * length)};
}

/// Apply a rotation to a direction (positive = CW, negative = CCW).
[[nodiscard]] core::ArmDirection RotateDir(core::ArmDirection dir, int steps) {
    const int mod = static_cast<int>(core::kDirectionCount);
    return static_cast<core::ArmDirection>(((static_cast<int>(dir) + steps) % mod + mod) % mod);
}

/// Check if a grid cell is occupied (excluding the given owner).
[[nodiscard]] bool IsOccupied(const core::MachineState& state, const core::HexCoord pos,
                              std::uint8_t excludeOwner) {
    auto iter = state.grid.find(pos);
    if (iter == state.grid.end()) {
        return false;
    }
    return iter->second.ownerId != excludeOwner;
}

/// Place an atom on the grid.
void PlaceAtom(core::MachineState& state, const core::HexCoord pos, core::AtomType type,
               std::uint8_t ownerId) {
    state.grid[pos] = core::Cell{
        .occupant = core::CellOccupant::Atom,
        .ownerId = ownerId,
        .atomType = type,
    };
}

/// Determine arm type from object type name.
[[nodiscard]] core::ArmType GetArmType(const std::string& typeName) {
    if (typeName.starts_with("arm")) {
        return core::ArmType::Arm1;
    }
    if (typeName == "piston") {
        return core::ArmType::Piston;
    }
    if (typeName == "baron") {
        return core::ArmType::Baron;
    }
    if (typeName == "track") {
        return core::ArmType::Track;
    }
    return core::ArmType::Arm1;
}

} // namespace

core::MachineState HexSimulator::InitialiseFromPuzzle(const io::PuzzleData& puzzle) {
    core::MachineState state;

    std::uint8_t ownerId = 0;
    for (const auto& molecule : puzzle.inputs) {
        for (const auto& atom : molecule.atoms) {
            PlaceAtom(state, atom.position, atom.type, ownerId);
        }
        ++ownerId;
    }
    return state;
}

void HexSimulator::PlaceSolutionObjects(core::MachineState& state,
                                        const io::SolutionData& solution) {
    state.arms.clear();

    for (const auto& obj : solution.objects) {
        if (obj.instructions.empty()) {
            continue;
        }

        core::ArmState arm;
        arm.basePosition = obj.position;
        arm.direction = core::ArmDirection::D0;
        arm.armLength = 0;
        arm.hasAtom = false;
        arm.instructionPtr = 0;
        arm.cycleOffset = 0;
        arm.active = true;
        arm.armType = GetArmType(obj.typeName);
        state.arms.push_back(arm);

        state.grid[obj.position] = core::Cell{
            .occupant = core::CellOccupant::ArmBase,
            .ownerId = static_cast<std::uint8_t>(state.arms.size() - 1),
        };
    }
}

void HexSimulator::ExecuteInstruction(core::MachineState& state, core::ArmState& arm,
                                      const io::ArmInstruction& instr) {
    const auto owner = static_cast<std::uint8_t>(&arm - state.arms.data());

    switch (instr.action) {
    case core::ArmAction::kGrab:
        TryGrab(state, arm);
        break;
    case core::ArmAction::kDrop:
        TryDrop(state, arm, owner);
        break;
    case core::ArmAction::kRotateCW:
        TryRotate(1, state, arm, owner);
        break;
    case core::ArmAction::kRotateCCW:
        TryRotate(-1, state, arm, owner);
        break;
    case core::ArmAction::kExtend:
    case core::ArmAction::kAdvance:
        TryExtend(1, state, arm, owner);
        break;
    case core::ArmAction::kRetract:
    case core::ArmAction::kWithdraw:
        TryRetract(arm);
        break;
    case core::ArmAction::kExtendHigh:
        TryExtend(2, state, arm, owner);
        break;
    case core::ArmAction::kPivotCW:
        TryPivot(arm, 1);
        break;
    case core::ArmAction::kPivotCCW:
        TryPivot(arm, -1);
        break;
    case core::ArmAction::kCollision:
    default:
        AdvanceIp(arm);
        break;
    }
}

// ── Instruction handler helpers ─────────────────────────────────────

void HexSimulator::TryGrab(core::MachineState& state, core::ArmState& arm) {
    arm.armLength = 1;
    const core::HexCoord tip = ArmTip(arm.basePosition, arm.direction, arm.armLength);
    auto iter = state.grid.find(tip);
    if (iter != state.grid.end() && iter->second.occupant == core::CellOccupant::Atom) {
        arm.hasAtom = true;
        arm.heldAtom = iter->second.atomType;
        state.grid.erase(iter);
    }
    AdvanceIp(arm);
}

void HexSimulator::TryDrop(core::MachineState& state, core::ArmState& arm, std::uint8_t owner) {
    if (arm.hasAtom) {
        arm.armLength = 1;
        if (arm.armLength > 0) {
            const core::HexCoord tip = ArmTip(arm.basePosition, arm.direction, arm.armLength);
            if (!IsOccupied(state, tip, owner)) {
                PlaceAtom(state, tip, arm.heldAtom, owner);
            }
        }
        arm.hasAtom = false;
    }
    arm.armLength = 0;
    AdvanceIp(arm);
}

void HexSimulator::TryRotate(int steps, core::MachineState& state, core::ArmState& arm,
                             std::uint8_t owner) {
    const auto previous_direction = arm.direction;
    arm.direction = RotateDir(arm.direction, steps);
    if (arm.armLength > 0) {
        const auto sweep = CollisionLUT::Instance().SweepCells(arm.basePosition, previous_direction,
                                                               arm.armLength);
        for (const auto& cell : sweep) {
            if (state.grid.contains(cell) && state.grid.at(cell).ownerId != owner) {
                arm.direction = previous_direction;
                break;
            }
        }
    }
    AdvanceIp(arm);
}

void HexSimulator::TryExtend(int amount, core::MachineState& state, core::ArmState& arm,
                             std::uint8_t owner) {
    const int new_len = arm.armLength + amount;
    const core::HexCoord tip = ArmTip(arm.basePosition, arm.direction, new_len);
    if (!IsOccupied(state, tip, owner)) {
        arm.armLength = new_len;
    }
    AdvanceIp(arm);
}

void HexSimulator::TryRetract(core::ArmState& arm) {
    if (arm.armLength > 0) {
        arm.armLength--;
    }
    AdvanceIp(arm);
}

void HexSimulator::TryPivot(core::ArmState& arm, int steps) {
    arm.direction = RotateDir(arm.direction, steps);
    AdvanceIp(arm);
}

std::size_t HexSimulator::AdvanceIp(core::ArmState& arm) {
    return ++arm.instructionPtr;
}

// TODO: implement full simulation loop with cycle management, arm synchronization, etc.
void HexSimulator::Step(core::MachineState& /*state*/) {}

[[nodiscard]] std::int32_t HexSimulator::Run(const io::PuzzleData& puzzle,
                                             const io::SolutionData& solution,
                                             core::MachineState& outState) {
    auto logger = opusoptima::GetLoggerOrFallback("simulator");
    logger->info("Simulation started — {} arms, {} input atoms, max {} cycles",
                 solution.objects.size(), puzzle.inputs.size(), solution.metrics.cycles);

    outState = InitialiseFromPuzzle(puzzle);

    // Build arm data from solution objects
    struct ArmEntry {
        const io::SolutionObject* obj;
        std::uint8_t idx;
    };
    std::vector<ArmEntry> armEntries;
    for (const auto& obj : solution.objects) {
        if (obj.typeName.starts_with("arm") || obj.typeName == "piston" ||
            obj.typeName == "baron" || obj.typeName == "track") {
            if (!obj.instructions.empty()) {
                armEntries.push_back({&obj, static_cast<std::uint8_t>(armEntries.size())});
            }
        }
    }
    logger->debug("{} instruction-bearing objects found", armEntries.size());

    // Set up arms
    outState.arms.clear();
    for (const auto& arm_entry : armEntries) {
        const auto* obj = arm_entry.obj;
        core::ArmState arm;
        arm.basePosition = obj->position;
        arm.direction = core::ArmDirection::D0;
        arm.armLength = 0;
        arm.hasAtom = false;
        arm.instructionPtr = 0;
        arm.cycleOffset = 0;
        arm.active = !obj->instructions.empty();
        arm.armType = GetArmType(obj->typeName);
        outState.arms.push_back(arm);

        outState.grid[obj->position] = core::Cell{
            .occupant = core::CellOccupant::ArmBase,
            .ownerId = static_cast<std::uint8_t>(armEntries.size()),
        };
        logger->trace("Arm {} placed at ({},{}) type={} ic={}", armEntries.size(), obj->position.q,
                      obj->position.r, obj->typeName, obj->instructions.size());
    }

    const std::int32_t maxCycles = solution.metrics.cycles;
    bool allDone = false;

    for (std::int32_t cycle = 0; cycle < maxCycles && !allDone; ++cycle) {
        outState.cycle = cycle;
        allDone = true;

        for (std::size_t i = 0; i < outState.arms.size(); ++i) {
            auto& arm = outState.arms[i];
            if (!arm.active) {
                continue;
            }
            if (arm.cycleOffset > 0) {
                arm.cycleOffset--;
                allDone = false;
                continue;
            }

            const auto& instructions = armEntries[i].obj->instructions;
            if (arm.instructionPtr >= instructions.size()) {
                arm.active = false;
                continue;
            }

            allDone = false;
            const auto& instr = instructions[arm.instructionPtr];
            ExecuteInstruction(outState, arm, instr);
        }
    }

    outState.cycle = maxCycles;

    // Count final grid atoms
    std::size_t atomCount = 0;
    for (const auto& [pos, cell] : outState.grid) {
        if (cell.occupant == core::CellOccupant::Atom) ++atomCount;
    }
    logger->info("Simulation finished — {} cycles, {} atoms on grid, allDone={}", maxCycles,
                 atomCount, allDone);
    return maxCycles;
}

} // namespace opusoptima::engine
