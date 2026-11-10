#include "FixedArmIntervals.hpp"
#include "FixedArmInventory.hpp"

#include <gtest/gtest.h>
#include <limits>

namespace opusoptima::solvers { namespace {
using IR = ExecutionIR;
TEST(FixedArmIntervals, RejectsInteriorSweptAtomWithFreeEndPoses) {
    IR::State state;
    state.atoms.emplace(1, IR::Atom{core::AtomType::Air, {1, 0}});
    state.atoms.emplace(2, IR::Atom{core::AtomType::Salt, {2, 0}});
    state.atoms.emplace(3, IR::Atom{core::AtomType::Fire, {1, 1}});
    state.bonds[{1, 2}] = 1;
    const HeldArmState from{0, 1, 0, 0}, to{0, 1, 1, 1};
    EXPECT_FALSE(CertifyFixedArmInterval(state, {}, {}, from, to, 'r', true));
    state.atoms.erase(3);
    EXPECT_TRUE(CertifyFixedArmInterval(state, {}, {}, from, to, 'r', true));
    EXPECT_FALSE(CertifyFixedArmInterval(state, {}, {{1, 1}}, from, to, 'r', true));
    EXPECT_FALSE(CertifyFixedArmInterval(state, {}, {}, from, from, 'r', true));
    EXPECT_TRUE(CertifyFixedArmInterval(state, {}, {}, from, from, 'G', false));
}
TEST(FixedArmIntervals, CertifiesLongArmAndPivotAroundTheActualGrip) {
    IR::State state;
    state.atoms = {{1, {core::AtomType::Fire, {3, 0}}}};
    EXPECT_TRUE(CertifyFixedArmInterval(state, {}, {}, {0, 3, 0, 0}, {0, 3, 1, 1}, 'r', true));
    state.atoms.emplace(2, IR::Atom{core::AtomType::Salt, {2, 1}});
    EXPECT_FALSE(CertifyFixedArmInterval(state, {}, {}, {0, 3, 0, 0}, {0, 3, 1, 1}, 'r', true));
    state.atoms = {{1, {core::AtomType::Lead, {1, 0}}}, {2, {core::AtomType::Quicksilver, {2, 0}}}};
    state.bonds[{1, 2}] = 14;
    EXPECT_TRUE(CertifyFixedArmInterval(state, {}, {}, {0, 1, 0, 0}, {0, 1, 0, 1}, 'p', true));
    EXPECT_FALSE(CertifyFixedArmInterval(state, {}, {}, {0, 1, 0, 0}, {0, 1, 0, 1}, 'p', false));
    state.atoms.emplace(3, IR::Atom{core::AtomType::Salt, {1, 1}});
    EXPECT_FALSE(CertifyFixedArmInterval(state, {}, {}, {0, 1, 0, 0}, {0, 1, 0, 1}, 'p', true));
}

TEST(FixedArmIntervals, RejectsOutOfRangeLocalCoordinatesBeforeFrameArithmetic) {
    io::PuzzleData p;
    p.version = 3;
    p.outputMultiplier = 1;
    p.permissionBits = (1ULL << 0) | (1ULL << 22) | (1ULL << 23);
    p.inputs = {{{{core::AtomType::Salt, {std::numeric_limits<int>::min(), 0}}}, {}}};
    p.outputs = p.inputs;
    bool accepted = false;
    EXPECT_FALSE(CompileAcceptedFixedArmInventory(p, [&](const auto&) {
        accepted = true;
        return true;
    }));
    EXPECT_FALSE(accepted);
}
}} // namespace opusoptima::solvers
