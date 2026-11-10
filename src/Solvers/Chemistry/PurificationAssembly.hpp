#pragma once

#include "IO/BinaryParser.hpp"
#include <chrono>

namespace opusoptima::solvers {
// Bounded singleton chain with genuine renewable feeds and a closed piston period at depths 2–5.
[[nodiscard]] bool SupportsSingletonPurificationChain(const io::PuzzleData& puzzle);
// Default admission requires cost-effective depth 4–5 and no genuine target feed.
[[nodiscard]] bool AdmitsSingletonPurificationChain(const io::PuzzleData& puzzle);
[[nodiscard]] io::SolutionData CompileSingletonPurificationChain(const io::PuzzleData& puzzle,
    std::chrono::steady_clock::time_point deadline);

// Physical recursive purification feeding the serial row assembler. Monatomic
// inputs can supply multiple derived metals from one common source metal; a
// shared serialized machine delivers them to the corresponding row feeds.
// Cardinal-to-salt chemistry remains available in row assembly. Six-monomer
// repeating candidates use the existing finite expansion and halt. Finite
// molecular feeds park surplus atoms when disposal is unavailable; opt in only
// after existing strategies have been exhausted.
// Candidates must pass independent verification before publication.
[[nodiscard]] io::SolutionData CompilePurificationAssembly(const io::PuzzleData& puzzle,
                                                           bool allowFiniteFeed = false);
// The absolute deadline bounds planning, row compilation, and purification emission.
[[nodiscard]] io::SolutionData CompilePurificationAssembly(const io::PuzzleData& puzzle,
    bool allowFiniteFeed, std::chrono::steady_clock::time_point deadline);
} // namespace opusoptima::solvers
