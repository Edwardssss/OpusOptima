#pragma once

#include <cstddef>
#include <cstdint>
#include <numbers>

namespace opusoptima::core {

// half of a 64-bit hash value.
inline constexpr std::size_t kIntBitWidth = 32U;

// A hexagon has six neighbors in axial coordinates.
inline constexpr int kDirectionCount = 6;

// Cube-coordinate distance formula normalization.
// Because q + r + s = 0, each step changes two coordinates,
// so the Manhattan sum is divided by 2.
inline constexpr int kCubeDistanceDivisor = 2;

// For a flat-top hexagon with radius R:
//   x = R * (√3 · q  +  (√3/2) · r)
//   y = R * (          (3/2)  · r)
inline constexpr double kFlatTopSpacingX = std::numbers::sqrt3; // √3  — horizontal spacing
inline constexpr double kFlatTopOffsetX =
    std::numbers::sqrt3 / 2.0;                  // √3/2 — horizontal offset per r-step
inline constexpr double kFlatTopSpacingY = 1.5; // 3/2 — vertical spacing per r-step

// Radius of the hexagon circumscribed circle (px).
inline constexpr double kHexRadius = 24.0;

// Flat-top vertex angles: each vertex is 60° apart, starting at -30° from horizontal.
inline constexpr double kHexVertexAngleStep = 60.0;
inline constexpr double kHexStartAngleOffset = 30.0;
inline constexpr int kHexVertexCount = 6;

inline constexpr double kPanelWidth = 320.0;
inline constexpr double kPanelHeight = 240.0;
inline constexpr double kPanelCornerRadius = 12.0;

inline constexpr double kAtomCircleRadius = 12.0;
// Vertical adjustment to centre the text symbol inside the atom circle.
inline constexpr double kAtomSymbolBaselineOffset = 4.5;
inline constexpr double kBondStrokeWidth = 4.0;

// Opus Magnum .puzzle file format version (int32, little-endian).
inline constexpr int kPuzzleFormatVersion = 3;

// Opus Magnum .solution file format tag (int32, little-endian).
inline constexpr int kSolutionFormatTag = 7;

// Number of int32 metrics in a .solution metric block.
inline constexpr int kSolutionMetricCount = 4;

// Number of int32 words in a solution object body header.
inline constexpr std::size_t kSolutionObjectHeaderWords = 5U;

// Bits per byte in little-endian integer packing.
inline constexpr int kBitsPerByte = 8;

// 1-byte mask used while splitting int32 values.
inline constexpr std::uint8_t kByteMask = 0xFFU;

// Default upward walk bound when searching for repository root from CWD.
inline constexpr int kDefaultRepoSearchMaxDepth = 5;

// Varint (variable-length integer) encoding — C# BinaryReader 7-bit format.
// Each group carries 7 data bits; the high bit (0x80) is the continuation flag.
inline constexpr int kVarintGroupBits = 7;

// Maximum groups needed for a 32-bit value: ceil(32 / 7) = 5.
inline constexpr int kVarintMaxGroups = 5;

// Lower 7 bits mask — the data payload within a varint byte.
inline constexpr std::uint8_t kVarintDataMask = 0x7FU;

// High bit — 1 = more groups follow, 0 = last group.
inline constexpr std::uint8_t kVarintContinueBit = 0x80U;

} // namespace opusoptima::core
