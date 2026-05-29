#!/usr/bin/env python3
"""Create a raw MediaPipe pose overlay video for inspection outside Unreal."""

from __future__ import annotations

import argparse
import os
import subprocess
import sys
from pathlib import Path

import cv2
import mediapipe as mp
import numpy as np


POSE_CONNECTIONS = [
    (0, 1), (1, 2), (2, 3), (3, 7),
    (0, 4), (4, 5), (5, 6), (6, 8),
    (9, 10),
    (11, 12),
    (11, 13), (13, 15), (15, 17), (15, 19), (15, 21), (17, 19),
    (12, 14), (14, 16), (16, 18), (16, 20), (16, 22), (18, 20),
    (11, 23), (12, 24), (23, 24),
    (23, 25), (25, 27), (27, 29), (29, 31), (27, 31),
    (24, 26), (26, 28), (28, 30), (30, 32), (28, 32),
]

IMPORTANT_LANDMARKS = {
    0: ("nose", (0, 255, 255)),
    7: ("ear_l", (255, 255, 0)),
    8: ("ear_r", (255, 255, 0)),
    11: ("shoulder_l", (0, 165, 255)),
    12: ("shoulder_r", (0, 165, 255)),
    13: ("elbow_l", (255, 120, 40)),
    14: ("elbow_r", (255, 120, 40)),
    15: ("wrist_l", (255, 0, 255)),
    16: ("wrist_r", (255, 0, 255)),
    23: ("hip_l", (180, 0, 255)),
    24: ("hip_r", (180, 0, 255)),
}


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", required=True, help="Input video path")
    parser.add_argument("--output", required=True, help="Output overlay MP4 path")
    parser.add_argument("--model", required=True, help="MediaPipe pose_landmarker .task path")
    parser.add_argument("--min-visibility", type=float, default=0.20)
    parser.add_argument("--codec", default="mp4v", help="OpenCV fourcc for temporary video")
    parser.add_argument("--max-frames", type=int, default=0, help="Optional frame limit for quick tests")
    return parser.parse_args()


def landmark_visibility(landmark) -> float:
    return float(getattr(landmark, "visibility", 1.0) or 0.0)


def landmark_presence(landmark) -> float:
    return float(getattr(landmark, "presence", 1.0) or 0.0)


def to_px(landmark, width: int, height: int) -> tuple[int, int]:
    return int(round(landmark.x * width)), int(round(landmark.y * height))


def in_frame(point: tuple[int, int], width: int, height: int) -> bool:
    x, y = point
    return -width * 0.1 <= x <= width * 1.1 and -height * 0.1 <= y <= height * 1.1


def draw_text_block(frame: np.ndarray, lines: list[str]) -> None:
    if not lines:
        return
    x, y = 18, 28
    font = cv2.FONT_HERSHEY_SIMPLEX
    scale = 0.58
    thickness = 1
    line_height = 24
    width = max(cv2.getTextSize(line, font, scale, thickness)[0][0] for line in lines) + 24
    height = line_height * len(lines) + 14
    overlay = frame.copy()
    cv2.rectangle(overlay, (x - 10, y - 22), (x - 10 + width, y - 22 + height), (0, 0, 0), -1)
    cv2.addWeighted(overlay, 0.52, frame, 0.48, 0, frame)
    for index, line in enumerate(lines):
        cv2.putText(frame, line, (x, y + index * line_height), font, scale, (245, 245, 245), thickness, cv2.LINE_AA)


def draw_pose(frame: np.ndarray, landmarks, timestamp_s: float, frame_index: int, model_name: str, min_visibility: float) -> None:
    height, width = frame.shape[:2]
    if not landmarks:
        draw_text_block(frame, [
            "MediaPipe Pose overlay",
            f"model: {model_name}",
            f"frame: {frame_index}  t: {timestamp_s:.2f}s",
            "NO POSE DETECTED",
        ])
        return

    pose = landmarks[0]
    points = [to_px(lm, width, height) for lm in pose]
    visible = [
        landmark_visibility(lm) >= min_visibility and landmark_presence(lm) >= min_visibility and in_frame(pt, width, height)
        for lm, pt in zip(pose, points)
    ]

    line_layer = frame.copy()
    for a, b in POSE_CONNECTIONS:
        if a < len(points) and b < len(points) and visible[a] and visible[b]:
            cv2.line(line_layer, points[a], points[b], (40, 220, 90), 3, cv2.LINE_AA)
    cv2.addWeighted(line_layer, 0.70, frame, 0.30, 0, frame)

    for index, point in enumerate(points):
        if not visible[index]:
            continue
        color = IMPORTANT_LANDMARKS.get(index, ("", (40, 255, 120)))[1]
        radius = 5 if index in IMPORTANT_LANDMARKS else 3
        cv2.circle(frame, point, radius + 2, (0, 0, 0), -1, cv2.LINE_AA)
        cv2.circle(frame, point, radius, color, -1, cv2.LINE_AA)

    for index, (name, color) in IMPORTANT_LANDMARKS.items():
        if index < len(points) and visible[index]:
            x, y = points[index]
            cv2.putText(frame, name, (x + 7, y - 7), cv2.FONT_HERSHEY_SIMPLEX, 0.42, color, 1, cv2.LINE_AA)

    if visible[11] and visible[12]:
        cv2.line(frame, points[11], points[12], (0, 165, 255), 4, cv2.LINE_AA)
    if visible[7] and visible[8]:
        cv2.line(frame, points[7], points[8], (255, 255, 0), 3, cv2.LINE_AA)
    if visible[0] and visible[11] and visible[12]:
        mid_shoulder = ((points[11][0] + points[12][0]) // 2, (points[11][1] + points[12][1]) // 2)
        cv2.arrowedLine(frame, mid_shoulder, points[0], (0, 255, 255), 2, cv2.LINE_AA, tipLength=0.12)

    vis_values = [landmark_visibility(lm) for lm in pose]
    visible_count = sum(visible)
    shoulder_vis = min(landmark_visibility(pose[11]), landmark_visibility(pose[12])) if len(pose) > 12 else 0.0
    head_indices = [0, 7, 8]
    head_vis = min(landmark_visibility(pose[i]) for i in head_indices if i < len(pose))
    wrist_vis_l = landmark_visibility(pose[15]) if len(pose) > 15 else 0.0
    wrist_vis_r = landmark_visibility(pose[16]) if len(pose) > 16 else 0.0

    draw_text_block(frame, [
        "MediaPipe Pose raw overlay",
        f"model: {model_name}",
        f"frame: {frame_index}  t: {timestamp_s:.2f}s",
        f"pose: yes  visible_landmarks: {visible_count}/33  avg_vis: {np.mean(vis_values):.2f}",
        f"head_vis: {head_vis:.2f}  shoulder_vis: {shoulder_vis:.2f}",
        f"wrist_vis L/R: {wrist_vis_l:.2f}/{wrist_vis_r:.2f}",
    ])


def transcode_with_ffmpeg(temp_path: Path, output_path: Path, source_path: Path) -> bool:
    ffmpeg_cmd = [
        "ffmpeg",
        "-y",
        "-hide_banner",
        "-loglevel",
        "error",
        "-i",
        str(temp_path),
        "-i",
        str(source_path),
        "-map",
        "0:v:0",
        "-map",
        "1:a?",
        "-c:v",
        "libx264",
        "-pix_fmt",
        "yuv420p",
        "-preset",
        "veryfast",
        "-crf",
        "20",
        "-c:a",
        "aac",
        "-shortest",
        str(output_path),
    ]
    try:
        subprocess.run(ffmpeg_cmd, check=True)
        return True
    except Exception as exc:
        print(f"ffmpeg transcode skipped/failed: {exc}", file=sys.stderr)
        return False


def main() -> int:
    args = parse_args()
    input_path = Path(args.input).resolve()
    output_path = Path(args.output).resolve()
    model_path = Path(args.model).resolve()
    output_path.parent.mkdir(parents=True, exist_ok=True)

    if not input_path.exists():
        print(f"Input video not found: {input_path}", file=sys.stderr)
        return 2
    if not model_path.exists():
        print(f"MediaPipe model not found: {model_path}", file=sys.stderr)
        return 2

    cap = cv2.VideoCapture(str(input_path))
    if not cap.isOpened():
        print(f"Failed to open video: {input_path}", file=sys.stderr)
        return 2

    fps = cap.get(cv2.CAP_PROP_FPS) or 30.0
    width = int(cap.get(cv2.CAP_PROP_FRAME_WIDTH))
    height = int(cap.get(cv2.CAP_PROP_FRAME_HEIGHT))
    total_frames = int(cap.get(cv2.CAP_PROP_FRAME_COUNT))
    if args.max_frames > 0:
        total_frames = min(total_frames, args.max_frames)

    temp_path = output_path.with_name(output_path.stem + "_opencv_temp.mp4")
    writer = cv2.VideoWriter(str(temp_path), cv2.VideoWriter_fourcc(*args.codec), fps, (width, height))
    if not writer.isOpened():
        print(f"Failed to create output video: {temp_path}", file=sys.stderr)
        return 2

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

    print(f"input={input_path}")
    print(f"model={model_path}")
    print(f"frames={total_frames} fps={fps:.3f} size={width}x{height}")
    print(f"temp={temp_path}")

    frame_index = 0
    model_name = model_path.name
    with PoseLandmarker.create_from_options(options) as landmarker:
        while True:
            ok, frame = cap.read()
            if not ok:
                break
            if args.max_frames > 0 and frame_index >= args.max_frames:
                break

            timestamp_ms = int(round(frame_index * 1000.0 / fps))
            rgb = cv2.cvtColor(frame, cv2.COLOR_BGR2RGB)
            mp_image = mp.Image(image_format=mp.ImageFormat.SRGB, data=rgb)
            result = landmarker.detect_for_video(mp_image, timestamp_ms)

            draw_pose(frame, result.pose_landmarks, timestamp_ms / 1000.0, frame_index, model_name, args.min_visibility)
            writer.write(frame)

            frame_index += 1
            if frame_index % 60 == 0 or frame_index == total_frames:
                print(f"processed {frame_index}/{total_frames}")

    cap.release()
    writer.release()

    if transcode_with_ffmpeg(temp_path, output_path, input_path):
        try:
            temp_path.unlink()
        except OSError:
            pass
    else:
        temp_path.replace(output_path)

    print(f"wrote={output_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
