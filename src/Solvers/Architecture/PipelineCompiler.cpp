#include "PipelineCompiler.hpp"

#include "LogManager.hpp"
#include "ModuleLibrary.hpp"
#include "SolutionPostProcess.hpp"
#include "SwingKernel.hpp"

#include <algorithm>
#include <climits>
#include <cstdlib>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_set>

namespace opusoptima::solvers::architecture {
namespace {

using layout::SwingRotation;
using layout::SwingStep;

constexpr std::size_t kMaxPipelineAtoms = 16;
// Bits the native gate requires for these machines: arm, bonder, and the tape
// instructions the swing uses — rotate/drop (22), grab (23), reset `X` (24) and
// no-op `O` (25). See `decode.c: parts_available_bits_for_instruction`.
constexpr std::uint64_t kArmPermissions =
    (1ULL << 0) | (1ULL << 8) | (1ULL << 22) | (1ULL << 23) | (1ULL << 24) | (1ULL << 25);
constexpr std::uint64_t kCalcifyPermission = 1ULL << 12;
constexpr std::uint64_t kProjectionPermission = 1ULL << 14;
constexpr std::uint64_t kPurificationPermission = 1ULL << 15;

struct StationPlan {
    core::AtomType element{core::AtomType::Salt};
    core::HexCoord target{};
    std::size_t port{};
    bool calcify{};
    /// Projection only: the quicksilver port and the fixed upgrade count.
    std::size_t auxPort{};
    bool project{};
    int steps{};
    /// Projection only: the glyph cell (it also holds the resident quicksilver).
    core::HexCoord glyph{};
    /// Purification only: the glyph cell and the second metal cell.
    bool purify{};
    core::HexCoord secondMetal{};
    layout::Swing swing{};
};

struct PortPlan {
    std::size_t port{};
    bool calcify{};
    std::size_t auxPort{};
    bool project{};
    int steps{};
    bool purify{};
};

/// The supply chain of `element` must resolve to one of the shapes this compiler
/// can emit without a generator module:
///
///  * `[Feed]`              a bare single-atom port,
///  * `[Feed, Calcify]`     a port feeding a calcification glyph,
///  * `[Feed, Feed, Project]` a metal port plus a quicksilver port feeding a
///    projection glyph. This is the first module with a non-zero latency: the
///    metal needs `steps` cycles on the glyph before it is the product, which is
///    exactly what the tick scheduler aligns against.
[[nodiscard]] std::optional<PortPlan> ResolvePort(const io::PuzzleData& puzzle,
                                                 const ElementPipeline& pipeline,
                                                 core::AtomType element) {
    const auto& chains = pipeline.Chains();
    const auto found = chains.find(element);
    if (found == chains.end() || found->second.empty()) return std::nullopt;
    const auto& stages = pipeline.Stages();
    const auto& chain = found->second;
    const auto& feed = stages[chain.front()];
    if (feed.kind != StageKind::Feed || feed.feed >= puzzle.inputs.size()) return std::nullopt;
    {
        const auto& molecule = puzzle.inputs[feed.feed];
        if (molecule.atoms.size() != 1 || !molecule.bonds.empty()) return std::nullopt;
    }
    PortPlan plan;
    plan.port = feed.feed;
    if (chain.size() == 1) return plan;
    if (chain.size() == 2) {
        const auto& second = stages[chain[1]];
        if (second.outputs.size() != 1 || second.outputs.front() != element)
            return std::nullopt;
        if (second.kind == StageKind::Calcify) {
            if ((puzzle.permissionBits & kCalcifyPermission) == 0) return std::nullopt;
            plan.calcify = true;
            return plan;
        }
        if (second.kind == StageKind::Purify) {
            // Two equal non-gold metals fuse into the next metal up. The glyph
            // takes one metal on its own cell and one on its east neighbour.
            if ((puzzle.permissionBits & kPurificationPermission) == 0) return std::nullopt;
            plan.purify = true;
            return plan;
        }
        return std::nullopt;
    }
    if (chain.size() < 3) return std::nullopt;
    const auto& auxiliary = stages[chain[1]];
    if (auxiliary.kind != StageKind::Feed || auxiliary.feed >= puzzle.inputs.size() ||
        auxiliary.outputs.size() != 1 ||
        auxiliary.outputs.front() != core::AtomType::Quicksilver)
        return std::nullopt;
    {
        const auto& molecule = puzzle.inputs[auxiliary.feed];
        if (molecule.atoms.size() != 1 || !molecule.bonds.empty()) return std::nullopt;
    }
    // `chain[2..]` must be one contiguous metal-projection run. A single glyph
    // upgrades in place, one rank per cycle, so the whole run collapses into one
    // station whose latency is `start_rank - target_rank`.
    int from = -1;
    int previous = -1;
    for (std::size_t step = 2; step < chain.size(); ++step) {
        const auto& project = stages[chain[step]];
        if ((puzzle.permissionBits & kProjectionPermission) == 0) return std::nullopt;
    if (project.kind != StageKind::Project || project.inputs.size() != 2 ||
            project.inputs[1] != core::AtomType::Quicksilver || project.outputs.size() != 1)
            return std::nullopt;
        if (step == 2) {
            from = static_cast<int>(project.inputs[0]);
        } else if (static_cast<int>(project.inputs[0]) != previous) {
            return std::nullopt;
        }
        previous = static_cast<int>(project.outputs.front());
    }
    if (previous != static_cast<int>(element)) return std::nullopt;
    if (from <= previous) return std::nullopt;
    // The quicksilver feed must be a different input from the metal feed.
    if (auxiliary.feed == feed.feed) return std::nullopt;
    plan.project = true;
    plan.auxPort = auxiliary.feed;
    plan.steps = from - previous;
    return plan;
}

/// Picks one swing per product atom.
///
/// Two placement rules, matching the two scheduling models:
///
///  * `simultaneous` (pipelined/staggered): every arm works in the same period, so
///    no arm's body disc may contain another arm's cells or a product cell other
///    than its own target. Sound by construction, but a fully surrounded atom (a
///    star frame's centre) has no such swing.
///  * `sequential` (serial): arms move one after another, so a swing may pass over
///    cells that are still empty air; only the cells that are already occupied —
///    dropped product atoms, arm posts and input ports — constrain it.
void PlanStations(const io::PuzzleData& puzzle, const ElementPipeline& pipeline,
                  const std::vector<PortPlan>& ports, PipelineVariant::ArmPreference preference,
                  bool simultaneous, std::vector<StationPlan>& stations) {
    const auto& output = puzzle.outputs.front();
    std::unordered_set<core::HexCoord, core::HexCoordHash> productCells;
    for (const auto& atom : output.atoms) productCells.insert(atom.position);

    std::unordered_set<core::HexCoord, core::HexCoordHash> blocked;
    std::unordered_set<core::HexCoord, core::HexCoordHash> placedTargets;
    std::unordered_set<core::HexCoord, core::HexCoordHash> usedStations;
    const auto& atoms = pipeline.Atoms();
    for (std::size_t index = 0; index < atoms.size(); ++index) {
        StationPlan plan;
        plan.element = atoms[index].type;
        plan.target = output.atoms[atoms[index].atom].position;
        plan.port = ports[index].port;
        plan.calcify = ports[index].calcify;
        plan.project = ports[index].project;
        plan.auxPort = ports[index].auxPort;
        plan.steps = ports[index].steps;
        plan.purify = ports[index].purify;
        auto candidates = layout::SwingCandidates(plan.target);
        // Compact: a radius-1 disc is 7 cells and leaves room for the neighbouring
        // atoms. Reach: a radius-3 disc turns fewer times but blocks more cells.
        std::stable_sort(candidates.begin(), candidates.end(),
                         [&](const layout::Swing& left, const layout::Swing& right) {
                             if (left.length != right.length) {
                                 if (preference == PipelineVariant::ArmPreference::Compact)
                                     return left.length < right.length;
                                 return left.length > right.length;
                             }
                             return std::abs(SwingRotation(left.grab, left.drop)) <
                                    std::abs(SwingRotation(right.grab, right.drop));
                         });
        bool placed = false;
        for (const auto& swing : candidates) {
            // A projection station also needs the glyph cell due west of the metal
            // cell, and that cell stays occupied by the resident quicksilver.
            core::HexCoord glyph{};
            core::HexCoord secondMetal{};
            if (plan.project) {
                glyph = core::Add(swing.station, layout::SwingStep(3));
                if (glyph == swing.target || productCells.count(glyph) != 0 ||
                    blocked.count(glyph) != 0)
                    continue;
            } else if (plan.purify) {
                // The glyph sits south of the grab cell, so its two metal cells are
                // the glyph itself and its east neighbour.
                glyph = core::Add(swing.station, layout::SwingStep(4));
                secondMetal = core::Add(glyph, layout::SwingStep(0));
                if (glyph == swing.target || secondMetal == swing.target ||
                    productCells.count(glyph) != 0 || productCells.count(secondMetal) != 0 ||
                    blocked.count(glyph) != 0 || blocked.count(secondMetal) != 0 ||
                    glyph == swing.station || secondMetal == swing.station)
                    continue;
            }
            bool clash = false;
            if (simultaneous) {
                for (const auto& cell : layout::SwingDisc(swing.pivot, swing.length)) {
                    if (cell == swing.target) continue;
                    if ((plan.project && cell == glyph) ||
                        (plan.purify && (cell == glyph || cell == secondMetal))) {
                        clash = true;
                        break;
                    }
                    if (productCells.count(cell) != 0 || blocked.count(cell) != 0) {
                        clash = true;
                        break;
                    }
                }
            } else {
                if (blocked.count(swing.pivot) != 0) clash = true;
                if (productCells.count(swing.pivot) != 0) clash = true;
                if (usedStations.count(swing.station) != 0) clash = true;
                if (usedStations.count(swing.pivot) != 0) clash = true;
                if (!clash)
                    for (const auto& delta :
                         layout::SwingSectorCells(swing.length, swing.grab, swing.drop)) {
                        const auto cell = core::Add(swing.pivot, delta);
                        if (cell == swing.target) continue;
                        if ((plan.project && cell == glyph) ||
                            (plan.purify && (cell == glyph || cell == secondMetal))) {
                            clash = true;
                            break;
                        }
                        if (blocked.count(cell) != 0 || productCells.count(cell) != 0) {
                            clash = true;
                            break;
                        }
                    }
            }
            if (clash) continue;
            plan.swing = swing;
            plan.glyph = glyph;
            plan.secondMetal = secondMetal;
            placed = true;
            break;
        }
        if (!placed)
            throw std::invalid_argument("pipeline assembly cannot place a swing for an atom");
        if (simultaneous) {
            for (const auto& cell : layout::SwingDisc(plan.swing.pivot, plan.swing.length))
                blocked.insert(cell);
        } else {
            blocked.insert(plan.swing.pivot);
            for (const auto& delta : layout::SwingSectorCells(
                     plan.swing.length, plan.swing.grab, plan.swing.drop))
                blocked.insert(core::Add(plan.swing.pivot, delta));
            placedTargets.insert(plan.swing.target);
        }
        if (plan.project) blocked.insert(plan.glyph);
        if (plan.purify) {
            blocked.insert(plan.glyph);
            blocked.insert(plan.secondMetal);
        }
        usedStations.insert(plan.swing.station);
        stations.push_back(plan);
    }
}

/// One balanced arm tape: grab, turn to the drop face, release, turn back, wait,
/// reset. `start` delays the whole swing inside the period.
[[nodiscard]] std::vector<io::ArmInstruction> SwingTape(int grab, int drop, int period, int start) {
    const int rotation = SwingRotation(grab, drop);
    const int turns = rotation > 0 ? rotation : -rotation;
    std::vector<io::ArmInstruction> tape;
    int at = 0;
    while (at < start) tape.push_back({'O', at++});
    tape.push_back({'G', at++});
    for (int step = 0; step < turns; ++step) tape.push_back({rotation > 0 ? 'r' : 'R', at++});
    tape.push_back({'g', at++});
    for (int step = 0; step < turns; ++step) tape.push_back({rotation > 0 ? 'R' : 'r', at++});
    while (at < period - 1) tape.push_back({'O', at++});
    tape.push_back({'X', at++});
    return tape;
}

[[nodiscard]] io::SolutionObject MakeObject(std::string type, core::HexCoord position,
                                            std::vector<int> body,
                                            std::vector<io::ArmInstruction> tape = {}) {
    io::SolutionObject object;
    object.typeName = std::move(type);
    object.position = position;
    object.bodyData = std::move(body);
    object.instructions = std::move(tape);
    return object;
}

} // namespace

std::string_view PipelineDomainReason(const io::PuzzleData& puzzle) {
    if (puzzle.productionInfo) return "production puzzle";
    if (puzzle.outputs.size() != 1) return "more than one output";
    if ((puzzle.permissionBits & kArmPermissions) != kArmPermissions)
        return "missing arm/bonder/rotate/grab permission";
    if (puzzle.outputMultiplier <= 0) return "non-positive multiplier";
    const auto& output = puzzle.outputs.front();
    if (output.atoms.empty()) return "empty product";
    if (output.atoms.size() > kMaxPipelineAtoms) return "product larger than six atoms";
    for (const auto& atom : output.atoms)
        if (atom.type == core::AtomType::Repeat) return "repeating output";
    for (const auto& bond : output.bonds) {
        if (bond.bits != 1) return "non-ordinary bond";
        if (layout::SwingDirectionOf(core::Subtract(bond.to, bond.from)) < 0)
            return "non-adjacent bond";
    }
    return {};
}

bool SupportsPipelineAssembly(const io::PuzzleData& puzzle) {
    return PipelineDomainReason(puzzle).empty();
}

io::SolutionData CompilePipelineMachine(const io::PuzzleData& puzzle, SchedulePolicy policy,
                                        PipelineVariant::ArmPreference armPreference,
                                        bool postProcess, PipelineVariant* detail) {
    if (!SupportsPipelineAssembly(puzzle))
        throw std::invalid_argument("pipeline assembly: puzzle outside the supported domain");
    const auto& output = puzzle.outputs.front();

    // ── M-A: logical command sequence ───────────────────────────────────────
    const auto pipeline = ElementPipeline::Build(puzzle);
    const auto dryRun = pipeline.Run();
    const auto& atoms = pipeline.Atoms();
    std::vector<PortPlan> ports;
    ports.reserve(atoms.size());
    for (std::size_t index = 0; index < atoms.size(); ++index) {
        const auto plan = ResolvePort(puzzle, pipeline, atoms[index].type);
        if (!plan) {
            if (std::getenv("OPUSOPTIMA_PIPELINE_TRACE")) {
                std::string trace;
                const auto found = pipeline.Chains().find(atoms[index].type);
                if (found != pipeline.Chains().end())
                    for (const auto stage : found->second) {
                        const auto& spec = pipeline.Stages()[stage];
                        trace += std::string(StageKindName(spec.kind));
                        trace += '(';
                        for (const auto in : spec.inputs) {
                            trace += std::string(core::AtomTypeName(in));
                            trace += ' ';
                        }
                        trace += "-> ";
                        for (const auto out : spec.outputs) {
                            trace += std::string(core::AtomTypeName(out));
                            trace += ' ';
                        }
                        trace += ") ";
                    }
                GetLoggerOrFallback("solver")->info("pipeline-trace element={} chain={}",
                                                    core::AtomTypeName(atoms[index].type), trace);
            }
            throw std::invalid_argument(
                std::string("pipeline assembly: no portable supply for element ") +
                std::string(core::AtomTypeName(atoms[index].type)));
        }
        ports.push_back(*plan);
    }

    // ── M-B: modules ────────────────────────────────────────────────────────
    std::vector<StationPlan> stations;
    PlanStations(puzzle, pipeline, ports, armPreference,
                 policy != SchedulePolicy::Serial, stations);

    std::vector<ModuleShape> arms;
    arms.reserve(stations.size());
    for (const auto& station : stations)
        arms.push_back(BuildAtomConveyor(station.swing.pivot, station.swing.grab, station.swing.drop,
                                         station.swing.length));

    // ── M-C: tick schedule ──────────────────────────────────────────────────
    // Generator latency: a projection station needs `steps` cycles on the glyph
    // before its metal is the product, plus one cycle for the next metal to land.
    // This is the first place the scheduler's per-module offset means something.
    std::vector<int> delays(stations.size(), 0);
    for (std::size_t index = 0; index < stations.size(); ++index) {
        if (stations[index].project) delays[index] = stations[index].steps + 1;
        else if (stations[index].calcify || stations[index].purify) delays[index] = 1;
    }
    const auto schedule = TickScheduler::Align(arms, policy);
    // One extra tick per arm for the trailing `X` (reset), which must not share a
    // tick with the swing: the tape is [start .. start+work-1] then `X` at
    // `period-1`.
    int period = std::max(3, schedule.period);
    for (std::size_t index = 0; index < arms.size(); ++index)
        period = std::max(period, schedule.offsets[index] + delays[index] +
                                      arms[index].period + 1);

    // ── emit ────────────────────────────────────────────────────────────────
    io::SolutionData machine;
    machine.puzzleName = puzzle.name;
    machine.solutionName = "Pipeline assembly";
    for (std::size_t index = 0; index < stations.size(); ++index) {
        const auto& station = stations[index];
        // The input object's origin is offset so the reagent atom lands on the
        // station cell (molecule coordinates are local to the object).
        const auto& molecule = puzzle.inputs[station.port];
        const auto local = molecule.atoms.front().position;
        // A purification station draws its metal onto the glyph's two metal cells,
        // not onto the grab cell: the grab cell is where the fused metal appears.
        if (!station.purify)
            machine.objects.push_back(MakeObject(
                "input", core::Subtract(station.swing.station, local),
                {1, 0, static_cast<int>(station.port), 0, 0}));
        if (station.calcify)
            machine.objects.push_back(
                MakeObject("glyph-calcification", station.swing.station, {1, 0, 0, 0, 0}));
        if (station.purify) {
            // Both metal cells draw from the same single-atom port, so the glyph
            // receives a pair every cycle and yields one upgraded metal.
            for (const auto cell : {station.glyph, station.secondMetal})
                machine.objects.push_back(MakeObject("input", core::Subtract(cell, local),
                                                     {1, 0, static_cast<int>(station.port), 0, 0}));
            machine.objects.push_back(MakeObject("glyph-purification", station.glyph, {1, 0, 0, 0, 0}));
        }
        if (station.project) {
            // The glyph sits west of its metal cell and keeps a resident quicksilver
            // atom on its own cell; the metal upgrades one rank per cycle.
            const auto& auxiliary = puzzle.inputs[station.auxPort];
            const auto auxiliaryLocal = auxiliary.atoms.front().position;
            machine.objects.push_back(
                MakeObject("input", core::Subtract(station.glyph, auxiliaryLocal),
                           {1, 0, static_cast<int>(station.auxPort), 0, 0}));
            machine.objects.push_back(MakeObject("glyph-projection", station.glyph, {1, 0, 0, 0, 0}));
        }
        const int start = (schedule.offsets[index] + delays[index]) % period;
        machine.objects.push_back(MakeObject(
            "arm1", station.swing.pivot,
            {station.swing.length, station.swing.grab, 0, 0, 0},
            SwingTape(station.swing.grab, station.swing.drop, period, start)));
    }
    for (const auto& bond : output.bonds) {
        const int facing = layout::SwingDirectionOf(core::Subtract(bond.to, bond.from));
        machine.objects.push_back(MakeObject("bonder", bond.from, {1, facing, 0, 0, 0}));
    }
    // Exactly one output area: the native decoder requires one `out-std` per
    // puzzle output, so extra copies of the product frame are not admissible.
    machine.objects.push_back(MakeObject("out-std", core::HexCoord{0, 0}, {1, 0, 0, 0, 0}));

    if (std::getenv("OPUSOPTIMA_PIPELINE_DUMP")) {
        for (const auto& object : machine.objects) {
            std::string tape;
            for (const auto& instruction : object.instructions) {
                tape += instruction.action;
                tape += std::to_string(instruction.parameter);
                tape += ' ';
            }
            std::string body;
            for (const int value : object.bodyData) body += std::to_string(value) + ",";
            GetLoggerOrFallback("solver")->info(
                "pipeline-object type={} at=({},{}) body=[{}] tape=[{}] period={}",
                object.typeName, object.position.q, object.position.r, body, tape, period);
        }
    }

    // ── M-E: post-processing ────────────────────────────────────────────────
    PruneReport report;
    auto result = postProcess ? SolutionPostProcess::CostOptimize(machine, &report)
                              : std::move(machine);
    result.puzzleName = puzzle.name;
    result.solutionName = "Pipeline assembly";

    if (detail) {
        detail->name = std::string("schedule=") + SchedulePolicyName(policy) + "; arms=" +
                       (armPreference == PipelineVariant::ArmPreference::Compact ? "compact"
                                                                                : "reach") +
                       "; period=" + std::to_string(period);
        detail->policy = policy;
        detail->armPreference = armPreference;
        detail->consumes = dryRun.consumes;
        detail->generates = dryRun.generates;
        detail->passThroughs = dryRun.passThroughs;
        std::size_t waste = 0;
        for (const auto& [element, count] : dryRun.waste) {
            (void)element;
            waste += count;
        }
        detail->waste = waste;
        detail->period = period;
        detail->depth = dryRun.depth;
        detail->pruned = report.Count();
    }
    return result;
}

std::vector<PipelineVariant> CompilePipelineVariants(const io::PuzzleData& puzzle,
                                                     bool postProcess) {
    std::vector<PipelineVariant> variants;
    if (const auto reason = PipelineDomainReason(puzzle); !reason.empty()) {
        GetLoggerOrFallback("solver")->info(
            "Assembly branch=pipeline-architecture event=unsupported detail=domain: {}", reason);
        return variants;
    }
    // M-D: the two knobs that change the machine. Only the pipelined schedule is
    // enumerated by default: it is the one shape the row families cannot produce
    // (period = slowest arm instead of the sum), while staggered/serial are
    // strictly dominated by the existing families and only spend the portfolio's
    // submission budget. `OPUSOPTIMA_PIPELINE_SCHEDULES` re-enables them for
    // experiments: e.g. `pipelined,staggered,serial`.
    std::vector<SchedulePolicy> policies{SchedulePolicy::Pipelined};
    if (const char* override = std::getenv("OPUSOPTIMA_PIPELINE_SCHEDULES")) {
        policies.clear();
        const std::string_view modes{override};
        for (const auto& mode : {SchedulePolicy::Pipelined, SchedulePolicy::Staggered,
                                 SchedulePolicy::Serial})
            if (modes.find(SchedulePolicyName(mode)) != std::string_view::npos)
                policies.push_back(mode);
        if (policies.empty()) policies.push_back(SchedulePolicy::Pipelined);
    }
    for (const auto policy : policies) {
        for (const auto preference : {PipelineVariant::ArmPreference::Compact,
                                      PipelineVariant::ArmPreference::Reach}) {
            try {
                PipelineVariant variant;
                variant.solution = CompilePipelineMachine(puzzle, policy, preference, postProcess,
                                                          &variant);
                variants.push_back(std::move(variant));
            } catch (const std::invalid_argument& error) {
                // Settings that cannot be placed are simply not offered, but the
                // reason is logged so a whole-family miss is diagnosable.
                GetLoggerOrFallback("solver")->info(
                    "Assembly branch=pipeline-architecture event=unsupported schedule={} arms={} "
                    "detail={}",
                    SchedulePolicyName(policy),
                    preference == PipelineVariant::ArmPreference::Compact ? "compact" : "reach",
                    error.what());
            }
        }
    }
    GetLoggerOrFallback("solver")->info(
        "Assembly branch=pipeline-architecture event=enumerated variants={} atoms={} depth={}",
        variants.size(), puzzle.outputs.front().atoms.size(),
        variants.empty() ? -1 : variants.front().depth);
    return variants;
}

} // namespace opusoptima::solvers::architecture
