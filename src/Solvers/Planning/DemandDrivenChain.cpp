#include "DemandDrivenChain.hpp"
#include <algorithm>
#include <array>
#include <functional>
#include <map>
#include <set>
#include <stdexcept>
#include <utility>
#include <vector>

namespace opusoptima::solvers {
io::SolutionData CompileDemandDrivenChain(const io::PuzzleData& p) {
    constexpr auto required = (1ULL << 0) | (1ULL << 9) | (1ULL << 12) | (1ULL << 16);
    if (p.version != 3 || p.productionInfo || p.outputMultiplier != 1 ||
        (p.permissionBits & required) != required || p.inputs.empty() || p.outputs.size() != 2)
        throw std::invalid_argument(
            "demand chain requires water and two bonded life/death consumers");
    int water = -1;
    for (int i = 0; i < static_cast<int>(p.inputs.size()); ++i)
        if (p.inputs[i].atoms.size() == 1 && p.inputs[i].bonds.empty() &&
            p.inputs[i].atoms[0].type == core::AtomType::Water)
            water = i;
    int indices[2]{-1, -1};
    core::HexCoord origins[2];
    for (int i = 0; i < 2; ++i) {
        const auto& m = p.outputs[i];
        if (m.atoms.size() != 2 || m.bonds.size() != 1 || m.bonds[0].bits != 1) break;
        for (int j = 0; j < 2; ++j) {
            const auto& a = m.atoms[j];
            const auto& b = m.atoms[1 - j];
            const int slot = a.type == core::AtomType::Vitae  ? 0
                             : a.type == core::AtomType::Mors ? 1
                                                              : -1;
            const auto& bond = m.bonds[0];
            if (slot >= 0 && b.type == core::AtomType::Water && b.position.q == a.position.q &&
                b.position.r == a.position.r + (slot == 0 ? 1 : -1) &&
                ((bond.from == a.position && bond.to == b.position) ||
                 (bond.to == a.position && bond.from == b.position))) {
                indices[slot] = i;
                origins[slot] = a.position;
            }
        }
    }
    if (water < 0 || indices[0] < 0 || indices[1] < 0)
        throw std::invalid_argument("demand chain consumer interface unsupported");
    io::SolutionData result;
    result.puzzleName = p.name;
    result.solutionName = "Demand-driven two-port calcify animismus chain";
    auto object = [&](const char* type, int q, int r, int rotation = 0, int index = 0) -> auto& {
        io::SolutionObject o;
        o.typeName = type;
        o.position = {q, r};
        o.bodyData = {1, rotation, index, 0, static_cast<int>(result.objects.size())};
        result.objects.push_back(o);
        return result.objects.back();
    };
    const auto origin = p.inputs[water].atoms[0].position;
    for (auto cell : {core::HexCoord{-1, 0}, {2, 0}, {0, 2}, {1, -2}})
        object("input", cell.q - origin.q, cell.r - origin.r, 0, water);
    object("glyph-calcification", 0, 0);
    object("glyph-calcification", 1, 0);
    object("glyph-life-and-death", 0, 0);
    object("bonder", 0, 1, 1);
    object("bonder", 1, -2, 1);
    object("out-std", -origins[0].q, 1 - origins[0].r, 0, indices[0]);
    object("out-std", 1 - origins[1].q, -1 - origins[1].r, 0, indices[1]);
    for (int side = 0; side < 2; ++side) {
        auto& arm = object("arm1", side ? 2 : -1, side ? -1 : 1, side ? 1 : 4);
        for (int batch = 0; batch < 6; ++batch) {
            const int start = 1 + batch * 12;
            arm.instructions.insert(
                arm.instructions.end(),
                {{'G', start}, {'r', start + 1}, {'g', start + 2}, {'R', start + 3}});
        }
        arm.instructions.push_back({'O', 73});
    }
    return result;
}
} // namespace opusoptima::solvers

namespace opusoptima::solvers {
namespace {
template <class Feed, class Glyph> void EmitStaticCascade(int depth, Feed feed, Glyph glyph) {
    for (int q = 0; q <= depth; ++q)
        feed(core::HexCoord{q, 0});
    // Downstream and rightmost glyphs take priority at shared reagent cells.
    for (int r = depth - 1; r >= 0; --r)
        for (int q = depth - r - 1; q >= 0; --q)
            glyph(core::HexCoord{q, r});
}
template <class Visit> void VisitStaticCascadeFootprint(int depth, Visit visit) {
    // The product at (0,depth) belongs to the output; every other cell is private.
    for (int r = 0; r < depth; ++r)
        for (int q = 0; q <= depth - r; ++q)
            visit(core::HexCoord{q, r});
}
StaticReactionResult CompileConsumersWithFamily(const io::PuzzleData& p,
                                                std::chrono::steady_clock::time_point deadline);
} // namespace
io::SolutionData CompileStaticReaction(const io::PuzzleData& p) {
    return CompileStaticReaction(p, std::chrono::steady_clock::time_point::max());
}
io::SolutionData CompileStaticReaction(const io::PuzzleData& p,
                                       std::chrono::steady_clock::time_point deadline) {
    return CompileStaticReactionWithFamily(p, deadline).solution;
}
StaticReactionResult CompileStaticReactionWithFamily(
    const io::PuzzleData& p, std::chrono::steady_clock::time_point deadline) {
    if (deadline != std::chrono::steady_clock::time_point::max() &&
        std::chrono::steady_clock::now() >= deadline)
        throw std::invalid_argument("static reaction Unsupported: deadline");
    if (p.outputs.size() != 1) return CompileConsumersWithFamily(p, deadline);
    if (p.version != 3 || p.productionInfo || p.outputMultiplier != 1 ||
        !(p.permissionBits & (1ULL << 15)) || p.inputs.empty() || p.inputs.size() > 8 ||
        p.outputs[0].atoms.size() != 1 || !p.outputs[0].bonds.empty())
        throw std::invalid_argument(
            "static purification requires an ordinary singleton metal target");
    const int target = static_cast<int>(p.outputs[0].atoms[0].type);
    if (target < 7 || target >= 12)
        throw std::invalid_argument("static purification requires a higher metal target");
    int source = -1;
    for (int i = 0; i < static_cast<int>(p.inputs.size()); ++i) {
        const auto& input = p.inputs[i];
        if (input.atoms.size() != 1 || !input.bonds.empty())
            throw std::invalid_argument(
                "static purification requires genuine singleton metal feeds");
        const int type = static_cast<int>(input.atoms[0].type);
        if (type < 7 || type > 12)
            throw std::invalid_argument("static purification requires metal feeds");
        if (type > target &&
            (source < 0 || type < static_cast<int>(p.inputs[source].atoms[0].type)))
            source = i;
    }
    if (source < 0)
        throw std::invalid_argument("static purification requires a supplied lower metal");
    const int depth = static_cast<int>(p.inputs[source].atoms[0].type) - target;
    io::SolutionData result;
    result.puzzleName = p.name;
    result.solutionName = "Static singleton metal purification";
    auto object = [&](const char* type, core::HexCoord cell, int index = 0) {
        io::SolutionObject o;
        o.typeName = type;
        o.position = cell;
        o.bodyData = {1, 0, index, 0, static_cast<int>(result.objects.size())};
        result.objects.push_back(o);
    };
    EmitStaticCascade(
        depth,
        [&](core::HexCoord cell) {
            object("input", core::Subtract(cell, p.inputs[source].atoms[0].position), source);
        },
        [&](core::HexCoord cell) { object("glyph-purification", cell); });
    object("out-std", core::Subtract({0, depth}, p.outputs[0].atoms[0].position));
    return {std::move(result), StaticReactionFamily::Purification};
}

std::optional<StaticEmbeddingSuppressionReason> StaticEmbeddingSuppressionAfterReaction(
    const io::PuzzleData& p, StaticReactionFamily family, const io::VerificationReport& report) {
    if (family != StaticReactionFamily::Purification || !report.Passed() ||
        report.authority != io::VerificationAuthority::LegacyGate || p.version != 3 ||
        p.productionInfo || p.outputMultiplier != 1 || !(p.permissionBits & (1ULL << 15)) ||
        p.inputs.empty() || p.inputs.size() > 8 || p.outputs.size() != 1 ||
        p.outputs[0].atoms.size() != 1 || !p.outputs[0].bonds.empty())
        return std::nullopt;
    const int target = static_cast<int>(p.outputs[0].atoms[0].type);
    if (target < 7 || target >= 12) return std::nullopt;
    bool lowerFeed = false;
    for (const auto& input : p.inputs) {
        if (input.atoms.size() != 1 || !input.bonds.empty()) return std::nullopt;
        const int source = static_cast<int>(input.atoms[0].type);
        if (source < 7 || source > 12 || source == target) return std::nullopt;
        lowerFeed |= source > target;
    }
    // Both compilers choose the nearest lower metal, keeping the first index on ties,
    // and emit the same ordered cascade. With one product and no direct target feed,
    // embedding only translates that triangle and adds an idle arm.
    if (lowerFeed) return StaticEmbeddingSuppressionReason::AcceptedSingletonPurification;
    return std::nullopt;
}

namespace {
io::SolutionData CompileLegacyDemandDrivenConsumers(const io::PuzzleData& p) {
    constexpr auto required = (1ULL << 0) | (1ULL << 8) | (1ULL << 16);
    if (p.version != 3 || p.productionInfo || p.outputMultiplier != 1 ||
        (p.permissionBits & required) != required || p.inputs.empty() || p.outputs.size() != 2)
        throw std::invalid_argument(
            "static consumers require ordinary arm, bonding and animismus permissions");
    int water = -1, salt = -1;
    for (int i = 0; i < static_cast<int>(p.inputs.size()); ++i) {
        const auto& input = p.inputs[i];
        if (input.atoms.size() != 1 || !input.bonds.empty())
            throw std::invalid_argument("static station feeds must be singleton Water or Salt");
        if (input.atoms[0].type == core::AtomType::Water)
            water = i;
        else if (input.atoms[0].type == core::AtomType::Salt)
            salt = i;
        else
            throw std::invalid_argument("unsupported static station feed material");
    }
    if (water < 0 || (salt < 0 && !(p.permissionBits & (1ULL << 12))))
        throw std::invalid_argument("static consumers require Water and a permitted Salt supply");
    using C = core::HexCoord;
    auto turn = [](C cell, int count) {
        while (count--)
            cell = core::RotateClockwise(cell);
        return cell;
    };
    struct Match {
        int output{-1}, turns{};
        C origin{};
        int atoms{};
    } matches[2];
    const C shape[2][4] = {{{0, 0}, {-1, 0}, {1, 0}, {1, 1}}, {{0, 0}, {-1, 0}, {1, 0}, {-1, -1}}};
    for (int side = 0; side < 2; ++side) {
        for (int output = 0; output < 2; ++output) {
            const auto& m = p.outputs[output];
            if ((m.atoms.size() != 3 && m.atoms.size() != 4) ||
                m.bonds.size() + 1 != m.atoms.size())
                continue;
            for (const auto& center : m.atoms) {
                if (center.type != (side ? core::AtomType::Mors : core::AtomType::Vitae)) continue;
                for (int rotation = 0; rotation < 6; ++rotation) {
                    C cells[4];
                    bool valid = true;
                    for (int atom = 0; atom < static_cast<int>(m.atoms.size()); ++atom) {
                        cells[atom] = core::Add(center.position, turn(shape[side][atom], rotation));
                        const auto type = atom == 0   ? center.type
                                          : atom == 3 ? core::AtomType::Salt
                                                      : core::AtomType::Water;
                        int found = 0;
                        for (const auto& a : m.atoms)
                            found += a.position == cells[atom] && a.type == type;
                        valid &= found == 1;
                    }
                    const int edges[3][2] = {{0, 1}, {0, 2}, {side ? 1 : 2, 3}};
                    for (int e = 0; e < static_cast<int>(m.bonds.size()); ++e) {
                        const auto& edge = edges[e];
                        int found = 0;
                        for (const auto& b : m.bonds)
                            found += b.bits == 1 &&
                                     ((b.from == cells[edge[0]] && b.to == cells[edge[1]]) ||
                                      (b.to == cells[edge[0]] && b.from == cells[edge[1]]));
                        valid &= found == 1;
                    }
                    if (valid)
                        matches[side] = {output, rotation, center.position,
                                         static_cast<int>(m.atoms.size())};
                }
            }
        }
        if (matches[side].output < 0)
            throw std::invalid_argument(
                "consumer graph requires unsupported material, edge, pivot or buffer layout");
    }
    if (matches[0].output == matches[1].output)
        throw std::invalid_argument("static consumers require distinct output graphs");
    io::SolutionData result;
    result.puzzleName = p.name;
    result.solutionName = "Experimental graph-matched static reaction station";
    auto object = [&](const char* type, C cell, int rotation = 0, int index = 0) -> auto& {
        io::SolutionObject o;
        o.typeName = type;
        o.position = cell;
        o.bodyData = {1, rotation, index, 0, static_cast<int>(result.objects.size())};
        result.objects.push_back(o);
        return result.objects.back();
    };
    auto source = [&](C cell, bool needsSalt) {
        const auto index = needsSalt && salt >= 0 ? salt : water;
        object("input", core::Subtract(cell, p.inputs[index].atoms[0].position), 0, index);
        if (needsSalt && salt < 0) object("glyph-calcification", cell);
    };
    source({0, 0}, true);
    source({1, 0}, true);
    object("glyph-life-and-death", {0, 0});
    for (int side = 0; side < 2; ++side) {
        const C port = side ? C{1, -1} : C{0, 1};
        C cells[4];
        for (int atom = 0; atom < 4; ++atom)
            cells[atom] = core::Add(port, shape[side][atom]);
        for (int atom = 1; atom < matches[side].atoms; ++atom)
            source(cells[atom], atom == 3);
        const int edges[3][2] = {{0, 1}, {0, 2}, {side ? 1 : 2, 3}};
        for (int e = 0; e < matches[side].atoms - 1; ++e) {
            const auto& edge = edges[e];
            const C delta = core::Subtract(cells[edge[1]], cells[edge[0]]);
            int rotation = 0;
            while (rotation < 6 && turn({1, 0}, rotation) != delta)
                ++rotation;
            object("bonder", cells[edge[0]], rotation);
        }
        const auto& match = matches[side];
        const int rotation = (6 - match.turns) % 6;
        object("out-std", core::Subtract(port, turn(match.origin, rotation)), rotation,
               match.output);
    }
    auto& arm = object("arm1", {4, 3});
    arm.instructions = {{'O', 96}};
    return result;
}

io::SolutionData CompileOrdinaryAnimismusPair(const io::PuzzleData& p,
                                              std::chrono::steady_clock::time_point deadline) {
    auto checkTime = [&] {
        if (deadline != std::chrono::steady_clock::time_point::max() &&
            std::chrono::steady_clock::now() >= deadline)
            throw std::invalid_argument("static consumers Unsupported: deadline");
    };
    checkTime();
    constexpr auto required = (1ULL << 0) | (1ULL << 8) | (1ULL << 16) | (1ULL << 25);
    if (p.version != 3 || p.productionInfo || p.outputMultiplier != 1 ||
        (p.permissionBits & required) != required || p.inputs.empty() || p.inputs.size() > 8 ||
        p.outputs.size() != 2)
        throw std::invalid_argument("static consumers Unsupported: header or permissions");
    using C = core::HexCoord;
    using Cell = std::pair<int, int>;
    auto key = [](C c) -> Cell { return {c.q, c.r}; };
    auto turn = [](C c, int n) {
        while (n--)
            c = core::RotateClockwise(c);
        return c;
    };
    auto element = [](int t) { return t >= 1 && t <= 16 && t != 15; };
    std::array<int, 17> feeds;
    feeds.fill(-1);
    for (int i = 0; i < static_cast<int>(p.inputs.size()); ++i) {
        const auto& m = p.inputs[i];
        if (m.atoms.size() != 1 || !m.bonds.empty() || !element(static_cast<int>(m.atoms[0].type)))
            throw std::invalid_argument(
                "static consumers Unsupported: genuine singleton feeds required");
        const int t = static_cast<int>(m.atoms[0].type);
        if (feeds[t] < 0) feeds[t] = i;
    }
    if (feeds[static_cast<int>(core::AtomType::Salt)] < 0)
        throw std::invalid_argument("static consumers Unsupported: genuine Salt feed required");
    std::array<C, 2> origins;
    std::array<int, 2> indices{-1, -1};
    for (int output = 0; output < 2; ++output) {
        checkTime();
        const auto& m = p.outputs[output];
        if (m.atoms.empty() || m.atoms.size() > 16 || m.bonds.size() > 36)
            throw std::invalid_argument("static consumers Unsupported: graph budget");
        std::set<Cell> cells;
        int side = -1;
        for (const auto& a : m.atoms) {
            const int t = static_cast<int>(a.type);
            if (!element(t) || !cells.insert(key(a.position)).second)
                throw std::invalid_argument(
                    "static consumers Unsupported: material or duplicate cell");
            if (a.type == core::AtomType::Vitae || a.type == core::AtomType::Mors) {
                if (side >= 0)
                    throw std::invalid_argument(
                        "static consumers Unsupported: one life atom per output required");
                side = a.type == core::AtomType::Vitae ? 0 : 1;
                origins[side] = a.position;
            } else if (feeds[t] < 0) {
                throw std::invalid_argument("static consumers Unsupported: missing direct feed");
            }
        }
        if (side < 0 || indices[side] >= 0)
            throw std::invalid_argument(
                "static consumers Unsupported: complementary outputs required");
        indices[side] = output;
        for (const auto& a : m.atoms) {
            const auto d = core::Subtract(a.position, origins[side]);
            if (core::Distance(d, {}) > 3)
                throw std::invalid_argument("static consumers Unsupported: life anchor radius");
        }
        std::set<std::pair<int, int>> edges;
        for (const auto& b : m.bonds) {
            int from = -1, to = -1;
            for (int i = 0; i < static_cast<int>(m.atoms.size()); ++i) {
                if (m.atoms[i].position == b.from) from = i;
                if (m.atoms[i].position == b.to) to = i;
            }
            if (b.bits != 1 || from < 0 || to < 0 || core::Distance(b.from, b.to) != 1 ||
                !edges.emplace(std::min(from, to), std::max(from, to)).second)
                throw std::invalid_argument(
                    "static consumers Unsupported: adjacent ordinary simple edges required");
        }
        std::set<int> reached{0};
        for (std::size_t pass = 0; pass < m.atoms.size(); ++pass)
            for (auto [a, b] : edges)
                if (reached.contains(a) || reached.contains(b)) {
                    reached.insert(a);
                    reached.insert(b);
                }
        if (reached.size() != m.atoms.size())
            throw std::invalid_argument("static consumers Unsupported: disconnected graph");
    }
    constexpr std::array<C, 2> ports{{{0, 1}, {1, -1}}};
    std::array<int, 2> rotations{};
    bool placed = false;
    // Life atoms stay on the real glyph ports; only the two graph rotations vary.
    for (int first = 0; first < 6 && !placed; ++first) {
        for (int second = 0; second < 6 && !placed; ++second) {
            checkTime();
            std::set<Cell> occupied{{0, 0}, {1, 0}};
            const std::array<int, 2> candidate{first, second};
            bool valid = true;
            for (int side = 0; side < 2; ++side)
                for (const auto& a : p.outputs[indices[side]].atoms) {
                    const C c =
                        core::Add(ports[side],
                                  turn(core::Subtract(a.position, origins[side]), candidate[side]));
                    valid &= occupied.insert(key(c)).second;
                }
            if (valid) {
                rotations = candidate;
                placed = true;
            }
        }
    }
    if (!placed)
        throw std::invalid_argument(
            "static consumers Unsupported: no disjoint anchored rotation pair");
    checkTime();
    io::SolutionData result;
    result.puzzleName = p.name;
    result.solutionName = "Bounded ordinary Animismus consumer embedding";
    auto object = [&](const char* type, C cell, int rotation = 0, int index = 0) {
        io::SolutionObject o;
        o.typeName = type;
        o.position = cell;
        o.bodyData = {1, rotation, index, 0, static_cast<int>(result.objects.size())};
        result.objects.push_back(o);
    };
    auto source = [&](C cell, int input) {
        object("input", core::Subtract(cell, p.inputs[input].atoms[0].position), 0, input);
    };
    source({0, 0}, feeds[1]);
    source({1, 0}, feeds[1]);
    object("glyph-life-and-death", {0, 0});
    for (int side = 0; side < 2; ++side) {
        const auto& m = p.outputs[indices[side]];
        auto place = [&](C c) {
            return core::Add(ports[side], turn(core::Subtract(c, origins[side]), rotations[side]));
        };
        for (const auto& a : m.atoms)
            if (a.position != origins[side])
                source(place(a.position), feeds[static_cast<int>(a.type)]);
        for (const auto& b : m.bonds) {
            const C from = place(b.from), to = place(b.to);
            int rotation = 0;
            while (turn({1, 0}, rotation) != core::Subtract(to, from))
                ++rotation;
            object("bonder", from, rotation);
        }
        object("out-std", core::Subtract(ports[side], turn(origins[side], rotations[side])),
               rotations[side], indices[side]);
    }
    return result;
}
} // namespace

io::SolutionData CompileDemandDrivenConsumers(const io::PuzzleData& p) {
    return CompileDemandDrivenConsumers(p, std::chrono::steady_clock::time_point::max());
}
io::SolutionData CompileDemandDrivenConsumers(const io::PuzzleData& p,
                                              std::chrono::steady_clock::time_point deadline) {
    return CompileConsumersWithFamily(p, deadline).solution;
}
namespace {
StaticReactionResult CompileConsumersWithFamily(const io::PuzzleData& p,
                                                std::chrono::steady_clock::time_point deadline) {
    if (deadline != std::chrono::steady_clock::time_point::max() &&
        std::chrono::steady_clock::now() >= deadline)
        throw std::invalid_argument("static consumers Unsupported: deadline");
    try {
        return {CompileLegacyDemandDrivenConsumers(p), StaticReactionFamily::LegacyConsumers};
    } catch (const std::invalid_argument&) {
        return {CompileOrdinaryAnimismusPair(p, deadline), StaticReactionFamily::OrdinaryPair};
    }
}
} // namespace
} // namespace opusoptima::solvers

namespace opusoptima::solvers {
// CompileStaticEmbedding registers singleton feeds and derives Salt/cardinals or metals.
// An unsupplied Vitae/Mors target is impossible within that compiler. The separate
// static reaction consumer compiler has its own Animismus emitter and native gate.
bool StaticEmbeddingMissingLifeFeed(const io::PuzzleData& puzzle) {
    return std::any_of(puzzle.outputs.begin(), puzzle.outputs.end(), [&](const auto& output) {
        return std::any_of(output.atoms.begin(), output.atoms.end(), [&](const auto& atom) {
            return (atom.type == core::AtomType::Vitae || atom.type == core::AtomType::Mors) &&
                   std::none_of(puzzle.inputs.begin(), puzzle.inputs.end(), [&](const auto& input) {
                       return input.atoms.size() == 1 && input.bonds.empty() &&
                              input.atoms[0].type == atom.type;
                   });
        });
    });
}
io::SolutionData CompileStaticEmbedding(const io::PuzzleData& p,
                                        std::chrono::steady_clock::time_point deadline) {
    auto checkTime = [&] {
        if (std::chrono::steady_clock::now() >= deadline)
            throw std::invalid_argument("static embedding Unsupported: deadline");
    };
    checkTime();
    constexpr auto required = (1ULL << 0) | (1ULL << 8) | (1ULL << 25);
    if (p.version != 3 || p.productionInfo || p.outputMultiplier != 1 ||
        (p.permissionBits & required) != required || p.inputs.empty() || p.inputs.size() > 8 ||
        p.outputs.empty() || p.outputs.size() > 2)
        throw std::invalid_argument("static embedding Unsupported: header or permissions");
    using C = core::HexCoord;
    auto turn = [](C c, int n) {
        while (n--)
            c = core::RotateClockwise(c);
        return c;
    };
    auto element = [](int t) { return t >= 1 && t <= 16 && t != 15; };
    std::array<int, 17> feeds;
    feeds.fill(-1);
    for (int i = 0; i < static_cast<int>(p.inputs.size()); ++i) {
        const auto& m = p.inputs[i];
        if (m.atoms.size() != 1 || !m.bonds.empty()) continue;
        int t = static_cast<int>(m.atoms[0].type);
        if (!element(t)) throw std::invalid_argument("static embedding Unsupported: feed material");
        if (feeds[t] < 0) feeds[t] = i;
    }
    int elemental = -1;
    for (int t = 2; t <= 5; ++t)
        if (feeds[t] >= 0) {
            elemental = feeds[t];
            break;
        }
    struct Supply {
        C cell;
        int input;
        bool calcify;
        int wheelType;
        int purify;
        bool project;
    };
    struct Output {
        C origin, anchor;
        int rotation;
    };
    std::vector<Supply> supplies, derived, purified, projected;
    std::vector<Output> outputs;
    std::set<std::pair<int, int>> occupied;
    for (int output = 0; output < static_cast<int>(p.outputs.size()); ++output) {
        checkTime();
        const auto& m = p.outputs[output];
        if (m.atoms.empty() || m.atoms.size() > 16 || m.bonds.size() > 36)
            throw std::invalid_argument("static embedding Unsupported: graph budget");
        std::set<std::pair<int, int>> cells;
        for (const auto& a : m.atoms) {
            const int t = static_cast<int>(a.type);
            if (!element(t) || !cells.emplace(a.position.q, a.position.r).second)
                throw std::invalid_argument(
                    "static embedding Unsupported: atom supply or duplicate cell");
        }
        std::set<std::pair<int, int>> edges;
        std::set<int> reached{0};
        for (const auto& b : m.bonds) {
            int from = -1, to = -1;
            for (int i = 0; i < static_cast<int>(m.atoms.size()); ++i) {
                if (m.atoms[i].position == b.from) from = i;
                if (m.atoms[i].position == b.to) to = i;
            }
            int rotation = 0;
            while (rotation < 6 && turn({1, 0}, rotation) != core::Subtract(b.to, b.from))
                ++rotation;
            const bool colored = b.bits != 1;
            if ((colored && (!b.bits || (b.bits & ~14))) || from < 0 || to < 0 || rotation == 6 ||
                !edges.emplace(std::min(from, to), std::max(from, to)).second)
                throw std::invalid_argument(
                    "static embedding Unsupported: adjacent simple valid-mask edges required");
            if (colored) {
                if (!(p.permissionBits & (1ULL << 11)) ||
                    m.atoms[from].type != core::AtomType::Fire ||
                    m.atoms[to].type != core::AtomType::Fire)
                    throw std::invalid_argument("static embedding Unsupported: colored edges "
                                                "require Fire endpoints and permission");
            }
        }
        for (std::size_t pass = 0; pass < m.atoms.size(); ++pass)
            for (auto [a, b] : edges)
                if (reached.contains(a) || reached.contains(b)) {
                    reached.insert(a);
                    reached.insert(b);
                }
        if (reached.size() != m.atoms.size())
            throw std::invalid_argument("static embedding Unsupported: disconnected graph");
        C origin{};
        int selected = -1;
        for (const auto& center : m.atoms) {
            for (int rotation = 0; rotation < 6; ++rotation) {
                checkTime();
                bool fits = true;
                for (const auto& a : m.atoms) {
                    const auto c = turn(core::Subtract(a.position, center.position), rotation);
                    fits &= std::max({std::abs(c.q), std::abs(c.r), std::abs(c.q + c.r)}) <= 3;
                }
                if (fits) {
                    origin = center.position;
                    selected = rotation;
                    break;
                }
            }
            if (selected >= 0) break;
        }
        if (selected < 0) throw std::invalid_argument("static embedding Unsupported: local radius");
        const C anchor{output * 10, 0};
        outputs.push_back({origin, anchor, selected});
        for (const auto& a : m.atoms) {
            checkTime();
            const int t = static_cast<int>(a.type);
            int input = feeds[t], wheelType = 0;
            bool calcify = false, project = false;
            int purify = 0;
            if (input < 0) {
                if (t >= 7 && t < 12 && feeds[t + 1] >= 0 && (p.permissionBits & (1ULL << 15))) {
                    input = feeds[t + 1];
                    purify = 1;
                } else if (t >= 7 && t < 12 && feeds[t + 1] >= 0 && feeds[6] >= 0 &&
                           (p.permissionBits & (1ULL << 14))) {
                    input = feeds[t + 1];
                    project = true;
                } else if (t >= 7 && t < 12 && (p.permissionBits & (1ULL << 15))) {
                    // Existing direct, adjacent purification and projection selections keep
                    // priority.
                    for (int source = t + 2; source <= 12; ++source)
                        if (feeds[source] >= 0) {
                            input = feeds[source];
                            purify = source - t;
                            break;
                        }
                    if (input < 0)
                        throw std::invalid_argument("static embedding Unsupported: atom supply");
                } else {
                    if (t > 5 || (feeds[1] < 0 && elemental < 0))
                        throw std::invalid_argument("static embedding Unsupported: atom supply");
                    input = feeds[1] >= 0 ? feeds[1] : elemental;
                    calcify = feeds[1] < 0;
                    wheelType = t == 1 ? 0 : t;
                }
            }
            if (calcify && !(p.permissionBits & (1ULL << 12)))
                throw std::invalid_argument(
                    "static embedding Unsupported: calcification permission");
            const auto cell = core::Add(anchor, turn(core::Subtract(a.position, origin), selected));
            if (!occupied.emplace(cell.q, cell.r).second)
                throw std::invalid_argument("static embedding Unsupported: overlapping outputs");
            supplies.push_back({cell, input, calcify, wheelType, purify, project});
            if (wheelType) derived.push_back(supplies.back());
            if (purify) purified.push_back(supplies.back());
            if (project) projected.push_back(supplies.back());
        }
    }
    // Calcified Salt remains stable while a private purification cascade finishes.
    // Wheel-derived cardinals and colored bonds still require separate chemistry.
    if (!purified.empty() &&
        (std::any_of(supplies.begin(), supplies.end(), [](const auto& s) { return s.wheelType; }) ||
         std::any_of(p.outputs.begin(), p.outputs.end(), [](const auto& m) {
             return std::any_of(m.bonds.begin(), m.bonds.end(),
                                [](const auto& b) { return b.bits != 1; });
         })))
        throw std::invalid_argument("static embedding Unsupported: purification requires direct or "
                                    "calcified ordinary neighbors");
    // Projection acts in both native half-cycles, including on bonded metals.
    // Every target must therefore finish and be consumed in the same half-cycle,
    // before the isolated Mercury input regenerates and upgrades it again.
    if (!projected.empty() &&
        (!purified.empty() ||
         std::any_of(supplies.begin(), supplies.end(), [](const auto& s) { return s.wheelType; }) ||
         std::any_of(p.outputs.begin(), p.outputs.end(), [](const auto& m) {
             return std::any_of(m.bonds.begin(), m.bonds.end(),
                                [](const auto& b) { return b.bits != 1; });
         })))
        throw std::invalid_argument("static embedding Unsupported: projection requires direct or "
                                    "calcified ordinary neighbors");
    constexpr auto wheelPermissions = (1ULL << 13) | (1ULL << 28);
    if (!derived.empty() && (p.permissionBits & wheelPermissions) != wheelPermissions)
        throw std::invalid_argument("static embedding Unsupported: wheel permissions");
    struct Wheel {
        C center;
        int rotation;
    };
    std::vector<Wheel> wheels;
    std::vector<std::pair<C, int>> duplicators;
    const std::array<C, 6> wheelOffsets{{{}, {}, {-1, 1}, {1, -1}, {0, -1}, {0, 1}}};
    std::size_t placementChecks = 0;
    constexpr std::size_t placementLimit = 16 * 36;
    using CellKey = std::pair<int, int>;
    using EdgeKey = std::pair<CellKey, CellKey>;
    auto key = [](C c) -> CellKey { return {c.q, c.r}; };
    auto edgeKey = [&](C a, C b) -> EdgeKey {
        auto x = key(a), y = key(b);
        if (y < x) std::swap(x, y);
        return {x, y};
    };
    std::map<EdgeKey, int> desired;
    std::set<EdgeKey> ordinaryEdges;
    std::set<CellKey> fireCells;
    for (std::size_t i = 0; i < outputs.size(); ++i) {
        checkTime();
        const auto placement = outputs[i];
        auto world = [&](C c) {
            return core::Add(placement.anchor,
                             turn(core::Subtract(c, placement.origin), placement.rotation));
        };
        for (const auto& atom : p.outputs[i].atoms)
            if (atom.type == core::AtomType::Fire) fireCells.insert(key(world(atom.position)));
        for (const auto& bond : p.outputs[i].bonds)
            if (bond.bits != 1)
                desired.emplace(edgeKey(world(bond.from), world(bond.to)),
                                static_cast<int>(bond.bits));
            else
                ordinaryEdges.insert(edgeKey(world(bond.from), world(bond.to)));
    }
    std::vector<std::pair<C, int>> prisms;
    std::set<EdgeKey> prismCleanup;
    auto planPrisms = [&](bool cleanup) -> bool {
        std::vector<std::pair<C, int>> candidate;
        std::map<EdgeKey, int> actual;
        std::set<CellKey> wheelCells;
        for (const auto& wheel : wheels) {
            wheelCells.insert(key(wheel.center));
            for (int r = 0; r < 6; ++r)
                wheelCells.insert(key(core::Add(wheel.center, turn({1, 0}, r))));
        }
        const std::array<C, 3> local{{{0, 0}, {0, 1}, {1, 0}}};
        constexpr std::array<int, 3> colors{{8, 2, 4}};
        for (const auto& [edge, mask] : desired) {
            const C a{edge.first.first, edge.first.second};
            for (int color : {2, 4, 8}) {
                if (!(mask & color) || (actual[edge] & color)) continue;
                int bestCoverage = 0, bestRotation = 0;
                C bestOrigin{};
                std::map<EdgeKey, int> bestInduced;
                for (int r = 0; r < 6; ++r) {
                    for (C anchor : local) {
                        checkTime();
                        if (placementChecks >= placementLimit) return false;
                        ++placementChecks;
                        const C origin = core::Subtract(a, turn(anchor, r));
                        std::array<C, 3> vertices;
                        bool safe = true, covers = false;
                        for (int j = 0; j < 3; ++j) {
                            vertices[j] = core::Add(origin, turn(local[j], r));
                            safe &= !wheelCells.contains(key(vertices[j]));
                        }
                        std::map<EdgeKey, int> induced;
                        for (int j = 0; j < 3 && safe; ++j) {
                            const C x = vertices[j], y = vertices[(j + 1) % 3];
                            const auto e = edgeKey(x, y);
                            covers |= e == edge && colors[j] == color;
                            if (!fireCells.contains(key(x)) || !fireCells.contains(key(y)))
                                continue;
                            const auto wanted = desired.find(e);
                            if (wanted == desired.end() || !(wanted->second & colors[j])) {
                                if (!cleanup || !ordinaryEdges.contains(e)) {
                                    safe = false;
                                    break;
                                }
                            }
                            induced[e] |= colors[j];
                        }
                        if (!safe || !covers) continue;
                        int coverage = 0;
                        for (const auto& [e, bits] : induced) {
                            if (ordinaryEdges.contains(e)) continue;
                            const auto previous = actual.find(e);
                            const int missing =
                                bits & ~(previous == actual.end() ? 0 : previous->second);
                            for (int bit : {2, 4, 8})
                                coverage += (missing & bit) != 0;
                        }
                        if (coverage > bestCoverage) {
                            bestCoverage = coverage;
                            bestOrigin = origin;
                            bestRotation = r;
                            bestInduced = std::move(induced);
                        }
                    }
                }
                if (!bestCoverage) return false;
                candidate.emplace_back(bestOrigin, bestRotation);
                for (const auto& [e, bits] : bestInduced)
                    actual[e] |= bits;
            }
        }
        std::set<EdgeKey> cleanupEdges;
        if (cleanup)
            for (const auto& e : ordinaryEdges)
                if (actual.erase(e)) cleanupEdges.insert(e);
        if (actual != desired) return false;
        prisms = std::move(candidate);
        prismCleanup = std::move(cleanupEdges);
        return true;
    };
    struct Projection {
        C origin;
        int rotation;
    };
    std::vector<Projection> projections;
    std::function<bool(std::size_t)> placeProjection = [&](std::size_t index) {
        checkTime();
        if (index == projected.size())
            return planPrisms(false) || ((p.permissionBits & (1ULL << 9)) && planPrisms(true));
        const auto supply = projected[index];
        for (int rotation = 0; rotation < 6; ++rotation) {
            checkTime();
            if (placementChecks >= placementLimit) return false;
            ++placementChecks;
            const C mercury = core::Subtract(supply.cell, turn({1, 0}, rotation));
            if (occupied.contains(key(mercury))) continue;
            occupied.insert(key(mercury));
            projections.push_back({mercury, rotation});
            if (placeProjection(index + 1)) return true;
            projections.pop_back();
            occupied.erase(key(mercury));
        }
        return false;
    };
    struct Purification {
        C origin;
        int rotation;
        int input;
        int depth;
    };
    std::vector<Purification> purifications;
    std::function<bool(std::size_t)> placePurification = [&](std::size_t index) {
        checkTime();
        if (index == purified.size()) return placeProjection(0);
        const auto supply = purified[index];
        for (int rotation = 0; rotation < 6; ++rotation) {
            checkTime();
            if (placementChecks >= placementLimit) return false;
            ++placementChecks;
            const C origin = core::Subtract(supply.cell, turn({0, supply.purify}, rotation));
            std::vector<C> footprint;
            bool blocked = false;
            VisitStaticCascadeFootprint(supply.purify, [&](C local) {
                const C cell = core::Add(origin, turn(local, rotation));
                blocked |= occupied.contains(key(cell));
                footprint.push_back(cell);
            });
            if (blocked) continue;
            for (const C cell : footprint)
                occupied.insert(key(cell));
            purifications.push_back({origin, rotation, supply.input, supply.purify});
            if (placePurification(index + 1)) return true;
            purifications.pop_back();
            for (const C cell : footprint)
                occupied.erase(key(cell));
        }
        return false;
    };
    std::function<bool(std::size_t)> place = [&](std::size_t index) {
        checkTime();
        if (index == derived.size()) return placePurification(0);
        const auto supply = derived[index];
        auto bind = [&](C catalyst) {
            int rotation = 0;
            while (rotation < 6 && turn({1, 0}, rotation) != core::Subtract(supply.cell, catalyst))
                ++rotation;
            if (rotation == 6) return false;
            duplicators.push_back({catalyst, rotation});
            if (place(index + 1)) return true;
            duplicators.pop_back();
            return false;
        };
        const auto existing = wheels.size();
        for (std::size_t i = 0; i < existing; ++i) {
            checkTime();
            if (++placementChecks > placementLimit) return false;
            const auto wheel = wheels[i];
            if (bind(core::Add(wheel.center, turn(wheelOffsets[supply.wheelType], wheel.rotation))))
                return true;
        }
        for (int direction = 0; direction < 6; ++direction) {
            const auto catalyst = core::Add(supply.cell, turn({1, 0}, direction));
            for (int rotation = 0; rotation < 6; ++rotation) {
                checkTime();
                if (++placementChecks > placementLimit) return false;
                const auto center =
                    core::Subtract(catalyst, turn(wheelOffsets[supply.wheelType], rotation));
                std::array<C, 7> footprint;
                footprint[0] = center;
                for (int i = 0; i < 6; ++i)
                    footprint[i + 1] = core::Add(center, turn({1, 0}, i));
                if (std::any_of(footprint.begin(), footprint.end(),
                                [&](C c) { return occupied.contains({c.q, c.r}); }))
                    continue;
                for (const auto cell : footprint)
                    occupied.emplace(cell.q, cell.r);
                wheels.push_back({center, rotation});
                if (bind(catalyst)) return true;
                wheels.pop_back();
                for (const auto cell : footprint)
                    occupied.erase({cell.q, cell.r});
            }
        }
        return false;
    };
    if (!place(0))
        throw std::invalid_argument(
            !projected.empty()
                ? "static embedding Unsupported: bounded projection placement exhausted"
            : purified.empty()
                ? "static embedding Unsupported: bounded wheel/prism placement exhausted"
                : "static embedding Unsupported: bounded purification placement exhausted");
    io::SolutionData result;
    result.puzzleName = p.name;
    result.solutionName = "Finite static typed reaction embedding";
    int armIndex = 0;
    auto object = [&](const char* name, C cell, int rotation = 0, int index = 0) {
        io::SolutionObject o;
        o.typeName = name;
        o.position = cell;
        o.bodyData = {1, rotation, index, 0, 0};
        if (o.typeName == "arm1" || o.typeName == "baron") o.bodyData[4] = armIndex++;
        result.objects.push_back(o);
    };
    for (const auto& supply : supplies)
        if (!supply.purify)
            object("input", core::Subtract(supply.cell, p.inputs[supply.input].atoms[0].position),
                   0, supply.input);
    for (const auto& tile : purifications) {
        auto world = [&](C cell) { return core::Add(tile.origin, turn(cell, tile.rotation)); };
        EmitStaticCascade(
            tile.depth,
            [&](C cell) {
                object("input", core::Subtract(world(cell), p.inputs[tile.input].atoms[0].position),
                       0, tile.input);
            },
            [&](C cell) { object("glyph-purification", world(cell), tile.rotation); });
    }
    for (const auto& tile : projections) {
        object("input", core::Subtract(tile.origin, p.inputs[feeds[6]].atoms[0].position), 0,
               feeds[6]);
        object("glyph-projection", tile.origin, tile.rotation);
    }
    // Native inputs regenerate before glyphs and outputs consume after glyphs in each
    // half-cycle. These target-only calcifiers precede every bonder, so projected
    // products finish before Mercury regenerates; Salt also waits safely for purification.
    // Calcification must precede duplication at a shared substrate cell.
    for (const auto& supply : supplies)
        if (supply.calcify) object("glyph-calcification", supply.cell);
    for (const auto& wheel : wheels)
        object("baron", wheel.center, wheel.rotation);
    for (const auto& [cell, rotation] : duplicators)
        object("glyph-duplication", cell, rotation);
    for (const auto& [cell, rotation] : prisms)
        object("bonder-prisma", cell, rotation);
    // Native glyph order clears prism spill before ordinary bonds are restored.
    // Only explicitly ordinary target edges may be cleared; colored edges stay exact.
    for (const auto& e : prismCleanup) {
        const C from{e.first.first, e.first.second}, to{e.second.first, e.second.second};
        int rotation = 0;
        while (rotation < 6 && turn({1, 0}, rotation) != core::Subtract(to, from))
            ++rotation;
        object("unbonder", from, rotation);
    }
    for (std::size_t output = 0; output < outputs.size(); ++output) {
        checkTime();
        const auto placement = outputs[output];
        auto world = [&](C c) {
            return core::Add(placement.anchor,
                             turn(core::Subtract(c, placement.origin), placement.rotation));
        };
        for (const auto& bond : p.outputs[output].bonds) {
            if (bond.bits != 1) continue;
            int rotation = 0;
            while (rotation < 6 &&
                   turn({1, 0}, rotation) != core::Subtract(world(bond.to), world(bond.from)))
                ++rotation;
            object("bonder", world(bond.from), rotation);
        }
        object("out-std",
               core::Subtract(placement.anchor, turn(placement.origin, placement.rotation)),
               placement.rotation, static_cast<int>(output));
    }
    C idle{4, 6};
    for (const auto& cell : occupied) {
        idle.q = std::max(idle.q, cell.first + 5);
        idle.r = std::max(idle.r, cell.second + 5);
    }
    object("arm1", idle);
    result.objects.back().instructions = {{'O', 96}};
    checkTime();
    return result;
}
} // namespace opusoptima::solvers
