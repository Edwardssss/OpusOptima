#include "BinaryWriter.hpp"
#include "ProductionMolecular.hpp"
#include "SolutionVerifier.hpp"

#include <gtest/gtest.h>
#include "RepoRoot.hpp"

#if defined(__linux__) && defined(OPUSOPTIMA_FAULT_WORKER_PATH)
#    include <atomic>
#    include <cstdlib>
#    include <fstream>
#    include <future>
#    include <spdlog/sinks/base_sink.h>
#    include <spdlog/spdlog.h>
#    include <thread>
#    include <vector>

namespace {
using namespace opusoptima;
const auto root = opusoptima::test::RepoRoot(__FILE__);
io::VerificationOptions FaultOptions() {
    io::VerificationOptions options;
    options.workerExecutable = OPUSOPTIMA_FAULT_WORKER_PATH;
    options.timeoutMilliseconds = 1000;
    return options;
}
struct Files {
    std::filesystem::path directory;
    Files() {
        auto pattern = (std::filesystem::temp_directory_path() / "opus-worker-XXXXXX").string();
        std::vector<char> bytes(pattern.begin(), pattern.end());
        bytes.push_back('\0');
        const auto created = mkdtemp(bytes.data());
        if (!created) throw std::runtime_error("Cannot create worker test directory");
        directory = created;
    }
    ~Files() {
        std::error_code error;
        std::filesystem::remove_all(directory, error);
    }
};
} // namespace
TEST(VerifierWorkerProcess, ProtocolFailuresAndWorkerCrashesFailClosed) {
    for (const auto* mode :
         {"crash", "silent", "truncated", "oversized", "exit-error", "wrong-authority"}) {
        SCOPED_TRACE(mode);
        const auto report = io::VerifySolutionFiles(mode, "unused", FaultOptions());
        EXPECT_EQ(report.status, io::VerificationStatus::Error) << report.detail;
        EXPECT_FALSE(report.Passed());
    }
    auto independent = FaultOptions();
    independent.mode = io::VerificationMode::Independent;
    const auto unknownProfile = io::VerifySolutionFiles("wrong-authority", "unused", independent);
    EXPECT_EQ(unknownProfile.status, io::VerificationStatus::Error);
    EXPECT_EQ(unknownProfile.reasonCode, "worker-protocol-invalid");
    auto options = FaultOptions();
    options.workerExecutable = "/definitely-missing-opus-worker";
    EXPECT_EQ(io::VerifySolutionFiles("unused", "unused", options).status,
              io::VerificationStatus::Error);
    options.workerExecutable = "OpusOptimaFaultVerifierWorker";
    EXPECT_EQ(io::VerifySolutionFiles("unused", "unused", options).reasonCode,
              "worker-path-invalid");
}
TEST(VerifierWorkerProcess, LargeResponsesAreDrainedAndWorkerPathsDoNotUseTheCwd) {
    Files files;
    const auto executable = files.directory / "worker with spaces";
    std::filesystem::copy_file(OPUSOPTIMA_FAULT_WORKER_PATH, executable);
    const auto previous = std::filesystem::current_path();
    struct Restore {
        std::filesystem::path path;
        ~Restore() {
            std::filesystem::current_path(path);
        }
    } restore{previous};
    std::filesystem::current_path(files.directory);
    auto options = FaultOptions();
    options.workerExecutable = executable;
    const auto report = io::VerifySolutionFiles("large", "unused", options);
    ASSERT_TRUE(report.Passed()) << report.detail;
    EXPECT_EQ(report.detail.size(), 32000);
    EXPECT_EQ(report.authority, io::VerificationAuthority::LegacyGate);
    EXPECT_EQ(report.metrics.instructionCount, 0);
}
TEST(VerifierWorkerProcess, SharedDeadlinesStopHangsAndCleanDescendants) {
    auto options = FaultOptions();
    options.outerDeadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(100);
    const auto start = std::chrono::steady_clock::now();
    EXPECT_EQ(io::VerifySolutionFiles("hang", "unused", options).status,
              io::VerificationStatus::Timeout);
    EXPECT_LT(std::chrono::steady_clock::now() - start, std::chrono::seconds(2));
    options.outerDeadline = std::chrono::steady_clock::now() - std::chrono::seconds(1);
    EXPECT_EQ(io::VerifySolutionFiles("hang", "unused", options).reasonCode, "worker-timeout");
    const auto descendant = io::VerifySolutionFiles("descendant", "unused", FaultOptions());
    ASSERT_TRUE(descendant.Passed()) << descendant.detail;
    EXPECT_GT(descendant.errorU, 0);
}
TEST(VerifierWorkerProcess, PublicationRejectsChangedMetricsOrProvenanceAndKeepsOutput) {
    Files files;
    const auto destination = files.directory / "keep.solution";
    {
        std::ofstream stream(destination, std::ios::binary);
        stream << "original output";
    }
    const auto before = io::BinaryParser::ReadAllBytes(destination);
    io::SolutionData candidate;
    candidate.puzzleName = "worker-fixture";
    for (const auto* mode : {"changing", "changing-profile", "crash"}) {
        SCOPED_TRACE(mode);
        const auto report =
            io::PublishVerifiedSolution(mode, destination, candidate, FaultOptions());
        EXPECT_FALSE(report.Passed());
        EXPECT_EQ(io::BinaryParser::ReadAllBytes(destination), before);
        EXPECT_EQ(std::distance(std::filesystem::directory_iterator(files.directory),
                                std::filesystem::directory_iterator{}),
                  1);
    }
    auto options = FaultOptions();
    options.outerDeadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(100);
    EXPECT_EQ(io::PublishVerifiedSolution("hang", destination, candidate, options).status,
              io::VerificationStatus::Timeout);
    EXPECT_EQ(io::BinaryParser::ReadAllBytes(destination), before);
}
TEST(VerifierWorkerProcess, ShadowObservationCannotChangeAuthoritativePublication) {
    Files files;
    auto options = FaultOptions();
    options.mode = io::VerificationMode::Shadow;
    options.independentWorkerExecutable = OPUSOPTIMA_FAULT_WORKER_PATH;
    const auto report = io::VerifySolutionFiles("valid", "unused", options);
    ASSERT_TRUE(report.Passed()) << report.detail;
    ASSERT_TRUE(report.shadow);
    EXPECT_EQ(report.authority, io::VerificationAuthority::LegacyGate);
    EXPECT_EQ(report.shadow->status, io::VerificationStatus::Error);
    io::SolutionData candidate;
    const auto published = io::PublishVerifiedSolution(
        "valid", files.directory / "published.solution", candidate, options);
    ASSERT_TRUE(published.Passed()) << published.detail;
    ASSERT_TRUE(published.shadow);
    EXPECT_EQ(published.shadow->status, io::VerificationStatus::Error);
    EXPECT_TRUE(std::filesystem::exists(files.directory / "published.solution"));
}

TEST(VerifierWorkerProcess, ShadowUsesItsOwnAcceptedSnapshotWhenTheDestinationChanges) {
    Files files;
    const auto observer = files.directory / "shadow-observer";
    std::filesystem::copy_file(OPUSOPTIMA_FAULT_WORKER_PATH, observer);
    auto options = FaultOptions();
    options.mode = io::VerificationMode::Shadow;
    options.independentWorkerExecutable = observer;
    const auto destination = files.directory / "published.solution";
    io::SolutionData candidate;
    const auto report = io::PublishVerifiedSolution("valid", destination, candidate, options);
    ASSERT_TRUE(report.Passed()) << report.detail;
    ASSERT_TRUE(report.shadow);
    EXPECT_EQ(report.shadow->reasonCode, "shadow-private-snapshot");
    EXPECT_EQ(report.shadow->metrics.cycles, report.metrics.cycles);
    EXPECT_EQ(io::BinaryParser::ParseSolutionFile(destination).metrics.cycles,
              report.metrics.cycles + 1);
    for (const auto& entry : std::filesystem::directory_iterator(files.directory))
        EXPECT_EQ(entry.path().filename().string().find(".candidate-"), std::string::npos);
}

TEST(VerifierWorkerProcess, RepeatedRequestsDoNotLeakDescriptors) {
    const auto descriptors = [] {
        return std::distance(std::filesystem::directory_iterator("/proc/self/fd"),
                             std::filesystem::directory_iterator{});
    };
    const auto before = descriptors();
    for (int i = 0; i < 20; ++i) {
        const auto report = io::VerifySolutionFiles("valid", "unused", FaultOptions());
        ASSERT_TRUE(report.Passed()) << report.detail;
    }
    EXPECT_EQ(descriptors(), before);
}
TEST(VerifierWorkerProcess, SampledReplayCannotClaimIndependentCertification) {
    Files files;
    const auto puzzlePath = root / "asset/puzzle/edge-cases/c544897394273089.puzzle";
    const auto puzzle = io::BinaryParser::ParsePuzzleFile(puzzlePath);
    const auto machine = solvers::CompileProductionMolecular(puzzle);
    const auto solutionPath = files.directory / "replay.solution";
    io::BinaryWriter::WriteSolutionFile(solutionPath, machine);
    io::VerificationOptions options;
    options.mode = io::VerificationMode::Independent;
    const auto report = io::VerifySolutionFiles(puzzlePath, solutionPath, options);
    EXPECT_EQ(report.status, io::VerificationStatus::Unavailable) << report.detail;
    EXPECT_EQ(report.authority, io::VerificationAuthority::None);
    EXPECT_EQ(report.reasonCode, "independent-profile-uncertified");
    EXPECT_FALSE(io::PublishVerifiedSolution(
                     puzzlePath, files.directory / "must-not-publish.solution", machine, options)
                     .Passed());
    EXPECT_FALSE(std::filesystem::exists(files.directory / "must-not-publish.solution"));
}
#    ifdef HAS_LIBVERIFY
namespace {
class HoldingSink final : public spdlog::sinks::base_sink<std::mutex> {
    public:
    void Hold(std::promise<void>& ready, const std::shared_future<void>& release) {
        std::lock_guard lock(mutex_);
        ready.set_value();
        release.wait();
    }

    private:
    void sink_it_(const spdlog::details::log_msg&) override {}
    void flush_() override {}
};
} // namespace
TEST(VerifierWorkerProcess, ExecDoesNotInheritTheParentsHeldLoggingMutex) {
    auto sink = std::make_shared<HoldingSink>();
    auto previous = spdlog::get("io");
    spdlog::drop("io");
    spdlog::register_logger(std::make_shared<spdlog::logger>("io", sink));
    std::promise<void> ready, release;
    auto released = release.get_future().share();
    std::thread holder([&] { sink->Hold(ready, released); });
    ready.get_future().wait();
    const auto report = io::VerifySolutionFiles(
        root / "asset/puzzle/easy/easy-conduit.puzzle",
        root / "asset/solution/easy/easy-conduit-easy-conduit-1.solution", 10000, 2000);
    release.set_value();
    holder.join();
    spdlog::drop("io");
    if (previous) spdlog::register_logger(previous);
    ASSERT_TRUE(report.Passed()) << report.detail;
    EXPECT_EQ(report.authority, io::VerificationAuthority::LegacyGate);
}
TEST(VerifierWorkerProcess, ConcurrentCallsUseSeparateWorkersAndResults) {
    std::vector<std::future<io::VerificationReport>> requests;
    for (int i = 0; i < 4; ++i)
        requests.push_back(std::async(std::launch::async, [] {
            return io::VerifySolutionFiles(
                root / "asset/puzzle/easy/easy-conduit.puzzle",
                root / "asset/solution/easy/easy-conduit-easy-conduit-1.solution", 10000, 2000);
        }));
    for (auto& request : requests) {
        const auto report = request.get();
        ASSERT_TRUE(report.Passed()) << report.detail;
    }
}
#    endif
#endif
