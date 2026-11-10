#include "MaterialPlanning.hpp"
#include "BinaryWriter.hpp"
#include "CandidateArchive.hpp"
#include "InventoryIR.hpp"
#include "SerialArmExecution.hpp"
#include "SerialPortAccess.hpp"
#include "VerificationJson.hpp"
#include <cstdlib>

#include "RepoRoot.hpp"
#include <filesystem>
#include <gtest/gtest.h>
#include <iostream>

namespace opusoptima::solvers { namespace {
using T = core::AtomType;
struct ClosedLifeEnvironment {
    const std::vector<std::string> names{
        "OPUSOPTIMA_PRODUCT_PORT_ACCESS", "OPUSOPTIMA_CALCIFY_ANIMISMUS",
        "OPUSOPTIMA_SERIAL_ANIMISMUS", "OPUSOPTIMA_CLOSED_CARDINAL_LIFE"};
    std::vector<std::optional<std::string>> saved;
    ClosedLifeEnvironment() {
        for (const auto& name : names) {
            const auto* value = std::getenv(name.c_str());
            saved.push_back(value ? std::optional<std::string>(value) : std::nullopt);
            unsetenv(name.c_str());
        }
    }
    ~ClosedLifeEnvironment() {
        for (std::size_t i = 0; i < names.size(); ++i)
            if (saved[i])
                setenv(names[i].c_str(), saved[i]->c_str(), 1);
            else
                unsetenv(names[i].c_str());
    }
};

io::PuzzleData ClosedLifeOriginal() {
    return io::BinaryParser::ParsePuzzleFile(opusoptima::test::RepoRoot(__FILE__) /
                                             "asset/puzzle/24hour-1-puzzles/GEN873.puzzle");
}

TEST(MaterialPlanning, ClosedCardinalLifeGuardAndTypedPolicy) {
    ClosedLifeEnvironment environment;
    const auto original = ClosedLifeOriginal();
    ASSERT_TRUE(SupportsClosedCardinalLife(original));
    auto small = original;
    small.inputs = {{{{T::Earth, {0, 0}}, {T::Earth, {1, 0}}}, {{1, {0, 0}, {1, 0}}}}};
    small.outputs = {{{{T::Vitae, {2, -1}}, {T::Mors, {3, -1}}}, {{1, {2, -1}, {3, -1}}}}};
    ASSERT_TRUE(SupportsClosedCardinalLife(small));
    for (const auto& p : {original, small}) {
        EXPECT_FALSE(SearchMaterialExecution(p).plan);
        EXPECT_FALSE(
            SearchMaterialExecutionWithPolicy(p, SupplyMaterialLoweringPolicy::CutOrdinarySources)
                .plan);
        const auto planned =
            SearchMaterialExecutionWithPolicy(p, SupplyMaterialLoweringPolicy::ClosedCardinalLife);
        ASSERT_TRUE(planned.plan) << planned.detail;
        EXPECT_LE(planned.states, 4096U);
        EXPECT_LE(planned.candidates, 32U);
        EXPECT_LE(planned.plan->material.steps.size(), 512U);
        EXPECT_TRUE(SerialMaterialSourceCuts(planned.plan->material));
        const auto label = "atoms" + std::to_string(p.outputs[0].atoms.size());
        RecordProperty(label + "States", std::to_string(planned.states));
        RecordProperty(label + "Operations", std::to_string(planned.plan->material.steps.size()));
        RecordProperty(label + "Batches",
                       std::to_string(std::count_if(
                           planned.plan->material.steps.begin(), planned.plan->material.steps.end(),
                           [](const auto& step) {
                               return step.operation.kind == ExecutionIR::Kind::Acquire;
                           })));
        ExecutionIR::RequireComplete(planned.plan->material.Replay(p), {1});
        MaterialPlanningLimits limits;
        limits.maxSteps = 1;
        EXPECT_FALSE(SearchMaterialExecutionWithPolicy(
                         p, SupplyMaterialLoweringPolicy::ClosedCardinalLife, limits)
                         .plan);
    }
    const auto denied = [&](const auto& mutate) {
        auto p = small;
        mutate(p);
        EXPECT_FALSE(SupportsClosedCardinalLife(p));
        EXPECT_EQ(
            SearchMaterialExecutionWithPolicy(p, SupplyMaterialLoweringPolicy::ClosedCardinalLife)
                .status,
            MaterialPlanningStatus::Unsupported);
        EXPECT_EQ(SearchJointSupplyInventoryWithPolicy(
                      p, 1, SupplyMaterialLoweringPolicy::ClosedCardinalLife)
                      .states,
                  0U);
    };
    for (auto type : {T::Salt, T::Vitae, T::Gold, T::Quintessence})
        denied([&](auto& p) { p.inputs[0].atoms[0].type = type; });
    denied([](auto& p) {
        p.outputs[0].atoms.push_back({T::Fire, {4, -1}});
        p.outputs[0].bonds.push_back({1, {3, -1}, {4, -1}});
    });
    denied([](auto& p) { p.outputs[0].atoms[1].type = T::Vitae; });
    denied([](auto& p) { p.outputs[0].bonds[0].bits = 2; });
    denied([](auto& p) { p.inputs[0].bonds[0].bits = 2; });
    denied([](auto& p) { p.inputs[0].atoms.push_back({T::Earth, {2, 0}}); });
    denied([](auto& p) { p.productionInfo.emplace(); });
    denied([](auto& p) { p.outputMultiplier = 2; });
    denied([](auto& p) { p.inputs.resize(5, p.inputs[0]); });
    denied([](auto& p) { p.outputs[0].atoms[1].position = {2, -1}; });
    denied([](auto& p) { p.outputs[0].bonds.clear(); });
    denied([](auto& p) { p.outputs[0].bonds[0].to = {9, 9}; });
    denied([](auto& p) { p.outputs[0].atoms.resize(17, p.outputs[0].atoms[0]); });
    for (auto bit : {0, 8, 9, 12, 16, 22, 23, 25})
        denied([&](auto& p) { p.permissionBits &= ~(1ULL << bit); });
}

TEST(MaterialPlanning, ClosedCardinalLifeDefaultCollectionRequiresNativeAuthority) {
    ClosedLifeEnvironment environment;
    const auto puzzle = ClosedLifeOriginal();
    for (auto authority :
         {io::VerificationAuthority::None, io::VerificationAuthority::Independent}) {
        AssemblySolver solver;
        solver.SetDeadline(std::chrono::steady_clock::now() + std::chrono::seconds(8));
        EXPECT_THROW((void)solver.CollectVerified(
                         puzzle,
                         [&](const auto&, const auto& stage) {
                             io::VerificationReport report;
                             if (stage.starts_with("shared-plan-ordinary-port-access"))
                                 report.status = io::VerificationStatus::Passed;
                             report.authority = authority;
                             return report;
                         },
                         1),
                     std::invalid_argument);
    }
    for (const auto* value : {"off", "unknown", "", "1"}) {
        setenv("OPUSOPTIMA_CLOSED_CARDINAL_LIFE", value, 1);
        AssemblySolver disabled;
        EXPECT_THROW((void)disabled.CollectVerified(
                         puzzle,
                         [](const auto&, const auto& stage) {
                             EXPECT_FALSE(stage.starts_with("shared-plan-ordinary-port-access"));
                             return io::VerificationReport{};
                         },
                         1),
                     std::invalid_argument);
    }
    unsetenv("OPUSOPTIMA_CLOSED_CARDINAL_LIFE");
#ifdef HAS_LIBVERIFY
    const auto input =
        opusoptima::test::RepoRoot(__FILE__) / "asset/puzzle/24hour-1-puzzles/GEN873.puzzle";
    const auto originalBytes = io::BinaryParser::ReadAllBytes(input);
    struct Files {
        std::filesystem::path path =
            std::filesystem::path("build/test-output") /
            ("closed-life-" +
             std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        Files() {
            std::filesystem::create_directories(path);
        }
        ~Files() {
            std::error_code error;
            std::filesystem::remove_all(path, error);
        }
    } files;
    std::vector<std::byte> firstBytes;
    for (auto cap : {1U, 3U}) {
        io::CandidateArchive archive(input, files.path / (std::to_string(cap) + ".solution"), {});
        AssemblySolver solver;
        solver.SetDeadline(std::chrono::steady_clock::now() + std::chrono::seconds(8));
        std::size_t passed = 0, submissions = 0;
        if (cap == 3) setenv("OPUSOPTIMA_CLOSED_CARDINAL_LIFE", "on", 1);
        const auto submit = [&](const auto& candidate, const auto& stage) {
            ++submissions;
            auto report = archive.Submit(candidate, stage);
            RecordProperty("cap" + std::to_string(cap) + "stage" + std::to_string(submissions),
                           io::VerificationReportJson(report));
            if (report.Passed()) {
                ++passed;
                EXPECT_TRUE(stage.starts_with("shared-plan-ordinary-port-access")) << stage;
                EXPECT_EQ(report.authority, io::VerificationAuthority::LegacyGate);
            }
            return report;
        };
        const auto first = cap == 1 ? solver.CollectVerified(puzzle, submit, 1)
                                    : solver.CollectVerified(puzzle, submit);
        EXPECT_GE(passed, 1U);
        EXPECT_LE(passed, cap);
        EXPECT_LE(submissions, 12U);
        ASSERT_TRUE(archive.First());
        EXPECT_EQ(first.metrics.cycles, archive.First()->solution.metrics.cycles);
        if (cap == 1)
            firstBytes = archive.First()->bytes;
        else
            EXPECT_EQ(firstBytes, archive.First()->bytes);
        ASSERT_TRUE(archive.Publish({}).Passed());
    }
    EXPECT_EQ(originalBytes, io::BinaryParser::ReadAllBytes(input));
#endif
}

TEST(MaterialPlanning, ExplicitCutPolicyPreservesDefaultAndSharesOperationLimit) {
    using IR = ExecutionIR;
    io::PuzzleData p;
    p.version = 3;
    p.outputMultiplier = 1;
    p.permissionBits = ~0ULL;
    p.inputs = {{{{T::Earth, {0, 0}}, {T::Earth, {1, 0}}}, {{1, {0, 0}, {1, 0}}}}};
    p.outputs = p.inputs;
    const auto preserved = SearchMaterialExecution(p);
    const auto explicitPreserved =
        SearchMaterialExecutionWithPolicy(p, SupplyMaterialLoweringPolicy::PreserveCompatible);
    ASSERT_TRUE(preserved.plan);
    ASSERT_TRUE(explicitPreserved.plan);
    EXPECT_EQ(preserved.plan->material.steps, explicitPreserved.plan->material.steps);
    EXPECT_FALSE(SupportsClosedCardinalLife(p));
    const auto oldMachine =
        CompileAcceptedSerialArmExecution(p, *preserved.plan, [](const auto&) { return true; });
    const auto adaptedMachine = CompileAcceptedSerialArmExecutionWithPolicy(
        p, *explicitPreserved.plan, SupplyMaterialLoweringPolicy::PreserveCompatible,
        [](const auto&) { return true; });
    ASSERT_TRUE(oldMachine);
    ASSERT_TRUE(adaptedMachine);
    EXPECT_EQ(io::BinaryWriter::WriteSolution(*oldMachine),
              io::BinaryWriter::WriteSolution(*adaptedMachine));
    MaterialPlanningLimits limits;
    limits.maxSteps = 6;
    const auto cut = SearchMaterialExecutionWithPolicy(
        p, SupplyMaterialLoweringPolicy::CutOrdinarySources, limits);
    ASSERT_TRUE(cut.plan) << cut.detail;
    EXPECT_EQ(cut.plan->material.steps.size(), 6U);
    EXPECT_TRUE(SerialMaterialSourceCuts(cut.plan->material));
    IR::RequireComplete(cut.plan->material.Replay(p), {1});
    limits.maxSteps = 5;
    const auto capped = SearchMaterialExecutionWithPolicy(
        p, SupplyMaterialLoweringPolicy::CutOrdinarySources, limits);
    EXPECT_FALSE(capped.plan);
    EXPECT_LE(capped.states, limits.maxStates);
    EXPECT_LE(capped.candidates, limits.maxCandidates);
    EXPECT_NE(capped.detail.find("lowered material operation limit exceeded"), std::string::npos);
    p.inputs[0].atoms.pop_back();
    p.inputs[0].bonds.clear();
    p.permissionBits &= ~(1ULL << 9);
    EXPECT_TRUE(SupportsOrdinarySourceCutLowering(p));
    EXPECT_TRUE(
        SearchMaterialExecutionWithPolicy(p, SupplyMaterialLoweringPolicy::CutOrdinarySources)
            .plan);
}

TEST(MaterialPlanning, CalcifiedAnimismusIsExplicitlyOutsideCurrentExecutionChain) {
    io::PuzzleData p;
    p.version = 3;
    p.outputMultiplier = 1;
    p.permissionBits = ~0ULL;
    p.inputs = {{{{T::Earth, {0, 0}}}, {}}};
    p.outputs = {{{{T::Vitae, {0, 0}}}, {}}, {{{T::Mors, {0, 0}}}, {}}};
    int histories = 0;
    const auto absent = SearchMaterialExecution(p, {}, [&](const auto&) {
        ++histories;
        return false;
    });
    EXPECT_EQ(absent.status, MaterialPlanningStatus::NoCandidate);
    EXPECT_EQ(histories, 0);
    EXPECT_LE(absent.states, 4096U);

    p.inputs[0].atoms[0].type = T::Salt;
    p.permissionBits &= ~(1ULL << 17);
    const auto material = SearchMaterialExecution(p);
    ASSERT_EQ(material.status, MaterialPlanningStatus::Found) << material.detail;
    ASSERT_TRUE(material.plan);
    ExecutionIR::RequireComplete(material.plan->material.Replay(p), {1, 1});
    int machines = 0;
    std::string failure;
    EXPECT_FALSE(CompileAcceptedSerialArmExecution(
        p, *material.plan,
        [&](const auto&) {
            ++machines;
            return true;
        },
        [&](const auto& message) { failure = message; }));
    EXPECT_EQ(machines, 0);
    EXPECT_NE(failure.find("material reaction not yet implemented"), std::string::npos) << failure;

    p.permissionBits &= ~(1ULL << 16);
    EXPECT_NE(SearchMaterialExecution(p).status, MaterialPlanningStatus::Found);
    p.permissionBits |= 1ULL << 16;
    p.permissionBits &= ~(1ULL << 17);
    p.outputs.pop_back();
    EXPECT_NE(SearchMaterialExecution(p).status, MaterialPlanningStatus::Found);
}
TEST(MaterialPlanning, ExperimentalAnimismusRejectsDeniedAndWrongFeedPlans) {
    io::PuzzleData p;
    p.version = 3;
    p.outputMultiplier = 1;
    p.permissionBits = ~0ULL;
    p.inputs = {{{{T::Salt, {0, 0}}}, {}}};
    p.outputs = {{{{T::Vitae, {0, 0}}}, {}}, {{{T::Mors, {0, 0}}}, {}}};
    const auto material = SearchMaterialExecution(p);
    ASSERT_TRUE(material.plan);
    const char* prior = std::getenv("OPUSOPTIMA_SERIAL_ANIMISMUS");
    const bool present = prior != nullptr;
    const std::string old = prior ? prior : "";
    setenv("OPUSOPTIMA_SERIAL_ANIMISMUS", "1", 1);
    int machines = 0;
    auto refuse = [&](const io::PuzzleData& invalid) {
        EXPECT_FALSE(CompileAcceptedSerialArmExecution(invalid, *material.plan, [&](const auto&) {
            ++machines;
            return true;
        }));
    };
    auto denied = p;
    denied.permissionBits &= ~(1ULL << 16);
    refuse(denied);
    auto wrong = p;
    wrong.inputs[0].atoms[0].type = T::Earth;
    refuse(wrong);
    EXPECT_EQ(machines, 0);
    if (present)
        setenv("OPUSOPTIMA_SERIAL_ANIMISMUS", old.c_str(), 1);
    else
        unsetenv("OPUSOPTIMA_SERIAL_ANIMISMUS");
}
TEST(MaterialPlanning, PlansChemistryWithoutTrackPistonOrRailGeometry) {
    io::PuzzleData p;
    p.version = 3;
    p.outputMultiplier = 1;
    p.permissionBits = 1ULL << 14;
    p.inputs = {{{{T::Lead, {0, 0}}, {T::Quicksilver, {1, 0}}}, {{14, {0, 0}, {1, 0}}}},
                {{{T::Quicksilver, {0, 0}}}, {}}};
    p.outputs = {{{{T::Tin, {0, 0}}, {T::Quicksilver, {1, 0}}}, {{14, {0, 0}, {1, 0}}}}};
    const auto found = SearchMaterialExecution(p);
    ASSERT_EQ(found.status, MaterialPlanningStatus::Found) << found.detail;
    ASSERT_TRUE(found.plan);
    ExecutionIR::RequireComplete(found.plan->material.Replay(p), {1});
    EXPECT_LE(found.states, 4096U);
    EXPECT_EQ(p.permissionBits & ((1ULL << 2) | (1ULL << 3)), 0U);
    auto impossible = p;
    impossible.permissionBits = 0;
    const auto absent = SearchMaterialExecution(impossible);
    EXPECT_NE(absent.status, MaterialPlanningStatus::Found);
    EXPECT_FALSE(absent.plan);
    const auto zero = SearchMaterialExecution(p, {0, 1, 64, 16});
    EXPECT_EQ(zero.status, MaterialPlanningStatus::Exhausted);
    EXPECT_EQ(zero.states, 0U);
    EXPECT_THROW((void)SearchMaterialExecution(p, {},
                                               [](const auto&) -> bool {
                                                   throw std::runtime_error(
                                                       "material execution callback failed");
                                               }),
                 std::runtime_error);
}
TEST(MaterialPlanning, FirePrecursorBuildsNonFireColoredEndpointWithoutFakeCatalystFeed) {
    io::PuzzleData p;
    p.version = 3;
    p.outputMultiplier = 1;
    p.permissionBits = (1ULL << 11) | (1ULL << 12) | (1ULL << 13) | (1ULL << 28);
    p.inputs = {{{{T::Fire, {0, 0}}}, {}}};
    p.outputs = {{{{T::Water, {0, 0}}, {T::Fire, {-1, 1}}, {T::Fire, {1, 0}}, {T::Fire, {0, -1}}},
                  {{8, {0, 0}, {-1, 1}}, {8, {0, 0}, {1, 0}}, {8, {0, 0}, {0, -1}}}}};
    const auto found = SearchMaterialExecution(p);
    ASSERT_EQ(found.status, MaterialPlanningStatus::Found) << found.detail;
    ASSERT_TRUE(found.plan);
    const auto final = found.plan->material.Replay(p);
    ExecutionIR::RequireComplete(final, {1});
    EXPECT_EQ(final.batches, (std::vector<std::size_t>{4}));
    EXPECT_EQ(final.wheelDevices.size(), 1U);
    EXPECT_EQ(final.discarded, 0U);
    bool builtColor = false, calcified = false, duplicated = false;
    for (const auto& step : found.plan->material.steps) {
        if (step.operation.kind == ExecutionIR::Kind::Bond) {
            EXPECT_FALSE(calcified);
            builtColor = true;
        } else if (step.operation.kind == ExecutionIR::Kind::Calcify) {
            EXPECT_TRUE(builtColor);
            calcified = true;
        } else if (step.operation.kind == ExecutionIR::Kind::Duplicate) {
            EXPECT_TRUE(calcified);
            EXPECT_TRUE(step.operation.wheelCatalyst);
            duplicated = true;
        }
    }
    EXPECT_TRUE(duplicated);
    bool certified = false;
    EXPECT_THROW((void)LowerInventoryIR(
                     p, found.plan->material, {{0, 1}}, {0, 1, 4, 0},
                     [&](const auto&, const auto&, const auto&, char, bool) {
                         certified = true;
                         return true;
                     },
                     [&](const auto&, const auto&) {
                         certified = true;
                         return true;
                     }),
                 ExecutionIR::Unsupported);
    EXPECT_FALSE(certified);
    auto denied = p;
    denied.permissionBits &= ~(1ULL << 28);
    EXPECT_NE(SearchMaterialExecution(denied).status, MaterialPlanningStatus::Found);
    EXPECT_LE(found.states, 4096U);
}

TEST(MaterialPlanning, ClosedMaterialPeriodsSkipImpossibleBatchTotals) {
    io::PuzzleData p;
    p.version = 3;
    p.outputMultiplier = 1;
    p.permissionBits = (1ULL << 8) | (1ULL << 9);
    p.inputs = {{{{T::Salt, {0, 0}}, {T::Salt, {1, 0}}, {T::Salt, {2, 0}}}, {}}};
    p.outputs = {{{{T::Salt, {0, 0}}, {T::Salt, {1, 0}}, {T::Salt, {2, 0}}, {T::Salt, {3, 0}}},
                  {{1, {0, 0}, {1, 0}}, {1, {1, 0}, {2, 0}}, {1, {2, 0}, {3, 0}}}}};
    EXPECT_EQ(ClosedMaterialPeriodDivisor(p), 3U);
    for (const auto period : {1U, 2U}) {
        const auto impossible = SearchJointSupplyInventory(p, period);
        EXPECT_EQ(impossible.status, InventorySearchStatus::Rejected);
        EXPECT_EQ(impossible.states, 0U);
    }
    const auto found = SearchMaterialExecution(p);
    ASSERT_TRUE(found.plan) << found.detail;
    EXPECT_EQ(found.periods, (std::vector<std::size_t>{3}));
    const auto final = found.plan->material.Replay(p);
    ExecutionIR::RequireComplete(final, {3});
    EXPECT_EQ(final.batches, (std::vector<std::size_t>{4}));
    auto disposal = p;
    disposal.permissionBits |= 1ULL << 17;
    const auto surplus = SearchMaterialExecution(disposal);
    ASSERT_TRUE(surplus.plan) << surplus.detail;
    EXPECT_EQ(surplus.periods, (std::vector<std::size_t>{1}));
    const auto drained = surplus.plan->material.Replay(disposal);
    ExecutionIR::RequireComplete(drained, {1});
    EXPECT_EQ(drained.discarded, 2U);

    p.outputs = {{{{T::Salt, {0, 0}}}, {}}};
    p.inputs[0].atoms.push_back({T::Salt, {3, 0}});
    p.inputs[0].atoms.push_back({T::Salt, {4, 0}});
    const auto impossible = SearchMaterialExecution(p);
    EXPECT_EQ(impossible.status, MaterialPlanningStatus::NoCandidate);
    EXPECT_TRUE(impossible.periods.empty());
    EXPECT_EQ(impossible.states, 0U);

    p.inputs[0].atoms.resize(2);
    p.inputs[0].atoms[0].type = T::Quintessence;
    p.inputs[0].atoms[1].type = T::Air;
    p.outputs.push_back(p.outputs[0]);
    EXPECT_EQ(ClosedMaterialPeriodDivisor(p), 5U);
    auto multipleFeeds = p;
    multipleFeeds.inputs.push_back({{{T::Salt, {0, 0}}, {T::Salt, {1, 0}}}, {}});
    EXPECT_EQ(ClosedMaterialPeriodDivisor(multipleFeeds), 1U);
    p.permissionBits |= 1ULL << 17;
    EXPECT_FALSE(ClosedMaterialPeriodDivisor(p));
    p.permissionBits &= ~(1ULL << 17);
    for (const auto type : {T::Gold, T::Lead, T::Quicksilver, T::Repeat}) {
        auto excluded = p;
        excluded.inputs[0].atoms[0].type = type;
        EXPECT_FALSE(ClosedMaterialPeriodDivisor(excluded));
        excluded = p;
        excluded.outputs[0].atoms[0].type = type;
        EXPECT_FALSE(ClosedMaterialPeriodDivisor(excluded));
    }
    p.productionInfo.emplace();
    EXPECT_FALSE(ClosedMaterialPeriodDivisor(p));
}

TEST(MaterialPlanning, OriginalUnsolvedFixturesReceiveIndependentMaterialDiagnosis) {
    const auto root = opusoptima::test::RepoRoot(__FILE__);
    for (const auto* file : {"w2868339730", "w2946687073", "w2946687209"}) {
        const auto puzzle = io::BinaryParser::ParsePuzzleFile(
            root / "asset/puzzle/workshop-versions" / (std::string(file) + ".puzzle"));
        const auto result = SearchMaterialExecution(puzzle);
        EXPECT_NE(result.status, MaterialPlanningStatus::Unsupported) << result.detail;
        EXPECT_LE(result.states, 4096U);
        std::cout << file << " status=" << static_cast<int>(result.status)
                  << " states=" << result.states << " candidates=" << result.candidates
                  << " detail=" << result.detail << '\n';
        if (result.plan) {
            for (const auto& step : result.plan->material.steps) {
                std::cout << "  op=" << static_cast<int>(step.operation.kind)
                          << " index=" << step.operation.index << " atoms=";
                for (const auto id : step.operation.atoms)
                    std::cout << id << ',';
                std::cout << " new=" << step.preconditions.nextId;
                if (step.operation.kind == ExecutionIR::Kind::Relocate) {
                    for (auto id : ExecutionIR::Fragment(step.effects, step.operation.atoms[0])) {
                        const auto cell = step.effects.atoms.at(id).position;
                        std::cout << " atom" << id << "@" << cell.q << ',' << cell.r;
                    }
                }
                std::cout << '\n';
            }
            const auto final = result.plan->material.Replay(puzzle);
            EXPECT_TRUE(final.atoms.empty());
            EXPECT_TRUE(final.stored.empty());
        }
    }
}
}} // namespace opusoptima::solvers
