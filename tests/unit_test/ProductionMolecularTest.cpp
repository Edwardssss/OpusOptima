#include "ProductionMolecular.hpp"
#include "AssemblySolver.hpp"
#include "BinaryWriter.hpp"
#include "RepoRoot.hpp"
#include "SolutionVerifier.hpp"
#include "UnitTestConstants.hpp"
#include <algorithm>
#include <filesystem>
#include <gtest/gtest.h>

namespace {
using namespace opusoptima;
std::filesystem::path MolecularRoot() {
    return opusoptima::test::RepoRoot(__FILE__);
}
io::PuzzleData MolecularPuzzle() {
    return io::BinaryParser::ParsePuzzleFile(MolecularRoot() /
                                             "asset/puzzle/edge-cases/c544897394273089.puzzle");
}
} // namespace
TEST(ProductionMolecular, StructuralBoundsAndNoSingletonShortcut) {
    auto p = MolecularPuzzle();
    auto solution = solvers::CompileProductionMolecular(p);
    EXPECT_EQ(std::count_if(solution.objects.begin(), solution.objects.end(),
                            [](auto& o) { return o.typeName == "pipe"; }),
              2);
    for (auto& o : solution.objects) {
        if (o.typeName == "input") {
            EXPECT_EQ(o.bodyData[2], 0);
        }
    }
    auto reject = [&](auto change) {
        auto q = p;
        change(q);
        EXPECT_THROW((void)solvers::CompileProductionMolecular(q), std::invalid_argument);
    };
    reject([](auto& q) { q.productionInfo->chambers[0].chamberType = "Unknown"; });
    reject([](auto& q) { q.productionInfo->conduits[0].path.pop_back(); });
    reject([](auto& q) {
        q.productionInfo->conduits[0].positionB = q.productionInfo->conduits[0].positionA;
    });
    reject([](auto& q) { q.inputs[0].bonds[1] = q.inputs[0].bonds[0]; });
    reject([](auto& q) { q.outputs[0].atoms[0].type = core::AtomType::Fire; });
    for (int bit : {0, 8, 9, 17, 22, 23, 25})
        reject([&](auto& q) { q.permissionBits &= ~(1ULL << bit); });
    for (auto& bond : p.inputs[0].bonds)
        std::swap(bond.from, bond.to);
    EXPECT_NO_THROW((void)solvers::CompileProductionMolecular(p));
}
#ifdef HAS_LIBVERIFY
TEST(ProductionMolecular, OriginalStaticVialsRequireDualExecutionAndKeepCausalRejections) {
    auto p = MolecularPuzzle();
    const auto puzzle = MolecularRoot() / "asset/puzzle/edge-cases/c544897394273089.puzzle";
    const auto file =
        MolecularRoot() /
        ("build/molecular-verify-" + std::to_string(opusoptima::test::ProcessId()) + ".solution");
    struct Cleanup {
        std::filesystem::path path;
        ~Cleanup() {
            std::filesystem::remove(path);
        }
    } cleanup{file};
    auto verify = [&](const auto& solution) {
        io::BinaryWriter::WriteSolutionFile(file, solution);
        return io::VerifySolutionFiles(puzzle, file, 200, 10000);
    };
    auto solution = solvers::CompileProductionMolecular(p);
    auto report = verify(solution);
    ASSERT_TRUE(report.Passed()) << report.detail;
    EXPECT_EQ(report.metrics.cycles, 43);
    EXPECT_NE(report.detail.find("independent static-production"), std::string::npos);
    solution.metrics = report.metrics;
    ASSERT_TRUE(verify(solution).Passed());
    for (auto part : {"pipe", "unbonder", "glyph-disposal"}) {
        auto broken = solution;
        std::erase_if(broken.objects, [&](auto& o) { return o.typeName == part; });
        EXPECT_FALSE(verify(broken).Passed()) << part;
    }
    solvers::AssemblySolver solver;
    int attempts = 0;
    const auto accepted = solver.SolveAccepted(p, [&](const auto& candidate) {
        ++attempts;
        return verify(candidate).Passed();
    });
    EXPECT_GE(attempts, 1);
    EXPECT_EQ(accepted.solutionName, solution.solutionName);
}
#endif
