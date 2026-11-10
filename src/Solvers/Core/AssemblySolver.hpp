#pragma once

#include "AutomaticSupply.hpp"
#include "BaseSolver.hpp"
#include "ChemicalPlanner.hpp"
#include <optional>

namespace opusoptima::solvers {

/// Logical assembly only: target coordinates are staging reservations, NOT paths,
/// swept volumes, reachable workstations, or a claim of machine feasibility.
/// Bounded to 128 atoms and ordinary adjacent bonds, one finite nonproduction
/// output and renewable feeds with explicit source-atom mappings. Chemical
/// feasibility is checked separately; mappings allow cardinal-to-salt conversion.
struct AssemblyPlan {
    struct Edge {
        std::size_t a{}, b{};
        friend bool operator==(const Edge&, const Edge&) = default;
    };
    struct State {
        std::vector<std::size_t> atoms;
        std::vector<Edge> bonds;
        bool emitted{};
        friend bool operator==(const State&, const State&) = default;
    };
    enum class Kind { Acquire, Bond, Output };
    enum class ResourceKind { Feed, Workstation, Output };
    struct Reservation {
        ResourceKind kind{};
        std::size_t index{};
        friend bool operator==(const Reservation&, const Reservation&) = default;
    };
    struct Operation {
        Kind kind{};
        std::size_t atom{}, input{};
        Edge edge{};
        // Exact logical state required before / resulting after this operation.
        State preconditions, effects;
        // Cells occupied during the operation, including cells released by output.
        std::vector<core::HexCoord> occupied;
        // Exclusive, serial operation-local resources; no durations implied.
        std::vector<Reservation> reservations;
        friend bool operator==(const Operation&, const Operation&) = default;
    };
    std::vector<std::size_t> inputForAtom;
    std::vector<std::size_t> sourceAtomForAtom;
    std::vector<Operation> operations;
    friend bool operator==(const AssemblyPlan&, const AssemblyPlan&) = default;
};

struct SupplyPlan {
    // Paired Purify steps consume equal source metals; the second either delivers
    // the upgraded atom or retains it as inventory. Only Store/Discard may intervene.
    enum class Kind {
        Take,
        Discard,
        Reagent,
        Store,
        Purify,
        Animismus,
        Coproduct,
        Unify,
        Disperse
    };
    struct Step {
        Kind kind{};
        std::size_t input{}, sourceAtom{}, targetAtom{};
        ElementConversion conversion{ElementConversion::None};
        // Coproduct + Duplication borrows a generated catalyst: input is
        // inputs.size(), sourceAtom its AtomType. Take with buffer -2 returns
        // that same atom; neither operation acquires another input batch.
        int buffer{-1};
        // Retain a purification intermediate for subsequent reactions instead of delivery.
        bool retainProduct{};
        core::AtomType dispersedElement{core::AtomType::Air};
        struct WheelCatalyst {
            std::size_t device{};
            core::AtomType element{};
            int orientation{};
            friend bool operator==(const WheelCatalyst&, const WheelCatalyst&) = default;
        };
        std::optional<WheelCatalyst> wheelCatalyst{};
        bool calcifyBeforeDuplication{};
        friend bool operator==(const Step&, const Step&) = default;
    };
    struct Usage {
        std::size_t batches{}, acquired{}, discarded{};
        friend bool operator==(const Usage&, const Usage&) = default;
    };
    std::vector<Step> steps;
    std::vector<Usage> usage;
    bool recipeConstrained{};
    std::size_t productsPerPeriod{1};
    friend bool operator==(const SupplyPlan&, const SupplyPlan&) = default;
};

class AssemblyPlanner {
    public:
    /// Serial row-order supply schedule with source-local inventory and batch accounting.
    /// Tries integer recipe batch/conversion budgets, preserving remaining elemental demand.
    /// If row-order obstruction makes that greedy attempt fail, retries unrestricted supply.
    /// The fallback prefers direct elements, then immediate waste and useful remaining stock.
    /// Without disposal, tries waste-free periods of up to twelve products,
    /// restricted to divisors of the requested output count; partial stock spans products.
    /// Up to eight single-atom buffer slots preserve blocked stock without disposal;
    /// Buffered atoms can supply products or projection reagents and must be consumed each period.
    /// Mixed salt/cardinal feeds may lend a catalyst, duplicate salt, and return the
    /// catalyst as the last cardinal output while reserving catalysts for later products.
    [[nodiscard]] static SupplyPlan PlanSupply(const io::PuzzleData& puzzle, bool forest = false,
                                               std::size_t maxSearchStates = 20000,
                                               std::chrono::steady_clock::time_point deadline =
                                                   std::chrono::steady_clock::time_point::max());
    static constexpr std::size_t MaxAtoms = 128;
    // Bonder bit verified against ianh/omsim decode.c, commit
    // a88d12d8d100784aa525ee90259255b869bcba38, parts_available_bits_for_part_name.
    static constexpr std::uint64_t RequiredPermissions =
        (1ULL << 0) | (1ULL << 8) | (1ULL << 22) | (1ULL << 23);
    /// Deterministic connected-graph traversal; explicit cycle-closing bonds.
    /// Feeds are renewable; matching prefers unused inputs, then the first match.
    [[nodiscard]] static AssemblyPlan Plan(const io::PuzzleData& puzzle, bool forest = false,
        std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::time_point::max());
    /// Replays checked preconditions/effects/reservations and rejects edited,
    /// incomplete, reordered or foreign plans (canonical schedule required).
    static void Validate(const io::PuzzleData& puzzle, const AssemblyPlan& plan,
                         bool forest = false,
                         std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::time_point::max());
};

struct AssemblyFailure {
    enum class Category { Planning, IndependentVerifier, VerificationUnavailable,
        Unsupported, SearchExhausted, BudgetExhausted, MaterialMechanicalFailure,
        LayoutCollision, InfrastructureError };
    Category category{};
    std::string stage;
    std::string detail;
    std::optional<io::VerificationReport> verification;
    std::optional<SupplyFailure> supply;
};
using AssemblyFeedback = std::function<void(const AssemblyFailure&)>;

/// Executable compiler supports bounded Repeat targets by building six connected
/// monomers and halting that machine after delivery to a nonconsuming out-rep.
/// This satisfies standard completion, not infinite-throughput certification;
/// expansion remains subject to ordinary assembly chemistry and geometry limits.
/// Executable compiler retains specialized pair/bend/chain workstations, then
/// falls back to serialized row assembly for ordinary-bond connected targets.
/// Row assembly requires piston/track permissions and supports multiple renewable
/// feeds; it supports irregular chains, branches and cycle-closing bonds.
/// Multi-atom inputs must rotate into at most three rows. Shared pickup columns
/// are supported by discarding blocking atoms before lower pickups.
/// Static unbonding requires unbonder permission; inventory cleanup
/// requires disposal permission. A deterministic supply plan chooses among sources
/// using immediate waste and remaining demand, accounts for batches and discarded
/// atoms, and closes each repeated period by draining partial batches or consuming
/// them across multiple products without disposal.
/// Selective calcification converts cardinal elements to salt when permitted.
/// It does not perform general disassembly, other chemistry or parallel scheduling.
/// Compilation is candidate generation, not a feasibility guarantee: publication
/// requires independent simulation and measured-metric revalidation.
/// For finite single outputs whose atom types have ordinary supply mappings,
/// ordinary assembly precedes chained chemistry backends. Failed compilation or
/// rejected acceptance still falls through, without retrying the same compiler.
class AssemblySolver final : public BaseSolver {
    public:
    AssemblySolver() : BaseSolver("AssemblySolver") {}
    [[nodiscard]] io::SolutionData Solve(const io::PuzzleData& puzzle) override;
    [[nodiscard]] io::SolutionData SolveAccepted(const io::PuzzleData& puzzle,
                                                 const CandidateAcceptance& accept) override;
    [[nodiscard]] io::SolutionData SolveVerified(const io::PuzzleData& puzzle,
                                                 const CandidateVerification& verify) override;
    [[nodiscard]] io::SolutionData SolveVerified(const io::PuzzleData& puzzle,
                                                 const CandidateVerification& verify,
                                                 const AssemblyFeedback& feedback);
    using CandidateSink = std::function<io::VerificationReport(const io::SolutionData&, const std::string&)>;
    static constexpr std::size_t MaxCandidateSubmissions = 12;
    static constexpr std::size_t MaxPortfolioConstructions = 3;
    // Baseline first, then at most three ordinary finite-output construction attempts.
    // All families share these limits and the caller's immutable accepted cap.
    // Other output modes stop their fallback chain at the first verified solution.
    [[nodiscard]] io::SolutionData CollectVerified(const io::PuzzleData& puzzle,
                                                   const CandidateSink& sink,
                                                   std::size_t maxAccepted = 3);
    void EnableExperimentalSharedBatchConsumer(bool enabled) { sharedBatchConsumerEnabled_ = enabled; }
    void EnableExperimentalSharedNearCache(bool enabled) { sharedNearCacheEnabled_ = enabled; }
    void EnableExperimentalSharedBufferRelay(bool enabled) { sharedBufferRelayEnabled_ = enabled; }
    void EnableExperimentalSharedScheduling(bool enabled) { sharedSchedulingEnabled_ = enabled; }
    void EnableExperimentalSharedAssembly(bool enabled) { sharedAssemblyEnabled_ = enabled; }
    void EnableExperimentalCompactRelay(bool enabled) { compactRelayEnabled_ = enabled; }
    [[nodiscard]] std::int32_t EstimateCycles(const io::PuzzleData& puzzle) override;
    [[nodiscard]] static bool Supports(const io::PuzzleData& puzzle) noexcept;
    [[nodiscard]] static io::SolutionData Compile(const io::PuzzleData& puzzle,
                                                  const AssemblyPlan& plan);

    private:
    bool compactRelayEnabled_{false};
    bool sharedAssemblyEnabled_{false};
    bool sharedSchedulingEnabled_{false};
    bool sharedBufferRelayEnabled_{false};
    bool sharedBatchConsumerEnabled_{false};
    bool sharedNearCacheEnabled_{false};
    io::SolutionData SolveCandidates(const io::PuzzleData& puzzle,
                                     const CandidateAcceptance& accept,
                                     const CandidateVerification* verify,
                                     const AssemblyFeedback& feedback = {},
                                     const CandidateSink* sink = nullptr,
                                     std::size_t maxAccepted = 1);
};

} // namespace opusoptima::solvers
