#pragma once

// Ported from gtw123/OpusSolver@fdedd3f01a238b941ceba8bace7ea35cbd3b8c41 (MIT).
// See docs/analysis/UPSTREAM_STANDARD_PORT_20261115.md for the module map.

// Front end for the upstream Standard solver port. `CompileUpstreamStandard` is
// also registered as the `upstream-standard` CLI solver and is offered first by
// the CLI's extra-portfolio path.

#include <optional>

#include "BinaryParser.hpp"

namespace opusoptima::solvers::standard {

/// Compiles one upstream-Standard solution. Returns nullopt when the puzzle is
/// outside the ported domain or the recipe/pipeline rejects it.
[[nodiscard]] std::optional<io::SolutionData> CompileUpstreamStandard(
    const io::PuzzleData& puzzle);


} // namespace opusoptima::solvers::standard
