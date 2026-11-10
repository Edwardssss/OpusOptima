#include "ContinuousCollision.hpp"

#include <algorithm>
#include <array>
#include <cfenv>
#include <cfloat>
#include <cmath>
#include <limits>
#include <vector>
#if defined(__SSE__)
#    include <xmmintrin.h>
#endif

namespace opusoptima::verification::continuous {
namespace {
using Real = long double;
struct Interval {
    Real low{}, high{};
};
struct Vector {
    Interval x, y;
};
Real Down(Real value) {
    return std::nextafter(value, -std::numeric_limits<Real>::infinity());
}
Real Up(Real value) {
    return std::nextafter(value, std::numeric_limits<Real>::infinity());
}
Interval Add(Interval a, Interval b) {
    return {Down(a.low + b.low), Up(a.high + b.high)};
}
Interval Negate(Interval value) {
    return {-value.high, -value.low};
}
Interval Subtract(Interval a, Interval b) {
    return Add(a, Negate(b));
}
Interval Multiply(Interval a, Interval b) {
    const std::array<Real, 4> values{a.low * b.low, a.low * b.high, a.high * b.low,
                                     a.high * b.high};
    return {Down(*std::min_element(values.begin(), values.end())),
            Up(*std::max_element(values.begin(), values.end()))};
}
Interval DividePositive(Interval a, Interval b) {
    return Multiply(a, {Down(1 / b.high), Up(1 / b.low)});
}
Interval Square(Interval value) {
    const Real greatest = std::max(std::abs(value.low), std::abs(value.high));
    const Real least =
        value.low <= 0 && value.high >= 0 ? 0 : std::min(std::abs(value.low), std::abs(value.high));
    return {least == 0 ? 0 : Down(least * least), Up(greatest * greatest)};
}
Interval Dot(Vector a, Vector b) {
    return Add(Multiply(a.x, b.x), Multiply(a.y, b.y));
}
Interval NormSquared(Vector value) {
    return Add(Square(value.x), Square(value.y));
}
Vector Subtract(Vector a, Vector b) {
    return {Subtract(a.x, b.x), Subtract(a.y, b.y)};
}
Vector Exact(Point point) {
    return {{point.x, point.x}, {point.y, point.y}};
}
Real AbsoluteBound(Interval value) {
    return std::max(std::abs(value.low), std::abs(value.high));
}

std::pair<Interval, Interval> SinCos(Interval angle) {
    const auto square = Square(angle);
    Interval sinTerm = angle, cosTerm{1, 1}, sine = sinTerm, cosine = cosTerm;
    for (unsigned k = 1; k < 32; ++k) {
        const Real sinDivisor = (2 * k) * (2 * k + 1);
        const Real cosDivisor = (2 * k - 1) * (2 * k);
        sinTerm = DividePositive(Negate(Multiply(sinTerm, square)), {sinDivisor, sinDivisor});
        cosTerm = DividePositive(Negate(Multiply(cosTerm, square)), {cosDivisor, cosDivisor});
        sine = Add(sine, sinTerm);
        cosine = Add(cosine, cosTerm);
    }
    // Both polynomials are Taylor expansions through degree 63; |f^(64)| <= 1.
    Real remainder = 1;
    const Real magnitude = AbsoluteBound(angle);
    for (unsigned i = 1; i <= 64; ++i)
        remainder = Up(Up(remainder * magnitude) / i);
    const Interval error{-remainder, remainder};
    return {Add(sine, error), Add(cosine, error)};
}
Vector At(const Trajectory& trajectory, Real time) {
    const auto start = Exact(trajectory.start), center = Exact(trajectory.center);
    const auto offset = Subtract(start, center);
    const auto displacement = Exact(trajectory.translation);
    const Interval t{time, time};
    if (trajectory.angle == 0)
        return {Add(start.x, Multiply(displacement.x, t)),
                Add(start.y, Multiply(displacement.y, t))};
    const auto [sine, cosine] = SinCos(Multiply({trajectory.angle, trajectory.angle}, t));
    return {Add(Add(center.x, Subtract(Multiply(offset.x, cosine), Multiply(offset.y, sine))),
                Multiply(displacement.x, t)),
            Add(Add(center.y, Add(Multiply(offset.x, sine), Multiply(offset.y, cosine))),
                Multiply(displacement.y, t))};
}
bool Valid(const Trajectory& trajectory) {
    for (double value : {trajectory.start.x, trajectory.start.y, trajectory.center.x,
                         trajectory.center.y, trajectory.translation.x, trajectory.translation.y})
        if (!std::isfinite(value) || std::abs(value) > 1.e9) return false;
    return std::isfinite(trajectory.angle) && std::abs(trajectory.angle) <= 8;
}
Real SpeedBound(const Trajectory& trajectory) {
    const auto offset = Subtract(Exact(trajectory.start), Exact(trajectory.center));
    const Real radiusBound = Up(AbsoluteBound(offset.x) + AbsoluteBound(offset.y));
    const Real rotationBound = Up(std::abs(static_cast<Real>(trajectory.angle)) * radiusBound);
    return Up(rotationBound + Up(std::abs(static_cast<Real>(trajectory.translation.x)) +
                                 std::abs(static_cast<Real>(trajectory.translation.y))));
}
bool SupportedEnvironment() {
#if defined(__FAST_MATH__) || FLT_EVAL_METHOD != 0
    return false;
#endif
    if (std::fegetround() != FE_TONEAREST) return false;
    // The runtime arithmetic precision can be narrower than the storage format (x87).
    volatile Real one = 1;
    volatile Real epsilon = std::numeric_limits<Real>::epsilon();
    volatile Real next = one + epsilon;
    if (next <= one) return false;
    if (!std::numeric_limits<double>::is_iec559 || std::numeric_limits<double>::radix != 2 ||
        !std::numeric_limits<Real>::is_iec559 || std::numeric_limits<Real>::radix != 2 ||
        std::numeric_limits<Real>::digits < std::numeric_limits<double>::digits ||
        std::numeric_limits<Real>::min_exponent - std::numeric_limits<Real>::digits >
            std::numeric_limits<double>::min_exponent - std::numeric_limits<double>::digits ||
        std::numeric_limits<Real>::max_exponent < std::numeric_limits<double>::max_exponent)
        return false;
#if defined(__SSE__)
    constexpr unsigned nonGradualUnderflow = (1U << 6) | (1U << 15);
    if ((_mm_getcsr() & nonGradualUnderflow) != 0) return false;
#endif
    return true;
}
Separation Complete(Separation result, Work* work) {
    if (work && result == Separation::Uncertain) ++work->uncertain;
    return result;
}
Separation Classify(Interval squaredDistance, Interval squaredThreshold) {
    if (squaredDistance.low > squaredThreshold.high) return Separation::Separated;
    if (squaredDistance.high < squaredThreshold.low) return Separation::Collision;
    return Separation::Uncertain;
}
Separation Linear(const Trajectory& a, const Trajectory& b, Interval threshold) {
    const auto offset = Subtract(Exact(a.start), Exact(b.start));
    const auto velocity = Subtract(Exact(a.translation), Exact(b.translation));
    const auto speedSquared = NormSquared(velocity);
    if (a.translation == b.translation) return Classify(NormSquared(offset), threshold);
    if (speedSquared.low <= 0) return Separation::Uncertain;
    const auto projection = Negate(Dot(offset, velocity));
    auto time = DividePositive(projection, speedSquared);
    time.low = std::clamp(time.low, Real{0}, Real{1});
    time.high = std::clamp(time.high, Real{0}, Real{1});
    const Vector nearest{Add(offset.x, Multiply(velocity.x, time)),
                         Add(offset.y, Multiply(velocity.y, time))};
    const auto distance = NormSquared(nearest);
    if (distance.low > threshold.high) return Separation::Separated;
    const Real candidate = (time.low + time.high) / 2;
    const auto atCandidate = NormSquared(Subtract(At(a, candidate), At(b, candidate)));
    if (atCandidate.high < threshold.low) return Separation::Collision;
    return Separation::Uncertain;
}
} // namespace

Point PointAt(const Trajectory& trajectory, double time) {
    if (!Valid(trajectory) || !std::isfinite(time) || time < 0 || time > 1)
        return {std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::quiet_NaN()};
    const auto point = At(trajectory, time);
    return {static_cast<double>((point.x.low + point.x.high) / 2),
            static_cast<double>((point.y.low + point.y.high) / 2)};
}

std::optional<Bounds> Enclose(const Trajectory& trajectory, double begin, double end) {
    if (!SupportedEnvironment() || !Valid(trajectory) || !std::isfinite(begin) ||
        !std::isfinite(end) || begin < 0 || end > 1 || begin > end)
        return std::nullopt;
    const Real midpoint = (static_cast<Real>(begin) + static_cast<Real>(end)) / 2;
    const auto point = At(trajectory, midpoint);
    const Real halfWidth = std::max(Up(midpoint - begin), Up(end - midpoint));
    const Real reach = Up(SpeedBound(trajectory) * halfWidth);
    const auto x = Add(point.x, {-reach, reach});
    const auto y = Add(point.y, {-reach, reach});
    return Bounds{x.low, x.high, y.low, y.high};
}

Separation CheckSeparation(const Trajectory& a, const Trajectory& b, double minimumDistance,
                           Options options, Work* work) {
    if (work) ++work->calls;
    if (!SupportedEnvironment()) return Complete(Separation::Uncertain, work);
    if (!Valid(a) || !Valid(b) || !std::isfinite(minimumDistance) || minimumDistance < 0 ||
        minimumDistance > 1.e9 || options.maxNodes > 1048576 || options.maxDepth > 64)
        return Complete(Separation::Uncertain, work);
    if (minimumDistance == 0) return Complete(Separation::Separated, work);
    const Interval threshold = Square({minimumDistance, minimumDistance});
    if (a.angle == 0 && b.angle == 0) {
        if (work) ++work->linear;
        return Complete(Linear(a, b, threshold), work);
    }
    if (a.center == b.center && a.translation == b.translation && a.angle == b.angle) {
        if (work) ++work->rigid;
        return Complete(Classify(NormSquared(Subtract(Exact(a.start), Exact(b.start))), threshold),
                        work);
    }
    const Real speed = Up(SpeedBound(a) + SpeedBound(b));
    const auto reachable = Square(Add({minimumDistance, minimumDistance}, {0, speed}));
    if (NormSquared(Subtract(Exact(a.start), Exact(b.start))).low > reachable.high) {
        if (work) ++work->broad;
        return Complete(Separation::Separated, work);
    }
    struct Segment {
        Real low, high;
        unsigned depth;
    };
    std::vector<Segment> pending{{0, 1, 0}};
    std::size_t nodes = 0;
    while (!pending.empty()) {
        if (nodes++ >= options.maxNodes) return Complete(Separation::Uncertain, work);
        const auto segment = pending.back();
        pending.pop_back();
        if (work) {
            ++work->segments;
            work->maxDepth = std::max(work->maxDepth, static_cast<std::uint64_t>(segment.depth));
        }
        const Real midpoint = (segment.low + segment.high) / 2;
        const auto difference = Subtract(At(a, midpoint), At(b, midpoint));
        const auto distance = NormSquared(difference);
        if (distance.high < threshold.low) return Complete(Separation::Collision, work);
        if (midpoint == segment.low || midpoint == segment.high)
            return Complete(Separation::Uncertain, work);
        const Real halfWidth = std::max(Up(midpoint - segment.low), Up(segment.high - midpoint));
        const Real reach = Up(speed * halfWidth);
        const auto expandedThreshold = Square(Add({minimumDistance, minimumDistance}, {0, reach}));
        if (distance.low > expandedThreshold.high) continue;
        if (segment.depth >= options.maxDepth) return Complete(Separation::Uncertain, work);
        pending.push_back({midpoint, segment.high, segment.depth + 1});
        pending.push_back({segment.low, midpoint, segment.depth + 1});
    }
    return Complete(Separation::Separated, work);
}

Separation CheckBoundedSeparation(const Trajectory& a, double positionErrorA, const Trajectory& b,
                                  double positionErrorB, double minimumDistance, Options options,
                                  Work* work) {
    if (!SupportedEnvironment() || !std::isfinite(positionErrorA) || positionErrorA < 0 ||
        !std::isfinite(positionErrorB) || positionErrorB < 0 || !std::isfinite(minimumDistance) ||
        minimumDistance < 0 || minimumDistance > 1.e9) {
        if (work) ++work->calls;
        return Complete(Separation::Uncertain, work);
    }
    if (positionErrorA == 0 && positionErrorB == 0)
        return CheckSeparation(a, b, minimumDistance, options, work);
    const Real expanded =
        Up(Up(static_cast<Real>(minimumDistance) + positionErrorA) + positionErrorB);
    if (expanded > 1.e9L) {
        if (work) ++work->calls;
        return Complete(Separation::Uncertain, work);
    }
    const double threshold =
        std::nextafter(static_cast<double>(expanded), std::numeric_limits<double>::infinity());
    const auto result = CheckSeparation(a, b, threshold, options, work);
    // Collision inside an enlarged exclusion radius is not a collision of the real bodies.
    if (result == Separation::Collision) return Complete(Separation::Uncertain, work);
    return result;
}
} // namespace opusoptima::verification::continuous
