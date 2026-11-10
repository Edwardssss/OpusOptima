#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace opusoptima::solvers {

// Port of the upstream `ProgramBuilder` scheduling contract (C3).
//
// Every emitter in this project writes absolute tapes directly: while a row is
// being built the local `time` counter is advanced by both the arm that works
// and the arm that waits, so two independent arms never overlap. Upstream does
// the opposite: each component writes a *relative* fragment (ticks measured
// from the start of that fragment) and `ProgramBuilder` merges the fragments
// afterwards, shifting a fragment only as far as its **own arm** requires.
//
// That single rule is what makes "free parallelism" possible: the upper arm of
// row N may still be returning while the feeder has already staged the atom of
// row N+1, because the two arms are different arms. Two fragments of the same
// arm are the only ones that constrain each other.
//
// The builder is deliberately geometry-free: it does not know about cells,
// collisions or walls. It only guarantees that, per arm, no two instructions
// ever share a tick. Collision freedom stays the emitter's responsibility, which
// matches upstream where `ProgramWriter` still lays out the geometry.
struct ProgramInstruction {
    char action{};
    std::int64_t tick{};  // relative to the fragment start on first insertion
};

struct ProgramFragment {
    std::string arm;                              // stable arm identity
    std::vector<ProgramInstruction> instructions;  // relative ticks, any order
    // Optional absolute lower bound for the fragment's first tick, used for
    // hand-off clearance ("this fragment may not start before tick N").
    std::int64_t anchor{};
};

struct PlacedInstruction {
    char action{};
    std::int64_t tick{};
};

class ProgramBuilder {
public:
    // Places the fragment at the smallest tick that satisfies its anchor and
    // keeps every instruction of that arm on a unique tick. Returns the shift
    // that was applied (the fragment's absolute start tick).
    std::int64_t Add(ProgramFragment fragment);

    // Merged tape of one arm, sorted by tick.
    const std::vector<PlacedInstruction>& Arm(const std::string& arm) const;

    // Number of ticks the merged program spans (0 when nothing was added).
    std::int64_t Makespan() const;

    // Ticks that at least one arm occupies; useful for hand-off clearance.
    const std::vector<std::int64_t>& Occupied() const { return occupied_; }

    // True when the arm already owns this tick.
    bool OccupiedBy(const std::string& arm, std::int64_t tick) const;

    std::size_t FragmentCount() const { return fragmentCount_; }

private:
    std::map<std::string, std::vector<PlacedInstruction>> arms_;
    std::vector<std::int64_t> occupied_;
    std::int64_t makespan_{};
    std::size_t fragmentCount_{};
};

}  // namespace opusoptima::solvers
