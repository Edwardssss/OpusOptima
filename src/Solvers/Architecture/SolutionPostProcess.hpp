#pragma once

#include "BinaryParser.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace opusoptima::solvers::architecture {

// ── M-E: post-processing ────────────────────────────────────────────────────
//
// Upstream's Step 7 removes every part that was never used (`*.OptimizeParts()`)
// and then runs a `CostOptimizer` pass. The row portfolio here never did either,
// which is one reason the reference machines show zero `glyph-disposal` objects
// against our 21 and a much smaller part count overall.
//
// The passes below are deliberately conservative: they only remove an object when
// its absence provably cannot change the machine, and the caller still re-runs the
// native gate on the pruned machine before publishing it.

struct PruneReport {
    struct Removed {
        std::string typeName;
        core::HexCoord position;
        std::string reason;
    };
    std::vector<Removed> removed;
    [[nodiscard]] std::size_t Count() const {
        return removed.size();
    }
};

class SolutionPostProcess {
    public:
    /// Removes parts that cannot affect the machine:
    ///  * an arm whose tape never grabs, rotates or releases (only waits),
    ///  * an object that is byte-identical to an earlier one,
    ///  * a track whose path cells are referenced by no other object.
    [[nodiscard]] static io::SolutionData PruneUnusedParts(const io::SolutionData& machine,
                                                           PruneReport* report = nullptr);

    /// `PruneUnusedParts` plus a deterministic object order, so two runs that build
    /// the same machine produce the same bytes (the candidate archive dedups on
    /// bytes).
    [[nodiscard]] static io::SolutionData CostOptimize(const io::SolutionData& machine,
                                                       PruneReport* report = nullptr);

    /// True when the arm's tape can never move an atom.
    [[nodiscard]] static bool ArmIsIdle(const io::SolutionObject& object);
};

} // namespace opusoptima::solvers::architecture
