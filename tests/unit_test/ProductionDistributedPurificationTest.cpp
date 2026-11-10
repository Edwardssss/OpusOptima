#include "ProductionDistributedPurification.hpp"
#include "BinaryWriter.hpp"
#include "SolutionVerifier.hpp"
#include "Verification/StaticProductionVerifier.hpp"

#include "UnitTestConstants.hpp"
#include <algorithm>
#include <fstream>
#include <gtest/gtest.h>
#include <stdexcept>
#include "RepoRoot.hpp"

namespace {
using namespace opusoptima;
const auto root = opusoptima::test::RepoRoot(__FILE__);
const auto original = root / "asset/puzzle/tourney-2020/OM2020_W5_Overloaded.puzzle";
io::PuzzleData Original() {
    return io::BinaryParser::ParsePuzzleFile(original);
}
struct Files {
    const std::filesystem::path puzzle =
        root / ("build/distributed-purification-" + std::to_string(opusoptima::test::ProcessId()) +
                ".puzzle");
    const std::filesystem::path solution =
        root / ("build/distributed-purification-" + std::to_string(opusoptima::test::ProcessId()) +
                ".solution");
    ~Files() {
        std::error_code error;
        std::filesystem::remove(puzzle, error);
        std::filesystem::remove(solution, error);
    }
};
void WritePuzzle(const std::filesystem::path& file, const io::PuzzleData& puzzle) {
    std::ofstream stream(file, std::ios::binary);
    const auto word = [&](std::uint64_t value, int bytes = 4) {
        for (int byte = 0; byte < bytes; ++byte)
            stream.put(static_cast<char>(value >> (8 * byte)));
    };
    const auto text = [&](const std::string& value) {
        auto count = value.size();
        do {
            word((count & 127) | (count > 127 ? 128 : 0), 1);
            count >>= 7;
        } while (count);
        stream.write(value.data(), value.size());
    };
    const auto cell = [&](core::HexCoord position) {
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
    word(puzzle.productionInfo.has_value(), 1);
    if (!puzzle.productionInfo) return;
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
core::HexCoord Rotate(core::HexCoord cell, int turns) {
    while (turns-- > 0)
        cell = core::RotateClockwise(cell);
    return cell;
}
} // namespace

TEST(ProductionDistributedPurification, ExactGraphsPermissionsAndRoomTopologyFailClosed) {
    const auto baseline = Original();
    const auto reject = [&](auto change) {
        auto puzzle = baseline;
        change(puzzle);
        unsigned attempts = 0;
        EXPECT_FALSE(
            solvers::CompileAcceptedProductionDistributedPurification(puzzle, [&](const auto&) {
                ++attempts;
                return true;
            }));
        EXPECT_EQ(attempts, 0);
    };
    for (int permission : {2, 3, 8, 9, 15, 22, 23, 25})
        reject([&](auto& puzzle) { puzzle.permissionBits &= ~(1ULL << permission); });
    reject([](auto& puzzle) { puzzle.productionInfo.reset(); });
    reject([](auto& puzzle) { puzzle.productionInfo->isolation = true; });
    reject([](auto& puzzle) { puzzle.productionInfo->chambers[1].chamberType = "Medium"; });
    reject([](auto& puzzle) {
        puzzle.productionInfo->conduits[0].positionA = puzzle.productionInfo->conduits[0].positionB;
    });
    reject([](auto& puzzle) {
        std::swap(puzzle.productionInfo->conduits[0].path, puzzle.productionInfo->conduits[1].path);
    });
    reject([](auto& puzzle) { puzzle.inputs[0].bonds.pop_back(); });
    reject([](auto& puzzle) { puzzle.inputs[0].bonds[0].bits = 14; });
    reject([](auto& puzzle) { puzzle.inputs[0].atoms[0].type = core::AtomType::Gold; });
    reject([](auto& puzzle) { puzzle.outputs[1].bonds[0].to = puzzle.outputs[1].bonds[1].to; });
    reject([](auto& puzzle) { puzzle.outputs[0].atoms[0].type = core::AtomType::Repeat; });
    reject([](auto& puzzle) { puzzle.version = 2; });
    reject([](auto& puzzle) { puzzle.outputMultiplier = 0; });
    unsigned attempts = 0;
    EXPECT_FALSE(
        solvers::CompileAcceptedProductionDistributedPurification(baseline, [&](const auto&) {
            ++attempts;
            return false;
        }));
    EXPECT_GT(attempts, 0);
    EXPECT_LE(attempts, 8);
    EXPECT_THROW((void)solvers::CompileAcceptedProductionDistributedPurification(
                     baseline,
                     [](const auto&) -> bool {
                         throw std::runtime_error("independent acceptance unavailable");
                     }),
                 std::runtime_error);
}

#ifdef HAS_LIBVERIFY
TEST(ProductionDistributedPurification,
     OriginalMachineUsesEveryRoomAndPassesMetricRewriteAndCausalNegatives) {
    Files files;
    const auto puzzleBytes = io::BinaryParser::ReadAllBytes(original);
    const auto puzzle = Original();
    io::VerificationReport accepted;
    auto machine = solvers::CompileAcceptedProductionDistributedPurification(
        puzzle, [&](const auto& candidate) {
            io::BinaryWriter::WriteSolutionFile(files.solution, candidate);
            accepted = io::VerifySolutionFiles(original, files.solution, 5000, 10000);
            return accepted.Passed();
        });
    ASSERT_TRUE(machine) << accepted.detail;
    EXPECT_EQ(std::count_if(machine->objects.begin(), machine->objects.end(),
                            [](const auto& part) { return part.typeName == "piston"; }),
              4);
    EXPECT_EQ(std::count_if(machine->objects.begin(), machine->objects.end(),
                            [](const auto& part) { return part.typeName == "input"; }),
              4);
    EXPECT_EQ(std::count_if(machine->objects.begin(), machine->objects.end(),
                            [](const auto& part) { return part.typeName == "pipe"; }),
              6);
    machine->metrics = accepted.metrics;
    io::BinaryWriter::WriteSolutionFile(files.solution, *machine);
    const auto repeated = io::VerifySolutionFiles(original, files.solution, 5000, 10000);
    ASSERT_TRUE(repeated.Passed()) << repeated.detail;
    const auto independent =
        verification::VerifyStaticProductionFiles(original, files.solution, 5000);
    ASSERT_TRUE(independent.Passed()) << independent.detail;
    EXPECT_EQ(independent.cycles, repeated.metrics.cycles);
    EXPECT_EQ(independent.outputs, (std::vector<std::size_t>{6, 6}));
    EXPECT_EQ(independent.acquired, 60);
    EXPECT_EQ(independent.remaining, 15);
    EXPECT_EQ(independent.disposed, 0);
    EXPECT_EQ(repeated.metrics.cycles, 813);
    EXPECT_EQ(io::BinaryParser::ReadAllBytes(original), puzzleBytes);
    const auto before = io::BinaryParser::ReadAllBytes(files.solution);
    for (int deletion : {0, 1, 2, 3}) {
        auto broken = *machine;
        std::erase_if(broken.objects, [&](const auto& part) {
            return deletion == 0   ? part.typeName == "glyph-purification"
                   : deletion == 1 ? part.typeName == "piston" && part.bodyData[4] == 2
                   : deletion == 2 ? part.typeName == "pipe"
                                   : part.typeName == "out-std";
        });
        EXPECT_FALSE(io::PublishVerifiedSolution(original, files.solution, broken).Passed());
        EXPECT_EQ(io::BinaryParser::ReadAllBytes(files.solution), before);
        EXPECT_FALSE(verification::VerifyStaticProduction(puzzle, broken, 1500).Passed());
    }
}

TEST(ProductionDistributedPurification,
     LocalFramesPortOrdersRoomsConduitsAndRoomTranslationsRemainVerified) {
    Files files;
    for (int variant = 0; variant < 6; ++variant) {
        SCOPED_TRACE(variant);
        auto puzzle = Original();
        puzzle.name = "structural distributed purification variant";
        for (auto* list : {&puzzle.inputs, &puzzle.outputs})
            for (auto& molecule : *list) {
                const auto transform = [&](core::HexCoord cell) {
                    return core::Add(Rotate(cell, variant), {-2, 3});
                };
                for (auto& atom : molecule.atoms)
                    atom.position = transform(atom.position);
                for (auto& bond : molecule.bonds) {
                    bond.from = transform(bond.from);
                    bond.to = transform(bond.to);
                    std::swap(bond.from, bond.to);
                }
                std::reverse(molecule.atoms.begin(), molecule.atoms.end());
                std::reverse(molecule.bonds.begin(), molecule.bonds.end());
            }
        std::rotate(puzzle.inputs.begin(), puzzle.inputs.begin() + variant % 4,
                    puzzle.inputs.end());
        if (variant % 2) std::reverse(puzzle.outputs.begin(), puzzle.outputs.end());
        std::reverse(puzzle.productionInfo->chambers.begin(),
                     puzzle.productionInfo->chambers.end());
        std::reverse(puzzle.productionInfo->conduits.begin(),
                     puzzle.productionInfo->conduits.end());
        const core::HexCoord translation = variant >= 4 ? core::HexCoord{2, -1} : core::HexCoord{};
        for (auto& chamber : puzzle.productionInfo->chambers)
            chamber.position = core::Add(chamber.position, translation);
        for (auto& conduit : puzzle.productionInfo->conduits) {
            conduit.positionA = core::Add(conduit.positionA, translation);
            conduit.positionB = core::Add(conduit.positionB, translation);
            std::swap(conduit.positionA, conduit.positionB);
            for (auto& cell : conduit.path)
                cell = Rotate(cell, variant);
        }
        puzzle.productionInfo->shrinkLeft = variant & 1;
        puzzle.productionInfo->shrinkRight = variant & 2;
        for (auto& vial : puzzle.productionInfo->vials) {
            vial.position = core::Add(vial.position, translation);
            vial.top = variant & 1;
        }
        WritePuzzle(files.puzzle, puzzle);
        const auto decoded = io::BinaryParser::ParsePuzzleFile(files.puzzle);
        io::VerificationReport accepted;
        const auto machine = solvers::CompileAcceptedProductionDistributedPurification(
            decoded, [&](const auto& candidate) {
                io::BinaryWriter::WriteSolutionFile(files.solution, candidate);
                accepted = io::VerifySolutionFiles(files.puzzle, files.solution, 5000, 10000);
                return accepted.Passed();
            });
        ASSERT_TRUE(machine) << accepted.detail << " cycle=" << accepted.errorCycle;
        auto measured = *machine;
        measured.metrics = accepted.metrics;
        io::BinaryWriter::WriteSolutionFile(files.solution, measured);
        const auto repeated = io::VerifySolutionFiles(files.puzzle, files.solution, 5000, 10000);
        ASSERT_TRUE(repeated.Passed()) << repeated.detail;
        const auto independent = verification::VerifyStaticProduction(decoded, measured, 5000);
        ASSERT_TRUE(independent.Passed()) << independent.detail;
        EXPECT_EQ(independent.cycles, repeated.metrics.cycles);
    }
}
#endif
