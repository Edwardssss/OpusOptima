#pragma once

#include <array>
#include <vector>

#include "Core/CoreConstants.hpp"
#include "Core/HexGrid.hpp"
#include "MachineState.hpp"

namespace opusoptima::engine {

// Maximum arm length for pre-computation (0 through 6).
inline constexpr std::size_t kCollisionMaxArmLength = 6;

/// Pre-computed collision lookup table for arm sweeps.
class CollisionLUT {
    public:
    /// Get the singleton instance.
    [[nodiscard]] static const CollisionLUT& Instance();

    [[nodiscard]] std::vector<core::HexCoord> SweepCells(core::HexCoord base,
                                                         core::ArmDirection fromDir,
                                                         int armLength) const;

    private:
    CollisionLUT();

    // Cell sets for each direction/length combination
    // Outer array: 6 directions; inner array: lengths 0..kCollisionMaxArmLength
    std::array<std::array<std::vector<core::HexCoord>, kCollisionMaxArmLength + 1>,
               core::kDirectionCount>
        armCells_{};
};

} // namespace opusoptima::engine
