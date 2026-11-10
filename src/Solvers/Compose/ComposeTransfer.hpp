#pragma once

#include "AssemblySolver.hpp"

namespace opusoptima::solvers {

/// Component-graph composer for direct-to-output transfer puzzles.
///
/// The machine is derived from the target molecule and the reagent sources only:
/// every product atom gets its own transfer component (`arm1` grab -> rotate ->
/// reset) whose base cell and source cell follow from the target cell and the
/// arm reach, and the output port is placed on the product frame. There is no
/// family-specific hand-written geometry, so the same code covers every puzzle
/// whose product atoms can be lifted straight from a single-atom source.
///
/// Domain (v1): one ordinary output, no production, exactly one atom and no
/// bonds, and a single-atom input carrying that element. Anything else throws
/// `std::invalid_argument` so the portfolio can fall back to another family.
bool SupportsComposedTransfer(const io::PuzzleData& puzzle);

/// Emits the composed transfer machine. Throws `std::invalid_argument` outside
/// the domain described above.
io::SolutionData ComposeTransferMachine(const io::PuzzleData& puzzle);

} // namespace opusoptima::solvers
