#include "ProductionExecution.hpp"
#include "HexFrame.hpp"
#include "ProductionRouting.hpp"

#include <algorithm>
#include <set>

namespace opusoptima::solvers {
namespace {
using IR = ExecutionIR;
using Coord = core::HexCoord;
Coord Turn(Coord cell, int count) {
    for (int i = 0; i < count; ++i)
        cell = core::RotateClockwise(cell);
    return cell;
}
bool Reaction(IR::Kind kind) {
    return kind == IR::Kind::Calcify || kind == IR::Kind::Project || kind == IR::Kind::Purify ||
           kind == IR::Kind::Animismus || kind == IR::Kind::Unify || kind == IR::Kind::Disperse ||
           kind == IR::Kind::Duplicate;
}
} // namespace
ProductionExecution::ProductionExecution(const io::PuzzleData& puzzle)
    : puzzle_(puzzle), chambers_(PlanProductionChambers(puzzle)),
      program_(IR::ForStaticProduction(puzzle)), state_(IR::InitialForStaticProduction(puzzle)) {}

void ProductionExecution::CheckRooms(const IR::State& state,
                                     const std::map<AtomId, std::size_t>& owners,
                                     const std::map<std::size_t, Pending>& transfers) const {
    std::map<AtomId, std::size_t> transiting;
    std::set<std::pair<int, int>> reserved;
    for (const auto& [conduit, pending] : transfers)
        for (auto id : pending.transfer.atoms) {
            if (!transiting.emplace(id, conduit).second || owners.contains(id) ||
                !state.atoms.contains(id))
                throw IR::LayoutFailure("duplicate material custody during conduit transfer");
            const auto& handoff = pending.transfer.handoff;
            const auto local = core::Subtract(state.atoms.at(id).position,
                                              pending.arrived ? handoff.destinationPosition
                                                              : handoff.sourcePosition);
            const auto arrival = core::Add(handoff.destinationPosition, local);
            if (!ContainsProductionCell(chambers_.at(pending.transfer.destinationRoom), arrival) ||
                !reserved.emplace(arrival.q, arrival.r).second)
                throw IR::LayoutFailure("overlapping conduit arrival reservations");
        }
    if (state.atoms.size() != owners.size() + transiting.size())
        throw IR::LayoutFailure("production material lacks unique room or conduit custody");
    std::set<std::pair<int, int>> occupied;
    for (const auto& [id, atom] : state.atoms) {
        const auto owner = owners.find(id);
        if (owner == owners.end()) {
            if (!transiting.contains(id)) throw IR::LayoutFailure("production atom has no custody");
            const auto& transfer = transfers.at(transiting.at(id));
            if (!transfer.arrived) continue;
            if (!ContainsProductionCell(chambers_.at(transfer.transfer.destinationRoom),
                                        atom.position))
                throw IR::LayoutFailure("arrived conduit atom is outside destination room");
        } else if (owner->second >= chambers_.size() ||
                   !ContainsProductionCell(chambers_[owner->second], atom.position)) {
            throw IR::LayoutFailure("owned atom is outside its production room");
        }
        if (!occupied.emplace(atom.position.q, atom.position.r).second ||
            (owner != owners.end() && reserved.contains({atom.position.q, atom.position.r})))
            throw IR::LayoutFailure("occupied production material cells overlap");
    }
}

void ProductionExecution::Commit(std::size_t room, const IR::Operation& operation,
                                 const std::vector<Coord>* productCells) {
    if (room >= chambers_.size()) throw IR::LayoutFailure("unknown production room");
    if (Reaction(operation.kind) != (productCells != nullptr))
        throw IR::Unsupported("production reaction requires explicit physical product cells");
    if (operation.kind != IR::Kind::Acquire) {
        for (auto id : operation.atoms) {
            if (!roomOf_.contains(id) || roomOf_.at(id) != room)
                throw IR::LayoutFailure("material operand belongs to another room or conduit");
            if (IR::Fragment(state_, id).size() > 1 &&
                (operation.kind == IR::Kind::Relocate || operation.kind == IR::Kind::Store ||
                 operation.kind == IR::Kind::Discard || operation.kind == IR::Kind::Output))
                for (auto member : IR::Fragment(state_, id))
                    if (!roomOf_.contains(member) || roomOf_.at(member) != room)
                        throw IR::LayoutFailure("fragment crosses production custody boundary");
        }
        if (operation.kind == IR::Kind::Retrieve) {
            if (!state_.stored.contains(operation.index))
                throw IR::Error("production cache slot is empty");
            const auto& fragment = state_.stored.at(operation.index);
            for (auto id : fragment)
                if (!roomOf_.contains(id) || roomOf_.at(id) != room)
                    throw IR::LayoutFailure("buffered fragment belongs to another room");
        }
    }
    auto program = program_;
    auto next = state_;
    program.Append(puzzle_, next, operation);
    if (productCells && next.nextId - state_.nextId != productCells->size())
        throw IR::Error("reaction product placement dimension mismatch");
    if (productCells)
        for (std::size_t index = 0; index < productCells->size(); ++index) {
            const auto id = state_.nextId + index;
            const auto translation =
                core::Subtract((*productCells)[index], next.atoms.at(id).position);
            program.Append(puzzle_, next, {IR::Kind::Relocate, 0, {id}, 0, 0, translation});
        }
    auto owners = roomOf_;
    for (const auto& [id, atom] : state_.atoms)
        if (!next.atoms.contains(id)) owners.erase(id);
    for (const auto& [id, atom] : next.atoms)
        if (!state_.atoms.contains(id)) owners.emplace(id, room);
    CheckRooms(next, owners, pending_);
    program_ = std::move(program);
    state_ = std::move(next);
    roomOf_ = std::move(owners);
}
void ProductionExecution::Apply(std::size_t room, const IR::Operation& operation) {
    Commit(room, operation, nullptr);
}
void ProductionExecution::React(std::size_t room, const IR::Operation& operation,
                                const std::vector<Coord>& productCells) {
    Commit(room, operation, &productCells);
}

void ProductionExecution::Release(const Transfer& transfer) {
    if (!puzzle_.productionInfo || transfer.sourceRoom >= chambers_.size() ||
        transfer.destinationRoom >= chambers_.size() ||
        transfer.sourceRoom == transfer.destinationRoom || transfer.atoms.empty() ||
        transfer.handoff.conduitIndex >= puzzle_.productionInfo->conduits.size() ||
        pending_.contains(transfer.handoff.conduitIndex) || transfer.handoff.rotation < 0 ||
        transfer.handoff.rotation >= 6 || transfer.handoff.releaseCycle < 0 ||
        transfer.arrivalCycle <= transfer.handoff.releaseCycle ||
        transfer.arrivalCycle > transfer.handoff.pickupCycle ||
        transfer.handoff.period <= transfer.handoff.pickupCycle)
        throw IR::TimingFailure("invalid or occupied production conduit handoff");
    const auto conduit = transfer.handoff.conduitIndex;
    const auto& original = puzzle_.productionInfo->conduits[conduit];
    if (transfer.handoff.localShape != original.path ||
        (conduitIds_.contains(conduit) && conduitIds_.at(conduit) != transfer.handoff.conduitId) ||
        std::any_of(conduitIds_.begin(), conduitIds_.end(),
                    [&](const auto& entry) {
                        return entry.first != conduit && entry.second == transfer.handoff.conduitId;
                    }) ||
        (lastPickup_.contains(conduit) && transfer.handoff.releaseCycle < lastPickup_.at(conduit)))
        throw IR::LayoutFailure("production conduit shape, identity or interval mismatch");
    auto endpointRoom = [&](Coord cell) -> std::optional<std::size_t> {
        for (std::size_t room = 0; room < chambers_.size(); ++room)
            if (ContainsProductionCell(chambers_[room], cell)) return room;
        return {};
    };
    const bool aligned = endpointRoom(original.positionA) == transfer.sourceRoom &&
                         endpointRoom(original.positionB) == transfer.destinationRoom;
    const bool reversed = endpointRoom(original.positionB) == transfer.sourceRoom &&
                          endpointRoom(original.positionA) == transfer.destinationRoom;
    if (!aligned && !reversed)
        throw IR::LayoutFailure("conduit endpoints do not connect the selected rooms");
    const auto fragment = IR::Fragment(state_, transfer.atoms.front());
    auto declared = transfer.atoms;
    std::sort(declared.begin(), declared.end());
    if (fragment != declared || transfer.handoff.payload.atoms.size() != fragment.size())
        throw IR::LayoutFailure("conduit release must contain one complete connected payload");
    std::map<std::pair<int, int>, AtomId> payload;
    for (const auto id : fragment) {
        if (!roomOf_.contains(id) || roomOf_.at(id) != transfer.sourceRoom)
            throw IR::LayoutFailure("conduit payload is not owned by its source room");
        const auto local =
            core::Subtract(state_.atoms.at(id).position, transfer.handoff.sourcePosition);
        payload.emplace(std::pair{local.q, local.r}, id);
    }
    if (payload.size() != fragment.size())
        throw IR::LayoutFailure("overlapping conduit payload cells");
    std::set<std::pair<int, int>> shape;
    for (auto cell : transfer.handoff.localShape) {
        const auto local = Turn(cell, transfer.handoff.rotation);
        if (!shape.emplace(local.q, local.r).second || !payload.contains({local.q, local.r}) ||
            !ContainsProductionCell(chambers_[transfer.sourceRoom],
                                    core::Add(transfer.handoff.sourcePosition, local)) ||
            !ContainsProductionCell(chambers_[transfer.destinationRoom],
                                    core::Add(transfer.handoff.destinationPosition, local)))
            throw IR::LayoutFailure("conduit ordered-cell payload or chamber footprint mismatch");
    }
    if (shape.size() != fragment.size())
        throw IR::LayoutFailure("conduit path omits payload cells");
    std::set<AtomId> payloadIdentities;
    for (const auto& atom : transfer.handoff.payload.atoms) {
        const auto found = payload.find({atom.position.q, atom.position.r});
        if (found == payload.end() || state_.atoms.at(found->second).type != atom.type ||
            !payloadIdentities.insert(found->second).second)
            throw IR::LayoutFailure("conduit atom identity or type differs from material ledger");
    }
    std::map<IR::Edge, std::uint8_t> bonds;
    for (const auto& bond : transfer.handoff.payload.bonds) {
        const auto a = payload.find({bond.from.q, bond.from.r});
        const auto b = payload.find({bond.to.q, bond.to.r});
        if (a == payload.end() || b == payload.end() ||
            !bonds
                 .emplace(IR::Edge{std::min(a->second, b->second), std::max(a->second, b->second)},
                          bond.bits)
                 .second)
            throw IR::LayoutFailure("conduit bond endpoint is absent or duplicated");
    }
    for (const auto& [edge, mask] : state_.bonds)
        if (std::binary_search(fragment.begin(), fragment.end(), edge.a)) {
            if (!bonds.contains(edge) || bonds.at(edge) != mask)
                throw IR::LayoutFailure("conduit release changed a bond mask");
            bonds.erase(edge);
        }
    if (!bonds.empty()) throw IR::LayoutFailure("conduit payload adds an unknown bond");
    for (const auto id : fragment) {
        const auto local =
            core::Subtract(state_.atoms.at(id).position, transfer.handoff.sourcePosition);
        const auto target = core::Add(transfer.handoff.destinationPosition, local);
        for (const auto& [other, room] : roomOf_)
            if (!std::binary_search(fragment.begin(), fragment.end(), other) &&
                state_.atoms.at(other).position == target)
                throw IR::LayoutFailure("destination conduit arrival footprint is occupied");
    }
    auto owners = roomOf_;
    for (const auto id : fragment)
        owners.erase(id);
    auto transfers = pending_;
    transfers.emplace(conduit, Pending{transfer, false});
    CheckRooms(state_, owners, transfers);
    pending_ = std::move(transfers);
    roomOf_ = std::move(owners);
    conduitIds_[conduit] = transfer.handoff.conduitId;
}
void ProductionExecution::Arrive(std::size_t conduit, int cycle) {
    auto pending = pending_.find(conduit);
    if (pending == pending_.end() || pending->second.arrived ||
        cycle != pending->second.transfer.arrivalCycle)
        throw IR::TimingFailure("premature or duplicate conduit arrival");
    const auto& handoff = pending->second.transfer.handoff;
    auto program = program_;
    auto next = state_;
    program.Append(puzzle_, next,
                   {IR::Kind::Relocate,
                    0,
                    {pending->second.transfer.atoms.front()},
                    0,
                    0,
                    core::Subtract(handoff.destinationPosition, handoff.sourcePosition)});
    auto transfers = pending_;
    transfers.at(conduit).arrived = true;
    CheckRooms(next, roomOf_, transfers);
    program_ = std::move(program);
    state_ = std::move(next);
    pending_ = std::move(transfers);
}
void ProductionExecution::Pickup(std::size_t conduit, int cycle) {
    auto pending = pending_.find(conduit);
    if (pending == pending_.end() || !pending->second.arrived ||
        cycle != pending->second.transfer.handoff.pickupCycle)
        throw IR::TimingFailure("premature or duplicate conduit pickup");
    auto owners = roomOf_;
    for (auto id : pending->second.transfer.atoms)
        owners.emplace(id, pending->second.transfer.destinationRoom);
    auto transfers = pending_;
    transfers.erase(conduit);
    CheckRooms(state_, owners, transfers);
    lastPickup_[conduit] = cycle;
    roomOf_ = std::move(owners);
    pending_ = std::move(transfers);
}
void ProductionExecution::RequireComplete(const std::vector<std::size_t>& outputCounts) const {
    if (!pending_.empty() || !roomOf_.empty())
        throw IR::Error("production period retains room or conduit material");
    IR::RequireComplete(state_, outputCounts);
    if (state_ != program_.Replay(puzzle_)) throw IR::Error("production material replay mismatch");
}
namespace {
void PlaceOnPipe(ProductionExecution& ledger, std::size_t room,
                 const ProductionConduitHandoff& handoff, const std::vector<IR::AtomId>& ids) {
    for (int rotation = 0; rotation < 6; ++rotation) {
        const auto desired =
            core::Add(handoff.sourcePosition, handoff.payload.atoms.at(0).position);
        const auto translation =
            core::Subtract(desired, Turn(ledger.Material().atoms.at(ids.at(0)).position, rotation));
        bool fits = true;
        for (std::size_t i = 0; i < ids.size(); ++i)
            fits &= ledger.Material().atoms.at(ids[i]).type == handoff.payload.atoms.at(i).type &&
                    core::Add(Turn(ledger.Material().atoms.at(ids[i]).position, rotation),
                              translation) ==
                        core::Add(handoff.sourcePosition, handoff.payload.atoms[i].position);
        if (!fits) continue;
        ledger.Apply(room, {IR::Kind::Relocate, 0, {ids.front()}, 0, rotation, translation});
        return;
    }
    throw IR::LayoutFailure("transport source pose cannot reach pipe payload");
}
void TransferPayload(ProductionExecution& ledger, const ProductionConduitHandoff& handoff,
                     std::size_t source, std::size_t destination,
                     const std::vector<IR::AtomId>& ids) {
    ProductionExecution::Transfer transfer;
    transfer.handoff = handoff;
    transfer.sourceRoom = source;
    transfer.destinationRoom = destination;
    transfer.arrivalCycle = handoff.releaseCycle + 1;
    transfer.atoms = ids;
    ledger.Release(transfer);
    ledger.Arrive(handoff.conduitIndex, transfer.arrivalCycle);
    ledger.Pickup(handoff.conduitIndex, handoff.pickupCycle);
}
void FinishRigidOutput(ProductionExecution& ledger, const io::PuzzleData& puzzle, std::size_t room,
                       core::HexFrame product, IR::AtomId representative) {
    const auto& output = puzzle.outputs.at(0);
    for (int rotation = 0; rotation < 6; ++rotation)
        for (const auto& anchor : output.atoms) {
            if (anchor.type != ledger.Material().atoms.at(representative).type) continue;
            const auto translation =
                core::Subtract(product.ToWorld(anchor.position),
                               Turn(ledger.Material().atoms.at(representative).position, rotation));
            bool fits = true;
            for (const auto& [id, atom] : ledger.Material().atoms)
                fits &=
                    std::any_of(output.atoms.begin(), output.atoms.end(), [&](const auto& target) {
                        return target.type == atom.type &&
                               product.ToWorld(target.position) ==
                                   core::Add(Turn(atom.position, rotation), translation);
                    });
            if (!fits) continue;
            auto candidate = ledger;
            try {
                candidate.Apply(
                    room, {IR::Kind::Relocate, 0, {representative}, 0, rotation, translation});
                candidate.Apply(room, {IR::Kind::Output, 0, {representative}});
                candidate.RequireComplete({1});
                return;
            } catch (const IR::Error&) {
            }
        }
    throw IR::LayoutFailure("transport destination cannot match output port");
}
} // namespace

void ValidateProductionTransportMaterial(const io::PuzzleData& puzzle,
                                         const ProductionTransportPlan& plan) {
    ProductionExecution ledger(puzzle);
    const auto& input = puzzle.inputs.at(0);
    const auto& output = puzzle.outputs.at(0);
    const core::HexFrame feed{plan.inputPosition, plan.inputRotation};
    const core::HexFrame product{plan.outputPosition, plan.outputRotation};
    ledger.Apply(plan.source.chamberIndex,
                 {IR::Kind::Acquire, 0, {}, 0, feed.Turns(), feed.origin});
    std::vector<IR::AtomId> ids;
    for (std::size_t index = 0; index < input.atoms.size(); ++index)
        ids.push_back(index + 1);
    PlaceOnPipe(ledger, plan.source.chamberIndex, plan.handoff, ids);
    TransferPayload(ledger, plan.handoff, plan.source.chamberIndex, plan.destination.chamberIndex,
                    ids);
    auto representative = IR::AtomId{1};
    if (input.atoms.size() == 1 && input.atoms[0].type != output.atoms[0].type) {
        const auto productId = ledger.Material().nextId;
        ledger.React(
            plan.destination.chamberIndex, {IR::Kind::Calcify, 0, {representative}},
            {core::Add(plan.handoff.destinationPosition, plan.handoff.payload.atoms[0].position)});
        representative = productId;
    }
    FinishRigidOutput(ledger, puzzle, plan.destination.chamberIndex, product, representative);
}
void ValidateProductionRouteMaterial(const io::PuzzleData& puzzle, const ProductionRoutePlan& plan,
                                     int rotation) {
    if (plan.handoffs.empty() || plan.handoffs.size() + 1 != plan.chambers.size() || rotation < 0 ||
        rotation >= 6)
        throw IR::LayoutFailure("production route has no coherent room path");
    ProductionExecution ledger(puzzle);
    const auto place = [&](std::size_t room, Coord position) {
        const auto center = plan.chambers[room].center;
        return core::Add(center, Turn(core::Subtract(position, center), rotation));
    };
    const bool singleton = puzzle.inputs.at(0).atoms.size() == 1;
    const core::HexFrame inputFrame =
        singleton
            ? core::HexFrame{core::Subtract(
                                 place(0, core::Add(plan.chambers.front().center, {1, 0})),
                                 Turn(puzzle.inputs[0].atoms[0].position, rotation)),
                             rotation}
            : core::HexFrame{place(0, plan.inputPosition), (plan.inputRotation + rotation) % 6};
    const core::HexFrame outputFrame =
        singleton
            ? core::HexFrame{core::Subtract(place(plan.chambers.size() - 1,
                                                  core::Add(plan.chambers.back().center, {-1, 1})),
                                            Turn(puzzle.outputs[0].atoms[0].position, rotation)),
                             rotation}
            : core::HexFrame{place(plan.chambers.size() - 1, plan.outputPosition),
                             (plan.outputRotation + rotation) % 6};
    ledger.Apply(plan.chambers.front().chamberIndex,
                 {IR::Kind::Acquire, 0, {}, 0, inputFrame.Turns(), inputFrame.origin});
    std::vector<IR::AtomId> ids;
    for (std::size_t i = 0; i < puzzle.inputs.at(0).atoms.size(); ++i)
        ids.push_back(i + 1);
    for (std::size_t hop = 0; hop < plan.handoffs.size(); ++hop) {
        auto handoff = plan.handoffs[hop];
        handoff.sourcePosition = place(hop, handoff.sourcePosition);
        handoff.destinationPosition = place(hop + 1, handoff.destinationPosition);
        handoff.rotation = (handoff.rotation + rotation) % 6;
        for (auto& atom : handoff.payload.atoms)
            atom.position = Turn(atom.position, rotation);
        for (auto& bond : handoff.payload.bonds) {
            bond.from = Turn(bond.from, rotation);
            bond.to = Turn(bond.to, rotation);
        }
        PlaceOnPipe(ledger, plan.chambers[hop].chamberIndex, handoff, ids);
        TransferPayload(ledger, handoff, plan.chambers[hop].chamberIndex,
                        plan.chambers[hop + 1].chamberIndex, ids);
    }
    FinishRigidOutput(ledger, puzzle, plan.chambers.back().chamberIndex, outputFrame, ids.front());
}
void ValidateProductionCoproductMaterial(const io::PuzzleData& puzzle,
                                         const ProductionTransportPlan& plan,
                                         const std::vector<int>& owner,
                                         const std::vector<io::SolutionObject>& ports) {
    if (puzzle.inputs.size() != 1 || owner.size() != puzzle.inputs[0].atoms.size() ||
        plan.handoff.payload.atoms.size() != owner.size() || puzzle.outputs.empty())
        throw IR::LayoutFailure("production coproduct material dimensions mismatch");
    ProductionExecution ledger(puzzle);
    ledger.Apply(plan.source.chamberIndex,
                 {IR::Kind::Acquire, 0, {}, 0, plan.inputRotation, plan.inputPosition});
    std::vector<IR::AtomId> ids(owner.size());
    for (std::size_t i = 0; i < ids.size(); ++i)
        ids[i] = i + 1;
    PlaceOnPipe(ledger, plan.source.chamberIndex, plan.handoff, ids);
    TransferPayload(ledger, plan.handoff, plan.source.chamberIndex, plan.destination.chamberIndex,
                    ids);
    const auto destination = plan.destination.chamberIndex;
    std::vector<std::vector<std::size_t>> mapped(puzzle.outputs.size());
    for (std::size_t output = 0; output < puzzle.outputs.size(); ++output) {
        const auto port = std::find_if(ports.begin(), ports.end(), [&](const auto& object) {
            return object.typeName == "out-std" && object.bodyData.size() >= 3 &&
                   object.bodyData[2] == static_cast<int>(output);
        });
        if (port == ports.end()) throw IR::LayoutFailure("coproduct output port is absent");
        const core::HexFrame frame{port->position, port->bodyData.at(1)};
        for (const auto& atom : puzzle.outputs[output].atoms) {
            const auto target = frame.ToWorld(atom.position);
            const auto source = std::find_if(
                plan.handoff.payload.atoms.begin(), plan.handoff.payload.atoms.end(),
                [&](const auto& value) {
                    return core::Add(plan.handoff.destinationPosition, value.position) == target;
                });
            if (source == plan.handoff.payload.atoms.end())
                throw IR::LayoutFailure("coproduct port has no transported atom");
            const auto index =
                static_cast<std::size_t>(source - plan.handoff.payload.atoms.begin());
            if (owner[index] != static_cast<int>(output) ||
                std::find(mapped[output].begin(), mapped[output].end(), index) !=
                    mapped[output].end())
                throw IR::LayoutFailure("coproduct atom is owned by another output");
            mapped[output].push_back(index);
        }
    }
    std::map<IR::Edge, std::uint8_t> targetMasks;
    std::set<std::size_t> convert;
    for (std::size_t output = 0; output < puzzle.outputs.size(); ++output) {
        const auto& molecule = puzzle.outputs[output];
        for (std::size_t i = 0; i < molecule.atoms.size(); ++i) {
            const auto index = mapped[output][i];
            const auto original = ledger.Material().atoms.at(ids[index]).type;
            const auto target = molecule.atoms[i].type;
            if (original != target) {
                if (target != core::AtomType::Salt || original < core::AtomType::Air ||
                    original > core::AtomType::Water)
                    throw IR::Unsupported("coproduct conversion is not supported by static ledger");
                convert.insert(index);
            }
        }
        for (const auto& bond : molecule.bonds) {
            const auto at = [&](Coord cell) {
                return std::find_if(molecule.atoms.begin(), molecule.atoms.end(),
                                    [&](const auto& atom) { return atom.position == cell; }) -
                       molecule.atoms.begin();
            };
            const auto a = at(bond.from), b = at(bond.to);
            if (a >= molecule.atoms.size() || b >= molecule.atoms.size())
                throw IR::LayoutFailure("coproduct bond references missing atom");
            const auto x = ids[mapped[output][a]], y = ids[mapped[output][b]];
            targetMasks[{std::min(x, y), std::max(x, y)}] = bond.bits;
        }
    }
    const auto initialBonds = ledger.Material().bonds;
    for (const auto& [edge, mask] : initialBonds) {
        const auto required = targetMasks.contains(edge) ? targetMasks.at(edge) : 0;
        const bool react = convert.contains(edge.a - 1) || convert.contains(edge.b - 1);
        if (required == mask && !react) continue;
        ledger.Apply(destination, {IR::Kind::Unbond, 0, {edge.a, edge.b}, mask});
    }
    for (const auto index : convert) {
        const auto original = ids[index];
        const auto cell = ledger.Material().atoms.at(original).position;
        const auto fresh = ledger.Material().nextId;
        ledger.React(destination, {IR::Kind::Calcify, 0, {original}}, {cell});
        ids[index] = fresh;
    }
    for (const auto& [edge, mask] : targetMasks) {
        const auto a = ids.at(edge.a - 1), b = ids.at(edge.b - 1);
        const IR::Edge resolved{std::min(a, b), std::max(a, b)};
        const auto existing =
            ledger.Material().bonds.contains(resolved) ? ledger.Material().bonds.at(resolved) : 0;
        if ((existing & ~mask) != 0)
            throw IR::LayoutFailure("coproduct target retains incompatible bond colors");
        if (const auto missing = static_cast<std::uint8_t>(mask & ~existing); missing)
            ledger.Apply(destination, {IR::Kind::Bond, 0, {a, b}, missing});
    }
    for (std::size_t output = 0; output < puzzle.outputs.size(); ++output)
        ledger.Apply(destination, {IR::Kind::Output, output, {ids[mapped[output].front()]}});
    ledger.RequireComplete(std::vector<std::size_t>(puzzle.outputs.size(), 1));
}
} // namespace opusoptima::solvers
