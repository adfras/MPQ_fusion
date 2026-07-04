"""Score a fused-replay measurement against the MHA offline solve.

Usage:
    python score_against_mha.py <mha_measure.json> <fused_measure.json>
        [--align-signal pelvis_z] [--window-seconds 5]

Both inputs use the live_pie_bone_measure schema (PIE sampler or
Tools/sample_anim_sequence.py). The two streams come from different clocks
and world frames, so:

1. TIME: alignment offset found by maximizing cross-correlation of the align
   signal (default: pelvis height, which both systems observe strongly).
2. SPACE: positions are compared PELVIS-RELATIVE (translation only) and
   joints as ANGLES, so differing world frames and root placement cancel.

Metrics per window (default 5 s) and overall:
- knee_angle_l/r RMSE + peak error (deg)         [lower body scaffold]
- elbow_angle_l/r RMSE + peak error (deg)        [arm chain]
- wrist height (hand z - pelvis z) RMSE + peak (cm)  [overhead class]
- pelvis height delta RMSE (cm)                  [squat depth class]

This is a survey/scoring tool, not a gate: MHA is a monocular reference,
not ground truth. Windows where BOTH candidates diverge from MHA in the
same direction deserve eyeball review against the video before any tuning
(see Docs/MHA_BODY_GROUNDTRUTH_WORKFLOW.md).
"""

import argparse
import json
import math
import sys


def _dist(a, b):
    return math.sqrt(sum((x - y) ** 2 for x, y in zip(a, b)))


def _angle(a, b, c):
    v1 = [a[i] - b[i] for i in range(3)]
    v2 = [c[i] - b[i] for i in range(3)]
    l1 = math.sqrt(sum(x * x for x in v1))
    l2 = math.sqrt(sum(x * x for x in v2))
    if l1 < 1e-4 or l2 < 1e-4:
        return None
    cosv = max(-1.0, min(1.0, sum(v1[i] * v2[i] for i in range(3)) / (l1 * l2)))
    return math.degrees(math.acos(cosv))


def load_series(path):
    with open(path) as fh:
        data = json.load(fh)
    rows = []
    for s in data["samples"]:
        bones = s.get("bones", {})
        pelvis = bones.get("pelvis")
        if not pelvis:
            continue
        row = {"t": s.get("wall_time", s.get("game_time", 0.0)), "pelvis_z": pelvis[2]}
        for side in ("l", "r"):
            for joint, (a, b, c) in {
                f"knee_{side}": (f"thigh_{side}", f"calf_{side}", f"foot_{side}"),
                f"elbow_{side}": (f"upperarm_{side}", f"lowerarm_{side}", f"hand_{side}"),
            }.items():
                if all(k in bones for k in (a, b, c)):
                    row[joint] = _angle(bones[a], bones[b], bones[c])
            hand = bones.get(f"hand_{side}")
            if hand:
                row[f"wrist_h_{side}"] = hand[2] - pelvis[2]
        rows.append(row)
    return data, rows


def resample(rows, hz):
    """Resample rows onto a uniform clock via nearest neighbour."""
    if not rows:
        return []
    t0, t1 = rows[0]["t"], rows[-1]["t"]
    out = []
    idx = 0
    t = t0
    while t <= t1:
        while idx + 1 < len(rows) and abs(rows[idx + 1]["t"] - t) <= abs(rows[idx]["t"] - t):
            idx += 1
        r = dict(rows[idx])
        r["t"] = t - t0
        out.append(r)
        t += 1.0 / hz
    return out


def best_offset(a, b, key, hz, max_shift_s=15.0):
    """Offset of b relative to a maximizing correlation of `key`."""
    sa = [r.get(key) for r in a]
    sb = [r.get(key) for r in b]
    max_shift = int(max_shift_s * hz)
    best = (0, -2.0)
    for shift in range(-max_shift, max_shift + 1):
        pairs = []
        for i in range(len(sa)):
            j = i + shift
            if 0 <= j < len(sb) and sa[i] is not None and sb[j] is not None:
                pairs.append((sa[i], sb[j]))
        if len(pairs) < int(5 * hz):
            continue
        xs, ys = zip(*pairs)
        mx, my = sum(xs) / len(xs), sum(ys) / len(ys)
        cov = sum((x - mx) * (y - my) for x, y in pairs)
        vx = math.sqrt(sum((x - mx) ** 2 for x in xs))
        vy = math.sqrt(sum((y - my) ** 2 for y in ys))
        if vx < 1e-6 or vy < 1e-6:
            continue
        r = cov / (vx * vy)
        if r > best[1]:
            best = (shift, r)
    return best


def score(a, b, shift, hz, window_s):
    keys = ["knee_l", "knee_r", "elbow_l", "elbow_r",
            "wrist_h_l", "wrist_h_r", "pelvis_z"]
    # pelvis_z compared as delta from its own median (squat depth, frame-free)
    med_a = sorted(r["pelvis_z"] for r in a)[len(a) // 2]
    med_b = sorted(r["pelvis_z"] for r in b)[len(b) // 2]

    windows = {}
    for i in range(len(a)):
        j = i + shift
        if not (0 <= j < len(b)):
            continue
        w = int(a[i]["t"] // window_s)
        bucket = windows.setdefault(w, {k: [] for k in keys})
        for k in keys:
            va, vb = a[i].get(k), b[j].get(k)
            if va is None or vb is None:
                continue
            if k == "pelvis_z":
                bucket[k].append(((va - med_a) - (vb - med_b)))
            else:
                bucket[k].append(va - vb)

    def rmse(v):
        return math.sqrt(sum(x * x for x in v) / len(v)) if v else None

    def peak(v):
        return max(abs(x) for x in v) if v else None

    print(f"{'window':>7} " + " ".join(f"{k:>12}" for k in keys) + "   (RMSE / peak)")
    overall = {k: [] for k in keys}
    for w in sorted(windows):
        cells = []
        for k in keys:
            v = windows[w][k]
            overall[k].extend(v)
            cells.append(f"{rmse(v):6.1f}/{peak(v):5.1f}" if v else "     -     ")
        print(f"{w * window_s:5.0f}s  " + " ".join(f"{c:>12}" for c in cells))
    print("-" * 110)
    cells = [f"{rmse(overall[k]):6.1f}/{peak(overall[k]):5.1f}" if overall[k] else "     -     " for k in keys]
    print(f"{'ALL':>6}  " + " ".join(f"{c:>12}" for c in cells))
    print("\nunits: knee/elbow deg; wrist_h/pelvis_z cm. peak = worst sample in window.")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("mha")
    ap.add_argument("fused")
    ap.add_argument("--align-signal", default="pelvis_z")
    ap.add_argument("--hz", type=float, default=30.0)
    ap.add_argument("--window-seconds", type=float, default=5.0)
    args = ap.parse_args()

    _, rows_a = load_series(args.mha)
    _, rows_b = load_series(args.fused)
    a = resample(rows_a, args.hz)
    b = resample(rows_b, args.hz)
    shift, corr = best_offset(a, b, args.align_signal, args.hz)
    print(f"alignment: shift={shift / args.hz:+.2f}s corr={corr:.3f} "
          f"on {args.align_signal} ({len(a)} vs {len(b)} samples)\n")
    if corr < 0.5:
        print("WARNING: weak alignment correlation - check takes match / signal choice")
    score(a, b, shift, args.hz, args.window_seconds)
    return 0


if __name__ == "__main__":
    sys.exit(main())
