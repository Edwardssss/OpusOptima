#include "ProductionChemistry.hpp"
#include "ProductionLayout.hpp"

#include <algorithm>
#include <array>
#include <stdexcept>

namespace opusoptima::solvers {
namespace {
[[noreturn]] void Reject() {
    throw std::invalid_argument(
        "production chemistry requires one static chamber without conduits: cardinal-to-salt "
        "singleton, two-singleton ordinary/Fire color-mask pair, complete animismus coproducts, or "
        "complete dispersion coproducts with optional selective calcification");
}
bool Bounded(core::HexCoord c) {
    return c.q >= -120 && c.q <= 120 && c.r >= -120 && c.r <= 120;
}
bool Cardinal(core::AtomType t) {
    return t == core::AtomType::Air || t == core::AtomType::Earth || t == core::AtomType::Fire ||
           t == core::AtomType::Water;
}
core::HexCoord Rotate(core::HexCoord c, int n) {
    while (n-- > 0)
        c = core::RotateClockwise(c);
    return c;
}
} // namespace

io::SolutionData CompileProductionChemistry(const io::PuzzleData& p) {
    constexpr auto armPermissions = (1ULL << 0) | (1ULL << 22) | (1ULL << 23);
    if (p.version == 3 && p.productionInfo && p.outputMultiplier == 1 && p.inputs.size() >= 1 &&
        p.inputs.size() <= 2 && p.outputs.size() == 2 && (p.permissionBits & (1ULL << 16))) {
        const auto& info = *p.productionInfo;
        if (!info.conduits.empty() || info.chambers.size() != 1) Reject();
        const auto layouts = PlanProductionChambers(p);
        for (const auto& input : p.inputs)
            if (input.atoms.size() != 1 || !input.bonds.empty() ||
                !Bounded(input.atoms[0].position) ||
                (input.atoms[0].type != core::AtomType::Salt &&
                 !(Cardinal(input.atoms[0].type) && (p.permissionBits & (1ULL << 12)))))
                Reject();
        bool seen[2]{};
        const auto center = layouts[0].center;
        io::SolutionData result;
        result.puzzleName = p.name;
        result.solutionName = "Joint production animismus coproducts";
        result.objects.push_back({"glyph-life-and-death", center, {}, {1, 0, 0, 0, 0}});
        for (int lane = 0; lane < 2; ++lane) {
            const auto cell = core::Add(center, {lane, 0});
            if (!ContainsProductionCell(layouts[0], cell)) Reject();
            const auto source = static_cast<std::size_t>(lane) % p.inputs.size();
            result.objects.push_back({"input",
                                      core::Subtract(cell, p.inputs[source].atoms[0].position),
                                      {},
                                      {1, 0, static_cast<int>(source), 0, 0}});
            if (p.inputs[source].atoms[0].type != core::AtomType::Salt)
                result.objects.push_back({"glyph-calcification", cell, {}, {1, 0, 0, 0, 0}});
        }
        for (std::size_t i = 0; i < p.outputs.size(); ++i) {
            const auto& output = p.outputs[i];
            if (output.atoms.size() != 1 || !output.bonds.empty() ||
                !Bounded(output.atoms[0].position))
                Reject();
            const auto type = output.atoms[0].type;
            if (type != core::AtomType::Vitae && type != core::AtomType::Mors) Reject();
            const int lane = type == core::AtomType::Mors;
            if (seen[lane]) Reject();
            seen[lane] = true;
            const auto cell =
                core::Add(center, lane ? core::HexCoord{1, -1} : core::HexCoord{0, 1});
            if (!ContainsProductionCell(layouts[0], cell)) Reject();
            result.objects.push_back({"out-std",
                                      core::Subtract(cell, output.atoms[0].position),
                                      {},
                                      {1, 0, static_cast<int>(i), 0, 0}});
        }
        return result;
    }
    if (p.version == 3 && p.productionInfo && p.outputMultiplier == 1 && p.inputs.size() == 1 &&
        p.inputs[0].atoms.size() == 1 && p.inputs[0].bonds.empty() &&
        p.inputs[0].atoms[0].type == core::AtomType::Quintessence &&
        Bounded(p.inputs[0].atoms[0].position) && p.outputs.size() == 4 &&
        (p.permissionBits & (1ULL << 18))) {
        const auto& info = *p.productionInfo;
        if (!info.conduits.empty() || info.chambers.size() != 1) Reject();
        const auto layouts = PlanProductionChambers(p);
        const core::HexCoord directions[] = {{-1, 0}, {1, 0}, {0, -1}, {1, -1}};
        std::array<int, 4> lanes{0, 1, 2, 3};
        const auto matches = [&](const auto& assignment) {
            for (std::size_t i = 0; i < p.outputs.size(); ++i) {
                const auto& output = p.outputs[i];
                if (output.atoms.size() != 1 || !output.bonds.empty() ||
                    !Bounded(output.atoms[0].position))
                    return false;
                const auto target = output.atoms[0].type;
                if (target != static_cast<core::AtomType>(assignment[i] + 2) &&
                    !(target == core::AtomType::Salt && (p.permissionBits & (1ULL << 12))))
                    return false;
            }
            return true;
        };
        while (!matches(lanes))
            if (!std::next_permutation(lanes.begin(), lanes.end())) Reject();
        io::SolutionData result;
        result.puzzleName = p.name;
        result.solutionName = "Joint production dispersion coproducts";
        const auto center = layouts[0].center;
        result.objects.push_back(
            {"input", core::Subtract(center, p.inputs[0].atoms[0].position), {}, {1, 0, 0, 0, 0}});
        result.objects.push_back({"glyph-dispersion", center, {}, {1, 0, 0, 0, 0}});
        for (std::size_t i = 0; i < p.outputs.size(); ++i) {
            const auto& output = p.outputs[i];
            if (output.atoms.size() != 1 || !output.bonds.empty() ||
                !Bounded(output.atoms[0].position))
                Reject();
            const int lane = lanes[i];
            const auto cell = core::Add(center, directions[lane]);
            if (!ContainsProductionCell(layouts[0], cell)) Reject();
            if (output.atoms[0].type == core::AtomType::Salt)
                result.objects.push_back({"glyph-calcification", cell, {}, {1, 0, 0, 0, 0}});
            result.objects.push_back({"out-std",
                                      core::Subtract(cell, output.atoms[0].position),
                                      {},
                                      {1, 0, static_cast<int>(i), 0, 0}});
        }
        return result;
    }
    if (p.version != 3 || !p.productionInfo || p.outputMultiplier != 1 || p.outputs.size() != 1 ||
        (p.permissionBits & armPermissions) != armPermissions)
        Reject();
    const auto& info = *p.productionInfo;
    if (!info.conduits.empty() || info.chambers.size() != 1) Reject();
    const auto layouts = PlanProductionChambers(p);
    const auto& chamber = info.chambers[0];
    const bool pair = p.outputs[0].atoms.size() == 2;
    const int bondMask = pair && p.outputs[0].bonds.size() == 1 ? p.outputs[0].bonds[0].bits : 0;
    const bool coloredPair = pair && bondMask != 1;
    if (p.inputs.size() != (pair ? 2U : 1U) || p.outputs[0].atoms.size() != (pair ? 2U : 1U) ||
        p.outputs[0].bonds.size() != (pair ? 1U : 0U))
        Reject();
    for (const auto* molecules : {&p.inputs, &p.outputs})
        for (const auto& m : *molecules)
            for (const auto& a : m.atoms)
                if (!Bounded(a.position) || a.type == core::AtomType::Repeat ||
                    !core::AtomTypeFromId(static_cast<std::uint8_t>(a.type)))
                    Reject();
    for (const auto& m : p.inputs)
        if (m.atoms.size() != 1 || !m.bonds.empty()) Reject();
    const auto& out = p.outputs[0];
    int outputRotation = 0;
    if (pair) {
        if (!(p.permissionBits & (1ULL << (coloredPair ? 11 : 8)))) Reject();
        const auto a = out.atoms[0].position, b = out.atoms[1].position;
        const auto& bond = out.bonds[0];
        if ((bond.bits != 1 && (bond.bits <= 0 || (bond.bits & ~14))) ||
            core::Distance(a, b) != 1 ||
            !((bond.from == a && bond.to == b) || (bond.from == b && bond.to == a)))
            Reject();
        while (outputRotation < 6 &&
               Rotate(core::Subtract(b, a), outputRotation) != core::HexCoord{0, 1})
            ++outputRotation;
        if (outputRotation == 6) Reject();
    }
    bool calcify[2]{};
    std::size_t sourceForTarget[]{0, 1};
    const auto compatible = [&](std::size_t sourceIndex, std::size_t targetIndex) {
        const auto source = p.inputs[sourceIndex].atoms[0].type;
        const auto target = out.atoms[targetIndex].type;
        return source == target || (Cardinal(source) && target == core::AtomType::Salt &&
                                    (p.permissionBits & (1ULL << 12)));
    };
    if (pair && (!compatible(0, 0) || !compatible(1, 1))) {
        if (!compatible(1, 0) || !compatible(0, 1)) Reject();
        std::swap(sourceForTarget[0], sourceForTarget[1]);
    }
    for (std::size_t i = 0; i < p.inputs.size(); ++i) {
        const auto source = p.inputs[sourceForTarget[i]].atoms[0].type, target = out.atoms[i].type;
        calcify[i] = source != target;
        if (calcify[i] && (!Cardinal(source) || target != core::AtomType::Salt ||
                           !(p.permissionBits & (1ULL << 12))))
            Reject();
        if (!pair && !calcify[i]) Reject();
    }
    if (coloredPair &&
        (out.atoms[0].type != core::AtomType::Fire || out.atoms[1].type != core::AtomType::Fire))
        Reject();
    std::vector<std::pair<core::HexCoord, int>> prisma;
    core::HexCoord machineOrigin = chamber.position;
    int machineRotation = 0;
    bool placed = false;
    auto origins = layouts[0].interior;
    origins.insert(origins.begin(), chamber.position);
    for (auto origin : origins) {
        for (int rotation = 0; rotation < 6 && !placed; ++rotation) {
            bool fits = true;
            for (auto cell : {core::HexCoord{}, core::HexCoord{1, 0}, core::HexCoord{0, 1},
                              core::HexCoord{pair ? 2 : 1, 0}, core::HexCoord{0, pair ? 2 : 1}})
                fits &=
                    ContainsProductionCell(layouts[0], core::Add(origin, Rotate(cell, rotation)));
            std::vector<std::pair<core::HexCoord, int>> candidatePrisma;
            if (fits && coloredPair) {
                for (int color : {2, 4, 8}) {
                    if (!(bondMask & color)) continue;
                    bool found = false;
                    for (int angle = 0; angle < 6 && !found; ++angle) {
                        const core::HexCoord vertices[]{Rotate({}, angle), Rotate({0, 1}, angle),
                                                        Rotate({1, 0}, angle)};
                        const int colors[]{8, 2, 4};
                        for (int edge = 0; edge < 3 && !found; ++edge) {
                            if (colors[edge] != color) continue;
                            for (bool reverse : {false, true}) {
                                const auto offset = core::Subtract(reverse ? core::HexCoord{2, 0}
                                                                           : core::HexCoord{1, 0},
                                                                   vertices[edge]);
                                if (core::Add(offset, vertices[(edge + 1) % 3]) !=
                                    (reverse ? core::HexCoord{1, 0} : core::HexCoord{2, 0}))
                                    continue;
                                bool contained = true;
                                for (auto vertex : vertices)
                                    contained &= ContainsProductionCell(
                                        layouts[0],
                                        core::Add(origin,
                                                  Rotate(core::Add(offset, vertex), rotation)));
                                if (!contained) continue;
                                candidatePrisma.emplace_back(offset, angle);
                                found = true;
                                break;
                            }
                        }
                    }
                    if (!found) {
                        fits = false;
                        break;
                    }
                }
            }
            if (fits) {
                prisma = std::move(candidatePrisma);
                machineOrigin = origin;
                machineRotation = rotation;
                placed = true;
            }
        }
        if (placed) break;
    }
    if (!placed) Reject();
    io::SolutionData result;
    result.puzzleName = p.name;
    result.solutionName = "Bounded production chemistry and assembly";
    auto add = [&](const char* name, core::HexCoord position, int angle = 0,
                   int index = 0) -> io::SolutionObject& {
        result.objects.push_back({name,
                                  core::Add(machineOrigin, Rotate(position, machineRotation)),
                                  {},
                                  {1, (angle + machineRotation) % 6, index, 0, 0}});
        return result.objects.back();
    };
    const int reach = 1;
    auto& arm = add("arm1", {});
    arm.bodyData = {reach, machineRotation, 0, 4, 0};
    // One idle cycle before each grab lets freshly spawned feeds calcify/bond.
    arm.instructions = {{'G', 1}, {'r', 2}, {'g', 3}, {'R', 4}};
    for (std::size_t i = 0; i < p.inputs.size(); ++i) {
        const core::HexCoord target{reach + static_cast<int>(i), 0};
        const auto sourceIndex = sourceForTarget[i];
        add("input", core::Subtract(target, p.inputs[sourceIndex].atoms[0].position), 0,
            static_cast<int>(sourceIndex));
        if (calcify[i]) add("glyph-calcification", target);
    }
    if (pair && !coloredPair) add("bonder", {reach, 0});
    for (const auto& [position, angle] : prisma)
        add("bonder-prisma", position, angle);
    add("out-std", core::Subtract({0, reach}, Rotate(out.atoms[0].position, outputRotation)),
        outputRotation);
    return result;
}
} // namespace opusoptima::solvers
