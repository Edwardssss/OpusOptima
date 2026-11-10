#!/usr/bin/env python3
"""Emitter-level probe: compile scheduled-row machines with and without the local
intake, then judge each machine with the native gate.

This bypasses the solver portfolio, so it isolates the emitter: the same puzzle
geometry is compiled twice and only the input stage differs. A machine that the
gate rejects here is rejected by the emitters, not by candidate ordering.

Examples
--------
    python3 tools/probe_layout.py --count 12
    python3 tools/probe_layout.py --count 24 --knob OPUSOPTIMA_COMPACT_LAYOUT_GAP=0
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path

import opsol


def eligible(puzzle: Path) -> bool:
    """Rows the probe can compile: one ordinary compact output, no production."""
    try:
        p = opsol.read_puzzle(puzzle)
    except Exception:
        return False
    if len(p["outputs"]) != 1 or p["multiplier"] != 1 or p["production"]:
        return False
    atoms = p["outputs"][0]["atoms"]
    bonds = p["outputs"][0]["bonds"]
    if not atoms or len(atoms) > 16:
        return False
    if any(bond[0] != 1 for bond in bonds):
        return False
    qs = [q for _, q, _ in atoms]
    rs = [r for _, _, r in atoms]
    return max(qs) - min(qs) < 16 and max(rs) - min(rs) < 16


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--count", type=int, default=12, help="how many puzzles to probe")
    parser.add_argument("--knob", action="append", default=[], metavar="KEY=VALUE",
                        help="extra environment for the compile step")
    parser.add_argument("--out", default=str(opsol.RESULT_ROOT / "probe"),
                        help="artefact directory for the compiled machines")
    args = parser.parse_args()

    extra = {}
    for pair in args.knob:
        key, _, value = pair.partition("=")
        extra[key] = value

    out_dir = Path(args.out)
    out_dir.mkdir(parents=True, exist_ok=True)

    entries = [e for e in opsol.load_oracle() if eligible(opsol.puzzle_for(e["sha"]))]
    entries.sort(key=lambda e: (e["class"], e["best"]["cycles"]["cycles"]))
    selected = entries[:args.count]
    print(f"eligible={len(entries)} selected={len(selected)}")

    cases = []
    for entry in selected:
        puzzle = opsol.puzzle_for(entry["sha"])
        cases.append((1, puzzle, out_dir / f'{entry["sha"][:8]}-intake.sol'))
        cases.append((0, puzzle, out_dir / f'{entry["sha"][:8]}-feeder.sol'))
    log = opsol.run_layout_probe(cases, env=extra)
    (out_dir / "probe.log").write_text(log)

    rows = []
    rejected = 0
    for entry in selected:
        row = {"sha": entry["sha"][:8], "class": entry["class"],
               "reference_cycles": entry["best"]["cycles"]["cycles"]}
        for tag in ("feeder", "intake"):
            machine = out_dir / f'{entry["sha"][:8]}-{tag}.sol'
            if not machine.exists():
                row[tag] = "missing"
                continue
            report = opsol.verify(opsol.puzzle_for(entry["sha"]), machine)
            metrics = report.get("metrics") or {}
            row[tag] = {"status": report.get("status"),
                        "cycles": metrics.get("cycles"), "cost": metrics.get("cost"),
                        "area": metrics.get("area"),
                        "errorCycle": report.get("errorCycle"),
                        "errorLocation": report.get("errorLocation")}
            if row[tag]["status"] != "passed":
                rejected += 1
        rows.append(row)
    (out_dir / "rows.json").write_text(json.dumps(rows, indent=1))

    for row in rows:
        print(f'{row["sha"]} {row["class"][:16]:16s} ref={row["reference_cycles"]:6d} '
              f'intake={row["intake"]} feeder={row["feeder"]}')
    print(f"native rejections={rejected}/{2 * len(rows)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
