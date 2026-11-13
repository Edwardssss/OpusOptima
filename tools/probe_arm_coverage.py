#!/usr/bin/env python3
"""How much of the corpus can the arm backend actually solve?

Every round so far has tuned one detail of the arm backend and watched `1dc60983`
fail at a new gate. That is a single puzzle. Before spending another round on it,
measure the whole route: run the corpus with life-death disabled so the material /
serial-arm families get the budget, and report per puzzle

  * whether any solution is produced at all,
  * its cycles versus the life-death default and versus the oracle.

Cases the arm backend already solves are the ones whose machinery is proven; a
zero result means the backend is not yet a general alternative and the next round
should debug the simplest failing shape instead of the hardest one.
"""

from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import opsol  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent


def cycles_of(out: Path) -> int | None:
    objectives = out.with_suffix(out.suffix + ".objectives.json")
    if not objectives.exists():
        return None
    try:
        # `read_slots` already unwraps to ``{slot: verification metrics}``.
        slots = opsol.read_slots(objectives)
    except Exception:
        return None
    best = None
    for metrics in slots.values():
        cycles = metrics.get("cycles")
        if cycles is None or cycles < 0:
            continue
        best = cycles if best is None else min(best, cycles)
    return best


def run_one(args: tuple[str, Path, dict[str, str]]) -> dict:
    sha, out_dir, env = args
    puzzle = opsol.puzzle_for(sha)
    out = out_dir / f"{sha}.solution"
    process = subprocess.run(
        [str(ROOT / "build" / "OpusOptima"), "-p", str(puzzle), "-o", str(out)],
        capture_output=True,
        text=True,
        env=env,
        cwd=ROOT,
    )
    reasons = [
        line.strip()
        for line in (process.stdout + process.stderr).splitlines()
        if "serial-arms" in line or "strategies exhausted" in line
    ]
    return {
        "sha": sha,
        "cycles": cycles_of(out),
        "exit": process.returncode,
        "reason": reasons[-1] if reasons else "",
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--jobs", type=int, default=8)
    parser.add_argument("--out", type=Path, default=ROOT / "result" / "arm-coverage")
    parser.add_argument("--json", type=Path)
    parser.add_argument("--limit", type=int, default=0)
    parser.add_argument("--sha", action="append", default=[])
    parser.add_argument(
        "--keep-life-death",
        action="store_true",
        help="leave the life-death family enabled so the arm route competes with it",
    )
    parser.add_argument(
        "--plain",
        action="store_true",
        help="run the untouched default so the two routes share one measurement",
    )
    options = parser.parse_args()
    options.out.mkdir(parents=True, exist_ok=True)

    if options.sha:
        shas: list[str] = options.sha
    else:
        shas = [row["sha"] for row in opsol.load_oracle()]
        if options.limit:
            shas = shas[: options.limit]

    env = dict(os.environ)
    if not options.plain:
        env["OPUSOPTIMA_SERIAL_ANIMISMUS"] = "1"
        env["OPUSOPTIMA_CALCIFY_ANIMISMUS"] = "1"
        env["OPUSOPTIMA_SERIAL_LONG_ARMS"] = "1"
        if not options.keep_life_death:
            env["OPUSOPTIMA_SKIP_LIFE_DEATH"] = "1"

    with ThreadPoolExecutor(max_workers=options.jobs) as pool:
        rows = list(pool.map(run_one, [(sha, options.out, env) for sha in shas]))

    solved = [row for row in rows if row["cycles"] is not None]
    print(f"arm route: {len(solved)}/{len(rows)} produced a verified solution")
    for row in solved:
        print(f"  {row['sha']}  {row['cycles']}")
    if options.json:
        options.json.parent.mkdir(parents=True, exist_ok=True)
        options.json.write_text(json.dumps(rows, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
