// Structural tests for the upstream `LinearDisassembler` port used by the
// pipelined input stage. They pin the module inventory, the hand-off geometry
// and the per-atom extraction tape so later wiring into a composite machine can
// rely on a stable contract.
#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "AssemblySolver.hpp"
#include "BinaryParser.hpp"
#include "BinaryWriter.hpp"
#include "LinearInputArea.hpp"
#include "RowAssembly.hpp"

using namespace opusoptima;
using namespace opusoptima::solvers;

namespace {
io::Molecule LinearChain(int atoms) {
    io::Molecule molecule;
    for (int i = 0; i < atoms; ++i) {
        io::MoleculeAtom atom;
        atom.type = core::AtomType::Salt;
        atom.position = {i, 0};
        molecule.atoms.push_back(atom);
        if (i) {
            io::MoleculeBond bond;
            bond.bits = 1;
            bond.from = {i - 1, 0};
            bond.to = {i, 0};
            molecule.bonds.push_back(bond);
        }
    }
    return molecule;
}

const io::SolutionObject* Find(const LinearInputArea& area, const char* type) {
    for (const auto& object : area.machine.objects)
        if (object.typeName == type) return &object;
    return nullptr;
}

std::size_t Count(const LinearInputArea& area, const char* type) {
    std::size_t count = 0;
    for (const auto& object : area.machine.objects)
        if (object.typeName == type) ++count;
    return count;
}

void ExpectMonotoneTicks(const LinearInputArea& area) {
    for (const auto& object : area.machine.objects) {
        int previous = -1;
        for (const auto& instruction : object.instructions) {
            EXPECT_GE(instruction.parameter, previous) << object.typeName;
            previous = instruction.parameter;
        }
    }
}
} // namespace

// Every linear source gets one reagent port, one static unbonder, one grab
// piston on a short rail and one output arm that parks atoms on the hand-off
// cell. The rail covers exactly one cell per source atom.
TEST(LinearInputArea, ModuleInventoryMatchesUpstreamGeometry) {
    for (int atoms = 1; atoms <= 4; ++atoms) {
        const core::HexCoord origin{10, -3};
        const auto area = BuildLinearInputArea(LinearChain(atoms), origin, 2);
        EXPECT_EQ(area.width, atoms);
        EXPECT_EQ(area.input, 2);
        EXPECT_EQ(Count(area, "input"), 1U);
        EXPECT_EQ(Count(area, "unbonder"), 1U);
        EXPECT_EQ(Count(area, "piston"), 1U);
        EXPECT_EQ(Count(area, "track"), 1U);
        EXPECT_EQ(Count(area, "arm1"), 1U);

        const auto* input = Find(area, "input");
        ASSERT_NE(input, nullptr);
        EXPECT_EQ(input->position.q, origin.q - atoms - 2);
        EXPECT_EQ(input->position.r, origin.r + 1);
        EXPECT_EQ(input->bodyData.at(2), 2);

        const auto* unbonder = Find(area, "unbonder");
        ASSERT_NE(unbonder, nullptr);
        EXPECT_EQ(unbonder->position.q, origin.q - 4);
        EXPECT_EQ(unbonder->position.r, origin.r);

        const auto* grab = Find(area, "piston");
        ASSERT_NE(grab, nullptr);
        EXPECT_EQ(grab->position.q, origin.q - atoms - 2);
        EXPECT_EQ(grab->position.r, origin.r + 2);
        EXPECT_EQ(grab->bodyData.at(1), 4);

        const auto* rail = Find(area, "track");
        ASSERT_NE(rail, nullptr);
        EXPECT_EQ(rail->position.q, grab->position.q);
        EXPECT_EQ(rail->position.r, grab->position.r);
        // bodyData is {1, rotation, index, 0, cellCount, (dq,dr)*cellCount, tail}.
        EXPECT_EQ(rail->bodyData.at(4), atoms);

        const auto* output = Find(area, "arm1");
        ASSERT_NE(output, nullptr);
        EXPECT_EQ(output->position.q, origin.q - 3);
        EXPECT_EQ(output->position.r, origin.r + 3);
        EXPECT_EQ(output->bodyData.at(1), 4);
        EXPECT_EQ(area.outputPosition.q, origin.q - 3);
        EXPECT_EQ(area.outputPosition.r, origin.r);
    }
}

// The tape is produced in supply order: the first atom is grabbed at the
// reagent and extended, later atoms advance the rail, and every atom is handed
// over by a grab/rotate/reset on the output arm. Ticks never go backwards.
TEST(LinearInputArea, ExtractionTapeFollowsSupplyOrder) {
    for (int atoms = 1; atoms <= 4; ++atoms) {
        auto area = BuildLinearInputArea(LinearChain(atoms), {0, 0}, 0);
        for (int atom = 0; atom < atoms; ++atom) EmitLinearInputAtom(area, atom);
        ExpectMonotoneTicks(area);

        const auto* grab = Find(area, "piston");
        const auto* output = Find(area, "arm1");
        ASSERT_NE(grab, nullptr);
        ASSERT_NE(output, nullptr);

        std::string grabTape, outputTape;
        for (const auto& instruction : grab->instructions) grabTape.push_back(instruction.action);
        for (const auto& instruction : output->instructions)
            outputTape.push_back(instruction.action);

        std::string expectedGrab = "GE";
        for (int atom = 1; atom < atoms; ++atom) expectedGrab.push_back('A');
        if (atoms > 1) expectedGrab.push_back('X');
        EXPECT_EQ(grabTape, expectedGrab);

        std::string expectedOutput;
        for (int atom = 0; atom < atoms; ++atom) expectedOutput += "GRX";
        EXPECT_EQ(outputTape, expectedOutput);

        // The reset overlaps the next tick, so a width-w source finishes after
        // 4 + 3 * (w - 1) shared ticks.
        EXPECT_EQ(area.time, 4 + 3 * (atoms - 1));
    }
}

// Only height-1 chains have a single-rail walk; bent, empty and out-of-range
// requests fail closed so the caller can fall back to the shared feeder.
TEST(LinearInputArea, RejectsUnsupportedSources) {
    io::Molecule bent = LinearChain(2);
    bent.atoms[1].position = {1, -1};
    auto area = BuildLinearInputArea(LinearChain(2), {0, 0}, 0);
    EXPECT_THROW(EmitLinearInputAtom(area, 2), std::invalid_argument);
    EXPECT_THROW(EmitLinearInputAtom(area, -1), std::invalid_argument);
    EXPECT_THROW(BuildLinearInputArea(io::Molecule{}, {0, 0}, 0), std::invalid_argument);
    EXPECT_THROW(BuildLinearInputArea(bent, {0, 0}, 0), std::invalid_argument);
}

// Two modules are independent: each keeps its own clock and tape length, so a
// composite stage can interleave them without a shared cursor.
TEST(LinearInputArea, ModulesKeepIndependentClocks) {
    auto first = BuildLinearInputArea(LinearChain(3), {0, 0}, 0);
    auto second = BuildLinearInputArea(LinearChain(2), {20, 0}, 1);
    for (int atom = 0; atom < 3; ++atom) EmitLinearInputAtom(first, atom);
    for (int atom = 0; atom < 2; ++atom) EmitLinearInputAtom(second, atom);
    EXPECT_EQ(first.time, 10);
    EXPECT_EQ(second.time, 7);
    EXPECT_EQ(first.machine.objects.size(), second.machine.objects.size());
    ExpectMonotoneTicks(first);
    ExpectMonotoneTicks(second);
}

// The scheduled row emitter uses the local intake by default and keeps the
// shared-serpentine machine behind an explicit `off`. Every other value must be
// deterministic so the portfolio and the measurement probes agree.
TEST(LinearInputArea, DirectIntakeIsDefaultAndOffRestoresFeeder) {
    const auto puzzle =
        io::BinaryParser::ParsePuzzleFile("asset/puzzle/easy/just-salt.puzzle");
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    const auto plan = AssemblyPlanner::Plan(puzzle, false, deadline);
    const char* flag = "OPUSOPTIMA_DIRECT_INPUT";
    struct Guard {
        const char* name;
        std::optional<std::string> saved;
        ~Guard() {
            if (saved) setenv(name, saved->c_str(), 1);
            else unsetenv(name);
        }
    } guard{flag, std::getenv(flag) ? std::optional<std::string>(std::getenv(flag)) : std::nullopt};

    unsetenv(flag);
    const auto intake = io::BinaryWriter::WriteSolution(
        CompileScheduledRowAssembly(puzzle, plan, deadline));
    setenv(flag, "off", 1);
    const auto feeder = io::BinaryWriter::WriteSolution(
        CompileScheduledRowAssembly(puzzle, plan, deadline));
    EXPECT_NE(intake, feeder);
    // Unknown values keep the default machine, and every run is deterministic.
    for (const char* value : {"on", "1", "", "enabled"}) {
        setenv(flag, value, 1);
        EXPECT_EQ(io::BinaryWriter::WriteSolution(
                      CompileScheduledRowAssembly(puzzle, plan, deadline)),
                  intake)
            << "value=" << value;
    }
    // The explicit feeder switch is stable as well.
    setenv(flag, "off", 1);
    EXPECT_EQ(io::BinaryWriter::WriteSolution(
                  CompileScheduledRowAssembly(puzzle, plan, deadline)),
              feeder);
}
