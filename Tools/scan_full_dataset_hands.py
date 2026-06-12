"""Check whether the ORIGINAL full tracking-fusion dataset rows carry full hand joints.

Scans samples_000.jsonl during the hands block (30-60 s) for
fusion.best_available.{left,right}_upper_limb.hand_joints content, and prints the structure
of one tracked example plus coverage stats.
"""

import json

PATH = (
    r"D:\Epic\Unreal_Projects\TestingKit5\Saved\CodexAgent\Diagnostics"
    r"\tracking_fusion_dataset_avatar_locked_sync_calibration_20260609_170656_samples_000.jsonl"
)

stats = {"rows": 0, "has_limb": 0, "joints_l": 0, "tracked_l": 0, "joints_r": 0, "tracked_r": 0}
example_done = False
with open(PATH) as f:
    for line in f:
        row = json.loads(line)
        t = row.get("t", row.get("time_seconds", -1))
        stats["rows"] += 1
        fusion = row.get("fusion", {})
        best = fusion.get("best_available", {})
        if not best:
            continue
        stats["has_limb"] += 1
        for side, jk, tk in (("left_upper_limb", "joints_l", "tracked_l"),
                             ("right_upper_limb", "joints_r", "tracked_r")):
            limb = best.get(side, {})
            hj = limb.get("hand_joints", {})
            if hj.get("has_joints"):
                stats[jk] += 1
                if hj.get("tracked"):
                    stats[tk] += 1
                if not example_done and side == "left_upper_limb" and hj.get("tracked"):
                    print("=== example left hand_joints at t=%s ===" % t)
                    print("limb keys:", sorted(limb.keys()))
                    print("hand_joints keys:", sorted(hj.keys()))
                    for k, v in hj.items():
                        if isinstance(v, list):
                            inner = " of list[%d]" % len(v[0]) if v and isinstance(v[0], list) else ""
                            print("  %s: list[%d]%s" % (k, len(v), inner))
                        else:
                            print("  %s: %r" % (k, v))
                    example_done = True
        if stats["rows"] >= 2500:  # samples_000 covers roughly the first ~70 s
            break

print("\nstats over first %d rows: %s" % (stats["rows"], stats))
