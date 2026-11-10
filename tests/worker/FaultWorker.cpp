#include "BinaryParser.hpp"
#include "BinaryWriter.hpp"
#include "Verification/WorkerProtocol.hpp"

#include <array>
#include <csignal>
#include <iostream>
#include <string>
#include <sys/resource.h>
#include <unistd.h>

int main(int argc, char* argv[]) {
    rlimit core{0, 0};
    setrlimit(RLIMIT_CORE, &core);
    if (argc != 4) return 2;
    const auto mode = std::filesystem::path(argv[1]).filename().string();
    if (mode == "hang")
        for (;;)
            pause();
    if (mode == "crash") raise(SIGSEGV);
    if (mode == "silent") return 0;
    const auto send = [](const std::uint8_t* data, std::size_t size) {
        while (size) {
            const auto written =
                write(opusoptima::verification::worker::kReportDescriptor, data, size);
            if (written < 0 && errno == EINTR) continue;
            if (written <= 0) return false;
            data += written;
            size -= static_cast<std::size_t>(written);
        }
        return true;
    };
    if (mode == "oversized") {
        std::array<std::uint8_t, opusoptima::verification::worker::kMaximumFrameBytes + 1> bytes{};
        return send(bytes.data(), bytes.size()) ? 0 : 1;
    }
    opusoptima::io::VerificationReport report;
    report.status = opusoptima::io::VerificationStatus::Passed;
    report.metrics.cycles = 1;
    report.metrics.cost = 10;
    report.metrics.area = 10;
    report.metrics.instructionCount = 0;
    report.authority = opusoptima::io::VerificationAuthority::LegacyGate;
    report.engineVersion = "fault-injection-v1";
    report.rulesetVersion = "test";
    report.capabilityProfile = "test-fixture";
    report.reasonCode = "completed";
    if (mode == "wrong-authority")
        report.authority = opusoptima::io::VerificationAuthority::Independent;
    if (mode == "large") report.detail.assign(32000, 'x');
    if (mode == "changing" || mode == "changing-profile") {
        const auto candidate = opusoptima::io::BinaryParser::ParseSolutionFile(argv[2]);
        if (mode == "changing")
            report.metrics.cycles = candidate.metrics.cycles + 1;
        else if (candidate.metrics.cycles)
            report.capabilityProfile = "changed";
    }
    if (mode == "descendant") {
        const auto child = fork();
        if (child == -1) return 3;
        if (child == 0)
            for (;;)
                pause();
        report.errorU = child;
    }
    if (std::filesystem::path(argv[0]).filename() == "shadow-observer") {
        auto observed = opusoptima::io::BinaryParser::ParseSolutionFile(argv[2]);
        const std::string source = argv[2];
        const auto marker = source.find(".candidate-shadow-");
        report.status = opusoptima::io::VerificationStatus::Rejected;
        report.authority = opusoptima::io::VerificationAuthority::None;
        report.reasonCode =
            marker == std::string::npos ? "shadow-shared-destination" : "shadow-private-snapshot";
        report.metrics = observed.metrics;
        if (marker != std::string::npos) {
            observed.metrics.cycles++;
            opusoptima::io::BinaryWriter::WriteSolutionFile(source.substr(0, marker), observed);
        }
    }
    std::cout << "stdout is not the protocol\n";
    if (mode == "truncated") {
        const auto bytes = opusoptima::verification::worker::EncodeReport(report);
        return send(bytes.data(), bytes.size() / 2) ? 0 : 1;
    }
    if (!opusoptima::verification::worker::WriteReport(
            opusoptima::verification::worker::kReportDescriptor, report))
        return 4;
    return mode == "exit-error" ? 7 : 0;
}
