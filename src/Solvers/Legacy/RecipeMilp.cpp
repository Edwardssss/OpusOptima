#include "RecipeMilp.hpp"

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "LogManager.hpp"
#include <spdlog/spdlog.h>

#include "Highs.h"

namespace opusoptima::solvers {

namespace {

[[nodiscard]] std::map<core::AtomType, std::size_t> CountTypes(
    const std::vector<core::AtomType>& types) {
    std::map<core::AtomType, std::size_t> counts;
    for (auto type : types)
        counts[type]++;
    return counts;
}

RecipeResult RunHiGHSAssignment(const std::vector<core::AtomType>& inputTypes,
                                const std::vector<core::AtomType>& outputTypes) {
    RecipeResult result;
    const std::size_t numInputs = inputTypes.size();
    const std::size_t numOutputs = outputTypes.size();

    if (numInputs == 0 || numOutputs == 0) {
        result.success = false;
        result.description = "No atoms to match";
        return result;
    }

    Highs highs;
    highs.setOptionValue("output_flag", false);

    const std::size_t numVars = numInputs * numOutputs;
    std::vector<double> colCost(numVars, 0.0);
    for (std::size_t i = 0; i < numInputs; ++i) {
        for (std::size_t j = 0; j < numOutputs; ++j) {
            const std::size_t varIdx = (i * numOutputs) + j;
            if (inputTypes[i] != outputTypes[j]) {
                constexpr double kPenalty = 100.0;
                colCost[varIdx] = kPenalty;
            } else {
                colCost[varIdx] = 1.0;
            }
        }
    }

    const std::size_t numRows = numOutputs + numInputs;
    std::vector<double> rowLower(numRows);
    std::vector<double> rowUpper(numRows);
    std::vector<double> colLower(numVars, 0.0);
    std::vector<double> colUpper(numVars, 1.0);
    std::vector<HighsInt> integrality(numVars, 1);
    for (std::size_t j = 0; j < numOutputs; ++j) {
        rowLower[j] = 1.0;
        rowUpper[j] = 1.0;
    }
    for (std::size_t i = 0; i < numInputs; ++i) {
        rowLower[numOutputs + i] = 0.0;
        rowUpper[numOutputs + i] = 1.0;
    }

    std::vector<HighsInt> aStart;
    std::vector<HighsInt> aIndex;
    std::vector<double> aValue;
    for (std::size_t i = 0; i < numInputs; ++i) {
        for (std::size_t j = 0; j < numOutputs; ++j) {
            aStart.push_back(static_cast<HighsInt>(aIndex.size()));
            aIndex.push_back(static_cast<HighsInt>(j));
            aValue.push_back(1.0);
            aIndex.push_back(static_cast<HighsInt>(numOutputs + i));
            aValue.push_back(1.0);
        }
    }
    aStart.push_back(static_cast<HighsInt>(aIndex.size()));

    HighsStatus status =
        highs.passModel(static_cast<HighsInt>(numVars), static_cast<HighsInt>(numRows),
                        static_cast<HighsInt>(aIndex.size()), 1, 1, 0.0, colCost.data(),
                        colLower.data(), colUpper.data(), rowLower.data(), rowUpper.data(),
                        aStart.data(), aIndex.data(), aValue.data(), integrality.data());

    if (status == HighsStatus::kError) {
        result.success = false;
        result.description = "Failed to pass model to HiGHS";
        return result;
    }

    status = highs.run();
    if (status == HighsStatus::kError) {
        result.success = false;
        result.description = "HiGHS solver failed";
        return result;
    }

    auto modelStatus = highs.getModelStatus();
    if (modelStatus != HighsModelStatus::kOptimal) {
        result.success = false;
        result.description =
            "No feasible assignment found: " + std::to_string(static_cast<int>(modelStatus));
        return result;
    }

    auto solution = highs.getSolution();
    result.inputAtomUsage.resize(numInputs, 0);
    result.outputAtomYield.resize(numOutputs, 0);
    constexpr double kThreshold = 0.5;
    for (std::size_t i = 0; i < numInputs; ++i) {
        for (std::size_t j = 0; j < numOutputs; ++j) {
            if (solution.col_value[(i * numOutputs) + j] > kThreshold) {
                result.inputAtomUsage[i] = 1;
                result.outputAtomYield[j] = 1;
            }
        }
    }
    result.success = true;
    auto info = highs.getInfo();
    result.estimatedCost = static_cast<std::int32_t>(info.objective_function_value);
    result.description = "HiGHS MIP assignment solved";
    return result;
}

} // namespace

RecipeMilpSolver::RecipeMilpSolver() : BaseSolver("RecipeMilp") {
    auto logger = opusoptima::GetLoggerOrFallback("solver");
    logger->debug("RecipeMilpSolver constructed");
}

RecipeResult RecipeMilpSolver::SolveTypes(const std::vector<core::AtomType>& inputTypes,
                                          const std::vector<core::AtomType>& outputTypes) {
    auto logger = opusoptima::GetLoggerOrFallback("solver");
    auto inputCounts = CountTypes(inputTypes);
    auto outputCounts = CountTypes(outputTypes);

    bool feasible = true;
    for (auto& [type, needed] : outputCounts) {
        const auto input_count_it = inputCounts.find(type);
        const std::size_t avail =
            (input_count_it != inputCounts.end()) ? input_count_it->second : 0;
        if (needed > avail) {
            feasible = false;
            break;
        }
    }

    if (feasible) {
        logger->debug("Type-count matching feasible — {} inputs, {} outputs", inputTypes.size(),
                      outputTypes.size());
        RecipeResult result;
        result.success = true;
        result.description = "Type-count matching succeeded";
        result.inputAtomUsage.resize(inputTypes.size(), 0);
        result.outputAtomYield.resize(outputTypes.size(), 0);
        std::vector<bool> used(inputTypes.size(), false);
        for (std::size_t j = 0; j < outputTypes.size(); ++j) {
            for (std::size_t i = 0; i < inputTypes.size(); ++i) {
                if (!used[i] && inputTypes[i] == outputTypes[j]) {
                    used[i] = true;
                    result.inputAtomUsage[i] = 1;
                    result.outputAtomYield[j] = 1;
                    break;
                }
            }
        }
        return result;
    }

    logger->info("Type-count matching infeasible; launching HiGHS MILP");
    return RunHiGHSAssignment(inputTypes, outputTypes);
}

io::SolutionData RecipeMilpSolver::Solve(const io::PuzzleData& puzzle) {
    auto logger = opusoptima::GetLoggerOrFallback("solver");
    logger->info("RecipeMilp solving puzzle '{}' ({} inputs, {} outputs)", puzzle.name,
                 puzzle.inputs.size(), puzzle.outputs.size());

    std::vector<core::AtomType> inputTypes;
    std::vector<core::AtomType> outputTypes;
    for (const auto& molecule : puzzle.inputs)
        for (const auto& atom : molecule.atoms)
            inputTypes.push_back(atom.type);
    for (const auto& molecule : puzzle.outputs)
        for (const auto& atom : molecule.atoms)
            outputTypes.push_back(atom.type);

    auto recipe = SolveTypes(inputTypes, outputTypes);

    io::SolutionData sol;
    sol.puzzleName = puzzle.name;
    sol.solutionName = "RecipeMilp Solution";
    sol.metrics.cost = recipe.estimatedCost;
    sol.metrics.cycles = 0;
    sol.metrics.area = 1;
    logger->info("RecipeMilp result — success={}, cost={}, description='{}'", recipe.success,
                 recipe.estimatedCost, recipe.description);
    return sol;
}

std::int32_t RecipeMilpSolver::EstimateCycles(const io::PuzzleData& puzzle) {
    static_cast<void>(puzzle);
    return 0;
}

// ── Self-registration ─────────────────────────────────────────

namespace {
const bool kRegistered = [] {
    BaseSolver::Register("RecipeMilp", [] { return std::make_unique<RecipeMilpSolver>(); });
    // aliases for CLI convenience
    BaseSolver::Register("milp", [] { return std::make_unique<RecipeMilpSolver>(); });
    BaseSolver::Register("mip", [] { return std::make_unique<RecipeMilpSolver>(); });
    return true;
}();
} // namespace

} // namespace opusoptima::solvers
