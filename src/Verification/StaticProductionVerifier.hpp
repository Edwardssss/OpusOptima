#pragma once

#include "IO/SolutionVerifier.hpp"
#include <vector>

namespace opusoptima::verification {

struct StaticProductionEvent {
    enum class Kind { Acquire, Transfer, Output, Dispose, Grab, Release, Motion, Reaction };
    enum class Phase { Initial, GrabDrop, BeforeMotion, Motion, AfterMotion };
    Kind kind{};
    int cycle{};
    std::size_t port{};
    std::vector<std::uint64_t> atoms;
    Phase phase{Phase::Initial};
    struct AtomState {
        std::uint64_t id{};
        int type{};
        bool held{};
        std::vector<std::pair<std::uint64_t, std::uint8_t>> bonds;
    };
    // Captured before output removal, not reconstructed from the target graph.
    std::vector<AtomState> outputState;
};
struct StaticProductionReport {
    io::VerificationStatus status{io::VerificationStatus::Unavailable};
    std::string detail{};
    int cycles{};
    std::vector<std::size_t> outputs{};
    std::size_t acquired{}, disposed{}, remaining{};
    std::vector<StaticProductionEvent> events{};
    io::SolutionMetrics measuredMetrics{};
    bool metricsConclusive{}, continuousCertified{}, traceTruncated{};
    std::optional<io::VerificationDiagnostics> diagnostics{};
    [[nodiscard]] bool Passed() const {
        return status == io::VerificationStatus::Passed;
    }
};

// Bounded ordinary replay with full (up to 4096 events) atom audit; not a legacy authority gate.
[[nodiscard]] StaticProductionReport AuditOrdinaryMachine(const io::PuzzleData&,
                                                         const io::SolutionData&,
                                                         int cycleLimit = 96);
// Independent bounded replay; unsupported instructions and mechanical profiles fail closed.
[[nodiscard]] StaticProductionReport VerifyStaticProduction(const io::PuzzleData&,
                                                            const io::SolutionData&,
                                                            int cycleLimit = 150000);
[[nodiscard]] StaticProductionReport VerifyStaticProductionFiles(
    const std::filesystem::path& puzzle, const std::filesystem::path& solution,
    int cycleLimit = 150000);

} // namespace opusoptima::verification
