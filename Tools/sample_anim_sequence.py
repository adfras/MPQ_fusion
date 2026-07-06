"""Sample an AnimSequence into the live_pie_bone_measure JSON schema.

Editor-python sibling of kellan_replay_bone_sampler.py: instead of sampling a
live PIE actor per tick, it evaluates an AnimSequence offline at SAMPLE_HZ via
AnimPoseExtensions. Output is schema-compatible with the PIE sampler so
compare/summary tooling can consume both.

Usage (in-editor / MCP):
    import importlib.util
    spec = importlib.util.spec_from_file_location(
        'anim_sampler', 'D:/Epic/Unreal_Projects/TestingKit5/Tools/sample_anim_sequence.py')
    mod = importlib.util.module_from_spec(spec)
    mod.ANIM_PATH = '/Game/MHAGroundTruth/AS_MHA_Body_Take1'
    mod.OUTPUT_LABEL = 'mhaSolveTake1'
    spec.loader.exec_module(mod)

Logged with the [AnimSampler] prefix; writes one JSON to OUTPUT_DIR.
"""

import json
import math
import os
import time

import unreal

ANIM_PATH = globals().get("ANIM_PATH", "/Game/MHAGroundTruth/AS_MHA_Body_Take1")
OUTPUT_LABEL = globals().get("OUTPUT_LABEL", "mhaSolve")
SAMPLE_HZ = float(globals().get("SAMPLE_HZ", 30.0))
OUTPUT_DIR = r"D:\Epic\Unreal_Projects\TestingKit5\Saved\CodexAgent\Diagnostics"

CORE_BONES = [
    "pelvis",
    "spine_01", "spine_02", "spine_03", "spine_04", "spine_05",
    "neck_01", "head",
    "clavicle_l", "upperarm_l", "lowerarm_l", "hand_l",
    "clavicle_r", "upperarm_r", "lowerarm_r", "hand_r",
    "thigh_l", "calf_l", "foot_l", "ball_l",
    "thigh_r", "calf_r", "foot_r", "ball_r",
    "thumb_01_l", "thumb_02_l", "thumb_03_l",
    "index_01_l", "index_02_l", "index_03_l",
    "middle_01_l", "middle_02_l", "middle_03_l",
    "ring_01_l", "ring_02_l", "ring_03_l",
    "pinky_01_l", "pinky_02_l", "pinky_03_l",
    "thumb_01_r", "thumb_02_r", "thumb_03_r",
    "index_01_r", "index_02_r", "index_03_r",
    "middle_01_r", "middle_02_r", "middle_03_r",
    "ring_01_r", "ring_02_r", "ring_03_r",
    "pinky_01_r", "pinky_02_r", "pinky_03_r",
]

# World-space rotations recorded for these bones (palm retarget fit 2026-07-06: positions
# cannot see a wrist twist; the constant palm-roll bias vs Epic needs orientation data).
ROT_BONES = ["hand_l", "hand_r", "lowerarm_l", "lowerarm_r", "upperarm_l", "upperarm_r"]


def _log(msg):
    unreal.log("[AnimSampler] {}".format(msg))


def _angle_deg(a, b, c):
    v1 = unreal.Vector(a.x - b.x, a.y - b.y, a.z - b.z)
    v2 = unreal.Vector(c.x - b.x, c.y - b.y, c.z - b.z)
    l1 = v1.length()
    l2 = v2.length()
    if l1 < 1e-4 or l2 < 1e-4:
        return None
    cos_angle = max(-1.0, min(1.0, v1.dot(v2) / (l1 * l2)))
    return math.degrees(math.acos(cos_angle))


def run():
    anim = unreal.load_asset(ANIM_PATH)
    if not anim:
        _log(f"FAILED to load {ANIM_PATH}")
        return
    length = anim.get_play_length()
    step = 1.0 / SAMPLE_HZ
    opts = unreal.AnimPoseEvaluationOptions()
    samples = []
    t = 0.0
    while t <= length:
        pose = unreal.AnimPoseExtensions.get_anim_pose_at_time(anim, t, opts)
        bones = {}
        for bone in CORE_BONES:
            try:
                tr = unreal.AnimPoseExtensions.get_bone_pose(
                    pose, bone, unreal.AnimPoseSpaces.WORLD).translation
                bones[bone] = [tr.x, tr.y, tr.z]
            except Exception:
                pass
        rots = {}
        for bone in ROT_BONES:
            try:
                q = unreal.AnimPoseExtensions.get_bone_pose(
                    pose, bone, unreal.AnimPoseSpaces.WORLD).rotation
                rots[bone] = [q.x, q.y, q.z, q.w]
            except Exception:
                pass
        sample = {"wall_time": t, "game_time": t, "bones": bones, "rots": rots}
        for side in ("l", "r"):
            thigh = bones.get(f"thigh_{side}")
            calf = bones.get(f"calf_{side}")
            foot = bones.get(f"foot_{side}")
            ball = bones.get(f"ball_{side}")
            if thigh and calf and foot:
                a = _angle_deg(
                    unreal.Vector(*thigh), unreal.Vector(*calf), unreal.Vector(*foot))
                sample[f"knee_angle_{side}"] = a
            if ball:
                sample[f"ball_{side}_z"] = ball[2]
        samples.append(sample)
        t += step

    ranges = {}
    for key in ("knee_angle_l", "knee_angle_r"):
        vals = [s[key] for s in samples if s.get(key) is not None]
        if vals:
            ranges[key] = {"min": min(vals), "max": max(vals)}

    out = {
        "actor": "MHA_Solve",
        "label": OUTPUT_LABEL,
        "source_anim": ANIM_PATH,
        "sample_count": len(samples),
        "sample_hz": SAMPLE_HZ,
        "play_length": length,
        "ranges": ranges,
        "samples": samples,
    }
    stamp = time.strftime("%Y%m%d_%H%M%S")
    path = os.path.join(
        OUTPUT_DIR, f"live_pie_bone_measure_MHA_Solve_{OUTPUT_LABEL}_{stamp}.json")
    with open(path, "w") as fh:
        json.dump(out, fh)
    _log(f"WROTE {path} samples={len(samples)} length={length:.2f}s")


run()
