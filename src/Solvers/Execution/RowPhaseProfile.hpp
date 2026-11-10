#pragma once
#include <array>
#include <chrono>
#include <cstdint>

namespace opusoptima::solvers::row_profile {
enum Phase { Append, ApplyLive, ApplyReplay, Replay, Custody, ProofCopy, Projection, Plan, Observer, Gate, Count };
struct Entry { std::uint64_t calls{}, units{}, nanoseconds{}; };
struct Counters { std::array<Entry, Count> phases{}; std::array<std::array<Entry, Count>, 6> checkpoints{}; std::size_t boundaries{}; };
inline thread_local Counters* active = nullptr;
class Scope {
    using Clock = std::chrono::steady_clock;
    inline static thread_local Scope* parent_ = nullptr;
    Counters* sink_;
    Scope* previous_{};
    Phase phase_;
    Clock::time_point start_{};
    std::uint64_t children_{};
public:
    explicit Scope(Phase phase, std::uint64_t units = 0) : sink_(active), phase_(phase) {
        if (!sink_) return;
        previous_ = parent_;
        parent_ = this;
        start_ = Clock::now();
        ++sink_->phases[phase].calls;
        sink_->phases[phase].units += units;
    }
    ~Scope() {
        if (!sink_) return;
        const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - start_).count();
        sink_->phases[phase_].nanoseconds += elapsed - children_;
        if (previous_) previous_->children_ += elapsed;
        parent_ = previous_;
    }
    static bool Within(Phase phase) {
        if (!active) return false;
        for (auto* scope = parent_; scope; scope = scope->previous_)
            if (scope->phase_ == phase) return true;
        return false;
    }
    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;
};
} // namespace opusoptima::solvers::row_profile
