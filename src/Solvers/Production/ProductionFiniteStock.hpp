#pragma once

#include "BinaryParser.hpp"
#include <functional>
#include <optional>

namespace opusoptima::solvers {
[[nodiscard]] std::optional<io::SolutionData> CompileAcceptedProductionFiniteStock(
    const io::PuzzleData& puzzle, const std::function<bool(const io::SolutionData&)>& accept);
}
