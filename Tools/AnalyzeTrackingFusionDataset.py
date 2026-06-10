#!/usr/bin/env python3
"""Analyze Quest/MediaPipe/BodyFusion/avatar tracking-fusion datasets."""

from __future__ import annotations

import argparse
import array
import csv
import json
import math
import statistics
import sys
from pathlib import Path
from typing import Any, Callable, Iterator


AXES = {"x": 0, "y": 1, "z": 2}
COMPACT_BONE_SPACE_OFFSETS = {"component": 0, "local": 10, "world": 20}
COMPACT_BONE_QUAT_OFFSETS = {"component": 3, "local": 13, "world": 23}
MIN_CORR_SAMPLES = 5
MIN_MOTION = 1.0
DIAGNOSTIC_BANDS = (
    "strong",
    "usable",
    "weak",
    "insufficient_motion",
    "source_missing",
    "unstable_lag",
    "avatar_mismatch",
)

AVATAR_LOCKED_VISIBLE_POLICY_CVARS: dict[str, int] = {
    "mp.BodyFusion.Enable": 1,
    "mp.BodyFusion.WritePose": 1,
    "mp.MediaPipeDriveSpine": 1,
    "mp.MediaPipeDrivePelvisTranslation": 1,
    "mp.MediaPipeDriveLegs": 1,
    "mp.MediaPipeUseLegIK": 0,
    "mp.MediaPipeUseFkRootGrounding": 1,
    "mp.MediaPipeDriveFootRotation": 1,
}

SOURCE_ALIGNMENT_ROW_FRAGMENTS: dict[str, tuple[str, ...]] = {
    "quest_hmd": ("quest_hmd_to_mediapipe",),
    "quest_hands": ("quest_left_hand_to_mediapipe", "quest_right_hand_to_mediapipe"),
    "quest_arm_chains": ("quest_left_arm_", "quest_right_arm_"),
    "mediapipe_body_pose": (
        "mediapipe_left_hip_to_hmd",
        "mediapipe_right_hip_to_hmd",
        "mediapipe_left_knee_to_hmd",
        "mediapipe_right_knee_to_hmd",
        "mediapipe_left_heel_to_hmd",
        "mediapipe_right_heel_to_hmd",
    ),
}

DEFAULT_THRESHOLDS: dict[str, float] = {
    "min_samples": 12.0,
    "min_source_availability": 0.35,
    "min_motion_cm": 2.0,
    "strong_corr": 0.82,
    "usable_corr": 0.55,
    "max_stable_lag_seconds": 0.75,
    "min_lag_confidence": 0.25,
    "min_axis_promotion_rows": 3.0,
    "min_axis_promotion_median_abs_corr": 0.82,
    "min_axis_promotion_median_lag_confidence": 0.25,
    "min_axis_sign_agreement": 0.80,
    "avatar_mismatch_residual_norm_p95": 0.65,
    "max_lag_seconds": 0.75,
}

FUTURE_CAPTURE_REQUIREMENTS_BY_REGION: dict[str, list[str]] = {
    "torso": [
        "slow torso lean forward/back and side-to-side while keeping the headset visible",
        "deliberate chest twist left/right with arms relaxed enough not to dominate the signal",
        "shoulder-height and hip-height MediaPipe visibility during the torso movement phase",
    ],
    "hips": [
        "left/right hip sway and small pelvis circles with the avatar standing in place",
        "weight shifts from one foot to the other while the camera sees both hips",
        "crouch/stand transitions with the pelvis visible to MediaPipe",
    ],
    "legs": [
        "alternating knee lifts and controlled shallow squats",
        "step forward/back and side lunge phases with both knees visible",
        "hold each leg movement long enough for several 30 Hz samples per direction",
    ],
    "feet": [
        "heel raises, toe taps, and ankle flex movements on each side",
        "step-in-place phases where heel and foot-index landmarks remain visible",
        "avoid foot occlusion by keeping the full feet inside the MediaPipe camera frame",
    ],
}

AVATAR_LOCKED_SYNC_PHASE_PRESET = "avatar_locked_sync_calibration"
AVATAR_LOCKED_SYNC_BLOCK_SECONDS = 30.0
AVATAR_LOCKED_SYNC_PHASES: list[dict[str, Any]] = [
    {
        "phase_name": "avatar_locked_head_30s",
        "region": "head",
        "readiness_targets": [
            "coordinate_axis_corrections.quest_hmd",
            "head_camera_anchor_offset_cm",
            "head_source_to_avatar_correlation",
        ],
    },
    {
        "phase_name": "avatar_locked_hands_wrists_30s",
        "region": "hands",
        "readiness_targets": [
            "coordinate_axis_corrections.quest_hands",
            "wrist_arm_chain_offsets_cm",
            "hand_source_to_avatar_correlation",
        ],
    },
    {
        "phase_name": "avatar_locked_arms_30s",
        "region": "arms",
        "readiness_targets": [
            "coordinate_axis_corrections.quest_arm_chains",
            "wrist_arm_chain_offsets_cm",
            "arm_chain_source_to_avatar_correlation",
        ],
    },
    {"phase_name": "avatar_locked_torso_30s", "region": "torso", "readiness_targets": ["torso_motion_sufficiency"]},
    {"phase_name": "avatar_locked_hips_30s", "region": "hips", "readiness_targets": ["hips_motion_sufficiency"]},
    {"phase_name": "avatar_locked_legs_30s", "region": "legs", "readiness_targets": ["legs_motion_sufficiency"]},
    {"phase_name": "avatar_locked_feet_30s", "region": "feet", "readiness_targets": ["feet_motion_sufficiency"]},
]

CALIBRATION_FIELD_PHASE_REQUIREMENTS: dict[str, list[str]] = {
    "source_alignment.coordinate_axis_corrections": [
        "avatar_locked_head_30s",
        "avatar_locked_hands_wrists_30s",
        "avatar_locked_arms_30s",
    ],
    "source_alignment.head_camera_anchor_offset_cm": ["avatar_locked_head_30s"],
    "source_alignment.wrist_arm_chain_offsets_cm": [
        "avatar_locked_hands_wrists_30s",
        "avatar_locked_arms_30s",
    ],
    "region.torso": ["avatar_locked_torso_30s"],
    "region.hips": ["avatar_locked_hips_30s"],
    "region.legs": ["avatar_locked_legs_30s"],
    "region.feet": ["avatar_locked_feet_30s"],
}

MEDIAPIPE_REGION_LANDMARKS: dict[str, list[str]] = {
    "torso": ["left_shoulder", "right_shoulder", "left_hip", "right_hip"],
    "hips": ["left_hip", "right_hip"],
    "legs": ["left_knee", "right_knee", "left_ankle", "right_ankle"],
    "feet": ["left_heel", "right_heel", "left_foot_index", "right_foot_index"],
}

FORBIDDEN_PROFILE_FIELD_FRAGMENTS = (
    "avatar_scale",
    "body_scale",
    "scale_avatar",
    "metahuman_deformation",
    "metahuman_body_deformation",
    "body_deformation",
    "user_height",
    "height_cm",
    "user_body",
    "user_arm",
    "user_leg",
    "arm_length",
    "leg_length",
    "pelvis_width",
    "chest_width",
    "head_size",
    "head_proportion",
    "torso_length",
    "body_shape",
    "avatar_height",
    "avatar_arm_length",
    "avatar_leg_length",
)


def load_dataset(path: Path) -> dict[str, Any]:
    text = path.read_text(encoding="utf-8")
    stripped = text.lstrip()
    if not stripped:
        raise ValueError(f"{path} is empty")
    if stripped[0] == "{":
        data = json.loads(text)
        if not isinstance(data, dict):
            raise ValueError(f"{path} did not contain a JSON object")
        if data.get("sample_files"):
            data["samples"] = load_sample_files(path, data["sample_files"])
        attach_binary_bone_samples(path, data)
        return data

    samples = [json.loads(line) for line in text.splitlines() if line.strip()]
    return {
        "schema": "tracking_fusion_dataset_jsonl",
        "schema_version": 1,
        "movement_phases": [],
        "samples": samples,
    }


def load_sample_files(manifest_path: Path, sample_files: Any) -> list[dict[str, Any]]:
    if not isinstance(sample_files, list):
        raise ValueError("manifest sample_files must be a list")

    samples: list[dict[str, Any]] = []
    for entry in sample_files:
        raw_path = entry.get("relative_path") or entry.get("path") if isinstance(entry, dict) else entry
        if not isinstance(raw_path, str) or not raw_path:
            continue
        chunk_path = Path(raw_path)
        if not chunk_path.is_absolute():
            chunk_path = manifest_path.parent / chunk_path
        samples.extend(read_chunk_samples(chunk_path))
    return samples


def read_chunk_samples(chunk_path: Path) -> list[dict[str, Any]]:
    return list(iter_json_objects(chunk_path.read_text(encoding="utf-8"), chunk_path))


def attach_binary_bone_samples(manifest_path: Path, dataset: dict[str, Any]) -> None:
    sample_files = dataset.get("bone_sample_files")
    samples = dataset.get("samples")
    bone_order = get_path(dataset, "bone_selection.recorded", []) or []
    if not isinstance(sample_files, list) or not isinstance(samples, list) or not isinstance(bone_order, list):
        return

    bone_lookup = {str(name): index for index, name in enumerate(bone_order)}
    if not bone_lookup:
        return

    fmt = get_path(dataset, "capture_settings.bone_sample_format", {}) or {}
    floats_per_bone = int(fmt.get("floats_per_bone") or 33)
    floats_per_sample = len(bone_lookup) * floats_per_bone
    if floats_per_sample <= 0:
        return

    next_index = 0
    for entry in sample_files:
        if isinstance(entry, dict):
            raw_path = entry.get("relative_path") or entry.get("path")
            first_sample_index = int(entry.get("first_sample_index") or next_index)
            declared_count = entry.get("sample_count")
        else:
            raw_path = entry
            first_sample_index = next_index
            declared_count = None
        if not isinstance(raw_path, str) or not raw_path:
            continue
        chunk_path = Path(raw_path)
        if not chunk_path.is_absolute():
            chunk_path = manifest_path.parent / chunk_path
        values = array.array("f")
        values.frombytes(chunk_path.read_bytes())
        if sys.byteorder != "little":
            values.byteswap()
        available_count = len(values) // floats_per_sample
        sample_count = int(declared_count) if declared_count is not None else available_count
        sample_count = min(sample_count, available_count, max(0, len(samples) - first_sample_index))
        for local_index in range(sample_count):
            sample = samples[first_sample_index + local_index]
            sample["_bone_binary_array"] = values
            sample["_bone_binary_offset"] = local_index * floats_per_sample
            sample["_bone_lookup"] = bone_lookup
            sample["_bone_floats_per_bone"] = floats_per_bone
        next_index = max(next_index, first_sample_index + sample_count)


def iter_json_objects(text: str, source: Path) -> Iterator[dict[str, Any]]:
    decoder = json.JSONDecoder()
    length = len(text)
    index = 0
    while index < length:
        while index < length and text[index].isspace():
            index += 1
        if index >= length:
            break
        try:
            value, next_index = decoder.raw_decode(text, index)
        except json.JSONDecodeError as exc:
            line = text.count("\n", 0, exc.pos) + 1
            last_newline = text.rfind("\n", 0, exc.pos)
            column = exc.pos + 1 if last_newline < 0 else exc.pos - last_newline
            raise ValueError(f"{source}:{line}:{column} invalid JSON object: {exc.msg}") from exc
        if not isinstance(value, dict):
            line = text.count("\n", 0, index) + 1
            raise ValueError(f"{source}:{line} did not contain a JSON object")
        yield value
        index = next_index


def get_path(obj: Any, path: str, default: Any = None) -> Any:
    cur = obj
    for part in path.split("."):
        if isinstance(cur, dict):
            cur = cur.get(part, default)
        elif isinstance(cur, list):
            try:
                cur = cur[int(part)]
            except (ValueError, IndexError):
                return default
        else:
            return default
    return cur


def as_float(value: Any) -> float | None:
    if isinstance(value, (int, float)) and math.isfinite(float(value)):
        return float(value)
    return None


def as_int(value: Any, default: int = 0) -> int:
    if isinstance(value, bool):
        return int(value)
    if isinstance(value, int):
        return value
    if isinstance(value, float) and math.isfinite(value):
        return int(value)
    return default


def vec_axis(value: Any, axis: str) -> float | None:
    if not isinstance(value, list):
        return None
    idx = AXES[axis]
    if idx >= len(value):
        return None
    return as_float(value[idx])


def vec_value(value: Any) -> list[float] | None:
    if not isinstance(value, list) or len(value) < 3:
        return None
    out = [as_float(value[0]), as_float(value[1]), as_float(value[2])]
    return None if any(v is None for v in out) else [float(v) for v in out]


def dist_vec(a: list[float] | None, b: list[float] | None) -> float | None:
    if a is None or b is None:
        return None
    return math.sqrt(sum((x - y) * (x - y) for x, y in zip(a, b)))


def phase_marker_start(phase: dict[str, Any]) -> float | None:
    return as_float(phase.get("start_time_seconds", phase.get("start_time")))


def phase_marker_end(phase: dict[str, Any]) -> float | None:
    return as_float(phase.get("end_time_seconds", phase.get("end_time")))


def phase_marker_duration(phase: dict[str, Any]) -> float | None:
    explicit = as_float(phase.get("duration_seconds"))
    if explicit is not None:
        return explicit
    start = phase_marker_start(phase)
    end = phase_marker_end(phase)
    return end - start if start is not None and end is not None else None


def phase_marker_regions(dataset: dict[str, Any]) -> dict[str, str]:
    regions: dict[str, str] = {}
    for phase in dataset.get("movement_phases") or []:
        if not isinstance(phase, dict):
            continue
        name = phase.get("phase_name")
        region = phase.get("region")
        if isinstance(name, str) and isinstance(region, str) and region:
            regions[name] = region
    return regions


def sample_phase_name(sample: dict[str, Any]) -> str:
    phase = sample.get("phase") or {}
    if isinstance(phase, dict):
        value = phase.get("phase_name")
        return str(value) if value is not None else "unlabeled"
    return "unlabeled"


def sample_phase_region(sample: dict[str, Any], phase_regions: dict[str, str]) -> str:
    phase = sample.get("phase") or {}
    if isinstance(phase, dict):
        region = phase.get("region")
        if isinstance(region, str) and region:
            return region
    return phase_regions.get(sample_phase_name(sample), "")


def sample_vec_path(path: str) -> Callable[[dict[str, Any]], list[float] | None]:
    return lambda sample: valid_vec_path(sample, path)


def path_axis(path: str, axis: str) -> Callable[[dict[str, Any]], float | None]:
    return lambda sample: vec_axis(valid_vec_path(sample, path), axis)


def valid_vec_path(sample: dict[str, Any], path: str) -> list[float] | None:
    if path.endswith(".pos") or path.endswith(".loc"):
        valid_path = path.rsplit(".", 1)[0] + ".valid"
        valid = get_path(sample, valid_path)
        if valid is False:
            return None
    return vec_value(get_path(sample, path))


def midpoint_axis(paths: tuple[str, ...], axis: str) -> Callable[[dict[str, Any]], float | None]:
    def getter(sample: dict[str, Any]) -> float | None:
        values = [valid_vec_path(sample, path) for path in paths]
        if any(value is None for value in values):
            return None
        axis_index = AXES[axis]
        return statistics.fmean(float(value[axis_index]) for value in values if value is not None)

    return getter


def path_present(path: str, truthy: bool = True) -> Callable[[dict[str, Any]], bool]:
    def getter(sample: dict[str, Any]) -> bool:
        value = get_path(sample, path)
        if truthy:
            return bool(value)
        return value is not None

    return getter


def bone_vec(bone: str, space: str = "world") -> Callable[[dict[str, Any]], list[float] | None]:
    def getter(sample: dict[str, Any]) -> list[float] | None:
        values = [bone_axis(bone, axis, space)(sample) for axis in ("x", "y", "z")]
        return None if any(value is None for value in values) else [float(value) for value in values]

    return getter


def bone_axis(bone: str, axis: str, space: str = "world") -> Callable[[dict[str, Any]], float | None]:
    def getter(sample: dict[str, Any]) -> float | None:
        old_value = vec_axis(get_path(sample, f"retarget_output.bones.{bone}.{space}.loc"), axis)
        if old_value is not None:
            return old_value

        lookup = sample.get("_bone_lookup")
        values = sample.get("_bone_binary_array")
        sample_offset = sample.get("_bone_binary_offset")
        floats_per_bone = sample.get("_bone_floats_per_bone", 33)
        if not isinstance(lookup, dict) or values is None or not isinstance(sample_offset, int):
            return None
        bone_index = lookup.get(bone)
        space_offset = COMPACT_BONE_SPACE_OFFSETS.get(space)
        axis_index = AXES[axis]
        if bone_index is None or space_offset is None:
            return None
        value_index = sample_offset + int(bone_index) * int(floats_per_bone) + space_offset + axis_index
        if value_index < 0 or value_index >= len(values):
            return None
        return as_float(values[value_index])

    return getter


def quat_to_euler_degrees(quat: list[float]) -> dict[str, float]:
    x, y, z, w = quat
    sinr_cosp = 2.0 * (w * x + y * z)
    cosr_cosp = 1.0 - 2.0 * (x * x + y * y)
    roll = math.degrees(math.atan2(sinr_cosp, cosr_cosp))

    sinp = 2.0 * (w * y - z * x)
    if abs(sinp) >= 1.0:
        pitch = math.degrees(math.copysign(math.pi / 2.0, sinp))
    else:
        pitch = math.degrees(math.asin(sinp))

    siny_cosp = 2.0 * (w * z + x * y)
    cosy_cosp = 1.0 - 2.0 * (y * y + z * z)
    yaw = math.degrees(math.atan2(siny_cosp, cosy_cosp))
    return {"x": roll, "y": pitch, "z": yaw}


def bone_rot_axis(bone: str, axis: str, space: str = "world") -> Callable[[dict[str, Any]], float | None]:
    def getter(sample: dict[str, Any]) -> float | None:
        old_rot = vec_axis(get_path(sample, f"retarget_output.bones.{bone}.{space}.rot"), axis)
        if old_rot is not None:
            return old_rot

        old_quat = get_path(sample, f"retarget_output.bones.{bone}.{space}.quat")
        if isinstance(old_quat, list) and len(old_quat) >= 4:
            quat = [as_float(old_quat[index]) for index in range(4)]
            if not any(value is None for value in quat):
                return quat_to_euler_degrees([float(value) for value in quat])[axis]

        lookup = sample.get("_bone_lookup")
        values = sample.get("_bone_binary_array")
        sample_offset = sample.get("_bone_binary_offset")
        floats_per_bone = sample.get("_bone_floats_per_bone", 33)
        if not isinstance(lookup, dict) or values is None or not isinstance(sample_offset, int):
            return None
        bone_index = lookup.get(bone)
        quat_offset = COMPACT_BONE_QUAT_OFFSETS.get(space)
        if bone_index is None or quat_offset is None:
            return None
        value_index = sample_offset + int(bone_index) * int(floats_per_bone) + quat_offset
        if value_index < 0 or value_index + 3 >= len(values):
            return None
        quat = [as_float(values[value_index + i]) for i in range(4)]
        if any(value is None for value in quat):
            return None
        return quat_to_euler_degrees([float(value) for value in quat])[axis]

    return getter


def residual_value(name: str) -> Callable[[dict[str, Any]], float | None]:
    return lambda sample: as_float(get_path(sample, f"residuals.{name}"))


def compact(values: list[float | None]) -> list[float]:
    return [v for v in values if v is not None and math.isfinite(v)]


def percentile(values: list[float], q: float) -> float | None:
    if not values:
        return None
    ordered = sorted(values)
    if len(ordered) == 1:
        return ordered[0]
    pos = (len(ordered) - 1) * q
    lo = math.floor(pos)
    hi = math.ceil(pos)
    if lo == hi:
        return ordered[lo]
    alpha = pos - lo
    return ordered[lo] * (1.0 - alpha) + ordered[hi] * alpha


def amplitude(values: list[float | None]) -> float:
    clean = compact(values)
    if len(clean) < 2:
        return 0.0
    p05 = percentile(clean, 0.05)
    p95 = percentile(clean, 0.95)
    if p05 is None or p95 is None:
        return 0.0
    return abs(p95 - p05)


def noise_floor(values: list[float | None]) -> float:
    clean = compact(values)
    if len(clean) < 3:
        return 0.0
    diffs = [abs(b - a) for a, b in zip(clean, clean[1:])]
    med = percentile(diffs, 0.5)
    return float(med or 0.0)


def pearson(a: list[float], b: list[float]) -> float | None:
    if len(a) != len(b) or len(a) < MIN_CORR_SAMPLES:
        return None
    ma = statistics.fmean(a)
    mb = statistics.fmean(b)
    da = [x - ma for x in a]
    db = [y - mb for y in b]
    va = sum(x * x for x in da)
    vb = sum(y * y for y in db)
    if va <= 1.0e-12 or vb <= 1.0e-12:
        return None
    return sum(x * y for x, y in zip(da, db)) / math.sqrt(va * vb)


def shifted_pairs(
    source: list[float],
    target: list[float],
    lag_steps: int,
) -> tuple[list[float], list[float]]:
    if lag_steps > 0:
        return source[:-lag_steps], target[lag_steps:]
    if lag_steps < 0:
        return source[-lag_steps:], target[:lag_steps]
    return source, target


def best_lag_correlation(
    source: list[float | None],
    target: list[float | None],
    times: list[float],
    max_lag_seconds: float = 0.75,
) -> dict[str, Any]:
    pairs = [(t, x, y) for t, x, y in zip(times, source, target) if x is not None and y is not None]
    if len(pairs) < MIN_CORR_SAMPLES:
        return {"samples": len(pairs), "corr_zero": None, "best_corr": None, "best_lag_seconds": None, "lag_confidence": 0.0}

    clean_times = [p[0] for p in pairs]
    xs = [float(p[1]) for p in pairs]
    ys = [float(p[2]) for p in pairs]
    dt_values = [b - a for a, b in zip(clean_times, clean_times[1:]) if b > a]
    dt = statistics.median(dt_values) if dt_values else 1.0 / 60.0
    max_steps = max(0, int(round(max_lag_seconds / max(dt, 1.0e-6))))
    zero = pearson(xs, ys)
    best_corr = zero
    best_lag_steps = 0 if zero is not None else None
    candidates: list[tuple[float, int, float]] = []
    for lag_steps in range(-max_steps, max_steps + 1):
        a, b = shifted_pairs(xs, ys, lag_steps)
        corr = pearson(a, b)
        if corr is None:
            continue
        candidates.append((abs(corr), lag_steps, corr))
        if best_corr is None or abs(corr) > abs(best_corr):
            best_corr = corr
            best_lag_steps = lag_steps

    best_lag = None if best_lag_steps is None else best_lag_steps * dt
    candidates.sort(reverse=True)
    if candidates:
        top = candidates[0][0]
        runner_up = candidates[1][0] if len(candidates) > 1 else 0.0
        lag_confidence = max(0.0, min(1.0, top * ((top - runner_up) / 0.08 if top > runner_up else 0.0)))
    else:
        lag_confidence = 0.0

    residuals: list[float] = []
    residual_offset = None
    if best_lag_steps is not None:
        a, b = shifted_pairs(xs, ys, best_lag_steps)
        offsets = [x - y for x, y in zip(a, b)]
        residual_offset = statistics.median(offsets) if offsets else None
        residuals = [abs(offset - residual_offset) for offset in offsets] if residual_offset is not None else []

    return {
        "samples": len(pairs),
        "corr_zero": zero,
        "best_corr": best_corr,
        "best_lag_seconds": best_lag,
        "lag_confidence": lag_confidence,
        "source_amplitude": amplitude(xs),
        "target_amplitude": amplitude(ys),
        "source_noise_floor": noise_floor(xs),
        "target_noise_floor": noise_floor(ys),
        "residual_offset": residual_offset,
        "residual_mean": statistics.fmean(residuals) if residuals else None,
        "residual_p95": percentile(residuals, 0.95) if residuals else None,
        "residual_max": max(residuals) if residuals else None,
    }


def group_samples_by_phase(samples: list[dict[str, Any]]) -> dict[str, list[dict[str, Any]]]:
    phases: dict[str, list[dict[str, Any]]] = {}
    for sample in samples:
        phase = sample_phase_name(sample)
        state = get_path(sample, "phase.state", "movement")
        if phase == "neutral_settle" or state == "neutral_settle":
            continue
        phases.setdefault(phase, []).append(sample)
    return phases


def series(samples: list[dict[str, Any]], getter: Callable[[dict[str, Any]], float | None]) -> list[float | None]:
    return [getter(sample) for sample in samples]


def sample_times(samples: list[dict[str, Any]]) -> list[float]:
    return [as_float(sample.get("t")) or as_float(sample.get("wall_t")) or float(i) for i, sample in enumerate(samples)]


def compute_effective_sample_rate(dataset: dict[str, Any], samples: list[dict[str, Any]]) -> float:
    manifest_rate = as_float(get_path(dataset, "capture_settings.effective_sample_rate_hz"))
    if manifest_rate is not None:
        return manifest_rate
    times = sample_times(samples)
    if len(times) < 2:
        return 0.0
    span = times[-1] - times[0]
    return (len(times) - 1) / span if span > 1.0e-6 else 0.0


def capture_summary(dataset: dict[str, Any], samples: list[dict[str, Any]]) -> dict[str, Any]:
    recorded_bones = get_path(dataset, "bone_selection.recorded", []) or []
    helper_bones = get_path(dataset, "bone_selection.helpers", []) or []
    other_bones = get_path(dataset, "bone_selection.other", []) or []
    manifest_sample_count = as_int(dataset.get("sample_count"), len(samples))
    return {
        "manifest_sample_count": manifest_sample_count,
        "loaded_sample_count": len(samples),
        "expected_sample_count": as_int(get_path(dataset, "capture_settings.expected_sample_count"), 0),
        "target_sample_rate_hz": as_float(get_path(dataset, "capture_settings.sample_rate_hz")) or 0.0,
        "effective_sample_rate_hz": compute_effective_sample_rate(dataset, samples),
        "missed_scheduled_sample_count": as_int(dataset.get("missed_scheduled_sample_count"), 0),
        "candidate_frame_count": as_int(dataset.get("candidate_frame_count"), 0),
        "skipped_frame_count": as_int(dataset.get("skipped_frame_count"), 0),
        "actual_elapsed_seconds": as_float(dataset.get("actual_elapsed_seconds")) or 0.0,
        "sample_time_span_seconds": as_float(dataset.get("sample_time_span_seconds")) or 0.0,
        "recorded_bone_count": len(recorded_bones) if isinstance(recorded_bones, list) else 0,
        "helper_bone_count": len(helper_bones) if isinstance(helper_bones, list) else 0,
        "other_bone_count": len(other_bones) if isinstance(other_bones, list) else 0,
        "sample_storage": get_path(dataset, "capture_settings.sample_storage"),
        "hot_path_storage": get_path(dataset, "capture_settings.hot_path_storage"),
        "bone_mode": get_path(dataset, "capture_settings.bone_mode"),
        "recorder_timing": get_path(dataset, "capture_settings.recorder_timing", {}) or {},
    }


def summarize_residual(samples: list[dict[str, Any]], name: str) -> dict[str, Any]:
    values = compact(series(samples, residual_value(name)))
    if not values:
        return {"count": 0}
    return {
        "count": len(values),
        "mean_cm": statistics.fmean(values),
        "p95_cm": percentile(values, 0.95),
        "max_cm": max(values),
    }


def helper_parent(helper: str) -> str | None:
    side = "_l" if helper.endswith("_l") else "_r" if helper.endswith("_r") else ""
    if helper.startswith("clavicle_"):
        return f"clavicle{side}"
    if helper.startswith("upperarm_"):
        return f"upperarm{side}"
    if helper.startswith("lowerarm_"):
        return f"lowerarm{side}"
    if helper.startswith("wrist_"):
        return f"hand{side}"
    return None


def bone_amp(samples: list[dict[str, Any]], bone: str, axis: str = "z") -> float:
    return amplitude(series(samples, bone_axis(bone, axis)))


def availability(samples: list[dict[str, Any]], getter: Callable[[dict[str, Any]], bool] | None) -> dict[str, Any]:
    if not samples or getter is None:
        return {"available_count": len(samples), "missing_count": 0, "availability": 1.0 if samples else 0.0, "missing_spans": []}
    flags = [bool(getter(sample)) for sample in samples]
    times = sample_times(samples)
    spans = []
    start = None
    for index, flag in enumerate(flags):
        if not flag and start is None:
            start = index
        if flag and start is not None:
            spans.append({"start_t": times[start], "end_t": times[index - 1], "samples": index - start})
            start = None
    if start is not None:
        spans.append({"start_t": times[start], "end_t": times[-1], "samples": len(flags) - start})
    available_count = sum(1 for flag in flags if flag)
    return {
        "available_count": available_count,
        "missing_count": len(flags) - available_count,
        "availability": available_count / len(flags) if flags else 0.0,
        "missing_spans": spans[:32],
    }


def median_segment_length(samples: list[dict[str, Any]], a_bone: str, b_bone: str) -> float | None:
    a_getter = bone_vec(a_bone)
    b_getter = bone_vec(b_bone)
    values = [dist_vec(a_getter(sample), b_getter(sample)) for sample in samples]
    clean = compact(values)
    return percentile(clean, 0.5) if clean else None


def avatar_segment_lengths(dataset: dict[str, Any], samples: list[dict[str, Any]]) -> dict[str, float]:
    del dataset
    lengths: dict[str, float] = {}
    pairs = {
        "head": ("head", "spine_03"),
        "torso": ("spine_03", "pelvis"),
        "hips": ("pelvis", "spine_03"),
        "left_upper_arm": ("clavicle_l", "upperarm_l"),
        "right_upper_arm": ("clavicle_r", "upperarm_r"),
        "left_lower_arm": ("upperarm_l", "hand_l"),
        "right_lower_arm": ("upperarm_r", "hand_r"),
        "left_leg": ("thigh_l", "foot_l"),
        "right_leg": ("thigh_r", "foot_r"),
        "left_foot": ("foot_l", "ball_l"),
        "right_foot": ("foot_r", "ball_r"),
    }
    for name, (a, b) in pairs.items():
        length = median_segment_length(samples, a, b)
        if length is not None and length > 1.0e-6:
            lengths[name] = length
    return lengths


class SignalPair:
    def __init__(
        self,
        region: str,
        name: str,
        source: str,
        source_getter: Callable[[dict[str, Any]], float | None],
        target: str,
        target_getter: Callable[[dict[str, Any]], float | None],
        source_present_getter: Callable[[dict[str, Any]], bool] | None,
        unit: str = "cm",
        segment_key: str = "",
        plot: bool = True,
        category: str = "source_to_avatar",
    ) -> None:
        self.region = region
        self.name = name
        self.source = source
        self.source_getter = source_getter
        self.target = target
        self.target_getter = target_getter
        self.source_present_getter = source_present_getter
        self.unit = unit
        self.segment_key = segment_key
        self.plot = plot
        self.category = category


def signal_pairs(dataset: dict[str, Any]) -> list[SignalPair]:
    pairs: list[SignalPair] = []

    def add_region_axes(
        region: str,
        base_name: str,
        source_name: str,
        source_path: str,
        target_name: str,
        target_bone: str,
        present_path: str | None,
        segment_key: str,
        axes: tuple[str, ...] = ("x", "y", "z"),
    ) -> None:
        for axis in axes:
            pairs.append(
                SignalPair(
                    region,
                    f"{base_name}_{axis}",
                    f"{source_name}.{axis}",
                    path_axis(source_path, axis),
                    f"{target_name}.{axis}",
                    bone_axis(target_bone, axis),
                    path_present(present_path) if present_path else None,
                    segment_key=segment_key,
                )
            )

    def add_source_axes(
        region: str,
        base_name: str,
        source_name: str,
        source_path: str,
        target_name: str,
        target_path: str,
        present_path: str | None,
        segment_key: str,
        axes: tuple[str, ...] = ("x", "y", "z"),
    ) -> None:
        for axis in axes:
            pairs.append(
                SignalPair(
                    region,
                    f"{base_name}_{axis}",
                    f"{source_name}.{axis}",
                    path_axis(source_path, axis),
                    f"{target_name}.{axis}",
                    path_axis(target_path, axis),
                    path_present(present_path) if present_path else None,
                    segment_key=segment_key,
                    category="source_to_source",
                )
            )

    add_region_axes("head", "hmd_to_avatar_head", "quest_hmd_head", "fusion.source.hmd.loc", "avatar_head", "head", "fusion.source.hmd.has_pose", "head")
    add_source_axes("head", "quest_hmd_to_mediapipe_nose", "quest_hmd_head", "fusion.source.hmd.loc", "mediapipe_nose", "fusion.source.body_pose.landmarks.nose.pos", "fusion.source.hmd.has_pose", "head")
    add_region_axes("head", "fused_to_avatar_head", "fused_head", "fusion.pose.head.loc", "avatar_head", "head", "fusion.pose.head.valid", "head")
    for axis in ("x", "y", "z"):
        pairs.append(SignalPair("head", f"hmd_rot_to_avatar_head_rot_{axis}", f"quest_hmd_rot.{axis}", path_axis("fusion.source.hmd.rot", axis), f"avatar_head_rot.{axis}", bone_rot_axis("head", axis), path_present("fusion.source.hmd.has_pose"), unit="deg", segment_key="head"))

    for side, side_name in (("left", "left"), ("right", "right")):
        suffix = "l" if side == "left" else "r"
        upper_key = f"{side}_upper_arm"
        lower_key = f"{side}_lower_arm"
        add_source_axes("hands", f"quest_{side}_hand_to_mediapipe_wrist", f"quest_{side}_hand", f"fusion.source.{side}_hand.wrist_world", f"mediapipe_{side}_wrist", f"fusion.source.body_pose.landmarks.{side}_wrist.pos", f"fusion.source.{side}_hand.has_hand", lower_key)
        add_region_axes("hands", f"quest_{side}_hand_to_avatar_hand", f"quest_{side}_hand", f"fusion.source.{side}_hand.wrist_world", f"avatar_hand_{suffix}", f"hand_{suffix}", f"fusion.source.{side}_hand.has_hand", lower_key)
        add_region_axes("hands", f"fused_{side}_wrist_to_avatar_hand", f"fused_{side}_wrist", f"fusion.pose.{side}_wrist.loc", f"avatar_hand_{suffix}", f"hand_{suffix}", f"fusion.pose.{side}_wrist.valid", lower_key)
        add_region_axes("hands", f"mediapipe_{side}_wrist_to_avatar_hand", f"mediapipe_{side}_wrist", f"fusion.source.body_pose.landmarks.{side}_wrist.pos", f"avatar_hand_{suffix}", f"hand_{suffix}", "fusion.source.body_pose.has_body_pose", lower_key)
        add_region_axes("arms", f"quest_{side}_arm_shoulder_to_avatar_upperarm", f"quest_{side}_arm_shoulder", f"fusion.source.{side}_arm_chain.shoulder_world", f"avatar_upperarm_{suffix}", f"upperarm_{suffix}", f"fusion.source.{side}_arm_chain.has_chain", upper_key)
        add_source_axes("arms", f"quest_{side}_arm_shoulder_to_mediapipe_shoulder", f"quest_{side}_arm_shoulder", f"fusion.source.{side}_arm_chain.shoulder_world", f"mediapipe_{side}_shoulder", f"fusion.source.body_pose.landmarks.{side}_shoulder.pos", f"fusion.source.{side}_arm_chain.has_chain", upper_key)
        add_region_axes("arms", f"quest_{side}_arm_elbow_to_avatar_lowerarm", f"quest_{side}_arm_elbow", f"fusion.source.{side}_arm_chain.elbow_world", f"avatar_lowerarm_{suffix}", f"lowerarm_{suffix}", f"fusion.source.{side}_arm_chain.has_chain", lower_key)
        add_source_axes("arms", f"quest_{side}_arm_elbow_to_mediapipe_elbow", f"quest_{side}_arm_elbow", f"fusion.source.{side}_arm_chain.elbow_world", f"mediapipe_{side}_elbow", f"fusion.source.body_pose.landmarks.{side}_elbow.pos", f"fusion.source.{side}_arm_chain.has_chain", upper_key)
        add_region_axes("arms", f"quest_{side}_arm_wrist_to_avatar_hand", f"quest_{side}_arm_wrist", f"fusion.source.{side}_arm_chain.wrist_world", f"avatar_hand_{suffix}", f"hand_{suffix}", f"fusion.source.{side}_arm_chain.has_chain", lower_key)
        add_source_axes("arms", f"quest_{side}_arm_wrist_to_mediapipe_wrist", f"quest_{side}_arm_wrist", f"fusion.source.{side}_arm_chain.wrist_world", f"mediapipe_{side}_wrist", f"fusion.source.body_pose.landmarks.{side}_wrist.pos", f"fusion.source.{side}_arm_chain.has_chain", lower_key)
        add_region_axes("arms", f"mediapipe_{side}_shoulder_to_avatar_upperarm", f"mediapipe_{side}_shoulder", f"fusion.source.body_pose.landmarks.{side}_shoulder.pos", f"avatar_upperarm_{suffix}", f"upperarm_{suffix}", "fusion.source.body_pose.has_body_pose", upper_key)
        add_region_axes("arms", f"mediapipe_{side}_elbow_to_avatar_lowerarm", f"mediapipe_{side}_elbow", f"fusion.source.body_pose.landmarks.{side}_elbow.pos", f"avatar_lowerarm_{suffix}", f"lowerarm_{suffix}", "fusion.source.body_pose.has_body_pose", lower_key)
        add_region_axes("arms", f"mediapipe_{side}_wrist_to_avatar_hand", f"mediapipe_{side}_wrist", f"fusion.source.body_pose.landmarks.{side}_wrist.pos", f"avatar_hand_{suffix}", f"hand_{suffix}", "fusion.source.body_pose.has_body_pose", lower_key)
        add_source_axes("hips", f"mediapipe_{side}_hip_to_hmd", f"mediapipe_{side}_hip", f"fusion.source.body_pose.landmarks.{side}_hip.pos", "quest_hmd_head", "fusion.source.hmd.loc", "fusion.source.body_pose.has_body_pose", "hips", axes=("z",))
        add_region_axes("legs", f"mediapipe_{side}_knee_to_avatar_calf", f"mediapipe_{side}_knee", f"fusion.source.body_pose.landmarks.{side}_knee.pos", f"avatar_calf_{suffix}", f"calf_{suffix}", "fusion.source.body_pose.has_body_pose", f"{side}_leg")
        add_source_axes("legs", f"mediapipe_{side}_knee_to_hmd", f"mediapipe_{side}_knee", f"fusion.source.body_pose.landmarks.{side}_knee.pos", "quest_hmd_head", "fusion.source.hmd.loc", "fusion.source.body_pose.has_body_pose", f"{side}_leg", axes=("z",))
        add_region_axes("legs", f"mediapipe_{side}_ankle_to_avatar_foot", f"mediapipe_{side}_ankle", f"fusion.source.body_pose.landmarks.{side}_ankle.pos", f"avatar_foot_{suffix}", f"foot_{suffix}", "fusion.source.body_pose.has_body_pose", f"{side}_leg")
        add_region_axes("feet", f"mediapipe_{side}_heel_to_avatar_foot", f"mediapipe_{side}_heel", f"fusion.source.body_pose.landmarks.{side}_heel.pos", f"avatar_foot_{suffix}", f"foot_{suffix}", "fusion.source.body_pose.has_body_pose", f"{side}_foot")
        add_source_axes("feet", f"mediapipe_{side}_heel_to_hmd", f"mediapipe_{side}_heel", f"fusion.source.body_pose.landmarks.{side}_heel.pos", "quest_hmd_head", "fusion.source.hmd.loc", "fusion.source.body_pose.has_body_pose", f"{side}_foot", axes=("z",))
        add_region_axes("feet", f"mediapipe_{side}_foot_index_to_avatar_ball", f"mediapipe_{side}_foot_index", f"fusion.source.body_pose.landmarks.{side}_foot_index.pos", f"avatar_ball_{suffix}", f"ball_{suffix}", "fusion.source.body_pose.has_body_pose", f"{side}_foot")
        add_region_axes("legs", f"fused_{side}_knee_to_avatar_calf", f"fused_{side}_knee", f"fusion.pose.{side}_knee.loc", f"avatar_calf_{suffix}", f"calf_{suffix}", f"fusion.pose.{side}_knee.valid", f"{side}_leg")
        add_region_axes("legs", f"fused_{side}_ankle_to_avatar_foot", f"fused_{side}_ankle", f"fusion.pose.{side}_ankle.loc", f"avatar_foot_{suffix}", f"foot_{suffix}", f"fusion.pose.{side}_ankle.valid", f"{side}_leg")
        add_region_axes("feet", f"fused_{side}_foot_to_avatar_ball", f"fused_{side}_foot", f"fusion.pose.{side}_foot.loc", f"avatar_ball_{suffix}", f"ball_{suffix}", f"fusion.pose.{side}_foot.valid", f"{side}_foot")

    for axis in ("x", "y", "z"):
        pairs.append(
            SignalPair(
                "hips",
                f"mediapipe_hip_mid_to_avatar_pelvis_{axis}",
                f"mediapipe_hip_mid.{axis}",
                midpoint_axis(
                    (
                        "fusion.source.body_pose.landmarks.left_hip.pos",
                        "fusion.source.body_pose.landmarks.right_hip.pos",
                    ),
                    axis,
                ),
                f"avatar_pelvis.{axis}",
                bone_axis("pelvis", axis),
                path_present("fusion.source.body_pose.has_body_pose"),
                segment_key="hips",
            )
        )

    for source_path, source_name, target_bone in (
        ("fusion.pose.chest.loc", "fused_chest", "spine_03"),
        ("fusion.pose.spine.loc", "fused_spine", "spine_02"),
        ("fusion.pose.pelvis.loc", "fused_pelvis", "pelvis"),
    ):
        add_region_axes("torso", f"{source_name}_to_avatar_{target_bone}", source_name, source_path, f"avatar_{target_bone}", target_bone, source_path.rsplit(".", 1)[0] + ".valid", "torso")
    for axis in ("x", "y", "z"):
        pairs.append(
            SignalPair(
                "torso",
                f"mediapipe_shoulder_mid_to_avatar_spine_03_{axis}",
                f"mediapipe_shoulder_mid.{axis}",
                midpoint_axis(
                    (
                        "fusion.source.body_pose.landmarks.left_shoulder.pos",
                        "fusion.source.body_pose.landmarks.right_shoulder.pos",
                    ),
                    axis,
                ),
                f"avatar_spine_03.{axis}",
                bone_axis("spine_03", axis),
                path_present("fusion.source.body_pose.has_body_pose"),
                segment_key="torso",
            )
        )
        pairs.append(
            SignalPair(
                "torso",
                f"mediapipe_hip_mid_to_avatar_pelvis_{axis}",
                f"mediapipe_hip_mid.{axis}",
                midpoint_axis(
                    (
                        "fusion.source.body_pose.landmarks.left_hip.pos",
                        "fusion.source.body_pose.landmarks.right_hip.pos",
                    ),
                    axis,
                ),
                f"avatar_pelvis.{axis}",
                bone_axis("pelvis", axis),
                path_present("fusion.source.body_pose.has_body_pose"),
                segment_key="torso",
            )
        )
    for mp_name in ("left_shoulder", "right_shoulder", "left_hip", "right_hip"):
        add_source_axes("torso", f"mediapipe_{mp_name}_to_hmd", f"mediapipe_{mp_name}", f"fusion.source.body_pose.landmarks.{mp_name}.pos", "quest_hmd_head", "fusion.source.hmd.loc", "fusion.source.body_pose.has_body_pose", "torso", axes=("z",))

    helper_bones = get_path(dataset, "bone_selection.helpers", []) or []
    other_bones = get_path(dataset, "bone_selection.other", []) or []
    for helper in [str(value) for value in helper_bones if isinstance(value, str)]:
        parent = helper_parent(helper)
        if parent:
            for axis in ("x", "y", "z"):
                pairs.append(SignalPair("avatar_helpers", f"{helper}_relative_to_{parent}_{axis}", f"avatar_parent_{parent}.{axis}", bone_axis(parent, axis), f"avatar_helper_{helper}.{axis}", bone_axis(helper, axis), None, segment_key="arms"))
    for bone in [str(value) for value in other_bones if isinstance(value, str)][:80]:
        parent = helper_parent(bone)
        if parent:
            for axis in ("x", "y", "z"):
                pairs.append(SignalPair("avatar_helpers", f"other_{bone}_relative_to_{parent}_{axis}", f"avatar_parent_{parent}.{axis}", bone_axis(parent, axis), f"avatar_other_{bone}.{axis}", bone_axis(bone, axis), None, segment_key="arms", plot=False))

    return pairs


def classify_row(row: dict[str, Any], thresholds: dict[str, float]) -> str:
    if row["source_availability"] < thresholds["min_source_availability"] or row["samples"] < int(thresholds["min_samples"]):
        return "source_missing"
    if row["source_amplitude"] < thresholds["min_motion_cm"] or row["target_amplitude"] < thresholds["min_motion_cm"]:
        return "insufficient_motion"
    residual_norm = row.get("residual_p95_normalized")
    if isinstance(residual_norm, (int, float)) and residual_norm > thresholds["avatar_mismatch_residual_norm_p95"]:
        return "avatar_mismatch"
    lag = row.get("best_lag_seconds")
    if isinstance(lag, (int, float)) and abs(lag) > thresholds["max_stable_lag_seconds"] and row.get("lag_confidence", 0.0) < thresholds["min_lag_confidence"]:
        return "unstable_lag"
    corr = row.get("best_corr")
    if not isinstance(corr, (int, float)):
        return "weak"
    acorr = abs(corr)
    if acorr >= thresholds["strong_corr"]:
        return "strong"
    if acorr >= thresholds["usable_corr"]:
        return "usable"
    return "weak"


def phase_correlation(
    pair: SignalPair,
    phase_name: str,
    samples: list[dict[str, Any]],
    thresholds: dict[str, float],
    segment_lengths: dict[str, float],
) -> dict[str, Any]:
    source_values = series(samples, pair.source_getter)
    target_values = series(samples, pair.target_getter)
    result = best_lag_correlation(source_values, target_values, sample_times(samples), thresholds["max_lag_seconds"])
    source_avail = availability(samples, pair.source_present_getter)
    segment_length = segment_lengths.get(pair.segment_key) or segment_lengths.get(pair.region) or segment_lengths.get("torso")
    residual_p95 = result.get("residual_p95")
    residual_norm = residual_p95 / segment_length if isinstance(residual_p95, (int, float)) and segment_length else None
    row = {
        "region": pair.region,
        "category": pair.category,
        "phase": phase_name,
        "pair": pair.name,
        "source": pair.source,
        "target": pair.target,
        "unit": pair.unit,
        "samples": result["samples"],
        "phase_sample_count": len(samples),
        "source_available_count": source_avail["available_count"],
        "source_missing_count": source_avail["missing_count"],
        "source_availability": source_avail["availability"],
        "missing_spans": source_avail["missing_spans"],
        "source_amplitude": result.get("source_amplitude", amplitude(source_values)),
        "target_amplitude": result.get("target_amplitude", amplitude(target_values)),
        "source_noise_floor": result.get("source_noise_floor", noise_floor(source_values)),
        "target_noise_floor": result.get("target_noise_floor", noise_floor(target_values)),
        "corr_zero": result.get("corr_zero"),
        "best_corr": result.get("best_corr"),
        "best_lag_seconds": result.get("best_lag_seconds"),
        "lag_confidence": result.get("lag_confidence", 0.0),
        "residual_mean": result.get("residual_mean"),
        "residual_offset": result.get("residual_offset"),
        "residual_p95": residual_p95,
        "residual_max": result.get("residual_max"),
        "avatar_segment_length_cm": segment_length,
        "residual_p95_normalized": residual_norm,
        "phase_coverage": len(samples),
    }
    row["diagnostic_band"] = classify_row(row, thresholds)
    return row


def collect_correlations(dataset: dict[str, Any], phases: dict[str, list[dict[str, Any]]], thresholds: dict[str, float], segment_lengths: dict[str, float]) -> list[dict[str, Any]]:
    rows: list[dict[str, Any]] = []
    for phase_name, phase_samples in phases.items():
        for pair in signal_pairs(dataset):
            rows.append(phase_correlation(pair, phase_name, phase_samples, thresholds, segment_lengths))
    return rows


def detect_suspicious_cases(dataset: dict[str, Any], phases: dict[str, list[dict[str, Any]]], correlations: list[dict[str, Any]], thresholds: dict[str, float]) -> list[dict[str, Any]]:
    suspicious: list[dict[str, Any]] = []
    helper_bones = get_path(dataset, "bone_selection.helpers", []) or []

    for phase_name, samples in phases.items():
        if "left_shoulder_shrug" in phase_name:
            evidence = amplitude(series(samples, path_axis("fusion.source.body_pose.landmarks.left_shoulder.pos", "z")))
            response = max(bone_amp(samples, "clavicle_l"), bone_amp(samples, "clavicle_out_l"), bone_amp(samples, "clavicle_scap_l"))
            opposite = max(bone_amp(samples, "clavicle_r"), bone_amp(samples, "clavicle_out_r"), bone_amp(samples, "clavicle_scap_r"))
            if evidence >= 2.0 and response < 0.75:
                suspicious.append({"phase": phase_name, "case": "shoulder_shrug_evidence_without_left_avatar_response", "evidence_cm": evidence, "response_cm": response})
            if response >= 2.0 and opposite > max(2.0, response * 0.6):
                suspicious.append({"phase": phase_name, "case": "opposite_side_shoulder_contamination_right", "target_cm": response, "opposite_cm": opposite})
        if "right_shoulder_shrug" in phase_name:
            evidence = amplitude(series(samples, path_axis("fusion.source.body_pose.landmarks.right_shoulder.pos", "z")))
            response = max(bone_amp(samples, "clavicle_r"), bone_amp(samples, "clavicle_out_r"), bone_amp(samples, "clavicle_scap_r"))
            opposite = max(bone_amp(samples, "clavicle_l"), bone_amp(samples, "clavicle_out_l"), bone_amp(samples, "clavicle_scap_l"))
            if evidence >= 2.0 and response < 0.75:
                suspicious.append({"phase": phase_name, "case": "shoulder_shrug_evidence_without_right_avatar_response", "evidence_cm": evidence, "response_cm": response})
            if response >= 2.0 and opposite > max(2.0, response * 0.6):
                suspicious.append({"phase": phase_name, "case": "opposite_side_shoulder_contamination_left", "target_cm": response, "opposite_cm": opposite})

        for helper in helper_bones:
            parent = helper_parent(str(helper))
            if not parent:
                continue
            helper_motion = bone_amp(samples, str(helper))
            parent_motion = bone_amp(samples, parent)
            if helper_motion > 2.0 and parent_motion < 0.5:
                suspicious.append({"phase": phase_name, "case": "helper_moving_without_parent_chain_reason", "helper": helper, "helper_cm": helper_motion, "parent": parent, "parent_cm": parent_motion})
            if parent_motion > 2.0 and helper_motion < 0.25:
                suspicious.append({"phase": phase_name, "case": "parent_chain_moving_helper_leaf_stale", "helper": helper, "helper_cm": helper_motion, "parent": parent, "parent_cm": parent_motion})

        for side in ("l", "r"):
            upper = bone_amp(samples, f"upperarm_{side}")
            clav = bone_amp(samples, f"clavicle_{side}")
            if upper > 35.0:
                suspicious.append({"phase": phase_name, "case": f"excessive_upperarm_translation_{side}", "motion_cm": upper})
            if clav > 20.0:
                suspicious.append({"phase": phase_name, "case": f"excessive_clavicle_translation_{side}", "motion_cm": clav})

        for name in (
            "mediapipe_left_wrist_to_avatar_hand_l_cm",
            "mediapipe_right_wrist_to_avatar_hand_r_cm",
            "quest_left_hand_to_avatar_hand_l_cm",
            "quest_right_hand_to_avatar_hand_r_cm",
        ):
            residual = summarize_residual(samples, name)
            p95 = residual.get("p95_cm")
            if isinstance(p95, (int, float)) and p95 > 35.0:
                suspicious.append({"phase": phase_name, "case": "avatar_output_residual_high", "residual": name, "p95_cm": p95})

    for row in correlations:
        band = row.get("diagnostic_band")
        if band in {"weak", "unstable_lag", "avatar_mismatch"} and row.get("source_amplitude", 0.0) >= thresholds["min_motion_cm"]:
            suspicious.append({"phase": row["phase"], "case": f"diagnostic_band_{band}", "region": row["region"], "pair": row["pair"], "best_corr": row.get("best_corr"), "best_lag_seconds": row.get("best_lag_seconds")})
    return suspicious


def source_availability_summary(samples: list[dict[str, Any]]) -> dict[str, Any]:
    sources = {
        "hmd": path_present("fusion.source.hmd.has_pose"),
        "left_hand": path_present("fusion.source.left_hand.has_hand"),
        "right_hand": path_present("fusion.source.right_hand.has_hand"),
        "left_arm_chain": path_present("fusion.source.left_arm_chain.has_chain"),
        "right_arm_chain": path_present("fusion.source.right_arm_chain.has_chain"),
        "body_pose": path_present("fusion.source.body_pose.has_body_pose"),
    }
    return {name: availability(samples, getter) for name, getter in sources.items()}


def cvar_snapshot(dataset: dict[str, Any]) -> dict[str, Any]:
    cvars = get_path(dataset, "capture_settings.cvars", {}) or {}
    return cvars if isinstance(cvars, dict) else {}


def cvar_int(dataset: dict[str, Any], name: str, default: int) -> int:
    policy = get_path(dataset, "capture_settings.avatar_output_policy", {}) or {}
    if isinstance(policy, dict) and name in policy:
        value = policy.get(name)
    else:
        value = cvar_snapshot(dataset).get(name, default)
    if isinstance(value, bool):
        return int(value)
    if isinstance(value, int):
        return value
    if isinstance(value, float) and math.isfinite(value):
        return int(value)
    if isinstance(value, str):
        try:
            return int(float(value))
        except ValueError:
            return default
    return default


def avatar_output_policy_by_region(dataset: dict[str, Any]) -> dict[str, Any]:
    policy_obj = get_path(dataset, "capture_settings.avatar_output_policy", {}) or {}
    policy = policy_obj if isinstance(policy_obj, dict) else {}
    body_fusion_enable = cvar_int(dataset, "mp.BodyFusion.Enable", 0)
    body_fusion_write_pose = cvar_int(dataset, "mp.BodyFusion.WritePose", 1)
    body_fusion_mediapipe_authority = cvar_int(dataset, "mp.BodyFusion.MediaPipeAuthority", 0)
    drive_spine = cvar_int(dataset, "mp.MediaPipeDriveSpine", 1)
    drive_pelvis_translation = cvar_int(dataset, "mp.MediaPipeDrivePelvisTranslation", 0)
    drive_legs = cvar_int(dataset, "mp.MediaPipeDriveLegs", 0)
    use_leg_ik = cvar_int(dataset, "mp.MediaPipeUseLegIK", 0)
    use_leg_ik_foot_plant = cvar_int(dataset, "mp.MediaPipeUseLegIKFootPlant", 1)
    use_fk_root_grounding = cvar_int(dataset, "mp.MediaPipeUseFkRootGrounding", 0)
    drive_foot_rotation = cvar_int(dataset, "mp.MediaPipeDriveFootRotation", 0)

    def region_data(region: str, constrained: bool, reasons: list[str]) -> dict[str, Any]:
        explicit_key = f"{region}_output_policy_constrained"
        if explicit_key in policy:
            constrained = bool(policy[explicit_key])
        return {
            "avatar_output_constrained_by_policy": constrained,
            "reason": "avatar_output_policy_constrained" if constrained else "avatar_output_policy_allows_follow",
            "policy_reasons": reasons if constrained else [],
            "cvars": {
                "mp.BodyFusion.Enable": body_fusion_enable,
                "mp.BodyFusion.WritePose": body_fusion_write_pose,
                "mp.BodyFusion.MediaPipeAuthority": body_fusion_mediapipe_authority,
                "mp.MediaPipeDriveSpine": drive_spine,
                "mp.MediaPipeDrivePelvisTranslation": drive_pelvis_translation,
                "mp.MediaPipeDriveLegs": drive_legs,
                "mp.MediaPipeUseLegIK": use_leg_ik,
                "mp.MediaPipeUseLegIKFootPlant": use_leg_ik_foot_plant,
                "mp.MediaPipeUseFkRootGrounding": use_fk_root_grounding,
                "mp.MediaPipeDriveFootRotation": drive_foot_rotation,
            },
        }

    torso_reasons = []
    if drive_spine == 0:
        torso_reasons.append("mp.MediaPipeDriveSpine=0")
    if body_fusion_enable != 0 and body_fusion_write_pose == 0:
        torso_reasons.append("mp.BodyFusion.WritePose=0")
    if body_fusion_enable != 0 and body_fusion_mediapipe_authority == 0:
        torso_reasons.append("mp.BodyFusion.MediaPipeAuthority=0")

    hips_reasons = []
    if drive_pelvis_translation == 0:
        hips_reasons.append("mp.MediaPipeDrivePelvisTranslation=0")
    if body_fusion_write_pose == 0:
        hips_reasons.append("mp.BodyFusion.WritePose=0")

    legs_reasons = []
    if drive_legs == 0:
        legs_reasons.append("mp.MediaPipeDriveLegs=0")
    if body_fusion_write_pose == 0:
        legs_reasons.append("mp.BodyFusion.WritePose=0")

    feet_reasons = list(legs_reasons)
    has_foot_output_policy = (
        (use_leg_ik != 0 and use_leg_ik_foot_plant != 0) or
        use_fk_root_grounding != 0 or
        drive_foot_rotation != 0
    )
    if not has_foot_output_policy:
        feet_reasons.append("no foot grounding or foot rotation output policy")

    return {
        "torso": region_data("torso", bool(torso_reasons), torso_reasons),
        "hips": region_data("hips", bool(hips_reasons), hips_reasons),
        "legs": region_data("legs", bool(legs_reasons), legs_reasons),
        "feet": region_data("feet", bool(feet_reasons), feet_reasons),
    }


def avatar_locked_capture_policy_preflight(dataset: dict[str, Any]) -> dict[str, Any]:
    preset = get_path(dataset, "capture_settings.phase_preset")
    if preset != AVATAR_LOCKED_SYNC_PHASE_PRESET:
        return {
            "state": "not_applicable",
            "reason": "not_avatar_locked_sync_calibration_capture",
            "required_cvars": AVATAR_LOCKED_VISIBLE_POLICY_CVARS,
            "observed_cvars": {},
            "invalid_reasons": [],
        }

    label = str(dataset.get("label", "")).lower()
    if "replay_avatar_output" in label:
        return {
            "state": "not_applicable",
            "reason": "deterministic_replay_output_capture",
            "required_cvars": AVATAR_LOCKED_VISIBLE_POLICY_CVARS,
            "observed_cvars": {},
            "invalid_reasons": [],
        }

    observed: dict[str, int] = {}
    invalid: list[str] = []
    for name, expected in AVATAR_LOCKED_VISIBLE_POLICY_CVARS.items():
        value = cvar_int(dataset, name, -1)
        observed[name] = value
        if value != expected:
            invalid.append(f"{name}={value}")

    use_leg_ik = observed.get("mp.MediaPipeUseLegIK", cvar_int(dataset, "mp.MediaPipeUseLegIK", -1))
    use_leg_ik_foot_plant = cvar_int(dataset, "mp.MediaPipeUseLegIKFootPlant", -1)
    observed["mp.MediaPipeUseLegIKFootPlant"] = use_leg_ik_foot_plant
    if use_leg_ik != 0 and use_leg_ik_foot_plant != 0:
        invalid.append(f"mp.MediaPipeUseLegIKFootPlant={use_leg_ik_foot_plant}")

    authority = cvar_int(dataset, "mp.BodyFusion.MediaPipeAuthority", -1)
    observed["mp.BodyFusion.MediaPipeAuthority"] = authority
    if authority <= 0:
        invalid.append(f"mp.BodyFusion.MediaPipeAuthority={authority}")

    return {
        "state": "invalid" if invalid else "ready",
        "reason": "invalid_capture_policy" if invalid else "visible_full_body_capture_policy",
        "required_cvars": {
            **AVATAR_LOCKED_VISIBLE_POLICY_CVARS,
            "mp.MediaPipeUseLegIKFootPlant": "0 when mp.MediaPipeUseLegIK!=0",
            "mp.BodyFusion.MediaPipeAuthority": ">=1",
        },
        "observed_cvars": observed,
        "invalid_reasons": invalid,
    }


def raw_mediapipe_region_source_status(samples: list[dict[str, Any]], thresholds: dict[str, float]) -> dict[str, Any]:
    status: dict[str, Any] = {}
    total = len(samples)
    for region, landmarks in MEDIAPIPE_REGION_LANDMARKS.items():
        available_samples = 0
        amplitudes: list[float] = []
        landmark_status: dict[str, Any] = {}
        for landmark in landmarks:
            axis_amplitudes = []
            for axis in ("x", "y", "z"):
                getter = path_axis(f"fusion.source.body_pose.landmarks.{landmark}.pos", axis)
                values = series(samples, getter)
                axis_amplitudes.append(amplitude(values))
            landmark_status[landmark] = {
                "max_axis_amplitude_cm": round(max(axis_amplitudes), 6) if axis_amplitudes else 0.0,
                "axis_amplitudes_cm": {axis: round(value, 6) for axis, value in zip(("x", "y", "z"), axis_amplitudes)},
            }
            amplitudes.extend(axis_amplitudes)
        for sample in samples:
            if not bool(get_path(sample, "fusion.source.body_pose.has_body_pose")):
                continue
            if any(vec_value(get_path(sample, f"fusion.source.body_pose.landmarks.{landmark}.pos")) is not None for landmark in landmarks):
                available_samples += 1
        availability_ratio = (available_samples / total) if total else 0.0
        max_amp = max(amplitudes) if amplitudes else 0.0
        median_amp = statistics.median(amplitudes) if amplitudes else 0.0
        status[region] = {
            "source": "raw_mediapipe_body_pose_landmarks",
            "sample_count": total,
            "available_samples": available_samples,
            "availability": round(float(availability_ratio), 6),
            "source_availability_pass": availability_ratio >= thresholds["min_source_availability"],
            "max_axis_amplitude_cm": round(float(max_amp), 6),
            "median_axis_amplitude_cm": round(float(median_amp), 6),
            "source_motion_pass": max_amp >= thresholds["min_motion_cm"],
            "source_missing_rows": 0 if available_samples > 0 else None,
            "landmarks": landmark_status,
        }
    return status


def capture_gap_summary(samples: list[dict[str, Any]], dataset: dict[str, Any]) -> dict[str, Any]:
    times = sample_times(samples)
    gaps = [b - a for a, b in zip(times, times[1:]) if b >= a]
    expected = as_float(get_path(dataset, "capture_settings.sample_interval_seconds")) or (1.0 / 30.0)
    big_gaps = [{"start_t": times[i], "end_t": times[i + 1], "gap_seconds": gaps[i]} for i in range(len(gaps)) if gaps[i] > expected * 3.0]
    return {
        "expected_interval_seconds": expected,
        "largest_gap_seconds": max(gaps) if gaps else 0.0,
        "p95_gap_seconds": percentile(gaps, 0.95) if gaps else 0.0,
        "gaps_over_1s": sum(1 for gap in gaps if gap > 1.0),
        "large_gaps": big_gaps[:64],
        "missed_scheduled_sample_count": as_int(dataset.get("missed_scheduled_sample_count"), 0),
    }


def region_band_summary(correlations: list[dict[str, Any]]) -> dict[str, Any]:
    summary: dict[str, Any] = {}
    for row in correlations:
        region = row["region"]
        band = row["diagnostic_band"]
        region_summary = summary.setdefault(region, {"rows": 0, "bands": {name: 0 for name in DIAGNOSTIC_BANDS}})
        region_summary["rows"] += 1
        region_summary["bands"][band] = region_summary["bands"].get(band, 0) + 1
    return summary


def calibration_readiness(region_summary: dict[str, Any]) -> dict[str, Any]:
    readiness: dict[str, Any] = {}
    for region, data in sorted(region_summary.items()):
        bands = data.get("bands", {})
        ready_rows = int(bands.get("strong", 0)) + int(bands.get("usable", 0))
        insufficient_rows = int(bands.get("insufficient_motion", 0))
        source_missing_rows = int(bands.get("source_missing", 0))
        mismatch_rows = int(bands.get("avatar_mismatch", 0))
        unstable_rows = int(bands.get("unstable_lag", 0))
        total_rows = int(data.get("rows", 0))
        if total_rows <= 0:
            state = "not_ready"
            reason = "no_rows"
        elif source_missing_rows == total_rows:
            state = "not_ready"
            reason = "source_missing"
        elif insufficient_rows == total_rows:
            state = "not_ready"
            reason = "insufficient_motion"
        elif ready_rows <= 0:
            state = "not_ready"
            reason = "no_strong_or_usable_rows"
        elif mismatch_rows + unstable_rows > ready_rows:
            state = "diagnostic_only"
            reason = "unstable_or_avatar_mismatch_dominates"
        else:
            state = "ready"
            reason = "has_strong_or_usable_motion_rows"
        readiness[region] = {
            "state": state,
            "reason": reason,
            "ready_rows": ready_rows,
            "insufficient_motion_rows": insufficient_rows,
            "source_missing_rows": source_missing_rows,
            "avatar_mismatch_rows": mismatch_rows,
            "unstable_lag_rows": unstable_rows,
            "total_rows": total_rows,
        }
    return readiness


def avatar_locked_sync_phase_protocol(dataset: dict[str, Any]) -> dict[str, Any]:
    phases = [phase for phase in (dataset.get("movement_phases") or []) if isinstance(phase, dict)]
    by_name = {str(phase.get("phase_name")): phase for phase in phases if phase.get("phase_name") is not None}
    expected_rows: list[dict[str, Any]] = []
    for index, expected in enumerate(AVATAR_LOCKED_SYNC_PHASES):
        name = expected["phase_name"]
        phase = by_name.get(name)
        if not phase:
            expected_rows.append(
                {
                    "phase_name": name,
                    "region": expected["region"],
                    "present": False,
                    "duration_seconds": None,
                    "duration_ok": False,
                    "starts_on_expected_boundary": False,
                    "readiness_targets_present": [],
                    "missing_readiness_targets": list(expected["readiness_targets"]),
                }
            )
            continue

        duration = phase_marker_duration(phase)
        start = phase_marker_start(phase)
        readiness_targets = [str(value) for value in (phase.get("readiness_targets") or [])]
        missing_targets = [target for target in expected["readiness_targets"] if target not in readiness_targets]
        expected_start = index * AVATAR_LOCKED_SYNC_BLOCK_SECONDS
        expected_rows.append(
            {
                "phase_name": name,
                "region": phase.get("region", expected["region"]),
                "present": True,
                "duration_seconds": duration,
                "duration_ok": duration is not None and abs(duration - AVATAR_LOCKED_SYNC_BLOCK_SECONDS) <= 0.01,
                "starts_on_expected_boundary": start is not None and abs(start - expected_start) <= 0.01,
                "readiness_targets_present": readiness_targets,
                "missing_readiness_targets": missing_targets,
                "prompt": phase.get("prompt", ""),
            }
        )

    missing_phases = [row["phase_name"] for row in expected_rows if not row["present"]]
    bad_duration = [row["phase_name"] for row in expected_rows if row["present"] and not row["duration_ok"]]
    bad_start = [row["phase_name"] for row in expected_rows if row["present"] and not row["starts_on_expected_boundary"]]
    missing_targets = {
        row["phase_name"]: row["missing_readiness_targets"]
        for row in expected_rows
        if row["missing_readiness_targets"]
    }
    capture_settings = dataset.get("capture_settings") or {}
    preset = capture_settings.get("phase_preset")
    prompt_color = capture_settings.get("prompt_color")
    hud_suppressed = bool(capture_settings.get("calibration_debug_huds_suppressed"))
    protocol_ready = (
        not missing_phases
        and not bad_duration
        and not bad_start
        and not missing_targets
        and preset == AVATAR_LOCKED_SYNC_PHASE_PRESET
        and prompt_color == "green"
        and hud_suppressed
    )
    if protocol_ready:
        reason = "seven_green_30_second_blocks_with_debug_huds_suppressed"
    elif preset != AVATAR_LOCKED_SYNC_PHASE_PRESET:
        reason = "not_avatar_locked_sync_calibration_capture"
    elif prompt_color != "green":
        reason = "green_prompt_metadata_missing"
    elif not hud_suppressed:
        reason = "calibration_debug_hud_suppression_missing"
    elif missing_phases:
        reason = "required_phase_markers_missing"
    elif bad_duration:
        reason = "phase_duration_not_30_seconds"
    elif bad_start:
        reason = "phase_start_boundaries_not_30_seconds"
    else:
        reason = "phase_readiness_targets_missing"
    return {
        "state": "ready" if protocol_ready else "not_ready",
        "reason": reason,
        "expected_phase_preset": AVATAR_LOCKED_SYNC_PHASE_PRESET,
        "observed_phase_preset": preset,
        "expected_block_seconds": AVATAR_LOCKED_SYNC_BLOCK_SECONDS,
        "expected_block_count": len(AVATAR_LOCKED_SYNC_PHASES),
        "observed_phase_count": len(phases),
        "prompt_color": prompt_color,
        "calibration_debug_huds_suppressed": hud_suppressed,
        "missing_phases": missing_phases,
        "bad_duration_phases": bad_duration,
        "bad_start_boundary_phases": bad_start,
        "missing_readiness_targets_by_phase": missing_targets,
        "phases": expected_rows,
    }


def find_forbidden_profile_fields(obj: Any, prefix: str = "") -> list[str]:
    found: list[str] = []
    if isinstance(obj, dict):
        for key, value in obj.items():
            path = f"{prefix}.{key}" if prefix else str(key)
            lowered = path.lower()
            if any(fragment in lowered for fragment in FORBIDDEN_PROFILE_FIELD_FRAGMENTS):
                found.append(path)
            found.extend(find_forbidden_profile_fields(value, path))
    elif isinstance(obj, list):
        for index, value in enumerate(obj):
            found.extend(find_forbidden_profile_fields(value, f"{prefix}.{index}"))
    return found


def runtime_timing_offsets_by_source(summary: dict[str, Any]) -> dict[str, float]:
    thresholds = summary["thresholds"]
    rows = [
        row
        for row in summary["correlations"]
        if row.get("category") == "source_to_source"
        and row.get("diagnostic_band") in {"strong", "usable"}
        and isinstance(row.get("best_lag_seconds"), (int, float))
        and row["best_lag_seconds"] > 0.005
    ]

    def median_lag_for(name_fragments: tuple[str, ...]) -> float | None:
        values = [
            float(row["best_lag_seconds"])
            for row in rows
            if any(fragment in row["pair"] for fragment in name_fragments)
            and float(row["best_lag_seconds"]) <= thresholds["max_stable_lag_seconds"]
        ]
        if not values:
            return None
        return round(float(statistics.median(values)), 6)

    offsets: dict[str, float] = {}
    for source_name, fragments in SOURCE_ALIGNMENT_ROW_FRAGMENTS.items():
        value = median_lag_for(fragments)
        if value is not None:
            offsets[source_name] = value
    return offsets


def runtime_alignment_readiness_by_source(summary: dict[str, Any]) -> dict[str, Any]:
    offsets = runtime_timing_offsets_by_source(summary)
    source_rows = [row for row in summary["correlations"] if row.get("category") == "source_to_source"]
    readiness: dict[str, Any] = {}
    for source_name, fragments in SOURCE_ALIGNMENT_ROW_FRAGMENTS.items():
        rows = [row for row in source_rows if any(fragment in row["pair"] for fragment in fragments)]
        ready_rows = sum(1 for row in rows if row["diagnostic_band"] in {"strong", "usable"})
        insufficient_rows = sum(1 for row in rows if row["diagnostic_band"] == "insufficient_motion")
        state = "ready" if source_name in offsets else "not_ready"
        reason = "runtime_timing_offset_promoted" if state == "ready" else (
            "insufficient_motion" if rows and insufficient_rows == len(rows) else "no_supported_positive_lag"
        )
        readiness[source_name] = {
            "state": state,
            "reason": reason,
            "runtime_offset_seconds": offsets.get(source_name),
            "ready_rows": ready_rows,
            "insufficient_motion_rows": insufficient_rows,
            "total_rows": len(rows),
        }
    return readiness


def median_number(values: list[float]) -> float | None:
    return round(float(statistics.median(values)), 6) if values else None


def pair_axis(pair_name: str) -> str | None:
    suffix = pair_name.rsplit("_", 1)[-1] if "_" in pair_name else ""
    return suffix if suffix in AXES else None


def source_alignment_rows(summary: dict[str, Any], source_name: str) -> list[dict[str, Any]]:
    fragments = SOURCE_ALIGNMENT_ROW_FRAGMENTS.get(source_name, ())
    return [
        row
        for row in summary["correlations"]
        if row.get("category") == "source_to_source"
        and any(fragment in row["pair"] for fragment in fragments)
    ]


def coordinate_axis_promotion_stats(summary: dict[str, Any]) -> dict[str, Any]:
    thresholds = summary["thresholds"]
    stats_by_source: dict[str, Any] = {}
    min_rows = int(thresholds["min_axis_promotion_rows"])
    min_abs_corr = float(thresholds["min_axis_promotion_median_abs_corr"])
    min_lag_conf = float(thresholds["min_axis_promotion_median_lag_confidence"])
    min_agreement = float(thresholds["min_axis_sign_agreement"])
    max_lag = float(thresholds["max_stable_lag_seconds"])

    for source_name in SOURCE_ALIGNMENT_ROW_FRAGMENTS:
        rows = source_alignment_rows(summary, source_name)
        axis_stats: dict[str, Any] = {}
        for axis in ("x", "y", "z"):
            axis_rows = [row for row in rows if pair_axis(str(row.get("pair", ""))) == axis]
            stable_rows = [
                row
                for row in axis_rows
                if row.get("diagnostic_band") in {"strong", "usable"}
                and isinstance(row.get("best_corr"), (int, float))
                and abs(float(row["best_corr"])) >= min_abs_corr
                and isinstance(row.get("best_lag_seconds"), (int, float))
                and abs(float(row["best_lag_seconds"])) <= max_lag
                and float(row.get("lag_confidence") or 0.0) >= min_lag_conf
            ]
            signs = [-1 if float(row["best_corr"]) < 0.0 else 1 for row in stable_rows]
            negative_count = sum(1 for sign in signs if sign < 0)
            positive_count = sum(1 for sign in signs if sign > 0)
            dominant_sign = -1 if negative_count > positive_count else 1
            agreement = (max(negative_count, positive_count) / len(signs)) if signs else 0.0
            abs_corr_values = [abs(float(row["best_corr"])) for row in stable_rows]
            lag_conf_values = [float(row.get("lag_confidence") or 0.0) for row in stable_rows]
            promoted = (
                len(stable_rows) >= min_rows
                and agreement >= min_agreement
                and (median_number(abs_corr_values) or 0.0) >= min_abs_corr
                and (median_number(lag_conf_values) or 0.0) >= min_lag_conf
            )
            axis_stats[axis] = {
                "total_row_count": len(axis_rows),
                "stable_high_confidence_row_count": len(stable_rows),
                "positive_sign_count": positive_count,
                "negative_sign_count": negative_count,
                "dominant_sign": dominant_sign if stable_rows else None,
                "sign_agreement": round(float(agreement), 6),
                "median_abs_corr": median_number(abs_corr_values),
                "median_lag_confidence": median_number(lag_conf_values),
                "promoted": promoted,
            }
        stats_by_source[source_name] = {
            "total_source_to_source_rows": len(rows),
            "axes": axis_stats,
        }
    return stats_by_source


def runtime_coordinate_axis_corrections(summary: dict[str, Any]) -> dict[str, Any]:
    stats = coordinate_axis_promotion_stats(summary)
    corrections: dict[str, Any] = {}
    for source_name, source_stats in stats.items():
        axis_signs: list[float] = []
        has_non_identity_axis = False
        for axis in ("x", "y", "z"):
            axis_stats = source_stats["axes"][axis]
            sign = -1.0 if axis_stats["promoted"] and axis_stats["dominant_sign"] == -1 else 1.0
            axis_signs.append(sign)
            has_non_identity_axis = has_non_identity_axis or sign < 0.0
        if has_non_identity_axis:
            corrections[source_name] = {
                "space": "target_component",
                "location_axis_sign": axis_signs,
                "location_offset_cm": [0.0, 0.0, 0.0],
            }
    return corrections


def runtime_coordinate_axis_readiness_by_source(summary: dict[str, Any]) -> dict[str, Any]:
    corrections = runtime_coordinate_axis_corrections(summary)
    stats = coordinate_axis_promotion_stats(summary)
    readiness: dict[str, Any] = {}
    for source_name, source_stats in stats.items():
        promoted_negative_axes = [
            axis
            for axis, axis_stats in source_stats["axes"].items()
            if axis_stats["promoted"] and axis_stats["dominant_sign"] == -1
        ]
        promoted_identity_axes = [
            axis
            for axis, axis_stats in source_stats["axes"].items()
            if axis_stats["promoted"] and axis_stats["dominant_sign"] == 1
        ]
        if source_name in corrections:
            state = "ready"
            reason = "runtime_axis_sign_correction_promoted"
        elif len(promoted_identity_axes) == 3:
            state = "ready_identity"
            reason = "stable_source_to_source_rows_support_identity_axis_signs"
        elif source_stats["total_source_to_source_rows"] <= 0:
            state = "not_ready"
            reason = "no_source_to_source_rows"
        else:
            state = "not_ready"
            reason = "no_stable_high_confidence_axis_sign"
        readiness[source_name] = {
            "state": state,
            "reason": reason,
            "runtime_correction": corrections.get(source_name),
            "promoted_negative_axes": promoted_negative_axes,
            "promoted_identity_axes": promoted_identity_axes,
            "evidence": source_stats,
        }
    return readiness


def runtime_coordinate_axis_effect_estimate(summary: dict[str, Any]) -> dict[str, Any]:
    corrections = runtime_coordinate_axis_corrections(summary)
    readiness = runtime_coordinate_axis_readiness_by_source(summary)
    estimate: dict[str, Any] = {}
    for source_name, data in readiness.items():
        correction = corrections.get(source_name)
        estimate[source_name] = {
            "state": "applied_to_source_frame_before_body_fusion" if correction else "not_applied",
            "reason": data["reason"],
            "runtime_correction": correction,
            "promoted_negative_axes": data["promoted_negative_axes"],
        }
    return estimate


def runtime_alignment_effect_estimate(summary: dict[str, Any]) -> dict[str, Any]:
    thresholds = summary["thresholds"]
    offsets = runtime_timing_offsets_by_source(summary)
    source_rows = [row for row in summary["correlations"] if row.get("category") == "source_to_source"]
    estimate: dict[str, Any] = {}
    for source_name, fragments in SOURCE_ALIGNMENT_ROW_FRAGMENTS.items():
        rows = [
            row
            for row in source_rows
            if any(fragment in row["pair"] for fragment in fragments)
            and row.get("diagnostic_band") in {"strong", "usable"}
            and isinstance(row.get("best_lag_seconds"), (int, float))
            and 0.005 < float(row["best_lag_seconds"]) <= thresholds["max_stable_lag_seconds"]
        ]
        before_corr = [abs(float(row["corr_zero"])) for row in rows if isinstance(row.get("corr_zero"), (int, float))]
        after_corr = [abs(float(row["best_corr"])) for row in rows if isinstance(row.get("best_corr"), (int, float))]
        residual_p95 = [float(row["residual_p95"]) for row in rows if isinstance(row.get("residual_p95"), (int, float))]
        estimate[source_name] = {
            "state": "estimated_improved" if source_name in offsets and before_corr and after_corr and median_number(after_corr) is not None and median_number(before_corr) is not None and float(median_number(after_corr) or 0.0) >= float(median_number(before_corr) or 0.0) else "not_estimated",
            "runtime_offset_seconds": offsets.get(source_name),
            "promoted_row_count": len(rows),
            "median_abs_corr_before_zero_lag": median_number(before_corr),
            "median_abs_corr_after_best_lag": median_number(after_corr),
            "median_abs_corr_gain": round(float((median_number(after_corr) or 0.0) - (median_number(before_corr) or 0.0)), 6) if before_corr and after_corr else None,
            "median_residual_p95_cm_after_best_lag": median_number(residual_p95),
        }
    return estimate


def nonzero_vector(values: Any) -> bool:
    return isinstance(values, list) and any(isinstance(value, (int, float)) and abs(float(value)) > 1.0e-6 for value in values)


def runtime_applied_fields_for_source_alignment(source_alignment: dict[str, Any]) -> list[str]:
    fields: list[str] = []
    if source_alignment.get("timing_offsets_seconds_by_source"):
        fields.append("source_alignment.timing_offsets_seconds_by_source")
    if source_alignment.get("coordinate_axis_corrections"):
        fields.append("source_alignment.coordinate_axis_corrections")
    if nonzero_vector(source_alignment.get("head_camera_anchor_offset_cm")):
        fields.append("source_alignment.head_camera_anchor_offset_cm")
    if source_alignment.get("wrist_arm_chain_offsets_cm"):
        fields.append("source_alignment.wrist_arm_chain_offsets_cm")
    if source_alignment.get("bone_map_corrections"):
        fields.append("source_alignment.bone_map_corrections")
    return fields


def band_counts_for_rows(rows: list[dict[str, Any]]) -> dict[str, int]:
    counts = {name: 0 for name in DIAGNOSTIC_BANDS}
    for row in rows:
        band = row.get("diagnostic_band")
        if isinstance(band, str):
            counts[band] = counts.get(band, 0) + 1
    return counts


def phase_samples_for_regions(dataset: dict[str, Any], regions: set[str], phase_names: set[str] | None = None) -> list[dict[str, Any]]:
    samples = dataset.get("samples") or []
    if not isinstance(samples, list):
        return []
    phase_regions = phase_marker_regions(dataset)
    out: list[dict[str, Any]] = []
    for sample in samples:
        name = sample_phase_name(sample)
        if phase_names is not None and name not in phase_names:
            continue
        region = sample_phase_region(sample, phase_regions)
        if region in regions or (phase_names is not None and name in phase_names):
            out.append(sample)
    return out


def median_vector_delta(
    samples: list[dict[str, Any]],
    source_getter: Callable[[dict[str, Any]], list[float] | None],
    target_getter: Callable[[dict[str, Any]], list[float] | None],
) -> tuple[list[float] | None, dict[str, Any]]:
    deltas: list[list[float]] = []
    for sample in samples:
        source = source_getter(sample)
        target = target_getter(sample)
        if source is None or target is None:
            continue
        deltas.append([target[index] - source[index] for index in range(3)])
    if not deltas:
        return None, {"sample_count": 0, "median_abs_deviation_cm": None, "norm_cm": None}

    median = [float(statistics.median([delta[index] for delta in deltas])) for index in range(3)]
    deviations = [dist_vec(delta, median) or 0.0 for delta in deltas]
    norm = math.sqrt(sum(value * value for value in median))
    return [round(value, 4) for value in median], {
        "sample_count": len(deltas),
        "median_abs_deviation_cm": round(float(statistics.median(deviations)), 4) if deviations else 0.0,
        "p95_abs_deviation_cm": round(float(percentile(deviations, 0.95) or 0.0), 4) if deviations else 0.0,
        "norm_cm": round(float(norm), 4),
    }


def head_anchor_rows(summary: dict[str, Any]) -> list[dict[str, Any]]:
    return [
        row
        for row in summary["correlations"]
        if row.get("category") == "source_to_avatar"
        and row.get("region") == "head"
        and str(row.get("pair", "")).startswith(("hmd_to_avatar_head_", "fused_to_avatar_head_"))
    ]


def wrist_arm_chain_rows(summary: dict[str, Any], side: str) -> list[dict[str, Any]]:
    return [
        row
        for row in summary["correlations"]
        if row.get("category") == "source_to_avatar"
        and row.get("region") in {"hands", "arms"}
        and any(
            str(row.get("pair", "")).startswith(prefix)
            for prefix in (
                f"quest_{side}_hand_to_avatar_hand_",
                f"quest_{side}_arm_wrist_to_avatar_hand_",
            )
        )
    ]


def runtime_head_camera_anchor_offset_cm(dataset: dict[str, Any], summary: dict[str, Any]) -> list[float]:
    rows = head_anchor_rows(summary)
    ready_axes = {
        pair_axis(str(row.get("pair", "")))
        for row in rows
        if row.get("diagnostic_band") in {"strong", "usable"} and pair_axis(str(row.get("pair", "")))
    }
    if summary["calibration_readiness"].get("head", {}).get("state") != "ready" or len(ready_axes) < 3:
        return [0.0, 0.0, 0.0]

    samples = phase_samples_for_regions(
        dataset,
        {"head"},
        set(CALIBRATION_FIELD_PHASE_REQUIREMENTS["source_alignment.head_camera_anchor_offset_cm"]),
    )
    offset, evidence = median_vector_delta(samples, sample_vec_path("fusion.source.hmd.loc"), bone_vec("head"))
    if not offset or float(evidence.get("norm_cm") or 0.0) < 0.5 or float(evidence.get("p95_abs_deviation_cm") or 0.0) > 25.0:
        return [0.0, 0.0, 0.0]
    return offset


def head_signal_motion_summary(samples: list[dict[str, Any]]) -> dict[str, Any]:
    return {
        "avatar_head_translation_cm": {
            axis: round(float(amplitude(series(samples, bone_axis("head", axis)))), 6)
            for axis in ("x", "y", "z")
        },
        "avatar_head_rotation_deg": {
            axis: round(float(amplitude(series(samples, bone_rot_axis("head", axis)))), 6)
            for axis in ("x", "y", "z")
        },
        "hmd_translation_cm": {
            axis: round(float(amplitude(series(samples, path_axis("fusion.source.hmd.loc", axis)))), 6)
            for axis in ("x", "y", "z")
        },
        "hmd_rotation_deg": {
            axis: round(float(amplitude(series(samples, path_axis("fusion.source.hmd.rot", axis)))), 6)
            for axis in ("x", "y", "z")
        },
        "fused_head_translation_cm": {
            axis: round(float(amplitude(series(samples, path_axis("fusion.pose.head.loc", axis)))), 6)
            for axis in ("x", "y", "z")
        },
    }


def runtime_wrist_arm_chain_offsets_cm(dataset: dict[str, Any], summary: dict[str, Any]) -> dict[str, list[float]]:
    offsets: dict[str, list[float]] = {}
    phase_names = set(CALIBRATION_FIELD_PHASE_REQUIREMENTS["source_alignment.wrist_arm_chain_offsets_cm"])
    samples = phase_samples_for_regions(dataset, {"hands", "arms"}, phase_names)
    for side, bone in (("left", "hand_l"), ("right", "hand_r")):
        rows = wrist_arm_chain_rows(summary, side)
        ready_axes = {
            pair_axis(str(row.get("pair", "")))
            for row in rows
            if row.get("diagnostic_band") in {"strong", "usable"} and pair_axis(str(row.get("pair", "")))
        }
        counts = band_counts_for_rows(rows)
        if len(ready_axes) < 3 or counts.get("avatar_mismatch", 0) + counts.get("unstable_lag", 0) > counts.get("strong", 0) + counts.get("usable", 0):
            continue
        offset, evidence = median_vector_delta(samples, sample_vec_path(f"fusion.source.{side}_hand.wrist_world"), bone_vec(bone))
        if offset and float(evidence.get("norm_cm") or 0.0) >= 0.5 and float(evidence.get("p95_abs_deviation_cm") or 0.0) <= 25.0:
            offsets[side] = offset
    return offsets


def runtime_head_anchor_readiness(summary: dict[str, Any], runtime_value: list[float]) -> dict[str, Any]:
    rows = head_anchor_rows(summary)
    counts = band_counts_for_rows(rows)
    readiness = summary["calibration_readiness"].get("head", {})
    ready_axes = sorted({pair_axis(str(row.get("pair", ""))) for row in rows if row.get("diagnostic_band") in {"strong", "usable"} and pair_axis(str(row.get("pair", "")))})
    rotation_rows = [
        row
        for row in summary["correlations"]
        if row.get("category") == "source_to_avatar"
        and row.get("region") == "head"
        and str(row.get("pair", "")).startswith("hmd_rot_to_avatar_head_rot_")
    ]
    ready_rotation_axes = sorted({
        pair_axis(str(row.get("pair", "")))
        for row in rotation_rows
        if row.get("diagnostic_band") in {"strong", "usable"} and pair_axis(str(row.get("pair", "")))
    })
    if nonzero_vector(runtime_value):
        state = "ready"
        reason = "runtime_head_anchor_offset_promoted"
    elif len(ready_rotation_axes) >= 3:
        state = "ready"
        reason = "three_axis_hmd_to_avatar_head_rotation_ready"
    elif readiness.get("state") != "ready":
        state = "not_ready"
        reason = f"avatar_head_rows_{readiness.get('reason', 'not_ready')}"
    elif len(ready_axes) < 3:
        state = "not_ready"
        reason = "stable_three_axis_head_anchor_evidence_missing"
    else:
        state = "not_ready"
        reason = "no_consistent_nonzero_head_anchor_offset_estimate"
    return {
        "state": state,
        "reason": reason,
        "runtime_value": runtime_value,
        "ready_axes": ready_axes,
        "ready_rotation_axes": ready_rotation_axes,
        "band_counts": counts,
        "total_rows": len(rows),
    }


def runtime_wrist_arm_chain_readiness(summary: dict[str, Any], runtime_offsets: dict[str, list[float]] | None = None) -> dict[str, Any]:
    runtime_offsets = runtime_offsets or {}
    side_results: dict[str, Any] = {}
    for side in ("left", "right"):
        rows = wrist_arm_chain_rows(summary, side)
        counts = band_counts_for_rows(rows)
        ready_axes = sorted({pair_axis(str(row.get("pair", ""))) for row in rows if row.get("diagnostic_band") in {"strong", "usable"} and pair_axis(str(row.get("pair", "")))})
        if side in runtime_offsets:
            state = "ready"
            reason = "runtime_wrist_arm_chain_offset_promoted"
        elif len(ready_axes) < 3:
            state = "not_ready"
            reason = "stable_three_axis_wrist_residual_evidence_missing"
        elif counts.get("avatar_mismatch", 0) + counts.get("unstable_lag", 0) > counts.get("strong", 0) + counts.get("usable", 0):
            state = "not_ready"
            reason = "avatar_mismatch_or_unstable_lag_dominates"
        else:
            state = "not_ready"
            reason = "no_consistent_nonzero_wrist_arm_chain_offset_estimate"
        side_results[side] = {
            "state": state,
            "reason": reason,
            "runtime_value": runtime_offsets.get(side),
            "ready_axes": ready_axes,
            "band_counts": counts,
            "total_rows": len(rows),
        }
    reasons = {data["reason"] for data in side_results.values()}
    if all(data["state"] == "ready" for data in side_results.values()):
        state = "ready"
        reason = "runtime_wrist_arm_chain_offsets_promoted"
    elif "avatar_mismatch_or_unstable_lag_dominates" in reasons:
        state = "not_ready"
        reason = "avatar_mismatch_or_unstable_lag_dominates"
    elif "stable_three_axis_wrist_residual_evidence_missing" in reasons:
        state = "not_ready"
        reason = "stable_three_axis_wrist_residual_evidence_missing"
    else:
        state = "not_ready"
        reason = "no_consistent_nonzero_wrist_arm_chain_offset_estimate"
    return {
        "state": state,
        "reason": reason,
        "runtime_value": runtime_offsets,
        "by_side": side_results,
    }


def runtime_bone_map_readiness(summary: dict[str, Any]) -> dict[str, Any]:
    capture = summary["capture"]
    suspicious = [
        item
        for item in summary["suspicious_cases"]
        if str(item.get("case", "")).startswith(("helper_", "parent_chain_"))
    ]
    state = "not_needed" if not suspicious else "diagnostic_only"
    reason = (
        "all_recorded_bones_and_helper_classifications_are_present"
        if not suspicious
        else "helper_or_parent_chain_diagnostics_require_manual_bone_map_review"
    )
    return {
        "state": state,
        "reason": reason,
        "runtime_value": {},
        "recorded_bone_count": capture.get("recorded_bone_count"),
        "helper_bone_count": capture.get("helper_bone_count"),
        "other_bone_count": capture.get("other_bone_count"),
        "suspicious_helper_case_count": len(suspicious),
    }


def runtime_field_readiness(summary: dict[str, Any], source_alignment: dict[str, Any]) -> dict[str, Any]:
    return {
        "source_alignment.timing_offsets_seconds_by_source": {
            "state": "ready" if source_alignment.get("timing_offsets_seconds_by_source") else "not_ready",
            "reason": "positive_stable_source_to_source_lags_promoted" if source_alignment.get("timing_offsets_seconds_by_source") else "no_supported_positive_lag",
            "runtime_value": source_alignment.get("timing_offsets_seconds_by_source", {}),
            "by_source": runtime_alignment_readiness_by_source(summary),
        },
        "source_alignment.coordinate_axis_corrections": {
            "state": "ready" if source_alignment.get("coordinate_axis_corrections") else "not_ready",
            "reason": "stable_high_confidence_source_to_source_axis_sign_promoted" if source_alignment.get("coordinate_axis_corrections") else "no_stable_high_confidence_axis_sign",
            "runtime_value": source_alignment.get("coordinate_axis_corrections", {}),
            "by_source": runtime_coordinate_axis_readiness_by_source(summary),
        },
        "source_alignment.head_camera_anchor_offset_cm": runtime_head_anchor_readiness(
            summary,
            source_alignment.get("head_camera_anchor_offset_cm", [0.0, 0.0, 0.0]),
        ),
        "source_alignment.wrist_arm_chain_offsets_cm": runtime_wrist_arm_chain_readiness(
            summary,
            source_alignment.get("wrist_arm_chain_offsets_cm", {}),
        ),
        "source_alignment.bone_map_corrections": runtime_bone_map_readiness(summary),
    }


def lower_body_region_status(summary: dict[str, Any]) -> dict[str, Any]:
    status: dict[str, Any] = {}
    avatar_readiness = summary["calibration_readiness"]
    source_readiness = summary["source_alignment_region_band_summary"]
    raw_source_status = summary.get("raw_mediapipe_region_source_status", {})
    output_policy = summary.get("avatar_output_policy_by_region", {})
    for region in ("torso", "hips", "legs", "feet"):
        avatar_data = avatar_readiness.get(region, {})
        source_data = source_readiness.get(region, {"rows": 0, "bands": {}})
        raw_source_data = raw_source_status.get(region, {})
        policy_data = output_policy.get(region, {})
        constrained_by_policy = bool(policy_data.get("avatar_output_constrained_by_policy"))
        bands = avatar_data
        source_bands = source_data.get("bands", {})
        raw_source_available = bool(raw_source_data.get("source_availability_pass"))
        raw_source_motion = bool(raw_source_data.get("source_motion_pass"))
        if constrained_by_policy and not raw_source_motion:
            cause = "raw_source_motion_insufficient_and_avatar_output_policy_constrained"
        elif constrained_by_policy:
            cause = "avatar_output_policy_constrained"
        elif avatar_data.get("reason") == "insufficient_motion":
            cause = "insufficient_movement"
        elif avatar_data.get("reason") == "source_missing":
            cause = "source_quality_or_visibility" if not raw_source_available else "avatar_or_fusion_output_missing"
        elif int(bands.get("avatar_mismatch_rows", 0)) > 0:
            cause = "avatar_output_mismatch"
        elif int(bands.get("unstable_lag_rows", 0)) > 0:
            cause = "unstable_lag_or_coordinate_mismatch"
        elif int(source_bands.get("weak", 0)) + int(source_bands.get("unstable_lag", 0)) > int(source_bands.get("strong", 0)) + int(source_bands.get("usable", 0)):
            cause = "source_to_source_alignment_not_stable"
        else:
            cause = "not_enough_ready_rows"
        true_correlation_failure = (
            avatar_data.get("state") != "ready"
            and raw_source_available
            and raw_source_motion
            and not constrained_by_policy
            and cause not in {"insufficient_movement", "source_quality_or_visibility"}
        )
        status[region] = {
            "state": avatar_data.get("state", "not_ready"),
            "reason": avatar_data.get("reason", "no_rows"),
            "cause": cause,
            "raw_mediapipe_source": raw_source_data,
            "avatar_output_policy": policy_data,
            "avatar_output_constrained_by_policy": constrained_by_policy,
            "true_correlation_failure": true_correlation_failure,
            "avatar_region": avatar_data,
            "source_alignment_region": source_data,
            "future_capture_requirements": FUTURE_CAPTURE_REQUIREMENTS_BY_REGION.get(region, []),
        }
    return status


def median_row_value(rows: list[dict[str, Any]], key: str, absolute: bool = False) -> float | None:
    values = [
        abs(float(row[key])) if absolute else float(row[key])
        for row in rows
        if isinstance(row.get(key), (int, float)) and math.isfinite(float(row[key]))
    ]
    return median_number(values)


def field_row_evidence(rows: list[dict[str, Any]], thresholds: dict[str, float]) -> dict[str, Any]:
    counts = band_counts_for_rows(rows)
    ready_rows = int(counts.get("strong", 0)) + int(counts.get("usable", 0))
    median_source_amp = median_row_value(rows, "source_amplitude")
    median_target_amp = median_row_value(rows, "target_amplitude")
    median_availability = median_row_value(rows, "source_availability")
    median_abs_corr = median_row_value(rows, "best_corr", absolute=True)
    median_lag_confidence = median_row_value(rows, "lag_confidence")
    return {
        "total_rows": len(rows),
        "band_counts": counts,
        "ready_rows": ready_rows,
        "motion_amplitude": {
            "median_source_cm": median_source_amp,
            "median_target_cm": median_target_amp,
            "pass": (
                median_source_amp is not None
                and median_target_amp is not None
                and median_source_amp >= thresholds["min_motion_cm"]
                and median_target_amp >= thresholds["min_motion_cm"]
            ),
        },
        "source_availability": {
            "median": median_availability,
            "pass": median_availability is not None and median_availability >= thresholds["min_source_availability"],
        },
        "lag_stability": {
            "median_lag_confidence": median_lag_confidence,
            "unstable_lag_rows": int(counts.get("unstable_lag", 0)),
            "pass": ready_rows > 0 and int(counts.get("unstable_lag", 0)) <= ready_rows,
        },
        "row_confidence": {
            "median_abs_corr": median_abs_corr,
            "pass": ready_rows > 0,
        },
        "residuals": {
            "median_p95_cm": median_row_value(rows, "residual_p95"),
            "median_p95_normalized": median_row_value(rows, "residual_p95_normalized"),
        },
    }


def calibration_capture_sufficiency(
    dataset: dict[str, Any],
    summary: dict[str, Any],
    source_alignment: dict[str, Any],
    field_readiness: dict[str, Any],
    lower_body_status: dict[str, Any],
) -> dict[str, Any]:
    protocol = avatar_locked_sync_phase_protocol(dataset)
    policy_preflight = summary.get("avatar_locked_capture_policy_preflight") or avatar_locked_capture_policy_preflight(dataset)
    phase_names = {str(phase.get("phase_name")) for phase in (dataset.get("movement_phases") or []) if isinstance(phase, dict)}
    thresholds = summary["thresholds"]

    def required_phase_status(field: str) -> dict[str, Any]:
        required = CALIBRATION_FIELD_PHASE_REQUIREMENTS.get(field, [])
        missing = [phase for phase in required if phase not in phase_names]
        return {
            "required_phases": required,
            "missing_required_phases": missing,
            "pass": not missing,
        }

    fields: dict[str, Any] = {}
    row_sets: dict[str, list[dict[str, Any]]] = {
        "source_alignment.coordinate_axis_corrections": [
            row for row in summary["correlations"] if row.get("category") == "source_to_source"
        ],
        "source_alignment.head_camera_anchor_offset_cm": head_anchor_rows(summary),
        "source_alignment.wrist_arm_chain_offsets_cm": wrist_arm_chain_rows(summary, "left") + wrist_arm_chain_rows(summary, "right"),
        "region.torso": [row for row in summary["correlations"] if row.get("category") == "source_to_avatar" and row.get("region") == "torso"],
        "region.hips": [row for row in summary["correlations"] if row.get("category") == "source_to_avatar" and row.get("region") == "hips"],
        "region.legs": [row for row in summary["correlations"] if row.get("category") == "source_to_avatar" and row.get("region") == "legs"],
        "region.feet": [row for row in summary["correlations"] if row.get("category") == "source_to_avatar" and row.get("region") == "feet"],
    }
    readiness_by_field: dict[str, Any] = {
        "source_alignment.coordinate_axis_corrections": field_readiness.get("source_alignment.coordinate_axis_corrections", {}),
        "source_alignment.head_camera_anchor_offset_cm": field_readiness.get("source_alignment.head_camera_anchor_offset_cm", {}),
        "source_alignment.wrist_arm_chain_offsets_cm": field_readiness.get("source_alignment.wrist_arm_chain_offsets_cm", {}),
        "region.torso": lower_body_status.get("torso", {}),
        "region.hips": lower_body_status.get("hips", {}),
        "region.legs": lower_body_status.get("legs", {}),
        "region.feet": lower_body_status.get("feet", {}),
    }
    for field, rows in row_sets.items():
        readiness = readiness_by_field.get(field, {})
        phase_status = required_phase_status(field)
        evidence = field_row_evidence(rows, thresholds)
        state = readiness.get("state", "not_ready")
        if policy_preflight.get("state") == "invalid":
            field_state = "setup_invalid"
        elif state in {"ready", "ready_identity", "not_needed"}:
            field_state = "ready"
        else:
            field_state = "not_ready"
        fields[field] = {
            "state": field_state,
            "data_reason": readiness.get("reason", "no_rows"),
            "protocol_phase_coverage": phase_status,
            "raw_mediapipe_source": readiness.get("raw_mediapipe_source") if field.startswith("region.") else None,
            "avatar_output_policy": readiness.get("avatar_output_policy") if field.startswith("region.") else None,
            "avatar_output_constrained_by_policy": readiness.get("avatar_output_constrained_by_policy") if field.startswith("region.") else None,
            "true_correlation_failure": readiness.get("true_correlation_failure") if field.startswith("region.") else None,
            "runtime_value": (
                source_alignment.get("coordinate_axis_corrections")
                if field == "source_alignment.coordinate_axis_corrections"
                else source_alignment.get("head_camera_anchor_offset_cm")
                if field == "source_alignment.head_camera_anchor_offset_cm"
                else source_alignment.get("wrist_arm_chain_offsets_cm")
                if field == "source_alignment.wrist_arm_chain_offsets_cm"
                else None
            ),
            "evidence": evidence,
            "future_capture_requirements": FUTURE_CAPTURE_REQUIREMENTS_BY_REGION.get(field.split(".")[-1], []),
        }

    ready_fields = [field for field, data in fields.items() if data["state"] == "ready"]
    not_ready_fields = [field for field, data in fields.items() if data["state"] != "ready"]
    if policy_preflight.get("state") == "invalid":
        state = "setup_invalid"
        reason = "invalid_capture_policy"
    elif protocol["state"] == "ready" and not not_ready_fields:
        state = "ready"
        reason = "all_required_fields_ready"
    else:
        state = "not_ready"
        reason = protocol.get("reason", "not_ready") if protocol["state"] != "ready" else "required_fields_not_ready"
    return {
        "schema": "avatar_locked_sync_capture_sufficiency",
        "schema_version": 1,
        "protocol": protocol,
        "policy_preflight": policy_preflight,
        "state": state,
        "reason": reason,
        "setup_invalid_reasons": list(policy_preflight.get("invalid_reasons") or []),
        "ready_fields": ready_fields,
        "not_ready_fields": not_ready_fields,
        "fields": fields,
    }


def build_calibration_profile(dataset: dict[str, Any], summary: dict[str, Any]) -> dict[str, Any]:
    good_rows = [row for row in summary["correlations"] if row["diagnostic_band"] in {"strong", "usable"}]
    timing_offsets: dict[str, float] = {}
    for region in sorted({row["region"] for row in good_rows}):
        lags = [row["best_lag_seconds"] for row in good_rows if row["region"] == region and isinstance(row.get("best_lag_seconds"), (int, float))]
        if lags:
            timing_offsets[region] = round(float(statistics.median(lags)), 6)
    source_alignment = {
        "timing_offsets_seconds_by_source": runtime_timing_offsets_by_source(summary),
        "coordinate_axis_corrections": runtime_coordinate_axis_corrections(summary),
        "head_camera_anchor_offset_cm": runtime_head_camera_anchor_offset_cm(dataset, summary),
        "wrist_arm_chain_offsets_cm": runtime_wrist_arm_chain_offsets_cm(dataset, summary),
        "bone_map_corrections": {},
    }
    runtime_applied_fields = runtime_applied_fields_for_source_alignment(source_alignment)
    field_readiness = runtime_field_readiness(summary, source_alignment)
    lower_body_status = lower_body_region_status(summary)
    capture_sufficiency = calibration_capture_sufficiency(
        dataset,
        summary,
        source_alignment,
        field_readiness,
        lower_body_status,
    )
    timing_effect_estimate = runtime_alignment_effect_estimate(summary)
    coordinate_effect_estimate = runtime_coordinate_axis_effect_estimate(summary)

    profile = {
        "schema": "avatar_calibration_profile",
        "schema_version": 1,
        "mode": "avatar_locked_proteus",
        "generated_from": {
            "label": dataset.get("label"),
            "start_utc": dataset.get("start_utc"),
            "manifest_sample_count": summary["manifest_sample_count"],
            "loaded_sample_count": summary["sample_count"],
            "effective_sample_rate_hz": summary["effective_sample_rate_hz"],
            "recorded_bone_count": summary["recorded_bone_count"],
            "bone_mode": summary["capture"].get("bone_mode"),
            "target_skeletal_mesh": get_path(dataset, "target.skeletal_mesh"),
            "target_component": get_path(dataset, "target.component_path"),
        },
        "avatar_authority": {
            "policy": "avatar_reference_pose_and_active_profile_are_authoritative",
            "forbidden_adjustments": [
                "wearer-derived stature",
                "wearer-derived limb reach",
                "wearer-derived pelvis/chest/head proportions",
                "resizing the chosen avatar body",
                "deforming the chosen MetaHuman body",
            ],
        },
        "source_alignment": source_alignment,
        "runtime_applied_fields": runtime_applied_fields,
        "runtime_field_readiness": field_readiness,
        "calibration_capture_sufficiency": capture_sufficiency,
        "runtime_correction_effect_estimates": {
            "source_alignment.timing_offsets_seconds_by_source": timing_effect_estimate,
            "source_alignment.coordinate_axis_corrections": coordinate_effect_estimate,
        },
        "runtime_alignment_readiness_by_source": runtime_alignment_readiness_by_source(summary),
        "runtime_coordinate_axis_readiness_by_source": runtime_coordinate_axis_readiness_by_source(summary),
        "diagnostic_only": {
            "timing_offsets_seconds_by_region": timing_offsets,
            "runtime_alignment_effect_estimate": timing_effect_estimate,
            "coordinate_alignment": {
                "axis_sign_suggestions": [
                    {
                        "region": row["region"],
                        "pair": row["pair"],
                        "axis_sign": -1,
                        "best_corr": row["best_corr"],
                        "reason": "diagnostic_only_until_source_to_source_stable_and_runtime_consumer_exists",
                    }
                    for row in good_rows
                    if isinstance(row.get("best_corr"), (int, float)) and row["best_corr"] < -summary["thresholds"]["usable_corr"]
                ][:64],
                "runtime_promotion_readiness_by_source": runtime_coordinate_axis_readiness_by_source(summary),
            },
            "smoothing_confidence_dropout": {
                "source_availability": summary["source_availability"],
                "diagnostic_bands_by_region": summary["region_band_summary"],
                "avatar_diagnostic_bands_by_region": summary["avatar_region_band_summary"],
                "source_alignment_diagnostic_bands_by_region": summary["source_alignment_region_band_summary"],
            },
        },
        "calibration_readiness": summary["calibration_readiness"],
        "lower_body_region_status": lower_body_status,
        "raw_mediapipe_region_source_status": summary["raw_mediapipe_region_source_status"],
        "avatar_output_policy_by_region": summary["avatar_output_policy_by_region"],
        "preflight_sufficiency": capture_sufficiency,
        "diagnostic_bands": summary["avatar_region_band_summary"],
        "source_alignment_diagnostic_bands": summary["source_alignment_region_band_summary"],
        "all_diagnostic_bands": summary["region_band_summary"],
        "thresholds": summary["thresholds"],
        "safe_runtime_merge_fields": [
            "source_alignment.timing_offsets_seconds_by_source",
            "source_alignment.coordinate_axis_corrections",
            "source_alignment.head_camera_anchor_offset_cm",
            "source_alignment.wrist_arm_chain_offsets_cm",
            "source_alignment.bone_map_corrections",
        ],
    }
    forbidden = find_forbidden_profile_fields(profile)
    if forbidden:
        raise ValueError(f"generated calibration profile contains forbidden fields: {forbidden}")
    return profile


def analyze_dataset(dataset: dict[str, Any], thresholds: dict[str, float] | None = None) -> dict[str, Any]:
    samples = dataset.get("samples") or []
    if not isinstance(samples, list):
        raise ValueError("dataset samples must be a list")
    effective_thresholds = dict(DEFAULT_THRESHOLDS)
    if thresholds:
        effective_thresholds.update(thresholds)
    phases = group_samples_by_phase(samples)
    segment_lengths = avatar_segment_lengths(dataset, samples)
    correlations = collect_correlations(dataset, phases, effective_thresholds, segment_lengths)
    suspicious = detect_suspicious_cases(dataset, phases, correlations, effective_thresholds)
    capture = capture_summary(dataset, samples)
    policy_preflight = avatar_locked_capture_policy_preflight(dataset)

    phase_summaries = []
    for phase_name, phase_samples in phases.items():
        times = sample_times(phase_samples)
        phase_summaries.append(
            {
                "phase": phase_name,
                "sample_count": len(phase_samples),
                "duration_seconds": (times[-1] - times[0]) if len(times) >= 2 else 0.0,
                "head_motion_cm": bone_amp(phase_samples, "head"),
                "head_signal_motion": head_signal_motion_summary(phase_samples),
                "left_hand_motion_cm": bone_amp(phase_samples, "hand_l"),
                "right_hand_motion_cm": bone_amp(phase_samples, "hand_r"),
                "left_clavicle_motion_cm": bone_amp(phase_samples, "clavicle_l"),
                "right_clavicle_motion_cm": bone_amp(phase_samples, "clavicle_r"),
                "quest_left_hand_residual": summarize_residual(phase_samples, "quest_left_hand_to_avatar_hand_l_cm"),
                "quest_right_hand_residual": summarize_residual(phase_samples, "quest_right_hand_to_avatar_hand_r_cm"),
            }
        )

    summary = {
        "schema": dataset.get("schema"),
        "schema_version": dataset.get("schema_version"),
        "label": dataset.get("label"),
        "thresholds": effective_thresholds,
        "diagnostic_band_names": list(DIAGNOSTIC_BANDS),
        "sample_count": len(samples),
        "manifest_sample_count": capture["manifest_sample_count"],
        "expected_sample_count": capture["expected_sample_count"],
        "target_sample_rate_hz": capture["target_sample_rate_hz"],
        "effective_sample_rate_hz": capture["effective_sample_rate_hz"],
        "missed_scheduled_sample_count": capture["missed_scheduled_sample_count"],
        "recorded_bone_count": capture["recorded_bone_count"],
        "helper_bone_count": capture["helper_bone_count"],
        "other_bone_count": capture["other_bone_count"],
        "capture": capture,
        "capture_gaps": capture_gap_summary(samples, dataset),
        "source_availability": source_availability_summary(samples),
        "raw_mediapipe_region_source_status": raw_mediapipe_region_source_status(samples, effective_thresholds),
        "avatar_output_policy_by_region": avatar_output_policy_by_region(dataset),
        "avatar_locked_capture_policy_preflight": policy_preflight,
        "avatar_segment_lengths_cm": segment_lengths,
        "phase_count": len(dataset.get("movement_phases") or []),
        "phase_summaries": phase_summaries,
        "correlations": correlations,
        "suspicious_cases": suspicious,
    }
    summary["region_band_summary"] = region_band_summary(correlations)
    summary["avatar_region_band_summary"] = region_band_summary(
        [row for row in correlations if row.get("category") == "source_to_avatar"]
    )
    summary["source_alignment_region_band_summary"] = region_band_summary(
        [row for row in correlations if row.get("category") == "source_to_source"]
    )
    summary["calibration_readiness"] = calibration_readiness(summary["avatar_region_band_summary"])
    summary["calibration_profile"] = build_calibration_profile(dataset, summary)
    summary["avatar_locked_sync_phase_protocol"] = summary["calibration_profile"]["calibration_capture_sufficiency"]["protocol"]
    summary["calibration_capture_sufficiency"] = summary["calibration_profile"]["calibration_capture_sufficiency"]
    return summary


def plot_signal(pair: SignalPair, samples: list[dict[str, Any]], rows: list[dict[str, Any]], phases: list[dict[str, Any]], out_path: Path) -> None:
    import matplotlib

    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    times = sample_times(samples)
    source_values = series(samples, pair.source_getter)
    target_values = series(samples, pair.target_getter)
    present = [True] * len(samples) if pair.source_present_getter is None else [pair.source_present_getter(sample) for sample in samples]
    best_row = max((row for row in rows if row["pair"] == pair.name), key=lambda row: abs(row.get("best_corr") or 0.0), default=None)
    lag = best_row.get("best_lag_seconds") if best_row else None
    shifted_times = [t + float(lag) for t in times] if isinstance(lag, (int, float)) else times

    fig, ax = plt.subplots(figsize=(10, 4), dpi=120)
    ax.plot(times, source_values, label=pair.source, linewidth=1.0)
    ax.plot(times, target_values, label=pair.target, linewidth=1.0, alpha=0.75)
    ax.plot(shifted_times, source_values, label="source shifted by best lag", linewidth=0.8, alpha=0.7)
    for phase in phases:
        start = phase_marker_start(phase) if isinstance(phase, dict) else None
        end = phase_marker_end(phase) if isinstance(phase, dict) else None
        if start is not None:
            ax.axvline(start, color="0.8", linewidth=0.4)
        if end is not None:
            ax.axvline(end, color="0.88", linewidth=0.3)
    missing_start = None
    for index, flag in enumerate(present):
        if not flag and missing_start is None:
            missing_start = index
        if flag and missing_start is not None:
            ax.axvspan(times[missing_start], times[index - 1], color="red", alpha=0.12)
            missing_start = None
    if missing_start is not None and times:
        ax.axvspan(times[missing_start], times[-1], color="red", alpha=0.12)
    if best_row:
        band = best_row["diagnostic_band"]
        readiness = "ready" if band in {"strong", "usable"} else "not_ready" if band in {"source_missing", "insufficient_motion"} else "diagnostic_only"
        ax.set_title(
            f"{pair.region}/{pair.name} readiness={readiness} band={band} "
            f"corr={best_row.get('best_corr')} lag={best_row.get('best_lag_seconds')} "
            f"amp={best_row.get('source_amplitude'):.2f}/{best_row.get('target_amplitude'):.2f} "
            f"resid95={best_row.get('residual_p95')}"
        )
    else:
        ax.set_title(f"{pair.region}/{pair.name}")
    ax.set_xlabel("seconds")
    ax.set_ylabel(pair.unit)
    ax.legend(fontsize=7, loc="best")
    ax.grid(True, alpha=0.2)
    out_path.parent.mkdir(parents=True, exist_ok=True)
    fig.tight_layout()
    fig.savefig(out_path)
    plt.close(fig)


def heatmap(rows: list[dict[str, Any]], value_key: str, title: str, out_path: Path) -> None:
    import matplotlib

    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    regions = sorted({row["region"] for row in rows})
    phases = sorted({row["phase"] for row in rows})
    values = [[math.nan for _ in phases] for _ in regions]
    for r_index, region in enumerate(regions):
        for p_index, phase in enumerate(phases):
            vals = [row.get(value_key) for row in rows if row["region"] == region and row["phase"] == phase and isinstance(row.get(value_key), (int, float))]
            values[r_index][p_index] = statistics.fmean(vals) if vals else math.nan
    fig, ax = plt.subplots(figsize=(12, 4), dpi=120)
    image = ax.imshow(values, aspect="auto", interpolation="nearest")
    ax.set_title(title)
    ax.set_yticks(range(len(regions)), labels=regions)
    ax.set_xticks(range(len(phases)), labels=phases, rotation=90, fontsize=6)
    fig.colorbar(image, ax=ax, shrink=0.8)
    out_path.parent.mkdir(parents=True, exist_ok=True)
    fig.tight_layout()
    fig.savefig(out_path)
    plt.close(fig)


def readiness_heatmap(sufficiency: dict[str, Any], out_path: Path) -> None:
    import matplotlib

    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    fields = sorted((sufficiency.get("fields") or {}).keys())
    columns = ["protocol", "motion", "availability", "lag", "confidence", "ready"]
    values: list[list[float]] = []
    for field in fields:
        data = sufficiency["fields"][field]
        evidence = data.get("evidence", {})
        values.append(
            [
                1.0 if data.get("protocol_phase_coverage", {}).get("pass") else 0.0,
                1.0 if evidence.get("motion_amplitude", {}).get("pass") else 0.0,
                1.0 if evidence.get("source_availability", {}).get("pass") else 0.0,
                1.0 if evidence.get("lag_stability", {}).get("pass") else 0.0,
                1.0 if evidence.get("row_confidence", {}).get("pass") else 0.0,
                1.0 if data.get("state") == "ready" else 0.0,
            ]
        )
    fig, ax = plt.subplots(figsize=(8, max(3, len(fields) * 0.36)), dpi=120)
    image = ax.imshow(values, aspect="auto", interpolation="nearest", vmin=0.0, vmax=1.0)
    ax.set_title("Calibration field readiness")
    ax.set_yticks(range(len(fields)), labels=fields, fontsize=6)
    ax.set_xticks(range(len(columns)), labels=columns, rotation=35, ha="right")
    fig.colorbar(image, ax=ax, shrink=0.8)
    out_path.parent.mkdir(parents=True, exist_ok=True)
    fig.tight_layout()
    fig.savefig(out_path)
    plt.close(fig)


def plot_outputs(dataset: dict[str, Any], summary: dict[str, Any], out_dir: Path, stem: str) -> Path:
    plot_root = out_dir / f"{stem}_signal_plots"
    samples = dataset.get("samples") or []
    phases = dataset.get("movement_phases") or []
    rows = summary["correlations"]
    plotted: set[str] = set()
    for pair in signal_pairs(dataset):
        if not pair.plot or pair.name in plotted:
            continue
        plotted.add(pair.name)
        plot_signal(pair, samples, rows, phases, plot_root / pair.region / f"{pair.name}.png")
    summary_root = plot_root / "summary"
    heatmap(rows, "best_corr", "Correlation strength by region/phase", summary_root / "correlation_strength.png")
    heatmap(rows, "best_lag_seconds", "Best lag seconds by region/phase", summary_root / "lag_seconds.png")
    heatmap(rows, "residual_p95", "Residual p95 by region/phase", summary_root / "residuals.png")
    heatmap(rows, "source_availability", "Source availability by region/phase", summary_root / "source_availability.png")
    readiness_heatmap(summary["calibration_capture_sufficiency"], summary_root / "calibration_field_readiness.png")
    gap_rows = [
        {"region": "capture", "phase": row["phase"], "missed": summary["capture_gaps"]["missed_scheduled_sample_count"]}
        for row in summary["phase_summaries"]
    ]
    heatmap(gap_rows, "missed", "Missed scheduled samples", summary_root / "missed_samples_capture_gaps.png")
    return plot_root


def write_outputs(summary: dict[str, Any], out_dir: Path, stem: str, dataset: dict[str, Any] | None = None, no_plots: bool = False) -> tuple[Path, Path, Path, Path | None]:
    out_dir.mkdir(parents=True, exist_ok=True)
    json_path = out_dir / f"{stem}_analysis.json"
    csv_path = out_dir / f"{stem}_correlations.csv"
    profile_path = out_dir / f"{stem}_calibration_profile.json"
    plot_root = None

    json_path.write_text(json.dumps(summary, indent=2, sort_keys=True), encoding="utf-8")
    profile_path.write_text(json.dumps(summary["calibration_profile"], indent=2, sort_keys=True), encoding="utf-8")

    fields = [
        "region",
        "category",
        "phase",
        "pair",
        "source",
        "target",
        "unit",
        "diagnostic_band",
        "samples",
        "phase_sample_count",
        "source_available_count",
        "source_availability",
        "source_amplitude",
        "target_amplitude",
        "source_noise_floor",
        "target_noise_floor",
        "corr_zero",
        "best_corr",
        "best_lag_seconds",
        "lag_confidence",
        "residual_offset",
        "residual_mean",
        "residual_p95",
        "residual_max",
        "avatar_segment_length_cm",
        "residual_p95_normalized",
    ]
    with csv_path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=fields)
        writer.writeheader()
        for row in summary["correlations"]:
            writer.writerow({field: row.get(field) for field in fields})

    if dataset is not None and not no_plots:
        plot_root = plot_outputs(dataset, summary, out_dir, stem)
    return json_path, csv_path, profile_path, plot_root


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("dataset", type=Path)
    parser.add_argument("--out-dir", type=Path, default=None)
    parser.add_argument("--no-plots", action="store_true", help="Skip PNG signal plots and heatmaps for fast tests.")
    parser.add_argument("--threshold", action="append", default=[], metavar="KEY=VALUE", help="Override a diagnostic threshold.")
    args = parser.parse_args()

    thresholds: dict[str, float] = {}
    for item in args.threshold:
        key, sep, value = item.partition("=")
        if not sep or key not in DEFAULT_THRESHOLDS:
            raise ValueError(f"unknown threshold override {item!r}")
        thresholds[key] = float(value)

    dataset = load_dataset(args.dataset)
    summary = analyze_dataset(dataset, thresholds)
    out_dir = args.out_dir or args.dataset.parent
    json_path, csv_path, profile_path, plot_root = write_outputs(summary, out_dir, args.dataset.stem, dataset, args.no_plots)
    policy = summary.get("avatar_locked_capture_policy_preflight", {})
    timing = summary.get("capture", {}).get("recorder_timing", {}) or {}
    print(
        f"tracking fusion analysis: samples={summary['sample_count']} "
        f"manifestSamples={summary['manifest_sample_count']} "
        f"effectiveHz={summary['effective_sample_rate_hz']:.3f} "
        f"missedScheduled={summary['missed_scheduled_sample_count']} "
        f"policy={policy.get('reason', 'not_applicable')} "
        f"sampleBuildAvgMs={timing.get('sample_build_avg_ms', 'n/a')} "
        f"boneBuildAvgMs={timing.get('bone_build_avg_ms', 'n/a')} "
        f"writerBacklogMax={timing.get('async_writer_backlog_max_samples', 'n/a')} "
        f"recordedBones={summary['recorded_bone_count']} "
        f"helpers={summary['helper_bone_count']} other={summary['other_bone_count']} "
        f"phases={summary['phase_count']} rows={len(summary['correlations'])} "
        f"suspicious={len(summary['suspicious_cases'])} "
        f"json={json_path} csv={csv_path} profile={profile_path} plots={plot_root if plot_root else 'skipped'}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
