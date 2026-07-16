#!/usr/bin/env python3
"""Create/refresh L_DyadInteraction_01 (DYADIC_STUDY_PLAN Phase 4).

Face-to-face room built from the preview room: the placed pawn keeps its spot (self,
live-driven, facing +Y); ADyadInteractionStageActor marks the PARTNER's spot ~2.5 m in
front of it (the stage spawns the wire-driven partner rig there on arrival); a neutral
task table stands between them. The room's mirror wall carries self-visibility (the
pawn's self-view copy is disabled by the stage on arrival).

Run headless AFTER building DyadStudy:
  UnrealEditor-Cmd.exe <uproject> -run=pythonscript \
    -script=".../Tools/SetupDyadInteractionMap.py" -unattended -nullrhi -nosplash
"""
from __future__ import annotations

import sys

import unreal

SOURCE_MAP = "/Game/MetaHumanRooms/L_MetaHumanPreviewRoom_MPQSignalCompare_01"
DEST_MAP = "/Game/MetaHumanRooms/L_DyadInteraction_01"
STAGE_ACTOR_LABEL = "MP_DyadInteractionStage"
TABLE_ACTOR_LABEL = "MP_DyadTaskTable"

# Pawn sits at (0, -170) facing +Y: partner spot 2.5 m in front, table midway.
STAGE_LOCATION = unreal.Vector(0.0, 80.0, 0.0)
# MetaHuman actors visually face +Y at yaw 0: world yaw 180 lands the partner facing
# -Y, straight at the pawn across the table.
STAGE_ROTATION = unreal.Rotator(0.0, 0.0, 180.0)
TABLE_LOCATION = unreal.Vector(0.0, -45.0, 37.0)
TABLE_SCALE = unreal.Vector(1.2, 0.7, 0.74)


def main() -> int:
    if not unreal.EditorAssetLibrary.does_asset_exist(SOURCE_MAP):
        unreal.log_error(f"Source map does not exist: {SOURCE_MAP}")
        return 1

    if not unreal.EditorAssetLibrary.does_asset_exist(DEST_MAP):
        if not unreal.EditorAssetLibrary.duplicate_asset(SOURCE_MAP, DEST_MAP):
            unreal.log_error(f"Failed to duplicate {SOURCE_MAP} to {DEST_MAP}")
            return 1
        unreal.log(f"Created interaction map: {DEST_MAP}")
    else:
        unreal.log(f"Interaction map already exists, updating: {DEST_MAP}")

    if not unreal.EditorLevelLibrary.load_level(DEST_MAP):
        unreal.log_error(f"Failed to load interaction map: {DEST_MAP}")
        return 1

    stage_class = unreal.load_class(None, "/Script/DyadStudy.DyadInteractionStageActor")
    if stage_class is None:
        unreal.log_error("DyadInteractionStageActor class not found - build DyadStudy first")
        return 1

    actors = list(unreal.EditorLevelLibrary.get_all_level_actors())

    stage = next(
        (actor for actor in actors
         if actor.get_class().get_path_name().endswith(".DyadInteractionStageActor")
         or actor.get_actor_label() == STAGE_ACTOR_LABEL),
        None,
    )
    if stage is None:
        stage = unreal.EditorLevelLibrary.spawn_actor_from_class(stage_class, STAGE_LOCATION, STAGE_ROTATION)
        if stage is None:
            unreal.log_error("Failed to spawn DyadInteractionStageActor")
            return 1
        unreal.log("Spawned DyadInteractionStageActor")
    stage.set_actor_label(STAGE_ACTOR_LABEL, mark_dirty=True)
    stage.set_actor_location_and_rotation(STAGE_LOCATION, STAGE_ROTATION, False, False)

    table = next((actor for actor in actors if actor.get_actor_label() == TABLE_ACTOR_LABEL), None)
    if table is None:
        cube = unreal.EditorAssetLibrary.load_asset("/Engine/BasicShapes/Cube.Cube")
        table = unreal.EditorLevelLibrary.spawn_actor_from_class(
            unreal.StaticMeshActor, TABLE_LOCATION, unreal.Rotator(0.0, 0.0, 0.0))
        if table is None:
            unreal.log_error("Failed to spawn task table")
            return 1
        mesh_component = table.static_mesh_component
        mesh_component.set_editor_property("mobility", unreal.ComponentMobility.MOVABLE)
        mesh_component.set_editor_property("static_mesh", cube)
        unreal.log("Spawned task table")
    table.set_actor_label(TABLE_ACTOR_LABEL, mark_dirty=True)
    table.set_actor_location_and_rotation(TABLE_LOCATION, unreal.Rotator(0.0, 0.0, 0.0), False, False)
    table.set_actor_scale3d(TABLE_SCALE)

    unreal.EditorLevelLibrary.save_current_level()
    unreal.log(f"Configured dyad interaction map {DEST_MAP}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
