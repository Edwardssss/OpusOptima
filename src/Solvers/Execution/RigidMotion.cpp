#include "RigidMotion.hpp"

#include <algorithm>
#include <map>
#include <queue>
#include <stdexcept>
#include <tuple>

namespace opusoptima::solvers {
RigidMotionResult RouteRigidFragment(const RigidMotionProblem& p) {
    if (p.fragment.empty() || p.allowed.empty() || !p.executable)
        throw std::invalid_argument(
            "rigid routing requires fragment, finite domain and mechanical edge validator");
    auto normalized = [](RigidPose pose) {
        pose.rotation = (pose.rotation % 6 + 6) % 6;
        return pose;
    };
    auto key = [](RigidPose pose) {
        return std::tuple{pose.anchor.q, pose.anchor.r, pose.rotation};
    };
    std::unordered_set<core::HexCoord, core::HexCoordHash> unique;
    for (auto cell : p.fragment)
        if (!unique.insert(cell).second) throw std::invalid_argument("duplicate fragment cell");
    auto valid = [&](RigidPose pose) {
        for (auto cell : p.fragment) {
            for (int r = 0; r < pose.rotation; ++r)
                cell = core::RotateClockwise(cell);
            cell = core::Add(cell, pose.anchor);
            if (!p.allowed.contains(cell) || p.blocked.contains(cell)) return false;
        }
        return true;
    };
    RigidMotionResult result;
    const auto start = normalized(p.start), goal = normalized(p.goal);
    if (!valid(start) || !valid(goal)) return result;
    struct Node {
        RigidPose pose;
        std::size_t parent;
        RigidMove move;
    };
    if (p.maxStates == 0) {
        result.status = MotionStatus::Exhausted;
        return result;
    }
    std::vector<Node> nodes{{start, 0, RigidMove::Translate0}};
    std::map<decltype(key(start)), std::size_t> seen{{key(start), 0}};
    bool truncated = false;
    for (std::size_t index = 0; index < nodes.size(); ++index) {
        const auto pose = nodes[index].pose;
        ++result.explored;
        if (pose == goal) {
            result.status = MotionStatus::Found;
            for (auto i = index;; i = nodes[i].parent) {
                result.poses.push_back(nodes[i].pose);
                if (i == 0) break;
                result.moves.push_back(nodes[i].move);
            }
            std::reverse(result.poses.begin(), result.poses.end());
            std::reverse(result.moves.begin(), result.moves.end());
            return result;
        }
        for (int move = 0; move < 8; ++move) {
            auto next = pose;
            if (move < 6)
                next.anchor = core::Neighbor(pose.anchor, move);
            else
                next.rotation = (pose.rotation + (move == 6 ? 1 : 5)) % 6;
            if (!valid(next) || seen.contains(key(next)) ||
                !p.executable(pose, next, static_cast<RigidMove>(move)))
                continue;
            if (nodes.size() >= p.maxStates) {
                truncated = true;
                continue;
            }
            seen.emplace(key(next), nodes.size());
            nodes.push_back({next, index, static_cast<RigidMove>(move)});
        }
    }
    if (truncated) result.status = MotionStatus::Exhausted;
    return result;
}
} // namespace opusoptima::solvers
