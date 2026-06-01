import argparse
import csv
import json
import math
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np


AXES = ("pitch", "yaw", "roll")
ROT_INDEX = {"pitch": 0, "yaw": 1, "roll": 2}
MANNY_HEAD_FORWARD_LOCAL = np.asarray([0.0, 1.0, 0.0], dtype=float)
MANNY_HEAD_UP_LOCAL = np.asarray([1.0, 0.0, 0.0], dtype=float)
MANNY_HEAD_RIGHT_LOCAL = np.asarray([0.0, 0.0, -1.0], dtype=float)
COMPONENT_UP = np.asarray([0.0, 0.0, 1.0], dtype=float)


def nested_get(obj, path, default=math.nan):
    cur = obj
    for key in path:
        if not isinstance(cur, dict) or key not in cur:
            return default
        cur = cur[key]
    return cur


def number(obj, path, default=math.nan):
    value = nested_get(obj, path, default)
    try:
        return float(value)
    except (TypeError, ValueError):
        return default


def array_number(obj, path, index, default=math.nan):
    value = nested_get(obj, path, None)
    if not isinstance(value, list) or index >= len(value):
        return default
    try:
        return float(value[index])
    except (TypeError, ValueError):
        return default


def array_value(obj, path, length):
    value = nested_get(obj, path, None)
    if not isinstance(value, list) or len(value) < length:
        return None
    try:
        out = np.asarray([float(value[index]) for index in range(length)], dtype=float)
    except (TypeError, ValueError):
        return None
    return out if np.all(np.isfinite(out)) else None


def normalize_vector(value):
    value = np.asarray(value, dtype=float)
    length = float(np.linalg.norm(value))
    if not np.isfinite(length) or length <= 1.0e-8:
        return None
    return value / length


def quat_multiply(a, b):
    ax, ay, az, aw = a
    bx, by, bz, bw = b
    return np.asarray(
        [
            aw * bx + ax * bw + ay * bz - az * by,
            aw * by - ax * bz + ay * bw + az * bx,
            aw * bz + ax * by - ay * bx + az * bw,
            aw * bw - ax * bx - ay * by - az * bz,
        ],
        dtype=float,
    )


def quat_inverse(q):
    q = np.asarray(q, dtype=float)
    denom = float(np.dot(q, q))
    if not np.isfinite(denom) or denom <= 1.0e-12:
        return None
    return np.asarray([-q[0], -q[1], -q[2], q[3]], dtype=float) / denom


def quat_normalize(q):
    length = float(np.linalg.norm(q))
    if not np.isfinite(length) or length <= 1.0e-12:
        return None
    return np.asarray(q, dtype=float) / length


def quat_rotate(q, v):
    q = quat_normalize(q)
    if q is None:
        return None
    inv = quat_inverse(q)
    if inv is None:
        return None
    rotated = quat_multiply(quat_multiply(q, np.asarray([v[0], v[1], v[2], 0.0], dtype=float)), inv)[:3]
    return rotated if np.all(np.isfinite(rotated)) else None


def signed_angle_around_axis(base, current, axis):
    axis = normalize_vector(axis)
    base = normalize_vector(base)
    current = normalize_vector(current)
    if axis is None or base is None or current is None:
        return math.nan
    base_proj = base - axis * float(np.dot(base, axis))
    current_proj = current - axis * float(np.dot(current, axis))
    base_proj = normalize_vector(base_proj)
    current_proj = normalize_vector(current_proj)
    if base_proj is None or current_proj is None:
        return math.nan
    return math.degrees(math.atan2(float(np.dot(axis, np.cross(base_proj, current_proj))), float(np.dot(base_proj, current_proj))))


def quat_twist_degrees(delta_quat, axis):
    axis = normalize_vector(axis)
    delta_quat = quat_normalize(delta_quat)
    if axis is None or delta_quat is None:
        return math.nan
    vector = delta_quat[:3]
    projection = axis * float(np.dot(vector, axis))
    twist = quat_normalize(np.asarray([projection[0], projection[1], projection[2], delta_quat[3]], dtype=float))
    if twist is None:
        return 0.0
    angle = 2.0 * math.atan2(float(np.linalg.norm(twist[:3])), float(twist[3]))
    if float(np.dot(twist[:3], axis)) < 0.0:
        angle *= -1.0
    return ((math.degrees(angle) + 180.0) % 360.0) - 180.0


def vector_elevation_degrees(vector):
    vector = normalize_vector(vector)
    if vector is None:
        return math.nan
    return math.degrees(math.atan2(float(vector[2]), math.hypot(float(vector[0]), float(vector[1]))))


def bool_number(obj, path):
    value = nested_get(obj, path, False)
    return 1.0 if bool(value) else 0.0


def series(samples, getter):
    return np.asarray([getter(sample) for sample in samples], dtype=float)


def finite_mask(*arrays):
    if not arrays:
        return np.asarray([], dtype=bool)
    mask = np.ones_like(arrays[0], dtype=bool)
    for arr in arrays:
        mask &= np.isfinite(arr)
    return mask


def unwrap_deg(values):
    values = np.asarray(values, dtype=float)
    out = values.copy()
    mask = np.isfinite(out)
    if np.count_nonzero(mask) < 2:
        return out
    idx = np.flatnonzero(mask)
    out[idx] = np.rad2deg(np.unwrap(np.deg2rad(out[idx])))
    return out


def zero_to_first(values, baseline_index=None):
    values = unwrap_deg(values)
    mask = np.isfinite(values)
    if not np.any(mask):
        return values
    if baseline_index is not None and 0 <= baseline_index < values.size and np.isfinite(values[baseline_index]):
        return values - values[baseline_index]
    return values - values[np.flatnonzero(mask)[0]]


def safe_corr(a, b):
    a = np.asarray(a, dtype=float)
    b = np.asarray(b, dtype=float)
    mask = finite_mask(a, b)
    if np.count_nonzero(mask) < 5:
        return math.nan
    aa = a[mask]
    bb = b[mask]
    if np.nanstd(aa) < 1.0e-8 or np.nanstd(bb) < 1.0e-8:
        return math.nan
    return float(np.corrcoef(aa, bb)[0, 1])


def rms(values):
    values = np.asarray(values, dtype=float)
    values = values[np.isfinite(values)]
    if values.size == 0:
        return math.nan
    return float(np.sqrt(np.nanmean(values * values)))


def peak_abs(values):
    values = np.asarray(values, dtype=float)
    values = values[np.isfinite(values)]
    if values.size == 0:
        return math.nan
    return float(np.nanmax(np.abs(values)))


def finite_values(values):
    values = np.asarray(values, dtype=float)
    return values[np.isfinite(values)]


def safe_nanmedian(values):
    values = finite_values(values)
    if values.size == 0:
        return math.nan
    return float(np.nanmedian(values))


def safe_nanmax(values):
    values = finite_values(values)
    if values.size == 0:
        return math.nan
    return float(np.nanmax(values))


def best_lag_seconds(times, source, output, max_lag_seconds=0.75):
    times = np.asarray(times, dtype=float)
    source = np.asarray(source, dtype=float)
    output = np.asarray(output, dtype=float)
    mask = finite_mask(times, source, output)
    if np.count_nonzero(mask) < 8:
        return math.nan, math.nan
    t = times[mask]
    s = source[mask]
    o = output[mask]
    dt = np.nanmedian(np.diff(t))
    if not np.isfinite(dt) or dt <= 0:
        return math.nan, math.nan
    s = s - np.nanmean(s)
    o = o - np.nanmean(o)
    if np.nanstd(s) < 1.0e-8 or np.nanstd(o) < 1.0e-8:
        return math.nan, math.nan
    max_lag = max(1, int(round(max_lag_seconds / dt)))
    best_corr = -2.0
    best_lag = 0
    for lag in range(-max_lag, max_lag + 1):
        if lag < 0:
            ss = s[-lag:]
            oo = o[:lag]
        elif lag > 0:
            ss = s[:-lag]
            oo = o[lag:]
        else:
            ss = s
            oo = o
        corr = safe_corr(ss, oo)
        if np.isfinite(corr) and corr > best_corr:
            best_corr = corr
            best_lag = lag
    if best_corr < -1.0:
        return math.nan, math.nan
    return float(best_lag * dt), float(best_corr)


def slope_gain(source, output):
    source = np.asarray(source, dtype=float)
    output = np.asarray(output, dtype=float)
    mask = finite_mask(source, output)
    if np.count_nonzero(mask) < 5:
        return math.nan
    x = source[mask]
    y = output[mask]
    var = float(np.nanvar(x))
    if var < 1.0e-8:
        return math.nan
    return float(np.nanmean((x - np.nanmean(x)) * (y - np.nanmean(y))) / var)


def divergence_windows(times, error, threshold):
    times = np.asarray(times, dtype=float)
    error = np.asarray(error, dtype=float)
    bad = np.isfinite(times) & np.isfinite(error) & (np.abs(error) >= threshold)
    windows = []
    start = None
    peak = 0.0
    peak_t = math.nan
    for index, is_bad in enumerate(bad):
        if is_bad and start is None:
            start = index
            peak = abs(float(error[index]))
            peak_t = float(times[index])
        elif is_bad:
            current = abs(float(error[index]))
            if current > peak:
                peak = current
                peak_t = float(times[index])
        elif start is not None:
            windows.append(
                {
                    "start_s": float(times[start]),
                    "end_s": float(times[index - 1]),
                    "peak_abs_deg": peak,
                    "peak_t_s": peak_t,
                }
            )
            start = None
    if start is not None:
        windows.append(
            {
                "start_s": float(times[start]),
                "end_s": float(times[-1]),
                "peak_abs_deg": peak,
                "peak_t_s": peak_t,
            }
        )
    return windows[:10]


def clean_name(name):
    return name.replace("_deg", "").replace("_delta", "").replace("_local", "")


def load_trace(path):
    with open(path, "r", encoding="utf-8") as handle:
        root = json.load(handle)
    samples = root.get("samples", [])
    if not samples:
        raise RuntimeError(f"No samples in {path}")
    t = series(samples, lambda s: number(s, ("wall_t",)))
    if not np.any(np.isfinite(t)):
        t = series(samples, lambda s: number(s, ("t",)))
    first = t[np.flatnonzero(np.isfinite(t))[0]]
    t = t - first
    return root, samples, t


def build_columns(samples, t):
    pose_available = series(samples, lambda s: 1.0 if s.get("pose_available") else 0.0)
    dense_valid = series(samples, lambda s: bool_number(s, ("solver", "head", "dense_head_local_target_valid")))
    baseline_candidates = np.flatnonzero(
        np.isfinite(pose_available)
        & np.isfinite(dense_valid)
        & (pose_available > 0.5)
        & (dense_valid > 0.5)
    )
    baseline_index = int(baseline_candidates[0]) if baseline_candidates.size else None

    source = {}
    for axis in AXES:
        source[f"target_{axis}_deg"] = zero_to_first(
            series(samples, lambda s, a=axis: number(s, ("solver", "head", f"computed_{a}_deg"))),
            baseline_index,
        )
        source[f"dense_local_{axis}_deg"] = zero_to_first(
            series(samples, lambda s, a=axis: number(s, ("solver", "head", f"dense_head_local_{a}_deg"))),
            baseline_index,
        )
        source[f"dense_applied_{axis}_deg"] = zero_to_first(
            series(samples, lambda s, a=axis: number(s, ("solver", "head", f"dense_head_{a}_applied_deg"))),
            baseline_index,
        )
        source[f"screen_{axis}_deg"] = zero_to_first(
            series(samples, lambda s, a=axis: number(s, ("solver", "head", f"screen_{a}_deg"))),
            baseline_index,
        )

    source["screen_face_pitch_input"] = series(samples, lambda s: number(s, ("solver", "head", "screen_face_pitch_input")))
    source["screen_lateral_angle_delta_deg"] = series(
        samples, lambda s: number(s, ("solver", "head", "screen_lateral_angle_delta_deg"))
    )
    source["screen_side_bend_deg"] = zero_to_first(
        series(samples, lambda s: number(s, ("solver", "head", "screen_side_bend_deg"))),
        baseline_index,
    )
    source["dense_face_pitch_delta"] = series(samples, lambda s: number(s, ("solver", "head", "dense_face_pitch_delta")))
    source["dense_face_yaw_delta"] = series(samples, lambda s: number(s, ("solver", "head", "dense_face_yaw_delta")))
    source["dense_face_roll_delta_deg"] = zero_to_first(
        series(samples, lambda s: number(s, ("solver", "head", "dense_face_roll_delta_deg"))),
        baseline_index,
    )
    source["world_forward_pitch_delta_deg"] = zero_to_first(
        series(samples, lambda s: number(s, ("solver", "head", "world_forward_pitch_delta_deg"))),
        baseline_index,
    )
    source["has_dense_face"] = series(samples, lambda s: bool_number(s, ("solver", "head", "has_dense_face")))
    source["dense_target_valid"] = dense_valid

    manny = {}
    bones = ("head", "neck_01", "neck_02")
    for bone in bones:
        for axis in AXES:
            index = ROT_INDEX[axis]
            manny[f"{bone}_local_{axis}_deg"] = zero_to_first(
                series(samples, lambda s, b=bone, i=index: array_number(s, ("live", b, "local_rot"), i)),
                baseline_index,
            )
            manny[f"{bone}_component_{axis}_deg"] = zero_to_first(
                series(samples, lambda s, b=bone, i=index: array_number(s, ("live", b, "rot"), i)),
                baseline_index,
            )
    for axis in AXES:
        manny[f"chain_local_{axis}_deg"] = (
            manny[f"neck_01_local_{axis}_deg"]
            + manny[f"neck_02_local_{axis}_deg"]
            + manny[f"head_local_{axis}_deg"]
        )

    baseline_quat = None
    if baseline_index is not None:
        baseline_quat = array_value(samples[baseline_index], ("live", "head", "quat"), 4)
    if baseline_quat is None:
        for sample in samples:
            baseline_quat = array_value(sample, ("live", "head", "quat"), 4)
            if baseline_quat is not None:
                break

    visual_pitch = np.full(len(samples), math.nan, dtype=float)
    visual_yaw = np.full(len(samples), math.nan, dtype=float)
    visual_roll = np.full(len(samples), math.nan, dtype=float)
    if baseline_quat is not None:
        baseline_forward = quat_rotate(baseline_quat, MANNY_HEAD_FORWARD_LOCAL)
        baseline_forward = normalize_vector(baseline_forward) if baseline_forward is not None else None
        baseline_forward_elevation = vector_elevation_degrees(baseline_forward) if baseline_forward is not None else math.nan
        baseline_inverse = quat_inverse(baseline_quat)
        if baseline_forward is not None and baseline_inverse is not None:
            for index, sample in enumerate(samples):
                head_quat = array_value(sample, ("live", "head", "quat"), 4)
                if head_quat is None:
                    continue
                current_forward = quat_rotate(head_quat, MANNY_HEAD_FORWARD_LOCAL)
                if current_forward is None:
                    continue
                visual_pitch[index] = vector_elevation_degrees(current_forward) - baseline_forward_elevation
                visual_yaw[index] = signed_angle_around_axis(baseline_forward, current_forward, COMPONENT_UP)
                delta_quat = quat_multiply(head_quat, baseline_inverse)
                visual_roll[index] = quat_twist_degrees(delta_quat, baseline_forward)

    manny["head_visual_pitch_deg"] = unwrap_deg(visual_pitch)
    manny["head_visual_yaw_deg"] = unwrap_deg(visual_yaw)
    manny["head_visual_roll_deg"] = unwrap_deg(visual_roll)

    quality = {
        "pose_available": pose_available,
        "head_neck_quality": series(samples, lambda s: number(s, ("conditioning", "head_neck_quality"))),
        "mean_landmark_confidence": series(samples, lambda s: number(s, ("conditioning", "mean_landmark_confidence"))),
        "source_age_ms": series(samples, lambda s: number(s, ("conditioning", "source_age_ms"))),
        "prediction_horizon_ms": series(samples, lambda s: number(s, ("conditioning", "prediction_horizon_ms"))),
        "repeated_pose_run_length": series(samples, lambda s: number(s, ("conditioning", "repeated_pose_run_length"))),
        "capture_width": series(samples, lambda s: number(s, ("pipeline", "last_capture_width"))),
        "capture_height": series(samples, lambda s: number(s, ("pipeline", "last_capture_height"))),
        "inference_width": series(samples, lambda s: number(s, ("pipeline", "last_inference_width"))),
        "inference_height": series(samples, lambda s: number(s, ("pipeline", "last_inference_height"))),
    }

    return source, manny, quality


def write_csv(path, t, source, manny, quality):
    fields = ["t"] + list(source.keys()) + list(manny.keys()) + list(quality.keys())
    with open(path, "w", newline="", encoding="utf-8") as handle:
        writer = csv.writer(handle)
        writer.writerow(fields)
        for index in range(len(t)):
            row = [t[index]]
            for group in (source, manny, quality):
                row.extend(group[name][index] for name in group)
            writer.writerow(row)


def build_summary(root, samples, t, source, manny, quality):
    metrics = {}
    source_targets = {axis: source[f"target_{axis}_deg"] for axis in AXES}
    output_axes = {
        "visual_pitch": manny["head_visual_pitch_deg"],
        "visual_yaw": manny["head_visual_yaw_deg"],
        "visual_roll": manny["head_visual_roll_deg"],
        "head_pitch": manny["head_local_pitch_deg"],
        "head_yaw": manny["head_local_yaw_deg"],
        "head_roll": manny["head_local_roll_deg"],
        "chain_pitch": manny["chain_local_pitch_deg"],
        "chain_yaw": manny["chain_local_yaw_deg"],
        "chain_roll": manny["chain_local_roll_deg"],
        "neck01_pitch": manny["neck_01_local_pitch_deg"],
        "neck01_yaw": manny["neck_01_local_yaw_deg"],
        "neck01_roll": manny["neck_01_local_roll_deg"],
        "neck02_pitch": manny["neck_02_local_pitch_deg"],
        "neck02_yaw": manny["neck_02_local_yaw_deg"],
        "neck02_roll": manny["neck_02_local_roll_deg"],
    }

    corr_matrix = {}
    for source_name, source_values in source_targets.items():
        corr_matrix[source_name] = {}
        for output_name, output_values in output_axes.items():
            corr_matrix[source_name][output_name] = safe_corr(source_values, output_values)

    for axis in AXES:
        target = source_targets[axis]
        head = manny[f"head_local_{axis}_deg"]
        chain = manny[f"chain_local_{axis}_deg"]
        visual = manny[f"head_visual_{axis}_deg"]
        head_error = target - head
        chain_error = target - chain
        visual_error = target - visual
        threshold = max(8.0, float(np.nanpercentile(np.abs(target[np.isfinite(target)]), 90)) * 0.35 if np.any(np.isfinite(target)) else 8.0)
        lag_s, lag_corr = best_lag_seconds(t, target, head)
        visual_lag_s, visual_lag_corr = best_lag_seconds(t, target, visual)
        best_output = None
        best_abs_corr = -1.0
        for output_name, corr in corr_matrix[axis].items():
            if np.isfinite(corr) and abs(corr) > best_abs_corr:
                best_abs_corr = abs(corr)
                best_output = {"name": output_name, "correlation": corr}
        metrics[axis] = {
            "same_axis_head_correlation": safe_corr(target, head),
            "same_axis_chain_correlation": safe_corr(target, chain),
            "same_axis_visual_correlation": safe_corr(target, visual),
            "head_gain": slope_gain(target, head),
            "chain_gain": slope_gain(target, chain),
            "visual_gain": slope_gain(target, visual),
            "head_best_lag_ms_positive_means_manny_lags": lag_s * 1000.0 if np.isfinite(lag_s) else math.nan,
            "head_best_lag_correlation": lag_corr,
            "visual_best_lag_ms_positive_means_manny_lags": visual_lag_s * 1000.0 if np.isfinite(visual_lag_s) else math.nan,
            "visual_best_lag_correlation": visual_lag_corr,
            "head_rms_error_deg": rms(head_error),
            "chain_rms_error_deg": rms(chain_error),
            "visual_rms_error_deg": rms(visual_error),
            "head_peak_abs_error_deg": peak_abs(head_error),
            "chain_peak_abs_error_deg": peak_abs(chain_error),
            "visual_peak_abs_error_deg": peak_abs(visual_error),
            "divergence_threshold_deg": threshold,
            "head_divergence_windows": divergence_windows(t, head_error, threshold),
            "visual_divergence_windows": divergence_windows(t, visual_error, threshold),
            "best_matching_manny_output": best_output,
        }

    duration = float(np.nanmax(t) - np.nanmin(t)) if len(t) > 1 else 0.0
    dt = np.diff(t)
    dt = dt[np.isfinite(dt) & (dt > 0)]
    summary = {
        "schema": "manny_head_trace_analysis_v1",
        "input_sample_count": len(samples),
        "recorded_duration_s": duration,
        "median_sample_rate_hz": float(1.0 / np.nanmedian(dt)) if dt.size else math.nan,
        "source_json_schema": root.get("schema"),
        "auto_started": root.get("auto_started"),
        "metrics": metrics,
        "correlation_matrix": corr_matrix,
        "quality": {
            "pose_available_ratio": float(np.nanmean(quality["pose_available"])),
            "median_head_neck_quality": safe_nanmedian(quality["head_neck_quality"]),
            "median_landmark_confidence": safe_nanmedian(quality["mean_landmark_confidence"]),
            "median_source_age_ms": safe_nanmedian(quality["source_age_ms"]),
            "median_prediction_horizon_ms": safe_nanmedian(quality["prediction_horizon_ms"]),
            "max_repeated_pose_run_length": safe_nanmax(quality["repeated_pose_run_length"]),
            "capture_size": [
                safe_nanmedian(quality["capture_width"]),
                safe_nanmedian(quality["capture_height"]),
            ],
            "inference_size": [
                safe_nanmedian(quality["inference_width"]),
                safe_nanmedian(quality["inference_height"]),
            ],
        },
    }

    findings = []
    for axis, data in metrics.items():
        same = data["same_axis_head_correlation"]
        visual_same = data["same_axis_visual_correlation"]
        best = data["best_matching_manny_output"]
        if best and best["name"] != f"visual_{axis}" and abs(best["correlation"]) >= 0.45 and (
            not np.isfinite(visual_same) or abs(visual_same) < 0.45
        ):
            findings.append(
                f"{axis}: source target best matches {best['name']} corr={best['correlation']:.3f}, not visual_{axis}; possible axis mix."
            )
        if np.isfinite(visual_same) and visual_same <= -0.35:
            findings.append(f"{axis}: same-axis visual correlation is negative ({visual_same:.3f}); possible sign inversion.")
        elif np.isfinite(same) and same <= -0.35:
            findings.append(
                f"{axis}: local Euler same-axis correlation is negative ({same:.3f}); check visual axis metrics before treating this as a sign inversion."
            )
        visual_peak = data["visual_peak_abs_error_deg"]
        if np.isfinite(visual_peak) and visual_peak >= data["divergence_threshold_deg"]:
            windows = data["visual_divergence_windows"]
            if windows:
                first = windows[0]
                findings.append(
                    f"{axis}: first visual divergence {first['start_s']:.2f}-{first['end_s']:.2f}s, peak {first['peak_abs_deg']:.1f} deg at {first['peak_t_s']:.2f}s."
                )
        if data["head_peak_abs_error_deg"] >= data["divergence_threshold_deg"]:
            windows = data["head_divergence_windows"]
            if windows:
                first = windows[0]
                findings.append(
                    f"{axis}: first local-Euler divergence {first['start_s']:.2f}-{first['end_s']:.2f}s, peak {first['peak_abs_deg']:.1f} deg at {first['peak_t_s']:.2f}s."
                )
        chain_gain = data["chain_gain"]
        chain_peak = data["chain_peak_abs_error_deg"]
        if np.isfinite(chain_gain) and abs(chain_gain - 1.0) >= 0.30:
            findings.append(
                f"{axis}: visible neck+head chain gain is {chain_gain:.3f}x; neck_01/neck_02 are adding extra motion on top of the head target."
            )
        if np.isfinite(chain_peak) and chain_peak >= data["divergence_threshold_deg"]:
            findings.append(
                f"{axis}: visible neck+head chain peak error is {chain_peak:.1f} deg even though the head bone itself tracks the target."
            )
    if not findings:
        findings.append("No same-axis sign inversion or threshold divergence was detected in this recording.")
    summary["findings"] = findings
    return summary


def plot_signals(path, t, source, manny):
    fig, axes = plt.subplots(3, 1, figsize=(14, 10), sharex=True)
    for row, axis_name in enumerate(AXES):
        ax = axes[row]
        ax.plot(t, source[f"target_{axis_name}_deg"], label=f"source target {axis_name}", linewidth=2.0)
        ax.plot(t, manny[f"head_visual_{axis_name}_deg"], label=f"Manny visual {axis_name}", linewidth=1.8)
        ax.plot(t, manny[f"head_local_{axis_name}_deg"], label=f"Manny head local {axis_name}", linewidth=1.6)
        ax.plot(t, manny[f"chain_local_{axis_name}_deg"], label=f"Manny neck+head {axis_name}", linewidth=1.0, alpha=0.8)
        ax.axhline(0.0, color="black", linewidth=0.6)
        ax.set_ylabel("deg")
        ax.set_title(f"{axis_name.title()} source target vs Manny output")
        ax.grid(True, alpha=0.3)
        ax.legend(loc="upper right")
    axes[-1].set_xlabel("seconds")
    fig.tight_layout()
    fig.savefig(path, dpi=150)
    plt.close(fig)


def plot_errors(path, t, source, manny, summary):
    fig, axes = plt.subplots(3, 1, figsize=(14, 10), sharex=True)
    for row, axis_name in enumerate(AXES):
        ax = axes[row]
        error = source[f"target_{axis_name}_deg"] - manny[f"head_local_{axis_name}_deg"]
        threshold = summary["metrics"][axis_name]["divergence_threshold_deg"]
        ax.plot(t, error, label=f"target - Manny head {axis_name}", linewidth=1.8)
        ax.axhline(threshold, color="red", linestyle="--", linewidth=0.9)
        ax.axhline(-threshold, color="red", linestyle="--", linewidth=0.9)
        for window in summary["metrics"][axis_name]["head_divergence_windows"]:
            ax.axvspan(window["start_s"], window["end_s"], color="red", alpha=0.12)
        ax.axhline(0.0, color="black", linewidth=0.6)
        ax.set_ylabel("deg")
        ax.set_title(f"{axis_name.title()} divergence")
        ax.grid(True, alpha=0.3)
        ax.legend(loc="upper right")
    axes[-1].set_xlabel("seconds")
    fig.tight_layout()
    fig.savefig(path, dpi=150)
    plt.close(fig)


def plot_sources(path, t, source):
    fig, axes = plt.subplots(4, 1, figsize=(14, 12), sharex=True)
    for axis_name in AXES:
        axes[0].plot(t, source[f"target_{axis_name}_deg"], label=f"target {axis_name}")
        axes[1].plot(t, source[f"dense_local_{axis_name}_deg"], label=f"dense local {axis_name}")
        axes[2].plot(t, source[f"screen_{axis_name}_deg"], label=f"screen {axis_name}")
    axes[3].plot(t, source["dense_face_pitch_delta"], label="dense face pitch delta")
    axes[3].plot(t, source["dense_face_yaw_delta"], label="dense face yaw delta")
    axes[3].plot(t, source["dense_face_roll_delta_deg"], label="dense face roll delta deg")
    axes[3].plot(t, source["screen_face_pitch_input"], label="screen face pitch input", alpha=0.8)
    for ax in axes:
        ax.grid(True, alpha=0.3)
        ax.legend(loc="upper right", ncols=2)
    axes[0].set_title("Final source target signals")
    axes[1].set_title("Dense local head solve signals")
    axes[2].set_title("Sparse screen head solve signals")
    axes[3].set_title("Raw face proxy signals")
    axes[-1].set_xlabel("seconds")
    fig.tight_layout()
    fig.savefig(path, dpi=150)
    plt.close(fig)


def plot_manny_bones(path, t, manny):
    fig, axes = plt.subplots(3, 1, figsize=(14, 10), sharex=True)
    for row, axis_name in enumerate(AXES):
        ax = axes[row]
        ax.plot(t, manny[f"neck_01_local_{axis_name}_deg"], label=f"neck_01 {axis_name}")
        ax.plot(t, manny[f"neck_02_local_{axis_name}_deg"], label=f"neck_02 {axis_name}")
        ax.plot(t, manny[f"head_local_{axis_name}_deg"], label=f"head {axis_name}", linewidth=2.0)
        ax.plot(t, manny[f"chain_local_{axis_name}_deg"], label=f"chain {axis_name}", linewidth=1.2)
        ax.axhline(0.0, color="black", linewidth=0.6)
        ax.set_ylabel("deg")
        ax.set_title(f"Manny local bone {axis_name}")
        ax.grid(True, alpha=0.3)
        ax.legend(loc="upper right", ncols=2)
    axes[-1].set_xlabel("seconds")
    fig.tight_layout()
    fig.savefig(path, dpi=150)
    plt.close(fig)


def plot_quality(path, t, quality):
    fig, axes = plt.subplots(4, 1, figsize=(14, 11), sharex=True)
    axes[0].plot(t, quality["head_neck_quality"], label="head_neck_quality")
    axes[0].plot(t, quality["mean_landmark_confidence"], label="mean_landmark_confidence")
    axes[1].plot(t, quality["source_age_ms"], label="source_age_ms")
    axes[1].plot(t, quality["prediction_horizon_ms"], label="prediction_horizon_ms")
    axes[2].plot(t, quality["repeated_pose_run_length"], label="repeated_pose_run_length")
    axes[2].plot(t, quality["pose_available"], label="pose_available")
    axes[3].plot(t, quality["capture_width"], label="capture_width")
    axes[3].plot(t, quality["capture_height"], label="capture_height")
    axes[3].plot(t, quality["inference_width"], label="inference_width")
    axes[3].plot(t, quality["inference_height"], label="inference_height")
    for ax in axes:
        ax.grid(True, alpha=0.3)
        ax.legend(loc="upper right", ncols=2)
    axes[0].set_title("Tracking quality")
    axes[1].set_title("Latency")
    axes[2].set_title("Pose availability/repeats")
    axes[3].set_title("Capture and inference size")
    axes[-1].set_xlabel("seconds")
    fig.tight_layout()
    fig.savefig(path, dpi=150)
    plt.close(fig)


def plot_correlation(path, summary):
    corr = summary["correlation_matrix"]
    rows = list(corr.keys())
    cols = sorted({name for row in corr.values() for name in row.keys()})
    data = np.asarray([[corr[row].get(col, math.nan) for col in cols] for row in rows], dtype=float)
    fig, ax = plt.subplots(figsize=(16, 5))
    image = ax.imshow(data, vmin=-1.0, vmax=1.0, cmap="coolwarm", aspect="auto")
    ax.set_xticks(np.arange(len(cols)))
    ax.set_xticklabels([clean_name(c) for c in cols], rotation=45, ha="right")
    ax.set_yticks(np.arange(len(rows)))
    ax.set_yticklabels(rows)
    for y in range(data.shape[0]):
        for x in range(data.shape[1]):
            value = data[y, x]
            if np.isfinite(value):
                ax.text(x, y, f"{value:.2f}", ha="center", va="center", fontsize=8)
    ax.set_title("Correlation: source target axis vs Manny output axis")
    fig.colorbar(image, ax=ax, label="correlation")
    fig.tight_layout()
    fig.savefig(path, dpi=150)
    plt.close(fig)


def write_text_summary(path, summary):
    lines = [
        "Manny head trace divergence summary",
        f"samples: {summary['input_sample_count']}",
        f"duration_s: {summary['recorded_duration_s']:.3f}",
        f"median_sample_rate_hz: {summary['median_sample_rate_hz']:.3f}",
        "",
        "Quality:",
    ]
    for key, value in summary["quality"].items():
        lines.append(f"  {key}: {value}")
    lines.extend(["", "Axis metrics:"])
    for axis_name in AXES:
        data = summary["metrics"][axis_name]
        best = data["best_matching_manny_output"]
        best_text = f"{best['name']} corr={best['correlation']:.3f}" if best else "none"
        lines.append(
            "  "
            + axis_name
            + f": same_corr={data['same_axis_head_correlation']:.3f}, "
            + f"visual_corr={data['same_axis_visual_correlation']:.3f}, "
            + f"gain={data['head_gain']:.3f}, "
            + f"visual_gain={data['visual_gain']:.3f}, "
            + f"lag_ms={data['head_best_lag_ms_positive_means_manny_lags']:.1f}, "
            + f"visual_lag_ms={data['visual_best_lag_ms_positive_means_manny_lags']:.1f}, "
            + f"rms_error_deg={data['head_rms_error_deg']:.2f}, "
            + f"visual_rms_error_deg={data['visual_rms_error_deg']:.2f}, "
            + f"peak_error_deg={data['head_peak_abs_error_deg']:.2f}, "
            + f"visual_peak_error_deg={data['visual_peak_abs_error_deg']:.2f}, "
            + f"best={best_text}"
        )
    lines.extend(["", "Findings:"])
    lines.extend(f"  {finding}" for finding in summary["findings"])
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("trace_json", type=Path)
    parser.add_argument("--out-dir", type=Path, default=None)
    args = parser.parse_args()

    root, samples, t = load_trace(args.trace_json)
    out_dir = args.out_dir or args.trace_json.parent
    out_dir.mkdir(parents=True, exist_ok=True)
    stem = args.trace_json.stem

    source, manny, quality = build_columns(samples, t)
    summary = build_summary(root, samples, t, source, manny, quality)

    csv_path = out_dir / f"{stem}.csv"
    summary_json_path = out_dir / f"{stem}_summary.json"
    summary_txt_path = out_dir / f"{stem}_summary.txt"
    signals_path = out_dir / f"{stem}_signals.png"
    errors_path = out_dir / f"{stem}_errors.png"
    sources_path = out_dir / f"{stem}_sources.png"
    bones_path = out_dir / f"{stem}_manny_bones.png"
    quality_path = out_dir / f"{stem}_quality.png"
    corr_path = out_dir / f"{stem}_correlation.png"

    write_csv(csv_path, t, source, manny, quality)
    summary_json_path.write_text(json.dumps(summary, indent=2, allow_nan=True), encoding="utf-8")
    write_text_summary(summary_txt_path, summary)
    plot_signals(signals_path, t, source, manny)
    plot_errors(errors_path, t, source, manny, summary)
    plot_sources(sources_path, t, source)
    plot_manny_bones(bones_path, t, manny)
    plot_quality(quality_path, t, quality)
    plot_correlation(corr_path, summary)

    print(f"CSV={csv_path}")
    print(f"SUMMARY_JSON={summary_json_path}")
    print(f"SUMMARY_TXT={summary_txt_path}")
    print(f"PLOT_SIGNALS={signals_path}")
    print(f"PLOT_ERRORS={errors_path}")
    print(f"PLOT_SOURCES={sources_path}")
    print(f"PLOT_MANNY_BONES={bones_path}")
    print(f"PLOT_QUALITY={quality_path}")
    print(f"PLOT_CORRELATION={corr_path}")


if __name__ == "__main__":
    main()
