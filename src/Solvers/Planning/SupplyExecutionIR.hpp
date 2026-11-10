#pragma once

#include "AssemblySolver.hpp"
#include "ExecutionIR.hpp"
#include "HexFrame.hpp"
#include "MaterialPolicy.hpp"

namespace opusoptima::solvers {

// The cut policy requires ordinary singleton/dimer sources, ordinary multiatom
// outputs and bonder permission. Nonsingleton sources also require permission 9.
// Physical layout, cut-before-use certification and native acceptance remain required.
[[nodiscard]] bool SupportsOrdinarySourceCutLowering(const io::PuzzleData& puzzle);

/// Translate one exact supply period into material operations. This is not a
/// mechanical compilation: relocation frames and storage slots remain logical.
/// Rejects unsupported schedules or inconsistent accounting rather than
/// silently replanning or dropping source-batch remainder.
/// outputFrame maps the output port's local molecule into the supplied layout.
/// inputFrames maps each original input port into world coordinates; an empty
/// vector means identity for all ports. Frame rotations normalize modulo six;
/// the generated IR retains canonical turns in [0, 5].
[[nodiscard]] ExecutionIR BuildSupplyExecutionIR(
    const io::PuzzleData& puzzle, const AssemblyPlan& assembly, const SupplyPlan& supply,
    core::HexFrame outputFrame = {}, const std::vector<core::HexFrame>& inputFrames = {});

struct JointSupplyPlan;

/// Preserved direct-delivery fragments may have independent output rotations;
/// atoms joined by a retained edge must agree on the fragment rotation. Interleaved
/// operations on other atoms do not truncate same-batch fragment reservations.
/// Compile a shared-inventory period with explicit output-port identities.
/// Static production puzzles retain their original productionInfo for material
/// replay; this adapter neither assigns rooms nor performs conduit transfers.
/// Empty frame vectors mean identity frames. Every output must close the same
/// period; all acquired atoms must be consumed, with no remaining buffers.
[[nodiscard]] ExecutionIR BuildJointSupplyExecutionIR(
    const io::PuzzleData& puzzle, const JointSupplyPlan& supply,
    const std::vector<core::HexFrame>& outputFrames = {},
    const std::vector<core::HexFrame>& inputFrames = {});

// Explicit cuts use the original source identities and frames. Target bonds are
// recreated by ordinary Bond operations. Retained products and wheel bindings
// are excluded. Existing entry points retain PreserveCompatible behavior.
[[nodiscard]] ExecutionIR BuildSupplyExecutionIRWithPolicy(
    const io::PuzzleData& puzzle, const AssemblyPlan& assembly, const SupplyPlan& supply,
    SupplyMaterialLoweringPolicy policy, core::HexFrame outputFrame = {},
    const std::vector<core::HexFrame>& inputFrames = {});
[[nodiscard]] ExecutionIR BuildJointSupplyExecutionIRWithPolicy(
    const io::PuzzleData& puzzle, const JointSupplyPlan& supply,
    SupplyMaterialLoweringPolicy policy,
    const std::vector<core::HexFrame>& outputFrames = {},
    const std::vector<core::HexFrame>& inputFrames = {});

} // namespace opusoptima::solvers
