#pragma once

#include "AssemblySolver.hpp"

#include <algorithm>
#include <stdexcept>
#include <vector>

namespace opusoptima::solvers {
struct FeedLayout {
    int rotation{};
    std::vector<core::HexCoord> cells;
};

template <typename Molecule> FeedLayout PlanFeedLayout(const Molecule& input) {
    for (int pass = 0; pass < 2; ++pass)
        for (int rotation = 0; rotation < 6; ++rotation) {
            FeedLayout layout{rotation, {}};
            int low = 256, high = -256;
            for (const auto& atom : input.atoms) {
                auto c = atom.position;
                for (int n = 0; n < rotation; ++n)
                    c = core::RotateClockwise(c);
                layout.cells.push_back(c);
                low = std::min(low, c.r);
                high = std::max(high, c.r);
            }
            bool unique = true;
            for (std::size_t i = 0; i < layout.cells.size(); ++i)
                for (std::size_t j = 0; j < i; ++j)
                    unique &= layout.cells[i].q != layout.cells[j].q;
            if (high - low <= 2 && (pass == 1 || unique)) return layout;
        }
    throw std::invalid_argument(
        "narrow feed layout requires at most three rows after rotation; use the wide extraction "
        "compiler for wider bonded feeds (not a chemistry limitation)");
}
template <typename Molecule> FeedLayout PlanExtractionLayout(const Molecule& input) {
    FeedLayout best;
    int span = 100000;
    for (int rotation = 0; rotation < 6; ++rotation) {
        FeedLayout layout{rotation, {}};
        int low = 256, high = -256;
        for (const auto& atom : input.atoms) {
            auto c = atom.position;
            for (int n = 0; n < rotation; ++n)
                c = core::RotateClockwise(c);
            layout.cells.push_back(c);
            low = std::min(low, c.r);
            high = std::max(high, c.r);
        }
        if (high - low < span) {
            span = high - low;
            best = layout;
        }
    }
    return best;
}
} // namespace opusoptima::solvers
