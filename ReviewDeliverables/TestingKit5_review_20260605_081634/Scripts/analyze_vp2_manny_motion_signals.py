#!/usr/bin/env python3
"""Compare VP2 MediaPipe motion signals against Manny bone-derived signals."""

from __future__ import annotations

import argparse
import json
import math
from pathlib import Path

import cv2
import matplotlib.pyplot as plt
import mediapipe as mp
import numpy as np


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--vp2", required=True)
    parser.add_argument("--model", required=True)
    parser.add_argument("--manny-jsonl", required=True)
    parser.add_argument("--manny-component", default="live", choices=["live", "mirror", "flat"], help="Component to read from high-rate visible-timeseries JSON captures.")
    parser.add_argument("--out-prefix", required=True)
    parser.add_argument("--start", type=float, default=2.0)
    parser.add_argument("--end", type=float, default=18.5)
    parser.add_argument("--time-shift", type=float, default=0.0, help="Seconds added to VP2 time when sampling Manny.")
    parser.add_argument("--auto-shift", action="store_true")
    parser.add_argument(
        "--no-face-mesh",
        action="store_true",
        help="Skip dense MediaPipe FaceMesh chin/face-plane signals.",
    )
    parser.add_argument(
        "--plot-mode",
        default="standardized",
        choices=["standardized", "raw"],
        help="Plot raw values or robust-standardized values. Standardized uses median=0 and p95-p05=1 per signal in the analysis window.",
    )
    return parser.parse_args()


def nan() -> float:
    return float("nan")


def finite(values: np.ndarray) -> np.ndarray:
    return values[np.isfinite(values)]


def pct_range(values: np.ndarray) -> dict[str, float]:
    clean = finite(values)
    if clean.size == 0:
        return {"p05": nan(), "p95": nan(), "range_p95_p05": nan(), "std": nan()}
    return {
        "p05": float(np.percentile(clean, 5)),
        "p95": float(np.percentile(clean, 95)),
        "range_p95_p05": float(np.percentile(clean, 95) - np.percentile(clean, 5)),
        "std": float(np.std(clean)),
    }


def unwrap_degrees(values: np.ndarray) -> np.ndarray:
    out = values.astype(float).copy()
    mask = np.isfinite(out)
    if mask.sum() >= 2:
        out[mask] = np.rad2deg(np.unwrap(np.deg2rad(out[mask])))
    return out


def angle_from_vertical_deg(x_delta: float, y_up_delta: float) -> float:
    if not math.isfinite(x_delta) or not math.isfinite(y_up_delta):
        return nan()
    return math.degrees(math.atan2(x_delta, y_up_delta if abs(y_up_delta) > 1e-6 else 1e-6))


def angle_from_down_deg(x_delta: float, y_down_delta: float) -> float:
    if not math.isfinite(x_delta) or not math.isfinite(y_down_delta):
        return nan()
    return math.degrees(math.atan2(x_delta, y_down_delta if abs(y_down_delta) > 1e-6 else 1e-6))


def normalize_vec(v: np.ndarray) -> np.ndarray | None:
    norm = float(np.linalg.norm(v))
    if not math.isfinite(norm) or norm <= 1e-9:
        return None
    return v / norm


def world_up_ratio(a: np.ndarray, b: np.ndarray, up: np.ndarray, scale: float) -> float:
    if scale <= 1e-9:
        return nan()
    return float(np.dot(a - b, up) / scale)


def safe_ratio(numerator: float, denominator: float) -> float:
    if not math.isfinite(numerator) or not math.isfinite(denominator) or abs(denominator) <= 1e-9:
        return nan()
    return float(numerator / denominator)


def pitch_from_forward_up_deg(forward: np.ndarray, up: np.ndarray, right: np.ndarray) -> float:
    forward_no_right = forward - np.dot(forward, right) * right
    forward_no_right = normalize_vec(forward_no_right)
    if forward_no_right is None:
        return nan()
    body_forward = normalize_vec(np.cross(right, up))
    if body_forward is None:
        return nan()
    if np.dot(forward_no_right, body_forward) < 0.0:
        body_forward = -body_forward
    return float(math.degrees(math.atan2(np.dot(forward_no_right, up), max(abs(float(np.dot(forward_no_right, body_forward))), 1e-6))))


def half_life_alpha(half_life_s: float, dt_s: float) -> float:
    if half_life_s <= 0.0:
        return 1.0
    if dt_s <= 0.0:
        return 0.0
    return float(1.0 - math.exp(-math.log(2.0) * dt_s / half_life_s))


def as_arrays(rows: list[dict[str, float]]) -> dict[str, np.ndarray]:
    keys = sorted({key for row in rows for key in row})
    return {key: np.array([float(row.get(key, nan())) for row in rows], dtype=float) for key in keys}


def add_delta_signals(data: dict[str, np.ndarray], reference_seconds: float = 1.0) -> None:
    t = data["t"]
    ref_mask = t <= (np.nanmin(t) + reference_seconds)
    for key, values in list(data.items()):
        if key == "t" or key.endswith("_vis"):
            continue
        vals = values.astype(float)
        if key.endswith("_deg") or key.endswith("_yaw") or key.endswith("_pitch") or key.endswith("_roll"):
            vals = unwrap_degrees(vals)
            data[key] = vals
        ref_vals = finite(vals[ref_mask])
        if ref_vals.size == 0:
            ref_vals = finite(vals[: min(vals.size, 20)])
        if ref_vals.size == 0:
            ref_vals = finite(vals)
        if ref_vals.size:
            data[f"{key}_delta"] = vals - float(np.median(ref_vals))


def face_mesh_point(
    face_landmarks,
    index: int,
    width: int,
    height: int,
    origin_x: float = 0.0,
    origin_y: float = 0.0,
    inverse_scale: float = 1.0,
) -> np.ndarray:
    lm = face_landmarks.landmark[index]
    return np.array(
        [
            origin_x + float(lm.x) * width * inverse_scale,
            origin_y + float(lm.y) * height * inverse_scale,
            float(lm.z) * width * inverse_scale,
        ],
        dtype=float,
    )


def pose_guided_head_crop(frame: np.ndarray, pose_landmarks) -> tuple[np.ndarray, float, float, float] | None:
    height, width = frame.shape[:2]
    pts = np.array([[lm.x * width, lm.y * height] for lm in pose_landmarks], dtype=float)
    ls, rs = pts[11], pts[12]
    shoulder_span = float(np.linalg.norm(rs - ls))
    face_indices = [0, 2, 5, 7, 8, 9, 10]
    face_pts = pts[face_indices]
    finite_mask = np.isfinite(face_pts).all(axis=1)
    if finite_mask.sum() < 2:
        return None
    face_pts = face_pts[finite_mask]
    min_xy = np.min(face_pts, axis=0)
    max_xy = np.max(face_pts, axis=0)
    center = 0.5 * (min_xy + max_xy)
    face_box = max(float(max_xy[0] - min_xy[0]), float(max_xy[1] - min_xy[1]))
    crop_size = max(face_box * 3.2, shoulder_span * 0.55, 96.0)
    half = crop_size * 0.5
    x0 = int(max(0, math.floor(center[0] - half)))
    x1 = int(min(width, math.ceil(center[0] + half)))
    y0 = int(max(0, math.floor(center[1] - half * 1.05)))
    y1 = int(min(height, math.ceil(center[1] + half * 1.35)))
    if x1 - x0 < 24 or y1 - y0 < 24:
        return None
    crop = frame[y0:y1, x0:x1]
    crop_h, crop_w = crop.shape[:2]
    scale = min(4.0, max(1.0, 512.0 / max(crop_w, crop_h)))
    if scale > 1.01:
        crop = cv2.resize(crop, (int(round(crop_w * scale)), int(round(crop_h * scale))), interpolation=cv2.INTER_CUBIC)
    return crop, float(x0), float(y0), 1.0 / scale


def face_mesh_pnp_angles_deg(points_2d: dict[int, np.ndarray], width: int, height: int) -> tuple[float, float, float]:
    required = (1, 152, 33, 263, 61, 291)
    if any(index not in points_2d for index in required):
        return nan(), nan(), nan()

    image_points = np.array(
        [
            points_2d[1][:2],    # nose tip
            points_2d[152][:2],  # chin
            points_2d[33][:2],   # eye outer corner
            points_2d[263][:2],  # opposite eye outer corner
            points_2d[61][:2],   # mouth corner
            points_2d[291][:2],  # opposite mouth corner
        ],
        dtype=np.float64,
    )
    model_points = np.array(
        [
            (0.0, 0.0, 0.0),
            (0.0, -63.0, -12.0),
            (-43.0, 32.0, -26.0),
            (43.0, 32.0, -26.0),
            (-28.0, -28.0, -24.0),
            (28.0, -28.0, -24.0),
        ],
        dtype=np.float64,
    )
    focal_length = float(width)
    camera_matrix = np.array(
        [[focal_length, 0.0, width * 0.5], [0.0, focal_length, height * 0.5], [0.0, 0.0, 1.0]],
        dtype=np.float64,
    )
    distortion = np.zeros((4, 1), dtype=np.float64)
    ok, rotation_vec, _translation_vec = cv2.solvePnP(
        model_points,
        image_points,
        camera_matrix,
        distortion,
        flags=cv2.SOLVEPNP_ITERATIVE,
    )
    if not ok:
        return nan(), nan(), nan()
    rotation_matrix, _ = cv2.Rodrigues(rotation_vec)
    angles = cv2.RQDecomp3x3(rotation_matrix)[0]
    return float(angles[0]), float(angles[1]), float(angles[2])


def fill_face_mesh_row(
    row: dict[str, float],
    face,
    frame_width: int,
    frame_height: int,
    origin_x: float = 0.0,
    origin_y: float = 0.0,
    inverse_scale: float = 1.0,
) -> None:
    indices = (1, 13, 14, 33, 61, 133, 152, 263, 291, 362)
    pts = {
        index: face_mesh_point(face, index, frame_width, frame_height, origin_x, origin_y, inverse_scale)
        for index in indices
    }
    eye_left = 0.5 * (pts[33] + pts[133])
    eye_right = 0.5 * (pts[263] + pts[362])
    eye_mid = 0.5 * (eye_left + eye_right)
    eye_span = float(np.linalg.norm(eye_right[:2] - eye_left[:2]))
    mouth_mid = 0.5 * (pts[13] + pts[14])
    if eye_span > 1e-6:
        row.update(
            {
                "face_mesh_chin_eye_y_norm": safe_ratio(float(pts[152][1] - eye_mid[1]), eye_span),
                "face_mesh_chin_nose_y_norm": safe_ratio(float(pts[152][1] - pts[1][1]), eye_span),
                "face_mesh_chin_mouth_y_norm": safe_ratio(float(pts[152][1] - mouth_mid[1]), eye_span),
                "face_mesh_mouth_eye_y_norm": safe_ratio(float(mouth_mid[1] - eye_mid[1]), eye_span),
                "face_mesh_nose_eye_y_norm": safe_ratio(float(pts[1][1] - eye_mid[1]), eye_span),
                "face_mesh_lip_open_y_norm": safe_ratio(float(pts[14][1] - pts[13][1]), eye_span),
                "face_mesh_chin_eye_z_norm": safe_ratio(float(pts[152][2] - eye_mid[2]), eye_span),
                "face_mesh_chin_nose_z_norm": safe_ratio(float(pts[152][2] - pts[1][2]), eye_span),
                "face_mesh_chin_mouth_z_norm": safe_ratio(float(pts[152][2] - mouth_mid[2]), eye_span),
                "face_mesh_mouth_eye_z_norm": safe_ratio(float(mouth_mid[2] - eye_mid[2]), eye_span),
                "face_mesh_nose_eye_z_norm": safe_ratio(float(pts[1][2] - eye_mid[2]), eye_span),
                "face_mesh_chin_eye_yz_angle_deg": float(math.degrees(math.atan2(pts[152][2] - eye_mid[2], pts[152][1] - eye_mid[1]))),
                "face_mesh_chin_nose_yz_angle_deg": float(math.degrees(math.atan2(pts[152][2] - pts[1][2], pts[152][1] - pts[1][1]))),
                "face_mesh_chin_mouth_yz_angle_deg": float(math.degrees(math.atan2(pts[152][2] - mouth_mid[2], pts[152][1] - mouth_mid[1]))),
                "face_mesh_vis": 1.0,
            }
        )
    pitch_x, pitch_y, pitch_z = face_mesh_pnp_angles_deg(pts, frame_width, frame_height)
    row["face_mesh_pitch_x_deg"] = pitch_x
    row["face_mesh_pitch_y_deg"] = pitch_y
    row["face_mesh_pitch_z_deg"] = pitch_z


def analyze_vp2_face_mesh(video_path: Path, pose_model_path: Path) -> dict[str, np.ndarray]:
    cap = cv2.VideoCapture(str(video_path))
    if not cap.isOpened():
        raise RuntimeError(f"Failed to open video: {video_path}")
    fps = cap.get(cv2.CAP_PROP_FPS) or 30.0
    width = int(cap.get(cv2.CAP_PROP_FRAME_WIDTH))
    height = int(cap.get(cv2.CAP_PROP_FRAME_HEIGHT))

    pose_options = mp.tasks.vision.PoseLandmarkerOptions(
        base_options=mp.tasks.BaseOptions(model_asset_path=str(pose_model_path)),
        running_mode=mp.tasks.vision.RunningMode.VIDEO,
        num_poses=1,
        min_pose_detection_confidence=0.5,
        min_pose_presence_confidence=0.5,
        min_tracking_confidence=0.5,
        output_segmentation_masks=False,
    )

    rows: list[dict[str, float]] = []
    frame_index = 0
    with mp.tasks.vision.PoseLandmarker.create_from_options(pose_options) as pose_landmarker, mp.solutions.face_mesh.FaceMesh(
        static_image_mode=True,
        max_num_faces=1,
        refine_landmarks=True,
        min_detection_confidence=0.5,
        min_tracking_confidence=0.5,
    ) as face_mesh:
        while True:
            ok, frame = cap.read()
            if not ok:
                break
            row = {
                "t": frame_index / fps,
                "face_mesh_chin_eye_y_norm": nan(),
                "face_mesh_chin_nose_y_norm": nan(),
                "face_mesh_chin_mouth_y_norm": nan(),
                "face_mesh_mouth_eye_y_norm": nan(),
                "face_mesh_nose_eye_y_norm": nan(),
                "face_mesh_lip_open_y_norm": nan(),
                "face_mesh_chin_eye_z_norm": nan(),
                "face_mesh_chin_nose_z_norm": nan(),
                "face_mesh_chin_mouth_z_norm": nan(),
                "face_mesh_mouth_eye_z_norm": nan(),
                "face_mesh_nose_eye_z_norm": nan(),
                "face_mesh_chin_eye_yz_angle_deg": nan(),
                "face_mesh_chin_nose_yz_angle_deg": nan(),
                "face_mesh_chin_mouth_yz_angle_deg": nan(),
                "face_mesh_pitch_x_deg": nan(),
                "face_mesh_pitch_y_deg": nan(),
                "face_mesh_pitch_z_deg": nan(),
                "face_mesh_vis": 0.0,
            }
            rgb = cv2.cvtColor(frame, cv2.COLOR_BGR2RGB)
            result = face_mesh.process(rgb)
            if result.multi_face_landmarks:
                fill_face_mesh_row(row, result.multi_face_landmarks[0], width, height)
            else:
                timestamp_ms = int(round(frame_index * 1000.0 / fps))
                pose_result = pose_landmarker.detect_for_video(mp.Image(image_format=mp.ImageFormat.SRGB, data=rgb), timestamp_ms)
                if pose_result.pose_landmarks:
                    crop_info = pose_guided_head_crop(frame, pose_result.pose_landmarks[0])
                    if crop_info is not None:
                        crop, origin_x, origin_y, inverse_scale = crop_info
                        crop_rgb = cv2.cvtColor(crop, cv2.COLOR_BGR2RGB)
                        crop_result = face_mesh.process(crop_rgb)
                        if crop_result.multi_face_landmarks:
                            crop_h, crop_w = crop.shape[:2]
                            fill_face_mesh_row(
                                row,
                                crop_result.multi_face_landmarks[0],
                                crop_w,
                                crop_h,
                                origin_x,
                                origin_y,
                                inverse_scale,
                            )
            rows.append(row)
            frame_index += 1
    cap.release()
    data = as_arrays(rows)
    add_delta_signals(data)
    return data


def merge_signal_data(base: dict[str, np.ndarray], extra: dict[str, np.ndarray]) -> dict[str, np.ndarray]:
    if "t" not in extra:
        return base
    base_t = base.get("t")
    extra_t = extra["t"]
    for key, values in extra.items():
        if key == "t":
            continue
        if base_t is not None and base_t.size == extra_t.size and np.allclose(base_t, extra_t, equal_nan=True):
            base[key] = values
        elif base_t is not None:
            base[key] = interp_signal(extra, key, base_t)
    return base


def add_vp2_face_pitch_consensus_delta(data: dict[str, np.ndarray]) -> None:
    mouth_eye = data.get("mouth_eye_y_norm_delta")
    nose_eye = data.get("nose_eye_y_norm_delta")
    mouth_ear = data.get("mouth_ear_y_norm_delta")
    nose_ear = data.get("nose_ear_y_norm_delta")
    if mouth_eye is None or nose_eye is None:
        return
    existing = data.get("face_pitch_consensus_y_norm_delta", np.full_like(mouth_eye, nan(), dtype=float))
    consensus = existing.astype(float).copy()
    if nose_ear is not None:
        nose_ear_valid = np.isfinite(nose_ear)
        consensus[nose_ear_valid] = nose_ear[nose_ear_valid]
    both_valid = np.isfinite(mouth_eye) & np.isfinite(nose_eye)
    needs_fallback = ~np.isfinite(consensus)
    agree = mouth_eye * nose_eye >= 0.0
    consensus[needs_fallback & both_valid & agree] = 0.5 * (mouth_eye[needs_fallback & both_valid & agree] + nose_eye[needs_fallback & both_valid & agree])
    disagree = needs_fallback & both_valid & ~agree
    if mouth_ear is not None and nose_ear is not None:
        ears_valid = np.isfinite(mouth_ear) & np.isfinite(nose_ear)
        ears_agree = mouth_ear * nose_ear >= 0.0
        ear_fallback = disagree & ears_valid & ears_agree
        consensus[ear_fallback] = 0.5 * (mouth_ear[ear_fallback] + nose_ear[ear_fallback])
        consensus[disagree & ~ear_fallback] = 0.0
    elif mouth_ear is not None:
        ear_fallback = disagree & np.isfinite(mouth_ear)
        consensus[ear_fallback] = mouth_ear[ear_fallback]
        consensus[disagree & ~ear_fallback] = 0.0
    else:
        consensus[disagree] = 0.0
    data["face_pitch_consensus_y_norm_delta"] = consensus


def add_followed_reference_delta(
    data: dict[str, np.ndarray],
    source_key: str,
    dest_key: str,
    reference_half_life_s: float = 12.0,
) -> None:
    t = data.get("t")
    values = data.get(source_key)
    if t is None or values is None:
        return
    out = np.full_like(t, nan(), dtype=float)
    ref: float | None = None
    last_t: float | None = None
    for i, (time_s, value) in enumerate(zip(t, values)):
        if not math.isfinite(time_s) or not math.isfinite(value):
            continue
        if ref is None:
            ref = float(value)
            out[i] = 0.0
        else:
            delta = float(value) - ref
            out[i] = delta
            previous_t = float(time_s) if last_t is None else float(last_t)
            alpha = half_life_alpha(reference_half_life_s, max(float(time_s) - previous_t, 0.0))
            ref += delta * alpha
        last_t = float(time_s)
    data[dest_key] = out


def add_vp2_runtime_head_pitch_estimates(data: dict[str, np.ndarray]) -> None:
    for key in (
        "mouth_eye_y_norm",
        "nose_eye_y_norm",
        "mouth_ear_y_norm",
        "nose_ear_y_norm",
        "mouth_eye_up_world_norm",
        "nose_eye_up_world_norm",
        "mouth_ear_up_world_norm",
        "nose_ear_up_world_norm",
        "head_forward_pitch_world_deg",
        "head_center_offset_y_norm",
        "nose_face_offset_y_norm",
        "nose_shoulder_offset_y_norm",
    ):
        add_followed_reference_delta(data, key, f"runtime_{key}_delta")

    t = data.get("t")
    if t is None:
        return

    mouth_eye = data.get("runtime_mouth_eye_y_norm_delta")
    nose_eye = data.get("runtime_nose_eye_y_norm_delta")
    mouth_ear = data.get("runtime_mouth_ear_y_norm_delta")
    nose_ear = data.get("runtime_nose_ear_y_norm_delta")
    world_mouth_eye = data.get("runtime_mouth_eye_up_world_norm_delta")
    world_nose_eye = data.get("runtime_nose_eye_up_world_norm_delta")
    world_mouth_ear = data.get("runtime_mouth_ear_up_world_norm_delta")
    world_nose_ear = data.get("runtime_nose_ear_up_world_norm_delta")
    world_forward_pitch = data.get("runtime_head_forward_pitch_world_deg_delta")
    center_y = data.get("runtime_head_center_offset_y_norm_delta")
    nose_y = data.get("runtime_nose_face_offset_y_norm_delta")
    shoulder_nose_y = data.get("runtime_nose_shoulder_offset_y_norm_delta")
    if any(v is None for v in (mouth_eye, nose_eye, center_y, nose_y, shoulder_nose_y)):
        return

    face_input = np.full_like(t, nan(), dtype=float)
    if world_mouth_eye is not None:
        valid_world = np.isfinite(world_mouth_eye)
        face_input[valid_world] = world_mouth_eye[valid_world]
    if world_nose_eye is not None:
        fallback = ~np.isfinite(face_input) & np.isfinite(world_nose_eye)
        face_input[fallback] = world_nose_eye[fallback]
    if nose_ear is not None:
        nose_ear_valid = ~np.isfinite(face_input) & np.isfinite(nose_ear)
        face_input[nose_ear_valid] = nose_ear[nose_ear_valid]
    both_valid = np.isfinite(mouth_eye) & np.isfinite(nose_eye)
    needs_fallback = ~np.isfinite(face_input)
    agree = mouth_eye * nose_eye >= 0.0
    face_input[needs_fallback & both_valid & agree] = 0.5 * (mouth_eye[needs_fallback & both_valid & agree] + nose_eye[needs_fallback & both_valid & agree])
    disagree = needs_fallback & both_valid & ~agree
    if mouth_ear is not None and nose_ear is not None:
        ears_valid = np.isfinite(mouth_ear) & np.isfinite(nose_ear)
        ears_agree = mouth_ear * nose_ear >= 0.0
        ear_fallback = disagree & ears_valid & ears_agree
        if world_nose_ear is not None:
            world_ear_fallback = disagree & np.isfinite(world_nose_ear)
            face_input[world_ear_fallback] = world_nose_ear[world_ear_fallback]
            ear_fallback &= ~world_ear_fallback
        if world_mouth_ear is not None:
            world_mouth_ear_fallback = disagree & ~np.isfinite(face_input) & np.isfinite(world_mouth_ear)
            face_input[world_mouth_ear_fallback] = world_mouth_ear[world_mouth_ear_fallback]
            ear_fallback &= ~world_mouth_ear_fallback
        face_input[ear_fallback] = 0.5 * (mouth_ear[ear_fallback] + nose_ear[ear_fallback])
        face_input[disagree & ~ear_fallback & ~np.isfinite(face_input)] = 0.0
    elif mouth_ear is not None:
        ear_fallback = disagree & np.isfinite(mouth_ear)
        face_input[ear_fallback] = mouth_ear[ear_fallback]
        face_input[disagree & ~ear_fallback] = 0.0
    else:
        face_input[disagree] = 0.0
    unresolved = ~np.isfinite(face_input)
    mouth_only = unresolved & ~both_valid & np.isfinite(mouth_eye)
    nose_only = unresolved & ~both_valid & ~mouth_only & np.isfinite(nose_eye)
    face_input[mouth_only] = mouth_eye[mouth_only]
    face_input[nose_only] = nose_eye[nose_only]
    if mouth_ear is not None:
        fallback = ~np.isfinite(face_input) & np.isfinite(mouth_ear)
        face_input[fallback] = mouth_ear[fallback]

    shoulder_pitch_input = np.full_like(t, nan(), dtype=float)
    shoulder_valid = np.isfinite(shoulder_nose_y)
    shoulder_pitch_input[shoulder_valid] = np.where(
        np.abs(shoulder_nose_y[shoulder_valid]) > 0.005,
        -shoulder_nose_y[shoulder_valid],
        0.0,
    )

    valid = np.isfinite(face_input) & np.isfinite(shoulder_pitch_input) & np.isfinite(nose_y) & np.isfinite(center_y)
    legacy_estimate = np.full_like(t, nan(), dtype=float)
    legacy_estimate[valid] = (
        face_input[valid] * 65.0
        + shoulder_pitch_input[valid] * 12.0
        + nose_y[valid] * 16.0
        + center_y[valid] * 8.0
    )
    estimate = face_input * 65.0

    data["runtime_face_pitch_input_norm_delta"] = face_input
    data["runtime_shoulder_nose_pitch_input_norm_delta"] = shoulder_pitch_input
    data["runtime_screen_head_pitch_face_only_est_deg_delta"] = face_input * 65.0
    data["runtime_head_forward_pitch_world_est_deg_delta"] = world_forward_pitch if world_forward_pitch is not None else np.full_like(t, nan(), dtype=float)
    data["runtime_screen_head_pitch_legacy_est_deg_delta"] = legacy_estimate
    data["runtime_screen_head_pitch_est_deg_delta"] = estimate
    data["runtime_screen_head_pitch_clamped_est_deg_delta"] = np.clip(estimate, -45.0, 45.0)

    static_face = data.get("face_pitch_consensus_y_norm_delta")
    static_center = data.get("head_center_offset_y_norm_delta")
    static_nose = data.get("nose_face_offset_y_norm_delta")
    static_shoulder = data.get("nose_shoulder_offset_y_norm_delta")
    if all(v is not None for v in (static_face, static_center, static_nose, static_shoulder)):
        static_shoulder_input = np.full_like(t, nan(), dtype=float)
        static_valid = np.isfinite(static_shoulder)
        static_shoulder_input[static_valid] = np.where(
            np.abs(static_shoulder[static_valid]) > 0.005,
            -static_shoulder[static_valid],
            0.0,
        )
        static_estimate = np.full_like(t, nan(), dtype=float)
        static_all_valid = (
            np.isfinite(static_face)
            & np.isfinite(static_center)
            & np.isfinite(static_nose)
            & np.isfinite(static_shoulder_input)
        )
        static_estimate[static_all_valid] = (
            static_face[static_all_valid] * 65.0
            + static_shoulder_input[static_all_valid] * 12.0
            + static_nose[static_all_valid] * 16.0
            + static_center[static_all_valid] * 8.0
        )
        data["static_screen_head_pitch_est_deg_delta"] = static_estimate
        data["static_screen_head_pitch_face_only_est_deg_delta"] = static_face * 65.0
        data["static_shoulder_nose_pitch_input_norm_delta"] = static_shoulder_input


def analyze_vp2(video_path: Path, model_path: Path) -> dict[str, np.ndarray]:
    cap = cv2.VideoCapture(str(video_path))
    if not cap.isOpened():
        raise RuntimeError(f"Failed to open video: {video_path}")
    fps = cap.get(cv2.CAP_PROP_FPS) or 30.0
    width = int(cap.get(cv2.CAP_PROP_FRAME_WIDTH))
    height = int(cap.get(cv2.CAP_PROP_FRAME_HEIGHT))

    options = mp.tasks.vision.PoseLandmarkerOptions(
        base_options=mp.tasks.BaseOptions(model_asset_path=str(model_path)),
        running_mode=mp.tasks.vision.RunningMode.VIDEO,
        num_poses=1,
        min_pose_detection_confidence=0.5,
        min_pose_presence_confidence=0.5,
        min_tracking_confidence=0.5,
        output_segmentation_masks=False,
    )

    rows: list[dict[str, float]] = []
    frame_index = 0
    with mp.tasks.vision.PoseLandmarker.create_from_options(options) as landmarker:
        while True:
            ok, frame = cap.read()
            if not ok:
                break
            timestamp_ms = int(round(frame_index * 1000.0 / fps))
            rgb = cv2.cvtColor(frame, cv2.COLOR_BGR2RGB)
            result = landmarker.detect_for_video(mp.Image(image_format=mp.ImageFormat.SRGB, data=rgb), timestamp_ms)
            row = {
                "t": frame_index / fps,
                "shoulder_width": nan(),
                "left_shoulder_height_norm": nan(),
                "right_shoulder_height_norm": nan(),
                "left_shoulder_head_clearance_norm": nan(),
                "right_shoulder_head_clearance_norm": nan(),
                "bilateral_shoulder_head_clearance_norm": nan(),
                "left_shrug_clearance_norm": nan(),
                "right_shrug_clearance_norm": nan(),
                "bilateral_shrug_clearance_norm": nan(),
                "left_shrug_clearance_world_norm": nan(),
                "right_shrug_clearance_world_norm": nan(),
                "bilateral_shrug_clearance_world_norm": nan(),
                "left_head_center_clearance_world_norm": nan(),
                "right_head_center_clearance_world_norm": nan(),
                "bilateral_head_center_clearance_world_norm": nan(),
                "left_head_center_clearance_norm": nan(),
                "right_head_center_clearance_norm": nan(),
                "bilateral_head_center_clearance_norm": nan(),
                "left_shoulder_mid_up_norm": nan(),
                "right_shoulder_mid_up_norm": nan(),
                "shoulder_asym_norm": nan(),
                "shoulder_roll_deg": nan(),
                "head_x_norm": nan(),
                "head_y_norm": nan(),
                "mouth_y_norm": nan(),
                "mouth_ear_y_norm": nan(),
                "nose_ear_y_norm": nan(),
                "mouth_eye_y_norm": nan(),
                "nose_eye_y_norm": nan(),
                "face_pitch_consensus_y_norm": nan(),
                "head_center_offset_x_norm": nan(),
                "head_center_offset_y_norm": nan(),
                "nose_face_offset_x_norm": nan(),
                "nose_face_offset_y_norm": nan(),
                "nose_shoulder_offset_x_norm": nan(),
                "nose_shoulder_offset_y_norm": nan(),
                "mouth_eye_up_world_norm": nan(),
                "nose_eye_up_world_norm": nan(),
                "mouth_ear_up_world_norm": nan(),
                "nose_ear_up_world_norm": nan(),
                "head_forward_pitch_world_deg": nan(),
                "head_lateral_angle_deg": nan(),
                "nose_from_ear_mid_x_norm": nan(),
                "ear_roll_deg": nan(),
                "left_upperarm_down_angle_deg": nan(),
                "right_upperarm_down_angle_deg": nan(),
                "left_elbow_height_norm": nan(),
                "right_elbow_height_norm": nan(),
                "shoulder_vis": nan(),
                "head_vis": nan(),
            }
            if result.pose_landmarks:
                pose = result.pose_landmarks[0]
                pts = np.array([[lm.x * width, lm.y * height] for lm in pose], dtype=float)
                vis = np.array([float(getattr(lm, "visibility", 1.0) or 0.0) for lm in pose], dtype=float)
                ls, rs = pts[11], pts[12]
                le, re = pts[13], pts[14]
                lh, rh = pts[23], pts[24]
                nose = pts[0]
                lear, rear = pts[7], pts[8]
                mouth = (pts[9] + pts[10]) * 0.5
                shoulder_width = float(np.linalg.norm(rs - ls))
                if shoulder_width > 1e-6:
                    smid = (ls + rs) * 0.5
                    hmid = (lh + rh) * 0.5
                    ear_span = float(np.linalg.norm(rear - lear))
                    eye_span = float(np.linalg.norm(pts[5] - pts[2]))
                    ear_mid = (lear + rear) * 0.5
                    eye_mid = (pts[2] + pts[5]) * 0.5
                    head_center_2d = nose
                    face_span_2d = shoulder_width * 0.35
                    if ear_span > 1e-6:
                        head_center_2d = ear_mid
                        face_span_2d = max(ear_span, face_span_2d)
                    elif eye_span > 1e-6:
                        head_center_2d = eye_mid
                        face_span_2d = max(eye_span, face_span_2d)
                    left_head_clearance = float((ls[1] - lear[1]) / shoulder_width)
                    right_head_clearance = float((rs[1] - rear[1]) / shoulder_width)
                    bilateral_head_clearance = 0.5 * (left_head_clearance + right_head_clearance)
                    left_center_clearance = float((ls[1] - ear_mid[1]) / shoulder_width)
                    right_center_clearance = float((rs[1] - ear_mid[1]) / shoulder_width)
                    bilateral_center_clearance = 0.5 * (left_center_clearance + right_center_clearance)
                    row.update(
                        {
                            "shoulder_width": shoulder_width,
                            "left_shoulder_height_norm": float((hmid[1] - ls[1]) / shoulder_width),
                            "right_shoulder_height_norm": float((hmid[1] - rs[1]) / shoulder_width),
                            "left_shoulder_head_clearance_norm": left_head_clearance,
                            "right_shoulder_head_clearance_norm": right_head_clearance,
                            "bilateral_shoulder_head_clearance_norm": bilateral_head_clearance,
                            "left_shrug_clearance_norm": -left_head_clearance,
                            "right_shrug_clearance_norm": -right_head_clearance,
                            "bilateral_shrug_clearance_norm": -bilateral_head_clearance,
                            "left_head_center_clearance_norm": -left_center_clearance,
                            "right_head_center_clearance_norm": -right_center_clearance,
                            "bilateral_head_center_clearance_norm": -bilateral_center_clearance,
                            "left_shoulder_mid_up_norm": float((smid[1] - ls[1]) / shoulder_width),
                            "right_shoulder_mid_up_norm": float((smid[1] - rs[1]) / shoulder_width),
                            "shoulder_asym_norm": float((rs[1] - ls[1]) / shoulder_width),
                            "shoulder_roll_deg": float(math.degrees(math.atan2(rs[1] - ls[1], abs(rs[0] - ls[0])))),
                            "head_x_norm": float((nose[0] - smid[0]) / shoulder_width),
                            "head_y_norm": float((smid[1] - nose[1]) / shoulder_width),
                            "mouth_y_norm": float((mouth[1] - smid[1]) / shoulder_width),
                            "head_center_offset_x_norm": float((head_center_2d[0] - smid[0]) / shoulder_width),
                            "head_center_offset_y_norm": float((head_center_2d[1] - smid[1]) / shoulder_width),
                            "nose_face_offset_x_norm": float((nose[0] - head_center_2d[0]) / max(face_span_2d, 1e-6)),
                            "nose_face_offset_y_norm": float((nose[1] - head_center_2d[1]) / max(face_span_2d, 1e-6)),
                            "nose_shoulder_offset_x_norm": float((nose[0] - smid[0]) / shoulder_width),
                            "nose_shoulder_offset_y_norm": float((nose[1] - smid[1]) / shoulder_width),
                            "head_lateral_angle_deg": angle_from_vertical_deg(nose[0] - smid[0], smid[1] - nose[1]),
                            "ear_roll_deg": float(math.degrees(math.atan2(rear[1] - lear[1], abs(rear[0] - lear[0])))),
                            "left_upperarm_down_angle_deg": angle_from_down_deg(le[0] - ls[0], le[1] - ls[1]),
                            "right_upperarm_down_angle_deg": angle_from_down_deg(re[0] - rs[0], re[1] - rs[1]),
                            "left_elbow_height_norm": float((smid[1] - le[1]) / shoulder_width),
                            "right_elbow_height_norm": float((smid[1] - re[1]) / shoulder_width),
                            "shoulder_vis": float(min(vis[11], vis[12])),
                            "head_vis": float(min(vis[0], vis[7], vis[8])),
                        }
                    )
                    if ear_span > 1e-6:
                        row["nose_from_ear_mid_x_norm"] = float((nose[0] - ear_mid[0]) / ear_span)
                        row["mouth_ear_y_norm"] = float((mouth[1] - ear_mid[1]) / ear_span)
                        row["nose_ear_y_norm"] = float((nose[1] - ear_mid[1]) / ear_span)
                    if eye_span > 1e-6:
                        row["mouth_eye_y_norm"] = float((mouth[1] - eye_mid[1]) / eye_span)
                        row["nose_eye_y_norm"] = float((nose[1] - eye_mid[1]) / eye_span)
                    face_pitch_candidates = [
                        row[key]
                        for key in ("mouth_eye_y_norm", "nose_eye_y_norm")
                        if math.isfinite(row[key])
                    ]
                    if face_pitch_candidates:
                        row["face_pitch_consensus_y_norm"] = float(np.mean(face_pitch_candidates))
                    elif math.isfinite(row["mouth_ear_y_norm"]):
                        row["face_pitch_consensus_y_norm"] = row["mouth_ear_y_norm"]
            if result.pose_world_landmarks:
                wpose = result.pose_world_landmarks[0]
                wpts = np.array([[lm.x, lm.y, lm.z] for lm in wpose], dtype=float)
                ls_w, rs_w = wpts[11], wpts[12]
                lh_w, rh_w = wpts[23], wpts[24]
                nose_w = wpts[0]
                lear_w, rear_w = wpts[7], wpts[8]
                leye_w, reye_w = wpts[2], wpts[5]
                mouth_w = (wpts[9] + wpts[10]) * 0.5
                shoulder_width_w = float(np.linalg.norm(rs_w - ls_w))
                shoulder_mid_w = (ls_w + rs_w) * 0.5
                hip_mid_w = (lh_w + rh_w) * 0.5
                up_w = normalize_vec(shoulder_mid_w - hip_mid_w)
                if up_w is not None and shoulder_width_w > 1e-6:
                    ear_mid_w = (lear_w + rear_w) * 0.5
                    eye_mid_w = (leye_w + reye_w) * 0.5
                    ear_span_w = float(np.linalg.norm(rear_w - lear_w))
                    eye_span_w = float(np.linalg.norm(reye_w - leye_w))
                    left_clearance_w = world_up_ratio(lear_w, ls_w, up_w, shoulder_width_w)
                    right_clearance_w = world_up_ratio(rear_w, rs_w, up_w, shoulder_width_w)
                    left_center_clearance_w = world_up_ratio(ear_mid_w, ls_w, up_w, shoulder_width_w)
                    right_center_clearance_w = world_up_ratio(ear_mid_w, rs_w, up_w, shoulder_width_w)
                    row.update(
                        {
                            "left_shrug_clearance_world_norm": -left_clearance_w,
                            "right_shrug_clearance_world_norm": -right_clearance_w,
                            "bilateral_shrug_clearance_world_norm": -0.5 * (left_clearance_w + right_clearance_w),
                            "left_head_center_clearance_world_norm": -left_center_clearance_w,
                            "right_head_center_clearance_world_norm": -right_center_clearance_w,
                            "bilateral_head_center_clearance_world_norm": -0.5 * (left_center_clearance_w + right_center_clearance_w),
                        }
                    )
                    if eye_span_w > 1e-6:
                        row["mouth_eye_up_world_norm"] = world_up_ratio(mouth_w, eye_mid_w, up_w, eye_span_w)
                        row["nose_eye_up_world_norm"] = world_up_ratio(nose_w, eye_mid_w, up_w, eye_span_w)
                    if ear_span_w > 1e-6:
                        row["mouth_ear_up_world_norm"] = world_up_ratio(mouth_w, ear_mid_w, up_w, ear_span_w)
                        row["nose_ear_up_world_norm"] = world_up_ratio(nose_w, ear_mid_w, up_w, ear_span_w)
                    head_right_w = normalize_vec(rear_w - lear_w)
                    if head_right_w is None:
                        head_right_w = normalize_vec(reye_w - leye_w)
                    head_center_w = ear_mid_w if ear_span_w > 1e-6 else eye_mid_w
                    if head_right_w is not None:
                        row["head_forward_pitch_world_deg"] = pitch_from_forward_up_deg(nose_w - head_center_w, up_w, head_right_w)
            rows.append(row)
            frame_index += 1
    cap.release()
    data = as_arrays(rows)
    add_delta_signals(data)
    add_vp2_face_pitch_consensus_delta(data)
    add_vp2_runtime_head_pitch_estimates(data)
    return data


def pos(loc: dict[str, list[float]], name: str) -> np.ndarray | None:
    value = loc.get(name)
    return np.array(value, dtype=float) if value and len(value) >= 3 else None


def rot(rotations: dict[str, list[float]], name: str) -> tuple[float, float, float] | None:
    value = rotations.get(name)
    if value and len(value) >= 3:
        return float(value[0]), float(value[1]), float(value[2])
    return None


def quat_values(quaternions: dict[str, list[float]], name: str) -> np.ndarray | None:
    value = quaternions.get(name)
    if value and len(value) >= 4:
        q = np.array([float(value[0]), float(value[1]), float(value[2]), float(value[3])], dtype=float)
        norm = float(np.linalg.norm(q))
        return q / norm if norm > 1e-9 else None
    return None


def quat_inverse(q: np.ndarray) -> np.ndarray:
    return np.array([-q[0], -q[1], -q[2], q[3]], dtype=float)


def quat_mul(a: np.ndarray, b: np.ndarray) -> np.ndarray:
    ax, ay, az, aw = a
    bx, by, bz, bw = b
    return np.array(
        [
            aw * bx + ax * bw + ay * bz - az * by,
            aw * by - ax * bz + ay * bw + az * bx,
            aw * bz + ax * by - ay * bx + az * bw,
            aw * bw - ax * bx - ay * by - az * bz,
        ],
        dtype=float,
    )


def quat_delta_angle_deg(q: np.ndarray | None, ref: np.ndarray | None) -> float:
    if q is None or ref is None:
        return nan()
    delta = quat_mul(q, quat_inverse(ref))
    norm = float(np.linalg.norm(delta))
    if norm <= 1e-9:
        return nan()
    delta /= norm
    return float(math.degrees(2.0 * math.acos(min(1.0, abs(float(delta[3]))))))


def signed_twist_deg(delta: np.ndarray | None, axis: np.ndarray) -> float:
    if delta is None:
        return nan()
    axis_norm = normalize_vec(axis.astype(float))
    if axis_norm is None:
        return nan()
    q = delta.astype(float)
    norm = float(np.linalg.norm(q))
    if norm <= 1e-9:
        return nan()
    q /= norm
    projected = axis_norm * float(np.dot(q[:3], axis_norm))
    twist = np.array([projected[0], projected[1], projected[2], q[3]], dtype=float)
    twist_norm = float(np.linalg.norm(twist))
    if twist_norm <= 1e-9:
        return nan()
    twist /= twist_norm
    angle = 2.0 * math.atan2(float(np.linalg.norm(twist[:3])), max(min(float(twist[3]), 1.0), -1.0))
    sign = 1.0 if float(np.dot(twist[:3], axis_norm)) >= 0.0 else -1.0
    return float(math.degrees(angle) * sign)


def iter_manny_items(path: Path, component: str) -> list[dict]:
    text = path.read_text(encoding="utf-8").lstrip()
    if text.startswith("{"):
        payload = json.loads(text)
        items = []
        for sample in payload.get("samples", []):
            source = sample if component == "flat" else sample.get(component, {})
            loc = {}
            rotations = {}
            quaternions = {}
            for bone, value in source.items():
                if isinstance(value, dict):
                    if "loc" in value:
                        loc[bone] = value["loc"]
                    if "rot" in value:
                        rotations[bone] = value["rot"]
                    if "quat" in value:
                        quaternions[bone] = value["quat"]
                    if "local_rot" in value:
                        rotations[f"{bone}_local"] = value["local_rot"]
                    if "local_quat" in value:
                        quaternions[f"{bone}_local"] = value["local_quat"]
            items.append({"t": sample.get("pose_t", sample.get("t", nan())), "loc": loc, "rot": rotations, "quat": quaternions})
        return items

    items = []
    with path.open("r", encoding="utf-8") as handle:
        for line in handle:
            if line.strip():
                item = json.loads(line)
                if "pose_t" in item:
                    item["t"] = item["pose_t"]
                items.append(item)
    return items


def analyze_manny(jsonl_path: Path, component: str) -> dict[str, np.ndarray]:
    rows: list[dict[str, float]] = []
    head_local_ref_q: np.ndarray | None = None
    neck02_local_ref_q: np.ndarray | None = None
    clav_l_local_ref_q: np.ndarray | None = None
    clav_r_local_ref_q: np.ndarray | None = None
    for item in iter_manny_items(jsonl_path, component):
            loc = item.get("loc", {})
            rotations = item.get("rot", {})
            quaternions = item.get("quat", {})
            pelvis = pos(loc, "pelvis")
            spine03 = pos(loc, "spine_03")
            neck01 = pos(loc, "neck_01")
            neck02 = pos(loc, "neck_02")
            head = pos(loc, "head")
            clav_l = pos(loc, "clavicle_l")
            clav_r = pos(loc, "clavicle_r")
            upper_l = pos(loc, "upperarm_l")
            upper_r = pos(loc, "upperarm_r")
            upper_twist01_l = pos(loc, "upperarm_twist_01_l")
            upper_twist01_r = pos(loc, "upperarm_twist_01_r")
            upper_twist02_l = pos(loc, "upperarm_twist_02_l")
            upper_twist02_r = pos(loc, "upperarm_twist_02_r")
            lower_l = pos(loc, "lowerarm_l")
            lower_r = pos(loc, "lowerarm_r")
            row = {
                "t": float(item.get("t", nan())),
                "shoulder_width": nan(),
                "left_shoulder_height_norm": nan(),
                "right_shoulder_height_norm": nan(),
                "left_shrug_clearance_norm": nan(),
                "right_shrug_clearance_norm": nan(),
                "bilateral_shrug_clearance_norm": nan(),
                "left_upperarm_twist01_shrug_clearance_norm": nan(),
                "right_upperarm_twist01_shrug_clearance_norm": nan(),
                "bilateral_upperarm_twist01_shrug_clearance_norm": nan(),
                "left_upperarm_twist02_shrug_clearance_norm": nan(),
                "right_upperarm_twist02_shrug_clearance_norm": nan(),
                "bilateral_upperarm_twist02_shrug_clearance_norm": nan(),
                "left_shoulder_mid_up_norm": nan(),
                "right_shoulder_mid_up_norm": nan(),
                "shoulder_asym_norm": nan(),
                "shoulder_roll_deg": nan(),
                "left_clavicle_mid_up_norm": nan(),
                "right_clavicle_mid_up_norm": nan(),
                "clavicle_root_asym_norm": nan(),
                "left_clavicle_endpoint_angle_deg": nan(),
                "right_clavicle_endpoint_angle_deg": nan(),
                "left_upperarm_down_angle_deg": nan(),
                "right_upperarm_down_angle_deg": nan(),
                "left_elbow_height_norm": nan(),
                "right_elbow_height_norm": nan(),
                "head_x_norm": nan(),
                "head_y_norm": nan(),
                "head_segment_x_norm": nan(),
                "head_lateral_angle_deg": nan(),
                "neck_chain_lateral_angle_deg": nan(),
                "head_pitch": nan(),
                "head_yaw": nan(),
                "head_roll": nan(),
                "head_local_pitch": nan(),
                "head_local_yaw": nan(),
                "head_local_roll": nan(),
                "head_local_parent_angle_deg": nan(),
                "head_chest_twist_x_deg": nan(),
                "head_chest_twist_y_deg": nan(),
                "head_chest_twist_z_deg": nan(),
                "head_chest_raw_twist_z_deg": nan(),
                "head_chest_chin_pitch_deg": nan(),
                "neck02_local_parent_angle_deg": nan(),
                "left_clavicle_local_pitch": nan(),
                "left_clavicle_local_yaw": nan(),
                "left_clavicle_local_roll": nan(),
                "left_clavicle_local_parent_angle_deg": nan(),
                "right_clavicle_local_pitch": nan(),
                "right_clavicle_local_yaw": nan(),
                "right_clavicle_local_roll": nan(),
                "right_clavicle_local_parent_angle_deg": nan(),
            }
            if upper_l is not None and upper_r is not None:
                shoulder_width = float(abs(upper_l[0] - upper_r[0]))
                if shoulder_width > 1e-6:
                    smid = (upper_l + upper_r) * 0.5
                    row["shoulder_width"] = shoulder_width
                    if pelvis is not None:
                        row["left_shoulder_height_norm"] = float((upper_l[2] - pelvis[2]) / shoulder_width)
                        row["right_shoulder_height_norm"] = float((upper_r[2] - pelvis[2]) / shoulder_width)
                    row["left_shoulder_mid_up_norm"] = float((upper_l[2] - smid[2]) / shoulder_width)
                    row["right_shoulder_mid_up_norm"] = float((upper_r[2] - smid[2]) / shoulder_width)
                    row["shoulder_asym_norm"] = float((upper_l[2] - upper_r[2]) / shoulder_width)
                    row["shoulder_roll_deg"] = float(math.degrees(math.atan2(upper_l[2] - upper_r[2], abs(upper_l[0] - upper_r[0]))))
                    if lower_l is not None:
                        row["left_upperarm_down_angle_deg"] = angle_from_down_deg(lower_l[0] - upper_l[0], upper_l[2] - lower_l[2])
                        row["left_elbow_height_norm"] = float((lower_l[2] - smid[2]) / shoulder_width)
                    if lower_r is not None:
                        row["right_upperarm_down_angle_deg"] = angle_from_down_deg(lower_r[0] - upper_r[0], upper_r[2] - lower_r[2])
                        row["right_elbow_height_norm"] = float((lower_r[2] - smid[2]) / shoulder_width)
                    if head is not None:
                        row["head_x_norm"] = float((head[0] - smid[0]) / shoulder_width)
                        row["head_y_norm"] = float((head[2] - smid[2]) / shoulder_width)
                        row["left_shrug_clearance_norm"] = float(-(head[2] - upper_l[2]) / shoulder_width)
                        row["right_shrug_clearance_norm"] = float(-(head[2] - upper_r[2]) / shoulder_width)
                        row["bilateral_shrug_clearance_norm"] = float(
                            -(((head[2] - upper_l[2]) + (head[2] - upper_r[2])) * 0.5) / shoulder_width
                        )
                        row["left_head_center_clearance_norm"] = row["left_shrug_clearance_norm"]
                        row["right_head_center_clearance_norm"] = row["right_shrug_clearance_norm"]
                        row["bilateral_head_center_clearance_norm"] = row["bilateral_shrug_clearance_norm"]
                        if upper_twist01_l is not None and upper_twist01_r is not None:
                            row["left_upperarm_twist01_shrug_clearance_norm"] = float(-(head[2] - upper_twist01_l[2]) / shoulder_width)
                            row["right_upperarm_twist01_shrug_clearance_norm"] = float(-(head[2] - upper_twist01_r[2]) / shoulder_width)
                            row["bilateral_upperarm_twist01_shrug_clearance_norm"] = float(
                                -(((head[2] - upper_twist01_l[2]) + (head[2] - upper_twist01_r[2])) * 0.5) / shoulder_width
                            )
                        if upper_twist02_l is not None and upper_twist02_r is not None:
                            row["left_upperarm_twist02_shrug_clearance_norm"] = float(-(head[2] - upper_twist02_l[2]) / shoulder_width)
                            row["right_upperarm_twist02_shrug_clearance_norm"] = float(-(head[2] - upper_twist02_r[2]) / shoulder_width)
                            row["bilateral_upperarm_twist02_shrug_clearance_norm"] = float(
                                -(((head[2] - upper_twist02_l[2]) + (head[2] - upper_twist02_r[2])) * 0.5) / shoulder_width
                            )
                    if head is not None and neck02 is not None:
                        row["head_segment_x_norm"] = float((head[0] - neck02[0]) / shoulder_width)
                        row["head_lateral_angle_deg"] = angle_from_vertical_deg(head[0] - neck02[0], head[2] - neck02[2])
                    if head is not None and spine03 is not None:
                        row["neck_chain_lateral_angle_deg"] = angle_from_vertical_deg(head[0] - spine03[0], head[2] - spine03[2])
                    if clav_l is not None and clav_r is not None:
                        cmid = (clav_l + clav_r) * 0.5
                        row["left_clavicle_mid_up_norm"] = float((clav_l[2] - cmid[2]) / shoulder_width)
                        row["right_clavicle_mid_up_norm"] = float((clav_r[2] - cmid[2]) / shoulder_width)
                        row["clavicle_root_asym_norm"] = float((clav_l[2] - clav_r[2]) / shoulder_width)
                    if clav_l is not None:
                        row["left_clavicle_endpoint_angle_deg"] = float(
                            math.degrees(math.atan2(upper_l[2] - clav_l[2], abs(upper_l[0] - clav_l[0])))
                        )
                    if clav_r is not None:
                        row["right_clavicle_endpoint_angle_deg"] = float(
                            math.degrees(math.atan2(upper_r[2] - clav_r[2], abs(upper_r[0] - clav_r[0])))
                        )
            head_rot = rot(rotations, "head")
            if head_rot is not None:
                row["head_pitch"], row["head_yaw"], row["head_roll"] = head_rot
            head_local_rot = rot(rotations, "head_local")
            if head_local_rot is not None:
                row["head_local_pitch"], row["head_local_yaw"], row["head_local_roll"] = head_local_rot
            neck_local_rot = rot(rotations, "neck_01_local")
            if neck_local_rot is not None:
                row["neck01_local_pitch"], row["neck01_local_yaw"], row["neck01_local_roll"] = neck_local_rot
            neck02_local_rot = rot(rotations, "neck_02_local")
            if neck02_local_rot is not None:
                row["neck02_local_pitch"], row["neck02_local_yaw"], row["neck02_local_roll"] = neck02_local_rot
            head_local_q = quat_values(quaternions, "head_local")
            if head_local_q is not None:
                if head_local_ref_q is None:
                    head_local_ref_q = head_local_q
                row["head_local_parent_angle_deg"] = quat_delta_angle_deg(head_local_q, head_local_ref_q)
            head_comp_q = quat_values(quaternions, "head")
            chest_comp_q = quat_values(quaternions, "spine_05")
            if chest_comp_q is None:
                chest_comp_q = quat_values(quaternions, "spine_03")
            if head_comp_q is not None and chest_comp_q is not None:
                head_chest_delta = quat_mul(head_comp_q, quat_inverse(chest_comp_q))
                row["head_chest_twist_x_deg"] = signed_twist_deg(head_chest_delta, np.array([1.0, 0.0, 0.0], dtype=float))
                row["head_chest_twist_y_deg"] = signed_twist_deg(head_chest_delta, np.array([0.0, 1.0, 0.0], dtype=float))
                row["head_chest_twist_z_deg"] = signed_twist_deg(head_chest_delta, np.array([0.0, 0.0, 1.0], dtype=float))
                raw_twist_z = signed_twist_deg(head_chest_delta, np.array([0.0, 0.0, 1.0], dtype=float))
                row["head_chest_raw_twist_z_deg"] = raw_twist_z
                row["head_chest_chin_pitch_deg"] = -raw_twist_z if math.isfinite(raw_twist_z) else nan()
            neck02_local_q = quat_values(quaternions, "neck_02_local")
            if neck02_local_q is not None:
                if neck02_local_ref_q is None:
                    neck02_local_ref_q = neck02_local_q
                row["neck02_local_parent_angle_deg"] = quat_delta_angle_deg(neck02_local_q, neck02_local_ref_q)
            clav_l_local_rot = rot(rotations, "clavicle_l_local")
            if clav_l_local_rot is not None:
                row["left_clavicle_local_pitch"], row["left_clavicle_local_yaw"], row["left_clavicle_local_roll"] = clav_l_local_rot
            clav_l_local_q = quat_values(quaternions, "clavicle_l_local")
            if clav_l_local_q is not None:
                if clav_l_local_ref_q is None:
                    clav_l_local_ref_q = clav_l_local_q
                row["left_clavicle_local_parent_angle_deg"] = quat_delta_angle_deg(clav_l_local_q, clav_l_local_ref_q)
            clav_r_local_rot = rot(rotations, "clavicle_r_local")
            if clav_r_local_rot is not None:
                row["right_clavicle_local_pitch"], row["right_clavicle_local_yaw"], row["right_clavicle_local_roll"] = clav_r_local_rot
            clav_r_local_q = quat_values(quaternions, "clavicle_r_local")
            if clav_r_local_q is not None:
                if clav_r_local_ref_q is None:
                    clav_r_local_ref_q = clav_r_local_q
                row["right_clavicle_local_parent_angle_deg"] = quat_delta_angle_deg(clav_r_local_q, clav_r_local_ref_q)
            rows.append(row)
    if not rows:
        raise RuntimeError(f"No Manny rows in {jsonl_path}")
    data = as_arrays(rows)
    add_delta_signals(data)
    return data


def interp_signal(data: dict[str, np.ndarray], key: str, times: np.ndarray) -> np.ndarray:
    t = data["t"]
    values = data[key]
    mask = np.isfinite(t) & np.isfinite(values)
    if mask.sum() < 2:
        return np.full_like(times, nan(), dtype=float)
    return np.interp(times, t[mask], values[mask], left=nan(), right=nan())


def pair_stats(vp2: dict[str, np.ndarray], manny: dict[str, np.ndarray], vp2_key: str, manny_key: str, start: float, end: float, shift: float) -> dict[str, float]:
    times = vp2["t"]
    mask = (times >= start) & (times <= end) & np.isfinite(vp2[vp2_key])
    x = vp2[vp2_key][mask]
    y = interp_signal(manny, manny_key, times[mask] + shift)
    valid = np.isfinite(x) & np.isfinite(y)
    if valid.sum() < 8:
        return {"n": int(valid.sum()), "corr": nan(), "corr_flipped": nan(), "gain_manny_per_vp2": nan(), "bias": nan(), "rmse": nan()}
    x = x[valid]
    y = y[valid]
    corr = float(np.corrcoef(x, y)[0, 1]) if np.std(x) > 1e-9 and np.std(y) > 1e-9 else nan()
    gain, bias = np.polyfit(x, y, 1) if np.std(x) > 1e-9 else (nan(), nan())
    pred = gain * x + bias if math.isfinite(gain) else np.full_like(y, nan())
    rmse = float(np.sqrt(np.mean((pred - y) ** 2))) if np.isfinite(pred).all() else nan()
    return {
        "n": int(valid.sum()),
        "corr": corr,
        "corr_flipped": -corr if math.isfinite(corr) else nan(),
        "gain_manny_per_vp2": float(gain),
        "bias": float(bias),
        "rmse_after_linear_fit": rmse,
        "vp2_range": pct_range(x)["range_p95_p05"],
        "manny_range": pct_range(y)["range_p95_p05"],
    }


SIGNAL_PAIRS = [
    ("bilateral_head_center_clearance_norm_delta", "bilateral_head_center_clearance_norm_delta"),
    ("left_head_center_clearance_norm_delta", "left_head_center_clearance_norm_delta"),
    ("right_head_center_clearance_norm_delta", "right_head_center_clearance_norm_delta"),
    ("bilateral_head_center_clearance_world_norm_delta", "bilateral_head_center_clearance_norm_delta"),
    ("left_head_center_clearance_world_norm_delta", "left_head_center_clearance_norm_delta"),
    ("right_head_center_clearance_world_norm_delta", "right_head_center_clearance_norm_delta"),
    ("bilateral_shrug_clearance_norm_delta", "bilateral_shrug_clearance_norm_delta"),
    ("bilateral_shrug_clearance_world_norm_delta", "bilateral_shrug_clearance_norm_delta"),
    ("bilateral_shrug_clearance_norm_delta", "bilateral_upperarm_twist01_shrug_clearance_norm_delta"),
    ("bilateral_shrug_clearance_world_norm_delta", "bilateral_upperarm_twist01_shrug_clearance_norm_delta"),
    ("bilateral_shrug_clearance_norm_delta", "bilateral_upperarm_twist02_shrug_clearance_norm_delta"),
    ("bilateral_shrug_clearance_world_norm_delta", "bilateral_upperarm_twist02_shrug_clearance_norm_delta"),
    ("left_shrug_clearance_norm_delta", "left_shrug_clearance_norm_delta"),
    ("left_shrug_clearance_world_norm_delta", "left_shrug_clearance_norm_delta"),
    ("left_shrug_clearance_norm_delta", "left_upperarm_twist01_shrug_clearance_norm_delta"),
    ("left_shrug_clearance_norm_delta", "left_upperarm_twist02_shrug_clearance_norm_delta"),
    ("right_shrug_clearance_norm_delta", "right_shrug_clearance_norm_delta"),
    ("right_shrug_clearance_world_norm_delta", "right_shrug_clearance_norm_delta"),
    ("right_shrug_clearance_norm_delta", "right_upperarm_twist01_shrug_clearance_norm_delta"),
    ("right_shrug_clearance_norm_delta", "right_upperarm_twist02_shrug_clearance_norm_delta"),
    ("left_shoulder_height_norm_delta", "left_shoulder_height_norm_delta"),
    ("right_shoulder_height_norm_delta", "right_shoulder_height_norm_delta"),
    ("left_shoulder_mid_up_norm_delta", "left_shoulder_mid_up_norm_delta"),
    ("right_shoulder_mid_up_norm_delta", "right_shoulder_mid_up_norm_delta"),
    ("shoulder_asym_norm_delta", "shoulder_asym_norm_delta"),
    ("shoulder_roll_deg_delta", "shoulder_roll_deg_delta"),
    ("left_upperarm_down_angle_deg_delta", "left_upperarm_down_angle_deg_delta"),
    ("right_upperarm_down_angle_deg_delta", "right_upperarm_down_angle_deg_delta"),
    ("head_x_norm_delta", "head_x_norm_delta"),
    ("head_x_norm_delta", "head_segment_x_norm_delta"),
    ("head_lateral_angle_deg_delta", "head_lateral_angle_deg_delta"),
    ("head_lateral_angle_deg_delta", "neck_chain_lateral_angle_deg_delta"),
    ("head_lateral_angle_deg_delta", "head_yaw_delta"),
    ("head_lateral_angle_deg_delta", "head_local_yaw_delta"),
    ("head_lateral_angle_deg_delta", "head_local_roll_delta"),
    ("head_lateral_angle_deg_delta", "head_local_parent_angle_deg_delta"),
    ("head_lateral_angle_deg_delta", "neck02_local_parent_angle_deg_delta"),
    ("head_y_norm_delta", "head_local_pitch_delta"),
    ("mouth_y_norm_delta", "head_local_pitch_delta"),
    ("mouth_ear_y_norm_delta", "head_local_pitch_delta"),
    ("mouth_ear_y_norm_delta", "neck02_local_parent_angle_deg_delta"),
    ("mouth_ear_up_world_norm_delta", "head_local_pitch_delta"),
    ("mouth_ear_up_world_norm_delta", "neck02_local_parent_angle_deg_delta"),
    ("nose_ear_y_norm_delta", "head_local_pitch_delta"),
    ("nose_ear_y_norm_delta", "neck02_local_parent_angle_deg_delta"),
    ("nose_ear_up_world_norm_delta", "head_local_pitch_delta"),
    ("nose_ear_up_world_norm_delta", "neck02_local_parent_angle_deg_delta"),
    ("runtime_screen_head_pitch_est_deg_delta", "head_local_pitch_delta"),
    ("runtime_screen_head_pitch_est_deg_delta", "head_local_roll_delta"),
    ("runtime_screen_head_pitch_est_deg_delta", "head_local_yaw_delta"),
    ("runtime_screen_head_pitch_est_deg_delta", "neck02_local_yaw_delta"),
    ("runtime_screen_head_pitch_est_deg_delta", "head_chest_chin_pitch_deg_delta"),
    ("runtime_screen_head_pitch_est_deg_delta", "head_chest_twist_x_deg_delta"),
    ("runtime_screen_head_pitch_est_deg_delta", "head_chest_twist_y_deg_delta"),
    ("runtime_screen_head_pitch_est_deg_delta", "head_chest_twist_z_deg_delta"),
    ("runtime_screen_head_pitch_est_deg_delta", "head_local_parent_angle_deg_delta"),
    ("runtime_screen_head_pitch_est_deg_delta", "neck02_local_parent_angle_deg_delta"),
    ("runtime_screen_head_pitch_face_only_est_deg_delta", "head_local_pitch_delta"),
    ("runtime_screen_head_pitch_face_only_est_deg_delta", "head_local_roll_delta"),
    ("runtime_screen_head_pitch_face_only_est_deg_delta", "head_local_yaw_delta"),
    ("runtime_screen_head_pitch_face_only_est_deg_delta", "neck02_local_yaw_delta"),
    ("runtime_screen_head_pitch_face_only_est_deg_delta", "head_local_parent_angle_deg_delta"),
    ("runtime_screen_head_pitch_face_only_est_deg_delta", "neck02_local_parent_angle_deg_delta"),
    ("runtime_screen_head_pitch_clamped_est_deg_delta", "head_local_pitch_delta"),
    ("runtime_screen_head_pitch_clamped_est_deg_delta", "head_local_roll_delta"),
    ("runtime_screen_head_pitch_clamped_est_deg_delta", "head_local_yaw_delta"),
    ("runtime_screen_head_pitch_clamped_est_deg_delta", "neck02_local_yaw_delta"),
    ("runtime_screen_head_pitch_clamped_est_deg_delta", "head_local_parent_angle_deg_delta"),
    ("runtime_screen_head_pitch_clamped_est_deg_delta", "neck02_local_parent_angle_deg_delta"),
    ("static_screen_head_pitch_est_deg_delta", "head_local_pitch_delta"),
    ("static_screen_head_pitch_est_deg_delta", "head_local_roll_delta"),
    ("static_screen_head_pitch_est_deg_delta", "head_local_yaw_delta"),
    ("static_screen_head_pitch_est_deg_delta", "neck02_local_yaw_delta"),
    ("static_screen_head_pitch_est_deg_delta", "head_local_parent_angle_deg_delta"),
    ("static_screen_head_pitch_est_deg_delta", "neck02_local_parent_angle_deg_delta"),
    ("static_screen_head_pitch_face_only_est_deg_delta", "head_local_pitch_delta"),
    ("static_screen_head_pitch_face_only_est_deg_delta", "head_local_roll_delta"),
    ("static_screen_head_pitch_face_only_est_deg_delta", "head_local_yaw_delta"),
    ("static_screen_head_pitch_face_only_est_deg_delta", "neck02_local_yaw_delta"),
    ("static_screen_head_pitch_face_only_est_deg_delta", "head_local_parent_angle_deg_delta"),
    ("static_screen_head_pitch_face_only_est_deg_delta", "neck02_local_parent_angle_deg_delta"),
    ("runtime_face_pitch_input_norm_delta", "head_local_roll_delta"),
    ("runtime_face_pitch_input_norm_delta", "head_local_yaw_delta"),
    ("runtime_face_pitch_input_norm_delta", "neck02_local_yaw_delta"),
    ("runtime_face_pitch_input_norm_delta", "head_chest_chin_pitch_deg_delta"),
    ("runtime_face_pitch_input_norm_delta", "head_local_parent_angle_deg_delta"),
    ("runtime_face_pitch_input_norm_delta", "neck02_local_parent_angle_deg_delta"),
    ("face_pitch_consensus_y_norm_delta", "head_local_pitch_delta"),
    ("face_pitch_consensus_y_norm_delta", "head_local_roll_delta"),
    ("face_pitch_consensus_y_norm_delta", "head_local_yaw_delta"),
    ("face_pitch_consensus_y_norm_delta", "neck02_local_yaw_delta"),
    ("face_pitch_consensus_y_norm_delta", "head_chest_chin_pitch_deg_delta"),
    ("face_pitch_consensus_y_norm_delta", "head_local_parent_angle_deg_delta"),
    ("face_pitch_consensus_y_norm_delta", "neck02_local_parent_angle_deg_delta"),
    ("mouth_eye_y_norm_delta", "head_local_pitch_delta"),
    ("mouth_eye_y_norm_delta", "head_local_roll_delta"),
    ("mouth_eye_y_norm_delta", "head_local_yaw_delta"),
    ("mouth_eye_y_norm_delta", "neck02_local_yaw_delta"),
    ("mouth_eye_y_norm_delta", "head_local_parent_angle_deg_delta"),
    ("mouth_eye_y_norm_delta", "neck02_local_parent_angle_deg_delta"),
    ("mouth_eye_up_world_norm_delta", "head_local_pitch_delta"),
    ("mouth_eye_up_world_norm_delta", "head_local_roll_delta"),
    ("mouth_eye_up_world_norm_delta", "head_local_yaw_delta"),
    ("mouth_eye_up_world_norm_delta", "neck02_local_yaw_delta"),
    ("mouth_eye_up_world_norm_delta", "head_chest_chin_pitch_deg_delta"),
    ("mouth_eye_up_world_norm_delta", "neck02_local_parent_angle_deg_delta"),
    ("nose_eye_y_norm_delta", "head_local_pitch_delta"),
    ("nose_eye_y_norm_delta", "head_local_roll_delta"),
    ("nose_eye_y_norm_delta", "head_local_yaw_delta"),
    ("nose_eye_y_norm_delta", "neck02_local_yaw_delta"),
    ("nose_eye_y_norm_delta", "head_local_parent_angle_deg_delta"),
    ("nose_eye_y_norm_delta", "neck02_local_parent_angle_deg_delta"),
    ("nose_eye_up_world_norm_delta", "head_local_pitch_delta"),
    ("nose_eye_up_world_norm_delta", "neck02_local_parent_angle_deg_delta"),
    ("head_forward_pitch_world_deg_delta", "head_local_pitch_delta"),
    ("head_forward_pitch_world_deg_delta", "head_local_yaw_delta"),
    ("head_forward_pitch_world_deg_delta", "neck02_local_yaw_delta"),
    ("head_forward_pitch_world_deg_delta", "head_chest_chin_pitch_deg_delta"),
    ("head_forward_pitch_world_deg_delta", "neck02_local_parent_angle_deg_delta"),
    ("face_mesh_chin_eye_y_norm_delta", "head_local_pitch_delta"),
    ("face_mesh_chin_eye_y_norm_delta", "head_chest_chin_pitch_deg_delta"),
    ("face_mesh_chin_eye_y_norm_delta", "head_local_parent_angle_deg_delta"),
    ("face_mesh_chin_nose_y_norm_delta", "head_local_pitch_delta"),
    ("face_mesh_chin_nose_y_norm_delta", "head_chest_chin_pitch_deg_delta"),
    ("face_mesh_chin_nose_y_norm_delta", "head_local_parent_angle_deg_delta"),
    ("face_mesh_chin_mouth_y_norm_delta", "head_local_pitch_delta"),
    ("face_mesh_chin_mouth_y_norm_delta", "head_chest_chin_pitch_deg_delta"),
    ("face_mesh_mouth_eye_y_norm_delta", "head_local_pitch_delta"),
    ("face_mesh_mouth_eye_y_norm_delta", "head_chest_chin_pitch_deg_delta"),
    ("face_mesh_nose_eye_y_norm_delta", "head_local_pitch_delta"),
    ("face_mesh_nose_eye_y_norm_delta", "head_chest_chin_pitch_deg_delta"),
    ("face_mesh_chin_eye_z_norm_delta", "head_local_pitch_delta"),
    ("face_mesh_chin_eye_z_norm_delta", "head_chest_chin_pitch_deg_delta"),
    ("face_mesh_chin_nose_z_norm_delta", "head_local_pitch_delta"),
    ("face_mesh_chin_nose_z_norm_delta", "head_chest_chin_pitch_deg_delta"),
    ("face_mesh_chin_mouth_z_norm_delta", "head_local_pitch_delta"),
    ("face_mesh_chin_mouth_z_norm_delta", "head_chest_chin_pitch_deg_delta"),
    ("face_mesh_mouth_eye_z_norm_delta", "head_local_pitch_delta"),
    ("face_mesh_mouth_eye_z_norm_delta", "head_chest_chin_pitch_deg_delta"),
    ("face_mesh_nose_eye_z_norm_delta", "head_local_pitch_delta"),
    ("face_mesh_nose_eye_z_norm_delta", "head_chest_chin_pitch_deg_delta"),
    ("face_mesh_chin_eye_yz_angle_deg_delta", "head_local_pitch_delta"),
    ("face_mesh_chin_eye_yz_angle_deg_delta", "head_chest_chin_pitch_deg_delta"),
    ("face_mesh_chin_nose_yz_angle_deg_delta", "head_local_pitch_delta"),
    ("face_mesh_chin_nose_yz_angle_deg_delta", "head_chest_chin_pitch_deg_delta"),
    ("face_mesh_chin_mouth_yz_angle_deg_delta", "head_local_pitch_delta"),
    ("face_mesh_chin_mouth_yz_angle_deg_delta", "head_chest_chin_pitch_deg_delta"),
    ("face_mesh_pitch_x_deg_delta", "head_local_pitch_delta"),
    ("face_mesh_pitch_x_deg_delta", "head_chest_chin_pitch_deg_delta"),
    ("face_mesh_pitch_x_deg_delta", "head_local_parent_angle_deg_delta"),
    ("face_mesh_pitch_y_deg_delta", "head_local_yaw_delta"),
    ("face_mesh_pitch_z_deg_delta", "head_local_roll_delta"),
    ("nose_from_ear_mid_x_norm_delta", "head_yaw_delta"),
    ("nose_from_ear_mid_x_norm_delta", "head_local_yaw_delta"),
    ("ear_roll_deg_delta", "head_roll_delta"),
    ("ear_roll_deg_delta", "head_local_roll_delta"),
    ("shoulder_asym_norm_delta", "left_clavicle_local_parent_angle_deg_delta"),
    ("shoulder_asym_norm_delta", "right_clavicle_local_parent_angle_deg_delta"),
]


def auto_time_shift(vp2: dict[str, np.ndarray], manny: dict[str, np.ndarray], start: float, end: float) -> float:
    candidate_pairs = [
        ("head_lateral_angle_deg_delta", "head_lateral_angle_deg_delta"),
        ("face_pitch_consensus_y_norm_delta", "head_local_yaw_delta"),
        ("face_pitch_consensus_y_norm_delta", "neck02_local_yaw_delta"),
        ("bilateral_shrug_clearance_norm_delta", "bilateral_upperarm_twist01_shrug_clearance_norm_delta"),
        ("bilateral_shrug_clearance_norm_delta", "bilateral_upperarm_twist02_shrug_clearance_norm_delta"),
        ("left_shrug_clearance_norm_delta", "left_upperarm_twist01_shrug_clearance_norm_delta"),
        ("right_shrug_clearance_norm_delta", "right_upperarm_twist01_shrug_clearance_norm_delta"),
    ]
    best_shift = 0.0
    best_score = -1.0
    for shift in np.linspace(-1.0, 5.0, 361):
        scores = []
        for a, b in candidate_pairs:
            if a not in vp2 or b not in manny:
                continue
            stats = pair_stats(vp2, manny, a, b, start, end, float(shift))
            corr = stats["corr"]
            if math.isfinite(corr):
                scores.append(abs(corr))
        if scores:
            score = float(np.mean(scores))
            if score > best_score:
                best_score = score
                best_shift = float(shift)
    return best_shift


def best_pair_shift(
    vp2: dict[str, np.ndarray],
    manny: dict[str, np.ndarray],
    vp2_key: str,
    manny_key: str,
    start: float,
    end: float,
    center_shift: float,
) -> dict[str, float]:
    best: dict[str, float] | None = None
    for shift in np.linspace(center_shift - 0.5, center_shift + 0.5, 121):
        stats = pair_stats(vp2, manny, vp2_key, manny_key, start, end, float(shift))
        corr = stats.get("corr", nan())
        if not math.isfinite(corr):
            continue
        score = abs(corr)
        if best is None or score > best["abs_corr"]:
            best = {
                "best_shift_s": float(shift),
                "corr": corr,
                "abs_corr": score,
                "gain_manny_per_vp2": stats.get("gain_manny_per_vp2", nan()),
                "vp2_range": stats.get("vp2_range", nan()),
                "manny_range": stats.get("manny_range", nan()),
            }
    if best is None:
        return {"best_shift_s": nan(), "corr": nan(), "abs_corr": nan(), "gain_manny_per_vp2": nan(), "vp2_range": nan(), "manny_range": nan()}
    return best


def robust_standardize(values: np.ndarray, fit_mask: np.ndarray) -> np.ndarray:
    out = values.astype(float).copy()
    fit_values = finite(out[fit_mask])
    if fit_values.size < 3:
        return out
    center = float(np.median(fit_values))
    p05 = float(np.percentile(fit_values, 5))
    p95 = float(np.percentile(fit_values, 95))
    scale = p95 - p05
    if not math.isfinite(scale) or abs(scale) < 1e-6:
        scale = float(np.std(fit_values))
    if not math.isfinite(scale) or abs(scale) < 1e-6:
        return out - center
    return (out - center) / scale


def plot_values(data: dict[str, np.ndarray], key: str, visible_t: np.ndarray, start: float, end: float, mode: str) -> np.ndarray:
    values = data[key]
    if mode != "standardized":
        return values
    fit_mask = (visible_t >= start) & (visible_t <= end) & np.isfinite(values)
    return robust_standardize(values, fit_mask)


def plot_pair(
    ax: plt.Axes,
    vp2: dict[str, np.ndarray],
    manny: dict[str, np.ndarray],
    vp2_key: str,
    manny_keys: list[str],
    shift: float,
    start: float,
    end: float,
    plot_mode: str,
    title: str,
    ylabel: str,
) -> None:
    vp2_visible_t = vp2["t"]
    ax.plot(vp2_visible_t, plot_values(vp2, vp2_key, vp2_visible_t, start, end, plot_mode), label=f"VP2 {vp2_key}", linewidth=1.8)
    for key in manny_keys:
        if key in manny:
            manny_visible_t = manny["t"] - shift
            ax.plot(
                manny_visible_t,
                plot_values(manny, key, manny_visible_t, start, end, plot_mode),
                label=f"Manny {key}",
                linewidth=1.5,
                alpha=0.9,
            )
    ax.set_title(title)
    ax.set_ylabel("standardized" if plot_mode == "standardized" else ylabel)
    ax.set_xlim(start, end)
    ax.grid(True, alpha=0.25)
    ax.legend(loc="upper right", fontsize=8)


def plot_multi_pair(
    ax: plt.Axes,
    vp2: dict[str, np.ndarray],
    manny: dict[str, np.ndarray],
    vp2_keys: list[str],
    manny_keys: list[str],
    shift: float,
    start: float,
    end: float,
    plot_mode: str,
    title: str,
    ylabel: str,
) -> None:
    vp2_visible_t = vp2["t"]
    for key in vp2_keys:
        if key in vp2:
            ax.plot(
                vp2_visible_t,
                plot_values(vp2, key, vp2_visible_t, start, end, plot_mode),
                label=f"VP2 {key}",
                linewidth=1.8,
            )
    for key in manny_keys:
        if key in manny:
            manny_visible_t = manny["t"] - shift
            ax.plot(
                manny_visible_t,
                plot_values(manny, key, manny_visible_t, start, end, plot_mode),
                label=f"Manny {key}",
                linewidth=1.5,
                alpha=0.9,
            )
    ax.set_title(title)
    ax.set_ylabel("standardized" if plot_mode == "standardized" else ylabel)
    ax.set_xlim(start, end)
    ax.grid(True, alpha=0.25)
    ax.legend(loc="upper right", fontsize=8)


def resampled_standardized_pair(
    vp2: dict[str, np.ndarray],
    manny: dict[str, np.ndarray],
    vp2_key: str,
    manny_key: str,
    shift: float,
    start: float,
    end: float,
    sample_hz: float = 60.0,
) -> tuple[np.ndarray, np.ndarray, np.ndarray]:
    step = 1.0 / sample_hz
    times = np.arange(start, end, step, dtype=float)
    if times.size < 8:
        return times, np.full_like(times, nan()), np.full_like(times, nan())
    vp2_values = interp_signal(vp2, vp2_key, times)
    manny_values = interp_signal(manny, manny_key, times + shift)
    valid = np.isfinite(vp2_values) & np.isfinite(manny_values)
    fit_mask = valid.copy()
    vp2_std = robust_standardize(vp2_values, fit_mask)
    manny_std = robust_standardize(manny_values, fit_mask)
    vp2_std[~valid] = nan()
    manny_std[~valid] = nan()
    return times, vp2_std, manny_std


def fill_missing_linear(values: np.ndarray) -> np.ndarray | None:
    x = np.arange(values.size, dtype=float)
    mask = np.isfinite(values)
    if mask.sum() < 8:
        return None
    out = values.astype(float).copy()
    out[~mask] = np.interp(x[~mask], x[mask], out[mask])
    return out


def remove_linear_trend(values: np.ndarray) -> np.ndarray:
    x = np.arange(values.size, dtype=float)
    coeff = np.polyfit(x, values, 1)
    return values - (coeff[0] * x + coeff[1])


def spectral_pair_stats(
    vp2: dict[str, np.ndarray],
    manny: dict[str, np.ndarray],
    vp2_key: str,
    manny_key: str,
    shift: float,
    start: float,
    end: float,
    sample_hz: float = 60.0,
) -> dict[str, float]:
    times, x_raw, y_raw = resampled_standardized_pair(vp2, manny, vp2_key, manny_key, shift, start, end, sample_hz)
    x = fill_missing_linear(x_raw)
    y = fill_missing_linear(y_raw)
    if x is None or y is None or times.size < 16:
        return {"n": int(np.isfinite(x_raw).sum()), "dominant_frequency_hz": nan(), "fft_corr": nan()}
    x = remove_linear_trend(x)
    y = remove_linear_trend(y)
    if np.std(x) <= 1e-9 or np.std(y) <= 1e-9:
        return {"n": int(times.size), "dominant_frequency_hz": nan(), "fft_corr": nan()}

    window = np.hanning(times.size)
    freqs = np.fft.rfftfreq(times.size, d=1.0 / sample_hz)
    x_fft = np.fft.rfft(x * window)
    y_fft = np.fft.rfft(y * window)
    x_amp = np.abs(x_fft)
    y_amp = np.abs(y_fft)
    band = (freqs >= 0.5) & (freqs <= 4.0)
    if not np.any(band):
        return {"n": int(times.size), "dominant_frequency_hz": nan(), "fft_corr": nan()}

    band_indices = np.flatnonzero(band)
    dominant_index = int(band_indices[np.argmax(x_amp[band])])
    dominant_frequency = float(freqs[dominant_index])
    vp2_amp = float(x_amp[dominant_index])
    manny_amp = float(y_amp[dominant_index])
    amp_ratio = manny_amp / vp2_amp if vp2_amp > 1e-9 else nan()
    phase_delta = float(np.angle(y_fft[dominant_index] * np.conj(x_fft[dominant_index])))
    phase_deg = math.degrees(phase_delta)
    lag_s = phase_delta / (2.0 * math.pi * dominant_frequency) if dominant_frequency > 1e-9 else nan()
    x_power = float(np.sum(np.square(x_amp[band])))
    y_power = float(np.sum(np.square(y_amp[band])))
    power_ratio = y_power / x_power if x_power > 1e-9 else nan()
    amp_corr = float(np.corrcoef(x_amp[band], y_amp[band])[0, 1]) if np.std(x_amp[band]) > 1e-9 and np.std(y_amp[band]) > 1e-9 else nan()
    time_corr = float(np.corrcoef(x, y)[0, 1])
    return {
        "n": int(times.size),
        "dominant_frequency_hz": dominant_frequency,
        "vp2_amp_at_dominant": vp2_amp,
        "manny_amp_at_vp2_dominant": manny_amp,
        "amp_ratio_at_dominant": float(amp_ratio),
        "band_power_ratio_manny_over_vp2": float(power_ratio),
        "phase_delta_deg_manny_vs_vp2": float(phase_deg),
        "phase_lag_s_manny_vs_vp2": float(lag_s),
        "amplitude_spectrum_corr": amp_corr,
        "time_corr_after_standardize": time_corr,
    }


def plot_spectral_focus(
    out_prefix: Path,
    vp2: dict[str, np.ndarray],
    manny: dict[str, np.ndarray],
    shift: float,
    start: float,
    end: float,
) -> tuple[Path, dict[str, dict[str, float]]]:
    pairs = [
        ("face_mesh_mouth_eye_y_norm_delta", "head_chest_chin_pitch_deg_delta"),
        ("face_mesh_chin_eye_y_norm_delta", "head_chest_chin_pitch_deg_delta"),
        ("face_mesh_chin_eye_z_norm_delta", "head_chest_chin_pitch_deg_delta"),
        ("runtime_screen_head_pitch_est_deg_delta", "head_chest_chin_pitch_deg_delta"),
        ("runtime_screen_head_pitch_est_deg_delta", "head_chest_twist_x_deg_delta"),
        ("runtime_screen_head_pitch_est_deg_delta", "head_chest_twist_y_deg_delta"),
        ("runtime_screen_head_pitch_est_deg_delta", "head_chest_twist_z_deg_delta"),
        ("runtime_screen_head_pitch_est_deg_delta", "head_local_parent_angle_deg_delta"),
        ("runtime_screen_head_pitch_est_deg_delta", "neck02_local_parent_angle_deg_delta"),
        ("face_pitch_consensus_y_norm_delta", "head_chest_chin_pitch_deg_delta"),
    ]
    available_pairs = [(a, b) for a, b in pairs if a in vp2 and b in manny]
    if not available_pairs:
        return out_prefix.with_name(out_prefix.name + "_fft_focus").with_suffix(".png"), {}

    fig, axes = plt.subplots(len(available_pairs), 2, figsize=(15, max(3.0 * len(available_pairs), 6.0)), squeeze=False)
    stats_out: dict[str, dict[str, float]] = {}
    for row_index, (vp2_key, manny_key) in enumerate(available_pairs):
        times, x_raw, y_raw = resampled_standardized_pair(vp2, manny, vp2_key, manny_key, shift, start, end)
        x = fill_missing_linear(x_raw)
        y = fill_missing_linear(y_raw)
        ax_time = axes[row_index][0]
        ax_fft = axes[row_index][1]
        ax_time.plot(times, x_raw, label=f"VP2 {vp2_key}", linewidth=1.6)
        ax_time.plot(times, y_raw, label=f"Manny {manny_key}", linewidth=1.4)
        ax_time.set_xlim(start, end)
        ax_time.set_ylabel("standardized")
        ax_time.grid(True, alpha=0.25)
        ax_time.legend(loc="upper right", fontsize=7)

        pair_name = f"{vp2_key}__vs__{manny_key}"
        stats = spectral_pair_stats(vp2, manny, vp2_key, manny_key, shift, start, end)
        stats_out[pair_name] = stats
        if x is not None and y is not None:
            x = remove_linear_trend(x)
            y = remove_linear_trend(y)
            window = np.hanning(times.size)
            freqs = np.fft.rfftfreq(times.size, d=1.0 / 60.0)
            x_amp = np.abs(np.fft.rfft(x * window))
            y_amp = np.abs(np.fft.rfft(y * window))
            band = (freqs >= 0.5) & (freqs <= 4.0)
            ax_fft.plot(freqs[band], x_amp[band], label="VP2 FFT amp", linewidth=1.6)
            ax_fft.plot(freqs[band], y_amp[band], label="Manny FFT amp", linewidth=1.4)
            dom = stats.get("dominant_frequency_hz", nan())
            if math.isfinite(dom):
                ax_fft.axvline(dom, color="black", alpha=0.3, linewidth=1.0)
        ax_fft.set_title(
            f"{pair_name}\n"
            f"f={stats.get('dominant_frequency_hz', nan()):.2f}Hz "
            f"ampRatio={stats.get('amp_ratio_at_dominant', nan()):.2f} "
            f"phase={stats.get('phase_delta_deg_manny_vs_vp2', nan()):.0f}deg "
            f"corr={stats.get('time_corr_after_standardize', nan()):.2f}"
        )
        ax_fft.set_xlabel("Hz")
        ax_fft.set_ylabel("FFT amp")
        ax_fft.grid(True, alpha=0.25)
        ax_fft.legend(loc="upper right", fontsize=7)
    axes[-1][0].set_xlabel("VP2 video seconds")
    fig.suptitle("Focused 5-7s chin movement FFT handoff diagnostics")
    fig.tight_layout()
    plot_path = out_prefix.with_name(out_prefix.name + "_fft_focus").with_suffix(".png")
    fig.savefig(plot_path, dpi=150)
    return plot_path, stats_out


def main() -> int:
    args = parse_args()
    out_prefix = Path(args.out_prefix)
    out_prefix.parent.mkdir(parents=True, exist_ok=True)

    vp2 = analyze_vp2(Path(args.vp2), Path(args.model))
    if not args.no_face_mesh:
        vp2 = merge_signal_data(vp2, analyze_vp2_face_mesh(Path(args.vp2), Path(args.model)))
    manny = analyze_manny(Path(args.manny_jsonl), args.manny_component)
    shift = auto_time_shift(vp2, manny, args.start, args.end) if args.auto_shift else args.time_shift

    metrics = {}
    best_shift_metrics = {}
    for vp2_key, manny_key in SIGNAL_PAIRS:
        if vp2_key in vp2 and manny_key in manny:
            metrics[f"{vp2_key}__vs__{manny_key}"] = pair_stats(vp2, manny, vp2_key, manny_key, args.start, args.end, shift)
            best_shift_metrics[f"{vp2_key}__vs__{manny_key}"] = best_pair_shift(vp2, manny, vp2_key, manny_key, args.start, args.end, shift)
    spectral_plot_path, spectral_metrics = plot_spectral_focus(out_prefix, vp2, manny, shift, args.start, args.end)

    summary = {
        "analysis_window_s": [args.start, args.end],
        "time_shift_s": shift,
        "plot_mode": args.plot_mode,
        "standardization": "median=0 and p95-p05=1 per plotted signal inside the analysis window" if args.plot_mode == "standardized" else "raw units",
        "vp2_samples": int(vp2["t"].size),
        "manny_samples": int(manny["t"].size),
        "vp2_ranges": {key: pct_range(values[(vp2["t"] >= args.start) & (vp2["t"] <= args.end)]) for key, values in vp2.items() if key != "t" and key.endswith("_delta")},
        "manny_ranges": {key: pct_range(values[(manny["t"] - shift >= args.start) & (manny["t"] - shift <= args.end)]) for key, values in manny.items() if key != "t" and key.endswith("_delta")},
        "pair_metrics": metrics,
        "best_shift_metrics": best_shift_metrics,
        "spectral_metrics": spectral_metrics,
    }
    summary_path = out_prefix.with_suffix(".json")
    summary_path.write_text(json.dumps(summary, indent=2), encoding="utf-8")

    fig, axes = plt.subplots(6, 1, figsize=(15, 16), sharex=True)
    plot_multi_pair(
        axes[0],
        vp2,
        manny,
        ["bilateral_shrug_clearance_world_norm_delta", "bilateral_shrug_clearance_norm_delta"],
        [
            "bilateral_shrug_clearance_norm_delta",
            "bilateral_upperarm_twist01_shrug_clearance_norm_delta",
            "bilateral_upperarm_twist02_shrug_clearance_norm_delta",
        ],
        shift,
        args.start,
        args.end,
        args.plot_mode,
        "Bilateral shrug: 3D/world paired shoulder-to-head-side clearance",
        "norm",
    )
    plot_multi_pair(
        axes[1],
        vp2,
        manny,
        ["left_shrug_clearance_world_norm_delta", "right_shrug_clearance_world_norm_delta"],
        ["left_shrug_clearance_norm_delta", "right_shrug_clearance_norm_delta"],
        shift,
        args.start,
        args.end,
        args.plot_mode,
        "Side shrug: 3D/world paired shoulder-to-head-side clearance",
        "norm",
    )
    plot_pair(
        axes[2],
        vp2,
        manny,
        "shoulder_asym_norm_delta",
        ["shoulder_asym_norm_delta", "clavicle_root_asym_norm_delta"],
        shift,
        args.start,
        args.end,
        args.plot_mode,
        "Shoulder asymmetry",
        "norm",
    )
    plot_multi_pair(
        axes[3],
        vp2,
        manny,
        [
            "runtime_screen_head_pitch_est_deg_delta",
            "runtime_head_forward_pitch_world_est_deg_delta",
            "runtime_screen_head_pitch_face_only_est_deg_delta",
            "runtime_screen_head_pitch_clamped_est_deg_delta",
            "static_screen_head_pitch_est_deg_delta",
            "static_screen_head_pitch_face_only_est_deg_delta",
            "mouth_eye_up_world_norm_delta",
            "nose_eye_up_world_norm_delta",
            "mouth_ear_up_world_norm_delta",
            "head_forward_pitch_world_deg_delta",
            "face_pitch_consensus_y_norm_delta",
            "mouth_eye_y_norm_delta",
            "face_mesh_chin_eye_y_norm_delta",
            "face_mesh_chin_nose_y_norm_delta",
            "face_mesh_mouth_eye_y_norm_delta",
            "face_mesh_chin_eye_z_norm_delta",
            "face_mesh_chin_eye_yz_angle_deg_delta",
            "face_mesh_pitch_x_deg_delta",
        ],
        [
            "head_y_norm_delta",
            "head_local_yaw_delta",
            "neck02_local_yaw_delta",
            "head_local_roll_delta",
            "head_local_pitch_delta",
            "head_local_parent_angle_deg_delta",
            "neck02_local_parent_angle_deg_delta",
        ],
        shift,
        args.start,
        args.end,
        args.plot_mode,
        "Head/chin pitch: 3D/world proxies plus 2D mouth-eye reference",
        "norm / deg",
    )
    plot_pair(
        axes[4],
        vp2,
        manny,
        "head_lateral_angle_deg_delta",
        ["head_lateral_angle_deg_delta", "neck_chain_lateral_angle_deg_delta", "head_local_roll_delta"],
        shift,
        args.start,
        args.end,
        args.plot_mode,
        "Head lateral angle proxies",
        "degrees",
    )
    plot_pair(
        axes[5],
        vp2,
        manny,
        "ear_roll_deg_delta",
        ["head_local_roll_delta"],
        shift,
        args.start,
        args.end,
        args.plot_mode,
        "Head roll",
        "degrees",
    )
    axes[-1].set_xlabel("VP2 video seconds")
    fig.suptitle(
        "VP2 MediaPipe signals vs Manny bone-derived signals"
        + (" (standardized)" if args.plot_mode == "standardized" else " (raw units)")
    )
    fig.tight_layout()
    plot_path = out_prefix.with_suffix(".png")
    fig.savefig(plot_path, dpi=150)

    focused_fig, focused_axes = plt.subplots(4, 1, figsize=(15, 11), sharex=True)
    plot_multi_pair(
        focused_axes[0],
        vp2,
        manny,
        ["bilateral_shrug_clearance_norm_delta", "bilateral_shrug_clearance_world_norm_delta"],
        [
            "bilateral_shrug_clearance_norm_delta",
            "bilateral_upperarm_twist01_shrug_clearance_norm_delta",
            "bilateral_upperarm_twist02_shrug_clearance_norm_delta",
        ],
        shift,
        args.start,
        args.end,
        args.plot_mode,
        "Focused bilateral shrug candidates",
        "norm",
    )
    plot_multi_pair(
        focused_axes[1],
        vp2,
        manny,
        ["left_shrug_clearance_norm_delta", "right_shrug_clearance_norm_delta"],
        [
            "left_shrug_clearance_norm_delta",
            "right_shrug_clearance_norm_delta",
            "left_upperarm_twist01_shrug_clearance_norm_delta",
            "right_upperarm_twist01_shrug_clearance_norm_delta",
        ],
        shift,
        args.start,
        args.end,
        args.plot_mode,
        "Focused side shrug and visible upper-arm twist",
        "norm",
    )
    plot_multi_pair(
        focused_axes[2],
        vp2,
        manny,
        [
            "runtime_screen_head_pitch_est_deg_delta",
            "runtime_head_forward_pitch_world_est_deg_delta",
            "runtime_screen_head_pitch_face_only_est_deg_delta",
            "runtime_screen_head_pitch_clamped_est_deg_delta",
            "runtime_face_pitch_input_norm_delta",
            "static_screen_head_pitch_est_deg_delta",
            "static_screen_head_pitch_face_only_est_deg_delta",
            "face_pitch_consensus_y_norm_delta",
            "mouth_eye_up_world_norm_delta",
            "nose_eye_up_world_norm_delta",
            "mouth_ear_up_world_norm_delta",
            "nose_ear_up_world_norm_delta",
            "head_forward_pitch_world_deg_delta",
            "mouth_eye_y_norm_delta",
            "nose_eye_y_norm_delta",
            "face_mesh_chin_eye_y_norm_delta",
            "face_mesh_chin_nose_y_norm_delta",
            "face_mesh_chin_mouth_y_norm_delta",
            "face_mesh_mouth_eye_y_norm_delta",
            "face_mesh_nose_eye_y_norm_delta",
            "face_mesh_chin_eye_z_norm_delta",
            "face_mesh_chin_nose_z_norm_delta",
            "face_mesh_chin_mouth_z_norm_delta",
            "face_mesh_mouth_eye_z_norm_delta",
            "face_mesh_nose_eye_z_norm_delta",
            "face_mesh_chin_eye_yz_angle_deg_delta",
            "face_mesh_chin_nose_yz_angle_deg_delta",
            "face_mesh_chin_mouth_yz_angle_deg_delta",
            "face_mesh_pitch_x_deg_delta",
        ],
        [
            "head_local_yaw_delta",
            "neck02_local_yaw_delta",
            "head_chest_chin_pitch_deg_delta",
            "head_chest_twist_x_deg_delta",
            "head_chest_twist_y_deg_delta",
            "head_chest_twist_z_deg_delta",
            "head_local_parent_angle_deg_delta",
            "neck02_local_parent_angle_deg_delta",
        ],
        shift,
        args.start,
        args.end,
        args.plot_mode,
        "Focused chin pitch candidates",
        "norm / deg",
    )
    plot_pair(
        focused_axes[3],
        vp2,
        manny,
        "head_lateral_angle_deg_delta",
        ["head_lateral_angle_deg_delta", "neck_chain_lateral_angle_deg_delta"],
        shift,
        args.start,
        args.end,
        args.plot_mode,
        "Focused head lateral candidates",
        "degrees",
    )
    focused_axes[-1].set_xlabel("VP2 video seconds")
    focused_fig.suptitle(
        "VP2 vs Manny focused tuning signals"
        + (" (standardized)" if args.plot_mode == "standardized" else " (raw units)")
    )
    focused_fig.tight_layout()
    focused_plot_path = out_prefix.with_name(out_prefix.name + "_focused").with_suffix(".png")
    focused_fig.savefig(focused_plot_path, dpi=150)

    chin_fig, chin_axes = plt.subplots(2, 1, figsize=(15, 7), sharex=True)
    plot_multi_pair(
        chin_axes[0],
        vp2,
        manny,
        [
            "face_mesh_chin_eye_y_norm_delta",
            "face_mesh_chin_nose_y_norm_delta",
            "face_mesh_chin_mouth_y_norm_delta",
            "face_mesh_mouth_eye_y_norm_delta",
            "face_mesh_chin_eye_z_norm_delta",
            "face_mesh_chin_mouth_z_norm_delta",
            "face_mesh_chin_eye_yz_angle_deg_delta",
            "face_mesh_chin_mouth_yz_angle_deg_delta",
            "face_mesh_pitch_x_deg_delta",
            "runtime_screen_head_pitch_est_deg_delta",
            "runtime_head_forward_pitch_world_est_deg_delta",
        ],
        [
            "head_chest_chin_pitch_deg_delta",
            "head_chest_twist_x_deg_delta",
            "head_chest_twist_y_deg_delta",
            "head_chest_twist_z_deg_delta",
            "head_local_parent_angle_deg_delta",
            "neck02_local_parent_angle_deg_delta",
        ],
        shift,
        args.start,
        args.end,
        args.plot_mode,
        "Focused chin pitch: dense FaceMesh VP2 signals vs Manny head/neck",
        "norm / deg",
    )
    plot_multi_pair(
        chin_axes[1],
        vp2,
        manny,
        [
            "face_mesh_pitch_x_deg_delta",
            "face_mesh_pitch_y_deg_delta",
            "face_mesh_pitch_z_deg_delta",
        ],
        [
            "head_local_pitch_delta",
            "head_local_yaw_delta",
            "head_local_roll_delta",
        ],
        shift,
        args.start,
        args.end,
        args.plot_mode,
        "Focused face-plane axes against Manny local axes",
        "degrees",
    )
    chin_axes[-1].set_xlabel("VP2 video seconds")
    chin_fig.suptitle(
        "VP2 dense FaceMesh chin-pitch focus"
        + (" (standardized)" if args.plot_mode == "standardized" else " (raw units)")
    )
    chin_fig.tight_layout()
    chin_plot_path = out_prefix.with_name(out_prefix.name + "_chin_focus").with_suffix(".png")
    chin_fig.savefig(chin_plot_path, dpi=150)
    print(f"summary={summary_path}")
    print(f"plot={plot_path}")
    print(f"focused_plot={focused_plot_path}")
    print(f"chin_plot={chin_plot_path}")
    print(f"spectral_plot={spectral_plot_path}")
    print(json.dumps({k: summary[k] for k in ["analysis_window_s", "time_shift_s", "vp2_samples", "manny_samples"]}, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
