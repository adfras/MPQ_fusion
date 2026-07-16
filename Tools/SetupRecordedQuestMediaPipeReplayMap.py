#!/usr/bin/env python3
from __future__ import annotations

import sys

import unreal


SOURCE_MAP = "/Game/MetaHumanRooms/L_MetaHumanPreviewRoom_MPQSignalCompare_01"
DEST_MAP = "/Game/MetaHumanRooms/L_MetaHumanRecordedQuestMediaPipeReplay_01"
# Schema-v2 cache (2026-06-12): carries the recorded Quest hand skeletons that drive
# wrist rotation and fingers during replay (Docs/README.md "Current MPQ Fusion State").
# The v1 wrist-only manifest stays beside it for older comparisons.
REPLAY_MANIFEST = (
    "Saved/CodexAgent/Diagnostics/"
    "tracking_fusion_dataset_avatar_locked_sync_calibration_20260609_170656_replay_source_v2_manifest.json"
)
REPLAY_ACTOR_LABEL = "MP_Replay_QuestMediaPipeRecording_Startup"


def set_property(actor: unreal.Actor, name: str, value) -> None:
    try:
        actor.set_editor_property(name, value)
        return
    except Exception:
        pass
    actor.set_editor_property(name[0].lower() + name[1:], value)


def main() -> int:
    if not unreal.EditorAssetLibrary.does_asset_exist(SOURCE_MAP):
        unreal.log_error(f"Source map does not exist: {SOURCE_MAP}")
        return 1

    if not unreal.EditorAssetLibrary.does_asset_exist(DEST_MAP):
        if not unreal.EditorAssetLibrary.duplicate_asset(SOURCE_MAP, DEST_MAP):
            unreal.log_error(f"Failed to duplicate {SOURCE_MAP} to {DEST_MAP}")
            return 1
        unreal.log(f"Created replay map: {DEST_MAP}")
    else:
        unreal.log(f"Replay map already exists, updating: {DEST_MAP}")

    if not unreal.EditorLevelLibrary.load_level(DEST_MAP):
        unreal.log_error(f"Failed to load replay map: {DEST_MAP}")
        return 1

    actors = list(unreal.EditorLevelLibrary.get_all_level_actors())
    removed_live_startups = 0
    for actor in actors:
        actor_class_path = actor.get_class().get_path_name()
        actor_label = actor.get_actor_label()
        if (
            "BP_MetaHumanPreviewRoomAutoQuestStartup" in actor_class_path
            or "AutoQuestStartup" in actor_label
        ):
            unreal.EditorLevelLibrary.destroy_actor(actor)
            removed_live_startups += 1

    replay_class = unreal.load_class(
        None,
        "/Script/MediaPipeDriver.MediaPipeTrackingFusionDatasetReplayActor",
    )
    if replay_class is None:
        unreal.log_error("Failed to load MediaPipeTrackingFusionDatasetReplayActor class")
        return 1

    replay_actors = [
        actor for actor in unreal.EditorLevelLibrary.get_all_level_actors()
        if actor.get_class().get_path_name().endswith(".MediaPipeTrackingFusionDatasetReplayActor")
        or actor.get_actor_label() == REPLAY_ACTOR_LABEL
    ]
    if replay_actors:
        replay_actor = replay_actors[0]
        unreal.log(f"Updating existing replay actor: {replay_actor.get_actor_label()}")
    else:
        replay_actor = unreal.EditorLevelLibrary.spawn_actor_from_class(
            replay_class,
            unreal.Vector(0.0, -80.0, 80.0),
            unreal.Rotator(0.0, 0.0, 0.0),
        )
        unreal.log(f"Spawned replay actor: {REPLAY_ACTOR_LABEL}")

    replay_actor.set_actor_label(REPLAY_ACTOR_LABEL, mark_dirty=True)
    set_property(replay_actor, "ReplayManifestPath", REPLAY_MANIFEST)
    set_property(replay_actor, "ReplayTimeScale", 1.0)
    set_property(replay_actor, "bLoopReplay", True)
    set_property(replay_actor, "bAutoStartOnBeginPlay", True)
    set_property(replay_actor, "bStopReplayOnEndPlay", True)
    set_property(replay_actor, "bDisableCaptureRecorders", True)

    unreal.EditorLevelLibrary.save_current_level()
    unreal.log(
        "Configured recorded Quest+MediaPipe replay map "
        f"{DEST_MAP}; removed_live_startups={removed_live_startups}; "
        f"replay_manifest={REPLAY_MANIFEST}"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
