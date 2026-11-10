#ifndef _GNU_SOURCE
#    define _GNU_SOURCE
#endif
#include "SolutionVerifier.hpp"

#include "BinaryWriter.hpp"
#include "Verification/IndependentCapabilities.hpp"
#include "Verification/WorkerProtocol.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

#if defined(__linux__)
#    include <csignal>
#    include <fcntl.h>
#    include <poll.h>
#    include <spawn.h>
#    include <sys/syscall.h>
#    include <sys/wait.h>
#    include <unistd.h>
extern char** environ;
#endif

#ifndef OPUSOPTIMA_REFERENCE_WORKER_PATH
#    define OPUSOPTIMA_REFERENCE_WORKER_PATH ""
#endif
#ifndef OPUSOPTIMA_INDEPENDENT_WORKER_PATH
#    define OPUSOPTIMA_INDEPENDENT_WORKER_PATH ""
#endif

namespace opusoptima::io {
namespace {
using Clock = std::chrono::steady_clock;
VerificationReport Failure(VerificationStatus status, std::string detail, std::string reason) {
    VerificationReport result{status, {}, std::move(detail)};
    result.reasonCode = std::move(reason);
    return result;
}
std::filesystem::path WorkerExecutable(const VerificationOptions& options) {
    if (!options.workerExecutable.empty()) return options.workerExecutable;
    return options.mode == VerificationMode::Independent ? OPUSOPTIMA_INDEPENDENT_WORKER_PATH
                                                         : OPUSOPTIMA_REFERENCE_WORKER_PATH;
}
Clock::time_point Deadline(const VerificationOptions& options, Clock::time_point now,
                           std::int64_t milliseconds) {
    const auto deadline = now + std::chrono::milliseconds(milliseconds);
    return options.outerDeadline ? std::min(deadline, *options.outerDeadline) : deadline;
}
VerificationObservation Observation(const VerificationReport& report) {
    return {report.status,
            report.metrics,
            report.detail,
            report.authority,
            report.engineVersion,
            report.rulesetVersion,
            report.capabilityProfile,
            report.reasonCode,
            report.diagnostics};
}
VerificationOptions ShadowOptions(const VerificationOptions& options, Clock::time_point deadline) {
    auto independent = options;
    independent.mode = VerificationMode::Independent;
    independent.workerExecutable = options.independentWorkerExecutable;
    independent.outerDeadline = deadline;
    return independent;
}
bool SameMetrics(const SolutionMetrics& a, const SolutionMetrics& b) {
    return a.cycles == b.cycles && a.cost == b.cost && a.area == b.area &&
           a.instructionCount == b.instructionCount;
}
#if defined(__linux__)
struct Descriptor {
    int value{-1};
    void Close() {
        if (value >= 0) close(std::exchange(value, -1));
    }
    ~Descriptor() {
        Close();
    }
};
struct SpawnConfiguration {
    posix_spawn_file_actions_t actions{};
    posix_spawnattr_t attributes{};
    bool actionsReady{}, attributesReady{};
    ~SpawnConfiguration() {
        if (actionsReady) posix_spawn_file_actions_destroy(&actions);
        if (attributesReady) posix_spawnattr_destroy(&attributes);
    }
};
struct Child {
    pid_t pid;
    bool reaped{};
    int status{};
    bool Reap() {
        if (reaped) return true;
        siginfo_t information{};
        if (waitid(P_PID, pid, &information, WEXITED | WNOHANG | WNOWAIT) == -1 &&
            errno == ECHILD) {
            reaped = true;
            return false;
        }
        // WNOWAIT keeps this PID reserved until process-group cleanup completes.
        kill(-pid, SIGKILL);
        pid_t result;
        do {
            result = waitpid(pid, &status, 0);
        } while (result == -1 && errno == EINTR);
        reaped = true;
        return result == pid;
    }
    ~Child() {
        if (!reaped) Reap();
    }
};
VerificationReport RunWorker(const std::filesystem::path& executable,
                             const std::filesystem::path& puzzle,
                             const std::filesystem::path& solution,
                             const VerificationOptions& options, Clock::time_point deadline) {
    int raw[2];
    if (pipe2(raw, O_CLOEXEC) != 0)
        return Failure(VerificationStatus::Error, "Cannot create verifier pipe",
                       "worker-pipe-failed");
    Descriptor originalRead{raw[0]}, originalWrite{raw[1]};
    Descriptor readEnd{fcntl(originalRead.value, F_DUPFD_CLOEXEC, 4)};
    Descriptor writeEnd{fcntl(originalWrite.value, F_DUPFD_CLOEXEC, 4)};
    if (readEnd.value < 0 || writeEnd.value < 0)
        return Failure(VerificationStatus::Error, "Cannot prepare verifier descriptors",
                       "worker-pipe-failed");
    originalRead.Close();
    originalWrite.Close();
    const auto flags = fcntl(readEnd.value, F_GETFL);
    if (flags == -1 || fcntl(readEnd.value, F_SETFL, flags | O_NONBLOCK) == -1)
        return Failure(VerificationStatus::Error, "Cannot configure verifier pipe",
                       "worker-pipe-failed");
    SpawnConfiguration spawn;
    int error = posix_spawn_file_actions_init(&spawn.actions);
    if (error)
        return Failure(VerificationStatus::Error, "Cannot initialize worker actions",
                       "worker-start-failed");
    spawn.actionsReady = true;
    error = posix_spawnattr_init(&spawn.attributes);
    if (error)
        return Failure(VerificationStatus::Error, "Cannot initialize worker attributes",
                       "worker-start-failed");
    spawn.attributesReady = true;
    error = posix_spawn_file_actions_adddup2(&spawn.actions, writeEnd.value,
                                             verification::worker::kReportDescriptor);
    for (int descriptor = 0; descriptor <= 2 && !error; ++descriptor)
        error = posix_spawn_file_actions_addopen(&spawn.actions, descriptor, "/dev/null",
                                                 descriptor == 0 ? O_RDONLY : O_WRONLY, 0);
    if (!error) error = posix_spawn_file_actions_addclosefrom_np(&spawn.actions, 4);
    sigset_t mask, defaults;
    sigemptyset(&mask);
    sigemptyset(&defaults);
    sigaddset(&defaults, SIGPIPE);
    sigaddset(&defaults, SIGTERM);
    sigaddset(&defaults, SIGINT);
    if (!error) error = posix_spawnattr_setsigmask(&spawn.attributes, &mask);
    if (!error) error = posix_spawnattr_setsigdefault(&spawn.attributes, &defaults);
    if (!error) error = posix_spawnattr_setpgroup(&spawn.attributes, 0);
    if (!error)
        error = posix_spawnattr_setflags(&spawn.attributes, POSIX_SPAWN_SETPGROUP |
                                                                POSIX_SPAWN_SETSIGMASK |
                                                                POSIX_SPAWN_SETSIGDEF);
    if (error)
        return Failure(VerificationStatus::Error,
                       "Cannot configure verifier worker: " + std::string(std::strerror(error)),
                       "worker-start-failed");
    std::vector<std::string> arguments{
        executable.string(), std::filesystem::absolute(puzzle).string(),
        std::filesystem::absolute(solution).string(), std::to_string(options.cycleLimit)};
    if (options.collectTimings) arguments.emplace_back("--timings");
    std::vector<char*> argv;
    for (auto& argument : arguments)
        argv.push_back(argument.data());
    argv.push_back(nullptr);
    pid_t pid;
    error = posix_spawn(&pid, executable.c_str(), &spawn.actions, &spawn.attributes, argv.data(),
                        environ);
    if (error)
        return Failure(VerificationStatus::Error,
                       "Cannot start verifier worker: " + std::string(std::strerror(error)),
                       "worker-start-failed");
    Child child{pid};
#    ifdef SYS_pidfd_open
    Descriptor exitEvent{static_cast<int>(syscall(SYS_pidfd_open, pid, 0))};
#    else
    Descriptor exitEvent;
#    endif
    writeEnd.Close();
    std::vector<std::uint8_t> bytes;
    bytes.reserve(1024);
    bool eof = false;
    for (;;) {
        std::array<std::uint8_t, 4096> buffer;
        for (;;) {
            const auto count = read(readEnd.value, buffer.data(), buffer.size());
            if (count > 0) {
                if (bytes.size() + static_cast<std::size_t>(count) >
                    verification::worker::kMaximumFrameBytes)
                    return Failure(VerificationStatus::Error,
                                   "Verifier result exceeded protocol limit",
                                   "worker-protocol-invalid");
                bytes.insert(bytes.end(), buffer.begin(), buffer.begin() + count);
            } else if (count == 0) {
                eof = true;
                break;
            } else if (errno == EINTR)
                continue;
            else if (errno == EAGAIN || errno == EWOULDBLOCK)
                break;
            else
                return Failure(VerificationStatus::Error, "Cannot read verifier result",
                               "worker-read-failed");
        }
        if (!child.reaped) {
            siginfo_t information{};
            if (waitid(P_PID, child.pid, &information, WEXITED | WNOHANG | WNOWAIT) == -1) {
                if (errno == EINTR) continue;
                return Failure(VerificationStatus::Error, "Cannot supervise verifier worker",
                               "worker-wait-failed");
            }
            if (information.si_pid == child.pid && !child.Reap())
                return Failure(VerificationStatus::Error, "Cannot reap verifier worker",
                               "worker-wait-failed");
        }
        const auto remaining =
            std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now()).count();
        if (remaining <= 0)
            return Failure(VerificationStatus::Timeout, "Verifier wall-clock timeout",
                           "worker-timeout");
        if (child.reaped && eof) break;
        std::array<pollfd, 2> events{{{eof ? -1 : readEnd.value, POLLIN | POLLHUP, 0},
                                      {child.reaped ? -1 : exitEvent.value, POLLIN, 0}}};
        const auto interval = eof && exitEvent.value < 0 ? 2 : 50;
        const auto polled = poll(events.data(), events.size(),
                                 static_cast<int>(std::min<std::int64_t>(remaining, interval)));
        if (polled == -1 && errno != EINTR)
            return Failure(VerificationStatus::Error, "Cannot poll verifier worker",
                           "worker-wait-failed");
    }
    if (!WIFEXITED(child.status) || WEXITSTATUS(child.status) != 0)
        return Failure(VerificationStatus::Error,
                       "Verifier worker crashed or exited unsuccessfully", "worker-crashed");
    try {
        auto report = verification::worker::DecodeReport(bytes);
        if (Clock::now() >= deadline)
            return Failure(VerificationStatus::Timeout, "Verifier wall-clock timeout",
                           "worker-timeout");
        const auto expected = options.mode == VerificationMode::Independent
                                  ? VerificationAuthority::Independent
                                  : VerificationAuthority::LegacyGate;
        if (report.Passed() && report.authority != expected)
            return Failure(VerificationStatus::Error, "Verifier returned an unexpected authority",
                           "worker-protocol-invalid");
        if (report.Passed() && report.authority == VerificationAuthority::Independent &&
            std::none_of(verification::independentProfiles.begin(),
                         verification::independentProfiles.end(), [&](const auto& profile) {
                             return profile.id == report.capabilityProfile &&
                                    profile.rulesetVersion == report.rulesetVersion;
                         }))
            return Failure(VerificationStatus::Error,
                           "Verifier returned an unregistered independent capability profile",
                           "worker-protocol-invalid");
        return report;
    } catch (const std::exception& failure) {
        return Failure(VerificationStatus::Error,
                       std::string("Invalid verifier result: ") + failure.what(),
                       "worker-protocol-invalid");
    }
}
#endif
} // namespace

VerificationReport VerifySolutionFiles(const std::filesystem::path& puzzle,
                                       const std::filesystem::path& solution,
                                       const VerificationOptions& options) {
    const auto start = Clock::now();
    if (options.cycleLimit <= 0 || options.timeoutMilliseconds <= 0)
        return Failure(VerificationStatus::Error, "Verification budgets must be positive",
                       "invalid-budget");
    if (options.mode != VerificationMode::LegacyGate &&
        options.mode != VerificationMode::Independent && options.mode != VerificationMode::Shadow)
        return Failure(VerificationStatus::Error, "Unknown verification mode", "invalid-mode");
    const auto deadline = Deadline(options, start, options.timeoutMilliseconds);
    if (deadline <= start)
        return Failure(VerificationStatus::Timeout, "Verification deadline already exhausted",
                       "worker-timeout");
    if (options.mode == VerificationMode::Shadow) {
        auto primary = options;
        primary.mode = VerificationMode::LegacyGate;
        primary.outerDeadline = deadline;
        auto report = VerifySolutionFiles(puzzle, solution, primary);
        report.shadow =
            Observation(VerifySolutionFiles(puzzle, solution, ShadowOptions(options, deadline)));
        return report;
    }
    const auto executable = WorkerExecutable(options);
    if (executable.empty())
        return Failure(VerificationStatus::Unavailable,
                       "libverify is unavailable; reference worker was not built",
                       "reference-unavailable");
    if (!executable.is_absolute())
        return Failure(VerificationStatus::Error, "Verifier worker path must be absolute",
                       "worker-path-invalid");
#if defined(__linux__)
    try {
        return RunWorker(executable, puzzle, solution, options, deadline);
    } catch (const std::exception& failure) {
        return Failure(VerificationStatus::Error,
                       std::string("Verifier supervision failed: ") + failure.what(),
                       "worker-supervision-failed");
    }
#else
    return Failure(VerificationStatus::Unavailable, "Exec verification workers require Linux",
                   "worker-platform-unavailable");
#endif
}
VerificationReport VerifySolutionFiles(const std::filesystem::path& puzzle,
                                       const std::filesystem::path& solution, int cycleLimit,
                                       int timeoutMilliseconds) {
    VerificationOptions options;
    options.cycleLimit = cycleLimit;
    options.timeoutMilliseconds = timeoutMilliseconds;
    return VerifySolutionFiles(puzzle, solution, options);
}
VerificationReport PublishVerifiedSolution(const std::filesystem::path& puzzle,
                                           const std::filesystem::path& destination,
                                           const SolutionData& solution) {
    return PublishVerifiedSolution(puzzle, destination, solution, VerificationOptions{});
}
VerificationReport PublishVerifiedSolution(const std::filesystem::path& puzzle,
                                           const std::filesystem::path& destination,
                                           const SolutionData& solution,
                                           const VerificationOptions& options) {
    if (options.cycleLimit <= 0 || options.timeoutMilliseconds <= 0)
        return Failure(VerificationStatus::Error, "Verification budgets must be positive",
                       "invalid-budget");
    if (WorkerExecutable(options).empty())
        return Failure(VerificationStatus::Unavailable,
                       "libverify is unavailable; refusing to publish", "reference-unavailable");
    auto bounded = options;
    if (bounded.mode == VerificationMode::Shadow) bounded.mode = VerificationMode::LegacyGate;
    bounded.outerDeadline = Deadline(options, Clock::now(), 2LL * options.timeoutMilliseconds);
    if (*bounded.outerDeadline <= Clock::now())
        return Failure(VerificationStatus::Timeout, "Publication deadline already exhausted",
                       "worker-timeout");
#if defined(__linux__)
    auto pattern = destination.string() + ".candidate-XXXXXX";
    std::vector<char> name(pattern.begin(), pattern.end());
    name.push_back('\0');
    const int fd = mkstemp(name.data());
    if (fd == -1)
        throw std::runtime_error("Cannot create candidate next to output: " + destination.string());
    close(fd);
    const std::filesystem::path candidate(name.data());
    struct Cleanup {
        std::filesystem::path path;
        ~Cleanup() {
            std::error_code error;
            std::filesystem::remove(path, error);
        }
    } cleanup{candidate};
    BinaryWriter::WriteSolutionFile(candidate, solution);
    auto report = VerifySolutionFiles(puzzle, candidate, bounded);
    if (!report.Passed()) return report;
    const auto first = report;
    auto measured = solution;
    measured.metrics = first.metrics;
    BinaryWriter::WriteSolutionFile(candidate, measured);
    report = VerifySolutionFiles(puzzle, candidate, bounded);
    if (!report.Passed()) return report;
    if (!SameMetrics(first.metrics, report.metrics) || first.authority != report.authority ||
        first.engineVersion != report.engineVersion ||
        first.rulesetVersion != report.rulesetVersion ||
        first.capabilityProfile != report.capabilityProfile)
        return Failure(VerificationStatus::Error,
                       "Verifier metrics or provenance changed after metric rewrite",
                       "publication-result-changed");
    if (Clock::now() >= *bounded.outerDeadline)
        return Failure(VerificationStatus::Timeout, "Publication deadline exhausted",
                       "worker-timeout");
    std::filesystem::rename(candidate, destination);
    if (options.mode == VerificationMode::Shadow) {
        try {
            auto shadowPattern = destination.string() + ".candidate-shadow-XXXXXX";
            std::vector<char> shadowName(shadowPattern.begin(), shadowPattern.end());
            shadowName.push_back('\0');
            const int shadowFd = mkstemp(shadowName.data());
            if (shadowFd < 0) throw std::runtime_error("Cannot retain a private shadow candidate");
            close(shadowFd);
            const std::filesystem::path shadowCandidate(shadowName.data());
            Cleanup shadowCleanup{shadowCandidate};
            // Re-encode the exact accepted value; another publisher can replace the destination
            // meanwhile.
            BinaryWriter::WriteSolutionFile(shadowCandidate, measured);
            report.shadow = Observation(VerifySolutionFiles(
                puzzle, shadowCandidate, ShadowOptions(options, *bounded.outerDeadline)));
        } catch (const std::exception& error) {
            report.shadow = Observation(
                Failure(VerificationStatus::Error, error.what(), "shadow-snapshot-error"));
        }
    }
    return report;
#else
    return Failure(VerificationStatus::Unavailable, "Verified publication requires Linux",
                   "worker-platform-unavailable");
#endif
}
} // namespace opusoptima::io
