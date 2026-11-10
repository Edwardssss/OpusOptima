#include "RowAssemblyInternal.hpp"
#include "FragmentCustody.hpp"
#include "SerialArmExecution.hpp"
#include <gtest/gtest.h>
#include <set>
#include <cstdlib>
#include <optional>
#include "RepoRoot.hpp"

namespace opusoptima::solvers { namespace {
TEST(CardinalSurplusValidation, MockOnlyFreshLineageCustodyAndSixPhysicalReservations) {
    using IR = ExecutionIR;
    struct RetentionOptIn {
        std::optional<std::string> prior;
        RetentionOptIn() {
            if (const auto* value = std::getenv("OPUSOPTIMA_FINITE_RETENTION")) prior = value;
            setenv("OPUSOPTIMA_FINITE_RETENTION", "1", 1);
        }
        ~RetentionOptIn() {
            if (prior) setenv("OPUSOPTIMA_FINITE_RETENTION", prior->c_str(), 1);
            else unsetenv("OPUSOPTIMA_FINITE_RETENTION");
        }
    } retentionOptIn;
    const auto root = opusoptima::test::RepoRoot(__FILE__);
    const auto actual = io::BinaryParser::ParsePuzzleFile(root / "asset/puzzle/edge-cases/row-transfer-cardinal-surplus.puzzle");
    ASSERT_EQ(actual.permissionBits, 0xc1110dULL);
    ASSERT_EQ(actual.inputs.size(), 1u);
    ASSERT_EQ(actual.inputs[0].atoms[0].type, core::AtomType::Earth);
    auto proxy = actual;
    for (auto& atom : proxy.outputs[0].atoms) atom.type = core::AtomType::Salt;
    RowCompletionTransfer transfer;
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
        nullptr, nullptr, 3, false, false, false, false, nullptr, nullptr, false,
        &actual, nullptr, deadline, {}, nullptr, &transfer), IR::LayoutFailure);
    ASSERT_TRUE(called);
    EXPECT_FALSE(transfer.published);
    ASSERT_EQ(transfer.boundaries.size(), 6u);
    int acquire = 0, calcify = 0, animismus = 0, stores = 0, outputs = 0;
    std::set<IR::AtomId> retained;
    for (const auto& step : transfer.ledger.steps) {
        const auto& op = step.operation;
        if (op.kind == IR::Kind::Acquire) {
            ++acquire;
            EXPECT_EQ(op.index, 0u);
            EXPECT_EQ(step.effects.atoms.at(step.preconditions.nextId).type, core::AtomType::Earth);
        }
        if (op.kind == IR::Kind::Calcify || op.kind == IR::Kind::Animismus) {
            for (auto precursor : op.atoms) EXPECT_FALSE(step.effects.atoms.contains(precursor));
            const auto fresh = step.preconditions.nextId;
            EXPECT_EQ(step.effects.atoms.at(fresh).type,
                op.kind == IR::Kind::Calcify ? core::AtomType::Salt : core::AtomType::Vitae);
            if (op.kind == IR::Kind::Calcify) ++calcify;
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
                EXPECT_EQ(step.preconditions.atoms.size(), 9u);
                EXPECT_EQ(step.preconditions.stored.size(), 6u);
            }
        }
    }
    EXPECT_EQ(acquire, 24); EXPECT_EQ(calcify, 24); EXPECT_EQ(animismus, 12);
    EXPECT_EQ(stores, 6); EXPECT_EQ(outputs, 6);
    for (const auto& boundary : transfer.reactions) {
        FragmentCustody custody;
        for (const auto& holder : boundary.holders) if (holder.atom) {
            ASSERT_TRUE(boundary.material.atoms.contains(holder.atom));
            custody.Grab(boundary.material, holder.object, holder.tip, holder.atom);
        }
        EXPECT_NO_THROW(custody.Validate(boundary.material));
    }
    EXPECT_EQ(transfer.reactions.size(), 36u);
    const auto final = transfer.ledger.Replay(actual);
    EXPECT_EQ(final.atoms.size(), 6u);
    EXPECT_NO_THROW(RequireFinitePhysicalTerminal(final, transfer.retention, transfer.retentionTargets));
    EXPECT_THROW(IR::RequireComplete(final, {6}), IR::Error);
}
} }
