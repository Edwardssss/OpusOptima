#include "StandardModules.hpp"

// Ported from gtw123/OpusSolver@fdedd3f01a238b941ceba8bace7ea35cbd3b8c41 (MIT).
// The module map is documented in README.md.


#include <algorithm>
#include <stdexcept>

namespace opusoptima::solvers::standard {

// ── Assembly area ───────────────────────────────────────────────────────────
AssemblyArea::AssemblyArea(GameObject* parent, ProgramWriter& writer, int width,
                           const std::vector<const Molecule*>& products)
    : SolverComponent(parent, writer, Vec2{0, 0}, R0), width(width) {
    for (const Molecule* product : products) {
        hasTriplexBonders |= product->HasTriplex();
        for (const Atom& atom : product->Atoms()) {
            hasSingle60Bonder |= atom.bonds[static_cast<std::size_t>(R60.value)] == BondType::Single;
            hasSingle120Bonder |=
                atom.bonds[static_cast<std::size_t>(R120.value)] == BondType::Single;
        }
    }
    CreateArms();
    CreateBonders();
    CreateTracks();
}

void AssemblyArea::CreateArms() {
    for (int x = 0; x < width; ++x) {
        lowerArms.push_back(
            Make<Arm>(this, Vec2{-width + x + 1, -2}, R60, ArmType::Piston, 2));
        upperArms.push_back(Make<Arm>(this, Vec2{x + 2, -1}, R60, ArmType::Piston, 2));
    }
}

Glyph* AssemblyArea::AddBonder(Vec2& position, int xOffset, int yOffset, Rot direction,
                               GlyphType type) {
    position.x += xOffset;
    position.y += yOffset;
    Glyph* glyph = Make<Glyph>(this, position, direction, type);
    bonders.push_back(glyph);
    return glyph;
}

void AssemblyArea::CreateBonders() {
    Vec2 position{0, 0};
    AddBonder(position, 0, 0, R0, GlyphType::Bonding);
    position = position + Vec2{2, 0};
    if (hasSingle60Bonder) {
        AddBonder(position, width, 0, R60, GlyphType::Bonding);
    } else {
        position = position + Vec2{1, 0};
    }
    if (hasSingle120Bonder) {
        AddBonder(position, width, 0, R120, GlyphType::Bonding);
    } else {
        position = position + Vec2{1, 0};
    }
    if (hasTriplexBonders) {
        AddBonder(position, width - 1, 0, R0, GlyphType::TriplexBonding);
        AddBonder(position, 3, 0, R120, GlyphType::TriplexBonding);
        AddBonder(position, 1, 1, R240, GlyphType::TriplexBonding);
        AddBonder(position, width - 1, -1, R0, GlyphType::Unbonding);
        AddBonder(position, width, 0, R60, GlyphType::Unbonding);
        AddBonder(position, width, 0, R120, GlyphType::Unbonding);
    }
}

void AssemblyArea::CreateTracks() {
    int lowerTrackLength = width * 4 - 1;
    if (hasTriplexBonders) lowerTrackLength += width * 4 + 2;
    Make<Track>(this, Vec2{-width + 1, -2}, R0, lowerTrackLength);
    Make<Track>(this, Vec2{2, -1}, R0, lowerTrackLength - width - 1);
}

void AssemblyArea::SetUsedBonders(GlyphType type, std::optional<Rot> direction) {
    for (Glyph* bonder : bonders) {
        if (bonder->type != type) continue;
        if (direction && bonder->LocalTransform().rotation != *direction) continue;
        usedBonders_.insert(bonder);
    }
}

void AssemblyArea::OptimizeParts() {
    for (Glyph* bonder : bonders)
        if (!usedBonders_.contains(bonder)) bonder->Remove();
}

// ── Bond programmer ─────────────────────────────────────────────────────────
void BondProgrammer::Generate() {
    if (generated_) throw std::invalid_argument("bond programmer generated twice");
    generated_ = true;
    AddBonds();
    Optimize();
    int distance = 0;
    for (Instr instruction : instructions_)
        if (instruction == Instr::MovePos) ++distance;
    returnInstructions_.assign(static_cast<std::size_t>(distance), Instr::MoveNeg);
}

bool BondProgrammer::IsUnbondedFirePair(const Atom& atom, Rot direction) const {
    const Atom* other = molecule_.GetAdjacentAtom(atom.position, direction);
    return atom.element == Element::Fire &&
           atom.bonds[static_cast<std::size_t>(direction.value)] == BondType::None && other &&
           other->element == Element::Fire;
}

std::optional<int> BondProgrammer::LeftmostTriplexBondPosition() const {
    std::optional<int> position;
    for (const Atom* atom : molecule_.GetRow(row_)) {
        auto consider = [&](int value) {
            if (!position || value < *position) position = value;
        };
        if (atom->bonds[static_cast<std::size_t>(R0.value)] == BondType::Triplex)
            consider(atom->position.x);
        if (atom->bonds[static_cast<std::size_t>(R60.value)] == BondType::Triplex)
            consider(atom->position.x);
        if (atom->bonds[static_cast<std::size_t>(R120.value)] == BondType::Triplex)
            consider(atom->position.x - 1);
    }
    return position;
}

void BondProgrammer::MoveThroughBonder(GlyphType type, Rot direction, int count,
                                       const std::function<bool(const Atom&)>& shouldBondAtom) {
    for (int i = 0; i < count; ++i) {
        Add(Instr::MovePos);
        const Atom* atom = molecule_.GetAtom({assemblyArea_->width - 1 - i, row_});
        if (atom && shouldBondAtom(*atom)) {
            Add(Instr::Retract);
            Add(Instr::Extend);
            assemblyArea_->SetUsedBonders(type, direction);
        }
    }
}

void BondProgrammer::AddBonds() {
    if (assemblyArea_->hasSingle60Bonder) {
        MoveThroughBonder(GlyphType::Bonding, R60, assemblyArea_->width, [](const Atom& atom) {
            return atom.bonds[static_cast<std::size_t>(R60.value)] == BondType::Single;
        });
    } else {
        Add(Instr::MovePos);
    }
    if (assemblyArea_->hasSingle120Bonder) {
        MoveThroughBonder(GlyphType::Bonding, R120, assemblyArea_->width - 1,
                          [](const Atom& atom) {
                              return atom.bonds[static_cast<std::size_t>(R120.value)] ==
                                     BondType::Single;
                          });
    }

    const auto firstTriplex = LeftmostTriplexBondPosition();
    if (firstTriplex) {
        Add(Instr::MovePos);
        Add(Instr::Retract);
        const int maxMoves = assemblyArea_->width + 3;
        Repeat(Instr::MovePos, maxMoves - *firstTriplex);
        Add(Instr::Extend);
        assemblyArea_->SetUsedBonders(GlyphType::TriplexBonding, std::nullopt);
        Repeat(Instr::MovePos, *firstTriplex);
        MoveThroughBonder(GlyphType::Unbonding, R0, assemblyArea_->width - 1,
                          [&](const Atom& atom) { return IsUnbondedFirePair(atom, R180); });
        MoveThroughBonder(GlyphType::Unbonding, R60, assemblyArea_->width,
                          [&](const Atom& atom) { return IsUnbondedFirePair(atom, R60); });
        MoveThroughBonder(GlyphType::Unbonding, R120, assemblyArea_->width - 1,
                          [&](const Atom& atom) { return IsUnbondedFirePair(atom, R120); });
    }
}

void BondProgrammer::Optimize() {
    int lastIndex = -1;
    for (int i = static_cast<int>(instructions_.size()) - 1; i >= 0; --i) {
        if (instructions_[static_cast<std::size_t>(i)] != Instr::MovePos) {
            lastIndex = i;
            break;
        }
    }
    ++lastIndex;
    if (static_cast<std::size_t>(lastIndex) < instructions_.size())
        instructions_.erase(instructions_.begin() + lastIndex, instructions_.end());

    for (std::size_t i = 0; i + 2 < instructions_.size(); ++i) {
        if (instructions_[i] == Instr::Extend && instructions_[i + 1] == Instr::MovePos &&
            instructions_[i + 2] == Instr::Retract) {
            instructions_[i] = Instr::MovePos;
            instructions_.erase(instructions_.begin() + static_cast<std::ptrdiff_t>(i) + 1,
                                instructions_.begin() + static_cast<std::ptrdiff_t>(i) + 3);
        }
    }
}

// ── Product conveyor ────────────────────────────────────────────────────────
ProductConveyor::ProductConveyor(GameObject* parent, ProgramWriter& writer, Vec2 position,
                                 const std::vector<const Molecule*>& products)
    : SolverComponent(parent, writer, position, R0), products_(products) {
    CreateOutputs();
    const Vec2 armPosition{-1, 1};
    outputArm_ = Make<Arm>(this, armPosition, R300, ArmType::Arm1);
    int maxDrop = 0;
    for (const auto& [id, output] : outputs_) maxDrop = std::max(maxDrop, output.dropPosition);
    Make<Track>(this, armPosition, R60, maxDrop);
}

void ProductConveyor::CreateOutputs() {
    int totalHeight = 0;
    for (auto it = products_.rbegin(); it != products_.rend(); ++it) {
        const Molecule* product = *it;
        Make<Product>(this, Vec2{0, totalHeight}, R0, product);
        const auto column = product->GetColumn(0);
        if (column.empty()) throw std::invalid_argument("product first column is empty");
        const int grabY = column.front()->position.y;
        outputs_[product->Id()] = Output{grabY, grabY + totalHeight};
        totalHeight += product->Height();
    }
}

void ProductConveyor::MoveProductToOutputLocation(const Molecule& product) {
    const auto it = outputs_.find(product.Id());
    if (it == outputs_.end()) throw std::invalid_argument("unknown product conveyor output");
    const Output& output = it->second;
    if (output.dropPosition == output.grabPosition) return;
    writer.AdjustTime(-output.grabPosition);
    writer.Write(outputArm_, std::vector<Instr>(static_cast<std::size_t>(output.grabPosition),
                                                Instr::MovePos));
    writer.Write(outputArm_, Instr::Grab);
    writer.Write(outputArm_,
                 std::vector<Instr>(static_cast<std::size_t>(output.dropPosition -
                                                            output.grabPosition),
                                    Instr::MovePos));
    writer.Write(outputArm_, Instr::Reset);
}

// ── Specialized assemblers (upstream MoleculeAssemblerFactory) ─────────────
SingleMonoAssemblerModule::SingleMonoAssemblerModule(
    GameObject* parent, ProgramWriter& writer, Vec2 position,
    const std::vector<const Molecule*>& products)
    : MoleculeAssembler(parent, writer, position) {
    if (products.size() != 1 || products.front()->Atoms().size() != 1)
        throw std::invalid_argument("single mono assembler expects one monoatomic product");
    Make<Product>(this, Vec2{0, 0}, R0, products.front());
}

void SingleMonoAssemblerModule::AddAtom(Element, int) {}

MonoAssemblerModule::MonoAssemblerModule(GameObject* parent, ProgramWriter& writer,
                                         Vec2 position,
                                         const std::vector<const Molecule*>& products)
    : MoleculeAssembler(parent, writer, position) {
    for (const Molecule* product : products)
        if (product->Atoms().size() > 1)
            throw std::invalid_argument("mono assembler expects monoatomic products");
    if (products.size() < 2)
        throw std::invalid_argument("mono assembler expects multiple products");
    if (products.size() > static_cast<std::size_t>(kMaxProducts))
        throw std::invalid_argument("mono assembler supports at most four products");

    std::vector<const Molecule*> reversed(products.rbegin(), products.rend());
    Make<Product>(this, Vec2{0, 0}, R0, reversed.front());
    Rot direction = R0;
    for (std::size_t i = 1; i < reversed.size(); ++i) {
        const Molecule* product = reversed[i];
        const Vec2 productPosition = OffsetInDirection({0, 0}, direction, 1);
        Make<Product>(this, productPosition, R0, product);
        outputArms_[product->Id()] =
            Make<Arm>(this, RotateBy(productPosition, R300), direction + R120, ArmType::Arm1, 1);
        direction = direction - R120;
    }
}

void MonoAssemblerModule::AddAtom(Element, int productID) {
    const auto it = outputArms_.find(productID);
    if (it != outputArms_.end())
        writer.WriteGrabResetAction(it->second, Instr::RotateCW);
}

LinearAssemblerModule::LinearAssemblerModule(GameObject* parent, ProgramWriter& writer,
                                             Vec2 position,
                                             const std::vector<const Molecule*>& products)
    : MoleculeAssembler(parent, writer, position), products_(products) {
    for (const Molecule* product : products) {
        if (product->Height() != 1)
            throw std::invalid_argument("linear assembler expects horizontal molecules");
        if (product->HasTriplex())
            throw std::invalid_argument("linear assembler does not support triplex bonds");
    }
    Make<Glyph>(this, Vec2{0, 0}, R0, GlyphType::Bonding);
    if (products.size() == 1) {
        arm_ = Make<Arm>(this, Vec2{0, -2}, R60, ArmType::Piston, 2);
        Make<Track>(this, Vec2{0, -1},
                    std::vector<Track::Segment>{{R240, 1}, {R0, 1}});
        outputPosition_ = {0, 1};
    } else {
        arm_ = Make<Arm>(this, Vec2{0, -1}, R60, ArmType::Piston, 1);
        Make<Track>(this, arm_->LocalTransform().position, R0, 2);
        outputPosition_ = {2, 0};
    }
    productConveyor_ = Make<ProductConveyor>(this, writer, outputPosition_, products_);
}

void LinearAssemblerModule::AddAtom(Element, int productID) {
    const Molecule* product = nullptr;
    for (const Molecule* candidate : products_)
        if (candidate->Id() == productID) product = candidate;
    if (!product) throw std::invalid_argument("unknown linear product id");
    if (!assembling_ || currentProduct_ != product) {
        currentProduct_ = product;
        column_ = product->Width() - 1;
        assembling_ = true;
    }
    AssembleStep();
}

void LinearAssemblerModule::AssembleStep() {
    if (!assembling_ || !currentProduct_) return;
    const int x = column_;
    if (x > 0) {
        writer.WriteGrabResetAction(arm_, Instr::MovePos);
    } else {
        if (products_.size() == 1)
            writer.WriteGrabResetAction(arm_, Instr::MoveNeg);
        else
            writer.WriteGrabResetAction(arm_,
                                        std::vector<Instr>{Instr::MovePos, Instr::MovePos});
        productConveyor_->MoveProductToOutputLocation(*currentProduct_);
    }
    --column_;
    if (column_ < 0) assembling_ = false;
}

// ── Universal assembler ─────────────────────────────────────────────────────
UniversalAssemblerModule::UniversalAssemblerModule(GameObject* parent, ProgramWriter& writer,
                                                   Vec2 position,
                                                   const std::vector<const Molecule*>& products)
    : MoleculeAssembler(parent, writer, position), products_(products) {
    width_ = 0;
    for (const Molecule* product : products_) width_ = std::max(width_, product->Width());
    assemblyArea_ = Make<AssemblyArea>(this, writer, width_, products_);
    productConveyor_ = Make<ProductConveyor>(this, writer, OutputPosition(), products_);
}

std::set<const Atom*> UniversalAssemblerModule::GetConnectedAtoms(
    const Atom& startAtom, const std::vector<const Atom*>& allAtoms, bool ignoreTriplexBonds) {
    std::set<const Atom*> seen{&startAtom};
    std::set<const Atom*> connected;
    std::vector<const Atom*> queue{&startAtom};
    while (!queue.empty()) {
        const Atom* atom = queue.back();
        queue.pop_back();
        for (int dir = 0; dir < 6; ++dir) {
            const BondType bond = atom->bonds[static_cast<std::size_t>(dir)];
            const bool included = bond == BondType::Single ||
                                  (!ignoreTriplexBonds && bond == BondType::Triplex);
            if (!included) continue;
            const Vec2 otherPosition = OffsetInDirection(atom->position, Rot{dir}, 1);
            const Atom* other = nullptr;
            for (const Atom* candidate : allAtoms)
                if (candidate->position == otherPosition) {
                    other = candidate;
                    break;
                }
            if (other && !seen.contains(other)) {
                seen.insert(other);
                connected.insert(other);
                queue.push_back(other);
            }
        }
    }
    return connected;
}

std::vector<const Atom*> UniversalAssemblerModule::GetAtomsToGrabForRow(
    int row, bool ignoreTriplexBonds) const {
    std::vector<const Atom*> out;
    std::set<const Atom*> connected;
    for (int x = width_ - 1; x >= 0; --x) {
        const Atom* atom = currentProduct_ ? currentProduct_->GetAtom({x, row}) : nullptr;
        if (atom && !connected.contains(atom)) {
            out.push_back(atom);
            const auto reachable = GetConnectedAtoms(*atom, assembledAtoms_, ignoreTriplexBonds);
            connected.insert(reachable.begin(), reachable.end());
        }
    }
    return out;
}

void UniversalAssemblerModule::GrabAtom(const Atom& atom) {
    assembledAtoms_.push_back(&atom);
    Arm* arm = assemblyArea_->lowerArms[static_cast<std::size_t>(currentArm_)];
    if (atom.bonds[static_cast<std::size_t>(R0.value)] == BondType::Single) {
        assemblyArea_->SetUsedBonders(GlyphType::Bonding, R0);
    } else {
        const int distance = (width_ - 1) - currentArm_;
        writer.AdjustTime(-distance);
        writer.Write(arm, std::vector<Instr>(static_cast<std::size_t>(distance), Instr::MovePos));
        writer.Write(arm, Instr::Grab);
    }
    writer.Write(arm, Instr::MovePos);
    if (atom.bonds[static_cast<std::size_t>(R180.value)] != BondType::Single) {
        writer.Write(arm, std::vector<Instr>(static_cast<std::size_t>(atom.position.x + 1),
                                             Instr::MovePos));
        --currentArm_;
    }
}

void UniversalAssemblerModule::FinishRow(int row) {
    std::vector<Arm*> activeArms;
    for (int i = currentArm_ + 1; i < width_; ++i)
        activeArms.push_back(assemblyArea_->lowerArms[static_cast<std::size_t>(i)]);
    BondProgrammer programmer(assemblyArea_, *currentProduct_, row);
    programmer.Generate();

    if (row == currentProduct_->Height() - 1) {
        if (row == 0 && programmer.Instructions().empty()) {
            writer.Write(activeArms, std::vector<Instr>{Instr::Extend, Instr::Reset});
            return;
        }
        writer.Write(activeArms, Instr::Reset);
        writer.AdjustTime(-2);
        std::vector<Arm*> grabArms;
        for (const Atom* atom : GetAtomsToGrabForRow(row, true))
            grabArms.push_back(assemblyArea_->upperArms[static_cast<std::size_t>(atom->position.x)]);
        writer.Write(grabArms, std::vector<Instr>{Instr::Retract, Instr::Grab, Instr::Extend});
        if (programmer.Instructions().empty()) {
            writer.Write(assemblyArea_->upperArms, Instr::Extend);
            return;
        }
        writer.Write(assemblyArea_->upperArms, programmer.Instructions());
        writer.Write(assemblyArea_->upperArms, programmer.ReturnInstructions());
        if (row == 0)
            writer.Write(assemblyArea_->upperArms, Instr::Drop);
        else
            writer.Write(assemblyArea_->upperArms, Instr::Extend);
        return;
    }

    writer.Write(activeArms, Instr::Extend);
    std::vector<Arm*> bothArms = activeArms;
    bothArms.insert(bothArms.end(), assemblyArea_->upperArms.begin(),
                    assemblyArea_->upperArms.end());
    writer.Write(bothArms, programmer.Instructions());
    writer.Write(activeArms, Instr::Reset, false);
    if (row > 0) {
        writer.Write(assemblyArea_->upperArms,
                     std::vector<Instr>{Instr::Drop, Instr::Retract});
        std::vector<Arm*> grabArms;
        for (const Atom* atom : GetAtomsToGrabForRow(row, false))
            grabArms.push_back(assemblyArea_->upperArms[static_cast<std::size_t>(atom->position.x)]);
        writer.Write(grabArms, Instr::Grab);
        writer.Write(assemblyArea_->upperArms, programmer.ReturnInstructions());
        writer.Write(assemblyArea_->upperArms, Instr::Extend);
    } else {
        writer.Write(assemblyArea_->upperArms, programmer.ReturnInstructions());
        writer.Write(assemblyArea_->upperArms, Instr::Reset);
    }
}

void UniversalAssemblerModule::StartAssemble(const Molecule& product) {
    currentProduct_ = &product;
    assembledAtoms_.clear();
    assembleRow_ = product.Height() - 1;
    assembleAtomIndex_ = 0;
    currentArm_ = width_ - 1;
    assembling_ = true;
}

void UniversalAssemblerModule::AssembleStep() {
    if (!assembling_ || !currentProduct_) return;
    auto rowAtoms = currentProduct_->GetRow(assembleRow_);
    std::sort(rowAtoms.begin(), rowAtoms.end(),
              [](const Atom* a, const Atom* b) { return a->position.x > b->position.x; });
    if (rowAtoms.empty()) throw std::invalid_argument("product row is empty");
    if (assembleAtomIndex_ < static_cast<int>(rowAtoms.size()) - 1) {
        GrabAtom(*rowAtoms[static_cast<std::size_t>(assembleAtomIndex_)]);
        ++assembleAtomIndex_;
        return;
    }
    GrabAtom(*rowAtoms.back());
    FinishRow(assembleRow_);
    if (assembleRow_ == 0) {
        writer.AdjustTime(-1);
        productConveyor_->MoveProductToOutputLocation(*currentProduct_);
    }
    --assembleRow_;
    assembleAtomIndex_ = 0;
    currentArm_ = width_ - 1;
    if (assembleRow_ < 0) assembling_ = false;
}

void UniversalAssemblerModule::AddAtom(Element, int productID) {
    if (!assembling_) {
        const Molecule* product = nullptr;
        for (const Molecule* candidate : products_)
            if (candidate->Id() == productID) product = candidate;
        if (!product) throw std::invalid_argument("unknown product id");
        StartAssemble(*product);
    }
    AssembleStep();
}

void UniversalAssemblerModule::OptimizeParts() { assemblyArea_->OptimizeParts(); }

// ── Simple output area ──────────────────────────────────────────────────────
SimpleOutputAreaModule::SimpleOutputAreaModule(GameObject* parent, ProgramWriter& writer,
                                               const std::vector<const Molecule*>& products)
    : AtomGenerator(parent, writer) {
    bool allSingle = true;
    bool allLinear = true;
    bool anyTriplex = false;
    for (const Molecule* product : products) {
        allSingle &= product->Atoms().size() == 1;
        allLinear &= product->Height() == 1;
        anyTriplex |= product->HasTriplex();
    }
    const Vec2 position = OutputPosition();
    if (allSingle && products.size() == 1) {
        assembler_ = Make<SingleMonoAssemblerModule>(this, writer, position, products);
    } else if (allSingle && products.size() <= static_cast<std::size_t>(MonoAssemblerModule::kMaxProducts)) {
        assembler_ = Make<MonoAssemblerModule>(this, writer, position, products);
    } else if (allLinear && !anyTriplex) {
        assembler_ = Make<LinearAssemblerModule>(this, writer, position, products);
    } else if (UseHex3Assembler(products)) {
        assembler_ = Make<Hex3AssemblerModule>(this, writer, position, products);
    } else {
        assembler_ = Make<UniversalAssemblerModule>(this, writer, position, products);
    }
}

void SimpleOutputAreaModule::Consume(Element element, int id) {
    assembler_->AddAtom(element, id);
}

void SimpleOutputAreaModule::OptimizeParts() { assembler_->OptimizeParts(); }

// ── Factory ─────────────────────────────────────────────────────────────────
void CreateAtomGenerators(ElementPipeline& pipeline, ProgramWriter& writer,
                          std::unique_ptr<AtomGenerator>& root) {
    AtomGenerator* previous = nullptr;
    for (ElementGenerator* generator : pipeline.Generators()) {
        AtomGenerator* atom = nullptr;
        if (auto* input = dynamic_cast<InputGenerator*>(generator)) {
            std::vector<const Molecule*> reagents;
            bool allSingle = true;
            for (const ElementInput& entry : input->Inputs()) {
                reagents.push_back(&entry.GetMolecule());
                allSingle &= entry.GetMolecule().Atoms().size() == 1;
            }
            if (allSingle && reagents.size() <= static_cast<std::size_t>(SimpleInputAreaModule::kMaxReagents)) {
                auto module = std::make_unique<SimpleInputAreaModule>(nullptr, writer, reagents);
                atom = module.get();
                root = std::move(module);
            } else {
                auto module = std::make_unique<ComplexInputAreaModule>(nullptr, writer, reagents);
                atom = module.get();
                root = std::move(module);
            }
            previous = atom;
            generator->SetAtom(atom);
            continue;
        }
        if (auto* purifier = dynamic_cast<MetalPurifierGenerator*>(generator)) {
            atom = previous->Make<PurifierAtomGenerator>(previous, writer, purifier->Sequences());
        } else if (dynamic_cast<MetalProjectorGenerator*>(generator)) {
            atom = previous->Make<ProjectorAtomGenerator>(previous, writer);
        } else if (dynamic_cast<QuintessenceDisperserGenerator*>(generator)) {
            atom = previous->Make<DisperserAtomGenerator>(previous, writer);
        } else if (auto* salt = dynamic_cast<SaltGenerator*>(generator)) {
            if (salt->RequiresCardinalPassThrough())
                atom = previous->Make<SaltAtomGenerator>(previous, writer);
            else
                atom = previous->Make<SaltNoPassThroughAtomGenerator>(previous, writer);
        } else if (dynamic_cast<VanBerloGenerator*>(generator)) {
            atom = previous->Make<VanBerloAtomGenerator>(previous, writer);
        } else if (dynamic_cast<MorsVitaeGenerator*>(generator)) {
            atom = previous->Make<MorsVitaeAtomGenerator>(previous, writer);
        } else if (dynamic_cast<QuintessenceGenerator*>(generator)) {
            atom = previous->Make<QuintessenceAtomGenerator>(previous, writer);
        } else if (auto* buffer = dynamic_cast<MultiStackElementBuffer*>(generator)) {
            atom = previous->Make<BufferAtomGenerator>(previous, writer, buffer->GetBufferInfo());
        } else if (auto* output = dynamic_cast<OutputGenerator*>(generator)) {
            atom = previous->Make<SimpleOutputAreaModule>(previous, writer,
                                                          output->ProductPointers());
        } else {
            throw std::invalid_argument("unknown element generator in pipeline");
        }
        if (!atom) throw std::invalid_argument("failed to create atom generator");
        atom->LocalTransformRef().position = previous->OutputPosition();
        previous = atom;
        generator->SetAtom(atom);
    }
}

} // namespace opusoptima::solvers::standard
