#include "SerialArmExecution.hpp"
#include "MaterialPlanning.hpp"
#include "AssemblySolver.hpp"

#include <gtest/gtest.h>
#include <algorithm>
#include <cstdlib>
#include "HexFrame.hpp"
#include "SerialPortAccess.hpp"
#include "TestOutputDirectory.hpp"
#include <fstream>
#ifdef HAS_LIBVERIFY
#include "IO/BinaryWriter.hpp"
#include "IO/VerificationJson.hpp"
#include "RepoRoot.hpp"
#endif

namespace opusoptima::solvers {
#ifdef HAS_LIBVERIFY
namespace {
void WriteSerialPuzzle(const std::filesystem::path& path, const io::PuzzleData& p) {
    std::ofstream out(path, std::ios::binary);
    const auto word = [&](std::uint64_t value, int bytes) {
        for (int i = 0; i < bytes; ++i) out.put(static_cast<char>(value >> (8 * i)));
    };
    word(3,4); word(p.name.size(),1); out.write(p.name.data(),p.name.size());
    word(0,8); word(p.permissionBits,8);
    for (const auto* list : {&p.inputs, &p.outputs}) {
        word(list->size(),4);
        for (const auto& molecule : *list) {
            word(molecule.atoms.size(),4);
            for (const auto& atom : molecule.atoms) {
                word(static_cast<unsigned>(atom.type),1); word(atom.position.q,1); word(atom.position.r,1);
            }
            word(molecule.bonds.size(),4);
            for (const auto& bond : molecule.bonds) {
                word(bond.bits,1); word(bond.from.q,1); word(bond.from.r,1);
                word(bond.to.q,1); word(bond.to.r,1);
            }
        }
    }
    word(1,4); word(0,1);
}
}
#endif
TEST(SerialArmExecution, ProductPortStaticWitnesses) {
    using C = SerialPortCell;
    const C port{0, 0};
    const std::set<C> blocked{{1, -1}, {0, 1}, {-1, 0}};
    EXPECT_TRUE(SerialPortApproaches(port, blocked, {}).empty());
    // Removing ANY arm cannot help: all six bases or both donors are static cells.
    std::set<C> arms{{-1, 1}, {1, 0}, {0, -1}};
    EXPECT_FALSE(SerialPortHasApproach(SerialPortApproaches(port, blocked, {}), arms));
    auto open = blocked;
    open.erase({1, -1});
    const auto witnesses = SerialPortApproaches(port, open, {});
    ASSERT_FALSE(witnesses.empty());
    const auto [base, donor] = witnesses.front();
    EXPECT_FALSE(open.contains(base));
    EXPECT_FALSE(open.contains(donor));
    EXPECT_TRUE(SerialPortHasApproach(witnesses, {base}));
    EXPECT_FALSE(SerialPortHasApproach({{base, donor}}, {base, donor}));
    // Adding an arm at a protected port's sole donor must remain a rejection.
    const std::vector<std::pair<C, C>> protectedWitness{{{3, 0}, donor}};
    EXPECT_TRUE(SerialPortHasApproach(protectedWitness, {{3, 0}}));
    EXPECT_FALSE(SerialPortHasApproach(protectedWitness, {{3, 0}, donor}));
    // Feed occupancy is the existing explicit donor exception, not an output exception.
    EXPECT_FALSE(SerialPortApproaches(port, blocked, blocked).empty());
    const std::set<C> six{{1,0},{0,1},{-1,1},{-1,0},{0,-1},{1,-1}};
    EXPECT_TRUE(SerialPortApproaches(port, six, six).empty());
    // A rotated/translated copy has the same obstruction (pure geometry only).
    std::set<C> transformed;
    for (auto [q, r] : blocked) transformed.emplace(7-r, -4+q+r);
    EXPECT_TRUE(SerialPortApproaches({7,-4}, transformed, {}).empty());

    using IR = ExecutionIR;
    IR::State physical;
    physical.atoms.emplace(10, IR::Atom{core::AtomType::Fire, {0,-20}});
    const C transit{17,-1}, final{16,0}, neighbor{17,0};
    const std::set<C> outputs{transit, final, neighbor};
    std::set<C> excluded;
    const std::map<C, std::vector<C>> endpoints{{transit,{neighbor}}, {final,{neighbor}}};
    const std::map<IR::AtomId,C> targets{{10,final},{9,neighbor}};
    const std::map<IR::Edge,std::uint8_t> expected{{{9,10},1}};
    const auto allows = [&](C next, C destination) {
        return SerialSingletonContacts(physical, 10, endpoints, outputs, excluded,
                                       targets, expected).Allows(next, destination);
    };
    EXPECT_TRUE(allows(transit, final));
    for (auto blockedCell : {transit, final}) {
        excluded.insert(blockedCell);
        EXPECT_FALSE(allows(blockedCell, final));
        excluded.clear();
    }
    physical.atoms.emplace(9, IR::Atom{core::AtomType::Fire, {17,-1}});
    EXPECT_FALSE(allows(transit, final));
    physical.atoms.at(9).position = {17,0};
    // The actual empty future cell would bond the correct IDs at the wrong pose.
    EXPECT_FALSE(allows(transit, final));
    EXPECT_FALSE(allows(transit, transit));
    EXPECT_TRUE(allows(final, final));
    EXPECT_FALSE(allows(final, transit));
    physical.bonds.emplace(IR::Edge{9,10}, 1);
    EXPECT_FALSE(allows(final, final));
    physical.bonds.clear();
    physical.atoms.at(9).position = {18,0};
    EXPECT_TRUE(allows(transit, final));
    physical.atoms.emplace(8, IR::Atom{core::AtomType::Fire, {17,0}});
    EXPECT_FALSE(allows(transit, final));
    EXPECT_FALSE(allows(final, final));

    IR::State source;
    source.atoms.emplace(1, IR::Atom{core::AtomType::Fire, {-18,0}});
    source.atoms.emplace(2, IR::Atom{core::AtomType::Fire, {-17,0}});
    source.bonds.emplace(IR::Edge{1,2}, 1);
    const std::set<C> footprint{{-18,0},{-17,0}};
    std::vector<io::SolutionObject> glyphs{{"unbonder",{-18,0},{},{1,0,0,0,0}}};
    const auto cut = [&](std::uint64_t permissions = ~0ULL, const std::set<C>& output = {}) {
        return SerialSourceCutWitness(source, {1,2}, 1, glyphs, footprint, footprint,
                                      output, permissions);
    };
    EXPECT_TRUE(cut());
    IR material;
    material.steps.push_back({{IR::Kind::Acquire}, {}, source});
    auto separated = source;
    separated.bonds.clear();
    material.steps.push_back({{IR::Kind::Unbond,0,{1,2},1}, source, separated});
    EXPECT_TRUE(SerialMaterialSourceCuts(material));
    material.steps.pop_back();
    EXPECT_FALSE(SerialMaterialSourceCuts(material));
    material.steps.push_back({{IR::Kind::Relocate,0,{1}}, source, source});
    material.steps.push_back({{IR::Kind::Unbond,0,{1,2},1}, source, separated});
    EXPECT_FALSE(SerialMaterialSourceCuts(material));
    EXPECT_FALSE(cut(~(1ULL << 9)));
    EXPECT_FALSE(cut(~0ULL, {{-17,0}}));
    for (int turn : {1, -1, 6}) {
        glyphs[0].bodyData[1] = turn;
        EXPECT_FALSE(cut());
    }
    glyphs.clear();
    EXPECT_FALSE(cut());
    // A retained source edge cannot satisfy the singleton transport witness.
    EXPECT_FALSE(SerialSingletonContacts(source, 1, {}, {}, {}, {}, {}).Allows({0,0},{0,0}));
}

TEST(SerialArmExecution, OrdinaryFutureOutputTransit) {
    using IR = ExecutionIR;
    using K = IR::Kind;
    io::PuzzleData p;
    p.version = 3; p.outputMultiplier = 1; p.permissionBits = ~0ULL;
    p.name = "generated-ordinary-future-output-transit";
    p.inputs = {{{{core::AtomType::Earth,{0,0}}},{}}};
    p.outputs = {{{{core::AtomType::Earth,{0,0}}, {core::AtomType::Earth,{1,-1}},
                  {core::AtomType::Earth,{0,1}}, {core::AtomType::Earth,{-1,0}}},
                  {{1,{0,0},{1,-1}}, {1,{0,0},{0,1}}, {1,{0,0},{-1,0}}}}};
    const auto make = [](const auto& puzzle) {
        IR ir;
        auto state = IR::Initial(puzzle);
        for (std::size_t i = 0; i < puzzle.outputs[0].atoms.size();) {
            const auto first = state.nextId;
            ir.AppendFromTail(puzzle, state, {K::Acquire,0});
            if (puzzle.inputs[0].atoms.size() == 2)
                ir.AppendFromTail(puzzle, state, {K::Unbond,0,{first,first+1},1});
            for (std::size_t j = 0; j < puzzle.inputs[0].atoms.size(); ++j, ++i)
                ir.AppendFromTail(puzzle, state, {K::Relocate,0,{first+j},0,0,
                    core::Subtract(puzzle.outputs[0].atoms.at(i).position,
                                   puzzle.inputs[0].atoms[j].position)});
        }
        for (IR::AtomId id = 2; id <= puzzle.outputs[0].atoms.size(); ++id)
            ir.AppendFromTail(puzzle, state, {K::Bond,0,{1,id},1});
        ir.AppendFromTail(puzzle, state, {K::Output,0,{1}});
        return MakeExecutionPlan(puzzle, ir);
    };
    const auto plan = make(p);
    const auto* previous = std::getenv("OPUSOPTIMA_PRODUCT_PORT_ACCESS");
    const std::optional<std::string> saved = previous ? std::optional<std::string>(previous) : std::nullopt;
    unsetenv("OPUSOPTIMA_PRODUCT_PORT_ACCESS");
    int submissions = 0;
    EXPECT_FALSE(CompileAcceptedSerialArmExecution(p, plan, [&](const auto&) {
        ++submissions; return true;
    }));
    EXPECT_EQ(submissions, 0);
#ifdef HAS_LIBVERIFY
    struct Files {
        std::filesystem::path path = std::filesystem::path("build/test-output") /
            ("serial-contact-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        Files() { std::filesystem::create_directories(path); }
        ~Files() { std::error_code error; std::filesystem::remove_all(path, error); }
    } files;
#endif
    const auto accepted = [&](const auto& p, const auto& plan, const std::string& label) {
    submissions = 0;
#ifdef HAS_LIBVERIFY
    const auto puzzlePath = files.path / (label + ".puzzle");
    WriteSerialPuzzle(puzzlePath, p);
#endif
    setenv("OPUSOPTIMA_PRODUCT_PORT_ACCESS", "1", 1);
    std::string evidence;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    EXPECT_TRUE(CompileAcceptedSerialArmExecution(p, plan, [&](const auto& machine) {
        ++submissions;
#ifdef HAS_LIBVERIFY
        const auto output = files.path / "generated.solution";
        io::BinaryWriter::WriteSolutionFile(output, machine);
        io::VerificationOptions options;
        options.mode = io::VerificationMode::LegacyGate;
        options.outerDeadline = deadline;
        for (int pass = 0; pass < 2; ++pass) {
            const auto report = io::VerifySolutionFiles(puzzlePath, output, options);
            RecordProperty(label + "NativeGoal6Pass" + std::to_string(pass), io::VerificationReportJson(report));
            EXPECT_EQ(report.authority, io::VerificationAuthority::LegacyGate);
            EXPECT_TRUE(report.Passed()) << io::VerificationReportJson(report);
            if (!report.Passed()) return false;
        }
#else
        (void)machine;
#endif
        return true;
    }, [&](const auto& message) { evidence += message + "\n"; }, deadline)) << evidence;
    EXPECT_EQ(submissions, 1);
    EXPECT_NE(evidence.find("certified future output transit"), std::string::npos);
    };
    accepted(p, plan, "singleton");
    auto dimer = p;
    dimer.inputs[0].atoms.push_back({core::AtomType::Earth,{1,0}});
    dimer.inputs[0].bonds.push_back({1,{0,0},{1,0}});
    const auto cutMaterial = SearchMaterialExecutionWithPolicy(
        dimer, SupplyMaterialLoweringPolicy::CutOrdinarySources);
    ASSERT_EQ(cutMaterial.status, MaterialPlanningStatus::Found) << cutMaterial.detail;
    ASSERT_TRUE(cutMaterial.plan);
    EXPECT_TRUE(SerialMaterialSourceCuts(cutMaterial.plan->material));
    IR::RequireComplete(cutMaterial.plan->material.Replay(dimer), {1});
    accepted(dimer, *cutMaterial.plan, "dimer");
    const auto refused = [&](const auto& puzzle) {
        int calls = 0;
        EXPECT_FALSE(CompileAcceptedSerialArmExecution(puzzle, make(puzzle), [&](const auto&) {
            ++calls; return true;
        }));
        EXPECT_EQ(calls, 0);
    };
    auto denied = dimer;
    denied.permissionBits &= ~(1ULL << 9);
    EXPECT_THROW(make(denied), IR::PermissionFailure);
    auto dimerOverlap = dimer;
    for (auto& atom : dimerOverlap.inputs[0].atoms) atom.position.q += 36;
    dimerOverlap.inputs[0].bonds[0].from.q += 36;
    dimerOverlap.inputs[0].bonds[0].to.q += 36;
    refused(dimerOverlap);
    auto overlap = p;
    overlap.inputs[0].atoms[0].position = {36,0};
    refused(overlap);
    for (const auto cell : {core::HexCoord{-1,1}, core::HexCoord{1,0}, core::HexCoord{0,-1}}) {
        p.outputs[0].atoms.push_back({core::AtomType::Earth, cell});
        p.outputs[0].bonds.push_back({1,{0,0},cell});
    }
    refused(p);
    if (saved) setenv("OPUSOPTIMA_PRODUCT_PORT_ACCESS", saved->c_str(), 1);
    else unsetenv("OPUSOPTIMA_PRODUCT_PORT_ACCESS");
}

TEST(SerialArmExecution, FiniteTerminalIsDistinctAndConservative) {
    using IR = ExecutionIR;
    IR::State state;
    state.outputs = {6};
    IR::FiniteRetention terminal{{6}, {}, 6};
    for (std::size_t slot = 0; slot < 6; ++slot) {
        state.atoms.emplace(slot, IR::Atom{core::AtomType::Vitae, {int(slot) * 3, 20}});
        state.stored[slot] = {slot};
        terminal.slots.push_back({slot, slot, core::AtomType::Vitae});
    }
    EXPECT_NO_THROW(IR::RequireFiniteTerminal(state, terminal));
    std::map<IR::AtomId, core::HexCoord> bound;
    for (const auto& [id, atom] : state.atoms) bound.emplace(id, atom.position);
    EXPECT_NO_THROW(RequireFinitePhysicalTerminal(state, terminal, bound));
    auto wrongPosition = state;
    wrongPosition.atoms.at(0).position = {100, 100};
    EXPECT_THROW(RequireFinitePhysicalTerminal(wrongPosition, terminal, bound), IR::LayoutFailure);
    auto missingStore = state;
    missingStore.stored.erase(0);
    EXPECT_THROW(RequireFinitePhysicalTerminal(missingStore, terminal, bound), IR::Error);
    EXPECT_THROW(IR::RequireComplete(state, {6}), IR::Error);
    auto missing = state;
    missing.atoms.erase(0);
    EXPECT_THROW(IR::RequireFiniteTerminal(missing, terminal), IR::Error);
    auto duplicate = terminal;
    duplicate.slots[1].atom = duplicate.slots[0].atom;
    EXPECT_THROW(IR::RequireFiniteTerminal(state, duplicate), IR::Error);
    auto small = terminal;
    small.capacity = 5;
    EXPECT_THROW(IR::RequireFiniteTerminal(state, small), IR::Error);
    auto wrong = state;
    wrong.atoms.at(0).type = core::AtomType::Mors;
    EXPECT_THROW(IR::RequireFiniteTerminal(wrong, terminal), IR::Error);
    auto overlap = state;
    overlap.atoms.at(0).position = overlap.atoms.at(1).position;
    EXPECT_THROW(IR::RequireFiniteTerminal(overlap, terminal), IR::Error);
    auto discarded = state;
    discarded.discarded = 1;
    EXPECT_THROW(IR::RequireFiniteTerminal(discarded, terminal), IR::Error);
}
TEST(SerialArmExecution, RetentionEndpointBindingRefusals) {
    using IR = ExecutionIR;
    using K = IR::Kind;
    using T = core::AtomType;
    const auto* previous = std::getenv("OPUSOPTIMA_FINITE_RETENTION");
    const std::optional<std::string> saved = previous ? std::optional<std::string>(previous) : std::nullopt;
    setenv("OPUSOPTIMA_FINITE_RETENTION", "1", 1);
    io::PuzzleData p;
    p.version = 3; p.outputMultiplier = 1; p.permissionBits = ~0ULL;
    p.inputs = {{{{T::Earth, {0, 0}}}, {}}};
    p.outputs = {{{{T::Mors, {0, 0}}}, {}}};
    IR ir;
    auto state = IR::Initial(p);
    IR::FiniteRetention terminal{{6}, {}, 6};
    auto append = [&](IR::Operation op) { ir.AppendFromTail(p, state, op); };
    for (std::size_t slot = 0; slot < 6; ++slot) {
        auto a = state.nextId; append({K::Acquire, 0});
        auto saltA = state.nextId; append({K::Calcify, 0, {a}});
        auto b = state.nextId; append({K::Acquire, 0});
        auto saltB = state.nextId; append({K::Calcify, 0, {b}});
        auto vitae = state.nextId; append({K::Animismus, 0, {saltA, saltB}});
        append({K::Relocate, 0, {vitae}, 0, 0, {int(slot) * 3, 20}});
        append({K::Store, slot, {vitae}});
        append({K::Relocate, 0, {vitae + 1}, 0, 0, {0, 0}});
        terminal.slots.push_back({slot, vitae, T::Vitae});
        append({K::Output, 0, {vitae + 1}});
    }
    auto plan = MakeFiniteExecutionPlan(p, ir, terminal, io::VerificationMode::LegacyGate);
    ASSERT_EQ(plan.retentionEndpoints.size(), 6);
    // Replay stays legal when the last delivery is postponed beyond native stop.
    auto operations = ir.steps;
    const auto lastStore = plan.retentionEndpoints.back().storeStep;
    ASSERT_EQ(lastStore, 51);
    const auto delivery = operations.at(lastStore - 1).operation;
    const auto store = operations.at(lastStore).operation;
    operations.erase(operations.begin() + lastStore - 1, operations.begin() + lastStore + 1);
    IR late;
    auto lateState = IR::Initial(p);
    for (const auto& step : operations) late.AppendFromTail(p, lateState, step.operation);
    late.AppendFromTail(p, lateState, delivery);
    late.AppendFromTail(p, lateState, store);
    EXPECT_NO_THROW(IR::RequireFiniteTerminal(late.Replay(p), terminal));
    EXPECT_THROW(MakeFiniteExecutionPlan(p, late, terminal, io::VerificationMode::LegacyGate), IR::Error);
    auto latePublic = plan;
    latePublic.material = late;
    int lateSubmissions = 0;
    EXPECT_FALSE(CompileAcceptedSerialArmExecution(p, latePublic, [&](const auto&) {
        ++lateSubmissions;
        return true;
    }));
    EXPECT_EQ(lateSubmissions, 0);

    const auto refused = [&](const auto& bad) {
        int submissions = 0;
        EXPECT_FALSE(CompileAcceptedSerialArmExecution(p, bad,
            [&](const auto&) { ++submissions; return true; }));
        EXPECT_EQ(submissions, 0);
    };
    auto bad = plan; ++bad.retentionEndpoints[0].atom; refused(bad);
    bad = plan; bad.finiteTerminal->slots.pop_back(); refused(bad);
    bad = plan; bad.finiteTerminal->slots[1].atom = bad.finiteTerminal->slots[0].atom; refused(bad);
    bad = plan; bad.finiteTerminal->capacity = 5; refused(bad);
    auto lost = state;
    lost.atoms.erase(terminal.slots.front().atom);
    EXPECT_THROW(RequireFinitePhysicalTerminal(lost, terminal, {}), IR::Error);
    auto occupied = state;
    EXPECT_THROW(ir.AppendFromTail(p, occupied,
        {K::Store, terminal.slots[0].slot, {terminal.slots[1].atom}}), IR::Error);
    bad = plan; ++bad.retentionEndpoints[0].slot; refused(bad);
    bad = plan; bad.retentionEndpoints[0].type = T::Mors; refused(bad);
    bad = plan; bad.retentionEndpoints.clear(); refused(bad);
    bad = plan; bad.finiteTerminal.reset(); refused(bad);
    bad = plan; bad.caches[0].slot = 1; refused(bad);
    bad = plan; bad.retentionEndpoints[0].logicalPosition = {99, 99}; refused(bad);
    bad = plan; bad.retentionEndpoints[1] = bad.retentionEndpoints[0]; refused(bad);
    auto ordinaryPuzzle = p;
    ordinaryPuzzle.outputs = ordinaryPuzzle.inputs;
    IR ordinary;
    auto ordinaryState = IR::Initial(ordinaryPuzzle);
    const auto ordinaryAtom = ordinaryState.nextId;
    ordinary.AppendFromTail(ordinaryPuzzle, ordinaryState, {K::Acquire, 0});
    ordinary.AppendFromTail(ordinaryPuzzle, ordinaryState, {K::Relocate, 0, {ordinaryAtom}, 0, 0, {3, 20}});
    ordinary.AppendFromTail(ordinaryPuzzle, ordinaryState, {K::Relocate, 0, {ordinaryAtom}, 0, 0, {0, 0}});
    ordinary.AppendFromTail(ordinaryPuzzle, ordinaryState, {K::Output, 0, {ordinaryAtom}});
    int ordinarySubmissions = 0;
    EXPECT_TRUE(CompileAcceptedSerialArmExecution(ordinaryPuzzle,
        MakeExecutionPlan(ordinaryPuzzle, ordinary),
        [&](const auto&) { ++ordinarySubmissions; return true; }));
    EXPECT_EQ(ordinarySubmissions, 1);
    auto insufficient = terminal; insufficient.capacity = 5;
    EXPECT_THROW(MakeFiniteExecutionPlan(p, ir, insufficient, io::VerificationMode::LegacyGate), IR::Error);
    EXPECT_THROW(MakeFiniteExecutionPlan(p, ir, terminal, io::VerificationMode::Independent), IR::Unsupported);
    auto outside = p; outside.outputMultiplier = 2;
    EXPECT_THROW(MakeFiniteExecutionPlan(outside, ir, terminal, io::VerificationMode::LegacyGate), IR::Unsupported);
    outside = p; outside.version = 2;
    EXPECT_THROW(MakeFiniteExecutionPlan(outside, ir, terminal, io::VerificationMode::LegacyGate), IR::Unsupported);
    unsetenv("OPUSOPTIMA_FINITE_RETENTION");
    EXPECT_THROW(MakeFiniteExecutionPlan(p, ir, terminal, io::VerificationMode::LegacyGate), IR::Unsupported);
    if (saved) setenv("OPUSOPTIMA_FINITE_RETENTION", saved->c_str(), 1);
}
TEST(SerialArmExecution, RelayOptionalParentAndRefusalBoundaries) {
    io::PuzzleData p;
    p.version = 3;
    p.outputMultiplier = 1;
    p.permissionBits = ~0ULL;
    p.inputs = {{{{core::AtomType::Salt, {0, 0}}}, {}}};
    p.outputs = p.inputs;
    const auto material = SearchMaterialExecution(p);
    ASSERT_TRUE(material.plan);
    int parents = 0, children = 0;
    const auto accept = [&](const auto&) { ++parents; return true; };
    const auto child = [&](const auto&, const auto&) { ++children; };
    auto denied = p;
    denied.permissionBits &= ~(1ULL << 0);
    EXPECT_FALSE(CompileAcceptedSerialArmExecution(denied, *material.plan, accept, {},
        std::chrono::steady_clock::time_point::max(), {}, child));
    EXPECT_EQ(parents, 0);
    EXPECT_EQ(children, 0);
    EXPECT_FALSE(CompileAcceptedSerialArmExecution(p, *material.plan, accept, {},
        std::chrono::steady_clock::now(), {}, child));
    EXPECT_EQ(parents, 0);
    EXPECT_TRUE(CompileAcceptedSerialArmExecution(p, *material.plan, accept, {},
        std::chrono::steady_clock::time_point::max(), {}, child));
    EXPECT_EQ(parents, 1);
    EXPECT_LE(children, 1);
    EXPECT_TRUE(CompileAcceptedSerialArmExecution(p, *material.plan, accept, {},
        std::chrono::steady_clock::time_point::max(), {}, [](const auto&, const auto&) {
            throw std::runtime_error("optional gate unavailable");
        }));
    const auto* previous = std::getenv("OPUSOPTIMA_SERIAL_HANDOFF");
    const std::optional<std::string> saved = previous ? std::optional<std::string>(previous) : std::nullopt;
    setenv("OPUSOPTIMA_SERIAL_HANDOFF", "1", 1);
    children = 0;
    EXPECT_TRUE(CompileAcceptedSerialArmExecution(p, *material.plan, accept, {},
        std::chrono::steady_clock::time_point::max(), {}, child));
    EXPECT_EQ(children, 0);
    if (saved) setenv("OPUSOPTIMA_SERIAL_HANDOFF", saved->c_str(), 1);
    else unsetenv("OPUSOPTIMA_SERIAL_HANDOFF");
}
TEST(SerialArmExecution, PermissionGateAndAcceptanceExceptions) {
    io::PuzzleData p;
    p.version = 3;
    p.outputMultiplier = 1;
    p.permissionBits = ~0ULL;
    p.inputs = {{{{core::AtomType::Salt, {0, 0}}}, {}}};
    p.outputs = p.inputs;
    const auto material = SearchMaterialExecution(p);
    ASSERT_TRUE(material.plan);
    auto denied = p;
    denied.permissionBits &= ~(1ULL << 0);
    bool called = false;
    EXPECT_FALSE(CompileAcceptedSerialArmExecution(denied, *material.plan, [&](const auto&) {
        called = true;
        return true;
    }));
    EXPECT_FALSE(called);
    EXPECT_THROW(
        (void)CompileAcceptedSerialArmExecution(
            p, *material.plan,
            [](const auto&) -> bool { throw std::runtime_error("serial verifier unavailable"); }),
        std::runtime_error);
    auto forbidden = p;
    forbidden.permissionBits &= ~(1ULL << 22);
    EXPECT_FALSE(CompileAcceptedSerialArmExecution(forbidden, *material.plan, [&](const auto&) {
        called = true;
        return true;
    }));
    EXPECT_FALSE(called);
}
TEST(SerialArmExecution, RejectedMaterialHistoryContinuesAcrossFramesAndInputOrders) {
    for (bool bonded : {false, true})
        for (int rotation = 0; rotation < 6; ++rotation)
            for (bool reverse : {false, true}) {
                io::PuzzleData p;
                p.version = 3;
                p.outputMultiplier = 1;
                p.permissionBits = ~0ULL;
                p.inputs = {{{{core::AtomType::Salt, {0, 0}}}, {}}};
                if (bonded) {
                    p.inputs[0].atoms.push_back({core::AtomType::Salt, {1, 0}});
                    p.inputs[0].bonds.push_back({1, {0, 0}, {1, 0}});
                }
                p.outputs = p.inputs;
                auto alternate = p.inputs[0];
                const core::HexFrame frame{{3, -2}, rotation};
                for (auto& atom : alternate.atoms) atom.position = frame.ToWorld(atom.position);
                for (auto& bond : alternate.bonds) {
                    bond.from = frame.ToWorld(bond.from);
                    bond.to = frame.ToWorld(bond.to);
                }
                p.inputs.push_back(alternate);
                if (reverse) std::reverse(p.inputs.begin(), p.inputs.end());
                std::size_t histories = 0;
                std::optional<io::SolutionData> accepted;
                MaterialPlanningLimits limits;
                limits.deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
                const auto result = SearchMaterialExecution(p, limits, [&](const auto& plan) {
                    (void)plan.material.Replay(p);
                    // Deterministic physical acceptance oracle: the first history is rejected.
                    if (++histories == 1) return false;
                    accepted = CompileAcceptedSerialArmExecution(
                        p, plan, [](const auto&) { return true; }, {}, limits.deadline);
                    return accepted.has_value();
                });
                EXPECT_EQ(result.status, MaterialPlanningStatus::Found)
                    << bonded << "/" << rotation << "/" << reverse << ": " << result.detail;
                EXPECT_GE(histories, 2U);
                EXPECT_TRUE(accepted);
            }
}

TEST(SerialArmExecution, ColoredOutputStationMasksAndRefusals) {
    using IR = ExecutionIR;
    using K = IR::Kind;
    using T = core::AtomType;
    io::PuzzleData p;
    p.version = 3; p.outputMultiplier = 1; p.permissionBits = ~0ULL;
    p.inputs = {{{{T::Fire, {0, 0}}}, {}}};
    p.outputs = {{{{T::Fire, {0, 0}}, {T::Fire, {1, 0}}}, {{2, {0, 0}, {1, 0}}}}};
    const auto make = [](const auto& puzzle) {
        IR ir;
        auto state = IR::Initial(puzzle);
        const auto first = state.nextId;
        for (const auto& atom : puzzle.outputs[0].atoms) {
            const auto id = state.nextId;
            ir.AppendFromTail(puzzle, state, {K::Acquire, 0});
            ir.AppendFromTail(puzzle, state, {K::Relocate, 0, {id}, 0, 0,
                core::Subtract(atom.position, puzzle.inputs[0].atoms[0].position)});
        }
        for (const auto& bond : puzzle.outputs[0].bonds) {
            const auto at = [&](core::HexCoord position) {
                return first + static_cast<IR::AtomId>(std::find_if(puzzle.outputs[0].atoms.begin(),
                    puzzle.outputs[0].atoms.end(), [&](const auto& atom) {
                        return atom.position == position;
                    }) - puzzle.outputs[0].atoms.begin());
            };
            ir.AppendFromTail(puzzle, state, {K::Bond, 0, {at(bond.from), at(bond.to)}, bond.bits});
        }
        ir.AppendFromTail(puzzle, state, {K::Output, 0, {first}});
        return MakeExecutionPlan(puzzle, ir);
    };
    for (int mask : {2, 4, 6, 8, 10, 12, 14}) {
        p.outputs[0].bonds[0].bits = mask;
        const auto plan = make(p);
        EXPECT_TRUE(CompileAcceptedSerialArmExecution(p, plan, [&](const auto& machine) {
            const auto count = std::count_if(machine.objects.begin(), machine.objects.end(),
                [](const auto& part) { return part.typeName == "bonder-prisma"; });
            EXPECT_EQ(count, ((mask & 2) != 0) + ((mask & 4) != 0) + ((mask & 8) != 0));
            return true;
        })) << mask;
        auto denied = p;
        denied.permissionBits &= ~(1ULL << 11);
        int submissions = 0;
        EXPECT_FALSE(CompileAcceptedSerialArmExecution(denied, plan, [&](const auto&) {
            ++submissions; return true;
        }));
        EXPECT_EQ(submissions, 0);
    }
    p.outputs[0].atoms.push_back({T::Fire, {0, 1}});
    p.outputs[0].bonds.push_back({1, {1, 0}, {0, 1}});
    int submissions = 0;
    const auto enclosed = make(p);
    EXPECT_FALSE(CompileAcceptedSerialArmExecution(p, enclosed, [&](const auto&) {
        ++submissions; return true;
    }));
    EXPECT_EQ(submissions, 0);
    p.outputs[0].atoms.pop_back();
    p.outputs[0].bonds.pop_back();
    p.inputs[0].atoms[0].position = {36, 0};
    const auto collision = make(p);
    std::string failures;
    EXPECT_FALSE(CompileAcceptedSerialArmExecution(p, collision, [&](const auto&) {
        ++submissions; return true;
    }, [&](const auto& detail) { failures += detail + "\n"; }));
    EXPECT_EQ(submissions, 0);
    EXPECT_NE(failures.find("colored output prism overlaps a source port"), std::string::npos);
}

TEST(SerialArmExecution, ClosedSingletonWheelColoredPair) {
    using IR = ExecutionIR;
    using K = IR::Kind;
    using T = core::AtomType;
    io::PuzzleData p;
    p.version = 3; p.outputMultiplier = 1; p.permissionBits = ~0ULL;
    p.inputs = {{{{T::Earth, {0, 0}}, {T::Water, {1, 0}}}, {{1, {0, 0}, {1, 0}}}}};
    p.outputs = {{{{T::Fire, {0, 0}}, {T::Fire, {1, 0}}}, {{14, {0, 0}, {1, 0}}}}};
    EXPECT_TRUE(SupportsClosedSingletonWheelPlanning(p));
    const auto automatic = SearchMaterialExecution(p);
    ASSERT_TRUE(automatic.plan) << automatic.detail;
    EXPECT_LE(automatic.states, 4096U);
    EXPECT_LE(automatic.candidates, 32U);
    ExecutionIR::RequireComplete(automatic.plan->material.Replay(p), {1});
    auto refused = [&](const io::PuzzleData& invalid) {
        EXPECT_FALSE(SupportsClosedSingletonWheelPlanning(invalid));
    };
    for (int bit : {0, 8, 9, 11, 12, 13, 22, 23, 25, 28}) {
        auto denied = p; denied.permissionBits &= ~(1ULL << bit); refused(denied);
    }
    for (const auto type : {T::Fire, T::Quintessence, T::Lead, T::Vitae}) {
        auto source = p; source.inputs[0].atoms[0].type = type; refused(source);
    }
    auto invalid = p;
    invalid.outputs[0].atoms.push_back({T::Air, {2, 0}});
    invalid.outputs[0].atoms.push_back({T::Air, {3, 0}});
    invalid.outputs[0].atoms.push_back({T::Air, {4, 0}}); refused(invalid);
    invalid = p; invalid.productionInfo.emplace(); refused(invalid);
    invalid = p; invalid.outputMultiplier = 9; refused(invalid);
    invalid = p; invalid.outputs[0].bonds[0].bits = 3; refused(invalid);
    invalid = p; invalid.outputs[0].bonds.clear(); refused(invalid);
    invalid = p; invalid.inputs[0].bonds[0].bits = 14; refused(invalid);
    for (const auto mask : {2, 4, 6, 8, 10, 12, 14}) {
        auto valid = p; valid.outputs[0].bonds[0].bits = mask;
        EXPECT_TRUE(SupportsClosedSingletonWheelPlanning(valid));
    }
    auto disabled = p; disabled.permissionBits &= ~(1ULL << 28);
    EXPECT_NE(SearchMaterialExecution(disabled).status, MaterialPlanningStatus::Found);
    IR material;
    auto state = IR::Initial(p);
    auto append = [&](IR::Operation op) { material.AppendFromTail(p, state, op); };
    const auto first = state.nextId;
    append({K::Acquire, 0});
    append({K::Unbond, 0, {first, first + 1}, 1});
    std::vector<IR::AtomId> products;
    for (int index = 0; index < 2; ++index) {
        const auto salt = state.nextId;
        append({K::Calcify, 0, {first + index}});
        const auto fire = state.nextId;
        IR::Operation duplicate{K::Duplicate, 0, {salt}};
        duplicate.wheelCatalyst = IR::WheelCatalyst{0, T::Fire, 0};
        append(duplicate);
        append({K::Relocate, 0, {fire}, 0, 0,
            core::Subtract({index, 0}, state.atoms.at(fire).position)});
        products.push_back(fire);
    }
    append({K::Bond, 0, products, 14});
    append({K::Output, 0, {products.front()}});
    IR::RequireComplete(state, {1});
    const auto plan = MakeExecutionPlan(p, material);
    std::string errors;
    auto machine = CompileAcceptedSerialArmExecution(p, *automatic.plan, [](const auto&) { return true; },
        [&](const auto& error) { errors += error + "\n"; });
    ASSERT_TRUE(machine) << errors;
    EXPECT_EQ(std::count_if(machine->objects.begin(), machine->objects.end(), [](const auto& part) {
        return part.typeName == "baron" && part.instructions.empty();
    }), 1);
    EXPECT_TRUE(std::any_of(machine->objects.begin(), machine->objects.end(), [](const auto& part) {
        return part.typeName == "unbonder";
    }));
    auto forged = plan;
    forged.origins.at(products.front()).reagents = {first};
    int forgedSubmissions = 0;
    EXPECT_FALSE(CompileAcceptedSerialArmExecution(p, forged, [&](const auto&) {
        ++forgedSubmissions; return true;
    }));
    EXPECT_EQ(forgedSubmissions, 0);
    AssemblySolver collector;
    collector.SetDeadline(std::chrono::steady_clock::now() + std::chrono::seconds(3));
    std::size_t independentSubmissions = 0, wheelSubmissions = 0;
    EXPECT_THROW((void)collector.CollectVerified(p, [&](const auto&, const auto& stage) {
        ++independentSubmissions;
        io::VerificationReport report;
        report.status = io::VerificationStatus::Rejected;
        if (stage == "shared-plan-wheel") {
            ++wheelSubmissions;
            report.status = io::VerificationStatus::Passed;
            report.authority = io::VerificationAuthority::Independent;
        }
        return report;
    }, 1), std::exception);
    EXPECT_GT(wheelSubmissions, 0U);
    EXPECT_LE(independentSubmissions, AssemblySolver::MaxCandidateSubmissions);
    auto precursor = p;
    precursor.inputs = {{{{T::Fire, {0, 0}}}, {}}};
    precursor.outputs = {{{{T::Water, {0, 0}}, {T::Fire, {-1, 1}},
                            {T::Fire, {1, 0}}, {T::Fire, {0, -1}}},
                           {{8, {0, 0}, {-1, 1}}, {8, {0, 0}, {1, 0}}, {8, {0, 0}, {0, -1}}}}};
    ASSERT_FALSE(SupportsClosedSingletonWheelPlanning(precursor));
    AssemblySolver priorCollector;
    priorCollector.SetDeadline(std::chrono::steady_clock::now() + std::chrono::seconds(3));
    bool priorSerialAccepted = false;
    EXPECT_NO_THROW((void)priorCollector.CollectVerified(precursor, [&](const auto&, const auto& stage) {
        io::VerificationReport report;
        report.status = io::VerificationStatus::Rejected;
        if (stage == "shared-plan-serial-arms") {
            priorSerialAccepted = true;
            report.status = io::VerificationStatus::Passed;
            report.authority = io::VerificationAuthority::Independent;
        }
        return report;
    }, 1));
    EXPECT_TRUE(priorSerialAccepted);
    for (int permission : {9, 11, 12, 13, 28}) {
        auto denied = p;
        denied.permissionBits &= ~(1ULL << permission);
        int submissions = 0;
        EXPECT_FALSE(CompileAcceptedSerialArmExecution(denied, plan, [&](const auto&) {
            ++submissions; return true;
        })) << permission;
        EXPECT_EQ(submissions, 0) << permission;
    }
}

TEST(SerialArmExecution, FiniteMonomerWheelBinding) {
    using IR = ExecutionIR;
    using K = IR::Kind;
    using T = core::AtomType;
    io::PuzzleData p;
    p.version = 3;
    p.outputMultiplier = 1;
    p.permissionBits = ~0ULL;
    p.inputs = {{{{T::Earth, {0, 0}}}, {}}};
    p.outputs = {{{{T::Water, {0, 0}}}, {}}};
    IR material;
    auto state = IR::Initial(p);
    for (int n = 0; n < 6; ++n) {
        auto id = state.nextId;
        material.AppendFromTail(p, state, {K::Acquire, 0});
        auto salt = state.nextId;
        material.AppendFromTail(p, state, {K::Calcify, 0, {id}});
        IR::Operation duplicate{K::Duplicate, 0, {salt}};
        duplicate.wheelCatalyst = IR::WheelCatalyst{0, T::Water, 0};
        material.AppendFromTail(p, state, duplicate);
        material.AppendFromTail(p, state, {K::Output, 0, {state.nextId - 1}});
    }
    IR::RequireComplete(state, {6});
    const auto* previous = std::getenv("OPUSOPTIMA_FINITE_RETENTION");
    const std::optional<std::string> saved = previous ? std::optional<std::string>(previous) : std::nullopt;
    setenv("OPUSOPTIMA_FINITE_RETENTION", "1", 1);
    auto plan = MakeFiniteExecutionPlan(p, material, {{6}, {}, 6}, io::VerificationMode::LegacyGate);
    EXPECT_THROW(MakeFiniteExecutionPlan(p, material, {{6}, {}, 6},
                                       io::VerificationMode::Independent), IR::Unsupported);
    EXPECT_TRUE(CompileAcceptedSerialArmExecution(p, plan, [](const auto&) { return true; }));
    auto denied = p;
    denied.permissionBits &= ~(1ULL << 28);
    EXPECT_FALSE(CompileAcceptedSerialArmExecution(denied, plan, [](const auto&) { return true; }));
    auto wrong = p;
    wrong.inputs[0].atoms[0].type = T::Salt;
    EXPECT_FALSE(CompileAcceptedSerialArmExecution(wrong, plan, [](const auto&) { return true; }));
    unsetenv("OPUSOPTIMA_FINITE_RETENTION");
    EXPECT_THROW(MakeFiniteExecutionPlan(p, material, {{6}, {}, 6},
                                       io::VerificationMode::LegacyGate), IR::Unsupported);
    if (saved) setenv("OPUSOPTIMA_FINITE_RETENTION", saved->c_str(), 1);
}

TEST(SerialArmExecution, SharedDeadlineAndTypedPermissionFailures) {
    io::PuzzleData p;
    p.version = 3;
    p.outputMultiplier = 1;
    p.permissionBits = ~0ULL;
    p.inputs = {{{{core::AtomType::Salt, {0, 0}}}, {}}};
    p.outputs = p.inputs;
    const auto material = SearchMaterialExecution(p);
    ASSERT_TRUE(material.plan);
    bool published = false;
    const auto accept = [&](const auto&) { published = true; return true; };
    std::vector<SerialArmFailure> failures;
    const auto feedback = [&](auto kind, const auto&) { failures.push_back(kind); };
    const auto expired = std::chrono::steady_clock::now() - std::chrono::seconds(1);
    EXPECT_FALSE(CompileAcceptedSerialArmExecution(p, *material.plan, accept, {}, expired, feedback));
    ASSERT_EQ(failures.size(), 1U);
    EXPECT_EQ(failures.front(), SerialArmFailure::BudgetExhausted);
    MaterialPlanningLimits limits;
    limits.deadline = expired;
    EXPECT_EQ(SearchMaterialExecution(p, limits).status, MaterialPlanningStatus::BudgetExhausted);
    EXPECT_THROW((void)SearchMaterialExecution(p, {}, [](const auto&) -> bool {
        throw std::invalid_argument("acceptance infrastructure failed");
    }), std::invalid_argument);
    limits = {};
    limits.maxCandidates = 1;
    std::size_t rejected = 0;
    const auto bounded = SearchMaterialExecution(p, limits, [&](const auto&) {
        ++rejected;
        return false;
    });
    EXPECT_FALSE(bounded.plan);
    EXPECT_EQ(bounded.status, MaterialPlanningStatus::Exhausted);
    EXPECT_EQ(rejected, 1U);
    p.permissionBits &= ~(1ULL << 22);
    failures.clear();
    EXPECT_FALSE(CompileAcceptedSerialArmExecution(p, *material.plan, accept, {},
        std::chrono::steady_clock::time_point::max(), feedback));
    ASSERT_FALSE(failures.empty());
    for (auto failure : failures) EXPECT_EQ(failure, SerialArmFailure::Unsupported);
    EXPECT_FALSE(published);
}
} // namespace opusoptima::solvers

namespace opusoptima::solvers {
TEST(SerialArmExecution, WholeFragmentFiniteDockingBoundaries) {
    using IR = ExecutionIR;
    const auto* oldFinite = std::getenv("OPUSOPTIMA_FINITE_RETENTION");
    const auto* oldDock = std::getenv("OPUSOPTIMA_FRAGMENT_PRODUCT_DOCKING");
    const std::optional<std::string> savedFinite = oldFinite ? std::optional<std::string>(oldFinite) : std::nullopt;
    const std::optional<std::string> savedDock = oldDock ? std::optional<std::string>(oldDock) : std::nullopt;
    setenv("OPUSOPTIMA_FINITE_RETENTION", "1", 1);
    setenv("OPUSOPTIMA_FRAGMENT_PRODUCT_DOCKING", "1", 1);
    io::PuzzleData p;
    p.version = 3; p.outputMultiplier = 1; p.permissionBits = ~0ULL;
    io::Molecule molecule;
    for (auto cell : std::vector<core::HexCoord>{{0,0},{-1,0},{1,-1},{0,1}})
        molecule.atoms.push_back({core::AtomType::Salt, cell});
    for (std::size_t i = 1; i < molecule.atoms.size(); ++i)
        molecule.bonds.push_back({1, {0,0}, molecule.atoms[i].position});
    p.inputs = {molecule}; p.outputs = {molecule};
    auto material = [&](const io::PuzzleData& puzzle) {
        IR ir; auto state = IR::Initial(puzzle);
        for (int n = 0; n < 6; ++n) {
            auto id = state.nextId;
            ir.AppendFromTail(puzzle, state, {IR::Kind::Acquire, 0});
            ir.AppendFromTail(puzzle, state, {IR::Kind::Output, 0, {id}});
        }
        return ir;
    };
    auto ir = material(p);
    auto plan = MakeFiniteExecutionPlan(p, ir, {{6},{},6}, io::VerificationMode::LegacyGate);
    int candidates = 0;
    auto machine = CompileAcceptedSerialArmExecution(p, plan, [&](const auto&) { ++candidates; return true; });
    EXPECT_TRUE(machine); EXPECT_EQ(candidates, 1);
    auto denied = p; denied.permissionBits &= ~(1ULL << 0);
    candidates = 0;
    EXPECT_FALSE(CompileAcceptedSerialArmExecution(denied, plan, [&](const auto&) { ++candidates; return true; }));
    EXPECT_EQ(candidates, 0);
    unsetenv("OPUSOPTIMA_FRAGMENT_PRODUCT_DOCKING");
    EXPECT_THROW(MakeFiniteExecutionPlan(p, ir, {{6},{},6}, io::VerificationMode::LegacyGate), IR::Unsupported);
    setenv("OPUSOPTIMA_FRAGMENT_PRODUCT_DOCKING", "1", 1);
    EXPECT_THROW(MakeFiniteExecutionPlan(p, ir, {{6},{},6}, io::VerificationMode::Independent), IR::Unsupported);
    auto shifted = p;
    for (auto& atom : shifted.outputs[0].atoms) atom.position.q += 1;
    for (auto& bond : shifted.outputs[0].bonds) { bond.from.q += 1; bond.to.q += 1; }
    auto shiftedPlan = MakeFiniteExecutionPlan(shifted, material(shifted), {{6},{},6}, io::VerificationMode::LegacyGate);
    EXPECT_FALSE(CompileAcceptedSerialArmExecution(shifted, shiftedPlan, [](const auto&) { return true; }));
    if (savedFinite) setenv("OPUSOPTIMA_FINITE_RETENTION", savedFinite->c_str(), 1); else unsetenv("OPUSOPTIMA_FINITE_RETENTION");
    if (savedDock) setenv("OPUSOPTIMA_FRAGMENT_PRODUCT_DOCKING", savedDock->c_str(), 1); else unsetenv("OPUSOPTIMA_FRAGMENT_PRODUCT_DOCKING");
}
}

namespace opusoptima::solvers {
namespace {
void CheckWholeFeedDocking(int firstShape, int endShape) {
    using IR = ExecutionIR;
    using K = IR::Kind;
    test::ScopedTestDirectory scratch;
    const auto* savedEvidence = std::getenv("OPUSOPTIMA_DOCK_EVIDENCE_DIR");
    const auto directory = savedEvidence ? std::filesystem::path(savedEvidence) : scratch.Path();
    std::filesystem::create_directories(directory);
    for (int shape = firstShape; shape < endShape; ++shape)
        for (int variant = 0; variant < (shape >= 3 ? 1 : shape == 2 ? 2 : 3); ++variant) {
        io::PuzzleData p;
        p.version = 3; p.outputMultiplier = 1; p.permissionBits = ~0ULL;
        p.name = "whole-feed-dock-" + std::to_string(shape) + "-" + std::to_string(variant);
        const core::HexFrame frame{{variant * 2, -variant}, variant * 2};
        io::Molecule source;
        const std::vector<core::HexCoord> cells = shape == 0
            ? std::vector<core::HexCoord>{{0,0},{1,0},{2,0}}
            : std::vector<core::HexCoord>{{0,0},{1,0},{0,1}};
        for (std::size_t i = 0; i < cells.size(); ++i) {
            const auto type = static_cast<core::AtomType>(i + 2);
            source.atoms.push_back({type, frame.ToWorld(cells[i])});
            p.outputs.push_back({{{type, {0,0}}}, {}});
        }
        source.bonds = {{1, source.atoms[0].position, source.atoms[1].position},
                        {1, source.atoms[1].position, source.atoms[2].position}};
        if (shape) source.bonds.push_back({1, source.atoms[2].position, source.atoms[0].position});
        std::filesystem::path originalPath;
        if (shape >= 2) {
            const auto root = opusoptima::test::RepoRoot(__FILE__) /
                "asset/puzzle";
            originalPath = root / (shape == 2 ? "journal/journal9-p/P106.puzzle"
                : shape == 3 ? "24hour-1-test/GEN879.puzzle" : "custom/nonlinear-3 triangle 4.puzzle");
            const auto original = io::BinaryParser::ParsePuzzleFile(originalPath);
            ASSERT_EQ(original.inputs.size(), 1);
            source = original.inputs.front();
            p = original;
            if (shape == 2) {
                EXPECT_FALSE(SupportsWholeFeedDocking(original));
                ASSERT_EQ(source.atoms.size(), 7);
                ASSERT_EQ(source.bonds.size(), 9);
                p.name = "derived-enclosed-source-singleton-diagnostic-" + std::to_string(variant);
                p.outputs.clear();
            }
            for (auto& atom : source.atoms) {
                atom.position = frame.ToWorld(atom.position);
                if (shape == 2) p.outputs.push_back({{{atom.type, {0, 0}}}, {}});
            }
            for (auto& bond : source.bonds) {
                bond.from = frame.ToWorld(bond.from);
                bond.to = frame.ToWorld(bond.to);
            }
        }
        if (variant % 2) {
            std::reverse(source.atoms.begin(), source.atoms.end());
            std::reverse(source.bonds.begin(), source.bonds.end());
            for (auto& bond : source.bonds) std::swap(bond.from, bond.to);
        }
        p.inputs = {source};
        ASSERT_TRUE(SupportsWholeFeedDocking(p));
        const auto make = [&](bool cut) {
            IR ir; auto state = IR::Initial(p);
            ir.AppendFromTail(p, state, {K::Acquire, 0});
            if (cut) {
                const auto bonds = state.bonds;
                for (const auto& [edge, bits] : bonds)
                    ir.AppendFromTail(p, state, {K::Unbond, 0, {edge.a, edge.b}, bits});
            }
            for (std::size_t out = 0; out < p.outputs.size(); ++out) {
                const auto atom = std::find_if(state.atoms.begin(), state.atoms.end(), [&](const auto& entry) {
                    return entry.second.type == p.outputs[out].atoms[0].type;
                });
                ir.AppendFromTail(p, state, {K::Output, out, {atom->first}});
            }
            IR::RequireComplete(state, std::vector<std::size_t>(p.outputs.size(), 1));
            return ir;
        };
        const auto material = make(true);
        int submissions = 0, constructions = 0;
        std::string candidateLabel = p.name;
        std::string evidence;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
#ifdef HAS_LIBVERIFY
        io::VerificationReport gateReport;
        const auto puzzlePath = shape >= 3 ? originalPath : directory / (p.name + ".puzzle");
        if (shape < 3) WriteSerialPuzzle(puzzlePath, p);
        else std::filesystem::copy_file(originalPath, directory / (p.name + ".puzzle"),
                                        std::filesystem::copy_options::overwrite_existing);
#endif
        const auto accept = [&](const auto& candidate) {
            ++submissions;
#ifdef HAS_LIBVERIFY
            const auto path = directory / (candidateLabel + ".solution");
            io::BinaryWriter::WriteSolutionFile(path, candidate);
            io::VerificationOptions options;
            options.mode = io::VerificationMode::LegacyGate;
            options.outerDeadline = deadline;
            const auto native = io::VerifySolutionFiles(puzzlePath, path, options);
            gateReport = native;
            const auto report = io::VerificationReportJson(native);
            ::testing::Test::RecordProperty(candidateLabel, report);
            std::ofstream(directory / (candidateLabel + ".native.json")) << report << '\n';
            EXPECT_EQ(native.authority, io::VerificationAuthority::LegacyGate);
            EXPECT_TRUE(native.Passed()) << report;
            if (shape == 0 && variant == 0 && native.Passed()) {
                for (int fault = 0; fault < 3; ++fault) {
                    auto broken = candidate;
                    if (fault == 0) {
                        const auto glyph = std::find_if(broken.objects.begin(), broken.objects.end(),
                            [](const auto& object) { return object.typeName == "unbonder"; });
                        broken.objects.erase(glyph);
                    } else if (fault == 1) {
                        const auto arm = std::find_if(broken.objects.begin(), broken.objects.end(), [](const auto& object) {
                            return object.typeName == "arm1" && !object.instructions.empty() &&
                                   object.instructions.front().parameter == 5;
                        });
                        EXPECT_NE(arm, broken.objects.end());
                        if (arm == broken.objects.end()) return false;
                        arm->instructions.erase(arm->instructions.begin());
                    } else {
                        const auto input = std::find_if(broken.objects.begin(), broken.objects.end(),
                            [](const auto& object) { return object.typeName == "input"; });
                        input->position = broken.objects[1].position;
                    }
                    const auto negativePath = directory / (p.name + "-fault-" + std::to_string(fault) + ".solution");
                    io::BinaryWriter::WriteSolutionFile(negativePath, broken);
                    const auto negative = io::VerifySolutionFiles(puzzlePath, negativePath, options);
                    const auto negativeReport = io::VerificationReportJson(negative);
                    std::ofstream(directory / (p.name + "-fault-" + std::to_string(fault) + ".native.json"))
                        << negativeReport << '\n';
                    EXPECT_FALSE(negative.Passed()) << negativeReport;
                    EXPECT_EQ(negative.reasonCode, "reference-rejected") << negativeReport;
                }
            }
            return native.Passed();
#else
            (void)candidate;
            return true;
#endif
        };
        std::optional<io::SolutionData> machine;
#ifdef HAS_LIBVERIFY
        if (shape >= 3) {
            AssemblySolver solver;
            solver.SetDeadline(deadline);
            std::optional<io::SolutionData> first;
            machine = solver.CollectVerified(p, [&](const auto& candidate, const auto& stage) {
                if (!first) {
                    EXPECT_EQ(stage, "whole-feed-singleton-receivers");
                    first = candidate;
                }
                candidateLabel = p.name + "-" + stage;
                (void)accept(candidate);
                return gateReport;
            }, 3);
            ASSERT_TRUE(first);
            auto returned = *machine;
            returned.metrics = first->metrics = {};
            EXPECT_EQ(io::BinaryWriter::WriteSolution(returned), io::BinaryWriter::WriteSolution(*first));
            EXPECT_LE(submissions, 3);
        } else
#endif
        machine = CompileAcceptedWholeFeedDocking(p, material, accept,
            [&](const auto& message) { evidence += message + '\n'; }, deadline,
            [&] { ++constructions; return true; });
        if (shape < 3) {
            EXPECT_GE(constructions, 1);
            EXPECT_LE(constructions, 3);
            evidence += "constructions=" + std::to_string(constructions) +
                        " submissions=" + std::to_string(submissions) + '\n';
        }
        std::ofstream(directory / (p.name + ".compiler.txt")) << evidence;
        EXPECT_TRUE(machine) << p.name << ": " << evidence;
        if (!machine) continue;
        if (shape < 3) EXPECT_EQ(submissions, 1);
        else EXPECT_GE(submissions, 1);
        if (shape < 3) EXPECT_NE(evidence.find("scalar custody"), std::string::npos);
        if (shape == 2 && variant == 1) {
            int charged = 0, chargeRequests = 0;
            std::string limitedEvidence;
            EXPECT_FALSE(CompileAcceptedWholeFeedDocking(p, material, [](const auto&) {
                ADD_FAILURE() << "shared quota was bypassed"; return true;
            }, [&](const auto& detail) { limitedEvidence += detail + '\n'; }, deadline, [&] {
                ++chargeRequests;
                if (charged == 1) return false;
                ++charged;
                return true;
            }));
            EXPECT_EQ(charged, 1);
            EXPECT_EQ(chargeRequests, 2);
            EXPECT_NE(limitedEvidence.find("construction limit exhausted"), std::string::npos);
            std::ofstream(directory / (p.name + ".shared-quota.txt")) << limitedEvidence;
        }
        if (shape == 0 && variant == 0) {
            int charges = 0;
            EXPECT_FALSE(CompileAcceptedWholeFeedDocking(p, material, [](const auto&) {
                ADD_FAILURE() << "uncharged construction was submitted"; return true;
            }, {}, deadline, [&] { ++charges; return false; }));
            EXPECT_EQ(charges, 1);
            auto incompatible = p;
            incompatible.outputs[0].atoms[0].type = core::AtomType::Salt;
            EXPECT_FALSE(SupportsWholeFeedDocking(incompatible));
            incompatible = p;
            incompatible.inputs[0].bonds.clear();
            EXPECT_FALSE(SupportsWholeFeedDocking(incompatible));
            EXPECT_THROW((void)make(false), IR::Error);
            auto broken = material;
            broken.steps.erase(broken.steps.begin() + 1);
            EXPECT_FALSE(CompileAcceptedWholeFeedDocking(p, broken, [](const auto&) {
                ADD_FAILURE() << "missing cut was submitted"; return true;
            }, {}, deadline));
            auto denied = p; denied.permissionBits &= ~(1ULL << 9);
            EXPECT_FALSE(CompileAcceptedWholeFeedDocking(denied, material, [](const auto&) {
                ADD_FAILURE() << "unpermitted cut was submitted"; return true;
            }, {}, deadline));
            EXPECT_FALSE(CompileAcceptedWholeFeedDocking(p, material, [](const auto&) {
                return false;
            }, {}, deadline));
            EXPECT_FALSE(CompileAcceptedWholeFeedDocking(p, material, [](const auto&) {
                ADD_FAILURE() << "expired period was submitted"; return true;
            }, {}, std::chrono::steady_clock::now() - std::chrono::seconds(1)));
        }
    }
}
}
TEST(SerialArmExecution, WholeFeedDockingClosedScalarHandoff) {
    CheckWholeFeedDocking(0, 2);
}
TEST(SerialArmExecution, DerivedEnclosedSourceSingletonDiagnostic) {
    CheckWholeFeedDocking(2, 3);
}
TEST(SerialArmExecution, WholeFeedDockingOriginalClosedScalarReceivers) {
    const auto* value = std::getenv("OPUSOPTIMA_WHOLE_FEED_DOCKING");
    struct Restore {
        std::optional<std::string> prior;
        ~Restore() {
            if (prior) setenv("OPUSOPTIMA_WHOLE_FEED_DOCKING", prior->c_str(), 1);
            else unsetenv("OPUSOPTIMA_WHOLE_FEED_DOCKING");
        }
    } restore{value ? std::optional<std::string>(value) : std::nullopt};
    setenv("OPUSOPTIMA_WHOLE_FEED_DOCKING", "1", 1);
    CheckWholeFeedDocking(3, 5);
}
}
