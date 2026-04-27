#include <cstdio>
#include <filesystem>
#include <stdexcept>
#include <string>

#include <gtest/gtest.h>

#include "BinaryParser.hpp"

namespace {

std::filesystem::path GetRepoRoot() {
    std::filesystem::path cursor = std::filesystem::path(__FILE__).parent_path();
    while (!cursor.empty()) {
        if (std::filesystem::exists(cursor / "CMakeLists.txt")) {
            return cursor;
        }
        cursor = cursor.parent_path();
    }
    throw std::runtime_error("Cannot locate repository root from test file path");
}

} // namespace

// Solution parsing from file

TEST(SolutionReadTest, ParsesEasyConduitSolution) {
    const auto path =
        GetRepoRoot() / "asset/solution/easy" / "easy-conduit-easy-conduit-1.solution";
    const auto solution = opusoptima::io::BinaryParser::ParseSolutionFile(path);

    // Header
    EXPECT_EQ(solution.puzzleName, "easy-conduit");
    EXPECT_EQ(solution.solutionName, "NEW SOLUTION 1");

    // Metrics
    EXPECT_EQ(solution.metrics.cycles, 26);
    EXPECT_EQ(solution.metrics.cost, 40);
    EXPECT_EQ(solution.metrics.area, 29);
    EXPECT_EQ(solution.metrics.instructionCount, 8);

    // Objects
    ASSERT_EQ(solution.objects.size(), 6U);

    // Object 0: pipe at (-1, 0)
    EXPECT_EQ(solution.objects[0].typeName, "pipe");
    EXPECT_EQ(solution.objects[0].position.q, -1);
    EXPECT_EQ(solution.objects[0].position.r, 0);

    // Object 1: pipe at (1, 0)
    EXPECT_EQ(solution.objects[1].typeName, "pipe");
    EXPECT_EQ(solution.objects[1].position.q, 1);
    EXPECT_EQ(solution.objects[1].position.r, 0);

    // Object 2: out-std at (-2, 1)
    EXPECT_EQ(solution.objects[2].typeName, "out-std");
    EXPECT_EQ(solution.objects[2].position.q, -2);
    EXPECT_EQ(solution.objects[2].position.r, 1);

    // Object 3: input at (1, 1)
    EXPECT_EQ(solution.objects[3].typeName, "input");
    EXPECT_EQ(solution.objects[3].position.q, 1);
    EXPECT_EQ(solution.objects[3].position.r, 1);

    // Object 4: arm1 at (2, 0) with 3 instructions: Grab(1), Rotate(2), Drop(0)
    EXPECT_EQ(solution.objects[4].typeName, "arm1");
    EXPECT_EQ(solution.objects[4].position.q, 2);
    EXPECT_EQ(solution.objects[4].position.r, 0);
    ASSERT_EQ(solution.objects[4].instructions.size(), 3U);
    EXPECT_EQ(solution.objects[4].instructions[0].action, 'G');
    EXPECT_EQ(solution.objects[4].instructions[0].parameter, 1);
    EXPECT_EQ(solution.objects[4].instructions[1].action, 'r');
    EXPECT_EQ(solution.objects[4].instructions[1].parameter, 2);
    EXPECT_EQ(solution.objects[4].instructions[2].action, 'X');
    EXPECT_EQ(solution.objects[4].instructions[2].parameter, 0);

    // Object 5: arm1 at (-2, 0) with 3 instructions: Grab(4), rotate(5), Drop(1)
    EXPECT_EQ(solution.objects[5].typeName, "arm1");
    EXPECT_EQ(solution.objects[5].position.q, -2);
    EXPECT_EQ(solution.objects[5].position.r, 0);
    ASSERT_EQ(solution.objects[5].instructions.size(), 3U);
    EXPECT_EQ(solution.objects[5].instructions[0].action, 'G');
    EXPECT_EQ(solution.objects[5].instructions[0].parameter, 4);
    EXPECT_EQ(solution.objects[5].instructions[1].action, 'r');
    EXPECT_EQ(solution.objects[5].instructions[1].parameter, 5);
    EXPECT_EQ(solution.objects[5].instructions[2].action, 'X');
    EXPECT_EQ(solution.objects[5].instructions[2].parameter, 1);

    printf("Test solution path: %s\n", path.string().c_str());
}

TEST(SolutionReadTest, ParsesDoubleConduitSolution) {
    const auto path =
        GetRepoRoot() / "asset/solution/easy" / "double-conduit-double-conduit-1.solution";
    const auto solution = opusoptima::io::BinaryParser::ParseSolutionFile(path);

    EXPECT_EQ(solution.puzzleName, "double-conduit");
    EXPECT_EQ(solution.metrics.cycles, 26);
    EXPECT_EQ(solution.metrics.cost, 40);
    EXPECT_EQ(solution.metrics.area, 35);
    EXPECT_EQ(solution.metrics.instructionCount, 8);

    ASSERT_EQ(solution.objects.size(), 6U);

    EXPECT_EQ(solution.objects[2].typeName, "arm1");
    ASSERT_EQ(solution.objects[2].instructions.size(), 3U);
    EXPECT_EQ(solution.objects[2].instructions[0].action, 'G');
    EXPECT_EQ(solution.objects[2].instructions[0].parameter, 4);
    EXPECT_EQ(solution.objects[2].instructions[1].action, 'R');
    EXPECT_EQ(solution.objects[2].instructions[1].parameter, 5);
    EXPECT_EQ(solution.objects[2].instructions[2].action, 'X');
    EXPECT_EQ(solution.objects[2].instructions[2].parameter, 0);
}

TEST(SolutionReadTest, ParsesEasyConduitSolution2) {
    const auto path =
        GetRepoRoot() / "asset/solution/easy" / "easy-conduit-easy-conduit-2.solution";
    const auto solution = opusoptima::io::BinaryParser::ParseSolutionFile(path);

    // Header
    EXPECT_EQ(solution.puzzleName, "easy-conduit");
    EXPECT_EQ(solution.solutionName, "NEW SOLUTION 2");

    // Metrics
    EXPECT_EQ(solution.metrics.cycles, 72);
    EXPECT_EQ(solution.metrics.cost, 60);
    EXPECT_EQ(solution.metrics.area, 33);
    EXPECT_EQ(solution.metrics.instructionCount, 32);

    // The file header claims 8 objects.  With the self-describing binary
    // format parser all 8 objects can now be correctly parsed.
    ASSERT_EQ(solution.objects.size(), 8U);

    // Pipe at (-1, 0)
    EXPECT_EQ(solution.objects[0].typeName, "pipe");
    EXPECT_EQ(solution.objects[0].position.q, -1);

    // Pipe at (1, 0)
    EXPECT_EQ(solution.objects[1].typeName, "pipe");
    EXPECT_EQ(solution.objects[1].position.q, 1);

    // Input at (-3, 0)
    EXPECT_EQ(solution.objects[2].typeName, "input");
    EXPECT_EQ(solution.objects[2].position.q, -3);

    // Output at (2, 0)
    EXPECT_EQ(solution.objects[3].typeName, "out-std");
    EXPECT_EQ(solution.objects[3].position.q, 2);

    // First arm at (-2, 0) with 19 instructions
    EXPECT_EQ(solution.objects[4].typeName, "arm1");
    ASSERT_EQ(solution.objects[4].instructions.size(), 19U);

    // Bonder at (-3, 1)
    EXPECT_EQ(solution.objects[5].typeName, "bonder");

    // Unbonder at (-2, -1)
    EXPECT_EQ(solution.objects[6].typeName, "unbonder");

    // Second arm at (2, -1) with 4 instructions
    EXPECT_EQ(solution.objects[7].typeName, "arm1");
    ASSERT_EQ(solution.objects[7].instructions.size(), 4U);

    printf("Test solution path: %s\n", path.string().c_str());
}