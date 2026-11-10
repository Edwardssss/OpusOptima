#pragma once

#include "RowAssembly.hpp"
#include "RowCompletionTransfer.hpp"
#include <chrono>
#include <cstdlib>
#include <functional>

// Internal row compiler helpers and explicit completion-transfer seam.
namespace opusoptima::solvers {
bool SupportsFiniteWheelSecondSource(const io::PuzzleData& puzzle);

io::SolutionData CompileFiniteWheelSecondSource(
    const io::PuzzleData& actual, std::chrono::steady_clock::time_point deadline,
    const std::function<io::VerificationReport(const io::SolutionData&,
                                               std::chrono::steady_clock::time_point)>& nativeGate);

// Tracks serialize offsets in plus-instruction order, relative to their part origin.
inline std::vector<int> ReceiverRailBody(int distance, core::HexCoord direction = {1, 0}) {
    const int count = std::abs(distance) + 1;
    std::vector<int> data{1, 0, 0, 0, count};
    const int first = std::min(distance, 0);
    for (int i = 0; i < count; ++i)
        data.insert(data.end(), {(first + i) * direction.q, (first + i) * direction.r});
    data.push_back(0);
    return data;
}
inline bool ReceiverRailDisjoint(const io::SolutionData& machine, core::HexCoord base, int distance,
                                 core::HexCoord direction = {1, 0}) {
    const auto proposed = ReceiverRailBody(distance, direction);
    for (const auto& rail : machine.objects) {
        if (rail.typeName != "track") continue;
        const int count = rail.bodyData.at(4);
        for (int i = 0; i < count; ++i) {
            const auto cell = core::Add(rail.position,
                                        {rail.bodyData.at(5 + 2 * i), rail.bodyData.at(6 + 2 * i)});
            for (int j = 0; j < proposed.at(4); ++j)
                if (cell == core::Add(base, {proposed.at(5 + 2 * j), proposed.at(6 + 2 * j)}))
                    return false;
        }
    }
    return true;
}
struct MixedInventoryProgram;
// A null completionTransfer selects legacy compilation. With reactionAssembly,
// productOrigins must describe the legacy output placements; null is invalid.
io::SolutionData CompileRows(
    const io::PuzzleData& p, const AssemblyPlan& plan,
    const io::PuzzleData* sharedOutputs = nullptr, const io::Molecule* labels = nullptr,
    const std::vector<core::HexCoord>* productOrigins = nullptr, bool postCalcification = true,
    std::vector<int>* productEnds = nullptr, bool singleProduct = false,
    std::size_t finiteProducts = 0, const SupplyPlan* supplied = nullptr,
    const io::PuzzleData* validationPuzzle = nullptr, int feedSpacing = 3,
    bool retimeEmptyResets = false, bool boundEscapeStation = false, bool closeFeedBank = false,
    bool componentRows = false, std::vector<SharedFactoryEmission>* emissions = nullptr,
    const std::vector<ProductAtomRef>* owners = nullptr, bool animismus = false,
    const io::PuzzleData* reactionAssembly = nullptr,
    const MixedInventoryProgram* mixedInventory = nullptr,
    std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::time_point::max(),
    const std::function<std::chrono::steady_clock::time_point()>& clock = {},
    std::vector<FragmentCustodyEvent>* custody = nullptr,
    RowCompletionTransfer* completionTransfer = nullptr);
} // namespace opusoptima::solvers
