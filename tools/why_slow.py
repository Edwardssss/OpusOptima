#!/usr/bin/env python3
"""Explain why a solution is slow.

Every regression analysis in this repository eventually asks the same question:
*which object sets the period?* Answering it by hand means parsing the solution,
sorting tapes by length, and comparing against the oracle -- and the answer is
frequently counter-intuitive, because a machine can be highly concurrent and
still slow (with ``multiplier == 1`` there is a single product, so ``cycles`` is
the end-to-end *latency*, not a throughput figure).

Usage
-----
    python3 tools/why_slow.py 1dc60983
    python3 tools/why_slow.py 1dc60983 --solution path/to/x.solution
    python3 tools/why_slow.py 1dc60983 426d0058 --top 3

The report is deliberately mechanical: it prints the tapes that carry the
period, their action mix, and a one-line verdict, so a claim like "the transport
rail dominates" can be checked instead of asserted.
"""

from __future__ import annotations

import argparse
import collections
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from opsol import CORPUS, ORACLE, RESULT_ROOT, load_oracle, puzzle_for, read_puzzle  # noqa: E402
from opsol import read_solution  # noqa: E402

# Action letters as they appear in the tape. Only the ones that move material or
# change state are worth reporting; padding letters are summarised as "idle".
MOVE_ACTIONS = {
    "A": "extend",
    "a": "retract",
    "G": "grab",
    "g": "release",
    "R": "rotate ccw",
    "r": "rotate cw",
    "E": "extend-in",
    "e": "retract-in",
}
IDLE_ACTIONS = {"B", "O", "X"}


def build_oracle() -> dict[str, dict]:
    return {entry["sha"][:8]: entry for entry in load_oracle()}


def find_solution(key: str, explicit: Path | None) -> Path | None:
    if explicit is not None:
        return explicit
    # Newest bench directory first: the corpus is re-measured often and the newest
    # run is the one the current analysis refers to.
    for directory in sorted(RESULT_ROOT.glob("bench-*"), reverse=True):
        candidate = directory / f"{key}-control.solution"
        if candidate.exists():
            return candidate
    return None


def tape_actions(instructions: list[tuple[str, int]]) -> collections.Counter[str]:
    return collections.Counter(action for action, _ in instructions)


def report(key: str, solution: Path, oracle: dict[str, dict], top: int) -> int:
    slots, objects = read_solution(solution)
    cycles = slots[0][1] if slots else 0
    reference = None
    if key in oracle:
        reference = min(oracle[key]["best"][name]["cycles"]
                        for name in ("cost", "cycles", "area"))

    puzzle = None
    try:
        puzzle = read_puzzle(puzzle_for(key))
    except Exception:  # noqa: BLE001 - the report is still useful without the puzzle
        puzzle = None

    print(f"=== {key}  {solution}")
    if reference is not None:
        print(f"    cycles {cycles}   oracle {reference}   ratio {cycles / reference:.2f}x")
    else:
        print(f"    cycles {cycles}   (no oracle entry)")

    tapes = []
    for index, obj in enumerate(objects):
        instructions = obj.get("instructions") or []
        if not instructions:
            continue
        timestamps = [stamp for _, stamp in instructions]
        tapes.append({
            "index": index,
            "type": obj["type"],
            "position": obj["position"],
            "count": len(instructions),
            "span": max(timestamps) - min(timestamps) + 1,
            "actions": tape_actions(instructions),
            "cells": len(obj["cells"]) if obj.get("cells") else 0,
        })

    if not tapes:
        print("    no tapes")
        return 0

    total = sum(tape["count"] for tape in tapes)
    tapes.sort(key=lambda tape: tape["count"], reverse=True)
    dominant = tapes[0]
    share = dominant["count"] / cycles if cycles else 0.0
    print(f"    objects {len(objects)}   tapes {len(tapes)}   instructions {total}")
    print(f"    dominant tape = object {dominant['index']} ({dominant['type']} at "
          f"{dominant['position']}) with {dominant['count']} instructions = "
          f"{share * 100:.0f}% of the period")

    print(f"    {'obj':>4} {'type':<22} {'pos':<14} {'instr':>7} {'span':>7} {'track':>6}  mix")
    for tape in tapes[:top]:
        moving = collections.Counter({
            MOVE_ACTIONS[action]: count
            for action, count in tape["actions"].items() if action in MOVE_ACTIONS
        })
        idle = sum(count for action, count in tape["actions"].items() if action in IDLE_ACTIONS)
        mix = ", ".join(f"{name} {count}" for name, count in moving.most_common(4))
        if idle:
            mix += f", idle {idle}"
        print(f"    {tape['index']:>4} {tape['type']:<22} {str(tape['position']):<14} "
              f"{tape['count']:>7} {tape['span']:>7} {tape['cells']:>6}  {mix}")

    # Verdict. These three shapes need different fixes, so they are named apart.
    if cycles and dominant["count"] / cycles > 0.5:
        verdict = (
            f"ONE TAPE IS THE PERIOD: object {dominant['index']} carries "
            f"{share * 100:.0f}% of the cycles. Shortening this object's work "
            f"shortens the machine; adding parallelism elsewhere cannot."
        )
    elif cycles and total / cycles < 0.1:
        verdict = (
            f"WAITING, NOT WORKING: {total} instructions across {len(tapes)} tapes "
            f"for a {cycles}-cycle period. The machine is idle most of the time -- "
            f"look for a surplus/cleanup or handshake stall, not for shorter tapes."
        )
    else:
        verdict = (
            f"DISTRIBUTED: the longest tape is {share * 100:.0f}% of the period and "
            f"{total} instructions are spread over {len(tapes)} tapes. Concurrency is "
            f"real, so the cost is relay latency along the chain."
        )
    print(f"    verdict: {verdict}")

    if puzzle is not None:
        products = puzzle["outputs"]
        atoms = sum(len(output["atoms"]) for output in products)
        print(f"    puzzle: inputs {len(puzzle['inputs'])} "
              f"(molecular {sum(1 for m in puzzle['inputs'] if len(m['atoms']) > 1)}), "
              f"outputs {len(products)}, product atoms {atoms}, "
              f"multiplier {puzzle['multiplier']}")
        if cycles and puzzle["multiplier"] > 1:
            per_product = cycles / puzzle["multiplier"]
            print(f"    per-multiplier cycle: {per_product:.0f} "
                  f"(a repeated serial pass shows up as ~{dominant['span']} x mult)")
    return 0


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("keys", nargs="+", help="puzzle sha prefixes")
    parser.add_argument("--solution", type=Path, default=None,
                        help="explicit .solution path (single key only)")
    parser.add_argument("--top", type=int, default=5, help="tapes to list (default 5)")
    args = parser.parse_args(argv)

    if args.solution is not None and len(args.keys) != 1:
        parser.error("--solution requires exactly one key")

    oracle = build_oracle()
    status = 0
    for key in args.keys:
        solution = find_solution(key, args.solution)
        if solution is None:
            print(f"=== {key}\n    no *.solution found under {RESULT_ROOT}; "
                  f"pass --solution", file=sys.stderr)
            status = 1
            continue
        status |= report(key, solution, oracle, args.top)
        print()
    return status


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
