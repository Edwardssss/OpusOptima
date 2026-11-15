#include "SolutionPostProcess.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <optional>
#include <tuple>
#include <unordered_set>

namespace opusoptima::solvers::architecture {
namespace {

[[nodiscard]] bool IsArm(const std::string& type) {
    return type.starts_with("arm");
}

[[nodiscard]] bool SameObject(const io::SolutionObject& left, const io::SolutionObject& right) {
    return left.typeName == right.typeName && left.position == right.position &&
           left.bodyData == right.bodyData && left.instructions.size() == right.instructions.size() &&
           std::equal(left.instructions.begin(), left.instructions.end(), right.instructions.begin(),
                      [](const auto& a, const auto& b) {
                          return a.action == b.action && a.parameter == b.parameter;
                      });
}

/// Only explicit offsets occupy cells; an invalid path cannot prove a track unused.
[[nodiscard]] std::optional<std::unordered_set<core::HexCoord, core::HexCoordHash>> TrackCells(
    const io::SolutionObject& track) {
    constexpr std::size_t countIndex = 4;
    constexpr std::size_t offsetsBegin = countIndex + 1;
    constexpr std::size_t fixedWords = offsetsBegin + 1; // Header/count plus footer.
    if (track.bodyData.size() < fixedWords || track.bodyData[countIndex] <= 0)
        return std::nullopt;
    const auto count = static_cast<std::size_t>(track.bodyData[countIndex]);
    if (count > (track.bodyData.size() - fixedWords) / 2 ||
        track.bodyData.size() != fixedWords + (2 * count)) return std::nullopt;
    std::unordered_set<core::HexCoord, core::HexCoordHash> cells;
    for (std::size_t index = 0; index < count; ++index) {
        const std::size_t offset = offsetsBegin + (2 * index);
        const auto absoluteQ = static_cast<std::int64_t>(track.position.q) + track.bodyData[offset];
        const auto absoluteR = static_cast<std::int64_t>(track.position.r) + track.bodyData[offset + 1];
        if (absoluteQ < std::numeric_limits<int>::min() || absoluteQ > std::numeric_limits<int>::max() ||
            absoluteR < std::numeric_limits<int>::min() || absoluteR > std::numeric_limits<int>::max())
            return std::nullopt;
        cells.insert({static_cast<int>(absoluteQ), static_cast<int>(absoluteR)});
    }
    return cells;
}

} // namespace

bool SolutionPostProcess::ArmIsIdle(const io::SolutionObject& object) {
    if (!IsArm(object.typeName)) return false;
    for (const auto& instruction : object.instructions) {
        switch (instruction.action) {
        case 'G':
        case 'g':
        case 'r':
        case 'R':
        case 'A':
        case 'a':
            return false;
        default:
            break;
        }
    }
    return true;
}

io::SolutionData SolutionPostProcess::PruneUnusedParts(const io::SolutionData& machine,
                                                       PruneReport* report) {
    io::SolutionData pruned;
    pruned.puzzleName = machine.puzzleName;
    pruned.solutionName = machine.solutionName;
    pruned.metrics = machine.metrics;

    PruneReport local;
    for (const auto& object : machine.objects) {
        if (ArmIsIdle(object)) {
            local.removed.push_back({object.typeName, object.position, "idle arm"});
            continue;
        }
        const bool duplicate =
            std::any_of(pruned.objects.begin(), pruned.objects.end(),
                        [&](const auto& kept) { return SameObject(kept, object); });
        if (duplicate) {
            local.removed.push_back({object.typeName, object.position, "duplicate object"});
            continue;
        }
        pruned.objects.push_back(object);
    }

    // A track whose cells no other object references is dead weight (5 cost per hex).
    std::unordered_set<core::HexCoord, core::HexCoordHash> referenced;
    for (const auto& object : pruned.objects) {
        if (object.typeName == "track") continue;
        referenced.insert(object.position);
        if (object.typeName == "input" || object.typeName == "out-std" ||
            object.typeName == "out-rep") {
            // These place a whole molecule; its cells are not in `bodyData`, so keep
            // any track that is adjacent to one of their origins rather than guess.
            for (const auto& step : core::kAxialDirections)
                referenced.insert(core::Add(object.position, step));
        }
    }
    std::vector<io::SolutionObject> kept;
    kept.reserve(pruned.objects.size());
    for (auto& object : pruned.objects) {
        if (object.typeName == "track") {
            const auto cells = TrackCells(object);
            const bool used =
                !cells || std::any_of(cells->begin(), cells->end(),
                            [&](const core::HexCoord cell) { return referenced.count(cell) != 0; });
            if (!used) {
                local.removed.push_back({object.typeName, object.position, "unreferenced track"});
                continue;
            }
        }
        kept.push_back(std::move(object));
    }
    pruned.objects = std::move(kept);

    if (report) *report = std::move(local);
    return pruned;
}

io::SolutionData SolutionPostProcess::CostOptimize(const io::SolutionData& machine,
                                                   PruneReport* report) {
    auto pruned = PruneUnusedParts(machine, report);
    std::stable_sort(pruned.objects.begin(), pruned.objects.end(),
                     [](const io::SolutionObject& left, const io::SolutionObject& right) {
                         return std::tie(left.position.q, left.position.r, left.typeName) <
                                std::tie(right.position.q, right.position.r, right.typeName);
                     });
    return pruned;
}

} // namespace opusoptima::solvers::architecture
