#include "ConstraintLayout.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>

namespace opusoptima::solvers {
namespace {
bool Has(const std::vector<core::HexCoord>& cells, core::HexCoord cell) {
    return std::find(cells.begin(), cells.end(), cell) != cells.end();
}

bool Transform(core::HexCoord cell, const ConstraintPlacement& placement, core::HexCoord& out) {
    std::int64_t q = cell.q, r = cell.r;
    for (int i = 0; i < placement.rotation; ++i) {
        const auto previousQ = q;
        q = -r;
        r = previousQ + r;
    }
    q += placement.translation.q;
    r += placement.translation.r;
    if (q < std::numeric_limits<int>::min() || q > std::numeric_limits<int>::max() ||
        r < std::numeric_limits<int>::min() || r > std::numeric_limits<int>::max())
        return false;
    out = {static_cast<int>(q), static_cast<int>(r)};
    return true;
}

ConstraintLayoutFailure Failure(ConstraintLayoutFailureCode code, const char* message,
                                std::optional<std::size_t> module = {}) {
    ConstraintLayoutFailure failure;
    failure.code = code;
    failure.module = module;
    failure.message = message;
    return failure;
}
} // namespace

std::vector<ConstraintPlacement> MakeConstraintPlacementDomain(
    const std::vector<core::HexCoord>& translations, const std::vector<int>& rotations) {
    std::vector<ConstraintPlacement> result;
    for (auto translation : translations)
        for (auto rotation : rotations)
            result.push_back({translation, rotation});
    return result;
}

ConstraintLayoutResult SearchConstraintLayout(
    const ConstraintLayoutProblem& problem, const ConstraintLayoutOptions& options,
    const std::function<bool(const ConstraintLayout&)>& accept) {
    ConstraintLayoutResult result;
    std::vector<std::vector<ConstraintPlacedModule>> domains(problem.modules.size());
    auto invalid = [&](const char* message, std::optional<std::size_t> module = {}) {
        result.failure = Failure(ConstraintLayoutFailureCode::InvalidInput, message, module);
        return result;
    };
    for (std::size_t m = 0; m < problem.modules.size(); ++m) {
        const auto& module = problem.modules[m];
        if (module.footprint.empty()) return invalid("module footprint is empty", m);
        for (std::size_t i = 0; i < module.footprint.size(); ++i)
            if (std::find(module.footprint.begin(), module.footprint.begin() + i,
                          module.footprint[i]) != module.footprint.begin() + i)
                return invalid("module footprint contains duplicate cells", m);
        if (module.chamber && *module.chamber >= problem.chambers.size())
            return invalid("module chamber index is out of range", m);
        for (const auto& port : module.ports)
            if (port.direction < -1 || port.direction > 5)
                return invalid("port direction must be -1 or 0..5", m);
        for (std::size_t d = 0; d < module.domain.size(); ++d) {
            const auto& placement = module.domain[d];
            if (placement.rotation < 0 || placement.rotation > 5)
                return invalid("placement rotation must be 0..5", m);
            ConstraintPlacedModule placed;
            placed.domainIndex = d;
            placed.placement = placement;
            for (auto cell : module.footprint) {
                core::HexCoord world;
                if (!Transform(cell, placement, world))
                    return invalid("footprint transform overflows", m);
                placed.footprint.push_back(world);
            }
            for (auto port : module.ports) {
                if (!Transform(port.cell, placement, port.cell))
                    return invalid("port transform overflows", m);
                // RotateClockwise maps direction 0 to direction 5.
                if (port.direction >= 0)
                    port.direction = (port.direction + 6 - placement.rotation) % 6;
                placed.ports.push_back(port);
            }
            domains[m].push_back(std::move(placed));
        }
    }
    for (std::size_t h = 0; h < problem.handoffs.size(); ++h) {
        const auto& handoff = problem.handoffs[h];
        if (handoff.firstModule >= problem.modules.size() ||
            handoff.secondModule >= problem.modules.size())
            return invalid("handoff module index is out of range");
        if (handoff.firstPort >= problem.modules[handoff.firstModule].ports.size() ||
            handoff.secondPort >= problem.modules[handoff.secondModule].ports.size())
            return invalid("handoff port index is out of range");
        if (handoff.offsets.empty()) return invalid("handoff has no permitted offsets");
        if (handoff.requireOppositeFacing &&
            (problem.modules[handoff.firstModule].ports[handoff.firstPort].direction < 0 ||
             problem.modules[handoff.secondModule].ports[handoff.secondPort].direction < 0))
            return invalid("facing constraint requires oriented ports");
    }

    ConstraintLayout layout;
    std::vector<std::size_t> next(problem.modules.size(), 0);
    auto exhausted = [&](ConstraintLayoutFailureCode code, const char* message) {
        result.status = ConstraintLayoutStatus::Exhausted;
        result.failure = Failure(code, message);
        return result;
    };
    auto compatible = [&](const ConstraintPlacedModule& placed, std::size_t m) {
        const auto& module = problem.modules[m];
        for (auto cell : placed.footprint) {
            ConstraintLayoutFailureCode code = ConstraintLayoutFailureCode::None;
            if (Has(problem.blockedCells, cell))
                code = ConstraintLayoutFailureCode::BlockedCell;
            else if (module.allowedCells && !Has(*module.allowedCells, cell))
                code = ConstraintLayoutFailureCode::OutsideAllowedCells;
            else if (module.chamber &&
                     !ContainsProductionCell(problem.chambers[*module.chamber], cell))
                code = ConstraintLayoutFailureCode::OutsideChamber;
            if (code != ConstraintLayoutFailureCode::None) {
                result.failure = Failure(code, "occupied cell violates static domain", m);
                result.failure.cell = cell;
                return false;
            }
            for (std::size_t other = 0; other < layout.size(); ++other)
                if (Has(layout[other].footprint, cell)) {
                    result.failure = Failure(ConstraintLayoutFailureCode::Overlap,
                                             "module footprints overlap", m);
                    result.failure.otherModule = other;
                    result.failure.cell = cell;
                    return false;
                }
        }
        for (std::size_t h = 0; h < problem.handoffs.size(); ++h) {
            const auto& handoff = problem.handoffs[h];
            if (handoff.firstModule != m && handoff.secondModule != m) continue;
            if (handoff.firstModule > m || handoff.secondModule > m) continue;
            const auto& first = handoff.firstModule == m ? placed : layout[handoff.firstModule];
            const auto& second = handoff.secondModule == m ? placed : layout[handoff.secondModule];
            const auto& a = first.ports[handoff.firstPort];
            const auto& b = second.ports[handoff.secondPort];
            const auto dq = static_cast<std::int64_t>(b.cell.q) - a.cell.q;
            const auto dr = static_cast<std::int64_t>(b.cell.r) - a.cell.r;
            const bool offsetMatches =
                std::any_of(handoff.offsets.begin(), handoff.offsets.end(),
                            [&](auto offset) { return dq == offset.q && dr == offset.r; });
            if (!offsetMatches ||
                (handoff.requireOppositeFacing && (a.direction + 3) % 6 != b.direction)) {
                result.failure = Failure(ConstraintLayoutFailureCode::HandoffMismatch,
                                         "port spatial relation failed", m);
                result.failure.otherModule =
                    handoff.firstModule == m ? handoff.secondModule : handoff.firstModule;
                result.failure.handoff = h;
                return false;
            }
        }
        return true;
    };

    for (;;) {
        if (layout.size() == problem.modules.size()) {
            if (result.candidates == options.maxCandidates)
                return exhausted(ConstraintLayoutFailureCode::CandidateLimit,
                                 "candidate limit reached");
            ++result.candidates;
            if (!accept || accept(layout)) {
                result.status = ConstraintLayoutStatus::Found;
                result.layout = std::move(layout);
                result.failure = {};
                return result;
            }
            ++result.rejectedCandidates;
            result.failure = Failure(ConstraintLayoutFailureCode::CandidateRejected,
                                     "caller rejected candidate");
            if (layout.empty()) break;
            layout.pop_back();
            continue;
        }
        const auto m = layout.size();
        if (next[m] == domains[m].size()) {
            next[m] = 0;
            if (layout.empty()) break;
            layout.pop_back();
            continue;
        }
        if (result.nodes == options.maxNodes)
            return exhausted(ConstraintLayoutFailureCode::NodeLimit, "node limit reached");
        // Stop before exploring another branch once the caller's candidate budget is spent.
        if (result.candidates == options.maxCandidates)
            return exhausted(ConstraintLayoutFailureCode::CandidateLimit,
                             "candidate limit reached");
        ++result.nodes;
        const auto& placed = domains[m][next[m]++];
        if (compatible(placed, m)) layout.push_back(placed);
    }
    if (result.failure.code == ConstraintLayoutFailureCode::None)
        result.failure = Failure(ConstraintLayoutFailureCode::NoAssignment,
                                 "finite placement domains have no assignment");
    return result;
}
} // namespace opusoptima::solvers
