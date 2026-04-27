<div align="center">
  <h1>🔬 OpusOptima</h1>
  <p><em>A C++20 toolkit for parsing, rendering, and solving Opus Magnum puzzle files</em></p>
  <p>
    <img src="https://img.shields.io/badge/standard-C%2B%2B20-blue?logo=c%2B%2B" alt="C++20">
    <img src="https://img.shields.io/badge/build-CMake%20%7C%20Ninja-green?logo=cmake" alt="Build">
    <img src="https://img.shields.io/badge/license-MIT-yellow" alt="License">
    <img src="https://img.shields.io/badge/https://img.shields.io/badge/status-alpha-red" alt="Status">
  </p>

</div>

---

## 📖 Overview

**OpusOptima** is a library and toolset for working with puzzle files from [Opus Magnum](https://www.zachtronics.com/opus-magnum/) (the alchemy-themed puzzle game by Zachtronics). It provides:

- **Parsing** — Full support for reading binary `.puzzle` files (version 3), including production puzzle data
- **Rendering** — ASCII preview and SVG export with clear hexagonal molecule visualization
- **Solving** — Integration with the [HiGHS](https://highs.dev/) optimization solver, using two solver backends (A*-based greedy planner + MILP atom matching) to generate valid solutions

## ✨ Features

### Implemented

| Feature | Status |
|---|---|
| `.puzzle` binary format parser | ✅ |
| `.solution` binary format parser | ✅ |
| `.solution` binary format writer | ✅ |
| Hex grid coordinate system | ✅ |
| Molecule and atom data structures | ✅ |
| Bond type handling (normal, triplex) | ✅ |
| Arm instruction decoding (all types: arm1-6, piston, baron) | ✅ |
| Track/pipe/glyph object support (all 9 glyph types) | ✅ |
| ASCII puzzle rendering support | ✅ |
| SVG export support for puzzle rendering output | ✅ |
| Full instruction set simulation | ✅ |
| Collision detection (based on precomputed LUT sweep table) | ✅ |
| Deterministic simulation engine | ✅ |
| A* pathfinding algorithm | ✅ |
| GreedyPlanner solver (greedy atom assignment + direct path planning) | ✅ |
| RecipeMilp solver (HiGHS MILP-based atom matching) | ✅ |
| `.solution` generation from solver results | ✅ |
| spdlog logging output | ✅ |
| Simulator-based solution validation | ✅ |

### Future Plans

- [ ] Multi-arm coordinated solving (piston/baron/track)
- [ ] Compile to WebAssembly for browser-side use
- [ ] Optimize algorithms to approach the Pareto frontier
- [ ] Improve algorithm parallelism to increase solve throughput

---

## 🏗️ Project Structure

```
OpusOptima/
├── CMakeLists.txt
├── .clang-format
├── .clangd
├── .gitignore
├── LICENSE
├── project.md
├── analyse_file.md
├── src/
│   ├── main.cpp
│   ├── Core/
│   │   ├── CoreConstants.hpp
│   │   ├── HexGrid.hpp
│   │   ├── LogManager.hpp
│   │   ├── LogManager.cpp
│   │   ├── MachineState.hpp
│   │   └── PuzzleSpecs.hpp
│   ├── Engine/
│   │   ├── CollisionLUT.cpp
│   │   ├── CollisionLUT.hpp
│   │   ├── HexSimulator.cpp
│   │   └── HexSimulator.hpp
│   ├── IO/
│   │   ├── BinaryParser.cpp
│   │   ├── BinaryParser.hpp
│   │   ├── BinaryWriter.cpp
│   │   ├── BinaryWriter.hpp
│   │   └── libverify.h
│   ├── Render/
│   │   ├── AsciiRenderer.cpp
│   │   ├── AsciiRenderer.hpp
│   │   ├── SvgExporter.cpp
│   │   └── SvgExporter.hpp
│   └── Solvers/
│       ├── AStar.hpp
│       ├── BaseSolver.hpp
│       ├── BaseSolver.cpp
│       ├── GreedyPlanner.cpp
│       ├── GreedyPlanner.hpp
│       ├── RecipeMilp.cpp
│       └── RecipeMilp.hpp
├── tests/
│   ├── unit_test/
│   ├── functional_test/
│   └── benchmark_test/
├── asset/
│   ├── puzzle/
│   ├── solution/
│   └── libverify.so
└── third_party/
    ├── spdlog/
    ├── HiGHS/
    ├── googletest/
    └── benchmark/
```

---

## 🚀 Quick Start

### Prerequisites

- **CMake** ≥ 3.20
- A C++20 compiler (GCC 11+, Clang 14+, Apple Clang 14+)
- Ninja (recommended) or Make

### Build

```bash
git clone https://github.com/Edwardssss/OpusOptima.git
cd OpusOptima

cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

### Run Tests

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON
cmake --build build
cd build && ctest --output-on-failure
```

### Run Benchmarks

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build build
./build/OpusOptimaBenchmarks
```

---

## 🧪 Usage Examples

### CLI Usage

```bash
# Solve a puzzle using the default solver (GreedyPlanner)
./build/OpusOptima -p asset/puzzle/easy/easy-conduit.puzzle

# Specify a solver algorithm (registered names: "GreedyPlanner", "greedy", "RecipeMilp", "milp")
./build/OpusOptima -p asset/puzzle/easy/easy-infinite-product.puzzle -s greedy

# List all available solvers
./build/OpusOptima --list-solvers

# Customize output path
./build/OpusOptima -p asset/puzzle/easy/just-salt.puzzle -o my-solution.solution
```

**Default output directory**: solution files are written to `result/<puzzle-name>-solution.solution`.
Logs are recorded in `log/opusoptima.log`, including module-level details (`solver`, `io`, `simulator`, `validator`).

### SVG Output Example

The SVG renderer generates color-coded atoms (Salt, Air, Earth, Fire, etc.), chemical bonds, and a clean card-style molecular layout suitable for embedding in documents or sharing.

---

## 🧩 Puzzle File Format

OpusOptima parses the proprietary binary `.puzzle` format used by Opus Magnum. Main fields:

| Field | Type | Description |
|---|---|---|
| Version | `int32` (little-endian) | Expected value: `3` |
| Name | C# varint-prefixed string | Puzzle display name |
| Creator Steam ID | `uint64` | Author identifier |
| Permission bits | `uint64` | Sharing settings |
| Input molecules | `Molecule` list | Initial reagents |
| Output molecules | `Molecule` list | Target products |
| Output multiplier | `int32` | Score multiplier |
| Production info | Optional block | Extra data for production-mode puzzles |

Each `Molecule` consists of atoms (type + hex coordinates) and bonds (type + start/end coordinates).

---

## 🧾 Solution File Format

OpusOptima also parses the binary `.solution` format (format tag `7`). Main fields:

| Field | Type | Description |
|---|---|---|
| Format tag | `int32` (little-endian) | Expected value: `7` |
| Puzzle name | C# varint-prefixed string | Name of the corresponding puzzle |
| Solution name | C# varint-prefixed string | User-defined solution name |
| Metrics | `(type, value)` int32 pair list | Cycles, cost, area, instruction count |
| Object count | `int32` | Number of placed objects |
| Objects | Variable-length list | Each object includes type, position, a 5-int32 header, and a type-specific body |

### Supported Object Types

All **23 types** observed in real data are supported, each using a self-describing binary structure:

| Category | Types | Body structure |
|---|---|---|
| Instruction-based | `arm1`–`arm6`, `piston`, `baron` | 5-int32 header (`ic` at index `[3]`) + `N × 5`-byte instructions |
| Track / Pipe | `track`, `pipe` | Header (`segCount` at index `[4]`) + variable-length coordinate segments |
| Fixed-size | `input`, `output`, `out-std`, `out-rep`, `bonder`, `bonder-speed`, `bonder-prisma`, `unbonder`, `glyph-*` (9 types) | Exactly a 5-int32 header (20 bytes) |

The parser has been validated with **4,400+ real solution files** and **169+ puzzle files**, sourced from all campaign levels, tournament archives, and community repositories.

---

## 🛠️ Dependencies

| Library | Purpose | Path |
|---|---|---|
| [spdlog](https://github.com/gabime/spdlog) | High-performance header-only logging library | `third_party/spdlog` |
| [HiGHS](https://highs.dev/) | High-performance LP/IP solver | `third_party/HiGHS` |
| [Google Test](https://github.com/google/googletest) | Unit and functional testing | `third_party/googletest` |
| [Google Benchmark](https://github.com/google/benchmark) | Microbenchmarking | `third_party/benchmark` |

All dependencies are vendored under `third_party/` and built automatically by CMake. No manual installation is required.

The project root also provides `.clang-format`, `.clangd`, and `.gitignore` for consistent style and static-analysis integration.

---

## 📄 License

This project is released under the MIT License — see [LICENSE](LICENSE) for details.

*Opus Magnum is a trademark of Zachtronics LLC. This project is not affiliated with or endorsed by Zachtronics.*

---

## 📚 Acknowledgements

This project was inspired by gtw123's [OpusSolver](https://github.com/gtw123/OpusSolver). The `.puzzle` and `.solution` files used in testing all come from the [omsim](https://github.com/ianh/omsim) project. Thanks to all developers for their contributions.
