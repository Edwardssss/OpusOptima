"""Shared helpers for the OpusOptima measurement tools.

Everything the tools need to talk to the project lives here:

* locating the repository and the artefact tree (``../artifacts`` by default),
* the corpus and oracle paths used for comparisons,
* thin wrappers around the two binaries (``build/OpusOptima`` and
  ``build/OpusOptimaTests``),
* readers for the native gate report and the objective slot file.

Path conventions
----------------
The repository only contains source, tests, assets and tools. Solver runs,
probe output and reports live outside the tree::

    <workspace>/artifacts/build     <- symlinked as ./build
    <workspace>/artifacts/log       <- symlinked as ./log
    <workspace>/artifacts/result    <- symlinked as ./result

Every path below can be overridden with an environment variable so the tools keep
working if the artefact tree moves:

=============== ==================== ==========================================
variable         default              meaning
=============== ==================== ==========================================
OPUSOPTIMA_BIN   build/OpusOptima     solver CLI
OPUSOPTIMA_TESTS build/OpusOptimaTests test binary (layout probe)
OPUSOPTIMA_RESULT result              artefact root (solver runs, reports)
OPUSOPTIMA_CORPUS <result>/solver-generalization-track-envelope-default-integrated-20261109/inputs
OPUSOPTIMA_ORACLE <result>/opussolver-oracle-20261110/oracle.json
=============== ==================== ==========================================
"""

from __future__ import annotations

import json
import os
import struct
import subprocess
from pathlib import Path
from typing import Iterable

# ── repository and artefact locations ────────────────────────────────────────

REPO = Path(__file__).resolve().parent.parent
"""Repository root (the directory holding ``CMakeLists.txt``)."""


def _env_path(name: str, default: Path) -> Path:
    return Path(os.environ.get(name, default)).expanduser()


RESULT_ROOT = _env_path("OPUSOPTIMA_RESULT", REPO / "result")
"""Artefact root; ``result`` is a symlink into ``../artifacts/result``."""

CORPUS = _env_path(
    "OPUSOPTIMA_CORPUS",
    RESULT_ROOT / "solver-generalization-track-envelope-default-integrated-20261109" / "inputs",
)
"""Directory holding the 146 reference ``<sha>.puzzle`` inputs."""

ORACLE = _env_path("OPUSOPTIMA_ORACLE", RESULT_ROOT / "opussolver-oracle-20261110" / "oracle.json")
"""OpusSolver reference metrics used as the comparison baseline."""

SOLVER = _env_path("OPUSOPTIMA_BIN", REPO / "build" / "OpusOptima")
TESTS = _env_path("OPUSOPTIMA_TESTS", REPO / "build" / "OpusOptimaTests")

SLOT_KEYS = ("cost", "cycles", "area")


class ToolError(RuntimeError):
    """Raised when a prerequisite (binary, corpus, oracle) is missing."""


def require(path: Path, what: str) -> Path:
    if not path.exists():
        raise ToolError(f"{what} not found: {path}\n"
                        f"Build first (cmake --build build) or point the matching "
                        f"OPUSOPTIMA_* environment variable at it.")
    return path


def load_oracle() -> list[dict]:
    """Load the reference corpus description: ``[{sha, class, best{slot{slot:value}}}...]``."""
    return json.loads(require(ORACLE, "oracle").read_text())


def puzzle_for(sha: str) -> Path:
    """Resolve a full or abbreviated puzzle sha to its input file."""
    corpus = require(CORPUS, "corpus")
    if len(sha) == 64 and (corpus / f"{sha}.puzzle").exists():
        return corpus / f"{sha}.puzzle"
    matches = sorted(p for p in corpus.glob(f"{sha}*.puzzle") if p.stem.startswith(sha))
    if not matches:
        raise ToolError(f"no puzzle starting with {sha!r} in {corpus}")
    if len(matches) > 1:
        raise ToolError(f"{sha!r} is ambiguous: {[p.name for p in matches]}")
    return matches[0]


# ── solution / puzzle readers ────────────────────────────────────────────────


class _Reader:
    """Minimal little-endian reader with the LEB128 strings used by the format."""

    def __init__(self, data: bytes) -> None:
        self.data = data
        self.offset = 0

    def i32(self) -> int:
        value = struct.unpack_from("<i", self.data, self.offset)[0]
        self.offset += 4
        return value

    def u8(self) -> int:
        value = self.data[self.offset]
        self.offset += 1
        return value

    def i8(self) -> int:
        value = struct.unpack_from("<b", self.data, self.offset)[0]
        self.offset += 1
        return value

    def u64(self) -> int:
        value = struct.unpack_from("<Q", self.data, self.offset)[0]
        self.offset += 8
        return value

    def string(self) -> str:
        length = 0
        shift = 0
        while True:
            byte = self.u8()
            length |= (byte & 0x7F) << shift
            shift += 7
            if not byte & 0x80:
                break
        value = self.data[self.offset:self.offset + length].decode()
        self.offset += length
        return value


def read_puzzle(path: Path) -> dict:
    """Parse a puzzle file: name, permission bits, molecules, multiplier, production flag."""
    reader = _Reader(Path(path).read_bytes())
    reader.i32()
    name = reader.string()
    reader.u64()
    permissions = reader.u64()

    def molecules() -> list[dict]:
        result = []
        for _ in range(reader.i32()):
            atoms = [(reader.u8(), reader.i8(), reader.i8()) for _ in range(reader.i32())]
            bonds = []
            for _ in range(reader.i32()):
                bits = reader.u8()
                bonds.append((bits, (reader.i8(), reader.i8()), (reader.i8(), reader.i8())))
            result.append({"atoms": atoms, "bonds": bonds})
        return result

    inputs = molecules()
    outputs = molecules()
    multiplier = reader.i32()
    production = reader.u8()
    return {"name": name, "permissions": permissions, "inputs": inputs, "outputs": outputs,
            "multiplier": multiplier, "production": bool(production)}


def read_solution(path: Path) -> tuple[list[tuple[int, int]], list[dict]]:
    """Parse a ``.solution`` file into ``(metrics, objects)``.

    ``metrics`` is the raw ``(id, value)`` list from the header; use
    :func:`read_slots` for the verified per-slot numbers instead.
    """
    reader = _Reader(Path(path).read_bytes())
    if reader.i32() != 7:
        raise ToolError(f"unexpected solution version in {path}")
    reader.string()
    reader.string()
    metrics = [(reader.i32(), reader.i32()) for _ in range(reader.i32())]
    objects = []
    for _ in range(reader.i32()):
        type_name = reader.string()
        reader.u8()
        q = reader.i32()
        r = reader.i32()
        header = [reader.i32() for _ in range(4)]
        instructions = []
        for _ in range(header[3]):
            parameter = reader.i32()
            action = reader.u8()
            instructions.append((chr(action), parameter))
        cells = None
        if type_name == "track":
            cells = [(reader.i32(), reader.i32()) for _ in range(reader.i32())]
            reader.i32()
        else:
            reader.i32()
            if type_name == "pipe":
                reader.i32()
                [(reader.i32(), reader.i32()) for _ in range(reader.i32())]
        objects.append({"type": type_name, "position": (q, r), "header": header,
                        "instructions": instructions, "cells": cells})
    return metrics, objects


def read_slots(objectives_path: Path) -> dict[str, dict]:
    """Read ``<solution>.objectives.json`` into ``{slot: verification metrics}``."""
    data = json.loads(Path(objectives_path).read_text())
    return {slot: entry["verification"]["metrics"] for slot, entry in data["slots"].items()}


def solution_slots(solution: Path) -> dict[str, dict] | None:
    """Slots of a solver run, or ``None`` when the run produced nothing."""
    objectives = Path(str(solution) + ".objectives.json")
    return read_slots(objectives) if objectives.exists() else None


# ── binary wrappers ──────────────────────────────────────────────────────────


def run_solver(puzzle: Path, out: Path, strategy: str = "assembly",
               env: dict | None = None) -> dict[str, dict] | None:
    """Run the solver CLI and return the objective slots (``None`` when missing)."""
    require(SOLVER, "solver binary")
    merged = dict(os.environ)
    merged.update(env or {})
    subprocess.run([str(SOLVER), "-p", str(puzzle), "-o", str(out), "-s", strategy],
                   cwd=REPO, env=merged, capture_output=True, text=True)
    return solution_slots(out)


def verify(puzzle: Path, solution: Path) -> dict:
    """Judge one solution machine with the native ``legacy-gate`` authority."""
    require(SOLVER, "solver binary")
    proc = subprocess.run([str(SOLVER), "verify", "--puzzle", str(puzzle),
                           "--solution", str(solution), "--authority", "legacy-gate", "--json"],
                          cwd=REPO, capture_output=True, text=True)
    for line in reversed(proc.stdout.splitlines()):
        line = line.strip()
        if line.startswith("{"):
            try:
                return json.loads(line)
            except json.JSONDecodeError:
                continue
    return {"status": "unparseable", "detail": (proc.stdout + proc.stderr)[-400:]}


def run_layout_probe(cases: Iterable[tuple[int, Path, Path]], env: dict | None = None) -> str:
    """Compile scheduled-row machines without a solver run.

    ``cases`` holds ``(intake_mode, puzzle, output)`` triples; ``intake_mode`` 1
    forces the local intake, 0 forces the shared serpentine feeder. Returns the
    probe log (one ``LAYOUT <idx> <path> <hash>`` line per compiled machine).
    """
    require(TESTS, "test binary")
    work = RESULT_ROOT / "probe"
    work.mkdir(parents=True, exist_ok=True)
    case_file = work / "cases.txt"
    case_file.write_text("\n".join(f"{mode} {puzzle} {out}" for mode, puzzle, out in cases) + "\n")
    merged = dict(os.environ)
    merged.update(env or {})
    merged["OPUS_LAYOUT_CASES"] = str(case_file)
    proc = subprocess.run([str(TESTS), "--gtest_filter=CompactLayout.RegisteredLayoutProbe"],
                          cwd=REPO, env=merged, capture_output=True, text=True)
    return proc.stdout + proc.stderr


def slot_wins(slots: dict[str, dict], reference: dict[str, dict]) -> dict[str, int]:
    """Which objective slots beat the reference (strictly better, per slot)."""
    return {key: int(slots[key][key] < reference[key][key]) for key in SLOT_KEYS}
