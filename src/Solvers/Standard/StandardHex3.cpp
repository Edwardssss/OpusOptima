#include "StandardModules.hpp"

// Ported from gtw123/OpusSolver@fdedd3f01a238b941ceba8bace7ea35cbd3b8c41 (MIT):
// Solver/Standard/Output/Hex3/*. The builders generate fragments in the same
// order and with the same arm motions as upstream; the assembler only decides
// where each product's output arm lives.

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace opusoptima::solvers::standard {
namespace {

[[nodiscard]] int DirIndex(Rot rot) { return rot.value; }

[[nodiscard]] Rot DirFromIndex(int index) { return Rot{((index % 6) + 6) % 6}; }

[[nodiscard]] Rot RotateCW(Rot rot) { return Rotate60CW(rot); }

[[nodiscard]] Rot RotateCCW(Rot rot) { return Rotate60CCW(rot); }

[[nodiscard]] Rot rotationMinus(Rot first, Rot second) { return first - second; }

[[nodiscard]] std::array<Rot, 6> AllRotations() {
    return {R0, R60, R120, R180, R240, R300};
}

using AtomMap = std::map<int, const Atom*>;

[[nodiscard]] AtomMap AdjacentAtoms(
    const Molecule& molecule, Vec2 position,
    const std::function<bool(Rot, const Atom*)>& include = {}) {
    AtomMap atoms;
    for (Rot direction : AllRotations()) {
        const Atom* atom = molecule.GetAdjacentAtom(position, direction);
        if (atom != nullptr && (!include || include(direction, atom)))
            atoms[DirIndex(direction)] = atom;
    }
    return atoms;
}

[[nodiscard]] std::vector<std::pair<Rot, const Atom*>> Enumerate(
    const AtomMap& atoms, std::optional<Rot> startFrom, int step) {
    std::vector<std::pair<Rot, const Atom*>> out;
    Rot rotation = startFrom.value_or(R0);
    for (int i = 0; i < 6; ++i) {
        const auto it = atoms.find(DirIndex(rotation));
        if (it != atoms.end()) out.emplace_back(rotation, it->second);
        rotation = DirFromIndex(DirIndex(rotation) + step);
    }
    return out;
}

/// Upstream `HexRotation.CalculateRotationsTo`: the intermediate rotations,
/// including the target, using the shorter direction (clockwise on ties when
/// `rotateClockwiseIf180Degrees` is set).
[[nodiscard]] std::vector<Rot> CalculateRotationsTo(Rot from, Rot target,
                                                    bool rotateClockwiseIf180Degrees) {
    const int delta = (target - from).value;
    std::vector<Rot> out;
    Rot rotation = from;
    if (delta > 3 || (delta == 3 && rotateClockwiseIf180Degrees)) {
        const int steps = 6 - delta;
        for (int i = 0; i < steps; ++i) {
            rotation = RotateCW(rotation);
            out.push_back(rotation);
        }
    } else {
        for (int i = 0; i < delta; ++i) {
            rotation = RotateCCW(rotation);
            out.push_back(rotation);
        }
    }
    return out;
}

[[nodiscard]] Vec2 SurroundingPosition(Vec2 atom, Rot direction) {
    return OffsetInDirection(atom, direction, 1);
}

} // namespace

// ── Missing-center builder ──────────────────────────────────────────────────
Hex3MissingCenterAtomBuilder::Hex3MissingCenterAtomBuilder(const Molecule& product)
    : Hex3MoleculeBuilder(product) {
    centerPosition_ = FindCenterPosition();
    DetermineBuildOrder();
}

Vec2 Hex3MissingCenterAtomBuilder::FindCenterPosition() const {
    const auto& atoms = Product().Atoms();
    if (atoms.empty()) throw std::invalid_argument("hex3 product has no atoms");
    std::set<std::pair<int, int>> common;
    for (Rot direction : AllRotations()) {
        const Vec2 position = SurroundingPosition(atoms.front().position, direction);
        common.insert({position.x, position.y});
    }
    for (std::size_t i = 1; i < atoms.size(); ++i) {
        std::set<std::pair<int, int>> adjacent;
        for (Rot direction : AllRotations()) {
            const Vec2 position = SurroundingPosition(atoms[i].position, direction);
            adjacent.insert({position.x, position.y});
        }
        for (auto it = common.begin(); it != common.end();) {
            if (!adjacent.contains(*it))
                it = common.erase(it);
            else
                ++it;
        }
    }
    if (common.size() != 1)
        throw std::invalid_argument("hex3 missing-center product has no unique center");
    return {common.begin()->first, common.begin()->second};
}

void Hex3MissingCenterAtomBuilder::DetermineBuildOrder() {
    const AtomMap atoms = AdjacentAtoms(Product(), centerPosition_);
    Rot startDirection = R0;
    for (Rot direction : AllRotations()) {
        const auto it = atoms.find(DirIndex(direction));
        if (it != atoms.end() &&
            it->second->bonds[DirIndex(rotationMinus(direction, R120))] == BondType::None) {
            startDirection = direction;
            break;
        }
    }
    const auto ordered = Enumerate(atoms, startDirection, 1);
    if (ordered.size() < 2)
        throw std::invalid_argument("hex3 missing-center product has too few outer atoms");

    elementBuildOrder_.clear();
    for (const auto& [direction, atom] : ordered) {
        (void)direction;
        elementBuildOrder_.push_back(atom->element);
    }
    std::swap(elementBuildOrder_[0], elementBuildOrder_[1]);

    outputRotation_ = R180 - ordered.back().first;
    needsAllBonds_ = true;
    for (const auto& [direction, atom] : atoms) {
        if (atom->bonds[DirIndex(rotationMinus(DirFromIndex(direction), R120))] ==
            BondType::None) {
            needsAllBonds_ = false;
            break;
        }
    }
    if (needsAllBonds_) outputRotation_ = RotateCW(outputRotation_);
}

std::vector<Program> Hex3MissingCenterAtomBuilder::GenerateFragments(
    Hex3AssemblyArea& assemblyArea) const {
    ProgramWriter writer;
    writer.Write(assemblyArea.assemblyArm, Instr::MovePos);
    writer.Write(assemblyArea.horizontalArm, std::vector<Instr>{Instr::Grab, Instr::MoveNeg});
    writer.NewFragment();
    writer.Write(assemblyArea.horizontalArm,
                 std::vector<Instr>{Instr::PivotCCW, Instr::PivotCCW, Instr::Reset});

    const int atomCount = static_cast<int>(Product().Atoms().size());
    for (int index = 2; index < atomCount; ++index) {
        writer.NewFragment();
        if (index < atomCount - 1) {
            writer.WriteGrabResetAction(assemblyArea.horizontalArm,
                                        std::vector<Instr>{Instr::MoveNeg, Instr::PivotCW});
        }
    }
    if (needsAllBonds_) {
        writer.WriteGrabResetAction(assemblyArea.horizontalArm,
                                    std::vector<Instr>{Instr::MoveNeg, Instr::PivotCW});
        writer.AdjustTime(2);
    }
    writer.WriteGrabResetAction(assemblyArea.horizontalArm, Instr::RotateCCW);
    writer.Write(assemblyArea.assemblyArm, Instr::Reset);
    writer.AdjustTime(-1);
    return writer.Fragments();
}

// ── Center-atom builder ────────────────────────────────────────────────────
Hex3CenterAtomBuilder::Hex3CenterAtomBuilder(const Molecule& product,
                                             const std::vector<const Atom*>& centralAtoms)
    : Hex3MoleculeBuilder(product), product_(&product) {
    if (centralAtoms.empty())
        throw std::invalid_argument("hex3 center builder requires a central atom");
    centerAtom_ = ChooseCenterAtom(centralAtoms);
    GenerateCenterOperations();
    GenerateClockwiseOperations();
    GenerateCounterclockwiseOperations();
    if (needAvoidBondBeforeCounterclockwiseOperations_ && !counterclockwiseOperations_.empty()) {
        std::vector<Operation> all = centerOperations_;
        all.insert(all.end(), clockwiseOperations_.begin(), clockwiseOperations_.end());
        if (all.empty())
            throw std::invalid_argument("hex3 center builder has no final rotation");
        clockwiseOperations_.push_back(
            {OperationType::MoveAwayFromBonder, all.back().finalRotation, nullptr});
    }
}

const Atom* Hex3CenterAtomBuilder::ChooseCenterAtom(
    const std::vector<const Atom*>& centralAtoms) const {
    const Atom* best = nullptr;
    int bestCcw = 1000;
    int bestBonds = -1;
    for (const Atom* atom : centralAtoms) {
        int ccw = 0;
        const AtomMap adjacent = AdjacentAtoms(
            *product_, atom->position, [&](Rot direction, const Atom* other) {
                return other->bonds[DirIndex(direction + R180)] == BondType::None &&
                       other->bonds[DirIndex(rotationMinus(direction, R120))] == BondType::None &&
                       other->bonds[DirIndex(direction + R120)] != BondType::None;
            });
        ccw = static_cast<int>(adjacent.size());
        int bonds = 0;
        for (BondType bond : atom->bonds)
            bonds += bond != BondType::None;
        if (ccw < bestCcw || (ccw == bestCcw && bonds > bestBonds)) {
            best = atom;
            bestCcw = ccw;
            bestBonds = bonds;
        }
    }
    return best;
}

std::vector<Element> Hex3CenterAtomBuilder::GetElementsInBuildOrder() const {
    std::vector<Element> order{centerAtom_->element};
    for (const Operation& operation : centerOperations_)
        if (operation.type == OperationType::GrabAtom) order.push_back(operation.atom->element);
    for (const Operation& operation : clockwiseOperations_)
        if (operation.type == OperationType::GrabAtom) order.push_back(operation.atom->element);
    for (const Operation& operation : counterclockwiseOperations_)
        if (operation.type == OperationType::GrabAtom) order.push_back(operation.atom->element);
    return order;
}

Hex3OutputLocation Hex3CenterAtomBuilder::OutputLocation() const {
    if (!counterclockwiseOperations_.empty()) return Hex3OutputLocation::Left;
    if (!clockwiseOperations_.empty()) return Hex3OutputLocation::Right;
    return Hex3OutputLocation::RightSimple;
}

Rot Hex3CenterAtomBuilder::OutputRotation() const {
    if (centerOperations_.empty() && clockwiseOperations_.empty() &&
        counterclockwiseOperations_.empty())
        return R0;
    Rot rotation = centerOperations_.empty() ? R0 : centerOperations_.back().finalRotation;
    if (!clockwiseOperations_.empty()) rotation = clockwiseOperations_.back().finalRotation;
    if (!counterclockwiseOperations_.empty())
        rotation = counterclockwiseOperations_.back().finalRotation;
    if (!counterclockwiseOperations_.empty()) return R120 - rotation;
    if (!clockwiseOperations_.empty()) return R60 - rotation;
    return R120 - rotation;
}

void Hex3CenterAtomBuilder::GenerateCenterOperations() {
    const AtomMap atoms = AdjacentAtoms(
        *product_, centerAtom_->position,
        [&](Rot direction, const Atom* atom) {
            return atom->bonds[DirIndex(direction + R180)] != BondType::None;
        });
    Rot lastAtomDirection = R0;
    if (atoms.size() != 6) {
        for (Rot direction : AllRotations()) {
            const int dir = DirIndex(direction);
            if (atoms.contains(dir) &&
                !atoms.contains(DirIndex(rotationMinus(direction, R60)))) {
                lastAtomDirection = direction;
                break;
            }
        }
    } else {
        for (Rot direction : AllRotations()) {
            const auto next = atoms.find(DirIndex(rotationMinus(direction, R60)));
            if (next != atoms.end() &&
                next->second->bonds[DirIndex(direction + R180)] != BondType::None) {
                lastAtomDirection = direction;
                break;
            }
        }
    }
    const Rot startDirection = RotateCW(lastAtomDirection);
    const auto ordered = Enumerate(atoms, startDirection, -1);
    currentDirection_ = ordered.empty() ? R0 : ordered.front().first;
    for (const auto& [direction, atom] : ordered) {
        const auto rotations = CalculateRotationsTo(currentDirection_, direction, true);
        AddRotationOperations(centerOperations_, rotations, false, false);
        grabbedAtoms_[DirIndex(direction)] = atom;
        centerOperations_.push_back(
            {OperationType::GrabAtom, currentDirection_, atom});
    }
}

void Hex3CenterAtomBuilder::GenerateClockwiseOperations() {
    const AtomMap atoms = AdjacentAtoms(
        *product_, centerAtom_->position,
        [&](Rot direction, const Atom* atom) {
            return atom->bonds[DirIndex(rotationMinus(direction, R120))] != BondType::None;
        });
    currentDirection_ = RotateCW(currentDirection_);
    const Rot startDirection =
        grabbedAtoms_.empty() ? RotateCCW(R0) : RotateCCW(DirFromIndex(grabbedAtoms_.begin()->first));
    const auto ordered = Enumerate(atoms, startDirection, 1);
    for (const auto& [direction, atom] : ordered) {
        const int dir = DirIndex(direction);
        if (!grabbedAtoms_.contains(DirIndex(rotationMinus(direction, R60)))) continue;
        const auto rotations = CalculateRotationsTo(currentDirection_, direction, true);
        bool moveWhileRotating = false;
        for (Rot newDirection : rotations) {
            const int newDir = DirIndex(newDirection);
            const auto next = grabbedAtoms_.find(DirIndex(rotationMinus(newDirection, R60)));
            if (grabbedAtoms_.contains(newDir) && next != grabbedAtoms_.end() &&
                !atoms.contains(newDir)) {
                moveWhileRotating = true;
                break;
            }
        }
        AddRotationOperations(clockwiseOperations_, rotations, moveWhileRotating,
                              moveWhileRotating);
        constructedClockwiseBonds_.insert(dir);
        if (!grabbedAtoms_.contains(dir)) {
            grabbedAtoms_[dir] = atom;
            clockwiseOperations_.push_back(
                {OperationType::GrabAtom, currentDirection_, atom});
        } else {
            clockwiseOperations_.push_back({OperationType::Bond, currentDirection_, atom});
        }
    }
}

void Hex3CenterAtomBuilder::GenerateCounterclockwiseOperations() {
    const AtomMap atoms = AdjacentAtoms(
        *product_, centerAtom_->position,
        [&](Rot direction, const Atom* atom) {
            return atom->bonds[DirIndex(direction + R120)] != BondType::None;
        });
    currentDirection_ = currentDirection_ - R120;
    if (grabbedAtoms_.contains(DirIndex(currentDirection_)) &&
        grabbedAtoms_.contains(DirIndex(RotateCCW(currentDirection_))) &&
        !atoms.contains(DirIndex(currentDirection_))) {
        needAvoidBondBeforeCounterclockwiseOperations_ = true;
    }

    const Rot startDirection =
        grabbedAtoms_.empty() ? RotateCW(R0) : RotateCW(DirFromIndex(grabbedAtoms_.begin()->first));
    const auto ordered = Enumerate(atoms, startDirection, -1);
    bool firstOperation = true;
    for (const auto& [direction, atom] : ordered) {
        const int dir = DirIndex(direction);
        if (constructedClockwiseBonds_.contains(DirIndex(RotateCCW(direction)))) continue;
        const auto rotations = CalculateRotationsTo(currentDirection_, direction, false);
        bool moveWhileRotating = false;
        bool moveOnlyAfterRotating = false;
        if (firstOperation && needAvoidBondBeforeCounterclockwiseOperations_) {
            moveOnlyAfterRotating = true;
        } else {
            for (Rot newDirection : rotations) {
                const int newDir = DirIndex(newDirection);
                if (grabbedAtoms_.contains(newDir) &&
                    grabbedAtoms_.contains(DirIndex(RotateCCW(newDirection))) &&
                    !atoms.contains(newDir)) {
                    moveWhileRotating = true;
                    break;
                }
            }
        }
        AddRotationOperations(counterclockwiseOperations_, rotations, moveWhileRotating,
                              moveWhileRotating || moveOnlyAfterRotating);
        constructedClockwiseBonds_.insert(DirIndex(RotateCCW(direction)));
        if (!grabbedAtoms_.contains(dir)) {
            grabbedAtoms_[dir] = atom;
            counterclockwiseOperations_.push_back(
                {OperationType::GrabAtom, currentDirection_, atom});
        } else {
            counterclockwiseOperations_.push_back(
                {OperationType::Bond, currentDirection_, atom});
        }
        firstOperation = false;
    }
}

void Hex3CenterAtomBuilder::AddRotationOperations(std::vector<Operation>& operations,
                                                  const std::vector<Rot>& rotations,
                                                  bool moveBeforeRotating,
                                                  bool moveAfterRotating) {
    if (moveBeforeRotating)
        operations.push_back(
            {OperationType::MoveAwayFromBonder, currentDirection_, nullptr});
    if (!rotations.empty()) {
        const auto type = (rotations.front() - currentDirection_).value == 1
                              ? OperationType::RotateClockwise
                              : OperationType::RotateCounterclockwise;
        for (Rot rotation : rotations) {
            currentDirection_ = rotation;
            operations.push_back({type, currentDirection_, nullptr});
        }
    }
    if (moveAfterRotating)
        operations.push_back(
            {OperationType::MoveTowardBonder, currentDirection_, nullptr});
}

std::vector<Program> Hex3CenterAtomBuilder::GenerateFragments(
    Hex3AssemblyArea& assemblyArea) const {
    return BuildFragments(assemblyArea);
}

std::vector<Program> Hex3CenterAtomBuilder::BuildFragments(
    Hex3AssemblyArea& assemblyArea) const {
    ProgramWriter writer;
    writer.WriteGrabResetAction(assemblyArea.horizontalArm, Instr::MoveNeg);
    writer.Write(assemblyArea.assemblyArm, Instr::Grab);

    auto writeOperations = [&](const std::vector<Operation>& operations, bool afterGrab) {
        for (const Operation& operation : operations) {
            switch (operation.type) {
            case OperationType::RotateClockwise:
                writer.Write(assemblyArea.assemblyArm, Instr::PivotCW);
                break;
            case OperationType::RotateCounterclockwise:
                writer.Write(assemblyArea.assemblyArm, Instr::PivotCCW);
                break;
            case OperationType::GrabAtom:
                writer.NewFragment();
                if (afterGrab)
                    writer.WriteGrabResetAction(assemblyArea.horizontalArm, Instr::MovePos);
                break;
            case OperationType::Bond: break;
            case OperationType::MoveAwayFromBonder:
                writer.Write(assemblyArea.assemblyArm, Instr::MovePos);
                break;
            case OperationType::MoveTowardBonder:
                writer.Write(assemblyArea.assemblyArm, Instr::MoveNeg);
                break;
            }
        }
    };

    writeOperations(centerOperations_, false);
    if (clockwiseOperations_.empty() && counterclockwiseOperations_.empty()) {
        if (!centerOperations_.empty()) {
            writer.Write(assemblyArea.horizontalArm,
                         std::vector<Instr>{Instr::MoveNeg, Instr::MovePos}, false);
        }
        return writer.Fragments();
    }
    writer.Write(assemblyArea.assemblyArm, Instr::MovePos);
    writeOperations(clockwiseOperations_, false);
    if (counterclockwiseOperations_.empty()) return writer.Fragments();
    writer.Write(assemblyArea.assemblyArm, Instr::RotateCCW);
    writeOperations(counterclockwiseOperations_, true);
    return writer.Fragments();
}

// ── Assembly area ──────────────────────────────────────────────────────────
Hex3AssemblyArea::Hex3AssemblyArea(SolverComponent* parent, ProgramWriter& writer)
    : SolverComponent(parent, writer, parent->OutputPosition(), R0) {
    Make<Glyph>(this, Vec2{0, 0}, R0, GlyphType::Bonding);
    leftBonder_ = Make<Glyph>(this, Vec2{-2, 0}, R0, GlyphType::Bonding);
    horizontalArm = Make<Arm>(this, Vec2{3, 0}, R180, ArmType::Arm1, 3);
    Make<Track>(this, Vec2{4, 0}, R180, 2);
    assemblyArm = Make<Arm>(this, Vec2{1, -2}, R60, ArmType::Arm1, 2);
    Make<Track>(this, Vec2{1, -2},
                std::vector<Track::Segment>{{R240, 1}, {R300, 1}});
}

void Hex3AssemblyArea::OptimizeParts() {
    if (!isLeftBonderUsed) leftBonder_->Remove();
}

// ── Assembler ──────────────────────────────────────────────────────────────
Hex3AssemblerModule::Hex3AssemblerModule(GameObject* parent, ProgramWriter& writer,
                                         Vec2 position,
                                         const std::vector<const Molecule*>& products)
    : MoleculeAssembler(parent, writer, position) {
    assemblyArea_ = Make<Hex3AssemblyArea>(this, writer);
    for (const Molecule* product : products) {
        std::vector<const Atom*> centralAtoms;
        for (const Atom& atom : product->Atoms()) {
            bool central = true;
            for (const Atom& other : product->Atoms()) {
                if (&atom != &other && !Molecule::AreAtomsAdjacent(atom, other)) {
                    central = false;
                    break;
                }
            }
            if (central) centralAtoms.push_back(&atom);
        }
        if (centralAtoms.empty())
            builders_[product->Id()] = std::make_unique<Hex3MissingCenterAtomBuilder>(*product);
        else
            builders_[product->Id()] =
                std::make_unique<Hex3CenterAtomBuilder>(*product, centralAtoms);
    }

    std::vector<const Hex3MoleculeBuilder*> leftBuilders;
    std::vector<const Hex3MoleculeBuilder*> rightBuilders;
    for (const auto& [id, builder] : builders_) {
        (void)id;
        if (builder->OutputLocation() == Hex3OutputLocation::Left)
            leftBuilders.push_back(builder.get());
        else
            rightBuilders.push_back(builder.get());
    }
    leftOutputArms_ = AddOutputs(leftBuilders, Vec2{-1, -3}, Vec2{0, -3}, R300);
    assemblyArea_->isLeftBonderUsed = !leftBuilders.empty();
    rightOutputArms_ = AddOutputs(rightBuilders, Vec2{3, -3}, Vec2{3, -3}, R60);
}

std::vector<Arm*> Hex3AssemblerModule::AddOutputs(
    const std::vector<const Hex3MoleculeBuilder*>& builders, Vec2 initialPosition, Vec2 offset,
    Rot rotationOffset) {
    std::vector<Arm*> arms;
    Rot currentRotationOffset = R0;
    bool allSimpleOutputs = true;
    for (const Hex3MoleculeBuilder* builder : builders)
        allSimpleOutputs &= builder->OutputLocation() == Hex3OutputLocation::RightSimple;

    int index = 0;
    for (auto it = builders.rbegin(); it != builders.rend(); ++it, ++index) {
        const Hex3MoleculeBuilder& builder = **it;
        const Transform transform{Vec2{0, 0} - builder.CenterAtomPosition(), R0};
        const Rot rotation =
            builder.OutputRotation() +
            (builder.RequiresRotationsBetweenOutputPositions() ? currentRotationOffset : R0);
        Vec2 productCenter = initialPosition + offset * index;
        const bool useSimpleOutput =
            builder.OutputLocation() == Hex3OutputLocation::RightSimple &&
            (index == 0 || allSimpleOutputs);
        if (useSimpleOutput) productCenter = productCenter + Vec2{0, 1};
        outputs_[builder.Product().Id()] = {index, useSimpleOutput};

        const Transform placed =
            Transform{productCenter + builder.OutputPositionOffset(), rotation}.Apply(transform);
        Make<Product>(this, placed.position, placed.rotation, &builder.Product());

        if (index > 0) {
            const Rot armRotation = rotationOffset == R60 ? R180 : R0;
            const Vec2 armPosition =
                productCenter - offset - RotateBy(Vec2{3, 0}, armRotation);
            arms.push_back(Make<Arm>(this, armPosition, armRotation, ArmType::Arm1, 3));
        }
        currentRotationOffset = currentRotationOffset + rotationOffset;
    }
    return arms;
}

void Hex3AssemblerModule::AddAtom(Element element, int productID) {
    (void)element;
    const auto builderIt = builders_.find(productID);
    if (builderIt == builders_.end())
        throw std::invalid_argument("unknown hex3 product id");
    State& state = states_[productID];
    if (!state.started) {
        state.fragments = builderIt->second->GenerateFragments(*assemblyArea_);
        if (state.fragments.empty())
            throw std::invalid_argument("hex3 builder produced no fragments");
        state.next = 0;
        state.started = true;
    }
    if (state.next >= state.fragments.size()) {
        state.next = 0;
    }
    writer.AppendFragment(state.fragments[state.next]);
    const bool last = state.next + 1 >= state.fragments.size();
    ++state.next;
    if (last) {
        const Molecule& product = builderIt->second->Product();
        switch (builderIt->second->OutputLocation()) {
        case Hex3OutputLocation::Left: MoveProductToLefthandOutput(product); break;
        case Hex3OutputLocation::Right: MoveProductToRighthandOutput(product, false); break;
        case Hex3OutputLocation::RightSimple: {
            const bool simple = outputs_.at(productID).useSimpleOutput;
            MoveProductToRighthandOutput(product, !simple);
            break;
        }
        case Hex3OutputLocation::RightNoCenter:
            MoveProductToOutput(product, rightOutputArms_,
                                {Instr::RotateCCW, Instr::PivotCW});
            break;
        }
        state.started = false;
    }
}

void Hex3AssemblerModule::MoveProductToRighthandOutput(const Molecule& product,
                                                       bool moveAfterRotate) {
    writer.Write(assemblyArea_->assemblyArm,
                 moveAfterRotate
                     ? std::vector<Instr>{Instr::RotateCW, Instr::MovePos, Instr::Reset}
                     : std::vector<Instr>{Instr::RotateCW, Instr::Reset});
    writer.AdjustTime(-1);
    MoveProductToOutput(product, rightOutputArms_, {Instr::RotateCCW});
}

void Hex3AssemblerModule::MoveProductToLefthandOutput(const Molecule& product) {
    writer.Write(assemblyArea_->assemblyArm,
                 std::vector<Instr>{Instr::RotateCCW, Instr::Reset});
    writer.AdjustTime(-1);
    MoveProductToOutput(product, leftOutputArms_, {Instr::RotateCW});
}

void Hex3AssemblerModule::MoveProductToOutput(const Molecule& product,
                                              const std::vector<Arm*>& arms,
                                              const std::vector<Instr>& instructions) {
    const auto it = outputs_.find(product.Id());
    if (it == outputs_.end()) throw std::invalid_argument("unknown hex3 output product");
    if (it->second.index > static_cast<int>(arms.size()))
        throw std::invalid_argument("hex3 output arm index out of range");
    for (int i = 0; i < it->second.index; ++i)
        writer.WriteGrabResetAction(arms[static_cast<std::size_t>(i)], instructions);
}

void Hex3AssemblerModule::OptimizeParts() { assemblyArea_->OptimizeParts(); }

// ── Factory predicates ─────────────────────────────────────────────────────
bool IsHex3ProductCompatible(const Molecule& product) {
    if (product.Size() > 3) return false;
    if (product.Width() == 3 && product.Height() == 3 && product.DiagonalLength() == 3 &&
        product.GetAtom({0, 0}) != nullptr)
        return false;
    return true;
}

bool UseHex3Assembler(const std::vector<const Molecule*>& products) {
    const char* flag = std::getenv("OPUSOPTIMA_STANDARD_HEX3");
    if (flag != nullptr && std::string_view(flag) == "off") return false;
    if (products.empty()) return false;
    const bool diag = std::getenv("OPUSOPTIMA_STANDARD_DIAG") != nullptr;
    bool allSingle = true;
    bool allLinear = true;
    bool anyTriplex = false;
    bool allHex3 = true;
    for (const Molecule* product : products) {
        allSingle &= product->Atoms().size() == 1;
        allLinear &= product->Height() == 1;
        anyTriplex |= product->HasTriplex();
        allHex3 &= IsHex3ProductCompatible(*product);
        if (diag)
            std::fprintf(stderr,
                         "HEX3DIAG id=%d atoms=%zu size=%d w=%d h=%d d=%d compatible=%d\n",
                         product->Id(), product->Atoms().size(), product->Size(),
                         product->Width(), product->Height(), product->DiagonalLength(),
                         IsHex3ProductCompatible(*product) ? 1 : 0);
    }
    if (anyTriplex || allSingle || allLinear) return false;
    return allHex3;
}

std::vector<Element> ProductBuildOrder(const Molecule& product) {
    if (IsHex3ProductCompatible(product)) {
        std::vector<const Atom*> centralAtoms;
        for (const Atom& atom : product.Atoms()) {
            bool central = true;
            for (const Atom& other : product.Atoms()) {
                if (&atom != &other && !Molecule::AreAtomsAdjacent(atom, other)) {
                    central = false;
                    break;
                }
            }
            if (central) centralAtoms.push_back(&atom);
        }
        if (!centralAtoms.empty()) {
            Hex3CenterAtomBuilder builder(product, centralAtoms);
            return builder.GetElementsInBuildOrder();
        }
        Hex3MissingCenterAtomBuilder builder(product);
        return builder.GetElementsInBuildOrder();
    }
    std::vector<Element> order;
    for (const Atom& atom : product.GetAtomsInInputOrder()) order.push_back(atom.element);
    return order;
}

} // namespace opusoptima::solvers::standard
