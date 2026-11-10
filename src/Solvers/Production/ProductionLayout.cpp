#include "ProductionLayout.hpp"

#include <algorithm>
#include <stdexcept>

namespace opusoptima::solvers {
bool ContainsProductionCell(const ProductionChamberLayout& chamber, core::HexCoord cell) {
    return std::find(chamber.interior.begin(), chamber.interior.end(), cell) !=
           chamber.interior.end();
}

std::vector<ProductionChamberLayout> PlanProductionChambers(const io::PuzzleData& puzzle) {
    if (!puzzle.productionInfo || puzzle.productionInfo->chambers.empty())
        throw std::invalid_argument("production requires chamber geometry");
    if (puzzle.productionInfo->vials.size() > 64 ||
        std::any_of(puzzle.productionInfo->vials.begin(), puzzle.productionInfo->vials.end(),
                    [](const auto& vial) { return vial.count < 0 || vial.count > 64; }))
        throw std::invalid_argument(
            "production visual records exceed independently supported bounds");
    std::vector<ProductionChamberLayout> layouts;
    for (const auto& chamber : puzzle.productionInfo->chambers) {
        int radius = 0, stretch = 0;
        if (chamber.chamberType == "Small")
            radius = 1;
        else if (chamber.chamberType == "SmallWide") {
            radius = 1;
            stretch = 1;
        } else if (chamber.chamberType == "SmallWider") {
            radius = 1;
            stretch = 2;
        } else if (chamber.chamberType == "Medium")
            radius = 2;
        else if (chamber.chamberType == "MediumWide") {
            radius = 2;
            stretch = 1;
        } else if (chamber.chamberType == "Large")
            radius = 3;
        else
            throw std::invalid_argument("unsupported production chamber type");
        if (chamber.position.q < -120 || chamber.position.q > 120 || chamber.position.r < -120 ||
            chamber.position.r > 120)
            throw std::invalid_argument("production chamber coordinate out of range");
        ProductionChamberLayout layout;
        layout.chamberIndex = layouts.size();
        layout.center = chamber.position;
        for (int q = -radius - 1; q <= radius + stretch + 1; ++q)
            for (int r = -radius - 1; r <= radius + 1; ++r) {
                int distance = radius + 2;
                for (int shift = 0; shift <= stretch; ++shift)
                    distance = std::min(distance, core::Distance({shift, 0}, {q, r}));
                const auto cell = core::Add(chamber.position, {q, r});
                if (distance <= radius)
                    layout.interior.push_back(cell);
                else if (distance == radius + 1)
                    layout.walls.push_back(cell);
            }
        for (const auto& other : layouts) {
            for (auto cell : layout.interior)
                if (ContainsProductionCell(other, cell) ||
                    std::find(other.walls.begin(), other.walls.end(), cell) != other.walls.end())
                    throw std::invalid_argument("production chambers overlap");
            for (auto cell : layout.walls)
                if (ContainsProductionCell(other, cell))
                    throw std::invalid_argument("production wall intersects chamber interior");
        }
        layouts.push_back(std::move(layout));
    }
    return layouts;
}
} // namespace opusoptima::solvers
