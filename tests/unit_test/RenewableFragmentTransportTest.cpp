#include "RowAssemblyInternal.hpp"
#include <gtest/gtest.h>
#include <tuple>

namespace opusoptima::solvers { namespace {
io::PuzzleData RenewableTree() {
    io::PuzzleData p;
    p.version = 3;
    p.name = "renewable-tree";
    p.permissionBits = ~0ULL;
    p.outputMultiplier = 1;
    io::Molecule source;
    for (int q = 0; q < 4; ++q) {
        source.atoms.push_back({core::AtomType::Salt, {q, 0}});
        if (q) source.bonds.push_back({1, {q - 1, 0}, {q, 0}});
    }
    p.inputs = {source};
    p.outputs = {source};
    return p;
}
TEST(RenewableFragmentTransportTest, ClosedSixProducts) {
    const auto p = RenewableTree();
    const auto batch = CompileRenewableFragmentTransport(p);
    const auto state = batch.ledger.Replay(p);
    ExecutionIR::RequireComplete(state, {6});
    EXPECT_EQ(state.batches.at(0), 6);
    EXPECT_EQ(batch.emissions.size(), 24);
}
TEST(RenewableFragmentTransportTest, DeadlineInterruptsTapeConstruction) {
    const auto p = RenewableTree();
    const auto start = std::chrono::steady_clock::now();
    const auto deadline = start + std::chrono::hours(1);
    std::size_t completeChecks = 0;
    const auto baseline = CompileRenewableFragmentTransport(p, deadline, [&] {
        ++completeChecks;
        return start;
    });
    ASSERT_GT(completeChecks, 100U);
    std::size_t checks = 0;
    auto preserved = baseline.solution;
    EXPECT_THROW(preserved = CompileRenewableFragmentTransport(
                                 p, deadline,
                                 [&] { return ++checks < completeChecks / 2 ? start : deadline; })
                                 .solution,
                 std::invalid_argument);
    EXPECT_EQ(checks, completeChecks / 2);
    EXPECT_EQ(preserved.objects.size(), baseline.solution.objects.size());
    EXPECT_EQ(preserved.metrics.cycles, baseline.solution.metrics.cycles);
}
TEST(RenewableFragmentTransportTest, LegacyOrdinaryWrapperRemainsUnlimited) {
    const auto p = RenewableTree();
    const auto plan = AssemblyPlanner::Plan(p, true);
    const auto solution = CompileRowAssembly(p, plan);
    EXPECT_FALSE(solution.objects.empty());
    EXPECT_GT(solution.metrics.cycles, 0);
}
TEST(RenewableFragmentTransportTest, DeadlinePreservesPopulatedExternalContainers) {
    const auto p = RenewableTree();
    const auto plan = AssemblyPlanner::Plan(p, true);
    const auto supply = AssemblyPlanner::PlanSupply(p, true);
    std::vector<ProductAtomRef> owners;
    for (std::size_t i = 0; i < p.outputs[0].atoms.size(); ++i)
        owners.push_back({0, i});
    const std::vector<core::HexCoord> origins{{0, 0}};
    const auto start = std::chrono::steady_clock::now();
    const auto deadline = start + std::chrono::hours(1);
    std::vector<int> ends;
    std::vector<SharedFactoryEmission> emissions;
    std::vector<FragmentCustodyEvent> custody;
    auto compile = [&](const std::function<std::chrono::steady_clock::time_point()>& clock) {
        return CompileRows(p, plan, nullptr, nullptr, &origins, true, &ends, false, 6, &supply,
                           nullptr, 3, false, false, false, false, &emissions, &owners, false,
                           nullptr, nullptr, deadline, clock, &custody);
    };
    std::size_t completeChecks = 0;
    compile([&] {
        ++completeChecks;
        return start;
    });
    ASSERT_EQ(ends.size(), 6U);
    ASSERT_EQ(emissions.size(), 24U);
    ASSERT_FALSE(custody.empty());
    ASSERT_GT(completeChecks, 100U);
    const auto originalEnds = ends;
    const auto originalEmissions = emissions;
    const auto originalCustody = custody;
    // The final cooperative check follows all product construction but precedes
    // publication: every staged vector is populated, not merely the entry check.
    for (const auto interruptAt : {completeChecks / 2, completeChecks}) {
        std::size_t checks = 0;
        EXPECT_THROW(compile([&] { return ++checks < interruptAt ? start : deadline; }),
                     std::invalid_argument);
        EXPECT_EQ(checks, interruptAt);
        EXPECT_EQ(ends, originalEnds);
        ASSERT_EQ(emissions.size(), originalEmissions.size());
        const auto emissionFields = [](const SharedFactoryEmission& e) {
            return std::tie(e.atom, e.consumer, e.product, e.position, e.consumerPosition,
                            e.orientation, e.beginCycle, e.holderReleaseCycle, e.readyCycle,
                            e.releaseCycle, e.reservationEnd, e.sourceIdentity, e.sourceInput,
                            e.sourceAtom, e.sourceBatch, e.sourceRemainder, e.reactionInputs,
                            e.type, e.quantity);
        };
        for (std::size_t i = 0; i < emissions.size(); ++i)
            EXPECT_EQ(emissionFields(emissions[i]), emissionFields(originalEmissions[i]));
        ASSERT_EQ(custody.size(), originalCustody.size());
        const auto custodyFields = [](const FragmentCustodyEvent& e) {
            return std::tie(e.product, e.sourceAtom, e.sourceBatch, e.type, e.buffer, e.begin,
                            e.end, e.position, e.stored, e.regenerated);
        };
        for (std::size_t i = 0; i < custody.size(); ++i)
            EXPECT_EQ(custodyFields(custody[i]), custodyFields(originalCustody[i]));
    }
}
TEST(RenewableFragmentTransportTest, RejectsNonClosingGroupedSupply) {
    auto p = RenewableTree();
    p.inputs[0].atoms.resize(2);
    p.inputs[0].bonds.resize(1);
    p.inputs[0].atoms[0].type = static_cast<core::AtomType>(10);
    p.inputs[0].atoms[1].type = static_cast<core::AtomType>(11);
    for (int i = 0; i < 4; ++i)
        p.outputs[0].atoms[i].type = static_cast<core::AtomType>(i < 2 ? 10 : 11);
    EXPECT_THROW(CompileRenewableFragmentTransport(p), std::invalid_argument);
}
TEST(RenewableFragmentTransportTest, RejectsPermissionsGraphResidualAndDeadline) {
    const auto p = RenewableTree();
    for (int bit : {2, 3, 9}) {
        auto bad = p;
        bad.permissionBits &= ~(1ULL << bit);
        EXPECT_THROW(CompileRenewableFragmentTransport(bad), std::invalid_argument);
    }
    auto bad = p;
    bad.inputs[0].bonds.pop_back();
    EXPECT_THROW(CompileRenewableFragmentTransport(bad), std::invalid_argument);
    bad = p;
    bad.inputs[0].bonds.push_back({1, {0, 0}, {3, 0}});
    EXPECT_THROW(CompileRenewableFragmentTransport(bad), std::invalid_argument);
    bad = p;
    bad.inputs[0].bonds[0].bits = 2;
    EXPECT_THROW(CompileRenewableFragmentTransport(bad), std::invalid_argument);
    bad = p;
    bad.outputs[0].atoms.pop_back();
    bad.outputs[0].bonds.pop_back();
    EXPECT_THROW(CompileRenewableFragmentTransport(bad), std::invalid_argument);
    EXPECT_THROW(CompileRenewableFragmentTransport(p, std::chrono::steady_clock::now()),
                 std::invalid_argument);
}
}} // namespace opusoptima::solvers
