#include "FixedArmIntervals.hpp"
#include "FragmentCustody.hpp"
#include "HexFrame.hpp"
#include "SerialArmExecution.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <set>
#include <sstream>

namespace opusoptima::solvers {
namespace {
using IR = ExecutionIR;
using K = IR::Kind;
using C = core::HexCoord;
C Turn(C c, int n) {
    return core::HexFrame{{}, n}.ToWorld(c);
}
bool Interval(const IR::State& state, C base, std::vector<C> obstacles, HeldArmState from,
              HeldArmState to, char instruction, bool holding, std::string* contact = nullptr) {
    std::erase(obstacles, base);
    const bool certified =
        CertifyFixedArmInterval(state, base, obstacles, from, to, instruction, holding);
    if (!holding) return certified;
    const auto tip = core::Add(base, Turn({from.length, 0}, from.direction));
    const auto grip = std::find_if(state.atoms.begin(), state.atoms.end(),
                                   [&](const auto& entry) { return entry.second.position == tip; });
    if (grip == state.atoms.end()) return false;
    const auto moving = IR::Fragment(state, grip->first);
    std::vector<C> stationary;
    for (const auto& [id, atom] : state.atoms)
        if (std::find(moving.begin(), moving.end(), id) == moving.end())
            stationary.push_back(atom.position);
    for (C cell : obstacles)
        if (std::none_of(moving.begin(), moving.end(),
                         [&](auto id) { return state.atoms.at(id).position == cell; }))
            stationary.push_back(cell);
    // A full unit of center clearance conservatively covers native atom radii.
    // Radial distance alone misses near contacts at the middle of a sixty-degree arc.
    const double tau = 2 * std::numbers::pi;
    const auto xy = [](C c) { return std::pair{c.q + c.r * 0.5, c.r * std::sqrt(3.0) * 0.5}; };
    for (auto id : moving) {
        const C cell = state.atoms.at(id).position;
        const auto [x, y] = xy(core::Subtract(cell, base));
        const double radius = std::hypot(x, y), start = std::atan2(y, x);
        const auto [endX, endY] = xy(Turn(core::Subtract(cell, base), instruction == 'r' ? 1 : 5));
        for (C other : stationary) {
            const auto [ox, oy] = xy(core::Subtract(other, base));
            double phase = std::atan2(oy, ox) - start;
            if (instruction != 'r') phase = -phase;
            phase = std::fmod(phase + tau, tau);
            const double distance =
                phase <= std::numbers::pi / 3
                    ? std::abs(std::hypot(ox, oy) - radius)
                    : std::min(std::hypot(ox - x, oy - y), std::hypot(ox - endX, oy - endY));
            if (distance < 1 - 1e-9) {
                if (contact && contact->empty()) {
                    std::ostringstream detail;
                    detail << "atom=" << id << " from=(" << cell.q << ',' << cell.r << ") base=("
                           << base.q << ',' << base.r << ") obstacle=(" << other.q << ',' << other.r
                           << ") instruction=" << instruction
                           << " minimum-center-distance=" << distance;
                    *contact = detail.str();
                }
                return false;
            }
        }
    }
    return certified;
}
struct Relay {
    C base, destination;
    int length, direction, turn;
    std::size_t output;
};
void CheckDeadline(std::chrono::steady_clock::time_point deadline) {
    if (std::chrono::steady_clock::now() >= deadline)
        throw IR::BudgetFailure("whole feed docking deadline exhausted");
}

io::SolutionData Dock(const io::PuzzleData& p, const IR& material,
                      std::chrono::steady_clock::time_point deadline,
                      const std::function<void(const std::string&)>& feedback,
                      const std::function<bool()>& beginConstruction) {
    if (!SupportsWholeFeedDocking(p) || material.steps.size() > 512)
        throw IR::Unsupported("whole feed docking requires a bounded ordinary scalar period");
    const auto terminal = material.Replay(p);
    IR::RequireComplete(terminal, std::vector<std::size_t>(p.outputs.size(), 1));
    if (terminal.batches != std::vector<std::size_t>{1} || terminal.discarded)
        throw IR::Unsupported("whole feed docking requires exactly one conserved source batch");
    const auto& feed = p.inputs[0];
    auto acquired = IR::Initial(p);
    IR initial;
    initial.AppendFromTail(p, acquired, {K::Acquire, 0});
    if (IR::Fragment(acquired, 1).size() != feed.atoms.size())
        throw IR::Unsupported("whole feed docking requires a connected source");
    for (const auto& [edge, bits] : acquired.bonds)
        if (bits != 1) throw IR::Unsupported("whole feed docking requires ordinary source edges");
    std::vector<std::pair<IR::AtomId, std::size_t>> requests;
    std::set<IR::Edge> cuts;
    bool scalar = false;
    for (const auto& step : material.steps) {
        const auto& op = step.operation;
        if (op.kind == K::Acquire && &step == &material.steps.front()) continue;
        if (op.kind == K::Unbond && !scalar && op.bits == 1 && op.atoms.size() == 2) {
            IR::Edge edge{std::min(op.atoms[0], op.atoms[1]), std::max(op.atoms[0], op.atoms[1])};
            if (!acquired.bonds.contains(edge) || !cuts.insert(edge).second)
                throw IR::Unsupported("whole feed docking cut identity mismatch");
        } else if (op.kind == K::Output && op.atoms.size() == 1 &&
                   p.outputs.at(op.index).atoms.size() == 1 &&
                   p.outputs.at(op.index).bonds.empty()) {
            scalar = true;
            if (cuts.size() != acquired.bonds.size() ||
                IR::Fragment(step.preconditions, op.atoms[0]).size() != 1)
                throw IR::Unsupported("whole feed docking scalar use precedes complete cuts");
            requests.emplace_back(op.atoms[0], op.index);
        } else
            throw IR::Unsupported(
                "whole feed docking requires explicit cuts then exact scalar requests");
    }
    if (requests.size() != feed.atoms.size())
        throw IR::Unsupported("whole feed docking incomplete handoff");
    // A fixed service frame removes serialized origins without changing identity order.
    const C station = core::Subtract(C{18, 0}, feed.atoms.front().position);
    std::vector<C> service;
    for (const auto& atom : feed.atoms)
        service.push_back(core::Add(station, atom.position));
    const auto gripIndex = static_cast<std::size_t>(
        std::max_element(service.begin(), service.end(),
                         [](C a, C b) { return std::pair{a.q, a.r} < std::pair{b.q, b.r}; }) -
        service.begin());
    std::size_t constructions = 0;
    for (int length = 1; length <= 3; ++length)
        for (int direction = 0; direction < 6; ++direction) {
            CheckDeadline(deadline);
            const C base = core::Subtract(service[gripIndex], Turn({length, 0}, direction));
            const C sourceOrigin = core::Add(base, Turn(core::Subtract(station, base), 5));
            const core::HexFrame source{sourceOrigin, 5};
            std::vector<C> sourceCells, obstacles;
            for (const auto& atom : feed.atoms)
                sourceCells.push_back(source.ToWorld(atom.position));
            auto contains = [](const auto& cells, C c) {
                return std::find(cells.begin(), cells.end(), c) != cells.end();
            };
            if (contains(service, base) || contains(sourceCells, base) ||
                std::any_of(service.begin(), service.end(),
                            [&](C c) { return contains(sourceCells, c); }))
                continue;
            obstacles = sourceCells;
            obstacles.push_back(base);
            std::vector<Relay> relays;
            std::vector<IR::State> relayStarts;
            auto sourceState = acquired;
            for (auto& [id, atom] : sourceState.atoms)
                atom.position = sourceCells.at(id - 1);
            auto docked = acquired;
            for (auto& [id, atom] : docked.atoms)
                atom.position = service.at(id - 1);
            docked.bonds.clear();
            bool legal = true;
            std::string firstContact;
            auto pending = requests;
            std::vector<std::pair<IR::AtomId, std::size_t>> scheduled;
            // Complete cuts make singleton outputs independent. Exposed siblings can
            // leave before an enclosed identity without changing any requested port.
            while (!pending.empty()) {
                CheckDeadline(deadline);
                bool progress = false;
                for (auto request = pending.begin(); request != pending.end(); ++request) {
                    const auto [id, output] = *request;
                    const C cell = service.at(id - 1);
                    bool found = false;
                    for (int radius = 1; radius <= 3 && !found; ++radius)
                        for (int angle = 0; angle < 6 && !found; ++angle)
                            for (int turn : {1, 5}) {
                                const C relayBase = core::Subtract(cell, Turn({radius, 0}, angle));
                                const C destination =
                                    core::Add(relayBase, Turn({radius, 0}, (angle + turn) % 6));
                                if (contains(service, relayBase) ||
                                    contains(obstacles, relayBase) ||
                                    contains(service, destination) ||
                                    contains(obstacles, destination) ||
                                    std::any_of(relays.begin(), relays.end(),
                                                [&](const auto& relay) {
                                                    return relay.destination == destination ||
                                                           relay.destination == relayBase;
                                                }))
                                    continue;
                                auto blocked = obstacles;
                                blocked.push_back(relayBase);
                                if (!Interval(sourceState, base, blocked,
                                              {0, length, (direction + 5) % 6, 0},
                                              {0, length, direction, 1}, 'r', true))
                                    continue;
                                bool previousSafe = true;
                                for (std::size_t n = 0; n < relays.size(); ++n) {
                                    const auto& prior = relays[n];
                                    previousSafe &=
                                        Interval(relayStarts[n], prior.base, blocked,
                                                 {0, prior.length, prior.direction, 0},
                                                 {0, prior.length,
                                                  (prior.direction + prior.turn) % 6, prior.turn},
                                                 prior.turn == 1 ? 'r' : 'R', true);
                                }
                                if (!previousSafe) continue;
                                if (!Interval(docked, relayBase, blocked, {0, radius, angle, 0},
                                              {0, radius, (angle + turn) % 6, turn},
                                              turn == 1 ? 'r' : 'R', true, &firstContact))
                                    continue;
                                relays.push_back(
                                    {relayBase, destination, radius, angle, turn, output});
                                relayStarts.push_back(docked);
                                obstacles.push_back(relayBase);
                                docked.atoms.erase(id);
                                found = true;
                                break;
                            }
                    if (found) {
                        scheduled.push_back(*request);
                        pending.erase(request);
                        progress = true;
                        break;
                    }
                }
                if (!progress) {
                    if (feedback)
                        feedback("whole feed layout length=" + std::to_string(length) +
                                 " direction=" + std::to_string(direction) +
                                 " blocked identity=" + std::to_string(pending.front().first) +
                                 " first contact: " + firstContact);
                    legal = false;
                    break;
                }
            }
            if (!legal) continue;
            if (++constructions > 3 || (beginConstruction && !beginConstruction()))
                throw IR::BudgetFailure("whole feed docking construction limit exhausted");
            const auto constructionFeedback = [&](const std::string& detail) {
                if (feedback)
                    feedback("whole feed construction=" + std::to_string(constructions) +
                             " length=" + std::to_string(length) +
                             " direction=" + std::to_string(direction) + " " + detail);
            };
            constructionFeedback("charged");
            io::SolutionData machine;
            machine.puzzleName = p.name;
            machine.solutionName = "Closed whole-feed dock and identity scalar handoff";
            const auto object = [&](const char* type, C position, int radius, int angle,
                                    int index) {
                machine.objects.push_back(
                    {type,
                     position,
                     {},
                     {radius, angle, index, 0, static_cast<int>(machine.objects.size())}});
                return machine.objects.size() - 1;
            };
            object("input", sourceOrigin, 1, 5, 0);
            for (const auto& [edge, bits] : acquired.bonds) {
                const auto a = service.at(edge.a - 1), b = service.at(edge.b - 1);
                int angle = 0;
                while (angle < 6 && Turn({1, 0}, angle) != core::Subtract(b, a))
                    ++angle;
                if (angle == 6)
                    throw IR::Unsupported("whole feed docking source edge is not adjacent");
                object("unbonder", a, 1, angle, 0);
            }
            for (const auto& relay : relays)
                object("out-std",
                       core::Subtract(relay.destination, p.outputs[relay.output].atoms[0].position),
                       1, 0, relay.output);
            const int initialAngle = (direction + 5) % 6;
            const auto carrier = object("arm1", base, length, initialAngle, 0);
            std::vector<std::size_t> arms;
            for (const auto& relay : relays)
                arms.push_back(object("arm1", relay.base, relay.length, relay.direction, 0));
            auto state = IR::Initial(p);
            IR physical;
            FragmentCustody custody;
            const auto emit = [&](std::size_t arm, char instruction, int tick) {
                machine.objects[arm].instructions.push_back({instruction, tick});
            };
            physical.AppendFromTail(p, state, {K::Acquire, 0, {}, 0, 5, sourceOrigin});
            const auto gripId = static_cast<IR::AtomId>(gripIndex + 1);
            if (!Interval(state, base, obstacles, {0, length, initialAngle, 0},
                          {0, length, direction, 1}, 'r', true)) {
                constructionFeedback("rejected carrier interval");
                continue;
            }
            custody.Grab(state, carrier, source.ToWorld(feed.atoms[gripIndex].position), gripId);
            emit(carrier, 'G', 0);
            physical.AppendFromTail(
                p, state, {K::Relocate, 0, {gripId}, 0, 1, core::Subtract(base, Turn(base, 1))});
            emit(carrier, 'r', 1);
            custody.Validate(state);
            for (const auto& [edge, bits] : acquired.bonds)
                physical.AppendFromTail(p, state, {K::Unbond, 0, {edge.a, edge.b}, bits});
            custody.Drop(carrier);
            emit(carrier, 'g', 2);
            for (const auto& [id, atom] : state.atoms)
                custody.ReserveLoose(state, id, id);
            custody.Validate(state);
            int tick = 5;
            for (std::size_t n = 0; n < scheduled.size(); ++n) {
                CheckDeadline(deadline);
                const auto [id, output] = scheduled[n];
                const auto& relay = relays[n];
                const int finalAngle = (relay.direction + relay.turn) % 6;
                if (IR::Fragment(state, id).size() != 1 ||
                    state.atoms.at(id).position != service.at(id - 1) ||
                    !Interval(state, relay.base, obstacles, {0, relay.length, relay.direction, 0},
                              {0, relay.length, finalAngle, relay.turn},
                              relay.turn == 1 ? 'r' : 'R', true)) {
                    constructionFeedback("rejected scalar interval identity=" + std::to_string(id));
                    legal = false;
                    break;
                }
                custody.Retrieve(state, id, arms[n], service.at(id - 1), id);
                emit(arms[n], 'G', tick++);
                physical.AppendFromTail(
                    p, state,
                    {K::Relocate,
                     0,
                     {id},
                     0,
                     0,
                     core::Subtract(relay.destination, state.atoms.at(id).position)});
                custody.Validate(state);
                emit(arms[n], relay.turn == 1 ? 'r' : 'R', tick++);
                custody.Drop(arms[n]);
                emit(arms[n], 'g', tick++);
                physical.AppendFromTail(p, state, {K::Output, output, {id}});
                custody.Validate(state);
                tick += 2;
                if (!Interval(state, relay.base, obstacles, {0, relay.length, finalAngle, 0},
                              {0, relay.length, relay.direction, 0}, relay.turn == 1 ? 'R' : 'r',
                              false)) {
                    constructionFeedback("rejected receiver reset identity=" + std::to_string(id));
                    legal = false;
                    break;
                }
                emit(arms[n], relay.turn == 1 ? 'R' : 'r', tick++);
                tick += 2;
            }
            if (!legal) continue;
            if (!Interval(state, base, obstacles, {0, length, direction, 0},
                          {0, length, initialAngle, 0}, 'R', false)) {
                constructionFeedback("rejected carrier reset");
                continue;
            }
            emit(carrier, 'R', tick++);
            tick += 2;
            for (auto arm : arms)
                emit(arm, 'O', tick);
            emit(carrier, 'O', tick);
            IR::RequireComplete(state, std::vector<std::size_t>(p.outputs.size(), 1));
            if (!custody.Held().empty() || !custody.Parked().empty() || physical.Replay(p) != state)
                throw IR::Error("whole feed docking physical ledger or custody did not close");
            machine.metrics.cycles = tick + 1;
            return machine;
        }
    throw IR::LayoutFailure("whole feed docking exhausted eighteen disjoint service layouts");
}
} // namespace
bool SupportsWholeFeedDocking(const io::PuzzleData& p) noexcept {
    constexpr auto permissions =
        (1ULL << 0) | (1ULL << 9) | (1ULL << 22) | (1ULL << 23) | (1ULL << 25);
    if (p.version != 3 || p.productionInfo || p.outputMultiplier != 1 || p.inputs.size() != 1 ||
        (p.permissionBits & permissions) != permissions || p.inputs[0].atoms.size() < 2 ||
        p.inputs[0].atoms.size() > 12 || p.outputs.size() != p.inputs[0].atoms.size())
        return false;
    std::multiset<core::AtomType> types;
    for (const auto& atom : p.inputs[0].atoms)
        types.insert(atom.type);
    for (const auto& output : p.outputs) {
        if (output.atoms.size() != 1 || !output.bonds.empty()) return false;
        const auto found = types.find(output.atoms[0].type);
        if (found == types.end()) return false;
        types.erase(found);
    }
    try {
        auto state = IR::Initial(p);
        IR material;
        material.AppendFromTail(p, state, {K::Acquire, 0});
        return IR::Fragment(state, 1).size() == p.inputs[0].atoms.size() &&
               std::all_of(state.bonds.begin(), state.bonds.end(),
                           [](const auto& bond) { return bond.second == 1; });
    } catch (...) {
        return false;
    }
}
std::optional<io::SolutionData> CompileAcceptedWholeFeedDocking(
    const io::PuzzleData& p, const ExecutionIR& material,
    const std::function<bool(const io::SolutionData&)>& accept,
    const std::function<void(const std::string&)>& feedback,
    std::chrono::steady_clock::time_point deadline,
    const std::function<bool()>& beginConstruction) {
    std::optional<io::SolutionData> machine;
    try {
        CheckDeadline(deadline);
        machine = Dock(p, material, deadline, feedback, beginConstruction);
        CheckDeadline(deadline);
    } catch (const IR::Error& error) {
        if (feedback) feedback(error.what());
        return std::nullopt;
    }
    if (!accept || !accept(*machine)) return std::nullopt;
    if (feedback)
        feedback("whole feed original identities, complete cuts, scalar custody and renewable "
                 "reset certified");
    return machine;
}
} // namespace opusoptima::solvers
