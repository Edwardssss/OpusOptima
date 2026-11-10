#pragma once

#include "IO/BinaryParser.hpp"

namespace opusoptima::solvers {
// Finite six-product schedule for a bonded pair, one purification stage, and an unchanged
// coproduct. Requires outputMultiplier == 1; excess coproduct is stored without tracks or disposal.
[[nodiscard]] io::SolutionData CompileMolecularMetalSplit(const io::PuzzleData& puzzle);
} // namespace opusoptima::solvers
