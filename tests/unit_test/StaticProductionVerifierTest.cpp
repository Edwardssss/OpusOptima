#include "Verification/FiniteTapeExpansion.hpp"
#include "Verification/IndependentMetrics.hpp"
#include "Verification/StaticProductionVerifier.hpp"
#include "Verification/IndependentVerifier.hpp"
#include "BinaryWriter.hpp"
#include "ProductionMolecular.hpp"

#include "UnitTestConstants.hpp"
#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <limits>
#include <utility>
#include "RepoRoot.hpp"
#ifdef HAS_LIBVERIFY
#    include "libverify.h"
#endif

namespace opusoptima::verification { namespace {
const auto root = opusoptima::test::RepoRoot(__FILE__);
io::PuzzleData Conduit() {
    return io::BinaryParser::ParsePuzzleFile(root /
                                             "asset/puzzle/edge-cases/c544897394273089.puzzle");
}
TEST(IndependentStaticProduction, OriginalConduitExecutesWithoutSolverIRAndConservesEveryAtom) {
    const auto puzzle = Conduit();
    const auto machine = solvers::CompileProductionMolecular(puzzle);
    const auto report = VerifyStaticProduction(puzzle, machine);
    ASSERT_TRUE(report.Passed()) << report.detail;
    EXPECT_EQ(report.cycles, 43);
    EXPECT_EQ(report.outputs, (std::vector<std::size_t>{6}));
    EXPECT_EQ(report.disposed, 10U);
    EXPECT_EQ(report.acquired, report.disposed + 6 + report.remaining);
    EXPECT_EQ(std::count_if(report.events.begin(), report.events.end(),
                            [](const auto& event) {
                                return event.kind == StaticProductionEvent::Kind::Transfer;
                            }),
              6);
    const auto file = root / ("build/independent-conduit-" +
                              std::to_string(opusoptima::test::ProcessId()) + ".solution");
    struct Cleanup {
        std::filesystem::path path;
        ~Cleanup() {
            std::filesystem::remove(path);
        }
    } cleanup{file};
    io::BinaryWriter::WriteSolutionFile(file, machine);
    const auto decoded =
        VerifyStaticProductionFiles(root / "asset/puzzle/edge-cases/c544897394273089.puzzle", file);
    EXPECT_TRUE(decoded.Passed()) << decoded.detail;
    EXPECT_EQ(decoded.cycles, report.cycles);
}
TEST(IndependentStaticProduction, ConduitCellPermutationsPreserveSetsButNotOrderedFootprints) {
    const auto puzzle = Conduit();
    const auto machine = solvers::CompileProductionMolecular(puzzle);
    std::vector<std::size_t> endpoints;
    for (std::size_t i = 0; i < machine.objects.size(); ++i)
        if (machine.objects[i].typeName == "pipe") endpoints.push_back(i);
    ASSERT_EQ(endpoints.size(), 2U);
    const auto& path = puzzle.productionInfo->conduits.at(0).path;
    ASSERT_EQ(path.size(), 3U);
#ifdef HAS_LIBVERIFY
    const auto file = root / ("build/independent-conduit-order-" +
                              std::to_string(opusoptima::test::ProcessId()) + ".solution");
    struct Cleanup {
        std::filesystem::path path;
        ~Cleanup() { std::filesystem::remove(path); }
    } cleanup{file};
#endif
    std::array<int, 3> order{0, 1, 2};
    do {
        const bool unchanged = order == std::array<int, 3>{0, 1, 2};
        for (int mask = 1; mask <= (unchanged ? 1 : 3); ++mask) {
            SCOPED_TRACE(::testing::Message() << "order=" << order[0] << order[1] << order[2]
                                             << " endpoint mask=" << mask);
            auto modified = machine;
            for (std::size_t side = 0; side < endpoints.size(); ++side) {
                if (!(mask & (1 << side))) continue;
                auto& body = modified.objects[endpoints[side]].bodyData;
                for (std::size_t cell = 0; cell < order.size(); ++cell) {
                    body.at(7 + 2 * cell) = path[order[cell]].q;
                    body.at(8 + 2 * cell) = path[order[cell]].r;
                }
            }
            // Each permutation preserves the exact cell set, count and conduit identity.
            const auto report = VerifyStaticProduction(puzzle, modified, 100);
            EXPECT_EQ(report.status, unchanged ? io::VerificationStatus::Passed
                                               : io::VerificationStatus::Rejected)
                << report.detail;
            if (!unchanged) {
                EXPECT_EQ(report.detail, "altered ordered conduit footprint");
            }
            const auto certified = VerifyIndependent(puzzle, modified, 100, false);
            EXPECT_EQ(certified.status, io::VerificationStatus::Unavailable);
            EXPECT_EQ(certified.detail, "production-wall-profile-uncertified");
#ifdef HAS_LIBVERIFY
            io::BinaryWriter::WriteSolutionFile(file, modified);
            EXPECT_EQ(VerifyStaticProductionFiles(
                          root / "asset/puzzle/edge-cases/c544897394273089.puzzle", file, 100)
                          .status,
                      report.status);
            void* native = verifier_create(
                (root / "asset/puzzle/edge-cases/c544897394273089.puzzle").c_str(), file.c_str());
            ASSERT_NE(native, nullptr);
            verifier_set_cycle_limit(native, 100);
            const auto cycles = verifier_evaluate_metric(native, "cycles");
            const auto* error = verifier_error(native);
            if (unchanged) {
                EXPECT_EQ(error, nullptr) << (error ? error : "");
                EXPECT_EQ(cycles, report.cycles);
            } else {
                EXPECT_EQ(verifier_evaluate_metric(native, "conduit violations"), 1);
                EXPECT_EQ(verifier_error(native), nullptr);
            }
            EXPECT_EQ(verifier_evaluate_metric(native, "cabinet violations"), unchanged ? 0 : 1);
            verifier_destroy(native);
#endif
        }
    } while (std::next_permutation(order.begin(), order.end()));
}
TEST(IndependentStaticProduction, TapeGatePreservesEntryStatusAndErrorOrder) {
    const auto puzzlePath = root / "asset/puzzle/edge-cases/c544897394273089.puzzle";
    const auto puzzle = Conduit();
    const auto original = solvers::CompileProductionMolecular(puzzle);
    const auto file = root / ("build/static-tape-gate-" +
                              std::to_string(opusoptima::test::ProcessId()) + ".solution");
    struct Cleanup {
        std::filesystem::path file;
        ~Cleanup() {
            std::error_code error;
            std::filesystem::remove(file, error);
        }
    } cleanup{file};
    const auto checkMachine = [&](const io::SolutionData& machine,
                                  io::VerificationStatus status, const std::string& detail) {
        const auto direct = VerifyStaticProduction(puzzle, machine, 100);
        io::BinaryWriter::WriteSolutionFile(file, machine);
        const auto decoded = VerifyStaticProductionFiles(puzzlePath, file, 100);
        EXPECT_EQ(direct.status, status) << direct.detail;
        EXPECT_EQ(direct.detail, detail);
        EXPECT_EQ(decoded.status, direct.status) << decoded.detail;
        EXPECT_EQ(decoded.detail, direct.detail);
    };
    const auto arm = std::find_if(original.objects.begin(), original.objects.end(),
                                  [](const auto& part) { return part.typeName == "arm1"; });
    ASSERT_NE(arm, original.objects.end());
    const auto index = static_cast<std::size_t>(arm - original.objects.begin());
    for (const int offset : {-20000, 19993}) {
        SCOPED_TRACE(offset);
        auto machine = original;
        for (auto& instruction : machine.objects[index].instructions)
            instruction.parameter += offset;
        const auto direct = VerifyStaticProduction(puzzle, machine, 100);
        ASSERT_TRUE(direct.Passed()) << direct.detail;
        checkMachine(machine, direct.status, direct.detail);
    }
    const auto check = [&](const std::vector<io::ArmInstruction>& tape,
                           io::VerificationStatus status, const std::string& detail) {
        auto machine = original;
        machine.objects[index].instructions = tape;
        checkMachine(machine, status, detail);
    };
    const auto rejected = io::VerificationStatus::Rejected;
    const auto unavailable = io::VerificationStatus::Unavailable;
    const std::string duplicate = "invalid or duplicate tape position";
    const std::string unknown = "unexpanded repeat/reset or unknown independent instruction";
    for (const int position : {-20001, 20001, std::numeric_limits<std::int32_t>::min(),
                               std::numeric_limits<std::int32_t>::max()}) {
        SCOPED_TRACE(position);
        for (const char action : {'B', 'Z'})
            check({{action, position}}, rejected, duplicate);
    }
    check({{'B', 0}, {'B', 0}}, rejected, duplicate);
    check({{'B', 0}, {'Z', 0}}, rejected, duplicate);
    check({{'Z', 0}, {'B', 20001}}, unavailable, unknown);
    check({{'Z', 0}, {'B', 0}}, unavailable, unknown);
    check({{'B', 0}, {'B', 0}, {'B', 20001}}, rejected, duplicate);
    {
        std::ofstream stream(file, std::ios::binary | std::ios::trunc);
        stream.put('\a');
    }
    const auto broken = VerifyStaticProductionFiles(puzzlePath, file, 100);
    EXPECT_EQ(broken.status, io::VerificationStatus::Error);
    EXPECT_EQ(broken.detail,
              "independent file decoding failed: Unexpected EOF while reading i32");
}
TEST(IndependentStaticProduction, ConduitMutationsAndOccupancyDoNotPass) {
    const auto puzzle = Conduit();
    const auto machine = solvers::CompileProductionMolecular(puzzle);
    for (const auto* kind : {"pipe", "unbonder", "glyph-disposal", "out-std"}) {
        auto modified = machine;
        const auto part = std::find_if(modified.objects.begin(), modified.objects.end(),
                                       [&](const auto& part) { return part.typeName == kind; });
        ASSERT_NE(part, modified.objects.end());
        modified.objects.erase(part);
        const auto rejected = VerifyStaticProduction(puzzle, modified, 100);
        EXPECT_FALSE(rejected.Passed()) << kind;
    }
    auto wrong = machine;
    const auto pipe = std::find_if(wrong.objects.begin(), wrong.objects.end(),
                                   [](const auto& part) { return part.typeName == "pipe"; });
    ASSERT_NE(pipe, wrong.objects.end());
    pipe->bodyData.at(7) = 1;
    EXPECT_EQ(VerifyStaticProduction(puzzle, wrong).status, io::VerificationStatus::Rejected);
    auto occupied = machine;
    const auto output = std::find_if(occupied.objects.begin(), occupied.objects.end(),
                                     [](const auto& part) { return part.typeName == "out-std"; });
    ASSERT_NE(output, occupied.objects.end());
    occupied.objects.push_back({"input", output->position, {}, {1, 0, 1, 0, 0}});
    auto obstructed = puzzle;
    obstructed.inputs[1].atoms[0].type = core::AtomType::Fire;
    EXPECT_EQ(VerifyStaticProduction(obstructed, occupied).status,
              io::VerificationStatus::Rejected);
    EXPECT_EQ(VerifyStaticProduction(puzzle, machine, 2).status,
              io::VerificationStatus::CycleLimit);
}
#ifdef HAS_LIBVERIFY
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
    word(puzzle.productionInfo.has_value(), 1);
    if (!puzzle.productionInfo) return;
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
        for (auto c : conduit.path)
            cell(c);
    }
    word(production.vials.size(), 4);
    for (const auto& vial : production.vials) {
        cell(vial.position);
        word(vial.top, 1);
        word(vial.count, 4);
    }
}
io::PuzzleData ChemistryPuzzle() {
    io::PuzzleData puzzle;
    puzzle.version = 3;
    puzzle.name = "independent differential fixture";
    puzzle.permissionBits = std::numeric_limits<std::uint64_t>::max();
    puzzle.outputMultiplier = 1;
    puzzle.productionInfo = io::ProductionInfo{};
    puzzle.productionInfo->chambers.push_back({{}, "Large"});
    return puzzle;
}
void Differential(const io::PuzzleData& puzzle, const io::SolutionData& machine,
                  bool completes = true) {
    const auto stem =
        root / ("build/independent-chemistry-" + std::to_string(opusoptima::test::ProcessId()));
    const std::filesystem::path puzzlePath = stem.string() + ".puzzle",
                                solutionPath = stem.string() + ".solution";
    struct Cleanup {
        std::filesystem::path a, b;
        ~Cleanup() {
            std::filesystem::remove(a);
            std::filesystem::remove(b);
        }
    } cleanup{puzzlePath, solutionPath};
    WritePuzzle(puzzlePath, puzzle);
    io::BinaryWriter::WriteSolutionFile(solutionPath, machine);
    const auto independent = VerifyStaticProductionFiles(puzzlePath, solutionPath, 100);
    void* native = verifier_create(puzzlePath.c_str(), solutionPath.c_str());
    ASSERT_NE(native, nullptr);
    verifier_set_cycle_limit(native, 100);
    const auto cycles = verifier_evaluate_metric(native, "cycles");
    const auto* error = verifier_error(native);
    if (completes) {
        EXPECT_EQ(error, nullptr) << (error ? error : "");
        EXPECT_TRUE(independent.Passed()) << independent.detail;
        if (!error && independent.Passed()) EXPECT_EQ(cycles, independent.cycles);
    } else {
        EXPECT_NE(error, nullptr);
        EXPECT_FALSE(independent.Passed()) << independent.detail;
    }
    verifier_destroy(native);
}
void Feed(io::PuzzleData& puzzle, io::SolutionData& machine, int type, core::HexCoord cell) {
    const auto index = static_cast<int>(puzzle.inputs.size());
    puzzle.inputs.push_back({{{static_cast<core::AtomType>(type), {}}}, {}});
    machine.objects.push_back({"input", cell, {}, {1, 0, index, 0, 0}});
}
void Product(io::PuzzleData& puzzle, io::SolutionData& machine, int type, core::HexCoord cell) {
    const auto index = static_cast<int>(puzzle.outputs.size());
    puzzle.outputs.push_back({{{static_cast<core::AtomType>(type), {}}}, {}});
    machine.objects.push_back({"out-std", cell, {}, {1, 0, index, 0, 0}});
}
std::pair<io::PuzzleData, io::SolutionData> FiniteRepeatingPrefixFixture() {
    auto puzzle = ChemistryPuzzle();
    puzzle.productionInfo.reset();
    puzzle.name = "finite repeating prefix";
    io::Molecule material;
    for (int q = 0; q < 6; ++q)
        material.atoms.push_back({core::AtomType::Salt, {q, 0}});
    for (int q = 0; q < 5; ++q)
        material.bonds.push_back({1, {q, 0}, {q + 1, 0}});
    puzzle.inputs.push_back(material);
    puzzle.outputs.push_back({{{core::AtomType::Salt, {0, 0}},
                               {core::AtomType::Repeat, {1, 0}}},
                              {{1, {0, 0}, {1, 0}}}});
    io::SolutionData machine;
    machine.puzzleName = puzzle.name;
    machine.objects = {{"input", {}, {}, {1, 0, 0, 0, 0}},
                       {"out-rep", {}, {}, {1, 0, 0, 0, 0}},
                       {"arm1", {0, -5}, {{'G', 0}, {'g', 1}}, {1, 0, 0, 0, 0}}};
    return {puzzle, machine};
}
TEST(IndependentStaticProduction, FiniteRepeatingPrefixRequiresIntermonomerBonds) {
    auto [puzzle, machine] = FiniteRepeatingPrefixFixture();
    const auto stem = root / ("build/finite-repeating-prefix-" +
                              std::to_string(opusoptima::test::ProcessId()));
    const std::filesystem::path puzzlePath = stem.string() + ".puzzle",
                                solutionPath = stem.string() + ".solution";
    struct Cleanup {
        std::filesystem::path a, b;
        ~Cleanup() { std::filesystem::remove(a); std::filesystem::remove(b); }
    } cleanup{puzzlePath, solutionPath};
    io::BinaryWriter::WriteSolutionFile(solutionPath, machine);
    int referenceCases = 0;
    for (bool connected : {true, false}) {
        SCOPED_TRACE(connected ? "six connected monomers" : "missing boundary 2--3");
        auto fixture = puzzle;
        if (!connected) fixture.inputs[0].bonds.erase(fixture.inputs[0].bonds.begin() + 2);
        WritePuzzle(puzzlePath, fixture);
        const auto decoded = io::BinaryParser::ParsePuzzleFile(puzzlePath);
        EXPECT_EQ(decoded.outputMultiplier, 1);
        ASSERT_EQ(decoded.inputs[0].atoms.size(), 6U);
        for (int q = 0; q < 6; ++q) {
            EXPECT_EQ(decoded.inputs[0].atoms[q].type, core::AtomType::Salt);
            EXPECT_EQ(decoded.inputs[0].atoms[q].position, (core::HexCoord{q, 0}));
        }
        ASSERT_EQ(decoded.inputs[0].bonds.size(), connected ? 5U : 4U);
        for (std::size_t i = 0; i < decoded.inputs[0].bonds.size(); ++i) {
            const int q = static_cast<int>(i) + (!connected && i >= 2 ? 1 : 0);
            EXPECT_EQ(decoded.inputs[0].bonds[i].bits, 1);
            EXPECT_EQ(decoded.inputs[0].bonds[i].from, (core::HexCoord{q, 0}));
            EXPECT_EQ(decoded.inputs[0].bonds[i].to, (core::HexCoord{q + 1, 0}));
        }
        ASSERT_EQ(decoded.outputs[0].atoms.size(), 2U);
        EXPECT_EQ(decoded.outputs[0].atoms[0].type, core::AtomType::Salt);
        EXPECT_EQ(decoded.outputs[0].atoms[1].type, core::AtomType::Repeat);
        EXPECT_EQ(decoded.outputs[0].atoms[1].position, (core::HexCoord{1, 0}));
        ASSERT_EQ(decoded.outputs[0].bonds.size(), 1U);
        EXPECT_EQ(decoded.outputs[0].bonds[0].bits, 1);
        EXPECT_EQ(decoded.outputs[0].bonds[0].from, (core::HexCoord{0, 0}));
        EXPECT_EQ(decoded.outputs[0].bonds[0].to, (core::HexCoord{1, 0}));
        const auto parsedMachine = io::BinaryParser::ParseSolutionFile(solutionPath);
        ASSERT_EQ(parsedMachine.objects.size(), 3U);
        EXPECT_EQ(parsedMachine.objects[1].typeName, "out-rep");
        EXPECT_EQ(parsedMachine.objects[1].bodyData, machine.objects[1].bodyData);
        const auto direct = VerifyIndependent(fixture, machine, 20);
        const auto functional = ReplayRepeatingFiniteMachine(fixture, machine, 20);
        EXPECT_FALSE(functional.certificationEligible);
        EXPECT_EQ(functional.execution.outcome, connected ? ReplayOutcome::Completed
                                                        : ReplayOutcome::Unavailable);
        if (connected) {
            EXPECT_EQ(functional.execution.cycles, 1);
            EXPECT_EQ(functional.execution.outputs, (std::vector<std::size_t>{6}));
            EXPECT_EQ(functional.execution.remaining, 6U);
        }
        const auto files = VerifyIndependentFiles(puzzlePath, solutionPath, 20);
        for (const auto* report : {&direct, &files}) {
            EXPECT_EQ(report->status, io::VerificationStatus::Unavailable);
            EXPECT_EQ(report->detail, connected
                ? "independent profile excludes Repeat/unknown elements"
                : "independent disjoint-input links not yet established");
        }
        void* native = verifier_create(puzzlePath.c_str(), solutionPath.c_str());
        ASSERT_NE(native, nullptr);
        struct Destroy { void* value; ~Destroy() { verifier_destroy(value); } } destroy{native};
        ASSERT_EQ(verifier_error(native), nullptr);
        verifier_set_cycle_limit(native, 20);
        // Literal chain has six real atoms, not six copies of the raw two-atom marker form.
        // Removing one boundary leaves the same six Salt atoms but no connected six-mer.
        const int cycles = verifier_evaluate_metric(native, "product 6 cycles");
        ++referenceCases;
        if (connected) {
            EXPECT_EQ(verifier_error(native), nullptr);
            EXPECT_EQ(cycles, 1);
        } else {
            EXPECT_EQ(cycles, -1);
            ASSERT_NE(verifier_error(native), nullptr);
            EXPECT_STREQ(verifier_error(native), "solution did not complete within cycle limit");
        }
        verifier_error_clear(native);
        EXPECT_EQ(verifier_evaluate_metric(native, "product 7 cycles"), -1);
        ++referenceCases;
        ASSERT_NE(verifier_error(native), nullptr);
        EXPECT_STREQ(verifier_error(native), "solution did not complete within cycle limit");
    }
    RecordProperty("reference_cases", referenceCases);
    EXPECT_EQ(referenceCases, 4);
}
TEST(IndependentStaticProduction, FiniteRepeatingPrefixRequiresOrderedHeterogeneousMaterials) {
    auto [puzzle, machine] = FiniteRepeatingPrefixFixture();
    // The codec writes the instruction vector's count, not a stale bodyData count.
    machine.objects[2].bodyData[3] = static_cast<int>(machine.objects[2].instructions.size());
    puzzle.inputs[0].atoms.clear();
    puzzle.inputs[0].bonds.clear();
    for (int q = 0; q < 12; ++q)
        puzzle.inputs[0].atoms.push_back(
            {q % 2 == 0 ? core::AtomType::Salt : core::AtomType::Fire, {q, 0}});
    for (int q = 0; q < 11; ++q)
        puzzle.inputs[0].bonds.push_back({1, {q, 0}, {q + 1, 0}});
    puzzle.outputs[0] = {{{core::AtomType::Salt, {0, 0}},
                           {core::AtomType::Fire, {1, 0}},
                           {core::AtomType::Repeat, {2, 0}}},
                          {{1, {0, 0}, {1, 0}}, {1, {1, 0}, {2, 0}}}};
    const auto stem = root / ("build/finite-repeating-material-" +
                              std::to_string(opusoptima::test::ProcessId()));
    const std::filesystem::path puzzlePath = stem.string() + ".puzzle",
                                solutionPath = stem.string() + ".solution";
    struct Cleanup {
        std::filesystem::path a, b;
        ~Cleanup() { std::filesystem::remove(a); std::filesystem::remove(b); }
    } cleanup{puzzlePath, solutionPath};
    io::BinaryWriter::WriteSolutionFile(solutionPath, machine);
    int referenceCases = 0;
    for (bool misplaced : {false, true}) {
        SCOPED_TRACE(misplaced ? "third monomer Fire/Salt" : "all monomers Salt/Fire");
        auto fixture = puzzle;
        if (misplaced)
            std::swap(fixture.inputs[0].atoms[4].type, fixture.inputs[0].atoms[5].type);
        // Template displacement is (2,0): Salt@(2k,0), Fire@(2k+1,0).
        // All eleven bonds stay fixed, including Fire@(2k+1)--Salt@(2k+2).
        // Swapping only monomer 2 preserves the multiset and the first two valid monomers.
        for (const auto type : {core::AtomType::Salt, core::AtomType::Fire})
            EXPECT_EQ(std::count_if(fixture.inputs[0].atoms.begin(), fixture.inputs[0].atoms.end(),
                                    [=](const auto& atom) { return atom.type == type; }), 6);
        WritePuzzle(puzzlePath, fixture);
        const auto decoded = io::BinaryParser::ParsePuzzleFile(puzzlePath);
        EXPECT_EQ(decoded.outputMultiplier, 1);
        ASSERT_EQ(decoded.inputs.size(), 1U);
        ASSERT_EQ(decoded.outputs.size(), 1U);
        for (const auto& pair : {std::pair{&fixture.inputs[0], &decoded.inputs[0]},
                                std::pair{&fixture.outputs[0], &decoded.outputs[0]}}) {
            const auto& expected = *pair.first;
            const auto& actual = *pair.second;
            ASSERT_EQ(actual.atoms.size(), expected.atoms.size());
            ASSERT_EQ(actual.bonds.size(), expected.bonds.size());
            for (std::size_t i = 0; i < expected.atoms.size(); ++i) {
                EXPECT_EQ(actual.atoms[i].type, expected.atoms[i].type);
                EXPECT_EQ(actual.atoms[i].position, expected.atoms[i].position);
            }
            for (std::size_t i = 0; i < expected.bonds.size(); ++i) {
                EXPECT_EQ(actual.bonds[i].bits, expected.bonds[i].bits);
                EXPECT_EQ(actual.bonds[i].from, expected.bonds[i].from);
                EXPECT_EQ(actual.bonds[i].to, expected.bonds[i].to);
            }
        }
        const auto parsedMachine = io::BinaryParser::ParseSolutionFile(solutionPath);
        ASSERT_EQ(parsedMachine.objects.size(), machine.objects.size());
        for (std::size_t i = 0; i < machine.objects.size(); ++i) {
            EXPECT_EQ(parsedMachine.objects[i].typeName, machine.objects[i].typeName);
            EXPECT_EQ(parsedMachine.objects[i].position, machine.objects[i].position);
            EXPECT_EQ(parsedMachine.objects[i].bodyData, machine.objects[i].bodyData);
            ASSERT_EQ(parsedMachine.objects[i].instructions.size(), machine.objects[i].instructions.size());
            for (std::size_t j = 0; j < machine.objects[i].instructions.size(); ++j) {
                EXPECT_EQ(parsedMachine.objects[i].instructions[j].action,
                          machine.objects[i].instructions[j].action);
                EXPECT_EQ(parsedMachine.objects[i].instructions[j].parameter,
                          machine.objects[i].instructions[j].parameter);
            }
        }
        const auto direct = VerifyIndependent(fixture, machine, 20);
        const auto files = VerifyIndependentFiles(puzzlePath, solutionPath, 20);
        for (const auto* report : {&direct, &files}) {
            EXPECT_EQ(report->status, io::VerificationStatus::Unavailable);
            EXPECT_EQ(report->detail, "independent profile excludes Repeat/unknown elements");
        }
        void* native = verifier_create(puzzlePath.c_str(), solutionPath.c_str());
        ASSERT_NE(native, nullptr);
        struct Destroy { void* value; ~Destroy() { verifier_destroy(value); } } destroy{native};
        ASSERT_EQ(verifier_error(native), nullptr);
        verifier_set_cycle_limit(native, 20);
        for (const int threshold : {2, 3, 6}) {
            SCOPED_TRACE(threshold);
            verifier_error_clear(native);
            const auto metric = "product " + std::to_string(threshold) + " cycles";
            const int cycles = verifier_evaluate_metric(native, metric.c_str());
            ++referenceCases;
            const bool matches = !misplaced || threshold <= 2;
            EXPECT_EQ(cycles, matches ? 1 : -1);
            if (matches) {
                EXPECT_EQ(verifier_error(native), nullptr);
            } else {
                ASSERT_NE(verifier_error(native), nullptr);
                EXPECT_STREQ(verifier_error(native), "solution did not complete within cycle limit");
            }
        }
    }
    RecordProperty("reference_cases", referenceCases);
    EXPECT_EQ(referenceCases, 6);
}
TEST(IndependentStaticProduction, ConduitCapturesOnlyOnReleaseAndArrivesThatCycle) {
    // Locked omsim fill_conduits requires BEING_DROPPED and !GRABBED;
    // apply_conduit consumes in half 1 and produces in half 2. A stationary
    // one-atom feed needs no geometric or chemistry oracle.
    const auto stem = root / ("build/independent-conduit-release-" +
                              std::to_string(opusoptima::test::ProcessId()));
    const std::filesystem::path puzzlePath = stem.string() + ".puzzle",
                                solutionPath = stem.string() + ".solution";
    struct Cleanup {
        std::filesystem::path a, b;
        ~Cleanup() {
            std::error_code error;
            std::filesystem::remove(a, error);
            std::filesystem::remove(b, error);
        }
    } cleanup{puzzlePath, solutionPath};
    for (const int release : {-1, 2, 3}) {
        SCOPED_TRACE(release);
        auto puzzle = ChemistryPuzzle();
        puzzle.productionInfo->chambers = {{{0, 0}, "Large"}, {{9, 0}, "Large"}};
        puzzle.productionInfo->conduits = {{{0, 0}, {9, 0}, {{0, 0}}}};
        io::SolutionData machine;
        Feed(puzzle, machine, 1, {0, 0});
        Product(puzzle, machine, 1, {9, 0});
        machine.objects.push_back({"pipe", {0, 0}, {}, {1, 0, 0, 0, 0, 100, 1, 0, 0}});
        machine.objects.push_back({"pipe", {9, 0}, {}, {1, 0, 0, 0, 0, 100, 1, 0, 0}});
        const int period = release < 0 ? 4 : release + 2;
        std::vector<io::ArmInstruction> tape{{'G', 0}, {'O', period - 1}};
        if (release >= 0) tape.push_back({'g', release});
        machine.objects.push_back({"arm1", {-1, 0}, tape, {1, 0, 0, 0, 0}});
        const auto count = [](const auto& report, StaticProductionEvent::Kind kind) {
            return std::count_if(report.events.begin(), report.events.end(),
                                 [&](const auto& event) { return event.kind == kind; });
        };
        const int heldCycles = release < 0 ? 30 : release;
        const auto held = VerifyStaticProduction(puzzle, machine, heldCycles);
        ASSERT_EQ(held.status, io::VerificationStatus::CycleLimit) << held.detail;
        EXPECT_EQ(held.cycles, heldCycles);
        EXPECT_EQ(held.acquired, 1U);
        EXPECT_EQ(held.remaining, 1U);
        EXPECT_EQ(held.disposed, 0U);
        EXPECT_EQ(held.outputs, (std::vector<std::size_t>{0}));
        EXPECT_EQ(count(held, StaticProductionEvent::Kind::Transfer), 0);
        const auto report = VerifyStaticProduction(puzzle, machine, 40);
        if (release < 0) {
            EXPECT_EQ(report.status, io::VerificationStatus::CycleLimit) << report.detail;
            EXPECT_EQ(report.acquired, 1U);
            EXPECT_EQ(report.remaining, 1U);
            EXPECT_EQ(report.outputs, (std::vector<std::size_t>{0}));
            EXPECT_EQ(count(report, StaticProductionEvent::Kind::Transfer), 0);
        } else {
            ASSERT_TRUE(report.Passed()) << report.detail;
            EXPECT_EQ(report.cycles, release + 5 * period + 1);
            EXPECT_EQ(report.outputs, (std::vector<std::size_t>{6}));
            EXPECT_EQ(report.acquired, 7U);
            EXPECT_EQ(report.remaining, 1U);
            EXPECT_EQ(report.disposed, 0U);
            EXPECT_EQ(report.acquired, report.outputs[0] + report.remaining);
            EXPECT_EQ(count(report, StaticProductionEvent::Kind::Transfer), 6);
            EXPECT_EQ(count(report, StaticProductionEvent::Kind::Output), 6);
            std::vector<std::vector<std::uint64_t>> acquired, transferred, emitted;
            for (const auto& event : report.events) {
                if (event.kind == StaticProductionEvent::Kind::Acquire)
                    acquired.push_back(event.atoms);
                if (event.kind == StaticProductionEvent::Kind::Transfer ||
                    event.kind == StaticProductionEvent::Kind::Output) {
                    auto& sequence = event.kind == StaticProductionEvent::Kind::Transfer
                                         ? transferred : emitted;
                    EXPECT_EQ(event.cycle, release + static_cast<int>(sequence.size()) * period);
                    EXPECT_EQ(event.phase, StaticProductionEvent::Phase::AfterMotion);
                    ASSERT_EQ(event.atoms.size(), 1U);
                    sequence.push_back(event.atoms);
                }
            }
            ASSERT_EQ(acquired.size(), 7U);
            acquired.pop_back();
            EXPECT_EQ(transferred, acquired);
            EXPECT_EQ(emitted, acquired);
        }
        WritePuzzle(puzzlePath, puzzle);
        io::BinaryWriter::WriteSolutionFile(solutionPath, machine);
        const auto decoded = VerifyStaticProductionFiles(puzzlePath, solutionPath, 40);
        EXPECT_EQ(decoded.status, report.status) << decoded.detail;
        EXPECT_EQ(decoded.cycles, report.cycles);
        EXPECT_EQ(decoded.acquired, report.acquired);
        EXPECT_EQ(decoded.remaining, report.remaining);
        EXPECT_EQ(decoded.outputs, report.outputs);
        const auto certified = VerifyIndependent(puzzle, machine, 40, false);
        EXPECT_EQ(certified.status, io::VerificationStatus::Unavailable);
        EXPECT_EQ(certified.detail, "production-wall-profile-uncertified");
        void* native = verifier_create(puzzlePath.c_str(), solutionPath.c_str());
        ASSERT_NE(native, nullptr);
        struct NativeCleanup {
            void* handle;
            ~NativeCleanup() { verifier_destroy(handle); }
        } nativeCleanup{native};
        verifier_set_cycle_limit(native, 40);
        EXPECT_EQ(verifier_evaluate_metric(native, "cabinet violations"), 0);
        EXPECT_EQ(verifier_error(native), nullptr);
        const auto cycles = verifier_evaluate_metric(native, "cycles");
        if (release < 0) {
            EXPECT_NE(verifier_error(native), nullptr);
        } else {
            EXPECT_EQ(verifier_error(native), nullptr);
            EXPECT_EQ(cycles, release + 5 * period + 1);
        }
    }
    RecordProperty("reference_cases", "3");
}
TEST(IndependentStaticProduction, SixRoomStaticGlyphCellsMatchLockedCabinetRules) {
    // Pinned omsim decode.c:294-306 enumerates cells, not continuous wall contact.
    struct Shape {
        const char* name;
        std::array<core::HexCoord, 6> inside, wall, outside;
        std::vector<core::HexCoord> seams;
    };
    const Shape shapes[]{
        {"Small", {{{1,0},{0,1},{-1,1},{-1,0},{0,-1},{1,-1}}},
         {{{2,0},{0,2},{-2,2},{-2,0},{0,-2},{2,-2}}},
         {{{3,0},{0,3},{-3,3},{-3,0},{0,-3},{3,-3}}}, {}},
        {"SmallWide", {{{2,0},{1,1},{-1,1},{-1,0},{0,-1},{2,-1}}},
         {{{3,0},{1,2},{-2,2},{-2,0},{0,-2},{3,-2}}},
         {{{4,0},{1,3},{-3,3},{-3,0},{0,-3},{4,-3}}}, {{1,1},{1,-1}}},
        {"SmallWider", {{{3,0},{2,1},{-1,1},{-1,0},{0,-1},{3,-1}}},
         {{{4,0},{2,2},{-2,2},{-2,0},{0,-2},{4,-2}}},
         {{{5,0},{2,3},{-3,3},{-3,0},{0,-3},{5,-3}}}, {{1,1},{2,-1}}},
        {"Medium", {{{2,0},{0,2},{-2,2},{-2,0},{0,-2},{2,-2}}},
         {{{3,0},{0,3},{-3,3},{-3,0},{0,-3},{3,-3}}},
         {{{4,0},{0,4},{-4,4},{-4,0},{0,-4},{4,-4}}}, {}},
        {"MediumWide", {{{3,0},{1,2},{-2,2},{-2,0},{0,-2},{3,-2}}},
         {{{4,0},{1,3},{-3,3},{-3,0},{0,-3},{4,-3}}},
         {{{5,0},{1,4},{-4,4},{-4,0},{0,-4},{5,-4}}}, {{1,2},{1,-2}}},
        {"Large", {{{3,0},{0,3},{-3,3},{-3,0},{0,-3},{3,-3}}},
         {{{4,0},{0,4},{-4,4},{-4,0},{0,-4},{4,-4}}},
         {{{5,0},{0,5},{-5,5},{-5,0},{0,-5},{5,-5}}}, {}}
    };
    const auto stem = root / ("build/independent-room-cells-" +
                              std::to_string(opusoptima::test::ProcessId()));
    const std::filesystem::path puzzlePath = stem.string() + ".puzzle",
                                solutionPath = stem.string() + ".solution";
    struct Cleanup {
        std::filesystem::path a, b;
        ~Cleanup() {
            std::error_code error;
            std::filesystem::remove(a, error);
            std::filesystem::remove(b, error);
        }
    } cleanup{puzzlePath, solutionPath};
    std::size_t references = 0;
    for (const auto& shape : shapes) {
        for (const core::HexCoord center : {core::HexCoord{0, 0}, core::HexCoord{-7, 4}}) {
            auto puzzle = ChemistryPuzzle();
            // Keep the stationary completion control inside omsim's [-16, 15] map.
            puzzle.productionInfo->chambers = {{center, shape.name}, {{9, 0}, "Large"}};
            io::SolutionData control;
            Feed(puzzle, control, 1, {9, 0});
            Product(puzzle, control, 1, {9, 0});
            WritePuzzle(puzzlePath, puzzle);
            const auto check = [&](core::HexCoord offset, bool inside, const char* kind) {
                SCOPED_TRACE(::testing::Message() << shape.name << " " << kind
                    << " center=" << center.q << "," << center.r
                    << " cell=" << offset.q << "," << offset.r);
                auto machine = control;
                machine.objects.push_back({"glyph-calcification",
                    {center.q + offset.q, center.r + offset.r}, {}, {1, 0, 0, 0, 0}});
                const auto direct = VerifyStaticProduction(puzzle, machine, 10);
                EXPECT_EQ(direct.status, inside ? io::VerificationStatus::Passed
                                                : io::VerificationStatus::Rejected)
                    << direct.detail;
                if (!inside) {
                    EXPECT_EQ(direct.detail, "glyph crosses a room boundary");
                }
                io::BinaryWriter::WriteSolutionFile(solutionPath, machine);
                const auto decoded = VerifyStaticProductionFiles(puzzlePath, solutionPath, 10);
                EXPECT_EQ(decoded.status, direct.status) << decoded.detail;
                EXPECT_EQ(decoded.detail, direct.detail);
                const auto certified = VerifyIndependent(puzzle, machine, 10, false);
                EXPECT_EQ(certified.status, io::VerificationStatus::Unavailable);
                EXPECT_EQ(certified.detail, "production-wall-profile-uncertified");
                void* native = verifier_create(puzzlePath.c_str(), solutionPath.c_str());
                ASSERT_NE(native, nullptr);
                struct NativeCleanup {
                    void* handle;
                    ~NativeCleanup() { verifier_destroy(handle); }
                } nativeCleanup{native};
                ++references;
                verifier_set_cycle_limit(native, 10);
                EXPECT_EQ(verifier_evaluate_metric(native, "cabinet violations"), inside ? 0 : 1);
                EXPECT_EQ(verifier_error(native), nullptr);
            };
            for (auto cell : shape.inside) check(cell, true, "interior corner");
            for (auto cell : shape.wall) check(cell, false, "wall corner");
            for (auto cell : shape.outside) check(cell, false, "exterior");
            for (auto cell : shape.seams) check(cell, true, "stretch seam");
        }
    }
    EXPECT_EQ(references, 228U);
    RecordProperty("reference_cases", std::to_string(references));
}
TEST(IndependentStaticProduction, ProjectionTwoCellFootprintMatchesLockedCabinetRules) {
    // Pinned omsim sim.c:1901-1907: PROJECTION occupies (1,0), then (0,0).
    // Literal transformed cells and membership come from decode.c:294-306;
    // neither the production footprint nor its room-radius formula is an oracle.
    struct Case {
        const char* room;
        core::HexCoord anchor;
        int rotation;
        core::HexCoord second;
        bool inside;
    };
    const Case cases[]{
        {"Small", {1,0}, 0, {2,0}, false},
        {"Small", {1,0}, 1, {1,1}, false},
        {"Small", {1,0}, 2, {0,1}, true},
        {"Small", {1,0}, 3, {0,0}, true},
        {"Small", {1,0}, 4, {1,-1}, true},
        {"Small", {1,0}, 5, {2,-1}, false},
        {"Small", {0,1}, 0, {1,1}, false},
        {"SmallWide", {0,1}, 0, {1,1}, true},
        {"SmallWide", {1,1}, 0, {2,1}, false},
        {"SmallWider", {1,1}, 0, {2,1}, true},
    };
    const auto stem = root / ("build/independent-projection-cells-" +
                              std::to_string(opusoptima::test::ProcessId()));
    const std::filesystem::path puzzlePath = stem.string() + ".puzzle",
                                solutionPath = stem.string() + ".solution";
    struct Cleanup {
        std::filesystem::path a, b;
        ~Cleanup() {
            std::error_code error;
            std::filesystem::remove(a, error);
            std::filesystem::remove(b, error);
        }
    } cleanup{puzzlePath, solutionPath};
    std::size_t references = 0;
    for (const auto& test : cases) {
        SCOPED_TRACE(::testing::Message() << test.room << " rotation=" << test.rotation
            << " anchor=" << test.anchor.q << "," << test.anchor.r
            << " second=" << test.second.q << "," << test.second.r);
        auto puzzle = ChemistryPuzzle();
        // Both complete rooms (including walls) fit [-16,15]. No active chemistry
        // or motion near the tested glyph; the salt completion control is isolated.
        puzzle.productionInfo->chambers = {{{0, 0}, test.room}, {{9, 0}, "Large"}};
        io::SolutionData machine;
        Feed(puzzle, machine, 1, {9, 0});
        Product(puzzle, machine, 1, {9, 0});
        machine.objects.push_back({"glyph-projection", test.anchor, {},
                                   {1, test.rotation, 0, 0, 0}});
        const auto direct = VerifyStaticProduction(puzzle, machine, 10);
        EXPECT_EQ(direct.status, test.inside ? io::VerificationStatus::Passed
                                            : io::VerificationStatus::Rejected)
            << direct.detail;
        if (!test.inside) {
            EXPECT_EQ(direct.detail, "glyph crosses a room boundary");
        }
        WritePuzzle(puzzlePath, puzzle);
        io::BinaryWriter::WriteSolutionFile(solutionPath, machine);
        const auto decoded = VerifyStaticProductionFiles(puzzlePath, solutionPath, 10);
        EXPECT_EQ(decoded.status, direct.status) << decoded.detail;
        EXPECT_EQ(decoded.detail, direct.detail);
        const auto certified = VerifyIndependent(puzzle, machine, 10, false);
        EXPECT_EQ(certified.status, io::VerificationStatus::Unavailable);
        EXPECT_EQ(certified.detail, "production-wall-profile-uncertified");
        void* native = verifier_create(puzzlePath.c_str(), solutionPath.c_str());
        ASSERT_NE(native, nullptr);
        struct NativeCleanup {
            void* handle;
            ~NativeCleanup() { verifier_destroy(handle); }
        } nativeCleanup{native};
        ++references;
        verifier_set_cycle_limit(native, 10);
        EXPECT_EQ(verifier_evaluate_metric(native, "cabinet violations"), test.inside ? 0 : 1);
        EXPECT_EQ(verifier_error(native), nullptr);
    }
    EXPECT_EQ(references, 10U);
    RecordProperty("reference_cases", std::to_string(references));
}
TEST(IndependentStaticProduction, DifferentialEveryChemicalGlyphOnDirectFeeds) {
    for (const auto* name :
         {"glyph-calcification", "glyph-duplication", "glyph-projection", "glyph-purification",
          "glyph-life-and-death", "glyph-dispersion", "glyph-unification"}) {
        SCOPED_TRACE(name);
        auto puzzle = ChemistryPuzzle();
        io::SolutionData machine;
        const std::string glyph = name;
        if (glyph == "glyph-calcification") {
            Feed(puzzle, machine, 4, {});
            Product(puzzle, machine, 1, {});
        } else if (glyph == "glyph-duplication") {
            Feed(puzzle, machine, 4, {});
            Feed(puzzle, machine, 1, {1, 0});
            Product(puzzle, machine, 4, {1, 0});
        } else if (glyph == "glyph-projection") {
            Feed(puzzle, machine, 6, {});
            Feed(puzzle, machine, 11, {1, 0});
            Product(puzzle, machine, 10, {1, 0});
        } else if (glyph == "glyph-purification") {
            Feed(puzzle, machine, 11, {});
            Feed(puzzle, machine, 11, {1, 0});
            Product(puzzle, machine, 10, {0, 1});
        } else if (glyph == "glyph-life-and-death") {
            Feed(puzzle, machine, 1, {});
            Feed(puzzle, machine, 1, {1, 0});
            Product(puzzle, machine, 13, {0, 1});
            Product(puzzle, machine, 14, {1, -1});
        } else if (glyph == "glyph-dispersion") {
            Feed(puzzle, machine, 16, {});
            Product(puzzle, machine, 2, {-1, 0});
            Product(puzzle, machine, 3, {1, 0});
            Product(puzzle, machine, 4, {0, -1});
            Product(puzzle, machine, 5, {1, -1});
        } else {
            Feed(puzzle, machine, 2, {0, 1});
            Feed(puzzle, machine, 3, {-1, 1});
            Feed(puzzle, machine, 4, {0, -1});
            Feed(puzzle, machine, 5, {1, -1});
            Product(puzzle, machine, 16, {});
        }
        machine.objects.push_back({glyph, {}, {}, {1, 0, 0, 0, 0}});
        Differential(puzzle, machine);
    }
}
TEST(IndependentStaticProduction, DifferentialMultibonderUsesItsActualThreeSpokes) {
    auto puzzle = ChemistryPuzzle();
    io::SolutionData machine;
    for (auto cell : std::array<core::HexCoord, 4>{{{0, 0}, {1, 0}, {0, -1}, {-1, 1}}})
        Feed(puzzle, machine, 1, cell);
    puzzle.outputs.push_back({{{core::AtomType::Salt, {0, 0}},
                               {core::AtomType::Salt, {1, 0}},
                               {core::AtomType::Salt, {0, -1}},
                               {core::AtomType::Salt, {-1, 1}}},
                              {{1, {0, 0}, {1, 0}}, {1, {0, 0}, {0, -1}}, {1, {0, 0}, {-1, 1}}}});
    machine.objects.push_back({"bonder-speed", {}, {}, {1, 0, 0, 0, 0}});
    machine.objects.push_back({"out-std", {}, {}, {1, 0, 0, 0, 0}});
    Differential(puzzle, machine);
}
TEST(IndependentStaticProduction, DifferentialFreshUnbondPreventsSameHalfCycleConversion) {
    auto puzzle = ChemistryPuzzle();
    io::SolutionData machine;
    puzzle.inputs.push_back(
        {{{core::AtomType::Salt, {0, 0}}, {core::AtomType::Salt, {1, 0}}}, {{1, {0, 0}, {1, 0}}}});
    machine.objects.push_back({"input", {}, {}, {1, 0, 0, 0, 0}});
    machine.objects.push_back({"unbonder", {}, {}, {1, 0, 0, 0, 0}});
    machine.objects.push_back({"glyph-life-and-death", {}, {}, {1, 0, 0, 0, 0}});
    Product(puzzle, machine, 13, {0, 1});
    Product(puzzle, machine, 14, {1, -1});
    Differential(puzzle, machine);
}
TEST(IndependentStaticProduction, DifferentialPistonAndTrackEndInstructionsAreNoOps) {
    for (bool piston : {false, true}) {
        SCOPED_TRACE(piston);
        auto puzzle = ChemistryPuzzle();
        io::SolutionData machine;
        Feed(puzzle, machine, 1, {1, 0});
        Product(puzzle, machine, 1, piston ? core::HexCoord{2, 0} : core::HexCoord{1, 1});
        machine.objects.push_back({piston ? "piston" : "arm1",
                                   {},
                                   {{'G', 0},
                                    {piston ? 'E' : 'A', 1},
                                    {'g', 2},
                                    {piston ? 'e' : 'a', 3},
                                    {piston ? 'e' : 'a', 4}},
                                   {1, 0, 0, 0, 0}});
        if (!piston) machine.objects.push_back({"track", {}, {}, {1, 0, 0, 0, 2, 0, 0, 0, 1, 0}});
        Differential(puzzle, machine);
    }
}
TEST(IndependentStaticProduction, DifferentialInternalChordHeldSaltOrientationThree) {
    io::PuzzleData puzzle;
    puzzle.version = 3;
    puzzle.name = "finite ordinary replay";
    puzzle.permissionBits = ~0ULL;
    puzzle.outputMultiplier = 1;
    puzzle.inputs = {{{{core::AtomType::Salt, {}}}, {}}};
    puzzle.outputs = puzzle.inputs;
    io::SolutionData machine;
    machine.puzzleName = puzzle.name;
    machine.objects = {
        {"input", {-1, 0}, {}, {1, 0, 0, 0, 0}},
        {"out-std", {-1, 2}, {}, {1, 0, 0, 0, 0}},
        {"arm1", {}, {{'G', 0}, {'a', 1}, {'A', 2}, {'A', 3}, {'A', 4}, {'A', 5},
                      {'g', 6}, {'a', 7}, {'a', 8}, {'a', 9}, {'a', 10}},
         {1, 3, 0, 4, 0}},
        {"track", {}, {}, {1, 0, 0, 0, 4, 0, 0, 1, 0, 0, 1, 0, 2, 0}}};
    const auto stem = root / ("build/independent-chord-reference-" +
                              std::to_string(opusoptima::test::ProcessId()));
    const std::filesystem::path puzzlePath = stem.string() + ".puzzle",
                                solutionPath = stem.string() + ".solution";
    struct Cleanup {
        std::filesystem::path a, b;
        ~Cleanup() {
            std::error_code error;
            std::filesystem::remove(a, error);
            std::filesystem::remove(b, error);
        }
    } cleanup{puzzlePath, solutionPath};
    WritePuzzle(puzzlePath, puzzle);
    io::BinaryWriter::WriteSolutionFile(solutionPath, machine);
    const auto decodedPuzzle = io::BinaryParser::ParsePuzzleFile(puzzlePath);
    const auto decodedMachine = io::BinaryParser::ParseSolutionFile(solutionPath);
    ASSERT_FALSE(decodedPuzzle.productionInfo);
    ASSERT_EQ(decodedMachine.objects.back().bodyData, machine.objects.back().bodyData);
    const auto replay = ReplayFiniteMachine(decodedPuzzle, decodedMachine, 100);
    ASSERT_EQ(replay.outcome, ReplayOutcome::Completed) << replay.detail;
    EXPECT_EQ(replay.outputs, (std::vector<std::size_t>{6}));
    EXPECT_EQ(replay.cycles, 62);
    const auto certified = VerifyIndependent(decodedPuzzle, decodedMachine, 100);
    EXPECT_EQ(certified.status, io::VerificationStatus::Unavailable);
    EXPECT_EQ(certified.detail, "dynamic reference trajectories and area are not certified");
    EXPECT_EQ(certified.errorCycle, 2);
    void* native = verifier_create(puzzlePath.c_str(), solutionPath.c_str());
    ASSERT_NE(native, nullptr);
    struct NativeCleanup {
        void* verifier;
        ~NativeCleanup() { verifier_destroy(verifier); }
    } nativeCleanup{native};
    verifier_set_cycle_limit(native, 100);
    const auto cycles = verifier_evaluate_metric(native, "cycles");
    ASSERT_EQ(verifier_error(native), nullptr)
        << (verifier_error(native) ? verifier_error(native) : "");
    EXPECT_EQ(cycles, replay.cycles);
    for (const auto* metric : {"cycles", "cost", "area", "instructions"}) {
        const auto value = verifier_evaluate_metric(native, metric);
        EXPECT_EQ(verifier_error(native), nullptr) << metric;
        EXPECT_GE(value, 0) << metric;
        RecordProperty(std::string("reference_") + metric, value);
    }
}
TEST(IndependentStaticProduction, DifferentialTracksIgnoreSerializedRotation) {
    auto puzzle = ChemistryPuzzle();
    io::SolutionData machine;
    Feed(puzzle, machine, 1, {1, 0});
    Product(puzzle, machine, 1, {1, 1});
    machine.objects.push_back(
        {"arm1", {}, {{'G', 0}, {'A', 1}, {'g', 2}, {'a', 3}}, {1, 0, 0, 0, 0}});
    machine.objects.push_back({"track", {}, {}, {1, 3, 0, 0, 2, 0, 0, 0, 1, 0}});
    Differential(puzzle, machine);
}
TEST(IndependentStaticProduction, DifferentialGrabModeDoesNotRetryEmptyGrippers) {
    auto puzzle = ChemistryPuzzle();
    io::SolutionData machine;
    Feed(puzzle, machine, 1, {0, 1});
    Product(puzzle, machine, 1, {-1, 1});
    machine.objects.push_back(
        {"arm1",
         {},
         {{'G', 0}, {'r', 1}, {'G', 2}, {'r', 3}, {'g', 4}, {'R', 5}, {'R', 6}},
         {1, 0, 0, 0, 0}});
    Differential(puzzle, machine, false);
}
TEST(IndependentStaticProduction, DifferentialMultipleGrippersMoveAllContactsSimultaneously) {
    for (int grabbers : {1, 2, 3}) {
        SCOPED_TRACE(grabbers);
        auto puzzle = ChemistryPuzzle();
        io::SolutionData machine;
        const auto rotate = [](core::HexCoord cell, int turns) {
            while (turns--)
                cell = {-cell.r, cell.q + cell.r};
            return cell;
        };
        for (int grip = 0; grip < grabbers; ++grip) {
            Feed(puzzle, machine, 1, rotate({1, 0}, grip * 6 / grabbers));
            Product(puzzle, machine, 1, rotate({1, 0}, grip * 6 / grabbers + 1));
        }
        machine.objects.push_back({"arm" + std::to_string(grabbers),
                                   {},
                                   {{'G', 0}, {'r', 1}, {'g', 2}, {'R', 3}},
                                   {1, 0, 0, 0, 0}});
        Differential(puzzle, machine);
    }
}
TEST(IndependentStaticProduction, DifferentialTapeOffsetsNormalizeAgainstTheEarliestArm) {
    for (int offset : {0, 1, 7, -3}) {
        SCOPED_TRACE(offset);
        auto puzzle = ChemistryPuzzle();
        io::SolutionData machine;
        Feed(puzzle, machine, 1, {1, 0});
        Product(puzzle, machine, 1, {0, 1});
        machine.objects.push_back(
            {"arm1",
             {},
             {{'G', offset}, {'r', offset + 1}, {'g', offset + 2}, {'R', offset + 3}},
             {1, 0, 0, 0, 0}});
        Differential(puzzle, machine);
    }
}
TEST(IndependentStaticProduction, FiniteRepeatResetReferenceMetricsDoNotAuthorizeRawTapes) {
    auto puzzle = ChemistryPuzzle();
    io::SolutionData base;
    Feed(puzzle, base, 1, {1, 0});
    Product(puzzle, base, 1, {0, 1});
    base.objects.push_back({"arm1", {}, {}, {1, 0, 0, 0, 0}});
    const auto stem = root / ("build/finite-repeat-reset-" +
                              std::to_string(opusoptima::test::ProcessId()));
    const std::filesystem::path puzzlePath = stem.string() + ".puzzle",
                                solutionPath = stem.string() + ".solution";
    struct Cleanup {
        std::filesystem::path a, b;
        ~Cleanup() {
            std::filesystem::remove(a);
            std::filesystem::remove(b);
        }
    } cleanup{puzzlePath, solutionPath};
    WritePuzzle(puzzlePath, puzzle);
    for (char marker : {'C', 'X'}) {
        auto unsupported = base;
        unsupported.objects.back().instructions = {{marker, 0}};
        io::BinaryWriter::WriteSolutionFile(solutionPath, unsupported);
        const auto direct = VerifyStaticProduction(puzzle, unsupported, 100);
        const auto decoded = VerifyStaticProductionFiles(puzzlePath, solutionPath, 100);
        for (const auto* result : {&direct, &decoded}) {
            EXPECT_EQ(result->status, io::VerificationStatus::Unavailable);
            EXPECT_EQ(result->detail,
                      "unexpanded repeat/reset or unknown independent instruction");
        }
    }
    int referenceCases = 0;
    for (const auto& [offset, repeat] :
         std::array<std::pair<int, bool>, 3>{{{-3, false}, {0, true}, {7, true}}}) {
        SCOPED_TRACE(::testing::Message() << "offset=" << offset << " repeat=" << repeat);
        // Locked decode.c: X undoes the single swing; consecutive C markers each
        // copy the same preceding block, not the previously expanded prefix.
        const std::vector<io::ArmInstruction> raw = repeat
            ? std::vector<io::ArmInstruction>{{'G', offset}, {'r', offset + 1},
                                             {'g', offset + 2}, {'X', offset + 3},
                                             {'C', offset + 4}, {'C', offset + 8}}
            : std::vector<io::ArmInstruction>{{'G', offset}, {'r', offset + 1},
                                             {'g', offset + 2}, {'X', offset + 3}};
        const std::vector<io::ArmInstruction> expanded = repeat
            ? std::vector<io::ArmInstruction>{{'G', offset}, {'r', offset + 1},
                                             {'g', offset + 2}, {'R', offset + 3},
                                             {'G', offset + 4}, {'r', offset + 5},
                                             {'g', offset + 6}, {'R', offset + 7},
                                             {'G', offset + 8}, {'r', offset + 9},
                                             {'g', offset + 10}, {'R', offset + 11}}
            : std::vector<io::ArmInstruction>{{'G', offset}, {'r', offset + 1},
                                             {'g', offset + 2}, {'R', offset + 3}};
        auto rawMachine = base;
        rawMachine.objects.back().instructions = raw;
        const auto conversion = ExpandFiniteArm1Tape(puzzle, rawMachine);
        ASSERT_TRUE(std::holds_alternative<ExpandedFiniteTape>(conversion));
        const auto& converted = std::get<ExpandedFiniteTape>(conversion);
        ASSERT_EQ(converted.slots.size(), expanded.size());
        for (std::size_t i = 0; i < expanded.size(); ++i) {
            EXPECT_EQ(converted.slots[i].action, expanded[i].action);
            EXPECT_EQ(converted.slots[i].parameter, expanded[i].parameter);
        }
        EXPECT_EQ(converted.staticInstructions, repeat ? 12 : 4);
        std::optional<std::pair<int, int>> nativeCostArea;
        for (int variant : {0, 1, 2}) {
            const bool manual = variant != 0;
            SCOPED_TRACE(variant);
            auto machine = rawMachine;
            if (variant == 1) machine.objects.back().instructions = expanded;
            if (variant == 2) machine.objects[converted.armObject].instructions = converted.slots;
            if (manual) {
                IndependentMetrics metrics;
                metrics.Initialize(puzzle, machine);
                EXPECT_EQ(metrics.Finish(0).instructionCount, converted.staticInstructions);
            }
            io::BinaryWriter::WriteSolutionFile(solutionPath, machine);
            const auto direct = VerifyStaticProduction(puzzle, machine, 100);
            const auto decoded = VerifyStaticProductionFiles(puzzlePath, solutionPath, 100);
            for (const auto* result : {&direct, &decoded}) {
                if (manual) {
                    ASSERT_TRUE(result->Passed()) << result->detail;
                    EXPECT_EQ(result->cycles, 23);
                    EXPECT_EQ(result->outputs, (std::vector<std::size_t>{6}));
                } else {
                    EXPECT_EQ(result->status, io::VerificationStatus::Unavailable);
                    EXPECT_EQ(result->detail,
                              "unexpanded repeat/reset or unknown independent instruction");
                    EXPECT_EQ(result->cycles, 0);
                }
            }
            const auto certified = VerifyIndependent(puzzle, machine, 100, false);
            EXPECT_EQ(certified.status, io::VerificationStatus::Unavailable);
            EXPECT_EQ(certified.detail, "production-wall-profile-uncertified");
            void* native = verifier_create(puzzlePath.c_str(), solutionPath.c_str());
            ASSERT_NE(native, nullptr);
            verifier_set_cycle_limit(native, 100);
            EXPECT_EQ(verifier_evaluate_metric(native, "instructions"), repeat ? 12 : 4);
            EXPECT_EQ(verifier_evaluate_metric(native, "cycles"), 23);
            EXPECT_EQ(verifier_evaluate_metric(native, "executed instructions"), repeat ? 12 : 4);
            const std::pair<int, int> costArea{verifier_evaluate_metric(native, "cost"),
                                              verifier_evaluate_metric(native, "area")};
            if (nativeCostArea) EXPECT_EQ(costArea, *nativeCostArea);
            else nativeCostArea = costArea;
            const auto* error = verifier_error(native);
            EXPECT_EQ(error, nullptr) << (error ? error : "");
            verifier_destroy(native);
            ++referenceCases;
        }
    }
    RecordProperty("reference_cases", referenceCases);
}
TEST(IndependentStaticProduction, OfflineExpansionLockedDecoderInstructionDifferential) {
    const auto puzzle = ChemistryPuzzle();
    const auto stem = root / ("build/finite-decoder-" + std::to_string(test::ProcessId()));
    const std::filesystem::path puzzlePath = stem.string() + ".puzzle",
                                solutionPath = stem.string() + ".solution";
    struct Cleanup {
        std::filesystem::path a, b;
        ~Cleanup() { std::filesystem::remove(a); std::filesystem::remove(b); }
    } cleanup{puzzlePath, solutionPath};
    WritePuzzle(puzzlePath, puzzle);
    int referenceCases = 0;
    for (const auto& [text, explicitTape] : std::vector<std::pair<std::string, std::string>>{
             {"G_C__C", "G G  G"}, {"C", " "}, {"X", " "}, {"GrX", "GrgR"},
             {"rrrX", "rrrRRR"}, {"RRRX", "RRRrrr"}, {"rXX", "rR "},
             {"rXC_GX", "rRrRGg"}, {"O_C", "O O"}, {" _C", "   "},
             {"BrX", "BrR"}, {"GCCg", "GGGg"}, {"GrCC", "GrGGr"},
             {"G_XC", "G gG g"}, {"G__rCC___g", "G  rGG  rg"}}) {
        SCOPED_TRACE(text);
        io::SolutionData machine;
        machine.objects.push_back({"arm1", {}, {}, {1, 0, 0, 0, 0}});
        for (std::size_t i = 0; i < text.size(); ++i)
            if (text[i] != '_') machine.objects[0].instructions.push_back(
                {text[i], static_cast<std::int32_t>(i)});
        const auto result = ExpandFiniteArm1Tape(puzzle, machine);
        ASSERT_TRUE(std::holds_alternative<ExpandedFiniteTape>(result));
        const auto& converted = std::get<ExpandedFiniteTape>(result);
        std::optional<std::pair<int, int>> runtime;
        std::optional<std::string> runtimeError;
        for (int variant : {0, 1, 2}) {
            if (variant == 1) {
                machine.objects[0].instructions.clear();
                for (std::size_t i = 0; i < explicitTape.size(); ++i)
                    machine.objects[0].instructions.push_back(
                        {explicitTape[i], static_cast<std::int32_t>(i)});
            } else if (variant == 2) machine.objects[converted.armObject].instructions = converted.slots;
            io::BinaryWriter::WriteSolutionFile(solutionPath, machine);
            void* native = verifier_create(puzzlePath.c_str(), solutionPath.c_str());
            ASSERT_NE(native, nullptr);
            EXPECT_EQ(verifier_evaluate_metric(native, "instructions"), converted.staticInstructions);
            const auto* error = verifier_error(native);
            EXPECT_EQ(error, nullptr) << (error ? error : "");
            verifier_set_cycle_limit(native, 10);
            const auto cycles = verifier_evaluate_metric(native, "cycles");
            const auto* executionError = verifier_error(native);
            const std::string executionDetail = executionError ? executionError : "";
            verifier_error_clear(native);
            const auto executed = verifier_evaluate_metric(native, "executed instructions");
            const std::pair<int, int> observed{cycles, executed};
            if (runtime) {
                EXPECT_EQ(observed, *runtime);
                EXPECT_EQ(executionDetail, *runtimeError);
            } else {
                runtime = observed;
                runtimeError = executionDetail;
            }
            if (text == "BrX") EXPECT_EQ(executed, 0);
            verifier_destroy(native);
            ++referenceCases;
        }
    }
    EXPECT_EQ(referenceCases, 45);
    RecordProperty("reference_cases", referenceCases);
}
TEST(IndependentStaticProduction, DifferentialIndependentArmsKeepOffsetsAndIgnoreHaltedTapeLength) {
    for (bool halted : {false, true}) {
        SCOPED_TRACE(halted);
        auto puzzle = ChemistryPuzzle();
        io::SolutionData machine;
        puzzle.productionInfo->chambers.push_back({{10, 0}, "Large"});
        Feed(puzzle, machine, 1, {1, 0});
        Product(puzzle, machine, 1, {0, 1});
        machine.objects.push_back(
            {"arm1", {}, {{'G', 0}, {'r', 1}, {'g', 2}, {'R', 3}}, {1, 0, 0, 0, 0}});
        if (halted)
            machine.objects.push_back(
                {"arm1", {10, 0}, {{'O', 0}, {'O', 9}, {'B', 10}}, {1, 0, 0, 0, 1}});
        else {
            Feed(puzzle, machine, 1, {11, 0});
            Product(puzzle, machine, 1, {10, 1});
            machine.objects.push_back(
                {"arm1", {10, 0}, {{'G', 2}, {'r', 3}, {'g', 4}, {'R', 5}}, {1, 0, 0, 0, 1}});
        }
        Differential(puzzle, machine);
    }
}
TEST(IndependentStaticProduction, DifferentialPivotRotatesTheWholeFragmentAroundItsGrip) {
    auto puzzle = ChemistryPuzzle();
    io::SolutionData machine;
    puzzle.inputs.push_back(
        {{{core::AtomType::Salt, {0, 0}}, {core::AtomType::Fire, {1, 0}}}, {{1, {0, 0}, {1, 0}}}});
    puzzle.outputs.push_back(
        {{{core::AtomType::Salt, {0, 0}}, {core::AtomType::Fire, {0, 1}}}, {{1, {0, 0}, {0, 1}}}});
    machine.objects.push_back({"input", {1, 0}, {}, {1, 0, 0, 0, 0}});
    machine.objects.push_back({"out-std", {1, 0}, {}, {1, 0, 0, 0, 0}});
    machine.objects.push_back(
        {"arm1", {}, {{'G', 0}, {'p', 1}, {'g', 2}, {'P', 3}}, {1, 0, 0, 0, 0}});
    Differential(puzzle, machine);
}
TEST(IndependentStaticProduction, DifferentialWheelCatalystsRemainPresentThroughDuplication) {
    for (bool rotate : {false, true}) {
        SCOPED_TRACE(rotate);
        auto puzzle = ChemistryPuzzle();
        io::SolutionData machine;
        Feed(puzzle, machine, 1, {1, 1});
        Product(puzzle, machine, 5, {1, 1});
        machine.objects.push_back({"glyph-duplication", {0, 1}, {}, {1, 0, 0, 0, 0}});
        machine.objects.push_back({"baron",
                                   {},
                                   rotate ? std::vector<io::ArmInstruction>{{'r', 0}, {'R', 1}}
                                          : std::vector<io::ArmInstruction>{},
                                   {1, 0, 0, 0, 0}});
        Differential(puzzle, machine);
    }
}
TEST(IndependentStaticProduction, CrossingTrackBasesCannotPassWhileUnrelatedOutputCompletes) {
    auto puzzle = ChemistryPuzzle();
    io::SolutionData machine;
    puzzle.productionInfo->chambers.push_back({{10, 0}, "Large"});
    Feed(puzzle, machine, 4, {});
    Product(puzzle, machine, 1, {});
    machine.objects.push_back({"glyph-calcification", {}, {}, {1, 0, 0, 0, 0}});
    machine.objects.push_back({"track", {10, 0}, {}, {1, 0, 0, 0, 2, 0, 0, 1, 0, 0}});
    machine.objects.push_back({"arm1", {10, 0}, {{'A', 0}}, {1, 2, 0, 0, 0}});
    machine.objects.push_back({"arm1", {11, 0}, {{'a', 0}}, {1, 0, 0, 0, 1}});
    Differential(puzzle, machine, false);
}
TEST(IndependentStaticProduction, DifferentialRotationsTranslationsAndVialPresentationVariants) {
    const auto original = Conduit();
    const auto canonical = solvers::CompileProductionMolecular(original);
    const auto stem =
        root / ("build/independent-profile-" + std::to_string(opusoptima::test::ProcessId()));
    const std::filesystem::path puzzlePath = stem.string() + ".puzzle",
                                solutionPath = stem.string() + ".solution";
    struct Cleanup {
        std::filesystem::path a, b;
        ~Cleanup() {
            std::filesystem::remove(a);
            std::filesystem::remove(b);
        }
    } cleanup{puzzlePath, solutionPath};
    for (int turn = 0; turn < 6; ++turn)
        for (bool style : {false, true})
            for (int flags = 0; flags < 4; ++flags) {
                auto puzzle = original;
                auto machine = canonical;
                const auto transform = [&](core::HexCoord cell) {
                    for (int i = 0; i < turn; ++i)
                        cell = {-cell.r, cell.q + cell.r};
                    return core::HexCoord{cell.q + 20, cell.r - 10};
                };
                for (auto& chamber : puzzle.productionInfo->chambers)
                    chamber.position = transform(chamber.position);
                for (auto& conduit : puzzle.productionInfo->conduits) {
                    conduit.positionA = transform(conduit.positionA);
                    conduit.positionB = transform(conduit.positionB);
                }
                puzzle.productionInfo->shrinkLeft = flags & 1;
                puzzle.productionInfo->shrinkRight = flags & 2;
                puzzle.productionInfo->vials = {{transform({5, -3}), style, style ? 3 : 1}};
                for (auto& part : machine.objects) {
                    part.position = transform(part.position);
                    part.bodyData[1] = (part.bodyData[1] + turn) % 6;
                }
                WritePuzzle(puzzlePath, puzzle);
                io::BinaryWriter::WriteSolutionFile(solutionPath, machine);
                const auto independent = VerifyStaticProductionFiles(puzzlePath, solutionPath, 100);
                ASSERT_TRUE(independent.Passed())
                    << turn << ":" << style << " " << independent.detail;
                void* native = verifier_create(puzzlePath.c_str(), solutionPath.c_str());
                ASSERT_NE(native, nullptr);
                verifier_set_cycle_limit(native, 100);
                const auto cycles = verifier_evaluate_metric(native, "cycles");
                const auto* error = verifier_error(native);
                EXPECT_EQ(error, nullptr) << (error ? error : "");
                EXPECT_EQ(cycles, independent.cycles);
                verifier_destroy(native);
                const auto combined = io::VerifySolutionFiles(puzzlePath, solutionPath, 100, 2000);
                ASSERT_TRUE(combined.Passed()) << combined.detail;
                EXPECT_EQ(combined.metrics.cycles, 43);
                auto reference = puzzle;
                reference.productionInfo->shrinkLeft = false;
                reference.productionInfo->shrinkRight = false;
                const auto referenceReport = VerifyStaticProduction(reference, machine, 100);
                ASSERT_EQ(independent.events.size(), referenceReport.events.size());
                for (std::size_t event = 0; event < independent.events.size(); ++event) {
                    EXPECT_EQ(independent.events[event].kind, referenceReport.events[event].kind);
                    EXPECT_EQ(independent.events[event].cycle, referenceReport.events[event].cycle);
                    EXPECT_EQ(independent.events[event].port, referenceReport.events[event].port);
                    EXPECT_EQ(independent.events[event].atoms, referenceReport.events[event].atoms);
                }
                puzzle.productionInfo->chambers.front().chamberType = "Unknown";
                WritePuzzle(puzzlePath, puzzle);
                EXPECT_EQ(VerifyStaticProductionFiles(puzzlePath, solutionPath, 100).status,
                          io::VerificationStatus::Unavailable);
                if (turn == 0 && !style && flags == 0) {
                    const auto before = io::BinaryParser::ReadAllBytes(solutionPath);
                    EXPECT_FALSE(
                        io::PublishVerifiedSolution(puzzlePath, solutionPath, machine).Passed());
                    EXPECT_EQ(io::BinaryParser::ReadAllBytes(solutionPath), before);
                }
            }
}
#endif

TEST(IndependentStaticProduction, MalformedVisualMetadataCannotExpandTheCertifiedProfile) {
    auto puzzle = Conduit();
    const auto machine = solvers::CompileProductionMolecular(puzzle);
    puzzle.productionInfo->vials[0].count = -1;
    EXPECT_EQ(VerifyStaticProduction(puzzle, machine).status, io::VerificationStatus::Unavailable);
    puzzle.productionInfo->vials[0].count = 1000000;
    EXPECT_EQ(VerifyStaticProduction(puzzle, machine).status, io::VerificationStatus::Unavailable);
}

TEST(IndependentStaticProduction, UnknownRulesAndInstructionsStayUnavailable) {
    auto puzzle = Conduit();
    auto machine = solvers::CompileProductionMolecular(puzzle);
    puzzle.productionInfo->chambers.front().chamberType = "Unknown";
    EXPECT_EQ(VerifyStaticProduction(puzzle, machine).status, io::VerificationStatus::Unavailable);
    puzzle = Conduit();
    machine.objects.push_back({"glyph-unknown", {}, {}, {1, 0, 0, 0, 0}});
    EXPECT_EQ(VerifyStaticProduction(puzzle, machine).status, io::VerificationStatus::Unavailable);
    machine = solvers::CompileProductionMolecular(puzzle);
    const auto pipe = std::find_if(machine.objects.begin(), machine.objects.end(),
                                   [](const auto& part) { return part.typeName == "pipe"; });
    ASSERT_NE(pipe, machine.objects.end());
    machine.objects.push_back({"bonder", pipe->position, {}, {1, 0, 0, 0, 0}});
    EXPECT_FALSE(VerifyStaticProduction(puzzle, machine, 100).Passed());
    machine = solvers::CompileProductionMolecular(puzzle);
    const auto arm = std::find_if(machine.objects.begin(), machine.objects.end(),
                                  [](const auto& part) { return part.typeName == "arm1"; });
    ASSERT_NE(arm, machine.objects.end());
    arm->instructions.front().action = 'Z';
    EXPECT_EQ(VerifyStaticProduction(puzzle, machine).status, io::VerificationStatus::Unavailable);
    puzzle = Conduit();
    machine = solvers::CompileProductionMolecular(puzzle);
    puzzle.outputs[0].atoms[0].type = core::AtomType::Repeat;
    EXPECT_EQ(VerifyStaticProduction(puzzle, machine).status, io::VerificationStatus::Unavailable);
    puzzle = Conduit();
    machine = solvers::CompileProductionMolecular(puzzle);
    machine.objects.front().position.q = std::numeric_limits<int>::min();
    EXPECT_EQ(VerifyStaticProduction(puzzle, machine).status, io::VerificationStatus::Rejected);
    machine = solvers::CompileProductionMolecular(puzzle);
    puzzle.productionInfo->chambers.front().position.q = std::numeric_limits<int>::min();
    EXPECT_EQ(VerifyStaticProduction(puzzle, machine).status, io::VerificationStatus::Rejected);
    puzzle = Conduit();
    puzzle.permissionBits |= 1ULL << 3;
    machine.objects.push_back(
        {"track", {}, {}, {1, 0, 0, 0, 1, std::numeric_limits<int>::max(), 0, 0}});
    EXPECT_EQ(VerifyStaticProduction(puzzle, machine).status, io::VerificationStatus::Rejected);
}
}} // namespace opusoptima::verification
