#include "IO/BinaryParser.hpp"
#include "IO/BinaryWriter.hpp"
#include "IO/SolutionVerifier.hpp"
#include "AssemblySolver.hpp"
#include <filesystem>
#include <gtest/gtest.h>
#include "RepoRoot.hpp"

namespace opusoptima::solvers {
io::SolutionData CompileDispersion(const io::PuzzleData&);
#ifdef HAS_LIBVERIFY
TEST(DispersionTest, ExistingJournalProductsVerifyTwice) {
    const auto root = opusoptima::test::RepoRoot(__FILE__);
    const auto puzzlePath = root / "asset/puzzle/journal/journal4/P070.puzzle";
    const auto original = io::BinaryParser::ParsePuzzleFile(puzzlePath);
    auto solution = io::SolutionData{};
    solution.puzzleName = original.name;
    int offset = 0, arm = 0;
    for (std::size_t output = 0; output < original.outputs.size(); ++output) {
        auto puzzle = original;
        puzzle.outputs = {original.outputs[output]};
        auto machine = CompileDispersion(puzzle);
        for (auto object : machine.objects) {
            object.position.q += offset;
            if (object.typeName == "out-std") object.bodyData.at(2) = output;
            if (object.typeName == "piston" || object.typeName.starts_with("arm"))
                object.bodyData.at(4) = arm++;
            solution.objects.push_back(object);
        }
        offset += 160;
    }
    const auto path = std::filesystem::temp_directory_path() / "opus-dispersion-test.solution";
    struct Cleanup {
        std::filesystem::path p;
        ~Cleanup() {
            std::filesystem::remove(p);
        }
    } cleanup{path};
    io::BinaryWriter::WriteSolutionFile(path, solution);
    const auto report = io::VerifySolutionFiles(puzzlePath, path, 150000, 10000);
    ASSERT_TRUE(report.Passed()) << report.detail << " cycle=" << report.errorCycle;
    solution.metrics = report.metrics;
    io::BinaryWriter::WriteSolutionFile(path, solution);
    EXPECT_TRUE(io::VerifySolutionFiles(puzzlePath, path, 150000, 10000).Passed());
}
#endif
} // namespace opusoptima::solvers
