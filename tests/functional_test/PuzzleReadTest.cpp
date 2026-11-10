#include <cstdio>
#include <filesystem>
#include <stdexcept>
#include <string>

#include <gtest/gtest.h>

#include "AsciiRenderer.hpp"
#include "BinaryParser.hpp"
#include "PuzzleSpecs.hpp"
#include "SvgExporter.hpp"
#include "RepoRoot.hpp"

namespace {

std::filesystem::path GetRepoRoot() {
    return opusoptima::test::RepoRoot(__FILE__);}

} // namespace

// Puzzle parsing from file

TEST(PuzzleReadTest, ParsesJustSaltFromFile) {
    const auto path = GetRepoRoot() / "asset/puzzle/easy" / "just-salt.puzzle";
    const auto puzzle = opusoptima::io::BinaryParser::ParsePuzzleFile(path);

    EXPECT_EQ(puzzle.version, 3);
    EXPECT_EQ(puzzle.name, "JUST SALT");
    EXPECT_EQ(puzzle.outputMultiplier, 1);
    ASSERT_FALSE(puzzle.productionInfo.has_value());

    ASSERT_EQ(puzzle.inputs.size(), 1U);
    ASSERT_EQ(puzzle.outputs.size(), 1U);
    ASSERT_EQ(puzzle.inputs[0].atoms.size(), 1U);
    ASSERT_EQ(puzzle.outputs[0].atoms.size(), 1U);

    EXPECT_EQ(puzzle.inputs[0].atoms[0].type, opusoptima::core::AtomType::Salt);
    EXPECT_EQ(puzzle.outputs[0].atoms[0].type, opusoptima::core::AtomType::Salt);
    EXPECT_EQ(puzzle.inputs[0].atoms[0].position.q, 0);
    EXPECT_EQ(puzzle.inputs[0].atoms[0].position.r, 0);

    printf("Test puzzle path: %s\n", path.string().c_str());
}

// Render previews from parsed puzzle

TEST(PuzzleReadTest, RendersAsciiAndSvgFromFile) {
    const auto path = GetRepoRoot() / "asset/puzzle/easy" / "just-salt.puzzle";
    const auto puzzle = opusoptima::io::BinaryParser::ParsePuzzleFile(path);

    const std::string summary = opusoptima::io::SummarizePuzzle(puzzle);
    EXPECT_FALSE(summary.empty());
    EXPECT_NE(summary.find("JUST SALT"), std::string::npos);

    const std::string ascii = opusoptima::render::AsciiRenderer::RenderPuzzle(puzzle);
    EXPECT_FALSE(ascii.empty());

    const auto outputDir = GetRepoRoot() / "build/test-artifacts";
    const auto svgPath = outputDir / "just-salt-from-test.svg";
    std::filesystem::create_directories(outputDir);
    opusoptima::render::SvgExporter::WritePuzzleSvg(puzzle, svgPath);

    ASSERT_TRUE(std::filesystem::exists(svgPath));
    EXPECT_GT(std::filesystem::file_size(svgPath), 0U);
}
