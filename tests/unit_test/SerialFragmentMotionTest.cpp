#include "SerialFragmentMotion.hpp"
#include "FixedArmIntervals.hpp"
#include <gtest/gtest.h>

namespace opusoptima::solvers {
TEST(SerialFragmentMotion, RoutesInheritedDimerWithRealGripperAndRotation) {
    SerialFragmentMotionProblem p;
    p.anchor = 1;
    p.destination = {0, 1};
    p.goalRotations = {1};
    p.armBases = {{0, 0}};
    p.material.atoms = {{1, {core::AtomType::Lead, {1, 0}}},
                        {2, {core::AtomType::Quicksilver, {1, -1}}}};
    p.material.bonds[ExecutionIR::Edge{1, 2}] = 14;
    p.executable = [](const auto& state, auto base, int direction, char instruction) {
        const int turn = instruction == 'r' ? 1 : 5;
        return CertifyFixedArmInterval(state, base, {}, {0, 1, direction, 0},
                                       {0, 1, (direction + turn) % 6, turn}, instruction, true);
    };
    const auto found = RouteSerialFragment(p);
    ASSERT_EQ(found.status, MotionStatus::Found);
    ASSERT_EQ(found.moves.size(), 1U);
    EXPECT_EQ(found.moves[0].base, (core::HexCoord{0, 0}));
    EXPECT_EQ(found.moves[0].instruction, 'r');
    p.maxStates = 0;
    EXPECT_EQ(RouteSerialFragment(p).status, MotionStatus::Exhausted);
    p.maxStates = 32;
    p.executable = [](const auto&, auto, int, char) { return false; };
    EXPECT_EQ(RouteSerialFragment(p).status, MotionStatus::Unsatisfiable);
    p.executable = [](const auto&, auto, int, char) -> bool {
        throw std::runtime_error("certificate failed");
    };
    EXPECT_THROW((void)RouteSerialFragment(p), std::runtime_error);
}
TEST(SerialFragmentMotion, PivotIsAnExplicitImplementationCapability) {
    SerialFragmentMotionProblem p;
    p.anchor = 1;
    p.destination = {1, 0};
    p.goalRotations = {1};
    p.armBases = {{0, 0}};
    p.material.atoms = {{1, {core::AtomType::Lead, {1, 0}}},
                        {2, {core::AtomType::Quicksilver, {2, 0}}}};
    p.material.bonds[{1, 2}] = 14;
    p.executable = [](const auto& state, auto base, int direction, char instruction) {
        if (instruction != 'p') return false;
        return CertifyFixedArmInterval(state, base, {}, {0, 1, direction, 0}, {0, 1, direction, 1},
                                       instruction, true);
    };
    EXPECT_EQ(RouteSerialFragment(p).status, MotionStatus::Unsatisfiable);
    p.pivot = true;
    const auto result = RouteSerialFragment(p);
    ASSERT_EQ(result.status, MotionStatus::Found);
    ASSERT_EQ(result.moves.size(), 1U);
    EXPECT_EQ(result.moves.front().instruction, 'p');
    EXPECT_EQ(result.closestDistance, 0);
}
} // namespace opusoptima::solvers
