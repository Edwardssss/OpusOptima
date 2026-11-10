// Contract tests for the intake/feeder companion pairing of the scheduled row
// family. The local intake machine and the shared serpentine feeder machine are
// different tape sets, so the portfolio must be able to compare them slot by
// slot; these tests pin that the candidate builder exposes the companion, keeps
// it identical to the forced feeder machine, and stays deterministic.
#include <gtest/gtest.h>

#include <chrono>
#include <cstdlib>
#include <optional>
#include <string>

#include "AssemblySolver.hpp"
#include "BinaryParser.hpp"
#include "BinaryWriter.hpp"
#include "RowAssembly.hpp"

using namespace opusoptima;
using namespace opusoptima::solvers;

namespace {
struct ScopedDirectInput {
    const char* name = "OPUSOPTIMA_DIRECT_INPUT";
    std::optional<std::string> saved;
    explicit ScopedDirectInput(std::optional<std::string> value)
        : saved(std::getenv(name) ? std::optional<std::string>(std::getenv(name)) : std::nullopt) {
        if (value)
            setenv(name, value->c_str(), 1);
        else
            unsetenv(name);
    }
    ~ScopedDirectInput() {
        if (saved)
            setenv(name, saved->c_str(), 1);
        else
            unsetenv(name);
    }
};
} // namespace

TEST(ScheduledRowCompanion, IntakeCandidatesExposeTheFeederCompanion) {
    const auto puzzle = io::BinaryParser::ParsePuzzleFile("asset/puzzle/easy/just-salt.puzzle");
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    const auto plan = AssemblyPlanner::Plan(puzzle, false, deadline);
    ScopedDirectInput intake(std::nullopt);

    const auto candidates = CompileScheduledRowCandidates(puzzle, plan, deadline);
    ASSERT_TRUE(candidates.compactUsesIntake)
        << "the intake is the default machine on a plain single-input row";
    ASSERT_TRUE(candidates.compactFeeder.has_value())
        << "a small row must expose the shared-feeder companion";
    ASSERT_TRUE(candidates.clockedFeeder.has_value())
        << "a small row must expose the clocked shared-feeder companion";

    // The companion is a different machine than the intake one: that is the
    // whole point of submitting both, and the portfolio compares them slot by
    // slot instead of relying on one machine's numbers.
    EXPECT_FALSE(candidates.compactFeeder->objects.empty());
    EXPECT_NE(io::BinaryWriter::WriteSolution(*candidates.compactFeeder),
              io::BinaryWriter::WriteSolution(candidates.compact));
    EXPECT_NE(io::BinaryWriter::WriteSolution(*candidates.clockedFeeder),
              io::BinaryWriter::WriteSolution(candidates.clocked));

    // Rebuilding the ladder is deterministic, so a rejected candidate always
    // falls back to the same companion machine.
    const auto rebuilt = CompileScheduledRowCandidates(puzzle, plan, deadline);
    ASSERT_TRUE(rebuilt.compactFeeder.has_value());
    ASSERT_TRUE(rebuilt.clockedFeeder.has_value());
    EXPECT_EQ(io::BinaryWriter::WriteSolution(*rebuilt.compactFeeder),
              io::BinaryWriter::WriteSolution(*candidates.compactFeeder));
    EXPECT_EQ(io::BinaryWriter::WriteSolution(*rebuilt.clockedFeeder),
              io::BinaryWriter::WriteSolution(*candidates.clockedFeeder));
}

TEST(ScheduledRowCompanion, FeederModeHasNoCompanion) {
    const auto puzzle = io::BinaryParser::ParsePuzzleFile("asset/puzzle/easy/just-salt.puzzle");
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    const auto plan = AssemblyPlanner::Plan(puzzle, false, deadline);
    ScopedDirectInput feeder(std::string("off"));

    const auto candidates = CompileScheduledRowCandidates(puzzle, plan, deadline);
    EXPECT_FALSE(candidates.compactUsesIntake);
    EXPECT_FALSE(candidates.clockedUsesIntake);
    EXPECT_FALSE(candidates.compactFeeder.has_value());
    EXPECT_FALSE(candidates.clockedFeeder.has_value());
    // Without the intake the gap2 variant keeps its place in the ladder.
    EXPECT_TRUE(candidates.compactGap0.has_value());
}
