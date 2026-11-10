#include "ExecutionPlan.hpp"
#include <gtest/gtest.h>

namespace opusoptima::solvers { namespace {
using K = ExecutionIR::Kind;
using T = core::AtomType;

TEST(ExecutionPlan, RecordsFragmentsBatchesMasksCachesAndDependencies) {
    io::PuzzleData p;
    p.version = 3;
    p.permissionBits = ~0ULL;
    p.inputs = {{{{T::Air, {0, 0}}, {T::Salt, {1, 0}}}, {{1, {0, 0}, {1, 0}}}}};
    p.outputs = {{{{T::Air, {0, 0}}}, {}}, {{{T::Salt, {0, 0}}}, {}}};
    ExecutionIR ir;
    auto state = ExecutionIR::Initial(p);
    ir.Append(p, state, {K::Acquire, 0, {}});
    ir.Append(p, state, {K::Unbond, 0, {1, 2}, 1});
    ir.Append(p, state, {K::Store, 7, {2}});
    ir.Append(p, state, {K::Relocate, 0, {1}, 0, 0, {4, -2}});
    ir.Append(p, state, {K::Output, 0, {1}});
    ir.Append(p, state, {K::Retrieve, 7, {}});
    ir.Append(p, state, {K::Output, 1, {2}});
    const auto plan = MakeExecutionPlan(p, std::move(ir));
    ExecutionIR::RequireComplete(plan.material.Replay(p), {1, 1});
    ASSERT_EQ(plan.tasks.size(), 7);
    ASSERT_EQ(plan.tasks[0].products.size(), 2);
    EXPECT_EQ(plan.origins.at(2).input, 0);
    EXPECT_EQ(plan.origins.at(2).batch, 1);
    EXPECT_EQ(plan.origins.at(2).sourceAtom, 1);
    ASSERT_EQ(plan.tasks[1].edges.size(), 1);
    EXPECT_EQ(plan.tasks[1].edges[0].retained, 0);
    EXPECT_EQ(plan.tasks[1].edges[0].removed, 1);
    ASSERT_EQ(plan.caches.size(), 1);
    EXPECT_EQ(plan.caches[0].fragment, (std::vector<ExecutionIR::AtomId>{2}));
    EXPECT_EQ(plan.caches[0].storedAt, 2);
    EXPECT_EQ(plan.caches[0].retrievedAt, 5);
    EXPECT_EQ(plan.tasks[5].dependsOn, (std::vector<std::size_t>{2}));
    EXPECT_EQ(plan.tasks[6].dependsOn, (std::vector<std::size_t>{5}));
    EXPECT_EQ(plan.tasks[4].output, 0);
    EXPECT_EQ(plan.tasks[6].output, 1);
    PhysicalPlan rail, fixed;
    rail.assignments.push_back({3, PhysicalPlan::Mode::Rail, {}, 0, {}, {}, {4, -2}, 0, 10, 20});
    fixed.assignments.push_back(
        {3, PhysicalPlan::Mode::FixedArm, {}, 1, {}, {}, {4, -2}, 0, 10, 20});
    EXPECT_EQ(rail.assignments[0].task, fixed.assignments[0].task);
    EXPECT_NE(rail.assignments[0].mode, fixed.assignments[0].mode);
}

TEST(ExecutionPlan, ReusedCacheSlotDependsOnPreviousRetrieval) {
    io::PuzzleData p;
    p.version = 3;
    p.permissionBits = ~0ULL;
    p.inputs = {{{{T::Air, {0, 0}}}, {}}, {{{T::Salt, {0, 0}}}, {}}};
    p.outputs = p.inputs;
    ExecutionIR ir;
    auto state = ExecutionIR::Initial(p);
    ir.Append(p, state, {K::Acquire, 0, {}});
    ir.Append(p, state, {K::Acquire, 1, {}});
    ir.Append(p, state, {K::Store, 0, {1}});
    ir.Append(p, state, {K::Retrieve, 0, {}});
    ir.Append(p, state, {K::Store, 0, {2}});
    ir.Append(p, state, {K::Retrieve, 0, {}});
    const auto plan = MakeExecutionPlan(p, std::move(ir));
    EXPECT_EQ(plan.tasks[4].dependsOn, (std::vector<std::size_t>{1, 3}));

    PhysicalPlan physical;
    physical.assignments = {{0, PhysicalPlan::Mode::FixedArm, {}, 0, {}, {}, {}, 0, 0, 1},
                            {1, PhysicalPlan::Mode::FixedArm, {}, 1, {}, {}, {}, 0, 0, 1},
                            {2, PhysicalPlan::Mode::FixedArm, {}, 2, {}, {}, {}, 0, 1, 2},
                            {3, PhysicalPlan::Mode::FixedArm, {}, 3, {}, {}, {}, 0, 10, 11},
                            {4, PhysicalPlan::Mode::FixedArm, {}, 4, {}, {}, {}, 0, 2, 3},
                            {5, PhysicalPlan::Mode::FixedArm, {}, 5, {}, {}, {}, 0, 20, 21}};
    EXPECT_THROW(ValidatePhysicalPlan(plan, physical), ExecutionIR::TimingFailure);
    physical.assignments[4].begin = 11;
    physical.assignments[4].end = 12;
    EXPECT_NO_THROW(ValidatePhysicalPlan(plan, physical));
}

TEST(ExecutionPlan, TracksCoproductOriginWithoutAssigningItAnInputBatch) {
    io::PuzzleData p;
    p.version = 3;
    p.permissionBits = ~0ULL;
    p.inputs = {{{{T::Salt, {0, 0}}}, {}}, {{{T::Salt, {0, 0}}}, {}}};
    p.outputs = {{{{T::Vitae, {0, 0}}}, {}}, {{{T::Mors, {0, 0}}}, {}}};
    ExecutionIR ir;
    auto state = ExecutionIR::Initial(p);
    ir.Append(p, state, {K::Acquire, 0, {}});
    ir.Append(p, state, {K::Acquire, 1, {}});
    ir.Append(p, state, {K::Animismus, 0, {1, 2}});
    ir.Append(p, state, {K::Output, 0, {3}});
    ir.Append(p, state, {K::Output, 1, {4}});
    const auto plan = MakeExecutionPlan(p, std::move(ir));
    EXPECT_FALSE(plan.origins.at(3).input.has_value());
    EXPECT_EQ(plan.origins.at(3).reagents, (std::vector<ExecutionIR::AtomId>{1, 2}));
    EXPECT_EQ(plan.origins.at(4).reagents, plan.origins.at(3).reagents);
    EXPECT_EQ(plan.tasks[2].dependsOn, (std::vector<std::size_t>{0, 1}));
    EXPECT_EQ(plan.tasks[3].dependsOn, (std::vector<std::size_t>{2}));
    EXPECT_EQ(plan.tasks[4].dependsOn, (std::vector<std::size_t>{2}));
}

TEST(ExecutionPlan, DuplicationRetainsCatalystAndTracksBothOutputs) {
    io::PuzzleData p;
    p.version = 3;
    p.permissionBits = ~0ULL;
    p.inputs = {{{{T::Salt, {0, 0}}}, {}}, {{{T::Air, {0, 0}}}, {}}};
    p.outputs = {{{{T::Air, {0, 0}}}, {}}, {{{T::Air, {0, 0}}}, {}}};
    ExecutionIR ir;
    auto state = ExecutionIR::Initial(p);
    ir.Append(p, state, {K::Acquire, 0, {}});
    ir.Append(p, state, {K::Acquire, 1, {}});
    ir.Append(p, state, {K::Duplicate, 0, {1, 2}});
    ir.Append(p, state, {K::Output, 0, {3}});
    ir.Append(p, state, {K::Output, 1, {2}});
    const auto plan = MakeExecutionPlan(p, std::move(ir));
    ExecutionIR::RequireComplete(plan.material.Replay(p), {1, 1});
    EXPECT_EQ(plan.tasks[2].consumedInputs, (std::vector<ExecutionIR::AtomId>{1}));
    EXPECT_EQ(plan.tasks[2].retainedInputs, (std::vector<ExecutionIR::AtomId>{2}));
    EXPECT_EQ(plan.tasks[2].products, (std::vector<ExecutionIR::AtomId>{3}));
    EXPECT_EQ(plan.tasks[4].dependsOn, (std::vector<std::size_t>{2}));
}

TEST(ExecutionPlan, PhysicalChoicesShareMaterialWithoutInvalidatingOrdering) {
    io::PuzzleData p;
    p.version = 3;
    p.permissionBits = ~0ULL;
    p.inputs = {{{{T::Air, {0, 0}}}, {}}};
    p.outputs = p.inputs;
    ExecutionIR ir;
    auto state = ExecutionIR::Initial(p);
    ir.Append(p, state, {K::Acquire, 0, {}});
    ir.Append(p, state, {K::Output, 0, {1}});
    const auto material = MakeExecutionPlan(p, std::move(ir));
    PhysicalPlan rail;
    rail.assignments = {{0, PhysicalPlan::Mode::Rail, {}, 0, 0, 1, {}, 0, 0, 1},
                        {1, PhysicalPlan::Mode::Rail, {}, 0, 0, {}, {}, 0, 1, 2}};
    EXPECT_NO_THROW(ValidatePhysicalPlan(material, rail));
    auto fixed = rail;
    for (auto& assignment : fixed.assignments) {
        assignment.mode = PhysicalPlan::Mode::FixedArm;
        assignment.arm = 1;
    }
    EXPECT_NO_THROW(ValidatePhysicalPlan(material, fixed));
    fixed.assignments[1].begin = 0;
    EXPECT_THROW(ValidatePhysicalPlan(material, fixed), ExecutionIR::TimingFailure);
    fixed = rail;
    fixed.assignments[1].port = 3;
    EXPECT_THROW(ValidatePhysicalPlan(material, fixed), ExecutionIR::LayoutFailure);
    fixed = rail;
    fixed.assignments[1].heldAtom = 1;
    EXPECT_THROW(ValidatePhysicalPlan(material, fixed), ExecutionIR::LayoutFailure);
    fixed.assignments.pop_back();
    EXPECT_THROW(ValidatePhysicalPlan(material, fixed), ExecutionIR::LayoutFailure);
}

TEST(ExecutionPlan, DistinguishesRepeatOutputOccurrencesAndInputBatches) {
    io::PuzzleData p;
    p.version = 3;
    p.permissionBits = ~0ULL;
    p.inputs = {{{{T::Salt, {0, 0}}}, {}}};
    p.outputs = p.inputs;
    ExecutionIR ir;
    auto state = ExecutionIR::Initial(p);
    for (int period = 0; period < 2; ++period) {
        const auto atom = state.nextId;
        ir.Append(p, state, {K::Acquire, 0, {}});
        ir.Append(p, state, {K::Output, 0, {atom}});
    }
    const auto plan = MakeExecutionPlan(p, std::move(ir));
    EXPECT_EQ(plan.tasks[1].output, 0U);
    EXPECT_EQ(plan.tasks[1].outputOccurrence, 0U);
    EXPECT_EQ(plan.tasks[3].output, 0U);
    EXPECT_EQ(plan.tasks[3].outputOccurrence, 1U);
    EXPECT_EQ(plan.origins.at(1).batch, 1U);
    EXPECT_EQ(plan.origins.at(2).batch, 2U);
    EXPECT_EQ(plan.tasks[2].dependsOn, (std::vector<std::size_t>{0}));
    EXPECT_EQ(plan.tasks[3].dependsOn, (std::vector<std::size_t>{1, 2}));
    ExecutionIR::RequireComplete(plan.material.Replay(p), {2});
}

TEST(ExecutionPlan, RejectsMutatedMaterialSnapshots) {
    io::PuzzleData p;
    p.version = 3;
    p.permissionBits = ~0ULL;
    p.inputs = {{{{T::Air, {0, 0}}}, {}}};
    p.outputs = p.inputs;
    ExecutionIR ir;
    auto state = ExecutionIR::Initial(p);
    ir.Append(p, state, {K::Acquire, 0, {}});
    ir.Append(p, state, {K::Output, 0, {1}});
    ir.steps[0].effects.atoms.at(1).type = T::Salt;
    EXPECT_THROW((void)MakeExecutionPlan(p, std::move(ir)), ExecutionIR::Error);
}
}} // namespace opusoptima::solvers
