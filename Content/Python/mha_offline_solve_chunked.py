"""Chunked offline MHA body solve - crash-resilient variant of mha_offline_solve.

Motivation (2026-07-06): the take-4 full-length solve died 59 minutes in to a
GPU device removal (RTX 5070 Ti hang under sustained DirectML load, third such
crash this arc). A monolithic run loses everything; this module processes the
take in frame-range chunks and exports each chunk's animation to a saved asset
the moment it completes, bounding a crash's cost to one chunk.

Usage over MCP (each call cheap and non-blocking, per UNREAL_MCP_OPERATIONS rule 1):

    import mha_offline_solve_chunked as mc
    mc.solve_chunk(0)     # schedules ingest-reuse + fresh/reused perf asset + range solve
    mc.status()           # logs processing state + processed frame count
    mc.export_chunk(0)    # exports AS_<PERF_NAME>_c0 (ProcessingRange) and saves it
    mc.report_exported()  # logs which chunk exports already exist on disk

Chunk boundaries are adjacent, no overlap. The body solver is temporal, so
expect (small) seams at chunk joins - scoring should ignore +/-1s around them.
"""

import unreal

VIDEO = globals().get("VIDEO", r"C:/Users/Alan/Videos/take4_cfr30.mp4")
SLATE = globals().get("SLATE", "mha_groundtruth_take4")
PERF_DIR = globals().get("PERF_DIR", "/Game/MHAGroundTruth")
PERF_NAME = globals().get("PERF_NAME", "PF_MHA_GroundTruth_Take4")
EXPORT_DIR = globals().get("EXPORT_DIR", "/Game/MHAGroundTruth")
BODY_MESH = globals().get("BODY_MESH", "/Game/MetaHumans/Kellan/Body/m_med_nrw_body")

TOTAL_FRAMES = 6961          # take4_cfr30.mp4, exact 30fps
NUM_CHUNKS = 4               # ~58s per chunk => worst-case crash loses ~15-18min of solve

# Metric height prior (2026-07-06): mono video has no absolute scale, so the solve's
# auto-body-height GUESSES the performer's proportions (take-4 guessed a body with 32%
# narrower shoulders than Kellan; every cm-based referee metric inherited the artifact).
# The Quest dataset recorded alongside the video HAS absolute scale (SLAM). Derive the
# performer's height from the settle-phase HMD height + crown offset - automatic, works
# for any performer, nothing hardcoded. 0 keeps Epic's auto estimate.
BODY_HEIGHT_CM = globals().get("BODY_HEIGHT_CM", 0.0)
HMD_CROWN_OFFSET_CM = 12.0   # eye-line to top of head, anthropometric median


def derive_body_height_from_dataset(dataset_manifest_path, settle_end_s=25.0):
    """Median HMD height over the standing settle window + crown offset -> height (cm)."""
    import json as _json
    manifest = _json.load(open(dataset_manifest_path))
    heights = []
    for entry in manifest.get("sample_files", []):
        path = entry if isinstance(entry, str) else entry.get("path", "")
        try:
            with open(path) as fh:
                for line in fh:
                    s = _json.loads(line)
                    t = s.get("time_seconds", s.get("t", None))
                    if t is None or t > settle_end_s:
                        break
                    hmd = s.get("hmd", {})
                    z = hmd.get("position", {}).get("z", hmd.get("z", None))
                    if z is not None:
                        heights.append(float(z))
        except OSError:
            continue
    if not heights:
        _log("height prior: no HMD samples found - keeping auto body height")
        return 0.0
    heights.sort()
    hmd_z = heights[len(heights) // 2]
    height = hmd_z + HMD_CROWN_OFFSET_CM
    _log(f"height prior: settle-phase HMD z median {hmd_z:.1f}cm -> body height {height:.1f}cm "
         f"({len(heights)} samples)")
    return height

_tick_handle = None
_performance = None
_pending_chunk = None


def _log(msg):
    unreal.log(f"[MHASolveChunk] {msg}")


def chunk_range(index):
    per = (TOTAL_FRAMES + NUM_CHUNKS - 1) // NUM_CHUNKS
    start = index * per
    end = min(TOTAL_FRAMES, start + per)
    return start, end


def _export_name(index):
    return f"AS_MHA_Body_Take4_c{index}"


def _ensure_performance(footage):
    """Create the performance asset if this session doesn't have one yet.

    Unlike the monolithic script we do NOT delete an existing asset: the
    take-2 stale-range hazard doesn't apply because every chunk explicitly
    calls SetProcessingRange before start_pipeline.
    """
    global _performance
    if _performance is not None:
        return _performance
    perf_path = f"{PERF_DIR}/{PERF_NAME}"
    if unreal.EditorAssetLibrary.does_asset_exist(perf_path):
        _performance = unreal.load_asset(perf_path)
        _log(f"reusing performance asset {perf_path}")
        return _performance
    asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
    performance = asset_tools.create_asset(
        asset_name=PERF_NAME,
        package_path=PERF_DIR,
        asset_class=unreal.MetaHumanPerformance,
        factory=unreal.new_object(type=unreal.MetaHumanPerformanceFactoryNew),
    )
    if not performance:
        _log("PERFORMANCE ASSET CREATION FAILED")
        return None
    performance.set_editor_property("input_type", unreal.DataInputType.MONO_FOOTAGE)
    performance.set_editor_property("footage_capture_data", footage)
    performance.set_editor_property("body_tracking", True)
    try:
        performance.set_editor_property("face_tracking", False)
    except Exception as err:
        _log(f"face_tracking not settable ({err}); continuing")
    if BODY_HEIGHT_CM > 0.0:
        # Metric scale from the Quest dataset (see derive_body_height_from_dataset).
        # Property names probed against MetaHumanPerformance.h: bAutoBodyHeight gates
        # BodyHeight (Units=Centimeters).
        try:
            performance.set_editor_property("auto_body_height", False)
            performance.set_editor_property("body_height", BODY_HEIGHT_CM)
            _log(f"body height prior applied: {BODY_HEIGHT_CM:.1f}cm (auto estimate off)")
        except Exception as err:
            _log(f"BODY HEIGHT PRIOR NOT APPLIED ({err}) - solve falls back to auto")
    _performance = performance
    return performance


def _do_solve_chunk(_delta):
    global _tick_handle, _pending_chunk
    if _tick_handle is not None:
        unreal.unregister_slate_post_tick_callback(_tick_handle)
        _tick_handle = None
    index = _pending_chunk
    _pending_chunk = None

    existing_cd = f"/Game/CaptureManager/Imports/{SLATE}_1/CD_{SLATE}_1"
    if not unreal.EditorAssetLibrary.does_asset_exist(existing_cd):
        _log(f"NO SAVED INGEST at {existing_cd} - run the monolithic ingest first")
        return
    footage = unreal.load_asset(existing_cd)

    performance = _ensure_performance(footage)
    if not performance:
        return

    start, end = chunk_range(index)
    performance.set_processing_range(start, end)
    got_start = performance.get_editor_property("start_frame_to_process")
    got_end = performance.get_editor_property("end_frame_to_process")
    _log(f"chunk {index}: requested [{start},{end}) applied [{got_start},{got_end}]")

    can, reason = True, ""
    try:
        can = performance.can_process()
    except Exception as err:
        reason = str(err)
    _log(f"can_process={can} {reason}")
    if not can:
        _log("NOT PROCESSABLE - inspect performance asset settings")
        return

    performance.start_pipeline()
    _log(f"chunk {index} pipeline STARTED")


def solve_chunk(index):
    global _tick_handle, _pending_chunk
    _pending_chunk = index
    _tick_handle = unreal.register_slate_post_tick_callback(_do_solve_chunk)
    _log(f"chunk {index} scheduled on next editor tick")


def status():
    if _performance is None:
        _log("status: no performance asset this session")
        return
    processing = _performance.is_processing()
    frames = _performance.get_number_of_processed_frames()
    _log(f"status: processing={processing} frames={frames}")


def export_chunk(index):
    if _performance is None:
        _log("export: no performance asset this session")
        return
    if _performance.is_processing():
        _log("export: still processing - wait for status processing=False")
        return
    name = _export_name(index)
    asset_path = f"{EXPORT_DIR}/{name}"
    if unreal.EditorAssetLibrary.does_asset_exist(asset_path):
        unreal.EditorAssetLibrary.delete_asset(asset_path)
        _log(f"stale {name} deleted before re-export")
    skel = unreal.load_asset(BODY_MESH).get_editor_property("skeleton")
    settings = unreal.MetaHumanPerformanceExportAnimationSettings()
    settings.set_editor_property("show_export_dialog", False)
    settings.set_editor_property("export_body", True)
    settings.set_editor_property("export_face", False)
    settings.set_editor_property("auto_save_anim_sequence", True)
    settings.set_editor_property("package_path", EXPORT_DIR)
    settings.set_editor_property("asset_name", name)
    settings.set_editor_property("target_skeleton_or_skeletal_mesh", skel)
    settings.set_editor_property("export_range", unreal.PerformanceExportRange.PROCESSING_RANGE)
    anim = unreal.MetaHumanPerformanceExportUtils.export_animation_sequence(_performance, settings)
    if anim:
        unreal.EditorAssetLibrary.save_loaded_asset(anim)
        start, end = chunk_range(index)
        _log(f"chunk {index} export OK -> {anim.get_path_name()} len={anim.get_play_length():.3f}s "
             f"expected={(end - start) / 30.0:.3f}s")
    else:
        _log(f"chunk {index} EXPORT FAILED")


def report_exported():
    done = []
    for i in range(NUM_CHUNKS):
        if unreal.EditorAssetLibrary.does_asset_exist(f"{EXPORT_DIR}/{_export_name(i)}"):
            done.append(i)
    _log(f"exported chunks on disk: {done}")
