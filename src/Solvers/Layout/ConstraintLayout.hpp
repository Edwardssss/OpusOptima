#pragma once

#include "ProductionLayout.hpp"

#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace opusoptima::solvers {
struct ConstraintPlacement {
    core::HexCoord translation{};
    // Zero through five applications of core::RotateClockwise, about local (0,0).
    int rotation{};
    friend bool operator==(const ConstraintPlacement&, const ConstraintPlacement&) = default;
};

struct ConstraintPort {
    core::HexCoord cell{};
    // Axial direction index, or -1 for an unoriented port. Ports need not occupy footprint cells.
    int direction{-1};
};

struct ConstraintModule {
    std::vector<core::HexCoord> footprint;
    std::vector<ConstraintPort> ports;
    std::vector<ConstraintPlacement> domain;
    // When supplied, every occupied cell must belong to this set (empty means no space).
    std::optional<std::vector<core::HexCoord>> allowedCells;
    // Index into ConstraintLayoutProblem::chambers; intersects allowedCells if both are set.
    std::optional<std::size_t> chamber;
};

struct ConstraintHandoff {
    std::size_t firstModule{}, firstPort{}, secondModule{}, secondPort{};
    // Permitted world-space second-port minus first-port offsets. Empty is malformed.
    std::vector<core::HexCoord> offsets{{0, 0}};
    bool requireOppositeFacing{};
};

struct ConstraintLayoutProblem {
    std::vector<ConstraintModule> modules;
    std::vector<core::HexCoord> blockedCells;
    std::vector<ProductionChamberLayout> chambers;
    std::vector<ConstraintHandoff> handoffs;
};

struct ConstraintPlacedModule {
    std::size_t domainIndex{};
    ConstraintPlacement placement;
    std::vector<core::HexCoord> footprint;
    std::vector<ConstraintPort> ports;
};
using ConstraintLayout = std::vector<ConstraintPlacedModule>;

enum class ConstraintLayoutStatus { Found, Exhausted, Unsatisfiable };
enum class ConstraintLayoutFailureCode {
    None,
    InvalidInput,
    NodeLimit,
    CandidateLimit,
    NoAssignment,
    CandidateRejected,
    BlockedCell,
    OutsideAllowedCells,
    OutsideChamber,
    Overlap,
    HandoffMismatch
};
struct ConstraintLayoutFailure {
    ConstraintLayoutFailureCode code{ConstraintLayoutFailureCode::None};
    std::optional<std::size_t> module, otherModule, handoff;
    std::optional<core::HexCoord> cell;
    std::string message;
};
struct ConstraintLayoutOptions {
    // Counts attempted domain entries, including statically rejected entries; preparation is not
    // counted.
    std::size_t maxNodes{100000};
    // Bounds complete candidates sent to accept. A rejection at the limit returns Exhausted
    // only if unexplored assignments remain. Zero prevents even the empty assignment callback.
    std::size_t maxCandidates{1000};
};
struct ConstraintLayoutResult {
    ConstraintLayoutStatus status{ConstraintLayoutStatus::Unsatisfiable};
    std::optional<ConstraintLayout> layout;
    std::size_t nodes{}, candidates{}, rejectedCandidates{};
    ConstraintLayoutFailure failure;
};

// Deterministic depth-first search in module/domain order. accept may reject a complete
// candidate (e.g. after independent simulation), and search continues with the next one.
// No callback means accept the first candidate. Exceptions from callbacks propagate.
// Unsatisfiable means the finite domain, including callback rejections, is exhausted;
// malformed input also returns Unsatisfiable with InvalidInput, before any callback.
// This checks static occupied cells and explicit port relations ONLY, not swept volumes,
// motion, chemistry, timing, reachability, or physical correctness.
[[nodiscard]] ConstraintLayoutResult SearchConstraintLayout(
    const ConstraintLayoutProblem& problem, const ConstraintLayoutOptions& options = {},
    const std::function<bool(const ConstraintLayout&)>& accept = {});

// Cartesian product in translation-major order. Duplicates are retained intentionally.
[[nodiscard]] std::vector<ConstraintPlacement> MakeConstraintPlacementDomain(
    const std::vector<core::HexCoord>& translations, const std::vector<int>& rotations);
} // namespace opusoptima::solvers
