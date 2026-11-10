#include "ComposeAssembly.hpp"

#include <array>
#include <cstddef>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace opusoptima::solvers {
namespace {

// Axial direction table, identical to `core::kAxialDirections`.
constexpr std::array<core::HexCoord, 6> kSteps = {
    core::HexCoord{1, 0}, core::HexCoord{1, -1}, core::HexCoord{0, -1},
    core::HexCoord{-1, 0}, core::HexCoord{-1, 1}, core::HexCoord{0, 1},
};

// `arm1` geometry: reach 3, and `r` moves the facing one step down the table.
constexpr int kArmReach = 3;
constexpr int kRotateStep = -1;
// Ticks reserved per transfer component inside one period.
constexpr int kArmSlotTicks = 4;

struct Cell {
    int q{};
    int r{};

    friend auto operator<=>(const Cell&, const Cell&) = default;
};

Cell ToCell(core::HexCoord coord) {
    return {coord.q, coord.r};
}

Cell Step(int direction) {
    const auto& step = kSteps[static_cast<std::size_t>((direction % 6 + 6) % 6)];
    return {step.q, step.r};
}

Cell Add(Cell a, Cell b) {
    return {a.q + b.q, a.r + b.r};
}

Cell Scale(Cell a, int k) {
    return {a.q * k, a.r * k};
}

Cell Subtract(Cell a, Cell b) {
    return {a.q - b.q, a.r - b.r};
}

bool SameDirection(Cell delta, int direction) {
    const Cell step = Step(direction);
    return delta.q == step.q && delta.r == step.r;
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

// A single-atom reagent source that carries `element`, or -1.
int SingleAtomSourceIndex(const io::PuzzleData& puzzle, core::AtomType element) {
    for (std::size_t i = 0; i < puzzle.inputs.size(); ++i) {
        const auto& molecule = puzzle.inputs[i];
        if (molecule.atoms.size() == 1 && molecule.bonds.empty() &&
            molecule.atoms.front().type == element)
            return static_cast<int>(i);
    }
    return -1;
}

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
    // Product frame: atom 0 sits on the output port, every other atom keeps its
    // relative offset, so the machine is a function of the molecule alone.
    const auto origin = ToCell(output.atoms.front().position);
    std::vector<Cell> targets;
    targets.reserve(output.atoms.size());
    for (const auto& atom : output.atoms) targets.push_back(Subtract(ToCell(atom.position), origin));

    io::SolutionData machine;
    machine.puzzleName = puzzle.name;
    machine.solutionName = "Composed assembly";
    std::set<Cell> occupied;
    occupied.insert(targets.front()); // the output port

    struct Placement {
        std::size_t atom{};
        Cell target{};
        Cell reagent{};
        Cell base{};
        int grab{};
        int source{};
    };
    std::vector<Placement> placements;

    for (std::size_t i = 0; i < targets.size(); ++i) {
        const Cell target = targets[i];
        const int source = SingleAtomSourceIndex(puzzle, output.atoms[i].type);
        bool placed = false;
        for (int grab = 0; grab < 6 && !placed; ++grab) {
            const int drop = ((grab + kRotateStep) % 6 + 6) % 6;
            // target = base + reach*drop and reagent = base + reach*grab, so the
            // reagent cell follows from the target and the two facing directions.
            const Cell base = Subtract(target, Scale(Step(drop), kArmReach));
            const Cell reagent = Add(base, Scale(Step(grab), kArmReach));
            if (occupied.count(base) || occupied.count(reagent)) continue;
            occupied.insert(base);
            occupied.insert(reagent);
            placements.push_back({i, target, reagent, base, grab, source});
            placed = true;
        }
        if (!placed) throw std::invalid_argument("composed assembly found no free arm placement");
    }

    // Components are pipelined: each transfer arm grabs, rotates and releases in
    // its own slot of the period so two arms never sweep through the same cells at
    // the same time.
    int tick = 0;
    for (const auto& placement : placements) {
        PushObject(machine, "input", placement.reagent, {1, 0, placement.source, 0, 0});
        PushObject(machine, "arm1", placement.base, {kArmReach, placement.grab, 0, 0, 0},
                   {{'G', tick}, {kRotateStep < 0 ? 'r' : 'R', tick + 1}, {'X', tick + 2}});
        tick += kArmSlotTicks;
    }
    PushObject(machine, "out-std", targets.front(), {1, 0, 0, 0, 0});

    // Bonding stations: the engine model (`third_party/omsim/sim.c`, `BONDING`)
    // bonds the atom on the bonder cell with the atom one step along its facing,
    // so a bond between two adjacent product cells maps to exactly one posture.
    for (const auto& bond : output.bonds) {
        const Cell a = Subtract(ToCell(bond.from), origin);
        const Cell b = Subtract(ToCell(bond.to), origin);
        const Cell delta = Subtract(b, a);
        int facing = -1;
        for (int direction = 0; direction < 6; ++direction)
            if (SameDirection(delta, direction)) facing = direction;
        if (facing < 0)
            throw std::invalid_argument("composed assembly requires adjacent bonded atoms");
        PushObject(machine, "bonder", a, {1, facing, 0, 0, 0});
    }
    return machine;
}

} // namespace opusoptima::solvers
