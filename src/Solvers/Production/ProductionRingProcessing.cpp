#include "ProductionRingProcessing.hpp"
#include "HexFrame.hpp"
#include "ProductionLayout.hpp"

#include <algorithm>
#include <array>
#include <map>
#include <set>
#include <tuple>

namespace opusoptima::solvers {
namespace {
using Coord = core::HexCoord;
using Cell = std::pair<int, int>;
using Edge = std::pair<Cell, Cell>;
using Type = core::AtomType;
Cell Key(Coord cell) {
    return {cell.q, cell.r};
}
Edge Canonical(Coord a, Coord b) {
    auto first = Key(a), second = Key(b);
    if (second < first) std::swap(first, second);
    return {first, second};
}
Coord Turn(Coord cell, int count) {
    while (count-- > 0)
        cell = core::RotateClockwise(cell);
    return cell;
}
bool Bounded(Coord cell) {
    return cell.q >= -120 && cell.q <= 120 && cell.r >= -120 && cell.r <= 120;
}
std::optional<core::HexFrame> RingFrame(const io::Molecule& input, Coord destination) {
    if (input.atoms.size() != 6 || input.bonds.size() != 6) return {};
    const auto element = input.atoms.front().type;
    if (element < Type::Air || element > Type::Water) return {};
    std::set<Cell> cells;
    for (const auto& atom : input.atoms)
        if (atom.type != element || !Bounded(atom.position) ||
            !cells.insert(Key(atom.position)).second)
            return {};
    std::set<Edge> edges;
    for (const auto& bond : input.bonds)
        if (bond.bits != 1 || !Bounded(bond.from) || !Bounded(bond.to) ||
            core::Distance(bond.from, bond.to) != 1 || !cells.contains(Key(bond.from)) ||
            !cells.contains(Key(bond.to)) || !edges.insert(Canonical(bond.from, bond.to)).second)
            return {};
    for (int direction = 0; direction < 6; ++direction) {
        const auto center = core::Add(input.atoms.front().position, Turn({1, 0}, direction));
        std::set<Edge> expected;
        bool valid = true;
        for (int side = 0; side < 6; ++side) {
            const auto a = core::Add(center, Turn({1, 0}, side));
            const auto b = core::Add(center, Turn({1, 0}, (side + 1) % 6));
            valid &= cells.contains(Key(a));
            expected.insert(Canonical(a, b));
        }
        if (valid && expected == edges)
            return core::HexFrame{core::Subtract(destination, center), 0};
    }
    return {};
}
std::optional<core::HexFrame> Match(const io::Molecule& molecule, const io::Molecule& target) {
    if (molecule.atoms.size() != target.atoms.size() ||
        molecule.bonds.size() != target.bonds.size())
        return {};
    for (const auto& atom : molecule.atoms)
        if (!Bounded(atom.position)) return {};
    for (const auto& bond : molecule.bonds)
        if (!Bounded(bond.from) || !Bounded(bond.to)) return {};
    std::map<Cell, Type> expectedAtoms;
    std::map<Edge, unsigned> expectedBonds;
    for (const auto& atom : target.atoms)
        expectedAtoms.emplace(Key(atom.position), atom.type);
    for (const auto& bond : target.bonds)
        expectedBonds.emplace(Canonical(bond.from, bond.to), bond.bits);
    for (int rotation = 0; rotation < 6; ++rotation)
        for (const auto& anchor : target.atoms) {
            if (anchor.type != molecule.atoms.front().type) continue;
            const core::HexFrame frame{
                core::Subtract(anchor.position, Turn(molecule.atoms.front().position, rotation)),
                rotation};
            std::map<Cell, Type> atoms;
            std::map<Edge, unsigned> bonds;
            for (const auto& atom : molecule.atoms)
                atoms.emplace(Key(frame.ToWorld(atom.position)), atom.type);
            for (const auto& bond : molecule.bonds)
                bonds.emplace(Canonical(frame.ToWorld(bond.from), frame.ToWorld(bond.to)),
                              bond.bits);
            if (atoms == expectedAtoms && bonds == expectedBonds) return frame;
        }
    return {};
}
std::optional<core::HexFrame> PipeFrame(const std::vector<Coord>& path, Coord first, Coord second) {
    if (path.size() != 2 || !Bounded(path[0]) || !Bounded(path[1]) ||
        core::Distance(path[0], path[1]) != 1)
        return {};
    for (int rotation = 0; rotation < 6; ++rotation) {
        const core::HexFrame frame{core::Subtract(first, Turn(path[0], rotation)), rotation};
        if (frame.ToWorld(path[1]) == second) return frame;
    }
    return {};
}
void AddPipe(io::SolutionData& machine, const std::vector<Coord>& path, core::HexFrame frame) {
    std::vector<int> body{1, frame.Turns(), 0, 0, 0, 100, static_cast<int>(path.size())};
    for (auto cell : path) {
        body.push_back(cell.q);
        body.push_back(cell.r);
    }
    machine.objects.push_back({"pipe", frame.origin, {}, std::move(body)});
}
} // namespace

std::optional<io::SolutionData> CompileAcceptedProductionRingProcessing(
    const io::PuzzleData& puzzle, const std::function<bool(const io::SolutionData&)>& accept) {
    constexpr std::uint64_t permissions = (1ULL << 1) | (1ULL << 2) | (1ULL << 3) | (1ULL << 8) |
                                          (1ULL << 9) | (1ULL << 11) | (1ULL << 12) | (1ULL << 13) |
                                          (1ULL << 22) | (1ULL << 23) | (1ULL << 25) | (1ULL << 28);
    if (puzzle.version != 3 || !puzzle.productionInfo || puzzle.productionInfo->isolation ||
        puzzle.outputMultiplier < 1 || puzzle.outputMultiplier > 8 || puzzle.inputs.size() != 1 ||
        puzzle.outputs.size() != 2 || (puzzle.permissionBits & permissions) != permissions)
        return {};
    const auto& info = *puzzle.productionInfo;
    if (info.chambers.empty() || info.chambers.size() > 2 || info.vials.size() > 64 ||
        std::any_of(info.vials.begin(), info.vials.end(),
                    [](const auto& vial) { return vial.count < 0 || vial.count > 64; }))
        return {};
    const auto chambers = PlanProductionChambers(puzzle);
    std::optional<std::size_t> active;
    for (std::size_t index = 0; index < info.chambers.size(); ++index)
        if (info.chambers[index].chamberType == "Large") {
            if (active) return {};
            active = index;
        }
    if (!active) return {};
    if (chambers.size() == 1 && !info.conduits.empty()) return {};
    if (chambers.size() == 2) {
        if (info.conduits.size() != 1) return {};
        const auto& conduit = info.conduits[0];
        if (conduit.path.size() != 2 || !Bounded(conduit.path[0]) || !Bounded(conduit.path[1]) ||
            core::Distance(conduit.path[0], conduit.path[1]) != 1)
            return {};
        const auto room = [&](Coord cell) {
            for (std::size_t index = 0; index < chambers.size(); ++index)
                if (ContainsProductionCell(chambers[index], cell)) return index;
            return chambers.size();
        };
        const auto a = room(conduit.positionA), b = room(conduit.positionB);
        if (a >= chambers.size() || b >= chambers.size() || a == b) return {};
    }
    const auto inputFrame = RingFrame(puzzle.inputs[0], {-1, 1});
    if (!inputFrame) return {};
    const io::Molecule fire{{{Type::Salt, {-3, 3}}, {Type::Fire, {-3, 2}}, {Type::Fire, {-2, 2}}},
                            {{1, {-3, 3}, {-3, 2}}, {14, {-3, 2}, {-2, 2}}}};
    const io::Molecule water{{{Type::Water, {-1, 0}}, {Type::Water, {-2, 1}}, {Type::Salt, {0, 0}}},
                             {{1, {-1, 0}, {-2, 1}}, {1, {-1, 0}, {0, 0}}}};
    std::optional<std::size_t> firePort, waterPort;
    std::array<core::HexFrame, 2> outputFrames;
    for (std::size_t port = 0; port < puzzle.outputs.size(); ++port) {
        if (const auto frame = Match(puzzle.outputs[port], fire)) {
            if (firePort) return {};
            firePort = port;
            outputFrames[port] = *frame;
        } else if (const auto frame = Match(puzzle.outputs[port], water)) {
            if (waterPort) return {};
            waterPort = port;
            outputFrames[port] = *frame;
        } else
            return {};
    }
    if (!firePort || !waterPort) return {};
    for (int rotation = 0; rotation < 6; ++rotation) {
        const core::HexFrame placement{chambers[*active].center, rotation};
        const auto world = [&](Coord cell) { return placement.ToWorld(cell); };
        io::SolutionData machine;
        machine.puzzleName = puzzle.name;
        machine.solutionName = "Production ring processing";
        const auto add = [&](const char* name, Coord position, int direction = 0, int index = 0) {
            machine.objects.push_back(
                {name, world(position), {}, {1, (direction + rotation) % 6, index, 0, 0}});
            return machine.objects.size() - 1;
        };
        auto composedInput = *inputFrame;
        composedInput.origin = world(composedInput.origin);
        composedInput.rotation = (composedInput.Turns() + rotation) % 6;
        machine.objects.push_back(
            {"input", composedInput.origin, {}, {1, composedInput.Turns(), 0, 0, 0}});
        std::array<Coord, 6> ring;
        for (int side = 0; side < 6; ++side)
            ring[side] = core::Add({-1, 1}, Turn({1, 0}, side));
        for (int side = 0; side < 6; ++side) {
            const auto a = ring[side], b = ring[(side + 1) % 6];
            int direction = 0;
            while (core::Add(a, Turn({1, 0}, direction)) != b)
                ++direction;
            add("unbonder", a, direction);
        }
        add("glyph-calcification", ring[0]);
        add("glyph-duplication", {1, 0}, 3);
        add("bonder", ring[3], 5);
        add("bonder", ring[4], 0);
        bool prismsFit = true;
        for (int color : {2, 4, 8}) {
            bool found = false;
            for (int angle = 0; angle < 6 && !found; ++angle)
                for (Coord local : {Coord{0, 0}, Coord{0, 1}, Coord{1, 0}}) {
                    const auto origin = core::Subtract(ring[2], Turn(local, angle));
                    const std::array<Coord, 3> vertices{origin,
                                                        core::Add(origin, Turn({0, 1}, angle)),
                                                        core::Add(origin, Turn({1, 0}, angle))};
                    if (!std::all_of(vertices.begin(), vertices.end(), [&](Coord cell) {
                            return ContainsProductionCell(chambers[*active], world(cell));
                        }))
                        continue;
                    const std::array<int, 3> colors{8, 2, 4};
                    for (int edge = 0; edge < 3; ++edge)
                        if (colors[edge] == color &&
                            Canonical(vertices[edge], vertices[(edge + 1) % 3]) ==
                                Canonical(ring[1], ring[2])) {
                            add("bonder-prisma", origin, angle);
                            found = true;
                            break;
                        }
                    if (found) break;
                }
            prismsFit &= found;
        }
        if (!prismsFit) continue;
        add("bonder", {-2, 3}, 4);
        for (std::size_t port = 0; port < outputFrames.size(); ++port) {
            const auto& frame = outputFrames[port];
            machine.objects.push_back(
                {"out-std",
                 world(frame.origin),
                 {},
                 {1, (frame.Turns() + rotation) % 6, static_cast<int>(port), 0, 0}});
        }
        const auto main = add("arm6", {-1, 1});
        const auto wheel = add("baron", {2, 0});
        const auto pull = add("piston", {0, 2}, 4);
        const auto carry = add("piston", {-2, -1});
        machine.objects[carry].bodyData[0] = 2;
        for (std::size_t index = 0; index < 4; ++index)
            machine.objects[std::array<std::size_t, 4>{main, wheel, pull, carry}[index]]
                .bodyData[4] = index;
        auto& track = machine.objects.emplace_back(
            io::SolutionObject{"track", world({-2, -1}), {}, {1, rotation, 0, 0, 4}});
        for (Coord cell : {Coord{0, 0}, Coord{-1, 1}, Coord{-1, 2}, Coord{0, 2}}) {
            track.bodyData.push_back(cell.q);
            track.bodyData.push_back(cell.r);
        }
        track.bodyData.push_back(0);
        const auto tape = [&](std::size_t part,
                              std::initializer_list<std::pair<char, int>> instructions) {
            for (auto [action, cycle] : instructions)
                machine.objects[part].instructions.push_back({action, cycle});
            machine.objects[part].bodyData[3] = 32;
        };
        // The leading temporary Fire returns through calcification; wheel motion must not coincide
        // with ring motion.
        tape(main, {{'G', 0},
                    {'R', 1},
                    {'R', 3},
                    {'R', 4},
                    {'R', 8},
                    {'R', 9},
                    {'R', 11},
                    {'g', 12},
                    {'O', 31}});
        tape(wheel, {{'O', 0},
                     {'R', 2},
                     {'r', 5},
                     {'r', 6},
                     {'r', 7},
                     {'r', 10},
                     {'r', 27},
                     {'r', 28},
                     {'r', 29},
                     {'O', 31}});
        tape(pull, {{'O', 0},
                    {'G', 13},
                    {'E', 14},
                    {'E', 15},
                    {'g', 17},
                    {'e', 18},
                    {'e', 19},
                    {'O', 31}});
        tape(carry, {{'O', 0},
                     {'G', 16},
                     {'e', 19},
                     {'r', 20},
                     {'A', 21},
                     {'E', 22},
                     {'A', 23},
                     {'A', 24},
                     {'a', 25},
                     {'g', 26},
                     {'a', 27},
                     {'a', 28},
                     {'e', 29},
                     {'R', 30},
                     {'E', 31}});
        if (chambers.size() == 2) {
            const auto& path = info.conduits[0].path;
            const auto source = PipeFrame(path, world({-3, 0}), world({-2, 0}));
            if (!source || !ContainsProductionCell(chambers[*active], source->origin)) continue;
            const auto other = 1 - *active;
            std::optional<core::HexFrame> destination;
            for (auto origin : chambers[other].interior) {
                if (destination) break;
                for (int angle = 0; angle < 6; ++angle) {
                    const core::HexFrame frame{origin, angle};
                    if (std::all_of(path.begin(), path.end(), [&](Coord cell) {
                            return ContainsProductionCell(chambers[other], frame.ToWorld(cell));
                        })) {
                        destination = frame;
                        break;
                    }
                }
            }
            if (!destination) continue;
            AddPipe(machine, path, *source);
            AddPipe(machine, path, *destination);
        }
        if (accept(machine)) return machine;
    }
    return {};
}
} // namespace opusoptima::solvers
