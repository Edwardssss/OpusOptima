#include "ElementPipeline.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <set>
#include <stdexcept>
#include <string>

namespace opusoptima::solvers::architecture {
namespace {

using core::AtomType;

[[nodiscard]] AtomType MetalFromId(int id) {
    return static_cast<AtomType>(id);
}

struct Rule {
    StageKind kind{StageKind::Calcify};
    std::vector<AtomType> inputs;
    std::vector<AtomType> outputs;
    bool catalyst{};
};

// The reaction vocabulary of the pinned gate. Reachability mirrors
// `ChemicalPlanner::CanSupply`; the arity and byproducts mirror the
// `ExecutionIR::Kind` documentation so a chain here can be lowered mechanically.
[[nodiscard]] std::vector<Rule> RulesFor(std::uint64_t bits) {
    std::vector<Rule> rules;
    // Calcification: a cardinal reagent becomes Salt (permission bit 12).
    if ((bits & (1ULL << 12)) != 0)
        for (const auto element : {AtomType::Air, AtomType::Earth, AtomType::Fire, AtomType::Water})
            rules.push_back({StageKind::Calcify, {element}, {AtomType::Salt}});
    // Projection from Salt yields the four elementals (permission bit 13).
    if ((bits & (1ULL << 13)) != 0)
        for (const auto element : {AtomType::Air, AtomType::Earth, AtomType::Fire, AtomType::Water})
            rules.push_back({StageKind::Project, {AtomType::Salt}, {element}});
    // Projection upgrades a metal one step toward Gold, consuming quicksilver (bit 14).
    if ((bits & (1ULL << 14)) != 0)
        for (int id = 8; id <= 12; ++id)
            rules.push_back({StageKind::Project,
                             {MetalFromId(id), AtomType::Quicksilver},
                             {MetalFromId(id - 1)}});
    // Purification: two equal non-gold metals fuse into the next metal up.
    for (int id = 8; id <= 12; ++id)
        rules.push_back({StageKind::Purify,
                         {MetalFromId(id), MetalFromId(id)},
                         {MetalFromId(id - 1)}});
    // Animismus: two Salt yield Vitae and Mors together.
    rules.push_back({StageKind::Animismus,
                     {AtomType::Salt, AtomType::Salt},
                     {AtomType::Vitae, AtomType::Mors}});
    // Unification: the four elementals yield Quintessence.
    rules.push_back({StageKind::Unify,
                     {AtomType::Air, AtomType::Earth, AtomType::Fire, AtomType::Water},
                     {AtomType::Quintessence}});
    // Dispersion: Quintessence splits back into the four elementals.
    for (const auto element : {AtomType::Air, AtomType::Earth, AtomType::Fire, AtomType::Water})
        rules.push_back({StageKind::Disperse, {AtomType::Quintessence}, {element}});
    // Duplication: a retained Salt catalyst doubles a cardinal.
    for (const auto element : {AtomType::Air, AtomType::Earth, AtomType::Fire, AtomType::Water})
        rules.push_back({StageKind::Duplicate, {AtomType::Salt, element}, {element}, true});
    return rules;
}

// Cheapest supply for one element. `via` records the rule that produces it and
// the depth of the deepest operand, which is what the physical rank uses.
struct Supply {
    int depth{std::numeric_limits<int>::max()};
    int rule{-1};
    std::size_t feed{};
    bool feedIsSingleAtom{};
};

} // namespace

std::string_view StageKindName(StageKind kind) {
    switch (kind) {
    case StageKind::Feed: return "feed";
    case StageKind::Calcify: return "calcify";
    case StageKind::Project: return "project";
    case StageKind::Purify: return "purify";
    case StageKind::Animismus: return "animismus";
    case StageKind::Unify: return "unify";
    case StageKind::Disperse: return "disperse";
    case StageKind::Duplicate: return "duplicate";
    }
    return "unknown";
}

std::string_view CommandKindName(CommandKind kind) {
    switch (kind) {
    case CommandKind::Consume: return "consume";
    case CommandKind::Generate: return "generate";
    case CommandKind::PassThrough: return "pass-through";
    }
    return "unknown";
}

ElementPipeline ElementPipeline::Build(const io::PuzzleData& puzzle) {
    if (puzzle.inputs.empty() || puzzle.outputs.empty())
        throw std::invalid_argument("element pipeline requires inputs and outputs");
    if (puzzle.outputs.size() > 8)
        throw std::invalid_argument("element pipeline supports at most eight outputs");

    const auto rules = RulesFor(puzzle.permissionBits);

    // Reachability: feeds start at depth 0, each rule adds one hop. Relax to a
    // fixpoint because the element graph has cycles (unify/disperse).
    std::map<AtomType, Supply> supply;
    for (std::size_t input = 0; input < puzzle.inputs.size(); ++input) {
        const bool single = puzzle.inputs[input].atoms.size() == 1 &&
                            puzzle.inputs[input].bonds.empty();
        for (const auto& atom : puzzle.inputs[input].atoms) {
            auto& entry = supply[atom.type];
            // Prefer a single-atom port over a molecular feed for the same element:
            // a port costs no disassembler, so it is the cheaper physical source.
            if (entry.depth != 0 || (single && !entry.feedIsSingleAtom)) {
                entry.depth = 0;
                entry.feed = input;
                entry.feedIsSingleAtom = single;
            }
        }
    }
    const std::size_t ruleBudget = (rules.size() * 4) + 4;
    for (std::size_t pass = 0; pass < ruleBudget; ++pass) {
        bool changed = false;
        for (std::size_t index = 0; index < rules.size(); ++index) {
            const auto& rule = rules[index];
            int deepest = 0;
            bool reachable = true;
            for (std::size_t operand = 0; operand < rule.inputs.size(); ++operand) {
                if (rule.catalyst && operand == 0) continue; // borrowed, not supplied
                const auto found = supply.find(rule.inputs[operand]);
                if (found == supply.end() || found->second.depth == std::numeric_limits<int>::max()) {
                    reachable = false;
                    break;
                }
                deepest = std::max(deepest, found->second.depth);
            }
            if (!reachable) continue;
            for (const auto produced : rule.outputs) {
                auto& entry = supply[produced];
                if (deepest + 1 < entry.depth) {
                    entry.depth = deepest + 1;
                    entry.rule = static_cast<int>(index);
                    changed = true;
                }
            }
        }
        if (!changed) break;
    }

    ElementPipeline pipeline;
    // Demand set, in output order.
    std::set<AtomType> demanded;
    for (const auto& output : puzzle.outputs)
        for (const auto& atom : output.atoms) demanded.insert(atom.type);
    for (const auto demand : demanded) {
        const auto found = supply.find(demand);
        if (found == supply.end() || found->second.depth == std::numeric_limits<int>::max())
            throw std::invalid_argument(std::string("no element supply chain for ") +
                                        std::string(core::AtomTypeName(demand)));
    }

    // Materialise the unique stages each demand needs, recursively.
    std::vector<StageSpec> stages;
    auto stageIndex = [&](const StageSpec& spec) {
        const auto found = std::find(stages.begin(), stages.end(), spec);
        if (found != stages.end()) return static_cast<std::size_t>(found - stages.begin());
        stages.push_back(spec);
        return stages.size() - 1;
    };
    // One ordered path of stage indices per element, source first.
    std::map<AtomType, std::vector<std::size_t>> chains;
    std::map<AtomType, bool> visiting;
    auto buildChain = [&](auto&& self, AtomType element) -> std::vector<std::size_t> {
        if (const auto found = chains.find(element); found != chains.end()) return found->second;
        if (visiting[element]) throw std::invalid_argument("element pipeline cycle");
        visiting[element] = true;
        const auto& entry = supply[element];
        std::vector<std::size_t> chain;
        if (entry.depth == 0) {
            StageSpec feed;
            feed.kind = StageKind::Feed;
            feed.inputs = {element};
            feed.outputs = {element};
            feed.feed = entry.feed;
            chain.push_back(stageIndex(feed));
        } else {
            const auto& rule = rules[static_cast<std::size_t>(entry.rule)];
            // Operand chains first: the physical pipeline is source-to-demand.
            for (std::size_t operand = 0; operand < rule.inputs.size(); ++operand) {
                if (rule.catalyst && operand == 0) continue;
                for (const auto stage : self(self, rule.inputs[operand]))
                    if (std::find(chain.begin(), chain.end(), stage) == chain.end())
                        chain.push_back(stage);
                (void)operand;
            }
            StageSpec spec;
            spec.kind = rule.kind;
            spec.inputs = rule.inputs;
            spec.outputs = rule.outputs;
            spec.catalyst = rule.catalyst;
            const auto index = stageIndex(spec);
            if (std::find(chain.begin(), chain.end(), index) == chain.end()) chain.push_back(index);
        }
        visiting[element] = false;
        chains[element] = chain;
        return chain;
    };
    for (const auto demand : demanded) (void)buildChain(buildChain, demand);

    // Physical rank: feeds first, then one layer per conversion step.
    std::vector<int> ranks(stages.size(), 0);
    for (std::size_t pass = 0; pass < ruleBudget; ++pass) {
        bool changed = false;
        for (std::size_t index = 0; index < stages.size(); ++index) {
            if (stages[index].kind == StageKind::Feed) continue;
            int rank = 1;
            for (std::size_t operand = 0; operand < stages[index].inputs.size(); ++operand) {
                if (stages[index].catalyst && operand == 0) continue;
                for (std::size_t other = 0; other < stages.size(); ++other)
                    if (std::find(stages[other].outputs.begin(), stages[other].outputs.end(),
                                  stages[index].inputs[operand]) != stages[other].outputs.end())
                        rank = std::max(rank, ranks[other] + 1);
            }
            if (rank != ranks[index]) {
                ranks[index] = rank;
                changed = true;
            }
        }
        if (!changed) break;
    }
    // Deterministic physical order: by rank, then by discovery.
    std::vector<std::size_t> order(stages.size());
    for (std::size_t i = 0; i < order.size(); ++i) order[i] = i;
    std::stable_sort(order.begin(), order.end(),
                     [&](std::size_t a, std::size_t b) { return ranks[a] < ranks[b]; });
    std::vector<std::size_t> remap(stages.size());
    for (std::size_t i = 0; i < order.size(); ++i) remap[order[i]] = i;
    std::vector<StageSpec> ordered;
    std::vector<int> orderedRanks;
    ordered.reserve(stages.size());
    orderedRanks.reserve(stages.size());
    for (const auto index : order) {
        ordered.push_back(stages[index]);
        orderedRanks.push_back(ranks[index]);
    }
    for (auto& [element, chain] : chains) {
        std::vector<std::size_t> remapped;
        remapped.reserve(chain.size());
        for (const auto stage : chain) remapped.push_back(remap[stage]);
        chain = std::move(remapped);
    }

    // Interior-first atom order: place the atoms with the most already-present
    // neighbours first so a bond closure never has to move through a placed atom.
    std::vector<DemandedAtom> atoms;
    for (std::size_t product = 0; product < puzzle.outputs.size(); ++product)
        for (std::size_t atom = 0; atom < puzzle.outputs[product].atoms.size(); ++atom)
            atoms.push_back({product, atom, puzzle.outputs[product].atoms[atom].type});
    std::vector<std::pair<std::size_t, std::size_t>> ranked;
    ranked.reserve(atoms.size());
    for (std::size_t index = 0; index < atoms.size(); ++index) {
        const auto& output = puzzle.outputs[atoms[index].product];
        std::size_t neighbours = 0;
        for (const auto& bond : output.bonds) {
            const auto& cell = output.atoms[atoms[index].atom].position;
            if (bond.from == cell || bond.to == cell) ++neighbours;
        }
        ranked.push_back({neighbours, index});
    }
    std::stable_sort(ranked.begin(), ranked.end(),
                     [](const auto& left, const auto& right) { return left.first > right.first; });
    std::vector<DemandedAtom> atomOrder;
    atomOrder.reserve(ranked.size());
    for (const auto& entry : ranked) atomOrder.push_back(atoms[entry.second]);

    pipeline.stages_ = std::move(ordered);
    pipeline.ranks_ = std::move(orderedRanks);
    pipeline.chainsByElement_ = std::move(chains);
    pipeline.atoms_ = std::move(atomOrder);
    int depth = 0;
    for (const auto& [element, chain] : pipeline.chainsByElement_) {
        (void)element;
        int conversions = 0;
        for (const auto stage : chain)
            if (pipeline.stages_[stage].kind != StageKind::Feed) ++conversions;
        depth = std::max(depth, conversions);
    }
    pipeline.depth_ = depth;
    return pipeline;
}

DryRun ElementPipeline::Run() const {
    DryRun run;
    run.stages = stages_;
    run.ranks = ranks_;
    run.moduleUse.assign(stages_.size(), 0);
    run.atomReadyRank.reserve(atoms_.size());
    run.complete = true;

    std::map<AtomType, std::size_t> produced;
    std::map<AtomType, std::size_t> consumed;
    std::map<AtomType, std::size_t> demanded;
    for (const auto& entry : atoms_) ++demanded[entry.type];

    std::vector<std::size_t> use(stages_.size(), 0);
    // Recursive supply: operands first, then the stage that fuses them. This is
    // the logical order the ProgramWriter walks upstream, and it makes the
    // command sequence a valid topological schedule on its own.
    auto supply = [&](auto&& self, AtomType element, std::size_t product,
                      std::size_t atom) -> void {
        const auto found = chainsByElement_.find(element);
        if (found == chainsByElement_.end())
            throw std::invalid_argument("dry run lost a supply chain");
        const auto index = found->second.back();
        const auto& stage = stages_[index];
        if (stage.kind == StageKind::Feed) {
            Command command{CommandKind::Consume, element, product, atom, index, use[index]++,
                            ranks_[index]};
            run.commands.push_back(command);
            ++run.consumes;
            return;
        }
        for (std::size_t operand = 0; operand < stage.inputs.size(); ++operand) {
            if (stage.catalyst && operand == 0) continue;
            self(self, stage.inputs[operand], product, atom);
        }
        Command command{CommandKind::Generate, element, product, atom, index, use[index]++,
                        ranks_[index]};
        run.commands.push_back(command);
        ++run.generates;
        // Everything ranked above the producer is physically downstream of it, so
        // the element crosses those modules as pure transport.
        for (std::size_t later = 0; later < stages_.size(); ++later) {
            if (ranks_[later] <= ranks_[index]) continue;
            Command pass{CommandKind::PassThrough, element, product, atom, later, 0, ranks_[later]};
            run.commands.push_back(pass);
            ++run.passThroughs;
        }
    };

    run.atomReadyRank.reserve(atoms_.size());
    for (const auto& entry : atoms_) {
        supply(supply, entry.type, entry.product, entry.atom);
        const auto found = chainsByElement_.find(entry.type);
        run.atomReadyRank.push_back(ranks_[found->second.back()]);
    }

    // Invocation bookkeeping and the waste chain.
    run.moduleUse = use;
    for (std::size_t index = 0; index < stages_.size(); ++index) {
        const auto& stage = stages_[index];
        for (const auto& element : stage.outputs) produced[element] += use[index];
        if (stage.kind == StageKind::Feed) {
            for (const auto& element : stage.outputs) produced[element] += use[index];
            continue;
        }
        for (std::size_t operand = 0; operand < stage.inputs.size(); ++operand) {
            if (stage.catalyst && operand == 0) continue;
            consumed[stage.inputs[operand]] += use[index];
        }
    }
    // Feed stages produce nothing themselves: their output is the drawn atom.
    for (std::size_t index = 0; index < stages_.size(); ++index)
        if (stages_[index].kind == StageKind::Feed) produced[stages_[index].outputs.front()] -= use[index];
    for (const auto& [element, count] : demanded) consumed[element] += count;
    for (const auto& [element, count] : produced) {
        const auto used = consumed.contains(element) ? consumed[element] : 0;
        if (count > used) run.waste[element] = count - used;
    }
    int depth = 0;
    for (const auto& chain : chainsByElement_) {
        int conversions = 0;
        for (const auto stage : chain.second)
            if (stages_[stage].kind != StageKind::Feed) ++conversions;
        depth = std::max(depth, conversions);
    }
    run.depth = depth;
    return run;
}

} // namespace opusoptima::solvers::architecture
