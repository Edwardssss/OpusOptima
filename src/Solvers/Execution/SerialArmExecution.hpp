#pragma once
#include <chrono>
#include "AutomaticSupply.hpp"
#include "ExecutionPlan.hpp"
#include "MaterialPolicy.hpp"

namespace opusoptima::solvers {
// Typed eligibility for the opt-in closed singleton receiver stage.
[[nodiscard]] bool SupportsWholeFeedDocking(const io::PuzzleData&) noexcept;
// Explicit closed-period compiler for one ordinary whole feed and singleton receivers.
// The supplied replay binds every original identity, cuts every source edge before
// scalar output, and closes with RequireComplete. Docking, cut-site parking, scalar
// custody transfers and all resets are certified outside the renewable footprint.
// Independent singleton requests may execute exposed identities first. The optional
// construction callback charges the caller's shared quota before each construction.
// Admission requires the caller's native acceptance callback.
[[nodiscard]] std::optional<io::SolutionData> CompileAcceptedWholeFeedDocking(
    const io::PuzzleData&, const ExecutionIR&,
    const std::function<bool(const io::SolutionData&)>& accept,
    const std::function<void(const std::string&)>& feedback,
    std::chrono::steady_clock::time_point deadline,
    const std::function<bool()>& beginConstruction = {});
// Check the projected post-Output physical ledger before committing a finite
// completion Output. Targets are actual bound cache cells, not logical IR poses.
void RequireFinitePhysicalTerminal(const ExecutionIR::State&,
    const ExecutionIR::FiniteRetention&,
    const std::map<ExecutionIR::AtomId, core::HexCoord>& targets);
enum class SerialArmFailure { Unsupported, BudgetExhausted, LayoutCollision, MechanicalFailure };
// OPUSOPTIMA_FRAGMENT_PRODUCT_DOCKING=1 selects a finite-only, bounded whole-feed
// rigid docking experiment. Requires identical connected ordinary feed/output
// local graphs and no retained stock. Default OFF; chemistry is not assembled.
// OPUSOPTIMA_PRODUCT_PORT_ACCESS=1 enables finite-plan static approach
// impossibility propagation and an ordinary singleton future-output experiment.
// The caller selects CutOrdinarySources material lowering for eligible opt-in
// temporal plans; actual Unbond operations and reconstructed output bonds replay.
// The latter requires singleton feeds or fully cut ordinary dimers, nonoverlapping
// multiatom ordinary outputs, and no retained source edges, wheel, prism,
// endpoint carrier or finite terminal. Every dimer cut requires permission 9,
// actual oriented source unbonder endpoints and matching acquired identities.
// Material Unbond must precede any use of either source identity. The entire
// regenerating source footprint stays reserved and output-disjoint.
// Bases remain outside every reserved cell. Empty future output cells must be
// disjoint from source/device footprints. Every scalar transition checks actual
// bonder endpoints against live stationary identities; only final destinations
// may form expected edges at both bound poses. Existing sweep/custody/period
// checks and native whole-goal acceptance remain mandatory. The broad flag defaults
// OFF. ClosedCardinalLife selects this same contract through the explicit policy
// entry point, only after validating the narrow closed cardinal-to-life domain.
// Serial fixed-arm handoffs implement material tasks in one global ledger.
// Rigid inherited fragments retain endpoint identities through reaction-site
// binding; unfinished fragments are delayed before always-on output assembly.
// Independent genuine Fire dimers bind exact colored masks to output prisms.
// This station requires permission 11, singleton feeds (at most four input types),
// outputs of at most two atoms, and only Acquire/Relocate/Bond/Output tasks under
// the existing 512-step/atom and layout/deadline bounds. Every live Fire pair on
// a station glyph must match the bound identities, output pose and exact mask.
// Scalar transport retains custody and swept-interval certification. Ordinary
// edges and mixed-type coproducts use their existing output bonders.
// Closed singleton wheel conversion also binds this station for one output of at
// most four atoms with exactly two converted Fire endpoints and one colored edge.
// At most four small ordinary source types (four atoms each) are admitted. Only
// Acquire/Unbond/Calcify/Duplicate(wheel)/Relocate/Bond/Output/Discard tasks are
// admitted; reactions and relocations require singleton substrates, source cuts
// require actual unbonders, and assembly bonds must belong to the final output.
// One static wheel supplies bounded cardinal contact stations. Its atoms remain
// device-owned, and scalar intervals and arm resets exclude its entire footprint.
// No material, stored stock or disposal debt survives the closed period. Source
// retention is disabled only for this contract. Bonded endpoint and finite wheel
// paths retain their prior rules. Material wheel search remains disabled by
// default; this is a direct physical capability within existing shared budgets.
// The acceptance callback remains the required native authority gate.
// After parent acceptance, optionally compile a persistent ordinary-source-fragment
// child, then an aligned scalar-relay child, within the same deadline. Source edges
// must survive continuously under identical IDs/masks; cuts/reactions cannot resurrect
// retention. The callback must gate each child before archive insertion.
// OPUSOPTIMA_SERIAL_FRAGMENTS=1 / OPUSOPTIMA_SERIAL_HANDOFF=1 disable these
// respective trials. The return remains the accepted parent.
[[nodiscard]] std::optional<io::SolutionData> CompileAcceptedSparseSourceSerialArmExecution(
    const io::PuzzleData&, const ExecutionPlan&,
    const std::function<bool(const io::SolutionData&)>& accept,
    const std::function<void(const std::string&)>& feedback,
    std::chrono::steady_clock::time_point deadline);

std::optional<io::SolutionData> CompileAcceptedSerialArmExecution(
    const io::PuzzleData&, const ExecutionPlan&,
    const std::function<bool(const io::SolutionData&)>&,
    const std::function<void(const std::string&)>& feedback = {},
    std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::time_point::max(),
    const std::function<void(SerialArmFailure, const std::string&)>& classifiedFeedback = {},
    const std::function<void(const io::SolutionData&, const io::SolutionData&)>& acceptedPipeline = {});
// ClosedCardinalLife validates its source domain before any physical trials.
std::optional<io::SolutionData> CompileAcceptedSerialArmExecutionWithPolicy(
    const io::PuzzleData&, const ExecutionPlan&, SupplyMaterialLoweringPolicy,
    const std::function<bool(const io::SolutionData&)>&,
    const std::function<void(const std::string&)>& feedback = {},
    std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::time_point::max(),
    const std::function<void(SerialArmFailure, const std::string&)>& classifiedFeedback = {},
    const std::function<void(const io::SolutionData&, const io::SolutionData&)>& acceptedPipeline = {});
} // namespace opusoptima::solvers
