#include "PhysicalInventory.hpp"

#include <algorithm>

namespace opusoptima::solvers {
namespace {
core::HexCoord Relative(core::HexCoord a, core::HexCoord b) {
    return {a.q - b.q, a.r - b.r};
}
core::HexCoord Rotate(core::HexCoord c, int n) {
    for (int i = 0; i < (n % 6 + 6) % 6; ++i)
        c = core::RotateClockwise(c);
    return c;
}
} // namespace
PhysicalInventory::PhysicalInventory(ExecutionIR::State state, std::vector<core::HexCoord> track,
                                     HeldArmState arm, Certificate certificate, int maxLength)
    : material_(std::move(state)), track_(std::move(track)), arm_(arm), maxLength_(maxLength),
      certificate_(std::move(certificate)) {
    EmptyArmMotionProblem check;
    check.track = track_;
    check.start = check.goal = arm_;
    check.maxLength = maxLength_;
    check.executable = [](const auto&, const auto&, char) { return false; };
    (void)RouteEmptyArm(check);
    if (!certificate_)
        throw ExecutionIR::Error("physical inventory requires interval certification");
    custody_.Validate(material_);
    arm_.fragmentRotation = 0;
}
core::HexCoord PhysicalInventory::Tip() const {
    return core::Add(track_.at(arm_.trackIndex), Rotate({arm_.length, 0}, arm_.direction));
}
void PhysicalInventory::Grab(ExecutionIR::AtomId id) {
    auto next = custody_;
    next.Grab(material_, 0, Tip(), id);
    if (!certificate_(material_, arm_, arm_, 'G', false))
        throw ExecutionIR::Error("grab interval rejected");
    custody_ = std::move(next);
    tape_.push_back('G');
}
void PhysicalInventory::Park(std::size_t slot) {
    auto next = custody_;
    next.Park(material_, 0, slot);
    if (!certificate_(material_, arm_, arm_, 'g', true))
        throw ExecutionIR::Error("release interval rejected");
    custody_ = std::move(next);
    tape_.push_back('g');
}
void PhysicalInventory::ReserveLoose(ExecutionIR::AtomId id, std::size_t slot) {
    custody_.ReserveLoose(material_, id, slot);
}
void PhysicalInventory::Retrieve(std::size_t slot, ExecutionIR::AtomId id) {
    auto next = custody_;
    next.Retrieve(material_, slot, 0, Tip(), id);
    if (!certificate_(material_, arm_, arm_, 'G', false))
        throw ExecutionIR::Error("retrieve interval rejected");
    custody_ = std::move(next);
    tape_.push_back('G');
}
MotionStatus PhysicalInventory::RetrieveRouted(std::size_t slot, ExecutionIR::AtomId id,
                                               std::size_t budget) {
    custody_.Validate(material_);
    if (!custody_.Held().empty())
        throw ExecutionIR::Error("retrieval routing requires an empty arm");
    const auto target = material_.atoms.at(id).position;
    auto check = custody_;
    check.Retrieve(material_, slot, 0, target, id);
    bool exhausted = false;
    for (std::size_t index = 0; index < track_.size(); ++index) {
        for (int length = 1; length <= maxLength_; ++length) {
            for (int direction = 0; direction < 6; ++direction) {
                if (deadline_ != std::chrono::steady_clock::time_point::max() &&
                    std::chrono::steady_clock::now() >= deadline_)
                    return MotionStatus::Exhausted;
                if (core::Add(track_[index], Rotate({length, 0}, direction)) != target) continue;
                auto candidate = *this;
                const auto status = candidate.MoveEmpty({index, length, direction, 0}, budget);
                exhausted |= status == MotionStatus::Exhausted;
                if (status != MotionStatus::Found) continue;
                try {
                    candidate.Retrieve(slot, id);
                } catch (const ExecutionIR::Error&) {
                    continue;
                }
                *this = std::move(candidate);
                return MotionStatus::Found;
            }
        }
    }
    return exhausted ? MotionStatus::Exhausted : MotionStatus::Unsatisfiable;
}
MotionStatus PhysicalInventory::RetrieveFragmentRouted(std::size_t slot, std::size_t budget) {
    custody_.Validate(material_);
    if (!custody_.Held().empty()) throw ExecutionIR::Error("arm is not empty");
    const auto parked = custody_.Parked().find(slot);
    if (parked == custody_.Parked().end()) throw ExecutionIR::Error("unknown parking slot");
    bool exhausted = false;
    for (auto id : parked->second.atoms) {
        if (deadline_ != std::chrono::steady_clock::time_point::max() &&
                    std::chrono::steady_clock::now() >= deadline_)
            return MotionStatus::Exhausted;
        auto candidate = *this;
        const auto status = candidate.RetrieveRouted(slot, id, budget);
        if (status == MotionStatus::Found) {
            *this = std::move(candidate);
            return status;
        }
        exhausted |= status == MotionStatus::Exhausted;
    }
    return exhausted ? MotionStatus::Exhausted : MotionStatus::Unsatisfiable;
}

MotionStatus PhysicalInventory::MoveParked(std::size_t slot, ExecutionIR::AtomId anchor,
                                           RigidPose goal, std::size_t budget) {
    custody_.Validate(material_);
    if (!custody_.Held().empty()) throw ExecutionIR::Error("parked routing requires an empty arm");
    const auto parked = custody_.Parked().find(slot);
    if (parked == custody_.Parked().end() ||
        std::find(parked->second.atoms.begin(), parked->second.atoms.end(), anchor) ==
            parked->second.atoms.end())
        throw ExecutionIR::Error("transport anchor is not in parking slot");
    const auto origin = material_.atoms.at(anchor).position;
    bool exhausted = false;
    for (auto id : parked->second.atoms) {
        const auto contact = material_.atoms.at(id).position;
        const RigidPose contactGoal{
            core::Add(goal.anchor, Rotate(Relative(contact, origin), goal.rotation)),
            goal.rotation};
        for (std::size_t index = 0; index < track_.size(); ++index) {
            for (int length = 1; length <= maxLength_; ++length) {
                for (int direction = 0; direction < 6; ++direction) {
                    if (deadline_ != std::chrono::steady_clock::time_point::max() &&
                    std::chrono::steady_clock::now() >= deadline_)
                        return MotionStatus::Exhausted;
                    if (core::Add(track_[index], Rotate({length, 0}, direction)) != contact)
                        continue;
                    auto candidate = *this;
                    auto status = candidate.MoveEmpty({index, length, direction, 0}, budget);
                    exhausted |= status == MotionStatus::Exhausted;
                    if (status != MotionStatus::Found) continue;
                    try {
                        candidate.Retrieve(slot, id);
                    } catch (const ExecutionIR::Error&) {
                        continue;
                    }
                    status = candidate.MoveHeld(contactGoal, budget);
                    exhausted |= status == MotionStatus::Exhausted;
                    if (status != MotionStatus::Found) continue;
                    *this = std::move(candidate);
                    return MotionStatus::Found;
                }
            }
        }
    }
    return exhausted ? MotionStatus::Exhausted : MotionStatus::Unsatisfiable;
}

void PhysicalInventory::Acquire(const io::PuzzleData& puzzle, std::size_t input,
                                const BondCertificate& certificate, core::HexFrame frame) {
    if (!certificate) throw ExecutionIR::Error("input acquisition requires interval certification");
    custody_.Validate(material_);
    const ExecutionIR::Operation operation{
        ExecutionIR::Kind::Acquire, input, {}, 0, frame.Turns(), frame.origin};
    auto next = material_;
    ExecutionIR::Apply(puzzle, next, operation);
    custody_.Validate(next);
    if (!certificate({operation, material_, next}, arm_))
        throw ExecutionIR::Error("input spawn interval rejected");
    material_ = std::move(next);
    tape_.push_back(' ');
}

void PhysicalInventory::Calcify(const io::PuzzleData& puzzle, const BondCertificate& certificate) {
    if (!certificate)
        throw ExecutionIR::Error("calcification requires glyph interval certification");
    custody_.Validate(material_);
    if (!custody_.Held().contains(0)) throw ExecutionIR::Error("calcification requires held stock");
    const auto id = custody_.Held().at(0);
    const auto position = material_.atoms.at(id).position;
    const ExecutionIR::Operation operation{ExecutionIR::Kind::Calcify, 0, {id}};
    auto next = material_;
    ExecutionIR::Apply(puzzle, next, operation);
    const auto product = material_.nextId;
    next.atoms.at(product).position = position;
    auto custody = custody_;
    custody.Drop(0);
    custody.Grab(next, 0, Tip(), product);
    if (!certificate({operation, material_, next}, arm_))
        throw ExecutionIR::Error("calcification glyph interval rejected");
    material_ = std::move(next);
    custody_ = std::move(custody);
    tape_.push_back(' ');
}

MotionStatus PhysicalInventory::CalcifyParked(std::size_t slot, const io::PuzzleData& puzzle,
                                              const BondCertificate& certificate,
                                              std::size_t budget) {
    return ActivateParked(
        slot, [&](PhysicalInventory& candidate) { candidate.Calcify(puzzle, certificate); },
        budget);
}

void PhysicalInventory::ReactReleased(const io::PuzzleData& puzzle,
                                      const ExecutionIR::Operation& operation,
                                      const std::vector<core::HexCoord>& products,
                                      const BondCertificate& certificate) {
    using K = ExecutionIR::Kind;
    if (operation.kind != K::Purify && operation.kind != K::Animismus &&
        operation.kind != K::Unify && operation.kind != K::Disperse)
        throw ExecutionIR::Unsupported(
            "released reaction requires purification or coproduct chemistry");
    if (!certificate)
        throw ExecutionIR::Error("released reaction requires glyph interval certification");
    custody_.Validate(material_);
    for (auto id : operation.atoms) {
        if (std::none_of(custody_.Parked().begin(), custody_.Parked().end(),
                         [&](const auto& entry) {
                             return entry.second.atoms == std::vector<ExecutionIR::AtomId>{id};
                         }))
            throw ExecutionIR::Error("released reaction requires singleton parking custody");
    }
    auto next = material_;
    ExecutionIR::Apply(puzzle, next, operation);
    if (next.nextId - material_.nextId != products.size())
        throw ExecutionIR::Error("released reaction product cell count mismatch");
    for (std::size_t i = 0; i < products.size(); ++i)
        next.atoms.at(material_.nextId + i).position = products[i];
    auto custody = custody_;
    custody.ConsumeSingletons(material_, next, operation.atoms);
    if (!certificate({operation, material_, next}, arm_))
        throw ExecutionIR::Error("released reaction glyph interval rejected");
    material_ = std::move(next);
    custody_ = std::move(custody);
    tape_.push_back(' ');
}

void PhysicalInventory::ReactOnRelease(std::size_t slot, const io::PuzzleData& puzzle,
                                       const ExecutionIR::Operation& operation,
                                       const std::vector<core::HexCoord>& products,
                                       const BondCertificate& certificate) {
    const auto held = custody_.Held().find(0);
    if (held == custody_.Held().end() || std::find(operation.atoms.begin(), operation.atoms.end(),
                                                   held->second) == operation.atoms.end())
        throw ExecutionIR::Error("reaction release requires a held reagent");
    if (!certificate_(material_, arm_, arm_, 'g', true))
        throw ExecutionIR::Error("reaction release interval rejected");
    auto candidate = *this;
    candidate.custody_.Park(candidate.material_, 0, slot);
    candidate.ReactReleased(puzzle, operation, products, certificate);
    candidate.tape_.back() = 'g';
    *this = std::move(candidate);
}

void PhysicalInventory::ReactHeld(const io::PuzzleData& puzzle,
                                  const ExecutionIR::Operation& operation,
                                  const BondCertificate& certificate) {
    using K = ExecutionIR::Kind;
    if (operation.kind != K::Project)
        throw ExecutionIR::Unsupported("held reaction requires projection");
    if (!certificate) throw ExecutionIR::Error("reaction requires glyph interval certification");
    custody_.Validate(material_);
    if (operation.atoms.empty() || !custody_.Held().contains(0) ||
        custody_.Held().at(0) != operation.atoms.front())
        throw ExecutionIR::Error("reaction requires held first operand");
    for (std::size_t i = 1; i < operation.atoms.size(); ++i) {
        const auto id = operation.atoms[i];
        if (std::none_of(custody_.Parked().begin(), custody_.Parked().end(),
                         [&](const auto& entry) {
                             return entry.second.atoms == std::vector<ExecutionIR::AtomId>{id};
                         }))
            throw ExecutionIR::Error("reaction partner requires singleton parking custody");
    }
    auto next = material_;
    ExecutionIR::Apply(puzzle, next, operation);
    const auto product = material_.nextId;
    next.atoms.at(product).position = material_.atoms.at(operation.atoms.front()).position;
    auto custody = custody_;
    if (ExecutionIR::Fragment(material_, operation.atoms.front()).size() > 1)
        custody.ProjectHeld(material_, next, 0, operation.atoms.front(), operation.atoms.at(1),
                            product);
    else {
        custody.ConsumeSingletons(material_, next, operation.atoms);
        custody.Grab(next, 0, Tip(), product);
    }
    if (!certificate({operation, material_, next}, arm_))
        throw ExecutionIR::Error("reaction glyph interval rejected");
    material_ = std::move(next);
    custody_ = std::move(custody);
    tape_.push_back(' ');
}

MotionStatus PhysicalInventory::ReactParked(std::size_t slot, const io::PuzzleData& puzzle,
                                            const ExecutionIR::Operation& operation,
                                            const BondCertificate& certificate,
                                            std::size_t budget) {
    return ActivateParked(
        slot,
        [&](PhysicalInventory& candidate) { candidate.ReactHeld(puzzle, operation, certificate); },
        budget);
}

void PhysicalInventory::Duplicate(const io::PuzzleData& puzzle, ExecutionIR::AtomId catalyst,
                                  const BondCertificate& certificate) {
    if (!certificate) throw ExecutionIR::Error("duplication requires glyph interval certification");
    custody_.Validate(material_);
    if (!custody_.Held().contains(0))
        throw ExecutionIR::Error("duplication requires held salt stock");
    const auto id = custody_.Held().at(0);
    const auto position = material_.atoms.at(id).position;
    const ExecutionIR::Operation operation{ExecutionIR::Kind::Duplicate, 0, {id, catalyst}};
    auto next = material_;
    ExecutionIR::Apply(puzzle, next, operation);
    const auto product = material_.nextId;
    next.atoms.at(product).position = position;
    auto custody = custody_;
    custody.Drop(0);
    custody.Grab(next, 0, Tip(), product);
    custody.Validate(next);
    if (!certificate({operation, material_, next}, arm_))
        throw ExecutionIR::Error("duplication glyph interval rejected");
    material_ = std::move(next);
    custody_ = std::move(custody);
    tape_.push_back(' ');
}

void PhysicalInventory::DuplicateWheel(const io::PuzzleData& puzzle,
                                       const ExecutionIR::WheelCatalyst& wheel,
                                       const BondCertificate& certificate) {
    if (!certificate)
        throw ExecutionIR::Error("wheel duplication requires glyph interval certification");
    custody_.Validate(material_);
    if (!custody_.Held().contains(0))
        throw ExecutionIR::Error("wheel duplication requires held salt stock");
    const auto id = custody_.Held().at(0);
    ExecutionIR::Operation operation{ExecutionIR::Kind::Duplicate, 0, {id}};
    operation.wheelCatalyst = wheel;
    auto next = material_;
    ExecutionIR::Apply(puzzle, next, operation);
    const auto product = material_.nextId;
    next.atoms.at(product).position = material_.atoms.at(id).position;
    auto custody = custody_;
    custody.Drop(0);
    custody.Grab(next, 0, Tip(), product);
    custody.Validate(next);
    if (!certificate({operation, material_, next}, arm_))
        throw ExecutionIR::Error("wheel duplication glyph interval rejected");
    material_ = std::move(next);
    custody_ = std::move(custody);
    tape_.push_back(' ');
}

MotionStatus PhysicalInventory::DuplicateWheelParked(std::size_t slot, const io::PuzzleData& puzzle,
                                                     const ExecutionIR::WheelCatalyst& wheel,
                                                     const BondCertificate& certificate,
                                                     std::size_t budget) {
    return ActivateParked(
        slot,
        [&](PhysicalInventory& candidate) { candidate.DuplicateWheel(puzzle, wheel, certificate); },
        budget);
}

MotionStatus PhysicalInventory::DuplicateParked(std::size_t slot, const io::PuzzleData& puzzle,
                                                ExecutionIR::AtomId catalyst,
                                                const BondCertificate& certificate,
                                                std::size_t budget) {
    return ActivateParked(
        slot,
        [&](PhysicalInventory& candidate) { candidate.Duplicate(puzzle, catalyst, certificate); },
        budget);
}

void PhysicalInventory::ApplyBond(const io::PuzzleData& puzzle,
                                  const ExecutionIR::Operation& operation,
                                  const BondCertificate& certificate) {
    if (operation.kind != ExecutionIR::Kind::Bond && operation.kind != ExecutionIR::Kind::Unbond)
        throw ExecutionIR::Unsupported("inventory bond transition requires Bond or Unbond");
    if (!certificate)
        throw ExecutionIR::Error("bond transition requires glyph interval certification");
    custody_.Validate(material_);
    auto next = material_;
    ExecutionIR::Apply(puzzle, next, operation);
    custody_.Validate(next);
    if (!certificate({operation, material_, next}, arm_))
        throw ExecutionIR::Error("bond glyph interval rejected");
    material_ = std::move(next);
    tape_.push_back(' ');
}
void PhysicalInventory::BondHeldToParked(std::size_t slot, const io::PuzzleData& puzzle,
                                         const ExecutionIR::Operation& operation,
                                         const BondCertificate& certificate) {
    if (operation.kind != ExecutionIR::Kind::Bond)
        throw ExecutionIR::Unsupported("parking merge requires Bond");
    if (!certificate)
        throw ExecutionIR::Error("parking merge requires glyph interval certification");
    auto next = material_;
    ExecutionIR::Apply(puzzle, next, operation);
    auto custody = custody_;
    custody.MergeParked(material_, next, 0, slot);
    if (!certificate({operation, material_, next}, arm_))
        throw ExecutionIR::Error("parking merge glyph interval rejected");
    material_ = std::move(next);
    custody_ = std::move(custody);
    tape_.push_back(' ');
}
MotionStatus PhysicalInventory::ApplyParkedBond(std::size_t slot, const io::PuzzleData& puzzle,
                                                const ExecutionIR::Operation& operation,
                                                const BondCertificate& certificate,
                                                std::size_t budget) {
    custody_.Validate(material_);
    if (!custody_.Held().empty())
        throw ExecutionIR::Error("parked glyph routing requires an empty arm");
    if (!certificate)
        throw ExecutionIR::Error("bond transition requires glyph interval certification");
    if (operation.kind != ExecutionIR::Kind::Unbond && operation.kind != ExecutionIR::Kind::Bond)
        throw ExecutionIR::Unsupported("inventory bond transition requires Bond or Unbond");
    const auto parked = custody_.Parked().find(slot);
    if (parked == custody_.Parked().end()) throw ExecutionIR::Error("unknown parked fragment slot");
    const auto atoms = parked->second.atoms;
    if (operation.atoms.empty() ||
        std::none_of(operation.atoms.begin(), operation.atoms.end(), [&](auto id) {
            return std::find(atoms.begin(), atoms.end(), id) != atoms.end();
        }))
        throw ExecutionIR::Error("bond operand is not in the selected parked fragment");
    auto expected = material_;
    ExecutionIR::Apply(puzzle, expected, operation);
    return ActivateParked(
        slot,
        [&](PhysicalInventory& candidate) {
            const auto held =
                ExecutionIR::Fragment(candidate.material_, candidate.custody_.Held().at(0));
            for (auto id : operation.atoms) {
                if (std::find(held.begin(), held.end(), id) != held.end()) continue;
                for (const auto& [partner, reservation] : candidate.custody_.Parked()) {
                    if (std::find(reservation.atoms.begin(), reservation.atoms.end(), id) ==
                        reservation.atoms.end())
                        continue;
                    candidate.BondHeldToParked(partner, puzzle, operation, certificate);
                    return;
                }
                throw ExecutionIR::Error("bond partner has no parking reservation");
            }
            candidate.ApplyBond(puzzle, operation, certificate);
        },
        budget);
}
MotionStatus PhysicalInventory::OutputParked(std::size_t slot, const io::PuzzleData& puzzle,
                                             std::size_t output, const BondCertificate& certificate,
                                             std::size_t budget) {
    if (!certificate) throw ExecutionIR::Error("output requires glyph interval certification");
    custody_.Validate(material_);
    if (!custody_.Held().empty()) throw ExecutionIR::Error("parked output requires an empty arm");
    const auto parked = custody_.Parked().find(slot);
    if (parked == custody_.Parked().end()) throw ExecutionIR::Error("unknown parked fragment slot");
    auto expected = material_;
    ExecutionIR::Apply(puzzle, expected,
                       {ExecutionIR::Kind::Output, output, {parked->second.atoms.front()}});
    return ActivateParked(
        slot, [&](PhysicalInventory& candidate) { candidate.Output(puzzle, output, certificate); },
        budget);
}
MotionStatus PhysicalInventory::ActivateParked(
    std::size_t slot, const std::function<void(PhysicalInventory&)>& activate, std::size_t budget) {
    custody_.Validate(material_);
    if (!custody_.Held().empty())
        throw ExecutionIR::Error("parked activation requires an empty arm");
    const auto parked = custody_.Parked().find(slot);
    if (parked == custody_.Parked().end()) throw ExecutionIR::Error("unknown parked fragment slot");
    const auto atoms = parked->second.atoms;
    bool exhausted = false;
    for (auto id : atoms) {
        for (std::size_t index = 0; index < track_.size(); ++index) {
            for (int length = 1; length <= maxLength_; ++length) {
                for (int direction = 0; direction < 6; ++direction) {
                    if (deadline_ != std::chrono::steady_clock::time_point::max() &&
                    std::chrono::steady_clock::now() >= deadline_)
                        return MotionStatus::Exhausted;
                    if (core::Add(track_[index], Rotate({length, 0}, direction)) !=
                        material_.atoms.at(id).position)
                        continue;
                    auto candidate = *this;
                    const auto status = candidate.MoveEmpty({index, length, direction, 0}, budget);
                    exhausted |= status == MotionStatus::Exhausted;
                    if (status != MotionStatus::Found) continue;
                    try {
                        candidate.Retrieve(slot, id);
                        activate(candidate);
                    } catch (const ExecutionIR::Error&) {
                        continue;
                    }
                    *this = std::move(candidate);
                    return MotionStatus::Found;
                }
            }
        }
    }
    return exhausted ? MotionStatus::Exhausted : MotionStatus::Unsatisfiable;
}

MotionStatus PhysicalInventory::DiscardParked(std::size_t slot, const io::PuzzleData& puzzle,
                                              const BondCertificate& certificate,
                                              std::size_t budget) {
    custody_.Validate(material_);
    if (!custody_.Held().empty()) throw ExecutionIR::Error("parked disposal requires an empty arm");
    const auto parked = custody_.Parked().find(slot);
    if (parked == custody_.Parked().end()) throw ExecutionIR::Error("unknown parked fragment slot");
    auto expected = material_;
    ExecutionIR::Apply(puzzle, expected,
                       {ExecutionIR::Kind::Discard, 0, {parked->second.atoms.front()}});
    return ActivateParked(
        slot, [&](PhysicalInventory& candidate) { candidate.Discard(puzzle, certificate); },
        budget);
}

void PhysicalInventory::Discard(const io::PuzzleData& puzzle, const BondCertificate& certificate) {
    custody_.Validate(material_);
    if (custody_.Held().size() != 1 || !certificate)
        throw ExecutionIR::Error("disposal requires held stock and interval certification");
    const ExecutionIR::Operation operation{ExecutionIR::Kind::Discard, 0, {custody_.Held().at(0)}};
    auto material = material_;
    auto custody = custody_;
    ExecutionIR::Apply(puzzle, material, operation);
    custody.Drop(0);
    custody.Validate(material);
    if (!certificate_(material_, arm_, arm_, 'g', true) ||
        !certificate({operation, material_, material}, arm_))
        throw ExecutionIR::Error("disposal release interval rejected");
    material_ = std::move(material);
    custody_ = std::move(custody);
    tape_.push_back('g');
}

void PhysicalInventory::Output(const io::PuzzleData& puzzle, std::size_t output,
                               const BondCertificate& certificate) {
    custody_.Validate(material_);
    if (custody_.Held().size() != 1 || !certificate)
        throw ExecutionIR::Error("output requires a held product and glyph interval certification");
    const auto id = custody_.Held().at(0);
    const ExecutionIR::Operation operation{ExecutionIR::Kind::Output, output, {id}};
    auto material = material_;
    auto custody = custody_;
    ExecutionIR::Apply(puzzle, material, operation);
    custody.Drop(0);
    custody.Validate(material);
    if (!certificate_(material_, arm_, arm_, 'g', true) ||
        !certificate({operation, material_, material}, arm_))
        throw ExecutionIR::Error("output release interval rejected");
    material_ = std::move(material);
    custody_ = std::move(custody);
    tape_.push_back('g');
}
MotionStatus PhysicalInventory::MoveEmpty(HeldArmState goal, std::size_t budget) {
    custody_.Validate(material_);
    if (!custody_.Held().empty())
        throw ExecutionIR::Error("empty routing requires released material");
    EmptyArmMotionProblem p;
    p.track = track_;
    p.start = arm_;
    p.goal = goal;
    p.maxStates = budget;
    p.maxLength = maxLength_;
    p.deadline = deadline_;
    p.executable = [&](const auto& from, const auto& to, char op) {
        return certificate_(material_, from, to, op, false);
    };
    auto result = RouteEmptyArm(p);
    if (result.status == MotionStatus::Found) {
        arm_ = result.states.back();
        tape_.insert(tape_.end(), result.instructions.begin(), result.instructions.end());
    }
    return result.status;
}
MotionStatus PhysicalInventory::MoveHeld(RigidPose goal, std::size_t budget) {
    custody_.Validate(material_);
    if (custody_.Held().size() != 1)
        throw ExecutionIR::Error("held routing requires one grabbed fragment");
    const auto id = custody_.Held().at(0);
    const auto origin = material_.atoms.at(id).position;
    if (origin != Tip()) throw ExecutionIR::Error("held fragment lost gripper contact");
    const auto ids = ExecutionIR::Fragment(material_, id);
    HeldArmMotionProblem p;
    p.track = track_;
    p.start = arm_;
    p.start.fragmentRotation = 0;
    p.maxLength = maxLength_;
    p.goal = goal;
    p.maxStates = budget;
    p.deadline = deadline_;
    for (auto atom : ids)
        p.fragment.push_back(Relative(material_.atoms.at(atom).position, origin));
    auto at = [&](const HeldArmState& arm) {
        auto state = material_;
        const auto pose = HeldFragmentPose(p, arm);
        for (auto atom : ids)
            state.atoms.at(atom).position =
                core::Add(pose.anchor, Rotate(Relative(material_.atoms.at(atom).position, origin),
                                              pose.rotation));
        return state;
    };
    p.executable = [&](const auto& from, const auto& to, char op) {
        auto before = at(from);
        try {
            custody_.Validate(before);
            custody_.Validate(at(to));
        } catch (const ExecutionIR::Error&) {
            return false;
        }
        return certificate_(before, from, to, op, true);
    };
    auto result = RouteHeldArm(p);
    if (result.status == MotionStatus::Found) {
        material_ = at(result.states.back());
        arm_ = result.states.back();
        arm_.fragmentRotation = 0;
        tape_.insert(tape_.end(), result.instructions.begin(), result.instructions.end());
    }
    return result.status;
}
} // namespace opusoptima::solvers
