#include "RepeatingOutput.hpp"

#include "AssemblySolver.hpp"
#include "LifeDeath.hpp"
#include "RowAssembly.hpp"

#include <algorithm>
#include <map>
#include <set>
#include <stdexcept>

namespace opusoptima::solvers {
namespace {
using Cell = std::pair<int, int>;
Cell Key(core::HexCoord p) {
    return {p.q, p.r};
}
[[noreturn]] void Reject(const char* reason) {
    throw std::invalid_argument(reason);
}
} // namespace

bool HasRepeatingOutput(const io::Molecule& molecule) noexcept {
    return std::any_of(molecule.atoms.begin(), molecule.atoms.end(),
                       [](const auto& atom) { return atom.type == core::AtomType::Repeat; });
}

io::Molecule ExpandRepeatingOutput(const io::Molecule& molecule) {
    std::map<Cell, std::size_t> original;
    core::HexCoord offset;
    unsigned markers = 0;
    for (std::size_t i = 0; i < molecule.atoms.size(); ++i) {
        const auto& atom = molecule.atoms[i];
        if (!original.emplace(Key(atom.position), i).second)
            Reject("overlapping repeating target atoms");
        if (atom.type == core::AtomType::Repeat) {
            offset = atom.position;
            ++markers;
        }
    }
    if (markers != 1 || molecule.atoms.size() < 2 || offset == core::HexCoord{})
        Reject("repeating output requires one nonzero repeat marker and real atoms");
    if ((molecule.atoms.size() - 1) * 6 > AssemblyPlanner::MaxAtoms)
        Reject("six-monomer repeating target exceeds assembly atom limit");
    for (const auto& bond : molecule.bonds) {
        if ((bond.bits != 1 && (!bond.bits || (bond.bits & ~14))) ||
            core::Distance(bond.from, bond.to) != 1 || !original.contains(Key(bond.from)) ||
            !original.contains(Key(bond.to)))
            Reject("repeating output requires adjacent supported bonds with valid endpoints");
        if (bond.bits != 1)
            for (const auto cell : {bond.from, bond.to}) {
                const auto type = molecule.atoms[original.at(Key(cell))].type;
                if (type != core::AtomType::Fire && type != core::AtomType::Salt &&
                    type != core::AtomType::Repeat)
                    Reject("colored repeating bonds require fire/salt endpoints");
            }
    }
    io::Molecule expanded;
    std::map<Cell, std::size_t> cells;
    for (int copy = 0; copy < 6; ++copy) {
        for (const auto& atom : molecule.atoms) {
            if (atom.type == core::AtomType::Repeat) continue;
            const core::HexCoord cell{atom.position.q + copy * offset.q,
                                      atom.position.r + copy * offset.r};
            if (cell.q < -128 || cell.q > 127 || cell.r < -128 || cell.r > 127)
                Reject("expanded repeating output coordinates exceed puzzle range");
            if (!cells.emplace(Key(cell), expanded.atoms.size()).second)
                Reject("overlapping repeating monomers");
            expanded.atoms.push_back({atom.type, cell});
        }
    }
    // ianh/omsim a88d12d8d100784aa525ee90259255b869bcba38,
    // decode.c repeat_molecule: copy real atoms' directional bonds, then make
    // monomer boundary bonds reciprocal; the Repeat marker is never an atom.
    std::map<std::pair<std::size_t, std::size_t>, int> edges;
    for (int copy = 0; copy < 6; ++copy) {
        const core::HexCoord shift{copy * offset.q, copy * offset.r};
        for (const auto& bond : molecule.bonds) {
            for (bool reverse : {false, true}) {
                const auto from = reverse ? bond.to : bond.from;
                const auto to = reverse ? bond.from : bond.to;
                if (molecule.atoms[original.at(Key(from))].type == core::AtomType::Repeat) continue;
                const auto a = cells.at(Key(core::Add(from, shift)));
                const auto b = cells.find(Key(core::Add(to, shift)));
                if (b == cells.end()) continue;
                auto edge = std::minmax(a, b->second);
                auto& bits = edges[{edge.first, edge.second}];
                bits |= bond.bits;
                if ((bits & 1) && bits != 1)
                    Reject("conflicting ordinary and colored repeating bonds");
            }
        }
    }
    for (const auto& [edge, bits] : edges)
        expanded.bonds.push_back({static_cast<std::uint8_t>(bits),
                                  expanded.atoms[edge.first].position,
                                  expanded.atoms[edge.second].position});
    std::set<std::size_t> reached{0};
    bool changed = true;
    while (changed) {
        changed = false;
        for (const auto& [edge, bits] : edges) {
            const auto [a, b] = edge;
            if (reached.contains(a)) changed |= reached.insert(b).second;
            if (reached.contains(b)) changed |= reached.insert(a).second;
        }
    }
    if (reached.size() != expanded.atoms.size())
        Reject("repeating output expansion must form one connected polymer");
    return expanded;
}

io::SolutionData CompileRepeatingOutput(const io::PuzzleData& puzzle) {
    if (puzzle.outputs.size() != 1 || puzzle.productionInfo)
        Reject("repeating compiler requires one nonproduction output");
    auto finite = puzzle;
    finite.outputs[0] = ExpandRepeatingOutput(puzzle.outputs[0]);
    finite.outputMultiplier = 1;
    auto result = [&] {
        try {
            return CompileRowAssemblySingleProduct(finite, AssemblyPlanner::Plan(finite));
        } catch (const std::invalid_argument&) {
            try {
                return CompileLifeDeath(finite, true);
            } catch (const std::invalid_argument&) {
                return AssemblySolver{}.Solve(finite);
            }
        }
    }();
    std::int32_t halt = 0;
    for (const auto& object : result.objects)
        for (const auto& instruction : object.instructions)
            halt = std::max(halt, instruction.parameter + 1);
    for (auto& object : result.objects) {
        if (object.typeName == "out-std") object.typeName = "out-rep";
        // Unlike standard outputs, a repeating output leaves its polymer on
        // the board. Stop this machine before its tape can assemble another.
        if (!object.instructions.empty()) object.instructions.push_back({'B', halt});
    }
    result.solutionName = "Six-monomer repeating assembly";
    return result;
}
} // namespace opusoptima::solvers
