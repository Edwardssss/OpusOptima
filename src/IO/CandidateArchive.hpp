#pragma once

#include <array>
#include <memory>
#include <vector>

#include "SolutionVerifier.hpp"

namespace opusoptima::io {

// Slots minimize one measured objective each; equal primary values retain the first entry.
// Entries are immutable, input/provenance scoped, and may occupy multiple slots.
struct ArchivedCandidate {
    SolutionData solution;
    std::vector<std::byte> bytes;
    VerificationReport verification;
    std::string solutionHash, inputHash, strategy;
    std::filesystem::path file;
};

class CandidateArchive {
public:
    using Entry = std::shared_ptr<const ArchivedCandidate>;
    CandidateArchive(const std::filesystem::path& puzzle,
                     const std::filesystem::path& output,
                     VerificationOptions options);
    [[nodiscard]] VerificationReport Submit(const SolutionData& candidate,
                                             const std::string& strategy,
                                             std::optional<std::chrono::steady_clock::time_point> deadline = std::nullopt);
    [[nodiscard]] const std::array<Entry, 3>& Slots() const { return slots_; }
    [[nodiscard]] Entry First() const { return first_; }
    [[nodiscard]] const VerificationOptions& Options() const { return options_; }
    [[nodiscard]] const std::filesystem::path& Directory() const { return directory_; }
    // Revalidates exact archived bytes before publication. The manifest rename is the
    // three-slot commit point; the legacy first-accepted output is a separate rename.
    [[nodiscard]] VerificationReport Publish(const VerificationOptions& options);
    // Commits already verified exact bytes without re-running the verifier. Earlier
    // snapshots reference immutable generation files and survive interrupted updates.
    [[nodiscard]] VerificationReport Snapshot(const VerificationOptions& options);
private:
    VerificationReport Commit(const VerificationOptions& options, bool final);
    std::filesystem::path puzzle_, output_, directory_;
    VerificationOptions options_;
    std::vector<std::byte> input_;
    std::string inputHash_;
    std::array<Entry, 3> slots_{};
    Entry first_;
    bool published_{};
    std::size_t sequence_{};
};

[[nodiscard]] std::string CandidateSha256(const std::vector<std::byte>& bytes);

} // namespace opusoptima::io
