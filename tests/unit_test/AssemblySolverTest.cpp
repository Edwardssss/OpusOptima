#include "TestOutputDirectory.hpp"
#include <gtest/gtest.h>

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <numeric>
#include <optional>
#include <set>

#include "AssemblySolver.hpp"
#include "AutomaticSupply.hpp"
#include "BinaryWriter.hpp"
#include "CandidateArchive.hpp"
#include "ChemicalPlanner.hpp"
#include "PurificationAssembly.hpp"
#include "RepoRoot.hpp"
#include "RowAssembly.hpp"
#include "SolutionVerifier.hpp"

namespace {
struct ScopedFeedPrefetch {
    std::optional<std::string> prior;
    explicit ScopedFeedPrefetch(const char* value) {
        if (const auto* existing = std::getenv("OPUSOPTIMA_FEED_PREFETCH")) prior = existing;
        if (value)
            setenv("OPUSOPTIMA_FEED_PREFETCH", value, 1);
        else
            unsetenv("OPUSOPTIMA_FEED_PREFETCH");
    }
    ~ScopedFeedPrefetch() {
        if (prior)
            setenv("OPUSOPTIMA_FEED_PREFETCH", prior->c_str(), 1);
        else
            unsetenv("OPUSOPTIMA_FEED_PREFETCH");
    }
};
struct ScopedRowPipeline {
    std::optional<std::string> prior;
    explicit ScopedRowPipeline(const char* value) {
        if (const auto* existing = std::getenv("OPUSOPTIMA_ROW_PIPELINE")) prior = existing;
        if (value)
            setenv("OPUSOPTIMA_ROW_PIPELINE", value, 1);
        else
            unsetenv("OPUSOPTIMA_ROW_PIPELINE");
    }
    ~ScopedRowPipeline() {
        if (prior)
            setenv("OPUSOPTIMA_ROW_PIPELINE", prior->c_str(), 1);
        else
            unsetenv("OPUSOPTIMA_ROW_PIPELINE");
    }
};
struct ScopedRowAssemblyClocks {
    std::optional<std::string> prior;
    explicit ScopedRowAssemblyClocks(const char* value) {
        if (const auto* existing = std::getenv("OPUSOPTIMA_ROW_ASSEMBLY_CLOCKS")) prior = existing;
        if (value)
            setenv("OPUSOPTIMA_ROW_ASSEMBLY_CLOCKS", value, 1);
        else
            unsetenv("OPUSOPTIMA_ROW_ASSEMBLY_CLOCKS");
    }
    ~ScopedRowAssemblyClocks() {
        if (prior)
            setenv("OPUSOPTIMA_ROW_ASSEMBLY_CLOCKS", prior->c_str(), 1);
        else
            unsetenv("OPUSOPTIMA_ROW_ASSEMBLY_CLOCKS");
    }
};
// Sets all three row-clock knobs at once; `nullptr` selects the pipelined default.
struct ScopedRowClocks {
    ScopedFeedPrefetch feed;
    ScopedRowPipeline pipeline;
    ScopedRowAssemblyClocks assembly;
    explicit ScopedRowClocks(const char* value) : feed(value), pipeline(value), assembly(value) {}
};

using namespace opusoptima;
using solvers::AssemblyPlan;
using solvers::AssemblyPlanner;
using solvers::AssemblySolver;

struct ScopedEmbeddingOff {
    std::optional<std::string> prior;
    ScopedEmbeddingOff() {
        if (const auto* value = std::getenv("OPUSOPTIMA_STATIC_EMBEDDING")) prior = value;
        setenv("OPUSOPTIMA_STATIC_EMBEDDING", "off", 1);
    }
    ~ScopedEmbeddingOff() {
        if (prior)
            setenv("OPUSOPTIMA_STATIC_EMBEDDING", prior->c_str(), 1);
        else
            unsetenv("OPUSOPTIMA_STATIC_EMBEDDING");
    }
};

struct ScopedCompactLayout {
    std::optional<std::string> prior;
    explicit ScopedCompactLayout(const char* value) {
        if (const auto* existing = std::getenv("OPUSOPTIMA_COMPACT_LAYOUT")) prior = existing;
        if (value)
            setenv("OPUSOPTIMA_COMPACT_LAYOUT", value, 1);
        else
            unsetenv("OPUSOPTIMA_COMPACT_LAYOUT");
    }
    ~ScopedCompactLayout() {
        if (prior)
            setenv("OPUSOPTIMA_COMPACT_LAYOUT", prior->c_str(), 1);
        else
            unsetenv("OPUSOPTIMA_COMPACT_LAYOUT");
    }
};

struct ScopedRowClockPortfolioOff {
    std::optional<std::string> priorClock, priorGap;
    ScopedRowClockPortfolioOff() {
        if (const auto* existing = std::getenv("OPUSOPTIMA_ROW_CLOCK_PORTFOLIO"))
            priorClock = existing;
        if (const auto* existing = std::getenv("OPUSOPTIMA_ROW_GAP_PORTFOLIO")) priorGap = existing;
        setenv("OPUSOPTIMA_ROW_CLOCK_PORTFOLIO", "off", 1);
        setenv("OPUSOPTIMA_ROW_GAP_PORTFOLIO", "off", 1);
    }
    ~ScopedRowClockPortfolioOff() {
        if (priorClock)
            setenv("OPUSOPTIMA_ROW_CLOCK_PORTFOLIO", priorClock->c_str(), 1);
        else
            unsetenv("OPUSOPTIMA_ROW_CLOCK_PORTFOLIO");
        if (priorGap)
            setenv("OPUSOPTIMA_ROW_GAP_PORTFOLIO", priorGap->c_str(), 1);
        else
            unsetenv("OPUSOPTIMA_ROW_GAP_PORTFOLIO");
    }
};

// Generated fixtures, not game assets. Independent feed for each target atom.
io::PuzzleData Generated(const std::vector<core::HexCoord>& cells,
                         const std::vector<AssemblyPlan::Edge>& edges) {
    io::PuzzleData p;
    p.version = 3;
    p.name = "generated-assembly";
    p.permissionBits = AssemblyPlanner::RequiredPermissions;
    p.outputMultiplier = 1;
    p.outputs.emplace_back();
    for (std::size_t i = 0; i < cells.size(); ++i) {
        const auto type = static_cast<core::AtomType>(1 + i % 14);
        p.inputs.push_back({{{type, {0, 0}}}, {}});
        p.outputs[0].atoms.push_back({type, cells[i]});
    }
    for (auto e : edges)
        p.outputs[0].bonds.push_back({1, cells[e.a], cells[e.b]});
    return p;
}
io::PuzzleData Pair() {
    return Generated({{0, 0}, {1, 0}}, {{0, 1}});
}

struct ComponentEnvironment {
    std::optional<std::string> prior;
    ComponentEnvironment() {
        if (const auto* value = std::getenv("OPUSOPTIMA_ROW_COMPONENT")) prior = value;
    }
    ~ComponentEnvironment() {
        if (prior)
            setenv("OPUSOPTIMA_ROW_COMPONENT", prior->c_str(), 1);
        else
            unsetenv("OPUSOPTIMA_ROW_COMPONENT");
    }
};
TEST(AssemblyDiagnostics, ComponentRequiresExactOptInAndSharesQuota) {
    ComponentEnvironment restore;
    std::vector<std::vector<std::byte>> baseline;
    for (const char* mode : {static_cast<const char*>(nullptr), "off", "unknown", "on"}) {
        if (mode)
            setenv("OPUSOPTIMA_ROW_COMPONENT", mode, 1);
        else
            unsetenv("OPUSOPTIMA_ROW_COMPONENT");
        auto p = Pair();
        p.permissionBits = ((1ULL << 29) - 1) & ~(1ULL << 17);
        ASSERT_TRUE(solvers::SupportsComponentRowAssembly(p));
        std::vector<std::vector<std::byte>> submitted;
        AssemblySolver solver;
        const auto first = solver.CollectVerified(
            p,
            [&](const auto& candidate, const auto&) {
                auto canonical = candidate;
                canonical.solutionName.clear();
                canonical.metrics = {};
                submitted.push_back(io::BinaryWriter::WriteSolution(canonical));
                io::VerificationReport report;
                report.status = io::VerificationStatus::Passed;
                return report;
            },
            12);
        EXPECT_FALSE(first.objects.empty());
        EXPECT_LE(submitted.size(), 4U);
        ASSERT_GE(submitted.size(), 2U);
        if (!mode)
            baseline = submitted;
        else if (std::string(mode) != "on")
            EXPECT_EQ(submitted, baseline);
        else {
            EXPECT_EQ(submitted.front(), baseline.front());
            EXPECT_EQ(submitted[1], baseline[1]);
        }
    }
}

TEST(AssemblyDiagnostics, IneligibleCompactPreservesBaselineCandidatesAndQuota) {
    const char* value = std::getenv("OPUSOPTIMA_COMPACT_SPARE");
    struct Restore {
        std::optional<std::string> prior;
        ~Restore() {
            if (prior)
                setenv("OPUSOPTIMA_COMPACT_SPARE", prior->c_str(), 1);
            else
                unsetenv("OPUSOPTIMA_COMPACT_SPARE");
        }
    } restore{value ? std::optional<std::string>(value) : std::nullopt};
    std::vector<std::pair<std::string, std::vector<std::byte>>> baseline;
    for (const char* mode : {"off", "on"}) {
        setenv("OPUSOPTIMA_COMPACT_SPARE", mode, 1);
        auto puzzle = Pair();
        puzzle.name = "arbitrary-no-animismus";
        puzzle.permissionBits = (1ULL << 29) - 1;
        std::vector<std::pair<std::string, std::vector<std::byte>>> submitted;
        const auto first = AssemblySolver{}.CollectVerified(
            puzzle,
            [&](const auto& candidate, const auto& family) {
                auto canonical = candidate;
                canonical.solutionName.clear();
                canonical.metrics = {};
                submitted.emplace_back(family, io::BinaryWriter::WriteSolution(canonical));
                io::VerificationReport report;
                report.status = io::VerificationStatus::Passed;
                return report;
            },
            3);
        EXPECT_FALSE(first.objects.empty());
        if (std::string(mode) == "off")
            baseline = submitted;
        else
            EXPECT_EQ(submitted, baseline);
    }
}
TEST(AssemblyDiagnostics, WholeFeedSingletonOptInAuthorityFallbackAndDeadline) {
    const char* value = std::getenv("OPUSOPTIMA_WHOLE_FEED_DOCKING");
    struct Restore {
        std::optional<std::string> prior;
        ~Restore() {
            if (prior)
                setenv("OPUSOPTIMA_WHOLE_FEED_DOCKING", prior->c_str(), 1);
            else
                unsetenv("OPUSOPTIMA_WHOLE_FEED_DOCKING");
        }
    } restore{value ? std::optional<std::string>(value) : std::nullopt};
    const auto root = opusoptima::test::RepoRoot(__FILE__);
    const auto p =
        io::BinaryParser::ParsePuzzleFile(root / "asset/puzzle/24hour-1-test/GEN879.puzzle");
    std::vector<std::byte> baseline;
    for (const char* mode : {static_cast<const char*>(nullptr), "off", "unknown", "1"}) {
        if (mode)
            setenv("OPUSOPTIMA_WHOLE_FEED_DOCKING", mode, 1);
        else
            unsetenv("OPUSOPTIMA_WHOLE_FEED_DOCKING");
        AssemblySolver solver;
        std::vector<std::string> stages;
        const auto first = solver.CollectVerified(
            p,
            [&](const auto&, const auto& stage) {
                stages.push_back(stage);
                io::VerificationReport report;
                report.status = io::VerificationStatus::Passed;
                report.authority = io::VerificationAuthority::LegacyGate;
                return report;
            },
            1);
        ASSERT_EQ(stages.size(), 1);
        if (mode && std::string_view(mode) == "1") {
            EXPECT_EQ(stages.front(), "whole-feed-singleton-receivers");
        } else {
            EXPECT_NE(stages.front(), "whole-feed-singleton-receivers");
            const auto bytes = io::BinaryWriter::WriteSolution(first);
            if (baseline.empty()) baseline = bytes;
            EXPECT_EQ(bytes, baseline);
        }
    }
    setenv("OPUSOPTIMA_WHOLE_FEED_DOCKING", "1", 1);
    AssemblySolver fallback;
    bool dockingRejected = false, fallbackCalled = false;
    const auto first = fallback.CollectVerified(
        p,
        [&](const auto&, const auto& stage) {
            const bool docking = stage == "whole-feed-singleton-receivers";
            dockingRejected |= docking;
            fallbackCalled |= !docking;
            io::VerificationReport report;
            report.status = io::VerificationStatus::Passed;
            report.authority = docking ? io::VerificationAuthority::Independent
                                       : io::VerificationAuthority::LegacyGate;
            return report;
        },
        1);
    EXPECT_TRUE(dockingRejected);
    EXPECT_TRUE(fallbackCalled);
    EXPECT_NE(first.solutionName, "Closed whole-feed dock and identity scalar handoff");
    AssemblySolver expired;
    expired.SetDeadline(std::chrono::steady_clock::now());
    int dockingCalls = 0;
    try {
        (void)expired.CollectVerified(
            p,
            [&](const auto&, const auto& stage) {
                dockingCalls += stage == "whole-feed-singleton-receivers";
                return io::VerificationReport{};
            },
            1);
    } catch (const std::exception&) {
    }
    EXPECT_EQ(dockingCalls, 0);
}
TEST(AssemblyDiagnostics, StaticEmbeddingDefaultExplicitOffQuotaFirstAndDeadline) {
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
    const auto root = opusoptima::test::RepoRoot(__FILE__);
    const auto p =
        io::BinaryParser::ParsePuzzleFile(root / "asset/puzzle/campaign/ch2/P021.puzzle");
    std::vector<std::byte> originalFirst;
    for (const char* mode : {static_cast<const char*>(nullptr), "off", "unknown", "on"}) {
        if (mode)
            setenv("OPUSOPTIMA_STATIC_EMBEDDING", mode, 1);
        else
            unsetenv("OPUSOPTIMA_STATIC_EMBEDDING");
        for (std::size_t cap : {1U, 12U})
            for (bool expire : {false, true}) {
                AssemblySolver solver;
                std::size_t calls = 0;
                bool embedding = false;
                const auto first = solver.CollectVerified(
                    p,
                    [&](const auto& candidate, const auto& stage) {
                        if (calls++ == 0) {
                            auto bytes = io::BinaryWriter::WriteSolution(candidate);
                            if (originalFirst.empty()) originalFirst = bytes;
                            EXPECT_EQ(originalFirst, bytes);
                        }
                        embedding |= stage == "portfolio-static-embedding";
                        if (expire) solver.SetDeadline(std::chrono::steady_clock::now());
                        io::VerificationReport report;
                        report.status = io::VerificationStatus::Passed;
                        report.authority = io::VerificationAuthority::LegacyGate;
                        return report;
                    },
                    cap);
                EXPECT_FALSE(first.objects.empty());
                EXPECT_LE(calls, std::min(cap, std::size_t{4}));
                EXPECT_EQ(embedding, (!mode || std::string(mode) != "off") && cap > 1 && !expire);
            }
    }
}

TEST(AssemblyDiagnostics, StaticReactionDefaultOffDomainQuotaAndFirst) {
    const char* value = std::getenv("OPUSOPTIMA_STATIC_REACTION");
    struct Restore {
        std::optional<std::string> prior;
        ~Restore() {
            if (prior)
                setenv("OPUSOPTIMA_STATIC_REACTION", prior->c_str(), 1);
            else
                unsetenv("OPUSOPTIMA_STATIC_REACTION");
        }
    } restore{value ? std::optional<std::string>(value) : std::nullopt};
    const auto root = opusoptima::test::RepoRoot(__FILE__);
    const auto domain =
        io::BinaryParser::ParsePuzzleFile(root / "asset/puzzle/campaign/ch4/P032.puzzle");
    std::vector<std::byte> originalFirst;
    std::vector<std::pair<std::string, std::vector<std::byte>>> externalBaseline;
    for (const char* mode : {"off", static_cast<const char*>(nullptr), "on", "unknown"}) {
        if (mode)
            setenv("OPUSOPTIMA_STATIC_REACTION", mode, 1);
        else
            unsetenv("OPUSOPTIMA_STATIC_REACTION");
        const bool enabled = !mode || std::string(mode) != "off";
        for (bool external : {false, true}) {
            for (std::size_t cap : {1U, 12U}) {
                for (bool expire : {false, true}) {
                    auto puzzle = external ? Pair() : domain;
                    AssemblySolver solver;
                    solver.EnableExperimentalSharedAssembly(true);
                    std::vector<std::pair<std::string, std::vector<std::byte>>> submitted;
                    const auto first = solver.CollectVerified(
                        puzzle,
                        [&](const auto& candidate, const auto& stage) {
                            auto canonical = candidate;
                            canonical.metrics = {};
                            submitted.emplace_back(stage,
                                                   io::BinaryWriter::WriteSolution(canonical));
                            if (expire) solver.SetDeadline(std::chrono::steady_clock::now());
                            io::VerificationReport report;
                            report.status = io::VerificationStatus::Passed;
                            report.authority = io::VerificationAuthority::LegacyGate;
                            return report;
                        },
                        cap);
                    ASSERT_FALSE(submitted.empty());
                    auto canonicalFirst = first;
                    canonicalFirst.metrics = {};
                    EXPECT_EQ(io::BinaryWriter::WriteSolution(canonicalFirst),
                              submitted.front().second);
                    EXPECT_LE(submitted.size(), std::min(cap, std::size_t{4}));
                    const bool station =
                        std::any_of(submitted.begin(), submitted.end(), [](const auto& item) {
                            return item.first == "portfolio-static-reaction";
                        });
                    EXPECT_EQ(station, enabled && !external && cap > 1 && !expire);
                    if (!external) {
                        if (originalFirst.empty()) originalFirst = submitted.front().second;
                        EXPECT_EQ(submitted.front().second, originalFirst);
                    } else if (cap == 12 && !expire) {
                        if (mode && std::string(mode) == "off")
                            externalBaseline = submitted;
                        else
                            EXPECT_EQ(submitted, externalBaseline);
                    }
                }
            }
        }
        int calls = 0;
        (void)AssemblySolver{}.SolveAccepted(domain, [&](const auto&) {
            ++calls;
            return true;
        });
        EXPECT_EQ(calls, 1);
    }
}

TEST(AssemblyDiagnostics, StaticPurificationReusesReactionSlotAndPreservesFirst) {
    const char* value = std::getenv("OPUSOPTIMA_STATIC_REACTION");
    struct Restore {
        std::optional<std::string> prior;
        ~Restore() {
            if (prior)
                setenv("OPUSOPTIMA_STATIC_REACTION", prior->c_str(), 1);
            else
                unsetenv("OPUSOPTIMA_STATIC_REACTION");
        }
    } restore{value ? std::optional<std::string>(value) : std::nullopt};
    const auto root = opusoptima::test::RepoRoot(__FILE__);
    const auto p =
        io::BinaryParser::ParsePuzzleFile(root / "asset/puzzle/edge-cases/c988640836512960.puzzle");
    std::vector<std::byte> originalFirst;
    // Rejected and nonnative purification preserve the embedding opportunity.
    for (int mode = 0; mode < 7; ++mode) {
        if (mode == 0)
            setenv("OPUSOPTIMA_STATIC_REACTION", "off", 1);
        else
            unsetenv("OPUSOPTIMA_STATIC_REACTION");
        AssemblySolver solver;
        std::size_t calls = 0;
        bool purification = false, embedding = false;
        const std::size_t cap = mode == 2 ? 1 : 3;
        const auto first = solver.CollectVerified(
            p,
            [&](const auto& candidate, const auto& stage) {
                if (calls++ == 0) {
                    const auto bytes = io::BinaryWriter::WriteSolution(candidate);
                    if (originalFirst.empty()) originalFirst = bytes;
                    EXPECT_EQ(bytes, originalFirst);
                }
                const bool isPurification = stage == "portfolio-static-reaction-purification";
                purification |= isPurification;
                embedding |= stage == "portfolio-static-embedding";
                if (mode == 3) solver.SetDeadline(std::chrono::steady_clock::now());
                io::VerificationReport report;
                report.status = mode == 5 && isPurification ? io::VerificationStatus::Rejected
                                                            : io::VerificationStatus::Passed;
                report.authority = mode == 4 || (mode == 6 && isPurification)
                                       ? io::VerificationAuthority::Independent
                                       : io::VerificationAuthority::LegacyGate;
                return report;
            },
            cap);
        EXPECT_EQ(io::BinaryWriter::WriteSolution(first), originalFirst);
        EXPECT_EQ(purification, mode == 1 || mode == 5 || mode == 6);
        EXPECT_EQ(embedding, mode == 0 || mode == 5 || mode == 6);
        EXPECT_LE(calls, cap + (mode == 5 || mode == 6 ? 1U : 0U));
        EXPECT_LE(calls, AssemblySolver::MaxCandidateSubmissions);
    }
}

TEST(AssemblyDiagnostics, SingletonChainUsesRemainingSlotAndRetainsFirstAndStatic) {
    const char* value = std::getenv("OPUSOPTIMA_SINGLETON_PURIFICATION_CHAIN");
    struct Restore {
        std::optional<std::string> prior;
        ~Restore() {
            if (prior)
                setenv("OPUSOPTIMA_SINGLETON_PURIFICATION_CHAIN", prior->c_str(), 1);
            else
                unsetenv("OPUSOPTIMA_SINGLETON_PURIFICATION_CHAIN");
        }
    } restore{value ? std::optional<std::string>(value) : std::nullopt};
    const auto root = opusoptima::test::RepoRoot(__FILE__);
    const auto p =
        io::BinaryParser::ParsePuzzleFile(root / "asset/puzzle/campaign/ch4/P036.puzzle");
    std::vector<std::byte> originalFirst, originalStatic;
    // Additional modes cover depths two through four, direct target supply,
    // nearest-source selection and explicit on at the original depth five.
    for (int mode = 0; mode < 15; ++mode) {
        SCOPED_TRACE(mode);
        auto puzzle = p;
        const auto target = puzzle.outputs[0].atoms[0].type;
        if (mode >= 9 && mode <= 11)
            puzzle.inputs[0].atoms[0].type =
                static_cast<decltype(target)>(static_cast<int>(target) + mode - 7);
        if (mode == 12 || mode == 13) {
            auto feed = puzzle.inputs[0];
            feed.atoms[0].type =
                static_cast<decltype(target)>(static_cast<int>(target) + (mode == 13 ? 3 : 0));
            puzzle.inputs.push_back(feed);
        }
        EXPECT_TRUE(solvers::SupportsSingletonPurificationChain(puzzle));
        EXPECT_EQ(solvers::AdmitsSingletonPurificationChain(puzzle),
                  mode != 9 && mode != 10 && mode != 12 && mode != 13);
        if (mode == 0)
            setenv("OPUSOPTIMA_SINGLETON_PURIFICATION_CHAIN", "off", 1);
        else if (mode == 8)
            setenv("OPUSOPTIMA_SINGLETON_PURIFICATION_CHAIN", "invalid", 1);
        else if (mode == 14)
            setenv("OPUSOPTIMA_SINGLETON_PURIFICATION_CHAIN", "on", 1);
        else
            unsetenv("OPUSOPTIMA_SINGLETON_PURIFICATION_CHAIN");
        AssemblySolver solver;
        std::vector<std::string> stages;
        const std::size_t cap = mode == 2 ? 1 : mode == 3 ? 2 : 3;
        const bool originalPuzzle = mode < 9 || mode == 14;
        const auto first = solver.CollectVerified(
            puzzle,
            [&](const auto& candidate, const auto& stage) {
                stages.push_back(stage);
                const auto bytes = io::BinaryWriter::WriteSolution(candidate);
                if (stages.size() == 1 && originalPuzzle) {
                    if (originalFirst.empty()) originalFirst = bytes;
                    EXPECT_EQ(bytes, originalFirst);
                }
                if (stage == "portfolio-static-reaction-purification" && originalPuzzle) {
                    if (originalStatic.empty()) originalStatic = bytes;
                    EXPECT_EQ(bytes, originalStatic);
                }
                if (mode == 4) solver.SetDeadline(std::chrono::steady_clock::now());
                io::VerificationReport report;
                report.status = mode == 7 && stage == "portfolio-singleton-purification-chain"
                                    ? io::VerificationStatus::Rejected
                                    : io::VerificationStatus::Passed;
                report.authority =
                    mode == 5 || (mode == 6 && stage == "portfolio-singleton-purification-chain")
                        ? io::VerificationAuthority::Independent
                        : io::VerificationAuthority::LegacyGate;
                return report;
            },
            cap);
        if (originalPuzzle) EXPECT_EQ(io::BinaryWriter::WriteSolution(first), originalFirst);
        EXPECT_EQ(std::count(stages.begin(), stages.end(), "portfolio-static-embedding"),
                  mode == 12 ? 1 : 0);
        const bool chain = mode == 1 || mode == 6 || mode == 7 || mode == 11 || mode == 14;
        EXPECT_EQ(
            std::count(stages.begin(), stages.end(), "portfolio-singleton-purification-chain"),
            chain ? 1 : 0);
        if (chain) {
            ASSERT_EQ(stages.size(), 3U);
            EXPECT_EQ(stages[1], "portfolio-static-reaction-purification");
            EXPECT_EQ(stages[2], "portfolio-singleton-purification-chain");
        }
        EXPECT_LE(stages.size(), cap);
        EXPECT_LE(stages.size(), AssemblySolver::MaxCandidateSubmissions);
    }
}

TEST(AssemblyDiagnostics, PortfolioDeduplicatesAndPreservesFirst) {
    auto p = Pair();
    p.permissionBits = (1ULL << 29) - 1;
    std::vector<std::vector<std::byte>> submitted;
    AssemblySolver solver;
    auto first = solver.CollectVerified(
        p,
        [&](const auto& candidate, const auto&) {
            auto normalized = candidate;
            normalized.metrics = {};
            normalized.solutionName.clear();
            auto bytes = io::BinaryWriter::WriteSolution(normalized);
            EXPECT_EQ(std::count(submitted.begin(), submitted.end(), bytes), 0);
            submitted.push_back(bytes);
            io::VerificationReport report;
            report.status = io::VerificationStatus::Passed;
            return report;
        },
        5);
    first.metrics = {};
    first.solutionName.clear();
    ASSERT_GE(submitted.size(), 2U);
    EXPECT_EQ(io::BinaryWriter::WriteSolution(first), submitted.front());
    EXPECT_LE(submitted.size(), 4U);
}

TEST(AssemblyDiagnostics, PortfolioRetainsFirstWhenAlternativesRejectOrBudgetExpires) {
    for (bool expire : {false, true}) {
        auto p = Pair();
        p.permissionBits = (1ULL << 29) - 1;
        AssemblySolver solver;
        int submissions = 0;
        std::vector<std::byte> acceptedBytes;
        auto first = solver.CollectVerified(
            p,
            [&](const auto& candidate, const auto&) {
                if (submissions == 0) acceptedBytes = io::BinaryWriter::WriteSolution(candidate);
                io::VerificationReport report;
                report.status = ++submissions == 1 ? io::VerificationStatus::Passed
                                                   : io::VerificationStatus::Rejected;
                if (expire) solver.SetDeadline(std::chrono::steady_clock::now());
                return report;
            },
            4);
        EXPECT_FALSE(first.objects.empty());
        EXPECT_EQ(io::BinaryWriter::WriteSolution(first), acceptedBytes);
        if (expire) {
            EXPECT_EQ(submissions, 1);
        } else {
            EXPECT_GT(submissions, 1);
            EXPECT_LE(submissions, 4);
        }
    }
}
TEST(AssemblyDiagnostics, CompactRelayPreservesParentCapsAndDeadline) {
    auto puzzle = Pair();
    puzzle.permissionBits = (1ULL << 32) - 1;
    puzzle.inputs = {
        {{{core::AtomType::Air, {0, 0}}, {core::AtomType::Air, {1, 0}}}, {{1, {0, 0}, {1, 0}}}}};
    puzzle.outputs = {
        {{{core::AtomType::Salt, {0, 0}}, {core::AtomType::Salt, {1, 0}}}, {{1, {0, 0}, {1, 0}}}}};
    for (bool enabled : {false, true}) {
        for (bool expire : {false, true}) {
            for (std::size_t cap : {1U, 3U}) {
                AssemblySolver solver;
                solver.EnableExperimentalCompactRelay(enabled);
                std::vector<std::byte> parent;
                std::size_t submissions = 0;
                bool relay = false;
                const auto first = solver.CollectVerified(
                    puzzle,
                    [&](const auto& candidate, const auto& stage) {
                        ++submissions;
                        if (submissions == 1) parent = io::BinaryWriter::WriteSolution(candidate);
                        if (stage == "portfolio-compact-chemistry-relay") relay = true;
                        if (expire) solver.SetDeadline(std::chrono::steady_clock::now());
                        io::VerificationReport report;
                        report.status = io::VerificationStatus::Passed;
                        return report;
                    },
                    cap);
                EXPECT_EQ(io::BinaryWriter::WriteSolution(first), parent);
                EXPECT_LE(submissions, cap);
                EXPECT_EQ(relay, enabled && !expire && cap > 1);
            }
        }
    }
    auto multi = puzzle;
    multi.outputs.push_back(multi.outputs.front());
    AssemblySolver solver;
    solver.EnableExperimentalCompactRelay(true);
    std::size_t submissions = 0;
    solver.CollectVerified(
        multi,
        [&](const auto&, const auto& stage) {
            ++submissions;
            EXPECT_NE(stage, "portfolio-compact-chemistry-relay");
            io::VerificationReport report;
            report.status = io::VerificationStatus::Passed;
            return report;
        },
        3);
    EXPECT_EQ(submissions, 1U);
}

TEST(AssemblyDiagnostics, BatchConsumerRetainsFirstAndRejectsWithinParentCap) {
    ScopedEmbeddingOff isolateLegacyPortfolio;
    const auto root = opusoptima::test::RepoRoot(__FILE__);
    const auto puzzle =
        io::BinaryParser::ParsePuzzleFile(root / "asset/puzzle/journal/journal8/P102.puzzle");
    for (bool enabled : {false, true}) {
        for (bool expire : {false, true}) {
            AssemblySolver solver;
            solver.EnableExperimentalSharedAssembly(true);
            solver.EnableExperimentalSharedScheduling(true);
            solver.EnableExperimentalSharedBufferRelay(true);
            solver.EnableExperimentalSharedBatchConsumer(enabled);
            std::vector<std::byte> parent;
            std::size_t submissions = 0;
            bool bypass = false;
            const auto first = solver.CollectVerified(
                puzzle,
                [&](const auto& candidate, const auto& stage) {
                    ++submissions;
                    if (submissions == 1) parent = io::BinaryWriter::WriteSolution(candidate);
                    bypass |= stage == "portfolio-shared-batch-consumer";
                    io::VerificationReport report;
                    report.status = submissions <= 2 ? io::VerificationStatus::Passed
                                                     : io::VerificationStatus::Rejected;
                    if (expire) solver.SetDeadline(std::chrono::steady_clock::now());
                    return report;
                },
                3);
            EXPECT_EQ(io::BinaryWriter::WriteSolution(first), parent);
            EXPECT_LE(submissions, 3U);
            EXPECT_EQ(bypass, enabled && !expire);
        }
    }
}

TEST(AssemblyDiagnostics, NearCacheRetainsFirstAndRejectsWithinParentCap) {
    const auto root = opusoptima::test::RepoRoot(__FILE__);
    const auto puzzle =
        io::BinaryParser::ParsePuzzleFile(root / "asset/puzzle/journal/journal8/P102.puzzle");
    for (bool enabled : {false, true}) {
        for (bool expire : {false, true}) {
            AssemblySolver solver;
            solver.EnableExperimentalSharedAssembly(true);
            solver.EnableExperimentalSharedScheduling(true);
            solver.EnableExperimentalSharedBufferRelay(true);
            solver.EnableExperimentalSharedBatchConsumer(true);
            solver.EnableExperimentalSharedNearCache(enabled);
            std::vector<std::byte> parent;
            std::size_t submissions = 0;
            bool bypass = false;
            const auto first = solver.CollectVerified(
                puzzle,
                [&](const auto& candidate, const auto& stage) {
                    ++submissions;
                    if (submissions == 1) parent = io::BinaryWriter::WriteSolution(candidate);
                    bypass |= stage == "portfolio-shared-near-cache";
                    io::VerificationReport report;
                    report.status = submissions <= 2 ? io::VerificationStatus::Passed
                                                     : io::VerificationStatus::Rejected;
                    if (expire) solver.SetDeadline(std::chrono::steady_clock::now());
                    return report;
                },
                3);
            EXPECT_EQ(io::BinaryWriter::WriteSolution(first), parent);
            EXPECT_LE(submissions, 3U);
            EXPECT_EQ(bypass, enabled && !expire);
        }
    }
}

TEST(AssemblyDiagnostics, PortfolioNeverExpandsCallerAcceptedCap) {
    for (std::size_t cap : {1U, 2U, 3U}) {
        auto p = Pair();
        p.permissionBits = (1ULL << 29) - 1;
        AssemblySolver solver;
        std::size_t submissions = 0;
        auto first = solver.CollectVerified(
            p,
            [&](const auto&, const auto&) {
                ++submissions;
                io::VerificationReport report;
                report.status = io::VerificationStatus::Passed;
                return report;
            },
            cap);
        EXPECT_FALSE(first.objects.empty());
        EXPECT_LE(submissions, cap);
    }
}

TEST(AssemblyDiagnostics, MultipleOutputsStopFallbackAfterFirstVerified) {
    auto p = Pair();
    p.outputs.push_back(p.outputs.front());
    p.permissionBits = (1ULL << 29) - 1;
    AssemblySolver solver;
    std::size_t submissions = 0;
    auto first = solver.CollectVerified(
        p,
        [&](const auto&, const auto&) {
            ++submissions;
            io::VerificationReport report;
            report.status = io::VerificationStatus::Passed;
            return report;
        },
        3);
    EXPECT_FALSE(first.objects.empty());
    EXPECT_EQ(submissions, 1U);
}

TEST(AssemblyDiagnostics, RejectedCandidateFallsBackToLaterCompiler) {
    auto p = Pair();
    p.inputs.push_back(p.outputs[0]);
    p.permissionBits = (1ULL << 28) - 1;
    int attempts = 0;
    auto result =
        AssemblySolver{}.SolveAccepted(p, [&](const io::SolutionData&) { return ++attempts > 1; });
    EXPECT_GE(attempts, 2);
    EXPECT_FALSE(result.objects.empty());
}

TEST(AssemblyDiagnostics, OrdinaryCompatibleFeedIsAttemptedOnceBeforeChainedChemistry) {
    auto p = Pair();
    p.permissionBits = (1ULL << 28) - 1;
    int candidates = 0;
    try {
        (void)AssemblySolver{}.SolveAccepted(p, [&](const io::SolutionData&) {
            ++candidates;
            return false;
        });
        FAIL() << "Rejected candidates unexpectedly accepted";
    } catch (const std::invalid_argument& error) {
        const std::string message = error.what();
        const auto ordinary = message.find("ordinary-assembly:");
        const auto chained = message.find("life-death:");
        ASSERT_NE(ordinary, std::string::npos);
        ASSERT_NE(chained, std::string::npos);
        EXPECT_LT(ordinary, chained);
        EXPECT_EQ(message.find("ordinary-assembly:", ordinary + 1), std::string::npos);
        EXPECT_NE(message.find("ordinary-assembly: candidate failed acceptance"),
                  std::string::npos);
    }
    EXPECT_GT(candidates, 0);
}

TEST(AssemblyDiagnostics, AcceptanceInfrastructureErrorsPropagate) {
    auto p = Pair();
    EXPECT_THROW((void)AssemblySolver{}.SolveAccepted(p,
                                                      [](const io::SolutionData&) -> bool {
                                                          throw std::runtime_error(
                                                              "verifier unavailable");
                                                      }),
                 std::runtime_error);
}

TEST(AssemblyDiagnostics, SubsetAcceptanceInvalidArgumentPropagates) {
    auto p = Pair();
    p.inputs.push_back(p.outputs[0]);
    p.permissionBits = (1ULL << 28) - 1;
    int attempts = 0;
    try {
        (void)AssemblySolver{}.SolveAccepted(p, [&](const io::SolutionData&) -> bool {
            ++attempts;
            throw std::invalid_argument("acceptance infrastructure error");
        });
        FAIL() << "Acceptance error was swallowed";
    } catch (const std::invalid_argument& error) {
        EXPECT_STREQ(error.what(), "acceptance infrastructure error");
    }
    EXPECT_EQ(attempts, 1);
}

TEST(AssemblyDiagnostics, RetainsAttemptedStrategies) {
    auto p = Pair();
    p.permissionBits = 0;
    p.inputs = {{{{core::AtomType::Salt, {0, 0}}}, {}}};
    p.outputs[0].atoms[0].type = core::AtomType::Gold;
    p.outputs[0].atoms[1].type = core::AtomType::Gold;
    try {
        (void)AssemblySolver{}.Solve(p);
        FAIL() << "Unsupported recipe unexpectedly solved";
    } catch (const std::invalid_argument& error) {
        const std::string message = error.what();
        EXPECT_NE(message.find("assembly strategies exhausted"), std::string::npos);
        EXPECT_NE(message.find("intact-feed:"), std::string::npos);
        EXPECT_NE(message.find("metal-purification:"), std::string::npos);
        EXPECT_NE(message.find("ordinary-assembly:"), std::string::npos);
    }
    EXPECT_FALSE(AssemblySolver::Supports(p));
}

TEST(AssemblyDiagnostics, EntryRejectionDoesNotClaimStrategiesWereAttempted) {
    auto p = Pair();
    p.productionInfo.emplace();
    try {
        (void)AssemblySolver{}.Solve(p);
        FAIL() << "Production rules unexpectedly accepted";
    } catch (const std::invalid_argument& error) {
        const std::string message = error.what();
        EXPECT_NE(message.find("special-rules:"), std::string::npos);
        EXPECT_EQ(message.find("intact-feed:"), std::string::npos);
        EXPECT_EQ(message.find("strategies exhausted"), std::string::npos);
    }
}

TEST(AssemblyDiagnostics, IndependentOutputFailureRetainsOutputIndex) {
    auto p = Pair();
    p.permissionBits = 0;
    p.inputs = {{{{core::AtomType::Salt, {0, 0}}}, {}}};
    p.outputs[0].atoms[0].type = core::AtomType::Gold;
    p.outputs.push_back(p.outputs[0]);
    try {
        (void)AssemblySolver{}.Solve(p);
        FAIL() << "Unsupported outputs unexpectedly solved";
    } catch (const std::invalid_argument& error) {
        const std::string message = error.what();
        EXPECT_NE(message.find("shared-ordinary-outputs:"), std::string::npos);
        EXPECT_NE(message.find("independent-outputs: output[0]:"), std::string::npos);
        EXPECT_NE(message.find("ordinary-assembly:"), std::string::npos);
    }
}

TEST(AssemblyDiagnostics, ChemicalInfeasibilityIsLimitedToSupportedModel) {
    auto p = Pair();
    p.permissionBits = 0;
    p.inputs = {{{{core::AtomType::Salt, {0, 0}}}, {}}};
    p.outputs[0].atoms[0].type = core::AtomType::Gold;
    try {
        (void)solvers::ChemicalPlanner::Plan(p);
        FAIL() << "Unsupported recipe unexpectedly feasible";
    } catch (const std::invalid_argument& error) {
        EXPECT_NE(std::string(error.what()).find("infeasible within supported reaction model"),
                  std::string::npos);
    }
    p.inputs.clear();
    try {
        (void)solvers::ChemicalPlanner::Plan(p);
        FAIL() << "Empty inputs unexpectedly accepted";
    } catch (const std::invalid_argument& error) {
        EXPECT_EQ(std::string(error.what()).find("infeasible within supported reaction model"),
                  std::string::npos);
    }
}

void Replay(const io::PuzzleData& p, const AssemblyPlan& plan) {
    AssemblyPlan::State state;
    std::set<std::size_t> present;
    std::set<std::pair<std::size_t, std::size_t>> bonds;
    for (const auto& op : plan.operations) {
        EXPECT_EQ(op.preconditions, state);
        ASSERT_FALSE(op.reservations.empty());
        EXPECT_EQ(op.reservations[0].kind, AssemblyPlan::ResourceKind::Workstation);
        if (op.kind == AssemblyPlan::Kind::Acquire) {
            EXPECT_TRUE(present.insert(op.atom).second);
            EXPECT_EQ(p.inputs[op.input].atoms[plan.sourceAtomForAtom[op.atom]].type,
                      p.outputs[0].atoms[op.atom].type);
            state.atoms.push_back(op.atom);
            EXPECT_EQ(op.reservations.size(), 2U);
            EXPECT_EQ(op.reservations[1].kind, AssemblyPlan::ResourceKind::Feed);
            EXPECT_EQ(op.reservations[1].index, op.input);
        } else if (op.kind == AssemblyPlan::Kind::Bond) {
            EXPECT_TRUE(present.count(op.edge.a));
            EXPECT_TRUE(present.count(op.edge.b));
            EXPECT_TRUE(bonds.insert({op.edge.a, op.edge.b}).second);
            EXPECT_EQ(core::Distance(p.outputs[0].atoms[op.edge.a].position,
                                     p.outputs[0].atoms[op.edge.b].position),
                      1);
            state.bonds.push_back(op.edge);
        } else {
            EXPECT_EQ(present.size(), p.outputs[0].atoms.size());
            EXPECT_EQ(bonds.size(), p.outputs[0].bonds.size());
            EXPECT_EQ(op.reservations[1].kind, AssemblyPlan::ResourceKind::Output);
        }
        std::vector<core::HexCoord> occupied;
        for (auto atom : state.atoms)
            occupied.push_back(p.outputs[0].atoms[atom].position);
        EXPECT_EQ(op.occupied, occupied);
        if (op.kind == AssemblyPlan::Kind::Output) {
            state.atoms.clear();
            state.bonds.clear();
            state.emitted = true;
        }
        EXPECT_EQ(op.effects, state);
    }
    EXPECT_TRUE(state.emitted);
    EXPECT_TRUE(state.atoms.empty());
    AssemblyPlanner::Validate(p, plan);
}

#ifdef HAS_LIBVERIFY
// v3 little-endian byte fixture: u32 version, u8 name length + name,
// u64 creator, u64 permissions, then input/output molecule lists (u32 count;
// each u32 atom count + [u8 type,i8 q,i8 r], u32 bond count +
// [u8 bits,i8 from.q,i8 from.r,i8 to.q,i8 to.r]), u32 multiplier, u8 production.
void WriteGeneratedPuzzle(const std::filesystem::path& path, const io::PuzzleData& p) {
    std::ofstream out(path, std::ios::binary);
    const auto word = [&](std::uint64_t value, int bytes) {
        for (int i = 0; i < bytes; ++i)
            out.put(static_cast<char>(value >> (8 * i)));
    };
    word(3, 4);
    word(p.name.size(), 1);
    out.write(p.name.data(), p.name.size());
    word(0, 8);
    word(p.permissionBits, 8);
    const auto list = [&](const std::vector<io::Molecule>& molecules) {
        word(molecules.size(), 4);
        for (const auto& m : molecules) {
            word(m.atoms.size(), 4);
            for (const auto& a : m.atoms) {
                word(static_cast<std::uint8_t>(a.type), 1);
                word(a.position.q, 1);
                word(a.position.r, 1);
            }
            word(m.bonds.size(), 4);
            for (const auto& b : m.bonds) {
                word(b.bits, 1);
                word(b.from.q, 1);
                word(b.from.r, 1);
                word(b.to.q, 1);
                word(b.to.r, 1);
            }
        }
    };
    list(p.inputs);
    list(p.outputs);
    word(p.outputMultiplier, 4);
    word(0, 1);
    ASSERT_TRUE(out.good());
}
class AssemblyVerifierTest : public testing::Test {
    protected:
    std::filesystem::path puzzlePath, solutionPath;
    void SetUp() override {
        const auto root = opusoptima::test::RepoRoot(__FILE__);
        const auto stem = std::string("assembly-verifier-") +
                          testing::UnitTest::GetInstance()->current_test_info()->name();
        puzzlePath = root / "build" / (stem + ".puzzle");
        solutionPath = root / "build" / (stem + ".solution");
    }
    void TearDown() override {
        std::error_code error;
        std::filesystem::remove(puzzlePath, error);
        std::filesystem::remove(solutionPath, error);
    }
    void VerifyRow(io::PuzzleData p) {
        p.permissionBits |= (1ULL << 2) | (1ULL << 3);
        WriteGeneratedPuzzle(puzzlePath, p);
        auto solution = AssemblySolver{}.Solve(p);
        io::BinaryWriter::WriteSolutionFile(solutionPath, solution);
        auto report = io::VerifySolutionFiles(puzzlePath, solutionPath, 30000, 2000);
        ASSERT_TRUE(report.Passed()) << report.detail << " cycle=" << report.errorCycle
                                     << " cell=" << report.errorU << ',' << report.errorV;
        solution.metrics = report.metrics;
        io::BinaryWriter::WriteSolutionFile(solutionPath, solution);
        const auto rewritten = io::VerifySolutionFiles(puzzlePath, solutionPath, 30000, 2000);
        ASSERT_TRUE(rewritten.Passed()) << rewritten.detail;
        EXPECT_EQ(rewritten.metrics.cycles, report.metrics.cycles);
        EXPECT_EQ(rewritten.metrics.cost, report.metrics.cost);
        EXPECT_EQ(rewritten.metrics.area, report.metrics.area);
        EXPECT_EQ(rewritten.metrics.instructionCount, report.metrics.instructionCount);
    }
    void Verify(const io::PuzzleData& p) {
        WriteGeneratedPuzzle(puzzlePath, p);
        const auto parsed = io::BinaryParser::ParsePuzzleFile(puzzlePath);
        auto solution = AssemblySolver{}.Solve(parsed);
        io::BinaryWriter::WriteSolutionFile(solutionPath, solution);
        const auto report = io::VerifySolutionFiles(puzzlePath, solutionPath, 5000, 2000);
        ASSERT_TRUE(report.Passed()) << report.detail << " cycle=" << report.errorCycle
                                     << " cell=" << report.errorU << ',' << report.errorV;
        solution.metrics = report.metrics;
        io::BinaryWriter::WriteSolutionFile(solutionPath, solution);
        const auto measured = io::VerifySolutionFiles(puzzlePath, solutionPath, 5000, 2000);
        ASSERT_TRUE(measured.Passed()) << measured.detail;
        EXPECT_EQ(measured.metrics.cycles, report.metrics.cycles);
        EXPECT_EQ(measured.metrics.cost, report.metrics.cost);
        EXPECT_EQ(measured.metrics.area, report.metrics.area);
        EXPECT_EQ(measured.metrics.instructionCount, report.metrics.instructionCount);
    }
};
TEST_F(AssemblyVerifierTest, PortfolioArchivesDistinctRealStructures) {
    ScopedEmbeddingOff isolateLegacyPortfolio;
    auto p = Pair();
    p.permissionBits = (1ULL << 29) - 1;
    WriteGeneratedPuzzle(puzzlePath, p);
    io::CandidateArchive archive(puzzlePath, solutionPath, {});
    AssemblySolver solver;
    solver.SetDeadline(std::chrono::steady_clock::now() + std::chrono::seconds(10));
    std::set<std::string> accepted;
    std::array<std::optional<int>, 3> best;
    auto first = solver.CollectVerified(
        p,
        [&](const auto& candidate, const auto& family) {
            auto report = archive.Submit(candidate, family);
            if (report.Passed()) {
                accepted.insert(family);
                const std::array<int, 3> scores{report.metrics.cost, report.metrics.cycles,
                                                report.metrics.area};
                for (std::size_t i = 0; i < best.size(); ++i)
                    best[i] = best[i] ? std::min(*best[i], scores[i]) : scores[i];
            }
            const auto& slots = archive.Slots();
            for (std::size_t i = 0; i < best.size(); ++i) {
                if (!best[i]) continue;
                EXPECT_TRUE(slots[i]);
                if (!slots[i]) continue;
                const auto& m = slots[i]->verification.metrics;
                EXPECT_EQ((std::array<int, 3>{m.cost, m.cycles, m.area})[i], *best[i]);
            }
            return report;
        },
        4);
    ASSERT_EQ(accepted.size(), 4U);
    ASSERT_TRUE(archive.First());
    EXPECT_EQ(first.objects.size(), archive.First()->solution.objects.size());
    EXPECT_TRUE(archive.Publish({}).Passed());
    EXPECT_EQ(io::BinaryParser::ReadAllBytes(solutionPath), archive.First()->bytes);
    for (const auto& slot : archive.Slots())
        ASSERT_TRUE(slot);
    EXPECT_LT(archive.Slots()[0]->verification.metrics.cost,
              archive.First()->verification.metrics.cost);
    std::filesystem::remove(solutionPath.string() + ".objectives.json");
    std::filesystem::remove_all(archive.Directory());
}
TEST_F(AssemblyVerifierTest, PortfolioSemanticTransformsAndPermissionBoundary) {
    ScopedEmbeddingOff isolateLegacyPortfolio;
    // This case pins the legacy portfolio families/clearance ordering under a
    // 2 s deadline; the clocked companion is covered by its own test and the
    // full-corpus measurement.
    ScopedRowClockPortfolioOff isolateLegacyFamilies;
    for (int rotation = 0; rotation < 6; ++rotation) {
        auto p = Pair();
        p.permissionBits = (1ULL << 29) - 1;
        auto transform = [&](core::HexCoord c) {
            for (int i = 0; i < rotation; ++i)
                c = {-c.r, c.q + c.r};
            return core::HexCoord{c.q + 3, c.r - 2};
        };
        for (auto& atom : p.outputs[0].atoms)
            atom.position = transform(atom.position);
        for (auto& bond : p.outputs[0].bonds) {
            bond.from = transform(bond.from);
            bond.to = transform(bond.to);
        }
        if (rotation % 2) std::reverse(p.inputs.begin(), p.inputs.end());
        WriteGeneratedPuzzle(puzzlePath, p);
        std::set<std::string> families;
        auto solver = AssemblySolver{};
        solver.SetDeadline(std::chrono::steady_clock::now() + std::chrono::seconds(2));
        auto first = solver.CollectVerified(
            p,
            [&](const auto& candidate, const auto& family) {
                io::BinaryWriter::WriteSolutionFile(solutionPath, candidate);
                auto report = io::VerifySolutionFiles(puzzlePath, solutionPath, 30000, 2000);
                EXPECT_TRUE(report.Passed()) << family << ": " << report.detail;
                if (report.Passed()) families.insert(family);
                return report;
            },
            4);
        EXPECT_FALSE(first.objects.empty());
        EXPECT_TRUE(families.count("portfolio-row-compact") ||
                    families.count("portfolio-row-retimed"));
        EXPECT_TRUE(families.count("portfolio-row-clearance"));
        p.permissionBits &= ~((1ULL << 2) | (1ULL << 3));
        WriteGeneratedPuzzle(puzzlePath, p);
        bool permissionAccepted = false;
        solver.SetDeadline(std::chrono::steady_clock::now() + std::chrono::seconds(2));
        solver.CollectVerified(
            p,
            [&](const auto& candidate, const auto& family) {
                EXPECT_NE(family, "portfolio-row-compact");
                EXPECT_NE(family, "portfolio-row-retimed");
                EXPECT_NE(family, "portfolio-row-clearance");
                io::BinaryWriter::WriteSolutionFile(solutionPath, candidate);
                auto report = io::VerifySolutionFiles(puzzlePath, solutionPath, 30000, 2000);
                permissionAccepted |= report.Passed();
                return report;
            },
            4);
        EXPECT_TRUE(permissionAccepted);
    }
}
TEST_F(AssemblyVerifierTest, CalcifiedAtomicWheelSupplyVerifiesAllCardinalPairs) {
    for (int source = 2; source <= 5; ++source) {
        for (int target = 2; target <= 5; ++target) {
            if (source == target) continue;
            SCOPED_TRACE(std::to_string(source) + " -> " + std::to_string(target));
            auto p = Pair();
            p.permissionBits = (1ULL << 29) - 1;
            p.inputs = {{{{static_cast<core::AtomType>(source), {1, -1}}}, {}}};
            p.outputs[0].atoms[0].type = static_cast<core::AtomType>(target);
            p.outputs[0].atoms[1].type = static_cast<core::AtomType>(source);
            Verify(p);
        }
    }
}
TEST_F(AssemblyVerifierTest, JointChemicalCoproductsVerifyIndependently) {
    for (const bool dispersion : {false, true}) {
        SCOPED_TRACE(dispersion);
        auto p = Pair();
        p.permissionBits = (1ULL << 29) - 1;
        p.inputs = {
            {{{dispersion ? core::AtomType::Quintessence : core::AtomType::Salt, {0, 0}}}, {}}};
        p.outputs.clear();
        for (const auto type : dispersion
                                   ? std::vector{core::AtomType::Air, core::AtomType::Salt,
                                                 core::AtomType::Salt, core::AtomType::Salt}
                                   : std::vector{core::AtomType::Vitae, core::AtomType::Mors})
            p.outputs.push_back({{{type, {0, 0}}}, {}});
        WriteGeneratedPuzzle(puzzlePath, p);
        auto solution = solvers::CompileVerifiedSupplyInventory(p, [&](const auto& candidate) {
            io::BinaryWriter::WriteSolutionFile(solutionPath, candidate);
            return io::VerifySolutionFiles(puzzlePath, solutionPath, 30000, 2000);
        });
        ASSERT_TRUE(solution.has_value());
        io::BinaryWriter::WriteSolutionFile(solutionPath, *solution);
        const auto report = io::VerifySolutionFiles(puzzlePath, solutionPath, 30000, 2000);
        ASSERT_TRUE(report.Passed()) << report.detail;
        EXPECT_EQ(report.metrics.cycles, solution->metrics.cycles);
    }
}
TEST_F(AssemblyVerifierTest, JointCatalystBorrowAndReturnVerifiesIndependently) {
    auto p = Pair();
    p.permissionBits = (1ULL << 29) - 1;
    p.inputs = {{{{core::AtomType::Salt, {0, 0}}}, {}}, {{{core::AtomType::Air, {0, 0}}}, {}}};
    p.outputs = {
        {{{core::AtomType::Air, {0, 0}}, {core::AtomType::Air, {1, 0}}}, {{1, {0, 0}, {1, 0}}}},
        {{{core::AtomType::Air, {0, 0}}, {core::AtomType::Air, {1, 0}}}, {{1, {0, 0}, {1, 0}}}},
        {{{core::AtomType::Air, {0, 0}}}, {}}};
    WriteGeneratedPuzzle(puzzlePath, p);
    auto solution = solvers::CompileVerifiedSupplyInventory(p, [&](const auto& candidate) {
        io::BinaryWriter::WriteSolutionFile(solutionPath, candidate);
        return io::VerifySolutionFiles(puzzlePath, solutionPath, 30000, 2000);
    });
    ASSERT_TRUE(solution.has_value());
    EXPECT_TRUE(
        std::any_of(solution->objects.begin(), solution->objects.end(),
                    [](const auto& object) { return object.typeName == "glyph-duplication"; }));
    io::BinaryWriter::WriteSolutionFile(solutionPath, *solution);
    const auto report = io::VerifySolutionFiles(puzzlePath, solutionPath, 30000, 2000);
    ASSERT_TRUE(report.Passed()) << report.detail;
    EXPECT_EQ(report.metrics.cycles, solution->metrics.cycles);
}
TEST_F(AssemblyVerifierTest, ColoredMasksRebuildAndSelectivelyRetainBonds) {
    for (const unsigned inputMask : {0U, 1U, 2U, 4U, 6U, 8U, 10U, 12U, 14U}) {
        for (const unsigned outputMask : {1U, 2U, 4U, 6U, 8U, 10U, 12U, 14U}) {
            SCOPED_TRACE(testing::Message() << inputMask << " -> " << outputMask);
            auto p = Pair();
            p.permissionBits = (1ULL << 29) - 1;
            p.inputs = {{{{core::AtomType::Fire, {0, 0}}}, {}}};
            if (inputMask) {
                p.inputs[0].atoms.push_back({core::AtomType::Fire, {1, 0}});
                p.inputs[0].bonds.push_back({static_cast<std::uint8_t>(inputMask), {0, 0}, {1, 0}});
            }
            p.outputs = {{{{core::AtomType::Fire, {0, 0}}, {core::AtomType::Fire, {1, 0}}}, {}}};
            p.outputs[0].bonds = {{static_cast<std::uint8_t>(outputMask), {0, 0}, {1, 0}}};
            WriteGeneratedPuzzle(puzzlePath, p);
            std::string lastFailure;
            auto solution = solvers::CompileVerifiedSupplyInventory(
                p,
                [&](const auto& candidate) {
                    io::BinaryWriter::WriteSolutionFile(solutionPath, candidate);
                    return io::VerifySolutionFiles(puzzlePath, solutionPath, 30000, 2000);
                },
                [&](const auto& failure) { lastFailure = failure.detail; });
            ASSERT_TRUE(solution.has_value()) << lastFailure;
            io::BinaryWriter::WriteSolutionFile(solutionPath, *solution);
            const auto report = io::VerifySolutionFiles(puzzlePath, solutionPath, 30000, 2000);
            ASSERT_TRUE(report.Passed()) << report.detail;
            EXPECT_EQ(report.metrics.cycles, solution->metrics.cycles);
        }
    }
}
TEST_F(AssemblyVerifierTest, TriangularTriplexVerifiesIndependently) {
    auto p = Pair();
    p.permissionBits = (1ULL << 29) - 1;
    p.inputs = {{{{core::AtomType::Fire, {0, 0}}}, {}}};
    p.outputs = {{{{core::AtomType::Fire, {0, 0}},
                   {core::AtomType::Fire, {0, 1}},
                   {core::AtomType::Fire, {1, 0}}},
                  {{8, {0, 0}, {0, 1}}, {2, {0, 1}, {1, 0}}, {4, {1, 0}, {0, 0}}}}};
    for (const int feedMask : {0, 1, 2, 4, 6, 8, 10, 12, 14}) {
        SCOPED_TRACE(feedMask);
        if (feedMask == 0)
            p.inputs = {{{{core::AtomType::Fire, {0, 0}}}, {}}};
        else
            p.inputs = {{{{core::AtomType::Fire, {0, 0}},
                          {core::AtomType::Fire, {0, 1}},
                          {core::AtomType::Fire, {1, 0}}},
                         {{static_cast<std::uint8_t>(feedMask), {0, 0}, {0, 1}},
                          {static_cast<std::uint8_t>(feedMask), {0, 1}, {1, 0}},
                          {static_cast<std::uint8_t>(feedMask), {1, 0}, {0, 0}}}}};
        WriteGeneratedPuzzle(puzzlePath, p);
        std::string lastFailure;
        auto solution = solvers::CompileVerifiedSupplyInventory(
            p,
            [&](const auto& candidate) {
                io::BinaryWriter::WriteSolutionFile(solutionPath, candidate);
                return io::VerifySolutionFiles(puzzlePath, solutionPath, 30000, 2000);
            },
            [&](const auto& failure) { lastFailure = failure.detail; });
        ASSERT_TRUE(solution.has_value()) << lastFailure;
        io::BinaryWriter::WriteSolutionFile(solutionPath, *solution);
        const auto report = io::VerifySolutionFiles(puzzlePath, solutionPath, 30000, 2000);
        ASSERT_TRUE(report.Passed()) << report.detail;
        EXPECT_EQ(report.metrics.cycles, solution->metrics.cycles);
    }
}
TEST_F(AssemblyVerifierTest, NativeCycleLimitSelectsLaterLayout) {
    auto p = Pair();
    p.permissionBits = (1ULL << 29) - 1;
    for (auto& atom : p.outputs[0].atoms)
        atom.type = core::AtomType::Salt;
    p.inputs = {{{{core::AtomType::Salt, {0, 0}}}, {}}, p.outputs[0]};
    WriteGeneratedPuzzle(puzzlePath, p);
    std::vector<solvers::SupplyFailure> failures;
    bool firstCandidate = true;
    auto solution = solvers::CompileVerifiedSupplyInventory(
        p,
        [&](const auto& candidate) {
            io::BinaryWriter::WriteSolutionFile(solutionPath, candidate);
            const auto limit = firstCandidate ? 1 : 163;
            firstCandidate = false;
            return io::VerifySolutionFiles(puzzlePath, solutionPath, limit, 2000);
        },
        [&](const auto& failure) { failures.push_back(failure); });
    ASSERT_TRUE(solution.has_value());
    io::BinaryWriter::WriteSolutionFile(solutionPath, *solution);
    const auto published = io::VerifySolutionFiles(puzzlePath, solutionPath, 163, 2000);
    ASSERT_TRUE(published.Passed()) << published.detail;
    EXPECT_EQ(published.metrics.cycles, solution->metrics.cycles);
    EXPECT_TRUE(std::any_of(failures.begin(), failures.end(), [](const auto& failure) {
        return failure.category == solvers::SupplyFailureCategory::IndependentVerifier &&
               failure.verification.has_value();
    }));
}
TEST_F(AssemblyVerifierTest, AssemblyFeedbackRetainsNativeFailuresAndReplans) {
    auto p = Pair();
    p.permissionBits = (1ULL << 29) - 1;
    for (auto& atom : p.outputs[0].atoms)
        atom.type = core::AtomType::Air;
    p.inputs = {{{{core::AtomType::Salt, {0, 0}}}, {}},
                {{{core::AtomType::Air, {0, 0}}}, {}},
                p.outputs[0]};
    WriteGeneratedPuzzle(puzzlePath, p);
    std::vector<solvers::AssemblyFailure> failures;
    const auto solution = solvers::AssemblySolver{}.SolveVerified(
        p,
        [&](const auto& candidate) {
            io::BinaryWriter::WriteSolutionFile(solutionPath, candidate);
            return io::VerifySolutionFiles(puzzlePath, solutionPath, 300, 2000);
        },
        [&](const auto& failure) { failures.push_back(failure); });
    EXPECT_TRUE(std::any_of(failures.begin(), failures.end(), [](const auto& failure) {
        return failure.category == solvers::AssemblyFailure::Category::IndependentVerifier &&
               failure.verification &&
               failure.verification->status == io::VerificationStatus::CycleLimit;
    }));
    EXPECT_TRUE(std::any_of(failures.begin(), failures.end(), [](const auto& failure) {
        return failure.stage == "automatic-inventory" && failure.supply &&
               failure.supply->materialAttempt > 0 && failure.verification;
    }));
    io::BinaryWriter::WriteSolutionFile(solutionPath, solution);
    const auto report = io::VerifySolutionFiles(puzzlePath, solutionPath, 300, 2000);
    ASSERT_TRUE(report.Passed()) << report.detail;
    EXPECT_EQ(report.metrics.cycles, solution.metrics.cycles);
    EXPECT_THROW((void)solvers::AssemblySolver{}.SolveVerified(
                     p,
                     [&](const auto&) -> io::VerificationReport {
                         throw std::invalid_argument("verifier infrastructure failure");
                     },
                     {}),
                 std::invalid_argument);
    EXPECT_THROW((void)solvers::AssemblySolver{}.SolveVerified(
                     p,
                     [&](const auto& candidate) {
                         io::BinaryWriter::WriteSolutionFile(solutionPath, candidate);
                         return io::VerifySolutionFiles(puzzlePath, solutionPath, 300, 2000);
                     },
                     [&](const auto& failure) {
                         if (failure.supply)
                             throw std::invalid_argument("feedback infrastructure failure");
                     }),
                 std::invalid_argument);
}
TEST(AssemblyFeedback, UnavailableOracleIsNotMisreportedAsIndependentRejection) {
    const auto p = Pair();
    std::vector<solvers::AssemblyFailure> failures;
    EXPECT_THROW((void)solvers::AssemblySolver{}.SolveVerified(
                     p,
                     [](const auto&) -> io::VerificationReport {
                         return {
                             io::VerificationStatus::Unavailable, {}, "oracle rule unavailable"};
                     },
                     [&](const auto& failure) { failures.push_back(failure); }),
                 std::invalid_argument);
    EXPECT_TRUE(std::any_of(failures.begin(), failures.end(), [](const auto& failure) {
        return failure.category == solvers::AssemblyFailure::Category::VerificationUnavailable &&
               failure.verification &&
               failure.verification->status == io::VerificationStatus::Unavailable;
    }));
    EXPECT_FALSE(std::any_of(failures.begin(), failures.end(), [](const auto& failure) {
        return failure.category == solvers::AssemblyFailure::Category::IndependentVerifier &&
               failure.verification &&
               failure.verification->status == io::VerificationStatus::Unavailable;
    }));
}
TEST_F(AssemblyVerifierTest, NativeCycleLimitSelectsLaterMaterialHistory) {
    auto p = Pair();
    p.permissionBits = (1ULL << 29) - 1;
    for (auto& atom : p.outputs[0].atoms)
        atom.type = core::AtomType::Air;
    p.inputs = {{{{core::AtomType::Salt, {0, 0}}}, {}},
                {{{core::AtomType::Air, {0, 0}}}, {}},
                p.outputs[0]};
    WriteGeneratedPuzzle(puzzlePath, p);
    std::vector<solvers::SupplyFailure> failures;
    auto solution = solvers::CompileVerifiedSupplyInventory(
        p,
        [&](const auto& candidate) {
            io::BinaryWriter::WriteSolutionFile(solutionPath, candidate);
            return io::VerifySolutionFiles(puzzlePath, solutionPath, 300, 2000);
        },
        [&](const auto& failure) { failures.push_back(failure); });
    ASSERT_TRUE(solution.has_value()) << [&] {
        std::string detail;
        for (const auto& failure : failures)
            detail += std::to_string(failure.materialAttempt) + ": " + failure.detail + "\n";
        return detail;
    }();
    ASSERT_TRUE(std::any_of(failures.begin(), failures.end(), [](const auto& failure) {
        return failure.materialAttempt == 1 && failure.verification &&
               failure.verification->status == io::VerificationStatus::CycleLimit;
    }));
    ASSERT_TRUE(std::any_of(failures.begin(), failures.end(),
                            [](const auto& failure) { return failure.materialAttempt > 1; }));
    io::BinaryWriter::WriteSolutionFile(solutionPath, *solution);
    const auto published = io::VerifySolutionFiles(puzzlePath, solutionPath, 300, 2000);
    ASSERT_TRUE(published.Passed()) << published.detail;
    EXPECT_EQ(published.metrics.cycles, solution->metrics.cycles);
}
TEST_F(AssemblyVerifierTest, MultistagePurificationRetainsChemistryDuringReplanning) {
    auto p = Pair();
    p.permissionBits = (1ULL << 29) - 1;
    p.inputs = {
        {{{core::AtomType::Lead, {0, 0}}}, {}},
        {{{core::AtomType::Lead, {0, 0}}, {core::AtomType::Lead, {1, 0}}}, {{1, {0, 0}, {1, 0}}}}};
    p.outputs = {{{{core::AtomType::Iron, {0, 0}}}, {}}};
    WriteGeneratedPuzzle(puzzlePath, p);
    std::size_t attempt = 1;
    std::size_t acceptedAttempt = 0;
    auto solution = solvers::CompileVerifiedSupplyInventory(
        p,
        [&](const auto& candidate) {
            // Reject the initial material history to exercise upstream replanning.
            if (attempt == 1) return io::VerificationReport{};
            io::BinaryWriter::WriteSolutionFile(solutionPath, candidate);
            auto report = io::VerifySolutionFiles(puzzlePath, solutionPath, 30000, 2000);
            if (report.Passed()) acceptedAttempt = attempt;
            return report;
        },
        [&](const auto& failure) { attempt = failure.materialAttempt; });
    ASSERT_TRUE(solution.has_value());
    EXPECT_GT(acceptedAttempt, 1U);
    io::BinaryWriter::WriteSolutionFile(solutionPath, *solution);
    const auto report = io::VerifySolutionFiles(puzzlePath, solutionPath, 30000, 2000);
    ASSERT_TRUE(report.Passed()) << report.detail;
    EXPECT_EQ(report.metrics.cycles, solution->metrics.cycles);
}
TEST_F(AssemblyVerifierTest, NativeCycleLimitExhaustsMaterialHistories) {
    auto p = Pair();
    p.permissionBits = (1ULL << 29) - 1;
    for (auto& atom : p.outputs[0].atoms)
        atom.type = core::AtomType::Salt;
    io::Molecule fragment;
    fragment.atoms = {{core::AtomType::Salt, {0, 0}},
                      {core::AtomType::Salt, {1, 0}},
                      {core::AtomType::Salt, {0, 1}}};
    fragment.bonds = {{1, {0, 0}, {1, 0}}, {1, {0, 0}, {0, 1}}};
    p.inputs = {fragment, {{{core::AtomType::Air, {0, 0}}}, {}}};
    p.outputs[0] = {{{core::AtomType::Salt, {0, 0}}}, {}};
    WriteGeneratedPuzzle(puzzlePath, p);
    std::vector<solvers::SupplyFailure> failures;
    std::size_t lastAttempt = 1;
    auto solution = solvers::CompileVerifiedSupplyInventory(
        p,
        [&](const auto& candidate) {
            io::BinaryWriter::WriteSolutionFile(solutionPath, candidate);
            return io::VerifySolutionFiles(puzzlePath, solutionPath, 1, 2000);
        },
        [&](const auto& failure) {
            failures.push_back(failure);
            lastAttempt = failure.materialAttempt;
        });
    EXPECT_FALSE(solution.has_value());
    EXPECT_GT(lastAttempt, 1U);
    ASSERT_FALSE(failures.empty());
    const auto& terminal = failures.back();
    EXPECT_TRUE(terminal.category == solvers::SupplyFailureCategory::Budget ||
                terminal.category == solvers::SupplyFailureCategory::Material);
    EXPECT_EQ(terminal.rotation, -1);
    EXPECT_EQ(terminal.spacing, 0);
    EXPECT_NE(terminal.detail.find("automatic material replanning"), std::string::npos);
    EXPECT_FALSE(terminal.verification.has_value());
    EXPECT_TRUE(std::any_of(failures.begin(), failures.end(), [](const auto& failure) {
        return failure.category == solvers::SupplyFailureCategory::IndependentVerifier &&
               failure.materialAttempt == 1 && failure.verification &&
               failure.verification->status == io::VerificationStatus::CycleLimit;
    }));
}
TEST_F(AssemblyVerifierTest, RejectedCandidateFallsBackThroughVerifiedPublication) {
    auto p = Pair();
    p.inputs.push_back(p.outputs[0]);
    p.permissionBits = (1ULL << 28) - 1;
    WriteGeneratedPuzzle(puzzlePath, p);
    int attempts = 0;
    auto result = AssemblySolver{}.SolveAccepted(p, [&](const io::SolutionData& candidate) {
        auto submitted = candidate;
        if (++attempts == 1) {
            for (auto& object : submitted.objects) {
                if (object.typeName == "out-std") object.position = {100, 100};
            }
        }
        const auto report = io::PublishVerifiedSolution(puzzlePath, solutionPath, submitted);
        EXPECT_NE(report.status, io::VerificationStatus::Unavailable);
        EXPECT_NE(report.status, io::VerificationStatus::Error) << report.detail;
        if (attempts == 1) {
            EXPECT_FALSE(report.Passed());
            EXPECT_FALSE(std::filesystem::exists(solutionPath));
        }
        return report.Passed();
    });
    EXPECT_GE(attempts, 2);
    EXPECT_FALSE(result.objects.empty());
    EXPECT_TRUE(io::VerifySolutionFiles(puzzlePath, solutionPath).Passed());
}
TEST_F(AssemblyVerifierTest, MixedMolecularSurplusWithoutDisposal) {
    auto p = Generated({{0, 0}}, {});
    p.permissionBits |= (1ULL << 9) | (1ULL << 2) | (1ULL << 3);
    p.inputs = {
        {{{core::AtomType::Salt, {0, 0}}, {core::AtomType::Fire, {0, 1}}}, {{1, {0, 0}, {0, 1}}}}};
    EXPECT_EQ(p.permissionBits & (1ULL << 17), 0U);
    const auto solution = AssemblySolver{}.Solve(p);
    EXPECT_TRUE(
        std::none_of(solution.objects.begin(), solution.objects.end(),
                     [](const auto& object) { return object.typeName == "glyph-disposal"; }));
    VerifyRow(p);
}
TEST_F(AssemblyVerifierTest, MultipleSurplusAtomsWithoutDisposal) {
    auto p = Generated({{0, 0}}, {});
    p.permissionBits |= 1ULL << 9;
    p.outputMultiplier = 2;
    p.inputs = {{{{core::AtomType::Salt, {0, 0}},
                  {core::AtomType::Fire, {0, 1}},
                  {core::AtomType::Water, {0, 2}}},
                 {{1, {0, 0}, {0, 1}}, {1, {0, 1}, {0, 2}}}}};
    VerifyRow(p);
}
TEST_F(AssemblyVerifierTest, MixedMolecularDuplicationClosesInventory) {
    for (int cardinal = 2; cardinal <= 5; ++cardinal)
        for (int catalystPosition : {0, 1, 2}) {
            SCOPED_TRACE(cardinal);
            SCOPED_TRACE(catalystPosition);
            const auto element = static_cast<core::AtomType>(cardinal);
            auto p = Generated({{0, 0}, {1, 0}, {2, 0}, {3, 0}}, {{0, 1}, {1, 2}, {2, 3}});
            p.permissionBits |= (1ULL << 9) | (1ULL << 13);
            p.inputs = {{{{core::AtomType::Salt, {0, 0}},
                          {core::AtomType::Salt, {1, 0}},
                          {core::AtomType::Salt, {2, 0}}},
                         {{1, {0, 0}, {1, 0}}, {1, {1, 0}, {2, 0}}}}};
            p.inputs[0].atoms[catalystPosition].type = element;
            for (auto& atom : p.outputs[0].atoms)
                atom.type = element;
            p.outputs[0].atoms[0].type = core::AtomType::Salt;
            const auto plan = AssemblyPlanner::PlanSupply(p);
            EXPECT_EQ(plan.productsPerPeriod, 3U);
            EXPECT_EQ(plan.usage[0].batches, 4U);
            EXPECT_EQ(plan.usage[0].acquired, 12U);
            EXPECT_EQ(std::count_if(plan.steps.begin(), plan.steps.end(),
                                    [](const auto& step) { return step.buffer == -2; }),
                      1);
            VerifyRow(p);
        }
}
TEST_F(AssemblyVerifierTest, MixedDuplicationReusesCatalystAcrossProducts) {
    auto p = Generated({{0, 0}, {1, 0}, {2, 0}, {3, 0}}, {{0, 1}, {1, 2}, {2, 3}});
    p.permissionBits |= (1ULL << 9) | (1ULL << 13);
    io::Molecule molecule;
    for (int q = 0; q < 6; ++q) {
        molecule.atoms.push_back({q == 0 ? core::AtomType::Fire : core::AtomType::Salt, {q, 0}});
        if (q) molecule.bonds.push_back({1, {q - 1, 0}, {q, 0}});
    }
    p.inputs = {molecule};
    for (auto& atom : p.outputs[0].atoms)
        atom.type = core::AtomType::Fire;
    p.outputs[0].atoms[0].type = core::AtomType::Salt;
    const auto plan = AssemblyPlanner::PlanSupply(p);
    EXPECT_EQ(plan.productsPerPeriod, 3U);
    EXPECT_EQ(plan.usage[0].batches, 2U);
    EXPECT_EQ(std::count_if(plan.steps.begin(), plan.steps.end(),
                            [](const auto& step) { return step.buffer == -2; }),
              1);
    VerifyRow(p);
}
#endif
} // namespace

TEST(AssemblyPlanTest, ChainBendBranchAndRingReplay) {
    for (const auto& p : {Generated({{0, 0}, {1, 0}, {2, 0}, {3, 0}}, {{0, 1}, {1, 2}, {2, 3}}),
                          Generated({{0, 0}, {1, 0}, {1, 1}, {0, 2}}, {{0, 1}, {1, 2}, {2, 3}}),
                          Generated({{0, 0}, {1, 0}, {0, 1}, {-1, 0}}, {{0, 1}, {0, 2}, {0, 3}}),
                          Generated({{1, 0}, {0, 1}, {-1, 1}, {-1, 0}, {0, -1}, {1, -1}},
                                    {{0, 1}, {1, 2}, {2, 3}, {3, 4}, {4, 5}, {0, 5}})}) {
        const auto plan = AssemblyPlanner::Plan(p);
        Replay(p, plan);
        EXPECT_EQ(plan, AssemblyPlanner::Plan(p));
        EXPECT_EQ(plan.operations.size(),
                  p.outputs[0].atoms.size() + p.outputs[0].bonds.size() + 1);
        EXPECT_FALSE(AssemblySolver::Supports(p));
        EXPECT_THROW((void)AssemblySolver::Compile(p, plan), std::invalid_argument);
    }
}

TEST(AssemblyPlanTest, RenewableFeedsAndBounds) {
    auto p = Generated({{0, 0}, {1, 0}, {2, 0}}, {{0, 1}, {1, 2}});
    for (auto& a : p.outputs[0].atoms)
        a.type = core::AtomType::Salt;
    p.inputs.resize(1);
    Replay(p, AssemblyPlanner::Plan(p));
    std::vector<core::HexCoord> cells;
    std::vector<AssemblyPlan::Edge> edges;
    for (std::size_t i = 0; i < AssemblyPlanner::MaxAtoms; ++i) {
        cells.push_back({static_cast<int>(i) - 64, 0});
        if (i) edges.push_back({i - 1, i});
    }
    p = Generated(cells, edges);
    Replay(p, AssemblyPlanner::Plan(p));
    p = Pair();
    p.outputs[0].atoms.resize(AssemblyPlanner::MaxAtoms + 1);
    EXPECT_THROW((void)AssemblyPlanner::Plan(p), std::invalid_argument);
    p = Pair();
    p.inputs.resize(AssemblyPlanner::MaxAtoms + 1, p.inputs[0]);
    EXPECT_THROW((void)AssemblyPlanner::Plan(p), std::invalid_argument);
}

TEST(AssemblyPlanTest, MalformedAndUnsupportedInputsRejected) {
    const auto reject = [](const io::PuzzleData& p) {
        EXPECT_THROW((void)AssemblyPlanner::Plan(p), std::invalid_argument);
        EXPECT_FALSE(AssemblySolver::Supports(p));
        EXPECT_THROW((void)AssemblySolver{}.Solve(p), std::invalid_argument);
    };
    auto p = Pair();
    p.version = 2;
    reject(p);
    p = Pair();
    p.productionInfo.emplace();
    reject(p);
    p = Pair();
    p.inputs.clear();
    reject(p);
    p = Pair();
    p.outputs.clear();
    reject(p);
    p = Pair();
    p.outputs.push_back(p.outputs[0]);
    EXPECT_THROW((void)AssemblyPlanner::Plan(p), std::invalid_argument);
    EXPECT_TRUE(AssemblySolver::Supports(p));
    p = Pair();
    p.outputs[0].atoms.clear();
    reject(p);
    p = Pair();
    p.inputs[0].atoms.clear();
    reject(p);
    p = Pair();
    p.inputs[0].atoms.push_back(p.inputs[0].atoms[0]);
    reject(p);
    p = Pair();
    p.inputs[0].bonds = p.outputs[0].bonds;
    reject(p);
    p = Pair();
    p.outputs[0].atoms[1].position = {0, 0};
    reject(p);
    p = Pair();
    p.outputs[0].bonds.clear();
    reject(p);
    p = Pair();
    p.outputs[0].bonds[0].from = {5, 5};
    reject(p);
    p = Pair();
    p.outputs[0].bonds[0].to = {0, 0};
    reject(p);
    p = Pair();
    p.outputs[0].atoms[1].position = {2, 0};
    p.outputs[0].bonds[0].to = {2, 0};
    reject(p);
    p = Pair();
    p.outputs[0].bonds.push_back({1, {1, 0}, {0, 0}});
    reject(p);
    for (int bits : {0, 2, 4, 8, 14, 15, 255}) {
        p = Pair();
        p.outputs[0].bonds[0].bits = bits;
        reject(p);
    }
    for (auto type : {core::AtomType::Repeat, static_cast<core::AtomType>(0),
                      static_cast<core::AtomType>(255)}) {
        p = Pair();
        p.inputs[0].atoms[0].type = type;
        reject(p);
        p = Pair();
        p.outputs[0].atoms[0].type = type;
        reject(p);
    }
    p = Pair();
    p.outputs[0].atoms[0].type = core::AtomType::Gold;
    reject(p);
    for (int bit : {0, 8, 22, 23}) {
        p = Pair();
        p.permissionBits &= ~(1ULL << bit);
        reject(p);
    }
    for (int value : {0, -1, std::numeric_limits<int>::max()}) {
        p = Pair();
        p.outputMultiplier = value;
        reject(p);
    }
    p = Pair();
    p.inputs[0].atoms[0].position.q = std::numeric_limits<int>::min();
    reject(p);
    p = Pair();
    p.outputs[0].atoms[0].position.r = std::numeric_limits<int>::max();
    reject(p);
}

TEST(AssemblySolverTest, RegistersAndConsumesValidatedIR) {
    ASSERT_NE(solvers::BaseSolver::Create("assembly"), nullptr);
    ASSERT_NE(solvers::BaseSolver::Create("AssemblySolver"), nullptr);
    auto p = Pair();
    const auto plan = AssemblyPlanner::Plan(p);
    Replay(p, plan);
    EXPECT_EQ(AssemblySolver::Compile(p, plan).objects.size(), 5U);
    EXPECT_EQ(AssemblySolver{}.EstimateCycles(p), 23);
    for (int mutation = 0; mutation < 7; ++mutation) {
        auto bad = plan;
        switch (mutation) {
        case 0:
            bad.operations.pop_back();
            break;
        case 1:
            bad.operations[0].input = 1;
            break;
        case 2:
            bad.operations[2].preconditions.atoms.clear();
            break;
        case 3:
            bad.operations[2].effects.bonds.clear();
            break;
        case 4:
            bad.operations[1].occupied.clear();
            break;
        case 5:
            bad.operations[0].reservations.clear();
            break;
        case 6:
            std::swap(bad.operations[0], bad.operations[1]);
            break;
        }
        EXPECT_THROW(AssemblyPlanner::Validate(p, bad), std::invalid_argument);
        EXPECT_THROW((void)AssemblySolver::Compile(p, bad), std::invalid_argument);
    }
    p.inputs.push_back(p.inputs[0]);
    EXPECT_FALSE(AssemblySolver::Supports(p));
    EXPECT_THROW((void)AssemblySolver{}.Solve(p), std::invalid_argument);
}

TEST(AssemblySolverTest, RenewableFeedBoundariesAndCanonicalReplay) {
    const auto shared = [](io::PuzzleData p) {
        p.inputs.resize(1);
        for (auto& atom : p.outputs[0].atoms)
            atom.type = core::AtomType::Salt;
        return p;
    };
    for (const auto& p :
         {shared(Pair()), shared(Generated({{1, 0}, {0, 1}, {-1, 1}}, {{0, 1}, {1, 2}}))}) {
        ASSERT_TRUE(AssemblySolver::Supports(p));
        const auto plan = AssemblyPlanner::Plan(p);
        const auto solution = AssemblySolver::Compile(p, plan);
        const bool tri = p.outputs[0].atoms.size() == 3;
        EXPECT_EQ(solution.objects.size(), 4U);
        EXPECT_EQ(solution.metrics.cycles, tri ? 34 : 23);
        EXPECT_EQ(solution.metrics.cost, 30);
        EXPECT_EQ(solution.metrics.area, tri ? 4 : 3);
        EXPECT_EQ(solution.metrics.instructionCount, tri ? 6 : 4);
        EXPECT_EQ(std::count_if(solution.objects.begin(), solution.objects.end(),
                                [](const auto& obj) { return obj.typeName == "input"; }),
                  1);
        for (std::size_t i = 0; i < plan.operations.size(); ++i) {
            auto bad = plan;
            bad.operations.erase(bad.operations.begin() + i);
            EXPECT_THROW((void)AssemblySolver::Compile(p, bad), std::invalid_argument);
        }
    }
    const auto reject = [](const io::PuzzleData& p) {
        EXPECT_FALSE(AssemblySolver::Supports(p));
        EXPECT_THROW((void)AssemblySolver{}.Solve(p), std::invalid_argument);
    };
    for (const auto& p :
         {shared(Generated({{0, 0}, {1, 0}, {2, 0}}, {{0, 1}, {1, 2}})),
          shared(Generated({{0, 0}, {1, 0}, {0, 1}}, {{0, 1}, {1, 2}})),
          shared(Generated({{0, 0}, {1, 0}, {0, 1}}, {{0, 1}, {1, 2}, {0, 2}})),
          shared(Generated({{0, 0}, {1, 0}, {2, 0}, {3, 0}}, {{0, 1}, {1, 2}, {2, 3}})),
          shared(Generated({{0, 0}, {1, 0}, {0, 1}, {-1, 0}}, {{0, 1}, {0, 2}, {0, 3}}))}) {
        Replay(p, AssemblyPlanner::Plan(p));
        reject(p);
    }
    auto bend = shared(Generated({{1, 0}, {0, 1}, {-1, 1}}, {{0, 1}, {1, 2}}));
    bend.inputs.push_back(bend.inputs[0]);
    reject(bend);
    bend.inputs.resize(1);
    bend.outputMultiplier = std::numeric_limits<int>::max() / 36 + 1;
    reject(bend);
    auto pair = shared(Pair());
    pair.outputs[0].atoms[1].type = core::AtomType::Air;
    reject(pair);
    for (int bit : {0, 8, 22, 23}) {
        pair = shared(Pair());
        pair.permissionBits &= ~(1ULL << bit);
        reject(pair);
    }
}

TEST(AssemblySolverTest, ThreeAtomBoundariesAndCanonicalReplay) {
    const auto chain = [] { return Generated({{0, 0}, {1, 0}, {2, 0}}, {{0, 1}, {1, 2}}); };
    auto p = chain();
    ASSERT_TRUE(AssemblySolver::Supports(p));
    const auto solution = AssemblySolver{}.Solve(p);
    EXPECT_EQ(solution.objects.size(), 7U);
    EXPECT_EQ(solution.metrics.cycles, 34);
    EXPECT_EQ(solution.metrics.cost, 40);
    EXPECT_EQ(solution.metrics.area, 40);
    EXPECT_EQ(solution.metrics.instructionCount, 6);
    for (std::size_t i = 0; i < 6; ++i) {
        auto plan = AssemblyPlanner::Plan(p);
        plan.operations.erase(plan.operations.begin() + i);
        EXPECT_THROW((void)AssemblySolver::Compile(p, plan), std::invalid_argument);
    }
    const auto reject = [](const io::PuzzleData& puzzle) {
        EXPECT_FALSE(AssemblySolver::Supports(puzzle));
        EXPECT_THROW((void)AssemblySolver{}.Solve(puzzle), std::invalid_argument);
    };
    p = Generated({{0, 0}, {1, 0}, {0, 1}}, {{0, 1}, {1, 2}, {0, 2}});
    Replay(p, AssemblyPlanner::Plan(p));
    reject(p);
    p = chain();
    p.inputs.push_back(p.inputs[0]);
    reject(p);
    p = chain();
    for (auto& atom : p.outputs[0].atoms)
        atom.type = core::AtomType::Salt;
    p.inputs.resize(1);
    Replay(p, AssemblyPlanner::Plan(p));
    reject(p);
    p.inputs.push_back(p.inputs[0]);
    Replay(p, AssemblyPlanner::Plan(p));
    reject(p);
    p = chain();
    p.outputMultiplier = std::numeric_limits<int>::max() / 36 + 1;
    Replay(p, AssemblyPlanner::Plan(p));
    reject(p);
}

#ifdef HAS_LIBVERIFY
TEST_F(AssemblyVerifierTest, RenewableFeedPairBendAndChain) {
    for (int shape : {0, 1, 2}) {
        const bool tri = shape != 0;
        for (int type = 1; type <= 16; ++type) {
            if (type == 15) continue;
            for (int turns = 0; turns < 6; ++turns) {
                std::vector<int> order = tri ? std::vector<int>{0, 1, 2} : std::vector<int>{0, 1};
                do {
                    auto p = shape == 2 ? Generated({{0, 0}, {1, 0}, {2, 0}}, {{0, 1}, {1, 2}})
                             : tri      ? Generated({{0, 1}, {1, 0}, {2, 0}}, {{0, 1}, {1, 2}})
                                        : Pair();
                    if (shape == 2) p.permissionBits |= 1ULL << 3;
                    for (auto& atom : p.outputs[0].atoms)
                        atom.type = static_cast<core::AtomType>(type);
                    p.inputs.resize(1);
                    p.inputs[0].atoms[0] = {static_cast<core::AtomType>(type), {7, -11}};
                    const auto transform = [&](core::HexCoord cell) {
                        for (int i = 0; i < turns; ++i)
                            cell = core::RotateClockwise(cell);
                        return core::Add(cell, {-9, 13});
                    };
                    const auto original = p.outputs[0].atoms;
                    for (std::size_t i = 0; i < order.size(); ++i) {
                        p.outputs[0].atoms[i] = original[order[i]];
                        p.outputs[0].atoms[i].position = transform(p.outputs[0].atoms[i].position);
                    }
                    for (auto& bond : p.outputs[0].bonds) {
                        const auto from = transform(bond.from);
                        bond.from = transform(bond.to);
                        bond.to = from;
                    }
                    std::reverse(p.outputs[0].bonds.begin(), p.outputs[0].bonds.end());
                    p.outputMultiplier = 1 + (type + turns) % 3;
                    SCOPED_TRACE("tri=" + std::to_string(tri) + " type=" + std::to_string(type) +
                                 " turns=" + std::to_string(turns) +
                                 " first=" + std::to_string(order[0]));
                    ASSERT_TRUE(AssemblySolver::Supports(p));
                    Replay(p, AssemblyPlanner::Plan(p));
                    Verify(p);
                } while (std::next_permutation(order.begin(), order.end()));
            }
        }
    }
}

TEST_F(AssemblyVerifierTest, MixedRenewableFeed) {
    auto p = Generated({{-1, 1}, {0, 1}, {-1, 0}}, {{0, 1}, {0, 2}});
    p.inputs.resize(2);
    p.permissionBits |= 1ULL << 3;
    p.outputs[0].atoms[0].type = p.outputs[0].atoms[1].type = p.inputs[0].atoms[0].type;
    p.outputs[0].atoms[2].type = p.inputs[1].atoms[0].type;
    for (int turns = 0; turns < 6; ++turns) {
        for (bool swapFeeds : {false, true}) {
            auto transformed = p;
            transformed.outputMultiplier = 1 + turns % 3;
            transformed.inputs[0].atoms[0].position = {7, -11};
            transformed.inputs[1].atoms[0].position = {-4, 6};
            if (swapFeeds) std::swap(transformed.inputs[0], transformed.inputs[1]);
            const auto transform = [&](core::HexCoord cell) {
                for (int i = 0; i < turns; ++i)
                    cell = core::RotateClockwise(cell);
                return core::Add(cell, {-9, 13});
            };
            for (auto& atom : transformed.outputs[0].atoms)
                atom.position = transform(atom.position);
            for (auto& bond : transformed.outputs[0].bonds) {
                const auto from = transform(bond.from);
                bond.from = transform(bond.to);
                bond.to = from;
            }
            std::reverse(transformed.outputs[0].atoms.begin(), transformed.outputs[0].atoms.end());
            std::reverse(transformed.outputs[0].bonds.begin(), transformed.outputs[0].bonds.end());
            Replay(transformed, AssemblyPlanner::Plan(transformed));
            Verify(transformed);
        }
    }
    WriteGeneratedPuzzle(puzzlePath, p);
    auto solution = AssemblySolver{}.Solve(p);
    solution.objects[0].instructions[2].action = 'G';
    io::BinaryWriter::WriteSolutionFile(solutionPath, solution);
    EXPECT_FALSE(io::VerifySolutionFiles(puzzlePath, solutionPath, 200, 2000).Passed());
    p.permissionBits &= ~(1ULL << 3);
    EXPECT_FALSE(AssemblySolver::Supports(p));
    p.permissionBits |= 1ULL << 3;
    p.outputMultiplier = std::numeric_limits<int>::max() / 48 + 1;
    EXPECT_FALSE(AssemblySolver::Supports(p));
    p.outputMultiplier = 1;
    p.outputs[0].atoms[2].position = {-2, 1};
    p.outputs[0].bonds[1].to = {-2, 1};
    EXPECT_FALSE(AssemblySolver::Supports(p));
}

TEST_F(AssemblyVerifierTest, ParkedFragmentRegrab) {
    auto p = Generated({{0, 0}, {1, 0}, {2, 0}, {3, 0}}, {{0, 1}, {1, 2}, {2, 3}});
    for (auto& atom : p.outputs[0].atoms)
        atom.type = core::AtomType::Salt;
    p.inputs.resize(1);
    p.permissionBits |= 1ULL << 3;
    for (int turns = 0; turns < 6; ++turns) {
        for (int type : {1, 2, 16}) {
            auto transformed = p;
            transformed.outputMultiplier = 1 + turns % 3;
            transformed.inputs[0].atoms[0] = {static_cast<core::AtomType>(type), {7, -11}};
            const auto transform = [&](core::HexCoord cell) {
                for (int i = 0; i < turns; ++i)
                    cell = core::RotateClockwise(cell);
                return core::Add(cell, {-9, 13});
            };
            for (auto& atom : transformed.outputs[0].atoms) {
                atom.type = static_cast<core::AtomType>(type);
                atom.position = transform(atom.position);
            }
            for (auto& bond : transformed.outputs[0].bonds) {
                const auto from = transform(bond.from);
                bond.from = transform(bond.to);
                bond.to = from;
            }
            std::reverse(transformed.outputs[0].atoms.begin(), transformed.outputs[0].atoms.end());
            Verify(transformed);
        }
    }
    WriteGeneratedPuzzle(puzzlePath, p);
    auto solution = AssemblySolver::Compile(p, AssemblyPlanner::Plan(p));
    EXPECT_EQ(solution.objects[0].instructions[3].action, 'g');
    EXPECT_EQ(solution.objects[0].instructions[4].action, 'a');
    EXPECT_EQ(solution.objects[0].instructions[5].action, 'A');
    EXPECT_EQ(solution.objects[0].instructions[6].action, 'G');
    solution.objects[0].instructions[6].action = 'g';
    io::BinaryWriter::WriteSolutionFile(solutionPath, solution);
    EXPECT_FALSE(io::VerifySolutionFiles(puzzlePath, solutionPath, 200, 2000).Passed());
    p.permissionBits &= ~(1ULL << 3);
    EXPECT_FALSE(AssemblySolver::Supports(p));
    p.permissionBits |= 1ULL << 3;
    p.outputMultiplier = std::numeric_limits<int>::max();
    EXPECT_FALSE(AssemblySolver::Supports(p));
    p.outputMultiplier = 1;
    p.outputs[0].atoms[3].position = {2, 1};
    p.outputs[0].bonds[2].to = {2, 1};
    EXPECT_THROW((void)AssemblySolver::Compile(p, AssemblyPlanner::Plan(p)), std::invalid_argument);
}

TEST_F(AssemblyVerifierTest, RenewableFeedChainRequiresTrackAndTranslation) {
    auto p = Generated({{0, 0}, {1, 0}, {2, 0}}, {{0, 1}, {1, 2}});
    for (auto& atom : p.outputs[0].atoms)
        atom.type = core::AtomType::Salt;
    p.inputs.resize(1);
    EXPECT_FALSE(AssemblySolver::Supports(p));
    p.permissionBits |= 1ULL << 3;
    const auto plan = AssemblyPlanner::Plan(p);
    Replay(p, plan);
    for (std::size_t i = 0; i < plan.operations.size(); ++i) {
        auto bad = plan;
        bad.operations.erase(bad.operations.begin() + i);
        EXPECT_THROW((void)AssemblySolver::Compile(p, bad), std::invalid_argument);
    }
    Verify(p);
    WriteGeneratedPuzzle(puzzlePath, p);
    for (bool removeTrack : {false, true}) {
        auto solution = AssemblySolver::Compile(p, plan);
        if (removeTrack) {
            solution.objects.erase(solution.objects.begin() + 1);
        } else {
            for (auto& instruction : solution.objects[0].instructions) {
                if (instruction.action == 'A') instruction.action = 'G';
            }
        }
        io::BinaryWriter::WriteSolutionFile(solutionPath, solution);
        EXPECT_FALSE(io::VerifySolutionFiles(puzzlePath, solutionPath, 100, 2000).Passed());
    }
    p.outputMultiplier = std::numeric_limits<int>::max() / 36 + 1;
    EXPECT_THROW((void)AssemblySolver::Compile(p, AssemblyPlanner::Plan(p)), std::invalid_argument);
}

TEST_F(AssemblyVerifierTest, RenewableFeedRequiresRespawnAndBonder) {
    auto p = Pair();
    p.inputs.resize(1);
    p.outputs[0].atoms[1].type = core::AtomType::Salt;
    WriteGeneratedPuzzle(puzzlePath, p);
    for (bool removeBonder : {false, true}) {
        auto solution = AssemblySolver{}.Solve(p);
        if (removeBonder)
            solution.objects.erase(solution.objects.begin() + 2);
        else
            solution.objects[0].instructions[1].action = 'G';
        io::BinaryWriter::WriteSolutionFile(solutionPath, solution);
        EXPECT_FALSE(io::VerifySolutionFiles(puzzlePath, solutionPath, 100, 2000).Passed());
    }
}

TEST_F(AssemblyVerifierTest, MissingSecondBondIsRejectedIndependently) {
    const auto p = Generated({{0, 0}, {1, 0}, {1, 1}}, {{0, 1}, {1, 2}});
    WriteGeneratedPuzzle(puzzlePath, p);
    auto solution = AssemblySolver{}.Solve(p);
    ASSERT_EQ(solution.objects[5].typeName, "bonder");
    solution.objects.erase(solution.objects.begin() + 5);
    io::BinaryWriter::WriteSolutionFile(solutionPath, solution);
    EXPECT_FALSE(io::VerifySolutionFiles(puzzlePath, solutionPath, 100, 2000).Passed());
}

TEST_F(AssemblyVerifierTest, ThreeAtomGeometry) {
    for (const auto cell : std::vector<core::HexCoord>{
             {2, 0}, {1, 1}, {0, 1}, {1, -1}, {2, -1}, {-1, 0}, {-1, 1}, {0, -1}}) {
        for (int attachment = 0; attachment < 2; ++attachment) {
            if (core::Distance(cell, {attachment, 0}) != 1) continue;
            for (int turns = 0; turns < 6; ++turns) {
                std::vector<int> order{0, 1, 2};
                do {
                    for (bool repeated : {false, true}) {
                        auto p = Generated({{0, 0}, {1, 0}, cell},
                                           {{0, 1}, {static_cast<std::size_t>(attachment), 2}});
                        if (repeated) {
                            for (auto& input : p.inputs)
                                input.atoms[0].type = core::AtomType::Salt;
                            for (auto& atom : p.outputs[0].atoms)
                                atom.type = core::AtomType::Salt;
                        }
                        const auto original = p.outputs[0].atoms;
                        for (int i = 0; i < 3; ++i)
                            p.outputs[0].atoms[i] = original[order[i]];
                        const auto transform = [&](core::HexCoord v) {
                            for (int i = 0; i < turns; ++i)
                                v = core::RotateClockwise(v);
                            return core::Add(v, {-7, 11});
                        };
                        for (auto& atom : p.outputs[0].atoms)
                            atom.position = transform(atom.position);
                        for (auto& bond : p.outputs[0].bonds) {
                            const auto from = transform(bond.from);
                            bond.from = transform(bond.to);
                            bond.to = from;
                        }
                        std::reverse(p.outputs[0].bonds.begin(), p.outputs[0].bonds.end());
                        std::reverse(p.inputs.begin(), p.inputs.end());
                        for (int i = 0; i < 3; ++i)
                            p.inputs[i].atoms[0].position = {i + 7, -i - 4};
                        p.outputMultiplier = turns % 3 + 1;
                        SCOPED_TRACE(std::to_string(cell.q) + "," + std::to_string(cell.r) +
                                     " attachment=" + std::to_string(attachment) + " turns=" +
                                     std::to_string(turns) + " order=" + std::to_string(order[0]) +
                                     std::to_string(order[1]) + std::to_string(order[2]) +
                                     " repeated=" + std::to_string(repeated));
                        ASSERT_TRUE(AssemblySolver::Supports(p));
                        Replay(p, AssemblyPlanner::Plan(p));
                        Verify(p);
                    }
                } while (std::next_permutation(order.begin(), order.end()));
            }
        }
    }
}

TEST_F(AssemblyVerifierTest, CanonicalMachine) {
    Verify(Pair());
}

TEST_F(AssemblyVerifierTest, ElementCombinationsAndMultipliers) {
    for (int a = 1; a <= 16; ++a) {
        if (a == 15) continue;
        for (int b = 1; b <= 16; ++b) {
            if (b == 15) continue;
            auto p = Pair();
            p.inputs[0].atoms[0].type = p.outputs[0].atoms[0].type = static_cast<core::AtomType>(a);
            p.inputs[1].atoms[0].type = p.outputs[0].atoms[1].type = static_cast<core::AtomType>(b);
            p.outputMultiplier = 1 + (a + b) % 3;
            SCOPED_TRACE(std::to_string(a) + "," + std::to_string(b));
            Verify(p);
        }
    }
}

TEST_F(AssemblyVerifierTest, RotationsTranslationsAndOrder) {
    for (int turns = 0; turns < 6; ++turns) {
        for (int order = 0; order < 8; ++order) {
            auto p = Pair();
            auto transform = [&](core::HexCoord cell) {
                for (int i = 0; i < turns; ++i)
                    cell = core::RotateClockwise(cell);
                return core::Add(cell, {-9, 6});
            };
            for (auto& a : p.outputs[0].atoms)
                a.position = transform(a.position);
            for (auto& b : p.outputs[0].bonds) {
                b.from = transform(b.from);
                b.to = transform(b.to);
            }
            p.inputs[0].atoms[0].position = {7, -4};
            p.inputs[1].atoms[0].position = {-5, 12};
            if (order & 1) std::swap(p.inputs[0], p.inputs[1]);
            if (order & 2) std::swap(p.outputs[0].atoms[0], p.outputs[0].atoms[1]);
            if (order & 4) std::swap(p.outputs[0].bonds[0].from, p.outputs[0].bonds[0].to);
            p.outputMultiplier = 2;
            SCOPED_TRACE(std::to_string(turns) + "," + std::to_string(order));
            Verify(p);
        }
    }
}
#endif

#ifdef HAS_LIBVERIFY
TEST_F(AssemblyVerifierTest, IndependentOutputs) {
    auto p = Generated({{0, 0}, {1, 0}, {0, 1}}, {{0, 1}, {1, 2}, {2, 0}});
    p.outputs.push_back(p.outputs.front());
    p.outputs[1].atoms.resize(2);
    p.outputs[1].bonds.resize(1);
    VerifyRow(p);
    p.outputMultiplier = 2;
    p.outputs.push_back(p.outputs.front());
    VerifyRow(p);
}
TEST_F(AssemblyVerifierTest, RowEmptyResetRetiming) {
    for (int mutation = 0; mutation < 3; ++mutation) {
        auto p =
            Generated({{0, 0}, {1, 0}, {2, 0}, {0, 1}, {2, 1}}, {{0, 1}, {1, 2}, {0, 3}, {2, 4}});
        for (auto& input : p.inputs)
            input.atoms[0].type = core::AtomType::Salt;
        for (auto& atom : p.outputs[0].atoms) {
            atom.type = core::AtomType::Salt;
            if (mutation == 1) atom.position = core::Add(atom.position, {4, -3});
        }
        if (mutation == 1)
            for (auto& bond : p.outputs[0].bonds) {
                bond.from = core::Add(bond.from, {4, -3});
                bond.to = core::Add(bond.to, {4, -3});
            }
        if (mutation == 2) std::reverse(p.inputs.begin(), p.inputs.end());
        p.permissionBits |= (1ULL << 2) | (1ULL << 3);
        const auto plan = AssemblyPlanner::Plan(p);
        // Empty-reset retiming is only observable on the serialized emitter: with
        // the row clocks on by default the pipeline already removes that slack, so
        // both machines would agree and the equivalence would be vacuous.
        io::SolutionData parent;
        io::SolutionData retimed;
        {
            ScopedRowClocks clocks("off");
            parent = solvers::CompileRowAssembly(p, plan);
            retimed = solvers::CompileRetimedRowAssembly(p, plan);
        }
        EXPECT_LT(retimed.metrics.cycles, parent.metrics.cycles);
        EXPECT_EQ(retimed.objects.size(), parent.objects.size());
        // This test isolates retiming equivalence; the compact scheduled
        // layout is covered by CompactLayout.* and would change the geometry.
        ScopedCompactLayout compactOff("off");
        io::SolutionData scheduled;
        {
            ScopedRowClocks clocks("off");
            scheduled = solvers::CompileScheduledRowAssembly(
                p, plan, std::chrono::steady_clock::now() + std::chrono::seconds(3));
        }
        EXPECT_EQ(io::BinaryWriter::WriteSolution(scheduled),
                  io::BinaryWriter::WriteSolution(retimed));
        WriteGeneratedPuzzle(puzzlePath, p);
        io::BinaryWriter::WriteSolutionFile(solutionPath, parent);
        const auto parentReport = io::VerifySolutionFiles(puzzlePath, solutionPath, 30000, 2000);
        ASSERT_TRUE(parentReport.Passed()) << parentReport.detail;
        EXPECT_EQ(parentReport.authority, io::VerificationAuthority::LegacyGate);
        io::BinaryWriter::WriteSolutionFile(solutionPath, retimed);
        const auto report = io::VerifySolutionFiles(puzzlePath, solutionPath, 30000, 2000);
        ASSERT_TRUE(report.Passed()) << report.detail << " cycle=" << report.errorCycle;
        EXPECT_EQ(report.authority, io::VerificationAuthority::LegacyGate);
        EXPECT_EQ(report.metrics.cost, parentReport.metrics.cost);
        EXPECT_LE(report.metrics.area, parentReport.metrics.area);
        EXPECT_LT(report.metrics.cycles, parentReport.metrics.cycles);
        auto forbidden = p;
        forbidden.permissionBits &= ~(1ULL << 2);
        EXPECT_THROW(solvers::CompileRetimedRowAssembly(forbidden, plan), std::invalid_argument);
    }
}

TEST_F(AssemblyVerifierTest, RowDemandBoundedEscape) {
    for (int family = 0; family < 4; ++family) {
        for (int mutation = 0; mutation < 4; ++mutation) {
            auto p = family == 1 ? Generated({{0, 0}, {1, 0}, {2, 0}, {0, 1}, {2, 1}},
                                             {{0, 1}, {1, 2}, {0, 3}, {2, 4}})
                                 : Generated({{0, 0}, {1, 0}}, {{0, 1}});
            if (family == 2) {
                p = Generated({{0, 0}, {1, 0}, {2, 0}, {3, 0}, {4, 0}, {5, 0}, {6, 0}},
                              {{0, 1}, {1, 2}, {2, 3}, {3, 4}, {4, 5}, {5, 6}});
                p.inputs.resize(1);
                p.inputs[0].atoms = {
                    {core::AtomType::Salt, {0, 0}},  {core::AtomType::Salt, {1, 0}},
                    {core::AtomType::Salt, {0, 1}},  {core::AtomType::Salt, {-1, 1}},
                    {core::AtomType::Salt, {-1, 0}}, {core::AtomType::Salt, {0, -1}},
                    {core::AtomType::Salt, {1, -1}}};
                p.inputs[0].bonds.clear();
                for (std::size_t i = 1; i < p.inputs[0].atoms.size(); ++i)
                    p.inputs[0].bonds.push_back({1, {0, 0}, p.inputs[0].atoms[i].position});
            }
            if (family == 3)
                p = Generated({{0, 0}, {1, 0}, {2, 0}, {3, 0}, {4, 0}},
                              {{0, 1}, {1, 2}, {2, 3}, {3, 4}});
            for (auto& input : p.inputs)
                for (auto& atom : input.atoms)
                    atom.type = core::AtomType::Salt;
            auto transform = [&](core::HexCoord c) {
                if (mutation == 1) return core::Add(c, {4, -3});
                if (mutation == 2) return core::RotateClockwise(c);
                return c;
            };
            for (auto& atom : p.outputs[0].atoms) {
                atom.type = core::AtomType::Salt;
                atom.position = transform(atom.position);
            }
            for (auto& bond : p.outputs[0].bonds) {
                bond.from = transform(bond.from);
                bond.to = transform(bond.to);
            }
            if (mutation == 3) std::reverse(p.inputs.begin(), p.inputs.end());
            p.permissionBits |= (1ULL << 2) | (1ULL << 3) | (1ULL << 9);
            SCOPED_TRACE(std::to_string(family) + "," + std::to_string(mutation));
            const auto plan = AssemblyPlanner::Plan(p);
            const auto parent = solvers::CompileRowAssembly(p, plan);
            const auto child = solvers::CompileStructuredRowAssembly(p, plan);
            EXPECT_EQ(child.objects.size() + (family == 2 ? 0 : 2), parent.objects.size());
            WriteGeneratedPuzzle(puzzlePath, p);
            io::BinaryWriter::WriteSolutionFile(solutionPath, child);
            const auto report = io::VerifySolutionFiles(puzzlePath, solutionPath, 30000, 2000);
            ASSERT_TRUE(report.Passed()) << report.detail << " cycle=" << report.errorCycle;
            io::BinaryWriter::WriteSolutionFile(solutionPath, parent);
            const auto baseline = io::VerifySolutionFiles(puzzlePath, solutionPath, 30000, 2000);
            ASSERT_TRUE(baseline.Passed()) << baseline.detail;
            EXPECT_LT(report.metrics.cost, baseline.metrics.cost);
            EXPECT_LE(report.metrics.area, baseline.metrics.area);
            EXPECT_EQ(report.metrics.cycles, baseline.metrics.cycles);
            for (int bit : {2, 3}) {
                auto forbidden = p;
                forbidden.permissionBits &= ~(1ULL << bit);
                EXPECT_THROW(solvers::CompileStructuredRowAssembly(forbidden, plan),
                             std::invalid_argument);
            }
        }
    }
}

TEST_F(AssemblyVerifierTest, RowCloseFeedBank) {
    for (int family = 0; family < 4; ++family) {
        for (int mutation = 0; mutation < 4; ++mutation) {
            auto p = family == 1 ? Generated({{0, 0}, {1, 0}, {2, 0}, {0, 1}, {2, 1}},
                                             {{0, 1}, {1, 2}, {0, 3}, {2, 4}})
                                 : Generated({{0, 0}, {1, 0}}, {{0, 1}});
            if (family == 2) {
                p = Generated({{0, 0}, {1, 0}, {2, 0}, {3, 0}, {4, 0}, {5, 0}, {6, 0}},
                              {{0, 1}, {1, 2}, {2, 3}, {3, 4}, {4, 5}, {5, 6}});
                p.inputs.resize(1);
                p.inputs[0].atoms = {
                    {core::AtomType::Salt, {0, 0}},  {core::AtomType::Salt, {1, 0}},
                    {core::AtomType::Salt, {0, 1}},  {core::AtomType::Salt, {-1, 1}},
                    {core::AtomType::Salt, {-1, 0}}, {core::AtomType::Salt, {0, -1}},
                    {core::AtomType::Salt, {1, -1}}};
                p.inputs[0].bonds.clear();
                for (std::size_t i = 1; i < p.inputs[0].atoms.size(); ++i)
                    p.inputs[0].bonds.push_back({1, {0, 0}, p.inputs[0].atoms[i].position});
            }
            if (family == 3)
                p = Generated({{0, 0}, {1, 0}, {2, 0}, {3, 0}, {4, 0}},
                              {{0, 1}, {1, 2}, {2, 3}, {3, 4}});
            for (auto& input : p.inputs)
                for (auto& atom : input.atoms)
                    atom.type = core::AtomType::Salt;
            auto transform = [&](core::HexCoord c) {
                if (mutation == 1) return core::Add(c, {4, -3});
                if (mutation == 2) return core::RotateClockwise(c);
                return c;
            };
            for (auto& atom : p.outputs[0].atoms) {
                atom.type = core::AtomType::Salt;
                atom.position = transform(atom.position);
            }
            for (auto& bond : p.outputs[0].bonds) {
                bond.from = transform(bond.from);
                bond.to = transform(bond.to);
            }
            if (mutation == 3) std::reverse(p.inputs.begin(), p.inputs.end());
            p.permissionBits |= (1ULL << 2) | (1ULL << 3) | (1ULL << 9);
            SCOPED_TRACE(std::to_string(family) + "," + std::to_string(mutation));
            const auto plan = AssemblyPlanner::Plan(p);
            const auto parent = solvers::CompileRowAssembly(p, plan);
            const auto child = solvers::CompileCloseFeedRowAssembly(p, plan);

            WriteGeneratedPuzzle(puzzlePath, p);
            io::BinaryWriter::WriteSolutionFile(solutionPath, child);
            const auto report = io::VerifySolutionFiles(puzzlePath, solutionPath, 30000, 2000);
            ASSERT_TRUE(report.Passed()) << report.detail << " cycle=" << report.errorCycle;
            io::BinaryWriter::WriteSolutionFile(solutionPath, parent);
            const auto baseline = io::VerifySolutionFiles(puzzlePath, solutionPath, 30000, 2000);
            ASSERT_TRUE(baseline.Passed()) << baseline.detail;
            EXPECT_LT(report.metrics.cost, baseline.metrics.cost);
            EXPECT_LE(report.metrics.area, baseline.metrics.area);
            EXPECT_LT(report.metrics.cycles, baseline.metrics.cycles);
            for (int bit : {2, 3}) {
                auto forbidden = p;
                forbidden.permissionBits &= ~(1ULL << bit);
                EXPECT_THROW(solvers::CompileCloseFeedRowAssembly(forbidden, plan),
                             std::invalid_argument);
            }
        }
    }
}

TEST_F(AssemblyVerifierTest, RowDemandBoundedEscapeRejectsWideOrLarge) {
    auto large = Generated({{0, 0}, {16, 0}}, {});
    large.permissionBits |= (1ULL << 2) | (1ULL << 3);
    EXPECT_THROW(solvers::CompileCloseFeedRowAssembly(large, AssemblyPlanner::Plan(large)),
                 std::invalid_argument);
    EXPECT_THROW(solvers::CompileStructuredRowAssembly(large, AssemblyPlanner::Plan(large)),
                 std::invalid_argument);
    auto wide = Generated({{0, 0}, {1, 0}}, {{0, 1}});
    wide.permissionBits |= (1ULL << 2) | (1ULL << 3) | (1ULL << 9);
    wide.inputs.resize(1);
    wide.inputs[0].atoms.clear();
    wide.inputs[0].bonds.clear();
    for (int q = -2; q <= 2; ++q)
        for (int r = -2; r <= 2; ++r)
            if (std::abs(q + r) <= 2)
                wide.inputs[0].atoms.push_back({core::AtomType::Salt, {q, r}});
    for (auto& atom : wide.outputs[0].atoms)
        atom.type = core::AtomType::Salt;
    EXPECT_THROW(solvers::CompileCloseFeedRowAssembly(wide, AssemblyPlanner::Plan(wide)),
                 std::invalid_argument);
    EXPECT_THROW(solvers::CompileStructuredRowAssembly(wide, AssemblyPlanner::Plan(wide)),
                 std::invalid_argument);
}

TEST_F(AssemblyVerifierTest, RowSupportsNineFeeds) {
    auto p = Generated({{0, 0}, {1, 0}, {2, 0}, {3, 0}, {4, 0}}, {{0, 1}, {1, 2}, {2, 3}, {3, 4}});
    p.inputs.clear();
    const std::vector<core::AtomType> types{core::AtomType::Salt, core::AtomType::Air,
                                            core::AtomType::Earth, core::AtomType::Fire,
                                            core::AtomType::Water};
    for (auto type : types)
        p.inputs.push_back({{{type, {0, 0}}}, {}});
    for (int i = 0; i < 4; ++i)
        p.inputs.push_back(p.inputs[0]);
    for (std::size_t i = 0; i < types.size(); ++i)
        p.outputs[0].atoms[i].type = types[i];
    VerifyRow(p);
}
TEST_F(AssemblyVerifierTest, RowBranch) {
    VerifyRow(Generated({{0, 0}, {1, 0}, {0, 1}, {-1, 0}}, {{0, 1}, {0, 2}, {0, 3}}));
}
TEST_F(AssemblyVerifierTest, RowShapesAndFeeds) {
    const std::vector<io::PuzzleData> shapes{
        Generated({{0, 0}, {1, 0}, {0, 1}}, {{0, 1}, {1, 2}, {2, 0}}),
        Generated({{0, 0}, {1, 0}, {2, 0}, {3, 0}, {4, 0}}, {{0, 1}, {1, 2}, {2, 3}, {3, 4}}),
        Generated({{0, 0}, {1, 0}, {1, 1}, {0, 2}, {1, 2}}, {{0, 1}, {1, 2}, {2, 3}, {3, 4}}),
        Generated({{0, 0}, {1, 0}, {0, 1}, {-1, 0}}, {{0, 1}, {0, 2}, {0, 3}})};
    for (std::size_t shape = 0; shape < shapes.size(); ++shape) {
        for (int sources = 1; sources <= 4; ++sources) {
            for (int turns = 0; turns < 6; ++turns) {
                auto p = shapes[shape];
                p.inputs.resize(sources, p.inputs.front());
                for (int i = 0; i < sources; ++i) {
                    p.inputs[i].atoms[0].type = static_cast<core::AtomType>(i + 1);
                    p.inputs[i].atoms[0].position = {7 - i, -4};
                }
                auto transform = [&](core::HexCoord cell) {
                    for (int n = 0; n < turns; ++n)
                        cell = core::RotateClockwise(cell);
                    return core::Add(cell, {-9, 6});
                };
                for (std::size_t i = 0; i < p.outputs[0].atoms.size(); ++i) {
                    auto& atom = p.outputs[0].atoms[i];
                    atom.type = p.inputs[i % sources].atoms[0].type;
                    atom.position = transform(atom.position);
                }
                for (auto& bond : p.outputs[0].bonds) {
                    bond.from = transform(bond.from);
                    bond.to = transform(bond.to);
                }
                p.outputMultiplier = 2;
                SCOPED_TRACE(std::to_string(shape) + "/" + std::to_string(sources) + "/" +
                             std::to_string(turns));
                VerifyRow(p);
            }
        }
    }
}
TEST_F(AssemblyVerifierTest, RowRing) {
    auto p = Generated({{1, 0}, {0, 1}, {-1, 1}, {-1, 0}, {0, -1}, {1, -1}},
                       {{0, 1}, {1, 2}, {2, 3}, {3, 4}, {4, 5}, {5, 0}});
    p.inputs.resize(1);
    for (auto& atom : p.outputs[0].atoms)
        atom.type = p.inputs[0].atoms[0].type;
    VerifyRow(p);
}
TEST_F(AssemblyVerifierTest, DisassembledBentFeeds) {
    bool reusedStockReducesGrabs = false;
    for (int rotation = 0; rotation < 6; ++rotation) {
        auto p = Generated({{0, 0}, {1, 0}, {2, 0}}, {{0, 1}, {1, 2}});
        p.inputs.resize(1);
        p.inputs[0].atoms = p.outputs[0].atoms;
        p.inputs[0].atoms[2].position = {2, -1};
        p.inputs[0].bonds = {{1, {0, 0}, {1, 0}}, {1, {1, 0}, {2, -1}}};
        for (int n = 0; n < rotation; ++n) {
            for (auto& a : p.inputs[0].atoms)
                a.position = core::RotateClockwise(a.position);
            for (auto& b : p.inputs[0].bonds) {
                b.from = core::RotateClockwise(b.from);
                b.to = core::RotateClockwise(b.to);
            }
        }
        p.permissionBits |= (1ULL << 2) | (1ULL << 3) | (1ULL << 9) | (1ULL << 17);
        p.outputMultiplier = 2;
        SCOPED_TRACE(rotation);
        VerifyRow(p);
        const auto cached = AssemblySolver::Compile(p, AssemblyPlanner::Plan(p));
        auto grabs = [](const io::SolutionData& solution) {
            std::size_t count = 0;
            for (const auto& object : solution.objects)
                for (const auto& instruction : object.instructions)
                    count += instruction.action == 'G';
            return count;
        };
        p.outputs[0].atoms[0].type = p.inputs[0].atoms[2].type;
        VerifyRow(p);
        const auto repeated = grabs(AssemblySolver::Compile(p, AssemblyPlanner::Plan(p)));
        EXPECT_LE(grabs(cached), repeated);
        reusedStockReducesGrabs |= grabs(cached) < repeated;
    }
    EXPECT_TRUE(reusedStockReducesGrabs);
}
TEST_F(AssemblyVerifierTest, DisassembledMixedFeeds) {
    auto p = Generated({{0, 0}, {1, 0}, {2, 0}, {3, 0}}, {{0, 1}, {1, 2}, {2, 3}});
    p.inputs.resize(2);
    p.inputs[0].atoms = {{p.outputs[0].atoms[0].type, {0, 0}},
                         {p.outputs[0].atoms[2].type, {1, 0}},
                         {p.outputs[0].atoms[0].type, {2, -1}},
                         {p.outputs[0].atoms[2].type, {3, -1}}};
    p.inputs[0].bonds = {{1, {0, 0}, {1, 0}}, {1, {1, 0}, {2, -1}}, {1, {2, -1}, {3, -1}}};
    p.inputs[1].atoms = {{p.outputs[0].atoms[1].type, {0, 0}},
                         {p.outputs[0].atoms[3].type, {1, 0}}};
    p.inputs[1].bonds = {{1, {0, 0}, {1, 0}}};
    p.permissionBits |= (1ULL << 2) | (1ULL << 3) | (1ULL << 9) | (1ULL << 17);
    p.outputMultiplier = 2;
    VerifyRow(p);

    p.inputs[0].atoms = {
        {p.outputs[0].atoms[0].type, {1, 0}},  {p.outputs[0].atoms[2].type, {0, 1}},
        {p.outputs[0].atoms[0].type, {-1, 1}}, {p.outputs[0].atoms[0].type, {-1, 0}},
        {p.outputs[0].atoms[0].type, {0, -1}}, {p.outputs[0].atoms[0].type, {1, -1}}};
    p.inputs[0].bonds.clear();
    for (std::size_t i = 0; i < p.inputs[0].atoms.size(); ++i)
        p.inputs[0].bonds.push_back(
            {1, p.inputs[0].atoms[i].position,
             p.inputs[0].atoms[(i + 1) % p.inputs[0].atoms.size()].position});
    VerifyRow(p);
    p.inputs[0].bonds.clear();
    for (auto& atom : p.inputs[0].atoms) {
        atom.position.q *= 2;
        atom.position.r *= 2;
    }
    EXPECT_THROW((void)AssemblySolver{}.Solve(p), std::invalid_argument);
}
TEST_F(AssemblyVerifierTest, DisassembledOverlappingColumns) {
    auto p = Generated({{0, 0}, {1, 0}, {2, 0}}, {{0, 1}, {1, 2}});
    p.inputs.resize(1);
    p.inputs[0].atoms = {{p.outputs[0].atoms[0].type, {0, 0}},
                         {p.outputs[0].atoms[1].type, {1, 0}},
                         {p.outputs[0].atoms[2].type, {0, 1}}};
    p.inputs[0].bonds = {{1, {0, 0}, {1, 0}}, {1, {1, 0}, {0, 1}}, {1, {0, 1}, {0, 0}}};
    p.permissionBits |= (1ULL << 2) | (1ULL << 3) | (1ULL << 9) | (1ULL << 17);
    p.outputMultiplier = 2;
    for (int rotation = 0; rotation < 6; ++rotation) {
        SCOPED_TRACE(rotation);
        const auto supply = AssemblyPlanner::PlanSupply(p);
        EXPECT_EQ(supply, AssemblyPlanner::PlanSupply(p));
        EXPECT_EQ(supply.usage[0].batches * 3,
                  supply.usage[0].acquired + supply.usage[0].discarded);
        VerifyRow(p);
        std::reverse(p.inputs[0].atoms.begin(), p.inputs[0].atoms.end());
        VerifyRow(p);
        std::reverse(p.inputs[0].atoms.begin(), p.inputs[0].atoms.end());
        for (auto& atom : p.inputs[0].atoms)
            atom.position = core::RotateClockwise(atom.position);
        for (auto& bond : p.inputs[0].bonds) {
            bond.from = core::RotateClockwise(bond.from);
            bond.to = core::RotateClockwise(bond.to);
        }
    }
    std::swap(p.inputs[0].atoms[0].type, p.inputs[0].atoms[2].type);
    const auto obstructed = AssemblyPlanner::PlanSupply(p);
    ASSERT_FALSE(obstructed.steps.empty());
    EXPECT_EQ(obstructed.steps.front().kind, solvers::SupplyPlan::Kind::Discard);
    EXPECT_GT(obstructed.usage[0].discarded, 0);
    VerifyRow(p);

    p.inputs[0].atoms.push_back({p.outputs[0].atoms[1].type, {1, 1}});
    p.inputs[0].bonds.push_back({1, {0, 1}, {1, 1}});
    p.inputs[0].bonds.push_back({1, {1, 0}, {1, 1}});
    VerifyRow(p);
}
TEST_F(AssemblyVerifierTest, CrossSourceSupplyInventory) {
    auto p = Generated({{0, 0}, {1, 0}, {2, 0}}, {{0, 1}, {1, 2}});
    const auto a = p.outputs[0].atoms[2].type;
    const auto b = p.outputs[0].atoms[1].type;
    p.outputs[0].atoms[0].type = a;
    p.inputs.resize(2);
    p.inputs[0].atoms = {{a, {0, 0}}};
    p.inputs[1].atoms = {{a, {0, 0}}, {b, {1, 0}}};
    p.inputs[1].bonds = {{1, {0, 0}, {1, 0}}};
    p.permissionBits |= (1ULL << 2) | (1ULL << 3) | (1ULL << 9) | (1ULL << 17);
    const auto plan = AssemblyPlanner::PlanSupply(p);
    EXPECT_EQ(plan, AssemblyPlanner::PlanSupply(p));
    ASSERT_EQ(plan.steps.size(), 3U);
    EXPECT_EQ(plan.steps[0].input, 1U);
    EXPECT_EQ(plan.steps[1].input, 1U);
    EXPECT_EQ(plan.steps[2].input, 0U);
    for (std::size_t i = 0; i < plan.steps.size(); ++i) {
        const auto& step = plan.steps[i];
        EXPECT_EQ(step.kind, solvers::SupplyPlan::Kind::Take);
        EXPECT_EQ(step.targetAtom, 2U - i);
        EXPECT_EQ(p.inputs[step.input].atoms[step.sourceAtom].type,
                  p.outputs[0].atoms[step.targetAtom].type);
    }
    auto extra = p;
    extra.inputs.push_back(p.inputs[0]);
    const auto unused = AssemblyPlanner::PlanSupply(extra);
    EXPECT_EQ(unused.usage.back().batches, 0U);
    EXPECT_EQ(unused.usage.back().acquired, 0U);
    EXPECT_EQ(unused.usage.back().discarded, 0U);
    for (std::size_t i = 0; i < p.inputs.size(); ++i) {
        EXPECT_EQ(plan.usage[i].batches, 1U);
        EXPECT_EQ(plan.usage[i].discarded, 0U);
        EXPECT_EQ(plan.usage[i].batches * p.inputs[i].atoms.size(),
                  plan.usage[i].acquired + plan.usage[i].discarded);
    }
    p.outputMultiplier = 2;
    p.permissionBits &= ~(1ULL << 17);
    VerifyRow(p);
    p.permissionBits |= 1ULL << 17;

    p.inputs.resize(1);
    p.inputs[0].atoms = {{a, {0, 0}}, {b, {1, 0}}};
    p.inputs[0].bonds = {{1, {0, 0}, {1, 0}}};
    const auto refill = AssemblyPlanner::PlanSupply(p);
    EXPECT_EQ(refill.usage[0].batches, 2U);
    EXPECT_EQ(refill.usage[0].acquired, 3U);
    EXPECT_EQ(refill.usage[0].discarded, 1U);
    EXPECT_EQ(refill.steps.back().kind, solvers::SupplyPlan::Kind::Discard);
    VerifyRow(p);

    p.outputs[0].atoms[1].type = a;
    const auto waste = AssemblyPlanner::PlanSupply(p);
    EXPECT_EQ(waste.usage[0].batches, 3U);
    EXPECT_EQ(waste.usage[0].acquired, 3U);
    EXPECT_EQ(waste.usage[0].discarded, 3U);
    VerifyRow(p);
}
TEST_F(AssemblyVerifierTest, WasteFreeMultiProductPeriod) {
    auto p = Generated({{0, 0}, {1, 0}}, {{0, 1}});
    const auto type = p.outputs[0].atoms[0].type;
    for (auto& atom : p.outputs[0].atoms)
        atom.type = type;
    p.inputs.resize(1);
    p.inputs[0].atoms = {{type, {0, 0}}, {type, {1, 0}}, {type, {2, 0}}};
    p.inputs[0].bonds = {{1, {0, 0}, {1, 0}}, {1, {1, 0}, {2, 0}}};
    p.permissionBits |= (1ULL << 2) | (1ULL << 3) | (1ULL << 9);
    p.permissionBits &= ~(1ULL << 17);
    const auto plan = AssemblyPlanner::PlanSupply(p);
    EXPECT_EQ(plan.productsPerPeriod, 3U);
    EXPECT_EQ(plan.usage[0].batches, 2U);
    EXPECT_EQ(plan.usage[0].acquired, 6U);
    EXPECT_EQ(plan.usage[0].discarded, 0U);
    VerifyRow(p);
    p.outputMultiplier = 2;
    VerifyRow(p);
    auto calcified = p;
    calcified.permissionBits |= 1ULL << 12;
    for (auto& atom : calcified.outputs[0].atoms)
        atom.type = core::AtomType::Salt;
    VerifyRow(calcified);
    auto branch = Generated({{0, 0}, {1, 0}, {0, 1}, {1, 1}}, {{0, 1}, {0, 2}, {1, 3}, {2, 3}});
    branch.inputs = p.inputs;
    branch.permissionBits = p.permissionBits;
    for (auto& atom : branch.outputs[0].atoms)
        atom.type = type;
    EXPECT_EQ(AssemblyPlanner::PlanSupply(branch).productsPerPeriod, 3U);
    VerifyRow(branch);
    p.inputs[0].atoms[1].type = core::AtomType::Quicksilver;
    EXPECT_THROW((void)AssemblyPlanner::PlanSupply(p), std::invalid_argument);
}
TEST_F(AssemblyVerifierTest, ThreeRowFeed) {
    auto p = Generated({{0, 0}, {1, 0}, {2, 0}, {3, 0}, {4, 0}, {5, 0}, {6, 0}},
                       {{0, 1}, {1, 2}, {2, 3}, {3, 4}, {4, 5}, {5, 6}});
    const auto type = p.outputs[0].atoms[0].type;
    for (auto& atom : p.outputs[0].atoms)
        atom.type = type;
    p.inputs.resize(1);
    p.inputs[0].atoms = {{type, {0, 0}},  {type, {1, 0}},  {type, {0, 1}}, {type, {-1, 1}},
                         {type, {-1, 0}}, {type, {0, -1}}, {type, {1, -1}}};
    p.inputs[0].bonds.clear();
    for (std::size_t i = 1; i < p.inputs[0].atoms.size(); ++i)
        p.inputs[0].bonds.push_back({1, {0, 0}, p.inputs[0].atoms[i].position});
    p.permissionBits |= (1ULL << 2) | (1ULL << 3) | (1ULL << 9);
    p.permissionBits &= ~(1ULL << 17);
    VerifyRow(p);
    p.outputMultiplier = 2;
    VerifyRow(p);
}
TEST_F(AssemblyVerifierTest, BufferedBlockers) {
    auto p = Generated({{0, 0}, {1, 0}, {2, 0}, {3, 0}}, {{0, 1}, {1, 2}, {2, 3}});
    p.inputs.resize(1);
    using core::AtomType;
    p.inputs[0].atoms = {{AtomType::Air, {0, 0}},
                         {AtomType::Fire, {1, 0}},
                         {AtomType::Water, {0, 1}},
                         {AtomType::Earth, {1, 1}}};
    p.inputs[0].bonds = {
        {1, {0, 0}, {1, 0}}, {1, {0, 0}, {0, 1}}, {1, {1, 0}, {1, 1}}, {1, {0, 1}, {1, 1}}};
    p.outputs[0].atoms[3].type = AtomType::Fire;
    p.outputs[0].atoms[2].type = AtomType::Earth;
    p.outputs[0].atoms[1].type = AtomType::Air;
    p.outputs[0].atoms[0].type = AtomType::Water;
    p.permissionBits |= (1ULL << 2) | (1ULL << 3) | (1ULL << 9);
    p.permissionBits &= ~(1ULL << 17);
    const auto supply = AssemblyPlanner::PlanSupply(p);
    EXPECT_EQ(supply.usage[0].batches, 1U);
    EXPECT_EQ(supply.usage[0].acquired, 4U);
    EXPECT_EQ(supply.usage[0].discarded, 0U);
    EXPECT_EQ(std::count_if(
                  supply.steps.begin(), supply.steps.end(),
                  [](const auto& step) { return step.kind == solvers::SupplyPlan::Kind::Store; }),
              2);
    VerifyRow(p);
    p.outputMultiplier = 2;
    VerifyRow(p);
    auto calcified = p;
    calcified.permissionBits |= 1ULL << 12;
    calcified.outputs[0].atoms[0].type = AtomType::Salt;
    VerifyRow(calcified);

    auto projection = Generated({{0, 0}, {1, 0}, {2, 0}}, {{0, 1}, {1, 2}});
    projection.inputs = p.inputs;
    projection.inputs[0].atoms[2].type = AtomType::Quicksilver;
    projection.inputs[0].atoms[3].type = AtomType::Lead;
    projection.outputs[0].atoms[2].type = AtomType::Fire;
    projection.outputs[0].atoms[1].type = AtomType::Air;
    projection.outputs[0].atoms[0].type = AtomType::Tin;
    projection.permissionBits = p.permissionBits | (1ULL << 14);
    const auto projected = AssemblyPlanner::PlanSupply(projection);
    EXPECT_TRUE(std::any_of(projected.steps.begin(), projected.steps.end(), [](const auto& step) {
        return step.kind == solvers::SupplyPlan::Kind::Reagent && step.buffer >= 0;
    }));
    VerifyRow(projection);

    auto refill = Generated({{0, 0}, {1, 0}, {2, 0}, {3, 0}, {4, 0}, {5, 0}, {6, 0}, {7, 0}},
                            {{0, 1}, {1, 2}, {2, 3}, {3, 4}, {4, 5}, {5, 6}, {6, 7}});
    refill.inputs = p.inputs;
    refill.permissionBits = p.permissionBits;
    for (std::size_t i = 0; i < 8; ++i)
        refill.outputs[0].atoms[i].type = p.outputs[0].atoms[i / 2].type;
    EXPECT_EQ(AssemblyPlanner::PlanSupply(refill).usage[0].batches, 2U);
    VerifyRow(refill);
}
TEST_F(AssemblyVerifierTest, DisassembledLinearFeeds) {
    for (int rotation = 0; rotation < 6; ++rotation) {
        auto p = Generated({{0, 0}, {1, 0}, {1, 1}}, {{0, 1}, {1, 2}});
        p.inputs.resize(1);
        p.inputs[0].atoms = p.outputs[0].atoms;
        p.inputs[0].atoms[2].position = {2, 0};
        p.inputs[0].bonds = {{1, {0, 0}, {1, 0}}, {1, {1, 0}, {2, 0}}};
        for (int n = 0; n < rotation; ++n) {
            for (auto& a : p.inputs[0].atoms)
                a.position = core::RotateClockwise(a.position);
            for (auto& b : p.inputs[0].bonds) {
                b.from = core::RotateClockwise(b.from);
                b.to = core::RotateClockwise(b.to);
            }
        }
        p.permissionBits |= (1ULL << 9) | (1ULL << 17);
        Replay(p, AssemblyPlanner::Plan(p));
        VerifyRow(p);
        p.outputs[0].atoms[0].type = p.inputs[0].atoms[2].type;
        p.outputMultiplier = 2;
        VerifyRow(p);
        auto plan = AssemblyPlanner::Plan(p);
        plan.sourceAtomForAtom[0] = 0;
        EXPECT_THROW(AssemblyPlanner::Validate(p, plan), std::invalid_argument);
        p.permissionBits &= ~(1ULL << 17);
        EXPECT_THROW(AssemblySolver::Compile(p, AssemblyPlanner::Plan(p)), std::invalid_argument);
        p.permissionBits |= 1ULL << 17;
        p.permissionBits &= ~(1ULL << 9);
        EXPECT_THROW(AssemblyPlanner::Plan(p), std::invalid_argument);
    }
}
#endif

TEST(ChemicalPlanTest, MetalRecipesDoNotAbortDuringMipRestarts) {
    const auto root = opusoptima::test::RepoRoot(__FILE__);
    for (const auto* file : {"asset/puzzle/journal/journal7/P095.puzzle",
                             "asset/puzzle/tourney-2020/OM2020_W2_AmalgamatedGoldRing.puzzle",
                             "asset/puzzle/workshop-versions/w2501727808.puzzle"}) {
        SCOPED_TRACE(file);
        const auto puzzle = io::BinaryParser::ParsePuzzleFile(root / file);
        const auto recipe = solvers::ChemicalPlanner::Plan(puzzle);
        EXPECT_EQ(recipe.inputBatches.size(), puzzle.inputs.size());
        EXPECT_GT(
            std::accumulate(recipe.inputBatches.begin(), recipe.inputBatches.end(), std::size_t{}),
            0U);
    }
}

TEST(ChemicalPlanTest, DuplicatesCardinalIntermediatesConsumedByUnification) {
    io::PuzzleData p;
    p.permissionBits = (1ULL << 13) | (1ULL << 18);
    p.outputMultiplier = 1;
    p.inputs = {{{{core::AtomType::Air, {0, 0}},
                  {core::AtomType::Earth, {1, 0}},
                  {core::AtomType::Fire, {2, 0}},
                  {core::AtomType::Water, {3, 0}},
                  {core::AtomType::Salt, {4, 0}},
                  {core::AtomType::Salt, {5, 0}},
                  {core::AtomType::Salt, {6, 0}},
                  {core::AtomType::Salt, {7, 0}}},
                 {}}};
    p.outputs = {
        {{{core::AtomType::Quintessence, {0, 0}}, {core::AtomType::Quintessence, {1, 0}}}, {}}};

    const auto recipe = solvers::ChemicalPlanner::Plan(p, 1, false);
    EXPECT_EQ(recipe.inputBatches, (std::vector<std::size_t>{1}));
    EXPECT_EQ(recipe.duplications, 4U);
    EXPECT_EQ(recipe.duplicatedElements, (std::vector<std::size_t>{1, 1, 1, 1}));
    EXPECT_EQ(recipe.unifications, 2U);
    EXPECT_EQ(recipe.dispersions, 0U);
    EXPECT_TRUE(std::all_of(recipe.surplus.begin(), recipe.surplus.end(),
                            [](std::size_t count) { return count == 0; }));
}

TEST(ChemicalPlanTest, IntegerBatchesAndSelectiveConversions) {
    auto p = Generated({{0, 0}, {1, 0}}, {{0, 1}});
    p.permissionBits |= 1ULL << 12;
    p.inputs.resize(1);
    p.inputs[0].atoms[0].type = core::AtomType::Water;
    p.outputs[0].atoms[0].type = core::AtomType::Salt;
    p.outputs[0].atoms[1].type = core::AtomType::Water;
    const auto recipe = solvers::ChemicalPlanner::Plan(p);
    ASSERT_EQ(recipe.inputBatches, std::vector<std::size_t>{2});
    EXPECT_EQ(recipe.calcifications, 1U);
    const auto supply = AssemblyPlanner::PlanSupply(p);
    EXPECT_EQ(supply.usage[0].batches, 2U);
    ASSERT_EQ(supply.steps.size(), 2U);
    EXPECT_EQ(supply.steps[0].conversion, solvers::ElementConversion::None);
    EXPECT_EQ(supply.steps[1].conversion, solvers::ElementConversion::Calcification);
    p.permissionBits &= ~(1ULL << 12);
    EXPECT_THROW((void)solvers::ChemicalPlanner::Plan(p), std::invalid_argument);
    p.inputs[0].atoms[0].type = core::AtomType::Quintessence;
    p.outputs[0].atoms.resize(1);
    p.outputs[0].atoms[0].type = core::AtomType::Quintessence;
    EXPECT_EQ(solvers::ChemicalPlanner::Plan(p).calcifications, 0U);
    p.outputs[0].atoms[0].type = core::AtomType::Repeat;
    EXPECT_THROW((void)solvers::ChemicalPlanner::Plan(p), std::invalid_argument);
}

TEST(ChemicalPlanTest, DirectSaltPrecedesConversionAndReplenishes) {
    auto p = Generated({{0, 0}, {1, 0}, {2, 0}}, {{0, 1}, {1, 2}});
    p.permissionBits |= 1ULL << 12;
    p.inputs.resize(2);
    p.inputs[0].atoms[0].type = core::AtomType::Water;
    p.inputs[1].atoms[0].type = core::AtomType::Salt;
    p.outputs[0].atoms[0].type = core::AtomType::Water;
    p.outputs[0].atoms[1].type = core::AtomType::Salt;
    p.outputs[0].atoms[2].type = core::AtomType::Salt;
    EXPECT_EQ(solvers::ChemicalPlanner::Plan(p).calcifications, 0U);
    const auto supply = AssemblyPlanner::PlanSupply(p);
    ASSERT_EQ(supply.steps.size(), 3U);
    EXPECT_EQ(supply.usage[0].acquired, 1U);
    EXPECT_EQ(supply.usage[1].batches, 2U);
    for (const auto& step : supply.steps)
        EXPECT_EQ(step.conversion, solvers::ElementConversion::None);
    p.inputs[0].atoms[0].type = core::AtomType::Repeat;
    EXPECT_THROW((void)solvers::ChemicalPlanner::Plan(p), std::invalid_argument);
    p.inputs[0].atoms[0].type = core::AtomType::Fire;
    EXPECT_THROW((void)solvers::ChemicalPlanner::Plan(p), std::invalid_argument);
}

#if defined(HAS_LIBVERIFY) && !defined(_WIN32)
TEST_F(AssemblyVerifierTest, RecipeBudgetUsesConvertibleSurplus) {
    auto p = Generated({{0, 0}, {1, 0}}, {{0, 1}});
    p.permissionBits |= (1ULL << 12) | (1ULL << 9) | (1ULL << 17);
    p.inputs.resize(2);
    p.inputs[0].atoms = {{core::AtomType::Water, {0, 0}}, {core::AtomType::Water, {1, 0}}};
    p.inputs[0].bonds = {{1, {0, 0}, {1, 0}}};
    p.inputs[1].atoms = {{core::AtomType::Salt, {0, 0}},
                         {core::AtomType::Fire, {1, 0}},
                         {core::AtomType::Fire, {2, 0}}};
    p.inputs[1].bonds = {{1, {0, 0}, {1, 0}}, {1, {1, 0}, {2, 0}}};
    for (bool saltFirst : {false, true}) {
        p.outputs[0].atoms[0].type = saltFirst ? core::AtomType::Water : core::AtomType::Salt;
        p.outputs[0].atoms[1].type = saltFirst ? core::AtomType::Salt : core::AtomType::Water;
        const auto recipe = solvers::ChemicalPlanner::Plan(p);
        ASSERT_EQ(recipe.inputBatches, (std::vector<std::size_t>{1, 0}));
        const auto supply = AssemblyPlanner::PlanSupply(p);
        EXPECT_TRUE(supply.recipeConstrained);
        EXPECT_EQ(supply.usage[0].batches, 1U);
        EXPECT_EQ(supply.usage[1].batches, 0U);
        EXPECT_EQ(supply.steps.size(), 2U);
        VerifyRow(p);
    }
    p.inputs.resize(1);
    p.inputs[0].atoms = {{core::AtomType::Water, {0, 0}},
                         {core::AtomType::Fire, {0, 1}},
                         {core::AtomType::Air, {1, 0}},
                         {core::AtomType::Earth, {1, 1}}};
    p.inputs[0].bonds = {{1, {0, 0}, {0, 1}}, {1, {0, 0}, {1, 0}}, {1, {1, 0}, {1, 1}}};
    p.outputs[0].atoms[0].type = core::AtomType::Fire;
    p.outputs[0].atoms[1].type = core::AtomType::Water;
    EXPECT_EQ(solvers::ChemicalPlanner::Plan(p).inputBatches[0], 1U);
    const auto fallback = AssemblyPlanner::PlanSupply(p);
    EXPECT_FALSE(fallback.recipeConstrained);
    EXPECT_EQ(fallback.usage[0].batches, 2U);
    EXPECT_EQ(fallback.usage[0].batches * p.inputs[0].atoms.size(),
              fallback.usage[0].acquired + fallback.usage[0].discarded);
    VerifyRow(p);
}

TEST_F(AssemblyVerifierTest, MetalProjection) {
    for (int source = 8; source <= 12; ++source) {
        auto p = Generated({{0, 0}, {1, 0}}, {{0, 1}});
        p.permissionBits |= 1ULL << 14;
        p.inputs[0].atoms[0].type = static_cast<core::AtomType>(source);
        p.inputs[1].atoms[0].type = core::AtomType::Quicksilver;
        p.outputs[0].atoms[0].type = core::AtomType::Gold;
        p.outputs[0].atoms[1].type = core::AtomType::Quicksilver;
        const auto recipe = solvers::ChemicalPlanner::Plan(p);
        EXPECT_EQ(recipe.projections, static_cast<std::size_t>(source - 7));
        const auto supply = AssemblyPlanner::PlanSupply(p);
        EXPECT_TRUE(supply.recipeConstrained);
        EXPECT_EQ(supply.usage[1].acquired, static_cast<std::size_t>(source - 6));
        VerifyRow(p);
        p.permissionBits &= ~(1ULL << 14);
        EXPECT_THROW((void)solvers::ChemicalPlanner::Plan(p), std::invalid_argument);
        p.permissionBits |= 1ULL << 14;
        p.inputs.resize(1);
        EXPECT_THROW((void)solvers::ChemicalPlanner::Plan(p), std::invalid_argument);
    }
}

TEST_F(AssemblyVerifierTest, ProjectionFromBondedFeeds) {
    auto p = Generated({{0, 0}, {1, 0}, {2, 0}}, {{0, 1}, {1, 2}});
    p.permissionBits |= (1ULL << 14) | (1ULL << 12) | (1ULL << 9) | (1ULL << 17);
    p.inputs.resize(2);
    p.inputs[0].atoms = {{core::AtomType::Lead, {0, 0}},
                         {core::AtomType::Quicksilver, {0, 1}},
                         {core::AtomType::Quicksilver, {1, 0}},
                         {core::AtomType::Water, {1, 1}}};
    p.inputs[0].bonds = {{1, {0, 0}, {0, 1}}, {1, {0, 0}, {1, 0}}, {1, {1, 0}, {1, 1}}};
    p.inputs[1].atoms[0].type = core::AtomType::Water;
    p.outputs[0].atoms[0].type = core::AtomType::Gold;
    p.outputs[0].atoms[1].type = core::AtomType::Salt;
    p.outputs[0].atoms[2].type = core::AtomType::Quicksilver;
    const auto supply = AssemblyPlanner::PlanSupply(p);
    for (std::size_t i = 0; i < p.inputs.size(); ++i)
        EXPECT_EQ(supply.usage[i].batches * p.inputs[i].atoms.size(),
                  supply.usage[i].acquired + supply.usage[i].discarded);
    VerifyRow(p);
    p.outputs[0].atoms[0].type = core::AtomType::Tin;
    VerifyRow(p);
}

TEST_F(AssemblyVerifierTest, SelectiveCalcification) {
    for (int type = 2; type <= 5; ++type) {
        auto p = Generated({{0, 0}, {1, 0}}, {{0, 1}});
        p.permissionBits |= 1ULL << 12;
        p.inputs.resize(1);
        p.inputs[0].atoms[0].type = static_cast<core::AtomType>(type);
        p.outputs[0].atoms[0].type = core::AtomType::Salt;
        p.outputs[0].atoms[1].type = static_cast<core::AtomType>(type);
        VerifyRow(p);
        p.inputs[0].atoms.push_back({static_cast<core::AtomType>(type), {1, 0}});
        p.inputs[0].bonds = {{1, {0, 0}, {1, 0}}};
        p.permissionBits |= (1ULL << 9) | (1ULL << 17);
        VerifyRow(p);
    }
}
#endif

TEST(AssemblyPlanTest, RejectMalformedTriplexInputs) {
    auto p = Generated({{0, 0}, {1, 0}}, {{0, 1}});
    for (auto& a : p.outputs[0].atoms)
        a.type = core::AtomType::Fire;
    p.inputs = {p.outputs[0]};
    p.permissionBits |= (1ULL << 9) | (1ULL << 2) | (1ULL << 3);
    for (int bits : {0, 3, 5, 7, 9, 11, 13, 15, 16, 255}) {
        p.inputs[0].bonds[0].bits = bits;
        EXPECT_THROW(AssemblyPlanner::Plan(p), std::invalid_argument) << bits;
    }
    for (int bits : {2, 4, 6, 8, 10, 12, 14}) {
        p.inputs[0].bonds[0].bits = bits;
        EXPECT_NO_THROW(AssemblyPlanner::Plan(p)) << bits;
    }
    p.inputs[0].bonds[0].bits = 14;
    auto bad = p;
    bad.permissionBits &= ~(1ULL << 9);
    EXPECT_THROW(AssemblyPlanner::Plan(bad), std::invalid_argument);
    bad = p;
    for (auto& atom : bad.inputs[0].atoms)
        atom.type = core::AtomType::Salt;
    for (auto& atom : bad.outputs[0].atoms)
        atom.type = core::AtomType::Salt;
    bad.outputs[0].bonds[0].bits = 1;
    EXPECT_NO_THROW(AssemblyPlanner::Plan(bad));
    bad = p;
    bad.inputs[0].bonds.push_back({14, {1, 0}, {0, 0}});
    EXPECT_THROW(AssemblyPlanner::Plan(bad), std::invalid_argument);
    bad = p;
    bad.inputs[0].bonds[0].to = {2, 0};
    EXPECT_THROW(AssemblyPlanner::Plan(bad), std::invalid_argument);
}

#ifdef HAS_LIBVERIFY
TEST_F(AssemblyVerifierTest, TriplexInputPhysicalDisassembly) {
    for (int rotation = 0; rotation < 6; ++rotation) {
        auto p = Generated({{0, 0}, {1, 0}, {2, 0}}, {{0, 1}, {1, 2}});
        for (auto& a : p.outputs[0].atoms)
            a.type = core::AtomType::Fire;
        p.inputs = {p.outputs[0]};
        p.inputs[0].bonds[0].bits = 14;
        for (int turn = 0; turn < rotation; ++turn) {
            for (auto& a : p.inputs[0].atoms)
                a.position = core::RotateClockwise(a.position);
            for (auto& b : p.inputs[0].bonds) {
                b.from = core::RotateClockwise(b.from);
                b.to = core::RotateClockwise(b.to);
            }
        }
        p.permissionBits |= (1ULL << 9);
        VerifyRow(p);
        p.outputs[0].bonds[1].bits = 14;
        p.permissionBits |= (1ULL << 11);
        VerifyRow(p);
    }
}

TEST_F(AssemblyVerifierTest, SelectivePostBondCalcificationTwice) {
    const auto root = opusoptima::test::RepoRoot(__FILE__);
    const auto original =
        io::BinaryParser::ParsePuzzleFile(root / "asset/puzzle/campaign/ch2/P017.puzzle");
    for (int rotation = 0; rotation < 6; ++rotation) {
        for (int selected : {1, 2}) {
            SCOPED_TRACE(rotation);
            SCOPED_TRACE(selected);
            auto p = original;
            for (auto& atom : p.outputs[0].atoms) {
                atom.type = core::AtomType::Fire;
                for (int turn = 0; turn < rotation; ++turn)
                    atom.position = core::RotateClockwise(atom.position);
            }
            for (auto& bond : p.outputs[0].bonds)
                for (int turn = 0; turn < rotation; ++turn) {
                    bond.from = core::RotateClockwise(bond.from);
                    bond.to = core::RotateClockwise(bond.to);
                }
            for (int atom = 0; atom < selected; ++atom)
                p.outputs[0].atoms[atom].type = core::AtomType::Salt;
            VerifyRow(p);
        }
    }
}

TEST_F(AssemblyVerifierTest, OriginalExplosivePhialRequiresPostBondCalcification) {
    const auto root = opusoptima::test::RepoRoot(__FILE__);
    const auto original = root / "asset/puzzle/tourney-2021/OM2021_W1.puzzle";
    const auto p = io::BinaryParser::ParsePuzzleFile(original);
    ASSERT_EQ(p.name, "IMPROVED EXPLOSIVE PHIAL");
    ASSERT_EQ(p.inputs[0].bonds[0].bits, 14);
    ASSERT_EQ(p.outputs[0].bonds[1].bits, 14);
    ASSERT_EQ(p.outputs[0].atoms[2].type, core::AtomType::Salt);
    // A triplex edge incident to salt must be created while it is fire, then
    // calcified on the assembled molecule; feed-only calcification cannot do it.
    ASSERT_TRUE(AssemblySolver{}.Supports(p));
    auto solution = AssemblySolver{}.Solve(p);
    io::BinaryWriter::WriteSolutionFile(solutionPath, solution);
    const auto first = io::VerifySolutionFiles(original, solutionPath, 150000, 10000);
    ASSERT_TRUE(first.Passed()) << first.detail << " cycle=" << first.errorCycle << " at "
                                << first.errorU << ',' << first.errorV;
    solution.metrics = first.metrics;
    io::BinaryWriter::WriteSolutionFile(solutionPath, solution);
    const auto second = io::VerifySolutionFiles(original, solutionPath, 150000, 10000);
    ASSERT_TRUE(second.Passed()) << second.detail;
    EXPECT_EQ(second.metrics.cycles, first.metrics.cycles);
    EXPECT_EQ(second.metrics.cost, first.metrics.cost);
    EXPECT_EQ(second.metrics.area, first.metrics.area);
    EXPECT_EQ(second.metrics.instructionCount, first.metrics.instructionCount);
    std::cout << "Original OM2021_W1 cycles=" << first.metrics.cycles
              << " cost=" << first.metrics.cost << " area=" << first.metrics.area
              << " instructions=" << first.metrics.instructionCount << '\n';
}
#endif

#ifdef HAS_LIBVERIFY
TEST_F(AssemblyVerifierTest, ComponentRowHandoff) {
    for (int transform = 0; transform < 4; ++transform)
        for (int family = 0; family < 3; ++family) {
            auto p = family == 0 ? Generated({{0, 0}, {1, 0}, {2, 0}}, {{0, 1}, {1, 2}})
                     : family == 1
                         ? Generated({{0, 0}, {1, 0}, {0, 1}, {1, 1}}, {{0, 1}, {2, 3}, {0, 2}})
                         : Generated({{0, 0}, {1, 0}, {2, 0}, {0, 1}, {1, 1}, {2, 1}},
                                     {{0, 1}, {1, 2}, {3, 4}, {4, 5}, {0, 3}, {2, 5}});
            p.permissionBits |= (1ULL << 2) | (1ULL << 3);
            if (transform & 1) {
                for (auto& atom : p.outputs[0].atoms) {
                    atom.position.q += 4;
                    atom.position.r -= 3;
                }
                for (auto& bond : p.outputs[0].bonds) {
                    bond.from.q += 4;
                    bond.from.r -= 3;
                    bond.to.q += 4;
                    bond.to.r -= 3;
                }
            }
            if (transform & 2) std::reverse(p.inputs.begin(), p.inputs.end());
            ASSERT_TRUE(solvers::SupportsComponentRowAssembly(p));
            for (int bit : {2, 3}) {
                auto forbidden = p;
                forbidden.permissionBits &= ~(1ULL << bit);
                EXPECT_FALSE(solvers::SupportsComponentRowAssembly(forbidden));
                EXPECT_THROW(
                    solvers::CompileComponentRowAssembly(forbidden, AssemblyPlanner::Plan(p)),
                    std::invalid_argument);
            }
            const auto plan = AssemblyPlanner::Plan(p);
            const auto child = solvers::CompileComponentRowAssembly(p, plan);
            WriteGeneratedPuzzle(puzzlePath, p);
            io::BinaryWriter::WriteSolutionFile(solutionPath, child);
            const auto report = io::VerifySolutionFiles(puzzlePath, solutionPath, 30000, 2000);
            EXPECT_TRUE(report.Passed())
                << "family=" << family << " " << report.detail << " cycle=" << report.errorCycle;
            // The component hand-off earns its place against the serialized row;
            // with the row clocks on the plain row already pipelines, so comparing
            // against the default would measure the pipeline instead of the hand-off.
            io::SolutionData parent;
            {
                ScopedRowClocks clocks("off");
                parent = solvers::CompileRowAssembly(p, plan);
            }
            io::BinaryWriter::WriteSolutionFile(solutionPath, parent);
            const auto parentReport =
                io::VerifySolutionFiles(puzzlePath, solutionPath, 30000, 2000);
            ASSERT_TRUE(parentReport.Passed()) << parentReport.detail;
            std::cout << "COMPONENT_METRICS " << family << " " << transform << " parent "
                      << parentReport.metrics.cost << " " << parentReport.metrics.cycles << " "
                      << parentReport.metrics.area << " child " << report.metrics.cost << " "
                      << report.metrics.cycles << " " << report.metrics.area << std::endl;
            EXPECT_LT(report.metrics.cycles, parentReport.metrics.cycles);
            if (!transform) {
                const test::ScopedTestDirectory scratch;
                WriteGeneratedPuzzle(
                    scratch.Path() / ("family-" + std::to_string(family) + ".puzzle"), p);
            }
        }
}

TEST(AssemblyDiagnostics, ComponentRowRegisteredRejections) {
    auto valid = Generated({{0, 0}, {1, 0}, {0, 1}, {1, 1}}, {{0, 1}, {2, 3}, {0, 2}});
    valid.permissionBits |= (1ULL << 2) | (1ULL << 3);
    ASSERT_TRUE(solvers::SupportsComponentRowAssembly(valid));
    auto broken = valid;
    broken.outputs[0].bonds.erase(broken.outputs[0].bonds.begin());
    EXPECT_FALSE(solvers::SupportsComponentRowAssembly(broken));
    auto hole = valid;
    hole.outputs[0].atoms.pop_back();
    EXPECT_FALSE(solvers::SupportsComponentRowAssembly(hole));
    auto rotated = valid;
    for (auto& atom : rotated.outputs[0].atoms)
        atom.position = core::RotateClockwise(atom.position);
    for (auto& bond : rotated.outputs[0].bonds) {
        bond.from = core::RotateClockwise(bond.from);
        bond.to = core::RotateClockwise(bond.to);
    }
    EXPECT_FALSE(solvers::SupportsComponentRowAssembly(rotated));
    auto multi = valid;
    multi.inputs[0].atoms.push_back({core::AtomType::Salt, {1, 0}});
    EXPECT_FALSE(solvers::SupportsComponentRowAssembly(multi));
    auto triplex = valid;
    triplex.outputs[0].bonds[0].bits = 2;
    EXPECT_FALSE(solvers::SupportsComponentRowAssembly(triplex));
}

TEST_F(AssemblyVerifierTest, SerialWorkstationPreregisteredFamilies) {
    const test::ScopedTestDirectory scratch;
    const auto& evidence = scratch.Path();
    int constructed = 0, passed = 0;
    for (int family = 0; family < 3; ++family) {
        for (int rotation = 0; rotation < 6; ++rotation) {
            auto p = Generated(
                {{0, 0}, {1, 0}, family == 0 ? core::HexCoord{2, 0} : core::HexCoord{1, 1}},
                {{0, 1}, {1, 2}});
            p.permissionBits |= (1ULL << 2) | (1ULL << 3) | (1ULL << 26);
            if (family == 2) {
                p.outputs[0].atoms[2].type = p.outputs[0].atoms[0].type;
                p.inputs.pop_back();
            }
            auto transform = [&](core::HexCoord cell) {
                for (int turn = 0; turn < rotation; ++turn)
                    cell = core::RotateClockwise(cell);
                return core::Add(cell, {2, -3});
            };
            for (auto& atom : p.outputs[0].atoms)
                atom.position = transform(atom.position);
            for (auto& bond : p.outputs[0].bonds) {
                bond.from = transform(bond.from);
                bond.to = transform(bond.to);
            }
            for (auto& input : p.inputs)
                input.atoms[0].position = {3, -2};
            const auto stem = evidence / ("family-" + std::to_string(family) + "-rotation-" +
                                          std::to_string(rotation));
            auto candidate = solvers::CompileSharedWorkstation(p);
            for (const int bit : {2, 3, 8, 26}) {
                auto denied = p;
                denied.permissionBits &= ~(1ULL << bit);
                EXPECT_FALSE(solvers::CompileSharedWorkstation(denied).has_value())
                    << "permission=" << bit;
            }
            if (family < 2) EXPECT_TRUE(candidate.has_value());
            std::cout << "WORKSTATION family=" << family << " rotation=" << rotation
                      << " constructed=" << candidate.has_value();
            if (!candidate) {
                std::cout << '\n';
                continue;
            }
            ++constructed;
            const auto puzzle = stem.string() + ".puzzle";
            const auto solution = stem.string() + ".solution";
            WriteGeneratedPuzzle(puzzle, p);
            io::BinaryWriter::WriteSolutionFile(solution, *candidate);
            const auto gate = io::VerifySolutionFiles(puzzle, solution, 30000, 2000);
            passed += gate.Passed();
            std::cout << " gate=" << gate.Passed() << " detail=" << gate.detail
                      << " cost=" << gate.metrics.cost << " cycles=" << gate.metrics.cycles
                      << " area=" << gate.metrics.area << '\n';
            EXPECT_EQ(std::count_if(candidate->objects.begin(), candidate->objects.end(),
                                    [](const auto& part) { return part.typeName == "bonder"; }),
                      1);
        }
    }
    std::cout << "WORKSTATION total constructed=" << constructed << " passed=" << passed << '\n';
    EXPECT_EQ(constructed, 18);
    EXPECT_EQ(passed, 18);
}
#endif

namespace {
// These diagnostics describe the shared serpentine feeder, so they pin the
// feeder machine even though the shipped compact candidate now uses the local
// source intake by default.
struct ScopedSharedFeeder {
    std::optional<std::string> prior;
    ScopedSharedFeeder() {
        if (const auto* existing = std::getenv("OPUSOPTIMA_DIRECT_INPUT")) prior = existing;
        setenv("OPUSOPTIMA_DIRECT_INPUT", "off", 1);
    }
    ~ScopedSharedFeeder() {
        if (prior)
            setenv("OPUSOPTIMA_DIRECT_INPUT", prior->c_str(), 1);
        else
            unsetenv("OPUSOPTIMA_DIRECT_INPUT");
    }
};
std::string ActionSequence(const io::SolutionObject& object) {
    std::string actions;
    for (const auto& instruction : object.instructions)
        actions.push_back(instruction.action);
    return actions;
}
int LastTick(const io::SolutionData& solution) {
    int last = 0;
    for (const auto& object : solution.objects)
        for (const auto& instruction : object.instructions)
            last = std::max(last, static_cast<int>(instruction.parameter));
    return last;
}
} // namespace

// The feeder clock is on by default since 2026-11-11. It must be
// byte-deterministic, keep every arm's action sequence and cost/area unchanged,
// never lengthen the machine, and `=off` must restore the serialized emitter
// byte for byte.
TEST(AssemblyDiagnostics, FeedPrefetchDefaultsOnDeterministicAndRetimesOnlyTheFeeder) {
    ScopedSharedFeeder sharedFeeder;
    auto puzzle = Generated({{0, 0}, {1, 0}, {2, 0}}, {{0, 1}, {1, 2}});
    puzzle.name = "generated-feed-prefetch";
    puzzle.permissionBits |= (1ULL << 2) | (1ULL << 3); // piston + track
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    const auto plan = AssemblyPlanner::Plan(puzzle, false, deadline);

    // `=off` on every knob is the serialized emitter of the pre-2026-11-11 default.
    std::vector<std::byte> serialized;
    io::SolutionData off;
    {
        ScopedRowClocks flag("off");
        off = solvers::CompileScheduledRowAssembly(puzzle, plan, deadline);
        serialized = io::BinaryWriter::WriteSolution(off);
        EXPECT_EQ(io::BinaryWriter::WriteSolution(
                      solvers::CompileScheduledRowAssembly(puzzle, plan, deadline)),
                  serialized);
    }

    // Unset is the pipelined default and must match an explicit "on".
    std::vector<std::byte> shipped;
    io::SolutionData on;
    {
        ScopedRowClocks flag(nullptr);
        on = solvers::CompileScheduledRowAssembly(puzzle, plan, deadline);
        shipped = io::BinaryWriter::WriteSolution(on);
        EXPECT_EQ(io::BinaryWriter::WriteSolution(
                      solvers::CompileScheduledRowAssembly(puzzle, plan, deadline)),
                  shipped);
    }
    {
        ScopedRowClocks flag("on");
        EXPECT_EQ(io::BinaryWriter::WriteSolution(
                      solvers::CompileScheduledRowAssembly(puzzle, plan, deadline)),
                  shipped);
    }
    EXPECT_NE(shipped, serialized);

    ASSERT_EQ(on.objects.size(), off.objects.size());
    for (std::size_t i = 0; i < off.objects.size(); ++i) {
        EXPECT_EQ(ActionSequence(on.objects[i]), ActionSequence(off.objects[i]))
            << "object " << i << " (" << off.objects[i].typeName << ")";
    }
    EXPECT_LT(LastTick(on), LastTick(off));
}

// The opt-in row pipeline gates each feeder drop on the exact hand-off clearance
// (rather than the whole shared assembly clock) and pre-stages the next atom.
// It must be byte-deterministic, retime only (never add/remove actions), and
// never be longer than the P2 prefetch machine.
TEST(AssemblyDiagnostics, RowPipelineIsOptInDeterministicAndNeverLaterThanPrefetch) {
    ScopedSharedFeeder sharedFeeder;
    auto puzzle = Generated({{0, 0}, {1, 0}, {2, 0}}, {{0, 1}, {1, 2}});
    puzzle.name = "generated-row-pipeline";
    puzzle.permissionBits |= (1ULL << 2) | (1ULL << 3); // piston + track
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    const auto plan = AssemblyPlanner::Plan(puzzle, false, deadline);

    io::SolutionData off;
    {
        ScopedRowClocks clocks("off");
        off = solvers::CompileScheduledRowAssembly(puzzle, plan, deadline);
    }
    io::SolutionData prefetch;
    {
        // Feeder clock only: the shipped default for it, with the pipeline off.
        ScopedFeedPrefetch feedFlag(nullptr);
        ScopedRowPipeline pipelineFlag("off");
        ScopedRowAssemblyClocks clocksFlag("off");
        prefetch = solvers::CompileScheduledRowAssembly(puzzle, plan, deadline);
    }
    io::SolutionData pipeline, pipelineAgain;
    {
        ScopedFeedPrefetch feedFlag(nullptr);
        ScopedRowPipeline pipelineFlag(nullptr);
        ScopedRowAssemblyClocks clocksFlag("off");
        pipeline = solvers::CompileScheduledRowAssembly(puzzle, plan, deadline);
        pipelineAgain = solvers::CompileScheduledRowAssembly(puzzle, plan, deadline);
    }
    // Byte-deterministic across two independent compilations.
    EXPECT_EQ(io::BinaryWriter::WriteSolution(pipeline),
              io::BinaryWriter::WriteSolution(pipelineAgain));
    // Retiming only: every object keeps the identical action string.
    ASSERT_EQ(pipeline.objects.size(), off.objects.size());
    for (std::size_t i = 0; i < off.objects.size(); ++i)
        EXPECT_EQ(ActionSequence(pipeline.objects[i]), ActionSequence(off.objects[i]))
            << "object " << i << " (" << off.objects[i].typeName << ")";
    // Never later than P2 prefetch, and strictly shorter than the serialized emitter.
    EXPECT_LE(LastTick(pipeline), LastTick(prefetch));
    EXPECT_LT(LastTick(pipeline), LastTick(off));
}

// The opt-in assembly clocks give the upper-arm return/regrab tail and the
// conveyor's empty row-entry return their own absolute clocks, so they overlap
// the lower-arm reset/staging. The transform must be byte-deterministic, retime
// only (identical action strings), and never lengthen the machine.
TEST(AssemblyDiagnostics, RowAssemblyClocksAreOptInDeterministicAndNeverLaterThanPipeline) {
    auto puzzle = Generated({{0, 0}, {1, 0}, {0, 1}}, {{0, 1}, {0, 2}});
    puzzle.name = "generated-row-assembly-clocks";
    puzzle.permissionBits |= (1ULL << 2) | (1ULL << 3); // piston + track
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    const auto plan = AssemblyPlanner::Plan(puzzle, false, deadline);

    io::SolutionData off;
    {
        ScopedRowClocks clocks("off");
        off = solvers::CompileScheduledRowAssembly(puzzle, plan, deadline);
    }
    io::SolutionData pipeline;
    {
        ScopedFeedPrefetch feedFlag(nullptr);
        ScopedRowPipeline pipelineFlag(nullptr);
        ScopedRowAssemblyClocks clocksFlag("off");
        pipeline = solvers::CompileScheduledRowAssembly(puzzle, plan, deadline);
    }
    io::SolutionData assembly, assemblyAgain;
    {
        // Unset is the shipped default: the assembly clock alone implies the pipeline.
        ScopedRowClocks clocksFlag(nullptr);
        assembly = solvers::CompileScheduledRowAssembly(puzzle, plan, deadline);
        assemblyAgain = solvers::CompileScheduledRowAssembly(puzzle, plan, deadline);
    }
    // Byte-deterministic across two independent compilations.
    EXPECT_EQ(io::BinaryWriter::WriteSolution(assembly),
              io::BinaryWriter::WriteSolution(assemblyAgain));
    // Retiming only: every object keeps the identical action string.
    ASSERT_EQ(assembly.objects.size(), off.objects.size());
    for (std::size_t i = 0; i < off.objects.size(); ++i)
        EXPECT_EQ(ActionSequence(assembly.objects[i]), ActionSequence(off.objects[i]))
            << "object " << i << " (" << off.objects[i].typeName << ")";
    // Never later than the row pipeline, and strictly shorter than the serialized emitter.
    EXPECT_LE(LastTick(assembly), LastTick(pipeline));
    EXPECT_LT(LastTick(assembly), LastTick(off));
}

// The separate clocked scheduled-row candidate must preserve the compact
// machine's object/action inventory and only retime independent streams. This
// is what lets the portfolio keep the compact cost/area slots while taking the
// clocked machine only for cycles.
TEST(AssemblyDiagnostics, ClockedScheduledRowKeepsActionsAndShortensTicks) {
    ScopedSharedFeeder sharedFeeder;
    auto puzzle = Generated({{0, 0}, {1, 0}, {2, 0}}, {{0, 1}, {1, 2}});
    puzzle.name = "generated-row-clocked";
    puzzle.permissionBits |= (1ULL << 2) | (1ULL << 3); // piston + track
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    const auto plan = AssemblyPlanner::Plan(puzzle, false, deadline);
    io::SolutionData compact;
    {
        // The serialized emitter, so the clocked candidate still has something to beat.
        ScopedRowClocks clocks("off");
        compact = solvers::CompileScheduledRowAssembly(puzzle, plan, deadline);
    }
    const auto clocked = solvers::CompileScheduledRowClockAssembly(puzzle, plan, deadline);
    const auto again = solvers::CompileScheduledRowClockAssembly(puzzle, plan, deadline);
    EXPECT_EQ(io::BinaryWriter::WriteSolution(clocked), io::BinaryWriter::WriteSolution(again));
    ASSERT_EQ(clocked.objects.size(), compact.objects.size());
    for (std::size_t i = 0; i < compact.objects.size(); ++i) {
        EXPECT_EQ(clocked.objects[i].typeName, compact.objects[i].typeName) << i;
        EXPECT_EQ(ActionSequence(clocked.objects[i]), ActionSequence(compact.objects[i]))
            << "object " << i << " (" << compact.objects[i].typeName << ")";
    }
    EXPECT_LT(LastTick(clocked), LastTick(compact));
}
