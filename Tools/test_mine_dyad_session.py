#!/usr/bin/env python3
"""Fixture test for Tools/mine_dyad_session.py (Phase 5 gate).

Builds two synthetic per-seat session folders (seat A still at the pawn spot; seat B
nodding gently in its own space, clock offset +5000 ms) and asserts the miner emits
every scoreboard column with sane values: distance ~ the 2.5 m face-to-face layout,
both seats square-on (orientation ~0 deg, gaze proportion ~1), B's movement energy
above A's, and a synchrony number present.

Run: python Tools/test_mine_dyad_session.py   (exit 0 = pass)
"""
from __future__ import annotations

import json
import math
import subprocess
import sys
import tempfile
from pathlib import Path

PROJECT_ROOT = Path(__file__).resolve().parent.parent
MINER = PROJECT_ROOT / "Tools" / "mine_dyad_session.py"

IDENTITY_QUAT = [0.0, 0.0, 0.0, 1.0]           # forward +X
FACE_PLUS_Y_QUAT = [0.0, 0.0, 0.7071, 0.7071]  # yaw +90: forward +Y


def write_seat(folder: Path, seat: str, offset_ms: float, rows: list[dict], hello_offset: float | None):
    folder.mkdir(parents=True, exist_ok=True)
    (folder / "session.json").write_text(json.dumps({
        "sessionId": f"fixture_seat{seat}", "seat": seat, "conditionTag": "fixture",
    }) + "\n", encoding="utf-8")
    events = []
    if hello_offset is not None:
        events.append({"tMonoS": 0.0, "kind": "wire_hello",
                       "detail": f"peerSeat=X offsetMs={hello_offset:.1f} rttMs=1.0"})
    (folder / "events.jsonl").write_text(
        "".join(json.dumps(event) + "\n" for event in events), encoding="utf-8")
    (folder / "control.jsonl").write_text("", encoding="utf-8")
    (folder / "rows_inbound.jsonl").write_text("", encoding="utf-8")
    (folder / "rows_outbound.jsonl").write_text(
        "".join(json.dumps(row, separators=(",", ":")) + "\n" for row in rows), encoding="utf-8")


def make_row(t_mono_ms: float, loc: list[float], quat: list[float]) -> dict:
    return {"type": "ROW", "seq": 0, "tMonoMs": t_mono_ms,
            "payload": {"t": 0.0, "fusion": {"source": {"hmd": {
                "has_pose": True, "loc": loc, "quat": quat,
                "tracking_up": [0.0, 0.0, 1.0]}}}}}


def main() -> int:
    failures: list[str] = []
    with tempfile.TemporaryDirectory() as temp_dir:
        base = Path(temp_dir)
        # Seat A: still at the pawn spot, facing +Y (toward the partner spot).
        rows_a = [make_row(1000.0 + index * 33.3, [0.0, -170.0, 165.0], FACE_PLUS_Y_QUAT)
                  for index in range(600)]
        # Seat B (own clock +5000ms): at ITS pawn spot facing +Y, nodding fore-aft; the
        # room mapping turns it 180 onto the partner spot facing seat A.
        rows_b = [make_row(6000.0 + index * 33.3,
                           [0.0, -170.0 + 3.0 * math.sin(index / 9.0), 168.0],
                           FACE_PLUS_Y_QUAT)
                  for index in range(600)]
        write_seat(base / "seatA", "A", 0.0, rows_a, hello_offset=5000.0)
        write_seat(base / "seatB", "B", 5000.0, rows_b, hello_offset=None)

        out_dir = base / "merged"
        result = subprocess.run(
            [sys.executable, str(MINER), str(base / "seatA"), str(base / "seatB"),
             "--out", str(out_dir)],
            capture_output=True, text=True, timeout=120)
        if result.returncode != 0:
            print(f"FAIL: miner exit {result.returncode}\n{result.stdout}\n{result.stderr}")
            return 1
        scoreboard = json.loads((out_dir / "scoreboard.json").read_text(encoding="utf-8"))

    def check(condition: bool, label: str):
        if not condition:
            failures.append(label)

    check((out_dir / "scoreboard.md").name == "scoreboard.md", "md emitted")
    check(abs(scoreboard["clockOffsetMs"] - 5000.0) < 1.0, "clock offset used")
    check(scoreboard["overlapSeconds"] > 15.0, "overlap window found")
    distance = scoreboard["interpersonal_distance_cm"]
    check(distance["mean"] is not None and 240.0 < distance["mean"] < 260.0,
          f"distance ~250cm (got {distance['mean']})")
    orientation = scoreboard["body_orientation_deg"]
    check(orientation["seatA"]["mean"] is not None and orientation["seatA"]["mean"] < 10.0,
          f"seat A square on (got {orientation['seatA']['mean']})")
    check(orientation["seatB"]["mean"] is not None and orientation["seatB"]["mean"] < 10.0,
          f"seat B square on (got {orientation['seatB']['mean']})")
    gaze = scoreboard["gaze_at_partner_proportion"]
    check(gaze["seatA"] is not None and gaze["seatA"] > 0.9, "seat A gazes at partner")
    check(gaze["seatB"] is not None and gaze["seatB"] > 0.9, "seat B gazes at partner")
    energy = scoreboard["movement_energy_cm_s"]
    check(energy["seatB"]["mean"] is not None and energy["seatB"]["mean"] > energy["seatA"]["mean"],
          "B moves more than A")
    check("peak_r" in scoreboard["motion_synchrony"], "synchrony column present")

    if failures:
        print("FAIL:")
        for failure in failures:
            print(f"  - {failure}")
        return 1
    print("PASS: scoreboard complete "
          f"(distance mean {distance['mean']:.1f} cm, overlap {scoreboard['overlapSeconds']:.1f}s)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
