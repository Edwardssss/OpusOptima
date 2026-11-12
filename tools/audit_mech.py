#!/usr/bin/env python3
"""Compare the machine inventory we emit with the one the oracle used.

The oracle entry carries ``bonders`` / ``arm1`` / ``pistons`` / ``tracks``, so
this reads the solutions produced by ``audit_gap.py`` and reports how many of
each part we use versus OpusSolver, grouped by how far behind the puzzle is.

    python3 tools/audit_mech.py result/audit-gap-20261112/rows.json
"""

from __future__ import annotations

import argparse
import json
import statistics
from pathlib import Path

import opsol

PARTS = ("arm", "bonder", "piston", "track")


def count_parts(objects: list[dict]) -> dict[str, int]:
    counts = {p: 0 for p in PARTS}
    for obj in objects:
        name = obj["type"]
        # The oracle's `arm1` column counts the arm1 part specifically, not every
        # arm variant, so match it exactly and fold the rest into the same bucket
        # only for the delta-free totals below.
        if name == "arm1":
            counts["arm"] += 1
        elif name.startswith("arm"):
            counts["arm"] += 1
        elif name.startswith("bonder"):
            counts["bonder"] += 1
        elif name.startswith("piston"):
            counts["piston"] += 1
        elif name.startswith("track"):
            counts["track"] += 1
    return counts


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("rows")
    parser.add_argument("--dir", default="result/audit-gap-20261112")
    args = parser.parse_args()

    rows = json.loads(Path(args.rows).read_text())
    out_dir = Path(args.dir)
    # The saved rows only carry the slots; the part inventory lives in the oracle.
    oracle = {entry["sha"]: entry for entry in opsol.load_oracle()}

    groups: dict[str, dict[str, list[float]]] = {}
    ours_total = {p: 0 for p in PARTS}
    oracle_total = {p: 0 for p in PARTS}
    counted = 0

    for row in rows:
        if row["status"] != "ok":
            continue
        path = out_dir / f"{row['sha'][:8]}.solution"
        if not path.exists():
            continue
        _, objects = opsol.read_solution(path)
        mine = count_parts(objects)
        ref = oracle.get(row["sha"])
        if ref is None:
            continue
        theirs = {"arm": ref["arm1"], "bonder": ref["bonders"],
                  "piston": ref["pistons"], "track": ref["tracks"]}
        ratio = row["slots"]["cycles"]["cycles"] / row["best"]["cycles"]["cycles"]
        bucket = "won <=1x" if ratio <= 1.0 else "behind <=2x" if ratio <= 2.0 \
            else "behind <=5x" if ratio <= 5.0 else "behind >5x"
        g = groups.setdefault(bucket, {p: [] for p in PARTS})
        for part in PARTS:
            g[part].append(mine[part] - theirs[part])
            ours_total[part] += mine[part]
            oracle_total[part] += theirs[part]
        counted += 1

    print(f"counted {counted} puzzles\n")
    header = f"{'bucket':12s} {'n':>3s}  " + "  ".join(f"{p:>16s}" for p in PARTS)
    print(header)
    print(f"{'':12s} {'':>3s}  " + "  ".join(f"{'ours-oracle(med)':>16s}" for p in PARTS))
    for bucket in ("won <=1x", "behind <=2x", "behind <=5x", "behind >5x"):
        if bucket not in groups:
            continue
        g = groups[bucket]
        cells = "  ".join(f"{statistics.median(g[p]):+16.1f}" for p in PARTS)
        print(f"{bucket:12s} {len(g['arm']):3d}  {cells}")

    print("\ntotals")
    for part in PARTS:
        delta = ours_total[part] - oracle_total[part]
        print(f"  {part:8s} ours {ours_total[part]:6d}   oracle {oracle_total[part]:6d}"
              f"   delta {delta:+6d}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
