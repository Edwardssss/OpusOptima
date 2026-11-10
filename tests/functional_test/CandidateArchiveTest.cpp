#include "../unit_test/TestOutputDirectory.hpp"
#include <optional>
#include <cstdlib>
#include "CandidateArchive.hpp"
#include "BinaryWriter.hpp"
#include "AssemblySolver.hpp"
#include "LocalOptimizer.hpp"
#include "MaterialPlanning.hpp"
#include "SerialArmExecution.hpp"
#include "SingleAtomSolver.hpp"
#include <gtest/gtest.h>
#include <fstream>
#include <cstdlib>
#include <iostream>
#include <iterator>
#include <sstream>
#include <thread>
#include "RepoRoot.hpp"

using namespace opusoptima::io;
namespace {
std::filesystem::path Root() { return opusoptima::test::RepoRoot(__FILE__); }
struct Workspace {
    opusoptima::test::ScopedTestDirectory scratch;
    std::filesystem::path path = scratch.Path();
};
}

TEST(CandidateArchive, IndependentNativeStagesRejectBeforePublication) {
    using namespace opusoptima::io;
    Workspace w;
    const auto input = Root() / "asset/puzzle/easy/easy-conduit.puzzle";
    const auto candidate = BinaryParser::ParseSolutionFile(
        Root() / "asset/solution/easy/easy-conduit-easy-conduit-1.solution");
    const auto output = w.path / "old.solution";
    { std::ofstream old(output); old << "old"; }
    const auto previous = BinaryParser::ReadAllBytes(output);
    VerificationOptions options;
    options.mode = VerificationMode::Independent;
    options.independentWorkerExecutable = w.path / "missing-worker";
    CandidateArchive archive(input, output, options);
    for (const auto* stage : {"shared-plan-calcify-animismus", "shared-plan-wheel",
                              "shared-plan-wheel:parent=012345", "finite-wheel-second-source",
                              "wheel-assembly-calcified-salt", "portfolio-static-embedding",
                              "portfolio-radial-wheel", "portfolio-radial-projection",
                              "portfolio-singleton-purification-chain",
                              "fallback-static-embedding", "portfolio-row-retimed",
                              "portfolio-shared-life-death", "closed-wide-metal-inventory"}) {
        SCOPED_TRACE(stage);
        const auto report = archive.Submit(candidate, stage);
        EXPECT_EQ(report.status, VerificationStatus::Unavailable);
        EXPECT_EQ(report.detail, "stage requires LegacyGate or legacy-primary Shadow");
        EXPECT_FALSE(archive.First());
        for (const auto& slot : archive.Slots()) EXPECT_FALSE(slot);
        EXPECT_TRUE(std::filesystem::is_empty(archive.Directory()));
        EXPECT_EQ(BinaryParser::ReadAllBytes(output), previous);
    }
}
TEST(CandidateArchive, Sha256KnownVectors) {
    EXPECT_EQ(CandidateSha256({}), "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    EXPECT_EQ(CandidateSha256({std::byte{'a'},std::byte{'b'},std::byte{'c'}}),
              "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
}
TEST(CandidateArchive, LocalTrackEndpointEncodingAndNoOp) {
    SolutionData parent;
    SolutionObject track;
    track.typeName = "track";
    track.bodyData = {1,0,0,0,3,0,0,1,0,2,0,7};
    parent.objects.push_back(track);
    auto head = opusoptima::solvers::LocalTrial(parent, 6);
    auto tail = opusoptima::solvers::LocalTrial(parent, 7);
    EXPECT_EQ(head.objects[0].bodyData, (std::vector<int>{1,0,0,0,2,1,0,2,0,7}));
    EXPECT_EQ(tail.objects[0].bodyData, (std::vector<int>{1,0,0,0,2,0,0,1,0,7}));
    EXPECT_EQ(BinaryWriter::WriteSolution(opusoptima::solvers::LocalTrial({}, 2)),
              BinaryWriter::WriteSolution({}));
}
TEST(CandidateArchive, UsedRailEnvelopeIsExplicitAndConservative) {
    const auto* old = std::getenv("OPUSOPTIMA_USED_RAIL");
    const std::optional<std::string> saved = old ? std::optional<std::string>(old) : std::nullopt;
    SolutionData parent;
    parent.objects.push_back({"track", {0, 0}, {}, {1, 0, 0, 0, 5, 0, 0, 1, 0, 2, 0, 3, 0, 4, 0, 7}});
    parent.objects.push_back({"piston", {1, 0}, {}, {}});
    for (const auto& [time, action] : std::vector<std::pair<int,char>>{{0,'G'},{1,'A'},{2,'a'},{3,'g'}})
        parent.objects[1].instructions.push_back({action, time});
    unsetenv("OPUSOPTIMA_USED_RAIL");
    const auto baseline = BinaryWriter::WriteSolution(opusoptima::solvers::LocalTrial(parent, 7));
    for (const char* mode : {"off", "unknown"}) {
        setenv("OPUSOPTIMA_USED_RAIL", mode, 1);
        EXPECT_EQ(BinaryWriter::WriteSolution(opusoptima::solvers::LocalTrial(parent, 7)), baseline);
    }
    setenv("OPUSOPTIMA_USED_RAIL", "on", 1);
    const auto trimmed = opusoptima::solvers::LocalTrial(parent, 7);
    EXPECT_EQ(trimmed.objects[0].bodyData, (std::vector<int>{1,0,0,0,2,1,0,2,0,7}));
    EXPECT_EQ(trimmed.objects[1].instructions.size(), 4);
    for (int failure = 0; failure < 10; ++failure) {
        auto invalid = parent;
        if (failure == 0) invalid.objects[1].instructions[0].action = 'O';
        if (failure == 1) invalid.objects[1].instructions[3].action = 'A';
        if (failure == 2) invalid.objects[0].bodyData[7] = 4;
        if (failure == 3) invalid.objects.push_back(parent.objects[1]);
        if (failure == 4) invalid.objects[1].instructions[1].parameter = 3;
        if (failure == 5) invalid.objects[1].position = {9,9};
        // Empty-return compaction changes swept geometry: retain the old opportunity.
        if (failure == 6) {
            invalid.objects[1].instructions.clear();
            for (const auto& [time, action] : std::vector<std::pair<int,char>>{
                     {0,'G'},{1,'g'},{2,'A'},{3,'a'},{4,'G'},{5,'g'}})
                invalid.objects[1].instructions.push_back({action, time});
        }
        if (failure == 7) invalid.objects[0].bodyData[1] = 1;
        if (failure == 8) {
            invalid.objects[0].bodyData = {1,0,0,0,4,0,0,1,0,1,-1,0,-1,7};
        }
        if (failure == 9) invalid.objects[1].position = {3,0};
        setenv("OPUSOPTIMA_USED_RAIL", "off", 1);
        const auto legacy = BinaryWriter::WriteSolution(opusoptima::solvers::LocalTrial(invalid, 7));
        setenv("OPUSOPTIMA_USED_RAIL", "on", 1);
        EXPECT_EQ(BinaryWriter::WriteSolution(opusoptima::solvers::LocalTrial(invalid, 7)), legacy);
    }
    if (saved) setenv("OPUSOPTIMA_USED_RAIL", saved->c_str(), 1);
    else unsetenv("OPUSOPTIMA_USED_RAIL");
}
TEST(CandidateArchive, SharedTrackEnvelopesDefaultToSparseClosedTapes) {
    const auto* old = std::getenv("OPUSOPTIMA_TRACK_ENVELOPE");
    const std::optional<std::string> saved = old ? std::optional<std::string>(old) : std::nullopt;
    SolutionData parent;
    parent.objects.push_back({"track", {0, 0}, {},
        {1, 0, 0, 0, 6, 0, 0, 1, 0, 2, 0, 3, 0, 4, 0, 5, 0, 7}});
    parent.objects.push_back({"track", {0, 5}, {}, parent.objects.front().bodyData});
    parent.objects.push_back({"piston", {1, 0}, {{'G', 0}, {'A', 3}, {'a', 7}, {'g', 10}, {'B', 12}}, {}});
    parent.objects.push_back({"arm1", {3, 0}, {{'G', 0}, {'a', 2}, {'A', 8}, {'g', 10}, {'O', 12}}, {}});
    parent.objects.push_back({"piston", {2, 5}, {{'G', 0}, {'A', 4}, {'a', 6}, {'g', 10}}, {}});
    setenv("OPUSOPTIMA_TRACK_ENVELOPE", "off", 1);
    const auto baseline = BinaryWriter::WriteSolution(opusoptima::solvers::LocalTrial(parent, 6));
    const auto tail = BinaryWriter::WriteSolution(opusoptima::solvers::LocalTrial(parent, 7));
    for (const char* mode : {"off", "unknown", "ON", "", "on "}) {
        setenv("OPUSOPTIMA_TRACK_ENVELOPE", mode, 1);
        EXPECT_EQ(BinaryWriter::WriteSolution(opusoptima::solvers::LocalTrial(parent, 6)), baseline);
    }
    unsetenv("OPUSOPTIMA_TRACK_ENVELOPE");
    const auto trimmed = opusoptima::solvers::LocalTrial(parent, 6);
    setenv("OPUSOPTIMA_TRACK_ENVELOPE", "on", 1);
    EXPECT_EQ(BinaryWriter::WriteSolution(opusoptima::solvers::LocalTrial(parent, 6)),
              BinaryWriter::WriteSolution(trimmed));
    unsetenv("OPUSOPTIMA_TRACK_ENVELOPE");
    EXPECT_EQ(trimmed.objects[0].bodyData,
              (std::vector<int>{1, 0, 0, 0, 3, 1, 0, 2, 0, 3, 0, 7}));
    EXPECT_EQ(trimmed.objects[1].bodyData,
              (std::vector<int>{1, 0, 0, 0, 2, 2, 0, 3, 0, 7}));
    auto restored = trimmed;
    restored.objects[0] = parent.objects[0];
    restored.objects[1] = parent.objects[1];
    EXPECT_EQ(BinaryWriter::WriteSolution(restored), BinaryWriter::WriteSolution(parent));
    EXPECT_EQ(BinaryWriter::WriteSolution(opusoptima::solvers::LocalTrial(parent, 7)), tail);
    auto stationary = parent;
    stationary.objects.push_back({"arm1", {0, 0}, {}, {}});
    EXPECT_EQ(opusoptima::solvers::LocalTrial(stationary, 6).objects[0].bodyData[4], 4);
    for (int failure = 0; failure < 11; ++failure) {
        SCOPED_TRACE(failure);
        auto invalid = parent;
        if (failure == 0) invalid.objects[2].instructions[0].action = 'X';
        if (failure == 1) invalid.objects[2].instructions[0].action = 'C';
        if (failure == 2) invalid.objects[2].instructions[2].action = 'A';
        if (failure == 3) invalid.objects[2].instructions[1].parameter = 0;
        if (failure == 4) invalid.objects[1].position = {0, 0};
        if (failure == 5) invalid.objects[0].bodyData[1] = 1;
        if (failure == 6) invalid.objects[0].bodyData[7] = 4;
        if (failure == 7) invalid.objects[2].position = {9, 9};
        if (failure == 8) invalid.objects[2].typeName = "arm2";
        if (failure == 9) invalid.objects[2].instructions[0].parameter = -1;
        if (failure == 10) {
            invalid.objects.resize(1);
            invalid.objects[0].bodyData = {1, 0, 0, 0, 7,
                0, 0, 1, 0, 2, 0, 2, 1, 1, 1, 0, 1, -1, 2, 7};
            invalid.objects.push_back({"arm1", {1, 0},
                {{'A', 0}, {'A', 1}, {'A', 2}, {'a', 3}, {'a', 4}, {'a', 5}}, {}});
        }
        setenv("OPUSOPTIMA_TRACK_ENVELOPE", "off", 1);
        const auto legacy = BinaryWriter::WriteSolution(opusoptima::solvers::LocalTrial(invalid, 6));
        setenv("OPUSOPTIMA_TRACK_ENVELOPE", "on", 1);
        EXPECT_EQ(BinaryWriter::WriteSolution(opusoptima::solvers::LocalTrial(invalid, 6)), legacy);
        unsetenv("OPUSOPTIMA_TRACK_ENVELOPE");
        EXPECT_EQ(BinaryWriter::WriteSolution(opusoptima::solvers::LocalTrial(invalid, 6)), legacy);
    }
    if (saved) setenv("OPUSOPTIMA_TRACK_ENVELOPE", saved->c_str(), 1);
    else unsetenv("OPUSOPTIMA_TRACK_ENVELOPE");
}
TEST(CandidateArchive, SharedBonderProbeIsExplicitAndBounded) {
    const auto* old = std::getenv("OPUSOPTIMA_SHARED_BONDER");
    const std::optional<std::string> saved = old ? std::optional<std::string>(old) : std::nullopt;
    SolutionData parent;
    parent.objects.push_back({"bonder", {3, 0}, {}, {1, 0, 0, 0, 0}});
    parent.objects.push_back({"bonder", {0, 4}, {}, {1, 1, 0, 0, 0}});
    for (const char* mode : {"off", "unknown"}) {
        setenv("OPUSOPTIMA_SHARED_BONDER", mode, 1);
        EXPECT_EQ(opusoptima::solvers::LocalTrial(parent, 3).objects.size(), 2);
    }
    unsetenv("OPUSOPTIMA_SHARED_BONDER");
    const auto baseline = BinaryWriter::WriteSolution(opusoptima::solvers::LocalTrial(parent, 3));
    setenv("OPUSOPTIMA_SHARED_BONDER", "off", 1);
    EXPECT_EQ(BinaryWriter::WriteSolution(opusoptima::solvers::LocalTrial(parent, 3)), baseline);
    setenv("OPUSOPTIMA_SHARED_BONDER", "on", 1);
    const auto trial = opusoptima::solvers::LocalTrial(parent, 3);
    ASSERT_EQ(trial.objects.size(), 1);
    EXPECT_EQ(trial.objects[0].position, parent.objects[1].position);
    EXPECT_EQ(trial.objects[0].bodyData, parent.objects[1].bodyData);
    EXPECT_EQ(parent.objects.size(), 2);
    SolutionData duplicate = parent;
    auto reversed = parent.objects[1];
    reversed.position = {0, 5};
    reversed.bodyData[1] = 4;
    duplicate.objects.push_back(reversed);
    const auto shared = opusoptima::solvers::LocalTrial(duplicate, 3);
    ASSERT_EQ(shared.objects.size(), 2);
    EXPECT_EQ(BinaryWriter::WriteSolution(shared), BinaryWriter::WriteSolution(parent));
    EXPECT_TRUE(opusoptima::solvers::LocalTrial({}, 3).objects.empty());
    if (saved) setenv("OPUSOPTIMA_SHARED_BONDER", saved->c_str(), 1);
    else unsetenv("OPUSOPTIMA_SHARED_BONDER");
}
TEST(CandidateArchive, SingleArmEmptyReturnCompactionPreservesLoadedTape) {
    SolutionData parent;
    SolutionObject arm;
    arm.typeName = "piston";
    const std::string tape = "GAaEegAaEeRrGg";
    for (std::size_t i = 0; i < tape.size(); ++i)
        arm.instructions.push_back({tape[i], static_cast<int>(i)});
    parent.objects.push_back(arm);
    const auto legacyV5 = BinaryWriter::WriteSolution(opusoptima::solvers::LocalTrial(
        opusoptima::solvers::LocalTrial(parent,0),2));
    EXPECT_EQ(BinaryWriter::WriteSolution(opusoptima::solvers::LocalTrial(parent,5)),legacyV5);
    auto trial = opusoptima::solvers::LocalTrial(parent, 4);
    std::string actual;
    for (const auto& instruction : trial.objects[0].instructions) {
        EXPECT_EQ(instruction.parameter, static_cast<int>(actual.size()));
        actual += instruction.action;
    }
    EXPECT_EQ(actual, "GAaEegGg");
    const auto* oldEmpty = std::getenv("OPUSOPTIMA_EMPTY_RETURN");
    const std::optional<std::string> savedEmpty = oldEmpty ? std::optional<std::string>(oldEmpty) : std::nullopt;
    setenv("OPUSOPTIMA_EMPTY_RETURN", "off", 1);
    auto disabled = opusoptima::solvers::LocalTrial(parent, 4);
    EXPECT_EQ(disabled.objects[0].instructions.size(), tape.size());
    EXPECT_EQ(BinaryWriter::WriteSolution(opusoptima::solvers::LocalTrial(parent,5)),legacyV5);
    if (savedEmpty) setenv("OPUSOPTIMA_EMPTY_RETURN", savedEmpty->c_str(), 1);
    else unsetenv("OPUSOPTIMA_EMPTY_RETURN");
    const auto* oldSerial = std::getenv("OPUSOPTIMA_SERIAL_RETURN");
    const std::optional<std::string> savedSerial = oldSerial ? std::optional<std::string>(oldSerial) : std::nullopt;
    setenv("OPUSOPTIMA_SERIAL_RETURN", "1", 1);
    disabled = opusoptima::solvers::LocalTrial(parent, 4);
    EXPECT_EQ(disabled.objects[0].instructions.size(), tape.size());
    EXPECT_EQ(BinaryWriter::WriteSolution(opusoptima::solvers::LocalTrial(parent,5)),legacyV5);
    if (savedSerial) setenv("OPUSOPTIMA_SERIAL_RETURN", savedSerial->c_str(), 1);
    else unsetenv("OPUSOPTIMA_SERIAL_RETURN");
    parent.objects.push_back(arm);
    trial = opusoptima::solvers::LocalTrial(parent, 4);
    EXPECT_EQ(trial.objects[0].instructions.size(), tape.size());
    parent.objects.pop_back();
    parent.objects[0].instructions[1].parameter = 3;
    trial = opusoptima::solvers::LocalTrial(parent, 4);
    EXPECT_EQ(trial.objects[0].instructions.size(), tape.size());
}
TEST(CandidateArchive, UnavailablePreservesEmptySlotsAndOutput) {
    Workspace w;
    auto output = w.path / "old.solution";
    std::ofstream(output) << "old";
    VerificationOptions options;
    options.workerExecutable = w.path / "missing-worker";
    CandidateArchive archive(Root() / "asset/puzzle/easy/easy-conduit.puzzle", output, options);
    EXPECT_FALSE(archive.Submit({}, "missing").Passed());
    EXPECT_FALSE(archive.First());
    for (const auto& slot : archive.Slots()) EXPECT_FALSE(slot);
    EXPECT_FALSE(archive.Publish(options).Passed());
    EXPECT_EQ(BinaryParser::ReadAllBytes(output).size(), 3);
}
TEST(CandidateArchive, ParallelEmptyReturnsConservativeBoundaries) {
    const auto legacy = [](const auto& parent) {
        return BinaryWriter::WriteSolution(opusoptima::solvers::LocalTrial(
            opusoptima::solvers::LocalTrial(parent,0),2));
    };
    SolutionData parent;
    SolutionObject arm;
    arm.typeName = "arm1"; arm.bodyData = {1,0,0,0,0};
    arm.instructions = {{'G',0},{'g',2},{'r',4},{'r',5},{'O',9}};
    parent.objects.push_back(arm);
    arm.position = {10,0}; arm.bodyData[4] = 1;
    arm.instructions = {{'G',0},{'g',2},{'R',6},{'O',9}};
    parent.objects.push_back(arm);
    const auto trial = opusoptima::solvers::LocalTrial(parent,4);
    EXPECT_EQ(trial.objects[0].instructions.back().parameter,6);
    EXPECT_EQ(trial.objects[1].instructions[2].parameter,4);
    EXPECT_EQ(trial.objects[1].instructions[2].action,'R');
    // Useful return compaction alone cannot replace legacy v5.
    EXPECT_EQ(BinaryWriter::WriteSolution(opusoptima::solvers::LocalTrial(parent,5)),legacy(parent));
    for (int kind=0; kind<5; ++kind) {
        auto invalid = parent;
        if (kind==0) invalid.objects[0].instructions[1].action='G';
        if (kind==1) invalid.objects[0].typeName="arm2";
        if (kind==2) invalid.objects[0].instructions.back().parameter=10;
        if (kind==3) invalid.objects[0].instructions.insert(
            invalid.objects[0].instructions.begin()+3, {'O',5});
        if (kind==4) invalid.objects[0].instructions[2].parameter=2;
        const auto result = opusoptima::solvers::LocalTrial(invalid,4);
        // Ineligible candidates retain the historical rotate-300 fallback timing.
        EXPECT_EQ(result.objects[1].instructions[2].parameter,6);
        EXPECT_EQ(result.objects[1].instructions.back().parameter,9);
    }

    parent.objects[0].instructions = {{'G',0},{'r',1},{'g',2},{'R',5},
        {'G',6},{'r',7},{'g',8},{'R',9},{'O',10}};
    parent.objects[1].instructions = {{'G',3},{'g',4},{'O',10}};
    const auto original = BinaryWriter::WriteSolution(parent);
    const auto prepositioned = opusoptima::solvers::LocalTrial(parent,4);
    EXPECT_EQ(prepositioned.objects[0].instructions[3].parameter,3);
    EXPECT_EQ(prepositioned.objects[0].instructions[4].parameter,5);
    for (std::size_t i=0; i<parent.objects.size(); ++i) {
        EXPECT_EQ(prepositioned.objects[i].position,parent.objects[i].position);
        EXPECT_EQ(prepositioned.objects[i].bodyData,parent.objects[i].bodyData);
        ASSERT_EQ(prepositioned.objects[i].instructions.size(),parent.objects[i].instructions.size());
        for (std::size_t j=0; j<parent.objects[i].instructions.size(); ++j)
            EXPECT_EQ(prepositioned.objects[i].instructions[j].action,parent.objects[i].instructions[j].action);
        EXPECT_EQ(prepositioned.objects[i].instructions.back().parameter,9);
    }
    EXPECT_EQ(BinaryWriter::WriteSolution(parent),original);
    // Prepositioning alone cannot replace legacy v5 either.
    EXPECT_EQ(BinaryWriter::WriteSolution(opusoptima::solvers::LocalTrial(parent,5)),legacy(parent));
    auto composable = parent;
    composable.objects[0].instructions[7].parameter = 11;
    for (auto& object : composable.objects) object.instructions.back().parameter = 15;
    const auto returnOnly = opusoptima::solvers::LocalTrial(composable,4);
    const auto composed = opusoptima::solvers::LocalTrial(composable,5);
    EXPECT_EQ(returnOnly.objects[0].instructions.back().parameter,12);
    EXPECT_EQ(composed.objects[0].instructions.back().parameter,9);
    EXPECT_EQ(BinaryWriter::WriteSolution(composed), BinaryWriter::WriteSolution(
        opusoptima::solvers::LocalTrial(returnOnly,4)));
    const auto* emptyValue = std::getenv("OPUSOPTIMA_EMPTY_RETURN");
    const std::optional<std::string> savedEmpty = emptyValue ?
        std::optional<std::string>(emptyValue) : std::nullopt;
    setenv("OPUSOPTIMA_EMPTY_RETURN","off",1);
    EXPECT_EQ(BinaryWriter::WriteSolution(opusoptima::solvers::LocalTrial(composable,5)),
              BinaryWriter::WriteSolution(composed));
    if (savedEmpty) setenv("OPUSOPTIMA_EMPTY_RETURN",savedEmpty->c_str(),1);
    else unsetenv("OPUSOPTIMA_EMPTY_RETURN");
    const auto* serialValue = std::getenv("OPUSOPTIMA_SERIAL_RETURN");
    const std::optional<std::string> savedSerial = serialValue ?
        std::optional<std::string>(serialValue) : std::nullopt;
    setenv("OPUSOPTIMA_SERIAL_RETURN","1",1);
    EXPECT_EQ(BinaryWriter::WriteSolution(opusoptima::solvers::LocalTrial(composable,5)),legacy(composable));
    unsetenv("OPUSOPTIMA_SERIAL_RETURN");
    for (int kind=0; kind<13; ++kind) {
        SCOPED_TRACE(kind);
        auto invalid = parent;
        if (kind==0) invalid.objects[1].typeName="piston";
        if (kind>=1 && kind<=4) invalid.objects[0].instructions[3].action="XC?B"[kind-1];
        if (kind==5) invalid.objects[0].instructions[3].parameter=2;
        if (kind==6) invalid.objects[0].instructions[0].parameter=-1;
        if (kind==7) invalid.objects[0].instructions[6].action='G';
        if (kind==8) invalid.objects[1].instructions.back().parameter=11;
        if (kind==9) invalid.objects[1].instructions.clear();
        if (kind==10) invalid.objects[0].instructions[2].action='G';
        if (kind==11) invalid.objects[0].instructions[3].parameter=3;
        if (kind==12) invalid.objects[1].instructions.pop_back();
        setenv("OPUSOPTIMA_SERIAL_RETURN","1",1);
        const auto fallback = BinaryWriter::WriteSolution(opusoptima::solvers::LocalTrial(invalid,4));
        unsetenv("OPUSOPTIMA_SERIAL_RETURN");
        EXPECT_EQ(BinaryWriter::WriteSolution(opusoptima::solvers::LocalTrial(invalid,4)),fallback);
        EXPECT_EQ(BinaryWriter::WriteSolution(opusoptima::solvers::LocalTrial(invalid,5)),legacy(invalid));
    }
    if (savedSerial) setenv("OPUSOPTIMA_SERIAL_RETURN",savedSerial->c_str(),1);
    else unsetenv("OPUSOPTIMA_SERIAL_RETURN");
}
#ifdef HAS_LIBVERIFY
TEST(CandidateArchive, RegisteredConstructionRelayEvidence) {
    const auto* input = std::getenv("OPUS_RELAY_PUZZLE");
    const auto* output = std::getenv("OPUS_RELAY_OUTPUT");
    if (!input || !output) GTEST_SKIP() << "Optional registered construction measurement";
    const auto puzzle = BinaryParser::ParsePuzzleFile(input);
    VerificationOptions options;
    options.outerDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    CandidateArchive archive(input, output, options);
    std::optional<SolutionData> parent, child;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    opusoptima::solvers::MaterialPlanningLimits limits;
    limits.deadline = deadline;
    const auto plan = opusoptima::solvers::SearchMaterialExecution(puzzle, limits);
    ASSERT_TRUE(plan.plan) << plan.detail;
    if (std::getenv("OPUS_RELAY_INTEGRATED")) {
        int children = 0;
        const auto result = opusoptima::solvers::CompileAcceptedSerialArmExecution(
            puzzle, *plan.plan, [&](const auto& candidate) {
                const auto a = archive.Submit(candidate, "integrated-parent");
                std::cout << "PARENT " << a.metrics.cost << "/" << a.metrics.cycles << "/"
                          << a.metrics.area << " status=" << static_cast<int>(a.status) << std::endl;
                return a.Passed();
            }, {}, deadline, {}, [&](const auto& original, const auto& candidate) {
                ++children;
                const auto first = archive.First();
                ASSERT_TRUE(first);
                const auto b = archive.Submit(candidate,
                    (candidate.solutionName == "Shared-plan retained ordinary fragments"
                         ? "integrated-retained-fragments:parent=" : "integrated-scalar-relay:parent=") +
                    CandidateSha256(BinaryWriter::WriteSolution(original)));
                std::cout << "CHILD " << b.metrics.cost << "/" << b.metrics.cycles << "/"
                          << b.metrics.area << " status=" << static_cast<int>(b.status) << std::endl;
                EXPECT_TRUE(b.Passed()) << b.detail;
                EXPECT_EQ(archive.First(), first);
                if (std::getenv("OPUS_RELAY_THROW"))
                    throw std::runtime_error("injected optional callback failure after gate");
            });
        ASSERT_TRUE(result);
        EXPECT_LE(children, 2);
        if (!children) std::cout << "REFUSAL ablated or shared3s deadline exhausted" << std::endl;
        EXPECT_TRUE(archive.Snapshot(options).Passed());
        return;
    }
    const auto compiled = opusoptima::solvers::CompileAcceptedSerialArmExecution(
        puzzle, *plan.plan, [&](const auto& candidate) {
            parent = candidate;
            return true;
        }, {}, deadline, {}, [&](const auto&, const auto& candidate) { child = candidate; });
    ASSERT_TRUE(compiled);
    ASSERT_TRUE(parent);
    const bool transformed = std::getenv("OPUS_RELAY_FRAME") != nullptr;
    const auto frame = [&](SolutionData candidate) {
        if (transformed) {
            candidate = opusoptima::solvers::LocalTrial(candidate, 3);
            for (auto& object : candidate.objects) {
                object.position.q += 7;
                object.position.r -= 4;
            }
        }
        return candidate;
    };
    *parent = frame(*parent);
    const auto a = archive.Submit(*parent, "relay-construction-parent");
    std::cout << "PARENT status=" << static_cast<int>(a.status) << " reason=" << a.reasonCode << std::endl;
    ASSERT_TRUE(a.Passed()) << a.detail;
    const auto first = archive.First();
    if (!child) {
        std::cout << "REFUSAL no eligible aligned scalar relay or ablated" << std::endl;
    } else {
        *child = frame(*child);
        const auto parentHash = CandidateSha256(BinaryWriter::WriteSolution(*parent));
        const auto b = archive.Submit(*child, "scalar-relay-pipeline:parent=" + parentHash);
        std::cout << "PAIR " << a.metrics.cost << "/" << a.metrics.cycles << "/" << a.metrics.area
                  << " -> " << b.metrics.cost << "/" << b.metrics.cycles << "/" << b.metrics.area
                  << " status=" << static_cast<int>(b.status) << " reason=" << b.reasonCode << std::endl;
        EXPECT_TRUE(b.Passed()) << b.detail;
    }
    EXPECT_EQ(archive.First(), first);
    EXPECT_TRUE(archive.Snapshot(options).Passed());
}
TEST(CandidateArchive, RegisteredParallelReturnEvidence) {
    const auto* input = std::getenv("OPUS_RETURN_PUZZLE");
    const auto* file = std::getenv("OPUS_RETURN_SOLUTION");
    const auto* output = std::getenv("OPUS_RETURN_OUTPUT");
    if (!input || !file || !output) GTEST_SKIP() << "Optional registered corpus measurement";
    auto parent = BinaryParser::ParseSolutionFile(file);
    VerificationOptions options;
    options.outerDeadline = std::chrono::steady_clock::now()+std::chrono::seconds(20);
    CandidateArchive archive(input, output, options);
    ASSERT_TRUE(archive.Submit(parent,"registered-parent").Passed());
    auto first = archive.First();
    const auto* serialValue = std::getenv("OPUSOPTIMA_SERIAL_RETURN");
    const bool serial = serialValue && std::string(serialValue) == "1";
    const auto* selectedValue = std::getenv("OPUS_RETURN_ROTATION");
    const int selected = selectedValue ? std::stoi(selectedValue) : -1;
    ASSERT_GE(selected, -1);
    ASSERT_LT(selected, 6);
    for (int rotation=0; rotation<6; ++rotation) {
        if (selected >= 0 && selected != rotation) {
            parent = opusoptima::solvers::LocalTrial(parent,3);
            continue;
        }
        auto candidate = opusoptima::solvers::LocalTrial(parent,4);
        const auto report = archive.Submit(candidate,"registered-rotation-"+std::to_string(rotation));
        ASSERT_TRUE(report.Passed()) << "rotation=" << rotation << " status="
                                    << static_cast<int>(report.status) << " reason=" << report.reasonCode;
        if (!serial) EXPECT_LT(report.metrics.cycles,first->verification.metrics.cycles);
        std::cout << "PAIR " << rotation << " " << first->verification.metrics.cost << "/"
                  << first->verification.metrics.cycles << "/" << first->verification.metrics.area
                  << " -> " << report.metrics.cost << "/" << report.metrics.cycles << "/"
                  << report.metrics.area << std::endl;
        parent = opusoptima::solvers::LocalTrial(parent,3);
    }
    EXPECT_EQ(archive.First(),first);
    auto saved=archive.Slots();
    EXPECT_EQ(archive.Submit(parent,"expired",std::chrono::steady_clock::now()).status,
              VerificationStatus::Timeout);
    EXPECT_EQ(archive.Slots(),saved);
    EXPECT_TRUE(archive.Snapshot(options).Passed());
}
TEST(CandidateArchive, RealLocalImprovementLayoutAndExpiredRetention) {
    Workspace w;
    auto puzzle = Root() / "asset/puzzle/easy/easy-conduit.puzzle";
    auto solution = BinaryParser::ParseSolutionFile(
        Root() / "asset/solution/easy/easy-conduit-easy-conduit-1.solution");
    for (auto& object : solution.objects)
        for (auto& instruction : object.instructions) instruction.parameter *= 2;
    SolutionObject unused;
    unused.typeName = "arm1"; unused.position = {30,30}; unused.bodyData = {1,0,0,0,9};
    solution.objects.push_back(unused);
    VerificationOptions options;
    options.outerDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    CandidateArchive archive(puzzle, w.path / "local.solution", options);
    ASSERT_TRUE(archive.Submit(solution, "parent").Passed());
    auto first = archive.First();
    for (const auto& slot : archive.Slots()) EXPECT_EQ(slot, first);
    opusoptima::solvers::OptimizeVerified(archive, *options.outerDeadline);
    EXPECT_LT(archive.Slots()[0]->verification.metrics.cost, first->verification.metrics.cost);
    EXPECT_LT(archive.Slots()[1]->verification.metrics.cycles, first->verification.metrics.cycles);
    EXPECT_LT(archive.Slots()[2]->verification.metrics.area, first->verification.metrics.area);
    EXPECT_EQ(archive.First(), first);
    auto combined = opusoptima::solvers::LocalTrial(first->solution, 5);
    const auto combinedReport = archive.Submit(combined, "combined-regression");
    ASSERT_TRUE(combinedReport.Passed());
    EXPECT_LT(combinedReport.metrics.cost, first->verification.metrics.cost);
    EXPECT_LT(combinedReport.metrics.cycles, first->verification.metrics.cycles);
    EXPECT_LT(combinedReport.metrics.area, first->verification.metrics.area);
    auto slots = archive.Slots();
    auto timedOut = archive.Submit(first->solution, "expired-local",
                                 std::chrono::steady_clock::now());
    EXPECT_EQ(timedOut.status, VerificationStatus::Timeout);
    EXPECT_EQ(archive.Slots(), slots);
    auto rotated = opusoptima::solvers::LocalTrial(slots[0]->solution, 3);
    const auto layoutReport = archive.Submit(rotated, "layout-regression");
    if (!layoutReport.Passed()) EXPECT_EQ(archive.Slots(), slots);
    auto forbidden = slots[0]->solution;
    SolutionObject unavailable;
    unavailable.typeName = "glyph-unification";
    unavailable.position = {80,80}; unavailable.bodyData = {1,0,0,0,12};
    forbidden.objects.push_back(unavailable);
    EXPECT_FALSE(archive.Submit(forbidden, "permission-negative").Passed());
    EXPECT_EQ(archive.Slots(), slots);
    opusoptima::solvers::OptimizeVerified(archive, std::chrono::steady_clock::now());
    EXPECT_EQ(archive.Slots(), slots);
    EXPECT_TRUE(archive.Publish(options).Passed());

    const auto scalarPuzzle = Root() / "asset/puzzle/easy/just-salt.puzzle";
    auto explicitTape = opusoptima::solvers::SingleAtomSolver{}.Solve(
        BinaryParser::ParsePuzzleFile(scalarPuzzle));
    explicitTape.objects[0].instructions = {{'G',0},{'r',1},{'g',2},{'R',5},
        {'G',6},{'r',7},{'g',8},{'R',9},{'O',10}};
    explicitTape.objects.push_back({"arm1",{10,0},{{'G',3},{'g',4},{'O',10}},{1,0,0,0,1}});
    auto repeatedOptions = options;
    repeatedOptions.cycleLimit = 100;
    CandidateArchive repeated(scalarPuzzle,w.path / "repeated.solution",repeatedOptions);
    const auto before = repeated.Submit(explicitTape,"explicit-parent");
    ASSERT_TRUE(before.Passed()) << before.detail;
    const auto repeatedFirst = repeated.First();
    const auto scheduled = opusoptima::solvers::LocalTrial(explicitTape,4);
    opusoptima::solvers::OptimizeVerified(repeated,*repeatedOptions.outerDeadline);
    const auto after = repeated.Slots()[1]->verification;
    ASSERT_TRUE(after.Passed()) << after.detail;
    EXPECT_EQ(after.authority,VerificationAuthority::LegacyGate);
    auto admitted = repeated.Slots()[1]->solution;
    auto expected = scheduled;
    admitted.metrics = {};
    expected.metrics = {};
    EXPECT_EQ(BinaryWriter::WriteSolution(admitted),BinaryWriter::WriteSolution(expected));
    std::ifstream localTrace(repeated.Directory() / "local-trials.jsonl");
    const std::string trace((std::istreambuf_iterator<char>(localTrace)),{});
    EXPECT_NE(trace.find("\"event\":\"accepted\",\"variant\":4,"),std::string::npos);
    EXPECT_EQ(before.metrics.cycles,31);
    EXPECT_EQ(after.metrics.cycles,28);
    EXPECT_EQ(after.metrics.cost,before.metrics.cost);
    EXPECT_EQ(after.metrics.area,before.metrics.area);
    EXPECT_EQ(repeated.First(),repeatedFirst);
    const auto repeatedSlots = repeated.Slots();
    auto halted = scheduled;
    for (auto& object : halted.objects)
        if (!object.instructions.empty()) object.instructions.back().action='B';
    const auto incomplete = repeated.Submit(halted,"incomplete-two-products");
    EXPECT_EQ(incomplete.status,VerificationStatus::CycleLimit) << incomplete.detail;
    EXPECT_EQ(repeated.Slots(),repeatedSlots);
    EXPECT_TRUE(repeated.Snapshot(repeatedOptions).Passed());

    explicitTape.objects[0].instructions[7].parameter = 11;
    for (auto& object : explicitTape.objects)
        if (!object.instructions.empty()) object.instructions.back().parameter = 15;
    CandidateArchive composite(scalarPuzzle,w.path / "composite.solution",repeatedOptions);
    const auto compositeBefore = composite.Submit(explicitTape,"composite-original-parent");
    ASSERT_TRUE(compositeBefore.Passed()) << compositeBefore.detail;
    const auto compositeFirst = composite.First();
    auto compositeExpected = opusoptima::solvers::LocalTrial(explicitTape,5);
    opusoptima::solvers::OptimizeVerified(composite,*repeatedOptions.outerDeadline);
    const auto compositeAfter = composite.Slots()[1]->verification;
    ASSERT_TRUE(compositeAfter.Passed()) << compositeAfter.detail;
    EXPECT_LT(compositeAfter.metrics.cycles,compositeBefore.metrics.cycles);
    EXPECT_EQ(compositeAfter.metrics.cycles,28);
    EXPECT_EQ(compositeAfter.metrics.cost,compositeBefore.metrics.cost);
    EXPECT_EQ(compositeAfter.metrics.area,compositeBefore.metrics.area);
    auto compositeAdmitted = composite.Slots()[1]->solution;
    compositeAdmitted.metrics = {}; compositeExpected.metrics = {};
    EXPECT_EQ(BinaryWriter::WriteSolution(compositeAdmitted),BinaryWriter::WriteSolution(compositeExpected));
    EXPECT_EQ(composite.First(),compositeFirst);
    std::ifstream compositeTrace(composite.Directory() / "local-trials.jsonl");
    const std::string compositeEvents((std::istreambuf_iterator<char>(compositeTrace)),{});
    EXPECT_NE(compositeEvents.find("\"event\":\"accepted\",\"variant\":5,"),std::string::npos);
    EXPECT_NE(compositeEvents.find("\"transformation\":\"compact-returns-and-preposition-empty-rotations\""),std::string::npos);
    const auto compositeSlots = composite.Slots();
    for (auto& object : compositeExpected.objects)
        if (!object.instructions.empty()) object.instructions.back().action='B';
    EXPECT_EQ(composite.Submit(compositeExpected,"composite-incomplete-two-products").status,
              VerificationStatus::CycleLimit);
    EXPECT_EQ(composite.Slots(),compositeSlots);
    EXPECT_TRUE(composite.Snapshot(repeatedOptions).Passed());
    std::cout << "Composite original parent=" << compositeBefore.metrics.cycles
              << " -> " << compositeAfter.metrics.cycles << std::endl;
    std::cout << "Local verified parent=" << first->verification.metrics.cost << "/"
              << first->verification.metrics.cycles << "/" << first->verification.metrics.area
              << " winners=";
    for (const auto& slot : archive.Slots())
        std::cout << slot->verification.metrics.cost << "/" << slot->verification.metrics.cycles
                  << "/" << slot->verification.metrics.area << " ";
    std::cout << std::endl;
    if (std::getenv("OPUS_LOCAL_EVIDENCE")) {
        const opusoptima::test::ScopedTestDirectory evidence;
        std::filesystem::copy(w.path, evidence.Path() / "recovery-regression",
                 std::filesystem::copy_options::recursive);
    }
}
TEST(CandidateArchive, RealGateStableTiesRejectTimeoutInputIsolationAndPublication) {
    Workspace w;
    auto puzzle = w.path / "input.puzzle";
    std::filesystem::copy_file(Root() / "asset/puzzle/easy/easy-conduit.puzzle", puzzle);
    auto solution = BinaryParser::ParseSolutionFile(
        Root() / "asset/solution/easy/easy-conduit-easy-conduit-1.solution");
    auto output = w.path / "main.solution";
    VerificationOptions options;
    CandidateArchive archive(puzzle, output, options);
    ASSERT_TRUE(archive.Submit(solution, "fixture-original").Passed());
    auto first = archive.First();
    for (const auto& slot : archive.Slots()) EXPECT_EQ(slot, first);
    solution.solutionName += "-tie";
    ASSERT_TRUE(archive.Submit(solution, "fixture-tie").Passed());
    for (const auto& slot : archive.Slots()) EXPECT_EQ(slot, first);
    EXPECT_FALSE(archive.Submit({}, "invalid").Passed());
    for (const auto& slot : archive.Slots()) EXPECT_EQ(slot, first);
    auto expired = options;
    expired.outerDeadline = std::chrono::steady_clock::now();
    EXPECT_FALSE(archive.Publish(expired).Passed());
    EXPECT_FALSE(std::filesystem::exists(output));
    ASSERT_TRUE(archive.Publish(options).Passed());
    EXPECT_EQ(BinaryParser::ReadAllBytes(output), first->bytes);
    for (const auto* name : {"cost", "cycles", "area"}) {
        auto path = archive.Directory() / (std::string(name) + ".solution");
        auto measured = VerifySolutionFiles(puzzle, path, options);
        ASSERT_TRUE(measured.Passed());
        EXPECT_EQ(measured.metrics.cost, first->verification.metrics.cost);
        EXPECT_EQ(measured.metrics.cycles, first->verification.metrics.cycles);
        EXPECT_EQ(measured.metrics.area, first->verification.metrics.area);
    }
    auto prior = BinaryParser::ReadAllBytes(output);
    std::ofstream(puzzle, std::ios::binary | std::ios::app) << 'x';
    EXPECT_FALSE(archive.Submit(solution, "changed-input").Passed());
    EXPECT_FALSE(archive.Publish(options).Passed());
    EXPECT_EQ(BinaryParser::ReadAllBytes(output), prior);
}
TEST(CandidateArchive, RealGateObjectiveReplacement) {
    Workspace w;
    auto puzzle = Root() / "asset/puzzle/easy/easy-conduit.puzzle";
    auto base = BinaryParser::ParseSolutionFile(Root() / "asset/solution/easy/easy-conduit-easy-conduit-1.solution");
    CandidateArchive archive(puzzle, w.path / "main.solution", {});
    for (int variant = 0; variant < 4; ++variant) {
        auto candidate = variant == 0 ? BinaryParser::ParseSolutionFile(Root() / "asset/solution/easy/easy-conduit-easy-conduit-2.solution") : base;
        candidate.solutionName = "variant-" + std::to_string(variant);
        if (variant == 0 || variant == 1)
            for (auto& object : candidate.objects)
                for (auto& instruction : object.instructions) instruction.parameter *= variant == 0 ? 3 : 2;
        if (variant == 1 || variant == 2) {
            SolutionObject extra;
            extra.typeName = "arm1";
            extra.position = {30, 30};
            extra.bodyData = {1,0,0,0,9};
            candidate.objects.push_back(extra);
        }
        auto report = archive.Submit(candidate, candidate.solutionName);
        ASSERT_TRUE(report.Passed()) << report.detail;
        if (variant == 0) EXPECT_EQ(archive.Slots()[0], archive.First());
        if (variant == 1) {
            EXPECT_EQ(archive.Slots()[0], archive.First()); // equal cost, stable first
            EXPECT_NE(archive.Slots()[1], archive.First());
            EXPECT_NE(archive.Slots()[2], archive.First());
        }
        if (variant == 2) EXPECT_NE(archive.Slots()[1], archive.Slots()[0]);
        if (variant == 3) {
            EXPECT_EQ(archive.Slots()[0]->strategy, "variant-3");
            EXPECT_EQ(archive.Slots()[1]->strategy, "variant-2"); // equal cycles, stable first
            EXPECT_EQ(archive.Slots()[2]->strategy, "variant-3");
        }
    }
}
TEST(CandidateArchive, RealGateThreeDistinctWinners) {
    Workspace w;
    auto input = Root() / "asset/puzzle/easy/easy-conduit.puzzle";
    auto base = BinaryParser::ParseSolutionFile(Root() / "asset/solution/easy/easy-conduit-easy-conduit-1.solution");
    CandidateArchive archive(input, w.path / "main.solution", {});
    for (int variant = 0; variant < 3; ++variant) {
        auto candidate = base;
        if (variant == 0) {
            for (auto& object : candidate.objects) if (!object.instructions.empty()) {
                for (auto& instruction : object.instructions) instruction.parameter += 6;
                for (int i = 0; i < 6; ++i) object.instructions.push_back({'R', i});
            }
        } else if (variant == 1) {
            SolutionObject extra;
            extra.typeName = "arm1"; extra.position = {30,30}; extra.bodyData = {1,0,0,0,9};
            candidate.objects.push_back(extra);
        } else {
            for (auto& object : candidate.objects)
                for (auto& instruction : object.instructions) instruction.parameter *= 2;
        }
        auto report = archive.Submit(candidate, std::to_string(variant));
        ASSERT_TRUE(report.Passed()) << report.detail;
    }
    EXPECT_EQ(archive.Slots()[0]->strategy, "0");
    EXPECT_EQ(archive.Slots()[1]->strategy, "1");
    EXPECT_EQ(archive.Slots()[2]->strategy, "2");
}
TEST(CandidateArchive, RealGateAdmissionTimeoutAndPublicationGuards) {
    Workspace w;
    auto puzzle = w.path / "input.puzzle";
    std::filesystem::copy_file(Root() / "asset/puzzle/easy/easy-conduit.puzzle", puzzle);
    auto solution = BinaryParser::ParseSolutionFile(
        Root() / "asset/solution/easy/easy-conduit-easy-conduit-1.solution");
    auto output = w.path / "main.solution";
    std::ofstream(output) << "old-output";
    auto manifest = std::filesystem::path(output.string() + ".objectives.json");
    std::ofstream(manifest) << "old-manifest";
    auto oldOutput = BinaryParser::ReadAllBytes(output);
    auto oldManifest = BinaryParser::ReadAllBytes(manifest);
    VerificationOptions admission;
    admission.outerDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
    CandidateArchive archive(puzzle, output, admission);
    ASSERT_TRUE(archive.Submit(solution, "original").Passed());
    auto first = archive.First();
    std::this_thread::sleep_until(*admission.outerDeadline);
    auto timeout = archive.Submit(solution, "expired");
    EXPECT_EQ(timeout.status, VerificationStatus::Timeout);
    for (const auto& slot : archive.Slots()) EXPECT_EQ(slot, first);
    VerificationOptions differentAuthority;
    differentAuthority.mode = VerificationMode::Independent;
    EXPECT_FALSE(archive.Publish(differentAuthority).Passed());
    EXPECT_EQ(BinaryParser::ReadAllBytes(output), oldOutput);
    EXPECT_EQ(BinaryParser::ReadAllBytes(manifest), oldManifest);
    std::ofstream(puzzle, std::ios::binary | std::ios::app) << 'x';
    EXPECT_FALSE(archive.Submit(solution, "different-input").Passed());
    EXPECT_FALSE(archive.Publish({}).Passed());
    for (const auto& slot : archive.Slots()) EXPECT_EQ(slot, first);
    EXPECT_EQ(BinaryParser::ReadAllBytes(output), oldOutput);
    EXPECT_EQ(BinaryParser::ReadAllBytes(manifest), oldManifest);
}
TEST(CandidateArchive, RealGateTamperAndDestinationProtection) {
    Workspace w;
    auto puzzle = Root() / "asset/puzzle/easy/easy-conduit.puzzle";
    auto solution = BinaryParser::ParseSolutionFile(
        Root() / "asset/solution/easy/easy-conduit-easy-conduit-1.solution");
    auto output = w.path / "main.solution";
    std::ofstream(output) << "old-output";
    auto oldOutput = BinaryParser::ReadAllBytes(output);
    CandidateArchive archive(puzzle, output, {});
    ASSERT_TRUE(archive.Submit(solution, "original").Passed());
    auto first = archive.First();
    std::ofstream(first->file, std::ios::binary | std::ios::app) << 'x';
    EXPECT_FALSE(archive.Publish({}).Passed());
    EXPECT_EQ(BinaryParser::ReadAllBytes(output), oldOutput);
    {
        std::ofstream restore(first->file, std::ios::binary | std::ios::trunc);
        restore.write(reinterpret_cast<const char*>(first->bytes.data()), first->bytes.size());
    }
    auto manifest = std::filesystem::path(output.string() + ".objectives.json");
    std::filesystem::create_directory(manifest);
    EXPECT_FALSE(archive.Publish({}).Passed());
    EXPECT_EQ(BinaryParser::ReadAllBytes(output), oldOutput);
    std::filesystem::remove(manifest);
    ASSERT_TRUE(archive.Publish({}).Passed());
    EXPECT_FALSE(archive.Submit(solution, "after-publication").Passed());
    EXPECT_EQ(BinaryParser::ReadAllBytes(output), first->bytes);
    for (const auto& slot : archive.Slots()) EXPECT_EQ(slot, first);
}
TEST(CandidateArchive, ProductionCollectionPreservesFirstWithinBudget) {
    Workspace w;
    auto input = Root() / "asset/puzzle/easy/easy-conduit.puzzle";
    auto puzzle = BinaryParser::ParsePuzzleFile(input);
    CandidateArchive archive(input, w.path / "main.solution", {});
    opusoptima::solvers::AssemblySolver solver;
    solver.SetDeadline(std::chrono::steady_clock::now() + std::chrono::seconds(2));
    int passed = 0;
    try {
        solver.CollectVerified(puzzle, [&](const auto& candidate, const auto& strategy) {
            auto report = archive.Submit(candidate, strategy);
            passed += report.Passed();
            return report;
        }, 3);
    } catch (const std::exception&) {}
    EXPECT_GE(passed, 1);
    EXPECT_LE(passed, 3);
    ASSERT_TRUE(archive.First());
}

TEST(CandidateArchive, AdmissionAndPublicationCountActualWorkerGates) {
#ifndef __linux__
    GTEST_SKIP() << "Worker wrapper counting requires Linux /proc/self/exe and /bin/sh";
#else
    Workspace w;
    auto worker = std::filesystem::read_symlink("/proc/self/exe").parent_path() /
                  "OpusOptimaReferenceWorker";
    auto counter = w.path / "gates.txt";
    auto wrapper = w.path / "count-worker.sh";
    {
        std::ofstream script(wrapper);
        script << "#!/bin/sh\nprintf 'gate\\n' >> '" << counter.string()
               << "'\nexec '" << worker.string() << "' \"$@\"\n";
    }
    std::filesystem::permissions(wrapper, std::filesystem::perms::owner_all);
    auto gates = [&] {
        std::ifstream input(counter);
        return std::count(std::istreambuf_iterator<char>(input),
                          std::istreambuf_iterator<char>(), '\n');
    };
    VerificationOptions options;
    options.workerExecutable = wrapper;
    auto input = Root() / "asset/puzzle/easy/easy-conduit.puzzle";
    auto solution = BinaryParser::ParseSolutionFile(
        Root() / "asset/solution/easy/easy-conduit-easy-conduit-1.solution");
    CandidateArchive archive(input, w.path / "main.solution", options);
    ASSERT_TRUE(archive.Submit(solution, "first").Passed());
    EXPECT_EQ(gates(), 2);
    ASSERT_TRUE(archive.Snapshot(options).Passed());
    EXPECT_EQ(gates(), 2);
    auto rejected = solution;
    rejected.objects.clear();
    EXPECT_FALSE(archive.Submit(rejected, "rejected").Passed());
    EXPECT_EQ(gates(), 3);
    ASSERT_TRUE(archive.Snapshot(options).Passed());
    EXPECT_EQ(gates(), 3);
    ASSERT_TRUE(archive.Publish(options).Passed());
    EXPECT_EQ(gates(), 4);
#endif
}

TEST(CandidateArchive, RealGateSnapshotsRetainVerifiedFirstAndImmutableGenerations) {
    Workspace w;
    auto input = Root() / "asset/puzzle/easy/easy-conduit.puzzle";
    auto solution = BinaryParser::ParseSolutionFile(
        Root() / "asset/solution/easy/easy-conduit-easy-conduit-1.solution");
    auto output = w.path / "main.solution";
    auto manifest = std::filesystem::path(output.string() + ".objectives.json");
    CandidateArchive archive(input, output, {});
    EXPECT_FALSE(archive.Snapshot({}).Passed());
    auto slow = solution;
    for (auto& object : slow.objects)
        for (auto& instruction : object.instructions) instruction.parameter *= 2;
    ASSERT_TRUE(archive.Submit(slow, "first").Passed());
    ASSERT_TRUE(archive.Snapshot({}).Passed());
    auto first = archive.First();
    auto oldManifest = BinaryParser::ReadAllBytes(manifest);
    for (const auto& slot : archive.Slots()) EXPECT_EQ(slot, first);
    std::vector<std::pair<std::filesystem::path, std::vector<std::byte>>> files;
    for (const auto& file : std::filesystem::directory_iterator(archive.Directory()))
        if (file.path().filename().string().starts_with("snapshot-"))
            files.emplace_back(file.path(), BinaryParser::ReadAllBytes(file.path()));
    ASSERT_EQ(files.size(), 3);
    auto expired = VerificationOptions{};
    expired.outerDeadline = std::chrono::steady_clock::now();
    EXPECT_FALSE(archive.Snapshot(expired).Passed());
    EXPECT_EQ(BinaryParser::ReadAllBytes(manifest), oldManifest);
    auto wrong = VerificationOptions{};
    wrong.mode = VerificationMode::Independent;
    EXPECT_FALSE(archive.Snapshot(wrong).Passed());
    solution.solutionName += "-later";
    ASSERT_TRUE(archive.Submit(solution, "later").Passed());
    ASSERT_NE(archive.Slots()[1], first);
    ASSERT_TRUE(archive.Snapshot({}).Passed());
    EXPECT_NE(BinaryParser::ReadAllBytes(manifest), oldManifest);
    EXPECT_EQ(BinaryParser::ReadAllBytes(output), first->bytes);
    for (const auto& [file, bytes] : files) EXPECT_EQ(BinaryParser::ReadAllBytes(file), bytes);
    oldManifest = BinaryParser::ReadAllBytes(manifest);
    std::ofstream(first->file, std::ios::binary | std::ios::app) << 'x';
    EXPECT_FALSE(archive.Snapshot({}).Passed());
    EXPECT_EQ(BinaryParser::ReadAllBytes(manifest), oldManifest);
    EXPECT_EQ(BinaryParser::ReadAllBytes(output), first->bytes);
}
#endif
