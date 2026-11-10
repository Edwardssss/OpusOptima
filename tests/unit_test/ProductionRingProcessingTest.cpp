#include "ProductionRingProcessing.hpp"
#include "BinaryWriter.hpp"
#include "SolutionVerifier.hpp"
#include "Verification/StaticProductionVerifier.hpp"

#include "UnitTestConstants.hpp"
#include <algorithm>
#include <fstream>
#include <gtest/gtest.h>
#include "RepoRoot.hpp"

namespace {
using namespace opusoptima;
std::filesystem::path Root() {
    return opusoptima::test::RepoRoot(__FILE__);
}
std::filesystem::path Original() {
    return Root() / "asset/puzzle/workshop-versions/w2946684660.puzzle";
}
io::PuzzleData Puzzle() {
    return io::BinaryParser::ParsePuzzleFile(Original());
}
struct Files {
    std::filesystem::path puzzle =
        std::filesystem::temp_directory_path() /
        ("ring-processing-" + std::to_string(opusoptima::test::ProcessId()) + ".puzzle");
    std::filesystem::path solution =
        std::filesystem::temp_directory_path() /
        ("ring-processing-" + std::to_string(opusoptima::test::ProcessId()) + ".solution");
    ~Files() {
        std::error_code error;
        std::filesystem::remove(puzzle, error);
        std::filesystem::remove(solution, error);
    }
};
void WritePuzzle(const std::filesystem::path& path, const io::PuzzleData& puzzle) {
    std::ofstream stream(path, std::ios::binary);
    const auto word = [&](std::uint64_t value, int size) {
        for (int byte = 0; byte < size; ++byte)
            stream.put(static_cast<char>(value >> (byte * 8)));
    };
    const auto text = [&](const std::string& value) {
        auto size = value.size();
        do {
            word((size & 127) | (size > 127 ? 128 : 0), 1);
            size >>= 7;
        } while (size);
        stream.write(value.data(), value.size());
    };
    const auto cell = [&](core::HexCoord position) {
        word(position.q, 1);
        word(position.r, 1);
    };
    word(puzzle.version, 4);
    text(puzzle.name);
    word(0, 8);
    word(puzzle.permissionBits, 8);
    for (const auto* list : {&puzzle.inputs, &puzzle.outputs}) {
        word(list->size(), 4);
        for (const auto& molecule : *list) {
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

TEST(ProductionRingProcessing, UnknownGraphsMetadataAndPermissionsNeverReachAcceptance) {
    const auto reject = [](auto change) {
        auto puzzle = Puzzle();
        change(puzzle);
        int calls = 0;
        EXPECT_FALSE(solvers::CompileAcceptedProductionRingProcessing(puzzle, [&](const auto&) {
            ++calls;
            return true;
        }));
        EXPECT_EQ(calls, 0);
    };
    reject([](auto& puzzle) { puzzle.productionInfo.reset(); });
    reject([](auto& puzzle) { puzzle.version = 2; });
    reject([](auto& puzzle) { puzzle.outputMultiplier = 0; });
    reject([](auto& puzzle) { puzzle.productionInfo->isolation = true; });
    reject([](auto& puzzle) { puzzle.productionInfo->chambers[0].chamberType = "Medium"; });
    reject([](auto& puzzle) { puzzle.productionInfo->conduits.clear(); });
    reject([](auto& puzzle) { puzzle.productionInfo->conduits[0].path.push_back({2, 0}); });
    reject([](auto& puzzle) {
        puzzle.productionInfo->conduits[0].positionA = puzzle.productionInfo->conduits[0].positionB;
    });
    reject([](auto& puzzle) { puzzle.productionInfo->vials[0].count = 65; });
    reject([](auto& puzzle) { puzzle.inputs[0].atoms[0].type = core::AtomType::Salt; });
    reject([](auto& puzzle) { puzzle.inputs[0].atoms[0].position.q = 1000000; });
    reject([](auto& puzzle) { puzzle.inputs[0].bonds[0].bits = 14; });
    reject([](auto& puzzle) { puzzle.inputs[0].bonds[1] = puzzle.inputs[0].bonds[0]; });
    reject([](auto& puzzle) { puzzle.outputs[0].bonds[1].bits = 1; });
    reject([](auto& puzzle) { puzzle.outputs[1].atoms[0].type = core::AtomType::Fire; });
    for (int bit : {1, 2, 3, 8, 9, 11, 12, 13, 22, 23, 25, 28})
        reject([&](auto& puzzle) { puzzle.permissionBits &= ~(1ULL << bit); });
}

TEST(ProductionRingProcessing, RejectedCandidatesNeverEscapeAndInfrastructureErrorsPropagate) {
    auto puzzle = Puzzle();
    int calls = 0;
    EXPECT_FALSE(solvers::CompileAcceptedProductionRingProcessing(puzzle, [&](const auto&) {
        ++calls;
        return false;
    }));
    EXPECT_EQ(calls, 6);
    EXPECT_THROW(
        (void)solvers::CompileAcceptedProductionRingProcessing(
            puzzle,
            [](const auto&) -> bool { throw std::runtime_error("ring acceptance unavailable"); }),
        std::runtime_error);
}

#ifdef HAS_LIBVERIFY
TEST(ProductionRingProcessing,
     OriginalCompletesBothOutputsThroughDualMetricRewriteAndCausalNegatives) {
    Files files;
    const auto before = io::BinaryParser::ReadAllBytes(Original());
    const auto puzzle = Puzzle();
    auto machine =
        solvers::CompileAcceptedProductionRingProcessing(puzzle, [&](const auto& candidate) {
            io::BinaryWriter::WriteSolutionFile(files.solution, candidate);
            return io::VerifySolutionFiles(Original(), files.solution, 1000, 10000).Passed();
        });
    ASSERT_TRUE(machine);
    const auto independent = verification::VerifyStaticProduction(puzzle, *machine, 1000);
    ASSERT_TRUE(independent.Passed()) << independent.detail;
    EXPECT_EQ(independent.cycles, 187);
    EXPECT_EQ(independent.outputs, (std::vector<std::size_t>{6, 6}));
    EXPECT_EQ(independent.acquired, 42);
    EXPECT_EQ(independent.remaining, 6);
    EXPECT_EQ(independent.disposed, 0);
    for (int pass = 0; pass < 2; ++pass) {
        io::BinaryWriter::WriteSolutionFile(files.solution, *machine);
        const auto report = io::VerifySolutionFiles(Original(), files.solution, 1000, 10000);
        ASSERT_TRUE(report.Passed()) << report.detail;
        EXPECT_EQ(report.metrics.cycles, independent.cycles);
        machine->metrics = report.metrics;
    }
    EXPECT_EQ(io::BinaryParser::ReadAllBytes(Original()), before);
    const auto published = io::BinaryParser::ReadAllBytes(files.solution);
    for (const auto* part : {"glyph-duplication", "bonder-prisma", "piston"}) {
        auto broken = *machine;
        std::erase_if(broken.objects, [&](const auto& object) { return object.typeName == part; });
        EXPECT_FALSE(verification::VerifyStaticProduction(puzzle, broken, 300).Passed());
        EXPECT_FALSE(io::PublishVerifiedSolution(Original(), files.solution, broken).Passed());
        EXPECT_EQ(io::BinaryParser::ReadAllBytes(files.solution), published);
    }
}

TEST(ProductionRingProcessing,
     RotatedLocalGraphsRenamedMetadataAndTranslatedRoomsRemainDualVerified) {
    Files files;
    for (int rotation = 0; rotation < 6; ++rotation) {
        SCOPED_TRACE(rotation);
        auto puzzle = Puzzle();
        puzzle.name = "graph-matched-ring-variant";
        const auto element = static_cast<core::AtomType>(2 + rotation % 4);
        const auto transform = [&](core::HexCoord position, int turns) {
            for (int i = 0; i < turns; ++i)
                position = core::RotateClockwise(position);
            return core::Add(position, {2, -3});
        };
        for (auto& atom : puzzle.inputs[0].atoms) {
            atom.type = element;
            atom.position = transform(atom.position, rotation);
        }
        for (auto& bond : puzzle.inputs[0].bonds) {
            bond.from = transform(bond.from, rotation);
            bond.to = transform(bond.to, rotation);
            std::swap(bond.from, bond.to);
        }
        std::reverse(puzzle.inputs[0].atoms.begin(), puzzle.inputs[0].atoms.end());
        for (std::size_t port = 0; port < puzzle.outputs.size(); ++port) {
            auto& output = puzzle.outputs[port];
            for (auto& atom : output.atoms)
                atom.position = transform(atom.position, (rotation + port) % 6);
            for (auto& bond : output.bonds) {
                bond.from = transform(bond.from, (rotation + port) % 6);
                bond.to = transform(bond.to, (rotation + port) % 6);
                std::swap(bond.from, bond.to);
            }
            std::reverse(output.atoms.begin(), output.atoms.end());
        }
        if (rotation % 2) std::reverse(puzzle.outputs.begin(), puzzle.outputs.end());
        const core::HexCoord translation{5, -3};
        for (auto& chamber : puzzle.productionInfo->chambers)
            chamber.position = core::Add(chamber.position, translation);
        for (auto& conduit : puzzle.productionInfo->conduits) {
            conduit.positionA = core::Add(conduit.positionA, translation);
            conduit.positionB = core::Add(conduit.positionB, translation);
            std::swap(conduit.positionA, conduit.positionB);
            if (rotation % 2) std::reverse(conduit.path.begin(), conduit.path.end());
        }
        if (rotation % 2)
            std::reverse(puzzle.productionInfo->chambers.begin(),
                         puzzle.productionInfo->chambers.end());
        puzzle.productionInfo->shrinkLeft = rotation & 1;
        puzzle.productionInfo->shrinkRight = rotation & 2;
        puzzle.productionInfo->vials[0].top = rotation % 2;
        puzzle.outputMultiplier = 1 + rotation % 2;
        WritePuzzle(files.puzzle, puzzle);
        const auto decoded = io::BinaryParser::ParsePuzzleFile(files.puzzle);
        std::string failures;
        auto machine =
            solvers::CompileAcceptedProductionRingProcessing(decoded, [&](const auto& candidate) {
                io::BinaryWriter::WriteSolutionFile(files.solution, candidate);
                const auto report =
                    io::VerifySolutionFiles(files.puzzle, files.solution, 2000, 10000);
                if (!report.Passed()) failures += report.detail + "\n";
                return report.Passed();
            });
        ASSERT_TRUE(machine) << failures;
        const auto report = io::PublishVerifiedSolution(files.puzzle, files.solution, *machine);
        ASSERT_TRUE(report.Passed()) << report.detail;
        const auto independent =
            verification::VerifyStaticProductionFiles(files.puzzle, files.solution, 2000);
        ASSERT_TRUE(independent.Passed()) << independent.detail;
        EXPECT_EQ(independent.cycles, report.metrics.cycles);
        EXPECT_EQ(independent.outputs, (std::vector<std::size_t>{
                                           static_cast<std::size_t>(6 * puzzle.outputMultiplier),
                                           static_cast<std::size_t>(6 * puzzle.outputMultiplier)}));
    }
}
#endif
