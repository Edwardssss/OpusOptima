#include "ClosedMetalInventory.hpp"
#include "InventorySearch.hpp"
#include "SerialArmExecution.hpp"
#include "SupplyExecutionIR.hpp"
#include <algorithm>
#include <cstdint>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <utility>

namespace opusoptima::solvers {
namespace {
struct MetalPeriod {
    std::size_t products, batches;
};
std::optional<MetalPeriod> Period(const io::PuzzleData& p) {
    constexpr auto permissions =
        (1ULL << 0) | (1ULL << 8) | (1ULL << 22) | (1ULL << 23) | (1ULL << 25);
    if ((p.permissionBits & permissions) != permissions || p.productionInfo ||
        p.outputMultiplier <= 0 || p.inputs.size() != 1 || p.outputs.size() != 1 ||
        p.outputs[0].atoms.size() != 1 || !p.outputs[0].bonds.empty() ||
        p.inputs[0].atoms.size() < 2 || p.inputs[0].atoms.size() > 12 ||
        (p.permissionBits & (1ULL << 17)))
        return {};
    const auto weight = [](core::AtomType type) -> std::size_t {
        if (type < core::AtomType::Gold || type > core::AtomType::Lead) return 0;
        return std::size_t{1} << (static_cast<int>(core::AtomType::Lead) - static_cast<int>(type));
    };
    const auto target = p.outputs[0].atoms[0].type;
    const auto targetMass = weight(target);
    if (!targetMass) return {};
    std::size_t sourceMass = 0;
    for (const auto& atom : p.inputs[0].atoms) {
        if (!weight(atom.type) || atom.type < target ||
            (atom.type != target && !(p.permissionBits & (1ULL << 15))))
            return {};
        sourceMass += weight(atom.type);
    }
    if (!p.inputs[0].bonds.empty() && !(p.permissionBits & (1ULL << 9))) return {};
    if (std::any_of(p.inputs[0].bonds.begin(), p.inputs[0].bonds.end(),
                    [](const auto& bond) { return bond.bits != 1; }))
        return {};
    std::int64_t height = std::numeric_limits<std::int64_t>::max();
    for (int rotation = 0; rotation < 6; ++rotation) {
        auto low = std::numeric_limits<std::int64_t>::max();
        auto high = std::numeric_limits<std::int64_t>::min();
        for (const auto& atom : p.inputs[0].atoms) {
            std::int64_t q = atom.position.q, r = atom.position.r;
            for (int turn = 0; turn < rotation; ++turn) {
                const auto old = q;
                q = -r;
                r += old;
            }
            low = std::min(low, r);
            high = std::max(high, r);
        }
        height = std::min(height, high - low + 1);
    }
    // The ordinary inventory route excludes every orientation in this domain.
    if (height != 4) return {};
    const auto divisor = std::gcd(sourceMass, targetMass);
    const auto products = sourceMass / divisor;
    if (products > 12 || (6ULL * p.outputMultiplier) % products) return {};
    return MetalPeriod{products, targetMass / divisor};
}
} // namespace

bool SupportsClosedWideMetalInventory(const io::PuzzleData& puzzle) {
    return Period(puzzle).has_value();
}

std::optional<io::SolutionData> CompileAcceptedClosedWideMetalInventory(
    const io::PuzzleData& puzzle, const std::function<bool(const io::SolutionData&)>& accept,
    std::chrono::steady_clock::time_point deadline,
    const std::function<void(const std::string&)>& feedback) {
    const auto period = Period(puzzle);
    if (!period || !accept) return {};
    deadline = std::min(deadline, std::chrono::steady_clock::now() + std::chrono::seconds(3));
    if (std::chrono::steady_clock::now() >= deadline) return {};
    InventorySearchLimits limits;
    limits.multiStagePurification = true;
    limits.mechanicalExtractionOrder = true;
    limits.deadline = deadline;
    const auto search = SearchSupplyInventory(puzzle, period->products, limits);
    if (search.status != InventorySearchStatus::Found) {
        if (feedback) feedback("closed wide-metal inventory search did not find a complete period");
        return {};
    }
    if (search.plan.usage.size() != 1 || search.plan.usage[0].batches != period->batches ||
        search.plan.usage[0].acquired != period->batches * puzzle.inputs[0].atoms.size())
        throw std::invalid_argument(
            "closed wide-metal inventory has inconsistent source accounting");
    JointSupplyPlan joint;
    joint.usage = search.plan.usage;
    joint.productsPerPeriod = search.plan.productsPerPeriod;
    joint.recipeConstrained = search.plan.recipeConstrained;
    for (const auto& step : search.plan.steps)
        joint.steps.push_back({step, 0});
    auto execution = [&] {
        try {
            auto material = BuildJointSupplyExecutionIR(puzzle, joint);
            ExecutionIR::RequireComplete(material.Replay(puzzle), {period->products});
            return MakeExecutionPlan(puzzle, std::move(material));
        } catch (const ExecutionIR::Error& error) {
            throw std::invalid_argument(error.what());
        }
    }();
    if (std::chrono::steady_clock::now() >= deadline) return {};
    std::size_t submissions = 0;
    return CompileAcceptedSparseSourceSerialArmExecution(
        puzzle, execution,
        [&](const auto& candidate) {
            if (submissions >= AssemblySolver::MaxCandidateSubmissions ||
                std::chrono::steady_clock::now() >= deadline)
                return false;
            ++submissions;
            return accept(candidate);
        },
        feedback, deadline);
}
} // namespace opusoptima::solvers
