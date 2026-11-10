#pragma once

#include "BinaryParser.hpp"
#include <array>
#include <vector>

namespace opusoptima::solvers {
enum class ChemistryReaction { Calcify, Duplicate, Animismus, Unify, Disperse };
enum class ChemistryFeedKind { Direct, Calcify, Wheel, Animismus, Unify, Disperse };
struct ChemistryFeed {
    ChemistryFeedKind kind{};
    int input{-1};
    bool calcifySource{};
};
struct ChemistryDemand {
    core::AtomType type;
    std::size_t count;
};
struct ChemistryOperation {
    // Input >= 0 acquires one renewable singleton; otherwise executes reaction.
    int input{-1};
    ChemistryReaction reaction{};
    core::AtomType element{core::AtomType::Salt};
};
struct ChemistryPlan {
    std::vector<ChemistryFeed> feeds;
    std::vector<ChemistryOperation> operations;
    std::vector<std::size_t> inputBatches;
    std::array<std::size_t, 16> stock{};
};
/// Demand-ordered singleton chemistry. Coproducts remain in stock and can serve
/// subsequent demands. This is a material schedule, not a geometric certificate.
/// Wheels supply renewable cardinal catalysts (not consumed material). Projection
/// and purification deliberately require other, multi-inlet mechanical backends.
class ChemistryPipeline {
    public:
    [[nodiscard]] static ChemistryFeed Resolve(const io::PuzzleData& puzzle, core::AtomType type);
    [[nodiscard]] static ChemistryPlan Plan(const io::PuzzleData& puzzle,
                                            const std::vector<ChemistryDemand>& demands);
};
} // namespace opusoptima::solvers
