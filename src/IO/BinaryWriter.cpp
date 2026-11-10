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

        // bodyData stores size, rotation, input/output index, instruction count,
        // and arm number. The arm number is a footer, not a fifth header word.
        const auto word = [&](std::size_t index, std::int32_t fallback = 0) {
            return index < obj.bodyData.size() ? obj.bodyData[index] : fallback;
        };
        WriteI32(buf, word(0, 1));
        WriteI32(buf, word(1));
        WriteI32(buf, word(2));
        WriteI32(buf, static_cast<std::int32_t>(obj.instructions.size()));
        for (const auto& instr : obj.instructions) {
            WriteI32(buf, instr.parameter); // zero-based tape index
            WriteU8(buf, static_cast<std::uint8_t>(instr.action));
        }
        if (obj.typeName == "track") {
            // For tracks bodyData[4] is the segment count, followed by offsets
            // and the arm-number footer, matching the parser's representation.
            for (std::size_t i = 4; i < obj.bodyData.size(); ++i) {
                WriteI32(buf, obj.bodyData[i]);
            }
            if (obj.bodyData.size() < 5) {
                WriteI32(buf, 0);
                WriteI32(buf, 0);
            }
        } else {
            WriteI32(buf, word(4));
            for (std::size_t i = 5; i < obj.bodyData.size(); ++i) {
                WriteI32(buf, obj.bodyData[i]);
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
