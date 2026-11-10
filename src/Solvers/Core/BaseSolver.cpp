#include "BaseSolver.hpp"

#include <algorithm>

namespace opusoptima::solvers {

void BaseSolver::Register(std::string name, Factory factory) {
    Registry().emplace(std::move(name), std::move(factory));
}

std::unique_ptr<BaseSolver> BaseSolver::Create(const std::string& name) {
    auto& registry = Registry();
    const auto iter = registry.find(name);
    if (iter == registry.end()) {
        return nullptr;
    }
    return iter->second();
}

std::vector<std::string> BaseSolver::RegisteredNames() {
    std::vector<std::string> names;
    names.reserve(Registry().size());
    for (const auto& [name, _] : Registry()) {
        names.push_back(name);
    }
    std::ranges::sort(names);
    return names;
}

std::unordered_map<std::string, BaseSolver::Factory>& BaseSolver::Registry() {
    static std::unordered_map<std::string, Factory> registry;
    return registry;
}

} // namespace opusoptima::solvers
