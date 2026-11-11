#include "ComposeAssembly.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <algorithm>
#include <utility>
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
// Above this product size the per-atom layout is neither cheap nor fast to search.
constexpr std::size_t kMaxComposedAtoms = 6;
constexpr int kRotateStep = 1;

// Puzzle permission bits, see `third_party/omsim/decode.c` and the game's
// mechanism allow-list. Bit 12 enables calcification, bit 14 projection.
constexpr std::uint64_t kCalcificationPermission = 1ULL << 12;
constexpr std::uint64_t kProjectionPermission = 1ULL << 14;

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



// Ring direction of `cell` seen from `pivot` when the two are exactly `length`
// hexes apart along an arm axis; -1 when that arm cannot reach it.
int AxisRingDirection(Cell cell, Cell pivot, int length) {
    const Cell delta = Subtract(cell, pivot);
    for (int direction = 0; direction < 6; ++direction) {
        const Cell ring = Scale(Step(direction), length);
        if (delta.q == ring.q && delta.r == ring.r) return direction;
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

// ── Swing transport kernel ──────────────────────────────────────────────────
// An `arm1` of size L grabs only the six cells at hex distance L along an axis
// (`pivot + L*step(d)`, `sim.c: record_swing_area` aborts above 3), and rotating
// advances the held atom along that six-cell ring. So one swing carries an atom
// from A to B exactly when `B - A == L*(step(drop) - step(grab))`, at a cost of 20
// regardless of L. That is the transport primitive the reference machines use;
// rails cost `5 * hex` plus a 40-cost piston for the same job.
constexpr int kMaxArmSize = 3;
// Face west first, then the neighbours: it reproduces the hand-verified
// single-atom template and keeps small machines byte-identical.
constexpr std::array<int, 6> kDropOrder = {3, 2, 4, 1, 5, 0};
// One rotation first, then the longer sweeps.
constexpr std::array<int, 5> kDeltaOrder = {1, -1, 2, -2, 3};

struct Swing {
    Cell pivot{};
    Cell station{}; // where the atom is grabbed (reagent area or glyph cell)
    Cell target{};  // where it is dropped (the product cell)
    int length{kMaxArmSize};
    int grab{};
    int drop{};
};

// Every swing that lands a product atom on `target`, cheapest rotation first.
// `station` is the ring cell the atom has to be picked from; callers decide
// whether that cell can host a reagent, glyph or extracted input atom.
std::vector<Swing> SwingCandidates(Cell target) {
    std::vector<Swing> candidates;
    for (const int delta : kDeltaOrder) {
        for (const int length : {kMaxArmSize, 2, 1}) {
            for (const int drop : kDropOrder) {
                const int grab = ((drop - delta) % 6 + 6) % 6;
                Swing swing;
                swing.pivot = Subtract(target, Scale(Step(drop), length));
                swing.station = Add(swing.pivot, Scale(Step(grab), length));
                swing.target = target;
                swing.length = length;
                swing.grab = grab;
                swing.drop = drop;
                candidates.push_back(swing);
            }
        }
    }
    return candidates;
}

// Signed rotation count from `grab` to `drop`; positive turns counter-clockwise
// (`r`), negative clockwise (`R`). Ties take the counter-clockwise route, which
// is what the single-step template emits.
int SwingRotation(int grab, int drop) {
    const int forward = ((drop - grab) % 6 + 6) % 6;
    return forward <= 6 - forward ? forward : forward - 6;
}

// Ring cells the grabber passes through between grab and drop, endpoints
// excluded. A sweep that crosses an atom or another machine is a collision.
std::vector<int> SweptDirections(int grab, int drop) {
    const int rotation = SwingRotation(grab, drop);
    std::vector<int> swept;
    for (int step = 1; step < (rotation > 0 ? rotation : -rotation); ++step)
        swept.push_back(((grab + (rotation > 0 ? step : -step)) % 6 + 6) % 6);
    return swept;
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

bool IsMetal(core::AtomType type) {
    return type >= core::AtomType::Gold && type <= core::AtomType::Lead;
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

// How one product atom is obtained. `Direct` lifts the finished atom from a
// single-atom reagent source; `Calcify` drops an elemental reagent on a
// calcification glyph so it turns into salt in place; `Project` parks a lower
// metal on a projection glyph's metal cell and feeds quicksilver every cycle,
// which upgrades it one rank per cycle until it is the product.
enum class StationKind { Direct, Calcify, Project };

struct Station {
    StationKind kind{StationKind::Direct};
    int input{-1};     // primary reagent source index
    int auxiliary{-1}; // quicksilver source index (projection)
    int steps{0};      // projection upgrades = cycles before the first grab
    core::AtomType reagent{core::AtomType::Salt};
    Cell cell{};  // where the product atom appears; also the arm's grab cell
    Cell glyph{}; // glyph origin (calcification/projection)
    std::array<Cell, 3> footprint{};
    std::size_t footprintCount{0};
    std::array<Cell, 2> reagentCells{};
    std::size_t reagentCount{0};

    [[nodiscard]] int Intro() const noexcept {
        switch (kind) {
        case StationKind::Direct: return 0;
        case StationKind::Calcify: return 1;
        case StationKind::Project: return steps;
        }
        return 0;
    }
};

bool ContainsReagent(const Station& station, Cell cell) {
    for (std::size_t i = 0; i < station.reagentCount; ++i)
        if (station.reagentCells[i] == cell) return true;
    return false;
}

bool ContainsFootprint(const Station& station, Cell cell) {
    for (std::size_t i = 0; i < station.footprintCount; ++i)
        if (station.footprint[i] == cell) return true;
    return false;
}

// Plans how to obtain `product` at `cell`. `std::nullopt` when the puzzle offers
// no reagent path for that atom at that cell.
std::optional<Station> PlanStation(const io::PuzzleData& puzzle, core::AtomType product, Cell cell) {
    if (const int direct = SingleAtomSourceIndex(puzzle, product); direct >= 0) {
        Station station;
        station.kind = StationKind::Direct;
        station.input = direct;
        station.reagent = product;
        station.cell = cell;
        station.reagentCells[0] = cell;
        station.reagentCount = 1;
        return station;
    }
    if (product == core::AtomType::Salt && (puzzle.permissionBits & kCalcificationPermission)) {
        for (const core::AtomType elemental :
             {core::AtomType::Water, core::AtomType::Fire, core::AtomType::Earth,
              core::AtomType::Air}) {
            const int index = SingleAtomSourceIndex(puzzle, elemental);
            if (index < 0) continue;
            Station station;
            station.kind = StationKind::Calcify;
            station.input = index;
            station.reagent = elemental;
            station.cell = cell;
            station.glyph = cell;
            station.footprint[0] = cell;
            station.footprintCount = 1;
            station.reagentCells[0] = cell;
            station.reagentCount = 1;
            return station;
        }
    }
    // Projection: quicksilver at the glyph cell, the metal on its `(1, 0)` cell.
    // The glyph transforms that cell in place, one rank per cycle, so a metal
    // source `steps` ranks below the product becomes the product in place.
    if (IsMetal(product) && (puzzle.permissionBits & kProjectionPermission)) {
        const int quicksilver = SingleAtomSourceIndex(puzzle, core::AtomType::Quicksilver);
        int metal = -1;
        for (int rank = static_cast<int>(product) + 1;
             rank <= static_cast<int>(core::AtomType::Lead); ++rank) {
            if (SingleAtomSourceIndex(puzzle, static_cast<core::AtomType>(rank)) >= 0) {
                metal = rank;
                break;
            }
        }
        if (quicksilver >= 0 && metal > static_cast<int>(product)) {
            Station station;
            station.kind = StationKind::Project;
            station.input = SingleAtomSourceIndex(puzzle, static_cast<core::AtomType>(metal));
            station.auxiliary = quicksilver;
            station.steps = metal - static_cast<int>(product);
            station.reagent = static_cast<core::AtomType>(metal);
            station.cell = cell;
            station.glyph = Subtract(cell, Step(0));
            station.footprint[0] = station.glyph;
            station.footprint[1] = cell;
            station.footprintCount = 2;
            station.reagentCells[0] = cell;
            station.reagentCells[1] = station.glyph;
            station.reagentCount = 2;
            return station;
        }
    }
    return std::nullopt;
}

// `true` when every product atom can be produced by some station, regardless of
// geometry.
bool SupportsEveryAtom(const io::PuzzleData& puzzle, const io::Molecule& output) {
    for (const auto& atom : output.atoms) {
        if (!PlanStation(puzzle, atom.type, Cell{atom.position.q, atom.position.r})) return false;
    }
    return true;
}

// Static layout: every station sits directly on its product cell, so the machine
// needs no arms at all — the reagents appear where the product must be, the
// glyphs transform them in place and the output area consumes them. This is the
// cheapest shape the gate accepts and the shape the game's slot policy rewards;
// it is only possible when no glyph covers another product atom.
void EmitStation(io::SolutionData& machine, const Station& station);

std::optional<io::SolutionData> BuildStatic(const io::PuzzleData& puzzle,
                                            const std::vector<Cell>& targets,
                                            const std::vector<core::AtomType>& types) {
    std::vector<Station> stations;
    stations.reserve(targets.size());
    const std::set<Cell> productCells(targets.begin(), targets.end());
    std::set<Cell> glyphCells;
    for (std::size_t i = 0; i < targets.size(); ++i) {
        auto station = PlanStation(puzzle, types[i], targets[i]);
        if (!station) return std::nullopt;
        for (std::size_t k = 0; k < station->footprintCount; ++k) {
            const Cell footprint = station->footprint[k];
            if (footprint != targets[i] && productCells.count(footprint)) return std::nullopt;
            if (!glyphCells.insert(footprint).second) return std::nullopt;
        }
        stations.push_back(*station);
    }
    io::SolutionData machine;
    machine.puzzleName = puzzle.name;
    machine.solutionName = "Static composed assembly";
    for (const auto& station : stations) EmitStation(machine, station);
    PushObject(machine, "out-std", Cell{0, 0}, {1, 0, 0, 0, 0});
    AppendBonders(machine, puzzle.outputs.front());
    return machine;
}

// Shared-ring layout: one arm serves every product cell that lies on a common
// radius-three ring, which is the cheapest machine shape the gate accepts.
std::optional<io::SolutionData> BuildRingGroup(const io::PuzzleData& puzzle,
                                               const std::vector<Cell>& targets) {
    if (targets.size() < 2 || targets.size() > kMaxComposedAtoms) return std::nullopt;
    // One arm serves the whole ring: pick the pivot and size that put the most
    // product atoms on the six cells that arm can reach, so one 20-cost mechanism
    // replaces a mechanism per atom.
    Cell best{};
    int bestLength = kMaxArmSize;
    std::size_t bestCount = 0;
    for (int q = -6; q <= 6; ++q) {
        for (int r = -6; r <= 6; ++r) {
            const Cell pivot{q, r};
            for (int length = kMaxArmSize; length >= 1; --length) {
                std::size_t count = 0;
                for (const Cell& target : targets)
                    if (AxisRingDirection(target, pivot, length) >= 0) ++count;
                if (count > bestCount) {
                    bestCount = count;
                    best = pivot;
                    bestLength = length;
                }
            }
        }
    }
    if (bestCount < 2) return std::nullopt;

    std::vector<std::size_t> ring;
    std::vector<std::size_t> extra;
    for (std::size_t i = 0; i < targets.size(); ++i) {
        if (AxisRingDirection(targets[i], best, bestLength) >= 0)
            ring.push_back(i);
        else
            extra.push_back(i);
    }
    if (ring.size() < 2) return std::nullopt;

    std::vector<int> facing;
    facing.reserve(ring.size());
    for (const auto index : ring) {
        const int direction = AxisRingDirection(targets[index], best, bestLength);
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
        PushObject(machine, "input", Add(best, Scale(Step(reagent), bestLength)),
                   {1, 0, SingleAtomSourceIndex(puzzle, output.atoms[ring[k]].type), 0, 0});
    }
    PushObject(machine, "arm1", best, {bestLength, facing.front(), 0, 0, 0}, std::move(tape));

    const std::set<Cell> occupied(targets.begin(), targets.end());
    for (const auto index : extra) {
        const Cell target = targets[index];
        bool placed = false;
        for (const auto& swing : SwingCandidates(target)) {
            if (occupied.count(swing.pivot) || swing.pivot == best) continue;
            PushObject(machine, "input", swing.station,
                       {1, 0, SingleAtomSourceIndex(puzzle, output.atoms[index].type), 0, 0});
            std::vector<io::ArmInstruction> armTape{{'G', 0}};
            int tick = 1;
            int route = swing.grab;
            AppendRotation(armTape, tick, route, swing.drop);
            armTape.push_back({'O', tick++});
            armTape.push_back({'X', tick++});
            PushObject(machine, "arm1", swing.pivot,
                       {swing.length, swing.grab, 0, 0, 0}, std::move(armTape));
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

// One arm per product atom: costs 20 per atom but pipelines perfectly. Every arm
// carries one product atom from its station (a plain reagent cell or a glyph
// station) onto the product frame.
class Placement {
    public:
    Placement(const io::PuzzleData& puzzle, std::vector<Cell> targets,
              std::vector<core::AtomType> types)
        : puzzle_(puzzle), targets_(std::move(targets)), types_(std::move(types)) {}

    struct Arm {
        Swing swing;
        Station station{};
    };

    [[nodiscard]] const std::vector<Arm>& Arms() const noexcept {
        return arms_;
    }

    bool Solve() {
        // Bounded search: large products have no cheap arm-per-atom layout anyway, and
        // an unbounded backtracking walk explodes on them.
        if (targets_.size() > kMaxComposedAtoms) return false;
        occupied_ = std::set<Cell>(targets_.begin(), targets_.end());
        return Visit(0);
    }

    private:
    bool Visit(std::size_t index) {
        if (index == targets_.size()) return true;
        const Cell target = targets_[index];
        // Every reachable swing is a legal station cell; the order inside
        // `SwingCandidates` starts with the hand-verified west-facing 60-degree
        // template, so existing machines stay byte-identical.
        for (const auto& swing : SwingCandidates(target)) {
            const Cell base = swing.pivot;
            const int grab = swing.grab;
            const int drop = swing.drop;
            const Cell cell = swing.station;
            if (used_.count(base) || occupied_.count(base) || blocked_.count(base)) continue;
            if (occupied_.count(cell) || used_.count(cell)) continue;
            auto station = PlanStation(puzzle_, types_[index], cell);
            if (!station) continue;
            bool clash = false;
            for (std::size_t i = 0; i < station->footprintCount && !clash; ++i) {
                const Cell footprint = station->footprint[i];
                if (used_.count(footprint) || blocked_.count(footprint)) clash = true;
                // Input areas deliberately sit on the station's own glyph cells, but no
                // glyph may cover another atom (product, station or reagent).
                if (occupied_.count(footprint) && !ContainsReagent(*station, footprint))
                    clash = true;
            }
            for (std::size_t i = 0; i < station->reagentCount && !clash; ++i) {
                const Cell reagent = station->reagentCells[i];
                if (occupied_.count(reagent) && !ContainsFootprint(*station, reagent))
                    clash = true;
            }
            // The arm body stands on the pivot and sweeps the ring between grab and
            // drop; only the target atom may be on that arc.
            for (const int sweptDirection : SweptDirections(grab, drop)) {
                const Cell swept =
                    Add(base, Scale(Step(sweptDirection), swing.length));
                if (clash) break;
                if (occupied_.count(swept)) clash = true;
                if (used_.count(swept) || blocked_.count(swept)) clash = true;
            }
            if (clash) continue;
            used_.insert(base);
            for (std::size_t i = 0; i < station->reagentCount; ++i)
                occupied_.insert(station->reagentCells[i]);
            occupied_.insert(station->cell);
            for (std::size_t i = 0; i < station->footprintCount; ++i)
                blocked_.insert(station->footprint[i]);
            arms_.push_back({swing, *station});
            if (Visit(index + 1)) return true;
            arms_.pop_back();
            for (std::size_t i = 0; i < station->footprintCount; ++i)
                blocked_.erase(station->footprint[i]);
            occupied_.erase(station->cell);
            for (std::size_t i = 0; i < station->reagentCount; ++i)
                occupied_.erase(station->reagentCells[i]);
            used_.erase(base);
        }
        return false;
    }

    const io::PuzzleData& puzzle_;
    std::vector<Cell> targets_;
    std::vector<core::AtomType> types_;
    std::set<Cell> occupied_; // product atoms, station atoms, reagent cells
    std::set<Cell> used_;     // arm bases
    std::set<Cell> blocked_;  // glyph footprints
    std::vector<Arm> arms_;
};

std::vector<io::ArmInstruction> StationTape(const Swing& swing, int intro) {
    std::vector<io::ArmInstruction> tape;
    int tick = 0;
    // A leading blank anchors the tape at index 0, so the intro delay repeats every
    // period instead of only shifting the first cycle (decode trims to the first
    // instruction index otherwise).
    if (intro > 0) tape.push_back({'O', tick++});
    while (tick < intro) tape.push_back({'O', tick++});
    tape.push_back({'G', tick++});
    const int rotation = SwingRotation(swing.grab, swing.drop);
    for (int step = 0; step < (rotation > 0 ? rotation : -rotation); ++step)
        tape.push_back({rotation > 0 ? 'r' : 'R', tick++});
    tape.push_back({'X', tick++});
    return tape;
}

void EmitStation(io::SolutionData& machine, const Station& station) {
    switch (station.kind) {
    case StationKind::Direct:
        PushObject(machine, "input", station.cell, {1, 0, station.input, 0, 0});
        break;
    case StationKind::Calcify:
        PushObject(machine, "input", station.cell, {1, 0, station.input, 0, 0});
        PushObject(machine, "glyph-calcification", station.cell, {1, 0, 0, 0, 0});
        break;
    case StationKind::Project:
        PushObject(machine, "input", station.glyph, {1, 0, station.auxiliary, 0, 0});
        PushObject(machine, "input", station.cell, {1, 0, station.input, 0, 0});
        PushObject(machine, "glyph-projection", station.glyph, {1, 0, 0, 0, 0});
        break;
    }
}

} // namespace

bool SupportsComposedAssembly(const io::PuzzleData& puzzle) {
    if (puzzle.productionInfo || puzzle.outputs.size() != 1 || puzzle.inputs.empty()) return false;
    const auto& output = puzzle.outputs.front();
    if (output.atoms.empty()) return false;
    return SupportsEveryAtom(puzzle, output);
}

bool ComposedAssemblyRequiresChemistry(const io::PuzzleData& puzzle) {
    if (puzzle.outputs.size() != 1) return false;
    for (const auto& atom : puzzle.outputs.front().atoms)
        if (SingleAtomSourceIndex(puzzle, atom.type) < 0) return true;
    return false;
}

bool ComposedAssemblyUsesInPlaceLayout(const io::PuzzleData& puzzle) {
    if (puzzle.productionInfo || puzzle.outputs.size() != 1) return false;
    if (ComposedAssemblyRequiresChemistry(puzzle)) return true;
    const auto& output = puzzle.outputs.front();
    if (output.bonds.empty() || output.atoms.size() <= kMaxComposedAtoms) return false;
    for (const auto& atom : output.atoms)
        if (SingleAtomSourceIndex(puzzle, atom.type) < 0) return false;
    return true;
}

std::vector<io::SolutionData> ComposeAssemblyVariants(const io::PuzzleData& puzzle) {
    if (!SupportsComposedAssembly(puzzle))
        throw std::invalid_argument("composed assembly needs a reagent path for every product atom");

    const auto& output = puzzle.outputs.front();
    std::vector<Cell> targets;
    std::vector<core::AtomType> types;
    targets.reserve(output.atoms.size());
    types.reserve(output.atoms.size());
    bool allDirect = true;
    for (const auto& atom : output.atoms) {
        targets.push_back(Cell{atom.position.q, atom.position.r});
        types.push_back(atom.type);
        if (SingleAtomSourceIndex(puzzle, atom.type) < 0) allDirect = false;
    }

    std::vector<io::SolutionData> variants;
    // A no-arm machine is emitted when the product cannot be assembled from
    // same-type reagents, and for large bonded products (lattices): the per-atom arm
    // search is bounded, so a >6-atom frame has no arm layout at all and the in-place
    // stations are the only competitive shape. Small direct products stay on the arm
    // machine, where an in-place input area stacked on the output frame would be a
    // degenerate machine.
    const bool largeDirect = allDirect && targets.size() > kMaxComposedAtoms && !output.bonds.empty();
    if (ComposedAssemblyUsesInPlaceLayout(puzzle) &&
        (ComposedAssemblyRequiresChemistry(puzzle) || largeDirect)) {
        try {
            if (auto staticMachine = BuildStatic(puzzle, targets, types))
                variants.push_back(std::move(*staticMachine));
        } catch (const std::invalid_argument&) {
            // Non-adjacent bonds never reach a static layout; fall back to the arms.
        }
    }
    if (allDirect) {
        if (auto ring = BuildRingGroup(puzzle, targets)) variants.push_back(std::move(*ring));
    }

    Placement placement(puzzle, targets, types);
    if (placement.Solve()) {
        io::SolutionData machine;
        machine.puzzleName = puzzle.name;
        machine.solutionName = "Composed assembly";
        const auto& arms = placement.Arms();
        for (const auto& arm : arms) EmitStation(machine, arm.station);
        for (const auto& arm : arms)
            PushObject(machine, "arm1", arm.swing.pivot,
                       {arm.swing.length, arm.swing.grab, 0, 0, 0},
                       StationTape(arm.swing, arm.station.Intro()));
        PushObject(machine, "out-std", Cell{0, 0}, {1, 0, 0, 0, 0});
        AppendBonders(machine, output);
        variants.push_back(std::move(machine));
    }
    if (variants.empty())
        throw std::invalid_argument("composed assembly found no collision-free arm placement");
    // Object-graph validity: every variant must be a self-contained machine. Rejecting
    // here keeps a malformed layout from reaching the gate or the archive (an invalid
    // graph used to crash a widened run instead of falling back to the families).
    for (auto& variant : variants) {
        bool hasOutput = false;
        std::set<std::pair<int, int>> mechanisms;
        for (const auto& object : variant.objects) {
            if (object.typeName == "out-std") hasOutput = true;
            if (object.typeName == "input") {
                const int index = object.bodyData.size() > 2 ? object.bodyData[2] : -1;
                if (index < 0 || static_cast<std::size_t>(index) >= puzzle.inputs.size())
                    throw std::invalid_argument("composed assembly produced an invalid source index");
                continue;
            }
            if (object.typeName == "bonder" || object.typeName == "arm1" ||
                object.typeName == "out-std")
                continue;
            // Input areas intentionally share their glyph's cells; different glyphs must
            // stay distinct so the machine is a real object graph.
            if (!mechanisms.insert({object.position.q, object.position.r}).second)
                throw std::invalid_argument("composed assembly produced overlapping glyphs");
        }
        if (!hasOutput || variant.objects.size() < 2)
            throw std::invalid_argument("composed assembly produced an incomplete machine");
    }
    return variants;
}

io::SolutionData ComposeAssemblyMachine(const io::PuzzleData& puzzle) {
    auto variants = ComposeAssemblyVariants(puzzle);
    return std::move(variants.front());
}

} // namespace opusoptima::solvers
