#include "HeldArmMotion.hpp"

#include <algorithm>
#include <map>
#include <stdexcept>

namespace opusoptima::solvers {
namespace {
int Wrap(int n) {
    return (n % 6 + 6) % 6;
}
core::HexCoord Rotate(core::HexCoord c, int n) {
    for (int i = 0; i < Wrap(n); ++i)
        c = core::RotateClockwise(c);
    return c;
}
} // namespace
RigidPose HeldFragmentPose(const HeldArmMotionProblem& p, const HeldArmState& s) {
    const auto reach = Rotate({s.length, 0}, s.direction);
    return {core::Add(p.track.at(s.trackIndex), reach), Wrap(s.fragmentRotation)};
}
namespace {
HeldArmMotionResult SearchArm(const HeldArmMotionProblem& p, bool holding,
                              const std::function<bool(const HeldArmState&)>& goal) {
    if (p.track.empty() || p.fragment.empty() || !p.executable || p.minLength < 1 ||
        p.maxLength > 3 || p.minLength > p.maxLength || p.start.trackIndex >= p.track.size() ||
        p.start.length < p.minLength || p.start.length > p.maxLength)
        throw std::invalid_argument("invalid held-arm motion problem");
    std::unordered_set<core::HexCoord, core::HexCoordHash> cells;
    for (std::size_t i = 0; i < p.track.size(); ++i) {
        if (!cells.insert(p.track[i]).second ||
            (i && core::Distance(p.track[i - 1], p.track[i]) != 1))
            throw std::invalid_argument("track must be a simple adjacent path");
    }
    cells.clear();
    for (auto c : p.fragment)
        if (!cells.insert(c).second) throw std::invalid_argument("duplicate fragment cell");
    if (!cells.contains({0, 0})) throw std::invalid_argument("fragment omits grabbed atom");
    struct Node {
        HeldArmState state;
        std::size_t parent;
        char instruction;
    };
    auto start = p.start;
    start.direction = Wrap(start.direction);
    start.fragmentRotation = Wrap(start.fragmentRotation);
    std::vector<Node> nodes{{start, 0, 0}};
    std::map<HeldArmState, std::size_t> seen{{start, 0}};
    HeldArmMotionResult result;
    if (!p.maxStates) {
        result.status = MotionStatus::Exhausted;
        return result;
    }
    bool exhausted = false;
    for (std::size_t head = 0; head < nodes.size(); ++head) {
        const auto current = nodes[head].state;
        ++result.explored;
        if (goal(current)) {
            result.status = MotionStatus::Found;
            for (auto i = head;; i = nodes[i].parent) {
                result.states.push_back(nodes[i].state);
                if (!i) break;
                result.instructions.push_back(nodes[i].instruction);
            }
            std::reverse(result.states.begin(), result.states.end());
            std::reverse(result.instructions.begin(), result.instructions.end());
            return result;
        }
        auto add = [&](HeldArmState next, char instruction) {
            next.direction = Wrap(next.direction);
            next.fragmentRotation = Wrap(next.fragmentRotation);
            if (seen.contains(next) || !p.executable(current, next, instruction)) return;
            if (nodes.size() >= p.maxStates) {
                exhausted = true;
                return;
            }
            seen.emplace(next, nodes.size());
            nodes.push_back({next, head, instruction});
        };
        auto next = current;
        --next.direction;
        if (holding) --next.fragmentRotation;
        add(next, 'R');
        next = current;
        ++next.direction;
        if (holding) ++next.fragmentRotation;
        add(next, 'r');
        if (holding && p.pivot) {
            next = current;
            --next.fragmentRotation;
            add(next, 'P');
            next = current;
            ++next.fragmentRotation;
            add(next, 'p');
        }
        if (current.length < p.maxLength) {
            next = current;
            ++next.length;
            add(next, 'E');
        }
        if (current.length > p.minLength) {
            next = current;
            --next.length;
            add(next, 'e');
        }
        if (current.trackIndex + 1 < p.track.size()) {
            next = current;
            ++next.trackIndex;
            add(next, 'A');
        }
        if (current.trackIndex) {
            next = current;
            --next.trackIndex;
            add(next, 'a');
        }
    }
    result.status = exhausted ? MotionStatus::Exhausted : MotionStatus::Unsatisfiable;
    return result;
}
} // namespace
HeldArmMotionResult RouteHeldArm(const HeldArmMotionProblem& p) {
    return SearchArm(p, true, [&](const HeldArmState& state) {
        const auto pose = HeldFragmentPose(p, state);
        return pose.anchor == p.goal.anchor && pose.rotation == Wrap(p.goal.rotation);
    });
}
HeldArmMotionResult RouteEmptyArm(const EmptyArmMotionProblem& p) {
    if (p.goal.trackIndex >= p.track.size() || p.goal.length < p.minLength ||
        p.goal.length > p.maxLength)
        throw std::invalid_argument("invalid empty-arm goal");
    HeldArmMotionProblem search;
    search.track = p.track;
    search.fragment = {{0, 0}};
    search.start = p.start;
    search.start.fragmentRotation = 0;
    search.minLength = p.minLength;
    search.maxLength = p.maxLength;
    search.maxStates = p.maxStates;
    search.executable = p.executable;
    return SearchArm(search, false, [&](const HeldArmState& state) {
        return state.trackIndex == p.goal.trackIndex && state.length == p.goal.length &&
               state.direction == Wrap(p.goal.direction);
    });
}
} // namespace opusoptima::solvers
