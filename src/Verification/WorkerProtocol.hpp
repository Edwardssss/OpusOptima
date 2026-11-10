#pragma once

#include "IO/SolutionVerifier.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace opusoptima::verification::worker {
inline constexpr int kReportDescriptor = 3;
inline constexpr std::uint32_t kProtocolVersion = 4;
inline constexpr std::size_t kMaximumFrameBytes = 65536;

[[nodiscard]] std::vector<std::uint8_t> EncodeReport(const io::VerificationReport&);
[[nodiscard]] io::VerificationReport DecodeReport(std::span<const std::uint8_t>);
[[nodiscard]] bool WriteReport(int descriptor, const io::VerificationReport&);
using Evaluator = io::VerificationReport (*)(const std::filesystem::path&,
                                             const std::filesystem::path&, int, bool);
int RunWorker(int argc, char* argv[], Evaluator);
} // namespace opusoptima::verification::worker
