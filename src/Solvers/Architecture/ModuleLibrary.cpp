#include "ModuleLibrary.hpp"

#include "SwingKernel.hpp"

#include <algorithm>
#include <cstdlib>
#include <stdexcept>

namespace opusoptima::solvers::architecture {
namespace {

using layout::SwingRotation;
using layout::SwingStep;

[[nodiscard]] core::HexCoord RingCell(core::HexCoord pivot, int direction, int length) {
    return core::Add(pivot, layout::SwingScale(SwingStep(direction), length));
}

// One balanced swing tape: grab, turn to the drop face, release, turn back, so the
// arm is in exactly the same state at the start of the next period.
[[nodiscard]] std::vector<io::ArmInstruction> SwingTape(int grab, int drop, int period) {
    const int rotation = SwingRotation(grab, drop);
    const int turns = rotation > 0 ? rotation : -rotation;
    std::vector<io::ArmInstruction> tape;
    int at = 0;
    tape.push_back({'G', at++});
    for (int step = 0; step < turns; ++step) tape.push_back({rotation > 0 ? 'r' : 'R', at++});
    tape.push_back({'g', at++});
    for (int step = 0; step < turns; ++step) tape.push_back({rotation > 0 ? 'R' : 'r', at++});
    while (at < period - 1) tape.push_back({'O', at++});
    tape.push_back({'X', at++});
    return tape;
}

[[nodiscard]] int SwingPeriod(int grab, int drop) {
    const int turns = std::abs(SwingRotation(grab, drop));
    return std::max((2 * turns) + 2, 3);
}

} // namespace

std::string_view ModuleKindName(ModuleKind kind) {
    switch (kind) {
    case ModuleKind::AtomConveyor: return "atom-conveyor";
    case ModuleKind::AtomBuffer: return "atom-buffer";
    case ModuleKind::SimpleInputArea: return "simple-input-area";
    case ModuleKind::UniversalAssembler: return "universal-assembler";
    }
    return "unknown";
}

ModuleShape BuildSimpleInputArea(core::HexCoord origin, int orientation, std::size_t inputIndex,
                                 core::AtomType element) {
    ModuleShape module;
    module.kind = ModuleKind::SimpleInputArea;
    module.name = "simple-input-area";
    module.origin = origin;
    module.latency = 0;
    module.period = 1;
    // The reagent atom appears on the port cell at tick 0 and is rewound after
    // being grabbed, so it is a zero-latency, period-one source.
    module.outputs.push_back({origin, orientation, 0});
    ModuleObject object;
    object.type = "input";
    object.position = origin;
    object.body = {1, orientation, static_cast<int>(inputIndex), 0, 0};
    module.objects.push_back(std::move(object));
    (void)element;
    return module;
}

ModuleShape BuildAtomConveyor(core::HexCoord pivot, int grab, int drop, int length) {
    if (length < 1 || length > layout::kMaxArmSize)
        throw std::invalid_argument("atom conveyor length out of range");
    ModuleShape module;
    module.kind = ModuleKind::AtomConveyor;
    module.name = "atom-conveyor";
    module.origin = pivot;
    const int period = SwingPeriod(grab, drop);
    module.latency = std::abs(SwingRotation(grab, drop)) + 1;
    module.period = period;
    module.inputs.push_back({RingCell(pivot, grab, length), grab, 0});
    module.outputs.push_back({RingCell(pivot, drop, length), drop, module.latency});
    for (const auto& cell : layout::SwingDisc(pivot, length))
        module.occupied.push_back(cell);
    ModuleObject object;
    object.type = "arm1";
    object.position = pivot;
    object.body = {length, grab, 0, 0, 0};
    object.tape = SwingTape(grab, drop, period);
    module.objects.push_back(std::move(object));
    return module;
}

ModuleShape BuildAtomBuffer(core::HexCoord pivot, int grab, int length) {
    if (length < 1 || length > layout::kMaxArmSize)
        throw std::invalid_argument("atom buffer length out of range");
    ModuleShape module;
    module.kind = ModuleKind::AtomBuffer;
    module.name = "atom-buffer";
    module.origin = pivot;
    // Park for one full turn on the same ring: grab, turn away, turn back, release.
    module.latency = 4;
    module.period = 6;
    module.inputs.push_back({RingCell(pivot, grab, length), grab, 0});
    module.outputs.push_back({RingCell(pivot, grab, length), grab, module.latency});
    for (const auto& cell : layout::SwingDisc(pivot, length))
        module.occupied.push_back(cell);
    ModuleObject object;
    object.type = "arm1";
    object.position = pivot;
    object.body = {length, grab, 0, 0, 0};
    object.tape = {{'G', 0}, {'r', 1}, {'O', 2}, {'O', 3}, {'R', 4}, {'g', 5}, {'X', 6}};
    module.objects.push_back(std::move(object));
    return module;
}

std::vector<core::HexCoord> AssemblerOccupied(const UniversalAssemblerSpec& spec) {
    std::vector<core::HexCoord> cells;
    for (std::size_t atom = 0; atom < spec.pivots.size(); ++atom) {
        const int length = atom < spec.lengths.size() ? spec.lengths[atom] : layout::kMaxArmSize;
        for (const auto& cell : layout::SwingDisc(spec.pivots[atom], length))
            cells.push_back(cell);
    }
    return cells;
}

ModuleShape BuildUniversalAssembler(const UniversalAssemblerSpec& spec) {
    if (spec.target.atoms.empty())
        throw std::invalid_argument("universal assembler needs a target");
    if (spec.pivots.size() != spec.target.atoms.size() ||
        spec.grabs.size() != spec.target.atoms.size() ||
        spec.drops.size() != spec.target.atoms.size() ||
        spec.stations.size() != spec.target.atoms.size())
        throw std::invalid_argument("universal assembler arrays disagree with the target");

    ModuleShape module;
    module.kind = ModuleKind::UniversalAssembler;
    module.name = "universal-assembler";
    module.origin = core::HexCoord{0, 0};
    // The bank is parallel: every arm works in the same period, so the bank needs
    // the slowest arm's ticks, not the sum of all of them.
    int period = 3;
    for (std::size_t atom = 0; atom < spec.target.atoms.size(); ++atom)
        period = std::max(period, SwingPeriod(spec.grabs[atom], spec.drops[atom]));
    if (spec.period > 0) period = std::max(period, spec.period);
    module.period = period;
    module.latency = period;

    for (std::size_t atom = 0; atom < spec.target.atoms.size(); ++atom) {
        const int length = spec.lengths[atom];
        module.inputs.push_back({spec.stations[atom], spec.grabs[atom], 0});
        for (const auto& cell : layout::SwingDisc(spec.pivots[atom], length))
            module.occupied.push_back(cell);
        ModuleObject object;
        object.type = "arm1";
        object.position = spec.pivots[atom];
        object.body = {length, spec.grabs[atom], 0, 0, 0};
        object.tape = SwingTape(spec.grabs[atom], spec.drops[atom], period);
        module.objects.push_back(std::move(object));
    }
    // One bonder per product bond, facing along the bond.
    for (const auto& bond : spec.target.bonds) {
        const int facing = layout::SwingDirectionOf(core::Subtract(bond.to, bond.from));
        if (facing < 0) throw std::invalid_argument("assembler target bond is not adjacent");
        ModuleObject bonder;
        bonder.type = "bonder";
        bonder.position = bond.from;
        bonder.body = {1, facing, 0, 0, 0};
        module.objects.push_back(std::move(bonder));
    }
    ModuleObject output;
    output.type = "out-std";
    output.position = core::HexCoord{0, 0};
    output.body = {1, 0, 0, 0, 0};
    module.objects.push_back(std::move(output));
    module.outputs.push_back({core::HexCoord{0, 0}, 0, period});
    return module;
}

} // namespace opusoptima::solvers::architecture
