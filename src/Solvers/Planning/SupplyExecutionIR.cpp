#include "SupplyExecutionIR.hpp"
#include "InventorySearch.hpp"
#include <algorithm>
#include <cstdlib>
#include <optional>
#include <set>
#include <tuple>

namespace opusoptima::solvers {
bool SupportsOrdinarySourceCutLowering(const io::PuzzleData& p) {
    if (p.productionInfo || p.inputs.empty() || p.outputs.empty() ||
        !(p.permissionBits & (1ULL << 8)))
        return false;
    for (const auto& input : p.inputs) {
        if (input.atoms.size() == 1 && input.bonds.empty()) continue;
        if (input.atoms.size() != 2 || input.bonds.size() != 1 || !(p.permissionBits & (1ULL << 9)))
            return false;
        const auto& bond = input.bonds.front();
        const auto a = input.atoms[0].position, b = input.atoms[1].position;
        if (a == b || bond.bits != 1 ||
            !((bond.from == a && bond.to == b) || (bond.from == b && bond.to == a)))
            return false;
    }
    return std::all_of(p.outputs.begin(), p.outputs.end(), [](const auto& output) {
        return output.atoms.size() > 1 && !output.bonds.empty() &&
               std::all_of(output.bonds.begin(), output.bonds.end(),
                           [](const auto& bond) { return bond.bits == 1; });
    });
}
bool SupportsClosedCardinalLife(const io::PuzzleData& p) {
    using T = core::AtomType;
    constexpr auto permissions = (1ULL << 0) | (1ULL << 8) | (1ULL << 9) | (1ULL << 12) |
                                 (1ULL << 16) | (1ULL << 22) | (1ULL << 23) | (1ULL << 25);
    if (p.version != 3 || p.productionInfo || p.outputMultiplier != 1 || p.inputs.empty() ||
        p.inputs.size() > 4 || p.outputs.size() != 1 ||
        (p.permissionBits & permissions) != permissions)
        return false;
    const auto cardinal = [](T t) {
        return t == T::Air || t == T::Earth || t == T::Fire || t == T::Water;
    };
    std::set<T> available;
    bool dimer = false;
    for (const auto& input : p.inputs) {
        if (input.atoms.empty() || input.atoms.size() > 2 ||
            input.bonds.size() != input.atoms.size() - 1)
            return false;
        dimer |= input.atoms.size() == 2;
        for (const auto& atom : input.atoms) {
            if (!cardinal(atom.type)) return false;
            available.insert(atom.type);
        }
    }
    const auto& output = p.outputs.front();
    if (!dimer || output.atoms.size() < 2 || output.atoms.size() > 16 ||
        output.bonds.size() > 3 * output.atoms.size() || !SupportsOrdinarySourceCutLowering(p))
        return false;
    std::size_t vitae = 0, mors = 0;
    for (const auto& atom : output.atoms) {
        vitae += atom.type == T::Vitae;
        mors += atom.type == T::Mors;
        if (atom.type != T::Vitae && atom.type != T::Mors && atom.type != T::Salt &&
            !available.contains(atom.type))
            return false;
    }
    if (!vitae || vitae != mors) return false;
    try {
        (void)ExecutionIR::Initial(p);
    } catch (const ExecutionIR::Error&) {
        return false;
    }
    return true;
}
namespace {
ExecutionIR BuildPeriod(const io::PuzzleData& p, const JointSupplyPlan& supply,
                        const std::vector<core::HexFrame>& outputFrames,
                        const std::vector<core::HexFrame>& inputFrames,
                        SupplyMaterialLoweringPolicy policy) {
    const bool closedLife = policy == SupplyMaterialLoweringPolicy::ClosedCardinalLife;
    if (closedLife && !SupportsClosedCardinalLife(p))
        throw ExecutionIR::Unsupported("closed cardinal life domain required");
    const bool cutSources = policy != SupplyMaterialLoweringPolicy::PreserveCompatible;
    if (cutSources && (!SupportsOrdinarySourceCutLowering(p) ||
                       std::any_of(supply.steps.begin(), supply.steps.end(), [](const auto& step) {
                           return step.supply.retainProduct ||
                                  step.supply.wheelCatalyst.has_value();
                       })))
        throw ExecutionIR::Unsupported(
            "ordinary source cut lowering requires unretained singleton/dimer temporal supply");
    if (p.outputs.empty() || (!outputFrames.empty() && outputFrames.size() != p.outputs.size()))
        throw ExecutionIR::Error("output frame dimension mismatch");
    if (!inputFrames.empty() && inputFrames.size() != p.inputs.size())
        throw ExecutionIR::Error("input frame dimension mismatch");
    using IR = ExecutionIR;
    using K = IR::Kind;
    using S = SupplyPlan::Kind;
    if (supply.productsPerPeriod == 0 || supply.usage.size() != p.inputs.size())
        throw IR::Error("invalid supply period or accounting dimension");
    IR ir = p.productionInfo ? IR::ForStaticProduction(p) : IR{};
    auto state = p.productionInfo ? IR::InitialForStaticProduction(p) : IR::Initial(p);
    auto append = [&](K kind, std::vector<IR::AtomId> atoms = {}, std::size_t index = 0,
                      std::uint8_t bits = 0) {
        ir.Append(p, state, {kind, index, std::move(atoms), bits});
    };
    std::vector<std::vector<IR::AtomId>> stock(p.inputs.size());
    std::map<int, std::tuple<std::size_t, std::size_t, IR::AtomId>> buffers;
    std::vector<SupplyPlan::Usage> usage(p.inputs.size());
    std::vector<std::vector<IR::AtomId>> targets;
    for (const auto& output : p.outputs)
        targets.emplace_back(output.atoms.size());
    std::size_t port = 0;
    std::optional<std::size_t> chemicalPort;
    std::optional<IR::AtomId> catalyst, pending, purification, animismus;
    std::vector<IR::AtomId> coproducts, unification;
    std::optional<std::tuple<std::size_t, std::size_t, std::size_t, std::size_t>> catalystSource;
    std::size_t pendingTarget = 0;
    bool purificationRetainsProduct = false;
    std::set<IR::Edge> preservedEdges;
    std::map<IR::AtomId, int> fragmentRotations;
    std::set<IR::AtomId> positionedFragments;
    std::vector<std::size_t> productInstance(supply.steps.size());
    std::vector<std::size_t> instances(p.outputs.size());
    std::vector<std::set<std::size_t>> requested(p.outputs.size());
    std::size_t unifyOperands = 0, animismusOperands = 0, purifyOperands = 0;
    for (std::size_t i = 0; i < supply.steps.size(); ++i) {
        const auto& joint = supply.steps[i];
        const auto& step = joint.supply;
        if (joint.output >= p.outputs.size()) continue;
        productInstance[i] = instances[joint.output];
        bool beginsTarget =
            step.kind == S::Take || step.kind == S::Disperse ||
            (step.kind == S::Coproduct && step.conversion != ElementConversion::Duplication);
        if (step.kind == S::Unify) beginsTarget = unifyOperands++ % 4 == 0;
        if (step.kind == S::Animismus) beginsTarget = animismusOperands++ % 2 == 0;
        if (step.kind == S::Purify) beginsTarget = purifyOperands++ % 2 == 0 && !step.retainProduct;
        if (beginsTarget && requested[joint.output].insert(step.targetAtom).second &&
            requested[joint.output].size() == p.outputs[joint.output].atoms.size()) {
            ++instances[joint.output];
            requested[joint.output].clear();
        }
    }
    std::size_t stepIndex = 0;
    auto extract = [&](const SupplyPlan::Step& step) {
        if (step.input == p.inputs.size()) {
            if (step.buffer >= 0 && step.kind != S::Store) {
                const auto it = buffers.find(step.buffer);
                if (it == buffers.end() || std::get<0>(it->second) != step.input ||
                    std::get<1>(it->second) != step.sourceAtom)
                    throw IR::Error("coproduct buffer source mismatch");
                const auto id = std::get<2>(it->second);
                append(K::Retrieve, {}, step.buffer);
                buffers.erase(it);
                return id;
            }
            const auto reserved = std::find_if(coproducts.begin(), coproducts.end(), [&](auto id) {
                return static_cast<std::size_t>(state.atoms.at(id).type) == step.sourceAtom;
            });
            if (reserved == coproducts.end()) throw IR::Error("missing generated coproduct source");
            const auto id = *reserved;
            coproducts.erase(reserved);
            return id;
        }
        if (step.input >= stock.size() || step.sourceAtom >= p.inputs[step.input].atoms.size())
            throw IR::Error("invalid supply source");
        IR::AtomId id;
        if (step.buffer >= 0 && step.kind != S::Store) {
            auto it = buffers.find(step.buffer);
            if (it == buffers.end() || std::get<0>(it->second) != step.input ||
                std::get<1>(it->second) != step.sourceAtom)
                throw IR::Error("buffer source mismatch");
            id = std::get<2>(it->second);
            append(K::Retrieve, {}, step.buffer);
            buffers.erase(it);
        } else {
            if (step.buffer < -1 && step.kind != S::Store) throw IR::Error("invalid source buffer");
            auto& batch = stock[step.input];
            if (batch.empty() || !batch[step.sourceAtom]) {
                if (std::any_of(batch.begin(), batch.end(), [](auto atom) { return atom != 0; }))
                    throw IR::Error("new batch would overwrite remaining stock");
                auto first = state.nextId;
                const auto frame = inputFrames.empty() ? core::HexFrame{} : inputFrames[step.input];
                ir.Append(p, state, {K::Acquire, step.input, {}, 0, frame.Turns(), frame.origin});
                ++usage[step.input].batches;
                batch.resize(p.inputs[step.input].atoms.size());
                for (std::size_t i = 0; i < batch.size(); ++i)
                    batch[i] = first + i;
                // Projection changes the metal endpoint but retains its inherited edges.
                std::map<std::size_t, std::tuple<std::size_t, std::size_t, std::size_t>>
                    destinations;
                std::set<std::size_t> projectedSources;
                std::set<std::size_t> batchReferences;
                for (std::size_t future = stepIndex; future < supply.steps.size(); ++future) {
                    const auto& delivery = supply.steps[future];
                    const auto& candidate = delivery.supply;
                    if (candidate.input != step.input) continue;
                    if (candidate.buffer != -1) continue;
                    // A second unbuffered use starts a later batch. Operations on
                    // other atoms in this batch do not invalidate direct fragments.
                    if (!batchReferences.insert(candidate.sourceAtom).second) break;
                    if (candidate.kind != S::Take ||
                        (candidate.conversion != ElementConversion::None &&
                         candidate.conversion != ElementConversion::Projection))
                        continue;
                    if (candidate.conversion == ElementConversion::Projection)
                        projectedSources.insert(candidate.sourceAtom);
                    const auto destination =
                        std::tuple{delivery.output, productInstance[future], candidate.targetAtom};
                    if (std::any_of(destinations.begin(), destinations.end(),
                                    [&](const auto& entry) { return entry.second == destination; }))
                        break;
                    destinations.emplace(candidate.sourceAtom, destination);
                }
                for (int rotation = 0; !cutSources && rotation < 6; ++rotation) {
                    std::set<IR::Edge> compatible;
                    for (const auto& [edge, bits] : state.bonds) {
                        if (edge.a < first || edge.b < first || edge.a >= first + batch.size() ||
                            edge.b >= first + batch.size())
                            continue;
                        if ((fragmentRotations.contains(edge.a) &&
                             fragmentRotations.at(edge.a) != rotation) ||
                            (fragmentRotations.contains(edge.b) &&
                             fragmentRotations.at(edge.b) != rotation))
                            continue;
                        const auto a = destinations.find(edge.a - first),
                                   b = destinations.find(edge.b - first);
                        if (a == destinations.end() || b == destinations.end()) continue;
                        const auto [output, instance, targetA] = a->second;
                        const auto [otherOutput, otherInstance, targetB] = b->second;
                        if (output != otherOutput || instance != otherInstance ||
                            output >= p.outputs.size())
                            continue;
                        const auto& molecule = p.outputs[output];
                        if (targetA >= molecule.atoms.size() || targetB >= molecule.atoms.size())
                            continue;
                        const auto& x = molecule.atoms[targetA];
                        const auto& y = molecule.atoms[targetB];
                        const auto compatibleType = [&](IR::AtomId id, core::AtomType target) {
                            const auto source = state.atoms.at(id).type;
                            return source == target ||
                                   (projectedSources.contains(id - first) &&
                                    source >= core::AtomType::Silver &&
                                    source <= core::AtomType::Lead &&
                                    target >= core::AtomType::Gold && target < source &&
                                    (p.permissionBits & (1ULL << 14)));
                        };
                        if (!compatibleType(edge.a, x.type) || !compatibleType(edge.b, y.type))
                            continue;
                        const auto frame =
                            outputFrames.empty() ? core::HexFrame{} : outputFrames[output];
                        const core::HexFrame turn{{0, 0}, rotation};
                        if (core::Subtract(turn.ToWorld(state.atoms.at(edge.b).position),
                                           turn.ToWorld(state.atoms.at(edge.a).position)) !=
                            core::Subtract(frame.ToWorld(y.position), frame.ToWorld(x.position)))
                            continue;
                        const auto bond = std::find_if(
                            molecule.bonds.begin(), molecule.bonds.end(), [&](const auto& bond) {
                                return ((bond.from == x.position && bond.to == y.position) ||
                                        (bond.to == x.position && bond.from == y.position)) &&
                                       (bond.bits & bits) == bits;
                            });
                        if (bond != molecule.bonds.end()) compatible.insert(edge);
                    }
                    if (compatible.empty()) continue;
                    preservedEdges.insert(compatible.begin(), compatible.end());
                    for (const auto& edge : compatible) {
                        fragmentRotations[edge.a] = rotation;
                        fragmentRotations[edge.b] = rotation;
                    }
                }
            }
            id = batch[step.sourceAtom];
            std::vector<std::pair<IR::Edge, std::uint8_t>> edges;
            for (const auto& edge : state.bonds)
                if (!preservedEdges.contains(edge.first) &&
                    ((fragmentRotations.contains(id) &&
                      (std::find(batch.begin(), batch.end(), edge.first.a) != batch.end() ||
                       std::find(batch.begin(), batch.end(), edge.first.b) != batch.end())) ||
                     edge.first.a == id || edge.first.b == id))
                    edges.push_back(edge);
            for (const auto& [edge, bits] : edges)
                append(K::Unbond, {edge.a, edge.b}, 0, bits);
            batch[step.sourceAtom] = 0;
        }
        if (step.kind == S::Discard)
            ++usage[step.input].discarded;
        else if (step.kind != S::Store)
            ++usage[step.input].acquired;
        return id;
    };
    auto deliver = [&](IR::AtomId id, std::size_t atom) {
        auto& target = targets.at(port);
        const auto outputFrame = outputFrames.empty() ? core::HexFrame{} : outputFrames[port];
        if (atom >= target.size() || target[atom] ||
            state.atoms.at(id).type != p.outputs[port].atoms[atom].type)
            throw IR::Error("supply target mismatch");
        const int rotation = fragmentRotations.contains(id) && !positionedFragments.contains(id)
                                 ? fragmentRotations.at(id)
                                 : 0;
        const core::HexFrame turn{{0, 0}, rotation};
        const auto translation =
            core::Subtract(outputFrame.ToWorld(p.outputs[port].atoms[atom].position),
                           turn.ToWorld(state.atoms.at(id).position));
        const auto fragment = IR::Fragment(state, id);
        ir.Append(p, state, {K::Relocate, 0, {id}, 0, rotation, translation});
        positionedFragments.insert(fragment.begin(), fragment.end());
        target[atom] = id;
        if (std::any_of(target.begin(), target.end(), [](auto value) { return value == 0; }))
            return;
        auto indexAt = [&](core::HexCoord position) {
            const auto& atoms = p.outputs[port].atoms;
            auto it = std::find_if(atoms.begin(), atoms.end(),
                                   [&](const auto& a) { return a.position == position; });
            if (it == atoms.end()) throw IR::Error("missing output bond endpoint");
            return static_cast<std::size_t>(it - atoms.begin());
        };
        for (const auto& bond : p.outputs[port].bonds) {
            const auto a = target[indexAt(bond.from)], b = target[indexAt(bond.to)];
            const IR::Edge edge{std::min(a, b), std::max(a, b)};
            const auto existing = state.bonds.find(edge);
            const auto missing =
                bond.bits & ~(existing == state.bonds.end() ? 0 : existing->second);
            if (missing) append(K::Bond, {a, b}, 0, missing);
        }
        append(K::Output, {target.front()}, port);
        std::fill(target.begin(), target.end(), 0);
    };
    for (stepIndex = 0; stepIndex < supply.steps.size(); ++stepIndex) {
        const auto& joint = supply.steps[stepIndex];
        const auto& step = joint.supply;
        if (step.retainProduct && step.kind != S::Purify)
            throw IR::Error("retained product on non-purification operation");
        if ((step.wheelCatalyst &&
             (step.kind != S::Take || step.conversion != ElementConversion::Duplication)) ||
            (step.calcifyBeforeDuplication && !step.wheelCatalyst))
            throw IR::Error("invalid wheel supply conversion binding");
        const bool inventoryOnly = step.kind == S::Store || step.kind == S::Discard;
        if (!inventoryOnly) {
            port = joint.output;
            if (port >= targets.size() || step.targetAtom >= targets[port].size())
                throw IR::Error("invalid supply target");
            if ((pending || purification || animismus || !unification.empty()) &&
                chemicalPort != port)
                throw IR::Error("interrupted cross-output chemistry");
            chemicalPort = port;
        }
        if ((pending && !inventoryOnly &&
             (step.kind != S::Reagent || step.conversion != ElementConversion::None)) ||
            (purification && !inventoryOnly && step.kind != S::Purify) ||
            (animismus && !inventoryOnly && step.kind != S::Animismus) ||
            (!unification.empty() && !inventoryOnly && step.kind != S::Unify))
            throw IR::Error("interrupted chemical delivery");
        if (inventoryOnly && step.conversion != ElementConversion::None)
            throw IR::Error("conversion on inventory operation");
        if (step.kind == S::Take && step.buffer == -2) {
            if (!catalyst || !catalystSource || step.conversion != ElementConversion::None ||
                *catalystSource != std::tuple{step.input, step.sourceAtom, step.targetAtom, port})
                throw IR::Error("missing or mismatched borrowed catalyst");
            auto id = *catalyst;
            catalyst.reset();
            catalystSource.reset();
            deliver(id, step.targetAtom);
            continue;
        }
        if (step.kind == S::Coproduct) {
            const auto reserved = std::find_if(coproducts.begin(), coproducts.end(), [&](auto id) {
                return state.atoms.at(id).type == p.outputs[port].atoms[step.targetAtom].type;
            });
            const bool borrow = step.conversion == ElementConversion::Duplication;
            if (reserved == coproducts.end() ||
                (step.conversion != ElementConversion::None && !borrow) || step.buffer != -1 ||
                (borrow &&
                 (catalyst || step.input != p.inputs.size() ||
                  step.sourceAtom != static_cast<std::size_t>(state.atoms.at(*reserved).type))))
                throw IR::Error("missing or invalid coproduct reservation");
            const auto id = *reserved;
            coproducts.erase(reserved);
            if (borrow) {
                catalyst = id;
                catalystSource = std::tuple{step.input, step.sourceAtom, step.targetAtom, port};
            } else
                deliver(id, step.targetAtom);
            continue;
        }
        auto id = extract(step);
        if (step.kind == S::Store) {
            if (step.buffer < 0 || buffers.contains(step.buffer))
                throw IR::Error("invalid occupied buffer");
            append(K::Store, {id}, step.buffer);
            buffers.emplace(step.buffer, std::tuple{step.input, step.sourceAtom, id});
        } else if (step.kind == S::Discard)
            append(K::Discard, {id});
        else if (step.kind == S::Reagent) {
            if (step.conversion == ElementConversion::Duplication) {
                if (catalyst) throw IR::Error("duplicate catalyst reservation");
                catalyst = id;
                catalystSource = std::tuple{step.input, step.sourceAtom, step.targetAtom, port};
            } else {
                if (!pending || step.targetAtom != pendingTarget ||
                    step.conversion != ElementConversion::None)
                    throw IR::Error("unexpected projection reagent");
                const auto product = state.nextId;
                const auto metal = *pending;
                append(K::Project, {metal, id});
                if (fragmentRotations.contains(metal)) {
                    fragmentRotations[product] = fragmentRotations.at(metal);
                    fragmentRotations.erase(metal);
                }
                if (positionedFragments.erase(metal)) positionedFragments.insert(product);
                std::set<IR::Edge> remapped;
                for (auto edge : preservedEdges) {
                    if (edge.a == metal) edge.a = product;
                    if (edge.b == metal) edge.b = product;
                    remapped.insert({std::min(edge.a, edge.b), std::max(edge.a, edge.b)});
                }
                preservedEdges = std::move(remapped);
                pending = product;
                if (state.atoms.at(product).type == p.outputs[port].atoms.at(pendingTarget).type) {
                    pending.reset();
                    deliver(product, pendingTarget);
                }
            }
        } else if (step.kind == S::Unify) {
            if (step.conversion != ElementConversion::None)
                throw IR::Error("invalid unification conversion");
            if (unification.empty()) pendingTarget = step.targetAtom;
            if (pendingTarget != step.targetAtom) throw IR::Error("unification target mismatch");
            unification.push_back(id);
            if (unification.size() == 4) {
                const auto product = state.nextId;
                append(K::Unify, unification);
                unification.clear();
                deliver(product, step.targetAtom);
            }
        } else if (step.kind == S::Disperse) {
            const bool calcify = step.conversion == ElementConversion::Calcification;
            if (step.conversion != ElementConversion::None && !calcify)
                throw IR::Error("invalid dispersion conversion");
            const auto wanted = p.outputs[port].atoms[step.targetAtom].type;
            if (calcify && wanted != core::AtomType::Salt)
                throw IR::Error("invalid calcified dispersion target");
            const auto selectedType = calcify ? step.dispersedElement : wanted;
            if (calcify &&
                (selectedType < core::AtomType::Air || selectedType > core::AtomType::Water))
                throw IR::Error("invalid calcified dispersion element");
            const auto first = state.nextId;
            append(K::Disperse, {id});
            std::optional<IR::AtomId> selected;
            for (auto product = first; product < state.nextId; ++product) {
                if (state.atoms.at(product).type == selectedType)
                    selected = product;
                else
                    coproducts.push_back(product);
            }
            if (!selected) throw IR::Error("dispersion target mismatch");
            if (calcify) {
                const auto product = state.nextId;
                append(K::Calcify, {*selected});
                selected = product;
            }
            deliver(*selected, step.targetAtom);
        } else if (step.kind == S::Animismus) {
            if (step.conversion == ElementConversion::Calcification) {
                const auto* enabled = std::getenv("OPUSOPTIMA_CALCIFY_ANIMISMUS");
                if (!closedLife && (!enabled || std::string(enabled) != "1"))
                    throw IR::Error("calcified animismus experiment disabled");
                const auto product = state.nextId;
                append(K::Calcify, {id});
                id = product;
            } else if (step.conversion != ElementConversion::None)
                throw IR::Error("invalid animismus reservation");
            if (!animismus) {
                animismus = id;
                pendingTarget = step.targetAtom;
            } else {
                if (step.targetAtom != pendingTarget) throw IR::Error("animismus target mismatch");
                const auto first = state.nextId;
                append(K::Animismus, {*animismus, id});
                animismus.reset();
                const bool vitae =
                    p.outputs[port].atoms.at(step.targetAtom).type == core::AtomType::Vitae;
                coproducts.push_back(first + (vitae ? 1 : 0));
                deliver(first + (vitae ? 0 : 1), step.targetAtom);
            }
        } else if (step.kind == S::Purify) {
            if (step.conversion != ElementConversion::None)
                throw IR::Error("invalid purification conversion");
            if (!purification) {
                purification = id;
                pendingTarget = step.targetAtom;
                purificationRetainsProduct = step.retainProduct;
            } else {
                if (step.targetAtom != pendingTarget ||
                    step.retainProduct != purificationRetainsProduct)
                    throw IR::Error("purification target or retention mismatch");
                const auto product = state.nextId;
                append(K::Purify, {*purification, id});
                purification.reset();
                if (step.retainProduct)
                    coproducts.push_back(product);
                else
                    deliver(product, step.targetAtom);
            }
        } else if (step.kind == S::Take) {
            if (pending || purification) throw IR::Error("unfinished chemical delivery");
            if (step.conversion == ElementConversion::Projection) {
                pending = id;
                pendingTarget = step.targetAtom;
                continue;
            }
            if (step.conversion != ElementConversion::None) {
                if (step.calcifyBeforeDuplication) {
                    const auto salt = state.nextId;
                    append(K::Calcify, {id});
                    id = salt;
                }
                const auto product = state.nextId;
                if (step.conversion == ElementConversion::Calcification)
                    append(K::Calcify, {id});
                else if (step.conversion == ElementConversion::Duplication && step.wheelCatalyst) {
                    const auto& binding = *step.wheelCatalyst;
                    if (binding.element != p.outputs[port].atoms.at(step.targetAtom).type)
                        throw IR::Error("wheel catalyst does not match supply target");
                    IR::Operation duplicate{K::Duplicate, 0, {id}};
                    duplicate.wheelCatalyst =
                        IR::WheelCatalyst{binding.device, binding.element, binding.orientation};
                    ir.Append(p, state, duplicate);
                } else if (step.conversion == ElementConversion::Duplication && catalyst)
                    append(K::Duplicate, {id, *catalyst});
                else
                    throw IR::Error("missing duplication catalyst");
                id = product;
            }
            deliver(id, step.targetAtom);
        } else
            throw IR::Error("unknown supply step");
    }
    if (pending || purification || animismus || !unification.empty() || !coproducts.empty() ||
        catalyst || !buffers.empty())
        throw IR::Error("unfinished supply inventory");
    if (usage != supply.usage) throw IR::Error("supply accounting mismatch");
    IR::RequireComplete(state,
                        std::vector<std::size_t>(p.outputs.size(), supply.productsPerPeriod));
    if (state != ir.Replay(p)) throw IR::Error("supply replay mismatch");
    return ir;
}
} // namespace

ExecutionIR BuildSupplyExecutionIR(const io::PuzzleData& p, const AssemblyPlan& assembly,
                                   const SupplyPlan& supply, core::HexFrame outputFrame,
                                   const std::vector<core::HexFrame>& inputFrames) {
    return BuildSupplyExecutionIRWithPolicy(p, assembly, supply,
                                            SupplyMaterialLoweringPolicy::PreserveCompatible,
                                            outputFrame, inputFrames);
}

ExecutionIR BuildSupplyExecutionIRWithPolicy(const io::PuzzleData& p, const AssemblyPlan& assembly,
                                             const SupplyPlan& supply,
                                             SupplyMaterialLoweringPolicy policy,
                                             core::HexFrame outputFrame,
                                             const std::vector<core::HexFrame>& inputFrames) {
    AssemblyPlanner::Validate(p, assembly);
    JointSupplyPlan joint;
    joint.usage = supply.usage;
    joint.productsPerPeriod = supply.productsPerPeriod;
    joint.recipeConstrained = supply.recipeConstrained;
    for (const auto& step : supply.steps)
        joint.steps.push_back({step, 0});
    return BuildPeriod(p, joint, {outputFrame}, inputFrames, policy);
}

ExecutionIR BuildJointSupplyExecutionIR(const io::PuzzleData& p, const JointSupplyPlan& supply,
                                        const std::vector<core::HexFrame>& outputFrames,
                                        const std::vector<core::HexFrame>& inputFrames) {
    return BuildJointSupplyExecutionIRWithPolicy(
        p, supply, SupplyMaterialLoweringPolicy::PreserveCompatible, outputFrames, inputFrames);
}

ExecutionIR BuildJointSupplyExecutionIRWithPolicy(const io::PuzzleData& p,
                                                  const JointSupplyPlan& supply,
                                                  SupplyMaterialLoweringPolicy policy,
                                                  const std::vector<core::HexFrame>& outputFrames,
                                                  const std::vector<core::HexFrame>& inputFrames) {
    return BuildPeriod(p, supply, outputFrames, inputFrames, policy);
}
} // namespace opusoptima::solvers
