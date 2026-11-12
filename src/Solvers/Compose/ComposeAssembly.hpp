#pragma once

#include "AssemblySolver.hpp"

namespace opusoptima::solvers {

/// Component-graph composer for ordinary assembly puzzles.
///
/// The machine is derived from the target molecule and the reagent sources only:
/// every product atom gets its own transfer component (`arm1` grab -> rotate ->
/// reset) whose base cell and source cell follow from the target cell and the
/// arm reach, every bond gets a `bonder` station placed on the bond edge, and the
/// output port is placed on the product frame. There is no family-specific
/// hand-written geometry, so the same code covers every puzzle whose product
/// atoms can be lifted straight from a single-atom reagent source (no chemistry,
/// no disassembly).
///
/// Anything outside that domain throws `std::invalid_argument` so the portfolio
/// can fall back to another family.
bool SupportsComposedAssembly(const io::PuzzleData& puzzle);

/// `true` when at least one product atom has no same-type single-atom reagent
/// source, i.e. the machine must go through a chemistry glyph station.
bool ComposedAssemblyRequiresChemistry(const io::PuzzleData& puzzle);

/// `true` when `ComposeAssemblyVariants` leads with the armless in-place layout:
/// either chemistry is required, or the product is a bonded frame larger than the
/// bounded arm search (dense lattices).
bool ComposedAssemblyUsesInPlaceLayout(const io::PuzzleData& puzzle);

/// `true` when the puzzle is one molecular input whose atoms are exactly the
/// product's atoms: the machine needs no reagent stations at all.
bool SupportsComposedMolecularAssembly(const io::PuzzleData& puzzle);

/// `true` when every product atom has a reagent path (single-atom port, freed
/// atom from a molecular input, or calcification) and the product is a
/// single-colour bond frame. The cheap mirror of the ordered network's own
/// eligibility test, used by the portfolio to decide whether to try it.
bool SupportsOrderedNetwork(const io::PuzzleData& puzzle);

/// Every composed variant, cheapest first: the inside-out arm network (one swing arm
/// per atom, staggered inside a single period), the shared-ring layout (one arm
/// feeding several product cells), the per-atom layout (one arm per atom) and, for a
/// single molecular input, the extraction layout (one unbonder per input bond plus one
/// swing arm per atom). The portfolio verifies each and keeps the one that wins the
/// most objective slots.
std::vector<io::SolutionData> ComposeAssemblyVariants(const io::PuzzleData& puzzle);

/// Emits the cheapest composed assembly machine. Throws `std::invalid_argument`
/// outside the supported domain.
io::SolutionData ComposeAssemblyMachine(const io::PuzzleData& puzzle);

} // namespace opusoptima::solvers
