#pragma once

#include "BinaryParser.hpp"

#include <cstddef>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace opusoptima::solvers::architecture {

// ── M-D: solution parameter registry + enumeration ───────────────────────────
//
// Upstream's `--optimize` re-runs the machine builder once per combination of
// `SolutionParameter` values (arm length, element order, bond traversal, extra
// disassembler width, no-arm buffers, ...), deduplicates the resulting machines
// and keeps the best one per objective.
//
// This is the same contract at the scale this port can afford: every knob the
// architecture compiler exposes is declared here, combinations are enumerated
// deterministically, and `ObjectiveSelector` keeps the best machine for cost,
// cycles and area separately instead of the 2/3-majority vote the row portfolio
// uses.

struct SolutionParameter {
    std::vector<std::string> values;
    std::string defaultValue;
};

class ParameterRegistry {
    public:
    void Define(std::string name, std::vector<std::string> values, std::string defaultValue);
    [[nodiscard]] const std::vector<std::pair<std::string, SolutionParameter>>& Entries() const {
        return entries_;
    }
    /// `value(name)` of the default of every parameter, i.e. one full setting.
    [[nodiscard]] std::map<std::string, std::string> Defaults() const;
    /// Deterministic cartesian product, in declaration order, bounded by `cap`.
    [[nodiscard]] std::vector<std::map<std::string, std::string>> Enumerate(
        std::size_t cap = 64) const;
    /// Stable dedup key for one setting.
    [[nodiscard]] static std::string Key(const std::map<std::string, std::string>& setting);
    /// Drops settings whose key was already seen, preserving order.
    [[nodiscard]] static std::vector<std::map<std::string, std::string>> Deduplicate(
        std::vector<std::map<std::string, std::string>> settings);

    private:
    std::vector<std::pair<std::string, SolutionParameter>> entries_;
};

/// Keeps one best machine per objective. Ties keep the earlier submission so the
/// result is deterministic.
class ObjectiveSelector {
    public:
    /// Returns true when `solution` became one of the three best machines.
    bool Submit(const io::SolutionData& solution);
    [[nodiscard]] bool Empty() const {
        return !cost_ && !cycles_ && !area_;
    }
    [[nodiscard]] const std::optional<io::SolutionData>& BestCost() const {
        return cost_;
    }
    [[nodiscard]] const std::optional<io::SolutionData>& BestCycles() const {
        return cycles_;
    }
    [[nodiscard]] const std::optional<io::SolutionData>& BestArea() const {
        return area_;
    }
    /// The machine that wins the most slots against the others; a majority of two
    /// out of three is preferred, and cycles break a tie.
    [[nodiscard]] std::optional<io::SolutionData> Publish() const;

    private:
    std::optional<io::SolutionData> cost_, cycles_, area_;
};

} // namespace opusoptima::solvers::architecture
