#include "ProductionFiniteStock.hpp"
#include "BinaryWriter.hpp"
#include "SolutionVerifier.hpp"
#include "Verification/StaticProductionVerifier.hpp"

#include "RepoRoot.hpp"
#include "UnitTestConstants.hpp"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>

namespace {
using namespace opusoptima;
std::filesystem::path Root() {
    return opusoptima::test::RepoRoot(__FILE__);
}
std::filesystem::path Original() {
    return Root() / "asset/puzzle/workshop-versions/w2788067624.puzzle";
}
io::PuzzleData Puzzle() {
    return io::BinaryParser::ParsePuzzleFile(Original());
}
struct Files {
    std::filesystem::path puzzle =
        Root() /
        ("build/finite-stock-" + std::to_string(opusoptima::test::ProcessId()) + ".puzzle");
    std::filesystem::path solution =
        Root() /
        ("build/finite-stock-" + std::to_string(opusoptima::test::ProcessId()) + ".solution");
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
    const auto& production = *puzzle.productionInfo;
    word(production.shrinkLeft, 1);
    word(production.shrinkRight, 1);
    word(production.isolation, 1);
    word(production.chambers.size(), 4);
    for (const auto& chamber : production.chambers) {
        cell(chamber.position);
        text(chamber.chamberType);
    }
    word(production.conduits.size(), 4);
    for (const auto& conduit : production.conduits) {
        cell(conduit.positionA);
        cell(conduit.positionB);
        word(conduit.path.size(), 4);
        for (auto position : conduit.path)
            cell(position);
    }
    word(production.vials.size(), 4);
    for (const auto& vial : production.vials) {
        cell(vial.position);
        word(vial.top, 1);
        word(vial.count, 4);
    }
}
} // namespace

TEST(ProductionFiniteStock,
     RejectsUnknownPatternsPermissionsAndInsufficientCapacityBeforeAcceptance) {
    const auto check = [](auto mutate) {
        auto puzzle = Puzzle();
        mutate(puzzle);
        int calls = 0;
        EXPECT_FALSE(solvers::CompileAcceptedProductionFiniteStock(puzzle, [&](const auto&) {
            ++calls;
            return true;
        }));
        EXPECT_EQ(calls, 0);
    };
    check([](auto& puzzle) { puzzle.productionInfo.reset(); });
    check([](auto& puzzle) { puzzle.productionInfo->isolation = true; });
    check([](auto& puzzle) { puzzle.version = 2; });
    for (int demand : {0, 3})
        check([&](auto& puzzle) { puzzle.outputMultiplier = demand; });
    for (int bit : {2, 3, 9, 18, 22, 23, 25})
        check([&](auto& puzzle) { puzzle.permissionBits &= ~(1ULL << bit); });
    check([](auto& puzzle) { puzzle.productionInfo->chambers[1].chamberType = "SmallWide"; });
    check([](auto& puzzle) { puzzle.productionInfo->conduits[1].path.push_back({1, 0}); });
    check([](auto& puzzle) {
        puzzle.productionInfo->conduits[1] = puzzle.productionInfo->conduits[0];
    });
    check([](auto& puzzle) { puzzle.inputs[0].atoms[1].type = core::AtomType::Water; });
    check([](auto& puzzle) { puzzle.inputs[0].bonds[0].bits = 14; });
    check([](auto& puzzle) { puzzle.inputs[0].bonds.push_back(puzzle.inputs[0].bonds[0]); });
    check([](auto& puzzle) { puzzle.inputs[0].atoms[0].position.q = 1000000; });
    check([](auto& puzzle) { puzzle.outputs[0].atoms[0].type = core::AtomType::Salt; });
    check([](auto& puzzle) { puzzle.productionInfo->vials[0].count = 65; });
}

TEST(ProductionFiniteStock, RejectionDoesNotReturnAnUnverifiedCandidateAndExceptionsPropagate) {
    auto puzzle = Puzzle();
    int calls = 0;
    EXPECT_FALSE(solvers::CompileAcceptedProductionFiniteStock(puzzle, [&](const auto&) {
        ++calls;
        return false;
    }));
    EXPECT_EQ(calls, 48);
    EXPECT_THROW(
        (void)solvers::CompileAcceptedProductionFiniteStock(
            puzzle,
            [](const auto&) -> bool { throw std::runtime_error("acceptance unavailable"); }),
        std::runtime_error);
}

#ifdef HAS_LIBVERIFY
TEST(ProductionFiniteStock, OriginalHasRealFiniteResidueStockAndDualMetricRewriteAcceptance) {
    Files files;
    const auto original = io::BinaryParser::ReadAllBytes(Original());
    auto puzzle = Puzzle();
    auto machine =
        solvers::CompileAcceptedProductionFiniteStock(puzzle, [&](const auto& candidate) {
            io::BinaryWriter::WriteSolutionFile(files.solution, candidate);
            return io::VerifySolutionFiles(Original(), files.solution, 1000, 10000).Passed();
        });
    ASSERT_TRUE(machine);
    const auto replay = verification::VerifyStaticProduction(puzzle, *machine, 1000);
    ASSERT_TRUE(replay.Passed()) << replay.detail;
    EXPECT_EQ(replay.cycles, 238);
    EXPECT_EQ(replay.acquired, 91);
    EXPECT_EQ(replay.remaining, 31);
    EXPECT_EQ(replay.disposed, 0);
    EXPECT_EQ(replay.outputs, (std::vector<std::size_t>{12, 12}));
    for (int pass = 0; pass < 2; ++pass) {
        io::BinaryWriter::WriteSolutionFile(files.solution, *machine);
        const auto report = io::VerifySolutionFiles(Original(), files.solution, 1000, 10000);
        ASSERT_TRUE(report.Passed()) << report.detail;
        EXPECT_EQ(report.metrics.cycles, 238);
        machine->metrics = report.metrics;
    }
    EXPECT_EQ(io::BinaryParser::ReadAllBytes(Original()), original);
    EXPECT_EQ(puzzle.productionInfo->conduits.size(), 3);
    EXPECT_EQ(puzzle.productionInfo->vials.size(), 2);
    for (const auto* remove : {"glyph-unification", "stock-arm"}) {
        auto broken = *machine;
        std::erase_if(broken.objects, [&](const auto& part) {
            return std::string(remove) == "stock-arm"
                       ? part.typeName == "piston" && part.bodyData[4] == 3
                       : part.typeName == remove;
        });
        io::BinaryWriter::WriteSolutionFile(files.solution, broken);
        EXPECT_FALSE(io::VerifySolutionFiles(Original(), files.solution, 500, 10000).Passed());
        EXPECT_FALSE(verification::VerifyStaticProduction(puzzle, broken, 500).Passed());
    }
}

TEST(ProductionFiniteStock, RotationsReorderedPortsRoomsConduitsAndFiniteDemandRetainAcceptance) {
    Files files;
    for (int rotation = 0; rotation < 6; ++rotation)
        for (int multiplier : {1, 2}) {
            SCOPED_TRACE(rotation);
            SCOPED_TRACE(multiplier);
            auto puzzle = Puzzle();
            puzzle.name = "finite-stock-variant";
            puzzle.outputMultiplier = multiplier;
            const auto transform = [&](core::HexCoord position) {
                for (int turn = 0; turn < rotation; ++turn)
                    position = core::RotateClockwise(position);
                return core::Add(position, {-2, 3});
            };
            for (auto& atom : puzzle.inputs[0].atoms)
                atom.position = transform(atom.position);
            for (auto& bond : puzzle.inputs[0].bonds) {
                bond.from = transform(bond.from);
                bond.to = transform(bond.to);
                std::swap(bond.from, bond.to);
            }
            std::reverse(puzzle.inputs[0].atoms.begin(), puzzle.inputs[0].atoms.end());
            for (auto& room : puzzle.productionInfo->chambers)
                room.position = core::Add(room.position, {20, -10});
            for (auto& pipe : puzzle.productionInfo->conduits) {
                pipe.positionA = core::Add(pipe.positionA, {20, -10});
                pipe.positionB = core::Add(pipe.positionB, {20, -10});
                std::swap(pipe.positionA, pipe.positionB);
            }
            std::reverse(puzzle.productionInfo->chambers.begin(),
                         puzzle.productionInfo->chambers.end());
            std::rotate(puzzle.productionInfo->conduits.begin(),
                        puzzle.productionInfo->conduits.begin() + 1,
                        puzzle.productionInfo->conduits.end());
            puzzle.outputs[0].atoms[0].position = {2, -1};
            puzzle.outputs[1].atoms[0].position = {-1, 2};
            puzzle.productionInfo->shrinkLeft = rotation & 1;
            puzzle.productionInfo->shrinkRight = rotation & 2;
            WritePuzzle(files.puzzle, puzzle);
            const auto decoded = io::BinaryParser::ParsePuzzleFile(files.puzzle);
            auto machine =
                solvers::CompileAcceptedProductionFiniteStock(decoded, [&](const auto& candidate) {
                    io::BinaryWriter::WriteSolutionFile(files.solution, candidate);
                    const auto report =
                        io::VerifySolutionFiles(files.puzzle, files.solution, 1000, 10000);
                    return report.Passed();
                });
            ASSERT_TRUE(machine);
            const auto replay =
                verification::VerifyStaticProductionFiles(files.puzzle, files.solution, 1000);
            ASSERT_TRUE(replay.Passed()) << replay.detail;
            EXPECT_EQ(replay.cycles, 120 * multiplier - 2);
            EXPECT_EQ(replay.outputs,
                      (std::vector<std::size_t>{static_cast<std::size_t>(6 * multiplier),
                                                static_cast<std::size_t>(6 * multiplier)}));
            EXPECT_EQ(replay.remaining, 12 * multiplier + 7);
        }
}
#endif
