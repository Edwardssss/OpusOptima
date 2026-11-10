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

/// Every composed variant, cheapest first: the shared-ring layout (one arm feeding
/// several product cells) and the per-atom layout (one arm per atom). The portfolio
/// verifies each and keeps the one that wins the most objective slots.
std::vector<io::SolutionData> ComposeAssemblyVariants(const io::PuzzleData& puzzle);

/// Emits the cheapest composed assembly machine. Throws `std::invalid_argument`
/// outside the supported domain.
io::SolutionData ComposeAssemblyMachine(const io::PuzzleData& puzzle);

} // namespace opusoptima::solvers
