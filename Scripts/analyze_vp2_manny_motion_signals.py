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
        if ref_vals.size:
            data[f"{key}_delta"] = vals - float(np.median(ref_vals))


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
                "left_shoulder_mid_up_norm": nan(),
                "right_shoulder_mid_up_norm": nan(),
                "shoulder_asym_norm": nan(),
                "shoulder_roll_deg": nan(),
                "head_x_norm": nan(),
                "head_y_norm": nan(),
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
                shoulder_width = float(np.linalg.norm(rs - ls))
                if shoulder_width > 1e-6:
                    smid = (ls + rs) * 0.5
                    hmid = (lh + rh) * 0.5
                    ear_span = float(np.linalg.norm(rear - lear))
                    ear_mid = (lear + rear) * 0.5
                    row.update(
                        {
                            "shoulder_width": shoulder_width,
                            "left_shoulder_height_norm": float((hmid[1] - ls[1]) / shoulder_width),
                            "right_shoulder_height_norm": float((hmid[1] - rs[1]) / shoulder_width),
                            "left_shoulder_mid_up_norm": float((smid[1] - ls[1]) / shoulder_width),
                            "right_shoulder_mid_up_norm": float((smid[1] - rs[1]) / shoulder_width),
                            "shoulder_asym_norm": float((rs[1] - ls[1]) / shoulder_width),
                            "shoulder_roll_deg": float(math.degrees(math.atan2(rs[1] - ls[1], abs(rs[0] - ls[0])))),
                            "head_x_norm": float((nose[0] - smid[0]) / shoulder_width),
                            "head_y_norm": float((smid[1] - nose[1]) / shoulder_width),
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
            rows.append(row)
            frame_index += 1
    cap.release()
    data = as_arrays(rows)
    add_delta_signals(data)
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
            items.append({"t": sample.get("t", nan()), "loc": loc, "rot": rotations, "quat": quaternions})
        return items

    items = []
    with path.open("r", encoding="utf-8") as handle:
        for line in handle:
            if line.strip():
                items.append(json.loads(line))
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
            lower_l = pos(loc, "lowerarm_l")
            lower_r = pos(loc, "lowerarm_r")
            row = {
                "t": float(item.get("t", nan())),
                "shoulder_width": nan(),
                "left_shoulder_height_norm": nan(),
                "right_shoulder_height_norm": nan(),
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
            head_local_q = quat_values(quaternions, "head_local")
            if head_local_q is not None:
                if head_local_ref_q is None:
                    head_local_ref_q = head_local_q
                row["head_local_parent_angle_deg"] = quat_delta_angle_deg(head_local_q, head_local_ref_q)
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
    ("nose_from_ear_mid_x_norm_delta", "head_yaw_delta"),
    ("nose_from_ear_mid_x_norm_delta", "head_local_yaw_delta"),
    ("ear_roll_deg_delta", "head_roll_delta"),
    ("ear_roll_deg_delta", "head_local_roll_delta"),
    ("shoulder_asym_norm_delta", "left_clavicle_local_parent_angle_deg_delta"),
    ("shoulder_asym_norm_delta", "right_clavicle_local_parent_angle_deg_delta"),
]


def auto_time_shift(vp2: dict[str, np.ndarray], manny: dict[str, np.ndarray], start: float, end: float) -> float:
    candidate_pairs = [
        ("head_lateral_angle_deg_delta", "head_yaw_delta"),
        ("head_lateral_angle_deg_delta", "head_local_yaw_delta"),
        ("head_x_norm_delta", "head_segment_x_norm_delta"),
        ("shoulder_asym_norm_delta", "shoulder_asym_norm_delta"),
        ("left_upperarm_down_angle_deg_delta", "left_upperarm_down_angle_deg_delta"),
        ("right_upperarm_down_angle_deg_delta", "right_upperarm_down_angle_deg_delta"),
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


def plot_pair(ax: plt.Axes, vp2: dict[str, np.ndarray], manny: dict[str, np.ndarray], vp2_key: str, manny_keys: list[str], shift: float, title: str, ylabel: str) -> None:
    ax.plot(vp2["t"], vp2[vp2_key], label=f"VP2 {vp2_key}", linewidth=1.8)
    for key in manny_keys:
        if key in manny:
            ax.plot(manny["t"] - shift, manny[key], label=f"Manny {key}", linewidth=1.5, alpha=0.9)
    ax.set_title(title)
    ax.set_ylabel(ylabel)
    ax.grid(True, alpha=0.25)
    ax.legend(loc="upper right", fontsize=8)


def main() -> int:
    args = parse_args()
    out_prefix = Path(args.out_prefix)
    out_prefix.parent.mkdir(parents=True, exist_ok=True)

    vp2 = analyze_vp2(Path(args.vp2), Path(args.model))
    manny = analyze_manny(Path(args.manny_jsonl), args.manny_component)
    shift = auto_time_shift(vp2, manny, args.start, args.end) if args.auto_shift else args.time_shift

    metrics = {}
    for vp2_key, manny_key in SIGNAL_PAIRS:
        if vp2_key in vp2 and manny_key in manny:
            metrics[f"{vp2_key}__vs__{manny_key}"] = pair_stats(vp2, manny, vp2_key, manny_key, args.start, args.end, shift)

    summary = {
        "analysis_window_s": [args.start, args.end],
        "time_shift_s": shift,
        "vp2_samples": int(vp2["t"].size),
        "manny_samples": int(manny["t"].size),
        "vp2_ranges": {key: pct_range(values[(vp2["t"] >= args.start) & (vp2["t"] <= args.end)]) for key, values in vp2.items() if key != "t" and key.endswith("_delta")},
        "manny_ranges": {key: pct_range(values[(manny["t"] - shift >= args.start) & (manny["t"] - shift <= args.end)]) for key, values in manny.items() if key != "t" and key.endswith("_delta")},
        "pair_metrics": metrics,
    }
    summary_path = out_prefix.with_suffix(".json")
    summary_path.write_text(json.dumps(summary, indent=2), encoding="utf-8")

    fig, axes = plt.subplots(5, 1, figsize=(15, 13), sharex=True)
    plot_pair(
        axes[0],
        vp2,
        manny,
        "left_shoulder_height_norm_delta",
        ["left_shoulder_height_norm_delta", "left_clavicle_endpoint_angle_deg_delta"],
        shift,
        "Left shoulder height and clavicle endpoint",
        "norm / deg",
    )
    plot_pair(
        axes[1],
        vp2,
        manny,
        "right_shoulder_height_norm_delta",
        ["right_shoulder_height_norm_delta", "right_clavicle_endpoint_angle_deg_delta"],
        shift,
        "Right shoulder height and clavicle endpoint",
        "norm / deg",
    )
    plot_pair(
        axes[2],
        vp2,
        manny,
        "shoulder_asym_norm_delta",
        ["shoulder_asym_norm_delta", "clavicle_root_asym_norm_delta"],
        shift,
        "Shoulder asymmetry",
        "norm",
    )
    plot_pair(
        axes[3],
        vp2,
        manny,
        "head_lateral_angle_deg_delta",
        ["head_lateral_angle_deg_delta", "neck_chain_lateral_angle_deg_delta", "head_yaw_delta", "head_local_yaw_delta"],
        shift,
        "Head lateral angle proxies",
        "degrees",
    )
    plot_pair(
        axes[4],
        vp2,
        manny,
        "ear_roll_deg_delta",
        ["head_roll_delta"],
        shift,
        "Head roll",
        "degrees",
    )
    axes[-1].set_xlabel("VP2 video seconds")
    fig.suptitle("VP2 MediaPipe signals vs Manny bone-derived signals")
    fig.tight_layout()
    plot_path = out_prefix.with_suffix(".png")
    fig.savefig(plot_path, dpi=150)
    print(f"summary={summary_path}")
    print(f"plot={plot_path}")
    print(json.dumps({k: summary[k] for k in ["analysis_window_s", "time_shift_s", "vp2_samples", "manny_samples"]}, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
