#include "MoleculePipeline.hpp"

#include "LogManager.hpp"
#include "SolutionPostProcess.hpp"
#include "SwingKernel.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <vector>

namespace opusoptima::solvers::architecture {
namespace {

using core::AtomType;

constexpr std::size_t kMaxMoleculeAtoms = 16;
// Arm, bonder, unbonder and the tape instructions the swing uses.
constexpr std::uint64_t kMoleculePermissions = (1ULL << 0) | (1ULL << 8) | (1ULL << 9) |
                                              (1ULL << 22) | (1ULL << 23) | (1ULL << 24) |
                                              (1ULL << 25);
// One cycle for the molecule to land and its bonds to be cut before any arm moves.
constexpr int kSettleCycles = 1;

[[nodiscard]] core::HexCoord Rotate(core::HexCoord cell, int turns) {
    for (int i = 0; i < ((turns % 6) + 6) % 6; ++i) cell = core::RotateClockwise(cell);
    return cell;
}

/// One way an `arm1` carries an atom from `release` to `target`:
/// `target - release == length * (step(drop) - step(grab))`.
struct SwingSolution {
    int length{};
    int grab{};
    int drop{};
};

[[nodiscard]] std::vector<SwingSolution> SolutionsFor(core::HexCoord delta) {
    std::vector<SwingSolution> solutions;
    for (int length = 1; length <= layout::kMaxArmSize; ++length)
        for (int grab = 0; grab < 6; ++grab)
            for (int drop = 0; drop < 6; ++drop) {
                if (grab == drop) continue;
                const auto step = core::Subtract(layout::SwingStep(drop), layout::SwingStep(grab));
                if (step.q * length == delta.q && step.r * length == delta.r)
                    solutions.push_back({length, grab, drop});
            }
    return solutions;
}

/// Every distinct offset an `arm1` can cover; the landing search walks these.
[[nodiscard]] const std::vector<core::HexCoord>& AllSwingOffsets() {
    static const std::vector<core::HexCoord> offsets = [] {
        std::vector<core::HexCoord> all;
        for (int length = 1; length <= layout::kMaxArmSize; ++length)
            for (int grab = 0; grab < 6; ++grab)
                for (int drop = 0; drop < 6; ++drop) {
                    if (grab == drop) continue;
                    const auto step =
                        core::Subtract(layout::SwingStep(drop), layout::SwingStep(grab));
                    const core::HexCoord delta{step.q * length, step.r * length};
                    if (std::find(all.begin(), all.end(), delta) == all.end()) all.push_back(delta);
                }
        // Prefer the smallest displacement: it keeps the molecule close to the
        // product frame and leaves the most room for the arms.
        std::stable_sort(all.begin(), all.end(), [](const auto& left, const auto& right) {
            return core::Distance(left, core::HexCoord{}) < core::Distance(right, core::HexCoord{});
        });
        return all;
    }();
    return offsets;
}

[[nodiscard]] std::vector<io::ArmInstruction> SwingTape(int grab, int drop, int period, int start) {
    const int rotation = layout::SwingRotation(grab, drop);
    const int turns = rotation > 0 ? rotation : -rotation;
    std::vector<io::ArmInstruction> tape;
    int at = 0;
    while (at < start) tape.push_back({'O', at++});
    tape.push_back({'G', at++});
    for (int step = 0; step < turns; ++step) tape.push_back({rotation > 0 ? 'r' : 'R', at++});
    tape.push_back({'g', at++});
    for (int step = 0; step < turns; ++step) tape.push_back({rotation > 0 ? 'R' : 'r', at++});
    while (at < period - 1) tape.push_back({'O', at++});
    tape.push_back({'X', at++});
    return tape;
}

[[nodiscard]] io::SolutionObject MakeObject(std::string type, core::HexCoord position,
                                            std::vector<int> body,
                                            std::vector<io::ArmInstruction> tape = {}) {
    io::SolutionObject object;
    object.typeName = std::move(type);
    object.position = position;
    object.bodyData = std::move(body);
    object.instructions = std::move(tape);
    return object;
}

/// Single-atom reagent port carrying `element`, or -1.
[[nodiscard]] int PortFor(const io::PuzzleData& puzzle, AtomType element) {
    for (std::size_t i = 0; i < puzzle.inputs.size(); ++i) {
        const auto& molecule = puzzle.inputs[i];
        if (molecule.atoms.size() == 1 && molecule.bonds.empty() &&
            molecule.atoms.front().type == element)
            return static_cast<int>(i);
    }
    return -1;
}

struct Demand {
    std::size_t atom{};
    AtomType element{AtomType::Salt};
    int port{-1};
    /// Set when the atom comes out of the disassembled molecule.
    bool fromMolecule{};
    std::size_t moleculeAtom{};
    core::HexCoord release{};
    SwingSolution swing{};
};

[[nodiscard]] std::size_t CountType(const io::Molecule& molecule, AtomType element) {
    return static_cast<std::size_t>(
        std::count_if(molecule.atoms.begin(), molecule.atoms.end(),
                      [&](const auto& atom) { return atom.type == element; }));
}

/// The molecular feed is used **whole**: every one of its atoms must be consumed by
/// some product atom. A partially used molecule leaves its spare atoms parked on the
/// board and the next period lands another one, so the machine never converges.
[[nodiscard]] std::optional<std::size_t> SelectMolecule(const io::PuzzleData& puzzle,
                                                        const std::vector<Demand>& demands) {
    std::optional<std::size_t> best;
    std::size_t bestScore = 0;
    for (std::size_t index = 0; index < puzzle.inputs.size(); ++index) {
        const auto& molecule = puzzle.inputs[index];
        if (molecule.atoms.size() < 2 || molecule.atoms.size() > kMaxMoleculeAtoms) continue;
        bool usable = true;
        std::size_t score = 0;
        for (const auto& atom : molecule.atoms) {
            std::size_t needed = 0;
            for (const auto& demand : demands)
                if (demand.element == atom.type) ++needed;
            const auto supplied = CountType(molecule, atom.type);
            if (supplied > needed) usable = false;
            score += std::min(supplied, needed);
        }
        if (!usable) continue;
        if (score > bestScore) {
            bestScore = score;
            best = index;
        }
    }
    return best;
}

/// Assigns every atom of the chosen molecule to a product atom of the same type,
/// preferring product atoms that no single-atom port can supply.
[[nodiscard]] bool AssignMolecule(const io::PuzzleData& puzzle, std::size_t moleculeInput,
                                  std::vector<Demand>& demands) {
    const auto& molecule = puzzle.inputs[moleculeInput];
    std::vector<bool> used(molecule.atoms.size(), false);
    std::vector<std::size_t> order(demands.size());
    for (std::size_t i = 0; i < order.size(); ++i) order[i] = i;
    // Port-less atoms first: a molecule is never spent on an atom a cheap port
    // already covers while a portless atom starves.
    std::stable_sort(order.begin(), order.end(), [&](std::size_t left, std::size_t right) {
        return demands[left].port < demands[right].port;
    });
    for (const auto index : order) {
        auto& demand = demands[index];
        if (demand.port >= 0) continue;
        for (std::size_t atom = 0; atom < molecule.atoms.size(); ++atom) {
            if (used[atom] || molecule.atoms[atom].type != demand.element) continue;
            used[atom] = true;
            demand.fromMolecule = true;
            demand.moleculeAtom = atom;
            break;
        }
        if (!demand.fromMolecule) return false;
    }
    // Every molecule atom must be consumed; leftovers would pile up forever.
    for (std::size_t atom = 0; atom < molecule.atoms.size(); ++atom) {
        if (used[atom]) continue;
        bool matched = false;
        for (auto& demand : demands) {
            if (demand.fromMolecule || demand.element != molecule.atoms[atom].type) continue;
            demand.fromMolecule = true;
            demand.moleculeAtom = atom;
            used[atom] = true;
            matched = true;
            break;
        }
        if (!matched) return false;
    }
    return true;
}

struct Landing {
    std::size_t input{};
    int turns{};
    core::HexCoord origin{};
};

/// Chooses the landing pose so that every displaced atom starts on a swing offset
/// from its own product cell.
[[nodiscard]] std::optional<Landing> PlanLanding(
    const io::PuzzleData& puzzle, std::size_t moleculeInput,
    const std::vector<core::HexCoord>& targets, std::vector<Demand>& demands) {
    const auto& molecule = puzzle.inputs[moleculeInput];
    std::vector<std::size_t> displaced;
    for (std::size_t index = 0; index < demands.size(); ++index)
        if (demands[index].fromMolecule) displaced.push_back(index);

    std::unordered_set<core::HexCoord, core::HexCoordHash> productCells;
    for (const auto& target : targets) productCells.insert(target);

    for (int turns = 0; turns < 6; ++turns) {
        std::vector<core::HexCoord> rotated;
        rotated.reserve(molecule.atoms.size());
        for (const auto& atom : molecule.atoms) rotated.push_back(Rotate(atom.position, turns));
        const auto anchor = displaced.front();
        const auto anchorLocal = rotated[demands[anchor].moleculeAtom];
        for (const auto& offset : AllSwingOffsets()) {
            const core::HexCoord release = core::Subtract(targets[anchor], offset);
            const core::HexCoord origin = core::Subtract(release, anchorLocal);
            bool ok = true;
            for (const auto index : displaced) {
                const auto cell = core::Add(origin, rotated[demands[index].moleculeAtom]);
                if (productCells.count(cell) != 0) {
                    ok = false;
                    break;
                }
                if (SolutionsFor(core::Subtract(targets[index], cell)).empty()) {
                    ok = false;
                    break;
                }
            }
            if (!ok) continue;
            for (const auto index : displaced) {
                auto& demand = demands[index];
                demand.release = core::Add(origin, rotated[demand.moleculeAtom]);
                const auto solutions = SolutionsFor(core::Subtract(targets[index], demand.release));
                // Shortest arm first: a radius-1 arm sweeps 7 cells and leaves room.
                demand.swing = *std::min_element(
                    solutions.begin(), solutions.end(), [](const auto& left, const auto& right) {
                        if (left.length != right.length) return left.length < right.length;
                        const int leftTurns = std::abs(layout::SwingRotation(left.grab, left.drop));
                        const int rightTurns =
                            std::abs(layout::SwingRotation(right.grab, right.drop));
                        return leftTurns < rightTurns;
                    });
            }
            return Landing{moleculeInput, turns, origin};
        }
    }
    return std::nullopt;
}

} // namespace

bool SupportsMoleculeAssembly(const io::PuzzleData& puzzle) {
    if (puzzle.productionInfo || puzzle.outputs.size() != 1) return false;
    if ((puzzle.permissionBits & kMoleculePermissions) != kMoleculePermissions) return false;
    if (puzzle.outputMultiplier <= 0) return false;
    const auto& output = puzzle.outputs.front();
    if (output.atoms.size() < 2 || output.atoms.size() > kMaxMoleculeAtoms) return false;
    for (const auto& atom : output.atoms)
        if (atom.type == core::AtomType::Repeat) return false;
    for (const auto& bond : output.bonds) {
        if (bond.bits != 1) return false;
        if (layout::SwingDirectionOf(core::Subtract(bond.to, bond.from)) < 0) return false;
    }
    bool molecular = false;
    for (const auto& molecule : puzzle.inputs)
        if (molecule.atoms.size() > 1) molecular = true;
    if (!molecular) return false;
    for (const auto& atom : output.atoms) {
        bool available = false;
        for (const auto& molecule : puzzle.inputs)
            for (const auto& source : molecule.atoms)
                if (source.type == atom.type) available = true;
        if (!available) return false;
    }
    return true;
}

io::SolutionData CompileMoleculeMachine(const io::PuzzleData& puzzle, bool postProcess,
                                        MoleculeVariant* detail) {
    if (!SupportsMoleculeAssembly(puzzle))
        throw std::invalid_argument("molecule assembly: puzzle outside the supported domain");
    const auto& output = puzzle.outputs.front();
    std::vector<core::HexCoord> targets;
    targets.reserve(output.atoms.size());
    for (const auto& atom : output.atoms) targets.push_back(atom.position);

    std::vector<Demand> demands(output.atoms.size());
    for (std::size_t index = 0; index < demands.size(); ++index) {
        demands[index].atom = index;
        demands[index].element = output.atoms[index].type;
        demands[index].port = PortFor(puzzle, demands[index].element);
    }
    const auto moleculeInput = SelectMolecule(puzzle, demands);
    if (!moleculeInput)
        throw std::invalid_argument("molecule assembly: no molecular feed can be used whole");
    if (!AssignMolecule(puzzle, *moleculeInput, demands))
        throw std::invalid_argument("molecule assembly: molecule atoms do not cover the product");
    const auto landing = PlanLanding(puzzle, *moleculeInput, targets, demands);
    if (!landing)
        throw std::invalid_argument("molecule assembly: no landing pose reaches every atom");

    // ── layout: an arm must not sweep a product cell, a parked atom or another arm.
    std::unordered_set<core::HexCoord, core::HexCoordHash> productCells;
    for (const auto& target : targets) productCells.insert(target);
    std::unordered_set<core::HexCoord, core::HexCoordHash> blocked;
    std::unordered_set<core::HexCoord, core::HexCoordHash> usedPivots;
    std::unordered_set<core::HexCoord, core::HexCoordHash> usedReleases;
    for (const auto& demand : demands)
        if (demand.fromMolecule) usedReleases.insert(demand.release);

    std::vector<core::HexCoord> pivots(demands.size(), core::HexCoord{0, 0});
    std::vector<bool> hasArm(demands.size(), false);
    for (std::size_t index = 0; index < demands.size(); ++index) {
        auto& demand = demands[index];
        if (!demand.fromMolecule) continue;
        bool placed = false;
        for (const auto& swing : SolutionsFor(core::Subtract(targets[index], demand.release))) {
            const auto pivot =
                core::Subtract(targets[index],
                               layout::SwingScale(layout::SwingStep(swing.drop), swing.length));
            if (productCells.count(pivot) != 0 || usedPivots.count(pivot) != 0) continue;
            if (usedReleases.count(pivot) != 0) continue;
            const auto disc = layout::SwingDisc(pivot, swing.length);
            bool clash = false;
            for (const auto& cell : disc) {
                if (cell == targets[index] || cell == demand.release) continue;
                if (productCells.count(cell) != 0 || blocked.count(cell) != 0 ||
                    usedReleases.count(cell) != 0) {
                    clash = true;
                    break;
                }
            }
            if (clash) continue;
            demand.swing = swing;
            pivots[index] = pivot;
            hasArm[index] = true;
            for (const auto& cell : disc) blocked.insert(cell);
            usedPivots.insert(pivot);
            placed = true;
            break;
        }
        if (!placed)
            throw std::invalid_argument("molecule assembly: no collision-free swing for an atom");
    }

    // ── schedule: every arm grabs on the same cycle, so the period is the slowest
    // swing, not their sum.
    int period = 3;
    for (std::size_t index = 0; index < demands.size(); ++index) {
        if (!hasArm[index]) continue;
        const int rotation = std::abs(
            layout::SwingRotation(demands[index].swing.grab, demands[index].swing.drop));
        period = std::max(period, kSettleCycles + (2 * rotation) + 2 + 1);
    }

    // ── emit
    const auto& molecule = puzzle.inputs[*moleculeInput];
    io::SolutionData machine;
    machine.puzzleName = puzzle.name;
    machine.solutionName = "Molecular settlement assembly";
    machine.objects.push_back(MakeObject(
        "input", landing->origin, {1, landing->turns, static_cast<int>(*moleculeInput), 0, 0}));
    // One unbonder per input bond; a cell hosts at most one glyph.
    std::unordered_set<core::HexCoord, core::HexCoordHash> glyphCells;
    for (const auto& bond : molecule.bonds) {
        auto a = core::Add(landing->origin, Rotate(bond.from, landing->turns));
        auto b = core::Add(landing->origin, Rotate(bond.to, landing->turns));
        if (glyphCells.count(a) != 0) std::swap(a, b);
        if (!glyphCells.insert(a).second)
            throw std::invalid_argument("molecule assembly: two bonds share a glyph cell");
        const int facing = layout::SwingDirectionOf(core::Subtract(b, a));
        if (facing < 0)
            throw std::invalid_argument("molecule assembly: transformed bond is not adjacent");
        machine.objects.push_back(MakeObject("unbonder", a, {1, facing, 0, 0, 0}));
    }
    for (const auto& demand : demands) {
        if (!demand.fromMolecule) {
            // A single-atom port lands its reagent directly on the product cell.
            const auto& source = puzzle.inputs[demand.port];
            const auto local = source.atoms.front().position;
            machine.objects.push_back(MakeObject("input",
                                                 core::Subtract(targets[demand.atom], local),
                                                 {1, 0, demand.port, 0, 0}));
            continue;
        }
        machine.objects.push_back(
            MakeObject("arm1", pivots[demand.atom],
                       {demand.swing.length, demand.swing.grab, 0, 0, 0},
                       SwingTape(demand.swing.grab, demand.swing.drop, period, kSettleCycles)));
    }
    for (const auto& bond : output.bonds) {
        const int facing = layout::SwingDirectionOf(core::Subtract(bond.to, bond.from));
        machine.objects.push_back(MakeObject("bonder", bond.from, {1, facing, 0, 0, 0}));
    }
    machine.objects.push_back(MakeObject("out-std", core::HexCoord{0, 0}, {1, 0, 0, 0, 0}));

    PruneReport report;
    auto result = postProcess ? SolutionPostProcess::CostOptimize(machine, &report)
                              : std::move(machine);
    result.puzzleName = puzzle.name;
    result.solutionName = "Molecular settlement assembly";
    if (detail) {
        detail->period = period;
        detail->fromMolecule = static_cast<std::size_t>(std::count_if(
            demands.begin(), demands.end(),
            [](const auto& demand) { return demand.fromMolecule; }));
        detail->fromPort = demands.size() - detail->fromMolecule;
        detail->unbonders = molecule.bonds.size();
    }
    return result;
}

void AppendMoleculeVariants(const io::PuzzleData& puzzle, bool postProcess,
                            std::vector<PipelineVariant>& variants) {
    // Its own switch: the pipeline family is measured separately, so the two must be
    // A/B-able without moving each other's baseline.
    const char* enabled = std::getenv("OPUSOPTIMA_MOLECULE_ARCHITECTURE");
    if (enabled == nullptr || std::string_view(enabled) != "on") return;
    if (!SupportsMoleculeAssembly(puzzle)) {
        GetLoggerOrFallback("solver")->info(
            "Assembly branch=molecule-architecture event=unsupported detail=domain");
        return;
    }
    try {
        MoleculeVariant detail;
        PipelineVariant variant;
        variant.solution = CompileMoleculeMachine(puzzle, postProcess, &detail);
        variant.policy = SchedulePolicy::Pipelined;
        variant.armPreference = PipelineVariant::ArmPreference::Compact;
        variant.period = detail.period;
        variant.name = "molecule-settlement; atoms=" + std::to_string(detail.fromMolecule) +
                       "+" + std::to_string(detail.fromPort) +
                       "; unbonders=" + std::to_string(detail.unbonders);
        variants.push_back(std::move(variant));
        GetLoggerOrFallback("solver")->info(
            "Assembly branch=molecule-architecture event=enumerated variants=1 atoms={} "
            "from_molecule={} from_port={} unbonders={} period={}",
            detail.fromMolecule + detail.fromPort, detail.fromMolecule, detail.fromPort,
            detail.unbonders, detail.period);
    } catch (const std::invalid_argument& error) {
        GetLoggerOrFallback("solver")->info(
            "Assembly branch=molecule-architecture event=unsupported detail={}", error.what());
    }
}

} // namespace opusoptima::solvers::architecture
