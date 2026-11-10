#include "DemandDrivenChain.hpp"
#include "AssemblySolver.hpp"
#include "IO/BinaryWriter.hpp"
#include "IO/CandidateArchive.hpp"
#include "IO/SolutionVerifier.hpp"
#include "LifeDeath.hpp"
#include "LocalOptimizer.hpp"
#include "PurificationAssembly.hpp"
#include "RepoRoot.hpp"
#include "TestOutputDirectory.hpp"
#include "Verification/StaticProductionVerifier.hpp"
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
namespace opusoptima::solvers { namespace {
const auto root = opusoptima::test::RepoRoot(__FILE__);
io::PuzzleData Domain() {
    auto p = io::BinaryParser::ParsePuzzleFile(root / "asset/puzzle/campaign/ch4/P032.puzzle");
    p.name = "Real chemistry reduction two consumer chain";
    p.outputs = {
        {{{core::AtomType::Vitae, {0, 0}}, {core::AtomType::Water, {0, 1}}}, {{1, {0, 0}, {0, 1}}}},
        {{{core::AtomType::Mors, {0, 0}}, {core::AtomType::Water, {0, -1}}},
         {{1, {0, 0}, {0, -1}}}}};
    return p;
}
void Gate(io::PuzzleData puzzle, const std::string& stem, bool nativeGate, bool consumers = false,
          int purificationDepth = 0, int expectedCycles = 0) {
    const bool embedding = stem.starts_with("embedding-");
    const test::ScopedTestDirectory scratch;
    const auto& dir = scratch.Path();
    std::filesystem::create_directories(dir);
    const auto path = dir / (stem + ".puzzle");
    {
        std::ofstream out(path, std::ios::binary);
        auto word = [&](std::uint64_t v, int bytes) {
            for (int i = 0; i < bytes; ++i)
                out.put(static_cast<char>(v >> (8 * i)));
        };
        word(3, 4);
        word(puzzle.name.size(), 1);
        out.write(puzzle.name.data(), puzzle.name.size());
        word(0, 8);
        word(puzzle.permissionBits, 8);
        for (const auto* list : {&puzzle.inputs, &puzzle.outputs}) {
            word(list->size(), 4);
            for (const auto& molecule : *list) {
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
        word(1, 4);
        word(0, 1);
    }
    const auto parsed = io::BinaryParser::ParsePuzzleFile(path);
    const auto begin = std::chrono::steady_clock::now();
    const auto solution =
        stem.starts_with("chain-")
            ? CompileSingletonPurificationChain(parsed, std::chrono::steady_clock::now() +
                                                            std::chrono::seconds(10))
        : embedding                         ? CompileStaticEmbedding(parsed)
        : stem.starts_with("purification-") ? CompileStaticReaction(parsed)
        : consumers                         ? CompileStaticReaction(parsed)
                                            : CompileDemandDrivenChain(parsed);
    const auto compileUs = std::chrono::duration_cast<std::chrono::microseconds>(
                               std::chrono::steady_clock::now() - begin)
                               .count();
    const auto file = dir / (stem + ".solution");
    io::BinaryWriter::WriteSolutionFile(file, solution);
    if (nativeGate) {
        const auto report = io::VerifySolutionFiles(path, file, 150000, 10000);
        std::ofstream(dir / (stem + ".txt"))
            << "passed=" << report.Passed() << " cost=" << report.metrics.cost
            << " cycles=" << report.metrics.cycles << " area=" << report.metrics.area
            << " instructions=" << report.metrics.instructionCount << " compile_us=" << compileUs
            << " first_verified_us="
            << std::chrono::duration_cast<std::chrono::microseconds>(
                   std::chrono::steady_clock::now() - begin)
                   .count()
            << " " << report.detail << '\n';
        EXPECT_TRUE(report.Passed()) << report.detail;
        EXPECT_EQ(report.authority, io::VerificationAuthority::LegacyGate);
        if (purificationDepth) {
            EXPECT_EQ(report.authority, io::VerificationAuthority::LegacyGate);
            EXPECT_EQ(report.metrics.cost, 10 * purificationDepth * (purificationDepth + 1));
            EXPECT_EQ(report.metrics.area, (purificationDepth + 1) * (purificationDepth + 2) / 2);
            EXPECT_EQ(report.metrics.cycles, expectedCycles);
            EXPECT_EQ(report.metrics.instructionCount, 0);
        }
    }
    if (stem == "reduction") {
        const auto baseline = CompileLifeDeath(parsed);
        const auto baselineFile = dir / "reduction-baseline.solution";
        io::BinaryWriter::WriteSolutionFile(baselineFile, baseline);
        if (nativeGate) {
            const auto verified = io::VerifySolutionFiles(path, baselineFile, 150000, 10000);
            std::ofstream(dir / "reduction-baseline.txt")
                << "passed=" << verified.Passed() << " cost=" << verified.metrics.cost
                << " cycles=" << verified.metrics.cycles << " area=" << verified.metrics.area << " "
                << verified.detail << '\n';
            EXPECT_TRUE(verified.Passed()) << verified.detail;
        }
    }
}
io::PuzzleData SingletonPurification() {
    auto p = Domain();
    p.name = "Singleton metal purification";
    p.permissionBits = 1ULL << 15;
    p.inputs = {{{{core::AtomType::Lead, {2, -3}}}, {}}, {{{core::AtomType::Gold, {-2, 1}}}, {}}};
    p.outputs = {{{{core::AtomType::Tin, {3, 2}}}, {}}};
    return p;
}
TEST(StaticReactionTest, AdjacentSingletonMetalsAndInputIndexInvariance) {
    for (int source = 8; source <= 12; ++source) {
        auto p = SingletonPurification();
        p.inputs[0].atoms[0].type = static_cast<core::AtomType>(source);
        p.outputs[0].atoms[0].type = static_cast<core::AtomType>(source - 1);
        if (source % 2) std::reverse(p.inputs.begin(), p.inputs.end());
        p.outputs[0].atoms[0].position = core::RotateClockwise(p.outputs[0].atoms[0].position);
        p.name = "Bounded ordinary Animismus consumer embedding";
        const auto compiled = CompileStaticReactionWithFamily(p);
        EXPECT_EQ(compiled.family, StaticReactionFamily::Purification);
        const auto& station = compiled.solution;
        EXPECT_EQ(io::BinaryWriter::WriteSolution(station),
                  io::BinaryWriter::WriteSolution(CompileStaticReaction(p)));
        ASSERT_EQ(station.objects.size(), 4U);
        int inputs = 0;
        for (const auto& object : station.objects)
            if (object.typeName == "input") {
                ++inputs;
                EXPECT_EQ(static_cast<int>(p.inputs.at(object.bodyData[2]).atoms[0].type), source);
            }
        EXPECT_EQ(inputs, 2);
#ifdef HAS_LIBVERIFY
        Gate(p, "purification-metal-" + std::to_string(source), true);
#endif
    }
}
TEST(StaticReactionTest, MultistageSingletonMetalsAndNearestFeed) {
    for (const auto [depth, cycles] : {std::pair{1, 6}, {2, 13}, {3, 21}, {4, 40}, {5, 69}}) {
        auto p = SingletonPurification();
        p.outputs[0].atoms[0].type = static_cast<core::AtomType>(12 - depth);
        std::reverse(p.inputs.begin(), p.inputs.end());
        const auto compiled = CompileStaticReactionWithFamily(p);
        const auto& station = compiled.solution;
        io::VerificationReport accepted;
        accepted.status = io::VerificationStatus::Passed;
        accepted.authority = io::VerificationAuthority::LegacyGate;
        EXPECT_EQ(StaticEmbeddingSuppressionAfterReaction(p, compiled.family, accepted).has_value(),
                  depth != 5);
        auto embeddingPuzzle = p;
        std::erase_if(embeddingPuzzle.inputs, [&](const auto& input) {
            return input.atoms[0].type == p.outputs[0].atoms[0].type;
        });
        embeddingPuzzle.permissionBits |= (1ULL << 0) | (1ULL << 8) | (1ULL << 25);
        EXPECT_EQ(
            StaticEmbeddingSuppressionAfterReaction(embeddingPuzzle, compiled.family, accepted),
            StaticEmbeddingSuppressionReason::AcceptedSingletonPurification);
        auto embedded = CompileStaticEmbedding(embeddingPuzzle);
        ASSERT_EQ(embedded.objects.size(), station.objects.size() + 1);
        ASSERT_EQ(embedded.objects.back().typeName, "arm1");
        ASSERT_EQ(embedded.objects.back().instructions.size(), 1U);
        EXPECT_EQ(embedded.objects.back().instructions[0].action, 'O');
        embedded.objects.pop_back();
        auto triangle = CompileStaticReaction(embeddingPuzzle);
        for (auto* machine : {&embedded, &triangle}) {
            machine->solutionName.clear();
            for (auto& object : machine->objects) {
                if (machine == &embedded) object.position.r += depth;
                object.bodyData[4] = 0;
            }
        }
        EXPECT_EQ(io::BinaryWriter::WriteSolution(embedded),
                  io::BinaryWriter::WriteSolution(triangle));
        for (auto status : {io::VerificationStatus::Rejected, io::VerificationStatus::Timeout,
                            io::VerificationStatus::Unavailable, io::VerificationStatus::CycleLimit,
                            io::VerificationStatus::Error}) {
            auto rejected = accepted;
            rejected.status = status;
            EXPECT_FALSE(StaticEmbeddingSuppressionAfterReaction(p, compiled.family, rejected));
        }
        for (auto authority :
             {io::VerificationAuthority::Independent, io::VerificationAuthority::None}) {
            auto nonnative = accepted;
            nonnative.authority = authority;
            EXPECT_FALSE(StaticEmbeddingSuppressionAfterReaction(p, compiled.family, nonnative));
        }
        EXPECT_FALSE(StaticEmbeddingSuppressionAfterReaction(p, StaticReactionFamily::OrdinaryPair,
                                                             accepted));
        EXPECT_FALSE(StaticEmbeddingSuppressionAfterReaction(
            p, StaticReactionFamily::LegacyConsumers, accepted));
        auto direct = embeddingPuzzle;
        direct.inputs.push_back({{{p.outputs[0].atoms[0].type, {0, 0}}}, {}});
        EXPECT_FALSE(StaticEmbeddingSuppressionAfterReaction(direct, compiled.family, accepted));
        const auto directMachine = CompileStaticEmbedding(direct);
        EXPECT_EQ(std::count_if(
                      directMachine.objects.begin(), directMachine.objects.end(),
                      [](const auto& object) { return object.typeName == "glyph-purification"; }),
                  0);
        auto multiple = p;
        multiple.outputs.push_back(p.outputs[0]);
        EXPECT_FALSE(StaticEmbeddingSuppressionAfterReaction(multiple, compiled.family, accepted));
        auto molecular = p;
        molecular.outputs[0].atoms.push_back({core::AtomType::Salt, {4, 2}});
        EXPECT_FALSE(StaticEmbeddingSuppressionAfterReaction(molecular, compiled.family, accepted));
        EXPECT_EQ(station.objects.size(),
                  static_cast<std::size_t>(depth + 2 + depth * (depth + 1) / 2));
        for (const auto& object : station.objects)
            if (object.typeName == "input") EXPECT_EQ(object.bodyData[2], 1);
#ifdef HAS_LIBVERIFY
        Gate(p, "purification-depth-" + std::to_string(depth), true, false, depth, cycles);
#endif
        p.inputs.push_back({{{static_cast<core::AtomType>(13 - depth), {-4, 2}}}, {}});
        const auto nearest = CompileStaticReaction(p);
        ASSERT_EQ(nearest.objects.size(), 4U);
        EXPECT_EQ(nearest.objects[0].bodyData[2], depth == 1 ? 1 : 2);
        EXPECT_EQ(nearest.objects[1].bodyData[2], depth == 1 ? 1 : 2);
    }
}
TEST(StaticReactionTest, SingletonChainPermissionsDeadlineAndBoundaries) {
    auto p = io::BinaryParser::ParsePuzzleFile(root / "asset/puzzle/campaign/ch4/P036.puzzle");
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    ASSERT_TRUE(SupportsSingletonPurificationChain(p));
    const auto chain = CompileSingletonPurificationChain(p, deadline);
    EXPECT_EQ(std::count_if(chain.objects.begin(), chain.objects.end(),
                            [](const auto& o) { return o.typeName == "glyph-purification"; }),
              5);
    EXPECT_EQ(chain.objects.size(), 9U);
    EXPECT_THROW((void)CompileSingletonPurificationChain(p, std::chrono::steady_clock::now()),
                 std::invalid_argument);
    for (int bit : {0, 2, 3, 15, 22, 23, 25}) {
        auto q = p;
        q.permissionBits &= ~(1ULL << bit);
        EXPECT_FALSE(SupportsSingletonPurificationChain(q));
        EXPECT_THROW((void)CompileSingletonPurificationChain(q, deadline), std::invalid_argument);
    }
    for (int defect = 0; defect < 8; ++defect) {
        auto q = p;
        if (defect == 0) q.outputMultiplier = 2;
        if (defect == 1) q.inputs[0].atoms.push_back(q.inputs[0].atoms[0]);
        if (defect == 2) q.outputs[0].atoms.push_back(q.outputs[0].atoms[0]);
        if (defect == 3) q.inputs[0].atoms[0].type = core::AtomType::Salt;
        if (defect == 4) q.productionInfo.emplace();
        if (defect == 5) q.outputs[0].bonds.push_back({1, {0, 0}, {1, 0}});
        if (defect == 6) q.inputs.push_back({{{core::AtomType::Silver, {0, 0}}}, {}});
        if (defect == 7) q.version = 2;
        EXPECT_FALSE(SupportsSingletonPurificationChain(q)) << defect;
        EXPECT_THROW((void)CompileSingletonPurificationChain(q, deadline), std::invalid_argument);
    }
    p.inputs.resize(1);
    p.inputs[0].atoms = {{core::AtomType::Copper, {-3, 2}}};
    p.outputs[0].atoms = {{core::AtomType::Gold, core::RotateClockwise({2, -1})}};
    ASSERT_TRUE(SupportsSingletonPurificationChain(p));
#ifdef HAS_LIBVERIFY
    Gate(p, "chain-transformed-depth-two", true);
#endif
}
#ifdef HAS_LIBVERIFY
TEST(StaticReactionTest, SingletonChainOriginalWholeMachinePeriodAndGateRefusal) {
    const auto path = root / "asset/puzzle/campaign/ch4/P036.puzzle";
    const auto p = io::BinaryParser::ParsePuzzleFile(path);
    ASSERT_EQ(p.outputMultiplier, 1);
    const test::ScopedTestDirectory scratch;
    const auto file = scratch.Path() / "chain.solution";
    auto chain = CompileSingletonPurificationChain(p, std::chrono::steady_clock::now() +
                                                          std::chrono::seconds(10));
    for (int replay = 0; replay < 2; ++replay) {
        io::BinaryWriter::WriteSolutionFile(file, chain);
        const auto report = io::VerifySolutionFiles(path, file, 150000, 10000);
        ASSERT_TRUE(report.Passed()) << report.detail;
        EXPECT_EQ(report.authority, io::VerificationAuthority::LegacyGate);
        EXPECT_EQ(report.metrics.cost, 225);
        EXPECT_EQ(report.metrics.cycles, 5554);
        EXPECT_EQ(report.metrics.area, 50);
        EXPECT_EQ(report.metrics.instructionCount, 867);
        chain.metrics = report.metrics;
    }
    auto& tape = chain.objects.back().instructions;
    ASSERT_EQ(tape.front().action, 'E');
    tape.front().action = 'O';
    io::BinaryWriter::WriteSolutionFile(file, chain);
    EXPECT_FALSE(io::VerifySolutionFiles(path, file, 150000, 10000).Passed());
}
TEST(StaticReactionTest, OriginalMultistagePurificationNativeCompletion) {
    const auto path = root / "asset/puzzle/campaign/ch4/P036.puzzle";
    const auto p = io::BinaryParser::ParsePuzzleFile(path);
    const test::ScopedTestDirectory scratch;
    const auto file = scratch.Path() / "cascade.solution";
    io::BinaryWriter::WriteSolutionFile(file, CompileStaticReaction(p));
    const auto report = io::VerifySolutionFiles(path, file, 150000, 10000);
    ASSERT_TRUE(report.Passed()) << report.detail;
    EXPECT_EQ(report.authority, io::VerificationAuthority::LegacyGate);
    EXPECT_EQ(report.metrics.cycles, 69);
    EXPECT_EQ(report.metrics.cost, 300);
    EXPECT_EQ(report.metrics.area, 21);
    EXPECT_EQ(report.metrics.instructionCount, 0);
    const char* value = std::getenv("OPUSOPTIMA_STATIC_EMBEDDING");
    struct Restore {
        std::optional<std::string> prior;
        ~Restore() {
            if (prior)
                setenv("OPUSOPTIMA_STATIC_EMBEDDING", prior->c_str(), 1);
            else
                unsetenv("OPUSOPTIMA_STATIC_EMBEDDING");
        }
    } restore{value ? std::optional<std::string>(value) : std::nullopt};
    std::vector<std::pair<std::string, std::vector<std::byte>>> defaultCandidates;
    for (bool oldPath : {false, true}) {
        if (oldPath)
            setenv("OPUSOPTIMA_STATIC_EMBEDDING", "off", 1);
        else
            unsetenv("OPUSOPTIMA_STATIC_EMBEDDING");
        std::vector<std::pair<std::string, std::vector<std::byte>>> candidates;
        const auto first = AssemblySolver{}.CollectVerified(
            p,
            [&](const auto& candidate, const auto& stage) {
                candidates.emplace_back(stage, io::BinaryWriter::WriteSolution(candidate));
                io::BinaryWriter::WriteSolutionFile(file, candidate);
                const auto verified = io::VerifySolutionFiles(path, file, 150000, 10000);
                EXPECT_TRUE(verified.Passed()) << stage << ": " << verified.detail;
                EXPECT_EQ(verified.authority, io::VerificationAuthority::LegacyGate);
                if (stage == "portfolio-static-reaction-purification") {
                    EXPECT_EQ(candidates.back().second,
                              io::BinaryWriter::WriteSolution(CompileStaticReaction(p)));
                    EXPECT_EQ(verified.metrics.cost, 300);
                    EXPECT_EQ(verified.metrics.cycles, 69);
                    EXPECT_EQ(verified.metrics.area, 21);
                }
                if (stage == "portfolio-singleton-purification-chain") {
                    EXPECT_EQ(verified.metrics.cost, 225);
                    EXPECT_EQ(verified.metrics.cycles, 5554);
                    EXPECT_EQ(verified.metrics.area, 50);
                    if (!oldPath) {
                        RecordProperty("default_cap3_chain_cost", verified.metrics.cost);
                        RecordProperty("default_cap3_chain_cycles", verified.metrics.cycles);
                    }
                }
                return verified;
            },
            3);
        ASSERT_EQ(candidates.size(), 3U);
        auto unmeasuredFirst = first;
        unmeasuredFirst.metrics = {};
        EXPECT_EQ(io::BinaryWriter::WriteSolution(unmeasuredFirst), candidates[0].second);
        EXPECT_EQ(candidates[1].first, "portfolio-static-reaction-purification");
        EXPECT_EQ(candidates[2].first, "portfolio-singleton-purification-chain");
        if (oldPath)
            EXPECT_EQ(candidates, defaultCandidates);
        else
            defaultCandidates = candidates;
    }
}
TEST(StaticReactionTest, OriginalSingletonPurificationRepeatedNativeCompletion) {
    const auto path = root / "asset/puzzle/edge-cases/c988640836512960.puzzle";
    const auto p = io::BinaryParser::ParsePuzzleFile(path);
    ASSERT_EQ(p.outputMultiplier, 1);
    const auto station = CompileStaticReaction(p);
    const test::ScopedTestDirectory scratch;
    const auto file = scratch.Path() / "station.solution";
    io::BinaryWriter::WriteSolutionFile(file, station);
    for (int repeat = 0; repeat < 2; ++repeat) {
        const auto report = io::VerifySolutionFiles(path, file, 150000, 10000);
        ASSERT_TRUE(report.Passed()) << report.detail;
        EXPECT_EQ(report.authority, io::VerificationAuthority::LegacyGate);
        EXPECT_EQ(report.metrics.cycles, 6);
        EXPECT_EQ(report.metrics.cost, 20);
        EXPECT_EQ(report.metrics.area, 3);
        EXPECT_EQ(report.metrics.instructionCount, 0);
    }
}
#endif
TEST(StaticReactionTest, SingletonPurificationBoundaries) {
    const auto p = SingletonPurification();
    auto q = p;
    q.permissionBits = 0;
    EXPECT_THROW((void)CompileStaticReaction(q), std::invalid_argument);
    q = p;
    q.outputs[0].atoms[0].type = core::AtomType::Lead;
    EXPECT_THROW((void)CompileStaticReaction(q), std::invalid_argument);
    q = p;
    q.inputs[0].atoms[0].type = core::AtomType::Water;
    EXPECT_THROW((void)CompileStaticReaction(q), std::invalid_argument);
    q = p;
    q.outputs[0].atoms[0].type = core::AtomType::Salt;
    EXPECT_THROW((void)CompileStaticReaction(q), std::invalid_argument);
    q = p;
    q.inputs.clear();
    EXPECT_THROW((void)CompileStaticReaction(q), std::invalid_argument);
    q = p;
    q.inputs.resize(9, p.inputs[0]);
    EXPECT_THROW((void)CompileStaticReaction(q), std::invalid_argument);
    q = p;
    q.inputs[0].atoms.push_back({core::AtomType::Lead, {0, 0}});
    EXPECT_THROW((void)CompileStaticReaction(q), std::invalid_argument);
    q = p;
    q.outputs[0].atoms.push_back({core::AtomType::Tin, {0, 0}});
    EXPECT_THROW((void)CompileStaticReaction(q), std::invalid_argument);
    q = p;
    q.productionInfo.emplace();
    EXPECT_THROW((void)CompileStaticReaction(q), std::invalid_argument);
    q = p;
    q.outputMultiplier = 2;
    EXPECT_THROW((void)CompileStaticReaction(q), std::invalid_argument);
}
void RunCompleteReactionReduction(bool nativeGate) {
    Gate(Domain(), "reduction", nativeGate);
}
TEST(DemandDrivenChainTest, CompleteReactionReductionSemantics) {
    RunCompleteReactionReduction(false);
}
#ifdef HAS_LIBVERIFY
TEST(DemandDrivenChainTest, CompleteReactionReduction) {
    RunCompleteReactionReduction(true);
}
#endif
void RunTranslatedReorderedHoldout(bool nativeGate) {
    auto p = Domain();
    p.name = "translated reordered chain";
    std::reverse(p.outputs.begin(), p.outputs.end());
    for (auto& m : p.outputs) {
        for (auto& a : m.atoms) {
            a.position.q += 3;
            a.position.r -= 2;
        }
        for (auto& b : m.bonds) {
            b.from.q += 3;
            b.from.r -= 2;
            b.to.q += 3;
            b.to.r -= 2;
        }
        std::reverse(m.atoms.begin(), m.atoms.end());
    }
    Gate(p, "holdout", nativeGate);
}
TEST(DemandDrivenChainTest, TranslatedReorderedHoldoutSemantics) {
    RunTranslatedReorderedHoldout(false);
}
#ifdef HAS_LIBVERIFY
TEST(DemandDrivenChainTest, TranslatedReorderedHoldout) {
    RunTranslatedReorderedHoldout(true);
}
#endif
TEST(DemandDrivenChainTest, PermissionAndActualDomainBoundary) {
    for (int bit : {0, 9, 12, 16}) {
        auto p = Domain();
        p.permissionBits &= ~(1ULL << bit);
        EXPECT_THROW(CompileDemandDrivenChain(p), std::invalid_argument);
    }
    for (const auto* file :
         {"asset/puzzle/campaign/ch4/P032.puzzle", "asset/puzzle/journal/journal8/P102.puzzle"}) {
        const auto p = io::BinaryParser::ParsePuzzleFile(root / file);
        EXPECT_THROW(CompileDemandDrivenChain(p), std::invalid_argument);
    }
}
}} // namespace opusoptima::solvers

namespace opusoptima::solvers { namespace {
void RunOriginalPrototypeFullGate(bool nativeGate) {
    const auto puzzle = root / "asset/puzzle/campaign/ch4/P032.puzzle";
    const auto p = io::BinaryParser::ParsePuzzleFile(puzzle);
    const auto begin = std::chrono::steady_clock::now();
    const auto candidate = CompileDemandDrivenConsumers(p);
    const auto compiled = CompileStaticReactionWithFamily(p);
    EXPECT_EQ(compiled.family, StaticReactionFamily::LegacyConsumers);
    EXPECT_EQ(io::BinaryWriter::WriteSolution(compiled.solution),
              io::BinaryWriter::WriteSolution(candidate));
    EXPECT_EQ(io::CandidateSha256(io::BinaryWriter::WriteSolution(candidate)),
              "1ea15c315a2055ba1c216604337f975ec2aec4b09e885c131e82ed446ac1da7e");
    const auto us = std::chrono::duration_cast<std::chrono::microseconds>(
                        std::chrono::steady_clock::now() - begin)
                        .count();
    const test::ScopedTestDirectory scratch;
    const auto& dir = scratch.Path();
    std::filesystem::create_directories(dir);
    const auto file = dir / "original-station.solution";
    io::BinaryWriter::WriteSolutionFile(file, candidate);
    if (nativeGate) {
        const auto report = io::VerifySolutionFiles(puzzle, file, 150000, 10000);
        std::ofstream(dir / "original-station.txt")
            << "passed=" << report.Passed() << " status=" << static_cast<int>(report.status)
            << " cost=" << report.metrics.cost << " cycles=" << report.metrics.cycles
            << " area=" << report.metrics.area << " compile_us=" << us
            << " error_cycle=" << report.errorCycle << " error_cell=" << report.errorU << ','
            << report.errorV << ' ' << report.detail << '\n';
        EXPECT_TRUE(report.Passed()) << report.detail;
        EXPECT_EQ(report.metrics.cost, 140);
        EXPECT_EQ(report.metrics.cycles, 6);
        EXPECT_EQ(report.metrics.area, 12);
        EXPECT_EQ(report.metrics.instructionCount, 0);
    }
    const auto replay = verification::AuditOrdinaryMachine(p, candidate, 96);
    ASSERT_TRUE(replay.Passed()) << replay.detail;
    EXPECT_EQ(replay.outputs, (std::vector<std::size_t>{6, 6}));
    EXPECT_EQ(replay.disposed, 0U);
    EXPECT_EQ(replay.remaining, 2U); // regenerated reaction inputs at completion
    EXPECT_FALSE(replay.traceTruncated);
    std::ofstream events(dir / "consumer-events.csv");
    events << "cycle,phase,kind,port,atom_ids\n";
    std::ofstream ledger(dir / "consumer-ledger.csv");
    ledger << "consumer,batch,atom,role,ready_cycle,held,output_cycle,discarded,bond_neighbors\n";
    int batches[2]{};
    for (const auto& event : replay.events) {
        events << event.cycle << ',' << static_cast<int>(event.phase) << ','
               << static_cast<int>(event.kind) << ',' << event.port;
        for (auto id : event.atoms)
            events << ',' << id;
        events << '\n';
        EXPECT_NE(event.kind, verification::StaticProductionEvent::Kind::Grab);
        EXPECT_NE(event.kind, verification::StaticProductionEvent::Kind::Dispose);
        if (event.kind != verification::StaticProductionEvent::Kind::Output) continue;
        ASSERT_EQ(event.atoms.size(), 4U);
        ASSERT_LT(event.port, 2U);
        const auto batch = ++batches[event.port];
        ASSERT_EQ(event.outputState.size(), 4U);
        std::size_t edges = 0;
        for (const auto& atom : event.outputState) {
            EXPECT_FALSE(atom.held);
            ledger << event.port << ',' << batch << ',' << atom.id << ',' << atom.type << ','
                   << event.cycle << ',' << atom.held << ',' << event.cycle << ",0,";
            for (const auto& [neighbor, bits] : atom.bonds) {
                EXPECT_EQ(bits, 1);
                ledger << neighbor << ':' << static_cast<int>(bits) << ';';
                ++edges;
            }
            ledger << '\n';
        }
        EXPECT_EQ(edges, 6U);
    }
    EXPECT_EQ(batches[0], 6);
    EXPECT_EQ(batches[1], 6);
}
io::PuzzleData OrdinaryPair() {
    return io::BinaryParser::ParsePuzzleFile(root /
                                             "asset/puzzle/workshop-versions/w2946693353.puzzle");
}
void RunOrdinaryPairMatrix(bool nativeGate) {
    for (int variant = 0; variant < 2; ++variant) {
        auto p = OrdinaryPair();
        p.name = "Direct-feed ordinary consumer graph";
        if (variant) {
            std::reverse(p.outputs.begin(), p.outputs.end());
            std::reverse(p.inputs.begin(), p.inputs.end());
            p.inputs.push_back({{{core::AtomType::Gold, {4, -3}}}, {}});
            for (auto& m : p.outputs) {
                for (auto& a : m.atoms)
                    a.position = core::Add(core::RotateClockwise(a.position), {7, -4});
                for (auto& b : m.bonds) {
                    b.from = core::Add(core::RotateClockwise(b.from), {7, -4});
                    b.to = core::Add(core::RotateClockwise(b.to), {7, -4});
                    std::swap(b.from, b.to);
                }
                std::reverse(m.atoms.begin(), m.atoms.end());
                std::reverse(m.bonds.begin(), m.bonds.end());
            }
            for (auto& input : p.inputs)
                input.atoms[0].position = {3, -2};
        }
        p.name = "Static singleton metal purification";
        const auto compiled = CompileStaticReactionWithFamily(p, std::chrono::steady_clock::now() +
                                                                     std::chrono::seconds(1));
        EXPECT_EQ(compiled.family, StaticReactionFamily::OrdinaryPair);
        const auto& station = compiled.solution;
        EXPECT_EQ(io::BinaryWriter::WriteSolution(station),
                  io::BinaryWriter::WriteSolution(CompileStaticReaction(p)));
        EXPECT_EQ(io::BinaryWriter::WriteSolution(station),
                  io::BinaryWriter::WriteSolution(CompileDemandDrivenConsumers(p)));
        EXPECT_EQ(station.solutionName, "Bounded ordinary Animismus consumer embedding");
        EXPECT_TRUE(std::none_of(station.objects.begin(), station.objects.end(), [](const auto& o) {
            return o.typeName == "arm1" || o.typeName == "glyph-disposal" ||
                   !o.instructions.empty();
        }));
        Gate(p, "ordinary-pair-" + std::to_string(variant), nativeGate, true);
    }
}
TEST(DemandDrivenConsumersTest, OrdinaryPairMatrixSemantics) {
    RunOrdinaryPairMatrix(false);
}
TEST(DemandDrivenConsumersTest, OrdinaryPairBoundariesAndDeadline) {
    const auto p = OrdinaryPair();
    for (int bit : {0, 8, 16, 25}) {
        auto bad = p;
        bad.permissionBits &= ~(1ULL << bit);
        EXPECT_THROW((void)CompileStaticReaction(bad), std::invalid_argument);
    }
    for (int mutation = 0; mutation < 11; ++mutation) {
        SCOPED_TRACE(mutation);
        auto bad = p;
        if (mutation == 0)
            std::erase_if(bad.inputs,
                          [](const auto& m) { return m.atoms[0].type == core::AtomType::Salt; });
        if (mutation == 1) bad.inputs[0].atoms.push_back(bad.inputs[0].atoms[0]);
        if (mutation == 2) bad.outputs[0].bonds[0].bits = 2;
        if (mutation == 3) bad.outputs[0].bonds.push_back(bad.outputs[0].bonds[0]);
        if (mutation == 4) bad.outputs[0].atoms.push_back(bad.outputs[0].atoms[0]);
        if (mutation == 5) bad.outputs[0].bonds.pop_back();
        if (mutation == 6) bad.productionInfo.emplace();
        if (mutation == 7) bad.outputMultiplier = 2;
        if (mutation == 8) bad.inputs.resize(9, bad.inputs[0]);
        if (mutation == 9) bad.outputs[0].atoms[0].type = core::AtomType::Fire;
        if (mutation == 10) {
            auto& m = bad.outputs[0];
            m.atoms = {{core::AtomType::Vitae, {0, 0}}};
            m.bonds.clear();
            for (const auto cell : core::kAxialDirections) {
                m.atoms.push_back({core::AtomType::Gold, cell});
                m.bonds.push_back({1, {0, 0}, cell});
            }
        }
        EXPECT_THROW((void)CompileStaticReaction(bad), std::invalid_argument);
    }
    const auto expired = std::chrono::steady_clock::now() - std::chrono::seconds(1);
    EXPECT_THROW((void)CompileDemandDrivenConsumers(p, expired), std::invalid_argument);
    EXPECT_THROW((void)CompileStaticReaction(p, expired), std::invalid_argument);
    EXPECT_THROW((void)CompileStaticReactionWithFamily(p, expired), std::invalid_argument);
    const auto legacy =
        io::BinaryParser::ParsePuzzleFile(root / "asset/puzzle/campaign/ch4/P032.puzzle");
    EXPECT_THROW((void)CompileStaticReaction(legacy, expired), std::invalid_argument);
    EXPECT_EQ(io::BinaryWriter::WriteSolution(CompileStaticReaction(legacy)),
              io::BinaryWriter::WriteSolution(
                  CompileStaticReaction(legacy, std::chrono::steady_clock::time_point::max())));
}
#ifdef HAS_LIBVERIFY
TEST(DemandDrivenConsumersTest, OrdinaryPairMatrixNative) {
    RunOrdinaryPairMatrix(true);
}
TEST(DemandDrivenConsumersTest, OriginalOrdinaryPairNativeArchiveAndCaps) {
    const auto path = root / "asset/puzzle/workshop-versions/w2946693353.puzzle";
    const auto p = io::BinaryParser::ParsePuzzleFile(path);
    std::vector<std::byte> firstBytes;
    for (const std::size_t cap : {1U, 3U}) {
        const test::ScopedTestDirectory scratch;
        io::VerificationOptions options;
        io::CandidateArchive archive(path, scratch.Path() / "original.solution", options);
        AssemblySolver solver;
        solver.SetDeadline(std::chrono::steady_clock::now() + std::chrono::seconds(10));
        std::size_t calls = 0, accepted = 0, pairs = 0;
        const auto first = solver.CollectVerified(
            p,
            [&](const auto& station, const std::string& stage) {
                ++calls;
                const auto report = archive.Submit(station, stage);
                if (report.Passed()) {
                    ++accepted;
                    EXPECT_EQ(report.authority, io::VerificationAuthority::LegacyGate);
                }
                if (stage == "portfolio-static-reaction-ordinary-pair") {
                    ++pairs;
                    EXPECT_TRUE(report.Passed()) << report.detail;
                    EXPECT_EQ(report.metrics.cost, 100);
                    EXPECT_EQ(report.metrics.cycles, 6);
                    EXPECT_EQ(report.metrics.area, 12);
                    EXPECT_EQ(report.metrics.instructionCount, 0);
                }
                return report;
            },
            cap);
        ASSERT_TRUE(archive.First());
        const auto bytes = io::BinaryWriter::WriteSolution(first);
        if (firstBytes.empty()) firstBytes = bytes;
        EXPECT_EQ(bytes, firstBytes);
        EXPECT_LE(accepted, cap);
        EXPECT_LE(calls, AssemblySolver::MaxCandidateSubmissions);
        EXPECT_EQ(pairs, cap == 1 ? 0U : 1U);
        ASSERT_TRUE(archive.Snapshot(options).Passed());
        if (cap > 1)
            for (const auto& slot : archive.Slots()) {
                ASSERT_TRUE(slot);
                EXPECT_EQ(slot->strategy, "portfolio-static-reaction-ordinary-pair");
            }
    }
}
#endif
TEST(DemandDrivenConsumersTest, OriginalPrototypeFullGateSemantics) {
    RunOriginalPrototypeFullGate(false);
}
#ifdef HAS_LIBVERIFY
TEST(DemandDrivenConsumersTest, OriginalPrototypeFullGate) {
    RunOriginalPrototypeFullGate(true);
}
#endif
void RunStructuralFamilyAndPermissions(bool nativeGate) {
    auto p = io::BinaryParser::ParsePuzzleFile(root / "asset/puzzle/campaign/ch4/P032.puzzle");
    EXPECT_NO_THROW((void)CompileDemandDrivenConsumers(p));
    p.name = "unrelated title";
    std::reverse(p.outputs.begin(), p.outputs.end());
    for (auto& m : p.outputs) {
        for (auto& a : m.atoms)
            a.position = core::Add(core::RotateClockwise(a.position), {7, -4});
        for (auto& b : m.bonds) {
            b.from = core::Add(core::RotateClockwise(b.from), {7, -4});
            b.to = core::Add(core::RotateClockwise(b.to), {7, -4});
            std::swap(b.from, b.to);
        }
        std::reverse(m.atoms.begin(), m.atoms.end());
        std::reverse(m.bonds.begin(), m.bonds.end());
    }
    EXPECT_NO_THROW((void)CompileDemandDrivenConsumers(p));
    Gate(p, "family-rotated-reordered", nativeGate, true);
    for (int rotation = 1; rotation < 6; ++rotation) {
        for (auto& m : p.outputs) {
            for (auto& a : m.atoms)
                a.position = core::RotateClockwise(a.position);
            for (auto& b : m.bonds) {
                b.from = core::RotateClockwise(b.from);
                b.to = core::RotateClockwise(b.to);
            }
        }
        p.inputs[0].atoms[0].position = {3, -2};
        p.inputs[1].atoms[0].position = {-4, 3};
        Gate(p, "family-turn-" + std::to_string(rotation), nativeGate, true);
    }
    for (const int bit : {0, 8, 12, 16}) {
        auto denied = p;
        denied.permissionBits &= ~(1ULL << bit);
        EXPECT_THROW((void)CompileDemandDrivenConsumers(denied), std::invalid_argument);
    }
    auto mutated = p;
    mutated.productionInfo.emplace();
    EXPECT_THROW((void)CompileDemandDrivenConsumers(mutated), std::invalid_argument);
    mutated = p;
    mutated.outputMultiplier = 2;
    EXPECT_THROW((void)CompileDemandDrivenConsumers(mutated), std::invalid_argument);
    mutated = p;
    mutated.version = 2;
    EXPECT_THROW((void)CompileDemandDrivenConsumers(mutated), std::invalid_argument);
    mutated = p;
    mutated.outputs[0].bonds[0].bits = 2;
    EXPECT_THROW((void)CompileDemandDrivenConsumers(mutated), std::invalid_argument);
    mutated = p;
    mutated.outputs[0].atoms[0].type = core::AtomType::Fire;
    EXPECT_THROW((void)CompileDemandDrivenConsumers(mutated), std::invalid_argument);
    mutated = p;
    mutated.outputs[0].bonds.push_back(mutated.outputs[0].bonds[0]);
    EXPECT_THROW((void)CompileDemandDrivenConsumers(mutated), std::invalid_argument);
    mutated = p;
    for (auto& a : mutated.outputs[0].atoms)
        std::swap(a.position.q, a.position.r);
    for (auto& b : mutated.outputs[0].bonds) {
        std::swap(b.from.q, b.from.r);
        std::swap(b.to.q, b.to.r);
    }
    EXPECT_THROW((void)CompileDemandDrivenConsumers(mutated), std::invalid_argument);
    mutated = p;
    mutated.outputs[0].atoms[0].position = core::Add(mutated.outputs[0].atoms[0].position, {9, 9});
    EXPECT_THROW((void)CompileDemandDrivenConsumers(mutated), std::invalid_argument);
}
TEST(DemandDrivenConsumersTest, StructuralFamilyAndPermissionsSemantics) {
    RunStructuralFamilyAndPermissions(false);
}
#ifdef HAS_LIBVERIFY
TEST(DemandDrivenConsumersTest, StructuralFamilyAndPermissions) {
    RunStructuralFamilyAndPermissions(true);
}
#endif
}} // namespace opusoptima::solvers

namespace opusoptima::solvers { namespace {
void RunDistinctPathGraphAndFeedLayouts(bool nativeGate) {
    auto p = io::BinaryParser::ParsePuzzleFile(root / "asset/puzzle/campaign/ch4/P032.puzzle");
    p.name = "pre-registered static path family";
    int trimmed = 0;
    for (auto& m : p.outputs) {
        auto salt = std::find_if(m.atoms.begin(), m.atoms.end(),
                                 [](const auto& a) { return a.type == core::AtomType::Salt; });
        ASSERT_NE(salt, m.atoms.end());
        const auto cell = salt->position;
        m.atoms.erase(salt);
        std::erase_if(m.bonds, [&](const auto& b) { return b.from == cell || b.to == cell; });
        if (++trimmed == 1) Gate(p, "path-mixed-four-three", nativeGate, true);
    }
    p.inputs.resize(1);
    Gate(p, "path-single-source", nativeGate, true);
    p.inputs.push_back({{{core::AtomType::Salt, {4, -3}}}, {}});
    p.permissionBits &= ~(1ULL << 12);
    Gate(p, "path-direct-salt", nativeGate, true);
    auto bad = p;
    bad.inputs[0].atoms[0].type = core::AtomType::Salt;
    EXPECT_THROW((void)CompileDemandDrivenConsumers(bad), std::invalid_argument);
    bad = p;
    bad.inputs[0].atoms.push_back(bad.inputs[0].atoms[0]);
    EXPECT_THROW((void)CompileDemandDrivenConsumers(bad), std::invalid_argument);
    bad = p;
    bad.outputs[0].bonds[0].bits = 2;
    EXPECT_THROW((void)CompileDemandDrivenConsumers(bad), std::invalid_argument);
    for (int bit : {0, 8, 16}) {
        bad = p;
        bad.permissionBits &= ~(1ULL << bit);
        EXPECT_THROW((void)CompileDemandDrivenConsumers(bad), std::invalid_argument);
    }
    // Unbonding is not used by this station.
    p.permissionBits &= ~(1ULL << 9);
    EXPECT_NO_THROW((void)CompileDemandDrivenConsumers(p));
    bad = p;
    bad.outputMultiplier = 2;
    EXPECT_THROW((void)CompileDemandDrivenConsumers(bad), std::invalid_argument);
    bad = p;
    bad.version = 2;
    EXPECT_THROW((void)CompileDemandDrivenConsumers(bad), std::invalid_argument);
    bad = p;
    bad.productionInfo.emplace();
    EXPECT_THROW((void)CompileDemandDrivenConsumers(bad), std::invalid_argument);
}
TEST(DemandDrivenConsumersTest, DistinctPathGraphAndFeedLayoutsSemantics) {
    RunDistinctPathGraphAndFeedLayouts(false);
}
#ifdef HAS_LIBVERIFY
TEST(DemandDrivenConsumersTest, DistinctPathGraphAndFeedLayouts) {
    RunDistinctPathGraphAndFeedLayouts(true);
}
#endif

TEST(StaticEmbeddingTest, RealTopologiesAndMetamorphisms) {
    for (const auto* file : {"campaign/ch1-and-prologue/P013.puzzle", "campaign/ch2/P021.puzzle",
                             "campaign/ch2/P019.puzzle"}) {
        auto p = io::BinaryParser::ParsePuzzleFile(root / "asset/puzzle" / file);
        for (int rotation = 0; rotation < 6; ++rotation) {
            auto q = p;
            auto transform = [&](core::HexCoord c) {
                for (int i = 0; i < rotation; ++i)
                    c = core::RotateClockwise(c);
                return core::Add(c, {2, -3});
            };
            std::reverse(q.inputs.begin(), q.inputs.end());
            for (auto& input : q.inputs)
                input.atoms[0].position = transform(input.atoms[0].position);
            for (auto& m : q.outputs) {
                for (auto& a : m.atoms)
                    a.position = transform(a.position);
                for (auto& b : m.bonds) {
                    b.from = transform(b.from);
                    b.to = transform(b.to);
                }
                std::reverse(m.atoms.begin(), m.atoms.end());
                std::reverse(m.bonds.begin(), m.bonds.end());
            }
            const auto candidate = CompileStaticEmbedding(q);
            EXPECT_FALSE(candidate.objects.empty());
#ifdef HAS_LIBVERIFY
            Gate(q,
                 "embedding-" + std::filesystem::path(file).stem().string() + "-r" +
                     std::to_string(rotation),
                 true);
#endif
        }
    }
}
TEST(StaticEmbeddingTest, AllOutputsRemainRequired) {
    auto p = io::BinaryParser::ParsePuzzleFile(
        root / "asset/puzzle/campaign/ch1-and-prologue/P013.puzzle");
    p.outputs.push_back(p.outputs[0]);
    EXPECT_NO_THROW((void)CompileStaticEmbedding(p));
#ifdef HAS_LIBVERIFY
    Gate(p, "embedding-two-outputs", true);
#endif
    p.outputs[1].atoms[0].type = core::AtomType::Gold;
    EXPECT_THROW((void)CompileStaticEmbedding(p), std::invalid_argument);
    const auto path = root / "asset/puzzle/tourney-2021/OM2021_W6.puzzle";
    const auto multi = io::BinaryParser::ParsePuzzleFile(path);
    ASSERT_EQ(multi.outputs.size(), 2U);
    io::VerificationReport accepted;
    accepted.status = io::VerificationStatus::Passed;
    accepted.authority = io::VerificationAuthority::LegacyGate;
    EXPECT_FALSE(StaticEmbeddingSuppressionAfterReaction(multi, StaticReactionFamily::Purification,
                                                         accepted));
    const auto machine = CompileStaticEmbedding(multi);
    RecordProperty("multioutput_embedding_sha256",
                   io::CandidateSha256(io::BinaryWriter::WriteSolution(machine)));
#ifdef HAS_LIBVERIFY
    const test::ScopedTestDirectory scratch;
    const auto file = scratch.Path() / "multioutput.solution";
    io::BinaryWriter::WriteSolutionFile(file, machine);
    const auto report = io::VerifySolutionFiles(path, file, 150000, 10000);
    ASSERT_TRUE(report.Passed()) << report.detail;
    EXPECT_EQ(report.authority, io::VerificationAuthority::LegacyGate);
    EXPECT_EQ(report.metrics.cost, 420);
    EXPECT_EQ(report.metrics.cycles, 21);
    EXPECT_EQ(report.metrics.area, 37);
    EXPECT_EQ(report.metrics.instructionCount, 0);
    const auto optimized = LocalTrial(machine, 2);
    io::BinaryWriter::WriteSolutionFile(file, optimized);
    const auto optimizedReport = io::VerifySolutionFiles(path, file, 150000, 10000);
    ASSERT_TRUE(optimizedReport.Passed()) << optimizedReport.detail;
    EXPECT_EQ(optimizedReport.authority, io::VerificationAuthority::LegacyGate);
    EXPECT_EQ(optimizedReport.metrics.cost, 400);
    EXPECT_EQ(optimizedReport.metrics.cycles, 21);
    EXPECT_EQ(optimizedReport.metrics.area, 35);
    RecordProperty("multioutput_embedding_cost", report.metrics.cost);
    RecordProperty("multioutput_embedding_cycles", report.metrics.cycles);
    RecordProperty("multioutput_embedding_area", report.metrics.area);
    RecordProperty("multioutput_optimized_cost", optimizedReport.metrics.cost);
    RecordProperty("multioutput_optimized_area", optimizedReport.metrics.area);
#endif
}
TEST(StaticEmbeddingTest, BoundedUnsupportedAndPermissions) {
    auto p = io::BinaryParser::ParsePuzzleFile(root / "asset/puzzle/campaign/ch2/P021.puzzle");
    auto reject = [](const auto& q) {
        EXPECT_THROW((void)CompileStaticEmbedding(q), std::invalid_argument);
    };
    for (int bit : {0, 8, 12, 25}) {
        auto q = p;
        q.permissionBits &= ~(1ULL << bit);
        reject(q);
    }
    auto q = p;
    q.outputs[0].bonds[0].bits = 2;
    reject(q);
    q = p;
    q.outputs[0].bonds.push_back(q.outputs[0].bonds[0]);
    reject(q);
    q = p;
    q.outputs[0].bonds.pop_back();
    reject(q);
    q = p;
    q.outputs[0].bonds[0].to = {20, 20};
    reject(q);
    q = p;
    q.outputs[0].atoms[1].position = q.outputs[0].atoms[0].position;
    reject(q);
    q = p;
    q.inputs.clear();
    reject(q);
    q = p;
    q.inputs.erase(q.inputs.begin() + 1);
    q.permissionBits &= ~(1ULL << 28);
    reject(q);
    q = p;
    for (auto& a : q.outputs[0].atoms)
        if (a.type == core::AtomType::Salt) a.type = core::AtomType::Water;
    EXPECT_NO_THROW((void)CompileStaticEmbedding(q));
#ifdef HAS_LIBVERIFY
    Gate(q, "embedding-direct-water", true);
#endif
    q = p;
    q.outputs[0].atoms[0].position = {30, 30};
    reject(q);
    q = p;
    q.outputs[0].atoms[0].type = core::AtomType::Gold;
    reject(q);
    q = p;
    q.inputs[0].atoms.push_back(q.inputs[0].atoms[0]);
    reject(q);
    q = p;
    q.outputMultiplier = 2;
    reject(q);
    q = p;
    q.productionInfo.emplace();
    reject(q);
    q = p;
    q.version = 2;
    reject(q);
    q = p;
    q.outputs.clear();
    reject(q);
    q = p;
    q.outputs.resize(3, p.outputs[0]);
    reject(q);
    q = p;
    q.outputs[0].atoms.resize(17);
    reject(q);
    q = p;
    q.outputs[0].bonds.resize(37);
    reject(q);
    q = p;
    q.inputs.resize(9, p.inputs[0]);
    reject(q);
    EXPECT_THROW((void)CompileStaticEmbedding(p, std::chrono::steady_clock::now()),
                 std::invalid_argument);
    const auto old =
        io::BinaryParser::ParsePuzzleFile(root / "asset/puzzle/campaign/ch4/P032.puzzle");
    EXPECT_NO_THROW((void)CompileDemandDrivenConsumers(old));
    reject(old);
}
}} // namespace opusoptima::solvers
