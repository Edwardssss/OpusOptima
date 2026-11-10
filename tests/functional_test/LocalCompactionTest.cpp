// Focused tests for the transport/hardware compaction (default-queued as v8) in
// LocalOptimizer. Every transform is deterministic; only v8 is part of the default
// local-trial queue. Variant 0 remains reachable through the legacy queue.
#include <cstdlib>
#include <chrono>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "BinaryParser.hpp"
#include "BinaryWriter.hpp"
#include "CandidateArchive.hpp"
#include "LocalOptimizer.hpp"
#include "AssemblySolver.hpp"
#include "RowAssembly.hpp"
#include "SingleAtomSolver.hpp"
#include "../unit_test/TestOutputDirectory.hpp"
#include "RepoRoot.hpp"

using namespace opusoptima::io;

namespace {
struct FlagGuard {
    explicit FlagGuard(const char* name) : name_(name) {
        const auto* value = std::getenv(name);
        if (value) saved_ = std::string(value);
    }
    ~FlagGuard() {
        if (saved_) setenv(name_, saved_->c_str(), 1);
        else unsetenv(name_);
    }
    FlagGuard(const FlagGuard&) = delete;
    FlagGuard& operator=(const FlagGuard&) = delete;
    const char* name_;
    std::optional<std::string> saved_;
};
std::string Tape(const std::vector<ArmInstruction>& instructions) {
    std::string text;
    for (const auto& instruction : instructions)
        text += std::string(1, instruction.action) +
                std::to_string(instruction.parameter) + ',';
    return text;
}
std::filesystem::path Root() { return opusoptima::test::RepoRoot(__FILE__); }
}

TEST(LocalCompaction, RetypesOnlyNonExtendingPistons) {
    FlagGuard guard("OPUSOPTIMA_COMPACT_TRANSPORT");
    SolutionData parent;
    parent.objects.push_back({"piston", {0, 0}, {{'G', 0}, {'g', 1}, {'O', 2}}, {}});
    parent.objects.push_back({"piston", {2, 0}, {{'E', 0}, {'G', 1}, {'e', 2}, {'g', 3}}, {}});
    parent.objects.push_back({"piston", {4, 0}, {{'R', 0}, {'G', 1}, {'r', 2}, {'g', 3}}, {}});
    setenv("OPUSOPTIMA_COMPACT_TRANSPORT", "off", 1);
    EXPECT_EQ(BinaryWriter::WriteSolution(opusoptima::solvers::LocalTrial(parent, 8)),
              BinaryWriter::WriteSolution(parent));
    setenv("OPUSOPTIMA_COMPACT_TRANSPORT", "on", 1);
    const auto trial = opusoptima::solvers::LocalTrial(parent, 8);
    ASSERT_EQ(trial.objects.size(), 3);
    EXPECT_EQ(trial.objects[0].typeName, "arm1");
    EXPECT_EQ(Tape(trial.objects[0].instructions), "G0,g1,O2,");
    // Extension and rotation keep the piston mechanism, so retyping is refused.
    EXPECT_EQ(trial.objects[1].typeName, "piston");
    EXPECT_EQ(trial.objects[2].typeName, "piston");
    // Deterministic and idempotent: retyped hardware never converts twice.
    EXPECT_EQ(BinaryWriter::WriteSolution(opusoptima::solvers::LocalTrial(trial, 8)),
              BinaryWriter::WriteSolution(trial));
}

TEST(LocalCompaction, RemovesNoGrabArmsAndRejectsResetTapes) {
    FlagGuard guard("OPUSOPTIMA_COMPACT_TRANSPORT");
    SolutionData parent;
    parent.objects.push_back({"arm1", {0, 0}, {{'G', 0}, {'g', 1}}, {}});
    parent.objects.push_back({"arm1", {2, 0}, {{'R', 0}, {'r', 1}}, {}});
    parent.objects.push_back({"arm1", {4, 0}, {}, {}});
    parent.objects.push_back({"bonder", {6, 0}, {}, {}});
    setenv("OPUSOPTIMA_COMPACT_TRANSPORT", "off", 1);
    EXPECT_EQ(BinaryWriter::WriteSolution(opusoptima::solvers::LocalTrial(parent, 8)),
              BinaryWriter::WriteSolution(parent));
    setenv("OPUSOPTIMA_COMPACT_TRANSPORT", "on", 1);
    const auto trial = opusoptima::solvers::LocalTrial(parent, 8);
    ASSERT_EQ(trial.objects.size(), 2);
    EXPECT_EQ(trial.objects[0].typeName, "arm1");
    EXPECT_EQ(Tape(trial.objects[0].instructions), "G0,g1,");
    EXPECT_EQ(trial.objects[1].typeName, "bonder");
    // Reset/repeat tapes change the serialized period, so hardware edits must refuse.
    auto reset = parent;
    reset.objects[0].instructions[1].action = 'C';
    EXPECT_EQ(BinaryWriter::WriteSolution(opusoptima::solvers::LocalTrial(reset, 8)),
              BinaryWriter::WriteSolution(reset));
}

TEST(LocalCompaction, TrimsAndErasesRailsFromArmMovement) {
    FlagGuard guard("OPUSOPTIMA_COMPACT_TRANSPORT");
    SolutionData parent;
    parent.objects.push_back({"track", {0, 0}, {},
                              {1, 0, 0, 0, 5, 0, 0, 1, 0, 2, 0, 3, 0, 4, 0, 7}});
    parent.objects.push_back({"arm1", {1, 0}, {{'G', 0}, {'A', 1}, {'g', 2}, {'a', 3}}, {}});
    parent.objects.push_back({"track", {20, 0}, {}, {1, 0, 0, 0, 3, 0, 0, 1, 0, 2, 0, 7}});
    setenv("OPUSOPTIMA_COMPACT_TRANSPORT", "off", 1);
    EXPECT_EQ(BinaryWriter::WriteSolution(opusoptima::solvers::LocalTrial(parent, 8)),
              BinaryWriter::WriteSolution(parent));
    setenv("OPUSOPTIMA_COMPACT_TRANSPORT", "on", 1);
    const auto trial = opusoptima::solvers::LocalTrial(parent, 8);
    // The wholly unused rail is erased; the walked rail keeps only visited cells.
    ASSERT_EQ(trial.objects.size(), 2);
    EXPECT_EQ(trial.objects[0].typeName, "track");
    EXPECT_EQ(trial.objects[0].bodyData, (std::vector<int>{1, 0, 0, 0, 2, 1, 0, 2, 0, 7}));
    EXPECT_EQ(trial.objects[1].typeName, "arm1");
    // The trimmed machine is stable under the same pass.
    EXPECT_EQ(BinaryWriter::WriteSolution(opusoptima::solvers::LocalTrial(trial, 8)),
              BinaryWriter::WriteSolution(trial));
    // An off-rail arm with track moves cannot be priced against an unrelated rail.
    auto offRail = parent;
    offRail.objects[1].position = {9, 9};
    const auto refused = opusoptima::solvers::LocalTrial(offRail, 8);
    EXPECT_EQ(refused.objects.size(), 3);
    EXPECT_EQ(BinaryWriter::WriteSolution(refused), BinaryWriter::WriteSolution(offRail));
}

TEST(LocalCompaction, DefaultQueueReplacesV0WithV8AndOffRestoresLegacy) {
    FlagGuard guard("OPUSOPTIMA_COMPACT_TRANSPORT");
    const auto puzzle = Root() / "asset/puzzle/easy/just-salt.puzzle";
    const auto parent = opusoptima::solvers::SingleAtomSolver{}.Solve(
        BinaryParser::ParsePuzzleFile(puzzle));
    // The trace records a "reached" event for every queued variant, so its order is
    // the exact default queue the solver would run.
    const auto orderFor = [&](const char* value) {
        if (value) setenv("OPUSOPTIMA_COMPACT_TRANSPORT", value, 1);
        else unsetenv("OPUSOPTIMA_COMPACT_TRANSPORT");
        opusoptima::test::ScopedTestDirectory scratch;
        VerificationOptions options;
        options.outerDeadline = std::chrono::steady_clock::now() +
                                std::chrono::seconds(15);
        CandidateArchive archive(puzzle, scratch.Path() / "queue.solution", options);
        const auto submitted = archive.Submit(parent, "parent");
        EXPECT_TRUE(submitted.Passed()) << submitted.detail;
        opusoptima::solvers::OptimizeVerified(archive, *options.outerDeadline);
        std::ifstream trace(archive.Directory() / "local-trials.jsonl");
        std::vector<int> order;
        std::string line;
        const std::string key = "\"event\":\"reached\",\"variant\":";
        while (std::getline(trace, line)) {
            const auto at = line.find(key);
            if (at == std::string::npos) continue;
            order.push_back(std::stoi(line.substr(at + key.size())));
        }
        return order;
    };
    const std::vector<int> legacy{0, 1, 2, 4, 5, 6, 7, 3};
    const std::vector<int> current{8, 1, 2, 4, 5, 6, 7, 3};
    EXPECT_EQ(orderFor("off"), legacy);
    EXPECT_EQ(orderFor(nullptr), current);
    EXPECT_EQ(orderFor("on"), current);
}

TEST(LocalCompaction, RegisteredP0CompactionCandidates) {
    const auto* cases = std::getenv("OPUS_P0_CASES");
    if (!cases) GTEST_SKIP() << "Optional registered P0 compaction measurement";
    std::ifstream input(cases);
    ASSERT_TRUE(input) << cases;
    std::string line;
    std::size_t written = 0;
    while (std::getline(input, line)) {
        if (line.empty()) continue;
        std::istringstream fields(line);
        int variant = 0;
        std::string puzzle;
        std::string parentPath;
        std::string outputPath;
        ASSERT_TRUE(static_cast<bool>(fields >> variant >> puzzle >> parentPath >> outputPath))
            << line;
        ASSERT_TRUE(std::filesystem::exists(parentPath)) << parentPath;
        const auto parent = BinaryParser::ParseSolutionFile(parentPath);
        const auto bytes = BinaryWriter::WriteSolution(
            opusoptima::solvers::LocalTrial(parent, variant));
        std::ofstream output(outputPath, std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(output) << outputPath;
        output.write(reinterpret_cast<const char*>(bytes.data()),
                     static_cast<std::streamsize>(bytes.size()));
        output.close();
        // Native judgment is a separate per-objective verify step in the driver.
        static_cast<void>(BinaryParser::ParseSolutionFile(outputPath));
        std::cout << "P0CAND " << variant << ' ' << outputPath << ' '
                  << CandidateSha256(bytes) << std::endl;
        ++written;
    }
    EXPECT_GT(written, 0U);
}

// Optional P1 relay probe: compile a bounded row machine for each listed puzzle
// through the same generator the solver uses. Mode selects the existing entry
// point so a default run can be compared against the frozen P0 parent before any
// relay path is priced.
TEST(RowRelay, RegisteredRowCompilation) {
    const auto* cases = std::getenv("OPUS_ROW_CASES");
    if (!cases) GTEST_SKIP() << "Optional registered row compilation measurement";
    std::ifstream input(cases);
    ASSERT_TRUE(input) << cases;
    std::string line;
    std::size_t written = 0;
    while (std::getline(input, line)) {
        if (line.empty()) continue;
        std::istringstream fields(line);
        int mode = 0;
        std::string puzzlePath;
        std::string outputPath;
        ASSERT_TRUE(static_cast<bool>(fields >> mode >> puzzlePath >> outputPath)) << line;
        ASSERT_TRUE(std::filesystem::exists(puzzlePath)) << puzzlePath;
        const auto puzzle = BinaryParser::ParsePuzzleFile(puzzlePath);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        const auto plan = opusoptima::solvers::AssemblyPlanner::Plan(puzzle, false, deadline);
        SolutionData solution;
        try {
            solution = mode == 1 ? opusoptima::solvers::CompileRowAssembly(puzzle, plan, 3)
                       : mode == 2 ? opusoptima::solvers::CompileRowAssembly(puzzle, plan, 4)
                       : opusoptima::solvers::CompileScheduledRowAssembly(puzzle, plan, deadline);
        } catch (const std::exception& error) {
            std::cout << "ROWFAIL " << mode << ' ' << puzzlePath << ' ' << error.what()
                      << std::endl;
            continue;
        }
        const auto bytes = BinaryWriter::WriteSolution(solution);
        std::ofstream output(outputPath, std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(output) << outputPath;
        output.write(reinterpret_cast<const char*>(bytes.data()),
                     static_cast<std::streamsize>(bytes.size()));
        output.close();
        std::cout << "ROWCAND " << mode << ' ' << outputPath << ' '
                  << CandidateSha256(bytes) << std::endl;
        ++written;
    }
    EXPECT_GT(written, 0U);
}
