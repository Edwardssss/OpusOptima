#pragma once

#include "AutomaticSupply.hpp"
#include "ProductionExecution.hpp"

namespace opusoptima::solvers {

// One transported quintessence feed, one real static conduit, and a complete
// four-output dispersion period. Room-local glyph products share the original
// material ledger and require independent acceptance of the serialized machine.
[[nodiscard]] std::optional<io::SolutionData> CompileAcceptedProductionDispersion(
    const io::PuzzleData& puzzle, const std::function<bool(const io::SolutionData&)>& accept,
    const std::function<void(const SupplyFailure&)>& feedback = {});
[[nodiscard]] std::optional<io::SolutionData> CompileAcceptedProductionDuplication(
    const io::PuzzleData& puzzle, const std::function<bool(const io::SolutionData&)>& accept,
    const std::function<void(const SupplyFailure&)>& feedback = {});

} // namespace opusoptima::solvers
