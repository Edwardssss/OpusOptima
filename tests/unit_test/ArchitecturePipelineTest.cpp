#include "ElementPipeline.hpp"
#include "IO/BinaryWriter.hpp"
#include "IO/SolutionVerifier.hpp"
#include "ModuleLibrary.hpp"
#include "PipelineCompiler.hpp"
#include "RepoRoot.hpp"
#include "SolutionParameters.hpp"
#include "SolutionPostProcess.hpp"
#include "SwingKernel.hpp"
#include "TickScheduler.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

namespace opusoptima::solvers::architecture {
namespace {

// Mechanism bits (arm, bonder, rotate/drop, grab) plus the instruction bits the
// tapes use: repeat ('X', 24) and no-op ('O', 25). `decode.c` rejects a tape whose
// instruction bit is not present in the puzzle's permission mask.
constexpr std::uint64_t kArmBits = (1ULL << 0) | (1ULL << 8) | (1ULL << 22) | (1ULL << 23) |
                                   (1ULL << 24) | (1ULL << 25);

io::PuzzleData BasePuzzle(std::uint64_t extraBits = 0) {
    io::PuzzleData puzzle;
    puzzle.version = 3;
    puzzle.name = "generated-architecture";
    puzzle.permissionBits = kArmBits | extraBits;
    puzzle.outputMultiplier = 1;
    return puzzle;
}

/// Two bonded salt atoms, two single-atom salt ports: a feed-only machine.
io::PuzzleData SaltPairPuzzle(int multiplier) {
    auto puzzle = BasePuzzle();
    puzzle.outputMultiplier = multiplier;
    puzzle.inputs.push_back({{{core::AtomType::Salt, {0, 0}}}, {}});
    puzzle.inputs.push_back({{{core::AtomType::Salt, {0, 0}}}, {}});
    puzzle.outputs.emplace_back();
    puzzle.outputs.front().atoms = {{core::AtomType::Salt, {0, 0}},
                                    {core::AtomType::Salt, {1, 0}}};
    puzzle.outputs.front().bonds = {{1, {0, 0}, {1, 0}}};
    return puzzle;
}

void WritePuzzle(const std::filesystem::path& path, const io::PuzzleData& puzzle) {
    std::ofstream out(path, std::ios::binary);
    const auto word = [&](std::uint64_t value, int bytes) {
        for (int i = 0; i < bytes; ++i) out.put(static_cast<char>(value >> (8 * i)));
    };
    word(3, 4);
    word(puzzle.name.size(), 1);
    out.write(puzzle.name.data(), static_cast<std::streamsize>(puzzle.name.size()));
    word(0, 8);
    word(puzzle.permissionBits, 8);
    const auto list = [&](const std::vector<io::Molecule>& molecules) {
        word(molecules.size(), 4);
        for (const auto& molecule : molecules) {
            word(molecule.atoms.size(), 4);
            for (const auto& atom : molecule.atoms) {
                word(static_cast<std::uint8_t>(atom.type), 1);
                word(atom.position.q, 1);
                word(atom.position.r, 1);
            }
            word(molecule.bonds.size(), 4);
            for (const auto& bond : molecule.bonds) {
                word(bond.bits, 1);
                word(bond.from.q, 1);
                word(bond.from.r, 1);
                word(bond.to.q, 1);
                word(bond.to.r, 1);
            }
        }
    };
    list(puzzle.inputs);
    list(puzzle.outputs);
    word(puzzle.outputMultiplier, 4);
    word(0, 1);
}

std::size_t Count(const io::SolutionData& machine, const std::string& type) {
    std::size_t count = 0;
    for (const auto& object : machine.objects)
        if (object.typeName == type) ++count;
    return count;
}

} // namespace

// ── M-A ─────────────────────────────────────────────────────────────────────

TEST(ArchitecturePipelineTest, FeedOnlyPipelineConsumesEveryAtom) {
    const auto puzzle = SaltPairPuzzle(1);
    const auto pipeline = ElementPipeline::Build(puzzle);
    EXPECT_TRUE(pipeline.IsFeedOnly());
    EXPECT_EQ(pipeline.Depth(), 0);
    const auto run = pipeline.Run();
    EXPECT_TRUE(run.complete);
    EXPECT_EQ(run.consumes, 2U);
    EXPECT_EQ(run.generates, 0U);
    EXPECT_EQ(run.passThroughs, 0U);
    EXPECT_TRUE(run.waste.empty());
    for (const auto& command : run.commands) EXPECT_EQ(command.kind, CommandKind::Consume);
}

TEST(ArchitecturePipelineTest, CalcificationAddsOneGenerateStage) {
    auto puzzle = BasePuzzle(1ULL << 12);
    puzzle.inputs.push_back({{{core::AtomType::Air, {0, 0}}}, {}});
    puzzle.outputs.emplace_back();
    puzzle.outputs.front().atoms = {{core::AtomType::Salt, {0, 0}}};
    const auto pipeline = ElementPipeline::Build(puzzle);
    EXPECT_FALSE(pipeline.IsFeedOnly());
    const auto run = pipeline.Run();
    EXPECT_EQ(run.depth, 1);
    EXPECT_EQ(run.consumes, 1U);
    EXPECT_EQ(run.generates, 1U);
}

TEST(ArchitecturePipelineTest, DryRunEmitsOperandsBeforeTheStageThatFusesThem) {
    auto puzzle = BasePuzzle(1ULL << 12);
    puzzle.inputs.push_back({{{core::AtomType::Fire, {0, 0}}}, {}});
    puzzle.inputs.push_back({{{core::AtomType::Earth, {0, 0}}}, {}});
    puzzle.outputs.emplace_back();
    puzzle.outputs.front().atoms = {{core::AtomType::Salt, {0, 0}},
                                    {core::AtomType::Salt, {1, 0}}};
    puzzle.outputs.front().bonds = {{1, {0, 0}, {1, 0}}};
    const auto run = ElementPipeline::Build(puzzle).Run();
    std::size_t generates = 0;
    for (std::size_t index = 0; index < run.commands.size(); ++index) {
        if (run.commands[index].kind != CommandKind::Generate) continue;
        ++generates;
        bool operandSeen = false;
        for (std::size_t earlier = 0; earlier < index; ++earlier)
            if (run.commands[earlier].kind == CommandKind::Consume) operandSeen = true;
        EXPECT_TRUE(operandSeen) << "generate at " << index << " has no earlier operand";
    }
    EXPECT_EQ(generates, 2U);
}

TEST(ArchitecturePipelineTest, AnimismusReportsTheUndemandedCoproductAsWaste) {
    auto puzzle = BasePuzzle();
    puzzle.inputs.push_back({{{core::AtomType::Salt, {0, 0}}}, {}});
    puzzle.outputs.emplace_back();
    puzzle.outputs.front().atoms = {{core::AtomType::Vitae, {0, 0}}};
    const auto run = ElementPipeline::Build(puzzle).Run();
    EXPECT_EQ(run.generates, 1U);
    ASSERT_TRUE(run.waste.count(core::AtomType::Mors) != 0);
    EXPECT_EQ(run.waste.at(core::AtomType::Mors), 1U);
}

TEST(ArchitecturePipelineTest, UnreachableElementIsRejected) {
    auto puzzle = BasePuzzle();
    puzzle.inputs.push_back({{{core::AtomType::Salt, {0, 0}}}, {}});
    puzzle.outputs.emplace_back();
    puzzle.outputs.front().atoms = {{core::AtomType::Gold, {0, 0}}};
    EXPECT_THROW(ElementPipeline::Build(puzzle), std::invalid_argument);
}

// ── M-B ─────────────────────────────────────────────────────────────────────

TEST(ArchitecturePipelineTest, ConveyorAccessPointsSitOnTheSwingRing) {
    const auto module = BuildAtomConveyor(core::HexCoord{0, 0}, 1, 3, 2);
    ASSERT_EQ(module.inputs.size(), 1U);
    ASSERT_EQ(module.outputs.size(), 1U);
    EXPECT_EQ(module.inputs.front().cell, layout::SwingScale(layout::SwingStep(1), 2));
    EXPECT_EQ(module.outputs.front().cell, layout::SwingScale(layout::SwingStep(3), 2));
    EXPECT_EQ(module.inputs.front().tick, 0);
    EXPECT_EQ(module.outputs.front().tick, module.latency);
    EXPECT_EQ(module.period, 2 * 2 + 2);
}

TEST(ArchitecturePipelineTest, AssemblerPeriodIsTheSlowestArmNotTheSum) {
    UniversalAssemblerSpec spec;
    spec.target.atoms = {{core::AtomType::Salt, {0, 0}}, {core::AtomType::Salt, {1, 0}}};
    // Arm 0: grab 0, drop 1 (one turn). Arm 1: grab 0, drop 3 (three turns).
    spec.stations = {core::HexCoord{-1, 0}, core::HexCoord{0, 0}};
    spec.grabs = {0, 0};
    spec.pivots = {core::HexCoord{1, 0}, core::HexCoord{1, 0}};
    spec.lengths = {1, 1};
    spec.drops = {1, 3};
    const auto module = BuildUniversalAssembler(spec);
    EXPECT_EQ(module.period, 2 * 3 + 2);
    EXPECT_EQ(module.objects.size(), 3U); // two arms + out-std
}

// ── M-C ─────────────────────────────────────────────────────────────────────

TEST(ArchitecturePipelineTest, SerialSchedulePeriodIsTheSumOfModuleWork) {
    std::vector<ModuleShape> modules;
    modules.push_back(BuildAtomConveyor(core::HexCoord{0, 0}, 0, 1, 1));
    modules.push_back(BuildAtomConveyor(core::HexCoord{9, 0}, 0, 1, 1));
    modules.push_back(BuildAtomConveyor(core::HexCoord{18, 0}, 0, 1, 1));
    const auto serial = TickScheduler::Align(modules, SchedulePolicy::Serial);
    EXPECT_EQ(serial.offsets.front(), 0);
    EXPECT_EQ(serial.offsets[1], modules.front().period);
    EXPECT_EQ(serial.period, 3 * modules.front().period);
}

TEST(ArchitecturePipelineTest, PipelinedSchedulePeriodIsTheSlowestModule) {
    std::vector<ModuleShape> modules;
    modules.push_back(BuildAtomConveyor(core::HexCoord{0, 0}, 0, 1, 1));
    modules.push_back(BuildAtomConveyor(core::HexCoord{9, 0}, 2, 0, 1));
    modules.push_back(BuildAtomConveyor(core::HexCoord{18, 0}, 0, 2, 1));
    const auto pipelined = TickScheduler::Align(modules, SchedulePolicy::Pipelined);
    for (const int offset : pipelined.offsets) EXPECT_EQ(offset, 0);
    int slowest = 1;
    for (const auto& module : modules) slowest = std::max(slowest, module.period);
    EXPECT_EQ(pipelined.period, slowest);
    const auto serial = TickScheduler::Align(modules, SchedulePolicy::Serial);
    EXPECT_LT(pipelined.period, serial.period);
}

// ── M-D ─────────────────────────────────────────────────────────────────────

TEST(ArchitecturePipelineTest, RegistryEnumeratesInDeclarationOrder) {
    ParameterRegistry registry;
    registry.Define("batches", {"1", "2", "4"}, "1");
    registry.Define("schedule", {"pipelined", "staggered"}, "pipelined");
    const auto settings = registry.Enumerate();
    ASSERT_EQ(settings.size(), 6U);
    EXPECT_EQ(settings.front().at("batches"), "1");
    EXPECT_EQ(settings.front().at("schedule"), "pipelined");
    EXPECT_EQ(settings[1].at("schedule"), "staggered");
    EXPECT_EQ(settings[2].at("batches"), "2");
    EXPECT_EQ(registry.Defaults().at("batches"), "1");
}

TEST(ArchitecturePipelineTest, SelectorKeepsTheBestOfEachObjective) {
    io::SolutionData cheap;
    cheap.metrics = {100, 10, 50, 0};
    io::SolutionData fast;
    fast.metrics = {10, 100, 60, 0};
    io::SolutionData small;
    small.metrics = {80, 200, 5, 0};
    ObjectiveSelector selector;
    EXPECT_TRUE(selector.Submit(cheap));
    EXPECT_TRUE(selector.Submit(fast));
    EXPECT_TRUE(selector.Submit(small));
    ASSERT_TRUE(selector.BestCost());
    ASSERT_TRUE(selector.BestCycles());
    ASSERT_TRUE(selector.BestArea());
    EXPECT_EQ(selector.BestCost()->metrics.cost, 10);
    EXPECT_EQ(selector.BestCycles()->metrics.cycles, 10);
    EXPECT_EQ(selector.BestArea()->metrics.area, 5);
    // Unmeasured machines never win a slot.
    io::SolutionData unmeasured;
    EXPECT_FALSE(selector.Submit(unmeasured));
}

// ── M-E ─────────────────────────────────────────────────────────────────────

TEST(ArchitecturePipelineTest, IdleArmsAndDuplicateObjectsArePruned) {
    io::SolutionData machine;
    io::SolutionObject idle;
    idle.typeName = "arm1";
    idle.position = {0, 0};
    idle.bodyData = {1, 0, 0, 4, 0};
    idle.instructions = {{'O', 0}, {'O', 1}, {'O', 2}, {'X', 3}};
    machine.objects.push_back(idle);
    io::SolutionObject input;
    input.typeName = "input";
    input.position = {1, 0};
    input.bodyData = {1, 0, 0, 0, 0};
    machine.objects.push_back(input);
    machine.objects.push_back(input);
    PruneReport report;
    const auto pruned = SolutionPostProcess::PruneUnusedParts(machine, &report);
    EXPECT_EQ(pruned.objects.size(), 1U);
    EXPECT_EQ(report.Count(), 2U);
    EXPECT_EQ(Count(pruned, "input"), 1U);
}

// ── end to end ──────────────────────────────────────────────────────────────

TEST(ArchitecturePipelineTest, CompilerEmitsOneArmPerAtomAndOneOutputArea) {
    const auto puzzle = SaltPairPuzzle(4);
    ASSERT_TRUE(SupportsPipelineAssembly(puzzle));
    PipelineVariant detail;
    const auto machine = CompilePipelineMachine(
        puzzle, SchedulePolicy::Pipelined, PipelineVariant::ArmPreference::Compact, true, &detail);
    EXPECT_EQ(Count(machine, "arm1"), 2U);
    EXPECT_EQ(Count(machine, "out-std"), 1U);
    EXPECT_EQ(Count(machine, "input"), 2U);
    EXPECT_EQ(Count(machine, "bonder"), 1U);
    EXPECT_GE(detail.period, 3);

    const auto serial = CompilePipelineMachine(
        puzzle, SchedulePolicy::Serial, PipelineVariant::ArmPreference::Compact, true, &detail);
    EXPECT_EQ(Count(serial, "arm1"), 2U);
    EXPECT_GT(detail.period, 3);

    const auto variants = CompilePipelineVariants(puzzle, true);
    EXPECT_FALSE(variants.empty());
    EXPECT_EQ(variants.front().policy, SchedulePolicy::Pipelined);
}

#ifdef HAS_LIBVERIFY
// The whole chain, gated by the pinned native verifier: M-A decides the element
// sequence, M-B places one conveyor per atom, M-C aligns the arms into one period,
// M-E prunes nothing, and the machine must produce `multiplier` products.
TEST(ArchitecturePipelineTest, PipelinedMachineBeatsSerialNatively) {
    const auto root = opusoptima::test::RepoRoot(__FILE__);
    const auto puzzlePath = root / "build/architecture-pipelined.puzzle";
    const auto solutionPath = root / "build/architecture-pipelined.solution";
    struct Cleanup {
        std::filesystem::path puzzle, solution;
        ~Cleanup() {
            std::error_code ec;
            std::filesystem::remove(puzzle, ec);
            std::filesystem::remove(solution, ec);
        }
    } cleanup{puzzlePath, solutionPath};

    const auto puzzle = SaltPairPuzzle(4);
    WritePuzzle(puzzlePath, puzzle);

    const auto gate = [&](SchedulePolicy policy) {
        const auto machine = CompilePipelineMachine(
            puzzle, policy, PipelineVariant::ArmPreference::Compact, true);
        io::BinaryWriter::WriteSolutionFile(solutionPath, machine);
        return io::VerifySolutionFiles(puzzlePath, solutionPath, 400000, 20000);
    };
    const auto pipelinedReport = gate(SchedulePolicy::Pipelined);
    EXPECT_TRUE(pipelinedReport.Passed())
        << pipelinedReport.detail << " cycle=" << pipelinedReport.errorCycle << " at=("
        << pipelinedReport.errorU << "," << pipelinedReport.errorV << ")";
    const auto serialReport = gate(SchedulePolicy::Serial);
    EXPECT_TRUE(serialReport.Passed())
        << serialReport.detail << " cycle=" << serialReport.errorCycle << " at=("
        << serialReport.errorU << "," << serialReport.errorV << ")";
    if (pipelinedReport.Passed() && serialReport.Passed()) {
        EXPECT_LT(pipelinedReport.metrics.cycles, serialReport.metrics.cycles)
            << "pipelining did not shorten the period: " << pipelinedReport.metrics.cycles << " vs "
            << serialReport.metrics.cycles;
    }
}
#endif

} // namespace opusoptima::solvers::architecture
