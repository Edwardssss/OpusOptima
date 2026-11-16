#pragma once

#include <chrono>

#include "RigidMotion.hpp"

namespace opusoptima::solvers {
struct HeldArmState {
    std::size_t trackIndex{};
    // Serialized part orientation: increasing turns map (1,0) to (0,1).
    // Tape R/P decrease these turns; r/p increase them.
    int length{1}, direction{}, fragmentRotation{};
    friend auto operator<=>(const HeldArmState&, const HeldArmState&) = default;
};
struct HeldArmMotionProblem {
    // Ordered, adjacent cells of one non-looping track; one cell means no track.
    std::vector<core::HexCoord> track;
    // Coordinates relative to the grabbed atom, including (0,0).
    std::vector<core::HexCoord> fragment;
    HeldArmState start;
    RigidPose goal;
    int minLength{1}, maxLength{1};
    bool pivot{true};
    // Like RouteRigidFragment, a zero budget returns Exhausted. The optional
    // wall-clock deadline stops a certificate-heavy route from running forever.
    std::size_t maxStates{20000};
    std::chrono::steady_clock::time_point deadline{std::chrono::steady_clock::time_point::max()};
    // Mandatory physical certificate for the ENTIRE instruction interval,
    // including arm bodies, held atoms, other material and rotation sweeps.
    std::function<bool(const HeldArmState&, const HeldArmState&, char)> executable;
};
struct EmptyArmMotionProblem {
    std::vector<core::HexCoord> track;
    HeldArmState start, goal;
    int minLength{1}, maxLength{1};
    std::size_t maxStates{20000};
    std::chrono::steady_clock::time_point deadline{std::chrono::steady_clock::time_point::max()};
    // Certifies the moving empty arm against parked material and machine parts.
    std::function<bool(const HeldArmState&, const HeldArmState&, char)> executable;
};
struct HeldArmMotionResult {
    MotionStatus status{MotionStatus::Unsatisfiable};
    std::vector<HeldArmState> states;
    std::vector<char> instructions;
    std::size_t explored{};
};
[[nodiscard]] RigidPose HeldFragmentPose(const HeldArmMotionProblem&, const HeldArmState&);
// Searches actual single-gripper arm instructions, not free fragment translations.
// Does not infer collision safety from endpoint occupancy or replace libverify.
[[nodiscard]] HeldArmMotionResult RouteHeldArm(const HeldArmMotionProblem&);
[[nodiscard]] HeldArmMotionResult RouteEmptyArm(const EmptyArmMotionProblem&);
} // namespace opusoptima::solvers
