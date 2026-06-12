"""Measure foot-bone pitch (ankle->ball slope) in live_pie_bone_measure JSONs.

The foot bone axis naturally slopes down from the ankle joint to the ball. A grounded foot
should keep the reference slope; a toe-up artifact shows as the per-frame slope being
shallower (less negative pitch) than the reference while the ball is near the floor.

Reports per side: reference pitch proxy (pitch at the most-extended standing frame),
grounded-frame pitch median/p95 delta vs that reference, and worst toe-up delta.

Usage: python Tools/check_foot_pitch.py <file.json> [more.json ...]
"""

import json
import math
import sys


def analyze(path):
    with open(path) as fh:
        data = json.load(fh)
    samples = data["samples"]
    print("== %s (%s)" % (data.get("actor"), data.get("label")))

    for side in ("l", "r"):
        foot_k, ball_k = "foot_" + side, "ball_" + side
        knee_k = "knee_angle_" + side
        rows = [s for s in samples
                if foot_k in s.get("bones", {}) and ball_k in s.get("bones", {})
                and s.get(knee_k) is not None and s.get("ball_%s_z" % side) is not None]

        def pitch(s):
            f, b = s["bones"][foot_k], s["bones"][ball_k]
            dx = math.sqrt((b[0] - f[0]) ** 2 + (b[1] - f[1]) ** 2)
            return math.degrees(math.atan2(b[2] - f[2], max(dx, 1e-3)))

        # Reference proxy: the most knee-extended frame with the ball on the floor.
        standing = max((s for s in rows if s["ball_%s_z" % side] < 1.5),
                       key=lambda s: s[knee_k], default=None)
        if standing is None:
            print("  %s: no grounded standing frame" % side.upper())
            continue
        ref_pitch = pitch(standing)

        grounded = [s for s in rows if s["ball_%s_z" % side] < 1.5]
        deltas = sorted(pitch(s) - ref_pitch for s in grounded)
        n = len(deltas)
        med = deltas[n // 2]
        p95 = deltas[min(int(n * 0.95), n - 1)]
        worst = deltas[-1]
        print("  %s: refPitch=%.1f deg  grounded n=%d  deltaPitch median=%.1f p95=%.1f worstToeUp=%.1f" % (
            side.upper(), ref_pitch, n, med, p95, worst))


for arg in sys.argv[1:]:
    analyze(arg)
