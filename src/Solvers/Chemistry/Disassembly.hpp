#pragma once

#include "BinaryParser.hpp"
#include "ExecutionIR.hpp"
#include <functional>
#include <optional>

namespace opusoptima::solvers {
void ValidateSpecialRules(const io::PuzzleData& puzzle);
// Carries a congruent connected molecule without changing atom types or bond
// masks, including inherited colored bonds on non-fire atoms.
io::SolutionData CompileIntactFeed(const io::PuzzleData& puzzle);
// Searches bounded bond-cut subsets and lowers matching terminal fragments.
io::SolutionData CompileFragmentFeed(const io::PuzzleData& puzzle);
// Acceptance runs outside compiler-error handling and may reject a layout
// without discarding remaining cut candidates. Exceptions propagate unchanged.
std::optional<io::SolutionData> CompileAcceptedFragmentFeed(
    const io::PuzzleData& puzzle, const std::function<bool(const io::SolutionData&)>& accept);
// Lowers complete bond cuts and outputs, optionally with detached-fragment
// translation and parking in a horizontal corridor (piston reach at most three).
// Piston motion can bypass always-active glyphs. Explicit output counts describe
// a balanced period dividing the puzzle demand; repeated deliveries share a port.
// This bounded layout does not support chemistry, rotation or repeated supply.
io::SolutionData CompileDisassemblyIR(const io::PuzzleData& puzzle, const ExecutionIR& program,
                                      const std::vector<std::size_t>& outputCounts = {});
// Lowers Acquire, positive-q Relocate, intermediate Store/Retrieve and Output
// to a repeatable tape.
// Only a separated positive-q translation corridor is currently supported;
// unsupported IR operations fail rather than being delegated to a template.
io::SolutionData CompileTranslationIR(const io::PuzzleData& puzzle, const ExecutionIR& program);
} // namespace opusoptima::solvers
