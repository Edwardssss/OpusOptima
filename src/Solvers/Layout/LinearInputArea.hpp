#pragma once

#include "AssemblySolver.hpp"
#include <vector>

namespace opusoptima::solvers {

// Port of the upstream `LinearDisassembler` input stage. It owns a local grab
// piston, a short source rail, an output arm and the source unbonder. The
// output arm hands one atom at a time to the shared conveyor; the caller drives
// `EmitLinearInputAtom` in supply order and must connect `outputPosition` to
// its handoff cell.
struct LinearInputArea {
    io::SolutionData machine;
    int input{};
    int width{};
    int grabArm{-1};
    int grabTrack{-1};
    int outputArm{-1};
    int unbonder{-1};
    core::HexCoord outputPosition{};
    int time{};
};

// `origin` is the component origin (upstream `LinearDisassembler` uses
// (-width-2, 1) for the reagent and (-3, 3) for the output arm). The molecule
// must be a horizontal linear chain (height 1).
LinearInputArea BuildLinearInputArea(const io::Molecule& molecule, core::HexCoord origin,
                                     int inputIndex);

// Appends the next atom's local extraction tape. `atom` is the index in the
// input molecule, consumed left-to-right as in the upstream coroutine.
void EmitLinearInputAtom(LinearInputArea& area, int atom);

} // namespace opusoptima::solvers
