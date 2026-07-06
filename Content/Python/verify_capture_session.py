"""Capture-session verification: prove every stream is armed and landed.

Two checkpoints, run via MCP or the py console:

    import verify_capture_session as v
    v.preflight_live()   # ~10s after VR Preview starts, BEFORE the sync clap
    v.post_take()        # right after the take ends, BEFORE gear comes off

preflight_live reads the SPAWNED webcam source's tracker component - not the
CVar - because take 2 (2026-07-04) lost its entire hand stream to exactly that
gap: the setting looked plausible but the sensor was never enabled at spawn.
post_take parses the recorder's own analysis sidecar and issues a PASS or
RE-RECORD verdict per stream while re-recording is still cheap.
"""

import glob
import json
import os

import unreal

DIAG_DIR = os.path.join(
    unreal.SystemLibrary.get_project_saved_directory(), "CodexAgent", "Diagnostics")

# Minimum fraction of samples carrying each stream. body/hips/chains stream
# continuously; camera hands are only detectable when hands are in frame, so
# the floor is lower - take 2 scored 0.0 there, which is the failure class.
STREAM_FLOORS = {
    "hmd": 0.95,
    "left_arm_chain": 0.90,
    "right_arm_chain": 0.90,
    "body_hips": 0.90,
    "body_pose": 0.90,
    "camera_hands": 0.30,
}


def _log(msg):
    unreal.log(f"[CaptureVerify] {msg}")


def preflight_live():
    """Verify the live sensors on the actually-spawned actors. Run in PIE."""
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
    if not world:
        _log("FAIL: no PIE world - press VR Preview first, then rerun")
        return False
    sources = unreal.GameplayStatics.get_all_actors_of_class(
        world, unreal.MediaPipeQuestWebcamSourceActor)
    if not sources:
        _log("FAIL: no webcam source actor spawned - camera pipeline never started")
        return False
    ok = True
    for src in sources:
        trackers = src.get_components_by_class(unreal.MediaPipePoseTrackerComponent)
        if not trackers:
            _log(f"FAIL: {src.get_actor_label()} has no pose tracker component")
            ok = False
            continue
        for tracker in trackers:
            hands_on = bool(tracker.get_editor_property("enable_hand_landmarker"))
            state = "PASS" if hands_on else "FAIL - HAND STREAM WILL BE EMPTY (take-2 bug)"
            _log(f"{src.get_actor_label()}: hand landmarker enabled={hands_on} -> {state}")
            ok = ok and hands_on
    if ok:
        _log("PREFLIGHT PASS: sensors live on spawned actors - proceed to sync clap")
    else:
        _log("PREFLIGHT FAIL: STOP - do not record; fix and restart VR Preview")
    return ok


def post_take():
    """Parse the newest capture's analysis sidecar; verdict per stream."""
    candidates = glob.glob(os.path.join(
        DIAG_DIR, "tracking_fusion_dataset_mha_groundtruth_*_analysis.json"))
    if not candidates:
        _log("FAIL: no analysis json found - did the take finish and analyze=1 run?")
        return False
    newest = max(candidates, key=os.path.getmtime)
    _log(f"analyzing {os.path.basename(newest)}")
    with open(newest, "r", encoding="utf-8") as f:
        data = json.load(f)

    presence = data.get("stream_presence") or data.get("source_presence")
    verdict_ok = True
    if isinstance(presence, dict):
        for stream, floor in STREAM_FLOORS.items():
            frac = presence.get(stream)
            if frac is None:
                _log(f"{stream}: NOT IN ANALYSIS - treat as missing")
                verdict_ok = False
                continue
            frac = float(frac)
            frac = frac / 100.0 if frac > 1.0 else frac
            state = "PASS" if frac >= floor else f"FAIL (floor {floor:.0%})"
            _log(f"{stream}: {frac:.1%} -> {state}")
            verdict_ok = verdict_ok and frac >= floor
    else:
        # Analysis schema carries no per-stream presence: fall back to counting
        # directly from the raw samples files (slow but authoritative).
        _log("no presence block in analysis - counting raw samples directly")
        sample_files = sorted(glob.glob(newest.replace("_analysis.json", "_samples_*.jsonl")))
        if not sample_files:
            _log("FAIL: no samples files next to analysis json")
            return False
        totals = {k: 0 for k in STREAM_FLOORS}
        n = 0
        flags = {
            "hmd": ("hmd", "has_pose"),
            "left_arm_chain": ("left_arm_chain", "has_chain"),
            "right_arm_chain": ("right_arm_chain", "has_chain"),
            "body_hips": ("body_hips", "has_hips"),
            "body_pose": ("body_pose", "has_body_pose"),
            "camera_hands": ("camera_hands", "has_hands"),
        }
        for path in sample_files:
            with open(path, "r", encoding="utf-8") as f:
                for line in f:
                    try:
                        src = json.loads(line)["fusion"]["source"]
                    except Exception:  # noqa: BLE001 - count only parsable samples
                        continue
                    n += 1
                    for stream, (key, flag) in flags.items():
                        if src.get(key, {}).get(flag):
                            totals[stream] += 1
        if n == 0:
            _log("FAIL: zero parsable samples")
            return False
        for stream, floor in STREAM_FLOORS.items():
            frac = totals[stream] / n
            state = "PASS" if frac >= floor else f"FAIL (floor {floor:.0%})"
            _log(f"{stream}: {frac:.1%} of {n} samples -> {state}")
            verdict_ok = verdict_ok and frac >= floor
    if verdict_ok:
        _log("TAKE VERDICT: PASS - all streams landed; gear can come off")
    else:
        _log("TAKE VERDICT: RE-RECORD - a stream is missing; keep gear on")
    return verdict_ok
