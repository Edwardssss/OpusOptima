# Architecture

This document describes how the OpusOptima source tree is organised, how a solve
request flows through it, and which invariants must survive refactoring. It is a
structural map, not an acceptance record: capability claims and evidence live in
[`README.md`](../README.md) and `result/`.

## 1. Workspace and container layout

The checkout container is `/home/edwards/OpusOptima`. It holds one CMake project and
all generated evidence:

```
/home/edwards/OpusOptima/
├── OpusOptima/                # the CMake project (all source, tests, docs)
│   ├── asset/                 # puzzles and fixture solutions (no binaries)
│   ├── build/                 # CMake build tree (generated, never edited by hand)
│   ├── docs/                  # architecture, verifier notes, analysis
│   ├── log/                   # runtime `opusoptima.log`
│   ├── result -> ../artifacts/result   # symlink; solver output and run reports
│   ├── src/                   # first-party C++ (see §3)
│   ├── tests/                 # unit, functional, benchmark, worker tests +
│   │                          #   support/ (RepoRoot.hpp, shared helpers)
│   ├── third_party/           # spdlog, HiGHS, GoogleTest, Benchmark, omsim
│   ├── CMakeLists.txt
│   └── README.md / README_zh.md
├── artifacts/                 # everything outside the code tree
│   ├── result/                # published solutions, immutable run reports, snapshots
│   ├── backups/               # pre-change snapshots (tar + SHA-256 manifests)
│   ├── log/                   # long-running experiment logs
│   ├── progress_doc/          # working notes
│   └── session-files/         # archived session material
└── README.md                  # container-level pointer
```

Only `OpusOptima/` is version-controlled source. `artifacts/result/` is read-only by
convention: solvers write there, humans and tools only read it. The `result` symlink
inside the project exists so the CLI defaults (`result/<stem>-solution.solution`,
`result/<stem>-solution.objectives.json`) resolve without absolute paths.

## 2. Build and test

Run from `/home/edwards/OpusOptima/OpusOptima`:

```sh
# Configure (Debug, tests on)
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON

# Build
cmake --build build -j 2

# Full serial suite
ctest --test-dir build -j 1 --output-on-failure

# Focused suites
./build/OpusOptimaTests --gtest_filter='AssemblyDiagnostics.*:CandidateArchive.*:LocalCompaction.*'

# CLI smoke
./build/OpusOptima --list-solvers
./build/OpusOptima -p asset/puzzle/easy/just-salt.puzzle --authority legacy-gate
./build/OpusOptima verify --puzzle asset/puzzle/easy/just-salt.puzzle \
  --solution result/just-salt-solution.solution --authority legacy-gate --json
```

`CMakeLists.txt` globs `src/**/*.cpp` (minus `main.cpp`) into `OpusOptimaLib`, so
adding or moving a source file needs no source list edit. Adding a new include
directory does: every solver subdirectory is listed explicitly in
`OPUSOPTIMA_INCLUDE_DIRS`. Project headers are included by basename
(`#include "RowAssembly.hpp"`), never by `Solvers/...` path.

## 3. Module responsibilities

| Directory | Responsibility |
| --- | --- |
| `src/Core/` | Axial hex coordinates and frame transforms (`HexCoord`, `HexFrame`), atom/molecule/part constants, puzzle specifications, logging. No solving, no I/O side effects. |
| `src/IO/` | `.puzzle`/`.solution` codecs (`BinaryParser`, `BinaryWriter`), `CandidateArchive` (three-slot admission, publication, snapshots), `SolutionVerifier` (authority selection), JSON report shapes (`VerificationJson`), the `libverify` binding to the `third_party/omsim` FFI. |
| `src/Solvers/` | Everything from material planning to bounded candidate compilers. See §4. |
| `src/Verification/` | Independent execution: tape expansion, finite repeating graph/progress, static production semantics, collision analysis, reference-motion cross-checks, worker protocol and worker processes. Must not import solver planning structures. |
| `src/Engine/` | Internal simulator and collision LUT used for development diagnostics, not the publication authority. |
| `src/Render/` | ASCII preview and SVG export. |
| `tests/unit_test/` | Contracts, transformations, negative cases, machine fixtures. |
| `tests/functional_test/` | Original CLI regressions, corpus scripts, rule evidence and env-gated measurement harnesses. |
| `tests/benchmark_test/` | Micro-benchmarks (Google Benchmark). |
| `tests/worker/` | Fault-injection and resource-probe workers that must run in their own process. |

## 4. Solver directory map

`src/Solvers/` is grouped by capability. `AssemblySolver.*`, `BaseSolver.*` and
`AStar.hpp` stay at the root because they are the entry points and dispatcher.

| Subdirectory | Contents |
| --- | --- |
| `src/Solvers/` (root) | `BaseSolver` (registry, `Create`, default name), `AssemblySolver` (portfolio entry, `AssemblyPlan`/`SupplyPlan`, diagnostics), `AStar.hpp` (bounded search helper). |
| `Planning/` | Material-level plans before any geometry: input inventory and buffers (`ClosedMetalInventory`, `InventoryIR`, `InventorySearch`), chemical/recipe planning (`ChemicalPlanner`), demand chains (`DemandDrivenChain`), supply scheduling (`AutomaticSupply`, `SupplyPlanner`, `SupplyExecutionIR`, `MaterialPlanning`, `MaterialPolicy.hpp`), repeating-output and finite-feed resource models. |
| `Chemistry/` | Reaction semantics: dispersion and balanced dispersion, purification, molecular metal split, unification, triplex, life/death, disassembly, whole-feed docking, trackless pairs, chemistry pipeline. |
| `Execution/` | Geometry-independent execution contracts and physical inventory: `ExecutionIR` (side-effect-free schedules), `ExecutionPlan` (physical lowering), arm/fragment custody and motion (`HeldArmMotion`, `RigidMotion`, `RigidCarrier`, `SerialArmExecution`, `SerialFragmentMotion`), fixed-arm helpers (`FixedArmIntervals`, `FixedArmInventory`, `FixedArmJoin`), small policy headers (`SerialPortAccess.hpp`, `ModuleHandoff.hpp`, `RowCompletionTransfer.hpp`, `RowPhaseProfile.hpp`). |
| `Layout/` | Machine layouts: constraint solvers (`ConstraintLayout`), row/conveyor assembly (`RowAssembly`, `RowAssemblyInternal.hpp`, `FeedLayout.hpp`), wheel assembly. |
| `Production/` | The `Production*` family: per-rule compilers for production puzzles (assembly, chemistry, direct feed, distributed purification, execution, finite stock, joint chemistry, layout, molecular, petal assembly, ring processing, routing, transport). |
| `Optimization/` | Local post-processing of published candidates: `LocalOptimizer` (bounded local trials, native-gated), `CandidateIdentity.hpp` (canonical hashing/identity). |
| `Legacy/` | `GreedyPlanner`, `RecipeMilp` (HiGHS MILP), `DiatomicSolver`, `SingleAtomSolver` — experimental/legacy CLI backends, not the recommended `assembly` portfolio. See [`Legacy/README.md`](../src/Solvers/Legacy/README.md). |

## 5. Solve data flow

```
main.cpp
  │  parse CLI (-s, -p, -o, --authority, --experimental-*)
  ▼
BaseSolver::Create(name)              # omitted -s / Create() => "assembly"
  │
  ├── assembly  ──► AssemblySolver (bounded portfolio)
  │                   ├── AssemblyPlanner::PlanSupply   (Planning: inventory, chemistry,
  │                   │                                   reactions, batches, buffers)
  │                   ├── AssemblyPlanner::Plan         (Planning: logical acquire/bond/output
  │                   │                                   operations with reservations)
  │                   ├── ExecutionIR / ExecutionPlan   (Execution: side-effect-free schedule,
  │                   │                                   then physical lowering)
  │                   ├── physical compilers            (Layout + Production + Chemistry:
  │                   │                                   arms, tracks, conveyors, glyphs)
  │                   ├── LocalOptimizer                (Optimization: opt-in local trials)
  │                   ▼
  │                CandidateArchive::Submit ──► SolutionVerifier ──► native gate
  │                   │  (cost / cycles / area slots, immutable entries)
  │                   ▼
  │                CandidateArchive::Publish / Snapshot  (atomic manifest + objectives.json)
  │
  └── greedy | milp | single-atom | diatomic   (Legacy single-shot backends, same gate)
```

Key points:

- Planning produces *logical* operations only (acquire, bond, output) with staging
  coordinates and exclusive resource reservations. Coordinates in an `AssemblyPlan`
  are reservations, not paths.
- `ExecutionIR` is side-effect free; `ExecutionPlan` and the physical compilers turn it
  into concrete arm tapes, tracks, conveyors, pistons and glyph visits.
- Every candidate, wherever it came from, is byte-serialized and priced by
  `CandidateArchive::Submit`, which calls the configured authority. Nothing bypasses it.
- Publication writes the three-slot manifest and the legacy first-accepted output as
  separate atomic renames; snapshots reference immutable generation files.

## 6. Invariants

These must hold after any refactor:

1. **Native gate authority.** Only the configured authority (`legacy-gate` by default,
   `independent`, or `shadow`) may admit or publish a solution. Solver-side diagnostics,
   metrics derived from `Engine`, and plan self-checks are advisory and never sufficient.
2. **Three slots.** `CandidateArchive` keeps exactly three immutable entries, one per
   measured objective (cost, cycles, area). Ties keep the first accepted entry. An entry
   may occupy several slots; the manifest rename is the commit point.
3. **No budget expansion.** The CLI budget remains 20 s total: 15 s for construction and
   admission, 5 s reserved for final verification. Local trials run inside the same
   deadline; they never extend it, and they never recursively refill the parent queue.
4. **Opt-in flags.** Capability experiments are disabled unless their exact switch value
   is set. Setting a flag to an unrecognised value must reproduce the default output
   byte-for-byte, except for the documented default-on opt-out below, whose
   unrecognised value deliberately keeps the legacy geometry. Relevant switches include
   `OPUSOPTIMA_COMPACT_TRANSPORT` (v8 is default
   on; exact `off` restores the legacy queue `{0,1,2,4,5,6,7,3}` and the default is
   `{8,1,2,4,5,6,7,3}`), `OPUSOPTIMA_SHARED_BONDER`, `OPUSOPTIMA_SERIAL_RETURN`,
   `OPUSOPTIMA_TRACK_ENVELOPE`, `OPUSOPTIMA_IDLE_TRACK_ENVELOPE`,
   `OPUSOPTIMA_EMPTY_RETURN`, `OPUSOPTIMA_USED_RAIL`,
   `OPUSOPTIMA_FEED_PREFETCH`, `OPUSOPTIMA_ROW_PIPELINE`,
   `OPUSOPTIMA_ROW_ASSEMBLY_CLOCKS` and the `--experimental-*` CLI switches.
   The scheduled-row compact layout is a **default-on opt-out**:
   `OPUSOPTIMA_COMPACT_LAYOUT` unset or `on` uses the compact geometry; exact `off`
   restores the legacy geometry, and unrecognised/empty values are handled
   conservatively as legacy. Only `CompileScheduledRowAssembly` enables it;
   generic and composite row emitters must stay byte-identical so
   `independent-outputs` sub-machines remain valid.
   The scheduled row also yields a **clocked cycles companion** from the same
   construction: `CompileScheduledRowCandidates` returns the compact cost/area
   machine plus a machine with independent feeder/upper/conveyor clocks. The
   portfolio submits both; the archive keeps the clocked machine only in the
   cycles slot. `OPUSOPTIMA_ROW_CLOCK_PORTFOLIO=off` disables the companion.
   `OPUSOPTIMA_ROW_GAP_PORTFOLIO=off` disables the gap0 primary/fallback.
   Gap0 is the primary compact/clocked candidate; if the native gate rejects it,
   the gap2 machine is submitted as a fallback, so the three accepted slots stay
   available for ordinary + compact + clocked. Optional compiles are skipped
   when the 20 s budget is tight. In the clocked companion, the ordinary
   product-finish upper reset runs on `upperTime`, so the next product's lower
   staging/feed overlaps it; the compact path keeps the original shared-clock
   reset.
   The clocked companion is passed through the default v8 transport compaction
   before native submission. The compacted clocked machine has the same cycles
   as the raw clocked machine and the compact machine's cost/area, so it ties
   the cost and area slots while strictly shortening their cycles; the archive
   then publishes one machine that is simultaneously best in cost, cycles and
   area. If native rejects the compacted clocked machine, the raw clocked
   machine and then the gap2 clocked fallback are submitted in order.
   `OPUSOPTIMA_ROW_CLOCK_LOCAL=off` skips only this local post-pass and is used
   for before/after measurements; it is not part of the production default.
5. **Determinism.** The same inputs, same flags and same budget produce identical bytes.
   Candidate ordering is defined by the fixed variant queue and stable slot ties.
6. **Bounded search.** Every search states its hard limit (atoms, states, steps, gates)
   and reports exhaustion as failure rather than as proof of impossibility.
7. **Verification independence.** `src/Verification/` must not depend on solver planning
   types; the independent path re-derives meaning from the puzzle and solution bytes.
8. **No game or save mutation.** Tooling never launches the game, writes saves, or edits
   `artifacts/result/`.

## 7. Legacy and experimental backends

`SingleAtomSolver` (single unchanged atom), `DiatomicSolver` (one normal-bond pair),
`GreedyPlanner` (fast greedy pickup) and `RecipeMilp` (HiGHS integer recipe) remain
selectable by exact name because published archives and regression tests depend on them.
They are single-shot candidate generators with narrow coverage, not substitutes for the
`assembly` portfolio. The historical `-s default` alias still means greedy, and there is
no fallback between backends: an unknown name fails.

## 8. Related documents

- [`IndependentVerifier.md`](IndependentVerifier.md) — independent verifier design and rules.
- [`ReviewAndNextSteps.md`](ReviewAndNextSteps.md) — verifier replacement notes.
- [`../README.md`](../README.md) — capabilities, acceptance and evidence links.
