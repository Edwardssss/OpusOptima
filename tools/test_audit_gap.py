"""Offline audit statistics regressions; never invoke the native solver."""

import io
import itertools
import json
import re
import sys
import tempfile
import unittest
from contextlib import redirect_stdout
from pathlib import Path
from unittest.mock import patch

import audit_gap
import opsol


def make_row(values=(20, 20, 20), oracle=(10, 10, 10), status="ok"):
    # Deliberately misleading cross-slot metrics catch accidental min/diagonal mixups.
    return {"sha": "synthetic", "class": "test", "status": status,
            "slots": {key: {**dict.fromkeys(opsol.SLOT_KEYS, 0), key: value}
                      for key, value in zip(opsol.SLOT_KEYS, values)},
            "best": {key: {**dict.fromkeys(opsol.SLOT_KEYS, 1000), key: value}
                     for key, value in zip(opsol.SLOT_KEYS, oracle)}}


class AuditGapTest(unittest.TestCase):
    def report(self, rows):
        with tempfile.TemporaryDirectory() as out, \
                patch.object(opsol, "load_oracle", return_value=rows), \
                patch.object(audit_gap, "run_one", side_effect=lambda row, _: row), \
                patch.object(opsol, "run_solver", side_effect=AssertionError("no solver")), \
                patch.object(sys, "argv", ["audit_gap.py", "--out", out]), \
                redirect_stdout(io.StringIO()) as output:
            self.assertEqual(audit_gap.main(), 0)
        text = output.getvalue()
        slots = {key: tuple(map(int, counts)) for key, *counts in
                 re.findall(r"(cost|cycles|area)\s+win\s+(\d+)\s+tie\s+(\d+)\s+lose\s+(\d+)", text)}
        allwin = int(re.search(r"winning every slot:\s+(\d+)", text)[1])
        alllose = int(re.search(r"losing every slot:\s+(\d+)", text)[1])
        return slots, allwin, alllose, text

    def test_all_win_tie_loss_combinations_and_slot_orders(self):
        for values in itertools.product((5, 10, 20), repeat=3):
            for keys in itertools.permutations(opsol.SLOT_KEYS):
                with self.subTest(values=values, keys=keys), patch.object(opsol, "SLOT_KEYS", keys):
                    # Keep the underlying objective data fixed while changing iteration order.
                    row = {"sha": "test", "class": "test", "status": "ok",
                           "slots": {k: {k: v} for k, v in zip(("cost", "cycles", "area"), values)},
                           "best": {k: {k: 10} for k in keys}}
                    slots, win, lose, _ = self.report([row])
                    self.assertEqual(win, int(all(v < 10 for v in values)))
                    self.assertEqual(lose, int(all(v > 10 for v in values)))
                    for key, value in zip(("cost", "cycles", "area"), values):
                        self.assertEqual(slots[key], (int(value < 10), int(value == 10), int(value > 10)))

    def test_uses_each_objectives_own_slot(self):
        slots, win, lose, _ = self.report([make_row((5, 20, 20))])
        self.assertEqual((win, lose), (0, 0))
        self.assertEqual(slots, {"cost": (1, 0, 0), "cycles": (0, 0, 1), "area": (0, 0, 1)})

    def test_partial_slots_and_missing_metrics(self):
        for side in ("slots", "best"):
            for key in opsol.SLOT_KEYS:
                for missing_value in ("absent", None, {}, {key: None}, {"unrelated": 10}):
                    with self.subTest(side=side, key=key, missing_value=missing_value):
                        row = make_row()
                        if missing_value == "absent":
                            del row[side][key]
                        else:
                            row[side][key] = missing_value
                        slots, win, lose, _ = self.report([row])
                        self.assertEqual((win, lose), (0, 0))
                        self.assertEqual(slots[key], (0, 0, 0))
                        self.assertEqual(sum(v[2] for v in slots.values()), 2)
                        self.assertNotIn(key, audit_gap.ratio_row(row))

    def test_partial_wins_are_not_all_win(self):
        row = make_row((5, 5, 5))
        del row["slots"]["area"]
        self.assertEqual(self.report([row])[1:3], (0, 0))

    def test_empty_and_missing_rows(self):
        for side in ("slots", "best"):
            for value in (None, {}):
                row = make_row()
                row[side] = value
                self.assertIsNone(audit_gap.ratio_row(row))
                self.assertEqual(self.report([row])[1:3], (0, 0))
        missing = make_row(status="missing")
        del missing["slots"]
        slots, win, lose, text = self.report([missing])
        self.assertEqual((win, lose), (0, 0))
        self.assertTrue(all(v == (0, 0, 0) for v in slots.values()))
        self.assertIn("produced 0/1  missing 1", text)
        self.assertEqual(self.report([])[1:3], (0, 0))

    def test_zero_oracle_has_counts_but_no_ratio(self):
        row = make_row((0, 1, 0), (0, 0, 10))
        slots, win, lose, text = self.report([row])
        self.assertEqual(slots, {"cost": (0, 1, 0), "cycles": (0, 0, 1), "area": (1, 0, 0)})
        self.assertEqual((win, lose), (0, 0))
        self.assertEqual(audit_gap.ratio_row(row), {"area": 0.0})
        self.assertNotIn("synthetic", text)
        self.assertEqual(self.report([make_row((1, 1, 1), (0, 0, 0))])[1:3], (0, 1))

    def test_integer_comparison_does_not_round_to_tie(self):
        n = 2 ** 60
        slots, win, lose, _ = self.report([make_row((n + 1,) * 3, (n,) * 3)])
        self.assertEqual((win, lose), (0, 1))
        self.assertTrue(all(v == (0, 0, 1) for v in slots.values()))

    def test_archived_gap_offline(self):
        path = opsol.REPO / "result/gap-quota-stagger-20261115/gap.json"
        if not path.exists():
            self.skipTest("local historical gap fixture unavailable")
        before = path.read_bytes()
        rows = json.loads(before)
        slots, win, lose, text = self.report(rows)
        self.assertEqual((win, lose), (43, 84))
        self.assertEqual(slots, {"cost": (55, 0, 86), "cycles": (43, 0, 98), "area": (52, 0, 89)})
        self.assertIn("produced 141/146  missing 5", text)
        self.assertEqual(path.read_bytes(), before)


if __name__ == "__main__":
    unittest.main()