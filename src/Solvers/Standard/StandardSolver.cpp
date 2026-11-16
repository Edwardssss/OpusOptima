#include "StandardSolver.hpp"

// Ported from gtw123/OpusSolver@fdedd3f01a238b941ceba8bace7ea35cbd3b8c41 (MIT).
// See docs/analysis/UPSTREAM_STANDARD_PORT_20261115.md for the module map.


#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <typeinfo>
#include <vector>

#include "BaseSolver.hpp"
#include "BinaryWriter.hpp"
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

std::optional<io::SolutionData> CompileWithRecipe(Puzzle& model, Recipe& recipe,
                                                 bool reverseProductBuildOrder, bool diag) {
    auto trace = [&](const char* fmt, auto... args) {
        if (diag) std::fprintf(stderr, fmt, args...);
    };
    std::vector<const Molecule*> requiredReagents;
    for (const Molecule& reagent : model.reagents)
        if (recipe.HasAvailableReactions(ReactionType::Reagent, reagent.Id()))
            requiredReagents.push_back(&reagent);

    std::map<int, MoleculeElementInfo> reagentInfo;
    for (Molecule& reagent : model.reagents) {
        // Upstream `MoleculeDisassemblerFactory` rotates a bent/triangle reagent
        // into the orientation its specialized disassembler expects before the
        // element order is recorded.
        PrepareReagentMolecule(reagent);
        MoleculeElementInfo info;
        info.elementOrder = ReagentElementOrder(reagent);
        reagentInfo[reagent.Id()] = std::move(info);
    }
    std::map<int, MoleculeElementInfo> productInfo;
    std::vector<const Molecule*> productPointers;
    for (const Molecule& product : model.products) productPointers.push_back(&product);
    const bool useHex3 = UseHex3Assembler(productPointers);
    for (const Molecule& product : model.products) {
        MoleculeElementInfo info;
        if (useHex3) {
            info.elementOrder = ProductBuildOrder(product);
        } else {
            for (const Atom& atom : product.GetAtomsInInputOrder())
                info.elementOrder.push_back(atom.element);
        }
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

namespace {

[[nodiscard]] std::string SanitizePuzzleName(const std::string& name) {
    std::string out;
    out.reserve(name.size());
    for (char c : name) {
        const auto uc = static_cast<unsigned char>(c);
        out.push_back(std::isalnum(uc) || c == '-' || c == '_' ? c : '_');
    }
    if (out.empty()) out = "puzzle";
    return out;
}

struct RecipeSet {
    std::vector<Recipe> recipes;
    std::size_t subsetBegin{};
};

[[nodiscard]] bool NextCombination(std::vector<std::size_t>& positions,
                                   std::size_t count) {
    const std::size_t size = positions.size();
    for (std::size_t i = size; i-- > 0;) {
        if (positions[i] < count - size + i) {
            ++positions[i];
            for (std::size_t j = i + 1; j < size; ++j)
                positions[j] = positions[j - 1] + 1;
            return true;
        }
    }
    return false;
}

[[nodiscard]] RecipeSet GenerateAllRecipes(
    const Puzzle& model,
    std::optional<std::chrono::steady_clock::time_point> deadline = std::nullopt) {
    RecipeSet set;
    auto recipes = RecipeGenerator(model, false).GenerateRecipes(true);
    auto extra = RecipeGenerator(model, true).GenerateRecipes(true);
    recipes.insert(recipes.end(), std::make_move_iterator(extra.begin()),
                   std::make_move_iterator(extra.end()));
    set.recipes = std::move(recipes);
    set.subsetBegin = set.recipes.size();

    const char* subsetFlag = std::getenv("OPUSOPTIMA_STANDARD_SUBSET");
    const bool enumerateSubsets =
        subsetFlag == nullptr || std::string_view(subsetFlag) != "off";
    if (!enumerateSubsets || model.reagents.size() <= 1) return set;

    std::vector<int> ids;
    for (const Molecule& reagent : model.reagents) ids.push_back(reagent.Id());
    std::size_t added = 0;
    const std::size_t maxSubsetSize = std::min<std::size_t>(3, ids.size() - 1);
    for (std::size_t size = 1; size <= maxSubsetSize && added < 8; ++size) {
        std::vector<std::size_t> positions(size);
        for (std::size_t i = 0; i < size; ++i) positions[i] = i;
        while (added < 8) {
            if (deadline && std::chrono::steady_clock::now() >= *deadline) return set;
            std::vector<int> subset;
            for (std::size_t position : positions) subset.push_back(ids[position]);
            try {
                auto subsetRecipes =
                    RecipeGenerator(model, true).GenerateRecipesForReagents(true, subset);
                if (!subsetRecipes.empty()) {
                    set.recipes.push_back(std::move(subsetRecipes.front()));
                    ++added;
                }
            } catch (const std::exception&) {
                // The subset cannot satisfy the product balance.
            }
            if (!NextCombination(positions, ids.size())) break;
        }
    }
    return set;
}

} // namespace

std::vector<io::SolutionData> CompileUpstreamStandardVariants(
    const io::PuzzleData& puzzle,
    std::optional<std::chrono::steady_clock::time_point> deadline,
    std::size_t maxVariants) {
    const bool diag = std::getenv("OPUSOPTIMA_STANDARD_DIAG") != nullptr;
    std::vector<io::SolutionData> variants;
    try {
        Puzzle model = BuildPuzzle(puzzle);
        const auto recipeSet = GenerateAllRecipes(model, deadline);
        const auto& recipes = recipeSet.recipes;

        // `Standard.SolutionBuilder.GetAvailableParameters`: the only Standard
        // parameter is the product build order, and only for multi-product puzzles.
        std::vector<bool> buildOrders{false};
        if (model.products.size() > 1) buildOrders.push_back(true);

        std::vector<std::size_t> selected;
        const auto addSelected = [&](std::size_t index) {
            if (index < recipes.size() &&
                std::find(selected.begin(), selected.end(), index) == selected.end())
                selected.push_back(index);
        };
        if (std::getenv("OPUSOPTIMA_STANDARD_DUMP_ALL") != nullptr) {
            for (std::size_t index = 0; index < recipes.size(); ++index) addSelected(index);
        } else {
            addSelected(0);
            const bool multi = model.products.size() > 1;
            if (!multi) {
                // Subset recipes are the ones that usually remove a redundant
                // input, so compile them before the normal alternatives. This
                // keeps the best cycles candidate inside the solver budget when
                // two audits run concurrently (e.g. 5911f6b1 993 vs 1183).
                std::size_t subsets = 0;
                for (std::size_t index = recipeSet.subsetBegin;
                     index < recipes.size() && subsets < 2; ++index, ++subsets)
                    addSelected(index);
                for (std::size_t index : {std::size_t{1}, std::size_t{2}, std::size_t{7}})
                    if (index < recipeSet.subsetBegin) addSelected(index);
            } else {
                // As for single-output puzzles, compile the subset recipes
                // before the normal alternatives: they carry the changes that
                // remove a redundant input and would otherwise fall outside the
                // solver budget (5911f6b1 cycles 993 vs 1183).
                std::size_t subsets = 0;
                for (std::size_t index = recipeSet.subsetBegin;
                     index < recipes.size() && subsets < 2; ++index, ++subsets)
                    addSelected(index);
                addSelected(1);
                addSelected(2);
            }
        }

        if (diag) {
            std::fprintf(stderr, "SDIAG subsetBegin=%zu recipes=%zu selected=",
                         recipeSet.subsetBegin, recipes.size());
            for (std::size_t index : selected) std::fprintf(stderr, "%zu,", index);
            std::fprintf(stderr, "\n");
        }
        const char* dumpDir = std::getenv("OPUSOPTIMA_STANDARD_VARIANT_DIR");
        std::set<std::string> seen;
        std::size_t variantIndex = 0;
        for (std::size_t recipeIndex : selected) {
            if (recipeIndex >= recipes.size()) continue;
            for (bool reverse : buildOrders) {
                if (deadline && std::chrono::steady_clock::now() >= *deadline) return variants;
                // Each compile records reaction usage, so every attempt needs a
                // pristine copy of the recipe (upstream uses `recipe.Copy()`).
                Recipe recipe = recipes[recipeIndex];
                if (diag) {
                    std::fprintf(stderr, "SDIAG recipe=%zu reverse=%d reactions:",
                                 recipeIndex, reverse ? 1 : 0);
                    for (int type = 0; type <= 8; ++type) {
                        for (const ReactionUsage* usage :
                             recipe.GetAvailableReactions(static_cast<ReactionType>(type))) {
                            std::fprintf(stderr, " t%d/id%d/x%d", type, usage->Get().id,
                                         usage->MaxUsages());
                        }
                    }
                    std::fprintf(stderr, "\n");
                }
                try {
                    auto solution = CompileWithRecipe(model, recipe, reverse, diag);
                    if (!solution) continue;
                    auto canonical = *solution;
                    canonical.solutionName.clear();
                    canonical.metrics = {};
                    const auto bytes = io::BinaryWriter::WriteSolution(canonical);
                    const std::string key(reinterpret_cast<const char*>(bytes.data()), bytes.size());
                    if (!seen.insert(key).second) continue;

                    if (dumpDir != nullptr && *dumpDir != '\0') {
                        std::error_code error;
                        std::filesystem::create_directories(dumpDir, error);
                        const auto base = std::filesystem::path(dumpDir) /
                                          (SanitizePuzzleName(model.name) + "__v" +
                                           std::to_string(variantIndex));
                        io::BinaryWriter::WriteSolutionFile(base.string() + ".solution", *solution);
                        std::ofstream meta(base.string() + ".meta");
                        meta << "puzzle=" << model.name << "\n"
                             << "variant=" << variantIndex << "\n"
                             << "recipe=" << recipeIndex << "\n"
                             << "reverse=" << (reverse ? 1 : 0) << "\n";
                    }
                    variants.push_back(std::move(*solution));
                    ++variantIndex;
                    if (maxVariants != 0 && variants.size() >= maxVariants) return variants;
                } catch (const std::exception& error) {
                    if (diag)
                        std::fprintf(stderr, "SDIAG variant recipe=%zu reverse=%d: %s\n",
                                     recipeIndex, reverse ? 1 : 0, error.what());
                }
            }
        }
    } catch (const std::exception& error) {
        if (diag) std::fprintf(stderr, "SDIAG variants exception: %s\n", error.what());
    }
    return variants;
}

std::optional<io::SolutionData> CompileUpstreamStandard(const io::PuzzleData& puzzle) {
    const bool diag = std::getenv("OPUSOPTIMA_STANDARD_DIAG") != nullptr;
    try {
        // The dump path is a measurement hook for the `--optimize` enumeration;
        // the normal CLI still compiles one candidate.
        if (std::getenv("OPUSOPTIMA_STANDARD_VARIANT_DIR") != nullptr) {
            auto variants = CompileUpstreamStandardVariants(puzzle);
            if (variants.empty()) return std::nullopt;
            return std::move(variants.front());
        }
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
