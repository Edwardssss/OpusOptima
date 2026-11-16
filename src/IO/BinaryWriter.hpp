#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <vector>

#include "IO/BinaryParser.hpp"

namespace opusoptima::io {

/// Binary writer for .solution files.
class BinaryWriter {
    public:
    explicit BinaryWriter() = default;

    /// Serialise a SolutionData into a byte vector.
    [[nodiscard]] static std::vector<std::byte> WriteSolution(const SolutionData& solution);

    /// Write a SolutionData directly to a file.
    static void WriteSolutionFile(const std::filesystem::path& path, const SolutionData& solution);

    /// Serialise a base (non-production) puzzle. This is the editor/export path;
    /// production metadata is intentionally rejected until it has a writer.
    [[nodiscard]] static std::vector<std::byte> WritePuzzle(const PuzzleData& puzzle);

    private:
    static void WriteI32(std::vector<std::byte>& buf, std::int32_t value);
    static void WriteU8(std::vector<std::byte>& buf, std::uint8_t value);
    static void WriteVarInt(std::vector<std::byte>& buf, std::int32_t value);
    static void WriteString(std::vector<std::byte>& buf, const std::string& str);
    static void WriteU64(std::vector<std::byte>& buf, std::uint64_t value);
    static void WriteBool(std::vector<std::byte>& buf, bool value);
    static void WriteHexI8(std::vector<std::byte>& buf, core::HexCoord cell);
    static void WriteMolecule(std::vector<std::byte>& buf, const Molecule& molecule);
};

} // namespace opusoptima::io
