#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

#include "BaseSolver.hpp"
#include "BinaryParser.hpp"
#include "BinaryWriter.hpp"
#include "CoreConstants.hpp"
#include "HexSimulator.hpp"
#include "LogManager.hpp"
#include "MachineState.hpp"

namespace {

void PrintUsage(const char* programName) {
    const auto names = opusoptima::solvers::BaseSolver::RegisteredNames();
    std::cerr << "Usage: " << programName << " [options]\n\n"
              << "Options:\n"
              << "  -p, --puzzle <path>     Path to .puzzle file\n"
              << "  -o, --output <path>     Output path for .solution file\n"
              << "  -s, --solver <name>     Solver algorithm\n"
              << "                          Available:";
    for (const auto& name : names)
        std::cerr << " " << name;
    std::cerr << "\n"
              << "  -h, --help              Show this help\n"
              << "  -l, --list-solvers      List solvers and exit\n";
}

[[nodiscard]] std::filesystem::path DefaultPuzzlePath() {
    auto cursor = std::filesystem::current_path();
    for (int search_depth = 0; search_depth < opusoptima::core::kDefaultRepoSearchMaxDepth;
         ++search_depth) {
        auto test = cursor / "asset/puzzle/easy/easy-conduit.puzzle";
        if (std::filesystem::exists(test)) return test;
        if (std::filesystem::exists(cursor / "CMakeLists.txt")) break;
        cursor = cursor.parent_path();
    }
    return cursor / "asset/puzzle/easy/easy-conduit.puzzle";
}

struct CliOptions {
    std::string puzzlePath;
    std::string outputPath;
    std::string solverName{"GreedyPlannerSolver"};
    bool showHelp{false};
    bool listSolvers{false};
};

bool HandleOptionWithValue(int& argIndex, int argc, char* argv[], const std::string& currentArg,
                           std::string& targetValue) {
    if (++argIndex >= argc) {
        spdlog::error("Missing arg for {}", currentArg);
        return false;
    }
    targetValue = argv[argIndex];
    return true;
}

bool ParseCliOptions(int argc, char* argv[], CliOptions& options) {
    for (int arg_index = 1; arg_index < argc; ++arg_index) {
        const std::string arg = argv[arg_index];
        if (arg == "-p" || arg == "--puzzle") {
            if (!HandleOptionWithValue(arg_index, argc, argv, arg, options.puzzlePath))
                return false;
            continue;
        }
        if (arg == "-o" || arg == "--output") {
            if (!HandleOptionWithValue(arg_index, argc, argv, arg, options.outputPath))
                return false;
            continue;
        }
        if (arg == "-s" || arg == "--solver") {
            if (!HandleOptionWithValue(arg_index, argc, argv, arg, options.solverName))
                return false;
            continue;
        }
        if (arg == "-h" || arg == "--help") {
            options.showHelp = true;
            continue;
        }
        if (arg == "-l" || arg == "--list-solvers") {
            options.listSolvers = true;
            continue;
        }
        spdlog::error("Unknown argument: {}", arg);
        PrintUsage(argv[0]);
        return false;
    }
    return true;
}

// ── Validation ───────────────────────────────────────────────────

struct ValidationReport {
    bool passed{false};
    std::string detail;
};

/// Run the solution through the simulator and verify output atoms
/// match the puzzle requirements.
[[nodiscard]] ValidationReport ValidateSolution(const opusoptima::io::PuzzleData& puzzle,
                                                const opusoptima::io::SolutionData& solution) {

    auto logger = opusoptima::GetLoggerOrFallback("validator");

    opusoptima::core::MachineState state;
    try {
        const std::int32_t simCycles =
            opusoptima::engine::HexSimulator::Run(puzzle, solution, state);
        logger->info("Simulation completed in {} cycles", simCycles);
    } catch (const std::exception& e) {
        logger->error("Simulation crashed: {}", e.what());
        return {false, std::string("Simulation exception: ") + e.what()};
    }

    std::map<opusoptima::core::AtomType, std::size_t> expected;
    for (const auto& molecule : puzzle.outputs)
        for (const auto& atom : molecule.atoms)
            expected[atom.type]++;

    std::map<opusoptima::core::AtomType, std::size_t> actual;
    std::size_t totalAtoms = 0;
    for (const auto& [pos, cell] : state.grid) {
        if (cell.occupant == opusoptima::core::CellOccupant::Atom) {
            actual[cell.atomType]++;
            totalAtoms++;
        }
    }

    bool allMatch = true;
    std::string detail;
    for (const auto& [type, expCount] : expected) {
        std::size_t actCount = actual[type];
        if (actCount < expCount) {
            allMatch = false;
            detail += "  missing " + std::to_string(expCount - actCount) + " x " +
                      std::string(opusoptima::core::AtomTypeName(type)) + "\n";
        }
    }

    if (allMatch) {
        logger->info("Validation PASSED — {} atom types matched, {} atoms on grid", expected.size(),
                     totalAtoms);
        return {true, "All output atom types matched"};
    }

    logger->warn("Validation FAILED — atom type mismatch:\n{}", detail);
    return {false, "Atom type mismatch:\n" + detail};
}

// ── Output path ───────────────────────────────────────────────────

[[nodiscard]] std::filesystem::path ResolveOutputPath(const std::filesystem::path& puzzlePath,
                                                      const std::string& explicitOutputPath) {
    if (!explicitOutputPath.empty()) return explicitOutputPath;
    const auto resultDir = std::filesystem::path("result");
    std::filesystem::create_directories(resultDir);
    return resultDir / (puzzlePath.stem().string() + "-solution.solution");
}

} // namespace

// ──────────────────────────────────────────────────────────────────

int main(int argc, char* argv[]) {
    // ── Logging ──
    opusoptima::LogManager::Init("log");
    auto logger = opusoptima::LogManager::Get();

    spdlog::register_logger(opusoptima::LogManager::Module("solver"));
    spdlog::register_logger(opusoptima::LogManager::Module("io"));
    spdlog::register_logger(opusoptima::LogManager::Module("simulator"));
    spdlog::register_logger(opusoptima::LogManager::Module("validator"));

    // ── CLI ──
    CliOptions options;
    if (!ParseCliOptions(argc, argv, options)) return 1;
    if (options.showHelp) {
        PrintUsage(argv[0]);
        return 0;
    }
    if (options.listSolvers) {
        logger->info("Available solvers:");
        for (const auto& name : opusoptima::solvers::BaseSolver::RegisteredNames())
            logger->info("  - {}", name);
        return 0;
    }

    if (options.puzzlePath.empty()) {
        options.puzzlePath = DefaultPuzzlePath().string();
        logger->info("No puzzle specified — using default: {}", options.puzzlePath);
    }
    if (!std::filesystem::exists(options.puzzlePath)) {
        logger->error("Puzzle file not found: {}", options.puzzlePath);
        return 1;
    }

    // ── Solver ──
    auto solver = opusoptima::solvers::BaseSolver::Create(options.solverName);
    if (!solver) {
        logger->error("Unknown solver: '{}'", options.solverName);
        return 1;
    }
    logger->info("Using solver: {}", solver->Name());

    // ── Parse ──
    opusoptima::io::PuzzleData puzzle;
    try {
        puzzle = opusoptima::io::BinaryParser::ParsePuzzleFile(
            std::filesystem::path(options.puzzlePath));
    } catch (const std::exception& e) {
        logger->error("Failed to parse puzzle: {}", e.what());
        return 1;
    }
    logger->info("Puzzle '{}' loaded — {} inputs, {} outputs", puzzle.name, puzzle.inputs.size(),
                 puzzle.outputs.size());

    // ── Solve ──
    opusoptima::io::SolutionData solution;
    try {
        solution = solver->Solve(puzzle);
    } catch (const std::exception& e) {
        logger->error("Solver failed: {}", e.what());
        return 1;
    }
    logger->info("Solution: {} cycles, {} instrs, area={}, cost={}", solution.metrics.cycles,
                 solution.metrics.instructionCount, solution.metrics.area, solution.metrics.cost);

    // ── Validate ──
    auto report = ValidateSolution(puzzle, solution);
    if (!report.passed) {
        logger->warn("⚠️  Solution validation issue — will still write for inspection");
    }

    // ── Write ──
    const auto outputPath =
        ResolveOutputPath(std::filesystem::path(options.puzzlePath), options.outputPath);
    try {
        opusoptima::io::BinaryWriter::WriteSolutionFile(outputPath, solution);
    } catch (const std::exception& e) {
        logger->error("Failed to write solution: {}", e.what());
        return 1;
    }

    // ── Summary ──
    logger->info("═══════════════════════════════");
    logger->info("  Puzzle:    {}", puzzle.name);
    logger->info("  Solver:    {}", solver->Name());
    logger->info("  Cycles:    {}", solution.metrics.cycles);
    logger->info("  Instrs:    {}", solution.metrics.instructionCount);
    logger->info("  Area:      {}", solution.metrics.area);
    logger->info("  Cost:      {}", solution.metrics.cost);
    logger->info("  Valid:     {}", report.passed ? "YES ✅" : "NO ⚠️");
    logger->info("  Output:    {}", outputPath.string());
    logger->info("═══════════════════════════════");

    return 0;
}
