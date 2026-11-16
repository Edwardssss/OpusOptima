#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "BinaryParser.hpp"
#include "BinaryWriter.hpp"
#include "RepoRoot.hpp"
#include "StandardModules.hpp"
#include "StandardRecipe.hpp"
#include "StandardSolver.hpp"

namespace opusoptima::solvers::standard {
namespace {

[[nodiscard]] std::vector<std::byte> Canonical(const io::SolutionData& solution) {
    auto copy = solution;
    copy.solutionName.clear();
    copy.metrics = {};
    return io::BinaryWriter::WriteSolution(copy);
}

TEST(StandardVariantTest, RecipesKeepEveryFeasibleScaleForOptimize) {
    const auto root = test::RepoRoot(__FILE__);
    const auto puzzle = io::BinaryParser::ParsePuzzleFile(
        root / "asset/puzzle/custom/2 monoatomic products.puzzle");
    const auto model = BuildPuzzle(puzzle);
    const auto single = RecipeGenerator(model).GenerateRecipes(false);
    const auto all = RecipeGenerator(model).GenerateRecipes(true);
    ASSERT_EQ(single.size(), 1U);
    ASSERT_GE(all.size(), 2U);
    // The first-recipe contract is unchanged: `--optimize` only adds recipes.
    EXPECT_FALSE(all.front().GetAvailableReactions(ReactionType::Product).empty());
}

TEST(StandardVariantTest, MultiProductEnumeratesBothBuildOrders) {
    const auto root = test::RepoRoot(__FILE__);
    const auto puzzle = io::BinaryParser::ParsePuzzleFile(
        root / "asset/puzzle/custom/2 monoatomic products.puzzle");
    const auto variants = CompileUpstreamStandardVariants(puzzle);
    ASSERT_GE(variants.size(), 2U);
    const auto single = CompileUpstreamStandard(puzzle);
    ASSERT_TRUE(single.has_value());
    // The historical single candidate is still the first variant.
    EXPECT_EQ(Canonical(*single), Canonical(variants.front()));
    // The next variant is the reverse product build order, which must be a
    // different machine even when its metrics happen to match.
    EXPECT_NE(Canonical(variants[0]), Canonical(variants[1]));
}

TEST(StandardVariantTest, SingleOutputDoesNotGainABuildOrderVariant) {
    const auto root = test::RepoRoot(__FILE__);
    const auto puzzle = io::BinaryParser::ParsePuzzleFile(
        root / "asset/puzzle/easy/just-salt.puzzle");
    const auto variants = CompileUpstreamStandardVariants(puzzle);
    ASSERT_FALSE(variants.empty());
    const auto single = CompileUpstreamStandard(puzzle);
    ASSERT_TRUE(single.has_value());
    EXPECT_EQ(Canonical(*single), Canonical(variants.front()));
}

TEST(StandardVariantTest, SpecializedShapeAssetsCompile) {
    const auto root = test::RepoRoot(__FILE__);
    const std::vector<std::string> paths{
        "asset/puzzle/custom/hex with and without center 1.puzzle",
        "asset/puzzle/custom/multiple hex both.puzzle",
        "asset/puzzle/custom/nonlinear-3 bent 1.puzzle",
        "asset/puzzle/custom/nonlinear-3 triangle 1.puzzle",
    };
    for (const std::string& path : paths) {
        SCOPED_TRACE(path);
        const auto puzzle = io::BinaryParser::ParsePuzzleFile(root / path);
        const auto solution = CompileUpstreamStandard(puzzle);
        ASSERT_TRUE(solution.has_value());
        EXPECT_FALSE(solution->objects.empty());
    }
}

TEST(StandardVariantTest, Hex3AndThreeAtomOrdersAreRecorded) {
    const auto root = test::RepoRoot(__FILE__);
    const auto hex = BuildPuzzle(io::BinaryParser::ParsePuzzleFile(
        root / "asset/puzzle/custom/hex with and without center 1.puzzle"));
    std::vector<const Molecule*> products;
    for (const Molecule& product : hex.products) products.push_back(&product);
    ASSERT_TRUE(UseHex3Assembler(products));
    for (const Molecule& product : hex.products)
        EXPECT_EQ(ProductBuildOrder(product).size(), product.Atoms().size());

    auto bent = BuildPuzzle(io::BinaryParser::ParsePuzzleFile(
        root / "asset/puzzle/custom/nonlinear-3 bent 1.puzzle"));
    ASSERT_FALSE(bent.reagents.empty());
    PrepareReagentMolecule(bent.reagents.front());
    EXPECT_EQ(ReagentElementOrder(bent.reagents.front()).size(),
              bent.reagents.front().Atoms().size());
}


TEST(StandardVariantTest, ReagentSubsetRecipesExcludeOtherInputs) {
    Puzzle model;
    model.name = "subset";
    Molecule first({{Element::Salt, {0, 0}, {}}, {Element::Salt, {1, 0}, {}}}, 0);
    Molecule second({{Element::Salt, {0, 0}, {}}, {Element::Salt, {1, 0}, {}}}, 1);
    Molecule product({{Element::Salt, {0, 0}, {}}, {Element::Salt, {1, 0}, {}}}, 0);
    model.reagents.push_back(std::move(first));
    model.reagents.push_back(std::move(second));
    model.products.push_back(std::move(product));
    const auto recipes = RecipeGenerator(model).GenerateRecipesForReagents(true, {0});
    ASSERT_FALSE(recipes.empty());
    for (const Recipe& recipe : recipes) {
        for (const ReactionUsage* usage :
             recipe.GetAvailableReactions(ReactionType::Reagent)) {
            EXPECT_EQ(usage->Get().id, 0);
        }
    }
}
} // namespace
} // namespace opusoptima::solvers::standard
