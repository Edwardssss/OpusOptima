// Focused tests and the env-gated measurement probe for the compact scheduled
// row layout. The scheduled-row path defaults to compact
// (`OPUSOPTIMA_COMPACT_LAYOUT` unset or "on"); explicit "off" restores the
// legacy geometry. The generic and composite row emitters never read it.
#include <chrono>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "AssemblySolver.hpp"
#include "BinaryParser.hpp"
#include "BinaryWriter.hpp"
#include "LocalOptimizer.hpp"
#include "RowAssembly.hpp"
#include "../unit_test/TestOutputDirectory.hpp"
#include "RepoRoot.hpp"

using namespace opusoptima::io;

namespace {
const char* kCompactLayout = "OPUSOPTIMA_COMPACT_LAYOUT";

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

std::filesystem::path Root() {
    return opusoptima::test::RepoRoot(__FILE__);
}

std::size_t TrackCells(const SolutionData& solution) {
    std::size_t cells = 0;
    for (const auto& object : solution.objects)
        if (object.typeName == "track") cells += object.bodyData.size();
    return cells;
}

std::size_t TrackCount(const SolutionData& solution) {
    std::size_t tracks = 0;
    for (const auto& object : solution.objects)
        if (object.typeName == "track") ++tracks;
    return tracks;
}
} // namespace

// Default-on must stay stable, and explicit "off" must restore the legacy
// geometry byte-for-byte. Unknown values keep the legacy geometry.
TEST(CompactLayout, FlagOffRestoresLegacyMachine) {
    const auto puzzle = BinaryParser::ParsePuzzleFile(Root() / "asset/puzzle/easy/just-salt.puzzle");
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    const auto plan = opusoptima::solvers::AssemblyPlanner::Plan(puzzle, false, deadline);
    FlagGuard guard(kCompactLayout);
    setenv(kCompactLayout, "off", 1);
    const auto legacy =
        BinaryWriter::WriteSolution(opusoptima::solvers::CompileScheduledRowAssembly(puzzle, plan, deadline));
    for (const char* value : {"off", "unknown", ""}) {
        setenv(kCompactLayout, value, 1);
        const auto bytes = BinaryWriter::WriteSolution(
            opusoptima::solvers::CompileScheduledRowAssembly(puzzle, plan, deadline));
        EXPECT_EQ(bytes, legacy) << "value=" << value;
    }
    unsetenv(kCompactLayout);
    const auto compact =
        opusoptima::solvers::CompileScheduledRowAssembly(puzzle, plan, deadline);
    const auto again =
        opusoptima::solvers::CompileScheduledRowAssembly(puzzle, plan, deadline);
    EXPECT_EQ(BinaryWriter::WriteSolution(again), BinaryWriter::WriteSolution(compact));
    setenv(kCompactLayout, "on", 1);
    const auto explicitOn =
        opusoptima::solvers::CompileScheduledRowAssembly(puzzle, plan, deadline);
    EXPECT_EQ(BinaryWriter::WriteSolution(explicitOn), BinaryWriter::WriteSolution(compact));
    EXPECT_GT(TrackCells(compact), 0U);
}

// The compact layout only contracts geometry: the object/arm inventory and its
// order stay identical, no rail grows, and the machine is deterministic.
TEST(CompactLayout, ShrinksOnlyFeedGeometry) {
    const auto puzzle = BinaryParser::ParsePuzzleFile(Root() / "asset/puzzle/easy/just-salt.puzzle");
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    const auto plan = opusoptima::solvers::AssemblyPlanner::Plan(puzzle, false, deadline);
    FlagGuard guard(kCompactLayout);
    setenv(kCompactLayout, "off", 1);
    const auto baseline =
        opusoptima::solvers::CompileScheduledRowAssembly(puzzle, plan, deadline);
    unsetenv(kCompactLayout);
    const auto compact = opusoptima::solvers::CompileScheduledRowAssembly(puzzle, plan, deadline);
    const auto compactAgain =
        opusoptima::solvers::CompileScheduledRowAssembly(puzzle, plan, deadline);
    setenv(kCompactLayout, "off", 1);
    EXPECT_EQ(BinaryWriter::WriteSolution(compact), BinaryWriter::WriteSolution(compactAgain));
    ASSERT_EQ(compact.objects.size(), baseline.objects.size());
    for (std::size_t i = 0; i < compact.objects.size(); ++i) {
        EXPECT_EQ(compact.objects[i].typeName, baseline.objects[i].typeName) << i;
        if (baseline.objects[i].typeName != "track") continue;
        EXPECT_LE(compact.objects[i].bodyData.size(), baseline.objects[i].bodyData.size()) << i;
        EXPECT_GE(compact.objects[i].bodyData.size(), 2U) << i;
    }
    EXPECT_LT(TrackCells(compact), TrackCells(baseline));
    EXPECT_EQ(TrackCount(compact), TrackCount(baseline));
}

// Paths outside the bounded ordinary row scope never read the flag: the finite
// single-product emitter must stay byte-identical.
TEST(CompactLayout, OutOfScopeEmittersAreUnchanged) {
    const auto puzzle = BinaryParser::ParsePuzzleFile(Root() / "asset/puzzle/easy/just-salt.puzzle");
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    const auto plan = opusoptima::solvers::AssemblyPlanner::Plan(puzzle, false, deadline);
    FlagGuard guard(kCompactLayout);
    unsetenv(kCompactLayout);
    const auto baseline =
        BinaryWriter::WriteSolution(opusoptima::solvers::CompileRowAssemblySingleProduct(puzzle, plan));
    setenv(kCompactLayout, "on", 1);
    const auto compact =
        BinaryWriter::WriteSolution(opusoptima::solvers::CompileRowAssemblySingleProduct(puzzle, plan));
    unsetenv(kCompactLayout);
    EXPECT_EQ(compact, baseline);
}

// The generic row emitter is also embedded as a sub-machine by
// independent-outputs and similar compositions. Compact layout is opt-in only
// on the explicit scheduled-row path, so this emitter must stay byte-identical.
TEST(CompactLayout, GenericRowAssemblyIsUnchanged) {
    const auto puzzle = BinaryParser::ParsePuzzleFile(Root() / "asset/puzzle/easy/just-salt.puzzle");
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    const auto plan = opusoptima::solvers::AssemblyPlanner::Plan(puzzle, false, deadline);
    FlagGuard guard(kCompactLayout);
    unsetenv(kCompactLayout);
    const auto baseline =
        BinaryWriter::WriteSolution(opusoptima::solvers::CompileRowAssembly(puzzle, plan));
    setenv(kCompactLayout, "on", 1);
    const auto compact =
        BinaryWriter::WriteSolution(opusoptima::solvers::CompileRowAssembly(puzzle, plan));
    unsetenv(kCompactLayout);
    EXPECT_EQ(compact, baseline);
}

// Optional measurement probe. One line per case: `mode puzzle output [v8]`.
// Modes 0/2/4 keep the flag off (existing parameter measurements); the odd
// modes request the compact layout so a pair of lines measures the delta.
TEST(CompactLayout, RegisteredLayoutProbe) {
    const auto* cases = std::getenv("OPUS_LAYOUT_CASES");
    if (!cases) GTEST_SKIP() << "Optional registered compact layout measurement";
    std::ifstream input(cases);
    ASSERT_TRUE(input) << cases;
    std::string line;
    std::size_t written = 0;
    while (std::getline(input, line)) {
        if (line.empty()) continue;
        std::istringstream fields(line);
        int mode = 0;
        std::string puzzlePath, outputPath;
        ASSERT_TRUE(static_cast<bool>(fields >> mode >> puzzlePath >> outputPath)) << line;
        std::string extra;
        fields >> extra;
        ASSERT_TRUE(std::filesystem::exists(puzzlePath)) << puzzlePath;
        const auto puzzle = BinaryParser::ParsePuzzleFile(puzzlePath);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
        {
            FlagGuard guard(kCompactLayout);
            setenv(kCompactLayout, mode % 2 == 1 ? "on" : "off", 1);
            SolutionData solution;
            bool compiled = false;
            try {
                // Planning rejects unsupported chemistry/geometry, so it must sit
                // inside the per-case guard: one unusable puzzle must not stop the
                // rest of a measurement batch.
                const auto plan =
                    opusoptima::solvers::AssemblyPlanner::Plan(puzzle, false, deadline);
                solution = mode < 2   ? opusoptima::solvers::CompileScheduledRowAssembly(puzzle, plan, deadline)
                           : mode < 4 ? opusoptima::solvers::CompileRowAssembly(puzzle, plan, 4)
                           : mode < 6 ? opusoptima::solvers::CompileCloseFeedRowAssembly(puzzle, plan)
                                      : opusoptima::solvers::CompileComponentRowAssembly(puzzle, plan);
                compiled = true;
            } catch (const std::exception& error) {
                std::cout << "LAYOUTFAIL " << mode << ' ' << puzzlePath << ' ' << error.what()
                          << std::endl;
            }
            if (!compiled) continue;
            const auto bytes = BinaryWriter::WriteSolution(solution);
            std::ofstream output(outputPath, std::ios::binary | std::ios::trunc);
            ASSERT_TRUE(output) << outputPath;
            output.write(reinterpret_cast<const char*>(bytes.data()),
                         static_cast<std::streamsize>(bytes.size()));
            output.close();
            std::cout << "LAYOUT " << mode << ' ' << outputPath << ' '
                      << CandidateSha256(bytes) << std::endl;
            if (extra == "v8") {
                const auto compacted =
                    BinaryWriter::WriteSolution(opusoptima::solvers::LocalTrial(solution, 8));
                std::ofstream compact(outputPath + ".v8", std::ios::binary | std::ios::trunc);
                ASSERT_TRUE(compact) << outputPath;
                compact.write(reinterpret_cast<const char*>(compacted.data()),
                              static_cast<std::streamsize>(compacted.size()));
                compact.close();
                std::cout << "LAYOUTV8 " << mode << ' ' << outputPath << ".v8 "
                          << CandidateSha256(compacted) << std::endl;
            }
            ++written;
        }
    }
    EXPECT_GT(written, 0U);
}
