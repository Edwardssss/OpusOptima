#include "Triplex.hpp"
#include "RowAssembly.hpp"

#include <algorithm>
#include <stdexcept>

namespace opusoptima::solvers {
namespace {
using core::HexCoord;
HexCoord Rotate(HexCoord c, int turns) {
    while (turns-- > 0)
        c = core::RotateClockwise(c);
    return c;
}
[[noreturn]] void Reject() {
    throw std::invalid_argument(
        "triplex requires colored fire/salt edges and row-assembly-compatible feeds");
}
} // namespace

io::SolutionData CompileTriplex(const io::PuzzleData& puzzle) {
    constexpr auto permissions =
        AssemblyPlanner::RequiredPermissions | (1ULL << 2) | (1ULL << 3) | (1ULL << 11);
    if (puzzle.version != 3 || puzzle.productionInfo || puzzle.outputs.size() != 1 ||
        puzzle.inputs.empty() || puzzle.inputs.size() > 4 || puzzle.outputMultiplier < 1 ||
        puzzle.outputMultiplier > 2 || (puzzle.permissionBits & permissions) != permissions)
        Reject();
    const auto& target = puzzle.outputs[0];
    for (const auto& atom : target.atoms)
        if (atom.position.q < -64 || atom.position.q > 64 || atom.position.r < -64 ||
            atom.position.r > 64)
            Reject();
    if (target.atoms.size() < 2 || target.atoms.size() > AssemblyPlanner::MaxAtoms) Reject();
    bool triplex = false;
    for (const auto& bond : target.bonds) {
        auto endpoint = [&](HexCoord c) {
            return std::find_if(target.atoms.begin(), target.atoms.end(),
                                [&](const auto& a) { return a.position == c; });
        };
        const auto a = endpoint(bond.from), b = endpoint(bond.to);
        if (a == target.atoms.end() || b == target.atoms.end() ||
            core::Distance(bond.from, bond.to) != 1)
            Reject();
        const auto proxyable = [](core::AtomType type) {
            return type == core::AtomType::Fire || type == core::AtomType::Salt;
        };
        if (bond.bits && !(bond.bits & ~14) && proxyable(a->type) && proxyable(b->type))
            triplex = true;
        else if (bond.bits != 1)
            Reject();
    }
    if (!triplex) Reject();
    auto ordinary = puzzle;
    bool postCalcification = false;
    for (auto& atom : ordinary.outputs[0].atoms) {
        if (atom.type == core::AtomType::Salt &&
            std::any_of(target.bonds.begin(), target.bonds.end(), [&](const auto& bond) {
                return bond.bits != 1 && (bond.from == atom.position || bond.to == atom.position);
            })) {
            atom.type = core::AtomType::Fire;
            postCalcification = true;
        }
    }
    if (postCalcification && !(puzzle.permissionBits & (1ULL << 12))) Reject();
    int rotation = 0;
    for (; rotation < 6; ++rotation) {
        const int row = Rotate(target.atoms[0].position, rotation).r;
        if (std::all_of(target.atoms.begin(), target.atoms.end(),
                        [&](const auto& a) { return Rotate(a.position, rotation).r == row; }))
            break;
    }
    if (rotation == 6) rotation = 0;
    for (auto& atom : ordinary.outputs[0].atoms)
        atom.position = Rotate(atom.position, rotation);
    for (auto& bond : ordinary.outputs[0].bonds) {
        bond.bits = 1;
        bond.from = Rotate(bond.from, rotation);
        bond.to = Rotate(bond.to, rotation);
    }
    auto labels = target;
    for (auto& atom : labels.atoms)
        atom.position = Rotate(atom.position, rotation);
    for (auto& bond : labels.bonds) {
        bond.from = Rotate(bond.from, rotation);
        bond.to = Rotate(bond.to, rotation);
    }
    auto machine = CompileTriplexRowAssembly(ordinary, AssemblyPlanner::Plan(ordinary), labels);
    for (auto& object : machine.objects)
        if (object.typeName == "out-std") object.bodyData[1] = rotation;
    machine.solutionName = "Serial row triplex assembly";
    return machine;
}
} // namespace opusoptima::solvers
