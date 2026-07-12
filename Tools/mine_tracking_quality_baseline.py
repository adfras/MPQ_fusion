#!/usr/bin/env python3
"""Mine tracking-quality tracer rows out of a TestingKit5 session log.

TRACKING_QUALITY_PLAN.md Phase 0 (2026-07-11): builds the before-numbers fingerprint in
Docs/tracking_quality_baseline/ from (a) the 2026-07-10 worn acceptance log (pre-plan row
families: mp.ArmJumpTrace, mp.ArmDirCorrection, mp.ArmOverheadRescue, mp.QuestWristSolve,
mp.ChainReachExtend, mp.MediaPipeLegScaffold) and (b) any capture taken with the Phase 0
tracers armed (mp.FootSkateTrace, mp.WristLimitTrace, mp.WebcamAgeTrace). Re-run on later
phase captures to produce comparable summaries (Phase 1 residual A/B, Phase 3 knee raise,
Phase 4 weight shifts).

Plain file reads only - NEVER ripgrep on Saved/Logs (it silently returns nothing there).

Usage:
  python Tools/mine_tracking_quality_baseline.py <session.log> <out_dir> [--label NAME]

Writes <out_dir>/<label>_rows.jsonl (raw parsed rows, one JSON object per row) and
<out_dir>/<label>_summary.md (per-family stats).
"""

import argparse
import json
import math
import re
import sys
from collections import defaultdict
from pathlib import Path

FAMILIES = [
    "mp.FootSkateTrace",
    "mp.WristLimitTrace",
    "mp.WebcamAgeTrace",
    "mp.ArmJumpTrace",
    "mp.ArmDirCorrection",
    "mp.ArmOverheadRescue",
    "mp.QuestWristSolve",
    "mp.ChainReachExtend",
    "mp.MediaPipeLegScaffold",
]

# [2026.07.10-13.31.02:123][456]LogMediaPipePose: mp.Family: key=value ...
LINE_RE = re.compile(
    r"^\[(?P<ts>[\d.:-]+)\]\[\s*\d+\]LogMediaPipePose: (?P<family>mp\.[A-Za-z0-9]+)[: ]\s*(?P<body>.*)$"
)
KV_RE = re.compile(r"([A-Za-z_][A-Za-z0-9_]*)=([^\s]+)")


def parse_ts_seconds(ts: str):
    # 2026.07.10-13.31.02:123 -> seconds within the day (+ms)
    m = re.match(r"^\d{4}\.\d{2}\.\d{2}-(\d{2})\.(\d{2})\.(\d{2}):(\d{3})$", ts)
    if not m:
        return None
    h, mi, s, ms = (int(g) for g in m.groups())
    return h * 3600 + mi * 60 + s + ms / 1000.0


def try_float(v: str):
    try:
        return float(v)
    except ValueError:
        return v


def mine(log_path: Path):
    rows = []
    with log_path.open("r", encoding="utf-8", errors="replace") as f:
        for line in f:
            m = LINE_RE.match(line)
            if not m:
                continue
            family = m.group("family")
            if family not in FAMILIES:
                continue
            body = m.group("body")
            row = {"family": family, "t": parse_ts_seconds(m.group("ts"))}
            # Per-side blobs first (mp.MediaPipeLegScaffold "L(...) R(...)"): both sides
            # reuse the same key names, so a flat sweep would let R overwrite L.
            for side_tag in ("L", "R"):
                blob = re.search(rf"(?<![A-Za-z]){side_tag}\(([^)]*)\)", body)
                if blob:
                    for k, v in KV_RE.findall(blob.group(1)):
                        row[f"{side_tag}_{k}"] = try_float(v)
            # actor= may contain no spaces (FName); generic k=v sweep covers the rest.
            for k, v in KV_RE.findall(body):
                row.setdefault(k, try_float(v))
            rows.append(row)
    return rows


def pct(values, q):
    if not values:
        return float("nan")
    vals = sorted(values)
    idx = min(len(vals) - 1, max(0, int(round(q * (len(vals) - 1)))))
    return vals[idx]


def fmt(v):
    if isinstance(v, float):
        if math.isnan(v):
            return "n/a"
        return f"{v:.2f}"
    return str(v)


def stats_line(name, values):
    return (
        f"| {name} | {len(values)} | {fmt(pct(values, 0.5))} | {fmt(pct(values, 0.9))} | "
        f"{fmt(pct(values, 0.99))} | {fmt(max(values) if values else float('nan'))} |"
    )


def group(rows, family):
    return [r for r in rows if r["family"] == family]


def by_actor_side(rows):
    out = defaultdict(list)
    for r in rows:
        out[(r.get("actor", "?"), r.get("side", "?"))].append(r)
    return out


def numeric(rows, key):
    return [r[key] for r in rows if isinstance(r.get(key), float)]


def summarize(rows, label):
    lines = [f"# Tracking-quality baseline summary: {label}", ""]
    total_by_family = {fam: len(group(rows, fam)) for fam in FAMILIES}
    lines.append("## Row counts")
    lines.append("")
    lines.append("| family | rows |")
    lines.append("| ------ | ---- |")
    for fam, count in total_by_family.items():
        lines.append(f"| {fam} | {count} |")
    lines.append("")

    header = "| metric | n | p50 | p90 | p99 | max |"
    sep = "| ------ | - | --- | --- | --- | --- |"

    fs = group(rows, "mp.FootSkateTrace")
    if fs:
        lines += ["## mp.FootSkateTrace (foot-skate scoreboard)", "", header, sep]
        for (actor, side), rs in sorted(by_actor_side(fs).items()):
            planted = [r for r in rs if r.get("grounded") == 1.0]
            lines.append(stats_line(
                f"{actor} {side} planted planarSpdCmS (n_planted/{len(rs)})",
                numeric(planted, "planarSpdCmS")))
            lines.append(stats_line(f"{actor} {side} penetrCm", [
                v for v in numeric(rs, "penetrCm") if v >= 0.0]))
            lines.append(stats_line(f"{actor} {side} liftCm", numeric(rs, "liftCm")))
        lines.append("")

    wl = group(rows, "mp.WristLimitTrace")
    if wl:
        lines += ["## mp.WristLimitTrace (anatomical envelope, report-only)", "", header, sep]
        for (actor, side), rs in sorted(by_actor_side(wl).items()):
            out_rows = [r for r in rs if r.get("out") == 1.0]
            lines.append(stats_line(f"{actor} {side} |twistDeg|", [abs(v) for v in numeric(rs, "twistDeg")]))
            lines.append(stats_line(f"{actor} {side} swingDeg", numeric(rs, "swingDeg")))
            lines.append(stats_line(f"{actor} {side} twistExcessDeg (out rows: {len(out_rows)})", numeric(out_rows, "twistExcessDeg")))
            lines.append(stats_line(f"{actor} {side} swingExcessDeg", numeric(out_rows, "swingExcessDeg")))
        lines.append("")

    wa = group(rows, "mp.WebcamAgeTrace")
    if wa:
        lines += ["## mp.WebcamAgeTrace (measurement age + current-pose residuals)", "", header, sep]
        for (actor, side), rs in sorted(by_actor_side(wa).items()):
            measured = [r for r in rs if r.get("hasMpArm") == 1.0]
            quiet = [r for r in measured if r.get("quiet") == 1.0]
            moving = [r for r in measured if r.get("quiet") == 0.0]
            lines.append(stats_line(f"{actor} {side} ageMs", [v for v in numeric(rs, "ageMs") if v >= 0.0]))
            lines.append(stats_line(f"{actor} {side} predMs", [v for v in numeric(rs, "predMs") if v >= 0.0]))
            lines.append(stats_line(f"{actor} {side} effAgeMs", [v for v in numeric(rs, "effAgeMs") if v > -900.0]))
            lines.append(stats_line(f"{actor} {side} QUIET wristResidDeg", [v for v in numeric(quiet, "wristResidDeg") if v >= 0.0]))
            lines.append(stats_line(f"{actor} {side} MOVING wristResidDeg", [v for v in numeric(moving, "wristResidDeg") if v >= 0.0]))
            lines.append(stats_line(f"{actor} {side} MOVING elbowResidDeg", [v for v in numeric(moving, "elbowResidDeg") if v >= 0.0]))
        lines.append("")

    aj = group(rows, "mp.ArmJumpTrace")
    if aj:
        lines += ["## mp.ArmJumpTrace (pre-plan event fingerprint)", "", header, sep]
        for (actor, side), rs in sorted(by_actor_side(aj).items()):
            lines.append(stats_line(f"{actor} {side} residCm", numeric(rs, "residCm")))
        lines.append("")

    ad = group(rows, "mp.ArmDirCorrection")
    if ad:
        lines += ["## mp.ArmDirCorrection (pre-plan drift fingerprint)", "", header, sep]
        for (actor, side), rs in sorted(by_actor_side(ad).items()):
            lines.append(stats_line(f"{actor} {side} elbowCorrDeg", numeric(rs, "elbowCorrDeg")))
            lines.append(stats_line(f"{actor} {side} wristCorrDeg", numeric(rs, "wristCorrDeg")))
        lines.append("")

    ls = group(rows, "mp.MediaPipeLegScaffold")
    if ls:
        lines += ["## mp.MediaPipeLegScaffold (pre-plan leg fingerprint)", "", header, sep]
        for actor in sorted({r.get("actor", "?") for r in ls}):
            ars = [r for r in ls if r.get("actor") == actor]
            for side_tag in ("L", "R"):
                grounded = [r for r in ars if r.get(f"{side_tag}_grounded") == 1.0]
                lines.append(stats_line(
                    f"{actor} {side_tag} liftCm (grounded {len(grounded)}/{len(ars)})",
                    numeric(ars, f"{side_tag}_liftCm")))
        lines.append("")

    other = [f for f in ("mp.ArmOverheadRescue", "mp.QuestWristSolve", "mp.ChainReachExtend") if total_by_family.get(f)]
    if other:
        lines += ["## Cadence sanity (rows/side, starvation check)", ""]
        for fam in other:
            per = {k: len(v) for k, v in sorted(by_actor_side(group(rows, fam)).items())}
            lines.append(f"- {fam}: {per}")
        lines.append("")
    return "\n".join(lines) + "\n"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("log", type=Path)
    ap.add_argument("out_dir", type=Path)
    ap.add_argument("--label", default=None)
    args = ap.parse_args()

    label = args.label or args.log.stem
    args.out_dir.mkdir(parents=True, exist_ok=True)
    rows = mine(args.log)
    jsonl_path = args.out_dir / f"{label}_rows.jsonl"
    with jsonl_path.open("w", encoding="utf-8") as f:
        for r in rows:
            f.write(json.dumps(r) + "\n")
    summary_path = args.out_dir / f"{label}_summary.md"
    summary_path.write_text(summarize(rows, label), encoding="utf-8")
    print(f"{len(rows)} rows -> {jsonl_path}")
    print(f"summary -> {summary_path}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
