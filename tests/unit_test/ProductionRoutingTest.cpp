#include "ProductionRouting.hpp"
#include "AssemblySolver.hpp"
#include "BinaryWriter.hpp"
#include "ProductionLayout.hpp"
#include "SolutionVerifier.hpp"
#include "UnitTestConstants.hpp"
#include <algorithm>
#include <fstream>
#include <gtest/gtest.h>

using namespace opusoptima;
namespace {
io::PuzzleData Route(int count) {
    io::PuzzleData p;
    p.version = 3;
    p.name = "production-route";
    p.outputMultiplier = 1;
    p.permissionBits = (1ULL << 0) | (1ULL << 22) | (1ULL << 23) | (1ULL << 25);
    p.inputs = {{{{core::AtomType::Salt, {}}}, {}}};
    p.outputs = p.inputs;
    p.productionInfo.emplace();
    for (int i = 0; i < count; ++i) {
        p.productionInfo->chambers.push_back({{i * 10, 0}, i % 2 ? "Medium" : "Small"});
        if (i) p.productionInfo->conduits.push_back({{(i - 1) * 10, 0}, {i * 10, 0}, {{0, 0}}});
    }
    return p;
}
void Write(const std::filesystem::path& path, const io::PuzzleData& p) {
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
    for (const auto& c : info.chambers) {
        cell(c.position);
        text(c.chamberType);
    }
    word(info.conduits.size(), 4);
    for (const auto& c : info.conduits) {
        cell(c.positionA);
        cell(c.positionB);
        word(c.path.size(), 4);
        for (auto x : c.path)
            cell(x);
    }
    word(0, 4);
}
} // namespace
TEST(ProductionRouting, AcceptsTreesAndRejectsCyclesAndUnsupportedRules) {
    auto p = Route(4);
    EXPECT_EQ(solvers::PlanProductionRoute(p).handoffs.size(), 3U);
    p.productionInfo->conduits[2].positionA = {10, 0};
    EXPECT_EQ(solvers::PlanProductionRoute(p).handoffs.size(), 2U);
    p.productionInfo->conduits[2].positionB = {0, 0};
    EXPECT_THROW((void)solvers::CompileProductionRoute(p), std::invalid_argument);
    p = Route(3);
    p.productionInfo->shrinkLeft = true;
    EXPECT_NO_THROW((void)solvers::CompileProductionRoute(p));
    p = Route(3);
    p.outputs[0].atoms[0].type = core::AtomType::Fire;
    EXPECT_THROW((void)solvers::CompileProductionRoute(p), std::invalid_argument);
}
TEST(ProductionRouting, RejectsUnsupportedCompositionAndOrientation) {
    auto chemistry = [] {
        auto p = Route(3);
        p.permissionBits |= 1ULL << 12;
        p.inputs[0].atoms[0].type = core::AtomType::Water;
        return p;
    };
    auto check = [&](auto mutate) {
        auto p = chemistry();
        mutate(p);
        EXPECT_THROW((void)solvers::CompileProductionChemistryRoute(p), std::invalid_argument);
    };
    EXPECT_THROW((void)solvers::CompileProductionRoute(chemistry()), std::invalid_argument);
    EXPECT_NO_THROW((void)solvers::CompileProductionChemistryRoute(chemistry()));
    EXPECT_THROW((void)solvers::CompileProductionChemistryRoute(chemistry(), 3),
                 std::invalid_argument);
    EXPECT_THROW((void)solvers::CompileProductionChemistryRoute(chemistry(), 0, 6),
                 std::invalid_argument);
    EXPECT_THROW((void)solvers::CompileProductionRoute(Route(3), -1), std::invalid_argument);
    check([](auto& p) { p.permissionBits &= ~(1ULL << 12); });
    check([](auto& p) { p.inputs[0].atoms[0].type = core::AtomType::Gold; });
    check([](auto& p) { p.outputs[0].atoms[0].type = core::AtomType::Water; });
    check([](auto& p) { p.inputs[0].bonds.push_back({1, {}, {1, 0}}); });
    check([](auto& p) { p.outputs[0].bonds.push_back({1, {}, {1, 0}}); });
    check([](auto& p) { p.version = 2; });
    check([](auto& p) { p.productionInfo->vials.push_back({{}, true, 65}); });
    check([](auto& p) { p.productionInfo->conduits[0].path.push_back({1, 0}); });
    check([](auto& p) { p.productionInfo->conduits[1].positionB = {}; });
}

TEST(ProductionRouting, ChemistryAcceptanceRetriesAndSharesBudget) {
    auto p = Route(4);
    p.permissionBits |= 1ULL << 12;
    p.inputs[0].atoms[0].type = core::AtomType::Water;
    int calls = 0;
    const auto candidate = solvers::CompileAcceptedProductionChemistryRoute(
        p, [&](const auto&) { return ++calls == 2; });
    ASSERT_TRUE(candidate);
    EXPECT_EQ(calls, 2);
    calls = 0;
    EXPECT_FALSE(solvers::CompileAcceptedProductionChemistryRoute(p, [&](const auto&) {
        ++calls;
        return false;
    }));
    EXPECT_GT(calls, 1);
    EXPECT_LE(calls, 64);
    EXPECT_THROW((void)solvers::CompileAcceptedProductionChemistryRoute(
                     p,
                     [](const auto&) -> bool {
                         throw std::invalid_argument("acceptance infrastructure failure");
                     }),
                 std::invalid_argument);
}

#ifdef HAS_LIBVERIFY
TEST(ProductionRouting, IndependentlyVerifiesMolecularChemistryAtEveryRouteStage) {
    const auto base =
        std::filesystem::current_path() /
        ("production-molecular-chemistry-" + std::to_string(opusoptima::test::ProcessId()));
    const auto puzzle = base.string() + ".puzzle", solution = base.string() + ".solution";
    struct Cleanup {
        std::string a, b;
        ~Cleanup() {
            std::filesystem::remove(a);
            std::filesystem::remove(b);
        }
    } cleanup{puzzle, solution};
    for (int count : {2, 3, 4})
        for (bool selective : {false, true})
            for (int turns = 0; turns < 6; ++turns)
                for (const auto mask : {1, 2, 6, 14}) {
                    auto p = Route(count);
                    p.permissionBits |= 1ULL << 12;
                    for (auto& chamber : p.productionInfo->chambers)
                        chamber.chamberType = "Large";
                    for (auto& pipe : p.productionInfo->conduits)
                        pipe.path = {{0, 0}, {1, 0}};
                    p.inputs = {{{{core::AtomType::Water, {}}, {core::AtomType::Air, {1, 0}}},
                                 {{1, {}, {1, 0}}}}};
                    p.inputs[0].bonds[0].bits = static_cast<std::uint8_t>(mask);
                    if (selective) p.inputs[0].atoms[1].type = core::AtomType::Gold;
                    p.outputs = p.inputs;
                    p.outputs[0].atoms[0].type = core::AtomType::Salt;
                    if (!selective) p.outputs[0].atoms[1].type = core::AtomType::Salt;
                    std::reverse(p.outputs[0].atoms.begin(), p.outputs[0].atoms.end());
                    auto transform = [&](core::HexCoord cell) {
                        for (int i = 0; i < turns; ++i)
                            cell = core::RotateClockwise(cell);
                        return core::Add(cell, {3, -2});
                    };
                    for (auto& atom : p.outputs[0].atoms)
                        atom.position = transform(atom.position);
                    for (auto& bond : p.outputs[0].bonds) {
                        bond.from = transform(bond.from);
                        bond.to = transform(bond.to);
                    }
                    Write(puzzle, p);
                    for (int stage = 0; stage < count; ++stage) {
                        auto machine = solvers::CompileProductionChemistryRoute(p, stage);
                        io::BinaryWriter::WriteSolutionFile(solution, machine);
                        const auto report = io::VerifySolutionFiles(puzzle, solution, 1000);
                        ASSERT_TRUE(report.Passed())
                            << "chambers=" << count << " stage=" << stage << " " << report.detail;
                        machine.metrics = report.metrics;
                        io::BinaryWriter::WriteSolutionFile(solution, machine);
                        EXPECT_TRUE(io::VerifySolutionFiles(puzzle, solution, 1000).Passed());
                    }
                    int chemistryCalls = 0;
                    auto chemistry = solvers::CompileAcceptedProductionChemistryRoute(
                        p, [&](const auto& candidate) {
                            if (++chemistryCalls == 1) return false;
                            io::BinaryWriter::WriteSolutionFile(solution, candidate);
                            return io::VerifySolutionFiles(puzzle, solution, 1000).Passed();
                        });
                    ASSERT_TRUE(chemistry);
                    ASSERT_GT(chemistryCalls, 1);
                    io::BinaryWriter::WriteSolutionFile(solution, *chemistry);
                    auto chemistryReport = io::VerifySolutionFiles(puzzle, solution, 1000);
                    ASSERT_TRUE(chemistryReport.Passed()) << chemistryReport.detail;
                    chemistry->metrics = chemistryReport.metrics;
                    io::BinaryWriter::WriteSolutionFile(solution, *chemistry);
                    ASSERT_TRUE(io::VerifySolutionFiles(puzzle, solution, 1000).Passed());
                    auto accepted =
                        solvers::AssemblySolver{}.SolveAccepted(p, [&](const auto& candidate) {
                            io::BinaryWriter::WriteSolutionFile(solution, candidate);
                            return io::VerifySolutionFiles(puzzle, solution, 1000).Passed();
                        });
                    io::BinaryWriter::WriteSolutionFile(solution, accepted);
                    ASSERT_TRUE(io::VerifySolutionFiles(puzzle, solution, 1000).Passed());
                    auto malformed = p;
                    malformed.outputs[0].bonds.clear();
                    EXPECT_THROW((void)solvers::CompileProductionChemistryRoute(malformed),
                                 std::invalid_argument);
                    malformed = p;
                    malformed.outputs[0].atoms[0].type = core::AtomType::Fire;
                    EXPECT_THROW((void)solvers::CompileProductionChemistryRoute(malformed),
                                 std::invalid_argument);
                    malformed = p;
                    malformed.outputs[0].atoms[0].position = malformed.outputs[0].atoms[1].position;
                    EXPECT_THROW((void)solvers::CompileProductionChemistryRoute(malformed),
                                 std::invalid_argument);
                    malformed = p;
                    malformed.outputs[0].bonds[0].bits = mask == 2 ? 1 : 2;
                    EXPECT_THROW((void)solvers::CompileProductionChemistryRoute(malformed),
                                 std::invalid_argument);
                }
}

#endif

TEST(ProductionRouting, AssemblyRetriesComposedChemistryCandidates) {
    auto p = Route(3);
    p.permissionBits |= 1ULL << 12;
    p.inputs[0].atoms[0].type = core::AtomType::Water;
    int candidates = 0;
    auto machine =
        solvers::AssemblySolver{}.SolveAccepted(p, [&](const auto&) { return ++candidates == 2; });
    EXPECT_EQ(candidates, 2);
    EXPECT_EQ(
        std::count_if(machine.objects.begin(), machine.objects.end(),
                      [](const auto& object) { return object.typeName == "glyph-calcification"; }),
        1);
    EXPECT_THROW((void)solvers::AssemblySolver{}.SolveAccepted(
                     p,
                     [](const auto&) -> bool {
                         throw std::runtime_error("acceptance infrastructure failure");
                     }),
                 std::runtime_error);
}

TEST(ProductionRouting, JointSearchExhaustionIsBoundedAndRejectsUnsupportedRules) {
    auto p = Route(6);
    p.inputs.push_back(p.inputs[0]);
    p.outputs = p.inputs;
    unsigned calls = 0;
    EXPECT_FALSE(solvers::CompileAcceptedProductionStreams(p, [&](const auto&) {
        ++calls;
        return false;
    }));
    EXPECT_GT(calls, 0U);
    EXPECT_LE(calls, 64U);
    p.permissionBits &= ~(1ULL << 0);
    calls = 0;
    EXPECT_FALSE(solvers::CompileAcceptedProductionStreams(p, [&](const auto&) {
        ++calls;
        return true;
    }));
    EXPECT_EQ(calls, 0U);
}

TEST(ProductionRouting, ReusedConduitRejectionIsBoundedAndPropagatesCallbackErrors) {
    auto p = Route(2);
    for (auto& chamber : p.productionInfo->chambers)
        chamber.chamberType = "Large";
    p.inputs.push_back({{{core::AtomType::Air, {}}}, {}});
    p.outputs = p.inputs;
    unsigned calls = 0;
    EXPECT_FALSE(solvers::CompileAcceptedProductionStreams(p, [&](const auto& candidate) {
        ++calls;
        EXPECT_EQ(std::count_if(candidate.objects.begin(), candidate.objects.end(),
                                [](const auto& object) { return object.typeName == "pipe"; }),
                  2);
        return false;
    }));
    EXPECT_GT(calls, 0U);
    EXPECT_LE(calls, 64U);
    for (bool invalidArgument : {false, true}) {
        calls = 0;
        try {
            (void)solvers::CompileAcceptedProductionStreams(p, [&](const auto&) -> bool {
                ++calls;
                if (invalidArgument) throw std::invalid_argument("reused conduit verifier failure");
                throw std::runtime_error("reused conduit verifier failure");
            });
            FAIL() << "acceptance exception was swallowed";
        } catch (const std::invalid_argument& error) {
            EXPECT_TRUE(invalidArgument);
            EXPECT_STREQ(error.what(), "reused conduit verifier failure");
        } catch (const std::runtime_error& error) {
            EXPECT_FALSE(invalidArgument);
            EXPECT_STREQ(error.what(), "reused conduit verifier failure");
        }
        EXPECT_EQ(calls, 1U);
    }
}

#ifdef HAS_LIBVERIFY
TEST(ProductionRouting, IndependentlyVerifiesJointStreamsAndCallbackPropagation) {
    const auto base = std::filesystem::current_path() /
                      ("production-streams-" + std::to_string(opusoptima::test::ProcessId()));
    const auto puzzle = base.string() + ".puzzle", solution = base.string() + ".solution";
    struct Cleanup {
        std::string a, b;
        ~Cleanup() {
            std::filesystem::remove(a);
            std::filesystem::remove(b);
        }
    } cleanup{puzzle, solution};
    auto p = Route(4);
    p.productionInfo->conduits.erase(p.productionInfo->conduits.begin() + 1);
    p.inputs.push_back({{{core::AtomType::Air, {}}}, {}});
    p.outputs = {p.inputs[1], p.inputs[0]};
    Write(puzzle, p);
    int attempts = 0;
    auto machine = solvers::AssemblySolver{}.SolveAccepted(p, [&](const auto& candidate) {
        ++attempts;
        io::BinaryWriter::WriteSolutionFile(solution, candidate);
        return io::VerifySolutionFiles(puzzle, solution, 500).Passed();
    });
    EXPECT_EQ(machine.solutionName, "Joint production conduit streams");
    io::BinaryWriter::WriteSolutionFile(solution, machine);
    const auto report = io::VerifySolutionFiles(puzzle, solution, 500);
    ASSERT_TRUE(report.Passed()) << report.detail;
    machine.metrics = report.metrics;
    io::BinaryWriter::WriteSolutionFile(solution, machine);
    EXPECT_TRUE(io::VerifySolutionFiles(puzzle, solution, 500).Passed());
    EXPECT_THROW((void)solvers::CompileAcceptedProductionStreams(
                     p,
                     [](const auto&) -> bool {
                         throw std::invalid_argument("stream verifier infrastructure failure");
                     }),
                 std::invalid_argument);
}

TEST(ProductionRouting, IndependentlyVerifiesReusedConduitStreams) {
    const auto base =
        std::filesystem::current_path() /
        ("production-reused-streams-" + std::to_string(opusoptima::test::ProcessId()));
    const auto puzzle = base.string() + ".puzzle", solution = base.string() + ".solution";
    struct Cleanup {
        std::string a, b;
        ~Cleanup() {
            std::filesystem::remove(a);
            std::filesystem::remove(b);
        }
    } cleanup{puzzle, solution};
    auto p = Route(2);
    for (auto& chamber : p.productionInfo->chambers)
        chamber.chamberType = "Large";
    p.inputs.push_back({{{core::AtomType::Air, {}}}, {}});
    p.outputs = p.inputs;
    Write(puzzle, p);
    auto machine = solvers::CompileAcceptedProductionStreams(p, [&](const auto& candidate) {
        EXPECT_EQ(std::count_if(candidate.objects.begin(), candidate.objects.end(),
                                [](const auto& object) { return object.typeName == "pipe"; }),
                  2);
        io::BinaryWriter::WriteSolutionFile(solution, candidate);
        return io::VerifySolutionFiles(puzzle, solution, 2000).Passed();
    });
    ASSERT_TRUE(machine);
    io::BinaryWriter::WriteSolutionFile(solution, *machine);
    const auto report = io::VerifySolutionFiles(puzzle, solution, 2000);
    ASSERT_TRUE(report.Passed()) << report.detail;
    machine->metrics = report.metrics;
    io::BinaryWriter::WriteSolutionFile(solution, *machine);
    EXPECT_TRUE(io::VerifySolutionFiles(puzzle, solution, 2000).Passed());
}

TEST(ProductionRouting, IndependentlyVerifiesReusedMolecularConduitStreams) {
    const auto base =
        std::filesystem::current_path() /
        ("production-reused-molecules-" + std::to_string(opusoptima::test::ProcessId()));
    const auto puzzle = base.string() + ".puzzle", solution = base.string() + ".solution";
    struct Cleanup {
        std::string a, b;
        ~Cleanup() {
            std::filesystem::remove(a);
            std::filesystem::remove(b);
        }
    } cleanup{puzzle, solution};
    for (const std::vector<core::HexCoord>& shape : std::vector<std::vector<core::HexCoord>>{
             {{0, 0}, {1, 0}}, {{0, 0}, {1, 0}, {2, 0}}, {{0, 0}, {1, 0}, {1, 1}}})
        for (const std::uint8_t bondMask : {1, 2, 14}) {
            SCOPED_TRACE(shape.size());
            SCOPED_TRACE(shape.back().q);
            SCOPED_TRACE(shape.back().r);
            SCOPED_TRACE(bondMask);
            auto p = Route(2);
            for (auto& chamber : p.productionInfo->chambers)
                chamber.chamberType = "Large";
            p.productionInfo->conduits[0].path = shape;
            p.inputs.clear();
            for (const auto type : {core::AtomType::Salt, core::AtomType::Air}) {
                io::Molecule molecule;
                for (const auto cell : shape)
                    molecule.atoms.push_back({type, cell});
                for (std::size_t i = 1; i < shape.size(); ++i)
                    molecule.bonds.push_back({bondMask, shape[i - 1], shape[i]});
                p.inputs.push_back(std::move(molecule));
            }
            p.outputs = p.inputs;
            auto single = p;
            single.inputs.resize(1);
            single.outputs.resize(1);
            EXPECT_NO_THROW((void)solvers::CompileProductionTransport(single));
            Write(puzzle, single);
            const auto verifiedSingle =
                solvers::CompileAcceptedProductionTransport(single, [&](const auto& candidate) {
                    io::BinaryWriter::WriteSolutionFile(solution, candidate);
                    return io::VerifySolutionFiles(puzzle, solution, 2000).Passed();
                });
            ASSERT_TRUE(verifiedSingle);
            Write(puzzle, p);
            std::string lastFailure;
            unsigned candidateCount = 0;
            auto machine = solvers::CompileAcceptedProductionStreams(p, [&](const auto& candidate) {
                ++candidateCount;
                EXPECT_EQ(
                    std::count_if(candidate.objects.begin(), candidate.objects.end(),
                                  [](const auto& object) { return object.typeName == "pipe"; }),
                    2);
                io::BinaryWriter::WriteSolutionFile(solution, candidate);
                const auto report = io::VerifySolutionFiles(puzzle, solution, 2000);
                lastFailure = report.detail + " cycle=" + std::to_string(report.errorCycle) +
                              " cell=" + std::to_string(report.errorU) + "," +
                              std::to_string(report.errorV);
                return report.Passed();
            });
            ASSERT_TRUE(machine) << "candidates=" << candidateCount << " " << lastFailure;
            io::BinaryWriter::WriteSolutionFile(solution, *machine);
            const auto report = io::VerifySolutionFiles(puzzle, solution, 2000);
            ASSERT_TRUE(report.Passed()) << report.detail;
            machine->metrics = report.metrics;
            io::BinaryWriter::WriteSolutionFile(solution, *machine);
            EXPECT_TRUE(io::VerifySolutionFiles(puzzle, solution, 2000).Passed());
        }
}

TEST(ProductionRouting, IndependentlyVerifiesSharedChamberStreams) {
    const auto base =
        std::filesystem::current_path() /
        ("production-shared-streams-" + std::to_string(opusoptima::test::ProcessId()));
    const auto puzzle = base.string() + ".puzzle", solution = base.string() + ".solution";
    struct Cleanup {
        std::string a, b;
        ~Cleanup() {
            std::filesystem::remove(a);
            std::filesystem::remove(b);
        }
    } cleanup{puzzle, solution};
    auto p = Route(3);
    for (auto& chamber : p.productionInfo->chambers)
        chamber.chamberType = "Large";
    p.inputs.push_back({{{core::AtomType::Air, {}}}, {}});
    p.outputs = p.inputs;
    Write(puzzle, p);
    unsigned attempts = 0;
    auto machine = solvers::CompileAcceptedProductionStreams(p, [&](const auto& candidate) {
        ++attempts;
        io::BinaryWriter::WriteSolutionFile(solution, candidate);
        const auto report = io::VerifySolutionFiles(puzzle, solution, 500);
        const bool delayed =
            std::any_of(candidate.objects.begin(), candidate.objects.end(), [](const auto& object) {
                return object.typeName == "arm1" && object.bodyData.at(4) == 2 &&
                       !object.instructions.empty() && object.instructions.front().parameter > 0;
            });
        return report.Passed() && delayed && attempts > 1;
    });
    ASSERT_TRUE(machine) << "attempts=" << attempts;
    EXPECT_GT(attempts, 1U);
    EXPECT_EQ(std::count_if(machine->objects.begin(), machine->objects.end(),
                            [&](const auto& object) {
                                return object.typeName == "arm1" &&
                                       core::Distance(object.position,
                                                      p.productionInfo->chambers[1].position) <= 3;
                            }),
              2);
    io::BinaryWriter::WriteSolutionFile(solution, *machine);
    const auto report = io::VerifySolutionFiles(puzzle, solution, 500);
    ASSERT_TRUE(report.Passed()) << report.detail;
    machine->metrics = report.metrics;
    io::BinaryWriter::WriteSolutionFile(solution, *machine);
    EXPECT_TRUE(io::VerifySolutionFiles(puzzle, solution, 500).Passed());
}

TEST(ProductionRouting, IndependentlyVerifiesJointRigidStreams) {
    const auto base = std::filesystem::current_path() /
                      ("production-rigid-streams-" + std::to_string(opusoptima::test::ProcessId()));
    const auto puzzle = base.string() + ".puzzle", solution = base.string() + ".solution";
    struct Cleanup {
        std::string a, b;
        ~Cleanup() {
            std::filesystem::remove(a);
            std::filesystem::remove(b);
        }
    } cleanup{puzzle, solution};
    for (bool chemistry : {false, true}) {
        SCOPED_TRACE(chemistry);
        auto p = Route(4);
        p.productionInfo->conduits.erase(p.productionInfo->conduits.begin() + 1);
        p.inputs[0].atoms.push_back({core::AtomType::Air, {1, 0}});
        p.inputs[0].bonds.push_back({14, {}, {1, 0}});
        p.inputs.push_back(p.inputs[0]);
        p.outputs = p.inputs;
        if (chemistry) {
            p.permissionBits |= 1ULL << 12;
            for (auto& output : p.outputs)
                output.atoms[1].type = core::AtomType::Salt;
        }
        for (auto& conduit : p.productionInfo->conduits)
            conduit.path.push_back({1, 0});
        Write(puzzle, p);
        unsigned rejectedChemistry = 0;
        auto machine = solvers::CompileAcceptedProductionStreams(p, [&](const auto& candidate) {
            if (chemistry && ++rejectedChemistry == 1) return false;
            io::BinaryWriter::WriteSolutionFile(solution, candidate);
            return io::VerifySolutionFiles(puzzle, solution, 500).Passed();
        });
        ASSERT_TRUE(machine);
        if (chemistry) {
            EXPECT_GT(rejectedChemistry, 1U);
            EXPECT_THROW((void)solvers::CompileAcceptedProductionStreams(
                             p,
                             [](const auto&) -> bool {
                                 throw std::invalid_argument("joint chemistry verifier failure");
                             }),
                         std::invalid_argument);
        }
        io::BinaryWriter::WriteSolutionFile(solution, *machine);
        const auto report = io::VerifySolutionFiles(puzzle, solution, 500);
        ASSERT_TRUE(report.Passed()) << report.detail;
        machine->metrics = report.metrics;
        io::BinaryWriter::WriteSolutionFile(solution, *machine);
        EXPECT_TRUE(io::VerifySolutionFiles(puzzle, solution, 500).Passed());
    }
}

TEST(ProductionRouting, IndependentlyVerifiesUnequalJointRelayPeriodsAndChemistry) {
    const auto base = std::filesystem::current_path() /
                      ("production-relay-streams-" + std::to_string(opusoptima::test::ProcessId()));
    const auto puzzle = base.string() + ".puzzle", solution = base.string() + ".solution";
    struct Cleanup {
        std::string a, b;
        ~Cleanup() {
            std::filesystem::remove(a);
            std::filesystem::remove(b);
        }
    } cleanup{puzzle, solution};
    auto p = Route(7);
    p.productionInfo->conduits.erase(p.productionInfo->conduits.begin() + 2);
    p.permissionBits |= 1ULL << 12;
    p.inputs.push_back({{{core::AtomType::Water, {}}}, {}});
    p.outputs = {p.inputs[0], p.inputs[0]};
    Write(puzzle, p);
    int attempts = 0;
    auto machine = solvers::CompileAcceptedProductionStreams(p, [&](const auto& candidate) {
        ++attempts;
        if (std::count_if(candidate.objects.begin(), candidate.objects.end(),
                          [](const auto& object) { return object.typeName == "pipe"; }) != 10)
            return false;
        io::BinaryWriter::WriteSolutionFile(solution, candidate);
        return io::VerifySolutionFiles(puzzle, solution, 500).Passed();
    });
    ASSERT_TRUE(machine) << "attempts=" << attempts;
    io::BinaryWriter::WriteSolutionFile(solution, *machine);
    const auto report = io::VerifySolutionFiles(puzzle, solution, 500);
    ASSERT_TRUE(report.Passed()) << report.detail;
    machine->metrics = report.metrics;
    io::BinaryWriter::WriteSolutionFile(solution, *machine);
    EXPECT_TRUE(io::VerifySolutionFiles(puzzle, solution, 500).Passed());
}

TEST(ProductionRouting, AcceptedRelaySearchVerifiesNonlinearPayloadAndMeasuredMetrics) {
    const auto base = std::filesystem::current_path() /
                      ("production-route-search-" + std::to_string(opusoptima::test::ProcessId()));
    const auto puzzle = base.string() + ".puzzle", solution = base.string() + ".solution";
    struct Cleanup {
        std::string a, b;
        ~Cleanup() {
            std::filesystem::remove(a);
            std::filesystem::remove(b);
        }
    } cleanup{puzzle, solution};
    auto p = Route(4);
    p.inputs[0] = {{{core::AtomType::Salt, {0, 0}},
                    {core::AtomType::Air, {1, 0}},
                    {core::AtomType::Fire, {1, 1}}},
                   {{1, {0, 0}, {1, 0}}, {1, {1, 0}, {1, 1}}}};
    p.outputs = p.inputs;
    for (auto& chamber : p.productionInfo->chambers)
        chamber.chamberType = "MediumWide";
    for (auto& conduit : p.productionInfo->conduits)
        conduit.path = {{0, 0}, {1, 0}, {1, 1}};
    Write(puzzle, p);
    int attempts = 0, failures = 0;
    auto machine = solvers::CompileAcceptedProductionRoute(p, [&](const auto& candidate) {
        ++attempts;
        io::BinaryWriter::WriteSolutionFile(solution, candidate);
        const auto report = io::VerifySolutionFiles(puzzle, solution, 500);
        if (!report.Passed()) ++failures;
        return report.Passed();
    });
    ASSERT_TRUE(machine) << "attempts=" << attempts << " failures=" << failures;
    io::BinaryWriter::WriteSolutionFile(solution, *machine);
    const auto report = io::VerifySolutionFiles(puzzle, solution, 500);
    ASSERT_TRUE(report.Passed()) << report.detail;
    machine->metrics = report.metrics;
    io::BinaryWriter::WriteSolutionFile(solution, *machine);
    ASSERT_TRUE(io::VerifySolutionFiles(puzzle, solution, 500).Passed());
    EXPECT_THROW((void)solvers::CompileAcceptedProductionRoute(
                     p,
                     [](const auto&) -> bool {
                         throw std::invalid_argument("native infrastructure failure");
                     }),
                 std::invalid_argument);
}
TEST(ProductionRouting, RotatedRigidFootprintsMustFitEveryPayloadCell) {
    int witnesses = 0, rotatedOnly = 0;
    for (const auto* type : {"SmallWide", "SmallWider", "MediumWide"})
        for (int count : {2, 3, 4})
            for (const auto tip :
                 std::vector<core::HexCoord>{{2, 0}, {1, 1}, {0, 1}, {1, -1}, {2, -1}}) {
                auto p = Route(count);
                for (auto& chamber : p.productionInfo->chambers)
                    chamber.chamberType = type;
                p.inputs[0] = {{{core::AtomType::Salt, {}},
                                {core::AtomType::Air, {1, 0}},
                                {core::AtomType::Fire, tip}},
                               {{1, {}, {1, 0}}, {1, {1, 0}, tip}}};
                p.outputs = p.inputs;
                for (auto& conduit : p.productionInfo->conduits)
                    conduit.path = {{0, 0}, {1, 0}, tip};
                solvers::ProductionRoutePlan plan;
                try {
                    plan = solvers::PlanProductionRoute(p);
                } catch (const std::invalid_argument&) {
                    continue;
                }
                int accepted = 0, rejected = 0;
                for (int rotation = 0; rotation < 6; ++rotation) {
                    auto rotate = [&](core::HexCoord cell) {
                        for (int i = 0; i < rotation; ++i)
                            cell = core::RotateClockwise(cell);
                        return cell;
                    };
                    auto fits = [&](std::size_t chamber, core::HexCoord cell) {
                        const auto& layout = plan.chambers[chamber];
                        return solvers::ContainsProductionCell(
                            layout,
                            core::Add(layout.center, rotate(core::Subtract(cell, layout.center))));
                    };
                    bool expected = true;
                    for (std::size_t hop = 0; hop < plan.handoffs.size(); ++hop) {
                        const auto& handoff = plan.handoffs[hop];
                        for (const auto& atom : handoff.payload.atoms) {
                            expected &= fits(hop, core::Add(handoff.sourcePosition, atom.position));
                            expected &= fits(hop + 1,
                                             core::Add(handoff.destinationPosition, atom.position));
                        }
                    }
                    auto moleculeFits = [&](const auto& molecule, std::size_t chamber,
                                            core::HexCoord position, int turns) {
                        for (const auto& atom : molecule.atoms) {
                            auto cell = atom.position;
                            for (int i = 0; i < turns; ++i)
                                cell = core::RotateClockwise(cell);
                            expected &= fits(chamber, core::Add(position, cell));
                        }
                    };
                    moleculeFits(p.inputs[0], 0, plan.inputPosition, plan.inputRotation);
                    moleculeFits(p.outputs[0], plan.chambers.size() - 1, plan.outputPosition,
                                 plan.outputRotation);
                    if (rotation == 0 && !expected) ++rotatedOnly;
                    SCOPED_TRACE(::testing::Message()
                                 << type << " rooms=" << count << " tip=" << tip.q << "," << tip.r
                                 << " rotation=" << rotation);
                    if (expected) {
                        EXPECT_NO_THROW((void)solvers::CompileProductionRoute(p, rotation));
                        ++accepted;
                    } else {
                        EXPECT_THROW((void)solvers::CompileProductionRoute(p, rotation),
                                     std::invalid_argument);
                        ++rejected;
                    }
                }
                if (accepted && rejected) ++witnesses;
            }
    EXPECT_GT(witnesses, 0);
    EXPECT_GT(rotatedOnly, 0);
}

TEST(ProductionRouting, TranslatedMoleculeOriginsDoNotChangePhysicalFootprints) {
    const auto base = std::filesystem::current_path() /
                      ("production-origin-" + std::to_string(opusoptima::test::ProcessId()));
    const auto puzzle = base.string() + ".puzzle", solution = base.string() + ".solution";
    struct Cleanup {
        std::string a, b;
        ~Cleanup() {
            std::filesystem::remove(a);
            std::filesystem::remove(b);
        }
    } cleanup{puzzle, solution};
    for (const auto offset : std::vector<core::HexCoord>{{20, -17}, {-19, 23}}) {
        auto p = Route(3);
        for (auto& chamber : p.productionInfo->chambers)
            chamber.chamberType = "Large";
        p.inputs[0] = {{{core::AtomType::Salt, offset},
                        {core::AtomType::Air, core::Add(offset, {1, 0})},
                        {core::AtomType::Fire, core::Add(offset, {2, 0})}},
                       {{1, offset, core::Add(offset, {1, 0})},
                        {1, core::Add(offset, {1, 0}), core::Add(offset, {2, 0})}}};
        p.outputs = p.inputs;
        for (auto& conduit : p.productionInfo->conduits)
            conduit.path = {{0, 0}, {1, 0}, {2, 0}};
        Write(puzzle, p);
        for (int rotation = 0; rotation < 6; ++rotation) {
            SCOPED_TRACE(rotation);
            auto machine = solvers::CompileProductionRoute(p, rotation);
            io::BinaryWriter::WriteSolutionFile(solution, machine);
            const auto report = io::VerifySolutionFiles(puzzle, solution, 500);
            ASSERT_TRUE(report.Passed()) << report.detail;
            machine.metrics = report.metrics;
            io::BinaryWriter::WriteSolutionFile(solution, machine);
            EXPECT_TRUE(io::VerifySolutionFiles(puzzle, solution, 500).Passed());
        }
    }
}

TEST(ProductionRouting, IndependentlyVerifiesRigidRelay) {
    const auto base = std::filesystem::current_path() /
                      ("production-rigid-route-" + std::to_string(opusoptima::test::ProcessId()));
    const auto puzzle = base.string() + ".puzzle", solution = base.string() + ".solution";
    struct Cleanup {
        std::string a, b;
        ~Cleanup() {
            std::filesystem::remove(a);
            std::filesystem::remove(b);
        }
    } cleanup{puzzle, solution};
    int verified = 0;
    for (int count : {3, 4, 6})
        for (int turn = 0; turn < 6; ++turn)
            for (int mask : {1, 2, 4, 6, 8, 10, 12, 14})
                for (bool reverse : {false, true})
                    for (bool bent : {false, true}) {
                        SCOPED_TRACE(count);
                        SCOPED_TRACE(turn);
                        SCOPED_TRACE(mask);
                        SCOPED_TRACE(reverse);
                        SCOPED_TRACE(bent);
                        auto p = Route(count);
                        const core::HexCoord end =
                            bent ? core::HexCoord{1, 1} : core::HexCoord{2, 0};
                        p.inputs[0] = {{{core::AtomType::Salt, {0, 0}},
                                        {core::AtomType::Air, {1, 0}},
                                        {core::AtomType::Fire, end}},
                                       {{1, {0, 0}, {1, 0}}, {1, {1, 0}, end}}};
                        if (mask != 1)
                            for (auto& atom : p.inputs[0].atoms)
                                atom.type = core::AtomType::Fire;
                        for (auto& bond : p.inputs[0].bonds)
                            bond.bits = mask;
                        p.outputs = p.inputs;
                        if (reverse) {
                            std::reverse(p.inputs[0].atoms.begin(), p.inputs[0].atoms.end());
                            for (auto& bond : p.outputs[0].bonds)
                                std::swap(bond.from, bond.to);
                            std::reverse(p.outputs[0].bonds.begin(), p.outputs[0].bonds.end());
                        }
                        for (auto& chamber : p.productionInfo->chambers)
                            chamber.chamberType = "Large";
                        for (auto& conduit : p.productionInfo->conduits)
                            conduit.path = {{0, 0}, {1, 0}, end};
                        Write(puzzle, p);
                        auto machine = solvers::CompileProductionRoute(p, turn);
                        io::BinaryWriter::WriteSolutionFile(solution, machine);
                        const auto report = io::VerifySolutionFiles(puzzle, solution, 500);
                        ASSERT_TRUE(report.Passed())
                            << "count=" << count << " rotation=" << turn << " " << report.detail
                            << " cycle=" << report.errorCycle << " at=" << report.errorU << ","
                            << report.errorV;
                        machine.metrics = report.metrics;
                        io::BinaryWriter::WriteSolutionFile(solution, machine);
                        ASSERT_TRUE(io::VerifySolutionFiles(puzzle, solution, 500).Passed());
                        ++verified;
                    }
    EXPECT_EQ(verified, 576);
}
TEST(ProductionRouting, IndependentlyVerifiesComposedChemistryAtEveryStageAndOrientation) {
    const auto base = std::filesystem::current_path() /
                      ("production-composition-" + std::to_string(opusoptima::test::ProcessId()));
    const auto puzzle = base.string() + ".puzzle", solution = base.string() + ".solution";
    struct Cleanup {
        std::string a, b;
        ~Cleanup() {
            std::filesystem::remove(a);
            std::filesystem::remove(b);
        }
    } cleanup{puzzle, solution};
    const std::vector<core::AtomType> cardinals{core::AtomType::Air, core::AtomType::Earth,
                                                core::AtomType::Fire, core::AtomType::Water};
    const std::vector<std::string> chambers{"Small",  "SmallWide",  "SmallWider",
                                            "Medium", "MediumWide", "Large"};
    for (int count : {2, 3, 6})
        for (int rotation = 0; rotation < 6; ++rotation)
            for (int stage = 0; stage < count; ++stage) {
                SCOPED_TRACE(count);
                SCOPED_TRACE(rotation);
                SCOPED_TRACE(stage);
                auto p = Route(count);
                p.permissionBits |= 1ULL << 12;
                p.inputs[0].atoms[0] = {cardinals[(rotation + stage) % 4], {-3, 4}};
                p.outputs[0].atoms[0].position = {2, -3};
                p.productionInfo->isolation = rotation % 2;
                for (int i = 0; i < count; ++i) {
                    p.productionInfo->chambers[i].position = {i % 2 ? -10 : 10, i * 10 - 30};
                    p.productionInfo->chambers[i].chamberType = chambers[(rotation + i) % 6];
                    if (i)
                        p.productionInfo->conduits[i - 1] = {
                            p.productionInfo->chambers[i].position,
                            p.productionInfo->chambers[i - 1].position,
                            {{0, 0}}};
                }
                // Chamber and edge listing order is not the route's spatial order.
                std::swap(p.productionInfo->chambers.front(), p.productionInfo->chambers.back());
                Write(puzzle, p);
                const auto parsed = io::BinaryParser::ParsePuzzleFile(puzzle);
                auto machine = solvers::CompileProductionChemistryRoute(parsed, stage, rotation);
                io::BinaryWriter::WriteSolutionFile(solution, machine);
                const auto report = io::VerifySolutionFiles(puzzle, solution, 300);
                ASSERT_TRUE(report.Passed()) << report.detail << " cycle=" << report.errorCycle;
                EXPECT_GT(report.metrics.cycles, 20);
                auto broken = machine;
                std::erase_if(broken.objects,
                              [](const auto& o) { return o.typeName == "glyph-calcification"; });
                io::BinaryWriter::WriteSolutionFile(solution, broken);
                EXPECT_FALSE(io::VerifySolutionFiles(puzzle, solution, 300).Passed());
                broken = machine;
                broken.objects.erase(broken.objects.begin());
                io::BinaryWriter::WriteSolutionFile(solution, broken);
                EXPECT_FALSE(io::VerifySolutionFiles(puzzle, solution, 300).Passed());
            }
}
TEST(ProductionRouting, IndependentlyVerifiesSerialSupplyAndRejectsBrokenHandoff) {
    const auto base = std::filesystem::current_path() /
                      ("production-route-" + std::to_string(opusoptima::test::ProcessId()));
    const auto puzzle = base.string() + ".puzzle", solution = base.string() + ".solution";
    struct Cleanup {
        std::string a, b;
        ~Cleanup() {
            std::filesystem::remove(a);
            std::filesystem::remove(b);
        }
    } cleanup{puzzle, solution};
    for (int count : {3, 4, 6})
        for (bool isolation : {false, true})
            for (bool branch : {false, true}) {
                SCOPED_TRACE(count);
                SCOPED_TRACE(isolation);
                SCOPED_TRACE(branch);
                auto p = Route(count);
                p.productionInfo->isolation = isolation;
                if (branch && count > 3) p.productionInfo->conduits.back().positionA = {10, 0};
                Write(puzzle, p);
                auto parsed = io::BinaryParser::ParsePuzzleFile(puzzle);
                solvers::AssemblySolver solver;
                int accepted = 0;
                auto machine = solver.SolveAccepted(parsed, [&](const io::SolutionData& candidate) {
                    ++accepted;
                    io::BinaryWriter::WriteSolutionFile(solution, candidate);
                    return io::VerifySolutionFiles(puzzle, solution, 300).Passed();
                });
                EXPECT_EQ(accepted, 1);
                io::BinaryWriter::WriteSolutionFile(solution, machine);
                const auto report = io::VerifySolutionFiles(puzzle, solution, 300);
                ASSERT_TRUE(report.Passed()) << report.detail << " cycle=" << report.errorCycle;
                machine.objects.erase(machine.objects.begin() + 2);
                io::BinaryWriter::WriteSolutionFile(solution, machine);
                EXPECT_FALSE(io::VerifySolutionFiles(puzzle, solution, 300).Passed());
            }
}
#endif
