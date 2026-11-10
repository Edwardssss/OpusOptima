#include "ChemistryPipeline.hpp"
#include "BinaryWriter.hpp"
#include "LifeDeath.hpp"
#include "SolutionVerifier.hpp"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <stdexcept>

namespace opusoptima::solvers { namespace {
using core::AtomType;
io::PuzzleData Feeds(std::initializer_list<AtomType> types) {
    io::PuzzleData puzzle;
    puzzle.permissionBits = ~0ULL;
    for (auto type : types)
        puzzle.inputs.push_back({{{type, {0, 0}}}, {}});
    return puzzle;
}
std::size_t Reactions(const ChemistryPlan& plan, ChemistryReaction reaction) {
    return std::count_if(plan.operations.begin(), plan.operations.end(),
                         [&](const auto& op) { return op.input < 0 && op.reaction == reaction; });
}
TEST(ChemistryPipelineTest, AnimismusRetainsAndReusesComplementaryStock) {
    const auto p = Feeds({AtomType::Air});
    const auto balanced = ChemistryPipeline::Plan(p, {{AtomType::Vitae, 1}, {AtomType::Mors, 1}});
    EXPECT_EQ(balanced.inputBatches, (std::vector<std::size_t>{2}));
    EXPECT_EQ(Reactions(balanced, ChemistryReaction::Calcify), 2);
    EXPECT_EQ(Reactions(balanced, ChemistryReaction::Animismus), 1);
    EXPECT_EQ(std::count(balanced.stock.begin(), balanced.stock.end(), 0), 16);
    const auto asymmetric = ChemistryPipeline::Plan(p, {{AtomType::Vitae, 3}, {AtomType::Mors, 1}});
    EXPECT_EQ(asymmetric.inputBatches[0], 6);
    EXPECT_EQ(asymmetric.stock[static_cast<int>(AtomType::Mors) - 1], 2);
}
TEST(ChemistryPipelineTest, DispersionUsesOneQuintessenceForFourDemands) {
    const auto plan = ChemistryPipeline::Plan(
        Feeds({AtomType::Quintessence}),
        {{AtomType::Air, 1}, {AtomType::Earth, 1}, {AtomType::Fire, 1}, {AtomType::Water, 1}});
    EXPECT_EQ(plan.inputBatches[0], 1);
    EXPECT_EQ(Reactions(plan, ChemistryReaction::Disperse), 1);
    EXPECT_EQ(std::count(plan.stock.begin(), plan.stock.end(), 0), 16);
    for (auto feed : plan.feeds)
        EXPECT_EQ(feed.kind, ChemistryFeedKind::Disperse);
}
TEST(ChemistryPipelineTest, DispersionCalcifiesSaltAndRetainsOtherElements) {
    auto puzzle = Feeds({AtomType::Quintessence});
    const auto plan = ChemistryPipeline::Plan(
        puzzle,
        {{AtomType::Salt, 2}, {AtomType::Earth, 2}, {AtomType::Fire, 2}, {AtomType::Water, 2}});
    EXPECT_EQ(plan.inputBatches[0], 2);
    EXPECT_EQ(Reactions(plan, ChemistryReaction::Disperse), 2);
    EXPECT_EQ(Reactions(plan, ChemistryReaction::Calcify), 2);
    EXPECT_EQ(std::count(plan.stock.begin(), plan.stock.end(), 0), 16);
    EXPECT_TRUE(plan.feeds.front().calcifySource);
    for (int bit : {12, 18}) {
        auto missing = puzzle;
        missing.permissionBits &= ~(1ULL << bit);
        EXPECT_THROW((void)ChemistryPipeline::Plan(missing, {{AtomType::Salt, 1}}),
                     std::invalid_argument);
    }
}
TEST(ChemistryPipelineTest, WheelAndUnificationRespectDemandAndPermissions) {
    auto p = Feeds({AtomType::Salt});
    const auto plan = ChemistryPipeline::Plan(p, {{AtomType::Quintessence, 2}, {AtomType::Air, 1}});
    EXPECT_EQ(plan.inputBatches[0], 9);
    EXPECT_EQ(Reactions(plan, ChemistryReaction::Duplicate), 9);
    EXPECT_EQ(Reactions(plan, ChemistryReaction::Unify), 2);
    for (int bit : {13, 18, 28}) {
        auto missing = p;
        missing.permissionBits &= ~(1ULL << bit);
        EXPECT_THROW((void)ChemistryPipeline::Plan(missing, {{AtomType::Quintessence, 1}}),
                     std::invalid_argument);
    }
}
TEST(ChemistryPipelineTest, DirectFeedsWinAndUnsupportedPhysicalChainsReject) {
    const auto p = Feeds({AtomType::Salt, AtomType::Air});
    const auto plan = ChemistryPipeline::Plan(p, {{AtomType::Air, 2}});
    EXPECT_EQ(plan.feeds[0].kind, ChemistryFeedKind::Direct);
    EXPECT_EQ(plan.inputBatches, (std::vector<std::size_t>{0, 2}));
    EXPECT_THROW((void)ChemistryPipeline::Plan(Feeds({AtomType::Air}), {{AtomType::Water, 1}}),
                 std::invalid_argument);
    EXPECT_THROW((void)ChemistryPipeline::Plan(Feeds({AtomType::Lead}), {{AtomType::Gold, 1}}),
                 std::invalid_argument);
    EXPECT_THROW((void)ChemistryPipeline::Plan(p, {{AtomType::Repeat, 1}}), std::invalid_argument);
    auto missing = Feeds({AtomType::Air});
    missing.permissionBits &= ~(1ULL << 16);
    EXPECT_THROW((void)ChemistryPipeline::Plan(missing, {{AtomType::Vitae, 1}}),
                 std::invalid_argument);
}
TEST(ChemistryPipelineTest, DispersionBackendStoresCoproductsWithoutDisposal) {
    auto puzzle = Feeds({AtomType::Quintessence});
    puzzle.version = 3;
    puzzle.outputMultiplier = 1;
    puzzle.outputs.push_back({{{AtomType::Water, {0, 0}}}, {}});
    puzzle.permissionBits &= ~(1ULL << 17);
    EXPECT_EQ(ChemistryPipeline::Resolve(puzzle, AtomType::Water).kind,
              ChemistryFeedKind::Disperse);
    EXPECT_NO_THROW((void)CompileLifeDeath(puzzle));
}
#ifdef HAS_LIBVERIFY
TEST(ChemistryPipelineVerifierTest, DispersionAndCompositeAnimismusVerifyTwice) {
    const auto directory = std::filesystem::current_path();
    for (bool composite : {false, true}) {
        auto puzzle = composite ? Feeds({AtomType::Air, AtomType::Quintessence})
                                : Feeds({AtomType::Quintessence});
        if (!composite) puzzle.permissionBits &= ~(1ULL << 16);
        puzzle.version = 3;
        puzzle.name = "pipeline-composite-test";
        puzzle.outputMultiplier = 1;
        puzzle.outputs.push_back(
            {{{composite ? AtomType::Vitae : AtomType::Earth, {0, 0}}, {AtomType::Water, {1, 0}}},
             {{1, {0, 0}, {1, 0}}}});
        // Keep this synthetic integration fixture in the build tree, never coverage.
        const auto path = directory / "pipeline-composite-test.puzzle";
        const auto output = directory / "pipeline-composite-test.solution";
        struct Cleanup {
            std::filesystem::path puzzle, solution;
            ~Cleanup() {
                std::error_code ignored;
                std::filesystem::remove(puzzle, ignored);
                std::filesystem::remove(solution, ignored);
            }
        } cleanup{path, output};
        std::ofstream bytes(path, std::ios::binary);
        const auto integer = [&](std::uint64_t value, int width) {
            for (int i = 0; i < width; ++i)
                bytes.put(static_cast<char>(value >> (8 * i)));
        };
        integer(3, 4);
        integer(puzzle.name.size(), 1);
        bytes << puzzle.name;
        integer(0, 8);
        integer(puzzle.permissionBits, 8);
        const auto molecules = [&](const auto& list) {
            integer(list.size(), 4);
            for (const auto& molecule : list) {
                integer(molecule.atoms.size(), 4);
                for (const auto& atom : molecule.atoms) {
                    integer(static_cast<int>(atom.type), 1);
                    integer(atom.position.q, 1);
                    integer(atom.position.r, 1);
                }
                integer(molecule.bonds.size(), 4);
                for (const auto& bond : molecule.bonds) {
                    integer(bond.bits, 1);
                    integer(bond.from.q, 1);
                    integer(bond.from.r, 1);
                    integer(bond.to.q, 1);
                    integer(bond.to.r, 1);
                }
            }
        };
        molecules(puzzle.inputs);
        molecules(puzzle.outputs);
        integer(1, 4);
        integer(0, 1);
        bytes.close();
        auto solution = CompileLifeDeath(puzzle);
        for (int pass = 0; pass < 2; ++pass) {
            io::BinaryWriter::WriteSolutionFile(output, solution);
            const auto report = io::VerifySolutionFiles(path, output, 150000, 10000);
            ASSERT_TRUE(report.Passed()) << report.detail << " cycle=" << report.errorCycle
                                         << " at " << report.errorU << ',' << report.errorV;
            solution.metrics = report.metrics;
        }
    }
}
#endif
}} // namespace opusoptima::solvers
