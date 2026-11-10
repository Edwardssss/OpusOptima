// Contract tests for the schedule audit that wires the `ProgramBuilder` (C3)
// merge into the emitters. An emitted machine is only well formed when every
// arm's tape sits on distinct, increasing ticks, because the upstream merge may
// not displace an instruction.
#include <gtest/gtest.h>

#include <cstdint>
#include <string>

#include "AssemblySolver.hpp"
#include "BinaryParser.hpp"
#include "RowAssembly.hpp"

using namespace opusoptima;
using namespace opusoptima::solvers;

namespace {
io::SolutionData Machine(std::initializer_list<std::vector<std::pair<char, int>>> tapes) {
    io::SolutionData machine;
    int index = 0;
    for (const auto& tape : tapes) {
        io::SolutionObject object;
        object.typeName = "piston";
        object.position = {.q = index++, .r = 0};
        for (const auto& [action, tick] : tape)
            object.instructions.push_back({.action = action, .parameter = tick});
        machine.objects.push_back(std::move(object));
    }
    return machine;
}
}  // namespace

TEST(ScheduleAudit, CleanMachineIsReportedClean) {
    const auto machine = Machine({{{'E', 0}, {'G', 1}, {'e', 2}}, {{'E', 0}, {'G', 1}}});
    const auto audit = AuditMachineSchedule(machine);
    EXPECT_EQ(audit.arms, 2U);
    EXPECT_EQ(audit.instructions, 5U);
    EXPECT_EQ(audit.period, 3);
    EXPECT_TRUE(audit.monotone);
    EXPECT_TRUE(audit.uniqueTicks);
    EXPECT_FALSE(audit.displaced);
}

// Two arms may share a tick: only the same arm constrains the merge.
TEST(ScheduleAudit, IndependentArmsMayShareTicks) {
    const auto machine = Machine({{{'E', 5}}, {{'E', 5}}, {{'E', 5}}});
    const auto audit = AuditMachineSchedule(machine);
    EXPECT_TRUE(audit.uniqueTicks);
    EXPECT_FALSE(audit.displaced);
}

TEST(ScheduleAudit, SameArmDuplicateTickIsReported) {
    const auto machine = Machine({{{'E', 0}, {'E', 0}}});
    const auto audit = AuditMachineSchedule(machine);
    EXPECT_FALSE(audit.uniqueTicks) << "an arm may not emit twice on one tick";
    EXPECT_TRUE(audit.displaced) << "the merge had to move the duplicate";
}

TEST(ScheduleAudit, DecreasingTicksAreReported) {
    const auto machine = Machine({{{'E', 4}, {'E', 2}}});
    const auto audit = AuditMachineSchedule(machine);
    EXPECT_FALSE(audit.monotone);
}

TEST(ScheduleAudit, EmptyArmsAreIgnored) {
    const auto machine = Machine({{}, {{'E', 3}}});
    const auto audit = AuditMachineSchedule(machine);
    EXPECT_EQ(audit.arms, 1U);
    EXPECT_EQ(audit.instructions, 1U);
    EXPECT_EQ(audit.period, 4);
}

// The real emitter must satisfy the contract: this is the regression guard that
// keeps the builder wired into the production path.
TEST(ScheduleAudit, CompiledScheduledRowIsSchedulable) {
    const auto puzzle = io::BinaryParser::ParsePuzzleFile("asset/puzzle/easy/just-salt.puzzle");
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    const auto plan = AssemblyPlanner::Plan(puzzle, false, deadline);
    const auto machine = CompileScheduledRowAssembly(puzzle, plan, deadline);
    const auto audit = AuditMachineSchedule(machine);
    EXPECT_TRUE(audit.monotone);
    EXPECT_TRUE(audit.uniqueTicks);
    EXPECT_FALSE(audit.displaced);
    EXPECT_GT(audit.period, 0);
}
