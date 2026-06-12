"""Find foot-lift (march/kick) peaks and toe-raise moments in the canonical replay source."""

import json

PATH = (
    r"D:\Epic\Unreal_Projects\TestingKit5\Saved\CodexAgent\Diagnostics"
    r"\tracking_fusion_dataset_avatar_locked_sync_calibration_20260609_170656_replay_source.jsonl"
)

rows = []
with open(PATH) as f:
    for line in f:
        r = json.loads(line)
        body = r.get("fusion", {}).get("source", {}).get("body_pose", {})
        if not body.get("has_body_pose"):
            continue
        lm = body.get("landmarks", {})

        def pos(name):
            e = lm.get(name, {})
            return e.get("pos") if e.get("valid") else None

        la, ra = pos("left_ankle"), pos("right_ankle")
        hmd = r.get("fusion", {}).get("source", {}).get("hmd", {})
        rows.append((r["t"], la[2] if la else None, ra[2] if ra else None,
                     hmd["loc"][2] if hmd.get("has_pose") else None))

# Approximate floor as the 1st percentile of ankle z over the recording.
all_ankle = sorted(z for _, l, r, _ in rows for z in (l, r) if z is not None)
floor = all_ankle[len(all_ankle) // 100]
print("approx ankle floor z = %.1f" % floor)

# Foot-lift peaks: local maxima of per-sample max ankle height above floor, > 12 cm, min 1.5 s apart.
peaks = []
for i in range(2, len(rows) - 2):
    t, l, r, hmd = rows[i]
    if l is None or r is None:
        continue
    lift = max(l, r) - floor
    if lift < 12.0:
        continue
    window = [max(x[1] or -999, x[2] or -999) for x in rows[i - 2:i + 3]]
    if max(l, r) >= max(window) and (not peaks or t - peaks[-1][0] > 1.5):
        side = "L" if l > r else "R"
        peaks.append((t, lift, side, hmd))

for t, lift, side, hmd in peaks:
    print("t=%7.2f  lift=%5.1f cm  side=%s  hmdZ=%s" % (t, lift, side, "%.1f" % hmd if hmd else "-"))
