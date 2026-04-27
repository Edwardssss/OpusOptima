#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "BinaryParser.hpp"

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

    /// Instantiate a solver by name.  Returns nullptr if unknown.
    [[nodiscard]] static std::unique_ptr<BaseSolver> Create(const std::string& name);

    /// All registered solver names (sorted for stable output).
    [[nodiscard]] static std::vector<std::string> RegisteredNames();

    private:
    std::string solverName_;

    static std::unordered_map<std::string, Factory>& Registry();
};

} // namespace opusoptima::solvers
