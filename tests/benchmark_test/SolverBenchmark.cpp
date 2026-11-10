#include <benchmark/benchmark.h>

#include <filesystem>
#include <vector>

#include "AStar.hpp"
#include "BinaryParser.hpp"
#include "GreedyPlanner.hpp"
#include "HexSimulator.hpp"
#include "MachineState.hpp"
#include "RecipeMilp.hpp"
#include "RepoRoot.hpp"

namespace {

constexpr int kAStarShortStartQ = -5;
constexpr int kAStarShortStartR = 3;
constexpr int kAStarShortGoalQ = 7;
constexpr int kAStarShortGoalR = -4;

constexpr int kAStarObstaclesStartQ = -3;
constexpr int kAStarObstaclesStartR = 3;
constexpr int kAStarObstaclesGoalQ = 5;
constexpr int kAStarObstaclesGoalR = -3;

constexpr std::size_t kInitialSaltInputs = 10U;
constexpr int kExtraInputGroups = 5;

std::filesystem::path GetRepoRoot() {
    return opusoptima::test::RepoRoot(__FILE__);}

} // namespace

// ── A* pathfinding benchmark ───────────────────────────────

static void BM_AStarShortPath(benchmark::State& state) {
    opusoptima::solvers::AStar astar;
    for (auto _val : state) {
        auto result = astar.FindPath({.q = kAStarShortStartQ, .r = kAStarShortStartR},
                                     {.q = kAStarShortGoalQ, .r = kAStarShortGoalR});
        benchmark::DoNotOptimize(result);
    }
}
BENCHMARK(BM_AStarShortPath);

static void BM_AStarWithObstacles(benchmark::State& state) {
    opusoptima::solvers::AStar astar;
    // Add some obstacles to make pathfinding harder
    astar.AddObstacle({.q = 0, .r = 0});
    astar.AddObstacle({.q = 1, .r = 0});
    astar.AddObstacle({.q = 2, .r = 0});
    astar.AddObstacle({.q = 3, .r = 0});
    astar.AddObstacle({.q = 1, .r = -1});
    astar.AddObstacle({.q = 2, .r = -2});

    for (auto _val : state) {
        auto result = astar.FindPath({.q = kAStarObstaclesStartQ, .r = kAStarObstaclesStartR},
                                     {.q = kAStarObstaclesGoalQ, .r = kAStarObstaclesGoalR});
        benchmark::DoNotOptimize(result);
    }
}
BENCHMARK(BM_AStarWithObstacles);

// ── GreedyPlannerSolver benchmark ──────────────────────────────

static void BM_GreedyPlannerSolverEasyInfiniteProduct(benchmark::State& state) {
    const auto repo = GetRepoRoot();
    const auto puzzlePath = repo / "asset/puzzle/easy/easy-infinite-product.puzzle";
    const auto puzzle = opusoptima::io::BinaryParser::ParsePuzzleFile(puzzlePath);

    for (auto _val : state) {
        opusoptima::solvers::GreedyPlannerSolver solver;
        auto plan = solver.Solve(puzzle);
        benchmark::DoNotOptimize(plan);
    }
}
BENCHMARK(BM_GreedyPlannerSolverEasyInfiniteProduct);

// ── RecipeMilp (HiGHS) benchmark ──────────────────────────

static void BM_RecipeMilpSmall(benchmark::State& state) {
    std::vector<opusoptima::core::AtomType> inputs(kInitialSaltInputs,
                                                   opusoptima::core::AtomType::Salt);
    for (int group_idx = 0; group_idx < kExtraInputGroups; ++group_idx) {
        inputs.push_back(opusoptima::core::AtomType::Air);
        inputs.push_back(opusoptima::core::AtomType::Water);
        inputs.push_back(opusoptima::core::AtomType::Fire);
    }

    std::vector<opusoptima::core::AtomType> outputs = {
        opusoptima::core::AtomType::Salt, opusoptima::core::AtomType::Salt,
        opusoptima::core::AtomType::Air,  opusoptima::core::AtomType::Water,
        opusoptima::core::AtomType::Fire, opusoptima::core::AtomType::Salt,
    };

    for (auto _val : state) {
        auto result = opusoptima::solvers::RecipeMilpSolver::SolveTypes(inputs, outputs);
        benchmark::DoNotOptimize(result);
    }
}
BENCHMARK(BM_RecipeMilpSmall);

// ── Full pipeline: plan + simulate ────────────────────────

static void BM_FullPipelineEasyInfiniteProduct(benchmark::State& state) {
    const auto repo = GetRepoRoot();
    const auto puzzlePath = repo / "asset/puzzle/easy/easy-infinite-product.puzzle";
    const auto puzzle = opusoptima::io::BinaryParser::ParsePuzzleFile(puzzlePath);

    std::int64_t cycleSum = 0;

    for (auto _val : state) {
        // 1. Plan
        opusoptima::solvers::GreedyPlannerSolver solver;
        auto plan = solver.Solve(puzzle);

        // 2. Simulate
        opusoptima::core::MachineState machineState;
        const auto cycles = opusoptima::engine::HexSimulator::Run(puzzle, plan, machineState);
        cycleSum += cycles;

        benchmark::DoNotOptimize(plan);
        benchmark::DoNotOptimize(machineState);
    }

    state.counters["avg_cycles"] =
        benchmark::Counter(static_cast<double>(cycleSum), benchmark::Counter::kAvgIterations);
}
BENCHMARK(BM_FullPipelineEasyInfiniteProduct);
