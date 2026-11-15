#pragma once

// Faithful C++ port of the upstream OpusSolver Standard solver model
// (gtw123/OpusSolver@fdedd3f01a238b941ceba8bace7ea35cbd3b8c41, MIT).
//
// The port keeps upstream's coordinate, rotation and element conventions so the
// hand-tuned module geometry and programs can be copied verbatim:
//   * Vec2 is (X, Y) == the solution format's (q, r);
//   * direction 0..5 is (1,0) (0,1) (-1,1) (-1,0) (0,-1) (1,-1);
//   * Rot is counter-clockwise in 60 degree steps;
//   * Element order is upstream's logical order, not the file's one-based IDs.

#include "BinaryParser.hpp"
#include "HexGrid.hpp"
#include "PuzzleSpecs.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <stdexcept>
#include <utility>
#include <vector>

namespace opusoptima::solvers::standard {

// ── Logical element order (upstream Element.cs) ──────────────────────────────
enum class Element {
    Salt,
    Air,
    Fire,
    Water,
    Earth,
    Quicksilver,
    Lead,
    Tin,
    Iron,
    Copper,
    Silver,
    Gold,
    Mors,
    Vitae,
    Quintessence,
    Repeat
};

[[nodiscard]] inline int ElemIndex(Element e) { return static_cast<int>(e); }

[[nodiscard]] inline Element ElementFromAtom(core::AtomType type) {
    switch (type) {
    case core::AtomType::Salt: return Element::Salt;
    case core::AtomType::Air: return Element::Air;
    case core::AtomType::Earth: return Element::Earth;
    case core::AtomType::Fire: return Element::Fire;
    case core::AtomType::Water: return Element::Water;
    case core::AtomType::Quicksilver: return Element::Quicksilver;
    case core::AtomType::Gold: return Element::Gold;
    case core::AtomType::Silver: return Element::Silver;
    case core::AtomType::Copper: return Element::Copper;
    case core::AtomType::Iron: return Element::Iron;
    case core::AtomType::Tin: return Element::Tin;
    case core::AtomType::Lead: return Element::Lead;
    case core::AtomType::Vitae: return Element::Vitae;
    case core::AtomType::Mors: return Element::Mors;
    case core::AtomType::Repeat: return Element::Repeat;
    case core::AtomType::Quintessence: return Element::Quintessence;
    }
    throw std::invalid_argument("unknown atom type");
}


// ── Hex geometry (upstream Vector2 / HexRotation / Transform2D) ─────────────
struct Vec2 {
    int x{};
    int y{};

    friend bool operator==(const Vec2&, const Vec2&) = default;
};

[[nodiscard]] inline Vec2 operator+(Vec2 a, Vec2 b) { return {a.x + b.x, a.y + b.y}; }
[[nodiscard]] inline Vec2 operator-(Vec2 a, Vec2 b) { return {a.x - b.x, a.y - b.y}; }
[[nodiscard]] inline Vec2 operator*(Vec2 a, int k) { return {a.x * k, a.y * k}; }

[[nodiscard]] inline Vec2 Add(Vec2 a, int x, int y) { return {a.x + x, a.y + y}; }

struct Rot {
    int value{};

    friend bool operator==(const Rot&, const Rot&) = default;
};

inline constexpr Rot R0{0};
inline constexpr Rot R60{1};
inline constexpr Rot R120{2};
inline constexpr Rot R180{3};
inline constexpr Rot R240{4};
inline constexpr Rot R300{5};

[[nodiscard]] inline Rot operator+(Rot a, Rot b) { return Rot{(a.value + b.value) % 6}; }
[[nodiscard]] inline Rot operator-(Rot a, Rot b) { return Rot{(a.value - b.value + 6) % 6}; }
[[nodiscard]] inline Rot Rotate60CCW(Rot a) { return Rot{(a.value + 1) % 6}; }
[[nodiscard]] inline Rot Rotate60CW(Rot a) { return Rot{(a.value + 5) % 6}; }
[[nodiscard]] inline Rot Rotate180(Rot a) { return Rot{(a.value + 3) % 6}; }

[[nodiscard]] inline Vec2 RotateBy(Vec2 v, Rot r) {
    switch (r.value) {
    case 0: return {v.x, v.y};
    case 1: return {-v.y, v.x + v.y};
    case 2: return {-v.x - v.y, v.x};
    case 3: return {-v.x, -v.y};
    case 4: return {v.y, -v.x - v.y};
    case 5: return {v.x + v.y, -v.x};
    default: throw std::invalid_argument("invalid rotation");
    }
}

[[nodiscard]] inline Vec2 OffsetInDirection(Vec2 v, Rot direction, int length) {
    switch (direction.value) {
    case 0: return {v.x + length, v.y};
    case 1: return {v.x, v.y + length};
    case 2: return {v.x - length, v.y + length};
    case 3: return {v.x - length, v.y};
    case 4: return {v.x, v.y - length};
    case 5: return {v.x + length, v.y - length};
    default: throw std::invalid_argument("invalid direction");
    }
}

[[nodiscard]] inline std::optional<Rot> ToRotation(Vec2 delta) {
    if (delta.y == 0) return delta.x > 0 ? std::optional<Rot>(R0)
                            : delta.x < 0 ? std::optional<Rot>(R180)
                                          : std::nullopt;
    if (delta.x == 0) return delta.y > 0 ? std::optional<Rot>(R60)
                            : delta.y < 0 ? std::optional<Rot>(R240)
                                          : std::nullopt;
    if (delta.x + delta.y == 0) {
        return delta.y > 0 ? std::optional<Rot>(R120) : std::optional<Rot>(R300);
    }
    return std::nullopt;
}

[[nodiscard]] inline int Sign(int v) { return (v > 0) - (v < 0); }

[[nodiscard]] inline int DistanceBetween(Vec2 a, Vec2 b) {
    const Vec2 diff = b - a;
    if (Sign(diff.x) == Sign(diff.y)) {
        return std::abs(diff.x + diff.y);
    }
    return std::max(std::abs(diff.x), std::abs(diff.y));
}

struct Transform {
    Vec2 position{};
    Rot rotation{};

    [[nodiscard]] Vec2 Apply(Vec2 vec) const { return position + RotateBy(vec, rotation); }
    [[nodiscard]] Rot Apply(Rot rot) const { return rotation + rot; }
    [[nodiscard]] Transform Apply(const Transform& t) const {
        return {Apply(t.position), Apply(t.rotation)};
    }
};

// ── Periodic table helpers (upstream PeriodicTable.cs) ──────────────────────
struct PeriodicTable {
    static const std::array<Element, 4>& Cardinals() {
        static const std::array<Element, 4> k{Element::Air, Element::Fire, Element::Water,
                                              Element::Earth};
        return k;
    }
    static const std::array<Element, 6>& Metals() {
        static const std::array<Element, 6> k{Element::Lead, Element::Tin, Element::Iron,
                                              Element::Copper, Element::Silver, Element::Gold};
        return k;
    }
    static const std::array<Element, 2>& MorsVitae() {
        static const std::array<Element, 2> k{Element::Mors, Element::Vitae};
        return k;
    }
    static std::vector<Element> AllElements() {
        std::vector<Element> all{Element::Salt, Element::Quicksilver, Element::Quintessence};
        all.insert(all.end(), Cardinals().begin(), Cardinals().end());
        all.insert(all.end(), Metals().begin(), Metals().end());
        all.insert(all.end(), MorsVitae().begin(), MorsVitae().end());
        return all;
    }
    static int GetMetalPurity(Element metal) {
        switch (metal) {
        case Element::Lead: return 1;
        case Element::Tin: return 2;
        case Element::Iron: return 4;
        case Element::Copper: return 8;
        case Element::Silver: return 16;
        case Element::Gold: return 32;
        default: throw std::invalid_argument("not a metal");
        }
    }
    static std::vector<Element> GetMetalsWithPuritySameOrLower(int purity) {
        std::vector<Element> out;
        for (Element metal : Metals()) {
            if (GetMetalPurity(metal) <= purity) out.push_back(metal);
        }
        return out;
    }
    static int GetMetalDifference(Element source, Element dest) {
        return ElemIndex(dest) - ElemIndex(source);
    }
    static Element GetLowestMetal(Element a, Element b) {
        return a < b ? a : b;
    }
};

inline bool IsCardinal(Element e) {
    return e == Element::Air || e == Element::Fire || e == Element::Water || e == Element::Earth;
}
// ── Bond type and atoms ─────────────────────────────────────────────────────
enum class BondType : std::uint8_t { None = 0, Single = 1, Triplex = 14 };

inline BondType BondFromBits(std::uint8_t bits) {
    if (bits == 0) return BondType::None;
    if (bits == 1) return BondType::Single;
    // Any triplex colour mask is treated like upstream's combined mask.
    if ((bits & 0x0E) != 0) return BondType::Triplex;
    return BondType::None;
}

struct Atom {
    Element element{Element::Salt};
    Vec2 position{};
    std::array<BondType, 6> bonds{};
};

// ── Molecule (upstream Molecule.cs) ─────────────────────────────────────────
class Molecule {
    public:
    Molecule() = default;
    Molecule(std::vector<Atom> atoms, int id);

    [[nodiscard]] const std::vector<Atom>& Atoms() const { return atoms_; }
    [[nodiscard]] std::vector<Atom>& Atoms() { return atoms_; }
    [[nodiscard]] int Id() const { return id_; }
    [[nodiscard]] int Width() const { return width_; }
    [[nodiscard]] int Height() const { return height_; }
    [[nodiscard]] int DiagonalLength() const { return diagonal_; }
    [[nodiscard]] int Size() const { return std::max({height_, width_, diagonal_}); }
    [[nodiscard]] bool HasRepeats() const { return hasRepeats_; }
    [[nodiscard]] bool HasTriplex() const { return hasTriplex_; }
    [[nodiscard]] const Transform& GlyphTransform() const { return glyphTransform_; }

    [[nodiscard]] Atom* GetAtom(Vec2 position) {
        for (auto& atom : atoms_)
            if (atom.position == position) return &atom;
        return nullptr;
    }
    [[nodiscard]] const Atom* GetAtom(Vec2 position) const {
        for (const auto& atom : atoms_)
            if (atom.position == position) return &atom;
        return nullptr;
    }
    [[nodiscard]] const Atom* GetAdjacentAtom(Vec2 position, Rot direction) const {
        return GetAtom(OffsetInDirection(position, direction, 1));
    }
    [[nodiscard]] std::vector<const Atom*> GetRow(int row) const {
        std::vector<const Atom*> out;
        for (int x = 0; x < width_; ++x)
            if (const Atom* atom = GetAtom({x, row})) out.push_back(atom);
        return out;
    }
    [[nodiscard]] std::vector<const Atom*> GetColumn(int column) const {
        std::vector<const Atom*> out;
        for (int y = 0; y < height_; ++y)
            if (const Atom* atom = GetAtom({column, y})) out.push_back(atom);
        return out;
    }
    [[nodiscard]] std::vector<Atom> GetAtomsInInputOrder() const {
        std::vector<Atom> out = atoms_;
        std::stable_sort(out.begin(), out.end(), [](const Atom& a, const Atom& b) {
            if (a.position.y != b.position.y) return a.position.y > b.position.y;
            return a.position.x > b.position.x;
        });
        return out;
    }
    void RotateBy(Rot rotation);
    void Rotate60CW() { RotateBy(R300); }
    void ExpandRepeats();
    void AdjustBounds();

    private:
    void NormalizeOrientation();

    std::vector<Atom> atoms_;
    int id_{};
    int width_{};
    int height_{};
    int diagonal_{};
    bool hasRepeats_{};
    bool hasTriplex_{};
    Transform glyphTransform_{};
};

// ── Puzzle model (a small wrapper around io::PuzzleData) ────────────────────
enum class GlyphType {
    Bonding,
    MultiBonding,
    TriplexBonding,
    Unbonding,
    Calcification,
    Duplication,
    Projection,
    Purification,
    Animismus,
    Disposal,
    Equilibrium,
    Unification,
    Dispersion
};

enum class ArmType { Arm1, Arm2, Arm3, Arm6, Piston, VanBerlo };

struct Puzzle {
    std::string name;
    std::vector<Molecule> products;
    std::vector<Molecule> reagents;
    std::set<ArmType> allowedArmTypes;
    std::set<GlyphType> allowedGlyphs;
    int outputScale{1};

    [[nodiscard]] bool AllowsGlyph(GlyphType type) const { return allowedGlyphs.contains(type); }
    [[nodiscard]] bool AllowsArm(ArmType type) const { return allowedArmTypes.contains(type); }
};

/// Converts a parsed puzzle into the upstream model, expanding repeats.
[[nodiscard]] Puzzle BuildPuzzle(const io::PuzzleData& data);

// ── Game objects and board ownership ────────────────────────────────────────
class GameObject {
    public:
    virtual ~GameObject() = default;

    GameObject* Parent() const { return parent_; }

    Transform LocalTransform() const { return transform_; }
    Transform& LocalTransformRef() { return transform_; }
    [[nodiscard]] Transform WorldTransform() const;

    [[nodiscard]] std::vector<GameObject*> Children() const {
        std::vector<GameObject*> out;
        out.reserve(ownedChildren_.size());
        for (const auto& child : ownedChildren_) out.push_back(child.get());
        return out;
    }
    void Remove() { removed_ = true; }

    [[nodiscard]] bool Removed() const { return removed_; }

    /// Creates an owned child. The child constructor receives `this` as its
    /// parent argument, so calls take the form `Make<Arm>(this, position, ...)`.
    template <typename T, typename... Args>
    T* Make(Args&&... args) {
        auto child = std::make_unique<T>(std::forward<Args>(args)...);
        T* raw = child.get();
        ownedChildren_.push_back(std::move(child));
        return raw;
    }

    protected:
    GameObject(GameObject* parent, Transform transform) : parent_(parent), transform_(transform) {}

    GameObject* parent_{};
    Transform transform_{};
    std::vector<std::unique_ptr<GameObject>> ownedChildren_;
    bool removed_{};
};

class Arm : public GameObject {
    public:
    Arm(GameObject* parent, Vec2 position, Rot rotation, ArmType type, int extension = 1)
        : GameObject(parent, {position, rotation}), type(type), extension(extension),
          uniqueId(s_nextId++) {}

    ArmType type{ArmType::Arm1};
    int extension{1};
    int uniqueId{};

    static void ResetArmIds() { s_nextId = 0; }

    private:
    static inline int s_nextId = 0;
};

class Glyph : public GameObject {
    public:
    Glyph(GameObject* parent, Vec2 position, Rot rotation, GlyphType type)
        : GameObject(parent, {position, rotation}), type(type) {}
    GlyphType type;
};

class Track : public GameObject {
    public:
    struct Segment {
        Rot direction{};
        int length{};
    };

    Track(GameObject* parent, Vec2 position, Rot pathDirection, int pathLength)
        : Track(parent, position, std::vector<Segment>{{pathDirection, pathLength}}) {}
    Track(GameObject* parent, Vec2 position, std::vector<Segment> segments);

    [[nodiscard]] const std::vector<Vec2>& Path() const { return path_; }
    [[nodiscard]] bool IsLooping() const {
        return path_.size() > 2 && DistanceBetween(path_.front(), path_.back()) == 1;
    }
    [[nodiscard]] std::vector<Vec2> GetAllPathCells() const {
        const Transform transform = WorldTransform();
        std::vector<Vec2> cells;
        cells.reserve(path_.size());
        for (const Vec2& cell : path_) cells.push_back(transform.Apply(cell));
        return cells;
    }
    void TrimPath(int firstIndex, int lastIndex);

    private:
    std::vector<Vec2> path_;
};

class MoleculeInputOutput : public GameObject {
    public:
    MoleculeInputOutput(GameObject* parent, Vec2 position, Rot rotation, const Molecule* molecule)
        : GameObject(parent, {position, rotation}), molecule(molecule) {}
    const Molecule* molecule{};
};

class Reagent : public MoleculeInputOutput {
    public:
    Reagent(GameObject* parent, Vec2 position, Rot rotation, const Molecule* molecule)
        : MoleculeInputOutput(parent, position, rotation, molecule) {}
};

class Product : public MoleculeInputOutput {
    public:
    Product(GameObject* parent, Vec2 position, Rot rotation, const Molecule* molecule)
        : MoleculeInputOutput(parent, position, rotation, molecule) {}
};

/// Visual objects in upstream's solution writer order (glyphs, arms, tracks,
/// molecules), with removed objects excluded. The root owns the whole tree.
[[nodiscard]] std::vector<GameObject*> CollectOutputObjects(GameObject* root);

} // namespace opusoptima::solvers::standard
