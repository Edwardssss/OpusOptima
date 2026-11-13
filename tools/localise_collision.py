#!/usr/bin/env python3
"""Who was standing on the collision cell?

The legacy gate rejects a machine with a reason code and, since the diagnostic
landed, a cycle and a cell -- but never the object. Every geometry change in this
repository has therefore been a blind guess. This replays the candidate's tapes
just far enough to answer that question:

  * a fixed arm (`arm1/2/3`) keeps its base;
  * a `piston` walks its rail, one cell per `A`/`a` instruction;
  * glyphs, tracks, bonders and ports never move.

It reports every object whose base is on, or adjacent to, the collision cell at
the collision cycle. That is enough to turn "collision at (188,3)" into "the
product-station piston and the storage arm both wanted that cell".

Usage:
  OPUSOPTIMA_DUMP_REJECTED=/tmp/x ./build/OpusOptima -p <puzzle> -o /tmp/x.solution
  tools/localise_collision.py /tmp/x.rejected.solution --cycle 1143 --cell 188,3
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import opsol  # noqa: E402

DIRECTIONS = {(1, 0), (0, 1), (-1, 1), (-1, 0), (0, -1), (1, -1)}


def rail_of(objs: list[dict], start: tuple[int, int]) -> list[tuple[int, int]] | None:
    for obj in objs:
        if obj["type"] != "track" or not obj.get("cells"):
            continue
        cells = [(int(q), int(r)) for q, r in obj["cells"]]
        if tuple(start) in cells:
            return cells
    return None


def base_at(objs: list[dict], obj: dict, cycle: int) -> tuple[int, int]:
    """Where this object's base sits at ``cycle``."""
    position = (int(obj["position"][0]), int(obj["position"][1]))
    if obj["type"] != "piston":
        return position
    rail = rail_of(objs, position)
    if not rail:
        return position
    index = rail.index(position)
    for opcode, parameter in obj.get("instructions") or []:
        if parameter > cycle:
            break
        if opcode == "A":
            index = min(index + 1, len(rail) - 1)
        elif opcode == "a":
            index = max(index - 1, 0)
    return rail[index]


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("solution", type=Path)
    parser.add_argument("--cycle", type=int, required=True)
    parser.add_argument("--cell", required=True, help="q,r")
    parser.add_argument("--radius", type=int, default=1)
    options = parser.parse_args()

    q, r = (int(part) for part in options.cell.split(","))
    _, objs = opsol.read_solution(options.solution)
    print(f"solution: {options.solution}  objects={len(objs)}")
    print(f"collision: cycle={options.cycle} cell=({q},{r}) radius={options.radius}")

    for index, obj in enumerate(objs):
        base = base_at(objs, obj, options.cycle)
        distance = max(abs(base[0] - q), abs(base[1] - r))
        if distance <= options.radius:
            print(
                f"  index={index:3d} {obj['type']:20s} base_now={base} "
                f"spawn={tuple(obj['position'])} distance={distance}"
            )
        elif obj.get("cells"):
            cells = {(int(cq), int(cr)) for cq, cr in obj["cells"]}
            if (q, r) in cells:
                print(f"  index={index:3d} {obj['type']:20s} rail passes through ({q},{r})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
