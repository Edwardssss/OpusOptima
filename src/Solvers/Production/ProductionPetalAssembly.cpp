#include "ProductionPetalAssembly.hpp"
#include "HexFrame.hpp"
#include "ProductionLayout.hpp"

#include <algorithm>
#include <chrono>
#include <map>
#include <set>

namespace opusoptima::solvers {
namespace {
using Coord = core::HexCoord;
using Cell = std::pair<int, int>;
using Edge = std::pair<Cell, Cell>;
using Type = core::AtomType;
Cell Key(Coord cell) {
    return {cell.q, cell.r};
}
Coord Turn(Coord cell, int rotation) {
    while (rotation-- > 0)
        cell = core::RotateClockwise(cell);
    return cell;
}
bool Bounded(Coord cell) {
    return cell.q >= -120 && cell.q <= 120 && cell.r >= -120 && cell.r <= 120;
}
Edge Canonical(Coord first, Coord second) {
    auto a = Key(first), b = Key(second);
    if (b < a) std::swap(a, b);
    return {a, b};
}
std::optional<core::HexFrame> Match(const io::Molecule& molecule, const io::Molecule& target) {
    if (molecule.atoms.size() != target.atoms.size() ||
        molecule.bonds.size() != target.bonds.size())
        return {};
    std::map<Cell, Type> expectedAtoms;
    std::map<Edge, unsigned> expectedBonds;
    for (const auto& atom : target.atoms)
        expectedAtoms.emplace(Key(atom.position), atom.type);
    for (const auto& bond : target.bonds)
        expectedBonds.emplace(Canonical(bond.from, bond.to), bond.bits);
    for (const auto& atom : molecule.atoms)
        if (!Bounded(atom.position)) return {};
    for (const auto& bond : molecule.bonds)
        if (!Bounded(bond.from) || !Bounded(bond.to)) return {};
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
            if (atoms.size() == molecule.atoms.size() && bonds.size() == molecule.bonds.size() &&
                atoms == expectedAtoms && bonds == expectedBonds)
                return frame;
        }
    return {};
}
std::optional<core::HexFrame> PipeFrame(const std::vector<Coord>& path,
                                        const std::vector<Coord>& target) {
    if (path.size() != target.size() || path.empty()) return {};
    for (auto cell : path)
        if (!Bounded(cell)) return {};
    for (int rotation = 0; rotation < 6; ++rotation) {
        const core::HexFrame frame{core::Subtract(target.front(), Turn(path.front(), rotation)),
                                   rotation};
        bool matches = true;
        for (std::size_t index = 0; index < path.size(); ++index)
            matches &= frame.ToWorld(path[index]) == target[index];
        if (matches) return frame;
    }
    return {};
}
io::Molecule InputShape() {
    return {{{Type::Water, {0, 0}}, {Type::Salt, {0, -1}}, {Type::Salt, {1, 0}}},
            {{1, {0, 0}, {0, -1}}, {1, {0, 0}, {1, 0}}}};
}
io::Molecule OutputShape() {
    const Coord center{0, 1};
    io::Molecule output{{{Type::Earth, center}}, {}};
    for (int rotation : {0, 2, 4}) {
        const auto water = core::Add(center, Turn({1, 0}, rotation));
        const auto vitae = core::Add(center, Turn({0, 1}, rotation));
        output.atoms.push_back({Type::Water, water});
        output.atoms.push_back({Type::Vitae, vitae});
        output.bonds.push_back({1, center, water});
        output.bonds.push_back({1, water, vitae});
    }
    return output;
}
} // namespace

std::optional<io::SolutionData> CompileAcceptedProductionPetalAssembly(
    const io::PuzzleData& puzzle, const std::function<bool(const io::SolutionData&)>& accept) {
    constexpr auto permissions = (1ULL << 0) | (1ULL << 1) | (1ULL << 2) | (1ULL << 3) |
                                 (1ULL << 8) | (1ULL << 9) | (1ULL << 13) | (1ULL << 16) |
                                 (1ULL << 17) | (1ULL << 22) | (1ULL << 23) | (1ULL << 25) |
                                 (1ULL << 28);
    if (puzzle.version != 3 || !puzzle.productionInfo || puzzle.productionInfo->isolation ||
        puzzle.outputMultiplier < 1 || puzzle.outputMultiplier > 2 || puzzle.inputs.size() != 1 ||
        puzzle.outputs.size() != 1 || (puzzle.permissionBits & permissions) != permissions)
        return {};
    const auto& info = *puzzle.productionInfo;
    if (info.chambers.size() != 3 || info.conduits.size() != 2) return {};
    const auto inputFrame = Match(puzzle.inputs[0], InputShape());
    const auto outputShape = OutputShape();
    const auto outputFrame = Match(puzzle.outputs[0], outputShape);
    if (!inputFrame || !outputFrame) return {};
    const auto chambers = PlanProductionChambers(puzzle);
    std::optional<std::size_t> large;
    for (std::size_t room = 0; room < chambers.size(); ++room) {
        if (info.chambers[room].chamberType == "Large") {
            if (large) return {};
            large = room;
        } else if (info.chambers[room].chamberType != "SmallWider")
            return {};
    }
    if (!large) return {};
    const auto roomOf = [&](Coord cell) -> std::optional<std::size_t> {
        if (!Bounded(cell)) return {};
        for (std::size_t room = 0; room < chambers.size(); ++room)
            if (ContainsProductionCell(chambers[room], cell)) return room;
        return {};
    };
    std::optional<std::size_t> pairPipe, earthPipe, pairRoom, earthRoom;
    for (std::size_t index = 0; index < info.conduits.size(); ++index) {
        const auto& pipe = info.conduits[index];
        const auto a = roomOf(pipe.positionA), b = roomOf(pipe.positionB);
        if (!a || !b || a == b || (*a != *large && *b != *large)) return {};
        const auto source = *a == *large ? *b : *a;
        for (auto cell : pipe.path)
            if (!Bounded(cell) || roomOf(core::Add(pipe.positionA, cell)) != a ||
                roomOf(core::Add(pipe.positionB, cell)) != b)
                return {};
        if (pipe.path.size() == 2 && core::Distance(pipe.path[0], pipe.path[1]) == 1 && !pairPipe) {
            pairPipe = index;
            pairRoom = source;
        } else if (pipe.path.size() == 1 && !earthPipe) {
            earthPipe = index;
            earthRoom = source;
        } else
            return {};
    }
    if (!pairRoom || !earthRoom || pairRoom == earthRoom) return {};
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    for (int rotation = 0; rotation < 6; ++rotation)
        for (int petalTurn : {0, 3})
            for (int earthTurn : {0, 3}) {
                if (std::chrono::steady_clock::now() >= deadline) return {};
                const core::HexFrame central{chambers[*large].center, rotation};
                const auto smallFrame = [&](std::size_t room, int turns) {
                    return core::HexFrame{
                        core::Add(chambers[room].center, turns ? Coord{2, 0} : Coord{}), turns};
                };
                const auto petal = smallFrame(*pairRoom, petalTurn),
                           earth = smallFrame(*earthRoom, earthTurn);
                io::SolutionData candidate;
                candidate.puzzleName = puzzle.name;
                candidate.solutionName = "Conduit petal reaction assembly";
                const auto object = [&](const char* name, core::HexFrame room, Coord local,
                                        int angle = 0, int index = 0) {
                    candidate.objects.push_back({name,
                                                 room.ToWorld(local),
                                                 {},
                                                 {1, (room.Turns() + angle) % 6, index, 0, 0}});
                    return candidate.objects.size() - 1;
                };
                const auto input = [&](core::HexFrame room, Coord water) {
                    auto frame = *inputFrame;
                    frame.origin = core::Add(frame.origin, water);
                    frame = room.Compose(frame);
                    candidate.objects.push_back(
                        {"input", frame.origin, {}, {1, frame.Turns(), 0, 0, 0}});
                };
                const auto arm = [&](const char* name, core::HexFrame room, Coord local, int length,
                                     int angle, int number,
                                     std::initializer_list<std::pair<char, int>> tape) {
                    const auto index = object(name, room, local, angle);
                    candidate.objects[index].bodyData = {length, (room.Turns() + angle) % 6, 0, 40,
                                                         number};
                    for (const auto& [action, cycle] : tape)
                        candidate.objects[index].instructions.push_back({action, cycle});
                };
                const auto addPipe = [&](std::size_t index, std::size_t room,
                                         const std::vector<Coord>& cells) {
                    const auto& shape = info.conduits[index].path;
                    const auto frame = PipeFrame(shape, cells);
                    if (!frame || !ContainsProductionCell(chambers[room], frame->origin))
                        return false;
                    std::vector<int> body{1,
                                          frame->Turns(),
                                          0,
                                          0,
                                          0,
                                          100 + static_cast<int>(index),
                                          static_cast<int>(shape.size())};
                    for (auto cell : shape) {
                        body.push_back(cell.q);
                        body.push_back(cell.r);
                    }
                    candidate.objects.push_back({"pipe", frame->origin, {}, std::move(body)});
                    return true;
                };
                input(petal, {});
                object("unbonder", petal, {}, 4);
                object("unbonder", petal, {}, 0);
                object("glyph-life-and-death", petal, {2, -1}, 2);
                object("glyph-disposal", petal, {2, 0});
                object("bonder", petal, {}, 5);
                if (!addPipe(*pairPipe, *pairRoom, {petal.ToWorld({}), petal.ToWorld({1, -1})}) ||
                    !addPipe(*pairPipe, *large,
                             {central.ToWorld({0, -2}), central.ToWorld({1, -2})}))
                    continue;
                arm("arm1", petal, {0, 1}, 2, 4, 0,
                    {{'O', 0},
                     {'G', 1},
                     {'r', 2},
                     {'g', 3},
                     {'R', 4},
                     {'G', 9},
                     {'r', 10},
                     {'g', 11},
                     {'R', 12},
                     {'G', 17},
                     {'r', 18},
                     {'g', 19},
                     {'R', 20},
                     {'O', 39}});
                arm("arm1", petal, {-1, 0}, 1, 0, 1,
                    {{'O', 0},
                     {'G', 5},
                     {'g', 6},
                     {'G', 13},
                     {'g', 14},
                     {'G', 21},
                     {'g', 22},
                     {'O', 39}});
                const auto wheel = object("baron", earth, {});
                candidate.objects[wheel].bodyData[4] = 5;
                input(earth, {2, 0});
                object("unbonder", earth, {2, 0}, 4);
                object("unbonder", earth, {2, 0}, 0);
                object("glyph-disposal", earth, {2, 0});
                object("glyph-disposal", earth, {3, 0});
                object("glyph-duplication", earth, {1, -1});
                if (!addPipe(*earthPipe, *earthRoom, {earth.ToWorld({2, -1})}) ||
                    !addPipe(*earthPipe, *large, {central.ToWorld({0, 1})}))
                    continue;
                arm("arm1", earth, {3, -1}, 1, 3, 2, {{'O', 0}, {'G', 1}, {'g', 24}, {'O', 39}});
                arm("arm3", central, {0, -1}, 1, 0, 3,
                    {{'O', 0},
                     {'G', 7},
                     {'r', 8},
                     {'r', 9},
                     {'g', 10},
                     {'G', 15},
                     {'r', 16},
                     {'r', 17},
                     {'A', 18},
                     {'A', 19},
                     {'g', 20},
                     {'a', 21},
                     {'a', 22},
                     {'a', 23},
                     {'A', 30},
                     {'r', 31},
                     {'r', 32},
                     {'O', 39}});
                arm("piston", central, {0, -3}, 1, 1, 4,
                    {{'O', 0},
                     {'G', 24},
                     {'E', 25},
                     {'E', 26},
                     {'g', 27},
                     {'e', 28},
                     {'e', 29},
                     {'O', 39}});
                const auto track = object("track", central, {-1, -1});
                candidate.objects[track].bodyData = {
                    1, central.Turns(), 0, 0, 4, 0, 0, 1, 0, 1, 1, 1, 2, 0};
                for (const auto& bond : outputShape.bonds) {
                    int angle = 0;
                    while (angle < 6 && core::Add(bond.from, Turn({1, 0}, angle)) != bond.to)
                        ++angle;
                    object("bonder", central, bond.from, angle);
                }
                const auto frame = central.Compose(*outputFrame);
                candidate.objects.push_back(
                    {"out-std", frame.origin, {}, {1, frame.Turns(), 0, 0, 0}});
                if (accept(candidate)) return candidate;
            }
    return {};
}
} // namespace opusoptima::solvers
