#pragma once

namespace opusoptima::io {
struct PuzzleData;
}

namespace opusoptima::solvers {
// Legacy entry points retain environment-controlled chemistry and source retention.
// ClosedCardinalLife is valid only for the guarded closed temporal composition.
enum class SupplyMaterialLoweringPolicy {
    PreserveCompatible,
    CutOrdinarySources,
    ClosedCardinalLife
};

[[nodiscard]] bool SupportsClosedCardinalLife(const io::PuzzleData& puzzle);
} // namespace opusoptima::solvers
