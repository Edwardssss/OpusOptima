#pragma once

#include "ModuleHandoff.hpp"
#include <optional>

namespace opusoptima::solvers {

// A single-exit scheduling contract, not a material closure or swept-path proof.
class FiniteFeedResource {
public:
    struct Slice {
        int begin{}, end{};
        friend bool operator==(const Slice&, const Slice&) = default;
    };

    FiniteFeedResource(ModulePort outlet, const std::vector<int>& productEnds, int duration,
                       std::optional<std::size_t> expectedDeliveries = std::nullopt)
        : outlet_(outlet), expectedDeliveries_(expectedDeliveries) {
        if (duration <= 0 || outlet.orientation < 0 || outlet.orientation >= 6)
            throw std::invalid_argument("invalid finite feed resource");
        int previous = 0;
        for (int end : productEnds) {
            if (end <= previous || end > duration)
                throw std::invalid_argument("invalid finite feed product boundary");
            slices_.push_back({previous, end});
            previous = end;
        }
        if (slices_.empty()) slices_.push_back({0, duration});
        else slices_.back().end = duration;
        if (expectedDeliveries_ && (*expectedDeliveries_ == 0 || *expectedDeliveries_ % slices_.size() != 0))
            throw std::invalid_argument("finite feed demand does not close its batch");
    }

    Slice Reserve(int start) {
        if (pending_ || start < 0 || (last_ && start < last_->cleared) ||
            (expectedDeliveries_ && delivered_ == *expectedDeliveries_))
            throw std::invalid_argument("finite feed exit is occupied");
        pending_ = start;
        return slices_[next_];
    }

    void Release(ModulePort consumed, int pickup, int cleared) {
        if (!pending_) throw std::invalid_argument("finite feed has no reserved slice");
        const auto slice = slices_[next_];
        if (static_cast<long long>(pickup) < static_cast<long long>(*pending_) + slice.end - slice.begin)
            throw std::invalid_argument("finite feed slice has not finished");
        // Occupy the exit conservatively from the start of production until the
        // receiver actually moves away, including all cleanup in the last slice.
        ModuleHandoff handoff{outlet_, consumed, *pending_, pickup, cleared, cleared};
        ValidateModuleHandoffs({handoff});
        last_ = handoff;
        pending_.reset();
        next_ = (next_ + 1) % slices_.size();
        if (expectedDeliveries_) ++delivered_;
    }

    bool Complete() const {
        return !pending_ && next_ == 0 && (!expectedDeliveries_ || delivered_ == *expectedDeliveries_);
    }
    const ModulePort& Outlet() const { return outlet_; }

private:
    ModulePort outlet_;
    std::vector<Slice> slices_;
    std::size_t next_{};
    std::optional<int> pending_;
    std::optional<ModuleHandoff> last_;
    std::optional<std::size_t> expectedDeliveries_;
    std::size_t delivered_{};
};

} // namespace opusoptima::solvers
