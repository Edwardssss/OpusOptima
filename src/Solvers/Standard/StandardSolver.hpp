#pragma once

// Ported from gtw123/OpusSolver@fdedd3f01a238b941ceba8bace7ea35cbd3b8c41 (MIT).
// See docs/analysis/UPSTREAM_STANDARD_PORT_20261115.md for the module map.

// Front end for the upstream Standard solver port. `CompileUpstreamStandard` is
// also registered as the `upstream-standard` CLI solver and is offered first by
// the CLI's extra-portfolio path.

#include <chrono>
#include <optional>
#include <vector>

#include "BinaryParser.hpp"

namespace opusoptima::solvers::standard {

/// Compiles one upstream-Standard solution. Returns nullopt when the puzzle is
/// outside the ported domain or the recipe/pipeline rejects it.
[[nodiscard]] std::optional<io::SolutionData> CompileUpstreamStandard(
    const io::PuzzleData& puzzle);

/// Upstream `--optimize`: every feasible recipe (all product scales, cardinal
/// relaxation, all relaxation, and the unneeded-reaction set), plus reagent
/// subsets that can avoid a redundant input, crossed with every available
/// build-order parameter. Solutions are deduplicated by their exact serialized
/// bytes; the first returned entry is the historical single-recipe candidate.
/// When `OPUSOPTIMA_STANDARD_VARIANT_DIR` is set, each variant is written there
/// for offline gate measurement.
[[nodiscard]] std::vector<io::SolutionData> CompileUpstreamStandardVariants(
    const io::PuzzleData& puzzle,
    std::optional<std::chrono::steady_clock::time_point> deadline = std::nullopt,
    std::size_t maxVariants = 0);


} // namespace opusoptima::solvers::standard
