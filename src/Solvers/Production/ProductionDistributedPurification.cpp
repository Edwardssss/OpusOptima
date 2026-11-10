#include "ProductionDistributedPurification.hpp"
#include "HexFrame.hpp"
#include "ProductionLayout.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <map>
#include <queue>
#include <set>
#include <stdexcept>
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
    return core::HexFrame{{}, rotation}.ToWorld(cell);
}
bool Bounded(Cell cell) {
    return cell.q >= -120 && cell.q <= 120 && cell.r >= -120 && cell.r <= 120;
}
struct Model {
    std::vector<std::pair<int, Cell>> atoms;
    std::vector<std::pair<int, int>> edges;
    io::Molecule Molecule() const {
        io::Molecule result;
        for (const auto& [type, cell] : atoms)
            result.atoms.push_back({static_cast<core::AtomType>(type), cell});
        for (auto [a, b] : edges)
            result.bonds.push_back({1, atoms[a].second, atoms[b].second});
        return result;
    }
};
const std::array<Model, 4> feeds{
    {{{{9, {0, 0}}, {2, {-1, 1}}, {2, {1, 0}}, {2, {1, -1}}, {4, {-1, 0}}},
      {{0, 2}, {1, 0}, {2, 3}, {4, 0}, {1, 4}, {0, 3}}},
     {{{4, {-1, 1}}, {10, {0, 0}}, {4, {1, 0}}}, {{1, 2}, {0, 1}}},
     {{{10, {0, 0}}, {8, {1, 0}}, {4, {2, 0}}}, {{0, 1}, {1, 2}}},
     {{{2, {0, 0}}, {8, {-1, 1}}, {9, {1, 0}}, {9, {0, -1}}}, {{0, 2}, {0, 3}, {1, 0}}}}};
const Model cardinalProduct{{{4, {0, 1}}, {4, {-1, 0}}, {2, {0, 0}}, {2, {1, 1}}},
                            {{0, 3}, {0, 2}, {1, 2}}};
const Model metalProduct{{{7, {0, 0}}}, {}};
const std::array<std::vector<Cell>, 3> pipeShapes{
    {{{0, 0}}, {{1, 0}, {0, 0}}, {{0, -1}, {1, 0}, {0, 0}}}};
using Edge = std::pair<Key, Key>;
Edge EdgeOf(Cell a, Cell b) {
    auto x = KeyOf(a), y = KeyOf(b);
    return {std::min(x, y), std::max(x, y)};
}
std::optional<core::HexFrame> MatchMolecule(const io::Molecule& actual, const Model& model) {
    if (actual.atoms.size() != model.atoms.size() || actual.bonds.size() != model.edges.size())
        return {};
    for (const auto& atom : actual.atoms)
        if (!Bounded(atom.position)) return {};
    std::map<Key, int> expected;
    std::set<Edge> expectedEdges;
    for (auto [type, cell] : model.atoms)
        expected.emplace(KeyOf(cell), type);
    for (auto [a, b] : model.edges)
        expectedEdges.insert(EdgeOf(model.atoms[a].second, model.atoms[b].second));
    for (int rotation = 0; rotation < 6; ++rotation)
        for (const auto& anchor : actual.atoms) {
            if (static_cast<int>(anchor.type) != model.atoms.front().first) continue;
            const core::HexFrame frame{
                core::Subtract(model.atoms.front().second, Turn(anchor.position, rotation)),
                rotation};
            std::map<Key, int> atoms;
            for (const auto& atom : actual.atoms)
                if (!atoms.emplace(KeyOf(frame.ToWorld(atom.position)), static_cast<int>(atom.type))
                         .second)
                    break;
            if (atoms != expected) continue;
            std::set<Edge> edges;
            bool valid = true;
            for (const auto& bond : actual.bonds) {
                if (bond.bits != 1 || !Bounded(bond.from) || !Bounded(bond.to)) {
                    valid = false;
                    break;
                }
                valid &=
                    edges.insert(EdgeOf(frame.ToWorld(bond.from), frame.ToWorld(bond.to))).second;
            }
            if (valid && edges == expectedEdges) return frame;
        }
    return {};
}
std::optional<core::HexFrame> MatchPipe(const std::vector<Cell>& actual,
                                        const std::vector<Cell>& expected) {
    if (actual.size() != expected.size() || actual.empty()) return {};
    for (auto cell : actual)
        if (!Bounded(cell)) return {};
    for (int rotation = 0; rotation < 6; ++rotation) {
        const core::HexFrame frame{core::Subtract(expected.front(), Turn(actual.front(), rotation)),
                                   rotation};
        bool matches = true;
        for (std::size_t i = 0; i < actual.size(); ++i)
            matches &= frame.ToWorld(actual[i]) == expected[i];
        if (matches) return frame;
    }
    return {};
}
struct Binding {
    std::array<std::size_t, 4> input{}, room{};
    std::array<core::HexFrame, 4> inputFrame;
    std::array<std::size_t, 2> output{};
    std::array<core::HexFrame, 2> outputFrame;
    std::array<std::size_t, 3> pipe{};
    std::array<core::HexFrame, 3> pipeFrame;
    std::vector<ProductionChamberLayout> chambers;
};
std::optional<Binding> Describe(const io::PuzzleData& puzzle) {
    constexpr auto required = (1ULL << 2) | (1ULL << 3) | (1ULL << 8) | (1ULL << 9) | (1ULL << 15) |
                              (1ULL << 22) | (1ULL << 23) | (1ULL << 25);
    if (puzzle.version != 3 || !puzzle.productionInfo || puzzle.productionInfo->isolation ||
        puzzle.outputMultiplier < 1 || puzzle.outputMultiplier > 2 || puzzle.inputs.size() != 4 ||
        puzzle.outputs.size() != 2 || puzzle.productionInfo->chambers.size() != 4 ||
        puzzle.productionInfo->conduits.size() != 3 ||
        (puzzle.permissionBits & required) != required)
        return {};
    Binding result;
    std::set<std::size_t> used;
    for (std::size_t model = 0; model < feeds.size(); ++model) {
        bool matched = false;
        for (std::size_t input = 0; input < puzzle.inputs.size(); ++input) {
            if (used.contains(input)) continue;
            const auto frame = MatchMolecule(puzzle.inputs[input], feeds[model]);
            if (!frame) continue;
            result.input[model] = input;
            result.inputFrame[model] = *frame;
            used.insert(input);
            matched = true;
            break;
        }
        if (!matched) return {};
    }
    used.clear();
    for (std::size_t model = 0; model < 2; ++model) {
        bool matched = false;
        for (std::size_t output = 0; output < puzzle.outputs.size(); ++output) {
            if (used.contains(output)) continue;
            const auto frame =
                MatchMolecule(puzzle.outputs[output], model == 0 ? metalProduct : cardinalProduct);
            if (!frame) continue;
            result.output[model] = output;
            result.outputFrame[model] = *frame;
            used.insert(output);
            matched = true;
            break;
        }
        if (!matched) return {};
    }
    result.chambers = PlanProductionChambers(puzzle);
    std::optional<std::size_t> large, small;
    std::vector<std::size_t> wide;
    for (std::size_t i = 0; i < result.chambers.size(); ++i) {
        const auto& type = puzzle.productionInfo->chambers[i].chamberType;
        if (type == "Large" && !large)
            large = i;
        else if (type == "SmallWide" && !small)
            small = i;
        else if (type == "SmallWider")
            wide.push_back(i);
        else
            return {};
    }
    if (!large || !small || wide.size() != 2) return {};
    const auto roomOf = [&](Cell cell) -> std::optional<std::size_t> {
        for (std::size_t i = 0; i < result.chambers.size(); ++i)
            if (ContainsProductionCell(result.chambers[i], cell)) return i;
        return {};
    };
    std::optional<std::size_t> relay, source;
    std::set<std::size_t> roles;
    for (std::size_t i = 0; i < puzzle.productionInfo->conduits.size(); ++i) {
        const auto& pipe = puzzle.productionInfo->conduits[i];
        const auto a = roomOf(pipe.positionA), b = roomOf(pipe.positionB);
        if (!a || !b || a == b) return {};
        std::optional<std::size_t> role;
        if ((*a == *large && *b == *small) || (*b == *large && *a == *small))
            role = 1;
        else if (*a == *large || *b == *large) {
            const auto other = *a == *large ? *b : *a;
            if (std::find(wide.begin(), wide.end(), other) == wide.end()) return {};
            role = 2;
            relay = other;
        } else if (std::find(wide.begin(), wide.end(), *a) != wide.end() &&
                   std::find(wide.begin(), wide.end(), *b) != wide.end())
            role = 0;
        if (!role || !roles.insert(*role).second) return {};
        const auto frame = MatchPipe(pipe.path, pipeShapes[*role]);
        if (!frame) return {};
        result.pipe[*role] = i;
        result.pipeFrame[*role] = *frame;
    }
    if (roles.size() != 3 || !relay) return {};
    source = wide.front() == *relay ? wide.back() : wide.front();
    result.room = {*source, *small, *relay, *large};
    return result;
}
struct Pose {
    int base{}, length{1}, direction{};
    auto operator<=>(const Pose&) const = default;
};
struct Atom {
    int type{};
    Cell cell;
};
struct Layout {
    std::vector<Cell> track;
    Pose initial;
    core::HexFrame input;
    std::map<int, core::HexFrame> ports;
};
struct Point {
    double x{}, y{};
};
Point XY(Cell cell) {
    return {cell.q + cell.r * .5, cell.r * std::sqrt(3.) * .5};
}
int SourceId(int input, int atom) {
    return 1 + input * 10 + atom;
}

class Module {
    public:
    Module(int input, Layout layout, int radius, int stretch)
        : input_(input), layout_(std::move(layout)), radius_(radius), stretch_(stretch),
          pose_(layout_.initial) {
        for (std::size_t i = 0; i < feeds[input_].atoms.size(); ++i) {
            const auto& [type, cell] = feeds[input_].atoms[i];
            const auto position = layout_.input.ToWorld(cell);
            feed_.push_back(position);
            world_.emplace(SourceId(input_, i), Atom{type, position});
        }
    }
    const Layout& Geometry() const {
        return layout_;
    }
    const auto& World() const {
        return world_;
    }
    std::vector<std::pair<Cell, Cell>> bonds;
    bool Inside(Cell cell) const {
        for (int shift = 0; shift <= stretch_; ++shift)
            if (core::Distance({shift, 0}, cell) <= radius_) return true;
        return false;
    }
    Cell Tip(Pose pose) const {
        return core::Add(layout_.track.at(pose.base), Turn({pose.length, 0}, pose.direction));
    }
    bool ValidGeometry() const {
        if (!Inside(Tip(pose_))) return false;
        const auto base = layout_.track.at(pose_.base);
        std::set<Key> occupied{KeyOf(base)};
        for (auto cell : feed_)
            if (!Inside(cell) || !occupied.insert(KeyOf(cell)).second) return false;
        for (const auto& [port, frame] : layout_.ports) {
            const int shape = port == 0 ? (input_ == 0 ? 0 : input_ == 1 ? 1 : 2) : 0;
            for (auto cell : pipeShapes[shape])
                if (!Inside(frame.ToWorld(cell)) ||
                    !occupied.insert(KeyOf(frame.ToWorld(cell))).second)
                    return false;
        }
        return true;
    }
    void Receive(int id, int type, int port) {
        const auto cell = layout_.ports.at(port).origin;
        if (std::any_of(world_.begin(), world_.end(),
                        [&](const auto& entry) { return entry.second.cell == cell; }))
            throw std::invalid_argument("distributed arrival footprint occupied");
        world_.emplace(id, Atom{type, cell});
    }
    void Add(int id, int type, Cell cell) {
        if (std::any_of(world_.begin(), world_.end(),
                        [&](const auto& entry) { return entry.second.cell == cell; }))
            throw std::invalid_argument("distributed reaction product occupied");
        world_.emplace(id, Atom{type, cell});
    }
    void Remove(int id) {
        world_.erase(id);
    }
    std::string Move(int id, Cell goal, bool remove = false) {
        if (!world_.contains(id)) throw std::invalid_argument("distributed operand absent");
        auto approach = Route(Contacts(world_.at(id).cell), {}, {});
        if (!approach) throw std::invalid_argument("distributed pickup unreachable");
        auto delivery = Route(Contacts(goal), id, goal);
        if (!delivery) throw std::invalid_argument("distributed delivery unreachable");
        if (remove) world_.erase(id);
        auto reset = Route({layout_.initial}, {}, {});
        if (!reset) throw std::invalid_argument("distributed reset unreachable");
        return *approach + "G" + *delivery + "g" + *reset;
    }

    private:
    bool Inside(Point point) const {
        const double r = point.y * 2 / std::sqrt(3.), q = point.x - r * .5;
        for (int shift = 0; shift <= stretch_; ++shift)
            if (std::max({std::abs(q - shift), std::abs(r), std::abs(q - shift + r)}) <=
                radius_ + .25)
                return true;
        return false;
    }
    std::vector<Pose> Contacts(Cell cell) const {
        std::vector<Pose> result;
        for (int base = 0; base < static_cast<int>(layout_.track.size()); ++base)
            for (int length = 1; length <= 3; ++length)
                for (int direction = 0; direction < 6; ++direction) {
                    const Pose pose{base, length, direction};
                    if (Tip(pose) == cell) result.push_back(pose);
                }
        return result;
    }
    bool Legal(Pose from, Pose to, char action, const std::vector<Cell>& occupied, bool held,
               Cell goal) const {
        if (!Inside(Tip(to))) return false;
        const auto oldBase = XY(layout_.track[from.base]), newBase = XY(layout_.track[to.base]);
        const auto oldTip = XY(Tip(from)), newTip = XY(Tip(to));
        for (int sample = 0; sample <= 8; ++sample) {
            const double fraction = sample / 8.;
            const Point base{oldBase.x + (newBase.x - oldBase.x) * fraction,
                             oldBase.y + (newBase.y - oldBase.y) * fraction};
            Point point{oldTip.x + (newTip.x - oldTip.x) * fraction,
                        oldTip.y + (newTip.y - oldTip.y) * fraction};
            if (action == 'r' || action == 'R') {
                const double angle = (action == 'r' ? 1 : -1) * std::acos(-1.) / 3 * fraction;
                const double x = oldTip.x - oldBase.x, y = oldTip.y - oldBase.y;
                point = {oldBase.x + x * std::cos(angle) - y * std::sin(angle),
                         oldBase.y + x * std::sin(angle) + y * std::cos(angle)};
            }
            if (!Inside(point)) return false;
            for (auto cell : occupied) {
                const auto obstacle = XY(cell);
                if ((obstacle.x - base.x) * (obstacle.x - base.x) +
                        (obstacle.y - base.y) * (obstacle.y - base.y) <
                    .205)
                    return false;
                if (held && (obstacle.x - point.x) * (obstacle.x - point.x) +
                                    (obstacle.y - point.y) * (obstacle.y - point.y) <
                                .365)
                    return false;
            }
        }
        if (held && Tip(to) != goal)
            for (auto [a, b] : bonds) {
                const auto cell = Tip(to);
                const auto other = cell == a   ? std::optional<Cell>{b}
                                   : cell == b ? std::optional<Cell>{a}
                                               : std::nullopt;
                if (other && std::find(occupied.begin(), occupied.end(), *other) != occupied.end())
                    return false;
            }
        return true;
    }
    std::optional<std::string> Route(const std::vector<Pose>& destinations, std::optional<int> held,
                                     Cell goal) {
        std::vector<Cell> occupied;
        for (const auto& [id, atom] : world_)
            if (!held || id != *held) occupied.push_back(atom.cell);
        const bool last = held && std::none_of(occupied.begin(), occupied.end(), [&](Cell cell) {
                              return std::find(feed_.begin(), feed_.end(), cell) != feed_.end();
                          });
        using State = std::pair<Pose, bool>;
        struct Parent {
            State previous;
            char action{};
        };
        const State initial{pose_, false};
        std::map<State, Parent> parent{{initial, {initial, 0}}};
        std::queue<State> queue;
        queue.push(initial);
        std::optional<State> found;
        while (!queue.empty()) {
            const auto [from, spawned] = queue.front();
            queue.pop();
            if (std::find(destinations.begin(), destinations.end(), from) != destinations.end()) {
                found = State{from, spawned};
                break;
            }
            std::vector<std::pair<Pose, char>> next{
                {{from.base, from.length, (from.direction + 1) % 6}, 'r'},
                {{from.base, from.length, (from.direction + 5) % 6}, 'R'}};
            if (from.length < 3)
                next.push_back({{from.base, from.length + 1, from.direction}, 'E'});
            if (from.length > 1)
                next.push_back({{from.base, from.length - 1, from.direction}, 'e'});
            if (from.base + 1 < static_cast<int>(layout_.track.size()))
                next.push_back({{from.base + 1, from.length, from.direction}, 'A'});
            if (from.base > 0) next.push_back({{from.base - 1, from.length, from.direction}, 'a'});
            for (const auto& [to, action] : next) {
                const bool spawn = spawned || (last && std::find(feed_.begin(), feed_.end(),
                                                                 Tip(to)) == feed_.end());
                const State state{to, spawn};
                if (parent.contains(state)) continue;
                auto obstacles = occupied;
                if (spawned) obstacles.insert(obstacles.end(), feed_.begin(), feed_.end());
                if (!Legal(from, to, action, obstacles, held.has_value(), goal)) continue;
                if (spawn && !spawned) {
                    const auto point = XY(Tip(to)), base = XY(layout_.track[to.base]);
                    bool clear = true;
                    for (auto cell : feed_) {
                        const auto obstacle = XY(cell);
                        if ((point.x - obstacle.x) * (point.x - obstacle.x) +
                                    (point.y - obstacle.y) * (point.y - obstacle.y) <
                                .365 ||
                            (base.x - obstacle.x) * (base.x - obstacle.x) +
                                    (base.y - obstacle.y) * (base.y - obstacle.y) <
                                .205)
                            clear = false;
                    }
                    if (!clear) continue;
                }
                parent.emplace(state, Parent{{from, spawned}, action});
                queue.push(state);
            }
        }
        if (!found) return {};
        std::string tape;
        auto state = *found;
        while (state != initial) {
            const auto entry = parent.at(state);
            tape.push_back(entry.action);
            state = entry.previous;
        }
        std::reverse(tape.begin(), tape.end());
        pose_ = found->first;
        if (held) world_.at(*held).cell = Tip(pose_);
        if (found->second) {
            ++generation_;
            for (std::size_t i = 0; i < feed_.size(); ++i)
                world_.emplace(1000 + input_ * 10000 + generation_ * 10 + i,
                               Atom{feeds[input_].atoms[i].first, feed_[i]});
        }
        return tape;
    }
    int input_{};
    Layout layout_;
    int radius_{}, stretch_{}, generation_{};
    Pose pose_;
    std::vector<Cell> feed_;
    std::map<int, Atom> world_;
};

enum class TaskKind { Take, ReactOutput, ReactStore, ReactKeep, Fresh, Stored, Cardinal, Output };
struct Task {
    TaskKind kind;
    int input{}, atom{}, destination{};
};
const std::array<Task, 24> tasks{{{TaskKind::Take, 2, 1, 0},     {TaskKind::Take, 3, 1, 1},
                                  {TaskKind::ReactOutput},       {TaskKind::Take, 0, 0, 0},
                                  {TaskKind::Take, 3, 2, 1},     {TaskKind::ReactStore},
                                  {TaskKind::Take, 1, 1, 0},     {TaskKind::Take, 2, 0, 1},
                                  {TaskKind::ReactKeep},         {TaskKind::Fresh, 0, 0, 0},
                                  {TaskKind::Take, 3, 3, 1},     {TaskKind::ReactKeep},
                                  {TaskKind::Stored, 0, 0, 0},   {TaskKind::Fresh, 0, 0, 1},
                                  {TaskKind::ReactOutput},       {TaskKind::Cardinal, 0, 4, 0},
                                  {TaskKind::Cardinal, 0, 1, 2}, {TaskKind::Cardinal, 0, 2, 3},
                                  {TaskKind::Cardinal, 1, 0, 1}, {TaskKind::Output},
                                  {TaskKind::Cardinal, 1, 2, 0}, {TaskKind::Cardinal, 0, 3, 2},
                                  {TaskKind::Cardinal, 3, 0, 3}, {TaskKind::Cardinal, 2, 2, 1}}};
struct Event {
    int module{};
    std::string tape;
};
struct Sources {
    std::array<Layout, 3> layout;
    std::array<std::map<int, std::string>, 3> tapes;
};
Layout Transform(Layout layout, core::HexFrame frame) {
    for (auto& cell : layout.track)
        cell = frame.ToWorld(cell);
    layout.initial.direction = (layout.initial.direction + frame.Turns()) % 6;
    layout.input = frame.Compose(layout.input);
    for (auto& [port, placement] : layout.ports)
        placement = frame.Compose(placement);
    return layout;
}
std::optional<Sources> PlanSources(unsigned symmetry) {
    Sources result;
    result.layout = {{{{{0, 0}, {1, 0}, {2, 0}}, {0, 1, 0}, {{2, 0}, 2}, {{0, {{1, -1}, 0}}}},
                      {{{0, -1}, {1, -1}, {2, -1}}, {0, 1, 0}, {{1, 0}, 5}, {{0, {{-1, 1}, 0}}}},
                      {{{2, -1}, {1, -1}, {0, -1}},
                       {0, 1, 3},
                       {{0, 1}, 4},
                       {{0, {{2, 1}, 5}}, {1, {{1, -1}, 5}}}}}};
    try {
        for (int i = 0; i < 3; ++i) {
            const int stretch = i == 1 ? 1 : 2;
            if (symmetry & (1U << i))
                result.layout[i] = Transform(result.layout[i], {{stretch, 0}, 3});
            Module module(i, result.layout[i], 1, stretch);
            if (!module.ValidGeometry()) return {};
            for (const auto& task : tasks) {
                if (task.kind != TaskKind::Take && task.kind != TaskKind::Cardinal) continue;
                if (task.input != i && !(i == 2 && task.input == 0)) continue;
                const int id = SourceId(task.input, task.atom);
                if (i == 2 && task.input == 0)
                    module.Receive(id, feeds[0].atoms[task.atom].first, 1);
                result.tapes[i][id] = module.Move(id, result.layout[i].ports.at(0).origin, true);
            }
        }
    } catch (const std::invalid_argument&) {
        return {};
    }
    return result;
}
struct CentralPlan {
    Layout layout;
    std::vector<Event> events;
    core::HexFrame frame;
};
std::optional<CentralPlan> PlanCentral(const Sources& sources, int rotation, int bufferChoice) {
    const core::HexFrame frame{{}, rotation};
    Layout layout{{{-1, 1}, {0, 1}, {1, 1}},
                  {0, 1, 4},
                  {{-2, 2}, 2},
                  {{0, {{-1, -2}, 1}}, {1, {{2, -2}, 1}}}};
    layout = Transform(std::move(layout), frame);
    Module module(3, layout, 3, 0);
    const std::array<Cell, 3> buffers{{{-3, 1}, {-3, 2}, {-2, -1}}};
    const auto buffer = frame.ToWorld(buffers.at(bufferChoice));
    const auto first = frame.ToWorld({0, -1}), second = frame.ToWorld({1, -1}),
               product = frame.ToWorld({0, 0});
    std::array<Cell, 4> output;
    for (std::size_t i = 0; i < output.size(); ++i)
        output[i] = frame.ToWorld(cardinalProduct.atoms[i].second);
    for (auto [a, b] : cardinalProduct.edges)
        module.bonds.push_back({output[a], output[b]});
    std::vector<Event> events;
    int fresh = 0, stored = 0, reaction = 0, products = 0;
    try {
        for (const auto& task : tasks) {
            if (task.kind == TaskKind::Take || task.kind == TaskKind::Cardinal) {
                const int id = SourceId(task.input, task.atom),
                          type = feeds[task.input].atoms[task.atom].first;
                if (task.input != 3) {
                    if (task.input == 0) {
                        events.push_back({0, sources.tapes[0].at(id)});
                        events.push_back({2, sources.tapes[2].at(id)});
                    } else
                        events.push_back({task.input, sources.tapes[task.input].at(id)});
                    module.Receive(id, type, task.input == 1 ? 0 : 1);
                }
                const auto goal = task.kind == TaskKind::Take
                                      ? (task.destination == 0 ? first : second)
                                      : output[task.destination];
                events.push_back({3, module.Move(id, goal)});
            } else if (task.kind == TaskKind::ReactOutput || task.kind == TaskKind::ReactKeep ||
                       task.kind == TaskKind::ReactStore) {
                std::vector<std::pair<int, int>> operands;
                for (const auto& [id, atom] : module.World())
                    if (atom.cell == first || atom.cell == second)
                        operands.push_back({id, atom.type});
                if (operands.size() != 2 || operands[0].second != operands[1].second ||
                    operands[0].second < 8 || operands[0].second > 12)
                    return {};
                const int type = operands[0].second - 1;
                for (auto [id, unused] : operands)
                    module.Remove(id);
                fresh = 100 + reaction++;
                if (type == 7)
                    ++products;
                else
                    module.Add(fresh, type, product);
                events.push_back({3, "OO"});
                if (task.kind == TaskKind::ReactStore) {
                    stored = fresh;
                    events.push_back({3, module.Move(fresh, buffer)});
                }
            } else if (task.kind == TaskKind::Fresh || task.kind == TaskKind::Stored) {
                events.push_back({3, module.Move(task.kind == TaskKind::Fresh ? fresh : stored,
                                                 task.destination == 0 ? first : second)});
            } else if (task.kind == TaskKind::Output) {
                for (auto cell : output) {
                    const auto atom =
                        std::find_if(module.World().begin(), module.World().end(),
                                     [&](const auto& entry) { return entry.second.cell == cell; });
                    if (atom == module.World().end()) return {};
                    module.Remove(atom->first);
                }
                events.push_back({3, "OO"});
            }
        }
        for (std::size_t i = 0; i < output.size(); ++i) {
            const auto atom =
                std::find_if(module.World().begin(), module.World().end(), [&](const auto& entry) {
                    return entry.second.cell == output[i] &&
                           entry.second.type == cardinalProduct.atoms[i].first;
                });
            if (atom == module.World().end()) return {};
            module.Remove(atom->first);
        }
        events.push_back({3, "OO"});
        if (reaction != 5 || products != 2 || module.World().size() != feeds[3].atoms.size())
            return {};
    } catch (const std::invalid_argument&) {
        return {};
    }
    return CentralPlan{std::move(layout), std::move(events), frame};
}

io::SolutionData Emit(const io::PuzzleData& puzzle, const Binding& binding, const Sources& sources,
                      const CentralPlan& central) {
    std::array<Layout, 4> layout{sources.layout[0], sources.layout[1], sources.layout[2],
                                 central.layout};
    std::array<std::vector<io::ArmInstruction>, 4> tapes;
    std::array<int, 4> available{1, 1, 1, 1};
    int cursor = 1;
    for (auto& tape : tapes)
        tape.push_back({'O', 0});
    for (const auto& event : central.events) {
        const int begin = std::max(cursor, available[event.module]);
        for (std::size_t i = 0; i < event.tape.size(); ++i)
            tapes[event.module].push_back({event.tape[i], begin + static_cast<int>(i)});
        available[event.module] = begin + static_cast<int>(event.tape.size());
        const auto release = event.tape.find_last_of('g');
        cursor = begin +
                 static_cast<int>(release == std::string::npos ? event.tape.size() : release + 1);
    }
    const int period = *std::max_element(available.begin(), available.end()) + 1;
    io::SolutionData machine;
    machine.puzzleName = puzzle.name;
    machine.solutionName = "Distributed production purification";
    const auto add = [&](const char* name, Cell cell, std::vector<int> body,
                         std::vector<io::ArmInstruction> tape = {}) {
        machine.objects.push_back({name, cell, std::move(tape), std::move(body)});
    };
    for (std::size_t i = 0; i < layout.size(); ++i) {
        const auto center = binding.chambers[binding.room[i]].center;
        const auto world = [&](Cell cell) { return core::Add(center, cell); };
        auto tape = tapes[i];
        tape.push_back({'O', period - 1});
        add("piston", world(layout[i].track.front()),
            {layout[i].initial.length, layout[i].initial.direction, 0, period, static_cast<int>(i)},
            std::move(tape));
        std::vector<int> track{1, 0, 0, 0, static_cast<int>(layout[i].track.size())};
        for (auto cell : layout[i].track) {
            const auto local = core::Subtract(cell, layout[i].track.front());
            track.push_back(local.q);
            track.push_back(local.r);
        }
        track.push_back(0);
        add("track", world(layout[i].track.front()), std::move(track));
        const auto inputFrame = layout[i].input.Compose(binding.inputFrame[i]);
        add("input", world(inputFrame.origin),
            {1, inputFrame.Turns(), static_cast<int>(binding.input[i]), 0, 0});
        for (auto [a, b] : feeds[i].edges) {
            const auto x = layout[i].input.ToWorld(feeds[i].atoms[a].second);
            const auto y = layout[i].input.ToWorld(feeds[i].atoms[b].second);
            const auto direction =
                std::find(directions.begin(), directions.end(), core::Subtract(y, x)) -
                directions.begin();
            add("unbonder", world(x), {1, static_cast<int>(direction), 0, 0, 0});
        }
    }
    const std::array<std::array<int, 4>, 3> connections{{{0, 0, 2, 1}, {1, 0, 3, 0}, {2, 0, 3, 1}}};
    for (std::size_t role = 0; role < connections.size(); ++role) {
        const auto& path = puzzle.productionInfo->conduits[binding.pipe[role]].path;
        for (int endpoint = 0; endpoint < 2; ++endpoint) {
            const auto module = connections[role][2 * endpoint],
                       port = connections[role][2 * endpoint + 1];
            const auto frame = layout[module].ports.at(port).Compose(binding.pipeFrame[role]);
            std::vector<int> body{1,
                                  frame.Turns(),
                                  0,
                                  0,
                                  0,
                                  100 + static_cast<int>(binding.pipe[role]),
                                  static_cast<int>(path.size())};
            for (auto cell : path) {
                body.push_back(cell.q);
                body.push_back(cell.r);
            }
            add("pipe", core::Add(binding.chambers[binding.room[module]].center, frame.origin),
                std::move(body));
        }
    }
    const auto center = binding.chambers[binding.room[3]].center;
    const core::HexFrame frame{center, central.frame.Turns()};
    add("glyph-purification", frame.ToWorld({0, -1}), {1, frame.Turns(), 0, 0, 0});
    for (std::size_t output = 0; output < 2; ++output) {
        const auto placement = frame.Compose(binding.outputFrame[output]);
        add("out-std", placement.origin,
            {1, placement.Turns(), static_cast<int>(binding.output[output]), 0, 0});
    }
    for (auto [a, b] : cardinalProduct.edges) {
        const auto x = frame.ToWorld(cardinalProduct.atoms[a].second),
                   y = frame.ToWorld(cardinalProduct.atoms[b].second);
        const auto direction =
            std::find(directions.begin(), directions.end(), core::Subtract(y, x)) -
            directions.begin();
        add("bonder", x, {1, static_cast<int>(direction), 0, 0, 0});
    }
    return machine;
}
} // namespace

std::optional<io::SolutionData> CompileAcceptedProductionDistributedPurification(
    const io::PuzzleData& puzzle, const std::function<bool(const io::SolutionData&)>& accept) {
    std::optional<Binding> binding;
    try {
        binding = Describe(puzzle);
    } catch (const std::invalid_argument&) {
        return {};
    }
    if (!binding) return {};
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(8);
    unsigned attempts = 0;
    for (unsigned symmetry = 0; symmetry < 8; ++symmetry) {
        const auto sources = PlanSources(symmetry);
        if (!sources) continue;
        for (int rotation = 0; rotation < 6; ++rotation)
            for (int buffer = 0; buffer < 3; ++buffer) {
                if (attempts >= 8 || std::chrono::steady_clock::now() >= deadline) return {};
                const auto central = PlanCentral(*sources, rotation, buffer);
                if (!central) continue;
                auto machine = Emit(puzzle, *binding, *sources, *central);
                ++attempts;
                if (accept(machine)) return machine;
            }
    }
    return {};
}
} // namespace opusoptima::solvers
