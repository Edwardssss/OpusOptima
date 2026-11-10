#include "VerificationJson.hpp"

#include <array>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace opusoptima::io {
std::string_view VerificationStatusName(VerificationStatus status) {
    switch (status) {
    case VerificationStatus::Passed:
        return "passed";
    case VerificationStatus::Rejected:
        return "rejected";
    case VerificationStatus::CycleLimit:
        return "cycle-limit";
    case VerificationStatus::Timeout:
        return "timeout";
    case VerificationStatus::Unavailable:
        return "unavailable";
    case VerificationStatus::Error:
        return "error";
    }
    throw std::invalid_argument("Unknown verification status");
}
std::string_view VerificationAuthorityName(VerificationAuthority authority) {
    switch (authority) {
    case VerificationAuthority::None:
        return "none";
    case VerificationAuthority::LegacyGate:
        return "legacy-gate";
    case VerificationAuthority::Independent:
        return "independent";
    }
    throw std::invalid_argument("Unknown verification authority");
}
std::string_view VerificationPhaseName(VerificationPhase phase) {
    constexpr std::array names{"setup",   "grabDrop", "spawn",      "glyphs",
                               "outputs", "motions",  "boundaries", "finish"};
    static_assert(names.size() == static_cast<std::size_t>(VerificationPhase::Count));
    const auto index = static_cast<std::size_t>(phase);
    if (index >= names.size()) throw std::invalid_argument("Unknown verification phase");
    return names[index];
}
int VerificationExitCode(VerificationStatus status) {
    switch (status) {
    case VerificationStatus::Passed:
        return 0;
    case VerificationStatus::Rejected:
        return 1;
    case VerificationStatus::Unavailable:
        return 2;
    case VerificationStatus::CycleLimit:
    case VerificationStatus::Timeout:
        return 3;
    case VerificationStatus::Error:
        return 4;
    }
    return 4;
}
namespace {
std::string Quote(std::string_view text) {
    std::ostringstream stream;
    stream << '"';
    for (std::size_t i = 0; i < text.size(); ++i) {
        const auto c = static_cast<unsigned char>(text[i]);
        if (c == '"' || c == '\\') {
            stream << '\\' << static_cast<char>(c);
            continue;
        }
        if (c >= 32 && c < 128) {
            stream << static_cast<char>(c);
            continue;
        }
        if (c >= 128) {
            const std::size_t count = c >= 0xc2 && c <= 0xdf   ? 2
                                      : c >= 0xe0 && c <= 0xef ? 3
                                      : c >= 0xf0 && c <= 0xf4 ? 4
                                                               : 0;
            bool valid = count && i + count <= text.size();
            for (std::size_t j = 1; j < count && valid; ++j) {
                const auto next = static_cast<unsigned char>(text[i + j]);
                valid = next >= 0x80 && next <= 0xbf;
            }
            if (valid) {
                const auto next = static_cast<unsigned char>(text[i + 1]);
                valid = !(c == 0xe0 && next < 0xa0) && !(c == 0xed && next >= 0xa0) &&
                        !(c == 0xf0 && next < 0x90) && !(c == 0xf4 && next >= 0x90);
            }
            if (valid) {
                stream << text.substr(i, count);
                i += count - 1;
                continue;
            }
        }
        stream << "\\u" << std::hex << std::setw(4) << std::setfill('0') << static_cast<unsigned>(c)
               << std::dec;
    }
    stream << '"';
    return stream.str();
}
void Diagnostics(std::ostringstream& stream, const VerificationDiagnostics& diagnostics) {
    constexpr std::array counterNames{
        "atomLookups",    "fragmentQueries",  "heldQueries",       "fragmentEdgeVisits",
        "collisionPairs", "kernelCalls",      "kernelLinear",      "kernelRigid",
        "kernelBroad",    "kernelSegments",   "kernelMaxDepth",    "kernelUncertain",
        "rasterPoints",   "rasterCandidates", "metricMarkedCells", "motionFrames"};
    static_assert(counterNames.size() == static_cast<std::size_t>(VerificationCounter::Count));
    if (diagnostics.cycle < 0 || diagnostics.outputs.size() > 16 ||
        diagnostics.trace.size() > kVerificationTraceLimit)
        throw std::invalid_argument("Verification diagnostics exceed JSON bounds");
    stream << "{\"phase\":" << Quote(VerificationPhaseName(diagnostics.phase))
           << ",\"cycle\":" << diagnostics.cycle
           << ",\"completedCycles\":" << diagnostics.completedCycles
           << ",\"inventory\":" << diagnostics.inventory
           << ",\"inventoryHighWater\":" << diagnostics.inventoryHighWater
           << ",\"elapsedMicros\":" << diagnostics.elapsedMicros
           << ",\"parseMicros\":" << diagnostics.parseMicros
           << ",\"rasterMicros\":" << diagnostics.rasterMicros
           << ",\"collisionMicros\":" << diagnostics.collisionMicros
           << ",\"observeMicros\":" << diagnostics.observeMicros
           << ",\"snapshotMicros\":" << diagnostics.snapshotMicros
           << ",\"fragmentMicros\":" << diagnostics.fragmentMicros
           << ",\"heldMicros\":" << diagnostics.heldMicros
           << ",\"timingsCollected\":" << (diagnostics.timingsCollected ? "true" : "false")
           << ",\"phaseMicros\":{";
    for (std::size_t i = 0; i < diagnostics.phaseMicros.size(); ++i) {
        if (i) stream << ',';
        stream << Quote(VerificationPhaseName(static_cast<VerificationPhase>(i))) << ':'
               << diagnostics.phaseMicros[i];
    }
    stream << "},\"counters\":{";
    for (std::size_t i = 0; i < diagnostics.counters.size(); ++i) {
        if (i) stream << ',';
        stream << Quote(counterNames[i]) << ':' << diagnostics.counters[i];
    }
    stream << "},\"outputs\":[";
    for (std::size_t i = 0; i < diagnostics.outputs.size(); ++i) {
        if (i) stream << ',';
        stream << diagnostics.outputs[i];
    }
    stream << "],\"trace\":[";
    for (std::size_t i = 0; i < diagnostics.trace.size(); ++i) {
        const auto& event = diagnostics.trace[i];
        if (event.kind > 7 || event.phase > 4 || event.cycle < 0 ||
            event.atomCount > kVerificationTraceAtoms)
            throw std::invalid_argument("Invalid JSON verification trace event");
        if (i) stream << ',';
        stream << "{\"cycle\":" << event.cycle << ",\"kind\":" << event.kind
               << ",\"phase\":" << event.phase << ",\"object\":" << event.object
               << ",\"atomCount\":" << event.atomCount << ",\"atoms\":[";
        for (std::size_t atom = 0; atom < event.atomCount; ++atom) {
            if (atom) stream << ',';
            stream << event.atoms[atom];
        }
        stream << "],\"atomsTruncated\":" << (event.atomsTruncated ? "true" : "false") << '}';
    }
    stream << "],\"traceTruncated\":" << (diagnostics.traceTruncated ? "true" : "false") << '}';
}
template <class Report> void Fields(std::ostringstream& stream, const Report& report) {
    stream << "\"status\":" << Quote(VerificationStatusName(report.status))
           << ",\"authority\":" << Quote(VerificationAuthorityName(report.authority))
           << ",\"engineVersion\":" << Quote(report.engineVersion)
           << ",\"rulesetVersion\":" << Quote(report.rulesetVersion) << ",\"profile\":"
           << (report.capabilityProfile.empty() ? "null" : Quote(report.capabilityProfile))
           << ",\"reasonCode\":" << Quote(report.reasonCode)
           << ",\"detail\":" << Quote(report.detail) << ",\"metrics\":";
    if (report.status != VerificationStatus::Passed)
        stream << "null";
    else
        stream << "{\"cycles\":" << report.metrics.cycles << ",\"cost\":" << report.metrics.cost
               << ",\"area\":" << report.metrics.area
               << ",\"instructions\":" << report.metrics.instructionCount << '}';
    if (report.diagnostics) {
        stream << ",\"diagnostics\":";
        Diagnostics(stream, *report.diagnostics);
    }
}
} // namespace
std::string VerificationReportJson(const VerificationReport& report) {
    std::ostringstream stream;
    stream << "{\"schemaVersion\":1,";
    Fields(stream, report);
    stream << ",\"errorCycle\":" << report.errorCycle << ",\"errorLocation\":[" << report.errorU
           << ',' << report.errorV << ']';
    if (report.shadow) {
        stream << ",\"shadow\":{";
        Fields(stream, *report.shadow);
        stream << '}';
    }
    stream << '}';
    return stream.str();
}
} // namespace opusoptima::io
