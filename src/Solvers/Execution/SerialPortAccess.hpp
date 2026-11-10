#pragma once
#include "ExecutionIR.hpp"
#include <algorithm>
#include <array>
#include <set>
#include <utility>
#include <vector>

namespace opusoptima::solvers {
using SerialPortCell = std::pair<int, int>;
// Each witness is a radius-one arm base and its +/-60 degree donor cell.
inline std::vector<std::pair<SerialPortCell, SerialPortCell>> SerialPortApproaches(
    SerialPortCell cell, const std::set<SerialPortCell>& reserved,
    const std::set<SerialPortCell>& feeds, bool allowReservedDonor = false) {
    constexpr std::array<SerialPortCell, 6> directions{
        {{1, 0}, {0, 1}, {-1, 1}, {-1, 0}, {0, -1}, {1, -1}}};
    std::vector<std::pair<SerialPortCell, SerialPortCell>> result;
    for (int grip = 0; grip < 6; ++grip) {
        const SerialPortCell base{cell.first - directions[grip].first,
                                  cell.second - directions[grip].second};
        if (reserved.contains(base)) continue;
        for (int turn : {1, 5}) {
            const auto direction = directions[(grip + turn) % 6];
            const SerialPortCell donor{base.first + direction.first,
                                       base.second + direction.second};
            if (!reserved.contains(donor) || feeds.contains(donor) || allowReservedDonor)
                result.emplace_back(base, donor);
        }
    }
    return result;
}
inline bool SerialPortHasApproach(
    const std::vector<std::pair<SerialPortCell, SerialPortCell>>& approaches,
    const std::set<SerialPortCell>& arms) {
    for (const auto& [base, donor] : approaches)
        if (arms.contains(base) && !arms.contains(donor)) return true;
    return false;
}
// A material source edge must be explicitly cut before either identity is used.
inline bool SerialMaterialSourceCuts(const ExecutionIR& material) {
    using IR = ExecutionIR;
    for (std::size_t i = 0; i < material.steps.size(); ++i) {
        const auto& acquired = material.steps[i];
        if (acquired.operation.kind != IR::Kind::Acquire) continue;
        const auto first = acquired.preconditions.nextId;
        for (const auto& [edge, bits] : acquired.effects.bonds) {
            if (edge.a < first || edge.b < first) continue;
            bool cut = false;
            for (std::size_t j = i + 1; j < material.steps.size(); ++j) {
                const auto& step = material.steps[j];
                const auto& op = step.operation;
                if (std::find(op.atoms.begin(), op.atoms.end(), edge.a) == op.atoms.end() &&
                    std::find(op.atoms.begin(), op.atoms.end(), edge.b) == op.atoms.end())
                    continue;
                cut = bits == 1 && op.kind == IR::Kind::Unbond && op.atoms.size() == 2 &&
                      op.bits == 1 && std::min(op.atoms[0], op.atoms[1]) == edge.a &&
                      std::max(op.atoms[0], op.atoms[1]) == edge.b &&
                      step.preconditions.bonds.contains(edge) &&
                      step.preconditions.bonds.at(edge) == 1 && !step.effects.bonds.contains(edge);
                break;
            }
            if (!cut) return false;
        }
    }
    return true;
}
// Source cuts bind real atom identities to an ordinary unbonder inside the
// protected whole-batch footprint, including the next regenerated batch.
inline bool SerialSourceCutWitness(const ExecutionIR::State& state, ExecutionIR::Edge edge,
                                   std::uint8_t bits,
                                   const std::vector<io::SolutionObject>& objects,
                                   const std::set<SerialPortCell>& feeds,
                                   const std::set<SerialPortCell>& protectedCells,
                                   const std::set<SerialPortCell>& outputs,
                                   std::uint64_t permissions) {
    if (bits != 1 || !(permissions & (1ULL << 9)) || !state.atoms.contains(edge.a) ||
        !state.atoms.contains(edge.b) || !state.bonds.contains(edge) ||
        state.bonds.at(edge) != bits)
        return false;
    const auto& first = state.atoms.at(edge.a).position;
    const auto& second = state.atoms.at(edge.b).position;
    const SerialPortCell a{first.q, first.r}, b{second.q, second.r};
    for (const auto cell : {a, b})
        if (!feeds.contains(cell) || !protectedCells.contains(cell) || outputs.contains(cell))
            return false;
    constexpr std::array<SerialPortCell, 6> directions{
        {{1, 0}, {0, 1}, {-1, 1}, {-1, 0}, {0, -1}, {1, -1}}};
    return std::any_of(objects.begin(), objects.end(), [&](const auto& glyph) {
        if (glyph.typeName != "unbonder" || glyph.bodyData.size() < 2 || glyph.bodyData[1] < 0 ||
            glyph.bodyData[1] >= 6)
            return false;
        const SerialPortCell x{glyph.position.q, glyph.position.r};
        const auto delta = directions[glyph.bodyData[1]];
        const SerialPortCell y{x.first + delta.first, x.second + delta.second};
        return (x == a && y == b) || (x == b && y == a);
    });
}
// Native ordinary bonders act at discrete post-motion poses. This witness
// certifies contacts only; interval, custody and native checks remain required.
class SerialSingletonContacts {
    using IR = ExecutionIR;
    using Cell = SerialPortCell;
    IR::AtomId moving_;
    bool singleton_;
    std::map<Cell, IR::AtomId> stationary_;
    const std::map<Cell, std::vector<Cell>>& endpoints_;
    const std::set<Cell>& outputs_;
    const std::set<Cell>& excluded_;
    const std::map<IR::AtomId, Cell>& targets_;
    const std::map<IR::Edge, std::uint8_t>& expected_;

    public:
    SerialSingletonContacts(const IR::State& state, IR::AtomId moving,
                            const std::map<Cell, std::vector<Cell>>& endpoints,
                            const std::set<Cell>& outputs, const std::set<Cell>& excluded,
                            const std::map<IR::AtomId, Cell>& targets,
                            const std::map<IR::Edge, std::uint8_t>& expected)
        : moving_(moving), singleton_(state.atoms.contains(moving)), endpoints_(endpoints),
          outputs_(outputs), excluded_(excluded), targets_(targets), expected_(expected) {
        for (const auto& [edge, mask] : state.bonds)
            if (edge.a == moving || edge.b == moving) singleton_ = false;
        for (const auto& [id, atom] : state.atoms)
            if (id != moving &&
                !stationary_.emplace(Cell{atom.position.q, atom.position.r}, id).second)
                singleton_ = false;
    }
    [[nodiscard]] bool Allows(Cell next, Cell destination) const {
        if (!singleton_ || stationary_.contains(next) ||
            (outputs_.contains(next) && excluded_.contains(next)))
            return false;
        const auto endpoints = endpoints_.find(next);
        if (endpoints == endpoints_.end()) return true;
        for (const auto neighbor : endpoints->second) {
            const auto other = stationary_.find(neighbor);
            if (other == stationary_.end()) continue;
            const auto a = targets_.find(moving_), b = targets_.find(other->second);
            const IR::Edge edge{std::min(moving_, other->second), std::max(moving_, other->second)};
            const auto bond = expected_.find(edge);
            if (next != destination || a == targets_.end() || b == targets_.end() ||
                a->second != next || b->second != neighbor || bond == expected_.end() ||
                bond->second != 1)
                return false;
        }
        return true;
    }
};
} // namespace opusoptima::solvers
