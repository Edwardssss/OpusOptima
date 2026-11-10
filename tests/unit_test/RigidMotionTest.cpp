#include "RigidMotion.hpp"
#include <gtest/gtest.h>

using namespace opusoptima;
using namespace solvers;
namespace {
RigidMotionProblem Corridor() {
    RigidMotionProblem p;
    p.fragment = {{0, 0}, {1, 0}};
    p.start = {{0, 0}, 0};
    p.goal = {{3, 0}, 0};
    for (int q = 0; q <= 4; ++q)
        p.allowed.insert({q, 0});
    p.executable = [](auto, auto, auto move) { return static_cast<int>(move) < 6; };
    return p;
}
} // namespace
TEST(RigidMotion, RoutesWholeFragment) {
    const auto result = RouteRigidFragment(Corridor());
    ASSERT_EQ(result.status, MotionStatus::Found);
    EXPECT_EQ(result.moves.size(), 3);
    EXPECT_EQ(result.poses.back().anchor, (core::HexCoord{3, 0}));
}
TEST(RigidMotion, RejectsFragmentNotJustAnchorCollision) {
    auto p = Corridor();
    p.blocked.insert({4, 0});
    EXPECT_EQ(RouteRigidFragment(p).status, MotionStatus::Unsatisfiable);
}
TEST(RigidMotion, HonorsMechanicalEdgeRejection) {
    auto p = Corridor();
    p.executable = [](auto, auto, auto) { return false; };
    EXPECT_EQ(RouteRigidFragment(p).status, MotionStatus::Unsatisfiable);
}
TEST(RigidMotion, DistinguishesBudgetFromImpossibility) {
    auto p = Corridor();
    p.maxStates = 1;
    EXPECT_EQ(RouteRigidFragment(p).status, MotionStatus::Exhausted);
    p.maxStates = 0;
    EXPECT_EQ(RouteRigidFragment(p).status, MotionStatus::Exhausted);
}
TEST(RigidMotion, ChecksQueuedGoalBeforeReportingBudgetExhaustion) {
    auto p = Corridor();
    p.goal = {{1, 0}, 0};
    p.maxStates = 2;
    EXPECT_EQ(RouteRigidFragment(p).status, MotionStatus::Found);
}
TEST(RigidMotion, RequiresPhysicalValidator) {
    auto p = Corridor();
    p.executable = {};
    EXPECT_THROW(static_cast<void>(RouteRigidFragment(p)), std::invalid_argument);
}
TEST(RigidMotion, NormalizesOrientationAndRejectsDuplicateAtoms) {
    auto p = Corridor();
    p.start.rotation = -6;
    EXPECT_EQ(RouteRigidFragment(p).status, MotionStatus::Found);
    p.fragment.push_back({0, 0});
    EXPECT_THROW(static_cast<void>(RouteRigidFragment(p)), std::invalid_argument);
}
