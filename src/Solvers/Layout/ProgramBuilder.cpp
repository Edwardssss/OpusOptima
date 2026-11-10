#include "ProgramBuilder.hpp"

#include <algorithm>
#include <stdexcept>

namespace opusoptima::solvers {
namespace {
const std::vector<PlacedInstruction> kEmptyArm{};
} // namespace

std::int64_t ProgramBuilder::Add(ProgramFragment fragment) {
    if (fragment.arm.empty()) throw std::invalid_argument("program fragment needs an arm");

    auto& tape = arms_[fragment.arm];
    ++fragmentCount_;
    if (fragment.instructions.empty()) return fragment.anchor;

    // Relative ticks are normalised against the earliest instruction so the
    // anchor and the returned start tick are expressed in the same frame.
    std::int64_t first = fragment.instructions.front().tick;
    for (const auto& instruction : fragment.instructions)
        first = std::min(first, instruction.tick);
    // A fragment may never place two actions of one arm on the same tick: no
    // shift can separate them, so this is a malformed fragment, not a conflict.
    for (std::size_t a = 0; a < fragment.instructions.size(); ++a)
        for (std::size_t b = a + 1; b < fragment.instructions.size(); ++b)
            if (fragment.instructions[a].tick == fragment.instructions[b].tick)
                throw std::invalid_argument("program fragment repeats a tick on one arm");

    // Only the same arm constrains the start tick: two different arms may share
    // a tick, which is the whole source of free parallelism.
    auto fits = [&](std::int64_t start) {
        for (const auto& instruction : fragment.instructions)
            if (OccupiedBy(fragment.arm, start + (instruction.tick - first))) return false;
        return true;
    };

    std::int64_t start = std::max<std::int64_t>(0, fragment.anchor);
    while (!fits(start))
        ++start;

    for (const auto& instruction : fragment.instructions) {
        const auto tick = start + (instruction.tick - first);
        tape.push_back({instruction.action, tick});
        if (std::find(occupied_.begin(), occupied_.end(), tick) == occupied_.end())
            occupied_.push_back(tick);
    }
    std::sort(tape.begin(), tape.end(), [](const PlacedInstruction& a, const PlacedInstruction& b) {
        return a.tick < b.tick;
    });
    std::sort(occupied_.begin(), occupied_.end());
    makespan_ = std::max<std::int64_t>(makespan_, tape.back().tick + 1);
    return start;
}

const std::vector<PlacedInstruction>& ProgramBuilder::Arm(const std::string& arm) const {
    const auto found = arms_.find(arm);
    return found == arms_.end() ? kEmptyArm : found->second;
}

std::int64_t ProgramBuilder::Makespan() const {
    return makespan_;
}

bool ProgramBuilder::OccupiedBy(const std::string& arm, std::int64_t tick) const {
    for (const auto& placed : Arm(arm))
        if (placed.tick == tick) return true;
    return false;
}

} // namespace opusoptima::solvers
