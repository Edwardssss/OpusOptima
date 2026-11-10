#include "Verification/ReferenceMotion.hpp"

#include <cfenv>
#include <cfloat>
#include <cmath>
#include <limits>
#include <numbers>
#if defined(__SSE__)
#    include <xmmintrin.h>
#endif

namespace opusoptima::verification::reference_motion {
namespace {
constexpr int coordinateLimit = 1 << 20;
bool SupportedEnvironment() {
#ifdef __FAST_MATH__
    return false;
#endif
    if (!std::numeric_limits<float>::is_iec559 || !std::numeric_limits<double>::is_iec559 ||
        std::numeric_limits<float>::radix != 2 || std::numeric_limits<float>::digits != 24 ||
        std::fegetround() != FE_TONEAREST)
        return false;
#if defined(__SSE__)
    constexpr unsigned nonGradualUnderflow = (1U << 6) | (1U << 15);
    if ((_mm_getcsr() & nonGradualUnderflow) != 0) return false;
#endif
    return true;
}
bool Valid(Hex h) {
    return h.u >= -coordinateLimit && h.u <= coordinateLimit && h.v >= -coordinateLimit &&
           h.v <= coordinateLimit;
}
Point ToXY(Hex h) {
    return {82.f * (static_cast<float>(h.u) + .5f * static_cast<float>(h.v)),
            71.f * static_cast<float>(h.v)};
}
float Radians(int turns) {
    return static_cast<float>(turns * 60) * (static_cast<float>(std::numbers::pi) / 180.f);
}
bool Valid(const Motion& m, float t) {
    const bool rotational = m.mechanism == Mechanism::Swing || m.mechanism == Mechanism::Pivot;
    const bool translating = m.mechanism == Mechanism::Track || m.mechanism == Mechanism::Piston;
    const bool extending = m.mechanism == Mechanism::Piston;
    return (rotational || translating) && (rotational || m.rotation == 0) &&
           (translating || (m.translation.u == 0 && m.translation.v == 0)) &&
           (extending || m.pistonExtension == 0) && (!extending || m.isPiston) &&
           std::isfinite(t) && t >= 0.f && t <= 1.f && Valid(m.base) && Valid(m.grabberOffset) &&
           Valid(m.absoluteGrabPosition) && Valid(m.atom) && Valid(m.translation) &&
           m.baseRotation >= -5 && m.baseRotation <= 5 && m.rotation >= -1 && m.rotation <= 1 &&
           m.pistonExtension >= -1 && m.pistonExtension <= 1 &&
           (m.mechanism == Mechanism::Swing || m.mechanism == Mechanism::Pivot ||
            m.mechanism == Mechanism::Track || m.mechanism == Mechanism::Piston);
}
} // namespace

std::optional<Point> BaseAt(const Motion& m, float t) {
    if (!SupportedEnvironment() || !Valid(m, t)) return std::nullopt;
    Point v = ToXY(m.base);
    if (m.mechanism == Mechanism::Track) {
        const Point tr = ToXY(m.translation);
        v.x -= tr.x * (1.f - t);
        v.y -= tr.y * (1.f - t);
    }
    return v;
}

std::optional<double> BasePositionErrorBound(const Motion& m) {
    if (FLT_EVAL_METHOD != 0 || std::numeric_limits<double>::radix != 2 ||
        std::numeric_limits<double>::digits != 53 || !SupportedEnvironment() || !Valid(m, 0.f))
        return std::nullopt;
    const auto up = [](double x) {
        return std::nextafter(x, std::numeric_limits<double>::infinity());
    };
    const auto add = [&](double a, double b) { return up(a + b); };
    const auto multiply = [&](double a, double b) { return up(a * b); };
    const Point base = ToXY(m.base);
    const Point translation = ToXY(m.translation);
    const bool track = m.mechanism == Mechanism::Track;
    const auto axis = [&](double exactBase, double roundedBase, double exactTranslation,
                          double roundedTranslation) {
        const double baseError = std::abs(exactBase - roundedBase);
        if (!track) return baseError;
        const double translationError = std::abs(exactTranslation - roundedTranslation);
        constexpr double unitRoundoff = 0x1p-24;
        const double magnitude =
            add(std::abs(roundedBase), multiply(3., std::abs(roundedTranslation)));
        const double arithmeticError =
            add(multiply(unitRoundoff, magnitude),
                3. * static_cast<double>(std::numeric_limits<float>::denorm_min()));
        return add(add(baseError, translationError), arithmeticError);
    };
    const double x = axis(82. * (m.base.u + .5 * m.base.v), base.x,
                          82. * (m.translation.u + .5 * m.translation.v), translation.x);
    const double y = axis(71. * m.base.v, base.y, 71. * m.translation.v, translation.y);
    // The L1 bound dominates Euclidean error without depending on libm sqrt.
    if (x == 0. && y == 0.) return 0.;
    return add(x, y);
}

std::optional<double> TrackAtomPositionErrorBound(const Motion& m) {
    if (m.mechanism != Mechanism::Track || m.isPiston || m.baseRotation != 0 ||
        !BasePositionErrorBound(m))
        return std::nullopt;
    // Qualify the host libm zero-angle path; general rotations remain unsupported.
    volatile double positiveZero = 0.;
    volatile double negativeZero = -0.;
    if (std::cos(positiveZero) != 1. || std::sin(positiveZero) != 0. ||
        std::cos(negativeZero) != 1. || std::sin(negativeZero) != 0.)
        return std::nullopt;
    const auto magnitude = [](Hex h) {
        return std::abs(82. * h.u + 41. * h.v) + std::abs(71. * h.v);
    };
    const Hex offset{m.atom.u - m.absoluteGrabPosition.u, m.atom.v - m.absoluteGrabPosition.v};
    const double scale = magnitude(m.base) + magnitude(m.translation) + magnitude(m.grabberOffset) +
                         magnitude(offset);
    // Each input-to-output path has fewer than 16 rounding operations.
    // gamma_16 < 32u; L1 magnitudes avoid cancellation and a libm sqrt.
    constexpr double factor = 32. * 0x1p-24;
    const double relative = std::nextafter(factor * scale, std::numeric_limits<double>::infinity());
    return std::nextafter(relative +
                              32. * static_cast<double>(std::numeric_limits<float>::denorm_min()),
                          std::numeric_limits<double>::infinity());
}

std::optional<Point> AtomAt(const Motion& m, float t) {
    auto base = BaseAt(m, t);
    if (!base) return std::nullopt;
    Point v = *base;
    const float rotation = Radians(m.rotation);
    const float armRotation = m.mechanism == Mechanism::Swing ? rotation : 0.f;
    const float r = (0.f - rotation) * (1.f - t);
    Point g = ToXY(m.grabberOffset);
    if (m.isPiston) {
        const float len = static_cast<float>(std::sqrt(static_cast<double>(g.x * g.x + g.y * g.y)));
        if (!(len > 0.f)) return std::nullopt;
        const float newlen = len - static_cast<float>(m.pistonExtension) * 82.f * (1.f - t);
        if (!(newlen > 0.f)) return std::nullopt;
        g.x = newlen / len * g.x;
        g.y = newlen / len * g.y;
    }
    const float grabberRotation = Radians(m.baseRotation) - armRotation * (1.f - t);
    const float grx = static_cast<float>(std::cos(static_cast<double>(grabberRotation)));
    const float gry = static_cast<float>(std::sin(static_cast<double>(grabberRotation)));
    v.x += g.x * grx - g.y * gry;
    v.y += g.x * gry + g.y * grx;
    const float rx = static_cast<float>(std::cos(static_cast<double>(r)));
    const float ry = static_cast<float>(std::sin(static_cast<double>(r)));
    const Point offset =
        ToXY({m.atom.u - m.absoluteGrabPosition.u, m.atom.v - m.absoluteGrabPosition.v});
    Point result{v.x + (offset.x * rx - offset.y * ry), v.y + (offset.x * ry + offset.y * rx)};
    if (!std::isfinite(result.x) || !std::isfinite(result.y)) return std::nullopt;
    return result;
}

std::optional<Hex> SnapToHex(Point point) {
    if (!SupportedEnvironment() || !std::isfinite(point.x) || !std::isfinite(point.y))
        return std::nullopt;
    const float u = point.x / 82.f - .5f * point.y / 71.f;
    const float v = point.y / 71.f;
    const float w = 0.f - u - v;
    // Bound before float-to-int conversion and reserve room for the six neighbours.
    if (std::abs(u) > coordinateLimit - 1 || std::abs(v) > coordinateLimit - 1 ||
        std::abs(w) > 2 * coordinateLimit - 2)
        return std::nullopt;
    int ui = static_cast<int>(std::round(u));
    int vi = static_cast<int>(std::round(v));
    const int wi = static_cast<int>(std::round(w));
    const float uf = std::abs(u - static_cast<float>(ui));
    const float vf = std::abs(v - static_cast<float>(vi));
    const float wf = std::abs(w - static_cast<float>(wi));
    if (uf > vf && uf > wf)
        ui = -vi - wi;
    else if (vf > wf)
        vi = -ui - wi;
    const Hex result{ui, vi};
    if (!Valid(result)) return std::nullopt;
    return result;
}

std::optional<std::size_t> BoardAreaCells(Point center, float radius, Hex (&cells)[7]) {
    const auto snapped = SnapToHex(center);
    if (!snapped || !std::isfinite(radius) || (radius != 15.f && radius != 20.f && radius != 29.f))
        return std::nullopt;
    constexpr Hex offsets[] = {{0, 0}, {1, 0}, {0, 1}, {-1, 1}, {-1, 0}, {0, -1}, {1, -1}};
    std::size_t count = 0;
    for (const auto offset : offsets) {
        const Hex cell{snapped->u + offset.u, snapped->v + offset.v};
        if (!Valid(cell)) return std::nullopt;
        const Point xy = ToXY(cell);
        const float dx = xy.x - center.x;
        const float dy = xy.y - center.y;
        const float distance =
            static_cast<float>(std::sqrt(static_cast<double>(dx * dx + dy * dy)));
        if (distance < radius + 29.f) cells[count++] = cell;
    }
    return count;
}

std::optional<float> CollisionIncrement(int maximumRotationDistance) {
    if (!SupportedEnvironment() || maximumRotationDistance < 1 ||
        maximumRotationDistance > coordinateLimit)
        return std::nullopt;
    double increment =
        .25 / std::pow(2., std::round(std::log(static_cast<double>(maximumRotationDistance)) /
                                      std::log(2.)));
    if (!(increment <= .125)) increment = .125;
    return static_cast<float>(increment);
}

std::optional<std::size_t> InteriorSampleCount(int maximumRotationDistance, std::size_t limit) {
    const auto increment = CollisionIncrement(maximumRotationDistance);
    if (!increment) return std::nullopt;
    // Keep the reference libm classification; only count its dyadic cadence exactly.
    if (!std::numeric_limits<float>::is_iec559 || std::numeric_limits<float>::radix != 2 ||
        std::numeric_limits<float>::digits < 23)
        return std::nullopt;
    int exponent = 0;
    if (std::frexp(*increment, &exponent) != .5f || exponent < -21 || exponent > -2)
        return std::nullopt;
    const auto denominatorExponent = static_cast<unsigned>(1 - exponent);
    if (denominatorExponent >= std::numeric_limits<std::size_t>::digits) return std::nullopt;
    // All j / 2^k through 1 are exact floats for k <= 22, so repeated addition is exact.
    const std::size_t count = (std::size_t{1} << denominatorExponent) - 1;
    if (count > limit) return std::nullopt;
    return count;
}
} // namespace opusoptima::verification::reference_motion
