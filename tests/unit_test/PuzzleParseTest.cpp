#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <vector>

#include <gtest/gtest.h>

#include "BinaryParser.hpp"
#include "UnitTestConstants.hpp"

using opusoptima::test::BufferBuilder;
using opusoptima::test::BuildMinimalPuzzle;

TEST(PuzzleParseUnitTest, ParsesMinimalPuzzle) {
    const auto buffer = BuildMinimalPuzzle();
    const auto puzzle = opusoptima::io::BinaryParser::ParsePuzzle(
        std::span<const std::byte>(buffer.data(), buffer.size()));

    EXPECT_EQ(puzzle.version, 3);
    EXPECT_EQ(puzzle.name, "test");
    EXPECT_EQ(puzzle.creatorSteamId, 0U);
    EXPECT_EQ(puzzle.permissionBits, 0U);
    EXPECT_EQ(puzzle.outputMultiplier, 1);
    ASSERT_FALSE(puzzle.productionInfo.has_value());
    ASSERT_EQ(puzzle.inputs.size(), 1U);
    ASSERT_EQ(puzzle.outputs.size(), 1U);
}

TEST(PuzzleParseUnitTest, PreservesLongName) {
    const auto buffer = BuildMinimalPuzzle("JUST SALT");
    const auto puzzle = opusoptima::io::BinaryParser::ParsePuzzle(
        std::span<const std::byte>(buffer.data(), buffer.size()));
    EXPECT_EQ(puzzle.name, "JUST SALT");
}

TEST(PuzzleParseUnitTest, ThrowsOnWrongVersion) {
    const auto buffer = BuildMinimalPuzzle();
    constexpr int kBadVersion = 99;
    std::vector<std::byte> corrupted = buffer;
    corrupted[0] = static_cast<std::byte>(kBadVersion);
    EXPECT_THROW(opusoptima::io::BinaryParser::ParsePuzzle(
                     std::span<const std::byte>(corrupted.data(), corrupted.size())),
                 opusoptima::io::ParseError);
}

TEST(PuzzleParseUnitTest, ThrowsOnTrailingBytes) {
    auto buffer = BuildMinimalPuzzle();
    constexpr std::byte kTrailingByte{0xFF};
    buffer.push_back(kTrailingByte);
    EXPECT_THROW(opusoptima::io::BinaryParser::ParsePuzzle(
                     std::span<const std::byte>(buffer.data(), buffer.size())),
                 opusoptima::io::ParseError);
}

TEST(PuzzleParseUnitTest, HandlesProductionPuzzle) {
    const auto base = BuildMinimalPuzzle();
    std::vector<std::byte> buffer(base.begin(), base.end() - 1);
    BufferBuilder prod;
    prod.PushU8(1);  // hasProduction = true
    prod.PushU8(0);  // shrinkLeft = false
    prod.PushU8(0);  // shrinkRight = false
    prod.PushU8(0);  // isolation = false
    prod.PushI32(0); // chamber count
    prod.PushI32(0); // conduit count
    prod.PushI32(0); // vial count
    const auto& tail = prod.Buffer();
    buffer.insert(buffer.end(), tail.begin(), tail.end());
    const auto puzzle = opusoptima::io::BinaryParser::ParsePuzzle(
        std::span<const std::byte>(buffer.data(), buffer.size()));
    ASSERT_TRUE(puzzle.productionInfo.has_value());
    EXPECT_FALSE(puzzle.productionInfo->shrinkLeft);
}

namespace {

BufferBuilder PuzzleListPrefix(const int list) {
    BufferBuilder buffer;
    buffer.PushI32(3);
    buffer.PushVStr("");
    buffer.PushU64(0);
    buffer.PushU64(0);
    if (list == 0) return buffer;
    buffer.PushI32(list == 1 || list == 2 ? 1 : 0);
    if (list == 1) return buffer;
    if (list == 2) {
        buffer.PushI32(0);
        return buffer;
    }
    if (list == 3) return buffer;
    buffer.PushI32(0);
    buffer.PushI32(1);
    buffer.PushU8(1);
    buffer.PushU8(0);
    buffer.PushU8(0);
    buffer.PushU8(0);
    if (list == 4) return buffer;
    buffer.PushI32(0);
    if (list == 5) return buffer;
    buffer.PushI32(list == 6 ? 1 : 0);
    if (list == 6) {
        for (int i = 0; i < 4; ++i) buffer.PushU8(0);
    }
    return buffer;
}

} // namespace

TEST(PuzzleParseUnitTest, RejectsImpossibleListCountsBeforeAllocation) {
    for (int list = 0; list < 8; ++list) {
        for (const auto count : {-1, std::numeric_limits<std::int32_t>::max()}) {
            SCOPED_TRACE(list);
            SCOPED_TRACE(count);
            auto buffer = PuzzleListPrefix(list);
            buffer.PushI32(count);
            for (int i = 0; i < 16; ++i) buffer.PushU8(0);
            EXPECT_THROW(opusoptima::io::BinaryParser::ParsePuzzle(buffer.Span()),
                         opusoptima::io::ParseError);
        }
    }
}

TEST(PuzzleParseUnitTest, RejectsTruncatedListEntries) {
    for (int list = 0; list < 8; ++list) {
        SCOPED_TRACE(list);
        auto buffer = PuzzleListPrefix(list);
        // The enclosing molecule needs eight bytes before it can be entered.
        buffer.PushI32(list == 1 ? 2 : 1);
        if (list == 1) buffer.PushI32(0);
        EXPECT_THROW(opusoptima::io::BinaryParser::ParsePuzzle(buffer.Span()),
                     opusoptima::io::ParseError);
    }
}

TEST(PuzzleParseUnitTest, AcceptsMinimumSizedListEntries) {
    auto buffer = PuzzleListPrefix(0);
    buffer.PushI32(1);
    buffer.PushI32(0);
    buffer.PushI32(0);
    buffer.PushI32(1);
    buffer.PushI32(1);
    buffer.PushU8(1);
    buffer.PushU8(0);
    buffer.PushU8(0);
    buffer.PushI32(1);
    for (int i = 0; i < 5; ++i) buffer.PushU8(0);
    buffer.PushI32(1);
    buffer.PushU8(1);
    for (int i = 0; i < 3; ++i) buffer.PushU8(0);
    buffer.PushI32(1);
    for (int i = 0; i < 3; ++i) buffer.PushU8(0);
    buffer.PushI32(1);
    for (int i = 0; i < 4; ++i) buffer.PushU8(0);
    buffer.PushI32(1);
    buffer.PushU8(0);
    buffer.PushU8(0);
    buffer.PushI32(1);
    for (int i = 0; i < 3; ++i) buffer.PushU8(0);
    buffer.PushI32(0);

    const auto puzzle = opusoptima::io::BinaryParser::ParsePuzzle(buffer.Span());
    ASSERT_EQ(puzzle.inputs.size(), 1U);
    EXPECT_TRUE(puzzle.inputs.front().atoms.empty());
    ASSERT_EQ(puzzle.outputs.size(), 1U);
    EXPECT_EQ(puzzle.outputs.front().atoms.size(), 1U);
    EXPECT_EQ(puzzle.outputs.front().bonds.size(), 1U);
    ASSERT_TRUE(puzzle.productionInfo.has_value());
    EXPECT_EQ(puzzle.productionInfo->chambers.size(), 1U);
    ASSERT_EQ(puzzle.productionInfo->conduits.size(), 1U);
    EXPECT_EQ(puzzle.productionInfo->conduits.front().path.size(), 1U);
    EXPECT_EQ(puzzle.productionInfo->vials.size(), 1U);
}

TEST(PuzzleParseUnitTest, StringRoundTrip) {
    const auto buffer = BuildMinimalPuzzle("Hello World");
    const auto puzzle = opusoptima::io::BinaryParser::ParsePuzzle(
        std::span<const std::byte>(buffer.data(), buffer.size()));
    EXPECT_EQ(puzzle.name, "Hello World");
}
