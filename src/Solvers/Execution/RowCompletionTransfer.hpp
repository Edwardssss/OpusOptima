#pragma once
#include "ExecutionIR.hpp"
#include "SolutionVerifier.hpp"
#include <functional>

namespace opusoptima::solvers {
struct RowCompletionTransfer {
    struct Holder {
        int object{}, length{}, trackOffset{}, rotation{};
        bool grabbing{};
        ExecutionIR::AtomId atom{};
        core::HexCoord base{}, tip{};
    };
    struct Boundary {
        int tapeBoundary{};
        ExecutionIR::State material;
        std::vector<Holder> holders;
    };
    struct Event {
        int tapeBoundary{};
        std::string kind;
        ExecutionIR::AtomId atom{}, other{};
        core::HexCoord position{};
    };
    // Explicit experiment only; diagnostics intentionally survive a rejected compilation.
    struct Candidate {
        core::HexCoord base{}, tip{}, destination{};
        int rotation{}, distance{};
        std::string rejection;
    };
    // Native-only opt-in. The gate must use the supplied unchanged outer deadline.
    bool coproductAssembly{};
    // Explicit same-world finite surplus experiment; never enabled by default.
    bool surplusRetention{};
    // Bounded typed cardinal + unchanged Quicksilver dispatch; independent opt-in.
    bool secondSingletonSource{};
    bool wheelSecondSingletonSource{}; // One static cardinal wheel inferred from the target.
    ExecutionIR::FiniteRetention retention;
    std::map<ExecutionIR::AtomId, core::HexCoord> retentionTargets;
    io::VerificationMode mode{io::VerificationMode::Independent};
    std::function<io::VerificationReport(const io::SolutionData&,
                                         std::chrono::steady_clock::time_point)>
        nativeGate;
    std::function<bool(const io::SolutionData&)> accept;
    io::VerificationReport verification;
    bool published{};
    std::vector<Candidate> candidates;
    io::SolutionData partialMachine;
    std::vector<Event> events;
    std::vector<Boundary> reactionInputs;
    // Committed world state immediately after real glyph conversion and custody rebinding.
    std::vector<Boundary> reactions;
    std::vector<Boundary> boundaries;
    ExecutionIR ledger;
    std::string failure;
};
} // namespace opusoptima::solvers
