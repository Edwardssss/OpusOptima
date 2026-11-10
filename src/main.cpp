#include "CalcifyAnimismusAdmission.hpp"
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

#include "BaseSolver.hpp"
#include "AssemblySolver.hpp"
#include "CandidateArchive.hpp"
#include "LocalOptimizer.hpp"
#include "BinaryParser.hpp"
#include "CoreConstants.hpp"
#include "LogManager.hpp"
#include "SolutionVerifier.hpp"
#include "VerificationJson.hpp"

namespace {

std::optional<opusoptima::io::VerificationMode> AuthorityMode(const std::string& name) {
    using Mode = opusoptima::io::VerificationMode;
    if (name == "legacy-gate") return Mode::LegacyGate;
    if (name == "independent") return Mode::Independent;
    if (name == "shadow") return Mode::Shadow;
    return {};
}
int VerifyCommand(int argc, char* argv[]) {
    using namespace opusoptima::io;
    VerificationOptions options;
    std::string puzzle, solution;
    VerificationReport report;
    try {
        for (int i = 2; i < argc; ++i) {
            const std::string option = argv[i];
            if (option == "--json") continue;
            if (option == "--diagnostics") {
                options.collectTimings = true;
                continue;
            }
            if (option != "--puzzle" && option != "--solution" && option != "--authority" &&
                option != "--cycle-limit" && option != "--timeout-ms")
                throw std::invalid_argument("Unknown verify option: " + option);
            if (++i == argc) throw std::invalid_argument("Missing verify option value");
            const std::string value = argv[i];
            if (option == "--puzzle")
                puzzle = value;
            else if (option == "--solution")
                solution = value;
            else if (option == "--authority") {
                const auto mode = AuthorityMode(value);
                if (!mode) throw std::invalid_argument("Unknown verification authority");
                options.mode = *mode;
            } else {
                int number = 0;
                const auto parsed =
                    std::from_chars(value.data(), value.data() + value.size(), number);
                if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size() ||
                    number <= 0 || (option == "--timeout-ms" && number > 10000) ||
                    (option == "--cycle-limit" && number > 150000))
                    throw std::invalid_argument("Invalid verification limit");
                if (option == "--cycle-limit")
                    options.cycleLimit = number;
                else
                    options.timeoutMilliseconds = number;
            }
        }
        if (puzzle.empty() || solution.empty())
            throw std::invalid_argument("verify requires --puzzle and --solution");
        report = VerifySolutionFiles(puzzle, solution, options);
    } catch (const std::exception& error) {
        report.status = VerificationStatus::Error;
        report.detail = error.what();
        report.reasonCode = "verify-command-invalid";
    }
    std::cout << VerificationReportJson(report) << '\n';
    return VerificationExitCode(report.status);
}

void PrintUsage(const char* programName) {
    const auto names = opusoptima::solvers::BaseSolver::RegisteredNames();
    std::cerr << "Usage: " << programName << " [options]\n\n"
              << "Options:\n"
              << "  -p, --puzzle <path>     Path to .puzzle file\n"
              << "  -o, --output <path>     Output path for .solution file\n"
              << "  -s, --solver <name>     Solver algorithm (default: "
              << opusoptima::solvers::BaseSolver::kDefaultName << ")\n"
              << "                          assembly: recommended bounded portfolio\n"
              << "                          single-atom: unchanged atom; diatomic: normal-bond pair\n"
              << "                          greedy / milp: experimental candidate generators\n"
              << "                          default: legacy greedy alias (not omitted -s)\n"
              << "                          No upstream solver fallback\n"
              << "                          Available:";
    for (const auto& name : names)
        std::cerr << " " << name;
    std::cerr << "\n"
              << "  Output: -o keeps first verified candidate; <output>.objectives.json commits\n"
              << "          cost/cycles/area files (stable first ties, duplicate hashes allowed).\n"
              << "          20s total: 15s construction/admission, 5s final verification reserve.\n"
              << "  -h, --help              Show this help\n"
              << "  -l, --list-solvers      List solvers and exit\n"
              << "  --experimental-shared-assembly  Post-parent shared/mixed inventory assembly (default OFF)\n"
              << "  --experimental-shared-near-cache  Dependency-derived near inventory cache (default OFF)\n"
              << "  --experimental-shared-batch-consumer  Bypass final precursor inventory roundtrip (default OFF)\n"
              << "  --experimental-shared-buffer-relay  Compact mixed inventory slots; requires shared assembly (default OFF)\n"
              << "  --experimental-shared-scheduling  Empty mixed-buffer return overlap; requires shared assembly (default OFF)\n"
              << "  --experimental-compact-relay  Replace first post-parent portfolio attempt (default OFF)\n"
              << "  --authority <mode>     legacy-gate (default), independent or shadow\n"
              << "  verify ... --diagnostics  Include phase timing (not canonical performance "
                 "results)\n\n"
              << "Verify an existing file without solving or rewriting:\n  " << programName
              << " verify --puzzle P --solution S --authority independent --json\n";
}

[[nodiscard]] std::filesystem::path DefaultPuzzlePath() {
    auto cursor = std::filesystem::current_path();
    for (int search_depth = 0; search_depth < opusoptima::core::kDefaultRepoSearchMaxDepth;
         ++search_depth) {
        auto test = cursor / "asset/puzzle/easy/just-salt.puzzle";
        if (std::filesystem::exists(test)) return test;
        if (std::filesystem::exists(cursor / "CMakeLists.txt")) break;
        cursor = cursor.parent_path();
    }
    return cursor / "asset/puzzle/easy/just-salt.puzzle";
}

struct CliOptions {
    std::string puzzlePath;
    std::string outputPath;
    std::string solverName{opusoptima::solvers::BaseSolver::kDefaultName};
    bool showHelp{false};
    bool listSolvers{false};
    bool experimentalCompactRelay{false};
    bool experimentalSharedAssembly{false};
    bool experimentalSharedScheduling{false};
    bool experimentalSharedBufferRelay{false};
    bool experimentalSharedBatchConsumer{false};
    bool experimentalSharedNearCache{false};
    opusoptima::io::VerificationMode authority{opusoptima::io::VerificationMode::LegacyGate};
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
        if (arg == "--experimental-shared-near-cache") {
            options.experimentalSharedNearCache = true;
            continue;
        }
        if (arg == "--experimental-shared-batch-consumer") {
            options.experimentalSharedBatchConsumer = true;
            continue;
        }
        if (arg == "--experimental-shared-buffer-relay") {
            options.experimentalSharedBufferRelay = true;
            continue;
        }
        if (arg == "--experimental-shared-scheduling") {
            options.experimentalSharedScheduling = true;
            continue;
        }
        if (arg == "--experimental-shared-assembly") {
            options.experimentalSharedAssembly = true;
            continue;
        }
        if (arg == "--experimental-compact-relay") {
            options.experimentalCompactRelay = true;
            continue;
        }
        if (arg == "--authority") {
            std::string name;
            if (!HandleOptionWithValue(arg_index, argc, argv, arg, name)) return false;
            const auto mode = AuthorityMode(name);
            if (!mode) {
                spdlog::error("Unknown verification authority: {}", name);
                return false;
            }
            options.authority = *mode;
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
    if (argc > 1 && std::string(argv[1]) == "verify") return VerifyCommand(argc, argv);
    const auto cliDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
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
        logger->info("Default/recommended: {} (omitted -s); default is a legacy greedy alias",
                     opusoptima::solvers::BaseSolver::kDefaultName);
        logger->info("single-atom / diatomic: narrow transport; greedy / milp: experimental");
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

    // ── Solve and independently accept candidates ──
    opusoptima::io::SolutionData solution;
    opusoptima::io::VerificationReport report;
    opusoptima::io::VerificationOptions verificationOptions;
    verificationOptions.outerDeadline = cliDeadline;
    // Preserve the existing search budget; publication reuses exact-byte admission evidence.
    const auto searchDeadline = cliDeadline - std::chrono::seconds(5);
    solver->SetDeadline(searchDeadline);
    if (auto* assembly = dynamic_cast<opusoptima::solvers::AssemblySolver*>(solver.get()))
    {
        assembly->EnableExperimentalCompactRelay(options.experimentalCompactRelay);
        assembly->EnableExperimentalSharedAssembly(options.experimentalSharedAssembly);
        assembly->EnableExperimentalSharedScheduling(options.experimentalSharedScheduling);
        assembly->EnableExperimentalSharedBufferRelay(options.experimentalSharedBufferRelay);
        assembly->EnableExperimentalSharedBatchConsumer(options.experimentalSharedBatchConsumer);
        assembly->EnableExperimentalSharedNearCache(options.experimentalSharedNearCache);
    }
    else if (options.experimentalCompactRelay || options.experimentalSharedAssembly || options.experimentalSharedScheduling || options.experimentalSharedBufferRelay || options.experimentalSharedBatchConsumer || options.experimentalSharedNearCache)
        throw std::invalid_argument("Experimental assembly options require AssemblySolver");
    verificationOptions.mode = options.authority;
    std::filesystem::path outputPath;
    std::unique_ptr<opusoptima::io::CandidateArchive> archive;
    try {
        outputPath = ResolveOutputPath(options.puzzlePath, options.outputPath);
        auto admissionOptions = verificationOptions;
        admissionOptions.outerDeadline = searchDeadline;
        archive = std::make_unique<opusoptima::io::CandidateArchive>(
            options.puzzlePath, outputPath, admissionOptions);
        auto submit = [&](const opusoptima::io::SolutionData& candidate,
                          const std::string& strategy) {
            const auto structuralDeadline = archive->First() &&
                dynamic_cast<opusoptima::solvers::AssemblySolver*>(solver.get())
                ? searchDeadline - std::chrono::seconds(2) : searchDeadline;
            if (std::chrono::steady_clock::now() >= structuralDeadline)
                throw std::runtime_error("Candidate structural phase exhausted");
            if (!opusoptima::io::CalcifyAnimismusModeAllowed(strategy, admissionOptions.mode)) {
                opusoptima::io::VerificationReport rejected;
                rejected.status = opusoptima::io::VerificationStatus::Unavailable;
                rejected.detail = "material stage requires LegacyGate or legacy-primary Shadow";
                return rejected;
            }
            const bool firstPending = !archive->First();
            auto accepted = archive->Submit(candidate, strategy, structuralDeadline);
            if (accepted.Passed()) {
                const auto snapshot = archive->Snapshot(verificationOptions);
                if (!snapshot.Passed()) throw std::runtime_error(snapshot.detail);
            }
            if (firstPending && accepted.Passed()) {
                if (dynamic_cast<opusoptima::solvers::AssemblySolver*>(solver.get()))
                    solver->SetDeadline(searchDeadline - std::chrono::seconds(2));
                logger->info("First verified feasible: {:.6f}s",
                    std::chrono::duration<double>(std::chrono::steady_clock::now() -
                        (cliDeadline - std::chrono::seconds(20))).count());
            }
            if (!accepted.Passed()) {
                logger->warn("Candidate not admitted: {}", accepted.detail);
                if (accepted.status == opusoptima::io::VerificationStatus::Unavailable ||
                    accepted.status == opusoptima::io::VerificationStatus::Error)
                    throw std::runtime_error(accepted.detail);
            }
            return accepted;
        };
        try {
            if (auto* assembly = dynamic_cast<opusoptima::solvers::AssemblySolver*>(solver.get()))
                solution = assembly->CollectVerified(puzzle, submit, 3);
            else
                solution = solver->SolveVerified(puzzle, [&](const auto& candidate) {
                    return submit(candidate, std::string(solver->Name()));
                });
        } catch (const std::exception& e) {
            if (!archive->First()) throw;
            logger->warn("Search stopped; preserving verified archive: {}", e.what());
        }
        if (dynamic_cast<opusoptima::solvers::AssemblySolver*>(solver.get()) && archive->First()) {
            try {
                opusoptima::solvers::OptimizeVerified(*archive, searchDeadline);
            } catch (const std::exception& error) {
                logger->warn("Local search stopped; preserving verified archive: {}", error.what());
            }
        }
        report = archive->Snapshot(verificationOptions);
        if (!report.Passed()) throw std::runtime_error(report.detail);
        solution = archive->First()->solution;
        logger->info("  Objective manifest: {}.objectives.json (stable first-verified ties)",
                     outputPath.string());
    } catch (const std::exception& e) {
        logger->error("Failed to solve/verify/publish solution: {}", e.what());
        return 1;
    }
    solution.metrics = report.metrics;

    // ── Summary ──
    logger->info("═══════════════════════════════");
    logger->info("  Puzzle:    {}", puzzle.name);
    logger->info("  Solver:    {}", solver->Name());
    logger->info("  Cycles:    {}", solution.metrics.cycles);
    logger->info("  Instrs:    {}", solution.metrics.instructionCount);
    logger->info("  Area:      {}", solution.metrics.area);
    logger->info("  Cost:      {}", solution.metrics.cost);
    logger->info("  Valid:     {}", report.Passed() ? "YES ✅" : "NO ⚠️");
    if (!report.detail.empty()) logger->info("  Verification: {}", report.detail);
    logger->info(
        "  Verification authority: {}",
        report.authority == opusoptima::io::VerificationAuthority::Independent  ? "independent"
        : report.authority == opusoptima::io::VerificationAuthority::LegacyGate ? "legacy-gate"
                                                                                : "none");
    if (!report.capabilityProfile.empty())
        logger->info("  Verification profile: {}", report.capabilityProfile);
    logger->info("  Verification result: {}", opusoptima::io::VerificationReportJson(report));
    logger->info("  Output:    {}", outputPath.string());
    logger->info("═══════════════════════════════");

    return 0;
}
