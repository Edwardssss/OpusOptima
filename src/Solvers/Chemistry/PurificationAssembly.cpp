#include "PurificationAssembly.hpp"
#include "AssemblySolver.hpp"
#include "RepeatingOutput.hpp"
#include "RowAssembly.hpp"

#include <algorithm>
#include <stdexcept>

namespace opusoptima::solvers {
namespace {
int SingletonSource(const io::PuzzleData& p) {
    constexpr auto permissions = (1ULL << 0) | (1ULL << 2) | (1ULL << 3) | (1ULL << 15) |
                                 (1ULL << 22) | (1ULL << 23) | (1ULL << 25);
    if (p.version != 3 || p.productionInfo || p.outputMultiplier != 1 ||
        (p.permissionBits & permissions) != permissions || p.outputs.size() != 1 ||
        p.outputs[0].atoms.size() != 1 || !p.outputs[0].bonds.empty() || p.inputs.empty() ||
        p.inputs.size() > 8)
        return -1;
    const int target = static_cast<int>(p.outputs[0].atoms[0].type);
    if (target < 7 || target > 10) return -1;
    int source = -1;
    for (int i = 0; i < static_cast<int>(p.inputs.size()); ++i) {
        const auto& input = p.inputs[i];
        if (input.atoms.size() != 1 || !input.bonds.empty()) return -1;
        const int type = static_cast<int>(input.atoms[0].type);
        if (type < 7 || type > 12) return -1;
        if (type > target &&
            (source < 0 || type < static_cast<int>(p.inputs[source].atoms[0].type)))
            source = i;
    }
    if (source < 0) return -1;
    const int depth = static_cast<int>(p.inputs[source].atoms[0].type) - target;
    return depth >= 2 && depth <= 5 ? source : -1;
}
} // namespace

bool SupportsSingletonPurificationChain(const io::PuzzleData& puzzle) {
    return SingletonSource(puzzle) >= 0;
}

bool AdmitsSingletonPurificationChain(const io::PuzzleData& puzzle) {
    const int source = SingletonSource(puzzle);
    if (source < 0) return false;
    const auto target = puzzle.outputs[0].atoms[0].type;
    const int depth =
        static_cast<int>(puzzle.inputs[source].atoms[0].type) - static_cast<int>(target);
    // Chain cost 35*d+50 beats static cost 10*d*(d+1) at depths four and five.
    return depth >= 4 &&
           std::none_of(puzzle.inputs.begin(), puzzle.inputs.end(),
                        [target](const auto& input) { return input.atoms[0].type == target; });
}

io::SolutionData CompileSingletonPurificationChain(const io::PuzzleData& p,
                                                   std::chrono::steady_clock::time_point deadline) {
    auto check = [&] {
        if (std::chrono::steady_clock::now() >= deadline)
            throw std::invalid_argument("singleton purification chain deadline");
    };
    check();
    const int source = SingletonSource(p);
    if (source < 0) throw std::invalid_argument("unsupported singleton purification chain");
    const int depth = static_cast<int>(p.inputs[source].atoms[0].type) -
                      static_cast<int>(p.outputs[0].atoms[0].type);
    io::SolutionData result;
    result.puzzleName = p.name;
    result.solutionName = "Reusable singleton purification chain";
    auto object = [&](const char* type, core::HexCoord cell, int length = 1, int rotation = 0,
                      int index = 0) {
        check();
        io::SolutionObject part;
        part.typeName = type;
        part.position = cell;
        part.bodyData = {length, rotation, index, 0, 0};
        result.objects.push_back(std::move(part));
        return result.objects.size() - 1;
    };
    object("input", core::Subtract({0, 0}, p.inputs[source].atoms[0].position), 1, 0, source);
    // Reagents occupy row zero; products occupy row one. Transport stays on row
    // minus one and retracts through a station only after its reagents are consumed.
    for (int level = 0; level < depth; ++level)
        object("glyph-purification", {3 * (level + 1), 0});
    object("out-std", core::Subtract({3 * depth, 1}, p.outputs[0].atoms[0].position));
    const auto rail = object("track", {0, -2});
    auto& data = result.objects[rail].bodyData;
    data = {1, 0, 0, 0, 3 * depth + 2};
    for (int q = 0; q <= 3 * depth + 1; ++q)
        data.insert(data.end(), {q, 0});
    data.push_back(0);
    const auto arm = object("piston", {0, -2}, 1, 1);
    int time = 0, position = 0;
    auto emit = [&](char action) {
        check();
        result.objects[arm].instructions.push_back({action, time++});
    };
    auto move = [&](int to) {
        while (position < to) {
            emit('A');
            ++position;
        }
        while (position > to) {
            emit('a');
            --position;
        }
    };
    auto transfer = [&](int from, int height, int to) {
        move(from);
        for (int i = 0; i <= height; ++i)
            emit('E');
        emit('G');
        for (int i = 0; i <= height; ++i)
            emit('e');
        move(to);
        emit('E');
        emit('g');
        emit('e');
    };
    auto produce = [&](auto&& self, int level, int to) -> void {
        check();
        if (level < 0) {
            transfer(0, 0, to);
            return;
        }
        const int q = 3 * (level + 1);
        self(self, level - 1, q);
        self(self, level - 1, q + 1);
        emit('O');
        emit('O');
        transfer(q, 1, to);
    };
    // The final glyph feeds the genuine output directly. All lower stock is
    // consumed, and the empty retracted piston returns to its initial pose.
    const int final = 3 * depth;
    produce(produce, depth - 2, final);
    produce(produce, depth - 2, final + 1);
    emit('O');
    emit('O');
    move(0);
    emit('g');
    check();
    return result;
}

// Replace only synthetic feed placeholders; every replacement is supplied by an
// explicit recursive purification machine, serialized ahead of the row pickup.
io::SolutionData CompilePurificationAssembly(const io::PuzzleData& puzzle, bool allowFiniteFeed) {
    return CompilePurificationAssembly(puzzle, allowFiniteFeed,
                                       std::chrono::steady_clock::time_point::max());
}

io::SolutionData CompilePurificationAssembly(const io::PuzzleData& puzzle, bool allowFiniteFeed,
                                             std::chrono::steady_clock::time_point deadline) {
    auto checkDeadline = [&] {
        if (deadline != std::chrono::steady_clock::time_point::max() &&
            std::chrono::steady_clock::now() >= deadline)
            throw std::invalid_argument("purification assembly deadline");
    };
    checkDeadline();
    constexpr auto permissions =
        AssemblyPlanner::RequiredPermissions | (1ULL << 2) | (1ULL << 3) | (1ULL << 15);
    if (puzzle.version != 3 || puzzle.productionInfo || puzzle.outputs.size() != 1 ||
        puzzle.outputMultiplier <= 0 || (puzzle.permissionBits & permissions) != permissions)
        throw std::invalid_argument("purification assembly requires one nonproduction output and "
                                    "piston/track/purification permissions");
    for (const auto& input : puzzle.inputs)
        if (input.atoms.empty())
            throw std::invalid_argument("purification assembly requires nonempty inputs");
    auto finite = puzzle;
    auto contains = [](const auto& input, auto type) {
        return std::any_of(input.atoms.begin(), input.atoms.end(),
                           [&](const auto& atom) { return atom.type == type; });
    };
    const bool repeating = HasRepeatingOutput(finite.outputs[0]);
    if (repeating) {
        finite.outputs[0] = ExpandRepeatingOutput(finite.outputs[0]);
        finite.outputMultiplier = 1;
    }
    struct Feed {
        int index, source, sourceAtom, stages;
        core::HexCoord cell;
    };
    std::vector<Feed> feeds;
    for (const auto& atom : finite.outputs[0].atoms) {
        checkDeadline();
        if (std::any_of(finite.inputs.begin(), finite.inputs.end(),
                        [&](const auto& input) { return contains(input, atom.type); }))
            continue;
        const int target = static_cast<int>(atom.type);
        if (target < 7 || target >= 12) continue;
        int source = -1, sourceAtom = -1, sourceMetal = 13;
        for (int i = 0; i < static_cast<int>(puzzle.inputs.size()); ++i)
            for (int j = 0; j < static_cast<int>(puzzle.inputs[i].atoms.size()); ++j) {
                checkDeadline();
                const int type = static_cast<int>(puzzle.inputs[i].atoms[j].type);
                if (type > target && type <= 12 && type < sourceMetal) {
                    source = i;
                    sourceAtom = j;
                    sourceMetal = type;
                }
            }
        if (source < 0)
            throw std::invalid_argument("no lower metal feed for purification assembly");
        feeds.push_back(
            {static_cast<int>(finite.inputs.size()), source, sourceAtom, sourceMetal - target, {}});
        io::Molecule input;
        input.atoms.push_back({atom.type, {0, 0}});
        finite.inputs.push_back(input);
    }
    if (feeds.empty())
        throw std::invalid_argument("purification assembly requires a derived metal");
    const auto sourceType =
        puzzle.inputs[feeds.front().source].atoms[feeds.front().sourceAtom].type;
    if (std::any_of(feeds.begin(), feeds.end(), [&](const auto& feed) {
            return puzzle.inputs[feed.source].atoms[feed.sourceAtom].type != sourceType;
        }))
        throw std::invalid_argument("purification assembly requires a common source metal");
    const auto& sourceFeed = feeds.front();
    const bool molecular = puzzle.inputs[sourceFeed.source].atoms.size() != 1;
    io::PuzzleData supplyPuzzle;
    AssemblyPlan supplyPlan;
    io::SolutionData batch;
    std::vector<int> productEnds;
    bool finiteFeed = false;
    if (molecular) {
        supplyPuzzle = puzzle;
        supplyPuzzle.inputs = {puzzle.inputs[sourceFeed.source]};
        io::Molecule output;
        output.atoms.push_back({sourceType, {0, 0}});
        supplyPuzzle.outputs = {output};
        supplyPuzzle.outputMultiplier = 1;
        supplyPlan = AssemblyPlanner::Plan(supplyPuzzle, false, deadline);
        try {
            batch = CompileRowAssemblyBatch(supplyPuzzle, supplyPlan, productEnds, deadline);
        } catch (const std::invalid_argument&) {
            if (!allowFiniteFeed || (puzzle.permissionBits & (1ULL << 17))) throw;
            finiteFeed = true;
        }
    }
    const auto plan = AssemblyPlanner::Plan(finite, false, deadline);
    auto result = [&] {
        if (finiteFeed) {
            std::vector<int> ends;
            return CompileRowAssemblyFiniteBatch(finite, plan, ends, 6ULL * finite.outputMultiplier,
                                                 deadline);
        }
        if (repeating) {
            try {
                return CompileRowAssemblySingleProduct(finite, plan, deadline);
            } catch (const std::invalid_argument&) {
            }
        }
        return CompileRowAssembly(finite, plan, deadline);
    }();
    int minQ = 128, maxQ = -128;
    for (const auto& atom : finite.outputs[0].atoms) {
        checkDeadline();
        minQ = std::min(minQ, atom.position.q);
        maxQ = std::max(maxQ, atom.position.q);
    }
    const int offset = maxQ - minQ + 9;
    int feedArm = -1;
    for (int i = 0; i < static_cast<int>(result.objects.size()); ++i) {
        checkDeadline();
        const auto& object = result.objects[i];
        if (object.typeName == "piston" && object.position == core::HexCoord{-offset, 3})
            feedArm = i;
        if (object.typeName == "input")
            for (auto& feed : feeds)
                if (object.bodyData[2] == feed.index) feed.cell = object.position;
    }
    if (feedArm < 0)
        throw std::invalid_argument("purification assembly row feed layout unsupported");
    struct Event {
        int time, feed;
    };
    std::vector<Event> events;
    int distance = 0, length = 1, rotation = 4;
    auto rotate = [](core::HexCoord c, int turns) {
        while (turns-- > 0)
            c = core::RotateClockwise(c);
        return c;
    };
    const auto feedInstructions = result.objects[feedArm].instructions;
    const io::SolutionObject* feedTrack = nullptr;
    for (const auto& object : result.objects)
        if (object.typeName == "track" && object.position == core::HexCoord{-offset, 3})
            feedTrack = &object;
    for (const auto& instruction : feedInstructions) {
        checkDeadline();
        const char action = instruction.action;
        if (action == 'A') ++distance;
        if (action == 'a') --distance;
        if (action == 'E') ++length;
        if (action == 'e') --length;
        if (action == 'R') rotation = (rotation + 1) % 6;
        if (action == 'r') rotation = (rotation + 5) % 6;
        if (action != 'G') continue;
        const auto& data = feedTrack->bodyData;
        auto cell = core::Add(
            core::HexCoord{-offset + data.at(5 + 2 * distance), 3 + data.at(6 + 2 * distance)},
            rotate({length, 0}, rotation));
        for (int i = 0; i < static_cast<int>(feeds.size()); ++i)
            if (cell == feeds[i].cell)
                events.push_back({instruction.parameter - distance - 6 - (length - 1), i});
    }
    // Remove placeholders only after inspecting their original object indices.
    result.objects.erase(std::remove_if(result.objects.begin(), result.objects.end(),
                                        [&](const auto& object) {
                                            return object.typeName == "input" &&
                                                   object.bodyData[2] >=
                                                       static_cast<int>(puzzle.inputs.size());
                                        }),
                         result.objects.end());
    auto object = [&](const char* type, core::HexCoord pos, int len, int rot, int index = 0) {
        checkDeadline();
        io::SolutionObject obj;
        obj.typeName = type;
        obj.position = pos;
        obj.bodyData = {len, rot, index, 0, static_cast<int>(result.objects.size())};
        result.objects.push_back(obj);
        return static_cast<int>(result.objects.size()) - 1;
    };
    struct Machine {
        int arm, delivery, source, destination, height;
    };
    int destinationMin = feeds.front().cell.q - 3, destinationMax = destinationMin;
    int stages = 0;
    for (const auto& feed : feeds) {
        checkDeadline();
        destinationMin = std::min(destinationMin, feed.cell.q - 3);
        destinationMax = std::max(destinationMax, feed.cell.q - 3);
        stages = std::max(stages, feed.stages);
    }
    const int height = 7;
    int source = destinationMin - 8 * stages - 8;
    for (const auto& part : result.objects) {
        checkDeadline();
        source = std::min(source, part.position.q - 8 * stages - 8);
        if (part.typeName == "track")
            for (int j = 0; j < part.bodyData[4]; ++j) {
                checkDeadline();
                source =
                    std::min(source, part.position.q + part.bodyData[5 + 2 * j] - 8 * stages - 8);
            }
    }
    int molecularOffset = 40;
    int batchDuration = 0;
    std::size_t nextProduct = 0;
    int batchCursor = 0;
    if (molecular) {
        if (finiteFeed) {
            std::size_t demand = 0;
            for (const auto& event : events)
                demand += std::size_t{1} << feeds[event.feed].stages;
            batch = CompileRowAssemblyFiniteBatch(supplyPuzzle, supplyPlan, productEnds, demand,
                                                  deadline);
        }
        batch.objects.erase(
            std::remove_if(batch.objects.begin(), batch.objects.end(),
                           [](const auto& part) { return part.typeName == "out-std"; }),
            batch.objects.end());
        // Keep the producer's vertical return outside every nested feed rail,
        // including the escape arm's parking position.
        int left = 0;
        for (const auto& part : batch.objects) {
            checkDeadline();
            left = std::min(left, part.position.q - 3);
            if (part.typeName == "track")
                for (int j = 0; j < part.bodyData[4]; ++j) {
                    checkDeadline();
                    left = std::min(left, part.position.q + part.bodyData[5 + 2 * j] - 3);
                }
        }
        molecularOffset = std::max(molecularOffset, 8 - left);
        for (auto& part : batch.objects) {
            checkDeadline();
            part.position = core::Add({-part.position.q, -part.position.r},
                                      {source - molecularOffset + 2, height + 2});
            part.bodyData[1] = (part.bodyData[1] + 3) % 6;
            if (part.typeName == "track")
                for (int j = 0; j < part.bodyData[4]; ++j) {
                    checkDeadline();
                    part.bodyData[5 + 2 * j] *= -1;
                    part.bodyData[6 + 2 * j] *= -1;
                }
            if (part.typeName == "input") part.bodyData[2] = sourceFeed.source;
            for (const auto& instruction : part.instructions) {
                checkDeadline();
                batchDuration = std::max(batchDuration, instruction.parameter + 1);
            }
        }
    } else {
        object("input",
               core::Subtract({source, height}, puzzle.inputs[sourceFeed.source].atoms[0].position),
               1, 0, sourceFeed.source);
    }
    for (int level = 0; level < stages; ++level)
        object("glyph-purification", {source + 8 * (level + 1), height}, 1, 0);
    auto track = [&](int from, int to, int row) {
        checkDeadline();
        int t = object("track", {from, row}, 1, 0);
        auto& data = result.objects[t].bodyData;
        data = {1, 0, 0, 0, to - from + 1};
        for (int q = 0; q <= to - from; ++q) {
            checkDeadline();
            data.insert(data.end(), {q, 0});
        }
        data.push_back(0);
    };
    const int pickup = molecular ? source - molecularOffset : source;
    std::vector<core::HexCoord> producerRoute;
    if (molecular) {
        // Route below finite waste storage as well as the batch's feed track.
        int bottom = -2;
        if (finiteFeed)
            for (const auto& part : batch.objects) {
                checkDeadline();
                bottom = std::min(bottom, part.position.r - 5);
                if (part.typeName == "track")
                    for (int j = 0; j < part.bodyData[4]; ++j) {
                        checkDeadline();
                        bottom = std::min(bottom, part.position.r + part.bodyData[6 + 2 * j] - 5);
                    }
            }
        for (int r = height - 2; r >= bottom; --r) {
            checkDeadline();
            producerRoute.push_back({pickup, r});
        }
        for (int q = pickup + 1; q <= source; ++q) {
            checkDeadline();
            producerRoute.push_back({q, bottom});
        }
        for (int r = bottom + 1; r <= height - 2; ++r) {
            checkDeadline();
            producerRoute.push_back({source, r});
        }
    }
    for (int q = source + (molecular ? 1 : 0); q <= destinationMax; ++q) {
        checkDeadline();
        producerRoute.push_back({q, height - 2});
    }
    const int producerTrack = object("track", producerRoute.front(), 1, 0);
    auto& producerData = result.objects[producerTrack].bodyData;
    producerData = {1, 0, 0, 0, static_cast<int>(producerRoute.size())};
    for (const auto& cell : producerRoute) {
        checkDeadline();
        const auto delta = core::Subtract(cell, producerRoute.front());
        producerData.insert(producerData.end(), {delta.q, delta.r});
    }
    producerData.push_back(0);
    auto routeIndex = [&](int q) {
        return static_cast<int>(
            std::find(producerRoute.begin(), producerRoute.end(), core::HexCoord{q, height - 2}) -
            producerRoute.begin());
    };
    const int arm = object("piston", {source, height - 2}, 1, 1);
    const int deliveryParking = destinationMin - 4;
    track(deliveryParking, destinationMax, 4);
    const int delivery = object("arm1", {deliveryParking, 4}, 3, 1);
    std::vector<int> batchObjects;
    for (auto part : batch.objects) {
        checkDeadline();
        part.instructions.clear();
        batchObjects.push_back(static_cast<int>(result.objects.size()));
        result.objects.push_back(std::move(part));
    }
    int deliveryPosition = deliveryParking;
    int shift = 0;
    for (const auto& event : events) {
        checkDeadline();
        const auto& feed = feeds[event.feed];
        const Machine m{arm, delivery, source, feed.cell.q - 3, height};
        int time = event.time + shift, start = time, position = routeIndex(source);
        std::vector<std::pair<int, io::ArmInstruction>> instructions;
        auto emit = [&](int arm, char action) {
            checkDeadline();
            instructions.push_back({arm, {action, time++}});
        };
        auto move = [&](int q) {
            const int target = routeIndex(q);
            while (position < target) {
                checkDeadline();
                emit(m.arm, 'A');
                ++position;
            }
            while (position > target) {
                checkDeadline();
                emit(m.arm, 'a');
                --position;
            }
        };
        auto transfer = [&](int from, int height, int to) {
            move(from);
            for (int i = 0; i <= height; ++i)
                emit(m.arm, 'E');
            emit(m.arm, 'G');
            for (int i = 0; i <= height; ++i)
                emit(m.arm, 'e');
            move(to);
            emit(m.arm, 'E');
            emit(m.arm, 'g');
            emit(m.arm, 'e');
        };
        auto produce = [&](auto&& self, int level, int to) -> void {
            checkDeadline();
            if (level < 0) {
                if (molecular) {
                    const int end = nextProduct + 1 == productEnds.size()
                                        ? batchDuration
                                        : productEnds.at(nextProduct);
                    for (std::size_t i = 0; i < batch.objects.size(); ++i)
                        for (auto instruction : batch.objects[i].instructions) {
                            checkDeadline();
                            if (instruction.parameter < batchCursor || instruction.parameter >= end)
                                continue;
                            instruction.parameter += time - batchCursor;
                            instructions.push_back({batchObjects[i], instruction});
                        }
                    time += end - batchCursor;
                    batchCursor = end;
                    if (++nextProduct == productEnds.size()) {
                        nextProduct = 0;
                        batchCursor = 0;
                    }
                    transfer(pickup, 1, to);
                } else
                    transfer(m.source, 0, to);
                return;
            }
            int q = m.source + 8 * (level + 1);
            self(self, level - 1, q);
            self(self, level - 1, q + 1);
            emit(m.arm, 'O');
            emit(m.arm, 'O');
            transfer(q, 1, to);
        };
        produce(produce, feed.stages - 1, m.destination);
        move(source);
        while (deliveryPosition < m.destination) {
            checkDeadline();
            emit(m.delivery, 'A');
            ++deliveryPosition;
        }
        while (deliveryPosition > m.destination) {
            checkDeadline();
            emit(m.delivery, 'a');
            --deliveryPosition;
        }
        emit(m.delivery, 'G');
        emit(m.delivery, 'R');
        emit(m.delivery, 'g');
        emit(m.delivery, 'r');
        while (deliveryPosition > deliveryParking) {
            checkDeadline();
            emit(m.delivery, 'a');
            --deliveryPosition;
        }
        const int duration = time - start;
        for (auto& part : result.objects)
            for (auto& instruction : part.instructions) {
                checkDeadline();
                if (instruction.parameter >= start) instruction.parameter += duration;
            }
        for (const auto& [arm, instruction] : instructions) {
            checkDeadline();
            result.objects[arm].instructions.push_back(instruction);
        }
        shift += duration;
    }
    if (events.empty()) throw std::logic_error("purified feed has no pickups");
    if (nextProduct != 0 || batchCursor != 0)
        throw std::invalid_argument("purification molecular feed period does not close");
    int end = 0;
    for (const auto& part : result.objects)
        for (const auto& instruction : part.instructions) {
            checkDeadline();
            end = std::max(end, instruction.parameter + 1);
        }
    int armIndex = 0;
    for (auto& part : result.objects) {
        checkDeadline();
        if (part.typeName == "piston" || part.typeName == "arm1") part.bodyData[4] = armIndex++;
        if (repeating && part.typeName == "out-std") part.typeName = "out-rep";
        if (!part.instructions.empty())
            part.instructions.push_back({repeating || finiteFeed ? 'B' : 'g', end});
    }
    result.solutionName =
        repeating ? "Purified six-monomer row assembly" : "Purified metal row assembly";
    checkDeadline();
    return result;
}
} // namespace opusoptima::solvers
