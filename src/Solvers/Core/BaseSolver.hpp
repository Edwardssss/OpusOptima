#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "BinaryParser.hpp"
#include "SolutionVerifier.hpp"

namespace opusoptima::solvers {

/// Abstract base class for all puzzle solvers.
///
/// Each solver inherits from this class and registers itself via the
/// static Register() method, so that it can be created by name at
/// runtime without hard-coded factory logic.
class BaseSolver {
    public:
    using Factory = std::function<std::unique_ptr<BaseSolver>()>;

    explicit BaseSolver(std::string_view name) : solverName_(name) {}
    virtual ~BaseSolver() = default;

    // ── Solver interface ─────────────────────────────────────

    /// The main entry point: solve a puzzle and return a solution.
    [[nodiscard]] virtual io::SolutionData Solve(const io::PuzzleData& puzzle) = 0;

    void SetDeadline(std::chrono::steady_clock::time_point deadline) {
        deadline_ = deadline;
    }
    [[nodiscard]] std::chrono::steady_clock::time_point Deadline() const {
        return deadline_;
    }

    using CandidateAcceptance = std::function<bool(const io::SolutionData&)>;
    /// Try candidates until the caller accepts one; validation remains outside compilers.
    [[nodiscard]] virtual io::SolutionData SolveAccepted(const io::PuzzleData& puzzle,
                                                         const CandidateAcceptance& accept) {
        auto candidate = Solve(puzzle);
        if (!accept(candidate))
            throw std::invalid_argument("solver candidate rejected by acceptance check");
        return candidate;
    }

    using CandidateVerification = std::function<io::VerificationReport(const io::SolutionData&)>;
    [[nodiscard]] virtual io::SolutionData SolveVerified(const io::PuzzleData& puzzle,
                                                         const CandidateVerification& verify) {
        io::VerificationReport report;
        auto candidate = SolveAccepted(puzzle, [&](const io::SolutionData& solution) {
            report = verify(solution);
            return report.Passed();
        });
        candidate.metrics = report.metrics;
        return candidate;
    }

    /// Optional: estimate cycles without full solve.
    [[nodiscard]] virtual std::int32_t EstimateCycles(const io::PuzzleData& puzzle) = 0;

    /// Human-readable solver name for logging / debugging.
    [[nodiscard]] std::string_view Name() const noexcept {
        return solverName_;
    }

    // ── Registry ─────────────────────────────────────────────

    /// Register a solver factory under a canonical name.
    /// Called once per solver at static-init time.
    static void Register(std::string name, Factory factory);

    static constexpr const char* kDefaultName = "assembly";

    /// Instantiate a solver by name, or the recommended assembly portfolio if omitted.
    /// Returns nullptr for an unknown explicit name; never substitutes another backend.
    [[nodiscard]] static std::unique_ptr<BaseSolver> Create(const std::string& name = kDefaultName);

    /// All registered solver names (sorted for stable output).
    [[nodiscard]] static std::vector<std::string> RegisteredNames();

    private:
    std::chrono::steady_clock::time_point deadline_{std::chrono::steady_clock::time_point::max()};
    std::string solverName_;

    static std::unordered_map<std::string, Factory>& Registry();
};

} // namespace opusoptima::solvers
