#include "IO/VerificationJson.hpp"
#include "Verification/WorkerProtocol.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <limits>
#include <stdexcept>
#include <utility>

namespace {
using namespace opusoptima;
namespace protocol = verification::worker;
io::VerificationReport Report() {
    io::VerificationReport report;
    report.status = io::VerificationStatus::Passed;
    report.authority = io::VerificationAuthority::LegacyGate;
    report.metrics = {43, 60, 67, 4};
    report.detail = "independent static-production execution completed every ordinary output";
    report.errorCycle = -1;
    report.errorU = std::numeric_limits<int>::min();
    report.errorV = std::numeric_limits<int>::max();
    report.engineVersion = "reference-v1";
    report.rulesetVersion = "rules-v1";
    report.capabilityProfile = "bounded-production";
    report.reasonCode = "passed";
    return report;
}
io::VerificationDiagnostics Diagnostics() {
    io::VerificationDiagnostics diagnostics;
    diagnostics.phase = io::VerificationPhase::Motions;
    diagnostics.cycle = 499;
    diagnostics.completedCycles = 498;
    diagnostics.inventory = 12;
    diagnostics.inventoryHighWater = 20;
    diagnostics.elapsedMicros = 1999000;
    diagnostics.parseMicros = 51;
    diagnostics.rasterMicros = 201;
    diagnostics.collisionMicros = 299;
    diagnostics.observeMicros = 137;
    diagnostics.snapshotMicros = 89;
    diagnostics.fragmentMicros = 73;
    diagnostics.heldMicros = 101;
    diagnostics.timingsCollected = true;
    diagnostics.traceTruncated = true;
    for (std::size_t i = 0; i < diagnostics.phaseMicros.size(); ++i)
        diagnostics.phaseMicros[i] = i * 100;
    for (std::size_t i = 0; i < diagnostics.counters.size(); ++i)
        diagnostics.counters[i] = i + 1;
    diagnostics.counters.back() = std::numeric_limits<std::uint64_t>::max();
    diagnostics.outputs.resize(16);
    for (std::size_t i = 0; i < diagnostics.outputs.size(); ++i)
        diagnostics.outputs[i] = i + 1;
    diagnostics.trace.resize(io::kVerificationTraceLimit);
    for (std::size_t i = 0; i < diagnostics.trace.size(); ++i) {
        auto& event = diagnostics.trace[i];
        event.kind = static_cast<std::uint32_t>(i % 8);
        event.phase = static_cast<std::uint32_t>(i % 5);
        event.object = static_cast<std::uint32_t>(i);
        event.cycle = static_cast<int>(i + 7);
        event.atomCount = static_cast<std::uint32_t>(i % (io::kVerificationTraceAtoms + 1));
        event.atomsTruncated = i % 2 != 0;
        for (std::size_t atom = 0; atom < event.atoms.size(); ++atom)
            event.atoms[atom] = (std::uint64_t{1} << 40) + i * 16 + atom;
    }
    diagnostics.trace.back().object = std::numeric_limits<std::uint32_t>::max();
    diagnostics.trace.back().atoms.back() = std::numeric_limits<std::uint64_t>::max();
    return diagnostics;
}
void Equal(const io::VerificationReport& actual, const io::VerificationReport& expected) {
    EXPECT_EQ(actual.status, expected.status);
    EXPECT_EQ(actual.authority, expected.authority);
    EXPECT_EQ(actual.metrics.cycles, expected.metrics.cycles);
    EXPECT_EQ(actual.metrics.cost, expected.metrics.cost);
    EXPECT_EQ(actual.metrics.area, expected.metrics.area);
    EXPECT_EQ(actual.metrics.instructionCount, expected.metrics.instructionCount);
    EXPECT_EQ(actual.errorCycle, expected.errorCycle);
    EXPECT_EQ(actual.errorU, expected.errorU);
    EXPECT_EQ(actual.errorV, expected.errorV);
    EXPECT_EQ(actual.detail, expected.detail);
    EXPECT_EQ(actual.engineVersion, expected.engineVersion);
    EXPECT_EQ(actual.rulesetVersion, expected.rulesetVersion);
    EXPECT_EQ(actual.capabilityProfile, expected.capabilityProfile);
    EXPECT_EQ(actual.reasonCode, expected.reasonCode);
    EXPECT_EQ(actual.diagnostics, expected.diagnostics);
}
void Word(std::vector<std::uint8_t>& frame, std::size_t offset, std::uint32_t value) {
    for (int byte = 0; byte < 4; ++byte)
        frame.at(offset + byte) = static_cast<std::uint8_t>(value >> (byte * 8));
}
} // namespace

TEST(VerifierWorkerProtocol, RoundTripPreservesFieldsStatusesAndCountedStrings) {
    auto report = Report();
    report.detail = std::string("\xe5\xae\x8c\xe6\x88\x90\0trace", 12);
    for (const auto status : {io::VerificationStatus::Passed, io::VerificationStatus::Rejected,
                              io::VerificationStatus::CycleLimit, io::VerificationStatus::Timeout,
                              io::VerificationStatus::Unavailable, io::VerificationStatus::Error}) {
        SCOPED_TRACE(static_cast<int>(status));
        report.status = status;
        report.authority = report.Passed() ? io::VerificationAuthority::Independent
                                           : io::VerificationAuthority::None;
        Equal(protocol::DecodeReport(protocol::EncodeReport(report)), report);
    }
    report.status = io::VerificationStatus::Passed;
    report.authority = io::VerificationAuthority::LegacyGate;
    report.metrics = {};
    Equal(protocol::DecodeReport(protocol::EncodeReport(report)), report);
}

TEST(VerifierWorkerProtocol, RejectsWrongMagicVersionLengthsAndTrailingPayload) {
    const auto original = protocol::EncodeReport(Report());
    auto frame = original;
    frame[0] ^= 1;
    EXPECT_THROW((void)protocol::DecodeReport(frame), std::invalid_argument);
    frame = original;
    Word(frame, 8, 1);
    EXPECT_THROW((void)protocol::DecodeReport(frame), std::invalid_argument);
    Word(frame, 8, 99);
    EXPECT_THROW((void)protocol::DecodeReport(frame), std::invalid_argument);
    frame = original;
    Word(frame, 12, std::numeric_limits<std::uint32_t>::max());
    EXPECT_THROW((void)protocol::DecodeReport(frame), std::invalid_argument);
    frame = original;
    frame.push_back(0);
    EXPECT_THROW((void)protocol::DecodeReport(frame), std::invalid_argument);
    Word(frame, 12, static_cast<std::uint32_t>(frame.size() - 16));
    EXPECT_THROW((void)protocol::DecodeReport(frame), std::invalid_argument);
    frame = original;
    Word(frame, 52, std::numeric_limits<std::uint32_t>::max());
    EXPECT_THROW((void)protocol::DecodeReport(frame), std::invalid_argument);
    for (std::size_t size = 0; size < original.size(); ++size) {
        SCOPED_TRACE(size);
        EXPECT_THROW((void)protocol::DecodeReport(std::span(original).first(size)),
                     std::invalid_argument);
    }
}

TEST(VerifierWorkerProtocol, RejectsInvalidEnumsOrUnqualifiedPassedReports) {
    const auto original = protocol::EncodeReport(Report());
    for (std::size_t offset : {16U, 20U}) {
        auto frame = original;
        Word(frame, offset, 99);
        EXPECT_THROW((void)protocol::DecodeReport(frame), std::invalid_argument);
    }
    auto report = Report();
    report.status = static_cast<io::VerificationStatus>(99);
    EXPECT_THROW((void)protocol::EncodeReport(report), std::invalid_argument);
    report = Report();
    report.authority = static_cast<io::VerificationAuthority>(99);
    EXPECT_THROW((void)protocol::EncodeReport(report), std::invalid_argument);
    report.authority = io::VerificationAuthority::None;
    EXPECT_THROW((void)protocol::EncodeReport(report), std::invalid_argument);
    for (std::size_t offset : {24U, 28U, 32U, 36U}) {
        auto frame = original;
        Word(frame, offset, std::numeric_limits<std::uint32_t>::max());
        EXPECT_THROW((void)protocol::DecodeReport(frame), std::invalid_argument);
    }
    for (int field = 0; field < 4; ++field) {
        report = Report();
        auto* value = field == 0   ? &report.engineVersion
                      : field == 1 ? &report.rulesetVersion
                      : field == 2 ? &report.capabilityProfile
                                   : &report.reasonCode;
        value->clear();
        EXPECT_THROW((void)protocol::EncodeReport(report), std::invalid_argument);
    }
    report = Report();
    report.shadow = io::VerificationObservation{};
    EXPECT_THROW((void)protocol::EncodeReport(report), std::invalid_argument);
    report = Report();
    report.status = io::VerificationStatus::Rejected;
    report.authority = io::VerificationAuthority::None;
    report.metrics.cycles = -1;
    Equal(protocol::DecodeReport(protocol::EncodeReport(report)), report);
}

TEST(VerifierWorkerProtocol, DiagnosticsRoundTripPreservesWideCountersAndMaximumBoundedTrace) {
    auto report = Report();
    report.diagnostics = Diagnostics();
    report.diagnostics->completedCycles = 0x1122334455667788ULL;
    const auto frame = protocol::EncodeReport(report);
    EXPECT_EQ(frame[8], 4);
    const auto wideValue = protocol::EncodeReport(Report()).size() + 8;
    const std::array<std::uint8_t, 8> littleEndian{0x88, 0x77, 0x66, 0x55, 0x44, 0x33, 0x22, 0x11};
    EXPECT_TRUE(std::equal(littleEndian.begin(), littleEndian.end(), frame.begin() + wideValue));
    Equal(protocol::DecodeReport(frame), report);
    EXPECT_LT(frame.size(), protocol::kMaximumFrameBytes);
    report.diagnostics = io::VerificationDiagnostics{};
    Equal(protocol::DecodeReport(protocol::EncodeReport(report)), report);
}

TEST(VerifierWorkerProtocol, DiagnosticBoundsAndBooleansAreValidatedBeforeAllocation) {
    auto report = Report();
    report.diagnostics = Diagnostics();
    const auto frame = protocol::EncodeReport(report);
    const auto presence = protocol::EncodeReport(Report()).size() - 4;
    const auto diagnostic = presence + 4;
    const auto outputs =
        diagnostic + 2 * 4 + 11 * 8 + 2 * 4 +
        (report.diagnostics->phaseMicros.size() + report.diagnostics->counters.size()) * 8;
    const auto traceCount = outputs + 4 + report.diagnostics->outputs.size() * 8;
    const auto trace = traceCount + 4;
    for (const auto [offset, value] :
         std::array<std::pair<std::size_t, std::uint32_t>, 12>{{{presence, 2},
                                                                {diagnostic, 8},
                                                                {diagnostic + 4, 0xffffffffU},
                                                                {diagnostic + 96, 2},
                                                                {diagnostic + 100, 2},
                                                                {outputs, 17},
                                                                {traceCount, 65},
                                                                {trace, 8},
                                                                {trace + 4, 5},
                                                                {trace + 12, 0xffffffffU},
                                                                {trace + 16, 9},
                                                                {trace + 20, 2}}}) {
        SCOPED_TRACE(offset);
        auto invalid = frame;
        Word(invalid, offset, value);
        EXPECT_THROW((void)protocol::DecodeReport(invalid), std::invalid_argument);
    }
    for (int invalid = 0; invalid < 8; ++invalid) {
        auto value = Diagnostics();
        switch (invalid) {
        case 0:
            value.phase = io::VerificationPhase::Count;
            break;
        case 1:
            value.cycle = -1;
            break;
        case 2:
            value.outputs.resize(17);
            break;
        case 3:
            value.trace.resize(io::kVerificationTraceLimit + 1);
            break;
        case 4:
            value.trace[0].kind = 8;
            break;
        case 5:
            value.trace[0].phase = 5;
            break;
        case 6:
            value.trace[0].cycle = -1;
            break;
        case 7:
            value.trace[0].atomCount = 9;
            break;
        }
        report.diagnostics = std::move(value);
        EXPECT_THROW((void)protocol::EncodeReport(report), std::invalid_argument);
    }
}

TEST(VerifierWorkerProtocol, DiagnosticPayloadTruncationIsRejectedWithinAValidOuterFrame) {
    auto report = Report();
    report.diagnostics = Diagnostics();
    const auto frame = protocol::EncodeReport(report);
    const auto presence = protocol::EncodeReport(Report()).size() - 4;
    for (std::size_t size = presence; size < frame.size(); size += 7) {
        auto truncated = frame;
        truncated.resize(size);
        Word(truncated, 12, static_cast<std::uint32_t>(size - 16));
        EXPECT_THROW((void)protocol::DecodeReport(truncated), std::invalid_argument);
    }
    auto truncated = frame;
    truncated.pop_back();
    Word(truncated, 12, static_cast<std::uint32_t>(truncated.size() - 16));
    EXPECT_THROW((void)protocol::DecodeReport(truncated), std::invalid_argument);
}

TEST(VerifierWorkerProtocol, PrimaryAndShadowJsonRetainDiagnosticsWithoutChangingMetrics) {
    auto report = Report();
    report.diagnostics = Diagnostics();
    report.diagnostics->counters.back() = 7;
    report.shadow.emplace();
    report.shadow->status = io::VerificationStatus::Timeout;
    report.shadow->reasonCode = "independent-timeout";
    report.shadow->diagnostics = io::VerificationDiagnostics{};
    report.shadow->diagnostics->phase = io::VerificationPhase::Glyphs;
    const auto json = io::VerificationReportJson(report);
    EXPECT_NE(json.find("\"diagnostics\":{\"phase\":\"motions\""), std::string::npos);
    EXPECT_NE(json.find("\"diagnostics\":{\"phase\":\"glyphs\""), std::string::npos);
    EXPECT_NE(json.find("\"phaseMicros\":{\"setup\":0,\"grabDrop\":100"), std::string::npos);
    EXPECT_NE(json.find("\"counters\":{\"atomLookups\":1,\"fragmentQueries\":2"),
              std::string::npos);
    EXPECT_NE(json.find("\"atomCount\""), std::string::npos);
    EXPECT_NE(json.find("\"parseMicros\":51"), std::string::npos);
    EXPECT_NE(json.find("\"rasterMicros\":201"), std::string::npos);
    EXPECT_NE(json.find("\"collisionMicros\":299"), std::string::npos);
    EXPECT_NE(json.find("\"observeMicros\":137"), std::string::npos);
    EXPECT_NE(json.find("\"snapshotMicros\":89"), std::string::npos);
    EXPECT_NE(json.find("\"fragmentMicros\":73"), std::string::npos);
    EXPECT_NE(json.find("\"heldMicros\":101"), std::string::npos);
    EXPECT_NE(json.find("\"timingsCollected\":true"), std::string::npos);
    EXPECT_NE(json.find("\"traceTruncated\":true"), std::string::npos);
    EXPECT_NE(json.find("\"cycles\":43,\"cost\":60,\"area\":67,\"instructions\":4"),
              std::string::npos);
    EXPECT_NE(json.find("\"shadow\":{\"status\":\"timeout\""), std::string::npos);
}

TEST(VerifierWorkerProtocol, LargeReportsAndExactFrameBoundRemainValid) {
    auto report = Report();
    report.diagnostics = Diagnostics();
    report.detail.assign(32000, 'x');
    const auto large = protocol::EncodeReport(report);
    EXPECT_GT(large.size(), 8192U);
    Equal(protocol::DecodeReport(large), report);
    const auto overhead = large.size() - report.detail.size();
    report.detail.assign(protocol::kMaximumFrameBytes - overhead, 'y');
    const auto maximum = protocol::EncodeReport(report);
    EXPECT_EQ(maximum.size(), protocol::kMaximumFrameBytes);
    Equal(protocol::DecodeReport(maximum), report);
    report.detail.push_back('z');
    EXPECT_THROW((void)protocol::EncodeReport(report), std::invalid_argument);
    auto excessive = maximum;
    excessive.push_back(0);
    EXPECT_THROW((void)protocol::DecodeReport(excessive), std::invalid_argument);
    EXPECT_FALSE(protocol::WriteReport(-1, report));
}
