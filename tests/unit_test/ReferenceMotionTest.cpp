#include "Verification/ReferenceMotion.hpp"
#include "Verification/IndependentMetrics.hpp"
#include "Verification/ContinuousCollision.hpp"
#include <gtest/gtest.h>
#include <cfloat>
#include <cmath>
#include <cfenv>
#include <limits>
#if defined(__SSE__)
#include <xmmintrin.h>
#endif

namespace {
struct RestoreRounding {
    int previous = std::fegetround();
    ~RestoreRounding() { std::fesetround(previous); }
};
#if defined(__SSE__)
struct RestoreControl {
    unsigned previous = _mm_getcsr();
    ~RestoreControl() { _mm_setcsr(previous); }
};
#endif
}

namespace rm = opusoptima::verification::reference_motion;

TEST(ReferenceMotion, SnapUsesStrictResidualTieOrder) {
    for (const auto point : {rm::Point{41.f, 0.f}, rm::Point{-41.f, 0.f}}) {
        const auto snapped = rm::SnapToHex(point);
        ASSERT_TRUE(snapped);
        EXPECT_EQ(snapped->u, point.x > 0.f ? 1 : -1);
        EXPECT_EQ(snapped->v, 0);
    }
    for (int u = -12; u <= 12; ++u) {
        for (int v = -12; v <= 12; ++v) {
            const auto snapped = rm::SnapToHex({82.f * (u + .5f * v), 71.f * v});
            ASSERT_TRUE(snapped);
            EXPECT_EQ(snapped->u, u);
            EXPECT_EQ(snapped->v, v);
        }
    }
}

TEST(ReferenceMotion, BoardRasterUsesStrictContactAndReferenceOrder) {
    rm::Hex cells[7];
    EXPECT_EQ(rm::BoardAreaCells({0.f, 0.f}, 29.f, cells), 1U);
    EXPECT_EQ(cells[0].u, 0);
    EXPECT_EQ(cells[0].v, 0);
    ASSERT_EQ(rm::BoardAreaCells({41.f, 0.f}, 29.f, cells), 2U);
    EXPECT_EQ(cells[0].u, 1);
    EXPECT_EQ(cells[1].u, 0);
    EXPECT_EQ(rm::BoardAreaCells({44.f, 0.f}, 15.f, cells), 1U);
    EXPECT_EQ(cells[0].u, 1);
    EXPECT_EQ(cells[0].v, 0);
    EXPECT_EQ(rm::BoardAreaCells({43.999f, 0.f}, 15.f, cells), 2U);
}

TEST(ReferenceMotion, SevenCandidatesCoverHorizontalAxisWithinBoundedDomain) {
    RestoreRounding restore;
    ASSERT_EQ(std::fesetround(FE_TONEAREST), 0);
    const float limit = std::ldexp(1.f, 26);
    // Probe radius contacts, snap ties, and the largest admitted magnitudes.
    for (float boundary : {-limit, -41.f, -38.f, -33.f, -24.f, 0.f,
                           24.f, 33.f, 38.f, 41.f, limit}) {
        for (float x : {std::nextafter(boundary, -INFINITY), boundary,
                        std::nextafter(boundary, INFINITY)}) {
            if (std::abs(x) > limit) continue;
            const auto snapped = rm::SnapToHex({x, 0.f});
            ASSERT_TRUE(snapped);
            ASSERT_EQ(snapped->v, 0);
            for (float radius : {15.f, 20.f, 29.f}) {
                rm::Hex cells[7];
                const auto count = rm::BoardAreaCells({x, 0.f}, radius, cells);
                ASSERT_TRUE(count);
                std::size_t expectedCount = 0;
                for (int u = snapped->u - 3; u <= snapped->u + 3; ++u) {
                    for (int v = -2; v <= 2; ++v) {
                        const float dx = 82.f * (static_cast<float>(u) +
                                                 .5f * static_cast<float>(v)) - x;
                        const float dy = 71.f * static_cast<float>(v);
                        const float distance = static_cast<float>(
                            std::sqrt(static_cast<double>(dx * dx + dy * dy)));
                        if (distance >= radius + 29.f) continue;
                        ++expectedCount;
                        bool found = false;
                        for (std::size_t i = 0; i < *count; ++i)
                            found |= cells[i].u == u && cells[i].v == v;
                        EXPECT_TRUE(found) << "x=" << x << " radius=" << radius
                                           << " cell=" << u << ',' << v;
                    }
                }
                EXPECT_EQ(*count, expectedCount);
            }
        }
    }
}

TEST(ReferenceMotion, SevenCandidatesCoverGeneralTwoDimensionalAdversaries) {
    RestoreRounding restore;
    ASSERT_EQ(std::fesetround(FE_TONEAREST), 0);
    ASSERT_EQ(FLT_EVAL_METHOD, 0);
    ASSERT_EQ(std::numeric_limits<double>::digits, 53);
    constexpr int limit = 1 << 20;
    const rm::Hex anchors[] = {{0, 0}, {17, -31}, {-31, 17},
        {limit - 3, limit - 3}, {-limit + 3, -limit + 3},
        {limit - 3, -limit + 3}, {-limit + 3, limit - 3},
        {limit - 3, 1}, {1, limit - 3}, {-limit + 3, -1},
        {-1, -limit + 3}, {524288, 524288}, {-524288, 524288}};
    // Oblique cube ties, contact neighbourhoods, and large-coordinate cancellation.
    const rm::Point offsets[] = {{0.f, 0.f}, {41.f, 35.5f}, {-41.f, -35.5f},
        {20.5f, 35.5f}, {-20.5f, 35.5f}, {41.f, 71.f / 3.f},
        {-41.f, -71.f / 3.f}, {26.4f, 35.2f}, {29.4f, 39.2f},
        {34.8f, 46.4f}, {-34.8f, 46.4f}};
    std::size_t accepted = 0;
    for (const auto anchor : anchors) {
        const float ax = 82.f * (static_cast<float>(anchor.u) +
                                 .5f * static_cast<float>(anchor.v));
        const float ay = 71.f * static_cast<float>(anchor.v);
        for (const auto offset : offsets) {
            const float x = ax + offset.x;
            const float y = ay + offset.y;
            for (float cx : {std::nextafter(x, -INFINITY), x,
                             std::nextafter(x, INFINITY)}) {
                for (float cy : {std::nextafter(y, -INFINITY), y,
                                 std::nextafter(y, INFINITY)}) {
                    const rm::Point center{cx, cy};
                    const auto snapped = rm::SnapToHex(center);
                    ASSERT_TRUE(snapped);
                    for (float radius : {15.f, 20.f, 29.f}) {
                        rm::Hex cells[7];
                        const auto count = rm::BoardAreaCells(center, radius, cells);
                        ASSERT_TRUE(count.has_value());
                        ++accepted;
                        std::size_t expected = 0;
                        for (int u = snapped->u - 4; u <= snapped->u + 4; ++u) {
                            for (int v = snapped->v - 4; v <= snapped->v + 4; ++v) {
                                if (std::abs(u) > limit || std::abs(v) > limit) continue;
                                const float dx = 82.f * (static_cast<float>(u) +
                                    .5f * static_cast<float>(v)) - cx;
                                const float dy = 71.f * static_cast<float>(v) - cy;
                                const float distance = static_cast<float>(std::sqrt(
                                    static_cast<double>(dx * dx + dy * dy)));
                                if (distance >= radius + 29.f) continue;
                                ++expected;
                                bool found = false;
                                for (std::size_t i = 0; i < *count; ++i)
                                    found |= cells[i].u == u && cells[i].v == v;
                                EXPECT_TRUE(found) << std::hexfloat << "center=" << cx
                                    << ',' << cy << " radius=" << radius
                                    << " snap=" << snapped->u << ',' << snapped->v
                                    << " cell=" << u << ',' << v << " d=" << distance;
                            }
                        }
                        EXPECT_EQ(*count, expected);
                    }
                }
            }
        }
    }
    EXPECT_EQ(accepted, 3861U);
}

TEST(ReferenceMotion, RasterRejectsUnsupportedInputsAndEnvironment) {
    rm::Hex cells[7];
    EXPECT_FALSE(rm::SnapToHex({std::numeric_limits<float>::infinity(), 0.f}));
    EXPECT_FALSE(rm::SnapToHex({std::numeric_limits<float>::max(), 0.f}));
    EXPECT_FALSE(rm::SnapToHex({0.f, std::numeric_limits<float>::quiet_NaN()}));
    EXPECT_FALSE(rm::BoardAreaCells({0.f, 0.f}, 30.f, cells));
    EXPECT_FALSE(rm::BoardAreaCells({0.f, 0.f}, -1.f, cells));
    EXPECT_FALSE(rm::BoardAreaCells({0.f, 0.f}, std::numeric_limits<float>::infinity(), cells));
    EXPECT_FALSE(rm::BoardAreaCells({0.f, 0.f}, std::numeric_limits<float>::quiet_NaN(), cells));
    RestoreRounding restore;
    ASSERT_EQ(std::fesetround(FE_UPWARD), 0);
    EXPECT_FALSE(rm::SnapToHex({0.f, 0.f}));
    EXPECT_FALSE(rm::BoardAreaCells({0.f, 0.f}, 29.f, cells));
}

TEST(ReferenceMotion, UnsupportedRoundingFailsClosed) {
    RestoreRounding restore;
    rm::Motion m;
    m.mechanism = rm::Mechanism::Track;
    rm::Hex cells[7];
    for (int mode : {FE_DOWNWARD, FE_UPWARD, FE_TOWARDZERO}) {
        ASSERT_EQ(std::fesetround(mode), 0);
        EXPECT_FALSE(rm::TrackAtomPositionErrorBound(m));
        EXPECT_FALSE(rm::BasePositionErrorBound(m));
        EXPECT_FALSE(rm::SnapToHex({0.f, 0.f}));
        EXPECT_FALSE(rm::BoardAreaCells({0.f, 0.f}, 29.f, cells));
        EXPECT_FALSE(rm::AtomAt(m, .5f));
        EXPECT_FALSE(rm::BaseAt(m, .5f));
        EXPECT_FALSE(rm::CollisionIncrement(10));
        EXPECT_FALSE(rm::InteriorSampleCount(10));
    }
    ASSERT_EQ(std::fesetround(FE_TONEAREST), 0);
    EXPECT_TRUE(rm::TrackAtomPositionErrorBound(m));
    EXPECT_TRUE(rm::BasePositionErrorBound(m));
    EXPECT_TRUE(rm::AtomAt(m, .5f));
    EXPECT_TRUE(rm::BaseAt(m, .5f));
    EXPECT_TRUE(rm::CollisionIncrement(10));
    EXPECT_TRUE(rm::InteriorSampleCount(10));
}

#if defined(__SSE__)
TEST(ReferenceMotion, NonGradualUnderflowFailsClosed) {
    RestoreControl restore;
    rm::Motion m;
    m.mechanism = rm::Mechanism::Track;
    rm::Hex cells[7];
    for (unsigned flag : {1U << 6, 1U << 15, (1U << 6) | (1U << 15)}) {
        _mm_setcsr((restore.previous & ~((1U << 6) | (1U << 15))) | flag);
        EXPECT_FALSE(rm::TrackAtomPositionErrorBound(m));
        EXPECT_FALSE(rm::BasePositionErrorBound(m));
        EXPECT_FALSE(rm::SnapToHex({0.f, 0.f}));
        EXPECT_FALSE(rm::BoardAreaCells({0.f, 0.f}, 29.f, cells));
        EXPECT_FALSE(rm::AtomAt(m, .5f));
        EXPECT_FALSE(rm::BaseAt(m, .5f));
        EXPECT_FALSE(rm::CollisionIncrement(10));
        EXPECT_FALSE(rm::InteriorSampleCount(10));
    }
    _mm_setcsr(restore.previous & ~((1U << 6) | (1U << 15)));
    EXPECT_TRUE(rm::TrackAtomPositionErrorBound(m));
}
#endif

TEST(ReferenceMotion, DyadicCountsAcrossEveryCadenceTransition) {
    for (int k = 0; k <= 20; ++k) {
        const int power = 1 << k;
        const int threshold = static_cast<int>(std::sqrt(2.) * power);
        for (int distance : {power - 1, power, power + 1, threshold, threshold + 1}) {
            if (distance < 1 || distance > (1 << 20)) continue;
            const auto increment = rm::CollisionIncrement(distance);
            ASSERT_TRUE(increment);
            std::size_t iterativeCount = 0;
            for (float t = *increment; t < 1.f; t += *increment) ++iterativeCount;
            EXPECT_EQ(rm::InteriorSampleCount(distance, iterativeCount), iterativeCount);
            EXPECT_FALSE(rm::InteriorSampleCount(distance, iterativeCount - 1));
        }
    }
    EXPECT_EQ(rm::InteriorSampleCount(1 << 20, std::numeric_limits<std::size_t>::max()),
              (std::size_t{1} << 22) - 1);
}

TEST(ReferenceMotion, TrackUsesFinalBaseAndAtomOffset) {
    rm::Motion m;
    m.mechanism = rm::Mechanism::Track;
    m.base = {1, 0};
    m.translation = {1, 0};
    m.grabberOffset = {1, 0};
    m.absoluteGrabPosition = {2, 0};
    m.atom = {3, 0};
    for (float t : {0.f, .5f, 1.f}) {
        auto p = rm::AtomAt(m, t);
        ASSERT_TRUE(p);
        EXPECT_FLOAT_EQ(p->x, 164.f + 82.f * t);
        EXPECT_FLOAT_EQ(p->y, 0.f);
        ASSERT_TRUE(rm::BaseAt(m, t));
        EXPECT_FLOAT_EQ(rm::BaseAt(m, t)->x, 82.f * t);
    }
}

TEST(ReferenceMotion, PistonReconstructsBothExtensionDirections) {
    for (int extension : {-1, 1}) {
        rm::Motion m;
        m.mechanism = rm::Mechanism::Piston;
        m.isPiston = true;
        m.grabberOffset = {2, 0};
        m.absoluteGrabPosition = {2, 0};
        m.atom = {2, 0};
        m.pistonExtension = extension;
        for (float t : {0.f, .5f, 1.f}) {
            auto p = rm::AtomAt(m, t);
            ASSERT_TRUE(p);
            EXPECT_FLOAT_EQ(p->x, 164.f - extension * 82.f * (1.f - t));
            EXPECT_FLOAT_EQ(p->y, 0.f);
        }
    }
}

TEST(ReferenceMotion, SwingUsesFinalIntegerGeometryInBothDirections) {
    for (int rotation : {-1, 1}) {
        rm::Motion m;
        m.mechanism = rm::Mechanism::Swing;
        m.grabberOffset = {1, 0};
        m.baseRotation = rotation;
        m.rotation = rotation;
        m.absoluteGrabPosition = rotation == 1 ? rm::Hex{0, 1} : rm::Hex{1, -1};
        m.atom = rotation == 1 ? rm::Hex{0, 10} : rm::Hex{10, -10};
        auto start = rm::AtomAt(m, 0.f);
        auto end = rm::AtomAt(m, 1.f);
        ASSERT_TRUE(start);
        ASSERT_TRUE(end);
        EXPECT_GT(start->x, 819.f);
        EXPECT_LT(start->x, 821.f);
        EXPECT_NEAR(start->y, 0.f, .15f);
        // Final grabber rotation is floating point, not an integer endpoint snap.
        EXPECT_NEAR(end->y, rotation * 710.f, .02f);
        auto middle = rm::AtomAt(m, .5f);
        ASSERT_TRUE(middle);
        EXPECT_GT(rotation * middle->y, 350.f);
    }
}

TEST(ReferenceMotion, PivotKeepsGrabberFixedWhileRotatingAtomOffset) {
    rm::Motion m;
    m.rotation = 1;
    m.grabberOffset = {1, 0};
    m.absoluteGrabPosition = {1, 0};
    m.atom = {1, 1};
    auto end = rm::AtomAt(m, 1.f);
    ASSERT_TRUE(end);
    EXPECT_FLOAT_EQ(end->x, 123.f);
    EXPECT_FLOAT_EQ(end->y, 71.f);
    m.atom = m.absoluteGrabPosition;
    for (float t : {0.f, .5f, 1.f}) {
        auto grab = rm::AtomAt(m, t);
        ASSERT_TRUE(grab);
        EXPECT_FLOAT_EQ(grab->x, 82.f);
        EXPECT_FLOAT_EQ(grab->y, 0.f);
    }
}

TEST(ReferenceMotion, PistonAttributeCombinesWithEveryMovementKind) {
    for (auto kind : {rm::Mechanism::Swing, rm::Mechanism::Pivot, rm::Mechanism::Track}) {
        rm::Motion m;
        m.mechanism = kind;
        m.grabberOffset = {2, 0};
        m.absoluteGrabPosition = {2, 0};
        m.atom = {3, 0};
        m.rotation = kind == rm::Mechanism::Track ? 0 : 1;
        m.translation = kind == rm::Mechanism::Track ? rm::Hex{1, 0} : rm::Hex{};
        for (float t : {0.f, .5f, 1.f}) {
            auto ordinary = rm::AtomAt(m, t);
            m.isPiston = true;
            auto piston = rm::AtomAt(m, t);
            ASSERT_TRUE(ordinary);
            ASSERT_TRUE(piston);
            EXPECT_FLOAT_EQ(piston->x, ordinary->x);
            EXPECT_FLOAT_EQ(piston->y, ordinary->y);
            m.isPiston = false;
        }
        m.grabberOffset = {};
        m.isPiston = true;
        EXPECT_FALSE(rm::AtomAt(m, .5f));
    }
}

TEST(ReferenceMotion, ConflictingFieldsFailClosed) {
    rm::Motion m;
    m.translation = {1, 0};
    EXPECT_FALSE(rm::BasePositionErrorBound(m));
    EXPECT_FALSE(rm::AtomAt(m, .5f));
    m.translation = {};
    m.pistonExtension = 1;
    EXPECT_FALSE(rm::BasePositionErrorBound(m));
    EXPECT_FALSE(rm::AtomAt(m, .5f));
    m.pistonExtension = 0;
    m.mechanism = rm::Mechanism::Track;
    m.rotation = 1;
    EXPECT_FALSE(rm::BasePositionErrorBound(m));
    EXPECT_FALSE(rm::AtomAt(m, .5f));
    m.rotation = 0;
    m.mechanism = rm::Mechanism::Piston;
    EXPECT_FALSE(rm::BasePositionErrorBound(m));
    EXPECT_FALSE(rm::AtomAt(m, .5f));
    m.mechanism = static_cast<rm::Mechanism>(999);
    EXPECT_FALSE(rm::BasePositionErrorBound(m));
    EXPECT_FALSE(rm::AtomAt(m, .5f));
}

TEST(ReferenceMotion, CoordinateBoundaryAndZeroSampleBudget) {
    rm::Motion m;
    m.base = {1 << 20, -(1 << 20)};
    ASSERT_TRUE(rm::AtomAt(m, 1.f));
    EXPECT_FLOAT_EQ(rm::AtomAt(m, 1.f)->x, 41.f * (1 << 20));
    EXPECT_FLOAT_EQ(rm::AtomAt(m, 1.f)->y, -71.f * (1 << 20));
    ++m.base.u;
    EXPECT_FALSE(rm::AtomAt(m, 1.f));
    EXPECT_FALSE(rm::InteriorSampleCount(1, 0));
    EXPECT_EQ(rm::InteriorSampleCount(2), 7u);
    EXPECT_EQ(rm::InteriorSampleCount(3), 15u);
}

TEST(ReferenceMotion, CadenceAndResourceLimits) {
    ASSERT_TRUE(rm::CollisionIncrement(1));
    EXPECT_FLOAT_EQ(*rm::CollisionIncrement(1), .125f);
    EXPECT_EQ(rm::InteriorSampleCount(1), 7u);
    EXPECT_FLOAT_EQ(*rm::CollisionIncrement(10), .03125f);
    EXPECT_EQ(rm::InteriorSampleCount(10), 31u);
    EXPECT_FALSE(rm::InteriorSampleCount(10, 30));
    EXPECT_FALSE(rm::InteriorSampleCount(1 << 20));
    EXPECT_FALSE(rm::CollisionIncrement(0));
    EXPECT_FALSE(rm::CollisionIncrement(std::numeric_limits<int>::max()));
}

TEST(ReferenceMotion, BaseBoundCoversRoundingAndCoordinateExtremes) {
    constexpr int limit = 1 << 20;
    const rm::Hex positions[] = {{0, 0}, {limit, limit}, {-limit, limit},
                                 {limit - 1, -limit + 3}, {1, -1}};
    const float progress[] = {0.f, std::numeric_limits<float>::denorm_min(),
        .1f, .5f, std::nextafter(1.f, 0.f), 1.f};
    for (const auto base : positions) {
        for (const auto translation : positions) {
            rm::Motion m;
            m.mechanism = rm::Mechanism::Track;
            m.base = base;
            m.translation = translation;
            const auto bound = rm::BasePositionErrorBound(m);
            ASSERT_TRUE(bound);
            EXPECT_TRUE(std::isfinite(*bound));
            for (const float t : progress) {
                const auto actual = rm::BaseAt(m, t);
                ASSERT_TRUE(actual);
                const long double q = 1.L - static_cast<long double>(t);
                const long double x = 82.L * (base.u + .5L * base.v) -
                    82.L * (translation.u + .5L * translation.v) * q;
                const long double y = 71.L * base.v - 71.L * translation.v * q;
                EXPECT_LE(std::abs(actual->x - x) + std::abs(actual->y - y), *bound);
            }
        }
    }
    rm::Motion m;
    EXPECT_EQ(rm::BasePositionErrorBound(m), 0.);
    m.base = {limit - 1, limit};
    EXPECT_GT(*rm::BasePositionErrorBound(m), 0.);
    ++m.base.v;
    EXPECT_FALSE(rm::BasePositionErrorBound(m));
}

TEST(ReferenceMotion, TrackAtomBoundCoversWholeExpression) {
    constexpr int limit = 1 << 20;
    const rm::Hex positions[] = {{0, 0}, {limit, limit}, {-limit, limit},
                                 {limit - 1, -limit + 3}, {1, -1}};
    const float progress[] = {0.f, std::numeric_limits<float>::denorm_min(),
        .1f, .5f, std::nextafter(1.f, 0.f), 1.f};
    for (const auto position : positions) {
        for (const auto translation : positions) {
            rm::Motion m;
            m.mechanism = rm::Mechanism::Track;
            m.base = position;
            m.translation = translation;
            m.grabberOffset = translation;
            m.atom = position;
            m.absoluteGrabPosition = {-position.u, -position.v};
            const auto bound = rm::TrackAtomPositionErrorBound(m);
            ASSERT_TRUE(bound);
            EXPECT_TRUE(std::isfinite(*bound));
            for (const auto t : progress) {
                const auto actual = rm::AtomAt(m, t);
                ASSERT_TRUE(actual);
                const long double u = 3.L * position.u + translation.u *
                    static_cast<long double>(t);
                const long double v = 3.L * position.v + translation.v *
                    static_cast<long double>(t);
                EXPECT_LE(std::abs(actual->x - (82.L * u + 41.L * v)) +
                          std::abs(actual->y - 71.L * v), *bound);
            }
        }
    }
    rm::Motion m;
    EXPECT_FALSE(rm::TrackAtomPositionErrorBound(m));
    m.mechanism = rm::Mechanism::Track;
    m.baseRotation = 1;
    EXPECT_FALSE(rm::TrackAtomPositionErrorBound(m));
    m.baseRotation = 0;
    m.isPiston = true;
    EXPECT_FALSE(rm::TrackAtomPositionErrorBound(m));
    m.isPiston = false;
    m.rotation = 1;
    EXPECT_FALSE(rm::TrackAtomPositionErrorBound(m));
    m.rotation = 0;
    m.pistonExtension = 1;
    EXPECT_FALSE(rm::TrackAtomPositionErrorBound(m));
    m.pistonExtension = 0;
    for (auto field : {&rm::Motion::base, &rm::Motion::translation,
                       &rm::Motion::grabberOffset, &rm::Motion::atom,
                       &rm::Motion::absoluteGrabPosition}) {
        for (int outside : {-limit - 1, limit + 1}) {
            m.*field = {outside, 0};
            EXPECT_FALSE(rm::TrackAtomPositionErrorBound(m));
            m.*field = {0, outside};
            EXPECT_FALSE(rm::TrackAtomPositionErrorBound(m));
            m.*field = {};
        }
    }
    EXPECT_TRUE(rm::TrackAtomPositionErrorBound(m));
}

TEST(ReferenceMotion, TrackAtomBoundCoversIndependentFields) {
    constexpr int limit = 1 << 20;
    const rm::Hex positions[] = {{0, 0}, {limit, -limit}, {-limit, limit},
                                 {limit - 1, limit - 3}, {-limit + 3, -limit + 1}};
    constexpr int combinations = 5 * 5 * 5 * 5 * 5;
    for (int combination = 0; combination < combinations; ++combination) {
        rm::Motion m;
        m.mechanism = rm::Mechanism::Track;
        int remaining = combination;
        for (auto field : {&rm::Motion::base, &rm::Motion::translation,
                           &rm::Motion::grabberOffset, &rm::Motion::atom,
                           &rm::Motion::absoluteGrabPosition}) {
            m.*field = positions[remaining % 5];
            remaining /= 5;
        }
        const auto bound = rm::TrackAtomPositionErrorBound(m);
        ASSERT_TRUE(bound);
        for (float t : {0.f, .125f, .5f, .875f, 1.f}) {
            const auto actual = rm::AtomAt(m, t);
            ASSERT_TRUE(actual);
            const long double u = static_cast<long double>(m.base.u) +
                m.grabberOffset.u + m.atom.u - m.absoluteGrabPosition.u -
                m.translation.u * (1.L - t);
            const long double v = static_cast<long double>(m.base.v) +
                m.grabberOffset.v + m.atom.v - m.absoluteGrabPosition.v -
                m.translation.v * (1.L - t);
            EXPECT_LE(std::abs(actual->x - (82.L * u + 41.L * v)) +
                      std::abs(actual->y - 71.L * v), *bound)
                << "combination=" << combination << ", t=" << t;
        }
    }
}

TEST(ReferenceMotion, TrackFieldCancellationExceedsEndpointScaleUncertainty) {
    RestoreRounding restore;
    ASSERT_EQ(std::fesetround(FE_TONEAREST), 0);
    ASSERT_EQ(FLT_EVAL_METHOD, 0);
    constexpr int limit = 1 << 20;
    constexpr float t = .5f;
    rm::Motion m;
    m.mechanism = rm::Mechanism::Track;
    m.base = {limit - 1, 0};
    m.translation = {1, 0};
    m.grabberOffset = {1, 0};
    m.absoluteGrabPosition = {limit, 0};
    ASSERT_FALSE(m.isPiston);
    ASSERT_EQ(m.baseRotation, 0);
    ASSERT_EQ(m.rotation, 0);
    ASSERT_EQ(m.pistonExtension, 0);
    for (auto h : {m.base, m.translation, m.grabberOffset, m.atom,
                   m.absoluteGrabPosition}) {
        EXPECT_GE(h.u, -limit);
        EXPECT_LE(h.u, limit);
        EXPECT_EQ(h.v, 0);
    }
    EXPECT_EQ(m.base.u + m.grabberOffset.u, m.absoluteGrabPosition.u);
    EXPECT_EQ(m.base.v + m.grabberOffset.v, m.absoluteGrabPosition.v);

    const auto baseFinal = rm::BaseAt(m, 1.f);
    const auto base = rm::BaseAt(m, t);
    const auto actual = rm::AtomAt(m, t);
    const auto baseBound = rm::BasePositionErrorBound(m);
    const auto atomBound = rm::TrackAtomPositionErrorBound(m);
    ASSERT_TRUE(baseFinal);
    ASSERT_TRUE(base);
    ASSERT_TRUE(actual);
    ASSERT_TRUE(baseBound);
    ASSERT_TRUE(atomBound);
    EXPECT_EQ(baseFinal->x, 85983152.f);
    EXPECT_EQ(base->x, 85983112.f);
    EXPECT_EQ(base->y, 0.f);
    EXPECT_EQ(actual->x, -40.f);
    EXPECT_EQ(actual->y, 0.f);
    auto withoutOffset = m;
    withoutOffset.atom = m.absoluteGrabPosition;
    const auto grabber = rm::AtomAt(withoutOffset, t);
    ASSERT_TRUE(grabber);
    EXPECT_EQ(grabber->x, 85983192.f);

    const opusoptima::core::HexCoord position{-1, 0}, center{0, 0}, translation{1, 0};
    constexpr int turns = 0;
    const auto pixelX = [](int q, int r) { return 82. * q + 41. * r; };
    const double b = pixelX(m.base.u, m.base.v);
    const double g = pixelX(m.grabberOffset.u, m.grabberOffset.v);
    const double o = pixelX(m.atom.u - m.absoluteGrabPosition.u,
                            m.atom.v - m.absoluteGrabPosition.v);
    const double p = pixelX(position.q, position.r);
    const double d = pixelX(translation.q, translation.r);
    EXPECT_EQ(o, -85983232.);
    EXPECT_EQ(d, pixelX(m.translation.u, m.translation.v));
    EXPECT_EQ(p, b + g + o - d); // P=B+G+O-T; D=T, not an inferred API mapping.
    EXPECT_EQ(position.r, m.base.v + m.grabberOffset.v + m.atom.v -
                          m.absoluteGrabPosition.v - m.translation.v);
    EXPECT_EQ(translation.r, m.translation.v);
    EXPECT_EQ(p + d, 0.);
    // Diagnostic specialization of EndMotions' current zero-angle formula only;
    // no private RasterPoint center/U observation or cell/area-error assertion.
    const double nominal = p + d * t;
    const double uncertainty = .05 + 16. * std::numeric_limits<float>::epsilon() *
        (std::abs(p) + std::abs(d)); // C=0, turns=0: no maximumRadius term.
    EXPECT_EQ(nominal, -41.);
    EXPECT_DOUBLE_EQ(uncertainty, 0.050312805175781253);
    const double error = std::abs(static_cast<double>(actual->x) - nominal);
    EXPECT_EQ(error, 1.);
    EXPECT_GT(error, uncertainty);
    EXPECT_LE(std::abs(base->x - (b - d * (1. - t))), *baseBound);
    const double scale = std::abs(b) + std::abs(d) + std::abs(g) + std::abs(o);
    EXPECT_EQ(scale, 171966546.);
    EXPECT_GT(scale, std::abs(p) + std::abs(d));
    EXPECT_GE(*atomBound, 32. * 0x1p-24 * scale);
    EXPECT_LE(error, *atomBound);
    EXPECT_TRUE(std::isfinite(*atomBound));

    opusoptima::verification::IndependentMetrics metrics;
    opusoptima::io::PuzzleData puzzle;
    puzzle.version = 3;
    ASSERT_NO_THROW(metrics.Initialize(puzzle, {}));
    ASSERT_NO_THROW(metrics.BeginMotions());
    ASSERT_NO_THROW(metrics.ObserveAtomMotion(position, center, translation, turns));
    ASSERT_NO_THROW(metrics.EndMotions());
    EXPECT_EQ(metrics.WorkStats().rasterPoints, 7U); // t=4/8 is an interior sample.
    EXPECT_FALSE(metrics.CertificationEligible());
}

TEST(ReferenceMotion, TrackBoundsComposeWithContinuousSeparation) {
    namespace cc = opusoptima::verification::continuous;
    const auto ideal = [](const rm::Motion& m) {
        const double u = static_cast<double>(m.base.u) + m.grabberOffset.u +
                         m.atom.u - m.absoluteGrabPosition.u - m.translation.u;
        const double v = static_cast<double>(m.base.v) + m.grabberOffset.v +
                         m.atom.v - m.absoluteGrabPosition.v - m.translation.v;
        return cc::Trajectory{{82. * u + 41. * v, 71. * v}, {},
                              {82. * m.translation.u + 41. * m.translation.v,
                               71. * m.translation.v}, 0.};
    };
    rm::Motion a;
    a.mechanism = rm::Mechanism::Track;
    a.base = {1 << 20, -(1 << 20)};
    a.translation = {2, 0};
    a.grabberOffset = {1, 0};
    a.atom = {2, 0};
    a.absoluteGrabPosition = {1, 0};
    rm::Motion b = a;
    const auto errorA = rm::TrackAtomPositionErrorBound(a);
    ASSERT_TRUE(errorA);
    const auto check = [&](double distance) {
        const auto errorB = rm::TrackAtomPositionErrorBound(b);
        EXPECT_TRUE(errorB);
        if (!errorB) return cc::Separation::Uncertain;
        return cc::CheckBoundedSeparation(ideal(a), *errorA, ideal(b), *errorB, distance);
    };

    b.base.v += 10;
    EXPECT_EQ(check(58.), cc::Separation::Separated);

    b = a;
    b.base.u -= 1;
    EXPECT_EQ(cc::CheckSeparation(ideal(a), ideal(b), 58.), cc::Separation::Separated);
    EXPECT_EQ(check(58.), cc::Separation::Uncertain);
    EXPECT_EQ(check(82.), cc::Separation::Uncertain);

    b = a;
    b.translation = {-2, 0};
    EXPECT_EQ(check(58.), cc::Separation::Uncertain);
    for (float t : {0.f, .5f, 1.f}) {
        const auto pointA = rm::AtomAt(a, t);
        const auto pointB = rm::AtomAt(b, t);
        ASSERT_TRUE(pointA);
        ASSERT_TRUE(pointB);
        if (t == 1.f) {
            EXPECT_EQ(pointA->x, pointB->x);
        }
    }
}

TEST(ReferenceMotion, InvalidInputsFailClosed) {
    rm::Motion m;
    EXPECT_FALSE(rm::AtomAt(m, std::numeric_limits<float>::quiet_NaN()));
    EXPECT_FALSE(rm::AtomAt(m, -1.f));
    EXPECT_FALSE(rm::AtomAt(m, 2.f));
    m.rotation = std::numeric_limits<int>::max();
    EXPECT_FALSE(rm::AtomAt(m, .5f));
    m.rotation = 0;
    m.atom.u = std::numeric_limits<int>::min();
    EXPECT_FALSE(rm::AtomAt(m, .5f));
    m.atom = {};
    m.mechanism = rm::Mechanism::Piston;
    m.isPiston = true;
    EXPECT_FALSE(rm::AtomAt(m, .5f));
}
