#pragma once

#include "BaseSolver.hpp"

namespace opusoptima::solvers {

/// A complete four-cycle transfer machine for one unchanged, unbonded atom.
/// Unsupported puzzle shapes are rejected rather than returning a partial machine.
class SingleAtomSolver final : public BaseSolver {
    public:
    SingleAtomSolver() : BaseSolver("SingleAtomSolver") {}
    [[nodiscard]] io::SolutionData Solve(const io::PuzzleData& puzzle) override;
    [[nodiscard]] std::int32_t EstimateCycles(const io::PuzzleData& puzzle) override;
    [[nodiscard]] static bool Supports(const io::PuzzleData& puzzle) noexcept;
};

} // namespace opusoptima::solvers
