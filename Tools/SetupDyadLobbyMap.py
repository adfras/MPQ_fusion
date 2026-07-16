#!/usr/bin/env python3
"""Create/refresh L_DyadLobby_01 (DYADIC_STUDY_PLAN Phase 2).

Duplicates the preview room and places one ADyadLobbyStageActor (the lobby conductor:
menu widget, live-pose tee, partner-preview rig). The live self-preview needs no map
work — the preview room's placed pawn assembles it at play start, exactly like the
mirror demo.

Run headless AFTER building DyadStudy:
  UnrealEditor-Cmd.exe <uproject> -run=pythonscript \
    -script="D:/Epic/Unreal_Projects/TestingKit5/Tools/SetupDyadLobbyMap.py" \
    -unattended -nullrhi -nosplash
"""
from __future__ import annotations

import sys

import unreal

SOURCE_MAP = "/Game/MetaHumanRooms/L_MetaHumanPreviewRoom_MPQSignalCompare_01"
DEST_MAP = "/Game/MetaHumanRooms/L_DyadLobby_01"
STAGE_ACTOR_LABEL = "MP_DyadLobbyStage"


def main() -> int:
    if not unreal.EditorAssetLibrary.does_asset_exist(SOURCE_MAP):
        unreal.log_error(f"Source map does not exist: {SOURCE_MAP}")
        return 1

    if not unreal.EditorAssetLibrary.does_asset_exist(DEST_MAP):
        if not unreal.EditorAssetLibrary.duplicate_asset(SOURCE_MAP, DEST_MAP):
            unreal.log_error(f"Failed to duplicate {SOURCE_MAP} to {DEST_MAP}")
            return 1
        unreal.log(f"Created lobby map: {DEST_MAP}")
    else:
        unreal.log(f"Lobby map already exists, updating: {DEST_MAP}")

    if not unreal.EditorLevelLibrary.load_level(DEST_MAP):
        unreal.log_error(f"Failed to load lobby map: {DEST_MAP}")
        return 1

    stage_class = unreal.load_class(None, "/Script/DyadStudy.DyadLobbyStageActor")
    if stage_class is None:
        unreal.log_error("DyadLobbyStageActor class not found - build DyadStudy first")
        return 1

    existing = [
        actor for actor in unreal.EditorLevelLibrary.get_all_level_actors()
        if actor.get_class().get_path_name().endswith(".DyadLobbyStageActor")
        or actor.get_actor_label() == STAGE_ACTOR_LABEL
    ]
    # The placed pawn faces +Y toward the mirror wall (pawn at Y=-170, yaw 90); from its
    # camera, screen-left is world +X and the disabled scene-capture mirror quad blocks
    # the -X side. Stage on +X: menu at eye height screen-left, partner preview beside
    # the self-view avatar, both clear of the quad. Placement re-asserted on every run.
    STAGE_LOCATION = unreal.Vector(80.0, 10.0, 0.0)
    STAGE_ROTATION = unreal.Rotator(0.0, 0.0, -90.0)
    if existing:
        stage = existing[0]
        unreal.log(f"Updating existing stage actor: {stage.get_actor_label()}")
        stage.set_actor_location_and_rotation(STAGE_LOCATION, STAGE_ROTATION, False, False)
    else:
        stage = unreal.EditorLevelLibrary.spawn_actor_from_class(
            stage_class, STAGE_LOCATION, STAGE_ROTATION)
        if stage is None:
            unreal.log_error("Failed to spawn DyadLobbyStageActor")
            return 1
        unreal.log("Spawned DyadLobbyStageActor")

    stage.set_actor_label(STAGE_ACTOR_LABEL, mark_dirty=True)
    unreal.EditorLevelLibrary.save_current_level()
    unreal.log(f"Configured dyad lobby map {DEST_MAP}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
