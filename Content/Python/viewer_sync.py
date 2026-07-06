"""Side-by-side viewer: full clothed reference Kellan, continuously clock-synced.

Run INSIDE PIE on the replay map (parity mode), after the driven Kellan exists:

    import viewer_sync
    viewer_sync.start()

- Finds the replay-driven Kellan and a pre-placed BP_Kellan reference actor
  (label MHA_Reference_Full), positions the reference beside the driven one.
- Slaves the reference's Body component to the MHA solve AnimSequence in
  single-node mode (clothing/face follow via leader pose) with the MetaHuman
  post-process ABP disabled so nothing fights the animation.
- Seeks the dataset replay to 0 and re-syncs the animation position EVERY
  TICK from the shared wall clock (modulo the replay duration), so the two
  Kellans stay comparable across loops - no drift, unlike the one-shot sync
  that made earlier comparisons unfair.
- The MHA solve's clock is NONLINEARLY warped against the dataset clock
  (VFR iPhone video ingested as constant-fps: measured offset wanders
  12.1s -> 0.7s across the take, 2026-07-04 forensics). A constant offset
  can never be in sync everywhere, so the anim position is mapped through
  measured piecewise-linear anchors instead.

    viewer_sync.stop()  # unregister the tick
"""

import glob
import json
import time
import unreal

# Take 4's Epic solve survived the GPU-crash era as FOUR chunk AnimSequences
# (2026-07-06: monolithic solves died twice to device removals; chunked solve +
# per-chunk export bounds a crash to ~15min). The viewer plays them as one
# continuous timeline: (asset, video_start_s) with 1741-frame spacing at 30fps.
CHUNK_SPACING_S = 1741 / 30.0
ANIM_SEGMENTS = [
    (f"/Game/MHAGroundTruth/AS_MHA_Body_Take4_c{i}", i * CHUNK_SPACING_S)
    for i in range(4)
]
REF_LABEL = "MHA_Reference_Full"
DRIVEN_LABEL = "MP_LiveMetaHumanKellan"
REPLAY_DURATION_S = 209.951  # take-4 dataset duration (sample_time_span_seconds)
SIDE_OFFSET_CM = 170.0

# (dataset_time_s, mha_offset_s) anchors. Take 4's video was clipped (34s in) and
# retimed to constant 30fps before the solve like take 3, so the offset is FLAT.
# MEASURED -2.90s by cross-correlation (pelvis+feet, take_score.py 2026-07-06);
# the -10.9s arithmetic guess from recorder timestamps was wrong - always fit.
TIME_WARP_ANCHORS = [
    (0.0, -2.9),
    (209.951, -2.9),
]


def _warp(t):
    """Dataset time -> MHA anim time via piecewise-linear offset anchors."""
    a = TIME_WARP_ANCHORS
    if t <= a[0][0]:
        return t + a[0][1]
    for (t0, o0), (t1, o1) in zip(a, a[1:]):
        if t <= t1:
            f = (t - t0) / (t1 - t0)
            return t + o0 + f * (o1 - o0)
    return t + a[-1][1]

# MOTION-MATCHED TIME TRACKING (2026-07-06 v3). Wall-clock sync assumed the
# replay advances in real time; with two full MetaHumans + parity solve the
# editor runs ~3fps and the replay clock visibly detaches from wall time
# (user-observed: reference off by seconds, fused clap missing on the Epic
# side). The replay actor exposes no playback time to Python (probed), so the
# tracker estimates dataset time from the DRIVEN AVATAR'S OWN POSE against the
# fused capture timeline: advance by wall dt, correct toward the best local
# match, global re-search when lost. Signals: pelvis z + hand z rel pelvis.
FUSED_CAPTURE_GLOB = (
    "D:/Epic/Unreal_Projects/TestingKit5/Saved/CodexAgent/Diagnostics/"
    "live_pie_bone_measure_MP_LiveMetaHumanKellan_fusedTake4round*_*.json")
TRACK_WINDOW_S = 3.0      # local search half-width
TRACK_BLEND = 0.25        # per-tick pull toward best match
TRACK_LOST_CM = 18.0      # sustained error above this -> global re-search
TRACK_LOST_S = 3.0

def _capture_sig(bones):
    # Pose fingerprint for time tracking. Feet are essential: pelvis+hands alone alias
    # every leg-raise onto quiet standing (hands hang, pelvis level) and the tracker
    # locked onto wrong moments during leg phases (user-caught 2026-07-06 evening).
    pel = bones.get("pelvis")
    hl = bones.get("hand_l")
    hr = bones.get("hand_r")
    fl = bones.get("foot_l")
    fr = bones.get("foot_r")
    if not (pel and hl and hr and fl and fr):
        return None
    return (pel[2], hl[2] - pel[2], hr[2] - pel[2], fl[2] - pel[2], fr[2] - pel[2])


def _load_capture():
    paths = sorted(glob.glob(FUSED_CAPTURE_GLOB))
    if not paths:
        return None
    data = json.load(open(paths[-1]))
    t0 = data["samples"][0]["wall_time"]
    ts, sigs = [], []
    for s in data["samples"]:
        sig = _capture_sig(s["bones"])
        if sig is not None:
            ts.append(s["wall_time"] - t0)
            sigs.append(sig)
    _log(f"tracker capture loaded: {len(ts)} refs from {paths[-1].rsplit(chr(92), 1)[-1]}")
    return {"ts": ts, "sigs": sigs}


def _live_sig(driven):
    mesh = None
    for comp in driven.get_components_by_class(unreal.SkeletalMeshComponent):
        if comp.does_socket_exist("pelvis") and comp.does_socket_exist("hand_r"):
            mesh = comp
            break
    if not mesh:
        return None
    pel = mesh.get_socket_location("pelvis")
    hl = mesh.get_socket_location("hand_l")
    hr = mesh.get_socket_location("hand_r")
    fl = mesh.get_socket_location("foot_l")
    fr = mesh.get_socket_location("foot_r")
    return (pel.z, hl.z - pel.z, hr.z - pel.z, fl.z - pel.z, fr.z - pel.z)


def _track_time(est_t, dt_wall):
    """Advance the dataset-time estimate and pull it toward the pose match."""
    cap = _state.get("cap")
    driven = _state.get("driven")
    if not cap or not driven:
        return est_t
    try:
        live = _live_sig(driven)
    except Exception:
        _state["driven"] = None
        return est_t
    if live is None:
        return est_t
    # RATE-ADAPTIVE advance (2026-07-06 night): advancing at wall speed while the replay
    # runs slower (3fps load) parks the reference a constant beat AHEAD of the fusion -
    # the user sees Epic perform every move first. Advance at the fused avatar's MEASURED
    # rate (lowpassed d(best-match)/d(wall)) so the lead converges to zero at any editor
    # frame rate.
    est_t = (est_t + dt_wall * _state.get("rate", 1.0)) % REPLAY_DURATION_S
    ts, sigs = cap["ts"], cap["sigs"]
    lost = _state.get("lost_since")
    global_search = lost is not None and (time.time() - lost) > TRACK_LOST_S
    if global_search:
        idxs = range(0, len(ts), 4)
    else:
        import bisect
        lo = bisect.bisect_left(ts, est_t - TRACK_WINDOW_S)
        hi = bisect.bisect_left(ts, est_t + TRACK_WINDOW_S)
        idxs = range(max(0, lo), min(len(ts), max(hi, lo + 1)))
    best_i, best_e = None, 1e18
    for i in idxs:
        s = sigs[i]
        e = sum(abs(s[k] - live[k]) for k in range(len(live)))
        if e < best_e:
            best_e, best_i = e, i
    if best_i is None:
        return est_t
    err_cm = best_e / float(len(live))
    if err_cm > TRACK_LOST_CM:
        if lost is None:
            _state["lost_since"] = time.time()
    else:
        _state["lost_since"] = None
    now = time.time()
    if global_search:
        est_t = ts[best_i]
        _state["lost_since"] = None
        _state["rate_ref"] = (ts[best_i], now)
        _log(f"tracker GLOBAL re-lock t={est_t:.1f}s err={err_cm:.1f}cm")
    else:
        est_t += TRACK_BLEND * (ts[best_i] - est_t)
        # measured replay rate: matched-time progress per wall second, lowpassed and
        # clamped; only updated while the match is trustworthy so calm-phase ambiguity
        # cannot drag the rate estimate around.
        ref = _state.get("rate_ref")
        if err_cm < 8.0:
            if ref is not None and now - ref[1] > 2.0:
                raw = (ts[best_i] - ref[0]) / (now - ref[1])
                if -0.2 < raw < 1.6:
                    _state["rate"] = 0.7 * _state.get("rate", 1.0) + 0.3 * max(raw, 0.0)
                _state["rate_ref"] = (ts[best_i], now)
            elif ref is None:
                _state["rate_ref"] = (ts[best_i], now)
    if now - _state.get("last_track_log", 0.0) > 5.0:
        _state["last_track_log"] = now
        _log(f"track t={est_t:.1f}s err={err_cm:.1f}cm rate={_state.get('rate', 1.0):.2f}")
    return est_t

_handle = None
_state = {}


def _log(msg):
    unreal.log(f"[ViewerSync] {msg}")


def _find(world, label):
    for a in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.Actor):
        if label in a.get_actor_label():
            return a
    return None


def _segment_for(pos):
    """Video time -> (segment_index, local_time) across the chunked anims."""
    anims = _state["anims"]
    idx = min(len(anims) - 1, max(0, int(pos / CHUNK_SPACING_S)))
    local = pos - ANIM_SEGMENTS[idx][1]
    max_local = _state["anim_lens"][idx] - 0.01
    return idx, min(max(local, 0.0), max_local)


def _resolve_body():
    """(Re)find the reference's Body component and slave it. Returns the component or None."""
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
    if not world:
        return None
    ref = _find(world, REF_LABEL)
    if not ref:
        return None
    body = None
    for comp in ref.get_components_by_class(unreal.SkeletalMeshComponent):
        if comp.get_name() == "Body":
            body = comp
            break
    if not body:
        comps = ref.get_components_by_class(unreal.SkeletalMeshComponent)
        body = comps[0] if comps else None
    if not body:
        return None
    body.set_editor_property("disable_post_process_blueprint", True)
    body.set_animation_mode(unreal.AnimationMode.ANIMATION_SINGLE_NODE)
    body.play_animation(_state["anims"][_state.get("seg", 0)], True)
    try:
        body.set_play_rate(0.0)
    except Exception:
        pass
    _state["comp"] = body
    return body


def _tick(_dt):
    global _handle
    try:
        comp = _state.get("comp")
        if not _state.get("anims"):
            return
        # SELF-HEALING (probed live 2026-07-05, twice): the MetaHuman BP both re-asserts
        # ANIMATION_BLUEPRINT after begin play AND rebuilds its Body component during delayed
        # init, so a stored handle can go stale and a one-shot slave silently freezes the
        # reference. Every tick: re-resolve the component if the handle is dead, re-assert
        # single-node if the BP took the mode back, then write the position. Never stop().
        try:
            mode_ok = comp and (
                comp.get_editor_property("animation_mode") == unreal.AnimationMode.ANIMATION_SINGLE_NODE)
        except Exception:
            comp = None
            mode_ok = False
        if not comp:
            now = time.time()
            if now - _state.get("last_resolve", 0.0) >= 1.0:
                _state["last_resolve"] = now
                comp = _resolve_body()
            if not comp:
                return
        elif not mode_ok:
            comp.set_animation_mode(unreal.AnimationMode.ANIMATION_SINGLE_NODE)
            comp.play_animation(_state["anims"][_state.get("seg", 0)], True)
            # The BP re-asserts constantly; every re-slave must restore scrub mode or the
            # reference resumes self-playing at 1x and races ahead between ticks.
            try:
                comp.set_play_rate(0.0)
            except Exception:
                pass
        now = time.time()
        dt_wall = min(now - _state.get("last_tick", now), 1.0)
        _state["last_tick"] = now
        if not _state.get("driven"):
            world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
            if world:
                _state["driven"] = _find(world, DRIVEN_LABEL)
        _state["est_t"] = _track_time(_state.get("est_t", 0.0), dt_wall)
        pos = _warp(_state["est_t"])
        if pos < 0.0:
            pos = 0.0
        seg, local = _segment_for(pos)
        if seg != _state.get("seg"):
            _state["seg"] = seg
            comp.play_animation(_state["anims"][seg], True)
            try:
                comp.set_play_rate(0.0)
            except Exception:
                pass
        comp.set_position(local, False)
    except Exception:
        # Keep the tick alive: transient failures (component churn, PIE transitions) must not
        # silently freeze the reference. The next tick retries resolution.
        _state["comp"] = None


def start():
    global _handle
    stop()
    ues = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
    world = ues.get_game_world()
    if not world:
        _log("no PIE world - start PIE first")
        return
    driven = _find(world, DRIVEN_LABEL)
    ref = _find(world, REF_LABEL)
    if not driven or not ref:
        _log(f"actors missing: driven={bool(driven)} ref={bool(ref)}")
        return
    # WORLD-AXIS placement, measured for this map (do NOT trust actor rotation - the driven
    # actor's yaw is 0 while the avatar VISUALLY faces world +Y toward the TV, so both the
    # right-vector and world-Y offsets put the reference in the driven avatar's sightline).
    # Side by side for a front camera = separated along world X at the SAME Y.
    # Reference on the driven's -X side: the in-world webcam panel sits toward +X and eats
    # the frame's right third from the front camera (screenshot-verified 2026-07-05).
    dloc = driven.get_actor_location()
    ref.set_actor_location(unreal.Vector(
        dloc.x - SIDE_OFFSET_CM, dloc.y, dloc.z), False, False)
    ref.set_actor_rotation(driven.get_actor_rotation(), False)

    # The map's self-view mirror Kellan is a THIRD figure standing in the comparison sightline.
    # Direct hides do NOT stick: the embodied pawn re-asserts self-view visibility every
    # update (SetMetaHumanSelfViewAvatarVisible). Flip the pawn's own switch instead.
    pawn = _find(world, "MP_PlacedEmbodiedMetaHumanPawn")
    if pawn:
        done = False
        for prop in ("show_media_pipe_self_view", "bShowMediaPipeSelfView"):
            try:
                pawn.set_editor_property(prop, False)
                done = True
                _log(f"self-view disabled via pawn.{prop}")
                break
            except Exception:
                continue
        if not done:
            _log("PAWN SELF-VIEW PROPERTY NOT FOUND - third figure will photobomb")
    else:
        _log("embodied pawn NOT FOUND - cannot disable self-view")

    # The pawn camera sits INSIDE the driven avatar (pawn-is-avatar), so the driven Kellan is
    # invisible from the default view. Frame both from the front with a fixed camera. The camera
    # actor must be PLACED PRE-PIE via EditorActorSubsystem (payload place_viewer_cam.json) -
    # runtime spawning from Python failed twice live (neither World.spawn_actor_from_class nor
    # GameplayStatics.begin_deferred_actor_spawn_from_class exists in 5.8 Python). Framing is
    # best-effort and must NEVER kill the sync below.
    try:
        cam = _find(world, "ViewerCam")
        if cam:
            rloc = ref.get_actor_location()
            mid = unreal.Vector((dloc.x + rloc.x) * 0.5, (dloc.y + rloc.y) * 0.5, dloc.z + 110.0)
            # Front of the avatars is world +Y in this map (visual facing; see above).
            cam_loc = unreal.Vector(mid.x, mid.y + 420.0, mid.z + 40.0)
            cam.set_actor_location(cam_loc, False, False)
            cam.set_actor_rotation(unreal.MathLibrary.find_look_at_rotation(cam_loc, mid), False)
            pc = unreal.GameplayStatics.get_player_controller(world, 0)
            if pc:
                pc.set_view_target_with_blend(cam, 0.5)
            _log("camera framing set (ViewerCam)")
        else:
            _log("no ViewerCam actor - keeping pawn view (place it pre-PIE)")
    except Exception as e:
        _log(f"camera framing failed, sync continues: {e}")

    body = None
    for comp in ref.get_components_by_class(unreal.SkeletalMeshComponent):
        if comp.get_name() == "Body":
            body = comp
            break
    if not body:
        comps = ref.get_components_by_class(unreal.SkeletalMeshComponent)
        body = comps[0] if comps else None
    if not body:
        _log("reference has no skeletal mesh component")
        return

    anims = [unreal.load_asset(path) for path, _ in ANIM_SEGMENTS]
    if not all(anims):
        _log(f"MISSING CHUNK ANIMS: {[p for (p, _), a in zip(ANIM_SEGMENTS, anims) if not a]}")
        return
    body.set_editor_property("disable_post_process_blueprint", True)
    body.set_animation_mode(unreal.AnimationMode.ANIMATION_SINGLE_NODE)
    _state["comp"] = body
    _state["anims"] = anims
    _state["anim_lens"] = [a.get_play_length() for a in anims]
    _state["seg"] = 0
    body.play_animation(anims[0], True)
    # SCRUB, don't play (2026-07-06 night): the single-node anim self-advances at 1.0x
    # between our per-tick position writes; at ~3fps editor ticks the reference sprints
    # a third of a second ahead then snaps back every tick - the user sees "Epic runs
    # faster than the fusion". Rate 0 makes set_position the only motion source.
    try:
        body.set_play_rate(0.0)
    except Exception:
        _log("set_play_rate not available - reference may sawtooth between ticks")

    _state["cap"] = _load_capture()
    _state["driven"] = driven
    _state["est_t"] = 0.0
    _state["lost_since"] = None
    unreal.SystemLibrary.execute_console_command(None, "mp.SeekTrackingFusionDatasetReplay 0")
    _state["t0"] = time.time()
    _state["last_tick"] = time.time()
    _handle = unreal.register_slate_post_tick_callback(_tick)
    mode = "motion-matched" if _state["cap"] else "WALL-CLOCK FALLBACK (no capture file!)"
    _log(f"synced ({mode}): replay seeked to 0, warp window {_warp(0.0):.1f}s..{_warp(REPLAY_DURATION_S):.1f}s, side offset {SIDE_OFFSET_CM}cm")


def stop():
    global _handle
    if _handle is not None:
        unreal.unregister_slate_post_tick_callback(_handle)
        _handle = None
