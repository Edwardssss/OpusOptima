#pragma once

// Ported from gtw123/OpusSolver@fdedd3f01a238b941ceba8bace7ea35cbd3b8c41 (MIT).
// See docs/analysis/UPSTREAM_STANDARD_PORT_20261115.md for the module map.

// C++ port of upstream OpusSolver's logical element pipeline
// (gtw123/OpusSolver@fdedd3f, MIT): the dry run that turns product element
// requests into a geometry-free Consume/Generate/PassThrough command sequence.

#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

#include "StandardModel.hpp"
#include "StandardRecipe.hpp"

namespace opusoptima::solvers::standard {

class AtomGenerator;
class ElementGenerator;

enum class CommandType { Consume, Generate, PassThrough };

struct Command {
    CommandType type{};
    Element element{};
    ElementGenerator* target{};
    int id{};
};

class CommandSequence {
    public:
    void Add(CommandType type, Element element, ElementGenerator* target, int id = 0) {
        commands_.push_back(Command{type, element, target, id});
    }
    [[nodiscard]] const std::vector<Command>& Commands() const { return commands_; }

    private:
    std::vector<Command> commands_;
};

struct MoleculeElementInfo {
    std::vector<Element> elementOrder;
};

class SolutionPlan {
    public:
    SolutionPlan(const Puzzle& puzzle, Recipe& recipe,
                 std::vector<const Molecule*> requiredReagents,
                 std::map<int, MoleculeElementInfo> reagentElementInfo,
                 std::map<int, MoleculeElementInfo> productElementInfo)
        : puzzle_(puzzle), recipe_(recipe), requiredReagents_(std::move(requiredReagents)),
          reagentElementInfo_(std::move(reagentElementInfo)),
          productElementInfo_(std::move(productElementInfo)) {}

    [[nodiscard]] const Puzzle& GetPuzzle() const { return puzzle_; }
    [[nodiscard]] Recipe& GetRecipe() { return recipe_; }
    [[nodiscard]] const Recipe& GetRecipe() const { return recipe_; }
    [[nodiscard]] const std::vector<const Molecule*>& RequiredReagents() const {
        return requiredReagents_;
    }
    [[nodiscard]] MoleculeElementInfo GetReagentElementInfo(const Molecule& molecule) const {
        return reagentElementInfo_.at(molecule.Id());
    }
    [[nodiscard]] MoleculeElementInfo GetProductElementInfo(const Molecule& molecule) const {
        return productElementInfo_.at(molecule.Id());
    }

    bool usePendingElementsInOrder{true};
    /// Reverse product build order (upstream solution parameter).
    bool reverseProductBuildOrder{};

    private:
    const Puzzle& puzzle_;
    Recipe& recipe_;
    std::vector<const Molecule*> requiredReagents_;
    std::map<int, MoleculeElementInfo> reagentElementInfo_;
    std::map<int, MoleculeElementInfo> productElementInfo_;
};

class ElementBuffer;

class ElementGenerator {
    public:
    ElementGenerator(CommandSequence& commands, SolutionPlan& plan, ElementBuffer* buffer = nullptr)
        : commands_(commands), plan_(plan), elementBuffer_(buffer) {}
    virtual ~ElementGenerator() = default;

    ElementGenerator* Parent() const { return parent_; }
    void SetParent(ElementGenerator* parent) { parent_ = parent; }
    AtomGenerator* Atom() const { return atomGenerator_; }
    void SetAtom(AtomGenerator* atom) { atomGenerator_ = atom; }
    void SetElementBuffer(ElementBuffer* buffer) { elementBuffer_ = buffer; }

    [[nodiscard]] bool HasPendingElement(Element element) const {
        for (const auto& [pending, id] : pendingElements_)
            if (pending == element) return true;
        return false;
    }
    [[nodiscard]] bool HasPendingElements() const { return !pendingElements_.empty(); }

    [[nodiscard]] Element RequestElement(Element element) {
        return RequestElement(std::vector<Element>{element});
    }
    [[nodiscard]] Element RequestElement(const std::vector<Element>& possibleElements);
    [[nodiscard]] virtual bool AlwaysRequiresPassthrough() const { return false; }
    void EndSolution();

    protected:
    [[nodiscard]] virtual bool CanGenerateElement(Element element) = 0;
    [[nodiscard]] virtual Element GenerateElement(const std::vector<Element>& possibleElements) = 0;
    void PassThrough(Element element) {
        commands_.Add(CommandType::PassThrough, element, this);
    }
    void AddPendingElement(Element element, int id = 0) {
        pendingElements_.push_back({element, id});
    }
    virtual void AddAllPendingElements() {}

    CommandSequence& commands_;
    SolutionPlan& plan_;
    ElementBuffer* elementBuffer_{};
    ElementGenerator* parent_{};
    AtomGenerator* atomGenerator_{};

    private:
    [[nodiscard]] Element TryGenerateElement(const std::vector<Element>& possibleElements);
    std::vector<std::pair<Element, int>> pendingElements_;
};

class ElementBuffer : public ElementGenerator {
    public:
    using ElementGenerator::ElementGenerator;
    virtual bool CanRestoreElement(Element element) = 0;
    virtual void StoreElement(Element element) = 0;
    [[nodiscard]] Element RestoreElement(const std::vector<Element>& possibleElements) {
        return GenerateElement(possibleElements);
    }
};

class MultiStackElementBuffer : public ElementBuffer {
    public:
    struct BufferedElement {
        Element element{};
        bool isWaste{true};
    };
    struct StackInfo {
        bool multiAtom{};
        bool usesRestore{};
        bool wastesAtoms{};
        std::vector<BufferedElement> elements;
    };
    struct BufferInfo {
        std::vector<StackInfo> stacks;
    };

    MultiStackElementBuffer(CommandSequence& commands, SolutionPlan& plan)
        : ElementBuffer(commands, plan) {}

    [[nodiscard]] BufferInfo GetBufferInfo() const;
    [[nodiscard]] bool CanRestoreElement(Element element) override;
    void StoreElement(Element element) override;

    protected:
    [[nodiscard]] bool CanGenerateElement(Element element) override;
    [[nodiscard]] Element GenerateElement(const std::vector<Element>& possibleElements) override;

    private:
    struct Stack {
        int id{};
        std::vector<Element> current;
        std::vector<BufferedElement> all;
        int maxCount{};
        bool usedPop{};
        [[nodiscard]] bool HasWaste() const { return !current.empty(); }
    };
    std::vector<Stack> stacks_;
};


class ElementInput {
    public:
    ElementInput(const Molecule& molecule, SolutionPlan& plan)
        : molecule_(molecule), plan_(plan) {
        originalSequence_ = plan.GetReagentElementInfo(molecule).elementOrder;
    }

    [[nodiscard]] const Molecule& GetMolecule() const { return molecule_; }
    [[nodiscard]] bool HasPendingElements() const { return currentIndex_ > 0; }
    Element GetNextElement(const std::vector<Element>& preferredElements);
    [[nodiscard]] std::optional<int> FindClosestElement(const std::vector<Element>& elements) const;

    private:
    const Molecule& molecule_;
    SolutionPlan& plan_;
    std::vector<Element> originalSequence_;
    std::optional<std::vector<Element>> currentSequence_;
    int currentIndex_{};
};

class InputGenerator : public ElementGenerator {
    public:
    InputGenerator(CommandSequence& commands, SolutionPlan& plan, ElementBuffer* buffer)
        : ElementGenerator(commands, plan, buffer) {
        for (const Molecule* reagent : plan.RequiredReagents()) inputs_.emplace_back(*reagent, plan);
    }
    [[nodiscard]] const std::vector<ElementInput>& Inputs() const { return inputs_; }

    protected:
    [[nodiscard]] bool CanGenerateElement(Element) override { return true; }
    [[nodiscard]] Element GenerateElement(const std::vector<Element>& possibleElements) override;
    void AddAllPendingElements() override;

    private:
    ElementInput& ChooseInput(const std::vector<Element>& possibleElements);
    std::vector<ElementInput> inputs_;
};

class OutputGenerator : public ElementGenerator {
    public:
    OutputGenerator(CommandSequence& commands, SolutionPlan& plan)
        : ElementGenerator(commands, plan) {}
    [[nodiscard]] bool CanGenerateElement(Element) override { return false; }
    [[nodiscard]] std::vector<const Molecule*> ProductPointers() const {
        std::vector<const Molecule*> products;
        for (const Molecule& product : plan_.GetPuzzle().products) products.push_back(&product);
        return products;
    }
    void GenerateCommandSequence();

    protected:
    [[nodiscard]] Element GenerateElement(const std::vector<Element>&) override {
        throw std::invalid_argument("OutputGenerator cannot generate an element");
    }
};

class MetalGenerator : public ElementGenerator {
    public:
    using ElementGenerator::ElementGenerator;

    protected:
    [[nodiscard]] virtual ReactionType GetReactionType() const = 0;
    [[nodiscard]] std::vector<Element> GetAvailableSourceElementsForTarget(Element target) const;
    [[nodiscard]] bool CanGenerateElement(Element element) override;
    [[nodiscard]] Element GenerateElement(const std::vector<Element>& possibleElements) override;
    virtual void GenerateMetal(Element firstMetal, Element targetMetal) = 0;
};

class MetalProjectorGenerator : public MetalGenerator {
    public:
    using MetalGenerator::MetalGenerator;

    protected:
    [[nodiscard]] ReactionType GetReactionType() const override {
        return ReactionType::Projection;
    }
    void GenerateMetal(Element firstMetal, Element targetMetal) override;
};

class MetalPurifierGenerator : public MetalGenerator {
    public:
    struct PurificationSequence {
        int id{};
        Element targetMetal{};
        Element lowestMetalUsed{};
    };
    using MetalGenerator::MetalGenerator;
    [[nodiscard]] const std::vector<PurificationSequence>& Sequences() const {
        return sequences_;
    }

    protected:
    [[nodiscard]] ReactionType GetReactionType() const override {
        return ReactionType::Purification;
    }
    void GenerateMetal(Element sourceMetal, Element targetMetal) override;

    private:
    std::vector<PurificationSequence> sequences_;
};

class SaltGenerator : public ElementGenerator {
    public:
    using ElementGenerator::ElementGenerator;
    [[nodiscard]] bool RequiresCardinalPassThrough() const;

    protected:
    [[nodiscard]] bool CanGenerateElement(Element element) override;
    [[nodiscard]] Element GenerateElement(const std::vector<Element>& possibleElements) override;
};

class MorsVitaeGenerator : public ElementGenerator {
    public:
    using ElementGenerator::ElementGenerator;

    protected:
    [[nodiscard]] bool CanGenerateElement(Element element) override;
    [[nodiscard]] Element GenerateElement(const std::vector<Element>& possibleElements) override;
};

class QuintessenceGenerator : public ElementGenerator {
    public:
    using ElementGenerator::ElementGenerator;

    protected:
    [[nodiscard]] bool CanGenerateElement(Element element) override;
    [[nodiscard]] Element GenerateElement(const std::vector<Element>& possibleElements) override;
};

class QuintessenceDisperserGenerator : public ElementGenerator {
    public:
    using ElementGenerator::ElementGenerator;

    protected:
    [[nodiscard]] bool CanGenerateElement(Element element) override;
    [[nodiscard]] Element GenerateElement(const std::vector<Element>& possibleElements) override;
};

class VanBerloGenerator : public ElementGenerator {
    public:
    using ElementGenerator::ElementGenerator;
    [[nodiscard]] bool AlwaysRequiresPassthrough() const override { return true; }

    protected:
    [[nodiscard]] bool CanGenerateElement(Element element) override;
    [[nodiscard]] Element GenerateElement(const std::vector<Element>& possibleElements) override;
};

class ElementPipeline {
    public:
    explicit ElementPipeline(SolutionPlan& plan);
    [[nodiscard]] CommandSequence GenerateCommandSequence();
    [[nodiscard]] const std::vector<ElementGenerator*>& Generators() const { return generators_; }

    private:
    void AddGenerator(std::unique_ptr<ElementGenerator> generator,
                      std::unique_ptr<ElementBuffer> buffer = nullptr);

    SolutionPlan& plan_;
    CommandSequence commands_;
    std::vector<std::unique_ptr<ElementGenerator>> owned_;
    std::vector<std::unique_ptr<ElementBuffer>> ownedBuffers_;
    std::vector<ElementGenerator*> generators_;
    OutputGenerator* outputGenerator_{};
};

} // namespace opusoptima::solvers::standard
