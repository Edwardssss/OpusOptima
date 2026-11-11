// Contract tests for the reagent-buffer stride of the shared row feeder.
//
// Buffers park on the serpentine's upper row and every slot stretches the feeder
// rail by one stride unit on both sides, so the legacy distance of three cost five
// per buffer per unit more than a packed row with one unit. The feeder clock, not
// the geometry, schedules the pickups, so the packed spacing is safe; these tests
// pin that the shipped default is the packed one, that it only touches the rail,
// and that the machine it produces still passes the native gate.
#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "AssemblySolver.hpp"
#include "BinaryParser.hpp"
#include "BinaryWriter.hpp"
#include "RepoRoot.hpp"
#include "RowAssembly.hpp"

namespace opusoptima::solvers {
namespace {
constexpr const char* kBufferPuzzle = "asset/puzzle/journal/journal3/P068.puzzle";

// Sets `OPUSOPTIMA_FEED_BUFFER_STRIDE` for the lifetime of the object and restores
// the previous value, so the suite stays order independent.
class ScopedBufferStride {
    public:
    explicit ScopedBufferStride(const char* value) {
        if (const char* prior = std::getenv("OPUSOPTIMA_FEED_BUFFER_STRIDE")) saved_ = prior;
        if (value)
            setenv("OPUSOPTIMA_FEED_BUFFER_STRIDE", value, 1);
        else
            unsetenv("OPUSOPTIMA_FEED_BUFFER_STRIDE");
    }
    ScopedBufferStride(const ScopedBufferStride&) = delete;
    ScopedBufferStride& operator=(const ScopedBufferStride&) = delete;
    ~ScopedBufferStride() {
        if (saved_)
            setenv("OPUSOPTIMA_FEED_BUFFER_STRIDE", saved_->c_str(), 1);
        else
            unsetenv("OPUSOPTIMA_FEED_BUFFER_STRIDE");
    }

    private:
    std::optional<std::string> saved_;
};

int TrackCells(const io::SolutionData& machine) {
    int total = 0;
    for (const auto& object : machine.objects)
        if (object.typeName == "track" && object.bodyData.size() > 4) total += object.bodyData[4];
    return total;
}

int TapeTicks(const io::SolutionData& machine) {
    int total = 0;
    for (const auto& object : machine.objects)
        total += static_cast<int>(object.instructions.size());
    return total;
}

} // namespace

// The packed default must be exactly what an explicit `=1` compiles. Packing moves
// the whole feed bank right (every station keeps its offset from the shorter rail),
// so only the inventory, the rail length and the total tape length are pinned.
TEST(FeedBufferStrideTest, PackedBuffersOnlyShortenTheFeedBank) {
    const auto root = opusoptima::test::RepoRoot(__FILE__);
    const auto puzzle = io::BinaryParser::ParsePuzzleFile(root / kBufferPuzzle);
    io::SolutionData legacy;
    io::SolutionData shipped;
    io::SolutionData explicitPacked;
    {
        const ScopedBufferStride guard("3");
        legacy = CompileSharedPurificationAssembly(puzzle);
    }
    {
        const ScopedBufferStride guard(nullptr);
        shipped = CompileSharedPurificationAssembly(puzzle);
    }
    {
        const ScopedBufferStride guard("1");
        explicitPacked = CompileSharedPurificationAssembly(puzzle);
    }
    ASSERT_EQ(legacy.objects.size(), shipped.objects.size());
    EXPECT_EQ(TrackCells(shipped), TrackCells(explicitPacked));
    EXPECT_LT(TrackCells(shipped), TrackCells(legacy));
    EXPECT_EQ(TapeTicks(shipped), TapeTicks(explicitPacked));
    EXPECT_LT(TapeTicks(shipped), TapeTicks(legacy));
    for (std::size_t i = 0; i < legacy.objects.size(); ++i)
        EXPECT_EQ(legacy.objects[i].typeName, shipped.objects[i].typeName) << i;
}

#ifdef HAS_LIBVERIFY
// The packed machine has a different period, so the native gate has the last word.
TEST(FeedBufferStrideTest, PackedBuffersVerifyNatively) {
    const auto root = opusoptima::test::RepoRoot(__FILE__);
    const auto puzzlePath = root / kBufferPuzzle;
    const auto solutionPath =
        std::filesystem::current_path() / "feed-buffer-stride-packed.solution";
    struct Cleanup {
        std::filesystem::path path;
        ~Cleanup() {
            std::error_code ec;
            std::filesystem::remove(path, ec);
        }
    } cleanup{solutionPath};
    const auto puzzle = io::BinaryParser::ParsePuzzleFile(puzzlePath);
    io::SolutionData solution;
    {
        const ScopedBufferStride guard(nullptr);
        solution = CompileSharedPurificationAssembly(puzzle);
    }
    io::BinaryWriter::WriteSolutionFile(solutionPath, solution);
    auto report = io::VerifySolutionFiles(puzzlePath, solutionPath, 1000000, 10000);
    ASSERT_TRUE(report.Passed()) << report.detail << " cycle=" << report.errorCycle << " at="
                                 << report.errorU << "," << report.errorV;
    solution.metrics = report.metrics;
    EXPECT_GT(solution.metrics.cost, 0);
    EXPECT_GT(solution.metrics.cycles, 0);
    io::BinaryWriter::WriteSolutionFile(solutionPath, solution);
    EXPECT_TRUE(io::VerifySolutionFiles(puzzlePath, solutionPath, 1000000, 10000).Passed());
}
#endif

} // namespace opusoptima::solvers
