#!/usr/bin/env python3
"""Analyze MPQ diagnostic capture JSON with separate MediaPipe-only and fused shadow lanes."""

from __future__ import annotations

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

AXES = {"x": 0, "y": 1, "z": 2}
ROT_AXES = {"pitch": 0, "yaw": 1, "roll": 2}
MIN_SIGNAL_SAMPLES = 5
NOT_VALID_STATUSES = {
    "not_recorded",
    "source_unavailable",
    "flat_expected_stage0",
    "flat_unexpected",
    "not_comparable_coordinate_space",
    "derived_unavailable",
}

LANDMARK_SPACES = {
    "mp_world": ["pose_world_landmarks"],
    "mp_norm": ["pose_normalized_landmarks"],
    "mp_body": ["fusion", "source", "body_pose", "landmarks"],
}

TIMING_MS_FIELDS = [
    "capture_to_enqueue_ms",
    "enqueue_to_worker_start_ms",
    "native_process_ms",
    "get_landmarks_ms",
    "capture_to_publish_ms",
    "publish_to_sample_ms",
    "capture_to_sample_ms",
]

PIPELINE_AGGREGATES = [
    ("component_conversion", "component_conversion_count", "component_conversion_total_ms", "component_conversion_max_ms"),
    (
        "component_readback_latency",
        "component_readback_latency_sample_count",
        "component_readback_latency_total_ms",
        "component_readback_latency_max_ms",
    ),
    ("worker_queue_latency", "worker_queue_latency_sample_count", "worker_queue_latency_total_ms", "worker_queue_latency_max_ms"),
    (
        "worker_native_process",
        "worker_native_process_sample_count",
        "worker_native_process_total_ms",
        "worker_native_process_max_ms",
    ),
    (
        "worker_get_landmarks",
        "worker_get_landmarks_sample_count",
        "worker_get_landmarks_total_ms",
        "worker_get_landmarks_max_ms",
    ),
]

CONDITIONING_FIELDS = [
    "source_video_fps",
    "mediapipe_output_fps",
    "unique_pose_timestamp_fps",
    "source_age_ms",
    "prediction_horizon_ms",
    "effective_added_latency_ms",
    "quality_score",
    "mean_landmark_confidence",
    "mean_landmark_jitter",
    "max_landmark_jitter",
    "whole_pose_spike_score",
    "root_pelvis_quality",
    "torso_spine_quality",
    "head_neck_quality",
    "shoulder_clavicle_quality",
    "arms_quality",
    "hands_wrists_quality",
    "hips_quality",
    "legs_quality",
    "feet_ankles_quality",
]


def finite(values):
    arr = np.asarray(values, dtype=float)
    return arr[np.isfinite(arr)]


def stats(values):
    clean = finite(values)
    if clean.size == 0:
        return {
            "count": 0,
            "min": math.nan,
            "p05": math.nan,
            "p50": math.nan,
            "p95": math.nan,
            "max": math.nan,
            "mean": math.nan,
        }
    return {
        "count": int(clean.size),
        "min": float(np.nanmin(clean)),
        "p05": float(np.nanpercentile(clean, 5)),
        "p50": float(np.nanpercentile(clean, 50)),
        "p95": float(np.nanpercentile(clean, 95)),
        "max": float(np.nanmax(clean)),
        "mean": float(np.nanmean(clean)),
    }


def percentile_range(values):
    summary = stats(values)
    if summary["count"] == 0:
        return math.nan
    return float(summary["p95"] - summary["p05"])


def standardize(values):
    arr = np.asarray(values, dtype=float)
    out = arr.copy()
    mask = np.isfinite(out)
    if not np.any(mask):
        return out
    med = float(np.nanmedian(out[mask]))
    spread = percentile_range(out[mask])
    if not np.isfinite(spread) or abs(spread) < 1.0e-9:
        spread = float(np.nanstd(out[mask]))
    if not np.isfinite(spread) or abs(spread) < 1.0e-9:
        spread = 1.0
    out[mask] = (out[mask] - med) / spread
    return out


def safe_corr(a, b):
    aa = np.asarray(a, dtype=float)
    bb = np.asarray(b, dtype=float)
    mask = np.isfinite(aa) & np.isfinite(bb)
    if np.count_nonzero(mask) < 5:
        return math.nan
    aa = aa[mask]
    bb = bb[mask]
    if np.nanstd(aa) < 1.0e-9 or np.nanstd(bb) < 1.0e-9:
        return math.nan
    return float(np.corrcoef(aa, bb)[0, 1])


def best_lag(times, source, target, max_lag_seconds=0.75, maximize_abs=False):
    t = np.asarray(times, dtype=float)
    s = standardize(source)
    m = standardize(target)
    mask = np.isfinite(t) & np.isfinite(s) & np.isfinite(m)
    if np.count_nonzero(mask) < 8:
        return math.nan, math.nan
    t = t[mask]
    s = s[mask]
    m = m[mask]
    dt = np.nanmedian(np.diff(t))
    if not np.isfinite(dt) or dt <= 0.0:
        return math.nan, math.nan
    max_lag = max(1, int(round(max_lag_seconds / dt)))
    best_score = -1.0
    best_corr = math.nan
    best_index = 0
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
        if not np.isfinite(corr):
            continue
        score = abs(corr) if maximize_abs else corr
        if score > best_score:
            best_score = score
            best_corr = corr
            best_index = lag
    if best_score < 0.0:
        return math.nan, math.nan
    return float(best_index * dt), float(best_corr)


def lag_aligned_arrays(times, source, target, lag_seconds):
    t = np.asarray(times, dtype=float)
    s = np.asarray(source, dtype=float)
    m = np.asarray(target, dtype=float)
    dt = np.nanmedian(np.diff(t))
    if not np.isfinite(dt) or dt <= 0.0 or not np.isfinite(lag_seconds):
        mask = np.isfinite(s) & np.isfinite(m)
        return s[mask], m[mask]
    lag = int(round(lag_seconds / dt))
    if lag < 0:
        ss = s[-lag:]
        mm = m[:lag]
    elif lag > 0:
        ss = s[:-lag]
        mm = m[lag:]
    else:
        ss = s
        mm = m
    mask = np.isfinite(ss) & np.isfinite(mm)
    return ss[mask], mm[mask]


def step95(values):
    z = standardize(values)
    diff = np.abs(np.diff(z))
    diff = diff[np.isfinite(diff)]
    if diff.size == 0:
        return math.nan
    return float(np.nanpercentile(diff, 95))


def is_mediapipe_signal(name):
    return name.startswith(("mp_body.", "mp_world.", "mp_world_unreal.", "mp_norm."))


def advance_signal(times, values, advance_seconds):
    if not np.isfinite(advance_seconds) or abs(advance_seconds) < 1.0e-9:
        return np.asarray(values, dtype=float)
    t = np.asarray(times, dtype=float)
    v = np.asarray(values, dtype=float)
    mask = np.isfinite(t) & np.isfinite(v)
    if np.count_nonzero(mask) < 2:
        return v.copy()
    return np.interp(t + advance_seconds, t[mask], v[mask], left=math.nan, right=math.nan)


def adjusted_signal(times, signals, name, mediapipe_advance_seconds):
    values = signals[name]["values"]
    if is_mediapipe_signal(name):
        return advance_signal(times, values, mediapipe_advance_seconds)
    return values


def nested_obj(sample, path, default=None):
    obj = sample
    for key in path:
        if isinstance(key, int):
            if not isinstance(obj, list) or key >= len(obj):
                return default
            obj = obj[key]
        else:
            if not isinstance(obj, dict):
                return default
            obj = obj.get(key, default)
        if obj is default:
            return default
    return obj


def to_float(value):
    if isinstance(value, bool):
        return 1.0 if value else 0.0
    try:
        return float(value)
    except (TypeError, ValueError):
        return math.nan


def nested(sample, path):
    return to_float(nested_obj(sample, path))


def nested_str(sample, path, default=""):
    obj = nested_obj(sample, path, default)
    return obj if isinstance(obj, str) else default


def series_from_path(samples, path):
    return np.asarray([nested(sample, path) for sample in samples], dtype=float)


def add_signal(signals, name, group, values, source_kind=""):
    values = np.asarray(values, dtype=float)
    if np.count_nonzero(np.isfinite(values)) >= MIN_SIGNAL_SAMPLES:
        signals[name] = {"group": group, "values": values, "source_kind": source_kind}


def empty_metric_row(source_name, target_name, group, pair_kind, status, reason, mediapipe_advance_seconds=0.0):
    stage_gate = status
    standardized_gate = status
    row = {
        "group": group,
        "pair_kind": pair_kind,
        "source": source_name,
        "target": target_name,
        "sample_count": 0,
        "valid_fraction": 0.0,
        "source_range_p95_p05": math.nan,
        "target_range_p95_p05": math.nan,
        "amplitude_ratio_target_over_source": math.nan,
        "corr_zero_lag": math.nan,
        "best_lag_seconds": math.nan,
        "corr_best_lag": math.nan,
        "best_abs_lag_seconds": math.nan,
        "corr_best_abs_lag": math.nan,
        "source_step95_standardized": math.nan,
        "target_step95_standardized": math.nan,
        "step95_ratio_target_over_source": math.nan,
        "measurement_only": group in {"arms_measure_only", "lower_body_measure_only"} or "measure_only" in pair_kind,
        "standardized_gate": standardized_gate,
        "standardized_flags": status,
        "stage_gate": stage_gate,
        "mediapipe_advance_ms": mediapipe_advance_seconds * 1000.0,
        "flags": status,
        "diagnostic_status": status,
        "not_valid_reason": reason,
        "source_recorded": False,
        "target_recorded": False,
    }
    return row


def flat_expected_stage0_pair(pair_kind):
    return pair_kind in {
        "hmd_to_output_head",
        "output_verification",
        "stage2_output_shoulder_compare",
        "shadow_vs_visible_pelvis_lock",
    }


def coordinate_space_not_comparable_pair(source_name, target_name, pair_kind):
    if pair_kind in {"mediapipe_raw_axis_diagnostic", "arm_conflict_measure_only", "arm_conflict_raw_measure_only", "shoulder_conflict_measure"}:
        return True
    mediapipe_source = source_name.startswith(("mp_body.", "mp_world.", "mp_norm."))
    non_mediapipe_target = target_name.startswith(("hmd.", "quest.", "fused.", "manny."))
    return mediapipe_source and non_mediapipe_target


def classify_pair_status(row, source_recorded=True, target_recorded=True):
    if not source_recorded and not target_recorded:
        return "not_recorded", "source and target signals were not recorded in this capture"
    if not source_recorded:
        return "not_recorded", "source signal was not recorded in this capture"
    if not target_recorded:
        return "not_recorded", "target signal was not recorded in this capture"
    if row["sample_count"] < 8 or row["valid_fraction"] < 0.25:
        return "source_unavailable", "source/target overlap is too sparse for a defensible diagnostic"
    flags = set(flag for flag in row["flags"].split(";") if flag)
    if ("flat_source" in flags or "flat_target" in flags) and flat_expected_stage0_pair(row["pair_kind"]):
        return "flat_expected_stage0", "flat visible-output row is expected while Stage 0 keeps MediaPipe authority disabled"
    if coordinate_space_not_comparable_pair(row["source"], row["target"], row["pair_kind"]):
        return "not_comparable_coordinate_space", "signals are intentionally diagnostic only and are not in a directly comparable coordinate space"
    if "flat_source" in flags or "flat_target" in flags:
        return "flat_unexpected", "source or target is flat and no Stage 0 lock explains it"
    if row["measurement_only"] or row["stage_gate"] in {"pass", "measure_only_no_authority"} or row["standardized_gate"] == "pass":
        return "valid", ""
    if row["flags"]:
        return "valid", "recorded and numerically valid, but gate did not pass"
    return "valid", ""


def add_path_signal(signals, samples, name, group, path, source_kind=""):
    add_signal(signals, name, group, series_from_path(samples, path), source_kind)


def availability_mask(samples, path):
    mask = np.zeros(len(samples), dtype=bool)
    for index, sample in enumerate(samples):
        mask[index] = nested_obj(sample, path) is True
    return mask


def add_masked_path_signal(signals, samples, name, group, path, mask, source_kind=""):
    values = series_from_path(samples, path)
    values[~mask] = math.nan
    add_signal(signals, name, group, values, source_kind)


def landmark_group(name):
    if any(part in name for part in ("eye", "ear", "mouth", "nose")):
        return "head_mp_diagnostic"
    if "shoulder" in name:
        return "shoulders"
    if any(part in name for part in ("elbow", "wrist", "pinky", "index", "thumb")):
        return "arms_measure_only"
    if "hip" in name:
        return "pelvis"
    if any(part in name for part in ("knee", "ankle", "heel", "foot")):
        return "lower_body_measure_only"
    return "mediapipe_landmarks"


def landmark_points(samples, root_path, landmark):
    points = np.full((len(samples), 3), math.nan, dtype=float)
    for sample_index, sample in enumerate(samples):
        for axis, axis_index in AXES.items():
            points[sample_index, axis_index] = nested(sample, root_path + [landmark, "pos", axis_index])
    return points


def add_point_axis_signals(signals, prefix, point_name, group, points, source_kind):
    for axis, axis_index in AXES.items():
        add_signal(signals, f"{prefix}.{point_name}.{axis}", group, points[:, axis_index], source_kind)


def midpoint(left, right):
    return (left + right) * 0.5


def distance(left, right):
    out = np.full(left.shape[0], math.nan, dtype=float)
    mask = np.all(np.isfinite(left), axis=1) & np.all(np.isfinite(right), axis=1)
    out[mask] = np.linalg.norm(left[mask] - right[mask], axis=1)
    return out


def axis_delta(a, b, axis):
    return a[:, AXES[axis]] - b[:, AXES[axis]]


def add_landmark_point_signals(signals, prefix, points):
    shoulder_mid = midpoint(points["left_shoulder"], points["right_shoulder"])
    hip_mid = midpoint(points["left_hip"], points["right_hip"])
    ear_mid = midpoint(points["left_ear"], points["right_ear"])
    wrist_mid = midpoint(points["left_wrist"], points["right_wrist"])
    ankle_mid = midpoint(points["left_ankle"], points["right_ankle"])
    add_point_axis_signals(signals, prefix, "shoulder_mid", "torso", shoulder_mid, prefix)
    add_point_axis_signals(signals, prefix, "hip_mid", "pelvis", hip_mid, prefix)
    add_point_axis_signals(signals, prefix, "ear_mid", "head_mp_diagnostic", ear_mid, prefix)
    add_point_axis_signals(signals, prefix, "wrist_mid", "arms_measure_only", wrist_mid, prefix)
    add_point_axis_signals(signals, prefix, "ankle_mid", "lower_body_measure_only", ankle_mid, prefix)

    add_signal(signals, f"{prefix}.torso_height", "torso", axis_delta(shoulder_mid, hip_mid, "z"), prefix)
    add_signal(signals, f"{prefix}.torso_side_proxy", "torso", axis_delta(shoulder_mid, hip_mid, "x"), prefix)
    add_signal(signals, f"{prefix}.torso_forward_proxy", "torso", axis_delta(shoulder_mid, hip_mid, "y"), prefix)
    add_signal(signals, f"{prefix}.shoulder_width", "shoulders", distance(points["left_shoulder"], points["right_shoulder"]), prefix)
    add_signal(signals, f"{prefix}.hip_width", "pelvis", distance(points["left_hip"], points["right_hip"]), prefix)
    add_signal(signals, f"{prefix}.ear_width", "head_mp_diagnostic", distance(points["left_ear"], points["right_ear"]), prefix)
    add_signal(signals, f"{prefix}.left_shoulder_lift_from_hips", "shoulders", axis_delta(points["left_shoulder"], hip_mid, "z"), prefix)
    add_signal(signals, f"{prefix}.right_shoulder_lift_from_hips", "shoulders", axis_delta(points["right_shoulder"], hip_mid, "z"), prefix)
    add_signal(
        signals,
        f"{prefix}.shoulder_lift_l_minus_r",
        "shoulders",
        points["left_shoulder"][:, AXES["z"]] - points["right_shoulder"][:, AXES["z"]],
        prefix,
    )
    for side in ("left", "right"):
        add_signal(
            signals,
            f"{prefix}.{side}_upper_arm_length",
            "arms_measure_only",
            distance(points[f"{side}_shoulder"], points[f"{side}_elbow"]),
            prefix,
        )
        add_signal(
            signals,
            f"{prefix}.{side}_forearm_length",
            "arms_measure_only",
            distance(points[f"{side}_elbow"], points[f"{side}_wrist"]),
            prefix,
        )
        add_signal(
            signals,
            f"{prefix}.{side}_knee_lift_from_hips",
            "lower_body_measure_only",
            axis_delta(points[f"{side}_knee"], hip_mid, "z"),
            prefix,
        )
        add_signal(
            signals,
            f"{prefix}.{side}_ankle_lift_from_hips",
            "lower_body_measure_only",
            axis_delta(points[f"{side}_ankle"], hip_mid, "z"),
            prefix,
        )


def add_landmark_space(signals, samples, prefix, root_path):
    points = {}
    for landmark in POSE_NAMES:
        points[landmark] = landmark_points(samples, root_path, landmark)
        add_point_axis_signals(signals, prefix, landmark, landmark_group(landmark), points[landmark], prefix)
    add_landmark_point_signals(signals, prefix, points)


def add_world_unreal_axis_landmark_space(signals, samples):
    # MediaPipe world uses x right, y up, z forward-ish. The candidate lane is in Unreal cm:
    # x right, y forward, z up. This derived space is for shape/axis diagnostics only.
    prefix = "mp_world_unreal"
    points = {}
    for landmark in POSE_NAMES:
        raw = landmark_points(samples, LANDMARK_SPACES["mp_world"], landmark)
        converted = np.full_like(raw, math.nan)
        converted[:, AXES["x"]] = raw[:, AXES["x"]]
        converted[:, AXES["y"]] = -raw[:, AXES["z"]]
        converted[:, AXES["z"]] = -raw[:, AXES["y"]]
        points[landmark] = converted
        add_point_axis_signals(signals, prefix, landmark, landmark_group(landmark), converted, prefix)
    add_landmark_point_signals(signals, prefix, points)


def add_difference_signal(signals, name, group, a_name, b_name, source_kind="derived"):
    if a_name not in signals or b_name not in signals:
        return
    add_signal(signals, name, group, signals[a_name]["values"] - signals[b_name]["values"], source_kind)


def extract_signals(samples):
    signals = {}
    mp_candidate_mask = availability_mask(samples, ["fusion", "mediapipe_candidate", "available"])
    for axis, index in AXES.items():
        add_path_signal(signals, samples, f"hmd.loc.{axis}", "head", ["fusion", "source", "hmd", "loc", index], "hmd")
        add_path_signal(signals, samples, f"fused.head.loc.{axis}", "head", ["fusion", "pose", "head", "loc", index], "fused")
        add_path_signal(signals, samples, f"manny.head.loc.{axis}", "head", ["live", "head", "loc", index], "manny")
        add_path_signal(signals, samples, f"fused.chest.loc.{axis}", "torso", ["fusion", "pose", "chest", "loc", index], "fused")
        add_path_signal(signals, samples, f"fused.pelvis.loc.{axis}", "pelvis", ["fusion", "pose", "pelvis", "loc", index], "fused")
        add_path_signal(
            signals,
            samples,
            f"shadow.chest.loc.{axis}",
            "torso",
            ["fusion", "shadow_candidate", "pose", "chest", "loc", index],
            "shadow_candidate",
        )
        add_path_signal(
            signals,
            samples,
            f"shadow.pelvis.loc.{axis}",
            "pelvis",
            ["fusion", "shadow_candidate", "pose", "pelvis", "loc", index],
            "shadow_candidate",
        )
        add_masked_path_signal(
            signals,
            samples,
            f"mp_candidate.chest.loc.{axis}",
            "torso",
            ["fusion", "mediapipe_candidate", "pose", "chest", "loc", index],
            mp_candidate_mask,
            "mediapipe_candidate",
        )
        add_masked_path_signal(
            signals,
            samples,
            f"mp_candidate.pelvis.loc.{axis}",
            "pelvis",
            ["fusion", "mediapipe_candidate", "pose", "pelvis", "loc", index],
            mp_candidate_mask,
            "mediapipe_candidate",
        )
        add_path_signal(signals, samples, f"manny.pelvis.loc.{axis}", "pelvis", ["live", "pelvis", "loc", index], "manny")
        add_path_signal(signals, samples, f"manny.spine_03.loc.{axis}", "torso", ["live", "spine_03", "loc", index], "manny")
        for side in ("left", "right"):
            hand_bone = "hand_l" if side == "left" else "hand_r"
            clavicle_bone = "clavicle_l" if side == "left" else "clavicle_r"
            add_path_signal(
                signals,
                samples,
                f"quest.{side}.wrist.{axis}",
                "arms_measure_only",
                ["fusion", "source", f"{side}_arm_chain", "wrist_world", index],
                "quest",
            )
            add_path_signal(
                signals,
                samples,
                f"quest.{side}.elbow.{axis}",
                "arms_measure_only",
                ["fusion", "source", f"{side}_arm_chain", "elbow_world", index],
                "quest",
            )
            add_path_signal(
                signals,
                samples,
                f"quest.{side}.shoulder.{axis}",
                "shoulders",
                ["fusion", "source", f"{side}_arm_chain", "shoulder_world", index],
                "quest",
            )
            add_path_signal(
                signals,
                samples,
                f"fused.{side}.shoulder.{axis}",
                "shoulders",
                ["fusion", "pose", f"{side}_shoulder", "loc", index],
                "fused",
            )
            add_path_signal(
                signals,
                samples,
                f"shadow.{side}.shoulder.{axis}",
                "shoulders",
                ["fusion", "shadow_candidate", "pose", f"{side}_shoulder", "loc", index],
                "shadow_candidate",
            )
            add_masked_path_signal(
                signals,
                samples,
                f"mp_candidate.{side}.shoulder.{axis}",
                "shoulders",
                ["fusion", "mediapipe_candidate", "pose", f"{side}_shoulder", "loc", index],
                mp_candidate_mask,
                "mediapipe_candidate",
            )
            add_path_signal(
                signals,
                samples,
                f"fused.{side}.wrist.{axis}",
                "arms_measure_only",
                ["fusion", "pose", f"{side}_wrist", "loc", index],
                "fused",
            )
            add_path_signal(signals, samples, f"manny.{hand_bone}.loc.{axis}", "arms_measure_only", ["live", hand_bone, "loc", index], "manny")
            add_path_signal(signals, samples, f"manny.{clavicle_bone}.loc.{axis}", "shoulders", ["live", clavicle_bone, "loc", index], "manny")

    for rot_axis, index in ROT_AXES.items():
        add_path_signal(signals, samples, f"hmd.rot.{rot_axis}", "head", ["fusion", "source", "hmd", "rot", index], "hmd")
        add_path_signal(signals, samples, f"fused.head.rot.{rot_axis}", "head", ["fusion", "pose", "head", "rot", index], "fused")
        add_path_signal(signals, samples, f"manny.head.local_rot.{rot_axis}", "head", ["live", "head", "local_rot", index], "manny")
        add_path_signal(signals, samples, f"solver.head.{rot_axis}", "head", ["solver", "head", f"computed_{rot_axis}_deg"], "solver")

    for prefix, root_path in LANDMARK_SPACES.items():
        add_landmark_space(signals, samples, prefix, root_path)
    add_world_unreal_axis_landmark_space(signals, samples)

    add_difference_signal(signals, "fused.torso_height", "torso", "fused.chest.loc.z", "fused.pelvis.loc.z", "fused")
    add_difference_signal(signals, "fused.torso_side_proxy", "torso", "fused.chest.loc.x", "fused.pelvis.loc.x", "fused")
    add_difference_signal(signals, "fused.torso_forward_proxy", "torso", "fused.chest.loc.y", "fused.pelvis.loc.y", "fused")
    add_difference_signal(signals, "shadow.torso_height", "torso", "shadow.chest.loc.z", "shadow.pelvis.loc.z", "shadow_candidate")
    add_difference_signal(signals, "shadow.torso_side_proxy", "torso", "shadow.chest.loc.x", "shadow.pelvis.loc.x", "shadow_candidate")
    add_difference_signal(
        signals,
        "shadow.torso_forward_proxy",
        "torso",
        "shadow.chest.loc.y",
        "shadow.pelvis.loc.y",
        "shadow_candidate",
    )
    add_difference_signal(signals, "mp_candidate.torso_height", "torso", "mp_candidate.chest.loc.z", "mp_candidate.pelvis.loc.z", "mediapipe_candidate")
    add_difference_signal(signals, "mp_candidate.torso_side_proxy", "torso", "mp_candidate.chest.loc.x", "mp_candidate.pelvis.loc.x", "mediapipe_candidate")
    add_difference_signal(
        signals,
        "mp_candidate.torso_forward_proxy",
        "torso",
        "mp_candidate.chest.loc.y",
        "mp_candidate.pelvis.loc.y",
        "mediapipe_candidate",
    )
    add_difference_signal(signals, "manny.torso_height", "torso", "manny.spine_03.loc.z", "manny.pelvis.loc.z", "manny")
    for side in ("left", "right"):
        clavicle_bone = "clavicle_l" if side == "left" else "clavicle_r"
        add_difference_signal(
            signals,
            f"fused.{side}.shoulder_lift_from_pelvis",
            "shoulders",
            f"fused.{side}.shoulder.z",
            "fused.pelvis.loc.z",
            "fused",
        )
        add_difference_signal(
            signals,
            f"shadow.{side}.shoulder_lift_from_pelvis",
            "shoulders",
            f"shadow.{side}.shoulder.z",
            "shadow.pelvis.loc.z",
            "shadow_candidate",
        )
        add_difference_signal(
            signals,
            f"mp_candidate.{side}.shoulder_lift_from_pelvis",
            "shoulders",
            f"mp_candidate.{side}.shoulder.z",
            "mp_candidate.pelvis.loc.z",
            "mediapipe_candidate",
        )
        add_difference_signal(
            signals,
            f"manny.{clavicle_bone}_lift_from_pelvis",
            "shoulders",
            f"manny.{clavicle_bone}.loc.z",
            "manny.pelvis.loc.z",
            "manny",
        )

    add_path_signal(signals, samples, "face.score", "head_mp_diagnostic", ["face", "score"], "face")
    add_path_signal(signals, samples, "face.count", "head_mp_diagnostic", ["face", "count"], "face")
    add_path_signal(signals, samples, "face.has_transform", "head_mp_diagnostic", ["face", "has_transform"], "face")
    for field in CONDITIONING_FIELDS:
        add_path_signal(signals, samples, f"conditioning.{field}", "conditioning", ["conditioning", field], "conditioning")

    return signals


def expected_pairs():
    pairs = [
        ("hmd.rot.pitch", "solver.head.pitch", "head", "hmd_to_solver_head"),
        ("hmd.rot.yaw", "solver.head.yaw", "head", "hmd_to_solver_head"),
        ("hmd.rot.roll", "solver.head.roll", "head", "hmd_to_solver_head"),
        ("hmd.loc.x", "fused.head.loc.x", "head", "hmd_to_fused_head"),
        ("hmd.loc.y", "fused.head.loc.y", "head", "hmd_to_fused_head"),
        ("hmd.loc.z", "fused.head.loc.z", "head", "hmd_to_fused_head"),
        ("hmd.loc.z", "manny.head.loc.z", "head", "hmd_to_output_head"),
        ("mp_body.nose.z", "hmd.loc.z", "head_mp_diagnostic", "stage3_head_compare_only"),
        ("mp_world.nose.z", "hmd.loc.z", "head_mp_diagnostic", "stage3_head_compare_only"),
        ("mp_body.ear_mid.z", "hmd.loc.z", "head_mp_diagnostic", "stage3_head_compare_only"),
        ("mp_world_unreal.shoulder_mid.z", "mp_candidate.chest.loc.z", "torso", "stage1_mediapipe_candidate_torso"),
        ("mp_world_unreal.torso_height", "mp_candidate.torso_height", "torso", "stage1_mediapipe_candidate_torso"),
        ("mp_world_unreal.torso_side_proxy", "mp_candidate.torso_side_proxy", "torso", "stage1_mediapipe_candidate_torso"),
        ("mp_world_unreal.torso_forward_proxy", "mp_candidate.torso_forward_proxy", "torso", "stage1_mediapipe_candidate_torso"),
        ("mp_body.shoulder_mid.z", "mp_candidate.chest.loc.z", "torso", "mediapipe_raw_axis_diagnostic"),
        ("mp_world.shoulder_mid.z", "mp_candidate.chest.loc.z", "torso", "mediapipe_raw_axis_diagnostic"),
        ("mp_body.torso_height", "mp_candidate.torso_height", "torso", "mediapipe_raw_axis_diagnostic"),
        ("mp_world.torso_height", "mp_candidate.torso_height", "torso", "mediapipe_raw_axis_diagnostic"),
        ("fused.torso_height", "manny.torso_height", "torso", "output_verification"),
        ("mp_world_unreal.hip_mid.z", "mp_candidate.pelvis.loc.z", "pelvis", "stage1_mediapipe_candidate_pelvis"),
        ("mp_world_unreal.hip_mid.x", "mp_candidate.pelvis.loc.x", "pelvis", "stage1_mediapipe_candidate_pelvis"),
        ("mp_world_unreal.hip_mid.y", "mp_candidate.pelvis.loc.y", "pelvis", "stage1_mediapipe_candidate_pelvis"),
        ("mp_body.hip_mid.z", "mp_candidate.pelvis.loc.z", "pelvis", "mediapipe_raw_axis_diagnostic"),
        ("mp_world.hip_mid.z", "mp_candidate.pelvis.loc.z", "pelvis", "mediapipe_raw_axis_diagnostic"),
        ("mp_candidate.pelvis.loc.z", "shadow.pelvis.loc.z", "pelvis", "mediapipe_candidate_vs_fused_shadow_measure"),
        ("shadow.pelvis.loc.z", "fused.pelvis.loc.z", "pelvis", "shadow_vs_visible_pelvis_lock"),
        ("fused.pelvis.loc.z", "manny.pelvis.loc.z", "pelvis", "output_verification"),
    ]
    for side in ("left", "right"):
        clavicle_bone = "clavicle_l" if side == "left" else "clavicle_r"
        pairs.extend(
            [
                (
                    f"mp_world_unreal.{side}_shoulder_lift_from_hips",
                    f"mp_candidate.{side}.shoulder_lift_from_pelvis",
                    "shoulders",
                    "stage2_mediapipe_candidate_shoulder",
                ),
                (
                    f"mp_body.{side}_shoulder_lift_from_hips",
                    f"manny.{clavicle_bone}_lift_from_pelvis",
                    "shoulders",
                    "stage2_output_shoulder_compare",
                ),
                (
                    f"mp_world_unreal.{side}_shoulder.z",
                    f"mp_candidate.{side}.shoulder.z",
                    "shoulders",
                    "stage2_mediapipe_candidate_shoulder",
                ),
                (f"mp_body.{side}_shoulder.z", f"mp_candidate.{side}.shoulder.z", "shoulders", "mediapipe_raw_axis_diagnostic"),
                (f"mp_candidate.{side}.shoulder.z", f"shadow.{side}.shoulder.z", "shoulders", "mediapipe_candidate_vs_fused_shadow_measure"),
                (f"quest.{side}.shoulder.z", f"mp_body.{side}_shoulder.z", "shoulders", "shoulder_conflict_measure"),
                (f"quest.{side}.shoulder.z", f"fused.{side}.shoulder.z", "shoulders", "quest_shoulder_output_measure"),
            ]
        )
        for axis in AXES:
            pairs.extend(
                [
                    (f"quest.{side}.wrist.{axis}", f"mp_body.{side}_wrist.{axis}", "arms_measure_only", "arm_conflict_measure_only"),
                    (f"quest.{side}.elbow.{axis}", f"mp_body.{side}_elbow.{axis}", "arms_measure_only", "arm_conflict_measure_only"),
                    (f"quest.{side}.wrist.{axis}", f"mp_world.{side}_wrist.{axis}", "arms_measure_only", "arm_conflict_raw_measure_only"),
                    (f"quest.{side}.elbow.{axis}", f"mp_world.{side}_elbow.{axis}", "arms_measure_only", "arm_conflict_raw_measure_only"),
                    (
                        f"quest.{side}.wrist.{axis}",
                        f"manny.hand_{'l' if side == 'left' else 'r'}.loc.{axis}",
                        "arms_measure_only",
                        "quest_arm_output_measure",
                    ),
                ]
            )
    return pairs


def pair_metrics(times, sample_total, signals, source_name, target_name, group, pair_kind, mediapipe_advance_seconds=0.0):
    source = adjusted_signal(times, signals, source_name, mediapipe_advance_seconds)
    target = adjusted_signal(times, signals, target_name, mediapipe_advance_seconds)
    valid_count = int(np.count_nonzero(np.isfinite(source) & np.isfinite(target)))
    lag, lag_corr = best_lag(times, source, target)
    abs_lag, abs_lag_corr = best_lag(times, source, target, maximize_abs=True)
    source_range = percentile_range(source)
    target_range = percentile_range(target)
    source_step = step95(source)
    target_step = step95(target)
    amp_ratio = target_range / source_range if np.isfinite(source_range) and abs(source_range) > 1.0e-9 else math.nan
    step_ratio = target_step / source_step if np.isfinite(source_step) and abs(source_step) > 1.0e-9 else math.nan
    corr_zero = safe_corr(source, target)
    valid_fraction = valid_count / sample_total if sample_total > 0 else 0.0
    measurement_only = group in {"arms_measure_only", "lower_body_measure_only"} or "measure_only" in pair_kind
    raw_world_to_candidate_pair = (
        pair_kind.startswith(("stage1_mediapipe_candidate_", "stage2_mediapipe_candidate_"))
        and source_name.startswith("mp_world_unreal.")
        and target_name.startswith("mp_candidate.")
    )

    flags = []
    if valid_count < 8 or valid_fraction < 0.25:
        flags.append("insufficient_overlap")
    if np.isfinite(source_range) and abs(source_range) < 1.0e-6:
        flags.append("flat_source")
    if np.isfinite(target_range) and abs(target_range) < 1.0e-6:
        flags.append("flat_target")
    corr_gate = 0.50 if measurement_only else 0.65
    if not np.isfinite(lag_corr) or lag_corr < corr_gate:
        flags.append("low_correlation")
    if np.isfinite(lag) and abs(lag) > 0.10:
        flags.append("lag_over_100ms")
    if not raw_world_to_candidate_pair and np.isfinite(amp_ratio) and (amp_ratio < 0.25 or amp_ratio > 4.0):
        flags.append("amplitude_mismatch")
    if np.isfinite(step_ratio) and step_ratio > 4.0:
        flags.append("jitter_or_step_mismatch")
    if valid_fraction < 0.85:
        flags.append("dropout_or_stale_overlap")
    if np.isfinite(abs_lag_corr) and abs_lag_corr < -0.60 and (not np.isfinite(lag_corr) or abs(abs_lag_corr) > abs(lag_corr) + 0.05):
        flags.append("possible_sign_or_axis_inversion")

    standardized_flags = [
        flag
        for flag in flags
        if flag
        in {
            "insufficient_overlap",
            "flat_source",
            "flat_target",
            "low_correlation",
            "lag_over_100ms",
            "jitter_or_step_mismatch",
            "dropout_or_stale_overlap",
            "possible_sign_or_axis_inversion",
        }
    ]
    standardized_ready = (
        valid_fraction >= 0.85
        and np.isfinite(lag_corr)
        and lag_corr >= (0.70 if group == "head_mp_diagnostic" else 0.75)
        and np.isfinite(lag)
        and abs(lag) <= 0.10
        and "flat_source" not in standardized_flags
        and "flat_target" not in standardized_flags
        and "possible_sign_or_axis_inversion" not in standardized_flags
    )
    standardized_gate = "pass" if standardized_ready else "fail"

    stage_ready = (
        not measurement_only
        and group in {"torso", "pelvis", "shoulders"}
        and valid_fraction >= 0.85
        and np.isfinite(lag_corr)
        and lag_corr >= 0.75
        and np.isfinite(lag)
        and abs(lag) <= 0.10
        and (raw_world_to_candidate_pair or (np.isfinite(amp_ratio) and 0.40 <= amp_ratio <= 2.50))
        and "flat_source" not in flags
        and "flat_target" not in flags
        and "possible_sign_or_axis_inversion" not in flags
    )
    stage_gate = "pass" if stage_ready else ("measure_only_no_authority" if measurement_only else "fail")

    row = {
        "group": group,
        "pair_kind": pair_kind,
        "source": source_name,
        "target": target_name,
        "sample_count": valid_count,
        "valid_fraction": valid_fraction,
        "source_range_p95_p05": source_range,
        "target_range_p95_p05": target_range,
        "amplitude_ratio_target_over_source": float(amp_ratio) if np.isfinite(amp_ratio) else math.nan,
        "corr_zero_lag": corr_zero,
        "best_lag_seconds": lag,
        "corr_best_lag": lag_corr,
        "best_abs_lag_seconds": abs_lag,
        "corr_best_abs_lag": abs_lag_corr,
        "source_step95_standardized": source_step,
        "target_step95_standardized": target_step,
        "step95_ratio_target_over_source": float(step_ratio) if np.isfinite(step_ratio) else math.nan,
        "measurement_only": measurement_only,
        "standardized_gate": standardized_gate,
        "standardized_flags": ";".join(dict.fromkeys(standardized_flags)),
        "stage_gate": stage_gate,
        "mediapipe_advance_ms": mediapipe_advance_seconds * 1000.0,
        "flags": ";".join(flags),
        "diagnostic_status": "",
        "not_valid_reason": "",
        "source_recorded": True,
        "target_recorded": True,
    }
    status, reason = classify_pair_status(row)
    row["diagnostic_status"] = status
    row["not_valid_reason"] = reason
    if status in NOT_VALID_STATUSES:
        row["stage_gate"] = status
        row["standardized_gate"] = status
    return row


def fit_signal_pairs():
    pairs = [
        ("stage3_head_compare_only", "mp_body.ear_mid.z", "hmd.loc.z", "head_mp_diagnostic"),
        ("stage3_head_compare_only", "mp_body.nose.z", "hmd.loc.z", "head_mp_diagnostic"),
        ("stage1_mediapipe_candidate_torso", "mp_world_unreal.shoulder_mid.z", "mp_candidate.chest.loc.z", "torso"),
        ("stage1_mediapipe_candidate_torso", "mp_world_unreal.torso_height", "mp_candidate.torso_height", "torso"),
        ("stage1_mediapipe_candidate_torso", "mp_world_unreal.torso_forward_proxy", "mp_candidate.torso_forward_proxy", "torso"),
        ("stage1_mediapipe_candidate_torso", "mp_world_unreal.torso_side_proxy", "mp_candidate.torso_side_proxy", "torso"),
        ("stage1_mediapipe_candidate_pelvis", "mp_world_unreal.hip_mid.z", "mp_candidate.pelvis.loc.z", "pelvis"),
        ("stage2_mediapipe_candidate_shoulder", "mp_world_unreal.left_shoulder.z", "mp_candidate.left.shoulder.z", "shoulders"),
        ("stage2_mediapipe_candidate_shoulder", "mp_world_unreal.right_shoulder.z", "mp_candidate.right.shoulder.z", "shoulders"),
        ("stage2_mediapipe_candidate_shoulder", "mp_world_unreal.left_shoulder_lift_from_hips", "mp_candidate.left.shoulder_lift_from_pelvis", "shoulders"),
        ("stage2_mediapipe_candidate_shoulder", "mp_world_unreal.right_shoulder_lift_from_hips", "mp_candidate.right.shoulder_lift_from_pelvis", "shoulders"),
        ("shoulder_conflict_measure", "mp_body.left_shoulder.z", "quest.left.shoulder.z", "shoulders"),
        ("shoulder_conflict_measure", "mp_body.right_shoulder.z", "quest.right.shoulder.z", "shoulders"),
    ]
    return pairs


def fit_vector_specs():
    return [
        {
            "fit_kind": "stage3_head_compare_only",
            "group": "head_mp_diagnostic",
            "source_prefix": "mp_body.ear_mid",
            "target_prefix": "hmd.loc",
            "label": "mp_body.ear_mid_xyz_to_hmd_loc_xyz",
        },
        {
            "fit_kind": "stage1_mediapipe_candidate_torso",
            "group": "torso",
            "source_prefix": "mp_world_unreal.shoulder_mid",
            "target_prefix": "mp_candidate.chest.loc",
            "label": "mp_world_unreal.shoulder_mid_xyz_to_mediapipe_candidate_chest_xyz",
        },
        {
            "fit_kind": "stage1_mediapipe_candidate_pelvis",
            "group": "pelvis",
            "source_prefix": "mp_world_unreal.hip_mid",
            "target_prefix": "mp_candidate.pelvis.loc",
            "label": "mp_world_unreal.hip_mid_xyz_to_mediapipe_candidate_pelvis_xyz",
        },
        {
            "fit_kind": "stage2_mediapipe_candidate_shoulder",
            "group": "shoulders",
            "source_prefix": "mp_world_unreal.left_shoulder",
            "target_prefix": "mp_candidate.left.shoulder",
            "label": "mp_world_unreal.left_shoulder_xyz_to_mediapipe_candidate_left_shoulder_xyz",
        },
        {
            "fit_kind": "stage2_mediapipe_candidate_shoulder",
            "group": "shoulders",
            "source_prefix": "mp_world_unreal.right_shoulder",
            "target_prefix": "mp_candidate.right.shoulder",
            "label": "mp_world_unreal.right_shoulder_xyz_to_mediapipe_candidate_right_shoulder_xyz",
        },
        {
            "fit_kind": "shoulder_conflict_measure",
            "group": "shoulders",
            "source_prefix": "mp_body.left_shoulder",
            "target_prefix": "quest.left.shoulder",
            "label": "mp_body.left_shoulder_xyz_to_quest_left_shoulder_xyz",
        },
        {
            "fit_kind": "shoulder_conflict_measure",
            "group": "shoulders",
            "source_prefix": "mp_body.right_shoulder",
            "target_prefix": "quest.right.shoulder",
            "label": "mp_body.right_shoulder_xyz_to_quest_right_shoulder_xyz",
        },
    ]


def fitted_gate(valid_fraction, fit_corr, fit_lag_seconds, normalized_rmse, flags, group):
    corr_threshold = 0.70 if group == "head_mp_diagnostic" else 0.75
    return (
        valid_fraction >= 0.85
        and np.isfinite(fit_corr)
        and fit_corr >= corr_threshold
        and np.isfinite(fit_lag_seconds)
        and abs(fit_lag_seconds) <= 0.10
        and np.isfinite(normalized_rmse)
        and normalized_rmse <= 0.35
        and "flat_source" not in flags
        and "flat_target" not in flags
    )


def fit_univariate_alignment(times, sample_total, signals, fit_kind, source_name, target_name, group, mediapipe_advance_seconds):
    source = adjusted_signal(times, signals, source_name, mediapipe_advance_seconds)
    target = adjusted_signal(times, signals, target_name, mediapipe_advance_seconds)
    mask = np.isfinite(source) & np.isfinite(target)
    valid_count = int(np.count_nonzero(mask))
    valid_fraction = valid_count / sample_total if sample_total > 0 else 0.0
    source_range = percentile_range(source)
    target_range = percentile_range(target)
    flags = []
    if valid_count < 8 or valid_fraction < 0.25:
        flags.append("insufficient_overlap")
    if not np.isfinite(source_range) or abs(source_range) < 1.0e-6:
        flags.append("flat_source")
    if not np.isfinite(target_range) or abs(target_range) < 1.0e-6:
        flags.append("flat_target")

    scale = math.nan
    offset = math.nan
    fit_corr = math.nan
    fit_lag = math.nan
    fit_lag_corr = math.nan
    rmse = math.nan
    normalized_rmse = math.nan
    rmse_best_lag = math.nan
    normalized_rmse_best_lag = math.nan
    r2 = math.nan
    r2_best_lag = math.nan
    fitted_values = np.full_like(source, math.nan)
    if "insufficient_overlap" not in flags and "flat_source" not in flags:
        x = source[mask]
        y = target[mask]
        design = np.column_stack([x, np.ones_like(x)])
        coeff, _, _, _ = np.linalg.lstsq(design, y, rcond=None)
        scale = float(coeff[0])
        offset = float(coeff[1])
        fitted_values = scale * source + offset
        residual = target[mask] - fitted_values[mask]
        rmse = float(np.sqrt(np.nanmean(residual * residual)))
        normalized_rmse = rmse / target_range if np.isfinite(target_range) and abs(target_range) > 1.0e-9 else math.nan
        fit_corr = safe_corr(fitted_values, target)
        fit_lag, fit_lag_corr = best_lag(times, fitted_values, target)
        aligned_fitted, aligned_target = lag_aligned_arrays(times, fitted_values, target, fit_lag)
        if aligned_fitted.size > 0:
            residual_best = aligned_target - aligned_fitted
            rmse_best_lag = float(np.sqrt(np.nanmean(residual_best * residual_best)))
            normalized_rmse_best_lag = rmse_best_lag / target_range if np.isfinite(target_range) and abs(target_range) > 1.0e-9 else math.nan
        total = np.nansum((y - np.nanmean(y)) ** 2)
        if np.isfinite(total) and total > 1.0e-9:
            r2 = float(1.0 - np.nansum(residual * residual) / total)
        if aligned_target.size > 0:
            total_best = np.nansum((aligned_target - np.nanmean(aligned_target)) ** 2)
            if np.isfinite(total_best) and total_best > 1.0e-9:
                r2_best_lag = float(1.0 - np.nansum((aligned_target - aligned_fitted) ** 2) / total_best)

    gate_corr = fit_lag_corr if np.isfinite(fit_lag_corr) else fit_corr
    gate_nrmse = normalized_rmse_best_lag if np.isfinite(normalized_rmse_best_lag) else normalized_rmse
    if not np.isfinite(gate_corr) or gate_corr < (0.70 if group == "head_mp_diagnostic" else 0.75):
        flags.append("low_fit_correlation")
    if np.isfinite(fit_lag) and abs(fit_lag) > 0.10:
        flags.append("residual_lag_over_100ms")
    if not np.isfinite(gate_nrmse) or gate_nrmse > 0.35:
        flags.append("high_fit_error")

    gate = "pass" if fitted_gate(valid_fraction, gate_corr, fit_lag, gate_nrmse, flags, group) else "fail"
    return {
        "fit_type": "univariate",
        "group": group,
        "fit_kind": fit_kind,
        "label": f"{source_name}_to_{target_name}",
        "source": source_name,
        "target": target_name,
        "sample_count": valid_count,
        "valid_fraction": valid_fraction,
        "mediapipe_advance_ms": mediapipe_advance_seconds * 1000.0,
        "scale": scale,
        "offset": offset,
        "matrix_json": "",
        "offset_json": "",
        "corr_after_fit": fit_corr,
        "best_lag_after_fit_seconds": fit_lag,
        "corr_best_lag_after_fit": fit_lag_corr,
        "rmse": rmse,
        "normalized_rmse": normalized_rmse,
        "rmse_best_lag": rmse_best_lag,
        "normalized_rmse_best_lag": normalized_rmse_best_lag,
        "r2": r2,
        "r2_best_lag": r2_best_lag,
        "axis_corr_x": math.nan,
        "axis_corr_y": math.nan,
        "axis_corr_z": math.nan,
        "axis_nrmse_x": math.nan,
        "axis_nrmse_y": math.nan,
        "axis_nrmse_z": math.nan,
        "gate": gate,
        "flags": ";".join(dict.fromkeys(flags)),
    }


def stack_axes(times, signals, prefix, mediapipe_advance_seconds):
    values = []
    for axis in ("x", "y", "z"):
        name = f"{prefix}.{axis}"
        if name not in signals:
            return None
        values.append(adjusted_signal(times, signals, name, mediapipe_advance_seconds))
    return np.column_stack(values)


def fit_vector_alignment(times, sample_total, signals, spec, mediapipe_advance_seconds):
    source = stack_axes(times, signals, spec["source_prefix"], mediapipe_advance_seconds)
    target = stack_axes(times, signals, spec["target_prefix"], mediapipe_advance_seconds)
    if source is None or target is None:
        return None

    mask = np.all(np.isfinite(source), axis=1) & np.all(np.isfinite(target), axis=1)
    valid_count = int(np.count_nonzero(mask))
    valid_fraction = valid_count / sample_total if sample_total > 0 else 0.0
    flags = []
    if valid_count < 8 or valid_fraction < 0.25:
        flags.append("insufficient_overlap")

    matrix = np.full((3, 3), math.nan, dtype=float)
    offset = np.full(3, math.nan, dtype=float)
    fitted = np.full_like(target, math.nan)
    corr_flat = math.nan
    lag_flat = math.nan
    lag_corr_flat = math.nan
    rmse = math.nan
    nrmse = math.nan
    r2 = math.nan
    axis_corr = [math.nan, math.nan, math.nan]
    axis_nrmse = [math.nan, math.nan, math.nan]
    source_axis_ranges = [percentile_range(source[:, i]) for i in range(3)]
    target_axis_ranges = [percentile_range(target[:, i]) for i in range(3)]
    if any(not np.isfinite(value) or abs(value) < 1.0e-6 for value in source_axis_ranges):
        flags.append("flat_source_axis")
    if any(not np.isfinite(value) or abs(value) < 1.0e-6 for value in target_axis_ranges):
        flags.append("flat_target_axis")

    if "insufficient_overlap" not in flags and "flat_source_axis" not in flags:
        x = source[mask]
        y = target[mask]
        design = np.column_stack([x, np.ones(x.shape[0])])
        coeff, _, _, _ = np.linalg.lstsq(design, y, rcond=None)
        matrix = coeff[:3, :].T
        offset = coeff[3, :]
        fitted = source @ matrix.T + offset
        residual = target[mask] - fitted[mask]
        rmse = float(np.sqrt(np.nanmean(residual * residual)))
        target_norm_range = np.linalg.norm([value for value in target_axis_ranges if np.isfinite(value)])
        nrmse = rmse / target_norm_range if target_norm_range > 1.0e-9 else math.nan
        corr_flat = safe_corr(fitted.reshape(-1), target.reshape(-1))
        lag_flat, lag_corr_flat = best_lag(
            np.repeat(times, 3),
            fitted.reshape(-1),
            target.reshape(-1),
        )
        total = np.nansum((y - np.nanmean(y, axis=0)) ** 2)
        if np.isfinite(total) and total > 1.0e-9:
            r2 = float(1.0 - np.nansum(residual * residual) / total)
        for axis_index in range(3):
            axis_corr[axis_index] = safe_corr(fitted[:, axis_index], target[:, axis_index])
            axis_rmse = float(np.sqrt(np.nanmean((target[mask, axis_index] - fitted[mask, axis_index]) ** 2)))
            axis_range = target_axis_ranges[axis_index]
            axis_nrmse[axis_index] = axis_rmse / axis_range if np.isfinite(axis_range) and axis_range > 1.0e-9 else math.nan

    min_axis_corr = min((value for value in axis_corr if np.isfinite(value)), default=math.nan)
    max_axis_nrmse = max((value for value in axis_nrmse if np.isfinite(value)), default=math.nan)
    if not np.isfinite(min_axis_corr) or min_axis_corr < (0.65 if spec["group"] == "head_mp_diagnostic" else 0.70):
        flags.append("low_axis_correlation")
    if not np.isfinite(max_axis_nrmse) or max_axis_nrmse > 0.45:
        flags.append("high_axis_fit_error")
    gate = (
        "pass"
        if valid_fraction >= 0.85
        and np.isfinite(corr_flat)
        and corr_flat >= (0.70 if spec["group"] == "head_mp_diagnostic" else 0.75)
        and np.isfinite(nrmse)
        and nrmse <= 0.35
        and "flat_source_axis" not in flags
        and "flat_target_axis" not in flags
        and "low_axis_correlation" not in flags
        else "fail"
    )

    return {
        "fit_type": "vector_affine_xyz",
        "group": spec["group"],
        "fit_kind": spec["fit_kind"],
        "label": spec["label"],
        "source": spec["source_prefix"] + ".{x,y,z}",
        "target": spec["target_prefix"] + ".{x,y,z}",
        "sample_count": valid_count,
        "valid_fraction": valid_fraction,
        "mediapipe_advance_ms": mediapipe_advance_seconds * 1000.0,
        "scale": math.nan,
        "offset": math.nan,
        "matrix_json": json.dumps(matrix.tolist()),
        "offset_json": json.dumps(offset.tolist()),
        "corr_after_fit": corr_flat,
        "best_lag_after_fit_seconds": lag_flat,
        "corr_best_lag_after_fit": lag_corr_flat,
        "rmse": rmse,
        "normalized_rmse": nrmse,
        "rmse_best_lag": math.nan,
        "normalized_rmse_best_lag": math.nan,
        "r2": r2,
        "r2_best_lag": math.nan,
        "axis_corr_x": axis_corr[0],
        "axis_corr_y": axis_corr[1],
        "axis_corr_z": axis_corr[2],
        "axis_nrmse_x": axis_nrmse[0],
        "axis_nrmse_y": axis_nrmse[1],
        "axis_nrmse_z": axis_nrmse[2],
        "gate": gate,
        "flags": ";".join(dict.fromkeys(flags)),
    }


def fitted_alignment_analysis(times, sample_total, signals, mediapipe_advance_seconds):
    rows = []
    for fit_kind, source, target, group in fit_signal_pairs():
        if source in signals and target in signals:
            rows.append(fit_univariate_alignment(times, sample_total, signals, fit_kind, source, target, group, mediapipe_advance_seconds))
    for spec in fit_vector_specs():
        row = fit_vector_alignment(times, sample_total, signals, spec, mediapipe_advance_seconds)
        if row:
            rows.append(row)
    return rows


def summarize_fitted_alignment(rows):
    pass_rows = [row for row in rows if row["gate"] == "pass"]
    stage1_pass = [row for row in pass_rows if row["fit_kind"].startswith("stage1_")]
    stage2_pass = [row for row in pass_rows if row["fit_kind"].startswith("stage2_")]
    head_pass = [row for row in pass_rows if row["fit_kind"].startswith("stage3_")]
    blocked = sorted({flag for row in rows if row["gate"] == "fail" for flag in row["flags"].split(";") if flag})
    if any(row["group"] == "torso" for row in stage1_pass) and any(row["group"] == "pelvis" for row in stage1_pass):
        recommendation = "Fitted Stage 1 torso/pelvis looks calibratable offline; consider a shadow-only runtime calibration knob next."
    elif any(row["group"] == "torso" for row in stage1_pass):
        recommendation = "Fitted torso has at least one calibratable signal, but pelvis remains blocked; do not enable full Stage 1 yet."
    elif stage2_pass:
        recommendation = "Fitted shoulder context has calibratable signals, but Stage 1 torso/pelvis remains blocked; keep visible authority unchanged."
    else:
        recommendation = "Fitted alignment still fails for Stage 1 and Stage 2; collect a more isolated motion trial or inspect coordinate mapping."
    return {
        "row_count": len(rows),
        "pass_count": len(pass_rows),
        "stage1_pass_count": len(stage1_pass),
        "stage2_pass_count": len(stage2_pass),
        "head_diagnostic_pass_count": len(head_pass),
        "pass_labels": [row["label"] for row in pass_rows],
        "blocked_reasons": blocked,
        "recommendation": recommendation,
    }


def standardized_alignment_rows(rows):
    result = []
    for row in rows:
        result.append(
            {
                "group": row["group"],
                "pair_kind": row["pair_kind"],
                "source": row["source"],
                "target": row["target"],
                "sample_count": row["sample_count"],
                "valid_fraction": row["valid_fraction"],
                "source_range_p95_p05": row["source_range_p95_p05"],
                "target_range_p95_p05": row["target_range_p95_p05"],
                "corr_zero_lag": row["corr_zero_lag"],
                "best_lag_seconds": row["best_lag_seconds"],
                "best_lag_ms": row["best_lag_seconds"] * 1000.0 if np.isfinite(row["best_lag_seconds"]) else math.nan,
                "corr_best_lag": row["corr_best_lag"],
                "best_abs_lag_seconds": row["best_abs_lag_seconds"],
                "corr_best_abs_lag": row["corr_best_abs_lag"],
                "source_step95_standardized": row["source_step95_standardized"],
                "target_step95_standardized": row["target_step95_standardized"],
                "step95_ratio_target_over_source": row["step95_ratio_target_over_source"],
                "measurement_only": row["measurement_only"],
                "standardized_gate": row["standardized_gate"],
                "standardized_flags": row["standardized_flags"],
                "raw_pair_gate": row["stage_gate"],
                "raw_pair_flags": row["flags"],
                "raw_amplitude_ratio_target_over_source": row["amplitude_ratio_target_over_source"],
                "mediapipe_advance_ms": row["mediapipe_advance_ms"],
                "diagnostic_status": row.get("diagnostic_status", "valid"),
                "not_valid_reason": row.get("not_valid_reason", ""),
                "source_recorded": row.get("source_recorded", True),
                "target_recorded": row.get("target_recorded", True),
            }
        )
    return result


def summarize_standardized_alignment(rows):
    comparable_rows = [row for row in rows if not row["measurement_only"]]
    pass_rows = [row for row in comparable_rows if row["standardized_gate"] == "pass"]
    stage1_rows = [row for row in comparable_rows if row["pair_kind"].startswith("stage1_")]
    stage1_pass = [row for row in stage1_rows if row["standardized_gate"] == "pass"]
    torso_pass = [row for row in stage1_pass if row["group"] == "torso"]
    pelvis_pass = [row for row in stage1_pass if row["group"] == "pelvis"]
    stage2_rows = [row for row in comparable_rows if row["pair_kind"].startswith("stage2_")]
    stage2_pass = [row for row in stage2_rows if row["standardized_gate"] == "pass"]
    head_rows = [row for row in comparable_rows if row["pair_kind"].startswith("stage3_")]
    head_pass = [row for row in head_rows if row["standardized_gate"] == "pass"]
    blocked = sorted(
        {
            flag
            for row in comparable_rows
            if row["standardized_gate"] == "fail"
            for flag in row["standardized_flags"].split(";")
            if flag
        }
    )
    if torso_pass and pelvis_pass:
        recommendation = "Standardized Stage 1 torso/pelvis waveforms are comparable; proceed to raw physical transform checks before any authority."
    elif torso_pass:
        recommendation = "Standardized torso waveforms are partially comparable, but pelvis is not; keep pelvis and visible authority shadow-only."
    elif stage2_pass:
        recommendation = "Standardized shoulder waveforms are partially comparable, but Stage 1 torso/pelvis is not; keep shoulders diagnostic-only."
    else:
        recommendation = "Standardized waveforms are not yet comparable enough for Stage 1 or Stage 2; inspect timing, axis choice, and target signals."
    return {
        "row_count": len(comparable_rows),
        "pass_count": len(pass_rows),
        "stage1_row_count": len(stage1_rows),
        "stage1_pass_count": len(stage1_pass),
        "stage1_torso_pass_count": len(torso_pass),
        "stage1_pelvis_pass_count": len(pelvis_pass),
        "stage2_row_count": len(stage2_rows),
        "stage2_pass_count": len(stage2_pass),
        "head_diagnostic_row_count": len(head_rows),
        "head_diagnostic_pass_count": len(head_pass),
        "pass_labels": [f"{row['source']}_to_{row['target']}" for row in pass_rows],
        "blocked_reasons": blocked,
        "recommendation": recommendation,
        "interpretation": "This gate compares standardized waveform shape, lag, sign, and freshness only. It intentionally ignores raw amplitude and centimeter-space fit.",
    }


def write_csv(path, rows, fields=None):
    if fields is None:
        fields = sorted({field for row in rows for field in row})
    with path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=fields)
        writer.writeheader()
        writer.writerows(rows)


def signal_inventory(signals, sample_count):
    rows = []
    for name, payload in sorted(signals.items()):
        values = payload["values"]
        summary = stats(values)
        rows.append(
            {
                "signal": name,
                "group": payload["group"],
                "source_kind": payload.get("source_kind", ""),
                "sample_count": summary["count"],
                "valid_fraction": summary["count"] / sample_count if sample_count > 0 else 0.0,
                "p05": summary["p05"],
                "p50": summary["p50"],
                "p95": summary["p95"],
                "range_p95_p05": summary["p95"] - summary["p05"] if summary["count"] else math.nan,
                "step95_standardized": step95(values),
            }
        )
    return rows


def landmark_inventory(samples):
    rows = []
    sample_count = len(samples)
    for space, root_path in LANDMARK_SPACES.items():
        for landmark in POSE_NAMES:
            points = landmark_points(samples, root_path, landmark)
            valid = np.all(np.isfinite(points), axis=1)
            reliability = series_from_path(samples, root_path + [landmark, "reliability"])
            visibility = series_from_path(samples, root_path + [landmark, "visibility"])
            presence = series_from_path(samples, root_path + [landmark, "presence"])
            row = {
                "space": space,
                "landmark": landmark,
                "group": landmark_group(landmark),
                "valid_samples": int(np.count_nonzero(valid)),
                "valid_fraction": float(np.count_nonzero(valid) / sample_count) if sample_count else 0.0,
                "reliability_p50": stats(reliability)["p50"],
                "visibility_p50": stats(visibility)["p50"],
                "presence_p50": stats(presence)["p50"],
            }
            for axis, axis_index in AXES.items():
                row[f"{axis}_range_p95_p05"] = percentile_range(points[:, axis_index])
            rows.append(row)
    return rows


def summarize_capture(samples):
    states = {}
    for key, path in {
        "authority_state": ["fusion", "authority_state"],
        "authority_reason": ["fusion", "authority_reason"],
        "hmd": ["fusion", "source", "hmd", "status", "state"],
        "body_pose": ["fusion", "source", "body_pose", "status", "state"],
        "mediapipe_candidate_available": ["fusion", "mediapipe_candidate", "available"],
        "mediapipe_candidate_reason": ["fusion", "mediapipe_candidate", "reason"],
        "mediapipe_candidate_body_pose": ["fusion", "mediapipe_candidate", "body_pose_status", "state"],
        "left_arm_chain": ["fusion", "source", "left_arm_chain", "status", "state"],
        "right_arm_chain": ["fusion", "source", "right_arm_chain", "status", "state"],
    }.items():
        counts = {}
        for sample in samples:
            raw_value = nested_obj(sample, path, "missing")
            value = str(raw_value).lower() if isinstance(raw_value, bool) else (raw_value if isinstance(raw_value, str) else "missing")
            counts[value] = counts.get(value, 0) + 1
        states[key] = counts
    return states


def counts_for_path(samples, path, default="missing"):
    counts = {}
    for sample in samples:
        raw_value = nested_obj(sample, path, default)
        value = str(raw_value).lower() if isinstance(raw_value, bool) else (raw_value if isinstance(raw_value, str) else default)
        counts[value] = counts.get(value, 0) + 1
    return counts


def array_series(samples, path, width):
    values = np.full((len(samples), width), math.nan, dtype=float)
    for sample_index, sample in enumerate(samples):
        raw = nested_obj(sample, path)
        if not isinstance(raw, list) or len(raw) < width:
            continue
        for value_index in range(width):
            values[sample_index, value_index] = to_float(raw[value_index])
    return values


def array_valid_count(samples, path, width):
    values = array_series(samples, path, width)
    return int(np.count_nonzero(np.all(np.isfinite(values), axis=1)))


def max_axis_range(values):
    if values.ndim != 2 or values.shape[1] == 0:
        return math.nan
    ranges = [percentile_range(values[:, index]) for index in range(values.shape[1])]
    return max((value for value in ranges if np.isfinite(value)), default=math.nan)


def pose_node_availability_row(samples, region, lane, path, rotation_source, owner_default="not_recorded"):
    sample_count = len(samples)
    valid_values = []
    owner_counts = {}
    source_state_counts = {}
    confidence_values = []
    for sample in samples:
        node = nested_obj(sample, path)
        if not isinstance(node, dict):
            valid_values.append(False)
            owner_counts[owner_default] = owner_counts.get(owner_default, 0) + 1
            source_state_counts["missing"] = source_state_counts.get("missing", 0) + 1
            continue
        valid_values.append(bool(node.get("valid", False)))
        owner = str(node.get("owner", owner_default))
        owner_counts[owner] = owner_counts.get(owner, 0) + 1
        source_state = str(node.get("source_state", "missing"))
        source_state_counts[source_state] = source_state_counts.get(source_state, 0) + 1
        confidence_values.append(to_float(node.get("confidence")))
    loc_values = array_series(samples, path + ["loc"], 3)
    rot_values = array_series(samples, path + ["rot"], 3)
    quat_values = array_series(samples, path + ["quat"], 4)
    loc_valid = int(np.count_nonzero(np.all(np.isfinite(loc_values), axis=1)))
    rot_valid = int(np.count_nonzero(np.all(np.isfinite(rot_values), axis=1)))
    quat_valid = int(np.count_nonzero(np.all(np.isfinite(quat_values), axis=1)))
    rot_range = max_axis_range(rot_values)
    has_rotation = rot_valid >= MIN_SIGNAL_SAMPLES and np.isfinite(rot_range) and rot_range > 1.0e-6
    return {
        "region": region,
        "lane": lane,
        "path": ".".join(str(part) for part in path),
        "valid_samples": int(sum(1 for value in valid_values if value)),
        "valid_fraction": float(sum(1 for value in valid_values if value) / sample_count) if sample_count else 0.0,
        "loc_samples": loc_valid,
        "loc_fraction": float(loc_valid / sample_count) if sample_count else 0.0,
        "rot_samples": rot_valid,
        "quat_samples": quat_valid,
        "has_rotation": bool(has_rotation),
        "rotation_range_max_p95_p05": rot_range,
        "rotation_source": rotation_source if has_rotation else "none",
        "owner_counts_json": json.dumps(owner_counts, sort_keys=True),
        "source_state_counts_json": json.dumps(source_state_counts, sort_keys=True),
        "confidence_p50": stats(confidence_values)["p50"],
    }


def live_bone_availability_row(samples, region, bone_name, rotation_field="rot"):
    sample_count = len(samples)
    path = ["live", bone_name]
    loc_values = array_series(samples, path + ["loc"], 3)
    rot_values = array_series(samples, path + [rotation_field], 3)
    loc_valid = int(np.count_nonzero(np.all(np.isfinite(loc_values), axis=1)))
    rot_valid = int(np.count_nonzero(np.all(np.isfinite(rot_values), axis=1)))
    rot_range = max_axis_range(rot_values)
    has_rotation = rot_valid >= MIN_SIGNAL_SAMPLES and np.isfinite(rot_range) and rot_range > 1.0e-6
    return {
        "region": region,
        "lane": f"output_{bone_name}",
        "path": ".".join(path),
        "valid_samples": loc_valid,
        "valid_fraction": float(loc_valid / sample_count) if sample_count else 0.0,
        "loc_samples": loc_valid,
        "loc_fraction": float(loc_valid / sample_count) if sample_count else 0.0,
        "rot_samples": rot_valid,
        "quat_samples": array_valid_count(samples, path + ["quat"], 4),
        "has_rotation": bool(has_rotation),
        "rotation_range_max_p95_p05": rot_range,
        "rotation_source": "output_bone" if has_rotation else "none",
        "owner_counts_json": json.dumps({"manny_output_recorder": loc_valid, "missing": max(0, sample_count - loc_valid)}, sort_keys=True),
        "source_state_counts_json": json.dumps({"recorded": loc_valid, "missing": max(0, sample_count - loc_valid)}, sort_keys=True),
        "confidence_p50": math.nan,
    }


def source_availability_row(samples, region, lane, path, source_owner, rotation_path=None):
    sample_count = len(samples)
    state_counts = counts_for_path(samples, path + ["status", "state"])
    fresh_count = int(state_counts.get("fresh", 0))
    loc_valid = array_valid_count(samples, path + ["loc"], 3)
    rot_valid = array_valid_count(samples, rotation_path or path + ["rot"], 3)
    rot_values = array_series(samples, rotation_path or path + ["rot"], 3)
    rot_range = max_axis_range(rot_values)
    has_rotation = rot_valid >= MIN_SIGNAL_SAMPLES and np.isfinite(rot_range) and rot_range > 1.0e-6
    return {
        "region": region,
        "lane": lane,
        "path": ".".join(path),
        "valid_samples": fresh_count,
        "valid_fraction": float(fresh_count / sample_count) if sample_count else 0.0,
        "loc_samples": loc_valid,
        "loc_fraction": float(loc_valid / sample_count) if sample_count else 0.0,
        "rot_samples": rot_valid,
        "quat_samples": array_valid_count(samples, path + ["quat"], 4),
        "has_rotation": bool(has_rotation),
        "rotation_range_max_p95_p05": rot_range,
        "rotation_source": source_owner if has_rotation else "none",
        "owner_counts_json": json.dumps({source_owner: fresh_count, "missing_or_not_fresh": max(0, sample_count - fresh_count)}, sort_keys=True),
        "source_state_counts_json": json.dumps(state_counts, sort_keys=True),
        "confidence_p50": stats(series_from_path(samples, path + ["status", "confidence"]))["p50"],
    }


def region_ownership_availability(samples):
    rows = [
        source_availability_row(samples, "head", "source_hmd", ["fusion", "source", "hmd"], "hmd"),
        source_availability_row(samples, "chest", "source_mediapipe_body", ["fusion", "source", "body_pose"], "mediapipe", ["fusion", "source", "body_pose", "rot"]),
        source_availability_row(samples, "left_arm", "source_quest_left_arm_chain", ["fusion", "source", "left_arm_chain"], "quest"),
        source_availability_row(samples, "right_arm", "source_quest_right_arm_chain", ["fusion", "source", "right_arm_chain"], "quest"),
    ]
    for lane, base_path, rotation_source in (
        ("fused", ["fusion", "pose"], "fusion_pose"),
        ("shadow_candidate", ["fusion", "shadow_candidate", "pose"], "shadow_candidate"),
        ("mediapipe_candidate", ["fusion", "mediapipe_candidate", "pose"], "derived_unavailable"),
    ):
        for region, bone in (
            ("head", "head"),
            ("chest", "chest"),
            ("pelvis", "pelvis"),
            ("left_shoulder", "left_shoulder"),
            ("left_elbow", "left_elbow"),
            ("left_wrist", "left_wrist"),
            ("right_shoulder", "right_shoulder"),
            ("right_elbow", "right_elbow"),
            ("right_wrist", "right_wrist"),
        ):
            rows.append(pose_node_availability_row(samples, region, lane, base_path + [bone], rotation_source))
    for region, bone, rotation_field in (
        ("head", "head", "local_rot"),
        ("pelvis", "pelvis", "rot"),
        ("chest", "spine_03", "rot"),
        ("left_shoulder", "clavicle_l", "rot"),
        ("right_shoulder", "clavicle_r", "rot"),
        ("left_hand", "hand_l", "rot"),
        ("right_hand", "hand_r", "rot"),
    ):
        rows.append(live_bone_availability_row(samples, region, bone, rotation_field))
    return rows


def not_valid_reason_rows(rows, fitted_rows, rotation_rows):
    result = []
    for row in rows:
        status = row.get("diagnostic_status", "valid")
        flat_flag = any(flag in row.get("flags", "") for flag in ("flat_source", "flat_target"))
        if status in NOT_VALID_STATUSES or flat_flag:
            result.append(
                {
                    "table": "pair_metrics",
                    "group": row["group"],
                    "kind": row["pair_kind"],
                    "source": row["source"],
                    "target": row["target"],
                    "diagnostic_status": status,
                    "not_valid_reason": row.get("not_valid_reason", ""),
                    "stage_gate": row["stage_gate"],
                    "standardized_gate": row["standardized_gate"],
                    "flags": row["flags"],
                    "sample_count": row["sample_count"],
                    "valid_fraction": row["valid_fraction"],
                }
            )
    for row in fitted_rows:
        flat_flag = any(flag in row.get("flags", "") for flag in ("flat_source", "flat_target", "flat_source_axis", "flat_target_axis"))
        if row.get("gate") == "fail" and flat_flag:
            result.append(
                {
                    "table": "fitted_alignment",
                    "group": row["group"],
                    "kind": row["fit_kind"],
                    "source": row["source"],
                    "target": row["target"],
                    "diagnostic_status": "flat_unexpected",
                    "not_valid_reason": "fitted alignment source or target axis is flat and no Stage 0 lock explains it",
                    "stage_gate": row["gate"],
                    "standardized_gate": "",
                    "flags": row["flags"],
                    "sample_count": row["sample_count"],
                    "valid_fraction": row["valid_fraction"],
                }
            )
    for row in rotation_rows:
        status = row.get("diagnostic_status", "valid")
        if status in NOT_VALID_STATUSES:
            result.append(
                {
                    "table": "rotation_diagnostics",
                    "group": row["bone_or_region"],
                    "kind": row["axis"],
                    "source": row.get("source", ""),
                    "target": row.get("target", ""),
                    "diagnostic_status": status,
                    "not_valid_reason": row.get("not_valid_reason", ""),
                    "stage_gate": status,
                    "standardized_gate": "",
                    "flags": "",
                    "sample_count": row.get("sample_count", 0),
                    "valid_fraction": row.get("valid_fraction", 0.0),
                }
            )
    return result


def main_bone_movement_summary_rows(rows):
    movement_kinds = {
        "hmd_to_fused_head": "head_position_hmd_to_fused",
        "stage1_mediapipe_candidate_pelvis": "pelvis_mediapipe_to_candidate",
        "quest_shoulder_output_measure": "shoulder_quest_to_fused",
        "stage2_mediapipe_candidate_shoulder": "shoulder_mediapipe_to_candidate",
        "stage1_mediapipe_candidate_torso": "torso_mediapipe_to_candidate",
        "arm_conflict_measure_only": "arm_quest_vs_mediapipe_measure_only",
        "quest_arm_output_measure": "hand_quest_to_manny_output",
        "output_verification": "visible_output_lock_stage0",
        "stage2_output_shoulder_compare": "visible_shoulder_output_lock_stage0",
    }
    result = []
    for row in rows:
        label = movement_kinds.get(row["pair_kind"])
        if label is None:
            continue
        result.append(
            {
                "bone_or_region": label,
                "source": row["source"],
                "target": row["target"],
                "zero_lag_corr": row["corr_zero_lag"],
                "best_lag_ms": row["best_lag_seconds"] * 1000.0 if np.isfinite(row["best_lag_seconds"]) else math.nan,
                "best_lag_corr": row["corr_best_lag"],
                "sample_count": row["sample_count"],
                "valid_fraction": row["valid_fraction"],
                "diagnostic_status": row.get("diagnostic_status", "valid"),
                "not_valid_reason": row.get("not_valid_reason", ""),
                "note": row.get("not_valid_reason", "") or "Recorded numeric movement diagnostic.",
            }
        )
    return result


def rotation_diagnostic_rows(samples, signals, times, sample_total, mediapipe_advance_seconds):
    rows = []
    for axis in ROT_AXES:
        source = f"hmd.rot.{axis}"
        target = f"fused.head.rot.{axis}"
        if source in signals and target in signals:
            metric = pair_metrics(times, sample_total, signals, source, target, "head", "hmd_to_fused_head_rotation", mediapipe_advance_seconds)
            rows.append(
                {
                    "bone_or_region": "head_rotation_hmd_to_fused",
                    "axis": axis,
                    "source": source,
                    "target": target,
                    "has_rotation": True,
                    "rotation_source": "hmd_authoritative",
                    "zero_lag_corr": metric["corr_zero_lag"],
                    "best_lag_ms": metric["best_lag_seconds"] * 1000.0 if np.isfinite(metric["best_lag_seconds"]) else math.nan,
                    "best_lag_corr": metric["corr_best_lag"],
                    "sample_count": metric["sample_count"],
                    "valid_fraction": metric["valid_fraction"],
                    "diagnostic_status": "valid",
                    "not_valid_reason": "",
                    "note": "HMD to fused head rotation is the comparable authoritative rotation source in this capture.",
                }
            )
        else:
            rows.append(
                {
                    "bone_or_region": "head_rotation_hmd_to_fused",
                    "axis": axis,
                    "source": source,
                    "target": target,
                    "has_rotation": False,
                    "rotation_source": "none",
                    "zero_lag_corr": math.nan,
                    "best_lag_ms": math.nan,
                    "best_lag_corr": math.nan,
                    "sample_count": 0,
                    "valid_fraction": 0.0,
                    "diagnostic_status": "not_recorded",
                    "not_valid_reason": "HMD or fused head rotation field was not recorded.",
                    "note": "HMD/fused head rotation was expected but unavailable.",
                }
            )

    for source_prefix, label, reason in (
        ("solver.head", "head_rotation_hmd_to_solver", "solver head rotation diagnostics were not recorded in this capture"),
        ("manny.head.local_rot", "manny_head_local_rotation", "Manny head local rotation recorder lane is flat/unsuitable in Stage 0; HMD to fused head rotation is authoritative"),
    ):
        for axis in ROT_AXES:
            source = f"hmd.rot.{axis}" if source_prefix == "solver.head" else f"{source_prefix}.{axis}"
            target = f"{source_prefix}.{axis}" if source_prefix == "solver.head" else ""
            if source_prefix == "solver.head" and source in signals and target in signals:
                metric = pair_metrics(times, sample_total, signals, source, target, "head", "hmd_to_solver_head", mediapipe_advance_seconds)
                status = metric.get("diagnostic_status", "valid")
                rows.append(
                    {
                        "bone_or_region": label,
                        "axis": axis,
                        "source": source,
                        "target": target,
                        "has_rotation": status == "valid",
                        "rotation_source": "solver" if status == "valid" else "none",
                        "zero_lag_corr": metric["corr_zero_lag"],
                        "best_lag_ms": metric["best_lag_seconds"] * 1000.0 if np.isfinite(metric["best_lag_seconds"]) else math.nan,
                        "best_lag_corr": metric["corr_best_lag"],
                        "sample_count": metric["sample_count"],
                        "valid_fraction": metric["valid_fraction"],
                        "diagnostic_status": status,
                        "not_valid_reason": metric.get("not_valid_reason", ""),
                        "note": metric.get("not_valid_reason", "") or "Solver head rotation was recorded.",
                    }
                )
            else:
                values = signals.get(source, {}).get("values", np.asarray([], dtype=float))
                value_range = percentile_range(values) if values.size else math.nan
                status = "flat_expected_stage0" if source_prefix == "manny.head.local_rot" and np.isfinite(value_range) and value_range <= 1.0e-6 else "not_recorded"
                rows.append(
                    {
                        "bone_or_region": label,
                        "axis": axis,
                        "source": source,
                        "target": target,
                        "has_rotation": False,
                        "rotation_source": "none",
                        "zero_lag_corr": math.nan,
                        "best_lag_ms": math.nan,
                        "best_lag_corr": math.nan,
                        "sample_count": int(np.count_nonzero(np.isfinite(values))) if values.size else 0,
                        "valid_fraction": float(np.count_nonzero(np.isfinite(values)) / sample_total) if values.size and sample_total else 0.0,
                        "diagnostic_status": status,
                        "not_valid_reason": reason,
                        "note": reason,
                    }
                )

    for region in ("chest", "pelvis", "left_shoulder", "right_shoulder"):
        rot_values = array_series(samples, ["fusion", "mediapipe_candidate", "pose", region, "rot"], 3)
        rot_count = int(np.count_nonzero(np.all(np.isfinite(rot_values), axis=1)))
        rows.append(
            {
                "bone_or_region": f"mediapipe_candidate_{region}_rotation",
                "axis": "pitch/yaw/roll",
                "source": f"fusion.mediapipe_candidate.pose.{region}.rot",
                "target": "",
                "has_rotation": False,
                "rotation_source": "derived_unavailable",
                "zero_lag_corr": math.nan,
                "best_lag_ms": math.nan,
                "best_lag_corr": math.nan,
                "sample_count": rot_count,
                "valid_fraction": float(rot_count / sample_total) if sample_total else 0.0,
                "diagnostic_status": "derived_unavailable",
                "not_valid_reason": "MediaPipe candidate body lane records landmark-derived positions; robust torso/pelvis/shoulder rotations are not recorded or derived here.",
                "note": "Position diagnostics are valid; body rotation diagnostics are explicitly unavailable.",
            }
        )

    for side in ("left", "right"):
        rows.append(
            {
                "bone_or_region": f"quest_{side}_arm_chain_rotation",
                "axis": "pitch/yaw/roll",
                "source": f"fusion.source.{side}_arm_chain",
                "target": "",
                "has_rotation": False,
                "rotation_source": "none",
                "zero_lag_corr": math.nan,
                "best_lag_ms": math.nan,
                "best_lag_corr": math.nan,
                "sample_count": 0,
                "valid_fraction": 0.0,
                "diagnostic_status": "not_recorded",
                "not_valid_reason": "Quest arm chain capture records shoulder/elbow/wrist positions but no chain rotation field.",
                "note": "Do not infer Quest arm rotations from chain positions in this report.",
            }
        )
    return rows


def timing_summary(samples):
    rows = []
    for field in TIMING_MS_FIELDS:
        values = series_from_path(samples, ["timing", field])
        summary = stats(values)
        rows.append(
            {
                "kind": "sample_timing_ms",
                "name": field,
                "count": summary["count"],
                "min": summary["min"],
                "p50": summary["p50"],
                "p95": summary["p95"],
                "max": summary["max"],
                "mean": summary["mean"],
                "total": math.nan,
            }
        )

    for field in CONDITIONING_FIELDS:
        values = series_from_path(samples, ["conditioning", field])
        summary = stats(values)
        rows.append(
            {
                "kind": "conditioning",
                "name": field,
                "count": summary["count"],
                "min": summary["min"],
                "p50": summary["p50"],
                "p95": summary["p95"],
                "max": summary["max"],
                "mean": summary["mean"],
                "total": math.nan,
            }
        )

    last_pipeline = {}
    for sample in reversed(samples):
        candidate = sample.get("pipeline")
        if isinstance(candidate, dict):
            last_pipeline = candidate
            break
    for name, count_field, total_field, max_field in PIPELINE_AGGREGATES:
        count = to_float(last_pipeline.get(count_field))
        total = to_float(last_pipeline.get(total_field))
        max_value = to_float(last_pipeline.get(max_field))
        mean = total / count if np.isfinite(count) and count > 0 and np.isfinite(total) else math.nan
        rows.append(
            {
                "kind": "pipeline_aggregate_ms",
                "name": name,
                "count": count,
                "min": math.nan,
                "p50": math.nan,
                "p95": math.nan,
                "max": max_value,
                "mean": mean,
                "total": total,
            }
        )
    return rows, last_pipeline


def face_summary(samples):
    score = series_from_path(samples, ["face", "score"])
    count = series_from_path(samples, ["face", "count"])
    has_transform = series_from_path(samples, ["face", "has_transform"])
    return {
        "samples_with_face": int(np.count_nonzero(np.isfinite(score))),
        "score": stats(score),
        "landmark_count": stats(count),
        "has_transform_samples": int(np.count_nonzero(has_transform == 1.0)),
    }


def plot_groups(out_dir, times, signals, rows, mediapipe_advance_seconds=0.0):
    charts = {}
    for group in sorted({row["group"] for row in rows}):
        group_rows = [row for row in rows if row["group"] == group and row["source"] in signals and row["target"] in signals]
        if not group_rows:
            continue
        for chunk_start in range(0, len(group_rows), 12):
            chunk = group_rows[chunk_start : chunk_start + 12]
            fig, axes = plt.subplots(len(chunk), 1, figsize=(15, max(3.0, 2.0 * len(chunk))), sharex=True)
            if len(chunk) == 1:
                axes = [axes]
            for ax, row in zip(axes, chunk):
                source = standardize(adjusted_signal(times, signals, row["source"], mediapipe_advance_seconds))
                target = standardize(adjusted_signal(times, signals, row["target"], mediapipe_advance_seconds))
                ax.plot(times, source, label=row["source"], linewidth=1.2)
                ax.plot(times, target, label=row["target"], linewidth=1.1)
                ax.grid(True, alpha=0.25)
                ax.legend(fontsize=8, loc="upper right")
                ax.set_title(
                    f"{row['pair_kind']} corr={row['corr_best_lag']:.3f} lag={row['best_lag_seconds']:.3f}s "
                    f"std_gate={row['standardized_gate']} raw_pair_gate={row['stage_gate']} "
                    f"std_flags={row['standardized_flags'] or 'none'}",
                    fontsize=9,
                )
            axes[-1].set_xlabel("capture seconds")
            fig.suptitle(f"{group}: standardized MPQ diagnostic signals")
            fig.tight_layout(rect=[0, 0, 1, 0.97])
            suffix = "" if chunk_start == 0 else f"_{chunk_start // 12 + 1:02d}"
            chart_path = out_dir / f"{group}_standardized{suffix}.png"
            fig.savefig(chart_path, dpi=150)
            plt.close(fig)
            charts[f"{group}{suffix}"] = str(chart_path)
    return charts


def plot_timing(out_dir, times, samples):
    available = []
    for field in TIMING_MS_FIELDS:
        values = series_from_path(samples, ["timing", field])
        if np.count_nonzero(np.isfinite(values)) >= MIN_SIGNAL_SAMPLES:
            available.append((field, values))
    if not available:
        return None
    fig, ax = plt.subplots(figsize=(15, 6))
    for field, values in available:
        ax.plot(times, values, label=field, linewidth=1.0)
    ax.grid(True, alpha=0.25)
    ax.legend(fontsize=8, loc="upper right")
    ax.set_xlabel("capture seconds")
    ax.set_ylabel("milliseconds")
    ax.set_title("MediaPipe frame timing")
    chart_path = out_dir / "timing_ms.png"
    fig.tight_layout()
    fig.savefig(chart_path, dpi=150)
    plt.close(fig)
    return str(chart_path)


def plot_landmark_availability(out_dir, inventory_rows):
    mp_body_rows = [row for row in inventory_rows if row["space"] == "mp_body"]
    if not mp_body_rows:
        return None
    names = [row["landmark"] for row in mp_body_rows]
    fractions = [row["valid_fraction"] for row in mp_body_rows]
    fig, ax = plt.subplots(figsize=(16, 6))
    ax.bar(range(len(names)), fractions)
    ax.set_xticks(range(len(names)))
    ax.set_xticklabels(names, rotation=70, ha="right", fontsize=8)
    ax.set_ylim(0, 1.05)
    ax.set_ylabel("valid fraction")
    ax.set_title("MediaPipe body landmark availability")
    ax.grid(True, axis="y", alpha=0.25)
    chart_path = out_dir / "landmark_availability.png"
    fig.tight_layout()
    fig.savefig(chart_path, dpi=150)
    plt.close(fig)
    return str(chart_path)


def estimate_mediapipe_lag_ms(rows):
    estimates = []
    for row in rows:
        lag = row["best_lag_seconds"]
        corr = row["corr_best_lag"]
        if not np.isfinite(lag) or not np.isfinite(corr) or corr < 0.50:
            continue
        source = row["source"]
        target = row["target"]
        if source.startswith("quest.") and target.startswith(("mp_body.", "mp_world.", "mp_norm.")):
            estimates.append(lag * 1000.0)
        elif source.startswith(("mp_body.", "mp_world.", "mp_norm.")) and target.startswith(("hmd.", "fused.", "manny.", "quest.")):
            estimates.append(-lag * 1000.0)
    if not estimates:
        return {"count": 0, "median_ms": math.nan, "p05_ms": math.nan, "p95_ms": math.nan}
    clean = finite(estimates)
    return {
        "count": int(clean.size),
        "median_ms": float(np.nanmedian(clean)),
        "p05_ms": float(np.nanpercentile(clean, 5)),
        "p95_ms": float(np.nanpercentile(clean, 95)),
    }


def stage_recommendations(rows, timing_rows, inventory_rows):
    non_measure_failures = [
        row
        for row in rows
        if row["stage_gate"] == "fail" and row["group"] in {"torso", "pelvis", "shoulders"} and not row["measurement_only"]
    ]
    stage1_rows = [row for row in rows if row["pair_kind"].startswith("stage1_") and not row["measurement_only"]]
    stage1_pass = [row for row in stage1_rows if row["stage_gate"] == "pass"]
    torso_pass = any(row["group"] == "torso" for row in stage1_pass)
    pelvis_pass = any(row["group"] == "pelvis" for row in stage1_pass)
    stage2_rows = [row for row in rows if row["pair_kind"].startswith("stage2_") and not row["measurement_only"]]
    stage2_pass = [row for row in stage2_rows if row["stage_gate"] == "pass"]
    lag = estimate_mediapipe_lag_ms(rows)

    body_inventory = [row for row in inventory_rows if row["space"] == "mp_body"]
    landmark_coverage = {
        "mp_body_all_33_valid_fraction_min": min((row["valid_fraction"] for row in body_inventory), default=math.nan),
        "mp_body_all_33_valid_fraction_median": float(np.nanmedian([row["valid_fraction"] for row in body_inventory])) if body_inventory else math.nan,
    }
    timing_counts = {row["name"]: row["count"] for row in timing_rows if row["kind"] == "sample_timing_ms"}
    has_new_timing = any(count and count > 0 for count in timing_counts.values())

    if torso_pass and pelvis_pass:
        first_active = "Stage 1 vertical pelvis/torso hint is eligible for a guarded trial."
    elif torso_pass:
        first_active = "Only torso hinting has enough evidence; keep pelvis shadow-only until pelvis variation passes."
    else:
        first_active = "Remain shadow-only; fix MediaPipe lag/alignment before enabling torso or pelvis authority."

    return {
        "lag_sign_convention": "positive best_lag_seconds means the target signal trails the source; negative means the target leads the source",
        "estimated_mediapipe_lag_ms": lag,
        "new_runtime_timing_present": has_new_timing,
        "landmark_coverage": landmark_coverage,
        "stage1_torso_pelvis_ready": bool(torso_pass and pelvis_pass),
        "stage1_pass_pairs": stage1_pass,
        "stage2_shoulder_hint_ready": bool(stage2_pass),
        "stage2_pass_pairs": stage2_pass,
        "first_active_fusion_recommendation": first_active,
        "blocked_reasons": sorted({flag for row in non_measure_failures for flag in row["flags"].split(";") if flag}),
        "safe_next_trials": [
            "mp.PrepareMPQShadowLatencyTrial maxdim=384 duration=45 prediction=1 maxPredictionMs=50 label=camo384_pred50",
            "mp.PrepareMPQShadowLatencyTrial maxdim=256 duration=45 prediction=1 maxPredictionMs=50 label=camo256_pred50",
            "mp.PrepareMPQShadowLatencyTrial maxdim=512 duration=45 prediction=1 maxPredictionMs=50 label=camo512_pred50",
        ],
        "authority_policy": {
            "head": "HMD remains authoritative; MediaPipe head/face is diagnostic only.",
            "arms": "Quest remains authoritative; MediaPipe arms are measurement-only and no fallback is added.",
            "stage1": "Only torso/pelvis hints may be considered after stage gates pass.",
            "stage2": "Shoulder/clavicle hints may be considered only after Quest reach is not contradicted.",
        },
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=Path)
    parser.add_argument("--out-dir", type=Path, default=None)
    parser.add_argument(
        "--mediapipe-advance-ms",
        type=float,
        default=0.0,
        help="Analysis-only compensation. Positive values advance MediaPipe landmark signals before scoring.",
    )
    args = parser.parse_args()
    mediapipe_advance_seconds = args.mediapipe_advance_ms * 0.001

    data = json.loads(args.input.read_text(encoding="utf-8"))
    samples = data.get("samples", [])
    if not samples:
        raise SystemExit(f"No samples found in {args.input}")

    out_dir = args.out_dir or args.input.with_suffix("").parent / f"{args.input.stem}_mpq_shadow_analysis"
    out_dir.mkdir(parents=True, exist_ok=True)
    times = np.asarray([float(sample.get("wall_t", sample.get("t", math.nan))) for sample in samples], dtype=float)
    time0 = float(np.nanmin(times)) if np.any(np.isfinite(times)) else 0.0
    times = times - time0

    signals = extract_signals(samples)
    rows = []
    for source, target, group, pair_kind in expected_pairs():
        if source in signals and target in signals:
            rows.append(pair_metrics(times, len(samples), signals, source, target, group, pair_kind, mediapipe_advance_seconds))
        else:
            missing = empty_metric_row(
                source,
                target,
                group,
                pair_kind,
                "not_recorded",
                "source signal was not recorded in this capture"
                if source not in signals and target in signals
                else "target signal was not recorded in this capture"
                if source in signals and target not in signals
                else "source and target signals were not recorded in this capture",
                mediapipe_advance_seconds,
            )
            missing["source_recorded"] = source in signals
            missing["target_recorded"] = target in signals
            rows.append(missing)
    rows.sort(key=lambda row: (row["measurement_only"], row["group"], row["pair_kind"], row["source"], row["target"]))

    pair_fields = [
        "group",
        "pair_kind",
        "source",
        "target",
        "sample_count",
        "valid_fraction",
        "source_range_p95_p05",
        "target_range_p95_p05",
        "amplitude_ratio_target_over_source",
        "corr_zero_lag",
        "best_lag_seconds",
        "corr_best_lag",
        "best_abs_lag_seconds",
        "corr_best_abs_lag",
        "source_step95_standardized",
        "target_step95_standardized",
        "step95_ratio_target_over_source",
        "measurement_only",
        "standardized_gate",
        "standardized_flags",
        "stage_gate",
        "mediapipe_advance_ms",
        "flags",
        "diagnostic_status",
        "not_valid_reason",
        "source_recorded",
        "target_recorded",
    ]
    metrics_csv = out_dir / "mpq_shadow_pair_metrics.csv"
    write_csv(metrics_csv, rows, pair_fields)

    standardized_rows = standardized_alignment_rows(rows)
    standardized_csv = out_dir / "mpq_shadow_standardized_alignment.csv"
    standardized_fields = [
        "group",
        "pair_kind",
        "source",
        "target",
        "sample_count",
        "valid_fraction",
        "source_range_p95_p05",
        "target_range_p95_p05",
        "corr_zero_lag",
        "best_lag_seconds",
        "best_lag_ms",
        "corr_best_lag",
        "best_abs_lag_seconds",
        "corr_best_abs_lag",
        "source_step95_standardized",
        "target_step95_standardized",
        "step95_ratio_target_over_source",
        "measurement_only",
        "standardized_gate",
        "standardized_flags",
        "raw_pair_gate",
        "raw_pair_flags",
        "raw_amplitude_ratio_target_over_source",
        "mediapipe_advance_ms",
        "diagnostic_status",
        "not_valid_reason",
        "source_recorded",
        "target_recorded",
    ]
    write_csv(standardized_csv, standardized_rows, standardized_fields)

    signal_rows = signal_inventory(signals, len(samples))
    signal_csv = out_dir / "mpq_shadow_signal_inventory.csv"
    write_csv(
        signal_csv,
        signal_rows,
        ["signal", "group", "source_kind", "sample_count", "valid_fraction", "p05", "p50", "p95", "range_p95_p05", "step95_standardized"],
    )

    landmark_rows = landmark_inventory(samples)
    landmark_csv = out_dir / "mpq_shadow_landmark_inventory.csv"
    write_csv(
        landmark_csv,
        landmark_rows,
        [
            "space",
            "landmark",
            "group",
            "valid_samples",
            "valid_fraction",
            "reliability_p50",
            "visibility_p50",
            "presence_p50",
            "x_range_p95_p05",
            "y_range_p95_p05",
            "z_range_p95_p05",
        ],
    )

    timing_rows, last_pipeline = timing_summary(samples)
    timing_csv = out_dir / "mpq_shadow_timing_summary.csv"
    write_csv(timing_csv, timing_rows, ["kind", "name", "count", "min", "p50", "p95", "max", "mean", "total"])

    fitted_rows = fitted_alignment_analysis(times, len(samples), signals, mediapipe_advance_seconds)
    fitted_csv = out_dir / "mpq_shadow_fitted_alignment.csv"
    fitted_fields = [
        "fit_type",
        "group",
        "fit_kind",
        "label",
        "source",
        "target",
        "sample_count",
        "valid_fraction",
        "mediapipe_advance_ms",
        "scale",
        "offset",
        "matrix_json",
        "offset_json",
        "corr_after_fit",
        "best_lag_after_fit_seconds",
        "corr_best_lag_after_fit",
        "rmse",
        "normalized_rmse",
        "rmse_best_lag",
        "normalized_rmse_best_lag",
        "r2",
        "r2_best_lag",
        "axis_corr_x",
        "axis_corr_y",
        "axis_corr_z",
        "axis_nrmse_x",
        "axis_nrmse_y",
        "axis_nrmse_z",
        "gate",
        "flags",
    ]
    write_csv(fitted_csv, fitted_rows, fitted_fields)

    region_rows = region_ownership_availability(samples)
    region_csv = out_dir / "mpq_shadow_region_ownership_availability.csv"
    region_fields = [
        "region",
        "lane",
        "path",
        "valid_samples",
        "valid_fraction",
        "loc_samples",
        "loc_fraction",
        "rot_samples",
        "quat_samples",
        "has_rotation",
        "rotation_range_max_p95_p05",
        "rotation_source",
        "owner_counts_json",
        "source_state_counts_json",
        "confidence_p50",
    ]
    write_csv(region_csv, region_rows, region_fields)

    rotation_rows = rotation_diagnostic_rows(samples, signals, times, len(samples), mediapipe_advance_seconds)
    rotation_csv = out_dir / "main_bone_rotation_correlation_summary.csv"
    rotation_fields = [
        "bone_or_region",
        "axis",
        "source",
        "target",
        "has_rotation",
        "rotation_source",
        "zero_lag_corr",
        "best_lag_ms",
        "best_lag_corr",
        "sample_count",
        "valid_fraction",
        "diagnostic_status",
        "not_valid_reason",
        "note",
    ]
    write_csv(rotation_csv, rotation_rows, rotation_fields)

    movement_rows = main_bone_movement_summary_rows(rows)
    movement_csv = out_dir / "main_bone_movement_correlation_summary.csv"
    movement_fields = [
        "bone_or_region",
        "source",
        "target",
        "zero_lag_corr",
        "best_lag_ms",
        "best_lag_corr",
        "sample_count",
        "valid_fraction",
        "diagnostic_status",
        "not_valid_reason",
        "note",
    ]
    write_csv(movement_csv, movement_rows, movement_fields)

    reason_rows = not_valid_reason_rows(rows, fitted_rows, rotation_rows)
    reason_csv = out_dir / "mpq_shadow_not_valid_reasons.csv"
    reason_fields = [
        "table",
        "group",
        "kind",
        "source",
        "target",
        "diagnostic_status",
        "not_valid_reason",
        "stage_gate",
        "standardized_gate",
        "flags",
        "sample_count",
        "valid_fraction",
    ]
    write_csv(reason_csv, reason_rows, reason_fields)

    charts = plot_groups(out_dir, times, signals, rows, mediapipe_advance_seconds)
    timing_chart = plot_timing(out_dir, times, samples)
    if timing_chart:
        charts["timing_ms"] = timing_chart
    landmark_chart = plot_landmark_availability(out_dir, landmark_rows)
    if landmark_chart:
        charts["landmark_availability"] = landmark_chart

    report = {
        "input": str(args.input),
        "schema": data.get("schema"),
        "mode": data.get("mode"),
        "sample_count": len(samples),
        "duration_seconds": float(np.nanmax(times) - np.nanmin(times)) if len(times) > 1 else 0.0,
        "signal_count": len(signals),
        "pair_count": len(rows),
        "analysis_mediapipe_advance_ms": args.mediapipe_advance_ms,
        "analysis_mediapipe_advance_note": "Positive values advance MediaPipe landmark signals offline before metrics and plots are scored.",
        "arm_policy": "arms_measurement_only_no_mediapipe_arm_fallback_decision",
        "capture_states": summarize_capture(samples),
        "face_summary": face_summary(samples),
        "last_pipeline": last_pipeline,
        "metrics_csv": str(metrics_csv),
        "standardized_alignment_csv": str(standardized_csv),
        "signal_inventory_csv": str(signal_csv),
        "landmark_inventory_csv": str(landmark_csv),
        "timing_summary_csv": str(timing_csv),
        "fitted_alignment_csv": str(fitted_csv),
        "region_ownership_availability_csv": str(region_csv),
        "not_valid_reasons_csv": str(reason_csv),
        "main_bone_movement_correlation_summary_csv": str(movement_csv),
        "main_bone_rotation_correlation_summary_csv": str(rotation_csv),
        "charts": charts,
        "outputs": {
            "metrics_csv": str(metrics_csv),
            "standardized_alignment_csv": str(standardized_csv),
            "signal_inventory_csv": str(signal_csv),
            "landmark_inventory_csv": str(landmark_csv),
            "timing_summary_csv": str(timing_csv),
            "fitted_alignment_csv": str(fitted_csv),
            "region_ownership_availability_csv": str(region_csv),
            "not_valid_reasons_csv": str(reason_csv),
            "main_bone_movement_correlation_summary_csv": str(movement_csv),
            "main_bone_rotation_correlation_summary_csv": str(rotation_csv),
            "charts": charts,
        },
        "region_ownership_availability": region_rows,
        "rotation_diagnostics": rotation_rows,
        "not_valid_reason_summary": {
            "csv": str(reason_csv),
            "count": len(reason_rows),
            "status_counts": {status: sum(1 for row in reason_rows if row["diagnostic_status"] == status) for status in sorted({row["diagnostic_status"] for row in reason_rows})},
            "rows": reason_rows,
        },
        "stage_recommendations": stage_recommendations(rows, timing_rows, landmark_rows),
        "standardized_alignment_summary": summarize_standardized_alignment(rows),
        "fitted_alignment_summary": summarize_fitted_alignment(fitted_rows),
        "physical_fit_summary": summarize_fitted_alignment(fitted_rows),
        "flagged_pairs": [row for row in rows if row["flags"]],
    }
    report_path = out_dir / "mpq_shadow_report.json"
    report_path.write_text(json.dumps(report, indent=2, sort_keys=True), encoding="utf-8")
    print(
        json.dumps(
            {
                "report": str(report_path),
                "metrics_csv": str(metrics_csv),
                "standardized_alignment_csv": str(standardized_csv),
                "signal_inventory_csv": str(signal_csv),
                "landmark_inventory_csv": str(landmark_csv),
                "timing_summary_csv": str(timing_csv),
                "fitted_alignment_csv": str(fitted_csv),
                "region_ownership_availability_csv": str(region_csv),
                "not_valid_reasons_csv": str(reason_csv),
                "main_bone_movement_correlation_summary_csv": str(movement_csv),
                "main_bone_rotation_correlation_summary_csv": str(rotation_csv),
                "charts": charts,
            },
            indent=2,
            sort_keys=True,
        )
    )


if __name__ == "__main__":
    main()
