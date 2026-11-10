#include "IO/SolutionVerifier.hpp"
#include "IO/VerificationJson.hpp"

#include <iostream>
#include <sys/resource.h>

int main(int argc, char** argv) {
    if (argc != 4 && argc != 5) return 4;
    opusoptima::io::VerificationOptions options;
    options.mode = opusoptima::io::VerificationMode::Independent;
    options.workerExecutable = std::filesystem::absolute(argv[3]);
    if (argc == 5) {
        if (std::string(argv[4]) != "--diagnostics") return 4;
        options.collectTimings = true;
    }
    const auto report = opusoptima::io::VerifySolutionFiles(argv[1], argv[2], options);
    rusage usage{};
    if (getrusage(RUSAGE_CHILDREN, &usage) != 0) return 4;
    std::cout << "{\"peakWorkerRssKiB\":" << usage.ru_maxrss
              << ",\"report\":" << opusoptima::io::VerificationReportJson(report) << "}\n";
    return opusoptima::io::VerificationExitCode(report.status);
}
