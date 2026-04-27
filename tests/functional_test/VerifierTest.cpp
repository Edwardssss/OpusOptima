#include <cstdint>
#include <filesystem>
#include <vector>

#include <gtest/gtest.h>

#include "BinaryParser.hpp"
#include "CollisionLUT.hpp"
#include "HexSimulator.hpp"
#include "MachineState.hpp"

namespace {

std::filesystem::path GetRepoRoot() {
    std::filesystem::path cursor = std::filesystem::path(__FILE__).parent_path();
    while (!cursor.empty()) {
        if (std::filesystem::exists(cursor / "CMakeLists.txt")) return cursor;
        cursor = cursor.parent_path();
    }
    throw std::runtime_error("Cannot locate repository root");
}

// std::vector<unsigned char> ReadFileBytes(const std::filesystem::path& path) {
//     FILE* file = std::fopen(path.string().c_str(), "rb");
//     if (file == nullptr) throw std::runtime_error("Cannot open " + path.string());
//     std::fseek(file, 0, SEEK_END);
//     long file_size = std::ftell(file);
//     std::rewind(file);
//     std::vector<unsigned char> buf(static_cast<std::size_t>(file_size));
//     if (file_size > 0) std::fread(buf.data(), 1, static_cast<std::size_t>(file_size), file);
//     std::fclose(file);
//     return buf;
// }

} // namespace

// Simulator-vs-verifier cross-check

TEST(SimulatorMatchesVerifier, EasyConduitCycleCount) {
    const auto repo = GetRepoRoot();
    const auto puzzlePath = repo / "asset/puzzle/easy/easy-conduit.puzzle";
    const auto solPath = repo / "asset/solution/easy/easy-conduit-easy-conduit-1.solution";

    // Our simulator
    const auto puzzle = opusoptima::io::BinaryParser::ParsePuzzleFile(puzzlePath);
    const auto solution = opusoptima::io::BinaryParser::ParseSolutionFile(solPath);

    opusoptima::core::MachineState state;
    std::int32_t simCycles = opusoptima::engine::HexSimulator::Run(puzzle, solution, state);
    EXPECT_EQ(simCycles, 26) << "Simulator should complete the solution in 26 cycles";

    // All arms should complete
    for (std::size_t i = 0; i < state.arms.size(); ++i) {
        EXPECT_FALSE(state.arms[i].active)
            << "Arm " << i << " at " << state.arms[i].basePosition << " did not finish";
    }

    // Output atoms should have been delivered
    EXPECT_GT(solution.metrics.cycles, 0);
}

TEST(SimulatorMatchesVerifier, DoubleConduitCycleCount) {
    const auto repo = GetRepoRoot();
    const auto puzzlePath = repo / "asset/puzzle/easy/double-conduit.puzzle";
    const auto solPath = repo / "asset/solution/easy/double-conduit-double-conduit-1.solution";

    const auto puzzle = opusoptima::io::BinaryParser::ParsePuzzleFile(puzzlePath);
    const auto solution = opusoptima::io::BinaryParser::ParseSolutionFile(solPath);

    opusoptima::core::MachineState state;
    std::int32_t simCycles = opusoptima::engine::HexSimulator::Run(puzzle, solution, state);
    EXPECT_EQ(simCycles, 26);
    EXPECT_EQ(solution.metrics.area, 35);
}

// Collision detection tests

TEST(CollisionLUTTest, SweepCellsReturnsCorrectCount) {
    const auto& lut = opusoptima::engine::CollisionLUT::Instance();
    const opusoptima::core::HexCoord base{.q = 0, .r = 0};

    // An arm of length 2 pointing direction 0 occupies cells (1,0) and (2,0)
    auto cells = lut.SweepCells(base, opusoptima::core::ArmDirection::D0, 2);
    EXPECT_EQ(cells.size(), 2U);
    if (cells.size() >= 2) {
        EXPECT_EQ(cells[0].q, 1);
        EXPECT_EQ(cells[0].r, 0);
        EXPECT_EQ(cells[1].q, 2);
        EXPECT_EQ(cells[1].r, 0);
    }
}

TEST(CollisionLUTTest, SweepLengthZeroReturnsEmpty) {
    const auto& lut = opusoptima::engine::CollisionLUT::Instance();
    auto cells = lut.SweepCells({0, 0}, opusoptima::core::ArmDirection::D0, 0);
    EXPECT_TRUE(cells.empty());
}
