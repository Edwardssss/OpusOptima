#include "BinaryWriter.hpp"

#include <fstream>
#include <stdexcept>

#include "Core/LogManager.hpp"
#include <spdlog/spdlog.h>

#include "Core/CoreConstants.hpp"

namespace opusoptima::io {

namespace {

/// Maximum value for a single-varint byte (7 bits).
constexpr std::uint8_t kVarintMaxSingle = 0x7F;
/// Bit that signals "more bytes follow".
constexpr std::uint8_t kVarintContinue = 0x80;
/// Bits per varint group.
constexpr int kVarintShift = 7;

} // namespace

void BinaryWriter::WriteI32(std::vector<std::byte>& buf, std::int32_t value) {
    for (int i = 0; i < 4; ++i) {
        buf.push_back(
            static_cast<std::byte>((value >> (i * core::kBitsPerByte)) & core::kByteMask));
    }
}

void BinaryWriter::WriteU8(std::vector<std::byte>& buf, std::uint8_t value) {
    buf.push_back(static_cast<std::byte>(value));
}

void BinaryWriter::WriteVarInt(std::vector<std::byte>& buf, std::int32_t value) {
    // C# BinaryReader/Writer uses unsigned 7-bit encoded varint.
    auto uval = static_cast<std::uint32_t>(value);
    while (uval > kVarintMaxSingle) {
        buf.push_back(static_cast<std::byte>((uval & kVarintMaxSingle) | kVarintContinue));
        uval >>= kVarintShift;
    }
    buf.push_back(static_cast<std::byte>(uval));
}

void BinaryWriter::WriteString(std::vector<std::byte>& buf, const std::string& str) {
    WriteVarInt(buf, static_cast<std::int32_t>(str.size()));
    for (char chr : str) {
        buf.push_back(static_cast<std::byte>(chr));
    }
}

std::vector<std::byte> BinaryWriter::WriteSolution(const SolutionData& solution) {
    std::vector<std::byte> buf;

    // ── Header ──
    // Format tag = 7
    WriteI32(buf, core::kSolutionFormatTag);

    // Puzzle name (varint-length-prefixed string)
    WriteString(buf, solution.puzzleName);

    // Solution name
    WriteString(buf, solution.solutionName);

    // ── Metrics ──
    // Metrics count (always 4: cycles, cost, area, instructionCount)
    WriteI32(buf, core::kSolutionMetricCount);

    auto writeMetric = [&](std::int32_t type, std::int32_t value) {
        WriteI32(buf, type);
        WriteI32(buf, value);
    };
    writeMetric(0, solution.metrics.cycles);
    writeMetric(1, solution.metrics.cost);
    writeMetric(2, solution.metrics.area);
    writeMetric(3, solution.metrics.instructionCount);

    // ── Objects ──
    WriteI32(buf, static_cast<std::int32_t>(solution.objects.size()));

    for (const auto& obj : solution.objects) {
        // Object type name (e.g. "arm1")
        WriteString(buf, obj.typeName);

        // Position: marker (0x01), q (i32), r (i32)
        WriteU8(buf, 0x01);
        WriteI32(buf, obj.position.q);
        WriteI32(buf, obj.position.r);

        if (obj.typeName.starts_with("arm") || obj.typeName == "piston" ||
            obj.typeName == "baron" || obj.typeName == "track") {
            // ── Instruction-based object ──
            // Body header: 5 int32s
            // h0 = marker (usually 1)
            // h1 = 0
            // h2 = 0
            // h3 = instruction count
            // h4 = 0
            WriteI32(buf, 1);                                                  // h0: marker
            WriteI32(buf, 0);                                                  // h1
            WriteI32(buf, 0);                                                  // h2
            WriteI32(buf, static_cast<std::int32_t>(obj.instructions.size())); // h3
            WriteI32(buf, 0);                                                  // h4

            // Instructions: each is 1-byte action + 4-byte parameter
            for (const auto& instr : obj.instructions) {
                WriteU8(buf, static_cast<std::uint8_t>(instr.action));
                WriteI32(buf, instr.parameter);
            }
        } else {
            // ── Other objects (bonder, glyph, input, output, pipe, etc.) ──
            // Write the 5-int32 body header followed by any additional body data
            std::size_t totalWords = obj.bodyData.size();
            if (totalWords < core::kSolutionObjectHeaderWords) {
                // Pad with zeros
                for (std::size_t i = 0; i < core::kSolutionObjectHeaderWords; ++i) {
                    WriteI32(buf, i < totalWords ? obj.bodyData[i] : 0);
                }
            } else {
                for (std::size_t i = 0; i < totalWords; ++i) {
                    WriteI32(buf, obj.bodyData[i]);
                }
            }
        }
    }

    return buf;
}

void BinaryWriter::WriteSolutionFile(const std::filesystem::path& path,
                                     const SolutionData& solution) {
    auto logger = opusoptima::GetLoggerOrFallback("io");
    logger->info("Writing solution '{}' to {}", solution.solutionName, path.string());
    const auto bytes = WriteSolution(solution);
    logger->debug("Serialised {} objects → {} bytes", solution.objects.size(), bytes.size());

    std::ofstream file(path, std::ios::binary);
    if (!file) {
        logger->error("Cannot open file for writing: {}", path.string());
        throw std::runtime_error("Cannot open file for writing: " + path.string());
    }
    file.write(reinterpret_cast<const char*>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
    if (!file) {
        logger->error("Failed to write solution file: {}", path.string());
        throw std::runtime_error("Failed to write solution file: " + path.string());
    }
    logger->info("Solution file written successfully");
}

} // namespace opusoptima::io
