#pragma once

#include "ExecutionIR.hpp"
#include "HeldArmMotion.hpp"

namespace opusoptima::solvers {

// Fixed-base, length-one through length-three rotation/pivot interval checks.
// Input footprints are
// treated as continuously available after extraction, except at the departing
// material's occupied starting cells. This is conservative and still requires
// native machine verification for glyph timing and non-lattice swept geometry.
[[nodiscard]] bool CertifyFixedArmInterval(const ExecutionIR::State& state, core::HexCoord base,
                                           const std::vector<core::HexCoord>& inputFootprints,
                                           const HeldArmState& from, const HeldArmState& to,
                                           char instruction, bool holding);

} // namespace opusoptima::solvers
