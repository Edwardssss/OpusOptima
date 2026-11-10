#pragma once

#include "SolutionVerifier.hpp"
#include <string>
#include <string_view>

namespace opusoptima::io {
[[nodiscard]] std::string_view VerificationStatusName(VerificationStatus);
[[nodiscard]] std::string_view VerificationAuthorityName(VerificationAuthority);
[[nodiscard]] std::string_view VerificationPhaseName(VerificationPhase);
[[nodiscard]] int VerificationExitCode(VerificationStatus);
[[nodiscard]] std::string VerificationReportJson(const VerificationReport&);
} // namespace opusoptima::io
