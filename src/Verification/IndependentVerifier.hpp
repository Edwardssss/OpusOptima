#pragma once

#include "StaticProductionVerifier.hpp"
#include "FiniteTapeExpansion.hpp"
#include "FiniteRepeatingProgress.hpp"

namespace opusoptima::verification {
enum class ReplayOutcome { Completed, Rejected, CycleLimit, Timeout, Unavailable, Error };
struct FiniteReplayReport {
    ReplayOutcome outcome{ReplayOutcome::Unavailable};
    std::string detail;
    int cycles{};
    std::vector<std::size_t> outputs;
    std::size_t acquired{}, disposed{}, remaining{};
    std::vector<StaticProductionEvent> events;
    bool traceTruncated{};
    std::optional<io::VerificationDiagnostics> diagnostics{};
};
struct RepeatingFiniteReplayReport {
    FiniteReplayReport execution;
    const bool certificationEligible{false};
    bool resourceUnavailable{};
};
// Functional opt-in only; fixed six-monomer bound, no raw C/X expansion.
[[nodiscard]] RepeatingFiniteReplayReport ReplayRepeatingFiniteMachine(
    const io::PuzzleData&, const io::SolutionData&, int cycleLimit = 150000);

struct ExpandedFiniteReplayReport {
    FiniteReplayReport execution;
    const bool certificationEligible{false};
    std::optional<io::SolutionData> rawSource;
    std::optional<ExpandedFiniteTape> expandedTape;
    std::optional<TapeExpansionFailure> expansionFailure;
    std::uint64_t totalMicros{};
};
// Explicit opt-in functional execution only; no verification authority or raw metrics.
[[nodiscard]] ExpandedFiniteReplayReport ReplayExpandedFiniteMachine(
    const io::PuzzleData&, const io::SolutionData&, int cycleLimit = 150000);

[[nodiscard]] FiniteReplayReport ReplayFiniteMachine(const io::PuzzleData&, const io::SolutionData&,
                                                     int cycleLimit = 150000,
                                                     bool collectTimings = false);
[[nodiscard]] io::VerificationReport VerifyIndependent(const io::PuzzleData&,
                                                       const io::SolutionData&,
                                                       int cycleLimit = 150000,
                                                       bool collectTimings = false);
[[nodiscard]] io::VerificationReport VerifyIndependentFiles(const std::filesystem::path& puzzle,
                                                            const std::filesystem::path& solution,
                                                            int cycleLimit = 150000,
                                                            bool collectTimings = false);
} // namespace opusoptima::verification
