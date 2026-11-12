#pragma once

#include "AssemblySolver.hpp"
#include "ExecutionIR.hpp"
#include <array>
#include <chrono>
#include <functional>
#include <optional>

namespace opusoptima::solvers {
struct ProductAtomRef {
    std::size_t output{}, atom{};
    friend bool operator==(const ProductAtomRef&, const ProductAtomRef&) = default;
};
struct SharedAssemblyTarget {
    io::PuzzleData puzzle;
    std::vector<core::HexCoord> origins;
    std::vector<ProductAtomRef> atoms;
};
SharedAssemblyTarget BuildSharedAssemblyTarget(const io::PuzzleData&);
io::SolutionData CompileSharedOrdinaryOutputs(const io::PuzzleData&);
struct SharedFactoryEmission {
    ExecutionIR::AtomId atom{};
    ProductAtomRef consumer{};
    std::size_t product{};
    // Pose at readyCycle, after conversion and conveyor contraction. Orientation
    // is the physical arm's CCW hex heading, not ExecutionIR's clockwise frame.
    core::HexCoord position{};
    core::HexCoord consumerPosition{0, 0};
    int orientation{}, beginCycle{}, holderReleaseCycle{}, readyCycle{}, releaseCycle{},
        reservationEnd{};
    // Molecular provenance: acquired identity before optional conversion.
    ExecutionIR::AtomId sourceIdentity{};
    std::size_t sourceInput{}, sourceAtom{}, sourceBatch{}, sourceRemainder{};
    // Reaction emissions have two real precursors, not a synthetic singleton source.
    std::array<ExecutionIR::AtomId, 2> reactionInputs{};
    core::AtomType type{};
    std::size_t quantity{1};
};
struct SharedCyclePhase {
    std::size_t product{};
    std::string phase;
    int begin{}, end{};
};
struct FragmentCustodyEvent {
    std::size_t product{}, sourceAtom{}, sourceBatch{};
    core::AtomType type{};
    int buffer{-1}, begin{}, end{};
    core::HexCoord position{};
    bool stored{}, regenerated{};
};
struct SharedFactoryBatch {
    std::vector<FragmentCustodyEvent> custody;
    io::SolutionData solution;
    ExecutionIR ledger;
    std::vector<SharedCyclePhase> phases;
    std::vector<SharedFactoryEmission> emissions;
    std::size_t emptyReturnTasks{};
    std::int64_t emptyReturnCycles{};
};
// Experimental direct-call domain only: one real singleton feed, two same-type
// products, Direct/Calcification. Serialized conveyor custody, six of each output.
SharedFactoryBatch CompileSharedFactoryBatch(const io::PuzzleData&);
// Experimental molecular subpath: one ordinary cardinal diatom, two singleton
// consumers exhausting opposite endpoints; Direct/Calcification, six closed pairs.
bool SupportsMolecularSupplySource(const io::Molecule&);
// Finite extraction into two ordinary retained/optionally calcified fragments.
// The entire real connected 2/3-atom source must be consumed once per pair.
SharedFactoryBatch CompileMolecularFragmentSupplyBatch(const io::PuzzleData&);
// Direct-call, default-unregistered finite tree atomization and row reassembly.
// One real <=16-atom tree, <=32 demand atoms, one/two outputs; each acquired
// batch is exhausted before renewal. One physical singleton slot; no disposal.
SharedFactoryBatch CompileRenewableFragmentTransport(
    const io::PuzzleData&,
    std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::time_point::max(),
    std::function<std::chrono::steady_clock::time_point()> clock = {});
SharedFactoryBatch CompileMolecularSupplyBatch(const io::PuzzleData&);
// Experimental closed real diatom -> two Salt -> one Animismus -> six of each
// singleton Vitae/Mors output. No default candidate registration or Quintessence.
SharedFactoryBatch CompileSharedAnimismusBatch(const io::PuzzleData&);
// Finite complete multiatom consumers; balanced Vitae/Mors, one pending coproduct.
SharedFactoryBatch CompileSharedAnimismusAssembly(const io::PuzzleData&);
// Finite closed ordinary/Animismus inventory: real sources and released products
// are parked in exclusive physical slots before row consumers retrieve them.
// Optional batchConsumer bypasses the final precursor store/retrieve, directly
// delivering the last real source atom to the first reaction; unsupported last
// source allocations fail closed. No consumer count or buffer geometry changes.
// Optional nearCache derives exclusive physical slot reuse from the complete
// store/reaction/consumer dependency order and places that bank before sources.
SharedFactoryBatch CompileMixedInventoryAssembly(const io::PuzzleData&,
                                                 bool scheduleEmptyReturns = false,
                                                 bool compactBuffers = false,
                                                 bool batchConsumer = false,
                                                 bool nearCache = false);
struct MolecularRecipeSupply {
    SharedFactoryBatch batch;
    // Local output index -> atom demand in the untouched original recipe.
    std::vector<ProductAtomRef> consumers;
    // One source batch reserved for original recipe atoms; deliberately incomplete.
    ExecutionIR recipeReservations;
    std::size_t input{};
};
MolecularRecipeSupply CompileMolecularRecipeSupply(const io::PuzzleData&, std::size_t input,
                                                   const std::vector<ProductAtomRef>& consumers);
// Single-stage purification consumes two source atoms in the shared feed inventory.
io::SolutionData CompileSharedPurificationAssembly(const io::PuzzleData&);
io::SolutionData CompileRowAssembly(const io::PuzzleData&, const AssemblyPlan&, int feedSpacing);
io::SolutionData CompileRowAssembly(const io::PuzzleData&, const AssemblyPlan&);
io::SolutionData CompileRowAssembly(const io::PuzzleData&, const AssemblyPlan&,
                                    std::chrono::steady_clock::time_point deadline);
bool SupportsComponentRowAssembly(const io::PuzzleData&);
io::SolutionData CompileComponentRowAssembly(const io::PuzzleData&, const AssemblyPlan&);
io::SolutionData CompileCloseFeedRowAssembly(const io::PuzzleData&, const AssemblyPlan&);
io::SolutionData CompileStructuredRowAssembly(const io::PuzzleData&, const AssemblyPlan&);
io::SolutionData CompileRetimedRowAssembly(const io::PuzzleData&, const AssemblyPlan&);
// Retain ordinary lowering when empty resets cannot overlap; never extend the deadline.
io::SolutionData CompileScheduledRowAssembly(const io::PuzzleData&, const AssemblyPlan&,
                                             std::chrono::steady_clock::time_point deadline);
// Same scheduled row with the local source intake forced on. Submitted as its own
// candidate so a rejection never replaces the shared-feeder machine.
io::SolutionData CompileScheduledRowIntakeAssembly(const io::PuzzleData&, const AssemblyPlan&,
                                                   std::chrono::steady_clock::time_point deadline);
// Same scheduled row with the shared serpentine feeder forced. Used as the lazy
// fallback when the intake machine is rejected by the native gate.
io::SolutionData CompileScheduledRowFeederAssembly(const io::PuzzleData&, const AssemblyPlan&,
                                                   std::chrono::steady_clock::time_point deadline,
                                                   int gapOverride = -1);
// Clocked companion with the shared feeder forced; the fallback for a rejected
// intake clocked machine.
io::SolutionData CompileScheduledRowFeederClockAssembly(
    const io::PuzzleData&, const AssemblyPlan&, std::chrono::steady_clock::time_point deadline,
    int gapOverride = -1);
// True when the most recent scheduled-row compile actually used the intake.
bool ScheduledRowIntakeApplied();

// Temporarily override the local-intake decision for the compiling thread:
//   0  = follow `OPUSOPTIMA_DIRECT_INPUT` (the shipped default)
//  >0  = force the intake on
//  <0  = force the shared serpentine feeder
//  == 2 = widened intake: the intake may also run outside the compact layout and
//        on up to eight sources. Measured 2026-11-11 as 10-25% lower cost on the
//        near-parity row puzzles (b2c42099 755 -> 635, cd9b4ea9 7610 -> 1620),
//        but it re-parks the serpentine, so it is only ever compiled as a sibling
//        candidate next to the plain machine. Restored on destruction.
class RowIntakeModeScope {
public:
    explicit RowIntakeModeScope(int mode);
    ~RowIntakeModeScope();
    RowIntakeModeScope(const RowIntakeModeScope&) = delete;
    RowIntakeModeScope& operator=(const RowIntakeModeScope&) = delete;

private:
    int previous_;
};

// Temporarily override the row clocks for the compiling thread:
//   0  = follow `OPUSOPTIMA_ROW_*` (pipelined by default since 2026-11-11)
//  >0  = force the pipelined clocks
//  <0  = force the serialized emitter
// Families that post-process an emitted row need the serialized timing: the wheel
// family relocates every object by a fixed offset and appends its own tape surgery,
// which assumes the original serialized schedule. Restored on destruction.
class RowClockScope {
public:
    explicit RowClockScope(int mode);
    ~RowClockScope();
    RowClockScope(const RowClockScope&) = delete;
    RowClockScope& operator=(const RowClockScope&) = delete;

private:
    int previous_;
};
// Same idea, but it only flips the finite-family opt-in
// (`OPUSOPTIMA_ROW_FINITE_CLOCKS`) and leaves every other row knob on the package
// default. RowClockScope forces them all at once, which changes the emitted
// machine on families that rely on the stock defaults; a caller that only wants
// the bounded-run clocks must use this narrower scope.
class FiniteClockScope {
public:
    explicit FiniteClockScope(int mode);
    ~FiniteClockScope();
    FiniteClockScope(const FiniteClockScope&) = delete;
    FiniteClockScope& operator=(const FiniteClockScope&) = delete;

private:
    int previous_;
};

// Result of auditing an emitted machine against the upstream scheduling
// contract (C3): every arm's tape must be strictly ordered in time, so the
// per-arm fragment merge can never displace an instruction.
struct ScheduleAudit {
    std::size_t arms{};
    std::size_t instructions{};
    std::int64_t period{};
    bool monotone{true};    // ticks non-decreasing inside every arm
    bool uniqueTicks{true}; // no arm emits two actions on the same tick
    bool displaced{};       // the fragment merge had to move an instruction
};

ScheduleAudit AuditMachineSchedule(const io::SolutionData& machine);
// Same scheduled row with independent feeder/upper/conveyor clocks enabled
// explicitly. Used as a separate cycles-slot portfolio candidate so the
// compact cost/area machine is not replaced.
io::SolutionData CompileScheduledRowClockAssembly(const io::PuzzleData&, const AssemblyPlan&,
                                                  std::chrono::steady_clock::time_point deadline);
// One construction that yields both the compact cost/area machine and the
// clocked cycles machine. The portfolio submits both, so the parallel clock
// does not consume a second construction or replace any existing family.
struct ScheduledRowCandidates {
    io::SolutionData compact; // gap2 fallback
    std::optional<io::SolutionData> compactGap0;
    io::SolutionData clocked; // gap2 clocked fallback
    std::optional<io::SolutionData> clockedGap0;
    // Shared-feeder companions of the intake machines. They replace the gap0
    // variant whenever the intake is in play, so the portfolio can compare the
    // two tape sets slot by slot without spending extra compile time.
    std::optional<io::SolutionData> compactFeeder;
    std::optional<io::SolutionData> clockedFeeder;
    bool hasClocked{false};
    // True when the compact machine replaced the shared serpentine feeder with
    // the local source intake, so the caller can compile a feeder fallback only
    // when the two machines can actually differ.
    bool compactUsesIntake{false};
    bool clockedUsesIntake{false};
};
ScheduledRowCandidates CompileScheduledRowCandidates(
    const io::PuzzleData&, const AssemblyPlan&, std::chrono::steady_clock::time_point deadline);
// Emit one product only; callers must halt the resulting finite machine.
io::SolutionData CompileRowAssemblySingleProduct(const io::PuzzleData&, const AssemblyPlan&);
io::SolutionData CompileRowAssemblySingleProduct(const io::PuzzleData&, const AssemblyPlan&,
                                                 std::chrono::steady_clock::time_point deadline);
// Product boundaries are exclusive instruction times in emission order. The
// last slice must run through the full tape duration to include feed cleanup.
io::SolutionData CompileRowAssemblyBatch(const io::PuzzleData&, const AssemblyPlan&,
                                         std::vector<int>&);
io::SolutionData CompileRowAssemblyBatch(const io::PuzzleData&, const AssemblyPlan&,
                                         std::vector<int>&,
                                         std::chrono::steady_clock::time_point deadline);
// Emit a finite number of intermediates, parking surplus when disposal is unavailable.
io::SolutionData CompileRowAssemblyFiniteBatch(const io::PuzzleData&, const AssemblyPlan&,
                                               std::vector<int>&, std::size_t products);
io::SolutionData CompileRowAssemblyFiniteBatch(const io::PuzzleData&, const AssemblyPlan&,
                                               std::vector<int>&, std::size_t products,
                                               std::chrono::steady_clock::time_point deadline);
io::SolutionData CompileRowAssembly(const io::PuzzleData&, const AssemblyPlan&,
                                    const io::PuzzleData*);
// Connectivity uses ordinary proxy bonds; labels select upgrades and final atom types.
// Salt endpoints of triplex edges are calcified selectively after assembly.
io::SolutionData CompileTriplexRowAssembly(const io::PuzzleData&, const AssemblyPlan&,
                                           const io::Molecule&, bool postCalcification = true,
                                           int feedSpacing = 3);
// The explicit deadline is unchanged through validation, supply planning and lowering.
io::SolutionData CompileTriplexRowAssembly(const io::PuzzleData&, const AssemblyPlan&,
                                           const io::Molecule&, bool postCalcification,
                                           int feedSpacing,
                                           std::chrono::steady_clock::time_point deadline);
} // namespace opusoptima::solvers
