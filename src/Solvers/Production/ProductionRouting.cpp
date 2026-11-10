#include "ProductionRouting.hpp"
#include "ProductionExecution.hpp"
#include "ProductionLayout.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <exception>
#include <set>
#include <sstream>
#include <stdexcept>

namespace opusoptima::solvers {
namespace {
std::optional<io::SolutionData> SearchProductionChemistryRoute(
    const io::PuzzleData& p, std::size_t chemistryStage, int rotation,
    const std::function<bool(const io::SolutionData&)>& accept, unsigned& attempts,
    std::chrono::steady_clock::time_point deadline);

core::HexCoord Rotate(core::HexCoord cell, int turns) {
    while (turns-- > 0)
        cell = core::RotateClockwise(cell);
    return cell;
}
} // namespace
ProductionRoutePlan PlanProductionRoute(const io::PuzzleData& p, unsigned relayChoices) {
    auto reject = [] {
        throw std::invalid_argument(
            "production routing requires a connected conduit tree, unchanged connected rigid "
            "molecule, and relay chambers fitting distinct conduit endpoints");
    };
    if (!p.productionInfo || p.inputs.size() != 1 || p.outputs.size() != 1 ||
        p.inputs[0].atoms.empty() || p.outputs[0].atoms.empty())
        reject();
    const bool rigid = p.inputs[0].atoms.size() > 1;
    if (!rigid &&
        (p.outputs[0].atoms.size() != 1 || p.inputs[0].atoms[0].type != p.outputs[0].atoms[0].type))
        reject();
    const auto layouts = PlanProductionChambers(p);
    const auto& conduits = p.productionInfo->conduits;
    if (layouts.size() < 2 || layouts.size() > 16 || conduits.size() + 1 != layouts.size())
        reject();
    struct Edge {
        std::size_t next, conduit;
    };
    std::vector<std::vector<Edge>> graph(layouts.size());
    auto chamber = [&](core::HexCoord position) {
        for (std::size_t i = 0; i < layouts.size(); ++i)
            if (ContainsProductionCell(layouts[i], position)) return i;
        reject();
        return layouts.size();
    };
    for (std::size_t i = 0; i < conduits.size(); ++i) {
        const auto& c = conduits[i];
        if ((!rigid && c.path != std::vector<core::HexCoord>{{0, 0}}) || c.path.empty()) reject();
        const auto a = chamber(c.positionA), b = chamber(c.positionB);
        if (a == b) reject();
        graph[a].push_back({b, i});
        graph[b].push_back({a, i});
    }
    std::vector<bool> connected(layouts.size());
    std::vector<std::size_t> pending{0};
    connected[0] = true;
    for (std::size_t i = 0; i < pending.size(); ++i)
        for (const auto edge : graph[pending[i]])
            if (!connected[edge.next]) {
                connected[edge.next] = true;
                pending.push_back(edge.next);
            }
    if (pending.size() != layouts.size()) reject();
    // A tree can contain unused branches. Select its longest source-to-sink
    // path, so relay scheduling does not mistake listing order for topology.
    auto farthest = [&](std::size_t root, std::vector<std::size_t>& parent) {
        parent.assign(layouts.size(), layouts.size());
        std::vector<std::size_t> queue{root};
        parent[root] = root;
        for (std::size_t i = 0; i < queue.size(); ++i)
            for (const auto edge : graph[queue[i]])
                if (parent[edge.next] == layouts.size()) {
                    parent[edge.next] = queue[i];
                    queue.push_back(edge.next);
                }
        return queue.back();
    };
    std::vector<std::size_t> parent;
    const auto start = farthest(0, parent);
    const auto finish = farthest(start, parent);
    std::vector<std::size_t> path{finish};
    while (path.back() != start)
        path.push_back(parent[path.back()]);
    std::reverse(path.begin(), path.end());
    ProductionRoutePlan result;
    int relayTurns = 0;
    for (std::size_t index = 0; index < path.size(); ++index) {
        const auto current = path[index];
        result.chambers.push_back(layouts[current]);
        if (index + 1 == path.size()) break;
        for (const auto edge : graph[current])
            if (edge.next == path[index + 1]) {
                // Reuse the strict transport gate, including permissions, chemistry,
                // special-rule and coordinate checks, for every edge.
                auto local = p;
                local.productionInfo->chambers = {p.productionInfo->chambers[current],
                                                  p.productionInfo->chambers[edge.next]};
                local.productionInfo->conduits = {conduits[edge.conduit]};
                if (chamber(conduits[edge.conduit].positionA) != current)
                    std::swap(local.productionInfo->conduits[0].positionA,
                              local.productionInfo->conduits[0].positionB);
                const auto transport = PlanProductionTransport(local, true);
                auto handoff = transport.handoff;
                if (index == 0) {
                    result.inputPosition = transport.inputPosition;
                    result.inputRotation = transport.inputRotation;
                    result.outputPosition = transport.outputPosition;
                    result.outputRotation = transport.outputRotation;
                }
                if (rigid) {
                    handoff.payload =
                        result.handoffs.empty() ? handoff.payload : result.handoffs.back().payload;
                    if (!result.handoffs.empty()) {
                        const int turn = ((relayChoices >> (index - 1)) & 1U)
                                             ? (index % 2 == 1 ? 1 : 5)
                                             : (index % 2 == 1 ? 5 : 1);
                        result.relayTurns.push_back(turn);
                        relayTurns = (relayTurns + turn) % 6;
                        for (auto& atom : handoff.payload.atoms)
                            atom.position = Rotate(atom.position, turn);
                        for (auto& bond : handoff.payload.bonds) {
                            bond.from = Rotate(bond.from, turn);
                            bond.to = Rotate(bond.to, turn);
                        }
                    }
                    bool matched = false;
                    for (int turn = 0; turn < 6 && !matched; ++turn) {
                        matched =
                            handoff.localShape.size() == handoff.payload.atoms.size() &&
                            std::all_of(handoff.payload.atoms.begin(), handoff.payload.atoms.end(),
                                        [&](const auto& atom) {
                                            return std::count_if(handoff.localShape.begin(),
                                                                 handoff.localShape.end(),
                                                                 [&](auto cell) {
                                                                     return Rotate(cell, turn) ==
                                                                            atom.position;
                                                                 }) == 1;
                                        });
                        if (matched) handoff.rotation = turn;
                    }
                    if (!matched) reject();
                }
                handoff.conduitIndex = edge.conduit;
                handoff.conduitId = 100 + static_cast<int>(edge.conduit);
                const int hop = static_cast<int>(result.handoffs.size());
                const auto outgoing = hop == 0 ? core::HexCoord{0, 1}
                                      : rigid && result.relayTurns.back() == 1
                                          ? core::HexCoord{-1, 1}
                                          : core::HexCoord{1, 0};
                handoff.sourcePosition = core::Add(layouts[current].center, outgoing);
                handoff.destinationPosition = core::Add(layouts[edge.next].center, {0, 1});
                handoff.releaseCycle = 2 + hop * 4;
                handoff.pickupCycle = 4 + hop * 4;
                result.handoffs.push_back(handoff);
                break;
            }
    }
    if (rigid) {
        const int turns = relayTurns;
        const auto firstDestination = result.chambers[1].center;
        result.outputPosition = core::Add(
            result.chambers.back().center,
            core::Add(core::HexCoord{-1, 1},
                      Rotate(core::Subtract(core::Subtract(result.outputPosition, firstDestination),
                                            {-1, 1}),
                             turns)));
        result.outputRotation = (result.outputRotation + turns) % 6;
        bool orientationFits = false;
        for (int rotation = 0; rotation < 6 && !orientationFits; ++rotation) {
            auto fits = [&](std::size_t chamber, core::HexCoord cell) {
                const auto center = result.chambers[chamber].center;
                return ContainsProductionCell(
                    result.chambers[chamber],
                    core::Add(center, Rotate(core::Subtract(cell, center), rotation)));
            };
            bool feasible = true;
            for (std::size_t i = 0; i < result.handoffs.size(); ++i) {
                const auto& h = result.handoffs[i];
                for (const auto& atom : h.payload.atoms) {
                    const auto source = core::Add(h.sourcePosition, atom.position);
                    const auto destination = core::Add(h.destinationPosition, atom.position);
                    feasible &= fits(i, source) && fits(i + 1, destination) &&
                                source != result.chambers[i].center &&
                                destination != result.chambers[i + 1].center;
                }
            }
            for (const auto& atom : p.inputs[0].atoms)
                feasible &= fits(0, core::Add(result.inputPosition,
                                              Rotate(atom.position, result.inputRotation)));
            for (const auto& atom : p.outputs[0].atoms)
                feasible &= fits(
                    result.chambers.size() - 1,
                    core::Add(result.outputPosition, Rotate(atom.position, result.outputRotation)));
            orientationFits = feasible;
        }
        if (!orientationFits) reject();
    }
    result.period = 4 * static_cast<int>(result.handoffs.size()) + 4;
    for (auto& handoff : result.handoffs)
        handoff.period = result.period;
    return result;
}

namespace {
io::SolutionData CompileRoute(const io::PuzzleData& p, ProductionRoutePlan plan, int rotation,
                              bool calcify, std::size_t chemistryStage) {
    if (rotation < 0 || rotation >= 6 || (calcify && chemistryStage >= plan.chambers.size()))
        throw std::invalid_argument("invalid production route orientation or chemistry stage");
    if (calcify) {
        // A free-atom cycle at the selected incoming pose allows calcification
        // before the grab. Delay every downstream handoff, not just that arm.
        ++plan.period;
        for (std::size_t i = 0; i < plan.handoffs.size(); ++i) {
            auto& h = plan.handoffs[i];
            if (i >= chemistryStage) ++h.releaseCycle;
            if (i + 1 >= chemistryStage) ++h.pickupCycle;
            h.period = plan.period;
        }
    }
    io::SolutionData result;
    result.puzzleName = p.name;
    result.solutionName =
        calcify ? "Composed production calcification route" : "Serial production supply route";
    auto transform = [&](std::size_t chamber, core::HexCoord position) {
        const auto center = plan.chambers[chamber].center;
        return core::Add(center, Rotate(core::Subtract(position, center), rotation));
    };
    auto pose = [&](std::size_t chamber, core::HexCoord position) {
        const auto rotated = transform(chamber, position);
        if (!ContainsProductionCell(plan.chambers[chamber], rotated))
            throw std::invalid_argument("production route pose lies outside chamber");
        return rotated;
    };
    for (std::size_t i = 0; i < plan.handoffs.size(); ++i) {
        const auto& h = plan.handoffs[i];
        for (const auto& atom : h.payload.atoms) {
            const auto source = pose(i, core::Add(h.sourcePosition, atom.position));
            const auto destination = pose(i + 1, core::Add(h.destinationPosition, atom.position));
            if (source == plan.chambers[i].center || destination == plan.chambers[i + 1].center)
                throw std::invalid_argument("production route payload intersects arm base");
        }
        std::vector<int> body{1,           (h.rotation + rotation) % 6,          0, 0, 0,
                              h.conduitId, static_cast<int>(h.localShape.size())};
        for (auto cell : h.localShape) {
            body.push_back(cell.q);
            body.push_back(cell.r);
        }
        result.objects.push_back({"pipe", pose(i, h.sourcePosition), {}, body});
        result.objects.push_back({"pipe", pose(i + 1, h.destinationPosition), {}, body});
    }
    const auto inputCell = pose(0, core::Add(plan.chambers.front().center, {1, 0}));
    const auto outputCell =
        pose(plan.chambers.size() - 1, core::Add(plan.chambers.back().center, {-1, 1}));
    if (p.inputs[0].atoms.size() > 1) {
        for (const auto& atom : p.inputs[0].atoms)
            (void)pose(0, core::Add(plan.inputPosition, Rotate(atom.position, plan.inputRotation)));
        for (const auto& atom : p.outputs[0].atoms)
            (void)pose(plan.chambers.size() - 1,
                       core::Add(plan.outputPosition, Rotate(atom.position, plan.outputRotation)));
        result.objects.push_back({"input",
                                  transform(0, plan.inputPosition),
                                  {},
                                  {1, (plan.inputRotation + rotation) % 6, 0, 0, 0}});
        result.objects.push_back({"out-std",
                                  transform(plan.chambers.size() - 1, plan.outputPosition),
                                  {},
                                  {1, (plan.outputRotation + rotation) % 6, 0, 0, 0}});
    } else {
        result.objects.push_back(
            {"input",
             core::Subtract(inputCell, Rotate(p.inputs[0].atoms[0].position, rotation)),
             {},
             {1, rotation, 0, 0, 0}});
        result.objects.push_back(
            {"out-std",
             core::Subtract(outputCell, Rotate(p.outputs[0].atoms[0].position, rotation)),
             {},
             {1, rotation, 0, 0, 0}});
    }
    const int start = calcify && chemistryStage == 0 ? 1 : 0;
    result.objects.push_back({"arm1",
                              plan.chambers.front().center,
                              {{'G', start},
                               {'r', start + 1},
                               {'g', start + 2},
                               {'R', start + 3},
                               {'O', plan.period - 1}},
                              {1, rotation, 0, 5, 0}});
    for (std::size_t i = 1; i < plan.chambers.size(); ++i) {
        const auto& incoming = plan.handoffs[i - 1];
        const int t = incoming.pickupCycle;
        const bool last = i + 1 == plan.chambers.size();
        const bool clockwise = last || (!plan.relayTurns.empty() && plan.relayTurns[i - 1] == 1);
        result.objects.push_back({"arm1",
                                  plan.chambers[i].center,
                                  {{'G', t},
                                   {clockwise ? 'r' : 'R', t + 1},
                                   {'g', t + 2},
                                   {clockwise ? 'R' : 'r', t + 3}},
                                  {1, (1 + rotation) % 6, 0, 4, static_cast<int>(i)}});
    }
    if (calcify) {
        for (std::size_t i = 0; i < p.inputs[0].atoms.size(); ++i) {
            if (p.inputs[0].atoms[i].type == p.outputs[0].atoms[i].type) continue;
            const auto cell =
                chemistryStage == 0
                    ? pose(0, core::Add(plan.inputPosition,
                                        Rotate(p.inputs[0].atoms[i].position, plan.inputRotation)))
                    : pose(chemistryStage,
                           core::Add(plan.handoffs[chemistryStage - 1].destinationPosition,
                                     plan.handoffs[chemistryStage - 1].payload.atoms[i].position));
            result.objects.push_back({"glyph-calcification", cell, {}, {1, rotation, 0, 0, 0}});
        }
    }
    if (!calcify) {
        try {
            ValidateProductionRouteMaterial(p, plan, rotation);
        } catch (const ExecutionIR::Error& error) {
            throw std::invalid_argument(error.what());
        }
    }
    return result;
}
} // namespace

std::optional<io::SolutionData> CompileAcceptedProductionRoute(
    const io::PuzzleData& p, const std::function<bool(const io::SolutionData&)>& accept) {
    if (!p.productionInfo || p.productionInfo->chambers.size() < 2 ||
        p.productionInfo->chambers.size() > 16 || p.inputs.empty())
        throw std::invalid_argument("production route requires 2–16 chambers and an input");
    const auto relays = p.productionInfo->chambers.size() - 2;
    const unsigned patterns =
        p.inputs[0].atoms.size() > 1 ? 1U << std::min<std::size_t>(relays, 6) : 1U;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    unsigned candidates = 0;
    for (unsigned choices = 0; choices < patterns; ++choices) {
        if (std::chrono::steady_clock::now() >= deadline) break;
        std::optional<ProductionRoutePlan> plan;
        try {
            plan = PlanProductionRoute(p, choices);
        } catch (const std::invalid_argument&) {
            continue;
        }
        for (int rotation = 0; rotation < 6; ++rotation) {
            if (candidates >= 64 || std::chrono::steady_clock::now() >= deadline)
                return std::nullopt;
            std::optional<io::SolutionData> candidate;
            try {
                candidate = CompileRoute(p, *plan, rotation, false, 0);
            } catch (const std::invalid_argument&) {
                continue;
            }
            ++candidates;
            if (accept(*candidate)) return candidate;
        }
    }
    return std::nullopt;
}

std::optional<io::SolutionData> CompileAcceptedProductionStreams(
    const io::PuzzleData& p, const std::function<bool(const io::SolutionData&)>& accept) {
    if (!p.productionInfo || p.outputs.size() < 2 || p.outputs.size() > 8 ||
        p.inputs.size() != p.outputs.size())
        throw std::invalid_argument("joint production streams require 2–8 input/output pairs");
    const auto layouts = PlanProductionChambers(p);
    const auto start = std::chrono::steady_clock::now();
    const auto deadline = start + std::chrono::seconds(2);
    auto stageDeadline = start + std::chrono::milliseconds(1500);
    unsigned attempts = 0, states = 0, chemistryCompilations = 0;
    unsigned stateLimit = 192, attemptLimit = 48;
    std::vector<bool> usedInputs(p.inputs.size());
    std::vector<unsigned> usedConduits(p.productionInfo->conduits.size());
    std::vector<unsigned> usedChambers(layouts.size());
    io::SolutionData merged;
    merged.puzzleName = p.name;
    merged.solutionName = "Joint production conduit streams";
    std::optional<io::SolutionData> result;
    auto chamberAt = [&](core::HexCoord position) {
        for (std::size_t i = 0; i < layouts.size(); ++i)
            if (ContainsProductionCell(layouts[i], position)) return i;
        return layouts.size();
    };
    std::vector<std::vector<std::size_t>> paths;
    std::vector<std::vector<std::pair<std::size_t, std::size_t>>> edges(layouts.size());
    for (std::size_t i = 0; i < p.productionInfo->conduits.size(); ++i) {
        const auto& pipe = p.productionInfo->conduits[i];
        const auto a = chamberAt(pipe.positionA), b = chamberAt(pipe.positionB);
        if (a < layouts.size() && b < layouts.size() && a != b) {
            edges[a].push_back({b, i});
            edges[b].push_back({a, i});
        }
    }
    std::vector<bool> visited(layouts.size());
    std::vector<std::size_t> path;
    std::function<void(std::size_t, std::size_t)> enumerate = [&](std::size_t start,
                                                                  std::size_t current) {
        if (paths.size() >= 256 || path.size() >= 15) return;
        visited[current] = true;
        for (auto [next, edge] : edges[current]) {
            if (visited[next]) continue;
            path.push_back(edge);
            if (next > start) paths.push_back(path);
            enumerate(start, next);
            path.pop_back();
            if (paths.size() >= 256) break;
        }
        visited[current] = false;
    };
    for (std::size_t start = 0; start < layouts.size() && paths.size() < 256; ++start)
        enumerate(start, start);
    std::set<std::string> testedMachines;
    bool diversify = false, shareChambers = false;
    std::function<void(std::size_t)> search = [&](std::size_t output) {
        const auto searchAttemptLimit =
            std::min(attemptLimit, attempts + (output == 0 ? attemptLimit : 16));
        if (result || attempts >= attemptLimit || ++states > stateLimit ||
            std::chrono::steady_clock::now() >= stageDeadline)
            return;
        if (output == p.outputs.size()) {
            auto candidate = merged;
            int period = 0;
            for (const auto& object : candidate.objects)
                for (const auto& instruction : object.instructions)
                    period = std::max(period, instruction.parameter + 1);
            for (auto& object : candidate.objects)
                for (auto& instruction : object.instructions)
                    if (instruction.action == 'O') instruction.parameter = period - 1;
            std::ostringstream signature;
            for (const auto& object : candidate.objects) {
                signature << object.typeName << ':' << object.position.q << ',' << object.position.r
                          << ':';
                for (auto value : object.bodyData)
                    signature << value << ',';
                signature << ':';
                for (const auto& instruction : object.instructions)
                    signature << instruction.action << instruction.parameter << ',';
                signature << ';';
            }
            if (!testedMachines.insert(signature.str()).second) return;
            ++attempts;
            if (accept(candidate)) result = std::move(candidate);
            return;
        }
        for (std::size_t input = 0; input < p.inputs.size(); ++input) {
            if (usedInputs[input]) continue;
            for (const auto& route : paths) {
                std::vector<std::size_t> chambers;
                for (auto conduit : route) {
                    const auto& pipe = p.productionInfo->conduits[conduit];
                    chambers.push_back(chamberAt(pipe.positionA));
                    chambers.push_back(chamberAt(pipe.positionB));
                }
                std::sort(chambers.begin(), chambers.end());
                chambers.erase(std::unique(chambers.begin(), chambers.end()), chambers.end());
                if (std::any_of(route.begin(), route.end(),
                                [&](auto i) { return usedConduits[i] != 0 && !shareChambers; }) ||
                    (!shareChambers && std::any_of(chambers.begin(), chambers.end(),
                                                   [&](auto i) { return usedChambers[i] != 0; })))
                    continue;
                for (bool reverse : {false, true}) {
                    if (result || attempts >= attemptLimit || ++states > stateLimit ||
                        std::chrono::steady_clock::now() >= stageDeadline)
                        return;
                    auto local = p;
                    local.inputs = {p.inputs[input]};
                    local.outputs = {p.outputs[output]};
                    local.productionInfo->chambers.clear();
                    local.productionInfo->conduits.clear();
                    for (auto i : chambers)
                        local.productionInfo->chambers.push_back(p.productionInfo->chambers[i]);
                    for (auto i : route)
                        local.productionInfo->conduits.push_back(p.productionInfo->conduits[i]);
                    if (reverse) {
                        std::reverse(local.productionInfo->chambers.begin(),
                                     local.productionInfo->chambers.end());
                        for (auto& pipe : local.productionInfo->conduits)
                            std::swap(pipe.positionA, pipe.positionB);
                    }
                    auto appendMachine = [&](const io::SolutionData& machine) {
                        if (result || attempts >= searchAttemptLimit || states >= stateLimit ||
                            std::chrono::steady_clock::now() >= stageDeadline)
                            return true;
                        for (const auto& object : machine.objects) {
                            if (object.typeName != "pipe") continue;
                            const auto conduit =
                                route.at(static_cast<std::size_t>(object.bodyData.at(5) - 100));
                            if (!usedConduits[conduit]) continue;
                            auto body = object.bodyData;
                            body.at(5) = 100 + static_cast<int>(conduit);
                            if (body.at(6) == 1 && body.at(7) == 0 && body.at(8) == 0)
                                body.at(1) = 0;
                            const auto existing = std::find_if(
                                merged.objects.begin(), merged.objects.end(),
                                [&](const auto& other) {
                                    if (other.typeName != "pipe" ||
                                        other.bodyData.at(5) != body.at(5) ||
                                        other.bodyData.at(6) != body.at(6))
                                        return false;
                                    for (int cell = 0; cell < body.at(6); ++cell) {
                                        const auto world = core::Add(
                                            object.position,
                                            Rotate({body.at(7 + cell * 2), body.at(8 + cell * 2)},
                                                   body.at(1)));
                                        bool found = false;
                                        for (int otherCell = 0; otherCell < other.bodyData.at(6);
                                             ++otherCell)
                                            found |=
                                                world ==
                                                core::Add(
                                                    other.position,
                                                    Rotate({other.bodyData.at(7 + otherCell * 2),
                                                            other.bodyData.at(8 + otherCell * 2)},
                                                           other.bodyData.at(1)));
                                        if (!found) return false;
                                    }
                                    return true;
                                });
                            if (existing == merged.objects.end()) return false;
                        }
                        for (const auto& object : machine.objects) {
                            if (object.typeName != "pipe") continue;
                            const auto conduit =
                                route.at(static_cast<std::size_t>(object.bodyData.at(5) - 100));
                            if (!usedConduits[conduit]) continue;
                            const auto peer = std::find_if(
                                machine.objects.begin(), machine.objects.end(),
                                [&](const auto& other) {
                                    return other.typeName == "pipe" &&
                                           other.bodyData.at(5) == object.bodyData.at(5) &&
                                           other.position != object.position;
                                });
                            if (peer == machine.objects.end() ||
                                peer->bodyData.at(6) != object.bodyData.at(6)) {
                                return false;
                            }
                            auto worldCell = [](const auto& port, int cell) {
                                return core::Add(port.position,
                                                 Rotate({port.bodyData.at(7 + cell * 2),
                                                         port.bodyData.at(8 + cell * 2)},
                                                        port.bodyData.at(1)));
                            };
                            bool compatible = false;
                            for (const auto& port : merged.objects) {
                                if (port.typeName != "pipe" ||
                                    port.bodyData.at(5) != 100 + static_cast<int>(conduit) ||
                                    port.bodyData.at(6) != object.bodyData.at(6))
                                    continue;
                                for (const auto& otherPort : merged.objects) {
                                    if (otherPort.typeName != "pipe" ||
                                        otherPort.bodyData.at(5) != port.bodyData.at(5) ||
                                        otherPort.position == port.position ||
                                        otherPort.bodyData.at(6) != port.bodyData.at(6))
                                        continue;
                                    bool matches = true;
                                    for (int cell = 0; cell < object.bodyData.at(6); ++cell) {
                                        bool paired = false;
                                        for (int otherCell = 0; otherCell < port.bodyData.at(6);
                                             ++otherCell)
                                            paired |= worldCell(object, cell) ==
                                                          worldCell(port, otherCell) &&
                                                      worldCell(*peer, cell) ==
                                                          worldCell(otherPort, otherCell);
                                        matches &= paired;
                                    }
                                    compatible |= matches;
                                }
                            }
                            if (!compatible) return false;
                        }
                        auto inputCovers = [](const auto& glyph, const auto& molecule,
                                              core::HexCoord cell) {
                            return std::any_of(
                                molecule.atoms.begin(), molecule.atoms.end(),
                                [&](const auto& atom) {
                                    return core::Add(glyph.position,
                                                     Rotate(atom.position, glyph.bodyData.at(1))) ==
                                           cell;
                                });
                        };
                        for (const auto& object : machine.objects) {
                            for (const auto& existing : merged.objects) {
                                if (object.typeName == "arm1" && existing.typeName == "input" &&
                                    inputCovers(existing, p.inputs.at(existing.bodyData.at(2)),
                                                object.position))
                                    return false;
                                if (object.typeName == "input" && existing.typeName == "arm1" &&
                                    inputCovers(object, local.inputs[0], existing.position))
                                    return false;
                            }
                        }
                        auto gripper = [](const auto& arm) {
                            return core::Add(arm.position,
                                             Rotate({arm.bodyData.at(0), 0}, arm.bodyData.at(1)));
                        };
                        for (const auto& arm : machine.objects) {
                            if (arm.typeName != "arm1") continue;
                            for (const auto& glyph : machine.objects) {
                                if (glyph.typeName != "input") continue;
                                const auto& molecule = local.inputs[0];
                                if (std::none_of(molecule.atoms.begin(), molecule.atoms.end(),
                                                 [&](const auto& atom) {
                                                     return core::Add(
                                                                glyph.position,
                                                                Rotate(atom.position,
                                                                       glyph.bodyData.at(1))) ==
                                                            gripper(arm);
                                                 }))
                                    continue;
                                for (const auto& atom : molecule.atoms) {
                                    const auto relative = core::Subtract(
                                        core::Add(glyph.position,
                                                  Rotate(atom.position, glyph.bodyData.at(1))),
                                        arm.position);
                                    for (int turn : {0, 1}) {
                                        const auto cell =
                                            core::Add(arm.position, Rotate(relative, turn));
                                        for (const auto& other : merged.objects)
                                            if (other.typeName == "arm1" && cell == other.position)
                                                return false;
                                    }
                                }
                            }
                        }
                        auto crossesBase = [&](const auto& moving, const auto& bases,
                                               bool localMolecules) {
                            for (const auto& glyph : moving.objects) {
                                if (glyph.typeName != "input" && glyph.typeName != "out-std")
                                    continue;
                                const auto& molecule =
                                    glyph.typeName == "input"
                                        ? (localMolecules ? local.inputs[0]
                                                          : p.inputs.at(glyph.bodyData.at(2)))
                                        : (localMolecules ? local.outputs[0]
                                                          : p.outputs.at(glyph.bodyData.at(2)));
                                for (const auto& arm : moving.objects) {
                                    if (arm.typeName != "arm1") continue;
                                    const auto tip = gripper(arm);
                                    const int finalTurn = glyph.typeName == "input" ? 0 : 1;
                                    const auto anchor = core::Add(
                                        arm.position,
                                        Rotate(core::Subtract(tip, arm.position), finalTurn));
                                    if (std::none_of(molecule.atoms.begin(), molecule.atoms.end(),
                                                     [&](const auto& atom) {
                                                         return core::Add(glyph.position,
                                                                          Rotate(atom.position,
                                                                                 glyph.bodyData.at(
                                                                                     1))) == anchor;
                                                     }))
                                        continue;
                                    for (const auto& atom : molecule.atoms) {
                                        const auto relative = core::Subtract(
                                            core::Add(glyph.position,
                                                      Rotate(atom.position, glyph.bodyData.at(1))),
                                            arm.position);
                                        const double x = relative.q + 0.5 * relative.r;
                                        const double y = std::sqrt(3.0) * 0.5 * relative.r;
                                        const double radius = std::hypot(x, y);
                                        const double startAngle = std::atan2(y, x);
                                        const double turnAngle = std::acos(-1.0) / 3;
                                        const double direction = glyph.typeName == "input" ? 1 : -1;
                                        for (const auto& other : bases.objects) {
                                            if (other.typeName != "arm1") continue;
                                            const auto offset =
                                                core::Subtract(other.position, arm.position);
                                            const double bx = offset.q + 0.5 * offset.r;
                                            const double by = std::sqrt(3.0) * 0.5 * offset.r;
                                            double angle =
                                                direction * (std::atan2(by, bx) - startAngle);
                                            angle = std::fmod(angle + 4 * std::acos(-1.0),
                                                              2 * std::acos(-1.0));
                                            const double nearest =
                                                std::clamp(angle, 0.0, turnAngle);
                                            double distance = std::min(
                                                std::hypot(x - bx, y - by),
                                                std::hypot(
                                                    radius * std::cos(startAngle +
                                                                      direction * turnAngle) -
                                                        bx,
                                                    radius * std::sin(startAngle +
                                                                      direction * turnAngle) -
                                                        by));
                                            if (angle <= turnAngle)
                                                distance = std::min(
                                                    distance,
                                                    std::hypot(
                                                        radius * std::cos(startAngle +
                                                                          direction * nearest) -
                                                            bx,
                                                        radius * std::sin(startAngle +
                                                                          direction * nearest) -
                                                            by));
                                            if (distance < 49.0 / 82.0) return true;
                                        }
                                    }
                                }
                            }
                            return false;
                        };
                        if (crossesBase(machine, merged, true)) return false;
                        if (crossesBase(merged, machine, false)) return false;
                        ++states;
                        const auto count = merged.objects.size();
                        const auto armOffset = std::count_if(
                            merged.objects.begin(), merged.objects.end(),
                            [](const auto& object) { return object.typeName == "arm1"; });
                        for (auto object : machine.objects) {
                            if (object.typeName == "input")
                                object.bodyData.at(2) = static_cast<int>(input);
                            else if (object.typeName == "out-std")
                                object.bodyData.at(2) = static_cast<int>(output);
                            else if (object.typeName == "pipe") {
                                const auto index =
                                    static_cast<std::size_t>(object.bodyData.at(5) - 100);
                                object.bodyData.at(5) = 100 + static_cast<int>(route.at(index));
                                if (object.bodyData.at(6) == 1 && object.bodyData.at(7) == 0 &&
                                    object.bodyData.at(8) == 0)
                                    object.bodyData.at(1) = 0;
                                const auto existing = std::find_if(
                                    merged.objects.begin(), merged.objects.end(),
                                    [&](const auto& other) {
                                        if (other.typeName != "pipe" ||
                                            other.bodyData.at(5) != object.bodyData.at(5) ||
                                            other.bodyData.at(6) != object.bodyData.at(6))
                                            return false;
                                        for (int cell = 0; cell < object.bodyData.at(6); ++cell) {
                                            const auto world =
                                                core::Add(object.position,
                                                          Rotate({object.bodyData.at(7 + cell * 2),
                                                                  object.bodyData.at(8 + cell * 2)},
                                                                 object.bodyData.at(1)));
                                            bool found = false;
                                            for (int otherCell = 0;
                                                 otherCell < other.bodyData.at(6); ++otherCell)
                                                found |=
                                                    world ==
                                                    core::Add(
                                                        other.position,
                                                        Rotate(
                                                            {other.bodyData.at(7 + otherCell * 2),
                                                             other.bodyData.at(8 + otherCell * 2)},
                                                            other.bodyData.at(1)));
                                            if (!found) return false;
                                        }
                                        return true;
                                    });
                                if (existing != merged.objects.end()) continue;
                            } else if (object.typeName == "arm1")
                                object.bodyData.at(4) += static_cast<int>(armOffset);
                            merged.objects.push_back(std::move(object));
                        }
                        usedInputs[input] = true;
                        for (auto i : route)
                            ++usedConduits[i];
                        for (auto i : chambers)
                            ++usedChambers[i];
                        const auto outerStateLimit = stateLimit;
                        const auto outerDeadline = stageDeadline;
                        if (shareChambers && output + 1 < p.outputs.size()) {
                            stateLimit = std::min(stateLimit, states + 24);
                            stageDeadline =
                                std::min(stageDeadline, std::chrono::steady_clock::now() +
                                                            std::chrono::milliseconds(100));
                        }
                        search(output + 1);
                        stateLimit = outerStateLimit;
                        stageDeadline = outerDeadline;
                        usedInputs[input] = false;
                        for (auto i : route)
                            --usedConduits[i];
                        for (auto i : chambers)
                            --usedChambers[i];
                        merged.objects.resize(count);
                        return result.has_value() || attempts >= searchAttemptLimit ||
                               states >= stateLimit ||
                               std::chrono::steady_clock::now() >= stageDeadline;
                    };
                    auto
                        extend =
                            [&](const io::SolutionData& machine) {
                                if (!shareChambers) return appendMachine(machine);
                                auto placed = machine;
                                const auto placementStateLimit = std::min(stateLimit, states + 32);
                                const auto candidateAttemptLimit =
                                    std::min(searchAttemptLimit, attempts + 12);
                                std::function<bool(std::size_t)>
                                    place =
                                        [&](std::size_t index) {
                                            if (result || attempts >= candidateAttemptLimit ||
                                                states >= placementStateLimit ||
                                                std::chrono::steady_clock::now() >= stageDeadline)
                                                return true;
                                            if (index == chambers.size()) {
                                                const bool shared = std::any_of(
                                                    chambers.begin(), chambers.end(),
                                                    [&](auto i) { return usedChambers[i] != 0; });
                                                for (int delay : {8, 4, 0}) {
                                                    if (attempts >= candidateAttemptLimit)
                                                        return true;
                                                    if (!shared && delay != 0) continue;
                                                    auto scheduled = placed;
                                                    for (auto& object : scheduled.objects)
                                                        for (auto& instruction :
                                                             object.instructions)
                                                            instruction.parameter += delay;
                                                    if (appendMachine(scheduled)) return true;
                                                }
                                                return false;
                                            }
                                            const auto chamber = chambers[index];
                                            const auto& layout = layouts[chamber];
                                            std::vector<std::size_t> objects;
                                            for (std::size_t i = 0; i < machine.objects.size();
                                                 ++i) {
                                                const auto& object = machine.objects[i];
                                                auto cell = object.position;
                                                if (object.typeName == "input" ||
                                                    object.typeName == "out-std") {
                                                    const auto& molecule =
                                                        object.typeName == "input"
                                                            ? local.inputs[0]
                                                            : local.outputs[0];
                                                    cell = core::Add(
                                                        cell, Rotate(molecule.atoms.at(0).position,
                                                                     object.bodyData.at(1)));
                                                }
                                                if (ContainsProductionCell(layout, cell))
                                                    objects.push_back(i);
                                            }
                                            for (int pose = 0; pose < 6; ++pose) {
                                                auto oriented = machine;
                                                for (auto i : objects) {
                                                    auto& object = oriented.objects[i];
                                                    object.position = core::Add(
                                                        layout.center,
                                                        Rotate(core::Subtract(object.position,
                                                                              layout.center),
                                                               pose));
                                                    object.bodyData.at(1) =
                                                        (object.bodyData.at(1) + pose) % 6;
                                                }
                                                std::vector<core::HexCoord> offsets{
                                                    {0, 0},  {1, 0},  {0, 1},  {-1, 1}, {-1, 0},
                                                    {0, -1}, {1, -1}, {2, 0},  {-2, 0}, {0, -2},
                                                    {0, 2},  {2, -2}, {-2, 2}, {0, -3}, {0, 3},
                                                    {3, -3}, {-3, 3}};
                                                if (merged.objects.empty() && !diversify)
                                                    offsets.resize(1);
                                                for (auto i : objects) {
                                                    const auto& object = oriented.objects[i];
                                                    if (object.typeName != "pipe") continue;
                                                    const auto conduit =
                                                        route.at(static_cast<std::size_t>(
                                                            object.bodyData.at(5) - 100));
                                                    if (!usedConduits[conduit]) continue;
                                                    offsets.clear();
                                                    for (const auto& existing : merged.objects) {
                                                        if (existing.typeName != "pipe" ||
                                                            existing.bodyData.at(5) !=
                                                                100 + static_cast<int>(conduit) ||
                                                            !ContainsProductionCell(
                                                                layout, existing.position))
                                                            continue;
                                                        for (int cell = 0;
                                                             cell < existing.bodyData.at(6);
                                                             ++cell) {
                                                            const auto target = core::Add(
                                                                existing.position,
                                                                Rotate({existing.bodyData.at(
                                                                            7 + cell * 2),
                                                                        existing.bodyData.at(
                                                                            8 + cell * 2)},
                                                                       existing.bodyData.at(1)));
                                                            offsets.push_back(core::Subtract(
                                                                target, object.position));
                                                        }
                                                    }
                                                    break;
                                                }
                                                for (auto offset : offsets) {
                                                    bool fits = true;
                                                    for (auto i : objects) {
                                                        auto& object = placed.objects[i];
                                                        object = oriented.objects[i];
                                                        object.position =
                                                            core::Add(object.position, offset);
                                                        auto contains = [&](core::HexCoord cell) {
                                                            return ContainsProductionCell(
                                                                layout,
                                                                core::Add(
                                                                    object.position,
                                                                    Rotate(cell,
                                                                           object.bodyData.at(1))));
                                                        };
                                                        if (object.typeName == "input" ||
                                                            object.typeName == "out-std") {
                                                            const auto& molecule =
                                                                object.typeName == "input"
                                                                    ? local.inputs[0]
                                                                    : local.outputs[0];
                                                            for (const auto& atom : molecule.atoms)
                                                                fits &= contains(atom.position);
                                                        } else if (object.typeName == "pipe") {
                                                            for (int cell = 0;
                                                                 cell < object.bodyData.at(6);
                                                                 ++cell)
                                                                fits &=
                                                                    contains({object.bodyData.at(
                                                                                  7 + cell * 2),
                                                                              object.bodyData.at(
                                                                                  8 + cell * 2)});
                                                            const auto conduit =
                                                                route.at(static_cast<std::size_t>(
                                                                    object.bodyData.at(5) - 100));
                                                            if (usedConduits[conduit]) {
                                                                fits &=
                                                                    std::
                                                                        any_of(merged.objects
                                                                                   .begin(),
                                                                               merged.objects.end(),
                                                                               [&](const auto&
                                                                                       existing) {
                                                                                   if (existing.typeName !=
                                                                                           "pipe" ||
                                                                                       existing.bodyData
                                                                                               .at(5) !=
                                                                                           100 +
                                                                                               static_cast<
                                                                                                   int>(
                                                                                                   conduit) ||
                                                                                       existing.bodyData
                                                                                               .at(6) !=
                                                                                           object
                                                                                               .bodyData
                                                                                               .at(6))
                                                                                       return false;
                                                                                   for (
                                                                                       int cell = 0;
                                                                                       cell <
                                                                                       object
                                                                                           .bodyData
                                                                                           .at(6);
                                                                                       ++cell) {
                                                                                       const auto world = core::Add(
                                                                                           object
                                                                                               .position,
                                                                                           Rotate(
                                                                                               {object
                                                                                                    .bodyData
                                                                                                    .at(7 +
                                                                                                        cell *
                                                                                                            2),
                                                                                                object
                                                                                                    .bodyData
                                                                                                    .at(8 +
                                                                                                        cell *
                                                                                                            2)},
                                                                                               object
                                                                                                   .bodyData
                                                                                                   .at(1)));
                                                                                       bool found =
                                                                                           false;
                                                                                       for (
                                                                                           int other =
                                                                                               0;
                                                                                           other <
                                                                                           existing
                                                                                               .bodyData
                                                                                               .at(6);
                                                                                           ++other)
                                                                                           found |=
                                                                                               world ==
                                                                                               core::Add(
                                                                                                   existing
                                                                                                       .position,
                                                                                                   Rotate(
                                                                                                       {existing
                                                                                                            .bodyData
                                                                                                            .at(7 +
                                                                                                                other *
                                                                                                                    2),
                                                                                                        existing
                                                                                                            .bodyData
                                                                                                            .at(8 +
                                                                                                                other *
                                                                                                                    2)},
                                                                                                       existing
                                                                                                           .bodyData
                                                                                                           .at(1)));
                                                                                       if (!found)
                                                                                           return false;
                                                                                   }
                                                                                   const auto peer = std::find_if(
                                                                                       placed
                                                                                           .objects
                                                                                           .begin(),
                                                                                       placed
                                                                                           .objects
                                                                                           .end(),
                                                                                       [&](const auto&
                                                                                               other) {
                                                                                           return other.typeName ==
                                                                                                      "pipe" &&
                                                                                                  &other !=
                                                                                                      &object &&
                                                                                                  other.bodyData
                                                                                                          .at(5) ==
                                                                                                      object
                                                                                                          .bodyData
                                                                                                          .at(5) &&
                                                                                                  std::any_of(
                                                                                                      chambers
                                                                                                          .begin(),
                                                                                                      chambers.begin() +
                                                                                                          index,
                                                                                                      [&](auto
                                                                                                              previous) {
                                                                                                          return ContainsProductionCell(
                                                                                                              layouts
                                                                                                                  [previous],
                                                                                                              other
                                                                                                                  .position);
                                                                                                      });
                                                                                       });
                                                                                   if (peer !=
                                                                                       placed
                                                                                           .objects
                                                                                           .end()) {
                                                                                       const auto peerChamber = std::find_if(
                                                                                           chambers
                                                                                               .begin(),
                                                                                           chambers.begin() +
                                                                                               index,
                                                                                           [&](auto
                                                                                                   previous) {
                                                                                               return ContainsProductionCell(
                                                                                                   layouts
                                                                                                       [previous],
                                                                                                   peer->position);
                                                                                           });
                                                                                       const auto existingPeer = std::find_if(
                                                                                           merged
                                                                                               .objects
                                                                                               .begin(),
                                                                                           merged
                                                                                               .objects
                                                                                               .end(),
                                                                                           [&](const auto&
                                                                                                   other) {
                                                                                               return other.typeName ==
                                                                                                          "pipe" &&
                                                                                                      other.bodyData
                                                                                                              .at(5) ==
                                                                                                          existing
                                                                                                              .bodyData
                                                                                                              .at(5) &&
                                                                                                      ContainsProductionCell(
                                                                                                          layouts
                                                                                                              [*peerChamber],
                                                                                                          other
                                                                                                              .position);
                                                                                           });
                                                                                       if (existingPeer ==
                                                                                           merged
                                                                                               .objects
                                                                                               .end())
                                                                                           return false;
                                                                                       for (
                                                                                           int cell =
                                                                                               0;
                                                                                           cell <
                                                                                           object
                                                                                               .bodyData
                                                                                               .at(6);
                                                                                           ++cell) {
                                                                                           bool paired =
                                                                                               false;
                                                                                           for (
                                                                                               int other =
                                                                                                   0;
                                                                                               other <
                                                                                               existing
                                                                                                   .bodyData
                                                                                                   .at(6);
                                                                                               ++other) {
                                                                                               auto world = [](const auto&
                                                                                                                   port,
                                                                                                               int i) {
                                                                                                   return core::Add(
                                                                                                       port.position,
                                                                                                       Rotate(
                                                                                                           {port.bodyData
                                                                                                                .at(7 +
                                                                                                                    i * 2),
                                                                                                            port.bodyData
                                                                                                                .at(8 +
                                                                                                                    i * 2)},
                                                                                                           port.bodyData
                                                                                                               .at(1)));
                                                                                               };
                                                                                               paired |=
                                                                                                   world(
                                                                                                       object,
                                                                                                       cell) ==
                                                                                                       world(
                                                                                                           existing,
                                                                                                           other) &&
                                                                                                   world(
                                                                                                       *peer,
                                                                                                       cell) ==
                                                                                                       world(
                                                                                                           *existingPeer,
                                                                                                           other);
                                                                                           }
                                                                                           if (!paired)
                                                                                               return false;
                                                                                       }
                                                                                   }
                                                                                   return true;
                                                                               });
                                                            }
                                                        } else {
                                                            fits &= contains({});
                                                            if (object.typeName == "arm1")
                                                                for (int direction = 0;
                                                                     direction < 2; ++direction)
                                                                    fits &= contains(Rotate(
                                                                        {object.bodyData.at(0), 0},
                                                                        direction));
                                                        }
                                                        if (object.typeName == "arm1")
                                                            for (const auto& existing :
                                                                 merged.objects) {
                                                                if (existing.typeName == "arm1" &&
                                                                    existing.position ==
                                                                        object.position)
                                                                    fits = false;
                                                                if (existing.typeName == "pipe")
                                                                    for (int cell = 0;
                                                                         cell <
                                                                         existing.bodyData.at(6);
                                                                         ++cell)
                                                                        if (object.position ==
                                                                            core::Add(
                                                                                existing.position,
                                                                                Rotate(
                                                                                    {existing
                                                                                         .bodyData
                                                                                         .at(7 +
                                                                                             cell *
                                                                                                 2),
                                                                                     existing
                                                                                         .bodyData
                                                                                         .at(8 +
                                                                                             cell *
                                                                                                 2)},
                                                                                    existing
                                                                                        .bodyData
                                                                                        .at(1))))
                                                                            fits = false;
                                                            }
                                                    }
                                                    if (fits && place(index + 1)) return true;
                                                }
                                            }
                                            for (auto i : objects)
                                                placed.objects[i] = machine.objects[i];
                                            return false;
                                        };
                                const auto stopped = place(0);
                                return stopped &&
                                       (result || attempts >= searchAttemptLimit ||
                                        states >= stateLimit ||
                                        std::chrono::steady_clock::now() >= stageDeadline);
                            };
                    if (!diversify) {
                        std::optional<io::SolutionData> candidate;
                        try {
                            try {
                                if (route.size() == 1)
                                    candidate = CompileProductionTransport(local);
                                else
                                    candidate = CompileProductionRoute(local);
                            } catch (const std::invalid_argument&) {
                                candidate = SearchProductionChemistryRoute(
                                    local, 0, 0, [](const auto&) { return true; },
                                    chemistryCompilations, stageDeadline);
                            }
                        } catch (const std::invalid_argument&) {
                            continue;
                        }
                        if (candidate) (void)extend(*candidate);
                        continue;
                    }
                    // Planning failures are local; acceptance exceptions must escape unchanged.
                    std::exception_ptr acceptanceFailure;
                    auto guardedExtend = [&](const io::SolutionData& candidate) {
                        try {
                            return extend(candidate);
                        } catch (...) {
                            acceptanceFailure = std::current_exception();
                            throw;
                        }
                    };
                    try {
                        if (route.size() == 1)
                            (void)CompileAcceptedProductionTransport(local, guardedExtend,
                                                                     !shareChambers);
                        else
                            (void)CompileAcceptedProductionRoute(local, guardedExtend);
                    } catch (const std::invalid_argument&) {
                        if (acceptanceFailure) std::rethrow_exception(acceptanceFailure);
                    }
                    if (result || attempts >= attemptLimit || states >= stateLimit ||
                        std::chrono::steady_clock::now() >= stageDeadline)
                        return;
                    const bool chemistryRequired =
                        local.inputs[0].atoms.size() != local.outputs[0].atoms.size() ||
                        !std::equal(local.inputs[0].atoms.begin(), local.inputs[0].atoms.end(),
                                    local.outputs[0].atoms.begin(),
                                    [](const auto& source, const auto& target) {
                                        return source.type == target.type;
                                    });
                    if (chemistryRequired && route.size() >= 1) {
                        for (std::size_t stage = 0; stage < chambers.size(); ++stage)
                            for (int rotation = 0; rotation < 6; ++rotation) {
                                if (++states > stateLimit || attempts >= attemptLimit ||
                                    std::chrono::steady_clock::now() >= stageDeadline)
                                    return;
                                try {
                                    (void)SearchProductionChemistryRoute(
                                        local, stage, rotation, guardedExtend,
                                        chemistryCompilations, stageDeadline);
                                } catch (const std::invalid_argument&) {
                                    if (acceptanceFailure)
                                        std::rethrow_exception(acceptanceFailure);
                                }
                                if (result || attempts >= attemptLimit || states >= stateLimit ||
                                    std::chrono::steady_clock::now() >= stageDeadline)
                                    return;
                            }
                    }
                }
            }
        }
    };
    // Every disjoint stream needs at least two chambers and one conduit.
    const bool disjointPossible = p.outputs.size() <= layouts.size() / 2 &&
                                  p.outputs.size() <= p.productionInfo->conduits.size();
    if (disjointPossible) search(0);
    // Try shared ports before pose diversification consumes the common budget.
    if (!result && states < stateLimit && std::chrono::steady_clock::now() < stageDeadline) {
        shareChambers = true;
        diversify = true;
        search(0);
        shareChambers = false;
    }
    // Explore assignments before spending the remaining shared budget on local poses.
    if (disjointPossible && !result && attempts < 64 && states < 256 &&
        std::chrono::steady_clock::now() < deadline) {
        diversify = true;
        search(0);
    }
    // Reserve part of the common budget for layouts that need a shared chamber.
    stageDeadline = deadline;
    stateLimit = 256;
    attemptLimit = 64;
    if (!result && attempts < attemptLimit && states < stateLimit &&
        std::chrono::steady_clock::now() < deadline) {
        shareChambers = true;
        search(0);
    }
    return result;
}

io::SolutionData CompileProductionRoute(const io::PuzzleData& p, int rotation) {
    return CompileRoute(p, PlanProductionRoute(p), rotation, false, 0);
}

namespace {
std::optional<io::SolutionData> SearchProductionChemistryRoute(
    const io::PuzzleData& p, std::size_t chemistryStage, int rotation,
    const std::function<bool(const io::SolutionData&)>& accept, unsigned& attempts,
    const std::chrono::steady_clock::time_point deadline) {
    if (p.inputs.size() != 1 || p.outputs.size() != 1 || p.inputs[0].atoms.empty() ||
        p.inputs[0].atoms.size() != p.outputs[0].atoms.size() || !(p.permissionBits & (1ULL << 12)))
        throw std::invalid_argument("production chemistry route requires corresponding "
                                    "cardinal-to-salt atoms and calcification permission");
    auto compileAligned = [&](const io::PuzzleData& aligned) -> std::optional<io::SolutionData> {
        auto transport = aligned;
        bool changed = false;
        for (std::size_t i = 0; i < aligned.inputs[0].atoms.size(); ++i) {
            const auto& source = aligned.inputs[0].atoms[i];
            const auto& target = aligned.outputs[0].atoms[i];
            if (source.type == target.type) continue;
            if ((source.type != core::AtomType::Air && source.type != core::AtomType::Earth &&
                 source.type != core::AtomType::Fire && source.type != core::AtomType::Water) ||
                target.type != core::AtomType::Salt)
                throw std::invalid_argument("unsupported production route chemistry");
            transport.inputs[0].atoms[i].type = target.type;
            changed = true;
        }
        if (!changed)
            throw std::invalid_argument("production route chemistry requires a transformation");
        const auto chambers = p.productionInfo ? p.productionInfo->chambers.size() : 0;
        const unsigned choices =
            chambers >= 2 && chambers <= 16 ? std::min(64U, 1U << (chambers - 2)) : 1;
        for (unsigned choice = 0; choice < choices; ++choice) {
            if (attempts >= 64 || std::chrono::steady_clock::now() >= deadline) return std::nullopt;
            ++attempts;
            std::optional<io::SolutionData> candidate;
            try {
                candidate = CompileRoute(aligned, PlanProductionRoute(transport, choice), rotation,
                                         true, chemistryStage);
            } catch (const std::invalid_argument&) {
                continue;
            }
            if (accept(*candidate)) return candidate;
        }
        return std::nullopt;
    };
    auto aligned = p;
    if (p.inputs[0].atoms.size() > 1) {
        const auto& input = p.inputs[0];
        const auto& output = p.outputs[0];
        bool matched = false;
        for (int turns = 0; turns < 6; ++turns) {
            for (const auto& anchor : output.atoms) {
                if (attempts >= 64 || std::chrono::steady_clock::now() >= deadline)
                    return std::nullopt;
                auto rotate = [&](core::HexCoord cell) { return Rotate(cell, turns); };
                const auto offset =
                    core::Subtract(anchor.position, rotate(input.atoms[0].position));
                auto transform = [&](core::HexCoord cell) {
                    return core::Add(rotate(cell), offset);
                };
                std::vector<std::size_t> indices;
                bool compatible = input.bonds.size() == output.bonds.size();
                bool changed = false;
                for (const auto& source : input.atoms) {
                    const auto cell = transform(source.position);
                    const auto found =
                        std::find_if(output.atoms.begin(), output.atoms.end(),
                                     [&](const auto& atom) { return atom.position == cell; });
                    if (found == output.atoms.end()) {
                        compatible = false;
                        break;
                    }
                    const auto index = static_cast<std::size_t>(found - output.atoms.begin());
                    if (std::find(indices.begin(), indices.end(), index) != indices.end() ||
                        std::count_if(output.atoms.begin(), output.atoms.end(),
                                      [&](const auto& atom) { return atom.position == cell; }) !=
                            1) {
                        compatible = false;
                        break;
                    }
                    if (source.type != found->type) {
                        const bool cardinal = source.type == core::AtomType::Air ||
                                              source.type == core::AtomType::Earth ||
                                              source.type == core::AtomType::Fire ||
                                              source.type == core::AtomType::Water;
                        if (!cardinal || found->type != core::AtomType::Salt) {
                            compatible = false;
                            break;
                        }
                        changed = true;
                    }
                    indices.push_back(index);
                }
                if (!compatible || !changed) continue;
                for (const auto& bond : input.bonds) {
                    const auto from = transform(bond.from), to = transform(bond.to);
                    compatible &=
                        std::count_if(output.bonds.begin(), output.bonds.end(),
                                      [&](const auto& other) {
                                          return bond.bits == other.bits &&
                                                 ((from == other.from && to == other.to) ||
                                                  (from == other.to && to == other.from));
                                      }) == 1;
                }
                if (!compatible) continue;
                for (std::size_t i = 0; i < indices.size(); ++i)
                    aligned.outputs[0].atoms[i] = output.atoms[indices[i]];
                matched = true;
                if (auto candidate = compileAligned(aligned)) return *candidate;
            }
        }
        if (!matched)
            throw std::invalid_argument(
                "production molecular chemistry requires congruent corresponding atoms and bonds");
    } else if (auto candidate = compileAligned(aligned)) {
        return *candidate;
    }
    return std::nullopt;
}
} // namespace

io::SolutionData CompileProductionChemistryRoute(const io::PuzzleData& p,
                                                 std::size_t chemistryStage, int rotation) {
    unsigned attempts = 0;
    auto candidate = SearchProductionChemistryRoute(
        p, chemistryStage, rotation, [](const auto&) { return true; }, attempts,
        std::chrono::steady_clock::now() + std::chrono::seconds(2));
    if (candidate) return std::move(*candidate);
    throw std::invalid_argument("no feasible production chemistry route geometry within bounds");
}

std::optional<io::SolutionData> CompileAcceptedProductionChemistryRoute(
    const io::PuzzleData& p, const std::function<bool(const io::SolutionData&)>& accept) {
    unsigned attempts = 0;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    const auto stages =
        p.productionInfo ? std::min<std::size_t>(16, p.productionInfo->chambers.size()) : 0;
    for (int rotation = 0; rotation < 6; ++rotation) {
        for (std::size_t stage = 0; stage < stages; ++stage) {
            if (attempts >= 64 || std::chrono::steady_clock::now() >= deadline) return std::nullopt;
            if (auto candidate =
                    SearchProductionChemistryRoute(p, stage, rotation, accept, attempts, deadline))
                return candidate;
        }
    }
    return std::nullopt;
}
} // namespace opusoptima::solvers
