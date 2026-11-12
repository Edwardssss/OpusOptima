#!/usr/bin/env python3
"""Structural inventory of reference (OpusSolver) machines versus ours.

Reads a directory of reference `.solution` files and a directory of our own, and
reports which *mechanisms* each side builds with, so a capability gap can be named
instead of guessed.

    python3 tools/reference_inventory.py \
        --ref /home/edwards/OpusOptima/artifacts/result/opussolver-oracle-20261110/solutions \
        --ours result/bench-ordered-network-20261112 \
        --oracle /home/edwards/OpusOptima/artifacts/result/opussolver-oracle-20261110/oracle.json
"""
from __future__ import annotations

import argparse
import collections
import json
import statistics
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import solution_probe  # noqa: E402

ARMS = {"arm1", "arm2", "arm3", "arm4", "arm5", "arm6", "piston", "baron"}
GLYPHS = {
    "bonder", "bonder-prisma", "bonder-speed", "unbonder", "unbonder-prisma",
    "glyph-calcification", "glyph-projection", "glyph-purification",
    "glyph-duplication", "glyph-life-and-death", "glyph-disposal",
    "glyph-marker", "glyph-unification", "glyph-dispersion",
}


def features(path: Path, override_cycles: int = 0) -> dict:
    machine = solution_probe.read(str(path))
    counts = collections.Counter(obj.kind for obj in machine.objects)
    arms = [obj for obj in machine.objects if obj.kind in ARMS]
    tracks = [obj for obj in machine.objects if obj.kind == "track"]
    periods = [obj.period for obj in machine.objects]
    period = max(periods) if periods else 0
    # An arm is "used" when its tape does something other than wait.
    active = [obj for obj in arms if any(ch != "O" for ch, _ in obj.tape)]
    grabs = sum(sum(1 for ch, _ in obj.tape if ch in "gG") for obj in arms)
    rotations = sum(sum(1 for ch, _ in obj.tape if ch in "rR") for obj in arms)
    waits = sum(sum(1 for ch, _ in obj.tape if ch == "O") for obj in arms)
    track_hex = sum(obj.body[4] for obj in tracks if len(obj.body) > 4)
    cells = [(obj.q, obj.r) for obj in machine.objects]
    for obj in tracks:
        segments = obj.body[4] if len(obj.body) > 4 else 0
        for i in range(segments):
            cells.append((obj.body[5 + 2 * i], obj.body[6 + 2 * i]))
    qs = [q for q, _ in cells] or [0]
    rs = [r for _, r in cells] or [0]
    work = sum(len(obj.tape) for obj in arms)
    return {
        "path": str(path),
        # OpusSolver leaves the embedded metrics zeroed, so the oracle is the only
        # trustworthy cycle count for a reference machine.
        "cycles": override_cycles or machine.metrics.get("cycles", 0),
        "cost": machine.metrics.get("cost", 0),
        "area": machine.metrics.get("area", 0),
        "instructions": machine.metrics.get("instructions", 0),
        "objects": len(machine.objects),
        "period": period,
        "batches": (override_cycles / period) if (period and override_cycles) else 0.0,
        "work": work,
        "utilization": (work / (period * len(arms))) if (period and arms) else 0.0,
        "rot_per_grab": (rotations / grabs) if grabs else 0.0,
        "ticks_per_grab": (period / grabs) if grabs else 0.0,
        "hex_per_grab": (track_hex / grabs) if grabs else 0.0,
        "arms": len(arms),
        "active_arms": len(active),
        "tracks": len(tracks),
        "track_hex": track_hex,
        "max_track_hex": max((obj.body[4] for obj in tracks if len(obj.body) > 4), default=0),
        "glyphs": sum(counts[t] for t in GLYPHS),
        "grabs": grabs,
        "rotations": rotations,
        "waits": waits,
        "extent_q": max(qs) - min(qs) + 1,
        "extent_r": max(rs) - min(rs) + 1,
        "counts": dict(counts),
        "types": set(counts),
        "arm_ext": dict(collections.Counter(obj.body[0] for obj in arms if obj.body)),
    }


def median(values):
    return statistics.median(values) if values else 0


def load_ours(directory: Path, shas: list[str]) -> dict[str, Path]:
    found = {}
    for sha in shas:
        for candidate in directory.glob(f"{sha[:8]}*control*.solution"):
            found[sha] = candidate
            break
        else:
            for candidate in directory.glob(f"{sha[:8]}*.solution"):
                found[sha] = candidate
                break
    return found


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ref", required=True)
    parser.add_argument("--ours", required=True)
    parser.add_argument("--oracle", required=True)
    args = parser.parse_args()

    oracle = {entry["sha"]: entry for entry in json.loads(Path(args.oracle).read_text())}
    ref_dir = Path(args.ref)
    ours = load_ours(Path(args.ours), list(oracle))

    ref_rows, ours_rows = {}, {}
    for sha in oracle:
        matches = sorted(ref_dir.glob(f"{sha}_*.solution"))
        # Prefer the cycles-optimal machine: it is the one whose period we chase.
        ordered = [p for p in matches if p.name.endswith("_cycles.solution")] + matches
        if ordered:
            try:
                ref_rows[sha] = features(ordered[0],
                                         oracle[sha]["best"]["cycles"]["cycles"])
            except Exception as error:  # noqa: BLE001 - inventory must not die mid-run
                print(f"ref parse failed {sha[:8]}: {error}", file=sys.stderr)
        if sha in ours:
            try:
                ours_rows[sha] = features(ours[sha])
            except Exception as error:  # noqa: BLE001
                print(f"ours parse failed {sha[:8]}: {error}", file=sys.stderr)

    pairs = sorted(set(ref_rows) & set(ours_rows))
    print(f"pairs={len(pairs)}  ref={len(ref_rows)}  ours={len(ours_rows)}\n")

    # 1. Mechanism coverage: how many machines on each side use a given object type.
    kinds = sorted({t for row in ref_rows.values() for t in row["types"]} |
                   {t for row in ours_rows.values() for t in row["types"]})
    print(f"{'object type':22s} {'ref uses':>9s} {'ours uses':>10s} "
          f"{'ref med':>8s} {'ours med':>9s}")
    for kind in kinds:
        ref_has = [row for row in ref_rows.values() if kind in row["types"]]
        our_has = [row for row in ours_rows.values() if kind in row["types"]]
        print(f"{kind:22s} {len(ref_has):9d} {len(our_has):10d} "
              f"{median([r['counts'][kind] for r in ref_has]):8.0f} "
              f"{median([r['counts'][kind] for r in our_has]):9.0f}")

    # 2. Structural medians per architecture class.
    def report(rows: dict, label: str) -> None:
        print(f"\n== {label} ==")
        print(f"{'class':22s} {'n':>4s} {'period':>7s} {'cycles':>7s} {'cyc/per':>8s} "
              f"{'arms':>5s} {'tracks':>6s} {'hex':>5s} {'glyphs':>6s} {'grabs':>6s} "
              f"{'rot':>5s} {'util':>5s} {'rot/g':>6s} {'tk/g':>6s} {'hex/g':>6s}")
        for name in sorted({oracle[sha]["class"] for sha in rows}):
            group = [row for sha, row in rows.items() if oracle[sha]["class"] == name]
            print(f"{name:22s} {len(group):4d} "
                  f"{median([r['period'] for r in group]):7.0f} "
                  f"{median([r['cycles'] for r in group]):7.0f} "
                  f"{median([r['batches'] for r in group]):8.2f} "
                  f"{median([r['arms'] for r in group]):5.0f} "
                  f"{median([r['tracks'] for r in group]):6.0f} "
                  f"{median([r['track_hex'] for r in group]):5.0f} "
                  f"{median([r['glyphs'] for r in group]):6.0f} "
                  f"{median([r['grabs'] for r in group]):6.0f} "
                  f"{median([r['rotations'] for r in group]):5.0f} "
                  f"{median([r['utilization'] for r in group]):5.2f} "
                  f"{median([r['rot_per_grab'] for r in group]):6.2f} "
                  f"{median([r['ticks_per_grab'] for r in group]):6.1f} "
                  f"{median([r['hex_per_grab'] for r in group]):6.2f}")

    report(ref_rows, "reference (OpusSolver Standard, cycles-optimal)")
    report(ours_rows, "ours (default build)")

    # 3. Cycle budget: where the reference spends its period on a per-puzzle basis.
    print("\n== period and batch structure (per-puzzle, ours vs reference) ==")
    faster = collections.Counter()
    for sha in pairs:
        ours_row, ref_row = ours_rows[sha], ref_rows[sha]
        ratio = ours_row["cycles"] / ref_row["cycles"] if ref_row["cycles"] else float("inf")
        if ratio >= 4:
            faster["cycles x4+"] += 1
        elif ratio >= 1.5:
            faster["cycles x1.5-4"] += 1
        elif ratio > 1:
            faster["cycles slightly slower"] += 1
        else:
            faster["cycles equal or better"] += 1
    for key, value in faster.most_common():
        print(f"  {key:26s} {value:4d}")

    print("\n  worst 15 puzzles by cycles ratio")
    print(f"  {'sha':10s} {'class':20s} {'ref period':>11s} {'our period':>11s} "
          f"{'ref cyc':>8s} {'our cyc':>8s} {'ref grabs':>10s} {'our grabs':>10s}")
    ranked = sorted(pairs, key=lambda s: -(ours_rows[s]["cycles"] /
                                           ref_rows[s]["cycles"] if ref_rows[s]["cycles"] else 0))
    for sha in ranked[:15]:
        ref_row, our_row = ref_rows[sha], ours_rows[sha]
        print(f"  {sha[:8]:10s} {oracle[sha]['class'][:20]:20s} "
              f"{ref_row['period']:11d} {our_row['period']:11d} "
              f"{ref_row['cycles']:8d} {our_row['cycles']:8d} "
              f"{ref_row['grabs']:10d} {our_row['grabs']:10d}")

    # 4. Arm extension vocabulary: which arm sizes each side ever builds.
    print("\n== arm extension (body[0]) vocabulary ==")
    for label, rows in (("ref", ref_rows), ("ours", ours_rows)):
        merged = collections.Counter()
        for row in rows.values():
            merged.update(row["arm_ext"])
        print(f"  {label:5s} " + "  ".join(f"{k}:{v}" for k, v in sorted(merged.items())))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
