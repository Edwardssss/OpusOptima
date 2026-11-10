#include "InventorySearch.hpp"
#include "FeedLayout.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <functional>
#include <limits>
#include <map>
#include <numeric>
#include <stdexcept>
#include <string>
#include <unordered_map>

namespace opusoptima::solvers {
std::optional<std::size_t> ClosedMaterialPeriodDivisor(const io::PuzzleData& p) {
    if (p.productionInfo || (p.permissionBits & (1ULL << 17)) || p.inputs.empty() ||
        p.outputs.empty())
        return std::nullopt;
    const auto mass = [](const io::Molecule& molecule) -> std::optional<std::size_t> {
        if (molecule.atoms.empty()) return std::nullopt;
        std::size_t total = 0;
        for (const auto& atom : molecule.atoms) {
            using T = core::AtomType;
            std::size_t weight;
            if (atom.type == T::Quintessence)
                weight = 4;
            else if ((atom.type >= T::Salt && atom.type <= T::Water) || atom.type == T::Vitae ||
                     atom.type == T::Mors)
                weight = 1;
            else
                return std::nullopt;
            if (total > std::numeric_limits<std::size_t>::max() - weight) return std::nullopt;
            total += weight;
        }
        return total;
    };
    std::size_t batches = 0, demand = 0;
    for (const auto& input : p.inputs) {
        const auto value = mass(input);
        if (!value) return std::nullopt;
        batches = std::gcd(batches, *value);
    }
    for (const auto& output : p.outputs) {
        const auto value = mass(output);
        if (!value || demand > std::numeric_limits<std::size_t>::max() - *value)
            return std::nullopt;
        demand += *value;
    }
    // Calcification and Animismus preserve weight; duplication changes Salt to
    // a cardinal while retaining its catalyst. Wheel atoms remain device-owned.
    // Unification and dispersion exchange four cardinals for one Quintessence.
    // These species cannot reach projection or purification reactants.
    return batches / std::gcd(batches, demand);
}
namespace {
struct OriginalDestination {
    std::size_t output{}, atom{};
};
InventorySearchResult SearchSupplyInventoryCore(
    const io::PuzzleData& p, std::size_t products, InventorySearchLimits limits,
    const ChemicalRecipe* recipe, const std::function<bool(const SupplyPlan&)>& accept,
    const io::PuzzleData& original, const std::vector<OriginalDestination>& destinations,
    bool prioritizeDirect = false, bool closedCardinalLife = false) {
    InventorySearchResult result;
    if (recipe && recipe->inputBatches.size() != p.inputs.size()) return result;
    if (p.outputs.size() != 1 || p.outputs[0].atoms.empty() || p.inputs.empty() ||
        !p.outputMultiplier || !products || products > 12 || (6ULL * p.outputMultiplier) % products)
        return result;
    const auto periodDivisor = ClosedMaterialPeriodDivisor(p);
    if (periodDivisor && products % *periodDivisor) return result;
    if (p.outputs[0].atoms.size() > limits.maxSteps / products) {
        result.status = InventorySearchStatus::Exhausted;
        return result;
    }
    struct Source {
        std::size_t input, atom;
        core::HexCoord cell;
    };
    std::vector<Source> sources;
    std::vector<std::vector<std::size_t>> feeds(p.inputs.size());
    std::vector<bool> wide(p.inputs.size());
    try {
        for (std::size_t i = 0; i < p.inputs.size(); ++i) {
            if (std::find(limits.excludedInputs.begin(), limits.excludedInputs.end(), i) !=
                limits.excludedInputs.end()) {
                if (recipe && recipe->inputBatches[i]) return result;
                continue;
            }
            if (p.inputs[i].atoms.empty()) return result;
            if (p.inputs[i].atoms.size() > limits.maxSourceAtoms - sources.size()) {
                result.status = InventorySearchStatus::Exhausted;
                return result;
            }
            std::vector<core::HexCoord> cells;
            if (limits.mechanicalExtractionOrder)
                cells = PlanExtractionLayout(p.inputs[i]).cells;
            else
                for (const auto& atom : p.inputs[i].atoms)
                    cells.push_back(atom.position);
            int low = 256, high = -256;
            for (auto c : cells) {
                low = std::min(low, c.r);
                high = std::max(high, c.r);
            }
            wide[i] = high - low > 2;
            for (std::size_t j = 0; j < p.inputs[i].atoms.size(); ++j) {
                feeds[i].push_back(sources.size());
                sources.push_back({i, j, cells[j]});
            }
        }
    } catch (const std::invalid_argument&) {
        return result;
    }
    if (std::any_of(wide.begin(), wide.end(), [](bool value) { return value; }))
        std::fill(wide.begin(), wide.end(), true);
    if (sources.size() > limits.maxSourceAtoms) {
        result.status = InventorySearchStatus::Exhausted;
        return result;
    }
    // No supported reaction creates quicksilver; parked inventory also originates in feeds.
    const bool hasQuicksilver =
        std::any_of(sources.begin(), sources.end(), [&](const auto& source) {
            return (!recipe || recipe->inputBatches[source.input]) &&
                   p.inputs[source.input].atoms[source.atom].type == core::AtomType::Quicksilver;
        });
    const auto& target = p.outputs[0].atoms;
    // Metal reactions only promote metals. Ignore quantities and extraction order here:
    // this is a necessary reachability check, not a proof of a feasible recipe.
    std::array<bool, 13> reachable{};
    for (const auto& source : sources) {
        const auto type = p.inputs[source.input].atoms[source.atom].type;
        if ((!recipe || recipe->inputBatches[source.input]) && type >= core::AtomType::Gold &&
            type <= core::AtomType::Lead)
            reachable[static_cast<std::size_t>(type)] = true;
    }
    for (int metal = static_cast<int>(core::AtomType::Lead);
         metal > static_cast<int>(core::AtomType::Gold); --metal) {
        const bool projection =
            hasQuicksilver && (p.permissionBits & (1ULL << 14)) && (!recipe || recipe->projections);
        const bool purification =
            (limits.singleStagePurification || limits.multiStagePurification) &&
            (p.permissionBits & (1ULL << 15)) && (!recipe || recipe->purifications);
        if (reachable[metal] && (projection || purification)) reachable[metal - 1] = true;
    }
    for (const auto& atom : target)
        if (atom.type >= core::AtomType::Gold && atom.type <= core::AtomType::Lead &&
            !reachable[static_cast<std::size_t>(atom.type)])
            return result;

    std::vector<std::size_t> order(target.size());
    std::iota(order.begin(), order.end(), 0);
    std::sort(order.begin(), order.end(), [&](auto a, auto b) {
        const auto x = target[a].position, y = target[b].position;
        return x.r != y.r ? x.r > y.r : x.q > y.q;
    });
    const auto molecule = order;
    for (std::size_t i = 1; i < products; ++i)
        order.insert(order.end(), molecule.begin(), molecule.end());
    if (order.size() > limits.maxSteps) {
        result.status = InventorySearchStatus::Exhausted;
        return result;
    }
    struct MandatoryEdge {
        std::size_t a{}, b{};
        std::uint8_t bits{};
    };
    std::vector<MandatoryEdge> mandatory;
    if (destinations.size() == target.size()) {
        for (std::size_t output = 0; output < original.outputs.size(); ++output) {
            const auto& molecule = original.outputs[output];
            for (const auto& bond : molecule.bonds) {
                const auto at = [&](core::HexCoord position) {
                    return std::find_if(
                               molecule.atoms.begin(), molecule.atoms.end(),
                               [&](const auto& atom) { return atom.position == position; }) -
                           molecule.atoms.begin();
                };
                const auto a = at(bond.from), b = at(bond.to);
                if (a >= molecule.atoms.size() || b >= molecule.atoms.size()) continue;
                const bool canRebuild = bond.bits == 1
                                            ? (p.permissionBits & (1ULL << 8))
                                            : (p.permissionBits & (1ULL << 11)) &&
                                                  molecule.atoms[a].type == core::AtomType::Fire &&
                                                  molecule.atoms[b].type == core::AtomType::Fire;
                if (canRebuild) continue;
                const auto flat = [&](std::size_t atom) {
                    return std::find_if(destinations.begin(), destinations.end(),
                                        [&](const auto& destination) {
                                            return destination.output == output &&
                                                   destination.atom == atom;
                                        }) -
                           destinations.begin();
                };
                const auto x = static_cast<std::size_t>(flat(a)),
                           y = static_cast<std::size_t>(flat(b));
                if (x < destinations.size() && y < destinations.size())
                    mandatory.push_back({x, y, bond.bits});
            }
        }
    }
    std::vector<std::size_t> orderOf(target.size());
    for (std::size_t i = 0; i < target.size(); ++i)
        orderOf[order[i]] = i;
    struct DirectMapping {
        std::size_t source{}, batch{};
    };
    struct State {
        std::vector<unsigned char> available;
        std::array<int, 8> buffers;
        std::size_t next{};
        int mercury{};
        int catalyst{-1};
        std::size_t catalystTarget{};
        std::size_t duplications{};
        std::array<std::size_t, 4> duplicatedElements{};
        int purification{}, animismus{};
        unsigned int unification{};
        std::vector<core::AtomType> coproducts;
        std::map<std::size_t, DirectMapping> direct;
        std::size_t calcifications{}, projections{}, animismusReactions{}, purificationReactions{};
        std::size_t unifications{}, dispersions{};
        SupplyPlan plan;
    };
    State initial;
    initial.available.resize(sources.size());
    initial.buffers.fill(-1);
    initial.plan.productsPerPeriod = products;
    initial.plan.recipeConstrained = recipe != nullptr;
    initial.plan.usage.resize(p.inputs.size());
    const bool disposal = p.permissionBits & (1ULL << 17);
    // Closed metal-only plans conserve lead-equivalent mass under purification.
    // Mercury, other chemistry and disposal disable this necessary-condition bound.
    bool closedMetalMass = !disposal;
    std::vector<std::size_t> batchMetalMass(p.inputs.size());
    std::size_t targetMetalMass = 0;
    const auto addMetalMass = [&](std::size_t& total, core::AtomType type) {
        if (type < core::AtomType::Gold || type > core::AtomType::Lead) {
            closedMetalMass = false;
            return;
        }
        const auto weight = std::size_t{1}
                            << (static_cast<int>(core::AtomType::Lead) - static_cast<int>(type));
        if (total > std::numeric_limits<std::size_t>::max() - weight) {
            closedMetalMass = false;
            return;
        }
        total += weight;
    };
    for (std::size_t i = 0; i < p.inputs.size(); ++i)
        for (const auto& atom : p.inputs[i].atoms)
            addMetalMass(batchMetalMass[i], atom.type);
    for (const auto& atom : target)
        addMetalMass(targetMetalMass, atom.type);
    if (closedMetalMass) {
        if (targetMetalMass > std::numeric_limits<std::size_t>::max() / products)
            closedMetalMass = false;
        else
            targetMetalMass *= products;
    }
    std::unordered_map<std::string, std::size_t> seen;
    std::size_t expanded = 0;
    bool exhausted = false;
    auto type = [&](std::size_t id) {
        if (id >= sources.size()) return static_cast<core::AtomType>(id - sources.size());
        const auto s = sources[id];
        return p.inputs[s.input].atoms[s.atom].type;
    };
    auto key = [recipe, &mandatory](const State& s) {
        std::string k;
        auto append = [&](std::size_t value) {
            for (std::size_t i = 0; i < sizeof(value); ++i)
                k.push_back(static_cast<char>(value >> (i * 8)));
        };
        append(s.next);
        append(s.mercury);
        append(s.purification);
        append(s.animismus);
        append(s.unification);
        append(static_cast<std::size_t>(s.catalyst + 1));
        append(s.catalystTarget);
        if (recipe) {
            append(s.duplications);
            for (auto count : s.duplicatedElements)
                append(count);
        }
        auto coproducts = s.coproducts;
        std::sort(coproducts.begin(), coproducts.end());
        append(coproducts.size());
        for (auto product : coproducts)
            append(static_cast<std::size_t>(product));
        if (recipe) {
            append(s.calcifications);
            append(s.projections);
            append(s.animismusReactions);
            append(s.purificationReactions);
            append(s.unifications);
            append(s.dispersions);
            for (const auto& usage : s.plan.usage)
                append(usage.batches);
        }
        for (auto a : s.available)
            k.push_back(static_cast<char>(a));
        auto buffers = s.buffers;
        std::sort(buffers.begin(), buffers.end());
        for (auto b : buffers)
            append(static_cast<std::size_t>(b + 1));
        if (!mandatory.empty()) {
            append(s.direct.size());
            for (const auto& [ordinal, mapping] : s.direct) {
                append(ordinal);
                append(mapping.source);
                append(mapping.batch);
            }
        }
        return k;
    };
    std::vector<std::size_t> component(target.size());
    std::iota(component.begin(), component.end(), 0);
    for (const auto& edge : mandatory) {
        const auto a = component[edge.a], b = component[edge.b];
        for (auto& id : component)
            if (id == b) id = a;
    }
    const auto sourceMask = [&](std::size_t a, std::size_t b) -> std::uint8_t {
        if (sources[a].input != sources[b].input) return 0;
        const auto& input = p.inputs[sources[a].input];
        const auto x = input.atoms[sources[a].atom].position;
        const auto y = input.atoms[sources[b].atom].position;
        const auto found =
            std::find_if(input.bonds.begin(), input.bonds.end(), [&](const auto& bond) {
                return (bond.from == x && bond.to == y) || (bond.from == y && bond.to == x);
            });
        return found == input.bonds.end() ? 0 : found->bits;
    };
    const auto retainedEndpointCanSupply = [&](core::AtomType source, core::AtomType target) {
        return source == target ||
               (source >= core::AtomType::Silver && source <= core::AtomType::Lead &&
                target >= core::AtomType::Gold && target < source &&
                (p.permissionBits & (1ULL << 14)));
    };
    const auto fitsInherited = [&](const State& state, std::size_t ordinal, std::size_t id,
                                   int buffer, bool startsBatch) {
        if (mandatory.empty()) return true;
        const auto flat = order[ordinal];
        const auto occurrence = ordinal / target.size();
        const auto batch = id < sources.size() ? state.plan.usage[sources[id].input].batches +
                                                     static_cast<unsigned>(startsBatch)
                                               : 0;
        const auto& destination = destinations[flat];
        const auto& molecule = original.outputs[destination.output];
        const auto current = molecule.atoms[destination.atom].position;
        bool needed = false;
        for (const auto& edge : mandatory) {
            if (edge.a != flat && edge.b != flat) continue;
            needed = true;
            if (id >= sources.size() || buffer >= 0) return false;
            const auto neighbor = edge.a == flat ? edge.b : edge.a;
            const auto neighborOrdinal = occurrence * target.size() + orderOf[neighbor];
            const auto& endpoint = destinations[neighbor];
            const auto other = original.outputs[endpoint.output].atoms[endpoint.atom].position;
            const auto prior = state.direct.find(neighborOrdinal);
            if (prior != state.direct.end()) {
                if (sources[prior->second.source].input != sources[id].input ||
                    prior->second.batch != batch ||
                    sourceMask(id, prior->second.source) != edge.bits)
                    return false;
            } else {
                if (neighborOrdinal < ordinal) return false;
                bool possible = false;
                for (const auto candidate : feeds[sources[id].input]) {
                    if (candidate == id ||
                        !retainedEndpointCanSupply(
                            type(candidate),
                            original.outputs[endpoint.output].atoms[endpoint.atom].type) ||
                        sourceMask(id, candidate) != edge.bits ||
                        (!startsBatch && !state.available[candidate]))
                        continue;
                    const auto sourceDelta = core::Subtract(
                        p.inputs[sources[id].input].atoms[sources[candidate].atom].position,
                        p.inputs[sources[id].input].atoms[sources[id].atom].position);
                    auto turned = sourceDelta;
                    for (int rotation = 0; rotation < 6; ++rotation) {
                        if (turned == core::Subtract(other, current)) possible = true;
                        turned = core::RotateClockwise(turned);
                    }
                    if (possible) break;
                }
                if (!possible) return false;
            }
        }
        if (!needed) return true;
        const auto sourcePosition = p.inputs[sources[id].input].atoms[sources[id].atom].position;
        for (int rotation = 0; rotation < 6; ++rotation) {
            bool consistent = true;
            for (const auto& [previousOrdinal, previous] : state.direct) {
                if (previousOrdinal / target.size() != occurrence ||
                    component[order[previousOrdinal]] != component[flat])
                    continue;
                if (sources[previous.source].input != sources[id].input ||
                    previous.batch != batch) {
                    consistent = false;
                    break;
                }
                const auto& previousDestination = destinations[order[previousOrdinal]];
                const auto previousTarget = original.outputs[previousDestination.output]
                                                .atoms[previousDestination.atom]
                                                .position;
                const auto previousSource =
                    p.inputs[sources[id].input].atoms[sources[previous.source].atom].position;
                auto delta = core::Subtract(previousSource, sourcePosition);
                for (int turn = 0; turn < rotation; ++turn)
                    delta = core::RotateClockwise(delta);
                if (delta != core::Subtract(previousTarget, current)) {
                    consistent = false;
                    break;
                }
            }
            if (consistent) return true;
        }
        return false;
    };
    std::function<bool(const State&)> visit;
    visit = [&](const State& state) {
        if (expanded >= limits.maxStates || std::chrono::steady_clock::now() >= limits.deadline) {
            exhausted = true;
            return false;
        }
        const bool finished = state.next == order.size() && !state.mercury && !state.purification &&
                              !state.animismus && !state.unification && state.coproducts.empty() &&
                              state.catalyst < 0;
        const bool empty =
            std::none_of(state.available.begin(), state.available.end(),
                         [](auto a) { return a != 0; }) &&
            std::all_of(state.buffers.begin(), state.buffers.end(), [](auto b) { return b < 0; });
        if (finished && empty) {
            if (recipe &&
                (state.calcifications != recipe->calcifications ||
                 state.projections != recipe->projections ||
                 state.animismusReactions != recipe->animismus ||
                 state.purificationReactions != recipe->purifications ||
                 state.duplications != recipe->duplications ||
                 state.unifications != recipe->unifications ||
                 state.dispersions != recipe->dispersions ||
                 (!recipe->duplicatedElements.empty() &&
                  (recipe->duplicatedElements.size() != state.duplicatedElements.size() ||
                   !std::equal(state.duplicatedElements.begin(), state.duplicatedElements.end(),
                               recipe->duplicatedElements.begin()))) ||
                 !std::equal(
                     state.plan.usage.begin(), state.plan.usage.end(), recipe->inputBatches.begin(),
                     [](const auto& usage, auto batches) { return usage.batches == batches; })))
                return false;
            if (accept && !accept(state.plan)) return false;
            result.plan = state.plan;
            return true;
        }
        if (state.next == order.size() && !state.coproducts.empty() && disposal && !state.mercury &&
            !state.purification && !state.animismus && !state.unification && state.catalyst < 0 &&
            state.plan.steps.size() < limits.maxSteps) {
            State next = state;
            const auto coproduct = next.coproducts.back();
            next.coproducts.pop_back();
            next.plan.steps.push_back({SupplyPlan::Kind::Discard, p.inputs.size(),
                                       static_cast<std::size_t>(coproduct), 0});
            ++expanded;
            return visit(next);
        }
        if (state.next == order.size() && !finished) return false;
        if (state.plan.steps.size() >= limits.maxSteps) {
            exhausted = true;
            return false;
        }
        const auto stateKey = key(state);
        const auto previous = seen.find(stateKey);
        if (!accept && previous != seen.end() && previous->second <= state.plan.steps.size())
            return false;
        seen[stateKey] = state.plan.steps.size();
        ++expanded;
        const auto output = finished ? target.size() : order[state.next];
        const auto wanted = state.mercury ? core::AtomType::Quicksilver
                                          : (finished ? core::AtomType::Salt : target[output].type);
        const bool needsInherited =
            !finished && std::any_of(mandatory.begin(), mandatory.end(), [&](const auto& edge) {
                return edge.a == output || edge.b == output;
            });
        if (state.catalyst >= 0 && state.next == state.catalystTarget && !state.mercury &&
            !state.purification && !state.animismus && !state.unification) {
            if (needsInherited) return false;
            State next = state;
            const bool coproduct = static_cast<std::size_t>(state.catalyst) >= sources.size();
            const auto source =
                coproduct
                    ? Source{p.inputs.size(), static_cast<std::size_t>(type(state.catalyst)), {}}
                    : sources[state.catalyst];
            next.plan.steps.push_back({SupplyPlan::Kind::Take, source.input, source.atom, output,
                                       ElementConversion::None, -2});
            next.catalyst = -1;
            next.catalystTarget = 0;
            ++next.next;
            if (visit(next)) return true;
            return false;
        }
        const auto reserved = std::find(state.coproducts.begin(), state.coproducts.end(), wanted);
        if (!needsInherited && !state.mercury && !state.purification && !state.animismus &&
            !state.unification && reserved != state.coproducts.end()) {
            State next = state;
            next.coproducts.erase(next.coproducts.begin() + (reserved - state.coproducts.begin()));
            ++next.next;
            next.plan.steps.push_back({SupplyPlan::Kind::Coproduct, 0, 0, output});
            if (visit(next)) return true;
        }
        std::size_t remainingMetalMass = targetMetalMass;
        if (closedMetalMass) {
            for (std::size_t i = 0; i < state.plan.usage.size(); ++i) {
                const auto mass = batchMetalMass[i];
                const auto batches = state.plan.usage[i].batches;
                if (!mass) continue;
                if (batches > remainingMetalMass / mass) return false;
                remainingMetalMass -= batches * mass;
            }
        }
        // Source pickups are restricted to exposed cells. Starting a new batch is
        // legal only after the previous molecule has been completely removed.
        struct Pickup {
            std::size_t id;
            int buffer;
            bool start;
        };
        std::vector<Pickup> pickups;
        for (int b = 0; b < 8; ++b)
            if (state.buffers[b] >= 0)
                pickups.push_back({static_cast<std::size_t>(state.buffers[b]), b, false});
        for (std::size_t i = 0; i < feeds.size(); ++i) {
            const bool start = std::none_of(feeds[i].begin(), feeds[i].end(),
                                            [&](auto id) { return state.available[id]; });
            if (start &&
                (finished || (recipe && state.plan.usage[i].batches >= recipe->inputBatches[i])))
                continue;
            if (start && closedMetalMass && batchMetalMass[i] > remainingMetalMass) continue;
            for (auto id : feeds[i]) {
                if (!start && !state.available[id]) continue;
                const auto cell = sources[id].cell;
                if (limits.mechanicalExtractionOrder &&
                    std::any_of(feeds[i].begin(), feeds[i].end(), [&](auto j) {
                        return (start || state.available[j]) &&
                               (wide[i] || sources[j].cell.q == cell.q) &&
                               sources[j].cell.r > cell.r;
                    }))
                    continue;
                pickups.push_back({id, -1, start});
            }
        }
        for (const auto coproduct : state.coproducts)
            pickups.push_back({sources.size() + static_cast<std::size_t>(coproduct), -1, false});
        auto inheritedBondScore = [&](const Pickup& pickup) {
            if (finished || pickup.id >= sources.size() || type(pickup.id) != wanted ||
                state.mercury || state.purification || state.animismus || state.unification)
                return 0;
            const auto source = sources[pickup.id];
            const auto& input = p.inputs[source.input];
            const auto position = input.atoms[source.atom].position;
            int score = 0;
            const auto& goal = original.outputs[destinations[output].output];
            const auto destination = goal.atoms[destinations[output].atom].position;
            for (const auto& bond : goal.bonds) {
                if (bond.from != destination && bond.to != destination) continue;
                const auto neighbor = bond.from == destination ? bond.to : bond.from;
                const auto atom =
                    std::find_if(goal.atoms.begin(), goal.atoms.end(),
                                 [&](const auto& a) { return a.position == neighbor; });
                if (atom == goal.atoms.end()) continue;
                const bool inherited =
                    std::any_of(input.bonds.begin(), input.bonds.end(), [&](const auto& edge) {
                        if ((edge.bits & bond.bits) != edge.bits ||
                            (edge.from != position && edge.to != position))
                            return false;
                        const auto other = edge.from == position ? edge.to : edge.from;
                        return std::any_of(input.atoms.begin(), input.atoms.end(),
                                           [&](const auto& a) {
                                               return a.position == other && a.type == atom->type;
                                           });
                    });
                const bool canRebuild =
                    bond.bits == 1
                        ? (p.permissionBits & (1ULL << 8))
                        : (p.permissionBits & (1ULL << 11)) && atom->type == core::AtomType::Fire &&
                              goal.atoms[destinations[output].atom].type == core::AtomType::Fire;
                if (inherited) score += canRebuild ? 1 : 4;
            }
            return score;
        };
        std::stable_sort(pickups.begin(), pickups.end(), [&](const auto& a, const auto& b) {
            if (type(a.id) != type(b.id)) return type(a.id) < type(b.id);
            return inheritedBondScore(a) > inheritedBondScore(b);
        });
        const bool directAvailable =
            prioritizeDirect && needsInherited && !state.mercury && !state.purification &&
            !state.animismus && !state.unification &&
            std::any_of(pickups.begin(), pickups.end(), [&](const auto& pickup) {
                return pickup.id < sources.size() && type(pickup.id) == wanted &&
                       fitsInherited(state, state.next, pickup.id, pickup.buffer, pickup.start);
            });
        auto transition = [&](Pickup pickup, SupplyPlan::Kind kind, ElementConversion conversion,
                              std::size_t catalystReturn = 0,
                              core::AtomType dispersedElement = core::AtomType::Air,
                              bool wheel = false, bool calcifyBeforeDuplication = false) {
            if (needsInherited && !state.mercury && kind != SupplyPlan::Kind::Store &&
                kind != SupplyPlan::Kind::Discard &&
                (kind != SupplyPlan::Kind::Take ||
                 (conversion != ElementConversion::None &&
                  conversion != ElementConversion::Projection) ||
                 pickup.id >= sources.size() ||
                 !fitsInherited(state, state.next, pickup.id, pickup.buffer, pickup.start)))
                return false;
            if (expanded >= limits.maxStates ||
                std::chrono::steady_clock::now() >= limits.deadline) {
                exhausted = true;
                return false;
            }
            State next = state;
            if (conversion == ElementConversion::Calcification || calcifyBeforeDuplication)
                ++next.calcifications;
            if (conversion == ElementConversion::Projection)
                next.projections += static_cast<int>(type(pickup.id)) - static_cast<int>(wanted);
            if (recipe && (next.calcifications > recipe->calcifications ||
                           next.projections > recipe->projections))
                return false;
            const bool generated = pickup.id >= sources.size();
            const auto source =
                generated ? Source{p.inputs.size(), static_cast<std::size_t>(type(pickup.id)), {}}
                          : sources[pickup.id];
            if (pickup.start) {
                for (auto id : feeds[source.input])
                    next.available[id] = 1;
                ++next.plan.usage[source.input].batches;
            }
            if (pickup.buffer >= 0)
                next.buffers[pickup.buffer] = -1;
            else if (generated) {
                const auto found =
                    std::find(next.coproducts.begin(), next.coproducts.end(), type(pickup.id));
                if (found == next.coproducts.end()) return false;
                next.coproducts.erase(found);
            } else
                next.available[pickup.id] = 0;
            int buffer = pickup.buffer;
            if (kind == SupplyPlan::Kind::Store) {
                const auto free = std::find(next.buffers.begin(), next.buffers.end(), -1);
                if (free == next.buffers.end()) return false;
                buffer = static_cast<int>(free - next.buffers.begin());
                *free = static_cast<int>(pickup.id);
            } else if (!generated) {
                auto& usage = next.plan.usage[source.input];
                if (kind == SupplyPlan::Kind::Discard)
                    ++usage.discarded;
                else
                    ++usage.acquired;
            }
            next.plan.steps.push_back(
                {kind, source.input, source.atom, output, conversion, buffer});
            if (wheel) {
                next.plan.steps.back().wheelCatalyst =
                    SupplyPlan::Step::WheelCatalyst{0, wanted, 0};
                next.plan.steps.back().calcifyBeforeDuplication = calcifyBeforeDuplication;
            }
            if (needsInherited && kind == SupplyPlan::Kind::Take)
                next.direct.emplace(
                    state.next, DirectMapping{pickup.id, next.plan.usage[source.input].batches});
            if (kind == SupplyPlan::Kind::Reagent && conversion == ElementConversion::Duplication) {
                next.catalyst = static_cast<int>(pickup.id);
                next.catalystTarget = catalystReturn;
                if (catalystReturn <= state.next || catalystReturn >= order.size() ||
                    target[order[catalystReturn]].type != type(pickup.id))
                    return false;
                next.plan.steps.back().targetAtom = order[next.catalystTarget];
            } else if (kind == SupplyPlan::Kind::Take) {
                if (conversion == ElementConversion::Duplication) {
                    ++next.duplications;
                    const auto element = static_cast<std::size_t>(wanted) - 2;
                    ++next.duplicatedElements[element];
                    if (recipe && (next.duplications > recipe->duplications ||
                                   (!recipe->duplicatedElements.empty() &&
                                    (element >= recipe->duplicatedElements.size() ||
                                     next.duplicatedElements[element] >
                                         recipe->duplicatedElements[element]))))
                        return false;
                }
                if (conversion == ElementConversion::Projection)
                    next.mercury = static_cast<int>(type(pickup.id)) - static_cast<int>(wanted);
                else
                    ++next.next;
            } else if (kind == SupplyPlan::Kind::Reagent && --next.mercury == 0)
                ++next.next;
            else if (kind == SupplyPlan::Kind::Animismus) {
                if (++next.animismus == 2) {
                    next.animismus = 0;
                    ++next.animismusReactions;
                    if (recipe && next.animismusReactions > recipe->animismus) return false;
                    next.coproducts.push_back(wanted == core::AtomType::Vitae
                                                  ? core::AtomType::Mors
                                                  : core::AtomType::Vitae);
                    ++next.next;
                }
            } else if (kind == SupplyPlan::Kind::Unify) {
                next.unification |= 1U << (static_cast<int>(type(pickup.id)) - 2);
                if (next.unification == 15) {
                    next.unification = 0;
                    ++next.unifications;
                    if (recipe && next.unifications > recipe->unifications) return false;
                    ++next.next;
                }
            } else if (kind == SupplyPlan::Kind::Disperse) {
                ++next.dispersions;
                if (recipe && next.dispersions > recipe->dispersions) return false;
                next.plan.steps.back().dispersedElement = dispersedElement;
                const auto selected =
                    conversion == ElementConversion::Calcification ? dispersedElement : wanted;
                for (int t = 2; t <= 5; ++t)
                    if (static_cast<core::AtomType>(t) != selected)
                        next.coproducts.push_back(static_cast<core::AtomType>(t));
                ++next.next;
            } else if (kind == SupplyPlan::Kind::Purify) {
                const auto reagent = static_cast<int>(type(pickup.id));
                if (!next.purification)
                    next.purification = reagent;
                else {
                    next.purification = 0;
                    ++next.purificationReactions;
                    if (recipe && next.purificationReactions > recipe->purifications) return false;
                    const auto product = static_cast<core::AtomType>(reagent - 1);
                    if (product == wanted)
                        ++next.next;
                    else
                        next.coproducts.push_back(product);
                }
                next.plan.steps.back().retainProduct = reagent - 1 != static_cast<int>(wanted);
            }
            return visit(next);
        };
        // Consume compatible parked coproducts before opening another reaction batch.
        if (!finished && wanted == core::AtomType::Salt && !state.unification && !state.animismus &&
            !state.purification && !state.mercury)
            for (auto pickup : pickups) {
                if (pickup.id < sources.size()) continue;
                const auto source = type(pickup.id);
                const auto futureDemand =
                    std::count_if(order.begin() + state.next + 1, order.end(),
                                  [&](auto atom) { return target[atom].type == source; });
                if (std::count(state.coproducts.begin(), state.coproducts.end(), source) <=
                    futureDemand)
                    continue;
                if (source != wanted &&
                    (source == core::AtomType::Salt ||
                     !ChemicalPlanner::CanSupply(source, wanted, p.permissionBits)))
                    continue;
                const auto conversion = source == wanted ? ElementConversion::None
                                                         : (wanted == core::AtomType::Salt
                                                                ? ElementConversion::Calcification
                                                                : ElementConversion::Projection);
                if (transition(pickup, SupplyPlan::Kind::Take, conversion)) return true;
            }
        if (!finished && !state.mercury && !state.purification && !state.animismus &&
            (p.permissionBits & (1ULL << 18))) {
            if (wanted == core::AtomType::Quintessence &&
                (!recipe || state.unifications < recipe->unifications))
                for (auto pickup : pickups) {
                    const int t = static_cast<int>(type(pickup.id));
                    if (t >= 2 && t <= 5 && !(state.unification & (1U << (t - 2))) &&
                        transition(pickup, SupplyPlan::Kind::Unify, ElementConversion::None))
                        return true;
                }
            if (!state.unification && reserved == state.coproducts.end() &&
                (!recipe || state.dispersions < recipe->dispersions) &&
                ((wanted >= core::AtomType::Air && wanted <= core::AtomType::Water) ||
                 (wanted == core::AtomType::Salt && (p.permissionBits & (1ULL << 12)))))
                for (auto pickup : pickups) {
                    if (type(pickup.id) != core::AtomType::Quintessence) continue;
                    if (wanted != core::AtomType::Salt) {
                        if (transition(pickup, SupplyPlan::Kind::Disperse, ElementConversion::None))
                            return true;
                    } else {
                        std::array<int, 4> elements{2, 3, 4, 5};
                        const auto futureDemand = [&](int element) {
                            return std::count_if(
                                order.begin() + state.next + 1, order.end(), [&](auto atom) {
                                    return static_cast<int>(target[atom].type) == element;
                                });
                        };
                        std::stable_sort(elements.begin(), elements.end(), [&](int a, int b) {
                            return futureDemand(a) < futureDemand(b);
                        });
                        for (int element : elements)
                            if (transition(pickup, SupplyPlan::Kind::Disperse,
                                           ElementConversion::Calcification, 0,
                                           static_cast<core::AtomType>(element)))
                                return true;
                    }
                }
        }
        if (!finished && !state.unification && !state.mercury && !state.purification &&
            reserved == state.coproducts.end() &&
            (!recipe || state.animismusReactions < recipe->animismus) &&
            (p.permissionBits & (1ULL << 16)) &&
            (wanted == core::AtomType::Vitae || wanted == core::AtomType::Mors)) {
            for (auto pickup : pickups) {
                const auto source = type(pickup.id);
                if (source == core::AtomType::Salt &&
                    transition(pickup, SupplyPlan::Kind::Animismus, ElementConversion::None))
                    return true;
                const auto* precursor = std::getenv("OPUSOPTIMA_CALCIFY_ANIMISMUS");
                if ((closedCardinalLife || (precursor && std::string(precursor) == "1")) &&
                    (p.permissionBits & (1ULL << 12)) && source >= core::AtomType::Air &&
                    source <= core::AtomType::Water &&
                    transition(pickup, SupplyPlan::Kind::Animismus,
                               ElementConversion::Calcification))
                    return true;
            }
        }
        if (!finished && !state.unification && !state.animismus && !state.mercury &&
            (limits.singleStagePurification || limits.multiStagePurification) &&
            (!recipe || state.purificationReactions < recipe->purifications) &&
            (p.permissionBits & (1ULL << 15)) && wanted >= core::AtomType::Gold &&
            wanted < core::AtomType::Lead) {
            for (auto pickup : pickups)
                if (static_cast<int>(type(pickup.id)) > static_cast<int>(wanted) &&
                    type(pickup.id) <= core::AtomType::Lead &&
                    (limits.multiStagePurification ||
                     static_cast<int>(type(pickup.id)) == static_cast<int>(wanted) + 1) &&
                    (!state.purification ||
                     static_cast<int>(type(pickup.id)) == state.purification) &&
                    (state.purification || pickup.id < sources.size() ||
                     std::count(state.coproducts.begin(), state.coproducts.end(),
                                type(pickup.id)) >= 2 ||
                     std::any_of(
                         sources.begin(), sources.end(),
                         [&](const auto& source) {
                             return p.inputs[source.input].atoms[source.atom].type ==
                                    type(pickup.id);
                         })) &&
                    transition(pickup, SupplyPlan::Kind::Purify, ElementConversion::None))
                    return true;
        }
        if (!finished && !state.unification && !state.animismus && !state.purification &&
            !state.mercury && (p.permissionBits & (1ULL << 13)) && wanted >= core::AtomType::Air &&
            wanted <= core::AtomType::Water) {
            if (state.catalyst < 0) {
                if (reserved != state.coproducts.end())
                    for (std::size_t i = order.size(); i-- > state.next + 1;)
                        if (target[order[i]].type == wanted) {
                            State next = state;
                            next.coproducts.erase(next.coproducts.begin() +
                                                  (reserved - state.coproducts.begin()));
                            next.catalyst =
                                static_cast<int>(sources.size() + static_cast<std::size_t>(wanted));
                            next.catalystTarget = i;
                            next.plan.steps.push_back({SupplyPlan::Kind::Coproduct, p.inputs.size(),
                                                       static_cast<std::size_t>(wanted), order[i],
                                                       ElementConversion::Duplication});
                            if (visit(next)) return true;
                        }
                for (auto pickup : pickups)
                    if (type(pickup.id) == wanted)
                        for (std::size_t i = order.size(); i-- > state.next + 1;)
                            if (target[order[i]].type == wanted &&
                                transition(pickup, SupplyPlan::Kind::Reagent,
                                           ElementConversion::Duplication, i))
                                return true;
            } else if (type(state.catalyst) == wanted) {
                for (auto pickup : pickups)
                    if (type(pickup.id) == core::AtomType::Salt &&
                        transition(pickup, SupplyPlan::Kind::Take, ElementConversion::Duplication))
                        return true;
            }
        }
        if (limits.wheelCatalysts && (p.permissionBits & (1ULL << 28)) &&
            (p.permissionBits & (1ULL << 13)) && !finished && !state.unification &&
            !state.animismus && !state.purification && !state.mercury && !needsInherited &&
            wanted >= core::AtomType::Air && wanted <= core::AtomType::Water) {
            for (auto pickup : pickups) {
                const auto source = type(pickup.id);
                if (source == wanted) continue;
                const bool calcify = source >= core::AtomType::Air &&
                                     source <= core::AtomType::Water &&
                                     (p.permissionBits & (1ULL << 12));
                if ((source == core::AtomType::Salt || calcify) &&
                    transition(pickup, SupplyPlan::Kind::Take, ElementConversion::Duplication, 0,
                               core::AtomType::Air, true, calcify))
                    return true;
            }
        }
        if (!finished && !state.unification && !state.animismus && !state.purification)
            for (auto pickup : pickups) {
                const auto source = type(pickup.id);
                if (state.mercury && source != core::AtomType::Quicksilver) continue;
                // Salt conversion needs the separately reserved, non-consuming catalyst.
                if (source != wanted &&
                    (state.mercury || source == core::AtomType::Salt ||
                     !ChemicalPlanner::CanSupply(source, wanted, p.permissionBits)))
                    continue;
                const auto conversion = source == wanted ? ElementConversion::None
                                                         : (wanted == core::AtomType::Salt
                                                                ? ElementConversion::Calcification
                                                                : ElementConversion::Projection);
                if (conversion == ElementConversion::Projection && !hasQuicksilver) continue;
                if (transition(pickup,
                               state.mercury ? SupplyPlan::Kind::Reagent : SupplyPlan::Kind::Take,
                               conversion))
                    return true;
            }
        // Generated coproducts are already parked inventory; storing them again
        // only changes the slot name and duplicates equivalent search states.
        if (!directAvailable && !finished &&
            std::find(state.buffers.begin(), state.buffers.end(), -1) != state.buffers.end())
            for (auto pickup : pickups)
                if (pickup.buffer < 0 && pickup.id < sources.size() &&
                    transition(pickup, SupplyPlan::Kind::Store, ElementConversion::None))
                    return true;
        if (disposal && !directAvailable)
            for (auto pickup : pickups)
                if ((!finished || !pickup.start) &&
                    transition(pickup, SupplyPlan::Kind::Discard, ElementConversion::None))
                    return true;
        return false;
    };
    const bool found = visit(initial);
    result.states = expanded;
    result.status =
        found ? InventorySearchStatus::Found
              : (exhausted ? InventorySearchStatus::Exhausted : InventorySearchStatus::Rejected);
    return result;
}
} // namespace
InventorySearchResult SearchSupplyInventory(const io::PuzzleData& p, std::size_t products,
                                            InventorySearchLimits limits,
                                            const ChemicalRecipe* recipe,
                                            const std::function<bool(const SupplyPlan&)>& accept) {
    std::vector<OriginalDestination> destinations;
    if (p.outputs.size() == 1) {
        destinations.reserve(p.outputs[0].atoms.size());
        for (std::size_t atom = 0; atom < p.outputs[0].atoms.size(); ++atom)
            destinations.push_back({0, atom});
    }
    return SearchSupplyInventoryCore(p, products, limits, recipe, accept, p, destinations);
}
JointInventorySearchResult SearchJointSupplyInventory(
    const io::PuzzleData& p, std::size_t products, InventorySearchLimits limits,
    const ChemicalRecipe* recipe, const std::function<bool(const JointSupplyPlan&)>& accept) {
    return SearchJointSupplyInventoryWithPolicy(
        p, products, SupplyMaterialLoweringPolicy::PreserveCompatible, limits, recipe, accept);
}
JointInventorySearchResult SearchJointSupplyInventoryWithPolicy(
    const io::PuzzleData& p, std::size_t products, SupplyMaterialLoweringPolicy policy,
    InventorySearchLimits limits, const ChemicalRecipe* recipe,
    const std::function<bool(const JointSupplyPlan&)>& accept) {
    JointInventorySearchResult result;
    const bool closedLife = policy == SupplyMaterialLoweringPolicy::ClosedCardinalLife;
    if (closedLife && !SupportsClosedCardinalLife(p)) return result;
    if (p.outputs.empty() || !products || products > 12 || !p.outputMultiplier ||
        (6ULL * p.outputMultiplier) % products)
        return result;
    std::vector<std::size_t> startingPorts(p.outputs.size());
    std::iota(startingPorts.begin(), startingPorts.end(), 0);
    if (p.permissionBits & (1ULL << 13)) {
        // Small products can release coproduct catalysts before larger demands consume them.
        std::stable_sort(startingPorts.begin(), startingPorts.end(), [&](auto a, auto b) {
            return p.outputs[a].atoms.size() < p.outputs[b].atoms.size();
        });
    }
    const bool needsFragmentSearch =
        std::any_of(p.outputs.begin(), p.outputs.end(), [&](const auto& molecule) {
            return std::any_of(molecule.bonds.begin(), molecule.bonds.end(), [&](const auto& bond) {
                const auto at = [&](core::HexCoord cell) {
                    return std::find_if(molecule.atoms.begin(), molecule.atoms.end(),
                                        [&](const auto& atom) { return atom.position == cell; });
                };
                const auto a = at(bond.from), b = at(bond.to);
                return bond.bits == 1
                           ? !(p.permissionBits & (1ULL << 8))
                           : !(p.permissionBits & (1ULL << 11)) || a == molecule.atoms.end() ||
                                 b == molecule.atoms.end() || a->type != core::AtomType::Fire ||
                                 b->type != core::AtomType::Fire;
            });
        });
    bool exhausted = false;
    std::vector<JointSupplyPlan> deliveredPlans;
    const auto orderings =
        needsFragmentSearch ? std::vector<bool>{true, false} : std::vector<bool>{false};
    for (auto firstPort : startingPorts)
        for (bool fragmentFirst : orderings) {
            if (result.states >= limits.maxStates ||
                std::chrono::steady_clock::now() >= limits.deadline) {
                exhausted = true;
                break;
            }
            const auto startStates = result.states;
            const auto candidateBudget =
                fragmentFirst ? std::max<std::size_t>(1, (limits.maxStates - result.states) / 4)
                              : limits.maxStates - result.states;
            auto demand = p;
            demand.outputs.resize(1);
            demand.outputs[0].atoms.clear();
            demand.outputs[0].bonds.clear();
            using Destination = OriginalDestination;
            std::vector<Destination> destinations;
            for (std::size_t offset = 0; offset < p.outputs.size(); ++offset) {
                const auto port = (firstPort + offset) % p.outputs.size();
                const auto& atoms = p.outputs[port].atoms;
                if (atoms.empty() || std::any_of(atoms.begin(), atoms.end(), [](const auto& atom) {
                        return !core::AtomTypeFromId(static_cast<std::uint8_t>(atom.type)) ||
                               atom.type == core::AtomType::Repeat;
                    }))
                    return result;
                if (destinations.size() > limits.maxSteps / products ||
                    atoms.size() > limits.maxSteps / products - destinations.size()) {
                    result.status = InventorySearchStatus::Exhausted;
                    return result;
                }
                std::vector<std::size_t> order(atoms.size());
                std::iota(order.begin(), order.end(), 0);
                std::vector<unsigned> mandatoryDegree(atoms.size());
                std::vector<std::size_t> fragmentGroup(atoms.size());
                std::iota(fragmentGroup.begin(), fragmentGroup.end(), 0);
                for (const auto& bond : p.outputs[port].bonds) {
                    const auto at = [&](core::HexCoord cell) {
                        return std::find_if(
                                   atoms.begin(), atoms.end(),
                                   [&](const auto& atom) { return atom.position == cell; }) -
                               atoms.begin();
                    };
                    const auto a = at(bond.from), b = at(bond.to);
                    if (a >= atoms.size() || b >= atoms.size()) continue;
                    const bool canRebuild = bond.bits == 1
                                                ? (p.permissionBits & (1ULL << 8))
                                                : (p.permissionBits & (1ULL << 11)) &&
                                                      atoms[a].type == core::AtomType::Fire &&
                                                      atoms[b].type == core::AtomType::Fire;
                    if (!canRebuild) {
                        ++mandatoryDegree[a];
                        ++mandatoryDegree[b];
                        const auto old = fragmentGroup[b], joined = fragmentGroup[a];
                        for (auto& group : fragmentGroup)
                            if (group == old) group = joined;
                    }
                }
                std::sort(order.begin(), order.end(), [&](auto a, auto b) {
                    if (fragmentFirst && mandatoryDegree[a] != mandatoryDegree[b])
                        return mandatoryDegree[a] > mandatoryDegree[b];
                    const auto x = atoms[a].position, y = atoms[b].position;
                    return x.r != y.r ? x.r > y.r : x.q > y.q;
                });
                if (fragmentFirst && !limits.mechanicalExtractionOrder) {
                    std::vector<std::size_t> rank(atoms.size(), atoms.size());
                    for (std::size_t index = 0; index < order.size(); ++index)
                        rank[fragmentGroup[order[index]]] =
                            std::min(rank[fragmentGroup[order[index]]], index);
                    const auto direct = [&](std::size_t atom) {
                        return std::any_of(
                            p.inputs.begin(), p.inputs.end(), [&](const auto& input) {
                                return std::any_of(input.atoms.begin(), input.atoms.end(),
                                                   [&](const auto& source) {
                                                       return source.type == atoms[atom].type;
                                                   });
                            });
                    };
                    std::stable_sort(order.begin(), order.end(), [&](auto a, auto b) {
                        if (rank[fragmentGroup[a]] != rank[fragmentGroup[b]])
                            return rank[fragmentGroup[a]] < rank[fragmentGroup[b]];
                        return direct(a) > direct(b);
                    });
                }
                for (auto atom : order) {
                    // Synthetic coordinates encode demand order only; no geometry is compiled.
                    const auto index = destinations.size();
                    if (index > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
                        result.status = InventorySearchStatus::Exhausted;
                        return result;
                    }
                    demand.outputs[0].atoms.push_back(
                        {atoms[atom].type, {-static_cast<int>(index), 0}});
                    destinations.push_back({port, atom});
                }
            }
            auto mapPlan = [&](const SupplyPlan& source) {
                JointSupplyPlan plan;
                plan.usage = source.usage;
                plan.productsPerPeriod = source.productsPerPeriod;
                plan.recipeConstrained = source.recipeConstrained;
                for (auto step : source.steps) {
                    const auto destination = step.targetAtom < destinations.size()
                                                 ? destinations[step.targetAtom]
                                                 : Destination{p.outputs.size(), 0};
                    step.targetAtom = destination.atom;
                    plan.steps.push_back({step, destination.output});
                }
                return plan;
            };
            std::function<bool(const SupplyPlan&)> acceptMapped;
            if (accept)
                acceptMapped = [&](const SupplyPlan& plan) {
                    auto mapped = mapPlan(plan);
                    if (std::find(deliveredPlans.begin(), deliveredPlans.end(), mapped) !=
                        deliveredPlans.end())
                        return false;
                    deliveredPlans.push_back(mapped);
                    return accept(mapped);
                };
            auto search = [&](std::size_t states, bool prioritizeDirect) {
                auto remaining = limits;
                remaining.maxStates = states;
                auto searched =
                    SearchSupplyInventoryCore(demand, products, remaining, recipe, acceptMapped, p,
                                              destinations, prioritizeDirect, closedLife);
                result.states += searched.states;
                if (searched.status == InventorySearchStatus::Found) {
                    result.status = searched.status;
                    result.plan = mapPlan(searched.plan);
                }
                return searched.status;
            };
            if (fragmentFirst && accept && candidateBudget > 8) {
                const auto priorityBudget = std::min<std::size_t>(64, candidateBudget / 4);
                if (priorityBudget && search(priorityBudget, true) == InventorySearchStatus::Found)
                    return result;
            }
            const auto spent = result.states - startStates;
            if (spent >= candidateBudget) {
                exhausted = true;
                continue;
            }
            const auto status = search(candidateBudget - spent, false);
            if (status == InventorySearchStatus::Found) return result;
            exhausted |= status == InventorySearchStatus::Exhausted;
        }
    result.status = exhausted || result.states >= limits.maxStates
                        ? InventorySearchStatus::Exhausted
                        : InventorySearchStatus::Rejected;
    return result;
}
} // namespace opusoptima::solvers
