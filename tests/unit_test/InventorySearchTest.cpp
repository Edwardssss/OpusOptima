#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <stdexcept>

#include "BinaryWriter.hpp"
#include "SolutionVerifier.hpp"

#include "FeedLayout.hpp"
#include "InventorySearch.hpp"
#include "SupplyExecutionIR.hpp"

namespace {
using namespace opusoptima;
using core::AtomType;
using solvers::InventorySearchStatus;
using solvers::SupplyPlan;

io::PuzzleData Fixture() {
    io::PuzzleData p;
    p.version = 3;
    p.name = "inventory-search-regression";
    p.outputMultiplier = 1;
    p.permissionBits = solvers::AssemblyPlanner::RequiredPermissions | (1ULL << 2) | (1ULL << 3) |
                       (1ULL << 9) | (1ULL << 12);
    p.inputs = {{{{AtomType::Earth, {0, 0}}, {AtomType::Air, {1, 0}}, {AtomType::Air, {2, 0}}},
                 {{1, {0, 0}, {1, 0}}, {1, {1, 0}, {2, 0}}}},
                {{{AtomType::Salt, {0, 0}}, {AtomType::Earth, {1, 0}}}, {{1, {0, 0}, {1, 0}}}}};
    p.outputs = {{{{AtomType::Salt, {0, 0}},
                   {AtomType::Air, {1, 0}},
                   {AtomType::Salt, {2, 0}},
                   {AtomType::Salt, {3, 0}}},
                  {{1, {0, 0}, {1, 0}}, {1, {1, 0}, {2, 0}}, {1, {2, 0}, {3, 0}}}}};
    return p;
}

TEST(InventorySearchTest, ProjectionRequiresAnAllowedQuicksilverFeed) {
    auto p = Fixture();
    p.permissionBits |= 1ULL << 14;
    p.inputs = {{{{AtomType::Lead, {0, 0}}}, {}}};
    p.outputs = {{{{AtomType::Tin, {0, 0}}}, {}}};
    solvers::InventorySearchLimits limits{128, 8, 16};
    const auto missing = solvers::SearchSupplyInventory(p, 1, limits);
    EXPECT_EQ(missing.status, InventorySearchStatus::Rejected);
    EXPECT_EQ(missing.states, 0U);

    p.inputs.push_back({{{AtomType::Quicksilver, {0, 0}}}, {}});
    std::size_t accepted{};
    const auto found = solvers::SearchSupplyInventory(p, 1, limits, nullptr,
        [&](const SupplyPlan& plan) {
            ++accepted;
            return std::any_of(plan.steps.begin(), plan.steps.end(), [](const auto& step) {
                return step.kind == SupplyPlan::Kind::Reagent;
            });
        });
    ASSERT_EQ(found.status, InventorySearchStatus::Found);
    EXPECT_GT(accepted, 0U);
    EXPECT_EQ(found.plan.usage[1].acquired, 1U);

    solvers::ChemicalRecipe recipe;
    recipe.inputBatches = {1, 0};
    recipe.projections = 1;
    const auto zeroFeed = solvers::SearchSupplyInventory(p, 1, limits, &recipe);
    EXPECT_EQ(zeroFeed.status, InventorySearchStatus::Rejected);
    EXPECT_EQ(zeroFeed.states, 0U);
    recipe.inputBatches[1] = 1;
    EXPECT_EQ(solvers::SearchSupplyInventory(p, 1, limits, &recipe).status,
              InventorySearchStatus::Found);
    recipe.projections = 0;
    const auto disabled = solvers::SearchSupplyInventory(p, 1, limits, &recipe);
    EXPECT_EQ(disabled.status, InventorySearchStatus::Rejected);
    EXPECT_EQ(disabled.states, 0U);

    limits.excludedInputs = {1};
    const auto excluded = solvers::SearchSupplyInventory(p, 1, limits);
    EXPECT_EQ(excluded.status, InventorySearchStatus::Rejected);
    EXPECT_EQ(excluded.states, 0U);
}

TEST(InventorySearchTest, PrefersInheritedColoredEdgeOverEquivalentLooseFeed) {
    auto p = Fixture();
    p.permissionBits &= ~(1ULL << 11);
    p.permissionBits |= 1ULL << 17;
    p.inputs = {{{{AtomType::Salt, {0, 0}}}, {}},
                {{{AtomType::Salt, {0, 0}}, {AtomType::Air, {1, 0}}}, {{14, {0, 0}, {1, 0}}}}};
    p.outputs = {{{{AtomType::Salt, {1, 0}}, {AtomType::Air, {0, 0}}}, {{14, {0, 0}, {1, 0}}}}};
    const auto found = solvers::SearchSupplyInventory(p, 1);
    ASSERT_EQ(found.status, InventorySearchStatus::Found);
    ASSERT_FALSE(found.plan.steps.empty());
    EXPECT_EQ(found.plan.steps.front().kind, SupplyPlan::Kind::Take);
    EXPECT_EQ(found.plan.steps.front().input, 1U);
    EXPECT_EQ(found.plan.usage[0].batches, 0U);
}

TEST(JointInventorySearch, MandatoryInheritedColorSurvivesFlatDemandAndPortVariants) {
    for (int turn = 0; turn < 6; ++turn)
        for (bool swapPorts : {false, true}) {
            auto p = Fixture();
            p.permissionBits &= ~(1ULL << 11);
            p.inputs = {
                {{{AtomType::Salt, {0, 0}}}, {}},
                {{{AtomType::Air, {0, 0}}}, {}},
                {{{AtomType::Salt, {0, 0}}, {AtomType::Air, {1, 0}}}, {{14, {0, 0}, {1, 0}}}},
                {{{AtomType::Fire, {0, 0}}}, {}}};
            const core::HexFrame frame{{3, -2}, turn};
            const auto salt = frame.ToWorld({0, 0}), air = frame.ToWorld({1, 0});
            p.outputs = {{{{AtomType::Air, air}, {AtomType::Salt, salt}}, {{14, salt, air}}},
                         {{{AtomType::Fire, {0, 0}}}, {}}};
            if (swapPorts) std::swap(p.outputs[0], p.outputs[1]);
            const auto found = solvers::SearchJointSupplyInventory(p, 1, {512, 64, 32});
            ASSERT_EQ(found.status, InventorySearchStatus::Found) << turn << ':' << swapPorts;
            EXPECT_EQ(found.plan.usage[0].batches, 0U);
            EXPECT_EQ(found.plan.usage[1].batches, 0U);
            EXPECT_EQ(found.plan.usage[2].batches, 1U);
            const auto ir = solvers::BuildJointSupplyExecutionIR(p, found.plan);
            const auto bonds =
                std::count_if(ir.steps.begin(), ir.steps.end(), [](const auto& step) {
                    return step.operation.kind == solvers::ExecutionIR::Kind::Bond;
                });
            EXPECT_EQ(bonds, 0) << turn << ':' << swapPorts;
            solvers::ExecutionIR::RequireComplete(ir.Replay(p), {1, 1});
        }
}

TEST(JointInventorySearch, RejectFirstFragmentMappingAndAcceptSecondWithinSharedBudget) {
    auto p = Fixture();
    p.permissionBits &= ~(1ULL << 11);
    p.inputs = {{{{AtomType::Air, {0, 0}}, {AtomType::Salt, {1, 0}}}, {{14, {0, 0}, {1, 0}}}},
                {{{AtomType::Air, {0, 0}}, {AtomType::Salt, {1, 0}}}, {{14, {0, 0}, {1, 0}}}}};
    p.outputs = {p.inputs[0]};
    std::vector<solvers::JointSupplyPlan> candidates;
    const auto found = solvers::SearchJointSupplyInventory(p, 1, {128, 32, 16}, nullptr,
                                                           [&](const auto& material) {
                                                               candidates.push_back(material);
                                                               return candidates.size() == 2;
                                                           });
    ASSERT_EQ(found.status, InventorySearchStatus::Found);
    ASSERT_EQ(candidates.size(), 2U);
    EXPECT_NE(candidates[0], candidates[1]);
    EXPECT_NE(candidates[0].usage, candidates[1].usage);
    EXPECT_EQ(found.plan, candidates[1]);
    EXPECT_LE(found.states, 128U);
    for (const auto& material : candidates)
        solvers::ExecutionIR::RequireComplete(
            solvers::BuildJointSupplyExecutionIR(p, material).Replay(p), {1});
    const auto bounded = solvers::SearchJointSupplyInventory(p, 1, {1, 32, 16}, nullptr,
                                                             [](const auto&) { return false; });
    EXPECT_EQ(bounded.status, InventorySearchStatus::Exhausted);
    EXPECT_THROW(
        (void)solvers::SearchJointSupplyInventory(
            p, 1, {128, 32, 16}, nullptr,
            [](const auto&) -> bool { throw std::runtime_error("fragment acceptance failed"); }),
        std::runtime_error);
}

TEST(JointInventorySearch, DistinguishesSameTypeFragmentsByInheritedMasksAndBatch) {
    for (bool reverse : {false, true}) {
        auto p = Fixture();
        p.permissionBits &= ~(1ULL << 11);
        p.inputs = {{{{AtomType::Fire, {0, 0}},
                      {AtomType::Fire, {1, 0}},
                      {AtomType::Fire, {2, 0}},
                      {AtomType::Fire, {3, 0}}},
                     {{2, {0, 0}, {1, 0}}, {1, {1, 0}, {2, 0}}, {8, {2, 0}, {3, 0}}}}};
        p.outputs = {{{{AtomType::Fire, {0, 0}}, {AtomType::Fire, {1, 0}}}, {{8, {0, 0}, {1, 0}}}},
                     {{{AtomType::Fire, {0, 0}}, {AtomType::Fire, {1, 0}}}, {{2, {0, 0}, {1, 0}}}}};
        if (reverse) std::swap(p.outputs[0], p.outputs[1]);
        const auto found = solvers::SearchJointSupplyInventory(p, 1, {1024, 64, 32});
        ASSERT_EQ(found.status, InventorySearchStatus::Found) << reverse;
        EXPECT_EQ(found.plan.usage[0].batches, 1U);
        const auto ir = solvers::BuildJointSupplyExecutionIR(p, found.plan);
        EXPECT_EQ(std::count_if(ir.steps.begin(), ir.steps.end(),
                                [](const auto& step) {
                                    return step.operation.kind == solvers::ExecutionIR::Kind::Bond;
                                }),
                  0);
        EXPECT_EQ(std::count_if(ir.steps.begin(), ir.steps.end(),
                                [](const auto& step) {
                                    return step.operation.kind ==
                                           solvers::ExecutionIR::Kind::Unbond;
                                }),
                  1);
        solvers::ExecutionIR::RequireComplete(ir.Replay(p), {1, 1});
    }
}

TEST(JointInventorySearch, MandatoryMetalColorSurvivesProjectionAndSourceReordering) {
    for (bool reverse : {false, true}) {
        auto p = Fixture();
        p.permissionBits |= (1ULL << 14);
        p.permissionBits &= ~((1ULL << 8) | (1ULL << 9) | (1ULL << 11) | (1ULL << 17));
        p.inputs = {
            {{{AtomType::Lead, {0, 0}}, {AtomType::Quicksilver, {1, 0}}}, {{14, {0, 0}, {1, 0}}}},
            {{{AtomType::Quicksilver, {0, 0}}}, {}}};
        p.outputs = {
            {{{AtomType::Tin, {0, 0}}, {AtomType::Quicksilver, {1, 0}}}, {{14, {0, 0}, {1, 0}}}}};
        if (reverse) std::reverse(p.outputs[0].atoms.begin(), p.outputs[0].atoms.end());
        const auto found = solvers::SearchJointSupplyInventory(p, 1, {1024, 64, 32});
        ASSERT_EQ(found.status, InventorySearchStatus::Found) << reverse;
        EXPECT_EQ(found.plan.usage[0].batches, 1U);
        EXPECT_EQ(found.plan.usage[1].batches, 1U);
        const auto ir = solvers::BuildJointSupplyExecutionIR(p, found.plan);
        solvers::ExecutionIR::RequireComplete(ir.Replay(p), {1});
        EXPECT_EQ(std::count_if(ir.steps.begin(), ir.steps.end(),
                                [](const auto& step) {
                                    return step.operation.kind ==
                                           solvers::ExecutionIR::Kind::Project;
                                }),
                  1);
        EXPECT_EQ(
            std::count_if(ir.steps.begin(), ir.steps.end(),
                          [](const auto& step) {
                              return step.operation.kind == solvers::ExecutionIR::Kind::Bond ||
                                     step.operation.kind == solvers::ExecutionIR::Kind::Unbond;
                          }),
            0);
    }
}

TEST(JointInventorySearch, RejectsMissingMaskAndNonRigidMandatoryGeometry) {
    auto p = Fixture();
    p.permissionBits &= ~(1ULL << 8);
    p.permissionBits &= ~(1ULL << 11);
    p.permissionBits &= ~(1ULL << 17);
    p.inputs = {{{{AtomType::Air, {0, 0}}, {AtomType::Salt, {1, 0}}}, {{2, {0, 0}, {1, 0}}}}};
    p.outputs = {{{{AtomType::Air, {0, 0}}, {AtomType::Salt, {1, 0}}}, {{14, {0, 0}, {1, 0}}}}};
    EXPECT_NE(solvers::SearchJointSupplyInventory(p, 1, {512, 32, 32}).status,
              InventorySearchStatus::Found);
    p.inputs = {{{{AtomType::Air, {0, 0}}, {AtomType::Salt, {1, 0}}, {AtomType::Earth, {2, 0}}},
                 {{1, {0, 0}, {1, 0}}, {1, {1, 0}, {2, 0}}}}};
    p.outputs = {{{{AtomType::Air, {0, 0}}, {AtomType::Salt, {1, 0}}, {AtomType::Earth, {1, 1}}},
                  {{1, {0, 0}, {1, 0}}, {1, {1, 0}, {1, 1}}}}};
    EXPECT_NE(solvers::SearchJointSupplyInventory(p, 1, {512, 32, 32}).status,
              InventorySearchStatus::Found);
}

TEST(SupplyPlannerBudget, ZeroSearchBudgetPreservesGreedySupply) {
    auto p = Fixture();
    p.inputs = {{{{AtomType::Salt, {0, 0}}}, {}}};
    p.outputs = {{{{AtomType::Salt, {0, 0}}}, {}}};
    const auto plan = solvers::AssemblyPlanner::PlanSupply(p, false, 0);
    ASSERT_FALSE(plan.steps.empty());
    EXPECT_EQ(plan.productsPerPeriod, 1U);
}

TEST(JointInventorySearch, CalcifiedDispersionPreservesEveryChoiceAndReactionBudget) {
    for (int consumed = 2; consumed <= 5; ++consumed) {
        auto p = Fixture();
        p.permissionBits |= 1ULL << 18;
        p.inputs = {{{{AtomType::Quintessence, {0, 0}}}, {}}};
        p.outputs = {{{{AtomType::Salt, {0, 0}}}, {}}};
        for (int element = 2; element <= 5; ++element)
            if (element != consumed)
                p.outputs.push_back({{{static_cast<AtomType>(element), {0, 0}}}, {}});
        solvers::ChemicalRecipe recipe;
        recipe.inputBatches = {1};
        recipe.calcifications = 1;
        recipe.dispersions = 1;
        auto found = solvers::SearchJointSupplyInventory(p, 1, {}, &recipe);
        ASSERT_EQ(found.status, InventorySearchStatus::Found) << consumed;
        const auto step =
            std::find_if(found.plan.steps.begin(), found.plan.steps.end(),
                         [](const auto& s) { return s.supply.kind == SupplyPlan::Kind::Disperse; });
        ASSERT_NE(step, found.plan.steps.end());
        EXPECT_EQ(step->supply.conversion, solvers::ElementConversion::Calcification);
        EXPECT_EQ(step->supply.dispersedElement, static_cast<AtomType>(consumed));
        recipe.calcifications = 0;
        EXPECT_EQ(solvers::SearchJointSupplyInventory(p, 1, {}, &recipe).status,
                  InventorySearchStatus::Rejected);
        recipe.calcifications = 1;
        recipe.dispersions = 0;
        EXPECT_EQ(solvers::SearchJointSupplyInventory(p, 1, {}, &recipe).status,
                  InventorySearchStatus::Rejected);
        p.permissionBits &= ~(1ULL << 12);
        EXPECT_EQ(solvers::SearchJointSupplyInventory(p, 1).status,
                  InventorySearchStatus::Rejected);
    }
}

TEST(JointInventorySearch, CalcifiesParkedDispersionBeforeOpeningAnotherBatch) {
    auto p = Fixture();
    p.permissionBits |= 1ULL << 18;
    p.inputs = {{{{AtomType::Quintessence, {0, 0}}}, {}}};
    p.outputs = {{{{AtomType::Air, {0, 0}}}, {}},
                 {{{AtomType::Salt, {0, 0}}}, {}},
                 {{{AtomType::Salt, {0, 0}}}, {}},
                 {{{AtomType::Salt, {0, 0}}}, {}}};
    for (std::size_t rotation = 0; rotation < p.outputs.size(); ++rotation) {
        const auto found = solvers::SearchJointSupplyInventory(p, 1, {192, 512, 512, true});
        ASSERT_EQ(found.status, InventorySearchStatus::Found) << rotation;
        EXPECT_LE(found.states, 192U);
        ASSERT_EQ(found.plan.usage.size(), 1U);
        EXPECT_EQ(found.plan.usage[0].batches, 1U);
        EXPECT_EQ(std::count_if(found.plan.steps.begin(), found.plan.steps.end(),
                                [](const auto& step) {
                                    return step.supply.kind == SupplyPlan::Kind::Disperse;
                                }),
                  1);
        EXPECT_EQ(std::count_if(found.plan.steps.begin(), found.plan.steps.end(),
                                [](const auto& step) {
                                    return step.supply.conversion ==
                                           solvers::ElementConversion::Calcification;
                                }),
                  3);
        std::rotate(p.outputs.begin(), p.outputs.begin() + 1, p.outputs.end());
    }
}

TEST(JointInventorySearch, RejectedExecutionEnumeratesDistinctHistoriesWithinSharedBudget) {
    auto p = Fixture();
    p.inputs = {{{{AtomType::Salt, {0, 0}}}, {}}, {{{AtomType::Salt, {0, 0}}}, {}}};
    p.outputs = {{{{AtomType::Salt, {0, 0}}}, {}}};
    std::vector<solvers::JointSupplyPlan> candidates;
    const auto result = solvers::SearchJointSupplyInventory(p, 1, {64, 16, 16, true}, nullptr,
                                                            [&](const auto& candidate) {
                                                                candidates.push_back(candidate);
                                                                return candidates.size() == 2;
                                                            });
    ASSERT_EQ(result.status, InventorySearchStatus::Found);
    ASSERT_EQ(candidates.size(), 2U);
    EXPECT_NE(candidates[0], candidates[1]);
    EXPECT_EQ(result.plan, candidates[1]);
    EXPECT_LE(result.states, 64U);
    std::size_t rejected = 0;
    const auto exhausted =
        solvers::SearchJointSupplyInventory(p, 1, {1, 16, 16, true}, nullptr, [&](const auto&) {
            ++rejected;
            return false;
        });
    EXPECT_EQ(exhausted.status, InventorySearchStatus::Exhausted);
    EXPECT_LE(exhausted.states, 1U);
    EXPECT_EQ(rejected, 0U);
    EXPECT_THROW((void)solvers::SearchJointSupplyInventory(
                     p, 1, {}, nullptr,
                     [](const auto&) -> bool {
                         throw std::runtime_error("independent acceptance unavailable");
                     }),
                 std::runtime_error);
}

TEST(JointInventorySearch, CatalystOrderingPreservesPortsAndSharedBounds) {
    auto p = Fixture();
    p.permissionBits |= (1ULL << 13) | (1ULL << 18);
    p.inputs = {{{{AtomType::Quintessence, {0, 0}}}, {}}, {{{AtomType::Salt, {0, 0}}}, {}}};
    p.outputs = {{{{AtomType::Fire, {0, 0}}, {AtomType::Fire, {1, 0}}}, {{1, {0, 0}, {1, 0}}}}};
    for (auto element : {AtomType::Earth, AtomType::Air, AtomType::Water})
        p.outputs.push_back({{{element, {0, 0}}}, {}});
    for (std::size_t rotation = 0; rotation < p.outputs.size(); ++rotation) {
        const auto found = solvers::SearchJointSupplyInventory(p, 1, {192, 512, 512, true});
        ASSERT_EQ(found.status, InventorySearchStatus::Found) << rotation;
        EXPECT_LE(found.states, 192U);
        std::vector<std::size_t> deliveries(p.outputs.size());
        for (const auto& step : found.plan.steps) {
            if (step.output >= p.outputs.size()) continue;
            ASSERT_LT(step.supply.targetAtom, p.outputs[step.output].atoms.size());
            if (step.supply.kind == SupplyPlan::Kind::Take ||
                (step.supply.kind == SupplyPlan::Kind::Coproduct &&
                 step.supply.conversion != solvers::ElementConversion::Duplication) ||
                step.supply.kind == SupplyPlan::Kind::Disperse)
                ++deliveries[step.output];
        }
        for (std::size_t port = 0; port < p.outputs.size(); ++port)
            EXPECT_EQ(deliveries[port], p.outputs[port].atoms.size());
        for (std::size_t budget : {0U, 1U, 2U}) {
            std::size_t accepted = 0;
            const auto bounded = solvers::SearchJointSupplyInventory(p, 1, {budget, 512, 512, true},
                                                                     nullptr, [&](const auto&) {
                                                                         ++accepted;
                                                                         return false;
                                                                     });
            EXPECT_EQ(bounded.status, InventorySearchStatus::Exhausted);
            EXPECT_LE(bounded.states, budget);
            EXPECT_EQ(accepted, 0U);
        }
        std::rotate(p.outputs.begin(), p.outputs.begin() + 1, p.outputs.end());
    }
}

TEST(JointInventorySearch, ExcludedFeedsPreservePortIndicesAndRecipeConstraints) {
    auto p = Fixture();
    p.inputs = {{{{AtomType::Salt, {0, 0}}}, {}}, {{{AtomType::Salt, {0, 0}}}, {}}};
    p.outputs = {{{{AtomType::Salt, {0, 0}}}, {}}};
    solvers::InventorySearchLimits limits;
    limits.excludedInputs = {0};
    const auto found = solvers::SearchJointSupplyInventory(p, 1, limits);
    ASSERT_EQ(found.status, InventorySearchStatus::Found);
    ASSERT_EQ(found.plan.usage.size(), 2U);
    EXPECT_EQ(found.plan.usage[0].batches, 0U);
    EXPECT_EQ(found.plan.usage[1].batches, 1U);
    auto recipe = solvers::ChemicalPlanner::Plan(p, 1, false);
    recipe.inputBatches = {1, 0};
    EXPECT_EQ(solvers::SearchJointSupplyInventory(p, 1, limits, &recipe).status,
              InventorySearchStatus::Rejected);
    recipe.inputBatches = {0, 1};
    EXPECT_EQ(solvers::SearchJointSupplyInventory(p, 1, limits, &recipe).status,
              InventorySearchStatus::Found);
    limits.excludedInputs.push_back(1);
    EXPECT_NE(solvers::SearchJointSupplyInventory(p, 1, limits).status,
              InventorySearchStatus::Found);
}

TEST(JointInventorySearch, SharesOneMolecularBatchAcrossOutputPorts) {
    auto p = Fixture();
    p.inputs.resize(1);
    p.inputs[0] = {{{AtomType::Earth, {0, 0}}, {AtomType::Air, {1, 0}}}, {{1, {0, 0}, {1, 0}}}};
    p.outputs = {{{{AtomType::Air, {0, 0}}}, {}}, {{{AtomType::Earth, {0, 0}}}, {}}};
    const auto result = solvers::SearchJointSupplyInventory(p, 1);
    ASSERT_EQ(result.status, InventorySearchStatus::Found);
    ASSERT_EQ(result.plan.usage.size(), 1U);
    EXPECT_EQ(result.plan.usage[0].batches, 1U);
    EXPECT_EQ(result.plan.usage[0].acquired, 2U);
    EXPECT_EQ(result.plan.usage[0].discarded, 0U);
    std::vector<std::size_t> ports;
    std::array<bool, 2> consumed{};
    for (const auto& step : result.plan.steps) {
        if (step.supply.kind != SupplyPlan::Kind::Take) continue;
        ASSERT_LT(step.output, p.outputs.size());
        EXPECT_EQ(step.supply.targetAtom, 0U);
        ASSERT_LT(step.supply.sourceAtom, consumed.size());
        EXPECT_FALSE(consumed[step.supply.sourceAtom]);
        consumed[step.supply.sourceAtom] = true;
        EXPECT_EQ(p.inputs[0].atoms[step.supply.sourceAtom].type,
                  p.outputs[step.output].atoms[0].type);
        ports.push_back(step.output);
    }
    EXPECT_EQ(ports, (std::vector<std::size_t>{0, 1}));
    EXPECT_TRUE(consumed[0] && consumed[1]);
    auto single = p;
    single.outputs.resize(1);
    EXPECT_NE(solvers::SearchSupplyInventory(single, 1).status, InventorySearchStatus::Found);
}

TEST(JointInventorySearch, ReusesGeneratedCoproductsWithoutAdditionalFeedBatches) {
    auto p = Fixture();
    p.permissionBits |= 1ULL << 18;
    p.inputs = {{{{AtomType::Quintessence, {0, 0}}}, {}}};
    p.outputs = {{{{AtomType::Air, {0, 0}}}, {}},
                 {{{AtomType::Salt, {0, 0}}}, {}},
                 {{{AtomType::Salt, {0, 0}}}, {}},
                 {{{AtomType::Salt, {0, 0}}}, {}}};
    const auto result = solvers::SearchJointSupplyInventory(p, 1, {256, 512, 512, true});
    ASSERT_EQ(result.status, InventorySearchStatus::Found);
    ASSERT_EQ(result.plan.usage.size(), 1U);
    EXPECT_EQ(result.plan.usage[0].batches, 1U);
    EXPECT_EQ(result.plan.usage[0].acquired, 1U);
    EXPECT_EQ(result.plan.usage[0].discarded, 0U);
    EXPECT_EQ(std::count_if(result.plan.steps.begin(), result.plan.steps.end(),
                            [&](const auto& step) {
                                return step.supply.input == p.inputs.size() &&
                                       step.supply.conversion ==
                                           solvers::ElementConversion::Calcification;
                            }),
              3);
    EXPECT_LE(result.states, 256U);
    p.permissionBits &= ~(1ULL << 12);
    EXPECT_NE(solvers::SearchJointSupplyInventory(p, 1, {256, 512, 512, true}).status,
              InventorySearchStatus::Found);
}

TEST(JointInventorySearch, RejectsUnconsumedCoproductAndRepeatingDemand) {
    auto p = Fixture();
    p.inputs = {
        {{{AtomType::Earth, {0, 0}}, {AtomType::Air, {1, 0}}, {AtomType::Water, {2, 0}}}, {}}};
    p.outputs = {{{{AtomType::Air, {0, 0}}}, {}}, {{{AtomType::Earth, {0, 0}}}, {}}};
    EXPECT_NE(solvers::SearchJointSupplyInventory(p, 1).status, InventorySearchStatus::Found);
    p.outputs[1].atoms[0].type = AtomType::Repeat;
    EXPECT_EQ(solvers::SearchJointSupplyInventory(p, 1).status, InventorySearchStatus::Rejected);
    EXPECT_EQ(solvers::SearchJointSupplyInventory(p, 0).status, InventorySearchStatus::Rejected);
}

// Replay inventory independently: exposed feed cells, refill boundaries, exact
// buffer identities, ordered demand/reagents, permissions and final conservation.
void Replay(const io::PuzzleData& p, const SupplyPlan& plan) {
    struct Atom {
        std::size_t input{}, atom{};
        bool occupied{};
    };
    std::array<Atom, 8> buffers{};
    std::vector<std::vector<bool>> available;
    std::vector<solvers::FeedLayout> layouts;
    std::vector<SupplyPlan::Usage> usage(p.inputs.size());
    for (const auto& input : p.inputs) {
        available.emplace_back(input.atoms.size(), false);
        layouts.push_back(solvers::PlanFeedLayout(input));
    }
    std::vector<std::size_t> order;
    for (std::size_t i = 0; i < p.outputs[0].atoms.size(); ++i)
        order.push_back(i);
    std::sort(order.begin(), order.end(), [&](auto a, auto b) {
        auto x = p.outputs[0].atoms[a].position, y = p.outputs[0].atoms[b].position;
        return x.r != y.r ? x.r > y.r : x.q > y.q;
    });
    const auto molecule = order;
    for (std::size_t i = 1; i < plan.productsPerPeriod; ++i)
        order.insert(order.end(), molecule.begin(), molecule.end());
    std::size_t next = 0;
    int mercury = 0;
    int purification = 0;
    for (const auto& step : plan.steps) {
        ASSERT_LT(step.input, p.inputs.size());
        ASSERT_LT(step.sourceAtom, available[step.input].size());
        auto& stock = available[step.input];
        if (step.buffer >= 0 && step.kind != SupplyPlan::Kind::Store) {
            ASSERT_LT(step.buffer, 8);
            auto& stored = buffers[step.buffer];
            ASSERT_TRUE(stored.occupied);
            EXPECT_EQ(stored.input, step.input);
            EXPECT_EQ(stored.atom, step.sourceAtom);
            stored.occupied = false;
        } else {
            if (std::none_of(stock.begin(), stock.end(), [](bool x) { return x; })) {
                std::fill(stock.begin(), stock.end(), true);
                ++usage[step.input].batches;
            }
            ASSERT_TRUE(stock[step.sourceAtom]);
            auto c = layouts[step.input].cells[step.sourceAtom];
            for (std::size_t j = 0; j < stock.size(); ++j) {
                auto other = layouts[step.input].cells[j];
                EXPECT_FALSE(stock[j] && other.q == c.q && other.r > c.r);
            }
            stock[step.sourceAtom] = false;
        }
        if (step.kind == SupplyPlan::Kind::Store) {
            ASSERT_GE(step.buffer, 0);
            ASSERT_LT(step.buffer, 8);
            EXPECT_FALSE(buffers[step.buffer].occupied);
            buffers[step.buffer] = {step.input, step.sourceAtom, true};
            continue;
        }
        if (step.kind == SupplyPlan::Kind::Discard) {
            EXPECT_NE(p.permissionBits & (1ULL << 17), 0U);
            ++usage[step.input].discarded;
            continue;
        }
        ++usage[step.input].acquired;
        ASSERT_LT(next, order.size());
        EXPECT_EQ(step.targetAtom, order[next]);
        const auto source = p.inputs[step.input].atoms[step.sourceAtom].type;
        const auto wanted = p.outputs[0].atoms[order[next]].type;
        if (step.kind == SupplyPlan::Kind::Purify) {
            EXPECT_EQ(mercury, 0);
            EXPECT_NE(p.permissionBits & (1ULL << 15), 0U);
            EXPECT_EQ(step.conversion, solvers::ElementConversion::None);
            EXPECT_EQ(static_cast<int>(source), static_cast<int>(wanted) + 1);
            if (++purification == 2) {
                purification = 0;
                ++next;
            }
        } else if (mercury) {
            EXPECT_EQ(step.kind, SupplyPlan::Kind::Reagent);
            EXPECT_EQ(source, AtomType::Quicksilver);
            EXPECT_EQ(step.conversion, solvers::ElementConversion::None);
            if (--mercury == 0) ++next;
        } else {
            EXPECT_EQ(step.kind, SupplyPlan::Kind::Take);
            if (step.conversion == solvers::ElementConversion::Projection) {
                EXPECT_NE(p.permissionBits & (1ULL << 14), 0U);
                EXPECT_GE(static_cast<int>(source), 8);
                EXPECT_LE(static_cast<int>(source), 12);
                EXPECT_GE(static_cast<int>(wanted), 7);
                mercury = static_cast<int>(source) - static_cast<int>(wanted);
                EXPECT_GT(mercury, 0);
            } else {
                if (step.conversion == solvers::ElementConversion::Calcification) {
                    EXPECT_NE(p.permissionBits & (1ULL << 12), 0U);
                    EXPECT_GE(static_cast<int>(source), 2);
                    EXPECT_LE(static_cast<int>(source), 5);
                    EXPECT_EQ(wanted, AtomType::Salt);
                } else
                    EXPECT_EQ(source, wanted);
                ++next;
            }
        }
    }
    EXPECT_EQ(next, order.size());
    EXPECT_EQ(mercury, 0);
    EXPECT_EQ(purification, 0);
    for (const auto& buffer : buffers)
        EXPECT_FALSE(buffer.occupied);
    for (std::size_t i = 0; i < available.size(); ++i) {
        for (bool remains : available[i])
            EXPECT_FALSE(remains);
        EXPECT_EQ(usage[i].batches, plan.usage[i].batches);
        EXPECT_EQ(usage[i].acquired, plan.usage[i].acquired);
        EXPECT_EQ(usage[i].discarded, plan.usage[i].discarded);
        EXPECT_EQ(usage[i].batches * p.inputs[i].atoms.size(),
                  usage[i].acquired + usage[i].discarded);
    }
}

TEST(InventorySearchTest, MetalReachabilityPreservesMultiStagePurification) {
    auto p = Fixture();
    p.permissionBits |= 1ULL << 15;
    p.inputs = {{{{AtomType::Lead, {0, 0}}}, {}}};
    p.outputs = {{{{AtomType::Iron, {0, 0}}}, {}}};
    solvers::InventorySearchLimits limits{2048, 64, 64};
    limits.multiStagePurification = true;
    solvers::ChemicalRecipe recipe;
    recipe.inputBatches = {4};
    recipe.purifications = 3;
    const auto found = solvers::SearchSupplyInventory(p, 1, limits, &recipe);
    ASSERT_EQ(found.status, InventorySearchStatus::Found);
    EXPECT_EQ(found.plan.usage[0].acquired, 4U);
    std::array<unsigned, 13> metals{};
    metals[static_cast<std::size_t>(AtomType::Lead)] = 4;
    int pending = 0;
    unsigned reactions = 0;
    unsigned delivered = 0;
    for (const auto& step : found.plan.steps) {
        ASSERT_EQ(step.kind, SupplyPlan::Kind::Purify);
        const auto type = step.input == p.inputs.size()
                              ? step.sourceAtom
                              : static_cast<std::size_t>(p.inputs.at(step.input)
                                                             .atoms.at(step.sourceAtom).type);
        ASSERT_GT(type, static_cast<std::size_t>(AtomType::Gold));
        ASSERT_LT(type, metals.size());
        ASSERT_GT(metals[type], 0U);
        --metals[type];
        EXPECT_EQ(step.retainProduct, type - 1 != static_cast<std::size_t>(AtomType::Iron));
        if (!pending) {
            pending = static_cast<int>(type);
        } else {
            ASSERT_EQ(pending, static_cast<int>(type));
            pending = 0;
            ++reactions;
            if (type - 1 == static_cast<std::size_t>(AtomType::Iron))
                ++delivered;
            else
                ++metals[type - 1];
        }
    }
    EXPECT_EQ(pending, 0);
    EXPECT_EQ(reactions, recipe.purifications);
    EXPECT_EQ(delivered, 1U);
    EXPECT_TRUE(std::all_of(metals.begin(), metals.end(), [](auto count) { return count == 0; }));
    recipe.purifications = 0;
    const auto disabled = solvers::SearchSupplyInventory(p, 1, limits, &recipe);
    EXPECT_EQ(disabled.status, InventorySearchStatus::Rejected);
    EXPECT_EQ(disabled.states, 0U);
}

TEST(InventorySearchTest, SingleStagePurificationSharesMolecularCoproducts) {
    auto p = Fixture();
    p.permissionBits |= 1ULL << 15;
    p.inputs = {{{{AtomType::Lead, {0, 0}}, {AtomType::Salt, {1, 0}}}, {{1, {0, 0}, {1, 0}}}}};
    p.outputs = {{{{AtomType::Tin, {0, 0}}, {AtomType::Salt, {1, 0}}, {AtomType::Salt, {2, 0}}},
                  {{1, {0, 0}, {1, 0}}, {1, {1, 0}, {2, 0}}}}};
    solvers::InventorySearchLimits limits;
    limits.singleStagePurification = true;
    auto result = solvers::SearchSupplyInventory(p, 1, limits);
    ASSERT_EQ(result.status, InventorySearchStatus::Found);
    Replay(p, result.plan);
    EXPECT_EQ(result.plan.usage[0].batches, 2U);
    EXPECT_EQ(result.plan.usage[0].acquired, 4U);
    EXPECT_EQ(result.plan.usage[0].discarded, 0U);
    EXPECT_EQ(std::count_if(result.plan.steps.begin(), result.plan.steps.end(),
                            [](const auto& step) { return step.kind == SupplyPlan::Kind::Purify; }),
              2);
    EXPECT_NE(solvers::SearchSupplyInventory(p, 1).status, InventorySearchStatus::Found);
    p.permissionBits &= ~(1ULL << 15);
    EXPECT_NE(solvers::SearchSupplyInventory(p, 1, limits).status, InventorySearchStatus::Found);
    p.permissionBits |= 1ULL << 15;
    p.outputs[0].atoms.pop_back();
    p.outputs[0].bonds.pop_back();
    EXPECT_NE(solvers::SearchSupplyInventory(p, 1, limits).status, InventorySearchStatus::Found);
}

#ifdef HAS_LIBVERIFY
void VerifyPhysicalPlan(const io::PuzzleData& p) {
    const auto puzzle = std::filesystem::current_path() / "inventory-regression.puzzle";
    const auto solution = std::filesystem::current_path() / "inventory-regression.solution";
    std::filesystem::create_directories(puzzle.parent_path());
    struct Cleanup {
        std::filesystem::path puzzle, solution;
        ~Cleanup() {
            std::error_code error;
            std::filesystem::remove(puzzle, error);
            std::filesystem::remove(solution, error);
        }
    } cleanup{puzzle, solution};
    {
        std::ofstream out(puzzle, std::ios::binary);
        const auto word = [&](std::uint64_t value, int bytes) {
            for (int i = 0; i < bytes; ++i)
                out.put(static_cast<char>(value >> (8 * i)));
        };
        word(3, 4);
        word(p.name.size(), 1);
        out.write(p.name.data(), p.name.size());
        word(0, 8);
        word(p.permissionBits, 8);
        const auto list = [&](const std::vector<io::Molecule>& molecules) {
            word(molecules.size(), 4);
            for (const auto& m : molecules) {
                word(m.atoms.size(), 4);
                for (const auto& a : m.atoms) {
                    word(static_cast<std::uint8_t>(a.type), 1);
                    word(a.position.q, 1);
                    word(a.position.r, 1);
                }
                word(m.bonds.size(), 4);
                for (const auto& b : m.bonds) {
                    word(b.bits, 1);
                    word(b.from.q, 1);
                    word(b.from.r, 1);
                    word(b.to.q, 1);
                    word(b.to.r, 1);
                }
            }
        };
        list(p.inputs);
        list(p.outputs);
        word(p.outputMultiplier, 4);
        word(0, 1);
        ASSERT_TRUE(out.good());
    }
    io::BinaryWriter::WriteSolutionFile(solution, solvers::AssemblySolver{}.Solve(p));
    const auto report = io::VerifySolutionFiles(puzzle, solution, 30000, 2000);
    EXPECT_TRUE(report.Passed()) << report.detail << " cycle=" << report.errorCycle;
}
#endif

TEST(InventorySearchTest, GreedyFailureFindsWasteFreeOrderedPlan) {
    const auto p = Fixture();
    const auto found = solvers::SearchSupplyInventory(p, 2);
    ASSERT_EQ(found.status, InventorySearchStatus::Found);
    EXPECT_GT(found.states, 0U);
    EXPECT_EQ(found.plan, solvers::SearchSupplyInventory(p, 2).plan);
    Replay(p, found.plan);
    // Both original greedy modes fail for every admissible period on this
    // fixture: direct salt priority and local conversion choices strand stock.
    const auto integrated = solvers::AssemblyPlanner::PlanSupply(p);
    EXPECT_TRUE(integrated.recipeConstrained);
    EXPECT_EQ(integrated.productsPerPeriod, 2U);
    Replay(p, integrated);
    EXPECT_NO_THROW((void)solvers::AssemblySolver{}.Solve(p));
#ifdef HAS_LIBVERIFY
    VerifyPhysicalPlan(p);
#endif
}

TEST(InventorySearchTest, PeriodRecipeSharesWholeInputBatches) {
    auto p = Fixture();
    p.inputs.resize(1);
    p.inputs[0].atoms = {{AtomType::Salt, {0, 0}}, {AtomType::Salt, {1, 0}}};
    p.inputs[0].bonds = {{1, {0, 0}, {1, 0}}};
    p.outputs[0].atoms = {{AtomType::Salt, {0, 0}}};
    p.outputs[0].bonds.clear();
    EXPECT_THROW(solvers::ChemicalPlanner::Plan(p, 1, false), std::invalid_argument);
    const auto single = solvers::ChemicalPlanner::Plan(p);
    EXPECT_EQ(single.inputBatches, (std::vector<std::size_t>{1}));
    EXPECT_EQ(single.surplus[0], 1U);
    const auto recipe = solvers::ChemicalPlanner::Plan(p, 2, false);
    EXPECT_EQ(recipe.inputBatches, (std::vector<std::size_t>{1}));
    EXPECT_TRUE(
        std::all_of(recipe.surplus.begin(), recipe.surplus.end(), [](auto n) { return n == 0; }));
    const auto found = solvers::SearchSupplyInventory(p, 2, {}, &recipe);
    ASSERT_EQ(found.status, InventorySearchStatus::Found);
    EXPECT_TRUE(found.plan.recipeConstrained);
    Replay(p, found.plan);
    const auto integrated = solvers::AssemblyPlanner::PlanSupply(p);
    EXPECT_TRUE(integrated.recipeConstrained);
    EXPECT_EQ(integrated.productsPerPeriod, 2U);
    EXPECT_EQ(integrated.usage[0].batches, 1U);
    Replay(p, integrated);
#ifdef HAS_LIBVERIFY
    VerifyPhysicalPlan(p);
#endif
    EXPECT_THROW(solvers::ChemicalPlanner::Plan(p, 0, false), std::invalid_argument);
    EXPECT_THROW(solvers::ChemicalPlanner::Plan(p, 13, false), std::invalid_argument);
}

TEST(InventorySearchTest, RecipeBudgetsAreNotAdvisory) {
    const auto p = Fixture();
    auto recipe = solvers::ChemicalPlanner::Plan(p, 2, false);
    const auto found = solvers::SearchSupplyInventory(p, 2, {}, &recipe);
    ASSERT_EQ(found.status, InventorySearchStatus::Found);
    Replay(p, found.plan);
    const auto valid = recipe;
    recipe.inputBatches.assign(p.inputs.size(), 0);
    EXPECT_EQ(solvers::SearchSupplyInventory(p, 2, {}, &recipe).status,
              InventorySearchStatus::Rejected);
    recipe = valid;
    recipe.calcifications = 0;
    EXPECT_EQ(solvers::SearchSupplyInventory(p, 2, {}, &recipe).status,
              InventorySearchStatus::Rejected);
    recipe = valid;
    ++recipe.projections;
    EXPECT_EQ(solvers::SearchSupplyInventory(p, 2, {}, &recipe).status,
              InventorySearchStatus::Rejected);
    recipe = valid;
    recipe.duplications = 1;
    EXPECT_EQ(solvers::SearchSupplyInventory(p, 2, {}, &recipe).status,
              InventorySearchStatus::Rejected);
    recipe = valid;
    recipe.purifications = 1;
    EXPECT_EQ(solvers::SearchSupplyInventory(p, 2, {}, &recipe).status,
              InventorySearchStatus::Rejected);
    recipe = valid;
    recipe.animismus = 1;
    EXPECT_EQ(solvers::SearchSupplyInventory(p, 2, {}, &recipe).status,
              InventorySearchStatus::Rejected);
    recipe = valid;
    recipe.inputBatches.clear();
    EXPECT_EQ(solvers::SearchSupplyInventory(p, 2, {}, &recipe).status,
              InventorySearchStatus::Rejected);
}

TEST(InventorySearchTest, ExplicitBoundsAndPeriodRejection) {
    const auto p = Fixture();
    EXPECT_EQ(solvers::SearchSupplyInventory(p, 2, {0, 512, 512}).status,
              InventorySearchStatus::Exhausted);
    const auto bounded = solvers::SearchSupplyInventory(p, 2, {1, 512, 512});
    EXPECT_EQ(bounded.status, InventorySearchStatus::Exhausted);
    EXPECT_LE(bounded.states, 1U);
    EXPECT_EQ(solvers::SearchSupplyInventory(p, 2, {20000, 1, 512}).status,
              InventorySearchStatus::Exhausted);
    EXPECT_EQ(solvers::SearchSupplyInventory(p, 2, {20000, 512, 1}).status,
              InventorySearchStatus::Exhausted);
    EXPECT_EQ(solvers::SearchSupplyInventory(p, 0).status, InventorySearchStatus::Rejected);
    EXPECT_EQ(solvers::SearchSupplyInventory(p, 4).status, InventorySearchStatus::Rejected);
    EXPECT_EQ(solvers::SearchSupplyInventory(p, 13).status, InventorySearchStatus::Rejected);
    auto invalid = p;
    invalid.outputMultiplier = 0;
    EXPECT_EQ(solvers::SearchSupplyInventory(invalid, 1).status, InventorySearchStatus::Rejected);
    invalid = p;
    invalid.inputs[0].atoms.clear();
    EXPECT_EQ(solvers::SearchSupplyInventory(invalid, 1).status, InventorySearchStatus::Rejected);
}

TEST(InventorySearchTest, NondisposalSurplusCannotDisappear) {
    auto p = Fixture();
    p.inputs.resize(1);
    p.inputs[0].atoms = {{AtomType::Salt, {0, 0}}, {AtomType::Quicksilver, {1, 0}}};
    p.inputs[0].bonds.clear();
    p.outputs[0].atoms = {{AtomType::Salt, {0, 0}}};
    p.outputs[0].bonds.clear();
    EXPECT_EQ(solvers::SearchSupplyInventory(p, 1).status, InventorySearchStatus::Rejected);
    p.permissionBits |= 1ULL << 17;
    const auto found = solvers::SearchSupplyInventory(p, 1);
    ASSERT_EQ(found.status, InventorySearchStatus::Found);
    Replay(p, found.plan);
}

TEST(InventorySearchTest, BufferedProjectionAndPermissionGates) {
    auto p = Fixture();
    p.inputs.resize(1);
    p.inputs[0].atoms = {
        {AtomType::Lead, {0, 0}}, {AtomType::Quicksilver, {0, 1}}, {AtomType::Quicksilver, {1, 0}}};
    p.inputs[0].bonds.clear();
    p.outputs[0].atoms = {{AtomType::Iron, {0, 0}}};
    p.outputs[0].bonds.clear();
    EXPECT_EQ(solvers::SearchSupplyInventory(p, 1).status, InventorySearchStatus::Rejected);
    p.permissionBits |= 1ULL << 14;
    const auto projection = solvers::SearchSupplyInventory(p, 1);
    ASSERT_EQ(projection.status, InventorySearchStatus::Found);
    EXPECT_TRUE(std::any_of(projection.plan.steps.begin(), projection.plan.steps.end(),
                            [](const auto& step) { return step.kind == SupplyPlan::Kind::Store; }));
    Replay(p, projection.plan);
    p.inputs[0].atoms = {{AtomType::Air, {0, 0}}};
    p.outputs[0].atoms[0].type = AtomType::Salt;
    ASSERT_EQ(solvers::SearchSupplyInventory(p, 1).status, InventorySearchStatus::Found);
    Replay(p, solvers::SearchSupplyInventory(p, 1).plan);
    p.permissionBits &= ~(1ULL << 12);
    EXPECT_EQ(solvers::SearchSupplyInventory(p, 1).status, InventorySearchStatus::Rejected);
    p.inputs[0].atoms[0].type = AtomType::Salt;
    p.outputs[0].atoms[0].type = AtomType::Air;
    p.permissionBits |= 1ULL << 13;
    EXPECT_EQ(solvers::SearchSupplyInventory(p, 1).status, InventorySearchStatus::Rejected);
}
} // namespace
