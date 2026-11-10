#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>

namespace opusoptima::verification::continuous {
struct Point {
    double x{}, y{};
    friend bool operator==(const Point&, const Point&) = default;
};
struct Bounds {
    long double minX{}, maxX{}, minY{}, maxY{};
};
struct Trajectory {
    Point start{}, center{}, translation{};
    double angle{};
};
enum class Separation { Separated, Collision, Uncertain };
struct Options {
    std::size_t maxNodes{4096};
    unsigned maxDepth{40};
};

struct Work {
    std::uint64_t calls{}, linear{}, rigid{}, broad{}, segments{}, maxDepth{}, uncertain{};
};

[[nodiscard]] Point PointAt(const Trajectory&, double time);
// Encloses the ideal real-valued trajectory, not reference/game floating-point motion.
[[nodiscard]] std::optional<Bounds> Enclose(const Trajectory&, double begin, double end);
[[nodiscard]] Separation CheckSeparation(const Trajectory&, const Trajectory&,
                                         double minimumDistance, Options = {}, Work* = nullptr);
// Errors bound each body's Euclidean deviation at every time, including endpoints.
// Nonzero errors only certify separation; an enlarged-radius hit returns Uncertain.
// The caller must prove these bounds for its actual motion; this API does not derive them.
[[nodiscard]] Separation CheckBoundedSeparation(const Trajectory&, double positionErrorA,
                                                const Trajectory&, double positionErrorB,
                                                double minimumDistance, Options = {},
                                                Work* = nullptr);
} // namespace opusoptima::verification::continuous
