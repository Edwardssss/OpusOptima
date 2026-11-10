#include "FragmentCustody.hpp"
#include "RepoRoot.hpp"
#include "RowAssemblyInternal.hpp"
#include "SerialArmExecution.hpp"
#include <gtest/gtest.h>
#include <set>

namespace opusoptima::solvers { namespace {
TEST(SecondSourceAssembly, TypedPortsFreshLineageAndSixReservations) {
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
    auto actual = io::BinaryParser::ParsePuzzleFile(
        root / "asset/puzzle/edge-cases/row-transfer-second-source.puzzle");
    ASSERT_EQ(actual.permissionBits, 0xc1110dULL);
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
        EXPECT_THROW(CompileRows(proxy, plan, nullptr, nullptr, nullptr, true, nullptr, false, 6,
                                 nullptr, nullptr, 3, false, false, false, false, nullptr, nullptr,
                                 false, &actual, nullptr, deadline, {}, nullptr, &transfer),
                     IR::LayoutFailure);
        ASSERT_TRUE(called);
        EXPECT_FALSE(transfer.published);
        ASSERT_EQ(transfer.boundaries.size(), 6u);
        int mercury = 0;
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
                    EXPECT_EQ(step.preconditions.atoms.size(), 10u);
                    EXPECT_EQ(step.preconditions.stored.size(), 6u);
                }
            }
        }
        EXPECT_EQ(acquire, 30);
        EXPECT_EQ(mercury, 6);
        EXPECT_EQ(calcify, 24);
        EXPECT_EQ(animismus, 12);
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
        EXPECT_EQ(transfer.reactions.size(), 36u);
        const auto final = transfer.ledger.Replay(actual);
        EXPECT_EQ(final.atoms.size(), 6u);
        EXPECT_NO_THROW(
            RequireFinitePhysicalTerminal(final, transfer.retention, transfer.retentionTargets));
        EXPECT_THROW(IR::RequireComplete(final, {6}), IR::Error);
    }
}
TEST(SecondSourceAssembly, RejectsWrongIndexTypePermissionsAndAuthority) {
    const auto root = opusoptima::test::RepoRoot(__FILE__);
    const auto fixture = io::BinaryParser::ParsePuzzleFile(
        root / "asset/puzzle/edge-cases/row-transfer-second-source.puzzle");
    for (int variant = 0; variant < 6; ++variant) {
        auto actual = fixture, proxy = fixture;
        for (auto& a : proxy.outputs[0].atoms)
            if (a.type != core::AtomType::Quicksilver) a.type = core::AtomType::Salt;
        RowCompletionTransfer transfer;
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
}} // namespace opusoptima::solvers
