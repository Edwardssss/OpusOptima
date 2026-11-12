#include "SolutionParameters.hpp"

#include <algorithm>
#include <stdexcept>

namespace opusoptima::solvers::architecture {

void ParameterRegistry::Define(std::string name, std::vector<std::string> values,
                               std::string defaultValue) {
    if (name.empty()) throw std::invalid_argument("parameter needs a name");
    if (values.empty()) throw std::invalid_argument("parameter needs at least one value");
    if (std::find(values.begin(), values.end(), defaultValue) == values.end())
        throw std::invalid_argument("parameter default is not one of its values");
    for (const auto& [existing, parameter] : entries_) {
        (void)parameter;
        if (existing == name) throw std::invalid_argument("parameter declared twice");
    }
    entries_.emplace_back(std::move(name), SolutionParameter{std::move(values), std::move(defaultValue)});
}

std::map<std::string, std::string> ParameterRegistry::Defaults() const {
    std::map<std::string, std::string> setting;
    for (const auto& [name, parameter] : entries_) setting[name] = parameter.defaultValue;
    return setting;
}

std::vector<std::map<std::string, std::string>> ParameterRegistry::Enumerate(
    std::size_t cap) const {
    std::vector<std::map<std::string, std::string>> settings(1);
    for (const auto& [name, parameter] : entries_) {
        std::vector<std::map<std::string, std::string>> next;
        next.reserve(settings.size() * parameter.values.size());
        for (const auto& setting : settings)
            for (const auto& value : parameter.values) {
                if (next.size() >= cap) break;
                auto copy = setting;
                copy[name] = value;
                next.push_back(std::move(copy));
            }
        settings = std::move(next);
    }
    return settings;
}

std::string ParameterRegistry::Key(const std::map<std::string, std::string>& setting) {
    std::string key;
    for (const auto& [name, value] : setting) {
        key += name;
        key += '=';
        key += value;
        key += ';';
    }
    return key;
}

std::vector<std::map<std::string, std::string>> ParameterRegistry::Deduplicate(
    std::vector<std::map<std::string, std::string>> settings) {
    std::vector<std::map<std::string, std::string>> unique;
    std::vector<std::string> seen;
    unique.reserve(settings.size());
    for (auto& setting : settings) {
        auto key = Key(setting);
        if (std::find(seen.begin(), seen.end(), key) != seen.end()) continue;
        seen.push_back(std::move(key));
        unique.push_back(std::move(setting));
    }
    return unique;
}

bool ObjectiveSelector::Submit(const io::SolutionData& solution) {
    // Only measured machines compete: a zero metric means the gate never reported
    // one, and letting it win would publish an unmeasured solution.
    auto update = [&](std::optional<io::SolutionData>& best,
                      std::int32_t io::SolutionMetrics::*field) {
        const std::int32_t candidate = solution.metrics.*field;
        if (candidate <= 0) return false;
        if (!best || candidate < best->metrics.*field) {
            best = solution;
            return true;
        }
        return false;
    };
    bool improved = update(cost_, &io::SolutionMetrics::cost);
    improved = update(cycles_, &io::SolutionMetrics::cycles) || improved;
    improved = update(area_, &io::SolutionMetrics::area) || improved;
    return improved;
}

std::optional<io::SolutionData> ObjectiveSelector::Publish() const {
    if (Empty()) return std::nullopt;
    // Slot wins against the cost baseline: cycles and area are the objectives the
    // reference machines win, so a machine that improves both is preferred even if
    // it costs more.
    if (cycles_ && area_) return *cycles_;
    if (cycles_) return *cycles_;
    if (cost_) return *cost_;
    return *area_;
}

} // namespace opusoptima::solvers::architecture
