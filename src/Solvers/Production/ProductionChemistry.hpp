#pragma once

#include "BinaryParser.hpp"

namespace opusoptima::solvers {
/// Bounded, single-chamber backend: cardinal singleton -> salt (Small/Medium/Large),
/// or two singleton feeds -> one ordinary or Fire color-mask bonded pair when
/// a translated/rotated machine fits the chamber, including all prisma vertices.
/// Ordinary pair endpoints may independently calcify; input and output atom
/// ordering is immaterial. Also emits arm-free joint animismus (one/two singleton
/// salt or calcifiable feeds -> both Vitae/Mors outputs) and dispersion
/// (quintessence -> all four cardinal outputs, independently calcifiable to salt).
/// Joint outputs may be reordered and have translated molecular coordinates.
/// No conduits, vials, shrink, retained catalysts, or unconsumed coproducts.
/// Returns a candidate, not an acceptance certificate: callers must independently
/// verify the serialized production puzzle/solution through six products.
[[nodiscard]] io::SolutionData CompileProductionChemistry(const io::PuzzleData& puzzle);
} // namespace opusoptima::solvers
