#include <cstdio>
#include <filesystem>
#include <stdexcept>
#include <string>

#include <gtest/gtest.h>

#include "BinaryParser.hpp"
#include "BinaryWriter.hpp"
#include "RepoRoot.hpp"

namespace {

std::filesystem::path GetRepoRoot() {
    return opusoptima::test::RepoRoot(__FILE__);
}

} // namespace

TEST(SolutionReadTest, RealFixturesRoundTripWithoutByteChanges) {
    for (const auto* name :
         {"easy-conduit-easy-conduit-1.solution", "double-conduit-double-conduit-1.solution",
          "easy-conduit-easy-conduit-2.solution"}) {
        const auto bytes = opusoptima::io::BinaryParser::ReadAllBytes(GetRepoRoot() /
                                                                      "asset/solution/easy" / name);
        const auto parsed = opusoptima::io::BinaryParser::ParseSolution(bytes);
        EXPECT_EQ(opusoptima::io::BinaryWriter::WriteSolution(parsed), bytes) << name;
    }
}

TEST(SolutionReadTest, RejectsTruncationAtDeclaredObjectBoundaries) {
    using namespace opusoptima::io;
    SolutionData solution;
    solution.objects.push_back({"arm1", {}, {}, {1, 0, 0, 0, 0}});
    const auto oneObject = BinaryWriter::WriteSolution(solution);
    auto noObjects = solution;
    noObjects.objects.clear();
    const auto headerSize = BinaryWriter::WriteSolution(noObjects).size();
    EXPECT_THROW(
        BinaryParser::ParseSolution(std::span<const std::byte>(oneObject.data(), headerSize)),
        ParseError);
    solution.objects.push_back({"input", {}, {}, {1, 0, 0, 0, 0}});
    const auto twoObjects = BinaryWriter::WriteSolution(solution);
    EXPECT_THROW(BinaryParser::ParseSolution(
                     std::span<const std::byte>(twoObjects.data(), oneObject.size())),
                 ParseError);
}

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

    // Object 4: arm1 at (2, 0) with 3 instructions: Grab at 0, Rotate at 1, Drop at 2
    EXPECT_EQ(solution.objects[4].typeName, "arm1");
    EXPECT_EQ(solution.objects[4].position.q, 2);
    EXPECT_EQ(solution.objects[4].position.r, 0);
    ASSERT_EQ(solution.objects[4].instructions.size(), 3U);
    EXPECT_EQ(solution.objects[4].instructions[0].action, 'G');
    EXPECT_EQ(solution.objects[4].instructions[0].parameter, 0);
    EXPECT_EQ(solution.objects[4].instructions[1].action, 'r');
    EXPECT_EQ(solution.objects[4].instructions[1].parameter, 1);
    EXPECT_EQ(solution.objects[4].instructions[2].action, 'X');
    EXPECT_EQ(solution.objects[4].instructions[2].parameter, 2);

    // Object 5: arm1 at (-2, 0) with 3 instructions: Grab at 3, rotate at 4, Drop at 5
    EXPECT_EQ(solution.objects[5].typeName, "arm1");
    EXPECT_EQ(solution.objects[5].position.q, -2);
    EXPECT_EQ(solution.objects[5].position.r, 0);
    ASSERT_EQ(solution.objects[5].instructions.size(), 3U);
    EXPECT_EQ(solution.objects[5].instructions[0].action, 'G');
    EXPECT_EQ(solution.objects[5].instructions[0].parameter, 3);
    EXPECT_EQ(solution.objects[5].instructions[1].action, 'r');
    EXPECT_EQ(solution.objects[5].instructions[1].parameter, 4);
    EXPECT_EQ(solution.objects[5].instructions[2].action, 'X');
    EXPECT_EQ(solution.objects[5].instructions[2].parameter, 5);

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
    EXPECT_EQ(solution.objects[2].instructions[0].parameter, 3);
    EXPECT_EQ(solution.objects[2].instructions[1].action, 'R');
    EXPECT_EQ(solution.objects[2].instructions[1].parameter, 4);
    EXPECT_EQ(solution.objects[2].instructions[2].action, 'X');
    EXPECT_EQ(solution.objects[2].instructions[2].parameter, 5);
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