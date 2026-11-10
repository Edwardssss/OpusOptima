#include "ComposeAssembly.hpp"

#include <array>
#include <cstddef>
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

struct Arm {
    std::size_t atom{};
    Cell base{};
    Cell reagent{};
    int grab{};
};

// One transfer component per product atom. An arm sweeps a radius-three ring
// between its reagent cell and its target cell; the two intermediate ring cells
// must stay clear of every other atom cell, otherwise the gate reports a motion
// collision. Backtracking keeps the search cheap: a failed arm only retries its
// own six facings.
class Placement {
    public:
    explicit Placement(std::vector<Cell> targets) : targets_(std::move(targets)) {}

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
        for (int grab = 0; grab < 6; ++grab) {
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
            arms_.push_back({index, base, reagent, grab});
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

io::SolutionData ComposeAssemblyMachine(const io::PuzzleData& puzzle) {
    if (!SupportsComposedAssembly(puzzle))
        throw std::invalid_argument("composed assembly needs every product atom to come from a "
                                    "single-atom reagent source");

    const auto& output = puzzle.outputs.front();
    std::vector<Cell> targets;
    targets.reserve(output.atoms.size());
    for (const auto& atom : output.atoms) targets.push_back(Cell{atom.position.q, atom.position.r});

    Placement placement(targets);
    if (!placement.Solve())
        throw std::invalid_argument("composed assembly found no collision-free arm placement");

    io::SolutionData machine;
    machine.puzzleName = puzzle.name;
    machine.solutionName = "Composed assembly";
    for (const auto& arm : placement.Arms()) {
        const int source = SingleAtomSourceIndex(puzzle, output.atoms[arm.atom].type);
        PushObject(machine, "input", arm.reagent, {1, 0, source, 0, 0});
        PushObject(machine, "arm1", arm.base, {kArmReach, arm.grab, 0, 0, 0},
                   {{'G', 0}, {kRotateStep > 0 ? 'r' : 'R', 1}, {'X', 2}});
    }
    // The gate matches the product against the puzzle frame, so the output port
    // sits at the world origin and the atoms are placed on their puzzle cells.
    PushObject(machine, "out-std", Cell{0, 0}, {1, 0, 0, 0, 0});
    for (const auto& bond : output.bonds) {
        const Cell a{bond.from.q, bond.from.r};
        const Cell b{bond.to.q, bond.to.r};
        const int facing = DirectionOf(Subtract(b, a));
        if (facing < 0)
            throw std::invalid_argument("composed assembly requires adjacent bonded atoms");
        PushObject(machine, "bonder", a, {1, facing, 0, 0, 0});
    }
    return machine;
}

} // namespace opusoptima::solvers
