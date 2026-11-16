#include "StandardModel.hpp"

// Ported from gtw123/OpusSolver@fdedd3f01a238b941ceba8bace7ea35cbd3b8c41 (MIT).
// The module map is documented in README.md.


#include <algorithm>
#include <stdexcept>

namespace opusoptima::solvers::standard {

// ── Molecule ────────────────────────────────────────────────────────────────
Molecule::Molecule(std::vector<Atom> atoms, int id) : atoms_(std::move(atoms)), id_(id) {
    for (const Atom& atom : atoms_) {
        hasRepeats_ |= atom.element == Element::Repeat;
        for (BondType bond : atom.bonds) hasTriplex_ |= bond == BondType::Triplex;
    }
    AdjustBounds();
    NormalizeOrientation();
}

void Molecule::AdjustBounds() {
    if (atoms_.empty()) {
        width_ = height_ = diagonal_ = 0;
        return;
    }
    int minX = atoms_.front().position.x;
    int minY = atoms_.front().position.y;
    int maxX = minX;
    int maxY = minY;
    int minDiag = minX + minY;
    int maxDiag = minDiag;
    for (const Atom& atom : atoms_) {
        minX = std::min(minX, atom.position.x);
        minY = std::min(minY, atom.position.y);
        maxX = std::max(maxX, atom.position.x);
        maxY = std::max(maxY, atom.position.y);
        minDiag = std::min(minDiag, atom.position.x + atom.position.y);
        maxDiag = std::max(maxDiag, atom.position.x + atom.position.y);
    }
    width_ = maxX - minX + 1;
    height_ = maxY - minY + 1;
    diagonal_ = maxDiag - minDiag + 1;

    const Vec2 offset{minX, minY};
    for (Atom& atom : atoms_) atom.position = atom.position - offset;
    glyphTransform_.position = glyphTransform_.position - offset;
}

void Molecule::NormalizeOrientation() {
    if (hasRepeats_) return;
    if (height_ > width_ || height_ > diagonal_) {
        Rotate60CW();
        if (height_ > width_ || height_ > diagonal_) Rotate60CW();
    }
}

void Molecule::RotateBy(Rot rotation) {
    for (Atom& atom : atoms_) {
        atom.position = standard::RotateBy(atom.position, rotation);
        const auto old = atom.bonds;
        for (int dir = 0; dir < 6; ++dir) {
            atom.bonds[static_cast<std::size_t>((dir + rotation.value) % 6)] =
                old[static_cast<std::size_t>(dir)];
        }
    }
    glyphTransform_ = Transform{{0, 0}, rotation}.Apply(glyphTransform_);
    AdjustBounds();
}

void Molecule::ExpandRepeats() {
    std::vector<std::size_t> repeatIndices;
    for (std::size_t i = 0; i < atoms_.size(); ++i)
        if (atoms_[i].element == Element::Repeat) repeatIndices.push_back(i);
    if (repeatIndices.empty()) return;
    if (repeatIndices.size() > 1) throw std::invalid_argument("more than one repeat atom");

    const std::size_t repeatIndex = repeatIndices.front();
    const Vec2 repeatPosition = atoms_[repeatIndex].position;
    const auto repeatBonds = atoms_[repeatIndex].bonds;
    const auto row = GetRow(repeatPosition.y);
    if (row.empty()) throw std::invalid_argument("repeat atom row is empty");
    const Vec2 leftmost = row.front()->position;
    const int width = repeatPosition.x - leftmost.x;
    if (width == 0) throw std::invalid_argument("no atom to the left of the repeat atom");

    std::vector<Atom> atomsToCopy;
    for (const Atom& atom : atoms_)
        if (atom.element != Element::Repeat) atomsToCopy.push_back(atom);

    constexpr int kRepeatCount = 6;
    for (int copy = 1; copy < kRepeatCount; ++copy) {
        const Vec2 offset{copy * width, 0};
        for (const Atom& atom : atomsToCopy) {
            Atom newAtom = atom;
            newAtom.position = atom.position + offset;
            if (atom.position == leftmost) {
                // Bond the copy to the rest of the polymer by copying the repeat
                // atom's bonds, matching upstream's ExpandRepeats.
                for (int dir = 0; dir < 6; ++dir)
                    if (repeatBonds[static_cast<std::size_t>(dir)] != BondType::None)
                        newAtom.bonds[static_cast<std::size_t>(dir)] =
                            repeatBonds[static_cast<std::size_t>(dir)];
            }
            atoms_.push_back(newAtom);
        }
    }

    // Move the repeat atom to the end and make it a real atom, as upstream does.
    Atom& repeatAtom = atoms_[repeatIndex];
    repeatAtom.position = repeatPosition + Vec2{(kRepeatCount - 1) * width, 0};
    for (const Atom& candidate : atomsToCopy)
        if (candidate.position == leftmost) {
            repeatAtom.element = candidate.element;
            break;
        }
    AdjustBounds();
}

// ── Puzzle conversion ───────────────────────────────────────────────────────
namespace {

[[nodiscard]] Molecule ConvertMolecule(const io::Molecule& source, int id) {
    std::vector<Atom> atoms;
    atoms.reserve(source.atoms.size());
    for (const auto& atom : source.atoms) {
        atoms.push_back(Atom{ElementFromAtom(atom.type), Vec2{atom.position.q, atom.position.r}, {}});
    }
    for (const auto& bond : source.bonds) {
        const Vec2 from{bond.from.q, bond.from.r};
        const Vec2 to{bond.to.q, bond.to.r};
        const auto direction = ToRotation(to - from);
        if (!direction) throw std::invalid_argument("bond offset is not a hex direction");
        auto findAtom = [&](Vec2 position) -> Atom* {
            for (Atom& atom : atoms)
                if (atom.position == position) return &atom;
            return nullptr;
        };
        Atom* fromAtom = findAtom(from);
        Atom* toAtom = findAtom(to);
        if (!fromAtom || !toAtom) throw std::invalid_argument("bond endpoint has no atom");
        const BondType type = BondFromBits(bond.bits);
        fromAtom->bonds[static_cast<std::size_t>(direction->value)] = type;
        toAtom->bonds[static_cast<std::size_t>(Rotate180(*direction).value)] = type;
    }
    return Molecule(std::move(atoms), id);
}

} // namespace

Puzzle BuildPuzzle(const io::PuzzleData& data) {
    if (data.productionInfo) throw std::invalid_argument("production puzzles are unsupported");

    Puzzle puzzle;
    puzzle.name = data.name;
    puzzle.outputScale = data.outputMultiplier;

    for (std::size_t i = 0; i < data.outputs.size(); ++i) {
        Molecule molecule = ConvertMolecule(data.outputs[i], static_cast<int>(i));
        if (molecule.HasRepeats()) molecule.ExpandRepeats();
        puzzle.products.push_back(std::move(molecule));
    }
    for (std::size_t i = 0; i < data.inputs.size(); ++i) {
        puzzle.reagents.push_back(ConvertMolecule(data.inputs[i], static_cast<int>(i)));
    }

    puzzle.allowedArmTypes = {ArmType::Arm1, ArmType::Arm2, ArmType::Arm3, ArmType::Arm6,
                              ArmType::Piston};
    if (((data.permissionBits >> 24) & 0x10U) != 0) puzzle.allowedArmTypes.insert(ArmType::VanBerlo);

    const std::uint64_t glyphFlags = (data.permissionBits >> 8) & 0xfffU;
    puzzle.allowedGlyphs.insert(GlyphType::Equilibrium);
    const std::pair<std::uint64_t, std::vector<GlyphType>> mapping[] = {
        {0x0001, {GlyphType::Bonding}},
        {0x0002, {GlyphType::Unbonding}},
        {0x0004, {GlyphType::MultiBonding}},
        {0x0008, {GlyphType::TriplexBonding}},
        {0x0010, {GlyphType::Calcification}},
        {0x0020, {GlyphType::Duplication}},
        {0x0040, {GlyphType::Projection}},
        {0x0080, {GlyphType::Purification}},
        {0x0100, {GlyphType::Animismus}},
        {0x0200, {GlyphType::Disposal}},
        {0x0400, {GlyphType::Unification, GlyphType::Dispersion}},
    };
    std::uint64_t remaining = glyphFlags;
    for (const auto& [flag, glyphs] : mapping) {
        if ((remaining & flag) != 0) {
            for (GlyphType glyph : glyphs) puzzle.allowedGlyphs.insert(glyph);
            remaining &= ~flag;
        }
    }
    if (remaining != 0) throw std::invalid_argument("puzzle allows unknown glyphs");
    return puzzle;
}

// ── Glyph / track cells ─────────────────────────────────────────────────────

Track::Track(GameObject* parent, Vec2 position, std::vector<Segment> segments)
    : GameObject(parent, {position, R0}) {
    Vec2 pos{0, 0};
    path_.push_back(pos);
    for (const Segment& segment : segments) {
        for (int i = 0; i < segment.length; ++i) {
            pos = OffsetInDirection(pos, segment.direction, 1);
            path_.push_back(pos);
        }
    }
}

void Track::TrimPath(int firstIndex, int lastIndex) {
    if (lastIndex < firstIndex) throw std::invalid_argument("invalid track trim range");
    if (static_cast<std::size_t>(lastIndex + 1) < path_.size())
        path_.erase(path_.begin() + lastIndex + 1, path_.end());
    if (firstIndex > 0) {
        path_.erase(path_.begin(), path_.begin() + firstIndex);
        const Vec2 originOffset = path_.front();
        for (Vec2& cell : path_) cell = cell - originOffset;
        transform_.position = transform_.position + originOffset;
    }
}

// ── Game object tree ────────────────────────────────────────────────────────
Transform GameObject::WorldTransform() const {
    if (!parent_) return transform_;
    return parent_->WorldTransform().Apply(transform_);
}

namespace {

void Collect(GameObject* object, std::vector<GameObject*>& out) {
    if (!object || object->Removed()) return;
    out.push_back(object);
    for (GameObject* child : object->Children()) Collect(child, out);
}

} // namespace

std::vector<GameObject*> CollectOutputObjects(GameObject* root) {
    std::vector<GameObject*> all;
    Collect(root, all);
    std::vector<GameObject*> out;
    for (GameObject* object : all)
        if (dynamic_cast<Glyph*>(object)) out.push_back(object);
    for (GameObject* object : all)
        if (dynamic_cast<Arm*>(object)) out.push_back(object);
    for (GameObject* object : all)
        if (dynamic_cast<Track*>(object)) out.push_back(object);
    for (GameObject* object : all)
        if (dynamic_cast<MoleculeInputOutput*>(object)) out.push_back(object);
    return out;
}

} // namespace opusoptima::solvers::standard
