#include "PhysicalInventory.hpp"
#include <algorithm>
#include <gtest/gtest.h>

namespace opusoptima::solvers { namespace {
using IR = ExecutionIR;
IR::State Stock() {
    IR::State state;
    state.atoms = {{1, {core::AtomType::Salt, {0, 0}}}, {2, {core::AtomType::Air, {2, 0}}}};
    return state;
}
PhysicalInventory Inventory(bool permit = true) {
    return {Stock(),
            {{0, 1}, {1, 1}, {2, 1}, {3, 1}, {4, 1}},
            {0, 1, 4, 0},
            [permit](const auto&, const auto& from, const auto& to, char op, bool) {
                return permit && (op == 'A' || op == 'a' || op == 'G' || op == 'g') &&
                       from.direction == 4 && to.direction == 4 && from.length == 1 &&
                       to.length == 1;
            }};
}
TEST(PhysicalInventory, DeviceWheelConversionRequiresPermissionAndAtomicGlyphAcceptance) {
    IR::State stock;
    stock.nextId = 3;
    stock.atoms = {{1, {core::AtomType::Salt, {0, 0}}}, {2, {core::AtomType::Salt, {1, 0}}}};
    stock.bonds = {{{1, 2}, 1}};
    PhysicalInventory inventory(
        stock, {{0, 1}}, {0, 1, 4, 0},
        [](const auto&, const auto&, const auto&, char, bool) { return true; });
    inventory.ReserveLoose(1, 0);
    inventory.Retrieve(0, 1);
    io::PuzzleData puzzle;
    puzzle.permissionBits = (1ULL << 13) | (1ULL << 28);
    const IR::WheelCatalyst wheel{7, core::AtomType::Water, 2};
    const auto before = inventory.Material();
    const auto tape = inventory.Tape();
    EXPECT_THROW(inventory.DuplicateWheel(puzzle, wheel, {}), IR::Error);
    EXPECT_THROW(
        inventory.DuplicateWheel(puzzle, wheel, [](const auto&, const auto&) { return false; }),
        IR::Error);
    EXPECT_EQ(inventory.Material(), before);
    EXPECT_EQ(inventory.Tape(), tape);
    auto forbidden = puzzle;
    forbidden.permissionBits &= ~(1ULL << 28);
    EXPECT_THROW(
        inventory.DuplicateWheel(forbidden, wheel, [](const auto&, const auto&) { return true; }),
        IR::Error);
    inventory.DuplicateWheel(puzzle, wheel, [](const auto&, const auto&) { return true; });
    EXPECT_FALSE(inventory.Material().atoms.contains(1));
    EXPECT_EQ(inventory.Material().atoms.at(3).type, core::AtomType::Water);
    EXPECT_EQ(inventory.Material().atoms.at(3).position, (core::HexCoord{0, 0}));
    EXPECT_TRUE(inventory.Material().bonds.contains({2, 3}));
    EXPECT_EQ(inventory.Material().wheelDevices.at(7), 2);
    EXPECT_EQ(inventory.Custody().Held().at(0), 3);
    inventory.Custody().Validate(inventory.Material());
}

TEST(PhysicalInventory, ReactionReleaseCommitsAtomicallyAndCertifiesDrop) {
    auto stock = Stock();
    stock.nextId = 3;
    stock.atoms.at(2).type = core::AtomType::Salt;
    io::PuzzleData puzzle;
    puzzle.permissionBits = 1ULL << 16;
    bool allowDrop = false;
    PhysicalInventory inventory(stock, {{0, 1}}, {0, 1, 4, 0},
                                [&](const auto&, const auto&, const auto&, char op, bool) {
                                    return op != 'g' || allowDrop;
                                });
    inventory.ReserveLoose(1, 0);
    inventory.ReserveLoose(2, 1);
    inventory.Retrieve(0, 1);
    const auto before = inventory.Tape();
    const IR::Operation op{IR::Kind::Animismus, 0, {1, 2}};
    const std::vector<core::HexCoord> cells{{0, 2}, {1, 2}};
    const auto permit = [](const auto&, const auto&) { return true; };
    EXPECT_THROW(inventory.ReactOnRelease(2, puzzle, op, cells, permit), IR::Error);
    allowDrop = true;
    EXPECT_THROW(inventory.ReactOnRelease(1, puzzle, op, cells, permit), IR::Error);
    EXPECT_THROW(inventory.ReactOnRelease(2, puzzle, op, cells,
                                          [](const auto&, const auto&) { return false; }),
                 IR::Error);
    EXPECT_THROW(inventory.ReactOnRelease(2, puzzle, op, {{0, 2}}, permit), IR::Error);
    EXPECT_EQ(inventory.Material(), stock);
    EXPECT_EQ(inventory.Tape(), before);
    EXPECT_EQ(inventory.Custody().Held().at(0), 1U);
    EXPECT_EQ(inventory.Custody().Parked().size(), 1U);
    inventory.ReactOnRelease(2, puzzle, op, cells, permit);
    EXPECT_EQ(inventory.Tape().size(), before.size() + 1);
    EXPECT_EQ(inventory.Tape().back(), 'g');
    EXPECT_TRUE(inventory.Custody().Held().empty());
    EXPECT_TRUE(inventory.Custody().Parked().empty());
    EXPECT_EQ(inventory.Material().atoms.at(3).type, core::AtomType::Vitae);
    EXPECT_EQ(inventory.Material().atoms.at(4).type, core::AtomType::Mors);
    inventory.Custody().Validate(inventory.Material());
}
TEST(PhysicalInventory, ReleasedCoproductsRequireCustodyCellsAndCertification) {
    IR::State stock;
    stock.nextId = 4;
    stock.atoms = {{1, {core::AtomType::Salt, {0, 0}}},
                   {2, {core::AtomType::Salt, {1, 0}}},
                   {3, {core::AtomType::Air, {4, 0}}}};
    PhysicalInventory inventory(
        stock, {{0, 2}}, {0, 1, 4, 0},
        [](const auto&, const auto&, const auto&, char, bool) { return true; });
    inventory.ReserveLoose(1, 7);
    inventory.ReserveLoose(3, 9);
    io::PuzzleData puzzle;
    puzzle.permissionBits = 1ULL << 16;
    const IR::Operation operation{IR::Kind::Animismus, 0, {1, 2}};
    const std::vector<core::HexCoord> cells{{0, 1}, {1, -1}};
    const auto permit = [](const auto&, const auto&) { return true; };
    EXPECT_THROW(inventory.ReactReleased(puzzle, operation, cells, permit), IR::Error);
    inventory.ReserveLoose(2, 8);
    EXPECT_THROW(inventory.ReactReleased(puzzle, operation, cells, {}), IR::Error);
    EXPECT_THROW(inventory.ReactReleased(puzzle, operation, {{0, 1}}, permit), IR::Error);
    EXPECT_THROW(inventory.ReactReleased(puzzle, operation, {{4, 0}, {1, -1}}, permit), IR::Error);
    EXPECT_THROW(inventory.ReactReleased(puzzle, operation, {{0, 1}, {0, 1}}, permit), IR::Error);
    EXPECT_THROW(inventory.ReactReleased(puzzle, operation, cells,
                                         [](const auto&, const auto&) { return false; }),
                 IR::Error);
    EXPECT_EQ(inventory.Material(), stock);
    EXPECT_TRUE(inventory.Tape().empty());
    EXPECT_EQ(inventory.Custody().Parked().size(), 3U);
    inventory.ReactReleased(puzzle, operation, cells, [](const auto& step, const auto&) {
        return step.effects.atoms.at(4).position == core::HexCoord{0, 1} &&
               step.effects.atoms.at(5).position == core::HexCoord{1, -1};
    });
    EXPECT_EQ(inventory.Material().atoms.at(4).type, core::AtomType::Vitae);
    EXPECT_EQ(inventory.Material().atoms.at(5).type, core::AtomType::Mors);
    EXPECT_TRUE(inventory.Custody().Held().empty());
    EXPECT_EQ(inventory.Custody().Parked().size(), 1U);
    EXPECT_EQ(inventory.Custody().Parked().at(9).material.at(3), stock.atoms.at(3));
    inventory.ReserveLoose(4, 7);
    inventory.ReserveLoose(5, 8);
    inventory.Custody().Validate(inventory.Material());
    EXPECT_EQ(inventory.Tape(), (std::vector<char>{' '}));
}
TEST(PhysicalInventory, ReleasedReactionSupportsPurificationUnificationAndDispersion) {
    using T = core::AtomType;
    struct Recipe {
        IR::Kind kind;
        int permission;
        std::vector<T> inputs, outputs;
    };
    for (const auto& recipe : std::vector<Recipe>{
             {IR::Kind::Purify, 15, {T::Lead, T::Lead}, {T::Tin}},
             {IR::Kind::Unify, 18, {T::Air, T::Earth, T::Fire, T::Water}, {T::Quintessence}},
             {IR::Kind::Disperse, 18, {T::Quintessence}, {T::Air, T::Earth, T::Fire, T::Water}}}) {
        IR::State stock;
        IR::Operation operation{recipe.kind};
        for (std::size_t i = 0; i < recipe.inputs.size(); ++i) {
            const auto id = stock.nextId++;
            stock.atoms.emplace(id, IR::Atom{recipe.inputs[i], {static_cast<int>(i), 0}});
            operation.atoms.push_back(id);
        }
        PhysicalInventory inventory(
            stock, {{0, 2}}, {0, 1, 4, 0},
            [](const auto&, const auto&, const auto&, char, bool) { return true; });
        for (auto id : operation.atoms)
            inventory.ReserveLoose(id, id);
        std::vector<core::HexCoord> cells;
        for (std::size_t i = 0; i < recipe.outputs.size(); ++i)
            cells.push_back({static_cast<int>(i), 1});
        io::PuzzleData puzzle;
        const auto permit = [](const auto&, const auto&) { return true; };
        EXPECT_THROW(inventory.ReactReleased(puzzle, operation, cells, permit), IR::Error);
        puzzle.permissionBits = 1ULL << recipe.permission;
        inventory.ReactReleased(puzzle, operation, cells, permit);
        ASSERT_EQ(inventory.Material().atoms.size(), recipe.outputs.size());
        for (std::size_t i = 0; i < recipe.outputs.size(); ++i) {
            EXPECT_EQ(inventory.Material().atoms.at(stock.nextId + i).type, recipe.outputs[i]);
            EXPECT_EQ(inventory.Material().atoms.at(stock.nextId + i).position, cells[i]);
        }
        EXPECT_TRUE(inventory.Custody().Parked().empty());
        inventory.Custody().Validate(inventory.Material());
    }
}
TEST(PhysicalInventory, DuplicationRetainsCatalystAndRejectsUncertifiedTransition) {
    auto stock = Stock();
    stock.nextId = 3;
    auto inventory =
        PhysicalInventory(stock, {{0, 1}}, {0, 1, 4, 0},
                          [](const auto&, const auto&, const auto&, char, bool) { return true; });
    inventory.ReserveLoose(2, 7);
    inventory.Grab(1);
    io::PuzzleData puzzle;
    const auto before = inventory.Material();
    const auto tape = inventory.Tape();
    const auto permit = [](const auto&, const auto&) { return true; };
    EXPECT_THROW(inventory.Duplicate(puzzle, 2, permit), IR::Error);
    puzzle.permissionBits = 1ULL << 13;
    EXPECT_THROW(inventory.Duplicate(puzzle, 2, {}), IR::Error);
    EXPECT_THROW(inventory.Duplicate(puzzle, 1, permit), IR::Error);
    EXPECT_THROW(inventory.Duplicate(puzzle, 2, [](const auto&, const auto&) { return false; }),
                 IR::Error);
    EXPECT_EQ(inventory.Material(), before);
    EXPECT_EQ(inventory.Tape(), tape);
    EXPECT_EQ(inventory.Custody().Held().at(0), 1U);
    inventory.Duplicate(puzzle, 2, [](const auto& step, const auto&) {
        return step.effects.atoms.at(3).position == core::HexCoord{0, 0};
    });
    EXPECT_EQ(inventory.Material().atoms.at(2), before.atoms.at(2));
    EXPECT_EQ(inventory.Custody().Parked().at(7).atoms, (std::vector<IR::AtomId>{2}));
    EXPECT_EQ(inventory.Material().atoms.at(3).type, core::AtomType::Air);
    EXPECT_EQ(inventory.Custody().Held().at(0), 3U);
    EXPECT_FALSE(inventory.Material().atoms.contains(1));
}
TEST(PhysicalInventory, ParkedDuplicationPreservesWorldFrameAndRollsBackRejectedContacts) {
    auto stock = Stock();
    stock.nextId = 3;
    stock.atoms.at(1).position = {3, 0};
    stock.atoms.at(2).position = {5, 0};
    PhysicalInventory inventory(stock, {{2, 1}, {3, 1}, {4, 1}}, {0, 1, 4, 0},
                                [](const auto&, const auto& from, const auto& to, char op, bool) {
                                    return from.direction == 4 && to.direction == 4 &&
                                           from.length == 1 && to.length == 1 &&
                                           (op == 'A' || op == 'a' || op == 'G' || op == 'g');
                                });
    inventory.ReserveLoose(1, 7);
    inventory.ReserveLoose(2, 8);
    io::PuzzleData puzzle;
    puzzle.permissionBits = 1ULL << 13;
    const auto permit = [](const auto&, const auto&) { return true; };
    const auto arm = inventory.Arm();
    auto unchanged = [&]() {
        EXPECT_EQ(inventory.Material(), stock);
        EXPECT_TRUE(inventory.Tape().empty());
        EXPECT_EQ(inventory.Arm(), arm);
        EXPECT_TRUE(inventory.Custody().Held().empty());
        EXPECT_EQ(inventory.Custody().Parked().at(7).material.at(1), stock.atoms.at(1));
        EXPECT_EQ(inventory.Custody().Parked().at(8).material.at(2), stock.atoms.at(2));
    };
    EXPECT_NE(inventory.DuplicateParked(8, puzzle, 1, permit), MotionStatus::Found);
    unchanged();
    EXPECT_NE(
        inventory.DuplicateParked(7, puzzle, 2, [](const auto&, const auto&) { return false; }),
        MotionStatus::Found);
    unchanged();
    EXPECT_NE(inventory.DuplicateParked(7, puzzle, 2, permit, 0), MotionStatus::Found);
    unchanged();
    ASSERT_EQ(inventory.DuplicateParked(7, puzzle, 2,
                                        [](const auto& step, const auto&) {
                                            return step.effects.atoms.at(3).position ==
                                                   core::HexCoord{3, 0};
                                        }),
              MotionStatus::Found);
    EXPECT_FALSE(inventory.Custody().Parked().contains(7));
    EXPECT_EQ(inventory.Custody().Parked().at(8).material.at(2), stock.atoms.at(2));
    EXPECT_EQ(inventory.Custody().Held().at(0), 3U);
    EXPECT_EQ(inventory.Material().atoms.at(3).type, core::AtomType::Air);
    ASSERT_EQ(inventory.MoveHeld({{4, 0}, 0}), MotionStatus::Found);
    EXPECT_EQ(inventory.Material().atoms.at(3).position, (core::HexCoord{4, 0}));
    inventory.Custody().Validate(inventory.Material());
}
TEST(PhysicalInventory, ProjectionConsumesOnlyPartnerCustodyAndRollsBackRejection) {
    IR::State stock;
    stock.nextId = 4;
    stock.atoms = {{1, {core::AtomType::Lead, {3, 0}}},
                   {2, {core::AtomType::Quicksilver, {4, 0}}},
                   {3, {core::AtomType::Air, {6, 0}}}};
    PhysicalInventory inventory(stock, {{2, 1}, {3, 1}, {4, 1}}, {0, 1, 4, 0},
                                [](const auto&, const auto& from, const auto& to, char op, bool) {
                                    return from.direction == 4 && to.direction == 4 &&
                                           from.length == 1 && to.length == 1 &&
                                           (op == 'A' || op == 'a' || op == 'G' || op == 'g');
                                });
    inventory.ReserveLoose(1, 7);
    inventory.ReserveLoose(2, 8);
    inventory.ReserveLoose(3, 9);
    io::PuzzleData puzzle;
    puzzle.permissionBits = 1ULL << 14;
    const IR::Operation operation{IR::Kind::Project, 0, {1, 2}};
    const auto arm = inventory.Arm();
    const auto permit = [](const auto&, const auto&) { return true; };
    EXPECT_THROW(inventory.ReactHeld(puzzle, operation, permit), IR::Error);
    EXPECT_NE(inventory.ReactParked(7, puzzle, operation, {}), MotionStatus::Found);
    auto forbidden = puzzle;
    forbidden.permissionBits = 0;
    EXPECT_NE(inventory.ReactParked(7, forbidden, operation, permit), MotionStatus::Found);
    EXPECT_NE(inventory.ReactParked(8, puzzle, operation, permit), MotionStatus::Found);
    EXPECT_NE(inventory.ReactParked(7, puzzle, {IR::Kind::Project, 0, {1}}, permit),
              MotionStatus::Found);
    EXPECT_NE(inventory.ReactParked(7, puzzle, {IR::Kind::Project, 0, {1, 3}}, permit),
              MotionStatus::Found);
    EXPECT_NE(inventory.ReactParked(7, puzzle, operation, permit, 0), MotionStatus::Found);
    EXPECT_NE(
        inventory.ReactParked(7, puzzle, operation, [](const auto&, const auto&) { return false; }),
        MotionStatus::Found);
    EXPECT_EQ(inventory.Material(), stock);
    EXPECT_EQ(inventory.Arm(), arm);
    EXPECT_TRUE(inventory.Tape().empty());
    EXPECT_TRUE(inventory.Custody().Held().empty());
    EXPECT_EQ(inventory.Custody().Parked().size(), 3U);
    ASSERT_EQ(inventory.ReactParked(7, puzzle, operation,
                                    [](const auto& step, const auto&) {
                                        return step.effects.atoms.at(4).position ==
                                               core::HexCoord{3, 0};
                                    }),
              MotionStatus::Found);
    EXPECT_EQ(inventory.Material().atoms.at(4).type, core::AtomType::Tin);
    EXPECT_FALSE(inventory.Material().atoms.contains(1));
    EXPECT_FALSE(inventory.Material().atoms.contains(2));
    EXPECT_EQ(inventory.Custody().Held().at(0), 4U);
    ASSERT_EQ(inventory.Custody().Parked().size(), 1U);
    EXPECT_EQ(inventory.Custody().Parked().at(9).material.at(3), stock.atoms.at(3));
    ASSERT_EQ(inventory.MoveHeld({{4, 0}, 0}), MotionStatus::Found);
    inventory.Custody().Validate(inventory.Material());
}
TEST(PhysicalInventory, CalcificationRebindsGripAtWorldPositionTransactionally) {
    auto stock = Stock();
    stock.nextId = 3;
    stock.atoms.at(1).position = {8, 0};
    PhysicalInventory inventory(
        stock, {{2, 1}, {3, 1}}, {0, 1, 4, 0},
        [](const auto&, const auto&, const auto&, char, bool) { return true; });
    inventory.ReserveLoose(1, 7);
    inventory.Grab(2);
    io::PuzzleData puzzle;
    const auto before = inventory.Material();
    const auto tape = inventory.Tape();
    const auto permit = [](const auto&, const auto&) { return true; };
    EXPECT_THROW(inventory.Calcify(puzzle, permit), IR::Error);
    puzzle.permissionBits = 1ULL << 12;
    EXPECT_THROW(inventory.Calcify(puzzle, {}), IR::Error);
    EXPECT_THROW(inventory.Calcify(puzzle, [](const auto&, const auto&) { return false; }),
                 IR::Error);
    EXPECT_EQ(inventory.Material(), before);
    EXPECT_EQ(inventory.Tape(), tape);
    EXPECT_EQ(inventory.Custody().Held().at(0), 2u);
    inventory.Calcify(puzzle, [](const auto& step, const auto&) {
        return step.effects.atoms.at(step.preconditions.nextId).position == core::HexCoord{2, 0};
    });
    const auto product = before.nextId;
    EXPECT_FALSE(inventory.Material().atoms.contains(2));
    EXPECT_EQ(inventory.Material().atoms.at(product).type, core::AtomType::Salt);
    EXPECT_EQ(inventory.Custody().Held().at(0), product);
    EXPECT_TRUE(inventory.Custody().Parked().contains(7));
    EXPECT_EQ(inventory.Material().atoms.at(1), before.atoms.at(1));
    ASSERT_EQ(inventory.MoveHeld({{3, 0}, 0}), MotionStatus::Found);
    EXPECT_EQ(inventory.Material().atoms.at(product).position, (core::HexCoord{3, 0}));
}
TEST(PhysicalInventory, CalcificationParkedRetriesContactsAndRejectsInvalidStock) {
    io::PuzzleData puzzle;
    puzzle.permissionBits = 1ULL << 12;
    auto stock = Stock();
    stock.nextId = 3;
    PhysicalInventory inventory(stock, {{0, 1}, {1, 1}, {2, 1}}, {0, 1, 4, 0},
                                [](const auto&, const auto& from, const auto& to, char op, bool) {
                                    return from.direction == 4 && to.direction == 4 &&
                                           (op == 'A' || op == 'a' || op == 'G' || op == 'g');
                                });
    const auto permit = [](const auto&, const auto&) { return true; };
    EXPECT_THROW(inventory.Calcify(puzzle, permit), IR::Error);
    inventory.ReserveLoose(1, 7);
    inventory.ReserveLoose(2, 8);
    const auto before = inventory.Material();
    EXPECT_NE(inventory.CalcifyParked(7, puzzle, permit), MotionStatus::Found);
    EXPECT_EQ(inventory.Material(), before);
    EXPECT_TRUE(inventory.Tape().empty());
    EXPECT_NE(inventory.CalcifyParked(8, puzzle, [](const auto&, const auto&) { return false; }),
              MotionStatus::Found);
    EXPECT_EQ(inventory.Material(), before);
    EXPECT_TRUE(inventory.Tape().empty());
    EXPECT_EQ(inventory.CalcifyParked(8, puzzle, permit), MotionStatus::Found);
    EXPECT_EQ(inventory.Custody().Held().at(0), 3u);
    EXPECT_TRUE(inventory.Custody().Parked().contains(7));
    EXPECT_FALSE(inventory.Custody().Parked().contains(8));

    stock.atoms.at(1).position = {1, 0};
    stock.bonds.emplace(IR::Edge{1, 2}, 1);
    PhysicalInventory bonded(
        stock, {{2, 1}}, {0, 1, 4, 0},
        [](const auto&, const auto&, const auto&, char, bool) { return true; });
    bonded.Grab(2);
    const auto bondedTape = bonded.Tape();
    EXPECT_THROW(bonded.Calcify(puzzle, [](const auto&, const auto&) { return false; }), IR::Error);
    EXPECT_EQ(bonded.Material(), stock);
    EXPECT_EQ(bonded.Tape(), bondedTape);
    bonded.Calcify(puzzle, permit);
    EXPECT_EQ(bonded.Custody().Held().at(0), 3U);
    EXPECT_EQ(bonded.Material().atoms.at(3).type, core::AtomType::Salt);
    EXPECT_EQ(bonded.Material().bonds.at({1, 3}), 1);
    EXPECT_EQ(bonded.Material().atoms.at(1), stock.atoms.at(1));
}
TEST(PhysicalInventory, BondedProjectionRollsBackAndPreservesUnrelatedParking) {
    io::PuzzleData p;
    p.permissionBits = 1ULL << 14;
    IR::State state;
    state.atoms = {{1, {core::AtomType::Lead, {0, 0}}},
                   {2, {core::AtomType::Quicksilver, {0, -1}}},
                   {3, {core::AtomType::Quicksilver, {1, 0}}},
                   {4, {core::AtomType::Salt, {5, 0}}}};
    state.bonds[IR::Edge{1, 2}] = 14;
    state.nextId = 5;
    PhysicalInventory inventory(
        state, {{0, 1}}, {0, 1, 4, 0},
        [](const auto&, const auto&, const auto&, char, bool) { return true; });
    inventory.Grab(1);
    inventory.ReserveLoose(3, 3);
    inventory.ReserveLoose(4, 4);
    const auto tape = inventory.Tape();
    EXPECT_THROW(inventory.ReactHeld(p, {IR::Kind::Project, 0, {1, 3}},
                                     [](const auto&, const auto&) { return false; }),
                 IR::Error);
    EXPECT_EQ(inventory.Material(), state);
    EXPECT_EQ(inventory.Tape(), tape);
    EXPECT_EQ(inventory.Custody().Held().at(0), 1U);
    EXPECT_TRUE(inventory.Custody().Parked().contains(3));
    inventory.ReactHeld(p, {IR::Kind::Project, 0, {1, 3}},
                        [](const auto&, const auto&) { return true; });
    EXPECT_EQ(inventory.Custody().Held().at(0), 5U);
    EXPECT_EQ(inventory.Material().bonds.at({2, 5}), 14);
    EXPECT_EQ(inventory.Material().atoms.at(4), state.atoms.at(4));
    EXPECT_TRUE(inventory.Custody().Parked().contains(4));
    EXPECT_FALSE(inventory.Custody().Parked().contains(3));
}

TEST(PhysicalInventory, AcquisitionRejectsOverlapAndCertificatesWithoutMutation) {
    io::PuzzleData puzzle;
    puzzle.inputs = {{{{core::AtomType::Salt, {0, 0}}}, {}}};
    auto state = IR::Initial(puzzle);
    PhysicalInventory inventory(
        state, {{0, 1}}, {0, 1, 4, 0},
        [](const auto&, const auto&, const auto&, char, bool) { return true; });
    EXPECT_THROW(inventory.Acquire(puzzle, 0, {}), IR::Error);
    EXPECT_THROW(inventory.Acquire(puzzle, 0, [](const auto&, const auto&) { return false; }),
                 IR::Error);
    EXPECT_EQ(inventory.Material(), state);
    EXPECT_TRUE(inventory.Tape().empty());
    inventory.Acquire(puzzle, 0, [](const auto&, const auto&) { return true; });
    inventory.ReserveLoose(1, 7);
    const auto before = inventory.Material();
    const auto tape = inventory.Tape();
    EXPECT_THROW(inventory.Acquire(puzzle, 0, [](const auto&, const auto&) { return true; }),
                 IR::Error);
    EXPECT_EQ(inventory.Material(), before);
    EXPECT_EQ(inventory.Tape(), tape);
    EXPECT_TRUE(inventory.Custody().Parked().contains(7));
}
TEST(PhysicalInventory, DiscardIsTransactionalAndPreservesOtherParkedStock) {
    auto inventory = Inventory();
    inventory.ReserveLoose(2, 8);
    inventory.Grab(1);
    io::PuzzleData puzzle;
    const auto before = inventory.Material();
    const auto tape = inventory.Tape();
    const auto permit = [](const auto&, const auto&) { return true; };
    EXPECT_THROW(inventory.Discard(puzzle, permit), IR::Error);
    puzzle.permissionBits = 1ULL << 17;
    EXPECT_THROW(inventory.Discard(puzzle, {}), IR::Error);
    EXPECT_THROW(inventory.Discard(puzzle, [](const auto&, const auto&) { return false; }),
                 IR::Error);
    EXPECT_EQ(inventory.Material(), before);
    EXPECT_EQ(inventory.Tape(), tape);
    ASSERT_EQ(inventory.Custody().Held().size(), 1u);
    EXPECT_EQ(inventory.Custody().Held().at(0), 1u);
    inventory.Discard(puzzle, permit);
    EXPECT_EQ(inventory.Material().discarded, 1u);
    EXPECT_FALSE(inventory.Material().atoms.contains(1));
    EXPECT_TRUE(inventory.Custody().Held().empty());
    EXPECT_TRUE(inventory.Custody().Parked().contains(8));
    const auto remaining = inventory.Material();
    const auto remainingTape = inventory.Tape();
    EXPECT_NE(inventory.DiscardParked(8, puzzle, [](const auto&, const auto&) { return false; }),
              MotionStatus::Found);
    EXPECT_EQ(inventory.Material(), remaining);
    EXPECT_EQ(inventory.Tape(), remainingTape);
    EXPECT_TRUE(inventory.Custody().Parked().contains(8));
    EXPECT_EQ(inventory.DiscardParked(8, puzzle, permit), MotionStatus::Found);
    EXPECT_EQ(inventory.Material().discarded, 2u);
    EXPECT_TRUE(inventory.Material().atoms.empty());
}
TEST(PhysicalInventory, CertifiedUnbondRecomputesMaterialAndReservesDetachedStock) {
    auto stock = Stock();
    stock.atoms.at(2).position = {1, 0};
    stock.bonds.emplace(IR::Edge{1, 2}, 1);
    PhysicalInventory inventory(
        stock, {{0, 1}, {1, 1}, {2, 1}}, {0, 1, 4, 0},
        [](const auto&, const auto&, const auto&, char, bool) { return true; });
    inventory.Grab(1);
    io::PuzzleData puzzle;
    puzzle.permissionBits = (1ULL << 8) | (1ULL << 9);
    const IR::Operation cut{IR::Kind::Unbond, 0, {1, 2}, 1};
    const auto before = inventory.Material();
    EXPECT_THROW(inventory.ApplyBond(puzzle, cut, [](const auto&, const auto&) { return false; }),
                 IR::Error);
    EXPECT_EQ(inventory.Material(), before);
    inventory.ApplyBond(puzzle, cut, [](const auto& step, const auto&) {
        return step.preconditions.bonds.size() == 1 && step.effects.bonds.empty();
    });
    inventory.ReserveLoose(2, 0);
    EXPECT_EQ(inventory.Tape(), (std::vector<char>{'G', ' '}));
    EXPECT_EQ(inventory.Custody().Parked().size(), 1u);
    EXPECT_THROW(inventory.ApplyBond(puzzle, {IR::Kind::Bond, 0, {1, 2}, 1},
                                     [](const auto&, const auto&) { return true; }),
                 IR::Error);
    EXPECT_TRUE(inventory.Material().bonds.empty());
}
TEST(PhysicalInventory, CertifiedMergeConsumesOnlySelectedParkingAndMovesCombinedFragment) {
    auto stock = Stock();
    stock.atoms.at(2).position = {1, 0};
    stock.atoms.emplace(3, IR::Atom{core::AtomType::Salt, {8, 0}});
    PhysicalInventory inventory(
        stock, {{0, 1}, {1, 1}}, {0, 1, 4, 0},
        [](const auto&, const auto&, const auto&, char, bool) { return true; });
    inventory.Grab(1);
    inventory.ReserveLoose(2, 7);
    inventory.ReserveLoose(3, 8);
    io::PuzzleData puzzle;
    puzzle.permissionBits = (1ULL << 8);
    const IR::Operation bond{IR::Kind::Bond, 0, {1, 2}, 1};
    const auto tape = inventory.Tape();
    EXPECT_THROW(
        inventory.BondHeldToParked(8, puzzle, bond, [](const auto&, const auto&) { return true; }),
        IR::Error);
    EXPECT_THROW(
        inventory.BondHeldToParked(7, puzzle, bond, [](const auto&, const auto&) { return false; }),
        IR::Error);
    EXPECT_EQ(inventory.Material(), stock);
    EXPECT_EQ(inventory.Tape(), tape);
    EXPECT_EQ(inventory.Custody().Parked().size(), 2u);
    inventory.BondHeldToParked(7, puzzle, bond, [](const auto& step, const auto&) {
        return step.effects.bonds.size() == 1;
    });
    EXPECT_FALSE(inventory.Custody().Parked().contains(7));
    EXPECT_TRUE(inventory.Custody().Parked().contains(8));
    EXPECT_EQ(inventory.Custody().Held().at(0), 1u);
    ASSERT_EQ(inventory.MoveHeld({{1, 0}, 0}), MotionStatus::Found);
    EXPECT_EQ(inventory.Material().atoms.at(2).position, (core::HexCoord{2, 0}));
    EXPECT_EQ(inventory.Material().atoms.at(3).position, (core::HexCoord{8, 0}));
}
TEST(PhysicalInventory, ParkedMergeSearchesPosesAndAcceptsEitherOperandSlot) {
    auto stock = Stock();
    stock.atoms.at(2).position = {1, 0};
    PhysicalInventory inventory(
        stock, {{0, 1}, {-1, 1}}, {0, 1, 4, 0},
        [](const auto&, const auto&, const auto&, char, bool) { return true; });
    inventory.ReserveLoose(1, 7);
    inventory.ReserveLoose(2, 8);
    io::PuzzleData puzzle;
    puzzle.permissionBits = (1ULL << 8);
    const IR::Operation bond{IR::Kind::Bond, 0, {1, 2}, 1};
    EXPECT_EQ(
        inventory.ApplyParkedBond(7, puzzle, bond, [](const auto&, const auto&) { return false; }),
        MotionStatus::Unsatisfiable);
    EXPECT_EQ(inventory.Material(), stock);
    EXPECT_TRUE(inventory.Tape().empty());
    EXPECT_EQ(inventory.Custody().Parked().size(), 2u);
    auto opposite = inventory;
    ASSERT_EQ(
        inventory.ApplyParkedBond(7, puzzle, bond,
                                  [](const auto&, const auto& arm) { return arm.trackIndex == 1; }),
        MotionStatus::Found);
    EXPECT_EQ(inventory.Arm().trackIndex, 1u);
    EXPECT_EQ(inventory.Custody().Held().at(0), 1u);
    EXPECT_TRUE(inventory.Custody().Parked().empty());
    EXPECT_EQ(inventory.Material().bonds.size(), 1u);
    ASSERT_EQ(
        opposite.ApplyParkedBond(8, puzzle, bond, [](const auto&, const auto&) { return true; }),
        MotionStatus::Found);
    EXPECT_EQ(opposite.Custody().Held().at(0), 2u);
    EXPECT_TRUE(opposite.Custody().Parked().empty());
}
TEST(PhysicalInventory, ParkedGlyphRetriesContactAndRollsBackRejectedCandidates) {
    auto stock = Stock();
    stock.atoms.at(2).position = {1, 0};
    stock.bonds.emplace(IR::Edge{1, 2}, 1);
    PhysicalInventory inventory(stock, {{0, 1}, {1, 1}}, {0, 1, 4, 0},
                                [](const auto&, const auto& from, const auto& to, char op, bool) {
                                    return from.direction == 4 && to.direction == 4 &&
                                           (op == 'A' || op == 'a' || op == 'G' || op == 'g');
                                });
    inventory.ReserveLoose(1, 7);
    io::PuzzleData puzzle;
    puzzle.permissionBits = (1ULL << 9);
    const IR::Operation cut{IR::Kind::Unbond, 0, {1, 2}, 1};
    EXPECT_EQ(inventory.ApplyParkedBond(
                  7, puzzle, cut, [](const auto&, const auto&) { return true; }, 0),
              MotionStatus::Exhausted);
    EXPECT_THROW((void)inventory.ApplyParkedBond(99, puzzle, cut,
                                                 [](const auto&, const auto&) { return true; }),
                 IR::Error);
    EXPECT_EQ(
        inventory.ApplyParkedBond(7, puzzle, cut, [](const auto&, const auto&) { return false; }),
        MotionStatus::Unsatisfiable);
    EXPECT_EQ(inventory.Material(), stock);
    EXPECT_TRUE(inventory.Tape().empty());
    EXPECT_TRUE(inventory.Custody().Held().empty());
    EXPECT_EQ(inventory.Custody().Parked().size(), 1u);
    ASSERT_EQ(inventory.ApplyParkedBond(
                  7, puzzle, cut, [](const auto&, const auto& arm) { return arm.trackIndex == 1; }),
              MotionStatus::Found);
    EXPECT_EQ(inventory.Custody().Held().at(0), 2u);
    EXPECT_TRUE(inventory.Material().bonds.empty());
    EXPECT_EQ(inventory.Tape(), (std::vector<char>{'A', 'G', ' '}));
}
TEST(PhysicalInventory, EmptyArmSwitchesSlotsAndTransportsWithoutMovingReservedStock) {
    auto inventory = Inventory();
    inventory.Grab(1);
    inventory.Park(0);
    inventory.ReserveLoose(2, 1);
    EXPECT_EQ(inventory.MoveEmpty({2, 1, 4, 0}), MotionStatus::Found);
    inventory.Retrieve(1, 2);
    EXPECT_EQ(inventory.MoveHeld({{4, 0}, 0}), MotionStatus::Found);
    EXPECT_EQ(inventory.Material().atoms.at(1).position, (core::HexCoord{0, 0}));
    EXPECT_EQ(inventory.Material().atoms.at(2).position, (core::HexCoord{4, 0}));
    inventory.Park(1);
    EXPECT_EQ(inventory.MoveEmpty({0, 1, 4, 0}), MotionStatus::Found);
    inventory.Retrieve(0, 1);
    EXPECT_EQ(inventory.Custody().Held().at(0), 1u);
    EXPECT_EQ(inventory.Tape(),
              (std::vector<char>{'G', 'g', 'A', 'A', 'G', 'A', 'A', 'g', 'a', 'a', 'a', 'a', 'G'}));
}
TEST(PhysicalInventory, RoutedRetrievalSearchesGripAndPreservesFailedInventory) {
    auto inventory = Inventory();
    inventory.Grab(1);
    inventory.Park(0);
    inventory.ReserveLoose(2, 1);
    ASSERT_EQ(inventory.RetrieveRouted(1, 2), MotionStatus::Found);
    EXPECT_EQ(inventory.Custody().Held().at(0), 2u);
    EXPECT_EQ(inventory.Arm().trackIndex, 2u);
    inventory.Park(1);
    const auto material = inventory.Material();
    const auto tape = inventory.Tape();
    const auto arm = inventory.Arm();
    EXPECT_EQ(inventory.RetrieveRouted(0, 1, 0), MotionStatus::Exhausted);
    EXPECT_EQ(inventory.Material(), material);
    EXPECT_EQ(inventory.Tape(), tape);
    EXPECT_EQ(inventory.Arm(), arm);
    ASSERT_EQ(inventory.RetrieveRouted(0, 1), MotionStatus::Found);
    EXPECT_EQ(inventory.Custody().Held().at(0), 1u);
}
TEST(PhysicalInventory, RoutedRetrievalFallsBackAfterRejectedGripInterval) {
    auto stock = Stock();
    stock.atoms.erase(2);
    PhysicalInventory inventory(stock, {{0, 1}, {1, 0}}, {0, 1, 4, 0},
                                [](const auto&, const auto&, const auto& to, char op, bool) {
                                    return op != 'G' || to.trackIndex == 1;
                                });
    inventory.ReserveLoose(1, 4);
    ASSERT_EQ(inventory.RetrieveRouted(4, 1), MotionStatus::Found);
    EXPECT_EQ(inventory.Arm().trackIndex, 1u);
    EXPECT_EQ(inventory.Arm().direction, 3);
    EXPECT_EQ(inventory.Custody().Held().at(0), 1u);
    EXPECT_EQ(std::count(inventory.Tape().begin(), inventory.Tape().end(), 'G'), 1);
    EXPECT_EQ(inventory.Material(), stock);
}
TEST(PhysicalInventory, FragmentRetrievalSelectsReachableAtomWithoutPartialMutation) {
    auto stock = Stock();
    stock.atoms.at(1).position = {3, 0};
    stock.bonds[ExecutionIR::Edge{1, 2}] = 1;
    PhysicalInventory inventory(
        stock, {{1, 0}}, {0, 1, 0, 0},
        [](const auto&, const auto&, const auto&, char, bool) { return true; });
    inventory.ReserveLoose(1, 7);
    const auto before = inventory.Material();
    EXPECT_THROW((void)inventory.RetrieveFragmentRouted(99), ExecutionIR::Error);
    EXPECT_EQ(inventory.Material(), before);
    EXPECT_TRUE(inventory.Tape().empty());
    ASSERT_EQ(inventory.RetrieveFragmentRouted(7), MotionStatus::Found);
    EXPECT_EQ(inventory.Custody().Held().at(0), 2u);
    EXPECT_EQ(inventory.Material(), before);
    EXPECT_THROW((void)inventory.RetrieveFragmentRouted(7), ExecutionIR::Error);
}
TEST(PhysicalInventory, FragmentRetrievalRejectsAllGripsWithoutMutation) {
    auto stock = Stock();
    stock.atoms.at(2).position = {1, 0};
    stock.bonds[IR::Edge{1, 2}] = 1;
    PhysicalInventory inventory(
        stock, {{0, 1}, {1, 1}}, {0, 1, 4, 0},
        [](const auto&, const auto&, const auto&, char op, bool) { return op != 'G'; });
    inventory.ReserveLoose(1, 3);
    EXPECT_EQ(inventory.RetrieveFragmentRouted(3), MotionStatus::Unsatisfiable);
    EXPECT_EQ(inventory.Material(), stock);
    EXPECT_TRUE(inventory.Tape().empty());
    EXPECT_TRUE(inventory.Custody().Held().empty());
    EXPECT_EQ(inventory.Custody().Parked().at(3).atoms.size(), 2u);
    EXPECT_EQ(inventory.Arm(), (HeldArmState{0, 1, 4, 0}));
}
TEST(PhysicalInventory, ParkedTransportRetriesGripWhenHeldRouteFails) {
    auto stock = Stock();
    stock.atoms.at(2).position = {1, 0};
    stock.bonds[IR::Edge{1, 2}] = 1;
    PhysicalInventory inventory(
        stock, {{0, 1}, {1, 1}, {2, 1}, {3, 1}}, {0, 1, 4, 0},
        [](const auto&, const auto& from, const auto& to, char op, bool holding) {
            return from.direction == 4 && to.direction == 4 &&
                   (op == 'G' || op == 'g' || op == 'a' ||
                    (op == 'A' && (!holding || from.trackIndex > 0)));
        });
    inventory.ReserveLoose(1, 0);
    ASSERT_EQ(inventory.MoveParked(0, 1, {{2, 0}, 0}), MotionStatus::Found);
    EXPECT_EQ(inventory.Custody().Held().at(0), 2u);
    EXPECT_EQ(inventory.Material().atoms.at(1).position, (core::HexCoord{2, 0}));
    EXPECT_EQ(inventory.Material().atoms.at(2).position, (core::HexCoord{3, 0}));
    EXPECT_EQ(inventory.Tape(), (std::vector<char>{'A', 'G', 'A', 'A'}));
}
TEST(PhysicalInventory, ParkedTransportRotatesNamedAnchorWhenGrippingAnotherAtom) {
    auto stock = Stock();
    stock.atoms.at(2).position = {1, 0};
    stock.bonds[IR::Edge{1, 2}] = 1;
    PhysicalInventory inventory(stock, {{0, 1}, {1, 1}, {2, 1}, {3, 1}}, {0, 1, 4, 0},
                                [](const auto& world, const auto&, const auto& to, char op, bool) {
                                    if (op != 'G') return true;
                                    // Only the second atom may be grabbed, regardless of contact
                                    // pose.
                                    return to.direction == 4 && to.length == 1 &&
                                           world.atoms.at(2).position ==
                                               core::HexCoord{static_cast<int>(to.trackIndex), 0};
                                });
    inventory.ReserveLoose(1, 0);
    ASSERT_EQ(inventory.MoveParked(0, 1, {{2, 0}, 1}), MotionStatus::Found);
    EXPECT_EQ(inventory.Custody().Held().at(0), 2u);
    EXPECT_EQ(inventory.Material().atoms.at(1).position, (core::HexCoord{2, 0}));
    EXPECT_EQ(inventory.Material().atoms.at(2).position,
              core::Add(core::HexCoord{2, 0}, core::RotateClockwise({1, 0})));
}
TEST(PhysicalInventory, ParkedTransportFailurePreservesGripAndTape) {
    auto inventory = Inventory();
    inventory.ReserveLoose(1, 0);
    const auto before = inventory.Material();
    EXPECT_EQ(inventory.MoveParked(0, 1, {{20, 0}, 0}), MotionStatus::Unsatisfiable);
    EXPECT_EQ(inventory.Material(), before);
    EXPECT_TRUE(inventory.Tape().empty());
    EXPECT_TRUE(inventory.Custody().Held().empty());
    EXPECT_EQ(inventory.Custody().Parked().size(), 1u);
    EXPECT_EQ(inventory.Arm(), (HeldArmState{0, 1, 4, 0}));
    EXPECT_THROW((void)inventory.MoveParked(0, 2, {{2, 0}, 0}), IR::Error);
}
TEST(PhysicalInventory, ParkedOutputRetriesReleaseAndRollsBackRejectedCandidates) {
    auto stock = Stock();
    stock.outputs.resize(1);
    io::PuzzleData puzzle;
    puzzle.outputs = {{{{core::AtomType::Salt, {0, 0}}}, {}}};
    PhysicalInventory inventory(stock, {{0, 1}, {1, 0}}, {0, 1, 4, 0},
                                [](const auto&, const auto& from, const auto&, char op, bool) {
                                    return op != 'g' || from.trackIndex == 1;
                                });
    inventory.ReserveLoose(1, 7);
    inventory.ReserveLoose(2, 8);
    const auto arm = inventory.Arm();
    EXPECT_THROW(
        (void)inventory.OutputParked(99, puzzle, 0, [](const auto&, const auto&) { return true; }),
        IR::Error);
    EXPECT_THROW(
        (void)inventory.OutputParked(7, puzzle, 1, [](const auto&, const auto&) { return true; }),
        IR::Error);
    EXPECT_EQ(inventory.OutputParked(7, puzzle, 0, [](const auto&, const auto&) { return false; }),
              MotionStatus::Unsatisfiable);
    EXPECT_EQ(inventory.Material(), stock);
    EXPECT_EQ(inventory.Arm(), arm);
    EXPECT_TRUE(inventory.Tape().empty());
    EXPECT_TRUE(inventory.Custody().Held().empty());
    EXPECT_EQ(inventory.Custody().Parked().size(), 2u);
    ASSERT_EQ(inventory.OutputParked(7, puzzle, 0, [](const auto&, const auto&) { return true; }),
              MotionStatus::Found);
    EXPECT_EQ(inventory.Arm().trackIndex, 1u);
    EXPECT_EQ(inventory.Material().outputs, (std::vector<std::size_t>{1}));
    EXPECT_EQ(inventory.Custody().Parked().size(), 1u);
    EXPECT_EQ(inventory.Material().atoms.size(), 1u);
    EXPECT_EQ(inventory.Tape(), (std::vector<char>{'R', 'A', 'G', 'g'}));
}
TEST(PhysicalInventory, OutputConsumesOnlyCertifiedHeldProductAndPreservesReservedStock) {
    auto stock = Stock();
    stock.outputs.resize(1);
    PhysicalInventory inventory(
        stock, {{0, 1}, {1, 1}}, {0, 1, 4, 0},
        [](const auto&, const auto&, const auto&, char, bool) { return true; });
    io::PuzzleData puzzle;
    puzzle.outputs = {{{{core::AtomType::Salt, {0, 0}}}, {}}};
    inventory.ReserveLoose(2, 7);
    inventory.Grab(1);
    const auto before = inventory.Material();
    const auto tape = inventory.Tape();
    EXPECT_THROW(inventory.Output(puzzle, 0, {}), IR::Error);
    EXPECT_THROW(inventory.Output(puzzle, 0, [](const auto&, const auto&) { return false; }),
                 IR::Error);
    EXPECT_THROW(inventory.Output(puzzle, 1, [](const auto&, const auto&) { return true; }),
                 IR::Error);
    EXPECT_EQ(inventory.Material(), before);
    EXPECT_EQ(inventory.Tape(), tape);
    EXPECT_EQ(inventory.Custody().Held().at(0), 1u);
    auto wrongShape = puzzle;
    wrongShape.outputs[0].atoms[0].type = core::AtomType::Fire;
    EXPECT_THROW(inventory.Output(wrongShape, 0, [](const auto&, const auto&) { return true; }),
                 IR::Error);
    EXPECT_EQ(inventory.Material(), before);
    EXPECT_EQ(inventory.Tape(), tape);
    inventory.Output(puzzle, 0, [](const auto&, const auto&) { return true; });
    EXPECT_TRUE(inventory.Custody().Held().empty());
    EXPECT_EQ(inventory.Custody().Parked().size(), 1u);
    EXPECT_EQ(inventory.Material().atoms.size(), 1u);
    EXPECT_EQ(inventory.Material().atoms.at(2), before.atoms.at(2));
    EXPECT_EQ(inventory.Material().outputs, (std::vector<std::size_t>{1}));
    EXPECT_EQ(inventory.Tape().back(), 'g');
    EXPECT_THROW(inventory.Output(puzzle, 0, [](const auto&, const auto&) { return true; }),
                 IR::Error);
}
TEST(PhysicalInventory, OutputRejectsMissingLedgerAndReleaseIntervalWithoutMutation) {
    io::PuzzleData puzzle;
    puzzle.outputs = {{{{core::AtomType::Salt, {0, 0}}}, {}}};
    for (const bool ledger : {false, true}) {
        auto stock = Stock();
        if (ledger) stock.outputs.resize(1);
        PhysicalInventory inventory(
            stock, {{0, 1}}, {0, 1, 4, 0},
            [](const auto&, const auto&, const auto&, char op, bool) { return op != 'g'; });
        inventory.Grab(1);
        const auto before = inventory.Material();
        const auto tape = inventory.Tape();
        EXPECT_THROW(inventory.Output(puzzle, 0, [](const auto&, const auto&) { return true; }),
                     IR::Error);
        EXPECT_EQ(inventory.Material(), before);
        EXPECT_EQ(inventory.Tape(), tape);
        EXPECT_EQ(inventory.Custody().Held().at(0), 1u);
    }
}
TEST(PhysicalInventory, FailedSearchAndParkedCollisionLeaveMaterialArmAndTapeUnchanged) {
    auto inventory = Inventory();
    inventory.Grab(1);
    inventory.ReserveLoose(2, 0);
    auto before = inventory.Material();
    auto tape = inventory.Tape();
    auto arm = inventory.Arm();
    EXPECT_EQ(inventory.MoveHeld({{4, 0}, 0}), MotionStatus::Unsatisfiable);
    EXPECT_EQ(inventory.Material(), before);
    EXPECT_EQ(inventory.Tape(), tape);
    EXPECT_EQ(inventory.Arm(), arm);
    EXPECT_EQ(inventory.MoveHeld({{1, 0}, 0}, 0), MotionStatus::Exhausted);
    EXPECT_THROW(inventory.MoveEmpty({0, 1, 4, 0}), IR::Error);
    inventory.Park(1);
    auto blocked = Inventory(false);
    EXPECT_EQ(blocked.MoveEmpty({1, 1, 4, 0}), MotionStatus::Unsatisfiable);
    EXPECT_TRUE(blocked.Tape().empty());
}
}} // namespace opusoptima::solvers
