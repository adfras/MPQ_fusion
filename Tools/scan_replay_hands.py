"""Scan the canonical replay source for hand-observation coverage and shape.

Reports per 10 s bucket: left/right has_hand ratio, and for the first has_hand sample the
full JSON keys of the hand entry (to see whether full keypoints or only wrists were recorded).
"""

import json
from collections import defaultdict

PATH = (
    r"D:\Epic\Unreal_Projects\TestingKit5\Saved\CodexAgent\Diagnostics"
    r"\tracking_fusion_dataset_avatar_locked_sync_calibration_20260609_170656_replay_source.jsonl"
)

buckets = defaultdict(lambda: {"n": 0, "l": 0, "r": 0})
example = None
with open(PATH) as f:
    for line in f:
        row = json.loads(line)
        src = row.get("fusion", {}).get("source", {})
        b = int(row.get("t", -1) // 10) * 10
        agg = buckets[b]
        agg["n"] += 1
        lh, rh = src.get("left_hand", {}), src.get("right_hand", {})
        if lh.get("has_hand"):
            agg["l"] += 1
            if example is None:
                example = (row.get("t"), lh)
        if rh.get("has_hand"):
            agg["r"] += 1

print(f"{'t0':>5} {'n':>4} {'handL%':>7} {'handR%':>7}")
for b in sorted(buckets):
    a = buckets[b]
    print(f"{b:>5} {a['n']:>4} {100.0 * a['l'] / a['n']:>6.1f}% {100.0 * a['r'] / a['n']:>6.1f}%")

if example:
    t, lh = example
    print(f"\nfirst has_hand sample t={t:.2f}; left_hand keys: {sorted(lh.keys())}")
    for k, v in lh.items():
        desc = f"list[{len(v)}]" if isinstance(v, list) else type(v).__name__
        print(f"  {k}: {desc}")
else:
    print("\nNO has_hand samples found in the entire recording")
