#include "ExecutionIR.hpp"
#include "ExecutionPlan.hpp"
#include "FragmentCustody.hpp"
#include "RowPhaseProfile.hpp"
#include <gtest/gtest.h>
#include <tuple>

namespace opusoptima::solvers { namespace {
using IR = ExecutionIR;
using K = IR::Kind;
using T = core::AtomType;
io::PuzzleData Puzzle(std::initializer_list<T> feeds) {
    io::PuzzleData p;
    p.permissionBits = ~0ULL;
    for (auto t : feeds)
        p.inputs.push_back({{{t, {}}}, {}});
    return p;
}
void Add(IR& ir, const io::PuzzleData& p, IR::State& s, K k, std::vector<IR::AtomId> atoms = {},
         std::size_t index = 0, std::uint8_t bits = 0) {
    ir.Append(p, s, {k, index, std::move(atoms), bits});
}
TEST(ExecutionIRTest, WholeBatchVariedShapesPreservesOriginalCoordinatesAndLabels) {
    for (const auto& cells :
         std::vector<std::vector<core::HexCoord>>{{{0, 0}, {1, 0}, {2, 0}},
                                                  {{0, 0}, {1, 0}, {1, -1}},
                                                  {{0, 0}, {1, 0}, {0, 1}, {-1, 1}},
                                                  {{0, 0}, {1, 0}, {1, -1}, {0, -1}}}) {
        auto p = Puzzle({});
        io::Molecule m;
        for (auto cell : cells)
            m.atoms.push_back({T::Fire, cell});
        for (std::size_t i = 1; i < cells.size(); ++i) {
            std::size_t parent = 0;
            while (core::Distance(cells[parent], cells[i]) != 1)
                ++parent;
            m.bonds.push_back(
                {static_cast<std::uint8_t>(i == 1 ? 14 : 1), cells[parent], cells[i]});
        }
        p.inputs = {m};
        p.outputs = {m};
        IR ir;
        auto s = IR::Initial(p);
        Add(ir, p, s, K::Acquire);
        EXPECT_EQ(s.atoms.size(), cells.size());
        EXPECT_EQ(s.bonds.size(), m.bonds.size());
        for (std::size_t i = 0; i < cells.size(); ++i)
            EXPECT_EQ(s.atoms.at(i + 1).position, cells[i]);
        ir.Append(p, s, {K::Relocate, 0, {1}, 0, 2, {12, -8}});
        Add(ir, p, s, K::Output, {1});
        EXPECT_NO_THROW(IR::RequireComplete(ir.Replay(p), {1}));
        EXPECT_EQ(s.batches, (std::vector<std::size_t>{1}));
    }
}
TEST(ExecutionIRTest, InheritedNonFireColorCanMoveButCannotBeCreated) {
    auto p = Puzzle({});
    p.inputs = {{{{T::Air, {0, 0}}, {T::Earth, {1, 0}}}, {{14, {0, 0}, {1, 0}}}}};
    p.outputs = p.inputs;
    IR ir;
    auto s = IR::Initial(p);
    Add(ir, p, s, K::Acquire);
    ir.Append(p, s, {K::Relocate, 0, {1}, 0, 0, {8, 0}});
    Add(ir, p, s, K::Output, {1});
    EXPECT_NO_THROW(IR::RequireComplete(ir.Replay(p), {1}));
    IR chemistry;
    auto stock = IR::Initial(p);
    Add(chemistry, p, stock, K::Acquire);
    Add(chemistry, p, stock, K::Unbond, {1, 2}, 0, 14);
    EXPECT_THROW(Add(chemistry, p, stock, K::Bond, {1, 2}, 0, 14), IR::Error);
}
TEST(ExecutionIRTest, ReverseOperandAndSourceEdgesRemainCanonical) {
    auto p = Puzzle({});
    p.inputs = {{{{T::Air, {0, 0}}, {T::Air, {1, 0}}}, {{1, {1, 0}, {0, 0}}}}};
    p.outputs = p.inputs;
    IR ir;
    auto s = IR::Initial(p);
    Add(ir, p, s, K::Acquire);
    EXPECT_TRUE(s.bonds.contains({1, 2}));
    EXPECT_FALSE(s.bonds.contains({2, 1}));
    Add(ir, p, s, K::Unbond, {2, 1}, 0, 1);
    Add(ir, p, s, K::Bond, {2, 1}, 0, 1);
    EXPECT_TRUE(s.bonds.contains({1, 2}));
    Add(ir, p, s, K::Output, {2});
    EXPECT_NO_THROW(IR::RequireComplete(ir.Replay(p), {1}));
}
TEST(ExecutionIRTest, ExactOutputRejectsWrongBondsExtraAtomsAndReflection) {
    auto p = Puzzle({T::Fire});
    p.inputs[0] = {{{T::Air, {0, 0}}, {T::Earth, {1, 0}}, {T::Water, {0, 1}}},
                   {{1, {0, 0}, {1, 0}}, {1, {0, 0}, {0, 1}}}};
    p.outputs = p.inputs;
    IR ir;
    auto s = IR::Initial(p);
    Add(ir, p, s, K::Acquire);
    Add(ir, p, s, K::Unbond, {1, 2}, 0, 1);
    EXPECT_THROW(Add(ir, p, s, K::Output, {1}), IR::Error);
    EXPECT_THROW(Add(ir, p, s, K::Output, {2}), IR::Error);
    Add(ir, p, s, K::Bond, {1, 2}, 0, 1);
    auto reflected = p;
    reflected.outputs[0].atoms[2].position = {1, -1};
    reflected.outputs[0].bonds[1].to = {1, -1};
    EXPECT_THROW(ir.Append(reflected, s, {K::Output, 0, {1}}), IR::Error);
    Add(ir, p, s, K::Output, {1});
    EXPECT_NO_THROW(IR::RequireComplete(s, {1}));
}
TEST(ExecutionIRTest, PartialColorUnbondPreservesOtherLabelsAndChecksPermissions) {
    auto p = Puzzle({});
    p.inputs.push_back({{{T::Fire, {0, 0}}, {T::Fire, {1, 0}}}, {{14, {0, 0}, {1, 0}}}});
    IR ir;
    auto s = IR::Initial(p);
    Add(ir, p, s, K::Acquire);
    auto denied = p;
    denied.permissionBits &= ~(1ULL << 9);
    EXPECT_THROW(ir.Append(denied, s, {K::Unbond, 0, {1, 2}, 2}), IR::Error);
    Add(ir, p, s, K::Unbond, {1, 2}, 0, 2);
    EXPECT_EQ(s.bonds.at({1, 2}), 12);
    denied = p;
    denied.permissionBits &= ~(1ULL << 11);
    EXPECT_THROW(ir.Append(denied, s, {K::Bond, 0, {1, 2}, 2}), IR::Error);
    Add(ir, p, s, K::Bond, {1, 2}, 0, 2);
    EXPECT_EQ(s.bonds.at({1, 2}), 14);
    EXPECT_THROW(Add(ir, p, s, K::Bond, {1, 2}, 0, 1), IR::Error);
    EXPECT_THROW(Add(ir, p, s, K::Unbond, {1, 2}, 0, 1), IR::Error);
}
TEST(ExecutionIRTest, MissingPermissionsCarryTypedBitsWithoutMutatingState) {
    for (const auto& [kind, mask, bit] : std::vector<std::tuple<K, std::uint8_t, int>>{
             {K::Unbond, 2, 9}, {K::Bond, 1, 8}, {K::Bond, 2, 11}}) {
        SCOPED_TRACE(bit);
        auto p = Puzzle({});
        io::Molecule input{{{T::Fire, {0, 0}}, {T::Fire, {1, 0}}}, {}};
        if (kind == K::Unbond) input.bonds.push_back({14, {0, 0}, {1, 0}});
        p.inputs.push_back(input);
        IR ir;
        auto state = IR::Initial(p);
        Add(ir, p, state, K::Acquire);
        const auto before = state;
        const auto stepCount = ir.steps.size();
        p.permissionBits &= ~(1ULL << bit);
        try {
            ir.Append(p, state, {kind, 0, {1, 2}, mask});
            FAIL() << "missing permission accepted";
        } catch (const IR::PermissionFailure& error) {
            EXPECT_EQ(error.bit, bit);
        }
        EXPECT_EQ(state, before);
        EXPECT_EQ(ir.steps.size(), stepCount);
    }
}
TEST(ExecutionIRTest, OrdinaryBondRequiresPermissionAdjacencyAndNoOverlap) {
    auto p = Puzzle({T::Salt});
    IR ir;
    auto s = IR::Initial(p);
    for (int i = 0; i < 3; ++i)
        Add(ir, p, s, K::Acquire);
    EXPECT_THROW(Add(ir, p, s, K::Bond, {1, 2}, 0, 1), IR::Error);
    ir.Append(p, s, {K::Relocate, 0, {2}, 0, 0, {1, 0}});
    auto denied = p;
    denied.permissionBits &= ~(1ULL << 8);
    EXPECT_THROW(ir.Append(denied, s, {K::Bond, 0, {1, 2}, 1}), IR::Error);
    EXPECT_THROW(Add(ir, p, s, K::Bond, {1, 2}, 0, 2), IR::Error);
    Add(ir, p, s, K::Bond, {1, 2}, 0, 1);
    EXPECT_THROW(Add(ir, p, s, K::Bond, {2, 3}, 0, 1), IR::Error);
}
TEST(ExecutionIRTest, AnimismusDistinctSaltFreshProductsAndAtomicRefusals) {
    auto p = Puzzle({T::Salt, T::Salt, T::Earth});
    IR ir;
    auto s = IR::Initial(p);
    for (std::size_t i = 0; i < 3; ++i)
        Add(ir, p, s, K::Acquire, {}, i);
    const auto before = s;
    EXPECT_THROW(Add(ir, p, s, K::Animismus, {1, 1}), IR::Error);
    EXPECT_THROW(Add(ir, p, s, K::Animismus, {1, 3}), IR::Error);
    auto denied = p;
    denied.permissionBits &= ~(1ULL << 16);
    EXPECT_THROW(ir.Append(denied, s, {K::Animismus, 0, {1, 2}}), IR::Error);
    EXPECT_EQ(s, before);
    Add(ir, p, s, K::Animismus, {2, 1});
    EXPECT_FALSE(s.atoms.contains(1));
    EXPECT_FALSE(s.atoms.contains(2));
    EXPECT_EQ(s.atoms.at(4).type, T::Vitae);
    EXPECT_EQ(s.atoms.at(5).type, T::Mors);
    EXPECT_EQ(s.atoms.at(3), before.atoms.at(3));
    EXPECT_EQ(s.nextId, 6U);
}
TEST(ExecutionIRTest, ChemistryExactAccountingAndPermissions) {
    struct Case {
        K kind;
        std::vector<T> feeds, products;
        int bit;
    };
    for (const auto& c : std::vector<Case>{
             {K::Calcify, {T::Air}, {T::Salt}, 12},
             {K::Project, {T::Lead, T::Quicksilver}, {T::Tin}, 14},
             {K::Purify, {T::Silver, T::Silver}, {T::Gold}, 15},
             {K::Animismus, {T::Salt, T::Salt}, {T::Vitae, T::Mors}, 16},
             {K::Unify, {T::Water, T::Fire, T::Air, T::Earth}, {T::Quintessence}, 18},
             {K::Disperse, {T::Quintessence}, {T::Air, T::Earth, T::Fire, T::Water}, 18},
             {K::Duplicate, {T::Salt, T::Earth}, {T::Earth}, 13}}) {
        auto p = Puzzle({});
        for (auto t : c.feeds)
            p.inputs.push_back({{{t, {}}}, {}});
        for (auto t : c.products)
            p.outputs.push_back({{{t, {}}}, {}});
        IR ir;
        auto s = IR::Initial(p);
        std::vector<IR::AtomId> operands;
        for (std::size_t i = 0; i < c.feeds.size(); ++i) {
            Add(ir, p, s, K::Acquire, {}, i);
            operands.push_back(i + 1);
        }
        const auto before = s;
        auto denied = p;
        denied.permissionBits &= ~(1ULL << c.bit);
        EXPECT_THROW(ir.Append(denied, s, {c.kind, 0, operands}), IR::Error);
        EXPECT_EQ(s, before);
        Add(ir, p, s, c.kind, operands);
        EXPECT_EQ(s.atoms.size(), c.products.size() + (c.kind == K::Duplicate));
        for (std::size_t i = 0; i < c.products.size(); ++i) {
            auto id = c.feeds.size() + i + 1;
            EXPECT_EQ(s.atoms.at(id).type, c.products[i]);
            Add(ir, p, s, K::Output, {id}, i);
        }
        if (c.kind == K::Duplicate) {
            EXPECT_EQ(s.atoms.at(2), before.atoms.at(2));
            EXPECT_THROW(IR::RequireComplete(s, {1}), IR::Error);
            Add(ir, p, s, K::Output, {2}, 0);
            EXPECT_NO_THROW(IR::RequireComplete(s, {2}));
        } else
            EXPECT_NO_THROW(IR::RequireComplete(s, std::vector<std::size_t>(c.products.size(), 1)));
        EXPECT_EQ(s, ir.Replay(p));
    }
}
TEST(ExecutionIRTest, CoproductCannotVanishWithoutDisposal) {
    auto p = Puzzle({T::Quintessence});
    p.permissionBits &= ~(1ULL << 17);
    p.outputs.push_back({{{T::Water, {}}}, {}});
    IR ir;
    auto s = IR::Initial(p);
    Add(ir, p, s, K::Acquire);
    Add(ir, p, s, K::Disperse, {1});
    Add(ir, p, s, K::Output, {5});
    EXPECT_THROW(IR::RequireComplete(s, {1}), IR::Error);
    EXPECT_THROW(Add(ir, p, s, K::Discard, {2}), IR::Error);
    EXPECT_EQ(s.atoms.size(), 3);
    EXPECT_EQ(s.discarded, 0);
}
TEST(ExecutionIRTest, BatchRemainderMustBeExplicitlyUsedOrDiscarded) {
    auto p = Puzzle({});
    p.permissionBits &= ~(1ULL << 17);
    p.inputs.push_back({{{T::Air, {}}, {T::Salt, {1, 0}}}, {{1, {}, {1, 0}}}});
    p.outputs.push_back({{{T::Air, {}}}, {}});
    IR ir;
    auto s = IR::Initial(p);
    Add(ir, p, s, K::Acquire);
    Add(ir, p, s, K::Unbond, {1, 2}, 0, 1);
    Add(ir, p, s, K::Output, {1});
    EXPECT_THROW(IR::RequireComplete(s, {1}), IR::Error);
    EXPECT_THROW(Add(ir, p, s, K::Discard, {2}), IR::Error);
    EXPECT_EQ(s.atoms.at(2).type, T::Salt);
    auto tampered = ir;
    tampered.steps.back().effects.atoms.clear();
    EXPECT_THROW((void)tampered.Replay(p), IR::Error);
}
TEST(ExecutionIRTest, WholeFragmentStorageAndDiscard) {
    auto p = Puzzle({});
    p.inputs.push_back({{{T::Air, {}}, {T::Water, {1, 0}}}, {{1, {}, {1, 0}}}});
    IR ir;
    auto s = IR::Initial(p);
    Add(ir, p, s, K::Acquire);
    Add(ir, p, s, K::Store, {2}, 7);
    EXPECT_EQ(s.stored.at(7), (std::vector<IR::AtomId>{1, 2}));
    EXPECT_THROW(Add(ir, p, s, K::Unbond, {1, 2}, 0, 1), IR::Error);
    EXPECT_THROW(Add(ir, p, s, K::Discard, {1}), IR::Error);
    EXPECT_THROW(Add(ir, p, s, K::Retrieve, {}, 8), IR::Error);
    Add(ir, p, s, K::Retrieve, {}, 7);
    Add(ir, p, s, K::Discard, {2});
    EXPECT_EQ(s.discarded, 2);
    EXPECT_NO_THROW(IR::RequireComplete(s, {}));
}
TEST(ExecutionIRTest, InvalidReferencesAndAlteredTraceRejectAtomically) {
    auto p = Puzzle({T::Air, T::Water});
    IR ir;
    auto s = IR::Initial(p);
    EXPECT_THROW(Add(ir, p, s, K::Acquire, {}, 2), IR::Error);
    Add(ir, p, s, K::Acquire);
    EXPECT_THROW(Add(ir, p, s, K::Calcify, {99}), IR::Error);
    EXPECT_THROW(Add(ir, p, s, K::Animismus, {1, 1}), IR::Error);
    EXPECT_THROW(Add(ir, p, s, K::Acquire, {1}), IR::Error);
    EXPECT_THROW(ir.Append(p, s, {K::Calcify, 1, {1}}), IR::Error);
    const auto before = s;
    Add(ir, p, s, K::Calcify, {1});
    EXPECT_THROW(Add(ir, p, s, K::Calcify, {1}), IR::Error);
    auto changed = ir;
    changed.steps[0].operation.index = 1;
    EXPECT_THROW((void)changed.Replay(p), IR::Error);
    changed = ir;
    changed.steps[1].effects.atoms.at(2).type = T::Gold;
    EXPECT_THROW((void)changed.Replay(p), IR::Error);
    changed = ir;
    changed.steps[1].preconditions = IR::Initial(p);
    EXPECT_THROW((void)changed.Replay(p), IR::Error);
    auto foreign = before;
    ++foreign.nextId;
    EXPECT_THROW(ir.Append(p, foreign, {K::Discard, 0, {2}}), IR::Error);
}
TEST(ExecutionIRTest, ProjectionRetainsBondedMetalGeometryAndMasksWithFreshIdentity) {
    for (const std::uint8_t mask : {1, 2, 6, 8, 14}) {
        auto p = Puzzle({});
        p.inputs = {{{{T::Lead, {7, -3}}, {T::Quicksilver, {7, -4}}}, {{mask, {7, -3}, {7, -4}}}},
                    {{{T::Quicksilver, {0, 0}}}, {}}};
        p.permissionBits &= ~((1ULL << 8) | (1ULL << 9) | (1ULL << 11));
        IR ir;
        auto state = IR::Initial(p);
        Add(ir, p, state, K::Acquire);
        Add(ir, p, state, K::Acquire, {}, 1);
        Add(ir, p, state, K::Project, {1, 3});
        ASSERT_FALSE(state.atoms.contains(1));
        ASSERT_FALSE(state.atoms.contains(3));
        EXPECT_EQ(state.atoms.at(4).type, T::Tin);
        EXPECT_EQ(state.atoms.at(4).position, (core::HexCoord{7, -3}));
        EXPECT_EQ(state.atoms.at(2).type, T::Quicksilver);
        EXPECT_EQ(state.bonds.at({2, 4}), mask);
        EXPECT_EQ(IR::Fragment(state, 4), (std::vector<IR::AtomId>{2, 4}));
        EXPECT_EQ(ir.Replay(p), state);
        auto denied = p;
        denied.permissionBits &= ~(1ULL << 14);
        EXPECT_THROW((void)ir.Replay(denied), IR::PermissionFailure);
    }
}

TEST(ExecutionIRTest, ProjectionRejectsBondedMercuryWithoutConsumingMaterial) {
    auto p = Puzzle({});
    p.inputs = {{{{T::Lead, {}}, {T::Salt, {0, -1}}}, {{1, {}, {0, -1}}}},
                {{{T::Quicksilver, {}}, {T::Quicksilver, {1, 0}}}, {{14, {}, {1, 0}}}}};
    IR ir;
    auto state = IR::Initial(p);
    Add(ir, p, state, K::Acquire);
    Add(ir, p, state, K::Acquire, {}, 1);
    const auto before = state;
    EXPECT_THROW(Add(ir, p, state, K::Project, {1, 3}), IR::Error);
    EXPECT_EQ(state, before);
}

TEST(ExecutionIRTest, IncrementalConstructionChecksTailAndRequiresFullFinalReplay) {
    auto p = Puzzle({T::Salt});
    p.outputs = {{{{T::Salt, {0, 0}}}, {}}};
    IR ir;
    auto state = IR::Initial(p);
    auto foreignInitial = state;
    ++foreignInitial.nextId;
    EXPECT_THROW(ir.AppendFromTail(p, foreignInitial, {K::Acquire, 0, {}}), IR::Error);
    EXPECT_TRUE(ir.steps.empty());
    EXPECT_EQ(state, IR::Initial(p));
    EXPECT_THROW(ir.AppendFromTail(p, state, {K::Output, 0, {1}}), IR::Error);
    EXPECT_EQ(state, IR::Initial(p));
    EXPECT_TRUE(ir.steps.empty());
    ir.AppendFromTail(p, state, {K::Acquire, 0, {}});
    EXPECT_EQ(ir.steps.front().preconditions, IR::Initial(p));
    EXPECT_EQ(ir.steps.back().effects, state);
    const auto before = state;
    const auto stepsBefore = ir.steps;
    EXPECT_THROW(ir.AppendFromTail(p, state, {K::Output, 0, {999}}), IR::Error);
    EXPECT_EQ(state, before);
    EXPECT_EQ(ir.steps, stepsBefore);
    EXPECT_EQ(ir.steps.size(), 1U);
    auto foreign = state;
    ++foreign.discarded;
    EXPECT_THROW(ir.AppendFromTail(p, foreign, {K::Output, 0, {1}}), IR::Error);
    EXPECT_EQ(ir.steps.size(), 1U);
    ir.AppendFromTail(p, state, {K::Output, 0, {1}});
    IR ordinary;
    auto ordinaryState = IR::Initial(p);
    ordinary.Append(p, ordinaryState, {K::Acquire, 0, {}});
    ordinary.Append(p, ordinaryState, {K::Output, 0, {1}});
    EXPECT_EQ(state, ordinaryState);
    EXPECT_EQ(ir.steps, ordinary.steps);
    IR::RequireComplete(ir.Replay(p), {1});
    ir.steps.front().effects.atoms.at(1).type = T::Air;
    EXPECT_THROW((void)ir.Replay(p), IR::Error);
}

TEST(ExecutionIRTest, PhaseDiagnosticsPreserveTransactionAndHistoricalTamperRejection) {
    auto puzzle = Puzzle({T::Salt});
    puzzle.outputs = {{{{T::Salt, {0, 0}}}, {}}};
    IR reference;
    auto referenceState = IR::Initial(puzzle);
    for (bool enabled : {false, true}) {
        row_profile::Counters counters;
        struct Reset {
            ~Reset() {
                row_profile::active = nullptr;
            }
        } reset;
        row_profile::active = enabled ? &counters : nullptr;
        IR ledger;
        auto state = IR::Initial(puzzle);
        ledger.AppendFromTail(puzzle, state, {K::Acquire, 0, {}});
        const auto before = state;
        const auto steps = ledger.steps;
        EXPECT_THROW(ledger.AppendFromTail(puzzle, state, {K::Output, 0, {999}}), IR::Error);
        EXPECT_EQ(state, before);
        EXPECT_EQ(ledger.steps, steps);
        ledger.AppendFromTail(puzzle, state, {K::Output, 0, {1}});
        EXPECT_EQ(ledger.Replay(puzzle), state);
        if (!enabled) {
            reference = ledger;
            referenceState = state;
        }
        EXPECT_EQ(reference.steps, ledger.steps);
        EXPECT_EQ(referenceState, state);
        ledger.steps.front().operation.index = 99;
        EXPECT_THROW((void)ledger.Replay(puzzle), IR::Error);
        ledger = reference;
        ledger.steps.front().effects.atoms.at(1).type = T::Air;
        EXPECT_THROW((void)ledger.Replay(puzzle), IR::Error);
        if (enabled) {
            EXPECT_EQ(counters.phases[row_profile::Append].calls, 3U);
            EXPECT_EQ(counters.phases[row_profile::Replay].calls, 3U);
            EXPECT_GT(counters.phases[row_profile::ApplyLive].calls, 0U);
            EXPECT_GT(counters.phases[row_profile::ApplyReplay].calls, 0U);
        } else {
            for (const auto& entry : counters.phases)
                EXPECT_EQ(entry.calls, 0U);
        }
    }
}

TEST(ExecutionIRTest, BondedEndpointConversionUsesDeviceCatalystWithoutMaterialStock) {
    auto p = Puzzle({});
    p.inputs = {{{{T::Fire, {}}, {T::Fire, {-1, 1}}, {T::Fire, {1, 0}}, {T::Fire, {0, -1}}},
                 {{8, {}, {-1, 1}}, {8, {}, {1, 0}}, {8, {}, {0, -1}}}}};
    p.outputs = p.inputs;
    p.outputs[0].atoms[0].type = T::Water;
    IR ir;
    auto state = IR::Initial(p);
    Add(ir, p, state, K::Acquire);
    Add(ir, p, state, K::Calcify, {1});
    ASSERT_EQ(state.atoms.at(5).type, T::Salt);
    EXPECT_EQ(state.bonds.size(), 3U);
    IR::Operation duplicate{K::Duplicate, 0, {5}};
    duplicate.wheelCatalyst = IR::WheelCatalyst{7, T::Water, 3};
    ir.Append(p, state, duplicate);
    EXPECT_EQ(state.atoms.at(6).type, T::Water);
    EXPECT_EQ(state.atoms.at(6).position, (core::HexCoord{}));
    EXPECT_EQ(state.wheelDevices.at(7), 3);
    EXPECT_EQ(state.nextId, 7U);
    const auto before = state;
    EXPECT_THROW(Add(ir, p, state, K::Bond, {6, 2}, 0, 2), IR::Error);
    EXPECT_EQ(state, before);
    Add(ir, p, state, K::Output, {6});
    IR::RequireComplete(ir.Replay(p), {1});
    EXPECT_EQ(state.batches, (std::vector<std::size_t>{1}));
    EXPECT_EQ(state.discarded, 0U);
    EXPECT_TRUE(state.atoms.empty());
    for (int bit : {12, 13, 28}) {
        auto denied = p;
        denied.permissionBits &= ~(1ULL << bit);
        EXPECT_THROW((void)ir.Replay(denied), IR::PermissionFailure);
    }
    auto changed = ir;
    changed.steps[2].operation.wheelCatalyst->element = T::Quicksilver;
    EXPECT_THROW((void)changed.Replay(p), IR::Error);
}

TEST(ExecutionIRTest, StaticDevicePoseMismatchRollsBackWithoutConsumingSubstrate) {
    auto p = Puzzle({T::Salt, T::Salt});
    p.outputs = {{{{T::Water, {}}}, {}}, {{{T::Earth, {}}}, {}}};
    IR ir;
    auto state = IR::Initial(p);
    Add(ir, p, state, K::Acquire);
    Add(ir, p, state, K::Acquire, {}, 1);
    IR::Operation first{K::Duplicate, 0, {1}};
    first.wheelCatalyst = IR::WheelCatalyst{0, T::Water, 3};
    ir.Append(p, state, first);
    IR::Operation second{K::Duplicate, 0, {2}};
    second.wheelCatalyst = IR::WheelCatalyst{0, T::Earth, 4};
    const auto before = state;
    EXPECT_THROW(ir.Append(p, state, second), IR::Error);
    EXPECT_EQ(state, before);
    second.wheelCatalyst->orientation = 3;
    ir.Append(p, state, second);
    Add(ir, p, state, K::Output, {3});
    Add(ir, p, state, K::Output, {4}, 1);
    IR::RequireComplete(ir.Replay(p), {1, 1});
    EXPECT_EQ(state.wheelDevices.size(), 1U);
}

TEST(ExecutionIRTest, InvalidChemistryDoesNotConsumeAndBondedChemistryUnsupported) {
    auto p = Puzzle({T::Gold, T::Salt});
    IR ir;
    auto s = IR::Initial(p);
    Add(ir, p, s, K::Acquire);
    Add(ir, p, s, K::Acquire, {}, 1);
    const auto before = s;
    for (auto k :
         {K::Calcify, K::Disperse, K::Project, K::Purify, K::Animismus, K::Unify, K::Duplicate}) {
        EXPECT_THROW(Add(ir, p, s, k, {1, 2}), IR::Error);
        EXPECT_EQ(s, before);
    }
    auto bonded = Puzzle({});
    bonded.inputs.push_back({{{T::Air, {}}, {T::Salt, {1, 0}}}, {{1, {}, {1, 0}}}});
    IR other;
    auto stock = IR::Initial(bonded);
    Add(other, bonded, stock, K::Acquire);
    EXPECT_THROW(Add(other, bonded, stock, K::Disperse, {1}), IR::Error);
}
TEST(ExecutionIRTest, HonestUnsupportedAndMalformedMolecules) {
    auto p = Puzzle({T::Repeat});
    EXPECT_THROW((void)IR::Initial(p), IR::Unsupported);
    p = Puzzle({T::Salt});
    p.productionInfo.emplace();
    EXPECT_THROW((void)IR::Initial(p), IR::Unsupported);
    p = Puzzle({T::Salt});
    p.outputs.push_back({{{T::Salt, {}}, {T::Salt, {1, 0}}}, {}});
    EXPECT_THROW((void)IR::Initial(p), IR::Unsupported);
    p = Puzzle({T::Salt});
    p.inputs[0].bonds.push_back({1, {}, {1, 0}});
    EXPECT_THROW((void)IR::Initial(p), IR::Error);
}
TEST(ExecutionIRTest, ProjectedOutputOwnsStateAndChecksEntireHistory) {
    auto p = Puzzle({T::Salt});
    p.outputs = p.inputs;
    IR ir;
    auto state = IR::Initial(p);
    const IR::Operation output{K::Output, 0, {1}};
    EXPECT_THROW((void)ir.ProjectOutput(p, state, output), IR::Error);
    EXPECT_TRUE(ir.steps.empty());
    EXPECT_EQ(state, IR::Initial(p));
    Add(ir, p, state, K::Acquire);
    const auto before = state;
    const auto history = ir;
    for (bool diagnostic : {false, true}) {
        row_profile::Counters counters;
        row_profile::active = diagnostic ? &counters : nullptr;
        auto projected = ir.ProjectOutput(p, state, output);
        row_profile::active = nullptr;
        IR::RequireComplete(projected, {1});
        EXPECT_EQ(state, before);
        EXPECT_EQ(ir.Replay(p), history.Replay(p));
        EXPECT_EQ(ir.steps.size(), history.steps.size());
        auto owned = history;
        auto tail = before;
        owned.AppendFromTail(p, tail, output);
        EXPECT_EQ(projected, owned.Replay(p));
        auto invalid = output;
        invalid.index = 9;
        EXPECT_THROW((void)ir.ProjectOutput(p, state, invalid), IR::Error);
        invalid = {K::Acquire};
        EXPECT_THROW((void)ir.ProjectOutput(p, state, invalid), IR::Error);
        EXPECT_EQ(state, before);
    }
    auto wrong = before;
    ++wrong.nextId;
    EXPECT_THROW((void)ir.ProjectOutput(p, wrong, output), IR::Error);
    for (int field = 0; field != 3; ++field) {
        auto altered = history;
        if (field == 0) altered.steps[0].operation.index = 9;
        if (field == 1) ++altered.steps[0].preconditions.nextId;
        if (field == 2) ++altered.steps[0].effects.nextId;
        EXPECT_THROW((void)altered.ProjectOutput(p, before, output), IR::Error);
    }
    EXPECT_EQ(state, before);
}
TEST(ExecutionIRTest, ProjectedOutputRetainsFiniteAndCustodyRejections) {
    auto p = Puzzle({T::Salt});
    p.outputs = p.inputs;
    IR ir;
    auto state = IR::Initial(p);
    Add(ir, p, state, K::Acquire);
    FragmentCustody custody;
    custody.Grab(state, 0, {}, 1);
    const auto projected = ir.ProjectOutput(p, state, {K::Output, 0, {1}});
    EXPECT_THROW(custody.Validate(projected), IR::Error);
    IR::FiniteRetention missing{{1}, {{0, 2, T::Vitae}}, 6};
    EXPECT_THROW(IR::RequireFiniteTerminal(projected, missing), IR::Error);
    auto wrongStock = projected;
    wrongStock.atoms.emplace(2, IR::Atom{T::Vitae, {3, 0}});
    wrongStock.stored[0] = {99};
    EXPECT_THROW(IR::RequireFiniteTerminal(wrongStock, missing), IR::Error);
    EXPECT_EQ(ir.Replay(p), state);
}
TEST(ExecutionIRTest, ProjectedOutputDoesNotBypassOwningFinitePlanLateStoreCheck) {
    auto p = Puzzle({T::Salt});
    p.version = 3;
    p.outputMultiplier = 1;
    p.outputs = p.inputs;
    IR ir;
    auto state = IR::Initial(p);
    for (int i = 0; i < 6; ++i) {
        Add(ir, p, state, K::Acquire);
        Add(ir, p, state, K::Output, {state.nextId - 1});
    }
    Add(ir, p, state, K::Acquire);
    Add(ir, p, state, K::Acquire);
    Add(ir, p, state, K::Animismus, {7, 8});
    ir.Append(p, state, {K::Relocate, 0, {9}, 0, 0, {3, 0}});
    Add(ir, p, state, K::Store, {9}, 0);
    ir.Append(p, state, {K::Relocate, 0, {10}, 0, 0, {6, 0}});
    Add(ir, p, state, K::Store, {10}, 1);
    IR::FiniteRetention terminal{{6}, {{0, 9, T::Vitae}, {1, 10, T::Mors}}, 6};
    IR::RequireFiniteTerminal(ir.Replay(p), terminal);
    const char* prior = std::getenv("OPUSOPTIMA_FINITE_RETENTION");
    const std::string saved = prior ? prior : "";
    setenv("OPUSOPTIMA_FINITE_RETENTION", "1", 1);
    try {
        (void)MakeFiniteExecutionPlan(p, ir, terminal, io::VerificationMode::LegacyGate);
        FAIL() << "late retention Store accepted";
    } catch (const IR::Error& error) {
        EXPECT_STREQ(error.what(), "terminal retention Store must precede completion Output");
    }
    if (prior)
        setenv("OPUSOPTIMA_FINITE_RETENTION", saved.c_str(), 1);
    else
        unsetenv("OPUSOPTIMA_FINITE_RETENTION");
}
}} // namespace opusoptima::solvers
