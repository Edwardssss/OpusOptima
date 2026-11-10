#include "LinearInputArea.hpp"

#include <algorithm>
#include <stdexcept>

namespace opusoptima::solvers {
namespace {
int PushObject(io::SolutionData& machine, const char* type, core::HexCoord position, int length,
               int rotation, int index = 0) {
    io::SolutionObject object;
    object.typeName = type;
    object.position = position;
    object.bodyData = {length, rotation, index, 0, static_cast<int>(machine.objects.size())};
    machine.objects.push_back(std::move(object));
    return static_cast<int>(machine.objects.size()) - 1;
}

int PushTrack(io::SolutionData& machine, core::HexCoord position, int steps) {
    const int id = PushObject(machine, "track", position, 1, 0);
    auto& data = machine.objects[id].bodyData;
    data = {1, 0, 0, 0, steps + 1};
    for (int x = 0; x <= steps; ++x)
        data.insert(data.end(), {x, 0});
    data.push_back(0);
    return id;
}

void Push(LinearInputArea& area, int arm, char action) {
    area.machine.objects[arm].instructions.push_back({action, area.time});
    ++area.time;
}

void PushGrabReset(LinearInputArea& area, int arm, char action) {
    Push(area, arm, 'G');
    Push(area, arm, action);
    Push(area, arm, 'X');
    // Upstream `WriteGrabResetAction` rolls the global cursor back one tick so
    // the reset overlaps the next instruction.
    if (area.time > 0) --area.time;
}
} // namespace

LinearInputArea BuildLinearInputArea(const io::Molecule& molecule, core::HexCoord origin,
                                     int inputIndex) {
    if (molecule.atoms.empty()) throw std::invalid_argument("linear input requires atoms");
    int lowQ = molecule.atoms.front().position.q, highQ = lowQ;
    int lowR = molecule.atoms.front().position.r, highR = lowR;
    for (const auto& atom : molecule.atoms) {
        lowQ = std::min(lowQ, atom.position.q);
        highQ = std::max(highQ, atom.position.q);
        lowR = std::min(lowR, atom.position.r);
        highR = std::max(highR, atom.position.r);
    }
    if (highR != lowR)
        throw std::invalid_argument("linear input area requires a height-1 molecule");

    LinearInputArea area;
    area.input = inputIndex;
    area.width = highQ - lowQ + 1;
    area.machine.solutionName = "Linear input area";

    // Upstream `LinearDisassembler` constructor geometry.
    const core::HexCoord reagentPosition = core::Add(origin, {-area.width - 2, 1});
    PushObject(area.machine, "input", reagentPosition, 1, 0, inputIndex);
    area.unbonder = PushObject(area.machine, "unbonder", core::Add(origin, {-4, 0}), 1, 0);
    area.grabArm = PushObject(area.machine, "piston", core::Add(reagentPosition, {0, 1}), 1, 4);
    area.grabTrack =
        PushTrack(area.machine, core::Add(reagentPosition, {0, 1}), std::max(0, area.width - 1));
    area.outputArm = PushObject(area.machine, "arm1", core::Add(origin, {-3, 3}), 3, 4);
    area.outputPosition = core::Add(origin, {-3, 0});
    return area;
}

void EmitLinearInputAtom(LinearInputArea& area, int atom) {
    if (atom < 0 || atom >= area.width)
        throw std::invalid_argument("linear input atom out of range");
    if (atom == 0) {
        // The grab arm picks the first atom at the reagent and extends into the
        // output arm; the output arm rotates it and resets.
        Push(area, area.grabArm, 'G');
        Push(area, area.grabArm, 'E');
    } else {
        Push(area, area.grabArm, 'A');
        if (atom == area.width - 1) {
            // `Write(..., Reset, updateTime: false)`: write at the current tick
            // without advancing the shared cursor.
            area.machine.objects[area.grabArm].instructions.push_back({'X', area.time});
        }
    }
    PushGrabReset(area, area.outputArm, 'R');
}

} // namespace opusoptima::solvers
