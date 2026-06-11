import argparse
import json
import math
import os
from pathlib import Path

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np


LANDMARKS = {
    "nose", "left_eye", "right_eye", "left_ear", "right_ear", "mouth_left", "mouth_right",
    "left_shoulder", "right_shoulder", "left_elbow", "right_elbow", "left_wrist", "right_wrist",
    "left_pinky", "right_pinky", "left_index", "right_index", "left_thumb", "right_thumb",
    "left_hip", "right_hip", "left_knee", "right_knee", "left_ankle", "right_ankle",
    "left_heel", "right_heel", "left_foot_index", "right_foot_index",
}


def finite_array(values):
    arr = np.asarray(values, dtype=float)
    if arr.ndim == 0:
        arr = arr.reshape(1)
    return arr


def percentile_range(values):
    values = finite_array(values)
    values = values[np.isfinite(values)]
    if values.size == 0:
        return math.nan
    return float(np.nanpercentile(values, 95) - np.nanpercentile(values, 5))


def standardize(values):
    values = finite_array(values)
    out = values.astype(float).copy()
    mask = np.isfinite(out)
    if not np.any(mask):
        return out
    median = np.nanmedian(out[mask])
    spread = np.nanpercentile(out[mask], 95) - np.nanpercentile(out[mask], 5)
    if not np.isfinite(spread) or abs(spread) < 1.0e-8:
        spread = np.nanstd(out[mask])
    if not np.isfinite(spread) or abs(spread) < 1.0e-8:
        spread = 1.0
    out[mask] = (out[mask] - median) / spread
    return out


def safe_corr(a, b):
    a = finite_array(a)
    b = finite_array(b)
    mask = np.isfinite(a) & np.isfinite(b)
    if np.count_nonzero(mask) < 3:
        return math.nan
    aa = a[mask]
    bb = b[mask]
    if np.nanstd(aa) < 1.0e-8 or np.nanstd(bb) < 1.0e-8:
        return math.nan
    return float(np.corrcoef(aa, bb)[0, 1])


def phase_lag_seconds(times, source, manny, max_lag_seconds=0.5):
    times = finite_array(times)
    source = standardize(source)
    manny = standardize(manny)
    mask = np.isfinite(times) & np.isfinite(source) & np.isfinite(manny)
    if np.count_nonzero(mask) < 5:
        return math.nan, math.nan
    t = times[mask]
    s = source[mask]
    m = manny[mask]
    dt = np.nanmedian(np.diff(t))
    if not np.isfinite(dt) or dt <= 0:
        return math.nan, math.nan
    max_lag = max(1, int(round(max_lag_seconds / dt)))
    best_corr = -2.0
    best_lag = 0
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
        if np.isfinite(corr) and corr > best_corr:
            best_corr = corr
            best_lag = lag
    if best_corr < -1.0:
        return math.nan, math.nan
    return float(best_lag * dt), float(best_corr)


def band_power(values):
    z = standardize(values)
    z = z[np.isfinite(z)]
    if z.size < 3:
        return math.nan
    diff = np.diff(z)
    return float(np.nanmean(diff * diff))


def get_lm(sample, name, space="pose_world_landmarks"):
    obj = sample.get(space, {}).get(name)
    if not obj:
        return np.array([math.nan, math.nan, math.nan], dtype=float)
    return np.asarray(obj.get("pos", [math.nan, math.nan, math.nan]), dtype=float)


def midpoint(sample, a, b):
    return (get_lm(sample, a) + get_lm(sample, b)) * 0.5


def distance(sample, a, b):
    return float(np.linalg.norm(get_lm(sample, a) - get_lm(sample, b)))


def angle_degrees(sample, a, b, c):
    va = get_lm(sample, a) - get_lm(sample, b)
    vc = get_lm(sample, c) - get_lm(sample, b)
    na = np.linalg.norm(va)
    nc = np.linalg.norm(vc)
    if na < 1.0e-8 or nc < 1.0e-8:
        return math.nan
    dot = float(np.clip(np.dot(va, vc) / (na * nc), -1.0, 1.0))
    return math.degrees(math.acos(dot))


def bone_value(sample, bone, key, index):
    obj = sample.get("live", {}).get(bone)
    if not obj:
        return math.nan
    values = obj.get(key)
    if not values or len(values) <= index:
        return math.nan
    return float(values[index])


def avg_bone(sample, bones, key, index):
    vals = [bone_value(sample, bone, key, index) for bone in bones]
    vals = [v for v in vals if np.isfinite(v)]
    if not vals:
        return math.nan
    return float(np.mean(vals))


def build_pairs():
    return [
        ("root_pelvis", "hip_mid_height_vs_pelvis_z",
         lambda s: midpoint(s, "left_hip", "right_hip")[2],
         lambda s: bone_value(s, "pelvis", "loc", 2)),
        ("root_pelvis", "hip_width_vs_pelvis_roll",
         lambda s: distance(s, "left_hip", "right_hip"),
         lambda s: bone_value(s, "pelvis", "local_rot", 2)),
        ("torso_spine", "torso_height_vs_spine_pitch",
         lambda s: (midpoint(s, "left_shoulder", "right_shoulder") - midpoint(s, "left_hip", "right_hip"))[2],
         lambda s: avg_bone(s, ["spine_01", "spine_02", "spine_03", "spine_04", "spine_05"], "local_rot", 0)),
        ("torso_spine", "shoulder_mid_depth_vs_spine_yaw",
         lambda s: midpoint(s, "left_shoulder", "right_shoulder")[1] - midpoint(s, "left_hip", "right_hip")[1],
         lambda s: avg_bone(s, ["spine_02", "spine_03", "spine_04"], "local_rot", 1)),
        ("head_neck", "nose_height_vs_head_pitch",
         lambda s: get_lm(s, "nose")[2] - midpoint(s, "left_shoulder", "right_shoulder")[2],
         lambda s: bone_value(s, "head", "local_rot", 0)),
        ("head_neck", "ear_width_vs_head_yaw",
         lambda s: distance(s, "left_ear", "right_ear"),
         lambda s: bone_value(s, "head", "local_rot", 1)),
        ("shoulders_clavicles", "shoulder_lift_vs_clavicle_pitch",
         lambda s: midpoint(s, "left_shoulder", "right_shoulder")[2] - midpoint(s, "left_hip", "right_hip")[2],
         lambda s: avg_bone(s, ["clavicle_l", "clavicle_r"], "local_rot", 0)),
        ("shoulders_clavicles", "shoulder_width_vs_clavicle_roll",
         lambda s: distance(s, "left_shoulder", "right_shoulder"),
         lambda s: avg_bone(s, ["clavicle_l", "clavicle_r"], "local_rot", 2)),
        ("arms", "left_elbow_angle_vs_left_arm_pitch",
         lambda s: angle_degrees(s, "left_shoulder", "left_elbow", "left_wrist"),
         lambda s: avg_bone(s, ["upperarm_l", "lowerarm_l"], "local_rot", 0)),
        ("arms", "right_elbow_angle_vs_right_arm_pitch",
         lambda s: angle_degrees(s, "right_shoulder", "right_elbow", "right_wrist"),
         lambda s: avg_bone(s, ["upperarm_r", "lowerarm_r"], "local_rot", 0)),
        ("hands_wrists", "left_wrist_height_vs_left_hand_z",
         lambda s: get_lm(s, "left_wrist")[2],
         lambda s: bone_value(s, "hand_l", "loc", 2)),
        ("hands_wrists", "right_wrist_height_vs_right_hand_z",
         lambda s: get_lm(s, "right_wrist")[2],
         lambda s: bone_value(s, "hand_r", "loc", 2)),
        ("hips", "hip_mid_depth_vs_pelvis_y",
         lambda s: midpoint(s, "left_hip", "right_hip")[1],
         lambda s: bone_value(s, "pelvis", "loc", 1)),
        ("hips", "hip_width_vs_thigh_roll",
         lambda s: distance(s, "left_hip", "right_hip"),
         lambda s: avg_bone(s, ["thigh_l", "thigh_r"], "local_rot", 2)),
        ("knees_legs", "left_knee_angle_vs_left_leg_pitch",
         lambda s: angle_degrees(s, "left_hip", "left_knee", "left_ankle"),
         lambda s: avg_bone(s, ["thigh_l", "calf_l"], "local_rot", 0)),
        ("knees_legs", "right_knee_angle_vs_right_leg_pitch",
         lambda s: angle_degrees(s, "right_hip", "right_knee", "right_ankle"),
         lambda s: avg_bone(s, ["thigh_r", "calf_r"], "local_rot", 0)),
        ("feet_ankles", "left_foot_pitch_proxy_vs_left_foot_pitch",
         lambda s: get_lm(s, "left_foot_index")[2] - get_lm(s, "left_ankle")[2],
         lambda s: bone_value(s, "foot_l", "local_rot", 0)),
        ("feet_ankles", "right_foot_pitch_proxy_vs_right_foot_pitch",
         lambda s: get_lm(s, "right_foot_index")[2] - get_lm(s, "right_ankle")[2],
         lambda s: bone_value(s, "foot_r", "local_rot", 0)),
    ]


def summarize_conditioning(samples, times):
    rows = [s.get("conditioning", {}) for s in samples if s.get("conditioning")]
    if not rows:
        return {}

    def vals(key):
        return np.asarray([float(r.get(key, math.nan)) for r in rows], dtype=float)

    def pct(key, p):
        arr = vals(key)
        arr = arr[np.isfinite(arr)]
        return float(np.nanpercentile(arr, p)) if arr.size else math.nan

    timestamps = np.asarray([float(s.get("pose_timestamp_us", math.nan)) for s in samples], dtype=float)
    unique_timestamps = np.unique(timestamps[np.isfinite(timestamps)])
    duration = float(np.nanmax(times) - np.nanmin(times)) if len(times) > 1 else 0.0
    unique_rate = float((len(unique_timestamps) - 1) / duration) if duration > 0 and len(unique_timestamps) > 1 else math.nan
    render_dt = np.diff(times)
    render_dt = render_dt[np.isfinite(render_dt) & (render_dt > 0)]
    render_fps = float(1.0 / np.nanmedian(render_dt)) if render_dt.size else math.nan

    repeated = vals("repeated_pose_run_length")
    predicted = np.asarray([bool(r.get("predicted", False)) for r in rows], dtype=bool)
    repeated_bool = repeated > 0
    observed_repeated = []
    last_ts = None
    current_run = 0
    for ts in timestamps:
        if np.isfinite(ts) and last_ts is not None and ts == last_ts:
            current_run += 1
        else:
            current_run = 0
        observed_repeated.append(current_run)
        last_ts = ts if np.isfinite(ts) else None
    observed_repeated = np.asarray(observed_repeated, dtype=float)
    observed_repeated_bool = observed_repeated > 0

    return {
        "render_fps_median": render_fps,
        "source_video_fps_median": pct("source_video_fps", 50),
        "mediapipe_output_fps_median": pct("mediapipe_output_fps", 50),
        "unique_pose_timestamp_fps_from_samples": unique_rate,
        "unique_pose_timestamp_fps_reported_median": pct("unique_pose_timestamp_fps", 50),
        "repeated_pose_run_length_p50": pct("repeated_pose_run_length", 50),
        "repeated_pose_run_length_p95": pct("repeated_pose_run_length", 95),
        "repeated_pose_run_length_max": float(np.nanmax(repeated)) if repeated.size else math.nan,
        "observed_repeated_pose_run_length_p50": float(np.nanpercentile(observed_repeated, 50)) if observed_repeated.size else math.nan,
        "observed_repeated_pose_run_length_p95": float(np.nanpercentile(observed_repeated, 95)) if observed_repeated.size else math.nan,
        "observed_repeated_pose_run_length_max": float(np.nanmax(observed_repeated)) if observed_repeated.size else math.nan,
        "observed_stale_unpredicted_repeated_pose_ratio": float(np.mean(observed_repeated_bool & ~predicted)) if observed_repeated.size and predicted.size == observed_repeated.size else math.nan,
        "predicted_repeated_pose_ratio": float(np.mean(predicted & repeated_bool)) if repeated.size else math.nan,
        "dropped_frame_count_max": pct("dropped_frame_count", 100),
        "timestamp_drift_seconds_p50": pct("timestamp_drift_seconds", 50),
        "timestamp_drift_seconds_p95": pct("timestamp_drift_seconds", 95),
        "prediction_horizon_ms_p50": pct("prediction_horizon_ms", 50),
        "prediction_horizon_ms_p95": pct("prediction_horizon_ms", 95),
        "prediction_horizon_ms_max": pct("prediction_horizon_ms", 100),
        "effective_added_latency_ms_p50": pct("effective_added_latency_ms", 50),
        "effective_added_latency_ms_p95": pct("effective_added_latency_ms", 95),
        "quality_score_p05": pct("quality_score", 5),
        "quality_score_p50": pct("quality_score", 50),
        "mean_landmark_confidence_p50": pct("mean_landmark_confidence", 50),
        "mean_landmark_jitter_p95": pct("mean_landmark_jitter", 95),
        "whole_pose_spike_score_p95": pct("whole_pose_spike_score", 95),
        "region_quality_p50": {
            "root_pelvis": pct("root_pelvis_quality", 50),
            "torso_spine": pct("torso_spine_quality", 50),
            "head_neck": pct("head_neck_quality", 50),
            "shoulders_clavicles": pct("shoulder_clavicle_quality", 50),
            "arms": pct("arms_quality", 50),
            "hands_wrists": pct("hands_wrists_quality", 50),
            "hips": pct("hips_quality", 50),
            "knees_legs": pct("legs_quality", 50),
            "feet_ankles": pct("feet_ankles_quality", 50),
        },
    }


def analyze(input_path, out_dir):
    input_path = Path(input_path)
    out_dir = Path(out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)

    with input_path.open("r", encoding="utf-8") as handle:
        data = json.load(handle)
    samples = data.get("samples", [])
    if not samples:
        raise RuntimeError(f"No samples found in {input_path}")

    times = np.asarray([float(s.get("wall_t", s.get("t", math.nan))) for s in samples], dtype=float)
    time0 = np.nanmin(times)
    times = times - time0

    pair_metrics = []
    grouped = {}
    for group, name, source_fn, manny_fn in build_pairs():
        source = np.asarray([source_fn(s) for s in samples], dtype=float)
        manny = np.asarray([manny_fn(s) for s in samples], dtype=float)
        mask = np.isfinite(source) & np.isfinite(manny)
        if np.count_nonzero(mask) < 5:
            continue
        source_std = standardize(source)
        manny_std = standardize(manny)
        lag, lag_corr = phase_lag_seconds(times, source, manny)
        source_range = percentile_range(source)
        manny_range = percentile_range(manny)
        source_power = band_power(source)
        manny_power = band_power(manny)
        metric = {
            "group": group,
            "signal": name,
            "sample_count": int(np.count_nonzero(mask)),
            "correlation_zero_lag": safe_corr(source_std, manny_std),
            "best_lag_seconds": lag,
            "correlation_at_best_lag": lag_corr,
            "source_p95_p05": source_range,
            "manny_p95_p05": manny_range,
            "amp_ratio_manny_over_source": float(manny_range / source_range) if np.isfinite(source_range) and abs(source_range) > 1.0e-8 else math.nan,
            "band_power_ratio_manny_over_source": float(manny_power / source_power) if np.isfinite(source_power) and abs(source_power) > 1.0e-8 else math.nan,
        }
        pair_metrics.append(metric)
        grouped.setdefault(group, []).append((name, source_std, manny_std, metric))

    chart_paths = {}
    for group, rows in grouped.items():
        fig_height = max(3.0, 2.2 * len(rows))
        fig, axes = plt.subplots(len(rows), 1, figsize=(13, fig_height), sharex=True)
        if len(rows) == 1:
            axes = [axes]
        for ax, (name, source_std, manny_std, metric) in zip(axes, rows):
            ax.plot(times, source_std, label="MediaPipe source standardized", linewidth=1.1)
            ax.plot(times, manny_std, label="Manny bone standardized", linewidth=1.1, alpha=0.85)
            ax.axhline(0.0, color="black", linewidth=0.4, alpha=0.35)
            ax.set_title(
                f"{name}  corr={metric['correlation_zero_lag']:.3f} "
                f"lag={metric['best_lag_seconds']:.3f}s amp={metric['amp_ratio_manny_over_source']:.3f}",
                fontsize=9,
            )
            ax.set_ylabel("std")
            ax.grid(True, alpha=0.2)
        axes[-1].set_xlabel("capture wall time (s)")
        axes[0].legend(loc="upper right", fontsize=8)
        fig.suptitle(f"{group}: standardized full-body source vs Manny signals", fontsize=12)
        fig.tight_layout(rect=[0, 0, 1, 0.96])
        chart_path = out_dir / f"{group}_standardized.png"
        fig.savefig(chart_path, dpi=150)
        plt.close(fig)
        chart_paths[group] = str(chart_path)

    metrics = {
        "input": str(input_path),
        "schema": data.get("schema"),
        "sample_count": len(samples),
        "duration_seconds": float(np.nanmax(times) - np.nanmin(times)) if len(times) > 1 else 0.0,
        "conditioning": summarize_conditioning(samples, times),
        "signals": pair_metrics,
        "charts": chart_paths,
    }
    metrics_path = out_dir / "metrics.json"
    with metrics_path.open("w", encoding="utf-8") as handle:
        json.dump(metrics, handle, indent=2, sort_keys=True)
    return metrics_path, chart_paths


def main():
    parser = argparse.ArgumentParser(description="Generate standardized MediaPipe source vs Manny bone charts from mp.RecordMannyBoneTimeseries JSON.")
    parser.add_argument("input", help="Path to manny_bone_timeseries JSON")
    parser.add_argument("--out-dir", default=None, help="Output directory for metrics and charts")
    args = parser.parse_args()

    input_path = Path(args.input)
    out_dir = Path(args.out_dir) if args.out_dir else input_path.with_suffix("").parent / (input_path.stem + "_analysis")
    metrics_path, chart_paths = analyze(input_path, out_dir)
    print(json.dumps({"metrics": str(metrics_path), "charts": chart_paths}, indent=2, sort_keys=True))


if __name__ == "__main__":
    main()
