#pragma once

#include "AutomaticSupply.hpp"

namespace opusoptima::solvers {

// A bounded fixed-arm alternative for unchanged connected molecular feeds.
// It lowers the same complete-batch material IR with one rotating arm, no track
// or piston. Only externally accepted machines may be published.
[[nodiscard]] std::optional<io::SolutionData> CompileAcceptedFixedArmInventory(
    const io::PuzzleData& puzzle, const std::function<bool(const io::SolutionData&)>& accept,
    const std::function<void(const std::string&)>& feedback = {});

} // namespace opusoptima::solvers
