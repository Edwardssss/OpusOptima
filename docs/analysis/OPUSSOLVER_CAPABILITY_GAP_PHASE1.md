## 8. Phase 1 status (2026-11-10): local input intake

Implemented `LinearInputArea` (upstream `LinearDisassembler` port) with contract
tests, and an opt-in local source intake in the scheduled row emitter
(`OPUSOPTIMA_DIRECT_INPUT=on`; unambiguous "off" keeps the shipped machine
byte-for-byte).

**New native constraint (hard):** a piston reaches at most **three** cells.
An intake rail on row 4 (extension 4 to reach row 0) never grabs and the machine
ends in `reference-cycle-limit` with no error location. Row 3 with extensions
3/2/1 for source rows 0/1/2 works.

Measured with `legacy-gate`:

| puzzle / variant | cycles | cost | area | status |
|---|---:|---:|---:|---|
| `just-salt`, scheduled row (off) | 377 | 355 | 53 | passed |
| `just-salt`, local intake (on) | **230** | **290** | **37** | passed |
| P088, scheduled row gap2 (off) | 8516 | 1120 | 225 | passed |
| P088, local intake (on) | 5652 | — | — | **rejected** |

The P088 intake machine is 33 % faster per period (1419 → 941) but a product
atom is still parked at `[12,1]` when the 941-tick period wraps, so the gate
reports `collision during motion phase` at cycle 1798. With extraction no longer
dominating the clock, the narrow row's own product transfer becomes the binding
constraint: the period closes before the last row finishes.

Consequence for the plan: a cheaper extraction stage alone cannot reach the
P088 `< 3000` target. The next increment must either extend the period to the
row's real completion time (no gain), add independent per-arm clocks
(`AdjustTime` equivalent), or port the multi-station assembly area
(`AssemblyArea` + `BondProgrammer`). Report:
`result/solver-local-intake-20261110/REPORT.md`.
