"""Verify avatar leg proportions and knee placement in live_pie_bone_measure JSONs.

Reports, per file:
  - thigh segment length |thigh -> calf| and calf segment length |calf -> foot| (min/max)
  - thigh:calf ratio (the skeleton's own proportions; must match across baseline/candidate)
  - knee height fraction at the most-extended sample: (kneeZ-ankleZ)/(hipZ-ankleZ)
    (how far up the leg the knee sits when standing; lower fraction = knee visually lower)
  - same fraction at the deepest-bend sample for context

Usage: python Tools/check_leg_proportions.py <file1.json> [file2.json ...]
"""

import json
import math
import sys


def dist(a, b):
    return math.sqrt(sum((x - y) ** 2 for x, y in zip(a, b)))


def analyze(path):
    with open(path) as fh:
        data = json.load(fh)
    samples = data["samples"]

    print("== %s  (%s, %s samples)" % (data.get("actor"), data.get("label"), len(samples)))
    for side in ("l", "r"):
        thigh_k, calf_k, foot_k = "thigh_" + side, "calf_" + side, "foot_" + side
        rows = [s for s in samples if all(k in s.get("bones", {}) for k in (thigh_k, calf_k, foot_k))]
        thigh_lens = [dist(s["bones"][thigh_k], s["bones"][calf_k]) for s in rows]
        calf_lens = [dist(s["bones"][calf_k], s["bones"][foot_k]) for s in rows]

        knee_key = "knee_angle_" + side
        angled = [s for s in rows if s.get(knee_key) is not None]
        most_extended = max(angled, key=lambda s: s[knee_key])
        deepest = min(angled, key=lambda s: s[knee_key])

        def knee_fraction(s):
            hip = s["bones"][thigh_k]
            knee = s["bones"][calf_k]
            ankle = s["bones"][foot_k]
            denom = hip[2] - ankle[2]
            if abs(denom) < 1e-3:
                return None
            return (knee[2] - ankle[2]) / denom

        print("  %s: thigh %.2f..%.2f cm  calf %.2f..%.2f cm  thigh:calf = %.4f" % (
            side.upper(), min(thigh_lens), max(thigh_lens), min(calf_lens), max(calf_lens),
            (sum(thigh_lens) / len(thigh_lens)) / (sum(calf_lens) / len(calf_lens))))
        print("     most-extended sample: knee=%.1f deg  kneeZ fraction=%.3f" % (
            most_extended[knee_key], knee_fraction(most_extended)))
        print("     deepest-bend sample:  knee=%.1f deg  kneeZ fraction=%.3f" % (
            deepest[knee_key], knee_fraction(deepest)))


for arg in sys.argv[1:]:
    analyze(arg)
