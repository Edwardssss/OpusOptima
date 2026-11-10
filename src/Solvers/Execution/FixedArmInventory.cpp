#include "FixedArmInventory.hpp"
#include "CandidateIdentity.hpp"
#include "FixedArmIntervals.hpp"

#include "ConstraintLayout.hpp"
#include "ExecutionPlan.hpp"
#include "InventoryIR.hpp"
#include "InventorySearch.hpp"
#include "SupplyExecutionIR.hpp"

#include <algorithm>
#include <chrono>
#include <limits>
#include <numeric>
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
std::optional<std::vector<std::size_t>> MatchMolecule(const io::Molecule& input,
                                                      const io::Molecule& output) {
    if (input.atoms.size() != output.atoms.size() || input.bonds.size() != output.bonds.size())
        return {};
    for (int rotation = 0; rotation < 6; ++rotation)
        for (std::size_t anchor = 0; anchor < output.atoms.size(); ++anchor) {
            if (input.atoms.front().type != output.atoms[anchor].type) continue;
            std::vector<std::size_t> targetForSource(input.atoms.size());
            std::set<std::size_t> used;
            bool matches = true;
            for (std::size_t source = 0; source < input.atoms.size(); ++source) {
                const auto relative =
                    core::Subtract(input.atoms[source].position, input.atoms.front().position);
                const auto position =
                    core::Add(output.atoms[anchor].position, Turn(relative, rotation));
                const auto target =
                    std::find_if(output.atoms.begin(), output.atoms.end(), [&](const auto& atom) {
                        return atom.position == position && atom.type == input.atoms[source].type;
                    });
                if (target == output.atoms.end()) {
                    matches = false;
                    break;
                }
                targetForSource[source] = target - output.atoms.begin();
                matches &= used.insert(targetForSource[source]).second;
            }
            if (!matches) continue;
            for (const auto& bond : input.bonds) {
                const auto from = core::Add(
                    output.atoms[anchor].position,
                    Turn(core::Subtract(bond.from, input.atoms.front().position), rotation));
                const auto to = core::Add(
                    output.atoms[anchor].position,
                    Turn(core::Subtract(bond.to, input.atoms.front().position), rotation));
                matches &= std::any_of(output.bonds.begin(), output.bonds.end(),
                                       [&](const auto& expected) {
                                           return bond.bits == expected.bits &&
                                                  ((expected.from == from && expected.to == to) ||
                                                   (expected.from == to && expected.to == from));
                                       });
            }
            if (matches) return targetForSource;
        }
    return {};
}
} // namespace

std::optional<io::SolutionData> CompileAcceptedFixedArmInventory(
    const io::PuzzleData& p, const std::function<bool(const io::SolutionData&)>& accept,
    const std::function<void(const std::string&)>& feedback) {
    constexpr auto permissions = (1ULL << 0) | (1ULL << 22) | (1ULL << 23);
    if (p.version != 3 || p.productionInfo || p.outputMultiplier <= 0 ||
        p.outputMultiplier > std::numeric_limits<int>::max() / 24 ||
        (p.permissionBits & permissions) != permissions || p.inputs.size() != 1 ||
        p.outputs.size() != 1 || p.inputs[0].atoms.empty() || p.inputs[0].atoms.size() > 16)
        return {};
    for (const auto* molecules : {&p.inputs, &p.outputs})
        for (const auto& molecule : *molecules)
            for (const auto& atom : molecule.atoms)
                if (atom.position.q < -120 || atom.position.q > 120 || atom.position.r < -120 ||
                    atom.position.r > 120)
                    return {};
    const auto match = MatchMolecule(p.inputs[0], p.outputs[0]);
    if (!match) return {};
    try {
        auto initial = IR::Initial(p);
        IR check;
        check.Append(p, initial, {IR::Kind::Acquire, 0, {}});
        if (IR::Fragment(initial, 1).size() != p.inputs[0].atoms.size()) return {};
    } catch (const IR::Error&) {
        return {};
    }
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    unsigned candidates = 0;
    std::set<std::string> testedMachines;
    const auto& input = p.inputs[0];
    const auto& output = p.outputs[0];
    for (std::size_t anchor = 0; anchor < input.atoms.size(); ++anchor)
        for (int inputTurn = 0; inputTurn < 6; ++inputTurn)
            for (int inputDirection = 0; inputDirection < 6; ++inputDirection)
                for (int shift : {1, 5, 2, 4, 3}) {
                    if (candidates >= 64 || std::chrono::steady_clock::now() >= deadline) return {};
                    const auto inputTip = Turn({1, 0}, inputDirection);
                    const core::HexFrame feed{
                        {core::Subtract(inputTip, Turn(input.atoms[anchor].position, inputTurn))},
                        inputTurn};
                    const auto targetAnchor = (*match)[anchor];
                    std::optional<core::HexFrame> product;
                    for (int outputTurn = 0; outputTurn < 6; ++outputTurn) {
                        const auto destination = Turn(inputTip, shift);
                        const core::HexFrame frame{
                            core::Subtract(destination,
                                           Turn(output.atoms[targetAnchor].position, outputTurn)),
                            outputTurn};
                        bool fits = true;
                        for (std::size_t source = 0; source < input.atoms.size(); ++source)
                            fits &= Turn(feed.ToWorld(input.atoms[source].position), shift) ==
                                    frame.ToWorld(output.atoms[(*match)[source]].position);
                        if (fits) {
                            product = frame;
                            break;
                        }
                    }
                    if (!product) continue;
                    std::set<std::pair<int, int>> footprint;
                    footprint.emplace(0, 0);
                    bool clear = true;
                    for (const auto& atom : input.atoms) {
                        const auto cell = feed.ToWorld(atom.position);
                        clear &= cell != Coord{} && footprint.emplace(cell.q, cell.r).second;
                    }
                    for (const auto& atom : output.atoms) {
                        const auto cell = product->ToWorld(atom.position);
                        clear &= cell != Coord{} && footprint.emplace(cell.q, cell.r).second;
                    }
                    if (!clear) continue;
                    ConstraintLayoutProblem layout;
                    ConstraintModule station;
                    for (const auto& [q, r] : footprint)
                        station.footprint.push_back({q, r});
                    station.domain = MakeConstraintPlacementDomain({{0, 0}, {8, 0}}, {0});
                    layout.modules = {std::move(station)};
                    std::optional<io::SolutionData> accepted;
                    const auto placed = SearchConstraintLayout(
                        layout, {.maxNodes = 4, .maxCandidates = 2},
                        [&](const ConstraintLayout& candidate) {
                            if (candidates++ >= 64 || std::chrono::steady_clock::now() >= deadline)
                                return false;
                            const auto base = candidate[0].placement.translation;
                            auto inputFrame = feed;
                            auto outputFrame = *product;
                            inputFrame.origin = core::Add(inputFrame.origin, base);
                            outputFrame.origin = core::Add(outputFrame.origin, base);
                            JointSupplyPlan supply;
                            supply.productsPerPeriod = 1;
                            supply.usage = {{1, input.atoms.size(), 0}};
                            std::vector<std::size_t> ordering(input.atoms.size());
                            std::iota(ordering.begin(), ordering.end(), 0);
                            std::rotate(ordering.begin(), ordering.begin() + anchor,
                                        ordering.end());
                            for (const auto source : ordering)
                                supply.steps.push_back(
                                    {{SupplyPlan::Kind::Take, 0, source, (*match)[source]}, 0});
                            IR program;
                            try {
                                program = BuildJointSupplyExecutionIR(p, supply, {outputFrame},
                                                                      {inputFrame});
                            } catch (const IR::Error& error) {
                                if (feedback)
                                    feedback(std::string("fixed-arm material: ") + error.what());
                                return false;
                            }
                            const std::vector<Coord> track{base};
                            const HeldArmState start{0, 1, inputDirection, 0};
                            std::vector<Coord> inputCells;
                            for (const auto& atom : input.atoms)
                                inputCells.push_back(inputFrame.ToWorld(atom.position));
                            const auto motion =
                                [&](const IR::State& state, const HeldArmState& from,
                                    const HeldArmState& to, char instruction, bool holding) {
                                    if ((instruction == 'p' || instruction == 'P') &&
                                        !(p.permissionBits & (1ULL << 26)))
                                        return false;
                                    return CertifyFixedArmInterval(state, base, inputCells, from,
                                                                   to, instruction, holding);
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
                            int previousTapeLength = 0;
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
                                             core::Add(base,
                                                       Turn({1, 0}, committed.Arm().direction)),
                                             committed.Arm().fragmentRotation,
                                             previousTapeLength,
                                             end});
                                        previousTapeLength = end;
                                    }));
                                if (inventory->MoveEmpty(start, 3000) != MotionStatus::Found) {
                                    if (feedback) feedback("fixed-arm cannot reset empty arm");
                                    return false;
                                }
                                const auto execution = MakeExecutionPlan(p, program);
                                ValidatePhysicalPlan(execution, physical);
                            } catch (const IR::Error& error) {
                                if (feedback)
                                    feedback(std::string("fixed-arm physical: ") + error.what());
                                return false;
                            }
                            io::SolutionData solution;
                            solution.puzzleName = p.name;
                            solution.solutionName = "Fixed-arm fragment inventory";
                            io::SolutionObject arm;
                            arm.typeName = "arm1";
                            arm.position = base;
                            int cycle = 0;
                            for (char action : inventory->Tape())
                                arm.instructions.push_back({action, cycle++});
                            const auto completionCycles = 6LL * p.outputMultiplier * cycle;
                            if (completionCycles > std::numeric_limits<std::int32_t>::max())
                                return false;
                            solution.metrics.cycles = static_cast<std::int32_t>(completionCycles);
                            arm.bodyData = {1, inputDirection, 0, cycle, 0};
                            solution.objects.push_back(std::move(arm));
                            solution.objects.push_back(
                                {"input", inputFrame.origin, {}, {1, inputFrame.Turns(), 0, 0, 0}});
                            solution.objects.push_back({"out-std",
                                                        outputFrame.origin,
                                                        {},
                                                        {1, outputFrame.Turns(), 0, 0, 0}});
                            if (!testedMachines.insert(PhysicalCandidateKey(solution)).second)
                                return false;
                            if (!accept(solution)) return false;
                            accepted = std::move(solution);
                            return true;
                        });
                    if (placed.status == ConstraintLayoutStatus::Found) return accepted;
                }
    return {};
}
} // namespace opusoptima::solvers
