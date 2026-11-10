#include "ProductionMolecular.hpp"
#include "ProductionLayout.hpp"
#include <algorithm>
#include <stdexcept>

namespace opusoptima::solvers {
namespace {
using core::HexCoord;
[[noreturn]] void Reject() {
    throw std::invalid_argument(
        "production molecular: requires a uniform triangular two-bond trimer, singleton product, "
        "two large chambers and matching three-cell conduit");
}
} // namespace
io::SolutionData CompileProductionMolecular(const io::PuzzleData& p) {
    constexpr auto permissions = (1ULL << 0) | (1ULL << 8) | (1ULL << 9) | (1ULL << 17) |
                                 (1ULL << 22) | (1ULL << 23) | (1ULL << 25);
    if (p.version != 3 || !p.productionInfo || p.outputMultiplier != 1 ||
        (p.permissionBits & permissions) != permissions || p.outputs.size() != 1 ||
        p.outputs[0].atoms.size() != 1 || !p.outputs[0].bonds.empty())
        Reject();
    const auto& info = *p.productionInfo;
    // Vial-bearing candidates still require the publication layer's independent
    // production-profile check; candidate construction is not acceptance.
    if (info.chambers.size() != 2 || info.conduits.size() != 1) Reject();
    const auto chambers = PlanProductionChambers(p);
    if (info.chambers[0].chamberType != "Large" || info.chambers[1].chamberType != "Large")
        Reject();
    const std::vector<HexCoord> shape{{0, 0}, {0, 1}, {1, 0}};
    const auto& pipe = info.conduits[0];
    if (pipe.path != shape) Reject();
    int source = -1, destination = -1;
    for (int i = 0; i < 2; ++i) {
        bool a = true, b = true;
        for (auto cell : shape) {
            a &= ContainsProductionCell(chambers[i], core::Add(pipe.positionA, cell));
            b &= ContainsProductionCell(chambers[i], core::Add(pipe.positionB, cell));
        }
        if (a) source = i;
        if (b) destination = i;
    }
    if (source < 0 || destination < 0 || source == destination) Reject();
    int input = -1;
    const auto type = p.outputs[0].atoms[0].type;
    if (type == core::AtomType::Repeat) Reject();
    for (std::size_t i = 0; i < p.inputs.size(); ++i) {
        const auto& m = p.inputs[i];
        if (m.atoms.size() != 3 || m.bonds.size() != 2) continue;
        bool matches = true;
        for (auto cell : shape)
            matches &= std::count_if(m.atoms.begin(), m.atoms.end(), [&](auto atom) {
                           return atom.position == cell && atom.type == type;
                       }) == 1;
        for (const auto& bond : m.bonds)
            matches &=
                bond.bits == 1 && ((bond.from == HexCoord{0, 0} &&
                                    (bond.to == HexCoord{1, 0} || bond.to == HexCoord{0, 1})) ||
                                   (bond.to == HexCoord{0, 0} &&
                                    (bond.from == HexCoord{1, 0} || bond.from == HexCoord{0, 1})));
        const auto hasEdge = [&](HexCoord leaf) {
            return std::count_if(m.bonds.begin(), m.bonds.end(), [&](const auto& bond) {
                       return (bond.from == HexCoord{0, 0} && bond.to == leaf) ||
                              (bond.to == HexCoord{0, 0} && bond.from == leaf);
                   }) == 1;
        };
        if (matches && hasEdge({1, 0}) && hasEdge({0, 1})) {
            input = static_cast<int>(i);
            break;
        }
    }
    if (input < 0) Reject();
    io::SolutionData result;
    result.puzzleName = p.name;
    result.solutionName = "Triangular molecular closure and conduit fragmentation";
    auto add = [&](const char* name, HexCoord pos, std::vector<int> body,
                   std::vector<io::ArmInstruction> tape = {}) {
        result.objects.push_back({name, pos, std::move(tape), std::move(body)});
    };
    auto s = chambers[source].center, d = chambers[destination].center;
    for (auto center : {s, d}) {
        std::vector<int> body{1, 1, 0, 0, 0, 100, 3};
        for (auto cell : shape) {
            body.push_back(cell.q);
            body.push_back(cell.r);
        }
        add("pipe", core::Add(center, {0, 1}), std::move(body));
    }
    add("input", core::Add(s, {1, 0}), {1, 0, input, 0, 0});
    add("bonder", core::Add(s, {2, 0}), {1, 2, 0, 0, 0});
    add("arm1", s, {1, 0, 0, 5, 0}, {{'G', 0}, {'r', 1}, {'g', 2}, {'R', 3}, {'O', 7}});
    // Fragment the transported ring; the two surplus atoms are disposed, not accumulated.
    add("unbonder", core::Add(d, {0, 1}), {1, 1, 0, 0, 0});
    add("unbonder", core::Add(d, {0, 1}), {1, 2, 0, 0, 0});
    add("unbonder", core::Add(d, {-1, 2}), {1, 0, 0, 0, 0});
    add("out-std", core::Subtract(core::Add(d, {0, 1}), p.outputs[0].atoms[0].position),
        {1, 0, 0, 0, 0});
    add("glyph-disposal", core::Add(d, {0, 2}), {1, 0, 0, 0, 0});
    add("glyph-disposal", core::Add(d, {-1, 2}), {1, 0, 0, 0, 0});
    return result;
}
} // namespace opusoptima::solvers
