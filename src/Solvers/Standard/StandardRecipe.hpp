#pragma once

// Ported from gtw123/OpusSolver@fdedd3f01a238b941ceba8bace7ea35cbd3b8c41 (MIT).
// See docs/analysis/UPSTREAM_STANDARD_PORT_20261115.md for the module map.

// C++ port of upstream OpusSolver's recipe layer
// (gtw123/OpusSolver@fdedd3f, MIT): reaction set, reaction usage budget, and the
// integer linear program that chooses how many reagents/reactions each solution
// consumes. The LP is solved with the vendored HiGHS instead of lp_solve.

#include <map>
#include <optional>
#include <set>
#include <stdexcept>
#include <vector>

#include "StandardModel.hpp"

namespace opusoptima::solvers::standard {

enum class ReactionType {
    Reagent,
    Product,
    Calcification,
    VanBerlo,
    Projection,
    Purification,
    Animismus,
    Unification,
    Dispersion
};

struct Reaction {
    ReactionType type{};
    int id{};
    std::map<Element, int> inputs;
    std::map<Element, int> outputs;
};

class ReactionUsage {
    public:
    ReactionUsage(Reaction reaction, int maxUsages)
        : reaction_(std::move(reaction)), maxUsages_(maxUsages) {}
    [[nodiscard]] const Reaction& Get() const { return reaction_; }
    [[nodiscard]] int MaxUsages() const { return maxUsages_; }
    [[nodiscard]] bool IsAvailable() const { return currentUsages_ < maxUsages_; }
    void RecordUsage() {
        if (currentUsages_ >= maxUsages_)
            throw std::invalid_argument("reaction used more than its allowed count");
        ++currentUsages_;
    }

    private:
    Reaction reaction_;
    int maxUsages_{};
    int currentUsages_{};
};

class Recipe {
    public:
    Recipe() = default;

    void AddReaction(const Reaction& reaction, int usageCount);
    [[nodiscard]] bool HasAvailableReactions(ReactionType type,
                                             std::optional<int> id = std::nullopt,
                                             std::optional<Element> inputElement = std::nullopt,
                                             std::optional<Element> outputElement = std::nullopt) const;
    [[nodiscard]] std::vector<const ReactionUsage*> GetAvailableReactions(
        ReactionType type, std::optional<int> id = std::nullopt,
        std::optional<Element> inputElement = std::nullopt,
        std::optional<Element> outputElement = std::nullopt) const;
    void RecordReactionUsage(ReactionType type, std::optional<int> id = std::nullopt,
                             std::optional<Element> inputElement = std::nullopt,
                             std::optional<Element> outputElement = std::nullopt);

    private:
    [[nodiscard]] std::vector<ReactionUsage*> GetReactionUsages(
        ReactionType type, std::optional<int> id, std::optional<Element> inputElement,
        std::optional<Element> outputElement);
    [[nodiscard]] std::vector<const ReactionUsage*> GetReactionUsages(
        ReactionType type, std::optional<int> id, std::optional<Element> inputElement,
        std::optional<Element> outputElement) const;

    std::map<ReactionType, std::vector<ReactionUsage>> reactions_;
};

class RecipeBuilder {
    public:
    void AddReagents(const std::vector<Molecule>& reagents);
    void AddProducts(const std::vector<Molecule>& products, int outputScale);
    void AddReaction(ReactionType type);
    [[nodiscard]] std::vector<Recipe> GenerateRecipes(bool generateMultiple);

    private:
    [[nodiscard]] static Reaction CreateReaction(ReactionType type,
                                                 std::optional<Element> element = std::nullopt,
                                                 int id = 0);
    [[nodiscard]] static std::vector<Element> UsedElements(
        const std::vector<Reaction>& reactions,
        const std::vector<std::pair<Reaction, int>>& products);
    [[nodiscard]] static std::optional<Recipe> SolveLp(
        const std::vector<Reaction>& reactions,
        const std::vector<std::pair<Reaction, int>>& productReactions,
        const std::vector<Element>& usedElements, int productScale,
        const std::vector<Element>& relaxedElements);

    std::vector<Reaction> reactions_;
    std::vector<std::pair<Reaction, int>> productReactions_;
};

class RecipeGenerator {
    public:
    explicit RecipeGenerator(const Puzzle& puzzle) : puzzle_(puzzle) {}
    [[nodiscard]] std::vector<Recipe> GenerateRecipes(bool generateMultiple);

    private:
    void AnalyzeProductsAndReagents();
    void AnalyzeQuintessence();
    void AnalyzeMorsVitae();
    void AnalyzeCardinals();
    void AnalyzeSalt();
    void AnalyzeCardinalsAgain();
    void AnalyzeMetals();

    [[nodiscard]] bool IsElementMissing(Element element) const;
    [[nodiscard]] bool IsAnyElementMissing(const std::vector<Element>& elements) const;
    [[nodiscard]] bool IsAnyCardinalMissing() const;
    [[nodiscard]] std::vector<Element> GetMissingElements(const std::vector<Element>& elements) const;

    const Puzzle& puzzle_;
    std::set<Element> generatedElements_;
    std::set<Element> neededElements_;
    std::set<Element> reagentElements_;
    bool needAnyCardinal_{};
    RecipeBuilder recipeBuilder_;
};

} // namespace opusoptima::solvers::standard
