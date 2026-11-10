#include "ProductionDirectFeed.hpp"
#include "BinaryWriter.hpp"
#include "Verification/StaticProductionVerifier.hpp"

#include "RepoRoot.hpp"
#include "UnitTestConstants.hpp"
#include <algorithm>
#include <fstream>
#include <gtest/gtest.h>

namespace {
using namespace opusoptima;
const auto root = opusoptima::test::RepoRoot(__FILE__);
const auto original = root / "asset/puzzle/tourney-2019/week5.puzzle";
struct Files {
    std::filesystem::path puzzle =
        root / ("build/direct-feed-" + std::to_string(opusoptima::test::ProcessId()) + ".puzzle");
    std::filesystem::path solution =
        root / ("build/direct-feed-" + std::to_string(opusoptima::test::ProcessId()) + ".solution");
    ~Files() {
        std::error_code error;
        std::filesystem::remove(puzzle, error);
        std::filesystem::remove(solution, error);
    }
};
void WritePuzzle(const std::filesystem::path& path, const io::PuzzleData& puzzle) {
    std::ofstream stream(path, std::ios::binary);
    const auto word = [&](std::uint64_t value, int size = 4) {
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
        for (auto local : conduit.path)
            cell(local);
    }
    word(info.vials.size());
    for (const auto& vial : info.vials) {
        cell(vial.position);
        word(vial.top, 1);
        word(vial.count);
    }
}
} // namespace

TEST(ProductionDirectFeed, InvalidRecipesPermissionsAndRejectedCallbacksNeverPublish) {
    const auto canonical = io::BinaryParser::ParsePuzzleFile(original);
    const auto reject = [&](auto change) {
        auto puzzle = canonical;
        change(puzzle);
        std::size_t attempts = 0;
        EXPECT_FALSE(solvers::CompileAcceptedProductionDirectFeed(puzzle, [&](const auto&) {
            ++attempts;
            return true;
        }));
        EXPECT_EQ(attempts, 0);
    };
    reject([](auto& puzzle) { puzzle.version = 2; });
    reject([](auto& puzzle) { puzzle.productionInfo.reset(); });
    reject([](auto& puzzle) { puzzle.productionInfo->isolation = true; });
    reject([](auto& puzzle) { puzzle.outputMultiplier = 0; });
    reject([](auto& puzzle) { puzzle.inputs[0].atoms[0].position.q = 1000000; });
    reject([](auto& puzzle) { puzzle.outputs[0].bonds[0].bits = 3; });
    reject([](auto& puzzle) { puzzle.outputs[0].atoms.pop_back(); });
    reject([](auto& puzzle) { puzzle.inputs[0].bonds.clear(); });
    for (int bit : {8, 9, 11, 12, 13, 28})
        reject([&](auto& puzzle) { puzzle.permissionBits &= ~(1ULL << bit); });
    std::size_t attempts = 0;
    EXPECT_FALSE(solvers::CompileAcceptedProductionDirectFeed(canonical, [&](const auto&) {
        ++attempts;
        return false;
    }));
    EXPECT_GT(attempts, 0);
    EXPECT_LE(attempts, 64);
    EXPECT_THROW((void)solvers::CompileAcceptedProductionDirectFeed(
                     canonical,
                     [](const auto&) -> bool {
                         throw std::runtime_error("direct feed acceptance unavailable");
                     }),
                 std::runtime_error);
}

#ifdef HAS_LIBVERIFY
TEST(ProductionDirectFeed,
     OriginalWineUsesActualWheelReactionsAndBothExecutorsBeforeAndAfterMetrics) {
    Files files;
    const auto bytes = io::BinaryParser::ReadAllBytes(original);
    const auto puzzle = io::BinaryParser::ParsePuzzleFile(original);
    const auto machine =
        solvers::CompileAcceptedProductionDirectFeed(puzzle, [&](const auto& candidate) {
            io::BinaryWriter::WriteSolutionFile(files.solution, candidate);
            return io::VerifySolutionFiles(original, files.solution, 100).Passed();
        });
    ASSERT_TRUE(machine);
    const auto count = [&](const char* type) {
        return std::count_if(machine->objects.begin(), machine->objects.end(),
                             [&](const auto& part) { return part.typeName == type; });
    };
    EXPECT_EQ(count("input"), 2);
    EXPECT_EQ(count("glyph-calcification"), 1);
    EXPECT_EQ(count("glyph-duplication"), 2);
    EXPECT_EQ(count("baron"), 2);
    EXPECT_EQ(count("unbonder"), 1);
    EXPECT_EQ(count("bonder-prisma"), 3);
    const auto publication = io::PublishVerifiedSolution(original, files.solution, *machine);
    ASSERT_TRUE(publication.Passed()) << publication.detail;
    const auto independent =
        verification::VerifyStaticProductionFiles(original, files.solution, 100);
    ASSERT_TRUE(independent.Passed()) << independent.detail;
    EXPECT_EQ(independent.cycles, publication.metrics.cycles);
    EXPECT_EQ(independent.outputs, (std::vector<std::size_t>{6}));
    EXPECT_EQ(independent.acquired, 36);
    EXPECT_EQ(independent.remaining, 0);
    EXPECT_EQ(publication.metrics.instructionCount, 0);
    EXPECT_EQ(io::BinaryParser::ReadAllBytes(original), bytes);
    const auto published = io::BinaryParser::ReadAllBytes(files.solution);
    for (const auto* type : {"baron", "glyph-calcification", "glyph-duplication", "unbonder",
                             "bonder-prisma", "bonder", "input"}) {
        SCOPED_TRACE(type);
        auto broken = *machine;
        std::erase_if(broken.objects, [&](const auto& part) { return part.typeName == type; });
        EXPECT_FALSE(verification::VerifyStaticProduction(puzzle, broken, 100).Passed());
        EXPECT_FALSE(io::PublishVerifiedSolution(original, files.solution, broken).Passed());
        EXPECT_EQ(io::BinaryParser::ReadAllBytes(files.solution), published);
    }
}

TEST(ProductionDirectFeed, RotatedReorderedTranslatedGraphsAndVisualMetadataDoNotChangeCapability) {
    Files files;
    for (int rotation = 0; rotation < 6; ++rotation)
        for (bool reverse : {false, true}) {
            SCOPED_TRACE(rotation);
            SCOPED_TRACE(reverse);
            auto puzzle = io::BinaryParser::ParsePuzzleFile(original);
            puzzle.name = "direct-feed-structural-variant";
            puzzle.outputMultiplier = reverse ? 2 : 1;
            for (auto* list : {&puzzle.inputs, &puzzle.outputs})
                for (auto& molecule : *list) {
                    const auto transform = [&](core::HexCoord cell) {
                        for (int turn = 0; turn < rotation; ++turn)
                            cell = core::RotateClockwise(cell);
                        return core::Add(cell, list == &puzzle.inputs ? core::HexCoord{-2, 3}
                                                                      : core::HexCoord{3, -2});
                    };
                    for (auto& atom : molecule.atoms)
                        atom.position = transform(atom.position);
                    for (auto& bond : molecule.bonds) {
                        bond.from = transform(bond.from);
                        bond.to = transform(bond.to);
                    }
                    if (reverse) {
                        std::reverse(molecule.atoms.begin(), molecule.atoms.end());
                        std::reverse(molecule.bonds.begin(), molecule.bonds.end());
                    }
                }
            for (auto& chamber : puzzle.productionInfo->chambers)
                chamber.position = core::Add(chamber.position, {10, -5});
            if (reverse)
                std::reverse(puzzle.productionInfo->chambers.begin(),
                             puzzle.productionInfo->chambers.end());
            for (auto& conduit : puzzle.productionInfo->conduits) {
                conduit.positionA = core::Add(conduit.positionA, {10, -5});
                conduit.positionB = core::Add(conduit.positionB, {10, -5});
                if (reverse) std::swap(conduit.positionA, conduit.positionB);
            }
            puzzle.productionInfo->shrinkLeft = rotation & 1;
            puzzle.productionInfo->shrinkRight = rotation & 2;
            puzzle.productionInfo->vials[0].top = reverse;
            puzzle.productionInfo->vials[0].count = reverse ? 3 : 1;
            WritePuzzle(files.puzzle, puzzle);
            const auto decoded = io::BinaryParser::ParsePuzzleFile(files.puzzle);
            std::string failures;
            const auto machine =
                solvers::CompileAcceptedProductionDirectFeed(decoded, [&](const auto& candidate) {
                    io::BinaryWriter::WriteSolutionFile(files.solution, candidate);
                    const auto report = io::VerifySolutionFiles(files.puzzle, files.solution, 100);
                    if (!report.Passed()) failures += report.detail + "\n";
                    return report.Passed();
                });
            ASSERT_TRUE(machine) << failures;
            const auto report = io::PublishVerifiedSolution(files.puzzle, files.solution, *machine);
            ASSERT_TRUE(report.Passed()) << report.detail;
            const auto independent =
                verification::VerifyStaticProductionFiles(files.puzzle, files.solution, 100);
            ASSERT_TRUE(independent.Passed()) << independent.detail;
            EXPECT_EQ(independent.cycles, report.metrics.cycles);
            EXPECT_EQ(
                independent.outputs,
                (std::vector<std::size_t>{static_cast<std::size_t>(6 * puzzle.outputMultiplier)}));
            EXPECT_EQ(independent.remaining, 0);
        }
}
#endif
