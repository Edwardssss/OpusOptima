#include "BinaryParser.hpp"

#include <fstream>
#include <sstream>
#include <utility>

#include "Core/LogManager.hpp"
#include <spdlog/spdlog.h>

namespace opusoptima::io {

namespace {

// 8 bits per byte — byte-shift multiplier for assembling multi-byte integers.
constexpr int kBitsPerByte = 8;

class ByteReader {
    public:
    explicit ByteReader(const std::span<const std::byte> bytes) : bytes_(bytes) {}

    [[nodiscard]] std::size_t Remaining() const noexcept {
        return bytes_.size() - offset_;
    }

    std::uint8_t ReadU8() {
        EnsureAvailable(1, "u8");
        return static_cast<std::uint8_t>(bytes_[offset_++]);
    }

    bool ReadBool() {
        return ReadU8() != 0U;
    }

    std::int32_t ReadI32() {
        EnsureAvailable(4, "i32");
        std::uint32_t value = 0;
        for (int i = 0; i < 4; ++i) {
            value |= static_cast<std::uint32_t>(ReadU8()) << (kBitsPerByte * i);
        }
        return static_cast<std::int32_t>(value);
    }

    std::uint64_t ReadU64() {
        constexpr int kU64ByteCount = 8;

        EnsureAvailable(kU64ByteCount, "u64");
        std::uint64_t value = 0;
        for (int i = 0; i < kU64ByteCount; ++i) {
            value |= static_cast<std::uint64_t>(ReadU8()) << (kBitsPerByte * i);
        }
        return value;
    }

    std::int32_t ReadVarInt() {
        // Opus Magnum strings use C# BinaryReader's 7-bit encoded length prefix.
        std::uint32_t result = 0;
        int shift = 0;

        for (int i = 0; i < core::kVarintMaxGroups; ++i) {
            const std::uint8_t byte = ReadU8();
            result |= static_cast<std::uint32_t>(byte & core::kVarintDataMask) << shift;
            if ((byte & core::kVarintContinueBit) == 0U) {
                return static_cast<std::int32_t>(result);
            }
            shift += core::kVarintGroupBits;
        }

        throw ParseError("VarInt is too long");
    }

    std::string ReadString() {
        const std::int32_t length = ReadVarInt();
        if (length < 0) {
            throw ParseError("Negative string length");
        }

        const auto usize = static_cast<std::size_t>(length);
        EnsureAvailable(usize, "string");

        std::string value;
        value.reserve(usize);
        for (std::size_t i = 0; i < usize; ++i) {
            value.push_back(static_cast<char>(ReadU8()));
        }
        return value;
    }

    core::HexCoord ReadHexCoordI8() {
        // Puzzle format stores hex coordinates as signed 8-bit integers.
        // Read as unsigned bytes, and manually sign-extend to int.
        constexpr int kMaxI8Value = 127;
        constexpr int kByteRange = 256;
        const int raw_q = ReadU8();
        const int raw_r = ReadU8();
        return core::HexCoord{
            .q = raw_q <= kMaxI8Value ? raw_q : (raw_q - kByteRange),
            .r = raw_r <= kMaxI8Value ? raw_r : (raw_r - kByteRange),
        };
    }

    template <typename T, typename Func>
    std::vector<T> ReadList(const std::size_t minimumEntryBytes, Func&& parseEntry) {
        const std::int32_t count = ReadI32();
        if (count < 0) {
            throw ParseError("Negative list length");
        }
        if (static_cast<std::size_t>(count) > Remaining() / minimumEntryBytes) {
            throw ParseError("Invalid list length");
        }

        std::vector<T> items;
        items.reserve(static_cast<std::size_t>(count));
        for (std::int32_t i = 0; i < count; ++i) {
            items.push_back(parseEntry(*this));
        }
        return items;
    }

    private:
    void EnsureAvailable(const std::size_t needed, const char* context) const {
        if (Remaining() < needed) {
            throw ParseError(std::string("Unexpected EOF while reading ") + context);
        }
    }

    std::span<const std::byte> bytes_;
    std::size_t offset_{};
};

Molecule ParseMolecule(ByteReader& reader) {
    Molecule molecule;

    // Molecule block: atoms first, then bond list.
    molecule.atoms = reader.ReadList<MoleculeAtom>(3, [](ByteReader& localReader) {
        const auto atomId = localReader.ReadU8();
        const auto atom = core::AtomTypeFromId(atomId);
        if (!atom.has_value()) {
            throw ParseError("Invalid atom ID: " + std::to_string(atomId));
        }

        return MoleculeAtom{
            .type = atom.value(),
            .position = localReader.ReadHexCoordI8(),
        };
    });

    molecule.bonds = reader.ReadList<MoleculeBond>(5, [](ByteReader& localReader) {
        return MoleculeBond{
            .bits = localReader.ReadU8(),
            .from = localReader.ReadHexCoordI8(),
            .to = localReader.ReadHexCoordI8(),
        };
    });

    return molecule;
}

} // namespace

PuzzleData BinaryParser::ParsePuzzle(const std::span<const std::byte> bytes) {
    ByteReader reader(bytes);

    PuzzleData puzzle;
    // version, metadata, inputs, outputs, multiplier, optional production section.
    puzzle.version = reader.ReadI32();
    if (puzzle.version != core::kPuzzleFormatVersion) {
        throw ParseError("Not an Opus Magnum puzzle file: expected version " +
                         std::to_string(core::kPuzzleFormatVersion) + ", got " +
                         std::to_string(puzzle.version));
    }

    puzzle.name = reader.ReadString();
    puzzle.creatorSteamId = reader.ReadU64();
    puzzle.permissionBits = reader.ReadU64();
    puzzle.inputs = reader.ReadList<Molecule>(
        8, [](ByteReader& localReader) { return ParseMolecule(localReader); });
    puzzle.outputs = reader.ReadList<Molecule>(
        8, [](ByteReader& localReader) { return ParseMolecule(localReader); });
    puzzle.outputMultiplier = reader.ReadI32();

    // Production puzzles append an extra section after the base puzzle payload.
    const bool hasProduction = reader.ReadBool();
    if (hasProduction) {
        ProductionInfo info;
        info.shrinkLeft = reader.ReadBool();
        info.shrinkRight = reader.ReadBool();
        info.isolation = reader.ReadBool();

        info.chambers = reader.ReadList<ProductionChamber>(3, [](ByteReader& localReader) {
            return ProductionChamber{
                .position = localReader.ReadHexCoordI8(),
                .chamberType = localReader.ReadString(),
            };
        });

        info.conduits = reader.ReadList<ProductionConduit>(8, [](ByteReader& localReader) {
            ProductionConduit conduit;
            conduit.positionA = localReader.ReadHexCoordI8();
            conduit.positionB = localReader.ReadHexCoordI8();
            conduit.path = localReader.ReadList<core::HexCoord>(
                2, [](ByteReader& nestedReader) { return nestedReader.ReadHexCoordI8(); });
            return conduit;
        });

        info.vials = reader.ReadList<ProductionVial>(7, [](ByteReader& localReader) {
            return ProductionVial{
                .position = localReader.ReadHexCoordI8(),
                .top = localReader.ReadBool(),
                .count = localReader.ReadI32(),
            };
        });

        puzzle.productionInfo = std::move(info);
    }

    if (reader.Remaining() != 0) {
        throw ParseError("Puzzle has trailing bytes: " + std::to_string(reader.Remaining()));
    }

    return puzzle;
}

PuzzleData BinaryParser::ParsePuzzleFile(const std::filesystem::path& puzzlePath) {
    auto logger = opusoptima::GetLoggerOrFallback("io");
    logger->info("Parsing puzzle file: {}", puzzlePath.string());
    const auto bytes = ReadAllBytes(puzzlePath);
    auto puzzle = ParsePuzzle(std::span<const std::byte>(bytes.data(), bytes.size()));
    logger->info("Puzzle '{}' loaded — {} inputs, {} outputs", puzzle.name, puzzle.inputs.size(),
                 puzzle.outputs.size());
    return puzzle;
}

std::vector<std::byte> BinaryParser::ReadAllBytes(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw ParseError("Cannot open file: " + path.string());
    }

    input.seekg(0, std::ios::end);
    const auto fileSize = input.tellg();
    if (fileSize < 0) {
        throw ParseError("Cannot determine file size: " + path.string());
    }

    const auto byteCount = static_cast<std::streamoff>(fileSize);
    if (!std::in_range<std::size_t>(byteCount) || !std::in_range<std::streamsize>(byteCount) ||
        static_cast<std::size_t>(byteCount) > std::vector<std::byte>().max_size()) {
        throw ParseError("File size exceeds representable buffer range: " + path.string());
    }

    input.seekg(0, std::ios::beg);

    std::vector<std::byte> bytes(static_cast<std::size_t>(byteCount));
    if (!bytes.empty()) {
        input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(byteCount));
    }

    if (!input) {
        throw ParseError("Failed to read file: " + path.string());
    }

    return bytes;
}

std::string SummarizePuzzle(const PuzzleData& puzzle) {
    std::size_t inputAtoms = 0;
    std::size_t outputAtoms = 0;
    std::size_t inputBonds = 0;
    std::size_t outputBonds = 0;

    for (const auto& input : puzzle.inputs) {
        inputAtoms += input.atoms.size();
        inputBonds += input.bonds.size();
    }

    for (const auto& output : puzzle.outputs) {
        outputAtoms += output.atoms.size();
        outputBonds += output.bonds.size();
    }

    std::ostringstream oss;
    oss << "Puzzle Summary\n";
    oss << "  Name: " << puzzle.name << '\n';
    oss << "  Version: " << puzzle.version << '\n';
    oss << "  CreatorSteamId: " << puzzle.creatorSteamId << '\n';
    oss << "  PermissionBits: 0x" << std::hex << puzzle.permissionBits << std::dec << '\n';
    oss << "  Inputs: " << puzzle.inputs.size() << " molecules, " << inputAtoms << " atoms, "
        << inputBonds << " bonds\n";
    oss << "  Outputs: " << puzzle.outputs.size() << " molecules, " << outputAtoms << " atoms, "
        << outputBonds << " bonds\n";
    oss << "  OutputMultiplier: " << puzzle.outputMultiplier << '\n';
    oss << "  ProductionMode: " << (puzzle.productionInfo.has_value() ? "yes" : "no") << '\n';
    return oss.str();
}

namespace {

/// Four header words precede instructions; the arm number follows them.
struct ObjectHeader {
    std::int32_t h0, h1, h2, h3;
};
ObjectHeader ReadObjectHeader(ByteReader& reader) {
    return {
        .h0 = reader.ReadI32(),
        .h1 = reader.ReadI32(),
        .h2 = reader.ReadI32(),
        .h3 = reader.ReadI32(),
    };
}

void ParseInstructions(ByteReader& reader, int count, std::vector<ArmInstruction>& instructions) {
    if (count < 0 || static_cast<std::size_t>(count) > reader.Remaining() / 5)
        throw ParseError("Invalid instruction count");
    instructions.reserve(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) {
        const auto param = reader.ReadI32();
        const auto action = static_cast<char>(reader.ReadU8());
        instructions.push_back({action, param});
    }
}

SolutionObject ParseSolutionObject(ByteReader& reader) {
    SolutionObject obj;
    obj.typeName = reader.ReadString();

    static_cast<void>(reader.ReadU8()); // marker (usually 0x01)
    obj.position.q = reader.ReadI32();
    obj.position.r = reader.ReadI32();

    const auto header = ReadObjectHeader(reader);
    obj.bodyData = {header.h0, header.h1, header.h2, header.h3};
    if (header.h3 < 0) throw ParseError("Negative instruction count");
    ParseInstructions(reader, header.h3, obj.instructions);

    if (obj.typeName == "track") {
        const int segCount = reader.ReadI32();
        obj.bodyData.push_back(segCount);
        if (segCount < 0 || static_cast<std::size_t>(segCount) > reader.Remaining() / 8)
            throw ParseError("Invalid track segment count");
        for (int i = 0; i < segCount; ++i) {
            obj.bodyData.push_back(reader.ReadI32());
            obj.bodyData.push_back(reader.ReadI32());
        }
        obj.bodyData.push_back(reader.ReadI32());
    } else {
        obj.bodyData.push_back(reader.ReadI32());
        if (obj.typeName == "pipe") {
            obj.bodyData.push_back(reader.ReadI32());
            const int segCount = reader.ReadI32();
            obj.bodyData.push_back(segCount);
            if (segCount < 0 || static_cast<std::size_t>(segCount) > reader.Remaining() / 8)
                throw ParseError("Invalid pipe segment count");
            for (int i = 0; i < segCount; ++i) {
                obj.bodyData.push_back(reader.ReadI32());
                obj.bodyData.push_back(reader.ReadI32());
            }
        }
    }

    return obj;
}

} // namespace

SolutionData BinaryParser::ParseSolution(const std::span<const std::byte> bytes) {
    constexpr int kSolutionFormatTag = 7;

    ByteReader reader(bytes);

    const int formatTag = reader.ReadI32();
    if (formatTag != kSolutionFormatTag) {
        throw ParseError("Unknown .solution format tag: " + std::to_string(formatTag) +
                         " (expected " + std::to_string(kSolutionFormatTag) + ")");
    }

    SolutionData solution;
    solution.puzzleName = reader.ReadString();
    solution.solutionName = reader.ReadString();

    const int metricsCount = reader.ReadI32();
    if (metricsCount < 0 || static_cast<std::size_t>(metricsCount) > reader.Remaining() / 8)
        throw ParseError("Invalid metrics count");
    for (int i = 0; i < metricsCount; ++i) {
        const int type = reader.ReadI32();
        const int value = reader.ReadI32();
        switch (type) {
        case 0:
            solution.metrics.cycles = value;
            break;
        case 1:
            solution.metrics.cost = value;
            break;
        case 2:
            solution.metrics.area = value;
            break;
        case 3:
            solution.metrics.instructionCount = value;
            break;
        default:
            break;
        }
    }

    const int objectCount = reader.ReadI32();
    // Empty type prefix, marker, position, header and ordinary tail occupy at least 30 bytes.
    constexpr std::size_t kMinimumObjectBytes = 1 + 1 + 8 + 16 + 4;
    if (objectCount < 0 ||
        static_cast<std::size_t>(objectCount) > reader.Remaining() / kMinimumObjectBytes)
        throw ParseError("Invalid object count");
    solution.objects.reserve(static_cast<std::size_t>(objectCount));

    for (int i = 0; i < objectCount; ++i) {
        solution.objects.push_back(ParseSolutionObject(reader));
    }

    return solution;
}

SolutionData BinaryParser::ParseSolutionFile(const std::filesystem::path& solutionPath) {
    auto logger = opusoptima::GetLoggerOrFallback("io");
    logger->info("Parsing solution file: {}", solutionPath.string());
    const auto bytes = ReadAllBytes(solutionPath);
    auto solution = ParseSolution(std::span<const std::byte>(bytes.data(), bytes.size()));
    logger->info("Solution '{}' loaded — {} objects, {} cycles", solution.solutionName,
                 solution.objects.size(), solution.metrics.cycles);
    return solution;
}

} // namespace opusoptima::io