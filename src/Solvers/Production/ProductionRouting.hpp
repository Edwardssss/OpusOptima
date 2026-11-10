#pragma once

#include "ProductionTransport.hpp"

namespace opusoptima::solvers {
struct ProductionRoutePlan {
    std::vector<ProductionChamberLayout> chambers;
    std::vector<ProductionConduitHandoff> handoffs;
    int period{};
    core::HexCoord inputPosition{}, outputPosition{};
    int inputRotation{}, outputRotation{};
    std::vector<int> relayTurns;
};

// A bounded serial schedule for an unchanged connected rigid molecule (1–16
// atoms, preserving ordinary/colored bond masks) along a connected 2–16 chamber
// tree's longest path. Relays alternate rotations to limit footprint drift.
// Branches outside the selected source-to-sink path remain unused; unsupported
// footprints fail closed and compiled candidates require independent verification.
[[nodiscard]] ProductionRoutePlan PlanProductionRoute(const io::PuzzleData& puzzle,
                                                      unsigned relayChoices = 0);
// Explore up to 64 relay-turn patterns and six orientations, sharing a maximum
// of 64 acceptance callbacks and a two-second deadline (checked between calls).
// Verification callbacks run outside planning catches so infrastructure failures propagate.
[[nodiscard]] std::optional<io::SolutionData> CompileAcceptedProductionRoute(
    const io::PuzzleData& puzzle, const std::function<bool(const io::SolutionData&)>& accept);
// Jointly schedule 2–8 independent input/output streams through conduit paths,
// including rigid transport and selective molecular calcification. Input matching,
// conduit paths, and directions are searched together with a common tape period.
// Disjoint chambers are preferred; shared chambers additionally try bounded local
// translations and stream phase offsets. Single- and multi-cell conduits may be
// reused when endpoint cell indices preserve the same payload correspondence;
// duplicate pipe objects are merged and dynamic collisions must be rejected by
// the acceptance verifier.
// Only complete machines reach acceptance (64 calls/256 states/2 s). Chemistry
// alternatives share 64 compilations and the outer deadline, and rejected complete
// machines resume local atom-correspondence/relay searches. Chemistry coupling
// between streams is not supported; shared feed coproducts use the separate
// CompileAcceptedProductionCoproducts backend.
[[nodiscard]] std::optional<io::SolutionData> CompileAcceptedProductionStreams(
    const io::PuzzleData& puzzle, const std::function<bool(const io::SolutionData&)>& accept);

// rotation is a clockwise local machine orientation in [0, 5], independent of
// chamber ordering, placement, and the original molecule coordinates.
[[nodiscard]] io::SolutionData CompileProductionRoute(const io::PuzzleData& puzzle,
                                                      int rotation = 0);

// Compose selective cardinal-to-salt chemistry with a connected rigid molecule
// route. Atom coordinates must match up to rigid rotation and translation,
// independently of serialization order; unchanged atoms
// and all bond masks are preserved. Glyphs may occupy any route chamber (indexed
// in PlanProductionRoute order); geometry shares 64 compilation attempts across
// rigid atom correspondences and relay patterns, with a two-second deadline.
// Same transport/rule gates apply; candidates require independent six-product
// verification. No vials or shrink.
[[nodiscard]] io::SolutionData CompileProductionChemistryRoute(const io::PuzzleData& puzzle,
                                                               std::size_t chemistryStage = 0,
                                                               int rotation = 0);

// Retry rejected atom correspondences, relay patterns, stages and orientations
// under one shared 64-compilation/two-second budget. Acceptance exceptions propagate.
[[nodiscard]] std::optional<io::SolutionData> CompileAcceptedProductionChemistryRoute(
    const io::PuzzleData& puzzle, const std::function<bool(const io::SolutionData&)>& accept);
} // namespace opusoptima::solvers
