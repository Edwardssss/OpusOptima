#pragma once

#include "IO/BinaryParser.hpp"
#include <functional>
#include <optional>

namespace opusoptima::solvers {
[[nodiscard]] std::optional<io::SolutionData> CompileAcceptedProductionAssembly(
    const io::PuzzleData&, const std::function<bool(const io::SolutionData&)>&);
}
