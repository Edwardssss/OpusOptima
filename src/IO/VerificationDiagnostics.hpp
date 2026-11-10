#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace opusoptima::io {
enum class VerificationPhase : std::uint32_t {
    Setup,
    GrabDrop,
    Spawn,
    Glyphs,
    Outputs,
    Motions,
    Boundaries,
    Finish,
    Count
};
enum class VerificationCounter : std::uint32_t {
    AtomLookups,
    FragmentQueries,
    HeldQueries,
    FragmentEdgeVisits,
    CollisionPairs,
    KernelCalls,
    KernelLinear,
    KernelRigid,
    KernelBroad,
    KernelSegments,
    KernelMaxDepth,
    KernelUncertain,
    RasterPoints,
    RasterCandidates,
    MetricMarkedCells,
    MotionFrames,
    Count
};
inline constexpr std::size_t kVerificationTraceLimit = 64;
inline constexpr std::size_t kVerificationTraceAtoms = 8;
struct VerificationTrace {
    std::uint32_t kind{}, phase{}, object{}, atomCount{};
    int cycle{};
    std::array<std::uint64_t, kVerificationTraceAtoms> atoms{};
    bool atomsTruncated{};
    friend bool operator==(const VerificationTrace&, const VerificationTrace&) = default;
};
struct VerificationDiagnostics {
    VerificationPhase phase{VerificationPhase::Setup};
    int cycle{};
    std::uint64_t completedCycles{}, inventory{}, inventoryHighWater{}, elapsedMicros{},
        parseMicros{}, rasterMicros{}, collisionMicros{}, observeMicros{}, snapshotMicros{},
        fragmentMicros{}, heldMicros{};
    std::array<std::uint64_t, static_cast<std::size_t>(VerificationPhase::Count)> phaseMicros{};
    std::array<std::uint64_t, static_cast<std::size_t>(VerificationCounter::Count)> counters{};
    std::vector<std::uint64_t> outputs;
    std::vector<VerificationTrace> trace;
    bool timingsCollected{}, traceTruncated{};
    friend bool operator==(const VerificationDiagnostics&,
                           const VerificationDiagnostics&) = default;
};
} // namespace opusoptima::io
