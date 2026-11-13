#pragma once
#include "BinaryParser.hpp"
#include <chrono>

namespace opusoptima::solvers {
/// Finite products from demand-planned renewable singleton feeds.
/// ChemistryPipeline selects actual source/reaction routes; its stock schedule
/// is a material lower bound, not a shared physical coproduct transport tape.
/// Animismus coproducts are buffered; dispersion's unused outlets are disposed
/// (requiring disposal permission). Machines halt after the requested
/// six-times-multiplier products, not infinite throughput.
/// Supports 1–4 connected ordinary or existing colored products (up to 16 atoms /
/// eight atom types each), singleton feeds, and multiplier 1–2. Salt may be calcified from an
/// elemental feed. Singleton salt can supply cardinals through stationary
/// wheels and quintessence through a serialized, reusable unification factory.
/// Singleton quintessence can supply cardinal feeds through dispersion.
/// Factories run on demand before downstream row-assembly pickups. An immutable
/// per-output time index replays consumer tapes in their original equal-cycle order.
/// Projection, purification, calcification-to-wheel chains and cross-lane
/// coproduct reuse are not supported by this geometry backend.
/// Throws std::invalid_argument when the puzzle is outside these bounds.
/// Experimental reaction-outlet evacuation using demand-sized, two-cell slots.
/// Preserves all coproducts and chemistry; requires full legacy verification.
/// Cheap conservative portfolio gate: singleton feeds and an unsupplied
/// animismus product. Molecular extraction is left to the baseline compiler.
[[nodiscard]] bool SupportsCompactSpareChemistry(const io::PuzzleData& puzzle);
[[nodiscard]] io::SolutionData CompileCompactSpareChemistry(const io::PuzzleData& puzzle);
/// Overrides the horizontal clearance the molecular factories leave for each
/// other. The value is multiplied by the factory count and becomes the length of
/// the shared transport corridor, which the period pays one instruction per cell
/// for. Zero is much faster where it fits (`1dc60983` 78166 -> 63966) but some
/// shapes lose their layout, so callers should try the tight value as a separate
/// candidate and keep the shipped one if the gate rejects it. Restored on
/// destruction; pass a negative value to fall back to the shipped default.
class FactoryClearanceScope {
public:
    explicit FactoryClearanceScope(int clearance);
    ~FactoryClearanceScope();
    FactoryClearanceScope(const FactoryClearanceScope&) = delete;
    FactoryClearanceScope& operator=(const FactoryClearanceScope&) = delete;

private:
    int previous_;
};
/// Column the left-most factory block is anchored on. The corridor makes every
/// delivered atom walk from that block to the assembly twice, so a closer anchor
/// shortens every round trip at once and, where it suits the puzzle, wins on all
/// three objectives at the same time (`1dc60983`: cost 6610 -> 6435, cycles
/// 23306 -> 18301, area 1545 -> 1476). It is a per-puzzle optimum, not a better
/// default -- `17a58334` loses to the shipped column and `a7d3c0bf`/`89d6e08e`
/// collide -- so it is only used to build one extra candidate that the caller
/// adopts when it Pareto-dominates. Restored on destruction.
class FactoryStartScope {
public:
    explicit FactoryStartScope(int start);
    ~FactoryStartScope();
    FactoryStartScope(const FactoryStartScope&) = delete;
    FactoryStartScope& operator=(const FactoryStartScope&) = delete;

private:
    int previous_;
};
[[nodiscard]] io::SolutionData CompileLifeDeath(const io::PuzzleData& puzzle);
/// Same finite-product geometry, but the narrow row feeds are compiled with the
/// local source intake widened past the compact layout (`RowIntakeModeScope(2)`):
/// the piston rail above the sources replaces the serpentine delay line, which is
/// 10-25% cheaper and much faster on near-parity row puzzles. The serpentine is
/// then parked off-board, so this machine is a sibling candidate, never a
/// replacement: callers must compile the plain machine too and keep the better.
[[nodiscard]] io::SolutionData CompileLifeDeathLocalIntake(const io::PuzzleData& puzzle);
/// Same geometry, but every molecular feed advances its own clock instead of all
/// of them appending to one global cursor. That turns the period from the *sum*
/// of all work into the *max*: measured 20014 -> 3033 on `cd9b4ea9`. Machines that
/// share a courier between feeds can still collide in the motion phase, so this is
/// a sibling candidate, never a replacement.
[[nodiscard]] io::SolutionData CompileLifeDeathClocked(const io::PuzzleData& puzzle);
/// Default-OFF finite sharing stays within each of one or two ordinary or existing
/// colored outputs and requires genuine multiatom Animismus feed. Singleton inputs
/// remain independent. Colored masks are nonempty subsets of 14 with Fire endpoints
/// and permission 11, lowered through ordinary proxy bonds and original labels at
/// feed spacing three with the unchanged deadline.
/// Bounded derived Gold-through-Tin and requested Quintessence suppliers consume
/// original puzzle inputs with the same deadline. Cardinal suppliers preserve whole
/// molecules and input indices. Unification retains singleton-salt validation and
/// molecular restrictions. Single-product and compact sharing remain unsupported.
/// Invocation-local bounded caches reuse successful identical real-source plans
/// and finite row batches, including cleanup boundaries. Each consumer owns a
/// value copy before placement and retains the same absolute deadline.
[[nodiscard]] io::SolutionData CompileSharedLifeDeath(
    const io::PuzzleData& puzzle, std::chrono::steady_clock::time_point deadline);
/// Assemble one finite product for an already expanded repeating target.
[[nodiscard]] io::SolutionData CompileLifeDeath(const io::PuzzleData& puzzle, bool singleProduct);
/// Experimental single-output, single direct/calcified molecular channel.
/// A short vertical extraction carrier hands a singleton to a fixed radius-two
/// arm at the actual assembly feed. No shared factory or inventory closure is
/// claimed. Unsupported coproduct/multiple-channel plans throw.
/// compact=false exposes unchanged long-bus geometry for controlled comparison.
/// Verification is mandatory. Not registered as a default solver candidate.
[[nodiscard]] io::SolutionData CompileCompactChemistryRelay(const io::PuzzleData& puzzle,
                                                            bool compact = true);
} // namespace opusoptima::solvers
