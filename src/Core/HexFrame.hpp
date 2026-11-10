#pragma once

#include "HexGrid.hpp"

namespace opusoptima::core {

// Rigid port-local to world transform; rotations use the grid's clockwise convention.
struct HexFrame {
    HexCoord origin{};
    int rotation{};

    [[nodiscard]] constexpr int Turns() const noexcept {
        return ((rotation % 6) + 6) % 6;
    }

    [[nodiscard]] constexpr HexFrame Compose(const HexFrame& localFrame) const noexcept {
        return {ToWorld(localFrame.origin), (Turns() + localFrame.Turns()) % 6};
    }

    [[nodiscard]] constexpr HexFrame Inverse() const noexcept {
        return {ToLocal({}), (6 - Turns()) % 6};
    }

    [[nodiscard]] constexpr HexCoord ToWorld(HexCoord local) const noexcept {
        const int turns = Turns();
        for (int i = 0; i < turns; ++i)
            local = RotateClockwise(local);
        return Add(local, origin);
    }

    [[nodiscard]] constexpr HexCoord ToLocal(HexCoord world) const noexcept {
        auto local = Subtract(world, origin);
        const int turns = Turns();
        for (int i = 0; i < turns; ++i)
            local = RotateCounterClockwise(local);
        return local;
    }
};

} // namespace opusoptima::core
