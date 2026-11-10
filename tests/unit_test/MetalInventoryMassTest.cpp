#include "InventorySearch.hpp"
#include "SupplyExecutionIR.hpp"
#include <gtest/gtest.h>

namespace opusoptima::solvers { namespace {
io::PuzzleData MetalFixture(int atoms, bool disposal) {
    io::PuzzleData puzzle;
    puzzle.version=3;puzzle.name="closed metal inventory";puzzle.outputMultiplier=1;
    puzzle.permissionBits=((1ULL<<32)-1)&~(1ULL<<17);
    if(disposal) puzzle.permissionBits|=1ULL<<17;
    puzzle.inputs.resize(1);
    for(int q=0;q<atoms;++q) {
        puzzle.inputs[0].atoms.push_back({core::AtomType::Lead,{q,0}});
        if(q) puzzle.inputs[0].bonds.push_back({1,{q-1,0},{q,0}});
    }
    puzzle.outputs={{{{core::AtomType::Tin,{0,0}}},{}}};
    return puzzle;
}

void RequireClosed(const io::PuzzleData& puzzle,const SupplyPlan& supply,std::size_t products) {
    JointSupplyPlan joint;
    joint.usage=supply.usage;joint.productsPerPeriod=supply.productsPerPeriod;
    joint.recipeConstrained=supply.recipeConstrained;
    for(const auto& step:supply.steps) joint.steps.push_back({step,0});
    const auto material=BuildJointSupplyExecutionIR(puzzle,joint);
    EXPECT_NO_THROW(ExecutionIR::RequireComplete(material.Replay(puzzle),{products}));
}

TEST(MetalInventoryMassTest, RejectsExcessWholeBatchWithoutExhaustingSearch) {
    const auto puzzle=MetalFixture(3,false);
    InventorySearchLimits limits{16,64,16};limits.singleStagePurification=true;
    const auto result=SearchSupplyInventory(puzzle,1,limits);
    EXPECT_EQ(result.status,InventorySearchStatus::Rejected);
    EXPECT_LT(result.states,limits.maxStates);
}

TEST(MetalInventoryMassTest, PreservesExactBatchRecipesAcrossOutputPeriods) {
    const auto puzzle=MetalFixture(2,false);
    for(std::size_t products:{1U,2U,3U}) {
        SCOPED_TRACE(products);
        InventorySearchLimits limits{2048,64,64};limits.singleStagePurification=true;
        ChemicalRecipe recipe;recipe.inputBatches={products};recipe.purifications=products;
        const auto result=SearchSupplyInventory(puzzle,products,limits,&recipe);
        ASSERT_EQ(result.status,InventorySearchStatus::Found);
        ASSERT_EQ(result.plan.usage.size(),1U);
        EXPECT_EQ(result.plan.usage[0].batches,products);
        EXPECT_EQ(result.plan.usage[0].acquired,2*products);
        RequireClosed(puzzle,result.plan,products);
    }
}

TEST(MetalInventoryMassTest, PermittedDisposalAllowsExcessSourceMass) {
    const auto puzzle=MetalFixture(3,true);
    InventorySearchLimits limits{2048,64,64};limits.singleStagePurification=true;
    const auto result=SearchSupplyInventory(puzzle,1,limits);
    ASSERT_EQ(result.status,InventorySearchStatus::Found);
    RequireClosed(puzzle,result.plan,1);
}
} }
