#include <cstddef>
#include <span>
#include <vector>

#include <gtest/gtest.h>

#include "BinaryParser.hpp"
#include "UnitTestConstants.hpp"

using opusoptima::test::BufferBuilder;
using opusoptima::test::BuildMinimalSolution;

TEST(SolutionParseUnitTest, ParsesMinimalSolution) {
    const auto buffer = BuildMinimalSolution();
    const auto sol = opusoptima::io::BinaryParser::ParseSolution(
        std::span<const std::byte>(buffer.data(), buffer.size()));
    EXPECT_EQ(sol.puzzleName, "test");
    EXPECT_EQ(sol.solutionName, "test");
    EXPECT_GT(sol.metrics.cycles, 0);
    EXPECT_TRUE(sol.objects.empty());
}

TEST(SolutionParseUnitTest, PreservesPuzzleName) {
    const auto buffer = BuildMinimalSolution("easy-conduit");
    const auto sol = opusoptima::io::BinaryParser::ParseSolution(
        std::span<const std::byte>(buffer.data(), buffer.size()));
    EXPECT_EQ(sol.puzzleName, "easy-conduit");
}

TEST(SolutionParseUnitTest, ThrowsOnWrongFormatTag) {
    const auto buffer = BuildMinimalSolution();
    constexpr int kBadTag = 99;
    std::vector<std::byte> corrupted = buffer;
    corrupted[0] = static_cast<std::byte>(kBadTag);
    EXPECT_THROW(opusoptima::io::BinaryParser::ParseSolution(
                     std::span<const std::byte>(corrupted.data(), corrupted.size())),
                 opusoptima::io::ParseError);
}

TEST(SolutionParseUnitTest, ParsesArmInstructions) {
    const auto base = BuildMinimalSolution();
    std::vector<std::byte> buffer(base.begin(), base.end() - 4);
    constexpr int kInstrCount = 2;

    BufferBuilder obj;
    obj.PushI32(1);
    obj.PushVStr("arm1");
    obj.PushU8(0x01);
    obj.PushI32(2);
    obj.PushI32(3);
    obj.PushI32(1);
    obj.PushI32(2);
    obj.PushI32(0);
    obj.PushI32(kInstrCount);
    obj.PushI32(0);
    obj.PushU8('G');
    obj.PushI32(1);
    obj.PushU8('X');
    obj.PushI32(0);

    const auto& tail = obj.Buffer();
    buffer.insert(buffer.end(), tail.begin(), tail.end());
    const auto sol = opusoptima::io::BinaryParser::ParseSolution(
        std::span<const std::byte>(buffer.data(), buffer.size()));

    ASSERT_EQ(sol.objects.size(), 1U);
    EXPECT_EQ(sol.objects[0].typeName, "arm1");
    ASSERT_EQ(sol.objects[0].instructions.size(), static_cast<std::size_t>(kInstrCount));
    EXPECT_EQ(sol.objects[0].instructions[0].action, 'G');
    EXPECT_EQ(sol.objects[0].instructions[1].action, 'X');
}

TEST(SolutionParseUnitTest, HandlesZeroInstructionsArm) {
    const auto base = BuildMinimalSolution();
    std::vector<std::byte> buffer(base.begin(), base.end() - 4);
    BufferBuilder obj;
    obj.PushI32(1);
    obj.PushVStr("arm1");
    obj.PushU8(0x01);
    obj.PushI32(0);
    obj.PushI32(0);
    obj.PushI32(0);
    obj.PushI32(0);
    obj.PushI32(0);
    obj.PushI32(0);
    obj.PushI32(0);
    const auto& tail = obj.Buffer();
    buffer.insert(buffer.end(), tail.begin(), tail.end());
    const auto sol = opusoptima::io::BinaryParser::ParseSolution(
        std::span<const std::byte>(buffer.data(), buffer.size()));
    ASSERT_EQ(sol.objects.size(), 1U);
    EXPECT_TRUE(sol.objects[0].instructions.empty());
}

TEST(SolutionParseUnitTest, HandlesMultipleObjects) {
    const auto base = BuildMinimalSolution();
    std::vector<std::byte> buffer(base.begin(), base.end() - 4);
    constexpr int kInputHeaderI32s = 5;
    constexpr int kPipeSegCount = 1;
    constexpr int kObjectCount = 3;

    BufferBuilder obj;
    obj.PushI32(kObjectCount);

    // ── input object ──
    obj.PushVStr("input");
    obj.PushU8(0x01);
    obj.PushI32(1);
    obj.PushI32(1); // position (1, 1)
    for (int i = 0; i < kInputHeaderI32s; ++i)
        obj.PushI32(0); // 5-i32 header

    // ── pipe object ──
    obj.PushVStr("pipe");
    obj.PushU8(0x01);
    obj.PushI32(-1);
    obj.PushI32(0); // position (-1, 0)
    obj.PushI32(0);
    obj.PushI32(0); // header[0..1]
    obj.PushI32(0);
    obj.PushI32(0);                                 // header[2..3]
    obj.PushI32(0);                                 // header[4]
    obj.PushI32(opusoptima::test::kTestPipeMarker); // pipe marker
    obj.PushI32(kPipeSegCount);                     // segment count
    obj.PushI32(0);
    obj.PushI32(0); // 1 segment pair

    // ── arm1 object ──
    obj.PushVStr("arm1");
    obj.PushU8(0x01);
    obj.PushI32(2);
    obj.PushI32(0); // position (2, 0)
    obj.PushI32(0);
    obj.PushI32(0); // header[0..1]
    obj.PushI32(0);
    obj.PushI32(1); // header[2..3]  → instruction count = 1
    obj.PushI32(0); // header[4]
    obj.PushU8('G');
    obj.PushI32(opusoptima::test::kTestGrabParam); // 1 instruction

    const auto& tail = obj.Buffer();
    buffer.insert(buffer.end(), tail.begin(), tail.end());
    const auto sol = opusoptima::io::BinaryParser::ParseSolution(
        std::span<const std::byte>(buffer.data(), buffer.size()));

    ASSERT_EQ(sol.objects.size(), static_cast<std::size_t>(kObjectCount));
    EXPECT_EQ(sol.objects[0].typeName, "input");
    EXPECT_EQ(sol.objects[1].typeName, "pipe");
    EXPECT_EQ(sol.objects[2].typeName, "arm1");
}

TEST(SolutionParseUnitTest, StopsWhenObjectCountExceedsData) {
    const auto base = BuildMinimalSolution();
    std::vector<std::byte> buffer(base.begin(), base.end() - 4);
    constexpr int kClaimedObjects = 5;
    constexpr int kBodyFiller = 5;

    BufferBuilder obj;
    obj.PushI32(kClaimedObjects);
    obj.PushVStr("input");
    obj.PushU8(0x01);
    obj.PushI32(0);
    obj.PushI32(0);
    for (int i = 0; i < kBodyFiller; ++i)
        obj.PushI32(0);
    const auto& tail = obj.Buffer();
    buffer.insert(buffer.end(), tail.begin(), tail.end());
    const auto sol = opusoptima::io::BinaryParser::ParseSolution(
        std::span<const std::byte>(buffer.data(), buffer.size()));
    EXPECT_EQ(sol.objects.size(), 1U);
}
