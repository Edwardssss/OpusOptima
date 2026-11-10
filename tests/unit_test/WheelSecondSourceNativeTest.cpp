#include "RowAssemblyInternal.hpp"
#include "AssemblySolver.hpp"
#include "WheelAssembly.hpp"
#include "DemandDrivenChain.hpp"
#include "IO/CandidateArchive.hpp"
#include "IO/BinaryWriter.hpp"
#include "IO/SolutionVerifier.hpp"
#include "TestOutputDirectory.hpp"
#include <gtest/gtest.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>
#include "RepoRoot.hpp"

#ifdef HAS_LIBVERIFY
namespace opusoptima::solvers { namespace {
class FiniteRetentionScope {
public:
    explicit FiniteRetentionScope(const char* requested = "1") {
        const auto* value = std::getenv("OPUSOPTIMA_FINITE_RETENTION");
        present_ = value != nullptr;
        if (value) saved_ = value;
        if (requested) setenv("OPUSOPTIMA_FINITE_RETENTION", requested, 1);
        else unsetenv("OPUSOPTIMA_FINITE_RETENTION");
    }
    ~FiniteRetentionScope() {
        if (present_) setenv("OPUSOPTIMA_FINITE_RETENTION", saved_.c_str(), 1);
        else unsetenv("OPUSOPTIMA_FINITE_RETENTION");
    }
private:
    bool present_{};
    std::string saved_;
};

void WriteWheelPuzzle(const std::filesystem::path& path, const io::PuzzleData& puzzle) {
    std::ofstream stream(path, std::ios::binary);
    auto word = [&](std::uint64_t value, int bytes) {
        for (int i = 0; i < bytes; ++i) stream.put(static_cast<char>(value >> (8 * i)));
    };
    auto cell = [&](core::HexCoord position) { word(position.q, 1); word(position.r, 1); };
    word(3, 4);
    word(puzzle.name.size(), 1);
    stream.write(puzzle.name.data(), puzzle.name.size());
    word(0, 8);
    word(puzzle.permissionBits, 8);
    for (const auto* molecules : {&puzzle.inputs, &puzzle.outputs}) {
        word(molecules->size(), 4);
        for (const auto& molecule : *molecules) {
            word(molecule.atoms.size(), 4);
            for (const auto& atom : molecule.atoms) {
                word(static_cast<int>(atom.type), 1);
                cell(atom.position);
            }
            word(molecule.bonds.size(), 4);
            for (const auto& bond : molecule.bonds) {
                word(bond.bits, 1);
                cell(bond.from);
                cell(bond.to);
            }
        }
    }
    word(puzzle.outputMultiplier, 4);
    word(0, 1);
    stream.close();
    ASSERT_TRUE(stream.good());
}

TEST(WheelAssemblyTest, RadialReuseNativeSubsetsRepeatedDemandAndTransforms) {
    test::ScopedTestDirectory scratch;
    io::PuzzleData original;
    original.version = 3;
    original.name = "generated radial wheel";
    original.outputMultiplier = 1;
    original.permissionBits = AssemblyPlanner::RequiredPermissions | (1ULL << 13) |
                              (1ULL << 25) | (1ULL << 26) | (1ULL << 28);
    original.inputs = {{{{core::AtomType::Salt, {-3, 4}}}, {}}};
    const std::array<core::HexCoord, 6> leaves{{{1, 0}, {0, 1}, {-1, 1}, {-1, 0}, {0, -1}, {1, -1}}};
    for (const auto [count, rotation] : std::array<std::pair<int, int>, 6>{{
             {2, 0}, {3, 1}, {4, 2}, {5, 3}, {6, 4}, {3, 5}}}) {
        SCOPED_TRACE(count);
        SCOPED_TRACE(rotation);
        auto p = original;
        auto transform = [&](core::HexCoord c) {
            for (int i = 0; i < rotation; ++i) c = core::RotateClockwise(c);
            return core::Add(c, {6, -3});
        };
        io::Molecule output;
        output.atoms.push_back({core::AtomType::Salt, transform({})});
        for (int i = 0; i < count; ++i) {
            output.atoms.push_back({i % 3 == 2 ? core::AtomType::Salt : core::AtomType::Air,
                                    transform(leaves[i])});
            output.bonds.push_back({1, transform(leaves[i]), transform({})});
        }
        std::reverse(output.atoms.begin(), output.atoms.end());
        p.outputs = {output};
        const auto puzzleFile = scratch.Path() / "input.puzzle";
        const auto machineFile = scratch.Path() / "candidate.solution";
        WriteWheelPuzzle(puzzleFile, p);
        const auto machine = CompileRadialWheelAssembly(p);
        for (const auto* device : {"baron", "glyph-duplication", "bonder"})
            EXPECT_EQ(std::count_if(machine.objects.begin(), machine.objects.end(),
                [&](const auto& part) { return part.typeName == device; }), 1);
        io::BinaryWriter::WriteSolutionFile(machineFile, machine);
        const auto report = io::VerifySolutionFiles(puzzleFile, machineFile, 1000000, 10000);
        ASSERT_TRUE(report.Passed()) << report.detail << " cycle=" << report.errorCycle;
        EXPECT_EQ(report.metrics.cost, 100);
    }
}

TEST(WheelAssemblyTest, RadialProjectionNativeRepeatedDemandTranslatedPortsAndOriginalW6) {
    test::ScopedTestDirectory scratch;
    const auto root = opusoptima::test::RepoRoot(__FILE__);
    const auto original = root / "asset/puzzle/tourney-2021/OM2021_W6.puzzle";
    for (bool generated : {true, false}) {
        SCOPED_TRACE(generated);
        auto p = io::BinaryParser::ParsePuzzleFile(original);
        auto puzzleFile = original;
        if (generated) {
            p.name = "generated controlled radial projection";
            p.inputs = {{{{core::AtomType::Salt, {-4, 3}}}, {}},
                        {{{core::AtomType::Quicksilver, {2, -5}}}, {}},
                        {{{core::AtomType::Lead, {-3, 4}}}, {}}};
            io::Molecule radial;
            const core::HexCoord center{6, -3};
            radial.atoms.push_back({core::AtomType::Quicksilver, center});
            const std::array<core::HexCoord, 4> leaves{{{1, 0}, {-1, 1}, {-1, 0}, {1, -1}}};
            const std::array<core::AtomType, 4> types{{core::AtomType::Iron, core::AtomType::Gold,
                                                    core::AtomType::Iron, core::AtomType::Silver}};
            for (int i = 0; i < 4; ++i) {
                const auto cell = core::Add(center, leaves[i]);
                radial.atoms.push_back({types[i], cell});
                radial.bonds.push_back({1, cell, center});
            }
            std::reverse(radial.atoms.begin(), radial.atoms.end());
            p.outputs = {{{{core::AtomType::Copper, {-5, 2}}}, {}}, radial};
            puzzleFile = scratch.Path() / "generated.puzzle";
            WriteWheelPuzzle(puzzleFile, p);
        }
        ASSERT_TRUE(SupportsRadialProjectionAssembly(p));
        auto machine = CompileRadialProjectionAssembly(p);
        EXPECT_EQ(std::count_if(machine.objects.begin(), machine.objects.end(),
            [](const auto& part) { return part.typeName == "glyph-projection"; }), 1);
        EXPECT_EQ(std::count_if(machine.objects.begin(), machine.objects.end(),
            [](const auto& part) { return part.typeName == "out-std"; }), 2);
        const auto machineFile = scratch.Path() / "candidate.solution";
        io::BinaryWriter::WriteSolutionFile(machineFile, machine);
        auto report = io::VerifySolutionFiles(puzzleFile, machineFile, 1000000, 10000);
        ASSERT_TRUE(report.Passed()) << report.detail << " cycle=" << report.errorCycle;
        EXPECT_EQ(report.authority, io::VerificationAuthority::LegacyGate);
        EXPECT_EQ(report.metrics.cost, 90);
        machine.metrics = report.metrics;
        io::BinaryWriter::WriteSolutionFile(machineFile, machine);
        const auto measured = io::VerifySolutionFiles(puzzleFile, machineFile, 1000000, 10000);
        ASSERT_TRUE(measured.Passed()) << measured.detail;
        EXPECT_EQ(measured.metrics.cycles, report.metrics.cycles);
        EXPECT_EQ(measured.metrics.area, report.metrics.area);
        EXPECT_EQ(measured.metrics.cost, report.metrics.cost);
        EXPECT_EQ(measured.metrics.instructionCount, report.metrics.instructionCount);
        std::cout << "Controlled radial projection generated=" << generated
                  << " cost=" << measured.metrics.cost << " cycles=" << measured.metrics.cycles
                  << " area=" << measured.metrics.area
                  << " instructions=" << measured.metrics.instructionCount << '\n';
        if (generated) {
            // Removing delivery leaves an unconverted atom at the real singleton outlet.
            machine.metrics = {};
            machine.objects.back().instructions = {{'O', machine.objects.back().instructions.back().parameter}};
            io::BinaryWriter::WriteSolutionFile(machineFile, machine);
            const auto wrong = io::VerifySolutionFiles(puzzleFile, machineFile, 1000000, 10000);
            EXPECT_EQ(wrong.status, io::VerificationStatus::Rejected) << wrong.detail;
            EXPECT_GT(wrong.errorCycle, 0);
            std::cout << "Missing Mercury doses rejected: " << wrong.detail
                      << " cycle=" << wrong.errorCycle << '\n';
        }
    }
}

void CheckWheelNative(core::AtomType element, bool swapped, bool wrongPose) {
    SCOPED_TRACE(static_cast<int>(element));
    SCOPED_TRACE(swapped);
    FiniteRetentionScope retention;
    test::ScopedTestDirectory scratch;
    const auto root = opusoptima::test::RepoRoot(__FILE__);
    auto actual = io::BinaryParser::ParsePuzzleFile(
        root / "asset/puzzle/edge-cases/row-transfer-wheel-second-source.puzzle");
    for (auto& atom : actual.outputs[0].atoms)
        if (atom.type == core::AtomType::Water) atom.type = element;
    if (swapped) std::swap(actual.inputs[0], actual.inputs[1]);
    auto proxy = actual;
    for (auto& atom : proxy.outputs[0].atoms)
        if (atom.type != core::AtomType::Quicksilver) atom.type = core::AtomType::Salt;
    const auto puzzlePath = scratch.Path() / "input.puzzle";
    const auto solutionPath = scratch.Path() / "candidate.solution";
    WriteWheelPuzzle(puzzlePath, actual);
    RowCompletionTransfer transfer;
    transfer.wheelSecondSingletonSource = transfer.secondSingletonSource = true;
    transfer.coproductAssembly = transfer.surplusRetention = true;
    transfer.mode = io::VerificationMode::LegacyGate;
    bool called = false;
    transfer.nativeGate = [&](const auto& candidate, auto deadline) {
        called = true;
        auto machine = candidate;
        if (wrongPose)
            for (auto& object : machine.objects)
                if (object.typeName == "baron")
                    object.bodyData.at(1) = (object.bodyData.at(1) + 1) % 6;
        io::BinaryWriter::WriteSolutionFile(solutionPath, machine);
        io::VerificationOptions options;
        options.mode = io::VerificationMode::LegacyGate;
        options.outerDeadline = deadline;
        return io::VerifySolutionFiles(puzzlePath, solutionPath, options);
    };
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    const auto plan = AssemblyPlanner::Plan(proxy, false, deadline);
    auto compile = [&] {
        return CompileRows(proxy, plan, nullptr, nullptr, nullptr, true, nullptr, false, 6,
            nullptr, nullptr, 3, false, false, false, false, nullptr, nullptr, false,
            &actual, nullptr, deadline, {}, nullptr, &transfer);
    };
    if (wrongPose) {
        EXPECT_THROW((void)compile(), ExecutionIR::LayoutFailure);
        EXPECT_TRUE(called);
        EXPECT_FALSE(transfer.published);
        EXPECT_FALSE(transfer.verification.Passed());
        return;
    }
    auto solution = compile();
    ASSERT_TRUE(called);
    ASSERT_TRUE(transfer.published);
    ASSERT_TRUE(transfer.verification.Passed()) << transfer.verification.detail;
    EXPECT_EQ(transfer.verification.authority, io::VerificationAuthority::LegacyGate);
    solution.metrics = transfer.verification.metrics;
    io::BinaryWriter::WriteSolutionFile(solutionPath, solution);
    io::VerificationOptions options;
    options.mode = io::VerificationMode::LegacyGate;
    const auto rewritten = io::VerifySolutionFiles(puzzlePath, solutionPath, options);
    ASSERT_TRUE(rewritten.Passed()) << rewritten.detail;
    EXPECT_EQ(rewritten.authority, io::VerificationAuthority::LegacyGate);
    EXPECT_EQ(rewritten.metrics.cost, solution.metrics.cost);
    EXPECT_EQ(rewritten.metrics.cycles, solution.metrics.cycles);
    EXPECT_EQ(rewritten.metrics.area, solution.metrics.area);
    EXPECT_EQ(rewritten.metrics.instructionCount, solution.metrics.instructionCount);
}

TEST(WheelSecondSourceAssemblyNative, AllCardinalsPassGateAndMetricRewrite) {
    for (const auto element : {core::AtomType::Air, core::AtomType::Earth,
                              core::AtomType::Fire, core::AtomType::Water})
        for (bool swapped : {false, true}) CheckWheelNative(element, swapped, false);
}

TEST(WheelSecondSourceAssemblyNative, WrongStaticPoseIsRejected) {
    for (const auto element : {core::AtomType::Air, core::AtomType::Earth,
                              core::AtomType::Fire, core::AtomType::Water})
        CheckWheelNative(element, false, true);
}
TEST(WheelSecondSourceAssemblyNative, CollectVerifiedReachesBoundedWheelFamily) {
    FiniteRetentionScope retention;
    const auto root = opusoptima::test::RepoRoot(__FILE__);
    for (const auto element : {core::AtomType::Air, core::AtomType::Earth,
                              core::AtomType::Fire, core::AtomType::Water}) {
        for (bool swapped : {false, true}) {
            SCOPED_TRACE(static_cast<int>(element));
            SCOPED_TRACE(swapped);
            auto puzzle = io::BinaryParser::ParsePuzzleFile(
                root / "asset/puzzle/edge-cases/row-transfer-wheel-second-source.puzzle");
            for (auto& atom : puzzle.outputs[0].atoms)
                if (atom.type == core::AtomType::Water) atom.type = element;
            if (swapped) std::swap(puzzle.inputs[0], puzzle.inputs[1]);
            test::ScopedTestDirectory scratch;
            const auto puzzlePath = scratch.Path() / "input.puzzle";
            WriteWheelPuzzle(puzzlePath, puzzle);
            const auto searchDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
            io::VerificationOptions options;
            options.mode = io::VerificationMode::LegacyGate;
            options.outerDeadline = searchDeadline;
            io::CandidateArchive archive(puzzlePath, scratch.Path() / "output.solution", options);
            AssemblySolver solver;
            solver.SetDeadline(searchDeadline);
            int wheelCalls = 0;
            std::optional<io::VerificationReport> wheelReport;
            const auto solution = solver.CollectVerified(puzzle,
                [&](const io::SolutionData& candidate, const std::string& strategy) {
                    const bool firstPending = !archive.First();
                    auto report = archive.Submit(candidate, strategy, solver.Deadline());
                    if (strategy == "finite-wheel-second-source") {
                        ++wheelCalls;
                        wheelReport = report;
                    }
                    if (firstPending && report.Passed())
                        solver.SetDeadline(searchDeadline - std::chrono::seconds(2));
                    return report;
                }, 3);
            (void)solution;
            ASSERT_TRUE(archive.First());
            EXPECT_EQ(wheelCalls, 1);
            ASSERT_TRUE(wheelReport.has_value());
            EXPECT_TRUE(wheelReport->Passed()) << wheelReport->detail;
            EXPECT_EQ(wheelReport->authority, io::VerificationAuthority::LegacyGate);
        }
    }
}
TEST(WheelAssemblyNative, CalcifiedSaltAndDerivedCardinalsPreserveOriginalInputs) {
    FiniteRetentionScope retention(nullptr);
    const auto root = opusoptima::test::RepoRoot(__FILE__);
    for (int recipe = 0; recipe < 4; ++recipe) {
        for (int rotation = 0; rotation < 6; ++rotation) {
            SCOPED_TRACE(recipe);
            SCOPED_TRACE(rotation);
            auto puzzle = io::BinaryParser::ParsePuzzleFile(
                root / "asset/puzzle/edge-cases/row-transfer-wheel-second-source.puzzle");
            puzzle.name = "calcified mixed wheel supply";
            puzzle.permissionBits |= 1ULL << 25;
            io::Molecule air;
            air.atoms.push_back({core::AtomType::Air, {0, 0}});
            puzzle.inputs = {air};
            puzzle.outputs[0].atoms = {
                {core::AtomType::Air, {0, 0}}, {core::AtomType::Salt, {1, 0}},
                {core::AtomType::Earth, {2, 0}}, {core::AtomType::Fire, {3, 0}}};
            puzzle.outputs[0].bonds = {
                {1, {0, 0}, {1, 0}}, {1, {1, 0}, {2, 0}}, {1, {2, 0}, {3, 0}}};
            if (recipe == 1) {
                io::Molecule mercury;
                mercury.atoms.push_back({core::AtomType::Quicksilver, {0, 0}});
                puzzle.inputs.push_back(mercury);
                puzzle.inputs.push_back(mercury);
                puzzle.outputs[0].atoms[0].type = core::AtomType::Earth;
                puzzle.outputs[0].atoms[2].type = core::AtomType::Quicksilver;
                puzzle.outputs[0].atoms[3].type = core::AtomType::Quicksilver;
                std::rotate(puzzle.inputs.begin(), puzzle.inputs.begin() + rotation % 3,
                            puzzle.inputs.end());
            }
            if (recipe >= 2) {
                puzzle.outputs[0].atoms.push_back({core::AtomType::Water, {4, 0}});
                puzzle.outputs[0].bonds.push_back({1, {3, 0}, {4, 0}});
            }
            if (recipe == 3) {
                puzzle.inputs[0].atoms[0].type = core::AtomType::Salt;
                puzzle.permissionBits &= ~(1ULL << 12);
            }
            auto transform = [&](core::HexCoord cell, core::HexCoord shift) {
                for (int turn = 0; turn < rotation; ++turn) cell = core::RotateClockwise(cell);
                return core::Add(cell, shift);
            };
            for (auto& input : puzzle.inputs)
                for (auto& atom : input.atoms) atom.position = transform(atom.position, {2, -1});
            for (auto& atom : puzzle.outputs[0].atoms)
                atom.position = transform(atom.position, {4, -3});
            for (auto& bond : puzzle.outputs[0].bonds) {
                bond.from = transform(bond.from, {4, -3});
                bond.to = transform(bond.to, {4, -3});
            }
            if (rotation % 2) {
                std::reverse(puzzle.outputs[0].atoms.begin(), puzzle.outputs[0].atoms.end());
                std::reverse(puzzle.outputs[0].bonds.begin(), puzzle.outputs[0].bonds.end());
            }
            if (rotation == 0)
                for (int bit : {12, 13, 25, 28}) {
                    if (recipe == 3 && bit == 12) continue;
                    auto denied = puzzle;
                    denied.permissionBits &= ~(1ULL << bit);
                    EXPECT_THROW((void)CompileWheelAssembly(denied), std::invalid_argument);
                }
            auto machine = CompileWheelAssembly(puzzle);
            for (const auto& object : machine.objects)
                if (object.typeName == "input") {
                    ASSERT_GE(object.bodyData.at(2), 0);
                    EXPECT_LT(static_cast<std::size_t>(object.bodyData.at(2)), puzzle.inputs.size());
                }
            test::ScopedTestDirectory scratch;
            const auto puzzlePath = scratch.Path() / "input.puzzle";
            const auto solutionPath = scratch.Path() / "candidate.solution";
            WriteWheelPuzzle(puzzlePath, puzzle);
            io::BinaryWriter::WriteSolutionFile(solutionPath, machine);
            io::VerificationOptions options;
            options.mode = io::VerificationMode::LegacyGate;
            const auto report = io::VerifySolutionFiles(puzzlePath, solutionPath, options);
            ASSERT_TRUE(report.Passed()) << report.detail;
            EXPECT_EQ(report.authority, io::VerificationAuthority::LegacyGate);
            machine.metrics = report.metrics;
            io::BinaryWriter::WriteSolutionFile(solutionPath, machine);
            const auto rewritten = io::VerifySolutionFiles(puzzlePath, solutionPath, options);
            ASSERT_TRUE(rewritten.Passed()) << rewritten.detail;
            EXPECT_EQ(rewritten.metrics.cost, machine.metrics.cost);
            EXPECT_EQ(rewritten.metrics.cycles, machine.metrics.cycles);
            EXPECT_EQ(rewritten.metrics.area, machine.metrics.area);
            EXPECT_EQ(rewritten.metrics.instructionCount, machine.metrics.instructionCount);
        }
    }
}
io::PuzzleData StaticWheelPuzzle(int recipe) {
    io::PuzzleData puzzle;
    puzzle.version = 3;
    puzzle.name = "typed static wheel fixture";
    puzzle.outputMultiplier = 1;
    puzzle.permissionBits = (1ULL << 0) | (1ULL << 2) | (1ULL << 3) |
        (1ULL << 8) | (1ULL << 12) | (1ULL << 13) | (1ULL << 22) |
        (1ULL << 23) | (1ULL << 25) | (1ULL << 28);
    puzzle.inputs = {
        {{{core::AtomType::Air,{0,0}}},{}},
        {{{core::AtomType::Quicksilver,{0,0}}},{}},
        {{{core::AtomType::Quicksilver,{0,0}}},{}}};
    puzzle.outputs = {
        {{{core::AtomType::Earth,{0,0}}, {core::AtomType::Salt,{1,0}},
          {core::AtomType::Quicksilver,{2,0}}, {core::AtomType::Quicksilver,{3,0}}},
         {{1,{0,0},{1,0}}, {1,{1,0},{2,0}}, {1,{2,0},{3,0}}}}};
    if (recipe == 1) {
        puzzle.inputs[2].atoms[0].type = core::AtomType::Tin;
        puzzle.outputs = {
            {{{core::AtomType::Tin,{0,0}}, {core::AtomType::Salt,{1,0}}, {core::AtomType::Fire,{0,1}}},
             {{1,{0,0},{1,0}}, {1,{0,0},{0,1}}}},
            {{{core::AtomType::Quicksilver,{0,0}}, {core::AtomType::Water,{1,0}}, {core::AtomType::Air,{0,1}}},
             {{1,{0,0},{1,0}}, {1,{0,0},{0,1}}}}};
    }
    if (recipe >= 2) {
        puzzle.inputs.resize(1);
        puzzle.outputs = {
            {{{core::AtomType::Water,{0,0}}, {core::AtomType::Water,{0,1}}, {core::AtomType::Salt,{-1,0}}},
             {{1,{0,0},{0,1}}, {1,{0,0},{-1,0}}}}};
        if (recipe == 3) {
            puzzle.inputs[0].atoms[0].type = core::AtomType::Salt;
            puzzle.permissionBits &= ~(1ULL << 12);
        }
    }
    return puzzle;
}

TEST(StaticEmbeddingWheelNative, TypedSourcesSharedCatalystsAndTransforms) {
    FiniteRetentionScope retention(nullptr);
    for (int recipe = 0; recipe < 4; ++recipe)
        for (int rotation = 0; rotation < 6; ++rotation) {
            SCOPED_TRACE(recipe);
            SCOPED_TRACE(rotation);
            auto puzzle = StaticWheelPuzzle(recipe);
            std::rotate(puzzle.inputs.begin(),
                        puzzle.inputs.begin() + rotation % puzzle.inputs.size(), puzzle.inputs.end());
            auto transform = [&](core::HexCoord cell) {
                for (int i = 0; i < rotation; ++i) cell = core::RotateClockwise(cell);
                return core::Add(cell,{2,-1});
            };
            for (auto& input : puzzle.inputs)
                input.atoms[0].position = transform(input.atoms[0].position);
            for (auto& output : puzzle.outputs) {
                for (auto& atom : output.atoms) atom.position = transform(atom.position);
                for (auto& bond : output.bonds) {
                    bond.from = transform(bond.from); bond.to = transform(bond.to);
                }
                if (rotation % 2) {
                    std::reverse(output.atoms.begin(),output.atoms.end());
                    std::reverse(output.bonds.begin(),output.bonds.end());
                }
            }
            auto machine = CompileStaticEmbedding(puzzle,
                std::chrono::steady_clock::now() + std::chrono::seconds(3));
            int wheels = 0;
            for (const auto& object : machine.objects) {
                if (object.typeName == "input") {
                    ASSERT_GE(object.bodyData.at(2),0);
                    EXPECT_LT(static_cast<std::size_t>(object.bodyData.at(2)),puzzle.inputs.size());
                }
                if (object.typeName == "baron") {
                    ++wheels;
                    EXPECT_TRUE(object.instructions.empty());
                }
            }
            ASSERT_GT(wheels,0);
            if (recipe >= 2 && rotation == 0) EXPECT_EQ(wheels,1);
            test::ScopedTestDirectory scratch;
            const auto input = scratch.Path()/"input.puzzle";
            const auto output = scratch.Path()/"candidate.solution";
            WriteWheelPuzzle(input,puzzle);
            io::BinaryWriter::WriteSolutionFile(output,machine);
            io::VerificationOptions options;
            options.mode = io::VerificationMode::LegacyGate;
            const auto report = io::VerifySolutionFiles(input,output,options);
            ASSERT_TRUE(report.Passed()) << report.detail;
            EXPECT_EQ(report.authority,io::VerificationAuthority::LegacyGate);
            machine.metrics = report.metrics;
            io::BinaryWriter::WriteSolutionFile(output,machine);
            const auto rewritten = io::VerifySolutionFiles(input,output,options);
            ASSERT_TRUE(rewritten.Passed()) << rewritten.detail;
            EXPECT_EQ(rewritten.authority,io::VerificationAuthority::LegacyGate);
            EXPECT_EQ(rewritten.metrics.cycles,machine.metrics.cycles);
            EXPECT_EQ(rewritten.metrics.cost,machine.metrics.cost);
            EXPECT_EQ(rewritten.metrics.area,machine.metrics.area);
        }
}

TEST(StaticEmbeddingWheelNative, WrongPoseRejectedAndPermissionRefusals) {
    auto puzzle = StaticWheelPuzzle(0);
    for (int bit : {0,8,12,13,25,28}) {
        auto denied = puzzle;
        denied.permissionBits &= ~(1ULL << bit);
        EXPECT_THROW((void)CompileStaticEmbedding(denied),std::invalid_argument) << bit;
    }
    auto machine = CompileStaticEmbedding(puzzle);
    bool changed = false;
    for (auto& object : machine.objects)
        if (object.typeName == "baron") {
            object.bodyData.at(1) = (object.bodyData.at(1) + 1) % 6;
            changed = true;
        }
    ASSERT_TRUE(changed);
    test::ScopedTestDirectory scratch;
    const auto input = scratch.Path()/"input.puzzle";
    const auto output = scratch.Path()/"bad.solution";
    WriteWheelPuzzle(input,puzzle);
    io::BinaryWriter::WriteSolutionFile(output,machine);
    io::VerificationOptions options;
    options.mode = io::VerificationMode::LegacyGate;
    EXPECT_FALSE(io::VerifySolutionFiles(input,output,options).Passed());
}

TEST(StaticEmbeddingWheelNative, CollectionPreservesFirstAndImprovesActualSlots) {
    FiniteRetentionScope retention(nullptr);
    const auto puzzle = StaticWheelPuzzle(0);
    test::ScopedTestDirectory scratch;
    const auto input = scratch.Path()/"input.puzzle";
    WriteWheelPuzzle(input,puzzle);
    const auto searchDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    io::VerificationOptions options;
    options.mode = io::VerificationMode::LegacyGate;
    options.outerDeadline = searchDeadline;
    io::CandidateArchive archive(input,scratch.Path()/"output.solution",options);
    AssemblySolver solver;
    solver.SetDeadline(searchDeadline);
    int embeddingCalls = 0;
    std::optional<io::VerificationReport> embedding;
    std::string firstHash;
    (void)solver.CollectVerified(puzzle,
        [&](const io::SolutionData& candidate, const std::string& strategy) {
            const bool firstPending = !archive.First();
            auto report = archive.Submit(candidate,strategy,solver.Deadline());
            if (strategy == "portfolio-static-embedding") {
                ++embeddingCalls;
                embedding = report;
            }
            if (firstPending && report.Passed()) {
                firstHash = archive.First()->solutionHash;
                solver.SetDeadline(searchDeadline - std::chrono::seconds(2));
            }
            return report;
        },3);
    ASSERT_TRUE(archive.First());
    EXPECT_EQ(archive.First()->solutionHash,firstHash);
    EXPECT_EQ(embeddingCalls,1);
    ASSERT_TRUE(embedding.has_value());
    ASSERT_TRUE(embedding->Passed()) << embedding->detail;
    EXPECT_EQ(embedding->authority,io::VerificationAuthority::LegacyGate);
    EXPECT_LT(embedding->metrics.cycles,archive.First()->verification.metrics.cycles);
    for (const auto& slot : archive.Slots()) ASSERT_TRUE(slot);
    EXPECT_EQ(archive.Slots()[1]->verification.metrics.cycles,embedding->metrics.cycles);
}
} }
#endif
