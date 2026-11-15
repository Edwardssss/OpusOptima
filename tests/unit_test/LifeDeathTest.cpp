#include "LifeDeath.hpp"
#include "LifeDeathParking.hpp"
#include "LifeDeathStorage.hpp"
#include "BinaryWriter.hpp"
#include "RepoRoot.hpp"
#include "SolutionVerifier.hpp"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <limits>
#include <stdexcept>
#include <tuple>

namespace {
using namespace opusoptima;
io::SolutionData ParkingFixture(int cells = 130) {
    io::SolutionData s;
    io::SolutionObject track;
    track.typeName = "track";
    track.bodyData = {1, 0, 0, 0, cells};
    for (int r = 1 - cells; r <= 0; ++r) track.bodyData.insert(track.bodyData.end(), {0, r});
    track.bodyData.push_back(0);
    s.objects.push_back(track);
    io::SolutionObject arm;
    arm.typeName = "piston";
    arm.instructions = {{'G', 0}, {'a', 1}, {'A', 2}, {'B', 3}};
    s.objects.push_back(arm);
    return s;
}
TEST(LifeDeathTest, StorageGlobalOverlapAndHaltSuffix) {
    using solvers::detail::TrimLifeDeathStorage;
    io::SolutionData machine;
    machine.objects.push_back({"track", {0, 0}, {},
        {1, 0, 0, 0, 6, 0, 0, 1, 0, 2, 0, 3, 0, 4, 0, 5, 0, 7}});
    // Reverse overlapping rail: earlier serialized rail wins.
    machine.objects.push_back({"track", {0, 0}, {},
        {1, 4, 0, 0, 6, 5, 0, 4, 0, 3, 0, 2, 0, 1, 0, 0, 0, 9}});
    machine.objects.push_back({"arm1", {1, 0},
        {{'A', 0}, {'B', 1}, {'a', 2}, {'A', 3}}, {1, 0, 0, 0, 0}});
    auto expected = machine;
    ASSERT_TRUE(TrimLifeDeathStorage(machine, {0, 1}));
    EXPECT_EQ(machine.objects[0].bodyData,
        (std::vector<int>{1, 0, 0, 0, 2, 1, 0, 2, 0, 7}));
    EXPECT_EQ(machine.objects[1].bodyData,
        (std::vector<int>{1, 4, 0, 0, 2, 2, 0, 1, 0, 9}));
    expected.objects[0].bodyData = machine.objects[0].bodyData;
    expected.objects[1].bodyData = machine.objects[1].bodyData;
    EXPECT_EQ(io::BinaryWriter::WriteSolution(machine), io::BinaryWriter::WriteSolution(expected));
}

TEST(LifeDeathTest, StoragePartialOverlapDirectionalEndpointWrites) {
    // At (2,0), earlier rail's minus endpoint must overwrite the later
    // rail's nonzero minus edge; at (4,0), its plus endpoint does likewise.
    // Test directions independently so copying both edges on a write fails.
    for (const char direction : {'A', 'a'}) {
        SCOPED_TRACE(direction);
        io::SolutionData machine;
        machine.objects.push_back({"track", {0, 0}, {},
            {1, 0, 0, 0, 3, 2, 0, 3, 0, 4, 0, 7}});
        machine.objects.push_back({"track", {0, 0}, {},
            {1, 0, 0, 0, 7, 0, 0, 1, 0, 2, 0, 3, 0, 4, 0, 5, 0, 6, 0, 9}});
        machine.objects.push_back({"arm1", {3, 0},
            {{direction, 0}, {direction, 1}, {'B', 2}}, {1, 0, 0, 0, 0}});
        auto expected = machine;
        const int low = direction == 'A' ? 3 : 2;
        expected.objects[0].bodyData = {1, 0, 0, 0, 2, low, 0, low + 1, 0, 7};
        expected.objects[1].bodyData = {1, 0, 0, 0, 2, low, 0, low + 1, 0, 9};
        ASSERT_TRUE(solvers::detail::TrimLifeDeathStorage(machine, {0, 1}));
        EXPECT_EQ(io::BinaryWriter::WriteSolution(machine),
                  io::BinaryWriter::WriteSolution(expected));
    }
}

TEST(LifeDeathTest, StorageOwnEnvelopeIgnoresCrossTrackVisitsOnlyAfterReplay) {
    io::SolutionData machine;
    // Earlier vertical rail owns both executed directions at the crossing.
    machine.objects.push_back({"track", {0, 0}, {},
        {1, 0, 0, 0, 5, 5, -2, 5, -1, 5, 0, 5, 1, 5, 2, 0}});
    machine.objects.push_back({"track", {0, 0}, {},
        {1, 0, 0, 0, 7, 0, 0, 1, 0, 2, 0, 3, 0, 4, 0, 5, 0, 6, 0, 9}});
    machine.objects.push_back({"arm1", {1, 0},
        {{'A', 0}, {'B', 1}}, {1, 0, 0, 0, 0}});
    machine.objects.push_back({"arm1", {5, -1},
        {{'A', 0}, {'A', 1}, {'a', 2}, {'a', 3}, {'B', 4}}, {1, 0, 0, 0, 1}});
    auto expected = machine;
    expected.objects[1].bodyData = {1, 0, 0, 0, 2, 1, 0, 2, 0, 9};
    ASSERT_TRUE(solvers::detail::TrimLifeDeathStorage(machine, {1}));
    EXPECT_EQ(io::BinaryWriter::WriteSolution(machine), io::BinaryWriter::WriteSolution(expected));
}

TEST(LifeDeathTest, StorageOwnEdgeChangeRejectedAndConservativeFallbackRetained) {
    io::SolutionData machine;
    // Earlier storage owns A at (5,0). Removing it redirects the entrant to
    // (5,1) instead of (6,0), so own proposal MUST fail the full replay.
    machine.objects.push_back({"track", {0, 0}, {},
        {1, 0, 0, 0, 7, 0, 0, 1, 0, 2, 0, 3, 0, 4, 0, 5, 0, 6, 0, 9}});
    machine.objects.push_back({"track", {0, 0}, {},
        {1, 0, 0, 0, 5, 5, -2, 5, -1, 5, 0, 5, 1, 5, 2, 0}});
    machine.objects.push_back({"arm1", {1, 0},
        {{'A', 0}, {'B', 1}}, {1, 0, 0, 0, 0}});
    machine.objects.push_back({"arm1", {5, -1},
        {{'A', 0}, {'A', 1}, {'A', 2}, {'B', 3}}, {1, 0, 0, 0, 1}});
    auto expected = machine;
    expected.objects[0].bodyData =
        {1, 0, 0, 0, 6, 1, 0, 2, 0, 3, 0, 4, 0, 5, 0, 6, 0, 9};
    ASSERT_TRUE(solvers::detail::TrimLifeDeathStorage(machine, {0}));
    EXPECT_EQ(io::BinaryWriter::WriteSolution(machine), io::BinaryWriter::WriteSolution(expected));
}

TEST(LifeDeathTest, StorageWithoutInitialUserRemainsWholeInOwnProposal) {
    io::SolutionData machine;
    machine.objects.push_back({"track", {0, 0}, {},
        {1, 0, 0, 0, 4, 0, 0, 1, 0, 2, 0, 3, 0, 0}});
    machine.objects.push_back({"track", {10, 0}, {},
        {1, 0, 0, 0, 4, 0, 0, 1, 0, 2, 0, 3, 0, 0}});
    machine.objects.push_back({"arm1", {1, 0},
        {{'A', 0}, {'B', 1}}, {1, 0, 0, 0, 0}});
    auto expected = machine;
    expected.objects[0].bodyData = {1, 0, 0, 0, 2, 1, 0, 2, 0, 0};
    ASSERT_TRUE(solvers::detail::TrimLifeDeathStorage(machine, {0, 1}));
    EXPECT_EQ(io::BinaryWriter::WriteSolution(machine), io::BinaryWriter::WriteSolution(expected));
}

TEST(LifeDeathTest, StorageRepeatedDirectedEdgesAndNoOpAreTransactional) {
    io::SolutionData machine;
    machine.objects.push_back({"track", {0, 0}, {},
        {1, 0, 0, 0, 5, 0, 0, 1, 0, 2, 0, 3, 0, 4, 0, 0}});
    machine.objects.push_back({"arm1", {1, 0}, {}, {1, 0, 0, 0, 0}});
    auto& tape = machine.objects[1].instructions;
    for (int i = 0; i < 4000; ++i) {
        tape.push_back({'A', 2 * i});
        tape.push_back({'a', 2 * i + 1});
    }
    tape.push_back({'B', 8000});
    const auto originalTape = tape;
    ASSERT_TRUE(solvers::detail::TrimLifeDeathStorage(machine, {0}));
    EXPECT_EQ(machine.objects[0].bodyData,
              (std::vector<int>{1, 0, 0, 0, 2, 1, 0, 2, 0, 0}));
    EXPECT_EQ(machine.objects[1].instructions.size(), originalTape.size());
    const auto before = io::BinaryWriter::WriteSolution(machine);
    EXPECT_FALSE(solvers::detail::TrimLifeDeathStorage(machine, {0}));
    EXPECT_EQ(io::BinaryWriter::WriteSolution(machine), before);
    // Even after B, malformed suffixes remain proof failures.
    machine.objects[1].instructions.push_back({'X', 8001});
    const auto invalid = io::BinaryWriter::WriteSolution(machine);
    EXPECT_FALSE(solvers::detail::TrimLifeDeathStorage(machine, {0}));
    EXPECT_EQ(io::BinaryWriter::WriteSolution(machine), invalid);
}

TEST(LifeDeathTest, StorageNonzeroOriginAndLocalOffsetsTrimHead) {
    io::SolutionData machine;
    machine.objects.push_back({"track", {10, -20}, {},
        {1, 4, 0, 0, 5, -3, 7, -2, 7, -1, 7, 0, 7, 1, 7, 9}});
    machine.objects.push_back({"arm1", {9, -13},
        {{'a', 0}, {'B', 1}}, {1, 0, 0, 0, 0}});
    auto expected = machine;
    expected.objects[0].bodyData = {1, 4, 0, 0, 2, -2, 7, -1, 7, 9};
    ASSERT_TRUE(solvers::detail::TrimLifeDeathStorage(machine, {0}));
    EXPECT_EQ(io::BinaryWriter::WriteSolution(machine),
              io::BinaryWriter::WriteSolution(expected));
}

TEST(LifeDeathTest, StorageReservedWorldCoordinateRejectedTransactionally) {
    // Selected and unselected tracks both participate in the global graph.
    for (const bool selected : {false, true}) {
        for (const bool offset : {false, true}) {
            SCOPED_TRACE(::testing::Message() << "selected=" << selected << " offset=" << offset);
            auto machine = ParkingFixture();
            machine.objects[1].bodyData = {1, 0, 0, 0, 0};
            machine.objects.push_back({"track",
                {INT32_MIN + (offset ? 1 : 0), INT32_MIN + (offset ? 2 : 0)}, {},
                {1, 0, 0, 0, 1, offset ? -1 : 0, offset ? -2 : 0, 0}});
            const auto before = io::BinaryWriter::WriteSolution(machine);
            EXPECT_FALSE(solvers::detail::TrimLifeDeathStorage(machine,
                selected ? std::vector<std::size_t>{0, 2} : std::vector<std::size_t>{0}));
            EXPECT_EQ(io::BinaryWriter::WriteSolution(machine), before);
        }
    }
    // INT32_MIN remains valid for a single coordinate (not the reserved pair).
    io::SolutionData machine;
    machine.objects.push_back({"track", {0, 0}, {},
        {1, 0, 0, 0, 3, INT32_MIN, 0, INT32_MIN + 1, 0, INT32_MIN + 2, 0, 0}});
    machine.objects.push_back({"arm1", {INT32_MIN + 1, 0},
        {{'A', 0}, {'B', 1}}, {1, 0, 0, 0, 0}});
    EXPECT_TRUE(solvers::detail::TrimLifeDeathStorage(machine, {0}));
}

TEST(LifeDeathTest, StorageFinalFilteredObjectIds) {
    io::SolutionData result;
    result.objects.push_back({"glyph-marker", {50, 50}, {}, {}});
    std::vector<std::size_t> selected;
    // Two output machines: local IDs are not final IDs; filtered placeholders
    // are deliberately not passed to the same append hook used in LifeDeath.
    for (const int shift : {10, 30}) {
        io::SolutionData local;
        local.objects.push_back({"glyph-marker", {}, {}, {}}); // filtered ID 0
        local.objects.push_back({"arm1", {shift + 1, 0},
            {{'A', 0}, {'B', 1}}, {1, 0, 0, 0, shift}});
        local.objects.push_back({"track", {shift, 0}, {},
            {1, 0, 0, 0, 4, 0, 0, 1, 0, 2, 0, 3, 0, 0}});
        for (std::size_t id = 1; id < local.objects.size(); ++id)
            solvers::detail::AppendLifeDeathObject(result, std::move(local.objects[id]),
                                                   id == 2, selected);
    }
    EXPECT_EQ(selected, (std::vector<std::size_t>{2, 4}));
    auto expected = result;
    for (const auto id : selected)
        expected.objects[id].bodyData = {1, 0, 0, 0, 2, 1, 0, 2, 0, 0};
    ASSERT_TRUE(solvers::detail::TrimLifeDeathStorage(result, selected));
    EXPECT_EQ(io::BinaryWriter::WriteSolution(result),
              io::BinaryWriter::WriteSolution(expected));
}

TEST(LifeDeathTest, StorageInvalidSelectedIdsRejectedTransactionally) {
    for (const auto& ids : {std::vector<std::size_t>{0, 2},
                            std::vector<std::size_t>{0, SIZE_MAX},
                            std::vector<std::size_t>{0, 0},
                            std::vector<std::size_t>{0, 1}}) {
        auto machine = ParkingFixture();
        machine.objects[1].bodyData = {1, 0, 0, 0, 0};
        const auto before = io::BinaryWriter::WriteSolution(machine);
        EXPECT_FALSE(solvers::detail::TrimLifeDeathStorage(machine, ids));
        EXPECT_EQ(io::BinaryWriter::WriteSolution(machine), before);
    }
}

TEST(LifeDeathTest, StorageStationaryAndMultipleUsers) {
    io::SolutionData machine;
    machine.objects.push_back({"track", {0, 0}, {},
        {1, 0, 0, 0, 7, 0, 0, 1, 0, 2, 0, 3, 0, 4, 0, 5, 0, 6, 0, 0}});
    machine.objects.push_back({"arm1", {1, 0},
        {{'A', 0}, {'B', 1}}, {1, 0, 0, 0, 0}});
    machine.objects.push_back({"piston", {4, 0}, {}, {2, 1, 0, 0, 1}});
    // Another user reaches the existing endpoint and executes a zero move.
    machine.objects.push_back({"arm1", {5, 0},
        {{'A', 0}, {'A', 1}, {'B', 2}}, {1, 0, 0, 0, 2}});
    auto expected = machine;
    ASSERT_TRUE(solvers::detail::TrimLifeDeathStorage(machine, {0}));
    EXPECT_EQ(machine.objects[0].bodyData[4], 6);
    EXPECT_EQ(machine.objects[0].bodyData[5], 1);
    expected.objects[0].bodyData = machine.objects[0].bodyData;
    EXPECT_EQ(io::BinaryWriter::WriteSolution(machine), io::BinaryWriter::WriteSolution(expected));
}

TEST(LifeDeathTest, StorageRejectsUnsafeInputsTransactionally) {
    using solvers::detail::TrimLifeDeathStorage;
    io::SolutionData parent;
    parent.objects.push_back({"track", {0, 0}, {},
        {1, 0, 0, 0, 5, 0, 0, 1, 0, 2, 0, 3, 0, 4, 0, 0}});
    parent.objects.push_back({"piston", {1, 0},
        {{'A', 0}, {'B', 1}, {'a', 2}}, {1, 0, 0, 0, 0}});
    for (int failure = 0; failure < 13; ++failure) {
        SCOPED_TRACE(failure);
        auto invalid = parent;
        auto& rail = invalid.objects[0];
        auto& arm = invalid.objects[1];
        if (failure == 0) arm.instructions.back().action = 'X';
        if (failure == 1) arm.instructions.back().action = 'O';
        if (failure == 2) arm.instructions.back().parameter = 1;
        if (failure == 3) arm.instructions.front().parameter = -1;
        if (failure == 4) arm.instructions.back().parameter = INT32_MAX;
        if (failure == 5) arm.instructions[1].action = 'g';
        if (failure == 6) rail.bodyData.pop_back();
        if (failure == 7) rail.bodyData[7] = 0;
        if (failure == 8) rail.position.q = INT32_MAX;
        if (failure == 9) rail.bodyData[4] = INT32_MAX;
        if (failure == 10) arm.position = {50, 50};
        if (failure == 11) arm.bodyData.clear();
        if (failure == 12) {
            invalid.objects.push_back({"track", {0, 20}, {},
                {1, 0, 0, 0, 7, 0, 0, 1, 0, 2, 0, 2, 1,
                 1, 1, 0, 1, -1, 2, 0}});
            invalid.objects.push_back({"arm1", {0, 20},
                {{'A', 0}, {'A', 1}, {'A', 2}, {'A', 3}, {'A', 4}, {'B', 5}},
                {1, 0, 0, 0, 1}});
        }
        const auto before = io::BinaryWriter::WriteSolution(invalid);
        EXPECT_FALSE(TrimLifeDeathStorage(invalid, failure == 12 ?
            std::vector<std::size_t>{0, 2} : std::vector<std::size_t>{0}));
        EXPECT_EQ(io::BinaryWriter::WriteSolution(invalid), before);
    }
}

TEST(LifeDeathTest, ParkingFiniteStationDemand) {
    for (const int demand : {6, 12, 24}) {
        auto machine = ParkingFixture();
        auto& instructions = machine.objects[1].instructions;
        instructions.clear();
        int cycle = 0;
        for (int stored = 1; stored <= demand; ++stored) {
            instructions.push_back({'G', cycle++});
            for (int step = 0; step < stored * 3; ++step)
                instructions.push_back({'a', cycle++});
            instructions.push_back({'g', cycle++});
            for (int step = 0; step < stored * 3; ++step)
                instructions.push_back({'A', cycle++});
        }
        instructions.push_back({'B', cycle});
        const auto tape = machine.objects[1].instructions;
        ASSERT_TRUE(solvers::detail::TrimLifeDeathParking(machine, 0, 1, demand));
        EXPECT_EQ(machine.objects[0].bodyData[4], demand * 3 + 4);
        ASSERT_EQ(machine.objects[1].instructions.size(), tape.size());
        for (std::size_t i = 0; i < tape.size(); ++i) {
            EXPECT_EQ(machine.objects[1].instructions[i].action, tape[i].action);
            EXPECT_EQ(machine.objects[1].instructions[i].parameter, tape[i].parameter);
        }
    }
    auto singleStation = ParkingFixture(22);
    EXPECT_FALSE(solvers::detail::TrimLifeDeathParking(singleStation, 0, 1, 6));
    EXPECT_EQ(singleStation.objects[0].bodyData[4], 22);

    auto multipleStations = ParkingFixture();
    auto secondStation = ParkingFixture();
    for (auto& part : secondStation.objects) {
        part.position.q = 10;
        multipleStations.objects.push_back(part);
    }
    ASSERT_TRUE(solvers::detail::TrimLifeDeathParking(multipleStations, 0, 1, 6));
    ASSERT_TRUE(solvers::detail::TrimLifeDeathParking(multipleStations, 2, 3, 12));
    EXPECT_EQ(multipleStations.objects[0].bodyData[4], 22);
    EXPECT_EQ(multipleStations.objects[2].bodyData[4], 40);
}
TEST(LifeDeathTest, ParkingConservativeFallback) {
    for (int kind = 0; kind < 5; ++kind) {
        auto machine = ParkingFixture();
        if (kind == 0) machine.objects.push_back(machine.objects[0]); // junction
        if (kind == 1) machine.objects.push_back(machine.objects[1]); // other arm
        if (kind == 2) machine.objects[1].instructions.push_back({'C', 4});
        if (kind == 3) machine.objects[1].instructions.insert(
            machine.objects[1].instructions.begin(), {'A', 0}); // leaves rail
        const auto original = machine.objects[0].bodyData;
        EXPECT_FALSE(solvers::detail::TrimLifeDeathParking(
            machine, 0, 1, kind == 4 ? INT64_MAX : 6));
        EXPECT_EQ(machine.objects[0].bodyData, original);
    }
}
TEST(LifeDeathTest, ParkingExtremeCoordinates) {
    constexpr auto low = std::numeric_limits<std::int32_t>::min();
    constexpr auto high = std::numeric_limits<std::int32_t>::max();
    for (const auto origin : {core::HexCoord{low, low + 129},
                              core::HexCoord{high, high}}) {
        auto machine = ParkingFixture();
        machine.objects[0].position = machine.objects[1].position = origin;
        EXPECT_TRUE(solvers::detail::TrimLifeDeathParking(machine, 0, 1, 6));
        EXPECT_EQ(machine.objects[0].bodyData[4], 22);
    }
    {
        auto machine = ParkingFixture();
        machine.objects[0].position.r = machine.objects[1].position.r = low;
        const auto original = io::BinaryWriter::WriteSolution(machine);
        EXPECT_FALSE(solvers::detail::TrimLifeDeathParking(machine, 0, 1, 6));
        EXPECT_EQ(io::BinaryWriter::WriteSolution(machine), original);
    }
    // Exercise both axes and both overflow directions on a different track.
    for (const bool qAxis : {false, true}) {
        for (const bool positive : {false, true}) {
            auto machine = ParkingFixture();
            io::SolutionObject other;
            other.typeName = "track";
            other.position = qAxis ? core::HexCoord{positive ? high : low, 0}
                                   : core::HexCoord{10, positive ? high : low};
            const int offset = positive ? 1 : -1;
            other.bodyData = {1, 0, 0, 0, 1, qAxis ? offset : 0,
                              qAxis ? 0 : offset, 0};
            machine.objects.push_back(other);
            const auto original = io::BinaryWriter::WriteSolution(machine);
            EXPECT_FALSE(solvers::detail::TrimLifeDeathParking(machine, 0, 1, 6));
            EXPECT_EQ(io::BinaryWriter::WriteSolution(machine), original);
        }
    }
    // Large, representable offsets must still detect a genuinely shared cell.
    for (const int extreme : {low, high}) {
        auto machine = ParkingFixture();
        machine.objects[0].position.q = machine.objects[1].position.q = -1;
        io::SolutionObject other;
        other.typeName = "track";
        other.position.q = extreme;
        other.bodyData = {1, 0, 0, 0, 1, extreme == low ? high : low, 0, 0};
        machine.objects.push_back(other);
        const auto original = io::BinaryWriter::WriteSolution(machine);
        EXPECT_FALSE(solvers::detail::TrimLifeDeathParking(machine, 0, 1, 6));
        EXPECT_EQ(io::BinaryWriter::WriteSolution(machine), original);
    }
}
TEST(LifeDeathTest, ParkingHaltCannotUseUnreachableReturn) {
    for (const auto& tape : {
             std::vector<io::ArmInstruction>{{'a', 0}, {'B', 1}, {'A', 2}},
             std::vector<io::ArmInstruction>{{'B', 0}, {'a', 1}, {'A', 2}},
             std::vector<io::ArmInstruction>{{'a', 0}, {'B', 1}},
             std::vector<io::ArmInstruction>{{'a', 0}},
             std::vector<io::ArmInstruction>{{'B', 0}, {'B', 1}},
             // Storage order alone cannot prove the actual execution order.
             std::vector<io::ArmInstruction>{{'a', 2}, {'A', 3}, {'B', 1}},
             std::vector<io::ArmInstruction>{{'a', 0}, {'A', 0}, {'B', 1}},
             std::vector<io::ArmInstruction>{{'a', -1}, {'A', 0}, {'B', 1}}}) {
        auto machine = ParkingFixture();
        machine.objects[1].instructions = tape;
        const auto original = io::BinaryWriter::WriteSolution(machine);
        EXPECT_FALSE(solvers::detail::TrimLifeDeathParking(machine, 0, 1, 6));
        EXPECT_EQ(io::BinaryWriter::WriteSolution(machine), original);
    }
    for (const bool halt : {false, true}) {
        auto machine = ParkingFixture();
        machine.objects[1].instructions = {{'G', 10}, {'a', 20}, {'A', 30}};
        if (halt) machine.objects[1].instructions.push_back({'B', 100});
        const auto tape = machine.objects[1].instructions;
        ASSERT_TRUE(solvers::detail::TrimLifeDeathParking(machine, 0, 1, 6));
        ASSERT_EQ(machine.objects[1].instructions.size(), tape.size());
        for (std::size_t i = 0; i < tape.size(); ++i) {
            EXPECT_EQ(machine.objects[1].instructions[i].action, tape[i].action);
            EXPECT_EQ(machine.objects[1].instructions[i].parameter, tape[i].parameter);
        }
    }
}
TEST(LifeDeathTest, ParkingWalkMustStayWithinShortenedRail) {
    constexpr int shortenedCells = 22;
    for (const int distance : {shortenedCells - 1, shortenedCells}) {
        auto machine = ParkingFixture();
        auto& tape = machine.objects[1].instructions;
        tape.clear();
        int cycle = 0;
        for (int step = 0; step < distance; ++step) tape.push_back({'a', cycle++});
        for (int step = 0; step < distance; ++step) tape.push_back({'A', cycle++});
        tape.push_back({'B', cycle});
        const auto original = io::BinaryWriter::WriteSolution(machine);
        EXPECT_EQ(solvers::detail::TrimLifeDeathParking(machine, 0, 1, 6),
                  distance < shortenedCells);
        if (distance >= shortenedCells)
            EXPECT_EQ(io::BinaryWriter::WriteSolution(machine), original);
    }
}
TEST(LifeDeathTest, ParkingAdjacentIndependentTracksDoNotLink) {
    // Parallel adjacent rails and end-to-end adjacent rails remain independent.
    for (const auto origin : {core::HexCoord{1, 0}, core::HexCoord{-1, 0},
                              core::HexCoord{0, -130}, core::HexCoord{0, 130}}) {
        auto machine = ParkingFixture();
        auto other = ParkingFixture();
        for (auto& part : other.objects) {
            part.position = origin;
            machine.objects.push_back(part);
        }
        const auto otherTrack = machine.objects[2].bodyData;
        ASSERT_TRUE(solvers::detail::TrimLifeDeathParking(machine, 0, 1, 6));
        EXPECT_EQ(machine.objects[0].bodyData[4], 22);
        EXPECT_EQ(machine.objects[2].bodyData, otherTrack);
    }
}
TEST(LifeDeathTest, ParkingRejectsOtherRailUsers) {
    for (const auto* type : {"arm1", "arm2", "arm3", "arm6", "piston",
                             "baron", "ravari", "arm-unknown", "unknown-device"}) {
        SCOPED_TRACE(type);
        auto machine = ParkingFixture();
        io::SolutionObject other;
        other.typeName = type;
        other.position = {0, -100}; // deleted part of the original track
        if (other.typeName == "unknown-device") other.instructions = {{'a', 0}};
        machine.objects.push_back(other);
        const auto original = io::BinaryWriter::WriteSolution(machine);
        EXPECT_FALSE(solvers::detail::TrimLifeDeathParking(machine, 0, 1, 6));
        EXPECT_EQ(io::BinaryWriter::WriteSolution(machine), original);
    }
}
TEST(LifeDeathTest, CompactRelayExperimentalAdmission) {
    io::PuzzleData puzzle;
    puzzle.version = 3;
    puzzle.name = "compact-relay-admission";
    puzzle.permissionBits = (1ULL << 32) - 1;
    puzzle.outputMultiplier = 1;
    puzzle.inputs = {
        {{{core::AtomType::Air, {0, 0}}, {core::AtomType::Air, {1, 0}}}, {{1, {0, 0}, {1, 0}}}}};
    puzzle.outputs = {
        {{{core::AtomType::Salt, {0, 0}}, {core::AtomType::Salt, {1, 0}}}, {{1, {0, 0}, {1, 0}}}}};
    EXPECT_THROW(solvers::CompileLifeDeath(puzzle), std::invalid_argument);
    EXPECT_NO_THROW(solvers::CompileCompactChemistryRelay(puzzle, false));
    EXPECT_NO_THROW(solvers::CompileCompactChemistryRelay(puzzle));
    puzzle.outputs.push_back(puzzle.outputs.front());
    EXPECT_THROW(solvers::CompileCompactChemistryRelay(puzzle), std::invalid_argument);
    puzzle.outputs.resize(1);
    puzzle.inputs.front().atoms.resize(1);
    puzzle.inputs.front().bonds.clear();
    EXPECT_THROW(solvers::CompileCompactChemistryRelay(puzzle), std::invalid_argument);
}
TEST(LifeDeathTest, UnsupportedInputsAndPermissions) {
    const auto root = opusoptima::test::RepoRoot(__FILE__);
    const auto original =
        io::BinaryParser::ParsePuzzleFile(root / "asset/puzzle/campaign/ch3/P030b.puzzle");
    for (const int bit : {0, 2, 3, 8, 12, 16, 22, 23}) {
        auto puzzle = original;
        puzzle.permissionBits &= ~(1ULL << bit);
        EXPECT_THROW(solvers::CompileLifeDeath(puzzle), std::invalid_argument) << bit;
    }
    auto puzzle = original;
    puzzle.outputs.clear();
    EXPECT_THROW(solvers::CompileLifeDeath(puzzle), std::invalid_argument);
    puzzle = original;
    puzzle.outputMultiplier = 3;
    EXPECT_THROW(solvers::CompileLifeDeath(puzzle), std::invalid_argument);
    puzzle = original;
    puzzle.inputs.front().atoms.push_back(puzzle.inputs.front().atoms.front());
    EXPECT_THROW(solvers::CompileLifeDeath(puzzle), std::invalid_argument);
    puzzle = original;
    puzzle.inputs.front().atoms.front().type = core::AtomType::Vitae;
    EXPECT_NO_THROW(solvers::CompileLifeDeath(puzzle));
    puzzle = original;
    puzzle.outputs.front().atoms.front().type = core::AtomType::Repeat;
    EXPECT_THROW(solvers::CompileLifeDeath(puzzle), std::invalid_argument);
    puzzle = original;
    for (auto& atom : puzzle.outputs.front().atoms)
        atom.type = core::AtomType::Salt;
    EXPECT_THROW(solvers::CompileLifeDeath(puzzle), std::invalid_argument);
}
TEST(LifeDeathTest, CombinedSaltChemistryPermissions) {
    const auto root = opusoptima::test::RepoRoot(__FILE__);
    const auto original =
        io::BinaryParser::ParsePuzzleFile(root / "asset/puzzle/journal/journal4/P072.puzzle");
    for (int bit : {13, 18, 28}) {
        auto puzzle = original;
        puzzle.permissionBits &= ~(1ULL << bit);
        EXPECT_THROW(solvers::CompileLifeDeath(puzzle), std::invalid_argument) << bit;
    }
}
#ifdef HAS_LIBVERIFY
TEST(LifeDeathVerifierTest, ParkingFiniteNativeTrackOnlyControl) {
    const auto root = opusoptima::test::RepoRoot(__FILE__);
    for (const auto& [file, shortenedCells, originalCells] : {
        std::tuple{"asset/puzzle/workshop-versions/w2788066865.puzzle", 22, 130},
        std::tuple{"asset/puzzle/tourney-2021/OM2021_W2.puzzle", 40, 94}}) {
        SCOPED_TRACE(file);
        const auto puzzlePath = root / file;
        const auto shortened = solvers::CompileLifeDeath(io::BinaryParser::ParsePuzzleFile(puzzlePath));
        auto control = shortened;
        int restored = 0;
        for (auto& part : control.objects) {
            if (part.typeName != "track" || part.bodyData.at(4) != shortenedCells) continue;
            bool vertical = true;
            for (int cell = 0; cell < shortenedCells; ++cell)
                vertical = vertical && part.bodyData.at(5 + 2 * cell) == 0 &&
                    part.bodyData.at(6 + 2 * cell) == cell - shortenedCells + 1;
            if (!vertical) continue;
            auto data = std::vector<std::int32_t>(part.bodyData.begin(), part.bodyData.begin() + 5);
            data[4] = originalCells;
            for (int offset = 1 - originalCells; offset <= 0; ++offset)
                data.insert(data.end(), {0, offset});
            data.push_back(part.bodyData.back());
            part.bodyData = std::move(data);
            ++restored;
        }
        ASSERT_GT(restored, 0);
        const auto solutionPath = root / "build/parking-native-track-only-control.solution";
        io::BinaryWriter::WriteSolutionFile(solutionPath, shortened);
        const auto actual = io::VerifySolutionFiles(puzzlePath, solutionPath, 150000, 10000);
        io::BinaryWriter::WriteSolutionFile(solutionPath, control);
        const auto original = io::VerifySolutionFiles(puzzlePath, solutionPath, 150000, 10000);
        EXPECT_TRUE(actual.Passed()) << actual.detail;
        EXPECT_TRUE(original.Passed()) << original.detail;
        EXPECT_EQ(actual.metrics.cycles, original.metrics.cycles);
        EXPECT_EQ(actual.metrics.instructionCount, original.metrics.instructionCount);
        EXPECT_LT(actual.metrics.cost, original.metrics.cost);
        EXPECT_LT(actual.metrics.area, original.metrics.area);
        std::filesystem::remove(solutionPath);
    }
}
TEST(LifeDeathVerifierTest, TwoRealStaggerMeasuredProbe) {
    const auto root = opusoptima::test::RepoRoot(__FILE__);
    const auto tracks = [](const auto& s) {
        int n = 0;
        for (const auto& o : s.objects) if (o.typeName == "track") n += o.bodyData.at(4);
        return n;
    };
    for (const auto* file : {
        "asset/puzzle/workshop-versions/w2788066865.puzzle",
        "asset/puzzle/tourney-2021/OM2021_W2.puzzle",
        "asset/puzzle/workshop-versions/w2450511665.puzzle",
        "asset/puzzle/tourney-2020/OM2020_W6_ActivePolymerase.puzzle",
        "asset/puzzle/journal/journal7/P098.puzzle",
        "asset/puzzle/journal/journal8/P102.puzzle",
        "asset/puzzle/tourney-2021/OM2021_W7.puzzle",
        "asset/puzzle/campaign/ch5/P043.puzzle",
        "asset/puzzle/journal/journal4/P072.puzzle"}) {
        const auto puzzlePath = root / file;
        const auto puzzle = io::BinaryParser::ParsePuzzleFile(puzzlePath);
        const auto baseline = solvers::CompileLifeDeath(puzzle);
        const auto bytes = io::BinaryWriter::WriteSolution(baseline);
        {
            solvers::FactoryStaggerScope outer(true);
            {
                solvers::FactoryStaggerScope inner(false);
                EXPECT_EQ(bytes, io::BinaryWriter::WriteSolution(solvers::CompileLifeDeath(puzzle)));
            }
            try {
                const auto raw = solvers::CompileLifeDeath(puzzle);
                const auto path = root / "build/stagger-probe.solution";
                io::BinaryWriter::WriteSolutionFile(path, raw);
                const auto report = io::VerifySolutionFiles(puzzlePath, path, 150000, 10000);
                std::cout << "STAGGER " << file << " tracks=" << tracks(baseline) << "->"
                          << tracks(raw) << " native=" << report.Passed() << " "
                          << report.detail << " cycle=" << report.errorCycle << " at="
                          << report.errorU << ',' << report.errorV << '\n';
                if (tracks(raw) < tracks(baseline)) {
                    EXPECT_NO_THROW((void)solvers::CompileStaggeredLifeDeath(puzzle));
                } else {
                    EXPECT_THROW((void)solvers::CompileStaggeredLifeDeath(puzzle), std::invalid_argument);
                }
                std::filesystem::remove(path);
            } catch (const std::invalid_argument& e) {
                std::cout << "STAGGER " << file << " rejected=" << e.what() << '\n';
            }
        }
        EXPECT_EQ(bytes, io::BinaryWriter::WriteSolution(solvers::CompileLifeDeath(puzzle)));
    }
}
TEST(LifeDeathVerifierTest, TwoRealStaggerFiniteSaltPair) {
    const auto root = opusoptima::test::RepoRoot(__FILE__);
    io::PuzzleData puzzle;
    puzzle.version = 3;
    puzzle.name = "stagger-finite-salt-pair";
    puzzle.permissionBits = (1ULL << 32) - 1;
    puzzle.outputMultiplier = 1;
    puzzle.inputs = {{{{core::AtomType::Salt, {0,0}}, {core::AtomType::Salt, {1,0}}},
                      {{1, {0,0}, {1,0}}}}};
    puzzle.outputs = {{{{core::AtomType::Vitae, {0,0}}}, {}}};
    const auto base = solvers::CompileLifeDeath(puzzle);
    const auto raw = [&] {
        solvers::FactoryStaggerScope stagger(true);
        return solvers::CompileLifeDeath(puzzle);
    }();
    auto tracks = [](const auto& s) {
        int n = 0;
        for (const auto& o : s.objects) if (o.typeName == "track") n += o.bodyData.at(4);
        return n;
    };
    std::cout << "STAGGER salt-pair tracks=" << tracks(base) << "->" << tracks(raw) << '\n';
    EXPECT_LT(tracks(raw), tracks(base));
    EXPECT_NO_THROW(solvers::CompileStaggeredLifeDeath(puzzle));
    {
        solvers::FactoryClearanceScope clearance(0);
        const auto tight = solvers::CompileLifeDeath(puzzle);
        solvers::FactoryStaggerScope stagger(true);
        const auto shifted = solvers::CompileLifeDeath(puzzle);
        std::cout << "STAGGER salt-pair clearance0 tracks=" << tracks(tight)
                  << "->" << tracks(shifted) << '\n';
        EXPECT_GE(tracks(shifted), tracks(tight));
        EXPECT_THROW(solvers::CompileStaggeredLifeDeath(puzzle), std::invalid_argument);
    }
    const auto pp = root / "build/stagger-salt-pair.puzzle";
    const auto sp = root / "build/stagger-salt-pair.solution";
    {
        std::ofstream out(pp, std::ios::binary);
        const auto word = [&](std::uint64_t v, int bytes) {
            for (int i=0; i<bytes; ++i) out.put(static_cast<char>(v >> (8*i)));
        };
        word(3,4); word(puzzle.name.size(),1);
        out.write(puzzle.name.data(), puzzle.name.size());
        word(0,8); word(puzzle.permissionBits,8);
        for (const auto* list : {&puzzle.inputs, &puzzle.outputs}) {
            word(list->size(),4);
            for (const auto& m : *list) {
                word(m.atoms.size(),4);
                for (const auto& a : m.atoms) {
                    word(static_cast<std::uint8_t>(a.type),1);
                    word(a.position.q,1); word(a.position.r,1);
                }
                word(m.bonds.size(),4);
                for (const auto& b : m.bonds) {
                    word(b.bits,1); word(b.from.q,1); word(b.from.r,1);
                    word(b.to.q,1); word(b.to.r,1);
                }
            }
        }
        word(puzzle.outputMultiplier,4); word(0,1);
    }
    for (const auto* s : {&base, &raw}) {
        io::BinaryWriter::WriteSolutionFile(sp, *s);
        const auto report = io::VerifySolutionFiles(pp, sp, 150000, 10000);
        EXPECT_TRUE(report.Passed()) << report.detail;
        std::cout << "STAGGER salt-pair native=" << report.Passed() << " " << report.detail
                  << " cycle=" << report.errorCycle << " at=" << report.errorU << ',' << report.errorV << '\n';
    }
}
TEST(LifeDeathVerifierTest, SelectiveCardinalUnification) {
    const auto root = opusoptima::test::RepoRoot(__FILE__);
    for (const auto* file : {"asset/puzzle/tourney-2020/OM2020_W4_VolatilityAndTranquility.puzzle",
                             "asset/puzzle/workshop-versions/w2501727977.puzzle"}) {
        SCOPED_TRACE(file);
        const auto puzzlePath = root / file;
        auto solution = solvers::CompileLifeDeath(io::BinaryParser::ParsePuzzleFile(puzzlePath));
        const auto path =
            root / "build" / (puzzlePath.stem().string() + "-selective-unification-test.solution");
        struct Cleanup {
            std::filesystem::path path;
            ~Cleanup() {
                std::error_code ec;
                std::filesystem::remove(path, ec);
            }
        } cleanup{path};
        EXPECT_LT(solution.objects.size(), 10000U);
        for (const auto& part : solution.objects) {
            if (!part.instructions.empty()) {
                const auto [first, last] = std::minmax_element(
                    part.instructions.begin(), part.instructions.end(),
                    [](const auto& a, const auto& b) { return a.parameter < b.parameter; });
                ASSERT_LE(last->parameter - first->parameter, 99999);
            }
            ASSERT_LT(part.instructions.size(), 100000U)
                << part.typeName << " at " << part.position.q << ',' << part.position.r;
            if (part.typeName == "track") {
                ASSERT_LE(part.bodyData[4], 9999);
            }
            for (const auto& ins : part.instructions)
                ASSERT_GE(ins.parameter, 0) << part.typeName;
        }
        for (int pass = 0; pass < 2; ++pass) {
            io::BinaryWriter::WriteSolutionFile(path, solution);
            const auto report = io::VerifySolutionFiles(puzzlePath, path, 150000, 10000);
            ASSERT_TRUE(report.Passed()) << report.detail << " cycle=" << report.errorCycle
                                         << " at " << report.errorU << ',' << report.errorV;
            solution.metrics = report.metrics;
        }
    }
}
TEST(LifeDeathVerifierTest, DispersionWithoutDisposal) {
    const auto root = opusoptima::test::RepoRoot(__FILE__);
    for (const auto* file : {"asset/puzzle/tourney-2019/week7.puzzle",
                             "asset/puzzle/workshop-versions/w1698787731.puzzle"}) {
        SCOPED_TRACE(file);
        const auto puzzlePath = root / file;
        const auto puzzle = io::BinaryParser::ParsePuzzleFile(puzzlePath);
        ASSERT_FALSE(puzzle.permissionBits & (1ULL << 17));
        auto solution = solvers::CompileLifeDeath(puzzle);
        const auto path =
            root / "build" / (puzzlePath.stem().string() + "-dispersion-storage-test.solution");
        struct Cleanup {
            std::filesystem::path path;
            ~Cleanup() {
                std::error_code ec;
                std::filesystem::remove(path, ec);
            }
        } cleanup{path};
        for (int pass = 0; pass < 2; ++pass) {
            io::BinaryWriter::WriteSolutionFile(path, solution);
            const auto report = io::VerifySolutionFiles(puzzlePath, path, 150000, 10000);
            ASSERT_TRUE(report.Passed()) << report.detail << " cycle=" << report.errorCycle
                                         << " at " << report.errorU << ',' << report.errorV;
            solution.metrics = report.metrics;
        }
    }
}
TEST(LifeDeathVerifierTest, MolecularMetalChemistry) {
    const auto root = opusoptima::test::RepoRoot(__FILE__);
    const auto puzzlePath = root / "asset/puzzle/workshop-versions/w2788067677.puzzle";
    auto solution = solvers::CompileLifeDeath(io::BinaryParser::ParsePuzzleFile(puzzlePath));
    const auto path = root / "build" / "fulmination-metal-feed-test.solution";
    struct Cleanup {
        std::filesystem::path path;
        ~Cleanup() {
            std::error_code ec;
            std::filesystem::remove(path, ec);
        }
    } cleanup{path};
    for (int pass = 0; pass < 2; ++pass) {
        io::BinaryWriter::WriteSolutionFile(path, solution);
        const auto report = io::VerifySolutionFiles(puzzlePath, path, 150000, 10000);
        ASSERT_TRUE(report.Passed()) << report.detail << " cycle=" << report.errorCycle << " at "
                                     << report.errorU << ',' << report.errorV;
        solution.metrics = report.metrics;
    }
}
TEST(LifeDeathVerifierTest, MolecularSaltChemistry) {
    const auto root = opusoptima::test::RepoRoot(__FILE__);
    for (const auto* file :
         {"asset/puzzle/workshop-versions/w2788066865.puzzle",
          "asset/puzzle/tourney-2021/OM2021_W2.puzzle",
          "asset/puzzle/workshop-versions/w2450511665.puzzle",
          "asset/puzzle/edge-cases/OM2025_6_Bulk_Transmutation.puzzle",
          "asset/puzzle/tourney-2020/OM2020_W6_ActivePolymerase.puzzle",
          "asset/puzzle/workshop-versions/w2501728219.puzzle",
          "asset/puzzle/workshop-versions/w2946682999.puzzle",
          "asset/puzzle/tourney-2019/week8.puzzle",
          "asset/puzzle/workshop-versions/w1698788220.puzzle",
          "asset/puzzle/journal/journal9-p/P108.puzzle",
          "asset/puzzle/journal/journal7/P098.puzzle", "asset/puzzle/journal/journal8/P102.puzzle",
          "asset/puzzle/tourney-2021/OM2021_W7.puzzle", "asset/puzzle/campaign/ch5/P043.puzzle"}) {
        SCOPED_TRACE(file);
        const auto puzzlePath = root / file;
        auto solution = solvers::CompileLifeDeath(io::BinaryParser::ParsePuzzleFile(puzzlePath));
        const auto path =
            root / "build" / (puzzlePath.stem().string() + "-molecular-life-test.solution");
        struct Cleanup {
            std::filesystem::path path;
            ~Cleanup() {
                std::error_code ec;
                std::filesystem::remove(path, ec);
            }
        } cleanup{path};
        for (int pass = 0; pass < 2; ++pass) {
            io::BinaryWriter::WriteSolutionFile(path, solution);
            const auto report = io::VerifySolutionFiles(puzzlePath, path, 150000, 10000);
            ASSERT_TRUE(report.Passed()) << report.detail << " cycle=" << report.errorCycle
                                         << " at " << report.errorU << ',' << report.errorV;
            solution.metrics = report.metrics;
        }
    }
}
TEST(LifeDeathVerifierTest, CombinedSaltChemistryOriginalProducts) {
    const auto root = opusoptima::test::RepoRoot(__FILE__);
    const auto directory = std::filesystem::current_path();
    std::filesystem::create_directories(directory);
    for (const auto* file :
         {"asset/puzzle/journal/journal4/P072.puzzle", "asset/puzzle/tourney-2019/week6.puzzle",
          "asset/puzzle/tourney-2019/week4.puzzle"}) {
        SCOPED_TRACE(file);
        const auto path = root / file;
        const auto puzzle = io::BinaryParser::ParsePuzzleFile(path);
        auto solution = solvers::CompileLifeDeath(puzzle);
        const auto output = directory / (path.stem().string() + "-combined-test.solution");
        struct Cleanup {
            std::filesystem::path path;
            ~Cleanup() {
                std::error_code ec;
                std::filesystem::remove(path, ec);
            }
        } cleanup{output};
        for (int pass = 0; pass < 2; ++pass) {
            io::BinaryWriter::WriteSolutionFile(output, solution);
            const auto report = io::VerifySolutionFiles(path, output, 150000, 10000);
            ASSERT_TRUE(report.Passed()) << report.detail << " cycle=" << report.errorCycle
                                         << " at " << report.errorU << ',' << report.errorV;
            solution.metrics = report.metrics;
        }
        RecordProperty(path.stem().string() + "_cycles", solution.metrics.cycles);
    }
}
TEST(LifeDeathVerifierTest, ExistingLocalProducts) {
    const auto root = opusoptima::test::RepoRoot(__FILE__);
    for (const auto* file :
         {"asset/puzzle/campaign/ch3/P030b.puzzle", "asset/puzzle/campaign/ch4/P032.puzzle",
          "asset/puzzle/journal/journal5/P086.puzzle",
          "asset/puzzle/workshop-versions/w2946693353.puzzle"}) {
        SCOPED_TRACE(file);
        const auto puzzlePath = root / file;
        auto puzzle = io::BinaryParser::ParsePuzzleFile(puzzlePath);
        auto solution = solvers::CompileLifeDeath(puzzle);
        const auto solutionPath =
            root / "build" / (puzzlePath.stem().string() + "-lifedeath.solution");
        io::BinaryWriter::WriteSolutionFile(solutionPath, solution);
        auto report = io::VerifySolutionFiles(puzzlePath, solutionPath, 150000, 10000);
        ASSERT_TRUE(report.Passed()) << report.detail << " cycle=" << report.errorCycle << " at "
                                     << report.errorU << "," << report.errorV;
        RecordProperty(puzzlePath.stem().string() + "_cycles", report.metrics.cycles);
        RecordProperty(puzzlePath.stem().string() + "_cost", report.metrics.cost);
        RecordProperty(puzzlePath.stem().string() + "_area", report.metrics.area);
        solution.metrics = report.metrics;
        io::BinaryWriter::WriteSolutionFile(solutionPath, solution);
        EXPECT_TRUE(io::VerifySolutionFiles(puzzlePath, solutionPath, 150000, 10000).Passed());
    }
}
#endif
} // namespace
