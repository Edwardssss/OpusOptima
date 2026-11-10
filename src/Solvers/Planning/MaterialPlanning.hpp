#pragma once

#include "ExecutionPlan.hpp"
#include "InventorySearch.hpp"
#include "SupplyExecutionIR.hpp"

namespace opusoptima::solvers {

enum class MaterialPlanningStatus { Found, Unsupported, NoCandidate, Exhausted, BudgetExhausted };
struct MaterialPlanningLimits {
    std::size_t maxStates{4096}, maxCandidates{32};
    std::size_t maxSteps{512}, maxSourceAtoms{512};
    std::chrono::steady_clock::time_point deadline{std::chrono::steady_clock::time_point::max()};
};
struct MaterialPlanningResult {
    MaterialPlanningStatus status{MaterialPlanningStatus::NoCandidate};
    std::optional<ExecutionPlan> plan;
    std::size_t states{}, candidates{};
    std::vector<std::size_t> periods;
    std::string detail;
};

// Automatic wheel planning requires bounded nonproduction Salt/Air/Earth/Water
// sources, a calcifiable source, and one connected <=4-atom output with exactly
// two Fire endpoints of one colored edge, plus all serial/glyph permissions.
// This is a search guard; physical singleton provenance and native acceptance
// remain mandatory. Period division and the shared search limits are unchanged.
[[nodiscard]] bool SupportsClosedSingletonWheelPlanning(const io::PuzzleData& puzzle);

// No rail, arm or extraction geometry gate for other chemistry. A Found result
// proves only a replayable closed period, never a machine.
[[nodiscard]] MaterialPlanningResult SearchMaterialExecution(
    const io::PuzzleData& puzzle, MaterialPlanningLimits limits = {},
    const std::function<bool(const ExecutionPlan&)>& accept = {});

// Explicit policy adapter preserves the original limits layout and entry point.
// Emitted cuts and reconstructed bonds share maxSteps and the existing deadline.
// ClosedCardinalLife validates its domain and enables only the closed cardinal
// precursor composition within this same search pass.
[[nodiscard]] MaterialPlanningResult SearchMaterialExecutionWithPolicy(
    const io::PuzzleData& puzzle, SupplyMaterialLoweringPolicy policy,
    MaterialPlanningLimits limits = {},
    const std::function<bool(const ExecutionPlan&)>& accept = {});

} // namespace opusoptima::solvers
