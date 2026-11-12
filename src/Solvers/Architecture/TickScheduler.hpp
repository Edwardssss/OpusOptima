#pragma once

#include "ModuleLibrary.hpp"

#include <vector>

namespace opusoptima::solvers::architecture {

// ── M-C: tick scheduler ──────────────────────────────────────────────────────
//
// Every machine this project emitted before M-C advanced a single `time` cursor:
// each new arm's tape started where the previous arm's tape ended, so the period
// was the SUM of all atom work (`ticks per grab` ≈ 12.8 against the reference's
// 3.8). Upstream instead aligns module *offsets*: all modules work inside the same
// period and the period is the slowest module, not the sum.
//
// The scheduler is pure data: it consumes `ModuleShape`s and returns per-module
// offsets. The compiler applies the offsets to the module tapes.

struct ModuleSchedule {
    /// Start tick of each module inside one period.
    std::vector<int> offsets;
    /// Ticks one production period lasts.
    int period{};
    /// Ticks until the first product leaves the machine.
    int fill{};
};

enum class SchedulePolicy {
    /// Legacy: module i starts when module i-1 emits its first element.
    Serial,
    /// Every module starts at tick 0; period = slowest module.
    Pipelined,
    /// Modules start at evenly spread ticks; period = slowest module + spread.
    Staggered,
};

[[nodiscard]] const char* SchedulePolicyName(SchedulePolicy policy);

class TickScheduler {
    public:
    /// Aligns `modules` (already in physical pipeline order) into one period.
    [[nodiscard]] static ModuleSchedule Align(const std::vector<ModuleShape>& modules,
                                              SchedulePolicy policy);
};

} // namespace opusoptima::solvers::architecture
