#include "DemandDrivenChain.hpp"
#include <algorithm>
#include <functional>
#include <limits>
#include <set>
#include <stdexcept>

namespace opusoptima::solvers {
const char* RigidCarrierReasonName(RigidCarrierReason reason) noexcept {
    switch (reason) {
    case RigidCarrierReason::InputHeader:
        return "input-header";
    case RigidCarrierReason::Permissions:
        return "permissions";
    case RigidCarrierReason::SourceGraph:
        return "source-graph";
    case RigidCarrierReason::TargetGraph:
        return "target-graph";
    case RigidCarrierReason::ExactCoverImpossible:
        return "exact-cover-impossible";
    case RigidCarrierReason::SearchNodeCap:
        return "search-node-cap";
    case RigidCarrierReason::Deadline:
        return "deadline";
    case RigidCarrierReason::NoMultiatomCover:
        return "no-multiatom-cover";
    }
    return "unknown";
}
RigidCarrierUnsupported::RigidCarrierUnsupported(RigidCarrierReason reason)
    : std::invalid_argument(std::string("rigid carrier Unsupported: ") +
                            RigidCarrierReasonName(reason)),
      reason_(reason) {}
namespace {
constexpr std::size_t CarrierAtomLimit = 16;
static_assert(std::numeric_limits<unsigned>::digits > CarrierAtomLimit);
using C = core::HexCoord;
[[noreturn]] void unsupported(RigidCarrierReason reason) {
    throw RigidCarrierUnsupported(reason);
}
C turn(C c, int n) {
    while (n--)
        c = core::RotateClockwise(c);
    return c;
}
auto edge(int a, int b) {
    return std::pair{std::min(a, b), std::max(a, b)};
}
bool StaticCopyOnly(const io::PuzzleData& p) {
    return p.inputs.size() > 8 || p.outputs.size() > 4;
}
// This necessary check performs no pose search or chemistry inference.
bool StaticCopyPossible(const io::PuzzleData& p) {
    bool molecular = false;
    for (const auto& target : p.outputs) {
        molecular |= target.atoms.size() > 1;
        if (!std::any_of(p.inputs.begin(), p.inputs.end(), [&](const auto& feed) {
                if (feed.atoms.size() != target.atoms.size() ||
                    feed.bonds.size() != target.bonds.size())
                    return false;
                for (const auto& atom : target.atoms) {
                    const auto count = [&](const auto& m) {
                        return std::count_if(m.atoms.begin(), m.atoms.end(),
                                             [&](const auto& a) { return a.type == atom.type; });
                    };
                    if (count(feed) != count(target)) return false;
                }
                return true;
            }))
            return false;
    }
    return molecular;
}
void CarrierHeader(const io::PuzzleData& p) {
    if (p.version != 3 || p.productionInfo || p.outputMultiplier != 1 || p.inputs.empty() ||
        p.inputs.size() > 16 || p.outputs.empty() || p.outputs.size() > 8)
        unsupported(RigidCarrierReason::InputHeader);
    if (!(p.permissionBits & 1) || (StaticCopyOnly(p) && !(p.permissionBits & (1ULL << 25))))
        unsupported(RigidCarrierReason::Permissions);
}
std::set<std::pair<int, int>> CarrierGraph(const io::Molecule& m, std::size_t limit,
                                           RigidCarrierReason reason) {
    std::set<std::pair<int, int>> edges, cells;
    if (m.atoms.empty() || m.atoms.size() > limit) unsupported(reason);
    for (const auto& a : m.atoms)
        if (!cells.emplace(a.position.q, a.position.r).second ||
            std::max({std::abs(a.position.q - m.atoms[0].position.q),
                      std::abs(a.position.r - m.atoms[0].position.r),
                      std::abs(a.position.q + a.position.r - m.atoms[0].position.q -
                               m.atoms[0].position.r)}) > 8)
            unsupported(reason);
    for (const auto& b : m.bonds) {
        int a = -1, c = -1;
        for (int i = 0; i < static_cast<int>(m.atoms.size()); ++i) {
            if (m.atoms[i].position == b.from) a = i;
            if (m.atoms[i].position == b.to) c = i;
        }
        int r = 0;
        while (r < 6 && turn({1, 0}, r) != core::Subtract(b.to, b.from))
            ++r;
        if (b.bits != 1 || a < 0 || c < 0 || r == 6 || !edges.insert(edge(a, c)).second)
            unsupported(reason);
    }
    std::set<int> reached{0};
    for (std::size_t pass = 0; pass < m.atoms.size(); ++pass)
        for (auto [a, b] : edges)
            if (reached.contains(a) || reached.contains(b)) {
                reached.insert(a);
                reached.insert(b);
            }
    if (reached.size() != m.atoms.size()) unsupported(reason);
    return edges;
}
using Edges = std::set<std::pair<int, int>>;
struct Tile {
    int input, rotation;
    C offset;
    unsigned mask;
    std::vector<int> mapping;
    Edges inherited, cuts;
};
void CheckCarrierDeadline(std::chrono::steady_clock::time_point deadline) {
    if (std::chrono::steady_clock::now() >= deadline) unsupported(RigidCarrierReason::Deadline);
}
// At most inputs * 6 * target atoms placements; visitor may stop at the first match.
template <class Visitor>
bool VisitCarrierTiles(const io::PuzzleData& p, const io::Molecule& target, const Edges& edges,
                       const std::vector<Edges>& sourceEdges,
                       std::chrono::steady_clock::time_point deadline, bool molecularOnly,
                       Visitor visit, std::size_t* copyNodes = nullptr) {
    for (int in = 0; in < static_cast<int>(p.inputs.size()); ++in) {
        const auto& feed = p.inputs[in];
        if (molecularOnly && feed.atoms.size() == 1) continue;
        if (copyNodes &&
            (feed.atoms.size() != target.atoms.size() || sourceEdges[in].size() != edges.size()))
            continue;
        for (int r = 0; r < 6; ++r)
            for (const auto& anchor : target.atoms) {
                CheckCarrierDeadline(deadline);
                if (copyNodes && ++*copyNodes > 20000)
                    unsupported(RigidCarrierReason::SearchNodeCap);
                Tile t{in, r,  core::Subtract(anchor.position, turn(feed.atoms[0].position, r)),
                       0,  {}, {},
                       {}};
                bool fits = true;
                for (const auto& a : feed.atoms) {
                    const C cell = core::Add(t.offset, turn(a.position, r));
                    int found = -1;
                    for (int i = 0; i < static_cast<int>(target.atoms.size()); ++i)
                        if (target.atoms[i].position == cell) found = i;
                    if (found < 0) {
                        fits = false;
                        break;
                    }
                    const int type = static_cast<int>(a.type),
                              wanted = static_cast<int>(target.atoms[found].type);
                    if (type != wanted && (copyNodes || !(wanted == 1 && type >= 2 && type <= 5 &&
                                                          (p.permissionBits & (1ULL << 12))))) {
                        fits = false;
                        break;
                    }
                    t.mask |= 1U << found;
                    t.mapping.push_back(found);
                }
                if (!fits) continue;
                for (auto [a, b] : sourceEdges[in]) {
                    const auto mapped = edge(t.mapping[a], t.mapping[b]);
                    if (edges.contains(mapped))
                        t.inherited.insert(mapped);
                    else if (!copyNodes && (p.permissionBits & (1ULL << 9)))
                        t.cuts.insert(mapped);
                    else {
                        fits = false;
                        break;
                    }
                }
                if (fits && visit(std::move(t))) return true;
            }
    }
    return false;
}

io::SolutionData CompileStaticCopies(const io::PuzzleData& p, const std::vector<Edges>& sourceEdges,
                                     const std::vector<Edges>& targetEdges,
                                     std::chrono::steady_clock::time_point deadline) {
    if (!StaticCopyPossible(p)) unsupported(RigidCarrierReason::InputHeader);
    io::SolutionData result;
    result.puzzleName = p.name;
    result.solutionName = "Bounded static complete-input copies";
    auto object = [&](const char* name, C cell, int rotation, int index) {
        io::SolutionObject o;
        o.typeName = name;
        o.position = cell;
        o.bodyData = {1, rotation, index, 0, static_cast<int>(result.objects.size())};
        result.objects.push_back(o);
    };
    std::size_t nodes = 0;
    for (int out = 0; out < static_cast<int>(p.outputs.size()); ++out) {
        const auto& target = p.outputs[out];
        // Normalize each region so arbitrary serialized origins cannot overlap stations.
        const C station = core::Subtract(C{out * 40, 0}, target.atoms[0].position);
        const bool matched = VisitCarrierTiles(
            p, target, targetEdges[out], sourceEdges, deadline, false,
            [&](Tile&& tile) {
                if (tile.mapping.size() != target.atoms.size() ||
                    tile.inherited != targetEdges[out] || !tile.cuts.empty())
                    return false;
                object("input", core::Add(station, tile.offset), tile.rotation, tile.input);
                object("out-std", station, 0, out);
                return true;
            },
            &nodes);
        if (!matched) unsupported(RigidCarrierReason::ExactCoverImpossible);
    }
    object("arm1", {20, 20}, 0, 0);
    result.objects.back().instructions = {{'O', 96}};
    CheckCarrierDeadline(deadline);
    return result;
}

// This size/type check admits uncertainty without enumerating partition placements.
bool PartitionPossible(const io::PuzzleData& p) {
    if (p.outputs.size() < 2) return false;
    std::size_t total = 0;
    for (const auto& target : p.outputs)
        total += target.atoms.size();
    return std::all_of(p.outputs.begin(), p.outputs.end(), [&](const auto& target) {
        return std::any_of(p.inputs.begin(), p.inputs.end(), [&](const auto& feed) {
            if (feed.atoms.size() < 2 || feed.atoms.size() > total) return false;
            for (const auto& atom : target.atoms) {
                const auto count = [&](const auto& molecule) {
                    return std::count_if(molecule.atoms.begin(), molecule.atoms.end(),
                                         [&](const auto& a) { return a.type == atom.type; });
                };
                if (count(feed) < count(target)) return false;
            }
            return true;
        });
    });
}

io::SolutionData CompileCarrierPartition(const io::PuzzleData& p,
                                         const std::vector<Edges>& sourceEdges,
                                         const std::vector<Edges>& targetEdges,
                                         std::chrono::steady_clock::time_point deadline) {
    if (!PartitionPossible(p)) unsupported(RigidCarrierReason::ExactCoverImpossible);
    if (!(p.permissionBits & (1ULL << 9))) unsupported(RigidCarrierReason::Permissions);
    struct Piece {
        int output, rotation;
        C offset;
        unsigned mask;
        Edges kept;
    };
    struct Batch {
        int input;
        std::vector<Piece> pieces;
        Edges kept;
    };
    std::vector<Batch> batches;
    std::vector<int> inputs;
    for (int in = 0; in < static_cast<int>(p.inputs.size()); ++in)
        if (p.inputs[in].atoms.size() > 1) inputs.push_back(in);
    std::stable_sort(inputs.begin(), inputs.end(), [&](int a, int b) {
        return p.inputs[a].atoms.size() < p.inputs[b].atoms.size();
    });
    std::size_t nodes = 0;
    auto charge = [&] {
        CheckCarrierDeadline(deadline);
        if (++nodes > 20000) unsupported(RigidCarrierReason::SearchNodeCap);
    };
    std::function<bool(unsigned, bool)> ports;
    std::function<bool(Batch&, unsigned, unsigned, unsigned, bool)> cover;
    cover = [&](Batch& batch, unsigned used, unsigned remaining, unsigned required,
                bool partitioned) {
        charge();
        const auto& feed = p.inputs[batch.input];
        if (used == (1U << feed.atoms.size()) - 1) {
            if (remaining & required) return false;
            batches.push_back(batch);
            if (ports(remaining, partitioned || batch.pieces.size() > 1)) return true;
            batches.pop_back();
            return false;
        }
        int first = 0;
        while (used & (1U << first))
            ++first;
        for (int out = 0; out < static_cast<int>(p.outputs.size()); ++out) {
            if (!(remaining & (1U << out))) continue;
            const auto& target = p.outputs[out];
            if (target.atoms.size() > feed.atoms.size()) continue;
            for (int r = 0; r < 6; ++r)
                for (const auto& anchor : target.atoms) {
                    charge();
                    Piece piece{
                        out,
                        r,
                        core::Subtract(feed.atoms[first].position, turn(anchor.position, r)),
                        0,
                        {}};
                    std::vector<int> mapping;
                    bool fits = true;
                    for (const auto& atom : target.atoms) {
                        const auto cell = core::Add(piece.offset, turn(atom.position, r));
                        int found = -1;
                        for (int a = 0; a < static_cast<int>(feed.atoms.size()); ++a)
                            if (feed.atoms[a].position == cell && feed.atoms[a].type == atom.type)
                                found = a;
                        if (found < 0 || (used & (1U << found))) {
                            fits = false;
                            break;
                        }
                        piece.mask |= 1U << found;
                        mapping.push_back(found);
                    }
                    if (!fits) continue;
                    for (auto [a, b] : targetEdges[out]) {
                        const auto inherited = edge(mapping[a], mapping[b]);
                        if (!sourceEdges[batch.input].contains(inherited)) {
                            fits = false;
                            break;
                        }
                        piece.kept.insert(inherited);
                    }
                    if (!fits) continue;
                    batch.pieces.push_back(piece);
                    batch.kept.insert(piece.kept.begin(), piece.kept.end());
                    if (cover(batch, used | piece.mask, remaining & ~(1U << out), required,
                              partitioned))
                        return true;
                    for (auto bond : piece.kept)
                        batch.kept.erase(bond);
                    batch.pieces.pop_back();
                }
        }
        return false;
    };
    ports = [&](unsigned remaining, bool partitioned) {
        charge();
        if (!remaining) return partitioned;
        unsigned required = 1;
        while (!(remaining & required))
            required <<= 1;
        std::size_t atoms = 0;
        for (std::size_t out = 0; out < p.outputs.size(); ++out)
            if (remaining & (1U << out)) atoms += p.outputs[out].atoms.size();
        for (int in : inputs) {
            if (p.inputs[in].atoms.size() > atoms) continue;
            Batch batch{in, {}, {}};
            if (cover(batch, 0, remaining, required, partitioned)) return true;
        }
        return false;
    };
    if (!ports((1U << p.outputs.size()) - 1, false))
        unsupported(RigidCarrierReason::ExactCoverImpossible);
    io::SolutionData result;
    result.puzzleName = p.name;
    result.solutionName = "Bounded static whole-molecule partition";
    auto object = [&](const char* name, C cell, int rotation = 0, int index = 0) {
        io::SolutionObject o;
        o.typeName = name;
        o.position = cell;
        o.bodyData = {1, rotation, index, 0, static_cast<int>(result.objects.size())};
        result.objects.push_back(o);
    };
    for (std::size_t i = 0; i < batches.size(); ++i) {
        CheckCarrierDeadline(deadline);
        const auto& batch = batches[i];
        const auto& feed = p.inputs[batch.input];
        const auto offset = core::Subtract(C{static_cast<int>(i) * 40, 0}, feed.atoms[0].position);
        object("input", offset, 0, batch.input);
        for (auto [a, b] : sourceEdges[batch.input])
            if (!batch.kept.contains(edge(a, b))) {
                const auto from = feed.atoms[a].position, to = feed.atoms[b].position;
                int r = 0;
                while (r < 6 && turn({1, 0}, r) != core::Subtract(to, from))
                    ++r;
                object("unbonder", core::Add(offset, from), r);
            }
        for (const auto& piece : batch.pieces)
            object("out-std", core::Add(offset, piece.offset), piece.rotation, piece.output);
    }
    object("arm1", {20, 20});
    result.objects.back().instructions = {{'O', 96}};
    CheckCarrierDeadline(deadline);
    return result;
}
} // namespace
RigidCarrierPreflight PreflightRigidCarrier(const io::PuzzleData& p,
                                            std::chrono::steady_clock::time_point deadline) {
    try {
        CheckCarrierDeadline(deadline);
        CarrierHeader(p);
        std::vector<Edges> sourceEdges, targetEdges;
        for (const auto& m : p.inputs)
            sourceEdges.push_back(
                CarrierGraph(m, CarrierAtomLimit, RigidCarrierReason::SourceGraph));
        for (const auto& m : p.outputs)
            targetEdges.push_back(
                CarrierGraph(m, CarrierAtomLimit, RigidCarrierReason::TargetGraph));
        if (StaticCopyOnly(p)) {
            if (!StaticCopyPossible(p)) unsupported(RigidCarrierReason::InputHeader);
            CheckCarrierDeadline(deadline);
            return {RigidCarrierPreflight::Status::Unknown, std::nullopt};
        }
        // A singleton-first exact cover says nothing about other legal molecular tiles.
        if (p.outputs.size() <= 2)
            for (std::size_t out = 0; out < p.outputs.size(); ++out)
                if (VisitCarrierTiles(p, p.outputs[out], targetEdges[out], sourceEdges, deadline,
                                      true, [](Tile&&) { return true; }))
                    return {RigidCarrierPreflight::Status::Unknown, std::nullopt};
        CheckCarrierDeadline(deadline);
        if (PartitionPossible(p)) {
            if (!(p.permissionBits & (1ULL << 9))) unsupported(RigidCarrierReason::Permissions);
            return {RigidCarrierPreflight::Status::Unknown, std::nullopt};
        }
        return {RigidCarrierPreflight::Status::Unsupported, RigidCarrierReason::NoMultiatomCover};
    } catch (const RigidCarrierUnsupported& error) {
        return {RigidCarrierPreflight::Status::Unsupported, error.reason()};
    }
}

RigidCarrierResult CompileRigidCarrierWithFamily(const io::PuzzleData& p,
                                                 std::chrono::steady_clock::time_point deadline) {
    auto check = [&] { CheckCarrierDeadline(deadline); };
    check();
    CarrierHeader(p);
    std::vector<Edges> sourceEdges, targetEdges;
    for (const auto& m : p.inputs)
        sourceEdges.push_back(CarrierGraph(m, CarrierAtomLimit, RigidCarrierReason::SourceGraph));
    for (const auto& m : p.outputs)
        targetEdges.push_back(CarrierGraph(m, CarrierAtomLimit, RigidCarrierReason::TargetGraph));
    if (StaticCopyOnly(p))
        return {CompileStaticCopies(p, sourceEdges, targetEdges, deadline),
                RigidCarrierFamily::StaticCopy};
    if (p.outputs.size() > 2)
        return {CompileCarrierPartition(p, sourceEdges, targetEdges, deadline),
                RigidCarrierFamily::StaticPartition};
    io::SolutionData result;
    result.puzzleName = p.name;
    result.solutionName = "Experimental bounded rigid molecular carrier layout";
    auto object = [&](const char* name, C cell, int rotation = 0, int index = 0) {
        io::SolutionObject o;
        o.typeName = name;
        o.position = cell;
        o.bodyData = {1, rotation, index, 0, static_cast<int>(result.objects.size())};
        result.objects.push_back(o);
    };
    std::vector<std::vector<Tile>> tiles(p.outputs.size());
    std::vector<std::vector<int>> chosen(p.outputs.size());
    std::set<std::pair<int, int>> targetCells;
    for (std::size_t out = 0; out < p.outputs.size(); ++out) {
        for (const auto& atom : p.outputs[out].atoms) {
            const auto world = core::Add(C{static_cast<int>(out) * 40, 0}, atom.position);
            if (!targetCells.emplace(world.q, world.r).second)
                unsupported(RigidCarrierReason::ExactCoverImpossible);
        }
        VisitCarrierTiles(p, p.outputs[out], targetEdges[out], sourceEdges, deadline, false,
                          [&](Tile&& tile) {
                              tiles[out].push_back(std::move(tile));
                              return false;
                          });
        auto savings = [&](const Tile& tile) {
            int conversions = 0;
            const auto& feed = p.inputs[tile.input];
            for (std::size_t atom = 0; atom < feed.atoms.size(); ++atom)
                conversions +=
                    feed.atoms[atom].type != p.outputs[out].atoms[tile.mapping[atom]].type;
            // Bonders, unbonders and calcifiers have equal device cost.
            return static_cast<int>(tile.inherited.size()) - static_cast<int>(tile.cuts.size()) -
                   conversions;
        };
        std::stable_sort(tiles[out].begin(), tiles[out].end(), [&](const Tile& a, const Tile& b) {
            const auto left = savings(a) * static_cast<int>(b.mapping.size());
            const auto right = savings(b) * static_cast<int>(a.mapping.size());
            if (left != right) return left > right;
            if (a.mapping.size() != b.mapping.size()) return a.mapping.size() > b.mapping.size();
            return a.inherited.size() > b.inherited.size();
        });
        check();
    }
    if (PartitionPossible(p) && std::any_of(tiles.begin(), tiles.end(),
                                            [](const auto& choices) { return choices.empty(); }))
        return {CompileCarrierPartition(p, sourceEdges, targetEdges, deadline),
                RigidCarrierFamily::StaticPartition};
    const bool molecularTileAvailable =
        std::any_of(tiles.begin(), tiles.end(), [&](const auto& choices) {
            return std::any_of(choices.begin(), choices.end(), [&](const auto& tile) {
                return p.inputs[tile.input].atoms.size() > 1;
            });
        });
    std::size_t nodes = 0;
    bool permissionRejected = false, singletonRejected = false;
    const bool canBond = (p.permissionBits & (1ULL << 8)) != 0;
    std::function<bool(std::size_t, unsigned, bool, const Edges&)> cover;
    cover = [&](std::size_t out, unsigned used, bool molecular, const Edges& supplied) {
        check();
        if (++nodes > 20000) unsupported(RigidCarrierReason::SearchNodeCap);
        const unsigned full = (1U << p.outputs[out].atoms.size()) - 1;
        if (used == full) {
            if (!canBond && supplied != targetEdges[out]) {
                permissionRejected = true;
                return false;
            }
            if (out + 1 == p.outputs.size()) {
                if (!molecular) {
                    if (!molecularTileAvailable) unsupported(RigidCarrierReason::NoMultiatomCover);
                    singletonRejected = true;
                    return false;
                }
                return true;
            }
            return cover(out + 1, 0, molecular, Edges{});
        }
        unsigned bit = 1;
        while (used & bit)
            bit <<= 1;
        for (int i = 0; i < static_cast<int>(tiles[out].size()); ++i) {
            const auto& t = tiles[out][i];
            if (!(t.mask & bit) || (t.mask & used)) continue;
            auto next = supplied;
            next.insert(t.inherited.begin(), t.inherited.end());
            chosen[out].push_back(i);
            if (cover(out, used | t.mask, molecular || p.inputs[t.input].atoms.size() > 1, next))
                return true;
            chosen[out].pop_back();
        }
        return false;
    };
    if (!cover(0, 0, false, Edges{})) {
        if (permissionRejected) unsupported(RigidCarrierReason::Permissions);
        if (singletonRejected) unsupported(RigidCarrierReason::NoMultiatomCover);
        unsupported(RigidCarrierReason::ExactCoverImpossible);
    }
    for (int out = 0; out < static_cast<int>(p.outputs.size()); ++out) {
        check();
        const auto& target = p.outputs[out];
        const C station{out * 40, 0};
        Edges supplied;
        for (int i : chosen[out]) {
            const auto& t = tiles[out][i];
            const auto& feed = p.inputs[t.input];
            object("input", core::Add(station, t.offset), t.rotation, t.input);
            for (int a = 0; a < static_cast<int>(feed.atoms.size()); ++a)
                if (feed.atoms[a].type != target.atoms[t.mapping[a]].type)
                    object("glyph-calcification",
                           core::Add(station, target.atoms[t.mapping[a]].position));
            supplied.insert(t.inherited.begin(), t.inherited.end());
            for (auto [a, b] : t.cuts) {
                const auto from = target.atoms[a].position, to = target.atoms[b].position;
                int r = 0;
                while (r < 6 && turn({1, 0}, r) != core::Subtract(to, from))
                    ++r;
                object("unbonder", core::Add(station, from), r);
            }
        }
        for (auto [a, b] : targetEdges[out])
            if (!supplied.contains(edge(a, b))) {
                const auto from = target.atoms[a].position, to = target.atoms[b].position;
                int r = 0;
                while (r < 6 && turn({1, 0}, r) != core::Subtract(to, from))
                    ++r;
                object("bonder", core::Add(station, from), r);
            }
        object("out-std", station, 0, out);
    }
    object("arm1", {20, 20});
    result.objects.back().instructions = {{'O', 96}};
    check();
    return {std::move(result), RigidCarrierFamily::WholeMolecule};
}
io::SolutionData CompileRigidCarrier(const io::PuzzleData& p,
                                     std::chrono::steady_clock::time_point deadline) {
    return CompileRigidCarrierWithFamily(p, deadline).solution;
}
} // namespace opusoptima::solvers
