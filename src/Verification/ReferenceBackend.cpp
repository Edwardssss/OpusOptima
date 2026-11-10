#include "ReferenceBackend.hpp"

#include "Verification/libverify.h"
#include "StaticProductionVerifier.hpp"

#include <cstring>
#include <memory>

namespace opusoptima::verification {
namespace {
std::string LegacyDetail(const std::string& text) {
    return text.substr(0, 511);
}
} // namespace

io::VerificationReport EvaluateLegacy(const std::filesystem::path& puzzle,
                                      const std::filesystem::path& solution, int cycleLimit, bool) {
    io::VerificationReport result;
    result.engineVersion = "omsim-a88d12d8+production-replay-v1";
    result.rulesetVersion = "legacy-gate-v1";
    result.capabilityProfile = "legacy-native";
    bool requireIndependentProduction = false;
    try {
        const auto parsed = io::BinaryParser::ParsePuzzleFile(puzzle);
        requireIndependentProduction =
            parsed.productionInfo &&
            (!parsed.productionInfo->vials.empty() || parsed.productionInfo->shrinkLeft ||
             parsed.productionInfo->shrinkRight);
    } catch (const std::exception& error) {
        result.detail =
            std::string("Cannot inspect puzzle verification requirements: ") + error.what();
        result.reasonCode = "puzzle-inspection-error";
        return result;
    }
    const auto puzzleName = puzzle.string();
    const auto solutionName = solution.string();
    std::unique_ptr<void, decltype(&verifier_destroy)> verifier(
        verifier_create(puzzleName.c_str(), solutionName.c_str()), verifier_destroy);
    if (!verifier) {
        result.detail = "Cannot allocate verifier";
        result.reasonCode = "reference-allocation-failed";
        return result;
    }
    verifier_set_cycle_limit(verifier.get(), cycleLimit);
    result.metrics.cycles = verifier_evaluate_metric(verifier.get(), "cycles");
    const char* error = verifier_error(verifier.get());
    if (!error && result.metrics.cycles >= 0) {
        result.metrics.cost = verifier_evaluate_metric(verifier.get(), "cost");
        result.metrics.area = verifier_evaluate_metric(verifier.get(), "area");
        result.metrics.instructionCount = verifier_evaluate_metric(verifier.get(), "instructions");
        error = verifier_error(verifier.get());
        if (!error && result.metrics.cost >= 0 && result.metrics.area >= 0 &&
            result.metrics.instructionCount >= 0) {
            result.status = io::VerificationStatus::Passed;
            result.reasonCode = "legacy-gate-passed";
            if (requireIndependentProduction) {
                result.capabilityProfile = "legacy-native-and-production-replay";
                const auto independent = VerifyStaticProductionFiles(puzzle, solution, cycleLimit);
                result.status = independent.status;
                std::string detail = independent.detail;
                if (independent.Passed() && independent.cycles != result.metrics.cycles) {
                    result.status = io::VerificationStatus::Error;
                    detail = "Independent production engines disagree about completion cycle";
                    result.reasonCode = "production-completion-disagreement";
                } else if (!independent.Passed()) {
                    result.reasonCode = "production-replay-not-passed";
                }
                if (result.status == io::VerificationStatus::Unavailable)
                    detail = "Independent production vial/rule verification unavailable for this "
                             "profile: " +
                             detail;
                result.detail = LegacyDetail(detail);
                if (!result.Passed()) result.metrics = {};
            }
            if (result.Passed()) result.authority = io::VerificationAuthority::LegacyGate;
            return result;
        }
    }
    result.status = error && std::strstr(error, "within cycle limit")
                        ? io::VerificationStatus::CycleLimit
                        : io::VerificationStatus::Rejected;
    result.detail = LegacyDetail(error ? error : "Metric evaluation failed");
    result.errorCycle = verifier_error_cycle(verifier.get());
    result.errorU = verifier_error_location_u(verifier.get());
    result.errorV = verifier_error_location_v(verifier.get());
    result.reasonCode = result.status == io::VerificationStatus::CycleLimit
                            ? "reference-cycle-limit"
                            : "reference-rejected";
    return result;
}
} // namespace opusoptima::verification
