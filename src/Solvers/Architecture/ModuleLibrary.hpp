#pragma once

#include "BinaryParser.hpp"
#include "HexGrid.hpp"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace opusoptima::solvers::architecture {

// ── M-B: access-point module library ─────────────────────────────────────────
//
// Upstream's module library is a set of pre-programmed machines, each with fixed
// input/output access cells, a fixed latency and a fixed tick offset. Assembly is
// then only translation + rotation + offset alignment.
//
// A `ModuleShape` is exactly that contract: where its atoms go in and come out,
// how many ticks it needs for one element, and the objects that implement it. It
// is both the description a scheduler reasons about and the emitter the compiler
// concatenates, so the two can never disagree.

struct AccessPoint {
    core::HexCoord cell{};
    /// Verifier direction index of the port face.
    int orientation{};
    /// Tick at which the element is present inside the module's own period.
    int tick{};
    friend bool operator==(const AccessPoint&, const AccessPoint&) = default;
};

enum class ModuleKind { AtomConveyor, AtomBuffer, SimpleInputArea, UniversalAssembler };

[[nodiscard]] std::string_view ModuleKindName(ModuleKind kind);

/// One object a module wants on the board, before any schedule shift.
struct ModuleObject {
    std::string type;
    core::HexCoord position{};
    std::vector<int> body;
    std::vector<io::ArmInstruction> tape;
};

struct ModuleShape {
    ModuleKind kind{ModuleKind::AtomConveyor};
    std::string name;
    core::HexCoord origin{};
    /// Ticks from an element entering to the same element leaving.
    int latency{};
    /// Ticks between two consecutive elements the module can service.
    int period{1};
    std::vector<AccessPoint> inputs;
    std::vector<AccessPoint> outputs;
    /// Exclusive cells the module's bodies occupy (arm discs, glyph footprints).
    std::vector<core::HexCoord> occupied;
    std::vector<ModuleObject> objects;
};

/// `SimpleInputArea`: up to four single-atom reagent ports, zero latency. The
/// element is available on the port cell from tick 0 of every period.
[[nodiscard]] ModuleShape BuildSimpleInputArea(core::HexCoord origin, int orientation,
                                               std::size_t inputIndex, core::AtomType element);

/// `AtomConveyor`: one `arm1` shuttle that grabs an element on the ring vertex
/// `grab` and drops it on `drop`, both at distance `length` from its pivot.
[[nodiscard]] ModuleShape BuildAtomConveyor(core::HexCoord pivot, int grab, int drop, int length);

/// `AtomBuffer`: one `arm1` that parks an element for one period on its own ring.
/// Used for waste chains and for holding an element until a downstream bank opens.
[[nodiscard]] ModuleShape BuildAtomBuffer(core::HexCoord pivot, int grab, int length);

/// `UniversalAssembler`: the row bank. One swing arm per target atom (grab from
/// the station, drop on the product cell) plus a bonder on every product bond and
/// one output area. Its period is the slowest arm in the bank, not their sum.
struct UniversalAssemblerSpec {
    io::Molecule target;
    /// Station cell per target atom, parallel to `target.atoms`.
    std::vector<core::HexCoord> stations;
    /// Grab direction per target atom.
    std::vector<int> grabs;
    /// Pivot per target atom.
    std::vector<core::HexCoord> pivots;
    std::vector<int> lengths;
    std::vector<int> drops;
    int period{};
};
[[nodiscard]] ModuleShape BuildUniversalAssembler(const UniversalAssemblerSpec& spec);

/// Cells the assembler's arms sweep; used to keep station cells clear.
[[nodiscard]] std::vector<core::HexCoord> AssemblerOccupied(const UniversalAssemblerSpec& spec);

} // namespace opusoptima::solvers::architecture
