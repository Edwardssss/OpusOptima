#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "PuzzleSpecs.hpp"
#include "BaseSolver.hpp"

namespace opusoptima::solvers {

/// Result of the MILP recipe computation.
struct RecipeResult {
    bool success{false};
    std::vector<std::size_t> inputAtomUsage;
    std::vector<std::size_t> outputAtomYield;
    std::int32_t estimatedCost{0};
    std::string description;
};

/// HiGHS-based MILP solver that computes the optimal atom-to-atom mapping
/// between input molecules and output molecules.
class RecipeMilpSolver : public BaseSolver {
    public:
    RecipeMilpSolver();

    [[nodiscard]] io::SolutionData Solve(const io::PuzzleData& puzzle) override;
    [[nodiscard]] std::int32_t EstimateCycles(const io::PuzzleData& puzzle) override;

    /// Direct atom-type matching (no puzzle required).
    [[nodiscard]] static RecipeResult SolveTypes(const std::vector<core::AtomType>& inputTypes,
                                                 const std::vector<core::AtomType>& outputTypes);
};

} // namespace opusoptima::solvers
