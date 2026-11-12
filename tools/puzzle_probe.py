"""Minimal reader for Opus Magnum `.puzzle` files plus the derived atom-supply view.

Mirrors `src/IO/BinaryParser.cpp::ParsePuzzle` so analysis scripts can ask two
questions that decide whether an arm1 network is even reachable:

* does every product atom have a single-atom source (the composer's requirement)?
* does a multi-atom input hold a matching freed atom (a relay could supply it)?
"""

from __future__ import annotations

import struct
from dataclasses import dataclass, field
from pathlib import Path

ATOM_NAMES = [
    "Salt", "Air", "Earth", "Fire", "Water", "Quicksilver", "Gold", "Silver",
    "Copper", "Iron", "Tin", "Lead", "Vitae", "Mors", "Repeat", "Quintessence",
]


class _Reader:
    def __init__(self, data: bytes) -> None:
        self.data = data
        self.pos = 0

    def u8(self) -> int:
        value = self.data[self.pos]
        self.pos += 1
        return value

    def i8(self) -> int:
        value = struct.unpack_from("<b", self.data, self.pos)[0]
        self.pos += 1
        return value

    def u64(self) -> int:
        value = struct.unpack_from("<Q", self.data, self.pos)[0]
        self.pos += 8
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

    def coord(self) -> tuple[int, int]:
        return self.i8(), self.i8()


@dataclass
class Molecule:
    atoms: list[tuple[str, tuple[int, int]]] = field(default_factory=list)
    bonds: list[tuple[int, tuple[int, int], tuple[int, int]]] = field(default_factory=list)


@dataclass
class Puzzle:
    name: str
    permissions: int
    inputs: list[Molecule]
    outputs: list[Molecule]
    multiplier: int
    production: bool


def _molecule(reader: _Reader) -> Molecule:
    molecule = Molecule()
    for _ in range(reader.i32()):
        atom_id = reader.u8()
        if not 1 <= atom_id <= len(ATOM_NAMES):
            raise ValueError(f"bad atom id {atom_id}")
        molecule.atoms.append((ATOM_NAMES[atom_id - 1], reader.coord()))
    for _ in range(reader.i32()):
        bits = reader.u8()
        start = reader.coord()
        end = reader.coord()
        molecule.bonds.append((bits, start, end))
    return molecule


def read(path: Path) -> Puzzle:
    reader = _Reader(Path(path).read_bytes())
    version = reader.i32()
    if version != 3:
        raise ValueError(f"unsupported puzzle version {version}")
    name = reader.string()
    reader.u64()  # creator steam id
    permissions = reader.u64()
    inputs = [_molecule(reader) for _ in range(reader.i32())]
    outputs = [_molecule(reader) for _ in range(reader.i32())]
    multiplier = reader.i32()
    production = reader.u8() != 0
    return Puzzle(name, permissions, inputs, outputs, multiplier, production)


def singleton_sources(puzzle: Puzzle) -> set[str]:
    """Elements the puzzle hands over as a ready single-atom port."""
    return {
        molecule.atoms[0][0]
        for molecule in puzzle.inputs
        if len(molecule.atoms) == 1 and not molecule.bonds
    }


def freed_elements(puzzle: Puzzle) -> set[str]:
    """Elements that only exist inside a multi-atom input, i.e. behind an unbond."""
    return {
        atom[0]
        for molecule in puzzle.inputs
        if len(molecule.atoms) > 1 or molecule.bonds
        for atom in molecule.atoms
    }


def describe(puzzle: Puzzle) -> dict:
    singleton = singleton_sources(puzzle)
    freed = freed_elements(puzzle)
    product = [atom[0] for output in puzzle.outputs for atom in output.atoms]
    return {
        "name": puzzle.name,
        "inputs": len(puzzle.inputs),
        "outputs": len(puzzle.outputs),
        "multiplier": puzzle.multiplier,
        "production": puzzle.production,
        "atoms": len(product),
        "bonds": sum(len(output.bonds) for output in puzzle.outputs),
        # Every product element either has a singleton port (composer domain) or
        # only appears inside a molecule (relay domain).
        "missing_singleton": sorted(set(product) - singleton),
        "relayable": sorted((set(product) - singleton) & freed),
        "pure_molecular_input": all(len(m.atoms) > 1 for m in puzzle.inputs),
    }
