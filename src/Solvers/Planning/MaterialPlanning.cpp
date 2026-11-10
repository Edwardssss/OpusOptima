#include "MaterialPlanning.hpp"
#include "SupplyExecutionIR.hpp"

#include <algorithm>

namespace opusoptima::solvers {
bool SupportsClosedSingletonWheelPlanning(const io::PuzzleData& puzzle) {
    using T = core::AtomType;
    constexpr auto permissions = (1ULL << 0) | (1ULL << 8) | (1ULL << 9) |
        (1ULL << 11) | (1ULL << 12) | (1ULL << 13) | (1ULL << 22) |
        (1ULL << 23) | (1ULL << 25) | (1ULL << 28);
    if (puzzle.version != 3 || puzzle.productionInfo || puzzle.outputMultiplier < 1 ||
        puzzle.outputMultiplier > 8 || puzzle.inputs.empty() || puzzle.inputs.size() > 4 ||
        puzzle.outputs.size() != 1 || (puzzle.permissionBits & permissions) != permissions)
        return false;
    const auto ordinary = [](T type) {
        return type == T::Salt || type == T::Air || type == T::Earth || type == T::Water;
    };
    bool calcifiableSource = false;
    for (const auto& input : puzzle.inputs) {
        if (input.atoms.empty() || input.atoms.size() > 4 || input.bonds.size() > 6 ||
            std::any_of(input.atoms.begin(), input.atoms.end(),
                        [&](const auto& atom) { return !ordinary(atom.type); }) ||
            std::any_of(input.bonds.begin(), input.bonds.end(),
                        [](const auto& bond) { return bond.bits != 1; }))
            return false;
        calcifiableSource |= std::any_of(input.atoms.begin(), input.atoms.end(),
            [](const auto& atom) { return atom.type != T::Salt; });
    }
    const auto& output = puzzle.outputs.front();
    if (!calcifiableSource || output.atoms.size() < 2 || output.atoms.size() > 4 ||
        output.bonds.size() > 6 ||
        std::count_if(output.atoms.begin(), output.atoms.end(),
                      [](const auto& atom) { return atom.type == T::Fire; }) != 2 ||
        std::any_of(output.atoms.begin(), output.atoms.end(),
                    [&](const auto& atom) { return atom.type != T::Fire && !ordinary(atom.type); }) ||
        std::count_if(output.bonds.begin(), output.bonds.end(),
                      [](const auto& bond) { return bond.bits != 1; }) != 1)
        return false;
    for (const auto& bond : output.bonds) {
        if (bond.bits == 1) continue;
        if (!bond.bits || (bond.bits & ~14U)) return false;
        for (const auto endpoint : {bond.from, bond.to})
            if (std::none_of(output.atoms.begin(), output.atoms.end(), [&](const auto& atom) {
                    return atom.position == endpoint && atom.type == T::Fire;
                })) return false;
    }
    try {
        (void)ExecutionIR::Initial(puzzle);
    } catch (const ExecutionIR::Error&) {
        return false;
    }
    return true;
}
namespace {
ExecutionIR LowerEndpointPrecursor(const io::PuzzleData& original, const io::PuzzleData& precursor,
                                   const ExecutionIR& source) {
    using IR = ExecutionIR;
    using K = IR::Kind;
    IR program;
    auto state = IR::Initial(original);
    std::map<IR::AtomId, IR::AtomId> aliases;
    const auto resolve = [&](IR::AtomId id) {
        const auto found = aliases.find(id);
        if (found == aliases.end()) throw IR::Error("precursor atom has no material identity");
        return found->second;
    };
    for (const auto& step : source.steps) {
        auto operation = step.operation;
        if (operation.kind == K::Output) {
            const auto port = operation.index;
            const auto product = IR::Fragment(step.preconditions, operation.atoms.at(0));
            for (std::size_t atom = 0; atom < original.outputs[port].atoms.size(); ++atom) {
                const auto wanted = original.outputs[port].atoms[atom].type;
                const auto assembly = precursor.outputs[port].atoms[atom].type;
                if (assembly == wanted) continue;
                if (assembly != core::AtomType::Fire)
                    throw IR::Unsupported("unsupported assembly precursor endpoint");
                const auto local = precursor.outputs[port].atoms[atom].position;
                const auto endpoint = std::find_if(product.begin(), product.end(), [&](auto id) {
                    return step.preconditions.atoms.at(id).position == local;
                });
                if (endpoint == product.end())
                    throw IR::Error("missing precursor endpoint identity");
                auto current = resolve(*endpoint);
                auto fresh = state.nextId;
                program.AppendFromTail(original, state, {K::Calcify, 0, {current}});
                current = fresh;
                if (wanted != core::AtomType::Salt) {
                    fresh = state.nextId;
                    IR::Operation duplicate{K::Duplicate, 0, {current}};
                    duplicate.wheelCatalyst = IR::WheelCatalyst{0, wanted, 3};
                    program.AppendFromTail(original, state, duplicate);
                    current = fresh;
                }
                aliases[*endpoint] = current;
            }
        }
        for (auto& id : operation.atoms)
            id = resolve(id);
        const auto fresh = state.nextId;
        program.AppendFromTail(original, state, operation);
        const auto created = step.effects.nextId - step.preconditions.nextId;
        if (state.nextId - fresh != created)
            throw IR::Error("precursor reaction changed product arity");
        for (IR::AtomId offset = 0; offset < created; ++offset)
            aliases[step.preconditions.nextId + offset] = fresh + offset;
    }
    const auto expected = source.Replay(precursor).outputs;
    IR::RequireComplete(program.Replay(original), expected);
    return program;
}

MaterialPlanningResult SearchMaterialExecutionBase(
    const io::PuzzleData& puzzle, MaterialPlanningLimits bounds,
    const std::function<bool(const ExecutionPlan&)>& accept,
    SupplyMaterialLoweringPolicy policy, const io::PuzzleData* original = nullptr) {
    MaterialPlanningResult result;
    try {
        (void)ExecutionIR::Initial(puzzle);
    } catch (const ExecutionIR::Error& error) {
        result.status = MaterialPlanningStatus::Unsupported;
        result.detail = error.what();
        return result;
    }
    if (puzzle.version != 3 || puzzle.outputMultiplier <= 0 || puzzle.inputs.empty() ||
        puzzle.outputs.empty()) {
        result.status = MaterialPlanningStatus::Unsupported;
        result.detail = "material planning requires nonempty finite version-3 supply";
        return result;
    }
    std::vector<std::size_t> periods;
    const auto periodDivisor = ClosedMaterialPeriodDivisor(puzzle);
    for (std::size_t period = 1; period <= 12; ++period)
        if ((6ULL * puzzle.outputMultiplier) % period == 0 &&
            (!periodDivisor || period % *periodDivisor == 0))
            periods.push_back(period);
    bool exhausted = false;
    std::string replayError;
    for (std::size_t index = 0; index < periods.size(); ++index) {
        if (std::chrono::steady_clock::now() >= bounds.deadline ||
            result.states >= bounds.maxStates || result.candidates >= bounds.maxCandidates) {
            exhausted = true;
            break;
        }
        const auto period = periods[index];
        result.periods.push_back(period);
        InventorySearchLimits limits;
        limits.maxStates =
            std::max<std::size_t>(1, (bounds.maxStates - result.states) / (periods.size() - index));
        limits.deadline = bounds.deadline;
        limits.maxSteps = bounds.maxSteps;
        limits.maxSourceAtoms = bounds.maxSourceAtoms;
        limits.multiStagePurification = true;
        limits.mechanicalExtractionOrder = false;
        // Without source Fire or Quintessence, nonwheel reactions cannot seed Fire.
        limits.wheelCatalysts = !original && SupportsClosedSingletonWheelPlanning(puzzle);
        auto tryPlan = [&](const JointSupplyPlan& supply) {
            if (result.candidates >= bounds.maxCandidates ||
                std::chrono::steady_clock::now() >= bounds.deadline) return false;
            ++result.candidates;
            std::optional<ExecutionPlan> candidate;
            try {
                auto material = BuildJointSupplyExecutionIRWithPolicy(
                    puzzle, supply, policy);
                if (original) material = LowerEndpointPrecursor(*original, puzzle, material);
                if (policy != SupplyMaterialLoweringPolicy::PreserveCompatible &&
                    material.steps.size() > bounds.maxSteps)
                    throw ExecutionIR::BudgetFailure("lowered material operation limit exceeded");
                candidate.emplace(
                    MakeExecutionPlan(original ? *original : puzzle, std::move(material)));
            } catch (const ExecutionIR::Error& error) {
                replayError = error.what();
                return false;
            }
            if (accept && !accept(*candidate)) return false;
            result.plan = std::move(candidate);
            return true;
        };
        if (original && limits.maxStates > 3) {
            std::optional<ChemicalRecipe> recipe;
            try {
                recipe = ChemicalPlanner::Plan(puzzle, period,
                                               (puzzle.permissionBits & (1ULL << 17)) != 0);
            } catch (const std::invalid_argument&) {
            }
            if (recipe) {
                auto constrainedLimits = limits;
                constrainedLimits.maxStates = limits.maxStates / 2;
                const auto constrained =
                    SearchJointSupplyInventoryWithPolicy(puzzle, period, policy, constrainedLimits, &*recipe);
                result.states += constrained.states;
                limits.maxStates -= std::min(limits.maxStates, constrained.states);
                exhausted |= constrained.status == InventorySearchStatus::Exhausted;
                if (constrained.status == InventorySearchStatus::Found &&
                    tryPlan(constrained.plan)) {
                    result.status = MaterialPlanningStatus::Found;
                    result.detail = "closed recipe-constrained material period; mechanical "
                                    "execution not established";
                    return result;
                }
            }
        }
        const auto first = SearchJointSupplyInventoryWithPolicy(puzzle, period, policy, limits);
        result.states += first.states;
        exhausted |= first.status == InventorySearchStatus::Exhausted;
        if (first.status == InventorySearchStatus::Found && tryPlan(first.plan)) {
            result.status = MaterialPlanningStatus::Found;
            result.detail = "closed material period; mechanical execution not established";
            return result;
        }
        // Rejected histories need distinct mapping search; keep its state cost
        // within the same period allowance instead of restarting a fresh budget.
        if (first.status == InventorySearchStatus::Found && first.states < limits.maxStates &&
            result.candidates < bounds.maxCandidates) {
            limits.maxStates -= first.states;
            const auto alternatives = SearchJointSupplyInventoryWithPolicy(
                puzzle, period, policy, limits, nullptr, [&](const auto& candidate) {
                    return candidate != first.plan && tryPlan(candidate);
                });
            result.states += alternatives.states;
            exhausted |= alternatives.status == InventorySearchStatus::Exhausted;
            if (result.plan) {
                result.status = MaterialPlanningStatus::Found;
                result.detail =
                    "closed alternative material period; mechanical execution not established";
                return result;
            }
        }
    }
    exhausted |= result.states >= bounds.maxStates || result.candidates >= bounds.maxCandidates;
    result.status =
        exhausted ? MaterialPlanningStatus::Exhausted : MaterialPlanningStatus::NoCandidate;
    result.detail = exhausted
                        ? "material state/candidate budget exhausted; not an impossibility proof"
                        : "no replayable candidate in supported reactions and periods; not an "
                          "impossibility proof";
    if (std::chrono::steady_clock::now() >= bounds.deadline) {
        result.status = MaterialPlanningStatus::BudgetExhausted;
        result.detail = "material/physical shared deadline exhausted; not an impossibility proof";
    }
    if (!replayError.empty()) result.detail += "; last material replay rejection: " + replayError;
    return result;
}
} // namespace

MaterialPlanningResult SearchMaterialExecution(
    const io::PuzzleData& puzzle, MaterialPlanningLimits bounds,
    const std::function<bool(const ExecutionPlan&)>& accept) {
    return SearchMaterialExecutionWithPolicy(
        puzzle, SupplyMaterialLoweringPolicy::PreserveCompatible, bounds, accept);
}

MaterialPlanningResult SearchMaterialExecutionWithPolicy(
    const io::PuzzleData& puzzle, SupplyMaterialLoweringPolicy policy, MaterialPlanningLimits bounds,
    const std::function<bool(const ExecutionPlan&)>& accept) {
    if (policy == SupplyMaterialLoweringPolicy::ClosedCardinalLife &&
        !SupportsClosedCardinalLife(puzzle)) {
        MaterialPlanningResult result;
        result.status = MaterialPlanningStatus::Unsupported;
        result.detail = "closed cardinal life domain required";
        return result;
    }
    auto precursor = puzzle;
    bool changed = false;
    const bool fireFeed =
        std::any_of(puzzle.inputs.begin(), puzzle.inputs.end(), [](const auto& molecule) {
            return std::any_of(molecule.atoms.begin(), molecule.atoms.end(),
                               [](const auto& atom) { return atom.type == core::AtomType::Fire; });
        });
    if (fireFeed && (puzzle.permissionBits & (1ULL << 11)) &&
        (puzzle.permissionBits & (1ULL << 12)))
        for (auto& output : precursor.outputs)
            for (auto& atom : output.atoms) {
                if (atom.type == core::AtomType::Fire || atom.type < core::AtomType::Salt ||
                    atom.type > core::AtomType::Water)
                    continue;
                const bool colored =
                    std::any_of(output.bonds.begin(), output.bonds.end(), [&](const auto& bond) {
                        return bond.bits != 1 &&
                               (bond.from == atom.position || bond.to == atom.position);
                    });
                const bool canDuplicate = (puzzle.permissionBits & (1ULL << 13)) &&
                                          (puzzle.permissionBits & (1ULL << 28));
                if (colored && (atom.type == core::AtomType::Salt || canDuplicate)) {
                    atom.type = core::AtomType::Fire;
                    changed = true;
                }
            }
    if (!changed) return SearchMaterialExecutionBase(puzzle, bounds, accept, policy);
    MaterialPlanningLimits first = bounds;
    first.maxStates = bounds.maxStates * 3 / 4;
    auto result = SearchMaterialExecutionBase(precursor, first, accept, policy, &puzzle);
    if (result.status == MaterialPlanningStatus::Found) {
        result.detail = "closed Fire-precursor and bonded endpoint-conversion period; mechanical "
                        "execution not established";
        return result;
    }
    MaterialPlanningLimits remaining = bounds;
    remaining.maxStates -= std::min(remaining.maxStates, result.states);
    remaining.maxCandidates -= std::min(remaining.maxCandidates, result.candidates);
    auto direct = SearchMaterialExecutionBase(puzzle, remaining, accept, policy);
    direct.states += result.states;
    direct.candidates += result.candidates;
    direct.periods.insert(direct.periods.begin(), result.periods.begin(), result.periods.end());
    if (direct.status != MaterialPlanningStatus::Found &&
        direct.status != MaterialPlanningStatus::BudgetExhausted &&
        result.status == MaterialPlanningStatus::Exhausted)
        direct.status = MaterialPlanningStatus::Exhausted;
    return direct;
}
} // namespace opusoptima::solvers
