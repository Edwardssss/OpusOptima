#include "FiniteRepeatingGraph.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <map>
#include <new>
#include <set>
#include <stdexcept>
#include <utility>
#include <vector>

#include "IndependentCapabilities.hpp"

namespace opusoptima::verification {
namespace {
static_assert(std::numeric_limits<int>::digits <= 31);
using Cell = std::pair<std::int64_t, std::int64_t>;
constexpr std::array<Cell, 6> directions{{{1, 0}, {0, 1}, {-1, 1}, {-1, 0}, {0, -1}, {1, -1}}};
struct Node {
    core::AtomType type;
    std::array<std::uint8_t, 6> bits{};
    std::size_t monomer{};
};
using Graph = std::map<Cell, Node>;
Cell cell(core::HexCoord p) {
    return {p.q, p.r};
}
Cell add(Cell p, Cell q) {
    return {p.first + q.first, p.second + q.second};
}
Cell shift(Cell p, Cell d, std::int64_t i) {
    return {p.first + i * d.first, p.second + i * d.second};
}
RepeatingGraphCell reported(Cell p) {
    return {p.first, p.second};
}
[[noreturn]] void malformed(const char* detail) {
    throw FiniteRepeatingGraphFailure{RepeatingGraphFailureCode::Malformed, detail};
}
[[noreturn]] void outside(const char* detail) {
    throw FiniteRepeatingGraphFailure{RepeatingGraphFailureCode::OutsideDomain, detail};
}
int direction(Cell a, Cell b) {
    const Cell delta{b.first - a.first, b.second - a.second};
    const auto found = std::find(directions.begin(), directions.end(), delta);
    return found == directions.end() ? -1 : static_cast<int>(found - directions.begin());
}
Graph validate(const io::Molecule& molecule) {
    Graph graph;
    for (const auto& atom : molecule.atoms) {
        const auto type = static_cast<unsigned>(atom.type);
        if (type < 1 || type > core::kAtomTypeNames.size()) malformed("unknown atom type");
        if (!graph.emplace(cell(atom.position), Node{atom.type}).second)
            malformed("duplicate atom coordinate");
    }
    for (const auto& bond : molecule.bonds) {
        const auto a = graph.find(cell(bond.from)), b = graph.find(cell(bond.to));
        if (a == graph.end() || b == graph.end()) malformed("missing bond endpoint");
        const int t = direction(a->first, b->first);
        if (t < 0) malformed("bond is not six-neighbor");
        if (!bond.bits || (bond.bits & ~0x0f) || ((bond.bits & 1) && bond.bits != 1))
            malformed("invalid bond bits");
        if (a->second.bits[t]) malformed("duplicate undirected bond");
        a->second.bits[t] = b->second.bits[(t + 3) % 6] = bond.bits;
    }
    return graph;
}
bool connected(const Graph& graph) {
    if (graph.empty()) return true;
    std::set<Cell> seen;
    std::vector<Cell> todo{graph.begin()->first};
    seen.insert(todo.front());
    for (std::size_t i = 0; i < todo.size(); ++i) {
        const auto& node = graph.at(todo[i]);
        for (std::size_t t = 0; t < 6; ++t) {
            const auto neighbor = add(todo[i], directions[t]);
            if (node.bits[t] && graph.contains(neighbor) && seen.insert(neighbor).second)
                todo.push_back(neighbor);
        }
    }
    return seen.size() == graph.size();
}
FiniteRepeatingGraphResult check(const FiniteRepeatingGraphRequest& request) {
    const auto& raw = request.rawTemplate;
    const auto& material = request.component;
    const auto limit = finiteOrdinaryProfile.maxInventory;
    // Bounds precede allocations and all structural/domain inspection.
    if (raw.atoms.size() > finiteOrdinaryProfile.maxMoleculeAtoms ||
        material.atoms.size() > limit || raw.bonds.size() > 3 * raw.atoms.size() ||
        material.bonds.size() > 3 * material.atoms.size())
        outside("graph resource bound");
    if (raw.atoms.size() > Graph{}.max_size() || material.atoms.size() > Graph{}.max_size())
        outside("container representability bound");
    std::size_t realCount = 0;
    for (const auto& atom : raw.atoms)
        realCount += atom.type != core::AtomType::Repeat;
    if (realCount && request.maxMonomers > limit / realCount)
        outside("expected vertex resource bound");
    const auto rawGraph = validate(raw);
    const auto actual = validate(material);
    const auto d = cell(request.displacement);
    if (!request.maxMonomers) malformed("zero monomer bound");
    if (d == Cell{}) malformed("zero displacement");
    std::size_t markerCount = 0, markerIndex = 0;
    for (std::size_t i = 0; i < raw.atoms.size(); ++i)
        if (raw.atoms[i].type == core::AtomType::Repeat) {
            ++markerCount;
            markerIndex = i;
        }
    if (markerCount == 1 && cell(raw.atoms[markerIndex].position) != d)
        malformed("marker displacement mismatch");
    if (markerCount != 1 || !realCount) outside("requires one marker and real material");
    for (const auto& [p, node] : actual) {
        (void)p;
        if (node.type == core::AtomType::Repeat) outside("component contains marker");
    }
    for (const auto* molecule : {&raw, &material})
        for (const auto& bond : molecule->bonds)
            if (bond.bits != 1) outside("colored bonds unsupported");
    Cell e{};
    std::int64_t width = 0;
    for (const auto& candidate : directions) {
        const auto w = candidate.first ? d.first / candidate.first : d.second / candidate.second;
        if (w > 0 && shift({}, candidate, w) == d) {
            e = candidate;
            width = w;
            break;
        }
    }
    if (!width) outside("non-axis displacement");
    const auto uv = [e](Cell p) -> Cell {
        return {(e.first + e.second) * p.first + e.second * p.second,
                -e.second * p.first + e.first * p.second};
    };
    std::map<std::int64_t, Cell> rows;
    Graph periodic = rawGraph;
    periodic.erase(d);
    if (!periodic.contains({0, 0})) outside("real origin required");
    for (const auto& [p, node] : periodic) {
        (void)node;
        const auto [u, v] = uv(p);
        const auto [it, inserted] = rows.emplace(v, Cell{u, u});
        if (!inserted) {
            it->second.first = std::min(it->second.first, u);
            it->second.second = std::max(it->second.second, u);
        }
    }
    if (rows.rbegin()->first - rows.begin()->first + 1 != static_cast<std::int64_t>(rows.size()))
        outside("empty material row");
    for (const auto& [v, bounds] : rows) {
        (void)v;
        if (bounds.second - bounds.first >= width) outside("overlapping row envelope");
    }
    if (!connected(rawGraph) || !connected(actual)) outside("disconnected input graph");
    // All periodic geometric neighbors must lie in this or an immediately adjacent copy.
    for (const auto& [p, a] : periodic)
        for (const auto& [q, b] : periodic)
            for (auto t : directions) {
                (void)a;
                (void)b;
                const auto delta =
                    uv({p.first + t.first - q.first, p.second + t.second - q.second});
                if (!delta.second && delta.first % width == 0 &&
                    (delta.first / width < -1 || delta.first / width > 1))
                    outside("nonadjacent periodic neighbor");
            }
    // decode.c swaps the marker with the last atom; it does not stably erase it.
    auto order = raw.atoms;
    std::swap(order[markerIndex], order.back());
    order.pop_back();
    const auto count = order.size();
    if (2 * count > std::vector<std::pair<Cell, Node>>{}.max_size())
        outside("signature representability bound");
    std::vector<std::pair<Cell, Node>> copies;
    copies.reserve(2 * count);
    for (int i = 0; i < 2; ++i)
        for (const auto& atom : order)
            copies.emplace_back(shift(cell(atom.position), d, i), rawGraph.at(cell(atom.position)));
    for (std::size_t i = 0; i < copies.size(); ++i)
        for (std::size_t j = i + 1; j < copies.size(); ++j) {
            const auto t = direction(copies[i].first, copies[j].first);
            if (t >= 0) copies[j].second.bits[(t + 3) % 6] |= copies[i].second.bits[t];
        }
    for (std::size_t i = 0; i < count; ++i)
        periodic.at(cell(order[i].position)).bits = copies[count + i].second.bits;
    const auto lookup = [&](Cell target) -> const Node* {
        const auto [u, v] = uv(target);
        for (const auto& [p, node] : periodic) {
            const auto local = uv(p);
            if (v == local.second && (u - local.first) % width == 0) return &node;
        }
        return nullptr;
    };
    for (const auto& [p, node] : periodic)
        for (std::size_t t = 0; t < 6; ++t) {
            if (!node.bits[t]) continue;
            const auto* other = lookup(add(p, directions[t]));
            if (node.bits[t] != 1 || !other || other->bits[(t + 3) % 6] != node.bits[t])
                outside("nonreciprocal periodic signature");
        }
    Graph two;
    for (int i = 0; i < 2; ++i)
        for (const auto& [p, node] : periodic)
            if (!two.emplace(shift(p, d, i), node).second) outside("overlapping copies");
    if (!connected(two)) outside("disconnected two-monomer graph");
    const auto total = request.maxMonomers * count;
    if (total > Graph{}.max_size()) outside("expected representability bound");
    Graph expected;
    for (std::size_t i = 0; i < request.maxMonomers; ++i)
        for (const auto& [p, signature] : periodic) {
            auto node = signature;
            node.monomer = i;
            if (!expected.emplace(shift(p, d, static_cast<std::int64_t>(i)), node).second)
                outside("overlapping copies");
        }
    for (std::size_t k = 1; k <= request.maxMonomers; ++k) {
        const auto ignored = [&](Cell p) {
            const auto [u, v] = uv(p);
            const auto row = rows.find(v);
            return row != rows.end() &&
                   (u < row->second.first ||
                    u > row->second.second + static_cast<std::int64_t>(k - 1) * width);
        };
        const auto difference = [&](RepeatingGraphDifferenceKind kind, Cell p,
                                    std::optional<core::AtomType> want,
                                    std::optional<core::AtomType> have) {
            return FiniteRepeatingGraphChecked{
                k - 1, RepeatingGraphDifference{kind, k, reported(p), {}, want, have}};
        };
        for (const auto& [p, node] : expected)
            if (node.monomer < k && !actual.contains(p))
                return difference(RepeatingGraphDifferenceKind::MissingAtom, p, node.type, {});
        for (const auto& [p, node] : expected)
            if (node.monomer < k && actual.at(p).type != node.type)
                return difference(RepeatingGraphDifferenceKind::AtomType, p, node.type,
                                  actual.at(p).type);
        for (const auto& [p, node] : expected) {
            if (node.monomer >= k) continue;
            for (std::size_t t = 0; t < 6; ++t) {
                const auto neighbor = add(p, directions[t]);
                const std::uint8_t allowed = ignored(neighbor) ? 0x0f : 0;
                const auto bits = actual.at(p).bits[t];
                if ((bits ^ node.bits[t]) & (0x0f ^ allowed)) {
                    auto result = difference(RepeatingGraphDifferenceKind::BondMask, p, {}, {});
                    auto& diff = *result.firstDifference;
                    diff.neighbor = reported(neighbor);
                    diff.expectedBits = node.bits[t];
                    diff.actualBits = bits;
                    diff.allowedBits = allowed;
                    return result;
                }
            }
        }
        for (const auto& [p, node] : actual) {
            const auto found = expected.find(p);
            if (!ignored(p) && (found == expected.end() || found->second.monomer >= k))
                return difference(RepeatingGraphDifferenceKind::UnexpectedAtom, p, {}, node.type);
        }
    }
    return FiniteRepeatingGraphChecked{request.maxMonomers, {}};
}
} // namespace
FiniteRepeatingGraphResult CheckFiniteRepeatingGraph(const FiniteRepeatingGraphRequest& request) {
    try {
        return check(request);
    } catch (FiniteRepeatingGraphFailure& failure) {
        return std::move(failure);
    } catch (const std::bad_alloc&) {
        return FiniteRepeatingGraphFailure{RepeatingGraphFailureCode::ResourceUnavailable,
                                           "bad_alloc"};
    } catch (const std::length_error&) {
        return FiniteRepeatingGraphFailure{RepeatingGraphFailureCode::ResourceUnavailable,
                                           "length_error"};
    }
}
} // namespace opusoptima::verification
