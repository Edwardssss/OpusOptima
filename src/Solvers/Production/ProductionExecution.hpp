#pragma once

#include "ExecutionIR.hpp"
#include "ProductionLayout.hpp"

#include <map>
#include <optional>

namespace opusoptima::solvers {
struct ProductionRoutePlan;

// A single material ledger for static production. Room ownership and conduit
// residency are physical constraints, not fields in ExecutionIR's material state.
// No method below certifies arm sweeps or game-specific conduit latency; a
// serialized machine still requires native independent acceptance.
class ProductionExecution {
    public:
    using AtomId = ExecutionIR::AtomId;
    struct Transfer {
        ProductionConduitHandoff handoff;
        std::size_t sourceRoom{}, destinationRoom{};
        int arrivalCycle{};
        std::vector<AtomId> atoms;
    };
    explicit ProductionExecution(const io::PuzzleData& puzzle);
    void Apply(std::size_t room, const ExecutionIR::Operation& operation);
    void React(std::size_t room, const ExecutionIR::Operation& operation,
               const std::vector<core::HexCoord>& productCells);
    void Release(const Transfer& transfer);
    void Arrive(std::size_t conduit, int cycle);
    void Pickup(std::size_t conduit, int cycle);
    void RequireComplete(const std::vector<std::size_t>& outputCounts) const;
    [[nodiscard]] const auto& Material() const {
        return state_;
    }
    [[nodiscard]] const auto& Program() const {
        return program_;
    }
    [[nodiscard]] const auto& Rooms() const {
        return roomOf_;
    }
    [[nodiscard]] std::size_t InFlight() const {
        return pending_.size();
    }

    private:
    struct Pending {
        Transfer transfer;
        bool arrived{};
    };
    void Commit(std::size_t room, const ExecutionIR::Operation& operation,
                const std::vector<core::HexCoord>* productCells);
    void CheckRooms(const ExecutionIR::State& state, const std::map<AtomId, std::size_t>& owners,
                    const std::map<std::size_t, Pending>& transfers) const;
    const io::PuzzleData& puzzle_;
    std::vector<ProductionChamberLayout> chambers_;
    ExecutionIR program_;
    ExecutionIR::State state_;
    std::map<AtomId, std::size_t> roomOf_;
    std::map<std::size_t, Pending> pending_;
    std::map<std::size_t, int> conduitIds_;
    std::map<std::size_t, int> lastPickup_;
};

// Apply an existing rigid transport template to the ORIGINAL static-production
// puzzle's shared material ledger before handing the machine to native acceptance.
void ValidateProductionTransportMaterial(const io::PuzzleData& puzzle,
                                         const ProductionTransportPlan& plan);
void ValidateProductionRouteMaterial(const io::PuzzleData& puzzle, const ProductionRoutePlan& plan,
                                     int rotation = 0);
void ValidateProductionCoproductMaterial(const io::PuzzleData& puzzle,
                                         const ProductionTransportPlan& plan,
                                         const std::vector<int>& owner,
                                         const std::vector<io::SolutionObject>& ports);

} // namespace opusoptima::solvers
