#include "TracklessPairChemistry.hpp"
#include "ChemicalPlanner.hpp"
#include "ModuleHandoff.hpp"

#include <algorithm>
#include <array>
#include <map>
#include <queue>
#include <set>
#include <stdexcept>

namespace opusoptima::solvers {
namespace {
using Cell = std::pair<int, int>;
constexpr std::array<Cell, 6> directions{{{1, 0}, {0, 1}, {-1, 1}, {-1, 0}, {0, -1}, {1, -1}}};
Cell Add(Cell a, Cell b) {
    return {a.first + b.first, a.second + b.second};
}
core::HexCoord Hex(Cell c) {
    return {c.first, c.second};
}
Cell Rotate(Cell c, int turns) {
    while (turns-- > 0)
        c = {-c.second, c.first + c.second};
    return c;
}
[[noreturn]] void Reject() {
    throw std::invalid_argument(
        "trackless pair chemistry requires an exact iron/water to copper/salt/vitae recipe");
}
} // namespace
io::SolutionData CompileTracklessPairChemistry(const io::PuzzleData& p) {
    constexpr auto required = (1ULL << 0) | (1ULL << 9) | (1ULL << 12) | (1ULL << 15) |
                              (1ULL << 16) | (1ULL << 22) | (1ULL << 23);
    if (p.version != 3 || p.productionInfo || p.outputMultiplier != 1 ||
        (p.permissionBits & required) != required || p.inputs.size() != 1 ||
        p.outputs.size() != 2 || p.inputs[0].atoms.size() != 2 || p.inputs[0].bonds.size() != 1 ||
        p.inputs[0].bonds[0].bits != 1)
        Reject();
    int iron = -1, water = -1, main = -1, waste = -1;
    for (int i = 0; i < 2; ++i) {
        if (p.inputs[0].atoms[i].type == core::AtomType::Iron) iron = i;
        if (p.inputs[0].atoms[i].type == core::AtomType::Water) water = i;
        if (p.outputs[i].atoms.size() == 5) main = i;
        if (p.outputs[i].atoms.size() == 1 && p.outputs[i].atoms[0].type == core::AtomType::Mors &&
            p.outputs[i].bonds.empty())
            waste = i;
    }
    if (iron < 0 || water < 0 || main < 0 || waste < 0 || main == waste) Reject();
    const auto& input = p.inputs[0];
    const auto a = input.atoms[iron].position, b = input.atoms[water].position;
    const auto bond = input.bonds[0];
    if (!((bond.from == a && bond.to == b) || (bond.from == b && bond.to == a))) Reject();
    int rotation = -1;
    for (int i = 0; i < 6; ++i)
        if (Rotate({b.q - a.q, b.r - a.r}, i) == Cell{1, 0}) rotation = i;
    if (rotation < 0) Reject();
    const auto recipe = ChemicalPlanner::Plan(p, 6, false);
    if (recipe.inputBatches != std::vector<std::size_t>{24} || recipe.purifications != 12 ||
        recipe.animismus != 6 || recipe.calcifications != 18)
        Reject();
    io::SolutionData result;
    result.puzzleName = p.name;
    result.solutionName = "Trackless exact pair chemistry";
    auto object = [&](const char* type, Cell c, int angle = 0, int index = 0) {
        io::SolutionObject o;
        o.typeName = type;
        o.position = Hex(c);
        o.bodyData = {1, angle, index, 0, static_cast<int>(result.objects.size())};
        result.objects.push_back(o);
        return result.objects.size() - 1;
    };
    const Cell inputMetal{-10, 0}, inputWater{-9, 0};
    const Cell purifyA{-4, 0}, purifyB{-3, 0}, purified{-4, 1};
    const Cell saltA{2, 0}, saltB{3, 0}, vitae{2, 1}, mors{3, -1};
    const Cell wasteOut{5, -5};
    const auto origin = Rotate({a.q, a.r}, rotation);
    object("input", {inputMetal.first - origin.first, inputMetal.second - origin.second}, rotation);
    object("unbonder", inputMetal);
    object("glyph-purification", purifyA);
    object("glyph-life-and-death", saltA);
    object("out-std",
           {wasteOut.first - p.outputs[waste].atoms[0].position.q,
            wasteOut.second - p.outputs[waste].atoms[0].position.r},
           0, waste);
    std::vector<Cell> targets;
    std::map<Cell, int> targetTypes;
    for (const auto& atom : p.outputs[main].atoms) {
        Cell c{atom.position.q + 10, atom.position.r};
        if (!targetTypes.emplace(c, static_cast<int>(atom.type)).second) Reject();
        targets.push_back(c);
    }
    std::multiset<int> types;
    for (const auto& [c, type] : targetTypes)
        types.insert(type);
    if (types != std::multiset<int>{1, 5, 9, 9, 13}) Reject();
    std::map<Cell, std::set<Cell>> bonding;
    for (const auto& bond : p.outputs[main].bonds) {
        if (bond.bits != 1) Reject();
        Cell from{bond.from.q + 10, bond.from.r}, to{bond.to.q + 10, bond.to.r};
        if (!targetTypes.contains(from) || !targetTypes.contains(to)) Reject();
        int d = -1;
        for (int i = 0; i < 6; ++i)
            if (Add(from, directions[i]) == to) d = i;
        if (d < 0) Reject();
        object("bonder", from, d);
        bonding[from].insert(to);
        bonding[to].insert(from);
    }
    object("out-std", {10, 0}, 0, main);
    std::set<Cell> reserved{inputMetal, inputWater, purifyA, purifyB, purified,
                            saltA,      saltB,      vitae,   mors,    wasteOut};
    reserved.insert(targets.begin(), targets.end());
    std::map<Cell, std::size_t> arms;
    std::map<Cell, int> angles;
    auto addArm = [&](Cell c) {
        if (reserved.contains(c) || arms.contains(c)) return;
        arms[c] = object("arm1", c);
        angles[c] = 0;
    };
    for (int q = -15; q <= 16; ++q)
        for (int r = -10; r <= 10; ++r)
            if ((q - r) % 3 == 0) addArm({q, r});
    for (auto c : reserved) {
        if ((c.first - c.second) % 3 != 0) continue;
        for (auto d : directions) {
            auto base = Add(c, d);
            if (!reserved.contains(base)) {
                addArm(base);
                break;
            }
        }
    }
    std::set<Cell> occupied{inputMetal, inputWater};
    int tick = 1;
    std::vector<ModuleHandoff> handoffs;
    auto move = [&](Cell source, Cell destination) {
        struct Step {
            Cell previous, base;
            int from, to;
        };
        std::map<Cell, Step> previous;
        std::queue<Cell> queue;
        queue.push(source);
        previous[source] = {source, {}, 0, 0};
        while (!queue.empty() && !previous.contains(destination)) {
            auto c = queue.front();
            queue.pop();
            for (int i = 0; i < 6; ++i) {
                Cell base{c.first - directions[i].first, c.second - directions[i].second};
                if (!arms.contains(base)) continue;
                for (int turn : {-1, 1}) {
                    int j = (i + turn + 6) % 6;
                    auto next = Add(base, directions[j]);
                    if (next.first < -15 || next.first > 16 || next.second < -10 ||
                        next.second > 10 || arms.contains(next) || previous.contains(next) ||
                        occupied.contains(next) ||
                        (reserved.contains(next) && !targetTypes.contains(next) &&
                         next != destination))
                        continue;
                    bool wouldBond = false;
                    if (next != destination)
                        for (auto neighbor : bonding[next])
                            if (occupied.contains(neighbor)) wouldBond = true;
                    if (wouldBond) continue;
                    previous[next] = {c, base, i, j};
                    queue.push(next);
                }
            }
        }
        if (!previous.contains(destination))
            throw std::invalid_argument(
                "trackless relay route blocked: " + std::to_string(source.first) + "," +
                std::to_string(source.second) + " -> " + std::to_string(destination.first) + "," +
                std::to_string(destination.second));
        std::vector<Step> path;
        for (auto c = destination; c != source; c = previous.at(c).previous)
            path.push_back(previous.at(c));
        std::reverse(path.begin(), path.end());
        for (const auto& step : path) {
            auto& tape = result.objects[arms.at(step.base)].instructions;
            auto& angle = angles.at(step.base);
            while (angle != step.from) {
                tape.push_back({'r', tick++});
                angle = (angle + 1) % 6;
            }
            tape.push_back({'G', tick++});
            tape.push_back({(step.to - step.from + 6) % 6 == 1 ? 'r' : 'R', tick++});
            angle = step.to;
            tape.push_back({'g', tick++});
            tick += 2;
        }
        occupied.erase(source);
        occupied.insert(destination);
    };
    auto deliver = [&](Cell source, int type, std::set<Cell>& filled) {
        for (auto c : targets)
            if (targetTypes.at(c) == type && !filled.contains(c)) {
                move(source, c);
                filled.insert(c);
                return;
            }
        Reject();
    };
    for (int batch = 0; batch < 6; ++batch) {
        std::set<Cell> filled;
        for (int feed = 0; feed < 4; ++feed) {
            occupied.insert(inputMetal);
            occupied.insert(inputWater);
            // Take water first so the input cannot respawn over the held metal.
            if (feed == 0)
                deliver(inputWater, 5, filled);
            else {
                const Cell salt = feed == 1 ? saltA : (feed == 2 ? saltB : Cell{0, -5});
                if (batch == 0) object("glyph-calcification", salt);
                move(inputWater, salt);
                if (feed == 2) {
                    occupied.erase(saltA);
                    occupied.erase(saltB);
                    occupied.insert(vitae);
                    occupied.insert(mors);
                    tick += 3;
                    deliver(vitae, 13, filled);
                    move(mors, wasteOut);
                    occupied.erase(wasteOut);
                } else if (feed == 3)
                    deliver(salt, 1, filled);
            }
            const Cell metalDestination = feed % 2 == 0 ? purifyA : purifyB;
            move(inputMetal, metalDestination);
            if (feed % 2) {
                occupied.erase(purifyA);
                occupied.erase(purifyB);
                occupied.insert(purified);
                const ModulePort port{Hex(purified), 0, false};
                const int available = tick;
                tick += 3;
                const int pickup = tick;
                deliver(purified, 9, filled);
                handoffs.push_back({port, port, available, pickup, tick, tick + 1});
            }
        }
        for (auto c : targets)
            occupied.erase(c);
        tick += 5;
    }
    ValidateModuleHandoffs(handoffs);
    for (const auto& [base, index] : arms)
        result.objects[index].instructions.push_back({'O', tick});
    return result;
}
} // namespace opusoptima::solvers
