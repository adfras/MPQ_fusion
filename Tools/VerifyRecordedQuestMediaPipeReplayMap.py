#!/usr/bin/env python3
from __future__ import annotations

import json
import sys

import unreal


MAP_PATH = "/Game/MetaHumanRooms/L_MetaHumanRecordedQuestMediaPipeReplay_01"
REPLAY_LABEL = "MP_Replay_QuestMediaPipeRecording_Startup"


def get_property(actor: unreal.Actor, name: str):
    try:
        return actor.get_editor_property(name)
    except Exception:
        return actor.get_editor_property(name[0].lower() + name[1:])


def main() -> int:
    if not unreal.EditorAssetLibrary.does_asset_exist(MAP_PATH):
        unreal.log_error(f"Replay map does not exist: {MAP_PATH}")
        return 1
    if not unreal.EditorLevelLibrary.load_level(MAP_PATH):
        unreal.log_error(f"Failed to load replay map: {MAP_PATH}")
        return 1

    actors = list(unreal.EditorLevelLibrary.get_all_level_actors())
    replay_actors = [
        actor for actor in actors
        if actor.get_actor_label() == REPLAY_LABEL
        or actor.get_class().get_path_name().endswith(".MediaPipeTrackingFusionDatasetReplayActor")
    ]
    live_startups = [
        actor for actor in actors
        if "BP_MetaHumanPreviewRoomAutoQuestStartup" in actor.get_class().get_path_name()
        or "AutoQuestStartup" in actor.get_actor_label()
    ]
    skeletal_or_metahuman = [
        actor.get_actor_label() for actor in actors
        if "Kellan" in actor.get_actor_label()
        or "MediaPipePoseDrivenSkeletalActor" in actor.get_class().get_path_name()
        or "MetaHuman" in actor.get_class().get_path_name()
    ]

    result = {
        "map": MAP_PATH,
        "actor_count": len(actors),
        "replay_actor_count": len(replay_actors),
        "live_autoquest_startup_count": len(live_startups),
        "driven_actor_hints": skeletal_or_metahuman,
    }
    if replay_actors:
        replay_actor = replay_actors[0]
        result["replay_actor_label"] = replay_actor.get_actor_label()
        result["replay_actor_class"] = replay_actor.get_class().get_path_name()
        result["replay_manifest_path"] = str(get_property(replay_actor, "ReplayManifestPath"))
        result["auto_start_on_begin_play"] = bool(get_property(replay_actor, "bAutoStartOnBeginPlay"))
        result["loop_replay"] = bool(get_property(replay_actor, "bLoopReplay"))
        result["disable_capture_recorders"] = bool(get_property(replay_actor, "bDisableCaptureRecorders"))

    print(json.dumps(result, indent=2))
    if len(replay_actors) != 1:
        unreal.log_error(f"Expected exactly one replay actor, found {len(replay_actors)}")
        return 1
    if live_startups:
        unreal.log_error(f"Expected no live AutoQuest startup actors, found {len(live_startups)}")
        return 1
    if not skeletal_or_metahuman:
        unreal.log_error("Did not find driven MetaHuman/MediaPipe actor hints")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
