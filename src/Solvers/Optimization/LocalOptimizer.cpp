#include "LocalOptimizer.hpp"
#include "BinaryWriter.hpp"
#include "VerificationJson.hpp"
#include <algorithm>
#include <array>
#include <fstream>
#include <cstdlib>
#include <limits>
#include <map>
#include <set>
#include <string_view>

namespace opusoptima::solvers {
namespace {
bool RemoveIdleArms(io::SolutionData& candidate) {
    const auto before = candidate.objects.size();
    const auto arm = [](const auto& object) {
        return object.typeName == "arm1" || object.typeName == "arm2" ||
               object.typeName == "arm3" || object.typeName == "arm6" ||
               object.typeName == "piston";
    };
    const auto removed = std::erase_if(candidate.objects, [&](const auto& object) {
        return arm(object) && object.instructions.empty();
    });
    // Preserve existing empty-arm trials and the tape periods of active machines.
    if (!removed) {
        const bool passive = std::all_of(candidate.objects.begin(),candidate.objects.end(),[](const auto& object) {
            return std::all_of(object.instructions.begin(),object.instructions.end(),
                [](const auto& instruction) { return instruction.action == 'O' || instruction.action == ' '; });
        });
        if (passive) std::erase_if(candidate.objects,arm);
        else {
            const auto idle = [&](const auto& object) {
                return arm(object) && std::all_of(object.instructions.begin(),object.instructions.end(),
                    [](const auto& instruction) { return instruction.action == 'O' || instruction.action == ' '; });
            };
            int retainedEnd = -1;
            bool explicitTape = true;
            for (const auto& object : candidate.objects) {
                if (idle(object)) continue;
                int previous = -1;
                for (const auto& instruction : object.instructions) {
                    // Reset/repeat expansion can change the serialized tape's period.
                    if (instruction.parameter <= previous ||
                        std::string_view("GgAaEeRrPpO ").find(instruction.action) == std::string_view::npos)
                        explicitTape = false;
                    previous = instruction.parameter;
                    if (instruction.action != ' ')
                        retainedEnd = std::max(retainedEnd,instruction.parameter);
                }
            }
            if (explicitTape && retainedEnd >= 0)
                std::erase_if(candidate.objects,[&](const auto& object) {
                    return idle(object) && std::all_of(object.instructions.begin(),object.instructions.end(),
                        [&](const auto& instruction) {
                            return instruction.parameter >= 0 && instruction.parameter <= retainedEnd;
                        });
                });
        }
    }
    return candidate.objects.size() != before;
}
bool NonLoopingTrack(const io::SolutionObject& track, int count) {
    if (count <= 2) return true;
    const auto dq = static_cast<long long>(track.bodyData[5 + 2 * (count - 1)]) - track.bodyData[5];
    const auto dr = static_cast<long long>(track.bodyData[6 + 2 * (count - 1)]) - track.bodyData[6];
    return std::max({std::abs(dq), std::abs(dr), std::abs(dq + dr)}) != 1;
}
bool TrimUsedRail(io::SolutionData& candidate) {
    io::SolutionObject* arm = nullptr;
    io::SolutionObject* track = nullptr;
    for (auto& object : candidate.objects) {
        if (object.typeName == "track") {
            if (track) return false;
            track = &object;
        }
        if (object.typeName == "arm1" || object.typeName == "piston") {
            if (arm) return false;
            arm = &object;
        } else if (!object.instructions.empty()) return false;
    }
    if (!arm || !track || arm->instructions.empty() || track->bodyData.size() < 10)
        return false;
    auto& body = track->bodyData;
    const int count = body[4];
    if (count < 2 || body.size() != 6 + 2 * static_cast<std::size_t>(count) ||
        body[1] != 0 || !NonLoopingTrack(*track, count) ||
        !NonLoopingTrack(*track, count - 1)) return false;
    std::set<std::pair<int, int>> cells;
    int start = -1;
    for (int i = 0; i < count; ++i) {
        const int q = body[5 + 2 * i], r = body[6 + 2 * i];
        if (!cells.emplace(q, r).second) return false;
        if (i) {
            const auto dq = static_cast<long long>(q) - body[3 + 2 * i];
            const auto dr = static_cast<long long>(r) - body[4 + 2 * i];
            if (std::max({std::abs(dq), std::abs(dr), std::abs(dq + dr)}) != 1)
                return false;
        }
        if (static_cast<long long>(track->position.q) + q == arm->position.q &&
            static_cast<long long>(track->position.r) + r == arm->position.r) start = i;
    }
    if (start < 0) return false;
    int position = start, low = start, high = start, time = 0;
    for (const auto& instruction : arm->instructions) {
        if (instruction.parameter != time++ ||
            std::string_view("GgAaEeRrPp ").find(instruction.action) == std::string_view::npos)
            return false;
        if (instruction.action == 'A') ++position;
        if (instruction.action == 'a') --position;
        if (position < 0 || position >= count) return false;
        low = std::min(low, position);
        high = std::max(high, position);
    }
    // The legacy candidate removes the tail; the replacement must not retain it.
    if (position != start || high == count - 1) return false;
    body.erase(body.begin() + 5 + 2 * (high + 1), body.end() - 1);
    body.erase(body.begin() + 5, body.begin() + 5 + 2 * low);
    body[4] = high - low + 1;
    return NonLoopingTrack(*track, body[4]);
}
bool TrimSharedTrackEnvelopes(io::SolutionData& candidate) {
    struct Rail {
        std::size_t object;
        int count, low, high;
    };
    using Cell = std::pair<long long, long long>;
    std::map<Cell, std::pair<std::size_t, int>> cells;
    std::vector<Rail> rails;
    for (std::size_t i = 0; i < candidate.objects.size(); ++i) {
        const auto& object = candidate.objects[i];
        if (object.typeName != "track") continue;
        const auto& body = object.bodyData;
        if (!object.instructions.empty() || body.size() < 8 || body[1] != 0) return false;
        const int count = body[4];
        if (count < 1 || body.size() != 6 + 2 * static_cast<std::size_t>(count) ||
            !NonLoopingTrack(object, count)) return false;
        for (int j = 0; j < count; ++j) {
            const long long q = body[5 + 2 * j], r = body[6 + 2 * j];
            if (j) {
                const auto dq = q - body[3 + 2 * j], dr = r - body[4 + 2 * j];
                if (std::max({std::abs(dq), std::abs(dr), std::abs(dq + dr)}) != 1)
                    return false;
            }
            if (!cells.emplace(Cell{object.position.q + q, object.position.r + r},
                               std::pair{rails.size(), j}).second) return false;
        }
        rails.push_back({i, count, count, -1});
    }
    if (rails.empty()) return false;
    for (const auto& object : candidate.objects) {
        const bool arm = object.typeName == "arm1" || object.typeName == "piston";
        if (!arm) {
            if (!object.instructions.empty() || object.typeName == "arm2" ||
                object.typeName == "arm3" || object.typeName == "arm6" ||
                object.typeName == "baron") return false;
            continue;
        }
        const auto found = cells.find({object.position.q, object.position.r});
        int position = found == cells.end() ? 0 : found->second.second;
        const int start = position;
        const auto visit = [&] {
            if (found == cells.end()) return true;
            auto& rail = rails[found->second.first];
            if (position < 0 || position >= rail.count) return false;
            rail.low = std::min(rail.low, position);
            rail.high = std::max(rail.high, position);
            return true;
        };
        if (!visit()) return false;
        int previous = -1;
        for (const auto& instruction : object.instructions) {
            if (instruction.parameter <= previous ||
                std::string_view("GgAaEeRrPpOB ").find(instruction.action) == std::string_view::npos)
                return false;
            previous = instruction.parameter;
            if (instruction.action == 'A' || instruction.action == 'a') {
                if (found == cells.end()) return false;
                position += instruction.action == 'A' ? 1 : -1;
                if (!visit()) return false;
            }
        }
        // Explicit sparse tapes repeat without reset expansion; their rail positions must close.
        if (position != start) return false;
    }
    auto trimmed = candidate;
    bool changed = false;
    for (const auto& rail : rails) {
        if (rail.high < 0 || (rail.low == 0 && rail.high == rail.count - 1)) continue;
        auto& track = trimmed.objects[rail.object];
        auto& body = track.bodyData;
        body.erase(body.begin() + 5 + 2 * (rail.high + 1), body.end() - 1);
        body.erase(body.begin() + 5, body.begin() + 5 + 2 * rail.low);
        body[4] = rail.high - rail.low + 1;
        // Removing endpoints must not turn the retained path into a wrapping track.
        if (!NonLoopingTrack(track, body[4])) return false;
        changed = true;
    }
    if (changed) candidate = std::move(trimmed);
    return changed;
}
bool ParallelReturns(io::SolutionData& candidate) {
    int boundary = -1, reset = -1, start = std::numeric_limits<int>::max();
    for (const auto& object : candidate.objects) {
        int previous = -1;
        for (const auto& instruction : object.instructions) {
            if (instruction.parameter <= previous) return false;
            previous = instruction.parameter;
            if (instruction.action == 'O') {
                if (reset >= 0 && reset != instruction.parameter) return false;
                reset = instruction.parameter;
            } else if (instruction.action != 'r' && instruction.action != 'R') {
                boundary = std::max(boundary, instruction.parameter);
            }
        }
        if (!object.instructions.empty() && object.instructions.back().action != 'O')
            return false;
    }
    if (reset <= boundary) return false;
    for (const auto& object : candidate.objects) {
        bool held = false;
        for (const auto& instruction : object.instructions) {
            const auto action = instruction.action;
            if (action == 'G') held = true;
            if (action == 'g') held = false;
            if (instruction.parameter > boundary && action != 'O') {
                if (held || object.typeName != "arm1") return false;
                start = std::min(start, instruction.parameter);
            }
        }
    }
    if (start >= reset) return false;
    auto trial = candidate;
    int end = start;
    for (auto& object : trial.objects) {
        int tick = start;
        for (auto& instruction : object.instructions)
            if (instruction.parameter > boundary && instruction.action != 'O')
                instruction.parameter = tick++;
        end = std::max(end, tick);
    }
    if (end >= reset) return false;
    for (auto& object : trial.objects)
        for (auto& instruction : object.instructions)
            if (instruction.action == 'O') instruction.parameter = end;
    candidate = std::move(trial);
    return true;
}
bool CompactEmptyReturns(io::SolutionData& candidate) {
    auto arm = candidate.objects.end();
    for (auto it = candidate.objects.begin(); it != candidate.objects.end(); ++it) {
        if (it->instructions.empty()) continue;
        if (arm != candidate.objects.end() ||
            (it->typeName != "arm1" && it->typeName != "piston")) return false;
        arm = it;
    }
    if (arm == candidate.objects.end()) return false;
    for (std::size_t i = 0; i < arm->instructions.size(); ++i)
        if (arm->instructions[i].parameter != static_cast<int>(i)) return false;
    auto tape = arm->instructions;
    std::vector<io::ArmInstruction> compact;
    bool held = false;
    const auto emptyMotion = [](char action) {
        return action == 'A' || action == 'a' || action == 'E' || action == 'e' ||
               action == 'r' || action == 'R' || action == ' ';
    };
    for (std::size_t i = 0; i < tape.size();) {
        const char action = tape[i].action;
        if (action == 'G') held = true;
        if (action == 'g') held = false;
        if (held || !emptyMotion(action)) {
            compact.push_back(tape[i++]);
            continue;
        }
        const auto begin = i;
        int track = 0, extension = 0, rotation = 0;
        while (i < tape.size() && emptyMotion(tape[i].action)) {
            switch (tape[i++].action) {
            case 'A': ++track; break; case 'a': --track; break;
            case 'E': ++extension; break; case 'e': --extension; break;
            case 'R': ++rotation; break; case 'r': --rotation; break;
            default: break;
            }
        }
        rotation = (rotation % 6 + 6) % 6;
        if (rotation > 3) rotation -= 6;
        if (static_cast<std::size_t>(std::abs(track) + std::abs(extension) +
                                     std::abs(rotation)) >= i - begin) {
            compact.insert(compact.end(), tape.begin() + begin, tape.begin() + i);
            continue;
        }
        const auto append = [&](int count, char positive, char negative) {
            for (int j = 0; j < std::abs(count); ++j) {
                auto instruction = tape[begin];
                instruction.action = count > 0 ? positive : negative;
                compact.push_back(instruction);
            }
        };
        append(track, 'A', 'a'); append(extension, 'E', 'e'); append(rotation, 'R', 'r');
    }
    if (compact.size() == tape.size()) return false;
    for (std::size_t i = 0; i < compact.size(); ++i)
        compact[i].parameter = static_cast<int>(i);
    arm->instructions = std::move(compact);
    return true;
}
bool PrepositionEmptyRotations(io::SolutionData& candidate) {
    std::set<int> occupied;
    std::size_t arms = 0;
    int periodEnd = -1;
    for (const auto& object : candidate.objects) {
        if (object.instructions.empty()) continue;
        if (object.typeName != "arm1" || object.instructions.back().action != 'O') return false;
        const int end = object.instructions.back().parameter;
        if (periodEnd >= 0 && periodEnd != end) return false;
        periodEnd = end;
        ++arms;
        int previous = -1;
        bool held = false;
        for (const auto& instruction : object.instructions) {
            // O is an explicit no-op; X and C expand reset/repeat tapes.
            if (instruction.parameter <= previous ||
                std::string_view("GgRrPpAaO").find(instruction.action) == std::string_view::npos)
                return false;
            previous = instruction.parameter;
            occupied.insert(previous);
            if (instruction.action == 'G') held = true;
            if (instruction.action == 'g') held = false;
        }
        if (held) return false;
    }
    if (arms < 2) return false;
    auto trial = candidate;
    std::set<int> retained;
    for (auto& object : trial.objects) {
        int previous = -1;
        bool held = false;
        for (auto& instruction : object.instructions) {
            if (!held && (instruction.action == 'R' || instruction.action == 'r'))
                instruction.parameter = *occupied.upper_bound(previous);
            previous = instruction.parameter;
            retained.insert(previous);
            if (instruction.action == 'G') held = true;
            if (instruction.action == 'g') held = false;
        }
    }
    // Require a vacated column, beyond gaps already present in the parent.
    if (retained.size() >= occupied.size()) return false;
    const std::vector<int> times(retained.begin(), retained.end());
    for (auto& object : trial.objects)
        for (auto& instruction : object.instructions)
            instruction.parameter = static_cast<int>(
                std::lower_bound(times.begin(), times.end(), instruction.parameter) - times.begin());
    // The global rank also retimes O markers, preserving a shared explicit period.
    // Empty arms can still collide; only the whole-machine native gate admits this trial.
    candidate = std::move(trial);
    return true;
}
bool SerialReturnsOnly() {
    const auto* value = std::getenv("OPUSOPTIMA_SERIAL_RETURN");
    return value && std::string(value) == "1";
}
// v8 is part of the default queue unless the switch is exactly "off". Unset, "on"
// and every other value all mean the new default; only "off" restores the legacy
// queue and refuses the hardware pass.
bool CompactTransportEnabled() {
    const auto* value = std::getenv("OPUSOPTIMA_COMPACT_TRANSPORT");
    return !value || std::string_view(value) != "off";
}
bool MechanicalArm(const io::SolutionObject& object) {
    return object.typeName == "arm1" || object.typeName == "arm2" ||
           object.typeName == "arm3" || object.typeName == "arm6" ||
           object.typeName == "piston" || object.typeName == "baron";
}
// A baron (wheel) grabs by rotating, so a tape without G/g never proves it idle.
bool TapedArm(const io::SolutionObject& object) {
    return object.typeName == "arm1" || object.typeName == "arm2" ||
           object.typeName == "arm3" || object.typeName == "arm6" ||
           object.typeName == "piston";
}
// Reset/repeat tapes (X/C) expand to a different period, so hardware edits against a
// serialized tape would misprice the machine. Every P0 transform needs explicit tapes.
bool ExplicitTapes(const io::SolutionData& candidate) {
    for (const auto& object : candidate.objects) {
        int previous = -1;
        for (const auto& instruction : object.instructions) {
            if (instruction.parameter < 0 || instruction.parameter <= previous ||
                std::string_view("GgAaEeRrPpOB ").find(instruction.action) ==
                    std::string_view::npos)
                return false;
            previous = instruction.parameter;
        }
    }
    return true;
}
// A piston costs 40 and an arm1 costs 20 for the same tape. Without extension or
// rotation the mechanism only grabs, so the retyped tape is executable unchanged.
bool RetypeNonExtendingPistons(io::SolutionData& candidate) {
    bool changed = false;
    for (auto& object : candidate.objects) {
        if (object.typeName != "piston") continue;
        const bool motion = std::any_of(
            object.instructions.begin(), object.instructions.end(), [](const auto& instruction) {
                return std::string_view("EeRrPp").find(instruction.action) != std::string_view::npos;
            });
        if (motion) continue;
        object.typeName = "arm1";
        changed = true;
    }
    return changed;
}
// An arm whose tape never grabs cannot hold or place an atom; it only pays cost
// and area. Empty and motion-only tapes are both removed, unlike v2's idle rule.
bool RemoveNoGrabArms(io::SolutionData& candidate) {
    const auto removed = std::erase_if(candidate.objects, [](const auto& object) {
        if (!TapedArm(object)) return false;
        return std::none_of(object.instructions.begin(), object.instructions.end(),
                            [](const auto& instruction) {
                                return instruction.action == 'G' || instruction.action == 'g';
                            });
    });
    return removed != 0;
}
// Trims both ends of every rail to the cells its own arms actually visit and erases
// rails no arm is attached to. Cell and adjacency guards mirror TrimSharedTrackEnvelopes.
bool CompactTrackEnvelopes(io::SolutionData& candidate) {
    struct Rail {
        std::size_t object;
        int count, low, high;
    };
    using Cell = std::pair<long long, long long>;
    std::map<Cell, std::pair<std::size_t, int>> cells;
    std::vector<Rail> rails;
    // Rails first: an arm can precede its track in serialized order.
    for (std::size_t i = 0; i < candidate.objects.size(); ++i) {
        const auto& object = candidate.objects[i];
        if (object.typeName != "track") continue;
        const auto& body = object.bodyData;
        if (!object.instructions.empty() || body.size() < 8 || body[1] != 0) return false;
        const int count = body[4];
        if (count < 1 || body.size() != 6 + 2 * static_cast<std::size_t>(count) ||
            !NonLoopingTrack(object, count))
            return false;
        for (int j = 0; j < count; ++j) {
            const long long q = body[5 + 2 * j], r = body[6 + 2 * j];
            if (j) {
                const auto dq = q - body[3 + 2 * j], dr = r - body[4 + 2 * j];
                if (std::max({std::abs(dq), std::abs(dr), std::abs(dq + dr)}) != 1)
                    return false;
            }
            if (!cells.emplace(Cell{object.position.q + q, object.position.r + r},
                               std::pair{rails.size(), j}).second)
                return false;
        }
        rails.push_back({i, count, count, -1});
    }
    if (rails.empty()) return false;
    for (const auto& object : candidate.objects) {
        if (!MechanicalArm(object)) continue;
        const auto found = cells.find({object.position.q, object.position.r});
        if (found == cells.end()) {
            // An off-rail arm cannot walk a rail it is not attached to.
            if (std::any_of(object.instructions.begin(), object.instructions.end(),
                            [](const auto& instruction) {
                                return instruction.action == 'A' || instruction.action == 'a';
                            }))
                return false;
            continue;
        }
        auto& rail = rails[found->second.first];
        int position = found->second.second;
        const int start = position;
        const auto visit = [&] {
            if (position < 0 || position >= rail.count) return false;
            rail.low = std::min(rail.low, position);
            rail.high = std::max(rail.high, position);
            return true;
        };
        if (!visit()) return false;
        for (const auto& instruction : object.instructions) {
            if (instruction.action != 'A' && instruction.action != 'a') continue;
            position += instruction.action == 'A' ? 1 : -1;
            if (!visit()) return false;
        }
        // Sparse tapes repeat without reset expansion; their rail positions must close.
        if (position != start) return false;
    }
    if (rails.empty()) return false;
    auto trimmed = candidate;
    bool changed = false;
    std::vector<std::size_t> unused;
    for (const auto& rail : rails) {
        if (rail.high < 0) {
            unused.push_back(rail.object);
            continue;
        }
        if (rail.low == 0 && rail.high == rail.count - 1) continue;
        auto& track = trimmed.objects[rail.object];
        auto& body = track.bodyData;
        body.erase(body.begin() + 5 + 2 * (rail.high + 1), body.end() - 1);
        body.erase(body.begin() + 5, body.begin() + 5 + 2 * rail.low);
        body[4] = rail.high - rail.low + 1;
        // Removing endpoints must not turn the retained path into a wrapping track.
        if (!NonLoopingTrack(track, body[4])) return false;
        changed = true;
    }
    if (!unused.empty()) {
        std::sort(unused.rbegin(), unused.rend());
        for (const auto index : unused) trimmed.objects.erase(trimmed.objects.begin() + index);
        changed = true;
    }
    if (changed) candidate = std::move(trimmed);
    return changed;
}
bool CompactTransport(io::SolutionData& candidate) {
    if (!ExplicitTapes(candidate)) return false;
    auto trial = candidate;
    bool changed = RetypeNonExtendingPistons(trial);
    changed = RemoveNoGrabArms(trial) || changed;
    changed = CompactTrackEnvelopes(trial) || changed;
    if (!changed) return false;
    candidate = std::move(trial);
    return true;
}
bool CompactReturns(io::SolutionData& candidate) {
    if (SerialReturnsOnly()) return false;
    const char* emptyReturns = std::getenv("OPUSOPTIMA_EMPTY_RETURN");
    return ((!emptyReturns || std::string_view(emptyReturns) != "off") &&
            CompactEmptyReturns(candidate)) || ParallelReturns(candidate);
}
}
static io::SolutionData LocalTrialImpl(const io::SolutionData& parent, int variant,
                                       const char** transformation = nullptr) {
    auto candidate = parent;
    const char* reuse = std::getenv("OPUSOPTIMA_SHARED_BONDER");
    if (variant == 3 && reuse && std::string_view(reuse) == "on") {
        static constexpr core::HexCoord directions[] = {{1,0},{0,1},{-1,1},{-1,0},{0,-1},{1,-1}};
        for (auto first = candidate.objects.begin(); first != candidate.objects.end(); ++first) {
            if (first->typeName != "bonder" || first->bodyData.size() < 2) continue;
            const auto endpoint = [&](const auto& object) {
                const auto direction = directions[(object.bodyData[1] % 6 + 6) % 6];
                return core::Add(object.position, direction);
            };
            for (auto second = std::next(first); second != candidate.objects.end(); ++second) {
                if (second->typeName != "bonder" || second->bodyData.size() < 2) continue;
                if ((first->position == second->position && endpoint(*first) == endpoint(*second)) ||
                    (first->position == endpoint(*second) && endpoint(*first) == second->position)) {
                    candidate.objects.erase(second);
                    return candidate;
                }
            }
        }
        const auto bonder = std::find_if(candidate.objects.begin(), candidate.objects.end(),
                                        [](const auto& object) { return object.typeName == "bonder"; });
        if (bonder != candidate.objects.end()) candidate.objects.erase(bonder);
        return candidate;
    }
    // Empty grippers preserve their individual paths; cross-arm timing still needs the real gate.
    if (variant == 4 && !SerialReturnsOnly()) {
        if (CompactReturns(candidate)) return candidate;
        if (PrepositionEmptyRotations(candidate)) return candidate;
    }
    if (variant == 0 || variant == 1) {
        std::set<int> times;
        for (const auto& object : candidate.objects)
            for (const auto& instruction : object.instructions) times.insert(instruction.parameter);
        int end = 0, gap = 0, previous = -1;
        for (int time : times) {
            if (time - previous - 1 > gap) { gap = time - previous - 1; end = time; }
            previous = time;
        }
        for (auto& object : candidate.objects)
            for (auto& instruction : object.instructions) {
                if (variant == 0) instruction.parameter /= 2;
                else if (instruction.parameter >= end) instruction.parameter -= gap;
            }
    } else if (variant == 2) {
        RemoveIdleArms(candidate);
    } else if (variant == 8) {
        // Default-queued hardware compaction; the native gate prices every child.
        if (CompactTransportEnabled() && CompactTransport(candidate) && transformation)
            *transformation = "compact-transport-hardware";
    } else if (variant == 3 || variant == 4) {
        const int turns = variant == 3 ? 1 : 5;
        auto rotate = [turns](int& q, int& r) {
            for (int i = 0; i < turns; ++i) { int old = q; q = -r; r += old; }
        };
        for (auto& object : candidate.objects) {
            rotate(object.position.q, object.position.r);
            if (object.bodyData.size() > 1)
                object.bodyData[1] = (object.bodyData[1] + turns) % 6;
            // Track offsets are serialized coordinates, not arm instructions.
            if (object.typeName == "track" && object.bodyData.size() >= 6)
                for (std::size_t i = 5; i + 1 < object.bodyData.size() - 1; i += 2)
                    rotate(object.bodyData[i], object.bodyData[i + 1]);
        }
    } else if (variant == 6 || variant == 7) {
        const char* envelopes = std::getenv("OPUSOPTIMA_TRACK_ENVELOPE");
        if (variant == 6 && (!envelopes || std::string_view(envelopes) == "on") &&
            TrimSharedTrackEnvelopes(candidate)) {
            if (transformation) *transformation = "trim-shared-track-envelopes";
            const char* compose = std::getenv("OPUSOPTIMA_IDLE_TRACK_ENVELOPE");
            if (compose && std::string_view(compose) == "on") {
                auto combined = parent;
                // Both envelope passes must succeed; idle removal never widens the rail domain.
                if (RemoveIdleArms(combined) && TrimSharedTrackEnvelopes(combined)) {
                    if (transformation) *transformation = "remove-idle-arms-and-trim-shared-track-envelopes";
                    return combined;
                }
            }
            return candidate;
        }
        const char* usedRail = std::getenv("OPUSOPTIMA_USED_RAIL");
        if (variant == 7 && usedRail && std::string_view(usedRail) == "on") {
            const char* emptyReturns = std::getenv("OPUSOPTIMA_EMPTY_RETURN");
            auto trimmed = candidate;
            const bool tapeChanged = !SerialReturnsOnly() &&
                (!emptyReturns || std::string_view(emptyReturns) != "off") &&
                CompactEmptyReturns(trimmed);
            // Parent/v4 scores cannot prove dominance over the ungenerated old v7.
            if (!tapeChanged && TrimUsedRail(trimmed)) return trimmed;
        }
        for (auto& object : candidate.objects) {
            if (object.typeName != "track" || object.bodyData.size() < 10 ||
                object.bodyData[4] < 2) continue;
            const auto index = variant == 6 ? 5 : 5 + 2 * (object.bodyData[4] - 1);
            object.bodyData.erase(object.bodyData.begin() + index,
                                  object.bodyData.begin() + index + 2);
            --object.bodyData[4];
        }
    } else if (variant == 5) {
        // Both edits must apply; only the final speculative timing reaches the gate.
        if (CompactReturns(candidate) && PrepositionEmptyRotations(candidate)) {
            if (transformation) *transformation = "compact-returns-and-preposition-empty-rotations";
            return candidate;
        }
        candidate = LocalTrial(LocalTrial(parent, 0), 2);
    }
    return candidate;
}

io::SolutionData LocalTrial(const io::SolutionData& parent, int variant) {
    return LocalTrialImpl(parent, variant);
}

void OptimizeVerified(io::CandidateArchive& archive,
                      std::chrono::steady_clock::time_point deadline) {
    if (std::chrono::steady_clock::now() >= deadline) return;
    std::vector<io::CandidateArchive::Entry> parents;
    std::set<std::string> seen;
    auto key = [](io::SolutionData solution) {
        solution.solutionName.clear(); solution.metrics = {};
        return io::CandidateSha256(io::BinaryWriter::WriteSolution(solution));
    };
    for (const auto& entry : archive.Slots()) {
        if (entry && seen.insert(key(entry->solution)).second) parents.push_back(entry);
    }
    static constexpr const char* transformations[]{
        "timing-floor-divide-2", "timing-close-largest-gap", "remove-empty-arms-or-static-noop-arms",
        "rotate-60", "compact-single-arm-empty-returns-or-parallel-returns-or-preposition-empty-rotations-or-rotate-300", "timing-divide-2-and-remove-idle-arms",
        "trim-track-head-one", "trim-track-tail-one", "compact-transport-hardware"};
    static_assert(std::size(transformations) == 9);
    std::ofstream trace(archive.Directory() / "local-trials.jsonl");
    const auto* bonderFlag = std::getenv("OPUSOPTIMA_SHARED_BONDER");
    const bool prioritizeBonder = bonderFlag && std::string_view(bonderFlag) == "on";
    // Exactly eight variants, so at most twenty-four gates for three distinct slots.
    // Measured on all 422 published slots: v0 made no metric improvement and was
    // native-rejected 298 times; v8 improved 158 and was never rejected. v8 takes
    // v0's first position. Cycle value lives in v1 (58 cycle improvements, 16
    // unique), v4 (9) and v5 (6), so no cycle-producing variant is dropped.
    // OPUSOPTIMA_COMPACT_TRANSPORT=off restores the legacy order exactly; unset and
    // on both select the new default, where v8 is already present exactly once.
    const auto* compact = std::getenv("OPUSOPTIMA_COMPACT_TRANSPORT");
    const bool legacyQueue = compact && std::string_view(compact) == "off";
    std::vector<int> variants = prioritizeBonder
        ? std::vector<int>{0,1,2,3,4,5,6,7}
        : std::vector<int>{0,1,2,4,5,6,7,3};
    if (!legacyQueue) std::replace(variants.begin(), variants.end(), 0, 8);
    // Snapshot only: accepted children never recursively refill this queue.
    // Defer the dedicated rotation trial, but preserve shared-bonder priority.
    for (int variant : variants) {
        for (const auto& parent : parents) {
            if (std::chrono::steady_clock::now() >= deadline) return;
            trace << "{\"event\":\"reached\",\"variant\":" << variant
                  << ",\"parentSha256\":\"" << parent->solutionHash << "\"}\n";
            const char* transformation = transformations[variant];
            auto candidate = LocalTrialImpl(parent->solution, variant, &transformation);
            const auto candidateHash = key(candidate);
            if (!seen.insert(candidateHash).second) {
                trace << "{\"event\":\"deduplicated\",\"variant\":" << variant
                      << ",\"parentSha256\":\"" << parent->solutionHash << "\"}\n";
                trace.flush();
                continue;
            }
            const auto strategy = "local-v" + std::to_string(variant) +
                ":parent=" + parent->solutionHash;
            const char* reuse = std::getenv("OPUSOPTIMA_SHARED_BONDER");
            const bool sharedBonder = variant == 3 && reuse && std::string_view(reuse) == "on";
            trace << "{\"event\":\"attempt\",\"variant\":" << variant
                  << ",\"parentSha256\":\"" << parent->solutionHash << "\"}\n";
            auto report = archive.Submit(candidate, strategy, deadline);
            trace << "{\"event\":\"" << (report.Passed() ? "accepted" : "rejected")
                  << "\",\"variant\":" << variant << ",\"parentSha256\":\""
                  << parent->solutionHash << "\"}\n";
            if (report.Passed()) {
                auto publication = archive.Options();
                publication.outerDeadline = deadline;
                // Admission owns the authority; publish with that same scope.
                auto snapshot = archive.Snapshot(publication);
                if (!snapshot.Passed()) return;
            }
            trace << "{\"parentSha256\":\"" << parent->solutionHash
                  << "\",\"candidateCanonicalSha256\":\"" << candidateHash
                  << "\",\"parentMetrics\":{\"cost\":" << parent->verification.metrics.cost
                  << ",\"cycles\":" << parent->verification.metrics.cycles
                  << ",\"area\":" << parent->verification.metrics.area
                  << "},\"variant\":" << variant << ",\"transformation\":\""
                  << (sharedBonder ? "remove-first-shared-bonder" : transformation) << "\",\"verification\":"
                  << io::VerificationReportJson(report) << "}\n";
            trace.flush();
            if (report.status == io::VerificationStatus::Timeout ||
                report.status == io::VerificationStatus::Unavailable ||
                report.status == io::VerificationStatus::Error) return;
        }
    }
}
} // namespace opusoptima::solvers
