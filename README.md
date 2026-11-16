# OpusOptima

> **Current direction:** items 1–6 are complete. On the 146 byte-distinct reference puzzles the shipped solver totals cost **0.857×**, cycles **0.742×** and area **0.821×** of the OpusSolver oracle; 55 puzzles win all three slots and none loses all three. Item 7 external validation: a fresh 300-file sample of the upstream 2401-puzzle corpus now passes **300/300**; the previously timing-out `GEN737` completes in about 12-14 seconds. A full 2401-file run is tracked separately. The independent-certification roadmap is paused; pinned omsim, legacy protection, explicit backends and 2/10/20 defaults remain. Historical seen puzzles are not unseen evaluation. The retained omsim binary is hash-locked, not source-reproducible; do not replace it from a mutable release URL.

A C++20 toolkit for parsing, rendering and solving Opus Magnum puzzle files.

[中文说明](README_zh.md)

## Current status

The latest legacy-gate validation solved **169/169 original files**, or
**146/146 byte-distinct puzzles**, including **37/37 journal files**. It reran all
five backends for every file: **845 attempts, 20 seconds per attempt**, without
merging historical results. The corresponding regression run passed **662 tests**
with no failures or skips; the passing corpus is unchanged from the B3–B6 baseline.

These are measurements on the current local corpus, not a guarantee for arbitrary
new puzzles, globally optimal scores or complete equivalence with the original game.

### Upstream Standard port (2026-11-15)

The default `assembly` CLI now includes a faithful C++ port of
`gtw123/OpusSolver@fdedd3f`'s Standard solver under
[`src/Solvers/Standard/`](src/Solvers/Standard/) (MIT). It is enabled
through the CLI's extra-portfolio path only, so the library API keeps its original
first-result and quota contracts. It solves **146/146** corpus puzzles and, after
the one-shot structural portfolio, upstream recipe/build-order/subset enumeration
and local passes, the `jobs=2` audit totals are cost **0.857×**, cycles **0.742×**
and area **0.821×** of the OpusSolver oracle. The standalone port is within
1.06–1.09× of the oracle; mechanical-only (`instructions>0`) totals are now
**0.966× / 0.989× / 0.973×**, i.e. below the oracle on all three objectives, and
no corpus puzzle loses every slot.

### Reference-corpus results (items 1-6)

Across the 146 byte-distinct reference puzzles the final audit totals are cost
**89,825** (0.8567× oracle), cycles **66,427** (0.7420×) and area **20,285**
(0.8211×). Per-slot wins/ties/losses are 75/70/1 (cost), 66/76/4 (cycles) and
70/73/3 (area); 55 puzzles win all three slots and none loses all three.
Mechanical-only (`instructions>0`) totals are 0.9657× / 0.9887× / 0.9733×.
Pipeline/cheap-arms totals are 0.9283× / 0.9624× / 0.9320×; the universal
family is 0.8794× / 0.7007× / 0.8438×. For item 6, 71/146 puzzles have at least
one single solution that is no worse than the oracle on all three objectives and
strictly better on one. Two `final3`/`final4` audits are slot-identical
(0/438 differences). The external 2401 corpus is tracked separately: a 300-file
sample passes **300/300** ([results](result/external-300-20261116/results.json));
the `GEN737` route search now honors the shared deadline and completes in about
12-14 seconds.

- [Full coverage report](result/verifier-m2-m6-certified-legacy-fresh-20261029/coverage.json)
- [Baseline comparison](result/verifier-m2-m6-certified-legacy-fresh-20261029/comparison.json)
- [B3–B6 implementation, evidence and limitations](tests/functional_test/ProductionB3B6Progress.md)

## Repository layout and developer workflow

Only source is tracked. Build trees, logs and measurement reports live outside the
repository under `../artifacts/`, exposed through the `build/`, `log/` and
`result/` symlinks, so `git status` only ever shows source, tests, assets and tools.

- This README is the only project documentation; there is no separate `docs/` tree.
- [`tools/README.md`](tools/README.md) — measurement scripts

```sh
cmake -S . -B build && cmake --build build -j4
env -C . ./build/OpusOptimaTests          # run the suite from the repository root
python3 tools/bench_corpus.py             # authoritative corpus A/B before any default flip
python3 tools/probe_layout.py --count 12  # emitter-level intake vs feeder comparison
```

## Restricted connected-row candidate

Ordinary RowAssembly has a bounded connected-row feed/handoff candidate for direct
singleton supply and complete one/two-row horizontal chains (width 2–8). It moves
an already connected row as one held piece, not independently parallel atoms.
`OPUSOPTIMA_ROW_COMPONENT=on` explicitly opts into replacing the clearance
portfolio slot. Unset, `off`, and unrecognized values retain the original
clearance slot and shared quotas. The candidate shares existing quotas and full
verification. This is an experimental
mechanism, not a claim of better real portfolio winners. See the
[mechanism and domain](result/solver-component-row-round7/mechanism.md) and
[round-7 evidence](result/solver-component-row-round7/report.md).

## External OpusSolver corpus

The 2401 puzzle files imported from pinned upstream revision
`fdedd3f01a238b941ceba8bace7ea35cbd3b8c41` (`gtw123/OpusSolver`, `test/puzzles`)
now live **inside the regular corpus** under [`asset/puzzle/`](asset/puzzle/): the
imported groups were merged into the existing `campaign/`, `easy/`, `edge-cases/`,
`journal/`, `tourney-2019/`, `tourney-2020/`, `tourney-2021/` and
`workshop-versions/` directories, and the new groups `24hour-1-puzzles/`,
`24hour-1-sample/`, `24hour-1-test/` and `custom/` were added next to them. 2236
files were novel, 165 were byte-identical to files already present, and no name
collided with different content, so there is a single puzzle tree and a single
`--corpus` argument for every tool.

Every imported file was checked against its Git blob hash during the import
(upstream revision `fdedd3f01a238b941ceba8bace7ea35cbd3b8c41`, 2401 paths, 2378
byte-distinct, 142 hashes overlapping the original corpus, 2236 novel). No
upstream code was executed during that import (the later paired benchmark did
execute it). Per-group counts and the per-file hash list were kept in the import
working notes rather than in the tree: the pinned revision plus this summary are
enough to re-derive them, and the puzzles themselves are now ordinary corpus
files.

| Scope | Passed / total | Coverage |
| --- | ---: | ---: |
| All imported files | 2273/2401 | 94.67% |
| Byte-distinct imported puzzles | 2250/2378 | 94.62% |
| Byte hashes absent from the original corpus | 2108/2236 | 94.28% |
| Overlapping original hashes | 142/142 | 100% |

The [full report](result/opussolver-fdedd3f-full-20261028/coverage.json) contains
12005 attempts: all five backends per file, 20 seconds per attempt, four isolated
CLI workers. Binaries were frozen, with no solver tuning or historical-result reuse.
This external measurement predates the worker refactor and remains a frozen baseline,
not a fresh measurement of every subsequent binary.
This measures OpusOptima on that corpus, not a head-to-head OpusSolver comparison;
byte novelty is not semantic deduplication or proof of an unseen holdout.
The imported puzzles come from `gtw123/OpusSolver` (MIT licensed, © its authors);
they are data, not code, and no separate license copy is kept next to them.

## Build

The verified publication path uses Linux/POSIX process isolation and the upstream
[native verifier FFI](https://github.com/ianh/omsim), built from the pinned
`third_party/omsim` submodule. Requirements are a C/C++20 compiler, CMake 3.20+,
Ninja or another CMake generator, and the dependencies under `third_party/`.
Node.js 20+ is used for corpus/benchmark scripts; Python 3 is used for the game
evidence tests.

```sh
git submodule update --init --recursive   # fetches omsim at the pinned revision
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON
cmake --build build -j 2
ctest --test-dir build --output-on-failure -j 1
```

CMake builds `libverify.so` from the six omsim C sources with the same flags the
upstream `Makefile` uses for its own `libverify.so` target (`-O2 -std=c11 -shared
-fpic`, linked with `-lm`) and links it into `OpusOptimaReferenceWorker`. The
submodule commit is the reproducible anchor:

- pinned revision `a88d12d8d100784aa525ee90259255b869bcba38` in `third_party/omsim`,
- the exported symbol set is identical to the retired prebuilt library,
- behaviour is pinned by the test suite and the corpus baselines: a machine the
  gate used to reject (`collision during motion phase`, cycle 1798 at `[12,1]`) is
  still rejected at the same cycle and cell, and accepted machines report
  identical metrics.

No prebuilt verifier binary is vendored any more. If the submodule is missing the
configuration fails with the `git submodule update --init third_party/omsim`
instruction instead of silently disabling verification. `-DOPUSOPTIMA_ENABLE_LIBVERIFY=OFF`
still exists for compile-only builds, where the default `legacy-gate` publication
fails closed and the explicit independent profile can only publish within its
supported finite subset.

For optimized builds use a separate directory, for example
`cmake -S . -B cmake-build-release -G Ninja -DCMAKE_BUILD_TYPE=Release`.
The coverage runner below explicitly uses `build/OpusOptima`; record build options
when comparing performance.

## Run

Run from the repository root:

```sh
./build/OpusOptima --list-solvers
./build/OpusOptima --help
./build/OpusOptima -p asset/puzzle/easy/just-salt.puzzle --authority independent
# Verify that existing file without solving or rewriting:
./build/OpusOptima verify --puzzle asset/puzzle/easy/just-salt.puzzle \
  --solution result/just-salt-solution.solution --authority independent --json
./build/OpusOptima -p asset/puzzle/campaign/ch1-and-prologue/P011.puzzle -s assembly
./build/OpusOptima -p asset/puzzle/workshop-versions/w2591419339.puzzle \
  -s assembly -o result/artificial-ore.solution
```

Omitting `-s` now selects the recommended `assembly` bounded portfolio, as does
`BaseSolver::Create()` in the C++ API. Explicit `-s assembly` is equivalent.
`single-atom` remains available for unchanged single atoms; `diatomic` handles
unchanged normal-bond pairs. `greedy` and `milp` (`mip`) remain experimental
candidate generators, not reliable complete-machine backends. All explicit backend
and class-name aliases remain exact selections; unknown names fail, with no fallback.
For compatibility, the historical **`-s default` / `Create("default")` alias still
means greedy**, not omitted `-s` / `Create()`; use `assembly` for the recommendation.
There is no application GUI solver selector in this source tree to synchronize.
OpusSolver is a benchmark-only opponent, never an automatic fallback; .NET is not
a product runtime dependency.

Recommendation is about useful bounded coverage, not competitive solution quality:
[mainline 3](result/solver-mainline3-20261105/report.md) raised fixed external16 from
0 to 4 successes, leaving **12/16 failures** (opponent 16/16). All four new solutions
are opponent-dominated; among the older 130 common successes, the opponent dominates
104, this solver dominates none, and 26 are tradeoffs. Local146 success and four-metric
tuples were unchanged. See [entry-point acceptance](result/solver-mainline4-20261105/report.md).
Default output is `result/<puzzle-file-stem>-solution.solution`; logs are written
to `log/opusoptima.log`.

## Capabilities and acceptance

- Version-3 puzzle parsing, solution parsing/writing, axial hex coordinates,
  ordinary/triplex bond masks, ASCII preview and SVG rendering utilities.
- Bounded material planning for whole input batches, retained fragments,
  chemical coproducts, catalyst borrowing, multiple outputs and finite inventory.
  Closed periods with only Salt, cardinals, Vitae, Mors and Quintessence skip
  source-batch totals incompatible with conserved material weight (Quintessence
  weighs four; the other species weigh one). Disposal, production, other species
  and finite retained inventory are outside this necessary feasibility check.
- With `OPUSOPTIMA_FINITE_RETENTION=1`, assembly can attempt a bounded six-product
  row completion combining cardinal/Quicksilver singleton feeds, Animismus and
  one static cardinal wheel identity (Air, Earth, Fire or Water). The target
  determines wheel pose; one surplus Vitae per product must occupy declared
  finite storage. This family shares candidate/deadline limits and requires an
  actual native report before acceptance. Multiple wheel identities and arbitrary
  mixed-graph assembly remain unsupported in this finite coproduct family.
- Ordinary monatomic wheel assembly can combine direct feeds, calcified Salt and
  multiple derived cardinal types. The mixed Salt route retains original input
  indices and requires actual native acceptance, including at the archive boundary;
  it does not require finite-retention opt-in. Supply rails reserve only active
  wheel stations while retaining the existing station and delivery clearances.
- Closed material planning automatically enables the existing singleton wheel path
  for version-3 nonproduction puzzles with multiplier 1–8, one to four inputs of
  one to four Salt/Air/Earth/Water atoms with ordinary source bonds, and at least
  one calcifiable source atom. One connected finite output has two to four atoms,
  exactly two Fire endpoints of exactly one colored edge (mask 2/4/6/8/10/12/14);
  other atoms are Salt/cardinals and other bonds are ordinary. Arm, ordinary
  bonder, unbonder, calcifier, prism, duplication, wheel, grab/drop, rotation and
  reset permissions are required. With neither source Fire nor Quintessence,
  nonwheel duplication cannot seed Fire, dispersion cannot start, and unification
  cannot supply its missing Fire reagent. This guard adds no searches or layouts:
  the existing 4096 states, 32 histories, 512 steps/identities, dividing periods
  and shared three-second material/physical deadline remain in force. The physical
  compiler still checks singleton Calcify provenance, one static wheel, exact
  prism/glyph/port witnesses, custody and complete material/reset state. A guard
  match alone proves no machine. Wheel-derived serial candidates in this domain
  and their optional children require actual `LegacyGate` authority before acceptance
  or publication; Independent reports cannot occupy their accepted slots. Existing
  source-Fire precursor and other serial admission remain unchanged.
- Static embedding combines directly supplied singleton types, calcification,
  static wheel duplication and exact colored masks between final Fire endpoints.
  The exact prism planner runs first. With unbonder permission, a second placement
  pass may spill colored bonds onto explicitly ordinary target edges only. Native
  glyph order clears those edges after all prisms and restores their ordinary bonds
  before output consumption; absent edges and extra colors on colored target edges
  remain forbidden. Prisms may not touch wheel footprints. Both passes, wheel
  sharing and prism placement share at most 576
  checks under the existing deadline; each output supports at most sixteen atoms
  and thirty-six edges, with at most two outputs and radius three. The family runs at most once, either after a first
  solution or as a fallback before material search. Actual native acceptance is
  mandatory, the first verified solution is retained, and exact `off` disables
  both routes through `OPUSOPTIMA_STATIC_EMBEDDING`. Directly supplied ordinary
  products and singleton identity outputs may use the same embedding without
  chemical permissions; every required output is retained. Unused molecular
  reagents do not disqualify the family: emitted supplies still reference only
  actual singleton input indices, never virtual atoms extracted from a molecule.
  Ordinary products may also combine direct feeds with bounded metal purification
  when permission 15 and a genuine lower-metal singleton are present. Direct feeds,
  adjacent purification and adjacent projection retain their existing priority.
  Otherwise the nearest supplied lower metal supports a depth-two-to-five triangular
  cascade (first original input index on ties). Depth `n` uses `n+1` real dispensers
  and `n(n+1)/2` glyphs, at most 15, with downstream/rightmost glyphs emitted first.
  Each tile tries six orientations about its actual target product cell. Its entire
  source/intermediate triangle stays clear of every other output cell and tile.
  Purification also composes with stable Salt calcified from genuine cardinal
  singleton feeds using permission 12, alongside direct ordinary neighbors. Salt
  waits for the private cascade root. Calcifiers occupy only Salt target cells,
  outside all private source/intermediate cells. Wheels, colored bonds and
  purification/projection mixtures remain unsupported. Tiles do not feed one
  another or share intermediate stock. Placement shares the same 576 checks and
  caller deadline; whole-puzzle native admission, portfolio slots and the missing
  Vitae/Mors proof remain unchanged.
- Rigid molecular covers support up to sixteen atoms per source or target, eight
  inputs, two outputs and the existing radius-eight domain. Covers are accepted
  only when their inherited edges and allowed residual bonders satisfy every
  output and at least one whole molecular feed is used. An infeasible first cover no longer blocks later covers; all outputs share
  the existing 20,000-node limit and caller deadline. Tiles are stably ordered by
  saved inherited-bond cost minus required calcifier/unbonder cost per atom; this
  is a bounded search heuristic, not a minimum-cost guarantee. With unbonding
  permission, an atom-preserving rigid cover may cut excess source edges and add
  only missing target edges. Cuts never overlap requested target edges, and
  cross-output atom overlap is rejected before construction.
- The ordinary row portfolio reuses its existing first row construction for
  dependency-aware empty-reset scheduling. No-overlap cases retain ordinary
  lowering, and the caller deadline is forwarded without a new time allowance.
  `OPUSOPTIMA_ROW_RETIMING=off` keeps serial lowering; the other variants retain
  their remaining construction slots.
- A bounded native-only closed wide-metal route attempts one 2–12-atom metal input whose
  minimum height over rotations is four rows, with one metal output and no disposal.
  It derives a whole-batch period by mass conservation, requires exact IR closure,
  and uses a sparse-source serial layout without changing the old serial API's layout.
  The route charges the shared construction budget before planning and retains the
  existing node, submission and outer deadline limits; search and construction share
  a three-second sub-deadline, while gate calls retain the caller's verification budget.
- Closed inventory search prunes new all-metal source batches whose cumulative
  lead-equivalent mass would exceed the requested products. The bound is disabled
  for disposal or any nonmetal input/output; it does not change search budgets.
- Molecular feed composition uses explicit finite-batch slices and a capacity-one
  exit reservation. The last slice includes cleanup, and the exit remains occupied
  until the receiver moves the atom away. Partial batches and overlapping reservations
  fail closed. This scheduling interface preserves existing layouts and tapes; it
  does not certify swept geometry or authorize sharing physical factories.
- `OPUSOPTIMA_SHARED_FEED_RESOURCE=on` enables a bounded post-first trial sharing
  one finite extraction factory between the two salt inlets of an Animismus site.
  It requires a native-accepted LifeDeath parent, one or two ordinary or existing
  colored outputs, and
  genuine multiatom Animismus feed; singleton inputs remain independent. Sharing stays
  within each output, with joint demand compiled once and checked exactly. The
  existing transport layout and native gate remain. It replaces the static embedding
  slot only when an output requires Vitae/Mors without a same-type genuine singleton
  input, which the static emitter cannot derive. Bounded derived Gold-through-Tin
  suppliers and requested synthetic Quintessence use original puzzle inputs and the
  same deadline. Cardinal suppliers preserve whole molecules and original input
  indices; a registered usable real Quintessence source suppresses the shared synthetic
  probe. Salt unification retains singleton-salt validation and molecular restrictions.
  Colored outputs retain Fire endpoints, permission 11, and nonempty color masks
  contained in 14. The existing row compiler uses ordinary proxy bonds and original
  labels with feed spacing three and the unchanged deadline. Single-product and
  compact sharing remain unsupported. The trial
  adds no construction slot and carries the enclosing deadline through bounded
  lowering. It is off by default and may consume verification time needed by later
  candidates.
- The existing local idle-arm trial can remove no-op-only mechanical placeholders
  from entirely passive machines when ordinary empty-tape removal made no change.
  On explicit active tapes without reset/repeat expansion, it also removes no-op
  arms whose instruction times do not exceed the retained program end. Arms that
  determine the period, wheels, and unknown instruction programs remain intact;
  archive verification still decides acceptance before any objective slot changes.
  The eight-trial queue defers its standalone 60-degree rotation until after timing
  and structural trials; explicit shared-bonder mode retains its original ordering.
- Physical lowering using fixed arms, pistons, tracks, handoffs and production
  chamber/conduit layouts. Supported production patterns include compact reaction
  stations, ring processing, direct feed overlays and distributed purification.
  Multi-stream routing skips disjoint layouts when their minimum chamber/conduit
  demand exceeds capacity, preserving the existing shared-search budgets.
- A separate bounded production executor that does not consume solver IR,
  schedules or collision certificates.

The default CLI gate writes a temporary solution, runs the reference-backed
verifier in an isolated process, writes measured metrics, verifies again and only
then atomically replaces the destination. Explicit independent mode follows the
same transaction using independently computed metrics. Failure preserves an
existing output; `HexSimulator` is not the publication authority.

Separate exec-based independent and reference workers keep `libverify.so` out of
the CLI, parent library and independent worker. The initial `finite-ordinary-v1`
profile now supports independent finite-output execution, all four metrics and
interval-certified nominal motion; it can publish with reference support disabled.
Production walls/conduits and other uncertified combinations remain unavailable,
and the global default remains `legacy-gate`.
See the verifier sections below and [acceptance evidence](result/independent-finite-certified-shadow-20261029/provenance.json).

Vial/nondefault-display-bound production candidates additionally require the
independent production executor and matching completion cycles. Version-locked
original-game IL evidence identifies vials and the two locally named “shrink”
fields as display metadata; it does **not** establish a time-varying room-shrink
rule. Unknown execution profiles still return `Unavailable` or are rejected.

Finite ordinary completion requires every output to receive
`6 × outputMultiplier` products. Verification certifies completion, not indefinite
collision-free operation after completion. The legacy production gate still
requires native continuous-collision acceptance; its sampled replay is not the
new independent interval proof. The latter certifies the declared nominal ruleset,
not complete original-game or all-platform floating-point equivalence.

Frozen-machine differential replay covered **2254 puzzle/solution combinations**:
**1019 independent passes**, **801 timeouts**, **434 unavailable**, and **zero
status/four-metric discrepancies**. Unknowns and timeouts are not counted as passes.
This is verifier-domain coverage, not a new solver sweep of the imported corpus.

## Reproduce validation

```sh
ctest --test-dir build --output-on-failure -j 1
python3 -I tests/functional_test/GameProductionEvidence.test.py
node --test tests/functional_test/FairBenchmark.test.cjs
node --test tests/functional_test/LocalCoverage.test.cjs
node tests/functional_test/LocalCoverage.cjs result/<new-empty-directory>
# To measure the separate imported corpus:
node tests/functional_test/LocalCoverage.cjs result/<another-new-directory> \
  --corpus asset/puzzle
```

Use a new directory for every coverage run. Do not rebuild the shared library
while tests or scans are running. Synthetic fixtures are excluded from the corpus
coverage denominator. A `--failed-from` scan is a subset diagnostic, not full coverage.

For comparison against another solver, use the
[fair benchmark protocol](tests/functional_test/FairBenchmark.md), which records
byte-deduplicated development/holdout groups and symmetric validation. The current
corpus has already influenced development; its holdout is not an unseen external test set.

## Code and documentation map

The table below is the code map. The solver flow is `main` → puzzle parser → assembly/portfolio → candidate archive → native gate; `src/Solvers/` holds the capability families and their shared planning/lowering code.

| Location | Role |
| --- | --- |
| [src/Core/](src/Core/) | Coordinates, molecule/part data and shared frame transforms |
| [src/IO/](src/IO/) | File codecs, verification worker and atomic publication |
| [src/Solvers/](src/Solvers/) | Material plans, physical lowering and bounded candidate compilers |
| [src/Verification/](src/Verification/) | Independent production execution |
| [src/Engine/](src/Engine/), [src/Render/](src/Render/) | Internal simulation and rendering utilities |
| [tests/unit_test/](tests/unit_test/) | Contracts, transformations, negative cases and machine fixtures |
| [tests/functional_test/](tests/functional_test/) | Original CLI regressions, corpus scripts and rule evidence |
| [wasm/](wasm/) | Emscripten target and the byte C ABI for the browser solver |
| [web/](web/) | Static GitHub Pages UI and the module bootstrap |
| [.github/workflows/](.github/workflows/) | Pages build and deployment workflow |
| [result/](result/) | Generated solutions, immutable run reports and logs |
| [third_party/](third_party/) | Dependencies and upstream license notices; not project formatting scope |

Historical acceptance records, not current capability specifications:

- [B1/B2 rule evidence and initial independent execution](tests/functional_test/ProductionB1B2.md)
- [Three nonproduction targets](tests/functional_test/ThreeTargetAcceptance.md)
- [Retained-fragment work](tests/functional_test/RetainedFragmentProgress.md)
- [Initial real-target breakthrough](tests/functional_test/RealTargetProgress.md)
- [Phase-A failure matrix](tests/functional_test/RemainingCompositionA1.md)
- [Phase-A acceptance](tests/functional_test/RemainingCompositionAcceptance.md)

## Solver architecture and invariants

The data flow is `io::PuzzleData` (binary parser) → `AssemblySolver` planning and
bounded candidate compilers → `CandidateArchive` verification/publication → the
native `legacy-gate`. The upstream Standard port lives under
`src/Solvers/Standard/`; it is offered first by the CLI extra-portfolio and keeps
the library's first-result and quota contracts. A refactor must preserve:
puzzle bytes are immutable inputs; planning certificates and independent
verification stay separate; only verified bytes are published; the first verified
result is retained; the archive keeps one minimum per objective slot; there is no
automatic OpusSolver fallback; and the default authority remains `legacy-gate`.
The four metrics are measured by the pinned omsim gate, not by `HexSimulator`.

## WebAssembly and GitHub Pages

The web build is a **separate target** (`wasm/` or `web/`) that reuses only the
portable solver/IO/render code; it must not change CLI defaults, the file archive
or the native worker path. Pure WASM can be hosted on GitHub Pages (static files,
single-thread Web Worker, no custom COOP/COEP headers). Pages cannot run the
native CLI, `posix_spawn` verifier or `libverify.so`; for those, keep the frontend
on Pages and put solving/verification on a self-hosted/container backend. The
first target is in `wasm/` with the static page in `web/`. It builds one
single-threaded module containing the upstream Standard port, the binary codecs
and omsim, exposes `opusoptima_solve` / `opusoptima_verify` / `opusoptima_describe` / `opusoptima_trace` / `opusoptima_make_puzzle` through a byte C ABI,
and deploys `web/dist` with `.github/workflows/pages.yml`. Local build:

```sh
emcmake cmake -S wasm -B build-wasm -DCMAKE_BUILD_TYPE=Release
emmake cmake --build build-wasm --parallel 2
mkdir -p web/dist
cp web/index.html web/app.js web/style.css web/dist/
cp build-wasm/opusoptima.js build-wasm/opusoptima.wasm web/dist/
```

Enable **Settings → Pages → Build and deployment → Source: GitHub Actions** once;
then push to `main` or run the `Deploy browser solver to GitHub Pages` workflow
manually. The page verifies candidates with the in-process omsim check, not the
native isolated `legacy-gate` transaction. A puzzle editor/solution renderer and
a server-backed native authority remain future work.

## Development

Format first-party C++ with `clang-format-19` and the repository
[.clang-format](.clang-format). Do not reformat third-party sources or generated
reports. Changes to planning, execution or verification need focused positive and
negative tests; acceptance changes also need original CLI and fresh-corpus checks.
Keep solver-derived planning certificates separate from independent verification.

First independent publication is available; next improve the unchanged-budget
execution timeouts, strengthen original-game/numeric-model evidence and certify
production space, repeating outputs and other excluded rules before any default
cutover. Local 100% solver coverage is not a universal verifier or solver proof.

## Current four-mainline integration acceptance (2026-11-05)

The bounded dependency lock, fair baseline, cardinal-to-Salt wheel supply and
assembly CLI/API default deliveries passed [current cumulative acceptance](result/solver-mainlines-final-20261105/report.md):
782 enabled / 503 disabled CTests and 46 Node tests. Fixed external16 improves
0→4, with 12 failures remaining; current omitted/explicit assembly agree.
This is not full-library coverage, independent certification or a quality win
over OpusSolver; the local146 unchanged result is historical, not a new scan.

## License

Project code is distributed under the [MIT license](LICENSE). Dependency terms
and the [OpusSolver attribution](third_party/OpusSolver-LICENSE.txt) apply to their
respective components; the external verifier is the upstream
[ianh/omsim](https://github.com/ianh/omsim) project, pinned as the
`third_party/omsim` submodule and built from its own sources, with its licence in
[third_party/omsim/COPYING](third_party/omsim/COPYING).

## Verified objective archives (step 2)

`-o` remains the **first verified candidate**, not a changing objective winner. On
success `<output>.objectives.json` references complete immutable generation-named solution files in an exclusive sibling bundle.
Slots minimize their own measured metric among admitted candidates only; equal
primary values retain the first entry, without secondary weighting. Duplicate
hashes are valid. No feasible candidate means three empty API slots and no new
public output. Every entry carries exact bytes/SHA-256, input SHA-256, all measured
metrics, authority/rules/profile/engine and strategy provenance.

`io::CandidateArchive::Submit(solution, strategy)` is the portfolio's real-gate sink;
`Slots()`/`First()` expose immutable accepted records. Admission verifies and
archives privately; `Publish(options)` explicitly revalidates unique representatives plus the
legacy first entry, then finalizes. CLI instead uses `Snapshot(options)` immediately after
admission: it checks exact admitted bytes, input, authority and deadline without redundant
verification, then atomically commits immutable generation files. Later snapshots never
overwrite files referenced by earlier manifests. Rejection, timeout or unavailable verification
never clears accepted slots. Only final `Publish` makes the archive immutable. `AssemblySolver::CollectVerified`
preserves the original first-feasible route, then tries at most three ordinary finite-output
alternatives: compact serial piston/track assembly, a compatible monatomic shared
workstation, and increased feed clearance. Capabilities filter unsupported families;
serialized structures (ignoring names and predicted metrics) are deduplicated before
admission. There are at most 12 submissions and default 3 accepted candidates, so
later variants may not run. Retiming, structure-neighborhood and critical-path row
families share the same three construction attempts (including failed construction),
not additive accepted quotas; the earlier baseline variants retain priority. Rejected
admissions consume submission quota, duplicates do not. Non-ordinary output modes
stop their fallback chain after the first verified candidate, preserving time for
archive publication and the existing local optimizer. These are bounded constructors,
not local optimization.
It may throw on exhaustion after admission: callers retain the archive, not an
unverified return value. Explicit backends remain explicit; no OpusSolver fallback.
See the [step 3 evidence and limits](result/solver-portfolio-step3/report.md).

Assembly then snapshots the three verified objective slots (at most three distinct
parents) for eight deterministic local trials each: compact transport (retype
non-extending pistons, remove arms that never grab, and trim rails to the cells
their arms visit), close the largest global empty interval, remove instruction-free
arms, rotate the complete layout by ±60°, combine tape halving with idle-arm
removal, and trim either track endpoint. `OPUSOPTIMA_COMPACT_TRANSPORT=off`
restores the legacy first trial (halve tape indices) and the legacy queue order
exactly; unset and `on` both select the default queue with compact transport.
When applicable, local-v4 instead overlaps terminal empty-hand arm1 returns while preserving
per-arm rotation order and sharing an earlier reset; otherwise it retains rotate-300.
After both existing return strategies decline, v4 prepositions empty arm1 rotations only on explicit tapes with common O markers. Existing strategy priority and fixed parents are preserved; every candidate requires native admission.
`OPUSOPTIMA_SERIAL_RETURN=1` restores the old v4 for ablation. This is not fragment retention,
co-product construction or a general pipeline; see the [round-one evidence and limitations](result/solver-general-capabilities-round1/report.md).
These are hypotheses, not equivalence rewrites: every distinct trial passes the
full gate, including permissions. Canonical bytes deduplicate parents/no-ops/trials;
children do not refill the queue (at most 24 gates). Independent strict objective
updates and stable ties preserve the first `-o` solution. Trace files record parent
hash/metrics, canonical candidate hash, variant and real verification result.
Before the first feasible solution the 15s allowance is unchanged; afterwards the
structural deadline becomes 13s, local admission ends at 15s and publication
retains the original 20s deadline. In-flight verification shares the 15s admission deadline, so the local
2s allocation is a scheduling target, not a guaranteed minimum. Explicit backends
are unchanged. See [step 4 measurements and limitations](result/solver-local-optimization-step4/report.md).

CLI uses the original 20-second deadline: construction/admission stop at 15s,
leaving 5s for publication. First admission immediately publishes all three slots
(duplicates allowed); accepted improvements publish new snapshots. Interrupted later
work retains the last committed snapshot, never permission to publish unverified bytes. The single-writer manifest rename is the
three-slot visibility boundary; `-o` has its own rename, **not** a multi-file
transaction. Synchronous manifest-rename failure attempts legacy rollback;
crashes, concurrent writers, rollback I/O failure and fsync durability are not
promised. Consumers should read the manifest once, then its bundle files. Bundles
(including failed private archives) are retained; no garbage collector is added.
See [step-2 evidence and limitations](result/solver-objectives-step2/report.md).

The existing local variant-4 slot first compacts consecutive empty-gripper returns
for a single continuous arm/piston tape (net track, extension, rotation; loaded
instructions unchanged). Every child still requires the original full gate;
parents, three objective slots, candidate count and shared deadline are unchanged.
After both existing return strategies decline, v4 can preposition empty arm1
rotations onto earlier globally occupied columns within each arm's idle interval.
It preserves per-arm instruction order and actions, requires at least two active
explicit arm1 tapes ending with empty grippers and a shared terminal `O` no-op
marker. Eligible actions are `GgRrPpAaO`; only empty `R/r` rotations are moved.
Reset/repeat expansion, other instructions and unsupported active arm types are
rejected. A trial is produced only
when prepositioning vacates a global column; global empty columns are then closed,
including retiming explicit no-op period markers. Work scales with serialized
instruction count, without scanning tape gaps. This is a speculative timing trial:
empty-arm motion can collide, so admission still requires the whole-machine native
gate and repeated output completion. Existing return scheduling has priority.
Within the existing v5 slot, useful return compaction followed by empty-rotation
prepositioning is tried directly on the original parent. Both must apply, and
prepositioning must vacate an occupied global column; otherwise v5 retains its
exact timing-division/idle-arm-removal trial. The first component shares v4's
return policy and excludes its preposition/rotation fallbacks. Serial-return mode
suppresses the composition. Only the final composite reaches native admission;
its trace label is `compact-returns-and-preposition-empty-rotations`, while legacy
v5 retains `timing-divide-2-and-remove-idle-arms`. Parent snapshots, variant IDs,
priorities and gate limits remain unchanged.
`OPUSOPTIMA_EMPTY_RETURN=off` disables only single-arm compaction, retaining the
parallel-return/preposition/rotation fallback. `OPUSOPTIMA_SERIAL_RETURN=1` retains its prior
return-ablation behavior. `local-trials.jsonl` records reached/deduplicated trials
as well as submitted trials and gate verdicts; assembly logs distinguish branch
reach, permission skip, construction attempt, unsupported input and acceptance.

An unset `OPUSOPTIMA_TRACK_ENVELOPE` or the exact value `on` enables local-v6
visited-envelope trimming in place of head deletion when every track is a disjoint,
nonwrapping, unrotated path and every arm is an explicit arm1/piston tape that returns to its
initial rail cell. Sparse timestamps and shared tracks are supported: the retained
interval includes every arm's initial cell and all intermediate rail positions.
Reset/repeat expansion, unsupported active objects, malformed paths and newly
wrapping retained intervals fall back to the original v6. Coordinates, instructions,
periods and unused tracks remain unchanged. Every candidate still requires the full
native gate. This bounded transform is enabled by default with the same conservative
guard; ineligible candidates retain the original v6. It adds no parents, variants,
submissions or additional time budget. The exact value `off` and every unrecognized value disable the
transform and preserve legacy v6. `OPUSOPTIMA_USED_RAIL` retains its existing v7 behavior.

The exact value `OPUSOPTIMA_IDLE_TRACK_ENVELOPE=on` opts v6 into composing the
existing v2 idle-arm removal with envelope trimming on the same fixed parent.
Both the original parent envelope and the envelope after idle removal must trim
successfully. Otherwise v6 returns its prior candidate byte for byte. The track
envelope switch takes precedence; unset, `off`, and unrecognized composition
values preserve existing behavior. Empty-arm removal retains v2's first-branch
precedence and passive-tape period guards. Active stationary arms remain.
Tracks left unused by idle removal remain unchanged, which can trade prior area
savings for cost savings. The single combined candidate uses the normal full
native gate, with no additional parents, variants, submissions, or budget.

`OPUSOPTIMA_SHARED_BONDER=on` enables an experimental gate-only local v3
trial: share an exact duplicate ordinary-bonder edge, otherwise probe removal of
the first ordinary bonder. It replaces rotation, not candidate budget. Unset,
`off`, and other values preserve rotation. Round9 found no real-winner cost gain;
this backend is not enabled by default.

### Experimental finite batch-consumer routing

`--experimental-shared-batch-consumer` is **OFF by default** and requires
`--experimental-shared-assembly` with the assembly solver. In the closed
two-input mixed ordinary/Animismus domain, it offers one additional fully
legacy-verified candidate: deliver the final source precursor straight to the
first reaction instead of storing and retrieving it. It preserves six copies
per output, real material identities and existing first/three-objective parents;
unsupported allocations or failed gates retain the parents. It does not assume
independent arms can run without collisions. For the measured baseline also use
`--experimental-shared-scheduling --experimental-shared-buffer-relay` on BOTH
sides. Results and limits: `result/solver-mixed-batch-consumer/report.md`.

`OPUSOPTIMA_COMPACT_SPARE=on` enables a **default-OFF** construction candidate
for finite LifeDeath chemistry with singleton feeds and an unsupplied Animismus
product. Molecular-extraction inputs bypass this additional construction, keeping
baseline candidate opportunities within the unchanged deadline. After the original
first result, it compacts
Animismus spare-product evacuation storage from three-cell to two-cell spacing
and sizes each rail from its own scheduled demand. It shares the existing
three-construction quota and legacy gate; it does not reuse coproducts across
outputs or change chemistry totals. It replaces the first shared-assembly
candidate when both experiments are enabled. Measured benefits and limitations:
`result/solver-finite-feed-compaction/report.md`. No default-on promotion is implied.

A separate **default-ON bounded candidate**, disabled explicitly with
`OPUSOPTIMA_STATIC_EMBEDDING=off`, embeds
bounded ordinary product graphs directly from singleton elemental/Salt supplies,
with required local calcification and normal bonders. It supports at most two
products of eight atoms each, radius three, and twelve edges, not arbitrary board
search. Unsupported/deadline exhaustion is not an infeasibility proof. It runs
only after a verified parent, shares the existing three-construction quota and
full original-input legacy gate, and does not replace the exact default matcher.
Default admission and fresh three-way measurements: `result/solver-static-embedding-default/report.md`.
Historical family evidence and limits: `result/solver-static-embedding-family/checkpoint.md`.

The same static candidate also supports one-step metal projection from genuine
singleton lower-metal and Mercury feeds with projection permission (bit 14).
Direct target feeds remain preferred, followed by existing purification tiles
when permitted. Projection places the lower-metal input at its product cell and
reserves a separate adjacent Mercury reagent cell outside all outputs and other
reaction tiles. It composes with direct feeds, ordinary output bonds and stable
Salt calcified from genuine cardinal singleton feeds with permission 12. Native
inputs regenerate before glyphs in each half-cycle; emitted calcifiers and metal
glyphs precede all ordinary bonders, and outputs consume after glyphs. Calcification
cannot further convert Salt, so every projected product finishes before another
Mercury regeneration. Purification may instead finish later while Salt waits.
Wheels, colored bonds and purification mixed with projection remain unsupported:
native projection can upgrade even bonded metal while a delayed neighbor finishes.
Private triangle placement can still reject an enclosed purification target.
The shared 576 placement checks, deadline, portfolio limits, original output goals,
and native verification gate are unchanged.

The singleton purification chain is enabled when
`OPUSOPTIMA_SINGLETON_PURIFICATION_CHAIN` is unset or exactly `on`; other values
disable it. It supports ordinary v3, nonproduction, multiplier-one puzzles with
one singleton metal output, one to eight genuine singleton metal inputs, and a
nearest lower feed two to five metal levels below the target for direct compilation.
Default portfolio admission requires a nearest lower feed four or five levels below
the target and no genuine singleton feed of the target metal. At these depths the
chain cost `35*d + 50` is below the static cascade cost `10*d*(d + 1)`; depths two
and three remain supported by the direct compiler. Arm, piston, track,
purification, rotation, grab and noop permissions are required. One piston transports
atoms below three-cell-spaced glyphs, and the final glyph feeds the output directly.
The recursive period consumes all intermediate stock and restores the empty piston.
It precharges a remaining construction slot after the existing static candidates,
shares the unchanged three-construction, twelve-submission, default three-accepted
and outer-deadline limits, and requires actual native authority at the exact
`portfolio-singleton-purification-chain` stage. The original first machine and fast
static cascade remain separate archive candidates. On the original six-output
singleton Lead-to-Gold puzzle, native cost/cycles/area are 225/5554/50 for this chain
and 300/69/21 for the static cascade. Depth-one static compilation is unchanged.

The radial candidate is enabled when `OPUSOPTIMA_RADIAL_WHEEL` is unset or
exactly `on`. Explicit `off` and all unrecognized values (including an empty
string or different case) disable it for controlled comparisons. Admission is
limited to ordinary v3, nonproduction, multiplier-one puzzles with one three-to-seven-atom
Salt-centered radial product, two to six Salt/cardinal leaves, one to eight genuine
singleton Salt feeds, ordinary exact hub edges, unique positions and the required
arm, bonder, duplication, rotation, grab, noop, pivot and wheel permissions. One rotating wheel, one duplicator, one bonder and two arms supply
and assemble successive leaves. Conversion occurs away from the input and the
held product pivots about its center. The complete one-product period restores
all arm and wheel poses. This candidate runs after the existing static and rigid
candidates, precharges an available construction slot, and shares the unchanged
three-construction, twelve-submission, accepted-result and deadline limits. It
requires native admission and verification on the original puzzle. The original
first candidate and separate cost/cycles/area archive slots remain available.

The mutually exclusive metal variant of that same budgeted radial slot has
limited default admission. Unset or `OPUSOPTIMA_RADIAL_PROJECTION=on` enables it;
`off`, empty and unrecognized values disable it. It accepts ordinary nonproduction v3, multiplier-one puzzles
with genuine singleton inputs, one genuinely supplied hub with two to six ordinary
radial metal leaves, a common supplied lower-or-equal metal and Mercury, and an
optional singleton metal output. Source coordinates and actual output indices are
preserved; unused ordinary inputs are allowed. Life atoms, colored or nonradial
bonds and other output forms are refused. Arm, bonder, grab, rotation, pivot,
wait (bit 25) and projection permissions are required. One projection glyph receives individually
transported Mercury doses while the metal remains held, with at most five doses
per atom. The singleton outlet is separate from projection and radial bonding.
The closed period restores all arms and the product pose without residual doses
or intermediate stock. This candidate requires its own original-puzzle native
gate; it does not change first-result priority, archive metric selection, caps or
deadlines. Hardware reuse can substantially increase cycles. This limited rollout
uses the validated seen corpus; it does not prove all unseen opportunities are free of regressions.

A **default-enabled, exact-domain** additional assembly candidate runs after the
first verified parent; `OPUSOPTIMA_STATIC_REACTION=off` explicitly disables it for
ablation (other experimental switches remain default OFF). It exactly matches two complementary
Vitae/Mors products with two opposing Water neighbors and an optional terminal
Salt at the supported handed branch, up to independent rotations/translations.
Singleton Water (optionally Salt) feeds are reusable; required arm, bonder,
Animismus and conditional calcification permissions are checked. These legacy
layouts retain their serialized bytes. If the exact matcher rejects, the same slot
can embed two connected ordinary output graphs with exactly one Vitae and one Mors,
respectively, and genuine singleton direct feeds for every other atom plus Salt
for the reaction. All inputs must be genuine singletons (at most eight); each
output has at most 16 atoms, 36 ordinary adjacent edges and radius three from its
life atom. The two life atoms stay on the real Animismus ports `(0,1)` and `(1,-1)`;
at most 36 rotation pairs are checked. Output cells must be disjoint and avoid
both reagent cells. Only requested bonds are emitted; no arms, disposal or other
derived chemistry are added. Ordinary arm, bonder, Animismus and output permissions
are required. The caller deadline bounds the fallback, and its distinct publication
stage requires actual LegacyGate authority for the candidate itself. The dispatcher
returns typed compiler-family provenance with each successful machine; the portfolio
selects that stage from the family enum, independently of puzzle or solution names.
Existing machine-only compiler APIs retain their signatures and serialized bytes. Legacy
calcified Water support remains confined to its existing matcher. This is not an
arbitrary-grid compiler or puzzle-name/hash dispatch. The same static reaction
slot also supports ordinary v3, nonproduction, multiplier-one puzzles with one
singleton metal product, one to eight genuine singleton metal inputs and a
supplied metal one to five purification steps below the target. Purification
permission is required. The nearest supplied lower metal is selected, retaining
the first input index on ties. A depth `n` cascade uses `n+1` dispensers of that
genuine input and `n(n+1)/2` glyphs (at most 15). Each glyph consumes adjacent
cells `(q,r)` and `(q+1,r)` and produces at `(q,r+1)`; shared intermediate cells
feed the next row, with downstream/rightmost glyphs placed first. The output
consumes the root at `(0,n)`. The station needs no arms or instructions.
Depth one retains the previous serialized layout. Molecular feeds/products and
nonmetal inputs are excluded. Locked-native completion of the original six-output
goal validates the bounded cascade; publication still verifies every candidate. Its publication stage requires actual LegacyGate
authority independently of the parent's authority. Existing construction,
submission and deadline limits and the original-puzzle native gate apply. The
first solution is unchanged; verified three-objective snapshots can improve.
After an actual Passed LegacyGate result with typed `Purification` provenance,
`AcceptedSingletonPurification` suppresses a redundant portfolio embedding only
for one singleton metal output with genuine singleton metal feeds and no direct
target feed. Both compilers select the nearest lower metal (first input on ties)
and emit the same ordered triangle; embedding merely translates it and adds an
idle arm. The suppression logs `accepted-singleton-purification`, charges no
construction, and does not set `staticEmbeddingAttempted`. The existing compact
chain keeps its order and remaining slot under unchanged construction, submission,
accepted-count and deadline caps. A genuine direct target feed remains eligible
for embedding's direct candidate. Unsupported, expired, rejected, nonnative,
disabled or unattempted static reactions cannot suppress embedding; multiatom,
multioutput and mixed derivations retain their existing paths. Pre-first fallback
and machine-only compiler callers remain unchanged.
Other mixed backends remain available. Evidence and bounded scope:
`result/solver-static-reaction-integration/report.md`.

### Bounded Calcify–Animismus experiment (default OFF)

Set both `OPUSOPTIMA_CALCIFY_ANIMISMUS=1` and `OPUSOPTIMA_SERIAL_ANIMISMUS=1`
to allow isolated cardinal feed atoms to become distinct Salt reservations through
physical Calcify operations, followed by Animismus and complete use of both fresh
Vitae/Mors products. No discard or surplus exception is provided; `RequireComplete`
is unchanged. Publication requires actual LegacyGate authority, including
legacy-primary Shadow; Independent is denied before archive submission for this
chain only. No accepted parent is required. Search/layout/production budgets are
unchanged. Native fixture/original-candidate success is not incremental CLI coverage;
see `result/solver-calcify-animismus-integration/report.md` for bounded results.

The broad closed ordinary source-cut experiment requires exact
`OPUSOPTIMA_PRODUCT_PORT_ACCESS=1`; its Calcify–Animismus route requires all three
flags above. All three experimental flags retain their OFF defaults. A guarded
closed cardinal-to-life composition is admitted by default as described below.
The explicit lowering admits only nonproduction singleton/dimer ordinary sources
and one ordinary molecular output, with bonder permission and unbonder permission
for dimers. It acquires complete original sources, emits real cuts before endpoint
use and reconstructs every requested target bond without residue. Retained products,
wheel bindings and colored bonds remain excluded. Its publication stage is
`shared-plan-ordinary-port-access`; optional children use
`shared-plan-ordinary-port-access:parent=<hash>`. Each candidate requires its own
actual LegacyGate report before acceptance, including legacy-primary Shadow;
Independent mode and unverified callbacks cannot authorize this stage. The existing
three-second shared material/physical/native deadline, construction allowance and
12-submission cap remain unchanged. The original lowering entry points preserve
compatible edges.

The default admission guard requires version 3, nonproduction, multiplier 1,
1–4 genuine ordinary singleton/dimer cardinal-only inputs with at least one dimer,
and one connected ordinary output of 2–16 atoms. The output must contain positive
equal Vitae/Mors counts; all remaining atoms must be Salt or cardinals present in
real inputs. The existing graph/identity validator and arm, bonder, unbonder,
calcification, Animismus, rotation, grab and noop permissions are required. Salt,
life, metals, Quintessence, colored/repeating molecules and other chemistry are
excluded as sources. No retained fragments, wheel/prism, overlapping outputs or
finite terminal exception is added.

`SupplyMaterialLoweringPolicy::ClosedCardinalLife` propagates explicitly through
`SearchMaterialExecutionWithPolicy`, `SearchJointSupplyInventoryWithPolicy`,
`BuildJointSupplyExecutionIRWithPolicy` and
`CompileAcceptedSerialArmExecutionWithPolicy`, including parent and child trials.
Each boundary validates the domain. Existing entry points and limits layouts keep
their previous environment-controlled behavior. No process environment is changed.
Earlier compiler families retain their order and opportunities; only the existing
shared material pass selects the new composition. Within this source domain,
legacy inventory Animismus with the precursor experiment disabled has no Salt feed,
and target-directed calcification
delivers Salt to its target rather than seeding life synthesis. Thus the selected
composition cannot replace a legacy material history. This is not a claim of global
unsolvability: earlier LifeDeath and other compilers retain their opportunities.

`OPUSOPTIMA_CLOSED_CARDINAL_LIFE` unset or exactly `on` enables only this guard;
`off` and every other value disable the default admission. This ablation does not
disable explicit legacy experiments: all three old flags set to `1` still work.
Outside the guard, default material lowering and native admission remain unchanged.
The unmodified GEN873 source qualifies for the guarded default; its original
six-product goal is checked through the normal native gate. This bounded admission
change makes no new aggregate coverage claim.

The rigid carrier is attempted by default when `OPUSOPTIMA_RIGID_CARRIER` is unset; only exact `off` disables it. As with static embedding, every other string (including `on`, `unknown`, empty, and `OFF`) allows an attempt. Admission still requires a native worker and the first Passed parent to have LegacyGate authority; shared construction/submission/accepted limits (3/12/3), deadline, first and objective-slot protection are unchanged. Its finite placement preflight shares the compiler header/graph guards. Proven unsupported inputs (including all-singleton sources) skip without consuming a portfolio construction; Unknown still uses the original construction budget and native gate, so it can consume time or displace a downstream opportunity. Missing bonder/calcification permissions alone are not sufficient to reject already-bonded molecules.

The rigid-carrier preflight shares the compiler’s six-rotation/translation tile matcher for the existing one/two-port whole-source path (at most 1,152 anchors). Without a whole-source placement, a conservative size/type check may admit a static partition as Unknown; it performs no partition placement or cover search. Both paths use the caller deadline.

Beyond the legacy eight-input/four-output carrier header, the same charged slot admits only static complete-input copies, bounded to sixteen original inputs and eight original outputs. Every output must exactly match one entire genuine input under rotation and translation, including types and all ordinary bonds; at least one output must be molecular. Unused inputs are permitted, while splitting, combining, chemistry and leftovers are excluded. Each original output index occurs once at a separate normalized region, retaining the genuine source index. The reused matcher charges every rotation/anchor attempt to a shared 20,000-node cap (at most 12,288 attempts under these limits) and the caller deadline. Preflight performs only bounded necessary size/type/edge-count checks. A failed copy search returns typed Unsupported without entering the general cover or partition search. `portfolio-static-input-copy` requires actual LegacyGate authority, including legacy-primary Shadow; Independent mode and native-disabled builds cannot admit it. Existing eligible carrier paths, first retention and shared portfolio budgets are unchanged.

The same charged carrier slot also supports static whole-molecule partitions across two to four original output ports. It keeps the existing v3, nonproduction, multiplier-one, eight-input, sixteen-atoms-per-graph and radius-eight limits. Each selected genuine bonded source is completely partitioned into disjoint, exactly typed rigid output shapes, with at least one batch supplying multiple ports. Every requested edge must be inherited; permitted real unbonders remove every other source edge. No chemistry, new bonds, retained atoms or disposal is introduced. Source copies occupy separate regions, and all assigned pieces must clear before the entire source can regenerate. The original six-product goal remains unchanged at every port. The existing one/two-port whole-source cover is retained whenever each port has a legal tile; partition construction is selected only for three/four ports or a missing whole-source tile. Partition recursion and every rotation/anchor attempt share a deterministic 20,000-node cap and the caller deadline; no second cover search follows exhaustion. `portfolio-static-molecule-partition` requires actual LegacyGate authority before publication, including legacy-primary Shadow. Independent mode and native-disabled builds cannot admit it. Construction/submission/accepted limits, first retention and objective-slot protection remain unchanged.

`CompileAcceptedWholeFeedDocking` is an explicit physical compiler for a conserved
ordinary connected feed and ordinary singleton receivers. Its supplied ExecutionIR
must acquire one complete batch, cut every original edge before scalar output, and
pass RequireComplete with one output per port and no remaining stock or disposal.
It moves the intact feed to a disjoint service station, binds original identities
to actual unbonders, parks every cut sibling, and retrieves each exact scalar
request with a receiving arm. Independent singleton requests execute exposed
siblings before enclosed identities when physical access requires that order.
Occupied source and service cells, native-safe swept clearance, custody, arm resets
and the next regenerating feed remain protected. Eighteen service layouts,
radius-three fixed arms, twelve source atoms, 512 material steps, at most three
constructions and the caller's shared deadline bound this path. The optional
construction callback charges an enclosing portfolio before each machine is built.

`OPUSOPTIMA_WHOLE_FEED_DOCKING=1` enables the verified AssemblySolver stage
`whole-feed-singleton-receivers`; the default remains disabled. Its typed guard
requires v3, nonproduction, multiplier one, one connected ordinary source of
2–12 atoms, the required arm/unbonder/output permissions, and one singleton output
per original atom with exactly matching type multiplicities. It preserves the
original output ports and constructs a complete identity-bound material period.
Each construction consumes the existing shared three-construction quota; the
existing twelve-submission and accepted-candidate limits, first-result retention,
archive slots and three-second shared deadline remain in force. The actual
LegacyGate authority is required, including in legacy-primary Shadow. Independent
authority cannot admit this stage. Rejection continues through the old fallback.
Material search policies and their 4096-state/32-history limits are unchanged.

Native diagnostics cover transformed seven-atom enclosed source feeds, with all
nine original bonds, through repeated complete singleton-output goals. Those
puzzles are derived diagnostics. The opt-in route also has native evidence on two
unchanged ordinary originals, GEN879 and NONLINEAR3 TRI 4. Multi-edge colored
receivers and the original P106 remain outside this capability; no aggregate
original-corpus coverage improvement is claimed.
