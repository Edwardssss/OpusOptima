#include "ChemistryPipeline.hpp"

#include <algorithm>
#include <functional>
#include <stdexcept>

namespace opusoptima::solvers {
namespace {
using core::AtomType;
bool Cardinal(AtomType t) {
    return t >= AtomType::Air && t <= AtomType::Water;
}
bool Allowed(const io::PuzzleData& p, std::uint64_t bits) {
    return (p.permissionBits & bits) == bits;
}
int Direct(const io::PuzzleData& p, AtomType t) {
    for (std::size_t i = 0; i < p.inputs.size(); ++i)
        if (p.inputs[i].atoms.size() == 1 && p.inputs[i].bonds.empty() &&
            p.inputs[i].atoms[0].type == t)
            return static_cast<int>(i);
    return -1;
}
} // namespace
ChemistryFeed ChemistryPipeline::Resolve(const io::PuzzleData& p, AtomType type) {
    if (type < AtomType::Salt || type > AtomType::Quintessence || type == AtomType::Repeat)
        throw std::invalid_argument("chemistry requires finite elements");
    if (const int direct = Direct(p, type); direct >= 0) return {ChemistryFeedKind::Direct, direct};
    int salt = Direct(p, AtomType::Salt);
    bool calcify = false;
    if (salt < 0 && Allowed(p, 1ULL << 12))
        for (int t = 2; t <= 5 && salt < 0; ++t) {
            salt = Direct(p, static_cast<AtomType>(t));
            calcify = salt >= 0;
        }
    if (salt >= 0 && type == AtomType::Salt) return {ChemistryFeedKind::Calcify, salt, true};
    if (salt >= 0 && (type == AtomType::Vitae || type == AtomType::Mors) && Allowed(p, 1ULL << 16))
        return {ChemistryFeedKind::Animismus, salt, calcify};
    // A calcifier on the same wheel pickup would immediately undo duplication.
    const int rawSalt = Direct(p, AtomType::Salt);
    constexpr auto wheel = (1ULL << 13) | (1ULL << 28);
    if (rawSalt >= 0 && Allowed(p, wheel)) {
        if (Cardinal(type)) return {ChemistryFeedKind::Wheel, rawSalt};
        if (type == AtomType::Quintessence && Allowed(p, 1ULL << 18))
            return {ChemistryFeedKind::Unify, rawSalt};
    }
    if ((Cardinal(type) || (type == AtomType::Salt && Allowed(p, 1ULL << 12))) &&
        Allowed(p, 1ULL << 18)) {
        const int quint = Direct(p, AtomType::Quintessence);
        if (quint >= 0) return {ChemistryFeedKind::Disperse, quint, type == AtomType::Salt};
    }
    throw std::invalid_argument(
        "chemistry has no compatible singleton feed (metal and chained factories unsupported)");
}
ChemistryPlan ChemistryPipeline::Plan(const io::PuzzleData& p,
                                      const std::vector<ChemistryDemand>& demands) {
    ChemistryPlan result;
    result.inputBatches.resize(p.inputs.size());
    std::array<bool, 16> active{};
    auto slot = [](AtomType t) {
        const int id = static_cast<int>(t) - 1;
        if (id < 0 || id >= 16 || t == AtomType::Repeat)
            throw std::invalid_argument("invalid chemistry element");
        return id;
    };
    auto acquire = [&](int input) {
        ++result.inputBatches[input];
        ++result.stock[slot(p.inputs[input].atoms[0].type)];
        result.operations.push_back({input});
    };
    std::function<void(AtomType)> take;
    take = [&](AtomType type) {
        const auto id = slot(type);
        if (result.stock[id]) {
            --result.stock[id];
            return;
        }
        if (active[id]) throw std::invalid_argument("cyclic chemistry demand");
        active[id] = true;
        const auto feed = Resolve(p, type);
        if (feed.kind == ChemistryFeedKind::Direct)
            acquire(feed.input);
        else if (feed.kind == ChemistryFeedKind::Calcify) {
            acquire(feed.input);
            --result.stock[slot(p.inputs[feed.input].atoms[0].type)];
            ++result.stock[slot(AtomType::Salt)];
            result.operations.push_back({-1, ChemistryReaction::Calcify});
        } else if (feed.kind == ChemistryFeedKind::Wheel) {
            take(AtomType::Salt);
            ++result.stock[id];
            result.operations.push_back({-1, ChemistryReaction::Duplicate, type});
        } else if (feed.kind == ChemistryFeedKind::Animismus) {
            take(AtomType::Salt);
            take(AtomType::Salt);
            ++result.stock[slot(AtomType::Vitae)];
            ++result.stock[slot(AtomType::Mors)];
            result.operations.push_back({-1, ChemistryReaction::Animismus});
        } else if (feed.kind == ChemistryFeedKind::Unify) {
            for (int t = 2; t <= 5; ++t)
                take(static_cast<AtomType>(t));
            ++result.stock[id];
            result.operations.push_back({-1, ChemistryReaction::Unify});
        } else {
            take(AtomType::Quintessence);
            for (int t = 2; t <= 5; ++t)
                ++result.stock[t - 1];
            result.operations.push_back({-1, ChemistryReaction::Disperse});
            if (feed.calcifySource) {
                --result.stock[slot(AtomType::Air)];
                ++result.stock[slot(AtomType::Salt)];
                result.operations.push_back({-1, ChemistryReaction::Calcify});
            }
        }
        active[id] = false;
        --result.stock[id];
    };
    for (const auto demand : demands) {
        result.feeds.push_back(Resolve(p, demand.type));
        for (std::size_t i = 0; i < demand.count; ++i)
            take(demand.type);
    }
    return result;
}
} // namespace opusoptima::solvers
