#include "SupplyExecutionIR.hpp"
#include "CalcifyAnimismusAdmission.hpp"
#include "InventoryIR.hpp"
#include "InventorySearch.hpp"
#include <algorithm>
#include <gtest/gtest.h>

namespace opusoptima::solvers { namespace {
using T = core::AtomType;
io::PuzzleData SupplyPuzzle() {
    io::PuzzleData p;
    p.version = 3;
    p.outputMultiplier = 1;
    p.permissionBits = ~0ULL;
    p.inputs = {{{{T::Air, {0, 0}}, {T::Salt, {1, 0}}}, {{1, {0, 0}, {1, 0}}}}};
    p.outputs = p.inputs;
    return p;
}
TEST(SupplyExecutionIR, DirectFragmentsPreserveMasksAcrossRotationsAndDeliveryOrders) {
    for (const std::uint8_t mask : {1, 2, 6, 8, 14}) {
        for (int rotation = 0; rotation < 6; ++rotation) {
            for (const bool reverse : {false, true}) {
                auto p = SupplyPuzzle();
                p.inputs[0].bonds[0].bits = mask;
                p.outputs = p.inputs;
                JointSupplyPlan plan;
                plan.productsPerPeriod = 1;
                plan.usage = {{1, 2, 0}};
                for (const std::size_t atom :
                     reverse ? std::vector<std::size_t>{1, 0} : std::vector<std::size_t>{0, 1})
                    plan.steps.push_back({{SupplyPlan::Kind::Take, 0, atom, atom}, 0});
                const auto ir =
                    BuildJointSupplyExecutionIR(p, plan, {{{9, -3}, rotation}}, {{{-4, 7}, 2}});
                EXPECT_EQ(ir.steps, BuildJointSupplyExecutionIRWithPolicy(
                                        p, plan, SupplyMaterialLoweringPolicy::PreserveCompatible,
                                        {{{9, -3}, rotation}}, {{{-4, 7}, 2}})
                                        .steps);
                EXPECT_EQ(std::count_if(ir.steps.begin(), ir.steps.end(),
                                        [](const auto& step) {
                                            return step.operation.kind ==
                                                       ExecutionIR::Kind::Unbond ||
                                                   step.operation.kind == ExecutionIR::Kind::Bond;
                                        }),
                          0)
                    << "mask=" << static_cast<int>(mask) << " rotation=" << rotation;
                ExecutionIR::RequireComplete(ir.Replay(p), {1});
            }
        }
    }
}

TEST(SupplyExecutionIR, ExplicitOrdinaryCutsRecreateBondsWithOriginalIndexFrames) {
    using IR = ExecutionIR;
    constexpr auto cut = SupplyMaterialLoweringPolicy::CutOrdinarySources;
    for (bool reverseSource : {false, true})
        for (bool reverseOutput : {false, true})
            for (int rotation = 0; rotation < 6; ++rotation) {
                auto p = SupplyPuzzle();
                if (reverseSource) std::reverse(p.inputs[0].atoms.begin(), p.inputs[0].atoms.end());
                if (reverseOutput)
                    std::reverse(p.outputs[0].atoms.begin(), p.outputs[0].atoms.end());
                JointSupplyPlan supply;
                supply.productsPerPeriod = 1;
                supply.usage = {{1, 2, 0}};
                for (std::size_t i : {1U, 0U})
                    supply.steps.push_back(
                        {{SupplyPlan::Kind::Take, 0, i, reverseSource == reverseOutput ? i : 1 - i},
                         0});
                const core::HexFrame output{{9, -3}, rotation}, input{{-4, 7}, 2};
                const auto ir =
                    BuildJointSupplyExecutionIRWithPolicy(p, supply, cut, {output}, {input});
                ASSERT_EQ(ir.steps.size(), 6U);
                EXPECT_EQ(ir.steps[0].operation.kind, IR::Kind::Acquire);
                EXPECT_EQ(ir.steps[0].operation.index, 0U);
                EXPECT_EQ(ir.steps[0].effects.atoms.at(1).position,
                          input.ToWorld(p.inputs[0].atoms[0].position));
                EXPECT_EQ(ir.steps[1].operation.kind, IR::Kind::Unbond);
                EXPECT_EQ(ir.steps[1].operation.atoms, (std::vector<IR::AtomId>{1, 2}));
                EXPECT_EQ(ir.steps[1].operation.bits, 1);
                EXPECT_TRUE(ir.steps[1].effects.bonds.empty());
                EXPECT_EQ(ir.steps[4].operation.kind, IR::Kind::Bond);
                for (std::size_t i = 0; i < 2; ++i)
                    EXPECT_EQ(ir.steps[4].effects.atoms.at(i + 1).position,
                              output.ToWorld(p.outputs[0]
                                                 .atoms[reverseSource == reverseOutput ? i : 1 - i]
                                                 .position));
                EXPECT_EQ(ir.steps[4].effects.bonds, ir.steps[0].effects.bonds);
                IR::RequireComplete(ir.Replay(p), {1});
                SupplyPlan scalar;
                scalar.productsPerPeriod = supply.productsPerPeriod;
                scalar.usage = supply.usage;
                for (const auto& step : supply.steps)
                    scalar.steps.push_back(step.supply);
                const auto assembly = AssemblyPlanner::Plan(p);
                EXPECT_EQ(ir.steps, BuildSupplyExecutionIRWithPolicy(p, assembly, scalar, cut,
                                                                     output, {input})
                                        .steps);
                EXPECT_EQ(BuildSupplyExecutionIR(p, assembly, scalar, output, {input}).steps,
                          BuildSupplyExecutionIRWithPolicy(
                              p, assembly, scalar, SupplyMaterialLoweringPolicy::PreserveCompatible,
                              output, {input})
                              .steps);
            }
    auto p = SupplyPuzzle();
    JointSupplyPlan supply;
    supply.productsPerPeriod = 1;
    supply.usage = {{1, 2, 0}};
    supply.steps = {{{SupplyPlan::Kind::Take, 0, 0, 0}, 0}, {{SupplyPlan::Kind::Take, 0, 1, 1}, 0}};
    const auto refused = [&](const auto& puzzle, const auto& plan) {
        EXPECT_THROW((void)BuildJointSupplyExecutionIRWithPolicy(puzzle, plan, cut),
                     IR::Unsupported);
    };
    p.permissionBits &= ~(1ULL << 9);
    refused(p, supply);
    p = SupplyPuzzle();
    p.permissionBits &= ~(1ULL << 8);
    refused(p, supply);
    EXPECT_FALSE(SupportsOrdinarySourceCutLowering(p));
    EXPECT_NO_THROW((void)BuildJointSupplyExecutionIR(p, supply));
    p = SupplyPuzzle();
    p.inputs[0].bonds[0].bits = 2;
    refused(p, supply);
    p = SupplyPuzzle();
    p.outputs[0].bonds[0].bits = 2;
    refused(p, supply);
    p = SupplyPuzzle();
    p.inputs[0].atoms.push_back({T::Fire, {2, 0}});
    refused(p, supply);
    p = SupplyPuzzle();
    p.productionInfo.emplace();
    refused(p, supply);
    p = SupplyPuzzle();
    supply.steps[0].supply.retainProduct = true;
    refused(p, supply);
    supply.steps[0].supply.retainProduct = false;
    supply.steps[0].supply.wheelCatalyst = SupplyPlan::Step::WheelCatalyst{};
    refused(p, supply);
}

TEST(SupplyExecutionIR, InterleavedResidualDisposalDoesNotDestroyInheritedFragment) {
    auto p = SupplyPuzzle();
    p.permissionBits &= ~(1ULL << 11);
    p.inputs[0].atoms.push_back({T::Fire, {2, 0}});
    p.inputs[0].bonds[0].bits = 14;
    p.inputs[0].bonds.push_back({1, {1, 0}, {2, 0}});
    p.outputs[0].bonds[0].bits = 14;
    JointSupplyPlan plan;
    plan.productsPerPeriod = 1;
    plan.usage = {{1, 2, 1}};
    plan.steps = {{{SupplyPlan::Kind::Take, 0, 0, 0}, 0},
                  {{SupplyPlan::Kind::Discard, 0, 2, 0}, 0},
                  {{SupplyPlan::Kind::Take, 0, 1, 1}, 0}};
    const auto ir = BuildJointSupplyExecutionIR(p, plan, {{{8, 0}, 0}});
    EXPECT_EQ(std::count_if(ir.steps.begin(), ir.steps.end(),
                            [](const auto& step) {
                                return step.operation.kind == ExecutionIR::Kind::Unbond;
                            }),
              1);
    EXPECT_EQ(std::count_if(
                  ir.steps.begin(), ir.steps.end(),
                  [](const auto& step) { return step.operation.kind == ExecutionIR::Kind::Bond; }),
              0);
    ExecutionIR::RequireComplete(ir.Replay(p), {1});
}

TEST(SupplyExecutionIR, OriginalStaticProductionRetainsMaterialContextWithoutFakeTransport) {
    auto p = SupplyPuzzle();
    p.productionInfo.emplace();
    p.productionInfo->chambers = {{{-10, 0}, "Large"}, {{10, 0}, "Large"}};
    p.productionInfo->conduits = {{{-10, 0}, {10, 0}, {{0, 0}, {1, 0}}}};
    JointSupplyPlan plan;
    plan.productsPerPeriod = 1;
    plan.usage = {{1, 2, 0}};
    plan.steps = {{{SupplyPlan::Kind::Take, 0, 0, 0}, 0}, {{SupplyPlan::Kind::Take, 0, 1, 1}, 0}};
    EXPECT_THROW((void)ExecutionIR::Initial(p), ExecutionIR::Unsupported);
    const auto program = BuildJointSupplyExecutionIR(p, plan);
    ExecutionIR::RequireComplete(program.Replay(p), {1});
    EXPECT_EQ(p.productionInfo->conduits.size(), 1U);
    p.productionInfo->vials.push_back({{}, true, 1});
    ExecutionIR::RequireComplete(program.Replay(p), {1});
    ExecutionIR::RequireComplete(BuildJointSupplyExecutionIR(p, plan).Replay(p), {1});
    p.productionInfo->vials[0].count = 65;
    EXPECT_THROW((void)program.Replay(p), ExecutionIR::Unsupported);
    EXPECT_THROW((void)BuildJointSupplyExecutionIR(p, plan), ExecutionIR::Unsupported);
}

TEST(SupplyExecutionIR, SeparateInheritedFragmentsUseIndependentRotations) {
    auto p = SupplyPuzzle();
    p.permissionBits &= ~(1ULL << 11);
    p.inputs = {{{{T::Air, {0, 0}}, {T::Salt, {1, 0}}, {T::Fire, {2, 0}}, {T::Earth, {3, 0}}},
                 {{14, {0, 0}, {1, 0}}, {1, {1, 0}, {2, 0}}, {14, {2, 0}, {3, 0}}}}};
    p.outputs = {{{{T::Air, {0, 0}}, {T::Salt, {1, 0}}}, {{14, {0, 0}, {1, 0}}}},
                 {{{T::Fire, {0, 0}}, {T::Earth, {1, 0}}}, {{14, {0, 0}, {1, 0}}}}};
    JointSupplyPlan plan;
    plan.productsPerPeriod = 1;
    plan.usage = {{1, 4, 0}};
    for (std::size_t atom = 0; atom < 4; ++atom)
        plan.steps.push_back({{SupplyPlan::Kind::Take, 0, atom, atom % 2}, atom / 2});
    for (int rotation = 1; rotation < 6; ++rotation) {
        const auto ir = BuildJointSupplyExecutionIR(p, plan, {{{8, 0}, 0}, {{16, 0}, rotation}});
        EXPECT_EQ(std::count_if(ir.steps.begin(), ir.steps.end(),
                                [](const auto& step) {
                                    return step.operation.kind == ExecutionIR::Kind::Unbond;
                                }),
                  1);
        EXPECT_EQ(std::count_if(ir.steps.begin(), ir.steps.end(),
                                [](const auto& step) {
                                    return step.operation.kind == ExecutionIR::Kind::Bond;
                                }),
                  0);
        ExecutionIR::RequireComplete(ir.Replay(p), {1, 1});
    }
}

TEST(SupplyExecutionIR, DirectFragmentsCutOnlyResidualEdgesAndCloseRepeatedBatches) {
    auto p = SupplyPuzzle();
    p.inputs[0].atoms.push_back({T::Fire, {2, 0}});
    p.inputs[0].bonds.push_back({1, {1, 0}, {2, 0}});
    p.inputs[0].bonds[0].bits = 14;
    p.outputs[0].bonds[0].bits = 14;
    JointSupplyPlan plan;
    plan.productsPerPeriod = 2;
    plan.usage = {{2, 4, 2}};
    for (int period = 0; period < 2; ++period) {
        plan.steps.push_back({{SupplyPlan::Kind::Take, 0, 1, 1}, 0});
        plan.steps.push_back({{SupplyPlan::Kind::Take, 0, 0, 0}, 0});
        plan.steps.push_back({{SupplyPlan::Kind::Discard, 0, 2, 0}, 0});
    }
    const auto ir = BuildJointSupplyExecutionIR(p, plan);
    EXPECT_EQ(std::count_if(ir.steps.begin(), ir.steps.end(),
                            [](const auto& step) {
                                return step.operation.kind == ExecutionIR::Kind::Unbond;
                            }),
              2);
    EXPECT_EQ(std::count_if(
                  ir.steps.begin(), ir.steps.end(),
                  [](const auto& step) { return step.operation.kind == ExecutionIR::Kind::Bond; }),
              0);
    ExecutionIR::RequireComplete(ir.Replay(p), {2});
}

TEST(SupplyExecutionIR, JointCatalystBorrowReturnClosesAcrossPorts) {
    auto p = SupplyPuzzle();
    p.permissionBits &= ~(1ULL << 17);
    p.inputs = {{{{T::Air, {0, 0}}, {T::Salt, {1, 0}}}, {{1, {0, 0}, {1, 0}}}}};
    p.outputs = {{{{T::Air, {0, 0}}}, {}}, {{{T::Air, {0, 0}}}, {}}};
    for (std::size_t periods : {1U, 2U}) {
        const auto searched = SearchJointSupplyInventory(p, periods);
        ASSERT_EQ(searched.status, InventorySearchStatus::Found);
        EXPECT_TRUE(std::any_of(searched.plan.steps.begin(), searched.plan.steps.end(),
                                [](const auto& step) {
                                    return step.supply.conversion == ElementConversion::Duplication;
                                }));
        ExecutionIR::RequireComplete(BuildJointSupplyExecutionIR(p, searched.plan).Replay(p),
                                     {periods, periods});
        auto incomplete = searched.plan;
        incomplete.steps.pop_back();
        EXPECT_THROW((void)BuildJointSupplyExecutionIR(p, incomplete), ExecutionIR::Error);
    }
    const auto recipe = ChemicalPlanner::Plan(p, 1, false);
    ASSERT_EQ(recipe.duplications, 1U);
    const auto constrained = SearchJointSupplyInventory(p, 1, {}, &recipe);
    ASSERT_EQ(constrained.status, InventorySearchStatus::Found);
    ExecutionIR::RequireComplete(BuildJointSupplyExecutionIR(p, constrained.plan).Replay(p),
                                 {1, 1});
    p.permissionBits &= ~(1ULL << 13);
    EXPECT_NE(SearchJointSupplyInventory(p, 1).status, InventorySearchStatus::Found);
}

TEST(SupplyExecutionIR, GeneratedCatalystRetainsIdentityWithoutExtraFeed) {
    auto p = SupplyPuzzle();
    p.permissionBits &= ~(1ULL << 17);
    p.inputs = {{{{T::Quintessence, {0, 0}}}, {}}, {{{T::Salt, {0, 0}}}, {}}};
    p.outputs.clear();
    for (const auto type : {T::Earth, T::Air, T::Air, T::Fire, T::Water})
        p.outputs.push_back({{{type, {0, 0}}}, {}});
    const auto searched = SearchJointSupplyInventory(p, 1);
    ASSERT_EQ(searched.status, InventorySearchStatus::Found);
    ASSERT_EQ(searched.plan.usage.size(), 2U);
    for (const auto& usage : searched.plan.usage) {
        EXPECT_EQ(usage.batches, 1U);
        EXPECT_EQ(usage.acquired, 1U);
        EXPECT_EQ(usage.discarded, 0U);
    }
    const auto borrowed =
        std::find_if(searched.plan.steps.begin(), searched.plan.steps.end(), [](const auto& step) {
            return step.supply.kind == SupplyPlan::Kind::Coproduct &&
                   step.supply.conversion == ElementConversion::Duplication;
        });
    ASSERT_NE(borrowed, searched.plan.steps.end());
    EXPECT_EQ(borrowed->supply.input, p.inputs.size());
    EXPECT_EQ(borrowed->supply.sourceAtom, static_cast<std::size_t>(T::Air));
    ExecutionIR::RequireComplete(BuildJointSupplyExecutionIR(p, searched.plan).Replay(p),
                                 {1, 1, 1, 1, 1});
    const auto index = static_cast<std::size_t>(borrowed - searched.plan.steps.begin());
    auto wrongSource = searched.plan;
    wrongSource.steps[index].supply.input = 0;
    EXPECT_THROW((void)BuildJointSupplyExecutionIR(p, wrongSource), ExecutionIR::Error);
    auto wrongType = searched.plan;
    wrongType.steps[index].supply.sourceAtom = static_cast<std::size_t>(T::Fire);
    EXPECT_THROW((void)BuildJointSupplyExecutionIR(p, wrongType), ExecutionIR::Error);
    auto missingReturn = searched.plan;
    const auto returned =
        std::find_if(missingReturn.steps.begin(), missingReturn.steps.end(), [](const auto& step) {
            return step.supply.kind == SupplyPlan::Kind::Take && step.supply.buffer == -2;
        });
    ASSERT_NE(returned, missingReturn.steps.end());
    missingReturn.steps.erase(returned);
    EXPECT_THROW((void)BuildJointSupplyExecutionIR(p, missingReturn), ExecutionIR::Error);
}

TEST(SupplyExecutionIR, DispersionProvidesCatalystForIntegerRecipe) {
    auto p = SupplyPuzzle();
    p.permissionBits &= ~((1ULL << 17) | (1ULL << 28));
    p.inputs = {{{{T::Quintessence, {0, 0}}}, {}}, {{{T::Salt, {0, 0}}}, {}}};
    p.outputs.clear();
    for (const auto type : {T::Air, T::Air, T::Earth, T::Fire, T::Water})
        p.outputs.push_back({{{type, {0, 0}}}, {}});
    const auto recipe = ChemicalPlanner::Plan(p, 1, false);
    EXPECT_EQ(recipe.inputBatches, (std::vector<std::size_t>{1, 1}));
    EXPECT_EQ(recipe.dispersions, 1U);
    EXPECT_EQ(recipe.duplications, 1U);
    EXPECT_EQ(recipe.duplicatedElements, (std::vector<std::size_t>{1, 0, 0, 0}));
    p.permissionBits &= ~(1ULL << 18);
    EXPECT_THROW((void)ChemicalPlanner::Plan(p, 1, false), std::invalid_argument);
}

TEST(SupplyExecutionIR, JointQuintessenceInventoryClosesAcrossPorts) {
    auto p = SupplyPuzzle();
    p.permissionBits &= ~(1ULL << 17);
    p.inputs = {{{{T::Quintessence, {0, 0}}}, {}}};
    p.outputs.clear();
    for (const auto type : {T::Water, T::Fire, T::Earth, T::Air})
        p.outputs.push_back({{{type, {0, 0}}}, {}});
    const auto dispersionRecipe = ChemicalPlanner::Plan(p, 1, false);
    EXPECT_EQ(dispersionRecipe.dispersions, 1U);
    EXPECT_EQ(dispersionRecipe.unifications, 0U);
    const auto constrainedDispersion = SearchJointSupplyInventory(p, 1, {}, &dispersionRecipe);
    ASSERT_EQ(constrainedDispersion.status, InventorySearchStatus::Found);
    ExecutionIR::RequireComplete(
        BuildJointSupplyExecutionIR(p, constrainedDispersion.plan).Replay(p), {1, 1, 1, 1});
    auto wrongRecipe = dispersionRecipe;
    wrongRecipe.dispersions = 0;
    EXPECT_NE(SearchJointSupplyInventory(p, 1, {}, &wrongRecipe).status,
              InventorySearchStatus::Found);
    const auto dispersed = SearchJointSupplyInventory(p, 1);
    ASSERT_EQ(dispersed.status, InventorySearchStatus::Found);
    const auto dispersion = BuildJointSupplyExecutionIR(p, dispersed.plan);
    ExecutionIR::RequireComplete(dispersion.Replay(p), {1, 1, 1, 1});
    auto incomplete = dispersed.plan;
    incomplete.steps.pop_back();
    EXPECT_THROW((void)BuildJointSupplyExecutionIR(p, incomplete), ExecutionIR::Error);
    p.inputs = p.outputs;
    p.outputs = {{{{T::Quintessence, {0, 0}}}, {}}};
    const auto unificationRecipe = ChemicalPlanner::Plan(p, 1, false);
    EXPECT_EQ(unificationRecipe.unifications, 1U);
    EXPECT_EQ(unificationRecipe.dispersions, 0U);
    const auto constrainedUnification = SearchJointSupplyInventory(p, 1, {}, &unificationRecipe);
    ASSERT_EQ(constrainedUnification.status, InventorySearchStatus::Found);
    ExecutionIR::RequireComplete(
        BuildJointSupplyExecutionIR(p, constrainedUnification.plan).Replay(p), {1});
    wrongRecipe = unificationRecipe;
    ++wrongRecipe.unifications;
    EXPECT_NE(SearchJointSupplyInventory(p, 1, {}, &wrongRecipe).status,
              InventorySearchStatus::Found);
    const auto unified = SearchJointSupplyInventory(p, 1);
    ASSERT_EQ(unified.status, InventorySearchStatus::Found);
    ExecutionIR::RequireComplete(BuildJointSupplyExecutionIR(p, unified.plan).Replay(p), {1});
    incomplete = unified.plan;
    incomplete.steps.pop_back();
    EXPECT_THROW((void)BuildJointSupplyExecutionIR(p, incomplete), ExecutionIR::Error);
    p.permissionBits &= ~(1ULL << 18);
    EXPECT_NE(SearchJointSupplyInventory(p, 1).status, InventorySearchStatus::Found);
}

TEST(SupplyExecutionIR, CalcifiedAnimismusAuthorityBoundary) {
    for (const auto stage :
         {"shared-plan-calcify-animismus", "shared-plan-ordinary-port-access",
          "whole-feed-singleton-receivers", "shared-plan-ordinary-port-access:parent=012345",
          "shared-plan-wheel", "shared-plan-wheel:parent=012345",
          "portfolio-static-reaction-purification", "portfolio-static-reaction-ordinary-pair",
          "portfolio-singleton-purification-chain"}) {
        for (const auto mode : {io::VerificationMode::LegacyGate, io::VerificationMode::Shadow})
            EXPECT_TRUE(io::CalcifyAnimismusModeAllowed(stage, mode));
        EXPECT_FALSE(io::CalcifyAnimismusModeAllowed(stage, io::VerificationMode::Independent));
        EXPECT_TRUE(
            io::CalcifyAnimismusModeAllowed("shared-plan", io::VerificationMode::Independent));
        io::VerificationReport report;
        report.status = io::VerificationStatus::Passed;
        for (const auto authority :
             {io::VerificationAuthority::None, io::VerificationAuthority::Independent}) {
            report.authority = authority;
            EXPECT_EQ(io::RequireCalcifyAnimismusAuthority(stage, report).status,
                      io::VerificationStatus::Unavailable);
            EXPECT_TRUE(io::RequireCalcifyAnimismusAuthority("shared-plan", report).Passed());
        }
        report.authority = io::VerificationAuthority::LegacyGate;
        EXPECT_TRUE(io::RequireCalcifyAnimismusAuthority(stage, report).Passed());
    }
    EXPECT_TRUE(io::CalcifyAnimismusModeAllowed("shared-plan-serial-arms",
                                                io::VerificationMode::Independent));
    EXPECT_FALSE(io::RequiresCalcifyAnimismusNativeGate("shared-plan-ordinary-port-access-extra"));
    EXPECT_FALSE(io::RequiresCalcifyAnimismusNativeGate("shared-plan-ordinary-port-access:child="));
}
TEST(SupplyExecutionIR, CalcifiedAnimismusHasDistinctFreshIdentitiesAndClosedReservations) {
    const char* prior = std::getenv("OPUSOPTIMA_CALCIFY_ANIMISMUS");
    const bool present = prior;
    const std::string old = prior ? prior : "";
    setenv("OPUSOPTIMA_CALCIFY_ANIMISMUS", "1", 1);
    auto p = SupplyPuzzle();
    p.inputs = {{{{T::Earth, {0, 0}}}, {}}};
    p.outputs = {{{{T::Vitae, {0, 0}}}, {}}, {{{T::Mors, {0, 0}}}, {}}};
    const auto found = SearchJointSupplyInventory(p, 1);
    EXPECT_EQ(found.status, InventorySearchStatus::Found);
    if (found.status == InventorySearchStatus::Found) {
        const auto ir = BuildJointSupplyExecutionIR(p, found.plan);
        ExecutionIR::RequireComplete(ir.Replay(p), {1, 1});
        EXPECT_EQ(std::count_if(ir.steps.begin(), ir.steps.end(),
                                [](const auto& step) {
                                    return step.operation.kind == ExecutionIR::Kind::Calcify;
                                }),
                  2);
        auto denied = p;
        denied.permissionBits &= ~(1ULL << 12);
        EXPECT_THROW((void)BuildJointSupplyExecutionIR(denied, found.plan),
                     ExecutionIR::PermissionFailure);
        denied = p;
        denied.permissionBits &= ~(1ULL << 16);
        EXPECT_THROW((void)BuildJointSupplyExecutionIR(denied, found.plan),
                     ExecutionIR::PermissionFailure);
        denied = p;
        denied.inputs[0].atoms[0].type = T::Quicksilver;
        EXPECT_THROW((void)BuildJointSupplyExecutionIR(denied, found.plan), ExecutionIR::Error);
        for (const auto& step : ir.steps)
            if (step.operation.kind == ExecutionIR::Kind::Animismus) {
                ASSERT_EQ(step.operation.atoms.size(), 2u);
                EXPECT_NE(step.operation.atoms[0], step.operation.atoms[1]);
            }
        auto pending = found.plan;
        auto first = std::find_if(pending.steps.begin(), pending.steps.end(), [](const auto& step) {
            return step.supply.kind == SupplyPlan::Kind::Animismus;
        });
        ASSERT_NE(first, pending.steps.end());
        pending.steps.erase(first + 1, pending.steps.end());
        EXPECT_THROW((void)BuildJointSupplyExecutionIR(p, pending), ExecutionIR::Error);
        first->supply.conversion = ElementConversion::Projection;
        EXPECT_THROW((void)BuildJointSupplyExecutionIR(p, pending), ExecutionIR::Error);
        auto broken = found.plan;
        broken.steps.pop_back();
        EXPECT_THROW((void)BuildJointSupplyExecutionIR(p, broken), ExecutionIR::Error);
        unsetenv("OPUSOPTIMA_CALCIFY_ANIMISMUS");
        EXPECT_THROW((void)BuildJointSupplyExecutionIR(p, found.plan), ExecutionIR::Error);
        EXPECT_NE(SearchJointSupplyInventory(p, 1).status, InventorySearchStatus::Found);
    }
    if (present)
        setenv("OPUSOPTIMA_CALCIFY_ANIMISMUS", old.c_str(), 1);
    else
        unsetenv("OPUSOPTIMA_CALCIFY_ANIMISMUS");
}
TEST(SupplyExecutionIR, JointAnimismusPreservesExactCoproductAcrossPorts) {
    for (bool reversed : {false, true}) {
        auto p = SupplyPuzzle();
        p.permissionBits &= ~(1ULL << 17);
        p.inputs = {{{{T::Salt, {0, 0}}}, {}}};
        p.outputs = {{{{reversed ? T::Mors : T::Vitae, {0, 0}}}, {}},
                     {{{reversed ? T::Vitae : T::Mors, {0, 0}}}, {}}};
        const auto recipe = ChemicalPlanner::Plan(p, 1, false);
        ASSERT_EQ(recipe.animismus, 1U);
        const auto constrained = SearchJointSupplyInventory(p, 1, {}, &recipe);
        ASSERT_EQ(constrained.status, InventorySearchStatus::Found);
        EXPECT_TRUE(constrained.plan.recipeConstrained);
        ExecutionIR::RequireComplete(BuildJointSupplyExecutionIR(p, constrained.plan).Replay(p),
                                     {1, 1});
        auto wrongRecipe = recipe;
        wrongRecipe.animismus = 0;
        EXPECT_NE(SearchJointSupplyInventory(p, 1, {}, &wrongRecipe).status,
                  InventorySearchStatus::Found);
        wrongRecipe = recipe;
        ++wrongRecipe.animismus;
        EXPECT_NE(SearchJointSupplyInventory(p, 1, {}, &wrongRecipe).status,
                  InventorySearchStatus::Found);
        const auto searched = SearchJointSupplyInventory(p, 1);
        ASSERT_EQ(searched.status, InventorySearchStatus::Found);
        ASSERT_EQ(searched.plan.steps.size(), 3U);
        EXPECT_EQ(searched.plan.steps.back().supply.kind, SupplyPlan::Kind::Coproduct);
        const auto ir = BuildJointSupplyExecutionIR(p, searched.plan, {{{8, 0}, 0}, {{-8, 0}, 0}});
        const auto final = ir.Replay(p);
        ExecutionIR::RequireComplete(final, {1, 1});
        EXPECT_EQ(final.batches, (std::vector<std::size_t>{2}));
        auto invalid = searched.plan;
        invalid.steps.pop_back();
        EXPECT_THROW((void)BuildJointSupplyExecutionIR(p, invalid), ExecutionIR::Error);
        p.permissionBits &= ~(1ULL << 16);
        EXPECT_NE(SearchJointSupplyInventory(p, 1).status, InventorySearchStatus::Found);
    }
}

TEST(SupplyExecutionIR, JointAnimismusReservesMultipleCoproductsUntilLaterPorts) {
    for (bool reversed : {false, true}) {
        auto p = SupplyPuzzle();
        p.permissionBits &= ~(1ULL << 17);
        p.inputs = {{{{T::Salt, {0, 0}}}, {}}};
        p.outputs = {
            {{{reversed ? T::Mors : T::Vitae, {0, 0}}, {reversed ? T::Mors : T::Vitae, {1, 0}}},
             {{1, {0, 0}, {1, 0}}}},
            {{{reversed ? T::Vitae : T::Mors, {0, 0}}, {reversed ? T::Vitae : T::Mors, {1, 0}}},
             {{1, {0, 0}, {1, 0}}}}};
        for (const std::size_t products : {1U, 2U, 3U}) {
            const auto recipe = ChemicalPlanner::Plan(p, products, false);
            const auto searched = SearchJointSupplyInventory(p, products, {}, &recipe);
            ASSERT_EQ(searched.status, InventorySearchStatus::Found);
            const auto ir = BuildJointSupplyExecutionIR(p, searched.plan);
            const auto final = ir.Replay(p);
            ExecutionIR::RequireComplete(final, {products, products});
            EXPECT_EQ(final.batches, (std::vector<std::size_t>{4 * products}));
            EXPECT_EQ(std::count_if(searched.plan.steps.begin(), searched.plan.steps.end(),
                                    [](const auto& step) {
                                        return step.supply.kind == SupplyPlan::Kind::Coproduct;
                                    }),
                      2 * products);
            auto invalid = searched.plan;
            invalid.steps.pop_back();
            EXPECT_THROW((void)BuildJointSupplyExecutionIR(p, invalid), ExecutionIR::Error);
        }
    }
}

TEST(SupplyExecutionIR, JointPeriodSharesBatchesAndReplaysEveryOutputPort) {
    auto p = SupplyPuzzle();
    p.permissionBits &= ~(1ULL << 17); // No disposal may hide leftover atoms.
    p.outputs = {{{{T::Salt, {0, 0}}}, {}}, {{{T::Air, {0, 0}}}, {}}};
    for (const std::size_t products : {1U, 2U, 3U, 6U}) {
        const auto searched = SearchJointSupplyInventory(p, products);
        ASSERT_EQ(searched.status, InventorySearchStatus::Found);
        const auto ir = BuildJointSupplyExecutionIR(p, searched.plan, {{{8, 0}, 2}, {{-8, 0}, 4}});
        const auto final = ir.Replay(p);
        ExecutionIR::RequireComplete(final, {products, products});
        EXPECT_EQ(final.batches, (std::vector<std::size_t>{products}));
        EXPECT_THROW((void)BuildJointSupplyExecutionIR(p, searched.plan, {{}}), ExecutionIR::Error);
        auto invalid = searched.plan;
        for (auto& step : invalid.steps) {
            if (step.supply.kind == SupplyPlan::Kind::Take) {
                step.output = 2;
                break;
            }
        }
        EXPECT_THROW((void)BuildJointSupplyExecutionIR(p, invalid), ExecutionIR::Error);
        invalid = searched.plan;
        ++invalid.usage[0].batches;
        EXPECT_THROW((void)BuildJointSupplyExecutionIR(p, invalid), ExecutionIR::Error);
    }
}

TEST(SupplyExecutionIR, JointPeriodReplaysBufferedCoproductAndExactConversionRecipe) {
    auto p = SupplyPuzzle();
    p.permissionBits &= ~(1ULL << 17);
    p.inputs = {{{{T::Air, {0, 1}}, {T::Earth, {0, 0}}}, {{1, {0, 1}, {0, 0}}}}};
    p.outputs = {{{{T::Earth, {0, 0}}}, {}}, {{{T::Salt, {0, 0}}}, {}}};
    const auto recipe = ChemicalPlanner::Plan(p, 1, false);
    const auto searched = SearchJointSupplyInventory(p, 1, {}, &recipe);
    ASSERT_EQ(searched.status, InventorySearchStatus::Found);
    EXPECT_TRUE(searched.plan.recipeConstrained);
    auto buffered = searched.plan;
    ASSERT_EQ(buffered.steps.size(), 2U);
    auto store = buffered.steps.back();
    store.supply.kind = SupplyPlan::Kind::Store;
    store.supply.conversion = ElementConversion::None;
    store.supply.buffer = 0;
    buffered.steps.back().supply.buffer = 0;
    buffered.steps.insert(buffered.steps.begin(), store);
    const auto ir = BuildJointSupplyExecutionIR(p, buffered);
    ExecutionIR::RequireComplete(ir.Replay(p), {1, 1});
    EXPECT_EQ(ir.Replay(p).batches, (std::vector<std::size_t>{1}));
    auto invalid = recipe;
    invalid.calcifications = 0;
    EXPECT_NE(SearchJointSupplyInventory(p, 1, {}, &invalid).status, InventorySearchStatus::Found);
    InventorySearchLimits bounds;
    bounds.maxSteps = 1;
    EXPECT_EQ(SearchJointSupplyInventory(p, 1, bounds).status, InventorySearchStatus::Exhausted);
}

TEST(SupplyExecutionIR, PortFramesComposeAndInvertAcrossAllOrientations) {
    for (int outerTurns = -6; outerTurns < 12; ++outerTurns) {
        const core::HexFrame outer{{-7, 4}, outerTurns};
        for (int innerTurns = -6; innerTurns < 12; ++innerTurns) {
            const core::HexFrame inner{{3, -2}, innerTurns};
            const auto combined = outer.Compose(inner);
            for (const auto point :
                 {core::HexCoord{}, core::HexCoord{2, -5}, core::HexCoord{-8, 3}}) {
                EXPECT_EQ(combined.ToWorld(point), outer.ToWorld(inner.ToWorld(point)));
                EXPECT_EQ(outer.Inverse().ToWorld(outer.ToWorld(point)), point);
                EXPECT_EQ(outer.Inverse().ToWorld(point), outer.ToLocal(point));
                EXPECT_EQ(outer.Compose(outer.Inverse()).ToWorld(point), point);
                EXPECT_EQ(outer.Inverse().Compose(outer).ToWorld(point), point);
            }
            EXPECT_GE(combined.rotation, 0);
            EXPECT_LT(combined.rotation, 6);
        }
    }
}
TEST(SupplyExecutionIR, ReplaysActualPlannerSupplyAndAccountsWholeBatches) {
    auto p = SupplyPuzzle();
    const auto supply = AssemblyPlanner::PlanSupply(p);
    const auto ir = BuildSupplyExecutionIR(p, AssemblyPlanner::Plan(p), supply);
    const auto final = ir.Replay(p);
    ExecutionIR::RequireComplete(final, {supply.productsPerPeriod});
    EXPECT_EQ(final.batches[0], supply.usage[0].batches);
}
TEST(SupplyExecutionIR, OutputPortFramesPreserveLocalPuzzleAndRotateWholeAssembly) {
    const auto p = SupplyPuzzle();
    const auto supply = AssemblyPlanner::PlanSupply(p);
    const auto assembly = AssemblyPlanner::Plan(p);
    for (int rotation = 0; rotation < 6; ++rotation) {
        const core::HexFrame frame{{8, -3}, rotation};
        const auto ir = BuildSupplyExecutionIR(p, assembly, supply, frame);
        ExecutionIR::RequireComplete(ir.Replay(p), {supply.productsPerPeriod});
        std::size_t outputs = 0;
        for (const auto& step : ir.steps) {
            if (step.operation.kind != ExecutionIR::Kind::Output) continue;
            ++outputs;
            for (const auto& atom : p.outputs[0].atoms) {
                const auto world = frame.ToWorld(atom.position);
                EXPECT_EQ(frame.ToLocal(world), atom.position);
                EXPECT_TRUE(std::any_of(step.preconditions.atoms.begin(),
                                        step.preconditions.atoms.end(), [&](const auto& entry) {
                                            return entry.second.position == world &&
                                                   entry.second.type == atom.type;
                                        }));
            }
        }
        EXPECT_EQ(outputs, supply.productsPerPeriod);
    }
}
TEST(SupplyExecutionIR, InputFramesPreserveBatchBondsAndRejectWrongDimensions) {
    const auto p = SupplyPuzzle();
    const auto supply = AssemblyPlanner::PlanSupply(p);
    const auto assembly = AssemblyPlanner::Plan(p);
    for (int rotation = -6; rotation < 12; ++rotation) {
        const core::HexFrame frame{{-9, 3}, rotation};
        const auto ir = BuildSupplyExecutionIR(p, assembly, supply, {{8, 0}, 0}, {frame});
        const auto& feed = ir.steps.front().effects;
        EXPECT_EQ(feed.bonds.size(), p.inputs[0].bonds.size());
        for (std::size_t i = 0; i < p.inputs[0].atoms.size(); ++i)
            EXPECT_EQ(feed.atoms.at(i + 1).position, frame.ToWorld(p.inputs[0].atoms[i].position));
        ExecutionIR::RequireComplete(ir.Replay(p), {supply.productsPerPeriod});
    }
    EXPECT_THROW((void)BuildSupplyExecutionIR(p, assembly, supply, {}, {{}, {}}),
                 ExecutionIR::Error);
}
TEST(SupplyExecutionIR, RepeatedPhysicalAcquisitionUsesTheSameInputFrame) {
    auto p = SupplyPuzzle();
    p.inputs = {{{{T::Salt, {3, -2}}}, {}}};
    p.outputs = {{{{T::Salt, {0, 0}}}, {}}};
    using K = ExecutionIR::Kind;
    ExecutionIR ir;
    auto state = ExecutionIR::Initial(p);
    const core::HexFrame frame{{-2, -3}, 1};
    ASSERT_EQ(frame.ToWorld(p.inputs[0].atoms[0].position), (core::HexCoord{0, -2}));
    for (int batch = 0; batch < 2; ++batch) {
        const auto id = state.nextId;
        ir.Append(p, state, {K::Acquire, 0, {}, 0, frame.rotation, frame.origin});
        ir.Append(p, state, {K::Relocate, 0, {id}, 0, 0, {4, 0}});
        ir.Append(p, state, {K::Output, 0, {id}});
    }
    std::vector<core::HexCoord> track;
    for (int q = 0; q <= 4; ++q)
        track.push_back({q, -1});
    int spawns = 0;
    const auto inventory = LowerInventoryIR(
        p, ir, track, {0, 1, 4, 0},
        [](const auto&, const auto& from, const auto& to, char op, bool) {
            return from.direction == 4 && to.direction == 4 &&
                   (op == 'A' || op == 'a' || op == 'G' || op == 'g');
        },
        [&](const auto& step, const auto&) {
            if (step.operation.kind != K::Acquire) return true;
            ++spawns;
            return step.operation.rotation == frame.rotation &&
                   step.operation.translation == frame.origin &&
                   step.effects.atoms.at(step.preconditions.nextId).position ==
                       core::HexCoord{0, -2};
        },
        1, 20000, {2});
    EXPECT_EQ(spawns, 2);
    EXPECT_EQ(inventory.Material(), ir.Replay(p));
}
TEST(SupplyExecutionIR, PlannerSupplyLowersThroughCertifiedPhysicalInventory) {
    auto p = SupplyPuzzle();
    p.inputs = {{{{T::Air, {-4, 0}}}, {}}};
    p.outputs = {{{{T::Salt, {2, 0}}}, {}}};
    const auto supply = AssemblyPlanner::PlanSupply(p);
    const auto ir = BuildSupplyExecutionIR(p, AssemblyPlanner::Plan(p), supply);
    std::vector<core::HexCoord> track;
    for (int q = -4; q <= 2; ++q)
        track.push_back({q, 1});
    const auto inventory = LowerInventoryIR(
        p, ir, track, {0, 1, 4, 0},
        [](const auto&, const auto& from, const auto& to, char op, bool) {
            return from.direction == 4 && to.direction == 4 &&
                   (op == 'A' || op == 'a' || op == 'G' || op == 'g');
        },
        [](const auto& step, const auto&) {
            if (step.operation.kind == ExecutionIR::Kind::Calcify)
                return step.effects.atoms.at(step.preconditions.nextId).position ==
                       core::HexCoord{-4, 0};
            if (step.operation.kind == ExecutionIR::Kind::Output)
                return step.preconditions.atoms.begin()->second.position == core::HexCoord{2, 0};
            return true;
        },
        1, 20000, {supply.productsPerPeriod});
    EXPECT_EQ(inventory.Material(), ir.Replay(p));
    EXPECT_TRUE(inventory.Custody().Held().empty());
    EXPECT_TRUE(inventory.Custody().Parked().empty());
}
TEST(SupplyExecutionIR, RejectsEditedAccountingAndMissingRemainder) {
    auto p = SupplyPuzzle();
    auto supply = AssemblyPlanner::PlanSupply(p);
    ++supply.usage[0].batches;
    EXPECT_THROW((void)BuildSupplyExecutionIR(p, AssemblyPlanner::Plan(p), supply),
                 ExecutionIR::Error);
    p.outputs = {{{{T::Air, {0, 0}}}, {}}};
    supply = AssemblyPlanner::PlanSupply(p);
    std::erase_if(supply.steps, [](const auto& s) { return s.kind == SupplyPlan::Kind::Discard; });
    EXPECT_THROW((void)BuildSupplyExecutionIR(p, AssemblyPlanner::Plan(p), supply),
                 ExecutionIR::Error);
}
TEST(SupplyExecutionIR, MultistagePurificationReusesGeneratedMetals) {
    for (const auto target : {T::Iron, T::Copper, T::Silver, T::Gold}) {
        auto p = SupplyPuzzle();
        p.inputs = {{{{T::Lead, {0, 0}}}, {}}};
        p.outputs = {{{{target, {0, 0}}}, {}}};
        InventorySearchLimits limits;
        limits.maxStates = 256;
        limits.multiStagePurification = true;
        const auto found = SearchSupplyInventory(p, 1, limits);
        ASSERT_EQ(found.status, InventorySearchStatus::Found) << static_cast<int>(target);
        const auto count = std::size_t{1} << (static_cast<int>(T::Lead) - static_cast<int>(target));
        EXPECT_EQ(found.plan.usage[0].batches, count);
        EXPECT_EQ(
            std::count_if(found.plan.steps.begin(), found.plan.steps.end(),
                          [](const auto& step) { return step.kind == SupplyPlan::Kind::Purify; }),
            2 * (count - 1));
        const auto ir = BuildSupplyExecutionIR(p, AssemblyPlanner::Plan(p), found.plan);
        ExecutionIR::RequireComplete(ir.Replay(p), {1});
        auto invalid = found.plan;
        invalid.steps.back().retainProduct = true;
        EXPECT_THROW((void)BuildSupplyExecutionIR(p, AssemblyPlanner::Plan(p), invalid),
                     ExecutionIR::Error);
        invalid = found.plan;
        const auto first =
            std::find_if(invalid.steps.begin(), invalid.steps.end(),
                         [](const auto& step) { return step.kind == SupplyPlan::Kind::Purify; });
        ASSERT_NE(first, invalid.steps.end());
        first->retainProduct = !first->retainProduct;
        EXPECT_THROW((void)BuildSupplyExecutionIR(p, AssemblyPlanner::Plan(p), invalid),
                     ExecutionIR::Error);
        invalid = found.plan;
        invalid.steps.front().kind = SupplyPlan::Kind::Take;
        invalid.steps.front().retainProduct = true;
        EXPECT_THROW((void)BuildSupplyExecutionIR(p, AssemblyPlanner::Plan(p), invalid),
                     ExecutionIR::Error);
        auto recipe = ChemicalPlanner::Plan(p, 1, false);
        EXPECT_EQ(SearchSupplyInventory(p, 1, limits, &recipe).status,
                  InventorySearchStatus::Found);
        ++recipe.purifications;
        EXPECT_NE(SearchSupplyInventory(p, 1, limits, &recipe).status,
                  InventorySearchStatus::Found);
        limits.multiStagePurification = false;
        limits.singleStagePurification = true;
        EXPECT_NE(SearchSupplyInventory(p, 1, limits).status, InventorySearchStatus::Found);
        limits.multiStagePurification = true;
        p.permissionBits &= ~(1ULL << 15);
        EXPECT_NE(SearchSupplyInventory(p, 1, limits).status, InventorySearchStatus::Found);
    }
}

TEST(SupplyExecutionIR, ProjectionAndPurificationReplayExplicitChemistry) {
    for (bool purify : {false, true}) {
        auto p = SupplyPuzzle();
        p.inputs = {{{{T::Tin, {0, 0}}}, {}}};
        if (purify)
            p.inputs.push_back(p.inputs.front());
        else
            p.inputs.push_back({{{T::Quicksilver, {0, 0}}}, {}});
        p.outputs = {{{{T::Iron, {0, 0}}}, {}}};
        if (purify) {
            auto recipe = ChemicalPlanner::Plan(p, 1, false);
            InventorySearchLimits limits;
            limits.singleStagePurification = true;
            const auto found = SearchSupplyInventory(p, 1, limits, &recipe);
            ASSERT_EQ(found.status, InventorySearchStatus::Found);
            ExecutionIR::RequireComplete(
                BuildSupplyExecutionIR(p, AssemblyPlanner::Plan(p), found.plan).Replay(p), {1});
            recipe.purifications = 0;
            recipe.projections = 0;
            EXPECT_NE(SearchSupplyInventory(p, 1, limits, &recipe).status,
                      InventorySearchStatus::Found);
            recipe.purifications = 2;
            EXPECT_NE(SearchSupplyInventory(p, 1, limits, &recipe).status,
                      InventorySearchStatus::Found);
        } else {
            const auto recipe = ChemicalPlanner::Plan(p, 1, false);
            const auto found = SearchSupplyInventory(p, 1, {}, &recipe);
            ASSERT_EQ(found.status, InventorySearchStatus::Found);
            EXPECT_EQ(found.plan.usage[1].acquired, 1U);
            ExecutionIR::RequireComplete(
                BuildSupplyExecutionIR(p, AssemblyPlanner::Plan(p), found.plan).Replay(p), {1});
        }
        SupplyPlan supply;
        supply.usage = {{1, 1, 0}, {1, 1, 0}};
        if (purify) {
            supply.steps = {{SupplyPlan::Kind::Purify, 0, 0, 0},
                            {SupplyPlan::Kind::Purify, 1, 0, 0}};
        } else {
            supply.steps = {{SupplyPlan::Kind::Take, 0, 0, 0, ElementConversion::Projection},
                            {SupplyPlan::Kind::Reagent, 1, 0, 0}};
        }
        const auto ir = BuildSupplyExecutionIR(p, AssemblyPlanner::Plan(p), supply);
        ExecutionIR::RequireComplete(ir.Replay(p), {1});
        auto edited = supply;
        edited.steps.back().targetAtom = p.outputs[0].atoms.size();
        EXPECT_THROW((void)BuildSupplyExecutionIR(p, AssemblyPlanner::Plan(p), edited),
                     ExecutionIR::Error);
        edited = supply;
        edited.steps.insert(edited.steps.begin() + 1,
                            {SupplyPlan::Kind::Reagent, 0, 0, 0, ElementConversion::Duplication});
        EXPECT_THROW((void)BuildSupplyExecutionIR(p, AssemblyPlanner::Plan(p), edited),
                     ExecutionIR::Error);
    }
}
TEST(SupplyExecutionIR, DuplicationReturnsBorrowedCatalystAtPeriodBoundary) {
    auto p = SupplyPuzzle();
    p.inputs = {{{{T::Salt, {0, 0}}}, {}}, {{{T::Air, {0, 0}}}, {}}};
    p.outputs = {{{{T::Air, {0, 0}}, {T::Air, {1, 0}}}, {{1, {0, 0}, {1, 0}}}}};
    SupplyPlan supply;
    supply.usage = {{1, 1, 0}, {1, 1, 0}};
    supply.steps = {{SupplyPlan::Kind::Reagent, 1, 0, 1, ElementConversion::Duplication},
                    {SupplyPlan::Kind::Take, 0, 0, 0, ElementConversion::Duplication},
                    {SupplyPlan::Kind::Take, 1, 0, 1, ElementConversion::None, -2}};
    const auto ir = BuildSupplyExecutionIR(p, AssemblyPlanner::Plan(p), supply);
    ExecutionIR::RequireComplete(ir.Replay(p), {1});
    auto edited = supply;
    edited.steps.back().sourceAtom = 1;
    EXPECT_THROW((void)BuildSupplyExecutionIR(p, AssemblyPlanner::Plan(p), edited),
                 ExecutionIR::Error);
    const auto planned = AssemblyPlanner::PlanSupply(p);
    const auto plannedIR = BuildSupplyExecutionIR(p, AssemblyPlanner::Plan(p), planned);
    ExecutionIR::RequireComplete(plannedIR.Replay(p), {planned.productsPerPeriod});
}
TEST(SupplyExecutionIR, WholeBatchDuplicationCompletesMultiProductPeriodWithoutDisposal) {
    auto p = SupplyPuzzle();
    p.permissionBits &= ~(1ULL << 17);
    p.inputs = {{{{T::Salt, {0, 0}}, {T::Salt, {1, 0}}}, {{1, {0, 0}, {1, 0}}}},
                {{{T::Air, {0, 0}}}, {}}};
    p.outputs = {{{{T::Air, {0, 0}}, {T::Air, {1, 0}}}, {{1, {0, 0}, {1, 0}}}}};
    const auto supply = AssemblyPlanner::PlanSupply(p);
    ASSERT_GT(supply.productsPerPeriod, 1u);
    const auto ir = BuildSupplyExecutionIR(p, AssemblyPlanner::Plan(p), supply);
    const auto final = ir.Replay(p);
    ExecutionIR::RequireComplete(final, {supply.productsPerPeriod});
    EXPECT_EQ(final.discarded, 0u);
    for (std::size_t i = 0; i < p.inputs.size(); ++i)
        EXPECT_EQ(final.batches[i], supply.usage[i].batches);
}
TEST(SupplyExecutionIR, StoresAndRetrievesRealBatchRemainderWithoutRefilling) {
    auto p = SupplyPuzzle();
    SupplyPlan supply;
    supply.usage = {{1, 2, 0}};
    supply.steps = {{SupplyPlan::Kind::Store, 0, 0, 0, ElementConversion::None, 0},
                    {SupplyPlan::Kind::Take, 0, 1, 1},
                    {SupplyPlan::Kind::Take, 0, 0, 0, ElementConversion::None, 0}};
    const auto ir = BuildSupplyExecutionIR(p, AssemblyPlanner::Plan(p), supply);
    ExecutionIR::RequireComplete(ir.Replay(p), {1});
    supply.steps.back().sourceAtom = 1;
    EXPECT_THROW((void)BuildSupplyExecutionIR(p, AssemblyPlanner::Plan(p), supply),
                 ExecutionIR::Error);
}
TEST(SupplyExecutionIR, CalcificationUsesFreshMaterialIdentity) {
    auto p = SupplyPuzzle();
    p.inputs = {{{{T::Air, {0, 0}}}, {}}};
    p.outputs = {{{{T::Salt, {0, 0}}}, {}}};
    auto supply = AssemblyPlanner::PlanSupply(p);
    const auto ir = BuildSupplyExecutionIR(p, AssemblyPlanner::Plan(p), supply);
    EXPECT_TRUE(std::any_of(ir.steps.begin(), ir.steps.end(), [](const auto& s) {
        return s.operation.kind == ExecutionIR::Kind::Calcify;
    }));
    ExecutionIR::RequireComplete(ir.Replay(p), {supply.productsPerPeriod});
}
}} // namespace opusoptima::solvers
