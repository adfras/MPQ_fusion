"""List sustained deep-squat windows (HMD height < threshold) in the canonical replay source."""

import json

PATH = (
    r"D:\Epic\Unreal_Projects\TestingKit5\Saved\CodexAgent\Diagnostics"
    r"\tracking_fusion_dataset_avatar_locked_sync_calibration_20260609_170656_replay_source.jsonl"
)
THRESHOLD = 150.0

rows = []
with open(PATH) as f:
    for line in f:
        r = json.loads(line)
        hmd = r.get("fusion", {}).get("source", {}).get("hmd", {})
        if hmd.get("has_pose"):
            rows.append((r["t"], hmd["loc"][2]))

runs = []
start = None
for t, z in rows:
    if z < THRESHOLD:
        if start is None:
            start = t
    else:
        if start is not None:
            runs.append((start, t))
            start = None

for a, b in runs:
    if b - a > 1.0:
        zmin = min(z for t, z in rows if a <= t <= b)
        print("%7.2f .. %7.2f  (%4.1fs)  minZ=%.1f" % (a, b, b - a, zmin))
