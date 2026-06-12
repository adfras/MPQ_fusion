"""Check whether the canonical replay source rows carry full hand joints anywhere.

Prints the top-level key tree of a mid-recording sample (depth 3) and reports any
'hand_joints' objects found, with their per-hand validity over the recording.
"""

import json

PATH = (
    r"D:\Epic\Unreal_Projects\TestingKit5\Saved\CodexAgent\Diagnostics"
    r"\tracking_fusion_dataset_avatar_locked_sync_calibration_20260609_170656_replay_source.jsonl"
)


def tree(obj, depth, prefix=""):
    lines = []
    if depth <= 0 or not isinstance(obj, dict):
        return lines
    for k, v in obj.items():
        if isinstance(v, dict):
            lines.append(prefix + k + "/")
            lines.extend(tree(v, depth - 1, prefix + "  "))
        elif isinstance(v, list):
            lines.append(prefix + k + " [%d]" % len(v))
        else:
            lines.append(prefix + k)
    return lines


count = 0
joints_stats = {"n": 0, "lj": 0, "rj": 0, "lj_tracked": 0, "rj_tracked": 0}
example_done = False
with open(PATH) as f:
    for line in f:
        row = json.loads(line)
        count += 1
        t = row.get("t", -1)
        if 40 < t < 50 and not example_done:
            print("=== sample key tree at t=%.2f ===" % t)
            print("\n".join(tree(row, 4)))
            example_done = True

        def find_hand_joints(obj, path=""):
            found = []
            if isinstance(obj, dict):
                for k, v in obj.items():
                    if k == "hand_joints" and isinstance(v, dict):
                        found.append((path + "/" + k, v))
                    else:
                        found.extend(find_hand_joints(v, path + "/" + k))
            return found

        if 30 < t < 60:
            joints = find_hand_joints(row)
            if joints:
                joints_stats["n"] += 1
                for p, v in joints:
                    if "left" in p:
                        joints_stats["lj"] += 1
                        if v.get("tracked") or v.get("has_joints"):
                            joints_stats["lj_tracked"] += 1
                    if "right" in p:
                        joints_stats["rj"] += 1
                        if v.get("tracked") or v.get("has_joints"):
                            joints_stats["rj_tracked"] += 1

print("\nrows scanned: %d" % count)
print("hand_joints stats over 30-60 s: %s" % joints_stats)
