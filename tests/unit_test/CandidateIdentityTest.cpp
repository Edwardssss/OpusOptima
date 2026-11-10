#include "CandidateIdentity.hpp"

#include <gtest/gtest.h>

namespace opusoptima::solvers {
TEST(CandidateIdentity, IgnoresMeasuredMetricsButNotMechanics) {
    io::SolutionData a;
    a.objects.push_back({"input", {0, 0}, {}, {1, 0, 0, 0, 0}});
    a.objects.push_back({"arm1", {1, 0}, {{'G', 0}, {'r', 1}}, {1, 0, 0, 2, 0}});
    auto b = a;
    b.solutionName = "another compiler";
    b.metrics.cycles = 137;
    EXPECT_EQ(PhysicalCandidateKey(a), PhysicalCandidateKey(b));
    b.objects[1].instructions[1].action = 'R';
    EXPECT_NE(PhysicalCandidateKey(a), PhysicalCandidateKey(b));
    b = a;
    b.objects[0].position.q = 3;
    EXPECT_NE(PhysicalCandidateKey(a), PhysicalCandidateKey(b));
}
TEST(CandidateIdentity, PreservesOrderOfOverlappingTracks) {
    io::SolutionData a;
    a.objects.push_back({"track", {}, {}, {1, 0, 0, 0, 2, 0, 0, 1, 0, 0}});
    a.objects.push_back({"track", {}, {}, {1, 0, 0, 0, 2, 0, 0, 0, 1, 0}});
    auto b = a;
    std::swap(b.objects[0], b.objects[1]);
    EXPECT_NE(PhysicalCandidateKey(a), PhysicalCandidateKey(b));
}
} // namespace opusoptima::solvers
