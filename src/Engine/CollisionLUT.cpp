#include "CollisionLUT.hpp"

#include <algorithm>
#include <cstddef>

namespace opusoptima::engine {

namespace {

/// Direction offset vectors for each of the 6 hex directions.
constexpr std::array<core::HexCoord, core::kDirectionCount> kDirVec = {{
    {.q = 1, .r = 0},
    {.q = 1, .r = -1},
    {.q = 0, .r = -1},
    {.q = -1, .r = 0},
    {.q = -1, .r = 1},
    {.q = 0, .r = 1},
}};

/// Compute cells occupied by an arm of given length in a given direction.
[[nodiscard]] std::vector<core::HexCoord> ArmCellsInDir(const core::HexCoord base,
                                                        core::ArmDirection dir, int length) {
    const std::size_t direction_count = static_cast<std::size_t>(dir) % core::kDirectionCount;
    std::vector<core::HexCoord> cells;
    cells.reserve(static_cast<std::size_t>(length));
    for (int len = 1; len <= length && len <= static_cast<int>(kCollisionMaxArmLength); ++len) {
        cells.push_back({.q = base.q + (kDirVec[direction_count].q * len),
                         .r = base.r + (kDirVec[direction_count].r * len)});
    }
    return cells;
}

} // namespace

const CollisionLUT& CollisionLUT::Instance() {
    static const CollisionLUT instance;
    return instance;
}

CollisionLUT::CollisionLUT() {
    constexpr core::HexCoord kOrigin{.q = 0, .r = 0};
    for (std::size_t dir = 0; dir < armCells_.size(); ++dir) {
        for (std::size_t len = 0; len <= kCollisionMaxArmLength; ++len) {
            armCells_[dir][len] =
                ArmCellsInDir(kOrigin, static_cast<core::ArmDirection>(static_cast<int>(dir)),
                              static_cast<int>(len));
        }
    }
}

std::vector<core::HexCoord> CollisionLUT::SweepCells(const core::HexCoord base,
                                                     const core::ArmDirection fromDir,
                                                     int armLength) const {
    const std::size_t direction_index = static_cast<std::size_t>(fromDir) % core::kDirectionCount;
    const std::size_t len = std::min(static_cast<std::size_t>(armLength), kCollisionMaxArmLength);

    const auto& relCells = armCells_[direction_index][len];
    std::vector<core::HexCoord> result;
    result.reserve(relCells.size());
    for (const auto& rel_cell : relCells) {
        result.push_back({.q = base.q + rel_cell.q, .r = base.r + rel_cell.r});
    }
    return result;
}

} // namespace opusoptima::engine
