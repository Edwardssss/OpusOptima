#pragma once

// Ported from gtw123/OpusSolver@fdedd3f01a238b941ceba8bace7ea35cbd3b8c41 (MIT).
// See docs/analysis/UPSTREAM_STANDARD_PORT_20261115.md for the module map.

// C++ port of upstream OpusSolver's Standard module library
// (gtw123/OpusSolver@fdedd3f, MIT): input areas and disassemblers, the atom
// conveyor, the element generators, and the universal output assembler. Every
// module writes its own per-arm fragments; ProgramBuilder aligns them.

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <vector>

#include "StandardModel.hpp"
#include "StandardPipeline.hpp"
#include "StandardProgram.hpp"

namespace opusoptima::solvers::standard {

class SolverComponent : public GameObject {
    public:
    SolverComponent(GameObject* parent, ProgramWriter& writer, Vec2 position, Rot rotation)
        : GameObject(parent, {position, rotation}), writer(writer) {}
    [[nodiscard]] virtual Vec2 OutputPosition() const { return {0, 0}; }

    protected:
    ProgramWriter& writer;
};

class AtomGenerator : public SolverComponent {
    public:
    AtomGenerator(GameObject* parent, ProgramWriter& writer)
        : SolverComponent(parent, writer, {0, 0}, R0) {}

    [[nodiscard]] Vec2 OutputPosition() const override;
    virtual void BeginSolution() {}
    virtual void Consume(Element element, int id) {
        (void)element;
        (void)id;
    }
    virtual void Generate(Element element, int id) {
        (void)element;
        (void)id;
    }
    virtual void PassThrough(Element element) { (void)element; }
    virtual void EndSolution() {}
    virtual void OptimizeParts() {}

    protected:
    Arm* outputArm_{};
};

// ── Input modules ───────────────────────────────────────────────────────────
class MoleculeDisassembler : public SolverComponent {
    public:
    MoleculeDisassembler(GameObject* parent, ProgramWriter& writer, Vec2 position,
                         const Molecule& molecule)
        : SolverComponent(parent, writer, position, R0), molecule(&molecule) {}
    [[nodiscard]] virtual int Height() const = 0;
    [[nodiscard]] virtual int HeightBelowOrigin() const { return 0; }
    virtual void GenerateNextAtom() = 0;
    const Molecule* molecule{};
};

class MonoDisassembler : public MoleculeDisassembler {
    public:
    MonoDisassembler(GameObject* parent, ProgramWriter& writer, Vec2 position,
                     const Molecule& molecule, Rot direction, Instr instruction);
    MonoDisassembler(GameObject* parent, ProgramWriter& writer, Vec2 position,
                     const Molecule& molecule, Vec2 armPosition, Rot armRotation, ArmType armType,
                     int armExtension, Instr instruction);
    [[nodiscard]] int Height() const override { return 1; }
    void GenerateNextAtom() override;

    private:
    void CreateObjects(const Molecule& molecule, Rot direction, Instr instruction);
    Arm* outputArm_{};
    Instr instruction_{};
};

class SingleMonoDisassembler : public MoleculeDisassembler {
    public:
    SingleMonoDisassembler(GameObject* parent, ProgramWriter& writer, Vec2 position,
                           const Molecule& molecule);
    [[nodiscard]] int Height() const override { return 1; }
    void GenerateNextAtom() override;
};

class LinearDisassemblerModule : public MoleculeDisassembler {
    public:
    LinearDisassemblerModule(GameObject* parent, ProgramWriter& writer, Vec2 position,
                             const Molecule& molecule);
    [[nodiscard]] int Height() const override { return 4; }
    void GenerateNextAtom() override;

    private:
    Arm* grabArm_{};
    Arm* outputArm_{};
    int step_{};
};

class UniversalDisassemblerModule : public MoleculeDisassembler {
    public:
    UniversalDisassemblerModule(GameObject* parent, ProgramWriter& writer, Vec2 position,
                                const Molecule& molecule);
    [[nodiscard]] int Height() const override { return molecule->Height() + 4; }
    [[nodiscard]] int HeightBelowOrigin() const override { return molecule->Height() - 1; }
    void GenerateNextAtom() override;

    private:
    [[nodiscard]] bool IsProblematicMolecule() const;
    void AddArms(Vec2 reagentPosition);
    void AddTracks(Vec2 armPosition);
    void AddGlyphs();
    bool NextAtom();
    void ResetCoroutine();

    int unbondWidth_{};
    Arm* grabArm_{};
    std::vector<Arm*> lowerUnbondArms_;
    std::vector<Arm*> upperUnbondArms_;
    std::vector<Arm*> moveArms_;
    bool addVerticalOffsetAtStart_{};
    // Coroutine state: row y and column x being processed.
    int row_{};
    int column_{};
    int rowFirstAtomX_{};
    int phase_{};
    bool coroutineStarted_{};
};

class SimpleInputAreaModule : public AtomGenerator {
    public:
    static constexpr int kMaxReagents = 4;
    SimpleInputAreaModule(GameObject* parent, ProgramWriter& writer,
                          const std::vector<const Molecule*>& reagents);
    void Generate(Element element, int id) override;

    private:
    void CreateDisassemblers(const std::vector<const Molecule*>& reagents);
    std::vector<MoleculeDisassembler*> disassemblers_;
};

class ComplexInputAreaModule : public AtomGenerator {
    public:
    ComplexInputAreaModule(GameObject* parent, ProgramWriter& writer,
                           const std::vector<const Molecule*>& reagents);
    [[nodiscard]] Vec2 OutputPosition() const override;
    void Generate(Element element, int id) override;

    private:
    void AddMultiAtomDisassemblers(const std::vector<const Molecule*>& reagents);
    void AddSingleAtomDisassemblers(const std::vector<const Molecule*>& reagents);
    std::vector<MoleculeDisassembler*> disassemblers_;
    class AtomConveyorModule* conveyor_{};
};

class AtomConveyorModule : public SolverComponent {
    public:
    AtomConveyorModule(GameObject* parent, ProgramWriter& writer, Vec2 position, int height);
    [[nodiscard]] Vec2 OutputPosition() const override { return {4, 0}; }
    void MoveAtom(int startY);

    private:
    std::vector<Arm*> downArms_;
    Arm* cornerArm_{};
    std::vector<Arm*> acrossArms_;
};

// ── Element atom generators ─────────────────────────────────────────────────
class SaltAtomGenerator : public AtomGenerator {
    public:
    SaltAtomGenerator(GameObject* parent, ProgramWriter& writer);
    void Generate(Element element, int id) override;
    void PassThrough(Element element) override;

    private:
    Arm* bigArm_{};
    Arm* smallArm_{};
};

class SaltNoPassThroughAtomGenerator : public AtomGenerator {
    public:
    SaltNoPassThroughAtomGenerator(GameObject* parent, ProgramWriter& writer);
    void Generate(Element element, int id) override;
    void PassThrough(Element element) override;
};

class ProjectorAtomGenerator : public AtomGenerator {
    public:
    ProjectorAtomGenerator(GameObject* parent, ProgramWriter& writer);
    void Consume(Element element, int id) override;
    void Generate(Element element, int id) override;
    void PassThrough(Element element) override;

    private:
    Arm* projectionArm_{};
    bool hasMetal_{};
};

class PurifierAtomGenerator : public AtomGenerator {
    public:
    PurifierAtomGenerator(GameObject* parent, ProgramWriter& writer,
                          const std::vector<MetalPurifierGenerator::PurificationSequence>& sequences);
    void Consume(Element element, int id) override;
    void Generate(Element element, int id) override;
    void PassThrough(Element element) override;

    private:
    struct Purifier {
        Arm* bigArm{};
        Arm* smallArm{};
        std::optional<Element> currentElement;
    };
    void CreateObjects();
    void PassThroughAtom(int firstPurifier);
    std::vector<MetalPurifierGenerator::PurificationSequence> sequences_;
    int size_{};
    std::vector<Purifier> purifiers_;
};

class MorsVitaeAtomGenerator : public AtomGenerator {
    public:
    MorsVitaeAtomGenerator(GameObject* parent, ProgramWriter& writer);
    void Consume(Element element, int id) override;
    void Generate(Element element, int id) override;
    void PassThrough(Element element) override;

    private:
    void MoveToOutput(Element element);
    Arm* inputArm_{};
    Arm* saltArm_{};
    Arm* vitaeArm_{};
    Arm* morsArm_{};
    int consumeStep_{};
    int generateStep_{};
    Element generatingElement_{};
};

class QuintessenceAtomGenerator : public AtomGenerator {
    public:
    QuintessenceAtomGenerator(GameObject* parent, ProgramWriter& writer);
    void Consume(Element element, int id) override;
    void Generate(Element element, int id) override;
    void PassThrough(Element element) override;

    private:
    Arm* leftArm_{};
    Arm* rightArm_{};
    int consumeStep_{};
};

class DisperserAtomGenerator : public AtomGenerator {
    public:
    DisperserAtomGenerator(GameObject* parent, ProgramWriter& writer);
    void Consume(Element element, int id) override;
    void Generate(Element element, int id) override;
    void PassThrough(Element element) override;

    private:
    Arm* inputArm_{};
    Arm* disperseArm_{};
    Arm* airFireArm_{};
    Arm* waterArm_{};
    int generateStep_{};
};

class VanBerloController {
    public:
    VanBerloController(ProgramWriter& writer, Arm* wheelArm)
        : writer_(writer), wheelArm_(wheelArm) {}
    void RotateToElement(Element element);
    void Reset(bool asEarlyAsPossible = true);

    private:
    ProgramWriter& writer_;
    Arm* wheelArm_{};
    bool isFirstAtom_{true};
    Rot currentWheelRotation_{};
};

class VanBerloAtomGenerator : public AtomGenerator {
    public:
    VanBerloAtomGenerator(GameObject* parent, ProgramWriter& writer);
    void Generate(Element element, int id) override;
    void PassThrough(Element element) override;
    void EndSolution() override;

    private:
    Arm* wheelArm_{};
    std::unique_ptr<VanBerloController> controller_;
};

class BufferAtomGenerator : public AtomGenerator {
    public:
    BufferAtomGenerator(GameObject* parent, ProgramWriter& writer,
                        MultiStackElementBuffer::BufferInfo info);
    void Consume(Element element, int id) override;
    void Generate(Element element, int id) override;
    void PassThrough(Element element) override;

    private:
    struct Stack {
        MultiStackElementBuffer::StackInfo info;
        int index{};
        Arm* pushArm{};
        Arm* popArm{};
        Arm* outputArm{};
        int atomCount{};
    };
    void AddStack(int index, const MultiStackElementBuffer::StackInfo& info);
    void MoveAtomThroughStacks(int startIndex, int endIndex);
    std::vector<Stack> stacks_;
};

// ── Output modules ──────────────────────────────────────────────────────────
class MoleculeAssembler : public SolverComponent {
    public:
    MoleculeAssembler(GameObject* parent, ProgramWriter& writer, Vec2 position)
        : SolverComponent(parent, writer, position, R0) {}
    virtual void AddAtom(Element element, int productID) = 0;
    virtual void OptimizeParts() {}
};

class BondProgrammer {
    public:
    BondProgrammer(class AssemblyArea* assemblyArea, const Molecule& molecule, int row)
        : assemblyArea_(assemblyArea), molecule_(molecule), row_(row) {}
    void Generate();
    [[nodiscard]] const std::vector<Instr>& Instructions() const { return instructions_; }
    [[nodiscard]] const std::vector<Instr>& ReturnInstructions() const { return returnInstructions_; }

    private:
    void AddBonds();
    void MoveThroughBonder(GlyphType type, Rot direction, int count,
                           const std::function<bool(const Atom&)>& shouldBondAtom);
    void Optimize();
    void Add(Instr instruction) { instructions_.push_back(instruction); }
    void Repeat(Instr instruction, int count) {
        instructions_.insert(instructions_.end(), static_cast<std::size_t>(count), instruction);
    }
    [[nodiscard]] bool IsUnbondedFirePair(const Atom& atom, Rot direction) const;
    [[nodiscard]] std::optional<int> LeftmostTriplexBondPosition() const;

    AssemblyArea* assemblyArea_{};
    const Molecule& molecule_;
    int row_{};
    std::vector<Instr> instructions_;
    std::vector<Instr> returnInstructions_;
    bool generated_{};
};

class AssemblyArea : public SolverComponent {
    public:
    AssemblyArea(GameObject* parent, ProgramWriter& writer, int width,
                 const std::vector<const Molecule*>& products);
    void SetUsedBonders(GlyphType type, std::optional<Rot> direction);
    void OptimizeParts();

    int width{};
    bool hasSingle60Bonder{};
    bool hasSingle120Bonder{};
    bool hasTriplexBonders{};
    std::vector<Arm*> lowerArms;
    std::vector<Arm*> upperArms;
    std::vector<Glyph*> bonders;

    private:
    void CreateArms();
    void CreateBonders();
    void CreateTracks();
    Glyph* AddBonder(Vec2& position, int xOffset, int yOffset, Rot direction, GlyphType type);
    std::set<Glyph*> usedBonders_;
};

class ProductConveyor : public SolverComponent {
    public:
    ProductConveyor(GameObject* parent, ProgramWriter& writer, Vec2 position,
                    const std::vector<const Molecule*>& products);
    void MoveProductToOutputLocation(const Molecule& product);

    private:
    struct Output {
        int grabPosition{};
        int dropPosition{};
    };
    void CreateOutputs();
    Arm* outputArm_{};
    std::map<int, Output> outputs_;
    std::vector<const Molecule*> products_;
};

class SingleMonoAssemblerModule : public MoleculeAssembler {
    public:
    SingleMonoAssemblerModule(GameObject* parent, ProgramWriter& writer, Vec2 position,
                              const std::vector<const Molecule*>& products);
    void AddAtom(Element element, int productID) override;
};

class MonoAssemblerModule : public MoleculeAssembler {
    public:
    static constexpr int kMaxProducts = 4;
    MonoAssemblerModule(GameObject* parent, ProgramWriter& writer, Vec2 position,
                        const std::vector<const Molecule*>& products);
    void AddAtom(Element element, int productID) override;

    private:
    std::map<int, Arm*> outputArms_;
};

class LinearAssemblerModule : public MoleculeAssembler {
    public:
    LinearAssemblerModule(GameObject* parent, ProgramWriter& writer, Vec2 position,
                          const std::vector<const Molecule*>& products);
    [[nodiscard]] Vec2 OutputPosition() const override { return outputPosition_; }
    void AddAtom(Element element, int productID) override;

    private:
    void AssembleStep();
    std::vector<const Molecule*> products_;
    const Molecule* currentProduct_{};
    ProductConveyor* productConveyor_{};
    Arm* arm_{};
    Vec2 outputPosition_{};
    int column_{};
    bool assembling_{};
};

class UniversalAssemblerModule : public MoleculeAssembler {
    public:
    UniversalAssemblerModule(GameObject* parent, ProgramWriter& writer, Vec2 position,
                             const std::vector<const Molecule*>& products);
    [[nodiscard]] Vec2 OutputPosition() const override { return {2, 1}; }
    void AddAtom(Element element, int productID) override;
    void OptimizeParts() override;

    private:
    void AssembleStep();
    void GrabAtom(const Atom& atom);
    void FinishRow(int row);
    void StartAssemble(const Molecule& product);
    std::vector<const Atom*> GetAtomsToGrabForRow(int row, bool ignoreTriplexBonds) const;
    static std::set<const Atom*> GetConnectedAtoms(const Atom& startAtom,
                                                   const std::vector<const Atom*>& allAtoms,
                                                   bool ignoreTriplexBonds);

    int width_{};
    std::vector<const Molecule*> products_;
    AssemblyArea* assemblyArea_{};
    ProductConveyor* productConveyor_{};
    const Molecule* currentProduct_{};
    std::vector<const Atom*> assembledAtoms_;
    int currentArm_{};
    int assembleRow_{};
    int assembleAtomIndex_{};
    bool assembling_{};
};

class SimpleOutputAreaModule : public AtomGenerator {
    public:
    SimpleOutputAreaModule(GameObject* parent, ProgramWriter& writer,
                           const std::vector<const Molecule*>& products);
    void Consume(Element element, int id) override;
    void OptimizeParts() override;

    private:
    MoleculeAssembler* assembler_{};
};

// ── Factories ───────────────────────────────────────────────────────────────
/// Builds the atom-generator tree for one logical pipeline, placing each module
/// at its parent's output access point (upstream CreateAtomGenerators).
void CreateAtomGenerators(ElementPipeline& pipeline, ProgramWriter& writer,
                          std::unique_ptr<AtomGenerator>& root);

} // namespace opusoptima::solvers::standard
