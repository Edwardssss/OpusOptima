#include "ProductionTransport.hpp"
#include "AssemblySolver.hpp"
#include "BinaryWriter.hpp"
#include "Disassembly.hpp"
#include "ProductionJointChemistry.hpp"
#include "ProductionLayout.hpp"
#include "SolutionVerifier.hpp"

#include "RepoRoot.hpp"
#include "UnitTestConstants.hpp"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <limits>

namespace {
using namespace opusoptima;
std::filesystem::path Root() {
    return opusoptima::test::RepoRoot(__FILE__);
}
io::PuzzleData Puzzle() {
    return io::BinaryParser::ParsePuzzleFile(Root() / "asset/puzzle/easy/easy-conduit.puzzle");
}
#ifdef HAS_LIBVERIFY
struct Files {
    std::filesystem::path puzzle =
        Root() /
        ("build/production-test-" + std::to_string(opusoptima::test::ProcessId()) + ".puzzle");
    std::filesystem::path solution =
        Root() /
        ("build/production-test-" + std::to_string(opusoptima::test::ProcessId()) + ".solution");
    ~Files() {
        std::filesystem::remove(puzzle);
        std::filesystem::remove(solution);
    }
    void WritePuzzle(const io::PuzzleData& p) const {
        std::ofstream stream(puzzle, std::ios::binary);
        auto byte = [&](int value) { stream.put(static_cast<char>(value)); };
        auto integer = [&](std::uint64_t value, int size = 4) {
            for (int i = 0; i < size; ++i)
                byte((value >> (8 * i)) & 255);
        };
        auto text = [&](const std::string& value) {
            byte(value.size());
            stream.write(value.data(), value.size());
        };
        auto cell = [&](core::HexCoord c) {
            byte(c.q);
            byte(c.r);
        };
        integer(3);
        text(p.name);
        integer(0, 8);
        integer(p.permissionBits, 8);
        for (const auto* list : {&p.inputs, &p.outputs}) {
            integer(list->size());
            for (const auto& molecule : *list) {
                integer(molecule.atoms.size());
                for (const auto& atom : molecule.atoms) {
                    byte(static_cast<int>(atom.type));
                    cell(atom.position);
                }
                integer(molecule.bonds.size());
                for (const auto& bond : molecule.bonds) {
                    byte(bond.bits);
                    cell(bond.from);
                    cell(bond.to);
                }
            }
        }
        integer(p.outputMultiplier);
        byte(1);
        const auto& info = *p.productionInfo;
        byte(info.shrinkLeft);
        byte(info.shrinkRight);
        byte(info.isolation);
        integer(info.chambers.size());
        for (const auto& chamber : info.chambers) {
            cell(chamber.position);
            text(chamber.chamberType);
        }
        integer(info.conduits.size());
        for (const auto& conduit : info.conduits) {
            cell(conduit.positionA);
            cell(conduit.positionB);
            integer(conduit.path.size());
            for (auto position : conduit.path)
                cell(position);
        }
        integer(info.vials.size());
        for (const auto& vial : info.vials) {
            cell(vial.position);
            byte(vial.top);
            integer(vial.count);
        }
    }
};
#endif
} // namespace

#ifdef HAS_LIBVERIFY
TEST(ProductionTransport, ConduitDispersionUsesOneSharedCoproductLedger) {
    Files files;
    for (bool calcify : {false, true})
        for (int flags = 0; flags < 4; ++flags) {
            auto p = Puzzle();
            p.name = "conduit-dispersion";
            p.permissionBits |= (1ULL << 0) | (1ULL << 12) | (1ULL << 18) | (1ULL << 22) |
                                (1ULL << 23) | (1ULL << 25);
            p.inputs = {{{{core::AtomType::Quintessence, {0, 0}}}, {}}};
            p.outputs.clear();
            for (auto type : {core::AtomType::Water, core::AtomType::Fire, core::AtomType::Earth,
                              core::AtomType::Air})
                p.outputs.push_back({{{type, {0, 0}}}, {}});
            if (calcify) p.outputs[0].atoms[0].type = core::AtomType::Salt;
            p.productionInfo->chambers = {{{-12, 0}, "Large"}, {{12, 0}, "Large"}};
            p.productionInfo->conduits = {{{-12, 0}, {12, 0}, {{0, 0}}}};
            p.productionInfo->vials = {{{-5, -3}, bool(flags & 1), 2}};
            p.productionInfo->shrinkLeft = flags & 1;
            p.productionInfo->shrinkRight = flags & 2;
            files.WritePuzzle(p);
            std::string failures;
            auto candidate = solvers::CompileAcceptedProductionDispersion(p, [&](const auto& s) {
                io::BinaryWriter::WriteSolutionFile(files.solution, s);
                const auto report =
                    io::VerifySolutionFiles(files.puzzle, files.solution, 10000, 2000);
                if (!report.Passed()) failures += report.detail + "\n";
                return report.Passed();
            });
            ASSERT_TRUE(candidate) << failures;
            for (int pass = 0; pass < 2; ++pass) {
                io::BinaryWriter::WriteSolutionFile(files.solution, *candidate);
                const auto report =
                    io::VerifySolutionFiles(files.puzzle, files.solution, 10000, 2000);
                ASSERT_TRUE(report.Passed()) << report.detail;
                candidate->metrics = report.metrics;
            }
            const auto publicCandidate =
                solvers::AssemblySolver{}.SolveVerified(p, [&](const auto& s) {
                    io::BinaryWriter::WriteSolutionFile(files.solution, s);
                    return io::VerifySolutionFiles(files.puzzle, files.solution, 10000, 2000);
                });
            EXPECT_EQ(publicCandidate.solutionName, candidate->solutionName);
            io::BinaryWriter::WriteSolutionFile(files.solution, publicCandidate);
            EXPECT_TRUE(
                io::VerifySolutionFiles(files.puzzle, files.solution, 10000, 2000).Passed());
        }
}
#endif

#ifdef HAS_LIBVERIFY
TEST(ProductionTransport, BondedConduitFeedSplitsBeforeJointDispersion) {
    Files files;
    auto p = Puzzle();
    p.name = "bonded-conduit-dispersion";
    p.permissionBits |=
        (1ULL << 0) | (1ULL << 9) | (1ULL << 18) | (1ULL << 22) | (1ULL << 23) | (1ULL << 25);
    p.inputs = {{{{core::AtomType::Quintessence, {0, 0}}, {core::AtomType::Salt, {0, 1}}},
                 {{1, {0, 0}, {0, 1}}}}};
    p.outputs.clear();
    for (auto type : {core::AtomType::Salt, core::AtomType::Water, core::AtomType::Fire,
                      core::AtomType::Earth, core::AtomType::Air})
        p.outputs.push_back({{{type, {0, 0}}}, {}});
    p.productionInfo->chambers = {{{-12, 0}, "Large"}, {{12, 0}, "Large"}};
    p.productionInfo->conduits = {{{-12, 0}, {12, 0}, {{0, 0}, {0, 1}}}};
    p.productionInfo->vials.clear();
    p.productionInfo->shrinkLeft = p.productionInfo->shrinkRight = false;
    for (auto companion : {core::AtomType::Salt, core::AtomType::Copper})
        for (bool reverse : {false, true}) {
            auto variant = p;
            variant.inputs[0].atoms[1].type = companion;
            variant.outputs[0].atoms[0].type = companion;
            if (reverse) {
                variant.inputs[0].atoms[0].position = {4, -3};
                variant.inputs[0].atoms[1].position = {5, -3};
                variant.inputs[0].bonds[0] = {1, {4, -3}, {5, -3}};
                variant.productionInfo->conduits[0].path = {{0, 0}, {1, 0}};
                std::reverse(variant.outputs.begin(), variant.outputs.end());
                for (std::size_t port = 0; port < variant.outputs.size(); ++port)
                    variant.outputs[port].atoms[0].position = {static_cast<int>(port + 3), -2};
            }
            files.WritePuzzle(variant);
            std::string failures;
            auto candidate =
                solvers::CompileAcceptedProductionDispersion(variant, [&](const auto& s) {
                    io::BinaryWriter::WriteSolutionFile(files.solution, s);
                    const auto report =
                        io::VerifySolutionFiles(files.puzzle, files.solution, 10000, 2000);
                    if (!report.Passed()) failures += report.detail + "\n";
                    return report.Passed();
                });
            ASSERT_TRUE(candidate)
                << "companion=" << static_cast<int>(companion) << " reversed=" << reverse << "\n"
                << failures;
            for (int pass = 0; pass < 2; ++pass) {
                io::BinaryWriter::WriteSolutionFile(files.solution, *candidate);
                const auto report =
                    io::VerifySolutionFiles(files.puzzle, files.solution, 10000, 2000);
                ASSERT_TRUE(report.Passed()) << report.detail;
                candidate->metrics = report.metrics;
            }
            if (!reverse) {
                const auto publicCandidate =
                    solvers::AssemblySolver{}.SolveVerified(variant, [&](const auto& s) {
                        io::BinaryWriter::WriteSolutionFile(files.solution, s);
                        return io::VerifySolutionFiles(files.puzzle, files.solution, 10000, 2000);
                    });
                EXPECT_EQ(publicCandidate.solutionName, candidate->solutionName);
                io::BinaryWriter::WriteSolutionFile(files.solution, publicCandidate);
                EXPECT_TRUE(
                    io::VerifySolutionFiles(files.puzzle, files.solution, 10000, 2000).Passed());
            }
        }
    auto mismatched = p;
    mismatched.inputs[0].atoms[1].type = core::AtomType::Copper;
    EXPECT_FALSE(
        solvers::CompileAcceptedProductionDispersion(mismatched, [](const auto&) { return true; }));
    auto unsupported = p;
    unsupported.productionInfo->vials.push_back({{}, true, 1});
    files.WritePuzzle(unsupported);
    EXPECT_TRUE(
        solvers::CompileAcceptedProductionDispersion(unsupported, [&](const auto& candidate) {
            io::BinaryWriter::WriteSolutionFile(files.solution, candidate);
            return io::VerifySolutionFiles(files.puzzle, files.solution, 10000, 2000).Passed();
        }));
    unsupported.productionInfo->vials.clear();
    unsupported.version = 2;
    EXPECT_FALSE(solvers::CompileAcceptedProductionDispersion(unsupported,
                                                              [](const auto&) { return true; }));
}
#endif

#ifdef HAS_LIBVERIFY
TEST(ProductionTransport, BondedConduitFeedReturnsBorrowedCatalystAcrossTwoOutputs) {
    Files files;
    for (auto cardinal :
         {core::AtomType::Air, core::AtomType::Earth, core::AtomType::Fire, core::AtomType::Water})
        for (bool reverse : {false, true})
            for (int flags = 0; flags < 4; ++flags) {
                auto p = Puzzle();
                p.name = "bonded-conduit-catalyst";
                p.permissionBits |= (1ULL << 0) | (1ULL << 9) | (1ULL << 13) | (1ULL << 22) |
                                    (1ULL << 23) | (1ULL << 25);
                p.inputs = {
                    {{{core::AtomType::Salt, {0, 0}}, {cardinal, {0, 1}}}, {{1, {0, 0}, {0, 1}}}}};
                if (reverse) std::reverse(p.inputs[0].atoms.begin(), p.inputs[0].atoms.end());
                p.outputs = {{{{cardinal, {0, 0}}}, {}}, {{{cardinal, {1, -2}}}, {}}};
                p.productionInfo->chambers = {{{-12, 0}, "Large"}, {{12, 0}, "Large"}};
                p.productionInfo->conduits = {{{-12, 0}, {12, 0}, {{0, 0}, {0, 1}}}};
                p.productionInfo->vials = {{{-5, -3}, bool(flags & 1), 2}};
                p.productionInfo->shrinkLeft = flags & 1;
                p.productionInfo->shrinkRight = flags & 2;
                files.WritePuzzle(p);
                std::string failures;
                auto candidate =
                    solvers::CompileAcceptedProductionDuplication(p, [&](const auto& s) {
                        io::BinaryWriter::WriteSolutionFile(files.solution, s);
                        const auto report =
                            io::VerifySolutionFiles(files.puzzle, files.solution, 10000, 2000);
                        if (!report.Passed()) failures += report.detail + "\n";
                        return report.Passed();
                    });
                ASSERT_TRUE(candidate)
                    << "element=" << static_cast<int>(cardinal) << " reversed=" << reverse << "\n"
                    << failures;
                for (int pass = 0; pass < 2; ++pass) {
                    io::BinaryWriter::WriteSolutionFile(files.solution, *candidate);
                    const auto report =
                        io::VerifySolutionFiles(files.puzzle, files.solution, 10000, 2000);
                    ASSERT_TRUE(report.Passed()) << report.detail;
                    candidate->metrics = report.metrics;
                }
                if (!reverse) {
                    const auto publicCandidate =
                        solvers::AssemblySolver{}.SolveVerified(p, [&](const auto& s) {
                            io::BinaryWriter::WriteSolutionFile(files.solution, s);
                            return io::VerifySolutionFiles(files.puzzle, files.solution, 10000,
                                                           2000);
                        });
                    EXPECT_EQ(publicCandidate.solutionName, candidate->solutionName);
                    io::BinaryWriter::WriteSolutionFile(files.solution, publicCandidate);
                    EXPECT_TRUE(io::VerifySolutionFiles(files.puzzle, files.solution, 10000, 2000)
                                    .Passed());
                }
            }
}
#endif

TEST(ProductionTransport, JointConduitFeedbackRetainsLocalDecisionScope) {
    auto p = Puzzle();
    p.permissionBits |=
        (1ULL << 0) | (1ULL << 9) | (1ULL << 18) | (1ULL << 22) | (1ULL << 23) | (1ULL << 25);
    p.inputs = {{{{core::AtomType::Quintessence, {0, 0}}, {core::AtomType::Salt, {0, 1}}},
                 {{1, {0, 0}, {0, 1}}}}};
    p.outputs.clear();
    for (auto type : {core::AtomType::Salt, core::AtomType::Air, core::AtomType::Earth,
                      core::AtomType::Fire, core::AtomType::Water})
        p.outputs.push_back({{{type, {0, 0}}}, {}});
    p.productionInfo->chambers = {{{-12, 0}, "Large"}, {{12, 0}, "Large"}};
    p.productionInfo->conduits = {{{-12, 0}, {12, 0}, {{0, 0}, {0, 1}}}};
    p.productionInfo->vials.clear();
    p.productionInfo->shrinkLeft = p.productionInfo->shrinkRight = false;
    std::vector<solvers::SupplyFailure> failures;
    EXPECT_FALSE(solvers::CompileAcceptedProductionDispersion(
        p, [](const auto&) { return false; },
        [&](const auto& failure) { failures.push_back(failure); }));
    EXPECT_TRUE(std::any_of(failures.begin(), failures.end(), [](const auto& failure) {
        return failure.category == solvers::SupplyFailureCategory::Layout && failure.chamber &&
               failure.conduit == 0 && failure.fragment.size() == 2 && failure.conflictCell &&
               failure.mechanicalMode == "conduit-reaction";
    }));
    EXPECT_THROW((void)solvers::CompileAcceptedProductionDispersion(
                     p, [](const auto&) { return false; },
                     [](const auto&) { throw std::runtime_error("conduit feedback unavailable"); }),
                 std::runtime_error);
}

TEST(ProductionTransport, RejectsUnsupportedRulesWithoutWeakeningGenericGate) {
    const auto original = Puzzle();
    EXPECT_THROW(solvers::ValidateSpecialRules(original), std::invalid_argument);
    auto check = [&](auto mutate) {
        auto p = original;
        mutate(p);
        EXPECT_THROW((void)solvers::PlanProductionTransport(p), std::invalid_argument);
        EXPECT_THROW((void)solvers::CompileProductionTransport(p), std::invalid_argument);
    };
    check([](auto& p) { p.productionInfo.reset(); });
    check([](auto& p) { p.productionInfo->vials.push_back({{}, true, 65}); });
    check([](auto& p) { p.productionInfo->chambers[0].chamberType = "Unknown"; });
    check([](auto& p) {
        p.productionInfo->chambers[1].position = p.productionInfo->chambers[0].position;
    });
    check([](auto& p) { p.productionInfo->conduits.clear(); });
    check([](auto& p) { p.productionInfo->conduits.push_back(p.productionInfo->conduits[0]); });
    check([](auto& p) { p.productionInfo->conduits[0].path.push_back({1, 0}); });
    check([](auto& p) { p.productionInfo->conduits[0].path[0] = {1, 0}; });
    check([](auto& p) { p.productionInfo->conduits[0].positionA = {0, 0}; });
    check([](auto& p) {
        p.productionInfo->conduits[0].positionB = p.productionInfo->conduits[0].positionA;
    });
    check([](auto& p) { p.outputs[0].atoms[0].type = core::AtomType::Fire; });
    check([](auto& p) { p.outputs[0].atoms[0].type = core::AtomType::Repeat; });
    check([](auto& p) { p.inputs[0].atoms.push_back(p.inputs[0].atoms[0]); });
    check([](auto& p) { p.outputs.push_back(p.outputs[0]); });
    check([](auto& p) { p.outputMultiplier = 2; });
    check([](auto& p) { p.version = 2; });
    for (int bit : {0, 22, 23, 25})
        check([&](auto& p) { p.permissionBits &= ~(1ULL << bit); });
    EXPECT_THROW((void)solvers::CompileProductionTransport(io::BinaryParser::ParsePuzzleFile(
                     Root() / "asset/puzzle/edge-cases/conduit-bug.puzzle")),
                 std::invalid_argument);
}

TEST(ProductionTransport, RejectedRigidPosesContinueBoundedSearch) {
    auto p = Puzzle();
    p.productionInfo->chambers = {{{-12, 0}, "Large"}, {{12, 0}, "Large"}};
    auto& conduit = p.productionInfo->conduits[0];
    conduit.positionA = {-12, 0};
    conduit.positionB = {12, 0};
    conduit.path = {{0, 0}, {1, 0}, {0, 1}};
    p.inputs = {{{{core::AtomType::Salt, {0, 0}},
                  {core::AtomType::Salt, {1, 0}},
                  {core::AtomType::Salt, {0, 1}}},
                 {{1, {0, 0}, {1, 0}}, {1, {0, 0}, {0, 1}}}}};
    p.outputs = p.inputs;
    int attempts = 0;
    auto result = solvers::CompileAcceptedProductionTransport(
        p, [&](const auto&) { return ++attempts == 2; });
    ASSERT_TRUE(result);
    EXPECT_EQ(attempts, 2);
    attempts = 0;
    EXPECT_FALSE(solvers::CompileAcceptedProductionTransport(p, [&](const auto&) {
        ++attempts;
        return false;
    }));
    EXPECT_GT(attempts, 1);
    EXPECT_LE(attempts, 64);
    EXPECT_THROW(
        (void)solvers::CompileAcceptedProductionTransport(
            p, [](const auto&) -> bool { throw std::invalid_argument("acceptance exception"); }),
        std::invalid_argument);
#ifdef HAS_LIBVERIFY
    Files files;
    files.WritePuzzle(p);
    attempts = 0;
    auto verified = solvers::CompileAcceptedProductionTransport(p, [&](const auto& candidate) {
        ++attempts;
        io::BinaryWriter::WriteSolutionFile(files.solution, candidate);
        const bool rotated =
            std::any_of(candidate.objects.begin(), candidate.objects.end(), [](const auto& object) {
                return object.typeName == "arm1" && object.bodyData.at(4) == 0 &&
                       object.bodyData.at(1) != 0;
            });
        return io::VerifySolutionFiles(files.puzzle, files.solution, 10000, 2000).Passed() &&
               rotated;
    });
    ASSERT_TRUE(verified);
    io::BinaryWriter::WriteSolutionFile(files.solution, *verified);
    auto report = io::VerifySolutionFiles(files.puzzle, files.solution, 10000, 2000);
    ASSERT_TRUE(report.Passed()) << report.detail;
    verified->metrics = report.metrics;
    io::BinaryWriter::WriteSolutionFile(files.solution, *verified);
    EXPECT_TRUE(io::VerifySolutionFiles(files.puzzle, files.solution, 10000, 2000).Passed());
#endif
}

TEST(ProductionTransport, RejectedSmallMoleculePosesContinueBoundedSearch) {
    for (bool pair : {false, true}) {
        auto p = Puzzle();
        p.productionInfo->chambers = {{{-12, 0}, "Large"}, {{12, 0}, "Large"}};
        auto& conduit = p.productionInfo->conduits[0];
        conduit.positionA = {-12, 0};
        conduit.positionB = {12, 0};
        conduit.path = {{0, 0}};
        p.inputs = {{{{core::AtomType::Salt, {0, 0}}}, {}}};
        if (pair) {
            conduit.path.push_back({1, 0});
            p.inputs[0].atoms.push_back({core::AtomType::Air, {1, 0}});
            p.inputs[0].bonds.push_back({14, {}, {1, 0}});
        }
        p.outputs = p.inputs;
        int attempts = 0;
        EXPECT_FALSE(solvers::CompileAcceptedProductionTransport(p, [&](const auto&) {
            ++attempts;
            return false;
        }));
        if (pair) {
            EXPECT_GE(attempts, 6);
            EXPECT_LE(attempts, 64);
        } else {
            EXPECT_EQ(attempts, 6);
        }
        EXPECT_THROW(
            (void)solvers::CompileAcceptedProductionTransport(
                p,
                [](const auto&) -> bool { throw std::invalid_argument("acceptance exception"); }),
            std::invalid_argument);
#ifdef HAS_LIBVERIFY
        Files files;
        files.WritePuzzle(p);
        auto machine = solvers::CompileAcceptedProductionTransport(p, [&](const auto& candidate) {
            io::BinaryWriter::WriteSolutionFile(files.solution, candidate);
            const bool rotated = std::any_of(
                candidate.objects.begin(), candidate.objects.end(), [](const auto& object) {
                    return object.typeName == "arm1" && object.bodyData.at(4) == 0 &&
                           object.bodyData.at(1) != 0;
                });
            return io::VerifySolutionFiles(files.puzzle, files.solution, 10000, 2000).Passed() &&
                   rotated;
        });
        ASSERT_TRUE(machine) << "pair=" << pair;
        io::BinaryWriter::WriteSolutionFile(files.solution, *machine);
        const auto report = io::VerifySolutionFiles(files.puzzle, files.solution, 10000, 2000);
        ASSERT_TRUE(report.Passed()) << report.detail;
        machine->metrics = report.metrics;
        io::BinaryWriter::WriteSolutionFile(files.solution, *machine);
        EXPECT_TRUE(io::VerifySolutionFiles(files.puzzle, files.solution, 10000, 2000).Passed());
#endif
    }
}

TEST(ProductionTransport, PlansRigidConnectedMoleculesWithoutShapeTemplates) {
    for (int mask : {1, 2, 4, 6, 8, 10, 12, 14})
        for (int turns = 0; turns < 6; ++turns)
            for (bool reverseInput : {false, true})
                for (const auto& shape :
                     std::vector<std::vector<core::HexCoord>>{{{0, 0}, {1, 0}, {2, 0}},
                                                              {{0, 0}, {1, 0}, {0, 1}},
                                                              {{0, 0}, {1, 0}, {0, 1}, {1, 1}}}) {
                    auto p = Puzzle();
                    p.productionInfo->chambers = {{{-12, 0}, "Large"}, {{12, 0}, "Large"}};
                    auto& conduit = p.productionInfo->conduits[0];
                    conduit.positionA = {-12, 0};
                    conduit.positionB = {12, 0};
                    conduit.path = shape;
                    io::Molecule molecule;
                    for (auto cell : shape)
                        molecule.atoms.push_back({core::AtomType::Salt, cell});
                    for (std::size_t i = 1; i < shape.size(); ++i)
                        for (std::size_t j = 0; j < i; ++j)
                            if (core::Distance(shape[i], shape[j]) == 1) {
                                molecule.bonds.push_back(
                                    {static_cast<std::uint8_t>(mask), shape[i], shape[j]});
                                break;
                            }
                    p.inputs = {molecule};
                    p.outputs = {molecule};
                    auto transformed = [&](core::HexCoord cell) {
                        for (int i = 0; i < turns; ++i)
                            cell = core::RotateClockwise(cell);
                        return core::Add(cell, {3, -2});
                    };
                    for (auto& atom : p.inputs[0].atoms)
                        atom.position = transformed(atom.position);
                    for (auto& bond : p.inputs[0].bonds) {
                        bond.from = transformed(bond.from);
                        bond.to = transformed(bond.to);
                    }
                    if (reverseInput)
                        std::reverse(p.inputs[0].atoms.begin(), p.inputs[0].atoms.end());
                    SCOPED_TRACE(::testing::Message()
                                 << "mask=" << mask << " turns=" << turns
                                 << " reverse=" << reverseInput << " atoms=" << shape.size());
                    EXPECT_NO_THROW((void)solvers::CompileProductionTransport(p));
                    std::reverse(p.outputs[0].atoms.begin(), p.outputs[0].atoms.end());
                    EXPECT_NO_THROW((void)solvers::CompileProductionTransport(p));
#ifdef HAS_LIBVERIFY
                    Files files;
                    files.WritePuzzle(p);
                    auto solution = solvers::CompileProductionTransport(p);
                    io::BinaryWriter::WriteSolutionFile(files.solution, solution);
                    auto report =
                        io::VerifySolutionFiles(files.puzzle, files.solution, 10000, 2000);
                    ASSERT_TRUE(report.Passed()) << report.detail;
                    solution.metrics = report.metrics;
                    io::BinaryWriter::WriteSolutionFile(files.solution, solution);
                    EXPECT_TRUE(io::VerifySolutionFiles(files.puzzle, files.solution, 10000, 2000)
                                    .Passed());
#endif
                    auto malformed = p;
                    auto duplicate = malformed.inputs[0].bonds.front();
                    malformed.inputs[0].bonds.push_back(duplicate);
                    malformed.outputs[0].bonds.push_back(duplicate);
                    EXPECT_THROW((void)solvers::PlanProductionTransport(malformed),
                                 std::invalid_argument);
                    std::swap(malformed.inputs[0].bonds.back().from,
                              malformed.inputs[0].bonds.back().to);
                    EXPECT_THROW((void)solvers::PlanProductionTransport(malformed),
                                 std::invalid_argument);
                    malformed = p;
                    malformed.productionInfo->conduits[0].path.back() =
                        malformed.productionInfo->conduits[0].path.front();
                    EXPECT_THROW((void)solvers::PlanProductionTransport(malformed),
                                 std::invalid_argument);
                    malformed = p;
                    for (auto& cell : malformed.productionInfo->conduits[0].path)
                        cell = core::Add(cell, {20, 20});
                    EXPECT_THROW((void)solvers::PlanProductionTransport(malformed),
                                 std::invalid_argument);
                    p.outputs[0].bonds.pop_back();
                    EXPECT_THROW((void)solvers::CompileProductionTransport(p),
                                 std::invalid_argument);
                }
}

TEST(ProductionTransport, PlansSixChamberGeometriesAndRejectsWallIntersections) {
    const std::vector<std::string> types{"Small",  "SmallWide",  "SmallWider",
                                         "Medium", "MediumWide", "Large"};
    const int interiors[]{7, 10, 13, 19, 24, 37};
    const int walls[]{12, 14, 16, 18, 20, 24};
    for (std::size_t i = 0; i < types.size(); ++i) {
        auto p = Puzzle();
        p.productionInfo->chambers = {{{-12, 0}, types[i]}, {{12, 0}, "Small"}};
        p.productionInfo->conduits[0].positionA = {-12, 0};
        p.productionInfo->conduits[0].positionB = {12, 0};
        const auto layouts = solvers::PlanProductionChambers(p);
        ASSERT_EQ(layouts.size(), 2U);
        EXPECT_EQ(layouts[0].interior.size(), interiors[i]);
        EXPECT_EQ(layouts[0].walls.size(), walls[i]);
        EXPECT_NO_THROW((void)solvers::CompileProductionTransport(p));
        p.productionInfo->chambers[1].position = {-10, 0};
        EXPECT_THROW((void)solvers::PlanProductionChambers(p), std::invalid_argument);
    }
}

TEST(ProductionTransport, RejectsChangedOrMalformedBondedPairs) {
    const auto original =
        io::BinaryParser::ParsePuzzleFile(Root() / "asset/puzzle/easy/double-conduit.puzzle");
    EXPECT_NO_THROW((void)solvers::CompileProductionTransport(original));
    auto check = [&](auto mutate) {
        auto p = original;
        mutate(p);
        EXPECT_THROW((void)solvers::PlanProductionTransport(p), std::invalid_argument);
        EXPECT_THROW((void)solvers::CompileProductionTransport(p), std::invalid_argument);
    };
    check([](auto& p) { p.outputs[0].atoms[0].type = core::AtomType::Repeat; });
    check([](auto& p) { p.outputs[0].atoms[0].type = p.outputs[0].atoms[1].type; });
    check([](auto& p) { p.outputs[0].bonds[0].bits = 2; });
    check([](auto& p) { p.inputs[0].bonds.clear(); });
    check([](auto& p) { p.outputs[0].bonds.push_back(p.outputs[0].bonds[0]); });
    check([](auto& p) { p.inputs[0].bonds[0].from = {8, 8}; });
    check([](auto& p) { p.inputs[0].atoms[1].position = p.inputs[0].atoms[0].position; });
    check([](auto& p) { p.inputs[0].atoms[1].position = {3, 0}; });
    check([](auto& p) { p.productionInfo->conduits[0].path[1] = {0, 0}; });
    check([](auto& p) { p.productionInfo->conduits[0].path[1] = {2, 0}; });
    check([](auto& p) {
        p.productionInfo->conduits[0].path[1] = {std::numeric_limits<int>::max(), 1};
    });
    check([](auto& p) { p.productionInfo->conduits[0].path[1] = {0, -1}; });
    for (int bits : {0, 3, 15, 16})
        check([&](auto& p) {
            p.inputs[0].bonds[0].bits = bits;
            p.outputs[0].bonds[0].bits = bits;
        });
}

TEST(ProductionTransport, PlansExplicitChamberAndConduitContracts) {
    for (const auto* fixture : {"easy-conduit.puzzle", "double-conduit.puzzle"}) {
        const auto p = io::BinaryParser::ParsePuzzleFile(Root() / "asset/puzzle/easy" / fixture);
        const auto plan = solvers::PlanProductionTransport(p);
        const auto machine = solvers::CompileProductionTransport(p);
        const auto& handoff = plan.handoff;
        EXPECT_NE(plan.source.chamberIndex, plan.destination.chamberIndex);
        for (const auto* chamber : {&plan.source, &plan.destination}) {
            ASSERT_EQ(chamber->interior.size(), 7);
            ASSERT_EQ(chamber->walls.size(), 12);
            EXPECT_EQ(chamber->center, p.productionInfo->chambers[chamber->chamberIndex].position);
            for (auto cell : chamber->interior)
                EXPECT_LE(core::Distance(cell, chamber->center), 1);
            for (auto cell : chamber->walls)
                EXPECT_EQ(core::Distance(cell, chamber->center), 2);
        }
        EXPECT_EQ(handoff.localShape, p.productionInfo->conduits[handoff.conduitIndex].path);
        EXPECT_EQ(handoff.payload.atoms.size(), p.inputs[0].atoms.size());
        EXPECT_EQ(handoff.payload.bonds.size(), p.inputs[0].bonds.size());
        if (!handoff.payload.bonds.empty()) {
            const auto& bond = handoff.payload.bonds[0];
            EXPECT_EQ(bond.bits, p.inputs[0].bonds[0].bits);
            EXPECT_TRUE((bond.from == handoff.payload.atoms[0].position &&
                         bond.to == handoff.payload.atoms[1].position) ||
                        (bond.to == handoff.payload.atoms[0].position &&
                         bond.from == handoff.payload.atoms[1].position));
        }
        EXPECT_EQ(handoff.releaseCycle, 2);
        EXPECT_EQ(handoff.pickupCycle, 4);
        EXPECT_EQ(handoff.period, 8);
        for (auto cell : handoff.localShape) {
            for (int i = 0; i < handoff.rotation; ++i)
                cell = core::RotateClockwise(cell);
            EXPECT_TRUE(std::any_of(handoff.payload.atoms.begin(), handoff.payload.atoms.end(),
                                    [&](const auto& atom) { return atom.position == cell; }));
            EXPECT_LE(core::Distance(core::Add(handoff.sourcePosition, cell), plan.source.center),
                      1);
            EXPECT_LE(core::Distance(core::Add(handoff.destinationPosition, cell),
                                     plan.destination.center),
                      1);
        }
        EXPECT_EQ(machine.objects[0].position, handoff.sourcePosition);
        EXPECT_EQ(machine.objects[1].position, handoff.destinationPosition);
        EXPECT_EQ(machine.objects[0].bodyData[5], handoff.conduitId);
        EXPECT_EQ(machine.objects[1].bodyData[5], handoff.conduitId);
        EXPECT_EQ(machine.objects[2].position, plan.inputPosition);
        EXPECT_EQ(machine.objects[3].position, plan.outputPosition);
        EXPECT_EQ(machine.objects[4].instructions[2].parameter, handoff.releaseCycle);
        EXPECT_EQ(machine.objects[5].instructions[0].parameter, handoff.pickupCycle);
    }
}

TEST(ProductionTransport, AssemblyUsesAcceptanceAndStopsOnProductionRejection) {
    const auto p = Puzzle();
    int calls = 0;
    EXPECT_THROW((void)solvers::AssemblySolver{}.SolveAccepted(p,
                                                               [&](const auto&) {
                                                                   ++calls;
                                                                   return false;
                                                               }),
                 std::invalid_argument);
    EXPECT_EQ(calls, 12);
    auto unsupported = p;
    unsupported.productionInfo->isolation = true;
    unsupported.version = 2;
    calls = 0;
    EXPECT_THROW((void)solvers::AssemblySolver{}.SolveAccepted(unsupported,
                                                               [&](const auto&) {
                                                                   ++calls;
                                                                   return true;
                                                               }),
                 std::invalid_argument);
    EXPECT_EQ(calls, 0);
    EXPECT_THROW((void)solvers::AssemblySolver{}.SolveAccepted(
                     p,
                     [](const auto&) -> bool {
                         throw std::runtime_error("acceptance infrastructure failure");
                     }),
                 std::runtime_error);
}

#ifdef HAS_LIBVERIFY
TEST(ProductionTransport, BondedPairsCompleteSixProductsAcrossRotationsAndReordering) {
    Files files;
    const auto fixture = Root() / "asset/puzzle/easy/double-conduit.puzzle";
    for (int variant = 0; variant < 14; ++variant) {
        SCOPED_TRACE(variant);
        auto bytes = io::BinaryParser::ReadAllBytes(fixture);
        const auto original = io::BinaryParser::ParsePuzzle(bytes);
        const auto firstMolecule = 4 + 1 + original.name.size() + 16 + 4;
        auto transform = [&](std::size_t offset, int turns) {
            core::HexCoord cell{
                static_cast<std::int8_t>(std::to_integer<int>(bytes.at(offset))),
                static_cast<std::int8_t>(std::to_integer<int>(bytes.at(offset + 1)))};
            for (int i = 0; i < turns; ++i)
                cell = core::RotateClockwise(cell);
            cell = core::Add(cell, {2, -3});
            bytes.at(offset) = std::byte(cell.q & 255);
            bytes.at(offset + 1) = std::byte(cell.r & 255);
        };
        for (int molecule = 0; molecule < 2; ++molecule) {
            const auto start = firstMolecule + molecule * 23;
            for (auto offset : {5U, 8U, 15U, 17U})
                transform(start + offset, variant + molecule);
        }
        const auto output = firstMolecule + 23;
        if (variant >= 6) {
            for (const auto molecule : {firstMolecule, output}) {
                bytes.at(molecule + 7) = bytes.at(molecule + 4);
                if (variant < 13) {
                    constexpr int triplexBits[] = {2, 4, 6, 8, 10, 12, 14};
                    bytes.at(molecule + 14) = std::byte(triplexBits[variant - 6]);
                }
            }
        }
        for (int i = 0; i < 3; ++i)
            std::swap(bytes[output + 4 + i], bytes[output + 7 + i]);
        constexpr std::size_t trailer = 4 + 4 + 2 * (2 + 1 + 5) + 4 + (4 + 4 + 4) + 4;
        const auto start = bytes.size() - trailer;
        bytes.at(start + 3) = std::byte(variant != 0);
        if (variant != 0) {
            auto cell = core::HexCoord{0, 1};
            for (int i = 0; i < variant; ++i)
                cell = core::RotateClockwise(cell);
            bytes.at(start + 38) = std::byte(cell.q & 255);
            bytes.at(start + 39) = std::byte(cell.r & 255);
            for (int i = 0; i < 2; ++i) {
                bytes.at(start + 28 + i) = bytes.at(start + 8 + i);
                bytes.at(start + 30 + i) = bytes.at(start + 16 + i);
            }
        }
        if (variant % 2) {
            for (int i = 0; i < 2; ++i)
                std::swap(bytes[start + 28 + i], bytes[start + 30 + i]);
        }
        if (variant == 5) {
            for (auto offset : {8U, 16U, 28U, 30U})
                transform(start + offset, 0);
        }
        {
            std::ofstream stream(files.puzzle, std::ios::binary);
            stream.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        }
        const auto p = io::BinaryParser::ParsePuzzle(bytes);
        const auto machine = solvers::AssemblySolver{}.SolveAccepted(p, [&](const auto& candidate) {
            io::BinaryWriter::WriteSolutionFile(files.solution, candidate);
            return io::VerifySolutionFiles(files.puzzle, files.solution, 200).Passed();
        });
        io::BinaryWriter::WriteSolutionFile(files.solution, machine);
        const auto report = io::VerifySolutionFiles(files.puzzle, files.solution, 200);
        ASSERT_TRUE(report.Passed()) << report.detail;
        EXPECT_EQ(report.metrics.cycles, 47);
        EXPECT_FALSE(io::VerifySolutionFiles(files.puzzle, files.solution, 40).Passed());
        auto broken = machine;
        broken.objects[1].bodyData[1] = (broken.objects[1].bodyData[1] + 1) % 6;
        io::BinaryWriter::WriteSolutionFile(files.solution, broken);
        EXPECT_FALSE(io::VerifySolutionFiles(files.puzzle, files.solution, 200).Passed());
    }
}

TEST(ProductionTransport, IndependentlyCalcifiesTransportedElements) {
    Files files;
    for (auto element : {core::AtomType::Air, core::AtomType::Earth, core::AtomType::Fire,
                         core::AtomType::Water}) {
        SCOPED_TRACE(static_cast<int>(element));
        auto bytes =
            io::BinaryParser::ReadAllBytes(Root() / "asset/puzzle/easy/easy-conduit.puzzle");
        const auto nameLength = std::to_integer<unsigned>(bytes.at(4));
        const auto permissions = 5 + nameLength + 8;
        bytes.at(permissions + 1) |= std::byte{0x10};
        const auto inputAtom = permissions + 8 + 4 + 4;
        bytes.at(inputAtom) = std::byte(static_cast<unsigned char>(element));
        {
            std::ofstream stream(files.puzzle, std::ios::binary);
            stream.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        }
        const auto p = io::BinaryParser::ParsePuzzle(bytes);
        ASSERT_EQ(p.inputs[0].atoms[0].type, element);
        ASSERT_EQ(p.outputs[0].atoms[0].type, core::AtomType::Salt);
        auto machine = solvers::CompileProductionTransport(p);
        io::BinaryWriter::WriteSolutionFile(files.solution, machine);
        const auto report = io::VerifySolutionFiles(files.puzzle, files.solution, 200);
        ASSERT_TRUE(report.Passed()) << report.detail;
        machine.metrics = report.metrics;
        io::BinaryWriter::WriteSolutionFile(files.solution, machine);
        EXPECT_TRUE(io::VerifySolutionFiles(files.puzzle, files.solution, 200).Passed());
        machine.objects.erase(std::remove_if(machine.objects.begin(), machine.objects.end(),
                                             [](const auto& object) {
                                                 return object.typeName == "glyph-calcification";
                                             }),
                              machine.objects.end());
        io::BinaryWriter::WriteSolutionFile(files.solution, machine);
        EXPECT_FALSE(io::VerifySolutionFiles(files.puzzle, files.solution, 200).Passed());
        auto forbidden = p;
        forbidden.permissionBits &= ~(1ULL << 12);
        EXPECT_THROW((void)solvers::CompileProductionTransport(forbidden), std::invalid_argument);
    }
}

TEST(ProductionTransport, IndependentlyVerifiesMixedChamberGeometries) {
    Files files;
    for (const std::string type :
         {"Small", "SmallWide", "SmallWider", "Medium", "MediumWide", "Large"}) {
        SCOPED_TRACE(type);
        auto bytes =
            io::BinaryParser::ReadAllBytes(Root() / "asset/puzzle/easy/easy-conduit.puzzle");
        constexpr std::size_t trailer = 4 + 4 + 2 * (2 + 1 + 5) + 4 + (4 + 4 + 2) + 4;
        const auto start = bytes.size() - trailer;
        bytes.at(start + 8) = std::byte(244);
        bytes.at(start + 16) = std::byte(12);
        bytes.at(start + 28) = std::byte(244);
        bytes.at(start + 30) = std::byte(12);
        bytes.erase(bytes.begin() + start + 11, bytes.begin() + start + 16);
        bytes.insert(bytes.begin() + start + 11, type.size(), std::byte{});
        bytes.at(start + 10) = std::byte(type.size());
        for (std::size_t i = 0; i < type.size(); ++i)
            bytes.at(start + 11 + i) = std::byte(type[i]);
        {
            std::ofstream stream(files.puzzle, std::ios::binary);
            stream.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        }
        const auto p = io::BinaryParser::ParsePuzzle(bytes);
        const auto machine = solvers::CompileProductionTransport(p);
        io::BinaryWriter::WriteSolutionFile(files.solution, machine);
        const auto report = io::VerifySolutionFiles(files.puzzle, files.solution, 200);
        ASSERT_TRUE(report.Passed()) << report.detail;
        auto broken = machine;
        broken.objects.back().position = {0, 0};
        io::BinaryWriter::WriteSolutionFile(files.solution, broken);
        EXPECT_FALSE(io::VerifySolutionFiles(files.puzzle, files.solution, 200).Passed());
    }
}

TEST(ProductionTransport, IndependentlyVerifiesReorderedPairConduitCells) {
    auto p = Puzzle();
    p.productionInfo->chambers = {{{-12, 0}, "Large"}, {{12, 0}, "Large"}};
    auto& conduit = p.productionInfo->conduits[0];
    conduit.positionA = {-12, 0};
    conduit.positionB = {12, 0};
    conduit.path = {{1, 0}, {0, 0}};
    p.inputs = {
        {{{core::AtomType::Salt, {0, 0}}, {core::AtomType::Air, {1, 0}}}, {{1, {0, 0}, {1, 0}}}}};
    p.outputs = p.inputs;
    Files files;
    files.WritePuzzle(p);
    auto machine = solvers::CompileAcceptedProductionTransport(p, [&](const auto& candidate) {
        io::BinaryWriter::WriteSolutionFile(files.solution, candidate);
        return io::VerifySolutionFiles(files.puzzle, files.solution, 2000).Passed();
    });
    ASSERT_TRUE(machine);
    io::BinaryWriter::WriteSolutionFile(files.solution, *machine);
    const auto report = io::VerifySolutionFiles(files.puzzle, files.solution, 2000);
    ASSERT_TRUE(report.Passed()) << report.detail;
    machine->metrics = report.metrics;
    io::BinaryWriter::WriteSolutionFile(files.solution, *machine);
    EXPECT_TRUE(io::VerifySolutionFiles(files.puzzle, files.solution, 2000).Passed());
}

TEST(ProductionTransport, IndependentlyCompletesSixProductsIncludingIsolation) {
    Files files;
    for (int variant = 0; variant < 4; ++variant) {
        SCOPED_TRACE(variant);
        const bool isolation = variant != 0;
        auto bytes =
            io::BinaryParser::ReadAllBytes(Root() / "asset/puzzle/easy/easy-conduit.puzzle");
        // The production trailer starts after multiplier. Locate its length from
        // the parsed fixed fixture: flags, two Small chambers, one conduit, no vials.
        constexpr std::size_t trailer = 4 + 4 + 2 * (2 + 1 + 5) + 4 + (4 + 4 + 2) + 4;
        bytes.at(bytes.size() - trailer + 3) = std::byte(isolation);
        const auto start = bytes.size() - trailer;
        if (variant == 2) {
            // Translate the fixture's two chambers and both prescribed endpoints.
            for (auto offset : {8U, 16U, 28U, 30U}) {
                bytes.at(start + offset) =
                    std::byte(std::to_integer<int>(bytes[start + offset]) + 7);
                bytes.at(start + offset + 1) =
                    std::byte(std::to_integer<int>(bytes[start + offset + 1]) + 5);
            }
        } else if (variant == 3) {
            for (int i = 0; i < 2; ++i)
                std::swap(bytes[start + 28 + i], bytes[start + 30 + i]);
        }
        {
            std::ofstream stream(files.puzzle, std::ios::binary);
            stream.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        }
        const auto p = io::BinaryParser::ParsePuzzle(bytes);
        ASSERT_EQ(p.productionInfo->isolation, isolation);
        const auto machine = solvers::AssemblySolver{}.SolveAccepted(p, [&](const auto& candidate) {
            io::BinaryWriter::WriteSolutionFile(files.solution, candidate);
            return io::VerifySolutionFiles(files.puzzle, files.solution, 200).Passed();
        });
        io::BinaryWriter::WriteSolutionFile(files.solution, machine);
        const auto report = io::VerifySolutionFiles(files.puzzle, files.solution, 200);
        ASSERT_TRUE(report.Passed()) << report.detail;
        EXPECT_GT(report.metrics.cycles, 40);
        EXPECT_FALSE(io::VerifySolutionFiles(files.puzzle, files.solution, 40).Passed());
        auto broken = machine;
        broken.objects.erase(broken.objects.begin());
        io::BinaryWriter::WriteSolutionFile(files.solution, broken);
        EXPECT_FALSE(io::VerifySolutionFiles(files.puzzle, files.solution, 200).Passed());
        broken = machine;
        broken.objects.back().position = {0, 0};
        io::BinaryWriter::WriteSolutionFile(files.solution, broken);
        EXPECT_FALSE(io::VerifySolutionFiles(files.puzzle, files.solution, 200).Passed());
    }
}
#endif

TEST(ProductionTransport, MolecularCoproductsPreserveInternalMasksAcrossConduit) {
    auto p = Puzzle();
    p.permissionBits |= (1ULL << 9) | (1ULL << 12);
    p.productionInfo->chambers = {{{-12, 0}, "Large"}, {{12, 0}, "Large"}};
    auto& pipe = p.productionInfo->conduits[0];
    pipe.positionA = {-12, 0};
    pipe.positionB = {12, 0};
    pipe.path = {{0, 0}, {1, 0}, {2, 0}};
    p.inputs = {{{{core::AtomType::Fire, {0, 0}},
                  {core::AtomType::Fire, {1, 0}},
                  {core::AtomType::Air, {2, 0}}},
                 {{14, {0, 0}, {1, 0}}, {1, {1, 0}, {2, 0}}}}};
    p.outputs = {{{{core::AtomType::Salt, {-2, 1}}}, {}},
                 {{{core::AtomType::Fire, {2, -1}}, {core::AtomType::Fire, {3, -1}}},
                  {{14, {2, -1}, {3, -1}}}}};
    std::size_t attempts = 0;
    EXPECT_FALSE(solvers::CompileAcceptedProductionCoproducts(p, [&](const auto&) {
        ++attempts;
        return false;
    }));
    EXPECT_GT(attempts, 0u);
    EXPECT_LE(attempts, 64u);
    auto malformed = [&](auto mutate) {
        auto invalid = p;
        mutate(invalid.outputs[1]);
        EXPECT_THROW((void)solvers::CompileAcceptedProductionCoproducts(
                         invalid, [](const auto&) { return true; }),
                     std::invalid_argument);
    };
    malformed([](auto& output) { output.bonds.push_back({1, {20, 20}, {21, 20}}); });
    malformed([](auto& output) { output.bonds.push_back(output.bonds.front()); });
    malformed([](auto& output) { output.atoms[1].position = output.atoms[0].position; });
    malformed([](auto& output) { output.bonds[0].bits = 3; });
    for (const auto permission : {9, 12}) {
        auto forbidden = p;
        forbidden.permissionBits &= ~(1ULL << permission);
        EXPECT_FALSE(solvers::CompileAcceptedProductionCoproducts(
            forbidden, [](const auto&) { return true; }));
    }
    EXPECT_THROW((void)solvers::CompileAcceptedProductionCoproducts(
                     p,
                     [](const auto&) -> bool {
                         throw std::invalid_argument("independent acceptance infrastructure");
                     }),
                 std::invalid_argument);
    auto incompatible = p;
    incompatible.outputs[1].bonds[0].bits = 1;
    incompatible.permissionBits &= ~(1ULL << 8);
    EXPECT_FALSE(solvers::CompileAcceptedProductionCoproducts(incompatible,
                                                              [](const auto&) { return true; }));
#ifdef HAS_LIBVERIFY
    Files files;
    for (const int mask : {1, 2, 6, 14})
        for (const bool reverse : {false, true}) {
            auto variant = p;
            variant.inputs[0].bonds[0].bits = mask;
            variant.outputs[1].bonds[0].bits = mask;
            if (reverse) {
                std::reverse(variant.inputs[0].atoms.begin(), variant.inputs[0].atoms.end());
                std::reverse(variant.outputs[1].atoms.begin(), variant.outputs[1].atoms.end());
            }
            files.WritePuzzle(variant);
            std::string failure;
            std::size_t rejectedShortArms = 0;
            auto result =
                solvers::CompileAcceptedProductionCoproducts(variant, [&](const auto& candidate) {
                    if (std::any_of(candidate.objects.begin(), candidate.objects.end(),
                                    [](const auto& object) {
                                        return object.typeName == "arm1" &&
                                               object.bodyData.at(0) == 1;
                                    })) {
                        ++rejectedShortArms;
                        return false;
                    }
                    io::BinaryWriter::WriteSolutionFile(files.solution, candidate);
                    auto report =
                        io::VerifySolutionFiles(files.puzzle, files.solution, 10000, 2000);
                    failure = report.detail;
                    return report.Passed();
                });
            EXPECT_GT(rejectedShortArms, 0u);
            ASSERT_TRUE(result) << "mask=" << mask << " reverse=" << reverse << ": " << failure;
            io::BinaryWriter::WriteSolutionFile(files.solution, *result);
            auto report = io::VerifySolutionFiles(files.puzzle, files.solution, 10000, 2000);
            ASSERT_TRUE(report.Passed()) << report.detail;
            result->metrics = report.metrics;
            io::BinaryWriter::WriteSolutionFile(files.solution, *result);
            EXPECT_TRUE(
                io::VerifySolutionFiles(files.puzzle, files.solution, 10000, 2000).Passed());
        }
#endif
}

TEST(ProductionTransport, CoproductsAddDestinationBondsAfterConduit) {
    auto p = Puzzle();
    p.permissionBits |= (1ULL << 8) | (1ULL << 9);
    p.productionInfo->chambers = {{{-12, 0}, "Large"}, {{12, 0}, "Large"}};
    auto& pipe = p.productionInfo->conduits[0];
    pipe.positionA = {-12, 0};
    pipe.positionB = {12, 0};
    pipe.path = {{0, 0}, {1, 0}, {0, 1}};
    p.inputs = {{{{core::AtomType::Salt, {0, 0}},
                  {core::AtomType::Air, {1, 0}},
                  {core::AtomType::Fire, {0, 1}}},
                 {{1, {0, 0}, {1, 0}}, {1, {1, 0}, {0, 1}}}}};
    p.outputs = {
        {{{core::AtomType::Air, {0, 0}}}, {}},
        {{{core::AtomType::Salt, {0, 0}}, {core::AtomType::Fire, {0, 1}}}, {{1, {0, 0}, {0, 1}}}}};
    auto forbidden = p;
    forbidden.permissionBits &= ~(1ULL << 8);
    EXPECT_FALSE(
        solvers::CompileAcceptedProductionCoproducts(forbidden, [](const auto&) { return true; }));
#ifdef HAS_LIBVERIFY
    Files files;
    for (const bool reverse : {false, true}) {
        auto variant = p;
        if (reverse) {
            std::reverse(variant.inputs[0].atoms.begin(), variant.inputs[0].atoms.end());
            std::reverse(variant.outputs[1].atoms.begin(), variant.outputs[1].atoms.end());
        }
        files.WritePuzzle(variant);
        std::string failure;
        auto result =
            solvers::CompileAcceptedProductionCoproducts(variant, [&](const auto& candidate) {
                io::BinaryWriter::WriteSolutionFile(files.solution, candidate);
                const auto report =
                    io::VerifySolutionFiles(files.puzzle, files.solution, 10000, 2000);
                failure = report.detail;
                return report.Passed();
            });
        ASSERT_TRUE(result) << failure;
        EXPECT_TRUE(std::any_of(result->objects.begin(), result->objects.end(),
                                [](const auto& object) { return object.typeName == "bonder"; }));
        io::BinaryWriter::WriteSolutionFile(files.solution, *result);
        const auto report = io::VerifySolutionFiles(files.puzzle, files.solution, 10000, 2000);
        ASSERT_TRUE(report.Passed()) << report.detail;
        result->metrics = report.metrics;
        io::BinaryWriter::WriteSolutionFile(files.solution, *result);
        EXPECT_TRUE(io::VerifySolutionFiles(files.puzzle, files.solution, 10000, 2000).Passed());
    }
#endif
}

TEST(ProductionTransport, CoproductsCreateColorMasksAtDestination) {
    for (const bool occupied : {false, true})
        for (const int mask : {2, 4, 8, 6, 10, 12, 14}) {
            SCOPED_TRACE(occupied);
            SCOPED_TRACE(mask);
            auto p = Puzzle();
            p.permissionBits |= (1ULL << 9) | (1ULL << 11);
            p.productionInfo->chambers = {{{-12, 0}, "Large"}, {{12, 0}, "Large"}};
            auto& pipe = p.productionInfo->conduits[0];
            pipe.positionA = {-12, 0};
            pipe.positionB = {12, 0};
            pipe.path = {{0, 0}, {1, 0}, {0, 1}};
            p.inputs = {{{{core::AtomType::Fire, {0, 0}},
                          {core::AtomType::Air, {1, 0}},
                          {core::AtomType::Fire, {0, 1}}},
                         {{1, {0, 0}, {1, 0}}, {1, {1, 0}, {0, 1}}}}};
            p.outputs = {{{{core::AtomType::Air, {0, 0}}}, {}},
                         {{{core::AtomType::Fire, {0, 0}}, {core::AtomType::Fire, {0, 1}}},
                          {{static_cast<std::uint8_t>(mask), {0, 0}, {0, 1}}}}};
            if (occupied) {
                pipe.path.push_back({-1, 1});
                p.inputs[0].atoms.push_back({core::AtomType::Water, {-1, 1}});
                p.inputs[0].bonds.push_back({1, {0, 0}, {-1, 1}});
                p.outputs.push_back({{{core::AtomType::Water, {0, 0}}}, {}});
            }
            auto forbidden = p;
            forbidden.permissionBits &= ~(1ULL << 11);
            EXPECT_FALSE(solvers::CompileAcceptedProductionCoproducts(
                forbidden, [](const auto&) { return true; }));
#ifdef HAS_LIBVERIFY
            Files files;
            for (const bool reverse : {false, true}) {
                auto variant = p;
                if (reverse) {
                    std::reverse(variant.inputs[0].atoms.begin(), variant.inputs[0].atoms.end());
                    std::reverse(variant.outputs[1].atoms.begin(), variant.outputs[1].atoms.end());
                }
                files.WritePuzzle(variant);
                std::string failure;
                auto result = solvers::CompileAcceptedProductionCoproducts(
                    variant, [&](const auto& candidate) {
                        io::BinaryWriter::WriteSolutionFile(files.solution, candidate);
                        const auto report =
                            io::VerifySolutionFiles(files.puzzle, files.solution, 10000, 2000);
                        failure = report.detail;
                        return report.Passed();
                    });
                ASSERT_TRUE(result) << failure;
                EXPECT_TRUE(std::any_of(
                    result->objects.begin(), result->objects.end(),
                    [](const auto& object) { return object.typeName == "bonder-prisma"; }));
                io::BinaryWriter::WriteSolutionFile(files.solution, *result);
                const auto report =
                    io::VerifySolutionFiles(files.puzzle, files.solution, 10000, 2000);
                ASSERT_TRUE(report.Passed()) << report.detail;
                result->metrics = report.metrics;
                io::BinaryWriter::WriteSolutionFile(files.solution, *result);
                EXPECT_TRUE(
                    io::VerifySolutionFiles(files.puzzle, files.solution, 10000, 2000).Passed());
            }
#endif
        }
}

TEST(ProductionTransport, CoproductsStageBondMaskConversionsAfterConduit) {
    for (const auto& [source, target] : {std::pair{1, 2}, {2, 1}, {14, 2}, {6, 8}}) {
        SCOPED_TRACE(source);
        SCOPED_TRACE(target);
        for (bool reverse : {false, true}) {
            SCOPED_TRACE(reverse);
            auto p = Puzzle();
            p.permissionBits |= (1ULL << 3) | (1ULL << 8) | (1ULL << 9) | (1ULL << 11);
            p.productionInfo->chambers = {{{-12, 0}, "Large"}, {{12, 0}, "Large"}};
            auto& pipe = p.productionInfo->conduits[0];
            pipe.positionA = {-12, 0};
            pipe.positionB = {12, 0};
            pipe.path = {{0, 0}, {1, 0}, {0, 1}};
            p.inputs = {{{{core::AtomType::Fire, {0, 0}},
                          {core::AtomType::Air, {1, 0}},
                          {core::AtomType::Fire, {0, 1}}},
                         {{1, {0, 0}, {1, 0}},
                          {1, {1, 0}, {0, 1}},
                          {static_cast<std::uint8_t>(source), {0, 0}, {0, 1}}}}};
            p.outputs = {{{{core::AtomType::Air, {0, 0}}}, {}},
                         {{{core::AtomType::Fire, {0, 0}}, {core::AtomType::Fire, {0, 1}}},
                          {{static_cast<std::uint8_t>(target), {0, 0}, {0, 1}}}}};
            if (reverse) {
                std::reverse(p.inputs[0].atoms.begin(), p.inputs[0].atoms.end());
                std::reverse(p.outputs[1].atoms.begin(), p.outputs[1].atoms.end());
                std::reverse(p.inputs[0].bonds.begin(), p.inputs[0].bonds.end());
            }
            for (int permission : {3, 9, target == 1 ? 8 : 11}) {
                auto forbidden = p;
                forbidden.permissionBits &= ~(1ULL << permission);
                EXPECT_FALSE(solvers::CompileAcceptedProductionCoproducts(
                    forbidden, [](const auto&) { return true; }));
            }
#ifdef HAS_LIBVERIFY
            Files files;
            files.WritePuzzle(p);
            std::string failure;
            std::vector<std::string> failures;
            auto result =
                solvers::CompileAcceptedProductionCoproducts(p, [&](const auto& candidate) {
                    io::BinaryWriter::WriteSolutionFile(files.solution, candidate);
                    const auto report =
                        io::VerifySolutionFiles(files.puzzle, files.solution, 10000, 2000);
                    failure = report.detail + " at " + std::to_string(report.errorCycle) + " (" +
                              std::to_string(report.errorU) + "," + std::to_string(report.errorV) +
                              ")";
                    if (!report.Passed() &&
                        std::find(failures.begin(), failures.end(), failure) == failures.end())
                        failures.push_back(failure);
                    return report.Passed();
                });
            for (const auto& detail : failures)
                failure += "\n" + detail;
            ASSERT_TRUE(result) << failure;
            io::BinaryWriter::WriteSolutionFile(files.solution, *result);
            const auto report = io::VerifySolutionFiles(files.puzzle, files.solution, 10000, 2000);
            ASSERT_TRUE(report.Passed()) << report.detail;
            result->metrics = report.metrics;
            io::BinaryWriter::WriteSolutionFile(files.solution, *result);
            EXPECT_TRUE(
                io::VerifySolutionFiles(files.puzzle, files.solution, 10000, 2000).Passed());
#endif
        }
    }
}

TEST(ProductionTransport, CoproductsCreateSharedFireTriangleMasksAtDestination) {
    auto p = Puzzle();
    p.permissionBits |= (1ULL << 9) | (1ULL << 11);
    p.productionInfo->chambers = {{{-12, 0}, "Large"}, {{12, 0}, "Large"}};
    auto& pipe = p.productionInfo->conduits[0];
    pipe.positionA = {-12, 0};
    pipe.positionB = {12, 0};
    pipe.path = {{0, 0}, {0, 1}, {1, 0}, {-1, 0}};
    p.inputs = {
        {{{core::AtomType::Fire, {0, 0}},
          {core::AtomType::Fire, {0, 1}},
          {core::AtomType::Fire, {1, 0}},
          {core::AtomType::Air, {-1, 0}}},
         {{2, {0, 0}, {0, 1}}, {4, {0, 1}, {1, 0}}, {8, {1, 0}, {0, 0}}, {1, {0, 0}, {-1, 0}}}}};
    p.outputs = {{{{core::AtomType::Air, {0, 0}}}, {}},
                 {{{core::AtomType::Fire, {0, 0}},
                   {core::AtomType::Fire, {0, 1}},
                   {core::AtomType::Fire, {1, 0}}},
                  {{14, {0, 0}, {0, 1}}, {14, {0, 1}, {1, 0}}, {14, {1, 0}, {0, 0}}}}};
    auto forbidden = p;
    forbidden.permissionBits &= ~(1ULL << 11);
    EXPECT_FALSE(
        solvers::CompileAcceptedProductionCoproducts(forbidden, [](const auto&) { return true; }));
#ifdef HAS_LIBVERIFY
    Files files;
    for (const bool protectedSides : {false, true})
        for (const int mask : {2, 4, 6, 8, 10, 12, 14})
            for (int turns = 0; turns < 6; ++turns)
                for (const bool reverse : {false, true}) {
                    SCOPED_TRACE(protectedSides);
                    SCOPED_TRACE(mask);
                    SCOPED_TRACE(turns);
                    auto variant = p;
                    variant.inputs[0].bonds[0].bits = 0;
                    variant.outputs[1].bonds[0].bits = mask;
                    if (protectedSides) {
                        variant.inputs[0].bonds[1].bits = 1;
                        variant.inputs[0].bonds[2].bits = 1;
                        variant.outputs[1].bonds[1].bits = 1;
                        variant.outputs[1].bonds[2].bits = 1;
                    }
                    variant.inputs[0].bonds.erase(variant.inputs[0].bonds.begin());
                    const auto rotate = [&](core::HexCoord cell) {
                        for (int i = 0; i < turns; ++i)
                            cell = core::RotateClockwise(cell);
                        return cell;
                    };
                    for (auto& input : variant.inputs) {
                        for (auto& atom : input.atoms)
                            atom.position = rotate(atom.position);
                        for (auto& bond : input.bonds) {
                            bond.from = rotate(bond.from);
                            bond.to = rotate(bond.to);
                        }
                    }
                    for (auto& cell : variant.productionInfo->conduits[0].path)
                        cell = rotate(cell);
                    if (reverse) {
                        std::reverse(variant.inputs[0].atoms.begin(),
                                     variant.inputs[0].atoms.end());
                        std::reverse(variant.outputs[1].atoms.begin(),
                                     variant.outputs[1].atoms.end());
                    }
                    files.WritePuzzle(variant);
                    std::string failure;
                    auto result = solvers::CompileAcceptedProductionCoproducts(
                        variant, [&](const auto& candidate) {
                            io::BinaryWriter::WriteSolutionFile(files.solution, candidate);
                            const auto report =
                                io::VerifySolutionFiles(files.puzzle, files.solution, 10000, 2000);
                            failure = report.detail;
                            return report.Passed();
                        });
                    ASSERT_TRUE(result) << failure;
                    for (int pass = 0; pass < 2; ++pass) {
                        io::BinaryWriter::WriteSolutionFile(files.solution, *result);
                        const auto report =
                            io::VerifySolutionFiles(files.puzzle, files.solution, 10000, 2000);
                        ASSERT_TRUE(report.Passed()) << report.detail;
                        result->metrics = report.metrics;
                    }
                }
#endif
}

TEST(ProductionTransport, CoproductsRemoveInternalEdgesAtDestination) {
    auto p = Puzzle();
    p.permissionBits |= (1ULL << 9);
    p.productionInfo->chambers = {{{-12, 0}, "Large"}, {{12, 0}, "Large"}};
    auto& pipe = p.productionInfo->conduits[0];
    pipe.positionA = {-12, 0};
    pipe.positionB = {12, 0};
    pipe.path = {{0, 0}, {1, 0}, {0, 1}, {1, 1}};
    p.inputs = {
        {{{core::AtomType::Salt, {0, 0}},
          {core::AtomType::Air, {1, 0}},
          {core::AtomType::Fire, {0, 1}},
          {core::AtomType::Water, {1, 1}}},
         {{1, {0, 0}, {1, 0}}, {1, {0, 0}, {0, 1}}, {1, {1, 0}, {0, 1}}, {1, {0, 1}, {1, 1}}}}};
    p.outputs = {{{{core::AtomType::Water, {0, 0}}}, {}},
                 {{{core::AtomType::Salt, {0, 0}},
                   {core::AtomType::Air, {1, 0}},
                   {core::AtomType::Fire, {0, 1}}},
                  {{1, {0, 0}, {1, 0}}, {1, {0, 0}, {0, 1}}}}};
    auto forbidden = p;
    forbidden.permissionBits &= ~(1ULL << 9);
    EXPECT_FALSE(
        solvers::CompileAcceptedProductionCoproducts(forbidden, [](const auto&) { return true; }));
#ifdef HAS_LIBVERIFY
    Files files;
    for (const bool reverse : {false, true}) {
        auto variant = p;
        if (reverse) {
            std::reverse(variant.inputs[0].atoms.begin(), variant.inputs[0].atoms.end());
            std::reverse(variant.outputs[1].atoms.begin(), variant.outputs[1].atoms.end());
        }
        files.WritePuzzle(variant);
        std::string failure;
        auto result =
            solvers::CompileAcceptedProductionCoproducts(variant, [&](const auto& candidate) {
                io::BinaryWriter::WriteSolutionFile(files.solution, candidate);
                const auto report =
                    io::VerifySolutionFiles(files.puzzle, files.solution, 10000, 2000);
                failure = report.detail;
                return report.Passed();
            });
        ASSERT_TRUE(result) << failure;
        io::BinaryWriter::WriteSolutionFile(files.solution, *result);
        const auto report = io::VerifySolutionFiles(files.puzzle, files.solution, 10000, 2000);
        ASSERT_TRUE(report.Passed()) << report.detail;
        result->metrics = report.metrics;
        io::BinaryWriter::WriteSolutionFile(files.solution, *result);
        EXPECT_TRUE(io::VerifySolutionFiles(files.puzzle, files.solution, 10000, 2000).Passed());
    }
#endif
}

TEST(ProductionTransport, SharedFeedCoproductsUseRealConduitAndIndependentAcceptance) {
    auto p = Puzzle();
    p.permissionBits |= (1ULL << 9) | (1ULL << 12);
    p.productionInfo->chambers = {{{-12, 0}, "Large"}, {{12, 0}, "Large"}};
    auto& pipe = p.productionInfo->conduits[0];
    pipe.positionA = {-12, 0};
    pipe.positionB = {12, 0};
    pipe.path = {{0, 0}, {1, 0}};
    p.inputs = {
        {{{core::AtomType::Air, {0, 0}}, {core::AtomType::Fire, {1, 0}}}, {{1, {0, 0}, {1, 0}}}}};
    p.outputs = {{{{core::AtomType::Fire, {2, -1}}}, {}}, {{{core::AtomType::Salt, {-1, 2}}}, {}}};
    int attempts = 0;
    auto reject = [&](auto mutate) {
        auto invalid = p;
        mutate(invalid);
        EXPECT_THROW((void)solvers::CompileAcceptedProductionCoproducts(
                         invalid, [](const auto&) { return true; }),
                     std::invalid_argument);
    };
    reject([](auto& invalid) { invalid.permissionBits &= ~(1ULL << 9); });
    reject([](auto& invalid) { invalid.permissionBits &= ~(1ULL << 12); });
    reject([](auto& invalid) { invalid.outputs.pop_back(); });
    reject([](auto& invalid) { invalid.outputs[0].atoms[0].type = core::AtomType::Repeat; });
    reject([](auto& invalid) { invalid.outputs[0].atoms[0].position = {121, 0}; });
    EXPECT_FALSE(solvers::CompileAcceptedProductionCoproducts(p, [&](const auto&) {
        ++attempts;
        return false;
    }));
    EXPECT_GT(attempts, 1);
    EXPECT_LE(attempts, 64);
    auto ambiguous = p;
    for (auto& atom : ambiguous.inputs[0].atoms)
        atom.type = core::AtomType::Fire;
    std::optional<core::HexCoord> firstPort;
    auto remapped =
        solvers::CompileAcceptedProductionCoproducts(ambiguous, [&](const auto& candidate) {
            const auto port = std::find_if(
                candidate.objects.begin(), candidate.objects.end(), [](const auto& object) {
                    return object.typeName == "out-std" && object.bodyData.at(2) == 0;
                });
            EXPECT_NE(port, candidate.objects.end());
            if (port == candidate.objects.end()) return false;
            if (!firstPort) {
                firstPort = port->position;
                return false;
            }
            return port->position != *firstPort;
        });
    ASSERT_TRUE(remapped);
    EXPECT_THROW((void)solvers::CompileAcceptedProductionCoproducts(
                     p,
                     [](const auto&) -> bool {
                         throw std::invalid_argument("independent verifier infrastructure failure");
                     }),
                 std::invalid_argument);
#ifdef HAS_LIBVERIFY
    Files files;
    files.WritePuzzle(ambiguous);
    io::BinaryWriter::WriteSolutionFile(files.solution, *remapped);
    auto remappedReport = io::VerifySolutionFiles(files.puzzle, files.solution, 10000, 2000);
    ASSERT_TRUE(remappedReport.Passed()) << remappedReport.detail;
    remapped->metrics = remappedReport.metrics;
    io::BinaryWriter::WriteSolutionFile(files.solution, *remapped);
    EXPECT_TRUE(io::VerifySolutionFiles(files.puzzle, files.solution, 10000, 2000).Passed());
    for (const auto mask : {1, 2, 4, 6, 8, 10, 12, 14}) {
        for (const bool reverse : {false, true}) {
            auto variant = p;
            if (mask != 1)
                for (auto& atom : variant.inputs[0].atoms)
                    atom.type = core::AtomType::Fire;
            variant.inputs[0].bonds[0].bits = mask;
            variant.outputs[0].atoms[0].type = core::AtomType::Fire;
            if (reverse)
                std::reverse(variant.inputs[0].atoms.begin(), variant.inputs[0].atoms.end());
            for (auto& atom : variant.inputs[0].atoms)
                atom.position = core::Add(atom.position, {2, -3});
            for (auto& bond : variant.inputs[0].bonds) {
                bond.from = core::Add(bond.from, {2, -3});
                bond.to = core::Add(bond.to, {2, -3});
            }
            files.WritePuzzle(variant);
            std::string failure;
            auto result =
                solvers::CompileAcceptedProductionCoproducts(variant, [&](const auto& candidate) {
                    io::BinaryWriter::WriteSolutionFile(files.solution, candidate);
                    auto report =
                        io::VerifySolutionFiles(files.puzzle, files.solution, 10000, 2000);
                    failure = report.detail + " cycle=" + std::to_string(report.errorCycle) +
                              " at=" + std::to_string(report.errorU) + "," +
                              std::to_string(report.errorV);
                    return report.Passed();
                });
            ASSERT_TRUE(result) << "mask=" << mask << " reverse=" << reverse << ": " << failure;
            io::BinaryWriter::WriteSolutionFile(files.solution, *result);
            auto report = io::VerifySolutionFiles(files.puzzle, files.solution, 10000, 2000);
            ASSERT_TRUE(report.Passed()) << report.detail;
            result->metrics = report.metrics;
            io::BinaryWriter::WriteSolutionFile(files.solution, *result);
            EXPECT_TRUE(
                io::VerifySolutionFiles(files.puzzle, files.solution, 10000, 2000).Passed());
        }
    }
    files.WritePuzzle(p);
    solvers::AssemblySolver solver;
    std::vector<solvers::AssemblyFailure> failures;
    std::size_t verified = 0;
    const auto solution = solver.SolveVerified(
        p,
        [&](const auto& candidate) {
            ++verified;
            io::BinaryWriter::WriteSolutionFile(files.solution, candidate);
            auto report = io::VerifySolutionFiles(files.puzzle, files.solution, 10000, 2000);
            if (verified == 1 && report.Passed()) {
                report.status = io::VerificationStatus::Rejected;
                report.detail = "forced first-pose rejection";
            }
            return report;
        },
        [&](const auto& failure) { failures.push_back(failure); });
    EXPECT_GT(verified, 1u);
    EXPECT_TRUE(std::any_of(failures.begin(), failures.end(), [](const auto& failure) {
        return failure.stage == "production-coproducts" && failure.verification &&
               failure.detail == "forced first-pose rejection";
    }));
    io::BinaryWriter::WriteSolutionFile(files.solution, solution);
    const auto finalReport = io::VerifySolutionFiles(files.puzzle, files.solution, 10000, 2000);
    ASSERT_TRUE(finalReport.Passed()) << finalReport.detail;
    EXPECT_EQ(solution.metrics.cycles, finalReport.metrics.cycles);
    EXPECT_THROW((void)solver.SolveVerified(p,
                                            [](const auto&) -> io::VerificationReport {
                                                throw std::invalid_argument(
                                                    "independent verifier infrastructure failure");
                                            }),
                 std::invalid_argument);
#endif
}
