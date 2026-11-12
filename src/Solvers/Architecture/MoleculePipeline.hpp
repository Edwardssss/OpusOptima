#pragma once

#include "BinaryParser.hpp"
#include "ElementPipeline.hpp"
#include "PipelineCompiler.hpp"

#include <cstddef>
#include <optional>
#include <vector>

namespace opusoptima::solvers::architecture {

// ── Molecular input: disassemble, then allocate with short swings ───────────
//
// Every emitter in this project that handled a molecular input either relayed the
// freed atoms through a chain of arms (one hop per cycle, so the period is the sum
// of the chain) or dropped them on a shared feeder. Both are serial at the level
// that matters: the measured cost was 114.7 cycles per tape instruction against a
// 3.8 reference.
//
// This builder takes the other shape. The molecule lands once, every bond is cut
// by an `unbonder`, and then **every freed atom gets its own arm** that swings it
// to its product cell in the same period. Only atoms that no single-atom port can
// supply are taken from the molecule, so a puzzle with a reagent port and a
// molecular feed emits both kinds side by side.
//
// It is a geometry search, not a planner: an arm can only carry an atom from A to
// B when `B - A` is one of the 90 swing offsets of an `arm1` (`L <= 3`), so the
// molecule's landing pose is chosen to make every displaced atom land on such an
// offset from its own product cell.

/// True when the builder has a domain for this puzzle.
[[nodiscard]] bool SupportsMoleculeAssembly(const io::PuzzleData& puzzle);

struct MoleculeVariant {
    io::SolutionData solution;
    int period{};
    /// How many product atoms came from the disassembled molecule.
    std::size_t fromMolecule{};
    /// Product atoms that a single-atom reagent port supplied in place.
    std::size_t fromPort{};
    std::size_t unbonders{};
};

/// Builds one machine. Throws `std::invalid_argument` outside the domain.
[[nodiscard]] io::SolutionData CompileMoleculeMachine(const io::PuzzleData& puzzle,
                                                      bool postProcess,
                                                      MoleculeVariant* detail = nullptr);

/// Appends the molecular variants this puzzle admits. Silent when the puzzle is
/// outside the domain, so the portfolio can call it unconditionally.
void AppendMoleculeVariants(const io::PuzzleData& puzzle, bool postProcess,
                            std::vector<PipelineVariant>& variants);

} // namespace opusoptima::solvers::architecture
