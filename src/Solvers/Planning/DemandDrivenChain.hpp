#pragma once
#include "BaseSolver.hpp"
#include <chrono>
#include <optional>
#include <stdexcept>

namespace opusoptima::solvers {
[[nodiscard]] io::SolutionData CompileDemandDrivenChain(const io::PuzzleData& puzzle);
// Legacy three/four-atom stations retain their bytes. The direct-feed fallback checks
// at most 36 anchored rotation pairs, with 16 atoms, 36 edges and life-anchor radius 3 per output.
[[nodiscard]] io::SolutionData CompileDemandDrivenConsumers(const io::PuzzleData& puzzle);
[[nodiscard]] io::SolutionData CompileDemandDrivenConsumers(
    const io::PuzzleData& puzzle, std::chrono::steady_clock::time_point deadline);
enum class StaticReactionFamily { Purification, LegacyConsumers, OrdinaryPair };
struct StaticReactionResult {
    io::SolutionData solution;
    StaticReactionFamily family;
};
// Provenance is returned only with a successfully compiled machine.
[[nodiscard]] StaticReactionResult CompileStaticReactionWithFamily(
    const io::PuzzleData& puzzle,
    std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::time_point::max());
enum class StaticEmbeddingSuppressionReason { AcceptedSingletonPurification };
// Suppression requires actual native acceptance and the same singleton purification triangle.
// Genuine direct target feeds, other families and molecular/multiple products remain eligible.
[[nodiscard]] std::optional<StaticEmbeddingSuppressionReason> StaticEmbeddingSuppressionAfterReaction(
    const io::PuzzleData& puzzle, StaticReactionFamily family, const io::VerificationReport& report);
// The bounded static reaction slot also compiles one-to-five-step singleton metal purification.
[[nodiscard]] io::SolutionData CompileStaticReaction(const io::PuzzleData& puzzle);
[[nodiscard]] io::SolutionData CompileStaticReaction(
    const io::PuzzleData& puzzle, std::chrono::steady_clock::time_point deadline);
// Stable first-observed rejection only; no inference about later construction stages.
enum class RigidCarrierReason {
    InputHeader, Permissions, SourceGraph, TargetGraph, ExactCoverImpossible,
    SearchNodeCap, Deadline, NoMultiatomCover
};
[[nodiscard]] const char* RigidCarrierReasonName(RigidCarrierReason reason) noexcept;
class RigidCarrierUnsupported : public std::invalid_argument {
public:
    explicit RigidCarrierUnsupported(RigidCarrierReason reason);
    [[nodiscard]] RigidCarrierReason reason() const noexcept { return reason_; }
private:
    RigidCarrierReason reason_;
};
struct RigidCarrierPreflight {
    enum class Status { Unsupported, Unknown };
    Status status;
    std::optional<RigidCarrierReason> reason;
};
// Necessary conditions only: Unknown must still pass construction and native verification.
[[nodiscard]] RigidCarrierPreflight PreflightRigidCarrier(
    const io::PuzzleData& puzzle,
    std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::time_point::max());
enum class RigidCarrierFamily { WholeMolecule, StaticPartition, StaticCopy };
struct RigidCarrierResult {
    io::SolutionData solution;
    RigidCarrierFamily family;
};
// Static partition consumes every source atom across at most four original output ports.
// Beyond the legacy eight-input/four-output header, only exact whole-input copies are
// admitted, bounded to sixteen inputs/eight outputs. Pose attempts share 20,000 nodes
// and the caller deadline; legacy cover and partition bounds remain unchanged.
[[nodiscard]] RigidCarrierResult CompileRigidCarrierWithFamily(
    const io::PuzzleData& puzzle,
    std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::time_point::max());
// Finite whole-molecule tiling; typed Unsupported retains invalid_argument compatibility.
[[nodiscard]] io::SolutionData CompileRigidCarrier(
    const io::PuzzleData& puzzle,
    std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::time_point::max());
[[nodiscard]] bool StaticEmbeddingMissingLifeFeed(const io::PuzzleData& puzzle);
// Direct singleton feeds and bounded static chemistry share 576 placement checks and the caller deadline.
// One-to-five-step metal purification or one-step projection composes with direct ordinary neighbors.
// Purification reserves each entire triangle and retains existing direct/adjacent supply priority.
// Projection reserves isolated Mercury beside the metal product; mixed derived chemistry is unsupported.
[[nodiscard]] io::SolutionData CompileStaticEmbedding(
    const io::PuzzleData& puzzle,
    std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::time_point::max());
}
