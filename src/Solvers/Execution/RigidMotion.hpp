#pragma once

#include "BinaryParser.hpp"
#include <functional>
#include <unordered_set>

namespace opusoptima::solvers {
struct RigidPose {
    core::HexCoord anchor{};
    int rotation{};
    friend bool operator==(const RigidPose&, const RigidPose&) = default;
};
enum class RigidMove {
    Translate0,
    Translate1,
    Translate2,
    Translate3,
    Translate4,
    Translate5,
    Clockwise,
    CounterClockwise
};
enum class MotionStatus { Found, Unsatisfiable, Exhausted };
struct RigidMotionProblem {
    std::vector<core::HexCoord> fragment;
    RigidPose start, goal;
    std::unordered_set<core::HexCoord, core::HexCoordHash> allowed, blocked;
    std::size_t maxStates{20000};
    // Called for each edge: the mechanical controller must certify reachability
    // and rotational swept volumes. Endpoint occupancy alone is not sufficient.
    std::function<bool(RigidPose, RigidPose, RigidMove)> executable;
};
struct RigidMotionResult {
    MotionStatus status{MotionStatus::Unsatisfiable};
    std::vector<RigidPose> poses;
    std::vector<RigidMove> moves;
    std::size_t explored{};
};
// Finite-domain rigid-fragment routing. This is not an arm instruction compiler.
[[nodiscard]] RigidMotionResult RouteRigidFragment(const RigidMotionProblem&);
} // namespace opusoptima::solvers
