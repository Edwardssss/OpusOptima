#pragma once

#include "ExecutionIR.hpp"
#include "SolutionVerifier.hpp"
#include <optional>

namespace opusoptima::solvers {

// Material dependencies and identities are independent of chamber, arm and layout.
// ExecutionIR remains the authority for material transitions; this projection is
// useful for scheduling and cannot certify a physical machine.
struct ExecutionPlan {
    using AtomId = ExecutionIR::AtomId;
    struct Origin {
        std::optional<std::size_t> input;
        std::size_t batch{}, sourceAtom{};
        std::vector<AtomId> reagents;
        friend bool operator==(const Origin&, const Origin&) = default;
    };
    struct MaskChange {
        ExecutionIR::Edge edge;
        std::uint8_t retained{}, removed{}, added{};
    };
    struct Task {
        std::size_t step{};
        ExecutionIR::Kind kind{};
        std::vector<AtomId> inputs, products;
        std::vector<AtomId> retainedInputs, consumedInputs;
        std::vector<MaskChange> edges;
        std::vector<std::size_t> dependsOn;
        std::optional<std::size_t> output, outputOccurrence;
        std::optional<std::size_t> buffer;
        std::optional<ExecutionIR::WheelCatalyst> wheelCatalyst;
    };
    struct CacheLifetime {
        std::size_t slot{}, storedAt{}, retrievedAt{};
        std::vector<AtomId> fragment;
    };
    struct RetentionEndpoint {
        std::size_t relocateStep{}, storeStep{}, slot{};
        AtomId atom{};
        core::AtomType type{};
        core::HexCoord logicalPosition{};
        friend bool operator==(const RetentionEndpoint&, const RetentionEndpoint&) = default;
    };
    std::vector<RetentionEndpoint> retentionEndpoints;
    std::optional<ExecutionIR::FiniteRetention> finiteTerminal;
    ExecutionIR material;
    std::map<AtomId, Origin> origins;
    std::vector<Task> tasks;
    std::vector<CacheLifetime> caches;
};

// The caller passes a material-only puzzle view; production assignments belong
// exclusively to the physical plan. Rejects altered snapshots and open buffers.
[[nodiscard]] ExecutionPlan MakeExecutionPlan(const io::PuzzleData& materialPuzzle,
                                              ExecutionIR material);

// Explicit single-output six-product task; never a closed-period certificate.
// Every bound retention delivery/Store must precede the completing Output.
// Terminal inventory must hold before native stop; trailing reset is not required
// to execute. This contract does not generalize to multiple output ports.
[[nodiscard]] ExecutionPlan MakeFiniteExecutionPlan(const io::PuzzleData&, ExecutionIR,
    ExecutionIR::FiniteRetention, io::VerificationMode);

// A physical implementation choice is deliberately separate from material
// scheduling. This records proposed sites, custody and timing, not a certificate.
struct PhysicalPlan {
    enum class Mode { Rail, FixedArm, Handoff, Conduit };
    struct Assignment {
        std::size_t task{};
        Mode mode{};
        std::optional<std::size_t> chamber, arm, port;
        std::optional<ExecutionPlan::AtomId> heldAtom;
        core::HexCoord site{};
        int orientation{}, begin{}, end{};
    };
    std::vector<Assignment> assignments;
};

// Checks step coverage, dependency ordering and exclusive arm intervals only.
// Motion, glyphs and production transfer require a separate physical certificate.
void ValidatePhysicalPlan(const ExecutionPlan& material, const PhysicalPlan& physical);

} // namespace opusoptima::solvers
