#include "ComposeTransfer.hpp"

#include <array>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <vector>

namespace opusoptima::solvers {
namespace {

// Axial direction table, identical to `core::kAxialDirections`; kept local so the
// placement math below reads as the component contract rather than as grid code.
struct HexStep {
    int q{};
    int r{};
};

constexpr std::array<HexStep, 6> kSteps = {{{1, 0}, {1, -1}, {0, -1}, {-1, 0}, {-1, 1}, {0, 1}}};

// Component geometry: an `arm1` grabs at its own reach along its facing
// direction, rotates by one 60-degree step and resets, leaving the atom one step
// away. `kGrabDirection` is the arm facing, `kDropStep` the signed rotation.
constexpr int kArmReach = 3;
constexpr int kGrabDirection = 3; // west
constexpr int kDropStep = -1;     // `r`: one counter-clockwise step

struct Port {
    core::HexCoord cell{};
};

int PushObject(io::SolutionData& machine, const char* type, core::HexCoord position,
               std::vector<int> body, std::vector<io::ArmInstruction> tape = {}) {
    io::SolutionObject object;
    object.typeName = type;
    object.position = position;
    object.instructions = std::move(tape);
    object.bodyData = std::move(body);
    machine.objects.push_back(std::move(object));
    return static_cast<int>(machine.objects.size()) - 1;
}

const io::Molecule& SingleAtomSource(const io::PuzzleData& puzzle, int element) {
    for (const auto& molecule : puzzle.inputs)
        if (molecule.atoms.size() == 1 && molecule.bonds.empty() &&
            static_cast<int>(molecule.atoms.front().type) == element)
            return molecule;
    throw std::invalid_argument("composed transfer needs a single-atom source for the product atom");
}

} // namespace

bool SupportsComposedTransfer(const io::PuzzleData& puzzle) {
    if (puzzle.productionInfo || puzzle.outputs.size() != 1 || puzzle.inputs.empty()) return false;
    const auto& output = puzzle.outputs.front();
    if (output.atoms.size() != 1 || !output.bonds.empty()) return false;
    const int element = static_cast<int>(output.atoms.front().type);
    for (const auto& molecule : puzzle.inputs)
        if (molecule.atoms.size() == 1 && molecule.bonds.empty() &&
            static_cast<int>(molecule.atoms.front().type) == element)
            return true;
    return false;
}

io::SolutionData ComposeTransferMachine(const io::PuzzleData& puzzle) {
    if (!SupportsComposedTransfer(puzzle))
        throw std::invalid_argument("composed transfer only covers one-atom products from a "
                                    "single-atom source");

    const auto& product = puzzle.outputs.front().atoms.front();
    const int element = static_cast<int>(product.type);
    const auto& source = SingleAtomSource(puzzle, element);
    const auto sourceIndex = static_cast<int>(&source - puzzle.inputs.data());

    // Placement is purely relative: the output port sits on the product frame and
    // the transfer arm is placed so its grab cell is one rotation away from it.
    const Port outputPort{.cell = {0, 0}};
    const int dropDirection = ((kGrabDirection + kDropStep) % 6 + 6) % 6;
    const HexStep drop = kSteps[static_cast<std::size_t>(dropDirection)];
    const HexStep grab = kSteps[static_cast<std::size_t>(kGrabDirection)];
    const core::HexCoord base{.q = outputPort.cell.q - kArmReach * drop.q,
                              .r = outputPort.cell.r - kArmReach * drop.r};
    const core::HexCoord reagent{.q = base.q + kArmReach * grab.q,
                                 .r = base.r + kArmReach * grab.r};

    io::SolutionData machine;
    machine.puzzleName = puzzle.name;
    machine.solutionName = "Composed direct transfer";
    PushObject(machine, "input", reagent, {1, 0, sourceIndex, 0, 0});
    PushObject(machine, "arm1", base, {kArmReach, kGrabDirection, 0, 0, 0},
               {{'G', 0}, {kDropStep < 0 ? 'r' : 'R', 1}, {'X', 2}});
    PushObject(machine, "out-std", outputPort.cell, {1, 0, 0, 0, 0});
    return machine;
}

} // namespace opusoptima::solvers
