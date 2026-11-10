#include "AssemblySolver.hpp"
#include "Core/LogManager.hpp"
#include "DemandDrivenChain.hpp"
#include "IO/BinaryWriter.hpp"
#include "IO/CalcifyAnimismusAdmission.hpp"
#include "IO/CandidateArchive.hpp"
#include "IO/SolutionVerifier.hpp"
#include "RepoRoot.hpp"
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <random>
#include <spdlog/sinks/ostream_sink.h>
#include <sstream>
namespace opusoptima::solvers { namespace {
const auto root = opusoptima::test::RepoRoot(__FILE__);
io::PuzzleData PartitionFixture() {
    auto p = io::BinaryParser::ParsePuzzleFile(
        root / "asset/puzzle/campaign/ch1-and-prologue/P014.puzzle");
    io::Molecule dimer, monomer;
    dimer.atoms = {{core::AtomType::Salt, {0, 0}}, {core::AtomType::Salt, {1, 0}}};
    dimer.bonds = {{1, {0, 0}, {1, 0}}};
    monomer.atoms = {{core::AtomType::Salt, {0, 0}}};
    auto trimer = dimer;
    trimer.atoms.push_back({core::AtomType::Salt, {2, 0}});
    trimer.bonds.push_back({1, {1, 0}, {2, 0}});
    p.inputs = {trimer, dimer};
    p.outputs = {monomer, monomer, dimer, dimer};
    p.permissionBits = 1ULL | (1ULL << 9) | (1ULL << 25);
    return p;
}
io::PuzzleData StaticCopyFixture() {
    auto p = PartitionFixture();
    io::Molecule graph;
    graph.atoms = {{core::AtomType::Gold, {13, -9}},
                   {core::AtomType::Salt, {13, -10}},
                   {core::AtomType::Water, {14, -10}}};
    graph.bonds = {{1, {13, -9}, {13, -10}}, {1, {13, -9}, {14, -10}}};
    auto target = graph;
    auto transform = [](core::HexCoord c) { return core::Add(core::RotateClockwise(c), {-7, 5}); };
    for (auto& a : target.atoms)
        a.position = transform(a.position);
    for (auto& b : target.bonds) {
        b.from = transform(b.from);
        b.to = transform(b.to);
    }
    std::reverse(graph.atoms.begin(), graph.atoms.end());
    p.inputs.assign(9, p.outputs[0]);
    p.inputs[7] = graph;
    p.outputs.clear();
    for (auto type : {core::AtomType::Air, core::AtomType::Earth, core::AtomType::Fire,
                      core::AtomType::Water}) {
        io::Molecule singleton;
        singleton.atoms = {{type, {0, 0}}};
        p.inputs[p.outputs.size() + 1] = singleton;
        p.outputs.push_back(singleton);
    }
    p.outputs.insert(p.outputs.begin() + 2, target);
    p.permissionBits = 1ULL | (1ULL << 25);
    return p;
}
void ExpectStaticCopyPoses(const io::PuzzleData& p, const io::SolutionData& candidate) {
    using Cell = std::pair<int, int>;
    std::set<int> ports;
    auto pose = [](core::HexCoord cell, const auto& object) {
        for (int r = 0; r < object.bodyData[1]; ++r)
            cell = core::RotateClockwise(cell);
        return core::Add(cell, object.position);
    };
    for (const auto& output : candidate.objects)
        if (output.typeName == "out-std") {
            const int out = output.bodyData[2];
            ASSERT_GE(out, 0);
            ASSERT_LT(out, p.outputs.size());
            EXPECT_TRUE(ports.insert(out).second);
            std::set<std::tuple<int, int, int>> wanted, actual;
            std::set<std::pair<Cell, Cell>> wantedEdges, actualEdges;
            auto cells = [&](const auto& molecule, const auto& object, auto& atoms, auto& edges) {
                for (const auto& a : molecule.atoms) {
                    const auto c = pose(a.position, object);
                    atoms.emplace(static_cast<int>(a.type), c.q, c.r);
                }
                for (const auto& b : molecule.bonds) {
                    const auto a = pose(b.from, object), c = pose(b.to, object);
                    edges.insert(std::minmax(Cell{a.q, a.r}, Cell{c.q, c.r}));
                }
            };
            cells(p.outputs[out], output, wanted, wantedEdges);
            int matches = 0;
            for (const auto& input : candidate.objects)
                if (input.typeName == "input") {
                    const int in = input.bodyData[2];
                    ASSERT_GE(in, 0);
                    ASSERT_LT(in, p.inputs.size());
                    actual.clear();
                    actualEdges.clear();
                    cells(p.inputs[in], input, actual, actualEdges);
                    matches += actual == wanted && actualEdges == wantedEdges;
                }
            EXPECT_EQ(matches, 1) << out;
        }
    EXPECT_EQ(ports.size(), p.outputs.size());
}
TEST(RigidCarrierTest, StaticCopyExactGraphsIndicesAndBoundaries) {
    const auto p = StaticCopyFixture();
    ASSERT_EQ(PreflightRigidCarrier(p).status, RigidCarrierPreflight::Status::Unknown);
    auto compiled = CompileRigidCarrierWithFamily(p);
    EXPECT_EQ(compiled.family, RigidCarrierFamily::StaticCopy);
    ExpectStaticCopyPoses(p, compiled.solution);
    EXPECT_EQ(compiled.solution.objects.size(), 2 * p.outputs.size() + 1);
    auto reject = [](const auto& invalid, RigidCarrierReason reason) {
        try {
            (void)CompileRigidCarrier(invalid);
            FAIL() << "copy unexpectedly accepted";
        } catch (const RigidCarrierUnsupported& e) {
            EXPECT_EQ(e.reason(), reason);
        }
    };
    auto invalid = p;
    invalid.inputs[7].bonds.pop_back();
    reject(invalid, RigidCarrierReason::SourceGraph);
    invalid = p;
    invalid.inputs[7].bonds.back() = {1, {13, -10}, {14, -10}};
    invalid.permissionBits |= (1ULL << 8) | (1ULL << 9) | (1ULL << 12);
    EXPECT_EQ(PreflightRigidCarrier(invalid).status, RigidCarrierPreflight::Status::Unknown);
    reject(invalid, RigidCarrierReason::ExactCoverImpossible);
    invalid = p;
    invalid.outputs[2].atoms.pop_back();
    invalid.outputs[2].bonds.pop_back();
    reject(invalid, RigidCarrierReason::InputHeader);
    invalid = p;
    invalid.outputs[2].atoms[0].type = core::AtomType::Lead;
    reject(invalid, RigidCarrierReason::InputHeader);
    invalid = p;
    invalid.permissionBits = 0;
    reject(invalid, RigidCarrierReason::Permissions);
    invalid = p;
    invalid.permissionBits &= ~(1ULL << 25);
    reject(invalid, RigidCarrierReason::Permissions);
    invalid = p;
    invalid.outputMultiplier = 2;
    reject(invalid, RigidCarrierReason::InputHeader);
    auto outputOnly = p;
    outputOnly.inputs.resize(8);
    ExpectStaticCopyPoses(outputOnly, CompileRigidCarrier(outputOnly));
    auto inputOnly = p;
    inputOnly.outputs.resize(4);
    ExpectStaticCopyPoses(inputOnly, CompileRigidCarrier(inputOnly));
    invalid = p;
    invalid.inputs.resize(17, p.inputs[0]);
    reject(invalid, RigidCarrierReason::InputHeader);
    invalid = p;
    invalid.outputs.resize(9, p.outputs[0]);
    reject(invalid, RigidCarrierReason::InputHeader);
    auto boundary = p;
    boundary.inputs.resize(16, p.inputs[0]);
    boundary.outputs.resize(8, p.outputs[0]);
    ExpectStaticCopyPoses(boundary, CompileRigidCarrier(boundary));
    boundary.outputs.assign(8, p.outputs[2]);
    boundary.inputs.assign(16, p.inputs[7]);
    for (auto& input : boundary.inputs)
        input.bonds.back() = {1, {13, -10}, {14, -10}};
    EXPECT_EQ(PreflightRigidCarrier(boundary).status, RigidCarrierPreflight::Status::Unknown);
    // A failed copy returns Unsupported without entering the legacy cover search.
    reject(boundary, RigidCarrierReason::ExactCoverImpossible);
    try {
        (void)CompileRigidCarrier(p, std::chrono::steady_clock::now());
        FAIL() << "expired copy accepted";
    } catch (const RigidCarrierUnsupported& e) {
        EXPECT_EQ(e.reason(), RigidCarrierReason::Deadline);
    }
}
TEST(RigidCarrierTest, StaticPartitionGuardsAndCompleteSourceAccounting) {
    auto p = PartitionFixture();
    EXPECT_EQ(PreflightRigidCarrier(p).status, RigidCarrierPreflight::Status::Unknown);
    auto compiled = CompileRigidCarrierWithFamily(p);
    EXPECT_EQ(compiled.family, RigidCarrierFamily::StaticPartition);
    const auto count = [&](const char* name) {
        return std::count_if(compiled.solution.objects.begin(), compiled.solution.objects.end(),
                             [&](const auto& o) { return o.typeName == name; });
    };
    EXPECT_EQ(count("input"), 3);
    EXPECT_EQ(count("out-std"), 4);
    EXPECT_EQ(count("unbonder"), 1);
    EXPECT_EQ(count("bonder"), 0);
    auto rejected = [](const io::PuzzleData& puzzle, RigidCarrierReason reason) {
        try {
            (void)CompileRigidCarrier(puzzle);
            FAIL() << "partition unexpectedly accepted";
        } catch (const RigidCarrierUnsupported& error) {
            EXPECT_EQ(error.reason(), reason);
        }
    };
    auto invalid = p;
    invalid.permissionBits &= ~(1ULL << 9);
    rejected(invalid, RigidCarrierReason::Permissions);
    invalid = p;
    invalid.outputs.push_back(invalid.outputs[0]);
    rejected(invalid, RigidCarrierReason::InputHeader);
    invalid = p;
    invalid.outputMultiplier = 2;
    rejected(invalid, RigidCarrierReason::InputHeader);
    invalid = p;
    invalid.outputs[0].atoms[0].type = core::AtomType::Air;
    rejected(invalid, RigidCarrierReason::ExactCoverImpossible);
    invalid = p;
    invalid.inputs.resize(1);
    invalid.outputs.resize(2);
    rejected(invalid, RigidCarrierReason::ExactCoverImpossible);
    invalid = p;
    invalid.inputs = {p.inputs[1]};
    invalid.outputs = {p.outputs[0], p.outputs[0]};
    EXPECT_EQ(CompileRigidCarrierWithFamily(invalid).family, RigidCarrierFamily::StaticPartition);
    auto wrongEdge = p;
    wrongEdge.inputs = {{{{core::AtomType::Salt, {0, 0}},
                          {core::AtomType::Salt, {1, 0}},
                          {core::AtomType::Salt, {0, 1}},
                          {core::AtomType::Salt, {-1, 1}}},
                         {{1, {0, 0}, {1, 0}}, {1, {0, 0}, {0, 1}}, {1, {0, 1}, {-1, 1}}}}};
    auto triangle = wrongEdge.inputs[0];
    triangle.atoms.pop_back();
    triangle.bonds.pop_back();
    triangle.bonds.push_back({1, {1, 0}, {0, 1}});
    wrongEdge.outputs = {triangle, p.outputs[0]};
    wrongEdge.permissionBits |= 1ULL << 8;
    rejected(wrongEdge, RigidCarrierReason::ExactCoverImpossible);
    try {
        (void)CompileRigidCarrier(p, std::chrono::steady_clock::now());
        FAIL() << "expired deadline accepted";
    } catch (const RigidCarrierUnsupported& error) {
        EXPECT_EQ(error.reason(), RigidCarrierReason::Deadline);
    }
}
TEST(RigidCarrierTest, StaticPartitionChargesPoseAttemptsToNodeCap) {
    auto p = PartitionFixture();
    p.inputs.assign(8, p.inputs.back());
    p.outputs = {p.outputs[0], p.outputs[0], p.outputs[0], p.outputs[2]};
    EXPECT_EQ(PreflightRigidCarrier(p).status, RigidCarrierPreflight::Status::Unknown);
    try {
        (void)CompileRigidCarrier(p);
        FAIL() << "odd output total unexpectedly partitioned";
    } catch (const RigidCarrierUnsupported& error) {
        EXPECT_EQ(error.reason(), RigidCarrierReason::SearchNodeCap);
    }
}
TEST(RigidCarrierTest, StaticPartitionRequiresNativeAuthority) {
    for (const auto* stage :
         {"portfolio-static-molecule-partition", "portfolio-static-input-copy"}) {
        EXPECT_FALSE(io::CalcifyAnimismusModeAllowed(stage, io::VerificationMode::Independent));
        EXPECT_TRUE(io::CalcifyAnimismusModeAllowed(stage, io::VerificationMode::LegacyGate));
        EXPECT_TRUE(io::CalcifyAnimismusModeAllowed(stage, io::VerificationMode::Shadow));
        for (auto authority :
             {io::VerificationAuthority::None, io::VerificationAuthority::Independent,
              io::VerificationAuthority::LegacyGate}) {
            io::VerificationReport report;
            report.status = io::VerificationStatus::Passed;
            report.authority = authority;
            EXPECT_EQ(io::RequireCalcifyAnimismusAuthority(stage, report).Passed(),
                      authority == io::VerificationAuthority::LegacyGate);
        }
    }
}
TEST(RigidCarrierTest, StableFirstObservedReasons) {
    const auto base = io::BinaryParser::ParsePuzzleFile(
        root / "asset/puzzle/campaign/ch1-and-prologue/P014.puzzle");
    auto expect = [](const io::PuzzleData& p, RigidCarrierReason reason, auto deadline) {
        try {
            (void)CompileRigidCarrier(p, deadline);
            FAIL() << "expected Unsupported";
        } catch (const RigidCarrierUnsupported& e) {
            EXPECT_EQ(e.reason(), reason);
            EXPECT_EQ(std::string(e.what()),
                      std::string("rigid carrier Unsupported: ") + RigidCarrierReasonName(reason));
        }
    };
    const auto unlimited = std::chrono::steady_clock::time_point::max();
    expect(base, RigidCarrierReason::Deadline, std::chrono::steady_clock::now());
    auto p = base;
    p.version = 2;
    p.permissionBits = 0;
    expect(p, RigidCarrierReason::InputHeader, unlimited);
    p = base;
    p.permissionBits &= ~1ULL;
    expect(p, RigidCarrierReason::Permissions, unlimited);
    p = base;
    p.inputs[0].atoms.push_back(p.inputs[0].atoms[0]);
    expect(p, RigidCarrierReason::SourceGraph, unlimited);
    p = base;
    p.outputs[0].atoms.push_back(p.outputs[0].atoms[0]);
    expect(p, RigidCarrierReason::TargetGraph, unlimited);
    p = base;
    p.permissionBits &= ~(1ULL << 8);
    expect(p, RigidCarrierReason::Permissions, unlimited);
    p = base;
    p.inputs = {p.outputs[0]};
    for (auto& a : p.inputs[0].atoms)
        a.type = core::AtomType::Quicksilver;
    expect(p, RigidCarrierReason::ExactCoverImpossible, unlimited);
    p = base;
    p.inputs = {};
    for (const auto& a : p.outputs[0].atoms) {
        io::Molecule m;
        m.atoms.push_back({a.type, {0, 0}});
        p.inputs.push_back(m);
    }
    expect(p, RigidCarrierReason::NoMultiatomCover, unlimited);
}
TEST(RigidCarrierTest, SharedPreflightNecessaryConditions) {
    const auto base = io::BinaryParser::ParsePuzzleFile(
        root / "asset/puzzle/campaign/ch1-and-prologue/P014.puzzle");
    auto rejected = [](const io::PuzzleData& p, RigidCarrierReason reason) {
        const auto result = PreflightRigidCarrier(p);
        EXPECT_EQ(result.status, RigidCarrierPreflight::Status::Unsupported);
        EXPECT_EQ(result.reason, reason);
        EXPECT_THROW(CompileRigidCarrier(p), RigidCarrierUnsupported);
    };
    auto p = base;
    p.version = 2;
    rejected(p, RigidCarrierReason::InputHeader);
    p = base;
    p.outputMultiplier = 2;
    rejected(p, RigidCarrierReason::InputHeader);
    p = base;
    p.permissionBits &= ~1ULL;
    rejected(p, RigidCarrierReason::Permissions);
    p = base;
    p.inputs[0].atoms.push_back(p.inputs[0].atoms[0]);
    rejected(p, RigidCarrierReason::SourceGraph);
    p = base;
    p.inputs[0].bonds[0].from = {8, 8};
    rejected(p, RigidCarrierReason::SourceGraph);
    p = base;
    p.inputs[0].bonds[0].bits = 2;
    rejected(p, RigidCarrierReason::SourceGraph);
    p = base;
    p.inputs[0].bonds.clear();
    rejected(p, RigidCarrierReason::SourceGraph);
    p = base;
    p.inputs[0].atoms[1].position = {9, 0};
    rejected(p, RigidCarrierReason::SourceGraph);
    p = base;
    p.outputs[0].atoms.push_back(p.outputs[0].atoms[0]);
    rejected(p, RigidCarrierReason::TargetGraph);
    p = base;
    p.inputs.clear();
    for (const auto& a : p.outputs[0].atoms) {
        io::Molecule m;
        m.atoms.push_back({a.type, {0, 0}});
        p.inputs.push_back(m);
    }
    rejected(p, RigidCarrierReason::NoMultiatomCover);
    for (const auto* path :
         {"campaign/ch1-and-prologue/P014.puzzle", "workshop-versions/w2868328394.puzzle",
          "tourney-2021/OM2021_W9_00.puzzle"}) {
        p = io::BinaryParser::ParsePuzzleFile(root / "asset/puzzle" / path);
        EXPECT_EQ(PreflightRigidCarrier(p).status, RigidCarrierPreflight::Status::Unknown);
        EXPECT_FALSE(PreflightRigidCarrier(p).reason);
        EXPECT_NO_THROW(CompileRigidCarrier(p));
    }
    p = base;
    p.inputs = {p.outputs[0]};
    p.permissionBits &= ~((1ULL << 8) | (1ULL << 12));
    for (auto& a : p.inputs[0].atoms)
        a.position = core::Add(a.position, {3, -2});
    for (auto& b : p.inputs[0].bonds) {
        b.from = core::Add(b.from, {3, -2});
        b.to = core::Add(b.to, {3, -2});
    }
    EXPECT_EQ(PreflightRigidCarrier(p).status, RigidCarrierPreflight::Status::Unknown);
    EXPECT_NO_THROW(CompileRigidCarrier(p));
    for (auto& a : p.inputs[0].atoms)
        a.type = core::AtomType::Quicksilver;
    rejected(p, RigidCarrierReason::NoMultiatomCover);
    EXPECT_THROW(CompileRigidCarrier(p), RigidCarrierUnsupported);
    p = base;
    p.permissionBits &= ~(1ULL << 8);
    EXPECT_EQ(PreflightRigidCarrier(p).status, RigidCarrierPreflight::Status::Unknown);
    EXPECT_THROW(CompileRigidCarrier(p), RigidCarrierUnsupported);
    p = base;
    p.inputs = {p.outputs[0]};
    for (auto& a : p.inputs[0].atoms)
        a.type = core::AtomType::Air;
    for (auto& a : p.outputs[0].atoms)
        a.type = core::AtomType::Salt;
    p.permissionBits &= ~(1ULL << 12);
    rejected(p, RigidCarrierReason::NoMultiatomCover);
    EXPECT_THROW(CompileRigidCarrier(p), RigidCarrierUnsupported);
}
io::Molecule CarrierSnake(int count) {
    io::Molecule molecule;
    for (int i = 0; i < std::min(count, 16); ++i) {
        const int row = i / 4, column = i % 4;
        const core::HexCoord cell{row % 2 ? 3 - column : column, row};
        molecule.atoms.push_back({i == 0 ? core::AtomType::Air : core::AtomType::Salt, cell});
        if (i) molecule.bonds.push_back({1, molecule.atoms[i - 1].position, cell});
    }
    if (count == 17) {
        molecule.atoms.push_back({core::AtomType::Salt, {4, 0}});
        molecule.bonds.push_back({1, {3, 0}, {4, 0}});
    }
    return molecule;
}

TEST(RigidCarrierTest, SharedSixteenAtomLimitAndValidSeventeenAtomRefusal) {
    auto p = io::BinaryParser::ParsePuzzleFile(
        root / "asset/puzzle/campaign/ch1-and-prologue/P014.puzzle");
    p.permissionBits = 1ULL | (1ULL << 25);
    for (int count : {13, 14, 15, 16}) {
        p.inputs = {CarrierSnake(count)};
        p.outputs = p.inputs;
        EXPECT_EQ(PreflightRigidCarrier(p).status, RigidCarrierPreflight::Status::Unknown);
        EXPECT_NO_THROW((void)CompileRigidCarrier(p));
    }
    io::Molecule singleton;
    singleton.atoms = {{core::AtomType::Salt, {0, 0}}};
    for (bool sourceTooLarge : {false, true}) {
        p.inputs = {sourceTooLarge ? CarrierSnake(17) : singleton};
        p.outputs = {sourceTooLarge ? singleton : CarrierSnake(17)};
        const auto reason =
            sourceTooLarge ? RigidCarrierReason::SourceGraph : RigidCarrierReason::TargetGraph;
        const auto preflight = PreflightRigidCarrier(p);
        EXPECT_EQ(preflight.status, RigidCarrierPreflight::Status::Unsupported);
        EXPECT_EQ(preflight.reason, reason);
        try {
            (void)CompileRigidCarrier(p);
            FAIL() << "seventeen-atom graph accepted";
        } catch (const RigidCarrierUnsupported& error) {
            EXPECT_EQ(error.reason(), reason);
        }
    }
}

TEST(RigidCarrierTest, CutPermissionAndCrossOutputSeparation) {
    auto p = io::BinaryParser::ParsePuzzleFile(
        root / "asset/puzzle/campaign/ch1-and-prologue/P014.puzzle");
    p.inputs = {{{{core::AtomType::Salt, {0, 0}},
                  {core::AtomType::Air, {1, 0}},
                  {core::AtomType::Earth, {0, 1}}},
                 {{1, {0, 0}, {1, 0}}, {1, {0, 0}, {0, 1}}, {1, {1, 0}, {0, 1}}}}};
    p.outputs = p.inputs;
    p.outputs[0].bonds.pop_back();
    p.permissionBits &= ~((1ULL << 8) | (1ULL << 9));
    EXPECT_EQ(PreflightRigidCarrier(p).status, RigidCarrierPreflight::Status::Unsupported);
    EXPECT_THROW((void)CompileRigidCarrier(p), RigidCarrierUnsupported);
    p.permissionBits |= 1ULL << 9;
    EXPECT_EQ(PreflightRigidCarrier(p).status, RigidCarrierPreflight::Status::Unknown);
    EXPECT_NO_THROW((void)CompileRigidCarrier(p));
    p.outputs.push_back(p.outputs[0]);
    for (auto& atom : p.outputs[1].atoms)
        atom.position.q -= 40;
    for (auto& bond : p.outputs[1].bonds) {
        bond.from.q -= 40;
        bond.to.q -= 40;
    }
    EXPECT_THROW((void)CompileRigidCarrier(p), RigidCarrierUnsupported);
}

TEST(RigidCarrierTest, PlacementNecessaryConditionMatchesCompiler) {
    auto p = io::BinaryParser::ParsePuzzleFile(
        root / "asset/puzzle/campaign/ch1-and-prologue/P014.puzzle");
    p.outputs = {p.inputs[0]};
    p.permissionBits &= ~((1ULL << 8) | (1ULL << 12));
    const auto bonded = p.inputs[0];
    p.inputs.clear();
    for (const auto& atom : bonded.atoms) {
        io::Molecule singleton;
        singleton.atoms = {atom};
        p.inputs.push_back(singleton);
    }
    p.inputs.push_back(bonded);
    EXPECT_EQ(PreflightRigidCarrier(p).status, RigidCarrierPreflight::Status::Unknown);
    EXPECT_NO_THROW(CompileRigidCarrier(p));
    p.inputs = {bonded};
    for (int r = 0; r < 6; ++r) {
        auto q = p;
        auto& feed = q.inputs[0];
        std::reverse(feed.atoms.begin(), feed.atoms.end());
        auto transform = [&](core::HexCoord c) {
            for (int i = 0; i < r; ++i)
                c = core::RotateClockwise(c);
            return core::Add(c, core::HexCoord{4, -3});
        };
        for (auto& a : feed.atoms)
            a.position = transform(a.position);
        for (auto& b : feed.bonds) {
            b.from = transform(b.from);
            b.to = transform(b.to);
        }
        EXPECT_EQ(PreflightRigidCarrier(q).status, RigidCarrierPreflight::Status::Unknown);
        EXPECT_NO_THROW(CompileRigidCarrier(q));
    }
    for (auto& a : p.inputs[0].atoms)
        a.type = core::AtomType::Air;
    p.outputs = {p.inputs[0]};
    for (auto& a : p.outputs[0].atoms)
        a.type = core::AtomType::Salt;
    EXPECT_EQ(PreflightRigidCarrier(p).status, RigidCarrierPreflight::Status::Unsupported);
    EXPECT_THROW(CompileRigidCarrier(p), RigidCarrierUnsupported);
    p.permissionBits |= 1ULL << 12;
    EXPECT_EQ(PreflightRigidCarrier(p).status, RigidCarrierPreflight::Status::Unknown);
    EXPECT_NO_THROW(CompileRigidCarrier(p));
    p.outputs[0].bonds.clear();
    EXPECT_EQ(PreflightRigidCarrier(p).status, RigidCarrierPreflight::Status::Unsupported);
    EXPECT_THROW(CompileRigidCarrier(p), RigidCarrierUnsupported);
    const auto expired = std::chrono::steady_clock::now() - std::chrono::milliseconds(1);
    const auto result = PreflightRigidCarrier(p, expired);
    EXPECT_EQ(result.status, RigidCarrierPreflight::Status::Unsupported);
    EXPECT_EQ(result.reason, RigidCarrierReason::Deadline);
}
TEST(RigidCarrierTest, FiniteDomainAndDeadline) {
    auto p = io::BinaryParser::ParsePuzzleFile(
        root / "asset/puzzle/campaign/ch1-and-prologue/P014.puzzle");
    EXPECT_NO_THROW(CompileRigidCarrier(p));
    EXPECT_THROW(CompileRigidCarrier(p, std::chrono::steady_clock::now()), std::invalid_argument);
    auto duplicate = p;
    duplicate.inputs[0].atoms.push_back(duplicate.inputs[0].atoms[0]);
    EXPECT_THROW(CompileRigidCarrier(duplicate), std::invalid_argument);
    auto bond = p;
    bond.inputs[0].bonds[0].bits = 2;
    EXPECT_THROW(CompileRigidCarrier(bond), std::invalid_argument);
    auto permission = p;
    permission.permissionBits &= ~(1ULL << 8);
    EXPECT_THROW(CompileRigidCarrier(permission), std::invalid_argument);
    auto rotated = p;
    for (auto* list : {&rotated.inputs, &rotated.outputs})
        for (auto& m : *list) {
            for (auto& a : m.atoms)
                a.position = core::Add(core::RotateClockwise(a.position), {2, -1});
            for (auto& b : m.bonds) {
                b.from = core::Add(core::RotateClockwise(b.from), {2, -1});
                b.to = core::Add(core::RotateClockwise(b.to), {2, -1});
            }
        }
    EXPECT_NO_THROW(CompileRigidCarrier(rotated));
}
struct CarrierEnvironment {
    std::optional<std::string> prior;
    CarrierEnvironment() {
        if (const auto* flag = std::getenv("OPUSOPTIMA_RIGID_CARRIER")) prior = flag;
    }
    ~CarrierEnvironment() {
        if (prior)
            setenv("OPUSOPTIMA_RIGID_CARRIER", prior->c_str(), 1);
        else
            unsetenv("OPUSOPTIMA_RIGID_CARRIER");
    }
};
TEST(RigidCarrierTest, StaticPartitionPortfolioRetainsFirstAndCallerCap) {
    CarrierEnvironment restore;
    unsetenv("OPUSOPTIMA_RIGID_CARRIER");
    auto copiesFixture = PartitionFixture();
    copiesFixture.inputs.resize(9, copiesFixture.outputs[0]);
    copiesFixture.outputs.push_back(copiesFixture.outputs.back());
    for (auto p : {PartitionFixture(), copiesFixture}) {
        const bool copies = p.inputs.size() > 8;
        p.permissionBits |= io::BinaryParser::ParsePuzzleFile(
                                root / "asset/puzzle/campaign/ch1-and-prologue/P014.puzzle")
                                .permissionBits;
        for (int mode : {0, 1, 2, 3}) {
            SCOPED_TRACE(mode);
            const auto authority = mode == 0   ? io::VerificationAuthority::None
                                   : mode == 1 ? io::VerificationAuthority::Independent
                                               : io::VerificationAuthority::LegacyGate;
            const std::size_t cap = mode == 2 ? 1 : 3;
            int calls = 0, partitions = 0;
            std::vector<std::byte> parent;
            const auto first = AssemblySolver{}.CollectVerified(
                p,
                [&](const auto& candidate, const auto& stage) {
                    if (!calls++) parent = io::BinaryWriter::WriteSolution(candidate);
                    partitions += stage == (copies ? "portfolio-static-input-copy"
                                                   : "portfolio-static-molecule-partition");
                    io::VerificationReport report;
                    report.status = io::VerificationStatus::Passed;
                    report.authority = authority;
                    return report;
                },
                cap);
            EXPECT_EQ(io::BinaryWriter::WriteSolution(first), parent);
            EXPECT_LE(calls, AssemblySolver::MaxCandidateSubmissions);
            EXPECT_LE(calls, cap);
#ifdef HAS_LIBVERIFY
            EXPECT_EQ(partitions, mode == 3 ? 1 : 0);
#else
            EXPECT_EQ(partitions, 0);
#endif
        }
    }
}
TEST(RigidCarrierTest, NoPlacementPreservesDownstreamOpportunity) {
    CarrierEnvironment restore;
    setenv("OPUSOPTIMA_RIGID_CARRIER", "on", 1);
    for (const auto& path : {"journal/journal2/P060.puzzle", "tourney-2019/week1.puzzle",
                             "campaign/ch2/P022.puzzle"}) {
        const auto p = io::BinaryParser::ParsePuzzleFile(root / "asset/puzzle" / path);
        const auto result = PreflightRigidCarrier(p);
        ASSERT_EQ(result.status, RigidCarrierPreflight::Status::Unsupported) << path;
        EXPECT_EQ(result.reason, RigidCarrierReason::NoMultiatomCover);
        EXPECT_THROW(CompileRigidCarrier(p), RigidCarrierUnsupported);
        std::vector<std::string> stages;
        AssemblySolver{}.CollectVerified(p, [&](const auto&, const auto& stage) {
            stages.push_back(stage);
            io::VerificationReport report;
            report.status = io::VerificationStatus::Passed;
            report.authority = io::VerificationAuthority::LegacyGate;
            return report;
        });
        EXPECT_EQ(std::count(stages.begin(), stages.end(), "portfolio-rigid-carrier"), 0);
    }
}
TEST(RigidCarrierTest, SingletonSkipPreservesDownstreamConstructionBudget) {
    CarrierEnvironment restore;
    const auto* oldRetiming = std::getenv("OPUSOPTIMA_ROW_RETIMING");
    struct RestoreRetiming {
        bool present;
        std::string value;
        ~RestoreRetiming() {
            if (present)
                setenv("OPUSOPTIMA_ROW_RETIMING", value.c_str(), 1);
            else
                unsetenv("OPUSOPTIMA_ROW_RETIMING");
        }
    } restoreRetiming{oldRetiming != nullptr, oldRetiming ? oldRetiming : ""};
    // Isolate carrier construction accounting from acceptance of newly scheduled candidates.
    setenv("OPUSOPTIMA_ROW_RETIMING", "off", 1);
    const auto p =
        io::BinaryParser::ParsePuzzleFile(root / "asset/puzzle/campaign/ch2/P021.puzzle");
    ASSERT_EQ(PreflightRigidCarrier(p).status, RigidCarrierPreflight::Status::Unsupported);
    auto logger = GetLoggerOrFallback("solver");
    const auto priorSinks = logger->sinks();
    const auto priorLevel = logger->level();
    std::ostringstream output;
    logger->sinks() = {std::make_shared<spdlog::sinks::ostream_sink_mt>(output)};
    logger->set_level(spdlog::level::info);
    std::vector<std::byte> offFirst;
    std::string offLog;
    for (const char* mode : {"off", static_cast<const char*>(nullptr)}) {
        output.str("");
        output.clear();
        if (mode)
            setenv("OPUSOPTIMA_RIGID_CARRIER", mode, 1);
        else
            unsetenv("OPUSOPTIMA_RIGID_CARRIER");
        int calls = 0;
        const auto first = AssemblySolver{}.CollectVerified(p, [&](const auto&, const auto& stage) {
            ++calls;
            EXPECT_NE(stage, "portfolio-rigid-carrier");
            io::VerificationReport r;
            r.status = io::VerificationStatus::Passed;
            r.authority = io::VerificationAuthority::LegacyGate;
            return r;
        });
        EXPECT_LE(calls, AssemblySolver::MaxCandidateSubmissions);
        if (mode) {
            offFirst = io::BinaryWriter::WriteSolution(first);
            offLog = output.str();
        } else {
            EXPECT_EQ(io::BinaryWriter::WriteSolution(first), offFirst);
#ifdef HAS_LIBVERIFY
            EXPECT_NE(
                output.str().find("status=Unsupported reason=no-multiatom-cover construction=1"),
                std::string::npos);
#endif
            EXPECT_EQ(output.str().find("portfolio-rigid-carrier event=attempt"),
                      std::string::npos);
            EXPECT_NE(
                output.str().find("portfolio-shared-workstation event=attempt construction=3"),
                std::string::npos)
                << output.str();
        }
    }
    EXPECT_NE(offLog.find("portfolio-shared-workstation event=attempt construction=3"),
              std::string::npos)
        << offLog;
    logger->sinks() = priorSinks;
    logger->set_level(priorLevel);
}
TEST(RigidCarrierTest, RuntimeAuthorityAndDefaultFlagAdmission) {
    CarrierEnvironment restore;
    const auto p = io::BinaryParser::ParsePuzzleFile(
        root / "asset/puzzle/campaign/ch1-and-prologue/P014.puzzle");
    ASSERT_NO_THROW(CompileRigidCarrier(p));
    auto logger = GetLoggerOrFallback("solver");
    const auto priorSinks = logger->sinks();
    const auto priorLevel = logger->level();
    std::ostringstream output;
    logger->sinks() = {std::make_shared<spdlog::sinks::ostream_sink_mt>(output)};
    logger->set_level(spdlog::level::info);
    for (auto authority : {io::VerificationAuthority::None, io::VerificationAuthority::Independent,
                           io::VerificationAuthority::LegacyGate})
        for (const char* flag :
             {static_cast<const char*>(nullptr), "off", "unknown", "on", "", "OFF"}) {
            if (flag)
                setenv("OPUSOPTIMA_RIGID_CARRIER", flag, 1);
            else
                unsetenv("OPUSOPTIMA_RIGID_CARRIER");
            output.str("");
            output.clear();
            int carriers = 0, calls = 0;
            std::vector<std::byte> parent;
            const auto first =
                AssemblySolver{}.CollectVerified(p, [&](const auto& candidate, const auto& stage) {
                    if (!calls++) parent = io::BinaryWriter::WriteSolution(candidate);
                    carriers += stage == "portfolio-rigid-carrier";
                    io::VerificationReport r;
                    r.status = io::VerificationStatus::Passed;
                    r.authority = authority;
                    return r;
                });
            EXPECT_EQ(io::BinaryWriter::WriteSolution(first), parent);
            bool expected = false;
#ifdef HAS_LIBVERIFY
            expected = authority == io::VerificationAuthority::LegacyGate &&
                       (!flag || std::string(flag) != "off");
#endif
            EXPECT_EQ(carriers, expected ? 1 : 0);
            if (expected) {
                EXPECT_NE(output.str().find(
                              "event=preflight status=Unknown reason=unknown construction=1"),
                          std::string::npos);
                EXPECT_NE(output.str().find("portfolio-rigid-carrier event=attempt construction=2"),
                          std::string::npos);
            }
            EXPECT_LE(calls, AssemblySolver::MaxCandidateSubmissions);
        }
    logger->sinks() = priorSinks;
    logger->set_level(priorLevel);
}
TEST(RigidCarrierTest, RejectingSinkDoesNotEnterUnverifiedPortfolio) {
    CarrierEnvironment restore;
    unsetenv("OPUSOPTIMA_RIGID_CARRIER");
    const auto p = io::BinaryParser::ParsePuzzleFile(
        root / "asset/puzzle/workshop-versions/w2868328394.puzzle");
    int calls = 0;
    EXPECT_THROW((void)AssemblySolver{}.CollectVerified(
                     p,
                     [&](const auto&, const auto& stage) {
                         ++calls;
                         EXPECT_LE(calls, AssemblySolver::MaxCandidateSubmissions);
                         EXPECT_NE(stage, "portfolio-rigid-carrier");
                         io::VerificationReport r;
                         r.status = io::VerificationStatus::Rejected;
                         return r;
                     }),
                 std::exception);
    EXPECT_EQ(calls, 1);
}
TEST(RigidCarrierTest, StaticReactionRequiresAcceptedNativeAuthority) {
    const auto p =
        io::BinaryParser::ParsePuzzleFile(root / "asset/puzzle/campaign/ch4/P032.puzzle");
    ASSERT_NO_THROW(CompileDemandDrivenConsumers(p));
    const auto* value = std::getenv("OPUSOPTIMA_STATIC_REACTION");
    const std::optional<std::string> prior =
        value ? std::optional<std::string>(value) : std::nullopt;
    unsetenv("OPUSOPTIMA_STATIC_REACTION");
    for (auto authority : {io::VerificationAuthority::None, io::VerificationAuthority::Independent,
                           io::VerificationAuthority::LegacyGate}) {
        int stations = 0, calls = 0;
        std::vector<std::byte> parent;
        const auto first =
            AssemblySolver{}.CollectVerified(p, [&](const auto& candidate, const auto& stage) {
                if (!calls++) parent = io::BinaryWriter::WriteSolution(candidate);
                stations += stage == "portfolio-static-reaction";
                io::VerificationReport report;
                report.status = io::VerificationStatus::Passed;
                report.authority = authority;
                return report;
            });
        EXPECT_EQ(io::BinaryWriter::WriteSolution(first), parent);
        EXPECT_EQ(stations, authority == io::VerificationAuthority::LegacyGate ? 1 : 0);
        EXPECT_LE(calls, AssemblySolver::MaxCandidateSubmissions);
    }
    if (prior)
        setenv("OPUSOPTIMA_STATIC_REACTION", prior->c_str(), 1);
    else
        unsetenv("OPUSOPTIMA_STATIC_REACTION");
}
TEST(RigidCarrierTest, RejectingSinkSharesUpperBound) {
    CarrierEnvironment restore;
    unsetenv("OPUSOPTIMA_RIGID_CARRIER");
    const auto p = io::BinaryParser::ParsePuzzleFile(
        root / "asset/puzzle/campaign/ch1-and-prologue/P014.puzzle");
    int calls = 0;
    std::vector<std::vector<std::byte>> unique;
    EXPECT_THROW((void)AssemblySolver{}.CollectVerified(
                     p,
                     [&](const auto& candidate, const auto& stage) {
                         ++calls;
                         EXPECT_LE(calls, AssemblySolver::MaxCandidateSubmissions);
                         EXPECT_NE(stage, "portfolio-static-reaction");
                         EXPECT_NE(stage, "portfolio-static-embedding");
                         EXPECT_NE(stage, "portfolio-rigid-carrier");
                         auto canonical = candidate;
                         canonical.solutionName.clear();
                         canonical.metrics = {};
                         const auto bytes = io::BinaryWriter::WriteSolution(canonical);
                         EXPECT_EQ(std::find(unique.begin(), unique.end(), bytes), unique.end());
                         unique.push_back(bytes);
                         io::VerificationReport report;
                         report.status = io::VerificationStatus::Rejected;
                         report.authority = io::VerificationAuthority::LegacyGate;
                         return report;
                     }),
                 std::exception);
    EXPECT_GT(calls, 1);
    EXPECT_LE(calls, 12);
}
TEST(RigidCarrierTest, CalcificationPermissionsAndIllegalConversion) {
    auto p = io::BinaryParser::ParsePuzzleFile(
        root / "asset/puzzle/campaign/ch1-and-prologue/P014.puzzle");
    // A complete molecular feed avoids any bonding or cover ambiguity.
    p.inputs = {p.outputs.front()};
    for (auto& atom : p.inputs[0].atoms)
        atom.type = core::AtomType::Air;
    for (auto& atom : p.outputs[0].atoms)
        atom.type = core::AtomType::Salt;
    p.permissionBits |= 1ULL << 12;
    EXPECT_NO_THROW(CompileRigidCarrier(p));
    auto noPermission = p;
    noPermission.permissionBits &= ~(1ULL << 12);
    EXPECT_THROW(CompileRigidCarrier(noPermission), std::invalid_argument);
    for (auto& atom : p.inputs[0].atoms)
        atom.type = core::AtomType::Quicksilver;
    EXPECT_THROW(CompileRigidCarrier(p), std::invalid_argument);
}
TEST(RigidCarrierTest, TwoOutputsRetainOriginalIndices) {
    auto p = io::BinaryParser::ParsePuzzleFile(
        root / "asset/puzzle/campaign/ch1-and-prologue/P014.puzzle");
    p.outputs.push_back(p.inputs.front());
    const auto solution = CompileRigidCarrier(p);
    std::vector<int> indices;
    for (const auto& object : solution.objects)
        if (object.typeName == "out-std") {
            indices.push_back(object.bodyData[2]);
            EXPECT_EQ(object.position, (core::HexCoord{object.bodyData[2] * 40, 0}));
        }
    EXPECT_EQ(indices, (std::vector<int>{0, 1}));
}
TEST(RigidCarrierTest, SearchNodeBudgetIsDeterministic) {
    auto p = io::BinaryParser::ParsePuzzleFile(
        root / "asset/puzzle/campaign/ch1-and-prologue/P014.puzzle");
    p.inputs.clear();
    p.outputs.clear();
    io::Molecule feed;
    feed.atoms = {{core::AtomType::Salt, {0, 0}}, {core::AtomType::Salt, {1, 0}}};
    feed.bonds = {{1, {0, 0}, {1, 0}}};
    p.inputs.assign(8, feed);
    io::Molecule target;
    // An odd chain cannot be tiled by dimers; duplicate legal feeds force the
    // bounded DFS to stop before exhausting the combinatorial search.
    for (int i = 0; i < 11; ++i) {
        target.atoms.push_back({core::AtomType::Salt, {i % 6, i / 6}});
        if (i && i != 6)
            target.bonds.push_back({1, target.atoms[i - 1].position, target.atoms[i].position});
    }
    target.bonds.push_back({1, {0, 0}, {0, 1}});
    p.outputs = {target};
    try {
        (void)CompileRigidCarrier(p);
        FAIL() << "odd target unexpectedly tiled";
    } catch (const RigidCarrierUnsupported& e) {
        EXPECT_EQ(e.reason(), RigidCarrierReason::SearchNodeCap);
        EXPECT_STREQ(e.what(), "rigid carrier Unsupported: search-node-cap");
    }
}
#ifdef HAS_LIBVERIFY
struct NativeWorkspace {
    std::filesystem::path dir;
    NativeWorkspace() {
        const auto base = root / "build/test-output/rigid-carrier";
        std::filesystem::create_directories(base);
        std::random_device rng;
        for (int attempt = 0; attempt < 64; ++attempt) {
            auto path = base / (std::to_string(rng()) + "-" + std::to_string(rng()));
            if (std::filesystem::create_directory(path)) {
                dir = path;
                break;
            }
        }
        if (dir.empty()) throw std::runtime_error("exclusive native workspace unavailable");
    }
    ~NativeWorkspace() {
        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
        EXPECT_FALSE(ec) << ec.message();
    }
};
void WriteOrdinaryPuzzle(const std::filesystem::path& path, const io::PuzzleData& p) {
    std::ofstream stream(path, std::ios::binary);
    auto word = [&](std::uint64_t v, int n) {
        for (int i = 0; i < n; ++i)
            stream.put(static_cast<char>(v >> (8 * i)));
    };
    auto cell = [&](core::HexCoord c) {
        word(c.q, 1);
        word(c.r, 1);
    };
    word(3, 4);
    word(p.name.size(), 1);
    stream.write(p.name.data(), p.name.size());
    word(0, 8);
    word(p.permissionBits, 8);
    for (const auto* list : {&p.inputs, &p.outputs}) {
        word(list->size(), 4);
        for (const auto& m : *list) {
            word(m.atoms.size(), 4);
            for (const auto& a : m.atoms) {
                word(static_cast<int>(a.type), 1);
                cell(a.position);
            }
            word(m.bonds.size(), 4);
            for (const auto& b : m.bonds) {
                word(b.bits, 1);
                cell(b.from);
                cell(b.to);
            }
        }
    }
    word(p.outputMultiplier, 4);
    word(0, 1);
    ASSERT_TRUE(stream.good());
}
TEST(RigidCarrierTest, StaticPartitionWholeBatchRegeneratesAtOriginalNativeGoal) {
    for (int rotation : {0, 2, 5}) {
        SCOPED_TRACE(rotation);
        auto p = PartitionFixture();
        auto transform = [&](core::HexCoord cell) {
            for (int r = 0; r < rotation; ++r)
                cell = core::RotateClockwise(cell);
            return core::Add(cell, {3, -2});
        };
        for (auto& input : p.inputs) {
            for (auto& atom : input.atoms)
                atom.position = transform(atom.position);
            for (auto& bond : input.bonds) {
                bond.from = transform(bond.from);
                bond.to = transform(bond.to);
            }
            std::reverse(input.atoms.begin(), input.atoms.end());
        }
        if (rotation) {
            std::reverse(p.inputs.begin(), p.inputs.end());
            std::reverse(p.outputs.begin(), p.outputs.end());
        }
        for (auto& target : p.outputs) {
            for (auto& atom : target.atoms)
                atom.position = core::Add(atom.position, {-2, 3});
            for (auto& bond : target.bonds) {
                bond.from = core::Add(bond.from, {-2, 3});
                bond.to = core::Add(bond.to, {-2, 3});
            }
        }
        NativeWorkspace w;
        WriteOrdinaryPuzzle(w.dir / "input.puzzle", p);
        auto candidate = CompileRigidCarrier(p);
        auto verify = [&](const auto& machine) {
            io::BinaryWriter::WriteSolutionFile(w.dir / "candidate.solution", machine);
            return io::VerifySolutionFiles(w.dir / "input.puzzle", w.dir / "candidate.solution",
                                           150000, 10000);
        };
        auto report = verify(candidate);
        ASSERT_TRUE(report.Passed()) << report.detail;
        EXPECT_EQ(report.authority, io::VerificationAuthority::LegacyGate);
        candidate.metrics = report.metrics;
        EXPECT_TRUE(verify(candidate).Passed());
        if (!rotation) {
            auto invalid = candidate;
            const auto cut = std::find_if(invalid.objects.begin(), invalid.objects.end(),
                                          [](const auto& o) { return o.typeName == "unbonder"; });
            ASSERT_NE(cut, invalid.objects.end());
            invalid.objects.erase(cut);
            EXPECT_FALSE(verify(invalid).Passed());
            invalid = candidate;
            for (auto& o : invalid.objects)
                if (o.typeName == "out-std") o.bodyData[2] = 0;
            EXPECT_FALSE(verify(invalid).Passed());
        }
    }
}
TEST(RigidCarrierTest, StaticCopyEveryOriginalPortPassesNativeGoal) {
    auto p = StaticCopyFixture();
    NativeWorkspace w;
    WriteOrdinaryPuzzle(w.dir / "input.puzzle", p);
    const auto candidate = CompileRigidCarrier(p);
    ExpectStaticCopyPoses(p, candidate);
    auto verify = [&](const auto& machine) {
        io::BinaryWriter::WriteSolutionFile(w.dir / "candidate.solution", machine);
        return io::VerifySolutionFiles(w.dir / "input.puzzle", w.dir / "candidate.solution", 150000,
                                       10000);
    };
    const auto report = verify(candidate);
    ASSERT_TRUE(report.Passed()) << report.detail;
    EXPECT_EQ(report.authority, io::VerificationAuthority::LegacyGate);
    auto invalid = candidate;
    const auto port = std::find_if(invalid.objects.begin(), invalid.objects.end(),
                                   [](const auto& o) { return o.typeName == "out-std"; });
    ASSERT_NE(port, invalid.objects.end());
    invalid.objects.erase(port);
    EXPECT_FALSE(verify(invalid).Passed());
    invalid = candidate;
    for (auto& o : invalid.objects)
        if (o.typeName == "out-std") o.bodyData[2] = 0;
    EXPECT_FALSE(verify(invalid).Passed());
    invalid = candidate;
    for (auto& o : invalid.objects)
        if (o.typeName == "input" && o.bodyData[2] == 7) o.bodyData[1] = (o.bodyData[1] + 1) % 6;
    EXPECT_FALSE(verify(invalid).Passed());
}
TEST(RigidCarrierTest, OriginalNativeGate) {
    NativeWorkspace workspace;
    const auto& dir = workspace.dir;
    for (const auto* file :
         {"campaign/ch1-and-prologue/P014.puzzle", "workshop-versions/w2868328394.puzzle",
          "tourney-2021/OM2021_W9_00.puzzle"}) {
        const auto original = root / "asset/puzzle" / file;
        const auto p = io::BinaryParser::ParsePuzzleFile(original);
        const auto solution = dir / "candidate.solution";
        io::BinaryWriter::WriteSolutionFile(solution, CompileRigidCarrier(p));
        const auto report = io::VerifySolutionFiles(original, solution, 150000, 10000);
        EXPECT_TRUE(report.Passed()) << file << ": " << report.detail;
    }
}
TEST(RigidCarrierTest, FeasibleWholeMoleculeCoverIgnoresInputOrder) {
    for (int rotation = 0; rotation < 6; ++rotation)
        for (bool reverseOutputs : {false, true}) {
            SCOPED_TRACE(rotation);
            SCOPED_TRACE(reverseOutputs);
            auto p = io::BinaryParser::ParsePuzzleFile(
                root / "asset/puzzle/campaign/ch1-and-prologue/P014.puzzle");
            p.permissionBits &= ~((1ULL << 8) | (1ULL << 12));
            p.permissionBits |= 1ULL << 25;
            io::Molecule fragment;
            fragment.atoms = {{core::AtomType::Salt, {0, 0}}, {core::AtomType::Air, {1, 0}}};
            fragment.bonds = {{1, {0, 0}, {1, 0}}};
            io::Molecule salt, air, earth;
            salt.atoms = {{core::AtomType::Salt, {0, 0}}};
            air.atoms = {{core::AtomType::Air, {0, 0}}};
            earth.atoms = {{core::AtomType::Earth, {0, 0}}};
            p.inputs = {salt, air, earth, fragment};
            p.outputs = {fragment, earth};
            if (reverseOutputs) std::reverse(p.outputs.begin(), p.outputs.end());
            std::rotate(p.inputs.begin(), p.inputs.begin() + rotation % p.inputs.size(),
                        p.inputs.end());
            auto transform = [&](core::HexCoord c) {
                for (int r = 0; r < rotation; ++r)
                    c = core::RotateClockwise(c);
                return core::Add(c, {3, -2});
            };
            for (auto& m : p.inputs) {
                for (auto& a : m.atoms)
                    a.position = transform(a.position);
                for (auto& b : m.bonds) {
                    b.from = transform(b.from);
                    b.to = transform(b.to);
                }
                std::reverse(m.atoms.begin(), m.atoms.end());
            }
            auto candidate = CompileRigidCarrier(p);
            EXPECT_EQ(std::count_if(candidate.objects.begin(), candidate.objects.end(),
                                    [](const auto& o) { return o.typeName == "bonder"; }),
                      0);
            NativeWorkspace w;
            WriteOrdinaryPuzzle(w.dir / "input.puzzle", p);
            io::BinaryWriter::WriteSolutionFile(w.dir / "candidate.solution", candidate);
            auto report = io::VerifySolutionFiles(w.dir / "input.puzzle",
                                                  w.dir / "candidate.solution", 150000, 10000);
            ASSERT_TRUE(report.Passed()) << report.detail;
            EXPECT_EQ(report.authority, io::VerificationAuthority::LegacyGate);
            candidate.metrics = report.metrics;
            io::BinaryWriter::WriteSolutionFile(w.dir / "candidate.solution", candidate);
            EXPECT_TRUE(io::VerifySolutionFiles(w.dir / "input.puzzle",
                                                w.dir / "candidate.solution", 150000, 10000)
                            .Passed());
        }
}

TEST(RigidCarrierTest, OnlyInheritedEdgesSuppressRequiredBonders) {
    auto p = io::BinaryParser::ParsePuzzleFile(
        root / "asset/puzzle/campaign/ch1-and-prologue/P014.puzzle");
    p.permissionBits |= (1ULL << 8) | (1ULL << 25);
    p.inputs = {{{{core::AtomType::Salt, {0, 0}},
                  {core::AtomType::Salt, {1, 0}},
                  {core::AtomType::Salt, {0, 1}}},
                 {{1, {0, 0}, {1, 0}}, {1, {0, 0}, {0, 1}}}}};
    p.outputs = p.inputs;
    p.outputs[0].bonds.push_back({1, {1, 0}, {0, 1}});
    auto candidate = CompileRigidCarrier(p);
    EXPECT_EQ(std::count_if(candidate.objects.begin(), candidate.objects.end(),
                            [](const auto& o) { return o.typeName == "bonder"; }),
              1);
    NativeWorkspace w;
    WriteOrdinaryPuzzle(w.dir / "input.puzzle", p);
    io::BinaryWriter::WriteSolutionFile(w.dir / "candidate.solution", candidate);
    const auto report = io::VerifySolutionFiles(w.dir / "input.puzzle",
                                                w.dir / "candidate.solution", 150000, 10000);
    ASSERT_TRUE(report.Passed()) << report.detail;
    auto denied = p;
    denied.permissionBits &= ~(1ULL << 8);
    EXPECT_THROW((void)CompileRigidCarrier(denied), RigidCarrierUnsupported);
    std::swap(p.inputs, p.outputs);
    p.permissionBits &= ~(1ULL << 9);
    EXPECT_THROW((void)CompileRigidCarrier(p), RigidCarrierUnsupported);
}

TEST(RigidCarrierTest, PrefersInheritedBondsAndAvoidsUnnecessaryCalcifiers) {
    for (bool reversed : {false, true}) {
        auto p = io::BinaryParser::ParsePuzzleFile(
            root / "asset/puzzle/campaign/ch1-and-prologue/P014.puzzle");
        p.permissionBits |= (1ULL << 8) | (1ULL << 12) | (1ULL << 25);
        io::Molecule path;
        path.atoms = {{core::AtomType::Salt, {0, 0}},
                      {core::AtomType::Salt, {1, 0}},
                      {core::AtomType::Salt, {0, 1}}};
        path.bonds = {{1, {0, 0}, {1, 0}}, {1, {0, 0}, {0, 1}}};
        auto triangle = path;
        triangle.bonds.push_back({1, {1, 0}, {0, 1}});
        auto converted = triangle;
        for (auto& atom : converted.atoms)
            atom.type = core::AtomType::Air;
        p.inputs = {path, converted, triangle};
        p.outputs = {triangle};
        if (reversed) std::reverse(p.inputs.begin(), p.inputs.end());
        auto candidate = CompileRigidCarrier(p);
        EXPECT_EQ(std::count_if(candidate.objects.begin(), candidate.objects.end(),
                                [](const auto& o) {
                                    return o.typeName == "bonder" ||
                                           o.typeName == "glyph-calcification";
                                }),
                  0);
        NativeWorkspace w;
        WriteOrdinaryPuzzle(w.dir / "input.puzzle", p);
        io::BinaryWriter::WriteSolutionFile(w.dir / "candidate.solution", candidate);
        const auto report = io::VerifySolutionFiles(w.dir / "input.puzzle",
                                                    w.dir / "candidate.solution", 150000, 10000);
        ASSERT_TRUE(report.Passed()) << report.detail;
        EXPECT_EQ(report.authority, io::VerificationAuthority::LegacyGate);
        candidate.metrics = report.metrics;
        io::BinaryWriter::WriteSolutionFile(w.dir / "candidate.solution", candidate);
        EXPECT_TRUE(io::VerifySolutionFiles(w.dir / "input.puzzle", w.dir / "candidate.solution",
                                            150000, 10000)
                        .Passed());
    }
}

TEST(RigidCarrierTest, ExactCutsAndJoinsPreserveAllSourceAtoms) {
    for (bool needsJoin : {false, true})
        for (int rotation = 0; rotation < 6; ++rotation) {
            SCOPED_TRACE(needsJoin);
            SCOPED_TRACE(rotation);
            auto p = io::BinaryParser::ParsePuzzleFile(
                root / "asset/puzzle/campaign/ch1-and-prologue/P014.puzzle");
            p.permissionBits |= (1ULL << 9) | (1ULL << 25);
            p.permissionBits &= ~(1ULL << 8);
            if (needsJoin) p.permissionBits |= 1ULL << 8;
            io::Molecule source;
            source.atoms = {{core::AtomType::Salt, {0, 0}},
                            {core::AtomType::Air, {1, 0}},
                            {core::AtomType::Earth, {0, 1}}};
            source.bonds = {{1, {0, 0}, {1, 0}}, {1, {0, 0}, {0, 1}}};
            auto target = source;
            if (needsJoin)
                target.bonds[1] = {1, {1, 0}, {0, 1}};
            else
                source.bonds.push_back({1, {1, 0}, {0, 1}});
            auto transform = [&](core::HexCoord c) {
                for (int r = 0; r < rotation; ++r)
                    c = core::RotateClockwise(c);
                return core::Add(c, {3, -2});
            };
            for (auto& atom : source.atoms)
                atom.position = transform(atom.position);
            for (auto& bond : source.bonds) {
                bond.from = transform(bond.from);
                bond.to = transform(bond.to);
                if (rotation % 2) std::swap(bond.from, bond.to);
            }
            if (rotation % 2) {
                std::reverse(source.atoms.begin(), source.atoms.end());
                std::reverse(source.bonds.begin(), source.bonds.end());
            }
            p.inputs = {source};
            p.outputs = {target};
            if (rotation % 2) p.outputs.push_back(target);
            auto denied = p;
            denied.permissionBits &= ~(1ULL << 9);
            EXPECT_THROW((void)CompileRigidCarrier(denied), RigidCarrierUnsupported);
            EXPECT_EQ(PreflightRigidCarrier(p).status, RigidCarrierPreflight::Status::Unknown);
            auto candidate = CompileRigidCarrier(p);
            EXPECT_EQ(std::count_if(candidate.objects.begin(), candidate.objects.end(),
                                    [](const auto& o) { return o.typeName == "unbonder"; }),
                      p.outputs.size());
            EXPECT_EQ(std::count_if(candidate.objects.begin(), candidate.objects.end(),
                                    [](const auto& o) { return o.typeName == "bonder"; }),
                      needsJoin ? p.outputs.size() : 0u);
            NativeWorkspace w;
            WriteOrdinaryPuzzle(w.dir / "input.puzzle", p);
            io::BinaryWriter::WriteSolutionFile(w.dir / "candidate.solution", candidate);
            const auto report = io::VerifySolutionFiles(
                w.dir / "input.puzzle", w.dir / "candidate.solution", 150000, 10000);
            ASSERT_TRUE(report.Passed()) << report.detail;
            EXPECT_EQ(report.authority, io::VerificationAuthority::LegacyGate);
            candidate.metrics = report.metrics;
            io::BinaryWriter::WriteSolutionFile(w.dir / "candidate.solution", candidate);
            EXPECT_TRUE(io::VerifySolutionFiles(w.dir / "input.puzzle",
                                                w.dir / "candidate.solution", 150000, 10000)
                            .Passed());
            auto missing = candidate;
            const auto cut = std::find_if(missing.objects.begin(), missing.objects.end(),
                                          [](const auto& o) { return o.typeName == "unbonder"; });
            ASSERT_NE(cut, missing.objects.end());
            missing.objects.erase(cut);
            io::BinaryWriter::WriteSolutionFile(w.dir / "candidate.solution", missing);
            EXPECT_FALSE(io::VerifySolutionFiles(w.dir / "input.puzzle",
                                                 w.dir / "candidate.solution", 150000, 10000)
                             .Passed());
        }
}

TEST(RigidCarrierTest, LargeCopiesRewritesAndFragmentJoinsPassNativeTransforms) {
    for (int mode : {0, 1, 2})
        for (int count : {13, 14, 15, 16}) {
            if (mode == 1 && count != 13 && count != 16) continue;
            if (mode == 2 && count != 16) continue;
            for (int rotation = 0; rotation < 6; ++rotation) {
                SCOPED_TRACE(mode);
                SCOPED_TRACE(count);
                SCOPED_TRACE(rotation);
                auto p = io::BinaryParser::ParsePuzzleFile(
                    root / "asset/puzzle/campaign/ch1-and-prologue/P014.puzzle");
                p.permissionBits = 1ULL | (1ULL << 25);
                if (mode) p.permissionBits |= 1ULL << 8;
                if (mode == 1) p.permissionBits |= 1ULL << 9;
                auto target = CarrierSnake(count);
                p.inputs = {target};
                p.outputs = {target};
                if (mode == 1) {
                    p.outputs[0].bonds.erase(p.outputs[0].bonds.begin() + 1);
                    p.outputs[0].bonds.push_back({1, {1, 0}, {1, 1}});
                }
                if (mode == 2) {
                    p.inputs.clear();
                    for (bool tail : {false, true}) {
                        io::Molecule fragment;
                        const auto first = tail ? 13 : 0, last = tail ? count : 13;
                        fragment.atoms.assign(target.atoms.begin() + first,
                                              target.atoms.begin() + last);
                        auto contains = [&](core::HexCoord cell) {
                            return std::any_of(
                                fragment.atoms.begin(), fragment.atoms.end(),
                                [&](const auto& atom) { return atom.position == cell; });
                        };
                        for (const auto& bond : target.bonds)
                            if (contains(bond.from) && contains(bond.to))
                                fragment.bonds.push_back(bond);
                        p.inputs.push_back(fragment);
                    }
                }
                if (rotation % 2) {
                    p.outputs.push_back(p.outputs[0]);
                    std::reverse(p.inputs.begin(), p.inputs.end());
                }
                auto transform = [&](core::HexCoord cell) {
                    for (int r = 0; r < rotation; ++r)
                        cell = core::RotateClockwise(cell);
                    return core::Add(cell, {3, -2});
                };
                for (auto& input : p.inputs) {
                    for (auto& atom : input.atoms)
                        atom.position = transform(atom.position);
                    for (auto& bond : input.bonds) {
                        bond.from = transform(bond.from);
                        bond.to = transform(bond.to);
                        if (rotation % 2) std::swap(bond.from, bond.to);
                    }
                    if (rotation % 2) {
                        std::reverse(input.atoms.begin(), input.atoms.end());
                        std::reverse(input.bonds.begin(), input.bonds.end());
                    }
                }
                EXPECT_EQ(PreflightRigidCarrier(p).status, RigidCarrierPreflight::Status::Unknown);
                auto candidate = CompileRigidCarrier(p);
                EXPECT_EQ(
                    std::count_if(candidate.objects.begin(), candidate.objects.end(),
                                  [](const auto& object) { return object.typeName == "bonder"; }),
                    mode ? p.outputs.size() : 0u);
                EXPECT_EQ(
                    std::count_if(candidate.objects.begin(), candidate.objects.end(),
                                  [](const auto& object) { return object.typeName == "unbonder"; }),
                    mode == 1 ? p.outputs.size() : 0u);
                NativeWorkspace w;
                WriteOrdinaryPuzzle(w.dir / "input.puzzle", p);
                io::BinaryWriter::WriteSolutionFile(w.dir / "candidate.solution", candidate);
                const auto report = io::VerifySolutionFiles(
                    w.dir / "input.puzzle", w.dir / "candidate.solution", 150000, 10000);
                ASSERT_TRUE(report.Passed()) << report.detail;
                EXPECT_EQ(report.authority, io::VerificationAuthority::LegacyGate);
                candidate.metrics = report.metrics;
                io::BinaryWriter::WriteSolutionFile(w.dir / "candidate.solution", candidate);
                EXPECT_TRUE(io::VerifySolutionFiles(w.dir / "input.puzzle",
                                                    w.dir / "candidate.solution", 150000, 10000)
                                .Passed());
                if (mode == 1 && rotation == 0)
                    for (const auto* part : {"unbonder", "bonder"}) {
                        auto invalid = candidate;
                        const auto it = std::find_if(
                            invalid.objects.begin(), invalid.objects.end(),
                            [&](const auto& object) { return object.typeName == part; });
                        ASSERT_NE(it, invalid.objects.end());
                        invalid.objects.erase(it);
                        io::BinaryWriter::WriteSolutionFile(w.dir / "candidate.solution", invalid);
                        EXPECT_FALSE(io::VerifySolutionFiles(w.dir / "input.puzzle",
                                                             w.dir / "candidate.solution", 150000,
                                                             10000)
                                         .Passed());
                    }
            }
        }
}

TEST(RigidCarrierTest, ReorderedNonzeroSourcePassesRealNativeGate) {
    NativeWorkspace w;
    auto p = io::BinaryParser::ParsePuzzleFile(
        root / "asset/puzzle/campaign/ch1-and-prologue/P014.puzzle");
    for (auto& m : p.inputs) {
        std::reverse(m.atoms.begin(), m.atoms.end());
        for (auto& a : m.atoms)
            a.position = core::Add(core::RotateClockwise(a.position), {2, -1});
        for (auto& b : m.bonds) {
            b.from = core::Add(core::RotateClockwise(b.from), {2, -1});
            b.to = core::Add(core::RotateClockwise(b.to), {2, -1});
        }
    }
    WriteOrdinaryPuzzle(w.dir / "input.puzzle", p);
    io::BinaryWriter::WriteSolutionFile(w.dir / "candidate.solution", CompileRigidCarrier(p));
    const auto report = io::VerifySolutionFiles(w.dir / "input.puzzle",
                                                w.dir / "candidate.solution", 150000, 10000);
    EXPECT_TRUE(report.Passed()) << report.detail;
}
TEST(RigidCarrierTest, TwoOriginalOutputIndicesPassNativeGate) {
    NativeWorkspace w;
    auto p = io::BinaryParser::ParsePuzzleFile(
        root / "asset/puzzle/campaign/ch1-and-prologue/P014.puzzle");
    p.outputs.push_back(p.inputs.front());
    std::swap(p.outputs[0], p.outputs[1]);
    WriteOrdinaryPuzzle(w.dir / "input.puzzle", p);
    const auto candidate = CompileRigidCarrier(p);
    io::BinaryWriter::WriteSolutionFile(w.dir / "candidate.solution", candidate);
    const auto report = io::VerifySolutionFiles(w.dir / "input.puzzle",
                                                w.dir / "candidate.solution", 150000, 10000);
    EXPECT_TRUE(report.Passed()) << report.detail;
    EXPECT_EQ(report.authority, io::VerificationAuthority::LegacyGate);
}
TEST(RigidCarrierTest, IndependentArchiveFailsClosedWithoutAcceptedParent) {
    NativeWorkspace w;
    CarrierEnvironment restore;
    unsetenv("OPUSOPTIMA_RIGID_CARRIER");
    const auto input = root / "asset/puzzle/campaign/ch1-and-prologue/P014.puzzle";
    const auto p = io::BinaryParser::ParsePuzzleFile(input);
    ASSERT_NO_THROW(CompileRigidCarrier(p));
    io::VerificationOptions options;
    options.mode = io::VerificationMode::Independent;
    io::CandidateArchive archive(input, w.dir / "first.solution", options);
    int carriers = 0, calls = 0;
    EXPECT_THROW((void)AssemblySolver{}.CollectVerified(
                     p,
                     [&](const auto& candidate, const auto& stage) {
                         ++calls;
                         carriers += stage == "portfolio-rigid-carrier";
                         EXPECT_NE(stage, "portfolio-static-embedding");
                         EXPECT_NE(stage, "portfolio-static-reaction");
                         const auto report = archive.Submit(candidate, stage);
                         EXPECT_FALSE(report.Passed()) << report.detail;
                         return report;
                     }),
                 std::exception);
    EXPECT_FALSE(archive.First());
    EXPECT_EQ(carriers, 0);
    for (const auto& slot : archive.Slots())
        EXPECT_FALSE(slot);
    EXPECT_FALSE(std::filesystem::exists(w.dir / "first.solution"));
    EXPECT_GT(calls, 0);
    EXPECT_LE(calls, AssemblySolver::MaxCandidateSubmissions);
}
TEST(RigidCarrierTest, NativeRejectAndTimeoutPreserveVerifiedArchive) {
    CarrierEnvironment restore;
    unsetenv("OPUSOPTIMA_RIGID_CARRIER");
    for (bool timeout : {false, true}) {
        NativeWorkspace w;
        const auto input = root / "asset/puzzle/campaign/ch1-and-prologue/P014.puzzle";
        const auto p = io::BinaryParser::ParsePuzzleFile(input);
        io::CandidateArchive archive(input, w.dir / "first.solution", {});
        io::CandidateArchive::Entry parent;
        std::array<io::CandidateArchive::Entry, 3> slots;
        int carriers = 0;
        const auto first =
            AssemblySolver{}.CollectVerified(p, [&](const auto& candidate, const auto& stage) {
                if (stage == "portfolio-rigid-carrier") {
                    ++carriers;
                    parent = archive.First();
                    slots = archive.Slots();
                    auto invalid = candidate;
                    invalid.objects.clear();
                    const auto r =
                        timeout ? archive.Submit(candidate, stage, std::chrono::steady_clock::now())
                                : archive.Submit(invalid, stage);
                    EXPECT_EQ(r.status, timeout ? io::VerificationStatus::Timeout
                                                : io::VerificationStatus::Rejected)
                        << r.detail;
                    EXPECT_EQ(archive.First(), parent);
                    EXPECT_EQ(archive.Slots(), slots);
                    return r;
                }
                return archive.Submit(candidate, stage);
            });
        ASSERT_EQ(carriers, 1);
        ASSERT_TRUE(parent);
        EXPECT_EQ(io::BinaryWriter::WriteSolution(first), parent->bytes);
        EXPECT_EQ(archive.First(), parent);
        EXPECT_EQ(archive.Slots(), slots);
        EXPECT_TRUE(archive.Snapshot({}).Passed());
    }
}

#endif
}} // namespace opusoptima::solvers
