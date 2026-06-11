"""Compare two live_pie_bone_measure JSONs (baseline vs candidate) for GATE-PIE.

Usage:
    python compare_replay_measurements.py <baseline.json> <candidate.json> [--tol-knee 1.5]
        [--tol-ball 0.15] [--tol-drift 0.001]

Metrics (computed identically for both files):
  - knee_angle_l/r min/max (deg)
  - lowest-ball median/min (cm above traced floor): per-sample min(ball_l_z, ball_r_z)
  - penetration frames: samples whose lowest ball is below -0.05 cm (under the floor)
  - segment-length drift: max over leg segments (thigh->calf, calf->foot) of the
    (max - min) world distance across all samples, in cm

Exit code 0 = PASS within tolerances, 1 = FAIL.
"""

import argparse
import json
import math
import statistics
import sys


def _dist(a, b):
    return math.sqrt(sum((x - y) ** 2 for x, y in zip(a, b)))


def load_metrics(path):
    with open(path) as fh:
        data = json.load(fh)
    samples = data["samples"]
    ranges = data.get("ranges", {})

    lowest_ball = [
        min(s["ball_l_z"], s["ball_r_z"])
        for s in samples
        if s.get("ball_l_z") is not None and s.get("ball_r_z") is not None
    ]
    penetration = sum(1 for v in lowest_ball if v < -0.05)

    seg_pairs = [("thigh_l", "calf_l"), ("calf_l", "foot_l"),
                 ("thigh_r", "calf_r"), ("calf_r", "foot_r")]
    drift = 0.0
    for a, b in seg_pairs:
        lens = [
            _dist(s["bones"][a], s["bones"][b])
            for s in samples
            if a in s.get("bones", {}) and b in s.get("bones", {})
        ]
        if lens:
            drift = max(drift, max(lens) - min(lens))

    def rng(key):
        r = ranges.get(key)
        return (r["min"], r["max"]) if r else (None, None)

    return {
        "actor": data.get("actor"),
        "label": data.get("label"),
        "sample_count": data.get("sample_count"),
        "knee_l": rng("knee_angle_l"),
        "knee_r": rng("knee_angle_r"),
        "ball_median": statistics.median(lowest_ball) if lowest_ball else None,
        "ball_min": min(lowest_ball) if lowest_ball else None,
        "penetration_frames": penetration,
        "segment_drift_cm": drift,
    }


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("baseline")
    ap.add_argument("candidate")
    ap.add_argument("--tol-knee", type=float, default=1.5)
    ap.add_argument("--tol-ball", type=float, default=0.15)
    ap.add_argument("--tol-drift", type=float, default=0.001)
    args = ap.parse_args()

    base = load_metrics(args.baseline)
    cand = load_metrics(args.candidate)

    rows = []
    ok = True

    def check(name, bval, cval, tol):
        nonlocal ok
        if bval is None or cval is None:
            rows.append((name, bval, cval, "MISSING"))
            ok = False
            return
        delta = abs(cval - bval)
        passed = delta <= tol
        if not passed:
            ok = False
        rows.append((name, round(bval, 3), round(cval, 3), "ok" if passed else "FAIL d=%.3f" % delta))

    check("knee_l_min", base["knee_l"][0], cand["knee_l"][0], args.tol_knee)
    check("knee_l_max", base["knee_l"][1], cand["knee_l"][1], args.tol_knee)
    check("knee_r_min", base["knee_r"][0], cand["knee_r"][0], args.tol_knee)
    check("knee_r_max", base["knee_r"][1], cand["knee_r"][1], args.tol_knee)
    check("ball_median", base["ball_median"], cand["ball_median"], args.tol_ball)

    if cand["penetration_frames"] != 0:
        ok = False
    rows.append(("penetration_frames", base["penetration_frames"], cand["penetration_frames"],
                 "ok" if cand["penetration_frames"] == 0 else "FAIL"))
    if cand["segment_drift_cm"] > args.tol_drift:
        ok = False
    rows.append(("segment_drift_cm", round(base["segment_drift_cm"], 5),
                 round(cand["segment_drift_cm"], 5),
                 "ok" if cand["segment_drift_cm"] <= args.tol_drift else "FAIL"))
    rows.append(("sample_count", base["sample_count"], cand["sample_count"], "info"))

    print("actor: baseline=%s candidate=%s" % (base["actor"], cand["actor"]))
    print("%-22s %12s %12s  %s" % ("metric", "baseline", "candidate", "status"))
    for name, b, c, status in rows:
        print("%-22s %12s %12s  %s" % (name, b, c, status))
    print("RESULT: %s" % ("PASS" if ok else "FAIL"))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
