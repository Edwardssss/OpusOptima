#include "FragmentCustody.hpp"
#include "IO/CalcifyAnimismusAdmission.hpp"
#include "RepoRoot.hpp"
#include "RowAssemblyInternal.hpp"
#include "SerialArmExecution.hpp"
#include <gtest/gtest.h>
#include <set>

namespace opusoptima::solvers { namespace {
TEST(WheelSecondSourceAssembly, TypedPortsFreshLineageAndSixReservations) {
    using IR = ExecutionIR;
    const char* previous = std::getenv("OPUSOPTIMA_FINITE_RETENTION");
    const std::string saved = previous ? previous : "";
    struct Restore {
        bool present;
        std::string value;
        ~Restore() {
            if (present)
                setenv("OPUSOPTIMA_FINITE_RETENTION", value.c_str(), 1);
            else
                unsetenv("OPUSOPTIMA_FINITE_RETENTION");
        }
    } restore{previous != nullptr, saved};
    setenv("OPUSOPTIMA_FINITE_RETENTION", "1", 1);
    const auto root = opusoptima::test::RepoRoot(__FILE__);
    for (const auto element : {core::AtomType::Air, core::AtomType::Earth, core::AtomType::Fire,
                               core::AtomType::Water}) {
        SCOPED_TRACE(static_cast<int>(element));
        auto actual = io::BinaryParser::ParsePuzzleFile(
            root / "asset/puzzle/edge-cases/row-transfer-wheel-second-source.puzzle");
        for (auto& atom : actual.outputs[0].atoms)
            if (atom.type == core::AtomType::Water) atom.type = element;
        const int expectedOrientation = element == core::AtomType::Air     ? 5
                                        : element == core::AtomType::Earth ? 2
                                        : element == core::AtomType::Fire  ? 3
                                                                           : 0;
        ASSERT_EQ(actual.permissionBits, (0xc1110dULL | (1ULL << 13) | (1ULL << 28)));
        ASSERT_EQ(actual.inputs.size(), 2u);
        ASSERT_EQ(actual.inputs[0].atoms[0].type, core::AtomType::Earth);
        auto proxy = actual;
        for (auto& atom : proxy.outputs[0].atoms)
            if (atom.type != core::AtomType::Quicksilver) atom.type = core::AtomType::Salt;
        for (bool swapped : {false, true}) {
            if (swapped) {
                std::swap(actual.inputs[0], actual.inputs[1]);
                std::swap(proxy.inputs[0], proxy.inputs[1]);
            }
            RowCompletionTransfer transfer;
            EXPECT_FALSE(transfer.secondSingletonSource);
            EXPECT_FALSE(transfer.wheelSecondSingletonSource);
            transfer.wheelSecondSingletonSource = true;
            transfer.secondSingletonSource = true;
            EXPECT_FALSE(transfer.coproductAssembly);
            EXPECT_FALSE(transfer.surplusRetention);
            transfer.coproductAssembly = transfer.surplusRetention = true;
            transfer.mode = io::VerificationMode::LegacyGate;
            bool called = false;
            transfer.nativeGate = [&](const auto&, auto) {
                called = true;
                return io::VerificationReport{}; // Mock rejection is not physical certification.
            };
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
            const auto plan = AssemblyPlanner::Plan(proxy, false, deadline);
            EXPECT_THROW(CompileRows(proxy, plan, nullptr, nullptr, nullptr, true, nullptr, false,
                                     6, nullptr, nullptr, 3, false, false, false, false, nullptr,
                                     nullptr, false, &actual, nullptr, deadline, {}, nullptr,
                                     &transfer),
                         IR::LayoutFailure);
            ASSERT_TRUE(called);
            int devices = 0, glyphs = 0;
            core::HexCoord wheelBase{};
            for (const auto& object : transfer.partialMachine.objects)
                if (object.typeName == "baron") {
                    ++devices;
                    wheelBase = object.position;
                    EXPECT_EQ(object.bodyData.at(1), expectedOrientation);
                    EXPECT_TRUE(object.instructions.empty());
                }
            for (const auto& object : transfer.partialMachine.objects)
                if (object.typeName == "glyph-duplication") {
                    ++glyphs;
                    EXPECT_EQ(object.position, core::Add(wheelBase, {0, 1}));
                    EXPECT_EQ(object.bodyData.at(1), 1);
                }
            EXPECT_EQ(devices, 1);
            EXPECT_EQ(glyphs, 1);
            for (const auto& event : transfer.events)
                if (event.kind == "WheelIdentity")
                    EXPECT_EQ(event.position, core::Add(wheelBase, {0, 2}));
            EXPECT_FALSE(transfer.published);
            ASSERT_EQ(transfer.boundaries.size(), 6u);
            int mercury = 0, wheel = 0;
            int acquire = 0, calcify = 0, animismus = 0, stores = 0, outputs = 0;
            std::set<IR::AtomId> retained;
            for (const auto& step : transfer.ledger.steps) {
                const auto& op = step.operation;
                if (op.kind == IR::Kind::Acquire) {
                    ++acquire;
                    ASSERT_LT(op.index, actual.inputs.size());
                    const auto type = actual.inputs[op.index].atoms[0].type;
                    EXPECT_EQ(step.effects.atoms.at(step.preconditions.nextId).type, type);
                    if (type == core::AtomType::Quicksilver)
                        ++mercury;
                    else
                        EXPECT_EQ(op.index, swapped ? 1u : 0u);
                }
                if (op.kind == IR::Kind::Calcify || op.kind == IR::Kind::Animismus) {
                    for (auto precursor : op.atoms)
                        EXPECT_FALSE(step.effects.atoms.contains(precursor));
                    const auto fresh = step.preconditions.nextId;
                    EXPECT_EQ(step.effects.atoms.at(fresh).type, op.kind == IR::Kind::Calcify
                                                                     ? core::AtomType::Salt
                                                                     : core::AtomType::Vitae);
                    if (op.kind == IR::Kind::Calcify)
                        ++calcify;
                    else {
                        ++animismus;
                        EXPECT_EQ(step.effects.atoms.at(fresh + 1).type, core::AtomType::Mors);
                    }
                }
                if (op.kind == IR::Kind::Duplicate) {
                    ++wheel;
                    ASSERT_TRUE(op.wheelCatalyst.has_value());
                    EXPECT_EQ(op.wheelCatalyst->element, element);
                    EXPECT_EQ(op.wheelCatalyst->orientation, expectedOrientation);
                    ASSERT_LT(op.wheelCatalyst->device, transfer.partialMachine.objects.size());
                    const auto& device =
                        transfer.partialMachine.objects.at(op.wheelCatalyst->device);
                    EXPECT_EQ(device.typeName, "baron");
                    EXPECT_EQ(device.bodyData.at(1), expectedOrientation);
                    EXPECT_TRUE(device.instructions.empty());
                    ASSERT_EQ(op.atoms.size(), 1u);
                    const auto old = op.atoms[0], fresh = step.preconditions.nextId;
                    EXPECT_EQ(step.preconditions.atoms.at(old).type, core::AtomType::Salt);
                    EXPECT_FALSE(step.effects.atoms.contains(old));
                    EXPECT_EQ(step.effects.atoms.at(fresh).type, element);
                    EXPECT_EQ(step.effects.atoms.at(fresh).position,
                              step.preconditions.atoms.at(old).position);
                    bool rebound = false;
                    for (const auto& boundary : transfer.reactions) {
                        if (!boundary.material.atoms.contains(fresh)) continue;
                        for (const auto& holder : boundary.holders) {
                            EXPECT_NE(holder.atom, old);
                            if (holder.atom == fresh) {
                                EXPECT_EQ(holder.tip, boundary.material.atoms.at(fresh).position);
                                rebound = true;
                            }
                        }
                        break;
                    }
                    EXPECT_TRUE(rebound);
                }
                if (op.kind == IR::Kind::Store) {
                    ++stores;
                    ASSERT_EQ(op.atoms.size(), 1u);
                    EXPECT_TRUE(retained.insert(op.atoms[0]).second);
                    EXPECT_EQ(step.effects.atoms.at(op.atoms[0]).type, core::AtomType::Vitae);
                }
                if (op.kind == IR::Kind::Output) {
                    ++outputs;
                    if (outputs == 6) {
                        EXPECT_EQ(stores, 6);
                        EXPECT_EQ(step.preconditions.atoms.size(), 11u);
                        EXPECT_EQ(step.preconditions.stored.size(), 6u);
                    }
                }
            }
            EXPECT_EQ(acquire, 36);
            EXPECT_EQ(mercury, 6);
            EXPECT_EQ(calcify, 30);
            EXPECT_EQ(animismus, 12);
            EXPECT_EQ(wheel, 6);
            EXPECT_EQ(stores, 6);
            EXPECT_EQ(outputs, 6);
            for (const auto& boundary : transfer.reactions) {
                FragmentCustody custody;
                for (const auto& holder : boundary.holders)
                    if (holder.atom) {
                        ASSERT_TRUE(boundary.material.atoms.contains(holder.atom));
                        custody.Grab(boundary.material, holder.object, holder.tip, holder.atom);
                    }
                EXPECT_NO_THROW(custody.Validate(boundary.material));
            }
            EXPECT_EQ(transfer.reactions.size(), 48u);
            const auto final = transfer.ledger.Replay(actual);
            EXPECT_EQ(final.atoms.size(), 6u);
            EXPECT_NO_THROW(RequireFinitePhysicalTerminal(final, transfer.retention,
                                                          transfer.retentionTargets));
            EXPECT_THROW(IR::RequireComplete(final, {6}), IR::Error);
        }
    }
}
TEST(WheelSecondSourceAssembly, RejectsWrongIndexTypePermissionsAndAuthority) {
    const auto root = opusoptima::test::RepoRoot(__FILE__);
    const auto fixture = io::BinaryParser::ParsePuzzleFile(
        root / "asset/puzzle/edge-cases/row-transfer-wheel-second-source.puzzle");
    for (int variant = 0; variant < 11; ++variant) {
        auto actual = fixture, proxy = fixture;
        for (auto& a : proxy.outputs[0].atoms)
            if (a.type != core::AtomType::Quicksilver) a.type = core::AtomType::Salt;
        RowCompletionTransfer transfer;
        transfer.wheelSecondSingletonSource = transfer.surplusRetention = true;
        transfer.secondSingletonSource = transfer.coproductAssembly = true;
        transfer.mode = io::VerificationMode::LegacyGate;
        bool called = false;
        transfer.nativeGate = [&](const auto&, auto) {
            called = true;
            return io::VerificationReport{};
        };
        if (variant == 0) std::swap(actual.inputs[0], actual.inputs[1]);
        if (variant == 1) proxy.inputs[1].atoms[0].type = core::AtomType::Salt;
        if (variant == 2) actual.permissionBits &= ~(1ULL << 12);
        if (variant == 3) proxy.permissionBits &= ~(1ULL << 16);
        if (variant == 4) transfer.mode = io::VerificationMode::Independent;
        if (variant == 5) transfer.secondSingletonSource = false;
        if (variant == 6) actual.permissionBits = proxy.permissionBits &= ~(1ULL << 13);
        if (variant == 7) actual.permissionBits = proxy.permissionBits &= ~(1ULL << 28);
        if (variant == 8) transfer.wheelSecondSingletonSource = false;
        if (variant == 9)
            for (auto& atom : actual.outputs[0].atoms)
                if (atom.type == core::AtomType::Water) atom.type = core::AtomType::Salt;
        if (variant == 10)
            for (auto& atom : actual.outputs[0].atoms)
                if (atom.type == core::AtomType::Mors) {
                    atom.type = core::AtomType::Air;
                    break;
                }
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        auto validProxy = fixture;
        for (auto& a : validProxy.outputs[0].atoms)
            if (a.type != core::AtomType::Quicksilver) a.type = core::AtomType::Salt;
        const auto plan = AssemblyPlanner::Plan(validProxy, false, deadline);
        EXPECT_THROW(CompileRows(proxy, plan, nullptr, nullptr, nullptr, true, nullptr, false, 6,
                                 nullptr, nullptr, 3, false, false, false, false, nullptr, nullptr,
                                 false, &actual, nullptr, deadline, {}, nullptr, &transfer),
                     std::invalid_argument);
        EXPECT_FALSE(called);
        EXPECT_TRUE(transfer.ledger.steps.empty());
    }
}
TEST(WheelSecondSourceAssembly, EligibilityPreservesFiniteNativeBounds) {
    const char* previous = std::getenv("OPUSOPTIMA_FINITE_RETENTION");
    struct Restore {
        bool present;
        std::string value;
        ~Restore() {
            if (present)
                setenv("OPUSOPTIMA_FINITE_RETENTION", value.c_str(), 1);
            else
                unsetenv("OPUSOPTIMA_FINITE_RETENTION");
        }
    } restore{previous != nullptr, previous ? previous : ""};
    const auto root = opusoptima::test::RepoRoot(__FILE__);
    const auto fixture = io::BinaryParser::ParsePuzzleFile(
        root / "asset/puzzle/edge-cases/row-transfer-wheel-second-source.puzzle");
    unsetenv("OPUSOPTIMA_FINITE_RETENTION");
    EXPECT_FALSE(SupportsFiniteWheelSecondSource(fixture));
    setenv("OPUSOPTIMA_FINITE_RETENTION", "0", 1);
    EXPECT_FALSE(SupportsFiniteWheelSecondSource(fixture));
    setenv("OPUSOPTIMA_FINITE_RETENTION", "1", 1);
    EXPECT_TRUE(SupportsFiniteWheelSecondSource(fixture));
    for (const auto element : {core::AtomType::Air, core::AtomType::Earth, core::AtomType::Fire,
                               core::AtomType::Water}) {
        auto puzzle = fixture;
        for (auto& atom : puzzle.outputs[0].atoms)
            if (atom.type == core::AtomType::Water) atom.type = element;
        EXPECT_TRUE(SupportsFiniteWheelSecondSource(puzzle));
        std::swap(puzzle.inputs[0], puzzle.inputs[1]);
        EXPECT_TRUE(SupportsFiniteWheelSecondSource(puzzle));
    }
    const auto required = AssemblyPlanner::RequiredPermissions | (1ULL << 2) | (1ULL << 3) |
                          (1ULL << 12) | (1ULL << 13) | (1ULL << 16) | (1ULL << 28);
    for (int bit = 0; bit < 64; ++bit) {
        if (!(required & (1ULL << bit))) continue;
        auto puzzle = fixture;
        puzzle.permissionBits &= ~(1ULL << bit);
        EXPECT_FALSE(SupportsFiniteWheelSecondSource(puzzle)) << bit;
    }
    for (int variant = 0; variant < 6; ++variant) {
        auto puzzle = fixture;
        if (variant == 0) puzzle.outputMultiplier = 2;
        if (variant == 1) puzzle.inputs[0].atoms.push_back(puzzle.inputs[0].atoms[0]);
        if (variant == 2) puzzle.inputs[1].atoms[0].type = core::AtomType::Salt;
        for (auto& atom : puzzle.outputs[0].atoms) {
            if (variant == 3 && atom.type == core::AtomType::Vitae) atom.type = core::AtomType::Air;
            if (variant == 4 && atom.type == core::AtomType::Quicksilver)
                atom.type = core::AtomType::Mors;
            if (variant == 5 && atom.type == core::AtomType::Vitae)
                atom.type = core::AtomType::Mors;
        }
        EXPECT_FALSE(SupportsFiniteWheelSecondSource(puzzle)) << variant;
    }
    EXPECT_FALSE(io::CalcifyAnimismusModeAllowed("finite-wheel-second-source",
                                                 io::VerificationMode::Independent));
    EXPECT_TRUE(io::CalcifyAnimismusModeAllowed("finite-wheel-second-source",
                                                io::VerificationMode::LegacyGate));
}
}} // namespace opusoptima::solvers
