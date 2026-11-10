#include "BinaryWriter.hpp"
#include "InventorySearch.hpp"
#include "SolutionVerifier.hpp"
#include "SupplyExecutionIR.hpp"

#include "RepoRoot.hpp"
#include "UnitTestConstants.hpp"
#include <algorithm>
#include <filesystem>
#include <gtest/gtest.h>

namespace {
using namespace opusoptima;
std::filesystem::path OreRoot() {
    return opusoptima::test::RepoRoot(__FILE__);
}
std::filesystem::path OrePuzzle() {
    return OreRoot() / "asset/puzzle/workshop-versions/w2591419339.puzzle";
}
} // namespace

TEST(ArtificialOre, OriginalRecipeAndPermissions) {
    const auto p = io::BinaryParser::ParsePuzzleFile(OrePuzzle());
    ASSERT_EQ(p.inputs.size(), 2);
    ASSERT_EQ(p.outputs.size(), 1);
    for (const auto& input : p.inputs) {
        ASSERT_EQ(input.atoms.size(), 2);
        ASSERT_EQ(input.bonds.size(), 1);
        EXPECT_EQ(input.bonds[0].bits, 1);
    }
    EXPECT_EQ(p.inputs[0].atoms[0].type, core::AtomType::Salt);
    EXPECT_EQ(p.inputs[0].atoms[1].type, core::AtomType::Earth);
    EXPECT_EQ(p.inputs[1].atoms[0].type, core::AtomType::Quicksilver);
    EXPECT_EQ(p.inputs[1].atoms[1].type, core::AtomType::Tin);
    std::vector<int> target;
    for (const auto& atom : p.outputs[0].atoms)
        target.push_back(static_cast<int>(atom.type));
    std::sort(target.begin(), target.end());
    EXPECT_EQ(target, (std::vector<int>{1, 1, 3, 9, 11}));
    ASSERT_EQ(p.outputs[0].bonds.size(), 4);
    ASSERT_TRUE(p.productionInfo);
    ASSERT_EQ(p.productionInfo->chambers.size(), 1);
    EXPECT_EQ(p.productionInfo->chambers[0].chamberType, "MediumWide");
    EXPECT_EQ(p.productionInfo->vials.size(), 3);
    EXPECT_EQ(p.permissionBits, 0x7c0570fULL);
    EXPECT_NE(p.permissionBits & (1ULL << 14), 0); // Projection.
    EXPECT_EQ(p.permissionBits & (1ULL << 15), 0); // Purification.
    EXPECT_EQ(p.permissionBits & (1ULL << 17), 0); // Disposal.
}

TEST(ArtificialOre, TwoProductMaterialPeriodDoesNotBondDifferentProductCopies) {
    using IR = solvers::ExecutionIR;
    for (int turns = 0; turns < 6; ++turns)
        for (bool reverse : {false, true}) {
            SCOPED_TRACE(turns);
            SCOPED_TRACE(reverse);
            auto p = io::BinaryParser::ParsePuzzleFile(OrePuzzle());
            if (reverse) {
                for (auto& input : p.inputs)
                    std::reverse(input.atoms.begin(), input.atoms.end());
                std::reverse(p.outputs[0].atoms.begin(), p.outputs[0].atoms.end());
            }
            solvers::InventorySearchLimits limits;
            limits.maxStates = 1000;
            limits.multiStagePurification = true;
            std::optional<IR> lowered;
            const auto searched =
                solvers::SearchJointSupplyInventory(p, 2, limits, nullptr, [&](const auto& plan) {
                    try {
                        lowered = solvers::BuildJointSupplyExecutionIR(p, plan, {{{8, -3}, turns}});
                        return true;
                    } catch (const IR::Error&) {
                        return false;
                    }
                });
            ASSERT_EQ(searched.status, solvers::InventorySearchStatus::Found);
            ASSERT_TRUE(lowered);
            IR::RequireComplete(lowered->Replay(p), {2});
            const auto count = [&](IR::Kind kind) {
                return std::count_if(lowered->steps.begin(), lowered->steps.end(),
                                     [&](const auto& step) { return step.operation.kind == kind; });
            };
            EXPECT_EQ(count(IR::Kind::Acquire), 7);
            EXPECT_EQ(count(IR::Kind::Project), 4);
            EXPECT_EQ(count(IR::Kind::Calcify), 1);
            EXPECT_EQ(count(IR::Kind::Discard), 0);
            EXPECT_EQ(searched.plan.usage[0].batches, 3);
            EXPECT_EQ(searched.plan.usage[1].batches, 4);
            EXPECT_EQ(p.productionInfo->vials.size(), 3);
        }
}

#ifdef HAS_LIBVERIFY
TEST(ArtificialOre, OriginalOracleRejectsForbiddenChemistryWithoutChangingPuzzle) {
    const auto p = io::BinaryParser::ParsePuzzleFile(OrePuzzle());
    const auto file = OreRoot() / ("build/ore-permissions-" +
                                   std::to_string(opusoptima::test::ProcessId()) + ".solution");
    struct Cleanup {
        std::filesystem::path file;
        ~Cleanup() {
            std::error_code ec;
            std::filesystem::remove(file, ec);
        }
    } cleanup{file};
    for (const auto* part : {"glyph-purification", "glyph-disposal"}) {
        SCOPED_TRACE(part);
        io::SolutionData candidate;
        candidate.puzzleName = p.name;
        candidate.solutionName = "permission rejection probe, not a solution";
        candidate.objects.push_back({part, {}, {}, {1, 0, 0, 0, 0}});
        io::BinaryWriter::WriteSolutionFile(file, candidate);
        const auto report = io::VerifySolutionFiles(OrePuzzle(), file, 200, 10000);
        EXPECT_FALSE(report.Passed());
        EXPECT_NE(report.detail.find("disabled in the puzzle file"), std::string::npos)
            << report.detail;
    }
}
#endif
