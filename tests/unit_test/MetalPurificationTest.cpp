#include "AssemblySolver.hpp"
#include "IO/BinaryWriter.hpp"
#include "IO/SolutionVerifier.hpp"
#include "RepoRoot.hpp"
#include <filesystem>
#include <gtest/gtest.h>

namespace opusoptima::solvers {
io::SolutionData CompileMetalPurification(const io::PuzzleData&);
namespace {
TEST(MetalPurificationTest, RejectsMissingPermissionAndSource) {
    const auto root = opusoptima::test::RepoRoot(__FILE__);
    auto p = io::BinaryParser::ParsePuzzleFile(root / "asset/puzzle/campaign/ch4/P036.puzzle");
    p.permissionBits &= ~(1ULL << 15);
    EXPECT_THROW(CompileMetalPurification(p), std::invalid_argument);
    p.permissionBits |= 1ULL << 15;
    p.inputs.clear();
    EXPECT_THROW(CompileMetalPurification(p), std::invalid_argument);
}
TEST(MetalPurificationTest, RejectsMissingMotionPermissionsAndInvalidMultiplier) {
    const auto root = opusoptima::test::RepoRoot(__FILE__);
    const auto original =
        io::BinaryParser::ParsePuzzleFile(root / "asset/puzzle/campaign/ch4/P036.puzzle");
    for (const int bit : {0, 2, 3, 22, 23}) {
        auto p = original;
        p.permissionBits &= ~(1ULL << bit);
        EXPECT_THROW(CompileMetalPurification(p), std::invalid_argument) << bit;
    }
    auto p = original;
    p.outputMultiplier = 0;
    EXPECT_THROW(CompileMetalPurification(p), std::invalid_argument);
}
TEST(MetalPurificationTest, ChoosesClosestMetalAndKeepsSingleStageWithoutTrack) {
    const auto root = opusoptima::test::RepoRoot(__FILE__);
    auto p = io::BinaryParser::ParsePuzzleFile(root / "asset/puzzle/campaign/ch4/P036.puzzle");
    auto silver = p.inputs[0];
    silver.atoms[0].type = core::AtomType::Silver;
    const auto silverIndex = static_cast<int>(p.inputs.size());
    p.inputs.push_back(silver);
    p.permissionBits &= ~((1ULL << 2) | (1ULL << 3));
    const auto solution = CompileMetalPurification(p);
    int inputs = 0;
    for (const auto& object : solution.objects) {
        EXPECT_NE(object.typeName, "track");
        EXPECT_NE(object.typeName, "piston");
        if (object.typeName == "input") {
            EXPECT_EQ(object.bodyData.at(2), silverIndex);
            ++inputs;
        }
    }
    EXPECT_EQ(inputs, 2);
}
#ifdef HAS_LIBVERIFY
TEST(MetalPurificationTest, ExistingSingleAndFiveStagePuzzlesVerifyTwice) {
    const auto root = opusoptima::test::RepoRoot(__FILE__);
    const auto path = root / "build/metal-purification-test.solution";
    struct Cleanup {
        std::filesystem::path path;
        ~Cleanup() {
            std::error_code ec;
            std::filesystem::remove(path, ec);
        }
    } cleanup{path};
    for (const auto* file : {"asset/puzzle/edge-cases/c988640836512960.puzzle",
                             "asset/puzzle/campaign/ch4/P036.puzzle"}) {
        SCOPED_TRACE(file);
        const auto puzzlePath = root / file;
        const auto p = io::BinaryParser::ParsePuzzleFile(puzzlePath);
        auto solution = CompileMetalPurification(p);
        io::BinaryWriter::WriteSolutionFile(path, solution);
        auto report = io::VerifySolutionFiles(puzzlePath, path, 150000, 10000);
        ASSERT_TRUE(report.Passed()) << report.detail << " cycle=" << report.errorCycle;
        solution.metrics = report.metrics;
        io::BinaryWriter::WriteSolutionFile(path, solution);
        EXPECT_TRUE(io::VerifySolutionFiles(puzzlePath, path, 150000, 10000).Passed());
    }
}
#endif
} // namespace
} // namespace opusoptima::solvers
