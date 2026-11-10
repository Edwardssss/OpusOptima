#include "DiatomicSolver.hpp"

#include <limits>
#include <stdexcept>

namespace opusoptima::solvers {
namespace {
using core::HexCoord;

HexCoord Rotate(HexCoord point, int turns) noexcept {
    while (turns-- > 0)
        point = core::RotateClockwise(point);
    return point;
}

bool ValidPair(const io::Molecule& molecule) noexcept {
    if (molecule.atoms.size() != 2 || molecule.bonds.size() != 1 || molecule.bonds[0].bits != 1)
        return false;
    for (const auto& atom : molecule.atoms) {
        if (!core::AtomTypeFromId(static_cast<std::uint8_t>(atom.type)) ||
            atom.type == core::AtomType::Repeat || atom.position.q < -128 ||
            atom.position.q > 127 || atom.position.r < -128 || atom.position.r > 127)
            return false;
    }
    const auto a = molecule.atoms[0].position;
    const auto b = molecule.atoms[1].position;
    const auto& bond = molecule.bonds[0];
    return core::Distance(a, b) == 1 &&
           ((bond.from == a && bond.to == b) || (bond.from == b && bond.to == a));
}

io::SolutionObject Place(const char* name, const io::Molecule& molecule, int anchor,
                         HexCoord target, HexCoord direction) {
    const auto local = molecule.atoms[anchor].position;
    const auto delta = core::Subtract(molecule.atoms[1 - anchor].position, local);
    int turns = 0;
    while (Rotate(delta, turns) != direction)
        ++turns;
    io::SolutionObject object;
    object.typeName = name;
    object.position = core::Subtract(target, Rotate(local, turns));
    object.bodyData = {1, turns, 0, 0, 0};
    return object;
}
} // namespace

bool DiatomicSolver::Supports(const io::PuzzleData& puzzle) noexcept {
    constexpr std::uint64_t required = (1ULL << 0) | (1ULL << 22) | (1ULL << 23);
    if (puzzle.productionInfo || puzzle.inputs.size() != 1 || puzzle.outputs.size() != 1 ||
        puzzle.outputMultiplier <= 0 ||
        puzzle.outputMultiplier > std::numeric_limits<std::int32_t>::max() / 24 ||
        (puzzle.permissionBits & required) != required || !ValidPair(puzzle.inputs[0]) ||
        !ValidPair(puzzle.outputs[0]))
        return false;
    const auto& a = puzzle.inputs[0].atoms;
    const auto& b = puzzle.outputs[0].atoms;
    return (a[0].type == b[0].type && a[1].type == b[1].type) ||
           (a[0].type == b[1].type && a[1].type == b[0].type);
}

std::int32_t DiatomicSolver::EstimateCycles(const io::PuzzleData& puzzle) {
    if (!Supports(puzzle))
        throw std::invalid_argument("DiatomicSolver requires one unchanged adjacent normal-bond "
                                    "pair, nonproduction, and arm/grab/rotation permissions");
    return 24 * puzzle.outputMultiplier - 1;
}

io::SolutionData DiatomicSolver::Solve(const io::PuzzleData& puzzle) {
    const auto cycles = EstimateCycles(puzzle);
    io::SolutionData solution;
    solution.puzzleName = puzzle.name;
    solution.solutionName = "Diatomic transfer";
    solution.metrics = {cycles, 20, 19, 4};
    io::SolutionObject arm;
    arm.typeName = "arm1";
    arm.bodyData = {3, 0, 0, 4, 0};
    arm.instructions = {{'G', 0}, {'r', 1}, {'g', 2}, {'R', 3}};
    solution.objects.push_back(std::move(arm));
    // Radial placement keeps both the rotating pair and the respawned input
    // clear of the base and each other, including during the rotation sweep.
    solution.objects.push_back(Place("input", puzzle.inputs[0], 0, {3, 0}, {1, 0}));
    const int outputAnchor =
        puzzle.outputs[0].atoms[0].type == puzzle.inputs[0].atoms[0].type ? 0 : 1;
    solution.objects.push_back(Place("out-std", puzzle.outputs[0], outputAnchor, {0, 3}, {0, 1}));
    return solution;
}

namespace {
const bool registered = [] {
    BaseSolver::Register("DiatomicSolver", [] { return std::make_unique<DiatomicSolver>(); });
    BaseSolver::Register("diatomic", [] { return std::make_unique<DiatomicSolver>(); });
    return true;
}();
} // namespace
} // namespace opusoptima::solvers
