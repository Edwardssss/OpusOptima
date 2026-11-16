#pragma once
#include "BinaryParser.hpp"
#include "SolutionVerifier.hpp"
#include <cstdint>
#include <chrono>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace opusoptima::solvers {
// Experimental serial ordinary-bond station; caller must independently verify.
std::optional<io::SolutionData> CompileSharedWorkstation(const io::PuzzleData&);
enum class SupplyFailureCategory {
    Material,
    Layout,
    Timing,
    Budget,
    IndependentVerifier,
    VerificationUnavailable
};
struct SupplyFailure {
    SupplyFailureCategory category;
    int rotation;
    int spacing;
    std::string detail;
    std::size_t materialAttempt{};
    std::size_t productsPerPeriod{};
    std::optional<io::VerificationReport> verification{};
    std::optional<int> missingPermission{};
    std::optional<std::size_t> operation{}, input{}, output{}, chamber{}, conduit{};
    std::vector<std::uint64_t> fragment{};
    std::optional<core::HexCoord> conflictCell{};
    std::optional<int> conflictCycle{};
    std::string mechanicalMode{}, budgetDimension{};
};
// Geometry-derived rail layouts for bounded connected feeds and finite outputs.
// Joint inventory periods share feed batches across up to eight output ports.
// Supports residual custody, multiple products per period, ordinary and colored
// bond extraction/rebuilding, and bounded chemical stations including coproduct
// reuse and catalyst borrowing/return. Retained fragments can pivot with bit 26
// when a conservative clear-disk sweep excludes parked stock and renewable feeds;
// tight rotations and arm-base rotation remain unsupported. Production placement
// includes the pivot sweep footprint. Unsupported schedules fail closed.
// Production lowering here fits the material program inside one static chamber;
// conduit-connected material custody is coordinated by ProductionExecution
// alongside the restricted production transport and chemistry backends. Every
// candidate requires independent acceptance; callback exceptions propagate unchanged.
// Feedback reports failed candidates and permits upstream parameter adjustment.
// Permission failures carry the missing puzzle permission bit. Repeated layouts
// for that material/extraction mode are skipped without excluding other histories.
// Output orientations that cannot fit the rail are rejected before material search,
// with Layout feedback and materialAttempt zero; retries cannot repair that geometry.
// Feed reach/width and extraction permissions constrain upstream inventory search;
// unused feeds retain their puzzle indices but produce no physical input objects.
// Layout trials receive bounded time slices so one material history cannot starve
// upstream replanning within the shared deadline. Supported production periods
// include every divisor of 6 * outputMultiplier up to 12. Replanning divides the
// remaining shared state budget across remaining periods, including period four.
// Exhausted replanning reports terminal Material/Budget feedback with rotation -1
// and spacing 0, distinguishing supported-period exhaustion from resource limits.
// `deadline` bounds the whole inventory attempt, including route certificates.
std::optional<io::SolutionData> CompileAcceptedSupplyInventory(
    const io::PuzzleData&, const std::function<bool(const io::SolutionData&)>& accept,
    const std::function<void(const SupplyFailure&)>& feedback = {},
    std::chrono::steady_clock::time_point deadline =
        std::chrono::steady_clock::time_point::max());

// Retains the independent verifier's status, cycle and coordinates in feedback.
std::optional<io::SolutionData> CompileVerifiedSupplyInventory(
    const io::PuzzleData&,
    const std::function<io::VerificationReport(const io::SolutionData&)>& verify,
    const std::function<void(const SupplyFailure&)>& feedback = {},
    std::chrono::steady_clock::time_point deadline =
        std::chrono::steady_clock::time_point::max());
} // namespace opusoptima::solvers
