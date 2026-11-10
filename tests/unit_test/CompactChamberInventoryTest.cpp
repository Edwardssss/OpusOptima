#include "AssemblySolver.hpp"
#include "AutomaticSupply.hpp"
#include "BinaryWriter.hpp"
#include "SolutionVerifier.hpp"
#include "Verification/StaticProductionVerifier.hpp"

#include "RepoRoot.hpp"
#include "UnitTestConstants.hpp"
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <limits>
#include <thread>

namespace {
using namespace opusoptima;
const auto root = opusoptima::test::RepoRoot(__FILE__);
const auto originalPath = root / "asset/puzzle/workshop-versions/w2591419339.puzzle";
struct Files {
    const std::filesystem::path puzzle =
        root /
        ("build/compact-chamber-" + std::to_string(opusoptima::test::ProcessId()) + ".puzzle");
    const std::filesystem::path solution =
        root /
        ("build/compact-chamber-" + std::to_string(opusoptima::test::ProcessId()) + ".solution");
    ~Files() {
        std::error_code error;
        std::filesystem::remove(puzzle, error);
        std::filesystem::remove(solution, error);
    }
};
void WritePuzzle(const std::filesystem::path& path, const io::PuzzleData& puzzle) {
    std::ofstream stream(path, std::ios::binary);
    const auto word = [&](std::uint64_t value, int size) {
        for (int i = 0; i < size; ++i)
            stream.put(static_cast<char>(value >> (8 * i)));
    };
    const auto text = [&](const std::string& value) {
        word(value.size(), 1);
        stream.write(value.data(), value.size());
    };
    const auto cell = [&](core::HexCoord value) {
        word(value.q, 1);
        word(value.r, 1);
    };
    word(3, 4);
    text(puzzle.name);
    word(0, 8);
    word(puzzle.permissionBits, 8);
    for (const auto* molecules : {&puzzle.inputs, &puzzle.outputs}) {
        word(molecules->size(), 4);
        for (const auto& molecule : *molecules) {
            word(molecule.atoms.size(), 4);
            for (const auto& atom : molecule.atoms) {
                word(static_cast<int>(atom.type), 1);
                cell(atom.position);
            }
            word(molecule.bonds.size(), 4);
            for (const auto& bond : molecule.bonds) {
                word(bond.bits, 1);
                cell(bond.from);
                cell(bond.to);
            }
        }
    }
    word(puzzle.outputMultiplier, 4);
    word(1, 1);
    const auto& info = *puzzle.productionInfo;
    word(info.shrinkLeft, 1);
    word(info.shrinkRight, 1);
    word(info.isolation, 1);
    word(info.chambers.size(), 4);
    for (const auto& chamber : info.chambers) {
        cell(chamber.position);
        text(chamber.chamberType);
    }
    word(info.conduits.size(), 4);
    for (const auto& conduit : info.conduits) {
        cell(conduit.positionA);
        cell(conduit.positionB);
        word(conduit.path.size(), 4);
        for (auto position : conduit.path)
            cell(position);
    }
    word(info.vials.size(), 4);
    for (const auto& vial : info.vials) {
        cell(vial.position);
        word(vial.top, 1);
        word(vial.count, 4);
    }
}
} // namespace

TEST(CompactChamberInventory, SlowCompactRejectionLeavesTimeForRailFallback) {
    io::PuzzleData puzzle;
    puzzle.version = 3;
    puzzle.name = "compact rejection fallback budget";
    puzzle.permissionBits = std::numeric_limits<std::uint64_t>::max();
    puzzle.outputMultiplier = 1;
    puzzle.inputs.push_back({{{core::AtomType::Salt, {}}}, {}});
    puzzle.outputs = puzzle.inputs;
    puzzle.productionInfo = io::ProductionInfo{};
    puzzle.productionInfo->chambers.push_back({{}, "Large"});

    bool delayedCompactRejection = false;
    bool attemptedRailFallback = false;
    (void)solvers::CompileAcceptedSupplyInventory(
        puzzle,
        [&](const io::SolutionData& candidate) {
            if (candidate.solutionName == "Compact chamber supply inventory") {
                if (!delayedCompactRejection) {
                    delayedCompactRejection = true;
                    // Exceed the old shared two-second deadline inside acceptance,
                    // independently of how quickly compact placement itself runs.
                    std::this_thread::sleep_for(std::chrono::milliseconds(2100));
                }
                return false;
            }
            attemptedRailFallback = true;
            return true;
        },
        [&](const solvers::SupplyFailure& failure) {
            // A layout failure also proves that a rail trial ran. Do not count
            // the expired-slice report produced before any compilation attempt.
            if (failure.materialAttempt > 0 && failure.rotation >= 0 && failure.spacing > 0 &&
                failure.category != solvers::SupplyFailureCategory::Budget)
                attemptedRailFallback = true;
        });
    ASSERT_TRUE(delayedCompactRejection) << "fixture must produce a compact candidate";
    EXPECT_TRUE(attemptedRailFallback)
        << "compact rejection must not spend the later rail/replanning time slice";
}

#ifdef HAS_LIBVERIFY
TEST(CompactChamberInventory, OriginalOrePublishesARealClosedTwoProductMachine) {
    Files files;
    const auto puzzle = io::BinaryParser::ParsePuzzleFile(originalPath);
    std::string failures;
    const auto machine =
        solvers::AssemblySolver{}.SolveVerified(puzzle, [&](const auto& candidate) {
            io::BinaryWriter::WriteSolutionFile(files.solution, candidate);
            const auto report = io::VerifySolutionFiles(originalPath, files.solution, 10000, 4000);
            if (!report.Passed()) failures += report.detail + "\n";
            return report;
        });
    EXPECT_EQ(machine.solutionName, "Compact chamber supply inventory") << failures;
    io::BinaryWriter::WriteSolutionFile(files.solution, machine);
    const auto native = io::VerifySolutionFiles(originalPath, files.solution, 10000, 4000);
    ASSERT_TRUE(native.Passed()) << native.detail;
    const auto independent =
        verification::VerifyStaticProductionFiles(originalPath, files.solution, 10000);
    ASSERT_TRUE(independent.Passed()) << independent.detail;
    EXPECT_EQ(independent.cycles, native.metrics.cycles);
    ASSERT_EQ(independent.outputs.size(), 1);
    EXPECT_EQ(independent.outputs[0], 6);
    EXPECT_EQ(independent.disposed, 0);
    EXPECT_EQ(std::count_if(machine.objects.begin(), machine.objects.end(),
                            [](const auto& part) { return part.typeName == "glyph-projection"; }),
              1);
    EXPECT_EQ(
        std::count_if(machine.objects.begin(), machine.objects.end(),
                      [](const auto& part) { return part.typeName == "glyph-calcification"; }),
        1);
    const auto published = io::PublishVerifiedSolution(originalPath, files.solution, machine);
    ASSERT_TRUE(published.Passed()) << published.detail;
    const auto decoded = io::BinaryParser::ParseSolutionFile(files.solution);
    EXPECT_EQ(decoded.metrics.cycles, native.metrics.cycles);
    for (int flags = 0; flags < 4; ++flags) {
        SCOPED_TRACE(flags);
        auto variant = puzzle;
        auto transformed = machine;
        variant.name = "translated compact chamber inventory";
        transformed.puzzleName = variant.name;
        const core::HexCoord delta{7 - flags, -5 + flags};
        for (auto& chamber : variant.productionInfo->chambers)
            chamber.position = core::Add(chamber.position, delta);
        for (auto& vial : variant.productionInfo->vials) {
            vial.position = core::Add(vial.position, delta);
            vial.top = flags & 1;
            vial.count = 1 + flags;
        }
        variant.productionInfo->shrinkLeft = flags & 1;
        variant.productionInfo->shrinkRight = flags & 2;
        for (auto& part : transformed.objects)
            part.position = core::Add(part.position, delta);
        WritePuzzle(files.puzzle, variant);
        io::BinaryWriter::WriteSolutionFile(files.solution, transformed);
        const auto report = io::VerifySolutionFiles(files.puzzle, files.solution, 10000, 4000);
        ASSERT_TRUE(report.Passed()) << report.detail;
        EXPECT_EQ(report.metrics.cycles, native.metrics.cycles);
        auto broken = transformed;
        std::erase_if(broken.objects,
                      [](const auto& part) { return part.typeName == "glyph-projection"; });
        io::BinaryWriter::WriteSolutionFile(files.solution, broken);
        EXPECT_FALSE(io::VerifySolutionFiles(files.puzzle, files.solution, 10000, 4000).Passed());
        EXPECT_FALSE(verification::VerifyStaticProductionFiles(files.puzzle, files.solution, 10000)
                         .Passed());
    }
}
#endif
