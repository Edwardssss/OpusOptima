#pragma once

#include "BaseSolver.hpp"

namespace opusoptima::solvers {

/// Transfers a rigid adjacent pair using a length-three arm and a four-cycle tape.
/// Accepts translated/rotated/reordered pairs with one unchanged normal bond.
/// Requires one input/output, puzzle-format coordinates, positive multiplier,
/// arm/grab/rotation permissions, and no production section or Repeat atoms.
/// Canonical geometry measures 20 cost, 19 area, and 24 * multiplier - 1 cycles.
class DiatomicSolver final : public BaseSolver {
    public:
    DiatomicSolver() : BaseSolver("DiatomicSolver") {}
    [[nodiscard]] io::SolutionData Solve(const io::PuzzleData& puzzle) override;
    [[nodiscard]] std::int32_t EstimateCycles(const io::PuzzleData& puzzle) override;
    [[nodiscard]] static bool Supports(const io::PuzzleData& puzzle) noexcept;
};

} // namespace opusoptima::solvers
