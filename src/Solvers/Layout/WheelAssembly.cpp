#include "WheelAssembly.hpp"
#include "AssemblySolver.hpp"
#include "RowAssembly.hpp"

#include <algorithm>
#include <array>
#include <optional>
#include <set>
#include <stdexcept>

namespace opusoptima::solvers {
io::SolutionData CompileRowAssembly(const io::PuzzleData&, const AssemblyPlan&,
                                    const io::PuzzleData*);

namespace {
io::SolutionData CompileMolecularWheel(const io::PuzzleData& puzzle, bool normalize = false) {
    if (puzzle.inputs.empty() ||
        std::none_of(puzzle.inputs.begin(), puzzle.inputs.end(),
                     [](const auto& input) { return !input.bonds.empty(); }))
        throw std::invalid_argument("molecular wheel requires a bonded reagent");
    if (!normalize && puzzle.inputs.size() != 1)
        throw std::invalid_argument("direct molecular wheel requires one reagent");
    auto logical = puzzle;
    const bool shared = puzzle.outputs.size() > 1;
    if (shared && !normalize) {
        logical.outputs = {io::Molecule{}};
        auto& target = logical.outputs[0];
        for (const auto& output : puzzle.outputs) {
            if (output.atoms.size() != 1 || !output.bonds.empty() ||
                std::any_of(target.atoms.begin(), target.atoms.end(),
                            [&](const auto& atom) { return atom.type == output.atoms[0].type; }))
                throw std::invalid_argument("wheel shared outputs require distinct single atoms");
            const int q = static_cast<int>(target.atoms.size());
            target.atoms.push_back({output.atoms[0].type, {q, 0}});
            if (q) target.bonds.push_back({1, {q - 1, 0}, {q, 0}});
        }
    }
    auto& reagent = logical.inputs[0];
    auto target = logical.outputs[0];
    std::array<bool, 6> derived{};
    if (normalize) {
        const bool cardinalInput =
            std::any_of(logical.inputs.begin(), logical.inputs.end(), [](const auto& input) {
                return std::any_of(input.atoms.begin(), input.atoms.end(), [](const auto& atom) {
                    return atom.type >= core::AtomType::Air && atom.type <= core::AtomType::Water;
                });
            });
        if (cardinalInput && !(puzzle.permissionBits & (1ULL << 12)))
            throw std::invalid_argument("molecular wheel normalization requires calcification");
        for (auto& output : logical.outputs)
            for (auto& atom : output.atoms) {
                const int type = static_cast<int>(atom.type);
                if (type >= 2 && type <= 5) {
                    derived[type] = true;
                    atom.type = core::AtomType::Salt;
                }
            }
        if (shared) {
            target = BuildSharedAssemblyTarget(logical).puzzle.outputs[0];
            for (std::size_t i = 0, product = 0; product < puzzle.outputs.size(); ++product)
                for (const auto& atom : puzzle.outputs[product].atoms)
                    target.atoms[i++].type = atom.type;
        }
    } else {
        if (reagent.atoms.size() != target.atoms.size())
            throw std::invalid_argument("molecular wheel requires a balanced single-batch recipe");
        std::array<int, 6> demand{}, stock{};
        for (const auto& atom : target.atoms) {
            const int type = static_cast<int>(atom.type);
            if (type < 1 || type > 5)
                throw std::invalid_argument("molecular wheel requires salt/cardinal outputs");
            ++demand[type];
        }
        for (const auto& atom : reagent.atoms) {
            const int type = static_cast<int>(atom.type);
            if (type < 1 || type > 5)
                throw std::invalid_argument("molecular wheel requires salt/cardinal inputs");
            ++stock[type];
        }
        for (int type = 2; type <= 5; ++type) {
            if (stock[type] > demand[type] || (stock[type] && stock[type] != demand[type]))
                throw std::invalid_argument(
                    "molecular wheel requires directly preserved cardinal stock");
            if (stock[type] == demand[type]) continue;
            derived[type] = true;
            for (int n = 0; n < demand[type]; ++n) {
                const auto salt =
                    std::find_if(reagent.atoms.begin(), reagent.atoms.end(), [](const auto& atom) {
                        return atom.type == core::AtomType::Salt;
                    });
                if (salt == reagent.atoms.end())
                    throw std::invalid_argument("molecular wheel salt deficit");
                salt->type = static_cast<core::AtomType>(type);
            }
        }
    }
    if (std::none_of(derived.begin(), derived.end(), [](bool value) { return value; }))
        throw std::invalid_argument("molecular wheel requires a derived cardinal");
    const auto supplyPuzzle =
        normalize && shared ? BuildSharedAssemblyTarget(logical).puzzle : logical;
    const auto supply = AssemblyPlanner::PlanSupply(supplyPuzzle);
    for (const auto& step : supply.steps)
        if ((step.conversion != ElementConversion::None &&
             !(normalize && (step.conversion == ElementConversion::Calcification ||
                             step.conversion == ElementConversion::Projection))) ||
            (!normalize &&
             (step.kind == SupplyPlan::Kind::Discard || step.kind == SupplyPlan::Kind::Reagent)))
            throw std::invalid_argument("molecular wheel requires direct or calcified disassembly");
    // The wheel family relocates every emitted object by a fixed offset and appends
    // its own tape surgery, so it needs the serialized row schedule.
    std::optional<io::SolutionData> row;
    {
        const RowClockScope serialized(-1);
        row = normalize && shared
                  ? CompileSharedOrdinaryOutputs(logical)
                  : CompileRowAssembly(logical, AssemblyPlanner::Plan(logical),
                                       shared ? &puzzle : nullptr);
    }
    auto result = std::move(*row);
    int minQ = 128, maxQ = -128;
    for (const auto& atom : target.atoms) {
        minQ = std::min(minQ, atom.position.q);
        maxQ = std::max(maxQ, atom.position.q);
    }
    const int offset = maxQ - minQ + 9;
    int conveyor = -1, track = -1, left = -offset;
    for (int i = 0; i < static_cast<int>(result.objects.size()); ++i) {
        const auto& obj = result.objects[i];
        left = std::min(left, obj.position.q);
        if (obj.position == core::HexCoord{-offset, -3}) {
            if (obj.typeName == "piston") conveyor = i;
            if (obj.typeName == "track") track = i;
        }
    }
    if (conveyor < 0 || track < 0) throw std::logic_error("row conveyor missing");
    std::array<int, 6> stations{};
    const std::array<core::HexCoord, 6> wheel{{{}, {}, {-1, 1}, {1, -1}, {0, -1}, {0, 1}}};
    auto object = [&](const char* type, core::HexCoord position, int rotation) {
        io::SolutionObject obj;
        obj.typeName = type;
        obj.position = position;
        obj.bodyData = {1, rotation, 0, 0, static_cast<int>(result.objects.size())};
        result.objects.push_back(obj);
    };
    for (int type = 2; type <= 5; ++type)
        if (derived[type]) {
            left -= 6;
            stations[type] = left;
            auto cell = wheel[type];
            int turns = 0;
            while (cell != core::HexCoord{0, -1}) {
                cell = core::RotateClockwise(cell);
                ++turns;
            }
            object("baron", {left, 2}, turns);
            object("glyph-duplication", {left, 1}, 4);
        }
    // Extend the conveyor left without changing its initial position or direction.
    auto& data = result.objects[track].bodyData;
    const int extension = -offset - left;
    std::vector<int> extended{1, 0, 0, 0, data.at(4) + extension};
    for (int q = -extension; q < 0; ++q)
        extended.insert(extended.end(), {q, 0});
    extended.insert(extended.end(), data.begin() + 5, data.end());
    data = std::move(extended);
    std::vector<int> order;
    for (int i = 0; i < static_cast<int>(target.atoms.size()); ++i)
        order.push_back(i);
    std::sort(order.begin(), order.end(), [&](int a, int b) {
        const auto x = target.atoms[a].position, y = target.atoms[b].position;
        return x.r != y.r ? x.r > y.r : x.q > y.q;
    });
    std::vector<ElementConversion> conversions;
    for (const auto& step : supply.steps)
        if (step.kind != SupplyPlan::Kind::Store && step.kind != SupplyPlan::Kind::Discard &&
            step.kind != SupplyPlan::Kind::Reagent)
            conversions.push_back(step.conversion);
    std::vector<std::pair<int, int>> events;
    std::size_t pickup = 0;
    for (const auto& instruction : result.objects[conveyor].instructions)
        if (instruction.action == 'G') {
            const int type = static_cast<int>(target.atoms[order[pickup % order.size()]].type);
            const int delay = conversions.at(pickup++) == ElementConversion::Calcification ? 4 : 2;
            if (type >= 2 && type <= 5 && derived[type])
                events.push_back({instruction.parameter + delay, type});
        }
    if (pickup != order.size() * supply.productsPerPeriod)
        throw std::logic_error("row conveyor pickup mismatch");
    int shift = 0;
    for (const auto& [originalTime, type] : events) {
        int time = originalTime + shift;
        const int start = time, distance = -offset - stations[type];
        std::vector<io::ArmInstruction> instructions;
        auto emit = [&](char action) { instructions.push_back({action, time++}); };
        for (int n = 0; n < distance; ++n)
            emit('a');
        emit('E');
        emit('O');
        // Return extended so the converted atom bypasses the calcification cell.
        for (int n = 0; n < distance; ++n)
            emit('A');
        emit('e');
        const int duration = time - start;
        for (auto& obj : result.objects)
            for (auto& instruction : obj.instructions)
                if (instruction.parameter >= start) instruction.parameter += duration;
        auto& tape = result.objects[conveyor].instructions;
        tape.insert(tape.end(), instructions.begin(), instructions.end());
        shift += duration;
    }
    for (auto& obj : result.objects)
        std::sort(obj.instructions.begin(), obj.instructions.end(),
                  [](const auto& a, const auto& b) { return a.parameter < b.parameter; });
    result.solutionName = "Molecular disassembly with Van Berlo conversion";
    return result;
}
} // namespace

io::SolutionData CompileWheelAssembly(const io::PuzzleData& puzzle) {
    constexpr auto required = AssemblyPlanner::RequiredPermissions | (1ULL << 2) | (1ULL << 3) |
                              (1ULL << 13) | (1ULL << 25) | (1ULL << 28);
    if (puzzle.version != 3 || puzzle.productionInfo || puzzle.outputs.empty() ||
        puzzle.outputMultiplier <= 0 || (puzzle.permissionBits & required) != required)
        throw std::invalid_argument("wheel assembly requires finite outputs and "
                                    "piston/track/duplication/no-op/wheel permissions");
    if (std::any_of(puzzle.inputs.begin(), puzzle.inputs.end(), [](const auto& input) {
            return input.atoms.size() != 1 || !input.bonds.empty();
        })) {
        try {
            return CompileMolecularWheel(puzzle);
        } catch (const std::invalid_argument&) {
            if (std::none_of(puzzle.outputs.begin(), puzzle.outputs.end(), [](const auto& output) {
                    return std::any_of(output.atoms.begin(), output.atoms.end(),
                                       [](const auto& atom) {
                                           return atom.type >= core::AtomType::Air &&
                                                  atom.type <= core::AtomType::Water;
                                       });
                }))
                throw std::invalid_argument("molecular wheel requires a derived cardinal");
            bool directAssembly = true;
            try {
                (void)AssemblyPlanner::PlanSupply(puzzle);
            } catch (const std::invalid_argument&) {
                directAssembly = false;
            }
            if (directAssembly)
                throw std::invalid_argument(
                    "prefer available direct assembly to normalized wheel supply");
            return CompileMolecularWheel(puzzle, true);
        }
    }
    if (puzzle.outputs.size() != 1)
        throw std::invalid_argument("monatomic wheel requires one finite output");
    int salt = -1;
    for (int i = 0; i < static_cast<int>(puzzle.inputs.size()); ++i) {
        const auto& input = puzzle.inputs[i];
        if (input.atoms.size() != 1 || !input.bonds.empty())
            throw std::invalid_argument("wheel assembly requires monatomic inputs");
        if (input.atoms[0].type == core::AtomType::Salt) salt = i;
    }
    if (salt < 0 && (puzzle.permissionBits & (1ULL << 12))) {
        for (int i = 0; i < static_cast<int>(puzzle.inputs.size()); ++i) {
            const auto type = puzzle.inputs[i].atoms[0].type;
            if (type >= core::AtomType::Air && type <= core::AtomType::Water) {
                salt = i;
                break;
            }
        }
    }
    if (salt < 0)
        throw std::invalid_argument("wheel assembly requires a salt or calcifiable source");
    const bool calcifySource = puzzle.inputs[salt].atoms[0].type != core::AtomType::Salt;
    auto finite = puzzle;
    struct Feed {
        int index, type;
        core::HexCoord cell;
    };
    std::vector<Feed> feeds;
    for (const auto& atom : puzzle.outputs[0].atoms) {
        if (std::any_of(finite.inputs.begin(), finite.inputs.end(),
                        [&](const auto& input) { return input.atoms[0].type == atom.type; }))
            continue;
        if (atom.type == core::AtomType::Salt && calcifySource) continue;
        const int type = static_cast<int>(atom.type);
        if (type < 2 || type > 5) throw std::invalid_argument("wheel cannot supply target element");
        feeds.push_back({static_cast<int>(finite.inputs.size()), type, {}});
        io::Molecule input;
        input.atoms.push_back({atom.type, {0, 0}});
        finite.inputs.push_back(input);
    }
    if (feeds.empty()) throw std::invalid_argument("wheel assembly requires a derived cardinal");
    // The cardinal (calcified-salt) lane only appends its own hardware to the row's
    // object list and derives its feed schedule from object positions, so it keeps
    // the shipped pipelined row; only the molecular lane above needs the serialized
    // timing. Measured 2026-11-11: keeping this lane pipelined is what carries
    // bdaa166d (23072 -> 13388 cycles) and 0f6f9d67 (12005 -> 8135).
    auto result = CompileRowAssembly(finite, AssemblyPlanner::Plan(finite), nullptr);
    int minQ = 128, maxQ = -128;
    for (const auto& atom : puzzle.outputs[0].atoms) {
        minQ = std::min(minQ, atom.position.q);
        maxQ = std::max(maxQ, atom.position.q);
    }
    const int offset = maxQ - minQ + 9;
    int feedArm = -1, feedTrack = -1;
    for (int i = 0; i < static_cast<int>(result.objects.size()); ++i) {
        const auto& obj = result.objects[i];
        if (obj.position == core::HexCoord{-offset, 3}) {
            if (obj.typeName == "piston") feedArm = i;
            if (obj.typeName == "track") feedTrack = i;
        }
        if (obj.typeName == "input")
            for (auto& feed : feeds)
                if (obj.bodyData.at(2) == feed.index) feed.cell = obj.position;
    }
    if (feedArm < 0 || feedTrack < 0) throw std::logic_error("row feed machine missing");
    auto rotate = [](core::HexCoord c, int turns) {
        while (turns-- > 0)
            c = core::RotateClockwise(c);
        return c;
    };
    struct Event {
        int time, feed;
    };
    std::vector<Event> events;
    int distance = 0, length = 1, rotation = 4;
    for (const auto& instruction : result.objects[feedArm].instructions) {
        switch (instruction.action) {
        case 'A':
            ++distance;
            break;
        case 'a':
            --distance;
            break;
        case 'E':
            ++length;
            break;
        case 'e':
            --length;
            break;
        case 'R':
            rotation = (rotation + 1) % 6;
            break;
        case 'r':
            rotation = (rotation + 5) % 6;
            break;
        default:
            break;
        }
        if (instruction.action != 'G') continue;
        const auto& data = result.objects[feedTrack].bodyData;
        const auto cell =
            core::Add({-offset + data.at(5 + 2 * distance), 3 + data.at(6 + 2 * distance)},
                      rotate({length, 0}, rotation));
        for (int i = 0; i < static_cast<int>(feeds.size()); ++i)
            if (cell == feeds[i].cell)
                events.push_back({instruction.parameter - distance - 6 - (length - 1), i});
    }
    if (events.empty()) throw std::logic_error("derived wheel feed has no pickups");
    result.objects.erase(std::remove_if(result.objects.begin(), result.objects.end(),
                                        [&](const auto& obj) {
                                            return obj.typeName == "input" &&
                                                   obj.bodyData.at(2) >=
                                                       static_cast<int>(puzzle.inputs.size());
                                        }),
                         result.objects.end());
    auto object = [&](const char* type, core::HexCoord pos, int len, int rot, int index = 0) {
        io::SolutionObject obj;
        obj.typeName = type;
        obj.position = pos;
        obj.bodyData = {len, rot, index, 0, static_cast<int>(result.objects.size())};
        result.objects.push_back(obj);
        return static_cast<int>(result.objects.size()) - 1;
    };
    auto track = [&](int from, int to, int row) {
        const int id = object("track", {from, row}, 1, 0);
        auto& data = result.objects[id].bodyData;
        data = {1, 0, 0, 0, to - from + 1};
        for (int q = 0; q <= to - from; ++q)
            data.insert(data.end(), {q, 0});
        data.push_back(0);
    };
    int destinationMin = feeds.front().cell.q - 3, destinationMax = destinationMin;
    for (const auto& feed : feeds) {
        destinationMin = std::min(destinationMin, feed.cell.q - 3);
        destinationMax = std::max(destinationMax, feed.cell.q - 3);
    }
    constexpr int stationSpacing = 5;
    constexpr int deliveryClearance = 8;
    // Preserve the four-station layout's delivery clearance without reserving unused stations.
    const int source =
        destinationMin - stationSpacing * static_cast<int>(feeds.size()) - deliveryClearance;
    object("input", core::Subtract({source, 7}, puzzle.inputs[salt].atoms[0].position), 1, 0, salt);
    if (calcifySource) object("glyph-calcification", {source, 7}, 1, 0);
    std::array<int, 6> stations{};
    const std::array<core::HexCoord, 6> wheel{{{}, {}, {-1, 1}, {1, -1}, {0, -1}, {0, 1}}};
    int station = source + stationSpacing;
    for (const auto& feed : feeds) {
        stations[feed.type] = station;
        int turns = 0;
        while (rotate(wheel[feed.type], turns) != core::HexCoord{0, -1})
            ++turns;
        object("baron", {station, 9}, 1, turns);
        object("glyph-duplication", {station, 8}, 1, 4);
        station += stationSpacing;
    }
    track(source, destinationMax, 5);
    const int arm = object("piston", {source, 5}, 1, 1);
    if (destinationMin != destinationMax) track(destinationMin, destinationMax, 4);
    const int delivery = object("arm1", {destinationMin, 4}, 3, 1);
    int deliveryPosition = destinationMin, shift = 0;
    // Pause row assembly before its pickup excursion. Produce a real atom, then
    // deposit it at the removed placeholder; both supplier arms return empty.
    for (const auto& event : events) {
        const auto& feed = feeds[event.feed];
        const int destination = feed.cell.q - 3;
        int time = event.time + shift, start = time, position = source;
        std::vector<std::pair<int, io::ArmInstruction>> instructions;
        auto emit = [&](int id, char action) { instructions.push_back({id, {action, time++}}); };
        auto move = [&](int q) {
            while (position < q) {
                emit(arm, 'A');
                ++position;
            }
            while (position > q) {
                emit(arm, 'a');
                --position;
            }
        };
        emit(arm, 'E');
        emit(arm, 'G');
        emit(arm, 'e');
        move(stations[feed.type]);
        emit(arm, 'E');
        emit(arm, 'O');
        emit(arm, 'e');
        move(destination);
        emit(arm, 'E');
        emit(arm, 'g');
        emit(arm, 'e');
        move(source);
        while (deliveryPosition < destination) {
            emit(delivery, 'A');
            ++deliveryPosition;
        }
        while (deliveryPosition > destination) {
            emit(delivery, 'a');
            --deliveryPosition;
        }
        emit(delivery, 'G');
        emit(delivery, 'R');
        emit(delivery, 'g');
        emit(delivery, 'r');
        while (deliveryPosition > destinationMin) {
            emit(delivery, 'a');
            --deliveryPosition;
        }
        const int duration = time - start;
        for (auto& part : result.objects)
            for (auto& instruction : part.instructions)
                if (instruction.parameter >= start) instruction.parameter += duration;
        for (const auto& [id, instruction] : instructions)
            result.objects[id].instructions.push_back(instruction);
        shift += duration;
    }
    int end = 0;
    for (const auto& part : result.objects)
        for (const auto& instruction : part.instructions)
            end = std::max(end, instruction.parameter + 1);
    int armIndex = 0;
    for (auto& part : result.objects) {
        if (part.typeName == "piston" || part.typeName == "arm1" || part.typeName == "baron")
            part.bodyData[4] = armIndex++;
        if (!part.instructions.empty()) part.instructions.push_back({'g', end});
    }
    result.solutionName = "Van Berlo duplication row assembly";
    return result;
}
namespace {
std::optional<core::HexCoord> RadialWheelCenter(const io::PuzzleData& puzzle) {
    constexpr auto permissions = AssemblyPlanner::RequiredPermissions | (1ULL << 13) |
                                 (1ULL << 25) | (1ULL << 26) | (1ULL << 28);
    if (puzzle.version != 3 || puzzle.productionInfo || puzzle.outputMultiplier != 1 ||
        (puzzle.permissionBits & permissions) != permissions || puzzle.inputs.empty() ||
        puzzle.inputs.size() > 8 || puzzle.outputs.size() != 1)
        return std::nullopt;
    for (const auto& input : puzzle.inputs)
        if (input.atoms.size() != 1 || !input.bonds.empty() ||
            input.atoms[0].type != core::AtomType::Salt)
            return std::nullopt;
    const auto& target = puzzle.outputs[0];
    if (target.atoms.size() < 3 || target.atoms.size() > 7 ||
        target.bonds.size() != target.atoms.size() - 1)
        return std::nullopt;
    std::set<std::pair<int, int>> cells;
    bool cardinal = false;
    for (const auto& atom : target.atoms) {
        const int type = static_cast<int>(atom.type);
        if (type < 1 || type > 5 || !cells.emplace(atom.position.q, atom.position.r).second)
            return std::nullopt;
        cardinal |= type != 1;
    }
    if (!cardinal) return std::nullopt;
    for (const auto& center : target.atoms) {
        if (center.type != core::AtomType::Salt) continue;
        std::set<std::pair<int, int>> leaves;
        bool valid = true;
        for (const auto& atom : target.atoms)
            if (atom.position != center.position &&
                core::Distance(atom.position, center.position) != 1)
                valid = false;
        for (const auto& bond : target.bonds) {
            const auto leaf = bond.from == center.position ? bond.to : bond.from;
            if (bond.bits != 1 || (bond.from != center.position && bond.to != center.position) ||
                leaf == center.position || !cells.contains({leaf.q, leaf.r}) ||
                !leaves.emplace(leaf.q, leaf.r).second)
                valid = false;
        }
        if (valid) return center.position;
    }
    return std::nullopt;
}
} // namespace

bool SupportsRadialWheelAssembly(const io::PuzzleData& puzzle) {
    return RadialWheelCenter(puzzle).has_value();
}

io::SolutionData CompileRadialWheelAssembly(const io::PuzzleData& puzzle) {
    const auto center = RadialWheelCenter(puzzle);
    if (!center)
        throw std::invalid_argument("radial wheel requires an ordinary Salt-centered radial "
                                    "product and singleton Salt feeds");
    io::SolutionData result;
    result.puzzleName = puzzle.name;
    result.solutionName = "Reusable radial wheel assembly";
    auto object = [&](const char* name, core::HexCoord cell, int length = 1, int rotation = 0,
                      int index = 0) {
        io::SolutionObject part;
        part.typeName = name;
        part.position = cell;
        part.bodyData = {length, rotation, index, 0, 0};
        result.objects.push_back(part);
        return static_cast<int>(result.objects.size()) - 1;
    };
    const auto source = puzzle.inputs[0].atoms[0].position;
    object("input", core::Subtract({0, 0}, source));
    object("input", core::Subtract({3, -1}, source));
    object("bonder", {0, 0});
    object("glyph-duplication", {2, 1}, 1, 4);
    object("out-std", core::Subtract({0, 0}, *center));
    const int holder = object("arm1", {-2, 0}, 2);
    const int feeder = object("arm1", {2, -1});
    const int wheel = object("baron", {2, 2});
    result.objects[feeder].bodyData[4] = 1;
    result.objects[wheel].bodyData[4] = 2;
    int time = 0, orientation = 0, wheelOrientation = 0;
    auto emit = [&](int part, char action) {
        result.objects[part].instructions.push_back({action, time++});
    };
    auto turnTo = [&](int part, int& current, int wanted, char cw, char ccw) {
        const int forward = (wanted - current + 6) % 6;
        if (forward <= 3) {
            for (int i = 0; i < forward; ++i)
                emit(part, cw);
        } else {
            for (int i = forward; i < 6; ++i)
                emit(part, ccw);
        }
        current = wanted;
    };
    const std::array<core::HexCoord, 6> catalysts{{{}, {1, 0}, {-1, 1}, {1, -1}, {0, -1}, {0, 1}}};
    emit(holder, 'G');
    for (const auto& atom : puzzle.outputs[0].atoms) {
        if (atom.position == *center) continue;
        auto leaf = core::Subtract(atom.position, *center);
        int wanted = 0;
        while (leaf != core::HexCoord{1, 0}) {
            leaf = core::RotateClockwise(leaf);
            ++wanted;
        }
        turnTo(holder, orientation, wanted, 'p', 'P');
        auto catalyst = catalysts[static_cast<int>(atom.type)];
        wanted = 0;
        while (catalyst != core::HexCoord{0, -1}) {
            catalyst = core::RotateClockwise(catalyst);
            ++wanted;
        }
        turnTo(wheel, wheelOrientation, wanted, 'r', 'R');
        // Conversion occurs on the intermediate stop, away from the renewable input.
        emit(feeder, 'G');
        emit(feeder, 'r');
        emit(feeder, 'O');
        emit(feeder, 'r');
        emit(feeder, 'O');
        emit(feeder, 'g');
        emit(feeder, 'R');
        emit(feeder, 'R');
    }
    turnTo(holder, orientation, 0, 'p', 'P');
    emit(holder, 'g');
    turnTo(wheel, wheelOrientation, 0, 'r', 'R');
    // Equal tape lengths close all poses after consumption and center respawn.
    time += 2;
    for (const int part : {holder, feeder, wheel})
        result.objects[part].instructions.push_back({'O', time});
    return result;
}

namespace {
struct RadialProjectionRecipe {
    int radial{}, singleton{-1}, hubSource{}, metalSource{}, mercurySource{};
    core::HexCoord center;
};
std::optional<RadialProjectionRecipe> RadialProjection(const io::PuzzleData& p) {
    constexpr auto permissions =
        AssemblyPlanner::RequiredPermissions | (1ULL << 14) | (1ULL << 25) | (1ULL << 26);
    if (p.version != 3 || p.productionInfo || p.outputMultiplier != 1 ||
        (p.permissionBits & permissions) != permissions || p.inputs.empty() ||
        p.inputs.size() > 8 || p.outputs.empty() || p.outputs.size() > 2)
        return std::nullopt;
    for (const auto& input : p.inputs)
        if (input.atoms.size() != 1 || !input.bonds.empty() ||
            input.atoms[0].type < core::AtomType::Salt ||
            input.atoms[0].type > core::AtomType::Lead)
            return std::nullopt;
    auto source = [&](core::AtomType type) {
        for (int i = 0; i < static_cast<int>(p.inputs.size()); ++i)
            if (p.inputs[i].atoms[0].type == type) return i;
        return -1;
    };
    RadialProjectionRecipe recipe;
    recipe.mercurySource = source(core::AtomType::Quicksilver);
    if (recipe.mercurySource < 0) return std::nullopt;
    if (p.outputs.size() == 2) {
        recipe.radial = p.outputs[0].atoms.size() == 1 ? 1 : 0;
        recipe.singleton = 1 - recipe.radial;
        const auto& singleton = p.outputs[recipe.singleton];
        if (singleton.atoms.size() != 1 || !singleton.bonds.empty() ||
            singleton.atoms[0].type < core::AtomType::Gold ||
            singleton.atoms[0].type > core::AtomType::Lead)
            return std::nullopt;
    }
    const auto& target = p.outputs[recipe.radial];
    if (target.atoms.size() < 3 || target.atoms.size() > 7 ||
        target.bonds.size() != target.atoms.size() - 1)
        return std::nullopt;
    std::set<std::pair<int, int>> cells;
    for (const auto& atom : target.atoms)
        if (!cells.emplace(atom.position.q, atom.position.r).second) return std::nullopt;
    for (const auto& hub : target.atoms) {
        recipe.hubSource = source(hub.type);
        if (recipe.hubSource < 0) continue;
        bool valid = true;
        int lowest = static_cast<int>(core::AtomType::Gold);
        for (const auto& atom : target.atoms) {
            if (atom.position == hub.position) continue;
            valid &= core::Distance(atom.position, hub.position) == 1 &&
                     atom.type >= core::AtomType::Gold && atom.type <= core::AtomType::Lead;
            lowest = std::max(lowest, static_cast<int>(atom.type));
        }
        std::set<std::pair<int, int>> leaves;
        for (const auto& bond : target.bonds) {
            const auto leaf = bond.from == hub.position ? bond.to : bond.from;
            valid &= bond.bits == 1 && (bond.from == hub.position || bond.to == hub.position) &&
                     leaf != hub.position && cells.contains({leaf.q, leaf.r}) &&
                     leaves.emplace(leaf.q, leaf.r).second;
        }
        if (!valid) continue;
        if (recipe.singleton >= 0)
            lowest = std::max(lowest, static_cast<int>(p.outputs[recipe.singleton].atoms[0].type));
        recipe.metalSource = -1;
        for (int type = lowest; type <= static_cast<int>(core::AtomType::Lead); ++type) {
            const int candidate = source(static_cast<core::AtomType>(type));
            if (candidate >= 0) {
                recipe.metalSource = candidate;
                break;
            }
        }
        if (recipe.metalSource < 0) continue;
        recipe.center = hub.position;
        return recipe;
    }
    return std::nullopt;
}
} // namespace

bool SupportsRadialProjectionAssembly(const io::PuzzleData& puzzle) {
    return RadialProjection(puzzle).has_value();
}

io::SolutionData CompileRadialProjectionAssembly(const io::PuzzleData& puzzle) {
    const auto recipe = RadialProjection(puzzle);
    if (!recipe)
        throw std::invalid_argument("radial projection requires genuine singleton hub, lower metal "
                                    "and Mercury feeds with ordinary radial metal leaves");
    io::SolutionData result;
    result.puzzleName = puzzle.name;
    result.solutionName = "Controlled radial projection assembly";
    int armIndex = 0;
    auto object = [&](const char* name, core::HexCoord cell, int length = 1, int rotation = 0,
                      int index = 0) {
        io::SolutionObject part;
        part.typeName = name;
        part.position = cell;
        part.bodyData = {length, rotation, index, 0, 0};
        if (part.typeName == "arm1") part.bodyData[4] = armIndex++;
        result.objects.push_back(part);
        return static_cast<int>(result.objects.size()) - 1;
    };
    auto input = [&](int index, core::HexCoord cell) {
        object("input", core::Subtract(cell, puzzle.inputs[index].atoms[0].position), 1, 0, index);
    };
    input(recipe->hubSource, {0, 0});
    input(recipe->metalSource, {2, -2});
    input(recipe->mercurySource, {5, 0});
    object("bonder", {0, 0});
    object("glyph-projection", {3, 0}, 1, 3);
    object("out-std", core::Subtract({0, 0}, recipe->center), 1, 0, recipe->radial);
    if (recipe->singleton >= 0)
        object("out-std",
               core::Subtract({3, -2}, puzzle.outputs[recipe->singleton].atoms[0].position), 1, 0,
               recipe->singleton);
    const int holder = object("arm1", {-2, 0}, 2);
    const int feeder = object("arm1", {2, -1}, 1, 4);
    const int mercury = object("arm1", {4, 0});
    int time = 0, orientation = 0;
    auto emit = [&](int arm, char action) {
        result.objects[arm].instructions.push_back({action, time++});
    };
    auto turnHolder = [&](int wanted) {
        const int forward = (wanted - orientation + 6) % 6;
        for (int i = 0; i < std::min(forward, 6 - forward); ++i)
            emit(holder, forward <= 3 ? 'p' : 'P');
        orientation = wanted;
    };
    auto manufacture = [&](core::AtomType type, bool singleton) {
        emit(feeder, 'G');
        for (int i = 0; i < 3; ++i)
            emit(feeder, 'r');
        const int depth = static_cast<int>(puzzle.inputs[recipe->metalSource].atoms[0].type) -
                          static_cast<int>(type);
        // Native projection permits the held metal to stay at this isolated station.
        // Each released Mercury dose is consumed before the delivery arm returns.
        for (int dose = 0; dose < depth; ++dose) {
            emit(mercury, 'G');
            for (int i = 0; i < 3; ++i)
                emit(mercury, 'r');
            emit(mercury, 'g');
            emit(mercury, 'O');
            for (int i = 0; i < 3; ++i)
                emit(mercury, 'R');
        }
        if (singleton) {
            // The singleton outlet is separate from both projection and bonding cells.
            emit(feeder, 'R');
            emit(feeder, 'R');
            emit(feeder, 'g');
            emit(feeder, 'R');
        } else {
            emit(feeder, 'r');
            emit(feeder, 'O');
            emit(feeder, 'g');
            for (int i = 0; i < 4; ++i)
                emit(feeder, 'R');
        }
    };
    if (recipe->singleton >= 0) manufacture(puzzle.outputs[recipe->singleton].atoms[0].type, true);
    emit(holder, 'G');
    for (const auto& atom : puzzle.outputs[recipe->radial].atoms) {
        if (atom.position == recipe->center) continue;
        auto leaf = core::Subtract(atom.position, recipe->center);
        int wanted = 0;
        while (leaf != core::HexCoord{1, 0}) {
            leaf = core::RotateClockwise(leaf);
            ++wanted;
        }
        turnHolder(wanted);
        manufacture(atom.type, false);
    }
    turnHolder(0);
    emit(holder, 'g');
    time += 2;
    for (const int arm : {holder, feeder, mercury})
        result.objects[arm].instructions.push_back({'O', time});
    return result;
}
} // namespace opusoptima::solvers
