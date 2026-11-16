#include "InventoryIR.hpp"
#include <algorithm>
#include <array>
#include <limits>
#include <map>
#include <optional>

namespace opusoptima::solvers {
PhysicalInventory LowerInventoryIR(
    const io::PuzzleData& puzzle, const ExecutionIR& program,
    const std::vector<core::HexCoord>& track, HeldArmState start,
    PhysicalInventory::Certificate motion, PhysicalInventory::BondCertificate glyph, int maxLength,
    std::size_t budget, const std::vector<std::size_t>& outputCounts,
    const std::map<std::size_t, std::vector<core::HexCoord>>& releasedProducts,
    const InventoryStepObserver& observe, bool freeSingletonOrientation,
    const std::map<std::size_t, core::HexFrame>& wheelBindings,
    std::chrono::steady_clock::time_point deadline) {
    using IR = ExecutionIR;
    using K = IR::Kind;
    const auto requiredOutputs =
        outputCounts.empty() ? std::vector<std::size_t>(puzzle.outputs.size(), 1) : outputCounts;
    const auto final = program.Replay(puzzle);
    IR::RequireComplete(final, requiredOutputs);
    if (program.steps.empty() || program.steps.front().operation.kind != K::Acquire)
        throw IR::Unsupported("inventory lowering requires an initial complete feed");
    std::size_t nextSlot = 0;
    for (const auto& step : program.steps) {
        if (deadline != std::chrono::steady_clock::time_point::max() &&
            std::chrono::steady_clock::now() >= deadline)
            throw IR::BudgetFailure("inventory lowering deadline exceeded");
        if (step.operation.wheelCatalyst) {
            const auto& wheel = *step.operation.wheelCatalyst;
            if (!wheelBindings.contains(wheel.device) ||
                wheelBindings.at(wheel.device).Turns() != wheel.orientation)
                throw IR::Unsupported(
                    "inventory lowering requires an explicit physical wheel binding");
        }
        if (step.operation.kind != K::Store && step.operation.kind != K::Retrieve) continue;
        if (step.operation.index == std::numeric_limits<std::size_t>::max())
            throw IR::Unsupported("inventory slot namespace exhausted");
        nextSlot = std::max(nextSlot, step.operation.index + 1);
    }
    const auto& feed = program.steps.front().effects;
    if (feed.atoms.empty() ||
        IR::Fragment(feed, feed.atoms.begin()->first).size() != feed.atoms.size())
        throw IR::Unsupported("inventory lowering requires a connected initial feed");
    PhysicalInventory inventory(feed, track, start, std::move(motion), maxLength);
    inventory.SetDeadline(deadline);
    if (!glyph || !glyph(program.steps.front(), inventory.Arm()))
        throw IR::TimingFailure("initial input spawn interval rejected");
    auto temporarySlot = [&]() {
        if (nextSlot == std::numeric_limits<std::size_t>::max())
            throw IR::Unsupported("inventory slot namespace exhausted");
        return nextSlot++;
    };
    auto reserveLoose = [&]() {
        for (const auto& [id, atom] : inventory.Material().atoms) {
            const auto fragment = IR::Fragment(inventory.Material(), id);
            bool owned = false;
            for (const auto& [arm, grip] : inventory.Custody().Held())
                owned |= std::find(fragment.begin(), fragment.end(), grip) != fragment.end();
            for (const auto& [slot, parked] : inventory.Custody().Parked())
                owned |=
                    std::find(parked.atoms.begin(), parked.atoms.end(), id) != parked.atoms.end();
            if (!owned) inventory.ReserveLoose(id, temporarySlot());
        }
    };
    auto requireRoute = [](MotionStatus status, const char* detail) {
        if (status == MotionStatus::Exhausted) throw IR::BudgetFailure(detail);
        if (status != MotionStatus::Found) throw IR::LayoutFailure(detail);
    };
    auto ensureHeld = [&](IR::AtomId id) {
        const auto fragment = IR::Fragment(inventory.Material(), id);
        if (!inventory.Custody().Held().empty()) {
            const auto grip = inventory.Custody().Held().at(0);
            if (std::find(fragment.begin(), fragment.end(), grip) != fragment.end()) return;
            inventory.Park(temporarySlot());
        }
        for (const auto& [slot, parked] : inventory.Custody().Parked()) {
            if (std::find(parked.atoms.begin(), parked.atoms.end(), id) == parked.atoms.end())
                continue;
            requireRoute(inventory.RetrieveFragmentRouted(slot, budget),
                         "cannot route to inventory operand");
            return;
        }
        throw IR::Error("inventory operand has no custody");
    };
    reserveLoose();
    if (observe) observe(0, inventory);
    std::map<IR::AtomId, core::HexCoord> worldOffsets;
    for (std::size_t i = 1; i < program.steps.size(); ++i) {
        if (deadline != std::chrono::steady_clock::time_point::max() &&
            std::chrono::steady_clock::now() >= deadline)
            throw IR::BudgetFailure("inventory lowering deadline exceeded");
        const auto& step = program.steps[i];
        const auto& op = step.operation;
        if (op.kind == K::Acquire) {
            inventory.Acquire(puzzle, op.index, glyph, {op.translation, op.rotation});
        } else if (op.kind == K::Relocate) {
            const auto anchor = op.atoms.at(0);
            bool delivered = false;
            const auto fragment = IR::Fragment(inventory.Material(), anchor);
            const int rotations = freeSingletonOrientation && fragment.size() == 1 ? 6 : 1;
            if (!inventory.Custody().Held().empty() &&
                std::find(fragment.begin(), fragment.end(), inventory.Custody().Held().at(0)) !=
                    fragment.end()) {
                for (int offset = 0; offset < rotations && !delivered; ++offset) {
                    auto candidate = inventory;
                    const auto grip = candidate.Custody().Held().at(0);
                    const RigidPose contactGoal{step.effects.atoms.at(grip).position,
                                                (op.rotation + offset) % 6};
                    if (candidate.MoveHeld(contactGoal, budget) == MotionStatus::Found) {
                        inventory = std::move(candidate);
                        delivered = true;
                    }
                }
            }
            if (!delivered) {
                if (!inventory.Custody().Held().empty()) inventory.Park(temporarySlot());
                std::size_t source = 0;
                bool found = false;
                for (const auto& [slot, parked] : inventory.Custody().Parked()) {
                    if (std::find(parked.atoms.begin(), parked.atoms.end(), anchor) ==
                        parked.atoms.end())
                        continue;
                    source = slot;
                    found = true;
                    break;
                }
                if (!found) throw IR::Error("relocation operand has no custody");
                MotionStatus status = MotionStatus::Unsatisfiable;
                for (int offset = 0; offset < rotations && !delivered; ++offset) {
                    auto candidate = inventory;
                    const RigidPose goal{step.effects.atoms.at(anchor).position,
                                         (op.rotation + offset) % 6};
                    const auto attempt = candidate.MoveParked(source, anchor, goal, budget);
                    if (attempt == MotionStatus::Found) {
                        inventory = std::move(candidate);
                        delivered = true;
                    } else if (attempt == MotionStatus::Exhausted)
                        status = attempt;
                }
                if (!delivered) {
                    const auto goal = step.effects.atoms.at(anchor).position;
                    const auto detail = "cannot deliver inventory operand " +
                                        std::to_string(anchor) + " at IR step " +
                                        std::to_string(i) + " to (" + std::to_string(goal.q) + "," +
                                        std::to_string(goal.r) + ")";
                    throw IR::PhysicalFailure(detail, i, anchor, goal,
                                              status == MotionStatus::Exhausted);
                }
            }
            for (const auto id : fragment)
                worldOffsets.erase(id);
        } else if (op.kind == K::Purify || op.kind == K::Animismus || op.kind == K::Unify ||
                   op.kind == K::Disperse) {
            const auto placement = releasedProducts.find(i);
            if (placement == releasedProducts.end())
                throw IR::Unsupported(
                    "released chemistry requires explicit physical product cells");
            const auto firstProduct = inventory.Material().nextId;
            const auto held = inventory.Custody().Held().find(0);
            if (held != inventory.Custody().Held().end() &&
                std::find(op.atoms.begin(), op.atoms.end(), held->second) != op.atoms.end()) {
                inventory.ReactOnRelease(temporarySlot(), puzzle, op, placement->second, glyph);
            } else {
                if (!inventory.Custody().Held().empty()) inventory.Park(temporarySlot());
                reserveLoose();
                inventory.ReactReleased(puzzle, op, placement->second, glyph);
            }
            for (auto id : op.atoms)
                worldOffsets.erase(id);
            for (std::size_t product = 0; product < placement->second.size(); ++product) {
                const auto id = firstProduct + product;
                worldOffsets[id] =
                    core::Subtract(placement->second[product], step.effects.atoms.at(id).position);
            }
            reserveLoose();
        } else if (op.kind == K::Calcify || op.kind == K::Duplicate || op.kind == K::Project) {
            const auto operand = op.atoms.at(0);
            const auto wheelGlyph = [&](const IR::Step& actual, const HeldArmState& arm) {
                if (!op.wheelCatalyst) return glyph(actual, arm);
                const auto& wheel = *op.wheelCatalyst;
                const auto& frame = wheelBindings.at(wheel.device);
                const std::array<std::pair<core::AtomType, core::HexCoord>, 4> contacts{
                    {{core::AtomType::Water, {0, 1}},
                     {core::AtomType::Air, {-1, 1}},
                     {core::AtomType::Fire, {0, -1}},
                     {core::AtomType::Earth, {1, -1}}}};
                const auto contact =
                    std::find_if(contacts.begin(), contacts.end(),
                                 [&](const auto& entry) { return entry.first == wheel.element; });
                return contact != contacts.end() &&
                       core::Distance(actual.preconditions.atoms.at(operand).position,
                                      frame.ToWorld(contact->second)) == 1 &&
                       glyph(actual, arm);
            };
            auto activate = [&](PhysicalInventory& candidate) {
                if (op.kind == K::Calcify)
                    candidate.Calcify(puzzle, glyph);
                else if (op.kind == K::Duplicate && op.wheelCatalyst)
                    candidate.DuplicateWheel(puzzle, *op.wheelCatalyst, wheelGlyph);
                else if (op.kind == K::Duplicate)
                    candidate.Duplicate(puzzle, op.atoms.at(1), glyph);
                else
                    candidate.ReactHeld(puzzle, op, glyph);
            };
            bool applied = false;
            if (inventory.Custody().Held().contains(0) &&
                inventory.Custody().Held().at(0) == operand) {
                auto candidate = inventory;
                try {
                    activate(candidate);
                    inventory = std::move(candidate);
                    applied = true;
                } catch (const IR::Error&) {
                }
            }
            if (!applied) {
                if (!inventory.Custody().Held().empty()) inventory.Park(temporarySlot());
                std::optional<std::size_t> source;
                for (const auto& [slot, parked] : inventory.Custody().Parked()) {
                    if (std::find(parked.atoms.begin(), parked.atoms.end(), operand) !=
                        parked.atoms.end()) {
                        source = slot;
                        break;
                    }
                }
                if (!source) throw IR::Error("chemistry operand has no custody");
                const auto parkedGlyph = [&](const IR::Step& actual, const HeldArmState& arm) {
                    return !actual.operation.atoms.empty() &&
                           actual.operation.atoms.front() == operand && glyph(actual, arm);
                };
                const auto parkedWheelGlyph = [&](const IR::Step& actual, const HeldArmState& arm) {
                    return !actual.operation.atoms.empty() &&
                           actual.operation.atoms.front() == operand && wheelGlyph(actual, arm);
                };
                const auto status =
                    op.kind == K::Calcify
                        ? inventory.CalcifyParked(*source, puzzle, parkedGlyph, budget)
                    : op.kind == K::Duplicate && op.wheelCatalyst
                        ? inventory.DuplicateWheelParked(*source, puzzle, *op.wheelCatalyst,
                                                         parkedWheelGlyph, budget)
                    : op.kind == K::Duplicate
                        ? inventory.DuplicateParked(*source, puzzle, op.atoms.at(1), parkedGlyph,
                                                    budget)
                        : inventory.ReactParked(*source, puzzle, op, glyph, budget);
                if (status != MotionStatus::Found)
                    throw IR::Unsupported("cannot route and activate singleton chemistry");
            }
            worldOffsets.erase(operand);
            if (op.kind == K::Project) worldOffsets.erase(op.atoms.at(1));
            const auto product = step.preconditions.nextId;
            const auto physical = inventory.Material().atoms.at(product).position;
            const auto logical = step.effects.atoms.at(product).position;
            worldOffsets[product] = {physical.q - logical.q, physical.r - logical.r};
        } else if (op.kind == K::Bond) {
            // Chemistry products start in a fresh logical frame. Align their
            // fragments before joining; a bond must not freeze unequal frames.
            for (const auto anchor : op.atoms) {
                const auto fragment = IR::Fragment(inventory.Material(), anchor);
                if (std::none_of(fragment.begin(), fragment.end(),
                                 [&](auto id) { return worldOffsets.contains(id); }))
                    continue;
                bool aligned = false;
                if (inventory.Custody().Held().contains(0) &&
                    std::find(fragment.begin(), fragment.end(), inventory.Custody().Held().at(0)) !=
                        fragment.end()) {
                    auto candidate = inventory;
                    const auto grip = candidate.Custody().Held().at(0);
                    const RigidPose goal{step.preconditions.atoms.at(grip).position, 0};
                    if (candidate.MoveHeld(goal, budget) == MotionStatus::Found) {
                        inventory = std::move(candidate);
                        aligned = true;
                    }
                }
                if (!aligned) {
                    if (!inventory.Custody().Held().empty()) inventory.Park(temporarySlot());
                    std::optional<std::size_t> source;
                    for (const auto& [slot, parked] : inventory.Custody().Parked())
                        if (std::find(parked.atoms.begin(), parked.atoms.end(), anchor) !=
                            parked.atoms.end())
                            source = slot;
                    if (!source) throw IR::Error("chemical fragment has no custody");
                    const RigidPose goal{step.preconditions.atoms.at(anchor).position, 0};
                    if (inventory.MoveParked(*source, anchor, goal, budget) != MotionStatus::Found)
                        throw IR::Unsupported("cannot align chemical fragment for joining");
                }
                for (const auto id : fragment)
                    worldOffsets.erase(id);
            }
            const auto before = inventory;
            const auto beforeSlot = nextSlot;
            bool applied = false;
            for (auto anchor : op.atoms) {
                inventory = before;
                nextSlot = beforeSlot;
                try {
                    ensureHeld(anchor);
                    const auto held =
                        IR::Fragment(inventory.Material(), inventory.Custody().Held().at(0));
                    std::optional<std::size_t> partner;
                    for (auto id : op.atoms) {
                        if (std::find(held.begin(), held.end(), id) != held.end()) continue;
                        for (const auto& [slot, parked] : inventory.Custody().Parked())
                            if (std::find(parked.atoms.begin(), parked.atoms.end(), id) !=
                                parked.atoms.end())
                                partner = slot;
                        if (!partner) throw IR::Error("bond partner has no custody");
                    }
                    if (partner)
                        inventory.BondHeldToParked(*partner, puzzle, op, glyph);
                    else
                        inventory.ApplyBond(puzzle, op, glyph);
                    applied = true;
                    break;
                } catch (const IR::Error&) {
                }
                inventory = before;
                nextSlot = beforeSlot;
                try {
                    if (!inventory.Custody().Held().empty()) inventory.Park(temporarySlot());
                    std::optional<std::size_t> source;
                    for (const auto& [slot, parked] : inventory.Custody().Parked())
                        if (std::find(parked.atoms.begin(), parked.atoms.end(), anchor) !=
                            parked.atoms.end())
                            source = slot;
                    if (source && inventory.ApplyParkedBond(*source, puzzle, op, glyph, budget) ==
                                      MotionStatus::Found) {
                        applied = true;
                        break;
                    }
                } catch (const IR::Error&) {
                }
            }
            if (!applied) {
                inventory = before;
                nextSlot = beforeSlot;
                throw IR::Unsupported("cannot join inventory operands at certified glyph");
            }
        } else if (op.kind == K::Unbond) {
            bool applied = false;
            const auto operand = IR::Fragment(inventory.Material(), op.atoms.at(0));
            if (!inventory.Custody().Held().empty() &&
                std::find(operand.begin(), operand.end(), inventory.Custody().Held().at(0)) !=
                    operand.end()) {
                auto candidate = inventory;
                try {
                    candidate.ApplyBond(puzzle, op, glyph);
                    inventory = std::move(candidate);
                    applied = true;
                } catch (const IR::Error&) {
                }
            }
            if (!applied) {
                if (!inventory.Custody().Held().empty()) inventory.Park(temporarySlot());
                std::size_t source = 0;
                bool found = false;
                for (const auto& [slot, parked] : inventory.Custody().Parked()) {
                    if (std::find(parked.atoms.begin(), parked.atoms.end(), op.atoms.at(0)) ==
                        parked.atoms.end())
                        continue;
                    source = slot;
                    found = true;
                    break;
                }
                if (!found) throw IR::Error("bond operand has no custody");
                if (inventory.ApplyParkedBond(source, puzzle, op, glyph, budget) !=
                    MotionStatus::Found)
                    throw IR::Unsupported("cannot route and activate inventory glyph");
            }
        } else if (op.kind == K::Store) {
            ensureHeld(op.atoms.at(0));
            inventory.Park(op.index);
        } else if (op.kind == K::Retrieve) {
            if (!inventory.Custody().Held().empty()) inventory.Park(temporarySlot());
            ensureHeld(step.preconditions.stored.at(op.index).at(0));
        } else if (op.kind == K::Output || op.kind == K::Discard) {
            bool applied = false;
            if (!inventory.Custody().Held().empty()) {
                const auto fragment = IR::Fragment(inventory.Material(), op.atoms.at(0));
                if (std::find(fragment.begin(), fragment.end(), inventory.Custody().Held().at(0)) !=
                    fragment.end()) {
                    auto candidate = inventory;
                    try {
                        if (op.kind == K::Output)
                            candidate.Output(puzzle, op.index, glyph);
                        else
                            candidate.Discard(puzzle, glyph);
                        inventory = std::move(candidate);
                        applied = true;
                    } catch (const IR::Error&) {
                    }
                }
            }
            if (!applied) {
                if (!inventory.Custody().Held().empty()) inventory.Park(temporarySlot());
                std::size_t source = 0;
                bool found = false;
                for (const auto& [slot, parked] : inventory.Custody().Parked()) {
                    if (std::find(parked.atoms.begin(), parked.atoms.end(), op.atoms.at(0)) ==
                        parked.atoms.end())
                        continue;
                    source = slot;
                    found = true;
                    break;
                }
                if (!found) throw IR::Error("consumed operand has no custody");
                const auto status =
                    op.kind == K::Output
                        ? inventory.OutputParked(source, puzzle, op.index, glyph, budget)
                        : inventory.DiscardParked(source, puzzle, glyph, budget);
                if (status != MotionStatus::Found)
                    throw IR::Unsupported("cannot route and activate inventory consumption");
            }
        } else {
            throw IR::Unsupported("inventory lowering does not implement this operation");
        }
        auto expected = step.effects;
        expected.stored.clear();
        for (const auto& [id, offset] : worldOffsets) {
            if (!expected.atoms.contains(id)) continue;
            auto& position = expected.atoms.at(id).position;
            position = {position.q + offset.q, position.r + offset.r};
        }
        if (inventory.Material() != expected)
            throw IR::Error("inventory lowering differs from trusted material IR");
        reserveLoose();
        if (observe) observe(i, inventory);
    }
    IR::RequireComplete(inventory.Material(), requiredOutputs);
    if (inventory.MoveEmpty(start, budget) != MotionStatus::Found)
        throw IR::Unsupported("cannot reset inventory arm");
    return inventory;
}
} // namespace opusoptima::solvers
