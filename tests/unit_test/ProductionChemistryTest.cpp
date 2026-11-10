#include "ProductionChemistry.hpp"
#include "AssemblySolver.hpp"
#include "BinaryWriter.hpp"
#include "SolutionVerifier.hpp"
#include "Verification/StaticProductionVerifier.hpp"

#include "UnitTestConstants.hpp"
#include <algorithm>
#include <fstream>
#include <gtest/gtest.h>
#include <limits>

namespace {
using namespace opusoptima;
io::PuzzleData Puzzle(bool pair = false) {
    io::PuzzleData p;
    p.version = 3;
    p.name = "bounded-production-chemistry";
    p.outputMultiplier = 1;
    p.permissionBits = (1ULL << 0) | (1ULL << 8) | (1ULL << 12) | (1ULL << 22) | (1ULL << 23);
    p.productionInfo.emplace();
    p.productionInfo->chambers.push_back({{}, pair ? "Large" : "Small"});
    p.inputs.push_back({{{core::AtomType::Water, {}}}, {}});
    p.outputs.push_back({{{core::AtomType::Salt, {}}}, {}});
    if (pair) {
        p.inputs.push_back({{{core::AtomType::Fire, {}}}, {}});
        p.outputs[0].atoms.push_back({core::AtomType::Fire, {1, 0}});
        p.outputs[0].bonds.push_back({1, {}, {1, 0}});
    }
    return p;
}
#ifdef HAS_LIBVERIFY
struct Files {
    std::filesystem::path puzzle =
        std::filesystem::current_path() /
        ("production-chemistry-" + std::to_string(opusoptima::test::ProcessId()) + ".puzzle");
    std::filesystem::path solution =
        std::filesystem::current_path() /
        ("production-chemistry-" + std::to_string(opusoptima::test::ProcessId()) + ".solution");
    ~Files() {
        std::error_code ec;
        std::filesystem::remove(puzzle, ec);
        std::filesystem::remove(solution, ec);
    }
};
void WritePuzzle(const std::filesystem::path& path, const io::PuzzleData& p) {
    std::ofstream out(path, std::ios::binary);
    auto word = [&](std::uint64_t v, int n) {
        for (int i = 0; i < n; ++i)
            out.put(static_cast<char>(v >> (8 * i)));
    };
    auto text = [&](const std::string& s) {
        word(s.size(), 1);
        out.write(s.data(), s.size());
    };
    auto cell = [&](core::HexCoord c) {
        word(c.q, 1);
        word(c.r, 1);
    };
    word(3, 4);
    text(p.name);
    word(0, 8);
    word(p.permissionBits, 8);
    for (const auto* list : {&p.inputs, &p.outputs}) {
        word(list->size(), 4);
        for (const auto& m : *list) {
            word(m.atoms.size(), 4);
            for (const auto& a : m.atoms) {
                word(static_cast<std::uint8_t>(a.type), 1);
                cell(a.position);
            }
            word(m.bonds.size(), 4);
            for (const auto& b : m.bonds) {
                word(b.bits, 1);
                cell(b.from);
                cell(b.to);
            }
        }
    }
    word(p.outputMultiplier, 4);
    word(1, 1);
    const auto& info = *p.productionInfo;
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
#endif
} // namespace

TEST(ProductionChemistry, RejectsUnsupportedRulesAndMalformedRecipes) {
    auto check = [](auto mutate, bool pair = false) {
        auto p = Puzzle(pair);
        mutate(p);
        EXPECT_THROW((void)solvers::CompileProductionChemistry(p), std::invalid_argument);
    };
    check([](auto& p) { p.productionInfo.reset(); });
    check([](auto& p) { p.version = 2; });
    check([](auto& p) { p.outputMultiplier = 2; });
    check([](auto& p) { p.productionInfo->conduits.push_back({{}, {}, {{}}}); });
    check([](auto& p) { p.productionInfo->chambers.push_back({{10, 0}, "Large"}); });
    check([](auto& p) { p.productionInfo->chambers[0].chamberType = "Unknown"; });
    check([](auto& p) {
        p.productionInfo->chambers[0].position.q = std::numeric_limits<int>::max();
    });
    check([](auto& p) { p.inputs[0].atoms.clear(); });
    check([](auto& p) { p.inputs[0].atoms[0].type = core::AtomType::Gold; });
    check([](auto& p) { p.outputs[0].atoms[0].type = core::AtomType::Repeat; });
    check([](auto& p) { p.outputs[0].atoms[0].position.q = std::numeric_limits<int>::max(); });
    check([](auto& p) { p.outputs.push_back(p.outputs[0]); });
    for (int bit : {0, 12, 22, 23})
        check([&](auto& p) { p.permissionBits &= ~(1ULL << bit); });
    check([](auto& p) { p.permissionBits &= ~(1ULL << 8); }, true);
    check([](auto& p) { p.productionInfo->chambers[0].chamberType = "Small"; }, true);
    check([](auto& p) { p.outputs[0].bonds[0].bits = 2; }, true);
    check([](auto& p) { p.outputs[0].bonds[0].to = {0, 1}; }, true);
    check([](auto& p) { p.outputs[0].atoms[1].position = {2, 0}; }, true);
}

TEST(ProductionChemistry, AcceptanceRejectionNeverReturnsAnUnverifiedCandidate) {
    solvers::AssemblySolver solver;
    int attempts = 0;
    EXPECT_THROW((void)solver.SolveAccepted(Puzzle(),
                                            [&](const io::SolutionData&) {
                                                ++attempts;
                                                return false;
                                            }),
                 std::exception);
    EXPECT_EQ(attempts, 1);
}

#ifdef HAS_LIBVERIFY
TEST(ProductionChemistry, VialProfilesRequireBothExecutorsAndPreserveOriginalMetadata) {
    Files files;
    for (int profile = 0; profile < 6; ++profile)
        for (int flags = 0; flags < 4; ++flags)
            for (bool style : {false, true}) {
                SCOPED_TRACE(profile);
                SCOPED_TRACE(style);
                auto p = Puzzle(profile == 1 || profile == 2);
                if (profile == 2) {
                    p.permissionBits |= 1ULL << 11;
                    for (auto& input : p.inputs)
                        input.atoms[0].type = core::AtomType::Fire;
                    for (auto& atom : p.outputs[0].atoms)
                        atom.type = core::AtomType::Fire;
                    p.outputs[0].bonds[0].bits = 14;
                } else if (profile == 3) {
                    p.permissionBits |= 1ULL << 16;
                    p.outputs = {{{{core::AtomType::Vitae, {}}}, {}},
                                 {{{core::AtomType::Mors, {}}}, {}}};
                } else if (profile >= 4) {
                    p.permissionBits |= 1ULL << 18;
                    p.inputs[0].atoms[0].type = core::AtomType::Quintessence;
                    p.outputs.clear();
                    for (int type = 2; type <= 5; ++type)
                        p.outputs.push_back(
                            {{{profile == 5 && type % 2 ? core::AtomType::Salt
                                                        : static_cast<core::AtomType>(type),
                               {}}},
                             {}});
                }
                p.productionInfo->shrinkLeft = flags & 1;
                p.productionInfo->shrinkRight = flags & 2;
                p.productionInfo->vials = {{{-5, -3}, style, style ? 3 : 1}};
                WritePuzzle(files.puzzle, p);
                const auto decoded = io::BinaryParser::ParsePuzzleFile(files.puzzle);
                ASSERT_EQ(decoded.productionInfo->vials.size(), 1);
                EXPECT_EQ(decoded.productionInfo->shrinkLeft, bool(flags & 1));
                EXPECT_EQ(decoded.productionInfo->shrinkRight, bool(flags & 2));
                EXPECT_EQ(decoded.productionInfo->vials[0].top, style);
                EXPECT_EQ(decoded.productionInfo->vials[0].count, style ? 3 : 1);
                solvers::AssemblySolver solver;
                const auto machine =
                    solver.SolveVerified(decoded, [&](const io::SolutionData& candidate) {
                        io::BinaryWriter::WriteSolutionFile(files.solution, candidate);
                        const auto report =
                            io::VerifySolutionFiles(files.puzzle, files.solution, 200);
                        EXPECT_TRUE(report.Passed()) << report.detail;
                        return report;
                    });
                io::BinaryWriter::WriteSolutionFile(files.solution, machine);
                const auto native = io::VerifySolutionFiles(files.puzzle, files.solution, 200);
                const auto independent =
                    verification::VerifyStaticProductionFiles(files.puzzle, files.solution, 200);
                ASSERT_TRUE(native.Passed()) << native.detail;
                ASSERT_EQ(independent.status, io::VerificationStatus::Passed) << independent.detail;
                EXPECT_EQ(independent.cycles, native.metrics.cycles);
                auto broken = machine;
                std::erase_if(broken.objects,
                              [](const auto& part) { return part.typeName == "out-std"; });
                io::BinaryWriter::WriteSolutionFile(files.solution, broken);
                EXPECT_FALSE(io::VerifySolutionFiles(files.puzzle, files.solution, 200).Passed());
                EXPECT_NE(
                    verification::VerifyStaticProductionFiles(files.puzzle, files.solution, 200)
                        .status,
                    io::VerificationStatus::Passed);
            }
}

TEST(ProductionChemistry, JointAnimismusConsumesBothProductsAndCalcifiesFeeds) {
    Files files;
    for (bool calcify : {false, true})
        for (bool reverse : {false, true})
            for (int feeds : {1, 2}) {
                auto p = Puzzle();
                p.productionInfo->chambers[0] = {{7, -8}, "Small"};
                p.permissionBits = (1ULL << 16) | (calcify ? (1ULL << 12) : 0);
                p.inputs[0].atoms[0] = {calcify ? core::AtomType::Water : core::AtomType::Salt,
                                        {-2, 3}};
                if (feeds == 2) p.inputs.push_back({{{core::AtomType::Salt, {2, -1}}}, {}});
                p.outputs = {{{{core::AtomType::Vitae, {2, -3}}}, {}},
                             {{{core::AtomType::Mors, {-1, 2}}}, {}}};
                if (reverse) std::reverse(p.outputs.begin(), p.outputs.end());
                WritePuzzle(files.puzzle, p);
                auto machine = solvers::CompileProductionChemistry(p);
                io::BinaryWriter::WriteSolutionFile(files.solution, machine);
                auto report = io::VerifySolutionFiles(files.puzzle, files.solution, 200);
                ASSERT_TRUE(report.Passed()) << report.detail << " cycle=" << report.errorCycle;
                machine.metrics = report.metrics;
                io::BinaryWriter::WriteSolutionFile(files.solution, machine);
                EXPECT_TRUE(io::VerifySolutionFiles(files.puzzle, files.solution, 200).Passed());
                p.permissionBits &= ~(1ULL << 16);
                EXPECT_THROW((void)solvers::CompileProductionChemistry(p), std::invalid_argument);
            }
}

TEST(ProductionChemistry, JointDispersionConsumesAllCoproductsWithoutArms) {
    Files files;
    for (const auto& chamber : {"Small", "Medium", "Large"})
        for (bool reverse : {false, true}) {
            auto p = Puzzle();
            p.productionInfo->chambers[0] = {{7, -8}, chamber};
            p.permissionBits = 1ULL << 18;
            p.inputs[0].atoms[0] = {core::AtomType::Quintessence, {-2, 3}};
            p.outputs.clear();
            for (int type = 2; type <= 5; ++type)
                p.outputs.push_back({{{static_cast<core::AtomType>(type), {type, -2}}}, {}});
            if (reverse) std::reverse(p.outputs.begin(), p.outputs.end());
            WritePuzzle(files.puzzle, p);
            auto machine = solvers::CompileProductionChemistry(p);
            io::BinaryWriter::WriteSolutionFile(files.solution, machine);
            auto report = io::VerifySolutionFiles(files.puzzle, files.solution, 200);
            ASSERT_TRUE(report.Passed()) << report.detail << " cycle=" << report.errorCycle;
            machine.metrics = report.metrics;
            io::BinaryWriter::WriteSolutionFile(files.solution, machine);
            EXPECT_TRUE(io::VerifySolutionFiles(files.puzzle, files.solution, 200).Passed());
            p.outputs[1] = p.outputs[0];
            EXPECT_THROW((void)solvers::CompileProductionChemistry(p), std::invalid_argument);
        }
}

TEST(ProductionChemistry, JointDispersionSelectivelyCalcifiesEveryCoproductSubset) {
    Files files;
    for (int mask = 1; mask < 16; ++mask)
        for (bool reverse : {false, true}) {
            auto p = Puzzle();
            p.permissionBits = (1ULL << 18) | (1ULL << 12);
            p.inputs[0].atoms[0].type = core::AtomType::Quintessence;
            p.outputs.clear();
            for (int lane = 0; lane < 4; ++lane)
                p.outputs.push_back({{{(mask & (1 << lane)) ? core::AtomType::Salt
                                                            : static_cast<core::AtomType>(lane + 2),
                                       {lane, -2}}},
                                     {}});
            if (reverse) std::reverse(p.outputs.begin(), p.outputs.end());
            WritePuzzle(files.puzzle, p);
            auto machine = solvers::CompileProductionChemistry(p);
            io::BinaryWriter::WriteSolutionFile(files.solution, machine);
            auto report = io::VerifySolutionFiles(files.puzzle, files.solution, 200);
            ASSERT_TRUE(report.Passed()) << "mask=" << mask << " " << report.detail;
            machine.metrics = report.metrics;
            io::BinaryWriter::WriteSolutionFile(files.solution, machine);
            EXPECT_TRUE(io::VerifySolutionFiles(files.puzzle, files.solution, 200).Passed());
            p.permissionBits &= ~(1ULL << 12);
            EXPECT_THROW((void)solvers::CompileProductionChemistry(p), std::invalid_argument);
        }
}

TEST(ProductionChemistry, PairMappingIgnoresInputAndOutputAtomOrder) {
    Files files;
    for (bool reverseInputs : {false, true})
        for (bool reverseOutput : {false, true})
            for (const auto& chamberType :
                 {"SmallWide", "SmallWider", "Medium", "MediumWide", "Large"})
                for (int turns = 0; turns < 6; ++turns) {
                    SCOPED_TRACE(chamberType);
                    SCOPED_TRACE(reverseInputs);
                    SCOPED_TRACE(reverseOutput);
                    SCOPED_TRACE(turns);
                    auto p = Puzzle(true);
                    p.productionInfo->chambers[0].chamberType = chamberType;
                    p.inputs[0].atoms[0].position = {-3, 4};
                    p.inputs[1].atoms[0].position = {2, -1};
                    if (reverseInputs) std::reverse(p.inputs.begin(), p.inputs.end());
                    if (reverseOutput)
                        std::reverse(p.outputs[0].atoms.begin(), p.outputs[0].atoms.end());
                    for (auto& atom : p.outputs[0].atoms) {
                        for (int turn = 0; turn < turns; ++turn)
                            atom.position = core::RotateClockwise(atom.position);
                        atom.position = core::Add(atom.position, {2, -3});
                    }
                    p.outputs[0].bonds[0] = {1, p.outputs[0].atoms[0].position,
                                             p.outputs[0].atoms[1].position};
                    WritePuzzle(files.puzzle, p);
                    auto machine = solvers::CompileProductionChemistry(p);
                    io::BinaryWriter::WriteSolutionFile(files.solution, machine);
                    const auto report = io::VerifySolutionFiles(files.puzzle, files.solution, 200);
                    ASSERT_TRUE(report.Passed()) << report.detail << " cycle=" << report.errorCycle;
                    machine.metrics = report.metrics;
                    io::BinaryWriter::WriteSolutionFile(files.solution, machine);
                    EXPECT_TRUE(
                        io::VerifySolutionFiles(files.puzzle, files.solution, 200).Passed());
                }
}

TEST(ProductionChemistry, FirePairMasksUseOnlyPrismaPermission) {
    Files files;
    for (const auto& chamberType : {"SmallWide", "SmallWider", "Medium", "MediumWide", "Large"})
        for (int mask : {2, 4, 6, 8, 10, 12, 14})
            for (int turns = 0; turns < 6; ++turns) {
                SCOPED_TRACE(chamberType);
                SCOPED_TRACE(mask);
                SCOPED_TRACE(turns);
                auto p = Puzzle(true);
                p.productionInfo->chambers[0] = {{7, -8}, chamberType};
                p.permissionBits &= ~((1ULL << 8) | (1ULL << 12));
                p.permissionBits |= 1ULL << 11;
                for (auto& input : p.inputs)
                    input.atoms[0] = {core::AtomType::Fire, {-2, 3}};
                for (auto& atom : p.outputs[0].atoms) {
                    atom.type = core::AtomType::Fire;
                    for (int turn = 0; turn < turns; ++turn)
                        atom.position = core::RotateClockwise(atom.position);
                    atom.position = core::Add(atom.position, {3, -2});
                }
                p.outputs[0].bonds[0] = {mask, p.outputs[0].atoms[0].position,
                                         p.outputs[0].atoms[1].position};
                WritePuzzle(files.puzzle, p);
                auto machine = solvers::CompileProductionChemistry(p);
                io::BinaryWriter::WriteSolutionFile(files.solution, machine);
                const auto report = io::VerifySolutionFiles(files.puzzle, files.solution, 200);
                ASSERT_TRUE(report.Passed()) << report.detail << " cycle=" << report.errorCycle;
                machine.metrics = report.metrics;
                io::BinaryWriter::WriteSolutionFile(files.solution, machine);
                EXPECT_TRUE(io::VerifySolutionFiles(files.puzzle, files.solution, 200).Passed());
                auto broken = machine;
                std::erase_if(broken.objects, [](const auto& object) {
                    return object.typeName == "bonder-prisma";
                });
                io::BinaryWriter::WriteSolutionFile(files.solution, broken);
                EXPECT_FALSE(io::VerifySolutionFiles(files.puzzle, files.solution, 200).Passed());
                p.permissionBits &= ~(1ULL << 11);
                EXPECT_THROW((void)solvers::CompileProductionChemistry(p), std::invalid_argument);
            }
}

TEST(ProductionChemistry, SerializedSixProductAcceptanceAndNegativeMutations) {
    Files files;
    for (bool pair : {false, true})
        for (int variant = 0; variant < 6; ++variant) {
            SCOPED_TRACE(pair);
            SCOPED_TRACE(variant);
            auto p = Puzzle(pair);
            p.productionInfo->isolation = variant % 2;
            p.productionInfo->chambers[0].position = {7, -8};
            if (!pair)
                p.productionInfo->chambers[0].chamberType = std::vector<std::string>{
                    "Small", "SmallWide", "SmallWider", "Medium", "MediumWide", "Large"}[variant];
            if (pair && variant >= 3) p.outputs[0].atoms[1].type = core::AtomType::Salt;
            for (auto& m : p.inputs)
                m.atoms[0].position = {-3, 4};
            for (auto& a : p.outputs[0].atoms) {
                for (int turn = 0; turn < variant; ++turn)
                    a.position = core::RotateClockwise(a.position);
                a.position = core::Add(a.position, {2, -3});
            }
            if (pair)
                p.outputs[0].bonds[0] = {1, p.outputs[0].atoms[0].position,
                                         p.outputs[0].atoms[1].position};
            WritePuzzle(files.puzzle, p);
            const auto decoded = io::BinaryParser::ParsePuzzleFile(files.puzzle);
            solvers::AssemblySolver solver;
            auto machine = solver.SolveAccepted(decoded, [&](const io::SolutionData& candidate) {
                io::BinaryWriter::WriteSolutionFile(files.solution, candidate);
                const auto accepted = io::VerifySolutionFiles(files.puzzle, files.solution, 200);
                EXPECT_TRUE(accepted.Passed())
                    << accepted.detail << " cycle=" << accepted.errorCycle;
                return accepted.Passed();
            });
            io::BinaryWriter::WriteSolutionFile(files.solution, machine);
            const auto report = io::VerifySolutionFiles(files.puzzle, files.solution, 200);
            ASSERT_TRUE(report.Passed()) << report.detail << " cycle=" << report.errorCycle
                                         << " cell=" << report.errorU << ',' << report.errorV;
            EXPECT_GT(report.metrics.cycles, 20);
            EXPECT_FALSE(io::VerifySolutionFiles(files.puzzle, files.solution, 20).Passed());
            auto broken = machine;
            std::erase_if(broken.objects, [&](const auto& o) {
                return o.typeName == (pair ? "bonder" : "glyph-calcification");
            });
            io::BinaryWriter::WriteSolutionFile(files.solution, broken);
            EXPECT_FALSE(io::VerifySolutionFiles(files.puzzle, files.solution, 200).Passed());
            broken = machine;
            broken.objects[0].position = core::Add(p.productionInfo->chambers[0].position, {20, 0});
            io::BinaryWriter::WriteSolutionFile(files.solution, broken);
            EXPECT_FALSE(io::VerifySolutionFiles(files.puzzle, files.solution, 200).Passed());
        }
}
#endif
