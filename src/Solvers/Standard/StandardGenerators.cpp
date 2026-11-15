#include "StandardModules.hpp"

// Ported from gtw123/OpusSolver@fdedd3f01a238b941ceba8bace7ea35cbd3b8c41 (MIT).
// See docs/analysis/UPSTREAM_STANDARD_PORT_20261115.md for the module map.


#include <algorithm>
#include <stdexcept>

namespace opusoptima::solvers::standard {

// ── Salt generators ─────────────────────────────────────────────────────────
SaltAtomGenerator::SaltAtomGenerator(GameObject* parent, ProgramWriter& writer)
    : AtomGenerator(parent, writer) {
    Make<Glyph>(this, Vec2{1, 0}, R0, GlyphType::Calcification);
    Make<Track>(this, Vec2{0, 2}, R0, 1);
    bigArm_ = Make<Arm>(this, Vec2{0, 2}, R240, ArmType::Arm1, 2);
    smallArm_ = Make<Arm>(this, Vec2{1, 1}, R240, ArmType::Arm1);
    outputArm_ = Make<Arm>(this, Vec2{5, 0}, R180, ArmType::Arm1, 3);
}

void SaltAtomGenerator::Generate(Element, int) {
    writer.WriteGrabResetAction(bigArm_, Instr::MovePos);
    writer.WriteGrabResetAction(smallArm_, Instr::RotateCCW);
    writer.WriteGrabResetAction(outputArm_, Instr::RotateCCW);
}

void SaltAtomGenerator::PassThrough(Element) {
    writer.WriteGrabResetAction(bigArm_, Instr::RotateCCW);
    writer.WriteGrabResetAction(outputArm_, Instr::RotateCCW);
}

SaltNoPassThroughAtomGenerator::SaltNoPassThroughAtomGenerator(GameObject* parent,
                                                               ProgramWriter& writer)
    : AtomGenerator(parent, writer) {
    Make<Glyph>(this, Vec2{0, 0}, R0, GlyphType::Calcification);
    outputArm_ = Make<Arm>(this, Vec2{3, 0}, R180, ArmType::Arm1, 3);
}

void SaltNoPassThroughAtomGenerator::Generate(Element, int) {
    writer.WriteGrabResetAction(outputArm_, Instr::RotateCCW);
}

void SaltNoPassThroughAtomGenerator::PassThrough(Element) {
    writer.WriteGrabResetAction(outputArm_, Instr::RotateCCW);
}

// ── Metal projector ─────────────────────────────────────────────────────────
ProjectorAtomGenerator::ProjectorAtomGenerator(GameObject* parent, ProgramWriter& writer)
    : AtomGenerator(parent, writer) {
    Make<Glyph>(this, Vec2{0, 0}, R0, GlyphType::Projection);
    projectionArm_ = Make<Arm>(this, Vec2{0, 1}, R240, ArmType::Arm1);
    outputArm_ = Make<Arm>(this, Vec2{4, 0}, R180, ArmType::Arm1, 3);
}

void ProjectorAtomGenerator::Consume(Element, int) {
    if (!hasMetal_) {
        writer.WriteGrabResetAction(projectionArm_, Instr::RotateCCW);
        hasMetal_ = true;
    }
}

void ProjectorAtomGenerator::Generate(Element, int) {
    writer.AdjustTime(-1);
    writer.WriteGrabResetAction(outputArm_, Instr::RotateCCW);
    hasMetal_ = false;
}

void ProjectorAtomGenerator::PassThrough(Element) {
    writer.WriteGrabResetAction(projectionArm_, Instr::RotateCCW);
    writer.WriteGrabResetAction(outputArm_, Instr::RotateCCW);
}

// ── Metal purifier ──────────────────────────────────────────────────────────
PurifierAtomGenerator::PurifierAtomGenerator(
    GameObject* parent, ProgramWriter& writer,
    const std::vector<MetalPurifierGenerator::PurificationSequence>& sequences)
    : AtomGenerator(parent, writer), sequences_(sequences) {
    int size = 0;
    for (const auto& sequence : sequences_) {
        size = std::max(size, PeriodicTable::GetMetalDifference(sequence.lowestMetalUsed,
                                                                sequence.targetMetal));
    }
    size_ = size;
    CreateObjects();
}

void PurifierAtomGenerator::CreateObjects() {
    for (int i = 0; i < size_; ++i) {
        Purifier purifier;
        const Vec2 pos{i * 2, 1};
        Make<Glyph>(this, pos, R240, GlyphType::Purification);
        const ArmType armType = i == 0 ? ArmType::Piston : ArmType::Arm1;
        purifier.bigArm = Make<Arm>(this, Add(pos, 0, 1), R240, armType, 2);
        purifier.smallArm = Make<Arm>(this, Add(pos, 1, 0), R240, ArmType::Arm1);
        purifiers_.push_back(purifier);
    }
    outputArm_ = Make<Arm>(this, Vec2{size_ * 2 + 3, 0}, R180, ArmType::Arm1, 3);
}

void PurifierAtomGenerator::Consume(Element element, int id) {
    const auto& sequence = sequences_.at(static_cast<std::size_t>(id));
    int purifierIndex = ElemIndex(element) - ElemIndex(sequence.lowestMetalUsed);
    for (int index = 0; index < purifierIndex; ++index) {
        writer.WriteGrabResetAction(purifiers_[static_cast<std::size_t>(index)].bigArm,
                                    Instr::RotateCCW);
    }
    Purifier* purifier = &purifiers_[static_cast<std::size_t>(purifierIndex)];
    if (!purifier->currentElement) {
        if (purifierIndex == 0) {
            writer.WriteGrabResetAction(purifier->bigArm, Instr::Retract);
        } else {
            Purifier& previous = purifiers_[static_cast<std::size_t>(purifierIndex - 1)];
            writer.AdjustTime(-1);
            writer.Write(previous.smallArm, Instr::RotateCCW);
            writer.WriteGrabResetAction(previous.smallArm, Instr::RotateCCW);
        }
        purifier->currentElement = element;
        return;
    }

    const int finalPurifierIndex =
        ElemIndex(sequence.targetMetal) - ElemIndex(sequence.lowestMetalUsed) - 1;
    while (purifierIndex <= finalPurifierIndex) {
        writer.Write(purifier->smallArm, Instr::Wait);
        purifier->currentElement.reset();
        if (purifierIndex == finalPurifierIndex) {
            writer.WriteGrabResetAction(purifier->smallArm, Instr::RotateCCW);
            break;
        }
        Purifier& next = purifiers_[static_cast<std::size_t>(purifierIndex + 1)];
        if (!next.currentElement) {
            writer.WriteGrabResetAction(
                purifier->smallArm, std::vector<Instr>{Instr::RotateCCW, Instr::RotateCCW});
            next.currentElement = element;
            break;
        }
        writer.WriteGrabResetAction(purifier->smallArm, Instr::RotateCCW);
        ++purifierIndex;
        purifier = &purifiers_[static_cast<std::size_t>(purifierIndex)];
    }
}

void PurifierAtomGenerator::Generate(Element element, int id) {
    for (const Purifier& purifier : purifiers_)
        if (purifier.currentElement)
            throw std::invalid_argument("expected all purifiers to be empty");
    const auto& sequence = sequences_.at(static_cast<std::size_t>(id));
    if (element != sequence.targetMetal)
        throw std::invalid_argument("purifier generated the wrong metal");
    const int purifierIndex = ElemIndex(element) - ElemIndex(sequence.lowestMetalUsed);
    PassThroughAtom(purifierIndex);
}

void PurifierAtomGenerator::PassThrough(Element) { PassThroughAtom(0); }

void PurifierAtomGenerator::PassThroughAtom(int firstPurifier) {
    for (int purifier = firstPurifier; purifier < size_; ++purifier) {
        writer.WriteGrabResetAction(purifiers_[static_cast<std::size_t>(purifier)].bigArm,
                                    Instr::RotateCCW);
    }
    writer.WriteGrabResetAction(outputArm_, Instr::RotateCCW);
}

// ── Mors / Vitae ────────────────────────────────────────────────────────────
MorsVitaeAtomGenerator::MorsVitaeAtomGenerator(GameObject* parent, ProgramWriter& writer)
    : AtomGenerator(parent, writer) {
    inputArm_ = Make<Arm>(this, Vec2{2, -2}, R120, ArmType::Arm1, 2);
    saltArm_ = Make<Arm>(this, Vec2{1, -1}, R120, ArmType::Arm1);
    Make<Glyph>(this, Vec2{1, 0}, R0, GlyphType::Animismus);
    vitaeArm_ = Make<Arm>(this, Vec2{2, 1}, R180, ArmType::Arm1);
    morsArm_ = Make<Arm>(this, Vec2{3, -1}, R180, ArmType::Arm1);
    outputArm_ = Make<Arm>(this, Vec2{4, -2}, R120, ArmType::Arm1, 2);
}

void MorsVitaeAtomGenerator::Consume(Element, int) {
    if (consumeStep_ == 0) {
        writer.WriteGrabResetAction(inputArm_, Instr::RotateCW);
        consumeStep_ = 1;
    } else {
        writer.WriteGrabResetAction(saltArm_, Instr::RotateCW);
        consumeStep_ = 0;
    }
}

void MorsVitaeAtomGenerator::Generate(Element element, int) {
    generatingElement_ = element;
    if (generateStep_ == 0) {
        writer.AdjustTime(1);
        MoveToOutput(generatingElement_);
        generateStep_ = 1;
    } else {
        writer.NewFragment();
        writer.Write(inputArm_, std::vector<Instr>{Instr::Wait, Instr::Wait}, false);
        MoveToOutput(generatingElement_);
        generateStep_ = 0;
    }
}

void MorsVitaeAtomGenerator::MoveToOutput(Element element) {
    if (element == Element::Mors)
        writer.WriteGrabResetAction(morsArm_, Instr::RotateCW);
    else
        writer.WriteGrabResetAction(vitaeArm_, Instr::RotateCCW);
    writer.WriteGrabResetAction(outputArm_, Instr::RotateCW);
}

void MorsVitaeAtomGenerator::PassThrough(Element) {
    writer.WriteGrabResetAction(inputArm_, Instr::RotateCW);
    writer.WriteGrabResetAction(outputArm_, Instr::RotateCW);
}

// ── Quintessence ────────────────────────────────────────────────────────────
QuintessenceAtomGenerator::QuintessenceAtomGenerator(GameObject* parent, ProgramWriter& writer)
    : AtomGenerator(parent, writer) {
    Make<Glyph>(this, Vec2{1, -1}, R0, GlyphType::Unification);
    leftArm_ = Make<Arm>(this, Vec2{0, -1}, R60, ArmType::Arm1);
    rightArm_ = Make<Arm>(this, Vec2{2, 0}, R180, ArmType::Piston, 2);
    outputArm_ = Make<Arm>(this, Vec2{4, -1}, R180, ArmType::Arm1, 3);
}

void QuintessenceAtomGenerator::Consume(Element, int) {
    switch (consumeStep_) {
    case 0: writer.WriteGrabResetAction(rightArm_, Instr::RotateCCW); break;
    case 1:
        writer.WriteGrabResetAction(leftArm_,
                                    std::vector<Instr>{Instr::RotateCW, Instr::RotateCW});
        break;
    case 2: writer.WriteGrabResetAction(rightArm_, Instr::Retract); break;
    default: break;
    }
    consumeStep_ = (consumeStep_ + 1) % 4;
}

void QuintessenceAtomGenerator::Generate(Element, int) {
    writer.AdjustTime(1);
    writer.WriteGrabResetAction(outputArm_, Instr::RotateCCW);
}

void QuintessenceAtomGenerator::PassThrough(Element) {
    writer.WriteGrabResetAction(leftArm_, Instr::RotateCW);
    writer.WriteGrabResetAction(outputArm_, Instr::RotateCCW);
}

// ── Quintessence disperser ──────────────────────────────────────────────────
DisperserAtomGenerator::DisperserAtomGenerator(GameObject* parent, ProgramWriter& writer)
    : AtomGenerator(parent, writer) {
    inputArm_ = Make<Arm>(this, Vec2{2, -2}, R120, ArmType::Arm1, 2);
    Make<Glyph>(this, Vec2{3, 0}, R0, GlyphType::Dispersion);
    disperseArm_ = Make<Arm>(this, Vec2{2, 1}, R240, ArmType::Arm1);
    airFireArm_ = Make<Arm>(this, Vec2{4, -2}, R120, ArmType::Piston, 2);
    waterArm_ = Make<Arm>(this, Vec2{4, 1}, R240, ArmType::Piston, 2);
    outputArm_ = Make<Arm>(this, Vec2{6, -2}, R120, ArmType::Arm1, 2);
}

void DisperserAtomGenerator::Consume(Element, int) {
    writer.WriteGrabResetAction(inputArm_, Instr::RotateCW);
    writer.WriteGrabResetAction(disperseArm_, Instr::RotateCCW);
}

void DisperserAtomGenerator::Generate(Element, int) {
    switch (generateStep_) {
    case 0:
        writer.AdjustTime(1);
        writer.WriteGrabResetAction(outputArm_, Instr::RotateCW);
        break;
    case 1:
        writer.NewFragment();
        writer.WriteGrabResetAction(airFireArm_, Instr::RotateCW);
        writer.WriteGrabResetAction(outputArm_, Instr::RotateCW);
        break;
    case 2:
        writer.NewFragment();
        writer.WriteGrabResetAction(waterArm_, Instr::Retract);
        writer.WriteGrabResetAction(outputArm_, Instr::RotateCW);
        break;
    default:
        writer.NewFragment();
        writer.Write(airFireArm_, Instr::Retract);
        writer.WriteGrabResetAction(airFireArm_,
                                    std::vector<Instr>{Instr::RotateCW, Instr::Extend});
        writer.WriteGrabResetAction(outputArm_, Instr::RotateCW);
        break;
    }
    generateStep_ = (generateStep_ + 1) % 4;
}

void DisperserAtomGenerator::PassThrough(Element) {
    writer.WriteGrabResetAction(inputArm_, Instr::RotateCW);
    writer.WriteGrabResetAction(airFireArm_, Instr::RotateCW);
    writer.WriteGrabResetAction(outputArm_, Instr::RotateCW);
}

// ── Van Berlo ───────────────────────────────────────────────────────────────
namespace {
const std::array<std::pair<Rot, Element>, 6> kVanBerloElements{{
    {R0, Element::Salt},   {R60, Element::Air},  {R120, Element::Water},
    {R180, Element::Salt}, {R240, Element::Earth}, {R300, Element::Fire},
}};
}

void VanBerloController::RotateToElement(Element element) {
    Rot destination{};
    for (const auto& [rotation, produced] : kVanBerloElements) {
        if (produced == element) {
            destination = rotation;
            break;
        }
    }
    if (isFirstAtom_) {
        wheelArm_->LocalTransformRef().rotation = destination;
        isFirstAtom_ = false;
    } else {
        const int delta = (destination.value - currentWheelRotation_.value + 6) % 6;
        std::vector<Instr> rotations;
        if (delta > 3) {
            rotations.assign(static_cast<std::size_t>(6 - delta), Instr::RotateCW);
        } else {
            rotations.assign(static_cast<std::size_t>(delta), Instr::RotateCCW);
        }
        if (!rotations.empty()) {
            writer_.AdjustTime(-static_cast<int>(rotations.size()));
            writer_.Write(wheelArm_, rotations);
        }
        writer_.Write(wheelArm_, Instr::Wait, false);
    }
    currentWheelRotation_ = destination;
}

void VanBerloController::Reset(bool asEarlyAsPossible) {
    if (!asEarlyAsPossible) {
        writer_.Write(wheelArm_, Instr::Reset);
        return;
    }
    Program* fragment = writer_.GetLastFragmentForArm(wheelArm_);
    if (!fragment) return;
    auto& instructions = fragment->GetArmInstructions(wheelArm_);
    int lastIndex = -1;
    for (int i = static_cast<int>(instructions.size()) - 1; i >= 0; --i) {
        if (instructions[static_cast<std::size_t>(i)] != Instr::None) {
            lastIndex = i;
            break;
        }
    }
    instructions.insert(instructions.begin() + lastIndex + 1, Instr::Reset);
}

VanBerloAtomGenerator::VanBerloAtomGenerator(GameObject* parent, ProgramWriter& writer)
    : AtomGenerator(parent, writer) {
    Make<Glyph>(this, Vec2{0, 1}, R240, GlyphType::Duplication);
    wheelArm_ = Make<Arm>(this, Vec2{1, 1}, R0, ArmType::VanBerlo);
    outputArm_ = Make<Arm>(this, Vec2{3, 0}, R180, ArmType::Arm1, 3);
    controller_ = std::make_unique<VanBerloController>(writer, wheelArm_);
}

void VanBerloAtomGenerator::Generate(Element element, int) {
    controller_->RotateToElement(element);
    writer.WriteGrabResetAction(outputArm_, Instr::RotateCCW);
}

void VanBerloAtomGenerator::PassThrough(Element element) {
    if (element == Element::Salt) controller_->RotateToElement(element);
    writer.WriteGrabResetAction(outputArm_, Instr::RotateCCW);
}

void VanBerloAtomGenerator::EndSolution() { controller_->Reset(); }

// ── Atom buffer ─────────────────────────────────────────────────────────────
BufferAtomGenerator::BufferAtomGenerator(GameObject* parent, ProgramWriter& writer,
                                         MultiStackElementBuffer::BufferInfo info)
    : AtomGenerator(parent, writer) {
    int index = 0;
    for (const auto& stackInfo : info.stacks) AddStack(index++, stackInfo);
    outputArm_ =
        Make<Arm>(this, Vec2{static_cast<int>(stacks_.size()) * 2 + 3, 0}, R180, ArmType::Arm1, 3);
}

void BufferAtomGenerator::AddStack(int index,
                                   const MultiStackElementBuffer::StackInfo& info) {
    Stack stack;
    stack.info = info;
    stack.index = index;
    const Vec2 pos{index * 2, 0};
    stack.pushArm = Make<Arm>(this, Add(pos, -1, 2), R300, ArmType::Arm1);
    stack.popArm = Make<Arm>(this, Add(pos, -1, 4), R300, ArmType::Arm1);
    stack.outputArm = Make<Arm>(this, Add(pos, 2, -2), R120, ArmType::Arm1, 2);
    Make<Track>(this, Add(pos, -1, 0), R60, 4);
    if (info.usesRestore && (info.multiAtom || info.wastesAtoms))
        Make<Glyph>(this, Add(pos, 0, -1), R60, GlyphType::Unbonding);
    if (info.multiAtom || info.wastesAtoms)
        Make<Glyph>(this, Add(pos, 0, 1), R60, GlyphType::Bonding);
    stacks_.push_back(stack);
}

void BufferAtomGenerator::Consume(Element, int id) {
    Stack& stack = stacks_.at(static_cast<std::size_t>(id));
    ++stack.atomCount;
    MoveAtomThroughStacks(0, stack.index);
    writer.AdjustTime(-1);
    writer.Write(stack.pushArm, std::vector<Instr>{Instr::MoveNeg, Instr::Grab, Instr::MovePos,
                                                   Instr::MovePos, Instr::Reset});
}

void BufferAtomGenerator::Generate(Element, int id) {
    writer.NewFragment();
    Stack& stack = stacks_.at(static_cast<std::size_t>(id));
    if (stack.atomCount > 1 || stack.info.wastesAtoms) {
        writer.Write(stack.pushArm, Instr::MovePos);
        writer.Write(std::vector<Arm*>{stack.pushArm, stack.popArm},
                     std::vector<Instr>{Instr::Grab, Instr::MoveNeg, Instr::MoveNeg,
                                        Instr::MoveNeg, Instr::MovePos});
        writer.Write(stack.pushArm, Instr::Reset, false);
        writer.Write(stack.popArm,
                     std::vector<Instr>{Instr::MovePos, Instr::Drop, Instr::MovePos}, false);
        writer.AdjustTime(-1);
        writer.Write(stack.outputArm, Instr::Wait);
    } else {
        writer.Write(stack.pushArm, std::vector<Instr>{Instr::MovePos, Instr::Grab, Instr::MoveNeg,
                                                       Instr::MoveNeg, Instr::Reset});
    }
    MoveAtomThroughStacks(stack.index, static_cast<int>(stacks_.size()));
    writer.WriteGrabResetAction(outputArm_, Instr::RotateCCW);
    --stack.atomCount;
}

void BufferAtomGenerator::PassThrough(Element) {
    MoveAtomThroughStacks(0, static_cast<int>(stacks_.size()));
    writer.WriteGrabResetAction(outputArm_, Instr::RotateCCW);
}

void BufferAtomGenerator::MoveAtomThroughStacks(int startIndex, int endIndex) {
    for (int i = startIndex; i < endIndex; ++i)
        writer.WriteGrabResetAction(stacks_[static_cast<std::size_t>(i)].outputArm,
                                    Instr::RotateCW);
}

} // namespace opusoptima::solvers::standard
