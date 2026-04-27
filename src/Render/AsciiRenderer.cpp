#include "AsciiRenderer.hpp"

#include <algorithm>
#include <limits>
#include <sstream>
#include <unordered_map>

#include "PuzzleSpecs.hpp"

namespace opusoptima::render {

namespace {

struct Bounds {
    int minQ = std::numeric_limits<int>::max();
    int maxQ = std::numeric_limits<int>::min();
    int minR = std::numeric_limits<int>::max();
    int maxR = std::numeric_limits<int>::min();
    bool valid = false;
};

void ExpandBounds(Bounds& bounds, const core::HexCoord coord) {
    bounds.minQ = std::min(bounds.minQ, coord.q);
    bounds.maxQ = std::max(bounds.maxQ, coord.q);
    bounds.minR = std::min(bounds.minR, coord.r);
    bounds.maxR = std::max(bounds.maxR, coord.r);
    bounds.valid = true;
}

Bounds ComputeBounds(const io::Molecule& molecule) {
    Bounds bounds;
    for (const auto& atom : molecule.atoms) {
        ExpandBounds(bounds, atom.position);
    }
    for (const auto& bond : molecule.bonds) {
        ExpandBounds(bounds, bond.from);
        ExpandBounds(bounds, bond.to);
    }
    return bounds;
}

std::string RenderAtomList(const io::Molecule& molecule) {
    std::ostringstream oss;
    for (const auto& atom : molecule.atoms) {
        oss << "  - " << core::AtomTypeLabel(atom.type) << " @ " << atom.position << '\n';
    }
    return oss.str();
}

std::string RenderBondList(const io::Molecule& molecule) {
    std::ostringstream oss;
    for (const auto& bond : molecule.bonds) {
        oss << "  - " << bond.from << " -> " << bond.to << " (" << core::BondBitsToLabel(bond.bits)
            << ")\n";
    }
    return oss.str();
}

} // namespace

std::string AsciiRenderer::RenderPuzzle(const io::PuzzleData& puzzle) {
    std::ostringstream oss;
    oss << "ASCII Preview: " << puzzle.name << "\n";
    oss << "========================================\n";

    for (std::size_t i = 0; i < puzzle.inputs.size(); ++i) {
        oss << RenderMolecule(puzzle.inputs[i], "Input " + std::to_string(i + 1));
    }
    for (std::size_t i = 0; i < puzzle.outputs.size(); ++i) {
        oss << RenderMolecule(puzzle.outputs[i], "Output " + std::to_string(i + 1));
    }

    if (puzzle.productionInfo.has_value()) {
        oss << "Production chambers: " << puzzle.productionInfo->chambers.size() << '\n';
        oss << "Production conduits: " << puzzle.productionInfo->conduits.size() << '\n';
        oss << "Production vials: " << puzzle.productionInfo->vials.size() << '\n';
    }

    return oss.str();
}

std::string AsciiRenderer::RenderMolecule(const io::Molecule& molecule, const std::string& title) {
    std::ostringstream oss;
    oss << '\n' << title << "\n";
    oss << "----------------------------------------\n";

    if (molecule.atoms.empty()) {
        oss << "(no atoms)\n";
        return oss.str();
    }

    const Bounds bounds = ComputeBounds(molecule);

    std::unordered_map<core::HexCoord, char, core::HexCoordHash> atomSymbols;
    atomSymbols.reserve(molecule.atoms.size());
    for (const auto& atom : molecule.atoms) {
        atomSymbols.emplace(atom.position, core::AtomTypeSymbol(atom.type));
    }

    for (int min_r = bounds.minR; min_r <= bounds.maxR; ++min_r) {
        const int indent = min_r - bounds.minR;
        oss << std::string(static_cast<std::size_t>(indent), ' ');
        for (int min_q = bounds.minQ; min_q <= bounds.maxQ; ++min_q) {
            const core::HexCoord cursor{.q = min_q, .r = min_r};
            const auto hit = atomSymbols.find(cursor);
            oss << (hit != atomSymbols.end() ? hit->second : '.');
            if (min_q != bounds.maxQ) {
                oss << ' ';
            }
        }
        oss << '\n';
    }

    oss << "Atoms:\n" << RenderAtomList(molecule);
    if (!molecule.bonds.empty()) {
        oss << "Bonds:\n" << RenderBondList(molecule);
    }

    return oss.str();
}

} // namespace opusoptima::render
