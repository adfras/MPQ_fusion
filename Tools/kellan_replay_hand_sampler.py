"""Replay live-PIE hand/finger sampler for the recorded Quest+MediaPipe replay map.

Samples wrist/finger bone positions every editor tick during PIE for CAPTURE_SECONDS after
seeking the dataset replay to SEEK_OFFSET_SECONDS. Writes one JSON per actor with per-sample
derived metrics:
  - middle/index finger curl angle (proximal->distal bend, degrees)
  - wrist flexion angle between the forearm axis and the hand->middle_01 axis (degrees)
Frozen fingers/wrists show near-zero range in these angles; replayed Quest hands show motion.

Usage (bridge run_unreal_python):
    import importlib.util
    spec = importlib.util.spec_from_file_location(
        'hands', 'D:/Epic/Unreal_Projects/TestingKit5/Tools/kellan_replay_hand_sampler.py')
    mod = importlib.util.module_from_spec(spec)
    mod.OUTPUT_LABEL = 'hands_after'
    mod.SEEK_OFFSET_SECONDS = 32.0
    mod.CAPTURE_SECONDS = 26.0
    spec.loader.exec_module(mod)
"""

import json
import math
import os
import time

import unreal

OUTPUT_LABEL = globals().get("OUTPUT_LABEL", "hands")
SEEK_OFFSET_SECONDS = float(globals().get("SEEK_OFFSET_SECONDS", 32.0))
CAPTURE_SECONDS = float(globals().get("CAPTURE_SECONDS", 26.0))
SETTLE_SECONDS = float(globals().get("SETTLE_SECONDS", 2.0))
ACTOR_LABELS = globals().get("ACTOR_LABELS", ["MP_LiveMetaHumanKellan"])

OUTPUT_DIR = r"D:\Epic\Unreal_Projects\TestingKit5\Saved\CodexAgent\Diagnostics"

HAND_BONES = [
    "lowerarm_l", "hand_l", "index_01_l", "index_03_l", "middle_01_l", "middle_03_l",
    "thumb_02_l", "pinky_01_l",
    "lowerarm_r", "hand_r", "index_01_r", "index_03_r", "middle_01_r", "middle_03_r",
    "thumb_02_r", "pinky_01_r",
]


def _log(msg):
    unreal.log("[HandSampler] {}".format(msg))


def _sub(a, b):
    return [a[0] - b[0], a[1] - b[1], a[2] - b[2]]


def _angle_deg(a, b):
    al = math.sqrt(sum(x * x for x in a))
    bl = math.sqrt(sum(x * x for x in b))
    if al <= 1e-6 or bl <= 1e-6:
        return None
    d = max(-1.0, min(1.0, sum(x * y for x, y in zip(a, b)) / (al * bl)))
    return math.degrees(math.acos(d))


class _State(object):
    def __init__(self):
        self.samples_by_actor = {}
        self.handle = None
        self.phase = "settle"
        self.elapsed = 0.0
        self.world = None
        self.targets = {}
        self.started_at = time.time()
        self.done = False


STATE = _State()


def _sample_actor(state, mesh):
    bones = {}
    for bone in HAND_BONES:
        if mesh.does_socket_exist(bone):
            v = mesh.get_socket_location(bone)
            bones[bone] = [v.x, v.y, v.z]
    if "hand_l" not in bones or "hand_r" not in bones:
        return None

    sample = {"wall_time": time.time() - state.started_at}
    for side in ("l", "r"):
        hand = bones.get("hand_" + side)
        forearm = bones.get("lowerarm_" + side)
        m1 = bones.get("middle_01_" + side)
        m3 = bones.get("middle_03_" + side)
        i1 = bones.get("index_01_" + side)
        i3 = bones.get("index_03_" + side)
        if hand and m1 and m3:
            sample["middle_curl_" + side] = _angle_deg(_sub(m1, hand), _sub(m3, m1))
        if hand and i1 and i3:
            sample["index_curl_" + side] = _angle_deg(_sub(i1, hand), _sub(i3, i1))
        if hand and forearm and m1:
            sample["wrist_flex_" + side] = _angle_deg(_sub(hand, forearm), _sub(m1, hand))
    return sample


def _write(state):
    ts = time.strftime("%Y%m%d_%H%M%S")
    keys = ["middle_curl_l", "middle_curl_r", "index_curl_l", "index_curl_r",
            "wrist_flex_l", "wrist_flex_r"]
    for label, samples in state.samples_by_actor.items():
        ranges = {}
        for key in keys:
            vals = [s[key] for s in samples if s.get(key) is not None]
            if vals:
                ranges[key] = {"min": min(vals), "max": max(vals), "range": max(vals) - min(vals)}
        path = os.path.join(OUTPUT_DIR, "live_pie_hand_measure_{}_{}_{}.json".format(label, OUTPUT_LABEL, ts))
        with open(path, "w") as fh:
            json.dump({
                "label": OUTPUT_LABEL,
                "actor": label,
                "seek_offset_seconds": SEEK_OFFSET_SECONDS,
                "capture_seconds": CAPTURE_SECONDS,
                "sample_count": len(samples),
                "ranges": ranges,
                "samples": samples,
            }, fh, indent=1)
        _log("WROTE {} samples={} ranges={}".format(
            path, len(samples), json.dumps({k: round(v["range"], 1) for k, v in ranges.items()})))


def _tick(dt):
    state = STATE
    if state.done:
        return
    try:
        state.elapsed += dt
        if state.phase == "settle":
            if state.elapsed >= SETTLE_SECONDS:
                state.phase = "capture"
                state.elapsed = 0.0
            return
        for label, (actor, mesh) in state.targets.items():
            sample = _sample_actor(state, mesh)
            if sample is not None:
                state.samples_by_actor.setdefault(label, []).append(sample)
        if state.elapsed >= CAPTURE_SECONDS:
            _finish(state)
    except Exception as exc:  # noqa: BLE001
        _finish(state, error=str(exc))


def _finish(state, error=None):
    state.done = True
    if state.handle is not None:
        unreal.unregister_slate_post_tick_callback(state.handle)
        state.handle = None
    if error:
        _log("ERROR {}".format(error))
        return
    _write(state)


def start():
    state = STATE
    editor_subsystem = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
    world = editor_subsystem.get_game_world()
    if world is None:
        _log("ERROR no PIE world")
        return
    for actor in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.Actor):
        try:
            label = actor.get_actor_label()
        except Exception:
            continue
        if label not in ACTOR_LABELS:
            continue
        for comp in actor.get_components_by_class(unreal.SkeletalMeshComponent):
            if comp.does_socket_exist("hand_l") and comp.does_socket_exist("middle_01_l"):
                state.targets[label] = (actor, comp)
                break
    if not state.targets:
        _log("ERROR no target actors found for {}".format(ACTOR_LABELS))
        return
    state.world = world
    unreal.SystemLibrary.execute_console_command(
        world, "mp.SeekTrackingFusionDatasetReplay {}".format(SEEK_OFFSET_SECONDS))
    state.phase = "settle"
    state.elapsed = 0.0
    state.handle = unreal.register_slate_post_tick_callback(_tick)
    _log("sampler registered seek={} settle={} capture={} targets={}".format(
        SEEK_OFFSET_SECONDS, SETTLE_SECONDS, CAPTURE_SECONDS, list(state.targets.keys())))


start()
