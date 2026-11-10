#include "ProductionFiniteStock.hpp"
#include "HexFrame.hpp"
#include "ProductionLayout.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <queue>
#include <set>
#include <tuple>

namespace opusoptima::solvers {
namespace {
using Cell = core::HexCoord;
using Key = std::pair<int, int>;
constexpr std::array<Cell, 6> directions{{{1, 0}, {0, 1}, {-1, 1}, {-1, 0}, {0, -1}, {1, -1}}};
Key KeyOf(Cell cell) {
    return {cell.q, cell.r};
}
Cell Turn(Cell cell, int rotation) {
    while (rotation-- > 0)
        cell = core::RotateClockwise(cell);
    return cell;
}
struct Pose {
    int base{}, length{}, direction{};
    auto operator<=>(const Pose&) const = default;
};
Cell Tip(Pose pose) {
    const auto direction = directions[pose.direction];
    return {pose.base + pose.length * direction.q, pose.length * direction.r};
}
Pose Next(Pose pose, char action) {
    if (action == 'r') pose.direction = (pose.direction + 1) % 6;
    if (action == 'R') pose.direction = (pose.direction + 5) % 6;
    if (action == 'E') pose.length = std::min(3, pose.length + 1);
    if (action == 'e') pose.length = std::max(1, pose.length - 1);
    if (action == 'A') pose.base = std::min(2, pose.base + 1);
    if (action == 'a') pose.base = std::max(0, pose.base - 1);
    return pose;
}
struct Point {
    double x{}, y{};
};
Point XY(Cell cell) {
    return {cell.q + cell.r * 0.5, cell.r * std::sqrt(3.0) * 0.5};
}
class StockSearch {
    public:
    explicit StockSearch(int ordering) : ordering_(ordering) {
        for (int q = -1; q <= 3; ++q)
            for (int r = -1; r <= 1; ++r) {
                int distance = 10;
                for (int shift = 0; shift <= 2; ++shift)
                    distance = std::min(distance, core::Distance({shift, 0}, {q, r}));
                if (distance <= 1) cells_.insert({q, r});
            }
        for (auto cell : cells_)
            if (cell != Key{1, 1}) slots_.push_back(cell);
    }
    std::vector<std::string> Find(std::size_t count) {
        if (count > 11) return {};
        targetCount_ = std::min(count, slots_.size());
        std::set<Key> occupied;
        std::vector<std::string> tapes;
        if (!Fill(occupied, tapes)) return {};
        return tapes;
    }
    int FinalBase() const {
        return finalBase_;
    }

    private:
    bool Legal(Pose from, Pose to, char action, const std::set<Key>& occupied, bool held) const {
        if (from == to || !cells_.contains(KeyOf(Tip(to)))) return false;
        const auto old = XY(Tip(from)), next = XY(Tip(to));
        for (int sample = 0; sample <= 64; ++sample) {
            const double fraction = static_cast<double>(sample) / 64;
            const double base = from.base + (to.base - from.base) * fraction;
            Point point{old.x + (next.x - old.x) * fraction, old.y + (next.y - old.y) * fraction};
            if (action == 'r' || action == 'R') {
                const double angle = (action == 'r' ? 1 : -1) * std::acos(-1.0) / 3 * fraction;
                point = {from.base + (old.x - from.base) * std::cos(angle) -
                             old.y * std::sin(angle),
                         (old.x - from.base) * std::sin(angle) + old.y * std::cos(angle)};
            }
            {
                const double r = point.y * 2 / std::sqrt(3.0), q = point.x - r * 0.5;
                double distance = 10;
                for (int shift = 0; shift <= 2; ++shift)
                    distance = std::min(distance, std::max({std::abs(q - shift), std::abs(r),
                                                            std::abs(q - shift + r)}));
                if (distance > 1.4) return false;
            }
            for (auto [q, r] : occupied) {
                const auto obstacle = XY({q, r});
                if ((base - obstacle.x) * (base - obstacle.x) + obstacle.y * obstacle.y < 0.20)
                    return false;
                if (held && (point.x - obstacle.x) * (point.x - obstacle.x) +
                                    (point.y - obstacle.y) * (point.y - obstacle.y) <
                                0.36)
                    return false;
            }
        }
        return true;
    }
    std::map<Pose, std::string> Routes(Pose initial, const std::set<Key>& occupied,
                                       bool held) const {
        std::map<Pose, std::string> result{{initial, {}}};
        std::queue<Pose> pending;
        pending.push(initial);
        while (!pending.empty()) {
            const auto pose = pending.front();
            pending.pop();
            auto actions = std::string("rREeAa");
            std::rotate(actions.begin(), actions.begin() + ordering_ % 6, actions.end());
            for (char action : actions) {
                const auto next = Next(pose, action);
                if (result.contains(next) || !Legal(pose, next, action, occupied, held)) continue;
                result.emplace(next, result.at(pose) + action);
                pending.push(next);
            }
        }
        return result;
    }
    bool Fill(std::set<Key>& occupied, std::vector<std::string>& tapes) {
        if (tapes.size() == targetCount_) return true;
        if (failed_.contains(occupied)) return false;
        struct Candidate {
            Key cell;
            std::string tape;
            int finalBase;
        };
        std::vector<Candidate> candidates;
        for (const auto& [pose, route] : Routes(start_, occupied, true)) {
            const auto cell = KeyOf(Tip(pose));
            if (occupied.contains(cell) ||
                std::find(slots_.begin(), slots_.end(), cell) == slots_.end())
                continue;
            auto next = occupied;
            next.insert(cell);
            const bool final = tapes.size() + 1 == targetCount_;
            const auto reset =
                final ? std::map<Pose, std::string>{{start_, {}}} : Routes(pose, next, false);
            if (!reset.contains(start_)) continue;
            const auto tape = "G" + route + "g" + reset.at(start_);
            if (tape.size() <= 19) candidates.push_back({cell, tape, pose.base});
        }
        std::stable_sort(candidates.begin(), candidates.end(), [&](const auto& a, const auto& b) {
            const auto distance = [](Key cell) {
                return std::max({std::abs(cell.first - 2), std::abs(cell.second),
                                 std::abs(cell.first - 2 + cell.second)});
            };
            const auto key = [&](const Candidate& candidate) {
                const auto [q, r] = candidate.cell;
                unsigned rank =
                    static_cast<unsigned>((q + 3) * 17 + (r + 3) * 113 + ordering_ * 7919);
                rank ^= rank >> 7;
                rank *= 2654435761U;
                rank ^= rank >> 13;
                return std::tuple{-distance(candidate.cell), rank, candidate.tape.size()};
            };
            return key(a) < key(b);
        });
        for (const auto& candidate : candidates) {
            occupied.insert(candidate.cell);
            tapes.push_back(candidate.tape);
            finalBase_ = candidate.finalBase;
            if (Fill(occupied, tapes)) return true;
            occupied.erase(candidate.cell);
            tapes.pop_back();
        }
        failed_.insert(occupied);
        return false;
    }
    int ordering_{}, finalBase_{2};
    std::size_t targetCount_{};
    const Pose start_{2, 1, 2};
    std::set<Key> cells_;
    std::vector<Key> slots_;
    std::set<std::set<Key>> failed_;
};
std::optional<core::HexFrame> FeedFrame(const io::Molecule& feed, Cell center) {
    const auto quint = std::find_if(feed.atoms.begin(), feed.atoms.end(), [](const auto& atom) {
        return atom.type == core::AtomType::Quintessence;
    });
    if (quint == feed.atoms.end()) return {};
    const std::set<Key> contacts{{0, 1}, {-1, 1}, {0, -1}, {1, -1}};
    for (int rotation = 0; rotation < 6; ++rotation) {
        const core::HexFrame frame{core::Subtract(center, Turn(quint->position, rotation)),
                                   rotation};
        std::set<Key> cardinals, salts;
        std::set<int> elements;
        bool valid = true;
        for (const auto& atom : feed.atoms) {
            const auto cell = core::Subtract(frame.ToWorld(atom.position), center);
            if (atom.type == core::AtomType::Quintessence)
                valid &= cell == Cell{};
            else if (atom.type == core::AtomType::Salt)
                valid &= salts.insert(KeyOf(cell)).second;
            else if (atom.type >= core::AtomType::Air && atom.type <= core::AtomType::Water) {
                valid &= cardinals.insert(KeyOf(cell)).second;
                valid &= elements.insert(static_cast<int>(atom.type)).second;
            } else
                valid = false;
        }
        if (valid && salts == std::set<Key>{{-1, 0}, {1, 0}} && cardinals == contacts &&
            elements.size() == 4)
            return frame;
    }
    return {};
}
} // namespace
std::optional<io::SolutionData> CompileAcceptedProductionFiniteStock(
    const io::PuzzleData& puzzle, const std::function<bool(const io::SolutionData&)>& accept) {
    constexpr std::uint64_t permissions = (1ULL << 2) | (1ULL << 3) | (1ULL << 9) | (1ULL << 18) |
                                          (1ULL << 22) | (1ULL << 23) | (1ULL << 25);
    if (puzzle.version != 3 || !puzzle.productionInfo || puzzle.productionInfo->isolation ||
        puzzle.outputMultiplier < 1 || puzzle.outputMultiplier > 2 || puzzle.inputs.size() != 1 ||
        puzzle.outputs.size() != 2 || puzzle.inputs[0].atoms.size() != 7 ||
        (puzzle.permissionBits & permissions) != permissions)
        return {};
    for (const auto* molecules : {&puzzle.inputs, &puzzle.outputs})
        for (const auto& molecule : *molecules)
            for (const auto& atom : molecule.atoms)
                if (atom.position.q < -120 || atom.position.q > 120 || atom.position.r < -120 ||
                    atom.position.r > 120)
                    return {};
    for (const auto& output : puzzle.outputs)
        if (output.atoms.size() != 1 || !output.bonds.empty() ||
            output.atoms[0].type != core::AtomType::Quintessence)
            return {};
    const auto& info = *puzzle.productionInfo;
    if (info.chambers.size() != 3 || info.conduits.size() != 3) return {};
    std::vector<ProductionChamberLayout> rooms;
    try {
        rooms = PlanProductionChambers(puzzle);
    } catch (const std::invalid_argument&) {
        return {};
    }
    std::size_t source = 3;
    std::vector<std::size_t> stock;
    for (std::size_t i = 0; i < 3; ++i) {
        if (info.chambers[i].chamberType == "Large") {
            if (source != 3) return {};
            source = i;
        } else if (info.chambers[i].chamberType == "SmallWider")
            stock.push_back(i);
        else
            return {};
    }
    if (source == 3 || stock.size() != 2) return {};
    const auto center = rooms[source].center;
    const auto feed = FeedFrame(puzzle.inputs[0], center);
    if (!feed) return {};
    std::set<std::pair<Key, Key>> edges;
    for (const auto& bond : puzzle.inputs[0].bonds) {
        if (bond.bits != 1 || core::Distance(bond.from, bond.to) != 1) return {};
        if (std::none_of(puzzle.inputs[0].atoms.begin(), puzzle.inputs[0].atoms.end(),
                         [&](const auto& atom) { return atom.position == bond.from; }) ||
            std::none_of(puzzle.inputs[0].atoms.begin(), puzzle.inputs[0].atoms.end(),
                         [&](const auto& atom) { return atom.position == bond.to; }))
            return {};
        auto a = KeyOf(bond.from), b = KeyOf(bond.to);
        if (b < a) std::swap(a, b);
        if (!edges.emplace(a, b).second) return {};
    }
    std::map<std::pair<std::size_t, std::size_t>, std::size_t> conduits;
    const auto owner = [&](Cell position) {
        for (std::size_t i = 0; i < rooms.size(); ++i)
            if (ContainsProductionCell(rooms[i], position)) return i;
        return rooms.size();
    };
    for (std::size_t i = 0; i < 3; ++i) {
        const auto& pipe = info.conduits[i];
        if (pipe.path != std::vector<Cell>{{0, 0}}) return {};
        auto a = owner(pipe.positionA), b = owner(pipe.positionB);
        if (a >= 3 || b >= 3 || a == b) return {};
        if (b < a) std::swap(a, b);
        if (!conduits.emplace(std::pair{a, b}, i).second) return {};
    }
    const auto identity = [&](std::size_t a, std::size_t b) {
        if (b < a) std::swap(a, b);
        return static_cast<int>(100 + conduits.at({a, b}));
    };
    const int demand = 6 * puzzle.outputMultiplier, period = 20 * demand;
    for (int ordering = 0; ordering < 24; ++ordering) {
        StockSearch search{ordering};
        const auto packing = search.Find(demand - 1);
        if (packing.size() != static_cast<std::size_t>(demand - 1)) continue;
        for (int stockTurn : {0, 3}) {
            const auto stockCell = [&](int lane, Cell cell) {
                return core::Add(rooms[stock[lane]].center,
                                 core::Add(Turn(core::Subtract(cell, {1, 0}), stockTurn), {1, 0}));
            };
            io::SolutionData machine;
            machine.puzzleName = puzzle.name;
            machine.solutionName = "Finite production coproduct stock";
            const auto add = [&](const std::string& name, Cell position, std::vector<int> body,
                                 std::vector<io::ArmInstruction> tape =
                                     std::vector<io::ArmInstruction>{}) {
                machine.objects.push_back({name, position, std::move(tape), std::move(body)});
            };
            const auto portal = [&](int id, Cell a, Cell b) {
                for (auto position : {a, b})
                    add("pipe", position, {1, 0, 0, 0, 0, id, 1, 0, 0});
            };
            portal(identity(source, stock[0]), core::Add(center, {-2, 0}), stockCell(0, {1, 1}));
            portal(identity(source, stock[1]), core::Add(center, {2, 0}), stockCell(1, {1, 1}));
            portal(identity(stock[0], stock[1]), stockCell(0, {search.FinalBase(), 0}),
                   stockCell(1, {search.FinalBase(), 0}));
            add("input", feed->origin, {1, feed->Turns(), 0, 0, 0});
            add("glyph-unification", center, {1, 0, 0, 0, 0});
            for (const auto& bond : puzzle.inputs[0].bonds) {
                const auto from = feed->ToWorld(bond.from), to = feed->ToWorld(bond.to);
                int direction = 0;
                while (direction < 6 && core::Add(from, directions[direction]) != to)
                    ++direction;
                add("unbonder", from, {1, direction, 0, 0, 0});
            }
            for (int output = 0; output < 2; ++output) {
                const auto position = core::Add(center, output == 0 ? Cell{-3, 1} : Cell{-2, -1});
                add("out-std", core::Subtract(position, puzzle.outputs[output].atoms[0].position),
                    {1, 0, output, 0, 0});
            }
            std::vector<io::ArmInstruction> left, right;
            const std::string actions = "GegEEGeergREEGeeRgrE";
            for (int batch = 0; batch < demand; ++batch) {
                for (std::size_t i = 0; i < actions.size(); ++i)
                    left.push_back({actions[i], 20 * batch + static_cast<int>(i)});
                for (auto [action, time] :
                     std::array<std::pair<char, int>, 4>{{{'G', 0}, {'e', 8}, {'g', 9}, {'E', 10}}})
                    right.push_back({action, 20 * batch + time});
            }
            right.push_back({'O', period - 1});
            add("piston", core::Add(center, {-3, 0}), {2, 0, 0, period, 0}, left);
            add("piston", core::Add(center, {3, 0}), {2, 3, 0, period, 1}, right);
            for (int lane = 0; lane < 2; ++lane) {
                const int trackDirection = stockTurn == 0 ? 1 : -1;
                add("track", stockCell(lane, {0, 0}),
                    {1, 0, 0, 0, 3, 0, 0, trackDirection, 0, 2 * trackDirection, 0, 0});
                std::vector<io::ArmInstruction> tape;
                for (int batch = 0; batch < demand - 1; ++batch)
                    for (std::size_t i = 0; i < packing[batch].size(); ++i)
                        tape.push_back({packing[batch][i],
                                        (lane == 0 ? 3 : 10) + 20 * batch + static_cast<int>(i)});
                tape.push_back({'O', period - 1});
                add("piston", stockCell(lane, {2, 0}),
                    {1, (2 + stockTurn) % 6, 0, period, 2 + lane}, tape);
            }
            if (accept(machine)) return machine;
        }
    }
    return {};
}
} // namespace opusoptima::solvers
