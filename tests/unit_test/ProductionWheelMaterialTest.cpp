#include "ChemicalPlanner.hpp"
#include "InventorySearch.hpp"
#include "SupplyExecutionIR.hpp"

#include <algorithm>
#include <array>
#include <filesystem>
#include <gtest/gtest.h>
#include "RepoRoot.hpp"

namespace opusoptima::solvers {
namespace {
using IR = ExecutionIR;
using T = core::AtomType;
struct Original {
    const char* file;
    std::size_t period, calcifications, duplications, purifications, animismus;
    std::vector<std::size_t> batches;
};
const std::array<Original, 4> originals{{
    {"tourney-2019/week5.puzzle", 1, 0, 1, 0, 0, {2}},
    {"tourney-2021/OM2021_W4.puzzle", 1, 0, 1, 0, 3, {4}},
    {"workshop-versions/w2946684660.puzzle", 1, 6, 4, 0, 0, {1}},
    {"tourney-2020/OM2020_W5_Overloaded.puzzle", 2, 0, 0, 5, 0, {1, 1, 1, 1}},
}};
io::PuzzleData Puzzle(const Original& original) {
    return io::BinaryParser::ParsePuzzleFile(
        opusoptima::test::RepoRoot(__FILE__) / "asset/puzzle" /
        original.file);
}
InventorySearchLimits Limits() {
    InventorySearchLimits limits;
    limits.maxStates = 4096;
    limits.multiStagePurification = true;
    limits.mechanicalExtractionOrder = false;
    limits.wheelCatalysts = true;
    return limits;
}
JointInventorySearchResult Find(const io::PuzzleData& puzzle, std::size_t period) {
    const auto recipe = ChemicalPlanner::PlanWithWheelCatalysts(
        puzzle, period, (puzzle.permissionBits & (1ULL << 17)) != 0);
    return SearchJointSupplyInventory(puzzle, period, Limits(), &recipe, [&](const auto& supply) {
        try {
            IR::RequireComplete(BuildJointSupplyExecutionIR(puzzle, supply).Replay(puzzle),
                                std::vector<std::size_t>(puzzle.outputs.size(), period));
            return true;
        } catch (const IR::Error&) {
            return false;
        }
    });
}
} // namespace

TEST(ProductionWheelMaterial, DeviceCapabilityDoesNotChangeMaterialOnlyBackendRecipes) {
    auto puzzle = Puzzle(originals[0]);
    for (bool production : {true, false}) {
        SCOPED_TRACE(production);
        if (!production) puzzle.productionInfo.reset();
        EXPECT_THROW((void)ChemicalPlanner::Plan(puzzle, 1, false), std::invalid_argument);
        const auto wheel = ChemicalPlanner::PlanWithWheelCatalysts(puzzle, 1, false);
        EXPECT_EQ(wheel.inputBatches, (std::vector<std::size_t>{2}));
        EXPECT_EQ(wheel.duplications, 1);
    }
}

TEST(ProductionWheelMaterial, OriginalRecipesCloseWithExplicitDeviceCatalystsAndNoFakeAtoms) {
    for (const auto& original : originals) {
        SCOPED_TRACE(original.file);
        const auto puzzle = Puzzle(original);
        const auto recipe = ChemicalPlanner::PlanWithWheelCatalysts(
            puzzle, original.period, (puzzle.permissionBits & (1ULL << 17)) != 0);
        EXPECT_EQ(recipe.inputBatches, original.batches);
        EXPECT_EQ(recipe.calcifications, original.calcifications);
        EXPECT_EQ(recipe.duplications, original.duplications);
        EXPECT_EQ(recipe.purifications, original.purifications);
        EXPECT_EQ(recipe.animismus, original.animismus);
        const auto searched = Find(puzzle, original.period);
        ASSERT_EQ(searched.status, InventorySearchStatus::Found);
        const auto program = BuildJointSupplyExecutionIR(puzzle, searched.plan);
        const auto final = program.Replay(puzzle);
        IR::RequireComplete(final,
                            std::vector<std::size_t>(puzzle.outputs.size(), original.period));
        EXPECT_EQ(final.batches, original.batches);
        const auto count = [&](IR::Kind kind) {
            return std::count_if(program.steps.begin(), program.steps.end(),
                                 [&](const auto& step) { return step.operation.kind == kind; });
        };
        EXPECT_EQ(count(IR::Kind::Calcify), original.calcifications);
        EXPECT_EQ(count(IR::Kind::Duplicate), original.duplications);
        EXPECT_EQ(count(IR::Kind::Purify), original.purifications);
        EXPECT_EQ(count(IR::Kind::Animismus), original.animismus);
        EXPECT_EQ(final.discarded, original.animismus ? 5 : 0);
        for (const auto& step : program.steps)
            if (step.operation.wheelCatalyst) {
                ASSERT_EQ(step.operation.kind, IR::Kind::Duplicate);
                ASSERT_EQ(step.operation.atoms.size(), 1);
                EXPECT_EQ(step.preconditions.atoms.at(step.operation.atoms[0]).type, T::Salt);
                EXPECT_EQ(step.operation.wheelCatalyst->device, 0);
                EXPECT_EQ(step.effects.atoms.size(), step.preconditions.atoms.size());
            }
        EXPECT_EQ(puzzle.productionInfo->vials.size(),
                  Puzzle(original).productionInfo->vials.size());
    }
}

TEST(ProductionWheelMaterial, OriginalInputOrderAndOutputRotationsDoNotDefineRecipes) {
    for (const auto& original : originals)
        for (int rotation = 0; rotation < 6; ++rotation) {
            SCOPED_TRACE(original.file);
            SCOPED_TRACE(rotation);
            auto puzzle = Puzzle(original);
            for (auto& input : puzzle.inputs)
                std::reverse(input.atoms.begin(), input.atoms.end());
            for (auto& output : puzzle.outputs) {
                const auto turn = [&](core::HexCoord cell) {
                    for (int i = 0; i < rotation; ++i)
                        cell = core::RotateClockwise(cell);
                    return cell;
                };
                std::reverse(output.atoms.begin(), output.atoms.end());
                for (auto& atom : output.atoms)
                    atom.position = turn(atom.position);
                for (auto& bond : output.bonds) {
                    bond.from = turn(bond.from);
                    bond.to = turn(bond.to);
                }
            }
            const auto searched = Find(puzzle, original.period);
            ASSERT_EQ(searched.status, InventorySearchStatus::Found);
            IR::RequireComplete(BuildJointSupplyExecutionIR(puzzle, searched.plan).Replay(puzzle),
                                std::vector<std::size_t>(puzzle.outputs.size(), original.period));
        }
}

TEST(ProductionWheelMaterial, OverloadedRequiresAnEvenClosedMaterialPeriod) {
    const auto puzzle = Puzzle(originals[3]);
    EXPECT_THROW((void)ChemicalPlanner::PlanWithWheelCatalysts(puzzle, 1, false),
                 std::invalid_argument);
    const auto searched = Find(puzzle, 2);
    ASSERT_EQ(searched.status, InventorySearchStatus::Found);
    EXPECT_TRUE(
        std::none_of(searched.plan.steps.begin(), searched.plan.steps.end(),
                     [](const auto& step) { return step.supply.wheelCatalyst.has_value(); }));
}

TEST(ProductionWheelMaterial, ProbeRetainsFreshSaltIdentityThroughCalcifyThenWheelConversion) {
    const auto puzzle = Puzzle(originals[2]);
    const auto searched = Find(puzzle, 1);
    ASSERT_EQ(searched.status, InventorySearchStatus::Found);
    EXPECT_EQ(std::count_if(searched.plan.steps.begin(), searched.plan.steps.end(),
                            [](const auto& step) {
                                return step.supply.calcifyBeforeDuplication &&
                                       step.supply.wheelCatalyst.has_value();
                            }),
              4);
    const auto program = BuildJointSupplyExecutionIR(puzzle, searched.plan);
    for (std::size_t i = 1; i < program.steps.size(); ++i) {
        const auto& step = program.steps[i];
        if (!step.operation.wheelCatalyst) continue;
        const auto& prior = program.steps[i - 1];
        EXPECT_EQ(prior.operation.kind, IR::Kind::Calcify);
        EXPECT_EQ(step.operation.atoms[0], prior.preconditions.nextId);
        EXPECT_EQ(step.preconditions.atoms.at(step.operation.atoms[0]).type, T::Salt);
    }
}

TEST(ProductionWheelMaterial, DisabledPermissionsAndMalformedBindingsCannotReplay) {
    const auto puzzle = Puzzle(originals[2]);
    const auto searched = Find(puzzle, 1);
    ASSERT_EQ(searched.status, InventorySearchStatus::Found);
    for (int bit : {12, 13, 28}) {
        auto forbidden = puzzle;
        forbidden.permissionBits &= ~(1ULL << bit);
        EXPECT_THROW((void)BuildJointSupplyExecutionIR(forbidden, searched.plan), IR::Error);
    }
    const auto wheel =
        std::find_if(searched.plan.steps.begin(), searched.plan.steps.end(),
                     [](const auto& step) { return step.supply.wheelCatalyst.has_value(); });
    ASSERT_NE(wheel, searched.plan.steps.end());
    const auto index = wheel - searched.plan.steps.begin();
    auto broken = searched.plan;
    broken.steps[index].supply.wheelCatalyst->orientation = 6;
    EXPECT_THROW((void)BuildJointSupplyExecutionIR(puzzle, broken), IR::Error);
    broken = searched.plan;
    broken.steps[index].supply.wheelCatalyst->element = T::Earth;
    EXPECT_THROW((void)BuildJointSupplyExecutionIR(puzzle, broken), IR::Error);
    broken = searched.plan;
    broken.steps[index].supply.wheelCatalyst.reset();
    EXPECT_THROW((void)BuildJointSupplyExecutionIR(puzzle, broken), IR::Error);
    broken = searched.plan;
    broken.steps[index].supply.kind = SupplyPlan::Kind::Store;
    EXPECT_THROW((void)BuildJointSupplyExecutionIR(puzzle, broken), IR::Error);
    auto noWheel = puzzle;
    noWheel.permissionBits &= ~(1ULL << 28);
    EXPECT_THROW((void)ChemicalPlanner::PlanWithWheelCatalysts(noWheel, 1, false),
                 std::invalid_argument);
    auto disabled = Limits();
    disabled.wheelCatalysts = false;
    disabled.maxStates = 128;
    const auto unavailable = SearchJointSupplyInventory(puzzle, 1, disabled);
    EXPECT_NE(unavailable.status, InventorySearchStatus::Found);
}

TEST(ProductionWheelMaterial, RecipeCountsIncludeBothStepsOfTheWheelConversionChain) {
    const auto puzzle = Puzzle(originals[2]);
    const auto recipe = ChemicalPlanner::PlanWithWheelCatalysts(puzzle, 1, false);
    for (int mutation = 0; mutation < 3; ++mutation) {
        auto wrong = recipe;
        if (mutation == 0)
            --wrong.calcifications;
        else if (mutation == 1)
            --wrong.duplications;
        else
            --wrong.duplicatedElements[2];
        auto limits = Limits();
        limits.maxStates = 128;
        EXPECT_NE(SearchJointSupplyInventory(puzzle, 1, limits, &wrong).status,
                  InventorySearchStatus::Found);
    }
}

TEST(ProductionWheelMaterial, MaterialCatalystBorrowReturnRemainsAvailableWithoutWheelPermission) {
    io::PuzzleData puzzle;
    puzzle.version = 3;
    puzzle.outputMultiplier = 1;
    puzzle.permissionBits = (1ULL << 8) | (1ULL << 9) | (1ULL << 13);
    puzzle.inputs = {{{{T::Air, {}}, {T::Salt, {1, 0}}}, {{1, {}, {1, 0}}}}};
    puzzle.outputs = {{{{T::Air, {}}}, {}}, {{{T::Air, {}}}, {}}};
    const auto searched = Find(puzzle, 1);
    ASSERT_EQ(searched.status, InventorySearchStatus::Found);
    EXPECT_TRUE(
        std::none_of(searched.plan.steps.begin(), searched.plan.steps.end(),
                     [](const auto& step) { return step.supply.wheelCatalyst.has_value(); }));
    const auto program = BuildJointSupplyExecutionIR(puzzle, searched.plan);
    IR::RequireComplete(program.Replay(puzzle), {1, 1});
    EXPECT_TRUE(program.Replay(puzzle).wheelDevices.empty());
}
} // namespace opusoptima::solvers
