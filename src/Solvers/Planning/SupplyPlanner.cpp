#include "AssemblySolver.hpp"
#include "ChemicalPlanner.hpp"
#include "FeedLayout.hpp"
#include "InventorySearch.hpp"

#include <algorithm>
#include <limits>
#include <numeric>
#include <stdexcept>

namespace opusoptima::solvers {
namespace {
void CheckSupplyDeadline(std::chrono::steady_clock::time_point deadline) {
    if (std::chrono::steady_clock::now() >= deadline)
        throw std::invalid_argument("supply planning deadline");
}
// A borrowed cardinal is returned as the last cardinal target of each product.
// This leaves the duplication station empty at the tape boundary.
SupplyPlan BuildDuplicationSupply(const io::PuzzleData& p,
                                  std::chrono::steady_clock::time_point deadline) {
    const auto& target = p.outputs[0].atoms;
    int cardinal = 0;
    std::size_t saltDemand = 0, cardinalDemand = 0;
    for (const auto& atom : target) {
        CheckSupplyDeadline(deadline);
        const int type = static_cast<int>(atom.type);
        if (type == 1)
            ++saltDemand;
        else if (type >= 2 && type <= 5 && (!cardinal || cardinal == type)) {
            cardinal = type;
            ++cardinalDemand;
        } else
            throw std::invalid_argument("duplication supply requires salt and one cardinal");
    }
    if (!(p.permissionBits & (1ULL << 13)) || cardinalDemand < 2)
        throw std::invalid_argument("duplication supply requires repeated cardinal targets");
    std::size_t salt = p.inputs.size(), catalyst = p.inputs.size();
    for (std::size_t i = 0; i < p.inputs.size(); ++i) {
        CheckSupplyDeadline(deadline);
        const auto& atoms = p.inputs[i].atoms;
        if (!atoms.empty() && std::all_of(atoms.begin(), atoms.end(), [](const auto& a) {
                return a.type == core::AtomType::Salt;
            }))
            salt = i;
        if (atoms.size() == 1 && static_cast<int>(atoms[0].type) == cardinal) catalyst = i;
    }
    if (salt == p.inputs.size() || catalyst == p.inputs.size())
        throw std::invalid_argument(
            "duplication supply needs salt feed and singleton catalyst feed");
    const auto perProduct = saltDemand + cardinalDemand - 1;
    std::size_t products = 1;
    while (products <= 12 && ((products * perProduct) % p.inputs[salt].atoms.size() ||
                              (6ULL * p.outputMultiplier) % products))
        ++products;
    if (products > 12) throw std::invalid_argument("no periodic duplication inventory");
    SupplyPlan plan;
    plan.productsPerPeriod = products;
    plan.usage.resize(p.inputs.size());
    std::vector<std::size_t> order(target.size());
    std::iota(order.begin(), order.end(), 0);
    std::sort(order.begin(), order.end(), [&](auto a, auto b) {
        CheckSupplyDeadline(deadline);
        const auto x = target[a].position, y = target[b].position;
        return x.r != y.r ? x.r > y.r : x.q > y.q;
    });
    const auto last = *std::find_if(order.rbegin(), order.rend(), [&](auto i) {
        return static_cast<int>(target[i].type) == cardinal;
    });
    const auto layout = PlanExtractionLayout(p.inputs[salt]);
    std::vector<std::size_t> saltOrder(p.inputs[salt].atoms.size());
    std::iota(saltOrder.begin(), saltOrder.end(), 0);
    std::stable_sort(saltOrder.begin(), saltOrder.end(), [&](auto a, auto b) {
        const auto x = layout.cells[a], y = layout.cells[b];
        return x.r != y.r ? x.r > y.r : x.q > y.q;
    });
    std::size_t cursor = 0;
    for (std::size_t product = 0; product < products; ++product) {
        CheckSupplyDeadline(deadline);
        plan.steps.push_back(
            {SupplyPlan::Kind::Reagent, catalyst, 0, last, ElementConversion::Duplication});
        ++plan.usage[catalyst].batches;
        ++plan.usage[catalyst].acquired;
        for (auto output : order) {
            CheckSupplyDeadline(deadline);
            if (output == last) {
                // buffer -2 denotes the borrowed catalyst, not another source acquisition.
                plan.steps.push_back(
                    {SupplyPlan::Kind::Take, catalyst, 0, output, ElementConversion::None, -2});
            } else {
                const auto conversion = target[output].type == core::AtomType::Salt
                                            ? ElementConversion::None
                                            : ElementConversion::Duplication;
                plan.steps.push_back({SupplyPlan::Kind::Take, salt,
                                      saltOrder[cursor++ % saltOrder.size()], output, conversion});
                ++plan.usage[salt].acquired;
            }
        }
    }
    plan.usage[salt].batches = cursor / saltOrder.size();
    return plan;
}

SupplyPlan BuildMixedDuplicationSupply(const io::PuzzleData& p,
                                       std::chrono::steady_clock::time_point deadline) {
    const auto& target = p.outputs[0].atoms;
    int cardinal = 0;
    for (const auto& atom : target) {
        CheckSupplyDeadline(deadline);
        const int type = static_cast<int>(atom.type);
        if (type == 1) continue;
        if (type < 2 || type > 5 || (cardinal && cardinal != type))
            throw std::invalid_argument("mixed duplication requires one cardinal");
        cardinal = type;
    }
    if (!cardinal || !(p.permissionBits & (1ULL << 13)))
        throw std::invalid_argument(
            "mixed duplication requires a catalyst and duplication permission");
    std::vector<std::size_t> outputs(target.size());
    std::iota(outputs.begin(), outputs.end(), 0);
    std::sort(outputs.begin(), outputs.end(), [&](auto a, auto b) {
        const auto x = target[a].position, y = target[b].position;
        return x.r != y.r ? x.r > y.r : x.q > y.q;
    });
    const auto last = *std::find_if(outputs.rbegin(), outputs.rend(), [&](auto i) {
        return static_cast<int>(target[i].type) == cardinal;
    });
    for (std::size_t input = 0; input < p.inputs.size(); ++input) {
        CheckSupplyDeadline(deadline);
        const auto& atoms = p.inputs[input].atoms;
        if (atoms.empty() ||
            std::none_of(atoms.begin(), atoms.end(),
                         [&](const auto& a) { return static_cast<int>(a.type) == cardinal; }) ||
            !std::all_of(atoms.begin(), atoms.end(), [&](const auto& a) {
                return a.type == core::AtomType::Salt || static_cast<int>(a.type) == cardinal;
            }))
            continue;
        const auto products = atoms.size() / std::gcd(atoms.size(), target.size());
        if (products > 12 || (6ULL * p.outputMultiplier) % products) continue;
        const auto layout = PlanExtractionLayout(p.inputs[input]);
        std::vector<std::size_t> sources(atoms.size());
        std::iota(sources.begin(), sources.end(), 0);
        std::stable_sort(sources.begin(), sources.end(), [&](auto a, auto b) {
            const auto x = layout.cells[a], y = layout.cells[b];
            return x.r != y.r ? x.r > y.r : x.q > y.q;
        });
        SupplyPlan plan;
        plan.productsPerPeriod = products;
        plan.usage.resize(p.inputs.size());
        std::vector<int> buffers;
        std::size_t cursor = 0;
        const auto total = products * target.size();
        std::size_t remainingCardinals =
            total / atoms.size() * std::count_if(atoms.begin(), atoms.end(), [&](const auto& a) {
                return static_cast<int>(a.type) == cardinal;
            });
        std::size_t reservedCatalysts = 1;
        auto take = [&](core::AtomType wanted, bool allowSalt, std::size_t output,
                        SupplyPlan::Kind kind) {
            const bool useCardinal = kind == SupplyPlan::Kind::Reagent ||
                                     (allowSalt && remainingCardinals > reservedCatalysts);
            auto matches = [&](std::size_t source) {
                return atoms[source].type ==
                       (useCardinal ? static_cast<core::AtomType>(cardinal) : core::AtomType::Salt);
            };
            int buffer = -1;
            std::size_t source = atoms.size();
            for (std::size_t b = 0; b < buffers.size(); ++b)
                if (buffers[b] >= 0 && matches(buffers[b])) {
                    CheckSupplyDeadline(deadline);
                    buffer = static_cast<int>(b);
                    source = buffers[b];
                    buffers[b] = -1;
                    break;
                }
            while (source == atoms.size() && cursor < total) {
                CheckSupplyDeadline(deadline);
                const auto next = sources[cursor++ % sources.size()];
                if (matches(next)) {
                    source = next;
                    break;
                }
                auto free = std::find(buffers.begin(), buffers.end(), -1);
                const int slot = static_cast<int>(free - buffers.begin());
                if (free == buffers.end())
                    buffers.push_back(static_cast<int>(next));
                else
                    *free = static_cast<int>(next);
                plan.steps.push_back(
                    {SupplyPlan::Kind::Store, input, next, output, ElementConversion::None, slot});
            }
            if (source == atoms.size()) return false;
            if (useCardinal) --remainingCardinals;
            if (kind == SupplyPlan::Kind::Reagent) --reservedCatalysts;
            const auto conversion =
                kind == SupplyPlan::Kind::Reagent || atoms[source].type != wanted
                    ? ElementConversion::Duplication
                    : ElementConversion::None;
            plan.steps.push_back({kind, input, source, output, conversion, buffer});
            return true;
        };
        bool valid = true;
        std::size_t catalyst = 0;
        for (std::size_t product = 0; product < products && valid; ++product) {
            CheckSupplyDeadline(deadline);
            if (product == 0) {
                valid = take(static_cast<core::AtomType>(cardinal), false, last,
                             SupplyPlan::Kind::Reagent);
                if (!valid) break;
                catalyst = plan.steps.back().sourceAtom;
            }
            for (auto output : outputs) {
                CheckSupplyDeadline(deadline);
                if (output == last && product + 1 == products)
                    plan.steps.push_back({SupplyPlan::Kind::Take, input, catalyst, output,
                                          ElementConversion::None, -2});
                else if (!take(target[output].type,
                               static_cast<int>(target[output].type) == cardinal, output,
                               SupplyPlan::Kind::Take)) {
                    valid = false;
                    break;
                }
            }
        }
        if (!valid || cursor != total ||
            std::any_of(buffers.begin(), buffers.end(), [](int b) { return b >= 0; }))
            continue;
        plan.usage[input].batches = total / atoms.size();
        plan.usage[input].acquired = total;
        return plan;
    }
    throw std::invalid_argument("no closed mixed duplication inventory");
}

SupplyPlan BuildSupply(const io::PuzzleData& p, const ChemicalRecipe* recipe, std::size_t products,
                       std::chrono::steady_clock::time_point deadline) {
    if (recipe && (recipe->duplications || recipe->purifications || recipe->animismus))
        throw std::invalid_argument("serial supply does not implement this recipe's reactions");
    SupplyPlan plan;
    plan.productsPerPeriod = products;
    const bool disposal = p.permissionBits & (1ULL << 17);
    plan.recipeConstrained = recipe != nullptr;
    std::vector<std::size_t> consumed(16);
    std::size_t conversions = 0, projections = 0;
    plan.usage.resize(p.inputs.size());
    std::vector<std::vector<bool>> available;
    std::vector<FeedLayout> layouts;
    std::vector<bool> wide;
    for (const auto& input : p.inputs) {
        CheckSupplyDeadline(deadline);
        available.emplace_back(input.atoms.size(), true);
        layouts.push_back(PlanExtractionLayout(input));
        int low = 256, high = -256;
        for (auto c : layouts.back().cells) {
            CheckSupplyDeadline(deadline);
            low = std::min(low, c.r);
            high = std::max(high, c.r);
        }
        wide.push_back(high - low > 2);
    }
    if (std::any_of(wide.begin(), wide.end(), [](bool value) { return value; }))
        std::fill(wide.begin(), wide.end(), true);
    auto blockers = [&](std::size_t input, std::size_t atom, bool reset) {
        std::vector<std::size_t> blocked;
        const auto c = layouts[input].cells[atom];
        for (std::size_t j = 0; j < layouts[input].cells.size(); ++j) {
            CheckSupplyDeadline(deadline);
            const auto other = layouts[input].cells[j];
            if ((reset || available[input][j]) && (wide[input] || other.q == c.q) && other.r > c.r)
                blocked.push_back(j);
        }
        std::stable_sort(blocked.begin(), blocked.end(), [&](auto a, auto b) {
            const auto x = layouts[input].cells[a], y = layouts[input].cells[b];
            return x.r != y.r ? x.r > y.r : x.q > y.q;
        });
        return blocked;
    };
    auto descending = [&](std::size_t input) {
        std::vector<std::size_t> atoms(available[input].size());
        std::iota(atoms.begin(), atoms.end(), 0);
        std::stable_sort(atoms.begin(), atoms.end(), [&](auto a, auto b) {
            const auto x = layouts[input].cells[a], y = layouts[input].cells[b];
            return x.r != y.r ? x.r > y.r : x.q > y.q;
        });
        return atoms;
    };
    std::vector<std::size_t> order(p.outputs[0].atoms.size());
    std::iota(order.begin(), order.end(), 0);
    const auto& target = p.outputs[0].atoms;
    std::sort(order.begin(), order.end(), [&](auto a, auto b) {
        const auto x = target[a].position, y = target[b].position;
        return x.r != y.r ? x.r > y.r : x.q > y.q;
    });
    const auto moleculeOrder = order;
    for (std::size_t product = 1; product < products; ++product)
        order.insert(order.end(), moleculeOrder.begin(), moleculeOrder.end());
    auto emit = [&](SupplyPlan::Kind kind, std::size_t input, std::size_t atom,
                    std::size_t output) {
        CheckSupplyDeadline(deadline);
        auto& usage = plan.usage[input];
        if (usage.batches == 0) usage.batches = 1;
        const auto source = p.inputs[input].atoms[atom].type;
        const auto conversion =
            kind == SupplyPlan::Kind::Take && source != target[output].type
                ? (target[output].type == core::AtomType::Salt ? ElementConversion::Calcification
                                                               : ElementConversion::Projection)
                : ElementConversion::None;
        plan.steps.push_back({kind, input, atom, output, conversion});
        if (kind != SupplyPlan::Kind::Discard)
            ++usage.acquired;
        else
            ++usage.discarded;
        ++consumed[static_cast<int>(p.inputs[input].atoms[atom].type) - 1];
        if (conversion == ElementConversion::Calcification) ++conversions;
        if (conversion == ElementConversion::Projection)
            projections += static_cast<int>(source) - static_cast<int>(target[output].type);
        available[input][atom] = false;
    };
    auto refill = [&](std::size_t input) {
        std::fill(available[input].begin(), available[input].end(), true);
        // Count a regenerated batch only if a later step actually uses it.
    };
    std::vector<bool> fresh(p.inputs.size(), false);
    struct Buffered {
        std::size_t input, atom;
        bool occupied{};
    };
    std::vector<Buffered> buffered(8);
    auto store = [&](std::size_t input, std::size_t atom, std::size_t output) {
        const auto free = std::find_if(buffered.begin(), buffered.end(),
                                       [](const auto& b) { return !b.occupied; });
        if (free == buffered.end()) throw std::invalid_argument("feed buffer capacity exceeded");
        const auto index = static_cast<int>(free - buffered.begin());
        *free = {input, atom, true};
        plan.steps.push_back(
            {SupplyPlan::Kind::Store, input, atom, output, ElementConversion::None, index});
        available[input][atom] = false;
        if (plan.usage[input].batches == 0) plan.usage[input].batches = 1;
    };
    for (std::size_t pos = 0; pos < order.size(); ++pos) {
        CheckSupplyDeadline(deadline);
        const auto output = order[pos];
        const bool direct = std::any_of(p.inputs.begin(), p.inputs.end(), [&](const auto& input) {
            return std::any_of(input.atoms.begin(), input.atoms.end(),
                               [&](const auto& atom) { return atom.type == target[output].type; });
        });
        auto canSupply = [&](core::AtomType source) {
            return !(source == core::AtomType::Salt &&
                     target[output].type != core::AtomType::Salt) &&
                   ChemicalPlanner::CanSupply(source, target[output].type, p.permissionBits);
        };
        auto matches = [&](core::AtomType source) {
            return source == target[output].type || (!direct && canSupply(source));
        };
        std::size_t chosen = p.inputs.size(), chosenAtom = 0;
        int chosenBuffer = -1;
        for (std::size_t b = 0; b < buffered.size(); ++b) {
            CheckSupplyDeadline(deadline);
            const auto& item = buffered[b];
            if (item.occupied && (recipe ? canSupply(p.inputs[item.input].atoms[item.atom].type)
                                         : matches(p.inputs[item.input].atoms[item.atom].type))) {
                chosen = item.input;
                chosenAtom = item.atom;
                chosenBuffer = static_cast<int>(b);
                break;
            }
        }
        std::size_t bestWaste = std::numeric_limits<std::size_t>::max(), bestUseful = 0;
        for (std::size_t input = 0; chosenBuffer < 0 && input < p.inputs.size(); ++input) {
            CheckSupplyDeadline(deadline);
            const auto& atoms = p.inputs[input].atoms;
            for (std::size_t match = 0; match < atoms.size(); ++match) {
                CheckSupplyDeadline(deadline);
                if (atoms[match].type == core::AtomType::Salt &&
                    target[output].type != core::AtomType::Salt)
                    continue;
                if (!(recipe ? ChemicalPlanner::CanSupply(atoms[match].type, target[output].type,
                                                          p.permissionBits)
                             : matches(atoms[match].type)))
                    continue;
                const bool reset = !available[input][match];
                if (!recipe && reset &&
                    std::any_of(atoms.begin(), atoms.end(), [&](const auto& atom) {
                        const auto j = static_cast<std::size_t>(&atom - atoms.data());
                        return available[input][j] && matches(atom.type);
                    }))
                    continue;
                const bool newBatch = reset || fresh[input] || plan.usage[input].batches == 0;
                if (recipe && plan.usage[input].batches + newBatch > recipe->inputBatches[input])
                    continue;
                const auto obstructing = blockers(input, match, reset);
                const auto waste =
                    (reset ? std::count(available[input].begin(), available[input].end(), true)
                           : 0) +
                    obstructing.size();
                if (!disposal && waste > std::count_if(buffered.begin(), buffered.end(),
                                                       [](const auto& b) { return !b.occupied; }))
                    continue;
                if (recipe && recipe->projections == 0 && recipe->duplications == 0) {
                    std::vector<std::int64_t> stock(16), demand(16);
                    for (std::size_t i = 0; i < p.inputs.size(); ++i)
                        for (const auto& atom : p.inputs[i].atoms)
                            stock[static_cast<int>(atom.type) - 1] += recipe->inputBatches[i];
                    for (std::size_t i = 0; i < stock.size(); ++i)
                        stock[i] -= static_cast<std::int64_t>(consumed[i]);
                    auto remove = [&](std::size_t atom) {
                        CheckSupplyDeadline(deadline);
                        --stock[static_cast<int>(atoms[atom].type) - 1];
                    };
                    if (disposal) {
                        if (reset)
                            for (std::size_t j = 0; j < atoms.size(); ++j)
                                if (available[input][j]) remove(j);
                        for (auto j : obstructing)
                            remove(j);
                    }
                    remove(match);
                    for (std::size_t n = pos + 1; n < order.size(); ++n)
                        ++demand[static_cast<int>(target[order[n]].type) - 1];
                    const auto converted = conversions + (atoms[match].type != target[output].type);
                    if (converted > recipe->calcifications) continue;
                    bool feasible = stock[0] >= 0;
                    std::int64_t convertible = 0;
                    for (std::size_t i = 1; i < stock.size(); ++i) {
                        CheckSupplyDeadline(deadline);
                        if (stock[i] < demand[i]) {
                            feasible = false;
                            break;
                        }
                        if (i <= 4) convertible += stock[i] - demand[i];
                    }
                    const auto deficit = demand[0] > stock[0] ? demand[0] - stock[0] : 0;
                    if (!feasible || deficit > convertible ||
                        deficit > static_cast<std::int64_t>(recipe->calcifications - converted))
                        continue;
                }
                std::vector<core::AtomType> remaining;
                for (std::size_t n = pos + 1; n < order.size(); ++n)
                    remaining.push_back(target[order[n]].type);
                std::size_t useful = 0;
                for (std::size_t j = 0; j < atoms.size(); ++j)
                    if (j != match && (reset || available[input][j]) &&
                        std::find(obstructing.begin(), obstructing.end(), j) == obstructing.end()) {
                        CheckSupplyDeadline(deadline);
                        const auto it =
                            std::find(remaining.begin(), remaining.end(), atoms[j].type);
                        if (it != remaining.end()) {
                            ++useful;
                            remaining.erase(it);
                        }
                    }
                if (chosen == p.inputs.size() || static_cast<std::size_t>(waste) < bestWaste ||
                    (static_cast<std::size_t>(waste) == bestWaste && useful > bestUseful)) {
                    chosen = input;
                    chosenAtom = match;
                    bestWaste = waste;
                    bestUseful = useful;
                }
            }
        }
        if (chosen == p.inputs.size())
            throw std::invalid_argument("supply requires unsupported chemistry");
        if (chosenBuffer < 0) {
            if (!available[chosen][chosenAtom]) {
                for (auto j : descending(chosen))
                    if (available[chosen][j]) {
                        CheckSupplyDeadline(deadline);
                        if (disposal)
                            emit(SupplyPlan::Kind::Discard, chosen, j, output);
                        else
                            store(chosen, j, output);
                    }
                refill(chosen);
                fresh[chosen] = true;
            }
            if (fresh[chosen]) {
                ++plan.usage[chosen].batches;
                fresh[chosen] = false;
            }
            for (auto j : blockers(chosen, chosenAtom, false)) {
                CheckSupplyDeadline(deadline);
                if (disposal)
                    emit(SupplyPlan::Kind::Discard, chosen, j, output);
                else
                    store(chosen, j, output);
            }
            emit(SupplyPlan::Kind::Take, chosen, chosenAtom, output);
        } else {
            const bool sourceAvailable = available[chosen][chosenAtom];
            emit(SupplyPlan::Kind::Take, chosen, chosenAtom, output);
            available[chosen][chosenAtom] = sourceAvailable;
            plan.steps.back().buffer = chosenBuffer;
            buffered[chosenBuffer].occupied = false;
        }
        const auto levels = static_cast<int>(p.inputs[chosen].atoms[chosenAtom].type) -
                            static_cast<int>(target[output].type);
        if (plan.steps.back().conversion == ElementConversion::Projection) {
            for (int level = 0; level < levels; ++level) {
                CheckSupplyDeadline(deadline);
                std::size_t mercuryInput = p.inputs.size(), mercuryAtom = 0;
                int mercuryBuffer = -1;
                for (std::size_t b = 0; b < buffered.size(); ++b) {
                    CheckSupplyDeadline(deadline);
                    const auto& item = buffered[b];
                    if (item.occupied &&
                        p.inputs[item.input].atoms[item.atom].type == core::AtomType::Quicksilver) {
                        mercuryInput = item.input;
                        mercuryAtom = item.atom;
                        mercuryBuffer = static_cast<int>(b);
                        break;
                    }
                }
                std::size_t mercuryWaste = std::numeric_limits<std::size_t>::max();
                for (std::size_t i = 0; mercuryBuffer < 0 && i < p.inputs.size(); ++i)
                    for (std::size_t j = 0; j < p.inputs[i].atoms.size(); ++j) {
                        CheckSupplyDeadline(deadline);
                        if (p.inputs[i].atoms[j].type != core::AtomType::Quicksilver) continue;
                        const bool reset = !available[i][j];
                        const bool newBatch = reset || fresh[i] || plan.usage[i].batches == 0;
                        if (recipe && plan.usage[i].batches + newBatch > recipe->inputBatches[i])
                            continue;
                        const auto waste =
                            blockers(i, j, reset).size() +
                            (reset ? std::count(available[i].begin(), available[i].end(), true)
                                   : 0);
                        if (!disposal &&
                            waste > std::count_if(buffered.begin(), buffered.end(),
                                                  [](const auto& b) { return !b.occupied; }))
                            continue;
                        if (waste < mercuryWaste) {
                            mercuryInput = i;
                            mercuryAtom = j;
                            mercuryWaste = waste;
                        }
                    }
                if (mercuryInput == p.inputs.size())
                    throw std::invalid_argument("projection requires mercury feed");
                if (mercuryBuffer >= 0) {
                    const bool sourceAvailable = available[mercuryInput][mercuryAtom];
                    emit(SupplyPlan::Kind::Reagent, mercuryInput, mercuryAtom, output);
                    available[mercuryInput][mercuryAtom] = sourceAvailable;
                    plan.steps.back().buffer = mercuryBuffer;
                    buffered[mercuryBuffer].occupied = false;
                } else {
                    if (!available[mercuryInput][mercuryAtom]) {
                        for (auto j : descending(mercuryInput))
                            if (available[mercuryInput][j]) {
                                CheckSupplyDeadline(deadline);
                                if (disposal)
                                    emit(SupplyPlan::Kind::Discard, mercuryInput, j, output);
                                else
                                    store(mercuryInput, j, output);
                            }
                        refill(mercuryInput);
                        fresh[mercuryInput] = true;
                    }
                    if (fresh[mercuryInput]) {
                        ++plan.usage[mercuryInput].batches;
                        fresh[mercuryInput] = false;
                    }
                    for (auto j : blockers(mercuryInput, mercuryAtom, false)) {
                        CheckSupplyDeadline(deadline);
                        if (disposal)
                            emit(SupplyPlan::Kind::Discard, mercuryInput, j, output);
                        else
                            store(mercuryInput, j, output);
                    }
                    emit(SupplyPlan::Kind::Reagent, mercuryInput, mercuryAtom, output);
                }
            }
        }
        if (std::none_of(available[chosen].begin(), available[chosen].end(),
                         [](bool v) { return v; })) {
            refill(chosen);
            fresh[chosen] = true;
        }
    }
    for (std::size_t input = 0; input < p.inputs.size(); ++input) {
        CheckSupplyDeadline(deadline);
        if (fresh[input] || plan.usage[input].batches == 0) continue;
        for (auto j : descending(input))
            if (available[input][j]) {
                CheckSupplyDeadline(deadline);
                if (!disposal)
                    throw std::invalid_argument(
                        "supply period leaves unused inventory without disposal permission");
                emit(SupplyPlan::Kind::Discard, input, j, target.size());
            }
    }
    if (std::any_of(buffered.begin(), buffered.end(), [](const auto& b) { return b.occupied; }))
        throw std::invalid_argument("supply period leaves buffered inventory");
    if (recipe) {
        if (conversions > recipe->calcifications || projections > recipe->projections)
            throw std::invalid_argument("reaction budget exceeded");
        for (std::size_t i = 0; i < plan.usage.size(); ++i)
            if (plan.usage[i].batches > recipe->inputBatches[i])
                throw std::invalid_argument("input budget exceeded");
    }
    return plan;
}
} // namespace

SupplyPlan AssemblyPlanner::PlanSupply(const io::PuzzleData& p, bool forest,
                                       std::size_t maxSearchStates,
                                       std::chrono::steady_clock::time_point deadline) {
    CheckSupplyDeadline(deadline);
    (void)Plan(p, forest, deadline);
    try {
        return BuildDuplicationSupply(p, deadline);
    } catch (const std::invalid_argument&) {
        CheckSupplyDeadline(deadline);
    }
    const std::size_t maxProducts = (p.permissionBits & (1ULL << 17)) ? 1 : 12;
    try {
        const auto recipe = ChemicalPlanner::Plan(p, 1, true, deadline);
        for (std::size_t products = 1; products <= maxProducts; ++products) {
            CheckSupplyDeadline(deadline);
            if ((6ULL * p.outputMultiplier) % products != 0) continue;
            auto budget = recipe;
            for (auto& batches : budget.inputBatches)
                batches *= products;
            budget.calcifications *= products;
            budget.projections *= products;
            budget.duplications *= products;
            budget.purifications *= products;
            budget.animismus *= products;
            try {
                return BuildSupply(p, &budget, products, deadline);
            } catch (const std::invalid_argument&) {
                CheckSupplyDeadline(deadline);
                try {
                    return BuildSupply(p, nullptr, products, deadline);
                } catch (const std::invalid_argument&) {
                    CheckSupplyDeadline(deadline);
                }
            }
        }
    } catch (const std::invalid_argument&) {
        CheckSupplyDeadline(deadline);
    }
    try {
        return BuildMixedDuplicationSupply(p, deadline);
    } catch (const std::invalid_argument&) {
        CheckSupplyDeadline(deadline);
    }
    // All recipe and period alternatives share one expansion budget.
    auto search = [&](std::size_t products, const ChemicalRecipe* recipe = nullptr) {
        InventorySearchLimits limits;
        limits.maxStates = maxSearchStates;
        limits.deadline = deadline;
        auto result = SearchSupplyInventory(p, products, limits, recipe);
        maxSearchStates -= std::min(maxSearchStates, result.states);
        return result;
    };
    // Preserve cheap greedy plans before searching complete-period recipes.
    for (std::size_t products = 1; products <= 12; ++products) {
        CheckSupplyDeadline(deadline);
        if ((6ULL * p.outputMultiplier) % products != 0) continue;
        try {
            const auto budget = ChemicalPlanner::Plan(p, products, false, deadline);
            const auto searched = search(products, &budget);
            if (searched.status == InventorySearchStatus::Found) return searched.plan;
        } catch (const std::invalid_argument&) {
            CheckSupplyDeadline(deadline);
        }
    }
    if (p.permissionBits & (1ULL << 17))
        for (std::size_t products = 1; products <= 12; ++products) {
            CheckSupplyDeadline(deadline);
            if ((6ULL * p.outputMultiplier) % products != 0) continue;
            try {
                const auto budget = ChemicalPlanner::Plan(p, products, true, deadline);
                const auto searched = search(products, &budget);
                if (searched.status == InventorySearchStatus::Found) return searched.plan;
            } catch (const std::invalid_argument&) {
                CheckSupplyDeadline(deadline);
            }
        }
    // The fallback emits only the existing row compiler's serial supply operations.
    for (std::size_t products = 1; products <= maxProducts; ++products) {
        CheckSupplyDeadline(deadline);
        if ((6ULL * p.outputMultiplier) % products != 0) continue;
        const auto searched = search(products);
        if (searched.status == InventorySearchStatus::Found) return searched.plan;
    }
    throw std::invalid_argument("no supply plan within bounded inventory search");
}
} // namespace opusoptima::solvers
