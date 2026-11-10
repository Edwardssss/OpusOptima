# Documentation index

Start with the project layout if you are new to the tree, then the architecture
note for how a puzzle becomes a machine.

| document | what it covers |
|---|---|
| [PROJECT_LAYOUT.md](PROJECT_LAYOUT.md) | where files live, artifact convention, build/test/measure workflows |
| [ARCHITECTURE.md](ARCHITECTURE.md) | pipeline: parse → plan → emit → judge, and the module responsibilities |
| [IndependentVerifier.md](IndependentVerifier.md) | the pinned `libverify` native authority and its worker process |
| [ReviewAndNextSteps.md](ReviewAndNextSteps.md) | open findings and the agreed next steps |

## Analysis archive

Deep dives that informed the current capability roadmap. They are snapshots: the
conclusions still hold, the measurements are dated.

| document | what it covers |
|---|---|
| [analysis/OPUSSOLVER_GAP_ANALYSIS_20261111.md](analysis/OPUSSOLVER_GAP_ANALYSIS_20261111.md) | 2026-11-11 metric comparison by class/metric, structural attribution (serial piston/track vs arm1 network), gap buckets and the targeted capability plan |
| [analysis/OPUSSOLVER_ARCHITECTURE_GAP_20261110.md](analysis/OPUSSOLVER_ARCHITECTURE_GAP_20261110.md) | structural comparison with the upstream OpusSolver (scheduling, geometry, arm mix) and the C1–C5 capability gap |
| [analysis/OPUSSOLVER_CAPABILITY_GAP.md](analysis/OPUSSOLVER_CAPABILITY_GAP.md) | earlier capability inventory (superseded by the dated analysis) |
| [analysis/OPUSSOLVER_CAPABILITY_GAP_PHASE1.md](analysis/OPUSSOLVER_CAPABILITY_GAP_PHASE1.md) | phase 1 execution notes for that inventory |

## Measurement reports

Solver reports are **not** part of this repository: they live under
`../artifacts/result/<topic>-<date>/REPORT.md`, together with the machines,
`rows.json`/`bench.json` raw data and the commands that produced them. Scripts
that reproduce them are versioned here in [`../tools/`](../tools/README.md).
