#include "ClosedMetalInventory.hpp"
#include "AssemblySolver.hpp"
#include "IO/BinaryParser.hpp"
#include "IO/CandidateArchive.hpp"
#include "RepoRoot.hpp"
#include "TestOutputDirectory.hpp"
#include <algorithm>
#include <gtest/gtest.h>

namespace opusoptima::solvers { namespace {
std::filesystem::path OriginalMetalInput() {
    return opusoptima::test::RepoRoot(__FILE__) / "tests/fixtures/closed-wide-metal.puzzle";
}

TEST(ClosedWideMetalInventory, StructuralDomainIsPoseIndependentAndFailClosed) {
    const auto original = io::BinaryParser::ParsePuzzleFile(OriginalMetalInput());
    ASSERT_TRUE(SupportsClosedWideMetalInventory(original));
    for (int bit : {0, 8, 9, 15, 22, 23, 25}) {
        auto denied = original;
        denied.permissionBits &= ~(1ULL << bit);
        EXPECT_FALSE(SupportsClosedWideMetalInventory(denied)) << bit;
    }
    for (int rotation = 0; rotation < 6; ++rotation) {
        auto puzzle = original;
        auto transform = [&](core::HexCoord cell) {
            for (int i = 0; i < rotation; ++i) {
                const int old = cell.q;
                cell.q = -cell.r;
                cell.r += old;
            }
            cell.q += 5;
            cell.r -= 3;
            return cell;
        };
        for (auto& atom : puzzle.inputs[0].atoms)
            atom.position = transform(atom.position);
        for (auto& bond : puzzle.inputs[0].bonds) {
            bond.from = transform(bond.from);
            bond.to = transform(bond.to);
        }
        std::reverse(puzzle.inputs[0].atoms.begin(), puzzle.inputs[0].atoms.end());
        EXPECT_TRUE(SupportsClosedWideMetalInventory(puzzle));
    }
    auto puzzle = original;
    puzzle.permissionBits |= 1ULL << 17;
    EXPECT_FALSE(SupportsClosedWideMetalInventory(puzzle));
    puzzle = original;
    puzzle.inputs[0].atoms[0].type = core::AtomType::Air;
    EXPECT_FALSE(SupportsClosedWideMetalInventory(puzzle));
    puzzle = original;
    puzzle.outputs.push_back(puzzle.outputs[0]);
    EXPECT_FALSE(SupportsClosedWideMetalInventory(puzzle));
    puzzle = original;
    puzzle.outputs[0].atoms[0].type = core::AtomType::Lead;
    EXPECT_FALSE(SupportsClosedWideMetalInventory(puzzle));
    puzzle = original;
    puzzle.outputs[0].atoms[0].type = core::AtomType::Tin;
    EXPECT_TRUE(SupportsClosedWideMetalInventory(puzzle));
    bool called = false;
    EXPECT_FALSE(CompileAcceptedClosedWideMetalInventory(
        original,
        [&](const auto&) {
            called = true;
            return true;
        },
        std::chrono::steady_clock::now()));
    EXPECT_FALSE(called);
}

#ifdef HAS_LIBVERIFY
TEST(ClosedWideMetalInventory, NativeOriginalAdmissionRetainsFirstAtAcceptedCapOne) {
    const auto input = OriginalMetalInput();
    const auto puzzle = io::BinaryParser::ParsePuzzleFile(input);
    std::string firstHash;
    for (std::size_t cap : {1U, 3U}) {
        test::ScopedTestDirectory scratch;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
        io::VerificationOptions options;
        options.mode = io::VerificationMode::LegacyGate;
        options.outerDeadline = deadline;
        io::CandidateArchive archive(input, scratch.Path() / "output.solution", options);
        AssemblySolver solver;
        solver.SetDeadline(deadline);
        std::size_t accepted = 0, submissions = 0;
        bool metalAccepted = false;
        const auto result = solver.CollectVerified(
            puzzle,
            [&](const auto& candidate, const std::string& stage) {
                ++submissions;
                const auto report = archive.Submit(candidate, stage, solver.Deadline());
                accepted += report.Passed();
                if (stage == "closed-wide-metal-inventory" && report.Passed()) metalAccepted = true;
                return report;
            },
            cap);
        ASSERT_TRUE(metalAccepted);
        ASSERT_TRUE(archive.First());
        EXPECT_LE(accepted, cap);
        EXPECT_LE(submissions, AssemblySolver::MaxCandidateSubmissions);
        EXPECT_EQ(archive.First()->verification.authority, io::VerificationAuthority::LegacyGate);
        EXPECT_GT(archive.First()->verification.metrics.cycles, 0);
        if (cap == 1) firstHash = archive.First()->solutionHash;
        EXPECT_EQ(archive.First()->solutionHash, firstHash);
        EXPECT_EQ(result.metrics.cycles, archive.First()->verification.metrics.cycles);
    }
}
#endif
}} // namespace opusoptima::solvers
