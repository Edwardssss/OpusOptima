#include "StandardModules.hpp"

// Ported from gtw123/OpusSolver@fdedd3f01a238b941ceba8bace7ea35cbd3b8c41 (MIT).
// The module map is documented in README.md.


#include <algorithm>
#include <cstdlib>
#include <stdexcept>
#include <string_view>

namespace opusoptima::solvers::standard {

Vec2 AtomGenerator::OutputPosition() const {
    if (!outputArm_) return {0, 0};
    const Rot rot = outputArm_->LocalTransform().rotation;
    const Rot direction = (rot == R180 || rot == R240 || rot == R300) ? Rotate60CCW(rot)
                                                                     : Rotate60CW(rot);
    return OffsetInDirection(outputArm_->LocalTransform().position, direction,
                             outputArm_->extension);
}

// ── Mono disassemblers ──────────────────────────────────────────────────────
MonoDisassembler::MonoDisassembler(GameObject* parent, ProgramWriter& writer, Vec2 position,
                                   const Molecule& molecule, Rot direction, Instr instruction)
    : MoleculeDisassembler(parent, writer, position, molecule), instruction_(instruction) {
    CreateObjects(molecule, direction, instruction);
}

MonoDisassembler::MonoDisassembler(GameObject* parent, ProgramWriter& writer, Vec2 position,
                                   const Molecule& molecule, Vec2 armPosition, Rot armRotation,
                                   ArmType armType, int armExtension, Instr instruction)
    : MoleculeDisassembler(parent, writer, position, molecule), instruction_(instruction) {
    outputArm_ = Make<Arm>(this, armPosition, armRotation, armType, armExtension);
    Make<Reagent>(this, Vec2{0, 0}, R0, &molecule);
}

void MonoDisassembler::CreateObjects(const Molecule& molecule, Rot direction, Instr instruction) {
    const Vec2 pos = OffsetInDirection({0, 0}, direction, 1);
    Make<Reagent>(this, pos, R0, &molecule);
    if (instruction == Instr::Extend) {
        outputArm_ = Make<Arm>(this, pos * 2, Rotate180(direction), ArmType::Piston);
    } else if (instruction == Instr::MovePos) {
        outputArm_ = Make<Arm>(this, pos * 3, Rotate180(direction), ArmType::Arm1, 2);
        Make<Track>(this, pos * 3, Rotate180(direction), 2);
    } else if (instruction == Instr::RotateCCW) {
        const Vec2 armPos = OffsetInDirection({0, 0}, Rotate60CW(direction), 1);
        outputArm_ = Make<Arm>(this, armPos, Rotate60CCW(direction), ArmType::Arm1);
    } else if (instruction == Instr::RotateCW) {
        const Vec2 armPos = OffsetInDirection({0, 0}, Rotate60CCW(direction), 1);
        outputArm_ = Make<Arm>(this, armPos, Rotate60CW(direction), ArmType::Arm1);
    } else {
        throw std::invalid_argument("invalid monoatomic disassembler instruction");
    }
}

void MonoDisassembler::GenerateNextAtom() {
    writer.NewFragment();
    writer.WriteGrabResetAction(outputArm_, instruction_);
}

SingleMonoDisassembler::SingleMonoDisassembler(GameObject* parent, ProgramWriter& writer,
                                               Vec2 position, const Molecule& molecule)
    : MoleculeDisassembler(parent, writer, position, molecule) {
    Make<Reagent>(this, Vec2{0, 0}, R0, &molecule);
}

void SingleMonoDisassembler::GenerateNextAtom() { writer.NewFragment(); }

// ── Linear disassembler ─────────────────────────────────────────────────────
LinearDisassemblerModule::LinearDisassemblerModule(GameObject* parent, ProgramWriter& writer,
                                                   Vec2 position, const Molecule& molecule)
    : MoleculeDisassembler(parent, writer, position, molecule) {
    if (molecule.Height() != 1) throw std::invalid_argument("linear molecule expected");
    const Vec2 reagentPosition{-molecule.Width() - 2, 1};
    Make<Reagent>(this, reagentPosition, R0, &molecule);
    grabArm_ = Make<Arm>(this, Add(reagentPosition, 0, 1), R240, ArmType::Piston);
    outputArm_ = Make<Arm>(this, Vec2{-3, 3}, R240, ArmType::Arm1, 3);
    Make<Track>(this, grabArm_->LocalTransform().position, R0, molecule.Width() - 1);
    Make<Glyph>(this, Vec2{-4, 0}, R0, GlyphType::Unbonding);
}

void LinearDisassemblerModule::GenerateNextAtom() {
    if (step_ == 0) {
        writer.NewFragment();
        writer.Write(grabArm_, std::vector<Instr>{Instr::Grab, Instr::Extend});
        writer.WriteGrabResetAction(outputArm_, Instr::RotateCCW);
        step_ = 1;
        return;
    }
    const int width = molecule->Width();
    const int x = step_;
    writer.NewFragment();
    writer.Write(grabArm_, Instr::MovePos);
    if (x == width - 1) writer.Write(grabArm_, Instr::Reset, false);
    writer.WriteGrabResetAction(outputArm_, Instr::RotateCCW);
    step_ = (x + 1 >= width) ? 0 : x + 1;
}

namespace {

[[nodiscard]] bool SpecialDisassemblersEnabled() {
    const char* flag = std::getenv("OPUSOPTIMA_STANDARD_SPECIAL_DISASSEMBLERS");
    return flag == nullptr || std::string_view(flag) != "off";
}

[[nodiscard]] bool Bent3Compatible(const Molecule& molecule) {
    return molecule.Atoms().size() == 3 && molecule.Size() == 3;
}

[[nodiscard]] bool Bent3Oriented(const Molecule& molecule) {
    return molecule.GetAtom({0, 1}) != nullptr && molecule.GetAtom({1, 1}) != nullptr &&
           molecule.GetAtom({2, 0}) != nullptr;
}

void PrepareBent3(Molecule& molecule) {
    for (int turn = 0; turn < 6; ++turn) {
        if (Bent3Oriented(molecule)) return;
        molecule.Rotate60CW();
    }
    throw std::invalid_argument("unexpected bent three-atom molecule orientation");
}

[[nodiscard]] bool Triangle3Compatible(const Molecule& molecule) {
    return molecule.Atoms().size() == 3 && molecule.Size() == 2;
}

[[nodiscard]] bool Triangle3Oriented(const Molecule& molecule) {
    const Atom* first = molecule.GetAtom({0, 0});
    if (first == nullptr || first->bonds[R60.value] == BondType::None) return false;
    const Atom* second = molecule.GetAtom({1, 0});
    return second != nullptr && second->bonds[R120.value] != BondType::None;
}

void PrepareTriangle3(Molecule& molecule) {
    for (int turn = 0; turn < 6; ++turn) {
        if (Triangle3Oriented(molecule)) return;
        molecule.Rotate60CW();
    }
    throw std::invalid_argument("unexpected triangle three-atom molecule orientation");
}

[[nodiscard]] bool Triangle3AllBonds(const Molecule& molecule) {
    const Atom* first = molecule.GetAtom({0, 0});
    return first != nullptr && first->bonds[R0.value] != BondType::None;
}

} // namespace

void PrepareReagentMolecule(Molecule& molecule) {
    if (!SpecialDisassemblersEnabled() || molecule.Height() == 1) return;
    if (Bent3Compatible(molecule)) {
        PrepareBent3(molecule);
    } else if (Triangle3Compatible(molecule)) {
        PrepareTriangle3(molecule);
    }
}

std::vector<Element> ReagentElementOrder(const Molecule& molecule) {
    if (SpecialDisassemblersEnabled() && molecule.Height() != 1 && Bent3Compatible(molecule) &&
        Bent3Oriented(molecule)) {
        return {molecule.GetAtom({2, 0})->element, molecule.GetAtom({0, 1})->element,
                molecule.GetAtom({1, 1})->element};
    }
    if (SpecialDisassemblersEnabled() && molecule.Height() != 1 &&
        Triangle3Compatible(molecule) && Triangle3Oriented(molecule)) {
        std::vector<Element> order{molecule.GetAtom({0, 0})->element};
        if (Triangle3AllBonds(molecule)) {
            order.push_back(molecule.GetAtom({0, 1})->element);
            order.push_back(molecule.GetAtom({1, 0})->element);
        } else {
            order.push_back(molecule.GetAtom({1, 0})->element);
            order.push_back(molecule.GetAtom({0, 1})->element);
        }
        return order;
    }
    std::vector<Element> order;
    for (const Atom& atom : molecule.GetAtomsInInputOrder()) order.push_back(atom.element);
    return order;
}

Bent3DisassemblerModule::Bent3DisassemblerModule(GameObject* parent, ProgramWriter& writer,
                                                 Vec2 position, const Molecule& molecule)
    : MoleculeDisassembler(parent, writer, position, molecule) {
    const Vec2 reagentPosition{-6, -1};
    Make<Reagent>(this, reagentPosition, R0, &molecule);
    grabArm_ = Make<Arm>(this, reagentPosition + Vec2{1, 2}, R240, ArmType::Arm1);
    middleArm_ = Make<Arm>(this, reagentPosition + Vec2{2, 2}, R240, ArmType::Arm1);
    outputArm_ = Make<Arm>(this, Vec2{-3, 3}, R240, ArmType::Arm1, 3);
    Make<Glyph>(this, Vec2{-4, 0}, R0, GlyphType::Unbonding);
}

void Bent3DisassemblerModule::GenerateNextAtom() {
    writer.NewFragment();
    switch (step_) {
    case 0:
        writer.Write(grabArm_, std::vector<Instr>{Instr::Grab, Instr::RotateCCW});
        break;
    case 1:
        writer.Write(grabArm_,
                     std::vector<Instr>{Instr::PivotCCW, Instr::PivotCCW, Instr::Reset});
        writer.AdjustTime(-1);
        break;
    default:
        writer.WriteGrabResetAction(middleArm_, Instr::RotateCCW);
        break;
    }
    writer.WriteGrabResetAction(outputArm_, Instr::RotateCCW);
    step_ = (step_ + 1) % 3;
}

Triangle3DisassemblerModule::Triangle3DisassemblerModule(GameObject* parent,
                                                         ProgramWriter& writer, Vec2 position,
                                                         const Molecule& molecule)
    : MoleculeDisassembler(parent, writer, position, molecule),
      allBonds_(Triangle3AllBonds(molecule)) {
    const Vec2 reagentPosition{-6, 0};
    Make<Reagent>(this, reagentPosition, R0, &molecule);
    grabArm_ = Make<Arm>(this, reagentPosition + Vec2{0, 3}, R240, ArmType::Arm1,
                         allBonds_ ? 3 : 2);
    outputArm_ = Make<Arm>(this, Vec2{-3, 3}, R240, ArmType::Arm1, 3);
    Make<Glyph>(this, Vec2{-3, 0}, R120, GlyphType::Unbonding);
    Make<Track>(this, grabArm_->LocalTransform().position, allBonds_ ? R120 : R300, 1);
}

void Triangle3DisassemblerModule::GenerateNextAtom() {
    writer.NewFragment();
    if (allBonds_) {
        switch (step_) {
        case 0:
            writer.Write(grabArm_, std::vector<Instr>{Instr::Grab, Instr::RotateCCW,
                                                      Instr::PivotCCW, Instr::Drop});
            writer.AdjustTime(-1);
            break;
        case 1:
            writer.Write(grabArm_,
                         std::vector<Instr>{Instr::MovePos, Instr::Grab, Instr::PivotCCW});
            break;
        default:
            writer.Write(grabArm_, std::vector<Instr>{Instr::MoveNeg, Instr::Reset});
            writer.AdjustTime(-1);
            break;
        }
    } else {
        switch (step_) {
        case 0:
            writer.Write(grabArm_, std::vector<Instr>{Instr::Grab, Instr::RotateCCW});
            break;
        case 1:
            writer.Write(grabArm_, Instr::PivotCW);
            break;
        default:
            writer.Write(grabArm_, std::vector<Instr>{Instr::MovePos, Instr::Reset});
            writer.AdjustTime(-1);
            break;
        }
    }
    writer.WriteGrabResetAction(outputArm_, Instr::RotateCCW);
    step_ = (step_ + 1) % 3;
}

// ── Universal disassembler ──────────────────────────────────────────────────
UniversalDisassemblerModule::UniversalDisassemblerModule(GameObject* parent, ProgramWriter& writer,
                                                         Vec2 position, const Molecule& molecule)
    : MoleculeDisassembler(parent, writer, position, molecule) {
    addVerticalOffsetAtStart_ = IsProblematicMolecule();
    unbondWidth_ = std::max(3, molecule.Width());
    const Vec2 reagentOffset = addVerticalOffsetAtStart_ ? Vec2{0, -1} : Vec2{0, 0};
    const Vec2 reagentPosition{-molecule.Width() * 2 - unbondWidth_ - 2, -molecule.Height() + 1};
    Make<Reagent>(this, reagentPosition + reagentOffset, R0, &molecule);
    AddArms(reagentPosition);
    AddGlyphs();
    row_ = molecule.Height() - 1;
    phase_ = 0;
    coroutineStarted_ = false;
}

bool UniversalDisassemblerModule::IsProblematicMolecule() const {
    const Molecule& molecule = *this->molecule;
    if (molecule.Width() <= 2) return false;
    for (int y = 1; y < molecule.Height() - 1; ++y) {
        if (molecule.GetAtom({molecule.Width() - 1, y}) != nullptr &&
            molecule.GetAtom({molecule.Width() - 2, y}) == nullptr &&
            molecule.GetAtom({molecule.Width() - 1, y - 1}) != nullptr &&
            molecule.GetAtom({molecule.Width() - 2, y - 1}) != nullptr &&
            molecule.GetAtom({molecule.Width() - 1, y + 1}) == nullptr &&
            molecule.GetAtom({molecule.Width() - 2, y + 1}) == nullptr &&
            molecule.GetAtom({0, y}) != nullptr && molecule.GetAtom({1, y}) == nullptr &&
            molecule.GetAtom({0, y + 1}) != nullptr &&
            molecule.GetAtom({1, y + 1}) != nullptr &&
            molecule.GetAtom({0, y - 1}) == nullptr &&
            molecule.GetAtom({1, y - 1}) == nullptr) {
            return true;
        }
    }
    return false;
}

void UniversalDisassemblerModule::AddArms(Vec2 reagentPosition) {
    const Molecule& molecule = *this->molecule;
    const auto topRow = molecule.GetRow(molecule.Height() - 1);
    if (topRow.empty()) throw std::invalid_argument("empty disassembler row");
    const int grabX = topRow.back()->position.x;
    Vec2 armPosition = Add(reagentPosition, grabX, molecule.Height() + 2);
    Vec2 grabArmPosition = armPosition;
    if (addVerticalOffsetAtStart_) grabArmPosition.y -= 1;
    grabArm_ = Make<Arm>(this, grabArmPosition, R240, ArmType::Arm1, 3);
    if (addVerticalOffsetAtStart_) {
        Make<Track>(this, grabArmPosition,
                    std::vector<Track::Segment>{{R60, 1}, {R0, molecule.Width()}});
    } else {
        Make<Track>(this, armPosition, R0, molecule.Width());
    }

    armPosition = Add(reagentPosition, molecule.Width(), molecule.Height());
    for (int x = 0; x < molecule.Width(); ++x) {
        lowerUnbondArms_.push_back(
            Make<Arm>(this, Add(armPosition, x, 0), R240, ArmType::Piston, 2));
        upperUnbondArms_.push_back(
            Make<Arm>(this, Add(armPosition, x, 1), R240, ArmType::Arm1, 2));
        moveArms_.push_back(
            Make<Arm>(this, Add(armPosition, unbondWidth_ + x, 2), R240, ArmType::Arm1, 3));
    }
    AddTracks(armPosition);
}

void UniversalDisassemblerModule::AddTracks(Vec2 armPosition) {
    const Molecule& molecule = *this->molecule;
    Make<Track>(this, armPosition, R0, molecule.Width() + unbondWidth_ - 1);
    Make<Track>(this, Add(armPosition, 0, 1), R0, molecule.Width() + unbondWidth_ - 1);
    Make<Track>(this, Add(armPosition, unbondWidth_, 2),
                std::vector<Track::Segment>{{R0, molecule.Width() - 1},
                                            {R60, 1},
                                            {R180, molecule.Width() - 2}});
}

void UniversalDisassemblerModule::AddGlyphs() {
    const Vec2 glyphPosition{-unbondWidth_ - 3, 0};
    Make<Glyph>(this, glyphPosition, R240, GlyphType::Unbonding);
    Make<Glyph>(this, Add(glyphPosition, 1, 0), R300, GlyphType::Unbonding);
    Make<Glyph>(this, Add(glyphPosition, 2, 0), R0, GlyphType::Unbonding);
}

bool UniversalDisassemblerModule::NextAtom() {
    const Molecule& molecule = *this->molecule;
    const int width = molecule.Width();
    if (!coroutineStarted_) {
        coroutineStarted_ = true;
        writer.NewFragment();
        const int count = width + (addVerticalOffsetAtStart_ ? 1 : 0);
        writer.WriteGrabResetAction(grabArm_, std::vector<Instr>(static_cast<std::size_t>(count),
                                                                 Instr::MovePos));
        row_ = molecule.Height() - 1;
        phase_ = 1;
    }

    const std::vector<Instr> lowerGrab = [&] {
        std::vector<Instr> out{Instr::Grab};
        for (int i = 0; i < unbondWidth_; ++i) out.push_back(Instr::MovePos);
        for (int i = 0; i < unbondWidth_; ++i) out.push_back(Instr::MoveNeg);
        out.push_back(Instr::Retract);
        out.push_back(Instr::Drop);
        out.push_back(Instr::Reset);
        return out;
    }();
    const std::vector<Instr> lowerWait = [&] {
        std::vector<Instr> out{Instr::Wait};
        for (int i = 0; i < unbondWidth_; ++i) out.push_back(Instr::MovePos);
        for (int i = 0; i < unbondWidth_; ++i) out.push_back(Instr::MoveNeg);
        out.push_back(Instr::Wait);
        out.push_back(Instr::Wait);
        out.push_back(Instr::Reset);
        return out;
    }();
    const std::vector<Instr> upperGrab = [&] {
        std::vector<Instr> out{Instr::Grab};
        for (int i = 0; i < unbondWidth_; ++i) out.push_back(Instr::MovePos);
        out.push_back(Instr::Drop);
        out.push_back(Instr::Reset);
        return out;
    }();
    const std::vector<Instr> upperWait = [&] {
        std::vector<Instr> out{Instr::Wait};
        for (int i = 0; i < unbondWidth_; ++i) out.push_back(Instr::MovePos);
        out.push_back(Instr::Wait);
        out.push_back(Instr::Reset);
        return out;
    }();

    while (true) {
        if (phase_ == 0) return false;
        if (phase_ == 1) {
            if (row_ > 0) {
                const auto below = molecule.GetRow(row_ - 1);
                const int firstLower = below.front()->position.x;
                for (int x = firstLower; x < width; ++x) {
                    const bool present = molecule.GetAtom({x, row_ - 1}) != nullptr;
                    writer.Write(lowerUnbondArms_[static_cast<std::size_t>(x)],
                                 present ? lowerGrab : lowerWait, false);
                }
            }
            const auto current = molecule.GetRow(row_);
            const int firstUpper = current.front()->position.x;
            for (int x = firstUpper; x < width; ++x) {
                const bool present = molecule.GetAtom({x, row_}) != nullptr;
                writer.Write(upperUnbondArms_[static_cast<std::size_t>(x)],
                             present ? upperGrab : upperWait, false);
            }
            writer.AdjustTime(unbondWidth_ + 1);
            rowFirstAtomX_ = current.front()->position.x;
            column_ = width - 1;
            phase_ = 2;
            continue;
        }
        if (phase_ == 2) {
            if (column_ < rowFirstAtomX_) {
                --row_;
                if (row_ < 0) {
                    coroutineStarted_ = false;
                    phase_ = 0;
                    return false;
                }
                phase_ = 1;
                continue;
            }
            if (column_ == width - 1)
                writer.Write(moveArms_, Instr::Grab);
            else
                writer.Write(moveArms_, Instr::MovePos);
            const Atom* atom = molecule.GetAtom({column_, row_});
            if (atom) {
                writer.Write(moveArms_[static_cast<std::size_t>(column_)],
                             std::vector<Instr>{Instr::RotateCCW, Instr::Drop});
                if (column_ == rowFirstAtomX_)
                    writer.Write(moveArms_, Instr::Reset, false);
                writer.AdjustTime(-1);
                phase_ = 3;
                return true;
            }
            --column_;
            continue;
        }
        // phase_ == 3: resumed after an atom yield.
        writer.NewFragment();
        --column_;
        phase_ = 2;
    }
}

void UniversalDisassemblerModule::GenerateNextAtom() {
    if (!NextAtom()) {
        ResetCoroutine();
        if (!NextAtom()) throw std::invalid_argument("empty universal disassembly cycle");
    }
}

void UniversalDisassemblerModule::ResetCoroutine() {
    coroutineStarted_ = false;
    phase_ = 1;
    row_ = this->molecule->Height() - 1;
    column_ = this->molecule->Width() - 1;
}

// ── Input areas ─────────────────────────────────────────────────────────────
SimpleInputAreaModule::SimpleInputAreaModule(GameObject* parent, ProgramWriter& writer,
                                             const std::vector<const Molecule*>& reagents)
    : AtomGenerator(parent, writer) {
    if (reagents.size() > static_cast<std::size_t>(kMaxReagents))
        throw std::invalid_argument("too many simple input reagents");
    CreateDisassemblers(reagents);
}

void SimpleInputAreaModule::CreateDisassemblers(
    const std::vector<const Molecule*>& reagents) {
    if (reagents.empty()) throw std::invalid_argument("simple input area needs a reagent");
    if (reagents.size() == 1) {
        disassemblers_.push_back(Make<SingleMonoDisassembler>(this, writer, Vec2{0, 0},
                                                             *reagents.front()));
        return;
    }

    auto addArmDisassembler = [&](const Molecule& molecule, Vec2 position, Vec2 armPosition,
                                  Rot armRotation, ArmType armType, int extension,
                                  Instr instruction) {
        MonoDisassembler* disassembler = Make<MonoDisassembler>(
            this, writer, position, molecule, armPosition, armRotation, armType, extension,
            instruction);
        disassemblers_.push_back(disassembler);
    };

    addArmDisassembler(*reagents[0], Vec2{-1, 0}, Vec2{1, -1}, R120, ArmType::Arm1, 1,
                       Instr::RotateCW);

    switch (reagents.size()) {
    case 4:
        Make<Track>(this, Vec2{1, 1}, R180, 1);
        addArmDisassembler(*reagents[3], Vec2{1, 0}, Vec2{0, 1}, R240, ArmType::Arm1, 1,
                           Instr::MovePos);
        [[fallthrough]];
    case 3:
        addArmDisassembler(*reagents[1], Vec2{-2, 2}, Vec2{0, -2}, R60, ArmType::Arm1, 2,
                           Instr::RotateCW);
        addArmDisassembler(*reagents[2], Vec2{0, 2}, Vec2{2, -2}, R120, ArmType::Arm1, 2,
                           Instr::RotateCCW);
        break;
    case 2:
        addArmDisassembler(*reagents[1], Vec2{-1, 1}, Vec2{1, 0}, R180, ArmType::Arm1, 1,
                           Instr::RotateCCW);
        break;
    default: throw std::invalid_argument("invalid simple input reagent count");
    }
}

void SimpleInputAreaModule::Generate(Element element, int id) {
    (void)element;
    for (MoleculeDisassembler* disassembler : disassemblers_) {
        if (disassembler->molecule->Id() == id) {
            disassembler->GenerateNextAtom();
            return;
        }
    }
    throw std::invalid_argument("no disassembler for reagent");
}

ComplexInputAreaModule::ComplexInputAreaModule(GameObject* parent, ProgramWriter& writer,
                                               const std::vector<const Molecule*>& reagents)
    : AtomGenerator(parent, writer) {
    std::vector<const Molecule*> multiAtom;
    std::vector<const Molecule*> singleAtom;
    for (const Molecule* reagent : reagents) {
        if (reagent->Atoms().size() > 1)
            multiAtom.push_back(reagent);
        else
            singleAtom.push_back(reagent);
    }
    AddMultiAtomDisassemblers(multiAtom);
    if (multiAtom.size() == 1 && singleAtom.size() == 1) {
        disassemblers_.push_back(Make<SingleMonoDisassembler>(this, writer, Vec2{0, 2},
                                                             *singleAtom.front()));
    } else {
        AddSingleAtomDisassemblers(singleAtom);
    }
    if (disassemblers_.size() > 1) {
        int highestY = 0;
        for (MoleculeDisassembler* disassembler : disassemblers_) {
            highestY = std::max(highestY,
                                disassembler->LocalTransform().position.y +
                                    disassembler->OutputPosition().y);
        }
        conveyor_ = Make<AtomConveyorModule>(this, writer, Vec2{0, 0}, highestY);
    }
}

void ComplexInputAreaModule::AddMultiAtomDisassemblers(
    const std::vector<const Molecule*>& reagents) {
    for (const Molecule* reagent : reagents) {
        MoleculeDisassembler* disassembler = nullptr;
        if (reagent->Height() == 1) {
            disassembler = Make<LinearDisassemblerModule>(this, writer, Vec2{0, 0}, *reagent);
        } else if (SpecialDisassemblersEnabled() && Bent3Compatible(*reagent) &&
                   Bent3Oriented(*reagent)) {
            disassembler = Make<Bent3DisassemblerModule>(this, writer, Vec2{0, 0}, *reagent);
        } else if (SpecialDisassemblersEnabled() && Triangle3Compatible(*reagent) &&
                   Triangle3Oriented(*reagent)) {
            disassembler = Make<Triangle3DisassemblerModule>(this, writer, Vec2{0, 0}, *reagent);
        } else {
            disassembler = Make<UniversalDisassemblerModule>(this, writer, Vec2{0, 0}, *reagent);
        }
        if (!disassemblers_.empty()) {
            MoleculeDisassembler* previous = disassemblers_.back();
            int y = previous->LocalTransform().position.y + previous->Height() -
                    previous->HeightBelowOrigin() + disassembler->HeightBelowOrigin();
            if (y % 2 > 0) ++y;
            disassembler->LocalTransformRef().position = Vec2{0, y};
        }
        disassemblers_.push_back(disassembler);
    }
}

void ComplexInputAreaModule::AddSingleAtomDisassemblers(
    const std::vector<const Molecule*>& reagents) {
    int nextY = 2;
    std::set<Element> seen;
    for (const Molecule* reagent : reagents) {
        const Element element = reagent->Atoms().front().element;
        if (!seen.insert(element).second) continue;
        disassemblers_.push_back(Make<MonoDisassembler>(this, writer, Vec2{0, nextY}, *reagent,
                                                        R0, Instr::RotateCCW));
        nextY += 2;
    }
}

Vec2 ComplexInputAreaModule::OutputPosition() const {
    return conveyor_ ? conveyor_->OutputPosition() : Vec2{0, 0};
}

void ComplexInputAreaModule::Generate(Element element, int id) {
    (void)element;
    for (MoleculeDisassembler* disassembler : disassemblers_) {
        if (disassembler->molecule->Id() == id) {
            disassembler->GenerateNextAtom();
            if (conveyor_) {
                conveyor_->MoveAtom(disassembler->LocalTransform().position.y +
                                    disassembler->OutputPosition().y);
            }
            return;
        }
    }
    throw std::invalid_argument("no disassembler for reagent");
}

// ── Atom conveyor ───────────────────────────────────────────────────────────
AtomConveyorModule::AtomConveyorModule(GameObject* parent, ProgramWriter& writer, Vec2 position,
                                       int height)
    : SolverComponent(parent, writer, position, R0) {
    if (height % 2 > 0) throw std::invalid_argument("conveyor height must be even");
    for (int y = height - 2; y >= 2; y -= 2)
        downArms_.push_back(Make<Arm>(this, Vec2{2, y}, R120, ArmType::Arm1, 2));
    cornerArm_ = Make<Arm>(this, Vec2{-2, 2}, R0, ArmType::Arm1, 2);
    for (int i = 1; i <= 2; ++i)
        acrossArms_.push_back(Make<Arm>(this, Vec2{i * 2, -2}, R120, ArmType::Arm1, 2));
}

void AtomConveyorModule::MoveAtom(int startY) {
    for (Arm* arm : downArms_) {
        if (arm->LocalTransform().position.y + 2 <= startY)
            writer.WriteGrabResetAction(arm, Instr::RotateCCW);
    }
    if (startY > 0) writer.WriteGrabResetAction(cornerArm_, Instr::RotateCW);
    for (Arm* arm : acrossArms_) writer.WriteGrabResetAction(arm, Instr::RotateCW);
}

} // namespace opusoptima::solvers::standard
