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
        thigh_l, thigh_r = bones.get("thigh_l"), bones.get("thigh_r")
        if thigh_l and thigh_r:
            # Hip-line yaw in the world XY plane. Compared baseline-relative (own
            # median) so the two streams' different world frames cancel; measures
            # whether hip TURNS are reproduced (user-observed gap 2026-07-04).
            row["hip_yaw"] = math.degrees(
                math.atan2(thigh_r[1] - thigh_l[1], thigh_r[0] - thigh_l[0]))
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


def score(a, b, shift, hz, window_s, baseline_relative=False):
    keys = ["knee_l", "knee_r", "elbow_l", "elbow_r",
            "wrist_h_l", "wrist_h_r", "pelvis_z", "hip_yaw"]
    # pelvis_z compared as delta from its own median (squat depth, frame-free)
    med_a = sorted(r["pelvis_z"] for r in a)[len(a) // 2]
    med_b = sorted(r["pelvis_z"] for r in b)[len(b) // 2]

    # Baseline-relative mode: subtract each stream's own median from every
    # signal, so constant offsets (different export skeletons place joints
    # differently -> systematic angle bias) cancel and the comparison
    # measures MOTION fidelity (amplitude + timing) rather than absolute
    # pose. Use when the two streams come from different skeletons.
    baselines_a, baselines_b = {}, {}
    if baseline_relative:
        for k in keys:
            if k == "pelvis_z":
                continue
            va = sorted(r[k] for r in a if r.get(k) is not None)
            vb = sorted(r[k] for r in b if r.get(k) is not None)
            if va and vb:
                baselines_a[k] = va[len(va) // 2]
                baselines_b[k] = vb[len(vb) // 2]

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
            elif baseline_relative and k in baselines_a:
                delta = (va - baselines_a[k]) - (vb - baselines_b[k])
                if k == "hip_yaw":
                    # circular: wrap to [-180, 180] so crossing the atan2 seam
                    # does not register as a 350-degree error
                    delta = (delta + 180.0) % 360.0 - 180.0
                bucket[k].append(delta)
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


def score_all_bones(a_data, b_data, a, b, shift, hz, window_s):
    """Per-bone motion-trajectory error: for EVERY bone present in both streams,
    compare its displacement from its own median position (cancels world frame
    and skeleton proportions). Coverage guard: no curated subset - a gap like
    the hips (2026-07-04) cannot hide."""
    def bone_series(data):
        series = {}
        for s in data["samples"]:
            t = s.get("wall_time", s.get("game_time", 0.0))
            for bone, pos in s.get("bones", {}).items():
                series.setdefault(bone, []).append((t, pos))
        return series
    sa, sb = bone_series(a_data), bone_series(b_data)
    common = sorted(set(sa) & set(sb))
    print(f"\n=== all-bones motion error ({len(common)} bones common to both) ===")
    missing_a = sorted(set(sb) - set(sa)); missing_b = sorted(set(sa) - set(sb))
    if missing_a: print("only in fused:", ", ".join(missing_a))
    if missing_b: print("only in MHA:", ", ".join(missing_b))

    def resample_bone(entries):
        if not entries: return []
        t0, t1 = entries[0][0], entries[-1][0]
        out, idx, t = [], 0, t0
        while t <= t1:
            while idx + 1 < len(entries) and abs(entries[idx+1][0]-t) <= abs(entries[idx][0]-t):
                idx += 1
            out.append(entries[idx][1]); t += 1.0/hz
        return out

    def median_pos(pts):
        return [sorted(p[i] for p in pts)[len(pts)//2] for i in range(3)]

    rows_out = []
    for bone in common:
        pa, pb = resample_bone(sa[bone]), resample_bone(sb[bone])
        if len(pa) < hz * 5 or len(pb) < hz * 5: continue
        ma, mb = median_pos(pa), median_pos(pb)
        errs, win_err = [], {}
        for i in range(len(pa)):
            j = i + shift
            if not (0 <= j < len(pb)): continue
            da = [pa[i][k]-ma[k] for k in range(3)]
            db = [pb[j][k]-mb[k] for k in range(3)]
            e = math.sqrt(sum((da[k]-db[k])**2 for k in range(3)))
            errs.append(e)
            win_err.setdefault(int((i/hz)//window_s)*int(window_s), []).append(e)
        if not errs: continue
        rmse = math.sqrt(sum(e*e for e in errs)/len(errs))
        worst_w = max(win_err, key=lambda w: sum(x*x for x in win_err[w])/len(win_err[w]))
        worst_rmse = math.sqrt(sum(x*x for x in win_err[worst_w])/len(win_err[worst_w]))
        rows_out.append((rmse, max(errs), worst_w, worst_rmse, bone))
    rows_out.sort(reverse=True)
    print(f"{'bone':<14}{'RMSE cm':>9}{'peak cm':>9}{'worst window':>14}")
    for rmse, peak, ww, wr, bone in rows_out:
        print(f"{bone:<14}{rmse:9.1f}{peak:9.1f}{('%ds (%.1f)' % (ww, wr)):>14}")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("mha")
    ap.add_argument("fused")
    ap.add_argument("--align-signal", default="pelvis_z")
    ap.add_argument("--hz", type=float, default=30.0)
    ap.add_argument("--window-seconds", type=float, default=5.0)
    ap.add_argument("--baseline-relative", action="store_true",
                    help="subtract each stream's own median per signal "
                         "(cross-skeleton comparison)")
    ap.add_argument("--all-bones", action="store_true",
                    help="also report per-bone motion-trajectory error for "
                         "every bone present in both streams")
    args = ap.parse_args()

    data_a, rows_a = load_series(args.mha)
    data_b, rows_b = load_series(args.fused)
    a = resample(rows_a, args.hz)
    b = resample(rows_b, args.hz)
    shift, corr = best_offset(a, b, args.align_signal, args.hz)
    print(f"alignment: shift={shift / args.hz:+.2f}s corr={corr:.3f} "
          f"on {args.align_signal} ({len(a)} vs {len(b)} samples)\n")
    if corr < 0.5:
        print("WARNING: weak alignment correlation - check takes match / signal choice")
    score(a, b, shift, args.hz, args.window_seconds,
          baseline_relative=args.baseline_relative)
    if args.all_bones:
        score_all_bones(data_a, data_b, a, b, shift, args.hz, args.window_seconds)
    return 0


if __name__ == "__main__":
    sys.exit(main())
