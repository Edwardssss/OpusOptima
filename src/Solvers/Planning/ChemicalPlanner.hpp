#pragma once
#include <chrono>

#include "BaseSolver.hpp"

namespace opusoptima::solvers {
enum class ElementConversion { None, Calcification, Projection, Duplication };

struct ChemicalRecipe {
    std::vector<std::size_t> inputBatches;
    std::vector<std::size_t> surplus;
    std::size_t calcifications{};
    std::size_t projections{};
    std::size_t duplications{};
    std::size_t purifications{};
    std::size_t animismus{};
    std::size_t unifications{};
    std::size_t dispersions{};
    // Cardinal element IDs 2..5, in order; catalyst atoms are not consumed.
    std::vector<std::size_t> duplicatedElements;
};

/// General integer stoichiometric feasibility, preserved for existing supply
/// backends. ChemistryPipeline additionally resolves physically supported
/// singleton routes with ordered coproduct stock for composite factories.
class ChemicalPlanner {
    public:
    [[nodiscard]] static bool CanSupply(core::AtomType source, core::AtomType requested,
                                        std::uint64_t permissions);
    /// Integer stoichiometric feasibility for one output molecule, minimizing raw atoms.
    /// This is a material lower bound; mechanical supply may consume additional batches.
    [[nodiscard]] static ChemicalRecipe Plan(const io::PuzzleData& puzzle);
    /// Solve the whole execution period; exact recipes forbid leftover material.
    [[nodiscard]] static ChemicalRecipe Plan(const io::PuzzleData& puzzle, std::size_t products,
                                             bool allowSurplus,
                                             std::chrono::steady_clock::time_point deadline =
                                                 std::chrono::steady_clock::time_point::max());
    [[nodiscard]] static ChemicalRecipe PlanWithWheelCatalysts(const io::PuzzleData& puzzle,
                                                               std::size_t products,
                                                               bool allowSurplus);

    private:
    [[nodiscard]] static ChemicalRecipe PlanRecipe(const io::PuzzleData&, std::size_t, bool, bool,
        std::chrono::steady_clock::time_point = std::chrono::steady_clock::time_point::max());
};
} // namespace opusoptima::solvers
