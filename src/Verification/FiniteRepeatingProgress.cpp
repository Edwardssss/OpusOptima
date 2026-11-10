#include "FiniteRepeatingProgress.hpp"

#include <algorithm>
#include <stdexcept>

#include "IndependentCapabilities.hpp"

namespace opusoptima::verification {
FiniteRepeatingProgress::FiniteRepeatingProgress(
    std::span<const FiniteRepeatingPortConfig> ports,
    std::chrono::steady_clock::time_point enclosingDeadline)
    : portCount_(ports.size()),
      deadline_(std::min(enclosingDeadline,
                         std::chrono::steady_clock::now() + std::chrono::seconds(2))) {
    if (ports.empty() || ports.size() > ports_.size())
        throw std::invalid_argument("finite repeating port bound");
    for (std::size_t i = 0; i < ports.size(); ++i) {
        const auto config = ports[i];
        if (!config.maxMonomers || config.maxMonomers > finiteOrdinaryProfile.maxInventory ||
            !config.outputMultiplier || config.outputMultiplier > 8 ||
            !config.targetMonomers || config.targetMonomers > 6 ||
            config.targetMonomers > config.maxMonomers)
            throw std::invalid_argument("finite repeating configuration bound");
        ports_[i] = {config, {0, 0, config.targetMonomers * config.outputMultiplier, false}};
    }
}

std::optional<FiniteRepeatingPortProgress> FiniteRepeatingProgress::Port(std::size_t port) const {
    if (port >= portCount_) return std::nullopt;
    return ports_[port].progress;
}

FiniteRepeatingProgressResult FiniteRepeatingProgress::Observe(
    std::size_t port, const io::Molecule& rawTemplate, const io::Molecule& component,
    core::HexCoord displacement) {
    if (port >= portCount_)
        return FiniteRepeatingGraphFailure{RepeatingGraphFailureCode::OutsideDomain,
                                          "finite repeating port bound"};
    const auto expired = [&] { return std::chrono::steady_clock::now() >= deadline_; };
    if (expired())
        return FiniteRepeatingGraphFailure{RepeatingGraphFailureCode::ResourceUnavailable,
                                          "finite repeating total budget"};
    auto& state = ports_[port];
    // Bound the checker's repeated prefix comparisons before entering its non-preemptible work.
    constexpr std::size_t maxComparisonWork = 65536;
    if (rawTemplate.atoms.size() > finiteOrdinaryProfile.maxMoleculeAtoms ||
        component.atoms.size() > finiteOrdinaryProfile.maxInventory)
        return FiniteRepeatingGraphFailure{RepeatingGraphFailureCode::OutsideDomain,
                                          "finite repeating graph resource bound"};
    const auto monomers = state.config.maxMonomers;
    const auto workPerPrefix = component.atoms.size() + rawTemplate.atoms.size() * monomers;
    if (workPerPrefix && monomers > maxComparisonWork / workPerPrefix)
        return FiniteRepeatingGraphFailure{RepeatingGraphFailureCode::ResourceUnavailable,
                                          "finite repeating comparison budget"};
    const auto result = CheckFiniteRepeatingGraph(
        {rawTemplate, component, displacement, state.config.maxMonomers});
    if (const auto* failure = std::get_if<FiniteRepeatingGraphFailure>(&result)) return *failure;
    auto next = state.progress;
    next.highWaterMonomers = std::max(next.highWaterMonomers,
                                     std::get<FiniteRepeatingGraphChecked>(result).provenPrefix);
    next.outputCount = next.highWaterMonomers * state.config.outputMultiplier;
    next.reached = next.outputCount >= next.targetOutputs;
    if (expired())
        return FiniteRepeatingGraphFailure{RepeatingGraphFailureCode::ResourceUnavailable,
                                          "finite repeating total budget"};
    state.progress = next;
    return next;
}
} // namespace opusoptima::verification
