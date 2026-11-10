#!/usr/bin/env python3
"""Sweep one emitter knob on one puzzle and report where the native gate lands.

Used to classify a rejection: if the error cell stays put while the error cycle
follows the knob, the defect is geometric rather than temporal (see
``result/solver-c2c3c5-20261110/REPORT.md`` §2).

Examples
--------
    python3 tools/probe_knob.py 18644c2c OPUSOPTIMA_ROW_INTAKE_GAP 0,1,2,4,8
    python3 tools/probe_knob.py 18644c2c OPUSOPTIMA_COMPACT_LAYOUT_GAP 0,1,2,3
"""

from __future__ import annotations

import argparse
from pathlib import Path

import opsol


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("sha", help="puzzle sha prefix")
    parser.add_argument("knob", help="environment variable to sweep")
    parser.add_argument("values", help="comma separated values")
    parser.add_argument("--out", default=str(opsol.RESULT_ROOT / "probe"),
                        help="artefact directory for the compiled machines")
    args = parser.parse_args()

    puzzle = opsol.puzzle_for(args.sha)
    out_dir = Path(args.out)
    out_dir.mkdir(parents=True, exist_ok=True)

    values = [v.strip() for v in args.values.split(",") if v.strip()]
    cases = []
    targets = []
    for value in values:
        machine = out_dir / f"{args.sha[:8]}-{args.knob.rsplit('_', 1)[-1]}-{value}.sol"
        targets.append((value, machine))
        cases.append((1, puzzle, machine))

    # One probe process per value: the knob is read when the emitter compiles.
    for value, machine in targets:
        opsol.run_layout_probe([case for case, (v, _) in zip(cases, targets) if v == value],
                               env={args.knob: value})

    print(f"{'value':>8}  {'status':10s} {'errorCycle':>10}  {'errorLocation':14}  metrics")
    for value, machine in targets:
        if not machine.exists():
            print(f"{value:>8}  {'no machine':10s}")
            continue
        report = opsol.verify(puzzle, machine)
        metrics = report.get("metrics") or {}
        summary = "" if not metrics else (f'cost={metrics.get("cost")} '
                                          f'cycles={metrics.get("cycles")} '
                                          f'area={metrics.get("area")}')
        print(f'{value:>8}  {report.get("status"):10s} {report.get("errorCycle"):>10}  '
              f'{str(report.get("errorLocation")):14}  {summary}')
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
