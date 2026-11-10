#include "SerialFragmentMotion.hpp"
#include "HexFrame.hpp"

#include <algorithm>
#include <array>
#include <map>
#include <queue>
#include <tuple>
#include <unordered_set>

namespace opusoptima::solvers {
SerialFragmentMotionResult RouteSerialFragment(const SerialFragmentMotionProblem& problem) {
    using IR = ExecutionIR;
    using Coord = core::HexCoord;
    using Pose = std::tuple<int, int, int>;
    if (!problem.executable || problem.goalRotations.empty())
        throw std::invalid_argument("serial fragment motion needs a goal and interval certificate");
    const auto members = IR::Fragment(problem.material, problem.anchor);
    const auto origin = problem.material.atoms.at(problem.anchor).position;
    const std::unordered_set<Coord, core::HexCoordHash> bases(problem.armBases.begin(),
                                                              problem.armBases.end());
    const auto directions =
        std::array<Coord, 6>{{{1, 0}, {0, 1}, {-1, 1}, {-1, 0}, {0, -1}, {1, -1}}};
    auto worldAt = [&](Pose pose) {
        auto world = problem.material;
        const auto [q, r, turns] = pose;
        const core::HexFrame frame{{q, r}, turns};
        for (auto id : members)
            world.atoms.at(id).position =
                frame.ToWorld(core::Subtract(problem.material.atoms.at(id).position, origin));
        return world;
    };
    struct Node {
        Pose pose;
        std::size_t previous{};
        SerialFragmentMove move;
    };
    const Pose start{origin.q, origin.r, 0};
    std::vector<Node> nodes{{start, 0, {}}};
    std::map<Pose, std::size_t> seen{{start, 0}};
    SerialFragmentMotionResult result;
    result.closestDistance = core::Distance(origin, problem.destination);
    if (!problem.maxStates) {
        result.status = MotionStatus::Exhausted;
        return result;
    }
    bool exhausted = false;
    using QueueEntry = std::pair<int, std::size_t>;
    std::priority_queue<QueueEntry, std::vector<QueueEntry>, std::greater<QueueEntry>> pending;
    std::vector<int> depth{0};
    pending.emplace(result.closestDistance * 4, 0);
    while (!pending.empty()) {
        const auto head = pending.top().second;
        pending.pop();
        const auto pose = nodes[head].pose;
        const auto [q, r, turns] = pose;
        ++result.explored;
        result.closestDistance =
            std::min(result.closestDistance, core::Distance({q, r}, problem.destination));
        if (Coord{q, r} == problem.destination &&
            std::find(problem.goalRotations.begin(), problem.goalRotations.end(), turns) !=
                problem.goalRotations.end()) {
            result.status = MotionStatus::Found;
            for (auto index = head; index; index = nodes[index].previous)
                result.moves.push_back(nodes[index].move);
            std::reverse(result.moves.begin(), result.moves.end());
            return result;
        }
        const auto world = worldAt(pose);
        for (auto id : members)
            for (int direction = 0; direction < 6; ++direction) {
                const auto base =
                    core::Subtract(world.atoms.at(id).position, directions[direction]);
                if (!bases.contains(base)) continue;
                for (int rotation : {1, 5})
                    for (bool pivot : {false, true}) {
                        if (pivot && !problem.pivot) continue;
                        const char instruction =
                            pivot ? (rotation == 1 ? 'p' : 'P') : (rotation == 1 ? 'r' : 'R');
                        const auto center = pivot ? world.atoms.at(id).position : base;
                        const core::HexFrame frame{center, rotation};
                        const auto next = frame.ToWorld(core::Subtract(Coord{q, r}, center));
                        const Pose after{next.q, next.r, (turns + rotation) % 6};
                        if (seen.contains(after)) continue;
                        if (!problem.executable(world, base, direction, instruction)) continue;
                        if (nodes.size() >= problem.maxStates) {
                            exhausted = true;
                            continue;
                        }
                        seen.emplace(after, nodes.size());
                        const auto score =
                            core::Distance(next, problem.destination) * 4 + depth[head] + 1;
                        pending.emplace(score, nodes.size());
                        depth.push_back(depth[head] + 1);
                        nodes.push_back({after, head, {base, id, direction, instruction}});
                    }
            }
    }
    result.status = exhausted ? MotionStatus::Exhausted : MotionStatus::Unsatisfiable;
    return result;
}
} // namespace opusoptima::solvers
