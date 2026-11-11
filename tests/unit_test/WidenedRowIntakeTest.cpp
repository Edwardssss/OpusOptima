// Contract tests for the widened local intake of the finite-product row.
//
// The narrow row feeds atoms with a shared serpentine delay line: every slot's
// pickup time is encoded as rail cells, so the rail is both expensive (five per
// cell) and slow (a full feeder cycle per separation). The local intake replaces
// it with one piston on a short rail above the sources, and widening it past the
// compact layout is 10-25% cheaper and three times faster on the near-parity row
// puzzles. Widening also parks the serpentine off-board, so it is only ever
// compiled as a *sibling* candidate: these tests pin that the widened machine is a
// different, valid machine, that leaving the scope restores the plain one exactly,
// and that the widened machine passes the native gate.
#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "BinaryParser.hpp"
#include "BinaryWriter.hpp"
#include "LifeDeath.hpp"
#include "RepoRoot.hpp"
#include "RowAssembly.hpp"

namespace opusoptima::solvers {
namespace {
constexpr const char* kRowPuzzle = "asset/puzzle/journal/journal4/P070.puzzle";

// Byte identity of two compiled machines, ignoring provenance strings and metrics.
bool SameMachine(const io::SolutionData& left, const io::SolutionData& right) {
    if (left.objects.size() != right.objects.size()) return false;
    for (std::size_t i = 0; i < left.objects.size(); ++i) {
        const auto& a = left.objects[i];
        const auto& b = right.objects[i];
        if (a.typeName != b.typeName || a.position != b.position || a.bodyData != b.bodyData)
            return false;
        if (a.instructions.size() != b.instructions.size()) return false;
        for (std::size_t k = 0; k < a.instructions.size(); ++k)
            if (a.instructions[k].action != b.instructions[k].action ||
                a.instructions[k].parameter != b.instructions[k].parameter)
                return false;
    }
    return true;
}

int TrackCells(const io::SolutionData& machine) {
    int total = 0;
    for (const auto& object : machine.objects)
        if (object.typeName == "track" && object.bodyData.size() > 4) total += object.bodyData[4];
    return total;
}

} // namespace

// The widened intake replaces the serpentine with a short source rail, so the
// machine it emits must be a genuinely different one -- and the plain compile must
// come back byte for byte once the scope ends, or the portfolio sibling would leak
// into every later candidate.
TEST(WidenedRowIntakeTest, ScopeEmitsADifferentMachineAndRestoresThePlainOne) {
    const auto root = opusoptima::test::RepoRoot(__FILE__);
    const auto puzzle = io::BinaryParser::ParsePuzzleFile(root / kRowPuzzle);
    const auto plain = CompileLifeDeath(puzzle, false);
    const auto widened = CompileLifeDeathLocalIntake(puzzle);
    const auto restored = CompileLifeDeath(puzzle, false);
    EXPECT_TRUE(SameMachine(plain, restored));
    EXPECT_FALSE(SameMachine(plain, widened));
    // The serpentine is parked, so the widened machine carries less track.
    EXPECT_LT(TrackCells(widened), TrackCells(plain));
}

// Nested scopes restore in reverse order, and an escaping exception must not leave
// the thread-wide override behind.
TEST(WidenedRowIntakeTest, ScopeRestoresAfterNestingAndOnException) {
    const auto root = opusoptima::test::RepoRoot(__FILE__);
    const auto puzzle = io::BinaryParser::ParsePuzzleFile(root / kRowPuzzle);
    const auto plain = CompileLifeDeath(puzzle, false);
    const bool plainApplied = ScheduledRowIntakeApplied();
    {
        const RowIntakeModeScope widened(2);
        const RowIntakeModeScope feeder(-1);
    }
    try {
        const RowIntakeModeScope widened(2);
        throw std::runtime_error("unwinds through the scope");
    } catch (const std::runtime_error&) {
    }
    const auto restored = CompileLifeDeath(puzzle, false);
    EXPECT_TRUE(SameMachine(plain, restored));
    EXPECT_EQ(ScheduledRowIntakeApplied(), plainApplied);
}

#ifdef HAS_LIBVERIFY
// The widened machine has a different period and a parked serpentine, so only the
// native gate can confirm it is a real machine.
TEST(WidenedRowIntakeTest, WidenedIntakeVerifiesNatively) {
    const auto root = opusoptima::test::RepoRoot(__FILE__);
    const auto puzzlePath = root / kRowPuzzle;
    const auto solutionPath =
        std::filesystem::current_path() / "widened-row-intake.solution";
    struct Cleanup {
        std::filesystem::path path;
        ~Cleanup() {
            std::error_code ec;
            std::filesystem::remove(path, ec);
        }
    } cleanup{solutionPath};
    const auto puzzle = io::BinaryParser::ParsePuzzleFile(puzzlePath);
    auto solution = CompileLifeDeathLocalIntake(puzzle);
    io::BinaryWriter::WriteSolutionFile(solutionPath, solution);
    const auto report = io::VerifySolutionFiles(puzzlePath, solutionPath, 1000000, 10000);
    ASSERT_TRUE(report.Passed()) << report.detail << " cycle=" << report.errorCycle << " at="
                                 << report.errorU << "," << report.errorV;
    solution.metrics = report.metrics;
    EXPECT_GT(solution.metrics.cost, 0);
    EXPECT_GT(solution.metrics.cycles, 0);
    // The shipped pairing: the plain machine is the cheaper fallback whenever the
    // widened one is rejected, so both must be valid.
    auto plain = CompileLifeDeath(puzzle, false);
    io::BinaryWriter::WriteSolutionFile(solutionPath, plain);
    EXPECT_TRUE(io::VerifySolutionFiles(puzzlePath, solutionPath, 1000000, 10000).Passed());
}
#endif

} // namespace opusoptima::solvers
