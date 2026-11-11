"""Minimal reader for Opus Optima `.solution` files.

Mirrors `src/IO/BinaryParser.cpp::ParseSolution` so analysis scripts can inspect
the emitted machines (object inventory, per-object tape length, period) without
going through the solver.
"""

from __future__ import annotations

import struct
from dataclasses import dataclass, field
from pathlib import Path


class _Reader:
    def __init__(self, data: bytes) -> None:
        self.data = data
        self.pos = 0

    def u8(self) -> int:
        value = self.data[self.pos]
        self.pos += 1
        return value

    def i32(self) -> int:
        value = struct.unpack_from("<i", self.data, self.pos)[0]
        self.pos += 4
        return value

    def varint(self) -> int:
        result = 0
        shift = 0
        for _ in range(5):
            byte = self.u8()
            result |= (byte & 0x7F) << shift
            if not byte & 0x80:
                return result
            shift += 7
        raise ValueError("varint too long")

    def string(self) -> str:
        length = self.varint()
        value = self.data[self.pos : self.pos + length].decode("utf-8", "replace")
        self.pos += length
        return value


@dataclass
class Obj:
    kind: str
    q: int
    r: int
    body: list[int]
    tape: list[tuple[str, int]] = field(default_factory=list)

    @property
    def period(self) -> int:
        return max((tick for _, tick in self.tape), default=-1) + 1


@dataclass
class Machine:
    puzzle: str
    metrics: dict[str, int]
    objects: list[Obj]


def read(path: Path) -> Machine:
    reader = _Reader(Path(path).read_bytes())
    if reader.i32() != 7:
        raise ValueError("not a .solution file")
    puzzle = reader.string()
    reader.string()  # solution name
    metrics: dict[str, int] = {}
    names = {0: "cycles", 1: "cost", 2: "area", 3: "instructions"}
    for _ in range(reader.i32()):
        key = reader.i32()
        metrics[names.get(key, str(key))] = reader.i32()
    objects: list[Obj] = []
    for _ in range(reader.i32()):
        kind = reader.string()
        reader.u8()  # marker
        q, r = reader.i32(), reader.i32()
        h0, h1, h2, h3 = reader.i32(), reader.i32(), reader.i32(), reader.i32()
        tape = []
        for _ in range(h3):
            param = reader.i32()
            tape.append((chr(reader.u8()), param))
        body = [h0, h1, h2, h3]
        if kind == "track":
            segments = reader.i32()
            body.append(segments)
            for _ in range(segments):
                body.append(reader.i32())
                body.append(reader.i32())
            body.append(reader.i32())
        else:
            body.append(reader.i32())
            if kind == "pipe":
                body.append(reader.i32())
                segments = reader.i32()
                body.append(segments)
                for _ in range(segments):
                    body.append(reader.i32())
                    body.append(reader.i32())
        objects.append(Obj(kind, q, r, body, tape))
    return Machine(puzzle, metrics, objects)


def summary(machine: Machine) -> str:
    counts: dict[str, int] = {}
    for obj in machine.objects:
        counts[obj.kind] = counts.get(obj.kind, 0) + 1
    hexes = sum(
        obj.body[4] for obj in machine.objects if obj.kind == "track" and len(obj.body) > 4
    )
    period = max((obj.period for obj in machine.objects), default=0)
    inventory = " ".join(f"{k}={v}" for k, v in sorted(counts.items()))
    return f"{inventory} | hex={hexes} | period={period} | {machine.metrics}"


def worst_tapes(machine: Machine, limit: int) -> list[tuple[str, int, int]]:
    rows = [(obj.kind, obj.period, len(obj.tape)) for obj in machine.objects]
    rows.sort(key=lambda row: -row[1])
    return rows[:limit]
