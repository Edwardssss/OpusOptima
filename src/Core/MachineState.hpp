#pragma once

#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

#include "HexGrid.hpp"
#include "PuzzleSpecs.hpp"

namespace opusoptima::core {

enum class CellOccupant : std::uint8_t {
    Empty = 0,
    Atom,       // An atom
    ArmBase,    // Base of an arm
    ArmSegment, // A segment of an extended arm
    Bonder,     // Bonder glyph
    Track,      // Track segment
    Barrier,    // Impassable cell
};

/// Represents a single hex cell.
struct Cell {
    CellOccupant occupant{CellOccupant::Empty};
    std::uint8_t ownerId{};            // Which arm/object owns this cell
    AtomType atomType{AtomType::Salt}; // Valid only when occupant == Atom
};

/// Direction an arm is facing (0–5, mapping to hex directions).
enum class ArmDirection : std::int8_t { D0 = 0, D1, D2, D3, D4, D5 };

/// The type of arm mechanism.
enum class ArmType : std::uint8_t {
    Arm1 = 0, // standard arm1 (grab/rotate/drop)
    Piston,   // piston: extends/retracts via g/e
    Baron,    // baron: extends/retracts via A/a, E/e
    Track,    // track: moves along track segments
};

/// State of a single arm during simulation
struct ArmState {
    HexCoord basePosition{};                  // The arm base sits
    ArmDirection direction{ArmDirection::D0}; // Direction it faces
    int armLength{0};                         // Current extension (0 = retracted)
    bool hasAtom{false};                      // Holding an atom
    AtomType heldAtom{AtomType::Salt};        // Type of held atom (if hasAtom)
    std::size_t instructionPtr{0};            // Index into the instruction list
    int cycleOffset{0};                       // Cycles remaining on current action
    bool active{true};                        // Is this arm still running?
    ArmType armType{ArmType::Arm1};           // Type of arm mechanism
};

struct MachineState {
    std::int32_t cycle{0};

    std::unordered_map<HexCoord, Cell, HexCoordHash> grid;
    std::vector<ArmState> arms;
};

/// Known arm instruction actions
namespace ArmAction {
inline constexpr char kGrab = 'G';
inline constexpr char kDrop = 'X';
inline constexpr char kRotateCW = 'R';
inline constexpr char kRotateCCW = 'r';
inline constexpr char kPivotCW = 'P'; // Pivot clockwise (arm6-style)
inline constexpr char kPivotCCW = 'p';
inline constexpr char kExtend = 'g'; // Extend by one hex (piston-style)
inline constexpr char kRetract = 'e';
inline constexpr char kAdvance = 'A'; // Advance track/baron
inline constexpr char kWithdraw = 'a';
inline constexpr char kExtendHigh = 'E'; // Extended form (upper case)
inline constexpr char kCollision = 'C';  // Collision check / sync point
} // namespace ArmAction

/// Number of cycles each basic action takes
namespace ArmCycles {
inline constexpr int kGrabOrDrop = 2;
inline constexpr int kRotate = 2;
inline constexpr int kExtendOrRetract = 1;
inline constexpr int kPivot = 1;
} // namespace ArmCycles

} // namespace opusoptima::core
