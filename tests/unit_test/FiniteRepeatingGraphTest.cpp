#include <gtest/gtest.h>

#include <climits>
#include <limits>

#include "FiniteRepeatingGraph.hpp"
#include "FiniteRepeatingProgress.hpp"
#include "IndependentVerifier.hpp"
#include <stdexcept>
#include "IndependentCapabilities.hpp"

namespace {
using namespace opusoptima;
using namespace opusoptima::verification;
using A = core::AtomType;
using K = RepeatingGraphDifferenceKind;
using F = RepeatingGraphFailureCode;

io::Molecule saltTemplate() {
    return {{{A::Salt, {0, 0}}, {A::Repeat, {1, 0}}}, {{1, {0, 0}, {1, 0}}}};
}
io::Molecule chain(int size, bool heterogeneous = false, int sign = 1) {
    io::Molecule graph;
    for (int q = 0; q < size; ++q) {
        graph.atoms.push_back({heterogeneous && q % 2 ? A::Fire : A::Salt, {sign * q, 0}});
        if (q) graph.bonds.push_back({1, {sign * (q - 1), 0}, {sign * q, 0}});
    }
    return graph;
}
FiniteRepeatingGraphChecked checked(const io::Molecule& raw, const io::Molecule& actual,
                                   std::size_t n, core::HexCoord d = {1, 0}) {
    const auto result = CheckFiniteRepeatingGraph({raw, actual, d, n});
    if (const auto* failure = std::get_if<FiniteRepeatingGraphFailure>(&result)) {
        ADD_FAILURE() << failure->detail;
        return {};
    }
    return std::get<FiniteRepeatingGraphChecked>(result);
}
void fails(const io::Molecule& raw, const io::Molecule& actual, F code,
           core::HexCoord d = {1, 0}, std::size_t n = 6) {
    const auto result = CheckFiniteRepeatingGraph({raw, actual, d, n});
    ASSERT_TRUE(std::holds_alternative<FiniteRepeatingGraphFailure>(result));
    EXPECT_EQ(std::get<FiniteRepeatingGraphFailure>(result).code, code);
}
void same(const io::Molecule& a, const io::Molecule& b) {
    ASSERT_EQ(a.atoms.size(), b.atoms.size());
    ASSERT_EQ(a.bonds.size(), b.bonds.size());
    for (std::size_t i = 0; i < a.atoms.size(); ++i) {
        EXPECT_EQ(a.atoms[i].type, b.atoms[i].type);
        EXPECT_EQ(a.atoms[i].position, b.atoms[i].position);
    }
    for (std::size_t i = 0; i < a.bonds.size(); ++i) {
        EXPECT_EQ(a.bonds[i].bits, b.bonds[i].bits);
        EXPECT_EQ(a.bonds[i].from, b.bonds[i].from);
        EXPECT_EQ(a.bonds[i].to, b.bonds[i].to);
    }
}
TEST(FiniteRepeatingGraph, LiteralHomogeneousFixtureAndSnapshotPurity) {
    // Literal data consumed by FiniteRepeatingPrefixRequiresIntermonomerBonds.
    auto raw = saltTemplate();
    auto actual = chain(6);
    const auto originalRaw = raw, originalActual = actual;
    EXPECT_EQ(checked(raw, actual, 1).provenPrefix, 1U);
    for (int repeat = 0; repeat < 2; ++repeat) {
        const auto all = checked(raw, actual, 6);
        EXPECT_EQ(all.provenPrefix, 6U);
        EXPECT_FALSE(all.firstDifference);
        const auto next = checked(raw, actual, 7);
        ASSERT_TRUE(next.firstDifference);
        EXPECT_EQ(next.provenPrefix, 6U);
        EXPECT_EQ(next.firstDifference->monomerCount, 7U);
        EXPECT_EQ(next.firstDifference->kind, K::MissingAtom);
        EXPECT_EQ(next.firstDifference->cell.q, 6);
        EXPECT_EQ(next.firstDifference->expectedType, A::Salt);
        EXPECT_FALSE(next.firstDifference->actualType);
    }
    same(raw, originalRaw); same(actual, originalActual);
    std::swap(raw.atoms.front(), raw.atoms.back());
    EXPECT_EQ(checked(raw, actual, 6).provenPrefix, 6U);
    EXPECT_EQ(checked(raw, {}, 6).provenPrefix, 0U);
    // The broken six-mer must be submitted as two complete components, not a disconnected graph.
    EXPECT_EQ(checked(raw, chain(3), 6).provenPrefix, 3U);
    auto right = chain(3);
    for (auto& atom : right.atoms) atom.position.q += 3;
    for (auto& bond : right.bonds) { bond.from.q += 3; bond.to.q += 3; }
    EXPECT_EQ(checked(raw, right, 6).provenPrefix, 0U);
    actual.bonds.erase(actual.bonds.begin() + 2);
    fails(raw, actual, F::OutsideDomain);
}
TEST(FiniteRepeatingGraph, LiteralHeterogeneousFixtureThresholds) {
    // Literal data from FiniteRepeatingPrefixRequiresOrderedHeterogeneousMaterials;
    // prior reference table: ordered product 2/3/6 pass, swapped product 2 only.
    io::Molecule raw{{{A::Salt, {0, 0}}, {A::Fire, {1, 0}}, {A::Repeat, {2, 0}}},
                     {{1, {0, 0}, {1, 0}}, {1, {1, 0}, {2, 0}}}};
    auto actual = chain(12, true);
    for (std::size_t n : {2, 3, 6}) EXPECT_EQ(checked(raw, actual, n, {2, 0}).provenPrefix, n);
    std::swap(actual.atoms[4].type, actual.atoms[5].type);
    const auto result = checked(raw, actual, 6, {2, 0});
    ASSERT_TRUE(result.firstDifference);
    EXPECT_EQ(result.provenPrefix, 2U);
    EXPECT_EQ(result.firstDifference->kind, K::AtomType);
    EXPECT_EQ(result.firstDifference->cell.q, 4);
    EXPECT_EQ(result.firstDifference->expectedType, A::Salt);
    EXPECT_EQ(result.firstDifference->actualType, A::Fire);
    // Marker swapping, not stable removal, also handles a non-last marker.
    std::swap(raw.atoms[0], raw.atoms[2]);
    EXPECT_EQ(checked(raw, chain(12, true), 6, {2, 0}).provenPrefix, 6U);
}
TEST(FiniteRepeatingGraph, EveryRealTypeAndSixAxes) {
    for (int type = 1; type <= 16; ++type) {
        if (type == static_cast<int>(A::Repeat)) continue;
        auto raw = saltTemplate(); auto actual = chain(1);
        raw.atoms[0].type = actual.atoms[0].type = static_cast<A>(type);
        EXPECT_EQ(checked(raw, actual, 1).provenPrefix, 1U);
        actual.atoms[0].type = type == 1 ? A::Fire : A::Salt;
        EXPECT_EQ(checked(raw, actual, 1).firstDifference->kind, K::AtomType);
    }
    for (core::HexCoord d : {core::HexCoord{1, 0}, {0, 1}, {-1, 1}, {-1, 0}, {0, -1}, {1, -1}}) {
        auto raw = saltTemplate(); raw.atoms[1].position = d; raw.bonds[0].to = d;
        io::Molecule actual{{{A::Salt, {}}, {A::Salt, d}}, {{1, {}, d}}};
        EXPECT_EQ(checked(raw, actual, 2, d).provenPrefix, 2U);
    }
}
TEST(FiniteRepeatingGraph, BoundaryMaskIsDirectionalAndDoesNotMaskType) {
    auto raw = saltTemplate();
    io::Molecule extended{{{A::Fire, {-1, 0}}, {A::Salt, {0, 0}}, {A::Water, {1, 0}}},
                          {{1, {-1, 0}, {0, 0}}, {1, {0, 0}, {1, 0}}}};
    EXPECT_EQ(checked(raw, extended, 1).provenPrefix, 1U);
    EXPECT_EQ(checked(raw, chain(1), 1).provenPrefix, 1U);
    EXPECT_EQ(checked(raw, extended, 2).firstDifference->kind, K::AtomType);
    extended.atoms.push_back({A::Salt, {0, 1}});
    extended.bonds.push_back({1, {0, 0}, {0, 1}});
    const auto result = checked(raw, extended, 1);
    ASSERT_TRUE(result.firstDifference);
    const auto& diff = *result.firstDifference;
    EXPECT_EQ(diff.kind, K::BondMask);
    EXPECT_EQ(diff.cell.q, 0); EXPECT_EQ(diff.cell.r, 0);
    ASSERT_TRUE(diff.neighbor);
    EXPECT_EQ(diff.neighbor->q, 0); EXPECT_EQ(diff.neighbor->r, 1);
    EXPECT_EQ(diff.expectedBits, 0); EXPECT_EQ(diff.actualBits, 1); EXPECT_EQ(diff.allowedBits, 0);
}
// Two-row triangle monomer; the lower row bridges copies and the upper row has envelope holes.
io::Molecule triangleTemplate() {
    return {{{A::Salt, {0, 0}}, {A::Fire, {1, 0}}, {A::Water, {0, 1}}, {A::Repeat, {2, 0}}},
            {{1, {0, 0}, {1, 0}}, {1, {0, 0}, {0, 1}},
             {1, {1, 0}, {0, 1}}, {1, {1, 0}, {2, 0}}}};
}
io::Molecule triangleMaterial() {
    return {{{A::Salt, {0, 0}}, {A::Fire, {1, 0}}, {A::Water, {0, 1}},
             {A::Salt, {2, 0}}, {A::Fire, {3, 0}}, {A::Water, {2, 1}}},
            {{1, {0, 0}, {1, 0}}, {1, {0, 0}, {0, 1}}, {1, {1, 0}, {0, 1}},
             {1, {1, 0}, {2, 0}}, {1, {2, 0}, {3, 0}},
             {1, {2, 0}, {2, 1}}, {1, {3, 0}, {2, 1}}}};
}
TEST(FiniteRepeatingGraph, ConnectedMissingAndExtraBondsAndEnvelopeHoles) {
    const auto raw = triangleTemplate(); auto actual = triangleMaterial();
    EXPECT_EQ(checked(raw, actual, 2, {2, 0}).provenPrefix, 2U);
    actual.bonds.erase(actual.bonds.begin()); // Triangle stays connected.
    auto result = checked(raw, actual, 2, {2, 0});
    ASSERT_TRUE(result.firstDifference);
    EXPECT_EQ(result.firstDifference->kind, K::BondMask);
    EXPECT_EQ(result.firstDifference->expectedBits, 1);
    EXPECT_EQ(result.firstDifference->actualBits, 0);
    actual = triangleMaterial();
    actual.bonds.push_back({1, {2, 0}, {2, 1}}); // Structural duplicates are never masks.
    fails(raw, actual, F::Malformed, {2, 0});
    actual = triangleMaterial();
    actual.bonds.push_back({1, {1, 0}, {2, 0}});
    fails(raw, actual, F::Malformed, {2, 0});
    actual = triangleMaterial();
    actual.atoms.push_back({A::Salt, {1, 1}});
    actual.bonds.push_back({1, {1, 0}, {1, 1}});
    result = checked(raw, actual, 2, {2, 0});
    EXPECT_EQ(result.provenPrefix, 1U); // (1,1) ignored at k=1, envelope hole at k=2.
    EXPECT_EQ(result.firstDifference->kind, K::BondMask);
    // No extra bond from an expected atom: the hole is reached from an ignored atom.
    actual = triangleMaterial();
    actual.atoms.push_back({A::Salt, {1, 1}});
    actual.atoms.push_back({A::Salt, {1, 2}});
    actual.atoms.push_back({A::Salt, {0, 2}});
    actual.bonds.push_back({1, {0, 1}, {0, 2}});
    actual.bonds.push_back({1, {0, 2}, {1, 2}});
    actual.bonds.push_back({1, {1, 2}, {1, 1}});
    EXPECT_EQ(checked(raw, actual, 2, {2, 0}).firstDifference->kind, K::BondMask);
}
TEST(FiniteRepeatingGraph, StructuralPriorityAndDomainRejections) {
    const auto raw = saltTemplate();
    auto bad = chain(2); bad.atoms.push_back(bad.atoms.front());
    fails(raw, bad, F::Malformed, {1, 1});
    bad = chain(2); bad.bonds.push_back({1, {1, 0}, {0, 0}});
    fails(raw, bad, F::Malformed);
    bad = chain(2); bad.bonds[0].to = {2, 0}; fails(raw, bad, F::Malformed);
    bad = chain(2); bad.bonds[0].to = {}; fails(raw, bad, F::Malformed);
    bad = chain(2); bad.atoms[1].position = {3, 0}; bad.bonds[0].to = {3, 0};
    fails(raw, bad, F::Malformed);
    for (std::uint8_t bits : {0, 3, 16}) {
        bad = chain(2); bad.bonds[0].bits = bits; fails(raw, bad, F::Malformed);
    }
    bad = chain(1); bad.atoms[0].type = static_cast<A>(0); fails(raw, bad, F::Malformed);
    bad.atoms[0].type = static_cast<A>(17); fails(raw, bad, F::Malformed);
    bad = chain(2); bad.bonds[0].bits = 6; fails(raw, bad, F::OutsideDomain);
    bad = chain(1); bad.atoms[0].type = A::Repeat; fails(raw, bad, F::OutsideDomain);
    fails(raw, {}, F::Malformed, {}, 1);
    fails(raw, {}, F::Malformed, {2, 0}, 1);
    fails(raw, {}, F::Malformed, {1, 0}, 0);
    auto domain = raw; domain.atoms[1].position = {1, 1}; domain.bonds.clear();
    fails(domain, {}, F::OutsideDomain, {1, 1});
    fails({}, {}, F::OutsideDomain);
    domain = raw; domain.atoms[0].type = A::Repeat; fails(domain, {}, F::OutsideDomain);
    domain = raw; domain.atoms.push_back({A::Salt, {1, 0}}); fails(domain, {}, F::Malformed);
    domain = raw; domain.atoms.push_back({A::Fire, {2, 0}}); // Width overlaps copies.
    fails(domain, {}, F::OutsideDomain);
    domain = raw; domain.atoms.push_back({A::Fire, {0, 2}}); fails(domain, {}, F::OutsideDomain);
    domain = raw; domain.atoms[0].position = {1, -1}; domain.bonds[0].from = {1, -1};
    fails(domain, {}, F::OutsideDomain); // no real origin
    // This diagonal marker bond does have a reciprocal periodic destination.
    domain = {{{A::Salt, {}}, {A::Fire, {0, 1}}, {A::Repeat, {1, 0}}},
              {{1, {}, {0, 1}}, {1, {0, 1}, {1, 0}}}};
    EXPECT_EQ(checked(domain, {}, 1).provenPrefix, 0U);
}
TEST(FiniteRepeatingGraph, InventoryAndInt32Extremes) {
    const auto raw = saltTemplate();
    EXPECT_EQ(checked(raw, {}, finiteOrdinaryProfile.maxInventory).provenPrefix, 0U);
    fails(raw, {}, F::OutsideDomain, {1, 0}, finiteOrdinaryProfile.maxInventory + 1);
    fails(raw, {}, F::OutsideDomain, {1, 0}, std::numeric_limits<std::size_t>::max());
    auto actual = chain(static_cast<int>(finiteOrdinaryProfile.maxInventory));
    EXPECT_EQ(checked(raw, actual, 1).provenPrefix, 1U);
    actual.atoms.push_back({A::Salt, {8192, 0}});
    fails(raw, actual, F::OutsideDomain);
    actual = {{{A::Salt, {INT_MAX, INT_MIN}}}, {}};
    EXPECT_EQ(checked(raw, actual, 1).firstDifference->kind, K::MissingAtom);
    auto extreme = raw; extreme.atoms[1].position = {INT_MIN, INT_MAX}; extreme.bonds.clear();
    fails(extreme, {}, F::OutsideDomain, {INT_MIN, INT_MAX});
    extreme = raw; extreme.atoms.push_back({A::Fire, {INT_MIN, INT_MAX}});
    extreme.bonds.push_back({1, {INT_MIN, INT_MAX}, {0, 0}});
    fails(extreme, {}, F::Malformed);
    auto huge = raw;
    while (huge.atoms.size() <= finiteOrdinaryProfile.maxMoleculeAtoms) huge.atoms.push_back(huge.atoms[0]);
    fails(huge, {}, F::OutsideDomain); // resource precheck before duplicate scan
    huge = raw; huge.bonds.resize(7, huge.bonds.front()); fails(huge, {}, F::OutsideDomain);
}
TEST(FiniteRepeatingProgress, RealSnapshotSequenceIsNonConsumingAndPortIsolated) {
    const std::array configs{FiniteRepeatingPortConfig{7, 3},
                             FiniteRepeatingPortConfig{6, 8, 2}};
    FiniteRepeatingProgress state(configs);
    const auto raw = saltTemplate(); const auto originalRaw = raw;
    std::size_t highWater = 0;
    for (int length : {1, 3, 3, 2, 0, 6, 6, 1}) {
        auto material = chain(length); const auto original = material;
        const auto result = state.Observe(0, raw, material, {1, 0});
        ASSERT_TRUE(std::holds_alternative<FiniteRepeatingPortProgress>(result));
        const auto progress = std::get<FiniteRepeatingPortProgress>(result);
        highWater = std::max(highWater, static_cast<std::size_t>(length));
        const auto expected = highWater;
        EXPECT_EQ(progress.highWaterMonomers, expected);
        EXPECT_EQ(progress.outputCount, expected * 3);
        EXPECT_EQ(progress.targetOutputs, 18U);
        EXPECT_EQ(progress.reached, expected >= 6);
        EXPECT_EQ(state.Port(1)->outputCount, 0U);
        same(material, original); same(raw, originalRaw);
    }
    auto second = chain(2); const auto originalSecond = second;
    ASSERT_TRUE(std::holds_alternative<FiniteRepeatingPortProgress>(
        state.Observe(1, raw, second, {1, 0})));
    EXPECT_EQ(state.Port(1)->outputCount, 16U);
    EXPECT_TRUE(state.Port(1)->reached);
    EXPECT_EQ(state.Port(0)->outputCount, 18U);
    same(second, originalSecond);
}
TEST(FiniteRepeatingProgress, HeterogeneousFiniteThresholdsAndShortening) {
    const io::Molecule raw{{{A::Salt, {0, 0}}, {A::Fire, {1, 0}}, {A::Repeat, {2, 0}}},
                           {{1, {0, 0}, {1, 0}}, {1, {1, 0}, {2, 0}}}};
    const std::array configs{FiniteRepeatingPortConfig{6, 2, 3}};
    FiniteRepeatingProgress state(configs);
    auto material = chain(12, true);
    std::swap(material.atoms[4].type, material.atoms[5].type);
    const auto swapped = material;
    ASSERT_TRUE(std::holds_alternative<FiniteRepeatingPortProgress>(
        state.Observe(0, raw, material, {2, 0})));
    EXPECT_EQ(state.Port(0)->highWaterMonomers, 2U);
    EXPECT_FALSE(state.Port(0)->reached); same(material, swapped);
    for (int length : {6, 12, 4}) {
        const auto snapshot = chain(length, true); const auto original = snapshot;
        ASSERT_TRUE(std::holds_alternative<FiniteRepeatingPortProgress>(
            state.Observe(0, raw, snapshot, {2, 0})));
        EXPECT_EQ(state.Port(0)->highWaterMonomers, length == 6 ? 3U : 6U);
        EXPECT_TRUE(state.Port(0)->reached); same(snapshot, original);
    }
}
TEST(FiniteRepeatingProgress, FailuresAreAtomicAndBounded) {
    const std::array configs{FiniteRepeatingPortConfig{6, 1}};
    FiniteRepeatingProgress state(configs);
    auto raw = saltTemplate(); const auto three = chain(3);
    ASSERT_TRUE(std::holds_alternative<FiniteRepeatingPortProgress>(
        state.Observe(0, raw, three, {1, 0})));
    const auto before = *state.Port(0);
    auto assertUnchanged = [&] {
        const auto after = *state.Port(0);
        EXPECT_EQ(after.highWaterMonomers, before.highWaterMonomers);
        EXPECT_EQ(after.outputCount, before.outputCount);
        EXPECT_EQ(after.targetOutputs, before.targetOutputs);
        EXPECT_EQ(after.reached, before.reached);
    };
    auto bad = chain(6); bad.bonds.erase(bad.bonds.begin() + 2);
    for (int kind = 0; kind < 4; ++kind) {
        auto templ = raw, material = bad;
        if (kind == 1) { material = three; material.atoms.push_back(material.atoms[0]); }
        if (kind == 2) { material = three; templ.atoms.resize(65, templ.atoms[0]); }
        if (kind == 3) { material = three; material.atoms.resize(8193, material.atoms[0]); }
        const auto original = material, originalTemplate = templ;
        EXPECT_TRUE(std::holds_alternative<FiniteRepeatingGraphFailure>(
            state.Observe(0, templ, material, {1, 0})));
        assertUnchanged(); same(material, original); same(templ, originalTemplate);
    }
    EXPECT_TRUE(std::holds_alternative<FiniteRepeatingGraphFailure>(
        state.Observe(16, raw, three, {1, 0})));
    EXPECT_FALSE(state.Port(16)); assertUnchanged();
    FiniteRepeatingProgress expired(configs, std::chrono::steady_clock::now());
    const auto timeout = expired.Observe(0, raw, three, {1, 0});
    ASSERT_TRUE(std::holds_alternative<FiniteRepeatingGraphFailure>(timeout));
    EXPECT_EQ(std::get<FiniteRepeatingGraphFailure>(timeout).code, F::ResourceUnavailable);
    EXPECT_EQ(expired.Port(0)->outputCount, 0U);
    const std::array<FiniteRepeatingPortConfig, 7> invalid{{{8193, 1}, {6, 0}, {6, 9},
                                                          {0, 1}, {6, 1, 0}, {7, 1, 7}, {2, 1, 3}}};
    for (const auto config : invalid) {
        const std::array one{config};
        EXPECT_THROW((FiniteRepeatingProgress{one}), std::invalid_argument);
    }
    EXPECT_THROW((FiniteRepeatingProgress{std::span<const FiniteRepeatingPortConfig>{}}),
                 std::invalid_argument);
    std::array<FiniteRepeatingPortConfig, 17> tooMany{};
    EXPECT_THROW((FiniteRepeatingProgress{tooMany}), std::invalid_argument);
    std::array<FiniteRepeatingPortConfig, 16> maximum;
    maximum.fill({8192, 8});
    FiniteRepeatingProgress bounded(maximum);
    const auto rawBefore = raw;
    const auto atBound = chain(8192);
    const auto atBoundBefore = atBound;
    const auto result = bounded.Observe(15, raw, atBound, {1, 0});
    ASSERT_TRUE(std::holds_alternative<FiniteRepeatingGraphFailure>(result));
    EXPECT_EQ(std::get<FiniteRepeatingGraphFailure>(result).code,
              RepeatingGraphFailureCode::ResourceUnavailable);
    EXPECT_EQ(bounded.Port(15)->highWaterMonomers, 0U);
    EXPECT_EQ(bounded.Port(15)->outputCount, 0U);
    same(raw, rawBefore); same(atBound, atBoundBefore);
}
TEST(FiniteRepeatingProgress, RawRepeatingDefaultRemainsUnavailable) {
    io::PuzzleData puzzle; puzzle.name = "finite repeating prefix"; puzzle.outputMultiplier = 1;
    puzzle.inputs.push_back(chain(6)); puzzle.outputs.push_back(saltTemplate());
    io::SolutionData machine; machine.puzzleName = puzzle.name;
    machine.objects = {{"input", {}, {}, {1, 0, 0, 0, 0}},
                       {"out-rep", {}, {}, {1, 0, 0, 0, 0}},
                       {"arm1", {0, -5}, {{'G', 0}, {'g', 1}}, {1, 0, 0, 0, 0}}};
    EXPECT_EQ(ReplayFiniteMachine(puzzle, machine).outcome, ReplayOutcome::Unavailable);
    EXPECT_EQ(VerifyIndependent(puzzle, machine).status, io::VerificationStatus::Unavailable);
}
} // namespace
