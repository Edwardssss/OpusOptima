#pragma once

#include "BinaryParser.hpp"

#include <cstddef>
#include <map>
#include <string_view>
#include <vector>

namespace opusoptima::solvers::architecture {

// ── M-A: logical element pipeline + dry run ──────────────────────────────────
//
// Upstream's Step 4 is the layer this port was missing: before any object is
// placed, the solver asks an OutputGenerator for each product element and lets
// that request travel *up* a chain of element generators. What comes back is a
// geometry-free command sequence — `Consume`, `Generate`, `PassThrough` — and its
// byproducts (how often each generator fires, how many elements cross each
// module, which elements are waste) are exactly the numbers a tick scheduler and
// a module library need.
//
// Nothing here knows about cells, arms, tapes or ticks. `PipelineCompiler` turns
// the resulting `DryRun` into geometry; this file is pure data and is unit
// testable on its own.

/// One pre-programmed element transformer: a rewrite rule with fixed arity, the
/// logical form of one upstream generator module.
enum class StageKind { Feed, Calcify, Project, Purify, Animismus, Unify, Disperse, Duplicate };

[[nodiscard]] std::string_view StageKindName(StageKind kind);

struct StageSpec {
    StageKind kind{StageKind::Feed};
    /// Ordered operands consumed by one invocation (catalysts included so the
    /// geometry can reach them, and flagged by `catalyst`).
    std::vector<core::AtomType> inputs;
    /// Elements produced by one invocation. Animismus produces two.
    std::vector<core::AtomType> outputs;
    /// Feed only: index of the puzzle input this stage draws from.
    std::size_t feed{};
    /// Duplicate only: `inputs.front()` is a retained catalyst, not consumed.
    bool catalyst{};
    friend bool operator==(const StageSpec&, const StageSpec&) = default;
};

enum class CommandKind { Consume, Generate, PassThrough };

[[nodiscard]] std::string_view CommandKindName(CommandKind kind);

struct Command {
    CommandKind kind{CommandKind::Consume};
    core::AtomType element{core::AtomType::Salt};
    std::size_t product{};
    std::size_t atom{};
    /// Index into `DryRun::stages`.
    std::size_t stage{};
    /// Ordinal use of that stage inside one production period.
    std::size_t invocation{};
    /// Physical pipeline rank of `stage`; a higher rank is closer to the assembler.
    int rank{};
    friend bool operator==(const Command&, const Command&) = default;
};

/// One demanded product atom, in machine build order.
struct DemandedAtom {
    std::size_t product{};
    std::size_t atom{};
    core::AtomType type{core::AtomType::Salt};
    friend bool operator==(const DemandedAtom&, const DemandedAtom&) = default;
};

struct DryRun {
    /// Instantiated modules, ordered by rank (feed side first).
    std::vector<StageSpec> stages;
    /// Rank of each stage in `stages`.
    std::vector<int> ranks;
    /// Invocations of each stage per production period.
    std::vector<std::size_t> moduleUse;
    /// Every logical command, in request order.
    std::vector<Command> commands;
    /// Per product atom: the rank at which its element first exists.
    std::vector<int> atomReadyRank;
    /// Elements produced but never demanded, i.e. the waste chain.
    std::map<core::AtomType, std::size_t> waste;
    std::size_t consumes{};
    std::size_t generates{};
    std::size_t passThroughs{};
    /// Longest conversion chain in the machine, 0 for a pure feed machine.
    int depth{};
    bool complete{};
};

/// The element pipeline of one puzzle: which generators exist, in what order, and
/// what each one consumes.
class ElementPipeline {
    public:
    /// Builds a supply chain for every demanded product element from the puzzle's
    /// own inputs and the permissions of its mechanisms. Throws
    /// `std::invalid_argument` when an element cannot be reached at all.
    [[nodiscard]] static ElementPipeline Build(const io::PuzzleData& puzzle);

    /// Requests every product element and returns the logical command sequence.
    [[nodiscard]] DryRun Run() const;

    [[nodiscard]] const std::vector<StageSpec>& Stages() const {
        return stages_;
    }
    /// Physical rank per stage (feeds are 0).
    [[nodiscard]] const std::vector<int>& Ranks() const {
        return ranks_;
    }
    /// Ordered stage indices from source to demand, per element.
    [[nodiscard]] const std::map<core::AtomType, std::vector<std::size_t>>& Chains() const {
        return chainsByElement_;
    }
    /// True when every product atom is a single-atom feed and no generator fires.
    [[nodiscard]] bool IsFeedOnly() const {
        return depth_ == 0;
    }
    [[nodiscard]] int Depth() const {
        return depth_;
    }
    /// Product atom order used by both the dry run and the emitter: interior-first,
    /// so a bond closure never has to move through an already placed atom.
    [[nodiscard]] const std::vector<DemandedAtom>& Atoms() const {
        return atoms_;
    }

    private:
    std::vector<StageSpec> stages_;
    std::vector<int> ranks_;
    std::map<core::AtomType, std::vector<std::size_t>> chainsByElement_;
    std::vector<DemandedAtom> atoms_;
    int depth_{};
};

} // namespace opusoptima::solvers::architecture
