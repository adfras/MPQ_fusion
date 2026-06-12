"""Dump raw calf/foot/ball bone positions at extreme knee frames for foot-pitch debugging."""

import json
import sys

path = sys.argv[1]
with open(path) as fh:
    d = json.load(fh)

s_ext = max(d["samples"], key=lambda s: s.get("knee_angle_r") or 0)
s_deep = min(d["samples"], key=lambda s: s.get("knee_angle_r") or 999)
for name, s in (("most-extended", s_ext), ("deepest", s_deep)):
    b = s["bones"]
    print(name, "kneeR=%.1f" % s["knee_angle_r"], "floor_z=%.2f" % s["floor_z"],
          "wall_t=%.1f" % s.get("wall_time", -1))
    for k in ("calf_r", "foot_r", "ball_r", "calf_l", "foot_l", "ball_l"):
        if k in b:
            print("  %-7s %8.2f %8.2f %8.2f" % (k, b[k][0], b[k][1], b[k][2]))
