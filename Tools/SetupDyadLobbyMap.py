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

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import unreal

from dyad_map_cosmetics import apply_room_cosmetics

SOURCE_MAP = "/Game/MetaHumanRooms/L_MetaHumanPreviewRoom_MPQSignalCompare_01"
DEST_MAP = "/Game/MetaHumanRooms/L_DyadLobby_01"
STAGE_ACTOR_LABEL = "MP_DyadLobbyStage"

# Exact labels to hide in-game (the dead scene-capture monolith etc.) — pinned from the
# cosmetics SMA log, never matched by pattern (the room's functional mirror-wall
# elements share the obvious keywords).
HIDE_LABELS = ()


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
    # camera, screen-left is world +X. The self-view mirror copy stands center (~Y115)
    # and the partner preview lands left of it, so the stage sits on +X. The menu panel
    # offsets to the -X (screen-right) column via its component transform in the stage
    # ctor — that side is free because FDyadStudyRoomPolicy suppresses the live-trial
    # tracking panel (the black monolith that history mislabeled a "mirror quad") inside
    # dyad rooms. Placement re-asserted on every run.
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

    # The menu widget rendered a second, dark, X=0-mirrored copy of itself (the
    # "black board", present since Phase 2). It ignores depth, so a blocker prop
    # cannot hide it — one was tried and removed here. Root fix lives in the stage
    # ctor: the ghost is the widget's flipped-winding backface, killed by
    # SetTwoSided(false).
    stale_blocker = next(
        (actor for actor in unreal.EditorLevelLibrary.get_all_level_actors()
         if actor.get_actor_label() == "MP_DyadMenuGhostBlocker"),
        None,
    )
    if stale_blocker is not None:
        unreal.EditorLevelLibrary.destroy_actor(stale_blocker)
        unreal.log("Removed stale menu ghost blocker")

    # Stage-1 dressing mirror (2026-07-17 participant design): a framed, glass-tinted
    # panel around the self-view clone's spot so the clone reads as a reflection. The
    # clone stands at world (0,107) facing the participant; the frame plane sits just
    # in front at Y=90, the tinted backing just behind. All pieces carry the
    # DyadMirrorDeco tag — ADyadLobbyStageActor strikes the set when the mirror "goes
    # away" at the partner stage. Materials are borrowed from existing room actors so
    # the palette stays coherent (bookshelf wood for the frame, wall-art glass-blue
    # for the backing).
    all_actors = unreal.EditorLevelLibrary.get_all_level_actors()

    def _find_material(label):
        for candidate in all_actors:
            if candidate.get_actor_label() == label and isinstance(candidate, unreal.StaticMeshActor):
                return candidate.static_mesh_component.get_material(0)
        return None

    frame_material = _find_material("Furnishing_Bookshelf_Frame")
    glass_material = _find_material("Decor_WallArt_Main")
    cube = unreal.EditorAssetLibrary.load_asset("/Engine/BasicShapes/Cube.Cube")

    MIRROR_PIECES = (
        ("MP_DyadMirror_SlatLeft", unreal.Vector(-69.0, 90.0, 110.0), unreal.Vector(0.08, 0.06, 2.2), frame_material),
        ("MP_DyadMirror_SlatRight", unreal.Vector(69.0, 90.0, 110.0), unreal.Vector(0.08, 0.06, 2.2), frame_material),
        ("MP_DyadMirror_SlatTop", unreal.Vector(0.0, 90.0, 218.0), unreal.Vector(1.46, 0.06, 0.08), frame_material),
        ("MP_DyadMirror_SlatBottom", unreal.Vector(0.0, 90.0, 2.0), unreal.Vector(1.46, 0.06, 0.08), frame_material),
        ("MP_DyadMirror_Backing", unreal.Vector(0.0, 138.0, 110.0), unreal.Vector(1.42, 0.02, 2.16), glass_material),
    )
    for piece_label, piece_loc, piece_scale, piece_material in MIRROR_PIECES:
        piece = next((a for a in all_actors if a.get_actor_label() == piece_label), None)
        if piece is None:
            piece = unreal.EditorLevelLibrary.spawn_actor_from_class(
                unreal.StaticMeshActor, piece_loc, unreal.Rotator(0.0, 0.0, 0.0))
            if piece is None:
                unreal.log_error(f"Failed to spawn mirror piece {piece_label}")
                return 1
            mesh_component = piece.static_mesh_component
            mesh_component.set_editor_property("mobility", unreal.ComponentMobility.MOVABLE)
            mesh_component.set_editor_property("static_mesh", cube)
            unreal.log(f"Spawned mirror piece {piece_label}")
        piece.set_actor_label(piece_label, mark_dirty=True)
        piece.set_actor_location_and_rotation(piece_loc, unreal.Rotator(0.0, 0.0, 0.0), False, False)
        piece.set_actor_scale3d(piece_scale)
        if piece_material is not None:
            piece.static_mesh_component.set_material(0, piece_material)
        piece.set_editor_property("tags", ["DyadMirrorDeco"])

    apply_room_cosmetics(hide_labels=HIDE_LABELS)
    unreal.EditorLevelLibrary.save_current_level()
    unreal.log(f"Configured dyad lobby map {DEST_MAP}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
