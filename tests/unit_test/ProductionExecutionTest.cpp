#include "ProductionExecution.hpp"
#include "ExecutionPlan.hpp"
#include "HexFrame.hpp"
#include "InventorySearch.hpp"
#include "ProductionRouting.hpp"
#include "SupplyExecutionIR.hpp"

#include <algorithm>
#include <gtest/gtest.h>

namespace opusoptima::solvers { namespace {
using K = ExecutionIR::Kind;
using T = core::AtomType;

io::PuzzleData StaticProduction() {
    io::PuzzleData p;
    p.version = 3;
    p.name = "static-production-ledger";
    p.outputMultiplier = 1;
    p.permissionBits = ~0ULL;
    p.inputs = {{{{T::Salt, {0, 0}}}, {}}};
    p.outputs = p.inputs;
    p.productionInfo.emplace();
    p.productionInfo->chambers = {{{-10, 0}, "Large"}, {{10, 0}, "Large"}};
    p.productionInfo->conduits = {{{-10, 0}, {10, 0}, {{0, 0}}}};
    return p;
}
ProductionExecution::Transfer Handoff(const io::PuzzleData& p) {
    const auto planned = PlanProductionTransport(p);
    return {planned.handoff,
            planned.source.chamberIndex,
            planned.destination.chamberIndex,
            planned.handoff.releaseCycle + 1,
            {1}};
}

TEST(ProductionExecution, RetainsOriginalRulesAndTransfersExactlyOneMaterialIdentity) {
    auto p = StaticProduction();
    EXPECT_THROW((void)ExecutionIR::Initial(p), ExecutionIR::Unsupported);
    auto ledger = ProductionExecution(p);
    const auto placement = PlanProductionTransport(p);
    const auto source = placement.source.chamberIndex;
    const auto destination = placement.destination.chamberIndex;
    ledger.Apply(source, {K::Acquire, 0, {}, 0, placement.inputRotation, placement.inputPosition});
    EXPECT_EQ(ledger.Material().batches, (std::vector<std::size_t>{1}));
    const auto sourceCell =
        core::Add(placement.handoff.sourcePosition, placement.handoff.payload.atoms[0].position);
    ledger.Apply(source, {K::Relocate,
                          0,
                          {1},
                          0,
                          0,
                          core::Subtract(sourceCell, ledger.Material().atoms.at(1).position)});
    const auto transfer = Handoff(p);
    ledger.Release(transfer);
    EXPECT_EQ(ledger.InFlight(), 1U);
    EXPECT_TRUE(ledger.Rooms().empty());
    const auto before = ledger.Material();
    EXPECT_THROW(ledger.Apply(destination, {K::Output, 0, {1}}), ExecutionIR::LayoutFailure);
    EXPECT_EQ(ledger.Material(), before);
    EXPECT_THROW(ledger.Pickup(0, placement.handoff.pickupCycle), ExecutionIR::TimingFailure);
    EXPECT_THROW(ledger.Arrive(0, placement.handoff.releaseCycle), ExecutionIR::TimingFailure);
    ledger.Arrive(0, transfer.arrivalCycle);
    EXPECT_EQ(ledger.Material().atoms.at(1).type, T::Salt);
    EXPECT_THROW(ledger.Arrive(0, transfer.arrivalCycle), ExecutionIR::TimingFailure);
    EXPECT_THROW(ledger.Pickup(0, transfer.arrivalCycle), ExecutionIR::TimingFailure);
    ledger.Pickup(0, placement.handoff.pickupCycle);
    EXPECT_EQ(ledger.Rooms().at(1), destination);
    EXPECT_THROW(ledger.Pickup(0, placement.handoff.pickupCycle), ExecutionIR::TimingFailure);
    const auto outputCell = core::Add(
        placement.outputPosition,
        core::HexFrame{{}, placement.outputRotation}.ToWorld(p.outputs[0].atoms[0].position));
    ledger.Apply(destination, {K::Relocate,
                               0,
                               {1},
                               0,
                               0,
                               core::Subtract(outputCell, ledger.Material().atoms.at(1).position)});
    ledger.Apply(destination, {K::Output, 0, {1}});
    EXPECT_EQ(ledger.Material().outputs, (std::vector<std::size_t>{1}));
    EXPECT_EQ(ledger.Material().batches, (std::vector<std::size_t>{1}));
    EXPECT_EQ(ledger.Material().nextId, 2U);
    EXPECT_EQ(ledger.InFlight(), 0U);
    EXPECT_NO_THROW(ledger.RequireComplete({1}));
}

TEST(ProductionExecution, PlacesBondedReactionProductsRelativeToTheirPreservedPosition) {
    auto p = StaticProduction();
    p.productionInfo->chambers = {{{0, 0}, "Large"}};
    p.productionInfo->conduits.clear();
    p.inputs = {{{{T::Air, {0, 0}}, {T::Salt, {1, 0}}}, {{1, {0, 0}, {1, 0}}}}};
    p.outputs = {{{{T::Salt, {0, 0}}, {T::Salt, {1, 0}}}, {{1, {0, 0}, {1, 0}}}}};
    for (const int destinationQ : {1, 2}) {
        SCOPED_TRACE(destinationQ);
        ProductionExecution ledger(p);
        ledger.Apply(0, {K::Acquire, 0, {}, 0, 0, {1, 0}});
        ASSERT_NO_THROW(ledger.React(0, {K::Calcify, 0, {1}}, {{destinationQ, 0}}));
        EXPECT_FALSE(ledger.Material().atoms.contains(1));
        EXPECT_EQ(ledger.Material().atoms.at(3).type, T::Salt);
        EXPECT_EQ(ledger.Material().atoms.at(3).position, (core::HexCoord{destinationQ, 0}));
        EXPECT_EQ(ledger.Material().atoms.at(2).position, (core::HexCoord{destinationQ + 1, 0}));
        EXPECT_EQ(ledger.Material().bonds.at({2, 3}), 1);
        EXPECT_EQ(ledger.Rooms().at(2), 0U);
        EXPECT_EQ(ledger.Rooms().at(3), 0U);
        EXPECT_EQ(ledger.Program().Replay(p), ledger.Material());
    }
}

TEST(ProductionExecution, ValidatesExistingRigidTransportPlansOnOriginalPuzzle) {
    auto p = StaticProduction();
    EXPECT_NO_THROW(ValidateProductionTransportMaterial(p, PlanProductionTransport(p)));
    p.inputs[0].atoms[0].type = T::Air;
    EXPECT_NO_THROW(ValidateProductionTransportMaterial(p, PlanProductionTransport(p)));
    p.outputs[0].atoms[0].type = T::Salt;
    EXPECT_NO_THROW(ValidateProductionTransportMaterial(p, PlanProductionTransport(p)));
    p.inputs[0].atoms.push_back({T::Fire, {1, 0}});
    p.inputs[0].bonds.push_back({14, {0, 0}, {1, 0}});
    p.outputs = p.inputs;
    p.productionInfo->conduits[0].path.push_back({1, 0});
    EXPECT_NO_THROW(ValidateProductionTransportMaterial(p, PlanProductionTransport(p)));
}

TEST(ProductionExecution, ReplaysSerialRelayAcrossThreeRoomsWithOriginalMetadata) {
    auto p = StaticProduction();
    p.productionInfo->chambers = {{{-20, 0}, "Large"}, {{0, 0}, "Large"}, {{20, 0}, "Large"}};
    p.productionInfo->conduits = {{{-20, 0}, {0, 0}, {{0, 0}}}, {{0, 0}, {20, 0}, {{0, 0}}}};
    const auto plan = PlanProductionRoute(p);
    ASSERT_EQ(plan.handoffs.size(), 2U);
    EXPECT_NO_THROW(ValidateProductionRouteMaterial(p, plan));
    auto wrong = plan;
    wrong.handoffs[1].conduitIndex = wrong.handoffs[0].conduitIndex;
    EXPECT_THROW(ValidateProductionRouteMaterial(p, wrong), ExecutionIR::Error);
}

TEST(ProductionExecution, SharedSupplyPlannerClosesOriginalProductionReactionPeriods) {
    for (bool duplicate : {false, true}) {
        auto p = StaticProduction();
        p.permissionBits &= ~(1ULL << 17);
        p.productionInfo->conduits[0].path = {{0, 0}, {0, 1}};
        if (duplicate) {
            p.inputs = {{{{T::Salt, {0, 0}}, {T::Air, {0, 1}}}, {{1, {0, 0}, {0, 1}}}}};
            p.outputs = {{{{T::Air, {0, 0}}}, {}}, {{{T::Air, {0, 0}}}, {}}};
        } else {
            p.inputs = {{{{T::Quintessence, {0, 0}}, {T::Salt, {0, 1}}}, {{1, {0, 0}, {0, 1}}}}};
            p.outputs = {{{{T::Salt, {0, 0}}}, {}},
                         {{{T::Air, {0, 0}}}, {}},
                         {{{T::Earth, {0, 0}}}, {}},
                         {{{T::Fire, {0, 0}}}, {}},
                         {{{T::Water, {0, 0}}}, {}}};
        }
        const auto found = SearchJointSupplyInventory(p, 1, {512, 64, 16});
        ASSERT_EQ(found.status, InventorySearchStatus::Found) << "duplication=" << duplicate;
        EXPECT_EQ(found.plan.usage[0].batches, 1U);
        const auto material = MakeExecutionPlan(p, BuildJointSupplyExecutionIR(p, found.plan));
        ExecutionIR::RequireComplete(material.material.Replay(p),
                                     std::vector<std::size_t>(p.outputs.size(), 1));
        EXPECT_TRUE(
            std::any_of(material.tasks.begin(), material.tasks.end(), [&](const auto& task) {
                return task.kind == (duplicate ? K::Duplicate : K::Disperse);
            }));
        ChemicalRecipe recipe;
        recipe.inputBatches = {1};
        if (duplicate)
            recipe.duplications = 1;
        else
            recipe.dispersions = 1;
        const auto constrained = SearchJointSupplyInventory(p, 1, {512, 64, 16}, &recipe);
        ASSERT_EQ(constrained.status, InventorySearchStatus::Found) << "constrained=" << duplicate;
        const auto constrainedPlan =
            MakeExecutionPlan(p, BuildJointSupplyExecutionIR(p, constrained.plan));
        EXPECT_FALSE(std::any_of(
            constrainedPlan.tasks.begin(), constrainedPlan.tasks.end(), [](const auto& task) {
                return task.kind == K::Store || task.kind == K::Retrieve || task.kind == K::Discard;
            }));
        EXPECT_EQ(p.productionInfo->conduits.size(), 1U);
    }
}

TEST(ProductionExecution, SerializesRepeatedConduitCustodyAndReservesArrivalCells) {
    auto p = StaticProduction();
    const auto plan = PlanProductionTransport(p);
    ProductionExecution ledger(p);
    for (int period = 0; period < 2; ++period) {
        const auto id = ledger.Material().nextId;
        ledger.Apply(plan.source.chamberIndex,
                     {K::Acquire, 0, {}, 0, plan.inputRotation, plan.inputPosition});
        const auto source =
            core::Add(plan.handoff.sourcePosition, plan.handoff.payload.atoms[0].position);
        ledger.Apply(plan.source.chamberIndex,
                     {K::Relocate,
                      0,
                      {id},
                      0,
                      0,
                      core::Subtract(source, ledger.Material().atoms.at(id).position)});
        auto transfer = Handoff(p);
        transfer.atoms = {id};
        transfer.handoff.releaseCycle += period * 8;
        transfer.handoff.pickupCycle += period * 8;
        transfer.handoff.period += period * 8;
        transfer.arrivalCycle += period * 8;
        ledger.Release(transfer);
        EXPECT_THROW(ledger.Release(transfer), ExecutionIR::TimingFailure);
        ledger.Arrive(0, transfer.arrivalCycle);
        ledger.Pickup(0, transfer.handoff.pickupCycle);
        ledger.Apply(plan.destination.chamberIndex, {K::Output, 0, {id}});
    }
    EXPECT_EQ(ledger.Material().batches, (std::vector<std::size_t>{2}));
    EXPECT_NO_THROW(ledger.RequireComplete({2}));

    auto occupied = p;
    occupied.inputs.push_back({{{T::Salt, {0, 0}}}, {}});
    ProductionExecution arrival(occupied);
    arrival.Apply(plan.source.chamberIndex,
                  {K::Acquire, 0, {}, 0, plan.inputRotation, plan.inputPosition});
    const auto source =
        core::Add(plan.handoff.sourcePosition, plan.handoff.payload.atoms[0].position);
    arrival.Apply(plan.source.chamberIndex,
                  {K::Relocate,
                   0,
                   {1},
                   0,
                   0,
                   core::Subtract(source, arrival.Material().atoms.at(1).position)});
    arrival.Release(Handoff(p));
    const auto destination =
        core::Add(plan.handoff.destinationPosition, plan.handoff.payload.atoms[0].position);
    const auto before = arrival.Material();
    EXPECT_THROW(
        arrival.Apply(plan.destination.chamberIndex, {K::Acquire, 1, {}, 0, 0, destination}),
        ExecutionIR::LayoutFailure);
    EXPECT_EQ(arrival.Material(), before);
    EXPECT_EQ(arrival.InFlight(), 1U);
}

TEST(ProductionExecution, RejectsWrongEndpointShapePartialPayloadAndUnsupportedRulesAtomically) {
    auto p = StaticProduction();
    const auto plan = PlanProductionTransport(p);
    ProductionExecution ledger(p);
    const auto source = plan.source.chamberIndex;
    ledger.Apply(source, {K::Acquire, 0, {}, 0, plan.inputRotation, plan.inputPosition});
    const auto cell =
        core::Add(plan.handoff.sourcePosition, plan.handoff.payload.atoms[0].position);
    ledger.Apply(
        source,
        {K::Relocate, 0, {1}, 0, 0, core::Subtract(cell, ledger.Material().atoms.at(1).position)});
    auto transfer = Handoff(p);
    const auto before = ledger.Material();
    auto invalid = transfer;
    invalid.destinationRoom = source;
    EXPECT_THROW(ledger.Release(invalid), ExecutionIR::Error);
    invalid = transfer;
    invalid.handoff.localShape = {{1, 0}};
    EXPECT_THROW(ledger.Release(invalid), ExecutionIR::Error);
    invalid = transfer;
    invalid.atoms.push_back(2);
    EXPECT_THROW(ledger.Release(invalid), ExecutionIR::Error);
    invalid = transfer;
    invalid.handoff.payload.atoms[0].type = T::Air;
    EXPECT_THROW(ledger.Release(invalid), ExecutionIR::Error);
    invalid = transfer;
    invalid.handoff.destinationPosition = {90, 90};
    EXPECT_THROW(ledger.Release(invalid), ExecutionIR::Error);
    EXPECT_EQ(ledger.Material(), before);
    EXPECT_EQ(ledger.InFlight(), 0U);
    EXPECT_EQ(ledger.Rooms().at(1), source);
    p.productionInfo->vials.push_back({{}, true, 1});
    EXPECT_NO_THROW((void)ProductionExecution(p));
    p.productionInfo->vials[0].count = 65;
    EXPECT_THROW((void)ProductionExecution(p), std::invalid_argument);
    p.productionInfo->vials.clear();
    p.productionInfo->shrinkLeft = true;
    EXPECT_NO_THROW((void)ProductionExecution(p));
    p.productionInfo->shrinkLeft = false;
    p.version = 2;
    EXPECT_THROW((void)ProductionExecution(p), ExecutionIR::Unsupported);
    p.version = 3;
    p.outputMultiplier = 0;
    EXPECT_THROW((void)ProductionExecution(p), ExecutionIR::Unsupported);
}
}} // namespace opusoptima::solvers
