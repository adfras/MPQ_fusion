#!/usr/bin/env python3
"""Mono-depth feasibility: can PC-side metric monocular depth beat MediaPipe's Z?

Runs Depth Anything V2 (metric, indoor) + MediaPipe Pose on the take-3 Camo video
and compares the knee forward-displacement waveform (the depth axis - the known
weak axis: knee raises score ~25 deg compressed) against the Epic MHA offline
solve of the SAME clip. Video frames and the MHA sample JSON are frame-aligned by
construction (same clip, both 30 Hz), so no alignment step exists to get wrong.

Per movement window and per side, reports amplitude (p95-p5, cm), correlation vs
Epic, and mean-centered MAE vs Epic for both estimators:
    mono  = depth-map difference hip_mid - knee (metres -> cm)
    mp    = MediaPipe world-landmark Z difference (its inferred depth)
Verdict: mono is worth integrating if it tracks Epic's waveform materially better
than MediaPipe's inferred Z (higher correlation, amplitude ratio nearer 1).

No UE dependency. GPU strongly recommended.
"""

import argparse
import json
import math
import sys
import time

import cv2
import numpy as np

# landmark indices
L_SHOULDER, R_SHOULDER = 11, 12
L_HIP, R_HIP = 23, 24
L_KNEE, R_KNEE = 25, 26
L_ANKLE, R_ANKLE = 27, 28

PATCH_HALF = 2
DEPTH_MIN_M, DEPTH_MAX_M = 0.15, 12.0
VIS_MIN = 0.5
MARGIN_S = 3.0
BATCH = 8

DEFAULT_VIDEO = r"C:\Users\Alan\Videos\take3_cfr30.mp4"
DEFAULT_MHA = (r"D:\Epic\Unreal_Projects\TestingKit5\Saved\CodexAgent\Diagnostics"
               r"\live_pie_bone_measure_MHA_Solve_mhaSolveTake3full_20260705_173923.json")
MODEL_ID = "depth-anything/Depth-Anything-V2-Metric-Indoor-Small-hf"


def sample_patch(depth, u, v, ref_depth=None, band_m=1.5, half=PATCH_HALF):
    """Median of the valid patch. With ref_depth, only depths within band_m of it
    count (foreground/body band): a landmark a few pixels off a raised limb
    otherwise samples the wall metres behind and flips the sign of the motion.
    Returns (depth_m or None, background_hit)."""
    h, w = depth.shape[:2]
    px, py = int(round(u * (w - 1))), int(round(v * (h - 1)))
    if px < 0 or py < 0 or px >= w or py >= h:
        return None, False
    x0, x1 = max(0, px - half), min(w, px + half + 1)
    y0, y1 = max(0, py - half), min(h, py + half + 1)
    patch = depth[y0:y1, x0:x1].ravel()
    valid = patch[np.isfinite(patch) & (patch > DEPTH_MIN_M) & (patch < DEPTH_MAX_M)]
    if not valid.size:
        return None, False
    if ref_depth is None:
        return float(np.median(valid)), False
    on_body = valid[np.abs(valid - ref_depth) < band_m]
    if not on_body.size:
        return None, True  # patch exists but everything is off-body: background hit
    return float(np.median(on_body)), bool(np.median(valid) - ref_depth > band_m)


def detect_windows(samples):
    """Movement windows from the Epic referee itself (video timeline)."""
    t = np.array([s["wall_time"] for s in samples])

    def series(bone, axis):
        return np.array([s["bones"].get(bone, [np.nan] * 3)[axis] for s in samples])

    def runs(mask, min_len=2.0, gap=2.0):
        out, start = [], None
        for i, m in enumerate(mask):
            if m and start is None:
                start = t[i]
            elif not m and start is not None:
                out.append([start, t[i]])
                start = None
        if start is not None:
            out.append([start, t[-1]])
        merged = []
        for r in out:
            if merged and r[0] - merged[-1][1] < gap:
                merged[-1][1] = r[1]
            else:
                merged.append(r)
        return [r for r in merged if r[1] - r[0] >= min_len]

    calf_l, calf_r = series("calf_l", 2), series("calf_r", 2)
    raise_mask = ((calf_l > np.nanpercentile(calf_l, 20) + 15)
                  | (calf_r > np.nanpercentile(calf_r, 20) + 15))
    pelvis = series("pelvis", 2)
    squat_mask = pelvis < np.nanpercentile(pelvis, 80) - 12
    return ([("knee_raise", a, b) for a, b in runs(raise_mask)]
            + [("squat", a, b) for a, b in runs(squat_mask)])


def epic_knee_forward(samples):
    """Per-frame horizontal knee-vs-pelvis displacement along the facing axis, cm."""
    n = len(samples)
    fwd_l = np.full(n, np.nan)
    fwd_r = np.full(n, np.nan)
    # facing from toe direction (ball - foot), horizontal, take-median (user faces
    # the camera within ~15 deg for the whole take)
    dirs = []
    for s in samples:
        b = s["bones"]
        acc = np.zeros(2)
        cnt = 0
        for side in ("l", "r"):
            foot, ball = b.get("foot_" + side), b.get("ball_" + side)
            if foot and ball:
                d = np.array([ball[0] - foot[0], ball[1] - foot[1]])
                if np.linalg.norm(d) > 1e-3:
                    acc += d / np.linalg.norm(d)
                    cnt += 1
        if cnt:
            dirs.append(acc / cnt)
    fwd_axis = np.median(np.array(dirs), axis=0)
    fwd_axis /= np.linalg.norm(fwd_axis)
    for i, s in enumerate(samples):
        b = s["bones"]
        pelvis = b.get("pelvis")
        if not pelvis:
            continue
        for side, arr in (("l", fwd_l), ("r", fwd_r)):
            calf = b.get("calf_" + side)
            if calf:
                arr[i] = ((calf[0] - pelvis[0]) * fwd_axis[0]
                          + (calf[1] - pelvis[1]) * fwd_axis[1])
    return fwd_l, fwd_r, fwd_axis.tolist()


def stats_vs_epic(est, epic):
    """(amplitude cm, correlation, mean-centered MAE cm, n) on jointly-valid frames."""
    m = np.isfinite(est) & np.isfinite(epic)
    if m.sum() < 15:
        return None
    e, g = est[m], epic[m]
    amp = float(np.percentile(e, 95) - np.percentile(e, 5))
    corr = float(np.corrcoef(e, g)[0, 1]) if e.std() > 1e-6 and g.std() > 1e-6 else 0.0
    mae = float(np.mean(np.abs((e - e.mean()) - (g - g.mean()))))
    return amp, corr, mae, int(m.sum())


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--video", default=DEFAULT_VIDEO)
    ap.add_argument("--mha", default=DEFAULT_MHA)
    ap.add_argument("--out", default=None, help="write per-frame series + report JSON")
    ap.add_argument("--model", default=MODEL_ID)
    args = ap.parse_args()

    with open(args.mha) as f:
        mha = json.load(f)
    samples = mha["samples"]
    windows = detect_windows(samples)
    print("Windows from Epic referee:", [(k, round(a, 1), round(b, 1)) for k, a, b in windows])
    epic_l, epic_r, fwd_axis = epic_knee_forward(samples)
    print("Facing axis (UE horizontal):", [round(v, 3) for v in fwd_axis])

    # consolidated frame ranges with margin
    fps = 30.0
    spans = sorted((max(0.0, a - MARGIN_S), b + MARGIN_S) for _, a, b in windows)
    merged = []
    for s in spans:
        if merged and s[0] <= merged[-1][1]:
            merged[-1] = (merged[-1][0], max(merged[-1][1], s[1]))
        else:
            merged.append(s)
    frame_set = sorted({fi for a, b in merged
                        for fi in range(int(a * fps), min(int(b * fps) + 1, len(samples)))})
    print("Processing %d frames across %d spans" % (len(frame_set), len(merged)))

    import mediapipe as mp
    import torch
    from PIL import Image
    from transformers import pipeline

    device = 0 if torch.cuda.is_available() else -1
    print("Loading %s (device=%s)..." % (args.model, device))
    depth_pipe = pipeline("depth-estimation", model=args.model, device=device)
    pose = mp.solutions.pose.Pose(model_complexity=1,
                                  min_detection_confidence=0.5,
                                  min_tracking_confidence=0.5)

    n = len(samples)
    mono_l = np.full(n, np.nan)
    mono_r = np.full(n, np.nan)
    mp_l = np.full(n, np.nan)
    mp_r = np.full(n, np.nan)

    cap = cv2.VideoCapture(args.video)
    if not cap.isOpened():
        print("FATAL: cannot open video", args.video)
        return 1

    t0 = time.monotonic()
    done = 0
    bg_hits = [0, 0, 0]  # left knee bg hits, right knee bg hits, total knee samples
    batch_frames, batch_ids = [], []

    def flush_batch():
        nonlocal done
        if not batch_frames:
            return
        pils = [Image.fromarray(f) for f in batch_frames]
        outs = depth_pipe(pils)
        if isinstance(outs, dict):
            outs = [outs]
        for rgb, fi, out in zip(batch_frames, batch_ids, outs):
            depth = out["predicted_depth"].squeeze().float().cpu().numpy()
            res = pose.process(rgb)
            if not res.pose_landmarks:
                continue
            lm = res.pose_landmarks.landmark
            wl = res.pose_world_landmarks.landmark if res.pose_world_landmarks else None
            if wl is None:
                continue
            if min(lm[i].visibility for i in (L_HIP, R_HIP, L_KNEE, R_KNEE)) < VIS_MIN:
                continue
            hip_d = [sample_patch(depth, lm[i].x, lm[i].y)[0] for i in (L_HIP, R_HIP)]
            hip_d = [d for d in hip_d if d is not None]
            if len(hip_d) < 2:
                continue
            hip_mid = float(np.mean(hip_d))
            kl, bg_l = sample_patch(depth, lm[L_KNEE].x, lm[L_KNEE].y,
                                    ref_depth=hip_mid, half=3)
            kr, bg_r = sample_patch(depth, lm[R_KNEE].x, lm[R_KNEE].y,
                                    ref_depth=hip_mid, half=3)
            bg_hits[0] += int(bg_l)
            bg_hits[1] += int(bg_r)
            bg_hits[2] += 2
            # mono forward = how much closer to the camera than the hips (cm)
            if kl is not None:
                mono_l[fi] = (hip_mid - kl) * 100.0
            if kr is not None:
                mono_r[fi] = (hip_mid - kr) * 100.0
            # MediaPipe inferred depth axis: world-landmark z, hips ~origin;
            # toward-camera = more negative -> forward = -(knee.z - hip_mid.z), cm
            hip_z = 0.5 * (wl[L_HIP].z + wl[R_HIP].z)
            mp_l[fi] = -(wl[L_KNEE].z - hip_z) * 100.0
            mp_r[fi] = -(wl[R_KNEE].z - hip_z) * 100.0
        done += len(batch_frames)
        batch_frames.clear()
        batch_ids.clear()
        if done % 96 < BATCH:
            rate = done / max(time.monotonic() - t0, 1e-6)
            print("  %d/%d frames (%.1f fps)" % (done, len(frame_set), rate), flush=True)

    prev = -10
    for fi in frame_set:
        if fi != prev + 1:
            cap.set(cv2.CAP_PROP_POS_FRAMES, fi)
        prev = fi
        ok, bgr = cap.read()
        if not ok:
            continue
        batch_frames.append(cv2.cvtColor(bgr, cv2.COLOR_BGR2RGB))
        batch_ids.append(fi)
        if len(batch_frames) >= BATCH:
            flush_batch()
    flush_batch()
    cap.release()
    pose.close()

    if bg_hits[2]:
        print("Knee background hits (patch off-body): L %.1f%%  R %.1f%% of %d samples"
              % (200.0 * bg_hits[0] / bg_hits[2], 200.0 * bg_hits[1] / bg_hits[2], bg_hits[2] // 2))

    # sign check: MediaPipe world z convention verified empirically against Epic
    report = {"model": args.model, "facing_axis": fwd_axis, "windows": []}
    print()
    print("=" * 96)
    print("MONO-DEPTH vs MEDIAPIPE-Z vs EPIC  -  knee forward displacement (depth axis)")
    print("%-12s %-6s | %-22s | %-22s | %-22s" % ("window", "side", "Epic", "mono (DAv2 metric)", "MediaPipe Z"))
    print("%-12s %-6s | %-22s | %-22s | %-22s" % ("", "", "amp cm", "amp cm  corr   mae", "amp cm  corr   mae"))
    print("-" * 96)
    for kind, a, b in windows:
        i0, i1 = int(a * fps), min(int(b * fps) + 1, n)
        sl = slice(i0, i1)
        for side, est_mono, est_mp, epic in (("L", mono_l, mp_l, epic_l),
                                             ("R", mono_r, mp_r, epic_r)):
            g = epic[sl]
            sm = stats_vs_epic(est_mono[sl], g)
            sp = stats_vs_epic(est_mp[sl], g)
            gm = np.isfinite(g)
            if not gm.sum():
                continue
            gamp = float(np.percentile(g[gm], 95) - np.percentile(g[gm], 5))
            row = {"window": kind, "t0": round(a, 1), "t1": round(b, 1), "side": side,
                   "epic_amp_cm": round(gamp, 1),
                   "mono": None if sm is None else dict(zip(("amp_cm", "corr", "mae_cm", "n"), [round(v, 3) for v in sm])),
                   "mp": None if sp is None else dict(zip(("amp_cm", "corr", "mae_cm", "n"), [round(v, 3) for v in sp]))}
            report["windows"].append(row)
            fmt = lambda s: "  none                " if s is None else "%6.1f  %+.2f  %5.1f  " % (s[0], s[1], s[2])
            print("%-12s %-6s | %20.1f   | %s | %s" % (
                "%s %.0f-%.0fs" % (kind[:6], a, b), side, gamp, fmt(sm), fmt(sp)))
    print("=" * 96)

    def agg(key):
        cs = [r[key]["corr"] for r in report["windows"] if r[key]]
        ms = [r[key]["mae_cm"] for r in report["windows"] if r[key]]
        ratios = [r[key]["amp_cm"] / r["epic_amp_cm"] for r in report["windows"]
                  if r[key] and r["epic_amp_cm"] > 3.0]
        return (np.median(cs) if cs else float("nan"),
                np.median(ms) if ms else float("nan"),
                np.median(ratios) if ratios else float("nan"))

    mono_c, mono_m, mono_r_ = agg("mono")
    mp_c, mp_m, mp_r_ = agg("mp")
    print("MEDIANS      corr    mae_cm   amp/epic")
    print("  mono      %+.2f   %6.1f   %.2f" % (mono_c, mono_m, mono_r_))
    print("  mediapipe %+.2f   %6.1f   %.2f" % (mp_c, mp_m, mp_r_))
    verdict_mono_better = (mono_c > mp_c + 0.1) or (mono_m < mp_m * 0.75)
    if verdict_mono_better:
        print("VERDICT: mono-depth tracks the Epic referee materially better than MediaPipe's")
        print("inferred Z on the depth axis -> PC-side depth integration is worth building.")
    else:
        print("VERDICT: mono-depth does NOT materially beat MediaPipe's inferred Z here;")
        print("try the Large metric model (--model ...-Large-hf) before concluding, then")
        print("consider shelving the depth arc.")
    report["medians"] = {"mono": {"corr": round(float(mono_c), 3), "mae_cm": round(float(mono_m), 2), "amp_ratio": round(float(mono_r_), 3)},
                         "mp": {"corr": round(float(mp_c), 3), "mae_cm": round(float(mp_m), 2), "amp_ratio": round(float(mp_r_), 3)}}
    if args.out:
        payload = {"report": report,
                   "series": {"mono_l": [None if not np.isfinite(v) else round(float(v), 2) for v in mono_l],
                              "mono_r": [None if not np.isfinite(v) else round(float(v), 2) for v in mono_r],
                              "mp_l": [None if not np.isfinite(v) else round(float(v), 2) for v in mp_l],
                              "mp_r": [None if not np.isfinite(v) else round(float(v), 2) for v in mp_r],
                              "epic_l": [None if not np.isfinite(v) else round(float(v), 2) for v in epic_l],
                              "epic_r": [None if not np.isfinite(v) else round(float(v), 2) for v in epic_r]}}
        with open(args.out, "w") as f:
            json.dump(payload, f)
        print("Wrote", args.out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
