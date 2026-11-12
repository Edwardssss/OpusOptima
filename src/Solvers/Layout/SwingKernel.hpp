#pragma once

#include "HexGrid.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <unordered_set>
#include <vector>

namespace opusoptima::solvers::layout {

/// Shared `arm1` transport kernel.
///
/// The native gate indexes directions in `third_party/omsim/sim.c:
/// direction_for_offset` order (0:(1,0) 1:(0,1) 2:(-1,1) 3:(-1,0) 4:(0,-1)
/// 5:(1,-1)), which is NOT `core::kAxialDirections`. Every emitter that wants a
/// rotated arm to release an atom on the cell it expects must use this table, so
/// it lives here once instead of being copied per family.
///
/// An `arm1` of size L grabs only the six cells at hex distance L along an axis
/// (`pivot + L*step(d)`; `sim.c: record_swing_area` aborts above 3) and rotation
/// advances the held atom along that six-cell ring. So one swing carries an atom
/// from A to B exactly when `B - A == L*(step(drop) - step(grab))`.
inline constexpr std::array<core::HexCoord, 6> kSwingSteps = {
    core::HexCoord{1, 0}, core::HexCoord{0, 1}, core::HexCoord{-1, 1},
    core::HexCoord{-1, 0}, core::HexCoord{0, -1}, core::HexCoord{1, -1},
};

inline constexpr int kMaxArmSize = 3;

[[nodiscard]] inline core::HexCoord SwingStep(int direction) {
    const std::size_t index = static_cast<std::size_t>((direction % 6 + 6) % 6);
    return kSwingSteps[index];
}

[[nodiscard]] inline core::HexCoord SwingAdd(core::HexCoord a, core::HexCoord b) {
    return core::Add(a, b);
}

[[nodiscard]] inline core::HexCoord SwingScale(core::HexCoord a, int k) {
    return core::HexCoord{a.q * k, a.r * k};
}

/// Direction index of an adjacent offset, or -1.
[[nodiscard]] inline int SwingDirectionOf(core::HexCoord delta) {
    for (int direction = 0; direction < 6; ++direction) {
        const auto step = SwingStep(direction);
        if (delta.q == step.q && delta.r == step.r) return direction;
    }
    return -1;
}

/// Ring direction of `cell` seen from `pivot` at exactly `length`, or -1.
[[nodiscard]] inline int SwingRingDirection(core::HexCoord cell, core::HexCoord pivot,
                                            int length) {
    const auto delta = core::Subtract(cell, pivot);
    for (int direction = 0; direction < 6; ++direction) {
        const auto ring = SwingScale(SwingStep(direction), length);
        if (delta.q == ring.q && delta.r == ring.r) return direction;
    }
    return -1;
}

/// Signed rotation count from `grab` to `drop`; positive turns counter-clockwise.
[[nodiscard]] inline int SwingRotation(int grab, int drop) {
    const int forward = ((drop - grab) % 6 + 6) % 6;
    return forward <= 6 - forward ? forward : forward - 6;
}

struct Swing {
    core::HexCoord pivot{};
    core::HexCoord station{}; // where the atom is grabbed
    core::HexCoord target{};  // where it is dropped
    int length{kMaxArmSize};
    int grab{};
    int drop{};
};

/// Every swing that lands an atom on `target`, cheapest rotation first. The
/// caller decides whether the station cell is free to host a reagent.
[[nodiscard]] inline std::vector<Swing> SwingCandidates(core::HexCoord target) {
    // Face west first, then the neighbours (mirrors the hand-verified template).
    static constexpr std::array<int, 6> kDropOrder = {3, 2, 4, 1, 5, 0};
    static constexpr std::array<int, 5> kDeltaOrder = {1, -1, 2, -2, 3};
    std::vector<Swing> candidates;
    candidates.reserve(kDropOrder.size() * kDeltaOrder.size() * 3);
    for (const int delta : kDeltaOrder) {
        for (const int length : {kMaxArmSize, 2, 1}) {
            for (const int drop : kDropOrder) {
                const int grab = ((drop - delta) % 6 + 6) % 6;
                Swing swing;
                swing.pivot = core::Subtract(target, SwingScale(SwingStep(drop), length));
                swing.station = core::Add(swing.pivot, SwingScale(SwingStep(grab), length));
                swing.target = target;
                swing.length = length;
                swing.grab = grab;
                swing.drop = drop;
                candidates.push_back(swing);
            }
        }
    }
    return candidates;
}

/// Every cell the *body* of an `arm1` of size `length` can occupy while it turns
/// from `grab` to `drop`: at every radius 1..length the angular sweep of the arm
/// segment. Endpoints included, so it also covers the resting segment at `grab`
/// and the delivery segment at `drop`.
[[nodiscard]] inline std::vector<core::HexCoord> SwingSectorCells(int length, int grab, int drop) {
    const int rotation = SwingRotation(grab, drop);
    const auto angle = [](core::HexCoord delta) {
        const double x = 82.0 * (static_cast<double>(delta.q) + 0.5 * static_cast<double>(delta.r));
        const double y = 71.0 * static_cast<double>(delta.r);
        const double degrees = std::atan2(y, x) * (180.0 / 3.14159265358979323846);
        return std::fmod(degrees + 360.0, 360.0);
    };
    const double start = static_cast<double>(((grab % 6) + 6) % 6) * 60.0;
    const double span = static_cast<double>(rotation > 0 ? rotation : -rotation) * 60.0;
    // The pixel transform puts the six hex axes about 0.02 degrees off 60-degree
    // steps, so cells on a facing boundary are snapped before the span test.
    constexpr double kAngleTolerance = 0.5;
    std::vector<core::HexCoord> cells;
    for (int radius = 1; radius <= length; ++radius)
        for (int dq = -radius; dq <= radius; ++dq)
            for (int dr = -radius; dr <= radius; ++dr) {
                const core::HexCoord delta{dq, dr};
                if (core::Distance(delta, core::HexCoord{}) != radius) continue;
                const double phase =
                    rotation >= 0 ? angle(delta) - start : start - angle(delta);
                double relative = std::fmod(std::fmod(phase, 360.0) + 360.0, 360.0);
                // The pixel transform does not put the six hex axes exactly 60
                // degrees apart, so a cell on the start facing can land at 359.98.
                // Snap that back to zero before the span test.
                if (relative > 360.0 - kAngleTolerance) relative = 0.0;
                if (relative > span + kAngleTolerance) continue;
                cells.push_back(delta);
            }
    return cells;
}

/// Every cell an `arm1` body or its held atom can occupy over a full turn: the
/// disc of radius `length` around its pivot. Two arms whose discs are disjoint
/// can never collide with each other, which is the placement invariant the
/// architecture compiler maintains instead of tracing exact arcs.
[[nodiscard]] inline std::unordered_set<core::HexCoord, core::HexCoordHash> SwingDisc(
    core::HexCoord pivot, int length) {
    std::unordered_set<core::HexCoord, core::HexCoordHash> cells;
    for (int dq = -length; dq <= length; ++dq)
        for (int dr = -length; dr <= length; ++dr) {
            const core::HexCoord delta{dq, dr};
            if (core::Distance(delta, core::HexCoord{}) <= length)
                cells.insert(core::Add(pivot, delta));
        }
    return cells;
}

} // namespace opusoptima::solvers::layout
