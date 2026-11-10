#pragma once

#include "BinaryParser.hpp"
#include <map>
#include <optional>
#include <stdexcept>

namespace opusoptima::solvers {

/// Material-only execution IR, NOT a machine layout, motion plan, timing model or
/// physical certification. Coordinates are abstract assembly-frame coordinates;
/// distinct fragments may overlap. Relocate rigidly changes one entire fragment's
/// frame, without claiming a collision-free path. Connectivity defines fragments.
///
/// Start with Initial(puzzle); Append records exact pre/post snapshots atomically.
/// Replay starts from Initial and recomputes every effect, rejecting changed
/// operations or snapshots. Call RequireComplete with explicit per-output counts;
/// it rejects ALL remaining stock (including catalysts and stored coproducts).
/// Replay alone permits intermediate stock and does not imply puzzle completion.
///
/// IDs are monotonically allocated, never reused. Acquire creates the ENTIRE
/// original input with original bonds and shape, transformed by its explicit
/// port-local to world rotation/translation (identity by default). Chemistry requires distinct,
/// unstored operands and allocates fresh product IDs; Duplicate retains its
/// second (catalyst) operand. Project also accepts a bonded metal: its fresh ID
/// replaces only that endpoint, preserving its position and every incident bond.
/// Calcify and Duplicate likewise preserve a bonded substrate endpoint. A typed
/// wheel catalyst constrains a static device pose, never a consumable atom.
/// Projection's consumed quicksilver and the other consuming reactions require singletons.
/// Calcify: cardinal -> salt; Project: metal, quicksilver -> next metal;
/// Purify: equal non-gold metals -> next metal; Animismus: salt,salt -> vitae,mors;
/// Unify: air,earth,fire,water (any order) -> quintessence;
/// Disperse: quintessence -> air,earth,fire,water; Duplicate: salt,cardinal -> cardinal.
/// New IDs can be read from effects.nextId (or calculated from preconditions.nextId).
///
/// Bond/Unbond change exactly the supplied mask (ordinary OR color bits, not both).
/// Colored bond creation requires fire endpoints; inherited input/output bond
/// labels are preserved regardless of endpoint types. No implicit disassembly or disposal.
/// Store/Retrieve reserve/release one whole connected fragment in an exclusive
/// logical slot; no physical buffer/arm capacity is claimed. Output and Discard
/// operate on the whole referenced fragment, never silently detach extra atoms.
/// Output matches every atom, coordinate and bond label up to six rotations and
/// translation, NOT reflection. Repeat atoms and disconnected outputs remain
/// unsupported. Wheel references only describe read-only device catalysts, not
/// extracted wheel atoms or device motion. Initial rejects production; ForStaticProduction opts
/// into its molecule material rules only. Room assignments, conduit custody, arm motion and
/// vial/shrink behavior are not inferred by this IR. SupplyExecutionIR provides a separate adapter
/// choosing logical fragment frames and accounting for complete batches, coproducts and catalysts.
class ExecutionIR {
    public:
    using AtomId = std::uint64_t;
    struct Error : std::runtime_error {
        using std::runtime_error::runtime_error;
    };
    struct Unsupported : Error {
        using Error::Error;
    };
    struct PermissionFailure : Error {
        int bit;
        PermissionFailure(int permissionBit, const std::string& detail)
            : Error(detail), bit(permissionBit) {}
    };
    struct LayoutFailure : Unsupported {
        using Unsupported::Unsupported;
    };
    struct TimingFailure : Unsupported {
        using Unsupported::Unsupported;
    };
    struct BudgetFailure : Unsupported {
        using Unsupported::Unsupported;
    };
    struct PhysicalFailure : Unsupported {
        std::size_t step{};
        AtomId atom{};
        core::HexCoord destination{};
        bool exhausted{};
        PhysicalFailure(const std::string& detail, std::size_t operation, AtomId materialAtom,
                        core::HexCoord cell, bool budgetExhausted)
            : Unsupported(detail), step(operation), atom(materialAtom), destination(cell),
              exhausted(budgetExhausted) {}
    };
    struct Atom {
        core::AtomType type{};
        core::HexCoord position{};
        friend bool operator==(const Atom&, const Atom&) = default;
    };
    struct Edge {
        AtomId a{}, b{};
        friend auto operator<=>(const Edge&, const Edge&) = default;
    };
    struct WheelCatalyst {
        std::size_t device{};
        core::AtomType element{};
        int orientation{};
        friend bool operator==(const WheelCatalyst&, const WheelCatalyst&) = default;
    };
    struct State {
        // Static device pose constraints, not acquired or consumable atoms.
        std::map<std::size_t, int> wheelDevices;
        std::map<AtomId, Atom> atoms;
        std::map<Edge, std::uint8_t> bonds;
        std::map<std::size_t, std::vector<AtomId>> stored;
        std::vector<std::size_t> batches, outputs;
        std::size_t discarded{};
        AtomId nextId{1};
        friend bool operator==(const State&, const State&) = default;
    };
    enum class Kind {
        Acquire,
        Relocate,
        Bond,
        Unbond,
        Calcify,
        Project,
        Purify,
        Animismus,
        Unify,
        Disperse,
        Duplicate,
        Discard,
        Output,
        Store,
        Retrieve
    };
    struct Operation {
        Kind kind{};
        // Acquire: input index; Output: output index; Store/Retrieve: slot.
        std::size_t index{};
        // Acquire/Retrieve: empty; unary operations: one fragment representative;
        // Bond/Unbond and chemistry: ordered operand IDs as documented above.
        std::vector<AtomId> atoms;
        std::uint8_t bits{}; // Bond/Unbond only.
        int rotation{};      // Acquire/Relocate only, 0..5 clockwise turns about frame origin.
        core::HexCoord translation{}; // Acquire/Relocate only, applied after rotation.
        std::optional<WheelCatalyst>
            wheelCatalyst{}; // Duplicate: one Salt substrate, no material catalyst.
        friend bool operator==(const Operation&, const Operation&) = default;
    };
    struct Step {
        Operation operation;
        State preconditions, effects;
        friend bool operator==(const Step&, const Step&) = default;
    };
    std::vector<Step> steps;

    [[nodiscard]] static State Initial(const io::PuzzleData& puzzle);
    // Explicitly opt into static production material replay. This does not add
    // room, conduit or motion semantics; a production coordinator must supply them.
    [[nodiscard]] static State InitialForStaticProduction(const io::PuzzleData& puzzle);
    [[nodiscard]] static ExecutionIR ForStaticProduction(const io::PuzzleData& puzzle);
    [[nodiscard]] static std::vector<AtomId> Fragment(const State& state, AtomId atom);
    void Append(const io::PuzzleData& puzzle, State& state, const Operation& operation);
    // Incremental construction checks the initial/tail snapshot and recomputes
    // the transition atomically. The noninitial tail is compared in place before
    // appending, so no borrowed tail survives vector growth. The finished program MUST still Replay before
    // consumption; this does not certify earlier caller-editable steps.
    void AppendFromTail(const io::PuzzleData& puzzle, State& state, const Operation& operation);
    [[nodiscard]] State Replay(const io::PuzzleData& puzzle) const;
    // Synchronous read-only projection: replay every historical op/pre/effect,
    // check the caller's tail, then apply Output to a private owning State.
    // No history reference escapes and no Step vector is copied or extended.
    [[nodiscard]] State ProjectOutput(const io::PuzzleData& puzzle, const State& tail,
                                      const Operation& output) const;
    struct FiniteRetention {
        struct Slot { std::size_t slot; AtomId atom; core::AtomType type; };
        std::vector<std::size_t> outputs;
        std::vector<Slot> slots;
        std::size_t capacity{};
    };
    static void RequireFiniteTerminal(const State&, const FiniteRetention&);
    static void RequireComplete(const State& state, const std::vector<std::size_t>& outputCounts);

    private:
    bool staticProduction_{};
    friend class PhysicalInventory;
    static void Apply(const io::PuzzleData&, State&, const Operation&);
};

} // namespace opusoptima::solvers
