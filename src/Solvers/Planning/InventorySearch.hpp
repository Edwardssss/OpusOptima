#pragma once

#include "AssemblySolver.hpp"
#include "ChemicalPlanner.hpp"
#include "MaterialPolicy.hpp"

#include <chrono>
#include <functional>
#include <optional>

namespace opusoptima::solvers {

// Inventory-only fallback; arm motion is compiled separately. Optional purification
// retains intermediate metals as inventory until they can supply the requested target.
struct InventorySearchLimits {
    // Expanded states (including shallower revisits), emitted operations and
    // distinct atoms in one batch of every input are independently bounded.
    std::size_t maxStates{20000};
    std::size_t maxSteps{512};
    std::size_t maxSourceAtoms{512};
    // Legacy backends may only execute direct reagent-to-target pairs.
    bool singleStagePurification{};
    // Automatic inventory execution can store and reuse intermediate metals.
    bool multiStagePurification{};
    // Physical backends can rule out feeds without changing port identities.
    std::vector<std::size_t> excludedInputs{};
    // Material-only exploration can select any source atom. Physical extraction
    // order is then an obligation of the chosen mechanical implementation.
    bool mechanicalExtractionOrder{true};
    bool wheelCatalysts{};
    std::chrono::steady_clock::time_point deadline{std::chrono::steady_clock::time_point::max()};
};
// Closed periods conserve cardinal-equivalent material when all source and target
// species are Salt, cardinals, Vitae, Mors or Quintessence. The returned divisor
// is a necessary period condition. Disposal, production and other species disable
// the check; finite retained inventory is outside this closed-period contract.
[[nodiscard]] std::optional<std::size_t> ClosedMaterialPeriodDivisor(const io::PuzzleData& puzzle);

enum class InventorySearchStatus { Found, Rejected, Exhausted };
struct InventorySearchResult {
    InventorySearchStatus status{InventorySearchStatus::Rejected};
    SupplyPlan plan;
    std::size_t states{};
};

// Source ordering prefers compatible inherited edges. Mandatory target edges
// (ordinary without bonder permission, or colored when prism creation is
// forbidden or either endpoint is not Fire) additionally require one coherent
// same-batch rigid source mapping. This is still a bounded material search;
// physical compilation and independent acceptance validate the full machine.
// Joint inventory schedules retain an explicit output identity for every step;
// they cannot be passed to a single-output IR compiler as a SupplyPlan.
struct JointSupplyPlan {
    struct Step {
        SupplyPlan::Step supply;
        // Store/Discard may use outputs.size() when draining period-end stock;
        // inventory-only steps do not address an output atom.
        std::size_t output{};
        friend bool operator==(const Step&, const Step&) = default;
    };
    std::vector<Step> steps;
    std::vector<SupplyPlan::Usage> usage;
    std::size_t productsPerPeriod{};
    bool recipeConstrained{};
    friend bool operator==(const JointSupplyPlan&, const JointSupplyPlan&) = default;
};
struct JointInventorySearchResult {
    InventorySearchStatus status{InventorySearchStatus::Rejected};
    JointSupplyPlan plan;
    std::size_t states{};
};

// One period makes `products` copies of every finite output. Cyclic starting-port
// and fragment-first/feed-exposure demand orders share one state budget; each
// rejected direct-fragment history is deduplicated before acceptance. Duplication prioritizes
// smaller outputs stably to release coproduct catalysts. Original port identities are preserved.
// Feed batches and buffers span output boundaries and close at period end.
// This is material planning only, not multi-output mechanical compilation.
[[nodiscard]] JointInventorySearchResult SearchJointSupplyInventory(
    const io::PuzzleData& puzzle, std::size_t products, InventorySearchLimits limits = {},
    const ChemicalRecipe* recipe = nullptr,
    const std::function<bool(const JointSupplyPlan&)>& accept = {});

[[nodiscard]] InventorySearchResult SearchSupplyInventory(
    const io::PuzzleData& puzzle, std::size_t products, InventorySearchLimits limits = {},
    const ChemicalRecipe* recipe = nullptr,
    const std::function<bool(const SupplyPlan&)>& accept = {});

// The guarded policy propagates through every joint demand ordering without
// changing the original limits or the environment-controlled entry points.
[[nodiscard]] JointInventorySearchResult SearchJointSupplyInventoryWithPolicy(
    const io::PuzzleData& puzzle, std::size_t products, SupplyMaterialLoweringPolicy policy,
    InventorySearchLimits limits = {}, const ChemicalRecipe* recipe = nullptr,
    const std::function<bool(const JointSupplyPlan&)>& accept = {});

} // namespace opusoptima::solvers
