#include "AssemblySolver.hpp"
#include "BinaryWriter.hpp"
#include "ChemicalPlanner.hpp"
#include "RepoRoot.hpp"
#include "SolutionVerifier.hpp"
#include <filesystem>
#include <gtest/gtest.h>
using namespace opusoptima;
#ifdef HAS_LIBVERIFY
TEST(DuplicationTest, PreservativeSalt) {
    const auto root = opusoptima::test::RepoRoot(__FILE__);
    const auto puzzle = root / "asset/puzzle/journal/journal2/P060.puzzle";
    auto p = io::BinaryParser::ParsePuzzleFile(puzzle);
    const auto recipe = solvers::ChemicalPlanner::Plan(p);
    EXPECT_FALSE(recipe.inputBatches.empty());
    auto solution = solvers::AssemblySolver{}.Solve(p);
    auto path = root / "build/dup-P060.solution";
    io::BinaryWriter::WriteSolutionFile(path, solution);
    auto report = io::VerifySolutionFiles(puzzle, path, 30000, 2000);
    ASSERT_TRUE(report.Passed()) << report.detail << " cycle=" << report.errorCycle
                                 << " at=" << report.errorU << "," << report.errorV;
    EXPECT_GT(report.metrics.cycles, 0);
    solution.metrics = report.metrics;
    io::BinaryWriter::WriteSolutionFile(path, solution);
    EXPECT_TRUE(io::VerifySolutionFiles(puzzle, path, 30000, 2000).Passed());
}

#endif

TEST(DuplicationTest, CardinalPermissions) {
    for (int cardinal = 2; cardinal <= 5; ++cardinal) {
        const auto element = static_cast<core::AtomType>(cardinal);
        EXPECT_TRUE(solvers::ChemicalPlanner::CanSupply(core::AtomType::Salt, element, 1ULL << 13));
        EXPECT_FALSE(
            solvers::ChemicalPlanner::CanSupply(core::AtomType::Salt, element, 1ULL << 12));
        EXPECT_FALSE(solvers::ChemicalPlanner::CanSupply(core::AtomType::Salt, element, 0));
    }
    EXPECT_FALSE(solvers::ChemicalPlanner::CanSupply(core::AtomType::Salt,
                                                     core::AtomType::Quicksilver, 1ULL << 13));
}

TEST(DuplicationTest, CardinalCatalystIsReusableAndRequired) {
    io::PuzzleData p;
    p.permissionBits = 1ULL << 13;
    p.outputMultiplier = 1;
    p.inputs = {{{{core::AtomType::Salt, {0, 0}}}, {}},
                {{{static_cast<core::AtomType>(2), {0, 0}}}, {}}};
    p.outputs = {{{{static_cast<core::AtomType>(2), {0, 0}},
                   {static_cast<core::AtomType>(2), {1, 0}},
                   {static_cast<core::AtomType>(2), {2, 0}}},
                  {}}};
    // An expensive cardinal feed makes reuse cheaper than acquiring three catalysts.
    p.inputs[0].atoms.push_back({core::AtomType::Salt, {1, 0}});
    p.inputs[1].atoms.push_back({core::AtomType::Salt, {1, 0}});
    p.inputs[1].atoms.push_back({core::AtomType::Salt, {2, 0}});
    const auto recipe = solvers::ChemicalPlanner::Plan(p);
    EXPECT_GT(recipe.duplications, 0u);
    EXPECT_EQ(recipe.duplicatedElements[0], recipe.duplications);
    EXPECT_GE(recipe.inputBatches[1], 1u);
    p.inputs.pop_back();
    EXPECT_THROW(solvers::ChemicalPlanner::Plan(p), std::invalid_argument);
    p.inputs.push_back({{{static_cast<core::AtomType>(2), {0, 0}}}, {}});
    p.permissionBits = 0;
    EXPECT_EQ(solvers::ChemicalPlanner::Plan(p).duplications, 0u);
    EXPECT_FALSE(solvers::ChemicalPlanner::CanSupply(core::AtomType::Salt,
                                                     static_cast<core::AtomType>(2), 0));
}

TEST(DuplicationTest, RealSupplyBalancesWholeBatchesAndReturnsCatalyst) {
    const auto root = opusoptima::test::RepoRoot(__FILE__);
    auto p = io::BinaryParser::ParsePuzzleFile(root / "asset/puzzle/journal/journal2/P060.puzzle");
    auto plan = solvers::AssemblyPlanner::PlanSupply(p);
    std::size_t borrowed = 0, returned = 0, duplicated = 0;
    for (const auto& step : plan.steps) {
        borrowed += step.kind == solvers::SupplyPlan::Kind::Reagent &&
                    step.conversion == solvers::ElementConversion::Duplication;
        returned += step.buffer == -2;
        duplicated += step.kind == solvers::SupplyPlan::Kind::Take &&
                      step.conversion == solvers::ElementConversion::Duplication;
    }
    EXPECT_EQ(borrowed, plan.productsPerPeriod);
    EXPECT_EQ(returned, borrowed);
    EXPECT_EQ(duplicated, 2 * plan.productsPerPeriod);
    for (std::size_t i = 0; i < p.inputs.size(); ++i)
        EXPECT_EQ(plan.usage[i].acquired, plan.usage[i].batches * p.inputs[i].atoms.size());
}
