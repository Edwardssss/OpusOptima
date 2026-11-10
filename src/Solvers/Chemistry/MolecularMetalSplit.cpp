#include "MolecularMetalSplit.hpp"
#include "ModuleHandoff.hpp"

#include <stdexcept>

namespace opusoptima::solvers {
io::SolutionData CompileMolecularMetalSplit(const io::PuzzleData& p) {
    constexpr auto required =
        (1ULL << 0) | (1ULL << 2) | (1ULL << 9) | (1ULL << 15) | (1ULL << 22) | (1ULL << 23);
    if (p.version != 3 || p.productionInfo || p.outputMultiplier != 1 ||
        (p.permissionBits & required) != required || p.inputs.size() != 1 ||
        p.outputs.size() != 2 || p.inputs[0].atoms.size() != 2 || p.inputs[0].bonds.size() != 1 ||
        p.inputs[0].bonds[0].bits != 1)
        throw std::invalid_argument(
            "molecular metal split requires a bonded pair and two atomic outputs");
    for (const auto& output : p.outputs)
        if (output.atoms.size() != 1 || !output.bonds.empty())
            throw std::invalid_argument("molecular metal split requires atomic outputs");
    int metal = -1, product = -1;
    for (int i = 0; i < 2; ++i) {
        const int type = static_cast<int>(p.inputs[0].atoms[i].type);
        if (type >= 8 && type <= 12)
            for (int j = 0; j < 2; ++j)
                if (static_cast<int>(p.outputs[j].atoms[0].type) == type - 1 &&
                    p.outputs[1 - j].atoms[0].type == p.inputs[0].atoms[1 - i].type) {
                    metal = i;
                    product = j;
                }
    }
    if (metal < 0)
        throw std::invalid_argument(
            "molecular metal split requires one-stage purification and unchanged coproduct");
    const auto a = p.inputs[0].atoms[metal].position;
    const auto b = p.inputs[0].atoms[1 - metal].position;
    const auto bond = p.inputs[0].bonds[0];
    if (!((bond.from == a && bond.to == b) || (bond.from == b && bond.to == a)))
        throw std::invalid_argument("molecular metal split requires a bond between its two atoms");
    auto rotate = [](core::HexCoord cell, int n) {
        while (n-- > 0)
            cell = {-cell.r, cell.q + cell.r};
        return cell;
    };
    int orientation = -1;
    for (int i = 0; i < 6; ++i)
        if (rotate(core::Subtract(b, a), i) == core::HexCoord{1, 0}) orientation = i;
    if (orientation < 0)
        throw std::invalid_argument("molecular metal split requires adjacent atoms");
    io::SolutionData result;
    result.puzzleName = p.name;
    result.solutionName = "Trackless molecular metal split";
    auto object = [&](const char* type, core::HexCoord pos, int rotation, int index = 0) {
        io::SolutionObject obj;
        obj.typeName = type;
        obj.position = pos;
        obj.bodyData = {1, rotation, index, 0, static_cast<int>(result.objects.size())};
        result.objects.push_back(obj);
    };
    const int rotation = (orientation + 3) % 6;
    object("input", core::Subtract(core::HexCoord{0, 0}, rotate(a, rotation)), rotation);
    object("unbonder", {-1, 0}, 0);
    object("piston", {-4, 0}, 0);
    const auto waterArm = result.objects.size() - 1;
    result.objects[waterArm].bodyData[0] = 3;
    object("out-std",
           core::Subtract(core::HexCoord{-1, -3}, p.outputs[1 - product].atoms[0].position), 0,
           1 - product);
    object("arm1", {1, -1}, 2);
    const auto metalArm = result.objects.size() - 1;
    object("glyph-purification", {0, 0}, 0);
    object("arm1", {0, 2}, 4);
    const auto productArm = result.objects.size() - 1;
    std::vector<ModuleHandoff> handoffs;
    for (int batch = 0; batch < 6; ++batch) {
        const int start = batch * 40;
        const ModulePort purified{{0, 1}, 0, false};
        handoffs.push_back({purified, purified, start + 28, start + 30, start + 32, start + 68});
        for (int feed = 0; feed < 2; ++feed) {
            const int n = batch * 2 + feed;
            // Fill outer storage slots farthest-first, then use the inner ring.
            const int angle = n < 6 ? 1 : (n < 10 ? 11 - n : 13 - n);
            const bool retract = n >= 10;
            int t = start + feed * 20 + 1;
            auto& tape = result.objects[waterArm].instructions;
            tape.push_back({'G', t});
            t += 4;
            if (retract) tape.push_back({'e', ++t});
            for (int turn = 0; turn < angle; ++turn)
                tape.push_back({'R', ++t});
            tape.push_back({'g', ++t});
            for (int turn = 0; turn < angle; ++turn)
                tape.push_back({'r', ++t});
            if (retract) tape.push_back({'E', ++t});
        }
        auto& metalTape = result.objects[metalArm].instructions;
        for (const auto instruction : std::vector<io::ArmInstruction>{{'G', start + 2},
                                                                      {'R', start + 3},
                                                                      {'g', start + 4},
                                                                      {'r', start + 5},
                                                                      {'G', start + 24},
                                                                      {'g', start + 27}})
            metalTape.push_back(instruction);
        auto& productTape = result.objects[productArm].instructions;
        const auto& handoff = handoffs.back();
        for (const auto instruction : std::vector<io::ArmInstruction>{{'G', handoff.pickup},
                                                                      {'R', handoff.cleared - 1},
                                                                      {'g', handoff.cleared},
                                                                      {'r', handoff.cleared + 1}})
            productTape.push_back(instruction);
    }
    ValidateModuleHandoffs(handoffs);
    for (auto index : {waterArm, metalArm, productArm})
        result.objects[index].instructions.push_back({'O', 240});
    object("out-std", core::Subtract(core::HexCoord{-1, 2}, p.outputs[product].atoms[0].position),
           0, product);
    return result;
}
} // namespace opusoptima::solvers
