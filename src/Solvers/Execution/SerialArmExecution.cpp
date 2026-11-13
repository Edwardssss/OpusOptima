#include "SerialArmExecution.hpp"
#include <cstdio>
#include "CandidateIdentity.hpp"
#include "FixedArmIntervals.hpp"
#include "FragmentCustody.hpp"
#include "HexFrame.hpp"
#include "SerialFragmentMotion.hpp"
#include "SerialPortAccess.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <limits>
#include <map>
#include <queue>
#include <set>
#include <string>

namespace opusoptima::solvers {
void RequireFinitePhysicalTerminal(const ExecutionIR::State& state,
                                   const ExecutionIR::FiniteRetention& terminal,
                                   const std::map<ExecutionIR::AtomId, core::HexCoord>& targets) {
    ExecutionIR::RequireFiniteTerminal(state, terminal);
    if (targets.size() != terminal.slots.size())
        throw ExecutionIR::LayoutFailure("terminal retention target count mismatch");
    for (const auto& slot : terminal.slots) {
        const auto target = targets.find(slot.atom);
        if (target == targets.end() || state.atoms.at(slot.atom).position != target->second)
            throw ExecutionIR::LayoutFailure("terminal retention departed bound endpoint");
    }
}

namespace {
using IR = ExecutionIR;
using K = IR::Kind;
using Cell = std::pair<int, int>;
using Id = IR::AtomId;
constexpr std::array<Cell, 6> directions{{{1, 0}, {0, 1}, {-1, 1}, {-1, 0}, {0, -1}, {1, -1}}};
Cell Add(Cell a, Cell b) {
    return {a.first + b.first, a.second + b.second};
}
Cell Sub(Cell a, Cell b) {
    return {a.first - b.first, a.second - b.second};
}
Cell Hex(core::HexCoord c) {
    return {c.q, c.r};
}
core::HexCoord Hex(Cell c) {
    return {c.first, c.second};
}

struct PrismPlacement {
    Cell origin;
    int turn{};
    std::array<Cell, 3> vertices;
};
constexpr std::array<int, 3> prismColors{8, 2, 4};

std::vector<PrismPlacement> SelectPrisms(const std::map<Id, Cell>& sites,
                                         const std::map<IR::Edge, std::uint8_t>& bonds,
                                         const std::vector<std::map<Id, Cell>>& incoming) {
    std::vector<PrismPlacement> result;
    for (const auto& [edge, bits] : bonds)
        for (int color : {2, 4, 8})
            if (bits & color) {
                const auto a = sites.at(edge.a), b = sites.at(edge.b);
                bool placed = false;
                for (int turn = 0; turn < 6 && !placed; ++turn) {
                    const std::array<Cell, 3> vertices{
                        {{0, 0},
                         Hex(core::HexFrame{{}, turn}.ToWorld({0, 1})),
                         Hex(core::HexFrame{{}, turn}.ToWorld({1, 0}))}};
                    for (int side = 0; side < 3 && !placed; ++side)
                        for (bool reverse : {false, true}) {
                            if (prismColors[side] != color) continue;
                            const auto origin = Sub(reverse ? b : a, vertices[side]);
                            if (Add(origin, vertices[(side + 1) % 3]) != (reverse ? a : b))
                                continue;
                            const auto third = Add(origin, vertices[(side + 2) % 3]);
                            if (std::any_of(sites.begin(), sites.end(),
                                            [&](const auto& atom) { return atom.second == third; }))
                                continue;
                            bool safe = true;
                            for (const auto& pose : incoming) {
                                std::map<Cell, Id> occupied;
                                for (const auto& [id, cell] : pose)
                                    occupied.emplace(cell, id);
                                for (int i = 0; i < 3; ++i) {
                                    const auto x = occupied.find(Add(origin, vertices[i]));
                                    const auto y =
                                        occupied.find(Add(origin, vertices[(i + 1) % 3]));
                                    if (x == occupied.end() || y == occupied.end()) continue;
                                    const IR::Edge pair{std::min(x->second, y->second),
                                                        std::max(x->second, y->second)};
                                    if (!bonds.contains(pair) || !(bonds.at(pair) & prismColors[i]))
                                        safe = false;
                                }
                            }
                            if (!safe) continue;
                            auto world = vertices;
                            for (auto& cell : world)
                                cell = Add(origin, cell);
                            result.push_back({origin, turn, world});
                            placed = true;
                            break;
                        }
                }
                if (!placed) throw IR::LayoutFailure("cannot place selective precursor prism");
            }
    return result;
}

// Each occurrence binds material identities to the actual output pose and glyphs.
struct ColoredOutputStation {
    std::size_t output{}, step{};
    std::map<Id, Cell> sites;
    std::map<IR::Edge, std::uint8_t> bonds;
    std::vector<PrismPlacement> glyphs;
};

io::SolutionData CompileWholeFragmentDock(const io::PuzzleData& p, const ExecutionPlan& plan,
                                          std::chrono::steady_clock::time_point deadline) {
    if (!plan.finiteTerminal || !plan.finiteTerminal->slots.empty() || p.inputs.size() != 1 ||
        p.outputs.size() != 1 || p.inputs[0].atoms.size() < 2 || p.inputs[0].atoms.size() > 12 ||
        p.inputs[0].bonds.empty())
        throw IR::Unsupported(
            "fragment dock requires complete connected ordinary feed/output and no retained stock");
    for (const auto& bond : p.inputs[0].bonds)
        if (bond.bits != 1) throw IR::Unsupported("fragment dock requires ordinary internal bonds");
    for (const auto& step : plan.material.steps)
        if (step.operation.kind != K::Acquire && step.operation.kind != K::Relocate &&
            step.operation.kind != K::Output)
            throw IR::Unsupported("fragment dock cannot assemble a noncongruent chemistry graph");
    if (p.inputs[0].atoms.size() != p.outputs[0].atoms.size() ||
        p.inputs[0].bonds.size() != p.outputs[0].bonds.size())
        throw IR::Unsupported("fragment dock requires identical local graph frames");
    for (const auto& a : p.inputs[0].atoms)
        if (std::none_of(p.outputs[0].atoms.begin(), p.outputs[0].atoms.end(), [&](const auto& b) {
                return a.position == b.position && a.type == b.type;
            }))
            throw IR::Unsupported("fragment dock requires identical local graph frames");
    for (const auto& a : p.inputs[0].bonds)
        if (std::none_of(p.outputs[0].bonds.begin(), p.outputs[0].bonds.end(), [&](const auto& b) {
                return a.bits == b.bits &&
                       ((a.from == b.from && a.to == b.to) || (a.from == b.to && a.to == b.from));
            }))
            throw IR::Unsupported("fragment dock internal edge mismatch");
    auto probe = IR::Initial(p);
    IR proof;
    const auto probeId = probe.nextId;
    proof.AppendFromTail(p, probe, {K::Acquire, 0});
    if (IR::Fragment(probe, probeId).size() != p.inputs[0].atoms.size())
        throw IR::Unsupported("fragment dock disconnected feed");
    proof.AppendFromTail(p, probe, {K::Output, 0, {probeId}});
    const core::HexCoord output{18, 0};
    std::size_t candidates = 0;
    for (const auto& atom : p.outputs[0].atoms)
        for (int radius = 1; radius <= 3; ++radius) {
            for (int direction = 0; direction < 6; ++direction) {
                if (++candidates > 18)
                    throw IR::LayoutFailure("fragment dock bounded layouts rejected");
                if (std::chrono::steady_clock::now() >= deadline)
                    throw IR::BudgetFailure("shared material/physical deadline exhausted");
                const auto targetGrip = core::Add(output, atom.position);
                core::HexCoord offset{radius, 0};
                for (int i = 0; i < direction; ++i)
                    offset = core::RotateClockwise(offset);
                const auto base = core::Subtract(targetGrip, offset);
                const core::HexFrame inverse{{}, 5};
                const auto sourceOrigin =
                    core::Add(base, inverse.ToWorld(core::Subtract(output, base)));
                const core::HexFrame source{sourceOrigin, 5};
                std::vector<core::HexCoord> feed;
                bool collision = false;
                for (const auto& a : p.inputs[0].atoms) {
                    feed.push_back(source.ToWorld(a.position));
                    collision |= feed.back() == base;
                    for (const auto& b : p.outputs[0].atoms)
                        collision |= feed.back() == core::Add(output, b.position);
                }
                for (const auto& a : p.outputs[0].atoms)
                    collision |= core::Add(output, a.position) == base;
                if (collision) continue;
                io::SolutionData machine;
                machine.puzzleName = p.name;
                machine.solutionName = "Finite complete-fragment docking";
                machine.objects.push_back({"input", sourceOrigin, {}, {1, 5, 0, 0, 0}});
                machine.objects.push_back({"out-std", output, {}, {1, 0, 0, 0, 0}});
                const int initial = (direction + 5) % 6;
                machine.objects.push_back({"arm1", base, {}, {radius, initial, 0, 0, 0}});
                auto& tape = machine.objects.back().instructions;
                auto state = IR::Initial(p);
                IR physical;
                FragmentCustody custody;
                int tick = 0;
                bool legal = true;
                for (const auto& step : plan.material.steps) {
                    const auto& op = step.operation;
                    if (op.kind == K::Relocate) continue;
                    if (op.kind == K::Acquire) {
                        if (!state.atoms.empty()) {
                            legal = false;
                            break;
                        }
                        physical.AppendFromTail(p, state, {K::Acquire, 0, {}, 0, 5, sourceOrigin});
                    } else {
                        const auto id = op.atoms.at(0);
                        const auto grip = source.ToWorld(atom.position);
                        auto held = std::find_if(
                            state.atoms.begin(), state.atoms.end(),
                            [&](const auto& entry) { return entry.second.position == grip; });
                        if (held == state.atoms.end() ||
                            !CertifyFixedArmInterval(state, base, feed, {0, radius, initial, 0},
                                                     {0, radius, direction, 1}, 'r', true)) {
                            legal = false;
                            break;
                        }
                        custody.Grab(state, 0, grip, held->first);
                        tape.push_back({'G', tick++});
                        const auto rotatedBase = core::RotateClockwise(base);
                        physical.AppendFromTail(
                            p, state,
                            {K::Relocate, 0, {id}, 0, 1, core::Subtract(base, rotatedBase)});
                        tape.push_back({'r', tick++});
                        custody.Validate(state);
                        for (const auto& a : p.outputs[0].atoms)
                            if (std::none_of(state.atoms.begin(), state.atoms.end(),
                                             [&](const auto& entry) {
                                                 return entry.second.position ==
                                                            core::Add(output, a.position) &&
                                                        entry.second.type == a.type;
                                             }))
                                throw IR::Error("fragment dock final physical frame mismatch");
                        custody.Drop(0);
                        tape.push_back({'g', tick++});
                        // This restricted domain has no retained stock; use the same physical
                        // terminal gate.
                        auto preview = physical;
                        auto projected = state;
                        preview.AppendFromTail(p, projected, {K::Output, 0, {id}});
                        if (projected.outputs == plan.finiteTerminal->outputs)
                            RequireFinitePhysicalTerminal(projected, *plan.finiteTerminal, {});
                        physical.AppendFromTail(p, state, {K::Output, 0, {id}});
                        custody.Validate(state);
                        auto renewable = IR::Initial(p);
                        IR renewal;
                        renewal.AppendFromTail(p, renewable,
                                               {K::Acquire, 0, {}, 0, 5, sourceOrigin});
                        if (!CertifyFixedArmInterval(renewable, base, feed,
                                                     {0, radius, direction, 0},
                                                     {0, radius, initial, 5}, 'R', false)) {
                            legal = false;
                            break;
                        }
                        tape.push_back({'R', tick++});
                    }
                }
                if (!legal) continue;
                IR::RequireFiniteTerminal(state, *plan.finiteTerminal);
                IR::RequireComplete(state, plan.finiteTerminal->outputs);
                if (physical.Replay(p) != state)
                    throw IR::Error("fragment dock physical replay mismatch");
                tape.push_back({'O', tick});
                machine.metrics.cycles = tick + 1;
                return machine;
            }
        }
    throw IR::LayoutFailure("fragment dock sweep or footprint collision");
}

io::SolutionData Compile(
    const io::PuzzleData& p, const ExecutionPlan& plan, int spacing, int latticePhase,
    int neighborChoice, std::chrono::steady_clock::time_point deadline, bool pipeline = false,
    bool retainOrdinary = false, const std::function<void(const std::string&)>& evidence = {},
    bool sparseSourceArms = false,
    SupplyMaterialLoweringPolicy policy = SupplyMaterialLoweringPolicy::PreserveCompatible) {
    const bool closedLife = policy == SupplyMaterialLoweringPolicy::ClosedCardinalLife;
    if (closedLife && (!SupportsClosedCardinalLife(p) || plan.finiteTerminal))
        throw IR::Unsupported("closed cardinal life requires an unretained closed plan");
    if (plan.finiteTerminal) {
        const auto* enabled = std::getenv("OPUSOPTIMA_FINITE_RETENTION");
        if (!enabled || std::string(enabled) != "1")
            throw IR::Unsupported("finite retention disabled");
    }
    if (plan.finiteTerminal) {
        const auto checked = MakeFiniteExecutionPlan(p, plan.material, *plan.finiteTerminal,
                                                     io::VerificationMode::LegacyGate);
        if (checked.retentionEndpoints != plan.retentionEndpoints ||
            checked.origins != plan.origins || checked.tasks.size() != plan.tasks.size() ||
            checked.caches.size() != plan.caches.size())
            throw IR::Unsupported("terminal retention endpoint metadata mismatch");
        for (std::size_t i = 0; i < checked.tasks.size(); ++i) {
            const auto& expected = checked.tasks[i];
            const auto& actual = plan.tasks[i];
            if (expected.step != actual.step || expected.kind != actual.kind ||
                expected.inputs != actual.inputs || expected.products != actual.products ||
                expected.dependsOn != actual.dependsOn || expected.buffer != actual.buffer ||
                expected.output != actual.output ||
                expected.outputOccurrence != actual.outputOccurrence)
                throw IR::Unsupported("terminal retention task metadata mismatch");
        }
        for (std::size_t i = 0; i < checked.caches.size(); ++i) {
            const auto& expected = checked.caches[i];
            const auto& actual = plan.caches[i];
            if (expected.slot != actual.slot || expected.storedAt != actual.storedAt ||
                expected.retrievedAt != actual.retrievedAt || expected.fragment != actual.fragment)
                throw IR::Unsupported("terminal retention cache metadata mismatch");
        }
    } else if (!plan.retentionEndpoints.empty())
        throw IR::Unsupported("ordinary plan cannot bind retention endpoints");
    constexpr auto permissions =
        (1ULL << 0) | (1ULL << 8) | (1ULL << 22) | (1ULL << 23) | (1ULL << 25);
    // The material ledger closes one period; every arm returns to its starting
    // pose before the common tape period repeats.
    if (p.productionInfo || p.version != 3 || p.outputMultiplier < 1 || p.outputMultiplier > 8 ||
        (p.permissionBits & permissions) != permissions || p.outputs.empty() ||
        p.outputs.size() > 4)
        throw IR::Unsupported(
            "serial arm implementation requires bounded nonproduction material supply");
    const auto* fragmentDock = std::getenv("OPUSOPTIMA_FRAGMENT_PRODUCT_DOCKING");
    if (fragmentDock && std::string(fragmentDock) == "1")
        return CompileWholeFragmentDock(p, plan, deadline);
    const bool closedSingletonWheel =
        !plan.finiteTerminal &&
        std::any_of(plan.material.steps.begin(), plan.material.steps.end(),
                    [](const auto& step) { return step.operation.wheelCatalyst.has_value(); }) &&
        std::all_of(
            plan.material.steps.begin(), plan.material.steps.end(),
            [](const auto& step) {
                return !step.operation.wheelCatalyst ||
                       IR::Fragment(step.preconditions, step.operation.atoms.at(0)).size() == 1;
            }) &&
        std::any_of(p.outputs.begin(), p.outputs.end(), [](const auto& output) {
            return std::any_of(output.bonds.begin(), output.bonds.end(),
                               [](const auto& bond) { return bond.bits != 1; });
        });
    std::set<Id> convertedFire;
    if (closedSingletonWheel) {
        const auto checked = MakeExecutionPlan(p, plan.material);
        if (checked.origins != plan.origins || checked.tasks.size() != plan.tasks.size() ||
            p.inputs.size() > 4 || p.outputs.size() != 1 || p.outputs.front().atoms.size() > 4 ||
            plan.material.steps.size() > 512 || plan.origins.size() > 512 || !plan.caches.empty())
            throw IR::Unsupported("closed singleton wheel requires a bounded scalar output");
        for (const auto& input : p.inputs)
            if (input.atoms.size() > 4 ||
                std::any_of(input.bonds.begin(), input.bonds.end(),
                            [](const auto& bond) { return bond.bits != 1; }))
                throw IR::Unsupported("closed singleton wheel requires small ordinary sources");
        std::map<IR::Edge, std::uint8_t> outputEdges;
        for (const auto& step : plan.material.steps)
            if (step.operation.kind == K::Output) {
                const auto ids = IR::Fragment(step.preconditions, step.operation.atoms.at(0));
                for (const auto id : ids) {
                    const auto& atom = step.preconditions.atoms.at(id);
                    if (std::none_of(p.outputs.front().atoms.begin(), p.outputs.front().atoms.end(),
                                     [&](const auto& requested) {
                                         return atom.position == requested.position &&
                                                atom.type == requested.type;
                                     }))
                        throw IR::Unsupported(
                            "closed singleton wheel requires actual output port pose");
                }
                for (const auto& [edge, mask] : step.preconditions.bonds)
                    if (std::binary_search(ids.begin(), ids.end(), edge.a))
                        outputEdges.emplace(edge, mask);
            }
        std::optional<IR::WheelCatalyst> device;
        std::set<IR::Edge> sourceEdges, sourceCuts;
        for (std::size_t index = 0; index < plan.material.steps.size(); ++index) {
            const auto& step = plan.material.steps[index];
            const auto& op = step.operation;
            const auto& task = checked.tasks.at(index);
            if (plan.tasks[index].step != task.step || plan.tasks[index].kind != task.kind ||
                plan.tasks[index].inputs != task.inputs ||
                plan.tasks[index].products != task.products)
                throw IR::Unsupported("closed singleton wheel task metadata mismatch");
            if (op.kind != K::Acquire && op.kind != K::Relocate && op.kind != K::Unbond &&
                op.kind != K::Calcify && op.kind != K::Duplicate && op.kind != K::Bond &&
                op.kind != K::Output && op.kind != K::Discard)
                throw IR::Unsupported("closed singleton wheel operation outside scalar contract");
            if (op.kind == K::Bond) {
                const IR::Edge edge{std::min(op.atoms.at(0), op.atoms.at(1)),
                                    std::max(op.atoms.at(0), op.atoms.at(1))};
                if (!outputEdges.contains(edge) || outputEdges.at(edge) != op.bits)
                    throw IR::Unsupported(
                        "closed singleton wheel only assembles final output edges");
            }
            if (op.kind == K::Acquire)
                for (const auto& [edge, mask] : step.effects.bonds)
                    if (mask == 1 && edge.a >= step.preconditions.nextId &&
                        edge.b >= step.preconditions.nextId)
                        sourceEdges.insert(edge);
            if (op.kind == K::Unbond) {
                const IR::Edge edge{std::min(op.atoms.at(0), op.atoms.at(1)),
                                    std::max(op.atoms.at(0), op.atoms.at(1))};
                if (op.bits != 1 || !sourceEdges.contains(edge) || !sourceCuts.insert(edge).second)
                    throw IR::Unsupported(
                        "closed singleton wheel only cuts original source bonds once");
            }
            if ((op.kind == K::Calcify || op.kind == K::Discard || op.kind == K::Relocate) &&
                IR::Fragment(step.preconditions, op.atoms.at(0)).size() != 1)
                throw IR::Unsupported("closed singleton wheel requires singleton reactions");
            if (op.kind == K::Calcify && !plan.origins.at(op.atoms.at(0)).input)
                throw IR::Unsupported(
                    "closed singleton wheel calcifies original source identities");
            if (op.kind != K::Duplicate) continue;
            if (!op.wheelCatalyst || index == 0 ||
                IR::Fragment(step.preconditions, op.atoms.at(0)).size() != 1 ||
                plan.material.steps[index - 1].operation.kind != K::Calcify ||
                plan.material.steps[index - 1].preconditions.nextId != op.atoms.at(0))
                throw IR::Unsupported("closed singleton wheel requires actual calcify provenance");
            const auto& binding = *op.wheelCatalyst;
            if (device &&
                (device->device != binding.device || device->orientation != binding.orientation))
                throw IR::Unsupported("closed singleton wheel requires one static device");
            device = binding;
            if (binding.element == core::AtomType::Fire)
                convertedFire.insert(step.preconditions.nextId);
        }
        const auto& output = p.outputs.front();
        if (std::count_if(output.atoms.begin(), output.atoms.end(),
                          [](const auto& atom) { return atom.type == core::AtomType::Fire; }) !=
                2 ||
            std::count_if(output.bonds.begin(), output.bonds.end(),
                          [](const auto& bond) { return bond.bits != 1; }) != 1)
            throw IR::Unsupported("closed singleton wheel requires one colored Fire pair");
        for (const auto& step : plan.material.steps)
            if (step.operation.kind == K::Output)
                for (const auto id : IR::Fragment(step.preconditions, step.operation.atoms.at(0)))
                    if (step.preconditions.atoms.at(id).type == core::AtomType::Fire &&
                        !convertedFire.contains(id))
                        throw IR::Unsupported(
                            "closed singleton wheel Fire lacks conversion provenance");
    }
    std::set<IR::Edge> inheritedRoots;
    const auto rootOf = [&](Id atom) {
        for (std::size_t depth = 0; depth < plan.origins.size(); ++depth) {
            const auto& origin = plan.origins.at(atom);
            if (origin.input || origin.reagents.empty()) return atom;
            atom = origin.reagents.front();
        }
        throw IR::Error("cyclic material provenance");
    };
    for (const auto& step : plan.material.steps)
        if (step.operation.kind == K::Output) {
            const auto product = IR::Fragment(step.preconditions, step.operation.atoms.at(0));
            for (const auto& [edge, bits] : step.preconditions.bonds)
                if (bits != 1 && std::binary_search(product.begin(), product.end(), edge.a)) {
                    const auto a = rootOf(edge.a), b = rootOf(edge.b);
                    inheritedRoots.insert({std::min(a, b), std::max(a, b)});
                }
        }
    if (retainOrdinary) {
        std::set<IR::Edge> persistent;
        for (const auto& step : plan.material.steps) {
            if (step.operation.kind == K::Output) {
                const auto product = IR::Fragment(step.preconditions, step.operation.atoms.at(0));
                for (const auto& edge : persistent)
                    if (std::binary_search(product.begin(), product.end(), edge.a) &&
                        std::binary_search(product.begin(), product.end(), edge.b))
                        inheritedRoots.insert(edge);
            }
            std::erase_if(persistent, [&](const auto& edge) {
                const auto found = step.effects.bonds.find(edge);
                return found == step.effects.bonds.end() || found->second != 1;
            });
            if (step.operation.kind == K::Acquire)
                for (const auto& [edge, bits] : step.effects.bonds)
                    if (bits == 1 && edge.a >= step.preconditions.nextId &&
                        edge.b >= step.preconditions.nextId)
                        persistent.insert(edge);
        }
    }
    // Closed singleton conversion cuts source bonds with the emitted source unbonders.
    if (closedSingletonWheel) inheritedRoots.clear();
    const auto final = plan.material.Replay(p);
    if (final.outputs.empty() || !final.outputs[0] || 6ULL * p.outputMultiplier % final.outputs[0])
        throw IR::Unsupported("serial arm implementation needs a dividing closed period");
    for (const auto count : final.outputs)
        if (count != final.outputs[0]) throw IR::Unsupported("serial arm output periods differ");
    if (plan.finiteTerminal)
        IR::RequireFiniteTerminal(final, *plan.finiteTerminal);
    else
        IR::RequireComplete(final, final.outputs);
    struct EndpointStage {
        Id endpoint{}, salt{}, product{};
        std::vector<Id> atoms;
        std::map<Id, Cell> relative;
        std::map<IR::Edge, std::uint8_t> bonds;
        IR::WheelCatalyst catalyst;
        Cell build{6, 0}, convert{6, 6};
    };
    std::optional<EndpointStage> stage;
    std::map<Id, IR::WheelCatalyst> monomers;
    for (std::size_t index = 0; index < plan.material.steps.size(); ++index) {
        const auto& step = plan.material.steps[index];
        if (!step.operation.wheelCatalyst) continue;
        const auto substrate = step.operation.atoms.at(0);
        if ((plan.finiteTerminal || closedSingletonWheel) &&
            IR::Fragment(step.preconditions, substrate).size() == 1) {
            if (index == 0 || plan.material.steps[index - 1].operation.kind != K::Calcify ||
                plan.material.steps[index - 1].preconditions.nextId != substrate)
                throw IR::Unsupported("monomer wheel requires a real calcify precursor");
            if (!monomers.empty() &&
                (monomers.begin()->second.device != step.operation.wheelCatalyst->device ||
                 (!closedSingletonWheel &&
                  monomers.begin()->second.element != step.operation.wheelCatalyst->element) ||
                 monomers.begin()->second.orientation != step.operation.wheelCatalyst->orientation))
                throw IR::Unsupported("monomer wheel requires one static cardinal station");
            monomers.emplace(substrate, *step.operation.wheelCatalyst);
            continue;
        }
        if (stage || index == 0 || plan.material.steps[index - 1].operation.kind != K::Calcify)
            throw IR::Unsupported("serial endpoint stage requires one calcify/duplicate chain");
        const auto& calcify = plan.material.steps[index - 1];
        EndpointStage binding;
        binding.endpoint = calcify.operation.atoms.at(0);
        binding.salt = calcify.preconditions.nextId;
        binding.product = step.preconditions.nextId;
        binding.catalyst = *step.operation.wheelCatalyst;
        std::set<Id> component{binding.endpoint};
        for (std::size_t before = 0; before != component.size();) {
            before = component.size();
            for (const auto& [edge, bits] : calcify.preconditions.bonds)
                if (bits != 1 && (component.contains(edge.a) || component.contains(edge.b))) {
                    component.insert(edge.a);
                    component.insert(edge.b);
                }
        }
        if (component.size() < 2 || component.size() > 8)
            throw IR::Unsupported("endpoint precursor component bound exceeded");
        const auto origin = calcify.preconditions.atoms.at(binding.endpoint).position;
        for (auto atom : component) {
            if (calcify.preconditions.atoms.at(atom).type != core::AtomType::Fire)
                throw IR::Unsupported("endpoint precursor must be assembled from Fire");
            binding.atoms.push_back(atom);
            binding.relative[atom] =
                Hex(core::Subtract(calcify.preconditions.atoms.at(atom).position, origin));
        }
        for (const auto& [edge, bits] : calcify.preconditions.bonds)
            if (bits != 1 && component.contains(edge.a) && component.contains(edge.b))
                binding.bonds.emplace(edge, bits);
        stage = std::move(binding);
    }
    if (stage && !(p.permissionBits & (1ULL << 3)))
        throw IR::PermissionFailure(3, "endpoint carrier requires track permission");
    io::SolutionData machine;
    machine.puzzleName = p.name;
    machine.solutionName = retainOrdinary ? "Shared-plan retained ordinary fragments"
                                          : "Shared-plan serial arm execution";
    auto object = [&](const char* name, Cell position, int angle = 0, int index = 0) {
        machine.objects.push_back({name, Hex(position), {}, {1, angle, index, 0, 0}});
        return machine.objects.size() - 1;
    };
    std::set<Cell> reserved, feeds, wheelCells, stageCells, carrierCells;
    std::set<Cell> outputCells, nonOutputCells;
    bool reservingOutput = false, overlappingOutputs = false;
    std::vector<Cell> carrierTrack;
    std::optional<std::size_t> carrier, stageFeeder, fragmentFeeder;
    Cell feederBase{}, feederDonor{}, fragmentFeederBase{};
    Id fragmentFeedGrip{};
    std::size_t carrierStart{};
    struct FeedPort {
        std::size_t input{};
        Cell origin;
        std::set<IR::Edge> retained;
    };
    std::vector<FeedPort> inputPorts;
    std::map<std::size_t, std::size_t> portForAcquire;
    std::map<std::size_t, core::HexFrame> frameForAcquire;
    std::map<std::size_t, std::set<IR::Edge>> keepForAcquire;
    std::vector<Cell> outputOrigins;
    int lowQ = -24, highQ = 28, lowR = -18, highR = 24;
    auto reserve = [&](Cell cell) {
        if (cell.first < -110 || cell.first > 110 || cell.second < -110 || cell.second > 110)
            throw IR::LayoutFailure("serial arm layout exceeds bounded workspace");
        reserved.insert(cell);
        if (reservingOutput)
            overlappingOutputs |= !outputCells.insert(cell).second;
        else
            nonOutputCells.insert(cell);
        lowQ = std::min(lowQ, cell.first - 4);
        highQ = std::max(highQ, cell.first + 4);
        lowR = std::min(lowR, cell.second - 4);
        highR = std::max(highR, cell.second + 4);
    };
    for (const auto& task : plan.tasks)
        if (task.kind == K::Acquire) {
            const auto& step = plan.material.steps[task.step];
            const auto input = step.operation.index;
            const auto first = step.preconditions.nextId;
            std::set<IR::Edge> keep;
            for (const auto& [edge, bits] : step.effects.bonds)
                if (edge.a >= first && edge.b >= first && inheritedRoots.contains(edge))
                    keep.insert({edge.a - first, edge.b - first});
            keepForAcquire[task.step] = keep;
            const auto& molecule = p.inputs[input];
            const auto retainedCells = [&](const auto& edges, core::HexFrame frame) {
                std::set<std::array<int, 4>> cells;
                for (const auto& edge : edges) {
                    auto a = Hex(frame.ToWorld(molecule.atoms.at(edge.a).position));
                    auto b = Hex(frame.ToWorld(molecule.atoms.at(edge.b).position));
                    if (b < a) std::swap(a, b);
                    cells.insert({a.first, a.second, b.first, b.second});
                }
                return cells;
            };
            bool reused = false;
            for (std::size_t port = 0; port < inputPorts.size() && !reused; ++port) {
                if (inputPorts[port].input != input) continue;
                for (int rotation = 0; rotation < 6 && !reused; ++rotation) {
                    const auto degree = [&](const auto& atom) {
                        return std::count_if(
                            molecule.bonds.begin(), molecule.bonds.end(), [&](const auto& bond) {
                                return bond.from == atom.position || bond.to == atom.position;
                            });
                    };
                    const auto center =
                        std::max_element(
                            molecule.atoms.begin(), molecule.atoms.end(),
                            [&](const auto& a, const auto& b) { return degree(a) < degree(b); })
                            ->position;
                    core::HexFrame symmetry{{}, rotation};
                    symmetry.origin = core::Subtract(center, symmetry.ToWorld(center));
                    bool same = true;
                    for (const auto& atom : molecule.atoms)
                        same &= std::any_of(
                            molecule.atoms.begin(), molecule.atoms.end(), [&](const auto& other) {
                                return other.type == atom.type &&
                                       other.position == symmetry.ToWorld(atom.position);
                            });
                    for (const auto& bond : molecule.bonds) {
                        const auto a = symmetry.ToWorld(bond.from), b = symmetry.ToWorld(bond.to);
                        same &= std::any_of(molecule.bonds.begin(), molecule.bonds.end(),
                                            [&](const auto& other) {
                                                return bond.bits == other.bits &&
                                                       ((other.from == a && other.to == b) ||
                                                        (other.from == b && other.to == a));
                                            });
                    }
                    if (!same || retainedCells(keep, symmetry) !=
                                     retainedCells(inputPorts[port].retained, {}))
                        continue;
                    portForAcquire[task.step] = port;
                    symmetry.origin = core::Add(Hex(inputPorts[port].origin), symmetry.origin);
                    frameForAcquire[task.step] = symmetry;
                    reused = true;
                }
            }
            if (reused) continue;
            const Cell origin{-18, static_cast<int>(inputPorts.size()) * spacing};
            portForAcquire[task.step] = inputPorts.size();
            inputPorts.push_back({input, origin, keep});
            frameForAcquire[task.step] = {Hex(origin), 0};
            object("input", origin, 0, input);
            for (const auto& atom : p.inputs[input].atoms) {
                const auto cell = Add(origin, Hex(atom.position));
                reserve(cell);
                feeds.insert(cell);
            }
            const auto at = [&](core::HexCoord cell) {
                const auto& atoms = p.inputs[input].atoms;
                return static_cast<Id>(
                    std::find_if(atoms.begin(), atoms.end(),
                                 [&](const auto& atom) { return atom.position == cell; }) -
                    atoms.begin());
            };
            for (const auto& bond : p.inputs[input].bonds) {
                const auto a = at(bond.from), b = at(bond.to);
                if (keep.contains({std::min(a, b), std::max(a, b)})) continue;
                if (!(p.permissionBits & (1ULL << 9)))
                    throw IR::PermissionFailure(9, "serial extraction requires unbonding");
                const auto delta = Hex(core::Subtract(bond.to, bond.from));
                const auto direction = std::find(directions.begin(), directions.end(), delta);
                if (direction == directions.end()) throw IR::Error("invalid feed bond direction");
                object("unbonder", Add(origin, Hex(bond.from)), direction - directions.begin());
            }
        }
    for (std::size_t output = 0; output < p.outputs.size(); ++output) {
        const Cell origin{18, static_cast<int>(output) * spacing};
        outputOrigins.push_back(origin);
        object("out-std", origin, 0, output);
        reservingOutput = true;
        for (const auto& atom : p.outputs[output].atoms)
            reserve(Add(origin, Hex(atom.position)));
        reservingOutput = false;
        for (const auto& bond : p.outputs[output].bonds) {
            if (bond.bits != 1) continue;
            if (retainOrdinary) {
                bool retained = true, seen = false;
                for (const auto& step : plan.material.steps) {
                    if (step.operation.kind != K::Output || step.operation.index != output)
                        continue;
                    seen = true;
                    Id a = std::numeric_limits<Id>::max(), b = a;
                    for (const auto id :
                         IR::Fragment(step.preconditions, step.operation.atoms.at(0))) {
                        const auto position = step.preconditions.atoms.at(id).position;
                        if (position == bond.from) a = id;
                        if (position == bond.to) b = id;
                    }
                    retained &= inheritedRoots.contains({std::min(a, b), std::max(a, b)});
                }
                if (seen && retained) continue;
            }
            const auto delta = Hex(core::Subtract(bond.to, bond.from));
            const auto direction = std::find(directions.begin(), directions.end(), delta);
            if (direction == directions.end()) throw IR::Error("invalid target bond direction");
            object("bonder", Add(origin, Hex(bond.from)), direction - directions.begin());
        }
    }
    std::vector<ColoredOutputStation> coloredStations;
    if (!stage && (monomers.empty() || closedSingletonWheel)) {
        for (const auto& task : plan.tasks) {
            if (task.kind != K::Output) continue;
            const auto& step = plan.material.steps.at(task.step);
            const auto ids = IR::Fragment(step.preconditions, step.operation.atoms.at(0));
            ColoredOutputStation station{step.operation.index, task.step, {}, {}, {}};
            for (const auto& [edge, bits] : step.preconditions.bonds)
                if (bits != 1 && std::binary_search(ids.begin(), ids.end(), edge.a))
                    station.bonds.emplace(edge, bits);
            if (station.bonds.empty()) continue;
            const bool genuine = std::all_of(ids.begin(), ids.end(), [&](Id id) {
                const auto& origin = plan.origins.at(id);
                return origin.input && origin.reagents.empty() &&
                       p.inputs.at(*origin.input).atoms.size() == 1 &&
                       p.inputs.at(*origin.input).bonds.empty() &&
                       p.inputs.at(*origin.input).atoms.front().type == core::AtomType::Fire;
            });
            if (!genuine && !closedSingletonWheel) continue;
            if (!(p.permissionBits & (1ULL << 11)))
                throw IR::PermissionFailure(11, "colored output station requires prism permission");
            if (!closedSingletonWheel &&
                (plan.finiteTerminal || ids.size() != 2 || station.bonds.size() != 1 ||
                 p.outputs.at(station.output).atoms.size() != 2 ||
                 p.outputs.at(station.output).bonds.size() != 1 || p.inputs.size() > 4 ||
                 plan.material.steps.size() > 512 || plan.origins.size() > 512 ||
                 std::any_of(p.inputs.begin(), p.inputs.end(),
                             [](const auto& input) {
                                 return input.atoms.size() != 1 || !input.bonds.empty();
                             }) ||
                 std::any_of(p.outputs.begin(), p.outputs.end(),
                             [](const auto& output) { return output.atoms.size() > 2; }) ||
                 std::any_of(plan.material.steps.begin(), plan.material.steps.end(),
                             [](const auto& material) {
                                 const auto kind = material.operation.kind;
                                 return kind != K::Acquire && kind != K::Relocate &&
                                        kind != K::Bond && kind != K::Output;
                             })))
                throw IR::Unsupported("independent colored output station requires scalar dimers");
            if (station.bonds.size() != 1)
                throw IR::Unsupported("colored output station requires one colored pair");
            const auto mask = station.bonds.begin()->second;
            if (!(mask & 14) || (mask & ~14))
                throw IR::Unsupported("colored output station requires an exact colored mask");
            const auto& pair = station.bonds.begin()->first;
            for (Id id : {pair.a, pair.b}) {
                if (step.preconditions.atoms.at(id).type != core::AtomType::Fire ||
                    (closedSingletonWheel && !convertedFire.contains(id)))
                    throw IR::Unsupported("colored output station requires genuine Fire endpoints");
                station.sites.emplace(id, Add(outputOrigins.at(station.output),
                                              Hex(step.preconditions.atoms.at(id).position)));
            }
            const auto requestedBond =
                std::find_if(p.outputs.at(station.output).bonds.begin(),
                             p.outputs.at(station.output).bonds.end(),
                             [](const auto& bond) { return bond.bits != 1; });
            if (requestedBond == p.outputs.at(station.output).bonds.end())
                throw IR::Unsupported("colored output station missing requested edge");
            const auto& requested = *requestedBond;
            const auto& edge = station.bonds.begin()->first;
            const auto a = step.preconditions.atoms.at(edge.a).position;
            const auto b = step.preconditions.atoms.at(edge.b).position;
            if (requested.bits != mask || !((requested.from == a && requested.to == b) ||
                                            (requested.from == b && requested.to == a)))
                throw IR::Unsupported("colored output station requires the actual port pose");
            if (std::chrono::steady_clock::now() >= deadline)
                throw IR::BudgetFailure("shared material/physical deadline exhausted");
            station.glyphs = SelectPrisms(station.sites, station.bonds, {station.sites});
            coloredStations.push_back(std::move(station));
        }
    }
    std::set<std::pair<Cell, int>> outputPrisms;
    for (const auto& station : coloredStations)
        for (const auto& glyph : station.glyphs) {
            for (auto cell : glyph.vertices)
                if (feeds.contains(cell))
                    throw IR::LayoutFailure("colored output prism overlaps a source port");
            if (outputPrisms.emplace(glyph.origin, glyph.turn).second)
                object("bonder-prisma", glyph.origin, glyph.turn);
        }
    const Cell metal{0, -6}, mercury{1, -6}, calcifier{0, -12}, purifier{0, 0}, purified{0, 1},
        disposal{8, -6}, animismus{6, 0}, duplication{12, 4};
    // Measurement knob: where the material duplication glyph sits.
    auto duplicationCell = [&] {
        const char* flag = std::getenv("OPUSOPTIMA_SERIAL_DUP_CELL");
        if (!flag) return duplication;
        const std::string text{flag};
        const auto comma = text.find(',');
        if (comma == std::string::npos) return duplication;
        return Cell{std::atoi(text.substr(0, comma).c_str()),
                    std::atoi(text.substr(comma + 1).c_str())};
    }();
    const Cell dup = duplicationCell;
    bool projects = false, calcifies = false, purifies = false, discards = false, animates = false;
    bool duplicates = false;
    for (const auto& task : plan.tasks) {
        projects |= task.kind == K::Project;
        calcifies |= task.kind == K::Calcify;
        purifies |= task.kind == K::Purify;
        discards |= task.kind == K::Discard;
        animates |= task.kind == K::Animismus;
        // Material-level duplication has no wheel catalyst; the wheel variant is laid
        // out by the stage block below.
        duplicates |= std::getenv("OPUSOPTIMA_SERIAL_LONG_ARMS") != nullptr &&
                      task.kind == K::Duplicate &&
                      !plan.material.steps.at(task.step).operation.wheelCatalyst.has_value();
    }
    if (animates) {
        const auto* enabled = std::getenv("OPUSOPTIMA_SERIAL_ANIMISMUS");
        if (!closedLife && (!enabled || std::string(enabled) != "1"))
            throw IR::Unsupported(
                "serial arm material reaction not yet implemented (Animismus experiment disabled)");
        if (!(p.permissionBits & (1ULL << 16)))
            throw IR::Unsupported("Animismus permission required");
        const auto* precursor = std::getenv("OPUSOPTIMA_CALCIFY_ANIMISMUS");
        if (discards || (calcifies && !closedLife && (!precursor || std::string(precursor) != "1")))
            throw IR::Unsupported(
                "Animismus experiment excludes disabled precursor calcification and disposal");
        std::size_t vitaeCount = 0, morsCount = 0;
        for (const auto& output : p.outputs)
            for (const auto& atom : output.atoms) {
                vitaeCount += atom.type == core::AtomType::Vitae;
                morsCount += atom.type == core::AtomType::Mors;
            }
        if ((!vitaeCount || vitaeCount != morsCount) && !plan.finiteTerminal)
            throw IR::Unsupported(
                "Animismus experiment requires balanced ordinary coproduct outputs");
        for (auto offset : {Cell{0, 0}, Cell{1, 0}, Cell{0, 1}, Cell{1, -1}})
            reserve(Add(animismus, offset));
        object("glyph-life-and-death", animismus);
    }
    if (duplicates) {
        for (auto offset : {Cell{0, 0}, Cell{1, 0}, Cell{0, 1}, Cell{1, -1}})
            reserve(Add(dup, offset));
        object("glyph-duplication", dup);
    }
    if (projects) {
        reserve(metal);
        reserve(mercury);
        object("glyph-projection", mercury, 3);
    }
    if (calcifies) {
        reserve(calcifier);
        object("glyph-calcification", calcifier);
    }
    if (purifies) {
        reserve(purifier);
        reserve(Add(purifier, {1, 0}));
        reserve(purified);
        object("glyph-purification", purifier);
    }
    if (discards) {
        reserve(disposal);
        object("glyph-disposal", disposal);
    }
    std::vector<Cell> cache;
    for (int i = 0; i < 32; ++i) {
        cache.push_back({-10 + (i % 8) * 3, 12 + (i / 8) * 3});
        reserve(cache.back());
    }
    std::map<Id, Cell> retentionTargets;
    for (const auto& endpoint : plan.retentionEndpoints) {
        if (endpoint.slot >= plan.finiteTerminal->capacity || endpoint.slot >= cache.size() ||
            !retentionTargets.emplace(endpoint.atom, cache.at(endpoint.slot)).second)
            throw IR::LayoutFailure("terminal retention physical capacity mismatch");
    }
    if (stage && !monomers.empty())
        throw IR::Unsupported("mixed monomer and bonded wheel stations unsupported");
    std::map<Id, Cell> monomerCells;
    if (!monomers.empty()) {
        const Cell base{10, -12};
        for (const auto delta : directions) {
            const auto cell = Add(base, delta);
            if (reserved.contains(cell))
                throw IR::LayoutFailure("monomer wheel occupied footprint");
            reserve(cell);
            wheelCells.insert(cell);
        }
        if (reserved.contains(base)) throw IR::LayoutFailure("monomer wheel occupied base");
        reserve(base);
        wheelCells.insert(base);
        object("baron", base, monomers.begin()->second.orientation);
        std::set<Cell> contacts;
        for (const auto& [substrate, binding] : monomers) {
            Cell offset{};
            switch (binding.element) {
            case core::AtomType::Water:
                offset = {0, 1};
                break;
            case core::AtomType::Air:
                offset = {-1, 1};
                break;
            case core::AtomType::Fire:
                offset = {0, -1};
                break;
            case core::AtomType::Earth:
                offset = {1, -1};
                break;
            default:
                throw IR::Unsupported("monomer wheel requires cardinal type");
            }
            offset = Hex(core::HexFrame{{}, binding.orientation}.ToWorld(Hex(offset)));
            const auto catalyst = Add(base, offset);
            const auto station = Add(catalyst, offset);
            monomerCells.emplace(substrate, station);
            if (!contacts.insert(station).second) continue;
            if (reserved.contains(station))
                throw IR::LayoutFailure("monomer wheel occupied station");
            reserve(station);
            object("glyph-duplication", catalyst,
                   std::find(directions.begin(), directions.end(), offset) - directions.begin());
        }
    }
    if (stage) {
        const auto& binding = *stage;
        for (int r = binding.convert.second - 2; r >= binding.build.second - 2; --r)
            carrierTrack.push_back({binding.build.first, r});
        carrierStart = carrierTrack.size() - 1;
        Cell finalCenter{};
        for (const auto& step : plan.material.steps)
            if (step.operation.kind == K::Output &&
                step.preconditions.atoms.contains(binding.product))
                finalCenter = Add(outputOrigins.at(step.operation.index),
                                  Hex(step.preconditions.atoms.at(binding.product).position));
        if (finalCenter.second != binding.build.second || finalCenter.first <= binding.build.first)
            throw IR::LayoutFailure(
                "endpoint carrier requires a horizontal final assembly corridor");
        for (int q = binding.build.first + 1; q <= finalCenter.first; ++q)
            carrierTrack.push_back({q, binding.build.second - 2});
        for (auto base : carrierTrack) {
            reserve(base);
            carrierCells.insert(base);
            const auto center = Add(base, {0, 2});
            for (const auto& [id, relative] : binding.relative) {
                const auto cell = Add(center, relative);
                reserve(cell);
                stageCells.insert(cell);
            }
        }
        for (const auto& [id, relative] : binding.relative)
            reserve(Add(binding.build, relative));
        const std::array<std::pair<core::AtomType, Cell>, 6> wheel{
            {{core::AtomType::Salt, {1, 0}},
             {core::AtomType::Water, {0, 1}},
             {core::AtomType::Air, {-1, 1}},
             {core::AtomType::Salt, {-1, 0}},
             {core::AtomType::Fire, {0, -1}},
             {core::AtomType::Earth, {1, -1}}}};
        Cell catalystOffset{};
        bool cardinal = false;
        for (const auto& [type, offset] : wheel)
            if (type == binding.catalyst.element) {
                catalystOffset =
                    Hex(core::HexFrame{{}, binding.catalyst.orientation}.ToWorld(Hex(offset)));
                cardinal = true;
                break;
            }
        if (!cardinal) throw IR::Error("invalid endpoint wheel cardinal");
        const Cell catalyst = Add(binding.convert, {0, 1});
        const auto wheelBase = Sub(catalyst, catalystOffset);
        reserve(wheelBase);
        wheelCells.insert(wheelBase);
        for (const auto& [type, offset] : wheel) {
            const auto cell =
                Add(wheelBase,
                    Hex(core::HexFrame{{}, binding.catalyst.orientation}.ToWorld(Hex(offset))));
            reserve(cell);
            wheelCells.insert(cell);
            for (const auto& [id, relative] : binding.relative)
                if (cell == Add(binding.convert, relative))
                    throw IR::LayoutFailure("wheel overlaps converted precursor");
        }
        object("baron", wheelBase, binding.catalyst.orientation);
        object("glyph-calcification", binding.convert);
        object("glyph-duplication", catalyst, 4);
        std::map<Id, Cell> prismSites;
        std::vector<std::map<Id, Cell>> incoming;
        for (const auto& [id, relative] : binding.relative)
            prismSites.emplace(id, Add(binding.build, relative));
        for (int distance = 1; distance <= binding.convert.second - binding.build.second;
             ++distance) {
            auto pose = prismSites;
            for (auto& [id, cell] : pose)
                cell = Add(cell, {0, distance});
            incoming.push_back(std::move(pose));
        }
        for (const auto& glyph : SelectPrisms(prismSites, binding.bonds, incoming))
            object("bonder-prisma", glyph.origin, glyph.turn);
        feederBase = Add(binding.build, {1, -3});
        feederDonor = Add(feederBase, {3, 0});
        reserve(feederBase);
        reserve(feederDonor);
        stageFeeder = object("arm1", feederBase, 0);
        machine.objects[*stageFeeder].bodyData[0] = 3;
        for (const auto& step : plan.material.steps)
            if (step.operation.kind == K::Output)
                for (const auto& [edge, bits] : step.preconditions.bonds)
                    if (bits != 1) {
                        const auto& firstOrigin = plan.origins.at(rootOf(edge.a));
                        const auto& secondOrigin = plan.origins.at(rootOf(edge.b));
                        if (!firstOrigin.input || firstOrigin.input != secondOrigin.input ||
                            firstOrigin.batch != secondOrigin.batch)
                            continue;
                        const auto a = Add(outputOrigins[step.operation.index],
                                           Hex(step.preconditions.atoms.at(edge.a).position));
                        const auto b = Add(outputOrigins[step.operation.index],
                                           Hex(step.preconditions.atoms.at(edge.b).position));
                        if (a.second != binding.build.second || b.second != binding.build.second ||
                            !stageCells.contains(a) || !stageCells.contains(b))
                            continue;
                        fragmentFeedGrip = a.first < b.first ? edge.a : edge.b;
                        const auto grip = a.first < b.first ? a : b;
                        fragmentFeederBase = Add(grip, {0, 3});
                        reserve(fragmentFeederBase);
                        fragmentFeeder = object("arm1", fragmentFeederBase, 3);
                        machine.objects[*fragmentFeeder].bodyData[0] = 3;
                        const core::HexFrame inverse{Hex(fragmentFeederBase), 5};
                        reserve(
                            Hex(inverse.ToWorld(core::Subtract(Hex(a), Hex(fragmentFeederBase)))));
                        reserve(
                            Hex(inverse.ToWorld(core::Subtract(Hex(b), Hex(fragmentFeederBase)))));
                    }
        const auto start = carrierTrack[carrierStart];
        carrier = object("arm1", start, 1);
        io::SolutionObject track{"track",
                                 Hex(carrierTrack.front()),
                                 {},
                                 {1, 0, 0, 0, static_cast<int>(carrierTrack.size())}};
        for (auto cell : carrierTrack) {
            const auto offset = Sub(cell, carrierTrack.front());
            track.bodyData.push_back(offset.first);
            track.bodyData.push_back(offset.second);
        }
        track.bodyData.push_back(0);
        machine.objects.push_back(std::move(track));
    }
    std::map<Cell, std::size_t> arms;
    std::map<Cell, int> angles;
    std::map<Cell, int> armReach;
    // An arm of type armL reaches the ring at radius L, so a cell whose six
    // neighbours are all reserved is still reachable from two or three cells away.
    auto addArm = [&](Cell base, int reach = 1) {
        if (reserved.contains(base) || arms.contains(base)) return;
        const char* name = reach == 3 ? "arm3" : reach == 2 ? "arm2" : "arm1";
        auto index = object(name, base);
        machine.objects[index].bodyData[4] = static_cast<int>(arms.size());
        arms.emplace(base, index);
        angles.emplace(base, 0);
        armReach.emplace(base, reach);
    };
    for (int q = lowQ; q <= highQ; ++q)
        for (int r = lowR; r <= highR; ++r)
            if (((q - r) % 3 + 3) % 3 == latticePhase) addArm({q, r});
    if (!sparseSourceArms) {
        for (const auto& port : inputPorts)
            if (port.retained.empty())
                for (const auto& atom : p.inputs[port.input].atoms) {
                    const auto degree = std::count_if(
                        p.inputs[port.input].bonds.begin(), p.inputs[port.input].bonds.end(),
                        [&](const auto& bond) {
                            return bond.from == atom.position || bond.to == atom.position;
                        });
                    if (degree < 3) continue;
                    const auto cell = Add(port.origin, Hex(atom.position));
                    for (const auto direction : directions)
                        addArm(Add(cell, direction));
                }
    }
    if (stage)
        for (const auto& [id, relative] : stage->relative)
            addArm(Add(Add(stage->build, relative), {-1, 0}));
    if (stage)
        for (std::size_t port = 0; port < p.outputs.size(); ++port)
            for (const auto& bond : p.outputs[port].bonds)
                if (bond.bits != 1) {
                    const auto a = Add(outputOrigins[port], Hex(bond.from));
                    const auto b = Add(outputOrigins[port], Hex(bond.to));
                    if (stageCells.contains(a) || stageCells.contains(b)) continue;
                    bool added = false;
                    for (auto grip : {a, b})
                        for (int offset = 0; offset < 6 && !added; ++offset) {
                            const int direction = (offset + neighborChoice) % 6;
                            const auto base = Sub(grip, directions[direction]);
                            if (reserved.contains(base) || arms.contains(base)) continue;
                            for (int turn : {1, 5}) {
                                bool clear = true;
                                const core::HexFrame inverse{Hex(base), turn};
                                for (auto endpoint : {a, b}) {
                                    const auto donor = Hex(
                                        inverse.ToWorld(core::Subtract(Hex(endpoint), Hex(base))));
                                    if (arms.contains(donor) ||
                                        (reserved.contains(donor) && !stageCells.contains(donor)))
                                        clear = false;
                                }
                                if (clear) {
                                    addArm(base);
                                    added = true;
                                    break;
                                }
                            }
                        }
                }
    std::set<Cell> dockingCells;
    if (stage)
        for (std::size_t port = 0; port < p.outputs.size(); ++port)
            for (const auto& bond : p.outputs[port].bonds)
                if (bond.bits != 1) {
                    auto a = Add(outputOrigins[port], Hex(bond.from));
                    auto b = Add(outputOrigins[port], Hex(bond.to));
                    if (a.first != b.first || stageCells.contains(a) || stageCells.contains(b))
                        continue;
                    if (b.second < a.second) std::swap(a, b);
                    const auto base = Add(a, {1, 0});
                    if (reserved.contains(base)) continue;
                    const core::HexFrame inverse{Hex(base), 5};
                    for (auto cell : {a, b}) {
                        const auto donor =
                            Hex(inverse.ToWorld(core::Subtract(Hex(cell), Hex(base))));
                        dockingCells.insert(donor);
                        if (donor != base) {
                            arms.erase(donor);
                            angles.erase(donor);
                        }
                    }
                    addArm(base);
                }
    if (stage) {
        const auto entry = Add(stage->build, {-1, -1});
        arms.erase(entry);
        angles.erase(entry);
        dockingCells.insert(entry);
    }
    const auto* accessFlag = std::getenv("OPUSOPTIMA_PRODUCT_PORT_ACCESS");
    const bool temporalOutput =
        (closedLife || (accessFlag && std::string(accessFlag) == "1")) && !plan.finiteTerminal &&
        !stage && monomers.empty() && inheritedRoots.empty() && !overlappingOutputs &&
        coloredStations.empty() && !closedSingletonWheel &&
        std::all_of(p.inputs.begin(), p.inputs.end(),
                    [](const auto& input) {
                        if (input.atoms.size() == 1) return input.bonds.empty();
                        if (input.atoms.size() != 2 || input.bonds.size() != 1) return false;
                        const auto& bond = input.bonds.front();
                        if (bond.bits != 1 || input.atoms[0].position == input.atoms[1].position)
                            return false;
                        return (bond.from == input.atoms[0].position &&
                                bond.to == input.atoms[1].position) ||
                               (bond.to == input.atoms[0].position &&
                                bond.from == input.atoms[1].position);
                    }) &&
        SerialMaterialSourceCuts(plan.material) &&
        std::all_of(p.outputs.begin(), p.outputs.end(), [](const auto& output) {
            return output.atoms.size() > 1 && !output.bonds.empty() &&
                   std::all_of(output.bonds.begin(), output.bonds.end(),
                               [](const auto& bond) { return bond.bits == 1; });
        });
    if (closedLife && !temporalOutput)
        throw IR::Unsupported("closed cardinal life requires the ordinary temporal contract");
    std::set<Cell> futureOutputCells;
    std::map<Cell, std::vector<Cell>> ordinaryEndpoints;
    if (temporalOutput) {
        std::set<std::pair<Cell, Cell>> sourceCuts;
        for (const auto& [stepIndex, portIndex] : portForAcquire) {
            const auto& step = plan.material.steps.at(stepIndex);
            const auto& port = inputPorts.at(portIndex);
            const auto& input = p.inputs.at(port.input);
            const auto& frame = frameForAcquire.at(stepIndex);
            const auto first = step.preconditions.nextId;
            if (!port.retained.empty() || !keepForAcquire.at(stepIndex).empty())
                throw IR::Unsupported("temporal source retains a fragment");
            for (std::size_t i = 0; i < input.atoms.size(); ++i) {
                const auto atom = step.effects.atoms.find(first + i);
                const auto position = frame.ToWorld(input.atoms[i].position);
                const auto cell = Hex(position);
                const core::HexFrame materialFrame{step.operation.translation,
                                                   step.operation.rotation};
                if (atom == step.effects.atoms.end() || atom->second.type != input.atoms[i].type ||
                    atom->second.position != materialFrame.ToWorld(input.atoms[i].position) ||
                    !feeds.contains(cell) || !nonOutputCells.contains(cell) ||
                    outputCells.contains(cell))
                    throw IR::LayoutFailure(
                        "temporal source regeneration footprint is not protected");
            }
            if (input.atoms.size() != 2) continue;
            const IR::Edge edge{first, first + 1};
            auto physical = step.effects;
            for (std::size_t i = 0; i < input.atoms.size(); ++i)
                physical.atoms.at(first + i).position = frame.ToWorld(input.atoms[i].position);
            if (!SerialSourceCutWitness(physical, edge, 1, machine.objects, feeds, nonOutputCells,
                                        outputCells, p.permissionBits))
                throw IR::LayoutFailure("temporal source Unbond has no physical glyph witness");
            auto a = Hex(physical.atoms.at(edge.a).position);
            auto b = Hex(physical.atoms.at(edge.b).position);
            if (b < a) std::swap(a, b);
            sourceCuts.emplace(a, b);
        }
        for (auto cell : outputCells)
            if (!nonOutputCells.contains(cell)) futureOutputCells.insert(cell);
        const std::set<std::string> known{"arm1",
                                          "input",
                                          "out-std",
                                          "bonder",
                                          "glyph-life-and-death",
                                          "glyph-projection",
                                          "glyph-calcification",
                                          "glyph-purification",
                                          "glyph-disposal"};
        for (const auto& glyph : machine.objects) {
            if (glyph.typeName == "unbonder") {
                if (glyph.bodyData.size() < 2 || glyph.bodyData[1] < 0 || glyph.bodyData[1] >= 6)
                    throw IR::LayoutFailure("temporal source unbonder has an invalid orientation");
                auto a = Hex(glyph.position);
                auto b = Add(a, directions[glyph.bodyData[1]]);
                if (b < a) std::swap(a, b);
                if (!sourceCuts.contains({a, b}))
                    throw IR::LayoutFailure("temporal source unbonder has an unknown footprint");
                continue;
            }
            if (!known.contains(glyph.typeName))
                throw IR::Unsupported("temporal output has an unknown device footprint");
            if (glyph.typeName != "bonder") continue;
            const auto a = Hex(glyph.position);
            const auto b = Add(a, directions.at(glyph.bodyData.at(1)));
            if (!outputCells.contains(a) || !outputCells.contains(b))
                throw IR::LayoutFailure("temporal output bonder has an unknown endpoint");
            ordinaryEndpoints[a].push_back(b);
            ordinaryEndpoints[b].push_back(a);
        }
    }
    // A product atom that is surrounded on all six sides by other product atoms is
    // written before its neighbours arrive, so it never needs an arm that can reach
    // it in the finished layout. Requiring static reachability there rules out every
    // compact product.
    const bool longArmsEnabled = std::getenv("OPUSOPTIMA_SERIAL_LONG_ARMS") != nullptr;
    const bool interiorPortsAllowed = longArmsEnabled;
    auto interiorProduct = [&](Cell cell) {
        if (!interiorPortsAllowed || !outputCells.contains(cell)) return false;
        for (const auto& direction : directions)
            if (!reserved.contains(Add(cell, direction))) return false;
        return true;
    };
    auto accessible = [&](Cell cell) {
        if (carrierCells.contains(cell) || wheelCells.contains(cell) || stageCells.contains(cell))
            return true;
        if (interiorProduct(cell)) return true;
        for (int grip = 0; grip < 6; ++grip) {
            const auto step = directions[grip];
            for (int reach = 1; reach <= (longArmsEnabled ? 3 : 1); ++reach) {
                const auto base =
                    Sub(cell, {step.first * reach, step.second * reach});
                const auto arm = arms.find(base);
                if (arm == arms.end() || armReach.at(base) != reach) continue;
                for (int turn : {1, 5}) {
                    const auto donor = Add(base, directions[(grip + turn) % 6]);
                    if (!arms.contains(donor) &&
                        (!reserved.contains(donor) || feeds.contains(donor) ||
                         !inheritedRoots.empty() || futureOutputCells.contains(donor)))
                        return true;
                }
            }
        }
        return false;
    };
    const bool checkPortGeometry =
        plan.finiteTerminal && accessFlag && std::string(accessFlag) == "1";
    std::vector<Cell> uncovered;
    for (const auto cell : reserved)
        if (!accessible(cell)) {
            if (checkPortGeometry &&
                SerialPortApproaches(cell, reserved, feeds, !inheritedRoots.empty()).empty())
                throw IR::LayoutFailure("serial port has no static approach witness at (" +
                                        std::to_string(cell.first) + "," +
                                        std::to_string(cell.second) + ")");
            uncovered.push_back(cell);
        }
    for (const auto cell : uncovered) {
        if (accessible(cell)) continue;
        bool placed = false;
        std::vector<Cell> protectedPorts;
        for (const auto port : reserved)
            if (accessible(port)) protectedPorts.push_back(port);
        for (int reach = 1; reach <= (longArmsEnabled ? 3 : 1) && !placed; ++reach)
            for (int offset = 0; offset < 6; ++offset) {
                const auto step = directions[(neighborChoice + offset) % 6];
                const auto base = Add(cell, {step.first * reach, step.second * reach});
                if (reserved.contains(base) || arms.contains(base)) continue;
                const auto saved = machine.objects.size();
                addArm(base, reach);
                bool safe = accessible(cell);
                for (const auto port : protectedPorts)
                    safe &= accessible(port);
                if (safe) {
                    placed = true;
                    break;
                }
                arms.erase(base);
                angles.erase(base);
                armReach.erase(base);
                machine.objects.resize(saved);
            }
        if (!placed) {
            const auto* trace = std::getenv("OPUSOPTIMA_PRODUCT_PORT_TRACE");
            if (trace && std::string(trace) == "1" && evidence) {
                std::string detail = "port-geometry spacing=" + std::to_string(spacing) +
                                     " phase=" + std::to_string(latticePhase) +
                                     " neighbor=" + std::to_string(neighborChoice) + " reserved=";
                for (auto x : reserved)
                    detail += std::to_string(x.first) + "," + std::to_string(x.second) + ";";
                detail += " arms=";
                for (auto [x, index] : arms)
                    detail += std::to_string(x.first) + "," + std::to_string(x.second) + ";";
                detail += " feeds=";
                for (auto x : feeds)
                    detail += std::to_string(x.first) + "," + std::to_string(x.second) + ";";
                detail += " protected=";
                for (auto x : protectedPorts)
                    detail += std::to_string(x.first) + "," + std::to_string(x.second) + ";";
                evidence(detail);
            }
            throw IR::LayoutFailure("serial port placement cannot preserve approach access at (" +
                                    std::to_string(cell.first) + "," + std::to_string(cell.second) +
                                    ")");
        }
    }
    std::vector<core::HexCoord> staticWheelFootprint;
    if (closedSingletonWheel) {
        for (const auto cell : wheelCells)
            staticWheelFootprint.push_back(Hex(cell));
        for (const auto& station : coloredStations)
            for (const auto& glyph : station.glyphs)
                for (const auto cell : glyph.vertices)
                    if (wheelCells.contains(cell))
                        throw IR::LayoutFailure("colored output prism intersects wheel device");
    }
    int tick = 2;

    const auto requiredPeriods = 6 * p.outputMultiplier / static_cast<int>(final.outputs[0]);
    const auto repetitions = 1;
    for (int repetition = 0; repetition < repetitions; ++repetition) {
        IR physical;
        auto state = IR::Initial(p);
        FragmentCustody custody;
        std::map<Id, Cell> target;
        std::map<IR::Edge, std::uint8_t> targetBonds;
        for (const auto& step : plan.material.steps)
            if (step.operation.kind == K::Output) {
                const auto ids = IR::Fragment(step.preconditions, step.operation.atoms[0]);
                const auto port = step.operation.index;
                for (auto id : ids)
                    target.emplace(id, Add(outputOrigins.at(port),
                                           Hex(step.preconditions.atoms.at(id).position)));
                for (const auto& [edge, bits] : step.preconditions.bonds)
                    if (std::binary_search(ids.begin(), ids.end(), edge.a))
                        targetBonds.emplace(edge, bits);
            }
        const auto stationMasks = [&]() {
            if (coloredStations.empty()) return std::map<IR::Edge, std::uint8_t>{};
            std::map<Cell, Id> fire;
            for (const auto& [id, atom] : state.atoms)
                if (atom.type == core::AtomType::Fire) fire.emplace(Hex(atom.position), id);
            std::map<IR::Edge, std::uint8_t> actual;
            for (const auto& station : coloredStations)
                for (const auto& glyph : station.glyphs)
                    for (int side = 0; side < 3; ++side) {
                        const auto a = fire.find(glyph.vertices[side]);
                        const auto b = fire.find(glyph.vertices[(side + 1) % 3]);
                        if (a == fire.end() || b == fire.end()) continue;
                        const IR::Edge edge{std::min(a->second, b->second),
                                            std::max(a->second, b->second)};
                        const auto existing = state.bonds.find(edge);
                        if (existing != state.bonds.end() && existing->second == 1) continue;
                        actual[edge] |= prismColors[side];
                    }
            for (const auto& [edge, mask] : actual) {
                const bool bound = std::any_of(
                    coloredStations.begin(), coloredStations.end(), [&](const auto& station) {
                        return station.bonds.contains(edge) && station.bonds.at(edge) == mask &&
                               Hex(state.atoms.at(edge.a).position) == station.sites.at(edge.a) &&
                               Hex(state.atoms.at(edge.b).position) == station.sites.at(edge.b);
                    });
                if (!bound)
                    throw IR::LayoutFailure(
                        "colored output prism creates an unbound or extra color");
            }
            return actual;
        };
        auto append = [&](const IR::Operation& operation) {
            physical.AppendFromTail(p, state, operation);
            (void)stationMasks();
        };
        bool stageDelivered = !stage;
        const auto inStage = [&](Id id) {
            return stage &&
                   (std::find(stage->atoms.begin(), stage->atoms.end(), id) != stage->atoms.end() ||
                    id == stage->salt || id == stage->product);
        };
        auto syncBonds = [&]() {
            if (!stageDelivered) return;
            for (const auto& [edge, mask] : targetBonds) {
                if (!state.atoms.contains(edge.a) || !state.atoms.contains(edge.b)) continue;
                if (Hex(state.atoms.at(edge.a).position) != target.at(edge.a) ||
                    Hex(state.atoms.at(edge.b).position) != target.at(edge.b) ||
                    state.bonds.contains(edge))
                    continue;
                if (mask == 1 && (closedSingletonWheel || temporalOutput)) {
                    const auto a = Hex(state.atoms.at(edge.a).position);
                    const auto b = Hex(state.atoms.at(edge.b).position);
                    if (std::none_of(machine.objects.begin(), machine.objects.end(),
                                     [&](const auto& glyph) {
                                         if (glyph.typeName != "bonder") return false;
                                         const auto x = Hex(glyph.position);
                                         const auto y = Add(x, directions.at(glyph.bodyData.at(1)));
                                         return (x == a && y == b) || (x == b && y == a);
                                     }))
                        throw IR::LayoutFailure(
                            "closed ordinary output bond has no physical glyph witness");
                }
                if (mask != 1 && !coloredStations.empty()) {
                    const auto actual = stationMasks();
                    if (!actual.contains(edge) || actual.at(edge) != mask)
                        throw IR::LayoutFailure(
                            "colored output bond has no exact physical station");
                    custody.Validate(state);
                }
                append({K::Bond, 0, {edge.a, edge.b}, mask});
            }
        };
        auto occupied = [&](Cell cell, Id except) {
            return std::any_of(state.atoms.begin(), state.atoms.end(), [&](const auto& entry) {
                return entry.first != except && Hex(entry.second.position) == cell;
            });
        };
        auto moveScalar = [&](Id id, Cell destination) {
            if (!state.atoms.contains(id))
                throw IR::Error("serial transport references consumed atom");
            const auto origin = Hex(state.atoms.at(id).position);
            if (origin == destination) return;
            if (IR::Fragment(state, id).size() != 1)
                throw IR::Unsupported("serial scalar transport cannot move an assembled fragment");
            std::optional<SerialSingletonContacts> contacts;
            if (temporalOutput)
                contacts.emplace(state, id, ordinaryEndpoints, outputCells, nonOutputCells, target,
                                 targetBonds);
            if (contacts && !contacts->Allows(origin, destination))
                throw IR::LayoutFailure("scalar origin has an uncertified ordinary bonder contact");
            struct Step {
                Cell previous, base;
                int from, to;
            };
            std::set<Cell> traversableFeed;
            for (const auto& port : inputPorts) {
                bool suppressed = false;
                for (const auto& source : p.inputs[port.input].atoms)
                    for (const auto& [other, stock] : state.atoms)
                        if (other != id &&
                            Hex(stock.position) == Add(port.origin, Hex(source.position)))
                            suppressed = true;
                if (suppressed)
                    for (const auto& source : p.inputs[port.input].atoms)
                        traversableFeed.insert(Add(port.origin, Hex(source.position)));
            }
            std::map<Cell, Step> previous;
            std::queue<Cell> queue;
            queue.push(origin);
            previous.emplace(origin, Step{origin, {}, 0, 0});
            std::size_t expanded = 0;
            while (!queue.empty() && !previous.contains(destination)) {
                if (++expanded > 20000 || std::chrono::steady_clock::now() > deadline)
                    throw IR::BudgetFailure("serial arm route state/deadline budget exhausted");
                const auto cell = queue.front();
                queue.pop();
                for (int from = 0; from < 6; ++from) {
                    const auto base = Sub(cell, directions[from]);
                    if (!arms.contains(base)) continue;
                    for (int turn : {1, 5}) {
                        const int to = (from + turn) % 6;
                        const auto next = Add(base, directions[to]);
                        bool feedTransit = traversableFeed.contains(next);
                        if (!feedTransit && feeds.contains(cell) && feeds.contains(next))
                            for (const auto& port : inputPorts) {
                                bool fromPort = false, toPort = false;
                                for (const auto& atom : p.inputs[port.input].atoms) {
                                    const auto footprint = Add(port.origin, Hex(atom.position));
                                    fromPort |= footprint == cell;
                                    toPort |= footprint == next;
                                }
                                feedTransit |= fromPort && toPort;
                            }
                        if (next.first < lowQ || next.first > highQ || next.second < lowR ||
                            next.second > highR || arms.contains(next) || previous.contains(next) ||
                            occupied(next, id) ||
                            (reserved.contains(next) && next != destination && !feedTransit &&
                             !futureOutputCells.contains(next) &&
                             !(stage && inStage(id) && stageCells.contains(next) &&
                               core::Distance(Hex(next), Hex(stage->build)) <= 2)) ||
                            (contacts && !contacts->Allows(next, destination)))
                            continue;
                        bool prematureBond = false;
                        if (!temporalOutput && next != destination)
                            for (const auto& [other, goal] : target) {
                                if (other == id || !state.atoms.contains(other)) continue;
                                if (Hex(state.atoms.at(other).position) == goal &&
                                    core::Distance(Hex(next), Hex(goal)) == 1)
                                    for (const auto& output : p.outputs)
                                        for (const auto& bond : output.bonds) {
                                            for (const auto outputOrigin : outputOrigins) {
                                                const auto a = Add(outputOrigin, Hex(bond.from)),
                                                           b = Add(outputOrigin, Hex(bond.to));
                                                prematureBond |= (a == next && b == goal) ||
                                                                 (b == next && a == goal);
                                            }
                                        }
                            }
                        if (prematureBond) continue;
                        previous.emplace(next, Step{cell, base, from, to});
                        queue.push(next);
                    }
                }
            }
            if (!previous.contains(destination)) {
                std::string context;
                for (const auto& [other, atom] : state.atoms)
                    if (core::Distance(atom.position, Hex(origin)) <= 2)
                        context += " atom" + std::to_string(other) + "@" +
                                   std::to_string(atom.position.q) + "," +
                                   std::to_string(atom.position.r);
                for (const auto dir : directions) {
                    const auto base = Add(origin, dir);
                    if (arms.contains(base))
                        context += " base@" + std::to_string(base.first) + "," +
                                   std::to_string(base.second);
                }
                throw IR::LayoutFailure(
                    "serial atom " + std::to_string(id) + " route blocked from (" +
                    std::to_string(origin.first) + "," + std::to_string(origin.second) + ") to (" +
                    std::to_string(destination.first) + "," + std::to_string(destination.second) +
                    ") explored=" + std::to_string(expanded) + context);
            }
            std::vector<Step> path;
            for (auto cell = destination; cell != origin; cell = previous.at(cell).previous)
                path.push_back(previous.at(cell));
            std::reverse(path.begin(), path.end());
            std::optional<Cell> deliveredBy;
            for (const auto& step : path) {
                auto& tape = machine.objects[arms.at(step.base)].instructions;
                auto& angle = angles.at(step.base);
                // A released scalar is available next cycle. Only an already aligned,
                // distinct receiver may grab during the donor's two idle cycles.
                // Keep this proof local to a single route (no task/input phase crossing).
                if (pipeline && deliveredBy && *deliveredBy != step.base && angle == step.from &&
                    (tape.empty() || tape.back().parameter < tick - 2))
                    tick -= 2;
                while (angle != step.from) {
                    if (closedSingletonWheel &&
                        !CertifyFixedArmInterval(state, Hex(step.base), staticWheelFootprint,
                                                 {0, 1, angle, 0}, {0, 1, (angle + 1) % 6, 0}, 'r',
                                                 false))
                        throw IR::LayoutFailure("closed wheel empty arm interval rejected");
                    tape.push_back({'r', tick++});
                    angle = (angle + 1) % 6;
                }
                custody.Grab(state, arms.at(step.base), Hex(step.previous), id);
                tape.push_back({'G', tick++});
                const char action = (step.to - step.from + 6) % 6 == 1 ? 'r' : 'R';
                const HeldArmState from{0, 1, step.from, 0},
                    to{0, 1, step.to, action == 'r' ? 1 : 5};
                if (!CertifyFixedArmInterval(state, Hex(step.base), staticWheelFootprint, from, to,
                                             action, true))
                    throw IR::LayoutFailure("serial arm interval certificate rejected");
                const auto next = Add(step.base, directions[step.to]);
                if (contacts && !contacts->Allows(next, destination))
                    throw IR::LayoutFailure("scalar ordinary bonder contact witness rejected");
                if (next != destination && futureOutputCells.contains(next) && evidence)
                    evidence("certified future output transit atom=" + std::to_string(id) +
                             " cell=" + std::to_string(next.first) + "," +
                             std::to_string(next.second));
                append(
                    {K::Relocate, 0, {id}, 0, 0, Hex(Sub(next, Hex(state.atoms.at(id).position)))});
                angle = step.to;
                tape.push_back({action, tick++});
                custody.Drop(arms.at(step.base));
                tape.push_back({'g', tick++});
                tick += 2;
                custody.Validate(state);
                deliveredBy = step.base;
            }
        };
        auto move = [&](Id id, Cell destination, const IR::State* expected = nullptr) {
            for (const auto& [slot, stored] : state.stored)
                if (plan.finiteTerminal &&
                    std::find(stored.begin(), stored.end(), id) != stored.end() &&
                    Hex(state.atoms.at(id).position) != destination)
                    throw IR::LayoutFailure("terminal retention custody cannot be displaced");
            const auto members = IR::Fragment(state, id);
            if (members.size() == 1) {
                moveScalar(id, destination);
                return;
            }
            SerialFragmentMotionProblem problem;
            problem.material = state;
            problem.anchor = id;
            problem.destination = Hex(destination);
            problem.pivot = (p.permissionBits & (1ULL << 26)) != 0;
            const auto origin = state.atoms.at(id).position;
            for (int turn = 0; turn < 6; ++turn) {
                bool fits = true;
                for (auto member : members)
                    if (expected) {
                        if (!expected->atoms.contains(member)) {
                            fits = false;
                            break;
                        }
                        fits &= core::HexFrame{{}, turn}.ToWorld(
                                    core::Subtract(state.atoms.at(member).position, origin)) ==
                                core::Subtract(expected->atoms.at(member).position,
                                               expected->atoms.at(id).position);
                    }
                if (fits) problem.goalRotations.push_back(turn);
            }
            for (const auto& [base, index] : arms)
                problem.armBases.push_back(Hex(base));
            std::set<Cell> usableFeed;
            for (const auto& port : inputPorts) {
                bool suppressed = false;
                for (const auto& atom : p.inputs[port.input].atoms)
                    for (const auto& [other, stock] : state.atoms)
                        if (!std::binary_search(members.begin(), members.end(), other) &&
                            Hex(stock.position) == Add(port.origin, Hex(atom.position)))
                            suppressed = true;
                if (suppressed)
                    for (const auto& atom : p.inputs[port.input].atoms)
                        usableFeed.insert(Add(port.origin, Hex(atom.position)));
            }
            problem.executable = [&](const IR::State& world, core::HexCoord armBase, int direction,
                                     char instruction) {
                if (std::chrono::steady_clock::now() > deadline)
                    throw IR::BudgetFailure("fragment route deadline exhausted");
                const bool pivot = instruction == 'p' || instruction == 'P';
                const int turn = instruction == 'r' || instruction == 'p' ? 1 : 5;
                const auto center =
                    pivot ? core::Add(armBase, Hex(directions[direction])) : armBase;
                const core::HexFrame frame{center, turn};
                std::set<Cell> oldCells, newCells;
                for (auto member : members) {
                    oldCells.insert(Hex(world.atoms.at(member).position));
                    newCells.insert(Hex(
                        frame.ToWorld(core::Subtract(world.atoms.at(member).position, center))));
                }
                std::set<Cell> goalCells;
                for (int goalTurn : problem.goalRotations) {
                    std::set<Cell> goal;
                    const core::HexFrame targetFrame{Hex(destination), goalTurn};
                    for (auto member : members)
                        goal.insert(Hex(targetFrame.ToWorld(
                            core::Subtract(state.atoms.at(member).position, origin))));
                    goalCells.insert(goal.begin(), goal.end());
                }
                bool atGoal = false;
                for (int goalTurn : problem.goalRotations) {
                    std::set<Cell> goal;
                    const core::HexFrame targetFrame{Hex(destination), goalTurn};
                    for (auto member : members)
                        goal.insert(Hex(targetFrame.ToWorld(
                            core::Subtract(state.atoms.at(member).position, origin))));
                    atGoal |= newCells == goal;
                }
                if (!atGoal)
                    for (const auto& glyph : machine.objects) {
                        if (glyph.typeName != "bonder") continue;
                        const auto a = Hex(glyph.position);
                        const auto b = Add(a, directions.at(glyph.bodyData.at(1)));
                        if (newCells.contains(a) && newCells.contains(b)) continue;
                        for (const auto& [other, atom] : world.atoms)
                            if (!std::binary_search(members.begin(), members.end(), other) &&
                                ((newCells.contains(a) && Hex(atom.position) == b) ||
                                 (newCells.contains(b) && Hex(atom.position) == a)))
                                return false;
                    }
                auto activeFeed = usableFeed;
                for (const auto& port : inputPorts) {
                    bool blocked = false;
                    for (const auto& atom : p.inputs[port.input].atoms)
                        blocked |= oldCells.contains(Add(port.origin, Hex(atom.position)));
                    if (blocked)
                        for (const auto& atom : p.inputs[port.input].atoms)
                            activeFeed.insert(Add(port.origin, Hex(atom.position)));
                }
                for (auto cell : newCells)
                    if (cell.first < lowQ || cell.first > highQ || cell.second < lowR ||
                        cell.second > highR || arms.contains(cell) ||
                        (reserved.contains(cell) && !goalCells.contains(cell) &&
                         !oldCells.contains(cell) && !activeFeed.contains(cell) &&
                         !dockingCells.contains(cell) &&
                         !(stage && stageCells.contains(cell) &&
                           cell.first > stage->build.first + 3 &&
                           cell.second < stage->build.second + 3)))
                        return false;
                auto certificateWorld = world;
                int radius = 1;
                for (auto member : members)
                    radius =
                        std::max(radius, core::Distance(armBase, world.atoms.at(member).position));
                Id fake = std::numeric_limits<Id>::max();
                for (const auto& port : inputPorts) {
                    bool blocked = false;
                    for (const auto& atom : p.inputs[port.input].atoms)
                        for (const auto& [other, stock] : world.atoms)
                            blocked |= Hex(stock.position) == Add(port.origin, Hex(atom.position));
                    if (!blocked)
                        for (const auto& atom : p.inputs[port.input].atoms)
                            certificateWorld.atoms.emplace(
                                fake--,
                                IR::Atom{atom.type, Hex(Add(port.origin, Hex(atom.position)))});
                }
                for (const auto& [base, index] : arms)
                    if (Hex(base) != armBase && core::Distance(Hex(base), armBase) <= radius + 1)
                        certificateWorld.atoms.emplace(fake--,
                                                       IR::Atom{core::AtomType::Salt, Hex(base)});
                return CertifyFixedArmInterval(certificateWorld, armBase, {}, {0, 1, direction, 0},
                                               {0, 1, (direction + (pivot ? 0 : turn)) % 6, turn},
                                               instruction, true);
            };
            const auto route = RouteSerialFragment(problem);
            if (route.status == MotionStatus::Exhausted)
                throw IR::BudgetFailure("fragment route state budget exhausted");
            if (route.status != MotionStatus::Found)
                throw IR::LayoutFailure(
                    "inherited fragment " + std::to_string(id) + " cannot move from (" +
                    std::to_string(origin.q) + "," + std::to_string(origin.r) + ") to (" +
                    std::to_string(destination.first) + "," + std::to_string(destination.second) +
                    ") explored=" + std::to_string(route.explored) +
                    " closest=" + std::to_string(route.closestDistance));
            for (const auto& step : route.moves) {
                const auto base = Hex(step.base);
                auto& tape = machine.objects[arms.at(base)].instructions;
                auto& angle = angles.at(base);
                while (angle != step.direction) {
                    tape.push_back({'r', tick++});
                    angle = (angle + 1) % 6;
                }
                custody.Grab(state, arms.at(base), state.atoms.at(step.grip).position, step.grip);
                tape.push_back({'G', tick++});
                const bool pivot = step.instruction == 'p' || step.instruction == 'P';
                const int turn = step.instruction == 'r' || step.instruction == 'p' ? 1 : 5;
                const auto center = pivot ? state.atoms.at(step.grip).position : step.base;
                append({K::Relocate,
                        0,
                        {id},
                        0,
                        turn,
                        core::Subtract(center, core::HexFrame{{}, turn}.ToWorld(center))});
                tape.push_back({step.instruction, tick++});
                angle = (angle + (pivot ? 0 : turn)) % 6;
                custody.Drop(arms.at(base));
                tape.push_back({'g', tick++});
                tick += 2;
                custody.Validate(state);
            }
        };
        auto park = [&](Id id) {
            const auto beforeState = state;
            const auto beforeProgram = physical;
            const auto beforeCustody = custody;
            const auto beforeMachine = machine;
            const auto beforeAngles = angles;
            const auto beforeTick = tick;
            std::string last;
            for (auto cell : cache)
                if (std::none_of(retentionTargets.begin(), retentionTargets.end(),
                                 [&](const auto& endpoint) { return endpoint.second == cell; }) &&
                    !occupied(cell, id) && Hex(state.atoms.at(id).position) != cell) {
                    try {
                        move(id, cell);
                        return;
                    } catch (const IR::LayoutFailure& error) {
                        last = error.what();
                        state = beforeState;
                        physical = beforeProgram;
                        custody = beforeCustody;
                        machine = beforeMachine;
                        angles = beforeAngles;
                        tick = beforeTick;
                    }
                }
            throw IR::LayoutFailure("serial arm cache placement exhausted: " + last);
        };
        auto clearFeedCenter = [&](Id id) {
            if (IR::Fragment(state, id).size() != 1) return;
            const auto cell = Hex(state.atoms.at(id).position);
            for (const auto& port : inputPorts)
                for (const auto& source : p.inputs[port.input].atoms) {
                    if (Add(port.origin, Hex(source.position)) != cell) continue;
                    const auto degree = std::count_if(
                        p.inputs[port.input].bonds.begin(), p.inputs[port.input].bonds.end(),
                        [&](const auto& bond) {
                            return bond.from == source.position || bond.to == source.position;
                        });
                    if (degree < 3) continue;
                    std::vector<Id> blockers;
                    for (const auto& atom : p.inputs[port.input].atoms)
                        for (const auto& [other, stock] : state.atoms)
                            if (other != id &&
                                Hex(stock.position) == Add(port.origin, Hex(atom.position)))
                                blockers.push_back(other);
                    if (blockers.size() < 2) continue;
                    for (auto other : blockers)
                        park(other);
                }
        };
        std::size_t carrierAt = carrierStart;
        auto carryStage = [&](Id endpoint, std::size_t destination) {
            if (!stage || !carrier) throw IR::Error("missing endpoint carrier binding");
            const auto grip =
                std::find_if(stage->relative.begin(), stage->relative.end(),
                             [](const auto& atom) { return atom.second == Cell{0, -1}; });
            if (grip == stage->relative.end())
                throw IR::Unsupported("endpoint carrier needs an exposed lower grip");
            const auto arm = *carrier;
            const auto startBase = carrierTrack[carrierAt];
            custody.Grab(state, arm, Hex(Add(startBase, {0, 1})), grip->first);
            auto& tape = machine.objects[arm].instructions;
            tape.push_back({'G', tick++});
            while (carrierAt != destination) {
                const auto next = carrierAt + (destination > carrierAt ? 1 : -1);
                const auto delta = Sub(carrierTrack[next], carrierTrack[carrierAt]);
                const auto fragment = IR::Fragment(state, endpoint);
                for (auto member : fragment) {
                    const auto cell = Add(Hex(state.atoms.at(member).position), delta);
                    if (arms.contains(cell) || wheelCells.contains(cell))
                        throw IR::LayoutFailure("endpoint carrier collides with a device");
                    for (const auto& [other, atom] : state.atoms)
                        if (!std::binary_search(fragment.begin(), fragment.end(), other) &&
                            Hex(atom.position) == cell)
                            throw IR::LayoutFailure("endpoint carrier crosses parked inventory");
                }
                append({K::Relocate, 0, {endpoint}, 0, 0, Hex(delta)});
                tape.push_back({destination > carrierAt ? 'A' : 'a', tick++});
                carrierAt = next;
            }
            custody.Drop(arm);
            tape.push_back({'g', tick++});
            tick += 2;
            custody.Validate(state);
        };
        auto assembleStage = [&]() {
            if (!stage) throw IR::Error("missing endpoint precursor");
            std::vector<Id> order{stage->endpoint};
            for (auto atom : stage->atoms)
                if (atom != stage->endpoint) order.push_back(atom);
            for (auto atom : order) {
                if (stage->relative.at(atom) == Cell{1, 0}) {
                    move(atom, feederDonor);
                    auto& tape = machine.objects[*stageFeeder].instructions;
                    custody.Grab(state, *stageFeeder, Hex(feederDonor), atom);
                    tape.push_back({'G', tick++});
                    if (!CertifyFixedArmInterval(state, Hex(feederBase), {}, {0, 3, 0, 0},
                                                 {0, 3, 1, 1}, 'r', true))
                        throw IR::LayoutFailure("precursor feeder swept interval rejected");
                    append({K::Relocate,
                            0,
                            {atom},
                            0,
                            1,
                            core::Subtract(Hex(feederBase),
                                           core::HexFrame{{}, 1}.ToWorld(Hex(feederBase)))});
                    tape.push_back({'r', tick++});
                    custody.Drop(*stageFeeder);
                    tape.push_back({'g', tick++});
                    tape.push_back({'R', tick++});
                    tick += 2;
                } else
                    move(atom, Add(stage->build, stage->relative.at(atom)));
                for (const auto& [edge, bits] : stage->bonds) {
                    if (state.bonds.contains(edge)) continue;
                    if (Hex(state.atoms.at(edge.a).position) ==
                            Add(stage->build, stage->relative.at(edge.a)) &&
                        Hex(state.atoms.at(edge.b).position) ==
                            Add(stage->build, stage->relative.at(edge.b)))
                        append({K::Bond, 0, {edge.a, edge.b}, bits});
                }
            }
        };
        std::vector<std::size_t> completed;
        for (const auto& task : plan.tasks) {
            if (std::chrono::steady_clock::now() >= deadline)
                throw IR::BudgetFailure("shared material/physical deadline exhausted");
            for (auto dependency : task.dependsOn)
                if (dependency >= task.step) throw IR::Error("noncausal material dependency");
            const auto& original = plan.material.steps.at(task.step);
            auto op = original.operation;
            if (op.kind == K::Acquire) {
                const auto& port = inputPorts.at(portForAcquire.at(task.step));
                const auto origin = port.origin;
                std::vector<Id> blockers;
                for (const auto& [id, atom] : state.atoms)
                    for (const auto& incoming : p.inputs.at(op.index).atoms)
                        if (Hex(atom.position) == Add(origin, Hex(incoming.position)))
                            blockers.push_back(id);
                for (auto id : blockers)
                    park(id);
                const auto& frame = frameForAcquire.at(task.step);
                append({K::Acquire, op.index, {}, 0, frame.Turns(), frame.origin});
                tick += 2;
                const auto bonds = state.bonds;
                for (const auto& [edge, bits] : bonds)
                    if (edge.a >= original.preconditions.nextId &&
                        edge.b >= original.preconditions.nextId &&
                        !keepForAcquire.at(task.step).contains(
                            {edge.a - original.preconditions.nextId,
                             edge.b - original.preconditions.nextId})) {
                        if ((closedSingletonWheel || temporalOutput) &&
                            !SerialSourceCutWitness(
                                state, edge, bits, machine.objects, feeds, nonOutputCells,
                                temporalOutput ? outputCells : std::set<Cell>{}, p.permissionBits))
                            throw IR::LayoutFailure(
                                "closed source Unbond has no physical glyph witness");
                        append({K::Unbond, 0, {edge.a, edge.b}, bits});
                        custody.Validate(state);
                    }
                if (temporalOutput)
                    for (Id id = original.preconditions.nextId; id < state.nextId; ++id)
                        if (IR::Fragment(state, id).size() != 1)
                            throw IR::LayoutFailure("temporal source extraction retained a bond");
            } else if (op.kind == K::Relocate) {
                const auto id = op.atoms[0];
                if (stage && !stageDelivered) {
                    if (inStage(id)) continue;
                    const auto members = IR::Fragment(state, id);
                    if (std::any_of(members.begin(), members.end(), [&](Id member) {
                            return target.contains(member) &&
                                   stageCells.contains(target.at(member));
                        }))
                        continue;
                }
                if (!target.contains(id)) {
                    const auto endpoint = std::find_if(
                        plan.retentionEndpoints.begin(), plan.retentionEndpoints.end(),
                        [&](const auto& binding) {
                            return binding.atom == id && binding.relocateStep == task.step;
                        });
                    if (endpoint == plan.retentionEndpoints.end() ||
                        state.atoms.at(id).type != endpoint->type ||
                        IR::Fragment(state, id).size() != 1)
                        throw IR::Unsupported("material relocation is not a terminal delivery");
                    const auto destination = retentionTargets.at(id);
                    if (occupied(destination, id))
                        throw IR::LayoutFailure("finite physical slot occupied");
                    move(id, destination);
                    custody.Validate(state);
                    completed.push_back(task.step);
                    continue;
                }
                const auto fragment = IR::Fragment(state, id);
                const bool unfinished = std::any_of(
                    plan.tasks.begin() + task.step + 1, plan.tasks.end(), [&](const auto& future) {
                        if (future.kind != K::Project && future.kind != K::Calcify) return false;
                        const auto operand = plan.material.steps[future.step].operation.atoms.at(0);
                        return std::binary_search(fragment.begin(), fragment.end(), operand);
                    });
                const bool occupiedProduct =
                    std::any_of(target.begin(), target.end(), [&](const auto& entry) {
                        return !std::binary_search(fragment.begin(), fragment.end(), entry.first) &&
                               state.atoms.contains(entry.first) &&
                               Hex(state.atoms.at(entry.first).position) == entry.second;
                    });
                if (unfinished && occupiedProduct) continue;
                move(id, target.at(id), &original.effects);
                syncBonds();
            } else if (op.kind == K::Unbond || op.kind == K::Bond) {
                // Source edges were physically cut at their input ports. The
                // always-on output bonders join final atoms on arrival.
            } else if (op.kind == K::Store) {
                if (plan.finiteTerminal) {
                    if (op.index >= plan.finiteTerminal->capacity)
                        throw IR::LayoutFailure("finite physical slot capacity exceeded");
                    const auto id = op.atoms.at(0);
                    const auto endpoint =
                        std::find_if(plan.retentionEndpoints.begin(), plan.retentionEndpoints.end(),
                                     [&](const auto& binding) {
                                         return binding.storeStep == task.step &&
                                                binding.atom == id && binding.slot == op.index;
                                     });
                    if (endpoint == plan.retentionEndpoints.end() ||
                        state.atoms.at(id).type != endpoint->type)
                        throw IR::Unsupported("finite Store endpoint mismatch");
                    const auto destination = retentionTargets.at(id);
                    if (occupied(destination, id))
                        throw IR::LayoutFailure("finite physical slot occupied");
                    if (Hex(state.atoms.at(id).position) != destination)
                        throw IR::LayoutFailure("finite Store not at bound physical endpoint");
                    append(op);
                    custody.Validate(state);
                } else
                    park(op.atoms.at(0));
            } else if (op.kind == K::Retrieve) {
            } else if (op.kind == K::Project) {
                move(op.atoms[1], mercury);
                clearFeedCenter(op.atoms[0]);
                move(op.atoms[0], metal);
                const auto product = state.nextId;
                append(op);
                append({K::Relocate,
                        0,
                        {product},
                        0,
                        0,
                        Hex(Sub(metal, Hex(state.atoms.at(product).position)))});
                tick += 2;
                if (stage && !stageDelivered && target.contains(product)) {
                    const auto members = IR::Fragment(state, product);
                    if (std::any_of(members.begin(), members.end(), [&](Id atom) {
                            return target.contains(atom) && stageCells.contains(target.at(atom));
                        }))
                        park(product);
                }
            } else if (op.kind == K::Animismus) {
                const auto vitae = Add(animismus, {0, 1});
                const auto mors = Add(animismus, {1, -1});
                std::vector<Id> outletStock;
                for (const auto& [other, atom] : state.atoms)
                    if (Hex(atom.position) == vitae || Hex(atom.position) == mors)
                        outletStock.push_back(other);
                for (auto other : outletStock)
                    park(other);
                clearFeedCenter(op.atoms.at(0));
                move(op.atoms.at(0), animismus);
                clearFeedCenter(op.atoms.at(1));
                move(op.atoms.at(1), Add(animismus, {1, 0}));
                const auto product = state.nextId;
                const auto before = state;
                append(op);
                append({K::Relocate, 0, {product}, 0, 0, Hex(vitae)});
                append({K::Relocate, 0, {product + 1}, 0, 0, Hex(mors)});
                custody.ConsumeSingletons(before, state, op.atoms);
                custody.Validate(state);
                tick += 3;
            } else if (op.kind == K::Purify) {
                std::vector<Id> outletStock;
                for (const auto& [other, atom] : state.atoms)
                    if (Hex(atom.position) == purified) outletStock.push_back(other);
                for (auto other : outletStock)
                    park(other);
                clearFeedCenter(op.atoms[0]);
                move(op.atoms[0], purifier);
                clearFeedCenter(op.atoms[1]);
                move(op.atoms[1], Add(purifier, {1, 0}));
                const auto product = state.nextId;
                append(op);
                append({K::Relocate, 0, {product}, 0, 0, Hex(purified)});
                tick += 3;
            } else if (op.kind == K::Discard) {
                move(op.atoms[0], disposal);
                append(op);
                tick += 2;
            } else if (op.kind == K::Calcify) {
                if (stage && op.atoms.at(0) == stage->endpoint) {
                    assembleStage();
                    carryStage(stage->endpoint, 0);
                    append(op);
                    tick += 2;
                } else {
                    if (animates) {
                        std::vector<Id> stock;
                        for (const auto& [other, atom] : state.atoms)
                            if (other != op.atoms[0] && Hex(atom.position) == calcifier)
                                stock.push_back(other);
                        for (auto other : stock)
                            park(other);
                        clearFeedCenter(op.atoms[0]);
                    }
                    move(op.atoms[0], calcifier);
                    const auto product = state.nextId;
                    append(op);
                    append({K::Relocate, 0, {product}, 0, 0, Hex(calcifier)});
                    tick += 2;
                }
            } else if (op.kind == K::Duplicate && op.wheelCatalyst) {
                if (monomers.contains(op.atoms.at(0))) {
                    if (IR::Fragment(state, op.atoms.at(0)).size() != 1 ||
                        state.atoms.at(op.atoms.at(0)).type != core::AtomType::Salt)
                        throw IR::Unsupported("monomer wheel physical substrate mismatch");
                    move(op.atoms.at(0), monomerCells.at(op.atoms.at(0)));
                    append(op);
                    tick += 2;
                    custody.Validate(state);
                    if (evidence)
                        evidence("monomer wheel bound product=" + std::to_string(state.nextId - 1));
                    continue;
                }
                if (!stage || op.atoms.at(0) != stage->salt)
                    throw IR::Unsupported("wheel task has no staged substrate");
                append(op);
                tick += 2;
                carryStage(stage->product, carrierTrack.size() - 1);
                stageDelivered = true;
                const IR::State* outputState = nullptr;
                for (const auto& taskStep : plan.material.steps)
                    if (taskStep.operation.kind == K::Output &&
                        taskStep.preconditions.atoms.contains(stage->product))
                        outputState = &taskStep.preconditions;
                if (!outputState) throw IR::Error("staged precursor has no output identity");
                for (const auto& [atom, cell] : target) {
                    if (!state.atoms.contains(atom) || inStage(atom) ||
                        Hex(state.atoms.at(atom).position) == cell)
                        continue;
                    if (fragmentFeeder &&
                        IR::Fragment(state, atom) == IR::Fragment(state, fragmentFeedGrip)) {
                        auto donorState = *outputState;
                        const core::HexFrame inverse{Hex(fragmentFeederBase), 5};
                        for (auto member : IR::Fragment(state, atom))
                            donorState.atoms.at(member).position = inverse.ToWorld(
                                core::Subtract(Hex(target.at(member)), Hex(fragmentFeederBase)));
                        move(fragmentFeedGrip, Hex(donorState.atoms.at(fragmentFeedGrip).position),
                             &donorState);
                        auto& tape = machine.objects[*fragmentFeeder].instructions;
                        custody.Grab(state, *fragmentFeeder,
                                     donorState.atoms.at(fragmentFeedGrip).position,
                                     fragmentFeedGrip);
                        tape.push_back({'G', tick++});
                        if (!CertifyFixedArmInterval(state, Hex(fragmentFeederBase), {},
                                                     {0, 3, 3, 0}, {0, 3, 4, 1}, 'r', true))
                            throw IR::LayoutFailure("fragment feeder swept interval rejected");
                        append({K::Relocate,
                                0,
                                {fragmentFeedGrip},
                                0,
                                1,
                                core::Subtract(
                                    Hex(fragmentFeederBase),
                                    core::HexFrame{{}, 1}.ToWorld(Hex(fragmentFeederBase)))});
                        tape.push_back({'r', tick++});
                        custody.Drop(*fragmentFeeder);
                        tape.push_back({'g', tick++});
                        tape.push_back({'R', tick++});
                        tick += 2;
                    } else
                        move(atom, cell, outputState);
                    syncBonds();
                }
                syncBonds();
            } else if (op.kind == K::Duplicate) {
                // One atom goes onto the duplication glyph and the glyph emits an
                // extra copy at the outlet just outside its reserved footprint.
                const auto outlet = Add(dup, {0, 1});
                std::vector<Id> stock;
                for (const auto& [other, atom] : state.atoms)
                    if (Hex(atom.position) == outlet) stock.push_back(other);
                for (auto other : stock)
                    park(other);
                clearFeedCenter(op.atoms.at(0));
                move(op.atoms.at(0), dup);
                const auto product = state.nextId;
                append(op);
                append({K::Relocate, 0, {product}, 0, 0, Hex(outlet)});
                tick += 2;
            } else if (op.kind == K::Output) {
                syncBonds();
                for (const auto& station : coloredStations)
                    if (station.step == task.step) {
                        custody.Validate(state);
                        for (const auto& [id, cell] : station.sites) {
                            if (Hex(state.atoms.at(id).position) != cell ||
                                state.atoms.at(id).type != core::AtomType::Fire ||
                                std::any_of(custody.Held().begin(), custody.Held().end(),
                                            [&](const auto& held) { return held.second == id; }))
                                throw IR::LayoutFailure("colored output station custody mismatch");
                        }
                        const auto actual = stationMasks();
                        for (const auto& [edge, mask] : station.bonds)
                            if (!actual.contains(edge) || actual.at(edge) != mask ||
                                !state.bonds.contains(edge) || state.bonds.at(edge) != mask)
                                throw IR::LayoutFailure("colored output station mask mismatch");
                    }
                if (plan.finiteTerminal && op.index == 0 &&
                    state.outputs.at(0) + 1 == plan.finiteTerminal->outputs.at(0)) {
                    // Recompute only a preview: real counts are not complete yet.
                    auto preview = physical;
                    auto projected = state;
                    preview.AppendFromTail(p, projected, op);
                    std::map<Id, core::HexCoord> bound;
                    for (const auto& [id, cell] : retentionTargets)
                        bound.emplace(id, core::HexCoord{cell.first, cell.second});
                    RequireFinitePhysicalTerminal(projected, *plan.finiteTerminal, bound);
                    custody.Validate(projected);
                }
                append(op);
                tick += 3;
            } else {
                if (std::getenv("OPUSOPTIMA_TASK_DIAG"))
                    std::fprintf(stderr, "TASKDIAG kind=%d catalysts=%d atoms=%zu\n",
                                 static_cast<int>(op.kind), 0, op.atoms.size());
                throw IR::Unsupported("serial arm material reaction not yet implemented");
            }
            completed.push_back(task.step);
        }
        if (plan.finiteTerminal) {
            IR::RequireFiniteTerminal(state, *plan.finiteTerminal);
            custody.Validate(state);
            for (const auto& endpoint : plan.retentionEndpoints) {
                const auto position = Hex(state.atoms.at(endpoint.atom).position);
                if (position != retentionTargets.at(endpoint.atom))
                    throw IR::LayoutFailure("terminal retention departed bound endpoint");
                if (evidence)
                    evidence("retention atom=" + std::to_string(endpoint.atom) +
                             " type=" + std::to_string(static_cast<int>(endpoint.type)) +
                             " slot=" + std::to_string(endpoint.slot) +
                             " q=" + std::to_string(position.first) +
                             " r=" + std::to_string(position.second));
            }
        } else
            IR::RequireComplete(state, final.outputs);
        if (carrier)
            while (carrierAt != carrierStart) {
                machine.objects[*carrier].instructions.push_back(
                    {carrierAt < carrierStart ? 'A' : 'a', tick++});
                carrierAt += carrierAt < carrierStart ? 1 : -1;
            }
        if (physical.Replay(p) != state) throw IR::Error("serial arm material ledger mismatch");
    }
    for (const auto& [base, index] : arms) {
        auto& angle = angles.at(base);
        auto& tape = machine.objects[index].instructions;
        while (angle) {
            if (closedSingletonWheel &&
                !CertifyFixedArmInterval(IR::Initial(p), Hex(base), staticWheelFootprint,
                                         {0, 1, angle, 0}, {0, 1, (angle + 1) % 6, 0}, 'r', false))
                throw IR::LayoutFailure("closed wheel arm reset intersects device");
            tape.push_back({'r', tick++});
            angle = (angle + 1) % 6;
        }
    }
    for (const auto& [base, index] : arms)
        if (!machine.objects[index].instructions.empty())
            machine.objects[index].instructions.push_back({'O', tick});
    if (carrier) machine.objects[*carrier].instructions.push_back({'O', tick});
    if (stageFeeder) machine.objects[*stageFeeder].instructions.push_back({'O', tick});
    if (fragmentFeeder) machine.objects[*fragmentFeeder].instructions.push_back({'O', tick});
    std::erase_if(machine.objects, [](const auto& part) {
        return part.typeName == "arm1" && part.instructions.empty();
    });
    int armNumber = 0;
    for (auto& part : machine.objects)
        if (part.typeName == "arm1" || part.typeName == "baron") part.bodyData[4] = armNumber++;
    machine.metrics.cycles = (tick + 1) * requiredPeriods;
    return machine;
}
} // namespace
static std::optional<io::SolutionData> CompileAcceptedSerialArmExecutionWithSourcePolicy(
    const io::PuzzleData& p, const ExecutionPlan& material,
    const std::function<bool(const io::SolutionData&)>& accept,
    const std::function<void(const std::string&)>& feedback,
    std::chrono::steady_clock::time_point deadline,
    const std::function<void(SerialArmFailure, const std::string&)>& classifiedFeedback,
    const std::function<void(const io::SolutionData&, const io::SolutionData&)>& acceptedPipeline,
    bool sparseSourceArms, SupplyMaterialLoweringPolicy policy) {
    deadline = std::min(deadline, std::chrono::steady_clock::now() + std::chrono::seconds(3));
    const auto* fragmentDock = std::getenv("OPUSOPTIMA_FRAGMENT_PRODUCT_DOCKING");
    const bool wholeFragment = fragmentDock && std::string(fragmentDock) == "1";
    std::set<std::string> tested;
    for (int spacing : {8, 12})
        for (int phase : {0, 1, 2})
            for (int neighbor : {0, 2, 4}) {
                if (wholeFragment && (spacing != 8 || phase != 0 || neighbor != 0)) continue;
                if (std::chrono::steady_clock::now() >= deadline) {
                    if (classifiedFeedback)
                        classifiedFeedback(SerialArmFailure::BudgetExhausted,
                                           "shared material/physical deadline exhausted");
                    return {};
                }
                std::optional<io::SolutionData> machine;
                try {
                    machine = Compile(p, material, spacing, phase, neighbor, deadline, false, false,
                                      feedback, sparseSourceArms, policy);
                } catch (const IR::Error& error) {
                    if (classifiedFeedback) {
                        const auto category =
                            dynamic_cast<const IR::BudgetFailure*>(&error)
                                ? SerialArmFailure::BudgetExhausted
                            : dynamic_cast<const IR::LayoutFailure*>(&error)
                                ? SerialArmFailure::LayoutCollision
                            : dynamic_cast<const IR::Unsupported*>(&error) ||
                                    dynamic_cast<const IR::PermissionFailure*>(&error)
                                ? SerialArmFailure::Unsupported
                                : SerialArmFailure::MechanicalFailure;
                        classifiedFeedback(category, error.what());
                    }
                    if (feedback) feedback(error.what());
                    continue;
                }
                if (!tested.insert(PhysicalCandidateKey(*machine)).second) continue;
                if (accept(*machine)) {
                    const auto* fragments = std::getenv("OPUSOPTIMA_SERIAL_FRAGMENTS");
                    if (acceptedPipeline && !(fragments && std::string(fragments) == "1") &&
                        std::chrono::steady_clock::now() < deadline) {
                        try {
                            auto child = Compile(p, material, spacing, phase, neighbor, deadline,
                                                 false, true, {}, sparseSourceArms, policy);
                            if (std::chrono::steady_clock::now() < deadline &&
                                tested.insert(PhysicalCandidateKey(child)).second)
                                acceptedPipeline(*machine, child);
                        } catch (const std::runtime_error& error) {
                            if (feedback) feedback(error.what());
                        }
                    }
                    const auto* serial = std::getenv("OPUSOPTIMA_SERIAL_HANDOFF");
                    if (acceptedPipeline && !(serial && std::string(serial) == "1") &&
                        std::chrono::steady_clock::now() < deadline) {
                        std::optional<io::SolutionData> child;
                        try {
                            child = Compile(p, material, spacing, phase, neighbor, deadline, true,
                                            false, {}, sparseSourceArms, policy);
                        } catch (const IR::Error& error) {
                            if (feedback) feedback(error.what());
                        }
                        if (child && std::chrono::steady_clock::now() < deadline &&
                            tested.insert(PhysicalCandidateKey(*child)).second) {
                            try {
                                acceptedPipeline(*machine, *child);
                            } catch (const std::runtime_error& error) {
                                if (feedback) feedback(error.what());
                            }
                        }
                    }
                    return machine;
                }
            }
    return {};
}

std::optional<io::SolutionData> CompileAcceptedSerialArmExecution(
    const io::PuzzleData& p, const ExecutionPlan& material,
    const std::function<bool(const io::SolutionData&)>& accept,
    const std::function<void(const std::string&)>& feedback,
    std::chrono::steady_clock::time_point deadline,
    const std::function<void(SerialArmFailure, const std::string&)>& classifiedFeedback,
    const std::function<void(const io::SolutionData&, const io::SolutionData&)>& acceptedPipeline) {
    return CompileAcceptedSerialArmExecutionWithSourcePolicy(
        p, material, accept, feedback, deadline, classifiedFeedback, acceptedPipeline, false,
        SupplyMaterialLoweringPolicy::PreserveCompatible);
}

std::optional<io::SolutionData> CompileAcceptedSerialArmExecutionWithPolicy(
    const io::PuzzleData& p, const ExecutionPlan& material, SupplyMaterialLoweringPolicy policy,
    const std::function<bool(const io::SolutionData&)>& accept,
    const std::function<void(const std::string&)>& feedback,
    std::chrono::steady_clock::time_point deadline,
    const std::function<void(SerialArmFailure, const std::string&)>& classifiedFeedback,
    const std::function<void(const io::SolutionData&, const io::SolutionData&)>& acceptedPipeline) {
    return CompileAcceptedSerialArmExecutionWithSourcePolicy(p, material, accept, feedback,
                                                             deadline, classifiedFeedback,
                                                             acceptedPipeline, false, policy);
}

std::optional<io::SolutionData> CompileAcceptedSparseSourceSerialArmExecution(
    const io::PuzzleData& p, const ExecutionPlan& material,
    const std::function<bool(const io::SolutionData&)>& accept,
    const std::function<void(const std::string&)>& feedback,
    std::chrono::steady_clock::time_point deadline) {
    return CompileAcceptedSerialArmExecutionWithSourcePolicy(
        p, material, accept, feedback, deadline, {}, {}, true,
        SupplyMaterialLoweringPolicy::PreserveCompatible);
}
} // namespace opusoptima::solvers
