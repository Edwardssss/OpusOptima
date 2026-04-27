#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

#include "HexGrid.hpp"
#include "PuzzleSpecs.hpp"

namespace opusoptima::io {

class ParseError : public std::runtime_error {
    public:
    using std::runtime_error::runtime_error;
};

// Puzzle structures

struct MoleculeAtom {
    core::AtomType type{};
    core::HexCoord position{};
};

struct MoleculeBond {
    std::uint8_t bits{};
    core::HexCoord from{};
    core::HexCoord to{};
};

struct Molecule {
    std::vector<MoleculeAtom> atoms;
    std::vector<MoleculeBond> bonds;
};

struct ProductionChamber {
    core::HexCoord position{};
    std::string chamberType;
};

struct ProductionConduit {
    core::HexCoord positionA{};
    core::HexCoord positionB{};
    std::vector<core::HexCoord> path;
};

struct ProductionVial {
    core::HexCoord position{};
    bool top{};
    std::int32_t count{};
};

struct ProductionInfo {
    bool shrinkLeft{};
    bool shrinkRight{};
    bool isolation{};
    std::vector<ProductionChamber> chambers;
    std::vector<ProductionConduit> conduits;
    std::vector<ProductionVial> vials;
};

struct PuzzleData {
    std::int32_t version{};
    std::string name;
    std::uint64_t creatorSteamId{};
    std::uint64_t permissionBits{};
    std::vector<Molecule> inputs;
    std::vector<Molecule> outputs;
    std::int32_t outputMultiplier{};
    std::optional<ProductionInfo> productionInfo;
};

// Solution structures

/// Metrics reported in a .solution file
struct SolutionMetrics {
    std::int32_t cycles{};
    std::int32_t cost{};
    std::int32_t area{};
    std::int32_t instructionCount{};
};

/// A single arm instruction: a 1-byte action code followed by a 4-byte parameter
struct ArmInstruction {
    char action{};
    std::int32_t parameter{};
};

/// An object placed on the board
struct SolutionObject {
    std::string typeName;
    core::HexCoord position{};
    // Only meaningful for arm-type objects (arm1, arm2, …):
    std::vector<ArmInstruction> instructions;
    // Raw body data for any object (useful for debugging / forward-compatibility):
    std::vector<std::int32_t> bodyData;
};

/// Parsed content of a .solution file
struct SolutionData {
    std::string puzzleName;
    std::string solutionName;
    SolutionMetrics metrics;
    std::vector<SolutionObject> objects;
};

class BinaryParser {
    public:
    // Puzzle
    static PuzzleData ParsePuzzle(std::span<const std::byte> bytes);
    static PuzzleData ParsePuzzleFile(const std::filesystem::path& puzzlePath);
    static std::vector<std::byte> ReadAllBytes(const std::filesystem::path& path);

    // Solution
    static SolutionData ParseSolution(std::span<const std::byte> bytes);
    static SolutionData ParseSolutionFile(const std::filesystem::path& solutionPath);
};

std::string SummarizePuzzle(const PuzzleData& puzzle);

} // namespace opusoptima::io
