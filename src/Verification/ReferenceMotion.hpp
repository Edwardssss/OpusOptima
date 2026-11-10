#pragma once

#include <cstddef>
#include <optional>

namespace opusoptima::verification::reference_motion {

struct Hex {
    int u = 0;
    int v = 0;
};
struct Point {
    float x = 0;
    float y = 0;
};
enum class Mechanism { Swing, Pivot, Track, Piston };

// All positions and orientations describe the integer state after the motion.
struct Motion {
    Mechanism mechanism = Mechanism::Pivot;
    Hex base;
    Hex grabberOffset;
    Hex absoluteGrabPosition;
    Hex atom;
    Hex translation;
    int baseRotation = 0;
    int rotation = 0;
    bool isPiston = false;
    int pistonExtension = 0;
};

// Diagnostic reference model only: neither a continuous enclosure nor game certification.
// All queries reject unsupported floating-point environments: fast-math,
// non-IEEE binary32, non-nearest rounding, and SSE DAZ/FTZ. Platform libm
// equivalence remains a separate qualification obligation.
std::optional<Point> AtomAt(const Motion& motion, float progress);
std::optional<Point> BaseAt(const Motion& motion, float progress);
// Uniform Euclidean error bound against exact 82x71 base translation for every
// binary32 progress in [0,1]. Covers BaseAt only, not AtomAt or game motion.
std::optional<double> BasePositionErrorBound(const Motion& motion);
// Uniform bound for unrotated, non-piston Track atoms against exact 82x71
// translation. Rejects other mechanisms/orientations; not game certification.
std::optional<double> TrackAtomPositionErrorBound(const Motion& motion);
std::optional<Hex> SnapToHex(Point point);
// Seven board candidates visited by add_collider, filtered by its strict radius test.
// This is the sampled reference raster, not a swept-area enclosure.
std::optional<std::size_t> BoardAreaCells(Point center, float radius,
                                         Hex (&cells)[7]);
std::optional<float> CollisionIncrement(int maximumRotationDistance);
std::optional<std::size_t> InteriorSampleCount(int maximumRotationDistance,
                                               std::size_t limit = 65536);

} // namespace opusoptima::verification::reference_motion
