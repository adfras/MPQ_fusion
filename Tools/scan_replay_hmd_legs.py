"""Scan the canonical replay source dataset for HMD vs MediaPipe lower-body signal coverage.

Reports, per 10-second bucket:
- HMD pose presence ratio and Z range
- hip-center Z range (MediaPipe world)
- ankle/heel/toe Z range
- knee/ankle validity ratios

Usage: python Tools/scan_replay_hmd_legs.py [path_to_replay_source.jsonl]
"""

import json
import sys
from collections import defaultdict

DEFAULT_PATH = (
    r"D:\Epic\Unreal_Projects\TestingKit5\Saved\CodexAgent\Diagnostics"
    r"\tracking_fusion_dataset_avatar_locked_sync_calibration_20260609_170656_replay_source.jsonl"
)


def main() -> None:
    path = sys.argv[1] if len(sys.argv) > 1 else DEFAULT_PATH
    buckets = defaultdict(lambda: {
        "n": 0,
        "hmd_n": 0,
        "hmd_z": [],
        "hip_z": [],
        "ankle_z": [],
        "knee_valid": 0,
        "ankle_valid": 0,
        "body_n": 0,
        "phases": set(),
    })

    with open(path, "r", encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            row = json.loads(line)
            t = row.get("t", -1.0)
            b = int(t // 10) * 10
            src = row.get("fusion", {}).get("source", {})
            agg = buckets[b]
            agg["n"] += 1
            agg["phases"].add(row.get("phase", {}).get("phase_name", "?"))

            hmd = src.get("hmd", {})
            if hmd.get("has_pose"):
                agg["hmd_n"] += 1
                agg["hmd_z"].append(hmd["loc"][2])

            body = src.get("body_pose", {})
            if body.get("has_body_pose"):
                agg["body_n"] += 1
                lm = body.get("landmarks", {})

                def get(name):
                    e = lm.get(name, {})
                    if e.get("valid"):
                        return e.get("pos")
                    return None

                lh, rh = get("left_hip"), get("right_hip")
                if lh and rh:
                    agg["hip_z"].append((lh[2] + rh[2]) * 0.5)
                la, ra = get("left_ankle"), get("right_ankle")
                if la:
                    agg["ankle_z"].append(la[2])
                if ra:
                    agg["ankle_z"].append(ra[2])
                if get("left_knee") and get("right_knee"):
                    agg["knee_valid"] += 1
                if la and ra:
                    agg["ankle_valid"] += 1

    print(f"{'t0':>5} {'n':>4} {'hmd%':>6} {'hmdZ min..max':>16} {'hipZ min..max':>16} "
          f"{'ankZ min..max':>16} {'knee%':>6} {'ank%':>6}  phases")
    for b in sorted(buckets):
        a = buckets[b]
        def rng(vals):
            if not vals:
                return "-"
            return f"{min(vals):6.1f}..{max(vals):6.1f}"
        hmd_pct = 100.0 * a["hmd_n"] / max(a["n"], 1)
        knee_pct = 100.0 * a["knee_valid"] / max(a["body_n"], 1) if a["body_n"] else 0.0
        ank_pct = 100.0 * a["ankle_valid"] / max(a["body_n"], 1) if a["body_n"] else 0.0
        print(f"{b:>5} {a['n']:>4} {hmd_pct:>5.1f}% {rng(a['hmd_z']):>16} {rng(a['hip_z']):>16} "
              f"{rng(a['ankle_z']):>16} {knee_pct:>5.1f}% {ank_pct:>5.1f}%  {','.join(sorted(a['phases']))}")


if __name__ == "__main__":
    main()
