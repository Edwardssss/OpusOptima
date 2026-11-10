#include "WheelAssembly.hpp"
#include "AssemblySolver.hpp"
#include "Core/LogManager.hpp"
#include "IO/BinaryWriter.hpp"
#include "IO/SolutionVerifier.hpp"
#include "RepoRoot.hpp"
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <optional>
#include <spdlog/sinks/ostream_sink.h>
#include <sstream>

namespace opusoptima::solvers { namespace {
const auto root = opusoptima::test::RepoRoot(__FILE__);
const auto puzzlePath = root / "asset/puzzle/journal/journal1/P054.puzzle";
TEST(WheelAssemblyTest, RejectsUnavailableHardwareAndNonatomicFeeds) {
    const auto puzzle = io::BinaryParser::ParsePuzzleFile(puzzlePath);
    for (int bit : {0, 2, 3, 8, 13, 22, 23, 25, 28}) {
        auto p = puzzle;
        p.permissionBits &= ~(1ULL << bit);
        EXPECT_THROW((void)CompileWheelAssembly(p), std::invalid_argument);
    }
    auto p = puzzle;
    p.inputs[0].atoms.push_back(p.inputs[0].atoms[0]);
    EXPECT_THROW((void)CompileWheelAssembly(p), std::invalid_argument);
    p = puzzle;
    p.inputs.clear();
    EXPECT_THROW((void)CompileWheelAssembly(p), std::invalid_argument);
}
TEST(WheelAssemblyTest, CalcifiedAtomicSupplyRequiresAllConversionPermissions) {
    auto puzzle = io::BinaryParser::ParsePuzzleFile(puzzlePath);
    puzzle.inputs = {{{{core::AtomType::Water, {0, 0}}}, {}}};
    for (auto& output : puzzle.outputs)
        for (auto& atom : output.atoms)
            atom.type = core::AtomType::Air;
    puzzle.outputs.resize(1);
    puzzle.permissionBits = (1ULL << 29) - 1;
    EXPECT_NO_THROW((void)CompileWheelAssembly(puzzle));
    for (const int bit : {12, 13, 25, 28}) {
        auto restricted = puzzle;
        restricted.permissionBits &= ~(1ULL << bit);
        EXPECT_THROW((void)CompileWheelAssembly(restricted), std::invalid_argument);
    }
    puzzle.inputs[0].atoms[0].type = core::AtomType::Gold;
    EXPECT_THROW((void)CompileWheelAssembly(puzzle), std::invalid_argument);
}
TEST(WheelAssemblyTest, RejectsUnsupportedMolecularRecipes) {
    const auto puzzle =
        io::BinaryParser::ParsePuzzleFile(root / "asset/puzzle/campaign/ch3/P024.puzzle");
    auto p = puzzle;
    p.permissionBits &= ~(1ULL << 9);
    EXPECT_THROW((void)CompileWheelAssembly(p), std::invalid_argument);
    p = puzzle;
    p.outputs[1] = p.outputs[0];
    EXPECT_THROW((void)CompileWheelAssembly(p), std::invalid_argument);
    p = puzzle;
    p.outputs.pop_back();
    EXPECT_THROW((void)CompileWheelAssembly(p), std::invalid_argument);
    p = puzzle;
    for (auto& atom : p.inputs[0].atoms)
        if (atom.type == core::AtomType::Salt) atom.type = static_cast<core::AtomType>(4);
    EXPECT_THROW((void)CompileWheelAssembly(p), std::invalid_argument);
}
TEST(WheelAssemblyTest, RejectsNoncardinalOutputsBeforeNormalizedSupplyPlanning) {
    const auto puzzle =
        io::BinaryParser::ParsePuzzleFile(root / "asset/puzzle/journal/journal7/P099.puzzle");
    for (const auto& output : puzzle.outputs)
        for (const auto& atom : output.atoms)
            ASSERT_FALSE(atom.type >= core::AtomType::Air && atom.type <= core::AtomType::Water);
    try {
        (void)CompileWheelAssembly(puzzle);
        FAIL() << "wheel must not normalize a recipe without cardinal outputs";
    } catch (const std::invalid_argument& error) {
        EXPECT_STREQ(error.what(), "molecular wheel requires a derived cardinal");
    }
}
TEST(WheelAssemblyTest, RadialReuseChecksPermissionsTopologyAndTransforms) {
    const auto original = io::BinaryParser::ParsePuzzleFile(puzzlePath);
    ASSERT_TRUE(SupportsRadialWheelAssembly(original));
    for (int bit : {0, 8, 13, 22, 23, 25, 26, 28}) {
        auto p = original;
        p.permissionBits &= ~(1ULL << bit);
        EXPECT_FALSE(SupportsRadialWheelAssembly(p));
        EXPECT_THROW((void)CompileRadialWheelAssembly(p), std::invalid_argument);
    }
    for (int turn = 0; turn < 6; ++turn) {
        auto p = original;
        auto transform = [&](core::HexCoord c) {
            for (int i = 0; i < turn; ++i)
                c = core::RotateClockwise(c);
            return core::Add(c, {7, -4});
        };
        for (auto& input : p.inputs)
            input.atoms[0].position = transform(input.atoms[0].position);
        for (auto& atom : p.outputs[0].atoms)
            atom.position = transform(atom.position);
        for (auto& bond : p.outputs[0].bonds) {
            bond.from = transform(bond.from);
            bond.to = transform(bond.to);
        }
        EXPECT_TRUE(SupportsRadialWheelAssembly(p));
        EXPECT_NO_THROW((void)CompileRadialWheelAssembly(p));
    }
    for (int defect = 0; defect < 6; ++defect) {
        auto p = original;
        if (defect == 0) p.outputs[0].bonds[0].bits = 2;
        if (defect == 1) p.outputs[0].bonds[0] = p.outputs[0].bonds[1];
        if (defect == 2) p.outputs[0].atoms[0].type = core::AtomType::Fire;
        if (defect == 3) p.outputs[0].atoms.back().position = {3, 0};
        if (defect == 4) p.inputs[0].atoms[0].type = core::AtomType::Fire;
        if (defect == 5) p.outputMultiplier = 2;
        EXPECT_FALSE(SupportsRadialWheelAssembly(p));
    }
}
TEST(WheelAssemblyTest, RadialReusePreservesFirstAndCollectionCaps) {
    const char* old = std::getenv("OPUSOPTIMA_RADIAL_WHEEL");
    const std::optional<std::string> prior = old ? std::optional<std::string>{old} : std::nullopt;
    struct Restore {
        const std::optional<std::string>& prior;
        ~Restore() {
            if (prior)
                setenv("OPUSOPTIMA_RADIAL_WHEEL", prior->c_str(), 1);
            else
                unsetenv("OPUSOPTIMA_RADIAL_WHEEL");
        }
    } restore{prior};
    const auto p = io::BinaryParser::ParsePuzzleFile(puzzlePath);
    auto parent = CompileWheelAssembly(p);
    parent.metrics = {};
    const auto expected = io::BinaryWriter::WriteSolution(parent);
    auto logger = GetLoggerOrFallback("solver");
    std::ostringstream output;
    struct RestoreLogger {
        std::shared_ptr<spdlog::logger> logger;
        std::vector<spdlog::sink_ptr> sinks;
        spdlog::level::level_enum level;
        ~RestoreLogger() {
            logger->sinks() = sinks;
            logger->set_level(level);
        }
    } restoreLogger{logger, logger->sinks(), logger->level()};
    logger->sinks() = {std::make_shared<spdlog::sinks::ostream_sink_mt>(output)};
    logger->set_level(spdlog::level::info);
    for (const auto* mode : {"off", "<unset>", "on", "invalid"})
        for (std::size_t cap : {1u, 2u, 3u})
            for (auto authority :
                 {io::VerificationAuthority::LegacyGate, io::VerificationAuthority::Independent,
                  io::VerificationAuthority::None}) {
                if (authority != io::VerificationAuthority::LegacyGate &&
                    (cap != 3 || std::string_view(mode) != "on"))
                    continue;
                SCOPED_TRACE(static_cast<int>(authority));
                if (std::string_view(mode) == "<unset>")
                    unsetenv("OPUSOPTIMA_RADIAL_WHEEL");
                else
                    setenv("OPUSOPTIMA_RADIAL_WHEEL", mode, 1);
                output.str("");
                output.clear();
                std::vector<std::string> stages;
                const auto first = AssemblySolver{}.CollectVerified(
                    p,
                    [&](const auto&, const auto& stage) {
                        stages.push_back(stage);
                        io::VerificationReport report;
                        report.status = io::VerificationStatus::Passed;
                        report.authority = stage == "portfolio-radial-wheel"
                                               ? authority
                                               : io::VerificationAuthority::LegacyGate;
                        return report;
                    },
                    cap);
                EXPECT_EQ(io::BinaryWriter::WriteSolution(first), expected);
                EXPECT_LE(stages.size(), cap);
                EXPECT_LE(stages.size(), AssemblySolver::MaxCandidateSubmissions);
                const bool radial = cap == 3 && (std::string_view(mode) == "on" ||
                                                 std::string_view(mode) == "<unset>");
                EXPECT_EQ(std::count(stages.begin(), stages.end(), "portfolio-radial-wheel"),
                          radial ? 1 : 0);
                if (radial) {
                    ASSERT_EQ(stages.size(), 3U);
                    EXPECT_EQ(stages[1], "portfolio-static-embedding");
                    EXPECT_EQ(stages[2], "portfolio-radial-wheel");
                    const auto accepted =
                        authority == io::VerificationAuthority::LegacyGate ? "true" : "false";
                    EXPECT_NE(
                        output.str().find(
                            std::string("portfolio-radial-wheel event=gate accepted=") + accepted),
                        std::string::npos)
                        << output.str();
                }
            }
}
TEST(WheelAssemblyTest, RadialProjectionRefusesUnsupportedStructureAndPermissions) {
    const auto original =
        io::BinaryParser::ParsePuzzleFile(root / "asset/puzzle/tourney-2021/OM2021_W6.puzzle");
    ASSERT_TRUE(SupportsRadialProjectionAssembly(original));
    ASSERT_FALSE(SupportsRadialWheelAssembly(original));
    auto refuse = [&](const auto& p) {
        EXPECT_FALSE(SupportsRadialProjectionAssembly(p));
        EXPECT_THROW((void)CompileRadialProjectionAssembly(p), std::invalid_argument);
    };
    for (int bit : {0, 8, 14, 22, 23, 25, 26}) {
        auto p = original;
        p.permissionBits &= ~(1ULL << bit);
        refuse(p);
    }
    for (int mutation = 0; mutation < 8; ++mutation) {
        auto p = original;
        auto radial = std::find_if(p.outputs.begin(), p.outputs.end(),
                                   [](const auto& output) { return output.atoms.size() > 1; });
        if (mutation == 0) {
            std::erase_if(p.inputs, [](const auto& input) {
                return input.atoms[0].type == core::AtomType::Quicksilver;
            });
        } else if (mutation == 1)
            radial->bonds[0].bits = 2;
        else if (mutation == 2)
            radial->bonds[0].to = {40, -30};
        else if (mutation == 3)
            radial->atoms[0].position = radial->atoms[1].position;
        else if (mutation == 4)
            p.outputMultiplier = 2;
        else if (mutation == 5)
            p.inputs[0].atoms[0].type = core::AtomType::Vitae;
        else if (mutation == 6)
            p.inputs[0].atoms[0].type = core::AtomType::Gold;
        else
            p.outputs.push_back(p.outputs.back());
        refuse(p);
    }
}

TEST(WheelAssemblyTest, RadialProjectionKeepsFirstCapsAndOwnNativeAuthority) {
    const char* old = std::getenv("OPUSOPTIMA_RADIAL_PROJECTION");
    const std::optional<std::string> prior = old ? std::optional<std::string>{old} : std::nullopt;
    struct Restore {
        const std::optional<std::string>& prior;
        ~Restore() {
            if (prior)
                setenv("OPUSOPTIMA_RADIAL_PROJECTION", prior->c_str(), 1);
            else
                unsetenv("OPUSOPTIMA_RADIAL_PROJECTION");
        }
    } restore{prior};
    const auto p =
        io::BinaryParser::ParsePuzzleFile(root / "asset/puzzle/tourney-2021/OM2021_W6.puzzle");
    auto logger = GetLoggerOrFallback("solver");
    std::ostringstream output;
    struct RestoreLogger {
        std::shared_ptr<spdlog::logger> logger;
        std::vector<spdlog::sink_ptr> sinks;
        spdlog::level::level_enum level;
        ~RestoreLogger() {
            logger->sinks() = sinks;
            logger->set_level(level);
        }
    } restoreLogger{logger, logger->sinks(), logger->level()};
    logger->sinks() = {std::make_shared<spdlog::sinks::ostream_sink_mt>(output)};
    logger->set_level(spdlog::level::info);
    std::vector<std::byte> expected;
    for (const auto* mode : {"off", "<unset>", "invalid", "on"})
        for (std::size_t cap : {1u, 2u, 3u})
            for (auto authority :
                 {io::VerificationAuthority::LegacyGate, io::VerificationAuthority::Independent,
                  io::VerificationAuthority::None}) {
                const bool enabled =
                    std::string_view(mode) == "<unset>" || std::string_view(mode) == "on";
                if (authority != io::VerificationAuthority::LegacyGate && (cap != 3 || !enabled))
                    continue;
                SCOPED_TRACE(mode);
                SCOPED_TRACE(cap);
                if (std::string_view(mode) == "<unset>")
                    unsetenv("OPUSOPTIMA_RADIAL_PROJECTION");
                else
                    setenv("OPUSOPTIMA_RADIAL_PROJECTION", mode, 1);
                output.str("");
                output.clear();
                std::vector<std::string> stages;
                const auto gate = [&](const auto&, const auto& stage) {
                    stages.push_back(stage);
                    io::VerificationReport report;
                    report.status = io::VerificationStatus::Passed;
                    report.authority = stage == "portfolio-radial-projection"
                                           ? authority
                                           : io::VerificationAuthority::LegacyGate;
                    return report;
                };
                const auto first = cap == 3 ? AssemblySolver{}.CollectVerified(p, gate)
                                            : AssemblySolver{}.CollectVerified(p, gate, cap);
                const auto bytes = io::BinaryWriter::WriteSolution(first);
                if (expected.empty()) expected = bytes;
                EXPECT_EQ(bytes, expected);
                EXPECT_LE(stages.size(), cap);
                EXPECT_LE(stages.size(), AssemblySolver::MaxCandidateSubmissions);
                const bool attempted = cap == 3 && enabled;
                EXPECT_EQ(std::count(stages.begin(), stages.end(), "portfolio-radial-projection"),
                          attempted ? 1 : 0);
                EXPECT_EQ(std::count(stages.begin(), stages.end(), "portfolio-radial-wheel"), 0);
                if (attempted) {
                    ASSERT_EQ(stages.size(), 3U);
                    EXPECT_EQ(stages.back(), "portfolio-radial-projection");
                    const auto accepted =
                        authority == io::VerificationAuthority::LegacyGate ? "true" : "false";
                    EXPECT_NE(output.str().find(
                                  std::string("portfolio-radial-projection event=gate accepted=") +
                                  accepted),
                              std::string::npos)
                        << output.str();
                }
            }
    auto restricted = p;
    restricted.permissionBits &= ~(1ULL << 25);
    std::vector<std::vector<std::byte>> baseline;
    for (const auto* mode : {"off", "on"}) {
        setenv("OPUSOPTIMA_RADIAL_PROJECTION", mode, 1);
        std::vector<std::vector<std::byte>> candidates;
        (void)AssemblySolver{}.CollectVerified(
            restricted,
            [&](const auto& candidate, const auto& stage) {
                EXPECT_NE(stage, "portfolio-radial-projection");
                candidates.push_back(io::BinaryWriter::WriteSolution(candidate));
                io::VerificationReport report;
                report.status = io::VerificationStatus::Passed;
                report.authority = io::VerificationAuthority::LegacyGate;
                return report;
            },
            3);
        if (std::string_view(mode) == "off")
            baseline = candidates;
        else
            EXPECT_EQ(candidates, baseline);
    }
}

TEST(WheelAssemblyTest, RadialReuseIneligiblePreservesPortfolio) {
    const char* old = std::getenv("OPUSOPTIMA_RADIAL_WHEEL");
    const std::optional<std::string> prior = old ? std::optional<std::string>{old} : std::nullopt;
    struct Restore {
        const std::optional<std::string>& prior;
        ~Restore() {
            if (prior)
                setenv("OPUSOPTIMA_RADIAL_WHEEL", prior->c_str(), 1);
            else
                unsetenv("OPUSOPTIMA_RADIAL_WHEEL");
        }
    } restore{prior};
    auto p = io::BinaryParser::ParsePuzzleFile(puzzlePath);
    p.permissionBits &= ~(1ULL << 26);
    ASSERT_FALSE(SupportsRadialWheelAssembly(p));
    std::vector<std::vector<std::byte>> baseline;
    for (const auto* mode : {"off", "<unset>", "on", "invalid"}) {
        if (std::string_view(mode) == "<unset>")
            unsetenv("OPUSOPTIMA_RADIAL_WHEEL");
        else
            setenv("OPUSOPTIMA_RADIAL_WHEEL", mode, 1);
        std::vector<std::vector<std::byte>> candidates;
        (void)AssemblySolver{}.CollectVerified(
            p,
            [&](const auto& candidate, const auto& stage) {
                EXPECT_NE(stage, "portfolio-radial-wheel");
                candidates.push_back(io::BinaryWriter::WriteSolution(candidate));
                io::VerificationReport report;
                report.status = io::VerificationStatus::Passed;
                report.authority = io::VerificationAuthority::LegacyGate;
                return report;
            },
            8);
        if (std::string_view(mode) == "off")
            baseline = candidates;
        else
            EXPECT_EQ(candidates, baseline);
    }
}
#ifdef HAS_LIBVERIFY
TEST(WheelAssemblyTest, RadialReuseWholeMachineVerifiesTwiceOnOriginal) {
    const auto puzzle = io::BinaryParser::ParsePuzzleFile(puzzlePath);
    const auto path = std::filesystem::current_path() / "radial-wheel-test.solution";
    struct Cleanup {
        std::filesystem::path path;
        ~Cleanup() {
            std::error_code ec;
            std::filesystem::remove(path, ec);
        }
    } cleanup{path};
    int oldCost = 0;
    for (bool radial : {false, true}) {
        auto solution = radial ? CompileRadialWheelAssembly(puzzle) : CompileWheelAssembly(puzzle);
        io::BinaryWriter::WriteSolutionFile(path, solution);
        auto report = io::VerifySolutionFiles(puzzlePath, path, 1000000, 10000);
        ASSERT_TRUE(report.Passed()) << report.detail << " cycle=" << report.errorCycle
                                     << " cell=" << report.errorU << ',' << report.errorV;
        solution.metrics = report.metrics;
        io::BinaryWriter::WriteSolutionFile(path, solution);
        const auto measured = report.metrics;
        report = io::VerifySolutionFiles(puzzlePath, path, 1000000, 10000);
        ASSERT_TRUE(report.Passed()) << report.detail;
        EXPECT_EQ(report.metrics.cost, measured.cost);
        EXPECT_EQ(report.metrics.cycles, measured.cycles);
        EXPECT_EQ(report.metrics.area, measured.area);
        if (!radial)
            oldCost = measured.cost;
        else
            EXPECT_LT(measured.cost, oldCost);
        std::cout << "radial=" << radial << " cost=" << measured.cost
                  << " cycles=" << measured.cycles << " area=" << measured.area << '\n';
    }
}
TEST(WheelAssemblyTest, MolecularProjectionNormalizationVerifiesTwice) {
    for (const auto* file : {"asset/puzzle/tourney-2020/OM2020_W7_HighGlossFinish.puzzle",
                             "asset/puzzle/workshop-versions/w2501728349.puzzle"}) {
        SCOPED_TRACE(file);
        const auto source = root / file;
        const auto puzzle = io::BinaryParser::ParsePuzzleFile(source);
        auto solution = CompileWheelAssembly(puzzle);
        const auto path = std::filesystem::current_path() / "wheel-projection-test.solution";
        struct Cleanup {
            std::filesystem::path path;
            ~Cleanup() {
                std::error_code ec;
                std::filesystem::remove(path, ec);
            }
        } cleanup{path};
        io::BinaryWriter::WriteSolutionFile(path, solution);
        auto report = io::VerifySolutionFiles(source, path, 1000000, 10000);
        ASSERT_TRUE(report.Passed()) << report.detail << " cycle=" << report.errorCycle;
        solution.metrics = report.metrics;
        io::BinaryWriter::WriteSolutionFile(path, solution);
        report = io::VerifySolutionFiles(source, path, 1000000, 10000);
        ASSERT_TRUE(report.Passed()) << report.detail;
        EXPECT_EQ(report.metrics.cost, solution.metrics.cost);
        EXPECT_EQ(report.metrics.cycles, solution.metrics.cycles);
        EXPECT_EQ(report.metrics.area, solution.metrics.area);
        EXPECT_EQ(report.metrics.instructionCount, solution.metrics.instructionCount);
    }
}
TEST(WheelAssemblyTest, MolecularSupplyNormalizationVerifiesTwice) {
    const auto source = root / "asset/puzzle/tourney-2021/OM2021_W8.puzzle";
    for (bool calcify : {false, true}) {
        SCOPED_TRACE(calcify);
        auto puzzle = io::BinaryParser::ParsePuzzleFile(source);
        if (calcify) {
            puzzle.permissionBits |= 1ULL << 12;
            for (auto& input : puzzle.inputs)
                for (auto& atom : input.atoms)
                    atom.type = core::AtomType::Fire;
        } else {
            puzzle.permissionBits &= ~(1ULL << 12);
        }
        const auto stem = std::filesystem::current_path() / "wheel-normalized-test";
        const auto puzzleFile = std::filesystem::path(stem.string() + ".puzzle");
        const auto solutionFile = std::filesystem::path(stem.string() + ".solution");
        struct Cleanup {
            std::filesystem::path puzzle, solution;
            ~Cleanup() {
                std::error_code ec;
                std::filesystem::remove(puzzle, ec);
                std::filesystem::remove(solution, ec);
            }
        } cleanup{puzzleFile, solutionFile};
        {
            std::ofstream out(puzzleFile, std::ios::binary);
            auto word = [&](std::uint64_t value, int bytes) {
                for (int i = 0; i < bytes; ++i)
                    out.put(static_cast<char>(value >> (8 * i)));
            };
            word(3, 4);
            word(puzzle.name.size(), 1);
            out.write(puzzle.name.data(), puzzle.name.size());
            word(0, 8);
            word(puzzle.permissionBits, 8);
            for (const auto* molecules : {&puzzle.inputs, &puzzle.outputs}) {
                word(molecules->size(), 4);
                for (const auto& molecule : *molecules) {
                    word(molecule.atoms.size(), 4);
                    for (const auto& atom : molecule.atoms) {
                        word(static_cast<std::uint8_t>(atom.type), 1);
                        word(atom.position.q, 1);
                        word(atom.position.r, 1);
                    }
                    word(molecule.bonds.size(), 4);
                    for (const auto& bond : molecule.bonds) {
                        word(bond.bits, 1);
                        word(bond.from.q, 1);
                        word(bond.from.r, 1);
                        word(bond.to.q, 1);
                        word(bond.to.r, 1);
                    }
                }
            }
            word(puzzle.outputMultiplier, 4);
            word(0, 1);
        }
        auto solution = CompileWheelAssembly(puzzle);
        EXPECT_EQ(solution.solutionName, "Molecular disassembly with Van Berlo conversion");
        if (calcify) {
            EXPECT_TRUE(
                std::any_of(solution.objects.begin(), solution.objects.end(),
                            [](const auto& obj) { return obj.typeName == "glyph-calcification"; }));
        }
        io::BinaryWriter::WriteSolutionFile(solutionFile, solution);
        auto report = io::VerifySolutionFiles(puzzleFile, solutionFile, 1000000, 10000);
        ASSERT_TRUE(report.Passed()) << report.detail << " cycle=" << report.errorCycle
                                     << " cell=" << report.errorU << ',' << report.errorV;
        solution.metrics = report.metrics;
        io::BinaryWriter::WriteSolutionFile(solutionFile, solution);
        const auto measured = report.metrics;
        report = io::VerifySolutionFiles(puzzleFile, solutionFile, 1000000, 10000);
        ASSERT_TRUE(report.Passed()) << report.detail;
        EXPECT_EQ(report.metrics.cycles, measured.cycles);
        EXPECT_EQ(report.metrics.cost, measured.cost);
        EXPECT_EQ(report.metrics.area, measured.area);
        EXPECT_EQ(report.metrics.instructionCount, measured.instructionCount);
        if (calcify) {
            puzzle.permissionBits &= ~(1ULL << 12);
            EXPECT_THROW((void)CompileWheelAssembly(puzzle), std::invalid_argument);
        }
    }
}
TEST(WheelAssemblyTest, SupplyFailureFallsBackToNormalizedWheelAndVerifiesTwice) {
    const auto source = root / "asset/puzzle/tourney-2021/OM2021_W3.puzzle";
    const auto puzzle = io::BinaryParser::ParsePuzzleFile(source);
    EXPECT_NO_THROW((void)AssemblyPlanner::Plan(puzzle));
    EXPECT_THROW((void)AssemblyPlanner::PlanSupply(puzzle), std::invalid_argument);
    int rejectedCandidates = 0;
    auto solution = AssemblySolver{}.SolveAccepted(puzzle, [&](const io::SolutionData& candidate) {
        const bool wheel =
            std::any_of(candidate.objects.begin(), candidate.objects.end(),
                        [](const auto& object) { return object.typeName == "baron"; });
        if (!wheel) ++rejectedCandidates;
        return wheel;
    });
    EXPECT_GT(rejectedCandidates, 0);
    EXPECT_EQ(solution.solutionName, "Molecular disassembly with Van Berlo conversion");
    const auto path = std::filesystem::current_path() / "wheel-supply-fallback-test.solution";
    struct Cleanup {
        std::filesystem::path path;
        ~Cleanup() {
            std::error_code ec;
            std::filesystem::remove(path, ec);
        }
    } cleanup{path};
    io::BinaryWriter::WriteSolutionFile(path, solution);
    auto report = io::VerifySolutionFiles(source, path, 1000000, 10000);
    ASSERT_TRUE(report.Passed()) << report.detail << " cycle=" << report.errorCycle;
    solution.metrics = report.metrics;
    io::BinaryWriter::WriteSolutionFile(path, solution);
    const auto measured = report.metrics;
    report = io::VerifySolutionFiles(source, path, 1000000, 10000);
    ASSERT_TRUE(report.Passed()) << report.detail << " cycle=" << report.errorCycle;
    EXPECT_EQ(report.metrics.cycles, measured.cycles);
    EXPECT_EQ(report.metrics.cost, measured.cost);
    EXPECT_EQ(report.metrics.area, measured.area);
    EXPECT_EQ(report.metrics.instructionCount, measured.instructionCount);
}
TEST(WheelAssemblyTest, OriginalP024VerifiesTwiceWithMeasuredMetrics) {
    const auto original = root / "asset/puzzle/campaign/ch3/P024.puzzle";
    const auto puzzle = io::BinaryParser::ParsePuzzleFile(original);
    auto solution = AssemblySolver{}.Solve(puzzle);
    EXPECT_EQ(solution.solutionName, "Molecular disassembly with Van Berlo conversion");
    const auto path = std::filesystem::current_path() / "wheel-molecular-test.solution";
    struct Cleanup {
        std::filesystem::path path;
        ~Cleanup() {
            std::error_code ec;
            std::filesystem::remove(path, ec);
        }
    } cleanup{path};
    for (const auto* type : {"baron", "glyph-duplication", "unbonder"})
        EXPECT_TRUE(std::any_of(solution.objects.begin(), solution.objects.end(),
                                [&](const auto& obj) { return obj.typeName == type; }));
    int inputs = 0, outputs = 0;
    for (const auto& obj : solution.objects) {
        if (obj.typeName == "input") {
            ++inputs;
            EXPECT_EQ(obj.bodyData.at(2), 0);
        }
        if (obj.typeName == "out-std") ++outputs;
    }
    EXPECT_EQ(inputs, 1);
    EXPECT_EQ(outputs, 4);
    io::BinaryWriter::WriteSolutionFile(path, solution);
    auto report = io::VerifySolutionFiles(original, path, 1000000, 10000);
    ASSERT_TRUE(report.Passed()) << report.detail << " cycle=" << report.errorCycle
                                 << " cell=" << report.errorU << ',' << report.errorV;
    solution.metrics = report.metrics;
    EXPECT_GT(solution.metrics.cycles, 0);
    EXPECT_GT(solution.metrics.cost, 0);
    io::BinaryWriter::WriteSolutionFile(path, solution);
    const auto measured = report.metrics;
    report = io::VerifySolutionFiles(original, path, 1000000, 10000);
    ASSERT_TRUE(report.Passed()) << report.detail << " cycle=" << report.errorCycle;
    EXPECT_EQ(report.metrics.cycles, measured.cycles);
    EXPECT_EQ(report.metrics.cost, measured.cost);
    EXPECT_EQ(report.metrics.area, measured.area);
    EXPECT_EQ(report.metrics.instructionCount, measured.instructionCount);
    std::cout << "P024 measured cycles=" << measured.cycles << " cost=" << measured.cost << '\n';
}
TEST(WheelAssemblyTest, OriginalP054AndP055VerifyTwiceWithMeasuredMetrics) {
    for (const auto* filename : {"P054.puzzle", "P055.puzzle"}) {
        SCOPED_TRACE(filename);
        const auto source = puzzlePath.parent_path() / filename;
        const auto path = std::filesystem::current_path() / "wheel-assembly-test.solution";
        struct Cleanup {
            std::filesystem::path path;
            ~Cleanup() {
                std::error_code ec;
                std::filesystem::remove(path, ec);
            }
        } cleanup{path};
        const auto puzzle = io::BinaryParser::ParsePuzzleFile(source);
        auto solution = AssemblySolver{}.Solve(puzzle);
        for (const auto* type : {"baron", "glyph-duplication"})
            EXPECT_TRUE(std::any_of(solution.objects.begin(), solution.objects.end(),
                                    [&](const auto& obj) { return obj.typeName == type; }));
        for (const auto& obj : solution.objects) {
            if (obj.typeName == "input") {
                ASSERT_LT(obj.bodyData.at(2), static_cast<int>(puzzle.inputs.size()));
            }
        }
        io::BinaryWriter::WriteSolutionFile(path, solution);
        auto report = io::VerifySolutionFiles(source, path, 1000000, 10000);
        ASSERT_TRUE(report.Passed()) << report.detail << " cycle=" << report.errorCycle
                                     << " cell=" << report.errorU << ',' << report.errorV;
        solution.metrics = report.metrics;
        EXPECT_GT(solution.metrics.cycles, 0);
        EXPECT_GT(solution.metrics.cost, 0);
        io::BinaryWriter::WriteSolutionFile(path, solution);
        const auto measured = report.metrics;
        report = io::VerifySolutionFiles(source, path, 1000000, 10000);
        EXPECT_TRUE(report.Passed()) << report.detail << " cycle=" << report.errorCycle;
        EXPECT_EQ(report.metrics.cycles, measured.cycles);
        EXPECT_EQ(report.metrics.cost, measured.cost);
        EXPECT_EQ(report.metrics.area, measured.area);
        EXPECT_EQ(report.metrics.instructionCount, measured.instructionCount);
        std::cout << filename << " measured cycles=" << solution.metrics.cycles
                  << " cost=" << solution.metrics.cost << '\n';
    }
}
#endif
}} // namespace opusoptima::solvers
