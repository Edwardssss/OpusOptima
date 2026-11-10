#include "RowPhaseProfile.hpp"
#include "ExecutionIR.hpp"

#include <algorithm>
#include <limits>
#include <set>

namespace opusoptima::solvers {
namespace {
using IR = ExecutionIR;
using Type = core::AtomType;
using Coord = core::HexCoord;
void Check(bool condition, const char* message) {
    if (!condition) throw IR::Error(message);
}
IR::Edge CanonicalEdge(IR::AtomId a, IR::AtomId b) {
    return {std::min(a, b), std::max(a, b)};
}
bool Cardinal(Type t) {
    return t >= Type::Air && t <= Type::Water;
}
bool Metal(Type t) {
    return t >= Type::Silver && t <= Type::Lead;
}
bool Mask(unsigned bits) {
    return bits == 1 || (bits && !(bits & ~14U));
}
Coord Transform(Coord p, int turns, Coord shift) {
    std::int64_t q = p.q, r = p.r;
    for (int i = 0; i < turns; ++i) {
        const auto old = q;
        q = -r;
        r += old;
    }
    q += shift.q;
    r += shift.r;
    Check(q >= std::numeric_limits<int>::min() && q <= std::numeric_limits<int>::max() &&
              r >= std::numeric_limits<int>::min() && r <= std::numeric_limits<int>::max(),
          "coordinate overflow");
    return {static_cast<int>(q), static_cast<int>(r)};
}
bool Adjacent(Coord a, Coord b) {
    const std::int64_t q = std::int64_t(a.q) - b.q, r = std::int64_t(a.r) - b.r;
    return std::max({std::abs(q), std::abs(r), std::abs(q + r)}) == 1;
}
void ValidateMolecule(const io::Molecule& m, bool output) {
    Check(!m.atoms.empty(), "empty molecule");
    std::set<std::pair<int, int>> cells;
    for (const auto& a : m.atoms) {
        if (a.type == Type::Repeat) throw IR::Unsupported("repeating molecules unsupported");
        Check(static_cast<int>(a.type) >= 1 && static_cast<int>(a.type) <= 16, "invalid atom type");
        Check(cells.emplace(a.position.q, a.position.r).second, "overlapping molecule atoms");
    }
    auto at = [&](Coord p) {
        auto i =
            std::find_if(m.atoms.begin(), m.atoms.end(), [&](auto a) { return a.position == p; });
        Check(i != m.atoms.end(), "bond references missing atom");
        return static_cast<std::size_t>(i - m.atoms.begin());
    };
    std::set<std::pair<std::size_t, std::size_t>> edges;
    std::vector<std::set<std::size_t>> neighbors(m.atoms.size());
    for (auto b : m.bonds) {
        auto a = at(b.from), c = at(b.to);
        Check(a != c && Adjacent(b.from, b.to) && Mask(b.bits), "invalid molecule bond");
        // Inherited colored bonds are transported unchanged; only creation by a
        // bonding operation requires fire endpoints.
        Check(edges.emplace(std::min(a, c), std::max(a, c)).second, "duplicate molecule bond");
        neighbors[a].insert(c);
        neighbors[c].insert(a);
    }
    if (output) {
        std::set<std::size_t> reached{0};
        for (std::size_t previous = 0; previous != reached.size();) {
            previous = reached.size();
            auto copy = reached;
            for (auto a : copy)
                reached.insert(neighbors[a].begin(), neighbors[a].end());
        }
        if (reached.size() != m.atoms.size())
            throw IR::Unsupported("disconnected output unsupported");
    }
}
bool Matches(const IR::State& s, const std::vector<IR::AtomId>& ids, const io::Molecule& goal) {
    if (ids.size() != goal.atoms.size()) return false;
    auto rotate = [](Coord p, int turns) {
        std::int64_t q = p.q, r = p.r;
        for (int i = 0; i < turns; ++i) {
            const auto old = q;
            q = -r;
            r += old;
        }
        return std::pair{q, r};
    };
    for (int rotation = 0; rotation < 6; ++rotation) {
        for (auto anchor : ids) {
            // Normalize in int64, so matching is independent of absolute frame range.
            const auto origin = rotate(s.atoms.at(anchor).position, rotation);
            std::vector<IR::AtomId> mapped;
            for (auto target : goal.atoms) {
                auto found = std::find_if(ids.begin(), ids.end(), [&](auto id) {
                    auto atom = s.atoms.at(id);
                    auto p = rotate(atom.position, rotation);
                    return atom.type == target.type &&
                           p.first - origin.first ==
                               std::int64_t(target.position.q) - goal.atoms[0].position.q &&
                           p.second - origin.second ==
                               std::int64_t(target.position.r) - goal.atoms[0].position.r;
                });
                if (found == ids.end()) break;
                mapped.push_back(*found);
            }
            if (mapped.size() != ids.size()) continue;
            std::map<IR::Edge, std::uint8_t> expected, actual;
            for (auto bond : goal.bonds) {
                auto index = [&](Coord p) {
                    return std::find_if(goal.atoms.begin(), goal.atoms.end(),
                                        [&](auto a) { return a.position == p; }) -
                           goal.atoms.begin();
                };
                expected[CanonicalEdge(mapped[index(bond.from)], mapped[index(bond.to)])] =
                    bond.bits;
            }
            for (auto [edge, bits] : s.bonds)
                if (std::binary_search(ids.begin(), ids.end(), edge.a)) actual[edge] = bits;
            if (expected == actual) return true;
        }
    }
    return false;
}
IR::State EmptyMaterialState(const io::PuzzleData& p) {
    for (const auto& m : p.inputs)
        ValidateMolecule(m, false);
    for (const auto& m : p.outputs)
        ValidateMolecule(m, true);
    IR::State s;
    s.batches.resize(p.inputs.size());
    s.outputs.resize(p.outputs.size());
    return s;
}
} // namespace

ExecutionIR::State ExecutionIR::Initial(const io::PuzzleData& p) {
    if (p.productionInfo) throw Unsupported("production routing unsupported by material IR");
    return EmptyMaterialState(p);
}
ExecutionIR::State ExecutionIR::InitialForStaticProduction(const io::PuzzleData& p) {
    if (p.version != 3 || p.outputMultiplier <= 0 || !p.productionInfo ||
        p.productionInfo->chambers.empty())
        throw Unsupported("static production material context requires confirmed chamber geometry");
    if (p.productionInfo->vials.size() > 64 ||
        std::any_of(p.productionInfo->vials.begin(), p.productionInfo->vials.end(),
                    [](const auto& vial) { return vial.count < 0 || vial.count > 64; }))
        throw Unsupported("production visual records exceed independently supported bounds");
    return EmptyMaterialState(p);
}
ExecutionIR ExecutionIR::ForStaticProduction(const io::PuzzleData& p) {
    (void)InitialForStaticProduction(p);
    ExecutionIR program;
    program.staticProduction_ = true;
    return program;
}
std::vector<ExecutionIR::AtomId> ExecutionIR::Fragment(const State& s, AtomId atom) {
    Check(s.atoms.contains(atom), "unknown or consumed atom ID");
    std::set<AtomId> ids{atom};
    for (std::size_t previous = 0; previous != ids.size();) {
        previous = ids.size();
        for (auto [e, bits] : s.bonds)
            if (ids.contains(e.a) || ids.contains(e.b)) {
                ids.insert(e.a);
                ids.insert(e.b);
            }
    }
    return {ids.begin(), ids.end()};
}
void ExecutionIR::Apply(const io::PuzzleData& p, State& s, const Operation& op) {
    row_profile::Scope profile(row_profile::Scope::Within(row_profile::Replay) ? row_profile::ApplyReplay : row_profile::ApplyLive);
    using K = Kind;
    auto permission = [&](int bit) {
        if (p.permissionBits & (1ULL << bit)) return;
        if (op.kind == K::Unbond)
            throw PermissionFailure(bit, "unbonder permission for cuts missing");
        if (op.kind == K::Bond)
            throw PermissionFailure(bit, op.bits == 1
                                             ? "ordinary-bonder permission for new bonds missing"
                                             : "colored-bonder permission for new bonds missing");
        throw PermissionFailure(bit,
                                "operation permission missing (bit " + std::to_string(bit) + ")");
    };
    Check(!op.wheelCatalyst || op.kind == K::Duplicate,
          "device catalyst is only valid for duplication");
    Check(op.kind == K::Bond || op.kind == K::Unbond || op.bits == 0, "unexpected bond mask");
    Check(op.kind == K::Relocate || op.kind == K::Acquire ||
              (op.rotation == 0 && op.translation == Coord{}),
          "unexpected transform");
    Check(op.kind == K::Acquire || op.kind == K::Output || op.kind == K::Store ||
              op.kind == K::Retrieve || op.index == 0,
          "unexpected operation index");
    std::set<AtomId> distinct;
    for (auto id : op.atoms) {
        Check(s.atoms.contains(id) && distinct.insert(id).second, "invalid or repeated operand ID");
        for (const auto& [slot, ids] : s.stored)
            Check(!std::binary_search(ids.begin(), ids.end(), id), "operand is stored");
    }
    auto arity = [&](std::size_t n) { Check(op.atoms.size() == n, "incorrect operand count"); };
    auto add = [&](Type type, Coord pos) {
        Check(s.nextId != std::numeric_limits<AtomId>::max(), "atom ID exhaustion");
        const auto id = s.nextId++;
        s.atoms.emplace(id, Atom{type, pos});
        return id;
    };
    auto remove = [&](const std::vector<AtomId>& ids) {
        for (auto id : ids)
            s.atoms.erase(id);
        std::erase_if(s.bonds, [&](auto entry) {
            return !s.atoms.contains(entry.first.a) || !s.atoms.contains(entry.first.b);
        });
    };
    if (op.kind == K::Acquire) {
        arity(0);
        Check(op.index < p.inputs.size(), "invalid input index");
        Check(op.rotation >= 0 && op.rotation < 6, "invalid input rotation");
        const auto& m = p.inputs[op.index];
        std::vector<AtomId> ids;
        for (auto a : m.atoms)
            ids.push_back(add(a.type, Transform(a.position, op.rotation, op.translation)));
        for (auto b : m.bonds) {
            auto at = [&](Coord pos) {
                return ids[std::find_if(m.atoms.begin(), m.atoms.end(),
                                        [&](auto a) { return a.position == pos; }) -
                           m.atoms.begin()];
            };
            s.bonds[CanonicalEdge(at(b.from), at(b.to))] = b.bits;
        }
        ++s.batches[op.index];
        return;
    }
    if (op.kind == K::Retrieve) {
        arity(0);
        Check(s.stored.erase(op.index) == 1, "empty storage slot");
        return;
    }
    if (op.kind == K::Relocate || op.kind == K::Store || op.kind == K::Discard ||
        op.kind == K::Output) {
        arity(1);
        auto ids = Fragment(s, op.atoms[0]);
        if (op.kind == K::Relocate) {
            Check(op.rotation >= 0 && op.rotation < 6, "invalid rotation");
            for (auto id : ids)
                s.atoms.at(id).position =
                    Transform(s.atoms.at(id).position, op.rotation, op.translation);
        } else if (op.kind == K::Store) {
            Check(!s.stored.contains(op.index), "occupied storage slot");
            s.stored.emplace(op.index, ids);
        } else if (op.kind == K::Discard) {
            permission(17);
            s.discarded += ids.size();
            remove(ids);
        } else {
            Check(op.index < p.outputs.size() && op.index < s.outputs.size(),
                  "invalid output index");
            Check(Matches(s, ids, p.outputs[op.index]), "output graph does not match");
            ++s.outputs[op.index];
            remove(ids);
        }
        return;
    }
    if (op.kind == K::Bond || op.kind == K::Unbond) {
        arity(2);
        Check(Mask(op.bits), "invalid bond mask");
        auto a = op.atoms[0], b = op.atoms[1];
        const auto edge = CanonicalEdge(a, b);
        if (op.kind == K::Bond) {
            permission(op.bits == 1 ? 8 : 11);
            if (op.bits != 1)
                Check(s.atoms.at(a).type == Type::Fire && s.atoms.at(b).type == Type::Fire,
                      "colored bond requires fire");
            Check(Adjacent(s.atoms.at(a).position, s.atoms.at(b).position),
                  "bond endpoints not adjacent");
            auto ids = Fragment(s, a);
            auto other = Fragment(s, b);
            if (ids != other) {
                for (auto x : ids)
                    for (auto y : other)
                        Check(s.atoms.at(x).position != s.atoms.at(y).position,
                              "bond would join overlapping fragments");
            }
            const auto old = s.bonds.contains(edge) ? s.bonds.at(edge) : 0;
            Check(!(old & op.bits) && Mask(old | op.bits),
                  "bond label already present or incompatible");
            s.bonds[edge] = old | op.bits;
        } else {
            permission(9);
            Check(s.bonds.contains(edge) && (s.bonds.at(edge) & op.bits) == op.bits,
                  "missing bond label");
            s.bonds.at(edge) &= ~op.bits;
            if (!s.bonds.at(edge)) s.bonds.erase(edge);
        }
        return;
    }
    const auto replaceEndpoint = [&](AtomId endpoint, Type type) {
        const auto product = add(type, s.atoms.at(endpoint).position);
        const auto bonds = s.bonds;
        s.bonds.clear();
        for (const auto& [original, bits] : bonds) {
            auto edge = original;
            if (edge.a == endpoint) edge.a = product;
            if (edge.b == endpoint) edge.b = product;
            s.bonds.emplace(CanonicalEdge(edge.a, edge.b), bits);
        }
        s.atoms.erase(endpoint);
        return product;
    };
    if (op.kind == K::Calcify && op.atoms.size() == 1 && Fragment(s, op.atoms[0]).size() > 1) {
        permission(12);
        Check(Cardinal(s.atoms.at(op.atoms[0]).type), "calcification requires cardinal");
        replaceEndpoint(op.atoms[0], Type::Salt);
        return;
    }
    if (op.kind == K::Duplicate && op.wheelCatalyst) {
        arity(1);
        permission(13);
        permission(28);
        const auto& wheel = *op.wheelCatalyst;
        Check(s.atoms.at(op.atoms[0]).type == Type::Salt && Cardinal(wheel.element),
              "wheel duplication requires salt and a cardinal device catalyst");
        Check(wheel.orientation >= 0 && wheel.orientation < 6, "invalid wheel orientation");
        const auto prior = s.wheelDevices.find(wheel.device);
        Check(prior == s.wheelDevices.end() || prior->second == wheel.orientation,
              "static wheel catalyst pose changed without a device operation");
        s.wheelDevices.emplace(wheel.device, wheel.orientation);
        replaceEndpoint(op.atoms[0], wheel.element);
        return;
    }
    if (op.kind == K::Duplicate && op.atoms.size() == 2 &&
        (Fragment(s, op.atoms[0]).size() > 1 || Fragment(s, op.atoms[1]).size() > 1)) {
        permission(13);
        Check(s.atoms.at(op.atoms[0]).type == Type::Salt && Cardinal(s.atoms.at(op.atoms[1]).type),
              "duplication requires salt and cardinal catalyst");
        replaceEndpoint(op.atoms[0], s.atoms.at(op.atoms[1]).type);
        return;
    }
    if (op.kind == K::Project && op.atoms.size() == 2 && Fragment(s, op.atoms[0]).size() > 1) {
        permission(14);
        const auto metal = op.atoms[0], mercury = op.atoms[1];
        Check(Metal(s.atoms.at(metal).type) && s.atoms.at(mercury).type == Type::Quicksilver,
              "projection requires metal and quicksilver");
        Check(Fragment(s, mercury).size() == 1, "projection mercury must be isolated");
        replaceEndpoint(metal, static_cast<Type>(static_cast<int>(s.atoms.at(metal).type) - 1));
        s.atoms.erase(mercury);
        return;
    }
    for (auto id : op.atoms)
        Check(Fragment(s, id).size() == 1, "chemistry requires isolated atoms");
    std::vector<Type> products;
    auto type = [&](std::size_t i) { return s.atoms.at(op.atoms.at(i)).type; };
    switch (op.kind) {
    case K::Calcify:
        arity(1);
        permission(12);
        Check(Cardinal(type(0)), "calcification requires cardinal");
        products = {Type::Salt};
        break;
    case K::Project:
        arity(2);
        permission(14);
        Check(Metal(type(0)) && type(1) == Type::Quicksilver,
              "projection requires metal and quicksilver");
        products = {static_cast<Type>(static_cast<int>(type(0)) - 1)};
        break;
    case K::Purify:
        arity(2);
        permission(15);
        Check(Metal(type(0)) && type(0) == type(1), "purification requires equal non-gold metals");
        products = {static_cast<Type>(static_cast<int>(type(0)) - 1)};
        break;
    case K::Animismus:
        arity(2);
        permission(16);
        Check(type(0) == Type::Salt && type(1) == Type::Salt, "animismus requires two salt");
        products = {Type::Vitae, Type::Mors};
        break;
    case K::Unify: {
        arity(4);
        permission(18);
        std::set<Type> types;
        for (auto id : op.atoms)
            types.insert(s.atoms.at(id).type);
        Check(types == std::set<Type>{Type::Air, Type::Earth, Type::Fire, Type::Water},
              "unification requires four cardinals");
        products = {Type::Quintessence};
        break;
    }
    case K::Disperse:
        arity(1);
        permission(18);
        Check(type(0) == Type::Quintessence, "dispersion requires quintessence");
        products = {Type::Air, Type::Earth, Type::Fire, Type::Water};
        break;
    case K::Duplicate:
        arity(2);
        permission(13);
        Check(type(0) == Type::Salt && Cardinal(type(1)),
              "duplication requires salt and cardinal catalyst");
        products = {type(1)};
        break;
    default:
        throw Unsupported("unsupported operation");
    }
    // Products are singleton fragments in independent local frames, not glyph cells.
    auto consumed = op.atoms;
    if (op.kind == K::Duplicate) consumed.resize(1);
    remove(consumed);
    for (auto product : products)
        add(product, {});
}
void ExecutionIR::Append(const io::PuzzleData& p, State& s, const Operation& op) {
    Check(Replay(p) == s, "append state differs from replay");
    auto next = s;
    Apply(p, next, op);
    steps.push_back({op, s, next});
    s = std::move(next);
}
void ExecutionIR::AppendFromTail(const io::PuzzleData& p, State& state,
                                 const Operation& operation) {
    row_profile::Scope profile(row_profile::Append);
    if (steps.empty()) {
        const auto expected = staticProduction_ ? InitialForStaticProduction(p) : Initial(p);
        Check(expected == state, "incremental append state differs from material tail");
    } else {
        Check(steps.back().effects == state, "incremental append state differs from material tail");
    }
    auto next = state;
    Apply(p, next, operation);
    steps.push_back({operation, state, next});
    state = std::move(next);
}
ExecutionIR::State ExecutionIR::Replay(const io::PuzzleData& p) const {
    row_profile::Scope profile(row_profile::Replay, steps.size());
    auto s = staticProduction_ ? InitialForStaticProduction(p) : Initial(p);
    for (const auto& step : steps) {
        Check(s == step.preconditions, "execution precondition mismatch");
        Apply(p, s, step.operation);
        Check(s == step.effects, "execution effect mismatch");
    }
    return s;
}
ExecutionIR::State ExecutionIR::ProjectOutput(const io::PuzzleData& puzzle, const State& tail,
                                             const Operation& output) const {
    Check(output.kind == Kind::Output, "projection requires Output");
    auto projected = Replay(puzzle);
    Check(projected == tail, "projected Output state differs from replay");
    Apply(puzzle, projected, output);
    return projected;
}
void ExecutionIR::RequireFiniteTerminal(const State& s, const FiniteRetention& terminal) {
    Check(s.outputs == terminal.outputs && s.discarded == 0, "finite output/discard mismatch");
    Check(terminal.capacity > 0 && terminal.capacity <= 6 &&
          terminal.slots.size() <= terminal.capacity, "finite slot capacity exceeded");
    Check(s.atoms.size() == terminal.slots.size() && s.stored.size() == terminal.slots.size() &&
          s.bonds.empty(), "finite inventory mismatch");
    std::set<AtomId> ids;
    std::set<std::size_t> slots;
    std::set<std::pair<int, int>> cells;
    for (const auto& slot : terminal.slots) {
        Check(slot.slot < terminal.capacity && slots.insert(slot.slot).second &&
              ids.insert(slot.atom).second, "duplicate finite slot or identity");
        Check(slot.type == core::AtomType::Vitae || slot.type == core::AtomType::Mors,
              "unsupported finite material");
        Check(s.atoms.contains(slot.atom) && s.atoms.at(slot.atom).type == slot.type,
              "finite material mismatch");
        Check(s.stored.contains(slot.slot) && s.stored.at(slot.slot) == std::vector<AtomId>{slot.atom},
              "finite custody mismatch");
        const auto cell = s.atoms.at(slot.atom).position;
        Check(cells.emplace(cell.q, cell.r).second, "overlapping finite inventory");
    }
}
void ExecutionIR::RequireComplete(const State& s, const std::vector<std::size_t>& counts) {
    Check(s.outputs == counts, "output count mismatch");
    Check(s.atoms.empty() && s.bonds.empty() && s.stored.empty(),
          "unconsumed inventory or catalyst");
}
} // namespace opusoptima::solvers
