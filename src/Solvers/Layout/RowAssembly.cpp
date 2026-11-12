#include "ExecutionPlan.hpp"
#include "FeedLayout.hpp"
#include "FragmentCustody.hpp"
#include "InventorySearch.hpp"
#include "ProgramBuilder.hpp"
#include "RowAssemblyInternal.hpp"
#include "RowPhaseProfile.hpp"
#include "SerialArmExecution.hpp"
#include <cmath>

#include <algorithm>
#include <cstdlib>
#include <limits>
#include <map>
#include <numeric>
#include <queue>
#include <set>
#include <stdexcept>
#include <string_view>

namespace opusoptima::solvers {
// Explicit, per-call intake mode used by the portfolio: +1 forces the local
// source intake, -1 forces the shared feeder, 0 keeps the environment flag.
static thread_local int g_intakeMode = 0;
// Row-clock override: 0 = follow the OPUSOPTIMA_ROW_* defaults, >0 = force the
// pipelined clocks, <0 = force the serialized emitter. Families that post-process
// an emitted row (the wheel family relocates every object by a fixed offset) need
// the serialized timing, because their own tape surgery assumes it.
static thread_local int g_rowClockMode = 0;
// Narrow override for the finite-family opt-in only. `RowClockScope` forces every
// row knob at once, which changes the emitted machine on families that need the
// stock defaults, so the bounded-run sibling flips just this one flag instead.
static thread_local int g_rowFiniteClockOptIn = 0;
// Set while emitting: true when the local intake geometry was actually used, so
// the portfolio only compiles a feeder fallback when it can differ.
static thread_local bool g_intakeApplied = false;
// One row-clock knob: the thread-local override wins, then the env flag, then the
// shipped default (on). `=off` on any knob and `RowClockScope(-1)` both restore the
// serialized emitter.
static bool RowClockEnabledFrom(const char* name) {
    if (g_rowClockMode != 0) return g_rowClockMode > 0;
    const char* flag = std::getenv(name);
    return !flag || std::string_view(flag) != "off";
}
// Opt-in variant of the knob above: same RowClockScope override, but the shipped
// default is off. Used for families whose widened clock coverage is only correct
// for some puzzles, so the portfolio has to try both.
static bool RowClockOptIn(const char* name) {
    if (std::string_view(name) == "OPUSOPTIMA_ROW_FINITE_CLOCKS" && g_rowFiniteClockOptIn != 0)
        return g_rowFiniteClockOptIn > 0;
    if (g_rowClockMode != 0) return g_rowClockMode > 0;
    const char* flag = std::getenv(name);
    return flag && std::string_view(flag) != "off";
}
namespace {
SupplyPlan PlanProxySupply(const io::PuzzleData& p,
                           std::chrono::steady_clock::time_point deadline) {
    try {
        return AssemblyPlanner::PlanSupply(p, false, 20000, deadline);
    } catch (const std::invalid_argument&) {
    }
    if (std::chrono::steady_clock::now() >= deadline)
        throw std::invalid_argument("proxy supply deadline");
    const auto& target = p.outputs[0].atoms;
    const auto cardinal = target.front().type;
    if (!(p.permissionBits & (1ULL << 13)) || p.inputs.size() != 1 || target.size() < 2 ||
        cardinal < core::AtomType::Air || cardinal > core::AtomType::Water ||
        !std::all_of(target.begin(), target.end(),
                     [&](const auto& a) { return a.type == cardinal; }))
        throw std::invalid_argument("no supported proxy supply");
    const auto& input = p.inputs[0];
    if (std::none_of(input.atoms.begin(), input.atoms.end(),
                     [&](const auto& a) { return a.type == cardinal; }) ||
        !std::all_of(input.atoms.begin(), input.atoms.end(), [&](const auto& a) {
            return a.type == cardinal || a.type == core::AtomType::Salt;
        }))
        throw std::invalid_argument("proxy duplication requires salt/cardinal feed");
    SupplyPlan supply;
    supply.productsPerPeriod = input.atoms.size() / std::gcd(input.atoms.size(), target.size());
    if (supply.productsPerPeriod > 12 || (6ULL * p.outputMultiplier) % supply.productsPerPeriod)
        throw std::invalid_argument("no periodic proxy duplication supply");
    const auto layout = PlanExtractionLayout(input);
    std::vector<std::size_t> sources(input.atoms.size()), outputs(target.size());
    std::iota(sources.begin(), sources.end(), 0);
    std::iota(outputs.begin(), outputs.end(), 0);
    auto descending = [](auto a, auto b) { return a.r != b.r ? a.r > b.r : a.q > b.q; };
    std::stable_sort(sources.begin(), sources.end(),
                     [&](auto a, auto b) { return descending(layout.cells[a], layout.cells[b]); });
    std::sort(outputs.begin(), outputs.end(),
              [&](auto a, auto b) { return descending(target[a].position, target[b].position); });
    std::vector<std::size_t> buffered;
    std::size_t cursor = 0;
    auto next = [&]() {
        if (std::chrono::steady_clock::now() >= deadline)
            throw std::invalid_argument("proxy supply deadline");
        return sources[cursor++ % sources.size()];
    };
    for (std::size_t product = 0; product < supply.productsPerPeriod; ++product) {
        auto catalyst = next();
        while (input.atoms[catalyst].type != cardinal) {
            supply.steps.push_back({SupplyPlan::Kind::Store, 0, catalyst, outputs.front(),
                                    ElementConversion::None, static_cast<int>(buffered.size())});
            buffered.push_back(catalyst);
            catalyst = next();
        }
        supply.steps.push_back({SupplyPlan::Kind::Reagent, 0, catalyst, outputs.back(),
                                ElementConversion::Duplication});
        for (std::size_t i = 0; i + 1 < outputs.size(); ++i) {
            int buffer = -1;
            std::size_t source;
            if (!buffered.empty()) {
                buffer = static_cast<int>(buffered.size()) - 1;
                source = buffered.back();
                buffered.pop_back();
            } else
                source = next();
            supply.steps.push_back({SupplyPlan::Kind::Take, 0, source, outputs[i],
                                    input.atoms[source].type == cardinal
                                        ? ElementConversion::None
                                        : ElementConversion::Duplication,
                                    buffer});
        }
        supply.steps.push_back(
            {SupplyPlan::Kind::Take, 0, catalyst, outputs.back(), ElementConversion::None, -2});
    }
    if (!buffered.empty() || cursor % sources.size())
        throw std::invalid_argument("proxy inventory does not close");
    supply.usage.push_back({cursor / sources.size(), cursor, 0});
    return supply;
}
} // namespace
// Row assembly geometry follows the universal-assembler approach in OpusSolver
// (gtw123, fdedd3f01a238b941ceba8bace7ea35cbd3b8c41).
// See third_party/OpusSolver-LICENSE.txt for the upstream MIT notice.
// The implementation here emits explicit, serialized machine instructions.
struct MixedReaction {
    std::array<SupplyPlan::Step, 2> inputs;
    std::array<int, 2> outputs;
};
struct MixedInventoryProgram {
    bool scheduleEmptyReturns{false};
    bool compactBuffers{false};
    std::vector<int> cacheSlots;
    int cacheCapacity{};
    int bypassSlot{-1};
    mutable std::vector<SharedCyclePhase> phases;
    mutable std::size_t emptyReturnTasks{};
    mutable std::int64_t emptyReturnCycles{};
    std::vector<SupplyPlan::Step> stores;
    std::vector<MixedReaction> reactions;
};
static io::SolutionData CompileRowsWithRetimingPolicy(
    const io::PuzzleData& p, const AssemblyPlan& plan, const io::PuzzleData* sharedOutputs,
    const io::Molecule* labels, const std::vector<core::HexCoord>* productOrigins,
    bool postCalcification, std::vector<int>* productEnds, bool singleProduct,
    std::size_t finiteProducts, const SupplyPlan* supplied, const io::PuzzleData* validationPuzzle,
    int feedSpacing, bool retimeEmptyResets, bool boundEscapeStation, bool closeFeedBank,
    bool componentRows, std::vector<SharedFactoryEmission>* emissions,
    const std::vector<ProductAtomRef>* owners, bool animismus,
    const io::PuzzleData* reactionAssembly, const MixedInventoryProgram* mixedInventory,
    std::chrono::steady_clock::time_point deadline,
    const std::function<std::chrono::steady_clock::time_point()>& clock,
    std::vector<FragmentCustodyEvent>* custody, RowCompletionTransfer* completionTransfer,
    bool requireRetimingOverlap, bool allowCompactLayout, bool parallelClocks,
    int compactGapOverride) {
    auto checkDeadline = [&] {
        if ((clock ? clock() : std::chrono::steady_clock::now()) >= deadline)
            throw std::invalid_argument("row assembly deadline");
    };
    checkDeadline();
    if (reactionAssembly && !completionTransfer && !productOrigins)
        throw std::invalid_argument("legacy reaction assembly requires product origins");
    const bool coproductTransfer = completionTransfer && completionTransfer->coproductAssembly;
    const bool surplusRetention = completionTransfer && completionTransfer->surplusRetention;
    const bool wheelSource = completionTransfer && completionTransfer->wheelSecondSingletonSource;
    if (wheelSource && (!completionTransfer->secondSingletonSource ||
                        !(p.permissionBits & (1ULL << 13)) || !(p.permissionBits & (1ULL << 28))))
        throw std::invalid_argument("wheel second source requires real two-source dispatch and "
                                    "wheel/duplication permissions");
    const bool secondSource = completionTransfer && completionTransfer->secondSingletonSource;
    core::AtomType wheelElement = core::AtomType::Salt;
    int wheelOrientation = 0;
    std::size_t cardinalInput = 0;
    if (secondSource) {
        if (!coproductTransfer || p.inputs.size() != 2)
            throw std::invalid_argument("second source requires bounded coproduct dispatch");
        int cardinal = 0, mercury = 0;
        for (std::size_t i = 0; i < p.inputs.size(); ++i) {
            const auto& input = p.inputs[i];
            if (input.atoms.size() != 1 || !input.bonds.empty())
                throw std::invalid_argument("second source requires singleton inputs");
            const auto type = input.atoms[0].type;
            if (type >= core::AtomType::Air && type <= core::AtomType::Water) {
                ++cardinal;
                cardinalInput = i;
            } else if (type == core::AtomType::Quicksilver)
                ++mercury;
        }
        if (cardinal != 1 || mercury != 1)
            throw std::invalid_argument("second source requires cardinal and Quicksilver");
    }
    if (surplusRetention && (!coproductTransfer || !std::getenv("OPUSOPTIMA_FINITE_RETENTION") ||
                             std::string(std::getenv("OPUSOPTIMA_FINITE_RETENTION")) != "1"))
        throw std::invalid_argument(
            "surplus retention requires coproduct and finite retention opt-in");
    const auto& materialPuzzle = coproductTransfer && reactionAssembly ? *reactionAssembly : p;
    if (completionTransfer) {
        if (completionTransfer->mode != io::VerificationMode::LegacyGate ||
            !completionTransfer->nativeGate)
            throw std::invalid_argument("completion transfer requires explicit native-only gate");
        if (!finiteProducts || singleProduct || sharedOutputs || labels || productOrigins ||
            supplied || validationPuzzle || retimeEmptyResets || boundEscapeStation ||
            closeFeedBank || componentRows || animismus ||
            (reactionAssembly && !coproductTransfer) || mixedInventory ||
            p.inputs.size() != (secondSource ? 2u : 1u) || p.outputs.size() != 1 ||
            p.inputs.front().atoms.size() != 1 || !p.inputs.front().bonds.empty())
            throw std::invalid_argument(
                "completion transfer requires finite reaction-free singleton rows");
        if (coproductTransfer) {
            if (!reactionAssembly || finiteProducts != 6 || !(p.permissionBits & (1ULL << 16)) ||
                (secondSource && (materialPuzzle.permissionBits != p.permissionBits)) ||
                materialPuzzle.inputs.size() != p.inputs.size())
                throw std::invalid_argument(
                    "coproduct transfer requires matching real source and six products");
            for (std::size_t i = 0; i < p.inputs.size(); ++i) {
                const auto& actual = materialPuzzle.inputs[i];
                const auto& proxy = p.inputs[i];
                if (actual.atoms.size() != 1 || actual.atoms[0].type != proxy.atoms[0].type ||
                    actual.atoms[0].position != proxy.atoms[0].position || !actual.bonds.empty())
                    throw std::invalid_argument(
                        "coproduct transfer source index/type/pose mismatch");
            }
            int balance = 0, quicksilverDemand = 0, wheelDemand = 0;
            if (materialPuzzle.version != p.version || materialPuzzle.productionInfo ||
                materialPuzzle.outputMultiplier != p.outputMultiplier ||
                materialPuzzle.outputs.size() != 1 || materialPuzzle.outputs[0].atoms.size() > 16 ||
                materialPuzzle.outputs[0].atoms.size() != p.outputs[0].atoms.size() ||
                materialPuzzle.outputs[0].bonds.size() != p.outputs[0].bonds.size())
                throw std::invalid_argument("coproduct transfer target bound");
            for (std::size_t i = 0; i < p.outputs[0].bonds.size(); ++i) {
                const auto& actual = materialPuzzle.outputs[0].bonds[i];
                const auto& proxy = p.outputs[0].bonds[i];
                if (actual.from != proxy.from || actual.to != proxy.to || actual.bits != proxy.bits)
                    throw std::invalid_argument("coproduct transfer proxy topology mismatch");
            }
            for (std::size_t i = 0; i < p.outputs[0].atoms.size(); ++i) {
                const auto type = materialPuzzle.outputs[0].atoms[i].type;
                const bool direct = secondSource && type == core::AtomType::Quicksilver;
                const bool derived =
                    wheelSource && type >= core::AtomType::Air && type <= core::AtomType::Water;
                if (p.outputs[0].atoms[i].type != (direct ? type : core::AtomType::Salt) ||
                    p.outputs[0].atoms[i].position != materialPuzzle.outputs[0].atoms[i].position ||
                    (!direct && type != core::AtomType::Vitae && type != core::AtomType::Mors &&
                     !derived))
                    throw std::invalid_argument(
                        "coproduct transfer requires aligned typed proxy and mixed target");
                if (direct)
                    ++quicksilverDemand;
                else if (derived) {
                    if (wheelElement != core::AtomType::Salt && wheelElement != type)
                        throw std::invalid_argument(
                            "wheel second source requires one cardinal identity");
                    wheelElement = type;
                    ++wheelDemand;
                } else
                    balance += type == core::AtomType::Vitae ? 1 : -1;
            }
            if (wheelSource &&
                (wheelDemand == 0 || quicksilverDemand == 0 || !surplusRetention || balance != -1))
                throw std::invalid_argument(
                    "wheel second source requires cardinal/Q and one retained Vitae per product");
            if (wheelSource) {
                core::HexCoord catalyst{};
                switch (wheelElement) {
                case core::AtomType::Air:
                    catalyst = {-1, 1};
                    break;
                case core::AtomType::Earth:
                    catalyst = {1, -1};
                    break;
                case core::AtomType::Fire:
                    catalyst = {0, -1};
                    break;
                case core::AtomType::Water:
                    catalyst = {0, 1};
                    break;
                default:
                    throw std::invalid_argument("wheel second source requires a cardinal target");
                }
                while (catalyst != core::HexCoord{0, 1} && wheelOrientation < 6) {
                    catalyst = core::RotateClockwise(catalyst);
                    ++wheelOrientation;
                }
                if (wheelOrientation == 6)
                    throw std::invalid_argument("wheel catalyst cannot reach fixed contact");
            }
            if (secondSource && !wheelSource &&
                (quicksilverDemand != 1 || !surplusRetention ||
                 materialPuzzle.outputs[0].atoms.size() != 4 || balance != -1))
                throw std::invalid_argument(
                    "second source requires registered V1/M2/Q1 retention demand");
            if ((surplusRetention ? balance != -1 : balance != 0) || p.outputs[0].atoms.empty())
                throw std::invalid_argument(
                    "coproduct transfer requires registered balanced or one-Vitae-surplus demand");
        }
        const auto sourceType = p.inputs[cardinalInput].atoms.front().type;
        const bool converted = std::any_of(
            p.outputs.front().atoms.begin(), p.outputs.front().atoms.end(), [&](const auto& atom) {
                return atom.type != sourceType &&
                       !(secondSource && atom.type == core::AtomType::Quicksilver);
            });
        if (converted && (p.outputs.front().atoms.size() > 16 ||
                          !std::all_of(p.outputs.front().atoms.begin(),
                                       p.outputs.front().atoms.end(), [&](const auto& atom) {
                                           return atom.type == core::AtomType::Salt ||
                                                  (secondSource &&
                                                   atom.type == core::AtomType::Quicksilver);
                                       })))
            throw std::invalid_argument(
                "completion transfer chemistry requires at most sixteen Salt atoms");
        for (const auto& atom : p.outputs.front().atoms)
            if (!(secondSource && atom.type == core::AtomType::Quicksilver) &&
                atom.type != sourceType &&
                !(sourceType >= core::AtomType::Air && sourceType <= core::AtomType::Water &&
                  atom.type == core::AtomType::Salt && (p.permissionBits & (1ULL << 12))))
                throw std::invalid_argument(
                    "completion transfer supports only Direct/Calcification singleton supply");
        completionTransfer->candidates.clear();
        completionTransfer->events.clear();
        completionTransfer->reactions.clear();
        completionTransfer->boundaries.clear();
        completionTransfer->ledger = {};
        completionTransfer->failure.clear();
        completionTransfer->published = false;
        completionTransfer->retention = {{6}, {}, 6};
        completionTransfer->retentionTargets.clear();
    }
    const auto* publishedMixed = mixedInventory;
    MixedInventoryProgram stagedMixed;
    if (mixedInventory) {
        stagedMixed = *mixedInventory;
        mixedInventory = &stagedMixed;
    }
    auto* publishedEmissions = emissions;
    auto* publishedEnds = productEnds;
    auto* publishedCustody = custody;
    std::vector<FragmentCustodyEvent> stagedCustody;
    if (custody) custody = &stagedCustody;
    std::vector<SharedFactoryEmission> stagedEmissions;
    std::vector<int> stagedEnds;
    if (emissions) emissions = &stagedEmissions;
    if (productEnds) productEnds = &stagedEnds;
    constexpr auto permissions = (1ULL << 2) | (1ULL << 3);
    if ((p.permissionBits & permissions) != permissions)
        throw std::invalid_argument("row assembly requires piston/track permissions");
    auto validation = validationPuzzle ? *validationPuzzle : p;
    if (finiteProducts && !(p.permissionBits & (1ULL << 17)))
        validation.permissionBits |= 1ULL << 17;
    AssemblyPlanner::Validate(validation, plan, productOrigins != nullptr || animismus, deadline);
    const bool proxySupply =
        labels &&
        !std::equal(p.outputs[0].atoms.begin(), p.outputs[0].atoms.end(), labels->atoms.begin(),
                    [](const auto& proxy, const auto& final) { return proxy.type == final.type; });
    bool boundedWaste = false;
    const auto supply = [&] {
        if (supplied) return *supplied;
        try {
            return proxySupply
                       ? PlanProxySupply(p, deadline)
                       : AssemblyPlanner::PlanSupply(p, productOrigins != nullptr, 20000, deadline);
        } catch (const std::invalid_argument&) {
            checkDeadline();
            if ((productEnds && !finiteProducts) || proxySupply || productOrigins ||
                sharedOutputs || p.productionInfo || (p.permissionBits & (1ULL << 17)))
                throw;
            auto storagePlan = p;
            // Disposal is a planning operation here; the compiler parks every surplus atom.
            storagePlan.permissionBits |= 1ULL << 17;
            if (finiteProducts) {
                for (std::size_t products : {6U, 3U, 2U}) {
                    if (finiteProducts % products) continue;
                    InventorySearchLimits limits;
                    limits.deadline = deadline;
                    auto inventory = SearchSupplyInventory(storagePlan, products, limits);
                    if (inventory.status == InventorySearchStatus::Found) {
                        boundedWaste = true;
                        return inventory.plan;
                    }
                }
            }
            auto planned = AssemblyPlanner::PlanSupply(storagePlan, false, 20000, deadline);
            boundedWaste = true;
            return planned;
        }
    }();
    if (std::any_of(supply.steps.begin(), supply.steps.end(), [](const auto& step) {
            return step.kind == SupplyPlan::Kind::Animismus ||
                   step.kind == SupplyPlan::Kind::Coproduct;
        }))
        throw std::invalid_argument("released coproduct supply requires the inventory backend");
    std::size_t supplyIndex = 0;
    const auto& target = p.outputs[0];
    std::vector<core::HexCoord> cells;
    int minQ = 128, minR = 128, maxQ = -128, maxR = -128;
    for (const auto& a : target.atoms) {
        checkDeadline();
        minQ = std::min(minQ, a.position.q);
        minR = std::min(minR, a.position.r);
        maxQ = std::max(maxQ, a.position.q);
        maxR = std::max(maxR, a.position.r);
    }
    const int width = maxQ - minQ + 1, height = maxR - minR + 1;
    for (const auto& a : target.atoms)
        cells.push_back({a.position.q - minQ, a.position.r - minR});
    auto edge = [&](int a, int dx, int dy) {
        for (const auto& b : target.bonds) {
            checkDeadline();
            auto from = core::Subtract(b.from, {minQ, minR});
            auto to = core::Subtract(b.to, {minQ, minR});
            auto next = core::Add(cells[a], {dx, dy});
            if ((from == cells[a] && to == next) || (to == cells[a] && from == next)) return true;
        }
        return false;
    };
    bool diagonal60 = false, diagonal120 = false;
    for (int i = 0; i < static_cast<int>(cells.size()); ++i) {
        checkDeadline();
        diagonal60 |= edge(i, 0, 1);
        diagonal120 |= edge(i, -1, 1);
    }
    if (componentRows) {
        if (sharedOutputs || labels || productOrigins || supplied || validationPuzzle ||
            finiteProducts || singleProduct || p.productionInfo || p.outputs.size() != 1 ||
            width < 2 || width > 8 || height < 1 || height > 2 ||
            cells.size() != static_cast<std::size_t>(width * height) ||
            std::any_of(p.inputs.begin(), p.inputs.end(),
                        [&](const auto& input) {
                            return input.atoms.size() != 1 || !input.bonds.empty();
                        }) ||
            std::any_of(target.bonds.begin(), target.bonds.end(),
                        [](const auto& bond) { return bond.bits != 1; }) ||
            std::any_of(supply.steps.begin(), supply.steps.end(), [](const auto& step) {
                return step.kind != SupplyPlan::Kind::Take ||
                       step.conversion != ElementConversion::None;
            }))
            throw std::invalid_argument(
                "component rows require direct singleton supply and full bounded ordinary rows");
        for (int atom = 0; atom < static_cast<int>(cells.size()); ++atom)
            if (cells[atom].q + 1 < width && !edge(atom, 1, 0))
                throw std::invalid_argument("component row has an unjoined horizontal boundary");
    }
    const int componentOffset = componentRows ? width + 8 : 0;
    io::SolutionData result;
    result.puzzleName = p.name;
    result.solutionName = "Serial row assembly";
    auto object = [&](const char* type, core::HexCoord pos, int length, int rotation,
                      int index = 0) {
        io::SolutionObject obj;
        obj.typeName = type;
        obj.position = pos;
        obj.bodyData = {length, rotation, index, 0, static_cast<int>(result.objects.size())};
        result.objects.push_back(obj);
        return static_cast<int>(result.objects.size()) - 1;
    };
    auto track = [&](core::HexCoord pos, int steps) {
        int id = object("track", pos, 1, 0);
        auto& data = result.objects[id].bodyData;
        data = {1, 0, 0, 0, steps + 1};
        for (int x = 0; x <= steps; ++x) {
            checkDeadline();
            data.push_back(x);
            data.push_back(0);
        }
        data.push_back(0);
    };
    std::vector<int> lower, upper;
    if (!sharedOutputs) {
        for (int x = 0; x < width; ++x)
            lower.push_back(object("piston", {-width + x + 1 - componentOffset, -2}, 2, 1));
        for (int x = 0; x < width; ++x)
            upper.push_back(object("piston", {x + 2, -1}, 2, 1));
        std::set<int> masks;
        if (labels)
            for (const auto& bond : labels->bonds)
                if (bond.bits != 1) masks.insert(bond.bits);
        const int extraTrack = labels ? 3 * (static_cast<int>(masks.size()) + 1) * (width + 2) : 0;
        track({1 - width - componentOffset, -2}, 4 * width - 1 + extraTrack + componentOffset);
        track({2, -1}, 3 * width - 2 + extraTrack);
        object("bonder", {-componentOffset, 0}, 1, 0);
    }
    int station = 2;
    if (diagonal60) {
        station += width;
        object("bonder", {station, 0}, 1, 1);
    } else
        ++station;
    if (diagonal120) {
        station += width;
        object("bonder", {station, 0}, 1, 2);
    }
    struct Contact {
        int distance, dx, dy, bits;
    };
    std::vector<Contact> contacts;
    if (labels) {
        std::set<int> masks;
        for (const auto& bond : labels->bonds)
            if (bond.bits != 1) masks.insert(bond.bits);
        std::vector<int> colors(masks.begin(), masks.end());
        colors.push_back(0);
        int cursor = (diagonal60 ? width : 1) + (diagonal120 ? width - 1 : 1);
        for (int bits : colors)
            for (const auto delta :
                 {core::HexCoord{-1, 0}, core::HexCoord{0, 1}, core::HexCoord{-1, 1}}) {
                cursor += width + 2;
                contacts.push_back({cursor, delta.q, delta.r, bits});
                const core::HexCoord base{2 + cursor + width - 1, 0};
                const auto other = core::Add(base, delta);
                int direction = 0;
                auto unit = core::HexCoord{1, 0};
                while (unit != delta) {
                    checkDeadline();
                    unit = core::RotateClockwise(unit);
                    ++direction;
                }
                // Ordinary bonds protect the other triangle edges from prisma.
                // Remove only the requested edge, then restore its required colors.
                // Later cleanup contacts remove incidental absent edges.
                object("unbonder", base, 1, direction);
                if (bits) {
                    // Keep each prism on the same triangle, selecting only glyphs
                    // whose colored edge coincides with this contact's target edge.
                    const auto prism = [&](core::HexCoord origin, int rotation) {
                        auto rotate = [&](core::HexCoord cell) {
                            for (int i = 0; i < rotation; ++i)
                                cell = core::RotateClockwise(cell);
                            return core::Add(origin, cell);
                        };
                        const core::HexCoord vertices[] = {rotate({0, 0}), rotate({0, 1}),
                                                           rotate({1, 0})};
                        const int colors[] = {8, 2, 4};
                        for (int i = 0; i < 3; ++i) {
                            checkDeadline();
                            const auto a = vertices[i], b = vertices[(i + 1) % 3];
                            if ((bits & colors[i]) &&
                                ((a == base && b == other) || (a == other && b == base)))
                                object("bonder-prisma", origin, 1, rotation);
                        }
                    };
                    if (delta.r == 0) {
                        prism(base, 3);
                        prism(other, 5);
                        prism(core::Add(base, {0, -1}), 1);
                    } else {
                        const core::HexCoord origin{std::min(base.q, other.q), 0};
                        prism(origin, 0);
                        prism(core::Add(origin, {1, 0}), 2);
                        prism(core::Add(origin, {0, 1}), 4);
                    }
                }
            }
    }
    const int reactionQ = station + width + 6;
    int vitaeArm = -1, morsArm = -1;
    if (animismus || reactionAssembly) {
        object("glyph-life-and-death", {reactionQ, 0}, 1, 0);
        vitaeArm = object("arm1", {reactionQ, 2}, 1, 4);
        morsArm = reactionAssembly ? object("arm1", {reactionQ + 2, -1}, 1, 3)
                                   : object("arm1", {reactionQ + 1, -2}, 1, 1);
        if (!reactionAssembly)
            for (std::size_t i = 0; i < sharedOutputs->outputs.size(); ++i) {
                checkDeadline();
                const bool vitae = sharedOutputs->outputs[i].atoms[0].type == core::AtomType::Vitae;
                const auto cell =
                    vitae ? core::HexCoord{reactionQ + 1, 1} : core::HexCoord{reactionQ + 2, -2};
                object("out-std", core::Subtract(cell, sharedOutputs->outputs[i].atoms[0].position),
                       1, 0, static_cast<int>(i));
            }
    }
    if ((reactionAssembly && !completionTransfer) || (!animismus && productOrigins)) {
        for (std::size_t i = 0; i < productOrigins->size(); ++i)
            object("out-std", core::Add({2 - minQ, 1 - minR}, (*productOrigins)[i]), 1, 0,
                   static_cast<int>(i));
    } else if (!sharedOutputs && !animismus && !completionTransfer)
        object("out-std", {2 - minQ, 1 - minR}, 1, 0);
    else if (!sharedOutputs && !animismus && completionTransfer) {
        // Output placement is deferred until the live completion boundary is inspected.
    } else if (!animismus)
        for (std::size_t i = 0; i < sharedOutputs->outputs.size(); ++i) {
            checkDeadline();
            const auto origin = sharedOutputs->outputs[i].atoms[0].position;
            object("out-std", {station + width + 6 + static_cast<int>(i) * 4 - origin.q, -origin.r},
                   1, 0, static_cast<int>(i));
        }
    if (closeFeedBank &&
        (sharedOutputs || labels || productOrigins || supplied || validationPuzzle ||
         finiteProducts || singleProduct || width > 16 || height > 16))
        throw std::invalid_argument("close feed bank outside bounded ordinary row scope");
    // Opt-in compact row layout: geometry only, custody unchanged. Inside the same
    // bounded ordinary row scope that already permits the close feed bank, the
    // station gap and the per-buffer feed stride contract together, so the feed
    // bank, the conveyor runway and the serpentine lose cells while every station
    // keeps its relative offset and every pickup/drop clearance still comes from
    // the same slot distances. Anything outside that scope, or any flag value
    // other than exactly "on", compiles the current default machine byte for byte.
    const bool expandedFeeds =
        std::any_of(p.inputs.begin(), p.inputs.end(), [&](const auto& input) {
            const auto layout = PlanExtractionLayout(input);
            int low = 256, high = -256;
            for (auto c : layout.cells) {
                checkDeadline();
                low = std::min(low, c.r);
                high = std::max(high, c.r);
            }
            return high - low > 2;
        });
    const bool compactLayout =
        allowCompactLayout &&
        [] {
            const char* flag = std::getenv("OPUSOPTIMA_COMPACT_LAYOUT");
            // Default-on for the explicit scheduled-row path; explicit off restores
            // the legacy geometry. Unknown values also keep the legacy geometry.
            return !flag || std::string_view(flag) == "on";
        }() &&
        !sharedOutputs && !labels && !productOrigins && !supplied && !validationPuzzle &&
        !finiteProducts && !singleProduct && !componentRows && !animismus && !reactionAssembly &&
        !mixedInventory && !emissions && !owners && !custody && !completionTransfer &&
        !expandedFeeds && p.outputs.size() == 1 && width <= 16 && height <= 16;
    // Opt-in local source extraction: one piston per machine walks a short rail
    // above the source rows and drops each atom on the shared hand-off cell, so
    // the shared serpentine feeder is no longer the critical path. Only the plain
    // narrow ordinary row is eligible. The source-shape test runs before any
    // geometry is emitted: an unsupported source must fall back to the shared
    // feeder instead of abandoning the whole candidate.
    //
    // Measured 2026-11-11: widening the intake past the compact layout
    // (`allowCompactLayout == false`) gives 14 puzzles 10-25% lower cost and
    // shorter periods (b2c42099 755->635, cd9b4ea9 7610->1620), but it also parks
    // the serpentine off-board, so when the intake machine is rejected by the gate
    // the whole stage is lost: 31 of 146 puzzles stopped producing. It therefore
    // only runs inside `RowIntakeModeScope(2)`, i.e. as a sibling candidate that is
    // compiled and gated next to the plain machine and never replaces it.
    const bool widenedIntake = g_intakeMode == 2;
    const bool directInputRequested = (g_intakeMode != 0 ? g_intakeMode > 0 : [] {
        const char* flag = std::getenv("OPUSOPTIMA_DIRECT_INPUT");
        return !flag || std::string_view(flag) != "off";
    }()) && (compactLayout || widenedIntake) && p.inputs.size() <= (widenedIntake ? 8U : 4U) &&
        std::none_of(supply.steps.begin(), supply.steps.end(), [](const auto& step) {
            return step.kind == SupplyPlan::Kind::Store ||
                   step.kind == SupplyPlan::Kind::Purify ||
                   step.kind == SupplyPlan::Kind::Animismus ||
                   step.kind == SupplyPlan::Kind::Coproduct ||
                   step.kind == SupplyPlan::Kind::Reagent ||
                   step.conversion != ElementConversion::None;
        });
    const bool directInput = directInputRequested && [&] {
        for (const auto& input : p.inputs) {
            const auto layout = PlanExtractionLayout(input);
            int lowRow = 256, highRow = -256;
            std::set<int> columns;
            for (const auto& atom : input.atoms) {
                auto cell = atom.position;
                for (int n = 0; n < layout.rotation; ++n)
                    cell = core::RotateClockwise(cell);
                lowRow = std::min(lowRow, cell.r);
                highRow = std::max(highRow, cell.r);
                columns.insert(cell.q);
            }
            // The intake rail sits on row 3 and reaches three cells, so the source
            // may span three rows and must have one atom per column. The widened
            // intake addresses every atom by its own (column, row), so a stacked
            // column is a sequence of pickups rather than a separate mechanism: the
            // arm walks to the column once and extends to each row. Pickups must run
            // bottom-up, because the extending shaft crosses the cells it does not
            // grab and leans on the gate to reject a top-down order.
            if (highRow - lowRow > 2) return false;
            if (columns.size() != input.atoms.size() && !widenedIntake) return false;
        }
        return true;
    }();
    g_intakeApplied = directInput;
    if (std::getenv("OPUSOPTIMA_DEBUG_INTAKE")) {
        std::fprintf(stderr, "[intake] clocks=%d mode=%d direct=%d compact=%d inputs=%zu\n",
                     int(parallelClocks), g_intakeMode, int(directInput), int(compactLayout),
                     p.inputs.size());
        for (const auto& input : p.inputs) {
            const auto layout = PlanExtractionLayout(input);
            int lowRow = 256, highRow = -256;
            std::set<int> columns;
            for (const auto& atom : input.atoms) {
                auto cell = atom.position;
                for (int n = 0; n < layout.rotation; ++n)
                    cell = core::RotateClockwise(cell);
                lowRow = std::min(lowRow, cell.r);
                highRow = std::max(highRow, cell.r);
                columns.insert(cell.q);
            }
            std::fprintf(stderr, "[intake]   src atoms=%zu rows=%d..%d cols=%zu\n",
                         input.atoms.size(), lowRow, highRow, columns.size());
        }
    }
    // Measurement-only overrides; they are read only when the layout flag is on.
    const int compactGap = [&] {
        if (compactLayout && compactGapOverride >= 0) return std::clamp(compactGapOverride, 0, 8);
        const char* value = compactLayout ? std::getenv("OPUSOPTIMA_COMPACT_LAYOUT_GAP") : nullptr;
        if (!value) return 2;
        return std::clamp(std::atoi(value), 0, 8);
    }();
    const int compactStride = [&] {
        const char* value =
            compactLayout ? std::getenv("OPUSOPTIMA_COMPACT_LAYOUT_STRIDE") : nullptr;
        if (!value) return 1;
        return std::clamp(std::atoi(value), 1, 3);
    }();
    const int compactSpacing = [&] {
        const char* value =
            compactLayout ? std::getenv("OPUSOPTIMA_COMPACT_LAYOUT_SPACING") : nullptr;
        if (!value) return std::min(2, feedSpacing);
        return std::clamp(std::atoi(value), 1, feedSpacing);
    }();
    // Extra idle ticks between two consecutive row periods on the local-intake
    // machine. The shared feeder compresses its drops so cheaply that the row
    // arms lose the inter-block separation they relied on, which shows up as a
    // motion-phase collision on wide rows; this is the measurement knob for the
    // minimum separation the row program actually needs.
    const int intakeGap = [&] {
        const char* value = std::getenv("OPUSOPTIMA_ROW_INTAKE_GAP");
        if (!value) return 0;
        return std::clamp(std::atoi(value), 0, 16);
    }();
    // Extra idle ticks between two consecutive row blocks of the local-intake
    // machine. Compressing the extraction pulls row N+1 earlier than the row
    // arms can tolerate, so the separation the row program needs has to be
    // measured rather than inherited from the serpentine feeder.
    const int intakeBlockGap = [&] {
        const char* value = std::getenv("OPUSOPTIMA_ROW_INTAKE_BLOCK_GAP");
        if (!value) return 0;
        return std::clamp(std::atoi(value), 0, 32);
    }();
    // Distance from the feed bank to the next source station; the compact layout
    // keeps one empty cell less than the caller's spacing inside the same scope.
    const int sourceSpacing = compactLayout ? compactSpacing : feedSpacing;
    const int feedOffset = width + (compactLayout ? compactGap : closeFeedBank ? 4 : 8);
    const int feedArm = object("piston", {-feedOffset, 3}, 1, 4);
    const int feedTrack = object("track", {-feedOffset, 3}, 1, 0);
    struct Slot {
        int input, atom, distance, extensions;
        // Direct-input source cell of the atom, in machine coordinates.
        int column{0}, row{0};
    };
    std::vector<Slot> slots;
    int directCursor = -feedOffset - 3;
    int directMinColumn = -feedOffset - 3;
    int intakeIndex = 0;
    // Caches sit on r=6, above the retracted feeder's r=4 traversal.
    // Keep two empty cells before the source stations and their unbonders.
    int feedReach =
        mixedInventory && mixedInventory->cacheCapacity ? mixedInventory->cacheCapacity + 2 : 0;
    struct WideFeed {
        int input, courier, batch, transferDistance, top, startDistance;
        std::vector<int> arms, distances, lifts;
    };
    std::vector<WideFeed> wideFeeds;
    int wideReach = 0;
    int commonTop = 14, courierStart = -feedOffset - 12;
    if (expandedFeeds)
        for (const auto& input : p.inputs) {
            checkDeadline();
            const auto layout = PlanExtractionLayout(input);
            int lowQ = 256, highQ = -256, lowR = 256, highR = -256;
            for (auto c : layout.cells) {
                checkDeadline();
                lowQ = std::min(lowQ, c.q);
                highQ = std::max(highQ, c.q);
                lowR = std::min(lowR, c.r);
                highR = std::max(highR, c.r);
            }
            commonTop = std::max(commonTop, 14 + highR - lowR);
            courierStart -= 2 * (highQ - lowQ) + 12;
        }
    int sharedCourier = -1, sharedTransfer = 0;
    if (expandedFeeds) {
        sharedCourier = object("piston", {courierStart, commonTop + 5}, 3, 4);
        const int route = object("track", {courierStart, commonTop + 5}, 1, 0);
        const int horizontal = -feedOffset + 6 - courierStart;
        sharedTransfer = horizontal + commonTop + 8;
        auto& data = result.objects[route].bodyData;
        data = {1, 0, 0, 0, sharedTransfer + 1};
        for (int q = 0; q <= horizontal; ++q)
            data.insert(data.end(), {q, 0});
        for (int r = 1; r <= commonTop + 2; ++r)
            data.insert(data.end(), {horizontal, -r});
        for (int q = 1; q <= 6; ++q)
            data.insert(data.end(), {horizontal - q, -commonTop - 2});
        data.push_back(0);
    }
    for (int i = 0; i < static_cast<int>(p.inputs.size()); ++i) {
        checkDeadline();
        const auto& input = p.inputs[i];
        auto rotate = [](core::HexCoord c, int turns) {
            while (turns-- > 0)
                c = core::RotateClockwise(c);
            return c;
        };
        const auto layout = PlanExtractionLayout(input);
        const int turns = layout.rotation;
        int lo = 256, hi = -256, lowR = 256;
        for (const auto& a : input.atoms) {
            checkDeadline();
            auto c = rotate(a.position, turns);
            lo = std::min(lo, c.q);
            hi = std::max(hi, c.q);
            lowR = std::min(lowR, c.r);
        }
        int highR = lowR;
        for (auto c : layout.cells)
            highR = std::max(highR, c.r);
        if (expandedFeeds) {
            checkDeadline();
            // Static unbonding leaves each column accessible from above. Track
            // travel, rather than piston stroke, supplies arbitrary pickup depth.
            // The final atom exits underneath the regenerated input instead.
            const int span = hi - lo;
            const int top = commonTop;
            const int right = -feedOffset - 12 - wideReach;
            const int shift = span + 4;
            const core::HexCoord work{right - hi, 14 - lowR};
            const auto source = core::Subtract(work, {shift, 0});
            object("input", source, 1, turns, i);

            const int escapeStart = source.q + lo - 3;
            const int batch = object("piston", {escapeStart, 11}, 1, 1);
            const int escapeRoute = object("track", {escapeStart, 11}, 1, 0);
            auto& escapeRouteData = result.objects[escapeRoute].bodyData;
            escapeRouteData = {1, 0, 0, 0, span + top - 6};
            for (int r = top - 10; r >= 1; --r)
                escapeRouteData.insert(escapeRouteData.end(), {0, r});
            for (int q = 0; q <= span + 3; ++q)
                escapeRouteData.insert(escapeRouteData.end(), {q, 0});
            escapeRouteData.push_back(0);
            const int sourceQ = source.q + lo - 3;
            const int courier = sharedCourier;
            WideFeed wide{i,  courier, batch, sharedTransfer, top, sourceQ - courierStart,
                          {}, {},      {}};
            std::map<int, int> columns;
            for (int j = 0; j < static_cast<int>(layout.cells.size()); ++j) {
                checkDeadline();
                const auto c = core::Add(source, layout.cells[j]);
                if (!columns.count(c.q)) {
                    const int arm = object("piston", {c.q, top + 3}, 3, 4);
                    columns[c.q] = arm;
                    const int rail = object("track", {c.q, top + 3}, 1, 0);
                    auto& railData = result.objects[rail].bodyData;
                    railData = {1, 0, 0, 0, top - 14 + 1};
                    for (int r = 0; r <= top - 14; ++r)
                        railData.insert(railData.end(), {0, -r});
                    railData.push_back(0);
                }
                wide.arms.push_back(columns[c.q]);
                wide.distances.push_back(c.q - courierStart);
                wide.lifts.push_back(top - c.r);
                slots.push_back({i, j, 0, 0});
            }
            for (const auto& b : input.bonds) {
                checkDeadline();
                const auto a = core::Add(source, rotate(b.from, turns));
                const auto c = core::Add(source, rotate(b.to, turns));
                auto direction = core::HexCoord{1, 0};
                int rotation = 0;
                while (direction != core::Subtract(c, a)) {
                    checkDeadline();
                    direction = core::RotateClockwise(direction);
                    ++rotation;
                }
                object("unbonder", a, 1, rotation);
            }
            // Severing any real bond clears the input's implicit disjoint links as well.
            if (input.atoms.size() > 1 && input.bonds.empty())
                throw std::invalid_argument(
                    "wide extraction requires a real bond to release implicit disjoint links");
            wideFeeds.push_back(wide);
            wideReach += 2 * span + 12;
            continue;
        }
        if (directInput) {
            int highR2 = lowR;
            for (const auto& a : input.atoms)
                highR2 = std::max(highR2, rotate(a.position, turns).r);
            if (highR2 - lowR > 2)
                throw std::invalid_argument("direct input requires at most three source rows");
            std::set<int> atomColumns;
            for (const auto& a : input.atoms)
                atomColumns.insert(rotate(a.position, turns).q);
            if (atomColumns.size() != input.atoms.size() && !widenedIntake)
                throw std::invalid_argument("direct input requires one atom per source column");
            const core::HexCoord origin{directCursor - hi, -lowR};
            object("input", origin, 1, turns, i);
            for (int j = 0; j < static_cast<int>(input.atoms.size()); ++j) {
                checkDeadline();
                const auto c = core::Add(origin, rotate(input.atoms[j].position, turns));
                slots.push_back({i, j, 0, 0, c.q, c.r});
                directMinColumn = std::min(directMinColumn, c.q);
            }
            for (const auto& b : input.bonds) {
                checkDeadline();
                const auto a = core::Add(origin, rotate(b.from, turns));
                const auto c = core::Add(origin, rotate(b.to, turns));
                auto direction = core::HexCoord{1, 0};
                int rotation = 0;
                while (direction != core::Subtract(c, a)) {
                    checkDeadline();
                    direction = core::RotateClockwise(direction);
                    ++rotation;
                }
                object("unbonder", a, 1, rotation);
            }
            directCursor = origin.q + lo - 2;
            continue;
        }
        const int start = feedReach + sourceSpacing;
        const core::HexCoord origin{-feedOffset - start - hi, 4 - lowR};
        object("input", origin, 1, turns, i);
        for (int j = 0; j < static_cast<int>(input.atoms.size()); ++j) {
            checkDeadline();
            auto c = core::Add(origin, rotate(input.atoms[j].position, turns));
            slots.push_back({i, j, 5 + 2 * (-feedOffset - c.q), 6 - c.r});
        }
        for (const auto& b : input.bonds) {
            checkDeadline();
            auto a = core::Add(origin, rotate(b.from, turns));
            auto c = core::Add(origin, rotate(b.to, turns));
            auto direction = core::HexCoord{1, 0};
            int rotation = 0;
            while (direction != core::Subtract(c, a)) {
                checkDeadline();
                direction = core::RotateClockwise(direction);
                ++rotation;
            }
            // omsim a88d12d sim.c UNBONDING clears bond_direction's entire
            // BOND_LOW_BITS mask, including all three triplex colors. Every
            // feed edge is covered before any atom is lifted from the input.
            object("unbonder", a, 1, rotation);
        }
        feedReach = start + hi - lo;
    }
    std::vector<Slot> bufferSlots;
    int bufferCount = 0;
    for (const auto& step : supply.steps)
        bufferCount = std::max(bufferCount, step.buffer + 1);
    if (mixedInventory) {
        checkDeadline();
        for (const auto& step : mixedInventory->stores)
            bufferCount = std::max(bufferCount, step.buffer + 1);
        for (const auto& reaction : mixedInventory->reactions)
            for (int slot : reaction.outputs)
                bufferCount = std::max(bufferCount, slot + 1);
    }
    if (mixedInventory && mixedInventory->cacheCapacity && expandedFeeds)
        throw std::invalid_argument("near inventory cache requires narrow source stations");
    // Reagent buffers park on the serpentine's upper row and every slot stretches
    // the shared feeder rail by one stride unit on both sides (`5 + 2*feedReach`),
    // so the stride costs five per buffer per unit. The legacy distance of three
    // kept a full feeder cycle between two parked atoms; the narrow feeder clock
    // schedules the pickups, so a packed row separates them in time and pays for one
    // rail unit instead of three. Measured on the 146 reference puzzles: 10 improved,
    // 0 regressed, cost wins 61 -> 63 (e59c2d6b 1030 -> 880). The wide extraction
    // (`expandedFeeds`) keeps three: its courier reads the slot distance as clearance
    // and packs two parked atoms into a motion-phase collision
    // (`DisassemblyVerifierTest.WideExtractionInventoryBuffers`).
    // `OPUSOPTIMA_FEED_BUFFER_STRIDE=3` restores the legacy spacing everywhere.
    const int bufferStride = [&] {
        const char* value = std::getenv("OPUSOPTIMA_FEED_BUFFER_STRIDE");
        if (!value) return expandedFeeds ? 3 : 1;
        return std::clamp(std::atoi(value), 1, 3);
    }();
    if (expandedFeeds && bufferCount) feedReach += bufferStride;
    for (int b = 0; b < bufferCount; ++b) {
        checkDeadline();
        if (mixedInventory && mixedInventory->cacheCapacity) {
            bufferSlots.push_back({-1, b, 5 + 2 * (1 + mixedInventory->cacheSlots.at(b)), 0});
        } else {
            feedReach += compactLayout                                      ? compactStride
                         : mixedInventory && mixedInventory->compactBuffers ? 1
                                                                            : bufferStride;
            bufferSlots.push_back({-1, b, 5 + 2 * feedReach, 2});
        }
    }
    if (std::getenv("OPUSOPTIMA_DEBUG_INTAKE"))
        std::fprintf(stderr, "[feed] buffers=%d stride=%d feedReach=%d width=%d\n", bufferCount,
                     bufferStride, feedReach, width);
    auto& feedData = result.objects[feedTrack].bodyData;
    if (directInput) {
        result.objects[feedArm].position = {-feedOffset, -20};
        result.objects[feedTrack].position = {-feedOffset, -20};
        feedData = {1, 0, 0, 0, 2, 0, 0, 0, 1, 0};
    } else {
        feedData = {1, 0, 0, 0, 7 + 2 * feedReach};
        for (int r = 0; r <= 5; ++r) {
            checkDeadline();
            feedData.push_back(0);
            feedData.push_back(r);
        }
        for (int q = 1; q <= feedReach; ++q) {
            checkDeadline();
            feedData.push_back(-q);
            feedData.push_back(5);
            feedData.push_back(-q);
            feedData.push_back(4);
        }
        feedData.push_back(-feedReach - 1);
        feedData.push_back(5);
        feedData.push_back(0);
    }
    const bool wideOnly = expandedFeeds && bufferSlots.empty();
    if (expandedFeeds && !bufferSlots.empty()) {
        result.objects[feedArm].position.q -= 3;
        result.objects[feedArm].bodyData[1] = 5;
        result.objects[feedTrack].position.q -= 3;
    }
    if (wideOnly) {
        result.objects[feedArm].position = {-feedOffset, -20};
        result.objects[feedTrack].position = {-feedOffset, -20};
        result.objects[feedTrack].bodyData = {1, 0, 0, 0, 2, 0, 0, 0, 1, 0};
    }
    const bool disposal =
        std::any_of(supply.steps.begin(), supply.steps.end(),
                    [](const auto& step) { return step.kind == SupplyPlan::Kind::Discard; });
    if (disposal && !boundedWaste && !(p.permissionBits & (1ULL << 17)))
        throw std::invalid_argument("disassembly inventory cleanup requires disposal permission");
    const bool purification =
        std::any_of(supply.steps.begin(), supply.steps.end(),
                    [](const auto& step) { return step.kind == SupplyPlan::Kind::Purify; });
    const bool duplication =
        std::any_of(supply.steps.begin(), supply.steps.end(), [](const auto& step) {
            return step.conversion == ElementConversion::Duplication;
        });
    const bool leftWaste = purification;
    const int wasteArm = disposal ? object("piston",
                                           leftWaste ? core::HexCoord{-feedOffset - 3, 0}
                                                     : core::HexCoord{-feedOffset + 3, 0},
                                           1, leftWaste ? 0 : 3)
                                  : -1;
    if (disposal && !boundedWaste)
        object("glyph-disposal",
               leftWaste ? core::HexCoord{-feedOffset - 2, 0} : core::HexCoord{-feedOffset + 2, 0},
               1, 0);
    if (disposal && boundedWaste) {
        const auto count =
            std::count_if(supply.steps.begin(), supply.steps.end(),
                          [](const auto& step) { return step.kind == SupplyPlan::Kind::Discard; }) *
            ((finiteProducts ? finiteProducts : 6 * p.outputMultiplier) / supply.productsPerPeriod);
        if (count > 1000) throw std::invalid_argument("bounded waste storage capacity exceeded");
        const int rail = object("track", result.objects[wasteArm].position, 1, 0);
        auto& data = result.objects[rail].bodyData;
        data = {1, 0, 0, 0, static_cast<int>(count * 3 + 13)};
        for (int r = 0; r <= count * 3 + 12; ++r)
            data.insert(data.end(), {0, r});
        data.push_back(0);
    }
    const int conveyor = componentRows ? -1 : object("piston", {-feedOffset, -3}, 2, 1);
    if (!componentRows)
        track({-feedOffset, -3},
              feedOffset + (sharedOutputs
                                ? station + width + 6 +
                                      static_cast<int>(sharedOutputs->outputs.size() - 1) * 4
                            : reactionAssembly ? reactionQ + 3
                                               : 0));
    // Local source intake: one piston on a short rail above the sources. Pistons
    // reach at most three cells, so the rail sits on row 3: extension 3 grabs row
    // 0, 2 grabs row 1 and 1 grabs row 2. It always travels retracted.
    int intakeArm = -1;
    if (directInput) {
        const int span = std::max(1, -feedOffset - directMinColumn);
        intakeArm = object("piston", {directMinColumn, 3}, 1, 4);
        const int rail = object("track", {directMinColumn, 3}, 1, 0);
        auto& data = result.objects[rail].bodyData;
        data = {1, 0, 0, 0, span + 1};
        for (int q = 0; q <= span; ++q)
            data.insert(data.end(), {q, 0});
        data.push_back(0);
    }
    int wheelDevice = -1;
    if (wheelSource) {
        wheelDevice = object("baron", {-feedOffset + 2, -7}, 1, wheelOrientation);
        object("glyph-duplication", {-feedOffset + 2, -6}, 1, 1);
    }
    int vitaeCourier = -1, morsCourier = -1;
    if (reactionAssembly) {
        vitaeCourier = object("piston", {reactionQ + 1, 3}, 2, 4);
        const int rail = object("track", {reactionQ + 1, 3}, 1, 0);
        result.objects[rail].bodyData = {1, 0, 0, 0, 3, 0, 0, 0, -1, 0, -2, 0};
        morsCourier = object("piston", {reactionQ + 2, -5}, 3, 1);
        const int lowerRail = object("track", {reactionQ + 2, -5}, 1, 0);
        result.objects[lowerRail].bodyData = {1, 0, 0, 0, 3, 0, 0, 1, 0, 1, 1, 0};
    }
    int retentionArm = -1;
    int retentionDistance = 0;
    auto fullBounds = [&]() {
        core::HexCoord maximum{std::numeric_limits<int>::min(), std::numeric_limits<int>::min()};
        auto include = [&](core::HexCoord cell) {
            maximum.q = std::max(maximum.q, cell.q);
            maximum.r = std::max(maximum.r, cell.r);
        };
        for (const auto& part : result.objects) {
            include(part.position);
            if (part.typeName == "track")
                for (int i = 0; i < part.bodyData.at(4); ++i)
                    include(core::Add(part.position,
                                      {part.bodyData.at(5 + 2 * i), part.bodyData.at(6 + 2 * i)}));
            if (part.typeName == "input")
                for (const auto& atom : p.inputs.at(part.bodyData.at(2)).atoms) {
                    auto cell = atom.position;
                    for (int n = 0; n < part.bodyData.at(1); ++n)
                        cell = core::RotateClockwise(cell);
                    include(core::Add(part.position, cell));
                }
        }
        return maximum;
    };
    if (purification) object("glyph-purification", {-feedOffset + 4, -1}, 1, 0);
    const bool chemistry =
        mixedInventory ||
        std::any_of(supply.steps.begin(), supply.steps.end(), [](const auto& step) {
            return step.conversion == ElementConversion::Calcification;
        });
    if (chemistry) object("glyph-calcification", {-feedOffset - 2, -1}, 1, 0);
    const bool projection =
        std::any_of(supply.steps.begin(), supply.steps.end(), [](const auto& step) {
            return step.conversion == ElementConversion::Projection;
        });
    if (projection) object("glyph-projection", {-feedOffset - 2, 0}, 1, 4);
    // omsim sim.c DUPLICATION: cardinal at (0,0) is unchanged; salt at (1,0) transforms,
    // including while grabbed. Return the borrowed cardinal as a product atom.
    if (duplication) object("glyph-duplication", {-feedOffset + 3, -6}, 1, 2);
    const int mercuryArm = duplication  ? object("arm1", {-feedOffset + 5, -6}, 2, 3)
                           : projection ? object("arm1", {-feedOffset, -2}, 2, 1)
                                        : -1;
    int catalystDistance = 0;
    if (duplication) {
        // Keep this track disconnected from the conveyor track at r=-3.
        const int catalystTrack = object("track", {-feedOffset + 5, -6}, 1, 0);
        std::vector<core::HexCoord> route{{0, 0}, {0, -1}, {0, -2}};
        for (int q = -1; q >= -9; --q)
            route.push_back({q, -2});
        for (int r = -1; r <= 7; ++r)
            route.push_back({-9, r});
        for (int q = -8; q <= -3; ++q)
            route.push_back({q, 7});
        route.push_back({-3, 6});
        catalystDistance = static_cast<int>(route.size()) - 1;
        auto& data = result.objects[catalystTrack].bodyData;
        data = {1, 0, 0, 0, static_cast<int>(route.size())};
        for (auto cell : route)
            data.insert(data.end(), {cell.q, cell.r});
        data.push_back(0);
    }
    const int escapePark = feedReach + 3;
    if (boundEscapeStation && (sharedOutputs || labels || productOrigins || supplied ||
                               validationPuzzle || finiteProducts || singleProduct ||
                               expandedFeeds || width > 16 || height > 16 || feedReach > 64))
        throw std::invalid_argument("bounded escape station outside ordinary narrow row scope");
    const bool needsEscape =
        !boundEscapeStation || std::any_of(slots.begin(), slots.end(), [&](const auto& slot) {
            return slot.input >= 0 && slot.extensions == 2 &&
                   std::any_of(slots.begin(), slots.end(), [&](const auto& other) {
                       return other.input == slot.input && other.extensions == 0;
                   });
        });
    int escapeArm = -1;
    if (needsEscape && !directInput) {
        escapeArm = object("piston", {-feedOffset - escapePark, 2}, 1, 1);
        const int escapeTrack = object("track", {-feedOffset - escapePark, 2}, 1, 0);
        auto& escapeData = result.objects[escapeTrack].bodyData;
        // Escape moves park -> park-distance -> park -> 0; no stage visits park+1/+2.
        const int end = escapePark + (boundEscapeStation ? 0 : 2);
        escapeData = {1, 0, 0, 0, end + 1};
        for (int q = 0; q <= end; ++q)
            escapeData.insert(escapeData.end(), {q, 0});
        escapeData.push_back(0);
    }
    std::vector<int> calcify;
    if (labels && postCalcification)
        for (int i = 0; i < static_cast<int>(cells.size()); ++i)
            if (target.atoms[i].type != labels->atoms[i].type) calcify.push_back(i);
    int finishingArm = -1;
    int finishingDistance = 0;
    if (!calcify.empty()) {
        checkDeadline();
        const auto anchor =
            *std::max_element(cells.begin(), cells.end(), [](auto a, auto b) { return a.r < b.r; });
        const core::HexCoord base{2 + anchor.q, height + 3};
        finishingArm = object("arm1", base, 3, 4);
        const int clearance = height + width + 6;
        const int remote = 16 * width + 40;
        const core::HexCoord glyph{remote + width + 4, clearance + height + 4};
        object("glyph-calcification", glyph, 1, 0);
        std::vector<core::HexCoord> translations{{0, 0}};
        for (int y = 1; y <= clearance; ++y)
            translations.push_back({0, y});
        for (int x = 1; x <= remote; ++x)
            translations.push_back({x, clearance});
        using Key = std::pair<int, int>;
        auto key = [](core::HexCoord c) { return Key{c.q, c.r}; };
        std::set<Key> forbidden, visited;
        for (int i = 0; i < static_cast<int>(cells.size()); ++i) {
            checkDeadline();
            const auto type = target.atoms[i].type;
            if (std::find(calcify.begin(), calcify.end(), i) == calcify.end() &&
                type >= core::AtomType::Air && type <= core::AtomType::Water)
                forbidden.insert(key(core::Subtract(glyph, core::Add(cells[i], {2, 1}))));
        }
        for (auto c : translations)
            visited.insert(key(c));
        for (int atom : calcify) {
            checkDeadline();
            const auto goal = key(core::Subtract(glyph, core::Add(cells[atom], {2, 1})));
            if (visited.count(goal)) continue;
            const auto start = key(translations.back());
            std::queue<Key> queue;
            std::map<Key, Key> previous;
            queue.push(start);
            previous[start] = start;
            while (!queue.empty() && !previous.count(goal)) {
                checkDeadline();
                const auto current = queue.front();
                queue.pop();
                auto delta = core::HexCoord{1, 0};
                for (int direction = 0; direction < 6; ++direction) {
                    checkDeadline();
                    const Key next{current.first + delta.q, current.second + delta.r};
                    delta = core::RotateClockwise(delta);
                    if (next.first < remote - width - height - 8 ||
                        next.first > remote + 2 * width + height + 12 || next.second < clearance ||
                        next.second > clearance + 2 * height + width + 12 ||
                        forbidden.count(next) || visited.count(next) || previous.count(next))
                        continue;
                    previous[next] = current;
                    queue.push(next);
                }
            }
            if (!previous.count(goal))
                throw std::invalid_argument(
                    "assembled target has no selective calcification route");
            std::vector<Key> path;
            for (auto c = goal; c != start; c = previous.at(c))
                path.push_back(c);
            for (auto it = path.rbegin(); it != path.rend(); ++it) {
                checkDeadline();
                translations.push_back({it->first, it->second});
                visited.insert(*it);
            }
        }
        finishingDistance = static_cast<int>(translations.size()) - 1;
        const int finishingTrack = object("track", base, 1, 0);
        auto& data = result.objects[finishingTrack].bodyData;
        data = {1, 0, 0, 0, static_cast<int>(translations.size())};
        for (auto cell : translations)
            data.insert(data.end(), {cell.q, cell.r});
        data.push_back(0);
    }
    int time = 0;
    if (surplusRetention) {
        auto reach = core::HexCoord{1, 0};
        for (int n = 0; n < (result.objects[vitaeArm].bodyData.at(1) + 1) % 6; ++n)
            reach = core::RotateClockwise(reach);
        const auto contact = core::Add(result.objects[vitaeArm].position, reach);
        int low = std::numeric_limits<int>::max(), high = std::numeric_limits<int>::min();
        for (const auto& atom : materialPuzzle.outputs.front().atoms) {
            low = std::min(low, atom.position.q);
            high = std::max(high, atom.position.q);
        }
        retentionDistance = fullBounds().q - contact.q + high - low +
                            static_cast<int>(completionTransfer->retention.capacity) * 3 + 2;
        const auto base = core::Add(contact, {1, 0});
        retentionArm = object("arm1", base, 1, 3);
        const int rail = object("track", base, 1, 0);
        result.objects[rail].bodyData = ReceiverRailBody(retentionDistance);
    }
    if (wheelSource) {
        const auto base = result.objects.at(wheelDevice).position;
        std::vector<core::HexCoord> footprint{base, core::Add(base, {0, 2})};
        auto spoke = core::HexCoord{1, 0};
        for (int i = 0; i < 6; ++i) {
            footprint.push_back(core::Add(base, spoke));
            spoke = core::RotateClockwise(spoke);
        }
        const auto occupied = [&](core::HexCoord cell) {
            return std::find(footprint.begin(), footprint.end(), cell) != footprint.end();
        };
        for (const auto& hardware : result.objects) {
            if (hardware.typeName == "track") {
                for (int i = 0; i < hardware.bodyData.at(4); ++i)
                    if (occupied(core::Add(hardware.position, {hardware.bodyData.at(5 + 2 * i),
                                                               hardware.bodyData.at(6 + 2 * i)})))
                        throw ExecutionIR::LayoutFailure(
                            "wheel occupied footprint intersects relative track");
            } else if ((hardware.typeName == "piston" || hardware.typeName == "arm1") &&
                       occupied(hardware.position)) {
                throw ExecutionIR::LayoutFailure("wheel occupied footprint intersects arm base");
            }
        }
    }
    std::vector<int> displacement(result.objects.size()), lengths(result.objects.size(), 2);
    lengths[feedArm] = 1;
    if (intakeArm >= 0) lengths[intakeArm] = 1;
    if (disposal) lengths[wasteArm] = 1;
    std::size_t phaseProduct = 0;
    auto phase = [&](const char* name, int begin, int end) {
        if (mixedInventory && begin != end)
            mixedInventory->phases.push_back({phaseProduct, name, begin, end});
    };
    // P2 opt-in per-arm feed clock. The feeder's extraction runs on its own
    // Independent feeder clock: the feeder keeps its own absolute tick stream so it
    // can prepare the next atom while the upper and lower arms finish the current
    // row. Only the plain narrow ordinary row is eligible: every path with shared
    // custody, reactions, buffers, escaping hand-off stations, wide feeds or a
    // finite/single-product halt keeps the original serialized emitter byte-for-byte.
    // Default on since 2026-11-11: with the assembly clock below it is the single
    // largest cycles lever the row has (146-puzzle A/B improved=32 regressed=0,
    // isolated samples -20% to -42% cycles: bdaa166d 23072 -> 13388, 284971e2
    // 3656 -> 2348, 0f6f9d67 12005 -> 8135). The master opt-out is
    // `OPUSOPTIMA_ROW_ASSEMBLY_CLOCKS=off`, because the assembly clock below implies
    // this one; `RowClockScope(-1)` forces the serialized emitter for one compile.
    const bool feedPrefetchRequested = parallelClocks || RowClockEnabledFrom(
        "OPUSOPTIMA_FEED_PREFETCH");
    // P2b bounded steady-state row pipeline. Requires the P2 independent feeder
    // clock and additionally (a) gates each drop on the exact tick the hand-off
    // cell is free instead of the whole shared assembly clock, and (b) lets the
    // feeder pre-stage the next atom against the pickup while the current
    // product's output/finish is still emitting. Default on with the clocks below;
    // `OPUSOPTIMA_ROW_ASSEMBLY_CLOCKS=off` is the master opt-out.
    const bool rowPipelineFlag =
        parallelClocks || RowClockEnabledFrom("OPUSOPTIMA_ROW_PIPELINE");
    // P3 bounded assembly/output clock overlap. Requires the P2b row pipeline and
    // additionally lets the upper-arm return/regrab/extend tail of one row run on
    // its own absolute clock so it overlaps the lower-arm reset and the next row's
    // atom staging. Default on since 2026-11-11 (see the feeder-clock note above);
    // `OPUSOPTIMA_ROW_ASSEMBLY_CLOCKS=off` restores the serialized emitter.
    const bool assemblyClocksRequested =
        parallelClocks || RowClockEnabledFrom("OPUSOPTIMA_ROW_ASSEMBLY_CLOCKS");
    const bool rowPipelineRequested = rowPipelineFlag || assemblyClocksRequested;
    const bool bufferedSupply = !bufferSlots.empty();
    // The emission schedule only *reads* the shared cursor (`beginCycle = time`,
    // and every await above uses std::max so the cursor stays monotonic), it never
    // participates in the hand-off protocol. Emitting that record therefore does
    // not by itself make the assembly clocks unsafe, so allow the opt-in to keep
    // the emission path on the fast row pipeline instead of excluding it.
    const bool allowEmissionClocks = RowClockOptIn("OPUSOPTIMA_ROW_EMISSION_CLOCKS");
    // A bounded run only changes how the tape terminates and how many repetitions
    // are emitted; it does not alter the hand-off protocol the clocks rely on.
    const bool allowFiniteClocks = RowClockOptIn("OPUSOPTIMA_ROW_FINITE_CLOCKS");
    const bool feedPrefetch =
        (feedPrefetchRequested || rowPipelineRequested) && !expandedFeeds && !completionTransfer &&
        !mixedInventory && !reactionAssembly && !animismus && !sharedOutputs && !labels &&
        !productOrigins && !supplied && !validationPuzzle && !custody &&
        ((!emissions && !owners) || allowEmissionClocks) && !singleProduct &&
        (!finiteProducts || allowFiniteClocks) && !componentRows && !bufferedSupply;
    const bool rowPipeline = rowPipelineRequested && feedPrefetch;
    const bool assemblyClocks = assemblyClocksRequested && rowPipeline;
    int feedReady = 0;
    int feedTime = 0;
    // Absolute clock for the upper-arm return/regrab/extend tail under the
    // assembly-clock opt-in; only advanced when assemblyClocks is on.
    int upperTime = 0;
    // Absolute clock for the conveyor's empty row-entry return leg. The arm is
    // free of the atom once it has released at the row, so the return may hide
    // under the lower-arm grab and the current row's bonding.
    int convTime = 0;
    // Earliest tick at which the single shared hand-off cell is guaranteed free
    // after the most recently emitted consumer. Only advanced by the opt-in
    // pipeline; the coarse drop sync keeps using the shared cursor otherwise.
    int handoffClear = 0;
    using IR = ExecutionIR;
    IR::State live;
    FragmentCustody liveCustody;
    std::vector<int> rotations(result.objects.size());
    std::vector<bool> grabbing(result.objects.size());
    if (completionTransfer) {
        live = IR::Initial(materialPuzzle);
        for (std::size_t i = 0; i < result.objects.size(); ++i) {
            rotations[i] = result.objects[i].bodyData.at(1);
            lengths[i] = result.objects[i].bodyData.at(0);
        }
    }
    auto baseAt = [&](int arm) {
        const auto origin = result.objects.at(arm).position;
        if (completionTransfer) {
            for (const auto& rail : result.objects) {
                if (rail.typeName != "track") continue;
                const int count = rail.bodyData.at(4);
                for (int i = 0; i < count; ++i) {
                    const auto cell = core::Add(
                        rail.position, {rail.bodyData.at(5 + 2 * i), rail.bodyData.at(6 + 2 * i)});
                    if (cell != origin) continue;
                    const int next = i + displacement.at(arm);
                    if (next < 0 || next >= count)
                        throw std::invalid_argument(
                            "completion transfer holder outside real track");
                    return core::Add(rail.position, {rail.bodyData.at(5 + 2 * next),
                                                     rail.bodyData.at(6 + 2 * next)});
                }
            }
        }
        return core::Add(origin, {displacement.at(arm), 0});
    };
    auto tipAt = [&](int arm) {
        core::HexCoord reach{lengths.at(arm), 0};
        for (int n = 0; n < rotations.at(arm); ++n)
            reach = core::RotateClockwise(reach);
        return core::Add(baseAt(arm), reach);
    };
    auto record = [&](const IR::Operation& operation, const char* kind) {
        completionTransfer->ledger.AppendFromTail(materialPuzzle, live, operation);
        const auto id = operation.kind == IR::Kind::Acquire ? live.nextId - 1
                        : operation.atoms.empty()           ? 0
                                                            : operation.atoms.front();
        row_profile::Scope observerProfile(row_profile::Observer);
        completionTransfer->events.push_back(
            {time, kind, id, operation.atoms.size() > 1 ? operation.atoms[1] : 0,
             live.atoms.contains(id) ? live.atoms.at(id).position : core::HexCoord{}});
    };
    auto atomAt = [&](core::HexCoord cell) -> IR::AtomId {
        for (const auto& [id, atom] : live.atoms)
            if (atom.position == cell) return id;
        return 0;
    };
    auto awaitFeed = [&] {
        // The opt-in feed clock may legitimately run ahead of the shared tick
        // stream; the shared stream only waits when it actually needs the atom.
        if (feedPrefetch)
            time = std::max(time, feedTime);
        else
            time = std::max(time, feedReady);
    };
    // Record that every instruction emitted so far has left the shared hand-off
    // cell free at the current shared tick. Under the row pipeline this replaces
    // the coarse "wait for the whole assembly clock" bound on the next drop.
    auto markHandoffFree = [&] {
        if (rowPipeline) handoffClear = time;
    };
    // Raise the shared cursor to the opt-in upper-arm tail clock. Used before
    // any shared-clock emission that touches an upper arm, so per-object ticks
    // stay non-decreasing and the upper tail's state is current.
    auto isUpperArm = [&](int arm) {
        return std::find(upper.begin(), upper.end(), arm) != upper.end();
    };
    auto awaitUpper = [&] {
        if (assemblyClocks) time = std::max(time, upperTime);
    };
    auto awaitConv = [&] {
        if (assemblyClocks) time = std::max(time, convTime);
    };
    auto write = [&](const std::vector<int>& arms, char action) {
        checkDeadline();
        if (std::find(arms.begin(), arms.end(), feedArm) != arms.end()) awaitFeed();
        if (assemblyClocks && conveyor >= 0 &&
            std::find(arms.begin(), arms.end(), conveyor) != arms.end())
            awaitConv();
        if (assemblyClocks &&
            std::any_of(arms.begin(), arms.end(), [&](int arm) { return isUpperArm(arm); }))
            awaitUpper();
        std::map<IR::AtomId, std::pair<int, core::HexCoord>> motions;
        for (int arm : arms) {
            checkDeadline();
            const auto previousTip = completionTransfer ? tipAt(arm) : core::HexCoord{};
            const auto previousBase = completionTransfer ? baseAt(arm) : core::HexCoord{};
            if (completionTransfer && action == 'G') {
                const auto tip = tipAt(arm);
                auto id = atomAt(tip);
                if (!id) {
                    for (const auto& port : result.objects) {
                        if (port.typeName != "input") continue;
                        const int inputIndex = port.bodyData.at(2);
                        if (inputIndex < 0 ||
                            static_cast<std::size_t>(inputIndex) >= p.inputs.size())
                            throw std::invalid_argument("completion transfer input port index");
                        const auto& input = p.inputs.at(inputIndex);
                        if (input.atoms.size() != 1 || !input.bonds.empty())
                            throw std::invalid_argument(
                                "completion transfer input port must be singleton");
                        auto local = input.atoms.front().position;
                        for (int n = 0; n < port.bodyData.at(1); ++n)
                            local = core::RotateClockwise(local);
                        if (core::Add(port.position, local) == tip) {
                            record({IR::Kind::Acquire,
                                    static_cast<std::size_t>(inputIndex),
                                    {},
                                    0,
                                    port.bodyData.at(1),
                                    port.position},
                                   "Acquire");
                            id = atomAt(tip);
                            break;
                        }
                    }
                }
                grabbing.at(arm) = true;
                if (id) liveCustody.Grab(live, arm, tip, id);
            }
            if (completionTransfer && action == 'g') {
                if (liveCustody.Held().contains(arm)) liveCustody.Drop(arm);
                grabbing.at(arm) = false;
            }
            result.objects[arm].instructions.push_back({action, time});
            if (action == 'A') ++displacement[arm];
            if (action == 'a') --displacement[arm];
            if (action == 'E') ++lengths[arm];
            if (action == 'e') --lengths[arm];
            if (completionTransfer) {
                if (action == 'r') rotations[arm] = (rotations[arm] + 1) % 6;
                if (action == 'R') rotations[arm] = (rotations[arm] + 5) % 6;
                if (liveCustody.Held().contains(arm) && action != 'G') {
                    const auto id = liveCustody.Held().at(arm);
                    const auto fragment = IR::Fragment(live, id);
                    const int turn = action == 'r' ? 1 : action == 'R' ? 5 : 0;
                    auto anchor = previousTip;
                    for (int n = 0; n < turn; ++n)
                        anchor = core::RotateClockwise(anchor);
                    const auto translation = core::Subtract(tipAt(arm), anchor);
                    if (turn || translation != core::HexCoord{}) {
                        const auto key = fragment.front();
                        const auto motion = std::pair{turn, translation};
                        if (motions.contains(key) && motions.at(key) != motion)
                            throw std::invalid_argument(
                                "completion transfer inconsistent simultaneous holders");
                        motions[key] = motion;
                    }
                    (void)previousBase;
                }
            }
        }
        if (completionTransfer) {
            for (const auto& [id, motion] : motions)
                record({IR::Kind::Relocate, 0, {id}, 0, motion.first, motion.second}, "Move");
            for (const auto& glyph : result.objects) {
                if (glyph.typeName != "glyph-calcification") continue;
                const auto precursor = atomAt(glyph.position);
                if (!precursor) continue;
                const auto type = live.atoms.at(precursor).type;
                if (type < core::AtomType::Air || type > core::AtomType::Water) continue;
                if (IR::Fragment(live, precursor).size() != 1)
                    throw std::invalid_argument(
                        "completion transfer calcification requires singleton contact");
                std::vector<std::size_t> holders;
                for (const auto& [arm, held] : liveCustody.Held())
                    if (held == precursor) holders.push_back(arm);
                const auto product = live.nextId;
                record({IR::Kind::Calcify, 0, {precursor}}, "Calcify");
                record({IR::Kind::Relocate, 0, {product}, 0, 0, glyph.position},
                       "ReactionPlacement");
                for (const auto arm : holders) {
                    liveCustody.Drop(arm);
                    liveCustody.Grab(live, arm, tipAt(static_cast<int>(arm)), product);
                }
                completionTransfer->events.push_back(
                    {time, "ReactionIdentity", precursor, product, glyph.position});
                liveCustody.Validate(live);
                RowCompletionTransfer::Boundary reaction{time, live, {}};
                for (std::size_t arm = 0; arm < result.objects.size(); ++arm) {
                    const auto& object = result.objects[arm];
                    if (object.typeName != "piston" && object.typeName != "arm1") continue;
                    const auto held = liveCustody.Held().find(arm);
                    reaction.holders.push_back(
                        {static_cast<int>(arm), lengths.at(arm), displacement.at(arm),
                         rotations.at(arm), grabbing.at(arm),
                         held == liveCustody.Held().end() ? 0 : held->second,
                         baseAt(static_cast<int>(arm)), tipAt(static_cast<int>(arm))});
                }
                completionTransfer->reactions.push_back(std::move(reaction));
            }
            if (wheelSource) {
                const core::HexCoord substrate{-feedOffset + 2, -5};
                const auto precursor = atomAt(substrate);
                if (precursor && live.atoms.at(precursor).type == core::AtomType::Salt) {
                    if (IR::Fragment(live, precursor).size() != 1)
                        throw std::invalid_argument("wheel contact requires singleton Salt");
                    std::vector<std::size_t> holders;
                    for (const auto& [arm, held] : liveCustody.Held())
                        if (held == precursor) holders.push_back(arm);
                    const auto product = live.nextId;
                    IR::Operation duplicate{IR::Kind::Duplicate, 0, {precursor}};
                    duplicate.wheelCatalyst =
                        IR::WheelCatalyst{static_cast<std::size_t>(wheelDevice), wheelElement,
                                          result.objects.at(wheelDevice).bodyData.at(1)};
                    record(duplicate, "WheelDuplicate");
                    for (const auto arm : holders) {
                        liveCustody.Drop(arm);
                        liveCustody.Grab(live, arm, tipAt(static_cast<int>(arm)), product);
                    }
                    completionTransfer->events.push_back(
                        {time, "WheelIdentity", precursor, product, substrate});
                    liveCustody.Validate(live);
                    RowCompletionTransfer::Boundary reaction{time, live, {}};
                    for (std::size_t arm = 0; arm < result.objects.size(); ++arm) {
                        const auto& object = result.objects[arm];
                        if (object.typeName != "piston" && object.typeName != "arm1") continue;
                        const auto held = liveCustody.Held().find(arm);
                        reaction.holders.push_back(
                            {static_cast<int>(arm), lengths.at(arm), displacement.at(arm),
                             rotations.at(arm), grabbing.at(arm),
                             held == liveCustody.Held().end() ? 0 : held->second,
                             baseAt(static_cast<int>(arm)), tipAt(static_cast<int>(arm))});
                    }
                    completionTransfer->reactions.push_back(std::move(reaction));
                }
            }
            if (coproductTransfer)
                for (const auto& glyph : result.objects) {
                    if (glyph.typeName != "glyph-life-and-death") continue;
                    auto cell = [&](core::HexCoord offset) {
                        for (int n = 0; n < glyph.bodyData.at(1); ++n)
                            offset = core::RotateClockwise(offset);
                        return core::Add(glyph.position, offset);
                    };
                    const auto a = atomAt(cell({0, 0})), b = atomAt(cell({1, 0}));
                    const auto v = cell({0, 1}), m = cell({1, -1});
                    if (!a || !b || atomAt(v) || atomAt(m)) continue;
                    if (live.atoms.at(a).type != core::AtomType::Salt ||
                        live.atoms.at(b).type != core::AtomType::Salt ||
                        IR::Fragment(live, a).size() != 1 || IR::Fragment(live, b).size() != 1)
                        continue;
                    bool held = false;
                    for (const auto& [arm, id] : liveCustody.Held())
                        held |= id == a || id == b;
                    if (held) continue; // Native conversion_input forbids grabbed precursors.
                    RowCompletionTransfer::Boundary inputs{time, live, {}};
                    for (std::size_t arm = 0; arm < result.objects.size(); ++arm) {
                        const auto& object = result.objects[arm];
                        if (object.typeName != "piston" && object.typeName != "arm1") continue;
                        const auto h = liveCustody.Held().find(arm);
                        inputs.holders.push_back(
                            {static_cast<int>(arm), lengths.at(arm), displacement.at(arm),
                             rotations.at(arm), grabbing.at(arm),
                             h == liveCustody.Held().end() ? 0 : h->second,
                             baseAt(static_cast<int>(arm)), tipAt(static_cast<int>(arm))});
                    }
                    completionTransfer->reactionInputs.push_back(std::move(inputs));
                    const auto first = live.nextId;
                    record({IR::Kind::Animismus, 0, {a, b}}, "Animismus");
                    record({IR::Kind::Relocate, 0, {first}, 0, 0, v}, "ReactionPlacement");
                    record({IR::Kind::Relocate, 0, {first + 1}, 0, 0, m}, "ReactionPlacement");
                    completionTransfer->events.push_back({time, "ReactionIdentity", a, first, v});
                    completionTransfer->events.push_back(
                        {time, "ReactionIdentity", b, first + 1, m});
                    // Empty grabbing arms on real outlet cells acquire the fresh identity.
                    for (std::size_t arm = 0; arm < result.objects.size(); ++arm) {
                        if (!grabbing.at(arm) || liveCustody.Held().contains(arm)) continue;
                        const auto tip = tipAt(static_cast<int>(arm));
                        if (tip == v || tip == m)
                            liveCustody.Grab(live, arm, tip, tip == v ? first : first + 1);
                    }
                    liveCustody.Validate(live);
                    RowCompletionTransfer::Boundary reaction{time, live, {}};
                    for (std::size_t arm = 0; arm < result.objects.size(); ++arm) {
                        const auto& object = result.objects[arm];
                        if (object.typeName != "piston" && object.typeName != "arm1") continue;
                        const auto h = liveCustody.Held().find(arm);
                        reaction.holders.push_back(
                            {static_cast<int>(arm), lengths.at(arm), displacement.at(arm),
                             rotations.at(arm), grabbing.at(arm),
                             h == liveCustody.Held().end() ? 0 : h->second,
                             baseAt(static_cast<int>(arm)), tipAt(static_cast<int>(arm))});
                    }
                    completionTransfer->reactions.push_back(std::move(reaction));
                }
            for (const auto& glyph : result.objects) {
                if (glyph.typeName != "bonder") continue;
                core::HexCoord d{1, 0};
                for (int n = 0; n < glyph.bodyData.at(1); ++n)
                    d = core::RotateClockwise(d);
                auto a = atomAt(glyph.position), b = atomAt(core::Add(glyph.position, d));
                if (a && b) {
                    if (b < a) std::swap(a, b);
                    if (!live.bonds.contains({a, b}))
                        record({IR::Kind::Bond, 0, {a, b}, 1}, "Bond");
                }
            }
            liveCustody.Validate(live);
            for (const auto& [arm, id] : liveCustody.Held())
                if (live.atoms.at(id).position != tipAt(static_cast<int>(arm)))
                    throw std::invalid_argument("completion transfer holder lost material contact");
        }
        ++time;
    };
    auto move = [&](const std::vector<int>& arms, int distance) {
        for (int n = 0; n < std::abs(distance); ++n)
            write(arms, distance > 0 ? 'A' : 'a');
    };
    // Feed-only actions on the opt-in independent clock. The bookkeeping mirrors
    // write() for the single feed arm; no other arm shares these ticks and the
    // shared time cursor is untouched so other arms keep their hand-offs.
    auto feedWrite = [&](char action) {
        checkDeadline();
        result.objects[feedArm].instructions.push_back({action, feedTime});
        if (action == 'A') ++displacement[feedArm];
        if (action == 'a') --displacement[feedArm];
        if (action == 'E') ++lengths[feedArm];
        if (action == 'e') --lengths[feedArm];
        if (action == 'r') rotations[feedArm] = (rotations[feedArm] + 1) % 6;
        if (action == 'R') rotations[feedArm] = (rotations[feedArm] + 5) % 6;
        if (action == 'G') grabbing[feedArm] = true;
        if (action == 'g') grabbing[feedArm] = false;
        ++feedTime;
    };
    auto feedAct = [&](char action) {
        if (feedPrefetch)
            feedWrite(action);
        else
            write({feedArm}, action);
    };
    auto feedMove = [&](int distance) {
        for (int n = 0; n < std::abs(distance); ++n)
            feedAct(distance > 0 ? 'A' : 'a');
    };
    // Upper-arm tail actions on the opt-in assembly clock. The bookkeeping
    // mirrors write() for the given arms; the shared time cursor is untouched so
    // the lower-arm reset and the next row's atom staging overlap this tail.
    auto upperWrite = [&](const std::vector<int>& arms, char action) {
        if (!assemblyClocks) {
            write(arms, action);
            return;
        }
        checkDeadline();
        for (int arm : arms) {
            result.objects[arm].instructions.push_back({action, upperTime});
            if (action == 'A') ++displacement[arm];
            if (action == 'a') --displacement[arm];
            if (action == 'E') ++lengths[arm];
            if (action == 'e') --lengths[arm];
            if (action == 'G') grabbing[arm] = true;
            if (action == 'g') grabbing[arm] = false;
        }
        ++upperTime;
    };
    auto upperMove = [&](const std::vector<int>& arms, int distance) {
        for (int n = 0; n < std::abs(distance); ++n)
            upperWrite(arms, distance > 0 ? 'A' : 'a');
    };
    // Conveyor return travel on the opt-in assembly clock. Only pure motion is
    // routed here; any grab/release still goes through the shared clock guard,
    // which joins convTime first. Mirrors write() bookkeeping for the conveyor.
    auto convWrite = [&](char action) {
        if (!assemblyClocks) {
            write({conveyor}, action);
            return;
        }
        checkDeadline();
        result.objects[conveyor].instructions.push_back({action, convTime});
        if (action == 'A') ++displacement[conveyor];
        if (action == 'a') --displacement[conveyor];
        if (action == 'E') ++lengths[conveyor];
        if (action == 'e') --lengths[conveyor];
        if (action == 'G') grabbing[conveyor] = true;
        if (action == 'g') grabbing[conveyor] = false;
        ++convTime;
    };
    auto convMove = [&](int distance) {
        for (int n = 0; n < std::abs(distance); ++n)
            convWrite(distance > 0 ? 'A' : 'a');
    };
    bool resetRetimed = false;
    auto reset = [&](const std::vector<int>& arms) {
        write(arms, 'g');
        struct Task {
            int arm, start{};
            std::vector<char> actions;
            std::set<std::pair<int, int>> occupancy;
        };
        std::vector<Task> tasks;
        bool eligible = retimeEmptyResets && arms.size() <= 16;
        for (int arm : arms)
            eligible &= result.objects[arm].typeName == "piston" &&
                        result.objects[arm].bodyData[1] == 1 &&
                        std::abs(displacement[arm]) + std::abs(lengths[arm] - 2) <= 256;
        for (int arm : eligible ? arms : std::vector<int>{}) {
            const auto& obj = result.objects[arm];
            int position = displacement[arm], length = lengths[arm];
            auto task = [&](std::vector<char> actions) {
                if (actions.empty()) return;
                Task t{arm, 0, std::move(actions), {}};
                auto occupy = [&] {
                    for (int n = 0; n <= length; ++n)
                        t.occupancy.emplace(obj.position.q + position, obj.position.r + n);
                };
                occupy();
                for (char action : t.actions) {
                    checkDeadline();
                    if (action == 'e') --length;
                    if (action == 'E') ++length;
                    if (action == 'a') --position;
                    if (action == 'A') ++position;
                    occupy();
                }
                tasks.push_back(std::move(t));
            };
            task(std::vector<char>(std::abs(length - 2), length > 2 ? 'e' : 'E'));
            task(std::vector<char>(std::abs(position), position > 0 ? 'a' : 'A'));
        }
        int serial = 0, span = 0;
        for (std::size_t i = 0; eligible && i < tasks.size(); ++i) {
            checkDeadline();
            auto& task = tasks[i];
            for (std::size_t j = 0; j < i; ++j) {
                checkDeadline();
                const auto& prior = tasks[j];
                const bool conflict =
                    task.arm == prior.arm ||
                    std::any_of(task.occupancy.begin(), task.occupancy.end(),
                                [&](auto cell) { return prior.occupancy.count(cell); });
                if (conflict)
                    task.start =
                        std::max(task.start, prior.start + static_cast<int>(prior.actions.size()));
            }
            serial += static_cast<int>(task.actions.size());
            span = std::max(span, task.start + static_cast<int>(task.actions.size()));
        }
        // The drop is a hard atom/bond barrier. Inside this phase only empty
        // pistons move: intersecting swept shafts and same-arm tasks keep parent
        // order. The next grab/glyph/input/output phase waits for every task.
        if (eligible && span < serial) {
            for (const auto& task : tasks)
                for (std::size_t n = 0; n < task.actions.size(); ++n)
                    result.objects[task.arm].instructions.push_back(
                        {task.actions[n], time + task.start + static_cast<int>(n)});
            for (int arm : arms) {
                checkDeadline();
                lengths[arm] = 2;
                displacement[arm] = 0;
            }
            time += span;
            resetRetimed = true;
            return;
        }
        for (int arm : arms) {
            checkDeadline();
            while (lengths[arm] > 2)
                write({arm}, 'e');
            while (lengths[arm] < 2)
                write({arm}, 'E');
            move({arm}, -displacement[arm]);
        }
    };
    // Product-level overlap: after the last row the upper arms still need
    // their return/reset, but the next product's lower staging and feed only
    // use the shared lower/feed clocks. Run the upper reset on upperTime so
    // both can overlap; the next upper action joins it through awaitUpper().
    auto resetUpperOnClock = [&] {
        if (!assemblyClocks) {
            reset(upper);
            return;
        }
        awaitUpper();
        for (int arm : upper)
            upperWrite({arm}, 'g');
        for (int arm : upper) {
            while (lengths[arm] > 2)
                upperWrite({arm}, 'e');
            while (lengths[arm] < 2)
                upperWrite({arm}, 'E');
            upperMove({arm}, -displacement[arm]);
        }
    };
    int receiver = -1;
    int completedTransfers = 0;
    bool vitaePending = false, morsPending = false;
    auto retainVitae = [&]() {
        if (!surplusRetention || !vitaePending || morsPending)
            throw std::invalid_argument("retention requires exactly one real pending Vitae");
        auto& terminal = completionTransfer->retention;
        const auto slot = terminal.slots.size();
        if (slot >= terminal.capacity || slot != static_cast<std::size_t>(completedTransfers))
            throw std::invalid_argument("retention capacity or per-product surplus exceeded");
        write({vitaeArm}, 'g');
        write({vitaeArm}, 'R');
        const auto id = atomAt(tipAt(retentionArm));
        if (!id || live.atoms.at(id).type != core::AtomType::Vitae ||
            IR::Fragment(live, id).size() != 1)
            throw std::invalid_argument("retention contact lacks real singleton Vitae");
        write({retentionArm}, 'G');
        write({retentionArm}, 'R');
        write({retentionArm}, 'R');
        const int distance = retentionDistance - static_cast<int>(slot) * 3;
        move({retentionArm}, distance);
        const auto target = tipAt(retentionArm);
        write({retentionArm}, 'g');
        liveCustody.ReserveLoose(live, id, slot);
        record({IR::Kind::Store, slot, {id}}, "RetentionStore");
        liveCustody.Validate(live);
        terminal.slots.push_back({slot, id, core::AtomType::Vitae});
        completionTransfer->retentionTargets.emplace(id, target);
        move({retentionArm}, -distance);
        write({retentionArm}, 'r');
        write({retentionArm}, 'r');
        vitaePending = false;
    };
    RowCompletionTransfer::Candidate selectedReceiver;
    auto finish = [&]() {
        // The upper arms may still be on their own tail clock; join before the
        // shared-clock finish reset so per-arm ticks stay ordered.
        awaitUpper();
        if (surplusRetention && vitaePending) retainVitae();
        const int begin = time;
        if (completionTransfer) {
            RowCompletionTransfer::Boundary boundary{time, live, {}};
            for (std::size_t i = 0; i < result.objects.size(); ++i) {
                const auto& obj = result.objects[i];
                if (obj.typeName != "piston" && obj.typeName != "arm1") continue;
                const auto held = liveCustody.Held().find(i);
                boundary.holders.push_back(
                    {static_cast<int>(i), lengths[i], displacement[i], rotations[i], grabbing[i],
                     held == liveCustody.Held().end() ? 0 : held->second,
                     baseAt(static_cast<int>(i)), tipAt(static_cast<int>(i))});
            }
            completionTransfer->boundaries.push_back(boundary);
            {
                row_profile::Scope profile(row_profile::Observer);
                completionTransfer->partialMachine = result;
            }
            if (live.atoms.empty())
                throw std::invalid_argument("completion transfer: empty completion boundary");
            auto product =
                std::find_if(live.atoms.begin(), live.atoms.end(), [&](const auto& entry) {
                    return std::none_of(live.stored.begin(), live.stored.end(),
                                        [&](const auto& slot) {
                                            return std::find(slot.second.begin(), slot.second.end(),
                                                             entry.first) != slot.second.end();
                                        });
                });
            if (product == live.atoms.end())
                throw std::invalid_argument("completion lacks product");
            const auto seed = product->first;
            if (IR::Fragment(live, seed).size() + live.stored.size() != live.atoms.size())
                throw std::invalid_argument(
                    "completion transfer: disconnected completion material");
            // Borrow history only for this synchronous proof; the projected state owns its
            // material.
            {
                row_profile::Scope projectionProfile(row_profile::Projection);
                const IR::Operation output{IR::Kind::Output, 0, {seed}};
                auto check = completionTransfer->ledger.ProjectOutput(materialPuzzle, live, output);
                if (surplusRetention) {
                    auto terminal = completionTransfer->retention;
                    terminal.outputs = {static_cast<std::size_t>(completedTransfers + 1)};
                    RequireFinitePhysicalTerminal(check, terminal,
                                                  completionTransfer->retentionTargets);
                    liveCustody.Validate(live);
                    if (completedTransfers == 5) {
                        auto proof = [&] {
                            row_profile::Scope profile(row_profile::ProofCopy,
                                                       completionTransfer->ledger.steps.size());
                            return completionTransfer->ledger;
                        }();
                        auto tail = live;
                        proof.AppendFromTail(materialPuzzle, tail, output);
                        (void)MakeFiniteExecutionPlan(materialPuzzle, std::move(proof), terminal,
                                                      completionTransfer->mode);
                    }
                } else
                    IR::RequireComplete(check, {completedTransfers + 1});
            }
            int maximumQ = std::numeric_limits<int>::min();
            for (const auto& obj : result.objects)
                maximumQ = std::max(maximumQ, obj.position.q);
            for (const auto& [id, atom] : live.atoms)
                maximumQ = std::max(maximumQ, atom.position.q);
            const core::HexCoord receiverDirection =
                surplusRetention ? core::HexCoord{0, 1} : core::HexCoord{1, 0};
            int maximumR = fullBounds().r;
            for (const auto& [id, atom] : live.atoms)
                maximumR = std::max(maximumR, atom.position.r);
            if (receiver < 0) {
                const auto productFragment = IR::Fragment(live, seed);
                for (const auto& [id, atom] : live.atoms) {
                    if (std::find(productFragment.begin(), productFragment.end(), id) ==
                        productFragment.end())
                        continue;
                    for (int rotation = 0; rotation < 6; ++rotation) {
                        checkDeadline();
                        core::HexCoord reach{1, 0};
                        for (int n = 0; n < rotation; ++n)
                            reach = core::RotateClockwise(reach);
                        const auto base = core::Subtract(atom.position, reach);
                        RowCompletionTransfer::Candidate candidate{
                            base,     atom.position,         {maximumQ + 2, base.r},
                            rotation, maximumQ + 2 - base.q, {}};
                        if (surplusRetention) {
                            candidate.destination = {base.q, maximumR + 2};
                            candidate.distance = maximumR + 2 - base.r;
                        }
                        for (const auto& obj : result.objects)
                            if ((obj.typeName == "piston" || obj.typeName == "arm1") &&
                                obj.position == base)
                                candidate.rejection = "receiver base overlaps existing arm base";
                        for (const auto& [other, material] : live.atoms)
                            if (material.position == base)
                                candidate.rejection = "receiver base overlaps completed material";
                        // This base must exist throughout assembly, not appear at the boundary.
                        for (const auto& step : completionTransfer->ledger.steps) {
                            for (const auto& [other, material] : step.effects.atoms)
                                if (material.position == base)
                                    candidate.rejection =
                                        "receiver base intersects earlier generated material";
                        }
                        if (!ReceiverRailDisjoint(result, base, candidate.distance,
                                                  receiverDirection))
                            candidate.rejection = "receiver rail shares an existing track cell";
                        if (surplusRetention) {
                            for (int offset = 1; offset <= candidate.distance; ++offset)
                                for (const auto member : productFragment) {
                                    auto cell = live.atoms.at(member).position;
                                    cell = core::Add(cell, {offset * receiverDirection.q,
                                                            offset * receiverDirection.r});
                                    for (const auto& obj : result.objects)
                                        if ((obj.typeName == "piston" || obj.typeName == "arm1") &&
                                            obj.position == cell)
                                            candidate.rejection = "receiver material sweep "
                                                                  "intersects existing arm base";
                                    for (const auto& [retained, target] :
                                         completionTransfer->retentionTargets)
                                        if (target == cell)
                                            candidate.rejection =
                                                "receiver material sweep intersects retained stock";
                                }
                        }
                        completionTransfer->candidates.push_back(candidate);
                        if (candidate.rejection.empty()) {
                            selectedReceiver = candidate;
                            break;
                        }
                    }
                    if (!completionTransfer->candidates.back().rejection.size()) break;
                }
                if (completionTransfer->candidates.empty() ||
                    !completionTransfer->candidates.back().rejection.empty())
                    throw IR::LayoutFailure(
                        "Unsupported: no disjoint receiver passes conservative base filter");
                // Serialization has no creation-time field: these objects exist from cycle zero.
                // Earlier tapes are retained unchanged; native checks their coexistence.
                receiver = object("arm1", selectedReceiver.base, 1, selectedReceiver.rotation);
                const int rail = object("track", selectedReceiver.base, 1, 0);
                auto& data = result.objects[rail].bodyData;
                data = ReceiverRailBody(selectedReceiver.distance, receiverDirection);
                displacement.resize(result.objects.size());
                lengths.resize(result.objects.size(), 1);
                rotations.resize(result.objects.size());
                grabbing.resize(result.objects.size());
                rotations[receiver] = selectedReceiver.rotation;
            }
            const auto receiverAtom = atomAt(tipAt(receiver));
            if (!receiverAtom || !liveCustody.Held().size())
                throw std::invalid_argument("receiver has no real completed donor fragment");
            const auto fragment = IR::Fragment(live, receiverAtom);
            write({receiver}, 'G');
            std::vector<int> donors;
            for (const auto& [arm, held] : liveCustody.Held())
                if (static_cast<int>(arm) != receiver) donors.push_back(static_cast<int>(arm));
            write(donors, 'g');
            move({receiver}, selectedReceiver.distance);
            if (!coproductTransfer) write({receiver}, 'r');
            // Derive the output's rigid pose from the actual transported target graph.
            const auto& transportedTarget = materialPuzzle.outputs[0];
            core::HexCoord outputOrigin{};
            int outputRotation = -1;
            for (int rotation = 0; rotation < 6 && outputRotation < 0; ++rotation) {
                auto rotate = [&](core::HexCoord cell) {
                    for (int n = 0; n < rotation; ++n)
                        cell = core::RotateClockwise(cell);
                    return cell;
                };
                for (const auto& [id, material] : live.atoms) {
                    if (std::find(fragment.begin(), fragment.end(), id) == fragment.end()) continue;
                    const auto origin = core::Subtract(
                        material.position, rotate(transportedTarget.atoms.front().position));
                    bool matches = true;
                    std::map<std::pair<int, int>, IR::AtomId> ids;
                    for (const auto& atom : transportedTarget.atoms) {
                        const auto found = atomAt(core::Add(origin, rotate(atom.position)));
                        if (!found || live.atoms.at(found).type != atom.type) {
                            matches = false;
                            break;
                        }
                        ids[{atom.position.q, atom.position.r}] = found;
                    }
                    if (matches)
                        for (const auto& bond : transportedTarget.bonds) {
                            auto a = ids.at({bond.from.q, bond.from.r}),
                                 b = ids.at({bond.to.q, bond.to.r});
                            if (b < a) std::swap(a, b);
                            const auto found = live.bonds.find({a, b});
                            if (found == live.bonds.end() || found->second != bond.bits)
                                matches = false;
                        }
                    if (matches) {
                        outputOrigin = origin;
                        outputRotation = rotation;
                        break;
                    }
                }
            }
            if (outputRotation < 0) throw std::invalid_argument("transport target pose mismatch");
            if (!completedTransfers) {
                object("out-std", outputOrigin, 1, outputRotation);
                displacement.resize(result.objects.size());
                lengths.resize(result.objects.size(), 1);
                rotations.resize(result.objects.size());
                grabbing.resize(result.objects.size());
            } else {
                const auto& output = result.objects.back();
                if (output.position != outputOrigin || output.bodyData.at(1) != outputRotation)
                    throw std::invalid_argument("transport output pose changed across products");
            }
            if (IR::Fragment(live, receiverAtom) != fragment)
                throw std::invalid_argument("transport fragment identity changed");
            write({receiver}, 'g');
            if (surplusRetention) {
                liveCustody.Validate(live);
                row_profile::Scope projectionProfile(row_profile::Projection);
                const IR::Operation output{IR::Kind::Output, 0, {receiverAtom}};
                auto projected =
                    completionTransfer->ledger.ProjectOutput(materialPuzzle, live, output);
                auto terminal = completionTransfer->retention;
                terminal.outputs = {static_cast<std::size_t>(completedTransfers + 1)};
                RequireFinitePhysicalTerminal(projected, terminal,
                                              completionTransfer->retentionTargets);
                liveCustody.Validate(projected);
                if (completedTransfers == 5) {
                    auto proof = [&] {
                        row_profile::Scope profile(row_profile::ProofCopy,
                                                   completionTransfer->ledger.steps.size());
                        return completionTransfer->ledger;
                    }();
                    auto tail = live;
                    proof.AppendFromTail(materialPuzzle, tail, output);
                    (void)MakeFiniteExecutionPlan(materialPuzzle, std::move(proof), terminal,
                                                  completionTransfer->mode);
                }
            }
            record({IR::Kind::Output, 0, {receiverAtom}}, "Output");
            ++completedTransfers;
            if (surplusRetention) {
                auto terminal = completionTransfer->retention;
                terminal.outputs = {static_cast<std::size_t>(completedTransfers)};
                RequireFinitePhysicalTerminal(live, terminal, completionTransfer->retentionTargets);
            } else
                IR::RequireComplete(live, {static_cast<std::size_t>(completedTransfers)});
            if (completionTransfer->ledger.Replay(materialPuzzle) != live)
                throw std::invalid_argument("transport replay mismatch");
            if (row_profile::active && row_profile::active->boundaries < 6) {
                auto& profile = *row_profile::active;
                profile.checkpoints[profile.boundaries++] = profile.phases;
            }
            if (!coproductTransfer) write({receiver}, 'R');
            move({receiver}, -selectedReceiver.distance);
            reset(upper);
            phase("native-transfer", begin, time);
            return;
        }
        if (finishingArm >= 0) write({finishingArm}, 'G');
        resetUpperOnClock();
        if (finishingArm >= 0) {
            move({finishingArm}, finishingDistance);
            move({finishingArm}, -finishingDistance);
            write({finishingArm}, 'g');
        }
        phase("output-finish", begin, time);
    };
    std::vector<bool> assembled(cells.size());
    auto representatives = [&](int row) {
        std::vector<int> roots(cells.size());
        std::iota(roots.begin(), roots.end(), 0);
        auto root = [&](int i) {
            while (roots[i] != i)
                i = roots[i];
            return i;
        };
        for (int i = 0; i < static_cast<int>(cells.size()); ++i)
            if (assembled[i])
                for (int j = 0; j < i; ++j)
                    if (assembled[j]) {
                        checkDeadline();
                        int dx = cells[j].q - cells[i].q, dy = cells[j].r - cells[i].r;
                        if (edge(i, dx, dy)) roots[root(i)] = root(j);
                    }
        std::vector<int> seen, arms;
        for (int x = width - 1; x >= 0; --x)
            for (int i = 0; i < static_cast<int>(cells.size()); ++i)
                if (cells[i] == core::HexCoord{x, row} &&
                    std::find(seen.begin(), seen.end(), root(i)) == seen.end()) {
                    seen.push_back(root(i));
                    arms.push_back(upper[x]);
                }
        return arms;
    };
    std::vector<std::size_t> remaining;
    for (const auto& input : p.inputs)
        remaining.push_back(input.atoms.size());
    std::size_t custodyProduct = 0, custodyBatch = 0;
    std::vector<std::optional<FragmentCustodyEvent>> custodySlots(bufferSlots.size());
    std::optional<FragmentCustodyEvent> heldCustody;
    auto deliver = [&](const Slot& slot) {
        checkDeadline();
        const int awaitBegin = time;
        awaitFeed();
        phase("empty-return-join", awaitBegin, time);
        const int phaseBegin = time;
        bool last = false;
        if (slot.input >= 0) {
            last = --remaining[slot.input] == 0;
            if (last) remaining[slot.input] = p.inputs[slot.input].atoms.size();
        }
        const auto wide = std::find_if(wideFeeds.begin(), wideFeeds.end(),
                                       [&](const auto& feed) { return feed.input == slot.input; });
        if (directInput && slot.input >= 0) {
            // Walk to the atom's column, extend into its source row, grab it,
            // retract, walk to the hand-off cell, lower and release.
            const int target = slot.column - directMinColumn;
            move({intakeArm}, target - intakeIndex);
            intakeIndex = target;
            const int reach = 3 - slot.row;
            while (lengths[intakeArm] > reach)
                write({intakeArm}, 'e');
            while (lengths[intakeArm] < reach)
                write({intakeArm}, 'E');
            write({intakeArm}, 'G');
            while (lengths[intakeArm] > 1)
                write({intakeArm}, 'e');
            const int pickupIndex = -feedOffset - directMinColumn;
            move({intakeArm}, pickupIndex - intakeIndex);
            intakeIndex = pickupIndex;
            while (lengths[intakeArm] < 3)
                write({intakeArm}, 'E');
            write({intakeArm}, 'g');
            while (lengths[intakeArm] > 1)
                write({intakeArm}, 'e');
            markHandoffFree();
            phase(slot.input >= 0 ? "source-extraction" : "buffer-retrieval", phaseBegin, time);
            return;
        }
        if (wide == wideFeeds.end() && displacement[feedArm]) feedMove(-displacement[feedArm]);
        if (wide != wideFeeds.end()) {
            if (!displacement[feedArm]) move({feedArm}, wideOnly ? 1 : 5);
            const int lift = wide->lifts.at(slot.atom);
            const int distance = wide->distances.at(slot.atom);
            if (last) {
                if (wide->top - lift != 14)
                    throw std::invalid_argument("wide extraction must finish on bottom row");
                move({wide->batch}, distance - wide->startDistance);
                write({wide->batch}, 'E');
                write({wide->batch}, 'E');
                write({wide->batch}, 'G');
                write({wide->batch}, 'e');
                write({wide->batch}, 'e');
                move({wide->batch}, wide->startDistance - distance);
                move({wide->batch}, 10 - wide->top);
                write({wide->batch}, 'g');
                move({wide->courier}, wide->startDistance);
                write({wide->courier}, 'G');
                move({wide->batch}, wide->top - 10);
            } else {
                const int arm = wide->arms.at(slot.atom);
                move({arm}, lift);
                write({arm}, 'G');
                move({arm}, -lift);
                write({arm}, 'e');
                write({arm}, 'e');
                move({wide->courier}, distance);
                write({wide->courier}, 'G');
                write({arm}, 'g');
            }
            const int pickupDistance = last ? wide->startDistance : distance;
            // Retract across the disposal-arm base, then lower onto the shared
            // conveyor pickup. The parked narrow feeder has a separate track.
            move({wide->courier}, wide->transferDistance - pickupDistance - 6);
            write({wide->courier}, 'e');
            write({wide->courier}, 'e');
            move({wide->courier}, 6);
            write({wide->courier}, 'E');
            write({wide->courier}, 'E');
            write({wide->courier}, 'g');
            write({wide->courier}, 'e');
            write({wide->courier}, 'e');
            move({wide->courier}, -6);
            write({wide->courier}, 'E');
            write({wide->courier}, 'E');
            move({wide->courier}, 6 - wide->transferDistance);
            if (!last) {
                write({wide->arms.at(slot.atom)}, 'E');
                write({wide->arms.at(slot.atom)}, 'E');
            }
            return;
        }
        const bool threeRows =
            slot.input >= 0 && std::any_of(slots.begin(), slots.end(), [&](const Slot& other) {
                return other.input == slot.input && other.extensions == 0;
            });
        if (last && threeRows && slot.extensions == 2) {
            // The bounded escape hand-off moves the feeder and a second arm on
            // the same material; keep both on the shared clock for this atom.
            if (feedPrefetch) feedTime = time;
            move({feedArm}, 5);
            const int distance = (slot.distance - 5) / 2;
            move({escapeArm}, escapePark - distance);
            write({escapeArm}, 'E');
            write({escapeArm}, 'G');
            write({escapeArm}, 'e');
            move({escapeArm}, distance);
            for (int n = 0; n < 3; ++n)
                write({escapeArm}, 'R');
            write({escapeArm}, 'E');
            write({escapeArm}, 'g');
            write({escapeArm}, 'e');
            for (int n = 0; n < 3; ++n)
                write({escapeArm}, 'r');
            move({escapeArm}, -escapePark);
            move({feedArm}, -5);
            if (feedPrefetch) feedTime = time;
            markHandoffFree();
            return;
        }
        for (int n = 0; n < 3; ++n)
            feedAct('R');
        feedMove(slot.distance - 1);
        for (int n = 0; n < 3; ++n)
            feedAct('r');
        feedMove(1);
        for (int n = 0; n < slot.extensions; ++n)
            feedAct('E');
        feedAct('G');
        for (int n = 0; n < slot.extensions; ++n)
            feedAct('e');
        feedMove(-1);
        for (int n = 0; n < 3; ++n)
            feedAct('R');
        feedMove(1 - slot.distance);
        for (int n = 0; n < 3; ++n)
            feedAct('r');
        // The shared stream has already advanced past the previous consumer by
        // the time this delivery is emitted, so its tick is a safe lower bound
        // for the drop. The outbound travel above still overlaps the assembly.
        // Under the row pipeline the exact hand-off clearance is used instead:
        // the cell is known free much earlier than the start of this delivery,
        // so the drop (and the feeder's next fetch) overlap the current atom's
        // transport while the atom waits for the consumer at the shared clock.
        if (feedPrefetch) feedTime = std::max(feedTime, rowPipeline ? handoffClear : time);
        feedAct('E');
        feedAct('E');
        feedAct('g');
        feedAct('e');
        feedAct('e');
        if (feedPrefetch) awaitFeed();
        phase(slot.input >= 0 ? "source-extraction" : "buffer-retrieval", phaseBegin, time);
        if (custody) {
            FragmentCustodyEvent event;
            if (slot.input >= 0) {
                event = {custodyProduct,
                         static_cast<std::size_t>(slot.atom),
                         custodyBatch,
                         p.inputs.at(slot.input).atoms.at(slot.atom).type,
                         -1,
                         phaseBegin,
                         time,
                         {},
                         false,
                         last};
                if (last) ++custodyBatch;
            } else {
                auto& occupied = custodySlots.at(slot.atom);
                if (!occupied) throw std::logic_error("physical buffer retrieve from empty slot");
                event = *occupied;
                event.stored = false;
                event.begin = phaseBegin;
                event.end = time;
                occupied.reset();
            }
            heldCustody = event;
            custody->push_back(event);
        }
    };
    auto slotFor = [&](const SupplyPlan::Step& step) -> const Slot& {
        if (step.buffer >= 0 && step.kind != SupplyPlan::Kind::Store)
            return bufferSlots.at(step.buffer);
        const auto it = std::find_if(slots.begin(), slots.end(), [&](const auto& slot) {
            return slot.input == static_cast<int>(step.input) &&
                   slot.atom == static_cast<int>(step.sourceAtom);
        });
        if (it == slots.end()) throw std::logic_error("supply slot missing from row layout");
        return *it;
    };
    int storedWaste = 0;
    auto park = [&](int buffer) {
        const int awaitBegin = time;
        awaitFeed();
        phase("empty-return-join", awaitBegin, time);
        const int phaseBegin = time;
        const auto& slot = bufferSlots.at(buffer);
        if (displacement[feedArm]) move({feedArm}, -displacement[feedArm]);
        write({feedArm}, 'E');
        write({feedArm}, 'E');
        write({feedArm}, 'G');
        write({feedArm}, 'e');
        write({feedArm}, 'e');
        for (int n = 0; n < 3; ++n)
            write({feedArm}, 'R');
        move({feedArm}, slot.distance - 1);
        for (int n = 0; n < 3; ++n)
            write({feedArm}, 'r');
        move({feedArm}, 1);
        for (int n = 0; n < slot.extensions; ++n)
            write({feedArm}, 'E');
        write({feedArm}, 'g');
        const int released = time;
        if (custody) {
            if (!heldCustody || custodySlots.at(buffer))
                throw std::logic_error("physical buffer store occupancy conflict");
            auto event = *heldCustody;
            event.buffer = buffer;
            event.stored = true;
            event.begin = phaseBegin;
            event.end = released;
            event.position = {-feedOffset - (expandedFeeds ? 3 : 0) - (slot.distance - 5) / 2, 6};
            custodySlots.at(buffer) = event;
            custody->push_back(event);
            heldCustody.reset();
        }
        phase("buffer-storage-held", phaseBegin, released);
        for (int n = 0; n < slot.extensions; ++n)
            write({feedArm}, 'e');
        move({feedArm}, -1);
        for (int n = 0; n < 3; ++n)
            write({feedArm}, 'R');
        move({feedArm}, 1 - slot.distance);
        for (int n = 0; n < 3; ++n)
            write({feedArm}, 'r');
        phase("empty-buffer-return", released, time);
        if (mixedInventory) {
            ++mixedInventory->emptyReturnTasks;
            mixedInventory->emptyReturnCycles += time - released;
        }
        if (mixedInventory && mixedInventory->scheduleEmptyReturns) {
            // The atom is in its exclusive physical buffer. Only the empty
            // feeder returns; its next user (or tape halt) joins this task.
            feedReady = time;
            time = released;
        }
    };
    auto discard = [&](const SupplyPlan::Step& step) {
        deliver(slotFor(step));
        if (step.kind == SupplyPlan::Kind::Store) {
            park(step.buffer);
            return;
        }
        write({wasteArm}, 'E');
        write({wasteArm}, 'E');
        write({wasteArm}, 'G');
        write({wasteArm}, 'e');
        write({wasteArm}, 'e');
        if (boundedWaste) {
            const int distance = 12 + 3 * ++storedWaste;
            move({wasteArm}, distance);
            write({wasteArm}, 'E');
            write({wasteArm}, 'E');
            write({wasteArm}, 'g');
            write({wasteArm}, 'e');
            write({wasteArm}, 'e');
            move({wasteArm}, -distance);
        } else
            write({wasteArm}, 'g');
        markHandoffFree();
    };
    if (singleProduct && supply.productsPerPeriod != 1)
        throw std::invalid_argument("single-product row requires a one-product supply period");
    if (finiteProducts && finiteProducts % supply.productsPerPeriod)
        throw std::invalid_argument("finite feed demand must contain complete supply periods");
    const std::size_t productCount =
        finiteProducts  ? finiteProducts
        : singleProduct ? 1
                        : (boundedWaste ? 6 * p.outputMultiplier : supply.productsPerPeriod);
    std::size_t reactionSource = 0;
    for (std::size_t product = 0; product < productCount; ++product) {
        custodyProduct = product;
        checkDeadline();
        if ((boundedWaste || finiteProducts) && product &&
            product % supply.productsPerPeriod == 0) {
            while (supplyIndex < supply.steps.size()) {
                checkDeadline();
                const auto& step = supply.steps[supplyIndex++];
                if (step.kind != SupplyPlan::Kind::Discard)
                    throw std::logic_error("unconsumed supply demand");
                discard(step);
            }
            supplyIndex = 0;
        }
        phaseProduct = product;
        if (mixedInventory) {
            for (const auto& source : mixedInventory->stores)
                if (source.buffer != mixedInventory->bypassSlot) discard(source);
            for (const auto& reaction : mixedInventory->reactions) {
                checkDeadline();
                for (int inlet = 0; inlet < 2; ++inlet) {
                    checkDeadline();
                    const auto& source = reaction.inputs[inlet];
                    deliver(slotFor(source));
                    const int reactionBegin = time;
                    write({conveyor}, 'E');
                    write({conveyor}, 'G');
                    write({conveyor}, 'e');
                    if (source.conversion == ElementConversion::Calcification) {
                        write({conveyor}, 'r');
                        write({conveyor}, 'R');
                    }
                    const int distance = feedOffset + reactionQ + 1 - inlet;
                    write({conveyor}, 'R');
                    move({conveyor}, distance);
                    write({conveyor}, 'r');
                    write({conveyor}, 'E');
                    write({conveyor}, 'g');
                    write({conveyor}, 'e');
                    phase("reaction-inlet-held", reactionBegin, time);
                    const int emptyBegin = time;
                    move({conveyor}, -distance);
                    phase("reaction-inlet-empty", emptyBegin, time);
                }
                const int reactionBegin = time;
                int reactionResume = time;
                write({vitaeArm, morsArm}, 'G');
                write({vitaeArm}, 'r');
                write({morsArm}, 'r');
                for (int side = 0; side < 2; ++side) {
                    checkDeadline();
                    const int collector = side == 0 ? vitaeArm : morsArm;
                    const int courier = side == 0 ? vitaeCourier : morsCourier;
                    write({collector}, 'g');
                    write({collector}, 'R');
                    write({courier}, 'G');
                    move({courier}, 2);
                    write({courier}, 'g');
                    move({courier}, -2);
                    const int distance = feedOffset + reactionQ + (side == 0 ? 1 : 3);
                    write({conveyor}, 'e');
                    move({conveyor}, distance);
                    write({conveyor}, 'E');
                    write({conveyor}, 'G');
                    write({conveyor}, 'e');
                    const int escape = side == 0 ? 0 : 2;
                    move({conveyor}, -escape);
                    write({conveyor}, 'R');
                    move({conveyor}, escape - distance);
                    write({conveyor}, 'r');
                    // Shared pickup is the same physical handoff used by source extraction.
                    write({conveyor}, 'E');
                    write({conveyor}, 'E');
                    write({conveyor}, 'g');
                    write({conveyor}, 'e');
                    phase("reaction-coproduct", side == 0 ? reactionBegin : reactionResume, time);
                    park(reaction.outputs[side]);
                    reactionResume = time;
                }
            }
        }
        std::fill(assembled.begin(), assembled.end(), false);
        for (int row = height - 1; row >= 0; --row) {
            checkDeadline();
            // Row-to-row clearance for the local-intake machine: nothing is
            // emitted on the gap ticks, so each block keeps its internal phase
            // while the next block starts later.
            if (directInput && intakeBlockGap > 0 && row < height - 1) time += intakeBlockGap;
            std::vector<int> order;
            for (int i = 0; i < static_cast<int>(cells.size()); ++i)
                if (cells[i].r == row) order.push_back(i);
            std::sort(order.begin(), order.end(), [&](int a, int b) {
                checkDeadline();
                return cells[a].q > cells[b].q;
            });
            int current = width - 1;
            for (int atom : order) {
                checkDeadline();
                while (supplyIndex < supply.steps.size() &&
                       (supply.steps[supplyIndex].kind == SupplyPlan::Kind::Discard ||
                        supply.steps[supplyIndex].kind == SupplyPlan::Kind::Store))
                    discard(supply.steps[supplyIndex++]);
                if (supplyIndex < supply.steps.size() &&
                    supply.steps[supplyIndex].kind == SupplyPlan::Kind::Reagent &&
                    supply.steps[supplyIndex].conversion == ElementConversion::Duplication) {
                    checkDeadline();
                    deliver(slotFor(supply.steps[supplyIndex++]));
                    move({mercuryArm}, catalystDistance);
                    write({mercuryArm}, 'G');
                    move({mercuryArm}, -catalystDistance);
                    markHandoffFree();
                }
                while (supplyIndex < supply.steps.size() &&
                       (supply.steps[supplyIndex].kind == SupplyPlan::Kind::Discard ||
                        supply.steps[supplyIndex].kind == SupplyPlan::Kind::Store))
                    discard(supply.steps[supplyIndex++]);
                if (supplyIndex == supply.steps.size() ||
                    supply.steps[supplyIndex].targetAtom != static_cast<std::size_t>(atom))
                    throw std::logic_error("supply order does not match row assembly");
                const auto& step = supply.steps[supplyIndex++];
                const int emissionBegin = time;
                int holderRelease = -1;
                if (wheelSource && reactionAssembly->outputs[0].atoms[atom].type == wheelElement) {
                    SupplyPlan::Step reagent = step;
                    reagent.input = cardinalInput;
                    reagent.sourceAtom = 0;
                    reagent.buffer = -1;
                    deliver(slotFor(reagent));
                    holderRelease = time - 3;
                    write({conveyor}, 'E');
                    write({conveyor}, 'G');
                    write({conveyor}, 'e');
                    write({conveyor}, 'r');
                    write({conveyor}, 'R');
                    write({conveyor}, 'R');
                    write({conveyor}, 'R');
                    write({conveyor}, 'r');
                    write({conveyor}, 'r');
                } else if (reactionAssembly && !mixedInventory &&
                           !(secondSource && reactionAssembly->outputs[0].atoms[atom].type ==
                                                 core::AtomType::Quicksilver)) {
                    checkDeadline();
                    const bool vitae =
                        reactionAssembly->outputs[0].atoms[atom].type == core::AtomType::Vitae;
                    if (!(vitae ? vitaePending : morsPending)) {
                        if (surplusRetention && vitaePending && !morsPending) retainVitae();
                        if (vitaePending || morsPending)
                            throw std::invalid_argument(
                                "shared reaction requires more than one pending coproduct");
                        for (int inlet = 0; inlet < 2; ++inlet) {
                            checkDeadline();
                            const auto& source =
                                supply.steps.at(reactionSource++ % supply.steps.size());
                            SupplyPlan::Step reagent = source;
                            if (secondSource) {
                                reagent.input = cardinalInput;
                                reagent.sourceAtom = 0;
                                reagent.buffer = -1;
                            }
                            deliver(slotFor(reagent));
                            holderRelease = time - 3;
                            write({conveyor}, 'E');
                            write({conveyor}, 'G');
                            write({conveyor}, 'e');
                            write({conveyor}, 'r');
                            write({conveyor}, 'R');
                            const int distance = feedOffset + reactionQ + 1 - inlet;
                            write({conveyor}, 'R');
                            move({conveyor}, distance);
                            write({conveyor}, 'r');
                            write({conveyor}, 'E');
                            write({conveyor}, 'g');
                            write({conveyor}, 'e');
                            move({conveyor}, -distance);
                        }
                        write({vitaeArm, morsArm}, 'G');
                        write({vitaeArm}, 'r');
                        write({morsArm}, 'r');
                        vitaePending = morsPending = true;
                    }
                    const int collector = vitae ? vitaeArm : morsArm;
                    const int courier = vitae ? vitaeCourier : morsCourier;
                    write({collector}, 'g');
                    write({collector}, 'R');
                    write({courier}, 'G');
                    move({courier}, 2);
                    holderRelease = time;
                    write({courier}, 'g');
                    move({courier}, -2);
                    const int distance = feedOffset + reactionQ + (vitae ? 1 : 3);
                    write({conveyor}, 'e');
                    move({conveyor}, distance);
                    write({conveyor}, 'E');
                    write({conveyor}, 'G');
                    write({conveyor}, 'e');
                    const int escape = vitae ? 0 : 2;
                    move({conveyor}, -escape);
                    write({conveyor}, 'R');
                    move({conveyor}, escape - distance);
                    write({conveyor}, 'r');
                    write({conveyor}, 'E');
                    (vitae ? vitaePending : morsPending) = false;
                } else if (step.kind == SupplyPlan::Kind::Purify) {
                    auto purifyInput = [&](const SupplyPlan::Step& reagent, int distance) {
                        deliver(slotFor(reagent));
                        write({conveyor}, 'E');
                        write({conveyor}, 'G');
                        move({conveyor}, distance);
                        write({conveyor}, 'e');
                        write({conveyor}, 'g');
                        move({conveyor}, -distance);
                        markHandoffFree();
                    };
                    purifyInput(step, 4);
                    while (supplyIndex < supply.steps.size() &&
                           (supply.steps[supplyIndex].kind == SupplyPlan::Kind::Store ||
                            supply.steps[supplyIndex].kind == SupplyPlan::Kind::Discard))
                        discard(supply.steps[supplyIndex++]);
                    if (supplyIndex == supply.steps.size() ||
                        supply.steps[supplyIndex].kind != SupplyPlan::Kind::Purify ||
                        supply.steps[supplyIndex].targetAtom != static_cast<std::size_t>(atom))
                        throw std::logic_error("second purification input missing");
                    purifyInput(supply.steps[supplyIndex++], 5);
                    write({conveyor}, 'O');
                    write({conveyor}, 'O');
                    move({conveyor}, 4);
                    write({conveyor}, 'E');
                    write({conveyor}, 'G');
                    write({conveyor}, 'e');
                    move({conveyor}, -4);
                } else {
                    if (step.buffer == -2) {
                        move({mercuryArm}, catalystDistance);
                        write({mercuryArm}, 'g');
                        move({mercuryArm}, -catalystDistance);
                    } else {
                        deliver(slotFor(step));
                        holderRelease = time - 3;
                    }
                    if (!componentRows) {
                        write({conveyor}, 'E');
                        write({conveyor}, 'G');
                        write({conveyor}, 'e');
                    }
                    // The shared pickup is free once the conveyor has retracted
                    // the grabbed atom to its inner ring. Advances the exact
                    // drop bound for the next delivery under the row pipeline.
                    markHandoffFree();
                }
                if (step.conversion == ElementConversion::Duplication) {
                    write({conveyor}, 'R');
                    write({conveyor}, 'R');
                    write({conveyor}, 'r');
                    write({conveyor}, 'r');
                }
                if ((!reactionAssembly || mixedInventory) &&
                    step.conversion == ElementConversion::Calcification) {
                    write({conveyor}, 'r');
                    write({conveyor}, 'R');
                }
                if (step.conversion == ElementConversion::Projection) {
                    write({conveyor}, 'r');
                    const auto levels =
                        static_cast<int>(p.inputs[step.input].atoms[step.sourceAtom].type) -
                        static_cast<int>(target.atoms[atom].type);
                    for (int level = 0; level < levels; ++level) {
                        checkDeadline();
                        while (supplyIndex < supply.steps.size() &&
                               (supply.steps[supplyIndex].kind == SupplyPlan::Kind::Discard ||
                                supply.steps[supplyIndex].kind == SupplyPlan::Kind::Store))
                            discard(supply.steps[supplyIndex++]);
                        if (supplyIndex == supply.steps.size() ||
                            supply.steps[supplyIndex].kind != SupplyPlan::Kind::Reagent ||
                            supply.steps[supplyIndex].targetAtom != static_cast<std::size_t>(atom))
                            throw std::logic_error("projection mercury supply missing");
                        deliver(slotFor(supply.steps[supplyIndex++]));
                        write({mercuryArm}, 'G');
                        write({mercuryArm}, 'r');
                        write({mercuryArm}, 'g');
                        write({mercuryArm}, 'R');
                        markHandoffFree();
                    }
                    write({conveyor}, 'R');
                }
                const int emissionReady = time;
                if (sharedOutputs) {
                    const auto output =
                        std::find_if(sharedOutputs->outputs.begin(), sharedOutputs->outputs.end(),
                                     [&](const auto& molecule) {
                                         return molecule.atoms[0].type == target.atoms[atom].type;
                                     });
                    const int inlet =
                        animismus ? static_cast<int>((supplyIndex - 1) % 2)
                                  : static_cast<int>(output - sharedOutputs->outputs.begin()) * 4;
                    const int distance = feedOffset + reactionQ + inlet;
                    move({conveyor}, distance);
                    write({conveyor}, 'E');
                    const int released = time;
                    write({conveyor}, 'g');
                    write({conveyor}, 'e');
                    move({conveyor}, -distance);
                    if (animismus) {
                        if (emissions)
                            emissions->push_back({0,
                                                  {static_cast<std::size_t>(inlet), 0},
                                                  product,
                                                  {-feedOffset, -1},
                                                  {reactionQ + inlet, 0},
                                                  1,
                                                  emissionBegin,
                                                  holderRelease,
                                                  emissionReady,
                                                  released,
                                                  time});
                        if (inlet == 1) {
                            write({vitaeArm, morsArm}, 'G');
                            write({vitaeArm}, 'r');
                            write({morsArm}, 'R');
                            write({vitaeArm, morsArm}, 'g');
                            write({vitaeArm}, 'R');
                            write({morsArm}, 'r');
                        }
                    }
                    continue;
                }
                if (!componentRows) {
                    const int rowBegin = time;
                    move({conveyor}, feedOffset);
                    write({conveyor}, 'E');
                    write({conveyor}, 'g');
                    write({conveyor}, 'e');
                    phase("row-entry-held", rowBegin, time);
                    const int emptyBegin = time;
                    if (assemblyClocks) {
                        // The atom rests on the row after the release above; the
                        // empty return overlaps the lower-arm grab and bonding.
                        convTime = std::max(convTime, time);
                        convMove(-feedOffset);
                    } else {
                        move({conveyor}, -feedOffset);
                    }
                    phase("row-entry-empty", emptyBegin, time);
                }
                if (emissions) {
                    if (!owners || componentRows || sharedOutputs)
                        throw std::logic_error(
                            "shared factory emission requires ordinary row custody");
                    emissions->push_back({0,
                                          owners->at(atom),
                                          product,
                                          {-feedOffset, -1},
                                          {0, 0},
                                          1,
                                          emissionBegin,
                                          holderRelease,
                                          emissionReady,
                                          emissionReady + feedOffset + 1,
                                          time});
                }
                assembled[atom] = true;
                if (!edge(atom, 1, 0)) {
                    move({lower[current]}, width - 1 - current);
                    write({lower[current]}, 'G');
                }
                move({lower[current]}, 1);
                if (!edge(atom, -1, 0)) {
                    move({lower[current]}, cells[atom].q + 1);
                    --current;
                }
            }
            if (sharedOutputs) continue;
            std::vector<int> active(lower.begin() + current + 1, lower.end());
            // Every atom in this row is now bonded to the held representative.
            // Only the completed connected row crosses the feed/assembly boundary.
            if (componentRows) move(active, componentOffset);
            std::vector<char> program;
            auto scan = [&](int dx, int steps, bool enabled) {
                if (!enabled) {
                    program.push_back('A');
                    return;
                }
                for (int n = 0; n < steps; ++n) {
                    checkDeadline();
                    program.push_back('A');
                    for (int atom : order)
                        if (cells[atom].q == width - 1 - n && edge(atom, dx, 1)) {
                            checkDeadline();
                            program.push_back('e');
                            program.push_back('E');
                        }
                }
            };
            scan(0, width, diagonal60);
            scan(-1, width - 1, diagonal120);
            for (const auto& contact : contacts) {
                checkDeadline();
                int travelled = static_cast<int>(std::count(program.begin(), program.end(), 'A'));
                while (travelled < contact.distance) {
                    checkDeadline();
                    program.push_back('A');
                    ++travelled;
                }
                for (int n = 0; n < width; ++n) {
                    checkDeadline();
                    if (n) program.push_back('A');
                    for (int atom : order)
                        if (cells[atom].q == width - 1 - n) {
                            checkDeadline();
                            const auto from = target.atoms[atom].position;
                            const auto to = core::Add(from, {contact.dx, contact.dy});
                            int bits = 0;
                            for (const auto& bond : labels->bonds)
                                if ((bond.from == from && bond.to == to) ||
                                    (bond.to == from && bond.from == to))
                                    bits = bond.bits;
                            const auto source = std::find_if(
                                labels->atoms.begin(), labels->atoms.end(), [&](const auto& a) {
                                    checkDeadline();
                                    return a.position == from;
                                });
                            const bool firePair =
                                source != labels->atoms.end() &&
                                source->type == core::AtomType::Fire &&
                                std::any_of(
                                    labels->atoms.begin(), labels->atoms.end(), [&](const auto& a) {
                                        return a.position == to && a.type == core::AtomType::Fire;
                                    });
                            if ((contact.bits && bits == contact.bits) ||
                                (!contact.bits && bits == 0 && firePair)) {
                                program.push_back('e');
                                program.push_back('E');
                            }
                        }
                }
            }
            while (!program.empty() && program.back() == 'A')
                program.pop_back();
            int distance = static_cast<int>(std::count(program.begin(), program.end(), 'A'));
            if (row == height - 1) {
                checkDeadline();
                reset(active);
                auto grabs = representatives(row);
                write(grabs, 'e');
                write(grabs, 'G');
                write(grabs, 'E');
                for (char action : program)
                    write(upper, action);
                if (assemblyClocks) {
                    // The upper return travel is independent of the next row's
                    // lower-arm staging; run it on the assembly clock.
                    upperTime = std::max(upperTime, time);
                    upperMove(upper, -distance);
                    if (row > 0)
                        upperWrite(upper, 'E');
                    else
                        finish();
                } else {
                    move(upper, -distance);
                    if (row > 0)
                        write(upper, 'E');
                    else
                        finish();
                }
            } else {
                write(active, 'E');
                auto all = active;
                all.insert(all.end(), upper.begin(), upper.end());
                for (char action : program)
                    write(all, action);
                if (assemblyClocks && row == 0) {
                    // Final upper return overlaps the lower reset; finish joins.
                    upperTime = std::max(upperTime, time);
                    upperMove(upper, -distance);
                }
                reset(active);
                if (row > 0) {
                    checkDeadline();
                    if (assemblyClocks) {
                        upperTime = std::max(upperTime, time);
                        upperWrite(upper, 'g');
                        upperWrite(upper, 'e');
                        upperWrite(representatives(row), 'G');
                        upperMove(upper, -distance);
                        upperWrite(upper, 'E');
                    } else {
                        write(upper, 'g');
                        write(upper, 'e');
                        write(representatives(row), 'G');
                        move(upper, -distance);
                        write(upper, 'E');
                    }
                } else {
                    if (productOrigins) {
                        // Products starting on the bottom row have no previous upper holder.
                        write(upper, 'g');
                        write(upper, 'e');
                        write(representatives(row), 'G');
                    }
                    if (!assemblyClocks) move(upper, -distance);
                    finish();
                }
            }
        }
        if (productEnds) productEnds->push_back(time);
        // Restore the intake's initial state so the repeating tape is neutral.
        if (directInput) {
            move({intakeArm}, -intakeIndex);
            intakeIndex = 0;
            while (lengths[intakeArm] > 1)
                write({intakeArm}, 'e');
            // Optional inter-period clearance for the row arms. Nothing is
            // emitted here, so the next period simply starts later and every
            // arm keeps its relative phase inside its own block.
            if (intakeGap > 0) time += intakeGap;
        }
    }
    while (supplyIndex < supply.steps.size()) {
        checkDeadline();
        const auto& step = supply.steps[supplyIndex++];
        if (step.kind != SupplyPlan::Kind::Discard)
            throw std::logic_error("unconsumed supply demand");
        discard(step);
    }
    awaitFeed();
    if (displacement[feedArm]) feedMove(-displacement[feedArm]);
    // The feed tape may end after the last shared tick only when the opt-in
    // clock ran ahead; the terminal marker must still cover every real action.
    if (feedPrefetch) awaitFeed();
    // Join any upper-arm or conveyor tail clock before the terminal marker.
    awaitUpper();
    awaitConv();
    // Finite parking tapes must stop; closed inventories keep a common repeated period.
    for (auto& obj : result.objects)
        if (!obj.instructions.empty())
            obj.instructions.push_back({boundedWaste || finiteProducts ? 'B' : 'g', time});
    const auto cycles = (static_cast<std::int64_t>(time) + 1) *
                        (boundedWaste || finiteProducts ? 1 : 6 * p.outputMultiplier);
    if (cycles > std::numeric_limits<std::int32_t>::max())
        throw std::invalid_argument(
            "row assembly cycle estimate exceeds the solution format limit");
    if (retimeEmptyResets && requireRetimingOverlap && !resetRetimed)
        throw std::invalid_argument("empty-reset dependency graph has no overlap");
    result.metrics.cycles = static_cast<std::int32_t>(cycles);
    checkDeadline();
    if (completionTransfer) {
        {
            row_profile::Scope profile(row_profile::Observer);
            completionTransfer->partialMachine = result;
        }
        {
            row_profile::Scope profile(row_profile::Gate);
            completionTransfer->verification = completionTransfer->nativeGate(result, deadline);
        }
        const auto& report = completionTransfer->verification;
        if (!report.Passed() || report.authority != io::VerificationAuthority::LegacyGate) {
            completionTransfer->failure =
                "native gate rejected complete row transfer: " + report.detail;
            throw IR::LayoutFailure(completionTransfer->failure);
        }
        checkDeadline();
        if (completionTransfer->accept && !completionTransfer->accept(result))
            throw IR::LayoutFailure("native row transfer acceptance rejected");
        completionTransfer->published = true;
    }
    if (publishedMixed) {
        publishedMixed->phases = std::move(stagedMixed.phases);
        publishedMixed->emptyReturnTasks = stagedMixed.emptyReturnTasks;
        publishedMixed->emptyReturnCycles = stagedMixed.emptyReturnCycles;
    }
    if (publishedCustody)
        publishedCustody->insert(publishedCustody->end(), stagedCustody.begin(),
                                 stagedCustody.end());
    if (publishedEmissions)
        publishedEmissions->insert(publishedEmissions->end(), stagedEmissions.begin(),
                                   stagedEmissions.end());
    if (publishedEnds)
        publishedEnds->insert(publishedEnds->end(), stagedEnds.begin(), stagedEnds.end());
    return result;
}

io::SolutionData CompileRows(
    const io::PuzzleData& p, const AssemblyPlan& plan, const io::PuzzleData* sharedOutputs,
    const io::Molecule* labels, const std::vector<core::HexCoord>* productOrigins,
    bool postCalcification, std::vector<int>* productEnds, bool singleProduct,
    std::size_t finiteProducts, const SupplyPlan* supplied, const io::PuzzleData* validationPuzzle,
    int feedSpacing, bool retimeEmptyResets, bool boundEscapeStation, bool closeFeedBank,
    bool componentRows, std::vector<SharedFactoryEmission>* emissions,
    const std::vector<ProductAtomRef>* owners, bool animismus,
    const io::PuzzleData* reactionAssembly, const MixedInventoryProgram* mixedInventory,
    std::chrono::steady_clock::time_point deadline,
    const std::function<std::chrono::steady_clock::time_point()>& clock,
    std::vector<FragmentCustodyEvent>* custody, RowCompletionTransfer* completionTransfer) {
    return CompileRowsWithRetimingPolicy(
        p, plan, sharedOutputs, labels, productOrigins, postCalcification, productEnds,
        singleProduct, finiteProducts, supplied, validationPuzzle, feedSpacing, retimeEmptyResets,
        boundEscapeStation, closeFeedBank, componentRows, emissions, owners, animismus,
        reactionAssembly, mixedInventory, deadline, clock, custody, completionTransfer, true, false,
        false, -1);
}

static io::SolutionData CompileScheduledRowOnce(const io::PuzzleData& puzzle,
                                                const AssemblyPlan& plan,
                                                std::chrono::steady_clock::time_point deadline,
                                                bool parallelClocks, int compactGapOverride) {
    // Diagnostic switch: the empty-reset retiming pass picks its overlap from
    // absolute idle gaps, so it can react to timing changes elsewhere in the
    // machine. Set OPUSOPTIMA_ROW_NO_RETIME=on to measure the same geometry
    // without that pass.
    const bool retimeEmptyResets = [] {
        const char* flag = std::getenv("OPUSOPTIMA_ROW_NO_RETIME");
        return !(flag && std::string_view(flag) == "on");
    }();
    return CompileRowsWithRetimingPolicy(
        puzzle, plan, nullptr, nullptr, nullptr, true, nullptr, false, 0, nullptr, nullptr, 3,
        retimeEmptyResets, false, false, false, nullptr, nullptr, false, nullptr, nullptr, deadline,
        {}, nullptr, nullptr, false, true, parallelClocks, compactGapOverride);
}

// The clocked companion is a second full compile. Keep it inside the same
// bounded ordinary-row scope as the compact layout; large workshop rows would
// consume the fixed 20 s budget and crowd out the existing shared candidate.
static bool ScheduledRowClockWithinBudget(const io::PuzzleData& puzzle) {
    if (puzzle.outputs.size() != 1 || puzzle.outputs[0].atoms.empty()) return false;
    int minQ = 128, maxQ = -128, minR = 128, maxR = -128;
    for (const auto& atom : puzzle.outputs[0].atoms) {
        minQ = std::min(minQ, atom.position.q);
        maxQ = std::max(maxQ, atom.position.q);
        minR = std::min(minR, atom.position.r);
        maxR = std::max(maxR, atom.position.r);
    }
    return maxQ - minQ + 1 <= 16 && maxR - minR + 1 <= 16;
}

io::SolutionData CompileScheduledRowAssembly(const io::PuzzleData& puzzle, const AssemblyPlan& plan,
                                             std::chrono::steady_clock::time_point deadline) {
    return CompileScheduledRowOnce(puzzle, plan, deadline, false, -1);
}

io::SolutionData CompileScheduledRowIntakeAssembly(const io::PuzzleData& puzzle,
                                                   const AssemblyPlan& plan,
                                                   std::chrono::steady_clock::time_point deadline) {
    // A separate candidate: it never replaces the shared-feeder machine, so a
    // native rejection here cannot cost the portfolio its proven row machine.
    const int saved = g_intakeMode;
    g_intakeMode = 1;
    try {
        auto machine = CompileScheduledRowOnce(puzzle, plan, deadline, false, -1);
        g_intakeMode = saved;
        return machine;
    } catch (...) {
        g_intakeMode = saved;
        throw;
    }
}

// Upstream scheduling contract (C3): components emit relative fragments and the
// builder merges them per arm, so an emitted tape is only well formed when each
// arm's instructions sit on distinct, increasing ticks. Replaying every tape
// through the builder either places each instruction on its own tick or has to
// displace it, which is exactly what this audit reports.
ScheduleAudit AuditMachineSchedule(const io::SolutionData& machine) {
    ScheduleAudit audit;
    ProgramBuilder builder;
    audit.period = 0;
    for (std::size_t i = 0; i < machine.objects.size(); ++i) {
        const auto& object = machine.objects[i];
        if (object.instructions.empty()) continue;
        ++audit.arms;
        std::int64_t previous = -1;
        for (const auto& instruction : object.instructions) {
            const auto tick = static_cast<std::int64_t>(instruction.parameter);
            if (tick < previous) audit.monotone = false;
            if (tick == previous) audit.uniqueTicks = false;
            previous = tick;
            audit.period = std::max(audit.period, tick + 1);
            ++audit.instructions;
        }
        opusoptima::solvers::ProgramFragment fragment;
        fragment.arm = object.typeName + "#" + std::to_string(i);
        for (const auto& instruction : object.instructions)
            fragment.instructions.push_back(
                {instruction.action, static_cast<std::int64_t>(instruction.parameter)});
        // Anchor the fragment at the tape's own first tick: the merge may only
        // shorten the gap before a fragment, never move an emitted action.
        fragment.anchor = object.instructions.empty()
                              ? std::int64_t{0}
                              : static_cast<std::int64_t>(object.instructions.front().parameter);
        const auto originalSize = fragment.instructions.size();
        try {
            builder.Add(std::move(fragment));
        } catch (const std::invalid_argument&) {
            audit.displaced = true;
            continue;
        }
        const auto& placed = builder.Arm(object.typeName + "#" + std::to_string(i));
        if (placed.size() != originalSize) {
            audit.displaced = true;
            continue;
        }
        for (std::size_t k = 0; k < placed.size(); ++k) {
            if (placed[k].action != object.instructions[k].action ||
                placed[k].tick != static_cast<std::int64_t>(object.instructions[k].parameter)) {
                audit.displaced = true;
                break;
            }
        }
    }
    return audit;
}

io::SolutionData CompileScheduledRowFeederAssembly(const io::PuzzleData& puzzle,
                                                   const AssemblyPlan& plan,
                                                   std::chrono::steady_clock::time_point deadline,
                                                   int gapOverride) {
    const int saved = g_intakeMode;
    g_intakeMode = -1;
    try {
        auto machine = CompileScheduledRowOnce(puzzle, plan, deadline, false, gapOverride);
        g_intakeMode = saved;
        return machine;
    } catch (...) {
        g_intakeMode = saved;
        throw;
    }
}

io::SolutionData CompileScheduledRowFeederClockAssembly(
    const io::PuzzleData& puzzle, const AssemblyPlan& plan,
    std::chrono::steady_clock::time_point deadline, int gapOverride) {
    const int saved = g_intakeMode;
    g_intakeMode = -1;
    try {
        auto machine = CompileScheduledRowOnce(puzzle, plan, deadline, true, gapOverride);
        g_intakeMode = saved;
        return machine;
    } catch (...) {
        g_intakeMode = saved;
        throw;
    }
}

bool ScheduledRowIntakeApplied() {
    return g_intakeApplied;
}

RowIntakeModeScope::RowIntakeModeScope(int mode) : previous_(g_intakeMode) {
    g_intakeMode = mode;
}

RowIntakeModeScope::~RowIntakeModeScope() {
    g_intakeMode = previous_;
}

RowClockScope::RowClockScope(int mode) : previous_(g_rowClockMode) {
    g_rowClockMode = mode;
}

RowClockScope::~RowClockScope() {
    g_rowClockMode = previous_;
}
FiniteClockScope::FiniteClockScope(int mode) : previous_(g_rowFiniteClockOptIn) {
    g_rowFiniteClockOptIn = mode;
}
FiniteClockScope::~FiniteClockScope() {
    g_rowFiniteClockOptIn = previous_;
}

io::SolutionData CompileScheduledRowClockAssembly(const io::PuzzleData& puzzle,
                                                  const AssemblyPlan& plan,
                                                  std::chrono::steady_clock::time_point deadline) {
    return CompileScheduledRowOnce(puzzle, plan, deadline, true, -1);
}

ScheduledRowCandidates CompileScheduledRowCandidates(
    const io::PuzzleData& puzzle, const AssemblyPlan& plan,
    std::chrono::steady_clock::time_point deadline) {
    ScheduledRowCandidates result;
    const auto start = std::chrono::steady_clock::now();
    result.compact = CompileScheduledRowOnce(puzzle, plan, deadline, false, -1);
    result.compactUsesIntake = g_intakeApplied;
    const auto compactEnd = std::chrono::steady_clock::now();
    const auto compactCost = compactEnd - start;
    if (std::getenv("OPUSOPTIMA_DEBUG_INTAKE"))
        std::fprintf(
            stderr, "[intake] compactCost=%lldms\n",
            static_cast<long long>(
                std::chrono::duration_cast<std::chrono::milliseconds>(compactCost).count()));
    // Reserve the same amount again for every optional compile; otherwise the
    // caller cannot submit the compact parent before its deadline.
    if (ScheduledRowClockWithinBudget(puzzle)) {
        auto hasBudget = [&] { return std::chrono::steady_clock::now() + compactCost < deadline; };
        if (hasBudget()) {
            try {
                result.compactGap0 = CompileScheduledRowOnce(puzzle, plan, deadline, false, 0);
            } catch (const std::exception&) {
                result.compactGap0.reset();
            }
        }
        // The shared-feeder companion has a different tape set than the intake
        // machine, so both are built up front; the portfolio then compares their
        // slots instead of spending a late compile that may miss the deadline.
        // Only small rows can afford the extra machine: on large ones the intake
        // clocked machine is skipped entirely and the proven feeder is used.
        const bool roomForCompanion = compactCost < std::chrono::milliseconds(500) &&
                                      std::chrono::steady_clock::now() + 2 * compactCost < deadline;
        const char* gapFlag = std::getenv("OPUSOPTIMA_ROW_GAP_PORTFOLIO");
        const bool gapEnabled = !gapFlag || std::string_view(gapFlag) != "off";
        const char* clockFlag = std::getenv("OPUSOPTIMA_ROW_CLOCK_PORTFOLIO");
        const bool clockEnabled = !clockFlag || std::string_view(clockFlag) != "off";
        if (result.compactUsesIntake && roomForCompanion && hasBudget()) {
            try {
                result.compactFeeder =
                    CompileScheduledRowFeederAssembly(puzzle, plan, deadline, gapEnabled ? 0 : -1);
            } catch (const std::exception&) {
                result.compactFeeder.reset();
            }
        }
        if (hasBudget()) {
            try {
                result.clocked = (result.compactUsesIntake && !roomForCompanion)
                                     ? CompileScheduledRowFeederClockAssembly(
                                           puzzle, plan, deadline, clockEnabled ? 0 : -1)
                                     : CompileScheduledRowOnce(puzzle, plan, deadline, true, -1);
                result.hasClocked = !result.clocked.objects.empty();
                result.clockedUsesIntake = g_intakeApplied;
            } catch (const std::exception&) {
                // The compact parent remains valid; the clocked companion is optional.
                result.hasClocked = false;
            }
        }
        if (hasBudget()) {
            try {
                result.clockedGap0 = CompileScheduledRowOnce(puzzle, plan, deadline, true, 0);
            } catch (const std::exception&) {
                result.clockedGap0.reset();
            }
        }
        if (result.clockedUsesIntake && roomForCompanion && hasBudget()) {
            try {
                result.clockedFeeder =
                    CompileScheduledRowFeederClockAssembly(puzzle, plan, deadline, 0);
            } catch (const std::exception&) {
                result.clockedFeeder.reset();
            }
        }
    }
    return result;
}

bool SupportsFiniteWheelSecondSource(const io::PuzzleData& p) {
    using T = core::AtomType;
    const char* retention = std::getenv("OPUSOPTIMA_FINITE_RETENTION");
    if (!retention || std::string(retention) != "1") return false;
    constexpr auto required = AssemblyPlanner::RequiredPermissions | (1ULL << 2) | (1ULL << 3) |
                              (1ULL << 12) | (1ULL << 13) | (1ULL << 16) | (1ULL << 28);
    if (p.version != 3 || p.productionInfo || p.outputMultiplier != 1 || p.inputs.size() != 2 ||
        p.outputs.size() != 1 || p.outputs[0].atoms.empty() || p.outputs[0].atoms.size() > 16 ||
        (p.permissionBits & required) != required)
        return false;
    auto cardinal = [](T type) { return type >= T::Air && type <= T::Water; };
    int cardinalSources = 0, mercurySources = 0;
    for (const auto& input : p.inputs) {
        if (input.atoms.size() != 1 || !input.bonds.empty()) return false;
        if (cardinal(input.atoms[0].type))
            ++cardinalSources;
        else if (input.atoms[0].type == T::Quicksilver)
            ++mercurySources;
        else
            return false;
    }
    if (cardinalSources != 1 || mercurySources != 1) return false;
    int vitae = 0, mors = 0, mercury = 0, derived = 0;
    T wheelElement = T::Salt;
    for (const auto& atom : p.outputs[0].atoms) {
        if (cardinal(atom.type)) {
            if (wheelElement != T::Salt && wheelElement != atom.type) return false;
            wheelElement = atom.type;
            ++derived;
        } else if (atom.type == T::Vitae)
            ++vitae;
        else if (atom.type == T::Mors)
            ++mors;
        else if (atom.type == T::Quicksilver)
            ++mercury;
        else
            return false;
    }
    for (const auto& bond : p.outputs[0].bonds)
        if (bond.bits != 1) return false;
    return derived > 0 && mercury > 0 && mors == vitae + 1;
}

io::SolutionData CompileFiniteWheelSecondSource(
    const io::PuzzleData& actual, std::chrono::steady_clock::time_point deadline,
    const std::function<io::VerificationReport(
        const io::SolutionData&, std::chrono::steady_clock::time_point)>& nativeGate) {
    if (!SupportsFiniteWheelSecondSource(actual))
        throw std::invalid_argument("finite wheel second source bounds or retention policy");
    if (std::chrono::steady_clock::now() >= deadline)
        throw std::invalid_argument("finite wheel second source deadline");
    auto proxy = actual;
    for (auto& atom : proxy.outputs[0].atoms)
        if (atom.type != core::AtomType::Quicksilver) atom.type = core::AtomType::Salt;
    RowCompletionTransfer transfer;
    transfer.coproductAssembly = transfer.surplusRetention = true;
    transfer.secondSingletonSource = transfer.wheelSecondSingletonSource = true;
    transfer.mode = io::VerificationMode::LegacyGate;
    transfer.nativeGate = nativeGate;
    const auto plan = AssemblyPlanner::Plan(proxy, false, deadline);
    auto result = CompileRows(proxy, plan, nullptr, nullptr, nullptr, true, nullptr, false, 6,
                              nullptr, nullptr, 3, false, false, false, false, nullptr, nullptr,
                              false, &actual, nullptr, deadline, {}, nullptr, &transfer);
    result.metrics = transfer.verification.metrics;
    return result;
}

io::SolutionData CompileSharedPurificationAssembly(const io::PuzzleData& puzzle) {
    if (puzzle.productionInfo || puzzle.outputs.size() != 1 ||
        !(puzzle.permissionBits & (1ULL << 15)))
        throw std::invalid_argument(
            "shared purification requires a finite output and purification permission");
    auto proxy = puzzle;
    bool changed = false;
    for (auto& atom : proxy.outputs[0].atoms) {
        const bool present =
            std::any_of(puzzle.inputs.begin(), puzzle.inputs.end(), [&](const auto& input) {
                return std::any_of(input.atoms.begin(), input.atoms.end(),
                                   [&](const auto& source) { return source.type == atom.type; });
            });
        if (!present && atom.type >= core::AtomType::Gold && atom.type < core::AtomType::Lead) {
            atom.type = static_cast<core::AtomType>(static_cast<int>(atom.type) + 1);
            changed = true;
        }
    }
    if (!changed)
        throw std::invalid_argument("shared purification needs a missing single-stage metal");
    const auto plan = AssemblyPlanner::Plan(proxy);
    InventorySearchLimits limits;
    limits.singleStagePurification = true;
    for (std::size_t products : {1U, 2U, 3U, 6U}) {
        const auto supply = SearchSupplyInventory(puzzle, products, limits);
        limits.maxStates -= std::min(limits.maxStates, supply.states);
        if (supply.status != InventorySearchStatus::Found ||
            std::none_of(supply.plan.steps.begin(), supply.plan.steps.end(),
                         [](const auto& step) { return step.kind == SupplyPlan::Kind::Purify; }))
            continue;
        return CompileRows(puzzle, plan, nullptr, nullptr, nullptr, true, nullptr, false, 0,
                           &supply.plan, &proxy);
    }
    throw std::invalid_argument("shared purification inventory cannot close");
}

io::SolutionData CompileRowAssembly(const io::PuzzleData& puzzle, const AssemblyPlan& plan,
                                    const io::PuzzleData* sharedOutputs) {
    return CompileRows(puzzle, plan, sharedOutputs);
}

io::SolutionData CompileRowAssembly(const io::PuzzleData& puzzle, const AssemblyPlan& plan,
                                    int feedSpacing) {
    if (feedSpacing < 3) throw std::invalid_argument("feed spacing must be at least three cells");
    return CompileRows(puzzle, plan, nullptr, nullptr, nullptr, true, nullptr, false, 0, nullptr,
                       nullptr, feedSpacing);
}

io::SolutionData CompileRowAssembly(const io::PuzzleData& puzzle, const AssemblyPlan& plan) {
    return CompileRows(puzzle, plan);
}

io::SolutionData CompileRowAssembly(const io::PuzzleData& puzzle, const AssemblyPlan& plan,
                                    std::chrono::steady_clock::time_point deadline) {
    return CompileRowsWithRetimingPolicy(puzzle, plan, nullptr, nullptr, nullptr, true, nullptr,
                                         false, 0, nullptr, nullptr, 3, false, false, false, false,
                                         nullptr, nullptr, false, nullptr, nullptr, deadline, {},
                                         nullptr, nullptr, true, false, false, -1);
}

bool SupportsComponentRowAssembly(const io::PuzzleData& puzzle) {
    constexpr auto permissions = (1ULL << 2) | (1ULL << 3);
    if (puzzle.productionInfo || puzzle.outputs.size() != 1 ||
        (puzzle.permissionBits & permissions) != permissions ||
        (puzzle.permissionBits & (1ULL << 17)) || puzzle.outputs[0].atoms.empty() ||
        std::any_of(puzzle.inputs.begin(), puzzle.inputs.end(), [](const auto& input) {
            return input.atoms.size() != 1 || !input.bonds.empty();
        }))
        return false;
    const auto& target = puzzle.outputs[0];
    int minQ = target.atoms[0].position.q, maxQ = minQ;
    int minR = target.atoms[0].position.r, maxR = minR;
    for (const auto& atom : target.atoms) {
        minQ = std::min(minQ, atom.position.q);
        maxQ = std::max(maxQ, atom.position.q);
        minR = std::min(minR, atom.position.r);
        maxR = std::max(maxR, atom.position.r);
        if (std::none_of(puzzle.inputs.begin(), puzzle.inputs.end(),
                         [&](const auto& input) { return input.atoms[0].type == atom.type; }))
            return false;
    }
    const int width = maxQ - minQ + 1, height = maxR - minR + 1;
    if (width < 2 || width > 8 || height > 2 ||
        target.atoms.size() != static_cast<std::size_t>(width * height))
        return false;
    if (std::any_of(target.bonds.begin(), target.bonds.end(),
                    [](const auto& bond) { return bond.bits != 1; }))
        return false;
    for (const auto& atom : target.atoms) {
        if (atom.position.q == maxQ) continue;
        const core::HexCoord neighbor{atom.position.q + 1, atom.position.r};
        if (std::none_of(target.bonds.begin(), target.bonds.end(), [&](const auto& bond) {
                return (bond.from == atom.position && bond.to == neighbor) ||
                       (bond.to == atom.position && bond.from == neighbor);
            }))
            return false;
    }
    return true;
}

io::SolutionData CompileComponentRowAssembly(const io::PuzzleData& puzzle,
                                             const AssemblyPlan& plan) {
    if (!SupportsComponentRowAssembly(puzzle))
        throw std::invalid_argument("outside registered component-row domain");
    return CompileRows(puzzle, plan, nullptr, nullptr, nullptr, true, nullptr, false, 0, nullptr,
                       nullptr, 3, false, false, false, true);
}

io::SolutionData CompileCloseFeedRowAssembly(const io::PuzzleData& puzzle,
                                             const AssemblyPlan& plan) {
    return CompileRows(puzzle, plan, nullptr, nullptr, nullptr, true, nullptr, false, 0, nullptr,
                       nullptr, 3, false, true, true);
}

io::SolutionData CompileStructuredRowAssembly(const io::PuzzleData& puzzle,
                                              const AssemblyPlan& plan) {
    return CompileRows(puzzle, plan, nullptr, nullptr, nullptr, true, nullptr, false, 0, nullptr,
                       nullptr, 3, false, true);
}

io::SolutionData CompileRetimedRowAssembly(const io::PuzzleData& puzzle, const AssemblyPlan& plan) {
    return CompileRows(puzzle, plan, nullptr, nullptr, nullptr, true, nullptr, false, 0, nullptr,
                       nullptr, 3, true);
}

io::SolutionData CompileRowAssemblySingleProduct(const io::PuzzleData& puzzle,
                                                 const AssemblyPlan& plan) {
    return CompileRowAssemblySingleProduct(puzzle, plan,
                                           std::chrono::steady_clock::time_point::max());
}

io::SolutionData CompileRowAssemblySingleProduct(const io::PuzzleData& puzzle,
                                                 const AssemblyPlan& plan,
                                                 std::chrono::steady_clock::time_point deadline) {
    return CompileRows(puzzle, plan, nullptr, nullptr, nullptr, true, nullptr, true, 0, nullptr,
                       nullptr, 3, false, false, false, false, nullptr, nullptr, false, nullptr,
                       nullptr, deadline, {}, nullptr, nullptr);
}

io::SolutionData CompileRowAssemblyBatch(const io::PuzzleData& puzzle, const AssemblyPlan& plan,
                                         std::vector<int>& productEnds) {
    return CompileRowAssemblyBatch(puzzle, plan, productEnds,
                                   std::chrono::steady_clock::time_point::max());
}

io::SolutionData CompileRowAssemblyBatch(const io::PuzzleData& puzzle, const AssemblyPlan& plan,
                                         std::vector<int>& productEnds,
                                         std::chrono::steady_clock::time_point deadline) {
    productEnds.clear();
    return CompileRows(puzzle, plan, nullptr, nullptr, nullptr, true, &productEnds, false, 0,
                       nullptr, nullptr, 3, false, false, false, false, nullptr, nullptr, false,
                       nullptr, nullptr, deadline, {}, nullptr, nullptr);
}

io::SolutionData CompileRowAssemblyFiniteBatch(const io::PuzzleData& puzzle,
                                               const AssemblyPlan& plan,
                                               std::vector<int>& productEnds,
                                               std::size_t products) {
    return CompileRowAssemblyFiniteBatch(puzzle, plan, productEnds, products,
                                         std::chrono::steady_clock::time_point::max());
}

io::SolutionData CompileRowAssemblyFiniteBatch(const io::PuzzleData& puzzle,
                                               const AssemblyPlan& plan,
                                               std::vector<int>& productEnds, std::size_t products,
                                               std::chrono::steady_clock::time_point deadline) {
    if (!products) throw std::invalid_argument("finite feed requires a positive product count");
    productEnds.clear();
    return CompileRowsWithRetimingPolicy(
        puzzle, plan, nullptr, nullptr, nullptr, true, &productEnds, false, products, nullptr,
        nullptr, 3, false, false, false, false, nullptr, nullptr, false, nullptr, nullptr, deadline,
        {}, nullptr, nullptr, true, false, false, -1);
}

io::SolutionData CompileTriplexRowAssembly(const io::PuzzleData& puzzle, const AssemblyPlan& plan,
                                           const io::Molecule& labels, bool postCalcification,
                                           int feedSpacing) {
    return CompileTriplexRowAssembly(puzzle, plan, labels, postCalcification, feedSpacing,
                                     std::chrono::steady_clock::time_point::max());
}

io::SolutionData CompileTriplexRowAssembly(const io::PuzzleData& puzzle, const AssemblyPlan& plan,
                                           const io::Molecule& labels, bool postCalcification,
                                           int feedSpacing,
                                           std::chrono::steady_clock::time_point deadline) {
    if (feedSpacing < 3) throw std::invalid_argument("feed spacing must be at least three cells");
    return CompileRows(puzzle, plan, nullptr, &labels, nullptr, postCalcification, nullptr, false,
                       0, nullptr, nullptr, feedSpacing, false, false, false, false, nullptr,
                       nullptr, false, nullptr, nullptr, deadline, {}, nullptr, nullptr);
}

SharedAssemblyTarget BuildSharedAssemblyTarget(const io::PuzzleData& puzzle) {
    if (puzzle.productionInfo || puzzle.outputs.size() < 2)
        throw std::invalid_argument("shared ordinary assembly requires multiple finite outputs");
    SharedAssemblyTarget shared{puzzle, {}, {}};
    auto& combined = shared.puzzle;
    combined.outputs.resize(1);
    auto& target = combined.outputs[0];
    target.atoms.clear();
    target.bonds.clear();
    auto& origins = shared.origins;
    int cursor = 0;
    for (std::size_t product = 0; product < puzzle.outputs.size(); ++product) {
        const auto& output = puzzle.outputs[product];
        auto single = puzzle;
        single.outputs = {output};
        single.permissionBits |= (1ULL << 17);
        (void)AssemblyPlanner::Plan(single);
        if (output.atoms.empty()) throw std::invalid_argument("empty shared product");
        int minQ = 128, minR = 128, maxQ = -128, maxR = -128;
        for (const auto& atom : output.atoms) {
            minQ = std::min(minQ, atom.position.q);
            maxQ = std::max(maxQ, atom.position.q);
            minR = std::min(minR, atom.position.r);
            maxR = std::max(maxR, atom.position.r);
        }
        // Bottom alignment lets shorter products enter the scan later while all
        // started products remain represented on each subsequent row.
        const core::HexCoord origin{cursor - minQ, -minR};
        origins.push_back(origin);
        for (std::size_t i = 0; i < output.atoms.size(); ++i) {
            auto atom = output.atoms[i];
            atom.position = core::Add(atom.position, origin);
            target.atoms.push_back(atom);
            shared.atoms.push_back({product, i});
        }
        for (auto bond : output.bonds) {
            bond.from = core::Add(bond.from, origin);
            bond.to = core::Add(bond.to, origin);
            target.bonds.push_back(bond);
        }
        cursor += maxQ - minQ + 4;
    }
    (void)AssemblyPlanner::Plan(combined, true);
    return shared;
}

SharedFactoryBatch CompileSharedAnimismusBatch(const io::PuzzleData& puzzle) {
    using K = ExecutionIR::Kind;
    using T = core::AtomType;
    const auto required = (1ULL << 2) | (1ULL << 3) | (1ULL << 9) | (1ULL << 12) | (1ULL << 16);
    if (puzzle.productionInfo || puzzle.outputMultiplier != 1 || puzzle.inputs.size() != 1 ||
        !SupportsMolecularSupplySource(puzzle.inputs[0]) || puzzle.outputs.size() != 2 ||
        (puzzle.permissionBits & required) != required)
        throw std::invalid_argument(
            "closed animismus requires permitted cardinal diatom and two products");
    std::set<T> products;
    for (const auto& output : puzzle.outputs) {
        if (output.atoms.size() != 1 || !output.bonds.empty())
            throw std::invalid_argument("closed animismus requires singleton products");
        products.insert(output.atoms[0].type);
    }
    if (products != std::set<T>{T::Vitae, T::Mors})
        throw std::invalid_argument("closed animismus requires both distinct coproduct demands");
    auto proxy = puzzle;
    for (auto& output : proxy.outputs)
        output.atoms[0].type = T::Salt;
    const auto shared = BuildSharedAssemblyTarget(proxy);
    const auto plan = AssemblyPlanner::Plan(shared.puzzle, true);
    const auto supply = AssemblyPlanner::PlanSupply(shared.puzzle, true);
    if (supply.productsPerPeriod != 1 || supply.steps.size() != 2 ||
        supply.steps[0].sourceAtom == supply.steps[1].sourceAtom)
        throw std::invalid_argument("closed animismus requires exhaustive two endpoint supply");
    for (const auto& step : supply.steps)
        if (step.kind != SupplyPlan::Kind::Take || step.input != 0 || step.buffer != -1 ||
            step.conversion != ElementConversion::Calcification)
            throw std::invalid_argument("closed animismus forbids synthetic or buffered feed");
    SharedFactoryBatch result;
    result.solution = CompileRows(shared.puzzle, plan, &puzzle, nullptr, nullptr, true, nullptr,
                                  false, 6, &supply, nullptr, 3, false, false, false, false,
                                  &result.emissions, nullptr, true);
    result.solution.puzzleName = puzzle.name;
    result.solution.solutionName = "Experimental closed shared Animismus";
    auto state = ExecutionIR::Initial(puzzle);
    for (std::size_t batch = 0; batch < 6; ++batch) {
        const auto first = state.nextId;
        result.ledger.AppendFromTail(puzzle, state, {K::Acquire, 0});
        result.ledger.AppendFromTail(puzzle, state, {K::Unbond, 0, {first, first + 1}, 1});
        std::vector<ExecutionIR::AtomId> salts;
        for (std::size_t i = 0; i < 2; ++i) {
            auto& emission = result.emissions.at(batch * 2 + i);
            const auto source = supply.steps[i].sourceAtom;
            emission.sourceIdentity = first + source;
            emission.sourceAtom = source;
            emission.sourceBatch = batch;
            emission.sourceRemainder = 1 - i;
            emission.atom = state.nextId;
            salts.push_back(state.nextId);
            result.ledger.AppendFromTail(puzzle, state, {K::Calcify, 0, {first + source}});
        }
        const auto vitae = state.nextId;
        result.ledger.AppendFromTail(puzzle, state, {K::Animismus, 0, salts});
        for (std::size_t output = 0; output < 2; ++output) {
            const auto id = puzzle.outputs[output].atoms[0].type == T::Vitae ? vitae : vitae + 1;
            result.ledger.AppendFromTail(puzzle, state,
                                         {K::Relocate,
                                          0,
                                          {id},
                                          0,
                                          0,
                                          core::Subtract(puzzle.outputs[output].atoms[0].position,
                                                         state.atoms.at(id).position)});
            result.ledger.AppendFromTail(puzzle, state, {K::Output, output, {id}});
        }
    }
    ExecutionIR::RequireComplete(result.ledger.Replay(puzzle), {6, 6});
    return result;
}

SharedFactoryBatch CompileSharedAnimismusAssembly(const io::PuzzleData& puzzle) {
    using T = core::AtomType;
    using K = ExecutionIR::Kind;
    const auto required = (1ULL << 2) | (1ULL << 3) | (1ULL << 9) | (1ULL << 12) | (1ULL << 16);
    if (puzzle.productionInfo || puzzle.outputMultiplier != 1 || puzzle.inputs.size() != 1 ||
        !SupportsMolecularSupplySource(puzzle.inputs[0]) || puzzle.outputs.size() != 2 ||
        (puzzle.permissionBits & required) != required)
        throw std::invalid_argument(
            "shared assembly requires permitted cardinal diatom and two products");
    int balance = 0;
    for (const auto& output : puzzle.outputs) {
        if (output.atoms.size() < 2)
            throw std::invalid_argument("shared assembly requires multiatom consumers");
        for (const auto& atom : output.atoms) {
            if (atom.type != T::Vitae && atom.type != T::Mors)
                throw std::invalid_argument("shared assembly has unsupported retained chemistry");
            balance += atom.type == T::Vitae ? 1 : -1;
        }
    }
    if (balance) throw std::invalid_argument("shared assembly requires balanced complete demand");
    auto saltPuzzle = puzzle;
    for (auto& output : saltPuzzle.outputs)
        for (auto& atom : output.atoms)
            atom.type = T::Salt;
    auto actual = BuildSharedAssemblyTarget(saltPuzzle);
    auto proxy = actual.puzzle;
    for (std::size_t atom = 0; atom < actual.atoms.size(); ++atom) {
        const auto ref = actual.atoms[atom];
        actual.puzzle.outputs[0].atoms[atom].type = puzzle.outputs[ref.output].atoms[ref.atom].type;
    }
    const auto plan = AssemblyPlanner::Plan(proxy, true);
    const auto supply = AssemblyPlanner::PlanSupply(proxy, true);
    if (supply.productsPerPeriod != 1)
        throw std::invalid_argument("shared assembly requires closed supply period");
    for (const auto& step : supply.steps)
        if (step.kind != SupplyPlan::Kind::Take || step.input != 0 || step.buffer != -1 ||
            step.conversion != ElementConversion::Calcification)
            throw std::invalid_argument("shared assembly forbids synthetic or surplus source");
    SharedFactoryBatch result;
    result.solution = CompileRows(proxy, plan, nullptr, nullptr, &actual.origins, true, nullptr,
                                  false, 6, &supply, nullptr, 3, false, false, false, false,
                                  &result.emissions, &actual.atoms, false, &actual.puzzle);
    result.solution.puzzleName = puzzle.name;
    result.solution.solutionName = "Experimental complete shared Animismus assembly";
    auto state = ExecutionIR::Initial(puzzle);
    std::vector<std::vector<ExecutionIR::AtomId>> owned(2);
    ExecutionIR::AtomId pendingV = 0, pendingM = 0;
    std::size_t sourceCursor = 0, batch = 0;
    std::array<ExecutionIR::AtomId, 2> precursors{};
    for (std::size_t product = 0; product < 6; ++product) {
        for (std::size_t output = 0; output < 2; ++output)
            owned[output].assign(puzzle.outputs[output].atoms.size(), 0);
        for (auto& emission : result.emissions) {
            if (emission.product != product) continue;
            const auto ref = emission.consumer;
            const auto type = puzzle.outputs[ref.output].atoms[ref.atom].type;
            auto& pending = type == T::Vitae ? pendingV : pendingM;
            if (!pending) {
                if (pendingV || pendingM) throw std::logic_error("unconsumed coproduct");
                const auto first = state.nextId;
                result.ledger.AppendFromTail(puzzle, state, {K::Acquire, 0});
                result.ledger.AppendFromTail(puzzle, state, {K::Unbond, 0, {first, first + 1}, 1});
                std::vector<ExecutionIR::AtomId> salts;
                for (int inlet = 0; inlet < 2; ++inlet) {
                    const auto& source = supply.steps.at(sourceCursor++ % supply.steps.size());
                    precursors[inlet] = first + source.sourceAtom;
                    salts.push_back(state.nextId);
                    result.ledger.AppendFromTail(puzzle, state,
                                                 {K::Calcify, 0, {first + source.sourceAtom}});
                }
                pendingV = state.nextId;
                pendingM = pendingV + 1;
                result.ledger.AppendFromTail(puzzle, state, {K::Animismus, 0, salts});
                ++batch;
            }
            emission.atom = pending;
            emission.sourceIdentity = 0;
            emission.sourceInput = 0;
            emission.sourceAtom = 0;
            emission.reactionInputs = precursors;
            emission.type = type;
            emission.sourceBatch = batch - 1;
            emission.sourceRemainder = (pendingV && pendingM) ? 1 : 0;
            owned[ref.output][ref.atom] = pending;
            const auto target = puzzle.outputs[ref.output].atoms[ref.atom].position;
            result.ledger.AppendFromTail(
                puzzle, state,
                {K::Relocate,
                 0,
                 {pending},
                 0,
                 0,
                 core::Subtract(target, state.atoms.at(pending).position)});
            pending = 0;
        }
        for (std::size_t output = 0; output < 2; ++output) {
            const auto& molecule = puzzle.outputs[output];
            auto idAt = [&](core::HexCoord position) {
                for (std::size_t atom = 0; atom < molecule.atoms.size(); ++atom)
                    if (molecule.atoms[atom].position == position) return owned[output][atom];
                throw std::logic_error("unknown shared consumer bond endpoint");
            };
            for (const auto& bond : molecule.bonds)
                result.ledger.AppendFromTail(
                    puzzle, state, {K::Bond, 0, {idAt(bond.from), idAt(bond.to)}, bond.bits});
            result.ledger.AppendFromTail(puzzle, state, {K::Output, output, {owned[output].at(0)}});
        }
    }
    ExecutionIR::RequireComplete(result.ledger.Replay(puzzle), {6, 6});
    return result;
}

SharedFactoryBatch CompileMixedInventoryAssembly(const io::PuzzleData& puzzle,
                                                 bool scheduleEmptyReturns, bool compactBuffers,
                                                 bool batchConsumer, bool nearCache) {
    using T = core::AtomType;
    using K = ExecutionIR::Kind;
    const auto required = (1ULL << 2) | (1ULL << 3) | (1ULL << 9) | (1ULL << 12) | (1ULL << 16);
    if (puzzle.productionInfo || puzzle.outputMultiplier != 1 || puzzle.inputs.size() != 2 ||
        (puzzle.permissionBits & required) != required)
        throw std::invalid_argument("mixed inventory requires two permitted finite sources");
    auto cardinal = [](T t) {
        return t == T::Salt || t == T::Earth || t == T::Water || t == T::Air || t == T::Fire;
    };
    if (puzzle.outputs.size() != 2)
        throw std::invalid_argument("mixed inventory requires two finite outputs");
    for (const auto& input : puzzle.inputs)
        if (input.atoms.empty() || input.atoms.size() > 512)
            throw std::invalid_argument("mixed inventory source atom bound exceeded");
    std::size_t atomCount = 0;
    std::map<T, int> demand;
    for (const auto& output : puzzle.outputs) {
        if (output.atoms.size() > 512 - atomCount)
            throw std::invalid_argument("mixed inventory atom bound exceeded");
        atomCount += output.atoms.size();
        for (const auto& atom : output.atoms) {
            if (!cardinal(atom.type) && atom.type != T::Vitae && atom.type != T::Mors)
                throw std::invalid_argument("mixed output chemistry");
            ++demand[atom.type];
        }
    }
    if (!demand[T::Vitae] || demand[T::Vitae] != demand[T::Mors])
        throw std::invalid_argument("mixed inventory requires balanced reaction demand");
    for (const auto& input : puzzle.inputs)
        for (const auto& atom : input.atoms)
            if (!cardinal(atom.type)) throw std::invalid_argument("mixed source chemistry");
    std::array<int, 2> counts{};
    bool found = false;
    int total = 0;
    for (const auto& [type, count] : demand)
        total += count;
    for (int sum = 1; sum <= total && !found; ++sum)
        for (int a = 0; a <= sum && !found; ++a) {
            std::map<T, int> available;
            for (int input = 0; input < 2; ++input)
                for (const auto& atom : puzzle.inputs[input].atoms)
                    available[atom.type] += input == 0 ? a : sum - a;
            int supplied = 0;
            bool enough = true;
            for (const auto& [type, count] : available)
                supplied += count;
            for (const auto& [type, count] : demand)
                if (type != T::Vitae && type != T::Mors && available[type] < count) enough = false;
            if (enough && supplied == total) {
                counts = {a, sum - a};
                found = true;
            }
        }
    if (!found) {
        const auto recipe = ChemicalPlanner::Plan(puzzle, 1, false);
        if (recipe.inputBatches.size() != 2 ||
            recipe.animismus != static_cast<std::size_t>(demand[T::Vitae]) || recipe.projections ||
            recipe.duplications || recipe.purifications || recipe.unifications ||
            recipe.dispersions ||
            std::any_of(recipe.surplus.begin(), recipe.surplus.end(),
                        [](auto n) { return n != 0; }))
            throw std::invalid_argument("exact recipe requires unsupported mixed chemistry");
        std::size_t supplied = 0;
        for (int input = 0; input < 2; ++input) {
            const auto atoms = puzzle.inputs[input].atoms.size();
            if (!atoms || recipe.inputBatches[input] > atomCount / atoms)
                throw std::invalid_argument("exact mixed source batch bound exceeded");
            supplied += recipe.inputBatches[input] * atoms;
            counts[input] = static_cast<int>(recipe.inputBatches[input]);
        }
        if (supplied != atomCount)
            throw std::invalid_argument("exact mixed source recipe is not atom closed");
    }
    auto saltPuzzle = puzzle;
    for (auto& output : saltPuzzle.outputs)
        for (auto& atom : output.atoms)
            atom.type = T::Salt;
    auto shared = BuildSharedAssemblyTarget(saltPuzzle);
    const auto plan = AssemblyPlanner::Plan(shared.puzzle, true);
    MixedInventoryProgram program;
    program.scheduleEmptyReturns = scheduleEmptyReturns;
    program.compactBuffers = compactBuffers;
    std::vector<T> slots;
    for (int input = 0; input < 2; ++input)
        for (int batch = 0; batch < counts[input]; ++batch)
            for (std::size_t atom = 0; atom < puzzle.inputs[input].atoms.size(); ++atom) {
                program.stores.push_back({SupplyPlan::Kind::Store, static_cast<std::size_t>(input),
                                          atom, 0, ElementConversion::None,
                                          static_cast<int>(slots.size())});
                slots.push_back(puzzle.inputs[input].atoms[atom].type);
            }
    std::vector<int> assigned(shared.atoms.size(), -1);
    std::set<int> used;
    for (std::size_t atom = 0; atom < assigned.size(); ++atom) {
        const auto ref = shared.atoms[atom];
        const auto type = puzzle.outputs[ref.output].atoms[ref.atom].type;
        if (type == T::Vitae || type == T::Mors) continue;
        for (int slot = 0; slot < static_cast<int>(slots.size()); ++slot)
            if (!used.count(slot) && slots[slot] == type) {
                assigned[atom] = slot;
                used.insert(slot);
                break;
            }
    }
    std::vector<ElementConversion> conversions(assigned.size(), ElementConversion::None);
    for (std::size_t atom = 0; atom < assigned.size(); ++atom) {
        const auto ref = shared.atoms[atom];
        const auto type = puzzle.outputs[ref.output].atoms[ref.atom].type;
        if (assigned[atom] >= 0 || type == T::Vitae || type == T::Mors) continue;
        if (type == T::Salt)
            for (int slot = 0; slot < static_cast<int>(slots.size()); ++slot)
                if (!used.count(slot) && cardinal(slots[slot])) {
                    assigned[atom] = slot;
                    used.insert(slot);
                    conversions[atom] = ElementConversion::Calcification;
                    break;
                }
        if (assigned[atom] < 0)
            throw std::invalid_argument("exact mixed ordinary allocation unsupported");
    }
    std::vector<int> precursor;
    for (int slot = 0; slot < static_cast<int>(slots.size()); ++slot)
        if (!used.count(slot)) precursor.push_back(slot);
    if (precursor.size() != 2 * static_cast<std::size_t>(demand[T::Vitae]))
        throw std::invalid_argument("mixed reaction precursor balance is not closed");
    if (batchConsumer) {
        const int last = static_cast<int>(program.stores.size()) - 1;
        const auto found = std::find(precursor.begin(), precursor.end(), last);
        if (found == precursor.end())
            throw std::invalid_argument("last source atom is not a reaction precursor");
        program.bypassSlot = last;
        precursor.erase(found);
        precursor.insert(precursor.begin(), last);
    }
    for (std::size_t i = 0; i < precursor.size(); i += 2) {
        MixedReaction reaction;
        for (int side = 0; side < 2; ++side) {
            const int source = precursor.at(i + side);
            reaction.inputs[side] = {SupplyPlan::Kind::Take,
                                     0,
                                     0,
                                     0,
                                     slots[source] == T::Salt ? ElementConversion::None
                                                              : ElementConversion::Calcification,
                                     source};
            reaction.outputs[side] = static_cast<int>(slots.size());
            slots.push_back(side == 0 ? T::Vitae : T::Mors);
        }
        if (batchConsumer && i == 0) {
            auto direct = program.stores.back();
            direct.conversion = reaction.inputs[0].conversion;
            reaction.inputs[0] = direct;
        }
        program.reactions.push_back(reaction);
    }
    for (std::size_t atom = 0; atom < assigned.size(); ++atom) {
        if (assigned[atom] >= 0) continue;
        const auto ref = shared.atoms[atom];
        const auto type = puzzle.outputs[ref.output].atoms[ref.atom].type;
        for (int slot = static_cast<int>(program.stores.size());
             slot < static_cast<int>(slots.size()); ++slot)
            if (!used.count(slot) && slots[slot] == type) {
                assigned[atom] = slot;
                used.insert(slot);
                break;
            }
        if (assigned[atom] < 0) throw std::logic_error("mixed coproduct allocation");
    }
    std::vector<int> order(assigned.size());
    std::iota(order.begin(), order.end(), 0);
    const auto& target = shared.puzzle.outputs[0];
    std::sort(order.begin(), order.end(), [&](int a, int b) {
        const auto x = target.atoms[a].position, y = target.atoms[b].position;
        return x.r != y.r ? x.r > y.r : x.q > y.q;
    });
    SupplyPlan supply;
    supply.productsPerPeriod = 1;
    for (int atom : order)
        supply.steps.push_back({SupplyPlan::Kind::Take, 0, 0, static_cast<std::size_t>(atom),
                                conversions[atom], assigned[atom]});
    if (nearCache) {
        if (!compactBuffers)
            throw std::invalid_argument("near inventory cache requires compact buffers");
        program.cacheSlots.assign(slots.size(), -1);
        std::vector<int> owner;
        auto store = [&](int logical) {
            auto free = std::find(owner.begin(), owner.end(), -1);
            const int physical = static_cast<int>(free - owner.begin());
            if (free == owner.end())
                owner.push_back(logical);
            else
                *free = logical;
            if (program.cacheSlots.at(logical) != -1)
                throw std::logic_error("duplicate inventory lifetime");
            program.cacheSlots[logical] = physical;
        };
        auto take = [&](int logical) {
            const int physical = program.cacheSlots.at(logical);
            if (physical < 0 || owner.at(physical) != logical)
                throw std::logic_error("inventory cache ownership conflict");
            owner[physical] = -1;
        };
        for (const auto& source : program.stores)
            if (source.buffer != program.bypassSlot) store(source.buffer);
        for (const auto& reaction : program.reactions) {
            for (const auto& source : reaction.inputs)
                if (source.buffer != program.bypassSlot) take(source.buffer);
            for (int output : reaction.outputs)
                store(output);
        }
        for (const auto& step : supply.steps)
            take(step.buffer);
        if (std::any_of(owner.begin(), owner.end(), [](int id) { return id != -1; }))
            throw std::logic_error("inventory cache not drained");
        program.cacheCapacity = static_cast<int>(owner.size());
        if (program.cacheCapacity > 32)
            throw std::invalid_argument("near inventory cache capacity exceeds bounded scope");
    }
    SharedFactoryBatch result;
    result.solution =
        CompileRows(shared.puzzle, plan, nullptr, nullptr, &shared.origins, true, nullptr, false, 6,
                    &supply, nullptr, 3, false, false, false, false, &result.emissions,
                    &shared.atoms, false, &shared.puzzle, &program);
    result.phases = program.phases;
    result.emptyReturnTasks = program.emptyReturnTasks;
    result.emptyReturnCycles = program.emptyReturnCycles;
    result.solution.puzzleName = puzzle.name;
    result.solution.solutionName = "Experimental finite mixed inventory assembly";
    auto state = ExecutionIR::Initial(puzzle);
    for (std::size_t product = 0; product < 6; ++product) {
        std::vector<ExecutionIR::AtomId> ids(slots.size());
        std::vector<SharedFactoryEmission> provenance(slots.size());
        int slot = 0;
        for (int input = 0; input < 2; ++input)
            for (int batch = 0; batch < counts[input]; ++batch) {
                const auto first = state.nextId;
                result.ledger.AppendFromTail(puzzle, state,
                                             {K::Acquire, static_cast<std::size_t>(input)});
                const auto& source = puzzle.inputs[input];
                auto idAt = [&](core::HexCoord c) {
                    for (std::size_t i = 0; i < source.atoms.size(); ++i)
                        if (source.atoms[i].position == c) return first + i;
                    throw std::logic_error("source endpoint");
                };
                for (const auto& bond : source.bonds)
                    result.ledger.AppendFromTail(
                        puzzle, state, {K::Unbond, 0, {idAt(bond.from), idAt(bond.to)}, bond.bits});
                for (std::size_t atom = 0; atom < source.atoms.size(); ++atom, ++slot) {
                    ids[slot] = first + atom;
                    provenance[slot].sourceIdentity = first + atom;
                    provenance[slot].sourceInput = input;
                    provenance[slot].sourceAtom = atom;
                    provenance[slot].sourceBatch = product * counts[input] + batch;
                    if (slot != program.bypassSlot)
                        result.ledger.AppendFromTail(
                            puzzle, state, {K::Store, static_cast<std::size_t>(slot), {ids[slot]}});
                }
            }
        for (const auto& reaction : program.reactions) {
            std::vector<ExecutionIR::AtomId> salts;
            for (const auto& source : reaction.inputs) {
                if (source.buffer != program.bypassSlot)
                    result.ledger.AppendFromTail(
                        puzzle, state, {K::Retrieve, static_cast<std::size_t>(source.buffer)});
                auto id = ids[source.buffer];
                if (source.conversion == ElementConversion::Calcification) {
                    const auto next = state.nextId;
                    result.ledger.AppendFromTail(puzzle, state, {K::Calcify, 0, {id}});
                    id = next;
                }
                salts.push_back(id);
            }
            const auto next = state.nextId;
            result.ledger.AppendFromTail(puzzle, state, {K::Animismus, 0, salts});
            for (int side = 0; side < 2; ++side) {
                ids[reaction.outputs[side]] = next + side;
                provenance[reaction.outputs[side]].reactionInputs = {salts[0], salts[1]};
                result.ledger.AppendFromTail(
                    puzzle, state,
                    {K::Store, static_cast<std::size_t>(reaction.outputs[side]), {next + side}});
            }
        }
        std::vector<std::vector<ExecutionIR::AtomId>> owned(puzzle.outputs.size());
        for (std::size_t output = 0; output < owned.size(); ++output)
            owned[output].resize(puzzle.outputs[output].atoms.size());
        for (int atom : order) {
            const auto ref = shared.atoms[atom];
            auto id = ids[assigned[atom]];
            result.ledger.AppendFromTail(puzzle, state,
                                         {K::Retrieve, static_cast<std::size_t>(assigned[atom])});
            if (conversions[atom] == ElementConversion::Calcification) {
                result.ledger.AppendFromTail(puzzle, state, {K::Calcify, 0, {id}});
                id = state.nextId - 1;
            }
            owned[ref.output][ref.atom] = id;
            result.ledger.AppendFromTail(
                puzzle, state,
                {K::Relocate,
                 0,
                 {id},
                 0,
                 0,
                 core::Subtract(puzzle.outputs[ref.output].atoms[ref.atom].position,
                                state.atoms.at(id).position)});
            for (auto& emission : result.emissions)
                if (emission.product == product && emission.consumer == ref) {
                    emission.atom = id;
                    emission.type = state.atoms.at(id).type;
                    const auto& source = provenance[assigned[atom]];
                    emission.sourceIdentity = source.sourceIdentity;
                    emission.sourceInput = source.sourceInput;
                    emission.sourceAtom = source.sourceAtom;
                    emission.sourceBatch = source.sourceBatch;
                    emission.reactionInputs = source.reactionInputs;
                }
        }
        for (std::size_t output = 0; output < owned.size(); ++output) {
            const auto& molecule = puzzle.outputs[output];
            auto idAt = [&](core::HexCoord c) {
                for (std::size_t atom = 0; atom < molecule.atoms.size(); ++atom)
                    if (molecule.atoms[atom].position == c) return owned[output][atom];
                throw std::logic_error("target endpoint");
            };
            for (const auto& bond : molecule.bonds)
                result.ledger.AppendFromTail(
                    puzzle, state, {K::Bond, 0, {idAt(bond.from), idAt(bond.to)}, bond.bits});
            result.ledger.AppendFromTail(puzzle, state, {K::Output, output, {owned[output][0]}});
        }
    }
    ExecutionIR::RequireComplete(result.ledger.Replay(puzzle),
                                 std::vector<std::size_t>(puzzle.outputs.size(), 6));
    return result;
}

io::SolutionData CompileSharedOrdinaryOutputs(const io::PuzzleData& puzzle) {
    const auto shared = BuildSharedAssemblyTarget(puzzle);
    auto result = CompileRows(shared.puzzle, AssemblyPlanner::Plan(shared.puzzle, true), nullptr,
                              nullptr, &shared.origins);
    result.solutionName = "Shared inventory ordinary products";
    return result;
}

SharedFactoryBatch CompileSharedFactoryBatch(const io::PuzzleData& puzzle) {
    using K = ExecutionIR::Kind;
    if (puzzle.productionInfo || puzzle.outputMultiplier != 1 || puzzle.outputs.size() != 2 ||
        puzzle.inputs.size() != 1 || puzzle.inputs[0].atoms.size() != 1 ||
        !puzzle.inputs[0].bonds.empty())
        throw std::invalid_argument("shared factory requires one real singleton and two outputs");
    const auto shared = BuildSharedAssemblyTarget(puzzle);
    const auto plan = AssemblyPlanner::Plan(shared.puzzle, true);
    const auto supply = AssemblyPlanner::PlanSupply(shared.puzzle, true);
    const auto type = puzzle.outputs[0].atoms.at(0).type;
    for (const auto& output : puzzle.outputs)
        for (const auto& atom : output.atoms)
            if (atom.type != type)
                throw std::invalid_argument("shared factory requires same-type consumers");
    if (supply.productsPerPeriod != 1)
        throw std::invalid_argument("shared factory requires one closed product pair");
    for (const auto& step : supply.steps)
        if (step.kind != SupplyPlan::Kind::Take || step.input != 0 || step.sourceAtom != 0 ||
            (step.conversion != ElementConversion::None &&
             step.conversion != ElementConversion::Calcification))
            throw std::invalid_argument("shared factory supports Direct/Calcification only");
    SharedFactoryBatch result;
    result.solution = CompileRows(shared.puzzle, plan, nullptr, nullptr, &shared.origins, true,
                                  nullptr, false, 6, &supply, nullptr, 3, false, false, false,
                                  false, &result.emissions, &shared.atoms);
    result.solution.solutionName = "Experimental finite shared singleton factory";
    auto state = ExecutionIR::Initial(puzzle);
    std::vector<std::vector<ExecutionIR::AtomId>> owned(2);
    for (std::size_t product = 0; product < 6; ++product) {
        for (std::size_t output = 0; output < 2; ++output)
            owned[output].assign(puzzle.outputs[output].atoms.size(), 0);
        for (auto& emission : result.emissions) {
            if (emission.product != product) continue;
            if (emission.beginCycle >= emission.readyCycle ||
                emission.readyCycle >= emission.releaseCycle)
                throw std::logic_error("invalid shared conveyor reservation");
            auto id = state.nextId;
            result.ledger.AppendFromTail(puzzle, state, {K::Acquire, 0});
            if (puzzle.inputs[0].atoms[0].type != type) {
                const auto converted = state.nextId;
                result.ledger.AppendFromTail(puzzle, state, {K::Calcify, 0, {id}});
                id = converted;
            }
            emission.atom = id;
            auto& slot = owned.at(emission.consumer.output).at(emission.consumer.atom);
            if (slot) throw std::logic_error("duplicate shared consumption");
            slot = id;
            const auto target =
                puzzle.outputs[emission.consumer.output].atoms[emission.consumer.atom].position;
            const auto delta = core::Subtract(target, state.atoms.at(id).position);
            result.ledger.AppendFromTail(puzzle, state, {K::Relocate, 0, {id}, 0, 0, delta});
        }
        for (std::size_t output = 0; output < 2; ++output) {
            const auto& molecule = puzzle.outputs[output];
            for (const auto& bond : molecule.bonds) {
                auto idAt = [&](core::HexCoord position) {
                    for (std::size_t atom = 0; atom < molecule.atoms.size(); ++atom)
                        if (molecule.atoms[atom].position == position) return owned[output][atom];
                    throw std::logic_error("unknown consumer bond endpoint");
                };
                result.ledger.AppendFromTail(
                    puzzle, state, {K::Bond, 0, {idAt(bond.from), idAt(bond.to)}, bond.bits});
            }
            result.ledger.AppendFromTail(puzzle, state, {K::Output, output, {owned[output].at(0)}});
        }
    }
    ExecutionIR::RequireComplete(result.ledger.Replay(puzzle), {6, 6});
    for (std::size_t i = 1; i < result.emissions.size(); ++i)
        if (result.emissions[i].beginCycle < result.emissions[i - 1].reservationEnd)
            throw std::logic_error("shared conveyor custody overlaps");
    return result;
}

bool SupportsMolecularSupplySource(const io::Molecule& source) {
    if (source.atoms.size() != 2 || source.bonds.size() != 1 || source.bonds[0].bits != 1 ||
        source.atoms[0].type == source.atoms[1].type)
        return false;
    for (const auto& atom : source.atoms)
        if (atom.type < core::AtomType::Air || atom.type > core::AtomType::Water) return false;
    const auto& bond = source.bonds[0];
    const auto a = source.atoms[0].position, b = source.atoms[1].position;
    const auto delta = core::Subtract(a, b);
    return ((bond.from == a && bond.to == b) || (bond.from == b && bond.to == a)) &&
           std::max({std::abs(delta.q), std::abs(delta.r), std::abs(delta.q + delta.r)}) == 1;
}

SharedFactoryBatch CompileRenewableFragmentTransport(
    const io::PuzzleData& puzzle, std::chrono::steady_clock::time_point deadline,
    std::function<std::chrono::steady_clock::time_point()> clock) {
    auto checkDeadline = [&] {
        if (std::chrono::steady_clock::now() >= deadline)
            throw std::invalid_argument("renewable fragment transport deadline");
    };
    checkDeadline();
    using K = ExecutionIR::Kind;
    using T = core::AtomType;
    constexpr auto required = (1ULL << 2) | (1ULL << 3) | (1ULL << 9);
    if (puzzle.productionInfo || puzzle.outputMultiplier != 1 || puzzle.inputs.size() != 1 ||
        puzzle.outputs.empty() || puzzle.outputs.size() > 2 ||
        (puzzle.permissionBits & required) != required)
        throw std::invalid_argument(
            "fragment supply requires one permitted finite molecular source");
    const auto& source = puzzle.inputs[0];
    if (source.atoms.size() < 2 || source.atoms.size() > 16 ||
        source.bonds.size() + 1 != source.atoms.size())
        throw std::invalid_argument("fragment supply requires a bounded ordinary tree");
    auto key = [](core::HexCoord cell) { return std::pair{cell.q, cell.r}; };
    std::set<std::pair<int, int>> positions;
    for (const auto& atom : source.atoms)
        if (atom.type < T::Salt || atom.type == T::Repeat || static_cast<int>(atom.type) > 16 ||
            !positions.insert(key(atom.position)).second)
            throw std::invalid_argument(
                "fragment supply requires distinct ordinary material cells");
    std::set<std::pair<int, int>> reached{key(source.atoms[0].position)};
    std::set<std::pair<std::pair<int, int>, std::pair<int, int>>> edges;
    for (const auto& bond : source.bonds) {
        const auto delta = core::Subtract(bond.from, bond.to);
        if (bond.bits != 1 || !positions.contains(key(bond.from)) ||
            !positions.contains(key(bond.to)) ||
            std::max({std::abs(delta.q), std::abs(delta.r), std::abs(delta.q + delta.r)}) != 1 ||
            !edges.insert(std::minmax(key(bond.from), key(bond.to))).second)
            throw std::invalid_argument("fragment supply requires adjacent ordinary source bonds");
    }
    for (std::size_t pass = 0; pass < source.atoms.size(); ++pass)
        for (const auto& bond : source.bonds) {
            if (reached.contains(key(bond.from))) reached.insert(key(bond.to));
            if (reached.contains(key(bond.to))) reached.insert(key(bond.from));
        }
    if (reached.size() != source.atoms.size())
        throw std::invalid_argument("fragment supply requires connected source");
    std::size_t demand = 0;
    for (const auto& output : puzzle.outputs) {
        demand += output.atoms.size();
        for (const auto& bond : output.bonds)
            if (bond.bits != 1)
                throw std::invalid_argument("fragment supply requires ordinary products");
    }
    if (!demand || demand > 32 || demand % source.atoms.size())
        throw std::invalid_argument("fragment supply must exhaust the real source");
    const auto shared = [&] {
        if (puzzle.outputs.size() > 1) return BuildSharedAssemblyTarget(puzzle);
        SharedAssemblyTarget target{puzzle, {{0, 0}}, {}};
        for (std::size_t i = 0; i < puzzle.outputs[0].atoms.size(); ++i)
            target.atoms.push_back({0, i});
        return target;
    }();
    const auto plan = AssemblyPlanner::Plan(shared.puzzle, true, deadline);
    checkDeadline();
    const auto supply = AssemblyPlanner::PlanSupply(shared.puzzle, true, 20000, deadline);
    checkDeadline();
    const auto batches = demand / source.atoms.size();
    if (supply.productsPerPeriod != 1 || supply.usage.at(0).batches != batches ||
        supply.usage.at(0).discarded)
        throw std::invalid_argument("fragment supply requires one exact exhausted source period");
    std::set<std::size_t> used;
    std::optional<std::size_t> buffered;
    std::size_t takes = 0, extracted = 0;
    for (const auto& step : supply.steps) {
        checkDeadline();
        if ((step.kind != SupplyPlan::Kind::Take && step.kind != SupplyPlan::Kind::Store) ||
            step.input != 0 || step.sourceAtom >= source.atoms.size() || step.buffer < -1 ||
            step.buffer > 0 ||
            (step.conversion != ElementConversion::None &&
             step.conversion != ElementConversion::Calcification))
            throw std::invalid_argument("fragment supply outside one-slot physical custody domain");
        if (step.kind == SupplyPlan::Kind::Store) {
            if (step.buffer != 0 || buffered || step.conversion != ElementConversion::None)
                throw std::invalid_argument("fragment physical slot occupied or invalid store");
            buffered = step.sourceAtom;
        } else {
            ++takes;
            if (step.buffer == 0) {
                if (!buffered || *buffered != step.sourceAtom)
                    throw std::invalid_argument("fragment physical slot identity mismatch");
                buffered.reset();
                continue;
            }
        }
        if (used.size() == source.atoms.size()) used.clear();
        if (!used.insert(step.sourceAtom).second)
            throw std::invalid_argument("fragment source atom extracted twice before regeneration");
        ++extracted;
    }
    if (buffered || takes != demand || extracted != demand || used.size() != source.atoms.size())
        throw std::invalid_argument("fragment physical slot or source residual");
    checkDeadline();
    SharedFactoryBatch result;
    result.solution =
        CompileRows(shared.puzzle, plan, nullptr, nullptr, &shared.origins, true, nullptr, false, 6,
                    &supply, nullptr, 3, false, false, false, false, &result.emissions,
                    &shared.atoms, false, nullptr, nullptr, deadline, clock, &result.custody);
    result.solution.solutionName = "Experimental renewable fragment transport";
    if (result.emissions.size() != 6 * demand) throw std::logic_error("missing fragment emissions");
    auto state = ExecutionIR::Initial(puzzle);
    for (std::size_t product = 0; product < 6; ++product) {
        checkDeadline();
        ExecutionIR::AtomId first{};
        auto acquire = [&] {
            first = state.nextId;
            result.ledger.AppendFromTail(puzzle, state, {K::Acquire, 0});
            auto sourceIndex = [&](core::HexCoord cell) {
                return static_cast<std::size_t>(
                    std::find_if(source.atoms.begin(), source.atoms.end(),
                                 [&](const auto& atom) { return atom.position == cell; }) -
                    source.atoms.begin());
            };
            for (const auto& bond : source.bonds)
                result.ledger.AppendFromTail(
                    puzzle, state,
                    {K::Unbond,
                     0,
                     {first + sourceIndex(bond.from), first + sourceIndex(bond.to)},
                     1});
        };
        std::vector<std::vector<ExecutionIR::AtomId>> delivered;
        for (const auto& output : puzzle.outputs)
            delivered.emplace_back(output.atoms.size());
        std::map<int, std::pair<ExecutionIR::AtomId, std::size_t>> stock;
        std::size_t extracted = 0, i = 0;
        for (const auto& step : supply.steps) {
            checkDeadline();
            ExecutionIR::AtomId id{};
            std::size_t batchIndex{};
            if (step.kind == SupplyPlan::Kind::Take && step.buffer >= 0) {
                std::tie(id, batchIndex) = stock.at(step.buffer);
                result.ledger.AppendFromTail(puzzle, state,
                                             {K::Retrieve, static_cast<std::size_t>(step.buffer)});
                stock.erase(step.buffer);
            } else {
                if (extracted % source.atoms.size() == 0) acquire();
                id = first + step.sourceAtom;
                batchIndex = product * batches + extracted / source.atoms.size();
                ++extracted;
                if (step.kind == SupplyPlan::Kind::Store) {
                    stock.emplace(step.buffer, std::pair{id, batchIndex});
                    result.ledger.AppendFromTail(
                        puzzle, state, {K::Store, static_cast<std::size_t>(step.buffer), {id}});
                    continue;
                }
            }
            auto& emission = result.emissions[product * demand + i];
            if (emission.consumer != shared.atoms.at(step.targetAtom) ||
                emission.product != product)
                throw std::logic_error("fragment emission consumer mismatch");
            emission.sourceIdentity = id;
            emission.sourceInput = 0;
            emission.sourceAtom = step.sourceAtom;
            emission.sourceBatch = batchIndex;
            emission.sourceRemainder =
                source.atoms.size() - ((extracted - 1) % source.atoms.size()) - 1;
            if (step.conversion == ElementConversion::Calcification) {
                const auto next = state.nextId;
                result.ledger.AppendFromTail(puzzle, state, {K::Calcify, 0, {id}});
                id = next;
            }
            emission.atom = id;
            const auto ref = emission.consumer;
            const auto& target = puzzle.outputs.at(ref.output).atoms.at(ref.atom);
            emission.type = state.atoms.at(id).type;
            if (emission.type != target.type) throw std::logic_error("fragment material mismatch");
            result.ledger.AppendFromTail(
                puzzle, state,
                {K::Relocate,
                 0,
                 {id},
                 0,
                 0,
                 core::Subtract(target.position, state.atoms.at(id).position)});
            delivered[ref.output][ref.atom] = id;
            if (emission.beginCycle > emission.holderReleaseCycle ||
                emission.holderReleaseCycle >= emission.readyCycle ||
                emission.readyCycle >= emission.releaseCycle ||
                emission.releaseCycle >= emission.reservationEnd ||
                (product * demand + i &&
                 emission.beginCycle < result.emissions[product * demand + i - 1].reservationEnd))
                throw std::logic_error("fragment custody overlap");
            ++i;
        }
        for (std::size_t output = 0; output < puzzle.outputs.size(); ++output) {
            const auto& molecule = puzzle.outputs[output];
            auto idAt = [&](core::HexCoord cell) {
                const auto it =
                    std::find_if(molecule.atoms.begin(), molecule.atoms.end(),
                                 [&](const auto& atom) { return atom.position == cell; });
                if (it == molecule.atoms.end())
                    throw std::logic_error("fragment target bond missing");
                return delivered[output][it - molecule.atoms.begin()];
            };
            for (const auto& bond : molecule.bonds)
                result.ledger.AppendFromTail(puzzle, state,
                                             {K::Bond, 0, {idAt(bond.from), idAt(bond.to)}, 1});
            result.ledger.AppendFromTail(puzzle, state,
                                         {K::Output, output, {delivered[output][0]}});
        }
    }
    checkDeadline();
    ExecutionIR::RequireComplete(result.ledger.Replay(puzzle),
                                 std::vector<std::size_t>(puzzle.outputs.size(), 6));
    return result;
}

SharedFactoryBatch CompileMolecularFragmentSupplyBatch(const io::PuzzleData& puzzle) {
    if (puzzle.inputs.size() != 1 || puzzle.outputs.size() != 2 ||
        puzzle.inputs[0].atoms.size() > 3)
        throw std::invalid_argument(
            "fragment supply requires a two/three atom source and two outputs");
    const auto& source = puzzle.inputs[0];
    std::size_t demand = 0;
    for (const auto& output : puzzle.outputs)
        demand += output.atoms.size();
    if (demand != source.atoms.size() ||
        std::any_of(source.atoms.begin(), source.atoms.end(), [](const auto& atom) {
            return atom.type < core::AtomType::Salt || atom.type > core::AtomType::Water;
        }))
        throw std::invalid_argument("legacy fragment supply requires one Salt/cardinal batch");
    return CompileRenewableFragmentTransport(puzzle);
}

SharedFactoryBatch CompileMolecularSupplyBatch(const io::PuzzleData& puzzle) {
    using K = ExecutionIR::Kind;
    if (puzzle.productionInfo || puzzle.outputMultiplier != 1 || puzzle.inputs.size() != 1 ||
        !SupportsMolecularSupplySource(puzzle.inputs[0]) || puzzle.outputs.size() != 2)
        throw std::invalid_argument("molecular supply requires one ordinary cardinal diatom");
    for (const auto& output : puzzle.outputs)
        if (output.atoms.size() != 1 || !output.bonds.empty())
            throw std::invalid_argument("molecular supply requires two singleton consumers");
    if (puzzle.outputs[0].atoms[0].type == puzzle.outputs[1].atoms[0].type)
        throw std::invalid_argument("molecular supply requires distinct demands");
    const auto shared = BuildSharedAssemblyTarget(puzzle);
    const auto plan = AssemblyPlanner::Plan(shared.puzzle, true);
    const auto supply = AssemblyPlanner::PlanSupply(shared.puzzle, true);
    if (supply.productsPerPeriod != 1 || supply.steps.size() != 2 ||
        supply.usage.at(0).batches != 1 || supply.usage.at(0).discarded)
        throw std::invalid_argument("molecular supply requires one exhausted shared batch");
    std::set<std::size_t> sources;
    for (const auto& step : supply.steps)
        if (step.kind != SupplyPlan::Kind::Take || step.input != 0 || step.buffer != -1 ||
            !sources.insert(step.sourceAtom).second ||
            (step.conversion != ElementConversion::None &&
             step.conversion != ElementConversion::Calcification))
            throw std::invalid_argument("molecular supply requires direct endpoint custody");
    SharedFactoryBatch result;
    result.solution = CompileRows(shared.puzzle, plan, nullptr, nullptr, &shared.origins, true,
                                  nullptr, false, 6, &supply, nullptr, 3, false, false, false,
                                  false, &result.emissions, &shared.atoms);
    result.solution.solutionName = "Experimental shared molecular extraction";
    auto state = ExecutionIR::Initial(puzzle);
    if (result.emissions.size() != 12) throw std::logic_error("missing molecular emissions");
    for (std::size_t product = 0; product < 6; ++product) {
        const auto first = state.nextId;
        result.ledger.AppendFromTail(puzzle, state, {K::Acquire, 0});
        result.ledger.AppendFromTail(puzzle, state, {K::Unbond, 0, {first, first + 1}, 1});
        for (std::size_t i = 0; i < 2; ++i) {
            auto& emission = result.emissions[product * 2 + i];
            const auto& step = supply.steps[i];
            if (emission.product != product ||
                emission.consumer != shared.atoms.at(step.targetAtom))
                throw std::logic_error("molecular emission source order mismatch");
            auto id = first + step.sourceAtom;
            emission.sourceIdentity = id;
            emission.sourceInput = 0;
            emission.sourceAtom = step.sourceAtom;
            emission.sourceBatch = product;
            emission.sourceRemainder = 1 - i;
            if (step.conversion == ElementConversion::Calcification) {
                const auto converted = state.nextId;
                result.ledger.AppendFromTail(puzzle, state, {K::Calcify, 0, {id}});
                id = converted;
            }
            emission.atom = id;
            const auto target = puzzle.outputs.at(emission.consumer.output).atoms[0].position;
            result.ledger.AppendFromTail(
                puzzle, state,
                {K::Relocate, 0, {id}, 0, 0, core::Subtract(target, state.atoms.at(id).position)});
            result.ledger.AppendFromTail(puzzle, state,
                                         {K::Output, emission.consumer.output, {id}});
            if (emission.beginCycle > emission.holderReleaseCycle ||
                emission.holderReleaseCycle >= emission.readyCycle ||
                emission.readyCycle >= emission.releaseCycle ||
                emission.releaseCycle >= emission.reservationEnd ||
                (product * 2 + i &&
                 emission.beginCycle < result.emissions[product * 2 + i - 1].reservationEnd))
                throw std::logic_error("molecular custody reservation overlap");
        }
    }
    ExecutionIR::RequireComplete(result.ledger.Replay(puzzle), {6, 6});
    return result;
}

MolecularRecipeSupply CompileMolecularRecipeSupply(const io::PuzzleData& recipe, std::size_t input,
                                                   const std::vector<ProductAtomRef>& consumers) {
    if (consumers.size() != 2 || consumers[0].output == consumers[1].output)
        throw std::invalid_argument("molecular recipe supply requires two distinct output demands");
    auto extraction = recipe;
    extraction.inputs = {recipe.inputs.at(input)};
    extraction.outputs.clear();
    for (const auto& consumer : consumers) {
        const auto type = recipe.outputs.at(consumer.output).atoms.at(consumer.atom).type;
        extraction.outputs.push_back({{{type, {0, 0}}}, {}});
    }
    extraction.outputMultiplier = 1;
    auto batch = CompileMolecularSupplyBatch(extraction);
    for (auto& emission : batch.emissions)
        emission.sourceInput = input;
    ExecutionIR reservations;
    auto state = ExecutionIR::Initial(recipe);
    const auto first = state.nextId;
    reservations.AppendFromTail(recipe, state, {ExecutionIR::Kind::Acquire, input});
    reservations.AppendFromTail(recipe, state,
                                {ExecutionIR::Kind::Unbond, 0, {first, first + 1}, 1});
    for (std::size_t i = 0; i < 2; ++i) {
        const auto& emission = batch.emissions[i];
        auto id = first + emission.sourceAtom;
        const auto& consumer = consumers.at(emission.consumer.output);
        const auto& atom = recipe.outputs.at(consumer.output).atoms.at(consumer.atom);
        if (state.atoms.at(id).type != atom.type) {
            const auto converted = state.nextId;
            reservations.AppendFromTail(recipe, state, {ExecutionIR::Kind::Calcify, 0, {id}});
            id = converted;
        }
        reservations.AppendFromTail(recipe, state,
                                    {ExecutionIR::Kind::Relocate,
                                     0,
                                     {id},
                                     0,
                                     0,
                                     core::Subtract(core::HexCoord{static_cast<int>(i) * 4, 10},
                                                    state.atoms.at(id).position)});
        reservations.AppendFromTail(recipe, state,
                                    {ExecutionIR::Kind::Store, emission.consumer.output, {id}});
    }
    (void)reservations.Replay(recipe);
    return {std::move(batch), consumers, std::move(reservations), input};
}

io::SolutionData CompileSharedAtomicOutputs(const io::PuzzleData& puzzle) {
    if (puzzle.productionInfo || puzzle.outputs.size() < 2 || puzzle.inputs.size() != 1)
        throw std::invalid_argument(
            "shared atomic outputs require one feed and multiple finite outputs");
    for (std::size_t i = 0; i < puzzle.outputs.size(); ++i) {
        const auto& output = puzzle.outputs[i];
        if (output.atoms.size() != 1 || !output.bonds.empty())
            throw std::invalid_argument("shared atomic outputs require single atoms");
        for (std::size_t j = 0; j < i; ++j)
            if (output.atoms[0].type == puzzle.outputs[j].atoms[0].type)
                throw std::invalid_argument("shared atomic outputs require distinct elements");
        if (std::none_of(puzzle.inputs[0].atoms.begin(), puzzle.inputs[0].atoms.end(),
                         [&](const auto& atom) { return atom.type == output.atoms[0].type; }))
            throw std::invalid_argument("shared atomic output absent from feed");
    }
    auto combined = puzzle;
    combined.outputs = {puzzle.inputs[0]};
    auto& target = combined.outputs[0];
    target.bonds.clear();
    for (std::size_t i = 0; i < target.atoms.size(); ++i) {
        if (std::none_of(puzzle.outputs.begin(), puzzle.outputs.end(), [&](const auto& output) {
                return output.atoms[0].type == target.atoms[i].type;
            }))
            throw std::invalid_argument("shared atomic feed has unsupported surplus");
        target.atoms[i].position = {static_cast<int>(i), 0};
        if (i) target.bonds.push_back({1, {static_cast<int>(i) - 1, 0}, {static_cast<int>(i), 0}});
    }
    auto result = CompileRows(combined, AssemblyPlanner::Plan(combined), &puzzle);
    result.solutionName = "Shared feed atomic outputs";
    return result;
}
} // namespace opusoptima::solvers
