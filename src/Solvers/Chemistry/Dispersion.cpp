#include "AssemblySolver.hpp"

#include <stdexcept>

namespace opusoptima::solvers {
io::SolutionData CompileDispersion(const io::PuzzleData& puzzle) {
    constexpr auto required =
        (1ULL << 0) | (1ULL << 17) | (1ULL << 18) | (1ULL << 22) | (1ULL << 23);
    if (puzzle.version != 3 || puzzle.outputMultiplier <= 0 || puzzle.productionInfo ||
        (puzzle.permissionBits & required) != required || puzzle.outputs.size() != 1)
        throw std::invalid_argument(
            "dispersion requires non-production single output and disposal");
    int source = -1;
    for (int i = 0; i < static_cast<int>(puzzle.inputs.size()); ++i)
        if (puzzle.inputs[i].atoms.size() == 1 && puzzle.inputs[i].bonds.empty() &&
            puzzle.inputs[i].atoms[0].type == core::AtomType::Quintessence) {
            source = i;
            break;
        }
    if (source < 0) throw std::invalid_argument("dispersion requires monatomic quintessence");
    for (const auto& atom : puzzle.outputs[0].atoms)
        if (static_cast<int>(atom.type) < 1 || static_cast<int>(atom.type) > 5)
            throw std::invalid_argument("dispersion supports cardinal and salt products");
    auto virtualPuzzle = puzzle;
    virtualPuzzle.inputs.clear();
    for (int type = 2; type <= 5; ++type) {
        io::Molecule input;
        input.atoms.push_back({static_cast<core::AtomType>(type), {0, 0}});
        virtualPuzzle.inputs.push_back(input);
    }
    auto result = AssemblySolver{}.Solve(virtualPuzzle);
    result.solutionName = "Independent dispersion feeds";
    const core::HexCoord outputs[] = {{-1, 0}, {1, 0}, {0, -1}, {1, -1}};
    std::vector<io::SolutionObject> additions;
    for (auto& object : result.objects) {
        if (object.typeName != "input") continue;
        const auto type = object.bodyData.at(2);
        const auto cell = object.position;
        // Keep the conversion inlet below its selected output, away from the transport lane.
        int rotation = 0;
        auto offset = outputs[type];
        for (int i = 1; i < 6; ++i) {
            auto candidate = outputs[type];
            for (int j = 0; j < i; ++j)
                candidate = core::RotateClockwise(candidate);
            if (candidate.r > offset.r) {
                offset = candidate;
                rotation = i;
            }
        }
        const auto center = core::Subtract(cell, offset);
        io::SolutionObject glyph;
        glyph.typeName = "glyph-dispersion";
        glyph.position = center;
        glyph.bodyData = {1, rotation, 0, 0, 0};
        additions.push_back(glyph);
        for (int other = 0; other < 4; ++other) {
            if (other == type) continue;
            auto delta = outputs[other];
            for (int j = 0; j < rotation; ++j)
                delta = core::RotateClockwise(delta);
            glyph.typeName = "glyph-disposal";
            glyph.position = core::Add(center, delta);
            glyph.bodyData = {1, 0, 0, 0, 0};
            additions.push_back(glyph);
        }
        object.position = core::Subtract(center, puzzle.inputs[source].atoms[0].position);
        object.bodyData.at(1) = 0;
        object.bodyData.at(2) = source;
    }
    result.objects.insert(result.objects.end(), additions.begin(), additions.end());
    return result;
}
} // namespace opusoptima::solvers
