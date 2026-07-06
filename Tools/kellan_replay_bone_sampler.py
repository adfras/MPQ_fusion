"""Replay live-PIE bone sampler for the recorded Quest+MediaPipe replay map.

Samples every editor tick during PIE for CAPTURE_SECONDS after seeking the tracking-fusion
dataset replay to SEEK_OFFSET_SECONDS, for EVERY actor label in ACTOR_LABELS (defaults to the
live MetaHuman and the Manny reference avatar so improvements are proven on both skeletons).
Writes one JSON per actor with per-sample bone positions plus derived knee/pelvis/foot metrics.

Usage (from bridge run_unreal_python):
    import importlib.util
    spec = importlib.util.spec_from_file_location(
        'sampler', 'D:/Epic/Unreal_Projects/TestingKit5/Saved/CodexAgent/kellan_replay_bone_sampler.py')
    mod = importlib.util.module_from_spec(spec)
    mod.OUTPUT_LABEL = 'after_x'
    mod.SEEK_OFFSET_SECONDS = 147.0
    mod.CAPTURE_SECONDS = 66.0
    spec.loader.exec_module(mod)

Do NOT move the player pawn while sampling: the embodied pawn carries the live avatar.
Progress/result is logged with the prefix [ReplaySampler].
"""

import json
import math
import os
import time

import unreal

OUTPUT_LABEL = globals().get("OUTPUT_LABEL", "measure")
SEEK_OFFSET_SECONDS = float(globals().get("SEEK_OFFSET_SECONDS", 147.0))
CAPTURE_SECONDS = float(globals().get("CAPTURE_SECONDS", 66.0))
SETTLE_SECONDS = float(globals().get("SETTLE_SECONDS", 2.0))
ACTOR_LABELS = globals().get("ACTOR_LABELS", ["MP_LiveMetaHumanKellan", "MP_LiveMediaPipeManny"])

OUTPUT_DIR = r"D:\Epic\Unreal_Projects\TestingKit5\Saved\CodexAgent\Diagnostics"

CORE_BONES = [
    "pelvis",
    "spine_01", "spine_02", "spine_03", "spine_04", "spine_05",
    "neck_01", "head",
    "clavicle_l", "upperarm_l", "lowerarm_l", "hand_l",
    "clavicle_r", "upperarm_r", "lowerarm_r", "hand_r",
    "thigh_l", "calf_l", "foot_l", "ball_l",
    "thigh_r", "calf_r", "foot_r", "ball_r",
    # Fingers (user rule: all bones, no curated subsets - 2026-07-05)
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


def _log(msg):
    unreal.log("[ReplaySampler] {}".format(msg))


def _get_pie_world():
    editor_subsystem = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
    return editor_subsystem.get_game_world()


def _find_target_meshes(world):
    targets = {}
    actors = unreal.GameplayStatics.get_all_actors_of_class(world, unreal.Actor)
    for actor in actors:
        try:
            label = actor.get_actor_label()
        except Exception:
            continue
        if label not in ACTOR_LABELS:
            continue
        for comp in actor.get_components_by_class(unreal.SkeletalMeshComponent):
            if comp.does_socket_exist("thigh_l") and comp.does_socket_exist("ball_r"):
                targets[label] = (actor, comp)
                break
    return targets


def _vec_to_list(v):
    return [v.x, v.y, v.z]


def _angle_deg(a, b):
    al = math.sqrt(sum(x * x for x in a))
    bl = math.sqrt(sum(x * x for x in b))
    if al <= 1e-6 or bl <= 1e-6:
        return 0.0
    d = max(-1.0, min(1.0, sum(x * y for x, y in zip(a, b)) / (al * bl)))
    return math.degrees(math.acos(d))


def _sub(a, b):
    return [a[0] - b[0], a[1] - b[1], a[2] - b[2]]


def _dot(a, b):
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]


class _SamplerState(object):
    def __init__(self):
        self.samples_by_actor = {}
        self.handle = None
        self.phase = "settle"
        self.elapsed = 0.0
        self.world = None
        self.targets = {}
        self.floor_z = None
        self.started_at = time.time()
        self.done = False


STATE = _SamplerState()


def _estimate_floor_z(world, actor):
    # Trace beside the avatar so co-located skeletal actors cannot shadow the floor.
    origin = actor.get_actor_location()
    start = unreal.Vector(origin.x + 80.0, origin.y, origin.z + 50.0)
    end = unreal.Vector(origin.x + 80.0, origin.y, origin.z - 500.0)
    hit = unreal.SystemLibrary.line_trace_single(
        world, start, end,
        unreal.TraceTypeQuery.TRACE_TYPE_QUERY1,
        False, [actor],
        unreal.DrawDebugTrace.NONE, True)
    if hit:
        return hit.to_tuple()[4].z
    return 0.0


ROT_BONES = ["hand_l", "hand_r", "lowerarm_l", "lowerarm_r", "upperarm_l", "upperarm_r"]


def _sample_actor(state, actor, mesh):
    bones = {}
    for bone in CORE_BONES:
        if not mesh.does_socket_exist(bone):
            continue
        bones[bone] = _vec_to_list(mesh.get_socket_location(bone))

    if "thigh_l" not in bones or "calf_l" not in bones or "foot_l" not in bones:
        return None

    # World rotations for the palm retarget fit (2026-07-06): positions cannot see a
    # wrist twist; the constant palm-roll bias vs Epic needs orientation data.
    rots = {}
    for bone in ROT_BONES:
        if not mesh.does_socket_exist(bone):
            continue
        try:
            q = mesh.get_socket_transform(bone, unreal.RelativeTransformSpace.RTS_WORLD).rotation
            rots[bone] = [q.x, q.y, q.z, q.w]
        except Exception:
            pass

    fwd = actor.get_actor_forward_vector()
    fwd_l = [fwd.x, fwd.y, fwd.z]

    def knee_angle(side):
        t, c, f = bones.get("thigh_" + side), bones.get("calf_" + side), bones.get("foot_" + side)
        if not t or not c or not f:
            return None
        return _angle_deg(_sub(t, c), _sub(f, c))

    def fwd_metric(a, b):
        return _dot(_sub(a, b), fwd_l)

    floor_z = state.floor_z if state.floor_z is not None else 0.0
    sample = {
        "game_time": unreal.GameplayStatics.get_time_seconds(state.world),
        "wall_time": time.time() - state.started_at,
        "floor_z": floor_z,
        "actor_forward": fwd_l,
        "pelvis_x": bones["pelvis"][0],
        "pelvis_y": bones["pelvis"][1],
        "pelvis_z": bones["pelvis"][2],
        "knee_angle_l": knee_angle("l"),
        "knee_angle_r": knee_angle("r"),
        "foot_l_z": bones["foot_l"][2] - floor_z if "foot_l" in bones else None,
        "ball_l_z": bones["ball_l"][2] - floor_z if "ball_l" in bones else None,
        "foot_r_z": bones["foot_r"][2] - floor_z if "foot_r" in bones else None,
        "ball_r_z": bones["ball_r"][2] - floor_z if "ball_r" in bones else None,
        "bones": bones,
        "rots": rots,
    }
    for side in ("l", "r"):
        calf, foot, ball = bones.get("calf_" + side), bones.get("foot_" + side), bones.get("ball_" + side)
        if calf and foot and ball:
            sample["knee_%s_forward_from_foot" % side] = fwd_metric(calf, foot)
            sample["knee_%s_forward_from_ball" % side] = fwd_metric(calf, ball)
            sample["foot_%s_forward_span" % side] = fwd_metric(ball, foot)
    return sample


def _write_outputs(state):
    ts = time.strftime("%Y%m%d_%H%M%S")
    summary_keys = [
        "pelvis_x", "pelvis_y", "pelvis_z",
        "knee_angle_l", "knee_angle_r",
        "foot_l_z", "ball_l_z", "foot_r_z", "ball_r_z",
        "knee_l_forward_from_foot", "knee_l_forward_from_ball", "foot_l_forward_span",
        "knee_r_forward_from_foot", "knee_r_forward_from_ball", "foot_r_forward_span",
    ]

    for label, samples in state.samples_by_actor.items():
        def series(key):
            vals = [s[key] for s in samples if s.get(key) is not None]
            if not vals:
                return None
            return {"min": min(vals), "max": max(vals), "range": max(vals) - min(vals)}

        path = os.path.join(
            OUTPUT_DIR,
            "live_pie_bone_measure_{}_{}_{}.json".format(label, OUTPUT_LABEL, ts))
        payload = {
            "captured_at": time.strftime("%Y-%m-%dT%H:%M:%S"),
            "label": OUTPUT_LABEL,
            "actor": label,
            "seek_offset_seconds": SEEK_OFFSET_SECONDS,
            "capture_seconds": CAPTURE_SECONDS,
            "sample_count": len(samples),
            "floor_z": state.floor_z,
            "ranges": {k: series(k) for k in summary_keys},
            "samples": samples,
        }
        with open(path, "w") as fh:
            json.dump(payload, fh, indent=1)
        _log("WROTE {} samples={}".format(path, len(samples)))


def _tick(delta_seconds):
    state = STATE
    if state.done:
        return
    try:
        # WALL-CLOCK phase timing (2026-07-06): accumulating engine delta_seconds under
        # heavy load (two MetaHumans + parity solve at ~3fps) runs ~10x slower than real
        # time because slate deltas are clamped - a "205s" capture silently became ~35min
        # and downstream watchers timed out. The samples' own time base is already
        # time.time(); the phase clock now matches it.
        state.elapsed = time.time() - state.started_at
        if state.phase == "settle":
            if state.elapsed >= SETTLE_SECONDS:
                state.phase = "capture"
                state.started_at = time.time()
                state.elapsed = 0.0
            return

        for label, (actor, mesh) in list(state.targets.items()):
            try:
                sample = _sample_actor(state, actor, mesh)
            except Exception:
                # SELF-HEAL (2026-07-05): the MetaHuman BP rebuilds components mid-session;
                # a cached mesh handle dying killed whole captures ("ObjectInstance is null").
                # Re-resolve targets once and retry on the next tick instead of dying.
                world = _get_pie_world()
                if world:
                    refreshed = _find_target_meshes(world)
                    if label in refreshed:
                        state.targets[label] = refreshed[label]
                        _log(f"re-resolved stale mesh for {label}")
                continue
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
    _write_outputs(state)


def start():
    state = STATE
    world = _get_pie_world()
    if world is None:
        _log("ERROR no PIE world")
        return
    state.targets = _find_target_meshes(world)
    if not state.targets:
        _log("ERROR no target actors found for labels {}".format(ACTOR_LABELS))
        return
    state.world = world
    first_actor = next(iter(state.targets.values()))[0]
    state.floor_z = _estimate_floor_z(world, first_actor)

    unreal.SystemLibrary.execute_console_command(
        world, "mp.SeekTrackingFusionDatasetReplay {}".format(SEEK_OFFSET_SECONDS))
    _log("seek issued offset={} targets={} floorZ={:.3f}".format(
        SEEK_OFFSET_SECONDS, list(state.targets.keys()), state.floor_z))

    state.phase = "settle"
    state.elapsed = 0.0
    state.handle = unreal.register_slate_post_tick_callback(_tick)
    _log("sampler registered settle={}s capture={}s".format(SETTLE_SECONDS, CAPTURE_SECONDS))


start()
