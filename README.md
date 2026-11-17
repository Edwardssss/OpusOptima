# OpusOptima

A C++20 toolkit for parsing, verifying, rendering and solving Opus Magnum puzzles.
The default `assembly` solver is a bounded portfolio; the upstream Standard port is
offered first by the CLI's extra portfolio.

**Browser demo:** https://edwardssss.github.io/OpusOptima/
**中文说明：** [README_zh.md](README_zh.md)

## Status

Comparison target: the **OpusSolver oracle** — the best cost/cycles/area solutions
produced by [gtw123/OpusSolver@fdedd3f](https://github.com/gtw123/OpusSolver/tree/fdedd3f01a238b941ceba8bace7ea35cbd3b8c41)
on the same 146-puzzle reference corpus.

| Metric | Value |
|---|---|
| Reference corpus | 146/146 byte-distinct puzzles |
| Cost: OpusOptima vs OpusSolver oracle | **0.8567×** (89,825 vs 104,855) |
| Cycles: OpusOptima vs OpusSolver oracle | **0.7420×** (66,427 vs 89,524) |
| Area: OpusOptima vs OpusSolver oracle | **0.8211×** (20,285 vs 24,705) |
| Slot wins/ties/losses vs oracle (cost, cycles, area) | 75/70/1, 66/76/4, 70/73/3 |
| Puzzles winning all three slots vs oracle | 55/146 |
| Puzzles losing all three slots vs oracle | 0/146 |
| External 2401-puzzle sample | **300/300** |
| Full C++ suite | 1103 tests: 1098 passed, 5 skipped, 0 failed |

Two final audits are slot-identical (0/438 differences). The previously timing-out
`GEN737` puzzle now completes in about 12–14 seconds.

## Build

Requirements: a C/C++20 compiler, CMake 3.20+, Ninja, and the pinned submodules.

```sh
git submodule update --init --recursive
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j4
ctest --test-dir build --output-on-failure -j1
```

Use a separate directory with `-DCMAKE_BUILD_TYPE=Release` for optimized runs.

## Run

```sh
./build/OpusOptima --help
./build/OpusOptima -p asset/puzzle/easy/just-salt.puzzle
./build/OpusOptima -p asset/puzzle/campaign/ch1-and-prologue/P010.puzzle -s assembly
./build/OpusOptima verify --puzzle <puzzle> --solution <solution> --authority legacy-gate --json
```

Omitting `-s` selects the bounded `assembly` portfolio. Output defaults to
`result/<puzzle-stem>-solution.solution`; logs go to `log/opusoptima.log`.
`verify` re-checks an existing solution without publishing or rewriting it.

## Verification

The publication authority is the pinned upstream [ianh/omsim](https://github.com/ianh/omsim)
verifier, built from the `third_party/omsim` submodule and isolated in native worker
processes. The default authority is `legacy-gate`; `--authority independent` is
available only within its explicitly supported finite subset.

A missing submodule fails configuration instead of silently disabling verification.
`-DOPUSOPTIMA_ENABLE_LIBVERIFY=OFF` is for compile-only builds, where default
publication fails closed.

## Browser

`wasm/` is a separate Emscripten target containing the Standard solver, binary
codecs and omsim. `web/` is a bilingual static page with:

- upload/solve/verify/download;
- immediate read-only machine layout plus omsim per-cycle animation;
- visual puzzle builder for atoms, bonds, multiple ports and production metadata;
- advanced text/JSON editing and a Refined Gold (冶炼金) example.

GitHub Pages deployment is in `.github/workflows/pages.yml`. Local build:

```sh
emcmake cmake -S wasm -B build-wasm -DCMAKE_BUILD_TYPE=Release
emmake cmake --build build-wasm --parallel 2
mkdir -p web/dist
cp web/index.html web/app.js web/i18n.js web/layout.js web/puzzle-builder.js \
   web/style.css web/example.puzzle web/dist/
cp build-wasm/opusoptima.js build-wasm/opusoptima.wasm web/dist/
```

The page verifies with in-process omsim; it is not the native isolated
`legacy-gate` publication transaction.

## Layout

| Path | Role |
|---|---|
| `src/IO/` | Puzzle/solution codecs, verification worker, publication |
| `src/Solvers/` | Planning, physical lowering and bounded candidate compilers |
| `src/Solvers/Standard/` | Port of the upstream Standard solver |
| `src/Verification/` | Independent and reference verification backends |
| `src/Core/`, `src/Engine/`, `src/Render/` | Coordinates, simulation and rendering |
| `wasm/`, `web/` | Browser solver, editor and Pages UI |
| `tests/`, `asset/`, `third_party/` | Tests, puzzle corpus and dependencies |

## License

Project code is MIT licensed. OpusSolver attribution is in
[third_party/OpusSolver-LICENSE.txt](third_party/OpusSolver-LICENSE.txt); omsim is
pinned under `third_party/omsim` with its license in
[third_party/omsim/COPYING](third_party/omsim/COPYING).
