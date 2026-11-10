#pragma once

#include "BinaryParser.hpp"
#include <functional>
#include <optional>

namespace opusoptima::solvers {
[[nodiscard]] std::optional<io::SolutionData> CompileAcceptedProductionDirectFeed(
    const io::PuzzleData&, const std::function<bool(const io::SolutionData&)>&);
}
