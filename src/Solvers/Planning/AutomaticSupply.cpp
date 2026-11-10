#include "AutomaticSupply.hpp"
#include "LogManager.hpp"
#include "AssemblySolver.hpp"
#include "ExecutionPlan.hpp"
#include "FeedLayout.hpp"
#include "InventoryIR.hpp"
#include "InventorySearch.hpp"
#include "ProductionLayout.hpp"
#include "SupplyExecutionIR.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <set>
#include <tuple>
#include <unordered_set>

namespace opusoptima::solvers {
namespace {
using IR = ExecutionIR;
using K = IR::Kind;
using Coord = core::HexCoord;
Coord Turn(Coord cell, int turns) {
    while (turns-- > 0)
        cell = core::RotateClockwise(cell);
    return cell;
}
Coord Tip(const std::vector<Coord>& rail, const HeldArmState& arm) {
    return core::Add(rail.at(arm.trackIndex), Turn({arm.length, 0}, arm.direction));
}
struct CompactLayout {
    std::vector<core::HexFrame> inputs, outputs;
    std::vector<Coord> rail, cells, buffers;
    std::map<K, Coord> stations;
    HeldArmState start{0, 1, 4, 0};
    int minQ{}, maxQ{}, minR{}, maxR{}, minSum{}, maxSum{};
    std::chrono::steady_clock::time_point deadline;
};
bool CompactMotion(const CompactLayout& layout, const IR::State& world, const HeldArmState& from,
                   const HeldArmState& to, char instruction, bool holding,
                   const std::vector<std::vector<Coord>>& feeds, IR::State& next) {
    if (std::chrono::steady_clock::now() >= layout.deadline)
        throw IR::BudgetFailure("compact chamber layout deadline exceeded");
    const auto oldBase = layout.rail.at(from.trackIndex), newBase = layout.rail.at(to.trackIndex);
    const auto oldTip = Tip(layout.rail, from), newTip = Tip(layout.rail, to);
    const bool rotation =
        instruction == 'R' || instruction == 'r' || instruction == 'P' || instruction == 'p';
    const bool pivot = instruction == 'P' || instruction == 'p';
    const int turns = holding ? (to.fragmentRotation - from.fragmentRotation + 6) % 6 : 0;
    std::vector<IR::AtomId> moving;
    if (holding) {
        const auto grip =
            std::find_if(world.atoms.begin(), world.atoms.end(),
                         [&](const auto& entry) { return entry.second.position == oldTip; });
        if (grip == world.atoms.end()) return false;
        moving = IR::Fragment(world, grip->first);
        for (auto id : moving)
            next.atoms.at(id).position =
                core::Add(newTip, Turn(core::Subtract(world.atoms.at(id).position, oldTip), turns));
    }
    struct Point {
        double x, y;
    };
    const auto xy = [](Coord cell) {
        return Point{cell.q + cell.r * .5, cell.r * std::sqrt(3.) * .5};
    };
    const auto inside = [&](Point point) {
        const double r = point.y * 2 / std::sqrt(3.), q = point.x - r * .5;
        return q >= layout.minQ - .4 && q <= layout.maxQ + .4 && r >= layout.minR - .4 &&
               r <= layout.maxR + .4 && q + r >= layout.minSum - .4 && q + r <= layout.maxSum + .4;
    };
    std::vector<Coord> ambient;
    for (const auto& feed : feeds) {
        const bool blocked = std::any_of(feed.begin(), feed.end(), [&](Coord cell) {
            return std::any_of(world.atoms.begin(), world.atoms.end(),
                               [&](const auto& entry) { return entry.second.position == cell; });
        });
        if (!blocked) ambient.insert(ambient.end(), feed.begin(), feed.end());
    }
    const auto center = xy(pivot ? oldTip : oldBase), delta = xy(core::Subtract(newTip, oldTip));
    const auto position = [&](Coord cell, double t, bool material) {
        const auto p = xy(cell);
        if (rotation && (material || !pivot)) {
            const auto angle =
                (instruction == 'r' || instruction == 'p' ? 1 : -1) * std::acos(-1.) / 3 * t;
            return Point{
                center.x + (p.x - center.x) * std::cos(angle) - (p.y - center.y) * std::sin(angle),
                center.y + (p.x - center.x) * std::sin(angle) + (p.y - center.y) * std::cos(angle)};
        }
        return Point{p.x + delta.x * t, p.y + delta.y * t};
    };
    const auto close = [](Point a, Point b, double distance) {
        return (a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y) < distance * distance;
    };
    if (!holding && oldBase == newBase) {
        const auto base = xy(oldBase);
        for (const auto& [id, atom] : world.atoms)
            if (close(base, xy(atom.position), .45)) return false;
        for (auto cell : ambient)
            if (close(base, xy(cell), .45)) return false;
        for (int sample = 0; sample <= 8; ++sample)
            if (!inside(position(oldTip, sample / 8., false))) return false;
        return true;
    }
    if (instruction == 'g') {
        const auto base = xy(oldBase);
        for (const auto& [id, atom] : world.atoms)
            if (!inside(xy(atom.position)) || close(base, xy(atom.position), .45)) return false;
        return true;
    }
    constexpr int samples = 8;
    for (int sample = 0; sample <= samples; ++sample) {
        const double t = static_cast<double>(sample) / samples;
        const auto a = xy(oldBase), b = xy(newBase);
        const Point base{a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t};
        if (!inside(base) || !inside(position(oldTip, t, false))) return false;
        for (const auto& [id, atom] : world.atoms) {
            const bool active = std::binary_search(moving.begin(), moving.end(), id);
            const auto p = active ? position(atom.position, t, true) : xy(atom.position);
            if (!inside(p) || close(p, base, .45)) return false;
            if (!active) continue;
            for (const auto& [other, stationary] : world.atoms)
                if (!std::binary_search(moving.begin(), moving.end(), other) &&
                    close(p, xy(stationary.position), .6))
                    return false;
            for (auto cell : ambient)
                if (close(p, xy(cell), .6)) return false;
        }
        for (auto cell : ambient)
            if (close(base, xy(cell), .45)) return false;
    }
    return true;
}
io::SolutionData Compile(const io::PuzzleData& p, const AssemblyPlan& assembly,
                         const SupplyPlan& supply, int turns, int spacing,
                         std::unordered_set<Coord, core::HexCoordHash>* footprint = nullptr,
                         const JointSupplyPlan* joint = nullptr, bool preserveResidualBonds = false,
                         const CompactLayout* compact = nullptr, bool sharedWorkstation = false) {
    std::vector<core::HexFrame> outputs =
        compact ? compact->outputs : std::vector<core::HexFrame>(p.outputs.size(), {{0, 0}, turns});
    struct Bounds {
        int minQ{}, maxQ{}, minR{}, maxR{};
    };
    std::vector<Bounds> bounds;
    int reach = 1;
    for (std::size_t port = 0; port < outputs.size(); ++port) {
        Bounds b;
        bool first = true;
        for (const auto& atom : p.outputs[port].atoms) {
            const auto c = outputs[port].ToWorld(atom.position);
            if (first) {
                b = {c.q, c.q, c.r, c.r};
                first = false;
            }
            b.minQ = std::min(b.minQ, c.q);
            b.maxQ = std::max(b.maxQ, c.q);
            b.minR = std::min(b.minR, c.r);
            b.maxR = std::max(b.maxR, c.r);
        }
        if (b.maxQ - b.minQ > 32) throw IR::Unsupported("automatic output exceeds width bound");
        reach = std::max(reach, b.maxR - b.minR + 1);
        bounds.push_back(b);
    }
    std::vector<core::HexFrame> inputs =
        compact ? compact->inputs : std::vector<core::HexFrame>(p.inputs.size());
    const auto& usage = joint ? joint->usage : supply.usage;
    auto usedInput = [&](std::size_t index) { return usage.at(index).batches != 0; };
    int cursor = 0;
    for (std::size_t index = 0; !compact && index < p.inputs.size(); ++index) {
        if (!usedInput(index)) continue;
        const auto& input = p.inputs[index];
        const auto layout = PlanExtractionLayout(input);
        int lowQ = layout.cells.front().q, highQ = lowQ, lowR = layout.cells.front().r,
            highR = lowR;
        for (auto c : layout.cells) {
            lowQ = std::min(lowQ, c.q);
            highQ = std::max(highQ, c.q);
            lowR = std::min(lowR, c.r);
            highR = std::max(highR, c.r);
        }
        reach = std::max(reach, std::min(3, highR - lowR + 1 + (input.bonds.empty() ? 0 : 1)));
        inputs[index] = {{cursor - lowQ, -highR}, layout.rotation};
        cursor += highQ - lowQ + spacing;
    }
    if (compact) reach = 3;
    if (reach > 3 || cursor > 64) throw IR::Unsupported("automatic rail exceeds reach/width bound");
    if (reach > 1 && !(p.permissionBits & (1ULL << 2)))
        throw IR::PermissionFailure(2, "automatic rail requires piston permission");
    for (std::size_t port = 0; !compact && port < outputs.size(); ++port) {
        const auto b = bounds[port];
        outputs[port].origin = {cursor + (spacing == 1 ? 1 : 3) - b.minQ, -b.maxR};
        cursor = outputs[port].origin.q + b.maxQ + spacing;
    }
    IR program;
    auto material = IR::Initial(p);
    std::vector<Coord> calcifiers, disposals;
    std::vector<Coord> purifiers;
    std::vector<Coord> projectors;
    std::vector<Coord> duplicators;
    std::vector<Coord> animismuses, unifiers, dispersers;
    const std::vector<Coord> unifyContacts{{0, -1}, {1, -1}, {0, 1}, {-1, 1}};
    const std::vector<Coord> disperseOutlets{{-1, 0}, {1, 0}, {0, -1}, {1, -1}};
    const auto source = joint ? BuildJointSupplyExecutionIR(p, *joint, outputs, inputs)
                              : BuildSupplyExecutionIR(p, assembly, supply, outputs[0], inputs);
    // Reserve reaction contacts beyond the outputs so unrelated deliveries need
    // not cross an active glyph. Material products keep their fresh IR frames.
    const auto station = [&](K kind, Coord fallback) {
        if (!compact) return fallback;
        const auto found = compact->stations.find(kind);
        if (found == compact->stations.end())
            throw IR::Unsupported("compact reaction kind has no station");
        return found->second;
    };
    for (const auto& original : source.steps) {
        if (original.operation.kind == K::Discard)
            disposals.push_back(station(K::Discard, {cursor + 2, 0}));
        else if (original.operation.kind == K::Calcify)
            calcifiers.push_back(station(K::Calcify, {cursor + 2, 0}));
        else if (original.operation.kind == K::Purify) {
            if (!(p.permissionBits & (1ULL << 2)))
                throw IR::PermissionFailure(
                    2, "automatic purification station requires piston permission");
            reach = std::max(reach, 2);
            purifiers.push_back(station(K::Purify, {cursor + 2, -1}));
        } else if (original.operation.kind == K::Animismus) {
            if (!(p.permissionBits & (1ULL << 2)))
                throw IR::PermissionFailure(2, "automatic animismus requires piston");
            reach = std::max(reach, 3);
            animismuses.push_back(station(K::Animismus, {cursor + 2, -1}));
        } else if (original.operation.kind == K::Unify || original.operation.kind == K::Disperse) {
            if (!(p.permissionBits & (1ULL << 2)))
                throw IR::PermissionFailure(2, "automatic quintessence requires piston");
            reach = std::max(reach, 3);
            (original.operation.kind == K::Unify ? unifiers : dispersers)
                .push_back(station(original.operation.kind, {cursor + 2, -1}));
        } else if (original.operation.kind == K::Project ||
                   original.operation.kind == K::Duplicate) {
            if (!(p.permissionBits & (1ULL << 2)))
                throw IR::PermissionFailure(2, "automatic chemistry requires piston");
            reach = std::max(reach, 2);
            if (original.operation.kind == K::Project)
                projectors.push_back(station(K::Project, {cursor + 2, -1}));
            else
                duplicators.push_back(station(K::Duplicate, {cursor + 2, -1}));
        } else
            continue;
        if (!compact) cursor += spacing + 3;
    }
    std::map<std::tuple<int, int, int>, Coord> cuttingStations;
    auto cuttingKey = [](const auto& state, const auto& operation) {
        const auto anchor = operation.atoms.at(0);
        const auto position = state.atoms.at(anchor).position;
        const auto delta = core::Subtract(state.atoms.at(operation.atoms.at(1)).position, position);
        int top = position.r;
        for (const auto id : IR::Fragment(state, anchor))
            top = std::max(top, state.atoms.at(id).position.r);
        return std::tuple{delta.q, delta.r, position.r - top};
    };
    if (preserveResidualBonds) {
        for (const auto& step : source.steps) {
            if (step.operation.kind != K::Unbond) continue;
            const auto key = cuttingKey(step.preconditions, step.operation);
            if (cuttingStations.contains(key)) continue;
            cuttingStations[key] = {cursor + 3, std::min(-1, std::get<2>(key))};
            cursor += 7;
        }
    }
    if (cursor > 96)
        throw IR::LayoutFailure("automatic reaction/cutting stations exceed width bound");
    const int inventoryStart = cursor + 3;
    std::size_t calcifier = 0, purifier = 0, projector = 0, duplicator = 0, animismus = 0,
                unifier = 0, disperser = 0;
    std::size_t disposal = 0;
    std::map<std::size_t, std::vector<Coord>> releasedProducts;
    if (!sharedWorkstation) for (const auto& original : source.steps) {
        if (original.operation.kind == K::Bond) continue;
        auto operation = original.operation;
        if (operation.kind == K::Acquire) {
            const core::HexFrame frame{operation.translation, operation.rotation};
            std::vector<IR::AtomId> blockers;
            for (const auto& [id, atom] : material.atoms)
                for (const auto& incoming : p.inputs.at(operation.index).atoms)
                    if (atom.position == frame.ToWorld(incoming.position)) blockers.push_back(id);
            for (const auto id : blockers) {
                const auto position = material.atoms.at(id).position;
                bool overlaps = false;
                for (const auto& incoming : p.inputs.at(operation.index).atoms)
                    overlaps |= position == frame.ToWorld(incoming.position);
                if (!overlaps) continue;
                const auto fragment = IR::Fragment(material, id);
                int lowQ = position.q, highQ = position.q;
                for (const auto member : fragment) {
                    lowQ = std::min(lowQ, material.atoms.at(member).position.q);
                    highQ = std::max(highQ, material.atoms.at(member).position.q);
                }
                Coord delta;
                if (compact) {
                    const auto destination = std::find_if(
                        compact->buffers.begin(), compact->buffers.end(), [&](Coord candidate) {
                            const auto offset = core::Subtract(candidate, position);
                            for (const auto member : fragment) {
                                const auto cell =
                                    core::Add(material.atoms.at(member).position, offset);
                                if (std::find(compact->cells.begin(), compact->cells.end(), cell) ==
                                    compact->cells.end())
                                    return false;
                                for (const auto& [other, atom] : material.atoms)
                                    if (!std::binary_search(fragment.begin(), fragment.end(),
                                                            other) &&
                                        atom.position == cell)
                                        return false;
                            }
                            return true;
                        });
                    if (destination == compact->buffers.end())
                        throw IR::LayoutFailure("compact chamber has no free residual cache");
                    delta = core::Subtract(*destination, position);
                } else {
                    const int lastDestination = 95 - (highQ - lowQ);
                    int destination = inventoryStart;
                    for (; destination <= lastDestination; ++destination) {
                        bool occupied = false;
                        for (const auto member : fragment) {
                            const auto cell = core::Add(material.atoms.at(member).position,
                                                        {destination - lowQ, 0});
                            for (const auto& [other, atom] : material.atoms)
                                if (!std::binary_search(fragment.begin(), fragment.end(), other) &&
                                    atom.position == cell)
                                    occupied = true;
                        }
                        if (!occupied) break;
                    }
                    if (destination > lastDestination)
                        throw IR::LayoutFailure("automatic residual inventory exceeds width bound");
                    delta = {destination - lowQ, 0};
                    cursor = std::max(cursor, destination + highQ - lowQ + 1);
                    if (cursor > 96)
                        throw IR::LayoutFailure("automatic residual inventory exceeds width bound");
                }
                std::optional<std::size_t> storedSlot;
                for (const auto& [slot, ids] : material.stored)
                    if (std::binary_search(ids.begin(), ids.end(), id)) {
                        storedSlot = slot;
                        break;
                    }
                if (storedSlot) program.Append(p, material, {K::Retrieve, *storedSlot});
                program.Append(p, material, {K::Relocate, 0, {id}, 0, 0, delta});
                if (storedSlot) program.Append(p, material, {K::Store, *storedSlot, {id}});
            }
        }
        if (operation.kind == K::Discard) {
            const auto id = operation.atoms.at(0);
            program.Append(
                p, material,
                {K::Relocate,
                 0,
                 {id},
                 0,
                 0,
                 core::Subtract(disposals.at(disposal++), material.atoms.at(id).position)});
        } else if (operation.kind == K::Calcify) {
            const auto id = operation.atoms.at(0);
            program.Append(
                p, material,
                {K::Relocate,
                 0,
                 {id},
                 0,
                 0,
                 core::Subtract(calcifiers.at(calcifier++), material.atoms.at(id).position)});
        } else if (operation.kind == K::Purify || operation.kind == K::Animismus) {
            const auto station = operation.kind == K::Purify ? purifiers.at(purifier++)
                                                             : animismuses.at(animismus++);
            for (std::size_t reagent = 0; reagent < operation.atoms.size(); ++reagent) {
                const auto id = operation.atoms[reagent];
                const auto cell = core::Add(station, {static_cast<int>(reagent), 0});
                program.Append(p, material,
                               {K::Relocate,
                                0,
                                {id},
                                0,
                                0,
                                core::Subtract(cell, material.atoms.at(id).position)});
            }
            std::vector<Coord> fresh{core::Add(station, {0, 1})};
            if (operation.kind == K::Animismus) fresh.push_back(core::Add(station, {1, -1}));
            releasedProducts.emplace(program.steps.size(), std::move(fresh));
        } else if (operation.kind == K::Unify || operation.kind == K::Disperse) {
            const auto station =
                operation.kind == K::Unify ? unifiers.at(unifier++) : dispersers.at(disperser++);
            for (std::size_t reagent = 0; reagent < operation.atoms.size(); ++reagent) {
                const auto id = operation.atoms[reagent];
                const auto cell = operation.kind == K::Unify
                                      ? core::Add(station, unifyContacts.at(reagent))
                                      : station;
                program.Append(p, material,
                               {K::Relocate,
                                0,
                                {id},
                                0,
                                0,
                                core::Subtract(cell, material.atoms.at(id).position)});
            }
            std::vector<Coord> fresh;
            if (operation.kind == K::Unify)
                fresh.push_back(station);
            else
                for (auto offset : disperseOutlets)
                    fresh.push_back(core::Add(station, offset));
            releasedProducts.emplace(program.steps.size(), std::move(fresh));
        } else if (operation.kind == K::Project || operation.kind == K::Duplicate) {
            const auto station = operation.kind == K::Project ? projectors.at(projector++)
                                                              : duplicators.at(duplicator++);
            // Keep the consumed/converted operand held while parking its partner first.
            for (std::size_t reagent : {1U, 0U}) {
                const auto id = operation.atoms.at(reagent);
                const auto cell = core::Add(station, {static_cast<int>(reagent), 0});
                program.Append(p, material,
                               {K::Relocate,
                                0,
                                {id},
                                0,
                                0,
                                core::Subtract(cell, material.atoms.at(id).position)});
            }
        } else if (operation.kind == K::Relocate) {
            const auto id = operation.atoms.at(0);
            auto rotated = material.atoms.at(id).position;
            for (int turn = 0; turn < operation.rotation; ++turn)
                rotated = core::RotateClockwise(rotated);
            operation.translation = core::Subtract(original.effects.atoms.at(id).position, rotated);
        }
        if (operation.kind == K::Unbond) {
            const IR::Edge edge{std::min(operation.atoms[0], operation.atoms[1]),
                                std::max(operation.atoms[0], operation.atoms[1])};
            if (!material.bonds.contains(edge)) continue;
            if (preserveResidualBonds) {
                const auto anchor = operation.atoms.at(0);
                const auto key = cuttingKey(material, operation);
                program.Append(
                    p, material,
                    {K::Relocate,
                     0,
                     {anchor},
                     0,
                     0,
                     core::Subtract(cuttingStations.at(key), material.atoms.at(anchor).position)});
            }
        }
        program.Append(p, material, operation);
        if (operation.kind == K::Acquire && !preserveResidualBonds) {
            const auto remainingBonds = material.bonds;
            for (const auto& [edge, bits] : remainingBonds)
                if (edge.a >= original.preconditions.nextId &&
                    edge.b >= original.preconditions.nextId)
                    program.Append(p, material, {K::Unbond, 0, {edge.a, edge.b}, bits});
        }
        // Finish a cut sequence before moving fragments away from its glyphs.
        const auto nextSource = &original + 1;
        const bool cutting = operation.kind == K::Unbond &&
                             nextSource != source.steps.data() + source.steps.size() &&
                             nextSource->operation.kind == K::Unbond;
        const bool looseAcquire =
            operation.kind == K::Acquire && p.inputs.at(operation.index).bonds.empty();
        if (!compact && !cutting &&
            (looseAcquire || operation.kind == K::Unbond || operation.kind == K::Retrieve)) {
            const auto pendingAtoms = material.atoms;
            for (const auto& [id, atom] : pendingAtoms) {
                if (IR::Fragment(material, id).size() != 1) continue;
                if (std::any_of(
                        material.stored.begin(), material.stored.end(), [&](const auto& entry) {
                            return std::binary_search(entry.second.begin(), entry.second.end(), id);
                        }))
                    continue;
                std::size_t stationIndex = 0, projectIndex = 0, duplicateIndex = 0,
                            animismusIndex = 0, unifyIndex = 0, disperseIndex = 0;
                for (const auto& future : source.steps) {
                    const auto kind = future.operation.kind;
                    if (kind != K::Purify && kind != K::Project && kind != K::Duplicate &&
                        kind != K::Animismus && kind != K::Unify && kind != K::Disperse)
                        continue;
                    const auto station = kind == K::Purify      ? purifiers.at(stationIndex++)
                                         : kind == K::Project   ? projectors.at(projectIndex++)
                                         : kind == K::Unify     ? unifiers.at(unifyIndex++)
                                         : kind == K::Disperse  ? dispersers.at(disperseIndex++)
                                         : kind == K::Animismus ? animismuses.at(animismusIndex++)
                                                                : duplicators.at(duplicateIndex++);
                    if (&future <= &original) continue;
                    const auto reagent =
                        std::find(future.operation.atoms.begin(), future.operation.atoms.end(), id);
                    if (reagent != future.operation.atoms.end()) {
                        const auto operand =
                            static_cast<std::size_t>(reagent - future.operation.atoms.begin());
                        // The final reagent must arrive at the reaction step, not during feed
                        // extraction.
                        if ((kind == K::Purify || kind == K::Animismus) &&
                            operand + 1 == future.operation.atoms.size())
                            break;
                        const auto cell =
                            kind == K::Disperse
                                ? station
                                : core::Add(station, kind == K::Unify
                                                         ? unifyContacts.at(operand)
                                                         : Coord{static_cast<int>(operand), 0});
                        program.Append(p, material,
                                       {K::Relocate,
                                        0,
                                        {id},
                                        0,
                                        0,
                                        core::Subtract(cell, material.atoms.at(id).position)});
                        break;
                    }
                }
            }
        }
        if (original.operation.kind != K::Relocate) continue;
        // Always-on bonders activate as soon as both final endpoints arrive.
        for (std::size_t port = 0; port < outputs.size(); ++port)
            for (const auto& bond : p.outputs[port].bonds) {
                const auto& output = outputs[port];
                auto x = material.atoms.end(), y = material.atoms.end();
                for (auto it = material.atoms.begin(); it != material.atoms.end(); ++it) {
                    if (std::any_of(material.stored.begin(), material.stored.end(),
                                    [&](const auto& entry) {
                                        return std::binary_search(entry.second.begin(),
                                                                  entry.second.end(), it->first);
                                    }))
                        continue;
                    if (it->second.position == output.ToWorld(bond.from)) x = it;
                    if (it->second.position == output.ToWorld(bond.to)) y = it;
                }
                if (x == material.atoms.end() || y == material.atoms.end()) continue;
                IR::Edge edge{std::min(x->first, y->first), std::max(x->first, y->first)};
                const auto existing = material.bonds.find(edge);
                const int missing =
                    bond.bits & ~(existing == material.bonds.end() ? 0 : existing->second);
                if (missing)
                    program.Append(
                        p, material,
                        {K::Bond, 0, {edge.a, edge.b}, static_cast<std::uint8_t>(missing)});
            }
    }
    const Coord sharedEdge{cursor + 3, -1};
    if (sharedWorkstation) {
        cursor += 5;
        reach = 3;
        const auto& molecule = p.outputs.at(0);
        if (compact || joint || p.productionInfo || p.outputs.size() != 1 ||
            molecule.atoms.size() != 3 || molecule.bonds.size() != 2 ||
            std::any_of(molecule.bonds.begin(), molecule.bonds.end(),
                        [](const auto& bond) { return bond.bits != 1; }) ||
            std::any_of(p.inputs.begin(), p.inputs.end(),
                        [](const auto& input) { return input.atoms.size() != 1; }))
            throw IR::Unsupported("shared workstation requires a singleton-fed ordinary trimer");
        if (!(p.permissionBits & (1ULL << 2)))
            throw IR::PermissionFailure(2, "shared workstation requires piston");
        const auto& finalState = source.steps.back().preconditions;
        std::unordered_map<Coord, IR::AtomId, core::HexCoordHash> ids;
        for (const auto& [id, atom] : finalState.atoms) ids[atom.position] = id;
        const auto& first = molecule.bonds[0];
        const auto& second = molecule.bonds[1];
        Coord middle = first.from;
        if (middle != second.from && middle != second.to) middle = first.to;
        if (middle != second.from && middle != second.to)
            throw IR::Unsupported("shared workstation requires a connected path");
        const Coord leaf = first.from == middle ? first.to : first.from;
        const Coord last = second.from == middle ? second.to : second.from;
        const auto a = ids.at(outputs[0].ToWorld(leaf));
        const auto b = ids.at(outputs[0].ToWorld(middle));
        const auto c = ids.at(outputs[0].ToWorld(last));
        auto move = [&](IR::AtomId id, Coord destination, int rotation = 0) {
            program.Append(p, material,
                           {K::Relocate, 0, {id}, 0, rotation,
                            core::Subtract(destination, Turn(material.atoms.at(id).position, rotation))});
        };
        for (const auto& step : source.steps) {
            if (step.operation.kind != K::Acquire) continue;
            const auto id = material.nextId;
            program.Append(p, material, step.operation);
            const auto uses = std::count_if(source.steps.begin(), source.steps.end(),
                                           [&](const auto& candidate) {
                                               return candidate.operation.kind == K::Acquire &&
                                                      candidate.operation.index == step.operation.index;
                                           });
            if (uses > 1) move(id, finalState.atoms.at(id).position);
        }
        if (material.atoms.size() != 3)
            throw IR::Unsupported("shared workstation rejects surplus material");
        move(a, sharedEdge);
        move(b, core::Add(sharedEdge, {1, 0}));
        program.Append(p, material, {K::Bond, 0, {a, b}, 1});
        int orientation = 0;
        const auto delta = core::Subtract(last, middle);
        while (orientation < 6 && Turn(delta, orientation) != Coord{1, 0}) ++orientation;
        if (orientation == 6) throw IR::Unsupported("shared workstation edge is not adjacent");
        int firstOrientation = 0;
        while (firstOrientation < 6 &&
               Turn(core::Subtract(leaf, middle), firstOrientation) != Coord{-1, 0})
            ++firstOrientation;
        move(b, sharedEdge, (orientation - firstOrientation + 6) % 6);
        move(c, core::Add(sharedEdge, {1, 0}));
        program.Append(p, material, {K::Bond, 0, {b, c}, 1});
        move(b, outputs[0].ToWorld(middle), (turns - orientation + 6) % 6);
        program.Append(p, material, {K::Output, 0, {b}});
    }
    for (const auto& step : program.steps)
        if (step.operation.kind != K::Acquire && step.operation.kind != K::Relocate &&
            step.operation.kind != K::Bond && step.operation.kind != K::Unbond &&
            step.operation.kind != K::Output && step.operation.kind != K::Store &&
            step.operation.kind != K::Retrieve && step.operation.kind != K::Discard &&
            step.operation.kind != K::Calcify && step.operation.kind != K::Purify &&
            step.operation.kind != K::Project && step.operation.kind != K::Duplicate &&
            step.operation.kind != K::Animismus && step.operation.kind != K::Unify &&
            step.operation.kind != K::Disperse)
            throw IR::Unsupported("automatic rail material operation unsupported");
    std::unordered_set<Coord, core::HexCoordHash> feeds;
    for (std::size_t i = 0; i < inputs.size(); ++i)
        if (usedInput(i))
            for (const auto& atom : p.inputs[i].atoms)
                feeds.insert(inputs[i].ToWorld(atom.position));
    std::vector<std::pair<Coord, Coord>> cuts;
    for (const auto& step : program.steps)
        if (step.operation.kind == K::Unbond) {
            const auto a = step.preconditions.atoms.at(step.operation.atoms[0]).position;
            const auto b = step.preconditions.atoms.at(step.operation.atoms[1]).position;
            if (std::find(cuts.begin(), cuts.end(), std::pair{a, b}) == cuts.end() &&
                std::find(cuts.begin(), cuts.end(), std::pair{b, a}) == cuts.end())
                cuts.push_back({a, b});
        }
    std::vector<std::pair<Coord, Coord>> bonds, ordinaryBonds;
    struct Prism {
        Coord origin;
        int rotation;
        std::array<Coord, 3> vertices;
    };
    std::vector<Prism> prisms;
    std::vector<std::unordered_set<Coord, core::HexCoordHash>> products(outputs.size());
    for (std::size_t port = 0; port < outputs.size(); ++port) {
        for (const auto& b : p.outputs[port].bonds) {
            const auto a = outputs[port].ToWorld(b.from), z = outputs[port].ToWorld(b.to);
            bonds.push_back({a, z});
            int created = 0;
            for (const auto& step : program.steps) {
                if (step.operation.kind != K::Bond) continue;
                const auto x = step.preconditions.atoms.at(step.operation.atoms.at(0)).position;
                const auto y = step.preconditions.atoms.at(step.operation.atoms.at(1)).position;
                if ((x == a && y == z) || (x == z && y == a)) created |= step.operation.bits;
            }
            if (!created) continue;
            if (created == 1) {
                if (!(p.permissionBits & (1ULL << 8)))
                    throw IR::PermissionFailure(
                        8, "automatic rail requires ordinary-bonder permission for new bonds");
                ordinaryBonds.push_back({a, z});
                continue;
            }
            if (created & ~14)
                throw IR::Unsupported("automatic rail requires valid colored bond masks");
            if (!(p.permissionBits & (1ULL << 11)))
                throw IR::PermissionFailure(11,
                                            "automatic rail requires colored-bonder permission");
            for (int color : {2, 4, 8})
                if (created & color) {
                    bool placed = false;
                    for (int rotation = 0; rotation < 6 && !placed; ++rotation)
                        for (auto anchor : {a, z})
                            for (auto local : {Coord{0, 0}, Coord{0, 1}, Coord{1, 0}}) {
                                const auto origin = core::Subtract(
                                    anchor, core::HexFrame{{0, 0}, rotation}.ToWorld(local));
                                const core::HexFrame frame{origin, rotation};
                                const std::array<Coord, 3> vertices{frame.ToWorld({0, 0}),
                                                                    frame.ToWorld({0, 1}),
                                                                    frame.ToWorld({1, 0})};
                                const int colors[]{8, 2, 4};
                                for (int i = 0; i < 3; ++i)
                                    if (!placed && colors[i] == color &&
                                        ((vertices[i] == a && vertices[(i + 1) % 3] == z) ||
                                         (vertices[i] == z && vertices[(i + 1) % 3] == a))) {
                                        bool safe = true;
                                        for (int j = 0; j < 3; ++j) {
                                            const auto first = std::find_if(
                                                p.outputs[port].atoms.begin(),
                                                p.outputs[port].atoms.end(), [&](const auto& atom) {
                                                    return outputs[port].ToWorld(atom.position) ==
                                                           vertices[j];
                                                });
                                            const auto second = std::find_if(
                                                p.outputs[port].atoms.begin(),
                                                p.outputs[port].atoms.end(), [&](const auto& atom) {
                                                    return outputs[port].ToWorld(atom.position) ==
                                                           vertices[(j + 1) % 3];
                                                });
                                            if (first == p.outputs[port].atoms.end() ||
                                                second == p.outputs[port].atoms.end() ||
                                                first->type != core::AtomType::Fire ||
                                                second->type != core::AtomType::Fire)
                                                continue;
                                            const auto desired = std::find_if(
                                                p.outputs[port].bonds.begin(),
                                                p.outputs[port].bonds.end(), [&](const auto& bond) {
                                                    return (bond.from == first->position &&
                                                            bond.to == second->position) ||
                                                           (bond.to == first->position &&
                                                            bond.from == second->position);
                                                });
                                            if (desired == p.outputs[port].bonds.end() ||
                                                !(desired->bits & colors[j]))
                                                safe = false;
                                        }
                                        if (!safe) continue;
                                        prisms.push_back({origin, rotation, vertices});
                                        placed = true;
                                    }
                            }
                    if (!placed) throw IR::Unsupported("automatic colored edge is not adjacent");
                }
        }
        for (const auto& a : p.outputs[port].atoms)
            products[port].insert(outputs[port].ToWorld(a.position));
    }
    if (sharedWorkstation) {
        if (!(p.permissionBits & (1ULL << 8)))
            throw IR::PermissionFailure(8, "shared workstation requires ordinary bonder");
        bonds = {{sharedEdge, core::Add(sharedEdge, {1, 0})}};
        ordinaryBonds = bonds;
    }
    std::vector<Coord> rail;
    const int end = std::max({cursor, outputs.back().origin.q + bounds.back().maxQ,
                              disposals.empty() ? 0 : disposals.back().q,
                              calcifiers.empty() ? 0 : calcifiers.back().q,
                              purifiers.empty() ? 0 : purifiers.back().q + 1,
                              projectors.empty() ? 0 : projectors.back().q + 1,
                              duplicators.empty() ? 0 : duplicators.back().q + 1,
                              animismuses.empty() ? 0 : animismuses.back().q + 1,
                              unifiers.empty() ? 0 : unifiers.back().q + 2,
                              dispersers.empty() ? 0 : dispersers.back().q + 2});
    if (compact)
        rail = compact->rail;
    else
        for (int q = 0; q <= end; ++q)
            rail.push_back({q, 1});
    const HeldArmState start = compact ? compact->start : HeldArmState{0, 1, 4, 0};
    std::vector<std::vector<Coord>> feedCells(inputs.size());
    for (std::size_t input = 0; input < inputs.size(); ++input)
        if (usedInput(input))
            for (const auto& atom : p.inputs[input].atoms)
                feedCells[input].push_back(inputs[input].ToWorld(atom.position));
    auto certifiedSteps = program.steps;
    std::map<IR::AtomId, Coord> productOffsets;
    for (std::size_t i = 0; i < certifiedSteps.size(); ++i) {
        auto& step = certifiedSteps[i];
        auto correct = [&](auto& state) {
            for (const auto& [id, offset] : productOffsets)
                if (state.atoms.contains(id))
                    state.atoms.at(id).position = core::Add(state.atoms.at(id).position, offset);
        };
        correct(step.preconditions);
        if (step.operation.kind == K::Relocate)
            for (auto id : IR::Fragment(program.steps[i].preconditions, step.operation.atoms.at(0)))
                productOffsets.erase(id);
        if (releasedProducts.contains(i)) {
            const auto& cells = releasedProducts.at(i);
            for (std::size_t n = 0; n < cells.size(); ++n) {
                const auto id = step.preconditions.nextId + n;
                productOffsets[id] = core::Subtract(cells[n], step.effects.atoms.at(id).position);
            }
        } else if (compact &&
                   (step.operation.kind == K::Calcify || step.operation.kind == K::Project ||
                    step.operation.kind == K::Duplicate)) {
            const auto product = step.preconditions.nextId;
            productOffsets[product] =
                core::Subtract(step.preconditions.atoms.at(step.operation.atoms.front()).position,
                               step.effects.atoms.at(product).position);
            for (auto operand : step.operation.atoms)
                productOffsets.erase(operand);
        }
        correct(step.effects);
    }
    auto validGlyphs = [&](const IR::State& state) {
        for (const auto kind : {K::Unify, K::Disperse}) {
            for (const auto station : (kind == K::Unify ? unifiers : dispersers)) {
                bool active = false;
                if (kind == K::Disperse) {
                    active =
                        std::any_of(state.atoms.begin(), state.atoms.end(), [&](const auto& entry) {
                            return entry.second.position == station &&
                                   entry.second.type == core::AtomType::Quintessence;
                        });
                } else {
                    unsigned mask = 0;
                    for (auto offset : unifyContacts)
                        for (const auto& [id, atom] : state.atoms)
                            if (atom.position == core::Add(station, offset) &&
                                atom.type >= core::AtomType::Air &&
                                atom.type <= core::AtomType::Water)
                                mask |= 1U << (static_cast<int>(atom.type) - 2);
                    active = mask == 15;
                }
                if (active && !std::any_of(certifiedSteps.begin(), certifiedSteps.end(),
                                           [&](const auto& expected) {
                                               return expected.operation.kind == kind &&
                                                      expected.preconditions.atoms == state.atoms &&
                                                      expected.preconditions.bonds == state.bonds;
                                           }))
                    return false;
            }
        }
        for (const auto kind : {K::Project, K::Duplicate}) {
            const auto& stations = kind == K::Project ? projectors : duplicators;
            for (const auto station : stations) {
                const auto a =
                    std::find_if(state.atoms.begin(), state.atoms.end(), [&](const auto& entry) {
                        return entry.second.position == station;
                    });
                const auto b =
                    std::find_if(state.atoms.begin(), state.atoms.end(), [&](const auto& entry) {
                        return entry.second.position == core::Add(station, {1, 0});
                    });
                if (a == state.atoms.end() || b == state.atoms.end()) continue;
                const auto cardinal = [](auto type) {
                    return type == core::AtomType::Air || type == core::AtomType::Earth ||
                           type == core::AtomType::Fire || type == core::AtomType::Water;
                };
                const bool active =
                    kind == K::Project
                        ? (a->second.type >= core::AtomType::Silver &&
                           a->second.type <= core::AtomType::Lead &&
                           b->second.type == core::AtomType::Quicksilver)
                        : (a->second.type == core::AtomType::Salt && cardinal(b->second.type));
                if (active && !std::any_of(certifiedSteps.begin(), certifiedSteps.end(),
                                           [&](const auto& expected) {
                                               return expected.operation.kind == kind &&
                                                      expected.preconditions.atoms == state.atoms &&
                                                      expected.preconditions.bonds == state.bonds;
                                           }))
                    return false;
            }
        }
        for (const auto kind : {K::Purify, K::Animismus})
            for (const auto station : (kind == K::Purify ? purifiers : animismuses)) {
                const auto a =
                    std::find_if(state.atoms.begin(), state.atoms.end(), [&](const auto& entry) {
                        return entry.second.position == station;
                    });
                const auto b =
                    std::find_if(state.atoms.begin(), state.atoms.end(), [&](const auto& entry) {
                        return entry.second.position == core::Add(station, {1, 0});
                    });
                if (a == state.atoms.end() || b == state.atoms.end() ||
                    a->second.type != b->second.type)
                    continue;
                if (kind == K::Purify ? (a->second.type < core::AtomType::Silver ||
                                         a->second.type > core::AtomType::Lead)
                                      : a->second.type != core::AtomType::Salt)
                    continue;
                if (!std::any_of(certifiedSteps.begin(), certifiedSteps.end(),
                                 [&](const auto& expected) {
                                     return expected.operation.kind == kind &&
                                            expected.preconditions.atoms == state.atoms &&
                                            expected.preconditions.bonds == state.bonds;
                                 }))
                    return false;
            }
        for (const auto& [id, atom] : state.atoms) {
            if (std::find(disposals.begin(), disposals.end(), atom.position) != disposals.end() &&
                !std::any_of(certifiedSteps.begin(), certifiedSteps.end(),
                             [&](const auto& expected) {
                                 return expected.operation.kind == K::Discard &&
                                        expected.operation.atoms.at(0) == id &&
                                        expected.preconditions.atoms == state.atoms &&
                                        expected.preconditions.bonds == state.bonds;
                             }))
                return false;
            if (atom.type != core::AtomType::Air && atom.type != core::AtomType::Earth &&
                atom.type != core::AtomType::Fire && atom.type != core::AtomType::Water)
                continue;
            if (std::find(calcifiers.begin(), calcifiers.end(), atom.position) == calcifiers.end())
                continue;
            if (!std::any_of(certifiedSteps.begin(), certifiedSteps.end(),
                             [&](const auto& expected) {
                                 return expected.operation.kind == K::Calcify &&
                                        expected.operation.atoms.at(0) == id &&
                                        expected.preconditions.atoms == state.atoms &&
                                        expected.preconditions.bonds == state.bonds;
                             }))
                return false;
        }
        for (const auto& [a, b] : cuts) {
            const auto x = std::find_if(state.atoms.begin(), state.atoms.end(),
                                        [&](const auto& e) { return e.second.position == a; });
            const auto y = std::find_if(state.atoms.begin(), state.atoms.end(),
                                        [&](const auto& e) { return e.second.position == b; });
            if (x == state.atoms.end() || y == state.atoms.end()) continue;
            const IR::Edge edge{std::min(x->first, y->first), std::max(x->first, y->first)};
            if (state.bonds.contains(edge) &&
                !std::any_of(certifiedSteps.begin(), certifiedSteps.end(),
                             [&](const auto& expected) {
                                 return expected.operation.kind == K::Unbond &&
                                        expected.preconditions.atoms == state.atoms &&
                                        expected.preconditions.bonds == state.bonds;
                             }))
                return false;
        }
        auto certifiedBond = [&](const IR::Edge& edge, int color) {
            auto atoms = state.atoms;
            auto currentBonds = state.bonds;
            for (const auto& expected : certifiedSteps) {
                if (expected.operation.kind != K::Bond || expected.preconditions.atoms != atoms ||
                    expected.preconditions.bonds != currentBonds)
                    continue;
                if ((expected.operation.bits & color) && expected.operation.atoms.size() == 2 &&
                    std::min(expected.operation.atoms[0], expected.operation.atoms[1]) == edge.a &&
                    std::max(expected.operation.atoms[0], expected.operation.atoms[1]) == edge.b)
                    return true;
                atoms = expected.effects.atoms;
                currentBonds = expected.effects.bonds;
            }
            return false;
        };
        for (const auto& prism : prisms)
            for (int i = 0; i < 3; ++i) {
                const auto x =
                    std::find_if(state.atoms.begin(), state.atoms.end(), [&](const auto& e) {
                        return e.second.position == prism.vertices[i];
                    });
                const auto y =
                    std::find_if(state.atoms.begin(), state.atoms.end(), [&](const auto& e) {
                        return e.second.position == prism.vertices[(i + 1) % 3];
                    });
                if (x == state.atoms.end() || y == state.atoms.end() ||
                    x->second.type != core::AtomType::Fire ||
                    y->second.type != core::AtomType::Fire)
                    continue;
                const IR::Edge edge{std::min(x->first, y->first), std::max(x->first, y->first)};
                const int colors[]{8, 2, 4};
                const int old = state.bonds.contains(edge) ? state.bonds.at(edge) : 0;
                if (old == 1 || (old & colors[i])) continue;
                if (!certifiedBond(edge, colors[i])) return false;
            }
        for (const auto& [a, b] : bonds) {
            const auto x = std::find_if(state.atoms.begin(), state.atoms.end(),
                                        [&](const auto& e) { return e.second.position == a; });
            const auto y = std::find_if(state.atoms.begin(), state.atoms.end(),
                                        [&](const auto& e) { return e.second.position == b; });
            if (x == state.atoms.end() || y == state.atoms.end()) continue;
            const IR::Edge edge{std::min(x->first, y->first), std::max(x->first, y->first)};
            if (!state.bonds.contains(edge) && !certifiedBond(edge, 15)) return false;
            if (sharedWorkstation) continue; // Exact ID/bond certificate above also covers the station.
            // Only final product atoms may encounter the permanently active bonders.
            for (auto it : {x, y}) {
                bool matches = false;
                for (std::size_t port = 0; port < outputs.size(); ++port)
                    matches |= std::any_of(p.outputs[port].atoms.begin(),
                                           p.outputs[port].atoms.end(), [&](const auto& atom) {
                                               return outputs[port].ToWorld(atom.position) ==
                                                          it->second.position &&
                                                      atom.type == it->second.type;
                                           });
                if (!matches) return false;
            }
        }
        return true;
    };
    PhysicalPlan railPlan;
    int previousTapeLength = 0;
    auto inventory = LowerInventoryIR(
        p, program, rail, start,
        [&](const auto& world, const auto& from, const auto& to, char op, bool holding) {
            if (compact) {
                if (holding && op != 'G' && op != 'g')
                    for (const auto& [a, b] : bonds) {
                        const auto first = std::find_if(
                            world.atoms.begin(), world.atoms.end(),
                            [&](const auto& entry) { return entry.second.position == a; });
                        const auto second = std::find_if(
                            world.atoms.begin(), world.atoms.end(),
                            [&](const auto& entry) { return entry.second.position == b; });
                        if (first != world.atoms.end() && second != world.atoms.end() &&
                            !world.bonds.contains({std::min(first->first, second->first),
                                                   std::max(first->first, second->first)}))
                            return false;
                    }
                const bool pivot = op == 'P' || op == 'p';
                if (pivot && (!holding || !(p.permissionBits & (1ULL << 26)))) return false;
                IR::State next = world;
                if (!CompactMotion(*compact, world, from, to, op, holding, feedCells, next) ||
                    (holding && !validGlyphs(next)))
                    return false;
                if (op == 'g')
                    for (std::size_t port = 0; port < products.size(); ++port) {
                        const bool complete = std::all_of(
                            products[port].begin(), products[port].end(), [&](Coord cell) {
                                return std::any_of(next.atoms.begin(), next.atoms.end(),
                                                   [&](const auto& entry) {
                                                       return entry.second.position == cell;
                                                   });
                            });
                        if (complete &&
                            std::none_of(certifiedSteps.begin(), certifiedSteps.end(),
                                         [&](const auto& expected) {
                                             if (expected.operation.kind != K::Output ||
                                                 expected.operation.index != port)
                                                 return false;
                                             const auto fragment =
                                                 IR::Fragment(expected.preconditions,
                                                              expected.operation.atoms.at(0));
                                             for (auto id : fragment) {
                                                 if (!next.atoms.contains(id) ||
                                                     next.atoms.at(id) !=
                                                         expected.preconditions.atoms.at(id))
                                                     return false;
                                             }
                                             for (const auto& [edge, bits] :
                                                  expected.preconditions.bonds)
                                                 if (std::binary_search(fragment.begin(),
                                                                        fragment.end(), edge.a) &&
                                                     (!next.bonds.contains(edge) ||
                                                      next.bonds.at(edge) != bits))
                                                     return false;
                                             return true;
                                         }))
                            return false;
                    }
                return true;
            }
            const bool pivot = op == 'P' || op == 'p';
            if (from.direction != 4 || to.direction != 4 ||
                (op != 'A' && op != 'a' && op != 'G' && op != 'g' && op != 'E' && op != 'e' &&
                 !pivot))
                return false;
            if (pivot && (!holding || !(p.permissionBits & (1ULL << 26)))) return false;
            const auto a = Tip(rail, from), b = Tip(rail, to);
            auto next = world;
            std::vector<IR::AtomId> moving;
            if (holding) {
                const auto grip =
                    std::find_if(world.atoms.begin(), world.atoms.end(),
                                 [&](const auto& e) { return e.second.position == a; });
                if (grip == world.atoms.end()) return false;
                moving = IR::Fragment(world, grip->first);
                const int turns = (to.fragmentRotation - from.fragmentRotation + 6) % 6;
                const core::HexFrame turn{{0, 0}, turns};
                int radius = 0;
                for (auto id : moving) {
                    const auto relative = core::Subtract(world.atoms.at(id).position, a);
                    radius = std::max(radius, core::Distance({}, relative));
                    next.atoms.at(id).position = core::Add(b, turn.ToWorld(relative));
                }
                if (pivot) {
                    // A clear enclosing disk certifies the entire rotational sweep,
                    // not only its endpoints. Tight rotations remain unsupported.
                    if (core::Distance(a, rail.at(from.trackIndex)) <= radius) return false;
                    if (footprint)
                        for (int q = -radius - 1; q <= radius + 1; ++q)
                            for (int r = -radius - 1; r <= radius + 1; ++r)
                                if (core::Distance({}, {q, r}) <= radius + 1)
                                    footprint->insert(core::Add(a, {q, r}));
                    for (const auto& [id, atom] : world.atoms)
                        if (std::find(moving.begin(), moving.end(), id) == moving.end() &&
                            core::Distance(a, atom.position) <= radius + 1)
                            return false;
                    for (const auto feed : feeds)
                        if (core::Distance(a, feed) <= radius + 1) return false;
                }
            }
            for (const auto& [id, atom] : next.atoms) {
                if (atom.position == rail.at(to.trackIndex)) return false;
                if (std::find(moving.begin(), moving.end(), id) == moving.end() &&
                    core::Distance(rail.at(from.trackIndex), atom.position) +
                            core::Distance(atom.position, rail.at(to.trackIndex)) ==
                        core::Distance(rail.at(from.trackIndex), rail.at(to.trackIndex)))
                    return false;
            }
            for (const auto& [id, atom] : world.atoms) {
                if (std::find(moving.begin(), moving.end(), id) != moving.end()) continue;
                if ((op == 'E' || op == 'e') &&
                    core::Distance(a, atom.position) + core::Distance(atom.position, b) ==
                        core::Distance(a, b))
                    return false;
                for (auto moved : moving)
                    if (next.atoms.at(moved).position == atom.position) return false;
            }
            // Residual stock suppresses input respawn, leaving its vacant cells traversable.
            const auto blockedFeed = [&](Coord cell) {
                for (std::size_t input = 0; input < inputs.size(); ++input) {
                    if (!usedInput(input)) continue;
                    bool contains = false, blocked = false;
                    for (const auto& atom : p.inputs[input].atoms) {
                        const auto position = inputs[input].ToWorld(atom.position);
                        contains |= position == cell;
                        blocked |= std::any_of(
                            world.atoms.begin(), world.atoms.end(),
                            [&](const auto& entry) { return entry.second.position == position; });
                    }
                    if (contains && blocked) return true;
                }
                return false;
            };
            for (auto id : moving)
                if (a != b && feeds.contains(next.atoms.at(id).position) &&
                    !blockedFeed(next.atoms.at(id).position))
                    return false;
            if ((op == 'E' || op == 'e') && a != b)
                for (auto feed : feeds)
                    if (feed != a && !blockedFeed(feed) &&
                        core::Distance(a, feed) + core::Distance(feed, b) == core::Distance(a, b))
                        return false;
            // Parking on an output is legal only for an incomplete molecule.
            if (op == 'g')
                for (std::size_t port = 0; port < products.size(); ++port) {
                    bool complete = true;
                    for (auto cell : products[port])
                        complete &=
                            std::any_of(next.atoms.begin(), next.atoms.end(),
                                        [&](const auto& e) { return e.second.position == cell; });
                    if (complete &&
                        std::none_of(
                            certifiedSteps.begin(), certifiedSteps.end(), [&](const auto& s) {
                                if (s.operation.kind != K::Output || s.operation.index != port)
                                    return false;
                                const auto fragment =
                                    IR::Fragment(s.preconditions, s.operation.atoms.at(0));
                                for (auto id : fragment) {
                                    const auto actual = next.atoms.find(id);
                                    if (actual == next.atoms.end() ||
                                        actual->second != s.preconditions.atoms.at(id))
                                        return false;
                                }
                                for (const auto& [edge, bits] : next.bonds) {
                                    if (std::find(fragment.begin(), fragment.end(), edge.a) ==
                                            fragment.end() &&
                                        std::find(fragment.begin(), fragment.end(), edge.b) ==
                                            fragment.end())
                                        continue;
                                    const auto expected = s.preconditions.bonds.find(edge);
                                    if (expected == s.preconditions.bonds.end() ||
                                        expected->second != bits)
                                        return false;
                                }
                                for (const auto& [edge, bits] : s.preconditions.bonds) {
                                    if (std::find(fragment.begin(), fragment.end(), edge.a) ==
                                        fragment.end())
                                        continue;
                                    const auto actual = next.bonds.find(edge);
                                    if (actual == next.bonds.end() || actual->second != bits)
                                        return false;
                                }
                                return true;
                            }))
                        return false;
                }
            if (footprint) {
                footprint->insert(rail.at(from.trackIndex));
                footprint->insert(rail.at(to.trackIndex));
                for (const auto& [id, atom] : world.atoms)
                    footprint->insert(atom.position);
                for (const auto& [id, atom] : next.atoms)
                    footprint->insert(atom.position);
            }
            return validGlyphs(next);
        },
        [&](const auto& step, const auto&) {
            if (step.operation.kind == K::Acquire) {
                const auto match = std::find_if(
                    certifiedSteps.begin(), certifiedSteps.end(), [&](const auto& expected) {
                        return expected.operation.kind == K::Acquire &&
                               expected.effects.atoms == step.effects.atoms &&
                               expected.effects.bonds == step.effects.bonds;
                    });
                if (match != certifiedSteps.end()) return true;
                if (compact) {
                    for (const auto& expected : certifiedSteps) {
                        if (expected.operation.kind != K::Acquire ||
                            expected.effects.nextId != step.effects.nextId)
                            continue;
                        for (const auto& [id, atom] : expected.effects.atoms) {
                            const auto actual = step.effects.atoms.find(id);
                            if (actual != step.effects.atoms.end() && actual->second != atom)
                                throw IR::TimingFailure(
                                    "compact spawn mismatch atom " + std::to_string(id) +
                                    " expected (" + std::to_string(atom.position.q) + "," +
                                    std::to_string(atom.position.r) + ") actual (" +
                                    std::to_string(actual->second.position.q) + "," +
                                    std::to_string(actual->second.position.r) + ")");
                        }
                    }
                }
                return false;
            }
            return validGlyphs(step.effects) &&
                   std::any_of(
                       certifiedSteps.begin(), certifiedSteps.end(), [&](const auto& expected) {
                           auto effects = expected.effects;
                           if (expected.operation.kind == K::Calcify ||
                               expected.operation.kind == K::Project ||
                               expected.operation.kind == K::Duplicate)
                               effects.atoms.at(expected.preconditions.nextId).position =
                                   expected.preconditions.atoms.at(expected.operation.atoms.at(0))
                                       .position;
                           return expected.operation.kind == step.operation.kind &&
                                  expected.operation.index == step.operation.index &&
                                  expected.preconditions.atoms == step.preconditions.atoms &&
                                  expected.preconditions.bonds == step.preconditions.bonds &&
                                  effects.atoms == step.effects.atoms &&
                                  effects.bonds == step.effects.bonds;
                       });
        },
        reach, 3000,
        std::vector<std::size_t>(outputs.size(),
                                 joint ? joint->productsPerPeriod : supply.productsPerPeriod),
        releasedProducts,
        [&](std::size_t task, const PhysicalInventory& committed) {
            const auto& op = program.steps.at(task).operation;
            std::optional<std::size_t> port;
            if (op.kind == K::Acquire || op.kind == K::Output) port = op.index;
            std::optional<IR::AtomId> held;
            if (auto it = committed.Custody().Held().find(0);
                it != committed.Custody().Held().end())
                held = it->second;
            const int end = static_cast<int>(committed.Tape().size());
            railPlan.assignments.push_back({task, PhysicalPlan::Mode::Rail, std::nullopt, 0, port,
                                            held, Tip(rail, committed.Arm()),
                                            committed.Arm().fragmentRotation, previousTapeLength,
                                            end});
            previousTapeLength = end;
        },
        compact != nullptr);
    if (inventory.MoveEmpty(start, 3000) != MotionStatus::Found)
        throw IR::Unsupported("automatic rail cannot close period");
    const auto execution = MakeExecutionPlan(p, program);
    ValidatePhysicalPlan(execution, railPlan);
    std::vector<std::size_t> delivered(outputs.size());
    for (const auto& task : execution.tasks)
        if (task.output) ++delivered.at(*task.output);
    if (delivered != std::vector<std::size_t>(outputs.size(), joint ? joint->productsPerPeriod
                                                                    : supply.productsPerPeriod))
        throw IR::Error("physical execution plan output period mismatch");
    io::SolutionData result;
    result.puzzleName = p.name;
    result.solutionName = "Automatic supply inventory";
    io::SolutionObject arm;
    arm.typeName = reach > 1 ? "piston" : "arm1";
    arm.position = rail[0];
    int time = 0;
    for (char op : inventory.Tape())
        arm.instructions.push_back({op, time++});
    const auto productsPerPeriod = joint ? joint->productsPerPeriod : supply.productsPerPeriod;
    const auto requiredProducts = 6LL * p.outputMultiplier;
    if (productsPerPeriod == 0 || requiredProducts <= 0 ||
        static_cast<std::int64_t>(time) *
                ((requiredProducts + productsPerPeriod - 1) / productsPerPeriod) >
            std::numeric_limits<std::int32_t>::max())
        throw IR::Unsupported("automatic inventory completion exceeds cycle range");
    arm.bodyData = {start.length, start.direction, 0, time, 0};
    result.objects.push_back(arm);
    io::SolutionObject track;
    track.typeName = "track";
    track.position = rail[0];
    track.bodyData = {1, 0, 0, 0, static_cast<int>(rail.size())};
    for (auto c : rail) {
        const auto local = core::Subtract(c, rail.front());
        track.bodyData.push_back(local.q);
        track.bodyData.push_back(local.r);
    }
    track.bodyData.push_back(0);
    result.objects.push_back(track);
    for (std::size_t i = 0; i < inputs.size(); ++i)
        if (usedInput(i))
            result.objects.push_back(
                {"input", inputs[i].origin, {}, {1, inputs[i].Turns(), static_cast<int>(i), 0, 0}});
    for (const auto c : disposals)
        result.objects.push_back({"glyph-disposal", c, {}, {1, 0, 0, 0, 0}});
    for (const auto c : calcifiers)
        result.objects.push_back({"glyph-calcification", c, {}, {1, 0, 0, 0, 0}});
    for (const auto c : purifiers)
        result.objects.push_back({"glyph-purification", c, {}, {1, 0, 0, 0, 0}});
    for (const auto c : animismuses)
        result.objects.push_back({"glyph-life-and-death", c, {}, {1, 0, 0, 0, 0}});
    for (const auto c : unifiers)
        result.objects.push_back({"glyph-unification", c, {}, {1, 0, 0, 0, 0}});
    for (const auto c : dispersers)
        result.objects.push_back({"glyph-dispersion", c, {}, {1, 0, 0, 0, 0}});
    for (const auto c : projectors)
        result.objects.push_back({"glyph-projection", core::Add(c, {1, 0}), {}, {1, 3, 0, 0, 0}});
    for (const auto c : duplicators)
        result.objects.push_back({"glyph-duplication", core::Add(c, {1, 0}), {}, {1, 3, 0, 0, 0}});
    if (!cuts.empty() && !(p.permissionBits & (1ULL << 9)))
        throw IR::PermissionFailure(9, "automatic rail requires unbonder permission for cuts");
    for (auto [a, b] : cuts) {
        int direction = 0;
        Coord delta{1, 0};
        while (direction < 6 && core::Add(a, delta) != b) {
            delta = core::RotateClockwise(delta);
            ++direction;
        }
        result.objects.push_back({"unbonder", a, {}, {1, direction, 0, 0, 0}});
    }
    for (const auto& prism : prisms)
        result.objects.push_back({"bonder-prisma", prism.origin, {}, {1, prism.rotation, 0, 0, 0}});
    for (auto [a, b] : ordinaryBonds) {
        int direction = 0;
        Coord delta{1, 0};
        while (direction < 6 && core::Add(a, delta) != b) {
            delta = core::RotateClockwise(delta);
            ++direction;
        }
        result.objects.push_back({"bonder", a, {}, {1, direction, 0, 0, 0}});
    }
    for (std::size_t port = 0; port < outputs.size(); ++port)
        result.objects.push_back({"out-std",
                                  outputs[port].origin,
                                  {},
                                  {1, outputs[port].Turns(), static_cast<int>(port), 0, 0}});
    if (compact) {
        result.solutionName = "Compact chamber supply inventory";
        std::set<std::tuple<std::string, int, int, int>> seen;
        std::erase_if(result.objects, [&](const auto& object) {
            if (!object.typeName.starts_with("glyph-") && !object.typeName.starts_with("bonder") &&
                object.typeName != "unbonder")
                return false;
            return !seen.emplace(object.typeName, object.position.q, object.position.r,
                                 object.bodyData[1])
                        .second;
        });
    }
    return result;
}
std::optional<io::SolutionData> CompileCompact(
    const io::PuzzleData& original, const io::PuzzleData& materialPuzzle,
    const AssemblyPlan& assembly, const SupplyPlan& supply, const JointSupplyPlan* joint,
    std::chrono::steady_clock::time_point deadline,
    const std::function<bool(const io::SolutionData&)>& accept,
    const std::function<void(const std::string&)>& feedback) {
    constexpr auto permissions = (1ULL << 0) | (1ULL << 2) | (1ULL << 3) | (1ULL << 8) |
                                 (1ULL << 9) | (1ULL << 22) | (1ULL << 23);
    if (!original.productionInfo || original.productionInfo->chambers.size() != 1 ||
        !original.productionInfo->conduits.empty() || original.productionInfo->isolation ||
        original.outputs.size() != 1 || original.inputs.size() > 4 ||
        (original.permissionBits & permissions) != permissions)
        return {};
    for (const auto* list : {&original.inputs, &original.outputs})
        for (const auto& molecule : *list)
            for (const auto& bond : molecule.bonds)
                if (bond.bits != 1) return {};
    const auto source = joint ? BuildJointSupplyExecutionIR(materialPuzzle, *joint)
                              : BuildSupplyExecutionIR(materialPuzzle, assembly, supply);
    std::set<K> reactions;
    for (const auto& step : source.steps) {
        if (step.operation.kind == K::Calcify || step.operation.kind == K::Project)
            reactions.insert(step.operation.kind);
        else if (step.operation.kind == K::Discard || step.operation.kind == K::Purify ||
                 step.operation.kind == K::Duplicate || step.operation.kind == K::Animismus ||
                 step.operation.kind == K::Unify || step.operation.kind == K::Disperse)
            return {};
    }
    std::vector<std::size_t> deliveryRank(original.outputs[0].atoms.size(), 1);
    std::size_t delivered = 0;
    for (const auto& step : source.steps) {
        if (step.operation.kind == K::Output) break;
        if (step.operation.kind != K::Relocate) continue;
        const auto& atom = step.effects.atoms.at(step.operation.atoms.front());
        for (std::size_t i = 0; i < original.outputs[0].atoms.size(); ++i)
            if (atom.position == original.outputs[0].atoms[i].position &&
                atom.type == original.outputs[0].atoms[i].type)
                deliveryRank[i] = ++delivered;
    }
    const auto chambers = PlanProductionChambers(original);
    CompactLayout layout;
    layout.inputs.resize(original.inputs.size());
    layout.outputs.resize(1);
    layout.cells = chambers[0].interior;
    layout.deadline = deadline;
    layout.minQ = layout.maxQ = layout.cells.front().q;
    layout.minR = layout.maxR = layout.cells.front().r;
    layout.minSum = layout.maxSum = layout.cells.front().q + layout.cells.front().r;
    for (auto cell : layout.cells) {
        layout.minQ = std::min(layout.minQ, cell.q);
        layout.maxQ = std::max(layout.maxQ, cell.q);
        layout.minR = std::min(layout.minR, cell.r);
        layout.maxR = std::max(layout.maxR, cell.r);
        layout.minSum = std::min(layout.minSum, cell.q + cell.r);
        layout.maxSum = std::max(layout.maxSum, cell.q + cell.r);
    }
    const auto contains = [&](Coord cell) { return ContainsProductionCell(chambers[0], cell); };
    int lowR = layout.cells.front().r, highR = lowR;
    for (auto cell : layout.cells) {
        lowR = std::min(lowR, cell.r);
        highR = std::max(highR, cell.r);
    }
    std::vector<int> rows{highR - 1, lowR + 1};
    for (int row = lowR; row <= highR; ++row)
        if (std::find(rows.begin(), rows.end(), row) == rows.end()) rows.push_back(row);
    unsigned attempts = 0;
    unsigned searchSeed = 0;
    std::optional<io::SolutionData> result;
    for (int row : rows) {
        layout.rail.clear();
        for (auto cell : layout.cells)
            if (cell.r == row) layout.rail.push_back(cell);
        std::sort(layout.rail.begin(), layout.rail.end(),
                  [](Coord a, Coord b) { return a.q < b.q; });
        if (layout.rail.size() < 3) continue;
        layout.start = {0, 1, 4, 0};
        if (!contains(Tip(layout.rail, layout.start))) layout.start.direction = 1;
        if (!contains(Tip(layout.rail, layout.start))) continue;
        std::set<std::pair<int, int>> occupied;
        for (auto cell : layout.rail)
            occupied.emplace(cell.q, cell.r);
        const auto frames = [&](const io::Molecule& molecule, bool output) {
            std::vector<core::HexFrame> domain;
            for (auto anchor : layout.cells)
                for (int turns = 0; turns < 6; ++turns) {
                    const core::HexFrame frame{
                        core::Subtract(anchor, Turn(molecule.atoms.front().position, turns)),
                        turns};
                    bool fits = true;
                    for (const auto& atom : molecule.atoms) {
                        const auto cell = frame.ToWorld(atom.position);
                        fits &= contains(cell) && !occupied.contains({cell.q, cell.r});
                    }
                    if (fits) domain.push_back(frame);
                }
            const auto score = [&](const core::HexFrame& frame) {
                double value = 0;
                for (std::size_t i = 0; i < molecule.atoms.size(); ++i) {
                    const auto cell = frame.ToWorld(molecule.atoms[i].position);
                    value +=
                        output ? deliveryRank[i] * std::abs(cell.r - row) : -std::abs(cell.r - row);
                    value += .001 * cell.q;
                }
                return value;
            };
            std::stable_sort(domain.begin(), domain.end(),
                             [&](const auto& a, const auto& b) { return score(a) < score(b); });
            return domain;
        };
        const auto outputDomain = frames(original.outputs[0], true);
        std::function<bool(std::size_t)> place;
        place = [&](std::size_t module) {
            if (attempts >= 48 || std::chrono::steady_clock::now() >= deadline) return true;
            const std::size_t ports = original.inputs.size() + 1;
            if (module < ports) {
                const bool output = module == 0;
                const auto& molecule = output ? original.outputs[0] : original.inputs[module - 1];
                auto domain = output ? outputDomain : frames(molecule, false);
                if (!domain.empty()) {
                    const std::size_t shift =
                        (searchSeed * (module * 37 + 17) + module * 11) % domain.size();
                    std::rotate(domain.begin(), domain.begin() + shift, domain.end());
                }
                unsigned branches = 0;
                for (const auto& frame : domain) {
                    if (branches++ >= 2) break;
                    std::vector<std::pair<int, int>> inserted;
                    bool clear = true;
                    for (const auto& atom : molecule.atoms) {
                        const auto cell = frame.ToWorld(atom.position);
                        if (occupied.contains({cell.q, cell.r})) {
                            clear = false;
                            break;
                        }
                        inserted.emplace_back(cell.q, cell.r);
                    }
                    if (!clear) continue;
                    for (auto cell : inserted)
                        occupied.insert(cell);
                    (output ? layout.outputs[0] : layout.inputs[module - 1]) = frame;
                    const bool done = place(module + 1);
                    for (auto cell : inserted)
                        occupied.erase(cell);
                    if (done) return true;
                }
                return false;
            }
            const std::vector<K> kinds(reactions.begin(), reactions.end());
            if (module < ports + kinds.size()) {
                const auto kind = kinds[module - ports];
                auto domain = layout.cells;
                std::stable_sort(domain.begin(), domain.end(), [&](Coord a, Coord b) {
                    return std::abs(a.r - row) < std::abs(b.r - row);
                });
                if (!domain.empty()) {
                    const std::size_t shift = (searchSeed * (module * 19 + 7)) % domain.size();
                    std::rotate(domain.begin(), domain.begin() + shift, domain.end());
                }
                unsigned branches = 0;
                for (auto cell : domain) {
                    const auto partner = core::Add(cell, {1, 0});
                    if (occupied.contains({cell.q, cell.r}) ||
                        (kind == K::Project &&
                         (!contains(partner) || occupied.contains({partner.q, partner.r}))))
                        continue;
                    if (branches++ >= 1) break;
                    occupied.emplace(cell.q, cell.r);
                    if (kind == K::Project) occupied.emplace(partner.q, partner.r);
                    layout.stations[kind] = cell;
                    const bool done = place(module + 1);
                    occupied.erase({cell.q, cell.r});
                    if (kind == K::Project) occupied.erase({partner.q, partner.r});
                    if (done) return true;
                }
                return false;
            }
            layout.buffers.clear();
            for (auto cell : layout.cells)
                if (!occupied.contains({cell.q, cell.r})) layout.buffers.push_back(cell);
            ++attempts;
            std::optional<io::SolutionData> candidate;
            try {
                candidate =
                    Compile(materialPuzzle, assembly, supply, 0, 1, nullptr, joint, false, &layout);
            } catch (const IR::Error& error) {
                if (feedback) feedback(error.what());
            }
            if (candidate && accept(*candidate)) {
                result = std::move(*candidate);
                return true;
            }
            return false;
        };
        bool finished = false;
        for (searchSeed = 0; searchSeed < 8 && !finished; ++searchSeed)
            finished = place(0);
        if (finished) break;
    }
    return result;
}
std::optional<io::SolutionData> PlaceInChamber(
    const io::PuzzleData& puzzle, const io::SolutionData& local,
    const std::unordered_set<Coord, core::HexCoordHash>& footprint,
    const std::function<bool(const io::SolutionData&)>& accept) {
    const auto& info = *puzzle.productionInfo;
    if (info.chambers.empty())
        throw IR::Unsupported("automatic inventory requires production chambers");
    const auto chambers = PlanProductionChambers(puzzle);
    // Include the entire piston sweep rectangle, not only its end poses.
    int lowQ = local.objects.front().position.q, highQ = lowQ, lowR = 1, highR = 1;
    for (const auto& object : local.objects) {
        lowQ = std::min(lowQ, object.position.q);
        highQ = std::max(highQ, object.position.q);
        lowR = std::min(lowR, object.position.r);
        highR = std::max(highR, object.position.r);
        if (object.typeName == "track")
            for (int i = 0; i < object.bodyData[4]; ++i)
                highQ = std::max(highQ, object.position.q + object.bodyData[5 + 2 * i]);
        if (object.typeName == "input") {
            const core::HexFrame frame{object.position, object.bodyData[1]};
            for (const auto& atom : puzzle.inputs[object.bodyData[2]].atoms) {
                const auto cell = frame.ToWorld(atom.position);
                lowQ = std::min(lowQ, cell.q);
                highQ = std::max(highQ, cell.q);
                lowR = std::min(lowR, cell.r);
                highR = std::max(highR, cell.r);
            }
        }
    }
    for (const auto cell : footprint) {
        lowQ = std::min(lowQ, cell.q);
        highQ = std::max(highQ, cell.q);
        lowR = std::min(lowR, cell.r);
        highR = std::max(highR, cell.r);
    }
    // Include the complete rail/piston sweep even when the period uses less reach.
    lowR = std::min(lowR, -2);
    unsigned attempts = 0;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(100);
    for (const auto& chamber : chambers)
        for (const auto anchor : chamber.interior) {
            if (attempts >= 16 || std::chrono::steady_clock::now() >= deadline) return std::nullopt;
            const auto delta = core::Subtract(anchor, {lowQ, lowR});
            bool fits = true;
            for (int q = lowQ; q <= highQ; ++q)
                for (int r = lowR; r <= highR; ++r)
                    fits &= ContainsProductionCell(chamber, core::Add({q, r}, delta));
            if (!fits) continue;
            auto result = local;
            result.solutionName = "Automatic chamber supply inventory";
            for (auto& object : result.objects)
                object.position = core::Add(object.position, delta);
            ++attempts;
            if (accept(result)) return result;
        }
    return std::nullopt;
}
} // namespace
std::optional<io::SolutionData> CompileSharedWorkstation(const io::PuzzleData& p) {
    try {
        if (p.outputs.size() != 1 || p.outputs[0].atoms.size() != 3 ||
            p.outputs[0].bonds.size() != 2) return std::nullopt;
        constexpr auto required = (1ULL << 2) | (1ULL << 3) | (1ULL << 8) |
                                  (1ULL << 22) | (1ULL << 23) | (1ULL << 26);
        if ((p.permissionBits & required) != required) return std::nullopt;
        const auto assembly = AssemblyPlanner::Plan(p);
        const auto supply = AssemblyPlanner::PlanSupply(p);
        return Compile(p, assembly, supply, 0, 1, nullptr, nullptr, false, nullptr, true);
    } catch (const std::invalid_argument& error) {
        GetLoggerOrFallback("solver")->info("Shared workstation rejected: {}", error.what());
        return std::nullopt;
    } catch (const std::runtime_error& error) {
        GetLoggerOrFallback("solver")->info("Shared workstation rejected: {}", error.what());
        return std::nullopt;
    }
}
std::optional<io::SolutionData> CompileAcceptedSupplyInventory(
    const io::PuzzleData& p, const std::function<bool(const io::SolutionData&)>& accept,
    const std::function<void(const SupplyFailure&)>& feedback) {
    std::size_t materialAttempt = 0;
    std::size_t productsPerPeriod = 0;
    auto report = [&](SupplyFailureCategory category, int rotation, int spacing,
                      const std::string& detail,
                      std::optional<int> missingPermission = std::nullopt) {
        if (feedback)
            feedback({.category = category,
                      .rotation = rotation,
                      .spacing = spacing,
                      .detail = detail,
                      .materialAttempt = materialAttempt,
                      .productsPerPeriod = productsPerPeriod,
                      .verification = std::nullopt,
                      .missingPermission = missingPermission});
    };
    auto rejectMaterial = [&](const char* detail) {
        report(SupplyFailureCategory::Material, -1, 0, detail);
        throw std::invalid_argument(detail);
    };
    constexpr auto required = (1ULL << 0) | (1ULL << 3) | (1ULL << 22) | (1ULL << 23);
    if (p.version != 3 || p.outputs.empty() || p.outputs.size() > 8 || p.inputs.empty() ||
        p.inputs.size() > 8)
        rejectMaterial("automatic inventory requires bounded finite supply");
    if ((p.permissionBits & required) != required) {
        for (const int bit : {0, 3, 22, 23})
            if (!(p.permissionBits & (1ULL << bit)))
                report(SupplyFailureCategory::Material, -1, 0,
                       "automatic inventory requires arm/track/grab permission (bit " +
                           std::to_string(bit) + ")",
                       bit);
        throw std::invalid_argument("automatic inventory requires arm/track/grab permissions");
    }
    for (const auto& input : p.inputs) {
        if (input.atoms.empty()) rejectMaterial("automatic inventory requires nonempty feeds");
    }
    for (const auto& output : p.outputs) {
        if (output.atoms.empty() || output.atoms.size() > 16)
            rejectMaterial("automatic inventory requires bounded nonempty products");
    }
    std::vector<int> outputOrientations;
    for (int turns = 0; turns < 6; ++turns) {
        const core::HexFrame frame{{0, 0}, turns};
        bool fits = true;
        int requiredReach = 1;
        for (const auto& output : p.outputs) {
            auto low = frame.ToWorld(output.atoms.front().position), high = low;
            for (const auto& atom : output.atoms) {
                const auto cell = frame.ToWorld(atom.position);
                low.q = std::min(low.q, cell.q);
                high.q = std::max(high.q, cell.q);
                low.r = std::min(low.r, cell.r);
                high.r = std::max(high.r, cell.r);
            }
            requiredReach = std::max(requiredReach, high.r - low.r + 1);
            fits &= high.q - low.q <= 32;
        }
        const bool missingPiston = requiredReach > 1 && !(p.permissionBits & (1ULL << 2));
        if (!fits || requiredReach > 3 || missingPiston) {
            report(SupplyFailureCategory::Layout, turns, 0,
                   "output orientation cannot fit automatic rail reach/width or piston "
                   "permissions; independent of material history",
                   missingPiston ? std::optional<int>{2} : std::nullopt);
        } else
            outputOrientations.push_back(turns);
    }
    if (outputOrientations.empty()) {
        report(SupplyFailureCategory::Layout, -1, 0,
               "no output orientation fits automatic rail; a different mechanical backend is "
               "required");
        return std::nullopt;
    }
    auto materialPuzzle = p;
    materialPuzzle.productionInfo.reset();
    try {
        (void)IR::Initial(materialPuzzle);
    } catch (const IR::Error& error) {
        rejectMaterial(error.what());
    }
    for (const auto* molecules : {&p.inputs, &p.outputs})
        for (const auto& molecule : *molecules)
            for (const auto& atom : molecule.atoms)
                if (atom.position.q < -128 || atom.position.q > 127 || atom.position.r < -128 ||
                    atom.position.r > 127)
                    rejectMaterial(
                        "automatic inventory molecule coordinates exceed serialized range");
    InventorySearchLimits mechanicalLimits;
    for (std::size_t i = 0; i < p.inputs.size(); ++i) {
        if (p.inputs[i].atoms.size() > 16) {
            mechanicalLimits.excludedInputs.push_back(i);
            continue;
        }
        const auto layout = PlanExtractionLayout(p.inputs[i]);
        int low = layout.cells.front().r, high = low;
        int lowQ = layout.cells.front().q, highQ = lowQ;
        for (const auto cell : layout.cells) {
            low = std::min(low, cell.r);
            high = std::max(high, cell.r);
            lowQ = std::min(lowQ, cell.q);
            highQ = std::max(highQ, cell.q);
        }
        const auto height = high - low + 1;
        const auto reach = std::min(3, height + (p.inputs[i].bonds.empty() ? 0 : 1));
        const bool missingPiston = reach > 1 && !(p.permissionBits & (1ULL << 2));
        if (height > 3 || highQ - lowQ + 1 > 64 || missingPiston) {
            mechanicalLimits.excludedInputs.push_back(i);
            report(missingPiston ? SupplyFailureCategory::Material : SupplyFailureCategory::Layout,
                   -1, 0,
                   "input " + std::to_string(i) +
                       " excluded: mechanical extraction requires reach " +
                       std::to_string(std::max(height, reach)) + ", width " +
                       std::to_string(highQ - lowQ + 1) +
                       "; supported reach is at most 3 and larger reach requires piston permission",
                   missingPiston ? std::optional<int>{2} : std::nullopt);
        }
    }
    std::vector<std::size_t> productionPeriods;
    for (std::size_t period = 1; period <= 12; ++period)
        if ((6ULL * p.outputMultiplier) % period == 0) productionPeriods.push_back(period);
    AssemblyPlan assembly;
    SupplyPlan supply;
    std::optional<JointSupplyPlan> joint;
    bool materialBudgetExhausted = false;
    try {
        if (p.outputs.size() == 1) {
            try {
                assembly = AssemblyPlanner::Plan(materialPuzzle);
                supply = AssemblyPlanner::PlanSupply(materialPuzzle, false, 256);
            } catch (const std::invalid_argument&) {
                auto limits = mechanicalLimits;
                limits.maxStates = 256;
                limits.multiStagePurification = true;
                std::size_t remainingPeriods = productionPeriods.size();
                for (std::size_t period : productionPeriods) {
                    if (limits.maxStates == 0) break;
                    auto periodLimits = limits;
                    periodLimits.maxStates =
                        std::max<std::size_t>(1, limits.maxStates / remainingPeriods--);
                    auto found = SearchJointSupplyInventory(materialPuzzle, period, periodLimits);
                    materialBudgetExhausted |= found.status == InventorySearchStatus::Exhausted;
                    limits.maxStates -= std::min(limits.maxStates, found.states);
                    if (found.status == InventorySearchStatus::Found) {
                        joint = std::move(found.plan);
                        break;
                    }
                }
                if (!joint)
                    throw std::invalid_argument(
                        "joint inventory found no closed production period within bounds");
            }
        } else {
            auto limits = mechanicalLimits;
            // Joint periods share a budget so fallback strategies retain execution time.
            limits.maxStates = 256;
            limits.multiStagePurification = true;
            std::size_t remainingPeriods = productionPeriods.size();
            for (std::size_t period : productionPeriods) {
                if (limits.maxStates == 0) break;
                auto periodLimits = limits;
                const auto periodsLeft = remainingPeriods--;
                periodLimits.maxStates =
                    periodsLeft == productionPeriods.size()
                        ? limits.maxStates * 3 / 4
                        : std::max<std::size_t>(1, limits.maxStates / periodsLeft);
                auto found = SearchJointSupplyInventory(materialPuzzle, period, periodLimits);
                materialBudgetExhausted |= found.status == InventorySearchStatus::Exhausted;
                limits.maxStates -= std::min(limits.maxStates, found.states);
                if (found.status == InventorySearchStatus::Found) {
                    joint = std::move(found.plan);
                    break;
                }
            }
            if (!joint)
                throw std::invalid_argument(
                    "joint inventory found no closed production period within bounds");
        }
    } catch (const std::invalid_argument& error) {
        report(materialBudgetExhausted ? SupplyFailureCategory::Budget
                                       : SupplyFailureCategory::Material,
               -1, 0, error.what());
        throw;
    }
    if (p.productionInfo) {
        const auto compactDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(4);
        auto candidate = CompileCompact(
            p, materialPuzzle, assembly, supply, joint ? &*joint : nullptr, compactDeadline, accept,
            [&](const std::string& detail) {
                report(SupplyFailureCategory::Layout, -1, 0, "compact chamber: " + detail);
            });
        if (candidate) return candidate;
    }
    // Compact placement has its own budget; fallbacks get their full slice afterward.
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    auto tryMaterial = [&](const JointSupplyPlan* material) -> std::optional<io::SolutionData> {
        ++materialAttempt;
        const auto materialDeadline =
            std::min(deadline, std::chrono::steady_clock::now() + std::chrono::milliseconds(400));
        productsPerPeriod = material ? material->productsPerPeriod : supply.productsPerPeriod;
        const auto& usage = material ? material->usage : supply.usage;
        for (const auto input : mechanicalLimits.excludedInputs)
            if (usage.at(input).batches) {
                report(SupplyFailureCategory::Layout, -1, 0,
                       "selected feed violates mechanical reach/width or extraction permissions; "
                       "replanning with feed excluded");
                return std::nullopt;
            }
        std::unordered_set<bool> permissionRejectedModes;
        std::set<std::pair<int, bool>> pistonRejectedOrientations;
        const bool mandatoryFragments =
            !(p.permissionBits & (1ULL << 9)) ||
            std::any_of(p.outputs.begin(), p.outputs.end(), [&](const auto& output) {
                return std::any_of(output.bonds.begin(), output.bonds.end(), [&](const auto& bond) {
                    if (bond.bits == 1) return !(p.permissionBits & (1ULL << 8));
                    if (!(p.permissionBits & (1ULL << 11))) return true;
                    for (const auto cell : {bond.from, bond.to}) {
                        const auto atom =
                            std::find_if(output.atoms.begin(), output.atoms.end(),
                                         [&](const auto& atom) { return atom.position == cell; });
                        if (atom == output.atoms.end() || atom->type != core::AtomType::Fire)
                            return true;
                    }
                    return false;
                });
            });
        const std::array<bool, 2> preservationOrder = mandatoryFragments
                                                          ? std::array<bool, 2>{true, false}
                                                          : std::array<bool, 2>{false, true};
        for (int spacing : {1, 3, 5})
            for (const int turns : outputOrientations)
                for (bool preserveResidualBonds : preservationOrder) {
                    if (permissionRejectedModes.contains(preserveResidualBonds) ||
                        pistonRejectedOrientations.contains({turns, preserveResidualBonds}))
                        continue;
                    if (preserveResidualBonds &&
                        std::none_of(materialPuzzle.inputs.begin(), materialPuzzle.inputs.end(),
                                     [](const auto& input) { return !input.bonds.empty(); }))
                        continue;
                    if (std::chrono::steady_clock::now() >= materialDeadline) {
                        report(SupplyFailureCategory::Budget, turns, spacing,
                               "material layout search slice exhausted; replanning upstream "
                               "inventory");
                        return std::nullopt;
                    }
                    std::optional<io::SolutionData> candidate;
                    std::unordered_set<Coord, core::HexCoordHash> footprint;
                    try {
                        candidate = Compile(materialPuzzle, assembly, supply, turns, spacing,
                                            p.productionInfo ? &footprint : nullptr, material,
                                            preserveResidualBonds);
                    } catch (const IR::PhysicalFailure& error) {
                        if (feedback)
                            feedback(
                                {.category = error.exhausted ? SupplyFailureCategory::Budget
                                                             : SupplyFailureCategory::Layout,
                                 .rotation = turns,
                                 .spacing = spacing,
                                 .detail = error.what(),
                                 .materialAttempt = materialAttempt,
                                 .productsPerPeriod = productsPerPeriod,
                                 .operation = error.step,
                                 .fragment = {error.atom},
                                 .conflictCell = error.destination,
                                 .mechanicalMode = "rail",
                                 .budgetDimension = error.exhausted ? "held-motion-states" : ""});
                        continue;
                    } catch (const IR::PermissionFailure& error) {
                        report(SupplyFailureCategory::Material, turns, spacing, error.what(),
                               error.bit);
                        if (error.bit == 2)
                            pistonRejectedOrientations.insert({turns, preserveResidualBonds});
                        else
                            permissionRejectedModes.insert(preserveResidualBonds);
                        continue;
                    } catch (const IR::BudgetFailure& error) {
                        report(SupplyFailureCategory::Budget, turns, spacing, error.what());
                        continue;
                    } catch (const IR::TimingFailure& error) {
                        report(SupplyFailureCategory::Timing, turns, spacing, error.what());
                        continue;
                    } catch (const IR::LayoutFailure& error) {
                        report(SupplyFailureCategory::Layout, turns, spacing, error.what());
                        continue;
                    } catch (const IR::Unsupported& error) {
                        report(SupplyFailureCategory::Layout, turns, spacing, error.what());
                        continue;
                    } catch (const IR::Error& error) {
                        report(SupplyFailureCategory::Material, turns, spacing, error.what());
                        continue;
                    }
                    if (!candidate) {
                        report(SupplyFailureCategory::Layout, turns, spacing,
                               "material program does not fit supported chamber geometry");
                        continue;
                    }
                    if (p.productionInfo) {
                        if (p.productionInfo->chambers.empty()) {
                            report(SupplyFailureCategory::Layout, turns, spacing,
                                   "automatic inventory requires production chambers");
                            continue;
                        }
                        candidate =
                            PlaceInChamber(p, *candidate, footprint, [&](const auto& placed) {
                                if (accept(placed)) return true;
                                report(SupplyFailureCategory::IndependentVerifier, turns, spacing,
                                       "chamber placement rejected by independent acceptance");
                                return false;
                            });
                        if (candidate) return candidate;
                        report(SupplyFailureCategory::Layout, turns, spacing,
                               "no accepted chamber placement within bounds");
                        continue;
                    }
                    if (accept(*candidate)) return candidate;
                    report(SupplyFailureCategory::IndependentVerifier, turns, spacing,
                           "candidate rejected by independent acceptance");
                }
        return std::nullopt;
    };
    if (auto candidate = tryMaterial(joint ? &*joint : nullptr)) return candidate;
    std::vector<JointSupplyPlan> attemptedMaterials;
    if (joint)
        attemptedMaterials.push_back(*joint);
    else {
        JointSupplyPlan initial;
        initial.usage = supply.usage;
        initial.productsPerPeriod = supply.productsPerPeriod;
        initial.recipeConstrained = supply.recipeConstrained;
        for (const auto& step : supply.steps)
            initial.steps.push_back({step, 0});
        attemptedMaterials.push_back(std::move(initial));
    }
    std::optional<io::SolutionData> accepted;
    auto limits = mechanicalLimits;
    limits.maxStates = 256;
    limits.multiStagePurification = true;
    std::size_t remainingPeriods = productionPeriods.size();
    for (std::size_t period : productionPeriods) {
        if (limits.maxStates == 0 || std::chrono::steady_clock::now() >= deadline) break;
        auto periodLimits = limits;
        periodLimits.maxStates = std::max<std::size_t>(1, limits.maxStates / remainingPeriods--);
        const auto replanned = SearchJointSupplyInventory(
            materialPuzzle, period, periodLimits, nullptr, [&](const JointSupplyPlan& alternative) {
                if (std::chrono::steady_clock::now() >= deadline ||
                    std::find(attemptedMaterials.begin(), attemptedMaterials.end(), alternative) !=
                        attemptedMaterials.end())
                    return false;
                attemptedMaterials.push_back(alternative);
                accepted = tryMaterial(&alternative);
                return accepted.has_value();
            });
        limits.maxStates -= std::min(limits.maxStates, replanned.states);
        if (accepted) return accepted;
    }
    const bool deadlineExceeded = std::chrono::steady_clock::now() >= deadline;
    report(deadlineExceeded || limits.maxStates == 0 ? SupplyFailureCategory::Budget
                                                     : SupplyFailureCategory::Material,
           -1, 0,
           deadlineExceeded ? "automatic material replanning deadline exceeded"
           : limits.maxStates == 0
               ? "automatic material replanning state budget exhausted"
               : "automatic material replanning exhausted supported production periods");
    return std::nullopt;
}

std::optional<io::SolutionData> CompileVerifiedSupplyInventory(
    const io::PuzzleData& p,
    const std::function<io::VerificationReport(const io::SolutionData&)>& verify,
    const std::function<void(const SupplyFailure&)>& feedback) {
    std::optional<io::VerificationReport> lastReport;
    auto candidate = CompileAcceptedSupplyInventory(
        p,
        [&](const io::SolutionData& solution) {
            lastReport = verify(solution);
            return lastReport->Passed();
        },
        [&](const SupplyFailure& failure) {
            if (!feedback) return;
            auto enriched = failure;
            if (failure.category == SupplyFailureCategory::IndependentVerifier && lastReport) {
                enriched.verification = lastReport;
                if (lastReport->status == io::VerificationStatus::Unavailable)
                    enriched.category = SupplyFailureCategory::VerificationUnavailable;
                if (!lastReport->detail.empty()) enriched.detail = lastReport->detail;
            }
            feedback(enriched);
        });
    if (candidate && lastReport) candidate->metrics = lastReport->metrics;
    return candidate;
}
} // namespace opusoptima::solvers
