#include "ProductionAssembly.hpp"
#include "HexFrame.hpp"
#include "ProductionLayout.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <map>
#include <queue>
#include <set>
#include <tuple>

namespace opusoptima::solvers {
namespace {
using Coord = core::HexCoord;
using Cell = std::pair<int, int>;
Cell Key(Coord cell) {
    return {cell.q, cell.r};
}
Coord Turn(Coord cell, int turns) {
    return core::HexFrame{{}, turns}.ToWorld(cell);
}
struct Pose {
    int length{1}, direction{};
    int Index() const {
        return (length - 1) * 6 + direction;
    }
    friend bool operator==(const Pose&, const Pose&) = default;
};
Coord Tip(Coord base, Pose pose) {
    return core::Add(base, Turn({pose.length, 0}, pose.direction));
}
struct Point {
    double x{}, y{};
};
Point XY(Coord cell) {
    return {cell.q + cell.r * .5, cell.r * std::sqrt(3.) * .5};
}

class AssemblyMotion {
    public:
    Coord base{}, feed{};
    ProductionChamberLayout chamber;
    std::vector<Coord> targets;
    std::vector<std::pair<Coord, Coord>> bonds;
    int maxLength{3}, radius{}, stretch{};

    bool Inside(Point point) const {
        const auto center = XY(chamber.center);
        const double r = (point.y - center.y) * 2 / std::sqrt(3.);
        const double q = point.x - center.x - r * .5;
        for (int shift = 0; shift <= stretch; ++shift)
            if (std::max({std::abs(q - shift), std::abs(r), std::abs(q - shift + r)}) <=
                radius + .001)
                return true;
        return false;
    }
    bool Interval(Pose from, Pose to, char action, unsigned occupied, bool holding,
                  Coord goal) const {
        if (!ContainsProductionCell(chamber, Tip(base, to))) return false;
        const auto origin = XY(base), start = XY(Tip(base, from)), finish = XY(Tip(base, to));
        const bool rotate = action == 'r' || action == 'R';
        const double turn = (action == 'r' ? 1 : -1) * std::acos(-1.) / 3.;
        for (int sample = 0; sample <= 64; ++sample) {
            const double time = sample / 64.;
            Point point;
            if (rotate) {
                const double x = start.x - origin.x, y = start.y - origin.y;
                point = {origin.x + x * std::cos(turn * time) - y * std::sin(turn * time),
                         origin.y + x * std::sin(turn * time) + y * std::cos(turn * time)};
            } else
                point = {start.x + (finish.x - start.x) * time,
                         start.y + (finish.y - start.y) * time};
            if (!Inside(point)) return false;
            if (!holding) continue;
            const auto collision = [&](Coord cell) {
                const auto obstacle = XY(cell);
                const double x = point.x - obstacle.x, y = point.y - obstacle.y;
                return x * x + y * y < .36;
            };
            if (Tip(base, from) != feed && collision(feed)) return false;
            for (std::size_t atom = 0; atom < targets.size(); ++atom)
                if ((occupied & (1U << atom)) && collision(targets[atom])) return false;
        }
        if (!holding || Tip(base, to) == goal) return true;
        const auto cell = Tip(base, to);
        for (const auto& [a, b] : bonds) {
            Coord other;
            if (a == cell)
                other = b;
            else if (b == cell)
                other = a;
            else
                continue;
            for (std::size_t atom = 0; atom < targets.size(); ++atom)
                if ((occupied & (1U << atom)) && targets[atom] == other) return false;
        }
        return true;
    }
    std::optional<std::vector<char>> Route(Pose start, Pose destination, unsigned occupied,
                                           bool holding, Coord goal) const {
        struct Parent {
            int previous{-1};
            char action{};
        };
        std::array<Parent, 18> parent;
        std::queue<Pose> queue;
        parent[start.Index()].previous = start.Index();
        queue.push(start);
        while (!queue.empty()) {
            const auto from = queue.front();
            queue.pop();
            if (from == destination) {
                std::vector<char> route;
                int index = from.Index();
                while (index != start.Index()) {
                    route.push_back(parent[index].action);
                    index = parent[index].previous;
                }
                std::reverse(route.begin(), route.end());
                return route;
            }
            const std::array<std::pair<Pose, char>, 4> next{
                {{{from.length, (from.direction + 1) % 6}, 'r'},
                 {{from.length, (from.direction + 5) % 6}, 'R'},
                 {{from.length + 1, from.direction}, 'E'},
                 {{from.length - 1, from.direction}, 'e'}}};
            for (const auto& [to, action] : next) {
                if (to.length < 1 || to.length > maxLength || parent[to.Index()].previous >= 0)
                    continue;
                if (!Interval(from, to, action, occupied, holding, goal)) continue;
                parent[to.Index()] = {from.Index(), action};
                queue.push(to);
            }
        }
        return {};
    }
    std::optional<std::vector<char>> Period(Pose source) const {
        std::set<unsigned> failed;
        std::vector<char> tape;
        const unsigned complete = (1U << targets.size()) - 1;
        const auto visit = [&](auto&& self, unsigned occupied) -> bool {
            if (occupied == complete) return true;
            if (failed.contains(occupied)) return false;
            for (std::size_t target = 0; target < targets.size(); ++target) {
                if (occupied & (1U << target)) continue;
                for (int length = 1; length <= maxLength; ++length)
                    for (int direction = 0; direction < 6; ++direction) {
                        const Pose destination{length, direction};
                        if (Tip(base, destination) != targets[target]) continue;
                        const auto delivery =
                            Route(source, destination, occupied, true, targets[target]);
                        if (!delivery) continue;
                        const auto next = occupied | (1U << target);
                        const auto reset =
                            Route(destination, source, next == complete ? 0 : next, false, {});
                        if (!reset) continue;
                        const auto size = tape.size();
                        tape.push_back('G');
                        tape.insert(tape.end(), delivery->begin(), delivery->end());
                        tape.push_back('g');
                        tape.insert(tape.end(), reset->begin(), reset->end());
                        if (self(self, next)) return true;
                        tape.resize(size);
                    }
            }
            failed.insert(occupied);
            return false;
        };
        if (!visit(visit, 0)) return {};
        return tape;
    }
};

bool AddUnusedPipes(const io::PuzzleData& puzzle,
                    const std::vector<ProductionChamberLayout>& chambers,
                    const std::set<Cell>& machineCells, io::SolutionData& machine) {
    std::set<Cell> reserved = machineCells;
    const auto roomOf = [&](Coord cell) -> const ProductionChamberLayout* {
        for (const auto& chamber : chambers)
            if (ContainsProductionCell(chamber, cell)) return &chamber;
        return nullptr;
    };
    for (std::size_t index = 0; index < puzzle.productionInfo->conduits.size(); ++index) {
        const auto& pipe = puzzle.productionInfo->conduits[index];
        const auto* first = roomOf(pipe.positionA);
        const auto* second = roomOf(pipe.positionB);
        if (!first || !second || first == second || pipe.path.empty() || pipe.path.size() > 64)
            return false;
        std::set<Cell> unique;
        for (auto cell : pipe.path)
            if (cell.q < -128 || cell.q > 127 || cell.r < -128 || cell.r > 127 ||
                !unique.insert(Key(cell)).second)
                return false;
        for (const auto* room : {first, second}) {
            bool placed = false;
            for (auto origin : room->interior) {
                if (placed) break;
                for (int rotation = 0; rotation < 6; ++rotation) {
                    std::vector<Coord> footprint;
                    bool fits = true;
                    for (auto local : pipe.path) {
                        const auto cell = core::Add(origin, Turn(local, rotation));
                        fits &=
                            ContainsProductionCell(*room, cell) && !reserved.contains(Key(cell));
                        footprint.push_back(cell);
                    }
                    if (!fits) continue;
                    std::vector<int> body{1,
                                          rotation,
                                          0,
                                          0,
                                          0,
                                          100 + static_cast<int>(index),
                                          static_cast<int>(pipe.path.size())};
                    for (auto local : pipe.path) {
                        body.push_back(local.q);
                        body.push_back(local.r);
                    }
                    machine.objects.push_back({"pipe", origin, {}, std::move(body)});
                    for (auto cell : footprint)
                        reserved.insert(Key(cell));
                    placed = true;
                    break;
                }
            }
            if (!placed) return false;
        }
    }
    return true;
}
} // namespace

std::optional<io::SolutionData> CompileAcceptedProductionAssembly(
    const io::PuzzleData& puzzle, const std::function<bool(const io::SolutionData&)>& accept) {
    constexpr auto required = (1ULL << 2) | (1ULL << 8) | (1ULL << 22) | (1ULL << 23);
    if (puzzle.version != 3 || !puzzle.productionInfo || puzzle.productionInfo->isolation ||
        puzzle.outputMultiplier < 1 || puzzle.outputMultiplier > 8 || puzzle.outputs.size() != 1 ||
        (puzzle.permissionBits & required) != required)
        return {};
    const auto& output = puzzle.outputs[0];
    if (output.atoms.size() < 2 || output.atoms.size() > 10 || output.bonds.empty()) return {};
    const auto element = output.atoms[0].type;
    const auto bounded = [](Coord cell) {
        return cell.q >= -120 && cell.q <= 120 && cell.r >= -120 && cell.r <= 120;
    };
    if (!core::AtomTypeFromId(static_cast<std::uint8_t>(element))) return {};
    for (const auto& atom : output.atoms)
        if (atom.type != element || element == core::AtomType::Repeat || !bounded(atom.position))
            return {};
    for (const auto& bond : output.bonds)
        if (!bounded(bond.from) || !bounded(bond.to) || bond.bits != 1 ||
            core::Distance(bond.from, bond.to) != 1)
            return {};
    const auto input =
        std::find_if(puzzle.inputs.begin(), puzzle.inputs.end(), [&](const auto& molecule) {
            return molecule.atoms.size() == 1 && molecule.bonds.empty() &&
                   molecule.atoms[0].type == element && bounded(molecule.atoms[0].position);
        });
    if (input == puzzle.inputs.end()) return {};
    auto chambers = PlanProductionChambers(puzzle);
    for (auto& chamber : chambers)
        std::stable_sort(chamber.interior.begin(), chamber.interior.end(), [&](Coord a, Coord b) {
            return core::Distance(a, chamber.center) < core::Distance(b, chamber.center);
        });
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    unsigned attempts = 0;
    std::map<Cell, unsigned> baseAttempts;
    std::set<std::tuple<int, int, int, int, int>> tested;
    for (const auto& chamber : chambers)
        for (auto base : chamber.interior)
            for (int rotation = 0; rotation < 6; ++rotation)
                for (int length = 1; length <= 3; ++length)
                    for (int direction = 0; direction < 6; ++direction) {
                        if (attempts >= 64 || std::chrono::steady_clock::now() >= deadline)
                            return {};
                        if (baseAttempts[Key(base)] >= 4) continue;
                        const core::HexFrame frame{
                            core::Subtract(Tip(base, {length, direction}),
                                           Turn(output.atoms[0].position, rotation)),
                            rotation};
                        if (!tested
                                 .emplace(base.q, base.r, frame.origin.q, frame.origin.r, rotation)
                                 .second)
                            continue;
                        AssemblyMotion motion;
                        motion.base = base;
                        motion.chamber = chamber;
                        const auto& type =
                            puzzle.productionInfo->chambers[chamber.chamberIndex].chamberType;
                        motion.radius = type == "Large" ? 3 : type.starts_with("Medium") ? 2 : 1;
                        motion.stretch = type == "SmallWider"                          ? 2
                                         : type == "SmallWide" || type == "MediumWide" ? 1
                                                                                       : 0;
                        std::set<Cell> cells{Key(base)};
                        bool fits = true;
                        for (const auto& atom : output.atoms) {
                            const auto cell = frame.ToWorld(atom.position);
                            bool reachable = false;
                            for (int reach = 1; reach <= 3; ++reach)
                                for (int angle = 0; angle < 6; ++angle)
                                    reachable |= Tip(base, {reach, angle}) == cell;
                            fits &= reachable && ContainsProductionCell(chamber, cell) &&
                                    cells.insert(Key(cell)).second;
                            motion.targets.push_back(cell);
                        }
                        if (!fits) continue;
                        for (const auto& bond : output.bonds)
                            motion.bonds.push_back(
                                {frame.ToWorld(bond.from), frame.ToWorld(bond.to)});
                        for (int reach = 1; reach <= 3; ++reach)
                            for (int angle = 0; angle < 6; ++angle) {
                                if (attempts >= 64 || std::chrono::steady_clock::now() >= deadline)
                                    return {};
                                motion.feed = Tip(base, {reach, angle});
                                if (!ContainsProductionCell(chamber, motion.feed) ||
                                    cells.contains(Key(motion.feed)))
                                    continue;
                                const Pose source{reach, angle};
                                const auto tape = motion.Period(source);
                                if (!tape) continue;
                                io::SolutionData candidate;
                                candidate.puzzleName = puzzle.name;
                                candidate.solutionName = "Room-local piston assembly";
                                io::SolutionObject arm{
                                    "piston",
                                    base,
                                    {},
                                    {reach, angle, 0, static_cast<int>(tape->size()), 0}};
                                int cycle = 0;
                                for (char action : *tape)
                                    arm.instructions.push_back({action, cycle++});
                                candidate.objects.push_back(std::move(arm));
                                candidate.objects.push_back(
                                    {"input",
                                     core::Subtract(motion.feed, input->atoms[0].position),
                                     {},
                                     {1, 0, static_cast<int>(input - puzzle.inputs.begin()), 0,
                                      0}});
                                candidate.objects.push_back(
                                    {"out-std", frame.origin, {}, {1, rotation, 0, 0, 0}});
                                for (const auto& [a, b] : motion.bonds) {
                                    int turn = 0;
                                    while (turn < 6 && core::Add(a, Turn({1, 0}, turn)) != b)
                                        ++turn;
                                    candidate.objects.push_back(
                                        {"bonder", a, {}, {1, turn, 0, 0, 0}});
                                }
                                auto reserved = cells;
                                reserved.insert(Key(motion.feed));
                                if (!AddUnusedPipes(puzzle, chambers, reserved, candidate))
                                    continue;
                                if (baseAttempts[Key(base)] >= 4) break;
                                ++attempts;
                                ++baseAttempts[Key(base)];
                                if (accept(candidate)) return candidate;
                            }
                    }
    return {};
}
} // namespace opusoptima::solvers
