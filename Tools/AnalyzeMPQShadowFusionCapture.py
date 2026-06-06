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
MP_WORLD_MIN_PLANAR_MOTION = 0.005
CM_MIN_POSITION_MOTION = 2.0
COMPENSATION_CORR_DELTA = 0.05
COMPENSATION_GOOD_CORR = 0.75
COMPENSATION_GOOD_LAG_SECONDS = 0.10

MEASURE_ONLY_PAIR_KINDS = {
    "arm_conflict_measure_only",
    "arm_conflict_raw_measure_only",
    "arm_conflict_world_unreal_measure_only",
    "mediapipe_candidate_vs_fused_shadow_measure",
    "quest_arm_output_measure",
    "quest_shoulder_output_measure",
    "shoulder_conflict_measure",
    "stage2_output_shoulder_compare",
}

POOR_AREA_COMPENSATION_FIELDS = [
    "area",
    "row_kind",
    "source",
    "target",
    "sample_count",
    "valid_fraction",
    "freshness_summary",
    "source_range_p95_p05",
    "target_range_p95_p05",
    "amplitude_ratio_target_over_source",
    "source_step95_standardized",
    "target_step95_standardized",
    "step95_ratio_target_over_source",
    "raw_corr_zero_lag",
    "raw_best_lag_seconds",
    "raw_best_lag_ms",
    "raw_corr_best_lag",
    "standardized_corr_zero_lag",
    "standardized_best_lag_seconds",
    "standardized_best_lag_ms",
    "standardized_corr_best_lag",
    "fit_gain",
    "fit_offset",
    "fit_sign",
    "fit_corr_zero_lag",
    "fit_best_lag_seconds",
    "fit_best_lag_ms",
    "fit_corr_best_lag",
    "fit_rmse",
    "fit_normalized_rmse",
    "fit_lag_rmse",
    "fit_lag_normalized_rmse",
    "fit_r2",
    "fit_lag_r2",
    "best_compensated_corr",
    "best_compensated_lag_seconds",
    "best_compensated_lag_ms",
    "corr_improvement_over_raw_zero",
    "corr_improvement_over_raw_best",
    "improvement_class",
    "concrete_cause",
    "authority_policy",
    "notes",
    "flags",
]

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

FACE_LANDMARK_INDICES = {
    "nose_tip": 1,
    "forehead": 10,
    "left_eye_outer": 33,
    "right_eye_outer": 263,
    "chin": 152,
}


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


def join_flags(flags):
    return ";".join(dict.fromkeys(flag for flag in flags if flag))


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


def face_normalized_landmark_points(samples, landmark_index):
    points = np.full((len(samples), 3), math.nan, dtype=float)
    for sample_index, sample in enumerate(samples):
        landmarks = nested_obj(sample, ["face", "normalized_landmarks"], [])
        if not isinstance(landmarks, list) or landmark_index >= len(landmarks):
            continue
        pos = nested_obj(landmarks[landmark_index], ["pos"], None)
        if not isinstance(pos, list) or len(pos) < 3:
            continue
        for axis_index in range(3):
            points[sample_index, axis_index] = to_float(pos[axis_index])
    return points


def add_face_normalized_proxy_signals(signals, samples):
    prefix = "face_norm"
    named_points = {
        name: face_normalized_landmark_points(samples, index)
        for name, index in FACE_LANDMARK_INDICES.items()
    }
    for name, points in named_points.items():
        add_point_axis_signals(signals, prefix, name, "head_mp_diagnostic", points, prefix)

    centroid = np.full((len(samples), 3), math.nan, dtype=float)
    for sample_index, sample in enumerate(samples):
        landmarks = nested_obj(sample, ["face", "normalized_landmarks"], [])
        if not isinstance(landmarks, list) or not landmarks:
            continue
        values = []
        for landmark in landmarks:
            pos = nested_obj(landmark, ["pos"], None)
            if isinstance(pos, list) and len(pos) >= 3:
                values.append([to_float(pos[0]), to_float(pos[1]), to_float(pos[2])])
        arr = np.asarray(values, dtype=float)
        if arr.size and np.all(np.isfinite(arr), axis=1).any():
            centroid[sample_index] = np.nanmean(arr, axis=0)
    add_point_axis_signals(signals, prefix, "centroid", "head_mp_diagnostic", centroid, prefix)

    left_eye = named_points["left_eye_outer"]
    right_eye = named_points["right_eye_outer"]
    nose = named_points["nose_tip"]
    forehead = named_points["forehead"]
    chin = named_points["chin"]
    eye_mid = midpoint(left_eye, right_eye)
    face_width = distance(left_eye, right_eye)
    face_height = distance(forehead, chin)
    add_signal(signals, f"{prefix}.eye_width", "head_mp_diagnostic", face_width, prefix)
    add_signal(signals, f"{prefix}.face_height", "head_mp_diagnostic", face_height, prefix)

    with np.errstate(invalid="ignore", divide="ignore"):
        yaw_proxy = (nose[:, AXES["x"]] - eye_mid[:, AXES["x"]]) / face_width
        pitch_proxy = (nose[:, AXES["y"]] - eye_mid[:, AXES["y"]]) / face_height
    roll_proxy = np.degrees(
        np.arctan2(
            left_eye[:, AXES["y"]] - right_eye[:, AXES["y"]],
            left_eye[:, AXES["x"]] - right_eye[:, AXES["x"]],
        )
    )
    add_signal(signals, f"{prefix}.yaw_proxy", "head_mp_diagnostic", yaw_proxy, prefix)
    add_signal(signals, f"{prefix}.pitch_proxy", "head_mp_diagnostic", pitch_proxy, prefix)
    add_signal(signals, f"{prefix}.roll_proxy", "head_mp_diagnostic", roll_proxy, prefix)


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
    add_face_normalized_proxy_signals(signals, samples)
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
        ("mp_body.nose.z", "hmd.loc.z", "head_mp_diagnostic", "stage3_head_landmark_raw_not_pose"),
        ("mp_world.nose.z", "hmd.loc.z", "head_mp_diagnostic", "stage3_head_landmark_raw_not_pose"),
        ("mp_world_unreal.nose.z", "hmd.loc.z", "head_mp_diagnostic", "stage3_head_landmark_raw_not_pose"),
        ("mp_body.ear_mid.z", "hmd.loc.z", "head_mp_diagnostic", "stage3_head_landmark_raw_not_pose"),
        ("mp_world_unreal.ear_mid.z", "hmd.loc.z", "head_mp_diagnostic", "stage3_head_landmark_raw_not_pose"),
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
                        f"mp_world_unreal.{side}_wrist.{axis}",
                        "arms_measure_only",
                        "arm_conflict_world_unreal_measure_only",
                    ),
                    (
                        f"quest.{side}.elbow.{axis}",
                        f"mp_world_unreal.{side}_elbow.{axis}",
                        "arms_measure_only",
                        "arm_conflict_world_unreal_measure_only",
                    ),
                    (
                        f"quest.{side}.wrist.{axis}",
                        f"manny.hand_{'l' if side == 'left' else 'r'}.loc.{axis}",
                        "arms_measure_only",
                        "quest_arm_output_measure",
                    ),
                ]
            )
    return pairs


def comparison_metadata(group, pair_kind, source_name, target_name):
    if pair_kind == "stage3_head_landmark_raw_not_pose":
        return {
            "comparison_role": "landmark_only_raw_space_diagnostic",
            "authority_policy": "hmd_head_authoritative_mediapipe_head_diagnostic_only",
            "interpretation": "MediaPipe face/head transform is absent in this capture; raw nose/ear landmark axes are not a proven HMD head pose.",
        }
    if pair_kind == "mediapipe_candidate_vs_fused_shadow_measure" and group == "shoulders":
        return {
            "comparison_role": "ownership_conflict_comparison",
            "authority_policy": "quest_shoulders_authoritative_candidate_diagnostic_only",
            "interpretation": "The fused shadow shoulder is Quest/mixed-owned, so MP-candidate-vs-shadow shoulder rows are ownership-conflict checks, not MediaPipe shoulder tracking failures.",
        }
    if pair_kind in {"shoulder_conflict_measure", "stage2_output_shoulder_compare"}:
        return {
            "comparison_role": "ownership_conflict_comparison",
            "authority_policy": "quest_shoulders_authoritative_no_mediapipe_shoulder_authority",
            "interpretation": "Shoulder output remains Quest/mixed-owned; use MP-candidate-to-MP-world rows for MediaPipe shoulder quality.",
        }
    if pair_kind == "quest_shoulder_output_measure":
        return {
            "comparison_role": "quest_shoulder_output_verification",
            "authority_policy": "quest_shoulders_authoritative_no_mediapipe_shoulder_authority",
            "interpretation": "Quest shoulder to fused shoulder rows verify the expected owner path; they are not MediaPipe shoulder quality rows.",
        }
    if group == "arms_measure_only" or pair_kind.startswith("arm_conflict_") or pair_kind == "quest_arm_output_measure":
        return {
            "comparison_role": "measure_only_no_authority",
            "authority_policy": "quest_hands_wrists_arms_fingers_authoritative_no_mediapipe_arm_fallback",
            "interpretation": "Arm rows quantify disagreement and lag only; they do not enable MediaPipe arm fallback.",
        }
    if pair_kind.startswith("stage1_mediapipe_candidate_pelvis") and target_name.endswith((".x", ".y")):
        return {
            "comparison_role": "planar_pelvis_diagnostic_disabled",
            "authority_policy": "vertical_pelvis_hint_only_no_planar_chasing",
            "interpretation": "Planar pelvis remains disabled; this row is an axis/calibration diagnostic only.",
        }
    if pair_kind.startswith("stage1_mediapipe_candidate_pelvis") and target_name.endswith(".z"):
        return {
            "comparison_role": "stage1_vertical_pelvis_candidate",
            "authority_policy": "vertical_pelvis_hint_only_no_planar_chasing",
            "interpretation": "Vertical pelvis/torso hint eligibility only; no planar pelvis movement is enabled.",
        }
    if pair_kind.startswith("stage1_mediapipe_candidate_torso"):
        return {
            "comparison_role": "stage1_torso_candidate",
            "authority_policy": "stage1_vertical_torso_pelvis_hint_only",
            "interpretation": "Stage 1 torso diagnostic row; visible head, hands, arms, wrists, and fingers remain Quest/HMD-owned.",
        }
    if pair_kind.startswith("stage2_mediapipe_candidate_shoulder"):
        return {
            "comparison_role": "mp_only_shoulder_candidate_quality",
            "authority_policy": "shoulder_candidate_diagnostic_only",
            "interpretation": "This compares MP-only candidate shoulders against MP/world references and is the preferred shoulder tracking-quality row.",
        }
    if pair_kind == "hmd_to_fused_head":
        return {
            "comparison_role": "hmd_to_fused_head_output",
            "authority_policy": "hmd_head_authoritative",
            "interpretation": "Fused head location is derived from the HMD/eye pose plus avatar head offset; small lateral ranges can make X lag estimates noisy.",
        }
    return {
        "comparison_role": "diagnostic",
        "authority_policy": "",
        "interpretation": "",
    }


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
    metadata = comparison_metadata(group, pair_kind, source_name, target_name)
    measurement_only = (
        group in {"arms_measure_only", "lower_body_measure_only"}
        or "measure_only" in pair_kind
        or pair_kind in MEASURE_ONLY_PAIR_KINDS
        or metadata["comparison_role"] in {"ownership_conflict_comparison", "measure_only_no_authority", "landmark_only_raw_space_diagnostic"}
    )
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
    if target_name.endswith((".x", ".y")) and pair_kind.startswith("stage1_mediapipe_candidate_pelvis"):
        if np.isfinite(source_range) and source_range < MP_WORLD_MIN_PLANAR_MOTION:
            flags.append("insufficient_planar_source_motion")
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
            "insufficient_planar_source_motion",
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

    return {
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
        "comparison_role": metadata["comparison_role"],
        "authority_policy": metadata["authority_policy"],
        "interpretation": metadata["interpretation"],
        "standardized_gate": standardized_gate,
        "standardized_flags": join_flags(standardized_flags),
        "stage_gate": stage_gate,
        "mediapipe_advance_ms": mediapipe_advance_seconds * 1000.0,
        "flags": join_flags(flags),
    }


def fit_signal_pairs():
    pairs = [
        ("stage3_head_landmark_raw_not_pose", "mp_body.ear_mid.z", "hmd.loc.z", "head_mp_diagnostic"),
        ("stage3_head_landmark_raw_not_pose", "mp_body.nose.z", "hmd.loc.z", "head_mp_diagnostic"),
        ("stage3_head_landmark_raw_not_pose", "mp_world_unreal.ear_mid.z", "hmd.loc.z", "head_mp_diagnostic"),
        ("stage3_head_landmark_raw_not_pose", "mp_world_unreal.nose.z", "hmd.loc.z", "head_mp_diagnostic"),
        ("stage1_mediapipe_candidate_torso", "mp_world_unreal.shoulder_mid.z", "mp_candidate.chest.loc.z", "torso"),
        ("stage1_mediapipe_candidate_torso", "mp_world_unreal.torso_height", "mp_candidate.torso_height", "torso"),
        ("stage1_mediapipe_candidate_torso", "mp_world_unreal.torso_forward_proxy", "mp_candidate.torso_forward_proxy", "torso"),
        ("stage1_mediapipe_candidate_torso", "mp_world_unreal.torso_side_proxy", "mp_candidate.torso_side_proxy", "torso"),
        ("stage1_mediapipe_candidate_pelvis_planar_disabled", "mp_world_unreal.hip_mid.x", "mp_candidate.pelvis.loc.x", "pelvis"),
        ("stage1_mediapipe_candidate_pelvis_planar_disabled", "mp_world_unreal.hip_mid.y", "mp_candidate.pelvis.loc.y", "pelvis"),
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
            "fit_kind": "stage3_head_landmark_raw_not_pose",
            "group": "head_mp_diagnostic",
            "source_prefix": "mp_body.ear_mid",
            "target_prefix": "hmd.loc",
            "label": "mp_body.ear_mid_xyz_to_hmd_loc_xyz",
        },
        {
            "fit_kind": "stage3_head_landmark_raw_not_pose",
            "group": "head_mp_diagnostic",
            "source_prefix": "mp_world_unreal.ear_mid",
            "target_prefix": "hmd.loc",
            "label": "mp_world_unreal.ear_mid_xyz_to_hmd_loc_xyz",
        },
        {
            "fit_kind": "stage1_mediapipe_candidate_torso",
            "group": "torso",
            "source_prefix": "mp_world_unreal.shoulder_mid",
            "target_prefix": "mp_candidate.chest.loc",
            "label": "mp_world_unreal.shoulder_mid_xyz_to_mediapipe_candidate_chest_xyz",
        },
        {
            "fit_kind": "stage1_mediapipe_candidate_pelvis_planar_disabled",
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
        and "insufficient_planar_source_motion" not in flags
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
    if fit_kind == "stage1_mediapipe_candidate_pelvis_planar_disabled":
        if not np.isfinite(source_range) or source_range < MP_WORLD_MIN_PLANAR_MOTION:
            flags.append("insufficient_planar_source_motion")

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
        "source_axis_range_x": math.nan,
        "source_axis_range_y": math.nan,
        "source_axis_range_z": math.nan,
        "target_axis_range_x": math.nan,
        "target_axis_range_y": math.nan,
        "target_axis_range_z": math.nan,
        "source_condition_number": math.nan,
        "gate": gate,
        "flags": join_flags(flags),
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
    source_condition_number = math.nan
    if any(not np.isfinite(value) or abs(value) < 1.0e-6 for value in source_axis_ranges):
        flags.append("flat_source_axis")
    if any(not np.isfinite(value) or abs(value) < 1.0e-6 for value in target_axis_ranges):
        flags.append("flat_target_axis")
    if spec["fit_kind"] == "stage1_mediapipe_candidate_pelvis_planar_disabled":
        if any(not np.isfinite(source_axis_ranges[i]) or source_axis_ranges[i] < MP_WORLD_MIN_PLANAR_MOTION for i in (0, 1)):
            flags.append("insufficient_planar_source_motion")

    if "insufficient_overlap" not in flags and "flat_source_axis" not in flags:
        x = source[mask]
        y = target[mask]
        design = np.column_stack([x, np.ones(x.shape[0])])
        try:
            source_condition_number = float(np.linalg.cond(design))
        except np.linalg.LinAlgError:
            source_condition_number = math.nan
        if np.isfinite(source_condition_number) and source_condition_number > 1000.0:
            flags.append("ill_conditioned_source_axes")
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
        and "insufficient_planar_source_motion" not in flags
        and "ill_conditioned_source_axes" not in flags
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
        "source_axis_range_x": source_axis_ranges[0],
        "source_axis_range_y": source_axis_ranges[1],
        "source_axis_range_z": source_axis_ranges[2],
        "target_axis_range_x": target_axis_ranges[0],
        "target_axis_range_y": target_axis_ranges[1],
        "target_axis_range_z": target_axis_ranges[2],
        "source_condition_number": source_condition_number,
        "gate": gate,
        "flags": join_flags(flags),
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


def axis_search_specs():
    specs = []
    for axis in ("x", "y"):
        specs.append(
            {
                "diagnostic_area": "planar_pelvis_disabled",
                "group": "pelvis",
                "target": f"mp_candidate.pelvis.loc.{axis}",
                "source_candidates": [f"mp_world_unreal.hip_mid.{candidate_axis}" for candidate_axis in ("x", "y", "z")],
                "policy": "vertical_pelvis_hint_only_no_planar_chasing",
                "source_motion_threshold": MP_WORLD_MIN_PLANAR_MOTION,
                "note": "Searches same-space MP hip axes for a better planar source; planar pelvis remains disabled.",
            }
        )
    for target_axis in ("x", "y", "z"):
        specs.append(
            {
                "diagnostic_area": "mediapipe_head_landmark_only",
                "group": "head_mp_diagnostic",
                "target": f"hmd.loc.{target_axis}",
                "source_candidates": [
                    f"mp_world_unreal.ear_mid.{axis}" for axis in ("x", "y", "z")
                ]
                + [f"mp_world_unreal.nose.{axis}" for axis in ("x", "y", "z")],
                "policy": "hmd_head_authoritative_mediapipe_head_diagnostic_only",
                "source_motion_threshold": MP_WORLD_MIN_PLANAR_MOTION,
                "note": "Face/head transform is absent; this searches landmark-only proxies and cannot prove head authority.",
            }
        )
    for side in ("left", "right"):
        for joint in ("elbow", "wrist"):
            for target_axis in ("x", "y", "z"):
                specs.append(
                    {
                        "diagnostic_area": f"arm_{side}_{joint}_measure_only",
                        "group": "arms_measure_only",
                        "target": f"quest.{side}.{joint}.{target_axis}",
                        "source_candidates": [f"mp_world_unreal.{side}_{joint}.{axis}" for axis in ("x", "y", "z")],
                        "policy": "quest_arms_hands_authoritative_no_mediapipe_arm_fallback",
                        "source_motion_threshold": MP_WORLD_MIN_PLANAR_MOTION,
                        "note": "World-to-Unreal MediaPipe arm landmark search for measurement only; no fallback is enabled.",
                    }
                )
    for target_axis in ("x", "y", "z"):
        specs.append(
            {
                "diagnostic_area": "hmd_to_fused_head_same_tick",
                "group": "head",
                "target": f"fused.head.loc.{target_axis}",
                "source_candidates": [f"hmd.loc.{target_axis}"],
                "policy": "hmd_head_authoritative",
                "source_motion_threshold": CM_MIN_POSITION_MOTION,
                "note": "Same-axis HMD camera point to fused avatar head-center output; offsets can be rotation-derived.",
            }
        )
    for side in ("left", "right"):
        hand_bone = "hand_l" if side == "left" else "hand_r"
        for target_axis in ("x", "y", "z"):
            specs.append(
                {
                    "diagnostic_area": f"quest_to_manny_{side}_hand_output",
                    "group": "arms_measure_only",
                    "target": f"manny.{hand_bone}.loc.{target_axis}",
                    "source_candidates": [f"quest.{side}.wrist.{target_axis}"],
                    "policy": "quest_hands_authoritative_output_measurement",
                    "source_motion_threshold": CM_MIN_POSITION_MOTION,
                    "note": "Quest wrist to Manny hand output check; lag here is output/writer diagnostic only.",
                }
            )
    return specs


def axis_search_analysis(times, sample_total, signals, mediapipe_advance_seconds):
    rows = []
    for spec in axis_search_specs():
        target_name = spec["target"]
        if target_name not in signals:
            continue
        target = adjusted_signal(times, signals, target_name, mediapipe_advance_seconds)
        target_range = percentile_range(target)
        best = None
        for source_name in spec["source_candidates"]:
            if source_name not in signals:
                continue
            source = adjusted_signal(times, signals, source_name, mediapipe_advance_seconds)
            valid_count = int(np.count_nonzero(np.isfinite(source) & np.isfinite(target)))
            valid_fraction = valid_count / sample_total if sample_total > 0 else 0.0
            corr_zero = safe_corr(source, target)
            lag, lag_corr = best_lag(times, source, target)
            abs_lag, abs_lag_corr = best_lag(times, source, target, maximize_abs=True)
            source_range = percentile_range(source)
            score = abs(abs_lag_corr) if np.isfinite(abs_lag_corr) else -1.0
            candidate = {
                "diagnostic_area": spec["diagnostic_area"],
                "group": spec["group"],
                "target": target_name,
                "source": source_name,
                "sample_count": valid_count,
                "valid_fraction": valid_fraction,
                "source_range_p95_p05": source_range,
                "target_range_p95_p05": target_range,
                "corr_zero_lag": corr_zero,
                "best_lag_seconds": lag,
                "corr_best_lag": lag_corr,
                "best_abs_lag_seconds": abs_lag,
                "corr_best_abs_lag": abs_lag_corr,
                "suggested_sign": -1 if np.isfinite(abs_lag_corr) and abs_lag_corr < 0.0 else 1,
                "policy": spec["policy"],
                "note": spec["note"],
                "flags": [],
            }
            if valid_fraction < 0.85:
                candidate["flags"].append("dropout_or_stale_overlap")
            if not np.isfinite(source_range) or source_range < spec["source_motion_threshold"]:
                candidate["flags"].append("insufficient_source_motion")
            if np.isfinite(abs_lag) and abs(abs_lag) > 0.10:
                candidate["flags"].append("lag_over_100ms")
            if not np.isfinite(abs_lag_corr) or abs(abs_lag_corr) < 0.70:
                candidate["flags"].append("low_axis_correlation")
            if best is None or score > best[0]:
                best = (score, candidate)
        if best:
            row = best[1]
            row["flags"] = join_flags(row["flags"])
            rows.append(row)
    return rows


def poor_area_compensation_specs():
    specs = []
    for axis in AXES:
        specs.extend(
            [
                {
                    "area": "head_hmd_to_fused_output",
                    "row_kind": "hmd_to_fused_head_position",
                    "source": f"hmd.loc.{axis}",
                    "target": f"fused.head.loc.{axis}",
                    "authority_policy": "hmd_head_authoritative_no_visible_authority_change",
                    "notes": "HMD camera/eye point compared with fused avatar head-center output.",
                },
                {
                    "area": "head_hmd_to_manny_output",
                    "row_kind": "hmd_to_manny_head_position",
                    "source": f"hmd.loc.{axis}",
                    "target": f"manny.head.loc.{axis}",
                    "authority_policy": "hmd_head_authoritative_no_visible_authority_change",
                    "notes": "Manny live head location output recorder check.",
                },
            ]
        )
    for rot_axis in ROT_AXES:
        specs.extend(
            [
                {
                    "area": "head_hmd_to_fused_output",
                    "row_kind": "hmd_to_fused_head_rotation",
                    "source": f"hmd.rot.{rot_axis}",
                    "target": f"fused.head.rot.{rot_axis}",
                    "authority_policy": "hmd_head_authoritative_no_visible_authority_change",
                    "notes": "HMD rotation to fused head rotation same-tick verification.",
                },
                {
                    "area": "head_hmd_to_manny_output",
                    "row_kind": "hmd_to_manny_head_rotation",
                    "source": f"hmd.rot.{rot_axis}",
                    "target": f"manny.head.local_rot.{rot_axis}",
                    "authority_policy": "hmd_head_authoritative_no_visible_authority_change",
                    "notes": "Manny live head local rotation recorder check.",
                },
            ]
        )
    for mp_prefix in ("mp_body", "mp_world_unreal"):
        for point in ("ear_mid", "nose"):
            for axis in AXES:
                specs.append(
                    {
                        "area": "mediapipe_head_landmark_to_hmd",
                        "row_kind": "mp_landmark_head_proxy_to_hmd",
                        "source": f"{mp_prefix}.{point}.{axis}",
                        "target": f"hmd.loc.{axis}",
                        "authority_policy": "hmd_head_authoritative_mediapipe_head_diagnostic_only",
                        "notes": "Landmark-only proxy; this capture has no usable MediaPipe face/head transform.",
                    }
                )
    for axis in AXES:
        specs.append(
            {
                "area": "mediapipe_face_proxy_to_hmd",
                "row_kind": "face_normalized_centroid_to_hmd",
                "source": f"face_norm.centroid.{axis}",
                "target": f"hmd.loc.{axis}",
                "authority_policy": "hmd_head_authoritative_mediapipe_face_proxy_diagnostic_only",
                "notes": "Dense face normalized-landmark centroid proxy; not in Unreal world space.",
            }
        )
    for proxy, rot_axis in (("yaw_proxy", "yaw"), ("pitch_proxy", "pitch"), ("roll_proxy", "roll")):
        specs.append(
            {
                "area": "mediapipe_face_proxy_to_hmd",
                "row_kind": f"face_normalized_{proxy}_to_hmd_rotation",
                "source": f"face_norm.{proxy}",
                "target": f"hmd.rot.{rot_axis}",
                "authority_policy": "hmd_head_authoritative_mediapipe_face_proxy_diagnostic_only",
                "notes": "Dense face normalized-landmark orientation proxy; diagnostic only and not a MediaPipe face transform.",
            }
        )
    for side in ("left", "right"):
        hand_bone = "hand_l" if side == "left" else "hand_r"
        for axis in AXES:
            specs.append(
                {
                    "area": "quest_wrist_to_manny_hand_output",
                    "row_kind": f"quest_{side}_wrist_to_manny_hand",
                    "source": f"quest.{side}.wrist.{axis}",
                    "target": f"manny.{hand_bone}.loc.{axis}",
                    "authority_policy": "quest_hands_wrists_arms_fingers_authoritative_no_mediapipe_arm_fallback",
                    "notes": "Quest wrist to Manny hand output write/recording check.",
                }
            )
    for point, group in (("chest", "torso"), ("pelvis", "pelvis")):
        for axis in AXES:
            source = f"mp_candidate.{point}.loc.{axis}"
            for target_prefix in ("shadow", "fused"):
                specs.append(
                    {
                        "area": f"mediapipe_candidate_{group}_context",
                        "row_kind": f"mp_candidate_{point}_to_{target_prefix}",
                        "source": source,
                        "target": f"{target_prefix}.{point}.loc.{axis}",
                        "authority_policy": "stage0_shadow_diagnostic_no_visible_mediapipe_authority",
                        "notes": "Candidate compared with shadow/fused context to separate candidate tracking from ownership/output.",
                    }
                )
            manny_target = "manny.spine_03.loc" if point == "chest" else "manny.pelvis.loc"
            specs.append(
                {
                    "area": f"mediapipe_candidate_{group}_context",
                    "row_kind": f"mp_candidate_{point}_to_manny_output",
                    "source": source,
                    "target": f"{manny_target}.{axis}",
                    "authority_policy": "stage0_shadow_diagnostic_no_visible_mediapipe_authority",
                    "notes": "Candidate compared with Manny live output; output may be intentionally non-MediaPipe-owned.",
                }
            )
    for signal in ("torso_height", "torso_side_proxy", "torso_forward_proxy"):
        for target_prefix in ("shadow", "fused", "manny"):
            target = f"{target_prefix}.{signal}"
            if target_prefix == "manny" and signal != "torso_height":
                continue
            specs.append(
                {
                    "area": "mediapipe_candidate_torso_context",
                    "row_kind": f"mp_candidate_{signal}_to_{target_prefix}",
                    "source": f"mp_candidate.{signal}",
                    "target": target,
                    "authority_policy": "stage0_shadow_diagnostic_no_visible_mediapipe_authority",
                    "notes": "Torso shape proxy comparison across candidate, shadow, fused, and live output where available.",
                }
            )
    for side in ("left", "right"):
        clavicle_bone = "clavicle_l" if side == "left" else "clavicle_r"
        for axis in AXES:
            for target_prefix in ("shadow", "fused", "quest"):
                target = f"{target_prefix}.{side}.shoulder.{axis}" if target_prefix != "quest" else f"quest.{side}.shoulder.{axis}"
                specs.append(
                    {
                        "area": "mediapipe_shoulder_candidate_context",
                        "row_kind": f"mp_candidate_{side}_shoulder_to_{target_prefix}",
                        "source": f"mp_candidate.{side}.shoulder.{axis}",
                        "target": target,
                        "authority_policy": "quest_shoulders_authoritative_candidate_diagnostic_only",
                        "notes": "MP candidate shoulder compared with Quest/fused/shadow context; ownership conflicts are expected.",
                    }
                )
            specs.append(
                {
                    "area": "mediapipe_shoulder_candidate_context",
                    "row_kind": f"mp_candidate_{side}_shoulder_to_manny_clavicle",
                    "source": f"mp_candidate.{side}.shoulder.{axis}",
                    "target": f"manny.{clavicle_bone}.loc.{axis}",
                    "authority_policy": "quest_shoulders_authoritative_candidate_diagnostic_only",
                    "notes": "MP candidate shoulder compared with Manny clavicle output.",
                }
            )
        for source_prefix in ("mp_candidate", "mp_world_unreal"):
            source = (
                f"{source_prefix}.{side}.shoulder_lift_from_pelvis"
                if source_prefix == "mp_candidate"
                else f"{source_prefix}.{side}_shoulder_lift_from_hips"
            )
            for target in (
                f"shadow.{side}.shoulder_lift_from_pelvis",
                f"fused.{side}.shoulder_lift_from_pelvis",
                f"manny.{clavicle_bone}_lift_from_pelvis",
            ):
                specs.append(
                    {
                        "area": "mediapipe_shoulder_candidate_context",
                        "row_kind": f"{source_prefix}_{side}_shoulder_lift_context",
                        "source": source,
                        "target": target,
                        "authority_policy": "quest_shoulders_authoritative_candidate_diagnostic_only",
                        "notes": "Shoulder lift comparison separates MP-only vertical quality from Quest/Manny-owned output.",
                    }
                )
    return specs


def signal_freshness_summary(source_name, target_name, capture_states):
    parts = []
    names = (source_name, target_name)
    if any(name.startswith("hmd.") for name in names):
        parts.append(f"hmd={capture_states.get('hmd', {})}")
    if any(name.startswith(("mp_body.", "mp_world.", "mp_world_unreal.")) for name in names):
        parts.append(f"body_pose={capture_states.get('body_pose', {})}")
    if any(name.startswith("face_norm.") for name in names):
        parts.append("face_norm=normalized_landmarks_only")
    if any(name.startswith("mp_candidate.") for name in names):
        parts.append(f"mp_candidate_available={capture_states.get('mediapipe_candidate_available', {})}")
    if any(".left." in name or "_left_" in name or name.startswith("quest.left.") for name in names):
        parts.append(f"left_arm_chain={capture_states.get('left_arm_chain', {})}")
    if any(".right." in name or "_right_" in name or name.startswith("quest.right.") for name in names):
        parts.append(f"right_arm_chain={capture_states.get('right_arm_chain', {})}")
    if not parts:
        return "recorded_signal_validity_only"
    return " | ".join(parts)


def fit_line_metrics(times, source, target):
    mask = np.isfinite(source) & np.isfinite(target)
    target_range = percentile_range(target)
    out = {
        "gain": math.nan,
        "offset": math.nan,
        "sign": 0,
        "fitted": np.full_like(source, math.nan, dtype=float),
        "corr_zero": math.nan,
        "best_lag": math.nan,
        "corr_best_lag": math.nan,
        "rmse": math.nan,
        "normalized_rmse": math.nan,
        "lag_rmse": math.nan,
        "lag_normalized_rmse": math.nan,
        "r2": math.nan,
        "lag_r2": math.nan,
    }
    if np.count_nonzero(mask) < 8 or percentile_range(source) < 1.0e-9:
        return out
    x = source[mask]
    y = target[mask]
    coeff, _, _, _ = np.linalg.lstsq(np.column_stack([x, np.ones_like(x)]), y, rcond=None)
    gain = float(coeff[0])
    offset = float(coeff[1])
    fitted = gain * source + offset
    residual = y - fitted[mask]
    rmse = float(np.sqrt(np.nanmean(residual * residual)))
    total = np.nansum((y - np.nanmean(y)) ** 2)
    r2 = float(1.0 - np.nansum(residual * residual) / total) if np.isfinite(total) and total > 1.0e-9 else math.nan
    lag, corr_lag = best_lag(times, fitted, target)
    aligned_fitted, aligned_target = lag_aligned_arrays(times, fitted, target, lag)
    lag_rmse = math.nan
    lag_r2 = math.nan
    if aligned_fitted.size > 0:
        lag_residual = aligned_target - aligned_fitted
        lag_rmse = float(np.sqrt(np.nanmean(lag_residual * lag_residual)))
        lag_total = np.nansum((aligned_target - np.nanmean(aligned_target)) ** 2)
        lag_r2 = float(1.0 - np.nansum(lag_residual * lag_residual) / lag_total) if np.isfinite(lag_total) and lag_total > 1.0e-9 else math.nan
    out.update(
        {
            "gain": gain,
            "offset": offset,
            "sign": -1 if gain < 0.0 else 1,
            "fitted": fitted,
            "corr_zero": safe_corr(fitted, target),
            "best_lag": lag,
            "corr_best_lag": corr_lag,
            "rmse": rmse,
            "normalized_rmse": rmse / target_range if np.isfinite(target_range) and target_range > 1.0e-9 else math.nan,
            "lag_rmse": lag_rmse,
            "lag_normalized_rmse": lag_rmse / target_range if np.isfinite(lag_rmse) and np.isfinite(target_range) and target_range > 1.0e-9 else math.nan,
            "r2": r2,
            "lag_r2": lag_r2,
        }
    )
    return out


def classify_compensation_cause(row, face_info):
    flags = [flag for flag in row["flags"].split(";") if flag]
    source = row["source"]
    target = row["target"]
    if row["area"] == "mediapipe_face_proxy_to_hmd" and row["valid_fraction"] < 0.25:
        return "face_landmark_coverage_too_sparse_and_not_unreal_space"
    if row["sample_count"] < 8 or row["valid_fraction"] < 0.25:
        return "unavailable_capture_field_or_insufficient_overlap"
    if "flat_target" in flags:
        return "target_output_field_flat_or_not_recording_runtime_motion"
    if "flat_source" in flags:
        return "source_signal_flat_or_not_exercised"
    if row["area"] == "mediapipe_head_landmark_to_hmd" and face_info.get("has_transform_samples", 0) == 0:
        return "missing_face_head_transform_raw_landmarks_not_head_pose"
    if "insufficient_planar_source_motion" in flags or (
        source.startswith("mp_world_unreal.hip_mid.") and row["source_range_p95_p05"] < MP_WORLD_MIN_PLANAR_MOTION
    ):
        return "insufficient_isolated_planar_pelvis_motion_for_axis_fit"
    if row["area"] == "head_hmd_to_fused_output" and source.endswith(".x") and row["source_range_p95_p05"] < 4.0:
        return "weak_lateral_head_motion_plus_head_center_vs_eye_offset"
    if row["area"] == "quest_wrist_to_manny_hand_output" and abs(row["best_compensated_lag_seconds"]) > COMPENSATION_GOOD_LAG_SECONDS:
        return "quest_to_manny_output_write_or_recording_lag"
    if row["area"] == "mediapipe_shoulder_candidate_context" and target.startswith(("quest.", "fused.", "manny.")):
        return "ownership_context_mismatch_quest_or_output_owned_not_mp_candidate_failure"
    if row["area"].startswith("mediapipe_candidate") and target.startswith(("fused.", "manny.")):
        return "visible_output_or_live_bone_not_owned_by_mediapipe_candidate"
    if abs(row["best_compensated_lag_seconds"]) > COMPENSATION_GOOD_LAG_SECONDS:
        return "timing_or_output_latency_after_compensation"
    if row["best_compensated_corr"] >= COMPENSATION_GOOD_CORR:
        return "good_or_improved_after_fitted_lag_compensation"
    if "amplitude_mismatch" in flags:
        return "coordinate_space_or_amplitude_mismatch"
    return "low_correlation_after_compensation"


def poor_area_compensation_analysis(times, sample_total, signals, capture_states, face_info, mediapipe_advance_seconds):
    rows = []
    for spec in poor_area_compensation_specs():
        source_name = spec["source"]
        target_name = spec["target"]
        if source_name not in signals or target_name not in signals:
            continue
        source = adjusted_signal(times, signals, source_name, mediapipe_advance_seconds)
        target = adjusted_signal(times, signals, target_name, mediapipe_advance_seconds)
        mask = np.isfinite(source) & np.isfinite(target)
        valid_count = int(np.count_nonzero(mask))
        valid_fraction = valid_count / sample_total if sample_total > 0 else 0.0
        source_range = percentile_range(source)
        target_range = percentile_range(target)
        source_step = step95(source)
        target_step = step95(target)
        amp_ratio = target_range / source_range if np.isfinite(source_range) and source_range > 1.0e-9 else math.nan
        step_ratio = target_step / source_step if np.isfinite(source_step) and source_step > 1.0e-9 else math.nan
        raw_corr_zero = safe_corr(source, target)
        raw_lag, raw_lag_corr = best_lag(times, source, target)
        source_std = standardize(source)
        target_std = standardize(target)
        std_corr_zero = safe_corr(source_std, target_std)
        std_lag, std_lag_corr = best_lag(times, source_std, target_std)
        fit = fit_line_metrics(times, source, target)
        best_comp_corr = fit["corr_best_lag"] if np.isfinite(fit["corr_best_lag"]) else std_lag_corr
        best_comp_lag = fit["best_lag"] if np.isfinite(fit["best_lag"]) else std_lag
        raw_best_for_delta = raw_lag_corr if np.isfinite(raw_lag_corr) else math.nan
        improvement_zero = best_comp_corr - raw_corr_zero if np.isfinite(best_comp_corr) and np.isfinite(raw_corr_zero) else math.nan
        improvement_best = best_comp_corr - raw_best_for_delta if np.isfinite(best_comp_corr) and np.isfinite(raw_best_for_delta) else math.nan
        flags = []
        if valid_count < 8 or valid_fraction < 0.25:
            flags.append("insufficient_overlap")
        if not np.isfinite(source_range) or source_range < 1.0e-6:
            flags.append("flat_source")
        if not np.isfinite(target_range) or target_range < 1.0e-6:
            flags.append("flat_target")
        if source_name.startswith("mp_world_unreal.hip_mid.") and source_name.endswith((".x", ".y")) and source_range < MP_WORLD_MIN_PLANAR_MOTION:
            flags.append("insufficient_planar_source_motion")
        if not np.isfinite(best_comp_corr) or best_comp_corr < COMPENSATION_GOOD_CORR:
            flags.append("low_compensated_correlation")
        if np.isfinite(best_comp_lag) and abs(best_comp_lag) > COMPENSATION_GOOD_LAG_SECONDS:
            flags.append("lag_over_100ms_after_compensation")
        if np.isfinite(amp_ratio) and (amp_ratio < 0.25 or amp_ratio > 4.0):
            flags.append("amplitude_mismatch")
        if np.isfinite(step_ratio) and step_ratio > 4.0:
            flags.append("jitter_or_step_mismatch")
        if valid_fraction < 0.85:
            flags.append("dropout_or_stale_overlap")
        row = {
            "area": spec["area"],
            "row_kind": spec["row_kind"],
            "source": source_name,
            "target": target_name,
            "sample_count": valid_count,
            "valid_fraction": valid_fraction,
            "freshness_summary": signal_freshness_summary(source_name, target_name, capture_states),
            "source_range_p95_p05": source_range,
            "target_range_p95_p05": target_range,
            "amplitude_ratio_target_over_source": amp_ratio,
            "source_step95_standardized": source_step,
            "target_step95_standardized": target_step,
            "step95_ratio_target_over_source": step_ratio,
            "raw_corr_zero_lag": raw_corr_zero,
            "raw_best_lag_seconds": raw_lag,
            "raw_best_lag_ms": raw_lag * 1000.0 if np.isfinite(raw_lag) else math.nan,
            "raw_corr_best_lag": raw_lag_corr,
            "standardized_corr_zero_lag": std_corr_zero,
            "standardized_best_lag_seconds": std_lag,
            "standardized_best_lag_ms": std_lag * 1000.0 if np.isfinite(std_lag) else math.nan,
            "standardized_corr_best_lag": std_lag_corr,
            "fit_gain": fit["gain"],
            "fit_offset": fit["offset"],
            "fit_sign": fit["sign"],
            "fit_corr_zero_lag": fit["corr_zero"],
            "fit_best_lag_seconds": fit["best_lag"],
            "fit_best_lag_ms": fit["best_lag"] * 1000.0 if np.isfinite(fit["best_lag"]) else math.nan,
            "fit_corr_best_lag": fit["corr_best_lag"],
            "fit_rmse": fit["rmse"],
            "fit_normalized_rmse": fit["normalized_rmse"],
            "fit_lag_rmse": fit["lag_rmse"],
            "fit_lag_normalized_rmse": fit["lag_normalized_rmse"],
            "fit_r2": fit["r2"],
            "fit_lag_r2": fit["lag_r2"],
            "best_compensated_corr": best_comp_corr,
            "best_compensated_lag_seconds": best_comp_lag,
            "best_compensated_lag_ms": best_comp_lag * 1000.0 if np.isfinite(best_comp_lag) else math.nan,
            "corr_improvement_over_raw_zero": improvement_zero,
            "corr_improvement_over_raw_best": improvement_best,
            "improvement_class": "",
            "concrete_cause": "",
            "authority_policy": spec["authority_policy"],
            "notes": spec["notes"],
            "flags": join_flags(flags),
        }
        if np.isfinite(improvement_zero) and improvement_zero >= COMPENSATION_CORR_DELTA:
            row["improvement_class"] = "improved_numerically"
        elif np.isfinite(best_comp_corr) and best_comp_corr >= COMPENSATION_GOOD_CORR and (
            not np.isfinite(best_comp_lag) or abs(best_comp_lag) <= COMPENSATION_GOOD_LAG_SECONDS
        ):
            row["improvement_class"] = "already_good_or_alignment_confirmed"
        else:
            row["improvement_class"] = "explained_but_still_poor"
        row["concrete_cause"] = classify_compensation_cause(row, face_info)
        rows.append(row)
    rows.sort(key=lambda row: (row["area"], row["row_kind"], row["source"], row["target"]))
    return rows


def summarize_poor_area_compensation(rows):
    by_area = {}
    for row in rows:
        area = by_area.setdefault(
            row["area"],
            {
                "row_count": 0,
                "improved_numerically": 0,
                "already_good_or_alignment_confirmed": 0,
                "explained_but_still_poor": 0,
                "causes": {},
                "best_rows": [],
                "remaining_poor_rows": [],
            },
        )
        area["row_count"] += 1
        area[row["improvement_class"]] += 1
        cause = row["concrete_cause"]
        area["causes"][cause] = area["causes"].get(cause, 0) + 1
        compact = {
            "source": row["source"],
            "target": row["target"],
            "raw_corr_zero_lag": row["raw_corr_zero_lag"],
            "raw_best_lag_ms": row["raw_best_lag_ms"],
            "raw_corr_best_lag": row["raw_corr_best_lag"],
            "best_compensated_corr": row["best_compensated_corr"],
            "best_compensated_lag_ms": row["best_compensated_lag_ms"],
            "fit_gain": row["fit_gain"],
            "fit_offset": row["fit_offset"],
            "cause": cause,
        }
        if row["improvement_class"] == "improved_numerically":
            area["best_rows"].append(compact)
        elif row["improvement_class"] == "explained_but_still_poor":
            area["remaining_poor_rows"].append(compact)
    for area in by_area.values():
        area["best_rows"] = sorted(area["best_rows"], key=lambda row: row["best_compensated_corr"] if np.isfinite(row["best_compensated_corr"]) else -1.0, reverse=True)[:8]
        area["remaining_poor_rows"] = sorted(
            area["remaining_poor_rows"],
            key=lambda row: row["best_compensated_corr"] if np.isfinite(row["best_compensated_corr"]) else -1.0,
        )[:12]
    return by_area


def summarize_fitted_alignment(rows):
    pass_rows = [row for row in rows if row["gate"] == "pass"]
    stage1_pass = [row for row in pass_rows if row["fit_kind"].startswith("stage1_")]
    stage2_pass = [row for row in pass_rows if row["fit_kind"].startswith("stage2_")]
    head_pass = [row for row in pass_rows if row["fit_kind"].startswith("stage3_")]
    blocked = sorted({flag for row in rows if row["gate"] == "fail" for flag in row["flags"].split(";") if flag})
    if any(row["group"] == "torso" for row in stage1_pass) and any(row["group"] == "pelvis" for row in stage1_pass):
        recommendation = "Fitted Stage 1 vertical torso/pelvis looks calibratable offline; keep planar pelvis disabled until isolated planar motion passes."
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
                "comparison_role": row["comparison_role"],
                "authority_policy": row["authority_policy"],
                "interpretation": row["interpretation"],
                "standardized_gate": row["standardized_gate"],
                "standardized_flags": row["standardized_flags"],
                "raw_pair_gate": row["stage_gate"],
                "raw_pair_flags": row["flags"],
                "raw_amplitude_ratio_target_over_source": row["amplitude_ratio_target_over_source"],
                "mediapipe_advance_ms": row["mediapipe_advance_ms"],
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


def row_by_pair(rows, source, target):
    for row in rows:
        if row["source"] == source and row["target"] == target:
            return row
    return None


def summarize_reclassifications(pair_rows, fitted_rows, axis_rows, face_info):
    pelvis_axis = [row for row in axis_rows if row["diagnostic_area"] == "planar_pelvis_disabled"]
    pelvis_fit = [row for row in fitted_rows if row["fit_kind"] == "stage1_mediapipe_candidate_pelvis_planar_disabled"]
    head_axis = [row for row in axis_rows if row["diagnostic_area"] == "mediapipe_head_landmark_only"]
    shoulder_mp = [
        row
        for row in pair_rows
        if row["pair_kind"] == "stage2_mediapipe_candidate_shoulder" and row["standardized_gate"] == "pass"
    ]
    shoulder_conflicts = [
        row
        for row in pair_rows
        if row["group"] == "shoulders" and row["comparison_role"] == "ownership_conflict_comparison"
    ]
    hmd_x = row_by_pair(pair_rows, "hmd.loc.x", "fused.head.loc.x")
    left_hand_xy = [
        row_by_pair(pair_rows, "quest.left.wrist.x", "manny.hand_l.loc.x"),
        row_by_pair(pair_rows, "quest.left.wrist.y", "manny.hand_l.loc.y"),
    ]
    left_hand_xy = [row for row in left_hand_xy if row]

    pelvis_planar_flags = sorted({flag for row in pelvis_axis + pelvis_fit for flag in str(row.get("flags", "")).split(";") if flag})
    head_flags = sorted({flag for row in head_axis for flag in str(row.get("flags", "")).split(";") if flag})
    face_transform_samples = face_info.get("has_transform_samples", 0)
    hmd_x_lag_ms = hmd_x["best_lag_seconds"] * 1000.0 if hmd_x and np.isfinite(hmd_x["best_lag_seconds"]) else math.nan
    hmd_x_range = hmd_x["source_range_p95_p05"] if hmd_x else math.nan

    return {
        "planar_pelvis_xy": {
            "status": "disabled_reclassified_as_insufficient_planar_motion",
            "rows_considered": len(pelvis_axis) + len(pelvis_fit),
            "flags": pelvis_planar_flags,
            "conclusion": "Existing capture does not provide enough MediaPipe hip planar motion for a stable yaw/offset/sign fit; keep planar pelvis chasing disabled.",
        },
        "mediapipe_head": {
            "status": "landmark_only_not_head_pose",
            "face_transform_samples": int(face_transform_samples),
            "axis_search_flags": head_flags,
            "conclusion": "No usable face/head transform was recorded, so raw nose/ear axes are diagnostic landmarks only. HMD remains authoritative for head and camera.",
        },
        "shoulders": {
            "mp_only_candidate_pass_count": len(shoulder_mp),
            "ownership_conflict_row_count": len(shoulder_conflicts),
            "conclusion": "Use MP-world-to-MP-candidate shoulder rows for MediaPipe shoulder quality; candidate-vs-fused/Quest-owned rows are ownership-conflict checks.",
        },
        "arms": {
            "status": "measure_only_no_fallback",
            "world_unreal_axis_search_rows": len([row for row in axis_rows if row["group"] == "arms_measure_only"]),
            "conclusion": "Arm diagnostics include per-axis world-to-Unreal landmark searches and lag flags only; Quest remains authoritative and no MediaPipe arm fallback is enabled.",
        },
        "hmd_head_x": {
            "best_lag_ms": hmd_x_lag_ms,
            "source_range_cm_p95_p05": hmd_x_range,
            "conclusion": "HMD X has weak lateral motion and fused head is an HMD-derived head-center output, not the raw eye point; collect isolated lateral head motion before treating the X lag as real latency.",
        },
        "quest_left_hand_output": {
            "best_lag_ms_by_axis": {
                row["source"].split(".")[-1]: row["best_lag_seconds"] * 1000.0 if np.isfinite(row["best_lag_seconds"]) else math.nan
                for row in left_hand_xy
            },
            "conclusion": "Left hand X/Y lag remains a measurement-only output issue; use an isolated left-hand side/forward/back capture to separate writer smoothing from timestamp or coordinate artifacts.",
        },
        "needs_user_vr_preview": True,
        "recommended_isolated_capture_sequence": [
            "Keep torso mostly still; move pelvis side-to-side only for 10 seconds, then forward/back only for 10 seconds.",
            "Keep body and hands still; move head laterally left/right for 10 seconds without nodding.",
            "Keep head/torso still; move left hand side-to-side, forward/back, then up/down for 10 seconds each.",
            "Repeat right hand as a control if left-hand output lag remains asymmetric.",
        ],
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
        group_rows = [row for row in rows if row["group"] == group]
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


def plot_lag_compensated_groups(out_dir, times, signals, rows, mediapipe_advance_seconds=0.0):
    charts = {}
    wanted_groups = {"arms_measure_only", "head", "head_mp_diagnostic", "pelvis", "shoulders"}
    wanted_rows = [
        row
        for row in rows
        if row["group"] in wanted_groups
        and row["flags"]
        and np.isfinite(row["best_lag_seconds"])
        and abs(row["best_lag_seconds"]) > 0.050
    ]
    for group in sorted({row["group"] for row in wanted_rows}):
        group_rows = [row for row in wanted_rows if row["group"] == group]
        for chunk_start in range(0, len(group_rows), 10):
            chunk = group_rows[chunk_start : chunk_start + 10]
            fig, axes = plt.subplots(len(chunk), 1, figsize=(15, max(3.0, 2.2 * len(chunk))), sharex=False)
            if len(chunk) == 1:
                axes = [axes]
            for ax, row in zip(axes, chunk):
                source = standardize(adjusted_signal(times, signals, row["source"], mediapipe_advance_seconds))
                target = standardize(adjusted_signal(times, signals, row["target"], mediapipe_advance_seconds))
                shifted_source_times = times + row["best_lag_seconds"]
                ax.plot(shifted_source_times, source, label=f"{row['source']} shifted by best lag", linewidth=1.2)
                ax.plot(times, target, label=row["target"], linewidth=1.1)
                ax.grid(True, alpha=0.25)
                ax.legend(fontsize=8, loc="upper right")
                ax.set_title(
                    f"{row['pair_kind']} lag={row['best_lag_seconds'] * 1000.0:.1f}ms "
                    f"corr={row['corr_best_lag']:.3f} role={row['comparison_role']} flags={row['flags'] or 'none'}",
                    fontsize=9,
                )
            axes[-1].set_xlabel("capture seconds")
            fig.suptitle(f"{group}: lag-compensated diagnostic signals")
            fig.tight_layout(rect=[0, 0, 1, 0.97])
            suffix = "" if chunk_start == 0 else f"_{chunk_start // 10 + 1:02d}"
            chart_path = out_dir / f"{group}_lag_compensated{suffix}.png"
            fig.savefig(chart_path, dpi=150)
            plt.close(fig)
            charts[f"{group}_lag_compensated{suffix}"] = str(chart_path)
    return charts


def plot_poor_area_compensation(out_dir, times, signals, rows, mediapipe_advance_seconds=0.0):
    charts = {}
    for area in sorted({row["area"] for row in rows}):
        area_rows = [row for row in rows if row["area"] == area]
        for chunk_start in range(0, len(area_rows), 8):
            chunk = area_rows[chunk_start : chunk_start + 8]
            fig, axes = plt.subplots(len(chunk), 1, figsize=(16, max(3.2, 2.5 * len(chunk))), sharex=False)
            if len(chunk) == 1:
                axes = [axes]
            for ax, row in zip(axes, chunk):
                source = adjusted_signal(times, signals, row["source"], mediapipe_advance_seconds)
                target = adjusted_signal(times, signals, row["target"], mediapipe_advance_seconds)
                fit = fit_line_metrics(times, source, target)
                ax.plot(times, standardize(source), label=f"raw {row['source']}", linewidth=1.0, alpha=0.75)
                ax.plot(times, standardize(target), label=f"target {row['target']}", linewidth=1.0, alpha=0.85)
                if np.count_nonzero(np.isfinite(fit["fitted"])) >= MIN_SIGNAL_SAMPLES:
                    fit_lag = fit["best_lag"] if np.isfinite(fit["best_lag"]) else 0.0
                    ax.plot(
                        times + fit_lag,
                        standardize(fit["fitted"]),
                        label=f"fitted source shifted {fit_lag * 1000.0:.1f}ms",
                        linewidth=1.25,
                        alpha=0.9,
                    )
                ax.grid(True, alpha=0.25)
                ax.legend(fontsize=7, loc="upper right")
                ax.set_title(
                    f"{row['row_kind']} raw0={row['raw_corr_zero_lag']:.3f} rawBest={row['raw_corr_best_lag']:.3f} "
                    f"fitBest={row['fit_corr_best_lag']:.3f} gain={row['fit_gain']:.3g} "
                    f"lag={row['best_compensated_lag_ms']:.1f}ms class={row['improvement_class']} cause={row['concrete_cause']}",
                    fontsize=8,
                )
            axes[-1].set_xlabel("capture seconds")
            fig.suptitle(f"{area}: raw vs fitted lag-compensated poor-area diagnostics")
            fig.tight_layout(rect=[0, 0, 1, 0.97])
            suffix = "" if chunk_start == 0 else f"_{chunk_start // 8 + 1:02d}"
            chart_path = out_dir / f"poor_area_compensation_{area}{suffix}.png"
            fig.savefig(chart_path, dpi=150)
            plt.close(fig)
            charts[f"poor_area_compensation_{area}{suffix}"] = str(chart_path)
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
    pelvis_vertical_pass = any(row["group"] == "pelvis" and row["target"].endswith(".z") for row in stage1_pass)
    pelvis_planar_pass = all(
        any(row["target"].endswith(f".{axis}") and row["stage_gate"] == "pass" for row in stage1_rows)
        for axis in ("x", "y")
    )
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

    if torso_pass and pelvis_vertical_pass:
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
        "stage1_torso_pelvis_ready": bool(torso_pass and pelvis_vertical_pass),
        "stage1_vertical_torso_pelvis_ready": bool(torso_pass and pelvis_vertical_pass),
        "stage1_planar_pelvis_ready": bool(pelvis_planar_pass),
        "stage1_planar_policy": "planar_pelvis_disabled_no_chasing",
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
        "comparison_role",
        "authority_policy",
        "interpretation",
        "standardized_gate",
        "standardized_flags",
        "stage_gate",
        "mediapipe_advance_ms",
        "flags",
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
        "comparison_role",
        "authority_policy",
        "interpretation",
        "standardized_gate",
        "standardized_flags",
        "raw_pair_gate",
        "raw_pair_flags",
        "raw_amplitude_ratio_target_over_source",
        "mediapipe_advance_ms",
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
        "source_axis_range_x",
        "source_axis_range_y",
        "source_axis_range_z",
        "target_axis_range_x",
        "target_axis_range_y",
        "target_axis_range_z",
        "source_condition_number",
        "gate",
        "flags",
    ]
    write_csv(fitted_csv, fitted_rows, fitted_fields)

    axis_rows = axis_search_analysis(times, len(samples), signals, mediapipe_advance_seconds)
    axis_csv = out_dir / "mpq_shadow_axis_search_diagnostics.csv"
    axis_fields = [
        "diagnostic_area",
        "group",
        "target",
        "source",
        "sample_count",
        "valid_fraction",
        "source_range_p95_p05",
        "target_range_p95_p05",
        "corr_zero_lag",
        "best_lag_seconds",
        "corr_best_lag",
        "best_abs_lag_seconds",
        "corr_best_abs_lag",
        "suggested_sign",
        "policy",
        "note",
        "flags",
    ]
    write_csv(axis_csv, axis_rows, axis_fields)

    capture_states = summarize_capture(samples)
    face_info = face_summary(samples)
    compensation_rows = poor_area_compensation_analysis(
        times,
        len(samples),
        signals,
        capture_states,
        face_info,
        mediapipe_advance_seconds,
    )
    compensation_csv = out_dir / "mpq_shadow_poor_area_compensation.csv"
    write_csv(compensation_csv, compensation_rows, POOR_AREA_COMPENSATION_FIELDS)

    charts = plot_groups(out_dir, times, signals, rows, mediapipe_advance_seconds)
    charts.update(plot_lag_compensated_groups(out_dir, times, signals, rows, mediapipe_advance_seconds))
    charts.update(plot_poor_area_compensation(out_dir, times, signals, compensation_rows, mediapipe_advance_seconds))
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
        "capture_states": capture_states,
        "face_summary": face_info,
        "last_pipeline": last_pipeline,
        "metrics_csv": str(metrics_csv),
        "standardized_alignment_csv": str(standardized_csv),
        "signal_inventory_csv": str(signal_csv),
        "landmark_inventory_csv": str(landmark_csv),
        "timing_summary_csv": str(timing_csv),
        "fitted_alignment_csv": str(fitted_csv),
        "axis_search_diagnostics_csv": str(axis_csv),
        "poor_area_compensation_csv": str(compensation_csv),
        "charts": charts,
        "outputs": {
            "metrics_csv": str(metrics_csv),
            "standardized_alignment_csv": str(standardized_csv),
            "signal_inventory_csv": str(signal_csv),
            "landmark_inventory_csv": str(landmark_csv),
            "timing_summary_csv": str(timing_csv),
            "fitted_alignment_csv": str(fitted_csv),
            "axis_search_diagnostics_csv": str(axis_csv),
            "poor_area_compensation_csv": str(compensation_csv),
            "charts": charts,
        },
        "stage_recommendations": stage_recommendations(rows, timing_rows, landmark_rows),
        "standardized_alignment_summary": summarize_standardized_alignment(rows),
        "fitted_alignment_summary": summarize_fitted_alignment(fitted_rows),
        "physical_fit_summary": summarize_fitted_alignment(fitted_rows),
        "diagnostic_reclassification_summary": summarize_reclassifications(rows, fitted_rows, axis_rows, face_info),
        "poor_area_compensation_summary": summarize_poor_area_compensation(compensation_rows),
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
                "axis_search_diagnostics_csv": str(axis_csv),
                "poor_area_compensation_csv": str(compensation_csv),
                "charts": charts,
            },
            indent=2,
            sort_keys=True,
        )
    )


if __name__ == "__main__":
    main()
