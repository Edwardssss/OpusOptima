#include "IO/BinaryWriter.hpp"
#include "IO/SolutionVerifier.hpp"
#include "LifeDeath.hpp"
#include "RepoRoot.hpp"
#include "RowAssembly.hpp"
#include "Verification/StaticProductionVerifier.hpp"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <random>
#include <set>
#include <stdexcept>

namespace opusoptima::solvers {
namespace {
const auto root = opusoptima::test::RepoRoot(__FILE__);

class ScopedTestDirectory {
    public:
    ScopedTestDirectory() {
        const auto base = root / "build/test-output/shared-ordinary";
        std::filesystem::create_directories(base);
        std::random_device random;
        for (int attempt = 0; attempt < 64; ++attempt) {
            path_ = base / (std::to_string(random()) + "-" + std::to_string(random()));
            if (std::filesystem::create_directory(path_)) return;
        }
        throw std::runtime_error("cannot allocate exclusive test output directory");
    }
    ~ScopedTestDirectory() {
        std::error_code error;
        std::filesystem::remove_all(path_, error);
        if (error) ADD_FAILURE() << "cannot clean test output directory: " << error.message();
    }
    ScopedTestDirectory(const ScopedTestDirectory&) = delete;
    ScopedTestDirectory& operator=(const ScopedTestDirectory&) = delete;
    const std::filesystem::path& Path() const {
        return path_;
    }

    private:
    std::filesystem::path path_;
};
const auto input = root / "asset/puzzle/workshop-versions/w2788066950.puzzle";

void RunMixedInventoryOriginalCompleteGate(bool nativeGate) {
    const auto originalPath = root / "asset/puzzle/journal/journal8/P102.puzzle";
    const auto original = io::BinaryParser::ParsePuzzleFile(originalPath);
    const ScopedTestDirectory scratch;
    const auto& dir = scratch.Path();
    for (int variant = 0; variant < 2; ++variant) {
        auto puzzle = original;
        auto puzzlePath = originalPath;
        const std::string stem = variant ? "transformed" : "original";
        if (variant) {
            puzzle.name += " transformed";
            for (auto& input : puzzle.inputs) {
                auto transform = [](core::HexCoord c) {
                    return core::Add(core::RotateClockwise(core::RotateClockwise(c)), {3, -2});
                };
                for (auto& atom : input.atoms)
                    atom.position = transform(atom.position);
                for (auto& bond : input.bonds) {
                    bond.from = transform(bond.from);
                    bond.to = transform(bond.to);
                }
                std::reverse(input.atoms.begin(), input.atoms.end());
            }
            std::reverse(puzzle.outputs.begin(), puzzle.outputs.end());
            puzzlePath = dir / "transformed.puzzle";
            std::ofstream out(puzzlePath, std::ios::binary);
            auto word = [&](std::uint64_t v, int bytes) {
                for (int i = 0; i < bytes; ++i)
                    out.put(static_cast<char>(v >> (8 * i)));
            };
            word(3, 4);
            word(puzzle.name.size(), 1);
            out.write(puzzle.name.data(), puzzle.name.size());
            word(0, 8);
            word(puzzle.permissionBits, 8);
            for (const auto* list : {&puzzle.inputs, &puzzle.outputs}) {
                word(list->size(), 4);
                for (const auto& molecule : *list) {
                    word(molecule.atoms.size(), 4);
                    for (const auto& atom : molecule.atoms) {
                        word(static_cast<std::uint8_t>(atom.type), 1);
                        word(atom.position.q, 1);
                        word(atom.position.r, 1);
                    }
                    word(molecule.bonds.size(), 4);
                    for (const auto& bond : molecule.bonds) {
                        word(bond.bits, 1);
                        word(bond.from.q, 1);
                        word(bond.from.r, 1);
                        word(bond.to.q, 1);
                        word(bond.to.r, 1);
                    }
                }
            }
            word(1, 4);
            word(0, 1);
        }
        const auto batch = CompileMixedInventoryAssembly(puzzle);
        const auto state = batch.ledger.Replay(puzzle);
        ExecutionIR::RequireComplete(state, {6, 6});
        EXPECT_EQ(state.batches, (std::vector<std::size_t>{12, 12}));
        EXPECT_EQ(state.discarded, 0);
        EXPECT_TRUE(state.stored.empty());
        EXPECT_EQ(batch.emissions.size(), 60);
        std::set<ExecutionIR::AtomId> identities;
        for (const auto& emission : batch.emissions) {
            EXPECT_TRUE(identities.insert(emission.atom).second);
            EXPECT_TRUE(emission.sourceIdentity || emission.reactionInputs[0]);
            EXPECT_LT(emission.readyCycle, emission.releaseCycle);
        }
        const auto solutionPath = dir / (stem + ".solution");
        io::BinaryWriter::WriteSolutionFile(solutionPath, batch.solution);
        if (nativeGate) {
            for (int pass = 0; pass < 2; ++pass) {
                const auto report =
                    io::VerifySolutionFiles(puzzlePath, solutionPath, 150000, 10000);
                std::ofstream(dir / (stem + "-gate-" + std::to_string(pass) + ".txt"))
                    << "passed=" << report.Passed() << " cost=" << report.metrics.cost
                    << " cycles=" << report.metrics.cycles << " area=" << report.metrics.area
                    << "\n"
                    << report.detail << " cycle=" << report.errorCycle << " cell=" << report.errorU
                    << ',' << report.errorV << '\n';
                ASSERT_TRUE(report.Passed()) << report.detail;
            }
        }
        if (!variant) {
            for (int bit : {2, 3, 9, 12, 16}) {
                auto denied = puzzle;
                denied.permissionBits &= ~(1ULL << bit);
                EXPECT_THROW(CompileMixedInventoryAssembly(denied), std::invalid_argument);
            }
            auto noDisposal = puzzle;
            noDisposal.permissionBits &= ~(1ULL << 17);
            const auto closed = CompileMixedInventoryAssembly(noDisposal);
            ExecutionIR::RequireComplete(closed.ledger.Replay(noDisposal), {6, 6});
            EXPECT_TRUE(std::none_of(
                closed.solution.objects.begin(), closed.solution.objects.end(),
                [](const auto& object) { return object.typeName == "glyph-disposal"; }));
            auto broken = batch.solution;
            for (auto& object : broken.objects)
                if (object.typeName == "piston" && object.position.r == 3) {
                    object.instructions.clear();
                    break;
                }
            const auto brokenPath = dir / "broken-buffer.solution";
            io::BinaryWriter::WriteSolutionFile(brokenPath, broken);
            if (nativeGate) {
                const auto rejected =
                    io::VerifySolutionFiles(puzzlePath, brokenPath, 150000, 10000);
                std::ofstream(dir / "broken-buffer-gate.txt")
                    << rejected.detail << " cycle=" << rejected.errorCycle;
                EXPECT_FALSE(rejected.Passed());
            }
        }
    }
}
TEST(SharedOrdinaryOutputTest, MixedInventoryOriginalCompleteGateSemantics) {
    RunMixedInventoryOriginalCompleteGate(false);
}
#ifdef HAS_LIBVERIFY
TEST(SharedOrdinaryOutputTest, MixedInventoryOriginalCompleteGate) {
    RunMixedInventoryOriginalCompleteGate(true);
}
#endif

void RunMixedInventoryCompactBufferCompleteGate(bool nativeGate) {
    const auto originalPath = root / "asset/puzzle/journal/journal8/P102.puzzle";
    const auto original = io::BinaryParser::ParsePuzzleFile(originalPath);
    const ScopedTestDirectory scratch;
    const auto& dir = scratch.Path();
    for (int variant = 0; variant < 4; ++variant) {
        auto puzzle = original;
        auto puzzlePath = originalPath;
        const std::string stem = variant == 3   ? "demand-permutation"
                                 : variant == 2 ? "holdout"
                                 : variant      ? "transformed"
                                                : "original";
        if (variant) {
            if (variant == 3) {
                for (auto& output : puzzle.outputs)
                    for (auto& atom : output.atoms) {
                        if (atom.type == core::AtomType::Vitae)
                            atom.type = core::AtomType::Mors;
                        else if (atom.type == core::AtomType::Mors)
                            atom.type = core::AtomType::Vitae;
                    }
            } else if (variant == 2) {
                std::reverse(puzzle.inputs.begin(), puzzle.inputs.end());
                for (auto& output : puzzle.outputs) {
                    std::reverse(output.atoms.begin(), output.atoms.end());
                    std::reverse(output.bonds.begin(), output.bonds.end());
                }
            } else {
                puzzle.name += " transformed";
                for (auto& input : puzzle.inputs) {
                    auto transform = [](core::HexCoord c) {
                        return core::Add(core::RotateClockwise(core::RotateClockwise(c)), {3, -2});
                    };
                    for (auto& atom : input.atoms)
                        atom.position = transform(atom.position);
                    for (auto& bond : input.bonds) {
                        bond.from = transform(bond.from);
                        bond.to = transform(bond.to);
                    }
                    std::reverse(input.atoms.begin(), input.atoms.end());
                }
                std::reverse(puzzle.outputs.begin(), puzzle.outputs.end());
            }
            puzzlePath = dir / (stem + ".puzzle");
            std::ofstream out(puzzlePath, std::ios::binary);
            auto word = [&](std::uint64_t v, int bytes) {
                for (int i = 0; i < bytes; ++i)
                    out.put(static_cast<char>(v >> (8 * i)));
            };
            word(3, 4);
            word(puzzle.name.size(), 1);
            out.write(puzzle.name.data(), puzzle.name.size());
            word(0, 8);
            word(puzzle.permissionBits, 8);
            for (const auto* list : {&puzzle.inputs, &puzzle.outputs}) {
                word(list->size(), 4);
                for (const auto& molecule : *list) {
                    word(molecule.atoms.size(), 4);
                    for (const auto& atom : molecule.atoms) {
                        word(static_cast<std::uint8_t>(atom.type), 1);
                        word(atom.position.q, 1);
                        word(atom.position.r, 1);
                    }
                    word(molecule.bonds.size(), 4);
                    for (const auto& bond : molecule.bonds) {
                        word(bond.bits, 1);
                        word(bond.from.q, 1);
                        word(bond.from.r, 1);
                        word(bond.to.q, 1);
                        word(bond.to.r, 1);
                    }
                }
            }
            word(1, 4);
            word(0, 1);
        }
        for (bool compact : {false, true}) {
            const std::string mode = compact ? "-on" : "-off";
            const auto batch = CompileMixedInventoryAssembly(puzzle, true, compact);
            const auto state = batch.ledger.Replay(puzzle);
            ExecutionIR::RequireComplete(state, {6, 6});
            EXPECT_TRUE(state.stored.empty());
            EXPECT_EQ(state.discarded, 0);
            std::set<ExecutionIR::AtomId> ids;
            for (const auto& emission : batch.emissions)
                EXPECT_TRUE(ids.insert(emission.atom).second);
            EXPECT_EQ(ids.size(), 60);
            std::ofstream phases(dir / (stem + mode + "-phases.csv"));
            phases << "product,phase,begin,end\n";
            for (const auto& phase : batch.phases)
                phases << phase.product << ',' << phase.phase << ',' << phase.begin << ','
                       << phase.end << '\n';
            phases << "-1,total,0," << batch.solution.metrics.cycles << '\n';
            const auto path = dir / (stem + mode + ".solution");
            io::BinaryWriter::WriteSolutionFile(path, batch.solution);
            if (nativeGate) {
                const auto report = io::VerifySolutionFiles(puzzlePath, path, 150000, 10000);
                std::ofstream(dir / (stem + mode + "-gate.txt"))
                    << "passed=" << report.Passed() << " cost=" << report.metrics.cost
                    << " cycles=" << report.metrics.cycles << " area=" << report.metrics.area << " "
                    << report.detail << " cycle=" << report.errorCycle << " cell=" << report.errorU
                    << ',' << report.errorV << '\n';
                EXPECT_TRUE(report.Passed()) << stem << mode << report.detail;
            }
            if (!variant && compact) {
                for (const int bit : {2, 3, 9, 12, 16}) {
                    auto denied = puzzle;
                    denied.permissionBits &= ~(std::uint64_t{1} << bit);
                    EXPECT_THROW(CompileMixedInventoryAssembly(denied, true, true),
                                 std::invalid_argument);
                }
                auto broken = batch.solution;
                for (auto& obj : broken.objects)
                    if (obj.typeName == "piston" && obj.position.r == 3) {
                        for (auto& ins : obj.instructions)
                            if (ins.action == 'a') {
                                ins.action = 'A';
                                break;
                            }
                        break;
                    }
                const auto negative = dir / "collision-negative.solution";
                io::BinaryWriter::WriteSolutionFile(negative, broken);
                if (nativeGate) {
                    const auto rejected =
                        io::VerifySolutionFiles(puzzlePath, negative, 150000, 10000);
                    std::ofstream(dir / "collision-negative.txt")
                        << rejected.Passed() << ' ' << rejected.detail
                        << " cycle=" << rejected.errorCycle << " cell=" << rejected.errorU << ','
                        << rejected.errorV << '\n';
                    EXPECT_FALSE(rejected.Passed());
                }
            }
        }
    }
}
TEST(SharedOrdinaryOutputTest, MixedInventoryCompactBufferCompleteGateSemantics) {
    RunMixedInventoryCompactBufferCompleteGate(false);
}
#ifdef HAS_LIBVERIFY
TEST(SharedOrdinaryOutputTest, MixedInventoryCompactBufferCompleteGate) {
    RunMixedInventoryCompactBufferCompleteGate(true);
}
#endif

void RunMixedInventoryBatchConsumerCompleteGate(bool nativeGate) {
    const auto originalPath = root / "asset/puzzle/journal/journal8/P102.puzzle";
    const auto original = io::BinaryParser::ParsePuzzleFile(originalPath);
    const ScopedTestDirectory scratch;
    const auto& dir = scratch.Path();
    for (int variant = 0; variant < 5; ++variant) {
        auto puzzle = original;
        auto puzzlePath = originalPath;
        const std::string stem = variant == 4   ? "new-holdout"
                                 : variant == 3 ? "demand-permutation"
                                 : variant == 2 ? "holdout"
                                 : variant      ? "transformed"
                                                : "original";
        if (variant) {
            if (variant == 4) {
                std::reverse(puzzle.inputs.begin(), puzzle.inputs.end());
                for (auto& input : puzzle.inputs) {
                    auto transform = [](core::HexCoord c) {
                        return core::Add(core::RotateClockwise(core::RotateClockwise(c)), {3, -2});
                    };
                    for (auto& atom : input.atoms)
                        atom.position = transform(atom.position);
                    for (auto& bond : input.bonds) {
                        bond.from = transform(bond.from);
                        bond.to = transform(bond.to);
                    }
                    std::reverse(input.atoms.begin(), input.atoms.end());
                }
                std::reverse(puzzle.outputs.begin(), puzzle.outputs.end());
            } else if (variant == 3) {
                for (auto& output : puzzle.outputs)
                    for (auto& atom : output.atoms) {
                        if (atom.type == core::AtomType::Vitae)
                            atom.type = core::AtomType::Mors;
                        else if (atom.type == core::AtomType::Mors)
                            atom.type = core::AtomType::Vitae;
                    }
            } else if (variant == 2) {
                std::reverse(puzzle.inputs.begin(), puzzle.inputs.end());
                for (auto& output : puzzle.outputs) {
                    std::reverse(output.atoms.begin(), output.atoms.end());
                    std::reverse(output.bonds.begin(), output.bonds.end());
                }
            } else {
                puzzle.name += " transformed";
                for (auto& input : puzzle.inputs) {
                    auto transform = [](core::HexCoord c) {
                        return core::Add(core::RotateClockwise(core::RotateClockwise(c)), {3, -2});
                    };
                    for (auto& atom : input.atoms)
                        atom.position = transform(atom.position);
                    for (auto& bond : input.bonds) {
                        bond.from = transform(bond.from);
                        bond.to = transform(bond.to);
                    }
                    std::reverse(input.atoms.begin(), input.atoms.end());
                }
                std::reverse(puzzle.outputs.begin(), puzzle.outputs.end());
            }
            puzzlePath = dir / (stem + ".puzzle");
            std::ofstream out(puzzlePath, std::ios::binary);
            auto word = [&](std::uint64_t v, int bytes) {
                for (int i = 0; i < bytes; ++i)
                    out.put(static_cast<char>(v >> (8 * i)));
            };
            word(3, 4);
            word(puzzle.name.size(), 1);
            out.write(puzzle.name.data(), puzzle.name.size());
            word(0, 8);
            word(puzzle.permissionBits, 8);
            for (const auto* list : {&puzzle.inputs, &puzzle.outputs}) {
                word(list->size(), 4);
                for (const auto& molecule : *list) {
                    word(molecule.atoms.size(), 4);
                    for (const auto& atom : molecule.atoms) {
                        word(static_cast<std::uint8_t>(atom.type), 1);
                        word(atom.position.q, 1);
                        word(atom.position.r, 1);
                    }
                    word(molecule.bonds.size(), 4);
                    for (const auto& bond : molecule.bonds) {
                        word(bond.bits, 1);
                        word(bond.from.q, 1);
                        word(bond.from.r, 1);
                        word(bond.to.q, 1);
                        word(bond.to.r, 1);
                    }
                }
            }
            word(1, 4);
            word(0, 1);
        }
        for (bool bypass : {false, true}) {
            const std::string mode = bypass ? "-on" : "-off";
            const auto batch = CompileMixedInventoryAssembly(puzzle, true, true, bypass);
            const auto state = batch.ledger.Replay(puzzle);
            ExecutionIR::RequireComplete(state, {6, 6});
            EXPECT_TRUE(state.stored.empty());
            EXPECT_EQ(state.discarded, 0);
            EXPECT_EQ(state.batches, (std::vector<std::size_t>{12, 12}));
            using K = ExecutionIR::Kind;
            const auto count = [&](K kind) {
                return std::count_if(batch.ledger.steps.begin(), batch.ledger.steps.end(),
                                     [&](const auto& step) { return step.operation.kind == kind; });
            };
            EXPECT_EQ(count(K::Acquire), 24);
            EXPECT_EQ(count(K::Animismus), 18);
            EXPECT_EQ(count(K::Store), bypass ? 90 : 96);
            EXPECT_EQ(count(K::Retrieve), bypass ? 90 : 96);
            std::set<ExecutionIR::AtomId> ids;
            for (const auto& emission : batch.emissions) {
                EXPECT_TRUE(ids.insert(emission.atom).second);
                EXPECT_LT(emission.beginCycle, emission.readyCycle);
                EXPECT_LT(emission.readyCycle, emission.releaseCycle);
            }
            EXPECT_EQ(ids.size(), 60);
            std::ofstream phases(dir / (stem + mode + "-phases.csv"));
            phases << "product,phase,begin,end\n";
            for (const auto& phase : batch.phases)
                phases << phase.product << ',' << phase.phase << ',' << phase.begin << ','
                       << phase.end << '\n';
            phases << "-1,total,0," << batch.solution.metrics.cycles << '\n';
            const auto path = dir / (stem + mode + ".solution");
            io::BinaryWriter::WriteSolutionFile(path, batch.solution);
            if (nativeGate) {
                const auto report = io::VerifySolutionFiles(puzzlePath, path, 150000, 10000);
                std::ofstream(dir / (stem + mode + "-gate.txt"))
                    << "passed=" << report.Passed() << " cost=" << report.metrics.cost
                    << " cycles=" << report.metrics.cycles << " area=" << report.metrics.area << " "
                    << report.detail << " cycle=" << report.errorCycle << " cell=" << report.errorU
                    << ',' << report.errorV << '\n';
                EXPECT_TRUE(report.Passed()) << stem << mode << report.detail;
            }
            if (!variant && bypass) {
                for (const int bit : {2, 3, 9, 12, 16}) {
                    auto denied = puzzle;
                    denied.permissionBits &= ~(std::uint64_t{1} << bit);
                    EXPECT_THROW(CompileMixedInventoryAssembly(denied, true, true, true),
                                 std::invalid_argument);
                }
                auto invalid = puzzle;
                invalid.outputMultiplier = 2;
                EXPECT_THROW(CompileMixedInventoryAssembly(invalid, true, true, true),
                             std::invalid_argument);
                invalid = puzzle;
                invalid.inputs[0].atoms[0].type = core::AtomType::Vitae;
                EXPECT_THROW(CompileMixedInventoryAssembly(invalid, true, true, true),
                             std::invalid_argument);
                auto broken = batch.solution;
                for (auto& obj : broken.objects)
                    if (obj.typeName == "piston" && obj.position.r == 3) {
                        for (auto& ins : obj.instructions)
                            if (ins.action == 'a') {
                                ins.action = 'A';
                                break;
                            }
                        break;
                    }
                const auto negative = dir / "collision-negative.solution";
                io::BinaryWriter::WriteSolutionFile(negative, broken);
                if (nativeGate) {
                    const auto rejected =
                        io::VerifySolutionFiles(puzzlePath, negative, 150000, 10000);
                    std::ofstream(dir / "collision-negative.txt")
                        << rejected.Passed() << ' ' << rejected.detail
                        << " cycle=" << rejected.errorCycle << " cell=" << rejected.errorU << ','
                        << rejected.errorV << '\n';
                    EXPECT_FALSE(rejected.Passed());
                }
            }
        }
    }
}
TEST(SharedOrdinaryOutputTest, MixedInventoryBatchConsumerCompleteGateSemantics) {
    RunMixedInventoryBatchConsumerCompleteGate(false);
}
#ifdef HAS_LIBVERIFY
TEST(SharedOrdinaryOutputTest, MixedInventoryBatchConsumerCompleteGate) {
    RunMixedInventoryBatchConsumerCompleteGate(true);
}
#endif

void RunNearCacheSwitchMatrix(bool nativeGate) {
    const auto puzzle =
        io::BinaryParser::ParsePuzzleFile(root / "asset/puzzle/journal/journal8/P102.puzzle");
    const ScopedTestDirectory scratch;
    const auto& dir = scratch.Path();
    for (bool scheduling : {false, true})
        for (bool compact : {false, true})
            for (bool bypass : {false, true})
                for (bool cache : {false, true}) {
                    if (cache && !compact) {
                        EXPECT_THROW(CompileMixedInventoryAssembly(puzzle, scheduling, compact,
                                                                   bypass, cache),
                                     std::invalid_argument);
                        continue;
                    }
                    auto batch =
                        CompileMixedInventoryAssembly(puzzle, scheduling, compact, bypass, cache);
                    const auto path =
                        dir / ("switch-" + std::to_string(scheduling) + std::to_string(compact) +
                               std::to_string(bypass) + std::to_string(cache) + ".solution");
                    io::BinaryWriter::WriteSolutionFile(path, batch.solution);
                    if (nativeGate) {
                        const auto report = io::VerifySolutionFiles(
                            root / "asset/puzzle/journal/journal8/P102.puzzle", path, 150000,
                            10000);
                        EXPECT_TRUE(report.Passed()) << path << report.detail;
                    }
                }
}
TEST(SharedOrdinaryOutputTest, NearCacheSwitchMatrixSemantics) {
    RunNearCacheSwitchMatrix(false);
}
#ifdef HAS_LIBVERIFY
TEST(SharedOrdinaryOutputTest, NearCacheSwitchMatrix) {
    RunNearCacheSwitchMatrix(true);
}
#endif

void RunMixedInventoryNearCacheCompleteGate(bool nativeGate) {
    const auto originalPath = root / "asset/puzzle/journal/journal8/P102.puzzle";
    const auto original = io::BinaryParser::ParsePuzzleFile(originalPath);
    const ScopedTestDirectory scratch;
    const auto& dir = scratch.Path();
    for (int variant = 0; variant < 6; ++variant) {
        auto puzzle = original;
        auto puzzlePath = originalPath;
        const std::string stem = variant == 5   ? "structural-holdout"
                                 : variant == 4 ? "new-holdout"
                                 : variant == 3 ? "demand-permutation"
                                 : variant == 2 ? "holdout"
                                 : variant      ? "transformed"
                                                : "original";
        if (variant) {
            if (variant == 5) {
                std::reverse(puzzle.outputs.begin(), puzzle.outputs.end());
            } else if (variant == 4) {
                std::reverse(puzzle.inputs.begin(), puzzle.inputs.end());
                for (auto& input : puzzle.inputs) {
                    auto transform = [](core::HexCoord c) {
                        return core::Add(core::RotateClockwise(core::RotateClockwise(c)), {3, -2});
                    };
                    for (auto& atom : input.atoms)
                        atom.position = transform(atom.position);
                    for (auto& bond : input.bonds) {
                        bond.from = transform(bond.from);
                        bond.to = transform(bond.to);
                    }
                    std::reverse(input.atoms.begin(), input.atoms.end());
                }
                std::reverse(puzzle.outputs.begin(), puzzle.outputs.end());
            } else if (variant == 3) {
                for (auto& output : puzzle.outputs)
                    for (auto& atom : output.atoms) {
                        if (atom.type == core::AtomType::Vitae)
                            atom.type = core::AtomType::Mors;
                        else if (atom.type == core::AtomType::Mors)
                            atom.type = core::AtomType::Vitae;
                    }
            } else if (variant == 2) {
                std::reverse(puzzle.inputs.begin(), puzzle.inputs.end());
                for (auto& output : puzzle.outputs) {
                    std::reverse(output.atoms.begin(), output.atoms.end());
                    std::reverse(output.bonds.begin(), output.bonds.end());
                }
            } else {
                puzzle.name += " transformed";
                for (auto& input : puzzle.inputs) {
                    auto transform = [](core::HexCoord c) {
                        return core::Add(core::RotateClockwise(core::RotateClockwise(c)), {3, -2});
                    };
                    for (auto& atom : input.atoms)
                        atom.position = transform(atom.position);
                    for (auto& bond : input.bonds) {
                        bond.from = transform(bond.from);
                        bond.to = transform(bond.to);
                    }
                    std::reverse(input.atoms.begin(), input.atoms.end());
                }
                std::reverse(puzzle.outputs.begin(), puzzle.outputs.end());
            }
            puzzlePath = dir / (stem + ".puzzle");
            std::ofstream out(puzzlePath, std::ios::binary);
            auto word = [&](std::uint64_t v, int bytes) {
                for (int i = 0; i < bytes; ++i)
                    out.put(static_cast<char>(v >> (8 * i)));
            };
            word(3, 4);
            word(puzzle.name.size(), 1);
            out.write(puzzle.name.data(), puzzle.name.size());
            word(0, 8);
            word(puzzle.permissionBits, 8);
            for (const auto* list : {&puzzle.inputs, &puzzle.outputs}) {
                word(list->size(), 4);
                for (const auto& molecule : *list) {
                    word(molecule.atoms.size(), 4);
                    for (const auto& atom : molecule.atoms) {
                        word(static_cast<std::uint8_t>(atom.type), 1);
                        word(atom.position.q, 1);
                        word(atom.position.r, 1);
                    }
                    word(molecule.bonds.size(), 4);
                    for (const auto& bond : molecule.bonds) {
                        word(bond.bits, 1);
                        word(bond.from.q, 1);
                        word(bond.from.r, 1);
                        word(bond.to.q, 1);
                        word(bond.to.r, 1);
                    }
                }
            }
            word(1, 4);
            word(0, 1);
        }
        for (bool nearCache : {false, true}) {
            const bool bypass = true;
            const std::string mode = nearCache ? "-on" : "-off";
            const auto batch = CompileMixedInventoryAssembly(puzzle, true, true, bypass, nearCache);
            const auto state = batch.ledger.Replay(puzzle);
            ExecutionIR::RequireComplete(state, {6, 6});
            EXPECT_TRUE(state.stored.empty());
            EXPECT_EQ(state.discarded, 0);
            EXPECT_EQ(state.batches, (std::vector<std::size_t>{12, 12}));
            using K = ExecutionIR::Kind;
            const auto count = [&](K kind) {
                return std::count_if(batch.ledger.steps.begin(), batch.ledger.steps.end(),
                                     [&](const auto& step) { return step.operation.kind == kind; });
            };
            EXPECT_EQ(count(K::Acquire), 24);
            EXPECT_EQ(count(K::Animismus), 18);
            EXPECT_EQ(count(K::Store), bypass ? 90 : 96);
            EXPECT_EQ(count(K::Retrieve), bypass ? 90 : 96);
            std::set<ExecutionIR::AtomId> ids;
            for (const auto& emission : batch.emissions) {
                EXPECT_TRUE(ids.insert(emission.atom).second);
                EXPECT_LT(emission.beginCycle, emission.readyCycle);
                EXPECT_LT(emission.readyCycle, emission.releaseCycle);
            }
            EXPECT_EQ(ids.size(), 60);
            std::ofstream phases(dir / (stem + mode + "-phases.csv"));
            phases << "product,phase,begin,end\n";
            for (const auto& phase : batch.phases)
                phases << phase.product << ',' << phase.phase << ',' << phase.begin << ','
                       << phase.end << '\n';
            phases << "-1,total,0," << batch.solution.metrics.cycles << '\n';
            const auto path = dir / (stem + mode + ".solution");
            io::BinaryWriter::WriteSolutionFile(path, batch.solution);
            if (nativeGate) {
                const auto report = io::VerifySolutionFiles(puzzlePath, path, 150000, 10000);
                std::ofstream(dir / (stem + mode + "-gate.txt"))
                    << "passed=" << report.Passed() << " cost=" << report.metrics.cost
                    << " cycles=" << report.metrics.cycles << " area=" << report.metrics.area << " "
                    << report.detail << " cycle=" << report.errorCycle << " cell=" << report.errorU
                    << ',' << report.errorV << '\n';
                EXPECT_TRUE(report.Passed()) << stem << mode << report.detail;
            }
            if (!variant && nearCache) {
                if (nativeGate) {
                    EXPECT_FALSE(io::VerifySolutionFiles(puzzlePath, path, 100, 10000).Passed());
                    io::VerificationOptions cancelled;
                    cancelled.outerDeadline = std::chrono::steady_clock::now();
                    EXPECT_FALSE(io::VerifySolutionFiles(puzzlePath, path, cancelled).Passed());
                }
                auto expanded = puzzle;
                expanded.inputs[0].atoms[0].position = {0, 20};
                EXPECT_THROW(CompileMixedInventoryAssembly(expanded, true, true, true, true),
                             std::invalid_argument);
                for (const int bit : {2, 3, 9, 12, 16}) {
                    auto denied = puzzle;
                    denied.permissionBits &= ~(std::uint64_t{1} << bit);
                    EXPECT_THROW(CompileMixedInventoryAssembly(denied, true, true, true, true),
                                 std::invalid_argument);
                }
                auto invalid = puzzle;
                invalid.outputMultiplier = 2;
                EXPECT_THROW(CompileMixedInventoryAssembly(invalid, true, true, true, true),
                             std::invalid_argument);
                invalid = puzzle;
                invalid.inputs[0].atoms[0].type = core::AtomType::Vitae;
                EXPECT_THROW(CompileMixedInventoryAssembly(invalid, true, true, true, true),
                             std::invalid_argument);
                auto broken = batch.solution;
                for (auto& obj : broken.objects)
                    if (obj.typeName == "piston" && obj.position.r == 3) {
                        for (auto& ins : obj.instructions)
                            if (ins.action == 'a') {
                                ins.action = 'A';
                                break;
                            }
                        break;
                    }
                const auto negative = dir / "collision-negative.solution";
                io::BinaryWriter::WriteSolutionFile(negative, broken);
                if (nativeGate) {
                    const auto rejected =
                        io::VerifySolutionFiles(puzzlePath, negative, 150000, 10000);
                    std::ofstream(dir / "collision-negative.txt")
                        << rejected.Passed() << ' ' << rejected.detail
                        << " cycle=" << rejected.errorCycle << " cell=" << rejected.errorU << ','
                        << rejected.errorV << '\n';
                    EXPECT_FALSE(rejected.Passed());
                }
            }
        }
    }
}
TEST(SharedOrdinaryOutputTest, MixedInventoryNearCacheCompleteGateSemantics) {
    RunMixedInventoryNearCacheCompleteGate(false);
}
#ifdef HAS_LIBVERIFY
TEST(SharedOrdinaryOutputTest, MixedInventoryNearCacheCompleteGate) {
    RunMixedInventoryNearCacheCompleteGate(true);
}
#endif

void RunMixedInventorySchedulingCompleteGate(bool nativeGate) {
    const auto originalPath = root / "asset/puzzle/journal/journal8/P102.puzzle";
    const auto original = io::BinaryParser::ParsePuzzleFile(originalPath);
    const ScopedTestDirectory scratch;
    const auto& dir = scratch.Path();
    for (int variant = 0; variant < 3; ++variant) {
        auto puzzle = original;
        auto puzzlePath = originalPath;
        const std::string stem = variant == 2 ? "holdout" : variant ? "transformed" : "original";
        if (variant) {
            if (variant == 2) {
                std::reverse(puzzle.inputs.begin(), puzzle.inputs.end());
                for (auto& output : puzzle.outputs) {
                    std::reverse(output.atoms.begin(), output.atoms.end());
                    std::reverse(output.bonds.begin(), output.bonds.end());
                }
            } else {
                puzzle.name += " transformed";
                for (auto& input : puzzle.inputs) {
                    auto transform = [](core::HexCoord c) {
                        return core::Add(core::RotateClockwise(core::RotateClockwise(c)), {3, -2});
                    };
                    for (auto& atom : input.atoms)
                        atom.position = transform(atom.position);
                    for (auto& bond : input.bonds) {
                        bond.from = transform(bond.from);
                        bond.to = transform(bond.to);
                    }
                    std::reverse(input.atoms.begin(), input.atoms.end());
                }
                std::reverse(puzzle.outputs.begin(), puzzle.outputs.end());
            }
            puzzlePath = dir / (stem + ".puzzle");
            std::ofstream out(puzzlePath, std::ios::binary);
            auto word = [&](std::uint64_t v, int bytes) {
                for (int i = 0; i < bytes; ++i)
                    out.put(static_cast<char>(v >> (8 * i)));
            };
            word(3, 4);
            word(puzzle.name.size(), 1);
            out.write(puzzle.name.data(), puzzle.name.size());
            word(0, 8);
            word(puzzle.permissionBits, 8);
            for (const auto* list : {&puzzle.inputs, &puzzle.outputs}) {
                word(list->size(), 4);
                for (const auto& molecule : *list) {
                    word(molecule.atoms.size(), 4);
                    for (const auto& atom : molecule.atoms) {
                        word(static_cast<std::uint8_t>(atom.type), 1);
                        word(atom.position.q, 1);
                        word(atom.position.r, 1);
                    }
                    word(molecule.bonds.size(), 4);
                    for (const auto& bond : molecule.bonds) {
                        word(bond.bits, 1);
                        word(bond.from.q, 1);
                        word(bond.from.r, 1);
                        word(bond.to.q, 1);
                        word(bond.to.r, 1);
                    }
                }
            }
            word(1, 4);
            word(0, 1);
        }
        for (bool scheduled : {false, true}) {
            const std::string mode = scheduled ? "-on" : "-off";
            const auto baseline = CompileMixedInventoryAssembly(puzzle);
            const auto batch = CompileMixedInventoryAssembly(puzzle, scheduled);
            EXPECT_EQ(batch.solution.objects.size(), baseline.solution.objects.size());
            for (std::size_t i = 0; i < baseline.solution.objects.size(); ++i) {
                EXPECT_EQ(batch.solution.objects[i].typeName,
                          baseline.solution.objects[i].typeName);
                EXPECT_EQ(batch.solution.objects[i].position,
                          baseline.solution.objects[i].position);
                EXPECT_EQ(batch.solution.objects[i].bodyData,
                          baseline.solution.objects[i].bodyData);
                EXPECT_EQ(batch.solution.objects[i].instructions.size(),
                          baseline.solution.objects[i].instructions.size());
                for (std::size_t j = 0; j < baseline.solution.objects[i].instructions.size(); ++j)
                    EXPECT_EQ(batch.solution.objects[i].instructions[j].action,
                              baseline.solution.objects[i].instructions[j].action);
            }
            if (scheduled)
                EXPECT_LT(batch.solution.metrics.cycles, baseline.solution.metrics.cycles);
            std::ofstream(dir / (stem + mode + "-time-ledger.txt"))
                << "serial=" << baseline.solution.metrics.cycles
                << " scheduled=" << batch.solution.metrics.cycles
                << " empty_return_tasks=" << batch.emptyReturnTasks
                << " empty_return_cycles=" << batch.emptyReturnCycles
                << " overlap=" << baseline.solution.metrics.cycles - batch.solution.metrics.cycles
                << '\n';
            const auto state = batch.ledger.Replay(puzzle);
            ExecutionIR::RequireComplete(state, {6, 6});
            EXPECT_EQ(state.batches, (std::vector<std::size_t>{12, 12}));
            EXPECT_EQ(state.discarded, 0);
            EXPECT_TRUE(state.stored.empty());
            EXPECT_EQ(batch.emissions.size(), 60);
            std::set<ExecutionIR::AtomId> identities;
            for (const auto& emission : batch.emissions) {
                EXPECT_TRUE(identities.insert(emission.atom).second);
                EXPECT_TRUE(emission.sourceIdentity || emission.reactionInputs[0]);
                EXPECT_LT(emission.readyCycle, emission.releaseCycle);
            }
            const auto solutionPath = dir / (stem + mode + ".solution");
            io::BinaryWriter::WriteSolutionFile(solutionPath, batch.solution);
            for (int pass = 0; pass < 2; ++pass) {
                if (nativeGate) {
                    const auto report =
                        io::VerifySolutionFiles(puzzlePath, solutionPath, 150000, 10000);
                    std::ofstream(dir / (stem + mode + "-gate-" + std::to_string(pass) + ".txt"))
                        << "passed=" << report.Passed() << " cost=" << report.metrics.cost
                        << " cycles=" << report.metrics.cycles << " area=" << report.metrics.area
                        << "\n"
                        << report.detail << " cycle=" << report.errorCycle
                        << " cell=" << report.errorU << ',' << report.errorV << '\n';
                    ASSERT_TRUE(report.Passed()) << report.detail;
                }
            }
            if (!variant && scheduled) {
                auto brokenReturn = batch.solution;
                bool changed = false;
                for (auto& object : brokenReturn.objects)
                    if (object.typeName == "piston" && object.position.r == 3) {
                        for (auto& instruction : object.instructions)
                            if (instruction.action == 'a') {
                                instruction.action = 'A';
                                changed = true;
                                break;
                            }
                        break;
                    }
                ASSERT_TRUE(changed);
                const auto path = dir / "broken-return.solution";
                io::BinaryWriter::WriteSolutionFile(path, brokenReturn);
                if (nativeGate) {
                    const auto rejected = io::VerifySolutionFiles(puzzlePath, path, 150000, 10000);
                    std::ofstream(dir / "broken-return-gate.txt")
                        << "passed=" << rejected.Passed() << " " << rejected.detail
                        << " cycle=" << rejected.errorCycle << " cell=" << rejected.errorU << ','
                        << rejected.errorV;
                    EXPECT_FALSE(rejected.Passed());
                }
            }
            if (!variant) {
                for (int bit : {2, 3, 9, 12, 16}) {
                    auto denied = puzzle;
                    denied.permissionBits &= ~(1ULL << bit);
                    EXPECT_THROW(CompileMixedInventoryAssembly(denied), std::invalid_argument);
                }
                auto noDisposal = puzzle;
                noDisposal.permissionBits &= ~(1ULL << 17);
                const auto closed = CompileMixedInventoryAssembly(noDisposal);
                ExecutionIR::RequireComplete(closed.ledger.Replay(noDisposal), {6, 6});
                EXPECT_TRUE(std::none_of(
                    closed.solution.objects.begin(), closed.solution.objects.end(),
                    [](const auto& object) { return object.typeName == "glyph-disposal"; }));
                auto broken = batch.solution;
                for (auto& object : broken.objects)
                    if (object.typeName == "piston" && object.position.r == 3) {
                        object.instructions.clear();
                        break;
                    }
                const auto brokenPath = dir / "broken-buffer.solution";
                io::BinaryWriter::WriteSolutionFile(brokenPath, broken);
                if (nativeGate) {
                    const auto rejected =
                        io::VerifySolutionFiles(puzzlePath, brokenPath, 150000, 10000);
                    std::ofstream(dir / "broken-buffer-gate.txt")
                        << rejected.detail << " cycle=" << rejected.errorCycle;
                    EXPECT_FALSE(rejected.Passed());
                }
            }
        }
    }
}
TEST(SharedOrdinaryOutputTest, MixedInventorySchedulingCompleteGateSemantics) {
    RunMixedInventorySchedulingCompleteGate(false);
}
#ifdef HAS_LIBVERIFY
TEST(SharedOrdinaryOutputTest, MixedInventorySchedulingCompleteGate) {
    RunMixedInventorySchedulingCompleteGate(true);
}
#endif

void RunRealMixedFragmentConsumption(bool nativeGate) {
    using T = core::AtomType;
    const auto original =
        io::BinaryParser::ParsePuzzleFile(root / "asset/puzzle/journal/journal8/P102.puzzle");
    const ScopedTestDirectory scratch;
    const auto& dir = scratch.Path();
    for (int variant = 0; variant < 4; ++variant) {
        auto puzzle = original;
        puzzle.name = "real-mixed-fragment-" + std::to_string(variant);
        puzzle.inputs.resize(1);
        puzzle.outputs[0].atoms = {{T::Salt, {-1, 0}}, {T::Water, {0, 0}}};
        puzzle.outputs[0].bonds = {{1, {-1, 0}, {0, 0}}};
        puzzle.outputs[1].atoms = {{T::Salt, {0, 0}}};
        puzzle.outputs[1].bonds.clear();
        if (variant == 1) {
            for (auto& atom : puzzle.inputs[0].atoms)
                atom.position =
                    core::Add(core::RotateClockwise(core::RotateClockwise(atom.position)), {3, -2});
            for (auto& bond : puzzle.inputs[0].bonds) {
                bond.from =
                    core::Add(core::RotateClockwise(core::RotateClockwise(bond.from)), {3, -2});
                bond.to = core::Add(core::RotateClockwise(core::RotateClockwise(bond.to)), {3, -2});
            }
            std::reverse(puzzle.inputs[0].atoms.begin(), puzzle.inputs[0].atoms.end());
            std::reverse(puzzle.outputs.begin(), puzzle.outputs.end());
        }
        if (variant == 2) puzzle.outputs[0].atoms[1].type = T::Salt;
        if (variant == 3) {
            puzzle.inputs[0] = original.inputs[1];
            puzzle.outputs[0].atoms = {{T::Earth, {0, 0}}};
            puzzle.outputs[0].bonds.clear();
        }
        const auto batch = CompileMolecularFragmentSupplyBatch(puzzle);
        EXPECT_EQ(batch.emissions.size(), variant == 3 ? 12 : 18);
        const auto state = batch.ledger.Replay(puzzle);
        EXPECT_EQ(state.batches[0], 6);
        EXPECT_EQ(state.discarded, 0);
        ExecutionIR::RequireComplete(state, {6, 6});
        for (int bit : {2, 3, 9}) {
            auto denied = puzzle;
            denied.permissionBits &= ~(1ULL << bit);
            EXPECT_THROW(CompileMolecularFragmentSupplyBatch(denied), std::invalid_argument);
        }
        auto malformed = puzzle;
        malformed.inputs[0].bonds[0].bits = 2;
        EXPECT_THROW(CompileMolecularFragmentSupplyBatch(malformed), std::invalid_argument);
        if (variant == 2) {
            auto denied = puzzle;
            denied.permissionBits &= ~(1ULL << 12);
            EXPECT_THROW(CompileMolecularFragmentSupplyBatch(denied), std::invalid_argument);
        }
        const auto path = dir / (puzzle.name + ".puzzle");
        const auto solutionPath = dir / (puzzle.name + ".solution");
        {
            std::ofstream trace(dir / (puzzle.name + ".emissions.csv"));
            trace << "sourceBatch,sourceInput,sourceAtom,sourceIdentity,remaining,atom,type,output,"
                     "outputAtom,product,begin,holderRelease,ready,release,reservationEnd\n";
            for (const auto& e : batch.emissions)
                trace << e.sourceBatch << ',' << e.sourceInput << ',' << e.sourceAtom << ','
                      << e.sourceIdentity << ',' << e.sourceRemainder << ',' << e.atom << ','
                      << static_cast<int>(e.type) << ',' << e.consumer.output << ','
                      << e.consumer.atom << ',' << e.product << ',' << e.beginCycle << ','
                      << e.holderReleaseCycle << ',' << e.readyCycle << ',' << e.releaseCycle << ','
                      << e.reservationEnd << '\n';
        }
        {
            std::ofstream out(path, std::ios::binary);
            auto word = [&](std::uint64_t value, int n) {
                for (int i = 0; i < n; ++i)
                    out.put(static_cast<char>(value >> (8 * i)));
            };
            word(3, 4);
            word(puzzle.name.size(), 1);
            out.write(puzzle.name.data(), puzzle.name.size());
            word(0, 8);
            word(puzzle.permissionBits, 8);
            for (const auto* list : {&puzzle.inputs, &puzzle.outputs}) {
                word(list->size(), 4);
                for (const auto& molecule : *list) {
                    word(molecule.atoms.size(), 4);
                    for (const auto& atom : molecule.atoms) {
                        word(static_cast<std::uint8_t>(atom.type), 1);
                        word(atom.position.q, 1);
                        word(atom.position.r, 1);
                    }
                    word(molecule.bonds.size(), 4);
                    for (const auto& bond : molecule.bonds) {
                        word(bond.bits, 1);
                        word(bond.from.q, 1);
                        word(bond.from.r, 1);
                        word(bond.to.q, 1);
                        word(bond.to.r, 1);
                    }
                }
            }
            word(1, 4);
            word(0, 1);
        }
        auto solution = batch.solution;
        io::BinaryWriter::WriteSolutionFile(solutionPath, solution);
        if (nativeGate) {
            const auto report = io::VerifySolutionFiles(path, solutionPath, 150000, 10000);
            solution.metrics = report.metrics;
            ASSERT_TRUE(report.Passed())
                << "variant=" << variant << " " << report.detail << " cycle=" << report.errorCycle;
            solution.metrics = report.metrics;
            io::BinaryWriter::WriteSolutionFile(solutionPath, solution);
            EXPECT_TRUE(io::VerifySolutionFiles(path, solutionPath, 150000, 10000).Passed());
            std::cout << "REAL-MIXED-FRAGMENT variant=" << variant
                      << " cost=" << report.metrics.cost << " cycles=" << report.metrics.cycles
                      << " area=" << report.metrics.area << '\n';
        }
    }
    EXPECT_THROW(CompileMolecularFragmentSupplyBatch(original), std::invalid_argument);
}
TEST(SharedOrdinaryOutputTest, RealMixedFragmentConsumptionSemantics) {
    RunRealMixedFragmentConsumption(false);
}
#ifdef HAS_LIBVERIFY
TEST(SharedOrdinaryOutputTest, RealMixedFragmentConsumption) {
    RunRealMixedFragmentConsumption(true);
}
#endif

TEST(SharedOrdinaryOutputTest, MolecularBatchBoundariesAreExclusiveAndIncludeCleanup) {
    auto puzzle =
        io::BinaryParser::ParsePuzzleFile(root / "asset/puzzle/tourney-2019/week3.puzzle");
    puzzle.outputs.resize(1);
    puzzle.outputs[0].atoms = {{core::AtomType::Silver, {0, 0}}};
    puzzle.outputs[0].bonds.clear();
    puzzle.outputMultiplier = 1;
    const auto supply = AssemblyPlanner::PlanSupply(puzzle);
    ASSERT_GT(supply.productsPerPeriod, 1);
    std::vector<int> ends;
    const auto batch = CompileRowAssemblyBatch(puzzle, AssemblyPlanner::Plan(puzzle), ends);
    ASSERT_EQ(ends.size(), supply.productsPerPeriod);
    int previous = 0;
    for (int end : ends) {
        EXPECT_GT(end, previous);
        previous = end;
    }
    int duration = 0;
    for (const auto& part : batch.objects)
        for (const auto& instruction : part.instructions)
            duration = std::max(duration, instruction.parameter + 1);
    EXPECT_LE(ends.back(), duration);
    std::size_t instructions = 0, sliced = 0;
    for (const auto& part : batch.objects)
        for (const auto& instruction : part.instructions) {
            ++instructions;
            int start = 0;
            for (std::size_t i = 0; i < ends.size(); ++i) {
                const int end = i + 1 == ends.size() ? duration : ends[i];
                if (instruction.parameter >= start && instruction.parameter < end) ++sliced;
                start = end;
            }
        }
    EXPECT_EQ(sliced, instructions);
}

TEST(SharedOrdinaryOutputTest, ProductIdentityAndGlobalRecipe) {
    const auto puzzle = io::BinaryParser::ParsePuzzleFile(input);
    const auto shared = BuildSharedAssemblyTarget(puzzle);
    ASSERT_EQ(shared.atoms.size(), 12);
    for (std::size_t i = 0; i < shared.atoms.size(); ++i) {
        const auto ref = shared.atoms[i];
        EXPECT_EQ(shared.puzzle.outputs[0].atoms[i].type,
                  puzzle.outputs[ref.output].atoms[ref.atom].type);
        EXPECT_EQ(ref.output, i / 6);
        EXPECT_EQ(ref.atom, i % 6);
    }
    const auto recipe = ChemicalPlanner::Plan(puzzle);
    EXPECT_EQ(recipe.inputBatches, ChemicalPlanner::Plan(shared.puzzle).inputBatches);
    EXPECT_THROW(AssemblyPlanner::Plan(shared.puzzle), std::invalid_argument);
}

TEST(SharedOrdinaryOutputTest, CommonStockClosesWithoutDisposal) {
    const auto shared = BuildSharedAssemblyTarget(io::BinaryParser::ParsePuzzleFile(input));
    EXPECT_EQ(shared.puzzle.permissionBits & (1ULL << 17), 0);
    const auto supply = AssemblyPlanner::PlanSupply(shared.puzzle, true);
    std::vector<std::size_t> takes(shared.atoms.size());
    std::size_t batches = 0;
    for (const auto& usage : supply.usage) {
        EXPECT_EQ(usage.discarded, 0);
        batches += usage.batches;
    }
    EXPECT_EQ(batches, 4 * supply.productsPerPeriod);
    for (const auto& step : supply.steps) {
        EXPECT_NE(step.kind, SupplyPlan::Kind::Discard);
        if (step.kind == SupplyPlan::Kind::Take) ++takes.at(step.targetAtom);
    }
    for (const auto count : takes)
        EXPECT_EQ(count, supply.productsPerPeriod);
}

TEST(SharedOrdinaryOutputTest, UnequalRowSpansShareBottomAlignment) {
    auto puzzle = io::BinaryParser::ParsePuzzleFile(input);
    puzzle.outputs[1].atoms.resize(2);
    puzzle.outputs[1].atoms[0].position = {0, 0};
    puzzle.outputs[1].atoms[1].position = {1, 0};
    puzzle.outputs[1].bonds.resize(1);
    puzzle.outputs[1].bonds[0].from = {0, 0};
    puzzle.outputs[1].bonds[0].to = {1, 0};
    puzzle.outputs[1].bonds[0].bits = 1;
    const auto shared = BuildSharedAssemblyTarget(puzzle);
    for (std::size_t product = 0; product < puzzle.outputs.size(); ++product) {
        const auto& atoms = puzzle.outputs[product].atoms;
        const auto minimum =
            std::min_element(atoms.begin(), atoms.end(), [](const auto& a, const auto& b) {
                return a.position.r < b.position.r;
            });
        EXPECT_EQ(minimum->position.r + shared.origins[product].r, 0);
    }
    EXPECT_NO_THROW(AssemblyPlanner::Plan(shared.puzzle, true));
}

TEST(SharedOrdinaryOutputTest, DuplicateElementsKeepSeparateProducts) {
    auto puzzle = io::BinaryParser::ParsePuzzleFile(input);
    puzzle.outputs[1] = puzzle.outputs[0];
    puzzle.permissionBits |= (1ULL << 17);
    const auto shared = BuildSharedAssemblyTarget(puzzle);
    EXPECT_NE(shared.origins[0], shared.origins[1]);
    EXPECT_EQ(shared.atoms[0], (ProductAtomRef{0, 0}));
    EXPECT_EQ(shared.atoms[6], (ProductAtomRef{1, 0}));
    EXPECT_NO_THROW(AssemblyPlanner::PlanSupply(shared.puzzle, true));
}

#ifdef HAS_LIBVERIFY
TEST(SharedOrdinaryOutputTest, UnequalHeightsVerifyTwice) {
    for (bool includeSingleton : {false, true})
        for (bool reverse : {false, true}) {
            io::PuzzleData puzzle;
            puzzle.version = 3;
            puzzle.name = "unequal-heights";
            puzzle.outputMultiplier = 1;
            puzzle.permissionBits =
                AssemblyPlanner::RequiredPermissions | (1ULL << 2) | (1ULL << 3) | (1ULL << 17);
            io::Molecule feed;
            feed.atoms.push_back({core::AtomType::Salt, {0, 0}});
            puzzle.inputs.push_back(feed);
            for (int height : {1, 2, 3}) {
                io::Molecule output;
                for (int row = 0; row < height; ++row) {
                    output.atoms.push_back({core::AtomType::Salt, {0, row}});
                    if (row) output.bonds.push_back({1, {0, row - 1}, {0, row}});
                }
                output.atoms.push_back({core::AtomType::Salt, {1, 0}});
                output.bonds.push_back({1, {0, 0}, {1, 0}});
                puzzle.outputs.push_back(output);
            }
            if (includeSingleton) puzzle.outputs.push_back(feed);
            if (reverse) std::reverse(puzzle.outputs.begin(), puzzle.outputs.end());
            const ScopedTestDirectory scratch;
            const auto path = scratch.Path() / "unequal-heights.puzzle";
            const auto solutionPath = scratch.Path() / "unequal-heights.solution";
            struct Cleanup {
                std::filesystem::path puzzle, solution;
                ~Cleanup() {
                    std::filesystem::remove(puzzle);
                    std::filesystem::remove(solution);
                }
            } cleanup{path, solutionPath};
            {
                std::ofstream out(path, std::ios::binary);
                auto word = [&](std::uint64_t v, int n) {
                    for (int i = 0; i < n; ++i)
                        out.put(static_cast<char>(v >> (8 * i)));
                };
                word(3, 4);
                word(puzzle.name.size(), 1);
                out.write(puzzle.name.data(), puzzle.name.size());
                word(0, 8);
                word(puzzle.permissionBits, 8);
                for (const auto* list : {&puzzle.inputs, &puzzle.outputs}) {
                    word(list->size(), 4);
                    for (const auto& molecule : *list) {
                        word(molecule.atoms.size(), 4);
                        for (const auto& atom : molecule.atoms) {
                            word(static_cast<std::uint8_t>(atom.type), 1);
                            word(atom.position.q, 1);
                            word(atom.position.r, 1);
                        }
                        word(molecule.bonds.size(), 4);
                        for (const auto& bond : molecule.bonds) {
                            word(bond.bits, 1);
                            word(bond.from.q, 1);
                            word(bond.from.r, 1);
                            word(bond.to.q, 1);
                            word(bond.to.r, 1);
                        }
                    }
                }
                word(1, 4);
                word(0, 1);
            }
            auto solution = CompileSharedOrdinaryOutputs(puzzle);
            io::BinaryWriter::WriteSolutionFile(solutionPath, solution);
            const auto report = io::VerifySolutionFiles(path, solutionPath, 150000, 10000);
            solution.metrics = report.metrics;
            ASSERT_TRUE(report.Passed())
                << report.detail << " cycle=" << report.errorCycle << " reverse=" << reverse;
            solution.metrics = report.metrics;
            io::BinaryWriter::WriteSolutionFile(solutionPath, solution);
            EXPECT_TRUE(io::VerifySolutionFiles(path, solutionPath, 150000, 10000).Passed());
        }
}

TEST(SharedOrdinaryOutputTest, MolecularSupplyActualSource) {
    using K = ExecutionIR::Kind;
    const auto original =
        io::BinaryParser::ParsePuzzleFile(root / "asset/puzzle/tourney-2021/OM2021_W7.puzzle");
    ASSERT_TRUE(SupportsMolecularSupplySource(original.inputs[0]));
    const auto embedded = CompileMolecularRecipeSupply(original, 0, {{0, 3}, {1, 2}});
    EXPECT_EQ(embedded.consumers[0].output, 0);
    EXPECT_EQ(embedded.consumers[0].atom, 3);
    EXPECT_EQ(embedded.consumers[1].output, 1);
    EXPECT_EQ(embedded.consumers[1].atom, 2);
    EXPECT_EQ(embedded.input, 0);
    auto reservationLedger = embedded.recipeReservations;
    auto reserved = reservationLedger.Replay(original);
    EXPECT_EQ(reserved.batches[0], 1);
    EXPECT_EQ(reserved.atoms.size(), 2);
    ASSERT_EQ(reserved.stored.size(), 2);
    for (std::size_t output = 0; output < 2; ++output) {
        const auto id = reserved.stored.at(output).at(0);
        const auto ref = embedded.consumers[output];
        EXPECT_EQ(reserved.atoms.at(id).type, original.outputs[ref.output].atoms[ref.atom].type);
        reservationLedger.AppendFromTail(original, reserved, {K::Retrieve, output});
    }
    EXPECT_TRUE(reserved.stored.empty());
    EXPECT_EQ(reserved.outputs, (std::vector<std::size_t>{0, 0}));
    // Original chemistry and bonds remain pending, not fabricated completed products.
    EXPECT_THROW(ExecutionIR::RequireComplete(reserved, {6, 6}), std::exception);
    auto unsupported = original.inputs[0];
    unsupported.atoms.pop_back();
    unsupported.bonds.clear();
    EXPECT_FALSE(SupportsMolecularSupplySource(unsupported));
    unsupported = original.inputs[0];
    unsupported.atoms.push_back(original.inputs[1].atoms[1]);
    EXPECT_FALSE(SupportsMolecularSupplySource(unsupported));
    ASSERT_EQ(embedded.batch.emissions.size(), 12);
    EXPECT_EQ(original.outputs[0].atoms[3].type, core::AtomType::Air);
    EXPECT_EQ(original.outputs[1].atoms[2].type, core::AtomType::Fire);
    for (int variant = 0; variant < 4; ++variant) {
        auto puzzle = original;
        puzzle.inputs = {original.inputs[variant == 3 ? 1 : 0]};
        puzzle.outputs.clear();
        for (const auto& atom : puzzle.inputs[0].atoms) {
            io::Molecule output;
            output.atoms = {{atom.type, {0, 0}}};
            puzzle.outputs.push_back(output);
        }
        if (variant == 1) puzzle.outputs[0].atoms[0].type = core::AtomType::Salt;
        if (variant == 2) {
            std::reverse(puzzle.outputs.begin(), puzzle.outputs.end());
            // Exact rigid transform of the real feed, not a replacement molecule.
            auto transform = [](core::HexCoord p) {
                return core::HexCoord{-p.r + 2, p.q + p.r - 1};
            };
            for (auto& atom : puzzle.inputs[0].atoms)
                atom.position = transform(atom.position);
            for (auto& bond : puzzle.inputs[0].bonds) {
                bond.from = transform(bond.from);
                bond.to = transform(bond.to);
            }
        }
        puzzle.outputMultiplier = 1;
        puzzle.name = "molecular-supply-round13-" + std::to_string(variant);
        for (int bit : {9, 2, 3}) {
            auto denied = puzzle;
            denied.permissionBits &= ~(1ULL << bit);
            EXPECT_THROW(CompileMolecularSupplyBatch(denied), std::exception);
        }
        if (variant == 1) {
            auto denied = puzzle;
            denied.permissionBits &= ~(1ULL << 12);
            EXPECT_THROW(CompileMolecularSupplyBatch(denied), std::exception);
        }
        auto bad = puzzle;
        bad.inputs[0].bonds[0].bits = 2;
        EXPECT_FALSE(SupportsMolecularSupplySource(bad.inputs[0]));
        EXPECT_THROW(CompileMolecularSupplyBatch(bad), std::exception);
        bad = puzzle;
        bad.outputs[1] = bad.outputs[0];
        EXPECT_THROW(CompileMolecularSupplyBatch(bad), std::exception);
        const auto batch = CompileMolecularSupplyBatch(puzzle);
        EXPECT_EQ(batch.emissions.size(), 12);
        for (const auto kind : {K::Acquire, K::Unbond})
            EXPECT_EQ(std::count_if(batch.ledger.steps.begin(), batch.ledger.steps.end(),
                                    [&](const auto& step) { return step.operation.kind == kind; }),
                      6);
        std::set<ExecutionIR::AtomId> emitted;
        for (const auto& event : batch.emissions)
            EXPECT_TRUE(emitted.insert(event.atom).second);
        std::set<ExecutionIR::AtomId> sourceIdentities;
        const ScopedTestDirectory scratch;
        std::ofstream trace(scratch.Path() /
                            ("molecular-supply-round13-" + std::to_string(variant) + ".trace"));
        trace << "batch sourceAtom acquiredId emittedId output atom remainder begin holderRelease "
                 "ready release reservationEnd\n";
        for (std::size_t i = 0; i < batch.emissions.size(); ++i) {
            const auto& e = batch.emissions[i];
            EXPECT_TRUE(sourceIdentities.insert(e.sourceIdentity).second);
            EXPECT_EQ(e.sourceBatch, i / 2);
            EXPECT_EQ(e.sourceRemainder, 1 - i % 2);
            EXPECT_EQ(e.sourceInput, 0);
            trace << e.sourceBatch << ' ' << e.sourceAtom << ' ' << e.sourceIdentity << ' '
                  << e.atom << ' ' << e.consumer.output << ' ' << e.consumer.atom << ' '
                  << e.sourceRemainder << ' ' << e.beginCycle << ' ' << e.holderReleaseCycle << ' '
                  << e.readyCycle << ' ' << e.releaseCycle << ' ' << e.reservationEnd << '\n';
        }
        ExecutionIR prefix;
        auto tail = ExecutionIR::Initial(puzzle);
        std::size_t peak = 0;
        for (const auto& step : batch.ledger.steps) {
            if (step.operation.kind == K::Acquire) EXPECT_TRUE(tail.atoms.empty());
            prefix.AppendFromTail(puzzle, tail, step.operation);
            peak = std::max(peak, tail.atoms.size());
            EXPECT_TRUE(tail.stored.empty());
            EXPECT_EQ(tail.discarded, 0);
        }
        EXPECT_EQ(peak, 2);
        EXPECT_EQ(tail.batches[0], 6);
        ExecutionIR::RequireComplete(batch.ledger.Replay(puzzle), {6, 6});
        const auto path = scratch.Path() / (puzzle.name + ".puzzle");
        const auto solutionPath = scratch.Path() / (puzzle.name + ".solution");
        {
            std::ofstream out(path, std::ios::binary);
            auto word = [&](std::uint64_t v, int n) {
                for (int i = 0; i < n; ++i)
                    out.put(static_cast<char>(v >> (8 * i)));
            };
            word(3, 4);
            word(puzzle.name.size(), 1);
            out.write(puzzle.name.data(), puzzle.name.size());
            word(0, 8);
            word(puzzle.permissionBits, 8);
            for (const auto* list : {&puzzle.inputs, &puzzle.outputs}) {
                word(list->size(), 4);
                for (const auto& molecule : *list) {
                    word(molecule.atoms.size(), 4);
                    for (const auto& atom : molecule.atoms) {
                        word(static_cast<std::uint8_t>(atom.type), 1);
                        word(atom.position.q, 1);
                        word(atom.position.r, 1);
                    }
                    word(molecule.bonds.size(), 4);
                    for (const auto& bond : molecule.bonds) {
                        word(bond.bits, 1);
                        word(bond.from.q, 1);
                        word(bond.from.r, 1);
                        word(bond.to.q, 1);
                        word(bond.to.r, 1);
                    }
                }
            }
            word(1, 4);
            word(0, 1);
        }
        auto solution = batch.solution;
        io::BinaryWriter::WriteSolutionFile(solutionPath, solution);
        const auto report = io::VerifySolutionFiles(path, solutionPath, 150000, 10000);
        ASSERT_TRUE(report.Passed())
            << report.detail << " cycle=" << report.errorCycle << " variant=" << variant;
        solution.metrics = report.metrics;
        io::BinaryWriter::WriteSolutionFile(solutionPath, solution);
        EXPECT_TRUE(io::VerifySolutionFiles(path, solutionPath, 150000, 10000).Passed());
        std::cout << "MOLECULAR variant=" << variant << " cost=" << report.metrics.cost
                  << " cycles=" << report.metrics.cycles << " area=" << report.metrics.area << '\n';
    }
}

TEST(SharedOrdinaryOutputTest, FiniteSharedFactoryVerifiesTwice) {
    for (int family : {0, 1, 2})
        for (int rotation = 0; rotation < 6; ++rotation)
            for (bool calcify : {false, true})
                for (bool reverse : {false, true}) {
                    io::PuzzleData puzzle;
                    puzzle.version = 3;
                    puzzle.name = "unequal-heights";
                    puzzle.outputMultiplier = 1;
                    puzzle.permissionBits = AssemblyPlanner::RequiredPermissions | (1ULL << 2) |
                                            (1ULL << 3) | (1ULL << 17);
                    io::Molecule feed;
                    feed.atoms.push_back({core::AtomType::Salt, {0, 0}});
                    puzzle.inputs.push_back(feed);
                    const std::vector<int> demands = family == 0   ? std::vector<int>{2, 3}
                                                     : family == 1 ? std::vector<int>{2, 4}
                                                                   : std::vector<int>{3, 4};
                    for (int count : demands) {
                        io::Molecule output;
                        for (int atom = 0; atom < count; ++atom) {
                            const core::HexCoord pos =
                                family == 0 ? core::HexCoord{atom, 0}
                                            : core::HexCoord{atom == count - 1 ? 1 : 0,
                                                             atom == count - 1 ? 0 : atom};
                            output.atoms.push_back({core::AtomType::Salt, pos});
                            if (atom)
                                output.bonds.push_back({1,
                                                        atom == count - 1 && family != 0
                                                            ? core::HexCoord{0, 0}
                                                            : output.atoms[atom - 1].position,
                                                        pos});
                        }
                        auto transform = [&](core::HexCoord pos) {
                            for (int i = 0; i < rotation; ++i)
                                pos = {-pos.r, pos.q + pos.r};
                            return core::Add(pos, {2, -3});
                        };
                        for (auto& atom : output.atoms)
                            atom.position = transform(atom.position);
                        for (auto& bond : output.bonds) {
                            bond.from = transform(bond.from);
                            bond.to = transform(bond.to);
                        }
                        puzzle.outputs.push_back(output);
                    }
                    if (calcify) {
                        puzzle.inputs[0].atoms[0].type = core::AtomType::Air;
                        puzzle.permissionBits |= 1ULL << 12;
                    }
                    if (reverse) std::reverse(puzzle.outputs.begin(), puzzle.outputs.end());
                    const ScopedTestDirectory scratch;
                    const auto path = scratch.Path() / "unequal-heights.puzzle";
                    const auto solutionPath = scratch.Path() / "unequal-heights.solution";
                    struct Cleanup {
                        std::filesystem::path puzzle, solution;
                        ~Cleanup() {
                            std::filesystem::remove(puzzle);
                            std::filesystem::remove(solution);
                        }
                    } cleanup{path, solutionPath};
                    {
                        std::ofstream out(path, std::ios::binary);
                        auto word = [&](std::uint64_t v, int n) {
                            for (int i = 0; i < n; ++i)
                                out.put(static_cast<char>(v >> (8 * i)));
                        };
                        word(3, 4);
                        word(puzzle.name.size(), 1);
                        out.write(puzzle.name.data(), puzzle.name.size());
                        word(0, 8);
                        word(puzzle.permissionBits, 8);
                        for (const auto* list : {&puzzle.inputs, &puzzle.outputs}) {
                            word(list->size(), 4);
                            for (const auto& molecule : *list) {
                                word(molecule.atoms.size(), 4);
                                for (const auto& atom : molecule.atoms) {
                                    word(static_cast<std::uint8_t>(atom.type), 1);
                                    word(atom.position.q, 1);
                                    word(atom.position.r, 1);
                                }
                                word(molecule.bonds.size(), 4);
                                for (const auto& bond : molecule.bonds) {
                                    word(bond.bits, 1);
                                    word(bond.from.q, 1);
                                    word(bond.from.r, 1);
                                    word(bond.to.q, 1);
                                    word(bond.to.r, 1);
                                }
                            }
                        }
                        word(1, 4);
                        word(0, 1);
                    }
                    if (calcify) {
                        auto denied = puzzle;
                        denied.permissionBits &= ~(1ULL << 12);
                        EXPECT_THROW(CompileSharedFactoryBatch(denied), std::exception);
                    }
                    for (int bit : {2, 3}) {
                        auto denied = puzzle;
                        denied.permissionBits &= ~(1ULL << bit);
                        EXPECT_THROW(CompileSharedFactoryBatch(denied), std::exception);
                    }
                    if (rotation == 0 && !reverse) {
                        auto outside = puzzle;
                        outside.inputs.push_back(outside.inputs.front());
                        EXPECT_THROW(CompileSharedFactoryBatch(outside), std::invalid_argument);
                        outside = puzzle;
                        outside.outputs[0].atoms[0].type = core::AtomType::Fire;
                        EXPECT_THROW(CompileSharedFactoryBatch(outside), std::invalid_argument);
                    }
                    auto batch = CompileSharedFactoryBatch(puzzle);
                    std::set<ExecutionIR::AtomId> identities;
                    for (const auto& emission : batch.emissions) {
                        EXPECT_TRUE(identities.insert(emission.atom).second);
                        EXPECT_LE(emission.beginCycle, emission.holderReleaseCycle);
                        EXPECT_LT(emission.holderReleaseCycle, emission.readyCycle);
                        EXPECT_LT(emission.readyCycle, emission.releaseCycle);
                        EXPECT_LT(emission.releaseCycle, emission.reservationEnd);
                    }
                    EXPECT_EQ(batch.emissions.size(), 6u * (demands[0] + demands[1]));
                    EXPECT_EQ(std::count_if(batch.solution.objects.begin(),
                                            batch.solution.objects.end(),
                                            [](const auto& object) {
                                                return object.typeName == "glyph-calcification";
                                            }),
                              calcify ? 1 : 0);
                    ExecutionIR::RequireComplete(batch.ledger.Replay(puzzle), {6, 6});
                    for (std::size_t i = 1; i < batch.emissions.size(); ++i)
                        EXPECT_LE(batch.emissions[i - 1].reservationEnd,
                                  batch.emissions[i].beginCycle);
                    auto solution = batch.solution;
                    io::BinaryWriter::WriteSolutionFile(solutionPath, solution);
                    const auto report = io::VerifySolutionFiles(path, solutionPath, 150000, 10000);
                    solution.metrics = report.metrics;
                    ASSERT_TRUE(report.Passed()) << report.detail << " cycle=" << report.errorCycle
                                                 << " reverse=" << reverse;
                    if (rotation == 0 && !reverse && calcify) {
                        std::cout << "POTENTIAL family=" << family
                                  << " shared=" << report.metrics.cost << ","
                                  << report.metrics.cycles << '\n';
                        EXPECT_THROW(CompileLifeDeath(puzzle), std::invalid_argument);
                    }
                    solution.metrics = report.metrics;
                    io::BinaryWriter::WriteSolutionFile(solutionPath, solution);
                    EXPECT_TRUE(
                        io::VerifySolutionFiles(path, solutionPath, 150000, 10000).Passed());
                }
}

TEST(SharedOrdinaryOutputTest, LubricatingSolventsVerifiesTwice) {
    auto solution = AssemblySolver{}.Solve(io::BinaryParser::ParsePuzzleFile(input));
    const ScopedTestDirectory scratch;
    const auto output = scratch.Path() / "shared-ordinary-test.solution";
    struct Cleanup {
        std::filesystem::path path;
        ~Cleanup() {
            std::filesystem::remove(path);
        }
    } cleanup{output};
    io::BinaryWriter::WriteSolutionFile(output, solution);
    auto report = io::VerifySolutionFiles(input, output, 150000, 10000);
    ASSERT_TRUE(report.Passed()) << report.detail << " cycle=" << report.errorCycle;
    solution.metrics = report.metrics;
    io::BinaryWriter::WriteSolutionFile(output, solution);
    EXPECT_TRUE(io::VerifySolutionFiles(input, output, 150000, 10000).Passed());
}
#endif
} // namespace
void RunCompleteSharedAnimismusAssembly(bool nativeGate) {
    using T = core::AtomType;
    const ScopedTestDirectory scratch;
    const auto& dir = scratch.Path();
    const auto original =
        io::BinaryParser::ParsePuzzleFile(root / "asset/puzzle/tourney-2021/OM2021_W7.puzzle");
    for (int variant = 0; variant < 3; ++variant) {
        SCOPED_TRACE(variant);
        auto puzzle = original;
        puzzle.name = "generated-complete-shared-assembly-" + std::to_string(variant);
        puzzle.inputs.resize(1);
        puzzle.outputs.resize(2);
        for (auto& output : puzzle.outputs) {
            output.atoms = {{T::Vitae, {0, 0}}, {T::Mors, {1, 0}}};
            output.bonds = {{1, {0, 0}, {1, 0}}};
        }
        if (variant == 1) {
            for (std::size_t i = 0; i < 2; ++i) {
                auto& output = puzzle.outputs[i];
                output.atoms = {{i ? T::Mors : T::Vitae, {0, 0}},
                                {i ? T::Vitae : T::Mors, {1, 0}},
                                {i ? T::Mors : T::Vitae, {2, 0}}};
                output.bonds = {{1, {0, 0}, {1, 0}}, {1, {1, 0}, {2, 0}}};
            }
        }
        if (variant == 2) {
            for (auto& atom : puzzle.inputs[0].atoms)
                atom.position =
                    core::Add(core::RotateClockwise(core::RotateClockwise(atom.position)), {2, -1});
            for (auto& bond : puzzle.inputs[0].bonds) {
                bond.from =
                    core::Add(core::RotateClockwise(core::RotateClockwise(bond.from)), {2, -1});
                bond.to = core::Add(core::RotateClockwise(core::RotateClockwise(bond.to)), {2, -1});
            }
            std::reverse(puzzle.inputs[0].atoms.begin(), puzzle.inputs[0].atoms.end());
            for (auto& output : puzzle.outputs) {
                std::reverse(output.atoms.begin(), output.atoms.end());
                for (auto& atom : output.atoms)
                    atom.position = core::Add(atom.position, {3, -2});
                for (auto& bond : output.bonds) {
                    bond.from = core::Add(bond.from, {3, -2});
                    bond.to = core::Add(bond.to, {3, -2});
                }
            }
            std::swap(puzzle.outputs[0], puzzle.outputs[1]);
        }
        const auto batch = CompileSharedAnimismusAssembly(puzzle);
        std::ofstream tape(dir / (puzzle.name + "-tape.txt"));
        for (const auto& object : batch.solution.objects) {
            tape << object.typeName << " " << object.position.q << "," << object.position.r << "\n";
            for (const auto& instruction : object.instructions)
                tape << instruction.parameter << ":" << instruction.action << " ";
            tape << "\n";
        }
        ASSERT_EQ(batch.emissions.size(), variant == 1 ? 36 : 24);
        const auto state = batch.ledger.Replay(puzzle);
        ExecutionIR::RequireComplete(state, {6, 6});
        EXPECT_EQ(state.batches[0], variant == 1 ? 18 : 12);
        EXPECT_EQ(state.discarded, 0);
        std::ofstream emissions(dir / (puzzle.name + "-emissions.tsv"));
        emissions
            << "product\toutput\tatom\ttype\tquantity\tidentity\tsourceBatch\tprecursor0\tprecursor"
               "1\tportQ\tportR\tconsumerQ\tconsumerR\tbegin\tholderRelease\tready\trelease\tend\n";
        for (const auto& e : batch.emissions) {
            emissions << e.product << '\t' << e.consumer.output << '\t' << e.consumer.atom << '\t'
                      << static_cast<int>(e.type) << '\t' << e.quantity << '\t' << e.atom << '\t'
                      << e.sourceBatch << '\t' << e.reactionInputs[0] << '\t' << e.reactionInputs[1]
                      << '\t' << e.position.q << '\t' << e.position.r << '\t'
                      << e.consumerPosition.q << '\t' << e.consumerPosition.r << '\t'
                      << e.beginCycle << '\t' << e.holderReleaseCycle << '\t' << e.readyCycle
                      << '\t' << e.releaseCycle << '\t' << e.reservationEnd << '\n';
            EXPECT_EQ(e.quantity, 1);
            EXPECT_EQ(e.type, puzzle.outputs[e.consumer.output].atoms[e.consumer.atom].type);
            EXPECT_NE(e.reactionInputs[0], e.reactionInputs[1]);
            EXPECT_GT(e.reactionInputs[0], 0);
            EXPECT_GT(e.reactionInputs[1], 0);
            EXPECT_EQ(e.sourceIdentity, 0);
            EXPECT_LT(e.beginCycle, e.holderReleaseCycle);
            EXPECT_LT(e.holderReleaseCycle, e.readyCycle);
            EXPECT_LT(e.readyCycle, e.releaseCycle);
            EXPECT_LT(e.releaseCycle, e.reservationEnd);
        }
        EXPECT_TRUE(state.atoms.empty());
        const auto puzzlePath = dir / (puzzle.name + ".puzzle");
        const auto solutionPath = dir / (puzzle.name + ".solution");
        {
            std::ofstream out(puzzlePath, std::ios::binary);
            auto word = [&](std::uint64_t v, int n) {
                for (int i = 0; i < n; ++i)
                    out.put(static_cast<char>(v >> (8 * i)));
            };
            word(3, 4);
            word(puzzle.name.size(), 1);
            out.write(puzzle.name.data(), puzzle.name.size());
            word(0, 8);
            word(puzzle.permissionBits, 8);
            for (const auto* list : {&puzzle.inputs, &puzzle.outputs}) {
                word(list->size(), 4);
                for (const auto& molecule : *list) {
                    word(molecule.atoms.size(), 4);
                    for (const auto& atom : molecule.atoms) {
                        word(static_cast<std::uint8_t>(atom.type), 1);
                        word(atom.position.q, 1);
                        word(atom.position.r, 1);
                    }
                    word(molecule.bonds.size(), 4);
                    for (const auto& bond : molecule.bonds) {
                        word(bond.bits, 1);
                        word(bond.from.q, 1);
                        word(bond.from.r, 1);
                        word(bond.to.q, 1);
                        word(bond.to.r, 1);
                    }
                }
            }
            word(1, 4);
            word(0, 1);
        }
        auto solution = batch.solution;
        io::BinaryWriter::WriteSolutionFile(solutionPath, solution);
        if (nativeGate) {
            const auto verified = io::VerifySolutionFiles(puzzlePath, solutionPath, 150000, 10000);
            std::ofstream report(dir / (puzzle.name + "-gate.txt"));
            report << "cost=" << verified.metrics.cost << " cycles=" << verified.metrics.cycles
                   << " area=" << verified.metrics.area << "\n";
            report << verified.detail << " cycle=" << verified.errorCycle << " at "
                   << verified.errorU << "," << verified.errorV << "\n";
            ASSERT_TRUE(verified.Passed()) << verified.detail;
        }
        const auto replay = verification::VerifyStaticProduction(puzzle, solution, 150000);
        std::ofstream audit(dir / (puzzle.name + "-physical-audit.txt"));
        audit << "status=" << static_cast<int>(replay.status) << " detail=" << replay.detail
              << " acquired=" << replay.acquired << " disposed=" << replay.disposed
              << " remaining=" << replay.remaining << " traceTruncated=" << replay.traceTruncated
              << " continuousCertified=" << replay.continuousCertified << '\n';
        for (const auto& event : replay.events) {
            audit << event.cycle << ' ' << static_cast<int>(event.kind) << ' ' << event.port;
            for (const auto atom : event.atoms)
                audit << ' ' << atom;
            audit << '\n';
        }
        if (nativeGate) {
            const auto twice = io::VerifySolutionFiles(puzzlePath, solutionPath, 150000, 10000);
            ASSERT_TRUE(twice.Passed()) << twice.detail;
        }
        for (const int bit : {2, 3, 9, 12, 16}) {
            auto denied = puzzle;
            denied.permissionBits &= ~(1ULL << bit);
            EXPECT_THROW(CompileSharedAnimismusAssembly(denied), std::invalid_argument);
        }
        if (variant == 0) {
            auto missing = solution;
            std::erase_if(missing.objects,
                          [](const auto& o) { return o.typeName == "glyph-life-and-death"; });
            io::BinaryWriter::WriteSolutionFile(dir / "missing-reaction.solution", missing);
            if (nativeGate) {
                EXPECT_FALSE(io::VerifySolutionFiles(puzzlePath, dir / "missing-reaction.solution",
                                                     150000, 10000)
                                 .Passed());
            }
            auto held = solution;
            for (auto& o : held.objects)
                if (o.typeName == "arm1") {
                    auto release = std::find_if(o.instructions.begin(), o.instructions.end(),
                                                [](const auto& i) { return i.action == 'g'; });
                    if (release != o.instructions.end()) {
                        o.instructions.erase(release);
                        break;
                    }
                }
            io::BinaryWriter::WriteSolutionFile(dir / "held-coproduct.solution", held);
            if (nativeGate) {
                EXPECT_FALSE(io::VerifySolutionFiles(puzzlePath, dir / "held-coproduct.solution",
                                                     150000, 10000)
                                 .Passed());
            }
            EXPECT_THROW(CompileSharedAnimismusAssembly(original), std::invalid_argument);
        }
    }
}
TEST(SharedOrdinaryOutputTest, CompleteSharedAnimismusAssemblySemantics) {
    RunCompleteSharedAnimismusAssembly(false);
}
#ifdef HAS_LIBVERIFY
TEST(SharedOrdinaryOutputTest, CompleteSharedAnimismusAssembly) {
    RunCompleteSharedAnimismusAssembly(true);
}
#endif

void RunClosedSharedAnimismusRealSubgraph(bool nativeGate) {
    using T = core::AtomType;
    const ScopedTestDirectory scratch;
    const auto& dir = scratch.Path();
    const auto original =
        io::BinaryParser::ParsePuzzleFile(root / "asset/puzzle/tourney-2021/OM2021_W7.puzzle");
    for (int variant = 0; variant < 4; ++variant) {
        auto puzzle = original;
        puzzle.name = "shared-chemistry-round14-" + std::to_string(variant);
        puzzle.inputs = {original.inputs[variant == 3 ? 1 : 0]};
        puzzle.outputs.resize(2);
        for (std::size_t i = 0; i < 2; ++i) {
            puzzle.outputs[i].atoms.resize(1);
            puzzle.outputs[i].atoms[0].type = (i == 0) != (variant == 2) ? T::Vitae : T::Mors;
            puzzle.outputs[i].atoms[0].position = {0, 0};
            puzzle.outputs[i].bonds.clear();
        }
        if (variant == 1) {
            for (auto& atom : puzzle.inputs[0].atoms)
                atom.position =
                    core::Add(core::RotateClockwise(core::RotateClockwise(atom.position)), {2, -1});
            for (auto& bond : puzzle.inputs[0].bonds) {
                bond.from =
                    core::Add(core::RotateClockwise(core::RotateClockwise(bond.from)), {2, -1});
                bond.to = core::Add(core::RotateClockwise(core::RotateClockwise(bond.to)), {2, -1});
            }
            std::reverse(puzzle.inputs[0].atoms.begin(), puzzle.inputs[0].atoms.end());
        }
        const auto batch = CompileSharedAnimismusBatch(puzzle);
        ASSERT_EQ(batch.emissions.size(), 12);
        const auto state = batch.ledger.Replay(puzzle);
        ExecutionIR::RequireComplete(state, {6, 6});
        EXPECT_EQ(state.batches[0], 6);
        EXPECT_EQ(state.discarded, 0);
        EXPECT_TRUE(state.atoms.empty());
        EXPECT_TRUE(state.bonds.empty());
        EXPECT_TRUE(state.stored.empty());
        for (std::size_t i = 0; i < batch.emissions.size(); ++i) {
            const auto& e = batch.emissions[i];
            EXPECT_LT(e.beginCycle, e.holderReleaseCycle);
            EXPECT_LT(e.holderReleaseCycle, e.readyCycle);
            EXPECT_LT(e.readyCycle, e.releaseCycle);
            EXPECT_LT(e.releaseCycle, e.reservationEnd);
            EXPECT_EQ(e.sourceBatch, i / 2);
            EXPECT_EQ(e.sourceRemainder, 1 - i % 2);
            if (i) {
                EXPECT_LE(batch.emissions[i - 1].reservationEnd, e.beginCycle);
            }
        }
        using K = ExecutionIR::Kind;
        auto incomplete = batch.ledger;
        incomplete.steps.pop_back();
        EXPECT_THROW(ExecutionIR::RequireComplete(incomplete.Replay(puzzle), {6, 6}),
                     ExecutionIR::Error);
        auto material = ExecutionIR::Initial(puzzle);
        ExecutionIR invalid;
        EXPECT_THROW(invalid.AppendFromTail(puzzle, material, {K::Animismus, 0, {1, 2}}),
                     ExecutionIR::Error);
        std::ofstream trace(dir / (puzzle.name + ".trace"));
        for (const auto& e : batch.emissions)
            trace << e.sourceBatch << ' ' << e.sourceAtom << ' ' << e.sourceIdentity << ' '
                  << e.atom << ' ' << e.consumerPosition.q << ' ' << e.consumerPosition.r << ' '
                  << e.beginCycle << ' ' << e.holderReleaseCycle << ' ' << e.readyCycle << ' '
                  << e.releaseCycle << ' ' << e.reservationEnd << '\n';
        const auto path = dir / (puzzle.name + ".puzzle");
        const auto solutionPath = dir / (puzzle.name + ".solution");
        {
            std::ofstream out(path, std::ios::binary);
            auto word = [&](std::uint64_t v, int n) {
                for (int i = 0; i < n; ++i)
                    out.put(static_cast<char>(v >> (8 * i)));
            };
            word(3, 4);
            word(puzzle.name.size(), 1);
            out.write(puzzle.name.data(), puzzle.name.size());
            word(0, 8);
            word(puzzle.permissionBits, 8);
            for (const auto* list : {&puzzle.inputs, &puzzle.outputs}) {
                word(list->size(), 4);
                for (const auto& molecule : *list) {
                    word(molecule.atoms.size(), 4);
                    for (const auto& atom : molecule.atoms) {
                        word(static_cast<std::uint8_t>(atom.type), 1);
                        word(atom.position.q, 1);
                        word(atom.position.r, 1);
                    }
                    word(molecule.bonds.size(), 4);
                    for (const auto& bond : molecule.bonds) {
                        word(bond.bits, 1);
                        word(bond.from.q, 1);
                        word(bond.from.r, 1);
                        word(bond.to.q, 1);
                        word(bond.to.r, 1);
                    }
                }
            }
            word(1, 4);
            word(0, 1);
        }
        io::VerificationOptions independent;
        independent.mode = io::VerificationMode::Independent;
        auto solution = batch.solution;
        io::BinaryWriter::WriteSolutionFile(solutionPath, solution);
        if (nativeGate) {
            const auto report = io::VerifySolutionFiles(path, solutionPath, 150000, 10000);
            solution.metrics = report.metrics;
            ASSERT_TRUE(report.Passed())
                << report.detail << " cycle=" << report.errorCycle << " at " << report.errorU << ","
                << report.errorV << " variant=" << variant;
        }
        const auto secondGate = io::VerifySolutionFiles(path, solutionPath, independent);
        EXPECT_EQ(secondGate.status, io::VerificationStatus::Unavailable) << secondGate.detail;
        std::cout << "INDEPENDENT variant=" << variant << " unavailable=" << secondGate.detail
                  << '\n';
        if (nativeGate) {
            if (variant == 0) {
                auto blocked = solution;
                auto glyph =
                    std::find_if(blocked.objects.begin(), blocked.objects.end(), [](const auto& o) {
                        return o.typeName == "glyph-life-and-death";
                    });
                ASSERT_NE(glyph, blocked.objects.end());
                blocked.objects.erase(glyph);
                const auto badPath = dir / "missing-reaction.solution";
                io::BinaryWriter::WriteSolutionFile(badPath, blocked);
                const auto missingGate = io::VerifySolutionFiles(path, badPath, 5000, 10000);
                EXPECT_FALSE(missingGate.Passed());
                std::cout << "NEGATIVE missing-reaction status="
                          << static_cast<int>(missingGate.status)
                          << " detail=" << missingGate.detail << '\n';
                blocked = solution;
                const auto collector =
                    std::find_if(blocked.objects.begin(), blocked.objects.end(),
                                 [](const auto& o) { return o.typeName == "arm1"; });
                ASSERT_NE(collector, blocked.objects.end());
                EXPECT_EQ(collector->position, (core::HexCoord{14, 2}));
                for (auto& instruction : collector->instructions)
                    if (instruction.action == 'g') instruction.action = ' ';
                const auto heldPath = dir / "held-coproduct.solution";
                io::BinaryWriter::WriteSolutionFile(heldPath, blocked);
                const auto heldGate = io::VerifySolutionFiles(path, heldPath, 5000, 10000);
                EXPECT_FALSE(heldGate.Passed());
                std::cout << "NEGATIVE held-coproduct status=" << static_cast<int>(heldGate.status)
                          << " detail=" << heldGate.detail << '\n';
            }
            io::BinaryWriter::WriteSolutionFile(solutionPath, solution);
            std::cout << "CLOSED variant=" << variant << " cost=" << solution.metrics.cost
                      << " cycles=" << solution.metrics.cycles << " area=" << solution.metrics.area
                      << '\n';
        }
        for (int bit : {2, 3, 9, 12, 16}) {
            auto denied = puzzle;
            denied.permissionBits &= ~(1ULL << bit);
            EXPECT_THROW(CompileSharedAnimismusBatch(denied), std::invalid_argument);
        }
        auto bad = puzzle;
        bad.outputMultiplier = 2;
        EXPECT_THROW(CompileSharedAnimismusBatch(bad), std::invalid_argument);
        bad = puzzle;
        bad.inputs.push_back(bad.inputs[0]);
        EXPECT_THROW(CompileSharedAnimismusBatch(bad), std::invalid_argument);
        bad = puzzle;
        bad.inputs[0].atoms[0].type = T::Salt;
        EXPECT_THROW(CompileSharedAnimismusBatch(bad), std::invalid_argument);
        auto duplicate = puzzle;
        duplicate.outputs[1] = duplicate.outputs[0];
        EXPECT_THROW(CompileSharedAnimismusBatch(duplicate), std::invalid_argument);
        try {
            auto baseline = CompileLifeDeath(puzzle);
            const auto baselinePath = dir / (puzzle.name + "-duplicated.solution");
            io::BinaryWriter::WriteSolutionFile(baselinePath, baseline);
            EXPECT_EQ(io::VerifySolutionFiles(path, baselinePath, independent).status,
                      io::VerificationStatus::Unavailable);
            if (nativeGate) {
                const auto gate = io::VerifySolutionFiles(path, baselinePath, 150000, 10000);
                EXPECT_TRUE(gate.Passed()) << gate.detail;
                baseline.metrics = gate.metrics;
                io::BinaryWriter::WriteSolutionFile(baselinePath, baseline);
                std::cout << "BASELINE variant=" << variant << " passed=" << gate.Passed()
                          << " cost=" << gate.metrics.cost << " cycles=" << gate.metrics.cycles
                          << " area=" << gate.metrics.area << " detail=" << gate.detail << '\n';
            }
        } catch (const std::exception& e) {
            FAIL() << "registered baseline unexpectedly rejected: " << e.what();
        }
    }
}
TEST(SharedOrdinaryOutputTest, ClosedSharedAnimismusRealSubgraphSemantics) {
    RunClosedSharedAnimismusRealSubgraph(false);
}
#ifdef HAS_LIBVERIFY
TEST(SharedOrdinaryOutputTest, ClosedSharedAnimismusRealSubgraph) {
    RunClosedSharedAnimismusRealSubgraph(true);
}
#endif
} // namespace opusoptima::solvers
