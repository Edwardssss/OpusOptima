#include "ProductionTransport.hpp"
#include "ProductionExecution.hpp"
#include "ProductionLayout.hpp"

#include <algorithm>
#include <chrono>
#include <stdexcept>

namespace opusoptima::solvers {
namespace {
core::HexCoord RotateTransportCell(core::HexCoord cell, int turns) {
    for (int i = 0; i < turns; ++i)
        cell = core::RotateClockwise(cell);
    return cell;
}

io::SolutionData CompileTransportPlan(const io::PuzzleData& p, const ProductionTransportPlan& plan);

ProductionTransportPlan PlanRigidTransport(
    const io::PuzzleData& p, const std::function<bool(const ProductionTransportPlan&)>& accept = {},
    int requiredArmLength = 0, bool enumerateOrientations = true) {
    auto reject = [] {
        throw std::invalid_argument(
            "rigid production transport has no compatible chamber/conduit placement");
    };
    constexpr auto permissions = (1ULL << 0) | (1ULL << 22) | (1ULL << 23) | (1ULL << 25);
    if (p.version != 3 || !p.productionInfo || p.inputs.size() != 1 || p.outputs.size() != 1 ||
        p.outputMultiplier != 1 || (p.permissionBits & permissions) != permissions)
        reject();
    const auto& info = *p.productionInfo;
    if (info.chambers.size() != 2 || info.conduits.size() != 1) reject();
    const auto layouts = PlanProductionChambers(p);
    const auto& pipe = info.conduits[0];
    const auto& input = p.inputs[0];
    const auto& output = p.outputs[0];
    if (input.atoms.empty() || input.atoms.size() > 16 ||
        input.atoms.size() != output.atoms.size() || input.atoms.size() != pipe.path.size() ||
        input.bonds.size() != output.bonds.size())
        reject();
    for (const auto* molecule : {&input, &output}) {
        std::vector<core::HexCoord> reached;
        for (const auto& atom : molecule->atoms) {
            if (atom.position.q < -120 || atom.position.q > 120 || atom.position.r < -120 ||
                atom.position.r > 120 ||
                !core::AtomTypeFromId(static_cast<std::uint8_t>(atom.type)) ||
                atom.type == core::AtomType::Repeat ||
                std::count_if(molecule->atoms.begin(), molecule->atoms.end(),
                              [&](const auto& other) { return atom.position == other.position; }) !=
                    1)
                reject();
        }
        for (const auto& bond : molecule->bonds) {
            if (std::count_if(molecule->bonds.begin(), molecule->bonds.end(),
                              [&](const auto& other) {
                                  return (bond.from == other.from && bond.to == other.to) ||
                                         (bond.from == other.to && bond.to == other.from);
                              }) != 1)
                reject();
            auto exists = [&](auto cell) {
                return std::any_of(molecule->atoms.begin(), molecule->atoms.end(),
                                   [&](const auto& atom) { return atom.position == cell; });
            };
            if (!exists(bond.from) || !exists(bond.to) || core::Distance(bond.from, bond.to) != 1 ||
                bond.bits == 0 || bond.bits > 14 || (bond.bits != 1 && (bond.bits & 1)))
                reject();
        }
        reached.push_back(molecule->atoms[0].position);
        for (std::size_t i = 0; i < reached.size(); ++i)
            for (const auto& bond : molecule->bonds) {
                auto next = bond.from == reached[i] ? bond.to
                            : bond.to == reached[i] ? bond.from
                                                    : reached[i];
                if (std::find(reached.begin(), reached.end(), next) == reached.end())
                    reached.push_back(next);
            }
        if (reached.size() != molecule->atoms.size()) reject();
    }
    auto chamberAt = [&](core::HexCoord cell) {
        for (std::size_t i = 0; i < layouts.size(); ++i)
            if (ContainsProductionCell(layouts[i], cell)) return static_cast<int>(i);
        return -1;
    };
    if (std::find(pipe.path.begin(), pipe.path.end(), core::HexCoord{0, 0}) == pipe.path.end())
        reject();
    for (auto cell : pipe.path)
        if (cell.q < -120 || cell.q > 120 || cell.r < -120 || cell.r > 120 ||
            std::count(pipe.path.begin(), pipe.path.end(), cell) != 1)
            reject();
    const int source = chamberAt(pipe.positionA), destination = chamberAt(pipe.positionB);
    if (source < 0 || destination < 0 || source == destination) reject();
    for (auto cell : pipe.path)
        if (chamberAt(core::Add(pipe.positionA, cell)) != source ||
            chamberAt(core::Add(pipe.positionB, cell)) != destination)
            reject();
    auto transform = [](const io::Molecule& molecule, core::HexCoord anchor, int rotation) {
        auto result = molecule;
        auto cell = [&](core::HexCoord c) {
            return RotateTransportCell(core::Subtract(c, anchor), rotation);
        };
        for (auto& atom : result.atoms)
            atom.position = cell(atom.position);
        for (auto& bond : result.bonds) {
            bond.from = cell(bond.from);
            bond.to = cell(bond.to);
        }
        return result;
    };
    auto same = [](const io::Molecule& a, const io::Molecule& b) {
        for (const auto& atom : a.atoms)
            if (std::count_if(b.atoms.begin(), b.atoms.end(), [&](const auto& other) {
                    return atom.type == other.type && atom.position == other.position;
                }) != 1)
                return false;
        for (const auto& bond : a.bonds)
            if (std::count_if(b.bonds.begin(), b.bonds.end(), [&](const auto& other) {
                    return bond.bits == other.bits &&
                           ((bond.from == other.from && bond.to == other.to) ||
                            (bond.from == other.to && bond.to == other.from));
                }) != 1)
                return false;
        return true;
    };
    auto grabs = input.atoms;
    std::sort(grabs.begin(), grabs.end(), [](const auto& a, const auto& b) {
        const int extentA = a.position.q + a.position.r;
        const int extentB = b.position.q + b.position.r;
        return extentA != extentB ? extentA > extentB : a.position.r > b.position.r;
    });
    for (int armLength : {1, 2, 3})
        for (int pipeRotation = 0; pipeRotation < 6; ++pipeRotation)
            for (const auto& grab : grabs)
                for (int rotation = 0; rotation < 6; ++rotation) {
                    if (requiredArmLength && armLength != requiredArmLength) continue;
                    const auto payload = transform(input, grab.position, (rotation + 1) % 6);
                    {
                        if (!std::all_of(
                                payload.atoms.begin(), payload.atoms.end(), [&](const auto& atom) {
                                    return std::count_if(
                                               pipe.path.begin(), pipe.path.end(), [&](auto cell) {
                                                   return RotateTransportCell(cell, pipeRotation) ==
                                                          atom.position;
                                               }) == 1;
                                }))
                            continue;
                        for (const auto& outputGrab : output.atoms)
                            for (int outRotation = 0; outRotation < 6; ++outRotation) {
                                if (!same(transform(input, grab.position, (rotation + 2) % 6),
                                          transform(output, outputGrab.position, outRotation)))
                                    continue;
                                ProductionTransportPlan plan;
                                plan.source = layouts[source];
                                plan.destination = layouts[destination];
                                plan.inputRotation = rotation;
                                plan.outputRotation = outRotation;
                                plan.armLength = armLength;
                                plan.inputPosition =
                                    core::Subtract(core::Add(plan.source.center, {armLength, 0}),
                                                   RotateTransportCell(grab.position, rotation));
                                plan.outputPosition = core::Subtract(
                                    core::Add(plan.destination.center, {-armLength, armLength}),
                                    RotateTransportCell(outputGrab.position, outRotation));
                                plan.handoff = {0,
                                                100,
                                                pipe.path,
                                                core::Add(plan.source.center, {0, armLength}),
                                                core::Add(plan.destination.center, {0, armLength}),
                                                pipeRotation,
                                                payload,
                                                2,
                                                4,
                                                8};
                                bool fits = true;
                                for (const auto& atom : input.atoms) {
                                    const auto relative = RotateTransportCell(
                                        core::Subtract(atom.position, grab.position), rotation);
                                    fits &= core::Add(core::HexCoord{armLength, 0}, relative) !=
                                            core::HexCoord{0, 0};
                                    fits &= core::Add(core::HexCoord{0, armLength},
                                                      RotateTransportCell(relative, 1)) !=
                                            core::HexCoord{0, 0};
                                    fits &= core::Add(core::HexCoord{-armLength, armLength},
                                                      RotateTransportCell(relative, 2)) !=
                                            core::HexCoord{0, 0};
                                }
                                if (!fits) continue;
                                for (int pose = 0; pose < (enumerateOrientations ? 6 : 1); ++pose) {
                                    auto placed = plan;
                                    placed.armRotation = pose;
                                    auto place = [&](core::HexCoord cell, core::HexCoord center) {
                                        return core::Add(center,
                                                         RotateTransportCell(
                                                             core::Subtract(cell, center), pose));
                                    };
                                    placed.inputPosition =
                                        place(plan.inputPosition, plan.source.center);
                                    placed.outputPosition =
                                        place(plan.outputPosition, plan.destination.center);
                                    placed.inputRotation = (rotation + pose) % 6;
                                    placed.outputRotation = (outRotation + pose) % 6;
                                    placed.handoff.sourcePosition =
                                        place(plan.handoff.sourcePosition, plan.source.center);
                                    placed.handoff.destinationPosition = place(
                                        plan.handoff.destinationPosition, plan.destination.center);
                                    placed.handoff.rotation = (pipeRotation + pose) % 6;
                                    for (auto& atom : placed.handoff.payload.atoms)
                                        atom.position = RotateTransportCell(atom.position, pose);
                                    for (auto& bond : placed.handoff.payload.bonds) {
                                        bond.from = RotateTransportCell(bond.from, pose);
                                        bond.to = RotateTransportCell(bond.to, pose);
                                    }
                                    bool inside = true;
                                    for (int direction = 0; direction < 3; ++direction) {
                                        const auto tip =
                                            RotateTransportCell({armLength, 0}, pose + direction);
                                        inside &= ContainsProductionCell(
                                            placed.source, core::Add(placed.source.center, tip));
                                        inside &= ContainsProductionCell(
                                            placed.destination,
                                            core::Add(placed.destination.center, tip));
                                    }
                                    for (const auto& atom : input.atoms)
                                        inside &= ContainsProductionCell(
                                            placed.source,
                                            core::Add(placed.inputPosition,
                                                      RotateTransportCell(atom.position,
                                                                          placed.inputRotation)));
                                    for (const auto& atom : output.atoms)
                                        inside &= ContainsProductionCell(
                                            placed.destination,
                                            core::Add(placed.outputPosition,
                                                      RotateTransportCell(atom.position,
                                                                          placed.outputRotation)));
                                    for (const auto& atom : placed.handoff.payload.atoms) {
                                        inside &= ContainsProductionCell(
                                            placed.source, core::Add(placed.handoff.sourcePosition,
                                                                     atom.position));
                                        inside &= ContainsProductionCell(
                                            placed.destination,
                                            core::Add(placed.handoff.destinationPosition,
                                                      atom.position));
                                    }
                                    if (inside && (!accept || accept(placed))) return placed;
                                }
                            }
                    }
                }
    if (!accept) reject();
    return {};
}
} // namespace
ProductionTransportPlan PlanProductionTransport(const io::PuzzleData& p, bool canonicalRelay) {
    if (p.inputs.size() == 1 && p.inputs[0].atoms.size() > 2) {
        if (canonicalRelay)
            return PlanRigidTransport(
                p, [](const auto& plan) { return plan.armLength == 1 && plan.armRotation == 0; }, 1,
                false);
        return PlanRigidTransport(p);
    }
    const bool pair = p.inputs.size() == 1 && p.inputs[0].atoms.size() == 2;
    auto reject = [] {
        throw std::invalid_argument(
            "restricted production transport requires an unchanged single atom or bonded pair, or "
            "permitted singleton cardinal-to-salt calcification, two disjoint supported chambers "
            "and one matching conduit; unsupported chemistry remains excluded");
    };
    constexpr auto permissions = (1ULL << 0) | (1ULL << 22) | (1ULL << 23) | (1ULL << 25);
    if (p.version != 3 || !p.productionInfo || p.inputs.size() != 1 || p.outputs.size() != 1 ||
        p.outputMultiplier != 1 || (p.permissionBits & permissions) != permissions)
        reject();
    const auto& info = *p.productionInfo;
    if (info.chambers.size() != 2 || info.conduits.size() != 1) reject();
    auto bounded = [](core::HexCoord c) {
        return c.q >= -120 && c.q <= 120 && c.r >= -120 && c.r <= 120;
    };
    const auto layouts = PlanProductionChambers(p);
    const auto& conduit = info.conduits[0];
    for (auto cell : conduit.path)
        if (!bounded(cell)) reject();
    if (conduit.path.size() != (pair ? 2U : 1U) || conduit.path[0] != core::HexCoord{} ||
        (pair && core::Distance(conduit.path[0], conduit.path[1]) != 1) ||
        !bounded(conduit.positionA) || !bounded(conduit.positionB))
        reject();
    auto cabinet = [&](core::HexCoord c) {
        for (std::size_t i = 0; i < layouts.size(); ++i)
            if (ContainsProductionCell(layouts[i], c)) return static_cast<int>(i);
        return -1;
    };
    const int ca = cabinet(conduit.positionA), cb = cabinet(conduit.positionB);
    if (ca < 0 || cb < 0 || ca == cb) reject();
    for (auto cell : conduit.path)
        if (cabinet(core::Add(conduit.positionA, cell)) != ca ||
            cabinet(core::Add(conduit.positionB, cell)) != cb)
            reject();
    const auto count = pair ? 2U : 1U;
    for (const auto* m : {&p.inputs[0], &p.outputs[0]}) {
        if (m->atoms.size() != count || m->bonds.size() != (pair ? 1U : 0U)) reject();
        for (const auto& atom : m->atoms)
            if (!bounded(atom.position) ||
                !core::AtomTypeFromId(static_cast<std::uint8_t>(atom.type)) ||
                atom.type == core::AtomType::Repeat)
                reject();
        if (pair) {
            const auto& bond = m->bonds[0];
            const auto x = m->atoms[0].position, y = m->atoms[1].position;
            if (core::Distance(x, y) != 1 || bond.bits == 0 || bond.bits > 14 ||
                (bond.bits != 1 && (bond.bits & 1) != 0) ||
                !((bond.from == x && bond.to == y) || (bond.from == y && bond.to == x)))
                reject();
        }
    }
    const auto& input = p.inputs[0];
    const auto& output = p.outputs[0];
    int outputAnchor = 0;
    if (pair && input.atoms[0].type == output.atoms[1].type &&
        input.atoms[1].type == output.atoms[0].type)
        outputAnchor = 1;
    const bool calcification = !pair && output.atoms[0].type == core::AtomType::Salt &&
                               input.atoms[0].type >= core::AtomType::Air &&
                               input.atoms[0].type <= core::AtomType::Water &&
                               (p.permissionBits & (1ULL << 12));
    if ((!calcification && input.atoms[0].type != output.atoms[outputAnchor].type) ||
        (pair && (input.atoms[1].type != output.atoms[1 - outputAnchor].type ||
                  input.bonds[0].bits != output.bonds[0].bits)))
        reject();
    auto rotate = [](core::HexCoord c, int turns) {
        while (turns-- > 0)
            c = core::RotateClockwise(c);
        return c;
    };
    auto orientation = [&](core::HexCoord from, core::HexCoord to) {
        for (int i = 0; i < 6; ++i)
            if (rotate(from, i) == to) return i;
        reject();
        return 0;
    };
    const int pipeRotation = pair ? orientation(conduit.path[1], {-1, 0}) : 0;
    const int inputRotation =
        pair
            ? orientation(core::Subtract(input.atoms[1].position, input.atoms[0].position), {-1, 1})
            : 0;
    const int outputRotation =
        pair ? orientation(core::Subtract(output.atoms[1 - outputAnchor].position,
                                          output.atoms[outputAnchor].position),
                           {0, -1})
             : 0;

    ProductionTransportPlan plan;
    plan.source = layouts[ca];
    plan.destination = layouts[cb];
    plan.inputRotation = inputRotation;
    plan.outputRotation = outputRotation;
    plan.inputPosition = core::Subtract(core::Add(plan.source.center, {1, 0}),
                                        rotate(input.atoms[0].position, inputRotation));
    plan.outputPosition =
        core::Subtract(core::Add(plan.destination.center, {-1, 1}),
                       rotate(output.atoms[outputAnchor].position, outputRotation));
    auto& handoff = plan.handoff;
    handoff.conduitIndex = 0;
    handoff.conduitId = 100;
    handoff.localShape = conduit.path;
    handoff.sourcePosition = core::Add(plan.source.center, {0, 1});
    handoff.destinationPosition = core::Add(plan.destination.center, {0, 1});
    handoff.rotation = pipeRotation;
    handoff.payload = input;
    // One clockwise turn takes the input from the grab pose onto the conduit.
    auto payloadCell = [&](core::HexCoord cell) {
        return core::Subtract(
            core::Add(plan.source.center,
                      core::RotateClockwise(
                          core::Subtract(core::Add(plan.inputPosition, rotate(cell, inputRotation)),
                                         plan.source.center))),
            handoff.sourcePosition);
    };
    for (auto& atom : handoff.payload.atoms)
        atom.position = payloadCell(atom.position);
    for (auto& bond : handoff.payload.bonds) {
        bond.from = payloadCell(bond.from);
        bond.to = payloadCell(bond.to);
    }
    auto fits = [](const ProductionChamberLayout& chamber, core::HexCoord cell) {
        return ContainsProductionCell(chamber, cell);
    };
    for (const auto& atom : input.atoms)
        if (!fits(plan.source, core::Add(plan.inputPosition, rotate(atom.position, inputRotation))))
            reject();
    for (const auto& atom : output.atoms)
        if (!fits(plan.destination,
                  core::Add(plan.outputPosition, rotate(atom.position, outputRotation))))
            reject();
    for (const auto& atom : handoff.payload.atoms) {
        bool onConduit = false;
        for (auto cell : handoff.localShape)
            if (rotate(cell, handoff.rotation) == atom.position) onConduit = true;
        if (!onConduit || !fits(plan.source, core::Add(handoff.sourcePosition, atom.position)) ||
            !fits(plan.destination, core::Add(handoff.destinationPosition, atom.position)))
            reject();
    }
    handoff.releaseCycle = 2;
    handoff.pickupCycle = 4;
    handoff.period = 8;
    return plan;
}

namespace {
io::SolutionData CompileTransportPlan(const io::PuzzleData& p,
                                      const ProductionTransportPlan& plan) {
    ValidateProductionTransportMaterial(p, plan);
    const auto& handoff = plan.handoff;
    io::SolutionData result;
    result.puzzleName = p.name;
    result.solutionName = "Restricted production conduit transport";
    auto add = [&](const char* name, core::HexCoord position, std::vector<int> body,
                   std::vector<io::ArmInstruction> tape = {}) {
        result.objects.push_back({name, position, std::move(tape), std::move(body)});
    };
    // Conduit endpoints can move inside their prescribed cabinets, but their
    // identity and local shape must remain unchanged (omsim production rules).
    for (auto position : {handoff.sourcePosition, handoff.destinationPosition}) {
        std::vector<int> body{1,
                              handoff.rotation,
                              0,
                              0,
                              0,
                              handoff.conduitId,
                              static_cast<int>(handoff.localShape.size())};
        for (auto cell : handoff.localShape) {
            body.push_back(cell.q);
            body.push_back(cell.r);
        }
        add("pipe", position, std::move(body));
    }
    add("input", plan.inputPosition, {1, plan.inputRotation, 0, 0, 0});
    add("out-std", plan.outputPosition, {1, plan.outputRotation, 0, 0, 0});
    if (p.inputs[0].atoms[0].type != p.outputs[0].atoms[0].type && p.inputs[0].atoms.size() == 1)
        add("glyph-calcification", handoff.destinationPosition, {1, 0, 0, 0, 0});
    add("arm1", plan.source.center, {plan.armLength, plan.armRotation, 0, 5, 0},
        {{'G', 0},
         {'r', handoff.releaseCycle - 1},
         {'g', handoff.releaseCycle},
         {'R', handoff.releaseCycle + 1},
         {'O', handoff.period - 1}});
    add("arm1", plan.destination.center, {plan.armLength, (1 + plan.armRotation) % 6, 0, 4, 1},
        {{'G', handoff.pickupCycle},
         {'r', handoff.pickupCycle + 1},
         {'g', handoff.pickupCycle + 2},
         {'R', handoff.pickupCycle + 3}});
    return result;
}
} // namespace

io::SolutionData CompileProductionTransport(const io::PuzzleData& p) {
    return CompileTransportPlan(p, PlanProductionTransport(p));
}

std::optional<io::SolutionData> CompileAcceptedProductionCoproducts(
    const io::PuzzleData& p, const std::function<bool(const io::SolutionData&)>& accept) {
    auto reject = [] {
        throw std::invalid_argument(
            "production coproduct transport requires one connected feed, connected outputs "
            "consuming every atom, and permitted destination unbonding/chemistry");
    };
    const bool singleton = std::all_of(p.outputs.begin(), p.outputs.end(),
                                       [](const auto& output) { return output.atoms.size() == 1; });
    if (singleton) {
        if (p.inputs.size() != 1 || p.outputs.size() < 2 || p.outputs.size() > 16 ||
            p.outputs.size() != p.inputs[0].atoms.size() || !(p.permissionBits & (1ULL << 9)))
            reject();
        std::vector<int> owner(p.outputs.size(), -1);
        std::function<bool(std::size_t, std::vector<bool>&)> match =
            [&](std::size_t atom, std::vector<bool>& visited) {
                const auto type = p.inputs[0].atoms[atom].type;
                for (std::size_t output = 0; output < p.outputs.size(); ++output) {
                    const auto target = p.outputs[output].atoms[0].type;
                    const bool calcify =
                        target == core::AtomType::Salt && type >= core::AtomType::Air &&
                        type <= core::AtomType::Water && (p.permissionBits & (1ULL << 12));
                    if (visited[output] || (type != target && !calcify)) continue;
                    visited[output] = true;
                    if (owner[output] >= 0 &&
                        !match(static_cast<std::size_t>(owner[output]), visited))
                        continue;
                    owner[output] = static_cast<int>(atom);
                    return true;
                }
                return false;
            };
        for (std::size_t atom = 0; atom < p.inputs[0].atoms.size(); ++atom) {
            std::vector<bool> visited(p.outputs.size());
            if (!match(atom, visited)) reject();
        }
    }
    {
        if (p.inputs.size() != 1 || p.outputs.size() < 2 || p.outputs.size() > 16) reject();
        const auto& feed = p.inputs[0];
        std::size_t count = 0;
        for (const auto& output : p.outputs) {
            if (output.atoms.empty()) reject();
            count += output.atoms.size();
            for (const auto& atom : output.atoms)
                if (atom.type == core::AtomType::Repeat ||
                    !core::AtomTypeFromId(static_cast<std::uint8_t>(atom.type)) ||
                    atom.position.q < -120 || atom.position.q > 120 || atom.position.r < -120 ||
                    atom.position.r > 120)
                    reject();
            for (const auto& bond : output.bonds) {
                const auto exists = [&](auto cell) {
                    return std::count_if(output.atoms.begin(), output.atoms.end(),
                                         [&](const auto& atom) { return atom.position == cell; }) ==
                           1;
                };
                if (!exists(bond.from) || !exists(bond.to) ||
                    core::Distance(bond.from, bond.to) != 1 || bond.bits == 0 || bond.bits > 14 ||
                    (bond.bits != 1 && (bond.bits & 1)) ||
                    std::count_if(output.bonds.begin(), output.bonds.end(), [&](const auto& other) {
                        return (bond.from == other.from && bond.to == other.to) ||
                               (bond.from == other.to && bond.to == other.from);
                    }) != 1)
                    reject();
            }
            std::vector<core::HexCoord> reached{output.atoms[0].position};
            for (std::size_t i = 0; i < reached.size(); ++i)
                for (const auto& bond : output.bonds) {
                    const auto next = bond.from == reached[i] ? bond.to
                                      : bond.to == reached[i] ? bond.from
                                                              : reached[i];
                    if (std::find(reached.begin(), reached.end(), next) == reached.end())
                        reached.push_back(next);
                }
            if (reached.size() != output.atoms.size()) reject();
        }
        if (count != feed.atoms.size()) reject();
        auto transport = p;
        transport.outputs = transport.inputs;
        std::optional<io::SolutionData> result;
        std::size_t attempts = 0, states = 0;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        for (int armLength : {1, 2, 3}) {
            const auto lengthLimit =
                std::min<std::size_t>(64, attempts + (armLength == 3 ? 22 : 21));
            const auto stateLimit =
                std::min<std::size_t>(4096, states + (armLength == 3 ? 1366 : 1365));
            (void)PlanRigidTransport(
                transport,
                [&](const ProductionTransportPlan& plan) {
                    auto exhausted = [&] {
                        return attempts >= lengthLimit || states >= stateLimit ||
                               std::chrono::steady_clock::now() >= deadline;
                    };
                    if (exhausted()) return true;
                    const auto& payload = plan.handoff.payload;
                    std::vector<int> owner(feed.atoms.size(), -1);
                    std::vector<io::SolutionObject> ports;
                    std::vector<std::pair<core::HexCoord, core::HexCoord>> stagedCuts;
                    auto bitsAt = [](const auto& molecule, auto a, auto b) {
                        int bits = 0;
                        for (const auto& bond : molecule.bonds)
                            if ((bond.from == a && bond.to == b) ||
                                (bond.from == b && bond.to == a))
                                bits |= bond.bits;
                        return bits;
                    };
                    std::function<bool(std::size_t)> place = [&](std::size_t outputIndex) {
                        if (exhausted()) return false;
                        ++states;
                        if (outputIndex == p.outputs.size()) {
                            auto candidate = CompileTransportPlan(transport, plan);
                            candidate.solutionName = "Joint molecular coproduct transport";
                            std::erase_if(candidate.objects, [&](const auto& object) {
                                return object.typeName == "out-std" ||
                                       (object.typeName == "arm1" &&
                                        object.position == plan.destination.center);
                            });
                            for (const auto& bond : payload.bonds) {
                                const auto a =
                                    std::find_if(payload.atoms.begin(), payload.atoms.end(),
                                                 [&](const auto& atom) {
                                                     return atom.position == bond.from;
                                                 }) -
                                    payload.atoms.begin();
                                const auto b =
                                    std::find_if(payload.atoms.begin(), payload.atoms.end(),
                                                 [&](const auto& atom) {
                                                     return atom.position == bond.to;
                                                 }) -
                                    payload.atoms.begin();
                                if (owner[a] == owner[b]) continue;
                                if (!(p.permissionBits & (1ULL << 9))) return false;
                                int direction = 0;
                                while (direction < 6 &&
                                       core::Add(bond.from,
                                                 RotateTransportCell({1, 0}, direction)) != bond.to)
                                    ++direction;
                                if (direction == 6) return false;
                                candidate.objects.push_back(
                                    {"unbonder",
                                     core::Add(plan.handoff.destinationPosition, bond.from),
                                     {},
                                     {1, direction, 0, 0, 0}});
                            }
                            candidate.objects.insert(candidate.objects.end(), ports.begin(),
                                                     ports.end());
                            if (stagedCuts.empty()) {
                                try {
                                    ValidateProductionCoproductMaterial(p, plan, owner, ports);
                                } catch (const ExecutionIR::Error&) {
                                    return false;
                                }
                            }
                            if (!stagedCuts.empty()) {
                                std::size_t configurations = 1;
                                for (std::size_t i = 0;
                                     i < payload.atoms.size() && configurations < 4096; ++i)
                                    configurations =
                                        std::min<std::size_t>(4096, configurations * 6);
                                for (std::size_t configuration = 0;
                                     configuration < configurations && !exhausted();
                                     ++configuration)
                                    for (int travel = 0; travel < 6 && !exhausted(); ++travel) {
                                        auto staged = candidate;
                                        auto grips = configuration;
                                        ++states;
                                        std::vector<core::HexCoord> trackCells;
                                        const auto step = RotateTransportCell({1, 0}, travel);
                                        const core::HexCoord displacement{3 * step.q, 3 * step.r};
                                        bool inside = true;
                                        // Every atom has independent custody before any bond is
                                        // cut.
                                        for (std::size_t i = 0; i < payload.atoms.size(); ++i) {
                                            const int grip = grips % 6;
                                            grips /= 6;
                                            const auto unitGrip = RotateTransportCell({1, 0}, grip);
                                            const core::HexCoord gripOffset{2 * unitGrip.q,
                                                                            2 * unitGrip.r};
                                            const auto cell =
                                                core::Add(plan.handoff.destinationPosition,
                                                          payload.atoms[i].position);
                                            const auto base = core::Subtract(cell, gripOffset);
                                            io::SolutionObject track{
                                                "track", base, {}, {1, 0, 0, 0, 4}};
                                            for (int distance = 0; distance <= 3; ++distance) {
                                                const core::HexCoord offset{distance * step.q,
                                                                            distance * step.r};
                                                const auto position = core::Add(base, offset);
                                                inside &=
                                                    ContainsProductionCell(plan.destination,
                                                                           position) &&
                                                    ContainsProductionCell(
                                                        plan.destination,
                                                        core::Add(cell, offset)) &&
                                                    std::find(trackCells.begin(), trackCells.end(),
                                                              position) == trackCells.end();
                                                trackCells.push_back(position);
                                                track.bodyData.push_back(offset.q);
                                                track.bodyData.push_back(offset.r);
                                            }
                                            track.bodyData.push_back(0);
                                            staged.objects.push_back(std::move(track));
                                            io::SolutionObject arm{
                                                "arm1",
                                                base,
                                                {},
                                                {2, grip, 0, 9, static_cast<int>(i + 1)}};
                                            arm.instructions = {{'G', 4},  {'A', 5},  {'A', 6},
                                                                {'A', 7},  {'g', 8},  {'a', 9},
                                                                {'a', 10}, {'a', 11}, {'O', 15}};
                                            staged.objects.push_back(std::move(arm));
                                        }
                                        if (!inside) continue;
                                        const auto portBegin = staged.objects.size() -
                                                               payload.atoms.size() * 2 -
                                                               ports.size();
                                        for (std::size_t i = portBegin;
                                             i < portBegin + ports.size(); ++i)
                                            staged.objects[i].position =
                                                core::Add(staged.objects[i].position, displacement);
                                        for (const auto& [from, to] : stagedCuts) {
                                            int direction = 0;
                                            while (direction < 6 &&
                                                   core::Add(from, RotateTransportCell(
                                                                       {1, 0}, direction)) != to)
                                                ++direction;
                                            if (direction == 6) {
                                                inside = false;
                                                break;
                                            }
                                            staged.objects.push_back(
                                                {"unbonder",
                                                 core::Add(plan.handoff.destinationPosition, from),
                                                 {},
                                                 {1, direction, 0, 0, 0}});
                                        }
                                        if (!inside) continue;
                                        for (auto& object : staged.objects)
                                            if (object.typeName == "arm1" &&
                                                object.position == plan.source.center) {
                                                object.instructions.back() = {'O', 15};
                                            }
                                        ++attempts;
                                        if (!accept(staged)) continue;
                                        result = std::move(staged);
                                        return true;
                                    }
                                return false;
                            }
                            ++attempts;
                            if (!accept(candidate)) return false;
                            result = std::move(candidate);
                            return true;
                        }
                        const auto& output = p.outputs[outputIndex];
                        for (std::size_t anchor = 0; anchor < payload.atoms.size(); ++anchor) {
                            if (owner[anchor] >= 0) continue;
                            const int rotations = output.atoms.size() == 1 ? 1 : 6;
                            for (int rotation = 0; rotation < rotations; ++rotation) {
                                if (exhausted()) return false;
                                const auto delta = core::Subtract(
                                    payload.atoms[anchor].position,
                                    RotateTransportCell(output.atoms[0].position, rotation));
                                std::vector<std::size_t> mapped;
                                bool fits = true;
                                for (const auto& atom : output.atoms) {
                                    const auto cell = core::Add(
                                        delta, RotateTransportCell(atom.position, rotation));
                                    const auto found = std::find_if(
                                        payload.atoms.begin(), payload.atoms.end(),
                                        [&](const auto& value) { return value.position == cell; });
                                    if (found == payload.atoms.end()) {
                                        fits = false;
                                        break;
                                    }
                                    const auto index =
                                        static_cast<std::size_t>(found - payload.atoms.begin());
                                    const bool calcify = atom.type == core::AtomType::Salt &&
                                                         found->type >= core::AtomType::Air &&
                                                         found->type <= core::AtomType::Water &&
                                                         (p.permissionBits & (1ULL << 12));
                                    if (owner[index] >= 0 ||
                                        (atom.type != found->type && !calcify) ||
                                        std::find(mapped.begin(), mapped.end(), index) !=
                                            mapped.end()) {
                                        fits = false;
                                        break;
                                    }
                                    mapped.push_back(index);
                                }
                                if (!fits) continue;
                                for (std::size_t a = 0; a < mapped.size(); ++a)
                                    for (std::size_t b = a + 1; b < mapped.size(); ++b)
                                        if (const int required =
                                                bitsAt(output, output.atoms[a].position,
                                                       output.atoms[b].position),
                                            existing =
                                                bitsAt(payload, payload.atoms[mapped[a]].position,
                                                       payload.atoms[mapped[b]].position);
                                            required != existing &&
                                            !(required == 1 && existing == 0 &&
                                              (p.permissionBits & (1ULL << 8))) &&
                                            !(required == 0 && existing != 0 &&
                                              (p.permissionBits & (1ULL << 9))) &&
                                            !(existing != 0 && required != 0 &&
                                              (p.permissionBits & (1ULL << 9)) &&
                                              (p.permissionBits & (1ULL << 3)) &&
                                              ((required == 1 &&
                                                (p.permissionBits & (1ULL << 8))) ||
                                               (!(required & 1) &&
                                                (p.permissionBits & (1ULL << 11)) &&
                                                payload.atoms[mapped[a]].type ==
                                                    core::AtomType::Fire &&
                                                payload.atoms[mapped[b]].type ==
                                                    core::AtomType::Fire))) &&
                                            !(required != 0 && !(required & 1) &&
                                              !(existing & ~required) &&
                                              (p.permissionBits & (1ULL << 11)) &&
                                              payload.atoms[mapped[a]].type ==
                                                  core::AtomType::Fire &&
                                              payload.atoms[mapped[b]].type ==
                                                  core::AtomType::Fire))
                                            fits = false;
                                if (!fits) continue;
                                const auto saved = ports.size();
                                const auto savedCuts = stagedCuts.size();
                                for (std::size_t a = 0; a < mapped.size(); ++a)
                                    for (std::size_t b = a + 1; b < mapped.size(); ++b) {
                                        const auto from = payload.atoms[mapped[a]].position;
                                        const auto to = payload.atoms[mapped[b]].position;
                                        const int required =
                                            bitsAt(output, output.atoms[a].position,
                                                   output.atoms[b].position);
                                        const int existing = bitsAt(payload, from, to);
                                        const bool staged =
                                            existing != 0 && required != 0 &&
                                            required != existing &&
                                            (required == 1 || (existing & ~required));
                                        if (staged) stagedCuts.emplace_back(from, to);
                                        const int retained = staged ? 0 : existing;
                                        const bool add = required == 1 && retained == 0;
                                        const bool remove = required == 0 && existing != 0;
                                        if (required != retained && required != 0 &&
                                            !(required & 1) && !(retained & ~required)) {
                                            for (const int color : {2, 4, 8}) {
                                                if (!(required & color) || (retained & color))
                                                    continue;
                                                bool placed = false;
                                                for (int rotation = 0; rotation < 6 && !placed;
                                                     ++rotation)
                                                    for (int edge = 0; edge < 3 && !placed;
                                                         ++edge) {
                                                        const core::HexCoord vertices[] = {
                                                            RotateTransportCell({0, 0}, rotation),
                                                            RotateTransportCell({0, 1}, rotation),
                                                            RotateTransportCell({1, 0}, rotation)};
                                                        const int colors[] = {8, 2, 4};
                                                        if (colors[edge] != color) continue;
                                                        for (const bool reverse : {false, true}) {
                                                            const auto origin =
                                                                core::Subtract(reverse ? to : from,
                                                                               vertices[edge]);
                                                            if (core::Add(
                                                                    origin,
                                                                    vertices[(edge + 1) % 3]) !=
                                                                (reverse ? from : to))
                                                                continue;
                                                            const auto third = core::Add(
                                                                origin, vertices[(edge + 2) % 3]);
                                                            const auto thirdAtom = std::find_if(
                                                                payload.atoms.begin(),
                                                                payload.atoms.end(),
                                                                [&](const auto& atom) {
                                                                    return atom.position == third &&
                                                                           atom.type ==
                                                                               core::AtomType::Fire;
                                                                });
                                                            if (thirdAtom != payload.atoms.end()) {
                                                                const auto thirdIndex =
                                                                    static_cast<std::size_t>(
                                                                        thirdAtom -
                                                                        payload.atoms.begin());
                                                                const auto thirdMapped = std::find(
                                                                    mapped.begin(), mapped.end(),
                                                                    thirdIndex);
                                                                if (thirdMapped == mapped.end())
                                                                    continue;
                                                                const auto c =
                                                                    static_cast<std::size_t>(
                                                                        thirdMapped -
                                                                        mapped.begin());
                                                                const auto safeSide =
                                                                    [&](std::size_t endpoint,
                                                                        int sideColor) {
                                                                        const int target = bitsAt(
                                                                            output,
                                                                            output.atoms[endpoint]
                                                                                .position,
                                                                            output.atoms[c]
                                                                                .position);
                                                                        const int source = bitsAt(
                                                                            payload,
                                                                            payload
                                                                                .atoms
                                                                                    [mapped
                                                                                         [endpoint]]
                                                                                .position,
                                                                            third);
                                                                        return (target &
                                                                                sideColor) ||
                                                                               (target == 1 &&
                                                                                source == 1);
                                                                    };
                                                                if (!safeSide(
                                                                        reverse ? b : a,
                                                                        colors[(edge + 2) % 3]) ||
                                                                    !safeSide(
                                                                        reverse ? a : b,
                                                                        colors[(edge + 1) % 3]))
                                                                    continue;
                                                            }
                                                            ports.push_back(
                                                                {"bonder-prisma",
                                                                 core::Add(plan.handoff
                                                                               .destinationPosition,
                                                                           origin),
                                                                 {},
                                                                 {1, rotation, 0, 0, 0}});
                                                            placed = true;
                                                            break;
                                                        }
                                                    }
                                                if (!placed) {
                                                    fits = false;
                                                    break;
                                                }
                                            }
                                            if (!fits) break;
                                        }
                                        if (!add && !remove) continue;
                                        int direction = 0;
                                        while (direction < 6 &&
                                               core::Add(from, RotateTransportCell(
                                                                   {1, 0}, direction)) != to)
                                            ++direction;
                                        if (direction == 6) {
                                            fits = false;
                                            break;
                                        }
                                        ports.push_back(
                                            {add ? "bonder" : "unbonder",
                                             core::Add(plan.handoff.destinationPosition, from),
                                             {},
                                             {1, direction, 0, 0, 0}});
                                    }
                                if (!fits) {
                                    ports.resize(saved);
                                    stagedCuts.resize(savedCuts);
                                    continue;
                                }
                                ports.push_back(
                                    {"out-std",
                                     core::Add(plan.handoff.destinationPosition, delta),
                                     {},
                                     {1, rotation, static_cast<int>(outputIndex), 0, 0}});
                                for (std::size_t i = 0; i < mapped.size(); ++i) {
                                    owner[mapped[i]] = static_cast<int>(outputIndex);
                                    if (payload.atoms[mapped[i]].type != output.atoms[i].type)
                                        ports.push_back(
                                            {"glyph-calcification",
                                             core::Add(plan.handoff.destinationPosition,
                                                       payload.atoms[mapped[i]].position),
                                             {},
                                             {1, 0, 0, 0, 0}});
                                }
                                if (place(outputIndex + 1)) return true;
                                for (const auto index : mapped)
                                    owner[index] = -1;
                                ports.resize(saved);
                                stagedCuts.resize(savedCuts);
                            }
                        }
                        return false;
                    };
                    return place(0) || exhausted();
                },
                armLength);
            if (result || attempts >= 64 || states >= 4096 ||
                std::chrono::steady_clock::now() >= deadline)
                break;
        }
        return result;
    }
}

std::optional<io::SolutionData> CompileAcceptedProductionTransport(
    const io::PuzzleData& p, const std::function<bool(const io::SolutionData&)>& accept,
    bool enumerateOrientations) {
    std::optional<io::SolutionData> result;
    bool rigid = p.inputs.size() == 1 && p.inputs[0].atoms.size() > 1;
    std::optional<ProductionTransportPlan> simplePlan;
    if (!rigid) {
        try {
            simplePlan = PlanProductionTransport(p);
        } catch (const std::invalid_argument&) {
            if (p.inputs.size() != 1 || p.inputs[0].atoms.size() != 2) throw;
            rigid = true;
        }
    }
    if (rigid) {
        std::size_t attempts = 0;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        for (int armLength : {1, 2, 3}) {
            const auto lengthLimit =
                std::min<std::size_t>(64, attempts + (armLength == 3 ? 22 : 21));
            (void)PlanRigidTransport(
                p,
                [&](const ProductionTransportPlan& plan) {
                    if (attempts >= lengthLimit || std::chrono::steady_clock::now() >= deadline)
                        return true;
                    ++attempts;
                    auto candidate = CompileTransportPlan(p, plan);
                    if (!accept(candidate)) return false;
                    result = std::move(candidate);
                    return true;
                },
                armLength, enumerateOrientations);
            if (result || attempts >= 64 || std::chrono::steady_clock::now() >= deadline) break;
        }
    } else {
        const auto& plan = *simplePlan;
        const auto canonical = CompileTransportPlan(p, plan);
        for (int pose = 0; pose < 6; ++pose) {
            auto candidate = canonical;
            bool fits = true;
            for (auto& object : candidate.objects) {
                auto anchor = object.position;
                if (object.typeName == "input" || object.typeName == "out-std") {
                    auto cell =
                        (object.typeName == "input" ? p.inputs[0] : p.outputs[0]).atoms[0].position;
                    for (int turn = 0; turn < object.bodyData.at(1); ++turn)
                        cell = core::RotateClockwise(cell);
                    anchor = core::Add(anchor, cell);
                }
                const auto& chamber =
                    ContainsProductionCell(plan.source, anchor) ? plan.source : plan.destination;
                auto relative = core::Subtract(object.position, chamber.center);
                for (int turn = 0; turn < pose; ++turn)
                    relative = core::RotateClockwise(relative);
                object.position = core::Add(chamber.center, relative);
                object.bodyData.at(1) = (object.bodyData.at(1) + pose) % 6;
                auto contains = [&](core::HexCoord cell) {
                    for (int turn = 0; turn < object.bodyData.at(1); ++turn)
                        cell = core::RotateClockwise(cell);
                    return ContainsProductionCell(chamber, core::Add(object.position, cell));
                };
                if (object.typeName == "input" || object.typeName == "out-std") {
                    const auto& molecule = object.typeName == "input" ? p.inputs[0] : p.outputs[0];
                    for (const auto& atom : molecule.atoms)
                        fits &= contains(atom.position);
                } else if (object.typeName == "pipe") {
                    for (auto cell : plan.handoff.localShape)
                        fits &= contains(cell);
                } else
                    fits &= contains({});
            }
            if (fits && accept(candidate)) {
                result = std::move(candidate);
                break;
            }
        }
    }
    return result;
}
} // namespace opusoptima::solvers
