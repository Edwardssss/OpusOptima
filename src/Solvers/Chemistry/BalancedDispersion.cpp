#include "AssemblySolver.hpp"

#include <algorithm>
#include <stdexcept>

namespace opusoptima::solvers {
io::SolutionData CompileBalancedDispersion(const io::PuzzleData& puzzle) {
    constexpr auto required = (1ULL << 0) | (1ULL << 3) | (1ULL << 8) | (1ULL << 12) |
                              (1ULL << 18) | (1ULL << 22) | (1ULL << 23);
    if (puzzle.version != 3 || puzzle.productionInfo || puzzle.outputMultiplier <= 0 ||
        puzzle.outputMultiplier > 8 || puzzle.outputs.size() != 4 ||
        (puzzle.permissionBits & required) != required)
        throw std::invalid_argument("balanced dispersion requires four cardinal-salt products");
    int source = -1;
    for (int i = 0; i < static_cast<int>(puzzle.inputs.size()); ++i)
        if (puzzle.inputs[i].atoms.size() == 1 && puzzle.inputs[i].bonds.empty() &&
            puzzle.inputs[i].atoms[0].type == core::AtomType::Quintessence)
            source = i;
    if (source < 0) throw std::invalid_argument("balanced dispersion requires quintessence input");
    int products[4] = {-1, -1, -1, -1};
    int saltIndices[4]{};
    int rotations[4]{};
    const core::HexCoord directions[] = {{-1, 0}, {1, 0}, {0, -1}, {1, -1}};
    auto scale = [](core::HexCoord d, int n) { return core::HexCoord{d.q * n, d.r * n}; };
    for (int p = 0; p < 4; ++p) {
        const auto& molecule = puzzle.outputs[p];
        if (molecule.atoms.size() != 2 || molecule.bonds.size() != 1 || molecule.bonds[0].bits != 1)
            throw std::invalid_argument("balanced dispersion requires ordinary diatomic products");
        if (molecule.bonds[0].from == molecule.bonds[0].to ||
            !((molecule.bonds[0].from == molecule.atoms[0].position &&
               molecule.bonds[0].to == molecule.atoms[1].position) ||
              (molecule.bonds[0].from == molecule.atoms[1].position &&
               molecule.bonds[0].to == molecule.atoms[0].position)))
            throw std::invalid_argument(
                "balanced dispersion requires a bond between product atoms");
        int salt = molecule.atoms[0].type == core::AtomType::Salt ? 0 : 1;
        int type = static_cast<int>(molecule.atoms[1 - salt].type) - 2;
        if (molecule.atoms[salt].type != core::AtomType::Salt || type < 0 || type >= 4 ||
            products[type] >= 0)
            throw std::invalid_argument("balanced dispersion requires one product per cardinal");
        auto delta =
            core::Subtract(molecule.atoms[1 - salt].position, molecule.atoms[salt].position);
        int rotation = 0;
        while (rotation < 6 && delta != scale(directions[type], -1)) {
            delta = core::RotateClockwise(delta);
            ++rotation;
        }
        if (rotation == 6)
            throw std::invalid_argument("balanced dispersion requires adjacent product atoms");
        products[type] = p;
        saltIndices[type] = salt;
        rotations[type] = rotation;
    }
    io::SolutionData result;
    result.puzzleName = puzzle.name;
    result.solutionName = "Balanced four-output dispersion";
    auto object = [&](const char* type, core::HexCoord cell, int rotation, int index) {
        io::SolutionObject obj;
        obj.typeName = type;
        obj.position = cell;
        obj.bodyData = {1, rotation, index, 0, static_cast<int>(result.objects.size())};
        result.objects.push_back(obj);
        return static_cast<int>(result.objects.size()) - 1;
    };
    object("input", scale(puzzle.inputs[source].atoms[0].position, -1), 0, source);
    object("glyph-dispersion", {0, 0}, 0, 0);
    for (int lane = 0; lane < 4; ++lane) {
        auto d = directions[lane];
        int rotation = 0;
        auto unit = core::HexCoord{1, 0};
        while (unit != d) {
            unit = core::RotateClockwise(unit);
            ++rotation;
        }
        auto armOffset = scale(core::RotateCounterClockwise(d), 3);
        auto base = core::Add(d, armOffset);
        int arm = object("arm1", base, (rotation + 2) % 6, 0);
        result.objects[arm].bodyData[0] = 3;
        int track = object("track", base, 0, 0);
        result.objects[track].bodyData = {1,   0,   0,       0,       4,       0,       0,
                                          d.q, d.r, 2 * d.q, 2 * d.r, 3 * d.q, 3 * d.r, 0};
        object("glyph-calcification", scale(d, 4), 0, 0);
        object("bonder", scale(d, 3), rotation, 0);
        auto salt = puzzle.outputs[products[lane]].atoms[saltIndices[lane]].position;
        for (int i = 0; i < rotations[lane]; ++i)
            salt = core::RotateClockwise(salt);
        object("out-std", core::Subtract(scale(d, 4), salt), rotations[lane], products[lane]);
        // A shared leading delay is otherwise removed by tape normalization.
        result.objects[arm].instructions.push_back({'O', 0});
        int time = 2;
        auto emit = [&](char action) {
            result.objects[arm].instructions.push_back({action, time++});
        };
        for (int repeat = 0; repeat < 6 * puzzle.outputMultiplier; ++repeat) {
            emit('G');
            emit('A');
            emit('A');
            emit('A');
            emit('g');
            emit('a');
            emit('a');
            emit('a');
            emit('G');
            emit('A');
            emit('A');
            emit('g');
            emit('a');
            emit('a');
        }
        emit('B');
    }
    return result;
}
} // namespace opusoptima::solvers
