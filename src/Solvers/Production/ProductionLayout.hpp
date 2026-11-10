#pragma once

#include "ProductionTransport.hpp"

namespace opusoptima::solvers {
[[nodiscard]] std::vector<ProductionChamberLayout> PlanProductionChambers(
    const io::PuzzleData& puzzle);
[[nodiscard]] bool ContainsProductionCell(const ProductionChamberLayout& chamber,
                                          core::HexCoord cell);
} // namespace opusoptima::solvers
