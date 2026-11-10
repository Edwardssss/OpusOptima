#pragma once

#include "IO/SolutionVerifier.hpp"

namespace opusoptima::verification {
[[nodiscard]] io::VerificationReport EvaluateLegacy(const std::filesystem::path& puzzle,
                                                    const std::filesystem::path& solution,
                                                    int cycleLimit, bool collectTimings = false);
} // namespace opusoptima::verification
