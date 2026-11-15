#pragma once
#include "BinaryParser.hpp"
#include <cstdint>
#include <limits>
#include <set>

namespace opusoptima::solvers::detail {
// Only the ordinary, straight Animismus collector is eligible. Any junction,
// second arm, unexpected tape or capacity outside the native quota keeps the
// original rail. This is a geometry proof, not a replacement for native gating.
inline bool TrimLifeDeathParking(io::SolutionData& machine, std::size_t rail,
                                std::size_t arm, std::int64_t demand) {
    if (rail >= machine.objects.size() || arm >= machine.objects.size() ||
        demand < 0 || demand > (9999 - 4) / 3) return false;
    auto& track = machine.objects[rail];
    const auto& collector = machine.objects[arm];
    const auto& data = track.bodyData;
    const int cells = static_cast<int>(demand * 3 + 4);
    if (track.typeName != "track" || collector.typeName != "piston" ||
        collector.position != track.position || data.size() < 6 ||
        data[4] <= cells || data[4] > 9999 ||
        data.size() != 6 + 2 * static_cast<std::size_t>(data[4])) return false;
    // omsim stores absolute track cells in int32_t. Widen before adding, and
    // reject unrepresentable geometry rather than comparing wrapped cells.
    const auto absoluteCell = [](const core::HexCoord& origin, std::int32_t q,
                                 std::int32_t r) {
        return std::pair<std::int64_t, std::int64_t>{
            std::int64_t{origin.q} + q, std::int64_t{origin.r} + r};
    };
    const auto representable = [](const auto& cell) {
        constexpr auto low = std::numeric_limits<std::int32_t>::min();
        constexpr auto high = std::numeric_limits<std::int32_t>::max();
        return cell.first >= low && cell.first <= high &&
               cell.second >= low && cell.second <= high;
    };
    std::set<std::pair<std::int64_t, std::int64_t>> oldCells;
    for (int i = 0; i < data[4]; ++i) {
        if (data[5 + 2 * i] != 0 || data[6 + 2 * i] != i - data[4] + 1)
            return false;
        const auto cell = absoluteCell(track.position, 0, data[6 + 2 * i]);
        if (!representable(cell)) return false;
        oldCells.insert(cell);
    }
    for (std::size_t j = 0; j < machine.objects.size(); ++j) {
        const auto& other = machine.objects[j];
        // omsim ANY_ARM also includes both wheels. Unknown programmed devices
        // on this rail are outside the single-collector proof as well.
        if (j != arm && (other.typeName == "piston" || other.typeName.starts_with("arm") ||
                         other.typeName == "baron" || other.typeName == "ravari" ||
                         !other.instructions.empty()) &&
            oldCells.contains({other.position.q, other.position.r})) return false;
        if (j == rail || other.typeName != "track") continue;
        const auto& b = other.bodyData;
        if (b.size() < 6 || b[4] < 0 || b.size() != 6 + 2 * static_cast<std::size_t>(b[4]))
            return false;
        // decode.c links consecutive cells of each track, not adjacent cells
        // belonging to distinct tracks. Only shared absolute cells can change
        // this rail's plus/minus motions (including endpoint/loop handling).
        for (std::size_t i = 0; i < static_cast<std::size_t>(b[4]); ++i) {
            const auto cell = absoluteCell(other.position, b[5 + 2 * i], b[6 + 2 * i]);
            if (!representable(cell) || oldCells.contains(cell)) return false;
        }
    }
    int position = 0;
    std::int32_t previousCycle = -1;
    bool halted = false;
    for (const auto& instruction : collector.instructions) {
        // omsim sorts by cycle; prove the same execution order without editing
        // the tape, and reject duplicate or negative indices conservatively.
        if (halted || instruction.parameter <= previousCycle) return false;
        previousCycle = instruction.parameter;
        switch (instruction.action) {
        case 'a': --position; break;
        case 'A': ++position; break;
        case 'G': case 'g': case 'R': case 'r': break;
        case 'B':
            // decode.c sets the first B as a permanent halt index; sim.c does
            // not wrap halted tapes. Production emits a terminal B. Require
            // return before it, and reject every unreachable suffix.
            if (position != 0) return false;
            halted = true;
            break;
        default: return false;
        }
        if (position > 0 || position < -(cells - 1)) return false;
    }
    // Without B, decode.c includes this tape's full length in the maximum
    // global period. No next iteration can start before this walk finishes.
    // Returning to zero makes the bounded walk inductive; gaps do not move.
    if (position != 0) return false;
    auto shortened = std::vector<std::int32_t>(data.begin(), data.begin() + 5);
    shortened[4] = cells;
    for (int r = 1 - cells; r <= 0; ++r) shortened.insert(shortened.end(), {0, r});
    shortened.push_back(data.back());
    track.bodyData = std::move(shortened);
    return true;
}
} // namespace opusoptima::solvers::detail