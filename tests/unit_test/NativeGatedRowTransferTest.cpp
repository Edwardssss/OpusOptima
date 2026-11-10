#include "FragmentCustody.hpp"
#include "IO/BinaryWriter.hpp"
#include "RepoRoot.hpp"
#include "RowAssemblyInternal.hpp"
#include <gtest/gtest.h>
#include <set>
#include <stdexcept>

namespace opusoptima::solvers { namespace {
using IR = ExecutionIR;
const auto root = opusoptima::test::RepoRoot(__FILE__);
io::PuzzleData TransferPuzzle() {
    return io::BinaryParser::ParsePuzzleFile(
        root / "asset/puzzle/edge-cases/row-transfer-singleton-star.puzzle");
}
io::SolutionData TransferCompile(const io::PuzzleData& p, RowCompletionTransfer* transfer) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    const auto plan = AssemblyPlanner::Plan(p, false, deadline);
    return CompileRows(p, plan, nullptr, nullptr, nullptr, true, nullptr, false, 6, nullptr,
                       nullptr, 3, false, false, false, false, nullptr, nullptr, false, nullptr,
                       nullptr, deadline, {}, nullptr, transfer);
}
io::SolutionData CoproductCompile(const io::PuzzleData& actual, RowCompletionTransfer* transfer) {
    auto proxy = actual;
    for (auto& atom : proxy.outputs[0].atoms)
        atom.type = core::AtomType::Salt;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    const auto plan = AssemblyPlanner::Plan(proxy, false, deadline);
    return CompileRows(proxy, plan, nullptr, nullptr, nullptr, true, nullptr, false, 6, nullptr,
                       nullptr, 3, false, false, false, false, nullptr, nullptr, false, &actual,
                       nullptr, deadline, {}, nullptr, transfer);
}
TEST(ReceiverRail, AbsoluteCrossingEndpointsAndSignedRelativeOffsets) {
    io::SolutionData machine;
    io::SolutionObject track;
    track.typeName = "track";
    track.position = {-7, -4};
    track.bodyData = {1, 0, 0, 0, 3, 2, -1, 2, 0, 2, 1, 0};
    machine.objects.push_back(track);
    EXPECT_FALSE(ReceiverRailDisjoint(machine, {-9, -4}, 8));  // interior crossing
    EXPECT_FALSE(ReceiverRailDisjoint(machine, {-9, -5}, 4));  // both endpoints
    EXPECT_FALSE(ReceiverRailDisjoint(machine, {-1, -3}, -8)); // negative direction
    EXPECT_TRUE(ReceiverRailDisjoint(machine, {-9, -6}, 8));
    EXPECT_EQ(ReceiverRailBody(2), (std::vector<int>{1, 0, 0, 0, 3, 0, 0, 1, 0, 2, 0, 0}));
    EXPECT_EQ(ReceiverRailBody(-2), (std::vector<int>{1, 0, 0, 0, 3, -2, 0, -1, 0, 0, 0, 0}));
    EXPECT_EQ(ReceiverRailBody(2, {0, 1}), (std::vector<int>{1, 0, 0, 0, 3, 0, 0, 0, 1, 0, 2, 0}));
    EXPECT_FALSE(ReceiverRailDisjoint(machine, {-5, -6}, 4, {0, 1}));
    EXPECT_TRUE(ReceiverRailDisjoint(machine, {-4, -6}, 4, {0, 1}));
    // Plus increases offset; a negative move starts at the last cell and uses minus.
    machine.objects[0].position = {-5, -4};
    machine.objects[0].bodyData = ReceiverRailBody(-2);
    EXPECT_FALSE(ReceiverRailDisjoint(machine, {-8, -4}, 2));
    EXPECT_TRUE(ReceiverRailDisjoint(machine, {-8, -3}, 2));
}
TEST(CoproductAssemblyTransfer, LogicOnlyBothFreshProductsAndAllCustodyReachRejectingGate) {
    const auto p = io::BinaryParser::ParsePuzzleFile(
        root / "asset/puzzle/edge-cases/row-transfer-coproduct-balanced.puzzle");
    RowCompletionTransfer transfer;
    EXPECT_FALSE(transfer.coproductAssembly);
    transfer.coproductAssembly = true;
    transfer.mode = io::VerificationMode::LegacyGate;
    bool called = false;
    transfer.nativeGate = [&](const auto&, auto) {
        called = true;
        EXPECT_EQ(transfer.reactionInputs.size(), 6u);
        return io::VerificationReport{};
    };
    EXPECT_THROW(CoproductCompile(p, &transfer), IR::LayoutFailure);
    EXPECT_TRUE(called);
    EXPECT_FALSE(transfer.published);
    ASSERT_EQ(transfer.reactions.size(), 6u);
    ASSERT_EQ(transfer.boundaries.size(), 6u);
    EXPECT_NO_THROW(IR::RequireComplete(transfer.ledger.Replay(p), {6}));
    std::set<IR::AtomId> products;
    for (std::size_t i = 0; i < transfer.reactions.size(); ++i) {
        const auto& before = transfer.reactionInputs[i];
        const auto& after = transfer.reactions[i];
        EXPECT_EQ(before.material.atoms.size(), 2u);
        EXPECT_EQ(after.material.atoms.size(), 2u);
        std::set<core::AtomType> types;
        for (const auto& [id, atom] : before.material.atoms) {
            EXPECT_EQ(atom.type, core::AtomType::Salt);
            EXPECT_FALSE(after.material.atoms.contains(id));
            for (const auto& holder : before.holders)
                EXPECT_NE(holder.atom, id);
        }
        for (const auto& [id, atom] : after.material.atoms) {
            EXPECT_TRUE(products.insert(id).second);
            types.insert(atom.type);
        }
        EXPECT_EQ(types, (std::set<core::AtomType>{core::AtomType::Vitae, core::AtomType::Mors}));
        const auto& assembled = transfer.boundaries[i];
        for (const auto* boundary : {&before, &after, &assembled}) {
            FragmentCustody custody;
            for (const auto& holder : boundary->holders)
                if (holder.atom) {
                    EXPECT_EQ(boundary->material.atoms.at(holder.atom).position, holder.tip);
                    custody.Grab(boundary->material, holder.object, holder.tip, holder.atom);
                }
            EXPECT_NO_THROW(custody.Validate(boundary->material));
        }
    }
    EXPECT_EQ(products.size(), 12u);
    transfer.mode = io::VerificationMode::Independent;
    EXPECT_THROW(CoproductCompile(p, &transfer), std::invalid_argument);
}
TEST(CoproductAssemblyTransfer, DefaultOffDeterministicAndExplicitOffGateRejects) {
    const auto p = io::BinaryParser::ParsePuzzleFile(
        root / "asset/puzzle/edge-cases/row-transfer-coproduct-balanced.puzzle");
    const auto baseline = TransferPuzzle();
    EXPECT_EQ(io::BinaryWriter::WriteSolution(TransferCompile(baseline, nullptr)),
              io::BinaryWriter::WriteSolution(TransferCompile(baseline, nullptr)));
    RowCompletionTransfer transfer;
    transfer.mode = io::VerificationMode::LegacyGate;
    transfer.nativeGate = [](const auto&, auto) { return io::VerificationReport{}; };
    EXPECT_THROW(CoproductCompile(p, &transfer), std::invalid_argument);
    EXPECT_TRUE(transfer.reactions.empty());
}
TEST(InventoryAssemblyTransfer, ChemicalLineageAndAllHoldersReachGate) {
    const auto p = io::BinaryParser::ParsePuzzleFile(
        root / "asset/puzzle/edge-cases/row-transfer-calcify-star.puzzle");
    RowCompletionTransfer transfer;
    transfer.mode = io::VerificationMode::LegacyGate;
    bool called = false;
    transfer.nativeGate = [&](const auto&, auto) {
        called = true;
        EXPECT_EQ(transfer.reactions.size(), 24u);
        EXPECT_EQ(transfer.boundaries.size(), 6u);
        IR::RequireComplete(transfer.ledger.Replay(p), {6});
        for (const auto& reaction : transfer.reactions) {
            FragmentCustody custody;
            for (const auto& holder : reaction.holders) {
                if (holder.atom) {
                    EXPECT_EQ(reaction.material.atoms.at(holder.atom).position, holder.tip);
                    custody.Grab(reaction.material, holder.object, holder.tip, holder.atom);
                }
            }
            EXPECT_NO_THROW(custody.Validate(reaction.material));
        }
        std::size_t identities = 0;
        for (const auto& event : transfer.events) {
            if (event.kind != "ReactionIdentity") continue;
            ++identities;
            EXPECT_NE(event.atom, event.other);
            const auto reaction = std::find_if(
                transfer.reactions.begin(), transfer.reactions.end(), [&](const auto& state) {
                    return state.tapeBoundary == event.tapeBoundary &&
                           state.material.atoms.contains(event.other);
                });
            EXPECT_NE(reaction, transfer.reactions.end());
            if (reaction == transfer.reactions.end()) continue;
            EXPECT_FALSE(reaction->material.atoms.contains(event.atom));
            EXPECT_EQ(reaction->material.atoms.at(event.other).type, core::AtomType::Salt);
            EXPECT_EQ(reaction->material.atoms.at(event.other).position, event.position);
        }
        EXPECT_EQ(identities, 24u);
        return io::VerificationReport{};
    };
    EXPECT_THROW(TransferCompile(p, &transfer), IR::LayoutFailure);
    EXPECT_TRUE(called);
    EXPECT_FALSE(transfer.published);
    transfer.mode = io::VerificationMode::Independent;
    EXPECT_THROW(TransferCompile(p, &transfer), std::invalid_argument);
}
TEST(InventoryAssemblyTransfer, ChemistryRequiresPermission) {
    auto p = io::BinaryParser::ParsePuzzleFile(
        root / "asset/puzzle/edge-cases/row-transfer-calcify-star.puzzle");
    p.permissionBits &= ~(1ULL << 12);
    RowCompletionTransfer transfer;
    transfer.mode = io::VerificationMode::LegacyGate;
    transfer.nativeGate = [](const auto&, auto) { return io::VerificationReport{}; };
    EXPECT_THROW(TransferCompile(p, &transfer), std::invalid_argument);
    EXPECT_TRUE(transfer.reactions.empty());
}
TEST(NativeGatedRowTransfer, RequiresExplicitLegacyGate) {
    RowCompletionTransfer transfer;
    bool called = false;
    transfer.nativeGate = [&](const auto&, auto) {
        called = true;
        return io::VerificationReport{};
    };
    EXPECT_THROW(TransferCompile(TransferPuzzle(), &transfer), std::invalid_argument);
    EXPECT_FALSE(called);
    EXPECT_FALSE(transfer.published);
}
TEST(NativeGatedRowTransfer, RejectionPreservesFullMachineAndNeverAccepts) {
    const auto p = TransferPuzzle();
    RowCompletionTransfer transfer;
    transfer.mode = io::VerificationMode::LegacyGate;
    bool called = false, accepted = false;
    transfer.accept = [&](const auto&) {
        accepted = true;
        return true;
    };
    transfer.nativeGate = [&](const io::SolutionData& machine, auto deadline) {
        called = true;
        EXPECT_GT(deadline, std::chrono::steady_clock::now());
        EXPECT_EQ(transfer.boundaries.size(), 6u);
        IR::RequireComplete(transfer.ledger.Replay(p), {6});
        const auto& candidate = transfer.candidates.back();
        auto receiver =
            std::find_if(machine.objects.begin(), machine.objects.end(), [&](const auto& o) {
                return o.typeName == "arm1" && o.position == candidate.base;
            });
        EXPECT_NE(receiver, machine.objects.end());
        if (receiver != machine.objects.end()) {
            EXPECT_FALSE(receiver->instructions.empty());
            if (!receiver->instructions.empty())
                EXPECT_GT(receiver->instructions.front().parameter, 0);
        }
        // The receiver is an ordinary serialized object, not a mid-tape creation operation.
        const auto serialized = io::BinaryWriter::WriteSolution(machine);
        EXPECT_FALSE(serialized.empty());
        auto rail =
            std::find_if(machine.objects.begin(), machine.objects.end(), [&](const auto& o) {
                return o.typeName == "track" && o.position == candidate.base;
            });
        EXPECT_NE(rail, machine.objects.end());
        if (rail != machine.objects.end()) {
            EXPECT_EQ(rail->bodyData.at(5), 0);
            EXPECT_EQ(rail->bodyData.at(6), 0);
        }
        return io::VerificationReport{};
    };
    EXPECT_THROW(TransferCompile(p, &transfer), IR::LayoutFailure);
    EXPECT_TRUE(called);
    EXPECT_FALSE(accepted);
    EXPECT_FALSE(transfer.published);
    EXPECT_FALSE(transfer.partialMachine.objects.empty());
    auto corrupt = transfer.ledger;
    auto output = std::find_if(corrupt.steps.begin(), corrupt.steps.end(),
                               [](const auto& s) { return s.operation.kind == IR::Kind::Output; });
    ASSERT_NE(output, corrupt.steps.end());
    output->operation.atoms = {999999};
    EXPECT_THROW(corrupt.Replay(p), IR::Error);
    corrupt = transfer.ledger;
    auto bond = std::find_if(corrupt.steps.begin(), corrupt.steps.end(),
                             [](const auto& s) { return s.operation.kind == IR::Kind::Bond; });
    ASSERT_NE(bond, corrupt.steps.end());
    bond->operation.bits = 14;
    EXPECT_THROW(corrupt.Replay(p), IR::Error);
    FragmentCustody custody;
    const auto& boundary = transfer.boundaries.front();
    const auto& holder = *std::find_if(boundary.holders.begin(), boundary.holders.end(),
                                       [](const auto& h) { return h.grabbing; });
    EXPECT_THROW(custody.Grab(boundary.material, holder.object, {999, 999}, holder.atom),
                 IR::Error);
    custody.Grab(boundary.material, holder.object, holder.tip, holder.atom);
    auto changed = boundary.material;
    changed.atoms.erase(holder.atom);
    EXPECT_THROW(custody.Validate(changed), IR::Error);
}
TEST(NativeGatedRowTransfer, WrongAuthorityCannotPublish) {
    RowCompletionTransfer transfer;
    transfer.mode = io::VerificationMode::LegacyGate;
    bool accepted = false;
    transfer.accept = [&](const auto&) {
        accepted = true;
        return true;
    };
    transfer.nativeGate = [](const auto&, auto) {
        io::VerificationReport report;
        report.status = io::VerificationStatus::Passed;
        report.authority = io::VerificationAuthority::Independent;
        return report;
    };
    EXPECT_THROW(TransferCompile(TransferPuzzle(), &transfer), IR::LayoutFailure);
    EXPECT_FALSE(accepted);
    EXPECT_FALSE(transfer.published);
}
TEST(NativeGatedRowTransfer, TimeoutCannotReturnAcceptOrPublish) {
    RowCompletionTransfer transfer;
    transfer.mode = io::VerificationMode::LegacyGate;
    bool called = false, accepted = false, returned = false;
    transfer.accept = [&](const auto&) {
        accepted = true;
        return true;
    };
    transfer.nativeGate = [&](const auto&, auto) {
        called = true;
        EXPECT_FALSE(transfer.published);
        io::VerificationReport report;
        report.status = io::VerificationStatus::Timeout;
        report.authority = io::VerificationAuthority::LegacyGate;
        return report;
    };
    EXPECT_THROW(
        {
            (void)TransferCompile(TransferPuzzle(), &transfer);
            returned = true;
        },
        IR::LayoutFailure);
    EXPECT_TRUE(called);
    EXPECT_EQ(transfer.verification.status, io::VerificationStatus::Timeout);
    EXPECT_FALSE(returned);
    EXPECT_FALSE(accepted);
    EXPECT_FALSE(transfer.published);
    EXPECT_FALSE(transfer.partialMachine.objects.empty());
}
TEST(NativeGatedRowTransfer, ThrowingGateCannotReturnAcceptOrPublish) {
    RowCompletionTransfer transfer;
    transfer.mode = io::VerificationMode::LegacyGate;
    bool called = false, accepted = false, returned = false;
    transfer.accept = [&](const auto&) {
        accepted = true;
        return true;
    };
    transfer.nativeGate = [&](const auto&, auto) -> io::VerificationReport {
        called = true;
        EXPECT_FALSE(transfer.published);
        throw std::runtime_error("mock gate failure");
    };
    EXPECT_THROW(
        {
            (void)TransferCompile(TransferPuzzle(), &transfer);
            returned = true;
        },
        std::runtime_error);
    EXPECT_TRUE(called);
    EXPECT_FALSE(returned);
    EXPECT_FALSE(accepted);
    EXPECT_FALSE(transfer.published);
    EXPECT_FALSE(transfer.partialMachine.objects.empty());
}
TEST(NativeGatedRowTransfer, MockPassedLegacyGateAcceptsThenPublishes) {
    RowCompletionTransfer transfer;
    transfer.mode = io::VerificationMode::LegacyGate;
    int gateCalls = 0, acceptCalls = 0;
    std::vector<std::byte> gatedBytes, acceptedBytes;
    transfer.nativeGate = [&](const auto& machine, auto) {
        ++gateCalls;
        EXPECT_EQ(acceptCalls, 0);
        EXPECT_FALSE(transfer.published);
        gatedBytes = io::BinaryWriter::WriteSolution(machine);
        io::VerificationReport mockPassed;
        mockPassed.status = io::VerificationStatus::Passed;
        mockPassed.authority = io::VerificationAuthority::LegacyGate;
        return mockPassed;
    };
    transfer.accept = [&](const auto& machine) {
        ++acceptCalls;
        EXPECT_EQ(gateCalls, 1);
        EXPECT_TRUE(transfer.verification.Passed());
        EXPECT_FALSE(transfer.published);
        acceptedBytes = io::BinaryWriter::WriteSolution(machine);
        return true;
    };
    // This callback seam exposes a flag and returned machine, not a file publisher.
    const auto result = TransferCompile(TransferPuzzle(), &transfer);
    EXPECT_EQ(gateCalls, 1);
    EXPECT_EQ(acceptCalls, 1);
    EXPECT_TRUE(transfer.published);
    EXPECT_FALSE(gatedBytes.empty());
    EXPECT_EQ(gatedBytes, acceptedBytes);
    EXPECT_EQ(gatedBytes, io::BinaryWriter::WriteSolution(result));
    EXPECT_EQ(gatedBytes, io::BinaryWriter::WriteSolution(transfer.partialMachine));
}
TEST(NativeGatedRowTransfer, MissingLegacyReactionOriginsRejectedBeforeEmission) {
    const auto actual = io::BinaryParser::ParsePuzzleFile(
        root / "asset/puzzle/edge-cases/row-transfer-coproduct-balanced.puzzle");
    std::vector<FragmentCustodyEvent> custody(1);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    auto proxy = actual;
    for (auto& atom : proxy.outputs[0].atoms)
        atom.type = core::AtomType::Salt;
    const auto plan = AssemblyPlanner::Plan(proxy, false, deadline);
    try {
        (void)CompileRows(proxy, plan, nullptr, nullptr, nullptr, true, nullptr, false, 6, nullptr,
                          nullptr, 3, false, false, false, false, nullptr, nullptr, false, &actual,
                          nullptr, deadline, {}, &custody, nullptr);
        FAIL() << "missing legacy output origins accepted";
    } catch (const std::invalid_argument& error) {
        EXPECT_STREQ(error.what(), "legacy reaction assembly requires product origins");
    }
    EXPECT_EQ(custody.size(), 1u);
}
TEST(NativeGatedRowTransfer, PublicFiniteDefaultMatchesHistoricalInputAndBytes) {
    const auto p = TransferPuzzle();
    const auto expected = io::BinaryParser::ReadAllBytes(
        root / "asset/solution/edge-cases/row-transfer-default-v1.solution");
    const auto baseline = io::BinaryParser::ParseSolution(expected);
    EXPECT_EQ(baseline.puzzleName, p.name);
    const auto plan = AssemblyPlanner::Plan(p);
    std::vector<int> ends;
    EXPECT_EQ(io::BinaryWriter::WriteSolution(CompileRowAssemblyFiniteBatch(p, plan, ends, 6)),
              expected);
    EXPECT_EQ(ends.size(), 6u);
    EXPECT_EQ(io::BinaryWriter::WriteSolution(CompileRowAssembly(p, plan)),
              io::BinaryWriter::WriteSolution(CompileRows(p, plan)));
}
TEST(NativeGatedRowTransfer, ValidLegacyReactionWrapperNeedsNoTransfer) {
    using T = core::AtomType;
    auto puzzle = TransferPuzzle();
    puzzle.inputs[0].atoms = {{T::Air, {0, 0}}, {T::Water, {1, 0}}};
    puzzle.inputs[0].bonds = {{1, {0, 0}, {1, 0}}};
    puzzle.outputs.resize(2);
    for (auto& output : puzzle.outputs) {
        output.atoms = {{T::Vitae, {0, 0}}, {T::Mors, {1, 0}}};
        output.bonds = {{1, {0, 0}, {1, 0}}};
    }
    puzzle.permissionBits |= (1ULL << 9) | (1ULL << 12) | (1ULL << 16);
    const auto batch = CompileSharedAnimismusAssembly(puzzle);
    EXPECT_FALSE(batch.solution.objects.empty());
    EXPECT_NO_THROW(IR::RequireComplete(batch.ledger.Replay(puzzle), {6, 6}));
}
TEST(NativeGatedRowTransfer, ConflictingLegacyTransferDoesNotResetState) {
    const auto p = TransferPuzzle();
    const auto plan = AssemblyPlanner::Plan(p);
    RowCompletionTransfer transfer;
    transfer.mode = io::VerificationMode::LegacyGate;
    transfer.failure = "sentinel";
    transfer.published = true;
    bool called = false;
    transfer.nativeGate = [&](const auto&, auto) {
        called = true;
        return io::VerificationReport{};
    };
    std::vector<core::HexCoord> origins{{0, 0}};
    EXPECT_THROW(CompileRows(p, plan, nullptr, nullptr, &origins, true, nullptr, false, 6, nullptr,
                             nullptr, 3, false, false, false, false, nullptr, nullptr, false,
                             nullptr, nullptr, std::chrono::steady_clock::time_point::max(), {},
                             nullptr, &transfer),
                 std::invalid_argument);
    EXPECT_EQ(transfer.failure, "sentinel");
    EXPECT_TRUE(transfer.published);
    EXPECT_FALSE(called);
}
TEST(NativeGatedRowTransfer, DefaultFinishBytesUnchanged) {
    const auto actual = io::BinaryWriter::WriteSolution(TransferCompile(TransferPuzzle(), nullptr));
    const auto expected = io::BinaryParser::ReadAllBytes(
        root / "asset/solution/edge-cases/row-transfer-default-v1.solution");
    EXPECT_EQ(actual, expected);
}
TEST(SurplusAssemblyTransfer, JointReceiverRejectingGatePreservesSixRealStores) {
    io::PuzzleData p;
    p.version = 3;
    p.name = "surplus-v1m2-path";
    p.permissionBits = ~std::uint64_t{0};
    p.outputMultiplier = 1;
    p.inputs = {{{{core::AtomType::Salt, {0, 0}}}, {}}};
    p.outputs.resize(1);
    p.outputs[0].atoms = {{core::AtomType::Mors, {0, 0}},
                          {core::AtomType::Mors, {1, 0}},
                          {core::AtomType::Vitae, {2, 0}}};
    p.outputs[0].bonds = {{1, {0, 0}, {1, 0}}, {1, {1, 0}, {2, 0}}};
    RowCompletionTransfer transfer;
    transfer.coproductAssembly = transfer.surplusRetention = true;
    transfer.mode = io::VerificationMode::LegacyGate;
    bool called = false;
    transfer.nativeGate = [&](const auto&, auto) {
        called = true;
        return io::VerificationReport{};
    };
    setenv("OPUSOPTIMA_FINITE_RETENTION", "1", 1);
    EXPECT_THROW(CoproductCompile(p, &transfer), IR::LayoutFailure);
    EXPECT_TRUE(called) << transfer.failure;
    EXPECT_FALSE(transfer.published);
    ASSERT_EQ(transfer.retention.slots.size(), 6u) << transfer.failure;
    ASSERT_EQ(transfer.boundaries.size(), 6u);
    std::set<IR::AtomId> retainedIds;
    for (const auto& retained : transfer.retention.slots) {
        EXPECT_TRUE(retainedIds.insert(retained.atom).second);
        EXPECT_EQ(transfer.boundaries.back().material.stored.at(retained.slot),
                  (std::vector<IR::AtomId>{retained.atom}));
        EXPECT_EQ(transfer.boundaries.back().material.atoms.at(retained.atom).type,
                  core::AtomType::Vitae);
    }
    const auto& slot = transfer.retention.slots.front();
    EXPECT_EQ(transfer.boundaries.front().material.stored.at(slot.slot),
              (std::vector<IR::AtomId>{slot.atom}));
    EXPECT_EQ(transfer.boundaries.front().material.atoms.at(slot.atom).type, core::AtomType::Vitae);
    transfer = {};
    transfer.coproductAssembly = transfer.surplusRetention = true;
    transfer.mode = io::VerificationMode::LegacyGate;
    transfer.nativeGate = [](const auto&, auto) {
        io::VerificationReport report;
        report.status = io::VerificationStatus::Passed;
        report.authority = io::VerificationAuthority::None;
        return report;
    };
    EXPECT_THROW(CoproductCompile(p, &transfer), IR::LayoutFailure);
    EXPECT_FALSE(transfer.published);
    unsetenv("OPUSOPTIMA_FINITE_RETENTION");
}
}} // namespace opusoptima::solvers
