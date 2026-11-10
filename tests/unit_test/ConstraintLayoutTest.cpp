#include "ConstraintLayout.hpp"

#include <gtest/gtest.h>

#include <limits>

namespace {
using namespace opusoptima;
using namespace opusoptima::solvers;
ConstraintModule Point(std::vector<core::HexCoord> translations) {
    ConstraintModule module;
    module.footprint = {{0, 0}};
    module.domain = MakeConstraintPlacementDomain(translations, {0});
    return module;
}
} // namespace

TEST(ConstraintLayout, BacktracksWhenGreedyPlacementBlocksLaterModule) {
    ConstraintLayoutProblem problem;
    problem.modules = {Point({{0, 0}, {1, 0}}), Point({{0, 0}})};
    const auto result = SearchConstraintLayout(problem);
    ASSERT_EQ(result.status, ConstraintLayoutStatus::Found);
    ASSERT_TRUE(result.layout);
    EXPECT_EQ((*result.layout)[0].domainIndex, 1U);
    EXPECT_EQ((*result.layout)[1].footprint, (std::vector<core::HexCoord>{{0, 0}}));
    EXPECT_EQ(result.nodes, 4U);
    EXPECT_EQ(result.candidates, 1U);
    const auto again = SearchConstraintLayout(problem);
    EXPECT_EQ(again.nodes, result.nodes);
    EXPECT_EQ((*again.layout)[0].placement, (*result.layout)[0].placement);
}

TEST(ConstraintLayout, DistinguishesNodeExhaustionAndUnsatisfiableDomains) {
    ConstraintLayoutProblem problem;
    problem.modules = {Point({{0, 0}, {1, 0}}), Point({{0, 0}})};
    auto result = SearchConstraintLayout(problem, {.maxNodes = 3});
    EXPECT_EQ(result.status, ConstraintLayoutStatus::Exhausted);
    EXPECT_EQ(result.failure.code, ConstraintLayoutFailureCode::NodeLimit);
    EXPECT_EQ(result.nodes, 3U);
    EXPECT_FALSE(result.layout);
    EXPECT_EQ(SearchConstraintLayout(problem, {.maxNodes = 4}).status,
              ConstraintLayoutStatus::Found);
    problem.modules[0].domain.resize(1);
    result = SearchConstraintLayout(problem);
    EXPECT_EQ(result.status, ConstraintLayoutStatus::Unsatisfiable);
    EXPECT_EQ(result.failure.code, ConstraintLayoutFailureCode::Overlap);
    EXPECT_EQ(result.failure.module, 1U);
    EXPECT_EQ(result.failure.otherModule, 0U);
    problem.modules[1].domain.clear();
    EXPECT_EQ(SearchConstraintLayout(problem).status, ConstraintLayoutStatus::Unsatisfiable);
    EXPECT_EQ(SearchConstraintLayout(problem, {.maxNodes = 0}).status,
              ConstraintLayoutStatus::Exhausted);
}

TEST(ConstraintLayout, UsesProductionChambersAndIntersectsExplicitAllowedCells) {
    io::PuzzleData puzzle;
    puzzle.productionInfo.emplace();
    puzzle.productionInfo->chambers = {{{10, -3}, "SmallWide"}};
    ConstraintLayoutProblem problem;
    problem.chambers = PlanProductionChambers(puzzle);
    auto module = Point({{8, -3}, {10, -3}, {11, -3}, {12, -3}});
    module.chamber = 0;
    module.allowedCells = std::vector<core::HexCoord>{{10, -3}, {12, -3}};
    problem.blockedCells = {{10, -3}};
    problem.modules = {module};
    auto result = SearchConstraintLayout(problem);
    ASSERT_EQ(result.status, ConstraintLayoutStatus::Found);
    EXPECT_EQ((*result.layout)[0].placement.translation, (core::HexCoord{12, -3}));
    for (auto cell : (*result.layout)[0].footprint)
        EXPECT_TRUE(ContainsProductionCell(problem.chambers[0], cell));
    problem.modules[0].allowedCells = std::vector<core::HexCoord>{};
    EXPECT_EQ(SearchConstraintLayout(problem).status, ConstraintLayoutStatus::Unsatisfiable);
    problem.modules[0].allowedCells.reset();
    problem.modules[0].domain.resize(1);
    result = SearchConstraintLayout(problem);
    EXPECT_EQ(result.failure.code, ConstraintLayoutFailureCode::OutsideChamber);
}

TEST(ConstraintLayout, RotatesNontrivialFootprintsAndPortsTogether) {
    ConstraintLayoutProblem problem;
    ConstraintModule module;
    module.footprint = {{0, 0}, {1, 0}, {1, 1}};
    module.ports = {{{2, 0}, 0}};
    module.domain = MakeConstraintPlacementDomain({{10, 2}}, {0, 1});
    module.allowedCells = std::vector<core::HexCoord>{{10, 2}, {10, 3}, {9, 4}};
    problem.modules.push_back(module);
    auto destination = Point({{10, 5}});
    destination.ports = {{{0, -1}, 2}};
    problem.modules.push_back(destination);
    problem.handoffs = {{0, 0, 1, 0, {{0, 0}}, true}};
    const auto result = SearchConstraintLayout(problem);
    ASSERT_EQ(result.status, ConstraintLayoutStatus::Found);
    const auto& placed = (*result.layout)[0];
    EXPECT_EQ(placed.domainIndex, 1U);
    EXPECT_EQ(placed.footprint, *module.allowedCells);
    EXPECT_EQ(placed.ports[0].cell, (core::HexCoord{10, 4}));
    EXPECT_EQ(placed.ports[0].direction, 5);
}

TEST(ConstraintLayout, EnforcesExplicitHandoffOffsetsFacingAndReverseModuleOrder) {
    ConstraintLayoutProblem problem;
    auto a = Point({{0, 0}});
    a.ports = {{{0, 0}, 0}};
    auto b = Point({{0, 1}, {1, 0}});
    b.ports = {{{0, 0}, 3}};
    problem.modules = {a, b};
    problem.handoffs = {{1, 0, 0, 0, {{-1, 0}}, true}};
    auto result = SearchConstraintLayout(problem);
    ASSERT_EQ(result.status, ConstraintLayoutStatus::Found);
    EXPECT_EQ((*result.layout)[1].domainIndex, 1U);
    problem.modules[1].ports[0].direction = 2;
    result = SearchConstraintLayout(problem);
    EXPECT_EQ(result.status, ConstraintLayoutStatus::Unsatisfiable);
    EXPECT_EQ(result.failure.code, ConstraintLayoutFailureCode::HandoffMismatch);
    EXPECT_EQ(result.failure.handoff, 0U);
    problem.handoffs[0].requireOppositeFacing = false;
    EXPECT_EQ(SearchConstraintLayout(problem).status, ConstraintLayoutStatus::Found);
    problem.handoffs = {{0, 0, 0, 0, {{0, 0}}, false}};
    EXPECT_EQ(SearchConstraintLayout(problem).status, ConstraintLayoutStatus::Found);
}

TEST(ConstraintLayout, CallbackRejectsCandidatesAndReceivesDeterministicAlternatives) {
    ConstraintLayoutProblem problem;
    problem.modules = {Point({{0, 0}, {1, 0}, {2, 0}})};
    std::vector<int> visited;
    const auto acceptThird = [&](const ConstraintLayout& layout) {
        visited.push_back(layout[0].placement.translation.q);
        return visited.size() == 3;
    };
    auto result = SearchConstraintLayout(problem, {}, acceptThird);
    ASSERT_EQ(result.status, ConstraintLayoutStatus::Found);
    EXPECT_EQ(visited, (std::vector<int>{0, 1, 2}));
    EXPECT_EQ(result.rejectedCandidates, 2U);
    EXPECT_EQ(result.candidates, 3U);
    auto reject = [](const ConstraintLayout&) { return false; };
    result = SearchConstraintLayout(problem, {.maxNodes = 100, .maxCandidates = 2}, reject);
    EXPECT_EQ(result.status, ConstraintLayoutStatus::Exhausted);
    EXPECT_EQ(result.failure.code, ConstraintLayoutFailureCode::CandidateLimit);
    EXPECT_EQ(result.candidates, 2U);
    result = SearchConstraintLayout(problem, {.maxNodes = 100, .maxCandidates = 3}, reject);
    EXPECT_EQ(result.status, ConstraintLayoutStatus::Unsatisfiable);
    EXPECT_EQ(result.rejectedCandidates, 3U);
    EXPECT_EQ(result.failure.code, ConstraintLayoutFailureCode::CandidateRejected);
    EXPECT_EQ(SearchConstraintLayout(problem, {.maxNodes = 100, .maxCandidates = 0}).nodes, 0U);
}

TEST(ConstraintLayout, EmptyProblemHasOneEmptyAssignment) {
    auto result = SearchConstraintLayout({}, {.maxNodes = 0});
    ASSERT_EQ(result.status, ConstraintLayoutStatus::Found);
    ASSERT_TRUE(result.layout);
    EXPECT_TRUE(result.layout->empty());
    EXPECT_EQ(result.candidates, 1U);
    EXPECT_EQ(SearchConstraintLayout({}, {}, [](const auto&) { return false; }).status,
              ConstraintLayoutStatus::Unsatisfiable);
    EXPECT_EQ(SearchConstraintLayout({}, {.maxNodes = 0, .maxCandidates = 0}).status,
              ConstraintLayoutStatus::Exhausted);
}

TEST(ConstraintLayout, ChecksEveryFootprintCellAndAllSixRotations) {
    for (int rotation = 0; rotation < 6; ++rotation) {
        ConstraintLayoutProblem problem;
        auto module = Point({{4, -2}});
        module.footprint = {{0, 0}, {2, -1}, {-1, 2}};
        module.domain[0].rotation = rotation;
        std::vector<core::HexCoord> expected;
        for (auto cell : module.footprint) {
            for (int i = 0; i < rotation; ++i)
                cell = core::RotateClockwise(cell);
            expected.push_back(core::Add(cell, {4, -2}));
        }
        module.allowedCells = expected;
        problem.modules = {module};
        auto result = SearchConstraintLayout(problem);
        ASSERT_EQ(result.status, ConstraintLayoutStatus::Found);
        EXPECT_EQ((*result.layout)[0].footprint, expected);
        problem.blockedCells = {expected.back()};
        result = SearchConstraintLayout(problem);
        EXPECT_EQ(result.status, ConstraintLayoutStatus::Unsatisfiable);
        EXPECT_EQ(result.failure.code, ConstraintLayoutFailureCode::BlockedCell);
        EXPECT_EQ(result.failure.cell, expected.back());
        problem.blockedCells.clear();
        problem.modules[0].allowedCells->pop_back();
        EXPECT_EQ(SearchConstraintLayout(problem).failure.code,
                  ConstraintLayoutFailureCode::OutsideAllowedCells);
        problem.modules[0].allowedCells.reset();
        ProductionChamberLayout chamber;
        chamber.interior = {expected.front()};
        problem.chambers = {chamber};
        problem.modules[0].chamber = 0;
        EXPECT_EQ(SearchConstraintLayout(problem).failure.code,
                  ConstraintLayoutFailureCode::OutsideChamber);
    }
}

TEST(ConstraintLayout, RejectsMalformedInputBeforeSearchOrCallbacks) {
    ConstraintLayoutProblem valid;
    valid.modules = {Point({{0, 0}})};
    valid.modules[0].ports = {{{0, 0}, 0}};
    auto check = [&](auto mutate) {
        auto problem = valid;
        mutate(problem);
        bool called = false;
        const auto result = SearchConstraintLayout(problem, {}, [&](const auto&) {
            called = true;
            return true;
        });
        EXPECT_EQ(result.status, ConstraintLayoutStatus::Unsatisfiable);
        EXPECT_EQ(result.failure.code, ConstraintLayoutFailureCode::InvalidInput);
        EXPECT_FALSE(result.failure.message.empty());
        EXPECT_EQ(result.nodes, 0U);
        EXPECT_FALSE(called);
    };
    check([](auto& p) { p.modules[0].footprint.clear(); });
    check([](auto& p) { p.modules[0].footprint.push_back({0, 0}); });
    check([](auto& p) { p.modules[0].domain[0].rotation = -1; });
    check([](auto& p) { p.modules[0].domain[0].rotation = 6; });
    check([](auto& p) { p.modules[0].ports[0].direction = 6; });
    check([](auto& p) { p.modules[0].chamber = 0; });
    check([](auto& p) { p.handoffs = {{0, 0, 1, 0}}; });
    check([](auto& p) { p.handoffs = {{0, 1, 0, 0}}; });
    check([](auto& p) { p.handoffs = {{0, 0, 0, 0, {}}}; });
    check([](auto& p) {
        p.handoffs = {{0, 0, 0, 0, {{0, 0}}, true}};
        p.modules[0].ports[0].direction = -1;
    });
    check([](auto& p) {
        p.modules[0].footprint = {{1, 0}};
        p.modules[0].domain[0].translation = {std::numeric_limits<int>::max(), 0};
    });
    check([](auto& p) {
        p.modules[0].ports[0].cell = {1, 0};
        p.modules[0].domain[0].translation = {std::numeric_limits<int>::max(), 0};
    });
}
