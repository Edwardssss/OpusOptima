#include "ChemicalPlanner.hpp"

#include "Highs.h"
#include <cmath>
#include <stdexcept>

namespace opusoptima::solvers {
bool ChemicalPlanner::CanSupply(core::AtomType source, core::AtomType requested,
                                std::uint64_t permissions) {
    const auto id = static_cast<int>(source);
    const auto goal = static_cast<int>(requested);
    return source == requested ||
           (requested == core::AtomType::Salt && id >= 2 && id <= 5 &&
            (permissions & (1ULL << 12))) ||
           (source == core::AtomType::Salt && goal >= 2 && goal <= 5 &&
            (permissions & (1ULL << 13))) ||
           (id >= 8 && id <= 12 && goal >= 7 && goal < id && (permissions & (1ULL << 14)));
}

ChemicalRecipe ChemicalPlanner::Plan(const io::PuzzleData& p) {
    return Plan(p, 1, true);
}

ChemicalRecipe ChemicalPlanner::Plan(const io::PuzzleData& p, std::size_t products,
                                     bool allowSurplus,
                                     std::chrono::steady_clock::time_point deadline) {
    return PlanRecipe(p, products, allowSurplus, false, deadline);
}

ChemicalRecipe ChemicalPlanner::PlanWithWheelCatalysts(const io::PuzzleData& p,
                                                       std::size_t products, bool allowSurplus) {
    return PlanRecipe(p, products, allowSurplus, true);
}

ChemicalRecipe ChemicalPlanner::PlanRecipe(const io::PuzzleData& p, std::size_t products,
                                           bool allowSurplus, bool wheelCatalysts,
                                           std::chrono::steady_clock::time_point deadline) {
    if (!products || products > 12)
        throw std::invalid_argument("chemical recipe period must be between 1 and 12 products");
    constexpr int elements = 16;
    if (p.outputs.empty() ||
        std::any_of(p.outputs.begin(), p.outputs.end(),
                    [](const auto& output) { return output.atoms.empty(); }) ||
        p.inputs.empty())
        throw std::invalid_argument("chemical recipe requires inputs and finite outputs");
    const int inputs = static_cast<int>(p.inputs.size());
    constexpr int reactions = 21;
    const int columns = inputs + reactions + elements;
    std::vector<double> cost(columns), lower(columns), upper(columns, kHighsInf);
    std::vector<double> demand(elements), rowUpper(elements);
    std::vector<HighsInt> start, index, integer(columns, 1);
    std::vector<double> value;
    auto add = [&](core::AtomType type, double amount) {
        const int id = static_cast<int>(type) - 1;
        if (id < 0 || id >= elements) throw std::invalid_argument("unsupported chemical element");
        index.push_back(id);
        value.push_back(amount);
    };
    for (const auto& output : p.outputs)
        for (const auto& atom : output.atoms) {
            const int id = static_cast<int>(atom.type) - 1;
            if (id < 0 || id >= elements || atom.type == core::AtomType::Repeat)
                throw std::invalid_argument("unsupported chemical target");
            demand[id] += static_cast<double>(products);
        }
    // Cardinal atoms may be duplicated as intermediates consumed by unification,
    // even when no cardinal survives in the final output.
    std::vector<double> catalystBound(4);
    for (int i = 0; i < 4; ++i)
        catalystBound[i] =
            demand[i + 1] + demand[static_cast<int>(core::AtomType::Quintessence) - 1];
    for (const auto& input : p.inputs) {
        start.push_back(index.size());
        // Aggregate duplicate element rows in a molecule before passing the sparse matrix.
        std::vector<int> counts(elements);
        for (const auto& atom : input.atoms) {
            const int id = static_cast<int>(atom.type) - 1;
            if (id < 0 || id >= elements || atom.type == core::AtomType::Repeat)
                throw std::invalid_argument("unsupported chemical input");
            ++counts[id];
        }
        for (int i = 0; i < elements; ++i)
            if (counts[i]) add(static_cast<core::AtomType>(i + 1), counts[i]);
        for (int i = 0; i < 4; ++i)
            if (counts[i + 1]) {
                index.push_back(elements + i);
                value.push_back(counts[i + 1] * catalystBound[i]);
            }
        cost[start.size() - 1] = input.atoms.size();
    }
    for (int i = 0; i < 4; ++i) {
        start.push_back(index.size());
        add(core::AtomType::Salt, 1);
        add(static_cast<core::AtomType>(i + 2), -1);
        cost[inputs + i] = 0.001;
        if (!(p.permissionBits & (1ULL << 12))) upper[inputs + i] = 0;
    }
    for (int i = 0; i < 5; ++i) {
        start.push_back(index.size());
        add(core::AtomType::Quicksilver, -1);
        add(static_cast<core::AtomType>(8 + i), -1);
        add(static_cast<core::AtomType>(7 + i), 1);
        cost[inputs + 4 + i] = 0.001;
        if (!(p.permissionBits & (1ULL << 14))) upper[inputs + 4 + i] = 0;
    }
    for (int i = 0; i < 4; ++i) {
        start.push_back(index.size());
        add(core::AtomType::Salt, -1);
        add(static_cast<core::AtomType>(i + 2), 1);
        if (!wheelCatalysts || !(p.permissionBits & (1ULL << 28))) {
            index.push_back(elements + i);
            value.push_back(-1);
        }
        cost[inputs + 9 + i] = 0.001;
        if (!(p.permissionBits & (1ULL << 13))) upper[inputs + 9 + i] = 0;
    }
    for (int i = 0; i < 5; ++i) {
        start.push_back(index.size());
        add(static_cast<core::AtomType>(8 + i), -2);
        add(static_cast<core::AtomType>(7 + i), 1);
        cost[inputs + 13 + i] = 0.001;
        if (!(p.permissionBits & (1ULL << 15))) upper[inputs + 13 + i] = 0;
    }
    start.push_back(index.size());
    add(core::AtomType::Salt, -2);
    add(core::AtomType::Vitae, 1);
    add(core::AtomType::Mors, 1);
    cost[inputs + 18] = 0.001;
    if (!(p.permissionBits & (1ULL << 16))) upper[inputs + 18] = 0;
    for (int reverse = 0; reverse < 2; ++reverse) {
        start.push_back(index.size());
        for (int i = 0; i < 4; ++i)
            add(static_cast<core::AtomType>(i + 2), reverse ? 1 : -1);
        add(core::AtomType::Quintessence, reverse ? -1 : 1);
        if (reverse)
            for (int i = 0; i < 4; ++i) {
                index.push_back(elements + i);
                value.push_back(catalystBound[i]);
            }
        cost[inputs + 19 + reverse] = 0.001;
        if (!(p.permissionBits & (1ULL << 18))) upper[inputs + 19 + reverse] = 0;
    }
    for (int i = 0; i < elements; ++i) {
        start.push_back(index.size());
        add(static_cast<core::AtomType>(i + 1), -1);
        if (!allowSurplus) upper[inputs + reactions + i] = 0;
    }
    start.push_back(index.size());
    rowUpper = demand;
    demand.resize(elements + 4, 0);
    rowUpper.resize(elements + 4, kHighsInf);
    Highs highs;
    highs.setOptionValue("output_flag", false);
    highs.setOptionValue("threads", 1);
    // HiGHS' restart bound bookkeeping can abort on recipes with reversible metal routes.
    highs.setOptionValue("mip_allow_restart", false);
    const auto remaining =
        std::chrono::duration<double>(deadline - std::chrono::steady_clock::now()).count();
    if (remaining <= 0) throw std::invalid_argument("chemical planning deadline");
    highs.setOptionValue("time_limit", std::min(2.0, remaining));
    const auto loadStatus = highs.passModel(
        columns, elements + 4, index.size(), 1, 1, 0.0, cost.data(), lower.data(), upper.data(),
        demand.data(), rowUpper.data(), start.data(), index.data(), value.data(), integer.data());
    if (loadStatus == HighsStatus::kError)
        throw std::invalid_argument("chemical recipe model loading failed");
    if (highs.run() == HighsStatus::kError)
        throw std::invalid_argument("chemical recipe solver execution failed");
    const auto status = highs.getModelStatus();
    if (status == HighsModelStatus::kInfeasible)
        throw std::invalid_argument("chemical recipe infeasible within supported reaction model "
                                    "(not proof of puzzle infeasibility)");
    if (status == HighsModelStatus::kTimeLimit)
        throw std::invalid_argument("chemical recipe time limit reached (feasibility unresolved)");
    if (status != HighsModelStatus::kOptimal)
        throw std::invalid_argument("chemical recipe did not reach optimality: " +
                                    highs.modelStatusToString(status));
    const auto& solution = highs.getSolution().col_value;
    ChemicalRecipe recipe;
    for (int i = 0; i < inputs; ++i)
        recipe.inputBatches.push_back(std::llround(solution[i]));
    for (int i = 0; i < 4; ++i)
        recipe.calcifications += std::llround(solution[inputs + i]);
    for (int i = 0; i < 5; ++i)
        recipe.projections += std::llround(solution[inputs + 4 + i]);
    for (int i = 0; i < 4; ++i) {
        const auto count = std::llround(solution[inputs + 9 + i]);
        recipe.duplicatedElements.push_back(count);
        recipe.duplications += count;
    }
    for (int i = 0; i < 5; ++i)
        recipe.purifications += std::llround(solution[inputs + 13 + i]);
    recipe.animismus = std::llround(solution[inputs + 18]);
    recipe.unifications = std::llround(solution[inputs + 19]);
    recipe.dispersions = std::llround(solution[inputs + 20]);
    for (int i = 0; i < elements; ++i)
        recipe.surplus.push_back(std::llround(solution[inputs + reactions + i]));
    return recipe;
}
} // namespace opusoptima::solvers
