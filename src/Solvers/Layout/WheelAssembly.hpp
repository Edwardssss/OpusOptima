#pragma once

#include "IO/BinaryParser.hpp"

namespace opusoptima::solvers {
// Compile a physical salt/Van Berlo duplication supply feeding row assembly.
// Monatomic feeds with finite ordinary-bond outputs, or balanced molecular
// salt/cardinal disassembly with ordinary outputs or distinct atomic outputs.
// Calcifiable monatomic sources may also supply Salt alongside derived cardinals.
// This returns a candidate; publication requires verification and measured-metric
// revalidation. The mixed calcified-Salt route requires actual LegacyGate authority.
[[nodiscard]] io::SolutionData CompileWheelAssembly(const io::PuzzleData& puzzle);

// Ordinary radial Salt/cardinal products share one rotating catalyst and bonder.
// Bounded default admission has an explicit off switch and requires whole-machine gating.
// The candidate has a closed one-product period.
[[nodiscard]] bool SupportsRadialWheelAssembly(const io::PuzzleData& puzzle);
[[nodiscard]] io::SolutionData CompileRadialWheelAssembly(const io::PuzzleData& puzzle);

// Singleton metal feeds share one projection station with individually delivered Mercury doses.
// Limited default admission accepts unset or on; off and invalid values disable it.
// Publication requires native verification of the original puzzle and every output port.
[[nodiscard]] bool SupportsRadialProjectionAssembly(const io::PuzzleData& puzzle);
[[nodiscard]] io::SolutionData CompileRadialProjectionAssembly(const io::PuzzleData& puzzle);
} // namespace opusoptima::solvers
