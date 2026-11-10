#include <gtest/gtest.h>

#include "FiniteFeedResource.hpp"
#include "ModuleHandoff.hpp"

using namespace opusoptima::solvers;

TEST(FiniteFeedResourceTest, IncludesCleanupAndRequiresEverySlice) {
    const ModulePort port{{3, 7}, 0, false};
    FiniteFeedResource feed(port, {4, 8, 12}, 15);
    EXPECT_TRUE(feed.Complete());
    int time = 0;
    const std::vector<FiniteFeedResource::Slice> expected{{0, 4}, {4, 8}, {8, 15}};
    for (int cycle = 0; cycle < 2; ++cycle) {
        for (std::size_t i = 0; i < expected.size(); ++i) {
            const auto slice = feed.Reserve(time);
            EXPECT_EQ(slice, expected[i]);
            EXPECT_FALSE(feed.Complete());
            time += slice.end - slice.begin;
            feed.Release(port, time, time + 2);
            time += 2;
            EXPECT_EQ(feed.Complete(), i + 1 == expected.size());
        }
    }
}

TEST(FiniteFeedResourceTest, RejectsInvalidBoundariesAndPrematureReuse) {
    const ModulePort port{{3, 7}, 0, false};
    for (const auto& ends : std::vector<std::vector<int>>{{0}, {4, 4}, {8, 4}, {11}, {4, 11}})
        EXPECT_THROW((FiniteFeedResource(port, ends, 10)), std::invalid_argument);
    EXPECT_THROW((FiniteFeedResource(port, {}, 0)), std::invalid_argument);
    FiniteFeedResource feed(port, {}, 10);
    EXPECT_EQ(feed.Reserve(0), (FiniteFeedResource::Slice{0, 10}));
    EXPECT_THROW(feed.Reserve(20), std::invalid_argument);
    EXPECT_THROW(feed.Release(port, 9, 11), std::invalid_argument);
    EXPECT_THROW(feed.Release(port, 10, 10), std::invalid_argument);
    auto other = port;
    other.position.q++;
    EXPECT_THROW(feed.Release(other, 10, 12), std::invalid_argument);
    EXPECT_FALSE(feed.Complete());
    feed.Release(port, 10, 12);
    EXPECT_TRUE(feed.Complete());
    EXPECT_THROW(feed.Release(port, 12, 13), std::invalid_argument);
    EXPECT_THROW(feed.Reserve(11), std::invalid_argument);
    EXPECT_EQ(feed.Reserve(12), (FiniteFeedResource::Slice{0, 10}));
    feed.Release(port, 22, 24);
    EXPECT_TRUE(feed.Complete());
}

TEST(FiniteFeedResourceTest, ExactDemandRejectsMissingAndExtraBatches) {
    const ModulePort port{{3, 7}, 0, false};
    EXPECT_THROW((FiniteFeedResource(port, {2, 4}, 4, 0)), std::invalid_argument);
    EXPECT_THROW((FiniteFeedResource(port, {2, 4}, 4, 3)), std::invalid_argument);
    FiniteFeedResource feed(port, {2, 4}, 4, 4);
    EXPECT_FALSE(feed.Complete());
    for (int i = 0; i < 4; ++i) {
        feed.Reserve(i * 4);
        feed.Release(port, i * 4 + 2, i * 4 + 3);
        EXPECT_EQ(feed.Complete(), i == 3);
    }
    EXPECT_THROW(feed.Reserve(16), std::invalid_argument);
    EXPECT_TRUE(feed.Complete());
}

TEST(ModuleHandoffTest, AcceptsSerialPhysicalExit) {
    ModulePort port{{0, 1}, 0, false};
    EXPECT_NO_THROW(
        ValidateModuleHandoffs({{port, port, 28, 30, 32, 68}, {port, port, 68, 70, 72, 108}}));
}

TEST(ModuleHandoffTest, RejectsIncompatiblePoseAndTiming) {
    ModulePort port{{0, 1}, 0, false};
    ModuleHandoff valid{port, port, 28, 30, 32, 68};
    for (int field = 0; field < 7; ++field) {
        auto h = valid;
        switch (field) {
        case 0:
            h.consumed.position = {1, 0};
            break;
        case 1:
            h.consumed.orientation = 1;
            break;
        case 2:
            h.consumed.held = true;
            break;
        case 3:
            h.pickup = 27;
            break;
        case 4:
            h.cleared = 30;
            break;
        case 5:
            h.nextArrival = 31;
            break;
        case 6:
            h.produced.orientation = h.consumed.orientation = 6;
            break;
        }
        EXPECT_THROW(ValidateModuleHandoffs({h}), std::invalid_argument);
    }
    EXPECT_THROW(ValidateModuleHandoffs({valid, valid}), std::invalid_argument);
}
