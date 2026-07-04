"""Offline MHA body solve of the mha_groundtruth Camo video.

Phases (each safe to trigger over MCP):

    import mha_offline_solve
    mha_offline_solve.run()      # schedules ingest + performance + solve start
    mha_offline_solve.status()   # logs pipeline progress
    mha_offline_solve.export()   # exports AnimSequence once processing is done

All heavy work runs in a one-shot slate post-tick callback so the MCP
request that triggers it closes before anything can pump the engine loop
(see Docs/UNREAL_MCP_OPERATIONS.md rule 1). Progress is logged with the
[MHASolve] prefix; poll with tail_log.
"""

import unreal

# Module globals - override before run() for new takes:
#   mod.VIDEO = r"C:\...\take.mp4"; mod.SLATE = "mha_groundtruth_take2"; mod.PERF_NAME = "PF_..._Take2"
VIDEO = globals().get("VIDEO", r"C:\Users\Alan\Videos\Camo Studio Recording 2026-07-03 21-28-41.mp4")
SLATE = globals().get("SLATE", "mha_groundtruth")
PERF_DIR = globals().get("PERF_DIR", "/Game/MHAGroundTruth")
PERF_NAME = globals().get("PERF_NAME", "PF_MHA_GroundTruth_Take1")
EXPORT_DIR = globals().get("EXPORT_DIR", "/Game/MHAGroundTruth")

_tick_handle = None
_performance = None


def _log(msg):
    unreal.log(f"[MHASolve] {msg}")


def _do_run(_delta):
    global _tick_handle, _performance
    if _tick_handle is not None:
        unreal.unregister_slate_post_tick_callback(_tick_handle)
        _tick_handle = None

    existing_cd = "/Game/CaptureManager/Imports/mha_groundtruth_1/CD_mha_groundtruth_1"
    if unreal.EditorAssetLibrary.does_asset_exist(existing_cd):
        footage = unreal.load_asset(existing_cd)
        _log(f"reusing saved ingest -> {footage.get_path_name()}")
    else:
        _log(f"ingesting {VIDEO}")
        params = unreal.CaptureManagerConversionParams()
        footage, error = unreal.CaptureManagerIngestBlueprintLibrary.ingest_mono_video_sync(
            VIDEO, "", SLATE, 1, params)
        if not footage:
            _log(f"INGEST FAILED: {error}")
            return
        _log(f"ingest OK -> {footage.get_path_name()}")

    asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
    perf_path = f"{PERF_DIR}/{PERF_NAME}"
    if unreal.EditorAssetLibrary.does_asset_exist(perf_path):
        performance = unreal.load_asset(perf_path)
        _log("performance asset exists, reusing")
    else:
        performance = asset_tools.create_asset(
            asset_name=PERF_NAME,
            package_path=PERF_DIR,
            asset_class=unreal.MetaHumanPerformance,
            factory=unreal.new_object(type=unreal.MetaHumanPerformanceFactoryNew),
        )
    if not performance:
        _log("PERFORMANCE ASSET CREATION FAILED")
        return
    _performance = performance

    performance.set_editor_property("input_type", unreal.DataInputType.MONO_FOOTAGE)
    performance.set_editor_property("footage_capture_data", footage)
    performance.set_editor_property("body_tracking", True)
    try:
        performance.set_editor_property("face_tracking", False)
    except Exception as err:
        _log(f"face_tracking not settable ({err}); continuing")

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
    _log("pipeline STARTED (non-blocking); poll with status()")


def run():
    global _tick_handle
    _tick_handle = unreal.register_slate_post_tick_callback(_do_run)
    _log("run scheduled on next editor tick")


def _find_performance():
    global _performance
    if _performance is None:
        path = f"{PERF_DIR}/{PERF_NAME}"
        if unreal.EditorAssetLibrary.does_asset_exist(path):
            _performance = unreal.load_asset(path)
    return _performance


def status():
    performance = _find_performance()
    if not performance:
        _log("status: no performance asset")
        return
    processing = performance.is_processing()
    frames = performance.get_number_of_processed_frames()
    _log(f"status: processing={processing} frames={frames}")


def export():
    performance = _find_performance()
    if not performance:
        _log("export: no performance asset")
        return
    if performance.is_processing():
        _log("export: still processing - wait for status processing=False")
        return
    settings = unreal.MetaHumanPerformanceExportAnimationSettings()
    settings.set_editor_property("show_export_dialog", False)
    try:
        settings.set_editor_property("export_range", unreal.PerformanceExportRange.PROCESSING_RANGE)
    except Exception:
        pass
    anim = unreal.MetaHumanPerformanceExportUtils.export_animation_sequence(performance, settings)
    if anim:
        unreal.EditorAssetLibrary.save_loaded_asset(anim)
        _log(f"export OK -> {anim.get_path_name()}")
    else:
        _log("EXPORT FAILED")
