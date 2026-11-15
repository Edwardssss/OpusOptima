#pragma once

#include "SolutionVerifier.hpp"
#include <string_view>

namespace opusoptima::io {
inline bool RequiresCalcifyAnimismusNativeGate(std::string_view stage) {
    return stage == "shared-plan-calcify-animismus" ||
           stage == "shared-plan-ordinary-port-access" ||
           stage.starts_with("shared-plan-ordinary-port-access:parent=") ||
           stage == "shared-plan-wheel" || stage.starts_with("shared-plan-wheel:parent=") ||
           stage == "finite-wheel-second-source" || stage == "whole-feed-singleton-receivers" ||
           stage == "wheel-assembly-calcified-salt" || stage == "portfolio-static-embedding" ||
           stage == "portfolio-static-molecule-partition" ||
           stage == "portfolio-static-input-copy" || stage == "portfolio-static-composition" ||
           stage == "portfolio-static-reaction-purification" ||
           stage == "portfolio-static-reaction-ordinary-pair" ||
           stage == "portfolio-singleton-purification-chain" || stage == "portfolio-radial-wheel" ||
           stage == "portfolio-radial-projection" || stage == "fallback-static-embedding" ||
           stage == "portfolio-row-retimed" || stage == "portfolio-shared-life-death" ||
           stage == "closed-wide-metal-inventory";
}
inline bool CalcifyAnimismusModeAllowed(std::string_view stage, VerificationMode mode) {
    return !RequiresCalcifyAnimismusNativeGate(stage) || mode != VerificationMode::Independent;
}
inline VerificationReport RequireCalcifyAnimismusAuthority(std::string_view stage,
                                                           VerificationReport report) {
    if (RequiresCalcifyAnimismusNativeGate(stage) && report.Passed() &&
        report.authority != VerificationAuthority::LegacyGate) {
        report.status = VerificationStatus::Unavailable;
        report.detail = "material stage requires actual LegacyGate authority";
    }
    return report;
}
} // namespace opusoptima::io
