#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <ostream>

#include "CoreConstants.hpp"

namespace opusoptima::core {

struct HexCoord {
    int q{};
    int r{};

    [[nodiscard]] constexpr int s() const noexcept {
        return -q - r;
    }

    friend constexpr bool operator==(const HexCoord&, const HexCoord&) noexcept = default;
};

struct HexCoordHash {
    std::size_t operator()(const HexCoord& coord) const noexcept {
        const std::uint64_t uint64_q = static_cast<std::uint32_t>(coord.q);
        const std::uint64_t uint64_r = static_cast<std::uint32_t>(coord.r);
        return static_cast<std::size_t>((uint64_q << kIntBitWidth) ^ uint64_r);
    }
};

[[nodiscard]] constexpr HexCoord Add(const HexCoord a_hexcoord,
                                     const HexCoord b_hexcoord) noexcept {
    return HexCoord{.q = a_hexcoord.q + b_hexcoord.q, .r = a_hexcoord.r + b_hexcoord.r};
}

[[nodiscard]] constexpr HexCoord Subtract(const HexCoord a_hexcoord,
                                          const HexCoord b_hexcoord) noexcept {
    return HexCoord{.q = a_hexcoord.q - b_hexcoord.q, .r = a_hexcoord.r - b_hexcoord.r};
}

[[nodiscard]] constexpr int AbsInt(const int value) noexcept {
    return value < 0 ? -value : value;
}

[[nodiscard]] constexpr int Distance(const HexCoord a_hexcoord,
                                     const HexCoord b_hexcoord) noexcept {
    const HexCoord delta = Subtract(a_hexcoord, b_hexcoord);
    return (AbsInt(delta.q) + AbsInt(delta.r) + AbsInt(delta.s())) / kCubeDistanceDivisor;
}

[[nodiscard]] constexpr HexCoord RotateClockwise(const HexCoord coord) noexcept {
    return HexCoord{.q = -coord.r, .r = -coord.s()};
}

[[nodiscard]] constexpr HexCoord RotateCounterClockwise(const HexCoord coord) noexcept {
    return HexCoord{.q = -coord.s(), .r = -coord.q};
}

inline constexpr std::array<HexCoord, kDirectionCount> kAxialDirections = {
    HexCoord{.q = 1, .r = 0},  HexCoord{.q = 1, .r = -1}, HexCoord{.q = 0, .r = -1},
    HexCoord{.q = -1, .r = 0}, HexCoord{.q = -1, .r = 1}, HexCoord{.q = 0, .r = 1},
};

[[nodiscard]] constexpr HexCoord Neighbor(const HexCoord origin,
                                          const int directionIndex) noexcept {
    const int wrapped = ((directionIndex % kDirectionCount) + kDirectionCount) % kDirectionCount;
    return Add(origin, kAxialDirections[static_cast<std::size_t>(wrapped)]);
}

struct Point2D {
    double x{};
    double y{};
};

[[nodiscard]] inline Point2D AxialToPixel(const HexCoord coord, const double hexRadius = 1.0) {
    return Point2D{
        .x = hexRadius * (kFlatTopSpacingX * static_cast<double>(coord.q) +
                          kFlatTopOffsetX * static_cast<double>(coord.r)),
        .y = hexRadius * (kFlatTopSpacingY * static_cast<double>(coord.r)),
    };
}

inline std::ostream& operator<<(std::ostream& os_stream, const HexCoord& coord) {
    os_stream << '(' << coord.q << ',' << coord.r << ')';
    return os_stream;
}

} // namespace opusoptima::core
