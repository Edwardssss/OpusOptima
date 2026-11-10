#include "RowPhaseProfile.hpp"
#include "ExecutionPlan.hpp"

#include <algorithm>
#include <set>
#include <cstdlib>

namespace opusoptima::solvers {
static ExecutionPlan BuildPlan(const io::PuzzleData& puzzle, ExecutionIR material,
                               std::optional<ExecutionIR::FiniteRetention> terminal) {
    row_profile::Scope profile(row_profile::Plan, material.steps.size());
    using IR = ExecutionIR;
    const auto final = material.Replay(puzzle);
    ExecutionPlan plan;
    plan.material = std::move(material);
    plan.finiteTerminal = terminal;
    std::map<IR::AtomId, std::size_t> lastUse;
    std::map<std::size_t, std::size_t> lastInput, lastOutput;
    std::map<std::size_t, std::size_t> openSlots, lastRetrieval;
    for (std::size_t index = 0; index < plan.material.steps.size(); ++index) {
        const auto& step = plan.material.steps[index];
        const auto& op = step.operation;
        ExecutionPlan::Task task;
        task.step = index;
        task.kind = op.kind;
        task.wheelCatalyst = op.wheelCatalyst;
        if (op.kind == IR::Kind::Output) {
            task.output = op.index;
            task.outputOccurrence = step.effects.outputs.at(op.index) - 1;
            if (lastOutput.contains(op.index)) task.dependsOn.push_back(lastOutput.at(op.index));
            lastOutput[op.index] = index;
        }
        if (op.kind == IR::Kind::Acquire) {
            if (lastInput.contains(op.index)) task.dependsOn.push_back(lastInput.at(op.index));
            lastInput[op.index] = index;
        }
        if (op.kind == IR::Kind::Store || op.kind == IR::Kind::Retrieve) task.buffer = op.index;
        std::set<IR::AtomId> touched(op.atoms.begin(), op.atoms.end());
        if (op.kind == IR::Kind::Relocate || op.kind == IR::Kind::Store ||
            op.kind == IR::Kind::Discard || op.kind == IR::Kind::Output ||
            op.kind == IR::Kind::Bond || op.kind == IR::Kind::Unbond ||
            op.kind == IR::Kind::Project || op.kind == IR::Kind::Calcify ||
            op.kind == IR::Kind::Duplicate)
            for (auto id : op.atoms)
                for (auto member : IR::Fragment(step.preconditions, id))
                    touched.insert(member);
        if (op.kind == IR::Kind::Retrieve) {
            const auto& ids = step.preconditions.stored.at(op.index);
            touched.insert(ids.begin(), ids.end());
            const auto it = openSlots.find(op.index);
            if (it == openSlots.end()) throw IR::Error("unpaired execution cache retrieval");
            plan.caches[it->second].retrievedAt = index;
            task.dependsOn.push_back(plan.caches[it->second].storedAt);
            openSlots.erase(it);
            lastRetrieval[op.index] = index;
        }
        task.inputs.assign(touched.begin(), touched.end());
        for (const auto id : task.inputs)
            (step.effects.atoms.contains(id) ? task.retainedInputs : task.consumedInputs)
                .push_back(id);
        for (const auto id : touched) {
            const auto previous = lastUse.find(id);
            if (previous != lastUse.end()) task.dependsOn.push_back(previous->second);
        }
        for (const auto& [edge, before] : step.preconditions.bonds) {
            const auto after = step.effects.bonds.find(edge);
            const auto now = after == step.effects.bonds.end() ? 0 : after->second;
            if (before != now || (touched.contains(edge.a) && touched.contains(edge.b)))
                task.edges.push_back({edge, static_cast<std::uint8_t>(before & now),
                                      static_cast<std::uint8_t>(before & ~now),
                                      static_cast<std::uint8_t>(now & ~before)});
        }
        for (const auto& [edge, now] : step.effects.bonds) {
            if (step.preconditions.bonds.contains(edge)) continue;
            task.edges.push_back({edge, 0, 0, now});
        }
        for (const auto& [id, atom] : step.effects.atoms) {
            if (step.preconditions.atoms.contains(id)) continue;
            task.products.push_back(id);
            ExecutionPlan::Origin origin;
            if (op.kind == IR::Kind::Acquire) {
                origin.input = op.index;
                origin.batch = step.effects.batches.at(op.index);
                origin.sourceAtom = id - step.preconditions.nextId;
                if (origin.sourceAtom >= puzzle.inputs.at(op.index).atoms.size())
                    throw IR::Error("acquisition origin out of range");
            } else
                origin.reagents = op.atoms;
            plan.origins.emplace(id, std::move(origin));
        }
        if (op.kind == IR::Kind::Store) {
            if (openSlots.contains(op.index))
                throw IR::Error("overlapping execution cache lifetimes");
            if (lastRetrieval.contains(op.index))
                task.dependsOn.push_back(lastRetrieval.at(op.index));
            openSlots.emplace(op.index, plan.caches.size());
            plan.caches.push_back({op.index, index, index, step.effects.stored.at(op.index)});
        }
        std::sort(task.dependsOn.begin(), task.dependsOn.end());
        task.dependsOn.erase(std::unique(task.dependsOn.begin(), task.dependsOn.end()),
                             task.dependsOn.end());
        for (const auto id : touched)
            lastUse[id] = index;
        for (const auto id : task.products)
            lastUse[id] = index;
        plan.tasks.push_back(std::move(task));
    }
    if (terminal) {
        IR::RequireFiniteTerminal(final, *terminal);
        // This finite contract is only the single-output six-product domain.
        if (terminal->outputs != std::vector<std::size_t>{6})
            throw IR::Unsupported("finite stop boundary requires one six-product output");
        const auto completion = std::find_if(plan.material.steps.begin(), plan.material.steps.end(),
            [&](const auto& step) {
                return step.operation.kind == IR::Kind::Output &&
                       step.effects.outputs == terminal->outputs;
            });
        if (completion == plan.material.steps.end()) throw IR::Error("finite completion Output missing");
        const auto completionStep = std::size_t(completion - plan.material.steps.begin());
        for (const auto& slot : terminal->slots) {
            const auto cache = openSlots.find(slot.slot);
            if (cache == openSlots.end()) throw IR::Error("terminal endpoint lacks Store");
            const auto store = plan.caches.at(cache->second).storedAt;
            if (!store) throw IR::Error("terminal endpoint lacks relocation");
            if (store >= completionStep)
                throw IR::Error("terminal retention Store must precede completion Output");
            const auto& relocation = plan.material.steps.at(store - 1);
            const auto& operation = relocation.operation;
            if (operation.kind != IR::Kind::Relocate ||
                operation.atoms != std::vector<IR::AtomId>{slot.atom} ||
                IR::Fragment(relocation.preconditions, slot.atom).size() != 1 ||
                relocation.effects.atoms.at(slot.atom).type != slot.type ||
                relocation.effects.atoms.at(slot.atom).position != final.atoms.at(slot.atom).position ||
                plan.material.steps.at(store).operation.atoms != std::vector<IR::AtomId>{slot.atom})
                throw IR::Error("terminal endpoint relocation/Store mismatch");
            plan.retentionEndpoints.push_back({store - 1, store, slot.slot, slot.atom, slot.type,
                                               final.atoms.at(slot.atom).position});
        }
        for (const auto& [slot, index] : openSlots)
            plan.caches[index].retrievedAt = plan.tasks.size();
    } else if (!openSlots.empty() || !final.stored.empty())
        throw IR::Error("execution cache lifetime remains open");
    return plan;
}

ExecutionPlan MakeExecutionPlan(const io::PuzzleData& puzzle, ExecutionIR material) {
    return BuildPlan(puzzle, std::move(material), {});
}
ExecutionPlan MakeFiniteExecutionPlan(const io::PuzzleData& puzzle, ExecutionIR material,
    ExecutionIR::FiniteRetention terminal, io::VerificationMode mode) {
    const auto* enabled = std::getenv("OPUSOPTIMA_FINITE_RETENTION");
    if (!enabled || std::string(enabled) != "1" || mode == io::VerificationMode::Independent)
        throw ExecutionIR::Unsupported("finite retention requires opt-in legacy-primary authority");
    if (puzzle.productionInfo || puzzle.version != 3 || puzzle.outputMultiplier != 1 ||
        puzzle.outputs.size() != 1 || puzzle.outputs[0].atoms.empty() ||
        terminal.outputs != std::vector<std::size_t>(puzzle.outputs.size(), 6))
        throw ExecutionIR::Unsupported("finite retention requires original six-product task");
    const auto* fragmentDock = std::getenv("OPUSOPTIMA_FRAGMENT_PRODUCT_DOCKING");
    const bool wholeFragment = fragmentDock && std::string(fragmentDock) == "1" &&
        terminal.slots.empty() && puzzle.inputs.size() == 1 &&
        puzzle.inputs[0].atoms.size() >= 2 && puzzle.inputs[0].atoms.size() <= 12 &&
        !puzzle.inputs[0].bonds.empty();
    for (const auto& input : puzzle.inputs) {
        if (wholeFragment) {
            for (const auto& bond : input.bonds)
                if (bond.bits != 1)
                    throw ExecutionIR::Unsupported("fragment feed requires ordinary bonds");
            continue;
        }
        if (input.atoms.size() != 1 || !input.bonds.empty())
            throw ExecutionIR::Unsupported("finite retention requires monomer feeds");
        const auto type = input.atoms[0].type;
        if (type != core::AtomType::Salt && type != core::AtomType::Air &&
            type != core::AtomType::Earth && type != core::AtomType::Fire &&
            type != core::AtomType::Water && type != core::AtomType::Quicksilver)
            throw ExecutionIR::Unsupported("finite retention feed outside registered domain");
    }
    for (const auto& bond : puzzle.outputs[0].bonds)
        if (bond.bits != 1)
            throw ExecutionIR::Unsupported("finite retention requires ordinary bonds");
    return BuildPlan(puzzle, std::move(material), std::move(terminal));
}

void ValidatePhysicalPlan(const ExecutionPlan& plan, const PhysicalPlan& physical) {
    if (physical.assignments.size() != plan.tasks.size())
        throw ExecutionIR::LayoutFailure(
            "physical plan must assign every material task exactly once");
    std::vector<const PhysicalPlan::Assignment*> assigned(plan.tasks.size());
    for (const auto& choice : physical.assignments) {
        if (choice.task >= assigned.size() || assigned[choice.task] || choice.begin < 0 ||
            choice.end < choice.begin || choice.orientation < 0 || choice.orientation >= 6)
            throw ExecutionIR::LayoutFailure("invalid or duplicated physical task assignment");
        if (choice.heldAtom &&
            !plan.material.steps[choice.task].effects.atoms.contains(*choice.heldAtom))
            throw ExecutionIR::LayoutFailure("physical holder references absent material");
        if (choice.port && plan.tasks[choice.task].output &&
            *choice.port != *plan.tasks[choice.task].output)
            throw ExecutionIR::LayoutFailure("physical output port differs from material plan");
        assigned[choice.task] = &choice;
    }
    for (std::size_t i = 0; i < assigned.size(); ++i) {
        const auto& choice = *assigned[i];
        for (const auto previous : plan.tasks[i].dependsOn)
            if (assigned[previous]->end > choice.begin)
                throw ExecutionIR::TimingFailure("physical task runs before material dependency");
        for (std::size_t j = 0; j < i; ++j) {
            const auto& other = *assigned[j];
            if (choice.arm && other.arm == choice.arm &&
                std::max(choice.begin, other.begin) < std::min(choice.end, other.end))
                throw ExecutionIR::TimingFailure("physical arm has overlapping tasks");
        }
    }
}
} // namespace opusoptima::solvers
