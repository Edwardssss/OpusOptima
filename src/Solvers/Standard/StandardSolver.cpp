#include "StandardSolver.hpp"

// Ported from gtw123/OpusSolver@fdedd3f01a238b941ceba8bace7ea35cbd3b8c41 (MIT).
// See docs/analysis/UPSTREAM_STANDARD_PORT_20261115.md for the module map.


#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <typeinfo>
#include <vector>

#include "BaseSolver.hpp"
#include "StandardModules.hpp"
#include "StandardPipeline.hpp"
#include "StandardProgram.hpp"
#include "StandardRecipe.hpp"

namespace opusoptima::solvers::standard {
namespace {

// ── Cost optimizer (upstream CostOptimizer.cs) ──────────────────────────────
class CostOptimizer {
    public:
    CostOptimizer(GameObject* root, Program& program) : root_(root), program_(program) {}

    void Optimize() {
        RemoveUnusedArms();
        ConvertPistonsToArms();
        RemoveUnusedTracks();
    }

    private:
    static void CollectAll(GameObject* object, std::vector<GameObject*>& out) {
        if (!object || object->Removed()) return;
        out.push_back(object);
        for (GameObject* child : object->Children()) CollectAll(child, out);
    }

    [[nodiscard]] std::vector<GameObject*> AllObjects() const {
        std::vector<GameObject*> out;
        CollectAll(root_, out);
        return out;
    }

    void RemoveUnusedArms() {
        for (GameObject* object : AllObjects()) {
            auto* arm = dynamic_cast<Arm*>(object);
            if (!arm || arm->type == ArmType::VanBerlo) continue;
            const auto it = program_.instructions.find(arm);
            bool hasGrab = false;
            if (it != program_.instructions.end())
                for (Instr instruction : it->second)
                    hasGrab |= instruction == Instr::Grab;
            if (it == program_.instructions.end() || !hasGrab) arm->Remove();
        }
    }

    void ConvertPistonsToArms() {
        for (auto& [arm, instructions] : program_.instructions) {
            if (arm->type != ArmType::Piston) continue;
            bool extends = false;
            for (Instr instruction : instructions)
                extends |= instruction == Instr::Extend || instruction == Instr::Retract;
            if (!extends) arm->type = ArmType::Arm1;
        }
    }

    [[nodiscard]] std::vector<int> TrackCellsUsedByArm(const std::vector<Vec2>& cells,
                                                       bool looping, Arm* arm) const {
        int startIndex = -1;
        const Vec2 armPosition = arm->WorldTransform().position;
        for (std::size_t i = 0; i < cells.size(); ++i)
            if (cells[i] == armPosition) {
                startIndex = static_cast<int>(i);
                break;
            }
        if (startIndex < 0) return {};
        int minIndex = startIndex;
        int maxIndex = startIndex;
        int index = startIndex;
        const auto it = program_.instructions.find(arm);
        if (it != program_.instructions.end()) {
            for (Instr instruction : it->second) {
                switch (instruction) {
                case Instr::MovePos:
                    if (index < static_cast<int>(cells.size()) - 1)
                        ++index;
                    else if (looping)
                        index = 0;
                    break;
                case Instr::MoveNeg:
                    if (index > 0)
                        --index;
                    else if (looping)
                        index = static_cast<int>(cells.size()) - 1;
                    break;
                case Instr::Reset: index = startIndex; break;
                default: break;
                }
                minIndex = std::min(minIndex, index);
                maxIndex = std::max(maxIndex, index);
            }
        }
        if (maxIndex <= minIndex) return {};
        std::vector<int> used;
        for (int i = minIndex; i <= maxIndex; ++i) used.push_back(i);
        return used;
    }

    void RemoveUnusedTracks() {
        for (GameObject* object : AllObjects()) {
            auto* track = dynamic_cast<Track*>(object);
            if (!track) continue;
            const auto cells = track->GetAllPathCells();
            const bool looping = track->IsLooping();
            std::set<int> used;
            for (GameObject* other : AllObjects()) {
                auto* arm = dynamic_cast<Arm*>(other);
                if (!arm) continue;
                const auto armUsed = TrackCellsUsedByArm(cells, looping, arm);
                used.insert(armUsed.begin(), armUsed.end());
            }
            if (used.empty()) {
                track->Remove();
            } else {
                track->TrimPath(*used.begin(), *used.rbegin());
            }
        }
    }

    GameObject* root_;
    Program& program_;
};

// ── Solution conversion ─────────────────────────────────────────────────────
[[nodiscard]] std::string ArmTypeName(ArmType type) {
    switch (type) {
    case ArmType::Arm1: return "arm1";
    case ArmType::Arm2: return "arm2";
    case ArmType::Arm3: return "arm3";
    case ArmType::Arm6: return "arm6";
    case ArmType::Piston: return "piston";
    case ArmType::VanBerlo: return "baron";
    }
    throw std::invalid_argument("unknown arm type");
}

[[nodiscard]] std::string GlyphTypeName(GlyphType type) {
    switch (type) {
    case GlyphType::Bonding: return "bonder";
    case GlyphType::MultiBonding: return "bonder-speed";
    case GlyphType::TriplexBonding: return "bonder-prisma";
    case GlyphType::Unbonding: return "unbonder";
    case GlyphType::Calcification: return "glyph-calcification";
    case GlyphType::Duplication: return "glyph-duplication";
    case GlyphType::Projection: return "glyph-projection";
    case GlyphType::Purification: return "glyph-purification";
    case GlyphType::Animismus: return "glyph-life-and-death";
    case GlyphType::Disposal: return "glyph-disposal";
    case GlyphType::Equilibrium: return "glyph-marker";
    case GlyphType::Unification: return "glyph-unification";
    case GlyphType::Dispersion: return "glyph-dispersion";
    }
    throw std::invalid_argument("unknown glyph type");
}

[[nodiscard]] io::SolutionData ConvertSolution(GameObject* root, const Program& program,
                                               const std::string& puzzleName) {
    io::SolutionData solution;
    solution.puzzleName = puzzleName;
    solution.solutionName = "Generated solution (Upstream Standard port)";
    int nextArmId = 0;
    for (GameObject* object : CollectOutputObjects(root)) {
        io::SolutionObject out;
        if (auto* arm = dynamic_cast<Arm*>(object)) {
            out.typeName = ArmTypeName(arm->type);
            const Transform world = arm->WorldTransform();
            out.position = {world.position.x, world.position.y};
            out.bodyData = {arm->extension, world.rotation.value, 0, 0, nextArmId++};
            const auto it = program.instructions.find(arm);
            if (it != program.instructions.end()) {
                for (std::size_t index = 0; index < it->second.size(); ++index) {
                    const Instr instruction = it->second[index];
                    if (instruction == Instr::None || instruction == Instr::Wait) continue;
                    out.instructions.push_back(
                        {InstructionCode(instruction), static_cast<std::int32_t>(index)});
                }
            }
        } else if (auto* glyph = dynamic_cast<Glyph*>(object)) {
            out.typeName = GlyphTypeName(glyph->type);
            const Transform world = glyph->WorldTransform();
            out.position = {world.position.x, world.position.y};
            out.bodyData = {1, world.rotation.value, 0, 0, 0};
        } else if (auto* track = dynamic_cast<Track*>(object)) {
            out.typeName = "track";
            const Transform world = track->WorldTransform();
            out.position = {world.position.x, world.position.y};
            out.bodyData = {1, 0, 0, 0, static_cast<int>(track->Path().size())};
            for (const Vec2& cell : track->Path()) {
                const Vec2 rotated = RotateBy(cell, world.rotation);
                out.bodyData.push_back(rotated.x);
                out.bodyData.push_back(rotated.y);
            }
            out.bodyData.push_back(0);
        } else if (auto* molecule = dynamic_cast<MoleculeInputOutput*>(object)) {
            out.typeName = dynamic_cast<Reagent*>(object) ? "input"
                          : molecule->molecule->HasRepeats() ? "out-rep"
                                                             : "out-std";
            Transform transform = molecule->WorldTransform().Apply(
                molecule->molecule->GlyphTransform());
            out.position = {transform.position.x, transform.position.y};
            out.bodyData = {1, transform.rotation.value, molecule->molecule->Id(), 0, 0};
        } else {
            continue;
        }
        solution.objects.push_back(std::move(out));
    }
    return solution;
}

} // namespace

namespace {

std::optional<io::SolutionData> CompileWithRecipe(const Puzzle& model, Recipe& recipe,
                                                 bool reverseProductBuildOrder, bool diag) {
    auto trace = [&](const char* fmt, auto... args) {
        if (diag) std::fprintf(stderr, fmt, args...);
    };
    std::vector<const Molecule*> requiredReagents;
    for (const Molecule& reagent : model.reagents)
        if (recipe.HasAvailableReactions(ReactionType::Reagent, reagent.Id()))
            requiredReagents.push_back(&reagent);

    std::map<int, MoleculeElementInfo> reagentInfo;
    for (const Molecule& reagent : model.reagents) {
        MoleculeElementInfo info;
        for (const Atom& atom : reagent.GetAtomsInInputOrder())
            info.elementOrder.push_back(atom.element);
        reagentInfo[reagent.Id()] = std::move(info);
    }
    std::map<int, MoleculeElementInfo> productInfo;
    for (const Molecule& product : model.products) {
        MoleculeElementInfo info;
        for (const Atom& atom : product.GetAtomsInInputOrder())
            info.elementOrder.push_back(atom.element);
        productInfo[product.Id()] = std::move(info);
    }

    SolutionPlan plan(model, recipe, requiredReagents, std::move(reagentInfo),
                      std::move(productInfo));
    plan.usePendingElementsInOrder = true;
    plan.reverseProductBuildOrder = reverseProductBuildOrder;

    ElementPipeline pipeline(plan);
    trace("SDIAG pipeline generators=%zu\n", pipeline.Generators().size());
    CommandSequence commands = pipeline.GenerateCommandSequence();
    trace("SDIAG commands=%zu\n", commands.Commands().size());

    ProgramWriter writer;
    std::unique_ptr<AtomGenerator> root;
    CreateAtomGenerators(pipeline, writer, root);
    if (!root) return std::nullopt;
    trace("SDIAG atom generators created fragments=%zu\n", writer.Fragments().size());

    for (ElementGenerator* element : pipeline.Generators())
        if (element->Atom()) element->Atom()->BeginSolution();
    int commandIndex = 0;
    for (const Command& command : commands.Commands()) {
        AtomGenerator* atom = command.target->Atom();
        if (!atom) throw std::invalid_argument("command targets a missing atom generator");
        if (diag)
            std::fprintf(stderr, "SDIAG cmd=%d type=%d elem=%d target=%s\n", commandIndex,
                         static_cast<int>(command.type), static_cast<int>(command.element),
                         typeid(*atom).name());
        switch (command.type) {
        case CommandType::Consume: atom->Consume(command.element, command.id); break;
        case CommandType::Generate: atom->Generate(command.element, command.id); break;
        case CommandType::PassThrough: atom->PassThrough(command.element); break;
        }
        ++commandIndex;
    }
    trace("SDIAG commands executed fragments=%zu\n", writer.Fragments().size());
    for (ElementGenerator* element : pipeline.Generators())
        if (element->Atom()) element->Atom()->EndSolution();
    for (ElementGenerator* element : pipeline.Generators())
        if (element->Atom()) element->Atom()->OptimizeParts();

    Program program = ProgramBuilder(writer.Fragments()).Build();
    int maxTape = 0;
    for (const auto& [arm, instructions] : program.instructions) {
        (void)arm;
        maxTape = std::max(maxTape, static_cast<int>(instructions.size()));
    }
    trace("SDIAG program arms=%zu maxTape=%d\n", program.instructions.size(), maxTape);
    CostOptimizer(root.get(), program).Optimize();
    return ConvertSolution(root.get(), program, model.name);
}

} // namespace

std::optional<io::SolutionData> CompileUpstreamStandard(const io::PuzzleData& puzzle) {
    const bool diag = std::getenv("OPUSOPTIMA_STANDARD_DIAG") != nullptr;
    try {
        Puzzle model = BuildPuzzle(puzzle);
        RecipeGenerator generator(model);
        const auto recipes = generator.GenerateRecipes(false);
        if (recipes.empty()) return std::nullopt;
        Recipe recipe = recipes.front();
        return CompileWithRecipe(model, recipe, false, diag);
    } catch (const std::exception& error) {
        if (diag) std::fprintf(stderr, "SDIAG exception: %s\n", error.what());
        return std::nullopt;
    }
}

} // namespace opusoptima::solvers::standard

namespace opusoptima::solvers {

namespace {
class UpstreamStandardSolver final : public BaseSolver {
    public:
    UpstreamStandardSolver() : BaseSolver("UpstreamStandard") {}
    [[nodiscard]] io::SolutionData Solve(const io::PuzzleData& puzzle) override {
        auto solution = standard::CompileUpstreamStandard(puzzle);
        if (!solution) throw std::invalid_argument("upstream standard solver unsupported");
        return std::move(*solution);
    }
    [[nodiscard]] std::int32_t EstimateCycles(const io::PuzzleData& puzzle) override {
        static_cast<void>(puzzle);
        return 0;
    }
};
const bool kRegistered = [] {
    BaseSolver::Register("UpstreamStandard", [] {
        return std::make_unique<UpstreamStandardSolver>();
    });
    BaseSolver::Register("upstream-standard", [] {
        return std::make_unique<UpstreamStandardSolver>();
    });
    return true;
}();
} // namespace

} // namespace opusoptima::solvers
