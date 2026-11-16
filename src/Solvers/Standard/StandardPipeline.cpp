#include "StandardPipeline.hpp"

// Ported from gtw123/OpusSolver@fdedd3f01a238b941ceba8bace7ea35cbd3b8c41 (MIT).
// The module map is documented in README.md.


#include <algorithm>
#include <set>

namespace opusoptima::solvers::standard {

namespace {

bool Contains(const std::vector<Element>& elements, Element element) {
    return std::find(elements.begin(), elements.end(), element) != elements.end();
}

} // namespace

Element ElementGenerator::RequestElement(const std::vector<Element>& possibleElements) {
    if (possibleElements.empty())
        throw std::invalid_argument("possibleElements must contain at least one item");

    Element generated = TryGenerateElement(possibleElements);
    while (!Contains(possibleElements, generated)) {
        if (!elementBuffer_)
            throw std::invalid_argument("requested an element the pipeline could not generate");
        for (ElementGenerator* generator = elementBuffer_->Parent();
             generator != this && generator != nullptr; generator = generator->Parent()) {
            if (generator->AlwaysRequiresPassthrough()) {
                commands_.Add(CommandType::PassThrough, generated, generator);
            }
        }
        elementBuffer_->StoreElement(generated);
        generated = TryGenerateElement(possibleElements);
    }
    return generated;
}

Element ElementGenerator::TryGenerateElement(const std::vector<Element>& possibleElements) {
    std::vector<Element> elementsToGenerate;
    for (Element element : possibleElements)
        if (CanGenerateElement(element)) elementsToGenerate.push_back(element);

    if (!pendingElements_.empty()) {
        if (!plan_.usePendingElementsInOrder) {
            for (std::size_t i = 0; i < pendingElements_.size(); ++i) {
                if (Contains(possibleElements, pendingElements_[i].first)) {
                    const auto pending = pendingElements_[i];
                    pendingElements_.erase(pendingElements_.begin() + static_cast<std::ptrdiff_t>(i));
                    commands_.Add(CommandType::Generate, pending.first, this, pending.second);
                    return pending.first;
                }
            }
        }
        if (plan_.usePendingElementsInOrder || !elementsToGenerate.empty()) {
            const auto pending = pendingElements_.front();
            pendingElements_.erase(pendingElements_.begin());
            commands_.Add(CommandType::Generate, pending.first, this, pending.second);
            return pending.first;
        }
    }

    if (elementBuffer_) {
        for (Element element : possibleElements) {
            if (elementBuffer_->CanRestoreElement(element)) {
                return elementBuffer_->RestoreElement(possibleElements);
            }
        }
    }

    if (!elementsToGenerate.empty()) return GenerateElement(elementsToGenerate);

    if (parent_) {
        const Element generated = parent_->RequestElement(possibleElements);
        PassThrough(generated);
        return generated;
    }

    throw std::invalid_argument("cannot find a generator for the requested element");
}

void ElementGenerator::EndSolution() {
    AddAllPendingElements();
    if (!elementBuffer_) return;
    while (HasPendingElements()) {
        elementBuffer_->StoreElement(RequestElement(PeriodicTable::AllElements()));
    }
}

// ── Multi-stack buffer ──────────────────────────────────────────────────────
MultiStackElementBuffer::BufferInfo MultiStackElementBuffer::GetBufferInfo() const {
    BufferInfo info;
    for (const Stack& stack : stacks_) {
        StackInfo stackInfo;
        stackInfo.multiAtom = stack.maxCount > 1;
        stackInfo.usesRestore = stack.usedPop;
        stackInfo.wastesAtoms = stack.HasWaste();
        stackInfo.elements = stack.all;
        info.stacks.push_back(std::move(stackInfo));
    }
    return info;
}

bool MultiStackElementBuffer::CanGenerateElement(Element element) {
    if (parent_ && parent_->HasPendingElement(element)) return false;
    return CanRestoreElement(element);
}

bool MultiStackElementBuffer::CanRestoreElement(Element element) {
    for (const Stack& stack : stacks_) {
        if (std::find(stack.current.begin(), stack.current.end(), element) != stack.current.end())
            return true;
    }
    return false;
}

Element MultiStackElementBuffer::GenerateElement(const std::vector<Element>& possibleElements) {
    for (Element element : possibleElements) {
        for (Stack& stack : stacks_) {
            if (!stack.current.empty() && stack.current.back() == element) {
                commands_.Add(CommandType::Generate, element, this, stack.id);
                stack.usedPop = true;
                if (!stack.all.empty()) stack.all.back().isWaste = false;
                stack.current.pop_back();
                return element;
            }
        }
    }
    throw std::invalid_argument("element is not present in the multi-stack buffer");
}

void MultiStackElementBuffer::StoreElement(Element element) {
    Stack* target = nullptr;
    for (Stack& stack : stacks_) {
        if (!stack.current.empty() && stack.current.back() == element) {
            target = &stack;
            break;
        }
    }
    if (!target) {
        for (Stack& stack : stacks_) {
            if (stack.current.empty()) {
                target = &stack;
                break;
            }
        }
    }
    if (!target) {
        Stack stack;
        stack.id = static_cast<int>(stacks_.size());
        stacks_.push_back(std::move(stack));
        target = &stacks_.back();
    }
    target->current.push_back(element);
    target->all.push_back(BufferedElement{element, true});
    target->maxCount = std::max(target->maxCount, static_cast<int>(target->current.size()));
    commands_.Add(CommandType::Consume, element, this, target->id);
}

// ── Input generator ─────────────────────────────────────────────────────────
Element ElementInput::GetNextElement(const std::vector<Element>& preferredElements) {
    if (!currentSequence_) currentSequence_ = originalSequence_;
    const Element element = (*currentSequence_)[static_cast<std::size_t>(currentIndex_)];
    ++currentIndex_;
    if (currentIndex_ >= static_cast<int>(currentSequence_->size())) {
        currentIndex_ = 0;
        currentSequence_.reset();
        plan_.GetRecipe().RecordReactionUsage(ReactionType::Reagent, molecule_.Id());
    }
    return element;
}

std::optional<int> ElementInput::FindClosestElement(const std::vector<Element>& elements) const {
    if (currentSequence_) {
        for (int i = currentIndex_; i < static_cast<int>(currentSequence_->size()); ++i) {
            if (Contains(elements, (*currentSequence_)[static_cast<std::size_t>(i)]))
                return i - currentIndex_;
        }
    }
    for (std::size_t i = 0; i < originalSequence_.size(); ++i) {
        if (Contains(elements, originalSequence_[i])) return static_cast<int>(i);
    }
    return std::nullopt;
}

ElementInput& InputGenerator::ChooseInput(const std::vector<Element>& possibleElements) {
    ElementInput* best = nullptr;
    int bestDistance = 0;
    for (ElementInput& input : inputs_) {
        if (!plan_.GetRecipe().HasAvailableReactions(ReactionType::Reagent,
                                                     input.GetMolecule().Id()))
            continue;
        const auto distance = input.FindClosestElement(possibleElements);
        if (!distance) continue;
        if (!best || *distance < bestDistance) {
            best = &input;
            bestDistance = *distance;
        }
    }
    if (!best) throw std::invalid_argument("cannot find a suitable input");
    return *best;
}

Element InputGenerator::GenerateElement(const std::vector<Element>& possibleElements) {
    ElementInput& input = ChooseInput(possibleElements);
    const Element generated = input.GetNextElement(possibleElements);
    commands_.Add(CommandType::Generate, generated, this, input.GetMolecule().Id());
    return generated;
}

void InputGenerator::AddAllPendingElements() {
    for (ElementInput& input : inputs_) {
        while (input.HasPendingElements()) {
            AddPendingElement(input.GetNextElement(PeriodicTable::AllElements()),
                              input.GetMolecule().Id());
        }
    }
}

// ── Output generator ────────────────────────────────────────────────────────
void OutputGenerator::GenerateCommandSequence() {
    const auto& products = plan_.GetPuzzle().products;
    std::vector<const Molecule*> order;
    for (const Molecule& product : products) order.push_back(&product);
    if (plan_.reverseProductBuildOrder) std::reverse(order.begin(), order.end());
    for (const Molecule* product : order) {
        const auto info = plan_.GetProductElementInfo(*product);
        const auto usages = plan_.GetRecipe().GetAvailableReactions(ReactionType::Product,
                                                                    product->Id());
        if (usages.empty())
            throw std::invalid_argument("product has no available recipe usage");
        const int numCopies = usages.front()->MaxUsages();
        for (int copy = 0; copy < numCopies; ++copy) {
            for (Element element : info.elementOrder) {
                commands_.Add(CommandType::Consume, parent_->RequestElement(element), this,
                              product->Id());
            }
        }
    }
}

// ── Metal generators ────────────────────────────────────────────────────────
std::vector<Element> MetalGenerator::GetAvailableSourceElementsForTarget(Element target) const {
    std::vector<Element> sources;
    int source = ElemIndex(target) - 1;
    while (source >= ElemIndex(PeriodicTable::Metals().front())) {
        const Element element = static_cast<Element>(source);
        if (!plan_.GetRecipe().HasAvailableReactions(GetReactionType(), std::nullopt, element))
            break;
        sources.push_back(element);
        --source;
    }
    return sources;
}

bool MetalGenerator::CanGenerateElement(Element element) {
    return !GetAvailableSourceElementsForTarget(element).empty();
}

Element MetalGenerator::GenerateElement(const std::vector<Element>& possibleElements) {
    if (possibleElements.size() > 1)
        throw std::invalid_argument("metal generator only supports one target element");
    const Element target = possibleElements.front();
    const auto requested = GetAvailableSourceElementsForTarget(target);
    const Element received = parent_->RequestElement(requested);
    if (received != target)
        GenerateMetal(received, target);
    else
        PassThrough(received);
    return target;
}

void MetalProjectorGenerator::GenerateMetal(Element firstMetal, Element targetMetal) {
    const int diff = PeriodicTable::GetMetalDifference(firstMetal, targetMetal);
    if (diff < 0) throw std::invalid_argument("projection cannot lower a metal");
    commands_.Add(CommandType::Consume, firstMetal, this);
    for (int i = 0; i < diff; ++i) {
        commands_.Add(CommandType::Consume, parent_->RequestElement(Element::Quicksilver), this);
        const int metal = ElemIndex(firstMetal) - ElemIndex(PeriodicTable::Metals().front()) + i;
        plan_.GetRecipe().RecordReactionUsage(
            ReactionType::Projection, std::nullopt,
            PeriodicTable::Metals().at(static_cast<std::size_t>(metal)));
    }
    commands_.Add(CommandType::Generate, targetMetal, this);
}

void MetalPurifierGenerator::GenerateMetal(Element sourceMetal, Element targetMetal) {
    PurificationSequence sequence;
    sequence.id = static_cast<int>(sequences_.size());
    sequence.targetMetal = targetMetal;
    sequence.lowestMetalUsed = sourceMetal;
    sequences_.push_back(sequence);

    commands_.Add(CommandType::Consume, sourceMetal, this, sequence.id);
    int currentPurity = PeriodicTable::GetMetalPurity(sourceMetal);
    const int targetPurity = PeriodicTable::GetMetalPurity(targetMetal);
    while (currentPurity < targetPurity) {
        const auto allowable = PeriodicTable::GetMetalsWithPuritySameOrLower(targetPurity -
                                                                             currentPurity);
        const auto sources = GetAvailableSourceElementsForTarget(targetMetal);
        std::vector<Element> requested;
        for (Element element : allowable)
            if (std::find(sources.begin(), sources.end(), element) != sources.end())
                requested.push_back(element);
        const Element received = parent_->RequestElement(requested);
        sequences_.back().lowestMetalUsed =
            PeriodicTable::GetLowestMetal(sequences_.back().lowestMetalUsed, received);
        commands_.Add(CommandType::Consume, received, this, sequence.id);

        const int newPurity = currentPurity + PeriodicTable::GetMetalPurity(received);
        for (int value = ElemIndex(received); value < ElemIndex(targetMetal); ++value) {
            const int metalValue = PeriodicTable::GetMetalPurity(static_cast<Element>(value));
            if ((currentPurity & metalValue) != 0 && (newPurity & metalValue) == 0) {
                plan_.GetRecipe().RecordReactionUsage(
                    ReactionType::Purification, std::nullopt, static_cast<Element>(value));
            }
        }
        currentPurity = newPurity;
    }
    commands_.Add(CommandType::Generate, targetMetal, this, sequence.id);
}

// ── Other generators ────────────────────────────────────────────────────────
bool SaltGenerator::CanGenerateElement(Element element) {
    return plan_.GetRecipe().HasAvailableReactions(ReactionType::Calcification, std::nullopt,
                                                   std::nullopt, element);
}

bool SaltGenerator::RequiresCardinalPassThrough() const {
    for (const Command& command : commands_.Commands()) {
        if (command.target != this || command.type != CommandType::PassThrough) continue;
        if (IsCardinal(command.element)) return true;
    }
    return false;
}

Element SaltGenerator::GenerateElement(const std::vector<Element>& possibleElements) {
    (void)possibleElements;
    std::vector<Element> allowedInputs{Element::Salt};
    for (const ReactionUsage* usage :
         plan_.GetRecipe().GetAvailableReactions(ReactionType::Calcification)) {
        for (const auto& [element, count] : usage->Get().inputs) {
            (void)count;
            if (std::find(allowedInputs.begin(), allowedInputs.end(), element) ==
                allowedInputs.end())
                allowedInputs.push_back(element);
        }
    }
    const Element received = parent_->RequestElement(allowedInputs);
    if (received != Element::Salt) {
        commands_.Add(CommandType::Consume, received, this);
        commands_.Add(CommandType::Generate, Element::Salt, this);
        plan_.GetRecipe().RecordReactionUsage(ReactionType::Calcification, std::nullopt,
                                              received);
    } else {
        PassThrough(received);
    }
    return Element::Salt;
}

bool MorsVitaeGenerator::CanGenerateElement(Element element) {
    return plan_.GetRecipe().HasAvailableReactions(ReactionType::Animismus, std::nullopt,
                                                   std::nullopt, element);
}

Element MorsVitaeGenerator::GenerateElement(const std::vector<Element>& possibleElements) {
    commands_.Add(CommandType::Consume, parent_->RequestElement(Element::Salt), this);
    commands_.Add(CommandType::Consume, parent_->RequestElement(Element::Salt), this);
    const Element first = possibleElements.front();
    const Element second = first == Element::Mors ? Element::Vitae : Element::Mors;
    commands_.Add(CommandType::Generate, first, this);
    plan_.GetRecipe().RecordReactionUsage(ReactionType::Animismus);
    AddPendingElement(second);
    return first;
}

bool QuintessenceGenerator::CanGenerateElement(Element element) {
    return plan_.GetRecipe().HasAvailableReactions(ReactionType::Unification, std::nullopt,
                                                   std::nullopt, element);
}

Element QuintessenceGenerator::GenerateElement(const std::vector<Element>&) {
    std::vector<Element> inputs(PeriodicTable::Cardinals().begin(),
                                PeriodicTable::Cardinals().end());
    while (!inputs.empty()) {
        const Element element = parent_->RequestElement(inputs);
        commands_.Add(CommandType::Consume, element, this);
        inputs.erase(std::remove(inputs.begin(), inputs.end(), element), inputs.end());
    }
    commands_.Add(CommandType::Generate, Element::Quintessence, this);
    plan_.GetRecipe().RecordReactionUsage(ReactionType::Unification);
    return Element::Quintessence;
}

bool QuintessenceDisperserGenerator::CanGenerateElement(Element element) {
    return plan_.GetRecipe().HasAvailableReactions(ReactionType::Dispersion, std::nullopt,
                                                   std::nullopt, element);
}

Element QuintessenceDisperserGenerator::GenerateElement(
    const std::vector<Element>& possibleElements) {
    commands_.Add(CommandType::Consume, parent_->RequestElement(Element::Quintessence), this);
    Element generated{};
    if (plan_.usePendingElementsInOrder) {
        generated = Element::Earth;
        AddPendingElement(Element::Air);
        AddPendingElement(Element::Water);
        AddPendingElement(Element::Fire);
    } else {
        generated = possibleElements.front();
        for (Element cardinal : PeriodicTable::Cardinals())
            if (cardinal != generated) AddPendingElement(cardinal);
    }
    commands_.Add(CommandType::Generate, generated, this);
    plan_.GetRecipe().RecordReactionUsage(ReactionType::Dispersion);
    return generated;
}

bool VanBerloGenerator::CanGenerateElement(Element element) {
    return plan_.GetRecipe().HasAvailableReactions(ReactionType::VanBerlo, std::nullopt,
                                                   std::nullopt, element);
}

Element VanBerloGenerator::GenerateElement(const std::vector<Element>& possibleElements) {
    std::vector<Element> requested = possibleElements;
    requested.push_back(Element::Salt);
    const Element generated = parent_->RequestElement(requested);
    if (generated == Element::Salt) {
        const Element element = possibleElements.front();
        commands_.Add(CommandType::Consume, Element::Salt, this);
        commands_.Add(CommandType::Generate, element, this);
        plan_.GetRecipe().RecordReactionUsage(ReactionType::VanBerlo, std::nullopt, std::nullopt,
                                              element);
        return element;
    }
    PassThrough(generated);
    return generated;
}

// ── Pipeline ────────────────────────────────────────────────────────────────
ElementPipeline::ElementPipeline(SolutionPlan& plan) : plan_(plan) {
    const Recipe& recipe = plan_.GetRecipe();

    auto addWithBuffer = [&](std::unique_ptr<ElementGenerator> generator) {
        auto buffer = std::make_unique<MultiStackElementBuffer>(commands_, plan_);
        AddGenerator(std::move(generator), std::move(buffer));
    };

    addWithBuffer(std::make_unique<InputGenerator>(commands_, plan_, nullptr));
    if (recipe.HasAvailableReactions(ReactionType::Purification))
        AddGenerator(std::make_unique<MetalPurifierGenerator>(commands_, plan_));
    if (recipe.HasAvailableReactions(ReactionType::Projection))
        AddGenerator(std::make_unique<MetalProjectorGenerator>(commands_, plan_));
    if (recipe.HasAvailableReactions(ReactionType::Dispersion))
        addWithBuffer(std::make_unique<QuintessenceDisperserGenerator>(commands_, plan_));
    if (recipe.HasAvailableReactions(ReactionType::Calcification))
        AddGenerator(std::make_unique<SaltGenerator>(commands_, plan_));
    if (recipe.HasAvailableReactions(ReactionType::VanBerlo))
        AddGenerator(std::make_unique<VanBerloGenerator>(commands_, plan_));
    if (recipe.HasAvailableReactions(ReactionType::Animismus))
        addWithBuffer(std::make_unique<MorsVitaeGenerator>(commands_, plan_));
    if (recipe.HasAvailableReactions(ReactionType::Unification))
        AddGenerator(std::make_unique<QuintessenceGenerator>(commands_, plan_));

    auto output = std::make_unique<OutputGenerator>(commands_, plan_);
    outputGenerator_ = output.get();
    AddGenerator(std::move(output));
}

void ElementPipeline::AddGenerator(std::unique_ptr<ElementGenerator> generator,
                                   std::unique_ptr<ElementBuffer> buffer) {
    generator->SetParent(generators_.empty() ? nullptr : generators_.back());
    ElementGenerator* raw = generator.get();
    if (buffer) {
        buffer->SetParent(raw);
        raw->SetElementBuffer(buffer.get());
    }
    generators_.push_back(raw);
    owned_.push_back(std::move(generator));
    if (buffer) {
        generators_.push_back(buffer.get());
        ownedBuffers_.push_back(std::move(buffer));
    }
}

CommandSequence ElementPipeline::GenerateCommandSequence() {
    outputGenerator_->GenerateCommandSequence();
    for (ElementGenerator* generator : generators_) generator->EndSolution();
    return commands_;
}

} // namespace opusoptima::solvers::standard
