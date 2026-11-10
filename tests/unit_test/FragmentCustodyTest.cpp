#include "FragmentCustody.hpp"
#include <gtest/gtest.h>

namespace opusoptima::solvers { namespace {
using IR = ExecutionIR;
IR::State Material() {
    IR::State state;
    state.atoms = {{1, {core::AtomType::Salt, {0, 0}}}, {2, {core::AtomType::Air, {1, 0}}}};
    state.bonds = {{{1, 2}, 1}};
    return state;
}
TEST(FragmentCustody, TwoGripperHandoffRetainsReceiverAndRejectsParkingUntilReleased) {
    auto state = Material();
    FragmentCustody custody;
    custody.Grab(state, 0, {0, 0}, 1);
    custody.Grab(state, 1, {1, 0}, 2);
    EXPECT_THROW(custody.Park(state, 0, 3), IR::Error);
    EXPECT_EQ(custody.Held().size(), 2u);
    EXPECT_TRUE(custody.Parked().empty());
    custody.Drop(0);
    custody.Park(state, 1, 3);
    EXPECT_TRUE(custody.Held().empty());
    EXPECT_NO_THROW(custody.Validate(state));
    EXPECT_THROW(custody.Grab(state, 0, {0, 0}, 1), IR::Error);
    EXPECT_THROW(custody.Retrieve(state, 3, 0, {7, 0}, 1), IR::Error);
    EXPECT_EQ(custody.Parked().size(), 1u);
    custody.Retrieve(state, 3, 0, {0, 0}, 1);
    EXPECT_TRUE(custody.Parked().empty());
    EXPECT_EQ(custody.Held().at(0), 1u);
}
TEST(FragmentCustody, ParkingFreezesTypesCoordinatesAndBondLabels) {
    auto state = Material();
    FragmentCustody custody;
    custody.Grab(state, 0, {0, 0}, 1);
    custody.Park(state, 0, 0);
    auto changed = state;
    changed.atoms.at(2).position = {2, 0};
    EXPECT_THROW(custody.Validate(changed), IR::Error);
    changed = state;
    changed.atoms.at(2).type = core::AtomType::Fire;
    EXPECT_THROW(custody.Validate(changed), IR::Error);
    changed = state;
    changed.bonds.clear();
    EXPECT_THROW(custody.Validate(changed), IR::Error);
    changed = state;
    changed.bonds.at({1, 2}) = 14;
    EXPECT_THROW(custody.Validate(changed), IR::Error);
    changed = state;
    changed.atoms.erase(1);
    EXPECT_THROW(custody.Validate(changed), IR::Error);
    changed = state;
    changed.atoms.emplace(3, IR::Atom{core::AtomType::Salt, {2, 0}});
    changed.bonds.emplace(IR::Edge{2, 3}, 1);
    EXPECT_THROW(custody.Validate(changed), IR::Error);
    EXPECT_THROW(custody.Park(state, 0, 0), IR::Error);
    EXPECT_EQ(custody.Parked().size(), 1u);
    EXPECT_NO_THROW(custody.Validate(state));
}
TEST(FragmentCustody, UnbondingTracksActualGrabbedAtomsRatherThanOldFragment) {
    auto state = Material();
    FragmentCustody custody;
    custody.Grab(state, 0, {0, 0}, 1);
    custody.Grab(state, 1, {1, 0}, 2);
    state.bonds.clear();
    custody.Park(state, 0, 0);
    custody.Park(state, 1, 1);
    EXPECT_EQ(custody.Parked().at(0).atoms, (std::vector<IR::AtomId>{1}));
    EXPECT_EQ(custody.Parked().at(1).atoms, (std::vector<IR::AtomId>{2}));
    EXPECT_NO_THROW(custody.Validate(state));
}
TEST(FragmentCustody, RejectsOverlapConsumedHoldsAndInvalidContactsAtomically) {
    auto state = Material();
    FragmentCustody custody;
    EXPECT_THROW(custody.Grab(state, 0, {9, 0}, 1), IR::Error);
    EXPECT_TRUE(custody.Held().empty());
    custody.Grab(state, 0, {0, 0}, 1);
    EXPECT_THROW(custody.Grab(state, 0, {1, 0}, 2), IR::Error);
    auto changed = state;
    changed.atoms.at(2).position = {0, 0};
    EXPECT_THROW(custody.Validate(changed), IR::Error);
    changed = state;
    changed.atoms.erase(1);
    EXPECT_THROW(custody.Validate(changed), IR::Error);
    changed = state;
    changed.stored[0] = {1, 2};
    EXPECT_THROW(custody.Validate(changed), IR::Error);
    EXPECT_EQ(custody.Held().at(0), 1u);
    custody.Drop(0);
    EXPECT_THROW(custody.Drop(0), IR::Error);
}
TEST(FragmentCustody, SingleArmReservesCutStockAndSwitchesBetweenSlots) {
    auto state = Material();
    FragmentCustody custody;
    custody.Grab(state, 0, {0, 0}, 1);
    EXPECT_THROW(custody.ReserveLoose(state, 2, 1), IR::Error);
    state.bonds.clear();
    custody.ReserveLoose(state, 2, 1);
    EXPECT_THROW(custody.ReserveLoose(state, 2, 2), IR::Error);
    EXPECT_THROW(custody.ReserveLoose(state, 99, 2), IR::Error);
    custody.Park(state, 0, 0);
    custody.Retrieve(state, 1, 0, {1, 0}, 2);
    state.atoms.at(2).position = {4, 0};
    EXPECT_NO_THROW(custody.Validate(state));
    custody.Park(state, 0, 1);
    custody.Retrieve(state, 0, 0, {0, 0}, 1);
    EXPECT_EQ(custody.Held().at(0), 1u);
    EXPECT_EQ(custody.Parked().at(1).material.at(2).position, (core::HexCoord{4, 0}));
    auto changed = state;
    changed.atoms.at(2).position = {5, 0};
    EXPECT_THROW(custody.Validate(changed), IR::Error);
    EXPECT_NO_THROW(custody.Validate(state));
}
TEST(FragmentCustody, MergeRejectsUnrelatedLedgerChangesWithoutConsumingParking) {
    auto before = Material();
    before.bonds.clear();
    FragmentCustody custody;
    custody.Grab(before, 0, {0, 0}, 1);
    custody.ReserveLoose(before, 2, 7);
    auto after = before;
    after.bonds.emplace(IR::Edge{1, 2}, 1);
    auto changed = after;
    ++changed.nextId;
    EXPECT_THROW(custody.MergeParked(before, changed, 0, 7), IR::Error);
    changed = after;
    changed.outputs.push_back(1);
    EXPECT_THROW(custody.MergeParked(before, changed, 0, 7), IR::Error);
    EXPECT_EQ(custody.Parked().size(), 1u);
    EXPECT_NO_THROW(custody.Validate(before));
    custody.MergeParked(before, after, 0, 7);
    EXPECT_TRUE(custody.Parked().empty());
    EXPECT_NO_THROW(custody.Validate(after));
}
TEST(FragmentCustody, SingletonConsumptionRejectsInvalidChangesAtomically) {
    auto before = Material();
    before.bonds.clear();
    before.atoms.emplace(3, IR::Atom{core::AtomType::Fire, {3, 0}});
    FragmentCustody custody;
    custody.Grab(before, 0, {0, 0}, 1);
    custody.ReserveLoose(before, 2, 7);
    custody.ReserveLoose(before, 3, 8);
    auto after = before;
    after.atoms.erase(1);
    after.atoms.erase(2);
    EXPECT_THROW(custody.ConsumeSingletons(before, after, {1, 1}), IR::Error);
    EXPECT_THROW(custody.ConsumeSingletons(before, after, {1, 99}), IR::Error);
    EXPECT_THROW(custody.ConsumeSingletons(before, before, {1, 2}), IR::Error);
    auto changed = after;
    changed.atoms.at(3).type = core::AtomType::Salt;
    EXPECT_THROW(custody.ConsumeSingletons(before, changed, {1, 2}), IR::Error);
    changed = after;
    changed.stored[9] = {3};
    EXPECT_THROW(custody.ConsumeSingletons(before, changed, {1, 2}), IR::Error);
    EXPECT_EQ(custody.Held().at(0), 1u);
    EXPECT_EQ(custody.Parked().size(), 2u);
    EXPECT_NO_THROW(custody.Validate(before));
    custody.ConsumeSingletons(before, after, {1, 2});
    EXPECT_TRUE(custody.Held().empty());
    ASSERT_EQ(custody.Parked().size(), 1u);
    EXPECT_EQ(custody.Parked().at(8).atoms, (std::vector<IR::AtomId>{3}));
    EXPECT_NO_THROW(custody.Validate(after));

    auto bonded = Material();
    FragmentCustody held;
    held.Grab(bonded, 0, {0, 0}, 1);
    EXPECT_THROW(held.ConsumeSingletons(bonded, after, {1, 2}), IR::Error);
    EXPECT_EQ(held.Held().at(0), 1u);
    EXPECT_NO_THROW(held.Validate(bonded));
}
TEST(FragmentCustody, PhysicalAndLogicalStorageMustBothReleaseBeforeGrab) {
    auto state = Material();
    FragmentCustody custody;
    custody.Grab(state, 0, {0, 0}, 1);
    custody.Park(state, 0, 4);
    state.stored[7] = {1, 2};
    EXPECT_NO_THROW(custody.Validate(state));
    EXPECT_THROW(custody.Retrieve(state, 4, 0, {0, 0}, 1), IR::Error);
    EXPECT_EQ(custody.Parked().size(), 1u);
    EXPECT_TRUE(custody.Held().empty());
    state.stored.clear();
    custody.Retrieve(state, 4, 0, {1, 0}, 2);
    EXPECT_EQ(custody.Held().at(0), 2u);
}
}} // namespace opusoptima::solvers
