#pragma once

#include "PhysicalInventory.hpp"
#include <chrono>
#include <functional>
#include <map>

namespace opusoptima::solvers {
// Lowers material IR in an explicitly supplied world layout. The certificate
// must cover all glyph intervals and motion; this function does not infer layout.
// Every Acquire, including the initial spawn, requires a glyph certificate;
// later batches also require certified input regeneration. Discard requires
// certified release/consumption. Neither operation infers always-on glyph safety.
// Calcify preserves world contact and rebinds the new product ID. Its logical
// origin is tracked separately until explicit relocation or a pre-bond alignment
// places the fragment in world space. Alignment itself requires certified motion.
// Bond can explicitly consume a parked partner into the held component; a
// rejected interval retries contact poses and either operand before failing.
// outputCounts declares a complete supply period; empty preserves one per output.
// Counts are checked against replay before any motion or glyph callback is run.
// releasedProducts maps reaction step indices to physical product cells in new-ID
// order. Operands must be at certified reaction contacts. A held reagent is
// released atomically with the reaction; other operands must be parked. The
// caller remains responsible for always-on glyph safety throughout delivery.
// The optional observer receives only committed step boundaries, including the
// certified initial input spawn. It is not called for rejected pose trials.
using InventoryStepObserver = std::function<void(std::size_t, const PhysicalInventory&)>;
[[nodiscard]] PhysicalInventory LowerInventoryIR(
    const io::PuzzleData&, const ExecutionIR&, const std::vector<core::HexCoord>& track,
    HeldArmState start, PhysicalInventory::Certificate, PhysicalInventory::BondCertificate,
    int maxLength = 1, std::size_t budget = 20000,
    const std::vector<std::size_t>& outputCounts = {},
    const std::map<std::size_t, std::vector<core::HexCoord>>& releasedProducts = {},
    const InventoryStepObserver& observe = {}, bool freeSingletonOrientation = false,
    const std::map<std::size_t, core::HexFrame>& wheelBindings = {},
    std::chrono::steady_clock::time_point deadline =
        std::chrono::steady_clock::time_point::max());
} // namespace opusoptima::solvers
