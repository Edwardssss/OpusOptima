#include "FixedArmIntervals.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace opusoptima::solvers {
namespace {
using Coord = core::HexCoord;
Coord Turn(Coord cell, int steps) {
    for (int i = 0; i < steps; ++i)
        cell = core::RotateClockwise(cell);
    return cell;
}
std::int64_t Cross(Coord a, Coord b) {
    return std::int64_t(a.q) * b.r - std::int64_t(a.r) * b.q;
}
double Radius(Coord cell) {
    const auto q = std::int64_t(cell.q), r = std::int64_t(cell.r);
    return std::sqrt(static_cast<double>(q * q + q * r + r * r));
}
bool SweepsCell(Coord from, Coord to, Coord other, bool clockwise) {
    if (other == Coord{}) return true;
    const auto first = Cross(from, other), second = Cross(other, to);
    const bool inside = clockwise ? first >= 0 && second >= 0 : first <= 0 && second <= 0;
    return inside && std::abs(Radius(from) - Radius(other)) < 0.5;
}
} // namespace
bool CertifyFixedArmInterval(const ExecutionIR::State& state, Coord base,
                             const std::vector<Coord>& inputFootprints, const HeldArmState& from,
                             const HeldArmState& to, char instruction, bool holding) {
    if (from.trackIndex || to.trackIndex || from.length < 1 || from.length > 3 ||
        to.length != from.length)
        return false;
    if (instruction == 'G' || instruction == 'g') return from == to;
    const bool pivot = instruction == 'P' || instruction == 'p';
    if (instruction != 'R' && instruction != 'r' && !pivot) return false;
    if (pivot && !holding) return false;
    const int turns = instruction == 'r' || instruction == 'p' ? 1 : 5;
    if (to.direction != (from.direction + (pivot ? 0 : turns)) % 6 ||
        (holding && to.fragmentRotation != (from.fragmentRotation + turns) % 6))
        return false;
    const auto fromTip = core::Add(base, Turn({from.length, 0}, from.direction));
    const auto toTip = core::Add(base, Turn({to.length, 0}, to.direction));
    std::vector<ExecutionIR::AtomId> moving;
    if (holding) {
        const auto grip =
            std::find_if(state.atoms.begin(), state.atoms.end(),
                         [&](const auto& entry) { return entry.second.position == fromTip; });
        if (grip == state.atoms.end()) return false;
        moving = ExecutionIR::Fragment(state, grip->first);
    }
    const auto center = pivot ? fromTip : base;
    auto obstacle = [&](Coord oldCell, Coord newCell, Coord occupied) {
        if (occupied == base) return true;
        if (occupied == oldCell || (!holding && occupied == newCell)) return false;
        return occupied == newCell ||
               SweepsCell(core::Subtract(oldCell, center), core::Subtract(newCell, center),
                          core::Subtract(occupied, center), turns == 1);
    };
    auto check = [&](Coord oldCell, Coord newCell) {
        if (newCell == base) return false;
        for (const auto& [id, atom] : state.atoms) {
            if (std::find(moving.begin(), moving.end(), id) != moving.end()) continue;
            if (obstacle(oldCell, newCell, atom.position)) return false;
        }
        for (const auto feed : inputFootprints) {
            if (std::any_of(moving.begin(), moving.end(),
                            [&](auto id) { return state.atoms.at(id).position == feed; }))
                continue;
            if (obstacle(oldCell, newCell, feed)) return false;
        }
        return true;
    };
    if (!holding) return check(fromTip, toTip);
    for (const auto id : moving) {
        const auto oldCell = state.atoms.at(id).position;
        const auto newCell = core::Add(center, Turn(core::Subtract(oldCell, center), turns));
        if (pivot && SweepsCell(core::Subtract(oldCell, center), core::Subtract(newCell, center),
                                core::Subtract(base, center), turns == 1))
            return false;
        if (!check(oldCell, newCell)) return false;
    }
    return true;
}
} // namespace opusoptima::solvers
