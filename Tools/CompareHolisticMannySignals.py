import argparse
import csv
import json
import math
from pathlib import Path

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np


POSE_NAMES = [
    "nose",
    "left_eye_inner",
    "left_eye",
    "left_eye_outer",
    "right_eye_inner",
    "right_eye",
    "right_eye_outer",
    "left_ear",
    "right_ear",
    "mouth_left",
    "mouth_right",
    "left_shoulder",
    "right_shoulder",
    "left_elbow",
    "right_elbow",
    "left_wrist",
    "right_wrist",
    "left_pinky",
    "right_pinky",
    "left_index",
    "right_index",
    "left_thumb",
    "right_thumb",
    "left_hip",
    "right_hip",
    "left_knee",
    "right_knee",
    "left_ankle",
    "right_ankle",
    "left_heel",
    "right_heel",
    "left_foot_index",
    "right_foot_index",
]

FACE_POINTS = {
    1: "nose_tip",
    10: "forehead",
    33: "left_eye_outer",
    61: "mouth_left",
    152: "chin",
    199: "lower_face",
    263: "right_eye_outer",
    291: "mouth_right",
    468: "left_iris",
    473: "right_iris",
}


def finite(values):
    return np.asarray(values, dtype=float)


def p_range(values, lo=5, hi=95):
    arr = finite(values)
    arr = arr[np.isfinite(arr)]
    if arr.size == 0:
        return math.nan
    return float(np.nanpercentile(arr, hi) - np.nanpercentile(arr, lo))


def p_value(values, pct):
    arr = finite(values)
    arr = arr[np.isfinite(arr)]
    if arr.size == 0:
        return math.nan
    return float(np.nanpercentile(arr, pct))


def standardize(values):
    arr = finite(values).copy()
    mask = np.isfinite(arr)
    if not np.any(mask):
        return arr
    med = np.nanmedian(arr[mask])
    spread = p_range(arr[mask])
    if not np.isfinite(spread) or abs(spread) < 1.0e-9:
        spread = float(np.nanstd(arr[mask]))
    if not np.isfinite(spread) or abs(spread) < 1.0e-9:
        spread = 1.0
    arr[mask] = (arr[mask] - med) / spread
    return arr


def safe_corr(a, b):
    a = finite(a)
    b = finite(b)
    mask = np.isfinite(a) & np.isfinite(b)
    if np.count_nonzero(mask) < 5:
        return math.nan
    aa = a[mask]
    bb = b[mask]
    if np.nanstd(aa) < 1.0e-9 or np.nanstd(bb) < 1.0e-9:
        return math.nan
    return float(np.corrcoef(aa, bb)[0, 1])


def best_lag(times, source, target, max_lag_seconds=0.5):
    times = finite(times)
    source = standardize(source)
    target = standardize(target)
    mask = np.isfinite(times) & np.isfinite(source) & np.isfinite(target)
    if np.count_nonzero(mask) < 8:
        return math.nan, math.nan
    t = times[mask]
    s = source[mask]
    m = target[mask]
    dt = np.nanmedian(np.diff(t))
    if not np.isfinite(dt) or dt <= 0:
        return math.nan, math.nan
    max_lag = max(1, int(round(max_lag_seconds / dt)))
    best = -2.0
    best_i = 0
    for lag in range(-max_lag, max_lag + 1):
        if lag < 0:
            ss = s[-lag:]
            mm = m[:lag]
        elif lag > 0:
            ss = s[:-lag]
            mm = m[lag:]
        else:
            ss = s
            mm = m
        corr = safe_corr(ss, mm)
        if np.isfinite(corr) and corr > best:
            best = corr
            best_i = lag
    if best < -1.0:
        return math.nan, math.nan
    return float(best_i * dt), float(best)


def step_stats(values):
    arr = standardize(values)
    diff = np.abs(np.diff(arr))
    diff = diff[np.isfinite(diff)]
    if diff.size == 0:
        return math.nan, math.nan
    return float(np.nanpercentile(diff, 95)), float(np.nanmax(diff))


def point(sample, space, name):
    obj = sample.get(space, {}).get(name)
    if not obj:
        return np.array([math.nan, math.nan, math.nan], dtype=float)
    return np.asarray(obj.get("pos", [math.nan, math.nan, math.nan]), dtype=float)


def face_point(sample, index):
    face = sample.get("face", {})
    points = face.get("normalized_landmarks", [])
    if index < 0 or index >= len(points):
        return np.array([math.nan, math.nan, math.nan], dtype=float)
    return np.asarray(points[index].get("pos", [math.nan, math.nan, math.nan]), dtype=float)


def nested_scalar(sample, *keys):
    obj = sample
    for key in keys:
        if not isinstance(obj, dict):
            return math.nan
        obj = obj.get(key)
    try:
        return float(obj)
    except (TypeError, ValueError):
        return math.nan


def nested_bool(sample, *keys):
    obj = sample
    for key in keys:
        if not isinstance(obj, dict):
            return False
        obj = obj.get(key)
    return bool(obj)


def unwrap_degrees(values):
    arr = finite(values)
    result = arr.copy()
    finite_mask = np.isfinite(result)
    start = None
    for i, ok in enumerate(finite_mask):
        if ok and start is None:
            start = i
        if start is not None and (not ok or i == len(result) - 1):
            end = i if not ok else i + 1
            if end - start > 1:
                result[start:end] = np.degrees(np.unwrap(np.radians(result[start:end])))
            start = None
    return result


def midpoint(sample, space, a, b):
    return (point(sample, space, a) + point(sample, space, b)) * 0.5


def angle_deg(a, b, c):
    va = a - b
    vc = c - b
    na = np.linalg.norm(va)
    nc = np.linalg.norm(vc)
    if na < 1.0e-9 or nc < 1.0e-9:
        return math.nan
    return math.degrees(math.acos(float(np.clip(np.dot(va, vc) / (na * nc), -1.0, 1.0))))


def add_signal(signals, name, values, group):
    arr = finite(values)
    if arr.size == 0:
        return
    if np.count_nonzero(np.isfinite(arr)) < 5:
        return
    if not np.isfinite(p_range(arr)):
        return
    signals[name] = {"values": arr, "group": group}


def extract_source_signals(samples):
    signals = {}
    axes = ["x", "y", "z"]
    for space, prefix in [
        ("pose_world_landmarks", "pose_world"),
        ("pose_normalized_landmarks", "pose_norm"),
    ]:
        for lm in POSE_NAMES:
            vals = np.asarray([point(s, space, lm) for s in samples], dtype=float)
            for i, axis in enumerate(axes):
                add_signal(signals, f"{prefix}.{lm}.{axis}", vals[:, i], prefix)

    for space, prefix in [("pose_world_landmarks", "pose_world"), ("pose_normalized_landmarks", "pose_norm")]:
        shoulder_mid = np.asarray([midpoint(s, space, "left_shoulder", "right_shoulder") for s in samples])
        hip_mid = np.asarray([midpoint(s, space, "left_hip", "right_hip") for s in samples])
        left_shoulder = np.asarray([point(s, space, "left_shoulder") for s in samples])
        right_shoulder = np.asarray([point(s, space, "right_shoulder") for s in samples])
        left_ear = np.asarray([point(s, space, "left_ear") for s in samples])
        right_ear = np.asarray([point(s, space, "right_ear") for s in samples])
        nose = np.asarray([point(s, space, "nose") for s in samples])
        for i, axis in enumerate(axes):
            add_signal(signals, f"{prefix}.shoulder_mid.{axis}", shoulder_mid[:, i], "shoulders")
            add_signal(signals, f"{prefix}.hip_mid.{axis}", hip_mid[:, i], "hips")
            add_signal(signals, f"{prefix}.nose_from_shoulder.{axis}", nose[:, i] - shoulder_mid[:, i], "head")
        add_signal(signals, f"{prefix}.torso_height", shoulder_mid[:, 2] - hip_mid[:, 2], "torso")
        add_signal(signals, f"{prefix}.shoulder_width", np.linalg.norm(right_shoulder - left_shoulder, axis=1), "shoulders")
        add_signal(signals, f"{prefix}.left_shoulder_lift_from_hips", left_shoulder[:, 2] - hip_mid[:, 2], "shoulders")
        add_signal(signals, f"{prefix}.right_shoulder_lift_from_hips", right_shoulder[:, 2] - hip_mid[:, 2], "shoulders")
        add_signal(signals, f"{prefix}.relative_shoulder_lift_l_minus_r", left_shoulder[:, 2] - right_shoulder[:, 2], "shoulders")
        add_signal(signals, f"{prefix}.ear_width", np.linalg.norm(right_ear - left_ear, axis=1), "head")

        for side in ["left", "right"]:
            shoulder = np.asarray([point(s, space, f"{side}_shoulder") for s in samples])
            elbow = np.asarray([point(s, space, f"{side}_elbow") for s in samples])
            wrist = np.asarray([point(s, space, f"{side}_wrist") for s in samples])
            knee = np.asarray([point(s, space, f"{side}_knee") for s in samples])
            hip = np.asarray([point(s, space, f"{side}_hip") for s in samples])
            ankle = np.asarray([point(s, space, f"{side}_ankle") for s in samples])
            add_signal(signals, f"{prefix}.{side}_elbow_angle", [angle_deg(shoulder[i], elbow[i], wrist[i]) for i in range(len(samples))], "arms")
            add_signal(signals, f"{prefix}.{side}_knee_angle", [angle_deg(hip[i], knee[i], ankle[i]) for i in range(len(samples))], "legs")
            add_signal(signals, f"{prefix}.{side}_wrist_from_shoulder_z", wrist[:, 2] - shoulder[:, 2], "arms")

    face_vals = {idx: np.asarray([face_point(s, idx) for s in samples], dtype=float) for idx in FACE_POINTS}
    for idx, label in FACE_POINTS.items():
        vals = face_vals[idx]
        for i, axis in enumerate(axes):
            add_signal(signals, f"face.{label}.{axis}", vals[:, i], "face")

    le = face_vals[33]
    re = face_vals[263]
    nose = face_vals[1]
    chin = face_vals[152]
    eye_vec = re - le
    eye_span = np.linalg.norm(eye_vec[:, :2], axis=1)
    eye_span_safe = np.where(eye_span > 1.0e-8, eye_span, np.nan)
    eye_mid = (le + re) * 0.5
    add_signal(signals, "face.eye_span", eye_span, "face")
    add_signal(signals, "face.head_pitch_chin_eye_ratio", (chin[:, 1] - eye_mid[:, 1]) / eye_span_safe, "face")
    add_signal(signals, "face.head_yaw_nose_eye_ratio", (nose[:, 0] - eye_mid[:, 0]) / eye_span_safe, "face")
    add_signal(signals, "face.head_roll_eye_deg", np.degrees(np.arctan2(eye_vec[:, 1], eye_vec[:, 0])), "face")
    add_signal(signals, "face.nose_from_eye_mid_x", (nose[:, 0] - eye_mid[:, 0]) / eye_span_safe, "face")
    add_signal(signals, "face.nose_from_eye_mid_y", (nose[:, 1] - eye_mid[:, 1]) / eye_span_safe, "face")
    add_signal(signals, "face.chin_from_eye_mid_y", (chin[:, 1] - eye_mid[:, 1]) / eye_span_safe, "face")
    solver_specs = [
        ("solver.head.dense_face_pitch_ratio", ("solver", "head", "dense_face_pitch_ratio"), "solver_head", ("solver", "head", "has_dense_face")),
        ("solver.head.dense_face_yaw_ratio", ("solver", "head", "dense_face_yaw_ratio"), "solver_head", ("solver", "head", "has_dense_face")),
        ("solver.head.dense_face_roll_deg", ("solver", "head", "dense_face_roll_deg"), "solver_head", ("solver", "head", "has_dense_face")),
        ("solver.head.dense_face_pitch_delta", ("solver", "head", "dense_face_pitch_delta"), "solver_head", ("solver", "head", "has_dense_face")),
        ("solver.head.dense_face_yaw_delta", ("solver", "head", "dense_face_yaw_delta"), "solver_head", ("solver", "head", "has_dense_face")),
        ("solver.head.dense_face_roll_delta_deg", ("solver", "head", "dense_face_roll_delta_deg"), "solver_head", ("solver", "head", "has_dense_face")),
        ("solver.head.computed_pitch_deg", ("solver", "head", "computed_pitch_deg"), "solver_head", ("solver", "head", "has_dense_face")),
        ("solver.head.computed_yaw_deg", ("solver", "head", "computed_yaw_deg"), "solver_head", ("solver", "head", "has_dense_face")),
        ("solver.head.computed_roll_deg", ("solver", "head", "computed_roll_deg"), "solver_head", ("solver", "head", "has_dense_face")),
    ]
    for side in ["left", "right"]:
        json_side = f"{side}_shoulder"
        prefix = f"solver.{side}_shoulder"
        solver_specs.extend([
            (f"{prefix}.shoulder_signed_lift_cm", ("solver", json_side, "shoulder_signed_lift_cm"), "solver_shoulders", ("solver", json_side, "valid")),
            (f"{prefix}.shoulder_relative_lift_cm", ("solver", json_side, "shoulder_relative_lift_cm"), "solver_shoulders", ("solver", json_side, "valid")),
            (f"{prefix}.shoulder_positive_lift_evidence_cm", ("solver", json_side, "shoulder_positive_lift_evidence_cm"), "solver_shoulders", ("solver", json_side, "valid")),
            (f"{prefix}.shoulder_head_clearance_cm", ("solver", json_side, "shoulder_head_clearance_cm"), "solver_shoulders", ("solver", json_side, "valid")),
            (f"{prefix}.shoulder_head_clearance_shrug_cm", ("solver", json_side, "shoulder_head_clearance_shrug_cm"), "solver_shoulders", ("solver", json_side, "valid")),
            (f"{prefix}.computed_shrug_weight", ("solver", json_side, "computed_shrug_weight"), "solver_shoulders", ("solver", json_side, "valid")),
            (f"{prefix}.smoothed_shrug_weight", ("solver", json_side, "smoothed_shrug_weight"), "solver_shoulders", ("solver", json_side, "valid")),
            (f"{prefix}.computed_lift_translation_cm", ("solver", json_side, "computed_lift_translation_cm"), "solver_shoulders", ("solver", json_side, "valid")),
            (f"{prefix}.smoothed_lift_translation_cm", ("solver", json_side, "smoothed_lift_translation_cm"), "solver_shoulders", ("solver", json_side, "valid")),
            (f"{prefix}.applied_clavicle_lift_cm", ("solver", json_side, "applied_clavicle_lift_cm"), "solver_shoulders", ("solver", json_side, "valid")),
            (f"{prefix}.up_weight", ("solver", json_side, "up_weight"), "solver_shoulders", ("solver", json_side, "valid")),
        ])
    for name, path, group, valid_path in solver_specs:
        add_signal(
            signals,
            name,
            [nested_scalar(sample, *path) if nested_bool(sample, *valid_path) else math.nan for sample in samples],
            group)
    return signals


def extract_manny_signals(samples):
    signals = {}
    axes = ["x", "y", "z"]
    rot_axes = ["pitch", "yaw", "roll"]
    live_keys = sorted(samples[0].get("live", {}).keys()) if samples else []
    for bone in live_keys:
        for key, labels in [("loc", axes), ("local_rot", rot_axes), ("rot", rot_axes)]:
            vals = []
            for sample in samples:
                obj = sample.get("live", {}).get(bone, {})
                raw = obj.get(key, [math.nan, math.nan, math.nan])
                vals.append(raw if len(raw) >= 3 else [math.nan, math.nan, math.nan])
            arr = np.asarray(vals, dtype=float)
            for i, axis in enumerate(labels):
                values = unwrap_degrees(arr[:, i]) if key in ("local_rot", "rot") else arr[:, i]
                add_signal(signals, f"manny.{bone}.{key}.{axis}", values, bone)
    return signals


def compare_pair(times, source_name, source, manny_name, manny, lag_search=True):
    corr_zero = safe_corr(source, manny)
    if lag_search:
        lag, corr_lag = best_lag(times, source, manny)
    else:
        lag, corr_lag = 0.0, corr_zero
    source_step95, source_stepmax = step_stats(source)
    manny_step95, manny_stepmax = step_stats(manny)
    source_range = p_range(source)
    manny_range = p_range(manny)
    return {
        "source": source_name,
        "manny": manny_name,
        "source_range_p95_p05": source_range,
        "manny_range_p95_p05": manny_range,
        "range_ratio_manny_over_source": float(manny_range / source_range) if np.isfinite(source_range) and abs(source_range) > 1.0e-9 else math.nan,
        "corr_zero_lag": corr_zero,
        "best_lag_seconds": lag,
        "corr_best_lag": corr_lag,
        "source_step95_standardized": source_step95,
        "manny_step95_standardized": manny_step95,
        "source_stepmax_standardized": source_stepmax,
        "manny_stepmax_standardized": manny_stepmax,
        "step95_ratio_manny_over_source": float(manny_step95 / source_step95) if np.isfinite(source_step95) and abs(source_step95) > 1.0e-9 else math.nan,
    }


def expected_pairs():
    pairs = [
        ("face.head_pitch_chin_eye_ratio", "manny.head.local_rot.pitch"),
        ("face.head_yaw_nose_eye_ratio", "manny.head.local_rot.yaw"),
        ("face.head_roll_eye_deg", "manny.head.local_rot.roll"),
        ("solver.head.computed_pitch_deg", "manny.head.local_rot.pitch"),
        ("solver.head.computed_yaw_deg", "manny.head.local_rot.yaw"),
        ("solver.head.computed_roll_deg", "manny.head.local_rot.roll"),
        ("pose_world.nose_from_shoulder.z", "manny.head.local_rot.pitch"),
        ("pose_world.ear_width", "manny.head.local_rot.yaw"),
        ("pose_world.shoulder_mid.z", "manny.clavicle_l.loc.z"),
        ("pose_world.shoulder_mid.z", "manny.clavicle_r.loc.z"),
        ("solver.left_shoulder.shoulder_signed_lift_cm", "manny.clavicle_l.loc.z"),
        ("solver.right_shoulder.shoulder_signed_lift_cm", "manny.clavicle_r.loc.z"),
        ("solver.left_shoulder.smoothed_lift_translation_cm", "manny.clavicle_l.loc.z"),
        ("solver.right_shoulder.smoothed_lift_translation_cm", "manny.clavicle_r.loc.z"),
        ("solver.left_shoulder.applied_clavicle_lift_cm", "manny.clavicle_l.loc.z"),
        ("solver.right_shoulder.applied_clavicle_lift_cm", "manny.clavicle_r.loc.z"),
        ("pose_world.left_shoulder_lift_from_hips", "manny.clavicle_l.local_rot.pitch"),
        ("pose_world.right_shoulder_lift_from_hips", "manny.clavicle_r.local_rot.pitch"),
        ("pose_world.relative_shoulder_lift_l_minus_r", "manny.clavicle_l.local_rot.roll"),
        ("pose_world.relative_shoulder_lift_l_minus_r", "manny.clavicle_r.local_rot.roll"),
        ("pose_norm.left_shoulder_lift_from_hips", "manny.clavicle_l.local_rot.pitch"),
        ("pose_norm.right_shoulder_lift_from_hips", "manny.clavicle_r.local_rot.pitch"),
        ("pose_world.left_elbow_angle", "manny.upperarm_l.local_rot.pitch"),
        ("pose_world.right_elbow_angle", "manny.upperarm_r.local_rot.pitch"),
        ("pose_world.left_wrist_from_shoulder_z", "manny.hand_l.loc.z"),
        ("pose_world.right_wrist_from_shoulder_z", "manny.hand_r.loc.z"),
    ]
    seen = set()
    unique = []
    for item in pairs:
        if item not in seen:
            unique.append(item)
            seen.add(item)
    return unique


def write_csv(path, rows, fieldnames):
    with path.open("w", newline="", encoding="utf-8") as f:
        writer = csv.DictWriter(f, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(rows)


def make_expected_chart(out_path, times, source_signals, manny_signals, rows):
    selected = [r for r in rows if r["source"] in source_signals and r["manny"] in manny_signals]
    if not selected:
        return
    selected = selected[:12]
    fig, axes = plt.subplots(len(selected), 1, figsize=(16, max(2.2, 1.8 * len(selected))), sharex=True)
    if len(selected) == 1:
        axes = [axes]
    for ax, row in zip(axes, selected):
        src = standardize(source_signals[row["source"]]["values"])
        man = standardize(manny_signals[row["manny"]]["values"])
        ax.plot(times, src, label=row["source"], linewidth=1.3)
        ax.plot(times, man, label=row["manny"], linewidth=1.1)
        ax.set_title(f"corr={row['corr_zero_lag']:.3f} lag={row['best_lag_seconds']:.3f} step95_ratio={row['step95_ratio_manny_over_source']:.2f}")
        ax.grid(True, alpha=0.25)
        ax.legend(fontsize=8, loc="upper right")
    axes[-1].set_xlabel("capture wall time (s)")
    fig.tight_layout()
    fig.savefig(out_path, dpi=140)
    plt.close(fig)


def make_heatmap(out_path, source_signals, manny_signals, all_rows):
    source_groups = sorted({v["group"] for v in source_signals.values()})
    manny_groups = sorted({v["group"] for v in manny_signals.values()})
    values = np.full((len(source_groups), len(manny_groups)), np.nan, dtype=float)
    for i, sg in enumerate(source_groups):
        for j, mg in enumerate(manny_groups):
            corrs = [
                abs(r["corr_best_lag"])
                for r in all_rows
                if source_signals[r["source"]]["group"] == sg and manny_signals[r["manny"]]["group"] == mg and np.isfinite(r["corr_best_lag"])
            ]
            if corrs:
                values[i, j] = float(np.nanpercentile(corrs, 95))
    fig, ax = plt.subplots(figsize=(max(10, len(manny_groups) * 0.45), max(5, len(source_groups) * 0.45)))
    im = ax.imshow(values, vmin=0.0, vmax=1.0, cmap="viridis")
    ax.set_xticks(range(len(manny_groups)))
    ax.set_xticklabels(manny_groups, rotation=90, fontsize=7)
    ax.set_yticks(range(len(source_groups)))
    ax.set_yticklabels(source_groups, fontsize=8)
    fig.colorbar(im, ax=ax, label="p95 abs best-lag correlation")
    fig.tight_layout()
    fig.savefig(out_path, dpi=150)
    plt.close(fig)


def main():
    parser = argparse.ArgumentParser(description="Compare recorded Holistic pose/face signals with Manny bone transforms.")
    parser.add_argument("input", type=Path)
    parser.add_argument("--output-dir", type=Path, default=None)
    parser.add_argument("--top", type=int, default=300)
    args = parser.parse_args()

    data = json.loads(args.input.read_text(encoding="utf-8"))
    samples = data.get("samples", [])
    if not samples:
        raise SystemExit("No samples found")
    out_dir = args.output_dir or args.input.with_name(args.input.stem + "_holistic_compare")
    out_dir.mkdir(parents=True, exist_ok=True)

    times = np.asarray([float(s.get("wall_t", math.nan)) for s in samples], dtype=float)
    source_signals = extract_source_signals(samples)
    manny_signals = extract_manny_signals(samples)

    expected_rows = []
    for source_name, manny_name in expected_pairs():
        if source_name in source_signals and manny_name in manny_signals:
            expected_rows.append(compare_pair(times, source_name, source_signals[source_name]["values"], manny_name, manny_signals[manny_name]["values"]))

    all_rows = []
    for source_name, source in source_signals.items():
        if p_range(source["values"]) < 1.0e-7:
            continue
        for manny_name, manny in manny_signals.items():
            if p_range(manny["values"]) < 1.0e-7:
                continue
            row = compare_pair(times, source_name, source["values"], manny_name, manny["values"], lag_search=False)
            if np.isfinite(row["corr_best_lag"]):
                all_rows.append(row)
    all_rows.sort(key=lambda r: abs(r["corr_best_lag"]) if np.isfinite(r["corr_best_lag"]) else -1.0, reverse=True)
    top_rows = []
    for row in all_rows[: args.top]:
        top_rows.append(compare_pair(
            times,
            row["source"],
            source_signals[row["source"]]["values"],
            row["manny"],
            manny_signals[row["manny"]]["values"],
            lag_search=True))

    fields = [
        "source", "manny", "source_range_p95_p05", "manny_range_p95_p05", "range_ratio_manny_over_source",
        "corr_zero_lag", "best_lag_seconds", "corr_best_lag",
        "source_step95_standardized", "manny_step95_standardized", "step95_ratio_manny_over_source",
        "source_stepmax_standardized", "manny_stepmax_standardized",
    ]
    write_csv(out_dir / "expected_pairs.csv", expected_rows, fields)
    write_csv(out_dir / "top_correlations.csv", top_rows, fields)

    source_summary = [
        {
            "signal": name,
            "group": obj["group"],
            "range_p95_p05": p_range(obj["values"]),
            "step95_standardized": step_stats(obj["values"])[0],
            "stepmax_standardized": step_stats(obj["values"])[1],
        }
        for name, obj in source_signals.items()
    ]
    manny_summary = [
        {
            "signal": name,
            "group": obj["group"],
            "range_p95_p05": p_range(obj["values"]),
            "step95_standardized": step_stats(obj["values"])[0],
            "stepmax_standardized": step_stats(obj["values"])[1],
        }
        for name, obj in manny_signals.items()
    ]
    source_summary.sort(key=lambda r: r["range_p95_p05"] if np.isfinite(r["range_p95_p05"]) else -1.0, reverse=True)
    manny_summary.sort(key=lambda r: r["range_p95_p05"] if np.isfinite(r["range_p95_p05"]) else -1.0, reverse=True)
    write_csv(out_dir / "source_signal_summary.csv", source_summary, ["signal", "group", "range_p95_p05", "step95_standardized", "stepmax_standardized"])
    write_csv(out_dir / "manny_signal_summary.csv", manny_summary, ["signal", "group", "range_p95_p05", "step95_standardized", "stepmax_standardized"])

    make_expected_chart(out_dir / "expected_pairs_standardized.png", times, source_signals, manny_signals, expected_rows)
    make_heatmap(out_dir / "source_to_manny_region_heatmap.png", source_signals, manny_signals, all_rows)

    report = {
        "input": str(args.input),
        "sample_count": len(samples),
        "duration_seconds": float(np.nanmax(times) - np.nanmin(times)) if len(times) > 1 else 0.0,
        "source_signal_count": len(source_signals),
        "manny_signal_count": len(manny_signals),
        "face_landmark_count_first_sample": samples[0].get("face", {}).get("count", 0),
        "face_has_transform_first_sample": samples[0].get("face", {}).get("has_transform", False),
        "hands_recorded": any("hands" in s for s in samples),
        "expected_pairs": expected_rows,
        "top_correlations_csv": str(out_dir / "top_correlations.csv"),
        "expected_pairs_csv": str(out_dir / "expected_pairs.csv"),
        "source_signal_summary_csv": str(out_dir / "source_signal_summary.csv"),
        "manny_signal_summary_csv": str(out_dir / "manny_signal_summary.csv"),
        "expected_pairs_chart": str(out_dir / "expected_pairs_standardized.png"),
        "region_heatmap": str(out_dir / "source_to_manny_region_heatmap.png"),
    }
    (out_dir / "report.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
