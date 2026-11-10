#include "WorkerProtocol.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cerrno>
#include <charconv>
#include <stdexcept>
#include <string_view>
#include <utility>

#ifdef __linux__
#    include <sys/resource.h>
#    include <unistd.h>
#endif

namespace opusoptima::verification::worker {
namespace {
constexpr std::array<std::uint8_t, 8> magic{'O', 'P', 'U', 'S', 'V', 'R', 'F', 'Y'};
constexpr std::uint32_t version = kProtocolVersion;
constexpr std::size_t headerBytes = magic.size() + 2 * sizeof(std::uint32_t);

std::uint32_t StatusCode(io::VerificationStatus status) {
    switch (status) {
    case io::VerificationStatus::Passed:
        return 0;
    case io::VerificationStatus::Rejected:
        return 1;
    case io::VerificationStatus::CycleLimit:
        return 2;
    case io::VerificationStatus::Timeout:
        return 3;
    case io::VerificationStatus::Unavailable:
        return 4;
    case io::VerificationStatus::Error:
        return 5;
    }
    throw std::invalid_argument("Unknown verification status");
}
io::VerificationStatus Status(std::uint32_t code) {
    switch (code) {
    case 0:
        return io::VerificationStatus::Passed;
    case 1:
        return io::VerificationStatus::Rejected;
    case 2:
        return io::VerificationStatus::CycleLimit;
    case 3:
        return io::VerificationStatus::Timeout;
    case 4:
        return io::VerificationStatus::Unavailable;
    case 5:
        return io::VerificationStatus::Error;
    default:
        throw std::invalid_argument("Unknown worker status code");
    }
}
std::uint32_t AuthorityCode(io::VerificationAuthority authority) {
    switch (authority) {
    case io::VerificationAuthority::None:
        return 0;
    case io::VerificationAuthority::LegacyGate:
        return 1;
    case io::VerificationAuthority::Independent:
        return 2;
    }
    throw std::invalid_argument("Unknown verification authority");
}
io::VerificationAuthority Authority(std::uint32_t code) {
    switch (code) {
    case 0:
        return io::VerificationAuthority::None;
    case 1:
        return io::VerificationAuthority::LegacyGate;
    case 2:
        return io::VerificationAuthority::Independent;
    default:
        throw std::invalid_argument("Unknown worker authority code");
    }
}
void ValidateDiagnostics(const io::VerificationDiagnostics& diagnostics) {
    if (diagnostics.phase >= io::VerificationPhase::Count || diagnostics.cycle < 0 ||
        diagnostics.outputs.size() > 16 || diagnostics.trace.size() > io::kVerificationTraceLimit)
        throw std::invalid_argument("Worker diagnostics exceed supported bounds");
    for (const auto& event : diagnostics.trace)
        if (event.kind > 7 || event.phase > 4 || event.cycle < 0 ||
            event.atomCount > io::kVerificationTraceAtoms)
            throw std::invalid_argument("Invalid worker diagnostic trace event");
}
void Validate(const io::VerificationReport& report) {
    if (report.shadow)
        throw std::invalid_argument("Shadow observations belong to the supervising process");
    (void)StatusCode(report.status);
    (void)AuthorityCode(report.authority);
    if (report.diagnostics) ValidateDiagnostics(*report.diagnostics);
    if (report.Passed() &&
        (report.authority == io::VerificationAuthority::None || report.metrics.cycles < 0 ||
         report.engineVersion.empty() || report.rulesetVersion.empty() ||
         report.capabilityProfile.empty() || report.reasonCode.empty() || report.metrics.cost < 0 ||
         report.metrics.area < 0 || report.metrics.instructionCount < 0))
        throw std::invalid_argument("Passed worker report lacks authority or valid metrics");
}
class Writer {
    public:
    std::vector<std::uint8_t> bytes{magic.begin(), magic.end()};
    void Word(std::uint32_t value) {
        if (bytes.size() > kMaximumFrameBytes - sizeof(value))
            throw std::invalid_argument("Worker report exceeds frame bound");
        for (int byte = 0; byte < 4; ++byte)
            bytes.push_back(static_cast<std::uint8_t>(value >> (byte * 8)));
    }
    void QWord(std::uint64_t value) {
        Word(static_cast<std::uint32_t>(value));
        Word(static_cast<std::uint32_t>(value >> 32));
    }
    void Boolean(bool value) {
        Word(value ? 1 : 0);
    }
    void Signed(std::int32_t value) {
        Word(std::bit_cast<std::uint32_t>(value));
    }
    void Text(const std::string& value) {
        if (bytes.size() > kMaximumFrameBytes - sizeof(std::uint32_t) ||
            value.size() > kMaximumFrameBytes - bytes.size() - sizeof(std::uint32_t))
            throw std::invalid_argument("Worker string exceeds frame bound");
        Word(static_cast<std::uint32_t>(value.size()));
        bytes.insert(bytes.end(), value.begin(), value.end());
    }
};
class Reader {
    public:
    explicit Reader(std::span<const std::uint8_t> bytes) : bytes_(bytes) {}
    std::uint32_t Word() {
        if (bytes_.size() - position_ < sizeof(std::uint32_t))
            throw std::invalid_argument("Truncated worker word");
        std::uint32_t result = 0;
        for (int byte = 0; byte < 4; ++byte)
            result |= static_cast<std::uint32_t>(bytes_[position_++]) << (byte * 8);
        return result;
    }
    std::uint64_t QWord() {
        const auto low = Word();
        return std::uint64_t(low) | (std::uint64_t(Word()) << 32);
    }
    bool Boolean() {
        const auto value = Word();
        if (value > 1) throw std::invalid_argument("Invalid worker boolean");
        return value != 0;
    }
    std::int32_t Signed() {
        return std::bit_cast<std::int32_t>(Word());
    }
    std::string Text() {
        const auto size = Word();
        if (size > bytes_.size() - position_)
            throw std::invalid_argument("Truncated worker string");
        const auto* data = reinterpret_cast<const char*>(bytes_.data() + position_);
        position_ += size;
        return {data, size};
    }
    [[nodiscard]] bool Finished() const {
        return position_ == bytes_.size();
    }

    private:
    std::span<const std::uint8_t> bytes_;
    std::size_t position_{};
};
void WriteDiagnostics(Writer& writer, const io::VerificationDiagnostics& diagnostics) {
    writer.Word(static_cast<std::uint32_t>(diagnostics.phase));
    writer.Signed(diagnostics.cycle);
    writer.QWord(diagnostics.completedCycles);
    writer.QWord(diagnostics.inventory);
    writer.QWord(diagnostics.inventoryHighWater);
    writer.QWord(diagnostics.elapsedMicros);
    writer.QWord(diagnostics.parseMicros);
    writer.QWord(diagnostics.rasterMicros);
    writer.QWord(diagnostics.collisionMicros);
    writer.QWord(diagnostics.observeMicros);
    writer.QWord(diagnostics.snapshotMicros);
    writer.QWord(diagnostics.fragmentMicros);
    writer.QWord(diagnostics.heldMicros);
    writer.Boolean(diagnostics.timingsCollected);
    writer.Boolean(diagnostics.traceTruncated);
    for (const auto value : diagnostics.phaseMicros)
        writer.QWord(value);
    for (const auto value : diagnostics.counters)
        writer.QWord(value);
    writer.Word(static_cast<std::uint32_t>(diagnostics.outputs.size()));
    for (const auto value : diagnostics.outputs)
        writer.QWord(value);
    writer.Word(static_cast<std::uint32_t>(diagnostics.trace.size()));
    for (const auto& event : diagnostics.trace) {
        writer.Word(event.kind);
        writer.Word(event.phase);
        writer.Word(event.object);
        writer.Signed(event.cycle);
        writer.Word(event.atomCount);
        writer.Boolean(event.atomsTruncated);
        for (const auto atom : event.atoms)
            writer.QWord(atom);
    }
}
io::VerificationDiagnostics ReadDiagnostics(Reader& reader) {
    io::VerificationDiagnostics diagnostics;
    const auto phase = reader.Word();
    if (phase >= static_cast<std::uint32_t>(io::VerificationPhase::Count))
        throw std::invalid_argument("Unknown worker diagnostic phase");
    diagnostics.phase = static_cast<io::VerificationPhase>(phase);
    diagnostics.cycle = reader.Signed();
    if (diagnostics.cycle < 0) throw std::invalid_argument("Negative worker diagnostic cycle");
    diagnostics.completedCycles = reader.QWord();
    diagnostics.inventory = reader.QWord();
    diagnostics.inventoryHighWater = reader.QWord();
    diagnostics.elapsedMicros = reader.QWord();
    diagnostics.parseMicros = reader.QWord();
    diagnostics.rasterMicros = reader.QWord();
    diagnostics.collisionMicros = reader.QWord();
    diagnostics.observeMicros = reader.QWord();
    diagnostics.snapshotMicros = reader.QWord();
    diagnostics.fragmentMicros = reader.QWord();
    diagnostics.heldMicros = reader.QWord();
    diagnostics.timingsCollected = reader.Boolean();
    diagnostics.traceTruncated = reader.Boolean();
    for (auto& value : diagnostics.phaseMicros)
        value = reader.QWord();
    for (auto& value : diagnostics.counters)
        value = reader.QWord();
    const auto outputCount = reader.Word();
    if (outputCount > 16) throw std::invalid_argument("Worker diagnostic output bound exceeded");
    diagnostics.outputs.reserve(outputCount);
    for (std::uint32_t i = 0; i < outputCount; ++i)
        diagnostics.outputs.push_back(reader.QWord());
    const auto traceCount = reader.Word();
    if (traceCount > io::kVerificationTraceLimit)
        throw std::invalid_argument("Worker diagnostic trace bound exceeded");
    diagnostics.trace.reserve(traceCount);
    for (std::uint32_t i = 0; i < traceCount; ++i) {
        io::VerificationTrace event;
        event.kind = reader.Word();
        event.phase = reader.Word();
        event.object = reader.Word();
        event.cycle = reader.Signed();
        event.atomCount = reader.Word();
        if (event.kind > 7 || event.phase > 4 || event.cycle < 0 ||
            event.atomCount > io::kVerificationTraceAtoms)
            throw std::invalid_argument("Invalid worker diagnostic trace event");
        event.atomsTruncated = reader.Boolean();
        for (auto& atom : event.atoms)
            atom = reader.QWord();
        diagnostics.trace.push_back(event);
    }
    return diagnostics;
}
} // namespace

std::vector<std::uint8_t> EncodeReport(const io::VerificationReport& report) {
    Validate(report);
    Writer writer;
    writer.Word(version);
    writer.Word(0);
    writer.Word(StatusCode(report.status));
    writer.Word(AuthorityCode(report.authority));
    writer.Signed(report.metrics.cycles);
    writer.Signed(report.metrics.cost);
    writer.Signed(report.metrics.area);
    writer.Signed(report.metrics.instructionCount);
    writer.Signed(report.errorCycle);
    writer.Signed(report.errorU);
    writer.Signed(report.errorV);
    writer.Text(report.detail);
    writer.Text(report.engineVersion);
    writer.Text(report.rulesetVersion);
    writer.Text(report.capabilityProfile);
    writer.Text(report.reasonCode);
    writer.Boolean(report.diagnostics.has_value());
    if (report.diagnostics) WriteDiagnostics(writer, *report.diagnostics);
    const auto payloadBytes = static_cast<std::uint32_t>(writer.bytes.size() - headerBytes);
    for (int byte = 0; byte < 4; ++byte)
        writer.bytes[magic.size() + sizeof(std::uint32_t) + byte] =
            static_cast<std::uint8_t>(payloadBytes >> (byte * 8));
    return std::move(writer.bytes);
}

io::VerificationReport DecodeReport(std::span<const std::uint8_t> frame) {
    if (frame.size() < headerBytes || frame.size() > kMaximumFrameBytes)
        throw std::invalid_argument("Worker frame size outside supported bound");
    if (!std::equal(magic.begin(), magic.end(), frame.begin()))
        throw std::invalid_argument("Worker frame magic mismatch");
    Reader header(frame.subspan(magic.size(), 2 * sizeof(std::uint32_t)));
    if (header.Word() != version) throw std::invalid_argument("Worker protocol version mismatch");
    if (header.Word() != frame.size() - headerBytes)
        throw std::invalid_argument("Worker frame length mismatch");
    Reader reader(frame.subspan(headerBytes));
    io::VerificationReport report;
    report.status = Status(reader.Word());
    report.authority = Authority(reader.Word());
    report.metrics.cycles = reader.Signed();
    report.metrics.cost = reader.Signed();
    report.metrics.area = reader.Signed();
    report.metrics.instructionCount = reader.Signed();
    report.errorCycle = reader.Signed();
    report.errorU = reader.Signed();
    report.errorV = reader.Signed();
    report.detail = reader.Text();
    report.engineVersion = reader.Text();
    report.rulesetVersion = reader.Text();
    report.capabilityProfile = reader.Text();
    report.reasonCode = reader.Text();
    if (reader.Boolean()) report.diagnostics = ReadDiagnostics(reader);
    if (!reader.Finished()) throw std::invalid_argument("Unexpected trailing worker payload");
    Validate(report);
    return report;
}

bool WriteReport(int descriptor, const io::VerificationReport& report) {
#ifndef __linux__
    (void)descriptor;
    (void)report;
    return false;
#else
    std::vector<std::uint8_t> frame;
    try {
        frame = EncodeReport(report);
    } catch (const std::exception&) {
        return false;
    }
    std::size_t position = 0;
    while (position < frame.size()) {
        const auto written = write(descriptor, frame.data() + position, frame.size() - position);
        if (written < 0 && errno == EINTR) continue;
        if (written <= 0) return false;
        position += static_cast<std::size_t>(written);
    }
    return true;
#endif
}

int RunWorker(int argc, char* argv[], Evaluator evaluate) {
#ifdef __linux__
    const rlimit coreLimit{0, 0};
    if (setrlimit(RLIMIT_CORE, &coreLimit) != 0) return 2;
#endif
    if ((argc != 4 && argc != 5) || !evaluate) return 2;
    const bool collectTimings = argc == 5;
    if (collectTimings && std::string_view(argv[4]) != "--timings") return 2;
    int cycleLimit = 0;
    const std::string_view cycles(argv[3]);
    const auto parsed = std::from_chars(cycles.data(), cycles.data() + cycles.size(), cycleLimit);
    if (parsed.ec != std::errc{} || parsed.ptr != cycles.data() + cycles.size() || cycleLimit <= 0)
        return 2;
    io::VerificationReport report;
    try {
        report = evaluate(std::filesystem::path(argv[1]), std::filesystem::path(argv[2]),
                          cycleLimit, collectTimings);
    } catch (const std::exception& error) {
        report.status = io::VerificationStatus::Error;
        report.detail = std::string("Verifier worker failed: ") + error.what();
        report.reasonCode = "worker-evaluation-error";
    } catch (...) {
        report.status = io::VerificationStatus::Error;
        report.detail = "Unknown verifier worker failure";
        report.reasonCode = "worker-evaluation-error";
    }
    return WriteReport(kReportDescriptor, report) ? 0 : 3;
}
} // namespace opusoptima::verification::worker
