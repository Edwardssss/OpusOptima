#include "TickScheduler.hpp"

#include <algorithm>

namespace opusoptima::solvers::architecture {

const char* SchedulePolicyName(SchedulePolicy policy) {
    switch (policy) {
    case SchedulePolicy::Serial: return "serial";
    case SchedulePolicy::Pipelined: return "pipelined";
    case SchedulePolicy::Staggered: return "staggered";
    }
    return "unknown";
}

ModuleSchedule TickScheduler::Align(const std::vector<ModuleShape>& modules,
                                   SchedulePolicy policy) {
    ModuleSchedule schedule;
    schedule.offsets.assign(modules.size(), 0);
    if (modules.empty()) return schedule;

    switch (policy) {
    case SchedulePolicy::Serial: {
        // Legacy model: the next module only begins after the previous one has
        // finished its whole period, so the period is the sum of all module work.
        int cursor = 0;
        for (std::size_t index = 0; index < modules.size(); ++index) {
            schedule.offsets[index] = cursor;
            cursor += std::max(1, modules[index].period);
        }
        schedule.period = std::max(1, cursor);
        schedule.fill = schedule.period;
        return schedule;
    }
    case SchedulePolicy::Pipelined: {
        // Upstream model: every module starts at tick 0. Period = slowest module.
        int period = 1;
        for (const auto& module : modules) period = std::max(period, std::max(1, module.period));
        schedule.period = period;
        schedule.fill = period;
        return schedule;
    }
    case SchedulePolicy::Staggered: {
        // Compromise: spread the module starts evenly over one period so no two
        // arms are ever active on the same tick, for layouts whose cells overlap.
        int slowest = 1;
        for (const auto& module : modules) slowest = std::max(slowest, std::max(1, module.period));
        const int count = static_cast<int>(modules.size());
        for (int index = 0; index < count; ++index)
            schedule.offsets[static_cast<std::size_t>(index)] = (index * slowest) / count;
        schedule.period = slowest;
        int fill = 0;
        for (int index = 0; index < count; ++index)
            fill = std::max(fill, schedule.offsets[static_cast<std::size_t>(index)] +
                                      std::max(1, modules[static_cast<std::size_t>(index)].latency));
        schedule.fill = std::max(schedule.period, fill);
        return schedule;
    }
    }
    return schedule;
}

} // namespace opusoptima::solvers::architecture
