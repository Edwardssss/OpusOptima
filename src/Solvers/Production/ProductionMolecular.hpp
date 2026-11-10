#pragma once
#include "BinaryParser.hpp"
namespace opusoptima::solvers {
// Bounded triangular trimer closure/transport/fragmentation. External acceptance required.
[[nodiscard]] io::SolutionData CompileProductionMolecular(const io::PuzzleData& puzzle);
} // namespace opusoptima::solvers
