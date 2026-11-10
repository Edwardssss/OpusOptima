#include <cstddef>
#include <limits>
#include <span>
#include <vector>

#include <gtest/gtest.h>

#include "BinaryParser.hpp"
#include "BinaryWriter.hpp"
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

TEST(SolutionParseUnitTest, PreservesSignedInt32TapePositionsThroughCodec) {
    const std::vector<opusoptima::io::ArmInstruction> instructions = {
        {'G', std::numeric_limits<std::int32_t>::min()}, {'r', -20001},
        {'g', -20000}, {'R', 20000}, {'O', 20001},
        {'B', std::numeric_limits<std::int32_t>::max()}};
    opusoptima::io::SolutionData solution;
    solution.objects = {{"arm1", {}, instructions, {1, 0, 0, 0, 0}}};
    const auto bytes = opusoptima::io::BinaryWriter::WriteSolution(solution);
    const auto parsed = opusoptima::io::BinaryParser::ParseSolution(bytes);
    ASSERT_EQ(parsed.objects.size(), 1U);
    ASSERT_EQ(parsed.objects[0].instructions.size(), instructions.size());
    for (std::size_t index = 0; index < instructions.size(); ++index) {
        EXPECT_EQ(parsed.objects[0].instructions[index].parameter, instructions[index].parameter);
        EXPECT_EQ(parsed.objects[0].instructions[index].action, instructions[index].action);
    }
    EXPECT_EQ(opusoptima::io::BinaryWriter::WriteSolution(parsed), bytes);
}

TEST(SolutionParseUnitTest, RejectsInstructionCountsAndByteTruncationAtTheActualRead) {
    const auto parseError = [](int count, int payloadBytes, const char* expected) {
        SCOPED_TRACE(count);
        SCOPED_TRACE(payloadBytes);
        BufferBuilder builder;
        builder.Buffer() = BuildMinimalSolution();
        builder.Buffer().resize(builder.Buffer().size() - 4);
        builder.PushI32(1);
        builder.PushVStr("arm1");
        builder.PushU8(1);
        for (int field = 0; field < 5; ++field) builder.PushI32(0);
        builder.PushI32(count);
        for (int byte = 0; byte < payloadBytes; ++byte) builder.PushU8(0);
        try {
            static_cast<void>(opusoptima::io::BinaryParser::ParseSolution(builder.Span()));
            FAIL() << "Expected instruction structure rejection";
        } catch (const opusoptima::io::ParseError& error) {
            EXPECT_STREQ(error.what(), expected);
        }
    };
    parseError(-1, 4, "Negative instruction count");
    parseError(std::numeric_limits<std::int32_t>::max(), 4, "Invalid instruction count");
    for (int bytes = 0; bytes < 5; ++bytes)
        parseError(1, bytes, "Invalid instruction count");
    // A full entry passes the count gate; the separate i32 footer is still mandatory.
    for (int bytes = 5; bytes < 9; ++bytes)
        parseError(1, bytes, "Unexpected EOF while reading i32");
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

TEST(SolutionParseUnitTest, RejectsInvalidCountsBeforeReadingEntries) {
    for (const int count : {-1, 1, std::numeric_limits<int>::max()}) {
        for (const bool metrics : {false, true}) {
            SCOPED_TRACE(count);
            SCOPED_TRACE(metrics);
            BufferBuilder builder;
            builder.PushI32(7);
            builder.PushVStr("");
            builder.PushVStr("");
            if (!metrics) builder.PushI32(0);
            builder.PushI32(count);
            try {
                static_cast<void>(opusoptima::io::BinaryParser::ParseSolution(builder.Span()));
                FAIL() << "Expected invalid count to be rejected";
            } catch (const opusoptima::io::ParseError& error) {
                EXPECT_STREQ(error.what(), metrics ? "Invalid metrics count" : "Invalid object count");
            }
        }
    }
}

TEST(SolutionParseUnitTest, AcceptsMinimumSizeObjectsAndZeroMetrics) {
    for (const int count : {0, 1, 2}) {
        BufferBuilder builder;
        builder.PushI32(7);
        builder.PushVStr("");
        builder.PushVStr("");
        builder.PushI32(0);
        builder.PushI32(count);
        for (int i = 0; i < count; ++i) {
            builder.PushVStr("");
            builder.PushU8(1);
            for (int field = 0; field < 7; ++field) builder.PushI32(0);
        }
        const auto solution = opusoptima::io::BinaryParser::ParseSolution(builder.Span());
        EXPECT_EQ(solution.objects.size(), static_cast<std::size_t>(count));
    }
}

TEST(SolutionParseUnitTest, RejectsWhenObjectCountExceedsData) {
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
    EXPECT_THROW(opusoptima::io::BinaryParser::ParseSolution(
                     std::span<const std::byte>(buffer.data(), buffer.size())),
                 opusoptima::io::ParseError);
}
