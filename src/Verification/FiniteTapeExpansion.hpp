#pragma once

#include "IO/BinaryParser.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <variant>
#include <vector>

namespace opusoptima::verification {
struct TapeOrigin {
    enum class Kind { Hole, Raw, Reset, Copy } kind{Kind::Hole};
    std::optional<std::size_t> rawOrdinal;
    std::optional<std::int32_t> rawIndex;
    std::optional<std::int64_t> copySite, sourceSlot;
    std::optional<std::uint8_t> resetStep;
};
struct ExpandedFiniteTape {
    std::size_t armObject{};
    std::optional<std::int32_t> rawStart;
    std::int64_t rawEntries{}, rawSpan{}, expandedLength{}, staticInstructions{};
    std::optional<std::int64_t> haltSlot;
    std::vector<io::ArmInstruction> slots;
    std::vector<TapeOrigin> origins;
};
enum class TapeExpansionError {
    Malformed, OutsideDomain, BoundExceeded, Conflict, UnknownState, ResourceUnavailable
};
struct TapeExpansionFailure {
    TapeExpansionError code;
    std::optional<std::size_t> rawOrdinal;
    std::optional<std::int64_t> slot;
};
using TapeExpansionResult = std::variant<ExpandedFiniteTape, TapeExpansionFailure>;
// Offline conversion only: success is neither a scope decision nor certification.
// The optional enclosing deadline never renews the caller's budget. Initial
// structural/domain validation precedes cancellation, which returns no partial tape.
TapeExpansionResult ExpandFiniteArm1Tape(const io::PuzzleData&, const io::SolutionData&,
    std::optional<std::chrono::steady_clock::time_point> deadline = {});
} // namespace opusoptima::verification
