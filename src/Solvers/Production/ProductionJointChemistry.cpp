#include "ProductionJointChemistry.hpp"

#include "CandidateIdentity.hpp"
#include "ExecutionPlan.hpp"
#include "HexFrame.hpp"
#include "InventorySearch.hpp"
#include "ProductionTransport.hpp"
#include "SupplyExecutionIR.hpp"

#include <algorithm>
#include <array>
#include <set>
#include <tuple>

namespace opusoptima::solvers {
namespace {
using IR = ExecutionIR;
using Coord = core::HexCoord;
constexpr std::array<Coord, 4> outlets{{{-1, 0}, {1, 0}, {0, -1}, {1, -1}}};
Coord Turn(Coord cell, int count) {
    for (int i = 0; i < count; ++i)
        cell = core::RotateClockwise(cell);
    return cell;
}
struct ConduitArrival {
    io::SolutionData transport;
    std::size_t sourceRoom{}, destinationRoom{};
    core::HexFrame inputFrame;
    Coord sourceArm{};
    ProductionConduitHandoff handoff;
    [[nodiscard]] Coord DestinationCell(std::size_t atom) const {
        return core::Add(handoff.destinationPosition, handoff.payload.atoms.at(atom).position);
    }
    [[nodiscard]] io::SolutionData WithoutDestinationArm() const {
        auto machine = transport;
        std::erase_if(machine.objects, [](const auto& object) {
            return object.typeName == "out-std" ||
                   (object.typeName == "arm1" && object.bodyData.size() > 4 &&
                    object.bodyData[4] == 1);
        });
        return machine;
    }
};
std::optional<ConduitArrival> DescribeArrival(const io::PuzzleData& puzzle,
                                              const ProductionTransportPlan& plan,
                                              const std::vector<ProductionChamberLayout>& chambers,
                                              const io::SolutionData& transport) {
    const auto sourcePipe =
        std::find_if(transport.objects.begin(), transport.objects.end(), [&](const auto& object) {
            return object.typeName == "pipe" &&
                   ContainsProductionCell(chambers.at(plan.source.chamberIndex), object.position);
        });
    const auto destinationPipe =
        std::find_if(transport.objects.begin(), transport.objects.end(), [&](const auto& object) {
            return object.typeName == "pipe" &&
                   ContainsProductionCell(chambers.at(plan.destination.chamberIndex),
                                          object.position);
        });
    const auto input = std::find_if(transport.objects.begin(), transport.objects.end(),
                                    [](const auto& object) { return object.typeName == "input"; });
    const auto arm =
        std::find_if(transport.objects.begin(), transport.objects.end(), [](const auto& object) {
            return object.typeName == "arm1" && object.bodyData.size() > 4 &&
                   object.bodyData[4] == 0;
        });
    if (sourcePipe == transport.objects.end() || destinationPipe == transport.objects.end() ||
        input == transport.objects.end() || arm == transport.objects.end() ||
        sourcePipe->bodyData.size() < 7 || destinationPipe->bodyData.size() < 7 ||
        input->bodyData.size() < 2)
        return {};
    ConduitArrival arrival;
    arrival.transport = transport;
    arrival.sourceRoom = plan.source.chamberIndex;
    arrival.destinationRoom = plan.destination.chamberIndex;
    arrival.inputFrame = {input->position, input->bodyData[1]};
    arrival.sourceArm = arm->position;
    arrival.handoff = plan.handoff;
    arrival.handoff.sourcePosition = sourcePipe->position;
    arrival.handoff.destinationPosition = destinationPipe->position;
    arrival.handoff.rotation = sourcePipe->bodyData[1];
    arrival.handoff.conduitId = sourcePipe->bodyData[5];
    arrival.handoff.payload = puzzle.inputs[0];
    const auto ontoConduit = [&](Coord local) {
        const auto inputCell = arrival.inputFrame.ToWorld(local);
        const auto released =
            core::Add(arrival.sourceArm, Turn(core::Subtract(inputCell, arrival.sourceArm), 1));
        return core::Subtract(released, arrival.handoff.sourcePosition);
    };
    for (auto& atom : arrival.handoff.payload.atoms)
        atom.position = ontoConduit(atom.position);
    for (auto& bond : arrival.handoff.payload.bonds) {
        bond.from = ontoConduit(bond.from);
        bond.to = ontoConduit(bond.to);
    }
    return arrival;
}
ProductionExecution TransferFeed(const io::PuzzleData& puzzle, const ConduitArrival& arrival) {
    ProductionExecution ledger(puzzle);
    ledger.Apply(
        arrival.sourceRoom,
        {IR::Kind::Acquire, 0, {}, 0, arrival.inputFrame.Turns(), arrival.inputFrame.origin});
    ProductionExecution::Transfer transfer;
    transfer.handoff = arrival.handoff;
    transfer.handoff.pickupCycle = transfer.handoff.releaseCycle + 1;
    transfer.sourceRoom = arrival.sourceRoom;
    transfer.destinationRoom = arrival.destinationRoom;
    transfer.arrivalCycle = transfer.handoff.pickupCycle;
    for (std::size_t i = 0; i < puzzle.inputs[0].atoms.size(); ++i)
        transfer.atoms.push_back(i + 1);
    ledger.Apply(arrival.sourceRoom,
                 {IR::Kind::Relocate,
                  0,
                  {1},
                  0,
                  1,
                  core::Subtract(arrival.sourceArm, Turn(arrival.sourceArm, 1))});
    ledger.Release(transfer);
    ledger.Arrive(transfer.handoff.conduitIndex, transfer.arrivalCycle);
    ledger.Pickup(transfer.handoff.conduitIndex, transfer.handoff.pickupCycle);
    return ledger;
}
struct DispersionAssignment {
    std::array<std::size_t, 4> cardinal{};
    std::optional<std::size_t> companion;
};
std::optional<DispersionAssignment> AssignDispersionOutputs(
    const io::PuzzleData& puzzle, std::optional<core::AtomType> companionType) {
    DispersionAssignment result;
    std::array<bool, 4> matched{};
    std::optional<std::size_t> salt;
    for (std::size_t output = 0; output < puzzle.outputs.size(); ++output) {
        const auto& molecule = puzzle.outputs[output];
        if (molecule.atoms.size() != 1 || !molecule.bonds.empty()) return {};
        const auto coordinate = molecule.atoms[0].position;
        if (coordinate.q < -120 || coordinate.q > 120 || coordinate.r < -120 || coordinate.r > 120)
            return {};
        const auto type = molecule.atoms[0].type;
        if (companionType && type == *companionType) {
            if (result.companion) return {};
            result.companion = output;
        } else if (type == core::AtomType::Salt) {
            if (salt || companionType) return {};
            salt = output;
        } else {
            const int cardinal = static_cast<int>(type) - static_cast<int>(core::AtomType::Air);
            if (cardinal < 0 || cardinal >= 4 || matched[cardinal]) return {};
            result.cardinal[cardinal] = output;
            matched[cardinal] = true;
        }
    }
    std::size_t missing = 0;
    for (std::size_t i = 0; i < matched.size(); ++i)
        if (!matched[i]) {
            if (!salt || companionType || ++missing > 1 || !(puzzle.permissionBits & (1ULL << 12)))
                return {};
            result.cardinal[i] = *salt;
        }
    if (companionType && (!result.companion || missing)) return {};
    return result;
}
void AddSingletonOutput(io::SolutionData& candidate, const io::PuzzleData& puzzle, std::size_t port,
                        Coord cell) {
    candidate.objects.push_back({"out-std",
                                 core::Subtract(cell, puzzle.outputs.at(port).atoms.at(0).position),
                                 {},
                                 {1, 0, static_cast<int>(port), 0, 0}});
}
void ReportJointFailure(const std::function<void(const SupplyFailure&)>& feedback,
                        const ConduitArrival& arrival, const IR::Error& error, std::size_t attempt,
                        std::optional<Coord> conflictCell = {}) {
    if (!feedback) return;
    auto category = SupplyFailureCategory::Layout;
    std::optional<int> permission;
    if (const auto* missing = dynamic_cast<const IR::PermissionFailure*>(&error)) {
        category = SupplyFailureCategory::Material;
        permission = missing->bit;
    } else if (dynamic_cast<const IR::TimingFailure*>(&error))
        category = SupplyFailureCategory::Timing;
    else if (dynamic_cast<const IR::BudgetFailure*>(&error))
        category = SupplyFailureCategory::Budget;
    std::vector<std::uint64_t> fragment;
    for (std::size_t i = 0; i < arrival.handoff.payload.atoms.size(); ++i)
        fragment.push_back(i + 1);
    feedback({.category = category,
              .rotation = arrival.handoff.rotation,
              .spacing = 0,
              .detail = error.what(),
              .materialAttempt = attempt,
              .productsPerPeriod = 1,
              .missingPermission = permission,
              .input = 0,
              .chamber = arrival.destinationRoom,
              .conduit = arrival.handoff.conduitIndex,
              .fragment = std::move(fragment),
              .conflictCell = conflictCell,
              .mechanicalMode = "conduit-reaction"});
}
std::optional<ExecutionPlan> PlanConduitMaterial(
    const io::PuzzleData& puzzle, IR::Kind reaction,
    const std::function<void(const SupplyFailure&)>& feedback) {
    ChemicalRecipe recipe;
    recipe.inputBatches = {1};
    if (reaction == IR::Kind::Duplicate)
        recipe.duplications = 1;
    else {
        recipe.dispersions = 1;
        recipe.calcifications =
            puzzle.outputs.size() == 4 &&
            std::any_of(puzzle.outputs.begin(), puzzle.outputs.end(), [](const auto& output) {
                return output.atoms.size() == 1 && output.atoms[0].type == core::AtomType::Salt;
            });
    }
    const auto found = SearchJointSupplyInventory(puzzle, 1, {512, 64, 16}, &recipe);
    if (found.status != InventorySearchStatus::Found) {
        if (feedback)
            feedback({.category = found.status == InventorySearchStatus::Exhausted
                                      ? SupplyFailureCategory::Budget
                                      : SupplyFailureCategory::Material,
                      .rotation = -1,
                      .spacing = 0,
                      .detail = "joint production material period did not close",
                      .productsPerPeriod = 1,
                      .input = 0,
                      .mechanicalMode = "conduit-reaction",
                      .budgetDimension = found.status == InventorySearchStatus::Exhausted
                                             ? "material-states"
                                             : ""});
        return {};
    }
    try {
        auto planned = MakeExecutionPlan(puzzle, BuildJointSupplyExecutionIR(puzzle, found.plan));
        if (found.plan.usage.size() != 1 || found.plan.usage[0].batches != 1 ||
            found.plan.usage[0].discarded ||
            std::count_if(planned.tasks.begin(), planned.tasks.end(),
                          [&](const auto& task) { return task.kind == reaction; }) != 1 ||
            std::any_of(planned.tasks.begin(), planned.tasks.end(), [](const auto& task) {
                return task.kind == IR::Kind::Store || task.kind == IR::Kind::Retrieve ||
                       task.kind == IR::Kind::Discard;
            }))
            throw IR::Unsupported("production module needs one closed unbuffered reaction batch");
        return planned;
    } catch (const IR::Error& error) {
        if (feedback)
            feedback({.category = SupplyFailureCategory::Material,
                      .rotation = -1,
                      .spacing = 0,
                      .detail = error.what(),
                      .productsPerPeriod = 1,
                      .input = 0,
                      .mechanicalMode = "conduit-reaction"});
        return {};
    }
}
using Obligation = std::tuple<int, std::size_t, int, std::vector<IR::AtomId>>;
std::vector<Obligation> MaterialObligations(const IR& program) {
    std::vector<Obligation> obligations;
    for (const auto& step : program.steps) {
        auto operation = step.operation;
        if (operation.kind == IR::Kind::Acquire || operation.kind == IR::Kind::Relocate) continue;
        if (operation.kind == IR::Kind::Bond || operation.kind == IR::Kind::Unbond)
            std::sort(operation.atoms.begin(), operation.atoms.end());
        obligations.emplace_back(static_cast<int>(operation.kind), operation.index,
                                 static_cast<int>(operation.bits), operation.atoms);
    }
    std::sort(obligations.begin(), obligations.end());
    return obligations;
}
} // namespace

std::optional<io::SolutionData> CompileAcceptedProductionDispersion(
    const io::PuzzleData& puzzle, const std::function<bool(const io::SolutionData&)>& accept,
    const std::function<void(const SupplyFailure&)>& feedback) {
    constexpr auto permissions =
        (1ULL << 0) | (1ULL << 18) | (1ULL << 22) | (1ULL << 23) | (1ULL << 25);
    if (puzzle.version != 3 || !puzzle.productionInfo ||
        puzzle.productionInfo->chambers.size() != 2 ||
        puzzle.productionInfo->conduits.size() != 1 || puzzle.inputs.size() != 1 ||
        puzzle.outputMultiplier != 1 || (puzzle.permissionBits & permissions) != permissions)
        return {};
    const auto& feed = puzzle.inputs[0];
    const bool bonded = feed.atoms.size() == 2 && feed.bonds.size() == 1;
    if ((feed.atoms.size() != 1 && !bonded) ||
        (bonded && (!(puzzle.permissionBits & (1ULL << 9)) || feed.bonds[0].bits != 1 ||
                    !((feed.bonds[0].from == feed.atoms[0].position &&
                       feed.bonds[0].to == feed.atoms[1].position) ||
                      (feed.bonds[0].to == feed.atoms[0].position &&
                       feed.bonds[0].from == feed.atoms[1].position)))) ||
        (!bonded && !feed.bonds.empty()) || puzzle.outputs.size() != (bonded ? 5U : 4U))
        return {};
    const auto quint = std::find_if(feed.atoms.begin(), feed.atoms.end(), [](const auto& atom) {
        return atom.type == core::AtomType::Quintessence;
    });
    if (quint == feed.atoms.end()) return {};
    const auto quintIndex = static_cast<std::size_t>(quint - feed.atoms.begin());
    const auto companionIndex = bonded ? 1 - quintIndex : 0;
    const auto companionType = feed.atoms[companionIndex].type;
    if (bonded &&
        ((companionType >= core::AtomType::Air && companionType <= core::AtomType::Water) ||
         companionType == core::AtomType::Quintessence))
        return {};
    const auto assignment = AssignDispersionOutputs(
        puzzle, bonded ? std::optional<core::AtomType>{companionType} : std::nullopt);
    if (!assignment) return {};
    const auto material = PlanConduitMaterial(puzzle, IR::Kind::Disperse, feedback);
    if (!material) return {};
    const auto chambers = PlanProductionChambers(puzzle);
    auto proxy = puzzle;
    proxy.outputs = proxy.inputs;
    const auto plan = PlanProductionTransport(proxy);
    std::optional<io::SolutionData> accepted;
    std::set<std::string> testedMachines;
    std::size_t attempts = 0;
    (void)CompileAcceptedProductionTransport(proxy, [&](const io::SolutionData& transport) {
        const auto described = DescribeArrival(puzzle, plan, chambers, transport);
        if (!described) return false;
        const auto& arrival = *described;
        ++attempts;
        const auto station = arrival.DestinationCell(quintIndex);
        const auto saltCell = bonded ? arrival.DestinationCell(companionIndex) : Coord{};
        if (!ContainsProductionCell(chambers.at(arrival.destinationRoom), station) ||
            (bonded && !ContainsProductionCell(chambers.at(arrival.destinationRoom), saltCell))) {
            ReportJointFailure(
                feedback, arrival,
                IR::LayoutFailure("conduit reaction footprint outside destination room"), attempts,
                station);
            return false;
        }
        for (const auto offset : outlets)
            if (!ContainsProductionCell(chambers.at(arrival.destinationRoom),
                                        core::Add(station, offset)) ||
                (bonded && saltCell == core::Add(station, offset))) {
                ReportJointFailure(
                    feedback, arrival,
                    IR::LayoutFailure(
                        "reaction output conflicts with chamber or residual fragment"),
                    attempts, core::Add(station, offset));
                return false;
            }
        auto candidate = arrival.WithoutDestinationArm();
        candidate.solutionName =
            bonded ? "Joint bonded-feed conduit dispersion" : "Joint production conduit dispersion";
        if (bonded) {
            int direction = 0;
            while (direction < 6 && core::Add(saltCell, Turn({1, 0}, direction)) != station)
                ++direction;
            if (direction >= 6) return false;
            candidate.objects.push_back({"unbonder", saltCell, {}, {1, direction, 0, 0, 0}});
            AddSingletonOutput(candidate, puzzle, *assignment->companion, saltCell);
        }
        candidate.objects.push_back({"glyph-dispersion", station, {}, {1, 0, 0, 0, 0}});
        for (std::size_t cardinal = 0; cardinal < 4; ++cardinal) {
            const auto output = assignment->cardinal[cardinal];
            const auto cell = core::Add(station, outlets[cardinal]);
            if (puzzle.outputs[output].atoms[0].type == core::AtomType::Salt)
                candidate.objects.push_back({"glyph-calcification", cell, {}, {1, 0, 0, 0, 0}});
            AddSingletonOutput(candidate, puzzle, output, cell);
        }
        try {
            auto ledger = TransferFeed(puzzle, arrival);
            if (bonded)
                ledger.Apply(arrival.destinationRoom,
                             {IR::Kind::Unbond, 0, {quintIndex + 1, companionIndex + 1}, 1});
            const auto firstProduct = ledger.Material().nextId;
            ledger.React(arrival.destinationRoom, {IR::Kind::Disperse, 0, {quintIndex + 1}},
                         {core::Add(station, outlets[0]), core::Add(station, outlets[1]),
                          core::Add(station, outlets[2]), core::Add(station, outlets[3])});
            if (bonded)
                ledger.Apply(arrival.destinationRoom,
                             {IR::Kind::Output, *assignment->companion, {companionIndex + 1}});
            for (std::size_t cardinal = 0; cardinal < 4; ++cardinal) {
                auto atom = firstProduct + cardinal;
                if (puzzle.outputs[assignment->cardinal[cardinal]].atoms[0].type ==
                    core::AtomType::Salt) {
                    const auto fresh = ledger.Material().nextId;
                    ledger.React(arrival.destinationRoom, {IR::Kind::Calcify, 0, {atom}},
                                 {core::Add(station, outlets[cardinal])});
                    atom = fresh;
                }
                ledger.Apply(arrival.destinationRoom,
                             {IR::Kind::Output, assignment->cardinal[cardinal], {atom}});
            }
            ledger.RequireComplete(std::vector<std::size_t>(puzzle.outputs.size(), 1));
            if (MaterialObligations(material->material) != MaterialObligations(ledger.Program()))
                throw IR::Unsupported("conduit execution changed the shared material obligations");
        } catch (const IR::Error& error) {
            ReportJointFailure(feedback, arrival, error, attempts);
            return false;
        }
        if (!testedMachines.insert(PhysicalCandidateKey(candidate)).second) return false;
        if (!accept(candidate)) return false;
        accepted = std::move(candidate);
        return true;
    });
    return accepted;
}
std::optional<io::SolutionData> CompileAcceptedProductionDuplication(
    const io::PuzzleData& puzzle, const std::function<bool(const io::SolutionData&)>& accept,
    const std::function<void(const SupplyFailure&)>& feedback) {
    constexpr auto permissions =
        (1ULL << 0) | (1ULL << 9) | (1ULL << 13) | (1ULL << 22) | (1ULL << 23) | (1ULL << 25);
    if (puzzle.version != 3 || !puzzle.productionInfo ||
        puzzle.productionInfo->chambers.size() != 2 ||
        puzzle.productionInfo->conduits.size() != 1 || puzzle.inputs.size() != 1 ||
        puzzle.inputs[0].atoms.size() != 2 || puzzle.inputs[0].bonds.size() != 1 ||
        puzzle.inputs[0].bonds[0].bits != 1 || puzzle.outputs.size() != 2 ||
        puzzle.outputMultiplier != 1 || (puzzle.permissionBits & permissions) != permissions)
        return {};
    const auto& feed = puzzle.inputs[0];
    const auto salt = std::find_if(feed.atoms.begin(), feed.atoms.end(), [](const auto& atom) {
        return atom.type == core::AtomType::Salt;
    });
    if (salt == feed.atoms.end()) return {};
    const auto saltIndex = static_cast<std::size_t>(salt - feed.atoms.begin());
    const auto catalystIndex = 1 - saltIndex;
    const auto catalystType = feed.atoms[catalystIndex].type;
    if (catalystType < core::AtomType::Air || catalystType > core::AtomType::Water ||
        !((feed.bonds[0].from == feed.atoms[0].position &&
           feed.bonds[0].to == feed.atoms[1].position) ||
          (feed.bonds[0].to == feed.atoms[0].position &&
           feed.bonds[0].from == feed.atoms[1].position)) ||
        std::any_of(puzzle.outputs.begin(), puzzle.outputs.end(), [&](const auto& output) {
            return output.atoms.size() != 1 || output.atoms[0].type != catalystType ||
                   output.atoms[0].position.q < -120 || output.atoms[0].position.q > 120 ||
                   output.atoms[0].position.r < -120 || output.atoms[0].position.r > 120 ||
                   !output.bonds.empty();
        }))
        return {};
    const auto material = PlanConduitMaterial(puzzle, IR::Kind::Duplicate, feedback);
    if (!material) return {};
    const auto chambers = PlanProductionChambers(puzzle);
    auto proxy = puzzle;
    proxy.outputs = proxy.inputs;
    const auto plan = PlanProductionTransport(proxy);
    std::optional<io::SolutionData> accepted;
    std::set<std::string> testedMachines;
    std::size_t attempts = 0;
    (void)CompileAcceptedProductionTransport(proxy, [&](const io::SolutionData& transport) {
        const auto described = DescribeArrival(puzzle, plan, chambers, transport);
        if (!described) return false;
        const auto& arrival = *described;
        ++attempts;
        const auto saltCell = arrival.DestinationCell(saltIndex);
        const auto catalystCell = arrival.DestinationCell(catalystIndex);
        if (!ContainsProductionCell(chambers.at(arrival.destinationRoom), saltCell) ||
            !ContainsProductionCell(chambers.at(arrival.destinationRoom), catalystCell)) {
            ReportJointFailure(
                feedback, arrival,
                IR::LayoutFailure("conduit catalyst contacts outside destination room"), attempts,
                saltCell);
            return false;
        }
        int direction = 0;
        while (direction < 6 && core::Add(saltCell, Turn({1, 0}, direction)) != catalystCell)
            ++direction;
        if (direction >= 6) {
            ReportJointFailure(feedback, arrival,
                               IR::LayoutFailure("conduit catalyst contacts are not adjacent"),
                               attempts, catalystCell);
            return false;
        }
        auto base = arrival.WithoutDestinationArm();
        base.solutionName = "Joint bonded-feed conduit catalyst";
        base.objects.push_back({"unbonder", saltCell, {}, {1, direction, 0, 0, 0}});
        base.objects.push_back(
            {"glyph-duplication", catalystCell, {}, {1, (direction + 3) % 6, 0, 0, 0}});
        for (bool swapOutputs : {false, true}) {
            auto candidate = base;
            for (int port = 0; port < 2; ++port)
                AddSingletonOutput(candidate, puzzle, port,
                                   port == static_cast<int>(swapOutputs) ? saltCell : catalystCell);
            try {
                auto ledger = TransferFeed(puzzle, arrival);
                ledger.Apply(arrival.destinationRoom,
                             {IR::Kind::Unbond, 0, {saltIndex + 1, catalystIndex + 1}, 1});
                const auto duplicated = ledger.Material().nextId;
                ledger.React(arrival.destinationRoom,
                             {IR::Kind::Duplicate, 0, {saltIndex + 1, catalystIndex + 1}},
                             {saltCell});
                ledger.Apply(
                    arrival.destinationRoom,
                    {IR::Kind::Output, static_cast<std::size_t>(swapOutputs), {duplicated}});
                ledger.Apply(arrival.destinationRoom, {IR::Kind::Output,
                                                       static_cast<std::size_t>(!swapOutputs),
                                                       {catalystIndex + 1}});
                ledger.RequireComplete({1, 1});
                if (MaterialObligations(material->material) !=
                    MaterialObligations(ledger.Program()))
                    throw IR::Unsupported(
                        "conduit execution changed the shared material obligations");
            } catch (const IR::Error& error) {
                ReportJointFailure(feedback, arrival, error, attempts);
                continue;
            }
            if (!testedMachines.insert(PhysicalCandidateKey(candidate)).second) continue;
            if (!accept(candidate)) continue;
            accepted = std::move(candidate);
            return true;
        }
        return false;
    });
    return accepted;
}
} // namespace opusoptima::solvers
