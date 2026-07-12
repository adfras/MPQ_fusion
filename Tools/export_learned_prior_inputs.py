#!/usr/bin/env python3
"""Export HMD-Poser-shaped sparse inputs from the canonical replay dataset (v2 cache).

TRACKING_QUALITY_PLAN.md Phase 5 (2026-07-11): the learned-prior bake-off needs the
dataset's sparse observations (head 6DOF + both wrist 6DOF) as clean time series. This
exporter reads the schema-v2 replay cache (which carries the 26-keypoint Quest hand
skeletons with wrist orientation - the v1 cache is wrist-position-only) and writes an
.npz ready for the future offline HMD-Poser/EgoPoser run, plus a coverage report used
by the Phase 5 go/no-go memo.

Coordinate notes for the future run (do NOT skip):
- This export keeps UE conventions untouched: centimeters, Z-up, LEFT-handed, world
  space, quaternions (x, y, z, w).
- HMD-Poser (AMASS/SMPL-H) expects meters, Y-up, RIGHT-handed, and per-frame rotation
  matrices in its own canonical frame at 60 Hz. The dataset runs ~29.3 Hz, so the
  future harness must resample and convert; both transforms belong THERE so this
  export stays a faithful record of what the tracker actually saw.

Usage:
  python Tools/export_learned_prior_inputs.py <replay_source_v2.jsonl> <out.npz>
"""

import json
import sys
from pathlib import Path

import numpy as np

WRIST_KEYPOINT_INDEX = 0  # Quest hand skeleton: keypoint 0 is the wrist.


def export(jsonl_path: Path, out_path: Path) -> int:
    times = []
    head_pos = []
    head_quat = []
    head_valid = []
    hands = {"left": {"pos": [], "quat": [], "valid": []},
             "right": {"pos": [], "quat": [], "valid": []}}

    with jsonl_path.open("r", encoding="utf-8") as f:
        for line in f:
            d = json.loads(line)
            src = d.get("fusion", {}).get("source", {})
            times.append(float(d.get("t", -1.0)))
            hmd = src.get("hmd", {})
            head_valid.append(bool(hmd.get("has_pose")))
            head_pos.append(hmd.get("loc", [0.0, 0.0, 0.0]))
            head_quat.append(hmd.get("quat", [0.0, 0.0, 0.0, 1.0]))
            for side in ("left", "right"):
                h = src.get(f"{side}_hand", {})
                kp_world = h.get("keypoints_world") or []
                kp_quats = h.get("keypoint_quats") or []
                ok = bool(h.get("has_hand")) and len(kp_world) > WRIST_KEYPOINT_INDEX \
                    and len(kp_quats) > WRIST_KEYPOINT_INDEX
                hands[side]["valid"].append(ok)
                hands[side]["pos"].append(
                    kp_world[WRIST_KEYPOINT_INDEX] if ok else [0.0, 0.0, 0.0])
                hands[side]["quat"].append(
                    kp_quats[WRIST_KEYPOINT_INDEX] if ok else [0.0, 0.0, 0.0, 1.0])

    n = len(times)
    if n == 0:
        print("no samples found")
        return 1

    arrays = dict(
        t=np.asarray(times, dtype=np.float64),
        head_pos_cm=np.asarray(head_pos, dtype=np.float32),
        head_quat_xyzw=np.asarray(head_quat, dtype=np.float32),
        head_valid=np.asarray(head_valid, dtype=bool),
    )
    for side in ("left", "right"):
        arrays[f"{side}_wrist_pos_cm"] = np.asarray(hands[side]["pos"], dtype=np.float32)
        arrays[f"{side}_wrist_quat_xyzw"] = np.asarray(hands[side]["quat"], dtype=np.float32)
        arrays[f"{side}_wrist_valid"] = np.asarray(hands[side]["valid"], dtype=bool)
    np.savez_compressed(out_path, **arrays)

    dur = arrays["t"][-1] - arrays["t"][0]
    rate = (n - 1) / dur if dur > 0 else 0.0
    all_valid = arrays["head_valid"] & arrays["left_wrist_valid"] & arrays["right_wrist_valid"]
    print(f"samples={n} duration={dur:.1f}s rate={rate:.2f}Hz")
    print(f"coverage: head={arrays['head_valid'].mean():.1%} "
          f"leftWrist={arrays['left_wrist_valid'].mean():.1%} "
          f"rightWrist={arrays['right_wrist_valid'].mean():.1%} "
          f"all-three={all_valid.mean():.1%}")
    print(f"wrote {out_path}")
    return 0


if __name__ == "__main__":
    sys.exit(export(Path(sys.argv[1]), Path(sys.argv[2])))
