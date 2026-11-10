#pragma once

#include "ExecutionIR.hpp"

namespace opusoptima::solvers {

// The supplied state must already use physical world coordinates. This checks
// custody/contact at instruction boundaries, not motion sweeps or glyph timing.
class FragmentCustody {
    public:
    using AtomId = ExecutionIR::AtomId;
    struct Parking {
        std::vector<AtomId> atoms;
        std::map<AtomId, ExecutionIR::Atom> material;
        std::map<ExecutionIR::Edge, std::uint8_t> bonds;
    };
    void Grab(const ExecutionIR::State&, std::size_t arm, core::HexCoord tip, AtomId);
    void Drop(std::size_t arm);
    void Park(const ExecutionIR::State&, std::size_t arm, std::size_t slot);
    // Register a fragment left on the board after a cut; no synthetic grab/drop.
    void ReserveLoose(const ExecutionIR::State&, AtomId, std::size_t slot);
    void Retrieve(const ExecutionIR::State&, std::size_t slot, std::size_t arm, core::HexCoord tip,
                  AtomId);
    void Validate(const ExecutionIR::State&) const;
    // Consume one physical parking reservation when bonding it to held material.
    void MergeParked(const ExecutionIR::State& before, const ExecutionIR::State& after,
                     std::size_t arm, std::size_t slot);
    // Remove reservations only for consumed isolated atoms; all other snapshots survive.
    void ConsumeSingletons(const ExecutionIR::State& before, const ExecutionIR::State& after,
                           const std::vector<AtomId>& consumed);
    // Rebind a held projection endpoint while preserving its fragment and all
    // unrelated parking reservations; only an isolated parked reagent is consumed.
    void ProjectHeld(const ExecutionIR::State& before, const ExecutionIR::State& after,
                     std::size_t arm, AtomId metal, AtomId mercury, AtomId product);
    [[nodiscard]] const auto& Held() const {
        return held_;
    }
    [[nodiscard]] const auto& Parked() const {
        return parked_;
    }

    private:
    std::map<std::size_t, AtomId> held_;
    std::map<std::size_t, Parking> parked_;
};

} // namespace opusoptima::solvers
