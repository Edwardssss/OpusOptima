#include "StandardRecipe.hpp"

// Ported from gtw123/OpusSolver@fdedd3f01a238b941ceba8bace7ea35cbd3b8c41 (MIT).
// See docs/analysis/UPSTREAM_STANDARD_PORT_20261115.md for the module map.


#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <utility>

#include "Highs.h"

namespace opusoptima::solvers::standard {

namespace {

std::map<Element, int> ElementCounts(const Molecule& molecule) {
    std::map<Element, int> counts;
    for (const Atom& atom : molecule.Atoms()) ++counts[atom.element];
    return counts;
}

bool ContainsElement(const std::map<Element, int>& map, Element element) {
    return map.find(element) != map.end();
}

} // namespace


void Recipe::AddReaction(const Reaction& reaction, int usageCount) {
    if (usageCount == 0) return;
    reactions_[reaction.type].emplace_back(reaction, usageCount);
}

std::vector<const ReactionUsage*> Recipe::GetAvailableReactions(
    ReactionType type, std::optional<int> id, std::optional<Element> inputElement,
    std::optional<Element> outputElement) const {
    std::vector<const ReactionUsage*> out;
    for (const ReactionUsage* usage :
         GetReactionUsages(type, id, inputElement, outputElement))
        if (usage->IsAvailable()) out.push_back(usage);
    return out;
}

bool Recipe::HasAvailableReactions(ReactionType type, std::optional<int> id,
                                   std::optional<Element> inputElement,
                                   std::optional<Element> outputElement) const {
    return !GetAvailableReactions(type, id, inputElement, outputElement).empty();
}

std::vector<const ReactionUsage*> Recipe::GetReactionUsages(
    ReactionType type, std::optional<int> id, std::optional<Element> inputElement,
    std::optional<Element> outputElement) const {
    std::vector<const ReactionUsage*> out;
    const auto it = reactions_.find(type);
    if (it == reactions_.end()) return out;
    for (const ReactionUsage& usage : it->second) {
        const Reaction& reaction = usage.Get();
        if (id && reaction.id != *id) continue;
        if (inputElement && !ContainsElement(reaction.inputs, *inputElement)) continue;
        if (outputElement && !ContainsElement(reaction.outputs, *outputElement)) continue;
        out.push_back(&usage);
    }
    return out;
}

std::vector<ReactionUsage*> Recipe::GetReactionUsages(
    ReactionType type, std::optional<int> id, std::optional<Element> inputElement,
    std::optional<Element> outputElement) {
    std::vector<ReactionUsage*> out;
    const auto it = reactions_.find(type);
    if (it == reactions_.end()) return out;
    for (ReactionUsage& usage : it->second) {
        const Reaction& reaction = usage.Get();
        if (id && reaction.id != *id) continue;
        if (inputElement && !ContainsElement(reaction.inputs, *inputElement)) continue;
        if (outputElement && !ContainsElement(reaction.outputs, *outputElement)) continue;
        out.push_back(&usage);
    }
    return out;
}

void Recipe::RecordReactionUsage(ReactionType type, std::optional<int> id,
                                 std::optional<Element> inputElement,
                                 std::optional<Element> outputElement) {
    auto usages = GetReactionUsages(type, id, inputElement, outputElement);
    if (usages.empty()) throw std::invalid_argument("no reaction matches the usage criteria");
    if (usages.size() > 1) throw std::invalid_argument("more than one reaction matches");
    usages.front()->RecordUsage();
}


// ── RecipeBuilder ───────────────────────────────────────────────────────────
void RecipeBuilder::AddReagents(const std::vector<Molecule>& reagents) {
    std::map<int, std::map<Element, int>> countsById;
    for (const Molecule& reagent : reagents) countsById[reagent.Id()] = ElementCounts(reagent);

    // Remove redundant single-element reagents, keeping the smallest of each.
    std::map<Element, std::vector<std::pair<int, int>>> singleElement;
    for (const auto& [id, counts] : countsById) {
        if (counts.size() != 1) continue;
        singleElement[counts.begin()->first].push_back({id, counts.begin()->second});
    }
    for (auto& [element, group] : singleElement) {
        std::sort(group.begin(), group.end(),
                  [](const auto& a, const auto& b) { return a.second < b.second; });
        for (std::size_t i = 1; i < group.size(); ++i) countsById.erase(group[i].first);
    }

    for (const auto& [id, counts] : countsById) {
        reactions_.push_back(Reaction{ReactionType::Reagent, id, {}, counts});
    }
}

void RecipeBuilder::AddProducts(const std::vector<Molecule>& products, int outputScale) {
    bool anyRepeats = false;
    for (const Molecule& product : products) anyRepeats |= product.HasRepeats();
    for (const Molecule& product : products) {
        const int numCopies = (anyRepeats && !product.HasRepeats()) ? 6 * outputScale : 1;
        productReactions_.push_back(
            {Reaction{ReactionType::Product, product.Id(), ElementCounts(product), {}}, numCopies});
    }
}

Reaction RecipeBuilder::CreateReaction(ReactionType type, std::optional<Element> element,
                                       int id) {
    if ((type == ReactionType::Calcification || type == ReactionType::VanBerlo ||
         type == ReactionType::Projection || type == ReactionType::Purification) &&
        !element) {
        throw std::invalid_argument("this reaction requires an element");
    }
    Reaction reaction;
    reaction.type = type;
    reaction.id = id;
    switch (type) {
    case ReactionType::Calcification:
        reaction.inputs[*element] = 1;
        reaction.outputs[Element::Salt] = 1;
        break;
    case ReactionType::VanBerlo:
        reaction.inputs[Element::Salt] = 1;
        reaction.outputs[*element] = 1;
        break;
    case ReactionType::Animismus:
        reaction.inputs[Element::Salt] = 2;
        reaction.outputs[Element::Mors] = 1;
        reaction.outputs[Element::Vitae] = 1;
        break;
    case ReactionType::Projection:
        reaction.inputs[*element] = 1;
        reaction.inputs[Element::Quicksilver] = 1;
        reaction.outputs[static_cast<Element>(ElemIndex(*element) + 1)] = 1;
        break;
    case ReactionType::Purification:
        reaction.inputs[*element] = 2;
        reaction.outputs[static_cast<Element>(ElemIndex(*element) + 1)] = 1;
        break;
    case ReactionType::Dispersion:
        reaction.inputs[Element::Quintessence] = 1;
        reaction.outputs[Element::Air] = 1;
        reaction.outputs[Element::Fire] = 1;
        reaction.outputs[Element::Water] = 1;
        reaction.outputs[Element::Earth] = 1;
        break;
    case ReactionType::Unification:
        reaction.inputs[Element::Air] = 1;
        reaction.inputs[Element::Fire] = 1;
        reaction.inputs[Element::Water] = 1;
        reaction.inputs[Element::Earth] = 1;
        reaction.outputs[Element::Quintessence] = 1;
        break;
    default: throw std::invalid_argument("invalid reaction type");
    }
    return reaction;
}

void RecipeBuilder::AddReaction(ReactionType type) {
    if (type == ReactionType::Calcification || type == ReactionType::VanBerlo) {
        for (Element element : PeriodicTable::Cardinals())
            reactions_.push_back(CreateReaction(type, element));
    } else if (type == ReactionType::Projection || type == ReactionType::Purification) {
        for (std::size_t i = 0; i + 1 < PeriodicTable::Metals().size(); ++i)
            reactions_.push_back(CreateReaction(type, PeriodicTable::Metals()[i]));
    } else {
        reactions_.push_back(CreateReaction(type));
    }
}

std::vector<Element> RecipeBuilder::UsedElements(
    const std::vector<Reaction>& reactions,
    const std::vector<std::pair<Reaction, int>>& products) {
    std::set<Element> used;
    for (const Reaction& reaction : reactions) {
        for (const auto& [element, count] : reaction.inputs) {
            (void)count;
            used.insert(element);
        }
        for (const auto& [element, count] : reaction.outputs) {
            (void)count;
            used.insert(element);
        }
    }
    for (const auto& [reaction, count] : products) {
        (void)count;
        for (const auto& [element, value] : reaction.inputs) {
            (void)value;
            used.insert(element);
        }
    }
    return std::vector<Element>(used.begin(), used.end());
}

std::optional<Recipe> RecipeBuilder::SolveLp(
    const std::vector<Reaction>& reactions,
    const std::vector<std::pair<Reaction, int>>& productReactions,
    const std::vector<Element>& usedElements, int productScale,
    const std::vector<Element>& relaxedElements) {
    const std::size_t numVars = reactions.size();
    const std::size_t numRows = usedElements.size();
    if (numVars == 0) return std::nullopt;

    std::vector<double> colCost(numVars, 0.0);
    for (std::size_t i = 0; i < numVars; ++i) {
        if (reactions[i].type == ReactionType::Reagent) {
            int atoms = 0;
            for (const auto& [element, count] : reactions[i].outputs) {
                (void)element;
                atoms += count;
            }
            colCost[i] = 1.0 * atoms;
        }
    }

    std::vector<double> colLower(numVars, 0.0);
    std::vector<double> colUpper(numVars, 1e30);
    std::vector<HighsInt> integrality(numVars, 1);

    std::vector<double> rowLower(numRows, 0.0);
    std::vector<double> rowUpper(numRows, 0.0);
    std::vector<double> rhs(numRows, 0.0);
    for (std::size_t r = 0; r < numRows; ++r) {
        const Element element = usedElements[r];
        for (const auto& [product, count] : productReactions) {
            const auto it = product.inputs.find(element);
            if (it != product.inputs.end()) rhs[r] += static_cast<double>(count * it->second * productScale);
        }
    }
    std::set<Element> relaxed(relaxedElements.begin(), relaxedElements.end());
    for (std::size_t r = 0; r < numRows; ++r) {
        rowLower[r] = rhs[r];
        rowUpper[r] = relaxed.contains(usedElements[r]) ? 1e30 : rhs[r];
    }

    // Column-wise sparse matrix: one entry per (reaction, element) difference.
    std::vector<HighsInt> aStart;
    std::vector<HighsInt> aIndex;
    std::vector<double> aValue;
    for (std::size_t c = 0; c < numVars; ++c) {
        aStart.push_back(static_cast<HighsInt>(aIndex.size()));
        for (std::size_t r = 0; r < numRows; ++r) {
            const Element element = usedElements[r];
            int output = 0;
            int input = 0;
            if (const auto it = reactions[c].outputs.find(element); it != reactions[c].outputs.end())
                output = it->second;
            if (const auto it = reactions[c].inputs.find(element); it != reactions[c].inputs.end())
                input = it->second;
            const int coefficient = output - input;
            if (coefficient != 0) {
                aIndex.push_back(static_cast<HighsInt>(r));
                aValue.push_back(static_cast<double>(coefficient));
            }
        }
    }
    aStart.push_back(static_cast<HighsInt>(aIndex.size()));

    Highs highs;
    highs.setOptionValue("output_flag", false);
    const HighsStatus status = highs.passModel(
        static_cast<HighsInt>(numVars), static_cast<HighsInt>(numRows),
        static_cast<HighsInt>(aValue.size()), 1, 1, 0.0, colCost.data(), colLower.data(),
        colUpper.data(), rowLower.data(), rowUpper.data(), aStart.data(), aIndex.data(),
        aValue.data(), integrality.data());
    if (status == HighsStatus::kError) return std::nullopt;
    if (highs.run() == HighsStatus::kError) return std::nullopt;
    if (highs.getModelStatus() != HighsModelStatus::kOptimal) return std::nullopt;

    const auto solution = highs.getSolution();
    Recipe recipe;
    for (std::size_t i = 0; i < numVars; ++i) {
        const int value = static_cast<int>(std::llround(solution.col_value[i]));
        if (value > 0) recipe.AddReaction(reactions[i], value);
    }
    for (const auto& [product, count] : productReactions) {
        recipe.AddReaction(product, count * productScale);
    }
    return recipe;
}

std::vector<Recipe> RecipeBuilder::GenerateRecipes(
    bool generateMultiple, const std::vector<int>& reagentIds) {
    const std::vector<Reaction>* reactions = &reactions_;
    std::vector<Reaction> filtered;
    if (!reagentIds.empty()) {
        for (const Reaction& reaction : reactions_) {
            if (reaction.type == ReactionType::Reagent &&
                std::find(reagentIds.begin(), reagentIds.end(), reaction.id) ==
                    reagentIds.end())
                continue;
            filtered.push_back(reaction);
        }
        reactions = &filtered;
    }
    const auto used = UsedElements(*reactions, productReactions_);
    std::vector<Recipe> recipes;

    // Upstream `FindFeasibleRecipes`: every feasible product scale is a distinct
    // recipe. Scale 1 is tried first, so the single-recipe path is unchanged.
    for (int scale = 1; scale <= 6; ++scale) {
        if (auto recipe = SolveLp(*reactions, productReactions_, used, scale, {})) {
            if (!generateMultiple) return {std::move(*recipe)};
            recipes.push_back(std::move(*recipe));
        }
    }

    std::vector<Element> cardinals;
    for (Element element : PeriodicTable::Cardinals())
        if (std::find(used.begin(), used.end(), element) != used.end())
            cardinals.push_back(element);
    if (!cardinals.empty()) {
        if (auto recipe = SolveLp(*reactions, productReactions_, used, 1, cardinals)) {
            if (!generateMultiple) return {std::move(*recipe)};
            recipes.push_back(std::move(*recipe));
        }
    }

    if (auto recipe = SolveLp(*reactions, productReactions_, used, 1, used)) {
        if (!generateMultiple) return {std::move(*recipe)};
        recipes.push_back(std::move(*recipe));
    }

    if (recipes.empty())
        throw std::invalid_argument("could not solve the recipe linear program");
    return recipes;
}

// ── RecipeGenerator ─────────────────────────────────────────────────────────
std::vector<Recipe> RecipeGenerator::GenerateRecipes(bool generateMultiple) {
    AnalyzeProductsAndReagents();
    AnalyzeQuintessence();
    AnalyzeMorsVitae();
    AnalyzeCardinals();
    AnalyzeSalt();
    AnalyzeCardinalsAgain();
    AnalyzeMetals();
    return recipeBuilder_.GenerateRecipes(generateMultiple);
}

std::vector<Recipe> RecipeGenerator::GenerateRecipesForReagents(
    bool generateMultiple, const std::vector<int>& reagentIds) {
    AnalyzeProductsAndReagents();
    AnalyzeQuintessence();
    AnalyzeMorsVitae();
    AnalyzeCardinals();
    AnalyzeSalt();
    AnalyzeCardinalsAgain();
    AnalyzeMetals();
    return recipeBuilder_.GenerateRecipes(generateMultiple, reagentIds);
}

void RecipeGenerator::AnalyzeProductsAndReagents() {
    for (const Molecule& product : puzzle_.products)
        for (const Atom& atom : product.Atoms()) neededElements_.insert(atom.element);
    recipeBuilder_.AddProducts(puzzle_.products, puzzle_.outputScale);

    for (const Molecule& reagent : puzzle_.reagents)
        for (const Atom& atom : reagent.Atoms()) reagentElements_.insert(atom.element);
    generatedElements_.insert(reagentElements_.begin(), reagentElements_.end());
    recipeBuilder_.AddReagents(puzzle_.reagents);
}

void RecipeGenerator::AnalyzeQuintessence() {
    if (!IsElementMissing(Element::Quintessence)) return;
    if (!puzzle_.AllowsGlyph(GlyphType::Unification))
        throw std::invalid_argument(
            "puzzle requires Quintessence but does not allow unification");
    generatedElements_.insert(Element::Quintessence);
    for (Element element : PeriodicTable::Cardinals()) neededElements_.insert(element);
    recipeBuilder_.AddReaction(ReactionType::Unification);
}

void RecipeGenerator::AnalyzeMorsVitae() {
    std::vector<Element> missing;
    for (Element element : PeriodicTable::MorsVitae())
        if (IsElementMissing(element)) missing.push_back(element);
    if (missing.empty()) return;
    if (!puzzle_.AllowsGlyph(GlyphType::Animismus))
        throw std::invalid_argument("puzzle requires Mors/Vitae but does not allow Animismus");
    generatedElements_.insert(Element::Mors);
    generatedElements_.insert(Element::Vitae);
    neededElements_.insert(Element::Salt);
    recipeBuilder_.AddReaction(ReactionType::Animismus);
}

void RecipeGenerator::AnalyzeCardinals() {
    if (IsAnyCardinalMissing()) {
        bool reagentHasCardinalOrSalt = reagentElements_.contains(Element::Salt);
        for (Element element : PeriodicTable::Cardinals())
            reagentHasCardinalOrSalt |= reagentElements_.contains(element);
        if (puzzle_.AllowsArm(ArmType::VanBerlo) &&
            puzzle_.AllowsGlyph(GlyphType::Duplication) && reagentHasCardinalOrSalt) {
            for (Element element : PeriodicTable::Cardinals())
                generatedElements_.insert(element);
            neededElements_.insert(Element::Salt);
            recipeBuilder_.AddReaction(ReactionType::VanBerlo);
            return;
        }
    }

    // Upstream `RecipeOptions.IncludeUneededReactions`: an extra wheel can give
    // the LP a cheaper reagent mix even when no cardinal is strictly missing.
    if (includeUnneededReactions_ && puzzle_.AllowsArm(ArmType::VanBerlo) &&
        puzzle_.AllowsGlyph(GlyphType::Duplication))
        recipeBuilder_.AddReaction(ReactionType::VanBerlo);
}

void RecipeGenerator::AnalyzeSalt() {
    if (IsElementMissing(Element::Salt)) {
        if (!puzzle_.AllowsGlyph(GlyphType::Calcification))
            throw std::invalid_argument("puzzle requires Salt but does not allow calcification");
        generatedElements_.insert(Element::Salt);
        needAnyCardinal_ = true;
        recipeBuilder_.AddReaction(ReactionType::Calcification);
        return;
    }

    if (includeUnneededReactions_ && puzzle_.AllowsGlyph(GlyphType::Calcification))
        recipeBuilder_.AddReaction(ReactionType::Calcification);
}

void RecipeGenerator::AnalyzeCardinalsAgain() {
    if (!IsAnyCardinalMissing()) return;
    if (puzzle_.AllowsGlyph(GlyphType::Dispersion) &&
        reagentElements_.contains(Element::Quintessence)) {
        for (Element element : PeriodicTable::Cardinals()) generatedElements_.insert(element);
        neededElements_.insert(Element::Quintessence);
        recipeBuilder_.AddReaction(ReactionType::Dispersion);
        return;
    }
    throw std::invalid_argument(
        "puzzle requires cardinals but allows neither Van Berlo duplication nor dispersion");
}

void RecipeGenerator::AnalyzeMetals() {
    std::vector<Element> missing;
    for (Element metal : PeriodicTable::Metals())
        if (IsElementMissing(metal)) missing.push_back(metal);
    if (missing.empty()) return;
    if (puzzle_.AllowsGlyph(GlyphType::Projection) &&
        reagentElements_.contains(Element::Quicksilver)) {
        for (Element metal : missing) generatedElements_.insert(metal);
        neededElements_.insert(Element::Quicksilver);
        recipeBuilder_.AddReaction(ReactionType::Projection);
        return;
    }
    if (puzzle_.AllowsGlyph(GlyphType::Purification)) {
        for (Element metal : missing) generatedElements_.insert(metal);
        recipeBuilder_.AddReaction(ReactionType::Purification);
        return;
    }
    throw std::invalid_argument("puzzle requires metals but allows neither projection nor "
                                "purification");
}

bool RecipeGenerator::IsElementMissing(Element element) const {
    return neededElements_.contains(element) && !generatedElements_.contains(element);
}

std::vector<Element> RecipeGenerator::GetMissingElements(
    const std::vector<Element>& elements) const {
    std::vector<Element> missing;
    for (Element element : elements)
        if (IsElementMissing(element)) missing.push_back(element);
    return missing;
}

bool RecipeGenerator::IsAnyElementMissing(const std::vector<Element>& elements) const {
    return !GetMissingElements(elements).empty();
}

bool RecipeGenerator::IsAnyCardinalMissing() const {
    if (IsAnyElementMissing(std::vector<Element>(PeriodicTable::Cardinals().begin(),
                                                 PeriodicTable::Cardinals().end())))
        return true;
    if (!needAnyCardinal_) return false;
    for (Element element : PeriodicTable::Cardinals())
        if (generatedElements_.contains(element)) return false;
    return true;
}

} // namespace opusoptima::solvers::standard
