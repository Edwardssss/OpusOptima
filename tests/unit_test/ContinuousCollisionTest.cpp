#include "Verification/ContinuousCollision.hpp"

#include <array>
#include <cmath>
#include <cfenv>
#include <utility>
#include <gtest/gtest.h>
#include <limits>
#if defined(__GLIBC__) && (defined(__i386__) || defined(__x86_64__))
#include <fpu_control.h>
#endif
#if defined(__SSE__)
#include <xmmintrin.h>
#endif

namespace {
using namespace opusoptima::verification::continuous;
Trajectory Fixed(Point point) {
    return {point, {}, {}, 0};
}
Trajectory Line(Point start, Point delta) {
    return {start, {}, delta, 0};
}
TEST(ContinuousCollision, EnclosureCoversTranslationAndRotationalExtrema) {
    const auto line = Enclose(Line({2, -3}, {4, -8}), .25, .75);
    ASSERT_TRUE(line);
    EXPECT_LE(line->minX, 3);
    EXPECT_GE(line->maxX, 5);
    EXPECT_LE(line->minY, -9);
    EXPECT_GE(line->maxY, -5);
    const Trajectory circle{{10, 0}, {}, {}, 8};
    const auto bounds = Enclose(circle, 0, 1);
    ASSERT_TRUE(bounds);
    EXPECT_LE(bounds->minX, -10);
    EXPECT_GE(bounds->maxX, 10);
    EXPECT_LE(bounds->minY, -10);
    EXPECT_GE(bounds->maxY, 10);
    const auto instant = Enclose(Fixed({2, -3}), .5, .5);
    ASSERT_TRUE(instant);
    EXPECT_LE(instant->minX, 2);
    EXPECT_GE(instant->maxX, 2);
    EXPECT_LE(instant->minY, -3);
    EXPECT_GE(instant->maxY, -3);
}
TEST(ContinuousCollision, EnclosureCoversAdjacentAndSubnormalTimeIntervals) {
    const Trajectory moving{{0, 0}, {}, {1, -1}, 0};
    const double smallest = std::numeric_limits<double>::denorm_min();
    const std::array<std::pair<double, double>, 5> intervals{{
        {0, smallest},
        {smallest, 2 * smallest},
        {.5, std::nextafter(.5, 1.)},
        {std::nextafter(1., 0.), 1.},
        {1., 1.},
    }};
    for (const auto& [begin, end] : intervals) {
        const auto bounds = Enclose(moving, begin, end);
        ASSERT_TRUE(bounds);
        EXPECT_LE(bounds->minX, static_cast<long double>(begin));
        EXPECT_GE(bounds->maxX, static_cast<long double>(end));
        EXPECT_LE(bounds->minY, -static_cast<long double>(end));
        EXPECT_GE(bounds->maxY, -static_cast<long double>(begin));
    }
}
TEST(ContinuousCollision, EnclosureRejectsInvalidInputsAndRoundingModes) {
    const auto fixed = Fixed({0, 0});
    EXPECT_FALSE(Enclose(fixed, -.1, 1));
    EXPECT_FALSE(Enclose(fixed, 0, 1.1));
    EXPECT_FALSE(Enclose(fixed, .7, .3));
    EXPECT_FALSE(Enclose(fixed, std::numeric_limits<double>::quiet_NaN(), 1));
    EXPECT_FALSE(Enclose(Fixed({std::numeric_limits<double>::infinity(), 0}), 0, 1));
    const int original = std::fegetround();
    for (const int mode : {FE_DOWNWARD, FE_UPWARD, FE_TOWARDZERO}) {
        const int changed = std::fesetround(mode);
        const auto enclosure = Enclose(fixed, 0, 1);
        const auto separation = CheckSeparation(fixed, Fixed({2, 0}), 0);
        const auto bounded = CheckBoundedSeparation(fixed, 0, Fixed({2, 0}), 0, 0);
        const int restored = std::fesetround(original);
        ASSERT_EQ(restored, 0);
        ASSERT_EQ(changed, 0);
        EXPECT_FALSE(enclosure) << mode;
        EXPECT_EQ(separation, Separation::Uncertain) << mode;
        EXPECT_EQ(bounded, Separation::Uncertain) << mode;
    }
    EXPECT_TRUE(Enclose(fixed, 0, 1));
#if defined(__SSE__)
    const unsigned control = _mm_getcsr();
    _mm_setcsr(control | (1U << 6) | (1U << 15));
    const bool rejectedUnderflow = !Enclose(fixed, 0, 1);
    _mm_setcsr(control);
    EXPECT_TRUE(rejectedUnderflow);
#endif
}
#if defined(__GLIBC__) && (defined(__i386__) || defined(__x86_64__))
TEST(ContinuousCollision, ReducedX87PrecisionCannotCertifySeparation) {
    if (std::numeric_limits<long double>::digits != 64) GTEST_SKIP();
    fpu_control_t original;
    _FPU_GETCW(original);
    for (const fpu_control_t precision : {_FPU_SINGLE, _FPU_DOUBLE}) {
        const fpu_control_t reduced = (original & ~_FPU_EXTENDED) | precision;
        _FPU_SETCW(reduced);
        const auto enclosure = Enclose(Fixed({0, 0}), 0, 1);
        const auto separation = CheckSeparation(Fixed({0, 0}), Fixed({2, 0}), 1);
        const auto bounded = CheckBoundedSeparation(Fixed({0, 0}), .01, Fixed({2, 0}), .01, 1);
        _FPU_SETCW(original);
        EXPECT_FALSE(enclosure) << precision;
        EXPECT_EQ(separation, Separation::Uncertain) << precision;
        EXPECT_EQ(bounded, Separation::Uncertain) << precision;
    }
    EXPECT_TRUE(Enclose(Fixed({0, 0}), 0, 1));
}
#endif
TEST(ContinuousCollision, WorkCountersIdentifyAnalyticAndBroadProofsWithoutSubdivision) {
    Work work;
    EXPECT_EQ(CheckSeparation(Fixed({0, 0}), Fixed({2, 0}), 1, {0, 0}, &work),
              Separation::Separated);
    EXPECT_EQ(work.calls, 1U);
    EXPECT_EQ(work.linear, 1U);
    EXPECT_EQ(work.rigid, 0U);
    EXPECT_EQ(work.broad, 0U);
    EXPECT_EQ(work.segments, 0U);
    EXPECT_EQ(work.maxDepth, 0U);
    EXPECT_EQ(work.uncertain, 0U);
    const Trajectory first{{1, 2}, {1, 1}, {3, -4}, 6};
    const Trajectory second{{3, 1}, {1, 1}, {3, -4}, 6};
    EXPECT_EQ(CheckSeparation(first, second, 2, {0, 0}, &work), Separation::Separated);
    EXPECT_EQ(work.rigid, 1U);
    const Trajectory circle{{1, 0}, {}, {}, 1};
    EXPECT_EQ(CheckSeparation(circle, Fixed({100, 100}), 1, {0, 0}, &work), Separation::Separated);
    EXPECT_EQ(work.calls, 3U);
    EXPECT_EQ(work.broad, 1U);
    EXPECT_EQ(work.segments, 0U);
    EXPECT_EQ(work.uncertain, 0U);
    auto invalid = circle;
    invalid.start.x = std::numeric_limits<double>::quiet_NaN();
    EXPECT_EQ(CheckSeparation(invalid, circle, 1, {}, &work), Separation::Uncertain);
    EXPECT_EQ(work.calls, 4U);
    EXPECT_EQ(work.uncertain, 1U);
}
TEST(ContinuousCollision,
     WorkCountersRecordEvaluatedSegmentsAndUncertaintyWithoutChangingVerdicts) {
    const Trajectory circle{{1, 0}, {}, {}, 1};
    Work work;
    const auto expected = CheckSeparation(circle, Fixed({0, 0}), 1, {128, 3});
    EXPECT_EQ(CheckSeparation(circle, Fixed({0, 0}), 1, {128, 3}, &work), expected);
    EXPECT_EQ(expected, Separation::Uncertain);
    EXPECT_EQ(work.calls, 1U);
    EXPECT_EQ(work.segments, 4U);
    EXPECT_EQ(work.maxDepth, 3U);
    EXPECT_EQ(work.uncertain, 1U);
    EXPECT_EQ(CheckSeparation(circle, Fixed({1.1, 0}), .2, {0, 40}, &work),
              CheckSeparation(circle, Fixed({1.1, 0}), .2, {0, 40}));
    EXPECT_EQ(work.calls, 2U);
    EXPECT_EQ(work.segments, 4U);
    EXPECT_EQ(work.maxDepth, 3U);
    EXPECT_EQ(work.uncertain, 2U);
    EXPECT_EQ(work.linear, 0U);
    EXPECT_EQ(work.rigid, 0U);
    EXPECT_EQ(work.broad, 0U);
}

TEST(ContinuousCollision, LinearMinimumDetectsBetweenSampleCollisions) {
    const auto moving = Line({-1, 0}, {128, 0});
    const auto fixed = Fixed({0, 0});
    for (int i = 0; i <= 64; ++i) {
        const auto point = PointAt(moving, i / 64.);
        EXPECT_GT(std::hypot(point.x, point.y), .1);
    }
    EXPECT_EQ(CheckSeparation(moving, fixed, .1), Separation::Collision);
    EXPECT_EQ(CheckSeparation(Line({-2, 2}, {4, 0}), Line({2, -2}, {-4, 0}), 1),
              Separation::Separated);
    EXPECT_EQ(CheckSeparation(Line({-2, 0}, {4, 0}), Line({2, 0}, {-4, 0}), .1),
              Separation::Collision);
}
TEST(ContinuousCollision, LinearContactAndUncertainDistancesAreNotAccepted) {
    EXPECT_EQ(CheckSeparation(Line({-1, 1}, {2, 0}), Fixed({0, 0}), 1), Separation::Uncertain);
    EXPECT_EQ(CheckSeparation(Fixed({0, 0}), Fixed({1, 0}), 1), Separation::Uncertain);
    EXPECT_EQ(CheckSeparation(Fixed({0, 0}), Fixed({1.001, 0}), 1), Separation::Separated);
    EXPECT_EQ(CheckSeparation(Fixed({0, 0}), Fixed({.999, 0}), 1), Separation::Collision);
    EXPECT_EQ(CheckSeparation(Line({0, 0}, {1, 0}), Line({0, 2}, {1, 0}), 1),
              Separation::Separated);
}
TEST(ContinuousCollision, CurvedMinimumDetectsCollisionMissingAllSixtyFourSamples) {
    const Trajectory moving{{10, 0}, {0, 0}, {}, 1};
    const auto fixed = Fixed({10 * std::cos(1. / 128), 10 * std::sin(1. / 128)});
    for (int i = 0; i <= 64; ++i) {
        const auto point = PointAt(moving, i / 64.);
        EXPECT_GT(std::hypot(point.x - fixed.start.x, point.y - fixed.start.y), .01);
    }
    EXPECT_EQ(CheckSeparation(moving, fixed, .01), Separation::Collision);
}
TEST(ContinuousCollision, SimultaneousArcsTranslationsAndPivotsUseTheWholeInterval) {
    const double halfTurn = std::acos(-1.);
    const Trajectory first{{1, 0}, {0, 0}, {}, halfTurn};
    const Trajectory second{{-1, 0}, {0, 0}, {}, -halfTurn};
    EXPECT_EQ(CheckSeparation(first, second, .1), Separation::Collision);
    EXPECT_EQ(CheckSeparation(first, Fixed({0, 0}), .5), Separation::Separated);
    const Trajectory pivot{{3, 0}, {2, 0}, {0, 1}, halfTurn};
    const auto halfway = PointAt(pivot, .5);
    EXPECT_NEAR(halfway.x, 2, 1.e-12);
    EXPECT_NEAR(halfway.y, 1.5, 1.e-12);
    EXPECT_EQ(CheckSeparation(pivot, Fixed(halfway), .1), Separation::Collision);
    EXPECT_EQ(CheckSeparation(pivot, Fixed({30, 30}), .1), Separation::Separated);
}
TEST(ContinuousCollision, IdenticalRigidMotionKeepsDistanceWithoutSubdivision) {
    const Trajectory a{{1, 2}, {1, 1}, {3, -4}, 6};
    const Trajectory b{{3, 1}, {1, 1}, {3, -4}, 6};
    EXPECT_EQ(CheckSeparation(a, b, 2, {0, 0}), Separation::Separated);
    EXPECT_EQ(CheckSeparation(a, b, 3, {0, 0}), Separation::Collision);
    const Trajectory c{{1, 1}, {1, 1}, {3, -4}, 6};
    EXPECT_EQ(CheckSeparation(a, c, 1, {0, 0}), Separation::Uncertain);
}
TEST(ContinuousCollision, GrazingAndExhaustedProofBudgetsStayUncertain) {
    const Trajectory circle{{1, 0}, {0, 0}, {}, 1};
    EXPECT_EQ(CheckSeparation(circle, Fixed({0, 0}), 1, {128, 12}), Separation::Uncertain);
    EXPECT_EQ(CheckSeparation(circle, Fixed({100, 100}), 1, {0, 40}), Separation::Separated);
    EXPECT_EQ(CheckSeparation(circle, Fixed({1.1, 0}), .2, {0, 40}), Separation::Uncertain);
    EXPECT_EQ(CheckSeparation(circle, Fixed({1.1, 0}), .2, {1, 0}), Separation::Uncertain);
    EXPECT_EQ(CheckSeparation(circle, Fixed({100, 100}), 1), Separation::Separated);
}
TEST(ContinuousCollision, NonfiniteUnboundedAndInvalidInputsCannotPass) {
    const auto stationary = Fixed({0, 0});
    auto invalid = stationary;
    invalid.start.x = std::numeric_limits<double>::quiet_NaN();
    EXPECT_EQ(CheckSeparation(invalid, stationary, 1), Separation::Uncertain);
    invalid = stationary;
    invalid.translation.y = std::numeric_limits<double>::infinity();
    EXPECT_EQ(CheckSeparation(invalid, stationary, 1), Separation::Uncertain);
    invalid = stationary;
    invalid.angle = 9;
    EXPECT_EQ(CheckSeparation(invalid, stationary, 1), Separation::Uncertain);
    invalid = stationary;
    invalid.center.x = 1.e10;
    EXPECT_EQ(CheckSeparation(invalid, stationary, 1), Separation::Uncertain);
    EXPECT_EQ(CheckSeparation(stationary, stationary, -1), Separation::Uncertain);
    EXPECT_EQ(CheckSeparation(stationary, stationary, std::numeric_limits<double>::infinity()),
              Separation::Uncertain);
    EXPECT_EQ(CheckSeparation(stationary, stationary, 1, {1048577, 40}), Separation::Uncertain);
    EXPECT_EQ(CheckSeparation(stationary, stationary, 1, {4096, 65}), Separation::Uncertain);
    EXPECT_TRUE(std::isnan(PointAt(invalid, .5).x));
    EXPECT_TRUE(std::isnan(PointAt(stationary, 2).y));
}
TEST(ContinuousCollision, NonDefaultRoundingCannotCertifySeparation) {
    struct RestoreRounding {
        int mode = std::fegetround();
        ~RestoreRounding() { std::fesetround(mode); }
    } restore;
    for (const int mode : {FE_DOWNWARD, FE_UPWARD, FE_TOWARDZERO}) {
        ASSERT_EQ(std::fesetround(mode), 0);
        Work work;
        EXPECT_EQ(CheckSeparation(Fixed({0, 0}), Fixed({2, 0}), 1, {}, &work),
                  Separation::Uncertain);
        EXPECT_EQ(work.uncertain, 1U);
        EXPECT_EQ(work.linear, 0U);
    }
}
#if defined(__SSE__)
TEST(ContinuousCollision, FlushAndDenormalsModesCannotCertifyZeroClearance) {
    struct RestoreControl {
        unsigned value = _mm_getcsr();
        ~RestoreControl() { _mm_setcsr(value); }
    } restore;
    for (const unsigned mode : {0x40U, 0x8000U, 0x8040U}) {
        _mm_setcsr(restore.value | mode);
        EXPECT_EQ(CheckSeparation(Fixed({0, 0}), Fixed({0, 0}),
                                  std::numeric_limits<double>::denorm_min()),
                  Separation::Uncertain);
        EXPECT_EQ(CheckSeparation(Fixed({0, 0}), Fixed({2, 0}), 1), Separation::Uncertain);
    }
}
#endif
TEST(ContinuousCollision, TinyPositiveClearanceAtCoincidentPositionsIsNotZero) {
    EXPECT_NE(CheckSeparation(Fixed({0, 0}), Fixed({0, 0}),
                              std::numeric_limits<double>::denorm_min()),
              Separation::Separated);
}
TEST(ContinuousCollision, MovingApartAndSmallClearancesCanBeProved) {
    EXPECT_EQ(CheckSeparation(Line({0, 0}, {-1, 0}), Fixed({1.000001, 0}), 1),
              Separation::Separated);
    EXPECT_EQ(CheckSeparation(Line({0, 0}, {1, 0}), Fixed({2.000001, 0}), 1),
              Separation::Separated);
    const Trajectory curved{{2, 0}, {}, {}, .1};
    EXPECT_EQ(CheckSeparation(curved, Fixed({0, 0}), 1.9), Separation::Separated);
    EXPECT_EQ(CheckSeparation(curved, Fixed({0, 0}), 2.1), Separation::Collision);
}
TEST(ContinuousCollision, CircularDistanceOracleCoversSignedAngleAndCoordinateLimits) {
    for (const double angle : {-8., -1., 1., 8.}) {
        for (const Point center : {Point{0, 0}, Point{999999968, -999999968}}) {
            SCOPED_TRACE(::testing::Message() << "angle=" << angle << " center=" << center.x);
            const Trajectory circle{{center.x + 32, center.y}, center, {}, angle};
            Work separated;
            EXPECT_EQ(CheckSeparation(circle, Fixed(center), 24, {}, &separated),
                      Separation::Separated);
            EXPECT_GT(separated.segments, 0U);
            EXPECT_EQ(separated.broad, 0U);
            EXPECT_EQ(CheckSeparation(circle, Fixed(center), 40), Separation::Collision);
            EXPECT_EQ(CheckSeparation(Fixed(center), circle, 24), Separation::Separated);
            EXPECT_EQ(CheckSeparation(circle, Fixed(center), 24, {0, 40}),
                      Separation::Uncertain);
        }
    }
}
TEST(ContinuousCollision, BoundedPositionErrorsUseBothBodiesAndNeverInventCollision) {
    const auto a = Fixed({0, 0});
    const auto b = Fixed({2, 0});
    EXPECT_EQ(CheckBoundedSeparation(a, .2, b, .3, 1), Separation::Separated);
    Work work;
    EXPECT_EQ(CheckBoundedSeparation(a, .6, b, .6, 1, {}, &work), Separation::Uncertain);
    EXPECT_EQ(work.calls, 1U);
    EXPECT_EQ(work.uncertain, 1U);
    EXPECT_EQ(CheckBoundedSeparation(a, .1, a, .1, 1), Separation::Uncertain);
    EXPECT_EQ(CheckBoundedSeparation(a, 0, a, 0, 1), Separation::Collision);
    EXPECT_EQ(CheckBoundedSeparation(a, 0, b, 0, 1), CheckSeparation(a, b, 1));
    EXPECT_EQ(CheckBoundedSeparation(a, .1, b, .1, 0), Separation::Separated);
    EXPECT_EQ(CheckBoundedSeparation(a, .1, a, .1, 0), Separation::Uncertain);
}
TEST(ContinuousCollision, BoundedPositionErrorsCoverCurvesAndBudgetFailure) {
    const Trajectory circle{{2, 0}, {}, {}, 1};
    const auto center = Fixed({0, 0});
    EXPECT_EQ(CheckBoundedSeparation(circle, .1, center, .1, 1.7), Separation::Separated);
    EXPECT_EQ(CheckBoundedSeparation(circle, .1, center, .1, 1.9), Separation::Uncertain);
    EXPECT_EQ(CheckBoundedSeparation(circle, .1, center, .1, 1.7, {0, 40}),
              Separation::Uncertain);
    EXPECT_EQ(CheckBoundedSeparation(center, .1, circle, .1, 1.7), Separation::Separated);
}
TEST(ContinuousCollision, BoundedPositionErrorsRejectInvalidAndUnrepresentableBounds) {
    const auto a = Fixed({0, 0});
    const auto b = Fixed({2, 0});
    for (const double error : {-1., std::numeric_limits<double>::infinity(),
                               std::numeric_limits<double>::quiet_NaN(),
                               std::numeric_limits<double>::max()}) {
        EXPECT_EQ(CheckBoundedSeparation(a, error, b, 0, 1), Separation::Uncertain);
        EXPECT_EQ(CheckBoundedSeparation(a, 0, b, error, 1), Separation::Uncertain);
    }
    for (const double distance : {-1., 1.e10, std::numeric_limits<double>::infinity(),
                                  std::numeric_limits<double>::quiet_NaN()})
        EXPECT_EQ(CheckBoundedSeparation(a, .1, b, .1, distance), Separation::Uncertain);
    auto invalid = a;
    invalid.angle = 9;
    EXPECT_EQ(CheckBoundedSeparation(invalid, .1, b, .1, 1), Separation::Uncertain);
    EXPECT_EQ(CheckBoundedSeparation(a, .1, b, .1, 1, {1048577, 40}), Separation::Uncertain);
    EXPECT_EQ(CheckBoundedSeparation(a, .1, b, .1, 1, {4096, 65}), Separation::Uncertain);
    EXPECT_EQ(CheckBoundedSeparation(a, std::numeric_limits<double>::denorm_min(),
                                     Fixed({1, 0}), 0, 1), Separation::Uncertain);
}
TEST(ContinuousCollision, BoundedPositionErrorsRejectUnsupportedArithmetic) {
    struct RestoreRounding {
        int mode = std::fegetround();
        ~RestoreRounding() { std::fesetround(mode); }
    } restore;
    for (const int mode : {FE_DOWNWARD, FE_UPWARD, FE_TOWARDZERO}) {
        ASSERT_EQ(std::fesetround(mode), 0);
        Work work;
        EXPECT_EQ(CheckBoundedSeparation(Fixed({0, 0}), .1, Fixed({2, 0}), .1, 1,
                                         {}, &work), Separation::Uncertain);
        EXPECT_EQ(work.calls, 1U);
        EXPECT_EQ(work.uncertain, 1U);
        EXPECT_EQ(work.linear, 0U);
    }
}
#if defined(__SSE__)
TEST(ContinuousCollision, BoundedPositionErrorsRejectFlushAndDenormalsModes) {
    struct RestoreControl {
        unsigned value = _mm_getcsr();
        ~RestoreControl() { _mm_setcsr(value); }
    } restore;
    for (const unsigned mode : {0x40U, 0x8000U, 0x8040U}) {
        _mm_setcsr(restore.value | mode);
        for (const double error : {0., .1, std::numeric_limits<double>::denorm_min()}) {
            Work work;
            EXPECT_EQ(CheckBoundedSeparation(Fixed({0, 0}), error, Fixed({2, 0}), error,
                                             1, {}, &work), Separation::Uncertain);
            EXPECT_EQ(work.calls, 1U);
            EXPECT_EQ(work.uncertain, 1U);
            EXPECT_EQ(work.linear, 0U);
            EXPECT_EQ(work.segments, 0U);
        }
    }
}
#endif
TEST(ContinuousCollision, SwappingBodiesDoesNotChangeAConclusiveResult) {
    const Trajectory a{{2, 0}, {0, 0}, {.2, 0}, 1};
    for (const auto b : {Fixed({8, 1}), Fixed({0, 0}), Fixed(PointAt(a, .25))}) {
        const auto forward = CheckSeparation(a, b, .4);
        EXPECT_NE(forward, Separation::Uncertain);
        EXPECT_EQ(CheckSeparation(b, a, .4), forward);
    }
}
} // namespace
