// Contract tests for the `ProgramBuilder` port (C3). Upstream merges relative
// per-arm fragments and shifts a fragment only as far as its own arm requires,
// so independent arms overlap freely while one arm stays collision free.
#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <vector>

#include "ProgramBuilder.hpp"

using namespace opusoptima::solvers;

namespace {
ProgramFragment Fragment(const std::string& arm, std::vector<ProgramInstruction> instructions,
                         std::int64_t anchor = 0) {
    ProgramFragment fragment;
    fragment.arm = arm;
    fragment.instructions = std::move(instructions);
    fragment.anchor = anchor;
    return fragment;
}

std::vector<PlacedInstruction> Tape(const ProgramBuilder& builder, const std::string& arm) {
    return builder.Arm(arm);
}
}  // namespace

TEST(ProgramBuilder, DifferentArmsOverlapFreely) {
    ProgramBuilder builder;
    const auto first = builder.Add(Fragment("upper", {{'E', 0}, {'G', 1}, {'e', 2}}));
    const auto second = builder.Add(Fragment("intake", {{'E', 0}, {'G', 1}, {'e', 2}}));
    EXPECT_EQ(first, 0);
    EXPECT_EQ(second, 0) << "a different arm must not be pushed behind the upper arm";
    EXPECT_EQ(builder.Makespan(), 3);
    EXPECT_EQ(Tape(builder, "upper").size(), 3U);
    EXPECT_EQ(Tape(builder, "intake").size(), 3U);
}

TEST(ProgramBuilder, SameArmFragmentsAreShiftedToTheFirstFreeTick) {
    ProgramBuilder builder;
    builder.Add(Fragment("upper", {{'E', 0}, {'G', 1}, {'e', 2}, {'E', 3}}));
    const auto shifted = builder.Add(Fragment("upper", {{'E', 0}, {'G', 1}}));
    EXPECT_EQ(shifted, 4) << "the second fragment starts after the last occupied tick";
    EXPECT_TRUE(builder.OccupiedBy("upper", 4));
    EXPECT_TRUE(builder.OccupiedBy("upper", 5));
    EXPECT_FALSE(builder.OccupiedBy("upper", 6));
    EXPECT_EQ(builder.Makespan(), 6);
}

TEST(ProgramBuilder, SameArmFragmentsFillInteriorHoles) {
    ProgramBuilder builder;
    builder.Add(Fragment("feeder", {{'E', 0}, {'E', 4}}));
    // One-tick fragment: the smallest free tick is the hole at 1, not the tail.
    const auto shifted = builder.Add(Fragment("feeder", {{'R', 0}}));
    EXPECT_EQ(shifted, 1);
    EXPECT_TRUE(builder.OccupiedBy("feeder", 1));
    EXPECT_FALSE(builder.OccupiedBy("feeder", 2));
}

TEST(ProgramBuilder, AnchorIsAHardLowerBound) {
    ProgramBuilder builder;
    const auto start = builder.Add(Fragment("row", {{'E', 0}}, 7));
    EXPECT_EQ(start, 7);
    EXPECT_TRUE(builder.OccupiedBy("row", 7));
    EXPECT_EQ(builder.Makespan(), 8);
}

TEST(ProgramBuilder, NormalisesRelativeTicksAndKeepsOrder) {
    ProgramBuilder builder;
    // Relative ticks given unsorted and not starting at zero still merge in
    // order, and the returned value is the fragment's absolute start.
    const auto start = builder.Add(Fragment("conv", {{'e', 2}, {'E', 0}, {'G', 1}}));
    EXPECT_EQ(start, 0);
    const auto tape = Tape(builder, "conv");
    ASSERT_EQ(tape.size(), 3U);
    EXPECT_EQ(tape[0].action, 'E');
    EXPECT_EQ(tape[1].action, 'G');
    EXPECT_EQ(tape[2].action, 'e');
    EXPECT_EQ(tape[2].tick, 2);
}

TEST(ProgramBuilder, MergeIsDeterministic) {
    auto run = [] {
        ProgramBuilder builder;
        builder.Add(Fragment("a", {{'E', 0}, {'G', 1}}));
        builder.Add(Fragment("b", {{'E', 0}, {'G', 1}}));
        builder.Add(Fragment("a", {{'E', 0}, {'G', 1}}));
        builder.Add(Fragment("b", {{'R', 0}}));
        return builder;
    };
    const auto one = run();
    const auto two = run();
    EXPECT_EQ(one.Arm("a").size(), two.Arm("a").size());
    EXPECT_EQ(one.Arm("b").size(), two.Arm("b").size());
    EXPECT_EQ(one.Makespan(), two.Makespan());
    for (std::size_t i = 0; i < one.Arm("a").size(); ++i) {
        EXPECT_EQ(one.Arm("a")[i].tick, two.Arm("a")[i].tick);
        EXPECT_EQ(one.Arm("a")[i].action, two.Arm("a")[i].action);
    }
    EXPECT_EQ(one.Occupied(), two.Occupied());
}

TEST(ProgramBuilder, EmptyFragmentIsIgnoredButCounted) {
    ProgramBuilder builder;
    EXPECT_EQ(builder.Add(Fragment("upper", {}, 4)), 4);
    EXPECT_EQ(builder.FragmentCount(), 1U);
    EXPECT_EQ(builder.Makespan(), 0);
    EXPECT_TRUE(builder.Arm("upper").empty());
}

TEST(ProgramBuilder, MissingArmIsAnEmptyTape) {
    ProgramBuilder builder;
    EXPECT_TRUE(builder.Arm("never-used").empty());
    EXPECT_FALSE(builder.OccupiedBy("never-used", 0));
}
