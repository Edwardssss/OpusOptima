#include "FixedArmJoin.hpp"
#include "CandidateIdentity.hpp"
#include "FixedArmIntervals.hpp"

#include "ConstraintLayout.hpp"
#include "ExecutionPlan.hpp"
#include "InventoryIR.hpp"
#include "InventorySearch.hpp"
#include "SupplyExecutionIR.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <limits>
#include <set>

namespace opusoptima::solvers {
namespace {
using IR = ExecutionIR;
using Coord = core::HexCoord;
Coord Turn(Coord cell, int turns) {
    for (int i = 0; i < turns; ++i)
        cell = core::RotateClockwise(cell);
    return cell;
}
struct JoinMapping {
    std::vector<std::size_t> targetForSource;
    std::size_t newTarget{};
};
std::vector<JoinMapping> FindJoins(const io::Molecule& fragment, const io::Molecule& singleton,
                                   const io::Molecule& output) {
    std::vector<JoinMapping> mappings;
    if (output.atoms.size() != fragment.atoms.size() + 1 ||
        output.bonds.size() != fragment.bonds.size() + 1)
        return mappings;
    for (std::size_t addition = 0; addition < output.atoms.size(); ++addition) {
        if (output.atoms[addition].type != singleton.atoms[0].type) continue;
        for (std::size_t anchor = 0; anchor < output.atoms.size(); ++anchor) {
            if (anchor == addition || output.atoms[anchor].type != fragment.atoms[0].type) continue;
            const auto bridge =
                std::find_if(output.bonds.begin(), output.bonds.end(), [&](const auto& bond) {
                    return bond.bits == 1 && ((bond.from == output.atoms[anchor].position &&
                                               bond.to == output.atoms[addition].position) ||
                                              (bond.to == output.atoms[anchor].position &&
                                               bond.from == output.atoms[addition].position));
                });
            if (bridge == output.bonds.end()) continue;
            if (std::count_if(output.bonds.begin(), output.bonds.end(), [&](const auto& bond) {
                    return bond.from == output.atoms[addition].position ||
                           bond.to == output.atoms[addition].position;
                }) != 1)
                continue;
            for (int rotation = 0; rotation < 6; ++rotation) {
                JoinMapping mapping;
                mapping.newTarget = addition;
                std::set<std::size_t> used;
                bool fits = true;
                for (const auto& source : fragment.atoms) {
                    const auto cell =
                        core::Add(output.atoms[anchor].position,
                                  Turn(core::Subtract(source.position, fragment.atoms[0].position),
                                       rotation));
                    const auto found = std::find_if(
                        output.atoms.begin(), output.atoms.end(), [&](const auto& atom) {
                            return atom.position == cell && atom.type == source.type;
                        });
                    if (found == output.atoms.end()) {
                        fits = false;
                        break;
                    }
                    const auto index = static_cast<std::size_t>(found - output.atoms.begin());
                    if (index == addition || !used.insert(index).second) {
                        fits = false;
                        break;
                    }
                    mapping.targetForSource.push_back(index);
                }
                if (!fits) continue;
                for (const auto& bond : fragment.bonds) {
                    const auto desired = [&](Coord cell) {
                        return core::Add(
                            output.atoms[anchor].position,
                            Turn(core::Subtract(cell, fragment.atoms[0].position), rotation));
                    };
                    const auto a = desired(bond.from), b = desired(bond.to);
                    fits &= std::any_of(
                        output.bonds.begin(), output.bonds.end(), [&](const auto& goal) {
                            return goal.bits == bond.bits && ((goal.from == a && goal.to == b) ||
                                                              (goal.to == a && goal.from == b));
                        });
                }
                if (fits) mappings.push_back(std::move(mapping));
            }
        }
    }
    return mappings;
}
} // namespace

std::optional<io::SolutionData> CompileAcceptedFixedArmJoin(
    const io::PuzzleData& p, const std::function<bool(const io::SolutionData&)>& accept) {
    constexpr auto required = (1ULL << 0) | (1ULL << 8) | (1ULL << 22) | (1ULL << 23);
    if (p.version != 3 || p.productionInfo || p.outputMultiplier <= 0 ||
        p.outputMultiplier > std::numeric_limits<int>::max() / 24 ||
        (p.permissionBits & required) != required || p.inputs.size() != 2 ||
        p.outputs.size() != 1 || p.inputs[0].atoms.empty() || p.inputs[0].atoms.size() > 16 ||
        p.inputs[1].atoms.size() != 1 || !p.inputs[1].bonds.empty())
        return {};
    for (const auto* molecules : {&p.inputs, &p.outputs})
        for (const auto& molecule : *molecules)
            for (const auto& atom : molecule.atoms)
                if (atom.position.q < -120 || atom.position.q > 120 || atom.position.r < -120 ||
                    atom.position.r > 120)
                    return {};
    try {
        auto initial = IR::Initial(p);
        IR check;
        check.Append(p, initial, {IR::Kind::Acquire, 0, {}});
        if (IR::Fragment(initial, 1).size() != p.inputs[0].atoms.size()) return {};
    } catch (const IR::Error&) {
        return {};
    }
    const auto& fragment = p.inputs[0];
    const auto& outputMolecule = p.outputs[0];
    const auto mappings = FindJoins(fragment, p.inputs[1], outputMolecule);
    if (mappings.empty()) return {};
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    std::optional<io::SolutionData> accepted;
    std::set<std::string> testedMachines;
    unsigned candidates = 0;
    for (const auto& mapping : mappings) {
        for (int outputTurn = 0; outputTurn < 6; ++outputTurn) {
            const auto delta =
                Turn(core::Subtract(outputMolecule.atoms[mapping.newTarget].position,
                                    outputMolecule.atoms[mapping.targetForSource[0]].position),
                     outputTurn);
            if (delta != core::Subtract(Coord{0, 1}, Coord{1, 0})) continue;
            const core::HexFrame product{
                core::Subtract(
                    Coord{1, 0},
                    Turn(outputMolecule.atoms[mapping.targetForSource[0]].position, outputTurn)),
                outputTurn};
            for (int inputTurn = 0; inputTurn < 6; ++inputTurn) {
                const core::HexFrame source{
                    core::Subtract(Coord{0, -1}, Turn(fragment.atoms[0].position, inputTurn)),
                    inputTurn};
                bool aligned = true;
                for (std::size_t atom = 0; atom < fragment.atoms.size(); ++atom)
                    aligned &= Turn(source.ToWorld(fragment.atoms[atom].position), 2) ==
                               product.ToWorld(
                                   outputMolecule.atoms[mapping.targetForSource[atom]].position);
                if (!aligned) continue;
                const auto second =
                    core::HexFrame{core::Subtract(Coord{-1, 1}, p.inputs[1].atoms[0].position), 0};
                std::set<std::pair<int, int>> footprint{{0, 0}, {0, -1}, {-1, 1}, {1, 0}, {0, 1}};
                bool clear = true;
                for (const auto& atom : fragment.atoms) {
                    const auto cell = source.ToWorld(atom.position);
                    clear &= cell != Coord{} && cell != Coord{-1, 1};
                    footprint.emplace(cell.q, cell.r);
                }
                for (const auto& atom : outputMolecule.atoms) {
                    const auto cell = product.ToWorld(atom.position);
                    clear &= cell != Coord{} && cell != Coord{0, -1} && cell != Coord{-1, 1};
                    footprint.emplace(cell.q, cell.r);
                }
                if (!clear) continue;
                ConstraintLayoutProblem layout;
                ConstraintModule station;
                for (const auto& [q, r] : footprint)
                    station.footprint.push_back({q, r});
                station.domain = MakeConstraintPlacementDomain({{0, 0}, {8, 0}, {-8, 0}}, {0});
                layout.modules = {station};
                const auto placed = SearchConstraintLayout(
                    layout, {.maxNodes = 4, .maxCandidates = 3},
                    [&](const ConstraintLayout& found) {
                        if (candidates >= 64 || std::chrono::steady_clock::now() >= deadline)
                            return false;
                        const auto base = found[0].placement.translation;
                        const std::vector<Coord> track{base};
                        const auto firstCell = core::Add(base, {1, 0});
                        auto firstInput = source, secondInput = second, outputFrame = product;
                        firstInput.origin = core::Add(source.origin, base);
                        secondInput.origin = core::Add(second.origin, base);
                        outputFrame.origin = core::Add(product.origin, base);
                        JointSupplyPlan supply;
                        supply.productsPerPeriod = 1;
                        supply.usage = {{1, fragment.atoms.size(), 0}, {1, 1, 0}};
                        for (std::size_t atom = 0; atom < fragment.atoms.size(); ++atom)
                            supply.steps.push_back(
                                {{SupplyPlan::Kind::Take, 0, atom, mapping.targetForSource[atom]},
                                 0});
                        supply.steps.push_back(
                            {{SupplyPlan::Kind::Take, 1, 0, mapping.newTarget}, 0});
                        IR program;
                        try {
                            program = BuildJointSupplyExecutionIR(p, supply, {outputFrame},
                                                                  {firstInput, secondInput});
                        } catch (const IR::Error&) {
                            return false;
                        }
                        const HeldArmState start{0, 1, 4, 0};
                        std::vector<Coord> inputCells;
                        for (std::size_t sourceIndex = 0; sourceIndex < p.inputs.size();
                             ++sourceIndex)
                            for (const auto& atom : p.inputs[sourceIndex].atoms)
                                inputCells.push_back((sourceIndex == 0 ? firstInput : secondInput)
                                                         .ToWorld(atom.position));
                        const auto motion = [&](const IR::State& state, const HeldArmState& from,
                                                const HeldArmState& to, char instruction,
                                                bool holding) {
                            if ((instruction == 'p' || instruction == 'P') &&
                                !(p.permissionBits & (1ULL << 26)))
                                return false;
                            return CertifyFixedArmInterval(state, base, inputCells, from, to,
                                                           instruction, holding);
                        };
                        const auto glyph = [&](const IR::Step& step, const HeldArmState&) {
                            return std::any_of(
                                program.steps.begin(), program.steps.end(),
                                [&](const auto& expected) {
                                    return expected.operation.kind == step.operation.kind &&
                                           expected.preconditions.atoms ==
                                               step.preconditions.atoms &&
                                           expected.effects.atoms == step.effects.atoms &&
                                           expected.effects.bonds == step.effects.bonds;
                                });
                        };
                        PhysicalPlan physical;
                        int previous = 0;
                        std::optional<PhysicalInventory> inventory;
                        try {
                            inventory.emplace(LowerInventoryIR(
                                p, program, track, start, motion, glyph, 1, 3000, {}, {},
                                [&](std::size_t step, const PhysicalInventory& committed) {
                                    std::optional<IR::AtomId> held;
                                    if (auto it = committed.Custody().Held().find(0);
                                        it != committed.Custody().Held().end())
                                        held = it->second;
                                    const int end = static_cast<int>(committed.Tape().size());
                                    physical.assignments.push_back(
                                        {step,
                                         PhysicalPlan::Mode::FixedArm,
                                         {},
                                         0,
                                         program.steps[step].operation.kind == IR::Kind::Output
                                             ? std::optional<std::size_t>{0}
                                             : std::nullopt,
                                         held,
                                         core::Add(base, Turn({1, 0}, committed.Arm().direction)),
                                         committed.Arm().fragmentRotation,
                                         previous,
                                         end});
                                    previous = end;
                                },
                                true));
                            if (inventory->MoveEmpty(start, 3000) != MotionStatus::Found)
                                return false;
                            ValidatePhysicalPlan(MakeExecutionPlan(p, program), physical);
                        } catch (const IR::Error&) {
                            return false;
                        }
                        io::SolutionData candidate;
                        candidate.puzzleName = p.name;
                        candidate.solutionName = "Fixed-arm joint fragment inventory";
                        io::SolutionObject arm;
                        arm.typeName = "arm1";
                        arm.position = base;
                        int time = 0;
                        for (char instruction : inventory->Tape())
                            arm.instructions.push_back({instruction, time++});
                        const auto completionCycles = 6LL * p.outputMultiplier * time;
                        if (completionCycles > std::numeric_limits<std::int32_t>::max())
                            return false;
                        candidate.metrics.cycles = static_cast<std::int32_t>(completionCycles);
                        arm.bodyData = {1, 4, 0, time, 0};
                        candidate.objects.push_back(std::move(arm));
                        candidate.objects.push_back(
                            {"input", firstInput.origin, {}, {1, firstInput.Turns(), 0, 0, 0}});
                        candidate.objects.push_back(
                            {"input", secondInput.origin, {}, {1, secondInput.Turns(), 1, 0, 0}});
                        candidate.objects.push_back({"bonder", firstCell, {}, {1, 2, 0, 0, 0}});
                        candidate.objects.push_back(
                            {"out-std", outputFrame.origin, {}, {1, outputFrame.Turns(), 0, 0, 0}});
                        if (!testedMachines.insert(PhysicalCandidateKey(candidate)).second)
                            return false;
                        ++candidates;
                        if (!accept(candidate)) return false;
                        accepted = std::move(candidate);
                        return true;
                    });
                if (placed.status == ConstraintLayoutStatus::Found) return accepted;
            }
        }
    }
    return {};
}
} // namespace opusoptima::solvers
