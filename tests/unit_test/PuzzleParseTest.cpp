#include <cstddef>
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

TEST(PuzzleParseUnitTest, StringRoundTrip) {
    const auto buffer = BuildMinimalPuzzle("Hello World");
    const auto puzzle = opusoptima::io::BinaryParser::ParsePuzzle(
        std::span<const std::byte>(buffer.data(), buffer.size()));
    EXPECT_EQ(puzzle.name, "Hello World");
}
