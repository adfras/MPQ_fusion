#!/usr/bin/env python3
"""TrueDepth range probe: is iPhone measured depth usable at body-framing distance?

Streams RGB + depth from the Record3D iOS app over USB, runs MediaPipe Pose on the
RGB, samples the depth map at every pose landmark, and accumulates per-landmark
quality stats auto-binned by subject distance. Answers, empirically, whether the
iPhone 15's front TrueDepth sensor delivers usable metric depth at the 2-3 m the
user stands for full-body capture - BEFORE any UE pipeline work.

Phone setup (one-time):
  1. Install the Record3D app; buy the USB streaming unlock in-app if prompted.
  2. Settings tab -> Live RGBD Video Streaming -> USB.
  3. Select the FRONT (FaceID/TrueDepth) camera on the Record tab.
  4. Connect the phone by cable, unlock it, tap "Trust This Computer" if asked.
  5. Press the red record/toggle button to start streaming, then run this script.

Usage:
  python truedepth_range_probe.py --preflight     # prove every stream lands, then exit
  python truedepth_range_probe.py                 # live probe + verdict on 'q'
  python truedepth_range_probe.py --log out.json  # also write session log

Live window: landmarks green = valid depth, red = hole. Stand facing the phone and
walk slowly from ~1 m back to ~3.5 m and around; press 'q' for the verdict table,
'r' to reset accumulated stats.
"""

import argparse
import json
import math
import sys
import time
from collections import deque
from threading import Event

import numpy as np

DEVICE_TYPE_TRUEDEPTH = 0
DEVICE_TYPE_LIDAR = 1

LANDMARK_GROUPS = {
    "head": list(range(0, 11)),
    "shoulders": [11, 12],
    "elbows": [13, 14],
    "wrists_hands": list(range(15, 23)),
    "hips": [23, 24],
    "knees": [25, 26],
    "ankles_feet": list(range(27, 33)),
}

# Groups that must be measurable for the depth-correction plan to be worth building:
# these carry the known depth-sensitive divergences (knee raises, squat pelvis drop,
# hands-on-hips contact).
VERDICT_GROUPS = ("shoulders", "hips", "knees", "wrists_hands")
VERDICT_MIN_VALID_FRAC = 0.80
VERDICT_MAX_NOISE_CM = 5.0

DEPTH_MIN_M = 0.15
DEPTH_MAX_M = 6.0
PATCH_HALF = 2  # 5x5 neighborhood
BIN_SIZE_M = 0.25
NOISE_WINDOW = 15  # ~0.5 s at 30 fps


def bin_key(depth_m):
    return round(depth_m / BIN_SIZE_M) * BIN_SIZE_M


class LandmarkStats:
    """Per-landmark accumulators, partitioned by subject-distance bin."""

    def __init__(self):
        # bin -> [attempts, valid_hits, sum_patch_spread_m, spread_samples,
        #         noise_samples_list]
        self.bins = {}
        self.recent = deque(maxlen=NOISE_WINDOW)  # (t, depth_m) of valid samples

    def add(self, dist_bin, depth_m, patch_valid_frac, patch_spread_m, now):
        entry = self.bins.setdefault(dist_bin, [0, 0, 0.0, 0, []])
        entry[0] += 1
        if depth_m is not None:
            entry[1] += 1
            entry[2] += patch_spread_m
            entry[3] += 1
            self.recent.append((now, depth_m))
            # Temporal noise from first differences of consecutive valid samples:
            # robust to the subject slowly walking (removes trend), needs a
            # reasonably continuous window.
            if len(self.recent) >= 8:
                times = [t for t, _ in self.recent]
                if times[-1] - times[0] < 1.5:  # window not torn by dropouts
                    vals = np.array([d for _, d in self.recent])
                    diffs = np.diff(vals)
                    entry[4].append(float(np.std(diffs) / math.sqrt(2.0)))
                    if len(entry[4]) > 400:
                        del entry[4][:200]
        else:
            self.recent.clear()


def sample_depth_patch(depth, u, v):
    """Median depth of the valid 5x5 patch at normalized (u, v). Returns
    (depth_m or None, patch_valid_frac, patch_spread_m)."""
    h, w = depth.shape[:2]
    px = int(round(u * (w - 1)))
    py = int(round(v * (h - 1)))
    if px < 0 or py < 0 or px >= w or py >= h:
        return None, 0.0, 0.0
    x0, x1 = max(0, px - PATCH_HALF), min(w, px + PATCH_HALF + 1)
    y0, y1 = max(0, py - PATCH_HALF), min(h, py + PATCH_HALF + 1)
    patch = depth[y0:y1, x0:x1].astype(np.float64).ravel()
    finite = patch[np.isfinite(patch)]
    valid = finite[(finite > DEPTH_MIN_M) & (finite < DEPTH_MAX_M)]
    total = patch.size if patch.size else 1
    if valid.size == 0:
        return None, 0.0, 0.0
    spread = float(np.percentile(valid, 90) - np.percentile(valid, 10))
    return float(np.median(valid)), valid.size / total, spread


class ProbeApp:
    def __init__(self):
        self.frame_event = Event()
        self.session = None
        self.stream_stopped = False

    def on_new_frame(self):
        self.frame_event.set()

    def on_stream_stopped(self):
        self.stream_stopped = True
        self.frame_event.set()

    def connect(self):
        from record3d import Record3DStream

        devices = Record3DStream.get_connected_devices()
        if not devices:
            print("PREFLIGHT FAIL: no Record3D device found over USB.")
            print("  - Is the iPhone connected by cable and unlocked?")
            print("  - Is the Record3D app open with USB streaming enabled")
            print("    (Settings -> Live RGBD Video Streaming -> USB) and the")
            print("    red record toggle pressed?")
            print("  - Windows needs iTunes' Apple Mobile Device Service")
            print("    (verified running on this machine).")
            return False
        print("Found %d device(s): %s" % (len(devices), ", ".join(str(d.udid) for d in devices)))
        self.session = Record3DStream()
        self.session.on_new_frame = self.on_new_frame
        self.session.on_stream_stopped = self.on_stream_stopped
        self.session.connect(devices[0])
        return True

    def wait_frame(self, timeout=5.0):
        got = self.frame_event.wait(timeout)
        self.frame_event.clear()
        return got and not self.stream_stopped

    def read_frame(self):
        rgb = self.session.get_rgb_frame()          # HxWx3 uint8, RGB order
        depth = self.session.get_depth_frame()      # HxW float32, meters
        conf = self.session.get_confidence_frame()  # HxW uint8 or empty (LiDAR-only)
        return rgb, depth, conf

    def intrinsics(self):
        m = self.session.get_intrinsic_mat()
        return {"fx": float(m.fx), "fy": float(m.fy), "tx": float(m.tx), "ty": float(m.ty)}

    def device_type_name(self):
        dt = self.session.get_device_type()
        return {DEVICE_TYPE_TRUEDEPTH: "TRUEDEPTH", DEVICE_TYPE_LIDAR: "LIDAR"}.get(int(dt), "UNKNOWN(%s)" % dt)


def run_preflight(app):
    """Prove every stream is landing. Exit code 0 only if all checks pass."""
    checks = []

    def check(name, ok, detail=""):
        checks.append(ok)
        print("  [%s] %s%s" % ("PASS" if ok else "FAIL", name, (" - " + detail) if detail else ""))
        return ok

    print("Preflight:")
    if not app.connect():
        return 1
    if not check("first frame arrives", app.wait_frame(10.0), "10 s timeout"):
        return 1

    rgb, depth, conf = app.read_frame()
    check("RGB stream", rgb is not None and rgb.size > 0,
          "shape=%s" % (None if rgb is None else str(rgb.shape)))
    check("depth stream", depth is not None and depth.size > 0,
          "shape=%s" % (None if depth is None else str(depth.shape)))
    conf_present = conf is not None and conf.size > 0
    print("  [INFO] confidence map: %s" % ("present shape=%s" % str(conf.shape) if conf_present else "absent (normal for TrueDepth)"))

    dev = app.device_type_name()
    check("device type known", dev in ("TRUEDEPTH", "LIDAR"), dev)
    if dev == "LIDAR":
        print("  [INFO] LiDAR device - this probe works, but the iPhone 15 plan assumed TrueDepth.")

    intr = app.intrinsics()
    check("intrinsics", intr["fx"] > 0 and intr["fy"] > 0,
          "fx=%.1f fy=%.1f cx=%.1f cy=%.1f" % (intr["fx"], intr["fy"], intr["tx"], intr["ty"]))

    if rgb is not None and depth is not None and rgb.size and depth.size:
        ra = rgb.shape[1] / rgb.shape[0]
        da = depth.shape[1] / depth.shape[0]
        check("RGB/depth aspect match", abs(ra - da) < 0.02, "rgb %.3f vs depth %.3f" % (ra, da))
        h, w = depth.shape[:2]
        center = depth[h // 4: 3 * h // 4, w // 4: 3 * w // 4]
        finite = center[np.isfinite(center)]
        valid = finite[(finite > DEPTH_MIN_M) & (finite < DEPTH_MAX_M)]
        frac = valid.size / max(center.size, 1)
        check("center depth has signal", frac > 0.2, "valid frac %.2f" % frac)

    # fps over ~2 s
    n, t0 = 0, time.monotonic()
    while time.monotonic() - t0 < 2.0:
        if app.wait_frame(1.0):
            n += 1
        else:
            break
    fps = n / max(time.monotonic() - t0, 1e-6)
    check("frame rate", fps >= 15.0, "%.1f fps" % fps)

    ok = all(checks)
    print("Preflight %s." % ("PASSED - safe to run the probe" if ok else "FAILED - fix the above before standing up"))
    return 0 if ok else 1


def build_report(stats, intr, dev_type, rgb_shape, depth_shape):
    """Aggregate per-group, per-distance-bin quality stats + verdict."""
    all_bins = sorted({b for lm in stats for b in lm.bins})
    report = {
        "device_type": dev_type,
        "rgb_shape": list(rgb_shape) if rgb_shape else None,
        "depth_shape": list(depth_shape) if depth_shape else None,
        "intrinsics": intr,
        "bin_size_m": BIN_SIZE_M,
        "verdict_thresholds": {
            "groups": list(VERDICT_GROUPS),
            "min_valid_frac": VERDICT_MIN_VALID_FRAC,
            "max_noise_cm": VERDICT_MAX_NOISE_CM,
        },
        "bins": [],
    }
    for b in all_bins:
        row = {"distance_m": b, "groups": {}}
        for group, indices in LANDMARK_GROUPS.items():
            attempts = valid = spread_n = 0
            spread_sum = 0.0
            noises = []
            for i in indices:
                e = stats[i].bins.get(b)
                if not e:
                    continue
                attempts += e[0]
                valid += e[1]
                spread_sum += e[2]
                spread_n += e[3]
                noises.extend(e[4])
            if attempts == 0:
                continue
            row["groups"][group] = {
                "samples": attempts,
                "valid_frac": round(valid / attempts, 3),
                "noise_cm_median": round(float(np.median(noises)) * 100.0, 2) if noises else None,
                "patch_spread_cm_mean": round(spread_sum / spread_n * 100.0, 2) if spread_n else None,
            }
        vg = [row["groups"].get(g) for g in VERDICT_GROUPS]
        vg = [g for g in vg if g and g["samples"] >= 30]
        if vg:
            row["usable"] = all(
                g["valid_frac"] >= VERDICT_MIN_VALID_FRAC
                and g["noise_cm_median"] is not None
                and g["noise_cm_median"] <= VERDICT_MAX_NOISE_CM
                for g in vg
            )
        else:
            row["usable"] = None
        report["bins"].append(row)
    return report


def print_report(report):
    print()
    print("=" * 78)
    print("TRUEDEPTH RANGE PROBE VERDICT  (device: %s)" % report["device_type"])
    print("usable bin = %s all >= %d%% valid AND temporal noise <= %.0f cm" % (
        "+".join(report["verdict_thresholds"]["groups"]),
        report["verdict_thresholds"]["min_valid_frac"] * 100,
        report["verdict_thresholds"]["max_noise_cm"],
    ))
    print("=" * 78)
    header = "%6s | %7s" % ("dist", "usable")
    for g in LANDMARK_GROUPS:
        header += " | %s" % g[:12].center(14)
    print(header)
    print("       |         " + " | ".join(["valid%  nz(cm)"] * len(LANDMARK_GROUPS)))
    print("-" * len(header))
    for row in report["bins"]:
        usable = {True: "YES", False: "no", None: "?"}[row["usable"]]
        line = "%5.2fm | %7s" % (row["distance_m"], usable)
        for g in LANDMARK_GROUPS:
            e = row["groups"].get(g)
            if not e:
                line += " | %14s" % "-"
            else:
                nz = ("%5.1f" % e["noise_cm_median"]) if e["noise_cm_median"] is not None else "    ?"
                line += " |  %4.0f%%  %s " % (e["valid_frac"] * 100, nz)
        print(line)
    print("=" * 78)
    usable_bins = [r["distance_m"] for r in report["bins"] if r["usable"]]
    measured_bins = [r for r in report["bins"] if r["usable"] is not None]
    if usable_bins:
        print("Usable distance range: %.2f m - %.2f m" % (min(usable_bins), max(usable_bins)))
        if max(usable_bins) >= 2.0:
            print("VERDICT: PROMISING at body-framing distance - UE integration is worth building.")
        else:
            print("VERDICT: usable only close-in (< 2 m) - full-body framing likely NOT covered.")
    elif not measured_bins:
        print("VERDICT: INSUFFICIENT DATA - no distance bin collected enough samples in the")
        print("verdict groups (%s). Make sure the FULL body (hips, knees," % "+".join(VERDICT_GROUPS))
        print("wrists in frame) is visible and hold each distance a few seconds, then re-run.")
    else:
        print("VERDICT: measured and FAILED - no distance bin met the usability bar. Depth")
        print("from this sensor is unlikely to beat MediaPipe's inferred depth at these")
        print("distances. Consider the ARKit body-tracking route or dedicated depth hardware.")


def run_probe(app, log_path):
    import cv2
    import mediapipe as mp

    if not app.connect():
        return 1
    if not app.wait_frame(10.0):
        print("No frames arriving (10 s). Run --preflight for diagnosis.")
        return 1

    dev_type = app.device_type_name()
    intr = app.intrinsics()
    print("Streaming from %s camera. Walk 1 m -> 3.5 m; 'q' = verdict, 'r' = reset stats." % dev_type)

    pose = mp.solutions.pose.Pose(model_complexity=1,
                                  min_detection_confidence=0.5,
                                  min_tracking_confidence=0.5)
    stats = [LandmarkStats() for _ in range(33)]
    frame_log = []
    rgb_shape = depth_shape = None
    frames = 0
    fps_t0, fps_n, fps = time.monotonic(), 0, 0.0

    while True:
        if not app.wait_frame(2.0):
            if app.stream_stopped:
                print("Stream stopped by device.")
                break
            continue
        rgb, depth, _conf = app.read_frame()
        if rgb is None or depth is None or not rgb.size or not depth.size:
            continue
        rgb_shape, depth_shape = rgb.shape, depth.shape
        now = time.monotonic()
        frames += 1
        fps_n += 1
        if now - fps_t0 >= 1.0:
            fps, fps_n, fps_t0 = fps_n / (now - fps_t0), 0, now

        result = pose.process(rgb)
        display = cv2.cvtColor(rgb, cv2.COLOR_RGB2BGR)
        hip_depth = None
        n_valid = 0

        if result.pose_landmarks:
            landmarks = result.pose_landmarks.landmark
            samples = []
            for i, lm in enumerate(landmarks):
                d, vfrac, spread = sample_depth_patch(depth, lm.x, lm.y)
                samples.append((d, vfrac, spread, lm))
            hip_ds = [samples[i][0] for i in (23, 24) if samples[i][0] is not None]
            if hip_ds:
                hip_depth = float(np.median(hip_ds))
            if hip_depth is not None:
                dist_bin = bin_key(hip_depth)
                for i, (d, vfrac, spread, lm) in enumerate(samples):
                    if lm.visibility < 0.5:
                        continue
                    stats[i].add(dist_bin, d, vfrac, spread, now)
                if log_path and frames % 5 == 0:
                    frame_log.append({
                        "t": round(now, 3),
                        "hip_depth_m": round(hip_depth, 3),
                        "landmarks": [
                            [i, round(lm.x, 4), round(lm.y, 4),
                             round(d, 3) if d is not None else None,
                             round(lm.visibility, 2)]
                            for i, (d, _vf, _sp, lm) in enumerate(samples)
                            if lm.visibility >= 0.5
                        ],
                    })
            h, w = display.shape[:2]
            for i, (d, _vf, _sp, lm) in enumerate(samples):
                if lm.visibility < 0.5:
                    continue
                color = (0, 200, 0) if d is not None else (0, 0, 255)
                if d is not None:
                    n_valid += 1
                cv2.circle(display, (int(lm.x * w), int(lm.y * h)), 4, color, -1)

        hud = "dist %s | valid %d/33 | %.0f fps | frames %d" % (
            ("%.2f m" % hip_depth) if hip_depth is not None else "?", n_valid, fps, frames)
        display = cv2.flip(display, 1)  # selfie mirror AFTER sampling+drawing
        cv2.putText(display, hud, (10, 26), cv2.FONT_HERSHEY_SIMPLEX, 0.7, (255, 255, 255), 2)
        cv2.imshow("TrueDepth range probe ('q' verdict, 'r' reset)", display)
        key = cv2.waitKey(1) & 0xFF
        if key == ord('q'):
            break
        if key == ord('r'):
            stats = [LandmarkStats() for _ in range(33)]
            frame_log = []
            print("Stats reset.")

    cv2.destroyAllWindows()
    pose.close()

    report = build_report(stats, intr, dev_type, rgb_shape, depth_shape)
    print_report(report)
    if log_path:
        payload = {"report": report, "frames": frame_log}
        with open(log_path, "w") as f:
            json.dump(payload, f, indent=1)
        print("Session log written: %s" % log_path)
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--preflight", action="store_true",
                        help="verify device + all streams land, then exit")
    parser.add_argument("--log", metavar="PATH", default=None,
                        help="write session log JSON (report + downsampled frames)")
    args = parser.parse_args()

    app = ProbeApp()
    if args.preflight:
        code = run_preflight(app)
    else:
        code = run_probe(app, args.log)
    if app.session is not None:
        try:
            app.session.disconnect()
        except Exception:
            pass
    return code


if __name__ == "__main__":
    sys.exit(main())
