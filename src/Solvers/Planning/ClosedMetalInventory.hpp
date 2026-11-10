#pragma once

#include "BaseSolver.hpp"
#include <chrono>
#include <functional>
#include <optional>
#include <string>

namespace opusoptima::solvers {
bool SupportsClosedWideMetalInventory(const io::PuzzleData&);
std::optional<io::SolutionData> CompileAcceptedClosedWideMetalInventory(
    const io::PuzzleData&, const std::function<bool(const io::SolutionData&)>& accept,
    std::chrono::steady_clock::time_point deadline,
    const std::function<void(const std::string&)>& feedback = {});
} // namespace opusoptima::solvers
