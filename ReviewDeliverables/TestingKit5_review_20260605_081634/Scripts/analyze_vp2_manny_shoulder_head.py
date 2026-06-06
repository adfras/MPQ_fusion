#!/usr/bin/env python3
"""Compare VP2 MediaPipe shoulder/head motion against Manny Unreal bone motion."""

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
    parser.add_argument("--out-prefix", required=True)
    parser.add_argument("--start", type=float, default=2.0)
    parser.add_argument("--end", type=float, default=18.5)
    return parser.parse_args()


def pct_range(values: np.ndarray) -> dict[str, float]:
    values = values[np.isfinite(values)]
    if values.size == 0:
        return {"min": math.nan, "max": math.nan, "p05": math.nan, "p95": math.nan, "range_p95_p05": math.nan, "std": math.nan}
    return {
        "min": float(np.min(values)),
        "max": float(np.max(values)),
        "p05": float(np.percentile(values, 5)),
        "p95": float(np.percentile(values, 95)),
        "range_p95_p05": float(np.percentile(values, 95) - np.percentile(values, 5)),
        "std": float(np.std(values)),
    }


def unwrap_degrees(values: np.ndarray) -> np.ndarray:
    out = values.astype(float).copy()
    finite = np.isfinite(out)
    if finite.sum() < 2:
        return out
    out[finite] = np.rad2deg(np.unwrap(np.deg2rad(out[finite])))
    return out


def analyze_vp2(video_path: Path, model_path: Path) -> dict[str, np.ndarray]:
    cap = cv2.VideoCapture(str(video_path))
    if not cap.isOpened():
        raise RuntimeError(f"Failed to open video: {video_path}")
    fps = cap.get(cv2.CAP_PROP_FPS) or 30.0
    width = int(cap.get(cv2.CAP_PROP_FRAME_WIDTH))
    height = int(cap.get(cv2.CAP_PROP_FRAME_HEIGHT))

    BaseOptions = mp.tasks.BaseOptions
    PoseLandmarker = mp.tasks.vision.PoseLandmarker
    PoseLandmarkerOptions = mp.tasks.vision.PoseLandmarkerOptions
    VisionRunningMode = mp.tasks.vision.RunningMode
    options = PoseLandmarkerOptions(
        base_options=BaseOptions(model_asset_path=str(model_path)),
        running_mode=VisionRunningMode.VIDEO,
        num_poses=1,
        min_pose_detection_confidence=0.5,
        min_pose_presence_confidence=0.5,
        min_tracking_confidence=0.5,
        output_segmentation_masks=False,
    )

    rows: list[dict[str, float]] = []
    frame_index = 0
    with PoseLandmarker.create_from_options(options) as landmarker:
        while True:
            ok, frame = cap.read()
            if not ok:
                break
            timestamp_ms = int(round(frame_index * 1000.0 / fps))
            rgb = cv2.cvtColor(frame, cv2.COLOR_BGR2RGB)
            result = landmarker.detect_for_video(mp.Image(image_format=mp.ImageFormat.SRGB, data=rgb), timestamp_ms)
            row = {
                "t": frame_index / fps,
                "shoulder_width": math.nan,
                "shoulder_asym_norm": math.nan,
                "shoulder_roll_deg": math.nan,
                "head_x_norm": math.nan,
                "head_y_norm": math.nan,
                "ear_roll_deg": math.nan,
                "shoulder_vis": math.nan,
                "head_vis": math.nan,
            }
            if result.pose_landmarks:
                pose = result.pose_landmarks[0]
                pts = np.array([[lm.x * width, lm.y * height] for lm in pose], dtype=float)
                vis = np.array([float(getattr(lm, "visibility", 1.0) or 0.0) for lm in pose], dtype=float)
                left_shoulder = pts[11]
                right_shoulder = pts[12]
                shoulder_vec = right_shoulder - left_shoulder
                shoulder_width = float(np.linalg.norm(shoulder_vec))
                shoulder_mid = (left_shoulder + right_shoulder) * 0.5
                if shoulder_width > 1e-6:
                    nose = pts[0]
                    left_ear = pts[7]
                    right_ear = pts[8]
                    row.update({
                        "shoulder_width": shoulder_width,
                        # Positive means the camera-right shoulder is lower in the image.
                        "shoulder_asym_norm": float((right_shoulder[1] - left_shoulder[1]) / shoulder_width),
                        "shoulder_roll_deg": float(math.degrees(math.atan2(right_shoulder[1] - left_shoulder[1], abs(right_shoulder[0] - left_shoulder[0])))),
                        "head_x_norm": float((nose[0] - shoulder_mid[0]) / shoulder_width),
                        "head_y_norm": float((shoulder_mid[1] - nose[1]) / shoulder_width),
                        "ear_roll_deg": float(math.degrees(math.atan2(right_ear[1] - left_ear[1], abs(right_ear[0] - left_ear[0])))),
                        "shoulder_vis": float(min(vis[11], vis[12])),
                        "head_vis": float(min(vis[0], vis[7], vis[8])),
                    })
            rows.append(row)
            frame_index += 1
    cap.release()
    return {key: np.array([row[key] for row in rows], dtype=float) for key in rows[0].keys()}


def analyze_manny(jsonl_path: Path) -> dict[str, np.ndarray]:
    rows: list[dict[str, float]] = []
    with jsonl_path.open("r", encoding="utf-8") as handle:
        for line in handle:
            if not line.strip():
                continue
            item = json.loads(line)
            loc = item.get("loc", {})
            rot = item.get("rot", {})
            l_shoulder = loc.get("upperarm_l")
            r_shoulder = loc.get("upperarm_r")
            l_clav = loc.get("clavicle_l")
            r_clav = loc.get("clavicle_r")
            head = loc.get("head")
            neck = loc.get("neck_01")
            head_rot = rot.get("head")
            row = {
                "t": float(item.get("t", math.nan)),
                "shoulder_width": math.nan,
                "shoulder_asym_norm": math.nan,
                "shoulder_roll_deg": math.nan,
                "head_x_norm": math.nan,
                "head_y_norm": math.nan,
                "head_pitch": math.nan,
                "head_yaw": math.nan,
                "head_roll": math.nan,
                "clavicle_root_asym_norm": math.nan,
            }
            if l_shoulder and r_shoulder:
                l = np.array(l_shoulder, dtype=float)
                r = np.array(r_shoulder, dtype=float)
                # MirrorAvatarMesh faces the camera; X is horizontal, Z is vertical.
                shoulder_width = float(abs(l[0] - r[0]))
                if shoulder_width > 1e-6:
                    row["shoulder_width"] = shoulder_width
                    # Positive means avatar-right shoulder is lower in world Z.
                    row["shoulder_asym_norm"] = float((l[2] - r[2]) / shoulder_width)
                    row["shoulder_roll_deg"] = float(math.degrees(math.atan2(l[2] - r[2], abs(l[0] - r[0]))))
                    if head:
                        h = np.array(head, dtype=float)
                        mid = (l + r) * 0.5
                        row["head_x_norm"] = float((h[0] - mid[0]) / shoulder_width)
                        row["head_y_norm"] = float((h[2] - mid[2]) / shoulder_width)
                    elif neck:
                        h = np.array(neck, dtype=float)
                        mid = (l + r) * 0.5
                        row["head_x_norm"] = float((h[0] - mid[0]) / shoulder_width)
                        row["head_y_norm"] = float((h[2] - mid[2]) / shoulder_width)
            if l_clav and r_clav and l_shoulder and r_shoulder:
                lc = np.array(l_clav, dtype=float)
                rc = np.array(r_clav, dtype=float)
                ls = np.array(l_shoulder, dtype=float)
                rs = np.array(r_shoulder, dtype=float)
                shoulder_width = float(abs(ls[0] - rs[0]))
                if shoulder_width > 1e-6:
                    row["clavicle_root_asym_norm"] = float((lc[2] - rc[2]) / shoulder_width)
            if head_rot:
                row["head_pitch"], row["head_yaw"], row["head_roll"] = [float(v) for v in head_rot]
            rows.append(row)
    if not rows:
        raise RuntimeError(f"No Manny rows in {jsonl_path}")
    data = {key: np.array([row[key] for row in rows], dtype=float) for key in rows[0].keys()}
    for key in ["head_pitch", "head_yaw", "head_roll"]:
        data[key] = unwrap_degrees(data[key])
    return data


def window(data: dict[str, np.ndarray], start: float, end: float) -> np.ndarray:
    return (data["t"] >= start) & (data["t"] <= end)


def nearest(data: dict[str, np.ndarray], t: float) -> int:
    return int(np.argmin(np.abs(data["t"] - t)))


def main() -> int:
    args = parse_args()
    out_prefix = Path(args.out_prefix)
    out_prefix.parent.mkdir(parents=True, exist_ok=True)

    vp2 = analyze_vp2(Path(args.vp2), Path(args.model))
    manny = analyze_manny(Path(args.manny_jsonl))
    vp2_mask = window(vp2, args.start, args.end)
    manny_mask = window(manny, args.start, args.end)

    summary = {
        "analysis_window_s": [args.start, args.end],
        "vp2_samples": int(vp2["t"].size),
        "manny_samples": int(manny["t"].size),
        "vp2": {},
        "manny": {},
        "sample_times": {},
    }

    for key in ["shoulder_asym_norm", "shoulder_roll_deg", "head_x_norm", "head_y_norm", "ear_roll_deg"]:
        if key in vp2:
            summary["vp2"][key] = pct_range(vp2[key][vp2_mask])
    for key in ["shoulder_asym_norm", "shoulder_roll_deg", "head_x_norm", "head_y_norm", "head_pitch", "head_yaw", "head_roll", "clavicle_root_asym_norm"]:
        if key in manny:
            summary["manny"][key] = pct_range(manny[key][manny_mask])

    for t in [6.0, 12.0]:
        vi = nearest(vp2, t)
        mi = nearest(manny, t)
        summary["sample_times"][str(t)] = {
            "vp2": {
                "t": float(vp2["t"][vi]),
                "shoulder_asym_norm": float(vp2["shoulder_asym_norm"][vi]),
                "shoulder_roll_deg": float(vp2["shoulder_roll_deg"][vi]),
                "head_x_norm": float(vp2["head_x_norm"][vi]),
                "head_y_norm": float(vp2["head_y_norm"][vi]),
                "ear_roll_deg": float(vp2["ear_roll_deg"][vi]),
            },
            "manny": {
                "t": float(manny["t"][mi]),
                "shoulder_asym_norm": float(manny["shoulder_asym_norm"][mi]),
                "shoulder_roll_deg": float(manny["shoulder_roll_deg"][mi]),
                "head_x_norm": float(manny["head_x_norm"][mi]),
                "head_y_norm": float(manny["head_y_norm"][mi]),
                "head_pitch": float(manny["head_pitch"][mi]),
                "head_yaw": float(manny["head_yaw"][mi]),
                "head_roll": float(manny["head_roll"][mi]),
            },
        }

    summary_path = out_prefix.with_suffix(".json")
    summary_path.write_text(json.dumps(summary, indent=2), encoding="utf-8")

    fig, axes = plt.subplots(3, 1, figsize=(12, 9), sharex=True)
    axes[0].plot(vp2["t"], vp2["shoulder_asym_norm"], label="VP2 MediaPipe shoulders")
    axes[0].plot(manny["t"], manny["shoulder_asym_norm"], label="Manny shoulder joints")
    axes[0].set_ylabel("shoulder asym / width")
    axes[0].legend(loc="upper right")
    axes[0].grid(True, alpha=0.25)

    axes[1].plot(vp2["t"], vp2["head_x_norm"], label="VP2 nose lateral offset")
    axes[1].plot(manny["t"], manny["head_x_norm"], label="Manny head lateral offset")
    axes[1].set_ylabel("head X / shoulder width")
    axes[1].legend(loc="upper right")
    axes[1].grid(True, alpha=0.25)

    axes[2].plot(vp2["t"], vp2["ear_roll_deg"], label="VP2 ear-line/head roll proxy")
    axes[2].plot(manny["t"], manny["head_roll"], label="Manny head world roll")
    axes[2].set_ylabel("degrees")
    axes[2].set_xlabel("seconds")
    axes[2].legend(loc="upper right")
    axes[2].grid(True, alpha=0.25)

    fig.suptitle("VP2 MediaPipe input vs Manny Unreal bone motion")
    fig.tight_layout()
    plot_path = out_prefix.with_suffix(".png")
    fig.savefig(plot_path, dpi=150)

    print(f"summary={summary_path}")
    print(f"plot={plot_path}")
    print(json.dumps(summary, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
