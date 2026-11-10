#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <variant>

#include "BinaryParser.hpp"

namespace opusoptima::verification {
struct FiniteRepeatingGraphRequest {
    const io::Molecule& rawTemplate;
    const io::Molecule& component;
    core::HexCoord displacement;
    std::size_t maxMonomers;
};
struct RepeatingGraphCell { std::int64_t q, r; };
enum class RepeatingGraphDifferenceKind { MissingAtom, AtomType, BondMask, UnexpectedAtom };
struct RepeatingGraphDifference {
    RepeatingGraphDifferenceKind kind;
    std::size_t monomerCount;
    RepeatingGraphCell cell;
    std::optional<RepeatingGraphCell> neighbor;
    std::optional<core::AtomType> expectedType, actualType;
    std::uint8_t expectedBits{}, actualBits{}, allowedBits{};
};
struct FiniteRepeatingGraphChecked {
    std::size_t provenPrefix;
    std::optional<RepeatingGraphDifference> firstDifference;
};
enum class RepeatingGraphFailureCode { Malformed, OutsideDomain, ResourceUnavailable };
struct FiniteRepeatingGraphFailure {
    RepeatingGraphFailureCode code;
    std::string detail;
};
using FiniteRepeatingGraphResult =
    std::variant<FiniteRepeatingGraphChecked, FiniteRepeatingGraphFailure>;
FiniteRepeatingGraphResult CheckFiniteRepeatingGraph(const FiniteRepeatingGraphRequest&);
} // namespace opusoptima::verification
