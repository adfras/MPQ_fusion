"""Plot Kellan replay live-PIE measurements and BodyFusion region-quality timelines.

Inputs:
  - one or two kellan_live_pie_bone_measure_*.json files (baseline and after), produced by
    Saved/CodexAgent/kellan_replay_bone_sampler.py during PIE replay
  - optional bodyfusion_region_quality_*.jsonl produced by mp.BodyFusion.RegionQualityCapture

Outputs PNG plots into --out-dir:
  knee_angles.png, pelvis_translation.png, foot_floor_delta.png, knee_forward_metrics.png
  region_ownership_timeline.png, region_confidence.png, region_depth_ratio.png (when JSONL given)

Usage:
  python Tools/PlotKellanReplayMeasurements.py --baseline a.json [--after b.json]
      [--region-quality r.jsonl] --out-dir Saved/CodexAgent/Diagnostics/plots_xyz
  python Tools/PlotKellanReplayMeasurements.py --selftest
"""

import argparse
import json
import os
import sys
import tempfile

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402

REGION_ORDER = ["head", "hands", "arms", "shoulders", "chest_spine", "pelvis_hips", "legs", "feet"]
OWNER_COLORS = {
    "Hmd": "#4C72B0",
    "Quest": "#55A868",
    "MediaPipe": "#C44E52",
    "Fused": "#8172B2",
    "AvatarProfile": "#CCB974",
    "None": "#BBBBBB",
    "Unknown": "#BBBBBB",
}


def load_measure(path):
    with open(path) as fh:
        data = json.load(fh)
    samples = data.get("samples", [])
    t0 = samples[0].get("wall_time", samples[0].get("game_time", 0.0)) if samples else 0.0

    def series(key):
        ts, vs = [], []
        for s in samples:
            v = s.get(key)
            if v is None:
                continue
            ts.append(s.get("wall_time", s.get("game_time", 0.0)) - t0)
            vs.append(v)
        return ts, vs

    return data, series


def overlay_plot(series_sets, keys, labels, title, ylabel, out_path, ref_lines=None):
    fig, ax = plt.subplots(figsize=(12, 5))
    styles = ["-", "--"]
    for set_index, (name, series) in enumerate(series_sets):
        for key, label in zip(keys, labels):
            ts, vs = series(key)
            if not ts:
                continue
            ax.plot(ts, vs, styles[set_index % len(styles)], linewidth=1.0,
                    label=f"{name} {label}")
    for ref_value, ref_label in (ref_lines or []):
        ax.axhline(ref_value, color="#999999", linewidth=0.8, linestyle=":")
        ax.annotate(ref_label, xy=(0.99, ref_value), xycoords=("axes fraction", "data"),
                    fontsize=7, ha="right", va="bottom", color="#666666")
    ax.set_title(title)
    ax.set_xlabel("capture time (s)")
    ax.set_ylabel(ylabel)
    ax.legend(fontsize=8, ncol=2)
    ax.grid(True, alpha=0.3)
    fig.tight_layout()
    fig.savefig(out_path, dpi=110)
    plt.close(fig)


def load_region_quality(path):
    rows = []
    with open(path) as fh:
        for line in fh:
            line = line.strip()
            if not line:
                continue
            try:
                rows.append(json.loads(line))
            except json.JSONDecodeError:
                continue
    return rows


def plot_region_quality(rows, out_dir, actor=None):
    if actor is None and rows:
        actors = sorted({r.get("actor", "") for r in rows})
        actor = actors[0]
    rows = [r for r in rows if r.get("actor") == actor]
    if not rows:
        return []
    t0 = min(r["t"] for r in rows)
    written = []

    # Ownership timeline: one horizontal band per region, colored by owner; hatched when
    # the region may not influence the visible pose (diagnostics-only).
    fig, ax = plt.subplots(figsize=(12, 5))
    for region_index, region in enumerate(REGION_ORDER):
        region_rows = sorted((r for r in rows if r.get("region") == region), key=lambda r: r["t"])
        for row in region_rows:
            color = OWNER_COLORS.get(row.get("owner", "Unknown"), "#BBBBBB")
            hatch = None if row.get("may_influence") else "////"
            ax.barh(region_index, 0.12, left=row["t"] - t0, height=0.8,
                    color=color, edgecolor="none", hatch=hatch)
    ax.set_yticks(range(len(REGION_ORDER)))
    ax.set_yticklabels(REGION_ORDER)
    ax.set_xlabel("time (s)")
    ax.set_title(f"BodyFusion region ownership timeline ({actor}); hatched = diagnostics-only (no pose influence)")
    handles = [plt.Rectangle((0, 0), 1, 1, color=c) for c in OWNER_COLORS.values()]
    ax.legend(handles, OWNER_COLORS.keys(), fontsize=8, ncol=3, loc="upper right")
    fig.tight_layout()
    path = os.path.join(out_dir, "region_ownership_timeline.png")
    fig.savefig(path, dpi=110)
    plt.close(fig)
    written.append(path)

    def line_per_region(value_key, title, ylabel, filename):
        fig2, ax2 = plt.subplots(figsize=(12, 5))
        for region in REGION_ORDER:
            region_rows = sorted((r for r in rows if r.get("region") == region), key=lambda r: r["t"])
            if not region_rows:
                continue
            ax2.plot([r["t"] - t0 for r in region_rows],
                     [r.get(value_key, 0.0) for r in region_rows],
                     linewidth=1.0, label=region)
        ax2.set_title(title)
        ax2.set_xlabel("time (s)")
        ax2.set_ylabel(ylabel)
        ax2.legend(fontsize=8, ncol=4)
        ax2.grid(True, alpha=0.3)
        fig2.tight_layout()
        out_path = os.path.join(out_dir, filename)
        fig2.savefig(out_path, dpi=110)
        plt.close(fig2)
        written.append(out_path)

    line_per_region("conf", f"Region confidence ({actor})", "confidence", "region_confidence.png")
    line_per_region("depth_var_ratio", f"Region forward/lateral variance ratio ({actor}) - monocular depth weakness",
                    "forward var / lateral var", "region_depth_ratio.png")
    line_per_region("amp_cm", f"Region motion amplitude ({actor})", "amplitude (cm)", "region_amplitude.png")
    return written


def make_measure_plots(baseline_path, after_path, out_dir):
    series_sets = []
    base_data, base_series = load_measure(baseline_path)
    series_sets.append(("baseline", base_series))
    if after_path:
        _, after_series = load_measure(after_path)
        series_sets.append(("after", after_series))

    written = []

    def emit(keys, labels, title, ylabel, filename, ref_lines=None):
        path = os.path.join(out_dir, filename)
        overlay_plot(series_sets, keys, labels, title, ylabel, path, ref_lines)
        written.append(path)

    emit(["knee_angle_l", "knee_angle_r"], ["knee L", "knee R"],
         "Kellan knee angles during replay legs/feet blocks", "knee angle (deg; 180=straight)",
         "knee_angles.png")
    emit(["pelvis_z", "pelvis_y", "pelvis_x"], ["pelvis Z", "pelvis Y", "pelvis X"],
         "Kellan pelvis translation", "world position (cm)", "pelvis_translation.png")
    emit(["foot_l_z", "ball_l_z", "foot_r_z", "ball_r_z"],
         ["foot L", "ball L", "foot R", "ball R"],
         "Kellan foot/ball height above floor", "height above floor (cm)",
         "foot_floor_delta.png",
         ref_lines=[(0.0, "floor"), (5.89, "ball grounded ref"), (7.88, "foot grounded ref")])
    emit(["knee_l_forward_from_ball", "knee_r_forward_from_ball",
          "foot_l_forward_span", "foot_r_forward_span"],
         ["knee L fwd of ball", "knee R fwd of ball", "foot L span", "foot R span"],
         "Knee/heel alignment metrics (positive = forward of ball / toe ahead of heel)",
         "cm along actor forward", "knee_forward_metrics.png")
    return written


def selftest():
    with tempfile.TemporaryDirectory() as tmp:
        samples = []
        for index in range(120):
            t = index / 30.0
            samples.append({
                "game_time": 100.0 + t,
                "wall_time": t,
                "pelvis_x": 1.0, "pelvis_y": -160.0, "pelvis_z": 88.0 - 4.0 * (index % 30) / 30.0,
                "knee_angle_l": 170.0 - (index % 30), "knee_angle_r": 168.0 - (index % 25),
                "foot_l_z": 8.0, "ball_l_z": 6.0, "foot_r_z": 8.5, "ball_r_z": 6.2,
                "knee_l_forward_from_ball": -2.0, "knee_r_forward_from_ball": 1.0,
                "foot_l_forward_span": 3.0, "foot_r_forward_span": -3.0,
            })
        measure_path = os.path.join(tmp, "measure.json")
        with open(measure_path, "w") as fh:
            json.dump({"samples": samples, "ranges": {}}, fh)

        rq_path = os.path.join(tmp, "rq.jsonl")
        with open(rq_path, "w") as fh:
            for index in range(40):
                t = 50.0 + index / 10.0
                for region in REGION_ORDER:
                    lower = region in ("pelvis_hips", "legs", "feet")
                    fh.write(json.dumps({
                        "t": t, "actor": "TestActor", "region": region,
                        "owner": "MediaPipe" if lower else "Quest",
                        "state": "Fresh", "valid": 1, "conf": 0.9,
                        "amp_cm": 4.0, "speed_cm_s": 10.0, "dropouts": 0,
                        "fresh_ratio": 1.0, "depth_var_ratio": 5.0 if lower else 0.4,
                        "depth_weak": 1 if lower else 0,
                        "may_influence": 0 if lower else 1,
                        "reason": "avatar-locked replay" if lower else "owner=Quest",
                    }) + "\n")

        out_dir = os.path.join(tmp, "plots")
        os.makedirs(out_dir, exist_ok=True)
        written = make_measure_plots(measure_path, measure_path, out_dir)
        written += plot_region_quality(load_region_quality(rq_path), out_dir)
        missing = [p for p in written if not os.path.isfile(p) or os.path.getsize(p) <= 0]
        if missing or len(written) < 7:
            print("SELFTEST FAIL", missing, len(written))
            return 1
        print(f"SELFTEST OK ({len(written)} plots)")
        return 0


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--baseline")
    parser.add_argument("--after")
    parser.add_argument("--region-quality")
    parser.add_argument("--actor")
    parser.add_argument("--out-dir")
    parser.add_argument("--selftest", action="store_true")
    args = parser.parse_args()

    if args.selftest:
        sys.exit(selftest())

    if not args.baseline or not args.out_dir:
        parser.error("--baseline and --out-dir are required unless --selftest")
    os.makedirs(args.out_dir, exist_ok=True)
    written = make_measure_plots(args.baseline, args.after, args.out_dir)
    if args.region_quality:
        written += plot_region_quality(
            load_region_quality(args.region_quality), args.out_dir, args.actor)
    for path in written:
        print("WROTE", path)


if __name__ == "__main__":
    main()
