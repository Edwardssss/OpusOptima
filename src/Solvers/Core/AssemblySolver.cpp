#include "AssemblySolver.hpp"
#include "AutomaticSupply.hpp"
#include "BinaryWriter.hpp"
#include "CalcifyAnimismusAdmission.hpp"
#include "CandidateArchive.hpp"
#include "ChemicalPlanner.hpp"
#include "ClosedMetalInventory.hpp"
#include "ComposeAssembly.hpp"
#include "DemandDrivenChain.hpp"
#include "Disassembly.hpp"
#include "FixedArmInventory.hpp"
#include "FixedArmJoin.hpp"
#include "LifeDeath.hpp"
#include "LocalOptimizer.hpp"
#include "LogManager.hpp"
#include "MaterialPlanning.hpp"
#include "MolecularMetalSplit.hpp"
#include "ProductionAssembly.hpp"
#include "ProductionChemistry.hpp"
#include "ProductionDirectFeed.hpp"
#include "ProductionDistributedPurification.hpp"
#include "ProductionFiniteStock.hpp"
#include "ProductionJointChemistry.hpp"
#include "ProductionMolecular.hpp"
#include "ProductionPetalAssembly.hpp"
#include "ProductionRingProcessing.hpp"
#include "ProductionRouting.hpp"
#include "ProductionTransport.hpp"
#include "PurificationAssembly.hpp"
#include "RepeatingOutput.hpp"
#include "RowAssembly.hpp"
#include "RowAssemblyInternal.hpp"
#include "SerialArmExecution.hpp"
#include "TracklessPairChemistry.hpp"
#include "WheelAssembly.hpp"
#include <cstdlib>

#include <algorithm>
#include <chrono>
#include <exception>
#include <limits>
#include <optional>
#include <stdexcept>

namespace opusoptima::solvers {
namespace {
using Edge = AssemblyPlan::Edge;
using Kind = AssemblyPlan::Kind;
using Resource = AssemblyPlan::ResourceKind;
[[noreturn]] void Reject(const char* message) {
    throw std::invalid_argument(message);
}

bool ValidAtom(const io::MoleculeAtom& atom) {
    return core::AtomTypeFromId(static_cast<std::uint8_t>(atom.type)).has_value() &&
           atom.type != core::AtomType::Repeat && atom.position.q >= -128 &&
           atom.position.q <= 127 && atom.position.r >= -128 && atom.position.r <= 127;
}

std::vector<Edge> ValidatePuzzle(const io::PuzzleData& p) {
    ValidateSpecialRules(p);
    if (p.version != 3 || p.outputs.size() != 1 || p.inputs.empty() ||
        p.inputs.size() > AssemblyPlanner::MaxAtoms || p.outputMultiplier <= 0 ||
        p.outputMultiplier > std::numeric_limits<int>::max() / 24 ||
        (p.permissionBits & AssemblyPlanner::RequiredPermissions) !=
            AssemblyPlanner::RequiredPermissions)
        Reject("assembly requires v3 nonproduction, one finite output, positive bounded multiplier "
               "and arm/bonder/rotation/grab permissions");
    for (const auto& input : p.inputs) {
        if (input.atoms.empty() || input.atoms.size() > AssemblyPlanner::MaxAtoms)
            Reject("assembly input atom bound exceeded");
        for (std::size_t i = 0; i < input.atoms.size(); ++i) {
            if (!ValidAtom(input.atoms[i])) Reject("invalid input atom");
            for (std::size_t j = 0; j < i; ++j)
                if (input.atoms[i].position == input.atoms[j].position)
                    Reject("duplicate input cell");
        }
        for (std::size_t i = 0; i < input.bonds.size(); ++i) {
            const auto& b = input.bonds[i];
            auto has = [&](core::HexCoord c) {
                return std::any_of(input.atoms.begin(), input.atoms.end(),
                                   [&](const auto& a) { return a.position == c; });
            };
            // Input bonds are supplied by the puzzle, not created by a prism.
            // Colored bonds may therefore connect non-fire atoms as well.
            const bool supported = b.bits == 1 || (b.bits && !(b.bits & ~14));
            if (!supported || core::Distance(b.from, b.to) != 1 || !has(b.from) || !has(b.to))
                Reject("invalid or unsupported input bond");
            for (std::size_t j = 0; j < i; ++j) {
                const auto& old = input.bonds[j];
                if ((old.from == b.from && old.to == b.to) ||
                    (old.from == b.to && old.to == b.from))
                    Reject("duplicate input bond");
            }
        }
        if (!input.bonds.empty() && !(p.permissionBits & (1ULL << 9)))
            Reject("disassembly requires unbonder permission");
    }
    const auto& target = p.outputs[0];
    if (target.atoms.empty() || target.atoms.size() > AssemblyPlanner::MaxAtoms)
        Reject("assembly target atom bound exceeded");
    for (std::size_t i = 0; i < target.atoms.size(); ++i) {
        if (!ValidAtom(target.atoms[i])) Reject("invalid or repeat target atom");
        for (std::size_t j = 0; j < i; ++j)
            if (target.atoms[i].position == target.atoms[j].position)
                Reject("duplicate target cell");
    }
    std::vector<Edge> edges;
    for (const auto& bond : target.bonds) {
        if (bond.bits != 1) Reject("only ordinary bonds supported");
        auto index = [&](core::HexCoord cell) {
            for (std::size_t i = 0; i < target.atoms.size(); ++i)
                if (target.atoms[i].position == cell) return i;
            Reject("bond endpoint missing");
        };
        auto a = index(bond.from), b = index(bond.to);
        if (a == b || core::Distance(bond.from, bond.to) != 1) Reject("nonadjacent or self bond");
        if (a > b) std::swap(a, b);
        if (std::find(edges.begin(), edges.end(), Edge{a, b}) != edges.end())
            Reject("duplicate bond");
        edges.push_back({a, b});
    }
    std::sort(edges.begin(), edges.end(),
              [](Edge a, Edge b) { return a.a < b.a || (a.a == b.a && a.b < b.b); });
    return edges;
}
core::HexCoord Rotate(core::HexCoord cell, int turns) {
    for (int i = 0; i < turns; ++i)
        cell = core::RotateClockwise(cell);
    return cell;
}
} // namespace

AssemblyPlan AssemblyPlanner::Plan(const io::PuzzleData& puzzle, bool forest,
                                   std::chrono::steady_clock::time_point deadline) {
    auto checkDeadline = [&] {
        if (std::chrono::steady_clock::now() >= deadline)
            throw std::invalid_argument("assembly planning deadline");
    };
    checkDeadline();
    const auto edges = ValidatePuzzle(puzzle);
    (void)ChemicalPlanner::Plan(puzzle, 1, true, deadline);
    checkDeadline();
    const auto& atoms = puzzle.outputs[0].atoms;
    AssemblyPlan plan;
    std::vector<bool> used(puzzle.inputs.size());
    for (const auto& atom : atoms) {
        checkDeadline();
        std::size_t chosen = puzzle.inputs.size();
        std::size_t chosenAtom = 0;
        for (std::size_t i = 0; i < puzzle.inputs.size(); ++i)
            for (std::size_t j = 0; j < puzzle.inputs[i].atoms.size(); ++j)
                if (puzzle.inputs[i].atoms[j].type == atom.type &&
                    (chosen == puzzle.inputs.size() || (!used[i] && used[chosen]))) {
                    chosen = i;
                    chosenAtom = j;
                }
        if (chosen == puzzle.inputs.size()) {
            for (std::size_t i = 0; i < puzzle.inputs.size(); ++i)
                for (std::size_t j = 0; j < puzzle.inputs[i].atoms.size(); ++j)
                    if (ChemicalPlanner::CanSupply(puzzle.inputs[i].atoms[j].type, atom.type,
                                                   puzzle.permissionBits) &&
                        (chosen == puzzle.inputs.size() || (!used[i] && used[chosen]))) {
                        chosen = i;
                        chosenAtom = j;
                    }
        }
        if (chosen == puzzle.inputs.size()) Reject("target requires unsupported chemistry");
        plan.inputForAtom.push_back(chosen);
        plan.sourceAtomForAtom.push_back(chosenAtom);
        used[chosen] = true;
    }
    AssemblyPlan::State state;
    auto append = [&](Kind kind, std::size_t atom, Edge edge) {
        checkDeadline();
        AssemblyPlan::Operation op;
        op.kind = kind;
        op.atom = atom;
        op.edge = edge;
        op.preconditions = state;
        op.reservations.push_back({Resource::Workstation, 0});
        if (kind == Kind::Acquire) {
            op.input = plan.inputForAtom[atom];
            op.reservations.push_back({Resource::Feed, op.input});
            state.atoms.push_back(atom);
        } else if (kind == Kind::Bond)
            state.bonds.push_back(edge);
        else
            op.reservations.push_back({Resource::Output, 0});
        for (auto i : state.atoms)
            op.occupied.push_back(atoms[i].position);
        if (kind == Kind::Output) {
            state.atoms.clear();
            state.bonds.clear();
            state.emitted = true;
        }
        op.effects = state;
        plan.operations.push_back(std::move(op));
    };
    append(Kind::Acquire, 0, {});
    std::vector<bool> present(atoms.size());
    present[0] = true;
    std::vector<Edge> formed;
    while (state.atoms.size() != atoms.size()) {
        checkDeadline();
        const auto next = std::find_if(edges.begin(), edges.end(),
                                       [&](Edge e) { return present[e.a] != present[e.b]; });
        if (next == edges.end() && !forest) Reject("disconnected target");
        const auto atom =
            next == edges.end()
                ? static_cast<std::size_t>(std::find(present.begin(), present.end(), false) -
                                           present.begin())
                : (present[next->a] ? next->b : next->a);
        append(Kind::Acquire, atom, {});
        present[atom] = true;
        // Include every edge to the growing component, including cycle closures.
        for (auto edge : edges)
            if (present[edge.a] && present[edge.b] &&
                std::find(formed.begin(), formed.end(), edge) == formed.end()) {
                append(Kind::Bond, 0, edge);
                formed.push_back(edge);
            }
    }
    append(Kind::Output, 0, {});
    checkDeadline();
    return plan;
}

void AssemblyPlanner::Validate(const io::PuzzleData& puzzle, const AssemblyPlan& plan, bool forest,
                               std::chrono::steady_clock::time_point deadline) {
    if (plan != Plan(puzzle, forest, deadline))
        Reject("assembly plan failed canonical replay validation");
}

io::SolutionData CompileSpecialized(const io::PuzzleData& puzzle, const AssemblyPlan& plan) {
    AssemblyPlanner::Validate(puzzle, plan);
    const auto& atoms = puzzle.outputs[0].atoms;
    if (puzzle.inputs.size() == 1 && (atoms.size() >= 2 && atoms.size() <= 4)) {
        // Four-atom growth parks and regrabs the growing fragment before its final translation.
        const bool parkedChain = atoms.size() == 4;
        const bool tri = atoms.size() >= 3;
        bool chain = parkedChain;
        if (atoms.size() == 3) {
            for (std::size_t middle = 0; middle < atoms.size(); ++middle) {
                const auto a = atoms[(middle + 1) % 3].position;
                const auto b = atoms[(middle + 2) % 3].position;
                chain |=
                    core::Add(a, b) == core::Add(atoms[middle].position, atoms[middle].position);
            }
        }
        if (chain && !(puzzle.permissionBits & (1ULL << 3)))
            Reject("renewable-feed straight fragment requires track permission");
        const int period = parkedChain ? 12 : tri ? 6 : 4;
        if (puzzle.outputMultiplier > std::numeric_limits<int>::max() / (6 * period))
            Reject("renewable-feed cycle count exceeds supported range");
        const std::vector<core::HexCoord> cells =
            parkedChain ? std::vector<core::HexCoord>{{-3, 1}, {-2, 1}, {-1, 1}, {0, 1}}
            : chain     ? std::vector<core::HexCoord>{{-2, 1}, {-1, 1}, {0, 1}}
            : tri       ? std::vector<core::HexCoord>{{-1, 1}, {0, 1}, {1, 0}}
                        : std::vector<core::HexCoord>{{0, 1}, {1, 0}};
        if (puzzle.outputs[0].bonds.size() != atoms.size() - 1)
            Reject("renewable-feed workstation requires an open chain with two to four atoms");
        int orientation = -1;
        core::HexCoord offset{};
        for (int turns = 0; turns < 6 && orientation < 0; ++turns) {
            for (const auto anchor : cells) {
                const auto translation = core::Subtract(anchor, Rotate(atoms[0].position, turns));
                const auto transform = [&](core::HexCoord cell) {
                    return core::Add(translation, Rotate(cell, turns));
                };
                bool matches = true;
                for (const auto& atom : atoms)
                    matches &= std::find(cells.begin(), cells.end(), transform(atom.position)) !=
                               cells.end();
                for (const auto& op : plan.operations) {
                    if (op.kind != Kind::Bond) continue;
                    const auto from = transform(atoms[op.edge.a].position);
                    const auto to = transform(atoms[op.edge.b].position);
                    bool edgeMatches = false;
                    for (std::size_t i = 1; i < cells.size(); ++i)
                        edgeMatches |= (from == cells[i - 1] && to == cells[i]) ||
                                       (to == cells[i - 1] && from == cells[i]);
                    matches &= edgeMatches;
                }
                if (matches) {
                    orientation = turns;
                    offset = translation;
                    break;
                }
            }
        }
        if (orientation < 0) Reject("unverified renewable-feed workstation geometry");
        io::SolutionData solution;
        solution.puzzleName = puzzle.name;
        solution.solutionName = "Serial renewable-feed assembly";
        solution.metrics = {6 * period * puzzle.outputMultiplier - (tri ? 2 : 1), 30, tri ? 4 : 3,
                            period};
        io::SolutionObject arm;
        arm.typeName = "arm1";
        arm.bodyData = {1, 0, 0, period, 0};
        arm.instructions = {{'G', 0}, {'r', 1}, {'g', 2}, {'R', 3}};
        if (tri) arm.instructions = {{'G', 0}, {'r', 1}, {'r', 2}, {'g', 3}, {'R', 4}, {'R', 5}};
        if (chain) {
            arm.bodyData[1] = 1;
            arm.instructions = {{'G', 0}, {'A', 1}, {'A', 2}, {'g', 3}, {'a', 4}, {'a', 5}};
            solution.metrics = {36 * puzzle.outputMultiplier - 2, 45, 6, 6};
            if (parkedChain) {
                arm.bodyData[3] = period;
                arm.instructions = {{'G', 0}, {'A', 1}, {'A', 2}, {'g', 3}, {'a', 4},  {'A', 5},
                                    {'G', 6}, {'A', 7}, {'g', 8}, {'a', 9}, {'a', 10}, {'a', 11}};
                solution.metrics = {72 * puzzle.outputMultiplier - 3, 50, 8, period};
            }
        }
        solution.objects.push_back(arm);
        if (chain) {
            io::SolutionObject track;
            track.typeName = "track";
            track.bodyData = parkedChain
                                 ? std::vector<int>{1, 0, 0, 0, 4, 0, 0, -1, 0, -2, 0, -3, 0, 0}
                                 : std::vector<int>{1, 0, 0, 0, 3, 0, 0, -1, 0, -2, 0, 0};
            solution.objects.push_back(track);
        }
        io::SolutionObject input;
        input.typeName = "input";
        input.position = core::Subtract(chain ? core::HexCoord{0, 1} : core::HexCoord{1, 0},
                                        puzzle.inputs[0].atoms[0].position);
        input.bodyData = {1, 0, static_cast<int>(plan.inputForAtom[0]), 0, 0};
        solution.objects.push_back(input);
        io::SolutionObject bonder;
        bonder.typeName = "bonder";
        bonder.position = chain ? core::HexCoord{-1, 1} : core::HexCoord{1, 0};
        bonder.bodyData = {1, chain ? 0 : 2, 0, 0, 0};
        solution.objects.push_back(bonder);
        io::SolutionObject output;
        output.typeName = "out-std";
        output.position = offset;
        output.bodyData = {1, orientation, 0, 0, 0};
        solution.objects.push_back(output);
        return solution;
    }
    if (atoms.size() == 3 && puzzle.inputs.size() == 2) {
        if (puzzle.outputs[0].bonds.size() != 2)
            Reject("mixed-feed workstation requires two ordinary bonds");
        if (puzzle.outputMultiplier > std::numeric_limits<int>::max() / 48)
            Reject("mixed-feed cycle count exceeds supported range");
        if ((puzzle.permissionBits & (1ULL << 3)) == 0)
            Reject("mixed-feed workstation requires track permission");
        // Move the pair clear of the renewable bonder before adding the other feed.
        for (std::size_t repeated = 0; repeated < 2; ++repeated) {
            const auto other = 1 - repeated;
            if (puzzle.inputs[repeated].atoms[0].type == puzzle.inputs[other].atoms[0].type)
                continue;
            const core::HexCoord third{-3, 1};
            for (int turns = 0; turns < 6; ++turns) {
                for (auto anchor : {core::HexCoord{-2, 1}, core::HexCoord{-1, 0}, third}) {
                    const auto offset = core::Subtract(anchor, Rotate(atoms[0].position, turns));
                    bool matches = true;
                    for (const auto& atom : atoms) {
                        const auto cell = core::Add(offset, Rotate(atom.position, turns));
                        matches &=
                            (atom.type == puzzle.inputs[repeated].atoms[0].type &&
                             (cell == core::HexCoord{-2, 1} || cell == core::HexCoord{-1, 0})) ||
                            (atom.type == puzzle.inputs[other].atoms[0].type && cell == third);
                    }
                    for (const auto& bond : puzzle.outputs[0].bonds) {
                        auto from = core::Add(offset, Rotate(bond.from, turns));
                        auto to = core::Add(offset, Rotate(bond.to, turns));
                        const auto edge = [&](core::HexCoord a, core::HexCoord b) {
                            return (from == a && to == b) || (from == b && to == a);
                        };
                        matches &= edge({-2, 1}, {-1, 0}) || edge({-2, 1}, third);
                    }
                    if (!matches) continue;
                    io::SolutionData solution;
                    solution.puzzleName = puzzle.name;
                    solution.solutionName = "Serial mixed renewable-feed assembly";
                    solution.metrics = {48 * puzzle.outputMultiplier - 3, 55, 8, 8};
                    io::SolutionObject arm;
                    arm.typeName = "arm1";
                    arm.bodyData = {1, 0, 0, 8, 0};
                    arm.instructions = {{'G', 0}, {'r', 1}, {'A', 2}, {'A', 3},
                                        {'g', 4}, {'a', 5}, {'a', 6}, {'R', 7}};
                    solution.objects.push_back(arm);
                    io::SolutionObject track;
                    track.typeName = "track";
                    track.bodyData = {1, 0, 0, 0, 3, 0, 0, -1, 0, -2, 0, 0};
                    solution.objects.push_back(track);
                    for (auto inputIndex : {repeated, other}) {
                        io::SolutionObject input;
                        input.typeName = "input";
                        input.position =
                            core::Subtract(inputIndex == repeated ? core::HexCoord{1, 0} : third,
                                           puzzle.inputs[inputIndex].atoms[0].position);
                        input.bodyData = {1, 0, static_cast<int>(inputIndex), 0, 0};
                        solution.objects.push_back(input);
                    }
                    for (int station = 0; station < 2; ++station) {
                        io::SolutionObject bonder;
                        bonder.typeName = "bonder";
                        bonder.position =
                            station == 0 ? core::HexCoord{1, 0} : core::HexCoord{-2, 1};
                        bonder.bodyData = {1, station == 0 ? 2 : 3, 0, 0, 0};
                        solution.objects.push_back(bonder);
                    }
                    io::SolutionObject output;
                    output.typeName = "out-std";
                    output.position = offset;
                    output.bodyData = {1, turns, 0, 0, 0};
                    solution.objects.push_back(output);
                    return solution;
                }
            }
        }
        Reject("unverified mixed renewable-feed workstation geometry");
    }
    if (atoms.size() == 3) {
        if (puzzle.inputs.size() != 3 || puzzle.outputs[0].bonds.size() != 2 ||
            plan.operations.size() != 6 || plan.inputForAtom[0] == plan.inputForAtom[1] ||
            plan.inputForAtom[0] == plan.inputForAtom[2] ||
            plan.inputForAtom[1] == plan.inputForAtom[2])
            Reject(
                "three-atom workstation requires three independent feeds and two ordinary bonds");
        if (puzzle.outputMultiplier > std::numeric_limits<int>::max() / 36)
            Reject("three-atom cycle count exceeds supported range");
        const auto a = plan.operations[0].atom, b = plan.operations[1].atom;
        const auto c = plan.operations[3].atom;
        if (plan.operations[0].kind != Kind::Acquire || plan.operations[1].kind != Kind::Acquire ||
            plan.operations[2].kind != Kind::Bond || plan.operations[3].kind != Kind::Acquire ||
            plan.operations[4].kind != Kind::Bond || plan.operations[5].kind != Kind::Output)
            Reject("unsupported three-atom operation sequence");
        int turns = 0;
        while (turns < 6 && Rotate(core::Subtract(atoms[b].position, atoms[a].position), turns) !=
                                core::HexCoord{1, 0})
            ++turns;
        if (turns == 6) Reject("unreachable workstation orientation");
        const auto relative = Rotate(core::Subtract(atoms[c].position, atoms[a].position), turns);
        const bool reverse = relative.r < 0;
        const auto normalized =
            reverse ? core::HexCoord{relative.q + relative.r, -relative.r} : relative;
        int area = 0;
        // Exhaustive normalized placements for a connected two-edge triatomic.
        // Mirroring the entire motion keeps the incoming feed outside the sweep.
        if (normalized == core::HexCoord{2, 0})
            area = 40;
        else if (normalized == core::HexCoord{1, 1})
            area = 36;
        else if (normalized == core::HexCoord{0, 1})
            area = 34;
        else if (normalized == core::HexCoord{-1, 0})
            area = 33;
        else if (normalized == core::HexCoord{-1, 1})
            area = 34;
        else
            Reject("unverified three-atom workstation geometry");
        const int transfer = reverse ? 5 : 1;
        const int delivery = reverse ? 4 : 2;
        const auto station = [&](std::size_t atom) {
            return core::Add(
                core::HexCoord{3, 0},
                Rotate(core::Subtract(atoms[atom].position, atoms[a].position), turns));
        };
        // The first bond forms on its two input cells. The held pair then
        // acquires the third atom at a separate bonder after one rotation;
        // the second rotation clears that feed before dropping at output.
        io::SolutionData solution;
        solution.puzzleName = puzzle.name;
        solution.solutionName = "Serial growing-fragment assembly";
        solution.metrics = {36 * puzzle.outputMultiplier - 2, 40, area, 6};
        io::SolutionObject arm;
        arm.typeName = "arm1";
        arm.bodyData = {3, 0, 0, 6, 0};
        arm.instructions = {{'G', 0}, {'r', 1}, {'r', 2}, {'g', 3}, {'R', 4}, {'R', 5}};
        if (reverse)
            for (auto& instruction : arm.instructions) {
                if (instruction.action == 'r')
                    instruction.action = 'R';
                else if (instruction.action == 'R')
                    instruction.action = 'r';
            }
        solution.objects.push_back(arm);
        for (auto atom : {a, b, c}) {
            io::SolutionObject input;
            input.typeName = "input";
            input.position =
                core::Subtract(Rotate(station(atom), atom == c ? transfer : 0),
                               puzzle.inputs[plan.inputForAtom[atom]].atoms[0].position);
            input.bodyData = {1, 0, static_cast<int>(plan.inputForAtom[atom]), 0, 0};
            solution.objects.push_back(input);
        }
        for (auto step : {2U, 4U}) {
            const auto edge = plan.operations[step].edge;
            const auto from = Rotate(station(edge.a), step == 4 ? transfer : 0);
            const auto to = Rotate(station(edge.b), step == 4 ? transfer : 0);
            int direction = 0;
            while (direction < 6 && Rotate({1, 0}, direction) != core::Subtract(to, from))
                ++direction;
            if (direction == 6) Reject("unreachable bond station");
            io::SolutionObject bonder;
            bonder.typeName = "bonder";
            bonder.position = from;
            bonder.bodyData = {1, direction, 0, 0, 0};
            solution.objects.push_back(bonder);
        }
        io::SolutionObject out;
        out.typeName = "out-std";
        out.position = core::Subtract(Rotate({3, 0}, delivery),
                                      Rotate(atoms[a].position, (turns + delivery) % 6));
        out.bodyData = {1, (turns + delivery) % 6, 0, 0, 0};
        solution.objects.push_back(out);
        return solution;
    }
    if (puzzle.inputs.size() != 2 || puzzle.outputs[0].atoms.size() != 2 ||
        plan.operations.size() != 4 || plan.inputForAtom[0] == plan.inputForAtom[1])
        Reject("serial workstation compiles only two feeds used once into one bonded pair");
    const auto& acquireA = plan.operations[0];
    const auto& acquireB = plan.operations[1];
    const auto& bond = plan.operations[2];
    const auto& output = plan.operations[3];
    if (acquireA.kind != Kind::Acquire || acquireB.kind != Kind::Acquire ||
        bond.kind != Kind::Bond || output.kind != Kind::Output || bond.edge != Edge{0, 1})
        Reject("unsupported workstation operation sequence");
    // The feeds bond on the radial workstation before the grab. Moving the
    // pair clears both feed cells; the respawned pair stays outside its sweep.
    // A four-cycle serial tape transfers six products per output multiplier.
    io::SolutionData solution;
    solution.puzzleName = puzzle.name;
    solution.solutionName = "Serial ordinary-bond assembly";
    solution.metrics = {24 * puzzle.outputMultiplier - 1, 30, 19, 4};
    io::SolutionObject arm;
    arm.typeName = "arm1";
    arm.bodyData = {3, 0, 0, 4, 0};
    arm.instructions = {{'G', 0}, {'r', 1}, {'g', 2}, {'R', 3}};
    solution.objects.push_back(arm);
    for (const auto* acquire : {&acquireA, &acquireB}) {
        io::SolutionObject input;
        input.typeName = "input";
        input.position = core::Subtract(core::HexCoord{3 + static_cast<int>(acquire->atom), 0},
                                        puzzle.inputs[acquire->input].atoms[0].position);
        input.bodyData = {1, 0, static_cast<int>(acquire->input), 0, 0};
        solution.objects.push_back(input);
    }
    io::SolutionObject bonder;
    bonder.typeName = "bonder";
    bonder.position = {3, 0};
    bonder.bodyData = {1, 0, 0, 0, 0};
    solution.objects.push_back(bonder);
    io::SolutionObject out;
    out.typeName = "out-std";
    const auto delta = core::Subtract(atoms[1].position, atoms[0].position);
    int turns = 0;
    while (turns < 6 && Rotate(delta, turns) != core::HexCoord{0, 1})
        ++turns;
    if (turns == 6) Reject("unreachable output orientation");
    out.position = core::Subtract(core::HexCoord{0, 3}, Rotate(atoms[0].position, turns));
    out.bodyData = {1, turns, 0, 0, 0};
    solution.objects.push_back(out);
    return solution;
}

io::SolutionData CompileSharedAtomicOutputs(const io::PuzzleData&);
io::SolutionData CompileMetalPurification(const io::PuzzleData&);
io::SolutionData CompileTriplex(const io::PuzzleData&);
io::SolutionData CompileDispersion(const io::PuzzleData&);
io::SolutionData CompileBalancedDispersion(const io::PuzzleData&);
io::SolutionData CompileLifeDeath(const io::PuzzleData&);
io::SolutionData CompileUnification(const io::PuzzleData&);

io::SolutionData AssemblySolver::Compile(const io::PuzzleData& puzzle, const AssemblyPlan& plan) {
    AssemblyPlanner::Validate(puzzle, plan);
    bool chemistry = false;
    for (std::size_t i = 0; i < plan.inputForAtom.size(); ++i)
        chemistry |= puzzle.inputs[plan.inputForAtom[i]].atoms[plan.sourceAtomForAtom[i]].type !=
                     puzzle.outputs[0].atoms[i].type;
    if (chemistry || std::any_of(puzzle.inputs.begin(), puzzle.inputs.end(),
                                 [](const auto& input) { return input.atoms.size() != 1; }))
        return CompileRowAssembly(puzzle, plan);
    try {
        return CompileSpecialized(puzzle, plan);
    } catch (const std::invalid_argument& specialized) {
        try {
            return CompileRowAssembly(puzzle, plan);
        } catch (const std::invalid_argument& row) {
            throw std::invalid_argument(std::string("specialized-assembly: ") + specialized.what() +
                                        "; row-assembly: " + row.what());
        }
    }
}

bool AssemblySolver::Supports(const io::PuzzleData& puzzle) noexcept {
    try {
        (void)AssemblySolver{}.Solve(puzzle);
        return true;
    } catch (...) {
        return false;
    }
}
io::SolutionData AssemblySolver::Solve(const io::PuzzleData& puzzle) {
    return SolveAccepted(puzzle, [](const io::SolutionData&) { return true; });
}
io::SolutionData AssemblySolver::SolveAccepted(const io::PuzzleData& puzzle,
                                               const CandidateAcceptance& accept) {
    return SolveCandidates(puzzle, accept, nullptr);
}
io::SolutionData AssemblySolver::SolveVerified(const io::PuzzleData& puzzle,
                                               const CandidateVerification& verify) {
    return SolveVerified(puzzle, verify, {});
}
io::SolutionData AssemblySolver::SolveVerified(const io::PuzzleData& puzzle,
                                               const CandidateVerification& verify,
                                               const AssemblyFeedback& feedback) {
    return SolveCandidates(
        puzzle, [&](const io::SolutionData& candidate) { return verify(candidate).Passed(); },
        &verify, feedback);
}
io::SolutionData AssemblySolver::CollectVerified(const io::PuzzleData& puzzle,
                                                 const CandidateSink& sink,
                                                 std::size_t maxAccepted) {
    CandidateVerification verify = [&](const auto& candidate) {
        return sink(candidate, "assembly");
    };
    return SolveCandidates(puzzle, {}, &verify, {}, &sink, std::max(std::size_t{1}, maxAccepted));
}
io::SolutionData AssemblySolver::SolveCandidates(const io::PuzzleData& puzzle,
                                                 const CandidateAcceptance& accept,
                                                 const CandidateVerification* verify,
                                                 const AssemblyFeedback& feedback,
                                                 const CandidateSink* sink,
                                                 std::size_t maxAccepted) {
    std::optional<io::SolutionData> firstAccepted;
    std::string firstAcceptedStage;
    std::size_t submissions = 0, duplicates = 0, acceptedCount = 0;
    // The local intake machine and the shared feeder machine are distinct tape
    // sets, so one extra acceptance is allowed when both are in play; the three
    // objective slots are then compared independently.
    std::size_t acceptedCap = maxAccepted;
    // Slot-aware admission (C5): a candidate that strictly improves one of the
    // three objective slots may consume one of a small number of extra budget
    // slots even after the archiving cap is reached. Measured neutral on the
    // 146-puzzle corpus (55/53/43/51 either way), so it stays opt-in: the
    // binding constraint is the machine quality, not the acceptance budget.
    constexpr std::size_t kMaxExtraSlotAdmissions = 2;
    const char* slotAdmissionFlag = std::getenv("OPUSOPTIMA_SLOT_ADMISSION");
    const bool slotAdmission =
        sink && slotAdmissionFlag && std::string_view(slotAdmissionFlag) == "on";
    std::int64_t bestSlot[3] = {std::numeric_limits<std::int64_t>::max(),
                                std::numeric_limits<std::int64_t>::max(),
                                std::numeric_limits<std::int64_t>::max()};
    std::vector<std::pair<std::vector<std::byte>, io::VerificationReport>> seen;
    bool nativeAdmission = false;
    bool staticEmbeddingAttempted = false;
    std::size_t constructionAttempts = 0;
    auto verifyAt = [&](const char* stage, const io::SolutionData& candidate) {
        auto requireNative = [&](io::VerificationReport report) {
            return io::RequireCalcifyAnimismusAuthority(stage, std::move(report));
        };
        if (!sink) return requireNative((*verify)(candidate));
        if (submissions >= MaxCandidateSubmissions || acceptedCount >= acceptedCap ||
            std::chrono::steady_clock::now() >= Deadline())
            throw std::runtime_error("bounded candidate collection stopped");
        auto canonical = candidate;
        canonical.solutionName.clear();
        canonical.metrics = {};
        auto bytes = io::BinaryWriter::WriteSolution(canonical);
        for (const auto& prior : seen) {
            if (prior.first == bytes) {
                ++duplicates;
                GetLoggerOrFallback("solver")->info(
                    "Assembly branch={} event=deduplicated accepted={}", stage,
                    prior.second.Passed());
                return requireNative(prior.second);
            }
        }
        ++submissions;
        auto report = requireNative((*sink)(candidate, stage));
        // Static layouts require native timing/spawn semantics. Derive admission
        // from the accepted parent's actual gate, not the compiled worker path.
        if (report.Passed() && acceptedCount == 0) {
            nativeAdmission = report.authority == io::VerificationAuthority::LegacyGate;
            firstAcceptedStage = stage;
        }
        acceptedCount += report.Passed();
        if (slotAdmission && report.Passed()) {
            const std::int64_t slotMetrics[3] = {report.metrics.cost, report.metrics.cycles,
                                                 report.metrics.area};
            bool improves = false;
            for (int slot = 0; slot < 3; ++slot) {
                if (slotMetrics[slot] < bestSlot[slot]) {
                    bestSlot[slot] = slotMetrics[slot];
                    improves = true;
                }
            }
            if (improves && acceptedCap < maxAccepted + kMaxExtraSlotAdmissions) ++acceptedCap;
        }
        GetLoggerOrFallback("solver")->info(
            "Assembly branch={} event=gate accepted={} submission={}", stage, report.Passed(),
            submissions);
        seen.emplace_back(std::move(bytes), report);
        return report;
    };
    std::vector<std::string> rejections;
    auto planningFailure = [&](const char* stage, const std::string& detail) {
        rejections.push_back(std::string(stage) + ": " + detail);
        if (feedback)
            feedback(
                {AssemblyFailure::Category::Planning, stage, detail, std::nullopt, std::nullopt});
    };
    constexpr const char* wheelStage = "finite-wheel-second-source";
    bool wheelSecondSourceAttempted = false;
    const bool wheelSecondSourceEligible = verify && SupportsFiniteWheelSecondSource(puzzle);
    auto tryWheelSecondSource = [&]() -> std::optional<io::SolutionData> {
        wheelSecondSourceAttempted = true;
        const auto outerDeadline = Deadline();
        std::exception_ptr callbackError;
        std::optional<io::VerificationReport> gateReport;
        auto failure = [&](const std::string& detail) {
            if (gateReport && !gateReport->Passed()) {
                rejections.push_back(std::string(wheelStage) + ": " + detail);
                if (feedback)
                    feedback({gateReport->status == io::VerificationStatus::Unavailable
                                  ? AssemblyFailure::Category::VerificationUnavailable
                              : gateReport->status == io::VerificationStatus::Error
                                  ? AssemblyFailure::Category::InfrastructureError
                                  : AssemblyFailure::Category::IndependentVerifier,
                              wheelStage, detail, gateReport, std::nullopt});
            } else
                planningFailure(wheelStage, detail);
        };
        try {
            return CompileFiniteWheelSecondSource(
                puzzle, outerDeadline,
                [&](const io::SolutionData& candidate, auto suppliedDeadline) {
                    try {
                        if (suppliedDeadline != outerDeadline)
                            throw std::logic_error("finite wheel deadline changed");
                        if (std::chrono::steady_clock::now() >= outerDeadline)
                            throw std::runtime_error("finite wheel deadline exhausted");
                        gateReport = verifyAt(wheelStage, candidate);
                        return *gateReport;
                    } catch (...) {
                        callbackError = std::current_exception();
                        throw;
                    }
                });
        } catch (const ExecutionIR::LayoutFailure& error) {
            if (callbackError) std::rethrow_exception(callbackError);
            failure(error.what());
        } catch (const std::invalid_argument& error) {
            if (callbackError) std::rethrow_exception(callbackError);
            failure(error.what());
        }
        return std::nullopt;
    };
    auto keep = [&](const io::SolutionData& candidate) {
        if (!firstAccepted) firstAccepted = candidate;
        if (!sink) return true;
        // Baseline gets its original first-feasible route. Only then spend at most
        // three construction attempts; no late family can erase that first result.
        bool ordinaryPortfolio = maxAccepted > 1 && !puzzle.productionInfo &&
                                 puzzle.outputs.size() == 1 &&
                                 !HasRepeatingOutput(puzzle.outputs[0]);
        const auto canConstruct = [&] {
            return constructionAttempts < MaxPortfolioConstructions &&
                   submissions < MaxCandidateSubmissions && acceptedCount < maxAccepted &&
                   std::chrono::steady_clock::now() < Deadline();
        };
        if (nativeAdmission && wheelSecondSourceEligible && !wheelSecondSourceAttempted &&
            canConstruct()) {
            ++constructionAttempts;
            (void)tryWheelSecondSource();
        }
        std::optional<StaticEmbeddingSuppressionReason> staticEmbeddingSuppression;
        const char* staticReactionFlag = std::getenv("OPUSOPTIMA_STATIC_REACTION");
        if (nativeAdmission &&
            (!staticReactionFlag || std::string_view(staticReactionFlag) != "off") &&
            canConstruct()) {
            try {
                const auto compiled = CompileStaticReactionWithFamily(puzzle, Deadline());
                ++constructionAttempts;
                const char* stage = nullptr;
                switch (compiled.family) {
                case StaticReactionFamily::Purification:
                    stage = "portfolio-static-reaction-purification";
                    break;
                case StaticReactionFamily::LegacyConsumers:
                    stage = "portfolio-static-reaction";
                    break;
                case StaticReactionFamily::OrdinaryPair:
                    stage = "portfolio-static-reaction-ordinary-pair";
                    break;
                }
                if (std::chrono::steady_clock::now() < Deadline() &&
                    submissions < MaxCandidateSubmissions) {
                    const auto report = verifyAt(stage, compiled.solution);
                    staticEmbeddingSuppression =
                        StaticEmbeddingSuppressionAfterReaction(puzzle, compiled.family, report);
                }
            } catch (const std::invalid_argument& error) {
                GetLoggerOrFallback("solver")->info(
                    "Assembly branch=portfolio-static-reaction event=unsupported detail={}",
                    error.what());
            }
        }
        const char* staticEmbeddingFlag = std::getenv("OPUSOPTIMA_STATIC_EMBEDDING");
        if (nativeAdmission && !staticEmbeddingAttempted &&
            (!staticEmbeddingFlag || std::string_view(staticEmbeddingFlag) != "off") &&
            canConstruct() && staticEmbeddingSuppression) {
            GetLoggerOrFallback("solver")->info(
                "Assembly branch=portfolio-static-embedding event=suppressed "
                "reason=accepted-singleton-purification construction={}",
                constructionAttempts);
        }
        if (nativeAdmission && !staticEmbeddingAttempted && !staticEmbeddingSuppression &&
            (!staticEmbeddingFlag || std::string_view(staticEmbeddingFlag) != "off") &&
            canConstruct()) {
            staticEmbeddingAttempted = true;
            ++constructionAttempts;
            const char* sharedFeedFlag = std::getenv("OPUSOPTIMA_SHARED_FEED_RESOURCE");
            const bool sharedFeed =
                sharedFeedFlag && std::string_view(sharedFeedFlag) == "on" &&
                firstAcceptedStage == "life-death" && !puzzle.outputs.empty() &&
                puzzle.outputs.size() <= 2 &&
                std::any_of(puzzle.inputs.begin(), puzzle.inputs.end(),
                            [](const auto& input) { return input.atoms.size() > 1; }) &&
                StaticEmbeddingMissingLifeFeed(puzzle);
            const char* stage =
                sharedFeed ? "portfolio-shared-life-death" : "portfolio-static-embedding";
            try {
                const auto station = sharedFeed ? CompileSharedLifeDeath(puzzle, Deadline())
                                                : CompileStaticEmbedding(puzzle, Deadline());
                if (std::chrono::steady_clock::now() < Deadline() &&
                    submissions < MaxCandidateSubmissions)
                    verifyAt(stage, station);
            } catch (const std::invalid_argument& error) {
                GetLoggerOrFallback("solver")->info(
                    "Assembly branch={} event=unsupported detail={}", stage, error.what());
            }
        }
#ifdef OPUSOPTIMA_REFERENCE_WORKER_PATH
        const char* rigidCarrierFlag = std::getenv("OPUSOPTIMA_RIGID_CARRIER");
        if (nativeAdmission && (!rigidCarrierFlag || std::string_view(rigidCarrierFlag) != "off") &&
            canConstruct()) {
            const auto preflight = PreflightRigidCarrier(puzzle, Deadline());
            GetLoggerOrFallback("solver")->info(
                "Assembly branch=portfolio-rigid-carrier event=preflight status={} reason={} "
                "construction={} submissions={}",
                preflight.status == RigidCarrierPreflight::Status::Unsupported ? "Unsupported"
                                                                               : "Unknown",
                preflight.reason ? RigidCarrierReasonName(*preflight.reason) : "unknown",
                constructionAttempts, submissions);
            if (preflight.status == RigidCarrierPreflight::Status::Unknown) {
                ++constructionAttempts;
                GetLoggerOrFallback("solver")->info(
                    "Assembly branch=portfolio-rigid-carrier event=attempt construction={}",
                    constructionAttempts);
                try {
                    const auto carrier = CompileRigidCarrierWithFamily(puzzle, Deadline());
                    const char* stage = carrier.family == RigidCarrierFamily::StaticCopy
                                            ? "portfolio-static-input-copy"
                                        : carrier.family == RigidCarrierFamily::StaticPartition
                                            ? "portfolio-static-molecule-partition"
                                            : "portfolio-rigid-carrier";
                    if (std::chrono::steady_clock::now() < Deadline() &&
                        submissions < MaxCandidateSubmissions)
                        verifyAt(stage, carrier.solution);
                } catch (const std::invalid_argument& error) {
                    GetLoggerOrFallback("solver")->info(
                        "Assembly branch=portfolio-rigid-carrier event=unsupported detail={}",
                        error.what());
                }
            }
        }
#endif
        const char* singletonChainFlag = std::getenv("OPUSOPTIMA_SINGLETON_PURIFICATION_CHAIN");
        if (nativeAdmission && maxAccepted > 1 &&
            (!singletonChainFlag || std::string_view(singletonChainFlag) == "on") &&
            canConstruct() && AdmitsSingletonPurificationChain(puzzle)) {
            ++constructionAttempts;
            try {
                const auto chain = CompileSingletonPurificationChain(puzzle, Deadline());
                if (std::chrono::steady_clock::now() < Deadline() &&
                    submissions < MaxCandidateSubmissions && acceptedCount < maxAccepted)
                    verifyAt("portfolio-singleton-purification-chain", chain);
            } catch (const std::invalid_argument& error) {
                GetLoggerOrFallback("solver")->info(
                    "Assembly branch=portfolio-singleton-purification-chain event=unsupported "
                    "detail={}",
                    error.what());
            }
        }
        const char* radialWheelFlag = std::getenv("OPUSOPTIMA_RADIAL_WHEEL");
        const char* radialProjectionFlag = std::getenv("OPUSOPTIMA_RADIAL_PROJECTION");
        if (nativeAdmission && maxAccepted > 1 && canConstruct()) {
            const bool wheel = (!radialWheelFlag || std::string_view(radialWheelFlag) == "on") &&
                               SupportsRadialWheelAssembly(puzzle);
            const bool projection =
                !wheel &&
                (!radialProjectionFlag || std::string_view(radialProjectionFlag) == "on") &&
                SupportsRadialProjectionAssembly(puzzle);
            if (wheel || projection) {
                const char* stage =
                    projection ? "portfolio-radial-projection" : "portfolio-radial-wheel";
                ++constructionAttempts;
                try {
                    const auto radial = projection ? CompileRadialProjectionAssembly(puzzle)
                                                   : CompileRadialWheelAssembly(puzzle);
                    if (std::chrono::steady_clock::now() < Deadline() &&
                        submissions < MaxCandidateSubmissions && acceptedCount < maxAccepted)
                        verifyAt(stage, radial);
                } catch (const std::invalid_argument& error) {
                    GetLoggerOrFallback("solver")->info(
                        "Assembly branch={} event=unsupported detail={}", stage, error.what());
                }
            }
        }
        const char* compactSpareFlag = std::getenv("OPUSOPTIMA_COMPACT_SPARE");
        const bool compactSpare = maxAccepted > 1 && compactSpareFlag &&
                                  std::string_view(compactSpareFlag) == "on" &&
                                  SupportsCompactSpareChemistry(puzzle);
        if (compactSpare && canConstruct()) {
            ++constructionAttempts;
            try {
                const auto compact = CompileCompactSpareChemistry(puzzle);
                if (std::chrono::steady_clock::now() < Deadline() &&
                    submissions < MaxCandidateSubmissions)
                    verifyAt("portfolio-compact-reaction-outlet", compact);
            } catch (const std::invalid_argument& error) {
                GetLoggerOrFallback("solver")->info(
                    "Assembly branch=portfolio-compact-reaction-outlet event=unsupported detail={}",
                    error.what());
            }
        }
        if (!compactSpare && sharedAssemblyEnabled_ && maxAccepted > 1 &&
            puzzle.outputs.size() == 2 && canConstruct()) {
            ++constructionAttempts;
            try {
                const auto shared =
                    puzzle.inputs.size() == 2
                        ? CompileMixedInventoryAssembly(puzzle, sharedSchedulingEnabled_,
                                                        sharedBufferRelayEnabled_)
                        : CompileSharedAnimismusAssembly(puzzle);
                if (std::chrono::steady_clock::now() < Deadline() &&
                    submissions < MaxCandidateSubmissions)
                    verifyAt("portfolio-shared-animismus-assembly", shared.solution);
            } catch (const std::invalid_argument& error) {
                GetLoggerOrFallback("solver")->info(
                    "Assembly branch=portfolio-shared-animismus-assembly event=unsupported "
                    "detail={}",
                    error.what());
            }
        }
        if (sharedAssemblyEnabled_ && sharedBatchConsumerEnabled_ && !sharedNearCacheEnabled_ &&
            puzzle.inputs.size() == 2 && puzzle.outputs.size() == 2 && canConstruct()) {
            ++constructionAttempts;
            try {
                const auto batch = CompileMixedInventoryAssembly(puzzle, sharedSchedulingEnabled_,
                                                                 sharedBufferRelayEnabled_, true);
                if (canConstruct()) verifyAt("portfolio-shared-batch-consumer", batch.solution);
            } catch (const std::invalid_argument& error) {
                GetLoggerOrFallback("solver")->info(
                    "Assembly branch=portfolio-shared-batch-consumer event=unsupported detail={}",
                    error.what());
            }
        }
        if (sharedAssemblyEnabled_ && sharedNearCacheEnabled_ && puzzle.inputs.size() == 2 &&
            puzzle.outputs.size() == 2 && canConstruct()) {
            ++constructionAttempts;
            try {
                const auto cache = CompileMixedInventoryAssembly(puzzle, sharedSchedulingEnabled_,
                                                                 sharedBufferRelayEnabled_,
                                                                 sharedBatchConsumerEnabled_, true);
                if (std::chrono::steady_clock::now() < Deadline() &&
                    submissions < MaxCandidateSubmissions && acceptedCount < maxAccepted)
                    verifyAt("portfolio-shared-near-cache", cache.solution);
            } catch (const std::invalid_argument& error) {
                GetLoggerOrFallback("solver")->info(
                    "Assembly branch=portfolio-shared-near-cache event=unsupported detail={}",
                    error.what());
            }
        }
        if (ordinaryPortfolio && std::chrono::steady_clock::now() < Deadline()) {
            try {
                const auto plan = AssemblyPlanner::Plan(puzzle, false, Deadline());
                const auto& atoms = puzzle.outputs[0].atoms;
                const auto width = std::minmax_element(
                    atoms.begin(), atoms.end(),
                    [](const auto& a, const auto& b) { return a.position.q < b.position.q; });
                const char* retiming = std::getenv("OPUSOPTIMA_ROW_RETIMING");
                const auto rowPermissions = (1ULL << 2) | (1ULL << 3);
                const bool scheduleRows =
                    nativeAdmission && !atoms.empty() &&
                    (!retiming || std::string_view(retiming) != "off") &&
                    (puzzle.permissionBits & rowPermissions) == rowPermissions &&
                    width.second->position.q > width.first->position.q;
                const char* clockPortfolio = std::getenv("OPUSOPTIMA_ROW_CLOCK_PORTFOLIO");
                const bool clockPortfolioEnabled =
                    !clockPortfolio || std::string_view(clockPortfolio) != "off";
                const char* gapPortfolio = std::getenv("OPUSOPTIMA_ROW_GAP_PORTFOLIO");
                const bool gapPortfolioEnabled =
                    !gapPortfolio || std::string_view(gapPortfolio) != "off";
                const char* clockLocal = std::getenv("OPUSOPTIMA_ROW_CLOCK_LOCAL");
                const bool clockLocalEnabled = !clockLocal || std::string_view(clockLocal) != "off";
                for (int variant = 0; variant < 3 && canConstruct(); ++variant) {
                    if (std::chrono::steady_clock::now() >= Deadline()) break;
                    const char* componentMode = std::getenv("OPUSOPTIMA_ROW_COMPONENT");
                    const bool component =
                        variant == 2 &&
                        (componentMode && std::string_view(componentMode) == "on") &&
                        SupportsComponentRowAssembly(puzzle);
                    const char* sharedMode = std::getenv("OPUSOPTIMA_SHARED_WORKSTATION");
                    const bool shared =
                        variant == 1 && sharedMode && std::string_view(sharedMode) == "on";
                    const bool relay = variant == 0 && compactRelayEnabled_;
                    const bool scheduled = variant == 0 && !relay && scheduleRows;
                    const char* family = relay          ? "portfolio-compact-chemistry-relay"
                                         : shared       ? "portfolio-serial-bond-station"
                                         : component    ? "portfolio-row-component"
                                         : scheduled    ? "portfolio-row-retimed"
                                         : variant == 0 ? "portfolio-row-compact"
                                         : variant == 1 ? "portfolio-shared-workstation"
                                                        : "portfolio-row-clearance";
                    GetLoggerOrFallback("solver")->info("Assembly branch={} event=reached", family);
                    if (variant != 1 && (puzzle.permissionBits & ((1ULL << 2) | (1ULL << 3))) !=
                                            ((1ULL << 2) | (1ULL << 3))) {
                        GetLoggerOrFallback("solver")->info(
                            "Assembly branch={} event=permission-skip", family);
                        continue;
                    }
                    ++constructionAttempts;
                    GetLoggerOrFallback("solver")->info(
                        "Assembly branch={} event=attempt construction={}", family,
                        constructionAttempts);
                    std::optional<io::SolutionData> alternative;
                    std::optional<io::SolutionData> compactFallback;
                    std::optional<io::SolutionData> compactFeeder;
                    std::optional<io::SolutionData> clockedAlternative;
                    std::optional<io::SolutionData> clockedFallback;
                    std::optional<io::SolutionData> clockedFeeder;
                    bool compactUsesIntake = false;
                    bool clockedUsesIntake = false;
                    try {
                        if (variant == 1 &&
                            (std::any_of(
                                 puzzle.inputs.begin(), puzzle.inputs.end(),
                                 [](const auto& input) { return input.atoms.size() != 1; }) ||
                             [&] {
                                 for (std::size_t i = 0; i < plan.inputForAtom.size(); ++i)
                                     if (puzzle.inputs[plan.inputForAtom[i]].atoms[0].type !=
                                         puzzle.outputs[0].atoms[i].type)
                                         return true;
                                 return false;
                             }())) {
                            GetLoggerOrFallback("solver")->info(
                                "Assembly branch={} event=unsupported", family);
                            continue;
                        }
                        if (relay) {
                            alternative = CompileCompactChemistryRelay(puzzle);
                        } else if (shared) {
                            alternative = CompileSharedWorkstation(puzzle);
                        } else if (component) {
                            alternative = CompileComponentRowAssembly(puzzle, plan);
                        } else if (variant == 1) {
                            alternative = CompileSpecialized(puzzle, plan);
                        } else if (scheduled) {
                            if (clockPortfolioEnabled || gapPortfolioEnabled) {
                                auto pair = CompileScheduledRowCandidates(puzzle, plan, Deadline());
                                compactUsesIntake = pair.compactUsesIntake;
                                clockedUsesIntake = pair.clockedUsesIntake;
                                compactFeeder = std::move(pair.compactFeeder);
                                clockedFeeder = std::move(pair.clockedFeeder);
                                if (gapPortfolioEnabled && pair.compactGap0) {
                                    // gap0 is aggressive; gap2 stays as the native fallback.
                                    alternative = std::move(*pair.compactGap0);
                                    compactFallback = std::move(pair.compact);
                                } else {
                                    alternative = std::move(pair.compact);
                                }
                                if (clockPortfolioEnabled) {
                                    if (pair.clockedGap0) {
                                        clockedAlternative = std::move(*pair.clockedGap0);
                                        if (pair.hasClocked)
                                            clockedFallback = std::move(pair.clocked);
                                    } else if (pair.hasClocked) {
                                        clockedAlternative = std::move(pair.clocked);
                                    }
                                }
                            } else {
                                alternative = CompileScheduledRowAssembly(puzzle, plan, Deadline());
                            }
                        } else {
                            alternative = CompileRowAssembly(puzzle, plan, variant == 0 ? 3 : 4);
                        }
                    } catch (const std::invalid_argument& error) {
                        GetLoggerOrFallback("solver")->info(
                            "Assembly branch={} event=unsupported detail={}", family, error.what());
                    }
                    if (alternative && std::chrono::steady_clock::now() < Deadline() &&
                        submissions < MaxCandidateSubmissions) {
                        const auto report = verifyAt(family, *alternative);
                        if (!report.Passed() && compactFallback && acceptedCount < acceptedCap &&
                            std::chrono::steady_clock::now() < Deadline() &&
                            submissions < MaxCandidateSubmissions)
                            verifyAt("portfolio-row-retimed-gap2", *compactFallback);
                        // The intake machine is a different set of arm tapes, so a
                        // native rejection falls back to the precompiled shared feeder
                        // companion of the same family.
                        if (compactFeeder && !report.Passed() && acceptedCount < acceptedCap &&
                            std::chrono::steady_clock::now() < Deadline() &&
                            submissions < MaxCandidateSubmissions) {
                            try {
                                auto feeder = std::move(*compactFeeder);
                                compactFeeder.reset();
                                const auto feederReport = verifyAt("portfolio-row-feeder", feeder);
                                if (!feederReport.Passed() && compactFallback &&
                                    gapPortfolioEnabled && acceptedCount < acceptedCap &&
                                    std::chrono::steady_clock::now() < Deadline() &&
                                    submissions < MaxCandidateSubmissions) {
                                    auto retry = std::move(*compactFallback);
                                    compactFallback.reset();
                                    verifyAt("portfolio-row-feeder-gap2", retry);
                                }
                            } catch (const std::invalid_argument& error) {
                                GetLoggerOrFallback("solver")->info(
                                    "Assembly branch=portfolio-row-feeder event=unsupported "
                                    "detail={}",
                                    error.what());
                            }
                        }
                    }
                    if (clockedAlternative && acceptedCount < acceptedCap &&
                        std::chrono::steady_clock::now() < Deadline() &&
                        submissions < MaxCandidateSubmissions) {
                        io::SolutionData clocked = std::move(*clockedAlternative);
                        std::optional<io::SolutionData> rawClocked;
                        if (clockLocalEnabled) {
                            try {
                                auto compacted = solvers::LocalTrial(clocked, 8);
                                rawClocked = std::move(clocked);
                                clocked = std::move(compacted);
                            } catch (const std::exception&) {
                                // Keep the raw clocked machine if the local pass declines.
                            }
                        }
                        const auto report = verifyAt("portfolio-row-clocked", clocked);
                        if (!report.Passed() && rawClocked && acceptedCount < acceptedCap &&
                            std::chrono::steady_clock::now() < Deadline() &&
                            submissions < MaxCandidateSubmissions)
                            verifyAt("portfolio-row-clocked-raw", *rawClocked);
                        if (!report.Passed() && clockedFallback && acceptedCount < acceptedCap &&
                            std::chrono::steady_clock::now() < Deadline() &&
                            submissions < MaxCandidateSubmissions)
                            verifyAt("portfolio-row-clocked-gap2", *clockedFallback);
                        // The intake clocked machine has different absolute gaps than
                        // the feeder one, so a rejection falls back to the precompiled
                        // feeder clocked companion instead of dropping the cycles slot.
                        if (clockedFeeder && !report.Passed() && acceptedCount < acceptedCap &&
                            std::chrono::steady_clock::now() < Deadline() &&
                            submissions < MaxCandidateSubmissions) {
                            try {
                                auto feeder = std::move(*clockedFeeder);
                                clockedFeeder.reset();
                                std::optional<io::SolutionData> rawFeeder;
                                if (clockLocalEnabled) {
                                    try {
                                        auto compacted = solvers::LocalTrial(feeder, 8);
                                        rawFeeder = std::move(feeder);
                                        feeder = std::move(compacted);
                                    } catch (const std::exception&) {
                                        // Keep the raw feeder machine.
                                    }
                                }
                                const auto feederReport =
                                    verifyAt("portfolio-row-clocked-feeder", feeder);
                                if (!feederReport.Passed() && rawFeeder &&
                                    acceptedCount < acceptedCap &&
                                    std::chrono::steady_clock::now() < Deadline() &&
                                    submissions < MaxCandidateSubmissions)
                                    verifyAt("portfolio-row-clocked-feeder-raw", *rawFeeder);
                            } catch (const std::invalid_argument& error) {
                                GetLoggerOrFallback("solver")->info(
                                    "Assembly branch=portfolio-row-clocked-feeder "
                                    "event=unsupported detail={}",
                                    error.what());
                            }
                        }
                    }
                }
                const auto* structure = std::getenv("OPUSOPTIMA_ROW_STRUCTURE");
                if ((!structure || std::string(structure) != "off") && canConstruct()) {
                    ++constructionAttempts;
                    try {
                        auto child = CompileStructuredRowAssembly(puzzle, plan);
                        verifyAt("portfolio-row-structure", child);
                    } catch (const std::invalid_argument&) {
                        // Unsupported geometry leaves the verified parent untouched.
                    }
                }
                const auto* critical = std::getenv("OPUSOPTIMA_ROW_CRITICALPATH");
                if ((!critical || std::string_view(critical) != "off") && canConstruct()) {
                    ++constructionAttempts;
                    try {
                        auto child = CompileCloseFeedRowAssembly(puzzle, plan);
                        verifyAt("portfolio-row-criticalpath", child);
                    } catch (const std::invalid_argument&) {
                        // Loaded motion remains subject to the full candidate gate.
                    }
                }
            } catch (const std::invalid_argument&) {
                // No ordinary plan: retain the existing chemistry/material family.
            }
        }
        GetLoggerOrFallback("solver")->info(
            "Assembly portfolio: submitted={} accepted={} deduplicated={} constructions={} "
            "construction_limit={}",
            submissions, acceptedCount, duplicates, constructionAttempts,
            MaxPortfolioConstructions);
        // Other output modes have no post-first structural portfolio. Continuing
        // their fallback chain can compile expensive candidates after publication.
        return true;
    };
    std::optional<io::VerificationReport> acceptedReport;
    auto acceptStage = [&](const char* stage, const io::SolutionData& candidate) {
        if (!verify) return accept(candidate);
        const auto report = verifyAt(stage, candidate);
        if (report.Passed())
            acceptedReport = report;
        else if (feedback)
            feedback({report.status == io::VerificationStatus::Unavailable
                          ? AssemblyFailure::Category::VerificationUnavailable
                      : report.status == io::VerificationStatus::Error
                          ? AssemblyFailure::Category::InfrastructureError
                          : AssemblyFailure::Category::IndependentVerifier,
                      stage, report.detail, report, std::nullopt});
        return report.Passed();
    };
    auto acceptedSolution = [&](io::SolutionData candidate) {
        if (acceptedReport) candidate.metrics = acceptedReport->metrics;
        acceptedReport.reset();
        return candidate;
    };
    auto inventory = [&](std::exception_ptr& acceptanceError) {
        if (verify) {
            return CompileVerifiedSupplyInventory(
                puzzle,
                [&](const io::SolutionData& candidate) {
                    try {
                        return verifyAt("automatic-inventory", candidate);
                    } catch (...) {
                        acceptanceError = std::current_exception();
                        throw;
                    }
                },
                [&](const SupplyFailure& failure) {
                    rejections.push_back("automatic-inventory: " + failure.detail);
                    if (feedback) {
                        try {
                            feedback(
                                {failure.category == SupplyFailureCategory::VerificationUnavailable
                                     ? AssemblyFailure::Category::VerificationUnavailable
                                 : failure.verification
                                     ? AssemblyFailure::Category::IndependentVerifier
                                     : AssemblyFailure::Category::Planning,
                                 "automatic-inventory", failure.detail, failure.verification,
                                 failure});
                        } catch (...) {
                            acceptanceError = std::current_exception();
                            throw;
                        }
                    }
                    GetLoggerOrFallback("solver")->debug(
                        "Inventory attempt {} period {} rotation {} spacing {} rejected: {}",
                        failure.materialAttempt, failure.productsPerPeriod, failure.rotation,
                        failure.spacing, failure.detail);
                });
        }
        return CompileAcceptedSupplyInventory(puzzle, [&](const io::SolutionData& candidate) {
            try {
                return accept(candidate);
            } catch (...) {
                acceptanceError = std::current_exception();
                throw;
            }
        });
    };
    auto attempt = [&](const char* stage, auto&& compile) -> std::optional<io::SolutionData> {
        std::optional<io::SolutionData> candidate;
        const auto started = std::chrono::steady_clock::now();
        auto elapsed = [&] {
            return std::chrono::duration_cast<std::chrono::milliseconds>(
                       std::chrono::steady_clock::now() - started)
                .count();
        };
        try {
            candidate = compile();
        } catch (const std::invalid_argument& error) {
            GetLoggerOrFallback("solver")->debug("Assembly stage {} rejected after {} ms: {}",
                                                 stage, elapsed(), error.what());
            planningFailure(stage, error.what());
            return std::nullopt;
        }
        GetLoggerOrFallback("solver")->debug("Assembly stage {} compiled in {} ms", stage,
                                             elapsed());
        if (verify) {
            const auto report = verifyAt(stage, *candidate);
            if (report.Passed()) {
                candidate->metrics = report.metrics;
                return candidate;
            }
            if (feedback)
                feedback({report.status == io::VerificationStatus::Unavailable
                              ? AssemblyFailure::Category::VerificationUnavailable
                              : AssemblyFailure::Category::IndependentVerifier,
                          stage, report.detail, report, std::nullopt});
        } else if (accept(*candidate))
            return candidate;
        rejections.push_back(std::string(stage) + ": candidate failed acceptance");
        return std::nullopt;
    };
    auto attemptAccepted = [&](const char* stage,
                               auto compiler) -> std::optional<io::SolutionData> {
        std::exception_ptr acceptanceError;
        try {
            auto result = compiler(puzzle, [&](const auto& candidate) {
                try {
                    return acceptStage(stage, candidate);
                } catch (...) {
                    acceptanceError = std::current_exception();
                    throw;
                }
            });
            if (result) return acceptedSolution(std::move(*result));
        } catch (const std::invalid_argument& error) {
            if (acceptanceError) std::rethrow_exception(acceptanceError);
            planningFailure(stage, error.what());
        }
        return std::nullopt;
    };
    const auto* wholeFeedFlag = std::getenv("OPUSOPTIMA_WHOLE_FEED_DOCKING");
    if (verify && wholeFeedFlag && std::string_view(wholeFeedFlag) == "1" &&
        SupportsWholeFeedDocking(puzzle) && std::chrono::steady_clock::now() < Deadline()) {
        constexpr auto stage = "whole-feed-singleton-receivers";
        using IR = ExecutionIR;
        IR material;
        auto state = IR::Initial(puzzle);
        material.AppendFromTail(puzzle, state, {IR::Kind::Acquire, 0});
        const auto bonds = state.bonds;
        for (const auto& [edge, bits] : bonds)
            material.AppendFromTail(puzzle, state, {IR::Kind::Unbond, 0, {edge.a, edge.b}, bits});
        for (std::size_t port = 0; port < puzzle.outputs.size(); ++port) {
            const auto atom =
                std::find_if(state.atoms.begin(), state.atoms.end(), [&](const auto& entry) {
                    return entry.second.type == puzzle.outputs[port].atoms.front().type;
                });
            if (atom == state.atoms.end()) throw IR::Error("whole feed receiver identity mismatch");
            material.AppendFromTail(puzzle, state, {IR::Kind::Output, port, {atom->first}});
        }
        IR::RequireComplete(state, std::vector<std::size_t>(puzzle.outputs.size(), 1));
        const auto deadline =
            std::min(Deadline(), std::chrono::steady_clock::now() + std::chrono::seconds(3));
        std::string dockingDetail;
        auto machine = CompileAcceptedWholeFeedDocking(
            puzzle, material, [&](const auto& candidate) { return acceptStage(stage, candidate); },
            [&](const auto& detail) { dockingDetail += detail + '\n'; }, deadline,
            [&] {
                if (constructionAttempts >= MaxPortfolioConstructions ||
                    std::chrono::steady_clock::now() >= deadline ||
                    (sink &&
                     (submissions >= MaxCandidateSubmissions || acceptedCount >= maxAccepted)))
                    return false;
                ++constructionAttempts;
                GetLoggerOrFallback("solver")->info(
                    "Assembly branch={} event=construction count={} limit={}", stage,
                    constructionAttempts, MaxPortfolioConstructions);
                return true;
            });
        if (machine && keep(acceptedSolution(std::move(*machine)))) return *firstAccepted;
        if (!machine) planningFailure(stage, dockingDetail);
    }
    if (puzzle.productionInfo) {
        if (auto result = attemptAccepted("production-distributed-purification",
                                          CompileAcceptedProductionDistributedPurification))
            if (keep(*result)) return *firstAccepted;
        if (auto result = attemptAccepted("production-petal-assembly",
                                          CompileAcceptedProductionPetalAssembly))
            if (keep(*result)) return *firstAccepted;
        if (auto result =
                attemptAccepted("production-direct-feed", CompileAcceptedProductionDirectFeed))
            if (keep(*result)) return *firstAccepted;
        if (auto result = attemptAccepted("production-ring-processing",
                                          CompileAcceptedProductionRingProcessing))
            if (keep(*result)) return *firstAccepted;
        if (auto result =
                attemptAccepted("production-finite-stock", CompileAcceptedProductionFiniteStock))
            if (keep(*result)) return *firstAccepted;
        if (auto result =
                attemptAccepted("production-room-assembly", CompileAcceptedProductionAssembly))
            if (keep(*result)) return *firstAccepted;
        if (auto result =
                attempt("production-molecular", [&] { return CompileProductionMolecular(puzzle); }))
            if (keep(*result)) return *firstAccepted;
        std::exception_ptr coproductAcceptanceError;
        try {
            auto result = CompileAcceptedProductionCoproducts(puzzle, [&](const auto& candidate) {
                try {
                    return acceptStage("production-coproducts", candidate);
                } catch (...) {
                    coproductAcceptanceError = std::current_exception();
                    throw;
                }
            });
            if (result) {
                auto measured = acceptedSolution(std::move(*result));
                if (keep(measured)) return *firstAccepted;
            }
            planningFailure("production-coproducts",
                            "bounded shared-feed destination plans exhausted or rejected");
        } catch (const std::invalid_argument& error) {
            if (coproductAcceptanceError) std::rethrow_exception(coproductAcceptanceError);
            planningFailure("production-coproducts", error.what());
        }
        auto productionMaterialFailure = [&](const char* stage, const SupplyFailure& failure,
                                             std::exception_ptr& infrastructureError) {
            rejections.push_back(std::string(stage) + ": " + failure.detail);
            if (feedback) {
                try {
                    feedback({AssemblyFailure::Category::Planning, stage, failure.detail,
                              std::nullopt, failure});
                } catch (...) {
                    infrastructureError = std::current_exception();
                    throw;
                }
            }
        };
        std::exception_ptr dispersionAcceptanceError;
        try {
            auto result = CompileAcceptedProductionDispersion(
                puzzle,
                [&](const auto& candidate) {
                    try {
                        return acceptStage("production-joint-dispersion", candidate);
                    } catch (...) {
                        dispersionAcceptanceError = std::current_exception();
                        throw;
                    }
                },
                [&](const SupplyFailure& failure) {
                    productionMaterialFailure("production-joint-dispersion", failure,
                                              dispersionAcceptanceError);
                });
            if (result) {
                auto measured = acceptedSolution(std::move(*result));
                if (keep(measured)) return *firstAccepted;
            }
        } catch (const std::invalid_argument& error) {
            if (dispersionAcceptanceError) std::rethrow_exception(dispersionAcceptanceError);
            planningFailure("production-joint-dispersion", error.what());
        }
        std::exception_ptr duplicationAcceptanceError;
        try {
            auto result = CompileAcceptedProductionDuplication(
                puzzle,
                [&](const auto& candidate) {
                    try {
                        return acceptStage("production-joint-catalyst", candidate);
                    } catch (...) {
                        duplicationAcceptanceError = std::current_exception();
                        throw;
                    }
                },
                [&](const SupplyFailure& failure) {
                    productionMaterialFailure("production-joint-catalyst", failure,
                                              duplicationAcceptanceError);
                });
            if (result) {
                auto measured = acceptedSolution(std::move(*result));
                if (keep(measured)) return *firstAccepted;
            }
        } catch (const std::invalid_argument& error) {
            if (duplicationAcceptanceError) std::rethrow_exception(duplicationAcceptanceError);
            planningFailure("production-joint-catalyst", error.what());
        }
        std::exception_ptr streamAcceptanceError;
        bool streamsExhausted = false;
        try {
            auto result = CompileAcceptedProductionStreams(puzzle, [&](const auto& candidate) {
                try {
                    return acceptStage("production-streams", candidate);
                } catch (...) {
                    streamAcceptanceError = std::current_exception();
                    throw;
                }
            });
            if (result) {
                auto measured = acceptedSolution(std::move(*result));
                if (keep(measured)) return *firstAccepted;
            }
            streamsExhausted = true;
        } catch (const std::invalid_argument& error) {
            if (streamAcceptanceError) std::rethrow_exception(streamAcceptanceError);
            planningFailure("production-streams", error.what());
        }
        if (streamsExhausted)
            planningFailure("production-streams",
                            "bounded joint conduit assignments exhausted or rejected");
        std::exception_ptr transportAcceptanceError;
        bool transportExhausted = false;
        try {
            auto result = CompileAcceptedProductionTransport(puzzle, [&](const auto& candidate) {
                try {
                    return acceptStage("restricted-production", candidate);
                } catch (...) {
                    transportAcceptanceError = std::current_exception();
                    throw;
                }
            });
            if (result) {
                auto measured = acceptedSolution(std::move(*result));
                if (keep(measured)) return *firstAccepted;
            }
            transportExhausted = true;
        } catch (const std::invalid_argument& error) {
            if (transportAcceptanceError) std::rethrow_exception(transportAcceptanceError);
            planningFailure("restricted-production", error.what());
        }
        if (transportExhausted)
            planningFailure("restricted-production",
                            "bounded transport poses exhausted or rejected");
        std::exception_ptr routeAcceptanceError;
        bool routeExhausted = false;
        try {
            auto result = CompileAcceptedProductionRoute(puzzle, [&](const auto& candidate) {
                try {
                    return acceptStage("production-route", candidate);
                } catch (...) {
                    routeAcceptanceError = std::current_exception();
                    throw;
                }
            });
            if (result) {
                auto measured = acceptedSolution(std::move(*result));
                if (keep(measured)) return *firstAccepted;
            }
            routeExhausted = true;
        } catch (const std::invalid_argument& error) {
            if (routeAcceptanceError) std::rethrow_exception(routeAcceptanceError);
            planningFailure("production-route", error.what());
        }
        if (routeExhausted)
            planningFailure("production-route", "bounded relay poses exhausted or rejected");
        if (auto result =
                attempt("production-chemistry", [&] { return CompileProductionChemistry(puzzle); }))
            if (keep(*result)) return *firstAccepted;
        std::exception_ptr chemistryAcceptanceError;
        bool chemistryExhausted = false;
        try {
            auto result =
                CompileAcceptedProductionChemistryRoute(puzzle, [&](const auto& candidate) {
                    try {
                        return acceptStage("production-chemistry-route", candidate);
                    } catch (...) {
                        chemistryAcceptanceError = std::current_exception();
                        throw;
                    }
                });
            if (result) {
                auto measured = acceptedSolution(std::move(*result));
                if (keep(measured)) return *firstAccepted;
            }
            chemistryExhausted = true;
        } catch (const std::invalid_argument& error) {
            if (chemistryAcceptanceError) std::rethrow_exception(chemistryAcceptanceError);
            planningFailure("production-chemistry-route", error.what());
        }
        if (chemistryExhausted)
            planningFailure("production-chemistry-route",
                            "shared geometry budget exhausted or all candidates rejected");
        std::exception_ptr acceptanceError;
        bool inventoryExhausted = false;
        try {
            auto result = inventory(acceptanceError);
            if (result)
                if (keep(*result)) return *firstAccepted;
            inventoryExhausted = true;
        } catch (const std::invalid_argument& error) {
            if (acceptanceError) std::rethrow_exception(acceptanceError);
            planningFailure("production-automatic-inventory", error.what());
        }
        if (inventoryExhausted)
            planningFailure("production-automatic-inventory",
                            "bounded chamber layouts exhausted or rejected");
        std::string detail = "assembly rejected: special-rules:";
        for (const auto& rejection : rejections)
            detail += " " + rejection + ";";
        if (firstAccepted) return *firstAccepted;
        throw std::invalid_argument(detail);
    }
    try {
        ValidateSpecialRules(puzzle);
    } catch (const std::invalid_argument& error) {
        planningFailure("special-rules", error.what());
        throw std::invalid_argument(std::string("assembly rejected: special-rules: ") +
                                    error.what());
    }
    // Direct component composition runs before every family emitter: the whole
    // machine follows from the product frame plus the reagent source, so it is far
    // cheaper than a row/bond family on its domain. `OPUSOPTIMA_COMPOSE=off`
    // disables it for A/B measurement; every other value keeps the default.
    const char* composeMode = std::getenv("OPUSOPTIMA_COMPOSE");
    const bool composeEnabled = !composeMode || std::string_view(composeMode) != "off";
    std::optional<io::SolutionData> composedSolution;
    // Component-graph composition: one transfer arm per product atom plus one bonding
    // station per bond, all placed from the product frame. It is verified first because
    // it is cheap and wins the cycle slot by a wide margin on its domain, but it is only
    // *published* when no family emitter produces a machine: publishing it eagerly
    // displaced cheaper family machines (measured: INFINITE THROUGHPUT cost 495->780).
    const bool composeEligible = composeEnabled && SupportsComposedAssembly(puzzle) &&
                                 puzzle.outputs.front().atoms.size() == 1 &&
                                 puzzle.outputs.front().bonds.empty();
    if (composeEligible) {
        if (auto result =
                attempt("compose-assembly", [&] { return ComposeAssemblyMachine(puzzle); }))
            composedSolution = std::move(*result);
    }
    if (auto result = attempt("intact-feed", [&] { return CompileIntactFeed(puzzle); }))
        if (keep(*result)) return *firstAccepted;
    std::exception_ptr supplyAcceptanceError;
    bool supplyExhausted = false;
    try {
        auto result = inventory(supplyAcceptanceError);
        if (result)
            if (keep(*result)) return *firstAccepted;
        supplyExhausted = true;
    } catch (const std::invalid_argument& error) {
        if (supplyAcceptanceError) std::rethrow_exception(supplyAcceptanceError);
        planningFailure("automatic-supply-inventory", error.what());
    }
    if (supplyExhausted)
        planningFailure("automatic-supply-inventory", "bounded layouts exhausted or rejected");
    constexpr const char* metalStage = "closed-wide-metal-inventory";
    if (verify && !firstAccepted && SupportsClosedWideMetalInventory(puzzle) &&
        constructionAttempts < MaxPortfolioConstructions &&
        std::chrono::steady_clock::now() < Deadline() &&
        (!sink || (submissions < MaxCandidateSubmissions && acceptedCount < maxAccepted))) {
        ++constructionAttempts;
        GetLoggerOrFallback("solver")->info(
            "Assembly branch={} event=construction count={} limit={}", metalStage,
            constructionAttempts, MaxPortfolioConstructions);
        if (auto result = attemptAccepted(metalStage, [&](const auto& p, const auto& gate) {
                return CompileAcceptedClosedWideMetalInventory(p, gate, Deadline());
            }))
            if (keep(*result)) return *firstAccepted;
    }
    std::exception_ptr fixedAcceptanceError;
    try {
        auto result =
            CompileAcceptedFixedArmInventory(puzzle, [&](const io::SolutionData& candidate) {
                try {
                    return acceptStage("fixed-arm-inventory", candidate);
                } catch (...) {
                    fixedAcceptanceError = std::current_exception();
                    throw;
                }
            });
        if (result) {
            auto measured = acceptedSolution(std::move(*result));
            if (keep(measured)) return *firstAccepted;
        }
    } catch (const std::invalid_argument& error) {
        if (fixedAcceptanceError) std::rethrow_exception(fixedAcceptanceError);
        planningFailure("fixed-arm-inventory", error.what());
    }
    std::exception_ptr fragmentAcceptanceError;
    bool fragmentExhausted = false;
    try {
        auto result = CompileAcceptedFragmentFeed(puzzle, [&](const io::SolutionData& candidate) {
            try {
                return acceptStage("fragment-feed-ir", candidate);
            } catch (...) {
                fragmentAcceptanceError = std::current_exception();
                throw;
            }
        });
        if (result) {
            auto measured = acceptedSolution(std::move(*result));
            if (keep(measured)) return *firstAccepted;
        }
        fragmentExhausted = true;
    } catch (const std::invalid_argument& error) {
        if (fragmentAcceptanceError) std::rethrow_exception(fragmentAcceptanceError);
        planningFailure("fragment-feed-ir", error.what());
    }
    if (fragmentExhausted)
        planningFailure("fragment-feed-ir", "all candidates exhausted or rejected");
    if (!puzzle.productionInfo) {
        auto simple = puzzle;
        simple.inputs.clear();
        std::vector<std::size_t> originalInputs;
        for (std::size_t i = 0; i < puzzle.inputs.size(); ++i) {
            if (puzzle.inputs[i].atoms.size() != 1 || !puzzle.inputs[i].bonds.empty()) continue;
            simple.inputs.push_back(puzzle.inputs[i]);
            originalInputs.push_back(i);
        }
        if (!simple.inputs.empty() && simple.inputs.size() < puzzle.inputs.size()) {
            std::exception_ptr acceptanceError;
            try {
                auto result = SolveAccepted(simple, [&](const io::SolutionData& candidate) {
                    auto mapped = candidate;
                    for (auto& object : mapped.objects)
                        if (object.typeName == "input")
                            object.bodyData.at(2) = originalInputs.at(object.bodyData.at(2));
                    try {
                        return acceptStage("singleton-input-subset", mapped);
                    } catch (...) {
                        acceptanceError = std::current_exception();
                        throw;
                    }
                });
                for (auto& object : result.objects)
                    if (object.typeName == "input")
                        object.bodyData.at(2) = originalInputs.at(object.bodyData.at(2));
                auto measured = acceptedSolution(std::move(result));
                if (keep(measured)) return *firstAccepted;
            } catch (const std::invalid_argument& error) {
                if (acceptanceError) std::rethrow_exception(acceptanceError);
                planningFailure("singleton-input-subset", error.what());
            }
        }
    }
    const bool ordinaryFirst =
        puzzle.outputs.size() == 1 && !HasRepeatingOutput(puzzle.outputs[0]) &&
        std::all_of(puzzle.outputs[0].atoms.begin(), puzzle.outputs[0].atoms.end(),
                    [&](const auto& target) {
                        return std::any_of(
                            puzzle.inputs.begin(), puzzle.inputs.end(), [&](const auto& input) {
                                return std::any_of(input.atoms.begin(), input.atoms.end(),
                                                   [&](const auto& source) {
                                                       return ChemicalPlanner::CanSupply(
                                                           source.type, target.type,
                                                           puzzle.permissionBits);
                                                   });
                            });
                    });
    if (ordinaryFirst) {
        if (auto result = attempt("ordinary-assembly",
                                  [&] { return Compile(puzzle, AssemblyPlanner::Plan(puzzle)); }))
            if (keep(*result)) return *firstAccepted;
    }
    if (auto result = attempt("trackless-pair-chemistry",
                              [&] { return CompileTracklessPairChemistry(puzzle); }))
        if (keep(*result)) return *firstAccepted;
    if (auto result =
            attempt("molecular-metal-split", [&] { return CompileMolecularMetalSplit(puzzle); }))
        if (keep(*result)) return *firstAccepted;
    if (auto result =
            attempt("metal-purification", [&] { return CompileMetalPurification(puzzle); }))
        if (keep(*result)) return *firstAccepted;
    if (auto result = attempt("shared-purification-assembly",
                              [&] { return CompileSharedPurificationAssembly(puzzle); }))
        if (keep(*result)) return *firstAccepted;
    if (auto result =
            attempt("purification-assembly", [&] { return CompilePurificationAssembly(puzzle); }))
        if (keep(*result)) return *firstAccepted;
    if (wheelSecondSourceEligible && !wheelSecondSourceAttempted &&
        (!sink || (submissions < MaxCandidateSubmissions && acceptedCount < maxAccepted)) &&
        std::chrono::steady_clock::now() < Deadline()) {
        if (auto result = tryWheelSecondSource())
            if (keep(*result)) return *firstAccepted;
    }
    const bool wheelCalcifiedSalt =
        std::all_of(
            puzzle.inputs.begin(), puzzle.inputs.end(),
            [](const auto& input) { return input.atoms.size() == 1 && input.bonds.empty(); }) &&
        std::any_of(puzzle.outputs.begin(), puzzle.outputs.end(),
                    [](const auto& output) {
                        return std::any_of(
                            output.atoms.begin(), output.atoms.end(),
                            [](const auto& atom) { return atom.type == core::AtomType::Salt; });
                    }) &&
        std::none_of(puzzle.inputs.begin(), puzzle.inputs.end(), [](const auto& input) {
            return input.atoms.size() == 1 && input.bonds.empty() &&
                   input.atoms[0].type == core::AtomType::Salt;
        });
    const char* wheelAssemblyStage =
        wheelCalcifiedSalt ? "wheel-assembly-calcified-salt" : "wheel-assembly";
    if (!wheelCalcifiedSalt || verify) {
        if (auto result = attempt(wheelAssemblyStage, [&] { return CompileWheelAssembly(puzzle); }))
            if (keep(*result)) return *firstAccepted;
    }
    if (auto result = attempt("life-death", [&] { return CompileLifeDeath(puzzle); }))
        if (keep(*result)) return *firstAccepted;
    if (auto result = attempt("triplex", [&] { return CompileTriplex(puzzle); }))
        if (keep(*result)) return *firstAccepted;
    if (auto result = attempt("unification", [&] { return CompileUnification(puzzle); }))
        if (keep(*result)) return *firstAccepted;
    if (auto result =
            attempt("balanced-dispersion", [&] { return CompileBalancedDispersion(puzzle); }))
        if (keep(*result)) return *firstAccepted;
    if (auto result = attempt("dispersion", [&] { return CompileDispersion(puzzle); }))
        if (keep(*result)) return *firstAccepted;
    if (puzzle.outputs.size() == 1 && HasRepeatingOutput(puzzle.outputs[0])) {
        if (auto result =
                attempt("repeating-output", [&] { return CompileRepeatingOutput(puzzle); }))
            if (keep(*result)) return *firstAccepted;
    } else if (puzzle.outputs.size() <= 1) {
        if (!ordinaryFirst) {
            if (auto result = attempt("ordinary-assembly", [&] {
                    return Compile(puzzle, AssemblyPlanner::Plan(puzzle));
                }))
                if (keep(*result)) return *firstAccepted;
        }
    } else {
        if (auto result = attempt("shared-atomic-outputs",
                                  [&] { return CompileSharedAtomicOutputs(puzzle); }))
            if (keep(*result)) return *firstAccepted;
        if (auto result = attempt("shared-ordinary-outputs",
                                  [&] { return CompileSharedOrdinaryOutputs(puzzle); }))
            if (keep(*result)) return *firstAccepted;
        if (auto result = attempt("independent-outputs", [&]() -> io::SolutionData {
                if (puzzle.productionInfo) Reject("multiple production outputs are unsupported");
                io::SolutionData result;
                result.puzzleName = puzzle.name;
                result.solutionName = "Independent output assembly";
                int nextQ = 0, armNumber = 0;
                for (std::size_t output = 0; output < puzzle.outputs.size(); ++output) {
                    auto single = puzzle;
                    single.outputs = {puzzle.outputs[output]};
                    auto machine = [&] {
                        try {
                            return Solve(single);
                        } catch (const std::invalid_argument& error) {
                            throw std::invalid_argument("output[" + std::to_string(output) +
                                                        "]: " + error.what());
                        }
                    }();
                    int low = 0, high = 0;
                    auto include = [&](core::HexCoord cell, int radius = 0) {
                        low = std::min(low, cell.q - radius);
                        high = std::max(high, cell.q + radius);
                    };
                    for (const auto& obj : machine.objects) {
                        include(obj.position, 8);
                        if (obj.typeName == "track") {
                            const auto count = obj.bodyData.at(4);
                            for (int i = 0; i < count; ++i)
                                include(core::Add(obj.position, {obj.bodyData.at(5 + 2 * i),
                                                                 obj.bodyData.at(6 + 2 * i)}),
                                        8);
                        } else if (obj.typeName == "input" || obj.typeName == "out-std" ||
                                   obj.typeName == "out-rep") {
                            const auto molecule =
                                obj.typeName == "input"
                                    ? single.inputs.at(obj.bodyData.at(2))
                                    : (HasRepeatingOutput(single.outputs[0])
                                           ? ExpandRepeatingOutput(single.outputs[0])
                                           : single.outputs[0]);
                            for (const auto& atom : molecule.atoms) {
                                auto cell = atom.position;
                                for (int i = 0; i < obj.bodyData.at(1); ++i)
                                    cell = core::RotateClockwise(cell);
                                include(core::Add(obj.position, cell), 8);
                            }
                        }
                    }
                    const int offset = nextQ - low;
                    for (auto& obj : machine.objects) {
                        obj.position.q += offset;
                        if (obj.typeName == "out-std" || obj.typeName == "out-rep")
                            obj.bodyData.at(2) = static_cast<int>(output);
                        if (obj.typeName == "piston" || obj.typeName.starts_with("arm"))
                            obj.bodyData.at(4) = armNumber++;
                        result.objects.push_back(std::move(obj));
                    }
                    nextQ = high + offset + 32;
                }
                return result;
            }))
            if (keep(*result)) return *firstAccepted;
    }
    if (auto result = attempt("finite-molecular-purification",
                              [&] { return CompilePurificationAssembly(puzzle, true); }))
        if (keep(*result)) return *firstAccepted;
    std::exception_ptr fixedJoinAcceptanceError;
    try {
        auto result = CompileAcceptedFixedArmJoin(puzzle, [&](const io::SolutionData& candidate) {
            try {
                return acceptStage("fixed-arm-join", candidate);
            } catch (...) {
                fixedJoinAcceptanceError = std::current_exception();
                throw;
            }
        });
        if (result) {
            auto measured = acceptedSolution(std::move(*result));
            if (keep(measured)) return *firstAccepted;
        }
    } catch (const std::invalid_argument& error) {
        if (fixedJoinAcceptanceError) std::rethrow_exception(fixedJoinAcceptanceError);
        planningFailure("fixed-arm-join", error.what());
    }
    const char* fallbackStaticEmbeddingFlag = std::getenv("OPUSOPTIMA_STATIC_EMBEDDING");
    if (verify && !firstAccepted && !staticEmbeddingAttempted &&
        constructionAttempts < MaxPortfolioConstructions &&
        (!fallbackStaticEmbeddingFlag || std::string_view(fallbackStaticEmbeddingFlag) != "off") &&
        std::chrono::steady_clock::now() < Deadline() &&
        (!sink || (submissions < MaxCandidateSubmissions && acceptedCount < maxAccepted))) {
        staticEmbeddingAttempted = true;
        ++constructionAttempts;
        GetLoggerOrFallback("solver")->info(
            "Assembly branch=fallback-static-embedding event=construction count={} limit={}",
            constructionAttempts, MaxPortfolioConstructions);
        if (auto result = attempt("fallback-static-embedding", [&] {
                auto station = CompileStaticEmbedding(puzzle, Deadline());
                if (std::chrono::steady_clock::now() >= Deadline())
                    throw std::invalid_argument("static embedding Unsupported: deadline");
                return station;
            }))
            if (keep(*result)) return *firstAccepted;
    }
    // One allowance for all material histories and physical variants, not per candidate.
    MaterialPlanningLimits materialLimits;
    const auto* productPortAccess = std::getenv("OPUSOPTIMA_PRODUCT_PORT_ACCESS");
    const auto* closedLifeFlag = std::getenv("OPUSOPTIMA_CLOSED_CARDINAL_LIFE");
    const bool closedLife = (!closedLifeFlag || std::string_view(closedLifeFlag) == "on") &&
                            SupportsClosedCardinalLife(puzzle);
    // With the precursor experiment disabled, cardinal-only sources cannot seed
    // life in legacy material search: Animismus requires Salt, and calcification only
    // supplies a Salt target. Earlier compiler families keep their first chances.
    const auto loweringPolicy = closedLife ? SupplyMaterialLoweringPolicy::ClosedCardinalLife
                                : productPortAccess && std::string_view(productPortAccess) == "1" &&
                                        SupportsOrdinarySourceCutLowering(puzzle)
                                    ? SupplyMaterialLoweringPolicy::CutOrdinarySources
                                    : SupplyMaterialLoweringPolicy::PreserveCompatible;
    materialLimits.deadline =
        std::min(Deadline(), std::chrono::steady_clock::now() + std::chrono::seconds(3));
    std::optional<io::SolutionData> materialMachine;
    const auto material = SearchMaterialExecutionWithPolicy(
        puzzle, loweringPolicy, materialLimits, [&](const auto& plan) {
            const bool wheel = SupportsClosedSingletonWheelPlanning(puzzle) &&
                               std::any_of(plan.material.steps.begin(), plan.material.steps.end(),
                                           [](const auto& step) {
                                               return step.operation.wheelCatalyst.has_value();
                                           });
            const bool ordinaryPortAccess =
                loweringPolicy != SupplyMaterialLoweringPolicy::PreserveCompatible;
            if ((wheel || ordinaryPortAccess) && !verify) return false;
            materialMachine = CompileAcceptedSerialArmExecutionWithPolicy(
                puzzle, plan, loweringPolicy,
                [&](const auto& candidate) {
                    const bool precursor =
                        std::any_of(plan.tasks.begin(), plan.tasks.end(),
                                    [](const auto& task) {
                                        return task.kind == ExecutionIR::Kind::Calcify;
                                    }) &&
                        std::any_of(plan.tasks.begin(), plan.tasks.end(), [](const auto& task) {
                            return task.kind == ExecutionIR::Kind::Animismus;
                        });
                    const bool accepted =
                        acceptStage(ordinaryPortAccess ? "shared-plan-ordinary-port-access"
                                    : wheel            ? "shared-plan-wheel"
                                    : precursor        ? "shared-plan-calcify-animismus"
                                                       : "shared-plan-serial-arms",
                                    candidate);
                    if (accepted && !firstAccepted) {
                        firstAccepted = candidate;
                        if (acceptedReport) firstAccepted->metrics = acceptedReport->metrics;
                    }
                    return accepted;
                },
                {}, materialLimits.deadline,
                [&](SerialArmFailure failure, const std::string& detail) {
                    rejections.push_back("shared-plan-serial-arms: " + detail);
                    const auto category =
                        failure == SerialArmFailure::Unsupported
                            ? AssemblyFailure::Category::Unsupported
                        : failure == SerialArmFailure::BudgetExhausted
                            ? AssemblyFailure::Category::BudgetExhausted
                        : failure == SerialArmFailure::LayoutCollision
                            ? AssemblyFailure::Category::LayoutCollision
                            : AssemblyFailure::Category::MaterialMechanicalFailure;
                    if (feedback)
                        feedback({category, "shared-plan-serial-arms", detail, std::nullopt,
                                  std::nullopt});
                },
                sink ? std::function<void(const io::SolutionData&,
                                          const io::SolutionData&)>{[&](const auto& parent,
                                                                        const auto& child) {
                    const auto parentReport = acceptedReport;
                    auto measuredParent = parent;
                    if (parentReport) measuredParent.metrics = parentReport->metrics;
                    if (!firstAccepted) firstAccepted = measuredParent;
                    if (submissions < MaxCandidateSubmissions && acceptedCount < maxAccepted &&
                        std::chrono::steady_clock::now() < materialLimits.deadline) {
                        try {
                            auto canonical = parent;
                            canonical.solutionName.clear();
                            canonical.metrics = {};
                            const auto strategy =
                                (ordinaryPortAccess ? "shared-plan-ordinary-port-access:parent="
                                 : wheel            ? "shared-plan-wheel:parent="
                                 : child.solutionName == "Shared-plan retained ordinary fragments"
                                     ? "shared-plan-retained-fragments:parent="
                                     : "shared-plan-scalar-relay-pipeline:parent=") +
                                io::CandidateSha256(io::BinaryWriter::WriteSolution(canonical));
                            verifyAt(strategy.c_str(), child);
                        } catch (const std::runtime_error&) {
                            // Optional scheduling never invalidates the already gated parent.
                        }
                    }
                    acceptedReport = parentReport;
                }}
                     : std::function<void(const io::SolutionData&, const io::SolutionData&)>{});
            return materialMachine.has_value();
        });
    if (materialMachine) {
        auto measured = acceptedSolution(std::move(*materialMachine));
        if (keep(measured)) return *firstAccepted;
    }
    const auto materialDetail = material.detail + "; expanded=" + std::to_string(material.states) +
                                "; candidate-histories=" + std::to_string(material.candidates);
    rejections.push_back("mechanism-independent-material: " + materialDetail);
    if (feedback)
        feedback({material.status == MaterialPlanningStatus::Unsupported
                      ? AssemblyFailure::Category::Unsupported
                  : material.status == MaterialPlanningStatus::BudgetExhausted ||
                          material.status == MaterialPlanningStatus::Exhausted
                      ? AssemblyFailure::Category::BudgetExhausted
                      : AssemblyFailure::Category::SearchExhausted,
                  "mechanism-independent-material", materialDetail, std::nullopt, std::nullopt});
    std::string message = "assembly strategies exhausted";
    for (const auto& rejection : rejections)
        message += "\n  " + rejection;
    if (firstAccepted) return *firstAccepted;
    // The composed machine is the fallback of last resort: family emitters publish
    // first because they trade the cycle slot for much cheaper machines on large
    // products (measured: INFINITE THROUGHPUT 487/1040/144 vs 604/495/108).
    if (composedSolution) return *composedSolution;

    throw std::invalid_argument(message);
}
std::int32_t AssemblySolver::EstimateCycles(const io::PuzzleData& puzzle) {
    return Solve(puzzle).metrics.cycles;
}
namespace {
const bool registered = [] {
    BaseSolver::Register("AssemblySolver", [] { return std::make_unique<AssemblySolver>(); });
    BaseSolver::Register("assembly", [] { return std::make_unique<AssemblySolver>(); });
    return true;
}();
} // namespace
} // namespace opusoptima::solvers
