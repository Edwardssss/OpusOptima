#!/usr/bin/env python3
"""Summarise one `bench_corpus.py` run: per-class medians plus per-puzzle deltas.

Usage: python3 tools/ab_summary.py result/bench-<name>/rows.json
"""
import collections
import json
import statistics
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import opsol  # noqa: E402

SLOTS = opsol.SLOT_KEYS


def median(values):
    return statistics.median(values) if values else float("nan")


def main(path):
    rows = json.load(open(path))
    control = {r["sha"]: r for r in rows if r["mode"] == "control" and r["status"] == "ok"}
    variant = {r["sha"]: r for r in rows if r["mode"] == "variant" and r["status"] == "ok"}
    best = {entry["sha"]: entry for entry in opsol.load_oracle()}
    shas = sorted(set(control) & set(variant))
    print(f"pairs={len(shas)} control={len(control)} variant={len(variant)}")

    for label, table in (("control", control), ("variant", variant)):
        wins = {slot: sum(table[s]["wins"][slot] for s in shas) for slot in SLOTS}
        anywin = sum(table[s]["any"] for s in shas)
        print(f"  {label:7s} any-win={anywin:3d} " +
              " ".join(f"{slot}={wins[slot]:3d}" for slot in SLOTS))

    classes = collections.defaultdict(list)
    for sha in shas:
        classes[best[sha]["class"]].append(sha)
    print("\nclass medians, ours / reference")
    for name in sorted(classes):
        group = classes[name]
        cells = []
        for slot in SLOTS:
            ratios = [variant[s]["slots"][slot][slot] / best[s]["best"][slot][slot] for s in group]
            cells.append(median(ratios))
        print(f"  {name:22s} n={len(group):3d}  " + "  ".join(f"{v:6.2f}" for v in cells))
    allratios = [[variant[s]["slots"][slot][slot] / best[s]["best"][slot][slot] for s in shas]
                 for slot in SLOTS]
    print(f"  {'ALL':22s} n={len(shas):3d}  " + "  ".join(f"{median(r):6.2f}" for r in allratios))

    changed, improved, regressed, mixed = [], [], [], []
    for sha in shas:
        old, new = control[sha]["slots"], variant[sha]["slots"]
        if all(old[slot][slot] == new[slot][slot] for slot in SLOTS):
            continue
        delta = " ".join(f"{slot} {old[slot][slot]}->{new[slot][slot]}" for slot in SLOTS
                         if old[slot][slot] != new[slot][slot])
        record = (sha, best[sha]["class"], delta)
        changed.append(record)
        if all(new[s][s] <= old[s][s] for s in SLOTS):
            improved.append(record)
        elif all(new[s][s] >= old[s][s] for s in SLOTS):
            regressed.append(record)
        else:
            mixed.append(record)

    print(f"\nchanged={len(changed)} improved={len(improved)} "
          f"regressed={len(regressed)} mixed={len(mixed)}")
    for title, group in (("improve", improved), ("regress", regressed), ("mixed", mixed)):
        for sha, klass, delta in group:
            print(f"  {title:7s} {sha[:8]} {klass[:20]:20s} {delta}")


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1
         else "result/bench-ordered-network-20261112/rows.json")
