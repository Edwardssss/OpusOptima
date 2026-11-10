// Bulk-parse functional test: scans the asset tree at runtime and verifies
// every .puzzle and .solution file parses without throwing an exception.

#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "BinaryParser.hpp"
#include "RepoRoot.hpp"

#define MAX_RETRY_TIME 5

namespace {

std::filesystem::path GetRepoRoot() {
    return opusoptima::test::RepoRoot(__FILE__);}

void CollectFiles(const std::filesystem::path& dir, const std::string& extension,
                  std::vector<std::filesystem::path>& out) {
    if (!std::filesystem::is_directory(dir)) return;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(dir)) {
        if (entry.is_regular_file() && entry.path().extension() == extension) {
            out.push_back(entry.path());
        }
    }
}

} // namespace

TEST(PuzzleBulkTest, AllPuzzleFilesParseWithoutException) {
    const auto repo = GetRepoRoot();
    const auto puzzleDir = repo / "asset" / "puzzle";
    std::vector<std::filesystem::path> puzzles;
    CollectFiles(puzzleDir, ".puzzle", puzzles);
    ASSERT_GT(puzzles.size(), 0U);

    int parsed = 0;
    for (const auto& path : puzzles) {
        const auto puzzle = opusoptima::io::BinaryParser::ParsePuzzleFile(path);
        EXPECT_FALSE(puzzle.name.empty()) << path;
        EXPECT_GT(puzzle.inputs.size() + puzzle.outputs.size(), 0U) << path;
        ++parsed;
    }
    EXPECT_EQ(parsed, static_cast<int>(puzzles.size()));
}

TEST(SolutionBulkTest, AllSolutionFilesParseWithoutException) {
    const auto repo = GetRepoRoot();
    const auto solutionDir = repo / "asset" / "solution";
    std::vector<std::filesystem::path> solutions;
    CollectFiles(solutionDir, ".solution", solutions);
    ASSERT_GT(solutions.size(), 0U);

    int parsed = 0;
    int failed = 0;
    for (const auto& path : solutions) {
        try {
            const auto sol = opusoptima::io::BinaryParser::ParseSolutionFile(path);
            EXPECT_FALSE(sol.puzzleName.empty()) << path;
            EXPECT_FALSE(sol.solutionName.empty()) << path;
            EXPECT_GT(sol.objects.size(), 0U) << path;
            ++parsed;
        } catch (const std::exception& e) {
            // Print details to stderr for CI debugging
            fprintf(stderr, "\nFAIL: %s\n  %s\n", path.string().c_str(), e.what());
            ADD_FAILURE() << path << ": " << e.what();
            ++failed;
            if (failed >= MAX_RETRY_TIME) break;
        }
    }
    EXPECT_EQ(failed, 0);
    EXPECT_EQ(parsed + failed, static_cast<int>(solutions.size()));
}
