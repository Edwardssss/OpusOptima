#include "FragmentCustody.hpp"
#include "RowPhaseProfile.hpp"

#include <algorithm>
#include <set>

namespace opusoptima::solvers {
namespace {
using IR = ExecutionIR;
bool Contains(const std::vector<IR::AtomId>& ids, IR::AtomId id) {
    return std::find(ids.begin(), ids.end(), id) != ids.end();
}
std::map<IR::Edge, std::uint8_t> Bonds(const IR::State& state, const std::vector<IR::AtomId>& ids) {
    std::map<IR::Edge, std::uint8_t> result;
    for (const auto& [edge, bits] : state.bonds)
        if (Contains(ids, edge.a) || Contains(ids, edge.b)) result.emplace(edge, bits);
    return result;
}
} // namespace

void FragmentCustody::Validate(const IR::State& state) const {
    row_profile::Scope profile(row_profile::Custody, state.atoms.size());
    std::set<std::pair<int, int>> occupied;
    for (const auto& [id, atom] : state.atoms)
        if (!occupied.emplace(atom.position.q, atom.position.r).second)
            throw IR::Error("physical material overlaps at custody boundary for atom " +
                            std::to_string(id) + " at (" + std::to_string(atom.position.q) + "," +
                            std::to_string(atom.position.r) + ")");
    for (const auto& [arm, id] : held_) {
        if (!state.atoms.contains(id)) throw IR::Error("held atom was consumed or removed");
        for (const auto& [slot, ids] : state.stored)
            if (Contains(ids, id)) throw IR::Error("held atom is logically stored");
    }
    std::set<IR::AtomId> parkedAtoms;
    for (const auto& [slot, parking] : parked_) {
        for (auto id : parking.atoms) {
            if (!parkedAtoms.insert(id).second || !state.atoms.contains(id) ||
                state.atoms.at(id) != parking.material.at(id))
                throw IR::Error("parked material moved, changed or disappeared");
            for (const auto& [arm, held] : held_)
                if (held == id) throw IR::Error("parked material is still held");
        }
        if (IR::Fragment(state, parking.atoms.front()) != parking.atoms ||
            Bonds(state, parking.atoms) != parking.bonds)
            throw IR::Error("parked fragment connectivity changed");
    }
}

void FragmentCustody::MergeParked(const IR::State& before, const IR::State& after, std::size_t arm,
                                  std::size_t slot) {
    Validate(before);
    auto unchanged = after;
    unchanged.bonds = before.bonds;
    if (!held_.contains(arm) || !parked_.contains(slot) || unchanged != before ||
        before.bonds.size() + 1 != after.bonds.size())
        throw IR::Error("parking merge requires one added bond and unchanged physical stock");
    const auto held = IR::Fragment(before, held_.at(arm));
    const auto& parked = parked_.at(slot).atoms;
    for (const auto& [edge, bits] : before.bonds) {
        const auto it = after.bonds.find(edge);
        if (it == after.bonds.end() || it->second != bits)
            throw IR::Error("parking merge cannot alter existing bonds");
    }
    for (const auto& [edge, bits] : after.bonds) {
        if (before.bonds.contains(edge)) continue;
        if (!((Contains(held, edge.a) && Contains(parked, edge.b)) ||
              (Contains(held, edge.b) && Contains(parked, edge.a))))
            throw IR::Error("parking merge must join the selected held and parked fragments");
    }
    auto next = *this;
    next.parked_.erase(slot);
    next.Validate(after);
    *this = std::move(next);
}

void FragmentCustody::ConsumeSingletons(const IR::State& before, const IR::State& after,
                                        const std::vector<AtomId>& consumed) {
    Validate(before);
    std::set<AtomId> unique;
    auto next = *this;
    for (auto id : consumed) {
        if (!unique.insert(id).second || !before.atoms.contains(id) ||
            IR::Fragment(before, id).size() != 1 || after.atoms.contains(id))
            throw IR::Error("custody consumption requires removed isolated atoms");
        for (auto it = next.held_.begin(); it != next.held_.end();) {
            if (it->second == id)
                it = next.held_.erase(it);
            else
                ++it;
        }
        for (auto it = next.parked_.begin(); it != next.parked_.end();) {
            if (Contains(it->second.atoms, id))
                it = next.parked_.erase(it);
            else
                ++it;
        }
    }
    for (const auto& [id, atom] : before.atoms)
        if (!unique.contains(id) && (!after.atoms.contains(id) || after.atoms.at(id) != atom))
            throw IR::Error("custody consumption changed unrelated material");
    if (before.bonds != after.bonds || before.stored != after.stored)
        throw IR::Error("custody consumption changed connectivity or logical storage");
    next.Validate(after);
    *this = std::move(next);
}

void FragmentCustody::ProjectHeld(const IR::State& before, const IR::State& after, std::size_t arm,
                                  AtomId metal, AtomId mercury, AtomId product) {
    Validate(before);
    if (!held_.contains(arm) || held_.at(arm) != metal || !before.atoms.contains(mercury) ||
        IR::Fragment(before, mercury).size() != 1 || product != before.nextId ||
        !after.atoms.contains(product))
        throw IR::Error("projection custody requires held endpoint and isolated reagent");
    auto expected = before;
    const auto old = before.atoms.at(metal);
    expected.atoms.erase(metal);
    expected.atoms.erase(mercury);
    expected.atoms.emplace(
        product,
        IR::Atom{static_cast<core::AtomType>(static_cast<int>(old.type) - 1), old.position});
    ++expected.nextId;
    expected.bonds.clear();
    for (const auto& [original, bits] : before.bonds) {
        auto edge = original;
        if (edge.a == metal) edge.a = product;
        if (edge.b == metal) edge.b = product;
        expected.bonds.emplace(IR::Edge{std::min(edge.a, edge.b), std::max(edge.a, edge.b)}, bits);
    }
    if (expected != after)
        throw IR::Error("projection changed unrelated material or fragment geometry");
    auto next = *this;
    bool reagentParked = false;
    for (auto it = next.parked_.begin(); it != next.parked_.end();) {
        if (it->second.atoms == std::vector<AtomId>{mercury}) {
            reagentParked = true;
            it = next.parked_.erase(it);
        } else
            ++it;
    }
    if (!reagentParked) throw IR::Error("projection reagent lacks parked custody");
    next.held_.at(arm) = product;
    next.Validate(after);
    *this = std::move(next);
}

void FragmentCustody::Grab(const IR::State& state, std::size_t arm, core::HexCoord tip, AtomId id) {
    Validate(state);
    if (held_.contains(arm) || !state.atoms.contains(id) || state.atoms.at(id).position != tip)
        throw IR::Error("grab requires a free arm in contact with the selected atom");
    for (const auto& [slot, ids] : state.stored)
        if (Contains(ids, id)) throw IR::Error("grab requires retrieved logical stock");
    for (const auto& [slot, parking] : parked_)
        if (Contains(parking.atoms, id))
            throw IR::Error("parked fragment requires explicit retrieval");
    held_.emplace(arm, id);
}

void FragmentCustody::Drop(std::size_t arm) {
    if (!held_.erase(arm)) throw IR::Error("cannot drop an empty arm");
}

void FragmentCustody::Park(const IR::State& state, std::size_t arm, std::size_t slot) {
    Validate(state);
    if (!held_.contains(arm) || parked_.contains(slot))
        throw IR::Error("parking requires a held fragment and a free slot");
    Parking parking;
    parking.atoms = IR::Fragment(state, held_.at(arm));
    for (const auto& [other, id] : held_)
        if (other != arm && Contains(parking.atoms, id))
            throw IR::Error("parking requires releasing other grippers first");
    for (auto id : parking.atoms)
        parking.material.emplace(id, state.atoms.at(id));
    parking.bonds = Bonds(state, parking.atoms);
    parked_.emplace(slot, std::move(parking));
    held_.erase(arm);
}

void FragmentCustody::ReserveLoose(const IR::State& state, AtomId id, std::size_t slot) {
    Validate(state);
    if (!state.atoms.contains(id) || parked_.contains(slot))
        throw IR::Error("loose reservation requires an existing fragment and free slot");
    Parking parking;
    parking.atoms = IR::Fragment(state, id);
    for (const auto& [arm, held] : held_)
        if (Contains(parking.atoms, held))
            throw IR::Error("loose reservation cannot capture held material");
    for (const auto& [otherSlot, other] : parked_)
        if (Contains(other.atoms, id)) throw IR::Error("loose fragment already reserved");
    for (auto atom : parking.atoms)
        parking.material.emplace(atom, state.atoms.at(atom));
    parking.bonds = Bonds(state, parking.atoms);
    parked_.emplace(slot, std::move(parking));
}

void FragmentCustody::Retrieve(const IR::State& state, std::size_t slot, std::size_t arm,
                               core::HexCoord tip, AtomId id) {
    Validate(state);
    const auto it = parked_.find(slot);
    if (held_.contains(arm) || it == parked_.end() || !Contains(it->second.atoms, id) ||
        state.atoms.at(id).position != tip)
        throw IR::Error("retrieval requires a free arm touching the reserved fragment");
    for (const auto& [logicalSlot, ids] : state.stored)
        if (Contains(ids, id)) throw IR::Error("retrieval requires released logical stock");
    held_.emplace(arm, id);
    parked_.erase(it);
}

} // namespace opusoptima::solvers
