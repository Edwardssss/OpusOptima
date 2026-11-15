#pragma once
#include "BinaryParser.hpp"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <map>
#include <set>
#include <string_view>
#include <utility>
#include <vector>

namespace opusoptima::solvers::detail {
// Called only after LifeDeath's output filters and coordinate/arm remapping.
inline void AppendLifeDeathObject(io::SolutionData& result, io::SolutionObject object,
                                  bool storage, std::vector<std::size_t>& storageRails) {
    if (storage) storageRails.push_back(result.objects.size());
    result.objects.push_back(std::move(object));
}

// Independent of the overlap-rejecting local envelope and parking proofs.
// The caller supplies IDs in the FINAL serialized object order. No tapes,
// origins, arm bodies or non-storage objects are changed. Native remains the gate.
inline bool TrimLifeDeathStorage(io::SolutionData& machine,
                                 const std::vector<std::size_t>& storage) {
    struct Diagnostic {
        bool enabled{false};
        std::chrono::steady_clock::time_point start{};
        std::size_t graphBuilds{}, cells{}, instructions{}, moves{}, directions{};
        bool changed{};
        Diagnostic() {
            const auto* flag = std::getenv("OPUSOPTIMA_STORAGE_DIAG");
            enabled = flag && std::string_view(flag) == "1";
            if (enabled) start = std::chrono::steady_clock::now();
        }
        ~Diagnostic() {
            if (!enabled) return;
            const auto micros = std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now() - start).count();
            std::fprintf(stderr, "STORAGEDIAG elapsed_us=%lld builds=%zu cells=%zu "
                         "instructions=%zu moves=%zu directions=%zu changed=%d\n",
                         static_cast<long long>(micros), graphBuilds, cells, instructions,
                         moves, directions, int(changed));
        }
    } diagnostic;
    using Cell = std::pair<std::int64_t, std::int64_t>;
    using Graph = std::map<Cell, std::pair<Cell, Cell>>; // plus/minus destinations
    if (storage.empty()) return false;
    const auto fits = [](Cell p) {
        return p.first >= INT32_MIN && p.first <= INT32_MAX &&
               p.second >= INT32_MIN && p.second <= INT32_MAX;
    };
    const auto adjacent = [](Cell a, Cell b) {
        const auto q = a.first - b.first, r = a.second - b.second;
        return std::max({std::abs(q), std::abs(r), std::abs(q + r)}) == 1;
    };
    std::vector<std::vector<Cell>> rails(machine.objects.size());
    std::set<std::size_t> selected;
    for (auto id : storage)
        if (id >= rails.size() || machine.objects[id].typeName != "track" ||
            !selected.insert(id).second) return false;
    const auto build = [&](const io::SolutionData& source, Graph& graph,
                           std::vector<std::vector<Cell>>& cells) {
        if (diagnostic.enabled) ++diagnostic.graphBuilds;
        for (std::size_t id = source.objects.size(); id-- > 0;) {
            const auto& o = source.objects[id];
            if (o.typeName != "track") continue;
            const auto& b = o.bodyData;
            if (!o.instructions.empty() || b.size() < 8 || b[4] < 1 || b[4] > 9999 ||
                b.size() != 6 + 2 * static_cast<std::size_t>(b[4])) return false;
            // Native computes last-first in int32 even for an open rail.
            if (!fits({std::int64_t{b[3 + 2 * b[4]]} - b[5],
                       std::int64_t{b[4 + 2 * b[4]]} - b[6]})) return false;
            auto& rail = cells[id];
            std::set<Cell> unique;
            for (int i = 0; i < b[4]; ++i) {
                if (diagnostic.enabled) ++diagnostic.cells;
                Cell p{std::int64_t{o.position.q} + b[5 + 2 * i],
                       std::int64_t{o.position.r} + b[6 + 2 * i]};
                // sim.c lookup_track treats this WORLD coordinate as an empty
                // hash slot. It is not a restriction on representable deltas.
                if (!fits(p) || p == Cell{INT32_MIN, INT32_MIN} ||
                    !unique.insert(p).second ||
                    (!rail.empty() && !adjacent(rail.back(), p))) return false;
                rail.push_back(p);
            }
            if (rail.size() > 2 && adjacent(rail.front(), rail.back())) return false;
            // decode.c: reverse objects, reverse cells, plus[p] followed by
            // minus[last], including BOTH endpoint zero writes. Rotation ignored.
            Cell last = rail.back();
            for (std::size_t i = rail.size(); i-- > 0;) {
                graph[rail[i]].first = last;
                graph[last].second = rail[i];
                last = rail[i];
            }
            graph[last].second = last;
        }
        return true;
    };
    Graph original;
    if (!build(machine, original, rails)) return false;
    std::map<Cell, std::vector<std::pair<std::size_t, int>>> membership;
    std::map<std::size_t, std::pair<int, int>> bounds;
    std::map<std::size_t, std::pair<int, int>> ownBounds;
    for (auto id : storage) {
        bounds[id] = {static_cast<int>(rails[id].size()), -1};
        ownBounds[id] = bounds[id];
        for (std::size_t i = 0; i < rails[id].size(); ++i)
            membership[rails[id][i]].push_back({id, static_cast<int>(i)});
    }
    // Both are only proposals. The own envelope includes ALL arms initially on
    // that rail; the fallback also includes users entering through overlaps.
    const auto visit = [&](Cell p, const std::set<std::size_t>& owners) {
        const auto found = membership.find(p);
        if (found == membership.end()) return;
        for (const auto& [id, index] : found->second) {
            auto& [lo, hi] = bounds[id];
            lo = std::min(lo, index);
            hi = std::max(hi, index);
            if (owners.contains(id)) {
                auto& [ownLo, ownHi] = ownBounds[id];
                ownLo = std::min(ownLo, index);
                ownHi = std::max(ownHi, index);
            }
        }
    };
    // A deterministic halted tape only observes these directed edges. Keeping
    // each observed direction once is equivalent to replaying every occurrence:
    // induction on A/a preserves the base position; other actions do not move it.
    std::map<Cell, unsigned> usedDirections;
    const auto replay = [&]() {
        for (const auto& o : machine.objects) {
            const bool arm = o.typeName == "arm1" || o.typeName == "arm2" ||
                o.typeName == "arm3" || o.typeName == "arm6" ||
                o.typeName == "piston" || o.typeName == "baron";
            if (!arm) {
                if (o.typeName == "ravari" || !o.instructions.empty()) return false;
                continue;
            }
            if (o.bodyData.size() != 5 ||
                o.bodyData[0] < 1 || o.bodyData[0] > 3 ||
                o.bodyData[1] < 0 || o.bodyData[1] > 5 || o.bodyData[4] < 0) return false;
            std::set<std::size_t> owners;
            {
                const auto initial = membership.find({o.position.q, o.position.r});
                if (initial != membership.end())
                    for (const auto& [id, index] : initial->second) owners.insert(id);
            }
            // An empty native tape never executes a movement or repeats/reset.
            // Keep its stationary base in every containing storage envelope.
            if (o.instructions.empty()) {
                visit({o.position.q, o.position.r}, owners);
                continue;
            }
            const std::int64_t first = o.instructions.front().parameter;
            const std::int64_t length = std::int64_t{o.instructions.back().parameter} - first + 1;
            if (first < 0 || length < 1 || length > 100000 ||
                first + 2 * length > INT32_MAX) return false;
            std::int64_t previous = -1;
            bool halted = false;
            Cell p{o.position.q, o.position.r};
            visit(p, owners);
            for (const auto& ins : o.instructions) {
                if (diagnostic.enabled) ++diagnostic.instructions;
                if (ins.parameter <= previous ||
                    std::string_view("GgAaEeRrPpB ").find(ins.action) == std::string_view::npos)
                    return false;
                previous = ins.parameter;
                if (halted) continue; // suffix still checked, never removed
                if (ins.action == 'B') { halted = true; continue; }
                if (ins.action != 'A' && ins.action != 'a') continue;
                if (diagnostic.enabled) ++diagnostic.moves;
                const auto edge = original.find(p);
                if (edge == original.end()) return false;
                const Cell next = ins.action == 'A' ? edge->second.first : edge->second.second;
                if (!fits(next)) return false;
                usedDirections[p] |= ins.action == 'A' ? 1U : 2U;
                visit(p, owners); visit(next, owners);
                p = next;
            }
            if (!halted) return false; // no repeat/reset or unproved loop
        }
        return true;
    };
    if (!replay()) return false;
    if (diagnostic.enabled) diagnostic.directions = usedDirections.size();
    const auto attempt = [&](const auto& proposal) {
        const bool canShorten = std::any_of(storage.begin(), storage.end(), [&](auto id) {
            const auto [lo, hi] = proposal.at(id);
            return hi >= lo && (lo > 0 || hi + 1 < machine.objects[id].bodyData[4]);
        });
        if (!canShorten) return false;
        auto trial = machine;
        for (auto id : storage) {
            const auto [lo, hi] = proposal.at(id);
            if (hi < lo) continue; // no initial/proven user: retain entire rail
            const auto& old = machine.objects[id].bodyData;
            auto& b = trial.objects[id].bodyData;
            b.assign(old.begin(), old.begin() + 5);
            b[4] = hi - lo + 1;
            b.insert(b.end(), old.begin() + 5 + 2 * lo, old.begin() + 7 + 2 * hi);
            b.push_back(old.back());
        }
        Graph shortened;
        std::vector<std::vector<Cell>> trialRails(rails.size());
        if (!build(trial, shortened, trialRails)) return false;
        for (const auto& [p, directions] : usedDirections) {
            const auto edge = shortened.find(p);
            if (edge == shortened.end()) return false;
            const auto& expected = original.at(p);
            if (((directions & 1U) && edge->second.first != expected.first) ||
                ((directions & 2U) && edge->second.second != expected.second)) return false;
        }
        machine = std::move(trial);
        return true;
    };
    const bool changed = attempt(ownBounds) || (ownBounds != bounds && attempt(bounds));
    diagnostic.changed = changed;
    return changed;
}
} // namespace opusoptima::solvers::detail