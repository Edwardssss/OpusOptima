#include "ProductionPetalAssembly.hpp"
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
using Coord = core::HexCoord;
using Type = core::AtomType;
std::filesystem::path Root() {
    return opusoptima::test::RepoRoot(__FILE__);
}
std::filesystem::path Original() {
    return Root() / "asset/puzzle/tourney-2021/OM2021_W4.puzzle";
}
io::PuzzleData Puzzle() {
    return io::BinaryParser::ParsePuzzleFile(Original());
}
struct Files {
    std::filesystem::path puzzle =
        std::filesystem::temp_directory_path() /
        ("petal-assembly-" + std::to_string(opusoptima::test::ProcessId()) + ".puzzle");
    std::filesystem::path solution =
        std::filesystem::temp_directory_path() /
        ("petal-assembly-" + std::to_string(opusoptima::test::ProcessId()) + ".solution");
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
    const auto cell = [&](Coord position) {
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
Coord Rotate(Coord cell, int turns) {
    while (turns-- > 0)
        cell = core::RotateClockwise(cell);
    return cell;
}
} // namespace

TEST(ProductionPetalAssembly, UnsupportedGraphsRoomsAndPermissionsNeverReachAcceptance) {
    const auto reject = [](auto change) {
        auto puzzle = Puzzle();
        change(puzzle);
        int calls = 0;
        try {
            EXPECT_FALSE(solvers::CompileAcceptedProductionPetalAssembly(puzzle, [&](const auto&) {
                ++calls;
                return true;
            }));
        } catch (const std::invalid_argument&) {
        }
        EXPECT_EQ(calls, 0);
    };
    reject([](auto& puzzle) { puzzle.productionInfo.reset(); });
    reject([](auto& puzzle) { puzzle.version = 2; });
    reject([](auto& puzzle) { puzzle.outputMultiplier = 0; });
    reject([](auto& puzzle) { puzzle.outputMultiplier = 3; });
    reject([](auto& puzzle) { puzzle.productionInfo->isolation = true; });
    reject([](auto& puzzle) { puzzle.productionInfo->chambers[0].chamberType = "SmallWide"; });
    reject([](auto& puzzle) { puzzle.productionInfo->conduits.clear(); });
    reject([](auto& puzzle) { puzzle.productionInfo->conduits[0].path.push_back({2, 0}); });
    reject([](auto& puzzle) {
        puzzle.productionInfo->conduits[0].path[1] = puzzle.productionInfo->conduits[0].path[0];
    });
    reject([](auto& puzzle) {
        puzzle.productionInfo->conduits[0].positionA = puzzle.productionInfo->conduits[0].positionB;
    });
    reject([](auto& puzzle) { puzzle.productionInfo->vials[0].count = 65; });
    reject([](auto& puzzle) { puzzle.inputs[0].atoms[0].type = Type::Earth; });
    reject([](auto& puzzle) { puzzle.inputs[0].atoms[0].position.q = 1000000; });
    reject([](auto& puzzle) { puzzle.inputs[0].bonds[0].bits = 14; });
    reject([](auto& puzzle) { puzzle.inputs[0].bonds[1] = puzzle.inputs[0].bonds[0]; });
    reject([](auto& puzzle) { puzzle.outputs[0].bonds[0].bits = 14; });
    reject([](auto& puzzle) { puzzle.outputs[0].atoms[0].type = Type::Mors; });
    reject([](auto& puzzle) { puzzle.outputs[0].bonds[1] = puzzle.outputs[0].bonds[0]; });
    for (int bit : {0, 1, 2, 3, 8, 9, 13, 16, 17, 22, 23, 25, 28})
        reject([&](auto& puzzle) { puzzle.permissionBits &= ~(1ULL << bit); });
}

TEST(ProductionPetalAssembly, RejectedCandidatesNeverEscapeAndCallbackErrorsPropagate) {
    auto puzzle = Puzzle();
    int calls = 0;
    EXPECT_FALSE(solvers::CompileAcceptedProductionPetalAssembly(puzzle, [&](const auto&) {
        ++calls;
        return false;
    }));
    EXPECT_EQ(calls, 24);
    EXPECT_THROW(
        (void)solvers::CompileAcceptedProductionPetalAssembly(
            puzzle,
            [](const auto&) -> bool { throw std::runtime_error("petal acceptance unavailable"); }),
        std::runtime_error);
}

#ifdef HAS_LIBVERIFY
TEST(ProductionPetalAssembly, OriginalHasFullConduitAccountingAndDualMetricRewriteAcceptance) {
    Files files;
    const auto originalBytes = io::BinaryParser::ReadAllBytes(Original());
    const auto puzzle = Puzzle();
    auto machine =
        solvers::CompileAcceptedProductionPetalAssembly(puzzle, [&](const auto& candidate) {
            io::BinaryWriter::WriteSolutionFile(files.solution, candidate);
            return io::VerifySolutionFiles(Original(), files.solution, 1000, 10000).Passed();
        });
    ASSERT_TRUE(machine);
    const auto independent = verification::VerifyStaticProduction(puzzle, *machine, 1000);
    ASSERT_TRUE(independent.Passed()) << independent.detail;
    EXPECT_EQ(independent.cycles, 228);
    EXPECT_EQ(independent.outputs, (std::vector<std::size_t>{6}));
    EXPECT_EQ(independent.acquired, 78);
    EXPECT_EQ(independent.remaining, 4);
    EXPECT_EQ(independent.disposed, 32);
    EXPECT_EQ(std::count_if(independent.events.begin(), independent.events.end(),
                            [](const auto& event) {
                                return event.kind ==
                                       verification::StaticProductionEvent::Kind::Transfer;
                            }),
              24);
    const auto published = io::PublishVerifiedSolution(Original(), files.solution, *machine);
    ASSERT_TRUE(published.Passed()) << published.detail;
    EXPECT_EQ(published.metrics.cycles, independent.cycles);
    EXPECT_EQ(published.metrics.instructionCount, 42);
    EXPECT_EQ(published.metrics.area, 87);
    EXPECT_EQ(published.metrics.cost, 330);
    EXPECT_EQ(io::BinaryParser::ReadAllBytes(Original()), originalBytes);
    const auto publishedBytes = io::BinaryParser::ReadAllBytes(files.solution);
    for (const auto* part : {"glyph-life-and-death", "glyph-duplication", "pipe", "track"}) {
        auto broken = *machine;
        std::erase_if(broken.objects, [&](const auto& object) { return object.typeName == part; });
        EXPECT_FALSE(verification::VerifyStaticProduction(puzzle, broken, 500).Passed());
        EXPECT_FALSE(io::PublishVerifiedSolution(Original(), files.solution, broken).Passed());
        EXPECT_EQ(io::BinaryParser::ReadAllBytes(files.solution), publishedBytes);
    }
}

TEST(ProductionPetalAssembly, MolecularFramesRoomOrderAndVisualVariantsRetainDualAcceptance) {
    Files files;
    for (int variant = 0; variant < 6; ++variant) {
        SCOPED_TRACE(variant);
        auto puzzle = Puzzle();
        puzzle.name = "renamed-petal-production";
        for (auto* list : {&puzzle.inputs, &puzzle.outputs})
            for (auto& molecule : *list) {
                const auto transform = [&](Coord cell) {
                    return core::Add(Rotate(cell, variant), {3, -2});
                };
                for (auto& atom : molecule.atoms)
                    atom.position = transform(atom.position);
                for (auto& bond : molecule.bonds) {
                    bond.from = transform(bond.from);
                    bond.to = transform(bond.to);
                }
                if (variant & 1) {
                    std::reverse(molecule.atoms.begin(), molecule.atoms.end());
                    std::reverse(molecule.bonds.begin(), molecule.bonds.end());
                    for (auto& bond : molecule.bonds)
                        std::swap(bond.from, bond.to);
                }
            }
        const Coord translation{variant + 3, -variant - 2};
        for (auto& chamber : puzzle.productionInfo->chambers)
            chamber.position = core::Add(chamber.position, translation);
        for (auto& conduit : puzzle.productionInfo->conduits) {
            conduit.positionA = core::Add(conduit.positionA, translation);
            conduit.positionB = core::Add(conduit.positionB, translation);
            if (variant & 1) {
                std::swap(conduit.positionA, conduit.positionB);
                std::reverse(conduit.path.begin(), conduit.path.end());
            }
        }
        if (variant & 1) {
            std::reverse(puzzle.productionInfo->chambers.begin(),
                         puzzle.productionInfo->chambers.end());
            std::reverse(puzzle.productionInfo->conduits.begin(),
                         puzzle.productionInfo->conduits.end());
        }
        puzzle.productionInfo->shrinkLeft = variant & 1;
        puzzle.productionInfo->shrinkRight = variant & 2;
        for (auto& vial : puzzle.productionInfo->vials) {
            vial.top = variant & 1;
            vial.count = variant % 3 + 1;
        }
        puzzle.outputMultiplier = variant % 2 + 1;
        WritePuzzle(files.puzzle, puzzle);
        const auto decoded = io::BinaryParser::ParsePuzzleFile(files.puzzle);
        std::string failures;
        const auto machine =
            solvers::CompileAcceptedProductionPetalAssembly(decoded, [&](const auto& candidate) {
                io::BinaryWriter::WriteSolutionFile(files.solution, candidate);
                const auto report =
                    io::VerifySolutionFiles(files.puzzle, files.solution, 2000, 10000);
                if (!report.Passed())
                    failures +=
                        report.detail + " cycle=" + std::to_string(report.errorCycle) + "\n";
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
                                           static_cast<std::size_t>(6 * puzzle.outputMultiplier)}));
        EXPECT_EQ(decoded.productionInfo->vials[0].count, variant % 3 + 1);
        EXPECT_EQ(decoded.productionInfo->shrinkLeft, bool(variant & 1));
        EXPECT_EQ(decoded.productionInfo->shrinkRight, bool(variant & 2));
    }
}
#endif
