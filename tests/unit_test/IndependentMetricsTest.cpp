#include "Verification/IndependentMetrics.hpp"
#include "Verification/IndependentCapabilities.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>

#ifdef HAS_LIBVERIFY
#    include "IO/BinaryWriter.hpp"
#    include "UnitTestConstants.hpp"
#    include "Verification/libverify.h"
#endif

namespace opusoptima::verification {
struct IndependentMetricsRasterTestAccess {
    static void Raster(IndependentMetrics& metrics, double x, double y, double radius,
                       double uncertainty) {
        metrics.RasterPoint(x, y, radius, uncertainty);
    }
    static const auto& Area(const IndependentMetrics& metrics) {
        return metrics.area_;
    }
    static const auto& Uncertain(const IndependentMetrics& metrics) {
        return metrics.uncertainCells_;
    }
};
} // namespace opusoptima::verification

namespace {
using namespace opusoptima;
using verification::IndependentMetricsRasterTestAccess;
using Coord = core::HexCoord;
using verification::IndependentMetrics;
Coord Rotate(Coord cell, int turns) {
    for (int i = 0; i < (turns % 6 + 6) % 6; ++i)
        cell = core::RotateClockwise(cell);
    return cell;
}
io::PuzzleData Puzzle() {
    io::PuzzleData puzzle;
    puzzle.version = 3;
    puzzle.name = "independent-metrics";
    puzzle.outputMultiplier = 1;
    puzzle.permissionBits = ~0ULL;
    puzzle.inputs = {{{{core::AtomType::Salt, {0, 0}}}, {}}};
    puzzle.outputs = puzzle.inputs;
    return puzzle;
}
io::SolutionData Transfer(int length = 1, int offset = 0) {
    io::SolutionData machine;
    machine.puzzleName = "independent-metrics";
    machine.solutionName = "metric-fixture";
    machine.metrics = {999, 999, 999, 999};
    machine.objects = {{"arm1",
                        {},
                        {{'G', offset}, {'r', offset + 1}, {'g', offset + 2}, {'R', offset + 3}},
                        {length, 0, 0, 0, 0}},
                       {"input", {length, 0}, {}, {1, 0, 0, 0, 0}},
                       {"out-std", {0, length}, {}, {1, 0, 0, 0, 0}}};
    return machine;
}
io::SolutionMetrics RunTransfer(IndependentMetrics& metrics, int length = 1) {
    for (int cycle = 0; cycle < 23; ++cycle) {
        const int phase = cycle % 4;
        const int before = phase == 0 || phase == 1 ? 0 : 1;
        const int after = phase == 1 ? 1 : phase == 3 ? 0 : before;
        metrics.BeginMotions();
        metrics.ObserveArmMotion({}, length, before, {}, length, after, 1);
        if (phase == 1) metrics.ObserveAtomMotion({length, 0}, {}, {}, 1);
        metrics.EndMotions();
    }
    return metrics.Finish(23);
}
#ifdef HAS_LIBVERIFY
struct Files {
    std::filesystem::path prefix =
        std::filesystem::temp_directory_path() /
        ("independent-metrics-" + std::to_string(opusoptima::test::ProcessId()));
    std::filesystem::path puzzle = prefix.string() + ".puzzle",
                          solution = prefix.string() + ".solution";
    ~Files() {
        std::error_code error;
        std::filesystem::remove(puzzle, error);
        std::filesystem::remove(solution, error);
    }
};
void WritePuzzle(const std::filesystem::path& file, const io::PuzzleData& puzzle) {
    std::ofstream stream(file, std::ios::binary);
    const auto word = [&](std::uint64_t value, int size = 4) {
        for (int i = 0; i < size; ++i)
            stream.put(static_cast<char>(value >> (8 * i)));
    };
    const auto cell = [&](Coord point) {
        word(point.q, 1);
        word(point.r, 1);
    };
    const auto text = [&](const std::string& value) {
        word(value.size(), 1);
        stream.write(value.data(), value.size());
    };
    word(3);
    text(puzzle.name);
    word(0, 8);
    word(puzzle.permissionBits, 8);
    for (const auto* list : {&puzzle.inputs, &puzzle.outputs}) {
        word(list->size());
        for (const auto& molecule : *list) {
            word(molecule.atoms.size());
            for (const auto& atom : molecule.atoms) {
                word(static_cast<int>(atom.type), 1);
                cell(atom.position);
            }
            word(molecule.bonds.size());
            for (const auto& bond : molecule.bonds) {
                word(bond.bits, 1);
                cell(bond.from);
                cell(bond.to);
            }
        }
    }
    word(puzzle.outputMultiplier);
    word(puzzle.productionInfo.has_value(), 1);
    if (!puzzle.productionInfo) return;
    const auto& info = *puzzle.productionInfo;
    word(info.shrinkLeft, 1);
    word(info.shrinkRight, 1);
    word(info.isolation, 1);
    word(info.chambers.size());
    for (const auto& chamber : info.chambers) {
        cell(chamber.position);
        text(chamber.chamberType);
    }
    word(0);
    word(0);
}
io::SolutionMetrics Native(const io::PuzzleData& puzzle, const io::SolutionData& machine) {
    Files files;
    WritePuzzle(files.puzzle, puzzle);
    io::BinaryWriter::WriteSolutionFile(files.solution, machine);
    auto* verifier = verifier_create(files.puzzle.c_str(), files.solution.c_str());
    if (!verifier) throw std::runtime_error("Cannot create native metric oracle");
    struct Cleanup {
        void* value;
        ~Cleanup() {
            verifier_destroy(value);
        }
    } cleanup{verifier};
    verifier_set_cycle_limit(verifier, 1000);
    io::SolutionMetrics result;
    result.cycles = verifier_evaluate_metric(verifier, "cycles");
    if (const auto* error = verifier_error(verifier)) throw std::runtime_error(error);
    result.cost = verifier_evaluate_metric(verifier, "cost");
    result.area = verifier_evaluate_metric(verifier, "area");
    result.instructionCount = verifier_evaluate_metric(verifier, "instructions");
    if (const auto* error = verifier_error(verifier)) throw std::runtime_error(error);
    return result;
}
#endif
} // namespace

TEST(IndependentMetrics, WorkCountersMeasureRasterWorkAndResetWithoutChangingArea) {
    IndependentMetrics metrics;
    metrics.Initialize(Puzzle(), {});
    EXPECT_EQ(metrics.WorkStats().markedCells, 0U);
    metrics.ObserveAtom({0, 0});
    metrics.ObserveAtom({0, 0});
    EXPECT_EQ(metrics.WorkStats().markedCells, 1U);
    metrics.BeginMotions();
    EXPECT_EQ(metrics.WorkStats().motionFrames, 0U);
    metrics.ObserveAtomMotion({0, 0}, {}, {1, 0}, 0);
    metrics.EndMotions();
    const auto& work = metrics.WorkStats();
    EXPECT_EQ(work.motionFrames, 1U);
    EXPECT_EQ(work.rasterPoints, 7U);
    EXPECT_EQ(work.rasterCandidates, 252U);
    EXPECT_EQ(work.markedCells, 2U);
    EXPECT_EQ(metrics.Finish(1).area, 2);
    EXPECT_TRUE(metrics.Conclusive());
    metrics.BeginMotions();
    metrics.EndMotions();
    EXPECT_EQ(work.motionFrames, 2U);
    EXPECT_EQ(work.rasterPoints, 7U);
    EXPECT_EQ(work.rasterCandidates, 252U);
    EXPECT_THROW(metrics.EndMotions(), std::invalid_argument);
    EXPECT_EQ(work.motionFrames, 2U);
    metrics.Initialize(Puzzle(), {});
    EXPECT_EQ(metrics.WorkStats().motionFrames, 0U);
    EXPECT_EQ(metrics.WorkStats().rasterPoints, 0U);
    EXPECT_EQ(metrics.WorkStats().rasterCandidates, 0U);
    EXPECT_EQ(metrics.WorkStats().markedCells, 0U);
    EXPECT_EQ(metrics.Finish(1).area, 0);
}

TEST(IndependentMetrics, LargeCoordinateUncertaintyExpandsRasterCandidateCoverage) {
    IndependentMetrics metrics;
    metrics.Initialize(Puzzle(), {});
    metrics.BeginMotions();
    metrics.ObserveAtomMotion({1000000, 0}, {}, {1, 0}, 0);
    metrics.EndMotions();
    EXPECT_EQ(metrics.WorkStats().rasterPoints, 7U);
    EXPECT_GT(metrics.WorkStats().rasterCandidates, 252U);
    EXPECT_FALSE(metrics.Conclusive());
}

TEST(IndependentMetrics, RasterRectangleCoversFloatingFilterBoundaryAdversaries) {
    using Access = IndependentMetricsRasterTestAccess;
    constexpr int limit = 1 << 20;
    struct Case {
        double x, y, radius, uncertainty;
    };
    const std::array cases{
        Case{0., 0., 0., 0.},
        Case{-0., -0., 58., 0.},
        Case{58., 0., 58., 0.},
        Case{-58., 0., 58., 0.},
        Case{0., 71., 0., 71.},
        Case{41., -71., 49., 22.},
        Case{82. * (limit - 20), 71. * (limit - 20), 58., 500.},
        Case{-82. * (limit - 20), -71. * (limit - 20), 58., 500.},
        Case{82. * ((limit - 20) + (-limit + 20) * .5), 71. * (-limit + 20), 49., 500.}};
    for (const auto& fixture : cases)
        for (double direction :
             {-std::numeric_limits<double>::infinity(), std::numeric_limits<double>::infinity()}) {
            const double x = std::nextafter(fixture.x, direction),
                         y = std::nextafter(fixture.y, direction), radius = fixture.radius,
                         uncertainty = fixture.uncertainty;
            SCOPED_TRACE((testing::Message() << std::hexfloat << x << ',' << y << " radius="
                                             << radius << " uncertainty=" << uncertainty));
            IndependentMetrics metrics;
            metrics.Initialize(Puzzle(), {});
            ASSERT_NO_THROW(Access::Raster(metrics, x, y, radius, uncertainty));
            const int q0 = static_cast<int>(std::floor(x / 82. - y / 71. * .5)),
                      r0 = static_cast<int>(std::floor(y / 71.));
            std::set<std::pair<int, int>> expectedArea, expectedUncertain;
            // A local adversarial regression, not the full-domain completeness proof.
            for (int q = std::max(-limit, q0 - 32); q <= std::min(limit, q0 + 32); ++q)
                for (int r = std::max(-limit, r0 - 32); r <= std::min(limit, r0 + 32); ++r) {
                    const double dx = x - 82. * (q + r * .5), dy = y - 71. * r;
                    const double squared = dx * dx + dy * dy;
                    if (uncertainty < radius &&
                        squared < (radius - uncertainty) * (radius - uncertainty))
                        expectedArea.emplace(q, r);
                    else if (squared <= (radius + uncertainty) * (radius + uncertainty)) {
                        expectedUncertain.emplace(q, r);
                        if (squared < radius * radius) expectedArea.emplace(q, r);
                    }
                }
            EXPECT_EQ(Access::Area(metrics), expectedArea);
            EXPECT_EQ(Access::Uncertain(metrics), expectedUncertain);
        }
    IndependentMetrics contact;
    contact.Initialize(Puzzle(), {});
    Access::Raster(contact, 58., 0., 58., 0.);
    EXPECT_TRUE(Access::Uncertain(contact).contains({0, 0}));
    EXPECT_FALSE(Access::Area(contact).contains({0, 0}));
}

TEST(IndependentMetrics, RasterRectangleRejectsNumericCoordinateAndResourceOverflow) {
    using Access = IndependentMetricsRasterTestAccess;
    constexpr int limit = 1 << 20;
    IndependentMetrics metrics;
    metrics.Initialize(Puzzle(), {});
    const double inf = std::numeric_limits<double>::infinity();
    for (const auto& args : std::array{
             std::array{inf, 0., 58., 0.}, std::array{0., inf, 58., 0.},
             std::array{0., 0., std::numeric_limits<double>::quiet_NaN(), 0.},
             std::array{0., 0., -1., 0.}, std::array{0., 0., 1., -1.}, std::array{0., 0., inf, inf},
             std::array{0., 0., std::numeric_limits<double>::max(),
                        std::numeric_limits<double>::max()},
             std::array{0., 0., 0., double(limit)}, std::array{0., 0., double(limit) + 1., 0.},
             std::array{std::numeric_limits<double>::max(), 0., 58., 0.},
             std::array{82. * (limit - 2), 0., 58., 0.},
             std::array{82. * (-limit + 2) - .01, 0., 58., 0.},
             std::array{0., 71. * (limit - 2), 58., 0.},
             std::array{0., 71. * (-limit + 2) - .01, 58., 0.}, std::array{0., 0., 0., 31000.}}) {
        EXPECT_THROW(Access::Raster(metrics, args[0], args[1], args[2], args[3]),
                     std::invalid_argument);
        EXPECT_EQ(metrics.WorkStats().rasterCandidates, 0U);
        EXPECT_TRUE(Access::Area(metrics).empty());
        EXPECT_TRUE(Access::Uncertain(metrics).empty());
    }
    for (double q : {double(-limit + 2), double(limit - 3)}) {
        IndependentMetrics edge;
        edge.Initialize(Puzzle(), {});
        EXPECT_NO_THROW(Access::Raster(edge, 82. * q, 0., 58., 0.));
        EXPECT_EQ(edge.WorkStats().rasterCandidates, 36U);
    }
    IndependentMetrics large;
    large.Initialize(Puzzle(), {});
    EXPECT_NO_THROW(Access::Raster(large, 0., 0., 0., 30000.));
    EXPECT_LE(large.WorkStats().rasterCandidates, 1000000U);
    EXPECT_GT(large.WorkStats().rasterCandidates, 900000U);
    EXPECT_TRUE(Access::Area(large).empty());
    EXPECT_FALSE(Access::Uncertain(large).empty());
}

TEST(IndependentMetrics, StaticCostsAreaAndTapesNeverTrustDeclaredMetrics) {
    const auto puzzle = Puzzle();
    auto machine = Transfer();
    machine.objects[0].instructions.push_back({'O', 20});
    machine.objects[0].instructions.push_back({'B', 21});
    machine.objects[0].instructions.push_back({' ', 22});
    machine.objects.push_back({"track", {10, 10}, {}, {1, 3, 0, 0, 3, 0, 0, 1, 0, 2, 0, 0}});
    IndependentMetrics metrics;
    metrics.Initialize(puzzle, machine);
    const auto result = metrics.Finish(23);
    EXPECT_EQ(result.cycles, 23);
    EXPECT_EQ(result.cost, 35);
    EXPECT_EQ(result.instructionCount, 4);
    EXPECT_EQ(result.area, 6);
    metrics.ObserveAtom({15, 15});
    metrics.ObserveAtom({15, 15});
    EXPECT_EQ(metrics.Finish(23).area, 7);
}

TEST(IndependentMetrics, ArmShaftAndSwingFootprintsAreCountedAsAUnion) {
    for (int length : {1, 2, 3}) {
        IndependentMetrics metrics;
        metrics.Initialize(Puzzle(), Transfer(length));
        const auto result = RunTransfer(metrics, length);
        EXPECT_EQ(result.area, length == 1 ? 3 : length == 2 ? 8 : 13);
        EXPECT_EQ(result.cost, 20);
        EXPECT_EQ(result.instructionCount, 4);
    }
}

TEST(IndependentMetrics, SampleEnvelopesDoNotProveContinuousArcCoverage) {
    // This is an analytic counterexample for our double trajectory, not game evidence.
    const double px = 82. * (2. + 7. * .5), py = 71. * 7.;
    const double x = 82. * (-7. + 10. * .5), y = 71. * 10.;
    const double angle = std::atan2(y, x) - std::atan2(py, px);
    ASSERT_GT(angle, 0.);
    ASSERT_LT(angle, std::acos(-1.) / 3.);
    EXPECT_LT(std::abs(std::hypot(x, y) - std::hypot(px, py)), 58.);
    const double uncertainty =
        .05 + .03 * 9. + 16. * std::numeric_limits<float>::epsilon() * (px + py);
    for (int sample = 1; sample < 32; ++sample) {
        const double a = std::acos(-1.) / 3. * sample / 32.;
        EXPECT_GT(std::hypot(x - px * std::cos(a) + py * std::sin(a),
                             y - px * std::sin(a) - py * std::cos(a)),
                  58. + uncertainty);
    }

    IndependentMetrics metrics;
    metrics.Initialize(Puzzle(), {});
    metrics.BeginMotions();
    metrics.ObserveAtomMotion({2, 7}, {}, {}, 1);
    metrics.EndMotions();
    EXPECT_FALSE(metrics.CertificationEligible());
    const auto sampledArea = metrics.VisitedCells();
    metrics.ObserveAtom({-7, 10});
    EXPECT_EQ(metrics.VisitedCells(), sampledArea + 1);
    EXPECT_FALSE(metrics.CertificationEligible());
}

TEST(IndependentMetrics, AmbiguousRasterCellsRemainUncertifiedUntilDefinitelyOccupied) {
    bool exercised = false;
    for (int q = 1; q <= 8 && !exercised; ++q)
        for (int r = 0; r <= 8 && !exercised; ++r) {
            IndependentMetrics metrics;
            metrics.Initialize(Puzzle(), {});
            metrics.BeginMotions();
            metrics.ObserveAtomMotion({q, r}, {}, {}, 1);
            metrics.EndMotions();
            if (metrics.Conclusive()) continue;
            exercised = true;
            for (int x = -20; x <= 20; ++x)
                for (int y = -20; y <= 20; ++y)
                    metrics.ObserveAtom({x, y});
            EXPECT_TRUE(metrics.Conclusive());
            EXPECT_FALSE(metrics.CertificationEligible());
        }
    EXPECT_TRUE(exercised);
}

TEST(IndependentMetrics, MotionCertificationCannotBeRestoredByClearingRasterAmbiguity) {
    IndependentMetrics metrics;
    EXPECT_FALSE(metrics.CertificationEligible());
    const auto reset = [&] {
        metrics.Initialize(Puzzle(), {});
        EXPECT_TRUE(metrics.CertificationEligible());
        metrics.BeginMotions();
        EXPECT_FALSE(metrics.CertificationEligible());
    };
    const auto finish = [&] {
        metrics.EndMotions();
        for (int q = -10; q <= 10; ++q)
            for (int r = -10; r <= 10; ++r)
                metrics.ObserveAtom({q, r});
        EXPECT_TRUE(metrics.Conclusive());
        EXPECT_FALSE(metrics.CertificationEligible());
        EXPECT_NO_THROW((void)metrics.Finish(1));
        metrics.BeginMotions();
        metrics.EndMotions();
        EXPECT_FALSE(metrics.CertificationEligible());
    };
    reset();
    metrics.ObserveAtomMotion({}, {}, {}, 0);
    metrics.ObserveArmMotion({}, 1, 0, {}, 1, 6, 1);
    metrics.EndMotions();
    EXPECT_TRUE(metrics.CertificationEligible());

    reset();
    metrics.ObserveAtomMotion({}, {}, {1, 0}, 0);
    finish();
    reset();
    metrics.ObserveAtomMotion({1, 0}, {}, {}, 1);
    finish();
    reset();
    metrics.ObserveArmMotion({}, 1, 0, {1, 0}, 1, 0, 1);
    finish();
    reset();
    metrics.ObserveArmMotion({}, 1, 0, {}, 2, 0, 1);
    finish();
    reset();
    metrics.ObserveArmMotion({}, 1, 0, {}, 1, 1, 1);
    finish();
    reset();
    metrics.ObserveArmMotion({}, 1, 0, {}, 1, 1, 6, true);
    finish();
    metrics.Initialize(Puzzle(), {});
    EXPECT_TRUE(metrics.CertificationEligible());
}

TEST(IndependentMetrics, UnknownProfilesAndIncompleteMotionFramesCannotProduceMetrics) {
    IndependentMetrics metrics;
    EXPECT_FALSE(metrics.Conclusive());
    EXPECT_THROW((void)metrics.Finish(1), std::invalid_argument);
    auto machine = Transfer();
    machine.objects.push_back({"unknown-glyph", {}, {}, {1, 0}});
    EXPECT_THROW(metrics.Initialize(Puzzle(), machine), std::invalid_argument);
    machine = Transfer();
    machine.objects[0].instructions[0].action = 'X';
    EXPECT_THROW(metrics.Initialize(Puzzle(), machine), std::invalid_argument);
    machine = Transfer();
    machine.objects[0].instructions.push_back(machine.objects[0].instructions[0]);
    EXPECT_THROW(metrics.Initialize(Puzzle(), machine), std::invalid_argument);
    metrics.Initialize(Puzzle(), Transfer());
    metrics.BeginMotions();
    EXPECT_THROW((void)metrics.Finish(1), std::invalid_argument);
    EXPECT_THROW(metrics.ObserveAtomMotion({1, 0}, {}, {}, 3), std::invalid_argument);
    EXPECT_FALSE(metrics.Conclusive());
    EXPECT_THROW(metrics.EndMotions(), std::invalid_argument);
    EXPECT_THROW(metrics.ObserveAtom({}), std::invalid_argument);
    EXPECT_THROW((void)metrics.Finish(1), std::invalid_argument);
    metrics.Initialize(Puzzle(), Transfer());
    EXPECT_THROW((void)metrics.Finish(-1), std::invalid_argument);
    EXPECT_TRUE(metrics.Conclusive());
    EXPECT_NO_THROW((void)metrics.Finish(1));
}

TEST(IndependentMetrics, PendingSweepsAreBoundedAcrossAtomAndArmObservations) {
    constexpr std::size_t limit = verification::finiteOrdinaryProfile.maxInventory +
                                  verification::finiteOrdinaryProfile.maxArms;
    IndependentMetrics metrics;
    metrics.Initialize(Puzzle(), {});
    metrics.BeginMotions();
    for (std::size_t i = 0; i < limit; ++i)
        metrics.ObserveAtomMotion({}, {}, {1, 0}, 0);
    EXPECT_THROW(metrics.ObserveArmMotion({}, 1, 0, {1, 0}, 1, 0, 1), std::invalid_argument);
    EXPECT_FALSE(metrics.Conclusive());
    EXPECT_THROW((void)metrics.Finish(1), std::invalid_argument);
    EXPECT_THROW(metrics.EndMotions(), std::invalid_argument);

    metrics.Initialize(Puzzle(), {});
    metrics.BeginMotions();
    for (std::size_t i = 0; i < limit; ++i)
        metrics.ObserveArmMotion({}, 1, 0, {1, 0}, 1, 0, 1);
    EXPECT_THROW(metrics.ObserveAtomMotion({}, {}, {1, 0}, 0), std::invalid_argument);
    EXPECT_FALSE(metrics.CertificationEligible());

    metrics.Initialize(Puzzle(), {});
    for (int frame = 0; frame < 2; ++frame) {
        metrics.BeginMotions();
        for (std::size_t i = 0; i < limit; ++i)
            metrics.ObserveAtomMotion({}, {}, {1, 0}, 0);
        metrics.ObserveAtomMotion({}, {}, {}, 0);
        metrics.ObserveArmMotion({}, 1, 0, {}, 1, 0, 1);
        EXPECT_NO_THROW(metrics.EndMotions());
    }
    EXPECT_EQ(metrics.WorkStats().rasterPoints, 2 * limit * 7);
    EXPECT_EQ(metrics.WorkStats().motionFrames, 2u);
    EXPECT_FALSE(metrics.CertificationEligible());
}

TEST(IndependentMetrics, MutatingApiFailuresInvalidateUntilReinitialized) {
    IndependentMetrics metrics;
    const auto recover = [&] {
        EXPECT_FALSE(metrics.Conclusive());
        EXPECT_THROW((void)metrics.Finish(1), std::invalid_argument);
        metrics.Initialize(Puzzle(), Transfer());
        EXPECT_TRUE(metrics.Conclusive());
    };
    metrics.Initialize(Puzzle(), Transfer());
    EXPECT_THROW(metrics.ObserveAtom({(1 << 20) + 1, 0}), std::invalid_argument);
    recover();
    EXPECT_THROW(metrics.ObserveArm({}, 0, 0, 1, false), std::invalid_argument);
    recover();
    EXPECT_THROW(metrics.ObserveArm({}, 1, 0, 4, false), std::invalid_argument);
    recover();
    EXPECT_THROW(metrics.EndMotions(), std::invalid_argument);
    recover();
    metrics.BeginMotions();
    EXPECT_THROW(metrics.BeginMotions(), std::invalid_argument);
    recover();
    metrics.BeginMotions();
    metrics.ObserveAtomMotion({1 << 16, 0}, {}, {}, 1);
    EXPECT_THROW(metrics.EndMotions(), std::invalid_argument);
    recover();
    metrics.ObserveAtom({1 << 20, 0});
    metrics.ObserveAtom({-(1 << 20), 0});
    EXPECT_TRUE(metrics.Conclusive());
    metrics.BeginMotions();
    metrics.ObserveAtomMotion({1 << 20, 0}, {}, {-1, 0}, 0);
    EXPECT_THROW(metrics.EndMotions(), std::invalid_argument);
    recover();
    auto invalid = Transfer();
    invalid.objects.push_back({"unknown-glyph", {}, {}, {1, 0}});
    EXPECT_THROW(metrics.Initialize(Puzzle(), invalid), std::invalid_argument);
    recover();
}

TEST(IndependentMetrics, ExtremeDirectionsMatchNormalizedMultiGrabberPosesAndMotions) {
    for (int direction : {std::numeric_limits<int>::min(), std::numeric_limits<int>::max()})
        for (int grabbers : {1, 2, 3, 6}) {
            SCOPED_TRACE(direction);
            SCOPED_TRACE(grabbers);
            const int normalized = (direction % 6 + 6) % 6;
            IndependentMetrics extreme, ordinary;
            extreme.Initialize(Puzzle(), Transfer());
            ordinary.Initialize(Puzzle(), Transfer());
            extreme.ObserveArm({}, 3, direction, grabbers, false);
            ordinary.ObserveArm({}, 3, normalized, grabbers, false);
            extreme.BeginMotions();
            ordinary.BeginMotions();
            extreme.ObserveArmMotion({}, 3, direction, {}, 3, normalized + 1, grabbers, false);
            ordinary.ObserveArmMotion({}, 3, normalized, {}, 3, normalized + 1, grabbers, false);
            extreme.EndMotions();
            ordinary.EndMotions();
            EXPECT_TRUE(extreme.Conclusive());
            EXPECT_EQ(extreme.Finish(1).area, ordinary.Finish(1).area);
        }
}

#ifdef HAS_LIBVERIFY
TEST(IndependentMetrics, DifferentialTransferMetricsCoverArmLengthsAndSignedOffsets) {
    for (int length : {1, 2, 3})
        for (int offset : {-7, 0, 11}) {
            SCOPED_TRACE(length);
            SCOPED_TRACE(offset);
            auto puzzle = Puzzle();
            auto machine = Transfer(length, offset);
            IndependentMetrics metrics;
            metrics.Initialize(puzzle, machine);
            const auto actual = RunTransfer(metrics, length), expected = Native(puzzle, machine);
            EXPECT_EQ(actual.cycles, expected.cycles);
            EXPECT_EQ(actual.cost, expected.cost);
            EXPECT_EQ(actual.area, expected.area);
            EXPECT_EQ(actual.instructionCount, expected.instructionCount);
        }
}

TEST(IndependentMetrics, DifferentialRotatingFragmentsIncludeSweptAreaBeyondArmReach) {
    for (int rotation = 0; rotation < 6; ++rotation)
        for (int length : {1, 2, 3}) {
            SCOPED_TRACE(rotation);
            SCOPED_TRACE(length);
            auto puzzle = Puzzle();
            puzzle.inputs = {{{{core::AtomType::Salt, {0, 0}},
                               {core::AtomType::Salt, {1, 0}},
                               {core::AtomType::Salt, {1, 1}}},
                              {{1, {0, 0}, {1, 0}}, {1, {1, 0}, {1, 1}}}}};
            puzzle.outputs = puzzle.inputs;
            auto machine = Transfer(length);
            machine.objects[0].bodyData[1] = rotation;
            machine.objects[1].position = Rotate({length, 0}, rotation);
            machine.objects[1].bodyData[1] = rotation;
            machine.objects[2].position = Rotate({length, 0}, rotation + 1);
            machine.objects[2].bodyData[1] = (rotation + 1) % 6;
            IndependentMetrics metrics;
            metrics.Initialize(puzzle, machine);
            for (int cycle = 0; cycle < 23; ++cycle) {
                const int phase = cycle % 4;
                const int before = (rotation + (phase >= 2 ? 1 : 0)) % 6;
                const int after = (rotation + (phase == 1 || phase == 2 ? 1 : 0)) % 6;
                metrics.BeginMotions();
                metrics.ObserveArmMotion({}, length, before, {}, length, after, 1);
                if (phase == 1)
                    for (const auto& atom : puzzle.inputs[0].atoms)
                        metrics.ObserveAtomMotion(
                            core::Add(machine.objects[1].position, Rotate(atom.position, rotation)),
                            {}, {}, 1);
                metrics.EndMotions();
            }
            const auto expected = Native(puzzle, machine), actual = metrics.Finish(23);
            EXPECT_EQ(actual.cycles, expected.cycles);
            EXPECT_EQ(actual.cost, expected.cost);
            EXPECT_EQ(actual.area, expected.area);
            EXPECT_EQ(actual.instructionCount, expected.instructionCount);
        }
}

TEST(IndependentMetrics, DifferentialLinearPistonAndTrackMotionAndHaltedTapeAccounting) {
    for (bool track : {false, true}) {
        SCOPED_TRACE(track);
        auto puzzle = Puzzle();
        auto machine = Transfer();
        machine.objects = {{track ? "arm1" : "piston",
                            {0, -1},
                            {{'G', 0},
                             {track ? 'A' : 'E', 1},
                             {track ? 'A' : 'E', 2},
                             {'g', 3},
                             {track ? 'a' : 'e', 4},
                             {track ? 'a' : 'e', 5}},
                            {1, 1, 0, 0, 0}},
                           {"input", {0, 0}, {}, {1, 0, 0, 0, 0}},
                           {"out-std", track ? Coord{1, 1} : Coord{0, 2}, {}, {1, 0, 0, 0, 0}}};
        if (track)
            machine.objects.push_back({"track", {0, -1}, {}, {1, 4, 0, 0, 3, 0, 0, 1, 0, 1, 1, 0}});
        IndependentMetrics metrics;
        metrics.Initialize(puzzle, machine);
        const std::array<int, 6> before{{1, 1, 2, 3, 3, 2}}, after{{1, 2, 3, 3, 2, 1}};
        const std::array<Coord, 3> bases{{{0, -1}, {1, -1}, {1, 0}}};
        const std::array<int, 6> oldIndex{{0, 0, 1, 2, 2, 1}}, newIndex{{0, 1, 2, 2, 1, 0}};
        for (int cycle = 0; cycle < 34; ++cycle) {
            const int phase = cycle % 6;
            metrics.BeginMotions();
            metrics.ObserveArmMotion(
                track ? bases[oldIndex[phase]] : Coord{0, -1}, track ? 1 : before[phase], 1,
                track ? bases[newIndex[phase]] : Coord{0, -1}, track ? 1 : after[phase], 1, 1);
            if (phase == 1 || phase == 2) {
                const auto from = phase == 1 ? Coord{0, 0} : track ? Coord{1, 0} : Coord{0, 1};
                const auto translation = track && phase == 1 ? Coord{1, 0} : Coord{0, 1};
                metrics.ObserveAtomMotion(from, {}, translation, 0);
            }
            metrics.EndMotions();
        }
        const auto expected = Native(puzzle, machine), actual = metrics.Finish(34);
        EXPECT_EQ(actual.cycles, expected.cycles);
        EXPECT_EQ(actual.cost, expected.cost);
        EXPECT_EQ(actual.area, expected.area);
        EXPECT_EQ(actual.instructionCount, expected.instructionCount);
    }
    auto puzzle = Puzzle();
    auto machine = Transfer();
    machine.objects[0].instructions = {{'G', -3}, {'B', -2}, {'r', -1}, {'O', 0}, {' ', 1}};
    machine.objects[1].position = {20, 20};
    machine.objects[2].position = {20, 20};
    IndependentMetrics metrics;
    metrics.Initialize(puzzle, machine);
    const auto expected = Native(puzzle, machine), actual = metrics.Finish(expected.cycles);
    EXPECT_EQ(actual.area, expected.area);
    EXPECT_EQ(actual.instructionCount, 2);
    EXPECT_EQ(actual.instructionCount, expected.instructionCount);
}

TEST(IndependentMetrics, DifferentialStaticGlyphFootprintsAndAllChamberWallRings) {
    for (const auto* type :
         {"glyph-calcification", "glyph-duplication", "glyph-projection", "glyph-purification",
          "glyph-life-and-death", "glyph-unification", "glyph-dispersion", "glyph-disposal",
          "glyph-marker", "bonder", "unbonder", "bonder-speed", "bonder-prisma"}) {
        SCOPED_TRACE(type);
        auto puzzle = Puzzle();
        io::SolutionData machine;
        machine.puzzleName = puzzle.name;
        machine.objects = {{"input", {20, 20}, {}, {1, 0, 0, 0, 0}},
                           {"out-std", {20, 20}, {}, {1, 0, 0, 0, 0}},
                           {type, {}, {}, {1, 0, 0, 0, 0}}};
        IndependentMetrics metrics;
        metrics.Initialize(puzzle, machine);
        const auto native = Native(puzzle, machine), actual = metrics.Finish(native.cycles);
        EXPECT_EQ(actual.cost, native.cost);
        EXPECT_EQ(actual.area, native.area);
        EXPECT_EQ(actual.instructionCount, native.instructionCount);
    }
    for (const auto* type : {"Small", "SmallWide", "SmallWider", "Medium", "MediumWide", "Large"}) {
        SCOPED_TRACE(type);
        auto puzzle = Puzzle();
        puzzle.productionInfo.emplace();
        puzzle.productionInfo->chambers.push_back({{0, 0}, type});
        io::SolutionData machine;
        machine.puzzleName = puzzle.name;
        machine.objects = {{"input", {}, {}, {1, 0, 0, 0, 0}},
                           {"out-std", {}, {}, {1, 0, 0, 0, 0}}};
        IndependentMetrics metrics;
        metrics.Initialize(puzzle, machine);
        const auto native = Native(puzzle, machine);
        EXPECT_EQ(metrics.Finish(native.cycles).area, native.area);
    }
}

TEST(IndependentMetrics, DifferentialMultiGrippersAndIdleWheelUseEveryStaticFootprint) {
    for (int grabbers : {2, 3, 6}) {
        SCOPED_TRACE(grabbers);
        auto puzzle = Puzzle();
        auto machine = Transfer();
        machine.objects.clear();
        machine.objects.push_back({"arm" + std::to_string(grabbers),
                                   {},
                                   {{'G', 0}, {'r', 1}, {'g', 2}, {'R', 3}},
                                   {1, 0, 0, 0, 0}});
        puzzle.outputs.resize(grabbers, puzzle.outputs.front());
        for (int grip = 0; grip < grabbers; ++grip) {
            machine.objects.push_back(
                {"input", Rotate({1, 0}, grip * 6 / grabbers), {}, {1, 0, 0, 0, 0}});
            machine.objects.push_back(
                {"out-std", Rotate({1, 0}, grip * 6 / grabbers + 1), {}, {1, 0, grip, 0, 0}});
        }
        machine.objects.push_back({"baron", {10, 10}, {}, {1, 2, 0, 0, 1}});
        IndependentMetrics metrics;
        metrics.Initialize(puzzle, machine);
        const auto expected = Native(puzzle, machine);
        const auto actual = metrics.Finish(expected.cycles);
        EXPECT_EQ(actual.cost, expected.cost);
        EXPECT_EQ(actual.area, expected.area);
        EXPECT_EQ(actual.instructionCount, expected.instructionCount);
    }
}
#endif
