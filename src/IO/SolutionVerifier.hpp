#pragma once

#include <chrono>
#include <filesystem>
#include <optional>
#include <string>

#include "BinaryParser.hpp"
#include "VerificationDiagnostics.hpp"

namespace opusoptima::io {

enum class VerificationStatus { Passed, Rejected, CycleLimit, Timeout, Unavailable, Error };
enum class VerificationAuthority { None, LegacyGate, Independent };
enum class VerificationMode { LegacyGate, Independent, Shadow };

struct VerificationOptions {
    VerificationMode mode{VerificationMode::LegacyGate};
    int cycleLimit{150000};
    int timeoutMilliseconds{10000};
    std::filesystem::path workerExecutable{};
    std::filesystem::path independentWorkerExecutable{};
    std::optional<std::chrono::steady_clock::time_point> outerDeadline{};
    bool collectTimings{};
};

struct VerificationObservation {
    VerificationStatus status{VerificationStatus::Error};
    SolutionMetrics metrics{};
    std::string detail;
    VerificationAuthority authority{VerificationAuthority::None};
    std::string engineVersion, rulesetVersion, capabilityProfile, reasonCode;
    std::optional<VerificationDiagnostics> diagnostics{};
};

struct VerificationReport {
    VerificationStatus status{VerificationStatus::Error};
    SolutionMetrics metrics{};
    std::string detail;
    int errorCycle{};
    int errorU{};
    int errorV{};
    VerificationAuthority authority{VerificationAuthority::None};
    std::string engineVersion{};
    std::string rulesetVersion{};
    std::string capabilityProfile{};
    std::string reasonCode{};
    std::optional<VerificationObservation> shadow{};
    std::optional<VerificationDiagnostics> diagnostics{};
    [[nodiscard]] bool Passed() const {
        return status == VerificationStatus::Passed;
    }
};

// Production vial/nondefault-bound candidates require both the pinned external
// oracle and the independent static-production profile, including matching
// completion cycles. Unknown profiles remain Unavailable; native rejections
// are never promoted by the additional verifier.
[[nodiscard]] VerificationReport VerifySolutionFiles(const std::filesystem::path& puzzle,
                                                     const std::filesystem::path& solution,
                                                     const VerificationOptions& options);
[[nodiscard]] VerificationReport VerifySolutionFiles(const std::filesystem::path& puzzle,
                                                     const std::filesystem::path& solution,
                                                     int cycleLimit = 150000,
                                                     int timeoutMilliseconds = 10000);

// Only a verified file replaces the destination; rejected candidates are removed.
[[nodiscard]] VerificationReport PublishVerifiedSolution(const std::filesystem::path& puzzle,
                                                         const std::filesystem::path& destination,
                                                         const SolutionData& solution);
[[nodiscard]] VerificationReport PublishVerifiedSolution(const std::filesystem::path& puzzle,
                                                         const std::filesystem::path& destination,
                                                         const SolutionData& solution,
                                                         const VerificationOptions& options);

} // namespace opusoptima::io
