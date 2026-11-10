#pragma once

#include "FixedArmInventory.hpp"

namespace opusoptima::solvers {

// Bounded fragment-join fallback using the same material/physical inventory
// lowering as the fixed-arm transport mode. No track or piston is constructed.
[[nodiscard]] std::optional<io::SolutionData> CompileAcceptedFixedArmJoin(
    const io::PuzzleData& puzzle, const std::function<bool(const io::SolutionData&)>& accept);

} // namespace opusoptima::solvers
