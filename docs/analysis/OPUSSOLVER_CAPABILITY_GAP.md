# OpusSolver capability gap and next-step ROI

Evidence date: 2026-11-09. Native authority: `legacy-gate`, engine
`omsim-a88d12d8+production-replay-v1`. Upstream source:
`gtw123/OpusSolver@fdedd3f0`.

## 1. Observed gap

| puzzle | OpusSolver (cost/cycles/area) | OpusOptima unified (cost/cycles/area) |
|---|---|---|
| P088 | 755 / **1205** / 230 | 810 / **5082** / 201 |
| P022 | 700 / **379** / 130 | 615 / **2340** / 113 |

We are competitive on cost/area (P088 area wins, P022 cost+area win), but
cycles are 2.6x–6.2x the reference. The gap is not uniform: it is concentrated
on the row assembly family.

## 2. Critical path: the shared feeder

Parsing the unified row machine and reading each object's last instruction tick
shows that the single feed piston is the last busy arm:

| puzzle | feed arm ticks | feed instructions | machine time | cycles = (time+1)*6 |
|---|---:|---:|---:|---:|
| P088 | **854** | **849** | 846 | 5082 |
| P022 | **391** | **351** | 389 | 2340 |

The feed arm is not merely waiting; it is the busiest object by a wide margin.
P088's feed tape is:

```
R×3 Ax12 r×3 Ax1 Ex1 G ex1 a Rx3 ax12 r×3 Ex2 g ex2  ... per atom
```

Per atom it performs two 3-step rotations (6 rotations), a round trip to the
source, extension/retraction, grab and deposit. For P088 this is 204 rotations
and 486 travel actions out of 849 instructions. Every other arm has 95–235
instructions and idles behind the feeder.

The inputs are small linear chains, so the upstream `LinearDisassembler` pattern
applies directly:

| puzzle | input | atoms | bonds | turns |
|---|---|---:|---:|---:|
| P088 | 0 | 3 | 2 | 0 |
| P088 | 1 | 2 | 1 | 0 |
| P022 | 0 | 7 | 6 | 0 |
| P022 | 1 | 1 | 0 | 0 |

Upstream `LinearDisassembler` uses two local arms per input: a grab piston that
walks the chain on a short track (`Grab, Extend`, then `MovePositive` per atom)
and an output arm (`GrabResetAction(RotateCounterclockwise)`) that rotates and
hands each atom to the shared line. Per-input arms run independently; only the
shared handoff is ordered.

Conclusion: the dominant missing capability is **parallel, per-input
disassembly/rotation**, not upper-arm bonding, lower-arm placement, or compact
geometry.

## 2b. Reference machine anatomy (new, generated oracle)

The upstream solver was built locally in `/tmp` (`dotnet 8`) and P088 was
regenerated with `--solver Standard --optimize`. The generated solution passes
our `legacy-gate` at exactly **755 / 1205 / 230** and is stored as
`result/opussolver-reference-20261110/P088-Standard.solution`.

Its object inventory is qualitatively different from ours:

| kind | OpusSolver P088 | OpusOptima unified row |
|---|---:|---:|
| arm1 (price 20) | **12** | 0 |
| piston (price 40) | 6 | 11 |
| bonder | 4 | 3 |
| track | 4 | 4 |
| unbonder | 3 | 2 |

The four bonders sit at q=4, 9, 17 and 23: the reference is a **serial pipeline
of stations**, not one wide assembly area. Atoms are extracted by local input
arms, rotated and handed between cheap `arm1` transfer arms, and bonded at
successive stations while later atoms are still being extracted. That overlap
is the source of the 4x cycle advantage; the feeder is only one stage of it.

Our row machine is a **single wide comb**: one shared feeder extracts every
atom, the whole row is placed, then the bond program runs. Even a perfect feeder
would still leave the serial row bond program on the critical path.

The porting target is therefore the upstream component set:
`Input/SimpleInputArea`, `Input/LinearDisassembler`, `Input/AtomConveyor`,
`Output/UniversalAssembler`, `Output/Universal/AssemblyArea` and
`Output/BondProgrammer`, not only a faster feed arm.

A second structural difference is the instruction vocabulary. Upstream emits
`P/p` pivots (`PivotClockwise` / `PivotCounterclockwise`) and `C` repeats in its
local transfer arms, e.g. P088 input arm `GrppXGrppXGrppX` (`Grab, RotateCCW,
PivotCCW, PivotCCW, Reset`). Our row emitter never emits `P/p`; it only moves
along tracks with `A/a` and rotates with `R/r`. Pivots let upstream move and
orient atoms locally without long rail travel, which is exactly what the feeder
round trip is doing.

## 2c. Oracle classification (162 inputs)

All upstream solutions were generated and native-verified
(`result/opussolver-oracle-20261110`). Grouping the best-cycles machine by
object inventory gives:

| class | puzzles | our any-win | our all-three | avg our/reference cycles |
|---|---:|---:|---:|---:|
| `pipeline/cheap-arms` (4+ bonders, arm1>piston) | 30 | 9 | 3 | 30.5x |
| `linear/mono` (<=1 bonder) | 39 | 30 | 25 | 8.2x |
| `universal` (<=3 bonders) | 65 | 12 | 11 | 18.6x |
| `pipeline/heavy` | 7 | 4 | 4 | 13.6x |

The largest and worst gap is the multi-stage cheap-arm pipeline; `universal` is
the largest class and still 18.6x. The upstream composer is `ElementPipeline` +
`SolutionBuilder`, which chains input areas, generators, buffers and the output
assembler into one line. OpusOptima has fixed family emitters instead.

## 3. What upstream does differently

Upstream `Solver/Standard` composes a machine from independent components:

- `Input/SimpleInputArea`: one dedicated disassembler arm per reagent (up to
  four). Each arm stays at its source, rotates the atom locally and hands it to
  the shared line.
- `Input/MonoatomicDisassembler`, `MoleculeDisassembler`: source-local
  extraction and orientation.
- `Output/UniversalAssembler` + `AssemblyArea` + `BondProgrammer`: lower/upper
  arm bank and bond schedule.
- `Output/LinearAssembler`: a single piston + one bonder for height-1 chains.
- `ProgramWriter`: writes absolute instruction tapes and uses
  `AdjustTime(-distance)` / `AdjustTime(-1)` to overlap an arm's travel or reset
  with earlier work.
- `ProductConveyor`: independent output arm and track.

OpusOptima instead has a portfolio of fixed family emitters. The row emitter
uses one shared feeder for every input and a width-wide upper comb. It has
independent clocks for feed/upper/conveyor, but the feed clock is still one
serial arm that alternates source orientation and conveyor orientation for
every atom.

## 4. Capability gaps

1. **Per-input disassembler arms (largest gap).** No source-local, parallel
   extraction/rotation. One feeder serves all inputs serially.
2. **Global overlap scheduler.** We have three ad-hoc clocks
   (`feedTime`/`upperTime`/`convTime`); upstream's `ProgramWriter` can start any
   free arm's travel/reset earlier. Our lower-arm pre-position experiment only
   saved 6 cycles on P022 because the feeder remained the critical path.
3. **Component-level composition/search.** Upstream chooses and places input,
   output and generator components; our emitters are fixed per family.
4. **Assembly-area scheduling.** `UniversalAssembler` interleaves row
   placement/bonding with explicit time adjustments; our row builder places a
   whole row and then runs the bond program.
5. **Multi-objective component search.** Upstream can trade area for cycles by
   placing a different component graph; our cost/area/cycles trade-offs are
   hard-coded per emitter.

## 5. ROI ranking (revised after the reference oracle)

1. **Pipelined assembly area** (highest ROI).
   - Port `Output/UniversalAssembler` + `AssemblyArea` + `BondProgrammer`:
     four serial bonder stations and cheap `arm1` transfer arms instead of one
     wide comb.
   - Directly attacks both the feeder round trips and the serial row bond
     program; this is the only route that can reach the reference 1205 cycles.
   - High effort, but the reference machine above is an exact P088 oracle and
     the upstream source is available.
2. **Per-input disassembler + AtomConveyor** (part of the pipeline, still
   highest-value subcomponent).
   - Local `LinearDisassembler` arms for the 3+2 / 7+1 linear inputs; local
     rotation and handoff.
   - Expected on its own: P088 cycles 5082 → ~2200–2800; P022 2340 → ~1000–
     1300. It removes the feeder bottleneck but leaves the serial bond program.
3. **Global overlap scheduler** (`ProgramWriter::AdjustTime` equivalent).
4. **Bracket/shuttle row-bank rewrite** (area/cost, lower cycle impact). Useful
   for the 4-cell/10-cell area near-misses, but does not fix the pipeline gap.
5. **Local geometry/transform sweeps** (exhausted). Gap/stride/spacing, target
   rotation, tail trim, piston→arm1, idle-arm removal and unbonder removal have
   all been native-tested and rejected or metric-neutral.

## 6. Concrete next step

Implement a **`LinearInputArea`** candidate for narrow multi-input rows, ported
from upstream `Input/LinearDisassembler`:

- per input: one grab piston + short local track along the chain, plus one
  output arm at the handoff;
- extraction actions: `Grab, Extend` on the grab arm; `MovePositive` per atom;
  `GrabResetAction(RotateCounterclockwise)` on the output arm;
- independent clock per input; only the shared handoff/conveyor drop is
  serialized;
- remove the shared serpentine feeder so the new arms do not add net cost/area;
- keep the current single-feeder machine as the fallback candidate;
- native-gate every candidate; target P088/P022 first, then the 19 row-retimed
  cohort.

Success criteria for the first increment:

- P088 cycles < 3000 with cost <= 810 and area <= 201 (preserve the area win);
- P022 cycles < 1500 with cost <= 615 and area <= 113 (preserve both wins);
- no lost wins or status changes on the full 162-input run.

## 7. Rejected experiments (do not repeat)

- single upper shuttle: only one-row targets pass; all row-retimed targets are
  multi-row;
- pre-compile 60° target rotation: only 120/240° pass and re-emit a worse
  machine;
- tail track trim 1–8 cells: motion rejection; rails cover arm bodies;
- piston→arm1: rejected because tapes use `E/e`;
- dropping any bonder/unbonder: motion rejection;
- lower-arm pre-position clock: P022 2340→2334, P088 unchanged;
- forcing the expanded-feed path for narrow inputs: `wide extraction must finish
  on bottom row`;
- gap1: improves OM2020/6e993c03 but worsens 329bfd90/a94b5296 and adds no
  strict win.
