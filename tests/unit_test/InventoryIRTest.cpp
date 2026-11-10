#include "InventoryIR.hpp"
#include <algorithm>
#include <gtest/gtest.h>

namespace opusoptima::solvers { namespace {
using IR = ExecutionIR;
io::PuzzleData Feed() {
    io::PuzzleData p;
    p.version = 3;
    p.outputMultiplier = 1;
    p.inputs = {{{{core::AtomType::Salt, {0, 0}}}, {}}};
    p.outputs = p.inputs;
    return p;
}
IR Transport(const io::PuzzleData& p) {
    IR program;
    auto state = IR::Initial(p);
    program.Append(p, state, {IR::Kind::Acquire, 0, {}});
    program.Append(p, state, {IR::Kind::Relocate, 0, {1}, 0, 0, {2, 0}});
    program.Append(p, state, {IR::Kind::Store, 7, {1}});
    program.Append(p, state, {IR::Kind::Retrieve, 7});
    program.Append(p, state, {IR::Kind::Relocate, 0, {1}, 0, 0, {2, 0}});
    program.Append(p, state, {IR::Kind::Output, 0, {1}});
    return program;
}
TEST(InventoryIR, WheelConversionRequiresExplicitSpatialDeviceBinding) {
    auto puzzle = Feed();
    puzzle.permissionBits = ~0ULL;
    puzzle.outputs[0].atoms[0].type = core::AtomType::Water;
    IR program;
    auto state = IR::Initial(puzzle);
    program.Append(puzzle, state, {IR::Kind::Acquire, 0, {}});
    IR::Operation duplicate{IR::Kind::Duplicate, 0, {1}};
    duplicate.wheelCatalyst = IR::WheelCatalyst{4, core::AtomType::Water, 0};
    program.Append(puzzle, state, duplicate);
    program.Append(puzzle, state, {IR::Kind::Relocate, 0, {2}, 0, 0, {2, 0}});
    program.Append(puzzle, state, {IR::Kind::Output, 0, {2}});
    const auto motion = [](const auto&, const auto& from, const auto& to, char action, bool) {
        return from.direction == 4 && to.direction == 4 &&
               (action == 'A' || action == 'a' || action == 'G' || action == 'g');
    };
    const auto glyph = [](const auto&, const auto&) { return true; };
    const std::vector<core::HexCoord> track{{0, 1}, {1, 1}, {2, 1}};
    EXPECT_THROW((void)LowerInventoryIR(puzzle, program, track, {0, 1, 4, 0}, motion, glyph),
                 IR::Unsupported);
    const auto lower = [&](core::HexFrame frame) {
        return LowerInventoryIR(puzzle, program, track, {0, 1, 4, 0}, motion, glyph, 1, 100, {}, {},
                                {}, false, {{4, frame}});
    };
    EXPECT_THROW((void)lower({{-1, -1}, 1}), IR::Unsupported);
    EXPECT_THROW((void)lower({{30, 30}, 0}), IR::Error);
    const auto physical = lower({{-1, -1}, 0});
    EXPECT_EQ(physical.Material(), program.Replay(puzzle));
    EXPECT_EQ(physical.Material().outputs, (std::vector<std::size_t>{1}));
    EXPECT_EQ(physical.Material().wheelDevices.at(4), 0);
}

TEST(InventoryIR, RejectsUncertifiedInitialSpawnBeforeMotion) {
    const auto p = Feed();
    const auto program = Transport(p);
    bool moved = false;
    int spawns = 0;
    const auto motion = [&](const auto&, const auto&, const auto&, char, bool) {
        moved = true;
        return true;
    };
    EXPECT_THROW((void)LowerInventoryIR(p, program, {{0, 1}, {1, 1}, {2, 1}, {3, 1}, {4, 1}},
                                        {0, 1, 4, 0}, motion,
                                        [&](const auto& step, const auto&) {
                                            EXPECT_EQ(step.operation.kind, IR::Kind::Acquire);
                                            EXPECT_TRUE(step.preconditions.atoms.empty());
                                            EXPECT_EQ(step.effects.atoms.size(), 1U);
                                            ++spawns;
                                            return false;
                                        }),
                 IR::Error);
    EXPECT_EQ(spawns, 1);
    EXPECT_FALSE(moved);
    EXPECT_THROW((void)LowerInventoryIR(p, program, {{0, 1}}, {0, 1, 4, 0}, motion, {}), IR::Error);
    EXPECT_FALSE(moved);
}
TEST(InventoryIR, ObserverReportsOnlyCommittedMaterialStepsAndPropagatesExceptions) {
    const auto p = Feed();
    IR program;
    auto state = IR::Initial(p);
    program.Append(p, state, {IR::Kind::Acquire, 0, {}});
    program.Append(p, state, {IR::Kind::Output, 0, {1}});
    std::vector<std::size_t> observed;
    const auto motion = [](const auto&, const auto&, const auto&, char, bool) { return true; };
    const auto glyph = [](const auto&, const auto&) { return true; };
    const auto observer = [&](std::size_t step, const PhysicalInventory& inventory) {
        observed.push_back(step);
        EXPECT_EQ(inventory.Material().outputs, program.steps[step].effects.outputs);
    };
    const auto lowered = LowerInventoryIR(p, program, {{0, 1}}, {0, 1, 4, 0}, motion, glyph, 1,
                                          20000, {}, {}, observer);
    EXPECT_EQ(lowered.Material(), program.Replay(p));
    EXPECT_EQ(observed, (std::vector<std::size_t>{0, 1}));
    observed.clear();
    EXPECT_THROW((void)LowerInventoryIR(
                     p, program, {{0, 1}}, {0, 1, 4, 0}, motion,
                     [](const auto&, const auto&) { return false; }, 1, 20000, {}, {}, observer),
                 IR::Error);
    EXPECT_TRUE(observed.empty());
    EXPECT_THROW((void)LowerInventoryIR(p, program, {{0, 1}}, {0, 1, 4, 0}, motion, glyph, 1, 20000,
                                        {}, {},
                                        [](std::size_t, const PhysicalInventory&) {
                                            throw std::runtime_error("observer failed");
                                        }),
                 std::runtime_error);
}
TEST(InventoryIR, FailedPhysicalDeliveryCarriesAtomOperationAndTarget) {
    const auto p = Feed();
    const auto program = Transport(p);
    try {
        (void)LowerInventoryIR(
            p, program, {{0, 1}}, {0, 1, 4, 0},
            [](const auto&, const auto&, const auto&, char, bool) { return true; },
            [](const auto&, const auto&) { return true; });
        FAIL() << "unreachable delivery accepted";
    } catch (const IR::PhysicalFailure& failure) {
        EXPECT_EQ(failure.step, 1U);
        EXPECT_EQ(failure.atom, 1U);
        EXPECT_EQ(failure.destination, (core::HexCoord{2, 0}));
        EXPECT_FALSE(failure.exhausted);
    }
}
TEST(InventoryIR, RequiresExplicitCountsForMultipleProductPeriods) {
    const auto p = Feed();
    IR program;
    auto state = IR::Initial(p);
    for (int batch = 0; batch < 2; ++batch) {
        const auto id = state.nextId;
        program.Append(p, state, {IR::Kind::Acquire});
        program.Append(p, state, {IR::Kind::Output, 0, {id}});
    }
    bool certified = false;
    const auto motion = [&](const auto&, const auto&, const auto&, char, bool) {
        certified = true;
        return true;
    };
    const auto glyph = [&](const auto&, const auto&) {
        certified = true;
        return true;
    };
    EXPECT_THROW((void)LowerInventoryIR(p, program, {{0, 1}}, {0, 1, 4, 0}, motion, glyph),
                 IR::Error);
    EXPECT_FALSE(certified);
    const auto inventory =
        LowerInventoryIR(p, program, {{0, 1}}, {0, 1, 4, 0}, motion, glyph, 1, 20000, {2});
    EXPECT_EQ(inventory.Material(), program.Replay(p));
    EXPECT_TRUE(inventory.Custody().Held().empty());
    EXPECT_TRUE(inventory.Custody().Parked().empty());
    EXPECT_THROW(
        (void)LowerInventoryIR(p, program, {{0, 1}}, {0, 1, 4, 0}, motion, glyph, 1, 20000, {3}),
        IR::Error);
}
TEST(InventoryIR, ReleasedCoproductsKeepIndependentWorldFrames) {
    auto p = Feed();
    p.permissionBits = 1ULL << 16;
    p.inputs.push_back({{{core::AtomType::Salt, {1, 0}}}, {}});
    p.outputs = {{{{core::AtomType::Vitae, {}}}, {}}, {{{core::AtomType::Mors, {}}}, {}}};
    IR program;
    auto state = IR::Initial(p);
    program.Append(p, state, {IR::Kind::Acquire, 0});
    program.Append(p, state, {IR::Kind::Acquire, 1});
    program.Append(p, state, {IR::Kind::Animismus, 0, {1, 2}});
    program.Append(p, state, {IR::Kind::Output, 0, {3}});
    program.Append(p, state, {IR::Kind::Output, 1, {4}});
    const std::vector<core::HexCoord> track{{0, 1}, {1, 1}, {2, 1}, {3, 1}};
    const auto motion = [](const auto&, const auto&, const auto&, char, bool) { return true; };
    const auto glyph = [](const auto&, const auto&) { return true; };
    EXPECT_THROW((void)LowerInventoryIR(p, program, track, {0, 1, 4, 0}, motion, glyph),
                 IR::Unsupported);
    const auto inventory = LowerInventoryIR(p, program, track, {0, 1, 4, 0}, motion, glyph, 1,
                                            20000, {}, {{2, {{2, 0}, {3, 0}}}});
    EXPECT_EQ(inventory.Material(), program.Replay(p));
    EXPECT_TRUE(inventory.Custody().Held().empty());
    EXPECT_TRUE(inventory.Custody().Parked().empty());
    EXPECT_THROW((void)LowerInventoryIR(p, program, track, {0, 1, 4, 0}, motion, glyph, 1, 20000,
                                        {}, {{2, {{2, 0}}}}),
                 IR::Error);
    EXPECT_THROW((void)LowerInventoryIR(p, program, track, {0, 1, 4, 0}, motion,
                                        [](const auto& step, const auto&) {
                                            return step.operation.kind != IR::Kind::Animismus;
                                        },
                                        1, 20000, {}, {{2, {{2, 0}, {3, 0}}}}),
                 IR::Error);
}
TEST(InventoryIR, DuplicationReusesParkedCatalystAcrossInputBatches) {
    auto p = Feed();
    p.permissionBits = (1ULL << 13) | (1ULL << 17);
    p.inputs.push_back({{{core::AtomType::Air, {2, 0}}}, {}});
    p.outputs[0].atoms[0].type = core::AtomType::Air;
    IR program;
    auto state = IR::Initial(p);
    program.Append(p, state, {IR::Kind::Acquire});
    program.Append(p, state, {IR::Kind::Acquire, 1});
    for (int batch = 0; batch < 2; ++batch) {
        if (batch) program.Append(p, state, {IR::Kind::Acquire});
        const auto salt = batch ? 4U : 1U;
        const auto product = state.nextId;
        program.Append(p, state, {IR::Kind::Duplicate, 0, {salt, 2}});
        program.Append(p, state, {IR::Kind::Output, 0, {product}});
    }
    program.Append(p, state, {IR::Kind::Discard, 0, {2}});
    const auto motion = [](const auto&, const auto& from, const auto& to, char op, bool) {
        return from.direction == 4 && to.direction == 4 &&
               (op == 'A' || op == 'a' || op == 'G' || op == 'g');
    };
    int reactions = 0;
    const auto glyph = [&](const auto& step, const auto&) {
        if (step.operation.kind != IR::Kind::Duplicate) return true;
        ++reactions;
        return step.effects.atoms.at(2) == step.preconditions.atoms.at(2) &&
               step.effects.atoms.at(step.preconditions.nextId).position == core::HexCoord{0, 0};
    };
    const auto inventory = LowerInventoryIR(p, program, {{0, 1}, {1, 1}, {2, 1}}, {0, 1, 4, 0},
                                            motion, glyph, 1, 20000, {2});
    EXPECT_EQ(reactions, 2);
    EXPECT_EQ(inventory.Material(), program.Replay(p));
    EXPECT_TRUE(inventory.Custody().Held().empty());
    EXPECT_TRUE(inventory.Custody().Parked().empty());
    EXPECT_THROW((void)LowerInventoryIR(p, program, {{0, 1}, {1, 1}, {2, 1}}, {0, 1, 4, 0}, motion,
                                        [](const auto& step, const auto&) {
                                            return step.operation.kind != IR::Kind::Duplicate;
                                        },
                                        1, 20000, {2}),
                 IR::Error);
}
TEST(InventoryIR, CalcificationMapsProductOriginBackToWorldAndRelocationResolvesFrame) {
    auto p = Feed();
    p.permissionBits = 1ULL << 12;
    p.inputs[0].atoms[0].type = core::AtomType::Air;
    IR program;
    auto state = IR::Initial(p);
    program.Append(p, state, {IR::Kind::Acquire});
    program.Append(p, state, {IR::Kind::Relocate, 0, {1}, 0, 0, {4, 0}});
    program.Append(p, state, {IR::Kind::Calcify, 0, {1}});
    program.Append(p, state, {IR::Kind::Store, 7, {2}});
    program.Append(p, state, {IR::Kind::Retrieve, 7});
    program.Append(p, state, {IR::Kind::Relocate, 0, {2}, 0, 0, {8, 0}});
    program.Append(p, state, {IR::Kind::Output, 0, {2}});
    std::vector<core::HexCoord> track;
    for (int q = 0; q <= 8; ++q)
        track.push_back({q, 1});
    const auto motion = [](const auto&, const auto& from, const auto& to, char op, bool) {
        return from.direction == 4 && to.direction == 4 &&
               (op == 'A' || op == 'a' || op == 'G' || op == 'g');
    };
    const auto glyph = [](const auto& step, const auto&) {
        if (step.operation.kind != IR::Kind::Calcify) return true;
        return step.effects.atoms.at(step.preconditions.nextId).position == core::HexCoord{4, 0};
    };
    const auto inventory = LowerInventoryIR(p, program, track, {0, 1, 4, 0}, motion, glyph);
    EXPECT_EQ(inventory.Material(), program.Replay(p));
    EXPECT_TRUE(inventory.Custody().Held().empty());
    EXPECT_TRUE(inventory.Custody().Parked().empty());
    EXPECT_THROW((void)LowerInventoryIR(p, program, track, {0, 1, 4, 0}, motion,
                                        [](const auto& step, const auto&) {
                                            return step.operation.kind != IR::Kind::Calcify;
                                        }),
                 IR::Error);
}
TEST(InventoryIR, CalcifiesInitiallyParkedStockAndOutputsWithoutResolvingFrame) {
    auto p = Feed();
    p.permissionBits = 1ULL << 12;
    p.inputs[0].atoms[0].type = core::AtomType::Air;
    p.inputs[0].atoms[0].position = {4, 0};
    IR program;
    auto state = IR::Initial(p);
    program.Append(p, state, {IR::Kind::Acquire});
    program.Append(p, state, {IR::Kind::Calcify, 0, {1}});
    program.Append(p, state, {IR::Kind::Store, 7, {2}});
    program.Append(p, state, {IR::Kind::Retrieve, 7});
    program.Append(p, state, {IR::Kind::Output, 0, {2}});
    std::vector<core::HexCoord> track;
    for (int q = 0; q <= 4; ++q)
        track.push_back({q, 1});
    const auto motion = [](const auto&, const auto& from, const auto& to, char op, bool) {
        return from.direction == 4 && to.direction == 4 &&
               (op == 'A' || op == 'a' || op == 'G' || op == 'g');
    };
    const auto glyph = [](const auto& step, const auto&) {
        if (step.operation.kind == IR::Kind::Calcify)
            return step.effects.atoms.at(step.preconditions.nextId).position ==
                   core::HexCoord{4, 0};
        if (step.operation.kind == IR::Kind::Output)
            return step.preconditions.atoms.at(2).position == core::HexCoord{4, 0};
        return true;
    };
    const auto inventory = LowerInventoryIR(p, program, track, {0, 1, 4, 0}, motion, glyph);
    EXPECT_EQ(inventory.Material(), program.Replay(p));
    EXPECT_TRUE(inventory.Custody().Held().empty());
    EXPECT_TRUE(inventory.Custody().Parked().empty());
    EXPECT_THROW((void)LowerInventoryIR(p, program, track, {0, 1, 4, 0}, motion,
                                        [](const auto& step, const auto&) {
                                            return step.operation.kind != IR::Kind::Calcify;
                                        }),
                 IR::Error);
}
TEST(InventoryIR, ParkedCalcificationPreservesRequestedBondedEndpoint) {
    auto p = Feed();
    p.permissionBits = 1ULL << 12;
    p.inputs = {
        {{{core::AtomType::Air, {0, 0}}, {core::AtomType::Air, {1, 0}}}, {{1, {0, 0}, {1, 0}}}}};
    p.outputs = p.inputs;
    p.outputs[0].atoms[1].type = core::AtomType::Salt;
    IR program;
    auto state = IR::Initial(p);
    program.Append(p, state, {IR::Kind::Acquire});
    program.Append(p, state, {IR::Kind::Calcify, 0, {2}});
    program.Append(p, state, {IR::Kind::Output, 0, {1}});
    std::vector<IR::AtomId> calcified;
    const auto inventory = LowerInventoryIR(
        p, program, {{0, 1}, {1, 1}}, {0, 1, 4, 0},
        [](const auto&, const auto& from, const auto& to, char op, bool) {
            return from.direction == 4 && to.direction == 4 &&
                   (op == 'A' || op == 'a' || op == 'G' || op == 'g');
        },
        [&](const auto& step, const auto&) {
            if (step.operation.kind == IR::Kind::Calcify)
                calcified.push_back(step.operation.atoms.front());
            return true;
        });
    EXPECT_EQ(calcified, (std::vector<IR::AtomId>{2}));
    EXPECT_EQ(inventory.Material(), program.Replay(p));
    EXPECT_TRUE(inventory.Custody().Held().empty());
    EXPECT_TRUE(inventory.Custody().Parked().empty());
}
TEST(InventoryIR, CalcifiedStockRejoinsParkedPartnerAfterFrameResolution) {
    auto p = Feed();
    p.permissionBits = (1ULL << 8) | (1ULL << 9) | (1ULL << 12);
    p.inputs = {
        {{{core::AtomType::Air, {4, 0}}, {core::AtomType::Salt, {5, 0}}}, {{1, {4, 0}, {5, 0}}}}};
    p.outputs = p.inputs;
    p.outputs[0].atoms[0].type = core::AtomType::Salt;
    IR program;
    auto state = IR::Initial(p);
    program.Append(p, state, {IR::Kind::Acquire, 0, {}});
    program.Append(p, state, {IR::Kind::Unbond, 0, {1, 2}, 1});
    program.Append(p, state, {IR::Kind::Calcify, 0, {1}});
    program.Append(p, state, {IR::Kind::Relocate, 0, {3}, 0, 0, {4, 0}});
    program.Append(p, state, {IR::Kind::Bond, 0, {3, 2}, 1});
    program.Append(p, state, {IR::Kind::Relocate, 0, {3}, 0, 0, {3, 0}});
    program.Append(p, state, {IR::Kind::Output, 0, {3}});
    std::vector<core::HexCoord> track;
    for (int q = 0; q <= 9; ++q)
        track.push_back({q, 1});
    const auto inventory = LowerInventoryIR(
        p, program, track, {0, 1, 4, 0},
        [](const auto&, const auto& from, const auto& to, char op, bool) {
            return from.direction == 4 && to.direction == 4 &&
                   (op == 'A' || op == 'a' || op == 'G' || op == 'g');
        },
        [](const auto& step, const auto&) {
            return step.operation.kind != IR::Kind::Calcify ||
                   step.effects.atoms.at(step.preconditions.nextId).position ==
                       core::HexCoord{4, 0};
        });
    EXPECT_EQ(inventory.Material(), program.Replay(p));
    EXPECT_TRUE(inventory.Custody().Held().empty());
    EXPECT_TRUE(inventory.Custody().Parked().empty());
}
TEST(InventoryIR, AlignsChemicalFrameBeforeJoiningWithoutExplicitProductRelocation) {
    auto p = Feed();
    p.permissionBits = (1ULL << 8) | (1ULL << 9) | (1ULL << 12);
    p.inputs = {{{{core::AtomType::Air, {-4, 0}}, {core::AtomType::Salt, {-3, 0}}},
                 {{1, {-4, 0}, {-3, 0}}}}};
    p.outputs = p.inputs;
    p.outputs[0].atoms[0].type = core::AtomType::Salt;
    IR program;
    auto state = IR::Initial(p);
    program.Append(p, state, {IR::Kind::Acquire});
    program.Append(p, state, {IR::Kind::Unbond, 0, {1, 2}, 1});
    program.Append(p, state, {IR::Kind::Calcify, 0, {1}});
    program.Append(p, state, {IR::Kind::Relocate, 0, {2}, 0, 0, {4, 0}});
    program.Append(p, state, {IR::Kind::Bond, 0, {3, 2}, 1});
    program.Append(p, state, {IR::Kind::Relocate, 0, {3}, 0, 0, {2, 0}});
    program.Append(p, state, {IR::Kind::Output, 0, {3}});
    std::vector<core::HexCoord> track;
    for (int q = -4; q <= 3; ++q)
        track.push_back({q, 1});
    bool joinedAligned = false;
    const auto inventory = LowerInventoryIR(
        p, program, track, {0, 1, 4, 0},
        [](const auto&, const auto& from, const auto& to, char op, bool) {
            return from.direction == 4 && to.direction == 4 &&
                   (op == 'A' || op == 'a' || op == 'G' || op == 'g');
        },
        [&](const auto& step, const auto&) {
            if (step.operation.kind == IR::Kind::Bond) {
                joinedAligned = step.preconditions.atoms.at(3).position == core::HexCoord{0, 0} &&
                                step.preconditions.atoms.at(2).position == core::HexCoord{1, 0};
                return joinedAligned;
            }
            return true;
        });
    EXPECT_TRUE(joinedAligned);
    EXPECT_EQ(inventory.Material(), program.Replay(p));
    EXPECT_TRUE(inventory.Custody().Held().empty());
    EXPECT_TRUE(inventory.Custody().Parked().empty());
}
TEST(InventoryIR, CutRejoinTransportsMergedStockAndRetriesOppositeHeldOperand) {
    auto p = Feed();
    p.permissionBits = (1ULL << 8) | (1ULL << 9);
    p.inputs[0].atoms.push_back({core::AtomType::Air, {1, 0}});
    p.inputs[0].bonds.push_back({1, {0, 0}, {1, 0}});
    p.outputs = p.inputs;
    IR program;
    auto state = IR::Initial(p);
    program.Append(p, state, {IR::Kind::Acquire});
    program.Append(p, state, {IR::Kind::Unbond, 0, {1, 2}, 1});
    program.Append(p, state, {IR::Kind::Bond, 0, {1, 2}, 1});
    program.Append(p, state, {IR::Kind::Relocate, 0, {1}, 0, 0, {2, 0}});
    program.Append(p, state, {IR::Kind::Output, 0, {1}});
    const auto inventory = LowerInventoryIR(
        p, program, {{0, 1}, {1, 1}, {2, 1}, {3, 1}}, {0, 1, 4, 0},
        [](const auto&, const auto& from, const auto& to, char op, bool) {
            return from.direction == 4 && to.direction == 4 &&
                   (op == 'A' || op == 'a' || op == 'G' || op == 'g');
        },
        [](const auto& step, const auto& arm) {
            return step.operation.kind != IR::Kind::Bond || arm.trackIndex == 1;
        });
    EXPECT_EQ(inventory.Material(), program.Replay(p));
    EXPECT_TRUE(inventory.Custody().Parked().empty());
    EXPECT_TRUE(inventory.Custody().Held().empty());
}
TEST(InventoryIR, MergeRetriesAnotherPoseOfSameOperand) {
    auto p = Feed();
    p.permissionBits = (1ULL << 8) | (1ULL << 9);
    p.inputs[0].atoms.push_back({core::AtomType::Air, {1, 0}});
    p.inputs[0].bonds.push_back({1, {0, 0}, {1, 0}});
    p.outputs = p.inputs;
    IR program;
    auto state = IR::Initial(p);
    program.Append(p, state, {IR::Kind::Acquire});
    program.Append(p, state, {IR::Kind::Unbond, 0, {1, 2}, 1});
    program.Append(p, state, {IR::Kind::Bond, 0, {1, 2}, 1});
    program.Append(p, state, {IR::Kind::Output, 0, {1}});
    const auto inventory = LowerInventoryIR(
        p, program, {{0, 1}, {-1, 1}}, {0, 1, 4, 0},
        [](const auto&, const auto&, const auto&, char, bool) { return true; },
        [](const auto& step, const auto& arm) {
            return step.operation.kind != IR::Kind::Bond || arm.trackIndex == 1;
        });
    EXPECT_EQ(inventory.Material(), program.Replay(p));
    EXPECT_TRUE(inventory.Custody().Parked().empty());
    EXPECT_TRUE(inventory.Custody().Held().empty());
}
TEST(InventoryIR, AcquiresLaterInputAfterConsumingFirstBatch) {
    auto p = Feed();
    p.inputs.push_back({{{core::AtomType::Air, {0, 0}}}, {}});
    p.outputs = p.inputs;
    IR program;
    auto state = IR::Initial(p);
    program.Append(p, state, {IR::Kind::Acquire, 0});
    program.Append(p, state, {IR::Kind::Output, 0, {1}});
    program.Append(p, state, {IR::Kind::Acquire, 1});
    program.Append(p, state, {IR::Kind::Output, 1, {2}});
    const auto motion = [](const auto&, const auto&, const auto&, char, bool) { return true; };
    const auto inventory = LowerInventoryIR(p, program, {{0, 1}}, {0, 1, 4, 0}, motion,
                                            [](const auto&, const auto&) { return true; });
    EXPECT_EQ(inventory.Material(), program.Replay(p));
    EXPECT_EQ(inventory.Material().batches, (std::vector<std::size_t>{1, 1}));
    EXPECT_THROW((void)LowerInventoryIR(p, program, {{0, 1}}, {0, 1, 4, 0}, motion,
                                        [](const auto& step, const auto&) {
                                            return step.operation.kind != IR::Kind::Acquire;
                                        }),
                 IR::Error);
}
TEST(InventoryIR, DiscardsWasteBeforeAcquiringOutputBatch) {
    auto p = Feed();
    p.permissionBits |= 1ULL << 17;
    IR program;
    auto state = IR::Initial(p);
    program.Append(p, state, {IR::Kind::Acquire, 0});
    program.Append(p, state, {IR::Kind::Discard, 0, {1}});
    program.Append(p, state, {IR::Kind::Acquire, 0});
    program.Append(p, state, {IR::Kind::Output, 0, {2}});
    const auto motion = [](const auto&, const auto&, const auto&, char, bool) { return true; };
    const auto inventory = LowerInventoryIR(p, program, {{0, 1}}, {0, 1, 4, 0}, motion,
                                            [](const auto&, const auto&) { return true; });
    EXPECT_EQ(inventory.Material(), program.Replay(p));
    EXPECT_EQ(inventory.Material().discarded, 1u);
    EXPECT_TRUE(inventory.Custody().Parked().empty());
    EXPECT_TRUE(inventory.Custody().Held().empty());
    EXPECT_THROW((void)LowerInventoryIR(p, program, {{0, 1}}, {0, 1, 4, 0}, motion,
                                        [](const auto& step, const auto&) {
                                            return step.operation.kind != IR::Kind::Discard;
                                        }),
                 IR::Error);
}
TEST(InventoryIR, LowersLogicalStorageWithoutLeakingTemporaryCustody) {
    const auto p = Feed();
    const auto program = Transport(p);
    const HeldArmState start{0, 1, 4, 0};
    const auto inventory = LowerInventoryIR(
        p, program, {{0, 1}, {1, 1}, {2, 1}, {3, 1}, {4, 1}}, start,
        [](const auto&, const auto& from, const auto& to, char op, bool) {
            return from.direction == 4 && to.direction == 4 &&
                   (op == 'A' || op == 'a' || op == 'G' || op == 'g');
        },
        [](const auto&, const auto&) { return true; });
    EXPECT_EQ(inventory.Material(), program.Replay(p));
    EXPECT_TRUE(inventory.Custody().Parked().empty());
    EXPECT_TRUE(inventory.Custody().Held().empty());
    EXPECT_EQ(inventory.Arm(), start);
}
TEST(InventoryIR, RejectsDisconnectedFeedInsteadOfAssumingIndependentPhysicalFragments) {
    auto p = Feed();
    p.inputs[0].atoms.push_back({core::AtomType::Air, {1, 0}});
    p.outputs = {p.outputs[0], {{{core::AtomType::Air, {0, 0}}}, {}}};
    IR program;
    auto state = IR::Initial(p);
    program.Append(p, state, {IR::Kind::Acquire, 0, {}});
    program.Append(p, state, {IR::Kind::Output, 0, {1}});
    program.Append(p, state, {IR::Kind::Output, 1, {2}});
    EXPECT_THROW((void)LowerInventoryIR(
                     p, program, {{0, 1}}, {0, 1, 4, 0},
                     [](const auto&, const auto&, const auto&, char, bool) { return true; },
                     [](const auto&, const auto&) { return true; }),
                 IR::Unsupported);
}
TEST(InventoryIR, OutputRetriesPickupWhenReleaseRejectsFirstPose) {
    const auto p = Feed();
    IR program;
    auto state = IR::Initial(p);
    program.Append(p, state, {IR::Kind::Acquire, 0, {}});
    program.Append(p, state, {IR::Kind::Output, 0, {1}});
    const auto inventory = LowerInventoryIR(
        p, program, {{0, 1}, {1, 0}}, {0, 1, 4, 0},
        [](const auto&, const auto&, const auto&, char, bool) { return true; },
        [](const auto& step, const auto& arm) {
            return step.operation.kind == IR::Kind::Acquire || arm.trackIndex == 1;
        });
    EXPECT_EQ(inventory.Material(), program.Replay(p));
    EXPECT_EQ(std::count(inventory.Tape().begin(), inventory.Tape().end(), 'G'), 1);
    EXPECT_EQ(std::count(inventory.Tape().begin(), inventory.Tape().end(), 'g'), 1);
    EXPECT_TRUE(inventory.Custody().Held().empty());
    EXPECT_TRUE(inventory.Custody().Parked().empty());
}
TEST(InventoryIR, HeldOutputRetriesByParkingAndChangingPickupPose) {
    const auto p = Feed();
    IR program;
    auto state = IR::Initial(p);
    program.Append(p, state, {IR::Kind::Acquire, 0, {}});
    program.Append(p, state, {IR::Kind::Relocate, 0, {1}, 0, 0, {2, 0}});
    program.Append(p, state, {IR::Kind::Output, 0, {1}});
    int rejected = 0;
    const auto inventory = LowerInventoryIR(
        p, program, {{0, 1}, {1, 1}, {2, 1}, {3, 0}}, {0, 1, 4, 0},
        [](const auto&, const auto&, const auto&, char, bool) { return true; },
        [&](const auto& step, const auto& arm) {
            if (step.operation.kind == IR::Kind::Acquire) return true;
            if (arm.trackIndex != 3) {
                ++rejected;
                return false;
            }
            return true;
        });
    EXPECT_GT(rejected, 0);
    EXPECT_EQ(inventory.Material(), program.Replay(p));
    EXPECT_EQ(std::count(inventory.Tape().begin(), inventory.Tape().end(), 'G'), 2);
    EXPECT_EQ(std::count(inventory.Tape().begin(), inventory.Tape().end(), 'g'), 2);
    EXPECT_TRUE(inventory.Custody().Held().empty());
    EXPECT_TRUE(inventory.Custody().Parked().empty());
}
TEST(InventoryIR, ConsecutiveRelocationsKeepCertifiedGripWithoutExtraReleases) {
    const auto p = Feed();
    IR program;
    auto state = IR::Initial(p);
    program.Append(p, state, {IR::Kind::Acquire});
    program.Append(p, state, {IR::Kind::Relocate, 0, {1}, 0, 0, {2, 0}});
    program.Append(p, state, {IR::Kind::Relocate, 0, {1}, 0, 0, {2, 0}});
    program.Append(p, state, {IR::Kind::Output, 0, {1}});
    const auto inventory = LowerInventoryIR(
        p, program, {{0, 1}, {1, 1}, {2, 1}, {3, 1}, {4, 1}}, {0, 1, 4, 0},
        [](const auto&, const auto& from, const auto& to, char op, bool) {
            return from.direction == 4 && to.direction == 4 &&
                   (op == 'A' || op == 'a' || op == 'G' || op == 'g');
        },
        [](const auto&, const auto&) { return true; });
    EXPECT_EQ(inventory.Material(), program.Replay(p));
    EXPECT_EQ(std::count(inventory.Tape().begin(), inventory.Tape().end(), 'G'), 1);
    EXPECT_EQ(std::count(inventory.Tape().begin(), inventory.Tape().end(), 'g'), 1);
}
TEST(InventoryIR, ConsecutiveRelocationsUseActualGripRatherThanIRAnchor) {
    auto p = Feed();
    p.inputs[0].atoms.push_back({core::AtomType::Fire, {0, 1}});
    p.inputs[0].bonds.push_back({1, {0, 0}, {0, 1}});
    p.outputs = p.inputs;
    IR program;
    auto state = IR::Initial(p);
    program.Append(p, state, {IR::Kind::Acquire});
    program.Append(p, state, {IR::Kind::Relocate, 0, {1}, 0, 0, {2, 0}});
    program.Append(p, state, {IR::Kind::Relocate, 0, {1}, 0, 0, {2, 0}});
    program.Append(p, state, {IR::Kind::Output, 0, {1}});
    const auto inventory = LowerInventoryIR(
        p, program, {{0, 2}, {1, 2}, {2, 2}, {3, 2}, {4, 2}}, {0, 1, 4, 0},
        [](const auto&, const auto& from, const auto& to, char op, bool) {
            return from.direction == 4 && to.direction == 4 &&
                   (op == 'A' || op == 'a' || op == 'G' || op == 'g');
        },
        [](const auto&, const auto&) { return true; });
    EXPECT_EQ(inventory.Material(), program.Replay(p));
    EXPECT_EQ(std::count(inventory.Tape().begin(), inventory.Tape().end(), 'G'), 1);
    EXPECT_EQ(std::count(inventory.Tape().begin(), inventory.Tape().end(), 'g'), 1);
}
TEST(InventoryIR, ConsecutiveRelocationsApplyRelativeRotationAtActualGrip) {
    auto p = Feed();
    p.inputs[0].atoms.push_back({core::AtomType::Fire, {1, 0}});
    p.inputs[0].bonds.push_back({1, {0, 0}, {1, 0}});
    p.outputs = p.inputs;
    IR program;
    auto state = IR::Initial(p);
    program.Append(p, state, {IR::Kind::Acquire});
    program.Append(p, state, {IR::Kind::Relocate, 0, {2}, 0, 0, {2, 0}});
    program.Append(p, state, {IR::Kind::Relocate, 0, {2}, 0, 1, {4, 0}});
    program.Append(p, state, {IR::Kind::Output, 0, {2}});
    const auto inventory = LowerInventoryIR(
        p, program, {{0, 1}, {1, 1}, {2, 1}, {3, 1}, {4, 1}}, {0, 1, 4, 0},
        [](const auto&, const auto&, const auto&, char, bool) { return true; },
        [](const auto&, const auto&) { return true; }, 2);
    EXPECT_EQ(inventory.Material(), program.Replay(p));
    EXPECT_EQ(std::count(inventory.Tape().begin(), inventory.Tape().end(), 'G'), 1);
    EXPECT_EQ(std::count(inventory.Tape().begin(), inventory.Tape().end(), 'g'), 1);
}
TEST(InventoryIR, HeldRelocationFallsBackToChangingPickupPose) {
    const auto p = Feed();
    IR program;
    auto state = IR::Initial(p);
    program.Append(p, state, {IR::Kind::Acquire});
    program.Append(p, state, {IR::Kind::Relocate, 0, {1}, 0, 0, {2, 0}});
    program.Append(p, state, {IR::Kind::Relocate, 0, {1}, 0, 0, {2, 0}});
    program.Append(p, state, {IR::Kind::Output, 0, {1}});
    int rejected = 0;
    const auto inventory = LowerInventoryIR(
        p, program, {{0, 1}, {1, 1}, {2, 1}, {3, 0}, {4, 0}, {5, 0}}, {0, 1, 4, 0},
        [&](const auto& material, const auto& from, const auto& to, char op, bool holding) {
            if (holding && material.atoms.at(1).position.q >= 2 && from.direction == 4 &&
                op != 'g') {
                ++rejected;
                return false;
            }
            return !holding || op == 'g' || to.direction == from.direction;
        },
        [](const auto&, const auto&) { return true; }, 2);
    EXPECT_GT(rejected, 0);
    EXPECT_EQ(inventory.Material(), program.Replay(p));
    EXPECT_EQ(std::count(inventory.Tape().begin(), inventory.Tape().end(), 'G'), 2);
    EXPECT_EQ(std::count(inventory.Tape().begin(), inventory.Tape().end(), 'g'), 2);
}
TEST(InventoryIR, RejectsTamperedSnapshotsBeforeCallingCertificates) {
    const auto p = Feed();
    auto program = Transport(p);
    program.steps[1].effects.atoms.at(1).position = {9, 9};
    int calls = 0;
    EXPECT_THROW((void)LowerInventoryIR(
                     p, program, {{0, 1}}, {0, 1, 4, 0},
                     [&](const auto&, const auto&, const auto&, char, bool) {
                         ++calls;
                         return true;
                     },
                     [&](const auto&, const auto&) {
                         ++calls;
                         return true;
                     }),
                 IR::Error);
    EXPECT_EQ(calls, 0);
}
TEST(InventoryIR, RejectsUncertifiedOutputAndExhaustedRoutes) {
    const auto p = Feed();
    const auto program = Transport(p);
    const std::vector<core::HexCoord> track{{0, 1}, {1, 1}, {2, 1}, {3, 1}, {4, 1}};
    auto motion = [](const auto&, const auto&, const auto&, char, bool) { return true; };
    auto glyph = [](const auto&, const auto&) { return true; };
    EXPECT_THROW((void)LowerInventoryIR(p, program, track, {0, 1, 4, 0}, motion,
                                        [](const auto&, const auto&) { return false; }),
                 IR::Error);
    EXPECT_THROW((void)LowerInventoryIR(p, program, track, {0, 1, 4, 0}, motion, glyph, 1, 0),
                 IR::Unsupported);
}
}} // namespace opusoptima::solvers
