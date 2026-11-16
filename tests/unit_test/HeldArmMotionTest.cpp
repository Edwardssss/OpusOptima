#include "HeldArmMotion.hpp"
#include <gtest/gtest.h>

#include <chrono>

using namespace opusoptima;
using namespace solvers;
namespace {
HeldArmMotionProblem Corridor() {
    HeldArmMotionProblem p;
    p.track = {{0, 0}, {1, 0}, {2, 0}};
    p.fragment = {{0, 0}, {1, 0}, {1, -1}};
    p.goal = {{3, 0}, 0};
    p.executable = [](auto, auto, char op) { return op == 'A' || op == 'a'; };
    return p;
}
} // namespace
TEST(HeldArmMotion, EmitsTrackInstructionsForWholeHeldFragment) {
    const auto p = Corridor();
    const auto result = RouteHeldArm(p);
    ASSERT_EQ(result.status, MotionStatus::Found);
    EXPECT_EQ(result.instructions, (std::vector<char>{'A', 'A'}));
    EXPECT_EQ(HeldFragmentPose(p, result.states.back()), p.goal);
}
TEST(HeldArmMotion, ExpiredDeadlineReturnsExhaustedWithoutSearching) {
    auto p = Corridor();
    p.deadline = std::chrono::steady_clock::now() - std::chrono::milliseconds(1);
    const auto result = RouteHeldArm(p);
    EXPECT_EQ(result.status, MotionStatus::Exhausted);
    EXPECT_EQ(result.explored, 0U);
}

TEST(HeldArmMotion, CannotTranslateWithoutTrackOrExtension) {
    auto p = Corridor();
    p.track.resize(1);
    EXPECT_EQ(RouteHeldArm(p).status, MotionStatus::Unsatisfiable);
}
TEST(HeldArmMotion, RotatingArmMovesAnchorAndOrientationTogether) {
    auto p = Corridor();
    p.track.resize(1);
    p.goal = {{1, -1}, 5};
    p.executable = [](auto, auto, char op) { return op == 'R'; };
    const auto result = RouteHeldArm(p);
    ASSERT_EQ(result.status, MotionStatus::Found);
    EXPECT_EQ(result.instructions, (std::vector<char>{'R'}));
}
TEST(HeldArmMotion, PivotChangesOnlyFragmentOrientation) {
    auto p = Corridor();
    p.goal = {{1, 0}, 5};
    p.executable = [](auto, auto, char op) { return op == 'P'; };
    EXPECT_EQ(RouteHeldArm(p).instructions, (std::vector<char>{'P'}));
    p.pivot = false;
    EXPECT_EQ(RouteHeldArm(p).status, MotionStatus::Unsatisfiable);
}
TEST(HeldArmMotion, ExtensionChangesReachNotOrientation) {
    auto p = Corridor();
    p.track.resize(1);
    p.maxLength = 3;
    p.goal = {{3, 0}, 0};
    p.executable = [](auto, auto, char op) { return op == 'E'; };
    EXPECT_EQ(RouteHeldArm(p).instructions, (std::vector<char>{'E', 'E'}));
}
TEST(HeldArmMotion, RequiresFullIntervalPhysicalCertificate) {
    auto p = Corridor();
    p.executable = {};
    EXPECT_THROW(static_cast<void>(RouteHeldArm(p)), std::invalid_argument);
    p.executable = [](auto, auto, char) { return false; };
    EXPECT_EQ(RouteHeldArm(p).status, MotionStatus::Unsatisfiable);
}
TEST(HeldArmMotion, PreservesQueuedGoalAtBudgetBoundary) {
    auto p = Corridor();
    p.maxStates = 3;
    EXPECT_EQ(RouteHeldArm(p).status, MotionStatus::Found);
    p.maxStates = 2;
    EXPECT_EQ(RouteHeldArm(p).status, MotionStatus::Exhausted);
    p.maxStates = 0;
    EXPECT_EQ(RouteHeldArm(p).status, MotionStatus::Exhausted);
}
TEST(HeldArmMotion, RejectsMalformedTrackAndFragment) {
    auto p = Corridor();
    p.track.back() = {5, 0};
    EXPECT_THROW(static_cast<void>(RouteHeldArm(p)), std::invalid_argument);
    p = Corridor();
    p.fragment = {{1, 0}};
    EXPECT_THROW(static_cast<void>(RouteHeldArm(p)), std::invalid_argument);
    p = Corridor();
    p.fragment.push_back({0, 0});
    EXPECT_THROW(static_cast<void>(RouteHeldArm(p)), std::invalid_argument);
}
TEST(HeldArmMotion, EmptyArmRoutesExactConfigurationWithoutPivot) {
    EmptyArmMotionProblem p;
    p.track = {{0, 0}, {1, 0}, {2, 0}};
    p.start = {2, 1, 4, 3};
    p.goal = {0, 2, 3, 5};
    p.maxLength = 2;
    p.executable = [](auto, auto, char op) { return op != 'P' && op != 'p'; };
    const auto result = RouteEmptyArm(p);
    ASSERT_EQ(result.status, MotionStatus::Found);
    EXPECT_EQ(result.states.back().trackIndex, 0U);
    EXPECT_EQ(result.states.back().length, 2);
    EXPECT_EQ(result.states.back().direction, 3);
    for (const auto& state : result.states)
        EXPECT_EQ(state.fragmentRotation, 0);
}
TEST(HeldArmMotion, EmptyArmRequiresCertificateAndReportsBudget) {
    EmptyArmMotionProblem p;
    p.track = {{0, 0}, {1, 0}};
    p.goal.trackIndex = 1;
    EXPECT_THROW(static_cast<void>(RouteEmptyArm(p)), std::invalid_argument);
    p.executable = [](auto, auto, char) { return false; };
    EXPECT_EQ(RouteEmptyArm(p).status, MotionStatus::Unsatisfiable);
    p.executable = [](auto, auto, char op) { return op == 'A'; };
    p.maxStates = 1;
    EXPECT_EQ(RouteEmptyArm(p).status, MotionStatus::Exhausted);
    p.maxStates = 2;
    EXPECT_EQ(RouteEmptyArm(p).status, MotionStatus::Found);
    p.goal.trackIndex = 2;
    EXPECT_THROW(static_cast<void>(RouteEmptyArm(p)), std::invalid_argument);
}
