#pragma once

#include "IO/BinaryParser.hpp"

#include <cstdint>
#include <set>
#include <utility>
#include <vector>

namespace opusoptima::verification {

class IndependentMetrics {
    public:
    struct Work {
        std::uint64_t rasterPoints{}, rasterCandidates{}, markedCells{}, motionFrames{};
    };
    [[nodiscard]] const Work& WorkStats() const {
        return work_;
    }
    void Initialize(const io::PuzzleData&, const io::SolutionData&);
    void ObserveAtom(core::HexCoord);
    void ObserveArm(core::HexCoord base, int length, int direction, int grabbers,
                    bool wheel = false);
    void BeginMotions();
    void ObserveAtomMotion(core::HexCoord position, core::HexCoord center,
                           core::HexCoord translation, int turns);
    void ObserveArmMotion(core::HexCoord oldBase, int oldLength, int oldDirection,
                          core::HexCoord newBase, int newLength, int newDirection, int grabbers,
                          bool wheel = false);
    void EndMotions();
    [[nodiscard]] io::SolutionMetrics Finish(int cycles) const;
    // Conclusive describes raster-cell ambiguity, not equivalence to reference motion.
    [[nodiscard]] bool Conclusive() const {
        return initialized_ && !frameOpen_ && uncertainCells_.empty();
    }
    [[nodiscard]] bool CertificationEligible() const {
        return Conclusive() && !observedMotion_;
    }
    [[nodiscard]] std::size_t VisitedCells() const {
        return area_.size();
    }

    private:
    friend struct IndependentMetricsRasterTestAccess;
    struct Sweep {
        core::HexCoord position{}, center{}, translation{};
        int turns{};
        double radius{};
    };
    void AddSweep(Sweep);
    void Mark(core::HexCoord);
    void RasterPoint(double x, double y, double radius, double uncertainty);
    Work work_{};
    std::set<std::pair<int, int>> area_, uncertainCells_;
    std::vector<Sweep> sweeps_;
    std::int64_t cost_{}, instructions_{};
    bool initialized_{}, frameOpen_{}, observedMotion_{};
};

} // namespace opusoptima::verification
