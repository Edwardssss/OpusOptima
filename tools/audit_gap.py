#!/usr/bin/env python3
"""Measure where the shipped solver stands against the OpusSolver oracle.

Runs every puzzle in the reference corpus once with the shipped defaults, then
reports per-slot win/tie/lose counts and the distribution of the cycles ratio
(ours / oracle). Unlike bench_corpus.py this runs a single configuration, so it
answers "how far are we" rather than "did this knob help".

    python3 tools/audit_gap.py --jobs 8
"""

from __future__ import annotations

import argparse
import json
import statistics
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

import opsol


def run_one(entry: dict, out_dir: Path) -> dict:
    sha = entry["sha"]
    out = out_dir / f"{sha[:8]}.solution"
    slots = opsol.run_solver(opsol.puzzle_for(sha), out)
    row = {"sha": sha, "class": entry["class"], "best": entry["best"],
           "status": "missing" if slots is None else "ok"}
    if slots is not None:
        row["slots"] = slots
    return row


def ratio_row(row: dict) -> dict | None:
    """Per-slot ours/oracle ratios for one puzzle."""
    out = {}
    for key in opsol.SLOT_KEYS:
        mine = row["slots"][key][key]
        oracle = row["best"][key][key]
        if oracle and mine is not None:
            out[key] = mine / oracle
    return out or None


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--jobs", type=int, default=8)
    parser.add_argument("--out", default=str(opsol.RESULT_ROOT / "audit-gap-20261112"))
    parser.add_argument("--json", default="")
    args = parser.parse_args()

    entries = opsol.load_oracle()
    out_dir = Path(args.out)
    out_dir.mkdir(parents=True, exist_ok=True)

    with ThreadPoolExecutor(max_workers=args.jobs) as pool:
        rows = list(pool.map(lambda e: run_one(e, out_dir), entries))

    ok = [r for r in rows if r["status"] == "ok"]
    missing = [r for r in rows if r["status"] != "ok"]

    print(f"produced {len(ok)}/{len(entries)}  missing {len(missing)}")
    if missing:
        print("  missing:", " ".join(r["sha"][:8] for r in missing))

    wins = {k: 0 for k in opsol.SLOT_KEYS}
    ties = {k: 0 for k in opsol.SLOT_KEYS}
    loses = {k: 0 for k in opsol.SLOT_KEYS}
    allwin = alllose = 0
    ratios: dict[str, list[float]] = {k: [] for k in opsol.SLOT_KEYS}
    worst = []
    for row in ok:
        per = ratio_row(row)
        if not per:
            continue
        every_win = True
        every_lose = False
        for key, value in per.items():
            ratios[key].append(value)
            if value < 1:
                wins[key] += 1
                every_lose = False
            elif value == 1:
                ties[key] += 1
                every_win = False
            else:
                loses[key] += 1
                every_win = False
                every_lose = True
        allwin += int(every_win)
        alllose += int(every_lose)
        worst.append((per.get("cycles", 0.0), row["sha"], row["class"]))

    print(f"\nper-slot (ours vs oracle) over {len(ok)} puzzles")
    for key in opsol.SLOT_KEYS:
        r = sorted(ratios[key])
        if not r:
            continue
        print(f"  {key:7s} win {wins[key]:3d}  tie {ties[key]:3d}  lose {loses[key]:3d}"
              f"   ratio p50 {statistics.median(r):5.2f}  p90 {r[int(0.9*(len(r)-1))]:6.2f}"
              f"  max {r[-1]:7.2f}")
    print(f"\n  puzzles winning every slot: {allwin}")
    print(f"  puzzles losing every slot:  {alllose}")

    worst.sort(reverse=True)
    print("\nworst 20 by cycles ratio:")
    for value, sha, klass in worst[:20]:
        print(f"  {sha[:8]}  {klass[:24]:24s} {value:7.2f}x")

    if args.json:
        Path(args.json).write_text(json.dumps(rows, indent=1))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
