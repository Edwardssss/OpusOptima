#pragma once

#include "BinaryParser.hpp"

#include <cstddef>
#include <functional>
#include <optional>
#include <vector>

namespace opusoptima::solvers {
struct ProductionChamberLayout {
    std::size_t chamberIndex{};
    core::HexCoord center{};
    std::vector<core::HexCoord> interior;
    std::vector<core::HexCoord> walls;
};

struct ProductionConduitHandoff {
    std::size_t conduitIndex{};
    int conduitId{};
    std::vector<core::HexCoord> localShape;
    core::HexCoord sourcePosition{}, destinationPosition{};
    int rotation{};
    // Payload coordinates are relative to each relocated conduit anchor.
    io::Molecule payload;
    // Zero-based instruction cycles of the verified template, not a general conduit latency model.
    int releaseCycle{}, pickupCycle{}, period{};
};

struct ProductionTransportPlan {
    ProductionChamberLayout source, destination;
    ProductionConduitHandoff handoff;
    core::HexCoord inputPosition{}, outputPosition{};
    int inputRotation{}, outputRotation{};
    int armRotation{};
    int armLength{1};
};

// Planning has the same restricted support and rejection rules as compilation below.
// canonicalRelay restricts larger rigid payloads to unit arms and the unrotated
// local pose required by the serial multi-chamber relay compiler.
[[nodiscard]] ProductionTransportPlan PlanProductionTransport(const io::PuzzleData& puzzle,
                                                              bool canonicalRelay = false);

// Restricted v3 production transport: one non-Repeat atom (unchanged or cardinal
// to salt with calcification permission), or one unchanged adjacent bonded pair,
// multiplier one, two nonoverlapping supported chambers, and one
// matching one/two-cell conduit rooted at (0,0), wholly inside distinct chambers.
// Input/output rotation, translation and atom reordering are supported.
// Coordinates are bounded to [-120,120]; arm1, rotation/drop, grab and no-op permissions required.
// Isolation and singleton cardinal-to-salt calcification (with permission) are
// supported. Connected unchanged molecules of 3–16 atoms also use a bounded
// rigid-pose search with a matching conduit footprint, including six whole-machine
// orientations about the chamber centers; this does not establish
// swept-motion safety, so native acceptance remains mandatory. Other chemistry,
// shrink rules and vials are unsupported.
// The candidate still requires external six-product acceptance before publication.
[[nodiscard]] io::SolutionData CompileProductionTransport(const io::PuzzleData& puzzle);

// Transport one connected feed as a rigid payload, then partition it into 2–16
// connected molecular output ports at the destination. Internal bond masks are
// preserved exactly; cross-product ordinary/colored bonds are removed with
// permitted unbonders. Optional cardinal-to-salt conversion is selective.
// Every payload atom must reach an output (no implicit surplus disposal). Uses
// the transport geometry bounds and at most 64 externally verified candidates;
// singleton/molecular partition search also shares a 4096-state/two-second budget.
// Independent rejection continues with alternative atom-to-port assignments;
// equivalent singleton rotations do not consume acceptance attempts.
[[nodiscard]] std::optional<io::SolutionData> CompileAcceptedProductionCoproducts(
    const io::PuzzleData& puzzle, const std::function<bool(const io::SolutionData&)>& accept);

// Rejected singleton and pair poses try up to six whole-machine orientations;
// rejected larger rigid poses continue the bounded geometric search. Acceptance must
// check actual swept motion and six-product completion independently.
[[nodiscard]] std::optional<io::SolutionData> CompileAcceptedProductionTransport(
    const io::PuzzleData& puzzle, const std::function<bool(const io::SolutionData&)>& accept,
    bool enumerateOrientations = true);
} // namespace opusoptima::solvers
