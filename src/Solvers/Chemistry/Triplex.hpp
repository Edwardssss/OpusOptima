#pragma once

#include "BinaryParser.hpp"

namespace opusoptima::solvers {

/// Serial row assembly of finite targets (up to sixteen atoms) using the row
/// compiler's feed disassembly and chemistry. Supports full triplex fire/fire
/// edges and ordinary edges with at least one non-fire endpoint, including
/// branches and cycles. Ordinary fire/fire and partial triplex edges are rejected.
/// Requires the ordinary row compiler's permissions plus triplex.
/// Throws invalid_argument for unsupported chemistry, geometry or permissions.
/// Returned candidates require independent verification and metric writeback.
[[nodiscard]] io::SolutionData CompileTriplex(const io::PuzzleData& puzzle);

} // namespace opusoptima::solvers
