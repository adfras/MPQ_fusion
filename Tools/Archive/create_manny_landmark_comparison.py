#!/usr/bin/env python3
"""Create a Manny-vs-MediaPipe comparison with landmarks drawn on both sides."""

from __future__ import annotations

import argparse
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

PANEL_SIZE = (1280, 720)

# Crop used from the Unreal editor viewport recording before fitting into the left panel.
MANNY_RAW_CROP = (160, 180, 440, 520)  # x, y, width, height

# Approximate Manny torso anchors in the left 1280x720 panel after the crop/fit.
# MediaPipe ids are used, so the camera-facing right shoulder is usually id 12.
MANNY_TARGET_ANCHORS = {
    11: (745.0, 228.0),
    12: (535.0, 228.0),
    23: (720.0, 488.0),
    24: (560.0, 488.0),
}


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--vp2", required=True, help="Original VP2 input video")
    parser.add_argument("--manny", required=True, help="Unreal Manny viewport recording")
    parser.add_argument("--right-overlay", required=True, help="VP2 video with MediaPipe overlay")
    parser.add_argument("--model", required=True, help="MediaPipe pose_landmarker .task path")
    parser.add_argument("--output", required=True, help="Output comparison video path")
    parser.add_argument("--min-visibility", type=float, default=0.20)
    parser.add_argument("--max-frames", type=int, default=0)
    return parser.parse_args()


def landmark_visibility(landmark) -> float:
    return float(getattr(landmark, "visibility", 1.0) or 0.0)


def landmark_presence(landmark) -> float:
    return float(getattr(landmark, "presence", 1.0) or 0.0)


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


def fit_right_overlay(frame: np.ndarray) -> np.ndarray:
    panel_w, panel_h = PANEL_SIZE
    return cv2.resize(frame, (panel_w, panel_h), interpolation=cv2.INTER_AREA)


def landmarks_to_source_points(landmarks, width: int, height: int) -> np.ndarray:
    return np.array([[lm.x * width, lm.y * height] for lm in landmarks], dtype=np.float32)


def visible_landmarks(landmarks, points: np.ndarray, width: int, height: int, min_visibility: float) -> list[bool]:
    visible = []
    for lm, point in zip(landmarks, points):
        x, y = point
        in_bounds = -width * 0.1 <= x <= width * 1.1 and -height * 0.1 <= y <= height * 1.1
        visible.append(
            landmark_visibility(lm) >= min_visibility
            and landmark_presence(lm) >= min_visibility
            and in_bounds
        )
    return visible


def estimate_manny_affine(source_points: np.ndarray, visible: list[bool], previous_affine: np.ndarray | None) -> np.ndarray | None:
    source = []
    target = []
    for index, target_point in MANNY_TARGET_ANCHORS.items():
        if index < len(source_points) and visible[index]:
            source.append(source_points[index])
            target.append(target_point)

    if len(source) >= 3:
        source_array = np.array(source, dtype=np.float32)
        target_array = np.array(target, dtype=np.float32)
        affine, _ = cv2.estimateAffine2D(source_array, target_array, method=cv2.LMEDS)
        if affine is not None:
            return affine.astype(np.float32)

    return previous_affine


def transform_points(source_points: np.ndarray, affine: np.ndarray | None) -> np.ndarray | None:
    if affine is None:
        return None
    ones = np.ones((source_points.shape[0], 1), dtype=np.float32)
    homo = np.hstack([source_points, ones])
    return (affine @ homo.T).T


def draw_landmarks_on_manny(
    panel: np.ndarray,
    transformed_points: np.ndarray | None,
    visible: list[bool],
    frame_index: int,
    timestamp_s: float,
    model_name: str,
) -> None:
    if transformed_points is None:
        draw_label_block(panel, [
            "Manny + VP2 landmarks",
            f"model: {model_name}",
            f"frame: {frame_index}  t: {timestamp_s:.2f}s",
            "NO LANDMARK TRANSFORM",
        ])
        return

    points = [(int(round(x)), int(round(y))) for x, y in transformed_points]
    line_layer = panel.copy()
    for a, b in POSE_CONNECTIONS:
        if a < len(points) and b < len(points) and visible[a] and visible[b]:
            cv2.line(line_layer, points[a], points[b], (40, 220, 255), 4, cv2.LINE_AA)
    cv2.addWeighted(line_layer, 0.72, panel, 0.28, 0, panel)

    for index, point in enumerate(points):
        if not visible[index]:
            continue
        color = IMPORTANT_LANDMARKS.get(index, ("", (40, 255, 120)))[1]
        radius = 7 if index in IMPORTANT_LANDMARKS else 4
        cv2.circle(panel, point, radius + 2, (0, 0, 0), -1, cv2.LINE_AA)
        cv2.circle(panel, point, radius, color, -1, cv2.LINE_AA)

    if visible[11] and visible[12]:
        cv2.line(panel, points[11], points[12], (0, 165, 255), 5, cv2.LINE_AA)
    if visible[0] and visible[11] and visible[12]:
        mid_shoulder = (
            (points[11][0] + points[12][0]) // 2,
            (points[11][1] + points[12][1]) // 2,
        )
        cv2.arrowedLine(panel, mid_shoulder, points[0], (0, 255, 255), 3, cv2.LINE_AA, tipLength=0.12)

    visible_count = sum(visible)
    draw_label_block(panel, [
        "Manny + VP2 landmarks",
        "landmarks remapped by shoulders/hips",
        f"model: {model_name}",
        f"frame: {frame_index}  t: {timestamp_s:.2f}s",
        f"visible: {visible_count}/33",
    ])


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


def transcode_with_audio(temp_path: Path, output_path: Path, audio_source: Path) -> bool:
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
    vp2_path = Path(args.vp2).resolve()
    manny_path = Path(args.manny).resolve()
    right_overlay_path = Path(args.right_overlay).resolve()
    model_path = Path(args.model).resolve()
    output_path = Path(args.output).resolve()
    output_path.parent.mkdir(parents=True, exist_ok=True)

    for path in [vp2_path, manny_path, right_overlay_path, model_path]:
        if not path.exists():
            print(f"Missing input: {path}", file=sys.stderr)
            return 2

    vp2_cap = cv2.VideoCapture(str(vp2_path))
    manny_cap = cv2.VideoCapture(str(manny_path))
    right_cap = cv2.VideoCapture(str(right_overlay_path))
    if not vp2_cap.isOpened() or not manny_cap.isOpened() or not right_cap.isOpened():
        print("Failed to open one or more videos", file=sys.stderr)
        return 2

    fps = vp2_cap.get(cv2.CAP_PROP_FPS) or 30.0
    vp2_width = int(vp2_cap.get(cv2.CAP_PROP_FRAME_WIDTH))
    vp2_height = int(vp2_cap.get(cv2.CAP_PROP_FRAME_HEIGHT))
    total_frames = int(min(
        vp2_cap.get(cv2.CAP_PROP_FRAME_COUNT) or 0,
        manny_cap.get(cv2.CAP_PROP_FRAME_COUNT) or 0,
        right_cap.get(cv2.CAP_PROP_FRAME_COUNT) or 0,
    ))
    if args.max_frames > 0:
        total_frames = min(total_frames, args.max_frames)

    temp_path = output_path.with_name(output_path.stem + "_opencv_temp.mp4")
    writer = cv2.VideoWriter(
        str(temp_path),
        cv2.VideoWriter_fourcc(*"mp4v"),
        fps,
        (PANEL_SIZE[0] * 2, PANEL_SIZE[1]),
    )
    if not writer.isOpened():
        print(f"Failed to create temp video: {temp_path}", file=sys.stderr)
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

    print(f"vp2={vp2_path}")
    print(f"manny={manny_path}")
    print(f"right_overlay={right_overlay_path}")
    print(f"model={model_path}")
    print(f"frames={total_frames} fps={fps:.3f}")

    previous_affine = None
    model_name = model_path.name
    frame_index = 0
    with PoseLandmarker.create_from_options(options) as landmarker:
        while frame_index < total_frames:
            ok_vp2, vp2_frame = vp2_cap.read()
            ok_manny, manny_frame = manny_cap.read()
            ok_right, right_frame = right_cap.read()
            if not ok_vp2 or not ok_manny or not ok_right:
                break

            timestamp_ms = int(round(frame_index * 1000.0 / fps))
            rgb = cv2.cvtColor(vp2_frame, cv2.COLOR_BGR2RGB)
            mp_image = mp.Image(image_format=mp.ImageFormat.SRGB, data=rgb)
            result = landmarker.detect_for_video(mp_image, timestamp_ms)

            left_panel = crop_and_fit_manny(manny_frame)
            right_panel = fit_right_overlay(right_frame)

            if result.pose_landmarks:
                pose = result.pose_landmarks[0]
                source_points = landmarks_to_source_points(pose, vp2_width, vp2_height)
                visible = visible_landmarks(pose, source_points, vp2_width, vp2_height, args.min_visibility)
                previous_affine = estimate_manny_affine(source_points, visible, previous_affine)
                transformed_points = transform_points(source_points, previous_affine)
                draw_landmarks_on_manny(
                    left_panel,
                    transformed_points,
                    visible,
                    frame_index,
                    timestamp_ms / 1000.0,
                    model_name,
                )
            else:
                draw_landmarks_on_manny(left_panel, None, [False] * 33, frame_index, timestamp_ms / 1000.0, model_name)

            writer.write(np.hstack([left_panel, right_panel]))
            frame_index += 1
            if frame_index % 60 == 0 or frame_index == total_frames:
                print(f"processed {frame_index}/{total_frames}")

    writer.release()
    vp2_cap.release()
    manny_cap.release()
    right_cap.release()

    if frame_index == 0:
        print("No frames were written", file=sys.stderr)
        return 2

    if transcode_with_audio(temp_path, output_path, right_overlay_path):
        temp_path.unlink(missing_ok=True)
    else:
        temp_path.replace(output_path)

    print(f"wrote={output_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
