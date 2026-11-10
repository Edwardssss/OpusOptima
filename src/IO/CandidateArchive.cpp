#include "CandidateArchive.hpp"

#include <algorithm>
#include <bit>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>

#include "BinaryWriter.hpp"
#include "CalcifyAnimismusAdmission.hpp"
#include "VerificationJson.hpp"

namespace opusoptima::io {
namespace {
std::string Quote(const std::string& value) {
    std::ostringstream out;
    out << '"';
    for (unsigned char c : value) {
        if (c == '"' || c == '\\')
            out << '\\' << c;
        else if (c < 32)
            out << "\\u" << std::hex << std::setw(4) << std::setfill('0') << int(c);
        else
            out << c;
    }
    out << '"';
    return out.str();
}
void Write(const std::filesystem::path& path, const std::vector<std::byte>& bytes) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.exceptions(std::ios::badbit | std::ios::failbit);
    out.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    out.close();
}
bool Same(const VerificationReport& a, const VerificationReport& b) {
    return a.metrics.cost == b.metrics.cost && a.metrics.cycles == b.metrics.cycles &&
           a.metrics.area == b.metrics.area &&
           a.metrics.instructionCount == b.metrics.instructionCount && a.authority == b.authority &&
           a.engineVersion == b.engineVersion && a.rulesetVersion == b.rulesetVersion &&
           a.capabilityProfile == b.capabilityProfile;
}
VerificationReport Error(const std::string& detail) {
    VerificationReport report;
    report.status = VerificationStatus::Error;
    report.detail = detail;
    report.reasonCode = "candidate-archive-error";
    return report;
}
} // namespace

std::string CandidateSha256(const std::vector<std::byte>& bytes) {
    static constexpr std::uint32_t k[]{
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4,
        0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe,
        0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f,
        0x4a7484aa, 0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
        0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc,
        0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
        0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116,
        0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
        0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7,
        0xc67178f2};
    std::array<std::uint32_t, 8> h{0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                                   0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    auto data = bytes;
    const auto bits = std::uint64_t(data.size()) * 8;
    data.push_back(std::byte{0x80});
    while (data.size() % 64 != 56)
        data.push_back(std::byte{0});
    for (int i = 7; i >= 0; --i)
        data.push_back(std::byte((bits >> (8 * i)) & 255));
    for (std::size_t offset = 0; offset < data.size(); offset += 64) {
        std::uint32_t w[64]{};
        for (int i = 0; i < 16; ++i)
            for (int j = 0; j < 4; ++j)
                w[i] = (w[i] << 8) | std::to_integer<unsigned>(data[offset + i * 4 + j]);
        for (int i = 16; i < 64; ++i) {
            auto s0 = std::rotr(w[i - 15], 7) ^ std::rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
            auto s1 = std::rotr(w[i - 2], 17) ^ std::rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        auto v = h;
        for (int i = 0; i < 64; ++i) {
            auto t1 = v[7] + (std::rotr(v[4], 6) ^ std::rotr(v[4], 11) ^ std::rotr(v[4], 25)) +
                      ((v[4] & v[5]) ^ (~v[4] & v[6])) + k[i] + w[i];
            auto t2 = (std::rotr(v[0], 2) ^ std::rotr(v[0], 13) ^ std::rotr(v[0], 22)) +
                      ((v[0] & v[1]) ^ (v[0] & v[2]) ^ (v[1] & v[2]));
            for (int j = 7; j > 0; --j)
                v[j] = v[j - 1];
            v[4] += t1;
            v[0] = t1 + t2;
        }
        for (int i = 0; i < 8; ++i)
            h[i] += v[i];
    }
    std::ostringstream out;
    for (auto word : h)
        out << std::hex << std::setw(8) << std::setfill('0') << word;
    return out.str();
}

CandidateArchive::CandidateArchive(const std::filesystem::path& puzzle,
                                   const std::filesystem::path& output, VerificationOptions options)
    : puzzle_(puzzle), output_(output), options_(std::move(options)),
      input_(BinaryParser::ReadAllBytes(puzzle)), inputHash_(CandidateSha256(input_)) {
    // create_directory is exclusive; no publisher shares mutable bundle files.
    const auto prefix = output.string() + ".bundle-" +
                        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    for (unsigned i = 0;; ++i) {
        directory_ = prefix + "-" + std::to_string(i);
        if (std::filesystem::create_directory(directory_)) break;
    }
}

VerificationReport CandidateArchive::Submit(
    const SolutionData& candidate, const std::string& strategy,
    std::optional<std::chrono::steady_clock::time_point> deadline) {
    if (published_) return Error("Published archives are immutable; start a new archive");
    if (BinaryParser::ReadAllBytes(puzzle_) != input_)
        return Error("Input changed since archive creation");
    if (!CalcifyAnimismusModeAllowed(strategy, options_.mode)) {
        VerificationReport report;
        report.status = VerificationStatus::Unavailable;
        report.detail = "stage requires LegacyGate or legacy-primary Shadow";
        return report;
    }
    const auto file = directory_ / ("candidate-" + std::to_string(sequence_++) + ".solution");
    auto options = options_;
    if (deadline && (!options.outerDeadline || *deadline < *options.outerDeadline))
        options.outerDeadline = deadline;
    auto report = PublishVerifiedSolution(puzzle_, file, candidate, options);
    if (!report.Passed()) return report;
    report = RequireCalcifyAnimismusAuthority(strategy, std::move(report));
    if (!report.Passed()) {
        std::error_code cleanupError;
        std::filesystem::remove(file, cleanupError);
        if (cleanupError)
            return Error("Native authority rejected; private candidate cleanup failed: " +
                         cleanupError.message());
        return report;
    }
    auto entry = std::make_shared<ArchivedCandidate>();
    entry->bytes = BinaryParser::ReadAllBytes(file);
    entry->solution = BinaryParser::ParseSolution(entry->bytes);
    entry->verification = report;
    entry->solutionHash = CandidateSha256(entry->bytes);
    entry->inputHash = inputHash_;
    entry->strategy = strategy;
    entry->file = file;
    if (entry->solution.metrics.cost != report.metrics.cost ||
        entry->solution.metrics.cycles != report.metrics.cycles ||
        entry->solution.metrics.area != report.metrics.area ||
        BinaryParser::ReadAllBytes(puzzle_) != input_) {
        std::filesystem::remove(file);
        return Error("Archived bytes or input changed");
    }
    if (first_ && !Same(first_->verification, report)) {
        // Metrics differ legitimately; only provenance defines archive scope.
        const auto& first = first_->verification;
        if (first.authority != report.authority || first.engineVersion != report.engineVersion ||
            first.rulesetVersion != report.rulesetVersion ||
            first.capabilityProfile != report.capabilityProfile) {
            std::filesystem::remove(file);
            return Error("Candidate provenance differs from archive scope");
        }
    }
    if (!first_) first_ = entry;
    const auto score = [](const Entry& e, std::size_t slot) {
        const auto& m = e->verification.metrics;
        return slot == 0 ? m.cost : slot == 1 ? m.cycles : m.area;
    };
    for (std::size_t i = 0; i < slots_.size(); ++i)
        if (!slots_[i] || score(entry, i) < score(slots_[i], i)) slots_[i] = entry;
    return report;
}

VerificationReport CandidateArchive::Publish(const VerificationOptions& options) {
    if (published_) return Error("Published archives are immutable; start a new archive");
    if (!first_) return Error("No verified candidates; all three slots are empty");
    if (BinaryParser::ReadAllBytes(puzzle_) != input_)
        return Error("Input changed before publication");
    std::vector<Entry> unique{first_};
    for (const auto& entry : slots_)
        if (std::find(unique.begin(), unique.end(), entry) == unique.end()) unique.push_back(entry);
    for (const auto& entry : unique) {
        if (BinaryParser::ReadAllBytes(entry->file) != entry->bytes)
            return Error("Archived candidate file changed");
        const auto report = VerifySolutionFiles(puzzle_, entry->file, options);
        if (!report.Passed()) return report;
        if (!Same(entry->verification, report))
            return Error("Final verification provenance or metrics changed");
    }
    return Commit(options, true);
}

VerificationReport CandidateArchive::Snapshot(const VerificationOptions& options) {
    return Commit(options, false);
}

VerificationReport CandidateArchive::Commit(const VerificationOptions& options, bool final) {
    if (published_) return Error("Published archives are immutable; start a new archive");
    if (!first_) return Error("No verified candidates; all three slots are empty");
    if (options.mode != options_.mode) return Error("Snapshot authority differs from admission");
    for (const auto& entry : slots_)
        if (!entry->verification.Passed() ||
            BinaryParser::ReadAllBytes(entry->file) != entry->bytes)
            return Error("Archived candidate file changed");
    if (BinaryParser::ReadAllBytes(first_->file) != first_->bytes)
        return Error("Archived first candidate file changed");
    if (BinaryParser::ReadAllBytes(puzzle_) != input_)
        return Error("Input changed during publication");
    if (options.outerDeadline && std::chrono::steady_clock::now() >= *options.outerDeadline) {
        auto report = Error("Final publication deadline exhausted");
        report.status = VerificationStatus::Timeout;
        return report;
    }
    std::ostringstream manifest;
    manifest << "{\"schema\":1,\"inputSha256\":" << Quote(inputHash_)
             << ",\"tiePolicy\":\"first-verified\",\"legacyPolicy\":\"first-verified\",\"slots\":{";
    constexpr const char* names[]{"cost", "cycles", "area"};
    std::array<std::filesystem::path, 3> staged;
    for (std::size_t i = 0; i < slots_.size(); ++i) {
        const auto& entry = slots_[i];
        const auto name = "snapshot-" + std::to_string(sequence_++) + "-" + names[i] + ".solution";
        staged[i] = directory_ / name;
        Write(staged[i], entry->bytes);
        if (i) manifest << ',';
        manifest << Quote(names[i])
                 << ":{\"file\":" << Quote((directory_.filename() / name).string())
                 << ",\"sha256\":" << Quote(entry->solutionHash)
                 << ",\"inputSha256\":" << Quote(entry->inputHash)
                 << ",\"strategy\":" << Quote(entry->strategy)
                 << ",\"verification\":" << VerificationReportJson(entry->verification) << '}';
    }
    manifest << "}}\n";
    const auto text = manifest.str();
    std::vector<std::byte> encoded;
    for (unsigned char c : text)
        encoded.push_back(std::byte(c));
    Write(directory_ / "manifest.json", encoded);
    Write(directory_ / "legacy.solution", first_->bytes);
    const std::filesystem::path publicManifest = output_.string() + ".objectives.json";
    if (std::filesystem::exists(publicManifest) &&
        !std::filesystem::is_regular_file(publicManifest))
        return Error("Manifest destination is not a regular file");
    const bool hadOutput = std::filesystem::exists(output_);
    if (hadOutput) {
        if (!std::filesystem::is_regular_file(output_))
            return Error("Output is not a regular file");
        std::filesystem::copy_file(output_, directory_ / "previous.solution",
                                   std::filesystem::copy_options::overwrite_existing);
    }
    for (std::size_t i = 0; i < slots_.size(); ++i)
        if (BinaryParser::ReadAllBytes(staged[i]) != slots_[i]->bytes)
            return Error("Staged objective bytes changed");
    if (final)
        for (std::size_t i = 0; i < slots_.size(); ++i)
            Write(directory_ / (std::string(names[i]) + ".solution"), slots_[i]->bytes);
    if (options.outerDeadline && std::chrono::steady_clock::now() >= *options.outerDeadline) {
        auto report = Error("Final publication deadline exhausted");
        report.status = VerificationStatus::Timeout;
        return report;
    }
    // Single-writer contract: each rename is atomic, not a two-file transaction.
    // A caught second-rename failure restores -o; process/crash durability is not promised.
    std::filesystem::rename(directory_ / "legacy.solution", output_);
    try {
        std::filesystem::rename(directory_ / "manifest.json", publicManifest);
    } catch (...) {
        if (hadOutput)
            std::filesystem::rename(directory_ / "previous.solution", output_);
        else
            std::filesystem::remove(output_);
        throw;
    }
    published_ = final;
    std::error_code ignored;
    std::filesystem::remove(directory_ / "previous.solution", ignored);
    return first_->verification;
}
} // namespace opusoptima::io
