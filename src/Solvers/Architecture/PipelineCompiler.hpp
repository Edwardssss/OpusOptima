#pragma once

#include "BinaryParser.hpp"
#include "ElementPipeline.hpp"
#include "TickScheduler.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace opusoptima::solvers::architecture {

// ── End-to-end: logical commands -> modules -> schedule -> parts pruning ─────
//
// This is the whole M-A..M-E chain in one place:
//   M-A  `ElementPipeline` produces the geometry-free command sequence,
//   M-B  `ModuleLibrary` turns each stage into a pre-programmed module,
//   M-C  `TickScheduler` aligns the modules inside one period,
//   M-D  `CompilePipelineVariants` enumerates the parameter space,
//   M-E  `SolutionPostProcess` prunes what the machine never uses.
//
// The emitter is deliberately narrow: it only builds machines whose every product
// atom comes straight from a single-atom reagent port (or a calcification glyph).
// That is the `linear/mono` domain the reference solver wins by running several
// product batches in the same period, and it is the first place the scheduler can
// be measured against the legacy "period = sum of atom work" model.

struct PipelineVariant {
    std::string name;
    io::SolutionData solution;
    SchedulePolicy policy{SchedulePolicy::Pipelined};
    /// Which arm radius the placement tries first: compact discs leave room for
    /// the neighbouring atoms, reach-3 discs cut rotations.
    enum class ArmPreference { Compact, Reach };
    ArmPreference armPreference{ArmPreference::Compact};
    /// M-A byproducts, kept so the driver can log and measure them.
    std::size_t consumes{};
    std::size_t generates{};
    std::size_t passThroughs{};
    std::size_t waste{};
    int period{};
    int depth{};
    std::size_t pruned{};
};

/// True when `CompilePipelineMachine` has a domain for this puzzle.
[[nodiscard]] bool SupportsPipelineAssembly(const io::PuzzleData& puzzle);

/// The first reason `SupportsPipelineAssembly` declined, or an empty view when the
/// puzzle is supported. Used for measurement logging only.
[[nodiscard]] std::string_view PipelineDomainReason(const io::PuzzleData& puzzle);

/// Builds one machine. Throws `std::invalid_argument` outside the domain.
[[nodiscard]] io::SolutionData CompilePipelineMachine(
    const io::PuzzleData& puzzle, SchedulePolicy policy,
    PipelineVariant::ArmPreference armPreference, bool postProcess,
    PipelineVariant* detail = nullptr);

/// M-D enumeration: every (schedule, arm preference) setting that builds
/// successfully, in priority order.
[[nodiscard]] std::vector<PipelineVariant> CompilePipelineVariants(const io::PuzzleData& puzzle,
                                                                   bool postProcess);

} // namespace opusoptima::solvers::architecture
