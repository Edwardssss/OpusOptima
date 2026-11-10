#include "TestOutputDirectory.hpp"
#include <gtest/gtest.h>

#include "AssemblySolver.hpp"
#include "AutomaticSupply.hpp"
#include "BinaryWriter.hpp"
#include "CandidateArchive.hpp"
#include "Disassembly.hpp"
#include "FeedLayout.hpp"
#include "FixedArmInventory.hpp"
#include "FixedArmJoin.hpp"
#include "HeldArmMotion.hpp"
#include "InventoryIR.hpp"
#include "MaterialPlanning.hpp"
#include "PhysicalInventory.hpp"
#include "SerialArmExecution.hpp"
#include "SolutionVerifier.hpp"
#include "SupplyExecutionIR.hpp"
#include <fstream>
#include "RepoRoot.hpp"

namespace {
using namespace opusoptima;
io::PuzzleData WideFeed() {
    io::PuzzleData p;
    p.version = 3;
    p.name = "wide-intact";
    p.outputMultiplier = 1;
    p.permissionBits = (1ULL << 0) | (1ULL << 3) | (1ULL << 22) | (1ULL << 23);
    io::Molecule m;
    for (int q = -2; q <= 2; ++q)
        for (int r = -2; r <= 2; ++r) {
            if (core::Distance({q, r}, {}) > 2) continue;
            m.atoms.push_back({core::AtomType::Salt, {q, r}});
            for (const auto& a : m.atoms)
                if (core::Distance(a.position, {q, r}) == 1)
                    m.bonds.push_back({1, a.position, {q, r}});
        }
    p.inputs.push_back(m);
    p.outputs.push_back(m);
    return p;
}
#ifdef HAS_LIBVERIFY
void WritePuzzle(const std::filesystem::path& path, const io::PuzzleData& p) {
    std::ofstream out(path, std::ios::binary);
    auto word = [&](std::uint64_t v, int n) {
        for (int i = 0; i < n; ++i)
            out.put(static_cast<char>(v >> (8 * i)));
    };
    word(3, 4);
    word(p.name.size(), 1);
    out.write(p.name.data(), p.name.size());
    word(0, 8);
    word(p.permissionBits, 8);
    for (const auto* list : {&p.inputs, &p.outputs}) {
        word(list->size(), 4);
        for (const auto& m : *list) {
            word(m.atoms.size(), 4);
            for (const auto& a : m.atoms) {
                word(static_cast<std::uint8_t>(a.type), 1);
                word(a.position.q, 1);
                word(a.position.r, 1);
            }
            word(m.bonds.size(), 4);
            for (const auto& b : m.bonds) {
                word(b.bits, 1);
                word(b.from.q, 1);
                word(b.from.r, 1);
                word(b.to.q, 1);
                word(b.to.r, 1);
            }
        }
    }
    word(p.outputMultiplier, 4);
    word(0, 1);
}
class DisassemblyVerifierTest : public testing::Test {
    protected:
    std::filesystem::path puzzle, solution;
    void SetUp() override {
        const auto root = std::filesystem::current_path();
        const auto name = std::string("disassembly-") +
                          testing::UnitTest::GetInstance()->current_test_info()->name();
        puzzle = root / (name + ".puzzle");
        solution = root / (name + ".solution");
    }
    void TearDown() override {
        std::error_code ec;
        std::filesystem::remove(puzzle, ec);
        std::filesystem::remove(solution, ec);
    }
    void Verify(io::PuzzleData p) {
        WritePuzzle(puzzle, p);
        auto s = solvers::AssemblySolver{}.Solve(p);
        EXPECT_EQ(s.solutionName, "Intact wide-feed transport");
        io::BinaryWriter::WriteSolutionFile(solution, s);
        auto report = io::VerifySolutionFiles(puzzle, solution, 5000, 2000);
        ASSERT_TRUE(report.Passed()) << report.detail << " cycle=" << report.errorCycle
                                     << " at=" << report.errorU << ',' << report.errorV;
        s.metrics = report.metrics;
        io::BinaryWriter::WriteSolutionFile(solution, s);
        EXPECT_TRUE(io::VerifySolutionFiles(puzzle, solution, 5000, 2000).Passed());
    }
};
TEST_F(DisassemblyVerifierTest, SerialAnimismusBalancedSaltSixProducts) {
    using T = core::AtomType;
    io::PuzzleData p;
    p.version = 3;
    p.name = "serial-animismus-balanced-salt";
    p.outputMultiplier = 1;
    p.permissionBits = ~0ULL;
    p.inputs = {{{{T::Salt, {0, 0}}}, {}}};
    // Deliberately retain the original, reversed outlet indices.
    p.outputs = {{{{T::Mors, {0, 0}}}, {}}, {{{T::Vitae, {0, 0}}}, {}}};
    const auto material = solvers::SearchMaterialExecution(p);
    ASSERT_TRUE(material.plan) << material.detail;
    const test::ScopedTestDirectory scratch;
    const auto& root = scratch.Path();
    const auto input = root / "fixture.puzzle";
    WritePuzzle(input, p);
    struct Environment {
        std::string old;
        bool present = std::getenv("OPUSOPTIMA_SERIAL_ANIMISMUS") != nullptr;
        Environment() {
            if (present) old = std::getenv("OPUSOPTIMA_SERIAL_ANIMISMUS");
            setenv("OPUSOPTIMA_SERIAL_ANIMISMUS", "1", 1);
        }
        ~Environment() {
            if (present) setenv("OPUSOPTIMA_SERIAL_ANIMISMUS", old.c_str(), 1);
            else unsetenv("OPUSOPTIMA_SERIAL_ANIMISMUS");
        }
    } environment;
    int candidates = 0;
    std::ofstream log(root / "native-candidates.log");
    auto machine = solvers::CompileAcceptedSerialArmExecution(p, *material.plan,
        [&](const auto& candidate) {
            const auto output = root / ("candidate-" + std::to_string(candidates++) + ".solution");
            io::BinaryWriter::WriteSolutionFile(output, candidate);
            const auto report = io::VerifySolutionFiles(input, output, 150000, 10000);
            log << output.filename() << " passed=" << report.Passed()
                << " authority=" << int(report.authority) << " cycle=" << report.errorCycle
                << " coordinate=" << report.errorU << ',' << report.errorV
                << " detail=" << report.detail << std::endl;
            return report.Passed();
        }, [&](const auto& detail) { log << detail << std::endl; });
    EXPECT_TRUE(machine) << "See native-candidates.log; candidates=" << candidates;
}
TEST_F(DisassemblyVerifierTest, SerialCalcifyAnimismusBalancedCardinalSixProducts) {
    using T = core::AtomType;
    io::PuzzleData p;
    p.version = 3;
    p.name = "serial-calcify-animismus-balanced-cardinal";
    p.outputMultiplier = 1;
    p.permissionBits = ~0ULL;
    p.inputs = {{{{T::Earth, {0, 0}}}, {}}};
    // Deliberately retain the original, reversed outlet indices.
    p.outputs = {{{{T::Mors, {0, 0}}}, {}}, {{{T::Vitae, {0, 0}}}, {}}};
    struct Environment {
        std::string old, precursor;
        bool hadPrecursor = std::getenv("OPUSOPTIMA_CALCIFY_ANIMISMUS") != nullptr;
        bool present = std::getenv("OPUSOPTIMA_SERIAL_ANIMISMUS") != nullptr;
        Environment() {
            if (hadPrecursor) precursor = std::getenv("OPUSOPTIMA_CALCIFY_ANIMISMUS");
            setenv("OPUSOPTIMA_CALCIFY_ANIMISMUS", "1", 1);
            if (present) old = std::getenv("OPUSOPTIMA_SERIAL_ANIMISMUS");
            setenv("OPUSOPTIMA_SERIAL_ANIMISMUS", "1", 1);
        }
        ~Environment() {
            if (hadPrecursor) setenv("OPUSOPTIMA_CALCIFY_ANIMISMUS", precursor.c_str(), 1);
            else unsetenv("OPUSOPTIMA_CALCIFY_ANIMISMUS");
            if (present) setenv("OPUSOPTIMA_SERIAL_ANIMISMUS", old.c_str(), 1);
            else unsetenv("OPUSOPTIMA_SERIAL_ANIMISMUS");
        }
    } environment;
    const auto material = solvers::SearchMaterialExecution(p);
    ASSERT_TRUE(material.plan) << material.detail;
    const test::ScopedTestDirectory scratch;
    const auto& root = scratch.Path();
    const auto input = root / "fixture.puzzle";
    WritePuzzle(input, p);
    int candidates = 0;
    std::ofstream log(root / "native-candidates.log");
    auto machine = solvers::CompileAcceptedSerialArmExecution(p, *material.plan,
        [&](const auto& candidate) {
            const auto output = root / ("candidate-" + std::to_string(candidates++) + ".solution");
            io::BinaryWriter::WriteSolutionFile(output, candidate);
            const auto report = io::VerifySolutionFiles(input, output, 150000, 10000);
            log << output.filename() << " passed=" << report.Passed()
                << " authority=" << int(report.authority) << " cycle=" << report.errorCycle
                << " coordinate=" << report.errorU << ',' << report.errorV
                << " detail=" << report.detail << std::endl;
            return report.Passed();
        }, [&](const auto& detail) { log << detail << std::endl; });
    EXPECT_TRUE(machine) << "See native-candidates.log; candidates=" << candidates;
}
TEST_F(DisassemblyVerifierTest, AutomaticInventoryPreservesColoredFragmentWithoutBonder) {
    io::PuzzleData p;
    p.version = 3;
    p.name = "inventory-preserved-colored-fragment";
    p.outputMultiplier = 1;
    p.permissionBits = (1ULL << 0) | (1ULL << 2) | (1ULL << 3) | (1ULL << 9) | (1ULL << 17) |
                       (1ULL << 22) | (1ULL << 23);
    p.inputs = {{{{core::AtomType::Air, {0, 0}},
                  {core::AtomType::Salt, {1, 0}},
                  {core::AtomType::Fire, {2, 0}}},
                 {{14, {0, 0}, {1, 0}}, {1, {1, 0}, {2, 0}}}}};
    p.outputs = {
        {{{core::AtomType::Air, {0, 0}}, {core::AtomType::Salt, {1, 0}}}, {{14, {0, 0}, {1, 0}}}}};
    WritePuzzle(puzzle, p);
    std::string failure;
    const auto candidate = solvers::CompileAcceptedSupplyInventory(
        p,
        [&](const auto& s) {
            io::BinaryWriter::WriteSolutionFile(solution, s);
            const auto report = io::VerifySolutionFiles(puzzle, solution, 5000, 2000);
            failure = report.detail;
            return report.Passed();
        },
        [&](const solvers::SupplyFailure& feedback) { failure = feedback.detail; });
    ASSERT_TRUE(candidate) << failure;
    const auto report = io::VerifySolutionFiles(puzzle, solution, 5000, 2000);
    ASSERT_TRUE(report.Passed()) << report.detail;
    auto measured = *candidate;
    measured.metrics = report.metrics;
    io::BinaryWriter::WriteSolutionFile(solution, measured);
    EXPECT_TRUE(io::VerifySolutionFiles(puzzle, solution, 5000, 2000).Passed());
}
TEST_F(DisassemblyVerifierTest, AutomaticInventoryPreservesIntactMasksWithoutBondGlyphs) {
    for (const auto mask : {1, 2, 6, 8, 14}) {
        SCOPED_TRACE(mask);
        io::PuzzleData p;
        p.version = 3;
        p.name = "inventory-intact-mask";
        p.outputMultiplier = 1;
        p.permissionBits = (1ULL << 0) | (1ULL << 2) | (1ULL << 3) | (1ULL << 22) | (1ULL << 23);
        p.inputs = {{{{core::AtomType::Air, {0, 0}}, {core::AtomType::Salt, {1, 0}}},
                     {{static_cast<std::uint8_t>(mask), {0, 0}, {1, 0}}}}};
        p.outputs = p.inputs;
        WritePuzzle(puzzle, p);
        std::string failure;
        const auto candidate = solvers::CompileAcceptedSupplyInventory(
            p,
            [&](const auto& s) {
                io::BinaryWriter::WriteSolutionFile(solution, s);
                const auto report = io::VerifySolutionFiles(puzzle, solution, 5000, 2000);
                failure = report.detail;
                return report.Passed();
            },
            [&](const solvers::SupplyFailure& feedback) { failure = feedback.detail; });
        ASSERT_TRUE(candidate) << failure;
        for (const auto& object : candidate->objects)
            EXPECT_TRUE(object.typeName != "unbonder" && object.typeName != "bonder" &&
                        object.typeName != "bonder-prisma");
        const auto report = io::VerifySolutionFiles(puzzle, solution, 5000, 2000);
        ASSERT_TRUE(report.Passed()) << report.detail;
        auto measured = *candidate;
        measured.metrics = report.metrics;
        io::BinaryWriter::WriteSolutionFile(solution, measured);
        EXPECT_TRUE(io::VerifySolutionFiles(puzzle, solution, 5000, 2000).Passed());
    }
}
TEST_F(DisassemblyVerifierTest, AutomaticInventoryPreservesBranchedFragmentAcrossOutputPorts) {
    io::PuzzleData p;
    p.version = 3;
    p.name = "inventory-branched-multiport";
    p.outputMultiplier = 1;
    p.permissionBits =
        (1ULL << 0) | (1ULL << 2) | (1ULL << 3) | (1ULL << 9) | (1ULL << 22) | (1ULL << 23);
    p.inputs = {{{{core::AtomType::Salt, {0, 0}},
                  {core::AtomType::Air, {1, 0}},
                  {core::AtomType::Fire, {0, 1}},
                  {core::AtomType::Water, {-1, 1}}},
                 {{1, {0, 0}, {1, 0}}, {1, {0, 0}, {0, 1}}, {1, {0, 0}, {-1, 1}}}}};
    p.outputs = {{{{core::AtomType::Salt, {0, 0}},
                   {core::AtomType::Air, {1, 0}},
                   {core::AtomType::Fire, {0, 1}}},
                  {{1, {0, 0}, {1, 0}}, {1, {0, 0}, {0, 1}}}},
                 {{{core::AtomType::Water, {0, 0}}}, {}}};
    WritePuzzle(puzzle, p);
    std::string failure;
    const auto candidate = solvers::CompileAcceptedSupplyInventory(
        p,
        [&](const auto& s) {
            io::BinaryWriter::WriteSolutionFile(solution, s);
            const auto report = io::VerifySolutionFiles(puzzle, solution, 5000, 2000);
            failure = report.detail;
            return report.Passed();
        },
        [&](const solvers::SupplyFailure& feedback) { failure = feedback.detail; });
    ASSERT_TRUE(candidate) << failure;
    const auto report = io::VerifySolutionFiles(puzzle, solution, 5000, 2000);
    ASSERT_TRUE(report.Passed()) << report.detail;
    auto measured = *candidate;
    measured.metrics = report.metrics;
    io::BinaryWriter::WriteSolutionFile(solution, measured);
    EXPECT_TRUE(io::VerifySolutionFiles(puzzle, solution, 5000, 2000).Passed());
}
TEST_F(DisassemblyVerifierTest, AutomaticInventoryReassemblesSelectedColorMasks) {
    for (const auto sourceMask : {1, 2, 4, 6, 8, 10, 12, 14})
        for (const auto mask : {1, 2, 4, 6, 8, 10, 12, 14}) {
            SCOPED_TRACE(sourceMask);
            SCOPED_TRACE(mask);
            io::PuzzleData p;
            p.version = 3;
            p.name = "inventory-selective-color-reassembly";
            p.outputMultiplier = 1;
            p.permissionBits = (1ULL << 0) | (1ULL << 2) | (1ULL << 3) | (1ULL << 8) | (1ULL << 9) |
                               (1ULL << 11) | (1ULL << 22) | (1ULL << 23);
            p.inputs = {{{{core::AtomType::Fire, {0, 0}}, {core::AtomType::Fire, {1, 0}}},
                         {{static_cast<std::uint8_t>(sourceMask), {0, 0}, {1, 0}}}}};
            p.outputs = {{{{core::AtomType::Fire, {0, 0}}, {core::AtomType::Fire, {1, 0}}},
                          {{static_cast<std::uint8_t>(mask), {0, 0}, {1, 0}}}}};
            WritePuzzle(puzzle, p);
            std::string failure;
            const auto candidate = solvers::CompileAcceptedSupplyInventory(
                p,
                [&](const auto& s) {
                    io::BinaryWriter::WriteSolutionFile(solution, s);
                    const auto report = io::VerifySolutionFiles(puzzle, solution, 5000, 2000);
                    failure = report.detail;
                    return report.Passed();
                },
                [&](const solvers::SupplyFailure& feedback) { failure = feedback.detail; });
            ASSERT_TRUE(candidate) << failure;
            const auto report = io::VerifySolutionFiles(puzzle, solution, 5000, 2000);
            ASSERT_TRUE(report.Passed()) << report.detail;
            auto measured = *candidate;
            measured.metrics = report.metrics;
            io::BinaryWriter::WriteSolutionFile(solution, measured);
            EXPECT_TRUE(io::VerifySolutionFiles(puzzle, solution, 5000, 2000).Passed());
        }
}
TEST_F(DisassemblyVerifierTest, GeneratedAnimismusSurplusIsPhysicallyDisposed) {
    io::PuzzleData p;
    p.version = 3;
    p.name = "animismus-disposal";
    p.outputMultiplier = 1;
    p.permissionBits = (1ULL << 0) | (1ULL << 2) | (1ULL << 3) | (1ULL << 16) | (1ULL << 17) |
                       (1ULL << 22) | (1ULL << 23);
    p.inputs = {{{{core::AtomType::Salt, {0, 0}}}, {}}};
    p.outputs = {{{{core::AtomType::Vitae, {0, 0}}}, {}}};
    WritePuzzle(puzzle, p);
    const auto candidate = solvers::CompileAcceptedSupplyInventory(p, [&](const auto& s) {
        io::BinaryWriter::WriteSolutionFile(solution, s);
        return io::VerifySolutionFiles(puzzle, solution, 5000, 2000).Passed();
    });
    ASSERT_TRUE(candidate);
    const auto report = io::VerifySolutionFiles(puzzle, solution, 5000, 2000);
    ASSERT_TRUE(report.Passed()) << report.detail;
    auto measured = *candidate;
    measured.metrics = report.metrics;
    io::BinaryWriter::WriteSolutionFile(solution, measured);
    EXPECT_TRUE(io::VerifySolutionFiles(puzzle, solution, 5000, 2000).Passed());
}
TEST_F(DisassemblyVerifierTest, GeneratedDispersionCoproductsFeedCalcification) {
    io::PuzzleData p;
    p.version = 3;
    p.name = "dispersion-calcification";
    p.outputMultiplier = 1;
    p.permissionBits = (1ULL << 0) | (1ULL << 2) | (1ULL << 3) | (1ULL << 12) | (1ULL << 18) |
                       (1ULL << 22) | (1ULL << 23);
    p.inputs = {{{{core::AtomType::Quintessence, {0, 0}}}, {}}};
    p.outputs = {{{{core::AtomType::Air, {0, 0}}}, {}},
                 {{{core::AtomType::Salt, {0, 0}}}, {}},
                 {{{core::AtomType::Salt, {0, 0}}}, {}},
                 {{{core::AtomType::Salt, {0, 0}}}, {}}};
    WritePuzzle(puzzle, p);
    const auto candidate = solvers::CompileAcceptedSupplyInventory(p, [&](const auto& s) {
        io::BinaryWriter::WriteSolutionFile(solution, s);
        return io::VerifySolutionFiles(puzzle, solution, 5000, 2000).Passed();
    });
    ASSERT_TRUE(candidate);
    const auto report = io::VerifySolutionFiles(puzzle, solution, 5000, 2000);
    ASSERT_TRUE(report.Passed()) << report.detail;
    auto measured = *candidate;
    measured.metrics = report.metrics;
    io::BinaryWriter::WriteSolutionFile(solution, measured);
    EXPECT_TRUE(io::VerifySolutionFiles(puzzle, solution, 5000, 2000).Passed());
}
TEST_F(DisassemblyVerifierTest, PhysicalInventoryTapePassesIndependentVerifierTwice) {
    io::PuzzleData p;
    p.version = 3;
    p.name = "inventory-transport";
    p.outputMultiplier = 1;
    p.permissionBits = (1ULL << 0) | (1ULL << 3) | (1ULL << 22) | (1ULL << 23);
    io::Molecule m;
    m.atoms.push_back({core::AtomType::Salt, {0, 0}});
    p.inputs = {m};
    p.outputs = {m};
    solvers::ExecutionIR::State stock;
    stock.atoms.emplace(1, solvers::ExecutionIR::Atom{core::AtomType::Salt, {0, 0}});
    std::vector<core::HexCoord> track;
    for (int q = 0; q <= 8; ++q)
        track.push_back({q, 1});
    solvers::PhysicalInventory inventory(
        stock, track, {0, 1, 4, 0},
        [](const auto&, const auto& from, const auto& to, char op, bool) {
            return (op == 'A' || op == 'a' || op == 'G' || op == 'g') && from.direction == 4 &&
                   to.direction == 4 && from.length == 1 && to.length == 1;
        });
    inventory.Grab(1);
    ASSERT_EQ(inventory.MoveHeld({{4, 0}, 0}), solvers::MotionStatus::Found);
    inventory.Park(0);
    ASSERT_EQ(inventory.MoveEmpty({5, 1, 4, 0}), solvers::MotionStatus::Found);
    ASSERT_EQ(inventory.MoveEmpty({4, 1, 4, 0}), solvers::MotionStatus::Found);
    inventory.Retrieve(0, 1);
    ASSERT_EQ(inventory.MoveHeld({{8, 0}, 0}), solvers::MotionStatus::Found);
    io::SolutionData s;
    s.solutionName = "physical inventory";
    io::SolutionObject arm;
    arm.typeName = "arm1";
    arm.position = {0, 1};
    arm.bodyData = {1, 4, 0, 0, 0};
    int time = 0;
    for (char op : inventory.Tape())
        arm.instructions.push_back({op, time++});
    arm.instructions.push_back({'g', time++});
    for (int q = 0; q < 8; ++q)
        arm.instructions.push_back({'a', time++});
    arm.bodyData[3] = time;
    s.objects.push_back(arm);
    io::SolutionObject rail;
    rail.typeName = "track";
    rail.position = {0, 1};
    rail.bodyData = {1, 0, 0, 0, 9};
    for (int q = 0; q <= 8; ++q) {
        rail.bodyData.push_back(q);
        rail.bodyData.push_back(0);
    }
    rail.bodyData.push_back(0);
    s.objects.push_back(rail);
    s.objects.push_back({"input", {}, {}, {1, 0, 0, 0, 0}});
    s.objects.push_back({"out-std", {8, 0}, {}, {1, 0, 0, 0, 0}});
    WritePuzzle(puzzle, p);
    for (int pass = 0; pass < 2; ++pass) {
        io::BinaryWriter::WriteSolutionFile(solution, s);
        auto report = io::VerifySolutionFiles(puzzle, solution, 5000, 2000);
        ASSERT_TRUE(report.Passed()) << report.detail;
        s.metrics = report.metrics;
    }
}
TEST_F(DisassemblyVerifierTest, ReleasedPurificationTapePassesIndependentVerifierTwice) {
    using IR = solvers::ExecutionIR;
    io::PuzzleData p;
    p.version = 3;
    p.name = "released-purification";
    p.outputMultiplier = 1;
    p.permissionBits = (1ULL << 0) | (1ULL << 3) | (1ULL << 15) | (1ULL << 22) | (1ULL << 23);
    io::Molecule lead, tin;
    lead.atoms.push_back({core::AtomType::Lead, {}});
    tin.atoms.push_back({core::AtomType::Tin, {}});
    p.inputs = {lead, lead};
    p.outputs = {tin};
    IR::State stock = IR::Initial(p);
    stock.nextId = 3;
    stock.atoms = {{1, {core::AtomType::Lead, {0, 0}}}, {2, {core::AtomType::Lead, {1, 0}}}};
    std::vector<core::HexCoord> track;
    for (int q = 0; q <= 4; ++q)
        track.push_back({q, 2});
    solvers::PhysicalInventory inventory(
        stock, track, {0, 1, 4, 0},
        [](const auto&, const auto& from, const auto& to, char op, bool) {
            return (op == 'A' || op == 'a' || op == 'G' || op == 'g') && from.direction == 4 &&
                   to.direction == 4 && from.length == 1 && to.length == 1;
        });
    inventory.ReserveLoose(1, 0);
    inventory.ReserveLoose(2, 1);
    inventory.ReactReleased(
        p, {IR::Kind::Purify, 0, {1, 2}}, {{0, 1}}, [](const auto& step, const auto&) {
            return step.preconditions.atoms.at(1).position == core::HexCoord{0, 0} &&
                   step.preconditions.atoms.at(2).position == core::HexCoord{1, 0} &&
                   step.effects.atoms.at(3).position == core::HexCoord{0, 1};
        });
    inventory.ReserveLoose(3, 2);
    inventory.Retrieve(2, 3);
    ASSERT_EQ(inventory.MoveHeld({{4, 1}, 0}), solvers::MotionStatus::Found);
    inventory.Output(p, 0, [](const auto&, const auto&) { return true; });
    ASSERT_EQ(inventory.MoveEmpty({0, 1, 4, 0}), solvers::MotionStatus::Found);
    IR::RequireComplete(inventory.Material(), {1});
    io::SolutionData s;
    s.solutionName = "released purification inventory";
    io::SolutionObject arm;
    arm.typeName = "arm1";
    arm.position = track.front();
    int time = 0;
    for (char op : inventory.Tape())
        arm.instructions.push_back({op, time++});
    arm.bodyData = {1, 4, 0, time, 0};
    s.objects.push_back(arm);
    io::SolutionObject rail;
    rail.typeName = "track";
    rail.position = track.front();
    rail.bodyData = {1, 0, 0, 0, 5};
    for (int q = 0; q <= 4; ++q) {
        rail.bodyData.push_back(q);
        rail.bodyData.push_back(0);
    }
    rail.bodyData.push_back(0);
    s.objects.push_back(rail);
    s.objects.push_back({"input", {0, 0}, {}, {1, 0, 0, 0, 0}});
    s.objects.push_back({"input", {1, 0}, {}, {1, 0, 1, 0, 0}});
    s.objects.push_back({"glyph-purification", {0, 0}, {}, {1, 0, 0, 0, 0}});
    s.objects.push_back({"out-std", {4, 1}, {}, {1, 0, 0, 0, 0}});
    WritePuzzle(puzzle, p);
    for (int pass = 0; pass < 2; ++pass) {
        io::BinaryWriter::WriteSolutionFile(solution, s);
        const auto report = io::VerifySolutionFiles(puzzle, solution, 5000, 2000);
        ASSERT_TRUE(report.Passed()) << report.detail << " cycle=" << report.errorCycle;
        s.metrics = report.metrics;
    }
}
TEST_F(DisassemblyVerifierTest,
       LoweredPurificationReactsOnLastReagentReleaseWithIndependentVerification) {
    using IR = solvers::ExecutionIR;
    io::PuzzleData p;
    p.version = 3;
    p.name = "purification-on-release";
    p.outputMultiplier = 1;
    p.permissionBits = (1ULL << 0) | (1ULL << 3) | (1ULL << 15) | (1ULL << 22) | (1ULL << 23);
    io::Molecule lead, tin;
    lead.atoms.push_back({core::AtomType::Lead, {}});
    tin.atoms.push_back({core::AtomType::Tin, {}});
    p.inputs = {lead, lead};
    p.outputs = {tin};
    IR program;
    auto state = IR::Initial(p);
    program.Append(p, state, {IR::Kind::Acquire, 0});
    program.Append(p, state, {IR::Kind::Acquire, 1, {}, 0, 0, {2, -2}});
    program.Append(p, state, {IR::Kind::Relocate, 0, {2}, 0, 3, {3, -2}});
    program.Append(p, state, {IR::Kind::Purify, 0, {1, 2}});
    program.Append(p, state, {IR::Kind::Relocate, 0, {3}, 0, 0, {4, 1}});
    program.Append(p, state, {IR::Kind::Output, 0, {3}});
    const std::vector<core::HexCoord> track{{1, -1}, {2, -1}, {3, -1}, {4, -1}, {5, -1}, {5, 0},
                                            {5, 1},  {4, 2},  {3, 2},  {2, 2},  {1, 2},  {0, 2}};
    const solvers::HeldArmState start{1, 1, 4, 0};
    auto inventory = solvers::LowerInventoryIR(
        p, program, track, start,
        [](const auto&, const auto&, const auto&, char op, bool) {
            return op == 'A' || op == 'a' || op == 'R' || op == 'r' || op == 'G' || op == 'g';
        },
        [](const auto& step, const auto&) {
            if (step.operation.kind != IR::Kind::Purify) return true;
            return step.preconditions.atoms.at(1).position == core::HexCoord{0, 0} &&
                   step.preconditions.atoms.at(2).position == core::HexCoord{1, 0} &&
                   step.effects.atoms.at(3).position == core::HexCoord{0, 1};
        },
        1, 20000, {}, {{3, {{0, 1}}}});
    ASSERT_EQ(inventory.MoveEmpty(start), solvers::MotionStatus::Found);
    IR::RequireComplete(inventory.Material(), {1});
    io::SolutionData s;
    s.solutionName = "purification atomic release";
    io::SolutionObject arm;
    arm.typeName = "arm1";
    arm.position = track[start.trackIndex];
    int time = 0;
    for (char op : inventory.Tape())
        arm.instructions.push_back({op, time++});
    arm.bodyData = {1, 4, 0, time, 0};
    s.objects.push_back(arm);
    io::SolutionObject rail;
    rail.typeName = "track";
    rail.position = track.front();
    rail.bodyData = {1, 0, 0, 0, static_cast<int>(track.size())};
    for (auto cell : track) {
        cell = core::Subtract(cell, track.front());
        rail.bodyData.push_back(cell.q);
        rail.bodyData.push_back(cell.r);
    }
    rail.bodyData.push_back(0);
    s.objects.push_back(rail);
    s.objects.push_back({"input", {0, 0}, {}, {1, 0, 0, 0, 0}});
    s.objects.push_back({"input", {2, -2}, {}, {1, 0, 1, 0, 0}});
    s.objects.push_back({"glyph-purification", {0, 0}, {}, {1, 0, 0, 0, 0}});
    s.objects.push_back({"out-std", {4, 1}, {}, {1, 0, 0, 0, 0}});
    WritePuzzle(puzzle, p);
    for (int pass = 0; pass < 2; ++pass) {
        io::BinaryWriter::WriteSolutionFile(solution, s);
        const auto report = io::VerifySolutionFiles(puzzle, solution, 5000, 2000);
        ASSERT_TRUE(report.Passed()) << report.detail << " cycle=" << report.errorCycle;
        s.metrics = report.metrics;
    }
}
TEST_F(DisassemblyVerifierTest,
       ReleasedAnimismusDeliversBothCoproductsWithIndependentVerification) {
    using IR = solvers::ExecutionIR;
    io::PuzzleData p;
    p.version = 3;
    p.name = "released-animismus";
    p.outputMultiplier = 1;
    p.permissionBits = (1ULL << 0) | (1ULL << 3) | (1ULL << 16) | (1ULL << 22) | (1ULL << 23);
    io::Molecule salt, vitae, mors;
    salt.atoms.push_back({core::AtomType::Salt, {}});
    vitae.atoms.push_back({core::AtomType::Vitae, {}});
    mors.atoms.push_back({core::AtomType::Mors, {}});
    p.inputs = {salt, salt};
    p.outputs = {vitae, mors};
    IR program;
    auto state = IR::Initial(p);
    program.Append(p, state, {IR::Kind::Acquire, 0});
    program.Append(p, state, {IR::Kind::Acquire, 1, {}, 0, 0, {1, 0}});
    program.Append(p, state, {IR::Kind::Animismus, 0, {1, 2}});
    program.Append(p, state, {IR::Kind::Relocate, 0, {3}, 0, 0, {4, 1}});
    program.Append(p, state, {IR::Kind::Output, 0, {3}});
    program.Append(p, state, {IR::Kind::Relocate, 0, {4}, 0, 0, {4, -1}});
    program.Append(p, state, {IR::Kind::Output, 1, {4}});
    std::vector<core::HexCoord> track;
    for (int q = -1; q <= 4; ++q)
        track.push_back({q, 2});
    track.push_back({5, 1});
    for (int r = 0; r >= -2; --r)
        track.push_back({5, r});
    for (int q = 4; q >= 1; --q)
        track.push_back({q, -2});
    const solvers::HeldArmState start{0, 1, 5, 0};
    auto inventory = solvers::LowerInventoryIR(
        p, program, track, start,
        [](const auto&, const auto&, const auto&, char, bool) { return true; },
        [](const auto& step, const auto&) {
            if (step.operation.kind != IR::Kind::Animismus) return true;
            return step.preconditions.atoms.at(1).position == core::HexCoord{0, 0} &&
                   step.preconditions.atoms.at(2).position == core::HexCoord{1, 0} &&
                   step.effects.atoms.at(3).position == core::HexCoord{0, 1} &&
                   step.effects.atoms.at(4).position == core::HexCoord{1, -1};
        },
        1, 20000, {}, {{2, {{0, 1}, {1, -1}}}});
    ASSERT_EQ(inventory.MoveEmpty(start), solvers::MotionStatus::Found);
    IR::RequireComplete(inventory.Material(), {1, 1});
    io::SolutionData s;
    s.solutionName = "released animismus inventory";
    io::SolutionObject arm;
    arm.typeName = "arm1";
    arm.position = track.front();
    int time = 0;
    for (char op : inventory.Tape())
        arm.instructions.push_back({op, time++});
    arm.bodyData = {1, 5, 0, time, 0};
    s.objects.push_back(arm);
    io::SolutionObject rail;
    rail.typeName = "track";
    rail.position = track.front();
    rail.bodyData = {1, 0, 0, 0, static_cast<int>(track.size())};
    for (auto cell : track) {
        cell = core::Subtract(cell, track.front());
        rail.bodyData.push_back(cell.q);
        rail.bodyData.push_back(cell.r);
    }
    rail.bodyData.push_back(0);
    s.objects.push_back(rail);
    s.objects.push_back({"input", {0, 0}, {}, {1, 0, 0, 0, 0}});
    s.objects.push_back({"input", {1, 0}, {}, {1, 0, 1, 0, 0}});
    s.objects.push_back({"glyph-life-and-death", {0, 0}, {}, {1, 0, 0, 0, 0}});
    s.objects.push_back({"out-std", {4, 1}, {}, {1, 0, 0, 0, 0}});
    s.objects.push_back({"out-std", {4, -1}, {}, {1, 0, 1, 0, 0}});
    WritePuzzle(puzzle, p);
    for (int pass = 0; pass < 2; ++pass) {
        io::BinaryWriter::WriteSolutionFile(solution, s);
        const auto report = io::VerifySolutionFiles(puzzle, solution, 5000, 2000);
        ASSERT_TRUE(report.Passed()) << report.detail << " cycle=" << report.errorCycle;
        s.metrics = report.metrics;
    }
}
TEST_F(DisassemblyVerifierTest, InventoryCutsReservesAndTransportsBothFragments) {
    using IR = solvers::ExecutionIR;
    io::PuzzleData p;
    p.version = 3;
    p.name = "inventory-cut-transport";
    p.outputMultiplier = 1;
    p.permissionBits =
        (1ULL << 0) | (1ULL << 2) | (1ULL << 3) | (1ULL << 9) | (1ULL << 22) | (1ULL << 23);
    p.inputs = {{{{core::AtomType::Salt, {0, 0}}, {core::AtomType::Fire, {0, -1}}},
                 {{1, {0, 0}, {0, -1}}}}};
    p.outputs = {{{{core::AtomType::Salt, {0, 0}}}, {}}, {{{core::AtomType::Fire, {0, 0}}}, {}}};
    std::vector<core::HexCoord> track;
    for (int q = 0; q <= 10; ++q)
        track.push_back({q, 1});
    IR program;
    auto planned = IR::Initial(p);
    program.Append(p, planned, {IR::Kind::Acquire});
    program.Append(p, planned, {IR::Kind::Relocate, 0, {1}, 0, 0, {4, 0}});
    program.Append(p, planned, {IR::Kind::Unbond, 0, {1, 2}, 1});
    program.Append(p, planned, {IR::Kind::Relocate, 0, {2}, 0, 0, {3, 0}});
    program.Append(p, planned, {IR::Kind::Store, 7, {2}});
    program.Append(p, planned, {IR::Kind::Retrieve, 7});
    program.Append(p, planned, {IR::Kind::Relocate, 0, {2}, 0, 0, {3, 0}});
    program.Append(p, planned, {IR::Kind::Output, 1, {2}});
    program.Append(p, planned, {IR::Kind::Relocate, 0, {1}, 0, 0, {4, 0}});
    program.Append(p, planned, {IR::Kind::Output, 0, {1}});
    auto withoutPiston = p;
    withoutPiston.permissionBits &= ~(1ULL << 2);
    EXPECT_THROW(solvers::CompileDisassemblyIR(withoutPiston, program), IR::Unsupported);
    auto boundaryPuzzle = p;
    for (auto& atom : boundaryPuzzle.inputs.front().atoms)
        atom.position.r += 127;
    for (auto& bond : boundaryPuzzle.inputs.front().bonds) {
        bond.from.r += 127;
        bond.to.r += 127;
    }
    IR boundaryProgram;
    auto boundaryState = IR::Initial(boundaryPuzzle);
    for (const auto& step : program.steps)
        boundaryProgram.Append(boundaryPuzzle, boundaryState, step.operation);
    EXPECT_THROW(solvers::CompileDisassemblyIR(boundaryPuzzle, boundaryProgram), IR::Unsupported);
    auto compiled = solvers::CompileDisassemblyIR(p, program);
    WritePuzzle(puzzle, p);
    for (int pass = 0; pass < 2; ++pass) {
        io::BinaryWriter::WriteSolutionFile(solution, compiled);
        const auto report = io::VerifySolutionFiles(puzzle, solution, 5000, 2000);
        ASSERT_TRUE(report.Passed()) << report.detail << " cycle=" << report.errorCycle;
        compiled.metrics = report.metrics;
    }
    auto searched = solvers::CompileAcceptedFragmentFeed(p, [&](const io::SolutionData& candidate) {
        for (const auto& object : candidate.objects)
            if (object.typeName == "out-std" && object.position.q <= 5) return false;
        io::BinaryWriter::WriteSolutionFile(solution, candidate);
        return io::VerifySolutionFiles(puzzle, solution, 5000, 2000).Passed();
    });
    ASSERT_TRUE(searched.has_value());
    for (int pass = 0; pass < 2; ++pass) {
        io::BinaryWriter::WriteSolutionFile(solution, *searched);
        const auto report = io::VerifySolutionFiles(puzzle, solution, 5000, 2000);
        ASSERT_TRUE(report.Passed()) << report.detail;
        searched->metrics = report.metrics;
    }
    auto inventory = solvers::LowerInventoryIR(
        p, program, track, {0, 1, 4, 0},
        [&track](const auto& world, const auto& from, const auto& to, char op, bool holding) {
            if (!holding && (op == 'E' || op == 'e')) {
                auto tip = [&](const auto& arm) {
                    core::HexCoord reach{arm.length, 0};
                    for (int i = 0; i < arm.direction; ++i)
                        reach = core::RotateClockwise(reach);
                    return core::Add(track.at(arm.trackIndex), reach);
                };
                const auto a = tip(from), b = tip(to);
                for (const auto& [id, atom] : world.atoms) {
                    if (core::Distance(a, atom.position) + core::Distance(atom.position, b) ==
                        core::Distance(a, b))
                        return false;
                }
            }
            return from.direction == 4 && to.direction == 4 &&
                   (op == 'A' || op == 'a' || op == 'G' || op == 'g' ||
                    (!holding && (op == 'E' || op == 'e')));
        },
        [&program](const auto& step, const auto&) {
            if (step.operation.kind == IR::Kind::Acquire)
                return step.preconditions == program.steps.front().preconditions &&
                       step.effects == program.steps.front().effects;
            if (step.operation.kind == IR::Kind::Unbond)
                return step.preconditions.atoms.at(1).position == (core::HexCoord{4, 0}) &&
                       step.preconditions.atoms.at(2).position == (core::HexCoord{4, -1});
            if (step.operation.kind == IR::Kind::Output) {
                const auto id = step.operation.atoms.at(0);
                return step.preconditions.atoms.at(id).position ==
                       (id == 1 ? core::HexCoord{8, 0} : core::HexCoord{10, -1});
            }
            return false;
        },
        2);
    IR::RequireComplete(inventory.Material(), {1, 1});
    EXPECT_TRUE(inventory.Custody().Held().empty());
    EXPECT_TRUE(inventory.Custody().Parked().empty());
    ASSERT_EQ(inventory.MoveEmpty({0, 1, 4, 0}), solvers::MotionStatus::Found);
    io::SolutionData s;
    s.solutionName = "inventory cut and reuse";
    io::SolutionObject arm;
    arm.typeName = "piston";
    arm.position = {0, 1};
    arm.bodyData = {1, 4, 0, 0, 0};
    int time = 0;
    for (char op : inventory.Tape())
        arm.instructions.push_back({op, time++});
    arm.bodyData[3] = time;
    s.objects.push_back(arm);
    io::SolutionObject rail;
    rail.typeName = "track";
    rail.position = {0, 1};
    rail.bodyData = {1, 0, 0, 0, 11};
    for (int q = 0; q <= 10; ++q) {
        rail.bodyData.push_back(q);
        rail.bodyData.push_back(0);
    }
    rail.bodyData.push_back(0);
    s.objects.push_back(rail);
    s.objects.push_back({"input", {}, {}, {1, 0, 0, 0, 0}});
    s.objects.push_back({"unbonder", {4, -1}, {}, {1, 1, 0, 0, 0}});
    s.objects.push_back({"out-std", {8, 0}, {}, {1, 0, 0, 0, 0}});
    s.objects.push_back({"out-std", {10, -1}, {}, {1, 0, 1, 0, 0}});
    WritePuzzle(puzzle, p);
    for (int pass = 0; pass < 2; ++pass) {
        io::BinaryWriter::WriteSolutionFile(solution, s);
        auto report = io::VerifySolutionFiles(puzzle, solution, 5000, 2000);
        ASSERT_TRUE(report.Passed()) << report.detail << " cycle=" << report.errorCycle;
        s.metrics = report.metrics;
    }
}
TEST_F(DisassemblyVerifierTest, InventoryRejoinsParkedStockAndPassesIndependentVerifierTwice) {
    for (const bool calcify : {false, true}) {
        using IR = solvers::ExecutionIR;
        io::PuzzleData p;
        p.version = 3;
        p.name = "inventory-rejoin";
        p.outputMultiplier = 1;
        p.permissionBits =
            (1ULL << 0) | (1ULL << 3) | (1ULL << 8) | (1ULL << 9) | (1ULL << 22) | (1ULL << 23);
        p.inputs = {{{{core::AtomType::Salt, {0, 0}}, {core::AtomType::Fire, {1, 0}}},
                     {{1, {0, 0}, {1, 0}}}}};
        p.outputs = p.inputs;
        if (calcify) {
            p.permissionBits |= 1ULL << 12;
            p.inputs[0].atoms[0].type = core::AtomType::Air;
            p.inputs[0].atoms[1].type = core::AtomType::Copper;
            p.outputs[0].atoms[1].type = core::AtomType::Copper;
        }
        IR program;
        auto state = IR::Initial(p);
        program.Append(p, state, {IR::Kind::Acquire});
        program.Append(p, state, {IR::Kind::Relocate, 0, {1}, 0, 0, {4, 0}});
        program.Append(p, state, {IR::Kind::Unbond, 0, {1, 2}, 1});
        IR::AtomId product = 1;
        if (calcify) {
            program.Append(p, state, {IR::Kind::Relocate, 0, {1}, 0, 0, {-1, 0}});
            program.Append(p, state, {IR::Kind::Calcify, 0, {1}});
            product = 3;
            program.Append(p, state, {IR::Kind::Relocate, 0, {product}, 0, 0, {3, 0}});
        } else {
            program.Append(p, state, {IR::Kind::Relocate, 0, {product}, 0, 0, {-1, 0}});
        }
        program.Append(p, state, {IR::Kind::Relocate, 0, {2}, 0, 0, {3, 0}});
        program.Append(p, state, {IR::Kind::Relocate, 0, {product}, 0, 0, {4, 0}});
        program.Append(p, state, {IR::Kind::Bond, 0, {product, 2}, 1});
        program.Append(p, state, {IR::Kind::Relocate, 0, {2}, 0, 0, {4, 0}});
        program.Append(p, state, {IR::Kind::Output, 0, {product}});
        std::vector<core::HexCoord> track;
        for (int q = 0; q <= 12; ++q)
            track.push_back({q, 1});
        const auto inventory = solvers::LowerInventoryIR(
            p, program, track, {0, 1, 4, 0},
            [](const auto&, const auto& from, const auto& to, char op, bool) {
                return from.direction == 4 && to.direction == 4 &&
                       (op == 'A' || op == 'a' || op == 'G' || op == 'g');
            },
            [](const auto&, const auto&) { return true; });
        io::SolutionData s;
        s.solutionName = "inventory rejoin";
        io::SolutionObject arm;
        arm.typeName = "arm1";
        arm.position = {0, 1};
        arm.bodyData = {1, 4, 0, 0, 0};
        int time = 0;
        for (char op : inventory.Tape())
            arm.instructions.push_back({op, time++});
        arm.bodyData[3] = time;
        s.objects.push_back(arm);
        io::SolutionObject rail;
        rail.typeName = "track";
        rail.position = {0, 1};
        rail.bodyData = {1, 0, 0, 0, 13};
        for (int q = 0; q <= 12; ++q) {
            rail.bodyData.push_back(q);
            rail.bodyData.push_back(0);
        }
        rail.bodyData.push_back(0);
        s.objects.push_back(rail);
        s.objects.push_back({"input", {}, {}, {1, 0, 0, 0, 0}});
        s.objects.push_back({"unbonder", {4, 0}, {}, {1, 0, 0, 0, 0}});
        if (calcify) s.objects.push_back({"glyph-calcification", {3, 0}, {}, {1, 0, 0, 0, 0}});
        s.objects.push_back({"bonder", {7, 0}, {}, {1, 0, 0, 0, 0}});
        s.objects.push_back({"out-std", {11, 0}, {}, {1, 0, 0, 0, 0}});
        WritePuzzle(puzzle, p);
        for (int pass = 0; pass < 2; ++pass) {
            io::BinaryWriter::WriteSolutionFile(solution, s);
            const auto report = io::VerifySolutionFiles(puzzle, solution, 5000, 2000);
            ASSERT_TRUE(report.Passed()) << report.detail << " cycle=" << report.errorCycle;
            s.metrics = report.metrics;
        }
    }
}
TEST_F(DisassemblyVerifierTest, InventoryDuplicationAndCatalystRecoveryPassVerifierTwice) {
    using IR = solvers::ExecutionIR;
    io::PuzzleData p;
    p.version = 3;
    p.name = "inventory-duplication";
    p.outputMultiplier = 1;
    p.permissionBits = (1ULL << 0) | (1ULL << 3) | (1ULL << 13) | (1ULL << 22) | (1ULL << 23);
    p.inputs = {{{{core::AtomType::Salt, {0, 0}}}, {}}, {{{core::AtomType::Air, {5, 0}}}, {}}};
    p.outputs = {{{{core::AtomType::Air, {0, 0}}}, {}}};
    IR program;
    auto state = IR::Initial(p);
    program.Append(p, state, {IR::Kind::Acquire});
    program.Append(p, state, {IR::Kind::Acquire, 1});
    program.Append(p, state, {IR::Kind::Relocate, 0, {1}, 0, 0, {4, 0}});
    program.Append(p, state, {IR::Kind::Duplicate, 0, {1, 2}});
    program.Append(p, state, {IR::Kind::Relocate, 0, {3}, 0, 0, {2, 0}});
    program.Append(p, state, {IR::Kind::Output, 0, {3}});
    program.Append(p, state, {IR::Kind::Relocate, 0, {2}, 0, 0, {-3, 0}});
    program.Append(p, state, {IR::Kind::Output, 0, {2}});
    std::vector<core::HexCoord> track;
    for (int q = 0; q <= 8; ++q)
        track.push_back({q, 1});
    const auto inventory = solvers::LowerInventoryIR(
        p, program, track, {0, 1, 4, 0},
        [](const auto&, const auto& from, const auto& to, char op, bool) {
            return from.direction == 4 && to.direction == 4 &&
                   (op == 'A' || op == 'a' || op == 'G' || op == 'g');
        },
        [](const auto& step, const auto&) {
            return step.operation.kind != IR::Kind::Duplicate ||
                   (step.preconditions.atoms.at(1).position == core::HexCoord{4, 0} &&
                    step.preconditions.atoms.at(2).position == core::HexCoord{5, 0});
        },
        1, 20000, {2});
    io::SolutionData s;
    s.solutionName = "inventory duplication";
    io::SolutionObject arm;
    arm.typeName = "arm1";
    arm.position = {0, 1};
    arm.bodyData = {1, 4, 0, 0, 0};
    int time = 0;
    for (char op : inventory.Tape())
        arm.instructions.push_back({op, time++});
    arm.bodyData[3] = time;
    s.objects.push_back(arm);
    io::SolutionObject rail;
    rail.typeName = "track";
    rail.position = {0, 1};
    rail.bodyData = {1, 0, 0, 0, 9};
    for (int q = 0; q <= 8; ++q) {
        rail.bodyData.push_back(q);
        rail.bodyData.push_back(0);
    }
    rail.bodyData.push_back(0);
    s.objects.push_back(rail);
    s.objects.push_back({"input", {}, {}, {1, 0, 0, 0, 0}});
    s.objects.push_back({"input", {}, {}, {1, 0, 1, 0, 0}});
    s.objects.push_back({"glyph-duplication", {5, 0}, {}, {1, 3, 0, 0, 0}});
    s.objects.push_back({"out-std", {2, 0}, {}, {1, 0, 0, 0, 0}});
    WritePuzzle(puzzle, p);
    for (int pass = 0; pass < 2; ++pass) {
        io::BinaryWriter::WriteSolutionFile(solution, s);
        const auto report = io::VerifySolutionFiles(puzzle, solution, 5000, 2000);
        ASSERT_TRUE(report.Passed()) << report.detail << " cycle=" << report.errorCycle;
        s.metrics = report.metrics;
    }
}
TEST_F(DisassemblyVerifierTest, InventoryProjectionConsumesPartnerAndPassesVerifierTwice) {
    using IR = solvers::ExecutionIR;
    io::PuzzleData p;
    p.version = 3;
    p.name = "inventory-projection";
    p.outputMultiplier = 1;
    p.permissionBits = (1ULL << 0) | (1ULL << 3) | (1ULL << 14) | (1ULL << 22) | (1ULL << 23);
    p.inputs = {{{{core::AtomType::Lead, {0, 0}}}, {}},
                {{{core::AtomType::Quicksilver, {8, 0}}}, {}}};
    p.outputs = {{{{core::AtomType::Tin, {0, 0}}}, {}}};
    IR program;
    auto state = IR::Initial(p);
    program.Append(p, state, {IR::Kind::Acquire});
    program.Append(p, state, {IR::Kind::Acquire, 1});
    program.Append(p, state, {IR::Kind::Relocate, 0, {1}, 0, 0, {4, 0}});
    program.Append(p, state, {IR::Kind::Relocate, 0, {2}, 0, 0, {-3, 0}});
    program.Append(p, state, {IR::Kind::Project, 0, {1, 2}});
    program.Append(p, state, {IR::Kind::Relocate, 0, {3}, 0, 0, {2, 0}});
    program.Append(p, state, {IR::Kind::Output, 0, {3}});
    std::vector<core::HexCoord> track;
    for (int q = 0; q <= 8; ++q)
        track.push_back({q, 1});
    const auto inventory = solvers::LowerInventoryIR(
        p, program, track, {0, 1, 4, 0},
        [](const auto&, const auto& from, const auto& to, char op, bool) {
            return from.direction == 4 && to.direction == 4 &&
                   (op == 'A' || op == 'a' || op == 'G' || op == 'g');
        },
        [](const auto& step, const auto&) {
            return step.operation.kind != IR::Kind::Project ||
                   (step.preconditions.atoms.at(1).position == core::HexCoord{4, 0} &&
                    step.preconditions.atoms.at(2).position == core::HexCoord{5, 0});
        },
        1, 20000, {1});
    EXPECT_TRUE(inventory.Material().atoms.empty());
    EXPECT_TRUE(inventory.Custody().Parked().empty());
    io::SolutionData s;
    s.solutionName = "inventory projection";
    io::SolutionObject arm;
    arm.typeName = "arm1";
    arm.position = {0, 1};
    arm.bodyData = {1, 4, 0, 0, 0};
    int time = 0;
    for (char op : inventory.Tape())
        arm.instructions.push_back({op, time++});
    arm.bodyData[3] = time;
    s.objects.push_back(arm);
    io::SolutionObject rail;
    rail.typeName = "track";
    rail.position = {0, 1};
    rail.bodyData = {1, 0, 0, 0, 9};
    for (int q = 0; q <= 8; ++q) {
        rail.bodyData.push_back(q);
        rail.bodyData.push_back(0);
    }
    rail.bodyData.push_back(0);
    s.objects.push_back(rail);
    s.objects.push_back({"input", {}, {}, {1, 0, 0, 0, 0}});
    s.objects.push_back({"input", {}, {}, {1, 0, 1, 0, 0}});
    s.objects.push_back({"glyph-projection", {5, 0}, {}, {1, 3, 0, 0, 0}});
    s.objects.push_back({"out-std", {2, 0}, {}, {1, 0, 0, 0, 0}});
    WritePuzzle(puzzle, p);
    for (int pass = 0; pass < 2; ++pass) {
        io::BinaryWriter::WriteSolutionFile(solution, s);
        const auto report = io::VerifySolutionFiles(puzzle, solution, 5000, 2000);
        ASSERT_TRUE(report.Passed()) << report.detail << " cycle=" << report.errorCycle;
        s.metrics = report.metrics;
    }
}
TEST_F(DisassemblyVerifierTest, InventoryProjectionPreservesBondedMetalAndInheritedColor) {
    using IR = solvers::ExecutionIR;
    for (const std::uint8_t mask : {1, 14}) {
        io::PuzzleData p;
        p.version = 3;
        p.name = "bonded-metal-projection";
        p.outputMultiplier = 1;
        p.permissionBits = (1ULL << 0) | (1ULL << 3) | (1ULL << 14) | (1ULL << 22) | (1ULL << 23);
        p.inputs = {{{{core::AtomType::Lead, {0, 0}}, {core::AtomType::Quicksilver, {0, -1}}},
                     {{mask, {0, 0}, {0, -1}}}},
                    {{{core::AtomType::Quicksilver, {8, 0}}}, {}}};
        p.outputs = {p.inputs[0]};
        p.outputs[0].atoms[0].type = core::AtomType::Tin;
        IR program;
        auto state = IR::Initial(p);
        program.Append(p, state, {IR::Kind::Acquire, 0, {}});
        program.Append(p, state, {IR::Kind::Acquire, 1, {}});
        program.Append(p, state, {IR::Kind::Relocate, 0, {1}, 0, 0, {4, 0}});
        program.Append(p, state, {IR::Kind::Relocate, 0, {3}, 0, 0, {-3, 0}});
        program.Append(p, state, {IR::Kind::Project, 0, {1, 3}});
        program.Append(p, state, {IR::Kind::Relocate, 0, {4}, 0, 0, {2, 0}});
        program.Append(p, state, {IR::Kind::Output, 0, {4}});
        std::vector<core::HexCoord> track;
        for (int q = 0; q <= 8; ++q)
            track.push_back({q, 1});
        auto inventory = solvers::LowerInventoryIR(
            p, program, track, {0, 1, 4, 0},
            [](const auto&, const auto& from, const auto& to, char action, bool) {
                return from.direction == 4 && to.direction == 4 &&
                       (action == 'A' || action == 'a' || action == 'G' || action == 'g');
            },
            [](const auto& step, const auto&) {
                return step.operation.kind != IR::Kind::Project ||
                       (step.preconditions.atoms.at(1).position == core::HexCoord{4, 0} &&
                        step.preconditions.atoms.at(3).position == core::HexCoord{5, 0});
            });
        EXPECT_EQ(inventory.Material().batches, (std::vector<std::size_t>{1, 1}));
        EXPECT_TRUE(inventory.Material().atoms.empty());
        io::SolutionData machine;
        machine.solutionName = "bonded projection primitive";
        io::SolutionObject arm;
        arm.typeName = "arm1";
        arm.position = {0, 1};
        int time = 0;
        for (const auto action : inventory.Tape())
            arm.instructions.push_back({action, time++});
        arm.bodyData = {1, 4, 0, time, 0};
        machine.objects.push_back(arm);
        io::SolutionObject rail;
        rail.typeName = "track";
        rail.position = {0, 1};
        rail.bodyData = {1, 0, 0, 0, 9};
        for (int q = 0; q <= 8; ++q) {
            rail.bodyData.push_back(q);
            rail.bodyData.push_back(0);
        }
        rail.bodyData.push_back(0);
        machine.objects.push_back(rail);
        machine.objects.push_back({"input", {}, {}, {1, 0, 0, 0, 0}});
        machine.objects.push_back({"input", {}, {}, {1, 0, 1, 0, 0}});
        machine.objects.push_back({"glyph-projection", {5, 0}, {}, {1, 3, 0, 0, 0}});
        machine.objects.push_back({"out-std", {6, 0}, {}, {1, 0, 0, 0, 0}});
        WritePuzzle(puzzle, p);
        for (int pass = 0; pass < 2; ++pass) {
            io::BinaryWriter::WriteSolutionFile(solution, machine);
            const auto report = io::VerifySolutionFiles(puzzle, solution, 5000, 2000);
            ASSERT_TRUE(report.Passed())
                << "mask=" << static_cast<int>(mask) << ' ' << report.detail;
            machine.metrics = report.metrics;
        }
    }
}

TEST_F(DisassemblyVerifierTest, SerialArmExecutesOriginalTracklessMetalRing) {
    const auto root = opusoptima::test::RepoRoot(__FILE__);
    const auto original = root / "asset/puzzle/workshop-versions/w2868339730.puzzle";
    const auto p = io::BinaryParser::ParsePuzzleFile(original);
    const auto material = solvers::SearchMaterialExecution(p);
    ASSERT_TRUE(material.plan) << material.detail;
    std::string failures;
    auto machine = solvers::CompileAcceptedSerialArmExecution(
        p, *material.plan,
        [&](const auto& candidate) {
            io::BinaryWriter::WriteSolutionFile(solution, candidate);
            const auto report = io::VerifySolutionFiles(original, solution, 150000, 10000);
            failures += report.detail + " cycle=" + std::to_string(report.errorCycle) +
                        " coordinate=" + std::to_string(report.errorU) + "," +
                        std::to_string(report.errorV) + "\n";
            return report.Passed();
        },
        [&](const auto& detail) { failures += detail + "\n"; });
    ASSERT_TRUE(machine) << failures;
    for (int pass = 0; pass < 2; ++pass) {
        io::BinaryWriter::WriteSolutionFile(solution, *machine);
        const auto report = io::VerifySolutionFiles(original, solution, 150000, 10000);
        ASSERT_TRUE(report.Passed()) << report.detail;
        machine->metrics = report.metrics;
    }
}

TEST_F(DisassemblyVerifierTest, SerialArmExecutesOriginalInheritedMetalTree) {
    const auto root = opusoptima::test::RepoRoot(__FILE__);
    const auto original = root / "asset/puzzle/workshop-versions/w2946687073.puzzle";
    const auto p = io::BinaryParser::ParsePuzzleFile(original);
    const auto material = solvers::SearchMaterialExecution(p);
    ASSERT_TRUE(material.plan) << material.detail;
    std::string failures;
    auto machine = solvers::CompileAcceptedSerialArmExecution(
        p, *material.plan,
        [&](const auto& candidate) {
            io::BinaryWriter::WriteSolutionFile(solution, candidate);
            const auto report = io::VerifySolutionFiles(original, solution, 150000, 10000);
            failures += report.detail + " cycle=" + std::to_string(report.errorCycle) +
                        " at=" + std::to_string(report.errorU) + "," +
                        std::to_string(report.errorV) + "\n";
            int period = 0;
            for (const auto& part : candidate.objects)
                for (const auto& instruction : part.instructions)
                    period = std::max(period, instruction.parameter + 1);
            if (!report.Passed()) failures += "period=" + std::to_string(period) + "\n";
            if (!report.Passed())
                for (const auto& part : candidate.objects)
                    for (const auto& instruction : part.instructions)
                        if (std::abs(instruction.parameter -
                                     (report.errorCycle % std::max(1, period))) <= 2)
                            failures += part.typeName + "@" + std::to_string(part.position.q) +
                                        "," + std::to_string(part.position.r) + " " +
                                        instruction.action +
                                        " time=" + std::to_string(instruction.parameter) + "\n";
            return report.Passed();
        },
        [&](const auto& detail) { failures += detail + "\n"; });
    ASSERT_TRUE(machine) << failures;
    for (int pass = 0; pass < 2; ++pass) {
        io::BinaryWriter::WriteSolutionFile(solution, *machine);
        const auto report = io::VerifySolutionFiles(original, solution, 150000, 10000);
        ASSERT_TRUE(report.Passed()) << report.detail;
        machine->metrics = report.metrics;
    }
}

TEST_F(DisassemblyVerifierTest, SerialArmExecutesOriginalStagedWheelProduct) {
    const auto root = opusoptima::test::RepoRoot(__FILE__);
    const auto original = root / "asset/puzzle/workshop-versions/w2946687209.puzzle";
    const auto p = io::BinaryParser::ParsePuzzleFile(original);
    const auto material = solvers::SearchMaterialExecution(p);
    ASSERT_TRUE(material.plan) << material.detail;
    std::string failures;
    auto machine = solvers::CompileAcceptedSerialArmExecution(
        p, *material.plan,
        [&](const auto& candidate) {
            io::BinaryWriter::WriteSolutionFile(solution, candidate);
            const auto report = io::VerifySolutionFiles(original, solution, 150000, 10000);
            failures += report.detail + " cycle=" + std::to_string(report.errorCycle) +
                        " at=" + std::to_string(report.errorU) + "," +
                        std::to_string(report.errorV) + "\n";
            int period = 0;
            for (const auto& part : candidate.objects)
                for (const auto& instruction : part.instructions)
                    period = std::max(period, instruction.parameter + 1);
            if (!report.Passed()) failures += "period=" + std::to_string(period) + "\n";
            if (!report.Passed())
                for (const auto& part : candidate.objects)
                    for (const auto& instruction : part.instructions)
                        if (std::abs(instruction.parameter -
                                     (report.errorCycle % std::max(1, period))) <= 2)
                            failures += part.typeName + "@" + std::to_string(part.position.q) +
                                        "," + std::to_string(part.position.r) + " " +
                                        instruction.action +
                                        " time=" + std::to_string(instruction.parameter) + "\n";
            return report.Passed();
        },
        [&](const auto& detail) { failures += detail + "\n"; });
    ASSERT_TRUE(machine) << failures;
    for (int pass = 0; pass < 2; ++pass) {
        io::BinaryWriter::WriteSolutionFile(solution, *machine);
        const auto report = io::VerifySolutionFiles(original, solution, 150000, 10000);
        ASSERT_TRUE(report.Passed()) << report.detail;
        machine->metrics = report.metrics;
    }
}

TEST_F(DisassemblyVerifierTest, SerialArmIndependentFireOutputStation) {
    using T = core::AtomType;
    for (int mask : {2, 6, 14}) {
        SCOPED_TRACE(mask);
        io::PuzzleData p;
        p.version = 3; p.name = "serial-independent-fire";
        p.outputMultiplier = 1; p.permissionBits = ~0ULL;
        p.inputs = {{{{T::Fire, {0, 0}}}, {}}, {{{T::Salt, {0, 0}}}, {}}};
        p.outputs = {{{{T::Fire, {0, 0}}, {T::Fire, {1, 0}}},
                      {{static_cast<std::uint8_t>(mask), {0, 0}, {1, 0}}}},
                     {{{T::Salt, {0, 0}}, {T::Fire, {1, 0}}}, {{1, {0, 0}, {1, 0}}}}};
        WritePuzzle(puzzle, p);
        using IR = solvers::ExecutionIR;
        using K = IR::Kind;
        IR material;
        auto state = IR::Initial(p);
        for (std::size_t output = 0; output < p.outputs.size(); ++output) {
            const auto first = state.nextId;
            for (const auto& atom : p.outputs[output].atoms) {
                const auto id = state.nextId;
                material.AppendFromTail(p, state, {K::Acquire, atom.type == T::Fire ? 0U : 1U});
                material.AppendFromTail(p, state, {K::Relocate, 0, {id}, 0, 0, atom.position});
            }
            material.AppendFromTail(p, state,
                {K::Bond, 0, {first, first + 1}, p.outputs[output].bonds[0].bits});
            material.AppendFromTail(p, state, {K::Output, output, {first}});
        }
        const auto plan = solvers::MakeExecutionPlan(p, material);
        std::string errors;
        auto machine = solvers::CompileAcceptedSerialArmExecution(p, plan,
            [&](const auto& candidate) {
                io::BinaryWriter::WriteSolutionFile(solution, candidate);
                const auto report = io::VerifySolutionFiles(puzzle, solution, 150000, 10000);
                errors += report.detail + " cycle=" + std::to_string(report.errorCycle) + "\n";
                return report.Passed();
            }, [&](const auto& error) { errors += error + "\n"; });
        ASSERT_TRUE(machine) << errors;
        EXPECT_TRUE(std::any_of(machine->objects.begin(), machine->objects.end(),
            [](const auto& part) { return part.typeName == "bonder-prisma"; }));
        int period = 0;
        for (const auto& part : machine->objects)
            for (const auto& instruction : part.instructions)
                period = std::max(period, instruction.parameter + 1);
        for (int pass = 0; pass < 2; ++pass) {
            io::BinaryWriter::WriteSolutionFile(solution, *machine);
            const auto report = io::VerifySolutionFiles(puzzle, solution, 150000, 10000);
            ASSERT_TRUE(report.Passed()) << report.detail;
            machine->metrics = report.metrics;
        }
        RecordProperty("mask_" + std::to_string(mask) + "_period", period);
        RecordProperty("mask_" + std::to_string(mask) + "_cycles", machine->metrics.cycles);
        auto missing = *machine;
        std::erase_if(missing.objects, [](const auto& part) {
            return part.typeName == "bonder-prisma";
        });
        io::BinaryWriter::WriteSolutionFile(solution, missing);
        EXPECT_FALSE(io::VerifySolutionFiles(puzzle, solution, 150000, 10000).Passed());
        if (mask == 14) {
            p.outputs[0].bonds[0].bits = 2;
            WritePuzzle(puzzle, p);
            io::BinaryWriter::WriteSolutionFile(solution, *machine);
            EXPECT_FALSE(io::VerifySolutionFiles(puzzle, solution, 150000, 10000).Passed());
        }
    }
}

TEST_F(DisassemblyVerifierTest, SerialArmClosedWheelMixedColoredOutput) {
    using T = core::AtomType;
    using IR = solvers::ExecutionIR;
    using K = IR::Kind;
    io::PuzzleData p;
    p.version = 3; p.name = "closed-wheel-mixed-output";
    p.outputMultiplier = 1; p.permissionBits = ~0ULL;
    p.inputs = {{{{T::Earth, {0, 0}}, {T::Water, {1, 0}}, {T::Air, {2, 0}}},
                  {{1, {0, 0}, {1, 0}}, {1, {1, 0}, {2, 0}}}}};
    p.outputs = {{{{T::Fire, {0, 0}}, {T::Fire, {1, 0}}, {T::Salt, {2, 0}}, {T::Air, {0, 1}}},
                   {{14, {0, 0}, {1, 0}}, {1, {1, 0}, {2, 0}}, {1, {0, 0}, {0, 1}}}}};
    IR material;
    auto state = IR::Initial(p);
    auto append = [&](IR::Operation op) { material.AppendFromTail(p, state, op); };
    auto acquire = [&]() {
        const auto first = state.nextId;
        append({K::Acquire, 0});
        append({K::Unbond, 0, {first, first + 1}, 1});
        append({K::Unbond, 0, {first + 1, first + 2}, 1});
        return first;
    };
    auto convert = [&](IR::AtomId input, T element, core::HexCoord destination) {
        const auto salt = state.nextId;
        append({K::Calcify, 0, {input}});
        const auto product = state.nextId;
        IR::Operation duplicate{K::Duplicate, 0, {salt}};
        duplicate.wheelCatalyst = IR::WheelCatalyst{0, element, 0};
        append(duplicate);
        append({K::Relocate, 0, {product}, 0, 0,
            core::Subtract(destination, state.atoms.at(product).position)});
        return product;
    };
    const auto first = acquire();
    const auto salt = state.nextId;
    append({K::Calcify, 0, {first + 2}});
    append({K::Relocate, 0, {salt}, 0, 0,
        core::Subtract({2, 0}, state.atoms.at(salt).position)});
    const auto fireA = convert(first, T::Fire, {0, 0});
    const auto fireB = convert(first + 1, T::Fire, {1, 0});
    const auto second = acquire();
    const auto air = convert(second, T::Air, {0, 1});
    append({K::Bond, 0, {fireA, fireB}, 14});
    append({K::Bond, 0, {fireB, salt}, 1});
    append({K::Bond, 0, {fireA, air}, 1});
    append({K::Output, 0, {fireA}});
    append({K::Discard, 0, {second + 1}});
    append({K::Discard, 0, {second + 2}});
    IR::RequireComplete(state, {1});
    EXPECT_EQ(state.wheelDevices.size(), 1U);
    const auto plan = solvers::MakeExecutionPlan(p, material);
    WritePuzzle(puzzle, p);
    std::string errors;
    auto machine = solvers::CompileAcceptedSerialArmExecution(p, plan, [&](const auto& candidate) {
        io::BinaryWriter::WriteSolutionFile(solution, candidate);
        const auto report = io::VerifySolutionFiles(puzzle, solution, 150000, 10000);
        errors += report.detail + " cycle=" + std::to_string(report.errorCycle) + "\n";
        return report.Passed();
    }, [&](const auto& error) { errors += error + "\n"; });
    ASSERT_TRUE(machine) << errors;
    int period = 0;
    for (const auto& part : machine->objects)
        for (const auto& instruction : part.instructions)
            period = std::max(period, instruction.parameter + 1);
    for (int pass = 0; pass < 2; ++pass) {
        io::BinaryWriter::WriteSolutionFile(solution, *machine);
        const auto report = io::VerifySolutionFiles(puzzle, solution, 150000, 10000);
        ASSERT_TRUE(report.Passed()) << report.detail;
        machine->metrics = report.metrics;
    }
    RecordProperty("period", period);
    RecordProperty("native_cycles", machine->metrics.cycles);
    EXPECT_GT(machine->metrics.cycles, period);
    for (const std::string glyph : {"bonder-prisma", "bonder", "unbonder", "baron"}) {
        auto missing = *machine;
        std::erase_if(missing.objects, [&](const auto& part) { return part.typeName == glyph; });
        io::BinaryWriter::WriteSolutionFile(solution, missing);
        EXPECT_FALSE(io::VerifySolutionFiles(puzzle, solution, 150000, 10000).Passed()) << glyph;
    }
}

TEST_F(DisassemblyVerifierTest, OriginalClosedWheelMaterialCollection) {
    const auto root = opusoptima::test::RepoRoot(__FILE__);
    const auto original = root / "result/solver-generalization-conserved-period-20261108/inputs/"
        "1b47844e429729b179bf9b29d11b2d60dcd169a7c8d02ba53702f56bab8c18c5.puzzle";
    if (!std::filesystem::exists(original)) GTEST_SKIP() << "Original verified input unavailable";
    ASSERT_EQ(io::CandidateSha256(io::BinaryParser::ReadAllBytes(original)),
              "1b47844e429729b179bf9b29d11b2d60dcd169a7c8d02ba53702f56bab8c18c5");
    const auto p = io::BinaryParser::ParsePuzzleFile(original);
    ASSERT_EQ(p.outputMultiplier, 1);
    ASSERT_TRUE(solvers::SupportsClosedSingletonWheelPlanning(p));
    for (const bool defaultLimit : {false, true}) {
        SCOPED_TRACE(defaultLimit);
        test::ScopedTestDirectory scratch;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        io::VerificationOptions options;
        options.mode = io::VerificationMode::LegacyGate;
        options.outerDeadline = deadline;
        io::CandidateArchive archive(original, scratch.Path() / "first.solution", options);
        solvers::AssemblySolver solver;
        solver.SetDeadline(deadline);
        std::size_t submissions = 0;
        const solvers::AssemblySolver::CandidateSink sink = [&](const auto& candidate, const auto& stage) {
            ++submissions;
            EXPECT_LE(submissions, solvers::AssemblySolver::MaxCandidateSubmissions);
            EXPECT_TRUE(stage == "shared-plan-wheel" ||
                        stage.starts_with("shared-plan-wheel:parent="));
            return archive.Submit(candidate, stage, deadline);
        };
        const auto first = defaultLimit ? solver.CollectVerified(p, sink)
                                       : solver.CollectVerified(p, sink, 1);
        ASSERT_TRUE(archive.First());
        ASSERT_EQ(archive.First()->verification.authority, io::VerificationAuthority::LegacyGate);
        EXPECT_GE(submissions, 1U);
        EXPECT_LE(submissions, defaultLimit ? 3U : 1U);
        EXPECT_EQ(first.metrics.cycles, archive.First()->verification.metrics.cycles);
        EXPECT_EQ(first.metrics.cost, archive.First()->verification.metrics.cost);
        EXPECT_EQ(first.metrics.area, archive.First()->verification.metrics.area);
        EXPECT_EQ(io::BinaryWriter::WriteSolution(first), archive.First()->bytes);
        for (const auto& slot : archive.Slots()) {
            ASSERT_TRUE(slot);
            EXPECT_EQ(slot->verification.authority, io::VerificationAuthority::LegacyGate);
            if (!defaultLimit) EXPECT_EQ(slot, archive.First());
        }
        EXPECT_LT(std::chrono::steady_clock::now(), deadline);
        const std::string prefix = defaultLimit ? "default_" : "cap_one_";
        RecordProperty(prefix + "submissions", static_cast<int>(submissions));
        RecordProperty(prefix + "native_cycles", first.metrics.cycles);
        RecordProperty(prefix + "native_cost", first.metrics.cost);
        RecordProperty(prefix + "native_area", first.metrics.area);
        EXPECT_TRUE(archive.Snapshot(options).Passed());
    }
}

TEST_F(DisassemblyVerifierTest, SerialArmStagesWheelConvertedColoredComponent) {
    for (bool reverse : {false, true}) {
        io::PuzzleData p;
        p.version = 3;
        p.name = "staged-wheel-component";
        p.outputMultiplier = 1;
        p.permissionBits = ~0ULL;
        p.inputs = {{{{core::AtomType::Fire, {2, -3}}}, {}}};
        p.outputs = {{{{core::AtomType::Water, {0, 0}},
                       {core::AtomType::Fire, {-1, 1}},
                       {core::AtomType::Fire, {1, 0}},
                       {core::AtomType::Fire, {0, -1}}},
                      {{8, {0, 0}, {-1, 1}}, {8, {0, 0}, {1, 0}}, {8, {0, 0}, {0, -1}}}}};
        if (reverse) std::reverse(p.outputs[0].atoms.begin(), p.outputs[0].atoms.end());
        WritePuzzle(puzzle, p);
        const auto material = solvers::SearchMaterialExecution(p);
        ASSERT_TRUE(material.plan) << material.detail;
        std::string errors;
        auto machine = solvers::CompileAcceptedSerialArmExecution(
            p, *material.plan,
            [&](const auto& candidate) {
                io::BinaryWriter::WriteSolutionFile(solution, candidate);
                const auto report = io::VerifySolutionFiles(puzzle, solution, 150000, 10000);
                errors += report.detail + " cycle=" + std::to_string(report.errorCycle) + "\n";
                return report.Passed();
            },
            [&](const auto& error) { errors += error + "\n"; });
        ASSERT_TRUE(machine) << errors;
        for (int pass = 0; pass < 2; ++pass) {
            io::BinaryWriter::WriteSolutionFile(solution, *machine);
            const auto report = io::VerifySolutionFiles(puzzle, solution, 150000, 10000);
            ASSERT_TRUE(report.Passed()) << report.detail;
            machine->metrics = report.metrics;
        }
        auto denied = p;
        denied.permissionBits &= ~(1ULL << 3);
        bool accepted = false;
        EXPECT_FALSE(
            solvers::CompileAcceptedSerialArmExecution(denied, *material.plan, [&](const auto&) {
                accepted = true;
                return true;
            }));
        EXPECT_FALSE(accepted);
        const auto calcify =
            std::find_if(machine->objects.begin(), machine->objects.end(), [](const auto& part) {
                return part.typeName == "glyph-calcification" &&
                       part.position == core::HexCoord{6, 6};
            });
        const auto duplicate =
            std::find_if(machine->objects.begin(), machine->objects.end(),
                         [](const auto& part) { return part.typeName == "glyph-duplication"; });
        ASSERT_NE(calcify, machine->objects.end());
        ASSERT_NE(duplicate, machine->objects.end());
        std::iter_swap(calcify, duplicate);
        io::BinaryWriter::WriteSolutionFile(solution, *machine);
        EXPECT_FALSE(io::VerifySolutionFiles(puzzle, solution, 150000, 10000).Passed());
    }
}

TEST_F(DisassemblyVerifierTest, BondedCalcificationAndWheelDuplicationRetainColoredStar) {
    io::PuzzleData p;
    p.version = 3;
    p.name = "bonded-cardinal-wheel-semantics";
    p.outputMultiplier = 1;
    p.permissionBits = (1ULL << 12) | (1ULL << 13) | (1ULL << 28);
    p.inputs = {{{{core::AtomType::Fire, {0, 0}},
                  {core::AtomType::Fire, {-1, 1}},
                  {core::AtomType::Fire, {1, 0}},
                  {core::AtomType::Fire, {0, -1}}},
                 {{8, {0, 0}, {-1, 1}}, {8, {0, 0}, {1, 0}}, {8, {0, 0}, {0, -1}}}}};
    p.outputs = p.inputs;
    p.outputs[0].atoms[0].type = core::AtomType::Water;
    io::SolutionData machine;
    machine.solutionName = "bonded endpoint wheel conversion";
    machine.objects = {{"input", {}, {}, {1, 0, 0, 0, 0}},
                       {"glyph-calcification", {}, {}, {1, 0, 0, 0, 0}},
                       {"glyph-duplication", {0, 1}, {}, {1, 4, 0, 0, 0}},
                       {"baron", {0, 2}, {}, {1, 3, 0, 0, 0}},
                       {"out-std", {}, {}, {1, 0, 0, 0, 0}}};
    WritePuzzle(puzzle, p);
    for (int pass = 0; pass < 2; ++pass) {
        io::BinaryWriter::WriteSolutionFile(solution, machine);
        const auto report = io::VerifySolutionFiles(puzzle, solution, 5000, 2000);
        ASSERT_TRUE(report.Passed())
            << report.detail << " at " << report.errorU << ',' << report.errorV;
        machine.metrics = report.metrics;
    }
}

TEST_F(DisassemblyVerifierTest, InventoryReacquiresAfterDisposalAndPassesVerifierTwice) {
    using IR = solvers::ExecutionIR;
    for (int turns = 0; turns < 6; ++turns) {
        SCOPED_TRACE(turns);
        io::PuzzleData p;
        p.version = 3;
        p.name = "inventory-disposal-refeed";
        p.outputMultiplier = 1;
        p.permissionBits = (1ULL << 0) | (1ULL << 3) | (1ULL << 17) | (1ULL << 22) | (1ULL << 23);
        p.inputs = {{{{core::AtomType::Salt, {3, -2}}}, {}}};
        p.outputs = {{{{core::AtomType::Salt, {0, 0}}}, {}}};
        core::HexFrame frame{{}, turns};
        frame.origin = core::Subtract({}, frame.ToWorld(p.inputs[0].atoms[0].position));
        IR program;
        auto state = IR::Initial(p);
        program.Append(p, state, {IR::Kind::Acquire, 0, {}, 0, frame.Turns(), frame.origin});
        program.Append(p, state, {IR::Kind::Relocate, 0, {1}, 0, 0, {4, 0}});
        program.Append(p, state, {IR::Kind::Discard, 0, {1}});
        program.Append(p, state, {IR::Kind::Acquire, 0, {}, 0, frame.Turns(), frame.origin});
        program.Append(p, state, {IR::Kind::Relocate, 0, {2}, 0, 0, {8, 0}});
        program.Append(p, state, {IR::Kind::Output, 0, {2}});
        std::vector<core::HexCoord> track;
        for (int q = 0; q <= 8; ++q)
            track.push_back({q, 1});
        const auto inventory = solvers::LowerInventoryIR(
            p, program, track, {0, 1, 4, 0},
            [](const auto&, const auto& from, const auto& to, char op, bool) {
                return from.direction == 4 && to.direction == 4 &&
                       (op == 'A' || op == 'a' || op == 'G' || op == 'g');
            },
            [](const auto&, const auto&) { return true; });
        io::SolutionData s;
        s.solutionName = "inventory disposal refeed";
        io::SolutionObject arm;
        arm.typeName = "arm1";
        arm.position = {0, 1};
        arm.bodyData = {1, 4, 0, 0, 0};
        int time = 0;
        for (char op : inventory.Tape())
            arm.instructions.push_back({op, time++});
        arm.bodyData[3] = time;
        s.objects.push_back(arm);
        io::SolutionObject rail;
        rail.typeName = "track";
        rail.position = {0, 1};
        rail.bodyData = {1, 0, 0, 0, 9};
        for (int q = 0; q <= 8; ++q) {
            rail.bodyData.push_back(q);
            rail.bodyData.push_back(0);
        }
        rail.bodyData.push_back(0);
        s.objects.push_back(rail);
        s.objects.push_back({"input", frame.origin, {}, {1, frame.Turns(), 0, 0, 0}});
        s.objects.push_back({"glyph-disposal", {4, 0}, {}, {1, 0, 0, 0, 0}});
        s.objects.push_back({"out-std", {8, 0}, {}, {1, 0, 0, 0, 0}});
        WritePuzzle(puzzle, p);
        for (int pass = 0; pass < 2; ++pass) {
            io::BinaryWriter::WriteSolutionFile(solution, s);
            const auto report = io::VerifySolutionFiles(puzzle, solution, 5000, 2000);
            ASSERT_TRUE(report.Passed()) << report.detail << " cycle=" << report.errorCycle;
            s.metrics = report.metrics;
        }
    }
}
TEST_F(DisassemblyVerifierTest, InventoryCalcifiesHeldWorldStockAndPassesVerifierTwice) {
    using IR = solvers::ExecutionIR;
    io::PuzzleData p;
    p.version = 3;
    p.name = "inventory-world-calcification";
    p.outputMultiplier = 1;
    p.permissionBits = (1ULL << 0) | (1ULL << 3) | (1ULL << 12) | (1ULL << 22) | (1ULL << 23);
    p.inputs = {{{{core::AtomType::Air, {0, 0}}}, {}}};
    p.outputs = {{{{core::AtomType::Salt, {0, 0}}}, {}}};
    IR program;
    auto state = IR::Initial(p);
    program.Append(p, state, {IR::Kind::Acquire});
    program.Append(p, state, {IR::Kind::Relocate, 0, {1}, 0, 0, {4, 0}});
    program.Append(p, state, {IR::Kind::Calcify, 0, {1}});
    program.Append(p, state, {IR::Kind::Relocate, 0, {2}, 0, 0, {8, 0}});
    program.Append(p, state, {IR::Kind::Output, 0, {2}});
    std::vector<core::HexCoord> track;
    for (int q = 0; q <= 8; ++q)
        track.push_back({q, 1});
    const auto inventory = solvers::LowerInventoryIR(
        p, program, track, {0, 1, 4, 0},
        [](const auto&, const auto& from, const auto& to, char op, bool) {
            return from.direction == 4 && to.direction == 4 &&
                   (op == 'A' || op == 'a' || op == 'G' || op == 'g');
        },
        [](const auto& step, const auto&) {
            if (step.operation.kind != IR::Kind::Calcify) return true;
            return step.effects.atoms.at(step.preconditions.nextId).position ==
                   core::HexCoord{4, 0};
        });
    IR::RequireComplete(inventory.Material(), {1});
    io::SolutionData s;
    s.solutionName = "inventory held calcification";
    io::SolutionObject arm;
    arm.typeName = "arm1";
    arm.position = {0, 1};
    arm.bodyData = {1, 4, 0, 0, 0};
    int time = 0;
    for (char op : inventory.Tape())
        arm.instructions.push_back({op, time++});
    arm.bodyData[3] = time;
    s.objects.push_back(arm);
    io::SolutionObject rail;
    rail.typeName = "track";
    rail.position = {0, 1};
    rail.bodyData = {1, 0, 0, 0, 9};
    for (int q = 0; q <= 8; ++q) {
        rail.bodyData.push_back(q);
        rail.bodyData.push_back(0);
    }
    rail.bodyData.push_back(0);
    s.objects.push_back(rail);
    s.objects.push_back({"input", {}, {}, {1, 0, 0, 0, 0}});
    s.objects.push_back({"glyph-calcification", {4, 0}, {}, {1, 0, 0, 0, 0}});
    s.objects.push_back({"out-std", {8, 0}, {}, {1, 0, 0, 0, 0}});
    WritePuzzle(puzzle, p);
    for (int pass = 0; pass < 2; ++pass) {
        io::BinaryWriter::WriteSolutionFile(solution, s);
        const auto report = io::VerifySolutionFiles(puzzle, solution, 5000, 2000);
        ASSERT_TRUE(report.Passed()) << report.detail << " cycle=" << report.errorCycle;
        s.metrics = report.metrics;
    }
}
TEST_F(DisassemblyVerifierTest, PlannerSupplyInventoryPassesIndependentVerifierTwice) {
    auto p = WideFeed();
    p.name = "planner-supply-inventory";
    p.permissionBits |= (1ULL << 8);
    p.inputs = {{{{core::AtomType::Salt, {3, -2}}}, {}}};
    p.outputs = {{{{core::AtomType::Salt, {0, 0}}}, {}}};
    const auto supply = solvers::AssemblyPlanner::PlanSupply(p);
    const auto program = solvers::BuildSupplyExecutionIR(p, solvers::AssemblyPlanner::Plan(p),
                                                         supply, {{8, 0}, 0}, {{{-3, 2}, 0}});
    std::vector<core::HexCoord> track;
    for (int q = 0; q <= 8; ++q)
        track.push_back({q, 1});
    const auto inventory = solvers::LowerInventoryIR(
        p, program, track, {0, 1, 4, 0},
        [](const auto&, const auto& from, const auto& to, char op, bool) {
            return from.direction == 4 && to.direction == 4 &&
                   (op == 'A' || op == 'a' || op == 'G' || op == 'g');
        },
        [](const auto& step, const auto&) {
            return step.operation.kind != solvers::ExecutionIR::Kind::Output ||
                   step.preconditions.atoms.begin()->second.position == core::HexCoord{8, 0};
        },
        1, 20000, {supply.productsPerPeriod});
    io::SolutionData s;
    s.solutionName = "planner supply inventory";
    io::SolutionObject arm;
    arm.typeName = "arm1";
    arm.position = {0, 1};
    arm.bodyData = {1, 4, 0, 0, 0};
    int time = 0;
    for (char op : inventory.Tape())
        arm.instructions.push_back({op, time++});
    arm.bodyData[3] = time;
    s.objects.push_back(arm);
    io::SolutionObject rail;
    rail.typeName = "track";
    rail.position = {0, 1};
    rail.bodyData = {1, 0, 0, 0, 9};
    for (int q = 0; q <= 8; ++q) {
        rail.bodyData.push_back(q);
        rail.bodyData.push_back(0);
    }
    rail.bodyData.push_back(0);
    s.objects.push_back(rail);
    s.objects.push_back({"input", {-3, 2}, {}, {1, 0, 0, 0, 0}});
    s.objects.push_back({"out-std", {8, 0}, {}, {1, 0, 0, 0, 0}});
    WritePuzzle(puzzle, p);
    for (int pass = 0; pass < 2; ++pass) {
        io::BinaryWriter::WriteSolutionFile(solution, s);
        const auto report = io::VerifySolutionFiles(puzzle, solution, 5000, 2000);
        ASSERT_TRUE(report.Passed()) << report.detail << " cycle=" << report.errorCycle;
        s.metrics = report.metrics;
    }
}
TEST_F(DisassemblyVerifierTest, TranslationCompilerPreservesRotatedInputPortFrames) {
    for (int rotation = 0; rotation < 6; ++rotation) {
        auto p = WideFeed();
        const core::HexFrame frame{{-7, 4}, rotation};
        solvers::ExecutionIR program;
        auto state = solvers::ExecutionIR::Initial(p);
        using K = solvers::ExecutionIR::Kind;
        program.Append(p, state, {K::Acquire, 0, {}, 0, rotation, frame.origin});
        program.Append(p, state, {K::Relocate, 0, {1}, 0, 0, {20, 0}});
        program.Append(p, state, {K::Output, 0, {1}});
        auto s = solvers::CompileTranslationIR(p, program);
        EXPECT_EQ(s.objects[2].position, frame.origin);
        EXPECT_EQ(s.objects[2].bodyData[1], rotation);
        WritePuzzle(puzzle, p);
        for (int pass = 0; pass < 2; ++pass) {
            io::BinaryWriter::WriteSolutionFile(solution, s);
            const auto report = io::VerifySolutionFiles(puzzle, solution, 5000, 2000);
            ASSERT_TRUE(report.Passed()) << "rotation=" << rotation << " " << report.detail
                                         << " cycle=" << report.errorCycle;
            s.metrics = report.metrics;
        }
    }
}
TEST_F(DisassemblyVerifierTest, TranslationCompilerConsumesIRDistance) {
    for (const int distance : {8, 13}) {
        auto p = WideFeed();
        solvers::ExecutionIR program;
        auto state = solvers::ExecutionIR::Initial(p);
        using K = solvers::ExecutionIR::Kind;
        program.Append(p, state, {K::Acquire});
        program.Append(p, state, {K::Relocate, 0, {1}, 0, 0, {distance, 0}});
        program.Append(p, state, {K::Output, 0, {1}});
        auto s = solvers::CompileTranslationIR(p, program);
        ASSERT_EQ(s.objects[1].typeName, "track");
        EXPECT_EQ(s.objects[1].bodyData.at(4), distance + 1);
        WritePuzzle(puzzle, p);
        for (int pass = 0; pass < 2; ++pass) {
            io::BinaryWriter::WriteSolutionFile(solution, s);
            const auto report = io::VerifySolutionFiles(puzzle, solution, 5000, 2000);
            ASSERT_TRUE(report.Passed()) << report.detail << " cycle=" << report.errorCycle;
            s.metrics = report.metrics;
        }
    }
}
TEST_F(DisassemblyVerifierTest, TranslationCompilerParksAndRetrievesIRStock) {
    auto p = WideFeed();
    solvers::ExecutionIR program;
    auto state = solvers::ExecutionIR::Initial(p);
    using K = solvers::ExecutionIR::Kind;
    program.Append(p, state, {K::Acquire});
    program.Append(p, state, {K::Relocate, 0, {1}, 0, 0, {8, 0}});
    program.Append(p, state, {K::Store, 7, {1}});
    program.Append(p, state, {K::Retrieve, 7});
    program.Append(p, state, {K::Relocate, 0, {1}, 0, 0, {8, 0}});
    program.Append(p, state, {K::Output, 0, {1}});
    auto s = solvers::CompileTranslationIR(p, program);
    EXPECT_EQ(s.objects[0].bodyData.at(3), 36);
    const auto& tape = s.objects[0].instructions;
    ASSERT_GE(tape.size(), 13U);
    EXPECT_EQ(tape[9].action, 'g');
    EXPECT_EQ(tape[10].action, 'G');
    EXPECT_EQ(tape[11].action, 'A');
    EXPECT_EQ(tape[12].action, 'A');
    WritePuzzle(puzzle, p);
    for (int pass = 0; pass < 2; ++pass) {
        io::BinaryWriter::WriteSolutionFile(solution, s);
        const auto report = io::VerifySolutionFiles(puzzle, solution, 5000, 2000);
        ASSERT_TRUE(report.Passed()) << report.detail << " cycle=" << report.errorCycle;
        s.metrics = report.metrics;
    }
}
TEST_F(DisassemblyVerifierTest, DisassemblyRejectsEarlyGlyphContactInEveryInputFrame) {
    using IR = solvers::ExecutionIR;
    for (int rotation = 0; rotation < 6; ++rotation) {
        for (int offset : {-20, 0, 20}) {
            SCOPED_TRACE(::testing::Message() << "rotation=" << rotation << " offset=" << offset);
            io::PuzzleData p;
            p.version = 3;
            p.outputMultiplier = 1;
            p.permissionBits =
                (1ULL << 0) | (1ULL << 3) | (1ULL << 9) | (1ULL << 22) | (1ULL << 23);
            const core::HexFrame frame{{offset, 0}, rotation};
            const auto a = frame.ToLocal({0, 0});
            const auto b = frame.ToLocal({1, 0});
            p.inputs = {{{{core::AtomType::Salt, a}, {core::AtomType::Fire, b}}, {{1, a, b}}}};
            p.outputs = {{{{core::AtomType::Salt, {0, 0}}}, {}},
                         {{{core::AtomType::Fire, {0, 0}}}, {}}};
            IR program;
            auto state = IR::Initial(p);
            program.Append(p, state, {IR::Kind::Acquire, 0, {}, 0, rotation, frame.origin});
            program.Append(p, state, {IR::Kind::Relocate, 0, {1}, 0, 0, {8, 0}});
            program.Append(p, state, {IR::Kind::Unbond, 0, {1, 2}, 1});
            program.Append(p, state, {IR::Kind::Output, 0, {1}});
            program.Append(p, state, {IR::Kind::Output, 1, {2}});
            EXPECT_THROW(solvers::CompileDisassemblyIR(p, program), IR::Unsupported);
        }
    }
}
TEST_F(DisassemblyVerifierTest, DisassemblyPreservesInputFramesAndPassesVerifierTwice) {
    using IR = solvers::ExecutionIR;
    for (int rotation = 0; rotation < 6; ++rotation) {
        SCOPED_TRACE(rotation);
        io::PuzzleData p;
        p.version = 3;
        p.name = "framed-disassembly";
        p.outputMultiplier = 1;
        p.permissionBits = (1ULL << 0) | (1ULL << 3) | (1ULL << 9) | (1ULL << 22) | (1ULL << 23);
        const core::HexFrame frame{{-20, 3}, rotation};
        const auto a = frame.ToLocal({0, 0});
        const auto b = frame.ToLocal({0, 1});
        p.inputs = {{{{core::AtomType::Salt, a}, {core::AtomType::Fire, b}}, {{1, a, b}}}};
        p.outputs = {{{{core::AtomType::Salt, {0, 0}}}, {}},
                     {{{core::AtomType::Fire, {0, 0}}}, {}}};
        IR program;
        auto state = IR::Initial(p);
        program.Append(p, state, {IR::Kind::Acquire, 0, {}, 0, rotation, frame.origin});
        program.Append(p, state, {IR::Kind::Relocate, 0, {1}, 0, 0, {8, 0}});
        program.Append(p, state, {IR::Kind::Unbond, 0, {1, 2}, 1});
        program.Append(p, state, {IR::Kind::Output, 0, {1}});
        program.Append(p, state, {IR::Kind::Output, 1, {2}});
        auto s = solvers::CompileDisassemblyIR(p, program);
        WritePuzzle(puzzle, p);
        for (int pass = 0; pass < 2; ++pass) {
            io::BinaryWriter::WriteSolutionFile(solution, s);
            const auto report = io::VerifySolutionFiles(puzzle, solution, 5000, 2000);
            ASSERT_TRUE(report.Passed()) << report.detail << " cycle=" << report.errorCycle;
            s.metrics = report.metrics;
        }
    }
}
TEST_F(DisassemblyVerifierTest, DisassemblyCompilerConsumesExplicitCutsAndOutputs) {
    io::PuzzleData p;
    p.version = 3;
    p.name = "ir-fragment-cut";
    p.outputMultiplier = 1;
    p.permissionBits = (1ULL << 0) | (1ULL << 3) | (1ULL << 9) | (1ULL << 22) | (1ULL << 23);
    p.inputs = {{{{core::AtomType::Salt, {0, 0}},
                  {core::AtomType::Fire, {0, 1}},
                  {core::AtomType::Water, {0, 2}}},
                 {{1, {0, 0}, {0, 1}}, {1, {0, 1}, {0, 2}}}}};
    p.outputs = {{{{core::AtomType::Salt, {0, 0}}}, {}},
                 {{{core::AtomType::Fire, {0, 0}}}, {}},
                 {{{core::AtomType::Water, {0, 0}}}, {}}};
    solvers::ExecutionIR program;
    auto state = solvers::ExecutionIR::Initial(p);
    using K = solvers::ExecutionIR::Kind;
    program.Append(p, state, {K::Acquire});
    program.Append(p, state, {K::Relocate, 0, {1}, 0, 0, {8, 0}});
    program.Append(p, state, {K::Unbond, 0, {1, 2}, 1});
    program.Append(p, state, {K::Unbond, 0, {2, 3}, 1});
    for (std::size_t i = 0; i < 3; ++i)
        program.Append(p, state, {K::Output, i, {i + 1}});
    auto s = solvers::CompileDisassemblyIR(p, program);
    WritePuzzle(puzzle, p);
    for (int pass = 0; pass < 2; ++pass) {
        io::BinaryWriter::WriteSolutionFile(solution, s);
        const auto report = io::VerifySolutionFiles(puzzle, solution, 5000, 2000);
        ASSERT_TRUE(report.Passed()) << report.detail << " cycle=" << report.errorCycle;
        s.metrics = report.metrics;
    }
    s = solvers::AssemblySolver{}.Solve(p);
    EXPECT_EQ(s.solutionName, "IR fragment disassembly");
    for (int pass = 0; pass < 2; ++pass) {
        io::BinaryWriter::WriteSolutionFile(solution, s);
        const auto report = io::VerifySolutionFiles(puzzle, solution, 5000, 2000);
        ASSERT_TRUE(report.Passed()) << report.detail;
        s.metrics = report.metrics;
    }
    int candidates = 0;
    s = solvers::AssemblySolver{}.SolveAccepted(p, [&](const io::SolutionData& candidate) {
        if (++candidates == 1) return false;
        io::BinaryWriter::WriteSolutionFile(solution, candidate);
        return io::VerifySolutionFiles(puzzle, solution, 5000, 2000).Passed();
    });
    EXPECT_EQ(candidates, 2);
    for (int pass = 0; pass < 2; ++pass) {
        io::BinaryWriter::WriteSolutionFile(solution, s);
        const auto report = io::VerifySolutionFiles(puzzle, solution, 5000, 2000);
        ASSERT_TRUE(report.Passed()) << report.detail;
        s.metrics = report.metrics;
    }
    int rejected = 0;
    EXPECT_FALSE(solvers::CompileAcceptedFragmentFeed(p, [&](const auto&) {
        ++rejected;
        return false;
    }));
    EXPECT_GE(rejected, 3);
    EXPECT_THROW(solvers::AssemblySolver{}.SolveAccepted(p,
                                                         [](const auto&) -> bool {
                                                             throw std::invalid_argument(
                                                                 "acceptance failure must escape");
                                                         }),
                 std::invalid_argument);
    auto tampered = program;
    tampered.steps[2].operation.bits = 2;
    EXPECT_THROW(solvers::CompileDisassemblyIR(p, tampered), solvers::ExecutionIR::Error);
    auto denied = p;
    denied.permissionBits &= ~(1ULL << 9);
    EXPECT_THROW(solvers::CompileDisassemblyIR(denied, program), solvers::ExecutionIR::Unsupported);
}
TEST_F(DisassemblyVerifierTest, FragmentFeedRejectsUnreachableMaterialBeforeCutEnumeration) {
    io::PuzzleData p;
    p.version = 3;
    p.name = "unreachable-fragment-material";
    p.outputMultiplier = 1;
    p.permissionBits = (1ULL << 0) | (1ULL << 3) | (1ULL << 9) | (1ULL << 22) | (1ULL << 23);
    p.inputs = {
        {{{core::AtomType::Salt, {0, 0}}, {core::AtomType::Salt, {1, 0}}}, {{1, {0, 0}, {1, 0}}}}};
    p.outputs = {{{{core::AtomType::Fire, {0, 0}}}, {}}};
    int candidates = 0;
    auto reject = [&](const auto&) {
        ++candidates;
        return false;
    };
    EXPECT_THROW(solvers::CompileAcceptedFragmentFeed(p, reject), std::invalid_argument);
    p.outputs = {{{{core::AtomType::Salt, {0, 0}},
                   {core::AtomType::Salt, {1, 0}},
                   {core::AtomType::Salt, {2, 0}}},
                  {}}};
    EXPECT_THROW(solvers::CompileAcceptedFragmentFeed(p, reject), std::invalid_argument);
    EXPECT_EQ(candidates, 0);
}
TEST_F(DisassemblyVerifierTest, FragmentFeedRejectsIncompatibleGeometryAndMasks) {
    io::PuzzleData p;
    p.version = 3;
    p.name = "fragment-signature";
    p.outputMultiplier = 1;
    p.permissionBits = (1ULL << 0) | (1ULL << 3) | (1ULL << 9) | (1ULL << 22) | (1ULL << 23);
    p.inputs = {{{{core::AtomType::Fire, {0, 0}},
                  {core::AtomType::Fire, {1, 0}},
                  {core::AtomType::Fire, {2, 0}},
                  {core::AtomType::Fire, {3, 0}}},
                 {{2, {0, 0}, {1, 0}}, {2, {1, 0}, {2, 0}}, {1, {2, 0}, {3, 0}}}}};
    p.outputs = {{{{core::AtomType::Fire, {0, 0}},
                   {core::AtomType::Fire, {1, 0}},
                   {core::AtomType::Fire, {0, 1}}},
                  {{2, {0, 0}, {1, 0}}, {2, {0, 0}, {0, 1}}}}};
    int candidates = 0;
    auto reject = [&](const auto&) {
        ++candidates;
        return false;
    };
    EXPECT_FALSE(solvers::CompileAcceptedFragmentFeed(p, reject));
    p.outputs = {
        {{{core::AtomType::Fire, {0, 0}}, {core::AtomType::Fire, {1, 0}}}, {{4, {0, 0}, {1, 0}}}}};
    EXPECT_FALSE(solvers::CompileAcceptedFragmentFeed(p, reject));
    p.outputs = {p.outputs.front(), p.outputs.front()};
    p.outputs[0].bonds[0].bits = 2;
    p.outputs[1].bonds[0].bits = 2;
    EXPECT_FALSE(solvers::CompileAcceptedFragmentFeed(p, reject));
    EXPECT_EQ(candidates, 0);
}
TEST_F(DisassemblyVerifierTest, FragmentFeedDisposesUnmatchedResidual) {
    io::PuzzleData p;
    p.version = 3;
    p.name = "fragment-residual-disposal";
    p.outputMultiplier = 1;
    p.permissionBits = (1ULL << 0) | (1ULL << 2) | (1ULL << 3) | (1ULL << 9) | (1ULL << 17) |
                       (1ULL << 22) | (1ULL << 23);
    p.inputs = {{{{core::AtomType::Salt, {0, 0}},
                  {core::AtomType::Fire, {1, 0}},
                  {core::AtomType::Water, {2, 0}}},
                 {{14, {0, 0}, {1, 0}}, {1, {1, 0}, {2, 0}}}}};
    p.outputs = {
        {{{core::AtomType::Salt, {0, 0}}, {core::AtomType::Fire, {1, 0}}}, {{14, {0, 0}, {1, 0}}}}};
    for (const int mask : {1, 2, 4, 6, 8, 10, 12, 14}) {
        for (int rotation = 0; rotation < 6; ++rotation) {
            auto variant = p;
            variant.inputs[0].bonds[0].bits = mask;
            variant.outputs[0].bonds[0].bits = mask;
            auto turn = [rotation](core::HexCoord position) {
                for (int i = 0; i < rotation; ++i)
                    position = core::RotateClockwise(position);
                return position;
            };
            for (auto& atom : variant.inputs[0].atoms)
                atom.position = turn(atom.position);
            for (auto& bond : variant.inputs[0].bonds) {
                bond.from = turn(bond.from);
                bond.to = turn(bond.to);
            }
            std::reverse(variant.inputs[0].atoms.begin(), variant.inputs[0].atoms.end());
            WritePuzzle(puzzle, variant);
            std::string failure;
            auto candidate = solvers::CompileAcceptedFragmentFeed(variant, [&](const auto& s) {
                io::BinaryWriter::WriteSolutionFile(solution, s);
                const auto report = io::VerifySolutionFiles(puzzle, solution, 5000, 2000);
                failure = report.detail;
                return report.Passed();
            });
            ASSERT_TRUE(candidate) << failure;
            for (int pass = 0; pass < 2; ++pass) {
                io::BinaryWriter::WriteSolutionFile(solution, *candidate);
                const auto report = io::VerifySolutionFiles(puzzle, solution, 5000, 2000);
                ASSERT_TRUE(report.Passed()) << report.detail;
                candidate->metrics = report.metrics;
            }
        }
    }
    p.permissionBits &= ~(1ULL << 17);
    EXPECT_FALSE(solvers::CompileAcceptedFragmentFeed(p, [](const auto&) { return true; }));
}

TEST_F(DisassemblyVerifierTest, FragmentFeedDisposesMultipleResidualComponents) {
    io::PuzzleData p;
    p.version = 3;
    p.name = "fragment-multiple-residuals";
    p.outputMultiplier = 1;
    p.permissionBits = (1ULL << 0) | (1ULL << 2) | (1ULL << 3) | (1ULL << 9) | (1ULL << 17) |
                       (1ULL << 22) | (1ULL << 23);
    p.inputs = {{{{core::AtomType::Salt, {0, 0}},
                  {core::AtomType::Fire, {1, 0}},
                  {core::AtomType::Water, {1, 1}},
                  {core::AtomType::Air, {0, 1}}},
                 {{14, {0, 0}, {1, 0}}, {1, {1, 0}, {1, 1}}, {1, {1, 1}, {0, 1}}}}};
    p.outputs = {
        {{{core::AtomType::Salt, {0, 0}}, {core::AtomType::Fire, {1, 0}}}, {{14, {0, 0}, {1, 0}}}}};
    WritePuzzle(puzzle, p);
    std::string failure;
    auto candidate = solvers::CompileAcceptedFragmentFeed(p, [&](const auto& s) {
        io::BinaryWriter::WriteSolutionFile(solution, s);
        const auto report = io::VerifySolutionFiles(puzzle, solution, 5000, 2000);
        failure = report.detail + " cycle=" + std::to_string(report.errorCycle) +
                  " at=" + std::to_string(report.errorU) + "," + std::to_string(report.errorV);
        return report.Passed();
    });
    ASSERT_TRUE(candidate) << failure;
    for (int pass = 0; pass < 2; ++pass) {
        io::BinaryWriter::WriteSolutionFile(solution, *candidate);
        const auto report = io::VerifySolutionFiles(puzzle, solution, 5000, 2000);
        ASSERT_TRUE(report.Passed()) << report.detail;
        candidate->metrics = report.metrics;
    }
}

TEST_F(DisassemblyVerifierTest, FragmentFeedPreservesUncutColoredBonds) {
    io::PuzzleData p;
    p.version = 3;
    p.name = "ir-preserved-fragment";
    p.outputMultiplier = 1;
    p.permissionBits = (1ULL << 0) | (1ULL << 3) | (1ULL << 9) | (1ULL << 22) | (1ULL << 23);
    p.inputs = {{{{core::AtomType::Salt, {0, 0}},
                  {core::AtomType::Fire, {0, 1}},
                  {core::AtomType::Water, {0, 2}}},
                 {{2, {0, 0}, {0, 1}}, {1, {0, 1}, {0, 2}}}}};
    p.outputs = {
        {{{core::AtomType::Fire, {0, 0}}, {core::AtomType::Salt, {1, 0}}}, {{2, {0, 0}, {1, 0}}}},
        {{{core::AtomType::Water, {0, 0}}}, {}}};
    auto s = solvers::AssemblySolver{}.Solve(p);
    EXPECT_EQ(s.solutionName, "IR fragment disassembly");
    WritePuzzle(puzzle, p);
    for (int pass = 0; pass < 2; ++pass) {
        io::BinaryWriter::WriteSolutionFile(solution, s);
        const auto report = io::VerifySolutionFiles(puzzle, solution, 5000, 2000);
        ASSERT_TRUE(report.Passed()) << report.detail;
        s.metrics = report.metrics;
    }
    for (auto& atom : p.inputs[0].atoms)
        std::swap(atom.position.q, atom.position.r);
    for (auto& bond : p.inputs[0].bonds) {
        std::swap(bond.from.q, bond.from.r);
        std::swap(bond.to.q, bond.to.r);
    }
    for (const std::uint8_t mask : {1, 2, 4, 6, 8, 10, 12, 14}) {
        p.inputs[0].bonds[0].bits = mask;
        p.outputs[0].bonds[0].bits = mask;
        for (int rotation = 0; rotation < 6; ++rotation) {
            s = solvers::CompileFragmentFeed(p);
            WritePuzzle(puzzle, p);
            for (int pass = 0; pass < 2; ++pass) {
                io::BinaryWriter::WriteSolutionFile(solution, s);
                const auto report = io::VerifySolutionFiles(puzzle, solution, 5000, 2000);
                ASSERT_TRUE(report.Passed()) << "mask=" << static_cast<int>(mask)
                                             << " rotation=" << rotation << " " << report.detail;
                s.metrics = report.metrics;
            }
            for (auto& atom : p.inputs[0].atoms)
                atom.position = core::RotateClockwise(atom.position);
            for (auto& bond : p.inputs[0].bonds) {
                bond.from = core::RotateClockwise(bond.from);
                bond.to = core::RotateClockwise(bond.to);
            }
        }
    }
}
TEST_F(DisassemblyVerifierTest, FragmentFeedConsumesDistinctIdenticalFragments) {
    io::PuzzleData p;
    p.version = 3;
    p.name = "distinct-identical-fragments";
    p.outputMultiplier = 1;
    p.permissionBits = (1ULL << 0) | (1ULL << 3) | (1ULL << 9) | (1ULL << 22) | (1ULL << 23);
    p.inputs = {{{{core::AtomType::Salt, {0, 0}},
                  {core::AtomType::Fire, {1, 0}},
                  {core::AtomType::Salt, {2, 0}},
                  {core::AtomType::Fire, {3, 0}}},
                 {{2, {0, 0}, {1, 0}}, {1, {1, 0}, {2, 0}}, {2, {2, 0}, {3, 0}}}}};
    p.outputs = {
        {{{core::AtomType::Salt, {0, 0}}, {core::AtomType::Fire, {1, 0}}}, {{2, {0, 0}, {1, 0}}}}};
    p.outputs.push_back(p.outputs.front());
    auto s = solvers::CompileFragmentFeed(p);
    WritePuzzle(puzzle, p);
    for (int pass = 0; pass < 2; ++pass) {
        io::BinaryWriter::WriteSolutionFile(solution, s);
        const auto report = io::VerifySolutionFiles(puzzle, solution, 5000, 2000);
        ASSERT_TRUE(report.Passed()) << report.detail;
        s.metrics = report.metrics;
    }
}
TEST_F(DisassemblyVerifierTest, FragmentFeedRejectsUnbalancedPeriodWithoutSuppressingFallback) {
    io::PuzzleData p;
    p.version = 3;
    p.name = "unbalanced-fragment-period";
    p.outputMultiplier = 1;
    p.permissionBits =
        (1ULL << 0) | (1ULL << 2) | (1ULL << 3) | (1ULL << 9) | (1ULL << 22) | (1ULL << 23);
    io::Molecule input;
    for (int q = 0; q < 5; ++q) {
        input.atoms.push_back({core::AtomType::Salt, {q, 0}});
        if (q) input.bonds.push_back({1, {q - 1, 0}, {q, 0}});
    }
    p.inputs = {input};
    p.outputs = {{{{core::AtomType::Salt, {0, 0}}}, {}}};
    int candidates = 0;
    EXPECT_FALSE(solvers::CompileAcceptedFragmentFeed(p, [&](const auto&) {
        ++candidates;
        return true;
    }));
    EXPECT_EQ(candidates, 0);
}
TEST_F(DisassemblyVerifierTest, FragmentFeedConsumesRepeatedFragmentsAtOnePort) {
    for (std::uint8_t mask : {1, 2, 4, 6, 8, 10, 12, 14}) {
        SCOPED_TRACE(mask);
        io::PuzzleData p;
        p.version = 3;
        p.name = "repeated-fragment-period";
        p.outputMultiplier = 1;
        p.permissionBits =
            (1ULL << 0) | (1ULL << 2) | (1ULL << 3) | (1ULL << 9) | (1ULL << 22) | (1ULL << 23);
        p.inputs = {{{{core::AtomType::Salt, {0, 0}},
                      {core::AtomType::Fire, {1, 0}},
                      {core::AtomType::Salt, {2, 0}},
                      {core::AtomType::Fire, {3, 0}}},
                     {{mask, {0, 0}, {1, 0}}, {1, {1, 0}, {2, 0}}, {mask, {2, 0}, {3, 0}}}}};
        p.outputs = {{{{core::AtomType::Salt, {0, 0}}, {core::AtomType::Fire, {1, 0}}},
                      {{mask, {0, 0}, {1, 0}}}}};
        for (int rotation = 0; rotation < 6; ++rotation) {
            SCOPED_TRACE(rotation);
            std::reverse(p.inputs[0].atoms.begin(), p.inputs[0].atoms.end());
            auto s = solvers::CompileFragmentFeed(p);
            EXPECT_EQ(
                std::count_if(s.objects.begin(), s.objects.end(),
                              [](const auto& object) { return object.typeName == "out-std"; }),
                1);
            WritePuzzle(puzzle, p);
            for (int pass = 0; pass < 2; ++pass) {
                io::BinaryWriter::WriteSolutionFile(solution, s);
                const auto report = io::VerifySolutionFiles(puzzle, solution, 5000, 2000);
                ASSERT_TRUE(report.Passed()) << report.detail;
                s.metrics = report.metrics;
            }
            for (auto& atom : p.inputs[0].atoms)
                atom.position = core::RotateClockwise(atom.position);
            for (auto& bond : p.inputs[0].bonds) {
                bond.from = core::RotateClockwise(bond.from);
                bond.to = core::RotateClockwise(bond.to);
            }
        }
    }
}
TEST_F(DisassemblyVerifierTest, TranslationCompilerRejectsUnsupportedAndTamperedPrograms) {
    auto p = WideFeed();
    using IR = solvers::ExecutionIR;
    using K = IR::Kind;
    auto make = [&](int distance, int rotation) {
        IR program;
        auto state = IR::Initial(p);
        program.Append(p, state, {K::Acquire});
        program.Append(p, state, {K::Relocate, 0, {1}, 0, rotation, {distance, 0}});
        program.Append(p, state, {K::Output, 0, {1}});
        return program;
    };
    EXPECT_THROW(solvers::CompileTranslationIR(p, make(1, 0)), IR::Unsupported);
    EXPECT_THROW(solvers::CompileTranslationIR(p, make(8, 1)), IR::Unsupported);
    EXPECT_THROW(solvers::CompileTranslationIR(p, make(4097, 0)), IR::Unsupported);
    auto program = make(8, 0);
    program.steps[1].operation.translation.q = 9;
    EXPECT_THROW(solvers::CompileTranslationIR(p, program), IR::Error);
    IR incomplete;
    auto state = IR::Initial(p);
    incomplete.Append(p, state, {K::Acquire});
    EXPECT_THROW(solvers::CompileTranslationIR(p, incomplete), IR::Error);
    IR stored;
    state = IR::Initial(p);
    stored.Append(p, state, {K::Acquire});
    stored.Append(p, state, {K::Store, 0, {1}});
    stored.Append(p, state, {K::Retrieve});
    stored.Append(p, state, {K::Relocate, 0, {1}, 0, 0, {8, 0}});
    stored.Append(p, state, {K::Output, 0, {1}});
    EXPECT_THROW(solvers::CompileTranslationIR(p, stored), IR::Unsupported);
    IR diagonal;
    state = IR::Initial(p);
    diagonal.Append(p, state, {K::Acquire});
    diagonal.Append(p, state, {K::Relocate, 0, {1}, 0, 0, {8, 1}});
    diagonal.Append(p, state, {K::Output, 0, {1}});
    EXPECT_THROW(solvers::CompileTranslationIR(p, diagonal), IR::Unsupported);
}
TEST_F(DisassemblyVerifierTest, PartialColorInputExtraction) {
    for (const int bits : {2, 4, 6, 8, 10, 12, 14}) {
        SCOPED_TRACE(bits);
        io::PuzzleData p;
        p.version = 3;
        p.name = "partial-color-extraction";
        p.outputMultiplier = 1;
        p.permissionBits = solvers::AssemblyPlanner::RequiredPermissions | (1ULL << 2) |
                           (1ULL << 3) | (1ULL << 9) | (1ULL << 17);
        p.inputs = {{{{core::AtomType::Fire, {0, 0}}, {core::AtomType::Fire, {1, 0}}},
                     {{static_cast<std::uint8_t>(bits), {0, 0}, {1, 0}}}}};
        p.outputs = {{{{core::AtomType::Fire, {0, 0}}}, {}}};
        WritePuzzle(puzzle, p);
        auto s = solvers::AssemblySolver{}.Solve(p);
        for (int pass = 0; pass < 2; ++pass) {
            io::BinaryWriter::WriteSolutionFile(solution, s);
            const auto report = io::VerifySolutionFiles(puzzle, solution, 1000000, 10000);
            ASSERT_TRUE(report.Passed()) << report.detail << " cycle=" << report.errorCycle;
            s.metrics = report.metrics;
        }
        p.inputs[0].atoms[0].type = core::AtomType::Lead;
        p.inputs[0].atoms[1].type = core::AtomType::Quicksilver;
        p.outputs[0].atoms[0].type = core::AtomType::Lead;
        WritePuzzle(puzzle, p);
        s = solvers::AssemblySolver{}.Solve(p);
        for (int pass = 0; pass < 2; ++pass) {
            io::BinaryWriter::WriteSolutionFile(solution, s);
            const auto report = io::VerifySolutionFiles(puzzle, solution, 1000000, 10000);
            ASSERT_TRUE(report.Passed()) << report.detail << " cycle=" << report.errorCycle;
            s.metrics = report.metrics;
        }
    }
}
TEST_F(DisassemblyVerifierTest, IgnoresUnusedMolecularFeeds) {
    const auto root = opusoptima::test::RepoRoot(__FILE__);
    for (const auto* file : {"asset/puzzle/easy/infinite-row-condition.puzzle",
                             "asset/puzzle/edge-cases/area-test-1776837428738.puzzle"}) {
        SCOPED_TRACE(file);
        const auto source = root / file;
        auto p = io::BinaryParser::ParsePuzzleFile(source);
        auto s = solvers::AssemblySolver{}.Solve(p);
        bool hasInput = false;
        for (const auto& object : s.objects)
            if (object.typeName == "input") {
                hasInput = true;
                const auto index = object.bodyData.at(2);
                ASSERT_LT(index, p.inputs.size());
                EXPECT_EQ(p.inputs[index].atoms.size(), 1);
            }
        EXPECT_TRUE(hasInput);
        for (int pass = 0; pass < 2; ++pass) {
            io::BinaryWriter::WriteSolutionFile(solution, s);
            const auto report = io::VerifySolutionFiles(source, solution, 1000000, 10000);
            ASSERT_TRUE(report.Passed()) << report.detail << " cycle=" << report.errorCycle;
            s.metrics = report.metrics;
        }
    }
}
TEST_F(DisassemblyVerifierTest, OriginalWideExtraction) {
    const auto root = opusoptima::test::RepoRoot(__FILE__);
    for (const auto& path :
         {"asset/puzzle/journal/journal8/P103.puzzle", "asset/puzzle/tourney-2019/week8.puzzle"}) {
        auto p = io::BinaryParser::ParsePuzzleFile(root / path);
        auto s = solvers::AssemblySolver{}.Solve(p);
        io::BinaryWriter::WriteSolutionFile(solution, s);
        auto report = io::VerifySolutionFiles(root / path, solution, 1000000, 10000);
        ASSERT_TRUE(report.Passed())
            << path << ' ' << report.detail << " cycle=" << report.errorCycle
            << " at=" << report.errorU << ',' << report.errorV;
        s.metrics = report.metrics;
        io::BinaryWriter::WriteSolutionFile(solution, s);
        EXPECT_TRUE(io::VerifySolutionFiles(root / path, solution, 1000000, 10000).Passed());
    }
}
TEST_F(DisassemblyVerifierTest, ConstructedWideExtractionRotations) {
    for (int rotation = 0; rotation < 6; ++rotation) {
        auto p = WideFeed();
        p.name = "constructed-wide-extraction";
        p.permissionBits |= (1ULL << 8) | (1ULL << 2) | (1ULL << 9) | (1ULL << 17);
        p.outputs = {{{{core::AtomType::Salt, {0, 0}}}, {}}};
        for (auto& a : p.inputs[0].atoms)
            for (int n = 0; n < rotation; ++n)
                a.position = core::RotateClockwise(a.position);
        for (auto& b : p.inputs[0].bonds)
            for (int n = 0; n < rotation; ++n) {
                b.from = core::RotateClockwise(b.from);
                b.to = core::RotateClockwise(b.to);
            }
        WritePuzzle(puzzle, p);
        auto s = solvers::AssemblySolver{}.Solve(p);
        io::BinaryWriter::WriteSolutionFile(solution, s);
        auto report = io::VerifySolutionFiles(puzzle, solution, 1000000, 10000);
        ASSERT_TRUE(report.Passed())
            << rotation << ' ' << report.detail << " cycle=" << report.errorCycle;
        s.metrics = report.metrics;
        io::BinaryWriter::WriteSolutionFile(solution, s);
        EXPECT_TRUE(io::VerifySolutionFiles(puzzle, solution, 1000000, 10000).Passed());
    }
}
TEST_F(DisassemblyVerifierTest, DisconnectedWideExtractionRotations) {
    for (int rotation = 0; rotation < 6; ++rotation) {
        SCOPED_TRACE(rotation);
        auto p = WideFeed();
        p.name = "disconnected-wide-extraction";
        p.permissionBits |= (1ULL << 8) | (1ULL << 2) | (1ULL << 9) | (1ULL << 17);
        p.outputMultiplier = 2;
        // One bonded pair and seventeen isolated atoms share implicit links on spawn.
        p.inputs[0].bonds.resize(1);
        p.outputs = {{{{core::AtomType::Salt, {0, 0}}}, {}}};
        for (auto& a : p.inputs[0].atoms)
            for (int n = 0; n < rotation; ++n)
                a.position = core::RotateClockwise(a.position);
        for (auto& b : p.inputs[0].bonds)
            for (int n = 0; n < rotation; ++n) {
                b.from = core::RotateClockwise(b.from);
                b.to = core::RotateClockwise(b.to);
            }
        WritePuzzle(puzzle, p);
        auto s = solvers::AssemblySolver{}.Solve(p);
        io::BinaryWriter::WriteSolutionFile(solution, s);
        auto report = io::VerifySolutionFiles(puzzle, solution, 1000000, 10000);
        ASSERT_TRUE(report.Passed()) << report.detail << " cycle=" << report.errorCycle;
        s.metrics = report.metrics;
        io::BinaryWriter::WriteSolutionFile(solution, s);
        EXPECT_TRUE(io::VerifySolutionFiles(puzzle, solution, 1000000, 10000).Passed());
    }
}
TEST_F(DisassemblyVerifierTest, MixedAndMultipleWideFeeds) {
    for (bool secondWide : {false, true})
        for (bool disconnected : {false, true}) {
            SCOPED_TRACE(secondWide);
            SCOPED_TRACE(disconnected);
            auto p = WideFeed();
            p.permissionBits |= (1ULL << 8) | (1ULL << 2) | (1ULL << 9) | (1ULL << 17);
            if (disconnected) p.inputs[0].bonds.resize(1);
            auto second =
                secondWide ? p.inputs[0] : io::Molecule{{{core::AtomType::Fire, {0, 0}}}, {}};
            for (auto& a : second.atoms)
                a.type = core::AtomType::Fire;
            p.inputs.push_back(second);
            p.outputs = {{{{core::AtomType::Salt, {0, 0}}, {core::AtomType::Fire, {1, 0}}},
                          {{1, {0, 0}, {1, 0}}}}};
            WritePuzzle(puzzle, p);
            auto s = solvers::AssemblySolver{}.Solve(p);
            io::BinaryWriter::WriteSolutionFile(solution, s);
            auto report = io::VerifySolutionFiles(puzzle, solution, 1000000, 10000);
            ASSERT_TRUE(report.Passed()) << report.detail << " cycle=" << report.errorCycle
                                         << " at=" << report.errorU << ',' << report.errorV;
            s.metrics = report.metrics;
            io::BinaryWriter::WriteSolutionFile(solution, s);
            EXPECT_TRUE(io::VerifySolutionFiles(puzzle, solution, 1000000, 10000).Passed());
        }
}
TEST_F(DisassemblyVerifierTest, WideExtractionInventoryBuffers) {
    auto p = WideFeed();
    p.permissionBits |= (1ULL << 8) | (1ULL << 2) | (1ULL << 9);
    const auto layout = solvers::PlanExtractionLayout(p.inputs[0]);
    std::size_t first = 0;
    for (std::size_t i = 1; i < layout.cells.size(); ++i) {
        const auto a = layout.cells[i], b = layout.cells[first];
        if (a.r > b.r || (a.r == b.r && a.q > b.q)) first = i;
    }
    int fireCount = 0;
    for (std::size_t i = 0; i < layout.cells.size(); ++i) {
        if (layout.cells[i].r == layout.cells[first].r) {
            p.inputs[0].atoms[i].type = core::AtomType::Fire;
            ++fireCount;
        }
    }
    p.outputs[0] = {};
    const int count = static_cast<int>(p.inputs[0].atoms.size());
    for (int q = 0; q < count; ++q) {
        p.outputs[0].atoms.push_back(
            {q < fireCount ? core::AtomType::Fire : core::AtomType::Salt, {q, 0}});
        if (q) p.outputs[0].bonds.push_back({1, {q - 1, 0}, {q, 0}});
    }
    const auto supply = solvers::AssemblyPlanner::PlanSupply(p);
    ASSERT_TRUE(std::any_of(supply.steps.begin(), supply.steps.end(), [](const auto& step) {
        return step.kind == solvers::SupplyPlan::Kind::Store;
    }));
    ASSERT_TRUE(std::any_of(supply.steps.begin(), supply.steps.end(), [](const auto& step) {
        return step.buffer >= 0 && step.kind != solvers::SupplyPlan::Kind::Store;
    }));
    p.outputMultiplier = 2;
    WritePuzzle(puzzle, p);
    auto s = solvers::AssemblySolver{}.Solve(p);
    io::BinaryWriter::WriteSolutionFile(solution, s);
    auto report = io::VerifySolutionFiles(puzzle, solution, 1000000, 10000);
    ASSERT_TRUE(report.Passed()) << report.detail << " cycle=" << report.errorCycle
                                 << " at=" << report.errorU << ',' << report.errorV;
    s.metrics = report.metrics;
    io::BinaryWriter::WriteSolutionFile(solution, s);
    EXPECT_TRUE(io::VerifySolutionFiles(puzzle, solution, 1000000, 10000).Passed());

    auto& boundedFeed = p.inputs[0];
    std::erase_if(boundedFeed.atoms, [](const auto& atom) {
        return core::Distance(atom.position, {}) > 1 || atom.position.r < 0;
    });
    std::erase_if(boundedFeed.bonds, [](const auto& bond) {
        return core::Distance(bond.from, {}) > 1 || core::Distance(bond.to, {}) > 1 ||
               bond.from.r < 0 || bond.to.r < 0;
    });
    const auto boundedLayout = solvers::PlanExtractionLayout(boundedFeed);
    const int topRow = std::max_element(boundedLayout.cells.begin(), boundedLayout.cells.end(),
                                        [](auto a, auto b) { return a.r < b.r; })
                           ->r;
    int boundedFireCount = 0;
    for (std::size_t i = 0; i < boundedFeed.atoms.size(); ++i) {
        const bool fire = boundedLayout.cells[i].r == topRow;
        boundedFeed.atoms[i].type = fire ? core::AtomType::Fire : core::AtomType::Salt;
        boundedFireCount += fire;
    }
    p.outputs[0] = {};
    for (int q = 0; q < static_cast<int>(boundedFeed.atoms.size()); ++q) {
        p.outputs[0].atoms.push_back(
            {q < boundedFireCount ? core::AtomType::Fire : core::AtomType::Salt, {q, 0}});
        if (q) p.outputs[0].bonds.push_back({1, {q - 1, 0}, {q, 0}});
    }
    const auto automaticSupply = solvers::AssemblyPlanner::PlanSupply(p);
    ASSERT_TRUE(std::any_of(
        automaticSupply.steps.begin(), automaticSupply.steps.end(),
        [](const auto& step) { return step.kind == solvers::SupplyPlan::Kind::Store; }));
    ASSERT_TRUE(std::any_of(
        automaticSupply.steps.begin(), automaticSupply.steps.end(), [](const auto& step) {
            return step.buffer >= 0 && step.kind != solvers::SupplyPlan::Kind::Store;
        }));
    WritePuzzle(puzzle, p);
    std::string automaticFailures;
    auto automatic = solvers::CompileVerifiedSupplyInventory(
        p,
        [&](const auto& candidate) {
            io::BinaryWriter::WriteSolutionFile(solution, candidate);
            return io::VerifySolutionFiles(puzzle, solution, 1000000, 10000);
        },
        [&](const auto& failure) {
            if (!automaticFailures.empty()) automaticFailures += '\n';
            automaticFailures += failure.detail;
        });
    ASSERT_TRUE(automatic) << automaticFailures;
    io::BinaryWriter::WriteSolutionFile(solution, *automatic);
    const auto automaticReport = io::VerifySolutionFiles(puzzle, solution, 1000000, 10000);
    ASSERT_TRUE(automaticReport.Passed()) << automaticReport.detail;
    EXPECT_EQ(automaticReport.metrics.cycles, automatic->metrics.cycles);
}
TEST_F(DisassemblyVerifierTest, NarrowInheritedColoredBonds) {
    for (const int bits : {2, 4, 6, 8, 10, 12, 14})
        for (int rotation = 0; rotation < 6; ++rotation) {
            SCOPED_TRACE(bits);
            SCOPED_TRACE(rotation);
            auto p = WideFeed();
            p.name = "inherited-colored-transport";
            p.inputs = {{{{core::AtomType::Lead, {0, 0}}, {core::AtomType::Quicksilver, {1, 0}}},
                         {{static_cast<std::uint8_t>(bits), {0, 0}, {1, 0}}}}};
            p.outputs = p.inputs;
            auto transform = [&](core::HexCoord c) {
                for (int n = 0; n < rotation; ++n)
                    c = core::RotateClockwise(c);
                return core::Add(c, {3, -4});
            };
            for (auto& a : p.outputs[0].atoms)
                a.position = transform(a.position);
            for (auto& b : p.outputs[0].bonds) {
                b.from = transform(b.from);
                b.to = transform(b.to);
            }
            p.outputMultiplier = 2;
            Verify(p);
        }
}
TEST_F(DisassemblyVerifierTest, HeldArmRotationInstructionSemantics) {
    for (int orientation = 0; orientation < 6; ++orientation) {
        SCOPED_TRACE(orientation);
        const auto rotate = [orientation](core::HexCoord cell) {
            for (int n = 0; n < orientation; ++n)
                cell = core::RotateClockwise(cell);
            return cell;
        };
        for (const char instruction : {'R', 'r', 'P', 'p'}) {
            SCOPED_TRACE(instruction);
            io::PuzzleData p;
            p.version = 3;
            p.name = "held-arm-rotation";
            p.outputMultiplier = 1;
            p.permissionBits = ~0ULL;
            p.inputs = {{{{core::AtomType::Salt, {0, 0}}, {core::AtomType::Fire, {0, -1}}},
                         {{1, {0, 0}, {0, -1}}}}};
            p.outputs = p.inputs;
            solvers::HeldArmMotionProblem motion;
            for (int q = 0; q <= 6; ++q)
                motion.track.push_back(rotate({q, 1}));
            motion.fragment = {{0, 0}, {0, -1}};
            motion.start = {6, 1, (4 + orientation) % 6, orientation};
            auto end = motion.start;
            const int delta = instruction == 'R' || instruction == 'P' ? -1 : 1;
            end.fragmentRotation = (end.fragmentRotation + delta + 6) % 6;
            if (instruction == 'R' || instruction == 'r')
                end.direction = (end.direction + delta + 6) % 6;
            motion.goal = solvers::HeldFragmentPose(motion, end);
            motion.executable = [instruction](const auto&, const auto&, char candidate) {
                return candidate == instruction;
            };
            const auto route = solvers::RouteHeldArm(motion);
            ASSERT_EQ(route.status, solvers::MotionStatus::Found);
            ASSERT_EQ(route.instructions.size(), 1U);
            WritePuzzle(puzzle, p);
            io::SolutionData s;
            s.puzzleName = p.name;
            s.solutionName = "held-arm-oracle";
            io::SolutionObject arm;
            arm.typeName = "arm1";
            arm.position = rotate({0, 1});
            arm.bodyData = {1, (4 + orientation) % 6, 0, 16, 0};
            const char inverse = instruction == 'R'   ? 'r'
                                 : instruction == 'r' ? 'R'
                                 : instruction == 'P' ? 'p'
                                                      : 'P';
            int time = 0;
            arm.instructions.push_back({'G', time++});
            for (int n = 0; n < 6; ++n)
                arm.instructions.push_back({'A', time++});
            arm.instructions.push_back({route.instructions.front(), time++});
            arm.instructions.push_back({'g', time++});
            arm.instructions.push_back({inverse, time++});
            for (int n = 0; n < 6; ++n)
                arm.instructions.push_back({'a', time++});
            s.objects.push_back(arm);
            io::SolutionObject track;
            track.typeName = "track";
            track.position = rotate({0, 1});
            track.bodyData = {1, 0, 0, 0, 7};
            for (int n = 0; n <= 6; ++n) {
                const auto cell = rotate({n, 0});
                track.bodyData.push_back(cell.q);
                track.bodyData.push_back(cell.r);
            }
            track.bodyData.push_back(0);
            s.objects.push_back(track);
            s.objects.push_back({"input", {}, {}, {1, orientation, 0, 0, 0}});
            s.objects.push_back(
                {"out-std", motion.goal.anchor, {}, {1, motion.goal.rotation, 0, 0, 0}});
            io::BinaryWriter::WriteSolutionFile(solution, s);
            auto report = io::VerifySolutionFiles(puzzle, solution, 5000, 2000);
            ASSERT_TRUE(report.Passed()) << report.detail << " cycle=" << report.errorCycle;
            s.metrics = report.metrics;
            io::BinaryWriter::WriteSolutionFile(solution, s);
            EXPECT_TRUE(io::VerifySolutionFiles(puzzle, solution, 5000, 2000).Passed());
        }
    }
}
TEST_F(DisassemblyVerifierTest, WideConnectedFeed) {
    Verify(WideFeed());
}
TEST_F(DisassemblyVerifierTest, ActualEvilOreFeedIntactTransportOnly) {
    const auto root = opusoptima::test::RepoRoot(__FILE__);
    auto p = io::BinaryParser::ParsePuzzleFile(root / "asset/puzzle/tourney-2019/week8.puzzle");
    p.name = "evil-ore-intact-transport-only";
    p.outputs = p.inputs;
    Verify(p);
}
TEST_F(DisassemblyVerifierTest, RotatedTranslatedAsymmetricFeed) {
    auto p = WideFeed();
    p.inputs[0].atoms[0].type = core::AtomType::Fire;
    p.outputs[0] = p.inputs[0];
    for (auto& a : p.outputs[0].atoms)
        a.position = core::Add(core::RotateClockwise(a.position), {7, -9});
    for (auto& b : p.outputs[0].bonds) {
        b.from = core::Add(core::RotateClockwise(b.from), {7, -9});
        b.to = core::Add(core::RotateClockwise(b.to), {7, -9});
    }
    p.outputMultiplier = 2;
    Verify(p);
}
#endif
TEST(DisassemblyTest, ExtractionLayoutMinimizesReachAcrossRotatedOffsetFeeds) {
    for (int rotation = 0; rotation < 6; ++rotation) {
        SCOPED_TRACE(rotation);
        io::Molecule feed;
        for (int index = 0; index < 3; ++index) {
            core::HexCoord cell{index, 0};
            for (int turn = 0; turn < rotation; ++turn)
                cell = core::RotateClockwise(cell);
            feed.atoms.push_back({core::AtomType::Salt, core::Add(cell, {2, -3})});
        }
        const auto layout = solvers::PlanExtractionLayout(feed);
        ASSERT_EQ(layout.cells.size(), feed.atoms.size());
        for (std::size_t index = 0; index < layout.cells.size(); ++index) {
            EXPECT_EQ(layout.cells[index].r, layout.cells.front().r);
            auto expected = feed.atoms[index].position;
            for (int turn = 0; turn < layout.rotation; ++turn)
                expected = core::RotateClockwise(expected);
            EXPECT_EQ(layout.cells[index], expected);
        }
    }
}
TEST(DisassemblyTest, RetainsPhysicalThreeRowGuard) {
    EXPECT_THROW(solvers::PlanFeedLayout(WideFeed().inputs[0]), std::invalid_argument);
    EXPECT_NO_THROW(solvers::PlanExtractionLayout(WideFeed().inputs[0]));
    io::Molecule narrow{{{core::AtomType::Salt, {0, 0}}, {core::AtomType::Salt, {0, 8}}}, {}};
    EXPECT_NO_THROW(solvers::PlanFeedLayout(narrow));
}
TEST(DisassemblyTest, RejectsChemicalBondAndDisconnectedChanges) {
    auto p = WideFeed();
    p.outputs[0].atoms[0].type = core::AtomType::Fire;
    EXPECT_THROW(solvers::CompileIntactFeed(p), std::invalid_argument);
    p = WideFeed();
    p.outputs[0].bonds.pop_back();
    EXPECT_THROW(solvers::CompileIntactFeed(p), std::invalid_argument);
    p = WideFeed();
    p.inputs[0].bonds.clear();
    p.outputs[0].bonds.clear();
    EXPECT_THROW(solvers::CompileIntactFeed(p), std::invalid_argument);
    p = WideFeed();
    p.permissionBits &= ~(1ULL << 23);
    EXPECT_THROW(solvers::CompileIntactFeed(p), std::invalid_argument);
}
TEST(DisassemblyTest, RejectsInvalidOrMutatedInheritedBondMasks) {
    for (const int bits : {0, 3, 5, 15, 16}) {
        SCOPED_TRACE(bits);
        auto p = WideFeed();
        p.inputs[0].bonds[0].bits = bits;
        p.outputs = p.inputs;
        EXPECT_THROW(solvers::CompileIntactFeed(p), std::invalid_argument);
    }
    auto p = WideFeed();
    p.inputs[0].bonds[0].bits = 2;
    p.outputs[0].bonds[0].bits = 4;
    EXPECT_THROW(solvers::CompileIntactFeed(p), std::invalid_argument);
}
TEST(DisassemblyTest, OriginalWideChemistryFixturesRemainOutsideIntactScope) {
    const auto root = opusoptima::test::RepoRoot(__FILE__);
    for (const auto& path :
         {"asset/puzzle/journal/journal8/P103.puzzle", "asset/puzzle/tourney-2019/week8.puzzle"}) {
        const auto p = io::BinaryParser::ParsePuzzleFile(root / path);
        ASSERT_FALSE(p.inputs.empty());
        EXPECT_THROW(solvers::PlanFeedLayout(p.inputs[0]), std::invalid_argument);
        EXPECT_THROW(solvers::CompileIntactFeed(p), std::invalid_argument);
    }
}
TEST(DisassemblyTest, SpecialRulesFailBeforeChemistry) {
    auto p = WideFeed();
    p.productionInfo.emplace();
    for (bool conduit : {false, true}) {
        if (conduit) p.productionInfo->conduits.emplace_back();
        for (bool plan : {false, true}) {
            try {
                if (plan)
                    (void)solvers::AssemblyPlanner::Plan(p);
                else
                    (void)solvers::AssemblySolver{}.Solve(p);
                FAIL() << "production must not silently use a nonproduction compiler";
            } catch (const std::invalid_argument& e) {
                if (plan) {
                    EXPECT_NE(std::string(e.what()).find(conduit ? "production conduits"
                                                                 : "production chambers"),
                              std::string::npos);
                    EXPECT_NE(std::string(e.what()).find("not a chemistry limitation"),
                              std::string::npos);
                } else {
                    EXPECT_NE(std::string(e.what()).find("production"), std::string::npos);
                }
            }
        }
    }
}
TEST(AutomaticSupplyTest, ImpossibleOutputGeometryDoesNotReplanMaterial) {
    io::PuzzleData p;
    p.version = 3;
    p.name = "geometry-before-material";
    p.outputMultiplier = 1;
    p.permissionBits =
        (1ULL << 0) | (1ULL << 2) | (1ULL << 3) | (1ULL << 8) | (1ULL << 22) | (1ULL << 23);
    p.inputs = {{{{core::AtomType::Salt, {0, 0}}}, {}}};
    io::Molecule output;
    for (int q = 0; q < 4; ++q)
        for (int r = 0; r < 4; ++r) {
            output.atoms.push_back({core::AtomType::Salt, {q, r}});
            if (q) output.bonds.push_back({1, {q - 1, r}, {q, r}});
            if (r) output.bonds.push_back({1, {q, r - 1}, {q, r}});
        }
    p.outputs = {output};
    std::vector<solvers::SupplyFailure> failures;
    int accepted = 0;
    const auto candidate = solvers::CompileAcceptedSupplyInventory(
        p,
        [&](const auto&) {
            ++accepted;
            return true;
        },
        [&](const auto& failure) { failures.push_back(failure); });
    EXPECT_FALSE(candidate);
    EXPECT_EQ(accepted, 0);
    ASSERT_EQ(failures.size(), 7U);
    for (const auto& failure : failures) {
        EXPECT_EQ(failure.category, solvers::SupplyFailureCategory::Layout);
        EXPECT_EQ(failure.materialAttempt, 0U);
    }
    EXPECT_EQ(failures.back().rotation, -1);
}

TEST(AutomaticSupplyTest, IndependentRejectionReplansMaterialPeriod) {
    io::PuzzleData p;
    p.version = 3;
    p.name = "automatic-replanning";
    p.outputMultiplier = 1;
    p.permissionBits =
        (1ULL << 0) | (1ULL << 2) | (1ULL << 3) | (1ULL << 8) | (1ULL << 22) | (1ULL << 23);
    p.inputs = {{{{core::AtomType::Salt, {2, -3}}}, {}}};
    p.outputs = {{{{core::AtomType::Salt, {-4, 2}}}, {}}};
    std::size_t attempts = 0;
    const auto candidate = solvers::CompileAcceptedSupplyInventory(p, [&](const auto& solution) {
        (void)solution;
        return ++attempts == 19;
    });
    ASSERT_TRUE(candidate);
    EXPECT_GT(attempts, 18U);
}

#ifdef HAS_LIBVERIFY
TEST_F(DisassemblyVerifierTest, IndependentRejectionReplansToFourProductPeriod) {
    io::PuzzleData p;
    p.version = 3;
    p.name = "independent-four-period-replanning";
    p.outputMultiplier = 2;
    p.permissionBits =
        (1ULL << 0) | (1ULL << 2) | (1ULL << 3) | (1ULL << 8) | (1ULL << 22) | (1ULL << 23);
    p.inputs = {{{{core::AtomType::Salt, {0, 0}}}, {}}};
    p.outputs = p.inputs;
    WritePuzzle(puzzle, p);
    std::size_t rejectedPeriod = 0;
    std::set<std::size_t> rejectedPeriods;
    auto candidate = solvers::CompileAcceptedSupplyInventory(
        p,
        [&](const auto& s) {
            if (rejectedPeriod != 4) return false;
            io::BinaryWriter::WriteSolutionFile(solution, s);
            return io::VerifySolutionFiles(puzzle, solution, 10000, 2000).Passed();
        },
        [&](const auto& failure) {
            if (failure.category == solvers::SupplyFailureCategory::IndependentVerifier) {
                rejectedPeriod = failure.productsPerPeriod;
                rejectedPeriods.insert(rejectedPeriod);
            }
        });
    ASSERT_TRUE(candidate);
    EXPECT_TRUE(rejectedPeriods.contains(1));
    EXPECT_TRUE(rejectedPeriods.contains(4));
    io::BinaryWriter::WriteSolutionFile(solution, *candidate);
    const auto report = io::VerifySolutionFiles(puzzle, solution, 10000, 2000);
    ASSERT_TRUE(report.Passed());
    candidate->metrics = report.metrics;
    io::BinaryWriter::WriteSolutionFile(solution, *candidate);
    EXPECT_TRUE(io::VerifySolutionFiles(puzzle, solution, 10000, 2000).Passed());
}

#endif

TEST(AutomaticSupplyTest, ReportsMissingPistonForChemistry) {
    io::PuzzleData p;
    p.version = 3;
    p.name = "chemistry-without-piston";
    p.outputMultiplier = 1;
    p.permissionBits = (1ULL << 0) | (1ULL << 3) | (1ULL << 15) | (1ULL << 22) | (1ULL << 23);
    p.inputs = {{{{core::AtomType::Lead, {0, 0}}}, {}}};
    p.outputs = {{{{core::AtomType::Tin, {0, 0}}}, {}}};
    std::size_t accepted = 0;
    std::vector<solvers::SupplyFailure> failures;
    EXPECT_FALSE(solvers::CompileAcceptedSupplyInventory(
        p,
        [&](const auto&) {
            ++accepted;
            return true;
        },
        [&](const auto& failure) { failures.push_back(failure); }));
    EXPECT_EQ(accepted, 0U);
    EXPECT_TRUE(std::any_of(failures.begin(), failures.end(), [](const auto& failure) {
        return failure.category == solvers::SupplyFailureCategory::Material &&
               failure.missingPermission == 2;
    }));
    std::set<std::pair<std::size_t, int>> rejectedRotations;
    for (const auto& failure : failures)
        if (failure.missingPermission == 2) {
            EXPECT_GE(failure.rotation, 0);
            EXPECT_LT(failure.rotation, 6);
            EXPECT_EQ(failure.spacing, 1);
            EXPECT_TRUE(
                rejectedRotations.insert({failure.materialAttempt, failure.rotation}).second);
        }
}

#ifdef HAS_LIBVERIFY
TEST_F(DisassemblyVerifierTest, MissingPistonStillTriesFlatOutputOrientations) {
    io::PuzzleData p;
    p.version = 3;
    p.name = "flat-output-without-piston";
    p.outputMultiplier = 1;
    p.permissionBits = (1ULL << 0) | (1ULL << 3) | (1ULL << 8) | (1ULL << 22) | (1ULL << 23);
    p.inputs = {{{{core::AtomType::Salt, {0, 0}}}, {}}};
    p.outputs = {
        {{{core::AtomType::Salt, {0, 0}}, {core::AtomType::Salt, {0, 1}}}, {{1, {0, 0}, {0, 1}}}}};
    WritePuzzle(puzzle, p);
    std::vector<solvers::SupplyFailure> failures;
    auto candidate = solvers::CompileAcceptedSupplyInventory(
        p,
        [&](const auto& s) {
            io::BinaryWriter::WriteSolutionFile(solution, s);
            return io::VerifySolutionFiles(puzzle, solution, 5000, 2000).Passed();
        },
        [&](const auto& failure) { failures.push_back(failure); });
    ASSERT_TRUE(candidate);
    EXPECT_TRUE(std::any_of(failures.begin(), failures.end(), [](const auto& failure) {
        return failure.missingPermission == 2 && failure.rotation == 0;
    }));
    for (int pass = 0; pass < 2; ++pass) {
        io::BinaryWriter::WriteSolutionFile(solution, *candidate);
        const auto report = io::VerifySolutionFiles(puzzle, solution, 5000, 2000);
        ASSERT_TRUE(report.Passed()) << report.detail;
        candidate->metrics = report.metrics;
    }
}

TEST_F(DisassemblyVerifierTest, ExtractionPermissionExclusionReplansToSingletonFeed) {
    io::PuzzleData p;
    p.version = 3;
    p.name = "extraction-permission-replanning";
    p.outputMultiplier = 1;
    p.permissionBits = (1ULL << 0) | (1ULL << 3) | (1ULL << 9) | (1ULL << 22) | (1ULL << 23);
    p.inputs = {
        {{{core::AtomType::Salt, {0, 0}}, {core::AtomType::Salt, {1, 0}}}, {{1, {0, 0}, {1, 0}}}},
        {{{core::AtomType::Salt, {0, 0}}}, {}}};
    p.outputs = {p.inputs[1]};
    WritePuzzle(puzzle, p);
    std::vector<solvers::SupplyFailure> failures;
    auto candidate = solvers::CompileAcceptedSupplyInventory(
        p,
        [&](const auto& s) {
            io::BinaryWriter::WriteSolutionFile(solution, s);
            return io::VerifySolutionFiles(puzzle, solution, 5000, 2000).Passed();
        },
        [&](const auto& failure) { failures.push_back(failure); });
    ASSERT_TRUE(candidate);
    EXPECT_TRUE(std::any_of(failures.begin(), failures.end(), [](const auto& failure) {
        return failure.missingPermission == 2 && failure.rotation == -1 &&
               failure.category == solvers::SupplyFailureCategory::Material;
    }));
    for (int pass = 0; pass < 2; ++pass) {
        io::BinaryWriter::WriteSolutionFile(solution, *candidate);
        const auto report = io::VerifySolutionFiles(puzzle, solution, 5000, 2000);
        ASSERT_TRUE(report.Passed()) << report.detail;
        candidate->metrics = report.metrics;
    }
}

TEST_F(DisassemblyVerifierTest, MissingCutPermissionReplansToAlternativeMaterial) {
    io::PuzzleData p;
    p.version = 3;
    p.name = "cut-permission-material-replanning";
    p.outputMultiplier = 1;
    p.permissionBits =
        (1ULL << 0) | (1ULL << 2) | (1ULL << 3) | (1ULL << 17) | (1ULL << 22) | (1ULL << 23);
    p.inputs = {
        {{{core::AtomType::Salt, {0, 0}}, {core::AtomType::Air, {1, 0}}}, {{1, {0, 0}, {1, 0}}}},
        {{{core::AtomType::Salt, {0, 0}}}, {}}};
    p.outputs = {p.inputs[1]};
    WritePuzzle(puzzle, p);
    std::vector<solvers::SupplyFailure> failures;
    auto candidate = solvers::CompileAcceptedSupplyInventory(
        p,
        [&](const auto& s) {
            io::BinaryWriter::WriteSolutionFile(solution, s);
            return io::VerifySolutionFiles(puzzle, solution, 5000, 2000).Passed();
        },
        [&](const auto& failure) { failures.push_back(failure); });
    ASSERT_TRUE(candidate);
    EXPECT_TRUE(std::any_of(failures.begin(), failures.end(), [](const auto& failure) {
        return failure.missingPermission == 9 && failure.materialAttempt > 0 &&
               failure.category == solvers::SupplyFailureCategory::Material;
    }));
    EXPECT_TRUE(
        std::any_of(candidate->objects.begin(), candidate->objects.end(), [](const auto& object) {
            return object.typeName == "input" && object.bodyData.at(2) == 1;
        }));
    for (int pass = 0; pass < 2; ++pass) {
        io::BinaryWriter::WriteSolutionFile(solution, *candidate);
        const auto report = io::VerifySolutionFiles(puzzle, solution, 5000, 2000);
        ASSERT_TRUE(report.Passed()) << report.detail;
        candidate->metrics = report.metrics;
    }
}

#endif

TEST(AutomaticSupplyTest, RejectsRequiredCutsWithoutUnbonderBeforeAcceptance) {
    io::PuzzleData p;
    p.version = 3;
    p.name = "inventory-required-cut";
    p.outputMultiplier = 1;
    p.permissionBits =
        (1ULL << 0) | (1ULL << 2) | (1ULL << 3) | (1ULL << 17) | (1ULL << 22) | (1ULL << 23);
    p.inputs = {
        {{{core::AtomType::Air, {0, 0}}, {core::AtomType::Salt, {1, 0}}}, {{14, {0, 0}, {1, 0}}}}};
    p.outputs = {{{{core::AtomType::Air, {0, 0}}}, {}}};
    std::size_t accepted = 0;
    std::vector<solvers::SupplyFailure> failures;
    EXPECT_FALSE(solvers::CompileAcceptedSupplyInventory(
        p,
        [&](const auto&) {
            ++accepted;
            return true;
        },
        [&](const auto& failure) { failures.push_back(failure); }));
    EXPECT_EQ(accepted, 0U);
    std::string details;
    for (const auto& failure : failures)
        details += failure.detail + "\n";
    EXPECT_TRUE(std::any_of(failures.begin(), failures.end(), [](const auto& failure) {
        return failure.missingPermission == 9 &&
               failure.category == solvers::SupplyFailureCategory::Material &&
               failure.detail.find("unbonder permission for cuts") != std::string::npos;
    })) << details;
    for (const auto& failure : failures)
        if (failure.missingPermission) {
            EXPECT_EQ(failure.rotation, 0);
            EXPECT_EQ(failure.spacing, 1);
        }
}

TEST(AutomaticSupplyTest, ReportsExtractionExclusionAndActualJointFailure) {
    io::PuzzleData p;
    p.version = 3;
    p.name = "inventory-extraction-feedback";
    p.outputMultiplier = 1;
    p.permissionBits = (1ULL << 0) | (1ULL << 2) | (1ULL << 3) | (1ULL << 8) | (1ULL << 9) |
                       (1ULL << 22) | (1ULL << 23);
    p.inputs = {{{{core::AtomType::Fire, {0, 0}},
                  {core::AtomType::Fire, {1, 0}},
                  {core::AtomType::Fire, {0, 1}},
                  {core::AtomType::Fire, {-1, 1}},
                  {core::AtomType::Fire, {-1, 0}},
                  {core::AtomType::Fire, {0, -1}},
                  {core::AtomType::Fire, {1, -1}}},
                 {{1, {0, 0}, {1, 0}},
                  {1, {0, 0}, {0, 1}},
                  {1, {0, 0}, {-1, 1}},
                  {1, {0, 0}, {-1, 0}},
                  {1, {0, 0}, {0, -1}},
                  {1, {0, 0}, {1, -1}}}}};
    p.outputs = {{{{core::AtomType::Fire, {0, 0}}}, {}}};
    p.inputs[0].atoms.push_back({core::AtomType::Fire, {0, 2}});
    p.inputs[0].atoms.push_back({core::AtomType::Fire, {2, -1}});
    p.inputs[0].atoms.push_back({core::AtomType::Fire, {-2, 1}});
    p.inputs[0].bonds.push_back({1, {0, 1}, {0, 2}});
    p.inputs[0].bonds.push_back({1, {1, -1}, {2, -1}});
    p.inputs[0].bonds.push_back({1, {-1, 1}, {-2, 1}});
    std::vector<solvers::SupplyFailure> failures;
    EXPECT_THROW(solvers::CompileAcceptedSupplyInventory(
                     p, [](const auto&) { return true; },
                     [&](const auto& failure) { failures.push_back(failure); }),
                 std::invalid_argument);
    EXPECT_TRUE(std::any_of(failures.begin(), failures.end(), [](const auto& failure) {
        return failure.category == solvers::SupplyFailureCategory::Layout &&
               failure.detail.find("input 0 excluded") != std::string::npos &&
               failure.detail.find("reach 4") != std::string::npos;
    }));
    ASSERT_FALSE(failures.empty());
    EXPECT_EQ(failures.back().category, solvers::SupplyFailureCategory::Material);
    EXPECT_EQ(failures.back().detail,
              "joint inventory found no closed production period within bounds");
}

TEST(AutomaticSupplyTest, ReportsMaterialSearchBudgetSeparatelyFromRejection) {
    io::PuzzleData p;
    p.version = 3;
    p.name = "inventory-budget-feedback";
    p.outputMultiplier = 1;
    p.permissionBits = (1ULL << 0) | (1ULL << 2) | (1ULL << 3) | (1ULL << 22) | (1ULL << 23);
    p.inputs.resize(8);
    for (std::size_t input = 0; input < p.inputs.size(); ++input)
        for (std::size_t atom = 0; atom <= input; ++atom)
            p.inputs[input].atoms.push_back({core::AtomType::Salt, {static_cast<int>(atom), 0}});
    p.outputs.resize(2);
    for (int atom = 0; atom < 15; ++atom)
        p.outputs[0].atoms.push_back({core::AtomType::Salt, {atom, 0}});
    p.outputs[0].atoms.push_back({core::AtomType::Fire, {-1, 0}});
    for (int atom = -1; atom < 14; ++atom)
        p.outputs[0].bonds.push_back({1, {atom, 0}, {atom + 1, 0}});
    p.permissionBits |= 1ULL << 8;
    p.outputs[1].atoms.push_back({core::AtomType::Salt, {0, 0}});
    std::vector<solvers::SupplyFailure> failures;
    EXPECT_THROW(solvers::CompileAcceptedSupplyInventory(
                     p, [](const auto&) { return true; },
                     [&](const auto& failure) { failures.push_back(failure); }),
                 std::invalid_argument);
    ASSERT_FALSE(failures.empty());
    EXPECT_EQ(failures.back().category, solvers::SupplyFailureCategory::Budget)
        << failures.back().detail;
}

TEST(AutomaticSupplyTest, ReportsUnavailableVerificationSeparatelyFromIndependentRejection) {
    io::PuzzleData p;
    p.version = 3;
    p.name = "unavailable-verifier-feedback";
    p.outputMultiplier = 1;
    p.permissionBits = (1ULL << 0) | (1ULL << 2) | (1ULL << 3) | (1ULL << 22) | (1ULL << 23);
    p.inputs = {{{{core::AtomType::Salt, {0, 0}}}, {}}};
    p.outputs = p.inputs;
    std::vector<solvers::SupplyFailure> failures;
    const auto candidate = solvers::CompileVerifiedSupplyInventory(
        p,
        [](const auto&) {
            return io::VerificationReport{
                io::VerificationStatus::Unavailable, {}, "oracle lacks this rule"};
        },
        [&](const auto& failure) { failures.push_back(failure); });
    EXPECT_FALSE(candidate);
    EXPECT_TRUE(std::any_of(failures.begin(), failures.end(), [](const auto& failure) {
        return failure.category == solvers::SupplyFailureCategory::VerificationUnavailable &&
               failure.verification &&
               failure.verification->status == io::VerificationStatus::Unavailable;
    }));
    EXPECT_FALSE(std::any_of(failures.begin(), failures.end(), [](const auto& failure) {
        return failure.category == solvers::SupplyFailureCategory::IndependentVerifier;
    }));
}

TEST(AutomaticSupplyTest, EnumeratesRejectedLayoutsAndPreservesExceptions) {
    io::PuzzleData p;
    p.version = 3;
    p.name = "automatic-singleton";
    p.outputMultiplier = 1;
    p.permissionBits =
        (1ULL << 0) | (1ULL << 2) | (1ULL << 3) | (1ULL << 8) | (1ULL << 22) | (1ULL << 23);
    p.inputs = {{{{core::AtomType::Salt, {2, -3}}}, {}}};
    p.outputs = {{{{core::AtomType::Salt, {-4, 2}}}, {}}};
    int attempts = 0;
    const auto candidate =
        solvers::CompileAcceptedSupplyInventory(p, [&](const auto&) { return ++attempts == 2; });
    ASSERT_TRUE(candidate);
    EXPECT_EQ(attempts, 2);
    attempts = 0;
    std::vector<solvers::SupplyFailure> failures;
    EXPECT_FALSE(solvers::CompileAcceptedSupplyInventory(
        p,
        [&](const auto&) {
            ++attempts;
            return false;
        },
        [&](const auto& failure) { failures.push_back(failure); }));
    EXPECT_GE(attempts, 18);
    EXPECT_GE(failures.size(), static_cast<std::size_t>(attempts));
    EXPECT_EQ(std::count_if(failures.begin(), failures.end(),
                            [](const auto& failure) {
                                return failure.category ==
                                       solvers::SupplyFailureCategory::IndependentVerifier;
                            }),
              attempts);
    for (const auto& failure : failures) {
        EXPECT_FALSE(failure.detail.empty());
    }
    EXPECT_TRUE(failures.front().spacing != failures[1].spacing ||
                failures.front().rotation != failures[1].rotation);
    for (int spacing : {1, 3, 5})
        EXPECT_TRUE(std::any_of(failures.begin(), failures.end(), [&](const auto& failure) {
            return failure.category == solvers::SupplyFailureCategory::IndependentVerifier &&
                   failure.spacing == spacing;
        }));
    for (int rotation = 0; rotation < 6; ++rotation)
        EXPECT_TRUE(std::any_of(failures.begin(), failures.end(), [&](const auto& failure) {
            return failure.category == solvers::SupplyFailureCategory::IndependentVerifier &&
                   failure.rotation == rotation;
        }));
    EXPECT_THROW(solvers::CompileAcceptedSupplyInventory(
                     p, [](const auto&) -> bool { throw std::invalid_argument("acceptance"); }),
                 std::invalid_argument);
    attempts = 0;
    const auto integrated = solvers::AssemblySolver{}.SolveAccepted(p, [&](const auto& s) {
        return s.solutionName == "Automatic supply inventory" && ++attempts == 2;
    });
    EXPECT_EQ(integrated.solutionName, "Automatic supply inventory");
    EXPECT_EQ(attempts, 2);
    EXPECT_THROW((void)solvers::AssemblySolver{}.SolveAccepted(
                     p,
                     [](const auto& s) -> bool {
                         if (s.solutionName == "Automatic supply inventory")
                             throw std::invalid_argument("acceptance");
                         return false;
                     }),
                 std::invalid_argument);
    for (const int bit : {0, 3, 22, 23}) {
        auto missing = p;
        missing.permissionBits &= ~(1ULL << bit);
        failures.clear();
        EXPECT_THROW(solvers::CompileAcceptedSupplyInventory(
                         missing, [](const auto&) { return true; },
                         [&](const auto& failure) { failures.push_back(failure); }),
                     std::invalid_argument);
        ASSERT_EQ(failures.size(), 1);
        EXPECT_EQ(failures[0].category, solvers::SupplyFailureCategory::Material);
        EXPECT_EQ(failures[0].missingPermission, bit);
        EXPECT_EQ(failures[0].rotation, -1);
    }
    auto noMotion = p;
    noMotion.permissionBits &= ~((1ULL << 0) | (1ULL << 3));
    failures.clear();
    EXPECT_THROW(solvers::CompileAcceptedSupplyInventory(
                     noMotion, [](const auto&) { return true; },
                     [&](const auto& failure) { failures.push_back(failure); }),
                 std::invalid_argument);
    ASSERT_EQ(failures.size(), 2);
    EXPECT_EQ(failures[0].missingPermission, 0);
    EXPECT_EQ(failures[1].missingPermission, 3);
    auto molecular = p;
    molecular.inputs[0].atoms.push_back({core::AtomType::Salt, {3, -3}});
    EXPECT_FALSE(solvers::CompileAcceptedSupplyInventory(
        molecular, [](const auto&) { return true; },
        [&](const auto& failure) { failures.push_back(failure); }));
    EXPECT_TRUE(std::any_of(failures.begin(), failures.end(), [](const auto& failure) {
        return failure.category == solvers::SupplyFailureCategory::Layout;
    }));
    auto multiOutput = p;
    multiOutput.outputs.push_back(p.outputs[0]);
    EXPECT_TRUE(
        solvers::CompileAcceptedSupplyInventory(multiOutput, [](const auto&) { return true; }));
}
#ifdef HAS_LIBVERIFY
TEST_F(DisassemblyVerifierTest, StructuredIndependentFeedbackPreservesReportsAndMetrics) {
    io::PuzzleData p;
    p.version = 3;
    p.name = "structured-verifier-feedback";
    p.outputMultiplier = 1;
    p.permissionBits =
        (1ULL << 0) | (1ULL << 3) | (1ULL << 8) | (1ULL << 12) | (1ULL << 22) | (1ULL << 23);
    p.inputs = {{{{core::AtomType::Air, {2, -3}}}, {}}};
    p.outputs = {{{{core::AtomType::Salt, {-4, 2}}}, {}}};
    WritePuzzle(puzzle, p);
    std::vector<io::VerificationReport> reports;
    std::vector<solvers::SupplyFailure> failures;
    auto candidate = solvers::CompileVerifiedSupplyInventory(
        p,
        [&](const auto& s) {
            io::BinaryWriter::WriteSolutionFile(solution, s);
            reports.push_back(io::VerifySolutionFiles(puzzle, solution, 5000, 2000));
            return reports.back();
        },
        [&](const auto& failure) { failures.push_back(failure); });
    ASSERT_TRUE(candidate);
    ASSERT_FALSE(reports.empty());
    ASSERT_TRUE(reports.back().Passed());
    EXPECT_EQ(candidate->metrics.cycles, reports.back().metrics.cycles);
    io::BinaryWriter::WriteSolutionFile(solution, *candidate);
    EXPECT_TRUE(io::VerifySolutionFiles(puzzle, solution, 5000, 2000).Passed());

    auto assembled = solvers::AssemblySolver{}.SolveVerified(p, [&](const auto& s) {
        io::BinaryWriter::WriteSolutionFile(solution, s);
        return io::VerifySolutionFiles(puzzle, solution, 5000, 2000);
    });
    EXPECT_GT(assembled.metrics.cycles, 0);
    io::BinaryWriter::WriteSolutionFile(solution, assembled);
    EXPECT_TRUE(io::VerifySolutionFiles(puzzle, solution, 5000, 2000).Passed());
    EXPECT_THROW(
        (void)solvers::AssemblySolver{}.SolveVerified(
            p,
            [](const auto&) -> io::VerificationReport { throw std::invalid_argument("oracle"); }),
        std::invalid_argument);

    failures.clear();
    reports.clear();
    // A fixed oracle budget is applied to every unmodified candidate.
    EXPECT_FALSE(solvers::CompileVerifiedSupplyInventory(
        p,
        [&](const auto& s) {
            io::BinaryWriter::WriteSolutionFile(solution, s);
            reports.push_back(io::VerifySolutionFiles(puzzle, solution, 1, 2000));
            return reports.back();
        },
        [&](const auto& failure) { failures.push_back(failure); }));
    std::size_t rejected = 0;
    for (const auto& failure : failures) {
        if (failure.category != solvers::SupplyFailureCategory::IndependentVerifier) continue;
        ASSERT_LT(rejected, reports.size());
        ASSERT_TRUE(failure.verification);
        EXPECT_EQ(failure.verification->status, reports[rejected].status);
        EXPECT_EQ(failure.verification->detail, reports[rejected].detail);
        EXPECT_EQ(failure.verification->errorCycle, reports[rejected].errorCycle);
        EXPECT_EQ(failure.verification->errorU, reports[rejected].errorU);
        EXPECT_EQ(failure.verification->errorV, reports[rejected].errorV);
        EXPECT_GT(failure.materialAttempt, 0U);
        EXPECT_GT(failure.productsPerPeriod, 0U);
        ++rejected;
    }
    EXPECT_EQ(rejected, reports.size());
    EXPECT_GT(rejected, 0U);
    EXPECT_THROW(solvers::CompileVerifiedSupplyInventory(p,
                                                         [](const auto&) -> io::VerificationReport {
                                                             throw std::invalid_argument("oracle");
                                                         }),
                 std::invalid_argument);
}

TEST_F(DisassemblyVerifierTest, AutomaticCalcificationStation) {
    io::PuzzleData p;
    p.version = 3;
    p.name = "automatic-calcification";
    p.outputMultiplier = 1;
    p.permissionBits =
        (1ULL << 0) | (1ULL << 3) | (1ULL << 8) | (1ULL << 12) | (1ULL << 22) | (1ULL << 23);
    p.inputs = {{{{core::AtomType::Air, {2, -3}}}, {}}};
    p.outputs = {{{{core::AtomType::Salt, {-4, 2}}}, {}}};
    WritePuzzle(puzzle, p);
    std::string failures;
    auto candidate = solvers::CompileAcceptedSupplyInventory(
        p,
        [&](const auto& s) {
            io::BinaryWriter::WriteSolutionFile(solution, s);
            return io::VerifySolutionFiles(puzzle, solution, 5000, 2000).Passed();
        },
        [&](const auto& failure) { failures += failure.detail + "\n"; });
    ASSERT_TRUE(candidate) << failures;
    EXPECT_TRUE(
        std::any_of(candidate->objects.begin(), candidate->objects.end(),
                    [](const auto& object) { return object.typeName == "glyph-calcification"; }));
    io::BinaryWriter::WriteSolutionFile(solution, *candidate);
    const auto report = io::VerifySolutionFiles(puzzle, solution, 5000, 2000);
    ASSERT_TRUE(report.Passed());
    candidate->metrics = report.metrics;
    io::BinaryWriter::WriteSolutionFile(solution, *candidate);
    EXPECT_TRUE(io::VerifySolutionFiles(puzzle, solution, 5000, 2000).Passed());
}

TEST_F(DisassemblyVerifierTest, AutomaticProjectionStation) {
    io::PuzzleData p;
    p.version = 3;
    p.name = "automatic-projection";
    p.outputMultiplier = 1;
    p.permissionBits = (1ULL << 0) | (1ULL << 2) | (1ULL << 3) | (1ULL << 8) | (1ULL << 14) |
                       (1ULL << 22) | (1ULL << 23);
    p.inputs = {{{{core::AtomType::Lead, {0, 0}}}, {}},
                {{{core::AtomType::Quicksilver, {0, 0}}}, {}}};
    p.outputs = {{{{core::AtomType::Tin, {0, 0}}}, {}}};
    WritePuzzle(puzzle, p);
    std::string failures;
    auto candidate = solvers::CompileAcceptedSupplyInventory(
        p,
        [&](const auto& s) {
            io::BinaryWriter::WriteSolutionFile(solution, s);
            return io::VerifySolutionFiles(puzzle, solution, 5000, 2000).Passed();
        },
        [&](const auto& failure) { failures += failure.detail + "\n"; });
    ASSERT_TRUE(candidate) << failures;
    EXPECT_TRUE(
        std::any_of(candidate->objects.begin(), candidate->objects.end(),
                    [](const auto& object) { return object.typeName == "glyph-projection"; }));
    for (int pass = 0; pass < 2; ++pass) {
        io::BinaryWriter::WriteSolutionFile(solution, *candidate);
        const auto report = io::VerifySolutionFiles(puzzle, solution, 5000, 2000);
        ASSERT_TRUE(report.Passed()) << report.detail;
        candidate->metrics = report.metrics;
    }
}

TEST_F(DisassemblyVerifierTest, AutomaticDuplicationRecoversCatalyst) {
    io::PuzzleData p;
    p.version = 3;
    p.name = "automatic-duplication";
    p.outputMultiplier = 1;
    p.permissionBits = (1ULL << 0) | (1ULL << 2) | (1ULL << 3) | (1ULL << 8) | (1ULL << 9) |
                       (1ULL << 13) | (1ULL << 22) | (1ULL << 23);
    p.inputs = {
        {{{core::AtomType::Salt, {0, 0}}, {core::AtomType::Air, {1, 0}}}, {{1, {0, 0}, {1, 0}}}}};
    p.outputs = {
        {{{core::AtomType::Air, {0, 0}}, {core::AtomType::Air, {1, 0}}}, {{1, {0, 0}, {1, 0}}}}};
    WritePuzzle(puzzle, p);
    std::string failures;
    auto candidate = solvers::CompileAcceptedSupplyInventory(
        p,
        [&](const auto& s) {
            io::BinaryWriter::WriteSolutionFile(solution, s);
            const auto report = io::VerifySolutionFiles(puzzle, solution, 5000, 2000);
            if (!report.Passed()) failures += report.detail + "\n";
            return report.Passed();
        },
        [&](const auto& failure) { failures += failure.detail + "\n"; });
    ASSERT_TRUE(candidate) << failures;
    EXPECT_TRUE(
        std::any_of(candidate->objects.begin(), candidate->objects.end(),
                    [](const auto& object) { return object.typeName == "glyph-duplication"; }));
    for (int pass = 0; pass < 2; ++pass) {
        io::BinaryWriter::WriteSolutionFile(solution, *candidate);
        const auto report = io::VerifySolutionFiles(puzzle, solution, 5000, 2000);
        ASSERT_TRUE(report.Passed()) << report.detail;
        candidate->metrics = report.metrics;
    }
}

TEST_F(DisassemblyVerifierTest, AutomaticAnimismusDeliversBothCoproducts) {
    for (bool reversed : {false, true}) {
        io::PuzzleData p;
        p.version = 3;
        p.name = "automatic-animismus";
        p.outputMultiplier = 1;
        p.permissionBits = (1ULL << 0) | (1ULL << 2) | (1ULL << 3) | (1ULL << 8) | (1ULL << 9) |
                           (1ULL << 16) | (1ULL << 22) | (1ULL << 23);
        p.inputs = {{{{core::AtomType::Salt, {0, 0}}}, {}}};
        for (bool separatePorts : {false, true})
            for (int multiplicity = 1; multiplicity <= 2; ++multiplicity) {
                p.outputs = {{{{reversed ? core::AtomType::Mors : core::AtomType::Vitae, {0, 0}},
                               {reversed ? core::AtomType::Vitae : core::AtomType::Mors, {1, 0}}},
                              {{1, {0, 0}, {1, 0}}}}};
                if (separatePorts)
                    p.outputs = {
                        {{{reversed ? core::AtomType::Mors : core::AtomType::Vitae, {0, 0}}}, {}},
                        {{{reversed ? core::AtomType::Vitae : core::AtomType::Mors, {0, 0}}}, {}}};
                if (multiplicity == 2) {
                    if (separatePorts) {
                        for (auto& output : p.outputs) {
                            output.atoms.push_back({output.atoms.front().type, {1, 0}});
                            output.bonds.push_back({1, {0, 0}, {1, 0}});
                        }
                    } else {
                        p.outputs[0].atoms.push_back({p.outputs[0].atoms[0].type, {2, 0}});
                        p.outputs[0].atoms.push_back({p.outputs[0].atoms[1].type, {3, 0}});
                        p.outputs[0].bonds.push_back({1, {1, 0}, {2, 0}});
                        p.outputs[0].bonds.push_back({1, {2, 0}, {3, 0}});
                    }
                }
                WritePuzzle(puzzle, p);
                std::string failures;
                auto candidate = solvers::CompileAcceptedSupplyInventory(
                    p,
                    [&](const auto& s) {
                        io::BinaryWriter::WriteSolutionFile(solution, s);
                        const auto report = io::VerifySolutionFiles(puzzle, solution, 5000, 2000);
                        if (!report.Passed()) failures += report.detail + "\n";
                        return report.Passed();
                    },
                    [&](const auto& failure) { failures += failure.detail + "\n"; });
                ASSERT_TRUE(candidate) << failures;
                EXPECT_TRUE(std::any_of(
                    candidate->objects.begin(), candidate->objects.end(),
                    [](const auto& object) { return object.typeName == "glyph-life-and-death"; }));
                for (int pass = 0; pass < 2; ++pass) {
                    io::BinaryWriter::WriteSolutionFile(solution, *candidate);
                    const auto report = io::VerifySolutionFiles(puzzle, solution, 5000, 2000);
                    ASSERT_TRUE(report.Passed()) << report.detail;
                    candidate->metrics = report.metrics;
                }
                for (const auto permission : {2, 16}) {
                    auto forbidden = p;
                    forbidden.permissionBits &= ~(1ULL << permission);
                    bool accepted = false;
                    if (permission == 16) {
                        EXPECT_THROW(
                            (void)solvers::CompileAcceptedSupplyInventory(forbidden,
                                                                          [&](const auto&) {
                                                                              accepted = true;
                                                                              return true;
                                                                          }),
                            std::invalid_argument);
                    } else {
                        EXPECT_FALSE(
                            solvers::CompileAcceptedSupplyInventory(forbidden, [&](const auto&) {
                                accepted = true;
                                return true;
                            }));
                    }
                    EXPECT_FALSE(accepted);
                }
            }
    }
}

TEST_F(DisassemblyVerifierTest, AutomaticJointInheritedColoredFragmentRejectsLooseDecoys) {
    for (int turn : {0, 2, 4})
        for (bool swapPorts : {false, true}) {
            io::PuzzleData p;
            p.version = 3;
            p.name = "joint-inherited-colored";
            p.outputMultiplier = 1;
            p.permissionBits =
                (1ULL << 0) | (1ULL << 2) | (1ULL << 3) | (1ULL << 22) | (1ULL << 23);
            p.inputs = {{{{core::AtomType::Salt, {0, 0}}}, {}},
                        {{{core::AtomType::Air, {0, 0}}}, {}},
                        {{{core::AtomType::Salt, {0, 0}}, {core::AtomType::Air, {1, 0}}},
                         {{14, {0, 0}, {1, 0}}}},
                        {{{core::AtomType::Fire, {0, 0}}}, {}}};
            const core::HexFrame frame{{0, 0}, turn};
            const auto salt = frame.ToWorld({0, 0}), air = frame.ToWorld({1, 0});
            p.outputs = {
                {{{core::AtomType::Air, air}, {core::AtomType::Salt, salt}}, {{14, salt, air}}},
                {{{core::AtomType::Fire, {0, 0}}}, {}}};
            if (swapPorts) std::swap(p.outputs[0], p.outputs[1]);
            WritePuzzle(puzzle, p);
            std::string failures;
            auto candidate = solvers::CompileAcceptedSupplyInventory(
                p,
                [&](const auto& s) {
                    io::BinaryWriter::WriteSolutionFile(solution, s);
                    const auto report = io::VerifySolutionFiles(puzzle, solution, 5000, 2000);
                    if (!report.Passed()) failures += report.detail + "\n";
                    return report.Passed();
                },
                [&](const auto& failure) { failures += failure.detail + "\n"; });
            ASSERT_TRUE(candidate) << "rotation " << turn << " swapped " << swapPorts << "\n"
                                   << failures;
            for (int pass = 0; pass < 2; ++pass) {
                io::BinaryWriter::WriteSolutionFile(solution, *candidate);
                const auto report = io::VerifySolutionFiles(puzzle, solution, 5000, 2000);
                ASSERT_TRUE(report.Passed()) << report.detail;
                candidate->metrics = report.metrics;
            }
        }
}

TEST_F(DisassemblyVerifierTest, AutomaticIdenticalFragmentsRouteToDifferentColoredOutputs) {
    for (bool swapPorts : {false, true}) {
        io::PuzzleData p;
        p.version = 3;
        p.name = "same-type-masked-fragments";
        p.outputMultiplier = 1;
        p.permissionBits =
            (1ULL << 0) | (1ULL << 2) | (1ULL << 3) | (1ULL << 9) | (1ULL << 22) | (1ULL << 23);
        p.inputs = {{{{core::AtomType::Fire, {0, 0}},
                      {core::AtomType::Fire, {1, 0}},
                      {core::AtomType::Fire, {2, 0}},
                      {core::AtomType::Fire, {3, 0}}},
                     {{2, {0, 0}, {1, 0}}, {1, {1, 0}, {2, 0}}, {8, {2, 0}, {3, 0}}}}};
        p.outputs = {{{{core::AtomType::Fire, {0, 0}}, {core::AtomType::Fire, {1, 0}}},
                      {{8, {0, 0}, {1, 0}}}},
                     {{{core::AtomType::Fire, {0, 0}}, {core::AtomType::Fire, {1, 0}}},
                      {{2, {0, 0}, {1, 0}}}}};
        if (swapPorts) std::swap(p.outputs[0], p.outputs[1]);
        WritePuzzle(puzzle, p);
        std::string failures;
        auto candidate = solvers::CompileAcceptedSupplyInventory(
            p,
            [&](const auto& s) {
                io::BinaryWriter::WriteSolutionFile(solution, s);
                const auto report = io::VerifySolutionFiles(puzzle, solution, 5000, 2000);
                if (!report.Passed()) failures += report.detail + "\n";
                return report.Passed();
            },
            [&](const auto& failure) { failures += failure.detail + "\n"; });
        ASSERT_TRUE(candidate) << "swapped " << swapPorts << "\n" << failures;
        for (int pass = 0; pass < 2; ++pass) {
            io::BinaryWriter::WriteSolutionFile(solution, *candidate);
            const auto report = io::VerifySolutionFiles(puzzle, solution, 5000, 2000);
            ASSERT_TRUE(report.Passed()) << report.detail;
            candidate->metrics = report.metrics;
        }
    }
}

TEST_F(DisassemblyVerifierTest, AutomaticJoinsInheritedFragmentsFromSeparateFeeds) {
    for (bool reverseInputs : {false, true}) {
        io::PuzzleData p;
        p.version = 3;
        p.name = "cross-feed-fragment-join";
        p.outputMultiplier = 1;
        p.permissionBits =
            (1ULL << 0) | (1ULL << 2) | (1ULL << 3) | (1ULL << 8) | (1ULL << 22) | (1ULL << 23);
        p.inputs = {{{{core::AtomType::Air, {0, 0}}, {core::AtomType::Salt, {1, 0}}},
                     {{14, {0, 0}, {1, 0}}}},
                    {{{core::AtomType::Fire, {0, 0}}, {core::AtomType::Earth, {1, 0}}},
                     {{14, {0, 0}, {1, 0}}}}};
        if (reverseInputs) std::swap(p.inputs[0], p.inputs[1]);
        p.outputs = {{{{core::AtomType::Air, {0, 0}},
                       {core::AtomType::Salt, {1, 0}},
                       {core::AtomType::Fire, {2, 0}},
                       {core::AtomType::Earth, {3, 0}}},
                      {{14, {0, 0}, {1, 0}}, {1, {1, 0}, {2, 0}}, {14, {2, 0}, {3, 0}}}}};
        WritePuzzle(puzzle, p);
        std::string failures;
        auto candidate = solvers::CompileAcceptedSupplyInventory(
            p,
            [&](const auto& s) {
                io::BinaryWriter::WriteSolutionFile(solution, s);
                const auto report = io::VerifySolutionFiles(puzzle, solution, 5000, 2000);
                if (!report.Passed()) failures += report.detail + "\n";
                return report.Passed();
            },
            [&](const auto& failure) { failures += failure.detail + "\n"; });
        ASSERT_TRUE(candidate) << "input order " << reverseInputs << "\n" << failures;
        for (int pass = 0; pass < 2; ++pass) {
            io::BinaryWriter::WriteSolutionFile(solution, *candidate);
            const auto report = io::VerifySolutionFiles(puzzle, solution, 5000, 2000);
            ASSERT_TRUE(report.Passed()) << report.detail;
            candidate->metrics = report.metrics;
        }
    }
}

TEST_F(DisassemblyVerifierTest, AutomaticRejectedFragmentSourceReplansToDistinctVerifiedMachine) {
    io::PuzzleData p;
    p.version = 3;
    p.name = "alternate-inherited-source";
    p.outputMultiplier = 1;
    p.permissionBits = (1ULL << 0) | (1ULL << 2) | (1ULL << 3) | (1ULL << 22) | (1ULL << 23);
    p.inputs = {
        {{{core::AtomType::Air, {0, 0}}, {core::AtomType::Salt, {1, 0}}}, {{14, {0, 0}, {1, 0}}}},
        {{{core::AtomType::Air, {0, 0}}, {core::AtomType::Salt, {1, 0}}}, {{14, {0, 0}, {1, 0}}}}};
    p.outputs = {p.inputs[0]};
    WritePuzzle(puzzle, p);
    std::optional<int> rejectedInput;
    std::string failures;
    auto candidate = solvers::CompileAcceptedSupplyInventory(
        p,
        [&](const auto& s) {
            io::BinaryWriter::WriteSolutionFile(solution, s);
            const auto report = io::VerifySolutionFiles(puzzle, solution, 5000, 2000);
            if (!report.Passed()) {
                failures += report.detail + "\n";
                return false;
            }
            const auto port =
                std::find_if(s.objects.begin(), s.objects.end(),
                             [](const auto& object) { return object.typeName == "input"; });
            EXPECT_NE(port, s.objects.end());
            if (port == s.objects.end()) return false;
            const int input = port->bodyData.at(2);
            if (!rejectedInput) rejectedInput = input;
            return input != *rejectedInput;
        },
        [&](const auto& failure) { failures += failure.detail + "\n"; });
    ASSERT_TRUE(candidate) << failures;
    ASSERT_TRUE(rejectedInput);
    const auto acceptedPort =
        std::find_if(candidate->objects.begin(), candidate->objects.end(),
                     [](const auto& object) { return object.typeName == "input"; });
    ASSERT_NE(acceptedPort, candidate->objects.end());
    EXPECT_NE(acceptedPort->bodyData.at(2), *rejectedInput);
    for (int pass = 0; pass < 2; ++pass) {
        io::BinaryWriter::WriteSolutionFile(solution, *candidate);
        const auto report = io::VerifySolutionFiles(puzzle, solution, 5000, 2000);
        ASSERT_TRUE(report.Passed()) << report.detail;
        candidate->metrics = report.metrics;
    }
}

TEST_F(DisassemblyVerifierTest, FixedArmWideFragmentWithoutTrackOrPiston) {
    io::PuzzleData p;
    p.version = 3;
    p.name = "fixed-wide-fragment";
    p.outputMultiplier = 1;
    p.permissionBits = (1ULL << 0) | (1ULL << 22) | (1ULL << 23);
    p.inputs = {{{{core::AtomType::Air, {0, 0}},
                  {core::AtomType::Salt, {0, 1}},
                  {core::AtomType::Earth, {0, 2}},
                  {core::AtomType::Fire, {0, 3}}},
                 {{1, {0, 0}, {0, 1}}, {1, {0, 1}, {0, 2}}, {1, {0, 2}, {0, 3}}}}};
    p.outputs = p.inputs;
    WritePuzzle(puzzle, p);
    std::string failures;
    auto accepted = [&](const auto& s) {
        io::BinaryWriter::WriteSolutionFile(solution, s);
        const auto report = io::VerifySolutionFiles(puzzle, solution, 5000, 2000);
        if (!report.Passed()) failures += report.detail + "\n";
        return report.Passed();
    };
    auto candidate = solvers::CompileAcceptedFixedArmInventory(
        p, accepted, [&](const auto& detail) { failures += detail + "\n"; });
    ASSERT_TRUE(candidate) << failures;
    EXPECT_TRUE(
        std::none_of(candidate->objects.begin(), candidate->objects.end(), [](const auto& object) {
            return object.typeName == "track" || object.typeName == "piston";
        }));
    for (int pass = 0; pass < 2; ++pass) {
        io::BinaryWriter::WriteSolutionFile(solution, *candidate);
        const auto report = io::VerifySolutionFiles(puzzle, solution, 5000, 2000);
        ASSERT_TRUE(report.Passed()) << report.detail;
        candidate->metrics = report.metrics;
    }
    const auto fromSolver = solvers::AssemblySolver{}.SolveAccepted(p, [&](const auto& s) {
        return s.solutionName == "Fixed-arm fragment inventory" && accepted(s);
    });
    EXPECT_EQ(fromSolver.solutionName, candidate->solutionName);
}

TEST_F(DisassemblyVerifierTest, FixedArmRejectsFirstLayoutAndKeepsPhysicalPermissions) {
    io::PuzzleData p;
    p.version = 3;
    p.name = "fixed-layout-retry";
    p.outputMultiplier = 1;
    p.permissionBits = (1ULL << 0) | (1ULL << 22) | (1ULL << 23);
    p.inputs = {
        {{{core::AtomType::Air, {0, 0}}, {core::AtomType::Salt, {0, 1}}}, {{1, {0, 0}, {0, 1}}}}};
    p.outputs = p.inputs;
    WritePuzzle(puzzle, p);
    std::optional<core::HexCoord> rejectedBase;
    auto candidate = solvers::CompileAcceptedFixedArmInventory(p, [&](const auto& s) {
        io::BinaryWriter::WriteSolutionFile(solution, s);
        const auto report = io::VerifySolutionFiles(puzzle, solution, 5000, 2000);
        if (!report.Passed()) return false;
        const auto base = s.objects.front().position;
        if (!rejectedBase) rejectedBase = base;
        return base != *rejectedBase;
    });
    ASSERT_TRUE(candidate);
    ASSERT_TRUE(rejectedBase);
    EXPECT_NE(candidate->objects.front().position, *rejectedBase);
    for (int pass = 0; pass < 2; ++pass) {
        io::BinaryWriter::WriteSolutionFile(solution, *candidate);
        const auto report = io::VerifySolutionFiles(puzzle, solution, 5000, 2000);
        ASSERT_TRUE(report.Passed()) << report.detail;
        candidate->metrics = report.metrics;
    }
    auto denied = p;
    denied.permissionBits &= ~(1ULL << 22);
    bool called = false;
    EXPECT_FALSE(solvers::CompileAcceptedFixedArmInventory(denied, [&](const auto&) {
        called = true;
        return true;
    }));
    EXPECT_FALSE(called);
    EXPECT_THROW((void)solvers::CompileAcceptedFixedArmInventory(
                     p,
                     [](const auto&) -> bool {
                         throw std::runtime_error("fixed-arm verifier unavailable");
                     }),
                 std::runtime_error);
}

TEST_F(DisassemblyVerifierTest, FixedArmJoinsIndependentFeedsWithoutTrackOrPiston) {
    for (bool reverseInputs : {false, true}) {
        io::PuzzleData p;
        p.version = 3;
        p.name = "trackless-fragment-join";
        p.outputMultiplier = 1;
        p.permissionBits = (1ULL << 0) | (1ULL << 8) | (1ULL << 22) | (1ULL << 23);
        p.inputs = {{{{core::AtomType::Air, {0, 0}}}, {}}, {{{core::AtomType::Salt, {0, 0}}}, {}}};
        if (reverseInputs) std::swap(p.inputs[0], p.inputs[1]);
        p.outputs = {{{{core::AtomType::Air, {0, 0}}, {core::AtomType::Salt, {1, 0}}},
                      {{1, {0, 0}, {1, 0}}}}};
        WritePuzzle(puzzle, p);
        std::string failures;
        auto verified = [&](const auto& s) {
            io::BinaryWriter::WriteSolutionFile(solution, s);
            const auto report = io::VerifySolutionFiles(puzzle, solution, 5000, 2000);
            if (!report.Passed()) failures += report.detail + "\n";
            return report.Passed();
        };
        auto candidate = solvers::CompileAcceptedFixedArmJoin(p, verified);
        ASSERT_TRUE(candidate) << "reverse=" << reverseInputs << "\n" << failures;
        EXPECT_TRUE(std::none_of(
            candidate->objects.begin(), candidate->objects.end(), [](const auto& object) {
                return object.typeName == "track" || object.typeName == "piston";
            }));
        for (int pass = 0; pass < 2; ++pass) {
            io::BinaryWriter::WriteSolutionFile(solution, *candidate);
            const auto report = io::VerifySolutionFiles(puzzle, solution, 5000, 2000);
            ASSERT_TRUE(report.Passed()) << report.detail;
            candidate->metrics = report.metrics;
        }
        const auto publicCandidate = solvers::AssemblySolver{}.SolveAccepted(p, [&](const auto& s) {
            return s.solutionName == "Fixed-arm joint fragment inventory" && verified(s);
        });
        EXPECT_EQ(publicCandidate.solutionName, candidate->solutionName);
    }
}

TEST_F(DisassemblyVerifierTest, SamePairMaterialChoosesRailOrFixedArmAfterRejection) {
    io::PuzzleData p;
    p.version = 3;
    p.name = "same-pair-two-mechanisms";
    p.outputMultiplier = 1;
    p.permissionBits =
        (1ULL << 0) | (1ULL << 2) | (1ULL << 3) | (1ULL << 8) | (1ULL << 22) | (1ULL << 23);
    p.inputs = {{{{core::AtomType::Air, {0, 0}}}, {}}, {{{core::AtomType::Salt, {0, 0}}}, {}}};
    p.outputs = {
        {{{core::AtomType::Air, {0, 0}}, {core::AtomType::Salt, {1, 0}}}, {{1, {0, 0}, {1, 0}}}}};
    WritePuzzle(puzzle, p);
    auto verified = [&](const auto& machine) {
        io::BinaryWriter::WriteSolutionFile(solution, machine);
        return io::VerifySolutionFiles(puzzle, solution, 5000, 2000).Passed();
    };
    auto rail = solvers::CompileAcceptedSupplyInventory(p, verified);
    ASSERT_TRUE(rail);
    auto fixed = solvers::CompileAcceptedFixedArmJoin(p, verified);
    ASSERT_TRUE(fixed);
    EXPECT_NE(rail->solutionName, fixed->solutionName);
    const auto chosen = solvers::AssemblySolver{}.SolveAccepted(p, [&](const auto& machine) {
        return machine.solutionName == "Fixed-arm joint fragment inventory" && verified(machine);
    });
    EXPECT_EQ(chosen.solutionName, fixed->solutionName);
    for (auto machine : {*rail, *fixed, chosen})
        for (int pass = 0; pass < 2; ++pass) {
            io::BinaryWriter::WriteSolutionFile(solution, machine);
            const auto report = io::VerifySolutionFiles(puzzle, solution, 5000, 2000);
            ASSERT_TRUE(report.Passed()) << report.detail;
            machine.metrics = report.metrics;
        }
}

TEST_F(DisassemblyVerifierTest, RegisteredOrdinaryFragmentRound3) {
    io::PuzzleData p;
    p.version = 3;
    p.name = "round3-two-dimers";
    p.outputMultiplier = 1;
    p.permissionBits = ~0ULL;
    p.inputs = {{{{core::AtomType::Air, {0, 0}}, {core::AtomType::Salt, {1, 0}}},
                 {{1, {0, 0}, {1, 0}}}},
                {{{core::AtomType::Earth, {0, 0}}, {core::AtomType::Water, {1, 0}}},
                 {{1, {0, 0}, {1, 0}}}}};
    p.outputs = {{{{core::AtomType::Air, {0, 0}}, {core::AtomType::Salt, {1, 0}},
                   {core::AtomType::Earth, {2, 0}}, {core::AtomType::Water, {3, 0}}},
                  {{1, {0, 0}, {1, 0}}, {1, {1, 0}, {2, 0}}, {1, {2, 0}, {3, 0}}}}};
    const auto baseline = p;
    for (int variant = 0; variant < 18; ++variant) {
        p = baseline;
        if (variant == 12) {
            p.inputs[0].bonds[0].bits = 14;
            p.outputs[0].bonds[0].bits = 14;
        }
        if (variant == 13 || variant == 15) {
            p.inputs[0].atoms.push_back({core::AtomType::Fire, {1, 1}});
            p.inputs[0].bonds.push_back({1, {1, 0}, {1, 1}});
            p.outputs[0].atoms.push_back({core::AtomType::Fire, {1, 1}});
            p.outputs[0].bonds.push_back({1, {1, 0}, {1, 1}});
        }
        if (variant == 15) {
            p.inputs[1].atoms.push_back({core::AtomType::Fire, {1, 1}});
            p.inputs[1].bonds.push_back({1, {1, 0}, {1, 1}});
            p.outputs[0].atoms.push_back({core::AtomType::Fire, {3, 1}});
            p.outputs[0].bonds.push_back({1, {3, 0}, {3, 1}});
        }
        if (variant == 16) {
            const core::HexFrame frame{{-3, 4}, 2};
            for (auto& atom : p.outputs[0].atoms) atom.position = frame.ToWorld(atom.position);
            for (auto& bond : p.outputs[0].bonds) {
                bond.from = frame.ToWorld(bond.from);
                bond.to = frame.ToWorld(bond.to);
            }
        }
        if (variant < 12) {
            const core::HexFrame frame{{2, -3}, variant % 6};
            for (auto& molecule : p.inputs) {
                for (auto& atom : molecule.atoms) atom.position = frame.ToWorld(atom.position);
                for (auto& bond : molecule.bonds) {
                    bond.from = frame.ToWorld(bond.from);
                    bond.to = frame.ToWorld(bond.to);
                }
            }
            if (variant >= 6) std::reverse(p.inputs.begin(), p.inputs.end());
        }
        WritePuzzle(puzzle, p);
        auto material = solvers::SearchMaterialExecution(p);
        ASSERT_TRUE(material.plan) << material.detail;
        if (variant == 17) {
            using IR = solvers::ExecutionIR;
            IR cutRebond;
            auto state = IR::Initial(p);
            for (const auto& step : material.plan->material.steps) {
                cutRebond.Append(p, state, step.operation);
                if (step.operation.kind == IR::Kind::Acquire) {
                    for (const auto& [edge, bits] : step.effects.bonds)
                        if (edge.a >= step.preconditions.nextId && edge.b >= step.preconditions.nextId) {
                            cutRebond.Append(p, state, {IR::Kind::Unbond, 0, {edge.a, edge.b}, bits});
                            cutRebond.Append(p, state, {IR::Kind::Bond, 0, {edge.a, edge.b}, bits});
                        }
                }
            }
            material.plan = solvers::MakeExecutionPlan(p, std::move(cutRebond));
        }
        std::string failures;
        int children = 0;
        auto gate = [&](const auto& candidate) {
            io::BinaryWriter::WriteSolutionFile(solution, candidate);
            const auto report = io::VerifySolutionFiles(puzzle, solution, 150000, 10000);
            std::cout << "variant=" << variant << " " << candidate.solutionName
                      << " status=" << report.Passed() << " cost=" << report.metrics.cost
                      << " cycles=" << report.metrics.cycles << " area=" << report.metrics.area
                      << " " << report.detail << " cycle=" << report.errorCycle << '\n';
            if (variant == 14 && std::getenv("OPUS_RETAINED_EVIDENCE")) {
                const auto directory = std::filesystem::path(std::getenv("OPUS_RETAINED_EVIDENCE"));
                std::filesystem::copy_file(puzzle, directory / "two-dimers.puzzle",
                                           std::filesystem::copy_options::overwrite_existing);
                std::filesystem::copy_file(solution, directory /
                    (candidate.solutionName == "Shared-plan retained ordinary fragments"
                         ? "two-dimers-retained.solution" : "two-dimers-parent.solution"),
                    std::filesystem::copy_options::overwrite_existing);
            }
            return report.Passed();
        };
        const auto parent = solvers::CompileAcceptedSerialArmExecution(
            p, *material.plan, gate, [&](const auto& detail) { failures += detail + "\n"; },
            std::chrono::steady_clock::now() + std::chrono::seconds(3), {},
            [&](const auto&, const auto& child) {
                if (child.solutionName == "Shared-plan retained ordinary fragments") {
                    ++children;
                    EXPECT_TRUE(gate(child)) << variant;
                    if (variant == 14) {
                        for (int bit : {0, 8, 22, 23, 26}) {
                            auto denied = p;
                            denied.permissionBits &= ~(1ULL << bit);
                            WritePuzzle(puzzle, denied);
                            const auto report = io::VerifySolutionFiles(puzzle, solution, 150000, 10000);
                            EXPECT_FALSE(report.Passed()) << "permission=" << bit;
                            std::cout << "PERMISSION " << bit << " accepted=" << report.Passed()
                                      << " " << report.detail << '\n';
                        }
                        WritePuzzle(puzzle, p);
                    }
                }
            });
        if (variant == 15 || variant == 17) {
            EXPECT_EQ(children, 0);
            std::cout << "BOUNDED_REFUSAL variant=" << variant << " parent=" << bool(parent)
                      << " " << failures;
            continue;
        }
        ASSERT_TRUE(parent) << variant << " " << failures;
        if (variant == 13) {
            EXPECT_EQ(children, 0);
            std::cout << "HELDOUT_REFUSAL " << failures;
        } else if (!std::getenv("OPUSOPTIMA_SERIAL_FRAGMENTS"))
            EXPECT_GT(children, 0) << variant << " " << failures;
        else
            EXPECT_EQ(children, 0);
    }
}
TEST_F(DisassemblyVerifierTest, FixedArmJoinsWideInheritedFragmentBeyondThreeRows) {
    for (bool reverseOutput : {false, true}) {
        io::PuzzleData p;
        p.version = 3;
        p.name = "wide-fragment-join";
        p.outputMultiplier = 1;
        p.permissionBits = (1ULL << 0) | (1ULL << 8) | (1ULL << 22) | (1ULL << 23);
        p.inputs = {{{{core::AtomType::Air, {0, 0}},
                      {core::AtomType::Salt, {1, 0}},
                      {core::AtomType::Earth, {2, 0}},
                      {core::AtomType::Fire, {2, 1}},
                      {core::AtomType::Water, {1, 2}},
                      {core::AtomType::Copper, {2, -1}},
                      {core::AtomType::Quicksilver, {3, -1}}},
                     {{1, {0, 0}, {1, 0}},
                      {1, {1, 0}, {2, 0}},
                      {1, {2, 0}, {2, 1}},
                      {1, {2, 1}, {1, 2}},
                      {1, {2, 0}, {2, -1}},
                      {1, {2, -1}, {3, -1}}}},
                    {{{core::AtomType::Tin, {0, 0}}}, {}}};
        p.outputs = {p.inputs[0]};
        p.outputs[0].atoms.push_back({core::AtomType::Tin, {-1, 1}});
        p.outputs[0].bonds.push_back({1, {0, 0}, {-1, 1}});
        if (reverseOutput) std::reverse(p.outputs[0].atoms.begin(), p.outputs[0].atoms.end());
        int narrowest = 100;
        for (int turns = 0; turns < 6; ++turns) {
            int low = 100, high = -100;
            for (const auto& atom : p.outputs[0].atoms) {
                const auto position = core::HexFrame{{}, turns}.ToWorld(atom.position);
                low = std::min(low, position.r);
                high = std::max(high, position.r);
            }
            narrowest = std::min(narrowest, high - low + 1);
        }
        ASSERT_GT(narrowest, 3);
        WritePuzzle(puzzle, p);
        std::string failures;
        auto candidate = solvers::CompileAcceptedFixedArmJoin(p, [&](const auto& s) {
            io::BinaryWriter::WriteSolutionFile(solution, s);
            const auto report = io::VerifySolutionFiles(puzzle, solution, 5000, 2000);
            if (!report.Passed()) failures += report.detail + "\n";
            return report.Passed();
        });
        ASSERT_TRUE(candidate) << "reversed=" << reverseOutput << "\n" << failures;
        for (int pass = 0; pass < 2; ++pass) {
            io::BinaryWriter::WriteSolutionFile(solution, *candidate);
            const auto report = io::VerifySolutionFiles(puzzle, solution, 5000, 2000);
            ASSERT_TRUE(report.Passed()) << report.detail;
            candidate->metrics = report.metrics;
        }
        if (!reverseOutput) {
            const auto publicCandidate =
                solvers::AssemblySolver{}.SolveAccepted(p, [&](const auto& s) {
                    if (s.solutionName != "Fixed-arm joint fragment inventory") return false;
                    io::BinaryWriter::WriteSolutionFile(solution, s);
                    return io::VerifySolutionFiles(puzzle, solution, 5000, 2000).Passed();
                });
            EXPECT_EQ(publicCandidate.solutionName, candidate->solutionName);
        }
        auto forbidden = p;
        forbidden.permissionBits &= ~(1ULL << 8);
        EXPECT_FALSE(
            solvers::CompileAcceptedFixedArmJoin(forbidden, [](const auto&) { return true; }));
    }
}

TEST_F(DisassemblyVerifierTest, AutomaticInheritedFragmentJoinsConvertedSecondFeed) {
    io::PuzzleData p;
    p.version = 3;
    p.name = "inherited-plus-chemistry";
    p.outputMultiplier = 1;
    p.permissionBits = (1ULL << 0) | (1ULL << 2) | (1ULL << 3) | (1ULL << 8) | (1ULL << 12) |
                       (1ULL << 22) | (1ULL << 23);
    p.inputs = {
        {{{core::AtomType::Air, {0, 0}}, {core::AtomType::Salt, {1, 0}}}, {{14, {0, 0}, {1, 0}}}},
        {{{core::AtomType::Earth, {0, 0}}}, {}}};
    p.outputs = {{{{core::AtomType::Air, {0, 0}},
                   {core::AtomType::Salt, {1, 0}},
                   {core::AtomType::Salt, {2, 0}}},
                  {{14, {0, 0}, {1, 0}}, {1, {1, 0}, {2, 0}}}}};
    WritePuzzle(puzzle, p);
    std::string failures;
    auto candidate = solvers::CompileAcceptedSupplyInventory(
        p,
        [&](const auto& s) {
            io::BinaryWriter::WriteSolutionFile(solution, s);
            const auto report = io::VerifySolutionFiles(puzzle, solution, 5000, 2000);
            if (!report.Passed()) failures += report.detail + "\n";
            return report.Passed();
        },
        [&](const auto& failure) { failures += failure.detail + "\n"; });
    ASSERT_TRUE(candidate) << failures;
    for (int pass = 0; pass < 2; ++pass) {
        io::BinaryWriter::WriteSolutionFile(solution, *candidate);
        const auto report = io::VerifySolutionFiles(puzzle, solution, 5000, 2000);
        ASSERT_TRUE(report.Passed()) << report.detail;
        candidate->metrics = report.metrics;
    }
}

TEST_F(DisassemblyVerifierTest, AutomaticInheritedFragmentPivotWithoutBonders) {
    io::PuzzleData p;
    p.version = 3;
    p.name = "inherited-fragment-pivot";
    p.outputMultiplier = 1;
    p.permissionBits =
        (1ULL << 0) | (1ULL << 2) | (1ULL << 3) | (1ULL << 22) | (1ULL << 23) | (1ULL << 26);
    p.inputs = {
        {{{core::AtomType::Air, {0, 0}}, {core::AtomType::Salt, {1, 0}}}, {{14, {0, 0}, {1, 0}}}}};
    p.outputs = {
        {{{core::AtomType::Air, {0, 0}}, {core::AtomType::Salt, {0, 1}}}, {{14, {0, 0}, {0, 1}}}}};
    WritePuzzle(puzzle, p);
    std::string failures;
    auto candidate = solvers::CompileAcceptedSupplyInventory(
        p,
        [&](const auto& s) {
            io::BinaryWriter::WriteSolutionFile(solution, s);
            return io::VerifySolutionFiles(puzzle, solution, 5000, 2000).Passed();
        },
        [&](const auto& failure) { failures += failure.detail + "\n"; });
    ASSERT_TRUE(candidate) << failures;
    EXPECT_TRUE(
        std::any_of(candidate->objects.begin(), candidate->objects.end(), [](const auto& object) {
            return std::any_of(object.instructions.begin(), object.instructions.end(),
                               [](const auto& instruction) {
                                   return instruction.action == 'P' || instruction.action == 'p';
                               });
        }));
    for (int pass = 0; pass < 2; ++pass) {
        io::BinaryWriter::WriteSolutionFile(solution, *candidate);
        const auto report = io::VerifySolutionFiles(puzzle, solution, 5000, 2000);
        ASSERT_TRUE(report.Passed()) << report.detail;
        candidate->metrics = report.metrics;
    }
}

TEST_F(DisassemblyVerifierTest, AutomaticColoredBondInventory) {
    for (int mask : {2, 4, 6, 8, 10, 12, 14})
        for (const core::HexCoord delta :
             {core::HexCoord{1, 0}, core::HexCoord{0, 1}, core::HexCoord{-1, 1}}) {
            SCOPED_TRACE(mask);
            io::PuzzleData p;
            p.version = 3;
            p.name = "automatic-colored-inventory";
            p.outputMultiplier = 1;
            p.permissionBits = (1ULL << 0) | (1ULL << 2) | (1ULL << 3) | (1ULL << 9) |
                               (1ULL << 11) | (1ULL << 22) | (1ULL << 23);
            p.inputs = {{{{core::AtomType::Fire, {0, 0}}}, {}}};
            p.outputs = {{{{core::AtomType::Fire, {0, 0}}, {core::AtomType::Fire, delta}},
                          {{static_cast<std::uint8_t>(mask), {0, 0}, delta}}}};
            WritePuzzle(puzzle, p);
            std::string failures;
            auto candidate = solvers::CompileAcceptedSupplyInventory(
                p,
                [&](const auto& s) {
                    io::BinaryWriter::WriteSolutionFile(solution, s);
                    const auto report = io::VerifySolutionFiles(puzzle, solution, 5000, 2000);
                    if (!report.Passed()) failures += report.detail + "\n";
                    return report.Passed();
                },
                [&](const auto& failure) { failures += failure.detail + "\n"; });
            ASSERT_TRUE(candidate) << failures;
            EXPECT_TRUE(
                std::any_of(candidate->objects.begin(), candidate->objects.end(),
                            [](const auto& object) { return object.typeName == "bonder-prisma"; }));
            for (int pass = 0; pass < 2; ++pass) {
                io::BinaryWriter::WriteSolutionFile(solution, *candidate);
                const auto report = io::VerifySolutionFiles(puzzle, solution, 5000, 2000);
                ASSERT_TRUE(report.Passed()) << report.detail;
                candidate->metrics = report.metrics;
            }
        }
}

TEST_F(DisassemblyVerifierTest, AutomaticColoredMaskReassemblyInventory) {
    for (int mask : {2, 4, 6, 8, 10, 12}) {
        SCOPED_TRACE(mask);
        io::PuzzleData p;
        p.version = 3;
        p.name = "automatic-mask-reassembly";
        p.outputMultiplier = 1;
        p.permissionBits = (1ULL << 0) | (1ULL << 2) | (1ULL << 3) | (1ULL << 9) | (1ULL << 11) |
                           (1ULL << 22) | (1ULL << 23);
        p.inputs = {{{{core::AtomType::Fire, {0, 0}}, {core::AtomType::Fire, {1, 0}}},
                     {{14, {0, 0}, {1, 0}}}}};
        p.outputs = {{{{core::AtomType::Fire, {0, 0}}, {core::AtomType::Fire, {1, 0}}},
                      {{static_cast<std::uint8_t>(mask), {0, 0}, {1, 0}}}}};
        WritePuzzle(puzzle, p);
        std::string failures;
        auto candidate = solvers::CompileAcceptedSupplyInventory(
            p,
            [&](const auto& s) {
                io::BinaryWriter::WriteSolutionFile(solution, s);
                const auto report = io::VerifySolutionFiles(puzzle, solution, 5000, 2000);
                if (!report.Passed()) failures += report.detail + "\n";
                return report.Passed();
            },
            [&](const auto& failure) { failures += failure.detail + "\n"; });
        ASSERT_TRUE(candidate) << failures;
        for (int pass = 0; pass < 2; ++pass) {
            io::BinaryWriter::WriteSolutionFile(solution, *candidate);
            const auto report = io::VerifySolutionFiles(puzzle, solution, 5000, 2000);
            ASSERT_TRUE(report.Passed()) << report.detail;
            candidate->metrics = report.metrics;
        }
    }
}

TEST_F(DisassemblyVerifierTest, AutomaticColoredTriangleInventory) {
    for (unsigned fullEdges = 0; fullEdges < 8; ++fullEdges) {
        SCOPED_TRACE(fullEdges);
        io::PuzzleData p;
        p.version = 3;
        p.name = "automatic-colored-triangle";
        p.outputMultiplier = 1;
        p.permissionBits =
            (1ULL << 0) | (1ULL << 2) | (1ULL << 3) | (1ULL << 11) | (1ULL << 22) | (1ULL << 23);
        p.inputs = {{{{core::AtomType::Fire, {0, 0}}}, {}}};
        p.outputs = {{{{core::AtomType::Fire, {0, 0}},
                       {core::AtomType::Fire, {0, 1}},
                       {core::AtomType::Fire, {1, 0}}},
                      {{8, {0, 0}, {0, 1}}, {2, {0, 1}, {1, 0}}, {4, {1, 0}, {0, 0}}}}};
        for (std::size_t edge = 0; edge < 3; ++edge)
            if (fullEdges & (1U << edge)) p.outputs[0].bonds[edge].bits = 14;
        WritePuzzle(puzzle, p);
        std::string failures;
        auto candidate = solvers::CompileAcceptedSupplyInventory(
            p,
            [&](const auto& s) {
                io::BinaryWriter::WriteSolutionFile(solution, s);
                const auto report = io::VerifySolutionFiles(puzzle, solution, 5000, 2000);
                if (!report.Passed()) failures += report.detail + "\n";
                return report.Passed();
            },
            [&](const auto& failure) { failures += failure.detail + "\n"; });
        ASSERT_TRUE(candidate) << failures;
        for (int pass = 0; pass < 2; ++pass) {
            io::BinaryWriter::WriteSolutionFile(solution, *candidate);
            const auto report = io::VerifySolutionFiles(puzzle, solution, 5000, 2000);
            ASSERT_TRUE(report.Passed()) << report.detail;
            candidate->metrics = report.metrics;
        }
    }
}

TEST_F(DisassemblyVerifierTest, AutomaticMixedBondInventory) {
    for (int mask : {2, 14}) {
        io::PuzzleData p;
        p.version = 3;
        p.name = "automatic-mixed-inventory";
        p.outputMultiplier = 1;
        p.permissionBits = (1ULL << 0) | (1ULL << 2) | (1ULL << 3) | (1ULL << 8) | (1ULL << 9) |
                           (1ULL << 11) | (1ULL << 22) | (1ULL << 23);
        p.inputs = {{{{core::AtomType::Fire, {0, 0}}}, {}}};
        p.outputs = {{{{core::AtomType::Fire, {0, 0}},
                       {core::AtomType::Fire, {1, 0}},
                       {core::AtomType::Fire, {2, 0}}},
                      {{static_cast<std::uint8_t>(mask), {0, 0}, {1, 0}}, {1, {1, 0}, {2, 0}}}}};
        WritePuzzle(puzzle, p);
        auto candidate = solvers::CompileAcceptedSupplyInventory(p, [&](const auto& s) {
            io::BinaryWriter::WriteSolutionFile(solution, s);
            return io::VerifySolutionFiles(puzzle, solution, 5000, 2000).Passed();
        });
        ASSERT_TRUE(candidate) << mask;
        for (int pass = 0; pass < 2; ++pass) {
            io::BinaryWriter::WriteSolutionFile(solution, *candidate);
            const auto report = io::VerifySolutionFiles(puzzle, solution, 5000, 2000);
            ASSERT_TRUE(report.Passed()) << report.detail;
            candidate->metrics = report.metrics;
        }
    }
}

TEST_F(DisassemblyVerifierTest, AutomaticCoproductCatalystJointInventory) {
    io::PuzzleData p;
    p.version = 3;
    p.name = "automatic-coproduct-catalyst";
    p.outputMultiplier = 1;
    p.permissionBits = (1ULL << 0) | (1ULL << 2) | (1ULL << 3) | (1ULL << 13) | (1ULL << 18) |
                       (1ULL << 22) | (1ULL << 23);
    p.inputs = {{{{core::AtomType::Quintessence, {0, 0}}}, {}},
                {{{core::AtomType::Salt, {0, 0}}}, {}}};
    for (auto type : {core::AtomType::Earth, core::AtomType::Air, core::AtomType::Air,
                      core::AtomType::Fire, core::AtomType::Water})
        p.outputs.push_back({{{type, {0, 0}}}, {}});
    WritePuzzle(puzzle, p);
    std::string failures;
    auto candidate = solvers::CompileAcceptedSupplyInventory(
        p,
        [&](const auto& s) {
            io::BinaryWriter::WriteSolutionFile(solution, s);
            const auto report = io::VerifySolutionFiles(puzzle, solution, 5000, 2000);
            if (!report.Passed()) failures += report.detail + "\n";
            return report.Passed();
        },
        [&](const auto& failure) { failures += failure.detail + "\n"; });
    ASSERT_TRUE(candidate) << failures;
    for (int pass = 0; pass < 2; ++pass) {
        io::BinaryWriter::WriteSolutionFile(solution, *candidate);
        const auto report = io::VerifySolutionFiles(puzzle, solution, 5000, 2000);
        ASSERT_TRUE(report.Passed()) << report.detail;
        candidate->metrics = report.metrics;
    }
}

TEST_F(DisassemblyVerifierTest, AutomaticCoproductCatalystColoredJointInventory) {
    for (int mask : {1, 2, 14}) {
        SCOPED_TRACE(mask);
        io::PuzzleData p;
        p.version = 3;
        p.name = "automatic-coproduct-catalyst-colored";
        p.outputMultiplier = 1;
        p.permissionBits = (1ULL << 0) | (1ULL << 2) | (1ULL << 3) | (1ULL << 8) | (1ULL << 9) |
                           (1ULL << 11) | (1ULL << 13) | (1ULL << 18) | (1ULL << 22) | (1ULL << 23);
        p.inputs = {{{{core::AtomType::Quintessence, {0, 0}}}, {}},
                    {{{core::AtomType::Salt, {0, 0}}}, {}}};
        p.outputs = {{{{core::AtomType::Fire, {0, 0}}, {core::AtomType::Fire, {1, 0}}},
                      {{static_cast<std::uint8_t>(mask), {0, 0}, {1, 0}}}}};
        for (auto type : {core::AtomType::Earth, core::AtomType::Air, core::AtomType::Water})
            p.outputs.push_back({{{type, {0, 0}}}, {}});
        WritePuzzle(puzzle, p);
        std::string failures;
        auto candidate = solvers::CompileAcceptedSupplyInventory(
            p,
            [&](const auto& s) {
                io::BinaryWriter::WriteSolutionFile(solution, s);
                const auto report = io::VerifySolutionFiles(puzzle, solution, 5000, 2000);
                if (!report.Passed()) failures += report.detail + "\n";
                return report.Passed();
            },
            [&](const auto& failure) { failures += failure.detail + "\n"; });
        ASSERT_TRUE(candidate) << failures;
        for (int pass = 0; pass < 2; ++pass) {
            io::BinaryWriter::WriteSolutionFile(solution, *candidate);
            const auto report = io::VerifySolutionFiles(puzzle, solution, 5000, 2000);
            ASSERT_TRUE(report.Passed()) << report.detail;
            candidate->metrics = report.metrics;
        }
    }
}

TEST_F(DisassemblyVerifierTest, AutomaticStagedBondMaskConversions) {
    for (const auto [source, target] : {std::pair{1, 2}, {2, 1}, {14, 2}, {6, 8}}) {
        SCOPED_TRACE(source);
        SCOPED_TRACE(target);
        io::PuzzleData p;
        p.version = 3;
        p.name = "automatic-staged-bond-mask";
        p.outputMultiplier = 1;
        p.permissionBits = (1ULL << 0) | (1ULL << 2) | (1ULL << 3) | (1ULL << 8) | (1ULL << 9) |
                           (1ULL << 11) | (1ULL << 22) | (1ULL << 23);
        p.inputs = {{{{core::AtomType::Fire, {0, 0}}, {core::AtomType::Fire, {1, 0}}},
                     {{static_cast<std::uint8_t>(source), {0, 0}, {1, 0}}}}};
        p.outputs = p.inputs;
        p.outputs[0].bonds = {{static_cast<std::uint8_t>(target), {0, 0}, {1, 0}}};
        WritePuzzle(puzzle, p);
        std::string failures;
        auto candidate = solvers::CompileAcceptedSupplyInventory(
            p,
            [&](const auto& s) {
                io::BinaryWriter::WriteSolutionFile(solution, s);
                const auto report = io::VerifySolutionFiles(puzzle, solution, 5000, 2000);
                if (!report.Passed()) failures += report.detail + "\n";
                return report.Passed();
            },
            [&](const auto& failure) { failures += failure.detail + "\n"; });
        ASSERT_TRUE(candidate) << failures;
        for (int pass = 0; pass < 2; ++pass) {
            io::BinaryWriter::WriteSolutionFile(solution, *candidate);
            const auto report = io::VerifySolutionFiles(puzzle, solution, 5000, 2000);
            ASSERT_TRUE(report.Passed()) << report.detail;
            candidate->metrics = report.metrics;
        }
    }
}

TEST_F(DisassemblyVerifierTest, AutomaticCalcifiedDispersionJointInventory) {
    for (int consumed = 2; consumed <= 5; ++consumed) {
        for (bool saltFirst : {true, false}) {
            SCOPED_TRACE(consumed);
            SCOPED_TRACE(saltFirst);
            io::PuzzleData p;
            p.version = 3;
            p.name = "automatic-calcified-dispersion";
            p.outputMultiplier = 1;
            p.permissionBits = (1ULL << 0) | (1ULL << 2) | (1ULL << 3) | (1ULL << 12) |
                               (1ULL << 18) | (1ULL << 22) | (1ULL << 23);
            p.inputs = {{{{core::AtomType::Quintessence, {0, 0}}}, {}}};
            p.outputs = {{{{core::AtomType::Salt, {0, 0}}}, {}}};
            for (int element = 2; element <= 5; ++element)
                if (element != consumed)
                    p.outputs.push_back({{{static_cast<core::AtomType>(element), {0, 0}}}, {}});
            if (!saltFirst) std::reverse(p.outputs.begin(), p.outputs.end());
            WritePuzzle(puzzle, p);
            std::string failures;
            auto candidate = solvers::CompileAcceptedSupplyInventory(
                p,
                [&](const auto& s) {
                    io::BinaryWriter::WriteSolutionFile(solution, s);
                    const auto report = io::VerifySolutionFiles(puzzle, solution, 5000, 2000);
                    if (!report.Passed()) failures += report.detail + "\n";
                    return report.Passed();
                },
                [&](const auto& failure) { failures += failure.detail + "\n"; });
            ASSERT_TRUE(candidate) << failures;
            for (int pass = 0; pass < 2; ++pass) {
                io::BinaryWriter::WriteSolutionFile(solution, *candidate);
                const auto report = io::VerifySolutionFiles(puzzle, solution, 5000, 2000);
                ASSERT_TRUE(report.Passed()) << report.detail;
                candidate->metrics = report.metrics;
            }
        }
    }
}

TEST_F(DisassemblyVerifierTest, AutomaticCatalystJointInventory) {
    io::PuzzleData p;
    p.version = 3;
    p.name = "automatic-catalyst-joint";
    p.outputMultiplier = 1;
    p.permissionBits = (1ULL << 0) | (1ULL << 2) | (1ULL << 3) | (1ULL << 8) | (1ULL << 9) |
                       (1ULL << 13) | (1ULL << 22) | (1ULL << 23);
    p.inputs = {
        {{{core::AtomType::Air, {0, 0}}, {core::AtomType::Salt, {1, 0}}}, {{1, {0, 0}, {1, 0}}}}};
    p.outputs = {{{{core::AtomType::Air, {0, 0}}}, {}}, {{{core::AtomType::Air, {0, 0}}}, {}}};
    WritePuzzle(puzzle, p);
    std::string failures;
    auto candidate = solvers::CompileAcceptedSupplyInventory(
        p,
        [&](const auto& s) {
            io::BinaryWriter::WriteSolutionFile(solution, s);
            const auto report = io::VerifySolutionFiles(puzzle, solution, 5000, 2000);
            if (!report.Passed()) failures += report.detail + "\n";
            return report.Passed();
        },
        [&](const auto& failure) { failures += failure.detail + "\n"; });
    ASSERT_TRUE(candidate) << failures;
    EXPECT_TRUE(
        std::any_of(candidate->objects.begin(), candidate->objects.end(),
                    [](const auto& object) { return object.typeName == "glyph-duplication"; }));
    for (int pass = 0; pass < 2; ++pass) {
        io::BinaryWriter::WriteSolutionFile(solution, *candidate);
        const auto report = io::VerifySolutionFiles(puzzle, solution, 5000, 2000);
        ASSERT_TRUE(report.Passed()) << report.detail;
        candidate->metrics = report.metrics;
    }
}

TEST_F(DisassemblyVerifierTest, AutomaticQuintessenceJointInventory) {
    for (bool unify : {false, true}) {
        io::PuzzleData p;
        p.version = 3;
        p.name = "automatic-quintessence";
        p.outputMultiplier = 1;
        p.permissionBits = (1ULL << 0) | (1ULL << 2) | (1ULL << 3) | (1ULL << 8) | (1ULL << 18) |
                           (1ULL << 22) | (1ULL << 23);
        p.inputs = {{{{core::AtomType::Quintessence, {0, 0}}}, {}}};
        for (auto type : {core::AtomType::Water, core::AtomType::Fire, core::AtomType::Earth,
                          core::AtomType::Air})
            p.outputs.push_back({{{type, {0, 0}}}, {}});
        if (unify) std::swap(p.inputs, p.outputs);
        WritePuzzle(puzzle, p);
        std::string failures;
        auto candidate = solvers::CompileAcceptedSupplyInventory(
            p,
            [&](const auto& s) {
                io::BinaryWriter::WriteSolutionFile(solution, s);
                const auto report = io::VerifySolutionFiles(puzzle, solution, 5000, 2000);
                if (!report.Passed()) failures += report.detail + "\n";
                return report.Passed();
            },
            [&](const auto& failure) { failures += failure.detail + "\n"; });
        ASSERT_TRUE(candidate) << "unify=" << unify << "\n" << failures;
        EXPECT_TRUE(std::any_of(
            candidate->objects.begin(), candidate->objects.end(), [&](const auto& object) {
                return object.typeName == (unify ? "glyph-unification" : "glyph-dispersion");
            }));
        for (int pass = 0; pass < 2; ++pass) {
            io::BinaryWriter::WriteSolutionFile(solution, *candidate);
            const auto report = io::VerifySolutionFiles(puzzle, solution, 5000, 2000);
            ASSERT_TRUE(report.Passed()) << report.detail;
            candidate->metrics = report.metrics;
        }
    }
}

TEST_F(DisassemblyVerifierTest, AutomaticPurificationStation) {
    io::PuzzleData p;
    p.version = 3;
    p.name = "automatic-purification";
    p.outputMultiplier = 1;
    p.permissionBits = (1ULL << 0) | (1ULL << 2) | (1ULL << 3) | (1ULL << 8) | (1ULL << 15) |
                       (1ULL << 22) | (1ULL << 23);
    p.inputs = {{{{core::AtomType::Lead, {0, 0}}}, {}}};
    p.outputs = {{{{core::AtomType::Tin, {0, 0}}}, {}}};
    WritePuzzle(puzzle, p);
    std::string failures;
    auto candidate = solvers::CompileAcceptedSupplyInventory(
        p,
        [&](const auto& s) {
            io::BinaryWriter::WriteSolutionFile(solution, s);
            return io::VerifySolutionFiles(puzzle, solution, 5000, 2000).Passed();
        },
        [&](const auto& failure) { failures += failure.detail + "\n"; });
    ASSERT_TRUE(candidate) << failures;
    EXPECT_TRUE(
        std::any_of(candidate->objects.begin(), candidate->objects.end(),
                    [](const auto& object) { return object.typeName == "glyph-purification"; }));
    io::BinaryWriter::WriteSolutionFile(solution, *candidate);
    const auto report = io::VerifySolutionFiles(puzzle, solution, 5000, 2000);
    ASSERT_TRUE(report.Passed());
    candidate->metrics = report.metrics;
    io::BinaryWriter::WriteSolutionFile(solution, *candidate);
    EXPECT_TRUE(io::VerifySolutionFiles(puzzle, solution, 5000, 2000).Passed());
    for (const auto permission : {2, 15}) {
        auto denied = p;
        denied.permissionBits &= ~(1ULL << permission);
        bool accepted = false;
        try {
            const auto rejected = solvers::CompileAcceptedSupplyInventory(denied, [&](const auto&) {
                accepted = true;
                return true;
            });
            EXPECT_FALSE(rejected);
        } catch (const std::invalid_argument&) {
        }
        EXPECT_FALSE(accepted);
    }
}

TEST_F(DisassemblyVerifierTest, AutomaticPurificationRejectsMalformedMoleculesBeforeAcceptance) {
    io::PuzzleData p;
    p.version = 3;
    p.outputMultiplier = 1;
    p.permissionBits =
        (1ULL << 0) | (1ULL << 2) | (1ULL << 3) | (1ULL << 15) | (1ULL << 22) | (1ULL << 23);
    p.inputs = {{{{core::AtomType::Lead, {0, 0}}}, {}}};
    p.outputs = {{{{core::AtomType::Tin, {0, 0}}}, {}}};
    for (int mutation = 0; mutation < 4; ++mutation) {
        auto malformed = p;
        if (mutation == 0) malformed.inputs[0].atoms.push_back(malformed.inputs[0].atoms.front());
        if (mutation == 1) malformed.outputs[0].atoms.front().position = {128, 0};
        if (mutation == 2)
            malformed.outputs[0].atoms.front().type = static_cast<core::AtomType>(255);
        if (mutation == 3) malformed.outputs[0].bonds.push_back({1, {0, 0}, {1, 0}});
        bool accepted = false;
        EXPECT_THROW(solvers::CompileAcceptedSupplyInventory(malformed,
                                                             [&](const auto&) {
                                                                 accepted = true;
                                                                 return true;
                                                             }),
                     std::invalid_argument);
        EXPECT_FALSE(accepted);
    }
}

TEST_F(DisassemblyVerifierTest, AutomaticPurificationSharesJointFeedStock) {
    io::PuzzleData p;
    p.version = 3;
    p.name = "joint-purification";
    p.outputMultiplier = 1;
    p.permissionBits = (1ULL << 0) | (1ULL << 2) | (1ULL << 3) | (1ULL << 8) | (1ULL << 9) |
                       (1ULL << 15) | (1ULL << 22) | (1ULL << 23);
    p.inputs = {
        {{{core::AtomType::Lead, {0, 0}}, {core::AtomType::Lead, {1, 0}}}, {{1, {0, 0}, {1, 0}}}}};
    p.outputs = {{{{core::AtomType::Tin, {0, 0}}}, {}}, {{{core::AtomType::Lead, {0, 0}}}, {}}};
    WritePuzzle(puzzle, p);
    std::string failures;
    auto candidate = solvers::CompileAcceptedSupplyInventory(
        p,
        [&](const auto& s) {
            io::BinaryWriter::WriteSolutionFile(solution, s);
            return io::VerifySolutionFiles(puzzle, solution, 5000, 2000).Passed();
        },
        [&](const auto& failure) { failures += failure.detail + "\n"; });
    ASSERT_TRUE(candidate) << failures;
    io::BinaryWriter::WriteSolutionFile(solution, *candidate);
    const auto report = io::VerifySolutionFiles(puzzle, solution, 5000, 2000);
    ASSERT_TRUE(report.Passed());
    candidate->metrics = report.metrics;
    io::BinaryWriter::WriteSolutionFile(solution, *candidate);
    EXPECT_TRUE(io::VerifySolutionFiles(puzzle, solution, 5000, 2000).Passed());
}

TEST_F(DisassemblyVerifierTest, AutomaticMultistagePurificationVerifiesIndependently) {
    for (int feedShape : {0, 1, 2, 3})
        for (const auto target : {core::AtomType::Iron, core::AtomType::Copper}) {
            SCOPED_TRACE(feedShape);
            SCOPED_TRACE(static_cast<int>(target));
            io::PuzzleData p;
            p.version = 3;
            p.name = "multistage-purification";
            p.outputMultiplier = 1;
            p.permissionBits = (1ULL << 0) | (1ULL << 2) | (1ULL << 3) | (1ULL << 8) | (1ULL << 9) |
                               (1ULL << 15) | (1ULL << 22) | (1ULL << 23);
            p.inputs = {{{{core::AtomType::Lead, {0, 0}}}, {}}};
            if (feedShape != 0) {
                p.inputs[0].atoms.push_back({core::AtomType::Lead, {1, 0}});
                p.inputs[0].bonds.push_back({1, {0, 0}, {1, 0}});
            }
            if (feedShape >= 2) {
                p.inputs[0].atoms.push_back({core::AtomType::Lead, {0, 1}});
                p.inputs[0].bonds.push_back({1, {0, 0}, {0, 1}});
            }
            if (feedShape == 3) {
                std::reverse(p.inputs[0].atoms.begin(), p.inputs[0].atoms.end());
                for (auto& atom : p.inputs[0].atoms)
                    atom.position = core::Add(core::RotateClockwise(atom.position), {2, -3});
                for (auto& bond : p.inputs[0].bonds) {
                    bond.from = core::Add(core::RotateClockwise(bond.from), {2, -3});
                    bond.to = core::Add(core::RotateClockwise(bond.to), {2, -3});
                }
            }
            p.outputs = {{{{target, {0, 0}}}, {}}};
            WritePuzzle(puzzle, p);
            std::string failures;
            auto candidate = solvers::CompileAcceptedSupplyInventory(
                p,
                [&](const auto& s) {
                    io::BinaryWriter::WriteSolutionFile(solution, s);
                    return io::VerifySolutionFiles(puzzle, solution, 20000, 10000).Passed();
                },
                [&](const auto& failure) { failures += failure.detail + "\n"; });
            ASSERT_TRUE(candidate) << failures;
            io::BinaryWriter::WriteSolutionFile(solution, *candidate);
            const auto report = io::VerifySolutionFiles(puzzle, solution, 20000, 10000);
            ASSERT_TRUE(report.Passed());
            candidate->metrics = report.metrics;
            io::BinaryWriter::WriteSolutionFile(solution, *candidate);
            EXPECT_TRUE(io::VerifySolutionFiles(puzzle, solution, 20000, 10000).Passed());
        }
}

TEST_F(DisassemblyVerifierTest, AutomaticPurificationReacquiresStoredResiduals) {
    for (bool branched : {false, true}) {
        SCOPED_TRACE(branched);
        io::PuzzleData p;
        p.version = 3;
        p.name = "stored-residual-purification";
        p.outputMultiplier = 1;
        p.permissionBits = (1ULL << 0) | (1ULL << 2) | (1ULL << 3) | (1ULL << 8) | (1ULL << 9) |
                           (1ULL << 15) | (1ULL << 22) | (1ULL << 23);
        p.inputs = {{{{core::AtomType::Lead, {0, 0}}, {core::AtomType::Lead, {1, 0}}},
                     {{1, {0, 0}, {1, 0}}}}};
        if (branched) {
            p.inputs[0].atoms.push_back({core::AtomType::Lead, {0, 1}});
            p.inputs[0].bonds.push_back({1, {0, 0}, {0, 1}});
        }
        p.outputs = {{{{core::AtomType::Tin, {0, 0}}}, {}}, {{{core::AtomType::Lead, {0, 0}}}, {}}};
        WritePuzzle(puzzle, p);
        std::string failures;
        auto candidate = solvers::CompileAcceptedSupplyInventory(
            p,
            [&](const auto& s) {
                io::BinaryWriter::WriteSolutionFile(solution, s);
                return io::VerifySolutionFiles(puzzle, solution, 5000, 2000).Passed();
            },
            [&](const auto& failure) { failures += failure.detail + "\n"; });
        ASSERT_TRUE(candidate) << failures;
        io::BinaryWriter::WriteSolutionFile(solution, *candidate);
        const auto report = io::VerifySolutionFiles(puzzle, solution, 5000, 2000);
        ASSERT_TRUE(report.Passed());
        candidate->metrics = report.metrics;
        io::BinaryWriter::WriteSolutionFile(solution, *candidate);
        EXPECT_TRUE(io::VerifySolutionFiles(puzzle, solution, 5000, 2000).Passed());
    }
}

TEST_F(DisassemblyVerifierTest, AutomaticCalcificationPreservesJointResidualStock) {
    io::PuzzleData p;
    p.version = 3;
    p.name = "joint-calcification";
    p.outputMultiplier = 1;
    p.permissionBits = (1ULL << 0) | (1ULL << 2) | (1ULL << 3) | (1ULL << 8) | (1ULL << 9) |
                       (1ULL << 12) | (1ULL << 22) | (1ULL << 23);
    p.inputs = {{{{core::AtomType::Air, {2, -3}}, {core::AtomType::Water, {3, -3}}},
                 {{1, {2, -3}, {3, -3}}}}};
    p.outputs = {{{{core::AtomType::Salt, {-4, 2}}}, {}}, {{{core::AtomType::Water, {3, -2}}}, {}}};
    WritePuzzle(puzzle, p);
    std::string failures;
    auto candidate = solvers::CompileAcceptedSupplyInventory(
        p,
        [&](const auto& s) {
            io::BinaryWriter::WriteSolutionFile(solution, s);
            return io::VerifySolutionFiles(puzzle, solution, 5000, 2000).Passed();
        },
        [&](const auto& failure) { failures += failure.detail + "\n"; });
    ASSERT_TRUE(candidate) << failures;
    io::BinaryWriter::WriteSolutionFile(solution, *candidate);
    const auto report = io::VerifySolutionFiles(puzzle, solution, 5000, 2000);
    ASSERT_TRUE(report.Passed());
    candidate->metrics = report.metrics;
    io::BinaryWriter::WriteSolutionFile(solution, *candidate);
    EXPECT_TRUE(io::VerifySolutionFiles(puzzle, solution, 5000, 2000).Passed());
}

TEST_F(DisassemblyVerifierTest, AutomaticSkipsInaccessibleOptionalFeed) {
    io::PuzzleData p;
    p.version = 3;
    p.name = "optional-feed-replanning";
    p.outputMultiplier = 1;
    p.permissionBits = (1ULL << 0) | (1ULL << 3) | (1ULL << 22) | (1ULL << 23);
    p.inputs = {
        {{{core::AtomType::Salt, {0, 0}}, {core::AtomType::Salt, {1, 0}}}, {{1, {0, 0}, {1, 0}}}},
        {{{core::AtomType::Salt, {2, -3}}}, {}}};
    p.outputs = {{{{core::AtomType::Salt, {0, 0}}}, {}}};
    WritePuzzle(puzzle, p);
    std::string failures;
    auto candidate = solvers::CompileAcceptedSupplyInventory(
        p,
        [&](const auto& s) {
            io::BinaryWriter::WriteSolutionFile(solution, s);
            return io::VerifySolutionFiles(puzzle, solution, 5000, 2000).Passed();
        },
        [&](const auto& failure) { failures += failure.detail + "\n"; });
    ASSERT_TRUE(candidate) << failures;
    std::size_t inputs = 0;
    for (const auto& object : candidate->objects)
        if (object.typeName == "input") {
            ++inputs;
            EXPECT_EQ(object.bodyData.at(2), 1);
        }
    EXPECT_EQ(inputs, 1U);
    io::BinaryWriter::WriteSolutionFile(solution, *candidate);
    const auto report = io::VerifySolutionFiles(puzzle, solution, 5000, 2000);
    ASSERT_TRUE(report.Passed());
    candidate->metrics = report.metrics;
    io::BinaryWriter::WriteSolutionFile(solution, *candidate);
    EXPECT_TRUE(io::VerifySolutionFiles(puzzle, solution, 5000, 2000).Passed());
}

TEST_F(DisassemblyVerifierTest, AutomaticSkipsOverwideOptionalFeed) {
    io::PuzzleData p;
    p.version = 3;
    p.name = "optional-wide-feed-replanning";
    p.outputMultiplier = 1;
    p.permissionBits = (1ULL << 0) | (1ULL << 3) | (1ULL << 22) | (1ULL << 23);
    p.inputs = {{{{core::AtomType::Salt, {0, 0}}, {core::AtomType::Salt, {100, 0}}}, {}},
                {{{core::AtomType::Salt, {0, 0}}}, {}}};
    p.outputs = {{{{core::AtomType::Salt, {0, 0}}}, {}}};
    WritePuzzle(puzzle, p);
    std::string failures;
    auto candidate = solvers::CompileAcceptedSupplyInventory(
        p,
        [&](const auto& s) {
            io::BinaryWriter::WriteSolutionFile(solution, s);
            return io::VerifySolutionFiles(puzzle, solution, 5000, 2000).Passed();
        },
        [&](const auto& failure) { failures += failure.detail + "\n"; });
    ASSERT_TRUE(candidate) << failures;
    std::size_t inputs = 0;
    for (const auto& object : candidate->objects)
        if (object.typeName == "input") {
            ++inputs;
            EXPECT_EQ(object.bodyData.at(2), 1);
        }
    EXPECT_EQ(inputs, 1U);
    io::BinaryWriter::WriteSolutionFile(solution, *candidate);
    const auto report = io::VerifySolutionFiles(puzzle, solution, 5000, 2000);
    ASSERT_TRUE(report.Passed());
    candidate->metrics = report.metrics;
    io::BinaryWriter::WriteSolutionFile(solution, *candidate);
    EXPECT_TRUE(io::VerifySolutionFiles(puzzle, solution, 5000, 2000).Passed());
}

TEST_F(DisassemblyVerifierTest, AutomaticJointCoproductOutputs) {
    io::PuzzleData p;
    p.version = 3;
    p.name = "automatic-joint-coproduct";
    p.outputMultiplier = 1;
    p.permissionBits = (1ULL << 0) | (1ULL << 2) | (1ULL << 3) | (1ULL << 8) | (1ULL << 9) |
                       (1ULL << 22) | (1ULL << 23);
    p.inputs = {
        {{{core::AtomType::Salt, {0, 0}}, {core::AtomType::Air, {1, 0}}}, {{1, {0, 0}, {1, 0}}}}};
    p.outputs = {{{{core::AtomType::Salt, {0, 0}}}, {}}, {{{core::AtomType::Air, {0, 0}}}, {}}};
    WritePuzzle(puzzle, p);
    auto candidate = solvers::CompileAcceptedSupplyInventory(p, [&](const auto& s) {
        io::BinaryWriter::WriteSolutionFile(solution, s);
        return io::VerifySolutionFiles(puzzle, solution, 5000, 2000).Passed();
    });
    ASSERT_TRUE(candidate);
    const auto integrated = solvers::AssemblySolver{}.SolveAccepted(p, [&](const auto& s) {
        if (s.solutionName != "Automatic supply inventory") return false;
        io::BinaryWriter::WriteSolutionFile(solution, s);
        return io::VerifySolutionFiles(puzzle, solution, 5000, 2000).Passed();
    });
    EXPECT_EQ(integrated.solutionName, "Automatic supply inventory");
    io::BinaryWriter::WriteSolutionFile(solution, integrated);
    const auto report = io::VerifySolutionFiles(puzzle, solution, 5000, 2000);
    ASSERT_TRUE(report.Passed());
    candidate = integrated;
    candidate->metrics = report.metrics;
    io::BinaryWriter::WriteSolutionFile(solution, *candidate);
    EXPECT_TRUE(io::VerifySolutionFiles(puzzle, solution, 5000, 2000).Passed());
}

TEST_F(DisassemblyVerifierTest, AutomaticBondedResidualInventory) {
    io::PuzzleData p;
    p.version = 3;
    p.name = "automatic-bonded-residual";
    p.outputMultiplier = 1;
    p.permissionBits = (1ULL << 0) | (1ULL << 2) | (1ULL << 3) | (1ULL << 8) | (1ULL << 9) |
                       (1ULL << 22) | (1ULL << 23);
    p.inputs = {{{{core::AtomType::Salt, {2, -3}}, {core::AtomType::Air, {3, -3}}},
                 {{1, {2, -3}, {3, -3}}}}};
    p.outputs = {{{{core::AtomType::Salt, {-4, 2}}, {core::AtomType::Air, {-3, 2}}},
                  {{1, {-4, 2}, {-3, 2}}}}};
    WritePuzzle(puzzle, p);
    auto candidate = solvers::CompileAcceptedSupplyInventory(p, [&](const auto& s) {
        io::BinaryWriter::WriteSolutionFile(solution, s);
        return io::VerifySolutionFiles(puzzle, solution, 5000, 2000).Passed();
    });
    ASSERT_TRUE(candidate);
    io::BinaryWriter::WriteSolutionFile(solution, *candidate);
    const auto report = io::VerifySolutionFiles(puzzle, solution, 5000, 2000);
    ASSERT_TRUE(report.Passed());
    candidate->metrics = report.metrics;
    io::BinaryWriter::WriteSolutionFile(solution, *candidate);
    EXPECT_TRUE(io::VerifySolutionFiles(puzzle, solution, 5000, 2000).Passed());
}
TEST_F(DisassemblyVerifierTest, AutomaticConnectedResidualStock) {
    io::PuzzleData p;
    p.version = 3;
    p.name = "automatic-connected-residual";
    p.outputMultiplier = 1;
    p.permissionBits = (1ULL << 0) | (1ULL << 2) | (1ULL << 3) | (1ULL << 8) | (1ULL << 9) |
                       (1ULL << 22) | (1ULL << 23);
    p.inputs = {{{{core::AtomType::Salt, {0, 0}},
                  {core::AtomType::Salt, {1, 0}},
                  {core::AtomType::Salt, {2, 0}}},
                 {{1, {0, 0}, {1, 0}}, {1, {1, 0}, {2, 0}}}}};
    p.outputs = {{{{core::AtomType::Salt, {0, 0}}}, {}}};
    const auto linearFeed = p.inputs[0];
    for (bool bent : {false, true}) {
        p.inputs[0] = linearFeed;
        if (bent) {
            p.inputs[0].atoms[2].position = {1, 1};
            p.inputs[0].bonds[1].to = {1, 1};
        }
        const auto feed = p.inputs[0];
        SCOPED_TRACE(bent);
        for (int rotation = 0; rotation < 6; ++rotation) {
            SCOPED_TRACE(rotation);
            p.inputs[0] = feed;
            const auto transform = [&](core::HexCoord cell) {
                for (int turn = 0; turn < rotation; ++turn)
                    cell = core::RotateClockwise(cell);
                return core::Add(cell, {2, -3});
            };
            for (auto& atom : p.inputs[0].atoms)
                atom.position = transform(atom.position);
            for (auto& bond : p.inputs[0].bonds) {
                bond.from = transform(bond.from);
                bond.to = transform(bond.to);
            }
            ASSERT_EQ(solvers::AssemblyPlanner::PlanSupply(p).productsPerPeriod, 3u);
            WritePuzzle(puzzle, p);
            std::vector<solvers::SupplyFailure> failures;
            auto candidate = solvers::CompileAcceptedSupplyInventory(
                p,
                [&](const auto& s) {
                    io::BinaryWriter::WriteSolutionFile(solution, s);
                    return io::VerifySolutionFiles(puzzle, solution, 5000, 2000).Passed();
                },
                [&](const auto& failure) { failures.push_back(failure); });
            std::string failureDetails;
            for (const auto& failure : failures)
                failureDetails += failure.detail + "\n";
            ASSERT_TRUE(candidate) << failureDetails;
            EXPECT_TRUE(failures.empty());
            io::BinaryWriter::WriteSolutionFile(solution, *candidate);
            const auto report = io::VerifySolutionFiles(puzzle, solution, 5000, 2000);
            ASSERT_TRUE(report.Passed());
            candidate->metrics = report.metrics;
            io::BinaryWriter::WriteSolutionFile(solution, *candidate);
            EXPECT_TRUE(io::VerifySolutionFiles(puzzle, solution, 5000, 2000).Passed());
        }
    }
}

TEST_F(DisassemblyVerifierTest, AutomaticThreeRowFeedWithResidualDisposal) {
    io::PuzzleData p;
    p.version = 3;
    p.name = "automatic-three-row-feed";
    p.outputMultiplier = 1;
    p.permissionBits = (1ULL << 0) | (1ULL << 2) | (1ULL << 3) | (1ULL << 8) | (1ULL << 9) |
                       (1ULL << 17) | (1ULL << 22) | (1ULL << 23);
    p.inputs = {{{{core::AtomType::Salt, {0, 0}},
                  {core::AtomType::Fire, {1, 0}},
                  {core::AtomType::Fire, {0, 1}},
                  {core::AtomType::Fire, {-1, 1}},
                  {core::AtomType::Fire, {-1, 0}},
                  {core::AtomType::Fire, {0, -1}},
                  {core::AtomType::Fire, {1, -1}}},
                 {{1, {0, 0}, {1, 0}},
                  {1, {0, 0}, {0, 1}},
                  {1, {0, 0}, {-1, 1}},
                  {1, {0, 0}, {-1, 0}},
                  {1, {0, 0}, {0, -1}},
                  {1, {0, 0}, {1, -1}}}}};
    p.outputs = {{{{core::AtomType::Salt, {0, 0}}}, {}}};
    ASSERT_EQ(solvers::AssemblyPlanner::PlanSupply(p).productsPerPeriod, 1u);
    WritePuzzle(puzzle, p);
    std::vector<solvers::SupplyFailure> failures;
    const auto candidate = solvers::CompileAcceptedSupplyInventory(
        p,
        [&](const auto& s) {
            io::BinaryWriter::WriteSolutionFile(solution, s);
            return io::VerifySolutionFiles(puzzle, solution, 5000, 2000).Passed();
        },
        [&](const auto& failure) { failures.push_back(failure); });
    std::string details;
    for (const auto& failure : failures)
        details += failure.detail + "\n";
    ASSERT_TRUE(candidate) << details;
    io::BinaryWriter::WriteSolutionFile(solution, *candidate);
    const auto report = io::VerifySolutionFiles(puzzle, solution, 5000, 2000);
    ASSERT_TRUE(report.Passed());
    auto measured = *candidate;
    measured.metrics = report.metrics;
    io::BinaryWriter::WriteSolutionFile(solution, measured);
    EXPECT_TRUE(io::VerifySolutionFiles(puzzle, solution, 5000, 2000).Passed());
}

TEST_F(DisassemblyVerifierTest, AutomaticMultiProductResidualPeriod) {
    io::PuzzleData p;
    p.version = 3;
    p.name = "automatic-period";
    p.outputMultiplier = 1;
    p.permissionBits = (1ULL << 0) | (1ULL << 2) | (1ULL << 3) | (1ULL << 8) | (1ULL << 9) |
                       (1ULL << 22) | (1ULL << 23);
    p.inputs = {
        {{{core::AtomType::Salt, {0, 0}}, {core::AtomType::Salt, {1, 0}}}, {{1, {0, 0}, {1, 0}}}}};
    p.outputs = {{{{core::AtomType::Salt, {0, 0}}}, {}}};
    ASSERT_EQ(solvers::AssemblyPlanner::PlanSupply(p).productsPerPeriod, 2u);
    WritePuzzle(puzzle, p);
    auto candidate = solvers::CompileAcceptedSupplyInventory(p, [&](const auto& s) {
        io::BinaryWriter::WriteSolutionFile(solution, s);
        return io::VerifySolutionFiles(puzzle, solution, 5000, 2000).Passed();
    });
    ASSERT_TRUE(candidate);
    io::BinaryWriter::WriteSolutionFile(solution, *candidate);
    const auto report = io::VerifySolutionFiles(puzzle, solution, 5000, 2000);
    ASSERT_TRUE(report.Passed());
    candidate->metrics = report.metrics;
    io::BinaryWriter::WriteSolutionFile(solution, *candidate);
    EXPECT_TRUE(io::VerifySolutionFiles(puzzle, solution, 5000, 2000).Passed());
}
TEST_F(DisassemblyVerifierTest, AutomaticFourProductResidualPeriod) {
    io::PuzzleData p;
    p.version = 3;
    p.name = "automatic-four-product-period";
    p.outputMultiplier = 2;
    p.permissionBits = (1ULL << 0) | (1ULL << 2) | (1ULL << 3) | (1ULL << 8) | (1ULL << 9) |
                       (1ULL << 22) | (1ULL << 23);
    for (int q = 0; q < 4; ++q) {
        if (p.inputs.empty()) p.inputs.emplace_back();
        p.inputs[0].atoms.push_back({core::AtomType::Salt, {q, 0}});
        if (q) p.inputs[0].bonds.push_back({1, {q - 1, 0}, {q, 0}});
    }
    p.outputs = {{{{core::AtomType::Salt, {0, 0}}}, {}}};
    ASSERT_EQ(solvers::AssemblyPlanner::PlanSupply(p).productsPerPeriod, 4u);
    WritePuzzle(puzzle, p);
    auto candidate = solvers::CompileAcceptedSupplyInventory(p, [&](const auto& s) {
        io::BinaryWriter::WriteSolutionFile(solution, s);
        return io::VerifySolutionFiles(puzzle, solution, 5000, 2000).Passed();
    });
    ASSERT_TRUE(candidate);
    io::BinaryWriter::WriteSolutionFile(solution, *candidate);
    const auto report = io::VerifySolutionFiles(puzzle, solution, 5000, 2000);
    ASSERT_TRUE(report.Passed());
    candidate->metrics = report.metrics;
    io::BinaryWriter::WriteSolutionFile(solution, *candidate);
    EXPECT_TRUE(io::VerifySolutionFiles(puzzle, solution, 5000, 2000).Passed());
}

TEST_F(DisassemblyVerifierTest, AutomaticFourPeriodJointOutputs) {
    io::PuzzleData p;
    p.version = 3;
    p.name = "automatic-four-period-joint";
    p.outputMultiplier = 2;
    p.permissionBits = (1ULL << 0) | (1ULL << 2) | (1ULL << 3) | (1ULL << 8) | (1ULL << 9) |
                       (1ULL << 22) | (1ULL << 23);
    p.inputs.emplace_back();
    for (int q = 0; q < 8; ++q) {
        p.inputs[0].atoms.push_back({core::AtomType::Salt, {q, 0}});
        if (q) p.inputs[0].bonds.push_back({1, {q - 1, 0}, {q, 0}});
    }
    p.outputs = {{{{core::AtomType::Salt, {0, 0}}}, {}}, {{{core::AtomType::Salt, {0, 0}}}, {}}};
    WritePuzzle(puzzle, p);
    auto candidate = solvers::CompileAcceptedSupplyInventory(p, [&](const auto& s) {
        io::BinaryWriter::WriteSolutionFile(solution, s);
        return io::VerifySolutionFiles(puzzle, solution, 10000, 2000).Passed();
    });
    ASSERT_TRUE(candidate);
    io::BinaryWriter::WriteSolutionFile(solution, *candidate);
    const auto report = io::VerifySolutionFiles(puzzle, solution, 10000, 2000);
    ASSERT_TRUE(report.Passed());
    candidate->metrics = report.metrics;
    io::BinaryWriter::WriteSolutionFile(solution, *candidate);
    EXPECT_TRUE(io::VerifySolutionFiles(puzzle, solution, 10000, 2000).Passed());
}

TEST_F(DisassemblyVerifierTest, AutomaticSingleChamberMaterialIR) {
    io::PuzzleData p;
    p.version = 3;
    p.name = "automatic-chamber-ir";
    p.outputMultiplier = 1;
    p.permissionBits = (1ULL << 0) | (1ULL << 3) | (1ULL << 8) | (1ULL << 22) | (1ULL << 23);
    p.inputs = {{{{core::AtomType::Salt, {0, 0}}}, {}}};
    p.outputs = p.inputs;
    p.productionInfo.emplace();
    p.productionInfo->chambers.push_back({{7, -8}, "Large"});
    WritePuzzle(puzzle, p);
    auto candidate = solvers::CompileAcceptedSupplyInventory(p, [&](const auto& s) {
        io::BinaryWriter::WriteSolutionFile(solution, s);
        return io::VerifySolutionFiles(puzzle, solution, 5000, 2000).Passed();
    });
    ASSERT_TRUE(candidate);
    io::BinaryWriter::WriteSolutionFile(solution, *candidate);
    const auto report = io::VerifySolutionFiles(puzzle, solution, 5000, 2000);
    ASSERT_TRUE(report.Passed());
    candidate->metrics = report.metrics;
    io::BinaryWriter::WriteSolutionFile(solution, *candidate);
    EXPECT_TRUE(io::VerifySolutionFiles(puzzle, solution, 5000, 2000).Passed());
    const auto integrated = solvers::AssemblySolver{}.SolveAccepted(p, [&](const auto& s) {
        if (s.solutionName != "Automatic chamber supply inventory") return false;
        io::BinaryWriter::WriteSolutionFile(solution, s);
        return io::VerifySolutionFiles(puzzle, solution, 5000, 2000).Passed();
    });
    EXPECT_EQ(integrated.solutionName, "Automatic chamber supply inventory");
    auto cramped = p;
    cramped.productionInfo->chambers[0].chamberType = "Small";
    EXPECT_FALSE(
        solvers::CompileAcceptedSupplyInventory(cramped, [](const auto&) { return true; }));
}
TEST_F(DisassemblyVerifierTest, AutomaticMultipleChamberMaterialIR) {
    io::PuzzleData p;
    p.version = 3;
    p.name = "automatic-multiple-chamber-ir";
    p.outputMultiplier = 1;
    p.permissionBits = (1ULL << 0) | (1ULL << 3) | (1ULL << 8) | (1ULL << 22) | (1ULL << 23);
    p.inputs = {{{{core::AtomType::Salt, {0, 0}}}, {}}};
    p.outputs = p.inputs;
    p.productionInfo.emplace();
    p.productionInfo->chambers.push_back({{0, 0}, "Small"});
    p.productionInfo->chambers.push_back({{24, -8}, "Large"});
    WritePuzzle(puzzle, p);
    int rejected = 0;
    auto candidate = solvers::CompileAcceptedSupplyInventory(p, [&](const auto& s) {
        if (rejected++ == 0) return false;
        io::BinaryWriter::WriteSolutionFile(solution, s);
        return io::VerifySolutionFiles(puzzle, solution, 5000, 2000).Passed();
    });
    ASSERT_TRUE(candidate);
    EXPECT_GT(rejected, 1);
    io::BinaryWriter::WriteSolutionFile(solution, *candidate);
    const auto report = io::VerifySolutionFiles(puzzle, solution, 5000, 2000);
    ASSERT_TRUE(report.Passed());
    candidate->metrics = report.metrics;
    io::BinaryWriter::WriteSolutionFile(solution, *candidate);
    EXPECT_TRUE(io::VerifySolutionFiles(puzzle, solution, 5000, 2000).Passed());
}
TEST_F(DisassemblyVerifierTest, AutomaticColoredFeedWholeMaskExtraction) {
    for (const auto mask : {2, 6, 14}) {
        io::PuzzleData p;
        p.version = 3;
        p.name = "automatic-colored-feed";
        p.outputMultiplier = 1;
        p.permissionBits = (1ULL << 0) | (1ULL << 2) | (1ULL << 3) | (1ULL << 8) | (1ULL << 9) |
                           (1ULL << 22) | (1ULL << 23);
        p.inputs = {{{{core::AtomType::Salt, {0, 0}}, {core::AtomType::Air, {1, 0}}},
                     {{static_cast<std::uint8_t>(mask), {0, 0}, {1, 0}}}}};
        p.outputs = {{{{core::AtomType::Salt, {0, 0}}, {core::AtomType::Air, {1, 0}}},
                      {{1, {0, 0}, {1, 0}}}}};
        WritePuzzle(puzzle, p);
        auto candidate = solvers::CompileAcceptedSupplyInventory(p, [&](const auto& s) {
            io::BinaryWriter::WriteSolutionFile(solution, s);
            return io::VerifySolutionFiles(puzzle, solution, 5000, 2000).Passed();
        });
        ASSERT_TRUE(candidate) << "mask=" << mask;
        io::BinaryWriter::WriteSolutionFile(solution, *candidate);
        const auto report = io::VerifySolutionFiles(puzzle, solution, 5000, 2000);
        ASSERT_TRUE(report.Passed());
        candidate->metrics = report.metrics;
        io::BinaryWriter::WriteSolutionFile(solution, *candidate);
        EXPECT_TRUE(io::VerifySolutionFiles(puzzle, solution, 5000, 2000).Passed());
    }
}
TEST_F(DisassemblyVerifierTest, AutomaticSupplySkipsOversizedOptionalFeed) {
    io::PuzzleData p;
    p.version = 3;
    p.name = "optional-large-feed";
    p.outputMultiplier = 1;
    p.permissionBits = (1ULL << 0) | (1ULL << 2) | (1ULL << 3) | (1ULL << 8) | (1ULL << 9) |
                       (1ULL << 22) | (1ULL << 23);
    p.inputs.resize(1);
    for (int i = 0; i < 17; ++i) {
        p.inputs[0].atoms.push_back({core::AtomType::Air, {i, 0}});
        if (i) p.inputs[0].bonds.push_back({1, {i - 1, 0}, {i, 0}});
    }
    p.inputs.push_back({{{core::AtomType::Salt, {0, 0}}}, {}});
    p.outputs = {{{{core::AtomType::Salt, {0, 0}}}, {}}};
    WritePuzzle(puzzle, p);
    auto candidate = solvers::CompileAcceptedSupplyInventory(p, [&](const auto& s) {
        io::BinaryWriter::WriteSolutionFile(solution, s);
        return io::VerifySolutionFiles(puzzle, solution, 5000, 2000).Passed();
    });
    ASSERT_TRUE(candidate);
    io::BinaryWriter::WriteSolutionFile(solution, *candidate);
    const auto report = io::VerifySolutionFiles(puzzle, solution, 5000, 2000);
    ASSERT_TRUE(report.Passed());
    candidate->metrics = report.metrics;
    io::BinaryWriter::WriteSolutionFile(solution, *candidate);
    EXPECT_TRUE(io::VerifySolutionFiles(puzzle, solution, 5000, 2000).Passed());
    p.outputs.push_back(p.outputs.front());
    WritePuzzle(puzzle, p);
    candidate = solvers::CompileAcceptedSupplyInventory(p, [&](const auto& s) {
        io::BinaryWriter::WriteSolutionFile(solution, s);
        return io::VerifySolutionFiles(puzzle, solution, 5000, 2000).Passed();
    });
    ASSERT_TRUE(candidate);
    io::BinaryWriter::WriteSolutionFile(solution, *candidate);
    const auto jointReport = io::VerifySolutionFiles(puzzle, solution, 5000, 2000);
    ASSERT_TRUE(jointReport.Passed());
    candidate->metrics = jointReport.metrics;
    io::BinaryWriter::WriteSolutionFile(solution, *candidate);
    EXPECT_TRUE(io::VerifySolutionFiles(puzzle, solution, 5000, 2000).Passed());
    auto malformed = p;
    malformed.inputs[0].bonds[0].to = {30, 0};
    EXPECT_THROW(
        (void)solvers::CompileAcceptedSupplyInventory(malformed, [](const auto&) { return true; }),
        std::invalid_argument);
    p.outputs.resize(1);
    p.inputs.pop_back();
    EXPECT_THROW((void)solvers::CompileAcceptedSupplyInventory(p, [](const auto&) { return true; }),
                 std::invalid_argument);
}
TEST_F(DisassemblyVerifierTest, AutomaticSupplyIndependentAcceptanceAndFrames) {
    for (int shape = 0; shape < 4; ++shape) {
        io::PuzzleData p;
        p.version = 3;
        p.name = "automatic-supply";
        p.outputMultiplier = 1;
        p.permissionBits =
            (1ULL << 0) | (1ULL << 2) | (1ULL << 3) | (1ULL << 8) | (1ULL << 22) | (1ULL << 23);
        p.inputs = {{{{core::AtomType::Salt, {2, -3}}}, {}}};
        p.outputs = {{{{core::AtomType::Salt, {-4, 2}}}, {}}};
        if (shape) {
            p.outputs[0].atoms.push_back({core::AtomType::Salt, {-3, 2}});
            p.outputs[0].bonds.push_back({1, {-4, 2}, {-3, 2}});
        }
        if (shape >= 2) {
            p.outputs[0].atoms.push_back({core::AtomType::Salt, {-3, 1}});
            p.outputs[0].bonds.push_back({1, {-3, 2}, {-3, 1}});
        }
        if (shape == 3) {
            p.inputs.push_back({{{core::AtomType::Air, {-2, 4}}}, {}});
            p.outputs[0].atoms[1].type = core::AtomType::Air;
        }
        WritePuzzle(puzzle, p);
        int attempts = 0;
        const auto accept = [&](const io::SolutionData& s) {
            if (s.solutionName != "Automatic supply inventory" || ++attempts <= 2) return false;
            io::BinaryWriter::WriteSolutionFile(solution, s);
            return io::VerifySolutionFiles(puzzle, solution, 5000, 2000).Passed();
        };
        auto candidate =
            shape == 3 ? std::optional<io::SolutionData>{solvers::AssemblySolver{}.SolveAccepted(
                             p, accept)}
                       : solvers::CompileAcceptedSupplyInventory(p, accept);
        ASSERT_TRUE(candidate) << "shape=" << shape << " attempts=" << attempts;
        io::BinaryWriter::WriteSolutionFile(solution, *candidate);
        EXPECT_TRUE(io::VerifySolutionFiles(puzzle, solution, 5000, 2000).Passed());
        EXPECT_GE(attempts, 2);
    }
}
#endif
} // namespace
