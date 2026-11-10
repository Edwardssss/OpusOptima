#pragma once

#include <algorithm>
#include <stdexcept>
#include <vector>

#include "BaseSolver.hpp"

namespace opusoptima::solvers {

struct ModulePort {
    core::HexCoord position{};
    int orientation{};
    bool held{};
    friend bool operator==(const ModulePort&, const ModulePort&) = default;
};

struct ModuleHandoff {
    ModulePort produced, consumed;
    int available{}, pickup{}, cleared{}, nextArrival{};
};

// Half-open occupancy windows describe a single physical exit, not swept paths.
inline void ValidateModuleHandoffs(const std::vector<ModuleHandoff>& handoffs) {
    for (std::size_t i = 0; i < handoffs.size(); ++i) {
        const auto& h = handoffs[i];
        if (h.produced != h.consumed || h.produced.orientation < 0 || h.produced.orientation >= 6 ||
            h.available < 0 || h.pickup < h.available || h.cleared <= h.pickup ||
            h.nextArrival < h.cleared)
            throw std::invalid_argument("incompatible module exit or handoff timing");
        for (std::size_t j = 0; j < i; ++j) {
            const auto& previous = handoffs[j];
            if (previous.produced.position == h.produced.position &&
                std::max(previous.available, h.available) < std::min(previous.cleared, h.cleared))
                throw std::invalid_argument("overlapping module exit occupancy");
        }
    }
}

} // namespace opusoptima::solvers
