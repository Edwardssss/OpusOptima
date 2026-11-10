#include "AssemblySolver.hpp"

#include <stdexcept>

namespace opusoptima::solvers {
io::SolutionData CompileMetalPurification(const io::PuzzleData& p) {
    constexpr auto armPermissions = (1ULL << 0) | (1ULL << 22) | (1ULL << 23);
    if (p.version != 3 || p.productionInfo || p.outputMultiplier <= 0 || p.outputs.size() != 1 ||
        (p.permissionBits & armPermissions) != armPermissions || p.outputs[0].atoms.size() != 1 ||
        !p.outputs[0].bonds.empty() || !(p.permissionBits & (1ULL << 15)))
        throw std::invalid_argument("unsupported standalone purification target");
    const int target = static_cast<int>(p.outputs[0].atoms[0].type);
    if (target < 7 || target >= 12)
        throw std::invalid_argument("purification requires a higher metal target");
    int source = -1;
    for (int i = 0; i < static_cast<int>(p.inputs.size()); ++i)
        if (p.inputs[i].atoms.size() == 1 && p.inputs[i].bonds.empty() &&
            static_cast<int>(p.inputs[i].atoms[0].type) > target &&
            static_cast<int>(p.inputs[i].atoms[0].type) <= 12 &&
            (source < 0 || p.inputs[i].atoms[0].type < p.inputs[source].atoms[0].type)) {
            source = i;
        }
    if (source < 0)
        throw std::invalid_argument("purification requires single-atom lower metal feed");
    io::SolutionData result;
    result.puzzleName = p.name;
    result.solutionName = "Recursive metal purification";
    auto object = [&](const char* type, core::HexCoord pos, int length, int rotation,
                      int index = 0) {
        io::SolutionObject obj;
        obj.typeName = type;
        obj.position = pos;
        obj.bodyData = {length, rotation, index, 0, static_cast<int>(result.objects.size())};
        result.objects.push_back(obj);
    };
    const int stages = static_cast<int>(p.inputs[source].atoms[0].type) - target;
    if (stages == 1) {
        for (auto cell : {core::HexCoord{0, 0}, core::HexCoord{1, 0}})
            object("input", core::Subtract(cell, p.inputs[source].atoms[0].position), 1, 0, source);
        object("glyph-purification", {0, 0}, 1, 0);
        object("arm1", {0, 2}, 1, 4);
        result.objects.back().instructions = {{'G', 2}, {'R', 3}, {'g', 4}, {'r', 5}};
        object("out-std", core::Subtract(core::HexCoord{-1, 2}, p.outputs[0].atoms[0].position), 1,
               0);
        return result;
    }
    if ((p.permissionBits & ((1ULL << 2) | (1ULL << 3))) != ((1ULL << 2) | (1ULL << 3)))
        throw std::invalid_argument("recursive purification requires piston and track");
    const int destination = 8 * (stages - 1) + 4;
    object("input", core::Subtract(core::HexCoord{-8, 0}, p.inputs[source].atoms[0].position), 1, 0,
           source);
    for (int level = 0; level < stages; ++level)
        object("glyph-purification", {8 * level, 0}, 1, 0);
    object("track", {-8, -2}, 1, 0);
    auto& track = result.objects.back().bodyData;
    track = {1, 0, 0, 0, destination + 9};
    for (int q = 0; q <= destination + 8; ++q) {
        track.push_back(q);
        track.push_back(0);
    }
    track.push_back(0);
    object("piston", {-8, -2}, 1, 1);
    const auto arm = result.objects.size() - 1;
    int time = 0, position = -8;
    auto emit = [&](char action) { result.objects[arm].instructions.push_back({action, time++}); };
    auto move = [&](int q) {
        while (position < q) {
            emit('A');
            ++position;
        }
        while (position > q) {
            emit('a');
            --position;
        }
    };
    auto transfer = [&](int from, int height, int to) {
        move(from);
        for (int i = 0; i < height + 1; ++i)
            emit('E');
        emit('G');
        for (int i = 0; i < height + 1; ++i)
            emit('e');
        move(to);
        emit('E');
        emit('g');
        emit('e');
    };
    auto produce = [&](auto&& self, int level, int to) -> void {
        if (level < 0) {
            transfer(-8, 0, to);
            return;
        }
        self(self, level - 1, 8 * level);
        self(self, level - 1, 8 * level + 1);
        emit('O');
        emit('O');
        transfer(8 * level, 1, to);
    };
    produce(produce, stages - 1, destination);
    move(-8);
    object("out-std",
           core::Subtract(core::HexCoord{destination, 0}, p.outputs[0].atoms[0].position), 1, 0);
    return result;
}
} // namespace opusoptima::solvers
