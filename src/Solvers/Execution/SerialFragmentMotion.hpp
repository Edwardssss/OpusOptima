#pragma once

#include "ExecutionIR.hpp"
#include "RigidMotion.hpp"
#include <functional>

namespace opusoptima::solvers {
struct SerialFragmentMotionProblem {
    ExecutionIR::State material;
    ExecutionIR::AtomId anchor{};
    core::HexCoord destination{};
    // Allowed rotations relative to the supplied material frame.
    std::vector<int> goalRotations;
    std::vector<core::HexCoord> armBases;
    std::size_t maxStates{20000};
    bool pivot{}; // The caller must check the puzzle's pivot permission.
    // Complete interval, including the other arms and renewable port footprints.
    std::function<bool(const ExecutionIR::State&, core::HexCoord, int, char)> executable;
};
struct SerialFragmentMove {
    core::HexCoord base{};
    ExecutionIR::AtomId grip{};
    int direction{};
    char instruction{};
};
struct SerialFragmentMotionResult {
    MotionStatus status{MotionStatus::Unsatisfiable};
    std::vector<SerialFragmentMove> moves;
    std::size_t explored{};
    int closestDistance{};
};
[[nodiscard]] SerialFragmentMotionResult RouteSerialFragment(const SerialFragmentMotionProblem&);
} // namespace opusoptima::solvers
