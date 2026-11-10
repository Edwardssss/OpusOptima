#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <optional>
#include <span>
#include <variant>

#include "FiniteRepeatingGraph.hpp"

namespace opusoptima::verification {
struct FiniteRepeatingPortConfig {
    std::size_t maxMonomers;
    std::size_t outputMultiplier;
    std::size_t targetMonomers{6};
};
struct FiniteRepeatingPortProgress {
    std::size_t highWaterMonomers{};
    std::size_t outputCount{};
    std::size_t targetOutputs{};
    bool reached{};
};
using FiniteRepeatingProgressResult =
    std::variant<FiniteRepeatingPortProgress, FiniteRepeatingGraphFailure>;

// Explicit finite functional state only; never an independent certification result.
class FiniteRepeatingProgress {
public:
    // enclosingDeadline must be the caller's existing deadline when part of a larger run.
    // A port must retain its raw template, local frame, and displacement across observations.
    // Throws invalid_argument for configurations outside the bounded functional domain.
    explicit FiniteRepeatingProgress(
        std::span<const FiniteRepeatingPortConfig> ports,
        std::chrono::steady_clock::time_point enclosingDeadline =
            std::chrono::steady_clock::time_point::max());
    FiniteRepeatingProgressResult Observe(std::size_t port, const io::Molecule& rawTemplate,
                                         const io::Molecule& component,
                                         core::HexCoord displacement);
    [[nodiscard]] std::optional<FiniteRepeatingPortProgress> Port(std::size_t port) const;

private:
    struct PortState {
        FiniteRepeatingPortConfig config{};
        FiniteRepeatingPortProgress progress{};
    };
    std::array<PortState, 16> ports_{};
    std::size_t portCount_{};
    std::chrono::steady_clock::time_point deadline_;
};
} // namespace opusoptima::verification
