#pragma once
#include "BaseSolver.hpp"

namespace opusoptima::solvers {
[[nodiscard]] io::SolutionData CompileTracklessPairChemistry(const io::PuzzleData& puzzle);
}
