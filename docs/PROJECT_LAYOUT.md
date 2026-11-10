# Project layout

What lives where, and why. Keep this file in sync with `.gitignore` and
`tools/README.md`.

## The one rule

**Only source is tracked. Everything generated lives outside the tree.**

```text
<workspace>/OpusOptima          <- this git repository
<workspace>/artifacts           <- every generated artefact (not a repository)
    build/                      compiled trees      (exposed as ./build  -> ../artifacts/build)
    log/                        runtime logs        (exposed as ./log    -> ../artifacts/log)
    result/                     solver runs, probes, reports
                                                     (exposed as ./result -> ../artifacts/result)
    backups/                    pre-refactor snapshots
```

The three symlinks keep every historical relative path (`result/...`,
`./build/OpusOptima`, `log/opusoptima.log`) working while nothing generated sits
inside the repository. `git status` therefore only ever shows source, tests,
assets, tools and docs.

## Repository tree

```text
OpusOptima/
├── CMakeLists.txt          single build entry point (GLOB_RECURSE over src/ and tests/)
├── README.md               project overview (upstream fork documentation)
├── README_zh.md
├── .gitignore              artifact roots, compiler output, tool state
├── .clang-format .clangd   formatting and language-server configuration
│
├── src/
│   ├── main.cpp            CLI: solve / verify / inspect
│   ├── Core/               hex grid, machine state, logging, constants
│   ├── Engine/             instruction interpreter + collision LUT
│   ├── IO/                 puzzle/solution binary format, verifier bridge
│   ├── Render/             ASCII/wireframe rendering of machines
│   ├── Verification/       independent process worker + resource probes
│   └── Solvers/
│       ├── Core/           AssemblySolver (portfolio), BaseSolver, AStar
│       ├── Planning/       assembly plan: atom-to-input mapping, permissions
│       ├── Chemistry/      reaction / purification / metamorphic planners
│       ├── Execution/      execution IR, serial arm scheduling, custody
│       ├── Layout/         machine emitters: row, wheel, linear input, ProgramBuilder
│       ├── Production/     output/purification/production-specific machines
│       ├── Optimization/   local trial improvement passes
│       └── Legacy/         retired planners kept for the legacy portfolio
│
├── tests/
│   ├── unit_test/          gtest unit suites (also used as CLI-level probes)
│   ├── functional_test/    end-to-end suites, some driven by env case files
│   ├── benchmark_test/     benchmarks
│   ├── worker/             standalone probe/worker executables
│   ├── fixtures/           small puzzle fixtures used by the suites
│   └── support/            shared helpers (RepoRoot.hpp: repository root lookup)
│
├── asset/                  data only: no binaries, no build output, no licenses
│   ├── puzzle/             puzzle corpus (2412 files; 2401 imported from
│   │                       gtw123/OpusSolver@fdedd3f0, see README)
│   └── solution/           reference solutions
│
├── third_party/            submodules: HiGHS, spdlog, googletest, benchmark,
│                           and omsim (the native verifier, built from source)
├── tools/                  measurement scripts (see tools/README.md)
└── docs/                   architecture, verifier notes, analysis archive
```

## Build, test, measure

```sh
git submodule update --init --recursive   # includes third_party/omsim
cmake -S . -B build                 # configure (build/ is a symlink outside the repo)
cmake --build build -j4             # build everything
cmake --build build --target OpusOptima OpusOptimaTests
env -C . ./build/OpusOptimaTests    # run the suite FROM the repo root
```

Tests read `asset/puzzle/...` with relative paths, so the working directory must
be the repository root. `env -C . <binary>` is the portable way to guarantee it.

```sh
./build/OpusOptima -p <puzzle> -o <out> -s assembly      # solve
./build/OpusOptima verify --puzzle P --solution S --authority legacy-gate --json
python3 tools/bench_corpus.py                            # authoritative corpus A/B
```

## Conventions that keep reviews honest

* **Native authority only.** A machine is "good" only when
  `--authority legacy-gate` passes it; every measurement goes through that gate.
* **Objective slots are independent.** `cost`, `cycles` and `area` are separate
  slots; a candidate replaces a slot only when strictly better, and the first
  verified candidate is the published baseline.
* **The native gate is a dependency, not an asset.** `libverify.so` is built from
  the pinned `third_party/omsim` submodule; nothing prebuilt is vendored under
  `asset/`, which holds puzzle and solution data only.
* **One emitters file per machine family** under `src/Solvers/Layout/`; scheduling
  primitives (`ProgramBuilder`) stay free of geometry.
* **Environment flags are measurement knobs.** Any flag that changes emitted bytes
  must be inert when unset (`off`/empty/unknown == shipped default) and documented
  next to the code that reads it.
* **Tests never count directory levels.** `tests/support/RepoRoot.hpp` walks up to
  the directory holding `CMakeLists.txt`, so a test file may move between
  `tests/unit_test/`, `tests/functional_test/` or a new subdirectory without
  changing how it locates `asset/` or `result/`. `OPUSOPTIMA_REPO_ROOT` overrides
  the lookup for out-of-tree binaries.
* **Reports live outside the repo** under `../artifacts/result/<topic>-<date>/`
  with a `REPORT.md`; the reproducible commands inside them use `tools/`.
