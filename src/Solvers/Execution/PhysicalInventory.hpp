#pragma once

#include <chrono>

#include "FragmentCustody.hpp"
#include "HeldArmMotion.hpp"
#include "HexFrame.hpp"

namespace opusoptima::solvers {
// Single-arm physical inventory. The caller supplies world coordinates and an
// interval collision/glyph certificate; logical frames are never inferred.
class PhysicalInventory {
    public:
    using Certificate = std::function<bool(const ExecutionIR::State&, const HeldArmState&,
                                           const HeldArmState&, char, bool)>;
    PhysicalInventory(ExecutionIR::State state, std::vector<core::HexCoord> track, HeldArmState arm,
                      Certificate certificate, int maxLength = 1);
    // Optional wall-clock budget for route searches and pose retries.
    void SetDeadline(std::chrono::steady_clock::time_point deadline) { deadline_ = deadline; }
    void Grab(ExecutionIR::AtomId);
    void Park(std::size_t slot);
    void ReserveLoose(ExecutionIR::AtomId, std::size_t slot);
    void Retrieve(std::size_t slot, ExecutionIR::AtomId);
    // Searches reachable grip poses without changing inventory when no route succeeds.
    [[nodiscard]] MotionStatus RetrieveRouted(std::size_t slot, ExecutionIR::AtomId,
                                              std::size_t budget = 20000);
    // Also searches the fragment's atoms; the budget is per contact pose.
    [[nodiscard]] MotionStatus RetrieveFragmentRouted(std::size_t slot, std::size_t budget = 20000);
    // Searches grips and transport together; goal is relative to the named atom.
    // The budget applies separately to each empty and held route candidate.
    [[nodiscard]] MotionStatus MoveParked(std::size_t slot, ExecutionIR::AtomId anchor,
                                          RigidPose goal, std::size_t budget = 20000);
    using BondCertificate = std::function<bool(const ExecutionIR::Step&, const HeldArmState&)>;
    // Material transitions are recomputed, never accepted as caller-supplied state.
    void ApplyBond(const io::PuzzleData&, const ExecutionIR::Operation&, const BondCertificate&);
    void BondHeldToParked(std::size_t slot, const io::PuzzleData&, const ExecutionIR::Operation&,
                          const BondCertificate&);
    // Commits retrieval and glyph activation together, retrying other grips on rejection.
    // Bond may consume a separate parked partner; either operand may select the source slot.
    [[nodiscard]] MotionStatus ApplyParkedBond(std::size_t slot, const io::PuzzleData&,
                                               const ExecutionIR::Operation&,
                                               const BondCertificate&, std::size_t budget = 20000);
    // Adds a later input batch only after overlap and spawn-interval certification.
    void Acquire(const io::PuzzleData&, std::size_t input, const BondCertificate&,
                 core::HexFrame frame = {});
    // Calcification preserves the gripper contact while replacing the logical ID.
    // The product is placed at the consumed atom's world position, not IR origin.
    void Calcify(const io::PuzzleData&, const BondCertificate&);
    [[nodiscard]] MotionStatus CalcifyParked(std::size_t slot, const io::PuzzleData&,
                                             const BondCertificate&, std::size_t budget = 20000);
    // Released reactions place fresh singleton products at certified glyph cells.
    // All operands must have parking custody; no product is implicitly grabbed.
    void ReactReleased(const io::PuzzleData&, const ExecutionIR::Operation&,
                       const std::vector<core::HexCoord>& products, const BondCertificate&);
    // The last held reagent is released and consumed in the same instruction.
    void ReactOnRelease(std::size_t slot, const io::PuzzleData&, const ExecutionIR::Operation&,
                        const std::vector<core::HexCoord>& products, const BondCertificate&);
    // Projection consumes a parked partner and keeps the product at the held site.
    void ReactHeld(const io::PuzzleData&, const ExecutionIR::Operation&, const BondCertificate&);
    [[nodiscard]] MotionStatus ReactParked(std::size_t slot, const io::PuzzleData&,
                                           const ExecutionIR::Operation&, const BondCertificate&,
                                           std::size_t budget = 20000);
    void Duplicate(const io::PuzzleData&, ExecutionIR::AtomId catalyst, const BondCertificate&);
    void DuplicateWheel(const io::PuzzleData&, const ExecutionIR::WheelCatalyst&,
                        const BondCertificate&);
    [[nodiscard]] MotionStatus DuplicateWheelParked(std::size_t slot, const io::PuzzleData&,
                                                    const ExecutionIR::WheelCatalyst&,
                                                    const BondCertificate&,
                                                    std::size_t budget = 20000);
    [[nodiscard]] MotionStatus DuplicateParked(std::size_t slot, const io::PuzzleData&,
                                               ExecutionIR::AtomId catalyst, const BondCertificate&,
                                               std::size_t budget = 20000);
    void Discard(const io::PuzzleData&, const BondCertificate&);
    [[nodiscard]] MotionStatus DiscardParked(std::size_t slot, const io::PuzzleData&,
                                             const BondCertificate&, std::size_t budget = 20000);
    // Releases the held product and accounts for consumption in the same interval.
    void Output(const io::PuzzleData&, std::size_t output, const BondCertificate&);
    [[nodiscard]] MotionStatus OutputParked(std::size_t slot, const io::PuzzleData&,
                                            std::size_t output, const BondCertificate&,
                                            std::size_t budget = 20000);
    [[nodiscard]] MotionStatus MoveHeld(RigidPose goal, std::size_t budget = 20000);
    [[nodiscard]] MotionStatus MoveEmpty(HeldArmState goal, std::size_t budget = 20000);
    [[nodiscard]] const auto& Material() const {
        return material_;
    }
    [[nodiscard]] const auto& Custody() const {
        return custody_;
    }
    [[nodiscard]] const auto& Arm() const {
        return arm_;
    }
    [[nodiscard]] const auto& Tape() const {
        return tape_;
    }

    private:
    [[nodiscard]] MotionStatus ActivateParked(
        std::size_t slot, const std::function<void(PhysicalInventory&)>& activate,
        std::size_t budget);
    core::HexCoord Tip() const;
    ExecutionIR::State material_;
    std::vector<core::HexCoord> track_;
    HeldArmState arm_;
    int maxLength_;
    std::chrono::steady_clock::time_point deadline_{std::chrono::steady_clock::time_point::max()};
    Certificate certificate_;
    FragmentCustody custody_;
    std::vector<char> tape_;
};
} // namespace opusoptima::solvers
