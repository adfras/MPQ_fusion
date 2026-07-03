"""Summarize proportion-matrix replay measurements against the Kellan baseline.

Usage:
    python summarize_proportion_matrix.py [diagnostics_dir]

Finds the newest live_pie_bone_measure JSON per matrix label (pmTall, pmShort,
pmLongArms, pmShortLegs) plus the newest Kellan postHotfix* run, computes the
GATE-PIE metrics for each (reusing compare_replay_measurements.load_metrics),
and prints one table. Knee ANGLES should be roughly proportion-invariant;
positional metrics are expected to scale with limb length — this is a survey
table, not a pass/fail gate.
"""

import glob
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from compare_replay_measurements import load_metrics  # noqa: E402

DEFAULT_DIR = r"D:\Epic\Unreal_Projects\TestingKit5\Saved\CodexAgent\Diagnostics"

RUNS = [
    ("Kellan(base)", "live_pie_bone_measure_MP_LiveMetaHumanKellan_postHotfixRun2_*.json"),
    ("PM_Tall", "live_pie_bone_measure_MP_LiveMetaHumanPM_Tall_pmTall*.json"),
    ("PM_Short", "live_pie_bone_measure_MP_LiveMetaHumanPM_Short_pmShort*.json"),
    ("PM_LongArms", "live_pie_bone_measure_MP_LiveMetaHumanPM_LongArms_pmLongArms*.json"),
    ("PM_ShortLegs", "live_pie_bone_measure_MP_LiveMetaHumanPM_ShortLegs_pmShortLegs*.json"),
]

# (label, extractor) — knee_l/knee_r are (min, max) tuples in load_metrics
METRICS = [
    ("knee_l_min", lambda m: m["knee_l"][0]),
    ("knee_l_max", lambda m: m["knee_l"][1]),
    ("knee_r_min", lambda m: m["knee_r"][0]),
    ("knee_r_max", lambda m: m["knee_r"][1]),
    ("ball_median", lambda m: m["ball_median"]),
    ("ball_min", lambda m: m["ball_min"]),
    ("penetration_frames", lambda m: m["penetration_frames"]),
    ("segment_drift_cm", lambda m: m["segment_drift_cm"]),
    ("sample_count", lambda m: m["sample_count"]),
]


def newest(directory: str, pattern: str):
    matches = sorted(glob.glob(os.path.join(directory, pattern)), key=os.path.getmtime)
    return matches[-1] if matches else None


def main() -> int:
    directory = sys.argv[1] if len(sys.argv) > 1 else DEFAULT_DIR
    rows = {}
    for label, pattern in RUNS:
        path = newest(directory, pattern)
        if not path:
            print(f"note: no measurement file for {label} ({pattern})")
            continue
        rows[label] = load_metrics(path)

    if not rows:
        print("no measurement files found")
        return 1

    labels = list(rows)
    header = f"{'metric':<22}" + "".join(f"{label:>14}" for label in labels)
    print(header)
    print("-" * len(header))
    for key, extract in METRICS:
        line = f"{key:<22}"
        for label in labels:
            try:
                value = extract(rows[label])
            except (KeyError, TypeError, IndexError):
                value = None
            line += f"{value:>14.2f}" if isinstance(value, float) else f"{value!s:>14}"
        print(line)
    return 0


if __name__ == "__main__":
    sys.exit(main())
