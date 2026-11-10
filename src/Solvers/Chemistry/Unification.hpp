#pragma once

#include "BinaryParser.hpp"

#include <chrono>

namespace opusoptima::solvers {
// Ordinary-bond feeds total four atoms, with no repeated cardinals; salts
// supply missing cardinals using stationary wheels. The sole product is
// monatomic quintessence (multiplier 1..8).
io::SolutionData CompileUnification(const io::PuzzleData& puzzle);
// One reusable batch selecting whole input copies totaling four atoms; unrelated
// reagents are ignored and repeated copies retain their original input indices.
// A singleton output marker identifies its delivery cell.
// Callers remove that marker, translate the factory, and schedule its tape
// before each downstream pickup. Every arm returns to its initial state.
io::SolutionData CompileUnificationBatch(const io::PuzzleData& puzzle);
// Finite selective extraction parks unused atoms at distinct cells. Product ends
// delimit one delivery at a time; unlike a reusable batch its tape must not loop.
io::SolutionData CompileUnificationFiniteBatch(const io::PuzzleData& puzzle,
                                               std::vector<int>& productEnds, int products);
io::SolutionData CompileSaltQuintessenceBatch(const io::PuzzleData& puzzle, int saltInput);

// Absolute deadlines bound selection, layout, and emission. Expiration throws
// std::invalid_argument. Legacy overloads use an unlimited deadline.
io::SolutionData CompileUnification(const io::PuzzleData& puzzle,
                                    std::chrono::steady_clock::time_point deadline);
io::SolutionData CompileUnificationBatch(const io::PuzzleData& puzzle,
                                         std::chrono::steady_clock::time_point deadline);
io::SolutionData CompileUnificationFiniteBatch(const io::PuzzleData& puzzle,
                                               std::vector<int>& productEnds, int products,
                                               std::chrono::steady_clock::time_point deadline);
io::SolutionData CompileSaltQuintessenceBatch(const io::PuzzleData& puzzle, int saltInput,
                                              std::chrono::steady_clock::time_point deadline);
} // namespace opusoptima::solvers
