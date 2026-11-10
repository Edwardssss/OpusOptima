#include "SingleAtomSolver.hpp"

#include <limits>
#include <stdexcept>

namespace opusoptima::solvers {

bool SingleAtomSolver::Supports(const io::PuzzleData& puzzle) noexcept {
    // arm1, rotation/drop, and grab permissions as defined by omsim decode.c.
    constexpr std::uint64_t required = (1ULL << 0) | (1ULL << 22) | (1ULL << 23);
    if (puzzle.productionInfo || puzzle.inputs.size() != 1 || puzzle.outputs.size() != 1 ||
        puzzle.outputMultiplier <= 0 ||
        puzzle.outputMultiplier > (std::numeric_limits<std::int32_t>::max() / 24) ||
        (puzzle.permissionBits & required) != required) {
        return false;
    }
    const auto& input = puzzle.inputs.front();
    const auto& output = puzzle.outputs.front();
    if (input.atoms.size() != 1 || output.atoms.size() != 1 || !input.bonds.empty() ||
        !output.bonds.empty()) {
        return false;
    }
    const auto type = input.atoms.front().type;
    if (type != output.atoms.front().type || type == core::AtomType::Repeat ||
        !core::AtomTypeFromId(static_cast<std::uint8_t>(type))) {
        return false;
    }
    // Translation must remain representable even for manually constructed data.
    const auto safe = [](core::HexCoord position, core::HexCoord target) {
        const auto representable = [](std::int64_t value) {
            return value >= std::numeric_limits<std::int32_t>::min() &&
                   value <= std::numeric_limits<std::int32_t>::max();
        };
        return representable(std::int64_t(target.q) - position.q) &&
               representable(std::int64_t(target.r) - position.r);
    };
    return safe(input.atoms.front().position, {1, 0}) &&
           safe(output.atoms.front().position, {0, 1});
}

std::int32_t SingleAtomSolver::EstimateCycles(const io::PuzzleData& puzzle) {
    if (!Supports(puzzle)) {
        throw std::invalid_argument("SingleAtomSolver requires one unchanged unbonded atom, "
                                    "standard production, and arm/grab/rotation permissions");
    }
    return 24 * puzzle.outputMultiplier - 1;
}

io::SolutionData SingleAtomSolver::Solve(const io::PuzzleData& puzzle) {
    const auto cycles = EstimateCycles(puzzle);
    io::SolutionData solution;
    solution.puzzleName = puzzle.name;
    solution.solutionName = "Single atom transfer";
    solution.metrics = {cycles, 20, 3, 4};

    io::SolutionObject arm;
    arm.typeName = "arm1";
    arm.position = {0, 0};
    arm.bodyData = {1, 0, 0, 4, 0};
    arm.instructions = {{'G', 0}, {'r', 1}, {'g', 2}, {'R', 3}};
    solution.objects.push_back(std::move(arm));

    // Molecule atom coordinates are local to their input/output placement.
    const auto place = [&](const char* name, core::HexCoord target, core::HexCoord local) {
        io::SolutionObject object;
        object.typeName = name;
        object.position = {target.q - local.q, target.r - local.r};
        object.bodyData = {1, 0, 0, 0, 0};
        return object;
    };
    solution.objects.push_back(
        place("input", {1, 0}, puzzle.inputs.front().atoms.front().position));
    solution.objects.push_back(
        place("out-std", {0, 1}, puzzle.outputs.front().atoms.front().position));
    return solution;
}

namespace {
const bool registered = [] {
    BaseSolver::Register("SingleAtomSolver", [] { return std::make_unique<SingleAtomSolver>(); });
    BaseSolver::Register("single-atom", [] { return std::make_unique<SingleAtomSolver>(); });
    return true;
}();
} // namespace

} // namespace opusoptima::solvers
