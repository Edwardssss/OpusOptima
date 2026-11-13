#include "LifeDeath.hpp"
#include "AssemblySolver.hpp"
#include "ChemistryPipeline.hpp"
#include "FiniteFeedResource.hpp"
#include "LogManager.hpp"
#include "PurificationAssembly.hpp"
#include "RowAssembly.hpp"
#include "RowAssemblyInternal.hpp"
#include "Unification.hpp"
#include "IO/BinaryWriter.hpp"

#include <array>

#include <algorithm>
#include <map>
#include <stdexcept>
#include <tuple>

namespace opusoptima::solvers {
io::SolutionData CompileRowAssembly(const io::PuzzleData&, const AssemblyPlan&);
namespace {
constexpr int CarrierIdleClearance = 6;
using core::AtomType;
io::SolutionData CompileCardinalSource(const io::PuzzleData& puzzle, std::vector<int>& ends,
                                       int products,
                                       std::chrono::steady_clock::time_point deadline) {
    auto checkDeadline = [&] {
        if (deadline != std::chrono::steady_clock::time_point::max() &&
            std::chrono::steady_clock::now() >= deadline)
            throw std::invalid_argument("cardinal source deadline");
    };
    checkDeadline();
    auto source = puzzle;
    checkDeadline();
    source.inputs.clear();
    std::vector<int> inputs;
    for (std::size_t i = 0; i < puzzle.inputs.size(); ++i) {
        checkDeadline();
        const auto& molecule = puzzle.inputs[i];
        if (molecule.atoms.empty() ||
            std::none_of(molecule.atoms.begin(), molecule.atoms.end(), [&](const auto& atom) {
                checkDeadline();
                return atom.type >= AtomType::Salt && atom.type <= AtomType::Water;
            }))
            continue;
        checkDeadline();
        source.inputs.push_back(molecule);
        inputs.push_back(static_cast<int>(i));
        checkDeadline();
    }
    int atoms = 0;
    for (const auto& molecule : source.inputs) {
        checkDeadline();
        atoms += static_cast<int>(molecule.atoms.size());
    }
    if (atoms > 0 && atoms < 4) {
        const auto count = source.inputs.size();
        for (std::size_t i = 0; atoms < 4; ++i) {
            checkDeadline();
            source.inputs.push_back(source.inputs[i % count]);
            inputs.push_back(inputs[i % count]);
            atoms += static_cast<int>(source.inputs.back().atoms.size());
            checkDeadline();
        }
    }
    checkDeadline();
    auto result = CompileUnificationFiniteBatch(source, ends, products, deadline);
    checkDeadline();
    for (auto& part : result.objects) {
        checkDeadline();
        if (part.typeName == "input") part.bodyData[2] = inputs.at(part.bodyData[2]);
    }
    checkDeadline();
    return result;
}
[[noreturn]] void Reject(const char* message) {
    throw std::invalid_argument(message);
}
} // namespace
io::SolutionData CompileLifeDeath(const io::PuzzleData& puzzle) {
    return CompileLifeDeath(puzzle, false);
}
namespace {
// Sibling selection without touching the geometry signature: the per-feed clock
// schedule is a scheduling variant, so it is switched by scope like
// `RowIntakeModeScope` rather than by environment.
thread_local bool g_perFeedClocks = false;
// Molecular factory horizontal clearance. Zero tightens the transport corridor a lot
// (`1dc60983` 78166 -> 63966) but not every shape survives it, so callers that want
// the tight layout try it first and fall back to the shipped value when the gate
// rejects. -1 means "use the shipped default".
thread_local int g_factoryClearance = -1;
// First factory column. -100000 means "use the shipped anchor", which is the
// tightest column the corpus tolerates (`a7d3c0bf` overlaps at -40). The column
// only has to clear the assembly row; the shipped -200 left 160 cells of dead
// transport corridor in front of every factory, and each of those cells is
// walked twice per shuttle, so it is the largest cycle lever in the family
// (eleven of thirteen sampled puzzles gain 1.25-1.44x, `fc6a70eb` 10547 -> 7327).
thread_local int g_factoryStart = -100000;
constexpr int kShippedFactoryStart = -60;
io::SolutionData CompileLifeDeathGeometry(
    const io::PuzzleData& puzzle, bool singleProduct, bool compactRelay, bool relayBaseline = false,
    bool compactSpare = false, bool shareFeeds = false,
    std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::time_point::max()) {
    auto checkDeadline = [&] {
        if (std::chrono::steady_clock::now() >= deadline)
            throw std::invalid_argument("shared feed construction deadline");
    };
    checkDeadline();
    // These keys identify requests within this immutable puzzle and invocation.
    // Real suppliers keep the original input index; synthetic suppliers bypass reuse.
    // Extraction always has one origin-centered atom, multiplier one, and the
    // original permissions. Planning adds disposal permission and uses false.
    using SourceKey = std::pair<int, AtomType>;
    using BatchKey = std::tuple<int, AtomType, std::size_t>;
    struct SourceBatch {
        io::SolutionData machine;
        std::vector<int> productEnds;
    };
    constexpr std::size_t sourceCacheLimit = 32;
    std::map<SourceKey, AssemblyPlan> sourcePlans;
    std::map<BatchKey, SourceBatch> sourceBatches;
    auto realExtraction = [&](int input, AtomType type) {
        auto extraction = puzzle;
        extraction.inputs = {puzzle.inputs.at(input)};
        extraction.outputs = {{{{type, {0, 0}}}, {}}};
        extraction.outputMultiplier = 1;
        return extraction;
    };
    auto realSourcePlan = [&](int input, AtomType type) {
        checkDeadline();
        const SourceKey key{input, type};
        if (const auto cached = sourcePlans.find(key); cached != sourcePlans.end()) {
            auto plan = cached->second;
            checkDeadline();
            return plan;
        }
        auto planning = realExtraction(input, type);
        planning.permissionBits |= 1ULL << 17;
        auto plan = AssemblyPlanner::Plan(planning, false, deadline);
        checkDeadline();
        if (sourcePlans.size() < sourceCacheLimit) sourcePlans.emplace(key, plan);
        checkDeadline();
        return plan;
    };
    auto realSourceBatch = [&](int input, AtomType type, std::size_t products,
                               std::vector<int>& productEnds) {
        checkDeadline();
        const BatchKey key{input, type, products};
        // Note: the key already deduplicates identical compilations, and the
        // factories stay 60 cells apart even when they share one. Measured with
        // OPUSOPTIMA_LD_BATCH_DIAG on `1dc60983`: five of the six factories are the
        // same Air extractor (minQ=-17 maxQ=3) yet still lay out at -217,-277,...,
        // so the corridor length comes from giving every feed its own carrier and
        // pickup, not from recompiling the machine.
        if (const auto cached = sourceBatches.find(key); cached != sourceBatches.end()) {
            auto batch = cached->second;
            checkDeadline();
            productEnds = std::move(batch.productEnds);
            checkDeadline();
            return std::move(batch.machine);
        }
        auto extraction = realExtraction(input, type);
        const auto plan = realSourcePlan(input, type);
        SourceBatch batch;
        batch.machine =
            CompileRowAssemblyFiniteBatch(extraction, plan, batch.productEnds, products, deadline);
        checkDeadline();
        // Only complete successful compilations enter the cache, before any
        // placement, input remapping, tape clearing, or resource construction.
        if (sourceBatches.size() < sourceCacheLimit) sourceBatches.emplace(key, batch);
        checkDeadline();
        productEnds = std::move(batch.productEnds);
        return std::move(batch.machine);
    };
    bool sharedPair = false;
    if (shareFeeds &&
        (compactRelay || singleProduct || puzzle.outputs.empty() || puzzle.outputs.size() > 2 ||
         std::none_of(puzzle.inputs.begin(), puzzle.inputs.end(),
                      [](const auto& input) { return input.atoms.size() > 1; })))
        throw std::invalid_argument(
            "shared feed requires one or two outputs and a molecular input");
    if (compactRelay && puzzle.outputs.size() != 1)
        Reject("compact relay requires one ordinary output");
    if (!shareFeeds && !compactRelay && puzzle.outputs.size() == 1 &&
        puzzle.outputs[0].atoms.size() == 1 &&
        puzzle.outputs[0].atoms[0].type == core::AtomType::Quintessence)
        return CompileUnification(puzzle, deadline);
    const int productCount = singleProduct ? 1 : 6 * puzzle.outputMultiplier;
    constexpr auto permissions = AssemblyPlanner::RequiredPermissions | (1ULL << 2) | (1ULL << 3);
    if (puzzle.version != 3 || puzzle.productionInfo || puzzle.outputs.empty() ||
        puzzle.outputs.size() > 4 || puzzle.outputMultiplier < 1 || puzzle.outputMultiplier > 2 ||
        (puzzle.permissionBits & permissions) != permissions)
        Reject("life/death requires finite nonproduction products, assembly permissions, piston "
               "and track");
    auto sources = puzzle;
    sources.inputs.clear();
    struct Source {
        int input;
        AtomType type;
    };
    std::vector<Source> sourceMap;
    for (int i = 0; i < static_cast<int>(puzzle.inputs.size()); ++i) {
        const auto& molecule = puzzle.inputs[i];
        for (std::size_t a = 0; a < molecule.atoms.size(); ++a)
            for (std::size_t b = a + 1; b < molecule.atoms.size(); ++b)
                if (molecule.atoms[a].position == molecule.atoms[b].position)
                    Reject("life/death input contains overlapping atoms");
        for (const auto& atom : molecule.atoms) {
            if (std::any_of(sourceMap.begin(), sourceMap.end(), [&](const auto& source) {
                    return source.input == i && source.type == atom.type;
                }))
                continue;
            if (molecule.atoms.size() != 1 || !molecule.bonds.empty()) {
                auto extraction = puzzle;
                extraction.inputs = {molecule};
                extraction.outputs = {{{{atom.type, {0, 0}}}, {}}};
                extraction.outputMultiplier = 1;
                try {
                    auto planning = extraction;
                    planning.permissionBits |= 1ULL << 17;
                    if (shareFeeds)
                        (void)realSourcePlan(i, atom.type);
                    else
                        (void)AssemblyPlanner::Plan(planning, false, deadline);
                } catch (const std::invalid_argument&) {
                    continue;
                }
            }
            sources.inputs.push_back({{{atom.type, {0, 0}}}, {}});
            sourceMap.push_back({i, atom.type});
        }
    }
    const bool needsSyntheticQuintessence =
        std::any_of(puzzle.outputs.begin(), puzzle.outputs.end(),
                    [](const auto& output) {
                        return std::any_of(
                            output.atoms.begin(), output.atoms.end(),
                            [](const auto& atom) { return atom.type == AtomType::Quintessence; });
                    }) &&
        std::none_of(sourceMap.begin(), sourceMap.end(), [](const auto& source) {
            return source.input >= 0 && source.type == AtomType::Quintessence;
        });
    if (!shareFeeds || needsSyntheticQuintessence) {
        checkDeadline();
        auto extraction = puzzle;
        extraction.outputs = {{{{AtomType::Quintessence, {0, 0}}}, {}}};
        extraction.outputMultiplier = 1;
        try {
            std::vector<int> ends;
            (void)CompileCardinalSource(extraction, ends, 1, deadline);
            sources.inputs.push_back({{{AtomType::Quintessence, {0, 0}}}, {}});
            sourceMap.push_back({-1, AtomType::Quintessence});
        } catch (const std::invalid_argument&) {
        }
    }
    if (puzzle.permissionBits & ((1ULL << 14) | (1ULL << 15))) {
        for (int metal = 7; metal <= 11; ++metal) {
            const auto type = static_cast<AtomType>(metal);
            if (std::none_of(puzzle.outputs.begin(), puzzle.outputs.end(), [&](const auto& output) {
                    return std::any_of(output.atoms.begin(), output.atoms.end(),
                                       [&](const auto& atom) { return atom.type == type; });
                }))
                continue;
            if (std::any_of(sourceMap.begin(), sourceMap.end(),
                            [&](const auto& source) { return source.type == type; }))
                continue;
            auto extraction = puzzle;
            extraction.outputs = {{{{type, {0, 0}}}, {}}};
            extraction.outputMultiplier = 1;
            try {
                try {
                    (void)AssemblyPlanner::PlanSupply(extraction, false, 20000, deadline);
                } catch (const std::invalid_argument&) {
                    (void)CompilePurificationAssembly(extraction, false, deadline);
                }
                sources.inputs.push_back({{{type, {0, 0}}}, {}});
                sourceMap.push_back({-1, type});
            } catch (const std::invalid_argument&) {
            }
        }
    }
    bool needed = false;
    io::SolutionData result;
    result.puzzleName = puzzle.name;
    result.solutionName = "Finite salt chemistry row assembly";
    int offset = 0, armNumber = 0;
    for (std::size_t output = 0; output < puzzle.outputs.size(); ++output) {
        checkDeadline();
        auto single = puzzle;
        single.outputs = {puzzle.outputs[output]};
        single.inputs.clear();
        std::vector<ChemistryDemand> demands;
        for (const auto& atom : puzzle.outputs[output].atoms)
            demands.push_back({atom.type, 1});
        const auto chemistry = ChemistryPipeline::Plan(sources, demands);
        if (chemistry.operations.empty()) Reject("life/death empty chemistry schedule");
        std::vector<int> actual;
        std::vector<ChemistryFeed> feeds;
        std::vector<AtomType> desired;
        // Representative product atom for each deduplicated feed, so a factory can be
        // matched back to the emission that reports when its atom is in place.
        std::vector<std::size_t> atomOfFeed;
        // Every product atom's feed, so an emission can be resolved to the factory
        // that serves it even when one feed covers several product atoms.
        std::vector<std::size_t> feedOfAtom;
        std::size_t demandIndex = 0;
        std::size_t atomIndex = 0;
        for (const auto& atom : puzzle.outputs[output].atoms) {
            const auto feed = chemistry.feeds[demandIndex++];
            const std::size_t thisAtom = atomIndex++;
            if (atom.type == AtomType::Repeat) Reject("life/death repeating targets unsupported");
            const auto existing = std::find(desired.begin(), desired.end(), atom.type);
            if (existing != desired.end()) {
                feedOfAtom.push_back(static_cast<std::size_t>(existing - desired.begin()));
                continue;
            }
            feedOfAtom.push_back(desired.size());
            const int source = feed.input;
            if (feed.kind == ChemistryFeedKind::Animismus ||
                feed.kind == ChemistryFeedKind::Wheel || feed.kind == ChemistryFeedKind::Unify ||
                feed.kind == ChemistryFeedKind::Disperse)
                needed = true;
            if (sourceMap[source].input < 0) needed = true;
            feeds.push_back(feed);
            atomOfFeed.push_back(thisAtom);
            desired.push_back(atom.type);
            const auto proxy = static_cast<AtomType>(desired.size() + 1);
            single.inputs.push_back({{{proxy, {0, 0}}}, {}});
            actual.push_back(sourceMap[source].input);
        }
        if (compactRelay && (feeds.size() != 1 || actual.front() < 0 ||
                             (feeds.front().kind != ChemistryFeedKind::Direct &&
                              feeds.front().kind != ChemistryFeedKind::Calcify)))
            Reject("compact relay supports one direct/calcified molecular feed, no coproducts");
        // Keep the wide unification factory left of all vertical byproduct lanes.
        auto quint = std::find(desired.begin(), desired.end(), AtomType::Quintessence);
        if (quint != desired.end()) {
            const auto index = quint - desired.begin();
            const int source = actual[index];
            const auto feed = feeds[index];
            desired.erase(quint);
            actual.erase(actual.begin() + index);
            feeds.erase(feeds.begin() + index);
            desired.push_back(AtomType::Quintessence);
            actual.push_back(source);
            feeds.push_back(feed);
        }
        if (single.outputs[0].atoms.size() > AssemblyPlanner::MaxAtoms || single.inputs.size() > 8)
            Reject("life/death row bounds exceeded");
        const auto labels = single.outputs[0];
        bool triplex = false;
        for (auto& bond : single.outputs[0].bonds) {
            if (bond.bits && !(bond.bits & ~14)) {
                if (!(puzzle.permissionBits & (1ULL << 11)))
                    Reject("life/death triplex permission missing");
                for (const auto cell : {bond.from, bond.to}) {
                    const auto atom = std::find_if(
                        labels.atoms.begin(), labels.atoms.end(),
                        [&](const auto& candidate) { return candidate.position == cell; });
                    if (atom == labels.atoms.end() || atom->type != AtomType::Fire)
                        Reject("life/death triplex endpoints must be fire");
                }
                bond.bits = 1;
                triplex = true;
            } else if (bond.bits != 1)
                Reject("life/death unsupported bond");
        }
        for (auto& atom : single.outputs[0].atoms)
            atom.type = static_cast<AtomType>(std::find(desired.begin(), desired.end(), atom.type) -
                                              desired.begin() + 2);
        // Negative entries are planning placeholders; their bounded factories consume
        // original puzzle inputs through metal or whole-molecule cardinal lowering.
        const bool unsupportedSynthetic = [&] {
            for (std::size_t i = 0; i < actual.size(); ++i) {
                if (actual[i] >= 0) continue;
                const auto type = sources.inputs[feeds[i].input].atoms[0].type;
                if (feeds[i].kind != ChemistryFeedKind::Direct ||
                    !((type >= AtomType::Gold && type <= AtomType::Tin) ||
                      type == AtomType::Quintessence))
                    return true;
            }
            return false;
        }();
        if (shareFeeds && unsupportedSynthetic)
            Reject("shared feed requires bounded real-source lowering");
        const auto plan = AssemblyPlanner::Plan(single, false, deadline);
        const bool dispersionStorage =
            !(puzzle.permissionBits & (1ULL << 17)) &&
            std::any_of(feeds.begin(), feeds.end(),
                        [](const auto& feed) { return feed.kind == ChemistryFeedKind::Disperse; });
        const int feedSpacing = dispersionStorage
                                    ? 64 + 18 * puzzle.outputMultiplier *
                                               static_cast<int>(puzzle.outputs[output].atoms.size())
                                    : 3;
        if (shareFeeds && feedSpacing != 3) Reject("shared feed requires ordinary feed spacing");
        if (singleProduct && triplex)
            Reject("single-product chemistry does not support colored bonds");
        // Explicit pickup schedule. `CompileRows` can report one emission per atom it
        // hands to the row (`readyCycle` = when the atom is in place), which is the
        // information `events` currently recovers by walking the feed arm's tape. The
        // tape walk cannot survive a widened intake -- its arm only travels 0..9 cells
        // and never crosses the factory columns -- so the explicit table is the only
        // route to that layout.
        const char* emissionFlag = std::getenv("OPUSOPTIMA_LD_EMISSION_SCHEDULE");
        const bool useEmissions = emissionFlag != nullptr && std::string_view(emissionFlag) == "on";
        std::vector<SharedFactoryEmission> emissions;
        std::vector<ProductAtomRef> emissionOwners;
        if (useEmissions)
            for (std::size_t atom = 0; atom < single.outputs[0].atoms.size(); ++atom)
                emissionOwners.push_back({0, atom});
        auto machine = singleProduct ? CompileRowAssemblySingleProduct(single, plan)
                       : triplex     ? CompileTriplexRowAssembly(single, plan, labels, false,
                                                                 feedSpacing, deadline)
                       : shareFeeds  ? CompileRowAssembly(single, plan, deadline)
                       : useEmissions
                             ? CompileRows(single, plan, nullptr, nullptr, nullptr, true, nullptr,
                                           false, 0, nullptr, nullptr, feedSpacing, false, false,
                                           false, false, &emissions, &emissionOwners)
                             : CompileRowAssembly(single, plan, feedSpacing);
        if (useEmissions)
            GetLoggerOrFallback("solver")->info(
                "Assembly branch=life-death event=emissions count={} atoms={} owners={}",
                emissions.size(), single.outputs[0].atoms.size(), emissionOwners.size());
        int feedArm = -1, feedOffset = 0;
        for (std::size_t i = 0; i < machine.objects.size(); ++i) {
            const auto& o = machine.objects[i];
            if (o.typeName == "piston" && o.position.r == 3) {
                feedArm = static_cast<int>(i);
                feedOffset = -o.position.q;
            }
        }
        if (feedArm < 0) Reject("life/death feed arm unavailable");
        struct Factory {
            int q, distance;
            AtomType type;
            bool animismus = false;
            // Feed this factory is supplied from; matched against the emission table
            // through `feedOfAtom`.
            std::size_t feed{};
        };
        struct SpareOutlet {
            int factory, arm;
            bool horizontal;
        };
        std::vector<SpareOutlet> spareOutlets;
        struct MolecularFeed {
            core::HexCoord cell;
            io::SolutionData batch;
            std::vector<int> objects;
            int duration = 0, carrier = -1, pickup = 0, height = 0, distance = 0, factoryQ = 0;
            int elevator = 0;
            int elevatorTop = 88, elevatorDrop = 6, carrierEnd = 90;
            std::vector<int> productEnds;
            std::optional<FiniteFeedResource> resource;
            std::optional<std::size_t> sharedProducer;
            std::size_t expectedRequests{}, consumedRequests{};
        };
        std::vector<MolecularFeed> molecularFeeds;
        // First factory column. Every factory then steps left by its own width plus
        // the clearance, so this starting point is added to the whole transport
        // corridor: on `1dc60983` it puts the six extractors at -217..-517 while the
        // assembly row sits at -24, which is the 500-cell haul the period pays for.
        const char* factoryStartFlag = std::getenv("OPUSOPTIMA_LD_FACTORY_START");
        int nextFactoryRight = factoryStartFlag   ? std::atoi(factoryStartFlag)
                               : g_factoryStart > -100000 ? g_factoryStart
                                                          : kShippedFactoryStart;
        auto molecularInput = [&](int source, AtomType type) {
            return source < 0 || puzzle.inputs[source].atoms.size() != 1 ||
                   !puzzle.inputs[source].bonds.empty() ||
                   puzzle.inputs[source].atoms[0].type != type;
        };
        std::vector<Factory> factories;
        io::SolutionData quintessence;
        std::vector<int> quintObjects;
        int quintCarrier = -1;
        int quintDuration = 0;
        auto object = [&](const char* type, core::HexCoord pos, int length, int rotation,
                          int index = 0) {
            io::SolutionObject o;
            o.typeName = type;
            o.position = pos;
            o.bodyData = {length, rotation, index, 0, 0};
            machine.objects.push_back(o);
            return static_cast<int>(machine.objects.size()) - 1;
        };
        int assemblyTop = 0;
        for (const auto& part : machine.objects) {
            assemblyTop = std::max(assemblyTop, part.position.r);
            if (part.typeName == "track")
                for (int j = 0; j < part.bodyData[4]; ++j)
                    assemblyTop = std::max(assemblyTop, part.position.r + part.bodyData[6 + 2 * j]);
        }
        auto addFeed = [&](core::HexCoord cell, std::size_t index, int factoryQ,
                           bool shared = false) {
            checkDeadline();
            const int input = actual[index];
            const auto sourceType = sources.inputs[feeds[index].input].atoms[0].type;
            if (!molecularInput(input, sourceType)) {
                object("input", core::Subtract(cell, puzzle.inputs[input].atoms[0].position), 1, 0,
                       input);
                return;
            }
            MolecularFeed feed;
            feed.cell = cell;
            feed.factoryQ = factoryQ;
            auto extraction = puzzle;
            if (input >= 0) extraction.inputs = {puzzle.inputs[input]};
            extraction.outputs = {{{{sourceType, {0, 0}}}, {}}};
            extraction.outputMultiplier = 1;
            if (input < 0 && sourceType == AtomType::Quintessence) {
                const int products =
                    productCount *
                    std::count_if(puzzle.outputs[output].atoms.begin(),
                                  puzzle.outputs[output].atoms.end(),
                                  [&](const auto& atom) { return atom.type == desired[index]; });
                feed.batch =
                    CompileCardinalSource(extraction, feed.productEnds, products, deadline);
            } else
                try {
                    auto planning = extraction;
                    planning.permissionBits |= 1ULL << 17;
                    const auto products =
                        productCount * std::count_if(puzzle.outputs[output].atoms.begin(),
                                                     puzzle.outputs[output].atoms.end(),
                                                     [&](const auto& atom) {
                                                         return atom.type == desired[index];
                                                     });
                    if (shared) feed.expectedRequests = static_cast<std::size_t>(products);
                    const auto finiteProducts =
                        static_cast<std::size_t>(products) * (shared ? 2 : 1);
                    feed.batch =
                        shareFeeds && input >= 0
                            ? realSourceBatch(input, sourceType, finiteProducts, feed.productEnds)
                            : CompileRowAssemblyFiniteBatch(
                                  extraction, AssemblyPlanner::Plan(planning, false, deadline),
                                  feed.productEnds, finiteProducts, deadline);
                    if (shared && (feed.productEnds.empty() ||
                                   feed.productEnds.size() != 2 * feed.expectedRequests))
                        Reject("shared feed requires every finite product boundary");
                } catch (const std::invalid_argument&) {
                    if (input >= 0) throw;
                    feed.productEnds.clear();
                    feed.batch = CompilePurificationAssembly(extraction, false, deadline);
                }
            int minimumQ = 0, maximumQ = 0;
            if (std::getenv("OPUSOPTIMA_LD_BATCH_DIAG")) {
                std::size_t batchInstr = 0;
                for (const auto& part : feed.batch.objects)
                    batchInstr += part.instructions.size();
                std::fprintf(stderr,
                             "BATCHDIAG input=%d type=%d objs=%zu instr=%zu cell=(%d,%d)\n",
                             input, static_cast<int>(sourceType), feed.batch.objects.size(),
                             batchInstr, cell.q, cell.r);
            }
            for (const auto& part : feed.batch.objects) {
                minimumQ = std::min(minimumQ, part.position.q);
                maximumQ = std::max(maximumQ, part.position.q);
                if (part.typeName == "track")
                    for (int j = 0; j < part.bodyData[4]; ++j) {
                        minimumQ = std::min(minimumQ, part.position.q + part.bodyData[5 + 2 * j]);
                        maximumQ = std::max(maximumQ, part.position.q + part.bodyData[5 + 2 * j]);
                    }
            }
            // Rotate each factory into a disjoint horizontal envelope, leaving
            // clearance for its arms and held molecules instead of a fixed stride.
            const int left = nextFactoryRight + minimumQ;
            // The factories accumulate leftwards, so this clearance is multiplied by
            // the feed count and then becomes the transport corridor length - which
            // the period pays one instruction per cell for. Lowering it is a real
            // win (`1dc60983` 78166 -> 63966 at 0) but it is not universally safe:
            // `deecf24d`, `e55f57a6` and `ac8d3e1b` lose their layout, and they reach
            // this code through independent-outputs where the original multi-output
            // shape is no longer visible. Shipped value stays 40; the knob is here for
            // measurement and for whoever teaches the planner to stack the factories
            // vertically instead of spreading them along the corridor.
            const char* factoryClearFlag = std::getenv("OPUSOPTIMA_LD_FACTORY_CLEARANCE");
            const int factoryClear =
                factoryClearFlag ? std::atoi(factoryClearFlag)
                                 : (g_factoryClearance >= 0 ? g_factoryClearance : 40);
            if (std::getenv("OPUSOPTIMA_LD_CLEARANCE_DIAG"))
                std::fprintf(stderr, "CLEARANCEDIAG value=%d tl=%d feeds=%zu\n", factoryClear,
                             g_factoryClearance, molecularFeeds.size());
            if (std::getenv("OPUSOPTIMA_LD_BATCH_DIAG"))
                std::fprintf(stderr, "BATCHWIDTH input=%d type=%d minQ=%d maxQ=%d left=%d\n",
                             input, static_cast<int>(sourceType), minimumQ, maximumQ, left);
            nextFactoryRight = left - maximumQ - factoryClear;
            feed.pickup = compactRelay ? cell.q - 4 : left - 2;
            core::HexCoord delivery{};
            for (const auto& part : feed.batch.objects)
                if (part.typeName == "out-std") delivery = part.position;
            // Nested extraction machines may extend well below their output after
            // rotation. Keep their rails above the shared transport corridor.
            // Note: stacking the factories vertically does not shorten anything -
            // measured identical cycles at OPUSOPTIMA_LD_HEIGHT_STRIDE=40 because
            // `transportPath` is pinned to r=88 regardless of feed height, so the
            // corridor length is decided purely by the q spread.
            feed.height = compactRelay ? assemblyTop + 16 : 100;
            for (const auto& part : feed.batch.objects) {
                int extent = part.position.r;
                if (part.typeName == "track")
                    for (int j = 0; j < part.bodyData[4]; ++j)
                        extent = std::max(extent, part.position.r + part.bodyData[6 + 2 * j]);
                feed.height = std::max(feed.height, (compactRelay ? assemblyTop + 16 : 110) +
                                                        extent - delivery.r);
            }
            const auto translation = core::Add({feed.pickup, feed.height + 1}, delivery);
            for (auto part : feed.batch.objects) {
                if (part.typeName == "out-std") continue;
                part.position = core::Add({-part.position.q, -part.position.r}, translation);
                part.bodyData[1] = (part.bodyData[1] + 3) % 6;
                if (part.typeName == "track")
                    for (int j = 0; j < part.bodyData[4]; ++j) {
                        part.bodyData[5 + 2 * j] *= -1;
                        part.bodyData[6 + 2 * j] *= -1;
                    }
                if (part.typeName == "input" && input >= 0) part.bodyData[2] = input;
                for (auto ins : part.instructions)
                    feed.duration = std::max(feed.duration, ins.parameter + 1);
                feed.objects.push_back(static_cast<int>(machine.objects.size()));
                machine.objects.push_back(std::move(part));
            }
            feed.resource.emplace(ModulePort{{feed.pickup, feed.height + 1}, 0, false},
                                  feed.productEnds, feed.duration,
                                  shared ? std::optional<std::size_t>(2 * feed.expectedRequests)
                                         : std::nullopt);
            checkDeadline();
            molecularFeeds.push_back(std::move(feed));
        };
        const auto originalCount = machine.objects.size();
        for (std::size_t i = 0; i < originalCount; ++i) {
            auto& o = machine.objects[i];
            if (o.typeName != "input") continue;
            const int index = o.bodyData[2];
            const auto type = desired[index];
            if (feeds[index].kind == ChemistryFeedKind::Disperse) {
                const auto cell = o.position;
                const std::array<core::HexCoord, 4> outlets{{{-1, 0}, {1, 0}, {0, -1}, {1, -1}}};
                const int selected = feeds[index].calcifySource ? 0 : static_cast<int>(type) - 2;
                auto offset = outlets[selected];
                int rotation = 0;
                for (int turn = 1; turn < 6; ++turn) {
                    auto candidate = outlets[selected];
                    for (int j = 0; j < turn; ++j)
                        candidate = core::RotateClockwise(candidate);
                    if (candidate.r > offset.r) {
                        offset = candidate;
                        rotation = turn;
                    }
                }
                const auto center = core::Subtract(cell, offset);
                // Appending objects invalidates references into machine.objects.
                const bool molecular =
                    molecularInput(actual[index], sources.inputs[feeds[index].input].atoms[0].type);
                if (molecular)
                    o.typeName = "glyph-marker";
                else {
                    o.position =
                        core::Subtract(center, puzzle.inputs[actual[index]].atoms[0].position);
                    o.bodyData[2] = actual[index];
                }
                const bool park = !(puzzle.permissionBits & (1ULL << 17));
                const int factory = static_cast<int>(factories.size());
                if (molecular) addFeed(center, index, cell.q);
                if (molecular || park)
                    factories.push_back({cell.q, 5 + 2 * (-feedOffset - cell.q), type, false, static_cast<std::size_t>(index)});
                object("glyph-dispersion", center, 1, rotation);
                if (feeds[index].calcifySource) object("glyph-calcification", cell, 1, 0);
                for (int other = 0; other < 4; ++other)
                    if (other != selected) {
                        auto delta = outlets[other];
                        for (int j = 0; j < rotation; ++j)
                            delta = core::RotateClockwise(delta);
                        const auto outlet = core::Add(center, delta);
                        if (!park) {
                            object("glyph-disposal", outlet, 1, 0);
                            continue;
                        }
                        const auto direction = std::find(core::kAxialDirections.begin(),
                                                         core::kAxialDirections.end(), delta);
                        if (direction == core::kAxialDirections.end())
                            Reject("dispersion outlet is not radial");
                        const int angle =
                            static_cast<int>(direction - core::kAxialDirections.begin());
                        const auto base = core::Add(outlet, delta);
                        const int arm =
                            object(delta.r > 0 ? "piston" : "arm1", base, 1, (9 - angle) % 6);
                        const int rail = object("track", base, 1, 0);
                        const int length =
                            6 * puzzle.outputMultiplier *
                                static_cast<int>(puzzle.outputs[output].atoms.size()) * 3 +
                            15;
                        auto& data = machine.objects[rail].bodyData;
                        data = {1, 0, 0, 0, length + 1};
                        // Keep storage tracks above the feed corridor. Intersecting tracks
                        // form junctions, so a radial rail can divert onto the feed track.
                        const auto travel = delta.r > 0 ? core::HexCoord{-1, 0} : delta;
                        for (int step = 0; step <= length; ++step)
                            data.insert(data.end(), {step * travel.q, step * travel.r});
                        data.push_back(0);
                        spareOutlets.push_back({factory, arm, delta.r > 0});
                    }
                needed = true;
                continue;
            }
            if (feeds[index].kind == ChemistryFeedKind::Unify) {
                if (molecularInput(actual[index], sources.inputs[feeds[index].input].atoms[0].type))
                    Reject("molecular unification batch unsupported");
                const int q = o.position.q;
                o.typeName = "glyph-marker";
                factories.push_back({q, 5 + 2 * (-feedOffset - q), type, false, static_cast<std::size_t>(index)});
                quintessence = CompileSaltQuintessenceBatch(puzzle, actual[index], deadline);
                core::HexCoord delivery{};
                for (const auto& part : quintessence.objects)
                    if (part.typeName == "out-std") delivery = part.position;
                for (auto part : quintessence.objects) {
                    if (part.typeName == "out-std") continue;
                    part.position = core::Add(part.position, core::Subtract({q, -20}, delivery));
                    for (const auto& ins : part.instructions)
                        quintDuration = std::max(quintDuration, ins.parameter + 1);
                    quintObjects.push_back(static_cast<int>(machine.objects.size()));
                    machine.objects.push_back(std::move(part));
                }
                quintCarrier = object("piston", {q, -23}, 3, 1);
                const int rail = object("track", {q, -23}, 1, 0);
                auto& data = machine.objects[rail].bodyData;
                data = {1, 0, 0, 0, 25};
                for (int r = 0; r <= 24; ++r)
                    data.insert(data.end(), {0, r});
                data.push_back(0);
                continue;
            }
            if (feeds[index].kind != ChemistryFeedKind::Animismus) {
                const auto cell = o.position;
                const auto sourceType = sources.inputs[feeds[index].input].atoms[0].type;
                if (molecularInput(actual[index], sourceType)) {
                    o.typeName = "glyph-marker";
                    addFeed(cell, index, cell.q);
                } else {
                    o.position =
                        core::Subtract(cell, puzzle.inputs[actual[index]].atoms[0].position);
                    o.bodyData[2] = actual[index];
                }
                if (type >= AtomType::Air && type <= AtomType::Water &&
                    sourceType == AtomType::Salt) {
                    const std::array<core::HexCoord, 6> wheel{
                        {{}, {}, {-1, 1}, {1, -1}, {0, -1}, {0, 1}}};
                    auto direction = wheel[static_cast<int>(type)];
                    int rotation = 0;
                    while (direction != core::HexCoord{0, 1}) {
                        direction = core::RotateClockwise(direction);
                        ++rotation;
                    }
                    object("baron", core::Add(cell, {0, -2}), 1, rotation);
                    object("glyph-duplication", core::Add(cell, {0, -1}), 1, 1);
                }
                if (type == AtomType::Salt && sourceType != AtomType::Salt)
                    object("glyph-calcification", cell, 1, 0);
                if (molecularInput(actual[index], sourceType))
                    factories.push_back({cell.q, 5 + 2 * (-feedOffset - cell.q), type, false, static_cast<std::size_t>(index)});
                continue;
            }
            const int q = o.position.q;
            const auto saltType = sources.inputs[feeds[index].input].atoms[0].type;
            o.typeName = "glyph-life-and-death";
            o.position = {q + (type == AtomType::Mors), 3};
            o.bodyData[1] = type == AtomType::Mors ? 3 : 0;
            o.bodyData[2] = 0;
            factories.push_back({q, 5 + 2 * (-feedOffset - q), type, true, static_cast<std::size_t>(index)});
            // ianh/omsim sim.c, ANIMISMUS (a88d12d8d100784aa525ee90259255b869bcba38):
            // ungrabbed, unbonded salts at (0,0),(1,0), vitae at (0,1), mors
            // at (1,-1). A blocked output stops conversion.
            const bool shared =
                shareFeeds && actual[index] >= 0 && puzzle.inputs[actual[index]].atoms.size() > 1;
            const auto producerIndex = molecularFeeds.size();
            for (int x = q; x <= q + 1; ++x) {
                if (shared && x == q + 1) {
                    MolecularFeed request;
                    request.cell = {x, 3};
                    request.factoryQ = q;
                    request.sharedProducer = producerIndex;
                    request.expectedRequests = molecularFeeds.at(producerIndex).expectedRequests;
                    molecularFeeds.push_back(std::move(request));
                    sharedPair = true;
                } else
                    addFeed({x, 3}, index, q, shared);
                if (saltType != AtomType::Salt) object("glyph-calcification", {x, 3}, 1, 0);
            }
        }
        checkDeadline();
        constexpr int transportRow = 6;
        // One elevator carries every atom, so its tape sets the machine period.
        // Splitting the feeds across K elevators divides that tape by K, and each
        // elevator owns a contiguous block of factory columns so that no two top
        // lanes, no two descents and no two bottom lanes share a cell.
        const char* elevatorFlag = std::getenv("OPUSOPTIMA_LD_ELEVATORS");
        const int elevatorLimit = elevatorFlag ? std::max(1, std::atoi(elevatorFlag)) : 1;
        std::vector<int> transportArms, transportPositions, transportLocks;
        std::vector<std::vector<core::HexCoord>> transportPaths;
        if (compactRelay && molecularFeeds.size() != 1)
            Reject("compact relay requires exactly one extracted molecular channel");
        if (compactRelay) {
            // A radius-two arm bridges the local carrier outlet to the real inlet.
            // Its base stays left of the moving assembly feed arm.
            // Factory placement uses its full rail envelope above the assembly.
            transportArms.assign(1, -1);
            transportPositions.assign(1, 0);
            transportLocks.assign(1, 2);
            transportPaths.assign(1, {});
            transportArms[0] =
                object("arm1", core::Add(molecularFeeds.front().cell, {-2, 0}), 2, 3);
        } else if (!molecularFeeds.empty()) {
            std::vector<std::size_t> order(molecularFeeds.size());
            for (std::size_t i = 0; i < order.size(); ++i) order[i] = i;
            std::stable_sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
                return molecularFeeds[a].cell.q < molecularFeeds[b].cell.q;
            });
            const int groups =
                compactRelay ? 1
                             : std::min<int>(elevatorLimit, static_cast<int>(order.size()));
            // Grouping follows the delivery columns so that a group's bottom lane
            // never runs over another group's drop point, and each lane sits one
            // row pair above it. The lane itself only spans its own factories.
            for (std::size_t i = 0; i < order.size(); ++i) {
                auto& member = molecularFeeds[order[i]];
                member.elevator = static_cast<int>(i * groups / order.size());
                member.elevatorDrop = transportRow + 2 * member.elevator;
                member.elevatorTop = 88 - 2 * member.elevator;
                member.carrierEnd = member.elevatorTop + 2;
            }
            transportArms.assign(groups, -1);
            transportPositions.assign(groups, 0);
            transportLocks.assign(groups, 2);
            transportPaths.assign(groups, {});
            for (int group = 0; group < groups; ++group) {
                int left = 0, right = 0, pickup = 0;
                bool empty = true;
                for (const auto& feed : molecularFeeds) {
                    if (feed.elevator != group) continue;
                    if (empty) {
                        left = right = feed.cell.q;
                        pickup = feed.pickup;
                        empty = false;
                    }
                    left = std::min(left, feed.cell.q);
                    right = std::max(right, feed.cell.q);
                    if (!feed.sharedProducer) pickup = std::min(pickup, feed.pickup);
                }
                if (empty) continue;
                const int topRow = 88 - 2 * group;
                const int dropRow = transportRow + 2 * group;
                int pickupLeft = pickup, pickupRight = pickup;
                for (const auto& feed : molecularFeeds) {
                    if (feed.elevator != group) continue;
                    pickupLeft = std::min(pickupLeft, feed.pickup);
                    pickupRight = std::max(pickupRight, feed.pickup);
                }
                // Descend inside the gap that separates this group's factories from
                // the next group's, so no lane ever crosses another group's carrier
                // rail. The rightmost group keeps the assembly-side rule below.
                int nextPickup = 0;
                bool haveNext = false;
                for (const auto& feed : molecularFeeds) {
                    if (feed.elevator <= group) continue;
                    nextPickup = haveNext ? std::min(nextPickup, feed.pickup) : feed.pickup;
                    haveNext = true;
                }
                int bend = haveNext && nextPickup - pickupRight > 2
                               ? (pickupRight + nextPickup) / 2
                               : left - 20;
                // Tracks share a global junction table in the simulator. Route the
                // descent outside the assembly rails, not merely outside deliveries.
                for (const auto& part : machine.objects) {
                    if (part.typeName != "track") continue;
                    for (int i = 0; i < part.bodyData[4]; ++i) {
                        const int r = part.position.r + part.bodyData[6 + 2 * i];
                        if (r <= dropRow)
                            bend = std::min(bend, part.position.q + part.bodyData[5 + 2 * i] - 2);
                    }
                }
                auto& path = transportPaths[group];
                const int step = pickupLeft <= bend ? 1 : -1;
                for (int q = pickupLeft;; q += step) {
                    path.push_back({q, topRow});
                    if (q == bend) break;
                }
                for (int r = topRow - 1; r >= dropRow; --r)
                    path.push_back({bend, r});
                for (int q = bend + 1; q <= right; ++q)
                    path.push_back({q, dropRow});
            }
            // Objects are created only after every lane is placed. Creating them
            // inside the loop let the track-dodging scan below see the previous
            // group's own descent rail and drag this group's descent onto it.
            for (int group = 0; group < groups; ++group) {
                auto& path = transportPaths[group];
                if (path.empty()) continue;
                const int topRow = 88 - 2 * group;
                transportArms[group] = object("piston", path.front(), 1, 1);
                const int rail = object("track", path.front(), 1, 0);
                if (std::getenv("OPUSOPTIMA_LD_TRANSPORT_DIAG"))
                    std::fprintf(stderr, "TRANSPORT group=%d top=%d start=(%d,%d) path=%zu\n",
                                 group, topRow, path.front().q, path.front().r, path.size());
                auto& data = machine.objects[rail].bodyData;
                data = {1, 0, 0, 0, static_cast<int>(path.size())};
                for (auto cell : path)
                    data.insert(data.end(), {cell.q - path.front().q, cell.r - topRow});
                data.push_back(0);
            }
        }
        for (auto& feed : molecularFeeds) {
            if (feed.sharedProducer) continue;
            checkDeadline();
            feed.carrier =
                object("piston", {feed.pickup, feed.height - 2 - CarrierIdleClearance}, 1, 1);
            const int rail = object("track", {feed.pickup, feed.height - 2}, 1, 0);
            std::vector<core::HexCoord> path;
            for (int r = feed.height - 2;
                 r >= (compactRelay ? feed.cell.r + 1 : feed.carrierEnd); --r)
                path.push_back({feed.pickup, r});
            auto& data = machine.objects[rail].bodyData;
            data = {1, 0, 0, 0, static_cast<int>(path.size())};
            feed.distance = static_cast<int>(path.size()) - 1;
            for (const auto cell : path)
                data.insert(data.end(), {cell.q - feed.pickup, cell.r - feed.height + 2});
            data.push_back(0);
        }
        if (!factories.empty()) {
            std::vector<io::SolutionObject> molecularProgram = machine.objects;
            for (const auto& feed : molecularFeeds)
                for (int id : feed.objects)
                    machine.objects[id].instructions.clear();
            std::vector<io::SolutionObject> quintProgram;
            for (int id : quintObjects) {
                quintProgram.push_back(machine.objects[id]);
                machine.objects[id].instructions.clear();
            }
            int period = 0;
            for (const auto& o : machine.objects)
                for (auto instruction : o.instructions)
                    period = std::max(period, instruction.parameter + 1);
            struct Event {
                int time, factory;
            };
            std::vector<Event> events;
            const auto tape = machine.objects[feedArm].instructions;
            for (auto ins : tape)
                if (ins.action == 'G') {
                    int position = 0;
                    for (auto prior : tape) {
                        if (prior.parameter >= ins.parameter) break;
                        if (prior.action == 'A') ++position;
                        if (prior.action == 'a') --position;
                    }
                    for (int f = 0; f < static_cast<int>(factories.size()); ++f)
                        if (position == factories[f].distance)
                            events.push_back({ins.parameter - factories[f].distance - 8, f});
                }
            if (const char* probe = std::getenv("OPUSOPTIMA_LD_EMISSION_PROBE");
                probe != nullptr && *probe != '\0') {
                std::string line = "Assembly branch=life-death event=emission-probe tapeEvents=";
                for (std::size_t i = 0; i < events.size() && i < 4; ++i)
                    line += std::to_string(events[i].time) + "/" + std::to_string(events[i].factory) +
                            " ";
                line += "| emissions:";
                for (std::size_t i = 0; i < emissions.size() && i < 4; ++i) {
                    const auto& e = emissions[i];
                    line += " [beg=" + std::to_string(e.beginCycle) +
                            " hold=" + std::to_string(e.holderReleaseCycle) +
                            " rdy=" + std::to_string(e.readyCycle) +
                            " rel=" + std::to_string(e.releaseCycle) +
                            " atom=" + std::to_string(e.consumer.atom) + "]";
                }
                GetLoggerOrFallback("solver")->info("{}", line);
            }
            if (useEmissions && !emissions.empty()) {
                // Replace the tape-position inference with the explicit table: each
                // emission says when its atom is in place, and every factory knows
                // which product atom it feeds. `LEAD` retracts the schedule so the
                // molecular extraction still finishes ahead of the drop.
                // `beginCycle` is exactly the quantity the tape walk recovered
                // (`'G' tick - distance - 8`), verified against the plain layout below,
                // so the schedule is reproduced verbatim; a widened layout, whose arm
                // never crosses the factory columns, supplies its own lead instead.
                const char* leadFlag = std::getenv("OPUSOPTIMA_LD_EMISSION_LEAD");
                const bool explicitLead = leadFlag != nullptr;
                const int lead = explicitLead ? std::max(0, std::atoi(leadFlag)) : 0;
                events.clear();
                const auto feedOf = [&](std::size_t atom) -> std::size_t {
                    return atom < feedOfAtom.size() ? feedOfAtom[atom] : 0;
                };
                // One event per *extraction*, not per product atom: a single
                // dispersion/unification hands several atoms to different factories at
                // the same `beginCycle`, and the tape walk recorded one pickup for it.
                std::vector<int> extractions;
                for (const auto& emission : emissions) {
                    if (std::find(extractions.begin(), extractions.end(), emission.beginCycle) !=
                        extractions.end())
                        continue;
                    int factory = -1;
                    for (int f = 0; f < static_cast<int>(factories.size()); ++f)
                        if (factories[f].feed == feedOf(emission.consumer.atom)) {
                            factory = f;
                            break;
                        }
                    if (factory < 0) continue;
                    extractions.push_back(emission.beginCycle);
                    // `beginCycle` already equals the value the tape walk derived
                    // (`'G' tick - distance - 8`); subtracting again would double-count.
                    events.push_back({emission.beginCycle - (explicitLead ? lead : 0), factory});
                }
                std::sort(events.begin(), events.end(),
                          [](const Event& a, const Event& b) { return a.time < b.time; });
                int lowest = period;
                for (const auto& entry : events) lowest = std::min(lowest, entry.time);
                GetLoggerOrFallback("solver")->info(
                    "Assembly branch=life-death event=emission-schedule events={} period={} "
                    "lowest={} lead={}",
                    events.size(), period, lowest, lead);
            }
            std::sort(events.begin(), events.end(),
                      [](const Event& a, const Event& b) { return a.time < b.time; });
            if (events.empty() || events.front().time < 0)
                Reject("life/death pickup schedule unsupported");
            const int repetitions = productCount;
            for (const auto& feed : molecularFeeds) {
                if (!feed.expectedRequests) continue;
                const auto eventsPerProduct =
                    std::count_if(events.begin(), events.end(), [&](const auto& event) {
                        return factories[event.factory].q == feed.factoryQ;
                    });
                if (static_cast<std::size_t>(eventsPerProduct) * repetitions !=
                    feed.expectedRequests)
                    Reject("shared feed request count differs from finite demand");
            }
            // Separate vertical lanes keep collectors clear of other factories.
            // Dropping sideways leaves every lane free for the return journey.
            std::vector<int> spareArms, stored(factories.size());
            for (const auto& factory : factories) {
                if (!factory.animismus) {
                    spareArms.push_back(-1);
                    continue;
                }
                spareArms.push_back(object("piston", {factory.q + 1, 1}, 1, 1));
                const int rail = object("track", {factory.q + 1, 1}, 1, 0);
                auto& data = machine.objects[rail].bodyData;
                const auto pickups =
                    std::count_if(events.begin(), events.end(), [&](const auto& event) {
                        return event.factory == static_cast<int>(spareArms.size()) - 1;
                    });
                const int length = compactSpare
                                       ? static_cast<int>(pickups) * repetitions * 2 + 3
                                       : static_cast<int>(events.size()) * repetitions * 3 + 3;
                data = {1, 0, 0, 0, length + 1};
                for (int r = -length; r <= 0; ++r)
                    data.insert(data.end(), {0, r});
                data.push_back(0);
            }
            struct ReplayInstruction {
                std::size_t object;
                io::ArmInstruction instruction;
            };
            std::vector<ReplayInstruction> replay;
            for (std::size_t i = 0; i < machine.objects.size(); ++i)
                for (auto instruction : machine.objects[i].instructions)
                    if (instruction.parameter >= 0 && instruction.parameter < period)
                        replay.push_back({i, instruction});
            // Stable time ordering preserves object and tape order at equal cycles.
            // The immutable index is local to this output before its tapes are cleared.
            std::stable_sort(replay.begin(), replay.end(), [](const auto& a, const auto& b) {
                return a.instruction.parameter < b.instruction.parameter;
            });
            for (auto& o : machine.objects)
                o.instructions.clear();
            // Each molecular feed gets its own clock. Previously every object's tape
            // was appended to one global cursor, so the period was the *sum* of all
            // work (`instr/cycles` = 1.02 and `lastTick/cycles` = 1.00 on the losing
            // puzzles). Independent clocks make it the *max*, which is what the
            // reference machines do. Coordinates are untouched.
            int globalShift = 2;
            std::vector<int> feedShift(molecularFeeds.size(), 2);
            const bool perFeedClocks =
                g_perFeedClocks || std::getenv("OPUSOPTIMA_LD_FEED_CLOCKS") != nullptr;
            int* clockPtr = &globalShift;
            int activeArm = 0;
            // A machine may be shared between feeds (the transport rail is), so the
            // clock alone cannot order its tape. Each object keeps its own cursor and
            // a write never lands earlier than the clock it belongs to; that keeps the
            // timeline parallel across objects while staying strictly increasing
            // within one, which the decoder requires.
            std::vector<int> objectCursor(machine.objects.size(), 2);
            auto emit = [&](char action) {
                if (!perFeedClocks) {
                    // Legacy path byte-for-byte: one shared cursor, never behind.
                    machine.objects[activeArm].instructions.push_back({action, (*clockPtr)++});
                    return;
                }
                int& cursor = objectCursor[static_cast<std::size_t>(activeArm)];
                if (cursor < *clockPtr) cursor = *clockPtr;
                machine.objects[activeArm].instructions.push_back({action, cursor++});
                *clockPtr = std::max(*clockPtr, cursor);
            };
            // Each transport rail is owned by one feed group, so its occupancy still
            // has to be exclusive within that group: a feed waits for its rail to fall
            // idle, drives it, and leaves the rail's clock where it stopped.
            std::size_t event = 0;
            for (int repeat = 0; repeat < repetitions; ++repeat) {
                event = 0;
                std::size_t replayIndex = 0;
                for (int t = 0; t < period; ++t) {
                    while (event < events.size() && events[event].time == t) {
                        const int f = events[event++].factory;
                        if (factories[f].type == AtomType::Quintessence && quintCarrier >= 0) {
                            for (std::size_t j = 0; j < quintProgram.size(); ++j)
                                for (auto ins : quintProgram[j].instructions) {
                                    ins.parameter += globalShift;
                                    machine.objects[quintObjects[j]].instructions.push_back(ins);
                                }
                            globalShift += quintDuration + 1;
                            activeArm = quintCarrier;
                            emit('G');
                            for (int i = 0; i < 24; ++i)
                                emit('A');
                            emit('g');
                            for (int i = 0; i < 24; ++i)
                                emit('a');
                            continue;
                        }
                        for (std::size_t feedIndex = 0; feedIndex < molecularFeeds.size();
                             ++feedIndex) {
                            auto& feed = molecularFeeds[feedIndex];
                            if (feed.factoryQ != factories[f].q) continue;
                            checkDeadline();
                            auto& producer = feed.sharedProducer
                                                 ? molecularFeeds.at(*feed.sharedProducer)
                                                 : feed;
                            int& shift = perFeedClocks ? feedShift[feedIndex] : globalShift;
                            if (perFeedClocks) clockPtr = &shift;
                            const auto slice = producer.resource->Reserve(shift);
                            const int end = slice.end;
                            for (int id : producer.objects)
                                for (auto ins : molecularProgram[id].instructions) {
                                    if (ins.parameter < slice.begin || ins.parameter >= end)
                                        continue;
                                    ins.parameter += shift - slice.begin;
                                    machine.objects[id].instructions.push_back(ins);
                                }
                            shift += end - slice.begin + 1;
                            activeArm = producer.carrier;
                            for (int i = 0; i < CarrierIdleClearance; ++i)
                                emit('a');
                            emit('E');
                            emit('E');
                            const int pickupTime = shift;
                            emit('G');
                            emit('e');
                            producer.resource->Release(
                                ModulePort{{producer.pickup, producer.height + 1}, 0, false},
                                pickupTime, shift);
                            if (feed.expectedRequests) ++feed.consumedRequests;
                            emit('e');
                            for (int i = 0; i < producer.distance; ++i)
                                emit('A');
                            emit('R');
                            emit('R');
                            emit('R');
                            emit('g');
                            // Every elevator runs on its own lane and its own bottom
                            // row, so the atom's journey is parameterised by its group.
                            const int elevator = producer.elevator;
                            const int elevTop = producer.elevatorTop;
                            const int elevDrop = feed.elevatorDrop;
                            const int transport = transportArms.at(elevator);
                            int& transportPosition = transportPositions.at(elevator);
                            const auto& transportPath = transportPaths.at(elevator);
                            auto moveTransport = [&](core::HexCoord target) {
                                const int destination = static_cast<int>(
                                    std::find(transportPath.begin(), transportPath.end(), target) -
                                    transportPath.begin());
                                if (perFeedClocks)
                                    *clockPtr = std::max(*clockPtr, transportLocks.at(elevator));
                                activeArm = transport;
                                while (transportPosition < destination) {
                                    emit('A');
                                    ++transportPosition;
                                }
                                while (transportPosition > destination) {
                                    emit('a');
                                    --transportPosition;
                                }
                                if (perFeedClocks)
                                    transportLocks.at(elevator) =
                                        std::max(transportLocks.at(elevator), *clockPtr);
                            };
                            if (compactRelay)
                                activeArm = transport;
                            else
                                moveTransport({producer.pickup, elevTop});
                            emit('G');
                            activeArm = producer.carrier;
                            emit('r');
                            emit('r');
                            emit('r');
                            for (int i = CarrierIdleClearance; i < producer.distance; ++i)
                                emit('a');
                            if (compactRelay)
                                activeArm = transport;
                            else
                                moveTransport({feed.cell.q, elevDrop});
                            emit('R');
                            emit('R');
                            emit('R');
                            for (int i = 1; i < (compactRelay ? 1 : elevDrop - feed.cell.r);
                                 ++i)
                                emit('E');
                            emit('g');
                            for (int i = 1; i < (compactRelay ? 1 : elevDrop - feed.cell.r);
                                 ++i)
                                emit('e');
                            emit('r');
                            emit('r');
                            emit('r');
                            // Clear the assembly lane before its feed arm resumes.
                            if (!compactRelay)
                                moveTransport(*std::find_if(
                                    transportPath.begin(), transportPath.end(),
                                    [&](core::HexCoord cell) { return cell.r == elevTop - 1; }));
                            if (perFeedClocks) clockPtr = &globalShift;
                        }
                        // Outlet and spare arms serve one factory, so they belong on
                        // that factory's feed clock; leaving them on the shared clock
                        // desynchronises them from the feed they are supposed to hand
                        // over to (`1dc60983` collided at (81,89), a spare arm).
                        if (perFeedClocks && f < static_cast<int>(factories.size()) &&
                            factories[f].feed < feedShift.size())
                            clockPtr = &feedShift[factories[f].feed];
                        if (std::any_of(spareOutlets.begin(), spareOutlets.end(),
                                        [&](const auto& outlet) { return outlet.factory == f; })) {
                            const int distance = 12 + 3 * (stored[f] + 1);
                            for (const auto& outlet : spareOutlets) {
                                if (outlet.factory != f) continue;
                                activeArm = outlet.arm;
                                emit('G');
                                for (int i = 0; i < distance; ++i)
                                    emit('A');
                                if (outlet.horizontal) {
                                    emit('R');
                                    emit('E');
                                    emit('E');
                                    emit('g');
                                    emit('e');
                                    emit('e');
                                    emit('r');
                                } else {
                                    emit('R');
                                    emit('g');
                                    emit('r');
                                }
                                for (int i = 0; i < distance; ++i)
                                    emit('a');
                            }
                            ++stored[f];
                        }
                        if (spareArms[f] < 0) {
                            if (perFeedClocks) clockPtr = &globalShift;
                            continue;
                        }
                        activeArm = spareArms[f];
                        emit('G');
                        const int distance = (compactSpare ? 2 : 3) * ++stored[f];
                        for (int i = 0; i < distance; ++i)
                            emit('a');
                        emit('R');
                        emit('g');
                        emit('r');
                        for (int i = 0; i < distance; ++i)
                            emit('A');
                        if (perFeedClocks) clockPtr = &globalShift;
                    }
                    while (replayIndex < replay.size() &&
                           replay[replayIndex].instruction.parameter == t) {
                        const auto& entry = replay[replayIndex++];
                        machine.objects[entry.object].instructions.push_back(
                            {entry.instruction.action, globalShift});
                    }
                    ++globalShift;
                }
            }
            for (const auto& feed : molecularFeeds) {
                if (feed.expectedRequests && feed.consumedRequests != feed.expectedRequests)
                    Reject("shared feed did not consume its exact finite demand");
                if (!feed.sharedProducer && !feed.resource->Complete())
                    Reject("molecular feed period leaves an unfinished extraction batch");
            }
            checkDeadline();
            for (auto& o : machine.objects)
                if (!o.instructions.empty()) {
                    int end = globalShift;
                    for (const int value : feedShift) end = std::max(end, value);
                    o.instructions.push_back({'B', end});
                }
        }
        int low = 0, high = 0;
        for (const auto& o : machine.objects) {
            low = std::min(low, o.position.q - 20);
            high = std::max(high, o.position.q + 20);
            if (o.typeName == "track")
                for (int i = 0; i < o.bodyData[4]; ++i) {
                    low = std::min(low, o.position.q + o.bodyData[5 + 2 * i] - 20);
                    high = std::max(high, o.position.q + o.bodyData[5 + 2 * i] + 20);
                }
        }
        for (auto& o : machine.objects) {
            // Replaced virtual feeds are placeholders, not physical glyphs.
            if (o.typeName == "glyph-marker") continue;
            // Singleton feeds never use the three-row escape arm; remove its
            // idle base rather than retaining an unnecessary obstruction.
            if ((o.typeName == "piston" || o.typeName == "track") && o.position.r == 2) continue;
            o.position.q += offset - low;
            if (o.typeName == "out-std") o.bodyData[2] = static_cast<int>(output);
            if (o.typeName == "piston" || o.typeName.starts_with("arm"))
                o.bodyData[4] = armNumber++;
            result.objects.push_back(std::move(o));
        }
        offset += high - low + 32;
    }
    if (shareFeeds && !sharedPair) Reject("shared feed has no compatible Animismus pair");
    checkDeadline();
    if (compactRelay) result.solutionName = "Experimental compact molecular relay";
    if (!needed && !compactRelay && !relayBaseline)
        Reject("life/death target requires no chemistry conversion");
    if (const char* dump = std::getenv("OPUSOPTIMA_LD_DUMP"); dump != nullptr && *dump != '\0')
        io::BinaryWriter::WriteSolutionFile(dump, result);
    return result;
}
} // namespace
io::SolutionData CompileLifeDeath(const io::PuzzleData& puzzle, bool singleProduct) {
    return CompileLifeDeathGeometry(puzzle, singleProduct, false);
}

io::SolutionData CompileLifeDeathClocked(const io::PuzzleData& puzzle) {
    struct Scope {
        Scope() { g_perFeedClocks = true; }
        ~Scope() { g_perFeedClocks = false; }
    } scope;
    return CompileLifeDeathGeometry(puzzle, false, false);
}

io::SolutionData CompileLifeDeathLocalIntake(const io::PuzzleData& puzzle) {
    const RowIntakeModeScope widened(2);
    return CompileLifeDeathGeometry(puzzle, false, false);
}

io::SolutionData CompileSharedLifeDeath(const io::PuzzleData& puzzle,
                                        std::chrono::steady_clock::time_point deadline) {
    return CompileLifeDeathGeometry(puzzle, false, false, false, false, true, deadline);
}
bool SupportsCompactSpareChemistry(const io::PuzzleData& puzzle) {
    if (puzzle.version != 3 || puzzle.productionInfo || puzzle.inputs.empty() ||
        puzzle.outputs.empty() || puzzle.outputs.size() > 4 || puzzle.outputMultiplier < 1 ||
        puzzle.outputMultiplier > 2 ||
        !std::all_of(puzzle.inputs.begin(), puzzle.inputs.end(), [](const auto& input) {
            return input.atoms.size() == 1 && input.bonds.empty();
        }))
        return false;
    return std::any_of(puzzle.outputs.begin(), puzzle.outputs.end(), [&](const auto& output) {
        return std::any_of(output.atoms.begin(), output.atoms.end(), [&](const auto& atom) {
            return (atom.type == AtomType::Vitae || atom.type == AtomType::Mors) &&
                   std::none_of(puzzle.inputs.begin(), puzzle.inputs.end(), [&](const auto& input) {
                       return input.atoms.front().type == atom.type;
                   });
        });
    });
}
io::SolutionData CompileCompactSpareChemistry(const io::PuzzleData& puzzle) {
    if (!SupportsCompactSpareChemistry(puzzle))
        Reject("compact spare requires singleton feeds and an unsupplied animismus product");
    auto result = CompileLifeDeathGeometry(puzzle, false, false, false, true);
    result.solutionName = "Experimental compact reaction outlet storage";
    return result;
}
io::SolutionData CompileCompactChemistryRelay(const io::PuzzleData& puzzle, bool compact) {
    return CompileLifeDeathGeometry(puzzle, false, compact, !compact);
}
FactoryClearanceScope::FactoryClearanceScope(int clearance) : previous_(g_factoryClearance) {
    g_factoryClearance = clearance;
}
FactoryClearanceScope::~FactoryClearanceScope() {
    g_factoryClearance = previous_;
}
} // namespace opusoptima::solvers
