#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <queue>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "HexGrid.hpp"

namespace opusoptima::solvers {

/// A* pathfinding result: sequence of hex coordinates from start to goal.
struct AStarResult {
    std::vector<core::HexCoord> path;
    bool found{false};
};

/// Open-set node for the A* priority queue.
struct AStarNode {
    core::HexCoord pos;
    double fScore{};

    bool operator>(const AStarNode& other) const {
        return fScore > other.fScore;
    }
};

using CoordHash = core::HexCoordHash;

/// A* pathfinder on a hex grid with optional obstacle set.
class AStar {
    public:
    explicit AStar(std::unordered_set<core::HexCoord, CoordHash> obstacles = {})
        : obstacles_(std::move(obstacles)) {}

    /// Add a single obstacle.
    void AddObstacle(core::HexCoord cell) {
        obstacles_.insert(cell);
    }

    /// Find the shortest path from start to goal.
    /// Uses manhattan distance (hex distance) as heuristic.
    [[nodiscard]] AStarResult FindPath(core::HexCoord start, core::HexCoord goal) const {
        AStarResult result;

        if (start == goal) {
            result.path = {start};
            result.found = true;
            return result;
        }

        // Priority queue: lowest fScore first
        std::priority_queue<AStarNode, std::vector<AStarNode>, std::greater<>> openSet;
        openSet.push({start, HexDistance(start, goal)});

        std::unordered_map<core::HexCoord, core::HexCoord, CoordHash> cameFrom;
        std::unordered_map<core::HexCoord, double, CoordHash> gScore;
        gScore[start] = 0.0;

        while (!openSet.empty()) {
            const auto current = openSet.top().pos;
            openSet.pop();

            if (current == goal) {
                // Reconstruct path
                auto cur = current;
                while (!(cur == start)) {
                    result.path.push_back(cur);
                    cur = cameFrom.at(cur);
                }
                result.path.push_back(start);
                std::ranges::reverse(result.path);
                result.found = true;
                return result;
            }

            for (std::size_t direction_idx = 0; direction_idx < core::kDirectionCount;
                 ++direction_idx) {
                const auto neighbor = core::Neighbor(current, static_cast<int>(direction_idx));

                // Skip obstacles
                if (obstacles_.contains(neighbor)) continue;

                const double tentativeG = gScore.at(current) + 1.0;
                auto gIt = gScore.find(neighbor);
                if (gIt == gScore.end() || tentativeG < gIt->second) {
                    cameFrom[neighbor] = current;
                    gScore[neighbor] = tentativeG;
                    openSet.push({neighbor, tentativeG + HexDistance(neighbor, goal)});
                }
            }
        }

        return result; // found == false
    }

    private:
    /// Hex distance heuristic
    static double HexDistance(core::HexCoord source, core::HexCoord destination) {
        return static_cast<double>(core::Distance(source, destination));
    }

    std::unordered_set<core::HexCoord, CoordHash> obstacles_;
};

} // namespace opusoptima::solvers
