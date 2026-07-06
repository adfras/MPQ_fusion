"""Referee scoreboard: fused parity replay vs Epic MHA solve, one take per run.

Usage: python take_score.py take3|take4 [--offset X.X]
Computes the full metric set identically for any take, so take3-vs-take4
deltas come from one implementation. Offset auto-fit by cross-correlation
of pelvis height + foot-rel signals unless --offset given.
"""
import json, glob, bisect, sys, math, statistics

DIAG = "D:/Epic/Unreal_Projects/TestingKit5/Saved/CodexAgent/Diagnostics"

CFG = {
    "take3": {
        "fused": f"{DIAG}/live_pie_bone_measure_MP_LiveMetaHumanKellan_fusedTake3round5_*.json",
        "mha": f"{DIAG}/live_pie_bone_measure_MHA_Solve_mhaSolveTake3full_*.json",
        "offset": -7.4,
    },
    "take4": {
        "fused": f"{DIAG}/live_pie_bone_measure_MP_LiveMetaHumanKellan_fusedTake4round1_*.json",
        "mha": f"{DIAG}/live_pie_bone_measure_MHA_Solve_mhaSolveTake4full_kellanized.json",
        "offset": None,  # fit
    },
    "take4r2": {  # candidate variant: consolidation + pelvis HMD anchor (+ palm trim once fitted)
        "fused": f"{DIAG}/live_pie_bone_measure_MP_LiveMetaHumanKellan_fusedTake4round2_*.json",
        "mha": f"{DIAG}/live_pie_bone_measure_MHA_Solve_mhaSolveTake4full_kellanized.json",
        "offset": None,  # fit (anchor changes pelvis path; refit to be safe)
    },
    "take4r3": {  # r2 + fitted palm trims (L +12.5 / R -6.3) + yaw clamp restored (A/B vs 720)
        "fused": f"{DIAG}/live_pie_bone_measure_MP_LiveMetaHumanKellan_fusedTake4round3_*.json",
        "mha": f"{DIAG}/live_pie_bone_measure_MHA_Solve_mhaSolveTake4full_kellanized.json",
        "offset": None,
    },
}

take = sys.argv[1]
cfg = CFG[take]
force_offset = None
if "--offset" in sys.argv:
    force_offset = float(sys.argv[sys.argv.index("--offset") + 1])

fused_path = sorted(glob.glob(cfg["fused"]))[-1]
mha_path = sorted(glob.glob(cfg["mha"]))[-1]
fused = json.load(open(fused_path))
mha = json.load(open(mha_path))

fs = fused["samples"]
ft0 = fs[0]["wall_time"]
for s in fs:
    s["_t"] = s["wall_time"] - ft0
ftimes = [s["_t"] for s in fs]
mt = [s["game_time"] for s in mha["samples"]]
CAP = min(205.0, ftimes[-1])

def fused_at(t):
    i = min(bisect.bisect_left(ftimes, t), len(fs) - 1)
    return fs[i]
def mha_at(t, off):
    i = min(bisect.bisect_left(mt, t + off), len(mt) - 1)
    return mha["samples"][i]

def bz(sample, bone):
    b = sample["bones"].get(bone)
    return b[2] if b else None
def bpos(sample, bone):
    return sample["bones"].get(bone)

# ---------- offset fit (pelvis z + feet-rel-pelvis, 1cm-normalized xcorr) ----------
def series(get_sample, tgrid, fn):
    return [fn(get_sample(t)) for t in tgrid]

def pel(s): return bz(s, "pelvis") or 0.0
def ftl(s): return (bz(s, "foot_l") or 0.0) - (bz(s, "pelvis") or 0.0)
def ftr(s): return (bz(s, "foot_r") or 0.0) - (bz(s, "pelvis") or 0.0)

if force_offset is not None:
    OFF = force_offset
elif cfg["offset"] is not None:
    OFF = cfg["offset"]
else:
    tgrid = [10 + i * 0.25 for i in range(int((CAP - 10) * 4))]
    best = (-1e9, None)
    for off10 in range(-200, 1, 2):  # -20.0 .. 0.0 step 0.2
        off = off10 / 10.0
        score = 0.0
        for fn in (pel, ftl, ftr):
            a = series(fused_at, tgrid, fn)
            b = [fn(mha_at(t, off)) for t in tgrid]
            ma, mb = statistics.mean(a), statistics.mean(b)
            sa, sb = statistics.pstdev(a), statistics.pstdev(b)
            if sa < 1e-6 or sb < 1e-6:
                continue
            score += sum((x - ma) * (y - mb) for x, y in zip(a, b)) / (len(a) * sa * sb)
        if score > best[0]:
            best = (score, off)
    OFF = best[1]
    # refine +-0.2 at 0.033 step
    fine = (-1e9, OFF)
    for k in range(-6, 7):
        off = OFF + k / 30.0
        score = 0.0
        tgrid2 = tgrid[::2]
        for fn in (pel, ftl, ftr):
            a = series(fused_at, tgrid2, fn)
            b = [fn(mha_at(t, off)) for t in tgrid2]
            ma, mb = statistics.mean(a), statistics.mean(b)
            sa, sb = statistics.pstdev(a), statistics.pstdev(b)
            if sa < 1e-6 or sb < 1e-6:
                continue
            score += sum((x - ma) * (y - mb) for x, y in zip(a, b)) / (len(a) * sa * sb)
        if score > fine[0]:
            fine = (score, off)
    OFF = fine[1]
    print(f"# fitted offset {OFF:.3f}s (corr sum {fine[0]:.3f}/3)")

# chunk seams (take4 solves): exclude +-1s around them from snap/smoothness metrics
SEAMS = [58.033, 116.067, 174.1] if take.startswith("take4") else []
def near_seam(t_mha):
    return any(abs(t_mha - s) < 1.0 for s in SEAMS)

# ---------- metric helpers ----------
def baseline(fn, get, off=None):
    vals = [fn(get(t) if off is None else get(t, off)) for t in [12 + i * 0.5 for i in range(30)]]
    return statistics.mean(vals)

def amp(vals):
    vs = sorted(vals)
    return vs[int(len(vs) * 0.97)] - vs[int(len(vs) * 0.03)]

def knee_angle(sample, side):
    th, ca, ft = bpos(sample, f"thigh_{side}"), bpos(sample, f"calf_{side}"), bpos(sample, f"foot_{side}")
    if not (th and ca and ft):
        return None
    v1 = [a - b for a, b in zip(th, ca)]
    v2 = [a - b for a, b in zip(ft, ca)]
    l1 = math.sqrt(sum(x * x for x in v1)); l2 = math.sqrt(sum(x * x for x in v2))
    if l1 < 1e-4 or l2 < 1e-4:
        return None
    return math.degrees(math.acos(max(-1, min(1, sum(a * b for a, b in zip(v1, v2)) / (l1 * l2)))))

def shoulder_yaw(sample):
    l, r = bpos(sample, "upperarm_l"), bpos(sample, "upperarm_r")
    if not (l and r):
        return None
    return math.degrees(math.atan2(l[1] - r[1], l[0] - r[0]))

def hdist(a, b):
    return math.hypot(a[0] - b[0], a[1] - b[1])

tgrid = [10 + i * 0.25 for i in range(int((CAP - 10) * 4))]
F = [fused_at(t) for t in tgrid]
M = [mha_at(t, OFF) for t in tgrid]

R = {}

# 1. divergence classes (baseline-relative), mean abs cm
for key, fn in [("wrist L (rel shoulder)", lambda s: (bz(s, "hand_l") or 0) - (bz(s, "upperarm_l") or 0)),
                ("wrist R (rel shoulder)", lambda s: (bz(s, "hand_r") or 0) - (bz(s, "upperarm_r") or 0)),
                ("pelvis height", pel),
                ("foot L (rel pelvis)", ftl),
                ("foot R (rel pelvis)", ftr)]:
    bf = statistics.mean([fn(fused_at(t)) for t in [12 + i * 0.5 for i in range(30)]])
    bm = statistics.mean([fn(mha_at(t, OFF)) for t in [12 + i * 0.5 for i in range(30)]])
    diffs = [abs((fn(f) - bf) - (fn(m) - bm)) for f, m in zip(F, M)]
    R[f"div {key} (cm)"] = f"{statistics.mean(diffs):.1f} (pk {max(diffs):.0f})"

# 2. knee angle agreement per side + amplitude ratios
for side in ("l", "r"):
    fa = [knee_angle(f, side) for f in F]
    ma_ = [knee_angle(m, side) for m in M]
    pairs = [(a, b) for a, b in zip(fa, ma_) if a is not None and b is not None]
    d = [abs(a - b) for a, b in pairs]
    xa = [a for a, _ in pairs]; xb = [b for _, b in pairs]
    mxa, mxb = statistics.mean(xa), statistics.mean(xb)
    sa, sb = statistics.pstdev(xa), statistics.pstdev(xb)
    corr = sum((a - mxa) * (b - mxb) for a, b in pairs) / (len(pairs) * sa * sb) if sa > 1e-6 and sb > 1e-6 else 0
    R[f"knee angle {side.upper()} MAE (deg)"] = f"{statistics.mean(d):.1f}"
    R[f"knee angle {side.upper()} corr"] = f"{corr:.2f}"
    R[f"knee angle {side.upper()} min f/m (deg)"] = f"{min(xa):.0f}/{min(xb):.0f}"
    fv = [v for v in ( (bz(f, f'foot_{side}') or 0) - (bz(f, 'pelvis') or 0) for f in F)]
    mv = [v for v in ( (bz(m, f'foot_{side}') or 0) - (bz(m, 'pelvis') or 0) for m in M)]
    R[f"foot raise amp {side.upper()} f/m (cm)"] = f"{amp(fv):.1f}/{amp(mv):.1f}"

# 3. squat depth (pelvis drop from own baseline, p97 amp)
bfp = statistics.mean([pel(fused_at(t)) for t in [12 + i * 0.5 for i in range(30)]])
bmp = statistics.mean([pel(mha_at(t, OFF)) for t in [12 + i * 0.5 for i in range(30)]])
R["squat depth f/m (cm)"] = f"{amp([pel(f)-bfp for f in F]):.1f}/{amp([pel(m)-bmp for m in M]):.1f}"

# 4. shoulder shrug amplitude (upperarm z rel spine_05)
for side in ("l", "r"):
    fv = [(bz(f, f"upperarm_{side}") or 0) - (bz(f, "spine_05") or 0) for f in F]
    mv = [(bz(m, f"upperarm_{side}") or 0) - (bz(m, "spine_05") or 0) for m in M]
    R[f"shrug amp {side.upper()} f/m (cm)"] = f"{amp(fv):.1f}/{amp(mv):.1f}"

# 5. knee separation (calf-to-calf horizontal)
fv = [hdist(bpos(f, "calf_l"), bpos(f, "calf_r")) for f in F if bpos(f, "calf_l") and bpos(f, "calf_r")]
mv = [hdist(bpos(m, "calf_l"), bpos(m, "calf_r")) for m in M if bpos(m, "calf_l") and bpos(m, "calf_r")]
R["knee sep mean f/m (cm)"] = f"{statistics.mean(fv):.1f}/{statistics.mean(mv):.1f}"
R["knee sep min f/m (cm)"] = f"{min(fv):.1f}/{min(mv):.1f}"

# 6. facing yaw error (fused yaw - mha yaw), mean abs deg
d = [abs(((shoulder_yaw(f) - shoulder_yaw(m)) + 180) % 360 - 180) for f, m in zip(F, M)
     if shoulder_yaw(f) is not None and shoulder_yaw(m) is not None]
R["facing yaw err (deg)"] = f"{statistics.mean(d):.1f}"

# 7. snaps: hand/foot frame jumps >8cm per 33ms (30hz resample), seam-excluded
snaps = 0
grid30 = [10 + i / 30.0 for i in range(int((CAP - 10) * 30))]
prev = None
for t in grid30:
    f = fused_at(t)
    cur = {b: bpos(f, b) for b in ("hand_l", "hand_r", "foot_l", "foot_r")}
    if prev is not None and not near_seam(t + OFF):
        for b, p in cur.items():
            q = prev.get(b)
            if p and q and math.dist(p, q) > 8.0:
                snaps += 1
    prev = cur
R["fused snaps (>8cm/33ms)"] = str(snaps)

# 8. pelvis wander (horizontal drift of 5s-lowpassed pelvis XY, max dev from start)
xs = [bpos(f, "pelvis") for f in F]
lp = []
w = 20  # 5s at 4hz
for i in range(len(xs)):
    seg = xs[max(0, i - w):i + 1]
    lp.append((statistics.mean(p[0] for p in seg), statistics.mean(p[1] for p in seg)))
R["pelvis wander max (cm)"] = f"{max(math.hypot(x - lp[0][0], y - lp[0][1]) for x, y in lp):.1f}"

mxs = [bpos(m, "pelvis") for m in M]
mlp = []
for i in range(len(mxs)):
    seg = mxs[max(0, i - w):i + 1]
    mlp.append((statistics.mean(p[0] for p in seg), statistics.mean(p[1] for p in seg)))
R["mha wander max (cm)"] = f"{max(math.hypot(x - mlp[0][0], y - mlp[0][1]) for x, y in mlp):.1f}"

# torso lateral lean vs Epic by quarter (the tilt-drift fix verification):
# spine_05 - pelvis X delta, fused minus epic, per quarter of the take
leans = []
for f, m in zip(F, M):
    fs5, fp = bpos(f, "spine_05"), bpos(f, "pelvis")
    ms5, mp_ = bpos(m, "spine_05"), bpos(m, "pelvis")
    if fs5 and fp and ms5 and mp_:
        leans.append((fs5[0] - fp[0]) - (ms5[0] - mp_[0]))
if leans:
    q = len(leans) // 4
    qvals = [statistics.mean(leans[i * q:(i + 1) * q]) for i in range(4)]
    R["torso lean delta Q1..Q4 (cm)"] = " / ".join(f"{v:+.1f}" for v in qvals)

# worst 10s windows per divergence class
win = []
for key, fn in [("wrist L", lambda s: (bz(s, "hand_l") or 0) - (bz(s, "upperarm_l") or 0)),
                ("wrist R", lambda s: (bz(s, "hand_r") or 0) - (bz(s, "upperarm_r") or 0)),
                ("foot L", ftl), ("foot R", ftr)]:
    bf = statistics.mean([fn(fused_at(t)) for t in [12 + i * 0.5 for i in range(30)]])
    bm = statistics.mean([fn(mha_at(t, OFF)) for t in [12 + i * 0.5 for i in range(30)]])
    for w0 in range(10, int(CAP) - 10, 10):
        d = [abs((fn(fused_at(w0 + i * 0.25)) - bf) - (fn(mha_at(w0 + i * 0.25, OFF)) - bm)) for i in range(40)]
        win.append((statistics.mean(d), key, w0))

print(f"## {take} scoreboard  (offset {OFF:.2f}s)")
print(f"fused: {fused_path.split('/')[-1].split(chr(92))[-1]}")
print(f"mha:   {mha_path.split('/')[-1].split(chr(92))[-1]}")
for k, v in R.items():
    print(f"  {k:38s} {v}")
print("  worst 10s windows:")
for m_, k, w0 in sorted(win, reverse=True)[:6]:
    print(f"    {k}: {m_:.1f}cm at {w0}-{w0+10}s")

json.dump({"take": take, "offset": OFF, "metrics": R},
          open(f"C:/Users/Alan/AppData/Local/Temp/claude/C--Users-Alan/c1a99dbf-2456-4f21-8427-24068ed350e4/scratchpad/score_{take}.json", "w"), indent=1)
