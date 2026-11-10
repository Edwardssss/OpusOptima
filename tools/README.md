# Measurement tools

Small Python helpers that drive the two binaries and read the artefact tree.
They are the reproducible half of every report under `../artifacts/result/`.

All tools are plain scripts — no third party packages, Python 3.9+.

```sh
cd <repo>
python3 tools/bench_corpus.py                    # full corpus A/B, intake on vs off
python3 tools/bench_corpus.py --only 18644c2c    # one puzzle
python3 tools/probe_layout.py --count 12         # emitter-level intake vs feeder
python3 tools/probe_knob.py 18644c2c OPUSOPTIMA_ROW_INTAKE_GAP 0,1,2,4,8
```

## What each tool answers

| tool | question |
|---|---|
| `bench_corpus.py` | does a configuration change win objective slots on the whole corpus, and does it lose any? |
| `probe_layout.py` | is a machine rejected by the **emitter** (geometry/timing) or only by candidate ordering? |
| `probe_knob.py` | is a rejection geometric (error cell fixed) or temporal (error cycle follows the knob)? |

`opsol.py` is the shared library: repository/artefact path resolution, puzzle and
solution readers, and wrappers for `build/OpusOptima` and `build/OpusOptimaTests`.

## Conventions

* **Native authority only.** Every judgement comes from
  `./build/OpusOptima verify --authority legacy-gate --json`; nothing here
  re-implements the gate.
* **Artefacts never enter git.** Solvers write under `result/` (a symlink to
  `../artifacts/result`); the tools default to `result/bench` and `result/probe`.
  Override with `OPUSOPTIMA_RESULT`, `OPUSOPTIMA_CORPUS`, `OPUSOPTIMA_ORACLE`.
* **Measure, then believe.** A single `REGRESS` line is not a regression: parallel
  runs contend for the 20 s solver budget and flip whole puzzles between two
  outcomes. Re-run the puzzle in isolation (both modes, 2-3 times) before acting.
* **Objective slots are independent.** `cost`, `cycles` and `area` are compared
  one by one against the OpusSolver oracle (`oracle.json`), and a machine only
  replaces a slot when it is strictly better.
