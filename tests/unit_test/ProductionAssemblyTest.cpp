#include "ProductionAssembly.hpp"
#include "AssemblySolver.hpp"
#include "BinaryWriter.hpp"
#include "Verification/StaticProductionVerifier.hpp"

#include "UnitTestConstants.hpp"
#include <algorithm>
#include <fstream>
#include <gtest/gtest.h>
#include "RepoRoot.hpp"

namespace {
using namespace opusoptima;
const auto root = opusoptima::test::RepoRoot(__FILE__);
const auto original = root / "asset/puzzle/edge-cases/conduit-bug.puzzle";

struct Files {
    std::filesystem::path puzzle =
        root / ("build/room-assembly-" + std::to_string(opusoptima::test::ProcessId()) + ".puzzle");
    std::filesystem::path solution =
        root /
        ("build/room-assembly-" + std::to_string(opusoptima::test::ProcessId()) + ".solution");
    ~Files() {
        std::error_code error;
        std::filesystem::remove(puzzle, error);
        std::filesystem::remove(solution, error);
    }
};
void WritePuzzle(const std::filesystem::path& path, const io::PuzzleData& puzzle) {
    std::ofstream stream(path, std::ios::binary);
    auto word = [&](std::uint64_t value, int size = 4) {
        for (int byte = 0; byte < size; ++byte)
            stream.put(static_cast<char>(value >> (byte * 8)));
    };
    auto text = [&](const std::string& value) {
        auto size = value.size();
        do {
            word((size & 127) | (size > 127 ? 128 : 0), 1);
            size >>= 7;
        } while (size);
        stream.write(value.data(), value.size());
    };
    auto cell = [&](core::HexCoord position) {
        word(position.q, 1);
        word(position.r, 1);
    };
    word(puzzle.version);
    text(puzzle.name);
    word(0, 8);
    word(puzzle.permissionBits, 8);
    for (const auto* list : {&puzzle.inputs, &puzzle.outputs}) {
        word(list->size());
        for (const auto& molecule : *list) {
            word(molecule.atoms.size());
            for (const auto& atom : molecule.atoms) {
                word(static_cast<int>(atom.type), 1);
                cell(atom.position);
            }
            word(molecule.bonds.size());
            for (const auto& bond : molecule.bonds) {
                word(bond.bits, 1);
                cell(bond.from);
                cell(bond.to);
            }
        }
    }
    word(puzzle.outputMultiplier);
    word(1, 1);
    const auto& info = *puzzle.productionInfo;
    word(info.shrinkLeft, 1);
    word(info.shrinkRight, 1);
    word(info.isolation, 1);
    word(info.chambers.size());
    for (const auto& chamber : info.chambers) {
        cell(chamber.position);
        text(chamber.chamberType);
    }
    word(info.conduits.size());
    for (const auto& conduit : info.conduits) {
        cell(conduit.positionA);
        cell(conduit.positionB);
        word(conduit.path.size());
        for (auto position : conduit.path)
            cell(position);
    }
    word(info.vials.size());
    for (const auto& vial : info.vials) {
        cell(vial.position);
        word(vial.top, 1);
        word(vial.count);
    }
}
} // namespace

TEST(ProductionAssembly, UnsupportedRecipesAndPermissionsDoNotReachAcceptance) {
    const auto originalPuzzle = io::BinaryParser::ParsePuzzleFile(original);
    auto reject = [&](auto change) {
        auto puzzle = originalPuzzle;
        change(puzzle);
        int attempts = 0;
        EXPECT_FALSE(solvers::CompileAcceptedProductionAssembly(puzzle, [&](const auto&) {
            ++attempts;
            return true;
        }));
        EXPECT_EQ(attempts, 0);
    };
    for (int bit : {2, 8, 22, 23})
        reject([&](auto& puzzle) { puzzle.permissionBits &= ~(1ULL << bit); });
    reject([](auto& puzzle) { puzzle.productionInfo->isolation = true; });
    reject([](auto& puzzle) { puzzle.productionInfo.reset(); });
    reject([](auto& puzzle) { puzzle.outputs[0].atoms[0].type = core::AtomType::Repeat; });
    reject([](auto& puzzle) { puzzle.outputs[0].bonds[0].bits = 14; });
    reject([](auto& puzzle) { puzzle.inputs[0].atoms[0].type = core::AtomType::Gold; });
    reject([](auto& puzzle) { puzzle.outputMultiplier = 0; });
    reject([](auto& puzzle) { puzzle.version = 2; });
    unsigned attempts = 0;
    EXPECT_FALSE(solvers::CompileAcceptedProductionAssembly(originalPuzzle, [&](const auto&) {
        ++attempts;
        return false;
    }));
    EXPECT_GT(attempts, 0);
    EXPECT_LE(attempts, 64);
    EXPECT_THROW(
        (void)solvers::CompileAcceptedProductionAssembly(
            originalPuzzle,
            [](const auto&) -> bool { throw std::runtime_error("acceptance unavailable"); }),
        std::runtime_error);
}

#ifdef HAS_LIBVERIFY
TEST(ProductionAssembly, OriginalConduitBugPublishesThroughBothExecutorsAndRejectsBrokenMachines) {
    Files files;
    const auto puzzle = io::BinaryParser::ParsePuzzleFile(original);
    const auto machine =
        solvers::AssemblySolver{}.SolveVerified(puzzle, [&](const auto& candidate) {
            io::BinaryWriter::WriteSolutionFile(files.solution, candidate);
            return io::VerifySolutionFiles(original, files.solution, 2000);
        });
    ASSERT_EQ(machine.solutionName, "Room-local piston assembly");
    const auto published = io::PublishVerifiedSolution(original, files.solution, machine);
    ASSERT_TRUE(published.Passed()) << published.detail;
    const auto independent =
        verification::VerifyStaticProductionFiles(original, files.solution, 2000);
    ASSERT_TRUE(independent.Passed()) << independent.detail;
    EXPECT_EQ(independent.cycles, published.metrics.cycles);
    EXPECT_EQ(independent.outputs, (std::vector<std::size_t>{12}));
    EXPECT_LT(independent.cycles, 2000);
    const auto before = io::BinaryParser::ReadAllBytes(files.solution);
    for (const auto* type : {"bonder", "pipe", "out-std"}) {
        auto broken = machine;
        std::erase_if(broken.objects, [&](const auto& object) { return object.typeName == type; });
        EXPECT_FALSE(io::PublishVerifiedSolution(original, files.solution, broken).Passed());
        EXPECT_EQ(io::BinaryParser::ReadAllBytes(files.solution), before);
    }
}

TEST(ProductionAssembly, ElementOrderGeometryAndVisualVariantsRemainDualVerified) {
    Files files;
    for (int rotation = 0; rotation < 6; ++rotation)
        for (bool reverse : {false, true}) {
            SCOPED_TRACE(rotation);
            SCOPED_TRACE(reverse);
            auto puzzle = io::BinaryParser::ParsePuzzleFile(original);
            const auto element = rotation % 2 ? core::AtomType::Water : core::AtomType::Salt;
            for (auto& atom : puzzle.inputs[0].atoms)
                atom.type = element;
            for (auto& atom : puzzle.outputs[0].atoms)
                atom.type = element;
            auto turn = [&](core::HexCoord cell) {
                for (int i = 0; i < rotation; ++i)
                    cell = core::RotateClockwise(cell);
                return core::Add(cell, {10, -8});
            };
            for (auto& atom : puzzle.outputs[0].atoms)
                atom.position = turn(atom.position);
            for (auto& bond : puzzle.outputs[0].bonds) {
                bond.from = turn(bond.from);
                bond.to = turn(bond.to);
            }
            puzzle.inputs[0].atoms[0].position = {-4, 3};
            for (auto& chamber : puzzle.productionInfo->chambers)
                chamber.position = turn(chamber.position);
            for (auto& conduit : puzzle.productionInfo->conduits) {
                conduit.positionA = turn(conduit.positionA);
                conduit.positionB = turn(conduit.positionB);
            }
            puzzle.productionInfo->shrinkLeft = rotation & 1;
            puzzle.productionInfo->shrinkRight = rotation & 2;
            puzzle.productionInfo->vials[0].top = reverse;
            if (reverse) {
                std::reverse(puzzle.outputs[0].atoms.begin(), puzzle.outputs[0].atoms.end());
                std::reverse(puzzle.outputs[0].bonds.begin(), puzzle.outputs[0].bonds.end());
            }
            WritePuzzle(files.puzzle, puzzle);
            const auto decoded = io::BinaryParser::ParsePuzzleFile(files.puzzle);
            std::string failures;
            const auto machine =
                solvers::CompileAcceptedProductionAssembly(decoded, [&](const auto& candidate) {
                    io::BinaryWriter::WriteSolutionFile(files.solution, candidate);
                    const auto report = io::VerifySolutionFiles(files.puzzle, files.solution, 2000);
                    if (!report.Passed())
                        failures += "base " + std::to_string(candidate.objects[0].position.q) +
                                    "," + std::to_string(candidate.objects[0].position.r) + " " +
                                    report.detail + " at " + std::to_string(report.errorCycle) +
                                    " cell " + std::to_string(report.errorU) + "," +
                                    std::to_string(report.errorV) + "\n";
                    return report.Passed();
                });
            ASSERT_TRUE(machine) << failures;
            const auto report = io::PublishVerifiedSolution(files.puzzle, files.solution, *machine);
            ASSERT_TRUE(report.Passed()) << report.detail;
            const auto independent =
                verification::VerifyStaticProductionFiles(files.puzzle, files.solution, 2000);
            ASSERT_TRUE(independent.Passed()) << independent.detail;
            EXPECT_EQ(independent.cycles, report.metrics.cycles);
        }
}
#endif
