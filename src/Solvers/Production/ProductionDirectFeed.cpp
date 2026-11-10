#include "ProductionDirectFeed.hpp"
#include "HexFrame.hpp"
#include "ProductionLayout.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <map>
#include <set>
#include <tuple>

namespace opusoptima::solvers {
namespace {
using Coord = core::HexCoord;
using Key = std::pair<int, int>;
using Edge = std::pair<std::size_t, std::size_t>;
constexpr std::array<Coord, 6> directions{{{1, 0}, {0, 1}, {-1, 1}, {-1, 0}, {0, -1}, {1, -1}}};
constexpr std::array<core::AtomType, 6> wheelElements{
    {core::AtomType::Salt, core::AtomType::Water, core::AtomType::Air, core::AtomType::Salt,
     core::AtomType::Fire, core::AtomType::Earth}};
Key Cell(Coord cell) {
    return {cell.q, cell.r};
}
Edge Bond(std::size_t a, std::size_t b) {
    return {std::min(a, b), std::max(a, b)};
}
Coord Turn(Coord cell, int rotation) {
    while (rotation-- > 0)
        cell = core::RotateClockwise(cell);
    return cell;
}
bool Bounded(Coord cell) {
    return cell.q >= -120 && cell.q <= 120 && cell.r >= -120 && cell.r <= 120;
}
bool Cardinal(core::AtomType type) {
    return type >= core::AtomType::Air && type <= core::AtomType::Water;
}
int Direction(Coord a, Coord b) {
    const auto delta = core::Subtract(b, a);
    const auto found = std::find(directions.begin(), directions.end(), delta);
    return found == directions.end() ? -1 : static_cast<int>(found - directions.begin());
}
struct Cover {
    core::HexFrame frame;
    std::array<std::size_t, 3> targets{};
    unsigned mask{}, cost{};
};
struct Conversion {
    std::size_t target{};
    core::AtomType source{}, product{};
};
struct Wheel {
    core::HexFrame frame;
    std::set<Key> cells;
    Coord Catalyst(core::AtomType element) const {
        const auto found = std::find(wheelElements.begin(), wheelElements.end(), element);
        return frame.ToWorld(directions.at(found - wheelElements.begin()));
    }
};
void Object(io::SolutionData& machine, const char* name, Coord position, int rotation = 0,
            int index = 0) {
    machine.objects.push_back({name, position, {}, {1, rotation, index, 0, 0}});
}
bool Molecule(const io::Molecule& molecule, bool source) {
    if (molecule.atoms.empty()) return false;
    std::set<Key> cells;
    for (const auto& atom : molecule.atoms)
        if (!Bounded(atom.position) || !cells.insert(Cell(atom.position)).second ||
            (atom.type != core::AtomType::Salt && !Cardinal(atom.type)))
            return false;
    std::set<std::pair<Key, Key>> edges;
    for (const auto& bond : molecule.bonds) {
        if (!Bounded(bond.from) || !Bounded(bond.to) || !cells.contains(Cell(bond.from)) ||
            !cells.contains(Cell(bond.to)) || core::Distance(bond.from, bond.to) != 1 ||
            !bond.bits || (source ? bond.bits != 1 : bond.bits != 1 && (bond.bits & ~14)))
            return false;
        auto a = Cell(bond.from), b = Cell(bond.to);
        if (b < a) std::swap(a, b);
        if (!edges.emplace(a, b).second) return false;
    }
    std::set<Key> reached{*cells.begin()};
    for (std::size_t previous = 0; previous != reached.size();) {
        previous = reached.size();
        for (const auto& [a, b] : edges)
            if (reached.contains(a) || reached.contains(b)) {
                reached.insert(a);
                reached.insert(b);
            }
    }
    return reached == cells;
}
std::optional<unsigned> ConversionCost(core::AtomType source, core::AtomType target,
                                       std::uint64_t permissions) {
    if (source == target) return 0;
    unsigned cost = 0;
    if (source != core::AtomType::Salt) {
        if (!(permissions & (1ULL << 12))) return {};
        ++cost;
    }
    if (target != core::AtomType::Salt) {
        if (!(permissions & (1ULL << 13)) || !(permissions & (1ULL << 28))) return {};
        ++cost;
    }
    return cost;
}
std::vector<Cover> Covers(const io::PuzzleData& puzzle) {
    const auto& source = puzzle.inputs[0];
    const auto& target = puzzle.outputs[0];
    std::vector<Cover> covers;
    for (int rotation = 0; rotation < 6; ++rotation)
        for (const auto& anchor : target.atoms) {
            Cover cover;
            cover.frame = {
                core::Subtract(anchor.position, Turn(source.atoms[0].position, rotation)),
                rotation};
            bool valid = true;
            for (std::size_t atom = 0; atom < source.atoms.size(); ++atom) {
                const auto position = cover.frame.ToWorld(source.atoms[atom].position);
                const auto found = std::find_if(
                    target.atoms.begin(), target.atoms.end(),
                    [&](const auto& candidate) { return candidate.position == position; });
                if (found == target.atoms.end()) {
                    valid = false;
                    break;
                }
                const auto cost =
                    ConversionCost(source.atoms[atom].type, found->type, puzzle.permissionBits);
                if (!cost) {
                    valid = false;
                    break;
                }
                const auto index = static_cast<std::size_t>(found - target.atoms.begin());
                cover.targets[atom] = index;
                cover.mask |= 1U << index;
                cover.cost += *cost;
            }
            if (valid && std::none_of(covers.begin(), covers.end(), [&](const auto& existing) {
                    return existing.targets == cover.targets;
                }))
                covers.push_back(cover);
        }
    std::stable_sort(covers.begin(), covers.end(),
                     [](const auto& a, const auto& b) { return a.cost < b.cost; });
    return covers;
}
bool IdleConduits(const io::PuzzleData& puzzle,
                  const std::vector<ProductionChamberLayout>& chambers, std::set<Key> reserved,
                  io::SolutionData& machine) {
    const auto roomOf = [&](Coord position) -> const ProductionChamberLayout* {
        for (const auto& chamber : chambers)
            if (ContainsProductionCell(chamber, position)) return &chamber;
        return nullptr;
    };
    for (std::size_t index = 0; index < puzzle.productionInfo->conduits.size(); ++index) {
        const auto& conduit = puzzle.productionInfo->conduits[index];
        const auto* a = roomOf(conduit.positionA);
        const auto* b = roomOf(conduit.positionB);
        if (!a || !b || a == b || conduit.path.empty() || conduit.path.size() > 64) return false;
        std::set<Key> shape;
        for (auto local : conduit.path)
            if (!Bounded(local) || !shape.insert(Cell(local)).second) return false;
        for (const auto* chamber : {a, b}) {
            bool placed = false;
            for (auto position : chamber->interior) {
                if (placed) break;
                for (int rotation = 0; rotation < 6; ++rotation) {
                    const core::HexFrame frame{position, rotation};
                    std::set<Key> footprint;
                    bool fits = true;
                    for (auto local : conduit.path) {
                        const auto cell = frame.ToWorld(local);
                        fits &= ContainsProductionCell(*chamber, cell) &&
                                !reserved.contains(Cell(cell));
                        footprint.insert(Cell(cell));
                    }
                    if (!fits) continue;
                    std::vector<int> body{1,
                                          rotation,
                                          0,
                                          0,
                                          0,
                                          100 + static_cast<int>(index),
                                          static_cast<int>(conduit.path.size())};
                    for (auto local : conduit.path) {
                        body.push_back(local.q);
                        body.push_back(local.r);
                    }
                    machine.objects.push_back({"pipe", position, {}, std::move(body)});
                    reserved.insert(footprint.begin(), footprint.end());
                    placed = true;
                    break;
                }
            }
            if (!placed) return false;
        }
    }
    return true;
}
bool AddPrisms(io::SolutionData& machine, const io::Molecule& target,
               const std::vector<Coord>& positions, const std::map<Edge, unsigned>& edges,
               const ProductionChamberLayout& chamber) {
    const auto targetAt = [&](Coord cell) -> std::optional<std::size_t> {
        const auto found = std::find(positions.begin(), positions.end(), cell);
        if (found == positions.end()) return {};
        return found - positions.begin();
    };
    for (const auto& [edge, mask] : edges) {
        if (mask == 1) continue;
        if (target.atoms[edge.first].type != core::AtomType::Fire ||
            target.atoms[edge.second].type != core::AtomType::Fire)
            return false;
        for (int color : {2, 4, 8})
            if (mask & color) {
                bool placed = false;
                for (int rotation = 0; rotation < 6 && !placed; ++rotation)
                    for (auto local : {Coord{}, Coord{0, 1}, Coord{1, 0}}) {
                        const auto origin =
                            core::Subtract(positions[edge.first], Turn(local, rotation));
                        const core::HexFrame frame{origin, rotation};
                        const std::array<Coord, 3> vertices{
                            {frame.ToWorld({}), frame.ToWorld({0, 1}), frame.ToWorld({1, 0})}};
                        constexpr std::array<int, 3> colors{{8, 2, 4}};
                        bool fits = std::all_of(vertices.begin(), vertices.end(), [&](Coord cell) {
                            return ContainsProductionCell(chamber, cell);
                        });
                        bool covers = false;
                        for (int side = 0; side < 3 && fits; ++side) {
                            const auto a = targetAt(vertices[side]),
                                       b = targetAt(vertices[(side + 1) % 3]);
                            if (!a || !b) continue;
                            covers |= Bond(*a, *b) == edge && colors[side] == color;
                            if (target.atoms[*a].type != core::AtomType::Fire ||
                                target.atoms[*b].type != core::AtomType::Fire)
                                continue;
                            const auto desired = edges.find(Bond(*a, *b));
                            fits &= desired != edges.end() &&
                                    (desired->second == 1 || (desired->second & colors[side]));
                        }
                        if (!fits || !covers) continue;
                        Object(machine, "bonder-prisma", origin, rotation);
                        placed = true;
                        break;
                    }
                if (!placed) return false;
            }
    }
    return true;
}
} // namespace

std::optional<io::SolutionData> CompileAcceptedProductionDirectFeed(
    const io::PuzzleData& puzzle, const std::function<bool(const io::SolutionData&)>& accept) {
    if (puzzle.version != 3 || !puzzle.productionInfo || puzzle.productionInfo->isolation ||
        puzzle.outputMultiplier < 1 || puzzle.outputMultiplier > 8 || puzzle.inputs.size() != 1 ||
        puzzle.outputs.size() != 1 || puzzle.inputs[0].atoms.size() != 3 ||
        puzzle.outputs[0].atoms.size() != 6 || !Molecule(puzzle.inputs[0], true) ||
        !Molecule(puzzle.outputs[0], false))
        return {};
    const auto& source = puzzle.inputs[0];
    const auto& target = puzzle.outputs[0];
    const auto covers = Covers(puzzle);
    const auto chambers = PlanProductionChambers(puzzle);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    const auto at = [&](const io::Molecule& molecule, Coord cell) {
        return static_cast<std::size_t>(
            std::find_if(molecule.atoms.begin(), molecule.atoms.end(),
                         [&](const auto& atom) { return atom.position == cell; }) -
            molecule.atoms.begin());
    };
    std::map<Edge, unsigned> edges;
    for (const auto& bond : target.bonds)
        edges.emplace(Bond(at(target, bond.from), at(target, bond.to)), bond.bits);
    std::size_t attempts = 0;
    for (std::size_t first = 0; first < covers.size(); ++first)
        for (std::size_t second = first + 1; second < covers.size(); ++second) {
            const auto& a = covers[first];
            const auto& b = covers[second];
            if ((a.mask & b.mask) || (a.mask | b.mask) != 63) continue;
            std::vector<Conversion> conversions;
            std::set<Edge> inherited;
            for (const auto* cover : {&a, &b}) {
                for (std::size_t atom = 0; atom < source.atoms.size(); ++atom) {
                    const auto destination = cover->targets[atom];
                    if (source.atoms[atom].type != target.atoms[destination].type)
                        conversions.push_back(
                            {destination, source.atoms[atom].type, target.atoms[destination].type});
                }
                for (const auto& bond : source.bonds)
                    inherited.insert(Bond(cover->targets[at(source, bond.from)],
                                          cover->targets[at(source, bond.to)]));
            }
            const auto duplicateCount =
                std::count_if(conversions.begin(), conversions.end(), [](const auto& conversion) {
                    return conversion.product != core::AtomType::Salt;
                });
            if (duplicateCount > 2) continue;
            const bool cuts =
                std::any_of(inherited.begin(), inherited.end(), [&](const auto& edge) {
                    return !edges.contains(edge) || edges.at(edge) != 1;
                });
            const bool joins = std::any_of(edges.begin(), edges.end(), [&](const auto& edge) {
                return edge.second == 1 && !inherited.contains(edge.first);
            });
            const bool colored = std::any_of(edges.begin(), edges.end(),
                                             [](const auto& edge) { return edge.second != 1; });
            if ((cuts && !(puzzle.permissionBits & (1ULL << 9))) ||
                (joins && !(puzzle.permissionBits & (1ULL << 8))) ||
                (colored && !(puzzle.permissionBits & (1ULL << 11))))
                continue;
            for (const auto& chamber : chambers) {
                if (puzzle.productionInfo->chambers[chamber.chamberIndex].chamberType != "Large")
                    continue;
                for (int rotation = 0; rotation < 6; ++rotation)
                    for (auto anchor : chamber.interior) {
                        if (std::chrono::steady_clock::now() >= deadline || attempts >= 64)
                            return {};
                        const core::HexFrame frame{
                            core::Subtract(anchor, Turn(target.atoms[0].position, rotation)),
                            rotation};
                        std::vector<Coord> positions;
                        std::set<Key> outputCells;
                        bool fits = true;
                        for (const auto& atom : target.atoms) {
                            const auto cell = frame.ToWorld(atom.position);
                            fits &= ContainsProductionCell(chamber, cell);
                            positions.push_back(cell);
                            outputCells.insert(Cell(cell));
                        }
                        if (!fits) continue;
                        std::vector<Wheel> wheelDomain;
                        if (duplicateCount)
                            for (auto base : chamber.interior)
                                for (int angle = 0; angle < 6; ++angle) {
                                    Wheel wheel{{base, angle}, {Cell(base)}};
                                    bool valid = !outputCells.contains(Cell(base));
                                    for (auto local : directions) {
                                        const auto cell = wheel.frame.ToWorld(local);
                                        valid &= ContainsProductionCell(chamber, cell) &&
                                                 !outputCells.contains(Cell(cell));
                                        wheel.cells.insert(Cell(cell));
                                    }
                                    if (valid) wheelDomain.push_back(std::move(wheel));
                                }
                        std::vector<const Conversion*> duplicates;
                        for (const auto& conversion : conversions)
                            if (conversion.product != core::AtomType::Salt)
                                duplicates.push_back(&conversion);
                        std::vector<Wheel> wheels;
                        wheels.reserve(2);
                        std::vector<Coord> catalysts(duplicates.size());
                        std::optional<io::SolutionData> accepted;
                        const auto emit = [&]() {
                            io::SolutionData machine;
                            machine.puzzleName = puzzle.name;
                            machine.solutionName = "Direct production feed overlay";
                            for (const auto* cover : {&a, &b}) {
                                const auto input = frame.Compose(cover->frame);
                                Object(machine, "input", input.origin, input.Turns());
                            }
                            Object(machine, "out-std", frame.origin, frame.Turns());
                            auto reserved = outputCells;
                            for (std::size_t device = 0; device < wheels.size(); ++device) {
                                Object(machine, "baron", wheels[device].frame.origin,
                                       wheels[device].frame.Turns());
                                machine.objects.back().bodyData[4] = static_cast<int>(device);
                                reserved.insert(wheels[device].cells.begin(),
                                                wheels[device].cells.end());
                            }
                            for (const auto& conversion : conversions)
                                if (conversion.source != core::AtomType::Salt)
                                    Object(machine, "glyph-calcification",
                                           positions[conversion.target]);
                            for (std::size_t duplicate = 0; duplicate < duplicates.size();
                                 ++duplicate)
                                Object(machine, "glyph-duplication", catalysts[duplicate],
                                       Direction(catalysts[duplicate],
                                                 positions[duplicates[duplicate]->target]));
                            for (const auto& edge : inherited)
                                if (!edges.contains(edge) || edges.at(edge) != 1)
                                    Object(
                                        machine, "unbonder", positions[edge.first],
                                        Direction(positions[edge.first], positions[edge.second]));
                            for (const auto& [edge, mask] : edges)
                                if (mask == 1 && !inherited.contains(edge))
                                    Object(
                                        machine, "bonder", positions[edge.first],
                                        Direction(positions[edge.first], positions[edge.second]));
                            if (!AddPrisms(machine, target, positions, edges, chamber) ||
                                !IdleConduits(puzzle, chambers, reserved, machine))
                                return false;
                            ++attempts;
                            if (!accept(machine)) return false;
                            accepted = std::move(machine);
                            return true;
                        };
                        const auto bind = [&](auto&& self, std::size_t index) -> bool {
                            if (std::chrono::steady_clock::now() >= deadline || attempts >= 64)
                                return false;
                            if (index == duplicates.size()) return emit();
                            const auto& conversion = *duplicates[index];
                            for (const auto& existing : wheels) {
                                const auto catalyst = existing.Catalyst(conversion.product);
                                if (core::Distance(catalyst, positions[conversion.target]) != 1)
                                    continue;
                                catalysts[index] = catalyst;
                                if (self(self, index + 1)) return true;
                            }
                            if (wheels.size() >= 2) return false;
                            for (const auto& wheel : wheelDomain) {
                                const auto catalyst = wheel.Catalyst(conversion.product);
                                if (core::Distance(catalyst, positions[conversion.target]) != 1)
                                    continue;
                                bool overlap = false;
                                for (const auto& existing : wheels)
                                    for (const auto& cell : wheel.cells)
                                        overlap |= existing.cells.contains(cell);
                                if (overlap) continue;
                                wheels.push_back(wheel);
                                catalysts[index] = catalyst;
                                const bool found = self(self, index + 1);
                                wheels.pop_back();
                                if (found) return true;
                                if (attempts >= 64 || std::chrono::steady_clock::now() >= deadline)
                                    return false;
                            }
                            return false;
                        };
                        if (bind(bind, 0)) return accepted;
                    }
            }
        }
    return {};
}
} // namespace opusoptima::solvers
