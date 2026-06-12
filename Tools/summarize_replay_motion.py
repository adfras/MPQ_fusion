"""Distribution summary for live_pie_bone_measure JSONs (baseline vs candidate).

Reports pelvis-Z range/percentiles, knee-angle percentiles, knee-forward-of-ball max,
and foot lift maxima so a deliberate solve change can be judged on motion content
rather than single-frame extremes.

Usage: python Tools/summarize_replay_motion.py <baseline.json> <candidate.json>
"""

import json
import statistics
import sys


def pct(vals, p):
    vals = sorted(vals)
    if not vals:
        return None
    idx = min(int(len(vals) * p / 100.0), len(vals) - 1)
    return vals[idx]


def summarize(path):
    with open(path) as fh:
        data = json.load(fh)
    s = data["samples"]

    def series(key):
        return [x[key] for x in s if x.get(key) is not None]

    pelvis = series("pelvis_z")
    knees = series("knee_angle_l") + series("knee_angle_r")
    foot_l = series("foot_l_z")
    foot_r = series("foot_r_z")
    kfwd = series("knee_l_forward_from_ball") + series("knee_r_forward_from_ball")

    # Grounded-foot planar slide: per-sample planar speed of each ball while it stays near the
    # floor (ball_z < 2 cm in consecutive samples). p95 in cm/s; skating shows as large values.
    slide_speeds = []
    for side in ("l", "r"):
        ball_key = "ball_" + side
        z_key = "ball_%s_z" % side
        prev = None
        for sm in s:
            cur = sm.get("bones", {}).get(ball_key)
            t = sm.get("wall_time")
            z = sm.get(z_key)
            if cur is not None and prev is not None and z is not None and prev[2] is not None:
                dt = t - prev[1]
                if 0.001 < dt < 0.5 and z < 2.0 and prev[2] < 2.0:
                    dx = cur[0] - prev[0][0]
                    dy = cur[1] - prev[0][1]
                    slide_speeds.append((dx * dx + dy * dy) ** 0.5 / dt)
            prev = (cur, t, z)
    return {
        "label": data.get("label"),
        "actor": data.get("actor"),
        "n": data.get("sample_count"),
        "pelvis_min": min(pelvis),
        "pelvis_max": max(pelvis),
        "pelvis_range": max(pelvis) - min(pelvis),
        "pelvis_p05": pct(pelvis, 5),
        "knee_p01": pct(knees, 1),
        "knee_p05": pct(knees, 5),
        "knee_p25": pct(knees, 25),
        "knee_median": statistics.median(knees),
        "knee_stdev": statistics.pstdev(knees),
        "foot_l_max": max(foot_l),
        "foot_r_max": max(foot_r),
        "knee_fwd_ball_max": max(kfwd) if kfwd else None,
        "grounded_slide_p95": pct(slide_speeds, 95) if slide_speeds else None,
        "grounded_slide_max": max(slide_speeds) if slide_speeds else None,
    }


def main():
    base = summarize(sys.argv[1])
    cand = summarize(sys.argv[2])
    def fmt(v):
        return "%.2f" % v if isinstance(v, float) else str(v)

    print("%-20s %12s %12s" % ("metric", "baseline", "candidate"))
    for key in [k for k in base if k not in ("label", "actor")]:
        print("%-20s %12s %12s" % (key, fmt(base[key]), fmt(cand[key])))


if __name__ == "__main__":
    main()
