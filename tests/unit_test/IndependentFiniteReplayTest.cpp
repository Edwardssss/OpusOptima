#include "BinaryWriter.hpp"
#include "SingleAtomSolver.hpp"
#include "UnitTestConstants.hpp"
#include "Verification/IndependentVerifier.hpp"

#include "RepoRoot.hpp"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <gtest/gtest.h>
#include <limits>

namespace {
using namespace opusoptima;
const auto root = opusoptima::test::RepoRoot(__FILE__);
io::PuzzleData Ordinary() {
    io::PuzzleData puzzle;
    puzzle.version = 3;
    puzzle.name = "finite ordinary replay";
    puzzle.permissionBits = ~0ULL;
    puzzle.outputMultiplier = 1;
    puzzle.inputs = {{{{core::AtomType::Salt, {}}}, {}}};
    puzzle.outputs = puzzle.inputs;
    return puzzle;
}
io::SolutionData Machine() {
    io::SolutionData solution;
    solution.objects = {{"input", {1, 0}, {}, {1, 0, 0, 0, 0}},
                        {"out-std", {0, 1}, {}, {1, 0, 0, 0, 0}},
                        {"arm1", {}, {{'G', 0}, {'r', 1}, {'g', 2}, {'R', 3}}, {1, 0, 0, 4, 0}}};
    return solution;
}
io::SolutionData StaticMachine() {
    io::SolutionData solution;
    solution.objects = {{"input", {}, {}, {1, 0, 0, 0, 0}},
                        {"out-std", {}, {}, {1, 0, 0, 0, 0}},
                        {"arm1", {10, 0}, {{'B', 0}}, {1, 0, 0, 1, 0}}};
    return solution;
}
} // namespace
TEST(IndependentFiniteReplay, ExplicitExpansionExecutesRawWithoutAuthorizingIt) {
    const auto puzzle = Ordinary();
    for (const bool copy : {false, true}) {
        auto raw = Machine();
        raw.metrics = {999, 888, 777, 666};
        raw.objects.back().instructions.back().action = 'X';
        if (copy) raw.objects.back().instructions.push_back({'C', 4});
        const auto before = io::BinaryWriter::WriteSolution(raw);
        const auto report = verification::ReplayExpandedFiniteMachine(puzzle, raw, 100);
        ASSERT_EQ(report.execution.outcome, verification::ReplayOutcome::Completed)
            << report.execution.detail;
        EXPECT_FALSE(report.certificationEligible);
        EXPECT_EQ(report.execution.outputs, (std::vector<std::size_t>{6}));
        EXPECT_EQ(report.execution.cycles, 23);
        ASSERT_TRUE(report.rawSource);
        EXPECT_EQ(report.rawSource->metrics.instructionCount, 666);
        EXPECT_EQ(io::BinaryWriter::WriteSolution(*report.rawSource), before);
        EXPECT_EQ(io::BinaryWriter::WriteSolution(raw), before);
        ASSERT_TRUE(report.expandedTape);
        EXPECT_EQ(report.expandedTape->rawStart, 0);
        EXPECT_EQ(report.expandedTape->origins[3].rawOrdinal, 3U);
        EXPECT_EQ(report.expandedTape->origins[3].rawIndex, 3);
        EXPECT_EQ(report.expandedTape->origins[3].kind, verification::TapeOrigin::Kind::Reset);
        EXPECT_EQ(report.expandedTape->staticInstructions, copy ? 8 : 4);
        if (copy) {
            EXPECT_EQ(report.expandedTape->origins[4].copySite, 4);
        }
        EXPECT_FALSE(report.expansionFailure);
        EXPECT_EQ(verification::ReplayFiniteMachine(puzzle, raw, 100).outcome,
                  verification::ReplayOutcome::Unavailable);
        const auto certified = verification::VerifyIndependent(puzzle, raw, 100);
        EXPECT_EQ(certified.status, io::VerificationStatus::Unavailable);
        EXPECT_EQ(certified.reasonCode, "independent-profile-uncertified");
        EXPECT_EQ(verification::VerifyStaticProduction(puzzle, raw, 100).status,
                  io::VerificationStatus::Unavailable);
    }
}
TEST(IndependentFiniteReplay, ExplicitCopyPreservesUnorderedOffsetSource) {
    auto raw = Machine();
    raw.metrics = {999, 888, 777, 666};
    raw.objects.back().instructions = {{'C', 11}, {'g', 9}, {'G', 7}, {'R', 10}, {'r', 8}};
    const auto before = io::BinaryWriter::WriteSolution(raw);
    const auto report = verification::ReplayExpandedFiniteMachine(Ordinary(), raw, 100);
    ASSERT_EQ(report.execution.outcome, verification::ReplayOutcome::Completed)
        << report.execution.detail;
    ASSERT_TRUE(report.expandedTape);
    ASSERT_TRUE(report.rawSource);
    EXPECT_EQ(report.expandedTape->rawStart, 7);
    EXPECT_EQ(report.expandedTape->staticInstructions, 8);
    EXPECT_EQ(report.expandedTape->origins[4].rawOrdinal, 2U);
    EXPECT_EQ(report.expandedTape->origins[4].rawIndex, 7);
    EXPECT_EQ(report.expandedTape->origins[4].copySite, 11);
    EXPECT_EQ(report.execution.cycles, 23);
    EXPECT_FALSE(report.certificationEligible);
    EXPECT_EQ(io::BinaryWriter::WriteSolution(*report.rawSource), before);
    EXPECT_EQ(io::BinaryWriter::WriteSolution(raw), before);
}
TEST(IndependentFiniteReplay, ExpansionCategoriesRetainFailureAndNeverExecute) {
    using Error = verification::TapeExpansionError;
    struct Case {
        std::vector<io::ArmInstruction> tape;
        Error error;
    };
    const Case cases[] = {
        {{{'C', -1}}, Error::OutsideDomain},
        {{{'X', 20001}}, Error::BoundExceeded},
        {{{'r', 0}, {'r', 1}, {'r', 2}, {'X', 3}, {'G', 4}}, Error::Conflict},
    };
    for (const auto& item : cases) {
        auto raw = Machine();
        raw.objects.back().instructions = item.tape;
        const auto expected = verification::ExpandFiniteArm1Tape(Ordinary(), raw);
        ASSERT_TRUE(std::holds_alternative<verification::TapeExpansionFailure>(expected));
        const auto failure = std::get<verification::TapeExpansionFailure>(expected);
        const auto report = verification::ReplayExpandedFiniteMachine(Ordinary(), raw, 100);
        ASSERT_TRUE(report.expansionFailure);
        EXPECT_EQ(report.expansionFailure->code, item.error);
        EXPECT_EQ(report.expansionFailure->rawOrdinal, failure.rawOrdinal);
        EXPECT_EQ(report.expansionFailure->slot, failure.slot);
        EXPECT_FALSE(report.execution.detail.empty());
        EXPECT_EQ(report.execution.outcome, verification::ReplayOutcome::Unavailable);
        EXPECT_TRUE(report.execution.events.empty());
        EXPECT_EQ(report.execution.cycles, 0);
        EXPECT_FALSE(report.expandedTape);
        EXPECT_FALSE(report.certificationEligible);
    }
}
TEST(IndependentFiniteReplay, ExpansionFailureNeverExecutesPartialTape) {
    auto raw = Machine();
    raw.objects.back().instructions = {{'r', 0}, {'X', 1}, {'g', 1}};
    const auto report = verification::ReplayExpandedFiniteMachine(Ordinary(), raw, 100);
    ASSERT_TRUE(report.expansionFailure);
    EXPECT_EQ(report.expansionFailure->code, verification::TapeExpansionError::Malformed);
    EXPECT_EQ(report.execution.detail, "finite expansion failed: Malformed");
    EXPECT_EQ(report.execution.outcome, verification::ReplayOutcome::Unavailable);
    EXPECT_TRUE(report.execution.events.empty());
    EXPECT_EQ(report.execution.cycles, 0);
    EXPECT_FALSE(report.expandedTape);
    EXPECT_FALSE(report.certificationEligible);
    raw.objects.back().instructions = {{'G', 0}, {'r', 1}, {'g', 2}, {'X', 3}};
    const auto limited = verification::ReplayExpandedFiniteMachine(Ordinary(), raw, 1);
    EXPECT_EQ(limited.execution.outcome, verification::ReplayOutcome::CycleLimit);
    EXPECT_FALSE(limited.certificationEligible);
}
TEST(IndependentFiniteReplay, OrdinaryEnvironmentDoesNotInventChamberWallsOrTrustDeclaredMetrics) {
    auto puzzle = Ordinary();
    auto machine = Machine();
    machine.metrics = {1, 999999, 999999, 999999};
    const auto report = verification::ReplayFiniteMachine(puzzle, machine, 100);
    ASSERT_EQ(report.outcome, verification::ReplayOutcome::Completed) << report.detail;
    EXPECT_EQ(report.cycles, 23);
    EXPECT_EQ(report.outputs, (std::vector<std::size_t>{6}));
    EXPECT_EQ(report.acquired, report.outputs[0] + report.remaining);
    ASSERT_FALSE(report.events.empty());
    EXPECT_EQ(report.events.front().phase, verification::StaticProductionEvent::Phase::Initial);
    EXPECT_TRUE(std::any_of(report.events.begin(), report.events.end(), [](const auto& event) {
        return event.kind == verification::StaticProductionEvent::Kind::Motion &&
               event.phase == verification::StaticProductionEvent::Phase::Motion &&
               !event.atoms.empty();
    }));
    EXPECT_FALSE(report.traceTruncated);
    EXPECT_EQ(verification::VerifyStaticProduction(puzzle, machine).status,
              io::VerificationStatus::Unavailable);
    for (auto& part : machine.objects)
        part.position = core::Add(part.position, {80, -60});
    EXPECT_EQ(verification::ReplayFiniteMachine(puzzle, machine, 100).outcome,
              verification::ReplayOutcome::Completed);
}
TEST(IndependentFiniteReplay, TrackLengthBoundPreservesEntryOutcomeAndOriginalDetail) {
    const auto puzzle = Ordinary();
    const auto original = Machine();
    const auto baseline = verification::ReplayFiniteMachine(puzzle, original, 100);
    ASSERT_EQ(baseline.outcome, verification::ReplayOutcome::Completed) << baseline.detail;
    const auto certifiedBaseline = verification::VerifyIndependent(puzzle, original, 100);
    ASSERT_EQ(certifiedBaseline.status, io::VerificationStatus::Unavailable);
    ASSERT_EQ(certifiedBaseline.detail,
              "dynamic reference trajectories and area are not certified");
    const auto file = root / ("build/finite-track-bound-" +
                              std::to_string(opusoptima::test::ProcessId()) + ".solution");
    struct Cleanup {
        std::filesystem::path file;
        ~Cleanup() {
            std::error_code error;
            std::filesystem::remove(file, error);
        }
    } cleanup{file};
    for (const int count : {0, 1, 256, 257}) {
        SCOPED_TRACE(count);
        auto machine = original;
        io::SolutionObject track{"track", {10, 10}, {}, {1, 0, 0, 0, count}};
        for (int cell = 0; cell < count; ++cell) {
            track.bodyData.push_back(cell);
            track.bodyData.push_back(0);
        }
        track.bodyData.push_back(0);
        machine.objects.push_back(std::move(track));
        io::BinaryWriter::WriteSolutionFile(file, machine);
        auto decoded = io::BinaryParser::ParseSolutionFile(file);
        ASSERT_EQ(decoded.objects.back().bodyData, machine.objects.back().bodyData);
        const bool within = count >= 1 && count <= 256;
        for (const auto* input : {&machine, &decoded}) {
            const auto replay = verification::ReplayFiniteMachine(puzzle, *input, 100);
            const auto certified = verification::VerifyIndependent(puzzle, *input, 100);
            if (within) {
                EXPECT_EQ(replay.outcome, baseline.outcome) << replay.detail;
                EXPECT_EQ(replay.detail, baseline.detail);
                EXPECT_EQ(replay.cycles, baseline.cycles);
                EXPECT_EQ(replay.outputs, baseline.outputs);
                EXPECT_EQ(replay.acquired, baseline.acquired);
                EXPECT_EQ(replay.disposed, baseline.disposed);
                EXPECT_EQ(replay.remaining, baseline.remaining);
                EXPECT_EQ(certified.status, certifiedBaseline.status) << certified.detail;
                EXPECT_EQ(certified.detail, certifiedBaseline.detail);
                EXPECT_EQ(certified.reasonCode, certifiedBaseline.reasonCode);
            } else {
                EXPECT_EQ(replay.outcome, verification::ReplayOutcome::Unavailable);
                EXPECT_EQ(replay.detail, "independent track bound exceeded");
                EXPECT_EQ(certified.status, io::VerificationStatus::Unavailable);
                EXPECT_EQ(certified.detail, "independent track bound exceeded");
                EXPECT_EQ(certified.reasonCode, "independent-profile-uncertified");
            }
        }
    }
}
TEST(IndependentFiniteReplay, InternalChordAndStationaryTrackOverlapAreNotGlobalTopologyGates) {
    const auto puzzle = Ordinary();
    auto machine = Machine();
    // p0-p2 is a geometric chord, not an extra edge in the serialized path.
    machine.objects.push_back({"track", {10, 10}, {}, {1, 0, 0, 0, 4, 0, 0, 1, 0, 0, 1, 0, 2, 0}});
    machine.objects.push_back({"track", {10, 10}, {}, {1, 0, 0, 0, 2, 0, 1, 1, 1, 0}});
    machine.objects.push_back({"arm1", {10, 11}, {{'B', 0}}, {1, 0, 0, 1, 1}});
    const auto replay = verification::ReplayFiniteMachine(puzzle, machine, 100);
    EXPECT_EQ(replay.outcome, verification::ReplayOutcome::Completed) << replay.detail;
    EXPECT_EQ(replay.detail, "independent finite execution completed every ordinary output");
    // The unrelated four-instruction producer releases at 2,6,...,22.
    EXPECT_EQ(replay.cycles, 23);
    EXPECT_EQ(replay.outputs, (std::vector<std::size_t>{6}));
    const auto certified = verification::VerifyIndependent(puzzle, machine, 100);
    EXPECT_EQ(certified.status, io::VerificationStatus::Unavailable);
    EXPECT_EQ(certified.detail, "dynamic reference trajectories and area are not certified");
    EXPECT_EQ(certified.errorCycle, 1);
}
TEST(IndependentFiniteReplay, InternalChordUsesSequenceNeighborsAndBothEndpointNoOps) {
    const auto puzzle = Ordinary();
    auto machine = Machine();
    machine.objects[0].position = {-1, 0};
    machine.objects[1].position = {-1, 2};
    machine.objects[2].bodyData[1] = 3;
    machine.objects[2].instructions = {{'G', 0}, {'a', 1}, {'A', 2}, {'A', 3}, {'A', 4}, {'A', 5},
                                       {'g', 6}, {'a', 7}, {'a', 8}, {'a', 9}, {'a', 10}};
    machine.objects.push_back({"track", {}, {}, {1, 0, 0, 0, 4, 0, 0, 1, 0, 0, 1, 0, 2, 0}});
    const auto replay = verification::ReplayFiniteMachine(puzzle, machine, 100);
    ASSERT_EQ(replay.outcome, verification::ReplayOutcome::Completed) << replay.detail;
    EXPECT_EQ(replay.detail, "independent finite execution completed every ordinary output");
    // Index +/-1 and clamped endpoints put the held atom at p3+(-1,0).
    // Releases at 6+11*k yield the sixth output at cycle 61, hence 62 cycles.
    EXPECT_EQ(replay.cycles, 62);
    EXPECT_EQ(replay.outputs, (std::vector<std::size_t>{6}));
    std::vector<int> motionCycles, outputCycles;
    for (const auto& event : replay.events) {
        if (event.kind == verification::StaticProductionEvent::Kind::Motion)
            motionCycles.push_back(event.cycle);
        if (event.kind == verification::StaticProductionEvent::Kind::Output)
            outputCycles.push_back(event.cycle);
    }
    std::vector<int> expectedMotions, expectedOutputs;
    for (int iteration = 0; iteration < 6; ++iteration) {
        for (const int offset : {2, 3, 4})
            expectedMotions.push_back(11 * iteration + offset);
        expectedOutputs.push_back(11 * iteration + 6);
    }
    EXPECT_EQ(motionCycles, expectedMotions);
    EXPECT_EQ(outputCycles, expectedOutputs);
    const auto certified = verification::VerifyIndependent(puzzle, machine, 100);
    EXPECT_EQ(certified.status, io::VerificationStatus::Unavailable);
    EXPECT_EQ(certified.detail, "dynamic reference trajectories and area are not certified");
    EXPECT_EQ(certified.errorCycle, 2);
}
TEST(IndependentFiniteReplay, SharedOldBaseRejectsBothTrackDirectionsBeforeCompletion) {
    for (const char action : {'A', 'a'}) {
        SCOPED_TRACE(action);
        auto machine = Machine();
        machine.objects.push_back(
            {"track", {10, 10}, {}, {1, 0, 0, 0, 4, 0, 0, 1, 0, 0, 1, 0, 2, 0}});
        machine.objects.push_back({"track", {10, 10}, {}, {1, 0, 0, 0, 2, 0, 1, 1, 1, 0}});
        machine.objects.push_back({"arm1", {10, 11}, {{action, 0}}, {1, 0, 0, 1, 1}});
        const auto replay = verification::ReplayFiniteMachine(Ordinary(), machine, 100);
        EXPECT_EQ(replay.outcome, verification::ReplayOutcome::Unavailable);
        EXPECT_EQ(replay.detail, "ambiguous overlapping track reset/motion");
        EXPECT_EQ(replay.cycles, 0);
        EXPECT_EQ(replay.outputs, (std::vector<std::size_t>{0}));
        ASSERT_TRUE(replay.diagnostics);
        EXPECT_EQ(replay.diagnostics->completedCycles, 0);
        const auto certified = verification::VerifyIndependent(Ordinary(), machine, 100);
        EXPECT_EQ(certified.status, io::VerificationStatus::Unavailable);
        EXPECT_EQ(certified.detail, "ambiguous overlapping track reset/motion");
        EXPECT_EQ(certified.reasonCode, "independent-profile-uncertified");
        EXPECT_EQ(certified.errorCycle, 0);
    }
}
TEST(IndependentFiniteReplay, UniqueOldBaseMayEnterSharedCellButNextTrackActionIsAmbiguous) {
    for (const char action : {'A', 'a'}) {
        SCOPED_TRACE(action);
        auto machine = Machine();
        machine.objects.push_back(
            {"track", {10, 10}, {}, {1, 0, 0, 0, 4, 0, 0, 1, 0, 0, 1, 0, 2, 0}});
        machine.objects.push_back({"track", {10, 10}, {}, {1, 0, 0, 0, 2, 0, 1, 1, 1, 0}});
        machine.objects.push_back({"arm1",
                                   action == 'A' ? core::HexCoord{11, 10} : core::HexCoord{10, 12},
                                   {{action, 0}, {action, 1}},
                                   {1, 0, 0, 2, 1}});
        const auto replay = verification::ReplayFiniteMachine(Ordinary(), machine, 100);
        EXPECT_EQ(replay.outcome, verification::ReplayOutcome::Unavailable);
        EXPECT_EQ(replay.detail, "ambiguous overlapping track reset/motion");
        EXPECT_EQ(replay.cycles, 1);
        EXPECT_EQ(replay.outputs, (std::vector<std::size_t>{0}));
        ASSERT_TRUE(replay.diagnostics);
        EXPECT_EQ(replay.diagnostics->completedCycles, 1);
        // Certification stops at the first actual translation, before later ambiguity.
        const auto certified = verification::VerifyIndependent(Ordinary(), machine, 100);
        EXPECT_EQ(certified.status, io::VerificationStatus::Unavailable);
        EXPECT_EQ(certified.detail, "dynamic reference trajectories and area are not certified");
        EXPECT_EQ(certified.errorCycle, 0);
    }
}
TEST(IndependentFiniteReplay, EnteringSharedTrackCellWithAnotherBaseIsACollisionNotAmbiguity) {
    auto machine = Machine();
    machine.objects.push_back({"track", {10, 10}, {}, {1, 0, 0, 0, 4, 0, 0, 1, 0, 0, 1, 0, 2, 0}});
    machine.objects.push_back({"track", {10, 10}, {}, {1, 0, 0, 0, 2, 0, 1, 1, 1, 0}});
    machine.objects.push_back({"arm1", {11, 10}, {{'A', 0}}, {1, 0, 0, 1, 1}});
    machine.objects.push_back({"arm1", {10, 11}, {{'B', 0}}, {1, 0, 0, 1, 2}});
    const auto replay = verification::ReplayFiniteMachine(Ordinary(), machine, 100);
    EXPECT_EQ(replay.outcome, verification::ReplayOutcome::Rejected);
    EXPECT_EQ(replay.detail, "independent simultaneous arm-base collision");
    EXPECT_EQ(replay.cycles, 0);
    EXPECT_EQ(replay.outputs, (std::vector<std::size_t>{0}));
    const auto certified = verification::VerifyIndependent(Ordinary(), machine, 100);
    EXPECT_EQ(certified.status, io::VerificationStatus::Unavailable);
    EXPECT_EQ(certified.detail, "dynamic reference trajectories and area are not certified");
    EXPECT_EQ(certified.errorCycle, 0);
}
TEST(IndependentFiniteReplay, InitialOverlapIsRejectedBeforeUncertifiedMotion) {
    auto machine = Machine();
    machine.objects.push_back({"arm1", {1, 0}, {{'r', 0}}, {1, 0, 0, 1, 1}});
    const auto result = verification::VerifyIndependent(Ordinary(), machine, 100);
    EXPECT_EQ(result.status, io::VerificationStatus::Rejected);
    EXPECT_EQ(result.detail, "atom overlaps arm base");
}
TEST(IndependentFiniteReplay, RotatingWheelsRemainUncertifiedInBothDirections) {
    for (const char action : {'r', 'R'}) {
        auto machine = StaticMachine();
        machine.objects.push_back({"baron", {20, 0}, {{action, 0}}, {1, 0, 0, 1, 1}});
        const auto result = verification::VerifyIndependent(Ordinary(), machine, 100);
        EXPECT_EQ(result.status, io::VerificationStatus::Unavailable) << action;
        EXPECT_EQ(result.detail, "dynamic reference trajectories and area are not certified")
            << action;
    }
}
TEST(IndependentFiniteReplay, AnisotropicLatticeRotationCannotUseAPixelToleranceAsCertification) {
    const double euclideanEndpointY = 820.0 * std::sin(std::acos(-1.0) / 3.0);
    EXPECT_GT(std::abs(euclideanEndpointY - 710.0), 0.125);
    auto puzzle = Ordinary();
    puzzle.inputs[0].atoms.clear();
    for (int q = 0; q < 10; ++q) {
        puzzle.inputs[0].atoms.push_back({core::AtomType::Salt, {q, 0}});
        if (q > 0) puzzle.inputs[0].bonds.push_back({1, {q - 1, 0}, {q, 0}});
    }
    auto machine = Machine();
    machine.objects[1].position = {30, 0};
    const auto result = verification::VerifyIndependent(puzzle, machine, 100);
    EXPECT_EQ(result.status, io::VerificationStatus::Unavailable);
    EXPECT_EQ(result.detail, "dynamic reference trajectories and area are not certified");
}
TEST(IndependentFiniteReplay, InventoryBoundAllows8192AtomsAndRejectsTheNextInput) {
    auto puzzle = Ordinary();
    puzzle.inputs[0].atoms.clear();
    for (int q = 0; q < 64; ++q) {
        puzzle.inputs[0].atoms.push_back({core::AtomType::Salt, {q, 0}});
        if (q > 0) puzzle.inputs[0].bonds.push_back({1, {q - 1, 0}, {q, 0}});
    }
    io::SolutionData machine;
    machine.objects.push_back({"out-std", {-100, -100}, {}, {1, 0, 0, 0, 0}});
    for (int index = 0; index < 128; ++index)
        machine.objects.push_back(
            {"input", {(index % 16) * 64, (index / 16) * 2}, {}, {1, 0, 0, 0, 0}});
    const auto within = verification::ReplayFiniteMachine(puzzle, machine, 1);
    ASSERT_EQ(within.outcome, verification::ReplayOutcome::CycleLimit) << within.detail;
    EXPECT_EQ(within.acquired, 8192U);
    EXPECT_EQ(within.remaining, 8192U);
    machine.objects.push_back({"input", {0, 16}, {}, {1, 0, 0, 0, 0}});
    const auto outside = verification::ReplayFiniteMachine(puzzle, machine, 1);
    EXPECT_EQ(outside.outcome, verification::ReplayOutcome::Unavailable);
    EXPECT_EQ(outside.detail, "independent inventory bound exceeded");
}
TEST(IndependentFiniteReplay, ArmCountBoundIsCheckedBeforeConstructingTheNextTape) {
    const auto puzzle = Ordinary();
    io::SolutionData machine;
    machine.objects = {{"input", {}, {}, {1, 0, 0, 0, 0}}, {"out-std", {}, {}, {1, 0, 0, 0, 0}}};
    for (int index = 0; index < 128; ++index)
        machine.objects.push_back({"arm1", {10 + 3 * index, 0}, {{'B', 0}}, {1, 0, 0, 1, index}});
    const auto within = verification::ReplayFiniteMachine(puzzle, machine, 100);
    ASSERT_EQ(within.outcome, verification::ReplayOutcome::Completed) << within.detail;
    machine.objects.push_back({"arm1", {400, 0}, {{'Z', 0}}, {1, 0, 0, 1, 128}});
    const auto outside = verification::ReplayFiniteMachine(puzzle, machine, 100);
    EXPECT_EQ(outside.outcome, verification::ReplayOutcome::Unavailable);
    EXPECT_EQ(outside.detail, "independent arm count bound exceeded");
}
TEST(IndependentFiniteReplay, StaticWheelsWithDuplicateEditorNumbersCountTowardTheArmBound) {
    const auto puzzle = Ordinary();
    io::SolutionData machine;
    machine.objects = {{"input", {}, {}, {1, 0, 0, 0, 0}}, {"out-std", {}, {}, {1, 0, 0, 0, 0}}};
    for (int index = 0; index < 128; ++index)
        machine.objects.push_back({"baron", {10 + 4 * index, 0}, {}, {1, 0, 0, 0, 0}});
    const auto within = verification::ReplayFiniteMachine(puzzle, machine, 100);
    ASSERT_EQ(within.outcome, verification::ReplayOutcome::Completed) << within.detail;
    machine.objects.push_back({"arm1", {530, 0}, {{'Z', 0}}, {1, 0, 0, 1, 0}});
    const auto outside = verification::ReplayFiniteMachine(puzzle, machine, 100);
    EXPECT_EQ(outside.outcome, verification::ReplayOutcome::Unavailable);
    EXPECT_EQ(outside.detail, "independent arm count bound exceeded");
    machine.objects.back() = {"baron", {530, 0}, {}, {1, 0, 0, 0, 0}};
    const auto extraWheel = verification::ReplayFiniteMachine(puzzle, machine, 100);
    EXPECT_EQ(extraWheel.outcome, verification::ReplayOutcome::Unavailable);
    EXPECT_EQ(extraWheel.detail, "independent arm count bound exceeded");
}
TEST(IndependentFiniteReplay, HaltedLongTapeKeepsStaticCountsWithoutExecutingLaterMotion) {
    const auto puzzle = Ordinary();
    io::SolutionData machine;
    machine.objects = {{"input", {}, {}, {1, 0, 0, 0, 0}},
                       {"out-std", {}, {}, {1, 0, 0, 0, 0}},
                       {"arm1", {10, 0}, {{'B', 0}, {'r', 1}}, {1, 0, 0, 0, 0}}};
    for (int index = 2; index <= 20000; ++index)
        machine.objects[2].instructions.push_back({'O', index});
    const auto report = verification::VerifyIndependent(puzzle, machine, 100);
    ASSERT_TRUE(report.Passed()) << report.detail;
    EXPECT_EQ(report.metrics.cycles, 3);
    EXPECT_EQ(report.metrics.cost, 20);
    EXPECT_EQ(report.metrics.area, 3);
    EXPECT_EQ(report.metrics.instructionCount, 1);
    EXPECT_EQ(machine.objects[2].instructions.size(), 20001U);
    EXPECT_EQ(machine.objects[2].instructions[1].action, 'r');
}
TEST(IndependentFiniteReplay, TapeNormalizationPreservesActionsForPositiveAndNegativeStarts) {
    const auto puzzle = Ordinary();
    for (const int start : {-20000, -4, -1, 1, 4, 19997}) {
        auto machine = Machine();
        for (auto& instruction : machine.objects[2].instructions)
            instruction.parameter += start;
        const auto report = verification::ReplayFiniteMachine(puzzle, machine, 100);
        ASSERT_EQ(report.outcome, verification::ReplayOutcome::Completed)
            << start << ": " << report.detail;
        EXPECT_EQ(report.cycles, 23) << start;
        EXPECT_EQ(report.outputs, (std::vector<std::size_t>{6}));
        for (int index = 0; index < 4; ++index)
            EXPECT_EQ(machine.objects[2].instructions[index].parameter, start + index);
    }
}
TEST(IndependentFiniteReplay, CurrentTapePositionGateDistinguishesBoundsFromInt32Extremes) {
    const auto puzzle = Ordinary();
    for (const int position : {-20000, 20000}) {
        SCOPED_TRACE(position);
        auto machine = StaticMachine();
        machine.objects[2].instructions = {{'B', position}};
        const auto report = verification::VerifyIndependent(puzzle, machine, 100);
        ASSERT_TRUE(report.Passed()) << report.detail;
        EXPECT_EQ(report.metrics.cycles, 3);
    }
    for (const int position : {-20001, 20001, std::numeric_limits<std::int32_t>::min(),
                               std::numeric_limits<std::int32_t>::max()}) {
        SCOPED_TRACE(position);
        auto machine = StaticMachine();
        machine.objects[2].instructions = {{'B', position}};
        const auto report = verification::VerifyIndependent(puzzle, machine, 100);
        EXPECT_EQ(report.status, io::VerificationStatus::Unavailable);
        EXPECT_EQ(report.detail, "tape position exceeds the finite profile bounds");
    }
    auto sparse = StaticMachine();
    sparse.objects[2].instructions = {{'B', -20000}, {'r', 20000}};
    const auto report = verification::VerifyIndependent(puzzle, sparse, 100);
    ASSERT_TRUE(report.Passed()) << report.detail;
    EXPECT_EQ(report.metrics.cycles, 3);
    EXPECT_EQ(report.metrics.instructionCount, 1);
}
TEST(IndependentFiniteReplay, TapeGatePreservesEntryStatusAndErrorOrder) {
    const auto puzzlePath = root / "asset/puzzle/easy/just-salt.puzzle";
    const auto puzzle = io::BinaryParser::ParsePuzzleFile(puzzlePath);
    const auto file =
        root / ("build/independent-tape-gate-" + std::to_string(test::ProcessId()) + ".solution");
    struct Cleanup {
        std::filesystem::path file;
        ~Cleanup() {
            std::error_code error;
            std::filesystem::remove(file, error);
        }
    } cleanup{file};
    const auto check = [&](const std::vector<io::ArmInstruction>& tape,
                           io::VerificationStatus status, verification::ReplayOutcome outcome,
                           const std::string& detail, const std::string& reason) {
        auto machine = StaticMachine();
        machine.objects[2].instructions = tape;
        const auto direct = verification::VerifyIndependent(puzzle, machine, 100);
        const auto replay = verification::ReplayFiniteMachine(puzzle, machine, 100);
        io::BinaryWriter::WriteSolutionFile(file, machine);
        const auto decoded = verification::VerifyIndependentFiles(puzzlePath, file, 100);
        for (const auto* result : {&direct, &decoded}) {
            EXPECT_EQ(result->status, status) << result->detail;
            EXPECT_EQ(result->detail, detail);
            EXPECT_EQ(result->reasonCode, reason);
        }
        EXPECT_EQ(replay.outcome, outcome) << replay.detail;
        EXPECT_EQ(replay.detail, detail);
    };
    for (const int position : {-20000, 20000}) {
        SCOPED_TRACE(position);
        auto machine = StaticMachine();
        machine.objects[2].instructions = {{'B', position}};
        const auto direct = verification::VerifyIndependent(puzzle, machine, 100);
        ASSERT_TRUE(direct.Passed()) << direct.detail;
        EXPECT_EQ(direct.reasonCode, "independent-certified-completion");
        const auto replay = verification::ReplayFiniteMachine(puzzle, machine, 100);
        EXPECT_EQ(replay.outcome, verification::ReplayOutcome::Completed) << replay.detail;
        io::BinaryWriter::WriteSolutionFile(file, machine);
        const auto decoded = verification::VerifyIndependentFiles(puzzlePath, file, 100);
        EXPECT_EQ(decoded.status, direct.status);
        EXPECT_EQ(decoded.detail, direct.detail);
        EXPECT_EQ(decoded.reasonCode, direct.reasonCode);
    }
    const auto unavailable = io::VerificationStatus::Unavailable;
    const auto rejected = io::VerificationStatus::Rejected;
    const auto replayUnavailable = verification::ReplayOutcome::Unavailable;
    const auto replayRejected = verification::ReplayOutcome::Rejected;
    const std::string bounds = "tape position exceeds the finite profile bounds";
    const std::string duplicate = "invalid or duplicate tape position";
    const std::string unknown = "unexpanded repeat/reset or unknown independent instruction";
    for (const int position : {-20001, 20001, std::numeric_limits<std::int32_t>::min(),
                               std::numeric_limits<std::int32_t>::max()}) {
        SCOPED_TRACE(position);
        for (const char action : {'B', 'Z'})
            check({{action, position}}, unavailable, replayUnavailable, bounds,
                  "independent-profile-uncertified");
    }
    check({{'B', 0}, {'B', 0}}, rejected, replayRejected, duplicate,
          "independent-machine-rejected");
    check({{'B', 0}, {'Z', 0}}, rejected, replayRejected, duplicate,
          "independent-machine-rejected");
    check({{'Z', 0}, {'B', 20001}}, unavailable, replayUnavailable, unknown,
          "independent-profile-uncertified");
    check({{'Z', 0}, {'B', 0}}, unavailable, replayUnavailable, unknown,
          "independent-profile-uncertified");
    check({{'B', 0}, {'B', 0}, {'B', 20001}}, rejected, replayRejected, duplicate,
          "independent-machine-rejected");
    {
        std::ofstream stream(file, std::ios::binary | std::ios::trunc);
        stream.put('\a');
    }
    const auto broken = verification::VerifyIndependentFiles(puzzlePath, file, 100);
    EXPECT_EQ(broken.status, io::VerificationStatus::Error);
    EXPECT_EQ(broken.reasonCode, "independent-file-error");
    EXPECT_EQ(broken.detail, "Unexpected EOF while reading i32");
}
TEST(IndependentFiniteReplay, EmptyTapeAnchorsPositiveButNotNegativeLocalStarts) {
    const auto puzzle = Ordinary();
    for (const int start : {-4, 0, 4}) {
        SCOPED_TRACE(start);
        auto machine = Machine();
        for (auto& instruction : machine.objects[2].instructions)
            instruction.parameter += start;
        machine.objects.push_back({"arm1", {10, 0}, {}, {1, 0, 0, 0, 1}});
        const auto report = verification::ReplayFiniteMachine(puzzle, machine, 100);
        ASSERT_EQ(report.outcome, verification::ReplayOutcome::Completed) << report.detail;
        EXPECT_EQ(report.cycles, start == 4 ? 27 : 23);
        EXPECT_EQ(report.outputs, (std::vector<std::size_t>{6}));
        EXPECT_TRUE(machine.objects.back().instructions.empty());
    }
}
TEST(IndependentFiniteReplay,
     CompletionRequiresEveryPortAndRejectsWrongInstructionsAndPermissions) {
    auto puzzle = Ordinary();
    auto machine = Machine();
    puzzle.outputs.push_back(puzzle.outputs[0]);
    EXPECT_EQ(verification::ReplayFiniteMachine(puzzle, machine, 100).outcome,
              verification::ReplayOutcome::CycleLimit);
    puzzle = Ordinary();
    puzzle.permissionBits &= ~(1ULL << 23);
    EXPECT_EQ(verification::ReplayFiniteMachine(puzzle, machine, 100).outcome,
              verification::ReplayOutcome::Rejected);
    puzzle = Ordinary();
    machine.objects[2].instructions[0].action = 'Z';
    EXPECT_EQ(verification::ReplayFiniteMachine(puzzle, machine, 100).outcome,
              verification::ReplayOutcome::Unavailable);
    machine = Machine();
    puzzle.outputs[0].atoms[0].type = core::AtomType::Repeat;
    EXPECT_EQ(verification::ReplayFiniteMachine(puzzle, machine, 100).outcome,
              verification::ReplayOutcome::Unavailable);
}
TEST(IndependentFiniteReplay, CertifiedStaticWheelHasRealDeviceContactsAndIndependentMetrics) {
    auto puzzle = Ordinary();
    puzzle.outputs[0].atoms[0].type = core::AtomType::Water;
    io::SolutionData machine;
    machine.objects = {{"input", {2, 2}, {}, {1, 0, 0, 0, 0}},
                       {"out-std", {2, 2}, {}, {1, 0, 0, 0, 0}},
                       {"baron", {2, 0}, {}, {1, 0, 0, 0, 0}},
                       {"glyph-duplication", {2, 1}, {}, {1, 1, 0, 0, 0}}};
    const auto report = verification::VerifyIndependent(puzzle, machine, 100);
    ASSERT_TRUE(report.Passed()) << report.detail;
    EXPECT_EQ(report.authority, io::VerificationAuthority::Independent);
    EXPECT_EQ(report.metrics.cycles, 3);
    EXPECT_EQ(report.metrics.cost, 50);
    EXPECT_EQ(report.metrics.area, 8);
    EXPECT_EQ(report.metrics.instructionCount, 0);
    machine.objects.push_back({"baron", {10, 0}, {}, {1, 0, 0, 0, 0}});
    const auto duplicateEditorNumber = verification::VerifyIndependent(puzzle, machine, 100);
    ASSERT_TRUE(duplicateEditorNumber.Passed()) << duplicateEditorNumber.detail;
    EXPECT_EQ(duplicateEditorNumber.metrics.cost, 80);
    EXPECT_EQ(duplicateEditorNumber.metrics.area, 15);
    puzzle.permissionBits &= ~(1ULL << 28);
    EXPECT_EQ(verification::VerifyIndependent(puzzle, machine, 100).status,
              io::VerificationStatus::Rejected);
}
TEST(IndependentFiniteReplay,
     SimultaneouslyCrossingBasesAreRejectedBeforeUnrelatedOutputCompletes) {
    auto puzzle = Ordinary();
    io::SolutionData machine;
    machine.objects = {{"input", {5, 0}, {}, {1, 0, 0, 0, 0}},
                       {"out-std", {5, 0}, {}, {1, 0, 0, 0, 0}},
                       {"arm1", {0, 0}, {{'A', 0}}, {1, 3, 0, 1, 0}},
                       {"arm1", {1, 0}, {{'a', 0}}, {1, 0, 0, 1, 1}},
                       {"track", {0, 0}, {}, {1, 0, 0, 0, 2, 0, 0, 1, 0, 0}}};
    const auto report = verification::VerifyIndependent(puzzle, machine, 100);
    EXPECT_EQ(report.status, io::VerificationStatus::Rejected) << report.detail;
    EXPECT_EQ(report.authority, io::VerificationAuthority::None);
}

TEST(IndependentFiniteReplay, ProfileBoundsAreUnavailableRatherThanInvalidMachineVerdicts) {
    auto puzzle = Ordinary();
    auto machine = Machine();
    machine.objects[2].bodyData[0] = 4;
    EXPECT_EQ(verification::VerifyIndependent(puzzle, machine, 100).status,
              io::VerificationStatus::Unavailable);
    machine.objects[2].bodyData[0] = 0;
    EXPECT_EQ(verification::VerifyIndependent(puzzle, machine, 100).status,
              io::VerificationStatus::Unavailable);
    machine = Machine();
    machine.objects.push_back({"track", {10, 0}, {}, {1, 0, 0, 0, 3, 0, 0, 1, 0, 0, 1, 0}});
    EXPECT_EQ(verification::VerifyIndependent(puzzle, machine, 100).status,
              io::VerificationStatus::Unavailable);
    machine = Machine();
    machine.objects[2].typeName = "arm2";
    machine.objects[2].instructions[0].action = 'P';
    EXPECT_EQ(verification::VerifyIndependent(puzzle, machine, 100).status,
              io::VerificationStatus::Unavailable);
    machine = Machine();
    machine.objects[2].instructions[0].parameter = 30000;
    EXPECT_EQ(verification::VerifyIndependent(puzzle, machine, 100).status,
              io::VerificationStatus::Unavailable);
    machine = Machine();
    machine.objects[0].position = {1100, 0};
    EXPECT_EQ(verification::VerifyIndependent(puzzle, machine, 100).status,
              io::VerificationStatus::Unavailable);
}

#ifdef __linux__
TEST(IndependentFiniteReplay,
     IndependentPublicationProtectsExistingOutputFromUnknownOrBrokenMachines) {
    const auto puzzle = root / "asset/puzzle/easy/just-salt.puzzle";
    const auto destination =
        root / ("build/independent-publication-" + std::to_string(test::ProcessId()) + ".solution");
    struct Cleanup {
        std::filesystem::path file;
        ~Cleanup() {
            std::error_code error;
            std::filesystem::remove(file, error);
        }
    } cleanup{destination};
    io::VerificationOptions options;
    options.mode = io::VerificationMode::Independent;
    options.cycleLimit = 100;
    auto machine = StaticMachine();
    const auto report = io::PublishVerifiedSolution(puzzle, destination, machine, options);
    ASSERT_TRUE(report.Passed()) << report.detail;
    EXPECT_EQ(report.authority, io::VerificationAuthority::Independent);
    const auto before = io::BinaryParser::ReadAllBytes(destination);
    machine = Machine();
    EXPECT_EQ(io::PublishVerifiedSolution(puzzle, destination, machine, options).status,
              io::VerificationStatus::Unavailable);
    EXPECT_EQ(io::BinaryParser::ReadAllBytes(destination), before);
    machine = StaticMachine();
    machine.objects[2].instructions[0].action = 'Z';
    EXPECT_EQ(io::PublishVerifiedSolution(puzzle, destination, machine, options).status,
              io::VerificationStatus::Unavailable);
    EXPECT_EQ(io::BinaryParser::ReadAllBytes(destination), before);
    machine = Machine();
    std::erase_if(machine.objects, [](const auto& part) { return part.typeName == "out-std"; });
    EXPECT_FALSE(io::PublishVerifiedSolution(puzzle, destination, machine, options).Passed());
    EXPECT_EQ(io::BinaryParser::ReadAllBytes(destination), before);
}
#endif

TEST(IndependentFiniteReplay, DiagnosticsRetainTheLastCommittedStateOnFailure) {
    auto puzzle = Ordinary();
    auto machine = Machine();
    const auto limited = verification::VerifyIndependent(puzzle, machine, 1, true);
    ASSERT_EQ(limited.status, io::VerificationStatus::CycleLimit);
    ASSERT_TRUE(limited.diagnostics);
    EXPECT_EQ(limited.diagnostics->completedCycles, 1);
    EXPECT_EQ(limited.diagnostics->outputs, (std::vector<std::uint64_t>{0}));
    EXPECT_TRUE(limited.diagnostics->timingsCollected);
    EXPECT_GT(limited.diagnostics
                  ->counters[static_cast<std::size_t>(io::VerificationCounter::FragmentQueries)],
              0);
    puzzle.outputs[0].atoms[0].type = core::AtomType::Water;
    const auto failed = verification::ReplayFiniteMachine(puzzle, machine, 100, true);
    ASSERT_EQ(failed.outcome, verification::ReplayOutcome::Rejected) << failed.detail;
    ASSERT_TRUE(failed.diagnostics);
    EXPECT_GT(failed.diagnostics->completedCycles, 0);
    EXPECT_FALSE(failed.diagnostics->trace.empty());
    EXPECT_LE(failed.diagnostics->trace.size(), io::kVerificationTraceLimit);
    EXPECT_EQ(failed.diagnostics->inventory, failed.remaining);
    EXPECT_EQ(failed.diagnostics->phase, io::VerificationPhase::Motions);
    EXPECT_EQ(failed.diagnostics->cycle, failed.cycles);
    EXPECT_TRUE(std::all_of(
        failed.diagnostics->trace.begin(), failed.diagnostics->trace.end(),
        [](const auto& event) { return event.atomCount <= io::kVerificationTraceAtoms; }));
    puzzle = Ordinary();
    puzzle.permissionBits &= ~(1ULL << 23);
    const auto setup = verification::VerifyIndependent(puzzle, machine, 100, true);
    ASSERT_EQ(setup.status, io::VerificationStatus::Rejected);
    ASSERT_TRUE(setup.diagnostics);
    EXPECT_EQ(setup.diagnostics->phase, io::VerificationPhase::Setup);
    EXPECT_EQ(setup.diagnostics->completedCycles, 0);
}
TEST(IndependentFiniteReplay, OptionalTimingsCannotChangeVerdictsOrScores) {
    const auto puzzle = Ordinary();
    const auto machine = StaticMachine();
    const auto canonical = verification::VerifyIndependent(puzzle, machine, 100);
    const auto timed = verification::VerifyIndependent(puzzle, machine, 100, true);
    ASSERT_TRUE(canonical.Passed()) << canonical.detail;
    ASSERT_TRUE(timed.Passed()) << timed.detail;
    EXPECT_EQ(canonical.metrics.cycles, timed.metrics.cycles);
    EXPECT_EQ(canonical.metrics.cost, timed.metrics.cost);
    EXPECT_EQ(canonical.metrics.area, timed.metrics.area);
    EXPECT_EQ(canonical.metrics.instructionCount, timed.metrics.instructionCount);
    ASSERT_TRUE(canonical.diagnostics);
    ASSERT_TRUE(timed.diagnostics);
    EXPECT_FALSE(canonical.diagnostics->timingsCollected);
    EXPECT_EQ(canonical.diagnostics->observeMicros, 0);
    EXPECT_EQ(canonical.diagnostics->snapshotMicros, 0);
    EXPECT_EQ(canonical.diagnostics->fragmentMicros, 0);
    EXPECT_EQ(canonical.diagnostics->heldMicros, 0);
    EXPECT_EQ(canonical.diagnostics->collisionMicros, 0);
    EXPECT_LE(timed.diagnostics->collisionMicros, timed.diagnostics->elapsedMicros);
    EXPECT_LE(timed.diagnostics->snapshotMicros, timed.diagnostics->elapsedMicros);
    EXPECT_LE(timed.diagnostics->fragmentMicros, timed.diagnostics->elapsedMicros);
    EXPECT_LE(timed.diagnostics->heldMicros, timed.diagnostics->elapsedMicros);
    EXPECT_LE(timed.diagnostics->observeMicros, timed.diagnostics->elapsedMicros);
    EXPECT_TRUE(timed.diagnostics->timingsCollected);
    EXPECT_TRUE(std::all_of(canonical.diagnostics->phaseMicros.begin(),
                            canonical.diagnostics->phaseMicros.end(),
                            [](auto value) { return value == 0; }));
    // Sub-microsecond work may truncate to zero, even when timings are collected.
    EXPECT_GE(timed.diagnostics->elapsedMicros, 0ULL);
    for (const auto micros : timed.diagnostics->phaseMicros) {
        EXPECT_GE(micros, 0ULL);
        EXPECT_LE(micros, timed.diagnostics->elapsedMicros);
    }
    EXPECT_EQ(canonical.diagnostics->completedCycles, timed.diagnostics->completedCycles);
    EXPECT_EQ(canonical.diagnostics->outputs, timed.diagnostics->outputs);
    EXPECT_EQ(canonical.diagnostics->counters, timed.diagnostics->counters);
}
TEST(IndependentFiniteReplay, OptionalTimingsCoverMovingFragmentAndHeldQueries) {
    const auto puzzle = Ordinary();
    const auto machine = Machine();
    for (const std::uint64_t cycleLimit : {1ULL, 100ULL}) {
        SCOPED_TRACE(cycleLimit);
        const auto canonical = verification::ReplayFiniteMachine(puzzle, machine, cycleLimit);
        const auto timed = verification::ReplayFiniteMachine(puzzle, machine, cycleLimit, true);
        EXPECT_EQ(canonical.outcome, timed.outcome);
        EXPECT_EQ(timed.outcome, cycleLimit == 1 ? verification::ReplayOutcome::CycleLimit
                                                 : verification::ReplayOutcome::Completed);
        EXPECT_EQ(canonical.cycles, timed.cycles);
        EXPECT_EQ(canonical.outputs, timed.outputs);
        EXPECT_EQ(canonical.acquired, timed.acquired);
        EXPECT_EQ(canonical.disposed, timed.disposed);
        EXPECT_EQ(canonical.remaining, timed.remaining);
        ASSERT_TRUE(canonical.diagnostics);
        ASSERT_TRUE(timed.diagnostics);
        EXPECT_EQ(canonical.diagnostics->counters, timed.diagnostics->counters);
        EXPECT_EQ(canonical.diagnostics->completedCycles, timed.diagnostics->completedCycles);
        EXPECT_EQ(canonical.diagnostics->outputs, timed.diagnostics->outputs);
        EXPECT_GT(
            timed.diagnostics
                ->counters[static_cast<std::size_t>(io::VerificationCounter::FragmentQueries)],
            0);
        if (timed.outcome == verification::ReplayOutcome::Completed) {
            EXPECT_GT(
                timed.diagnostics
                    ->counters[static_cast<std::size_t>(io::VerificationCounter::HeldQueries)],
                0);
        }
        EXPECT_FALSE(canonical.diagnostics->timingsCollected);
        EXPECT_TRUE(timed.diagnostics->timingsCollected);
        EXPECT_EQ(canonical.diagnostics->snapshotMicros, 0);
        EXPECT_EQ(canonical.diagnostics->fragmentMicros, 0);
        EXPECT_EQ(canonical.diagnostics->heldMicros, 0);
        EXPECT_EQ(canonical.diagnostics->collisionMicros, 0);
        EXPECT_LE(timed.diagnostics->collisionMicros, timed.diagnostics->elapsedMicros);
        EXPECT_LE(timed.diagnostics->snapshotMicros, timed.diagnostics->elapsedMicros);
        EXPECT_LE(timed.diagnostics->fragmentMicros, timed.diagnostics->elapsedMicros);
        EXPECT_LE(timed.diagnostics->heldMicros, timed.diagnostics->elapsedMicros);
    }
}

TEST(IndependentFiniteReplay,
     TimeoutOrCycleLimitKeepsBoundedProgressWithoutAuthorizingPublication) {
    auto puzzle = Ordinary();
    puzzle.outputs[0].atoms[0].type = core::AtomType::Water;
    io::SolutionData machine;
    for (int i = 0; i < 1000; ++i)
        machine.objects.push_back({"input", {i, 0}, {}, {1, 0, 0, 0, 0}});
    machine.objects.push_back({"out-std", {0, 2}, {}, {1, 0, 0, 0, 0}});
    const auto report = verification::VerifyIndependent(puzzle, machine, 150000, true);
    EXPECT_TRUE(report.status == io::VerificationStatus::Timeout ||
                report.status == io::VerificationStatus::CycleLimit)
        << report.detail;
    EXPECT_EQ(report.authority, io::VerificationAuthority::None);
    ASSERT_TRUE(report.diagnostics);
    EXPECT_EQ(report.diagnostics->inventoryHighWater, 1000);
    EXPECT_EQ(report.diagnostics->inventory, 1000);
    EXPECT_LE(report.diagnostics->trace.size(), io::kVerificationTraceLimit);
    EXPECT_TRUE(report.diagnostics->traceTruncated);
}

TEST(IndependentFiniteReplay, ActualOrdinarySingleAtomMachineReplaysWithoutNativeNormalization) {
    const auto puzzle =
        io::BinaryParser::ParsePuzzleFile(root / "asset/puzzle/easy/just-salt.puzzle");
    const auto machine = solvers::SingleAtomSolver{}.Solve(puzzle);
    const auto report = verification::ReplayFiniteMachine(puzzle, machine, 100);
    ASSERT_EQ(report.outcome, verification::ReplayOutcome::Completed) << report.detail;
    EXPECT_EQ(report.outputs, (std::vector<std::size_t>{6}));
}

namespace {
io::PuzzleData RepeatingPuzzle(int size) {
    auto puzzle = Ordinary();
    io::Molecule material;
    for (int q = 0; q < size; ++q) {
        material.atoms.push_back({core::AtomType::Salt, {q, 0}});
        if (q) material.bonds.push_back({1, {q - 1, 0}, {q, 0}});
    }
    puzzle.inputs = {material};
    puzzle.outputs = {
        {{{core::AtomType::Salt, {}}, {core::AtomType::Repeat, {1, 0}}}, {{1, {}, {1, 0}}}}};
    return puzzle;
}
io::SolutionData RepeatingMachine() {
    auto machine = StaticMachine();
    machine.objects[1].typeName = "out-rep";
    return machine;
}
} // namespace
TEST(IndependentFiniteReplay, RepeatingOptInUsesRealInventoryAndStableHighWater) {
    for (int size : {1, 3, 5, 6, 7}) {
        auto puzzle = RepeatingPuzzle(size);
        const auto machine = RepeatingMachine();
        const auto original = io::BinaryWriter::WriteSolution(machine);
        const auto report = verification::ReplayRepeatingFiniteMachine(puzzle, machine, 8);
        EXPECT_FALSE(report.certificationEligible);
        EXPECT_EQ(report.execution.outcome, size >= 6 ? verification::ReplayOutcome::Completed
                                                      : verification::ReplayOutcome::CycleLimit)
            << report.execution.detail;
        EXPECT_EQ(report.execution.outputs, (std::vector<std::size_t>{std::min(size, 6)}));
        EXPECT_EQ(report.execution.remaining, size);
        EXPECT_EQ(report.execution.acquired, size);
        EXPECT_EQ(io::BinaryWriter::WriteSolution(machine), original);
        EXPECT_EQ(verification::ReplayFiniteMachine(puzzle, machine, 8).outcome,
                  verification::ReplayOutcome::Unavailable);
        EXPECT_EQ(verification::VerifyIndependent(puzzle, machine, 8).status,
                  io::VerificationStatus::Unavailable);
    }
}
TEST(IndependentFiniteReplay, RepeatingRigidFramesWrongMaterialAndIndependentPorts) {
    for (int rotation = 0; rotation < 6; ++rotation) {
        auto puzzle = RepeatingPuzzle(6);
        auto machine = RepeatingMachine();
        for (int i : {0, 1}) {
            machine.objects[i].position = {5, 4};
            machine.objects[i].bodyData[1] = rotation;
        }
        auto report = verification::ReplayRepeatingFiniteMachine(puzzle, machine, 4);
        EXPECT_EQ(report.execution.outcome, verification::ReplayOutcome::Completed)
            << report.execution.detail;
        puzzle.inputs[0].atoms[3].type = core::AtomType::Fire;
        report.execution = verification::ReplayRepeatingFiniteMachine(puzzle, machine, 4).execution;
        EXPECT_EQ(report.execution.outcome, verification::ReplayOutcome::CycleLimit);
        EXPECT_EQ(report.execution.outputs, (std::vector<std::size_t>{3}));
    }
    auto puzzle = RepeatingPuzzle(3);
    auto machine = RepeatingMachine();
    puzzle.outputs.push_back(puzzle.outputs[0]);
    auto second = machine.objects[1];
    second.bodyData[2] = 1;
    machine.objects.push_back(second);
    auto report = verification::ReplayRepeatingFiniteMachine(puzzle, machine, 8);
    EXPECT_EQ(report.execution.outputs, (std::vector<std::size_t>{3, 3}));
    EXPECT_EQ(report.execution.remaining, 3);
    machine.objects.back().bodyData[2] = 0;
    EXPECT_EQ(verification::ReplayRepeatingFiniteMachine(puzzle, machine, 8).execution.outcome,
              verification::ReplayOutcome::Unavailable);
}
TEST(IndependentFiniteReplay, RepeatingMisalignedAnchorAndMechanicalBondRemoval) {
    const auto puzzle = RepeatingPuzzle(6);
    auto machine = RepeatingMachine();
    machine.objects[1].position = {0, 1};
    auto report = verification::ReplayRepeatingFiniteMachine(puzzle, machine, 4);
    EXPECT_EQ(report.execution.outputs, (std::vector<std::size_t>{0}));
    machine.objects[1].position = {};
    machine.objects.push_back({"unbonder", {2, 0}, {}, {1, 0}});
    report.execution = verification::ReplayRepeatingFiniteMachine(puzzle, machine, 4).execution;
    EXPECT_EQ(report.execution.outcome, verification::ReplayOutcome::CycleLimit)
        << report.execution.detail;
    EXPECT_EQ(report.execution.outputs, (std::vector<std::size_t>{3}));
}
TEST(IndependentFiniteReplay, RepeatingMechanicalGrowthThenShorteningRetainsHighWater) {
    auto puzzle = RepeatingPuzzle(6);
    puzzle.outputs.push_back(puzzle.outputs[0]);
    auto machine = RepeatingMachine();
    machine.objects[0].position = {2, 0};
    machine.objects[2] = {
        "arm1", {2, -1}, {{'G', 0}, {'A', 1}, {'A', 3}, {'A', 5}, {'B', 7}}, {1, 1, 0, 8, 0}};
    machine.objects.push_back({"track", {2, -1}, {}, {1, 0, 0, 0, 4, 0, 0, -1, 0, -2, 0, -3, 0}});
    machine.objects.push_back({"out-rep", {20, 0}, {}, {1, 0, 1, 0, 0}});
    const auto original = io::BinaryWriter::WriteSolution(machine);
    for (const auto [limit, expected] : {std::pair{1, 0U}, {2, 0U}, {4, 6U}, {6, 6U}, {8, 6U}}) {
        const auto report = verification::ReplayRepeatingFiniteMachine(puzzle, machine, limit);
        EXPECT_EQ(report.execution.outcome, verification::ReplayOutcome::CycleLimit)
            << report.execution.detail;
        EXPECT_EQ(report.execution.outputs, (std::vector<std::size_t>{expected, 0}));
        EXPECT_EQ(report.execution.remaining, 6U);
        EXPECT_FALSE(report.certificationEligible);
    }
    EXPECT_EQ(io::BinaryWriter::WriteSolution(machine), original);
}
TEST(IndependentFiniteReplay, RepeatingMixedOrdinaryConsumptionAndStrictOptInDomain) {
    auto puzzle = RepeatingPuzzle(6);
    auto machine = RepeatingMachine();
    puzzle.inputs.push_back({{{core::AtomType::Fire, {0, 0}}}, {}});
    puzzle.outputs.push_back(puzzle.inputs.back());
    machine.objects.push_back({"input", {20, 0}, {}, {1, 0, 1, 0, 0}});
    machine.objects.push_back({"out-std", {20, 0}, {}, {1, 0, 1, 0, 0}});
    const auto before = puzzle;
    const auto report = verification::ReplayRepeatingFiniteMachine(puzzle, machine, 8);
    EXPECT_EQ(report.execution.outcome, verification::ReplayOutcome::Completed)
        << report.execution.detail;
    EXPECT_EQ(report.execution.outputs, (std::vector<std::size_t>{6, 6}));
    EXPECT_EQ(report.execution.remaining, 6U);
    EXPECT_EQ(report.execution.acquired, 12U);
    for (const auto* pair : {&puzzle.inputs, &puzzle.outputs}) {
        const auto& original = pair == &puzzle.inputs ? before.inputs : before.outputs;
        ASSERT_EQ(pair->size(), original.size());
        for (std::size_t i = 0; i < pair->size(); ++i) {
            ASSERT_EQ((*pair)[i].atoms.size(), original[i].atoms.size());
            ASSERT_EQ((*pair)[i].bonds.size(), original[i].bonds.size());
            for (std::size_t j = 0; j < original[i].atoms.size(); ++j) {
                EXPECT_EQ((*pair)[i].atoms[j].type, original[i].atoms[j].type);
                EXPECT_EQ((*pair)[i].atoms[j].position, original[i].atoms[j].position);
            }
            for (std::size_t j = 0; j < original[i].bonds.size(); ++j) {
                EXPECT_EQ((*pair)[i].bonds[j].bits, original[i].bonds[j].bits);
                EXPECT_EQ((*pair)[i].bonds[j].from, original[i].bonds[j].from);
                EXPECT_EQ((*pair)[i].bonds[j].to, original[i].bonds[j].to);
            }
        }
    }
    machine.objects[2].instructions = {{'C', 0}, {'X', 1}};
    EXPECT_EQ(verification::ReplayRepeatingFiniteMachine(puzzle, machine, 8).execution.outcome,
              verification::ReplayOutcome::Unavailable);
    machine = RepeatingMachine();
    puzzle = RepeatingPuzzle(6);
    puzzle.outputs[0].atoms.back().position = {0, 0};
    EXPECT_EQ(verification::ReplayRepeatingFiniteMachine(puzzle, machine, 8).execution.outcome,
              verification::ReplayOutcome::Unavailable);
}
