#!/usr/bin/env python3
"""Merge two dyad session folders and emit the behavioral scoreboard (Phase 5).

Inputs: two per-seat session folders (Saved/DyadStudy/<SessionId>/ shape: session.json,
events.jsonl, control.jsonl, rows_outbound.jsonl, rows_inbound.jsonl). Each seat's OWN
motion is its rows_outbound stream; the clocks merge on the HELLO offset recorded in
either seat's events (offsetMs: peer_mono ~ local_mono + offset).

Room frame: seat A's tracking space is the room (pawn spot (0,-170) facing +Y). Seat B's
space maps through the face-to-face partner transform: p' = (0,80) - (p - (0,-170))
in XY (180-degree turn about Z), Z unchanged — so both bodies stand ~2.5 m apart facing
each other, matching L_DyadInteraction_01.

Scoreboard (the objective measures the study design leans on):
  - interpersonal_distance_cm: head-to-head distance over time (mean/p50/p90)
  - body_orientation_deg: each seat's facing-away-from-partner angle (0 = square on)
  - gaze_at_partner_proportion: fraction of time head forward is within 25 deg of the
    direction to the partner's head
  - movement_energy_cm_s: smoothed head speed per seat
  - motion_synchrony: peak lagged Pearson r between the two movement-energy series
    (lags +-2 s), plus the lag at the peak

Usage:
  python Tools/mine_dyad_session.py <seatA_dir> <seatB_dir> [--out <dir>]
Writes <out>/scoreboard.json and <out>/scoreboard.md (default: <seatA_dir>/../merged_<A>_<B>/).
"""
from __future__ import annotations

import argparse
import json
import math
import sys
from pathlib import Path

SAMPLE_HZ = 10.0
GAZE_THRESHOLD_DEG = 25.0
SYNC_MAX_LAG_S = 2.0
PAWN_SPOT = (0.0, -170.0)
PARTNER_SPOT = (0.0, 80.0)


def read_jsonl(path: Path) -> list[dict]:
    rows = []
    if not path.exists():
        return rows
    with path.open(encoding="utf-8") as handle:
        for line in handle:
            line = line.strip()
            if not line:
                continue
            try:
                rows.append(json.loads(line))
            except json.JSONDecodeError:
                continue
    return rows


def quat_forward(quat: list[float]) -> tuple[float, float, float]:
    """UE quat [x,y,z,w] -> world forward (+X rotated)."""
    x, y, z, w = quat
    return (
        1.0 - 2.0 * (y * y + z * z),
        2.0 * (x * y + z * w),
        2.0 * (x * z - y * w),
    )


def load_track(session_dir: Path) -> dict:
    """Extracts a head track [(tMonoS, pos, fwd)] from a seat's outbound rows."""
    track = []
    for row in read_jsonl(session_dir / "rows_outbound.jsonl"):
        payload = row.get("payload", {})
        source = payload.get("fusion", {}).get("source", {})
        hmd = source.get("hmd", {})
        if not hmd.get("has_pose"):
            continue
        loc = hmd.get("loc")
        quat = hmd.get("quat")
        if not loc or not quat:
            continue
        track.append((row.get("tMonoMs", 0.0) / 1000.0, tuple(loc), quat_forward(quat)))
    session = {}
    session_path = session_dir / "session.json"
    if session_path.exists():
        session = json.loads(session_path.read_text(encoding="utf-8").splitlines()[0])
    offset_ms = None
    for event in read_jsonl(session_dir / "events.jsonl"):
        if event.get("kind") == "wire_hello":
            detail = event.get("detail", "")
            for token in detail.split():
                if token.startswith("offsetMs="):
                    offset_ms = float(token.split("=", 1)[1])
    return {"seat": session.get("seat", "?"), "sessionId": session.get("sessionId", "?"),
            "track": track, "helloOffsetMs": offset_ms}


def map_seat_b_to_room(pos: tuple, fwd: tuple) -> tuple[tuple, tuple]:
    px = PARTNER_SPOT[0] - (pos[0] - PAWN_SPOT[0])
    py = PARTNER_SPOT[1] - (pos[1] - PAWN_SPOT[1])
    return (px, py, pos[2]), (-fwd[0], -fwd[1], fwd[2])


def resample(track: list, t0: float, t1: float) -> list:
    """Nearest-earlier sample at SAMPLE_HZ over [t0, t1]."""
    out = []
    if not track:
        return out
    index = 0
    steps = int((t1 - t0) * SAMPLE_HZ)
    for step in range(max(0, steps)):
        t = t0 + step / SAMPLE_HZ
        while index + 1 < len(track) and track[index + 1][0] <= t:
            index += 1
        out.append((t, track[index][1], track[index][2]))
    return out


def stats(values: list[float]) -> dict:
    if not values:
        return {"n": 0, "mean": None, "p50": None, "p90": None}
    ordered = sorted(values)
    return {
        "n": len(values),
        "mean": sum(values) / len(values),
        "p50": ordered[len(ordered) // 2],
        "p90": ordered[min(len(ordered) - 1, int(0.9 * (len(ordered) - 1)))],
    }


def angle_between_deg(v1: tuple, v2: tuple) -> float:
    dot = sum(a * b for a, b in zip(v1, v2))
    n1 = math.sqrt(sum(a * a for a in v1))
    n2 = math.sqrt(sum(a * a for a in v2))
    if n1 < 1e-6 or n2 < 1e-6:
        return 180.0
    return math.degrees(math.acos(max(-1.0, min(1.0, dot / (n1 * n2)))))


def movement_energy(samples: list) -> list[float]:
    energy = [0.0]
    for previous, current in zip(samples, samples[1:]):
        dt = current[0] - previous[0]
        dist = math.dist(current[1], previous[1])
        energy.append(dist / dt if dt > 1e-6 else 0.0)
    # light 3-tap smoothing
    smoothed = []
    for index in range(len(energy)):
        window = energy[max(0, index - 1):index + 2]
        smoothed.append(sum(window) / len(window))
    return smoothed


def pearson(a: list[float], b: list[float]) -> float:
    n = min(len(a), len(b))
    if n < 8:
        return float("nan")
    a, b = a[:n], b[:n]
    ma, mb = sum(a) / n, sum(b) / n
    va = sum((x - ma) ** 2 for x in a)
    vb = sum((x - mb) ** 2 for x in b)
    if va < 1e-9 or vb < 1e-9:
        return float("nan")
    cov = sum((x - ma) * (y - mb) for x, y in zip(a, b))
    return cov / math.sqrt(va * vb)


def lagged_synchrony(energy_a: list[float], energy_b: list[float]) -> dict:
    best_r, best_lag = None, None
    max_lag_steps = int(SYNC_MAX_LAG_S * SAMPLE_HZ)
    for lag in range(-max_lag_steps, max_lag_steps + 1):
        if lag >= 0:
            r = pearson(energy_a[lag:], energy_b[:len(energy_b) - lag if lag else None])
        else:
            r = pearson(energy_a[:lag], energy_b[-lag:])
        if not math.isnan(r) and (best_r is None or r > best_r):
            best_r, best_lag = r, lag / SAMPLE_HZ
    return {"peak_r": best_r, "lag_s": best_lag}


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("seat_a_dir", type=Path)
    parser.add_argument("seat_b_dir", type=Path)
    parser.add_argument("--out", type=Path, default=None)
    args = parser.parse_args()

    seat_a = load_track(args.seat_a_dir)
    seat_b = load_track(args.seat_b_dir)
    if not seat_a["track"]:
        print(f"seat A has no usable rows ({args.seat_a_dir})")
        return 1
    if not seat_b["track"]:
        print(f"seat B has no usable rows ({args.seat_b_dir})")
        return 1

    # Clock merge: A's hello offset maps B's monotonic clock into A's timeline.
    offset_ms = seat_a["helloOffsetMs"]
    source = "seatA wire_hello"
    if offset_ms is None and seat_b["helloOffsetMs"] is not None:
        offset_ms = -seat_b["helloOffsetMs"]
        source = "seatB wire_hello (negated)"
    if offset_ms is None:
        print("no wire_hello clock offset in either events.jsonl")
        return 1
    track_b = [(t - offset_ms / 1000.0, pos, fwd) for (t, pos, fwd) in seat_b["track"]]

    t0 = max(seat_a["track"][0][0], track_b[0][0])
    t1 = min(seat_a["track"][-1][0], track_b[-1][0])
    if t1 - t0 < 3.0:
        print(f"overlap too short after clock merge ({t1 - t0:.1f}s)")
        return 1

    samples_a = resample(seat_a["track"], t0, t1)
    samples_b = [
        (t, *map_seat_b_to_room(pos, fwd)) for (t, pos, fwd) in resample(track_b, t0, t1)
    ]

    distances, orient_a, orient_b, gaze_a, gaze_b = [], [], [], [], []
    for (ta, pa, fa), (tb, pb, fb) in zip(samples_a, samples_b):
        distances.append(math.dist(pa, pb))
        to_b = (pb[0] - pa[0], pb[1] - pa[1], pb[2] - pa[2])
        to_a = (-to_b[0], -to_b[1], -to_b[2])
        angle_a = angle_between_deg(fa, to_b)
        angle_b = angle_between_deg(fb, to_a)
        orient_a.append(angle_a)
        orient_b.append(angle_b)
        gaze_a.append(1.0 if angle_a < GAZE_THRESHOLD_DEG else 0.0)
        gaze_b.append(1.0 if angle_b < GAZE_THRESHOLD_DEG else 0.0)

    energy_a = movement_energy(samples_a)
    energy_b = movement_energy(samples_b)

    scoreboard = {
        "seatA": {"sessionId": seat_a["sessionId"], "rows": len(seat_a["track"])},
        "seatB": {"sessionId": seat_b["sessionId"], "rows": len(seat_b["track"])},
        "clockOffsetMs": offset_ms,
        "clockOffsetSource": source,
        "overlapSeconds": t1 - t0,
        "interpersonal_distance_cm": stats(distances),
        "body_orientation_deg": {"seatA": stats(orient_a), "seatB": stats(orient_b)},
        "gaze_at_partner_proportion": {
            "seatA": sum(gaze_a) / len(gaze_a) if gaze_a else None,
            "seatB": sum(gaze_b) / len(gaze_b) if gaze_b else None,
            "thresholdDeg": GAZE_THRESHOLD_DEG,
        },
        "movement_energy_cm_s": {"seatA": stats(energy_a), "seatB": stats(energy_b)},
        "motion_synchrony": lagged_synchrony(energy_a, energy_b),
    }

    out_dir = args.out or (args.seat_a_dir.parent /
                           f"merged_{args.seat_a_dir.name}_{args.seat_b_dir.name}")
    out_dir.mkdir(parents=True, exist_ok=True)
    (out_dir / "scoreboard.json").write_text(
        json.dumps(scoreboard, indent=2) + "\n", encoding="utf-8")

    def fmt(value, unit=""):
        return "n/a" if value is None else f"{value:.1f}{unit}"

    md = [
        f"# Dyad scoreboard: {seat_a['sessionId']} x {seat_b['sessionId']}",
        "",
        f"- overlap: {t1 - t0:.1f}s at {SAMPLE_HZ:.0f} Hz (clock offset {offset_ms:.1f} ms, {source})",
        f"- interpersonal distance: mean {fmt(scoreboard['interpersonal_distance_cm']['mean'], ' cm')}"
        f" (p50 {fmt(scoreboard['interpersonal_distance_cm']['p50'], ' cm')},"
        f" p90 {fmt(scoreboard['interpersonal_distance_cm']['p90'], ' cm')})",
        f"- body orientation (deg off partner): A mean {fmt(scoreboard['body_orientation_deg']['seatA']['mean'])},"
        f" B mean {fmt(scoreboard['body_orientation_deg']['seatB']['mean'])}",
        f"- gaze-at-partner proportion (<{GAZE_THRESHOLD_DEG:.0f} deg): A"
        f" {fmt(100 * scoreboard['gaze_at_partner_proportion']['seatA'] if scoreboard['gaze_at_partner_proportion']['seatA'] is not None else None, '%')},"
        f" B {fmt(100 * scoreboard['gaze_at_partner_proportion']['seatB'] if scoreboard['gaze_at_partner_proportion']['seatB'] is not None else None, '%')}",
        f"- movement energy: A mean {fmt(scoreboard['movement_energy_cm_s']['seatA']['mean'], ' cm/s')},"
        f" B mean {fmt(scoreboard['movement_energy_cm_s']['seatB']['mean'], ' cm/s')}",
        f"- motion synchrony: peak r {scoreboard['motion_synchrony']['peak_r'] if scoreboard['motion_synchrony']['peak_r'] is not None else 'n/a'}"
        f" at lag {fmt(scoreboard['motion_synchrony']['lag_s'], ' s')}",
        "",
    ]
    (out_dir / "scoreboard.md").write_text("\n".join(md), encoding="utf-8")
    print(f"scoreboard -> {out_dir}")
    print("\n".join(md))
    return 0


if __name__ == "__main__":
    sys.exit(main())
