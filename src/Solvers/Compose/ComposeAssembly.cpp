#include "ComposeAssembly.hpp"

#include <array>
#include <cstddef>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace opusoptima::solvers {
namespace {

// Verifier direction order (`third_party/omsim/sim.c: direction_for_offset`):
// 0:(1,0) 1:(0,1) 2:(-1,1) 3:(-1,0) 4:(0,-1) 5:(1,-1). Using this order is what
// makes an emitted rotation mean the same cell to the native gate.
constexpr std::array<core::HexCoord, 6> kSteps = {
    core::HexCoord{1, 0}, core::HexCoord{0, 1}, core::HexCoord{-1, 1},
    core::HexCoord{-1, 0}, core::HexCoord{0, -1}, core::HexCoord{1, -1},
};

// `arm1` geometry: reach 3, and `r` advances the facing by one table step.
constexpr int kArmReach = 3;
constexpr int kRotateStep = 1;

struct Cell {
    int q{};
    int r{};

    friend auto operator<=>(const Cell&, const Cell&) = default;
};

Cell Add(Cell a, Cell b) {
    return {a.q + b.q, a.r + b.r};
}

Cell Subtract(Cell a, Cell b) {
    return {a.q - b.q, a.r - b.r};
}

Cell Scale(Cell a, int k) {
    return {a.q * k, a.r * k};
}

Cell Step(int direction) {
    const auto& step = kSteps[static_cast<std::size_t>((direction % 6 + 6) % 6)];
    return {step.q, step.r};
}

int HexDistance(Cell a, Cell b) {
    const Cell delta = Subtract(a, b);
    const int ds = -delta.q - delta.r;
    const int aq = delta.q < 0 ? -delta.q : delta.q;
    const int ar = delta.r < 0 ? -delta.r : delta.r;
    const int as = ds < 0 ? -ds : ds;
    int best = aq > ar ? aq : ar;
    return best > as ? best : as;
}

// Direction whose radius-three ring cell is `target` seen from `base`.
int RingDirection(Cell target, Cell base) {
    for (int direction = 0; direction < 6; ++direction) {
        const Cell cell = Add(base, Scale(Step(direction), kArmReach));
        if (cell.q == target.q && cell.r == target.r) return direction;
    }
    return -1;
}

int DirectionOf(Cell delta) {
    for (int direction = 0; direction < 6; ++direction) {
        const Cell step = Step(direction);
        if (delta.q == step.q && delta.r == step.r) return direction;
    }
    return -1;
}

int PushObject(io::SolutionData& machine, const char* type, Cell position, std::vector<int> body,
               std::vector<io::ArmInstruction> tape = {}) {
    io::SolutionObject object;
    object.typeName = type;
    object.position = core::HexCoord{.q = position.q, .r = position.r};
    object.instructions = std::move(tape);
    object.bodyData = std::move(body);
    machine.objects.push_back(std::move(object));
    return static_cast<int>(machine.objects.size()) - 1;
}

// Single-atom reagent source carrying `element`, or -1.
int SingleAtomSourceIndex(const io::PuzzleData& puzzle, core::AtomType element) {
    for (std::size_t i = 0; i < puzzle.inputs.size(); ++i) {
        const auto& molecule = puzzle.inputs[i];
        if (molecule.atoms.size() == 1 && molecule.bonds.empty() &&
            molecule.atoms.front().type == element)
            return static_cast<int>(i);
    }
    return -1;
}

void AppendRotation(std::vector<io::ArmInstruction>& tape, int& tick, int& facing, int target) {
    const int forward = ((target - facing) % 6 + 6) % 6;
    const int backward = 6 - forward;
    const bool counterClockwise = forward <= backward;
    const int steps = counterClockwise ? forward : backward;
    for (int i = 0; i < steps; ++i) {
        tape.push_back({counterClockwise ? 'r' : 'R', tick++});
        facing = ((facing + (counterClockwise ? 1 : -1)) % 6 + 6) % 6;
    }
}

void AppendBonders(io::SolutionData& machine, const io::Molecule& output) {
    for (const auto& bond : output.bonds) {
        const Cell a{bond.from.q, bond.from.r};
        const Cell b{bond.to.q, bond.to.r};
        const int facing = DirectionOf(Subtract(b, a));
        if (facing < 0)
            throw std::invalid_argument("composed assembly requires adjacent bonded atoms");
        PushObject(machine, "bonder", a, {1, facing, 0, 0, 0});
    }
}

// Shared-ring layout: one arm serves every product cell that lies on a common
// radius-three ring, which is the cheapest machine shape the gate accepts.
std::optional<io::SolutionData> BuildRingGroup(const io::PuzzleData& puzzle,
                                               const std::vector<Cell>& targets) {
    if (targets.size() < 2) return std::nullopt;
    Cell best{};
    std::size_t bestCount = 0;
    for (int q = -6; q <= 6; ++q) {
        for (int r = -6; r <= 6; ++r) {
            const Cell base{q, r};
            std::size_t count = 0;
            for (const Cell& target : targets)
                if (HexDistance(target, base) == kArmReach) ++count;
            if (count > bestCount) {
                bestCount = count;
                best = base;
            }
        }
    }
    if (bestCount < 2) return std::nullopt;

    std::vector<std::size_t> ring;
    std::vector<std::size_t> extra;
    for (std::size_t i = 0; i < targets.size(); ++i) {
        if (HexDistance(targets[i], best) == kArmReach)
            ring.push_back(i);
        else
            extra.push_back(i);
    }
    if (ring.size() < 2) return std::nullopt;

    std::vector<int> facing;
    facing.reserve(ring.size());
    for (const auto index : ring) {
        const int direction = RingDirection(targets[index], best);
        if (direction < 0) return std::nullopt;
        facing.push_back(direction);
    }
    std::set<int> unique(facing.begin(), facing.end());
    if (unique.size() != facing.size()) return std::nullopt;

    io::SolutionData machine;
    machine.puzzleName = puzzle.name;
    machine.solutionName = "Composed ring assembly";

    int tick = 0;
    int current = facing.front();
    std::vector<io::ArmInstruction> tape;
    const auto& output = puzzle.outputs.front();
    for (std::size_t k = 0; k < ring.size(); ++k) {
        const int target = facing[k];
        const int reagent = ((target - kRotateStep) % 6 + 6) % 6;
        AppendRotation(tape, tick, current, reagent);
        tape.push_back({'G', tick++});
        AppendRotation(tape, tick, current, target);
        tape.push_back({'X', tick++});
        PushObject(machine, "input", Add(best, Scale(Step(reagent), kArmReach)),
                   {1, 0, SingleAtomSourceIndex(puzzle, output.atoms[ring[k]].type), 0, 0});
    }
    PushObject(machine, "arm1", best, {kArmReach, facing.front(), 0, 0, 0}, std::move(tape));

    const std::set<Cell> occupied(targets.begin(), targets.end());
    for (const auto index : extra) {
        const Cell target = targets[index];
        bool placed = false;
        for (const int grab : {3, 2, 4, 1, 5, 0}) {
            const int drop = ((grab + kRotateStep) % 6 + 6) % 6;
            const Cell base = Subtract(target, Scale(Step(drop), kArmReach));
            const Cell reagent = Add(base, Scale(Step(grab), kArmReach));
            if (occupied.count(base) || base == best) continue;
            PushObject(machine, "input", reagent,
                       {1, 0, SingleAtomSourceIndex(puzzle, output.atoms[index].type), 0, 0});
            PushObject(machine, "arm1", base, {kArmReach, grab, 0, 0, 0},
                       {{'G', 0}, {kRotateStep > 0 ? 'r' : 'R', 1}, {'X', 2}});
            placed = true;
            break;
        }
        if (!placed) return std::nullopt;
    }

    PushObject(machine, "out-std", Cell{0, 0}, {1, 0, 0, 0, 0});
    try {
        AppendBonders(machine, output);
    } catch (const std::invalid_argument&) {
        return std::nullopt;
    }
    return machine;
}

// One arm per product atom: costs 20 per atom but pipelines perfectly.
class Placement {
    public:
    explicit Placement(std::vector<Cell> targets) : targets_(std::move(targets)) {}

    struct Arm {
        Cell base{};
        Cell reagent{};
        int grab{};
    };

    [[nodiscard]] const std::vector<Arm>& Arms() const noexcept {
        return arms_;
    }

    bool Solve() {
        occupied_ = std::set<Cell>(targets_.begin(), targets_.end());
        return Visit(0);
    }

    private:
    bool Visit(std::size_t index) {
        if (index == targets_.size()) return true;
        const Cell target = targets_[index];
        // Facing 3 (west) first: it keeps the machine in the same minimal shape as the
        // hand-verified single-atom template, then the neighbours, then the rest.
        static constexpr std::array<int, 6> kFacingOrder = {3, 2, 4, 1, 5, 0};
        for (const int grab : kFacingOrder) {
            const int drop = ((grab + kRotateStep) % 6 + 6) % 6;
            const Cell base = Subtract(target, Scale(Step(drop), kArmReach));
            const Cell reagent = Add(base, Scale(Step(grab), kArmReach));
            if (used_.count(base) || used_.count(reagent)) continue;
            if (occupied_.count(base) || occupied_.count(reagent)) continue;
            const std::array<Cell, 2> sweep = {
                Add(base, Add(Scale(Step(grab), 2), Step(drop))),
                Add(base, Add(Step(grab), Scale(Step(drop), 2))),
            };
            bool blocked = false;
            for (const Cell& cell : sweep) {
                if (used_.count(cell)) blocked = true;
                if (occupied_.count(cell) && cell != target) blocked = true;
            }
            if (blocked) continue;
            used_.insert(base);
            used_.insert(reagent);
            arms_.push_back({base, reagent, grab});
            if (Visit(index + 1)) return true;
            arms_.pop_back();
            used_.erase(base);
            used_.erase(reagent);
        }
        return false;
    }

    std::vector<Cell> targets_;
    std::set<Cell> occupied_;
    std::set<Cell> used_;
    std::vector<Arm> arms_;
};

} // namespace

bool SupportsComposedAssembly(const io::PuzzleData& puzzle) {
    if (puzzle.productionInfo || puzzle.outputs.size() != 1 || puzzle.inputs.empty()) return false;
    const auto& output = puzzle.outputs.front();
    if (output.atoms.empty()) return false;
    for (const auto& atom : output.atoms)
        if (SingleAtomSourceIndex(puzzle, atom.type) < 0) return false;
    return true;
}

std::vector<io::SolutionData> ComposeAssemblyVariants(const io::PuzzleData& puzzle) {
    if (!SupportsComposedAssembly(puzzle))
        throw std::invalid_argument("composed assembly needs every product atom to come from a "
                                    "single-atom reagent source");

    const auto& output = puzzle.outputs.front();
    std::vector<Cell> targets;
    targets.reserve(output.atoms.size());
    for (const auto& atom : output.atoms) targets.push_back(Cell{atom.position.q, atom.position.r});

    std::vector<io::SolutionData> variants;
    if (auto ring = BuildRingGroup(puzzle, targets)) variants.push_back(std::move(*ring));

    Placement placement(targets);
    if (placement.Solve()) {
        io::SolutionData machine;
        machine.puzzleName = puzzle.name;
        machine.solutionName = "Composed assembly";
        const auto& arms = placement.Arms();
        for (std::size_t i = 0; i < arms.size(); ++i)
            PushObject(machine, "input", arms[i].reagent,
                       {1, 0, SingleAtomSourceIndex(puzzle, output.atoms[i].type), 0, 0});
        for (const auto& arm : arms)
            PushObject(machine, "arm1", arm.base, {kArmReach, arm.grab, 0, 0, 0},
                       {{'G', 0}, {'r', 1}, {'X', 2}});
        PushObject(machine, "out-std", Cell{0, 0}, {1, 0, 0, 0, 0});
        AppendBonders(machine, output);
        variants.push_back(std::move(machine));
    }
    if (variants.empty())
        throw std::invalid_argument("composed assembly found no collision-free arm placement");
    return variants;
}

io::SolutionData ComposeAssemblyMachine(const io::PuzzleData& puzzle) {
    auto variants = ComposeAssemblyVariants(puzzle);
    return std::move(variants.front());
}

} // namespace opusoptima::solvers
