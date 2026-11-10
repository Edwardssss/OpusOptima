#!/usr/bin/env python3
"""Full-corpus A/B of two solver configurations, judged by the native gate.

Every puzzle in the reference corpus is solved twice: once with the *control*
environment and once with the *variant* environment. The three objective slots
(cost, cycles, area) are then compared against the OpusSolver oracle and against
each other. The run is the authoritative measurement before any default flips.

Examples
--------
Local intake on (default) versus off::

    python3 tools/bench_corpus.py

Any other knob is just an environment assignment::

    python3 tools/bench_corpus.py --variant OPUSOPTIMA_SLOT_ADMISSION=on \
                                  --variant OPUSOPTIMA_SLOT_ADMISSION=$OPUSOPTIMA_SLOT_ADMISSION

Measure only a subset or reuse an existing artefact directory::

    python3 tools/bench_corpus.py --only 18644c2c,a94b5296 --jobs 2
"""

from __future__ import annotations

import argparse
import json
import shlex
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

import opsol


def parse_env(pairs: list[str]) -> dict[str, str]:
    env: dict[str, str] = {}
    for pair in pairs:
        if "=" not in pair:
            raise SystemExit(f"--variant expects KEY=VALUE, got {pair!r}")
        key, value = pair.split("=", 1)
        env[key] = shlex.split(value)[0] if value else ""
    return env


def run_one(entry: dict, mode: str, puzzle: Path, out_dir: Path,
            control: dict[str, str], variant: dict[str, str]) -> dict:
    sha = entry["sha"]
    out = out_dir / f"{sha[:8]}-{mode}.solution"
    env = control if mode == "control" else variant
    slots = opsol.run_solver(puzzle, out, env=env)
    if slots is None:
        return {"sha": sha, "class": entry["class"], "mode": mode, "status": "missing"}
    wins = opsol.slot_wins(slots, entry["best"])
    return {"sha": sha, "class": entry["class"], "mode": mode, "status": "ok",
            "slots": slots, "wins": wins,
            "any": int(any(wins.values())), "all": int(all(wins.values()))}


def summarise(rows: list[dict], label: str) -> dict:
    group = [r for r in rows if r["mode"] == label]
    ok = [r for r in group if r["status"] == "ok"]
    totals = {key: sum(r["wins"][key] for r in ok) for key in opsol.SLOT_KEYS}
    return {"produced": len(ok), "expected": len(group), "any": sum(r["any"] for r in ok),
            "all": sum(r["all"] for r in ok), **totals}


def compare(rows: list[dict], entries: list[dict]) -> list[str]:
    index = {(r["sha"], r["mode"]): r for r in rows}
    lines = []
    improved = regressed = same = 0
    for entry in entries:
        control = index.get((entry["sha"], "control"))
        variant = index.get((entry["sha"], "variant"))
        if not control or not variant:
            continue
        if control["status"] != "ok" or variant["status"] != "ok":
            continue
        delta = [f"{key}:{control['slots'][key][key]}->{variant['slots'][key][key]}"
                 for key in opsol.SLOT_KEYS
                 if variant["slots"][key][key] != control["slots"][key][key]]
        if not delta:
            same += 1
            continue
        worse = any(variant["slots"][key][key] > control["slots"][key][key]
                    for key in opsol.SLOT_KEYS)
        if worse:
            regressed += 1
            lines.append(f"REGRESS {entry['sha'][:8]} {entry['class'][:16]:16s} " + "; ".join(delta))
        else:
            improved += 1
            lines.append(f"improve {entry['sha'][:8]} {entry['class'][:16]:16s} " + "; ".join(delta))
    lines.insert(0, f"changed: improved={improved} regressed={regressed} unchanged={same}")
    return lines


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--control", action="append", default=[],
                        metavar="KEY=VALUE",
                        help="environment of the control run "
                             "(default: OPUSOPTIMA_DIRECT_INPUT=off)")
    parser.add_argument("--variant", action="append", default=[],
                        metavar="KEY=VALUE",
                        help="environment of the variant run (default: intake as shipped)")
    parser.add_argument("--only", default="", help="comma separated puzzle sha prefixes")
    parser.add_argument("--jobs", type=int, default=4, help="parallel solver runs (default 4)")
    parser.add_argument("--out", default=str(opsol.RESULT_ROOT / "bench"),
                        help="artefact directory for the produced solutions")
    parser.add_argument("--json", default="", help="also write the raw rows to this file")
    args = parser.parse_args()

    control = parse_env(args.control) or {"OPUSOPTIMA_DIRECT_INPUT": "off"}
    variant = parse_env(args.variant) or {"OPUSOPTIMA_DIRECT_INPUT": ""}
    # An empty value means "unset", which is how a knob is restored to its default.
    variant = {k: v for k, v in variant.items() if v}

    entries = opsol.load_oracle()
    if args.only:
        prefixes = tuple(p.strip() for p in args.only.split(",") if p.strip())
        entries = [e for e in entries if e["sha"].startswith(prefixes)]

    out_dir = Path(args.out)
    out_dir.mkdir(parents=True, exist_ok=True)
    jobs = [(entry, mode, opsol.puzzle_for(entry["sha"]), out_dir, control, variant)
            for entry in entries for mode in ("control", "variant")]

    with ThreadPoolExecutor(max_workers=max(1, args.jobs)) as pool:
        rows = list(pool.map(lambda job: run_one(*job), jobs))

    if args.json:
        Path(args.json).write_text(json.dumps(rows, indent=1))

    for mode in ("control", "variant"):
        s = summarise(rows, mode)
        print(f'{mode:7s} produced={s["produced"]:3d}/{s["expected"]:3d} '
              f'any-win={s["any"]:3d} cost={s["cost"]:3d} cycles={s["cycles"]:3d} '
              f'area={s["area"]:3d} all-three={s["all"]:3d}')
    print(f"control env: {control or '{}'}")
    print(f"variant env: {variant or '{}'}")
    for line in compare(rows, entries):
        print("  " + line if not line.startswith("changed:") else line)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
