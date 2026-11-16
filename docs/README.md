# Documentation index

Start with the project layout if you are new to the tree, then the architecture
note for how a puzzle becomes a machine.

| document | what it covers |
|---|---|
| [PROJECT_LAYOUT.md](PROJECT_LAYOUT.md) | where files live, artifact convention, build/test/measure workflows |
| [ARCHITECTURE.md](ARCHITECTURE.md) | pipeline: parse → plan → emit → judge, and the module responsibilities |
| [IndependentVerifier.md](IndependentVerifier.md) | the pinned `libverify` native authority and its worker process |
| [ReviewAndNextSteps.md](ReviewAndNextSteps.md) | open findings and the agreed next steps |

## Technical reports

Current reports kept with the code. Intermediate search logs and superseded
capability inventories were removed; their evidence remains under
`../artifacts/result/`.

| document | what it covers |
|---|---|
| [analysis/UPSTREAM_STANDARD_PORT_20261115.md](analysis/UPSTREAM_STANDARD_PORT_20261115.md) | the upstream Standard port, its module map and the first full comparison |
| [analysis/UPSTREAM_STANDARD_OPTIMIZE_20261116.md](analysis/UPSTREAM_STANDARD_OPTIMIZE_20261116.md) | recipe/build-order/subset enumeration, Hex3 and three-atom shapes, final 146-puzzle audit |
| [analysis/LIFEDEATH_STORAGE_PROOF_20261115.md](analysis/LIFEDEATH_STORAGE_PROOF_20261115.md) | the dispersion storage rail-trimming proof and its native measurements |
| [analysis/WASM_DEPLOYMENT_ASSESSMENT_20261116.md](analysis/WASM_DEPLOYMENT_ASSESSMENT_20261116.md) | WebAssembly/GitHub Pages feasibility, missing layers and phased plan |

## Measurement reports

Solver reports are **not** part of this repository: they live under
`../artifacts/result/<topic>-<date>/REPORT.md`, together with the machines,
`rows.json`/`bench.json` raw data and the commands that produced them. Scripts
that reproduce them are versioned here in [`../tools/`](../tools/README.md).
