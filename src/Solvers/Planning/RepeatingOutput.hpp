#pragma once

#include "BinaryParser.hpp"

namespace opusoptima::solvers {
// Finite six-monomer candidates for libverify's non-consuming repeating outputs.
[[nodiscard]] bool HasRepeatingOutput(const io::Molecule& molecule) noexcept;
[[nodiscard]] io::Molecule ExpandRepeatingOutput(const io::Molecule& molecule);
[[nodiscard]] io::SolutionData CompileRepeatingOutput(const io::PuzzleData& puzzle);
} // namespace opusoptima::solvers
