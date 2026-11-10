#include "Verification/FiniteTapeExpansion.hpp"
#include "BinaryWriter.hpp"
#include "UnitTestConstants.hpp"
#include "Verification/IndependentMetrics.hpp"

#include "RepoRoot.hpp"
#include <algorithm>
#include <filesystem>
#include <gtest/gtest.h>
#include <limits>

namespace opusoptima::verification { namespace {
using Error = TapeExpansionError;
io::PuzzleData Puzzle() {
    io::PuzzleData puzzle;
    puzzle.version = 3;
    puzzle.permissionBits = ~0ULL;
    return puzzle;
}
io::SolutionData Machine(std::vector<io::ArmInstruction> tape) {
    io::SolutionData machine;
    machine.objects.push_back({"arm1", {}, std::move(tape), {1, 0, 0, 0, 0}});
    return machine;
}
std::vector<io::ArmInstruction> Tape(std::string_view text) {
    std::vector<io::ArmInstruction> tape;
    for (std::size_t i = 0; i < text.size(); ++i)
        if (text[i] != '_') tape.push_back({text[i], static_cast<std::int32_t>(i)});
    return tape;
}
TEST(FiniteTapeExpansion, ExpiredEnclosingDeadlineCancelsCopyWithoutPublishingTape) {
    const auto expired = std::chrono::steady_clock::now() - std::chrono::seconds(1);
    for (const auto raw : {"GrCC", "GrX", "G", ""}) {
        SCOPED_TRACE(raw);
        const auto machine = Machine(Tape(raw));
        const auto result = ExpandFiniteArm1Tape(Puzzle(), machine, expired);
        ASSERT_TRUE(std::holds_alternative<TapeExpansionFailure>(result));
        EXPECT_EQ(std::get<TapeExpansionFailure>(result).code, Error::ResourceUnavailable);
        EXPECT_TRUE(
            std::holds_alternative<ExpandedFiniteTape>(ExpandFiniteArm1Tape(Puzzle(), machine)));
        EXPECT_TRUE(std::holds_alternative<ExpandedFiniteTape>(ExpandFiniteArm1Tape(
            Puzzle(), machine, std::chrono::steady_clock::now() + std::chrono::hours(1))));
    }
    const auto malformed = ExpandFiniteArm1Tape(Puzzle(), Machine({{'G', 0}, {'C', 0}}), expired);
    ASSERT_TRUE(std::holds_alternative<TapeExpansionFailure>(malformed));
    EXPECT_EQ(std::get<TapeExpansionFailure>(malformed).code, Error::Malformed);
}
TEST(FiniteTapeExpansion, LiteralSlotsCountsOriginsAndStrictRoundTrip) {
    const auto path = opusoptima::test::RepoRoot(__FILE__) /
                      ("build/finite-expansion-" + std::to_string(test::ProcessId()) + ".solution");
    struct Cleanup {
        std::filesystem::path path;
        ~Cleanup() {
            std::filesystem::remove(path);
        }
    } cleanup{path};
    for (const auto& [raw, expected] :
         std::vector<std::pair<std::string, std::string>>{{"G_C__C", "G G  G"},
                                                          {"C", " "},
                                                          {"X", " "},
                                                          {"", ""},
                                                          {"GrX", "GrgR"},
                                                          {"rrrX", "rrrRRR"},
                                                          {"RRRX", "RRRrrr"},
                                                          {"rXX", "rR "},
                                                          {"rXC_GX", "rRrRGg"},
                                                          {"O_C", "O O"},
                                                          {" _C", "   "},
                                                          {"BrX", "BrR"},
                                                          {"GCCg", "GGGg"},
                                                          {"GrCC", "GrGGr"},
                                                          {"G_XC", "G gG g"},
                                                          {"G_C__O_C", "G G  O O"}}) {
        SCOPED_TRACE(raw);
        const auto machine = Machine(Tape(raw));
        const auto saved = machine;
        auto result = ExpandFiniteArm1Tape(Puzzle(), machine);
        ASSERT_EQ(machine.objects[0].instructions.size(), saved.objects[0].instructions.size());
        for (std::size_t i = 0; i < saved.objects[0].instructions.size(); ++i) {
            EXPECT_EQ(machine.objects[0].instructions[i].action,
                      saved.objects[0].instructions[i].action);
            EXPECT_EQ(machine.objects[0].instructions[i].parameter,
                      saved.objects[0].instructions[i].parameter);
        }
        EXPECT_EQ(machine.objects[0].bodyData, saved.objects[0].bodyData);
        ASSERT_TRUE(std::holds_alternative<ExpandedFiniteTape>(result));
        const auto& out = std::get<ExpandedFiniteTape>(result);
        ASSERT_EQ(out.slots.size(), expected.size());
        ASSERT_EQ(out.origins.size(), expected.size());
        EXPECT_EQ(out.rawEntries, machine.objects[0].instructions.size());
        EXPECT_EQ(out.rawSpan, raw.size());
        EXPECT_EQ(out.expandedLength, expected.size());
        std::int64_t count = 0;
        for (std::size_t i = 0; i < expected.size(); ++i) {
            EXPECT_EQ(out.slots[i].action, expected[i]);
            EXPECT_EQ(out.slots[i].parameter, i);
            if (std::string_view("GgRrPp").find(expected[i]) != std::string_view::npos) ++count;
        }
        EXPECT_EQ(out.staticInstructions, count);
        EXPECT_EQ(out.rawStart,
                  raw.empty() ? std::optional<std::int32_t>{} : std::optional<std::int32_t>{0});
        EXPECT_EQ(out.haltSlot, raw.find('B') == std::string::npos
                                    ? std::optional<std::int64_t>{}
                                    : std::optional<std::int64_t>{raw.find('B')});
        io::BinaryWriter::WriteSolutionFile(path, machine);
        const auto decoded = io::BinaryParser::ParseSolutionFile(path);
        const auto again = ExpandFiniteArm1Tape(Puzzle(), decoded);
        ASSERT_TRUE(std::holds_alternative<ExpandedFiniteTape>(again));
        const auto& second = std::get<ExpandedFiniteTape>(again);
        EXPECT_EQ(second.slots.size(), out.slots.size());
        for (std::size_t i = 0; i < out.slots.size(); ++i) {
            EXPECT_EQ(second.slots[i].action, out.slots[i].action);
            EXPECT_EQ(second.slots[i].parameter, out.slots[i].parameter);
            EXPECT_EQ(second.origins[i].kind, out.origins[i].kind);
            EXPECT_EQ(second.origins[i].rawOrdinal, out.origins[i].rawOrdinal);
        }
        auto explicitMachine = machine;
        explicitMachine.objects[out.armObject].instructions = out.slots;
        io::BinaryWriter::WriteSolutionFile(path, explicitMachine);
        const auto explicitDecoded = io::BinaryParser::ParseSolutionFile(path);
        EXPECT_EQ(explicitDecoded.objects[0].instructions.size(), out.slots.size());
        IndependentMetrics metrics;
        metrics.Initialize(Puzzle(), explicitMachine);
        EXPECT_EQ(metrics.Finish(0).instructionCount, count);
        EXPECT_EQ(machine.objects[0].instructions.size(), Tape(raw).size());
    }
}
TEST(FiniteTapeExpansion, LeafProvenanceSurvivesCopiesAndHoles) {
    auto machine = Machine({{'C', 5}, {'G', 0}, {'C', 2}});
    auto out = std::get<ExpandedFiniteTape>(ExpandFiniteArm1Tape(Puzzle(), machine));
    EXPECT_EQ(out.rawEntries, 3);
    EXPECT_EQ(out.rawSpan, 6);
    EXPECT_EQ(out.staticInstructions, 3);
    EXPECT_EQ(out.origins[1].kind, TapeOrigin::Kind::Hole);
    EXPECT_EQ(out.origins[2].kind, TapeOrigin::Kind::Copy);
    EXPECT_EQ(out.origins[2].rawOrdinal, 1U);
    EXPECT_EQ(out.origins[2].rawIndex, 0);
    EXPECT_EQ(out.origins[2].copySite, 2);
    EXPECT_EQ(out.origins[5].copySite, 5);
    EXPECT_EQ(out.origins[5].sourceSlot, 0);
    out = std::get<ExpandedFiniteTape>(ExpandFiniteArm1Tape(Puzzle(), Machine(Tape("G_XC"))));
    EXPECT_EQ(out.origins[4].kind, TapeOrigin::Kind::Copy);
    EXPECT_FALSE(out.origins[4].rawOrdinal);
    EXPECT_EQ(out.origins[5].rawOrdinal, 1U);
    EXPECT_EQ(out.origins[5].resetStep, 0);
    EXPECT_EQ(out.origins[5].sourceSlot, 2);
    auto cleared = ExpandFiniteArm1Tape(Puzzle(), Machine(Tape("G__rCC___g")));
    ASSERT_TRUE(std::holds_alternative<ExpandedFiniteTape>(cleared));
    out = std::get<ExpandedFiniteTape>(cleared);
    EXPECT_EQ(out.slots[6].action, ' ');
    EXPECT_EQ(out.origins[6].kind, TapeOrigin::Kind::Copy);
    EXPECT_FALSE(out.origins[6].rawOrdinal);
    EXPECT_EQ(out.origins[6].sourceSlot, 1);
    EXPECT_EQ(out.origins[6].copySite, 5);
    // The second C clears the first copy's r at slot 7; a later raw write is legal.
    const auto rewritten = ExpandFiniteArm1Tape(Puzzle(), Machine(Tape("G__rCC_g")));
    ASSERT_TRUE(std::holds_alternative<ExpandedFiniteTape>(rewritten));
    EXPECT_EQ(std::get<ExpandedFiniteTape>(rewritten).slots[7].action, 'g');
    EXPECT_EQ(std::get<ExpandedFiniteTape>(rewritten).origins[7].kind, TapeOrigin::Kind::Raw);
}
TEST(FiniteTapeExpansion, FailuresAreAtomicAndOrdered) {
    const auto fail = [](io::SolutionData machine, Error code, io::PuzzleData puzzle = Puzzle()) {
        const auto result = ExpandFiniteArm1Tape(puzzle, machine);
        ASSERT_TRUE(std::holds_alternative<TapeExpansionFailure>(result));
        EXPECT_EQ(std::get<TapeExpansionFailure>(result).code, code);
        const auto path =
            opusoptima::test::RepoRoot(__FILE__) /
            ("build/finite-failure-" + std::to_string(test::ProcessId()) + ".solution");
        struct Cleanup {
            std::filesystem::path path;
            ~Cleanup() {
                std::filesystem::remove(path);
            }
        } cleanup{path};
        // Malformed object bodies are rejected by the codec before conversion.
        if (std::all_of(machine.objects.begin(), machine.objects.end(), [](const auto& part) {
                return part.bodyData.size() >= 5 && part.typeName != "pipe" &&
                       part.typeName != "track";
            })) {
            io::BinaryWriter::WriteSolutionFile(path, machine);
            const auto decoded =
                ExpandFiniteArm1Tape(puzzle, io::BinaryParser::ParseSolutionFile(path));
            ASSERT_TRUE(std::holds_alternative<TapeExpansionFailure>(decoded));
            EXPECT_EQ(std::get<TapeExpansionFailure>(decoded).code, code);
        }
    };
    for (const auto raw : {"rrrXG", "rrrXC", "rrrX X", "GrCCG"})
        fail(Machine(Tape(raw)), Error::Conflict);
    fail(Machine({{'C', 0}, {'C', 0}}), Error::Malformed);
    fail(Machine({{'?', 0}, {'G', 0}}), Error::Malformed);
    fail(Machine({{'?', 20001}}), Error::BoundExceeded);
    fail(Machine({{'C', -1}}), Error::OutsideDomain);
    fail(Machine(Tape("BC")), Error::OutsideDomain);
    for (char action : std::string("?AaEe"))
        fail(Machine({{action, 0}}), Error::OutsideDomain);
    for (const auto type : {"track", "piston", "arm2", "baron", "out-rep", "unknown"}) {
        auto machine = Machine({});
        machine.objects.push_back({type, {}, {}, {1, 0, 0, 0, 0}});
        fail(machine, Error::OutsideDomain);
    }
    auto machine = Machine({});
    machine.objects.push_back(machine.objects.front());
    fail(machine, Error::OutsideDomain);
    machine = Machine({});
    machine.objects[0].bodyData.clear();
    fail(machine, Error::Malformed);
    for (int bad : {-1, 6}) {
        machine = Machine({});
        machine.objects[0].bodyData[1] = bad;
        fail(machine, Error::Malformed);
    }
    for (int bad : {0, 4}) {
        machine = Machine({});
        machine.objects[0].bodyData[0] = bad;
        fail(machine, Error::Malformed);
    }
    for (const auto& [action, bit] : std::vector<std::pair<char, int>>{
             {'G', 23}, {'g', 22}, {'P', 26}, {'O', 25}, {'C', 25}, {'X', 24}}) {
        auto puzzle = Puzzle();
        puzzle.permissionBits &= ~(1ULL << bit);
        fail(Machine({{action, 0}}), Error::OutsideDomain, puzzle);
    }
    auto puzzle = Puzzle();
    puzzle.permissionBits &= ~(1ULL << 22);
    fail(Machine(Tape("GX")), Error::OutsideDomain, puzzle);
    puzzle = Puzzle();
    puzzle.inputs.push_back({{{core::AtomType::Repeat, {}}}, {}});
    fail(Machine({}), Error::OutsideDomain, puzzle);
    fail(Machine({{'G', std::numeric_limits<std::int32_t>::max()}}), Error::BoundExceeded);
    fail(Machine({{'X', std::numeric_limits<std::int32_t>::min()}}), Error::BoundExceeded);
    fail(Machine({{'G', 20000}, {'C', 20001}}), Error::BoundExceeded);
    fail(Machine({{'r', 19998}, {'r', 19999}, {'X', 20000}}), Error::BoundExceeded);
    machine = Machine(std::vector<io::ArmInstruction>(40002, {'G', 0}));
    fail(machine, Error::BoundExceeded);
    machine = Machine({});
    machine.objects.resize(1025, {"bonder", {}, {}, {1, 0, 0, 0, 0}});
    fail(machine, Error::BoundExceeded);
    machine = Machine({});
    machine.objects.push_back({"pipe", {}, {}, {1, 0, 0, 0, 0, 0, 2, 0, 0}});
    fail(machine, Error::Malformed);
}
TEST(FiniteTapeExpansion, ResourceEdgesAndNegativeResetStart) {
    for (int offset : {-3, 19997}) {
        auto machine = Machine(Tape("GrgX"));
        for (auto& inst : machine.objects[0].instructions)
            inst.parameter += offset;
        const auto result = ExpandFiniteArm1Tape(Puzzle(), machine);
        ASSERT_TRUE(std::holds_alternative<ExpandedFiniteTape>(result));
        const auto& out = std::get<ExpandedFiniteTape>(result);
        EXPECT_EQ(out.rawStart, offset);
        EXPECT_EQ(out.slots.back().parameter, offset + 3);
        EXPECT_EQ(out.slots.back().action, 'R');
    }
    auto result = ExpandFiniteArm1Tape(Puzzle(), Machine({{'r', 19999}, {'X', 20000}}));
    ASSERT_TRUE(std::holds_alternative<ExpandedFiniteTape>(result));
    result = ExpandFiniteArm1Tape(Puzzle(), Machine({{'X', -20000}, {'O', 20000}}));
    ASSERT_TRUE(std::holds_alternative<ExpandedFiniteTape>(result));
    const auto& out = std::get<ExpandedFiniteTape>(result);
    EXPECT_EQ(out.rawSpan, 40001);
    EXPECT_EQ(out.expandedLength, 40001);
    EXPECT_EQ(out.origins.front().kind, TapeOrigin::Kind::Reset);
    EXPECT_EQ(out.origins.back().kind, TapeOrigin::Kind::Raw);
}
}} // namespace opusoptima::verification
