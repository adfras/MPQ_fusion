#!/usr/bin/env python3
"""Create a side-by-side video with actual projected Manny bones on the left."""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
from pathlib import Path

import cv2
import numpy as np


PANEL_SIZE = (1280, 720)
MANNY_RAW_CROP = (160, 180, 440, 520)  # x, y, width, height

MANNY_BONE_CONNECTIONS = [
    ("pelvis", "spine_01"),
    ("spine_01", "spine_02"),
    ("spine_02", "spine_03"),
    ("spine_03", "neck_01"),
    ("neck_01", "neck_02"),
    ("neck_02", "head"),
    ("spine_03", "clavicle_l"),
    ("clavicle_l", "upperarm_l"),
    ("upperarm_l", "lowerarm_l"),
    ("lowerarm_l", "hand_l"),
    ("spine_03", "clavicle_r"),
    ("clavicle_r", "upperarm_r"),
    ("upperarm_r", "lowerarm_r"),
    ("lowerarm_r", "hand_r"),
]

IMPORTANT_BONES = {
    "head": ("head", (0, 255, 255)),
    "neck_01": ("neck", (255, 255, 0)),
    "clavicle_l": ("clav_l", (0, 165, 255)),
    "clavicle_r": ("clav_r", (0, 165, 255)),
    "upperarm_l": ("uparm_l", (255, 120, 40)),
    "upperarm_r": ("uparm_r", (255, 120, 40)),
    "lowerarm_l": ("larm_l", (255, 0, 255)),
    "lowerarm_r": ("larm_r", (255, 0, 255)),
    "hand_l": ("hand_l", (180, 0, 255)),
    "hand_r": ("hand_r", (180, 0, 255)),
    "pelvis": ("pelvis", (90, 255, 120)),
}


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manny", required=True, help="Raw Unreal Manny viewport recording")
    parser.add_argument("--bones-jsonl", required=True, help="Projected Manny bone JSONL")
    parser.add_argument("--right-overlay", required=True, help="VP2 MediaPipe overlay video")
    parser.add_argument("--output", required=True, help="Output comparison video path")
    parser.add_argument("--time-offset", type=float, default=0.0, help="Seconds added to video time when sampling bone JSON")
    parser.add_argument("--raw-x-offset", type=float, default=0.0, help="Pixel offset applied to projected Manny bone X before crop fitting")
    parser.add_argument("--raw-y-offset", type=float, default=0.0, help="Pixel offset applied to projected Manny bone Y before crop fitting")
    return parser.parse_args()


def load_bone_samples(path: Path) -> list[dict]:
    samples = []
    with path.open("r", encoding="utf-8") as handle:
        for line in handle:
            line = line.strip()
            if not line:
                continue
            try:
                item = json.loads(line)
            except json.JSONDecodeError:
                continue
            if "t" in item and "points" in item:
                samples.append(item)
    samples.sort(key=lambda item: float(item["t"]))
    return samples


def interpolate_points(samples: list[dict], timestamp_s: float) -> dict[str, list[float] | None]:
    if not samples:
        return {}
    if timestamp_s <= samples[0]["t"]:
        return samples[0]["points"]
    if timestamp_s >= samples[-1]["t"]:
        return samples[-1]["points"]

    lo = 0
    hi = len(samples) - 1
    while lo + 1 < hi:
        mid = (lo + hi) // 2
        if samples[mid]["t"] <= timestamp_s:
            lo = mid
        else:
            hi = mid

    a = samples[lo]
    b = samples[hi]
    ta = float(a["t"])
    tb = float(b["t"])
    alpha = 0.0 if tb <= ta else (timestamp_s - ta) / (tb - ta)
    result: dict[str, list[float] | None] = {}
    for bone, point_a in a["points"].items():
        point_b = b["points"].get(bone)
        if point_a is None and point_b is None:
            result[bone] = None
        elif point_a is None:
            result[bone] = point_b
        elif point_b is None:
            result[bone] = point_a
        else:
            result[bone] = [
                float(point_a[0]) + (float(point_b[0]) - float(point_a[0])) * alpha,
                float(point_a[1]) + (float(point_b[1]) - float(point_a[1])) * alpha,
            ]
    return result


def raw_to_panel(point: list[float] | tuple[float, float] | None, raw_x_offset: float, raw_y_offset: float) -> tuple[int, int] | None:
    if point is None:
        return None
    crop_x, crop_y, crop_w, crop_h = MANNY_RAW_CROP
    panel_w, panel_h = PANEL_SIZE
    scale = min(panel_w / crop_w, panel_h / crop_h)
    fitted_w = crop_w * scale
    fitted_h = crop_h * scale
    ox = (panel_w - fitted_w) / 2.0
    oy = (panel_h - fitted_h) / 2.0
    x = (float(point[0]) + raw_x_offset - crop_x) * scale + ox
    y = (float(point[1]) + raw_y_offset - crop_y) * scale + oy
    return int(round(x)), int(round(y))


def crop_and_fit_manny(frame: np.ndarray) -> np.ndarray:
    x, y, w, h = MANNY_RAW_CROP
    cropped = frame[y:y + h, x:x + w]
    panel_w, panel_h = PANEL_SIZE
    scale = min(panel_w / w, panel_h / h)
    fitted_w = int(round(w * scale))
    fitted_h = int(round(h * scale))
    resized = cv2.resize(cropped, (fitted_w, fitted_h), interpolation=cv2.INTER_AREA)
    panel = np.zeros((panel_h, panel_w, 3), dtype=np.uint8)
    ox = (panel_w - fitted_w) // 2
    oy = (panel_h - fitted_h) // 2
    panel[oy:oy + fitted_h, ox:ox + fitted_w] = resized
    return panel


def fit_right(frame: np.ndarray) -> np.ndarray:
    return cv2.resize(frame, PANEL_SIZE, interpolation=cv2.INTER_AREA)


def draw_label_block(frame: np.ndarray, lines: list[str]) -> None:
    x, y = 18, 28
    font = cv2.FONT_HERSHEY_SIMPLEX
    scale = 0.58
    thickness = 1
    line_height = 24
    width = max(cv2.getTextSize(line, font, scale, thickness)[0][0] for line in lines) + 24
    height = line_height * len(lines) + 14
    overlay = frame.copy()
    cv2.rectangle(overlay, (x - 10, y - 22), (x - 10 + width, y - 22 + height), (0, 0, 0), -1)
    cv2.addWeighted(overlay, 0.56, frame, 0.44, 0, frame)
    for index, line in enumerate(lines):
        cv2.putText(frame, line, (x, y + index * line_height), font, scale, (245, 245, 245), thickness, cv2.LINE_AA)


def draw_bones(
    panel: np.ndarray,
    raw_points: dict[str, list[float] | None],
    frame_index: int,
    timestamp_s: float,
    sample_time: float,
    raw_x_offset: float,
    raw_y_offset: float,
) -> None:
    points = {bone: raw_to_panel(point, raw_x_offset, raw_y_offset) for bone, point in raw_points.items()}
    layer = panel.copy()
    for a, b in MANNY_BONE_CONNECTIONS:
        pa = points.get(a)
        pb = points.get(b)
        if pa is not None and pb is not None:
            cv2.line(layer, pa, pb, (50, 220, 255), 5, cv2.LINE_AA)
    cv2.addWeighted(layer, 0.72, panel, 0.28, 0, panel)

    for bone, point in points.items():
        if point is None:
            continue
        label, color = IMPORTANT_BONES.get(bone, ("", (40, 255, 120)))
        radius = 7 if bone in IMPORTANT_BONES else 5
        cv2.circle(panel, point, radius + 2, (0, 0, 0), -1, cv2.LINE_AA)
        cv2.circle(panel, point, radius, color, -1, cv2.LINE_AA)
        if label:
            cv2.putText(panel, label, (point[0] + 8, point[1] - 8), cv2.FONT_HERSHEY_SIMPLEX, 0.42, color, 1, cv2.LINE_AA)

    visible_count = sum(1 for point in points.values() if point is not None)
    draw_label_block(panel, [
        "Manny actual bone overlay",
        "projected from MirrorAvatarMesh",
        f"frame: {frame_index}  video_t: {timestamp_s:.2f}s",
        f"bone_t: {sample_time:.2f}s  visible: {visible_count}",
    ])


def transcode(temp_path: Path, output_path: Path, audio_source: Path) -> bool:
    cmd = [
        "ffmpeg",
        "-y",
        "-hide_banner",
        "-loglevel",
        "error",
        "-i",
        str(temp_path),
        "-i",
        str(audio_source),
        "-map",
        "0:v:0",
        "-map",
        "1:a?",
        "-c:v",
        "libx264",
        "-preset",
        "medium",
        "-crf",
        "18",
        "-pix_fmt",
        "yuv420p",
        "-c:a",
        "aac",
        "-shortest",
        str(output_path),
    ]
    try:
        subprocess.run(cmd, check=True)
        return True
    except Exception as exc:
        print(f"ffmpeg transcode failed: {exc}", file=sys.stderr)
        return False


def main() -> int:
    args = parse_args()
    manny_path = Path(args.manny).resolve()
    bones_path = Path(args.bones_jsonl).resolve()
    right_path = Path(args.right_overlay).resolve()
    output_path = Path(args.output).resolve()
    output_path.parent.mkdir(parents=True, exist_ok=True)

    for path in [manny_path, bones_path, right_path]:
        if not path.exists():
            print(f"Missing input: {path}", file=sys.stderr)
            return 2

    samples = load_bone_samples(bones_path)
    if not samples:
        print(f"No bone samples in {bones_path}", file=sys.stderr)
        return 2

    manny_cap = cv2.VideoCapture(str(manny_path))
    right_cap = cv2.VideoCapture(str(right_path))
    if not manny_cap.isOpened() or not right_cap.isOpened():
        print("Failed to open input videos", file=sys.stderr)
        return 2

    fps = manny_cap.get(cv2.CAP_PROP_FPS) or 30.0
    total_frames = int(min(manny_cap.get(cv2.CAP_PROP_FRAME_COUNT) or 0, right_cap.get(cv2.CAP_PROP_FRAME_COUNT) or 0))
    temp_path = output_path.with_name(output_path.stem + "_opencv_temp.mp4")
    writer = cv2.VideoWriter(
        str(temp_path),
        cv2.VideoWriter_fourcc(*"mp4v"),
        fps,
        (PANEL_SIZE[0] * 2, PANEL_SIZE[1]),
    )
    if not writer.isOpened():
        print(f"Failed to create output temp video: {temp_path}", file=sys.stderr)
        return 2

    print(f"manny={manny_path}")
    print(f"bones={bones_path} samples={len(samples)} first={samples[0]['t']:.3f} last={samples[-1]['t']:.3f}")
    print(f"right_overlay={right_path}")
    print(
        f"frames={total_frames} fps={fps:.3f} time_offset={args.time_offset:.3f} "
        f"raw_offset=({args.raw_x_offset:.1f},{args.raw_y_offset:.1f})"
    )

    frame_index = 0
    while frame_index < total_frames:
        ok_manny, manny_frame = manny_cap.read()
        ok_right, right_frame = right_cap.read()
        if not ok_manny or not ok_right:
            break
        timestamp_s = frame_index / fps
        sample_time = max(0.0, timestamp_s + args.time_offset)
        raw_points = interpolate_points(samples, sample_time)
        left_panel = crop_and_fit_manny(manny_frame)
        right_panel = fit_right(right_frame)
        draw_bones(left_panel, raw_points, frame_index, timestamp_s, sample_time, args.raw_x_offset, args.raw_y_offset)
        writer.write(np.hstack([left_panel, right_panel]))
        frame_index += 1
        if frame_index % 60 == 0 or frame_index == total_frames:
            print(f"processed {frame_index}/{total_frames}")

    writer.release()
    manny_cap.release()
    right_cap.release()

    if frame_index == 0:
        print("No frames written", file=sys.stderr)
        return 2

    if transcode(temp_path, output_path, right_path):
        temp_path.unlink(missing_ok=True)
    else:
        temp_path.replace(output_path)
    print(f"wrote={output_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
