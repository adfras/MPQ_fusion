#!/usr/bin/env python3
"""Shared desk-presentability pass for the dyad rooms (lobby + interaction).

Both rooms are duplicates of L_MetaHumanPreviewRoom_MPQSignalCompare_01, which was
authored as a tracking-verification space, not a participant-facing one: a disabled
scene-capture mirror quad stands as a giant black monolith on the pawn camera's -X
side, and nothing lights the room above eye level. Fine for tracer-row gates,
unacceptable for a study participant's first impression (2026-07-17 trial feedback).

apply_room_cosmetics() is idempotent and label-keyed like the setup scripts that call
it: hide the mirror/scene-capture actors, add a movable skylight + two shadowless fill
lights. It logs every StaticMeshActor label so a missed monolith can be pinned from
the run log instead of guessing.
"""
from __future__ import annotations

import re

import unreal

# Log-only candidates: the room has BOTH a dead scene-capture quad (the black monolith
# on the pawn camera's -X side — hide it) and mirror-wall elements that carry the
# self-view design (must stay). Never hide by pattern; pin exact labels from the run
# log (the -X location disambiguates the monolith) and pass them to
# apply_room_cosmetics(hide_labels=...).
CANDIDATE_LABEL_PATTERN = re.compile(r"mirror|scenecapture|scene_capture|reflect|quad", re.IGNORECASE)

SKY_LABEL = "MP_DyadRoomSky"
FILL_A_LABEL = "MP_DyadRoomFill_A"
FILL_B_LABEL = "MP_DyadRoomFill_B"
FILL_A_LOCATION = unreal.Vector(0.0, -40.0, 250.0)
FILL_B_LOCATION = unreal.Vector(110.0, 40.0, 250.0)


def _set_props(component, props):
    for name, value in props.items():
        try:
            component.set_editor_property(name, value)
        except Exception as err:  # property name drift between engine versions
            unreal.log_warning(f"cosmetics: could not set {name}: {err}")


def _hide_actor(actor):
    try:
        actor.set_editor_property("actor_hidden_in_game", True)
    except Exception:
        actor.set_actor_hidden_in_game(True)
    unreal.log(f"cosmetics: hidden in game: {actor.get_actor_label()}")


def _find_by_label(actors, label):
    return next((a for a in actors if a.get_actor_label() == label), None)


def _ensure_movable(actor):
    root = actor.root_component
    if root is not None:
        _set_props(root, {"mobility": unreal.ComponentMobility.MOVABLE})


def apply_room_cosmetics(hide_labels=()):
    actors = list(unreal.EditorLevelLibrary.get_all_level_actors())

    # Leftover template BSP: the source room descends from a VR template map, and an
    # additive builder-brush slab survived as level-model geometry. BSP renders via
    # level-owned model components — no actor primitive components — so it is
    # invisible to any actor/component census and shows up only as a giant lit-gray
    # board on the pawn camera's right (2026-07-17 hunt; pixel forensics: lit shading,
    # default tiled texture at boot). Volumes are ABrush subclasses and the builder
    # brush is BRUSH_DEFAULT, so deleting only plain Brush actors with BRUSH_ADD is
    # safe and removes exactly the junk geometry.
    deleted_brushes = 0
    for actor in list(actors):
        if actor.get_class().get_name() != "Brush":
            continue
        try:
            brush_type = actor.get_editor_property("brush_type")
        except Exception:
            brush_type = None
        loc = actor.get_actor_location()
        unreal.log(
            f"cosmetics: BRUSH label={actor.get_actor_label()} type={brush_type} "
            f"loc=({loc.x:.0f},{loc.y:.0f},{loc.z:.0f})"
        )
        if brush_type == unreal.BrushType.BRUSH_ADD:
            unreal.log(f"cosmetics: deleting additive template brush {actor.get_actor_label()}")
            unreal.EditorLevelLibrary.destroy_actor(actor)
            actors.remove(actor)
            deleted_brushes += 1
    # Rebuild geometry even when no brush actors exist: the slab's source brush was
    # deleted long ago, leaving BAKED model surfaces in ULevel->Model — the only UE
    # geometry that renders with no actor at all. A rebuild with zero brushes bakes
    # an empty model, which is exactly what these all-static-mesh rooms want.
    unreal.log(f"cosmetics: brush actors deleted this pass: {deleted_brushes}; rebuilding level geometry")
    try:
        world = unreal.EditorLevelLibrary.get_editor_world()
        unreal.SystemLibrary.execute_console_command(world, "MAP REBUILD")
        unreal.log("cosmetics: MAP REBUILD issued")
    except Exception as err:
        unreal.log_warning(f"cosmetics: MAP REBUILD failed: {err}")

    hidden = 0
    for actor in actors:
        label = actor.get_actor_label()
        cls = actor.get_class().get_name()
        if isinstance(actor, unreal.StaticMeshActor):
            loc = actor.get_actor_location()
            scale = actor.get_actor_scale3d()
            unreal.log(
                f"cosmetics: SMA label={label} cls={cls} "
                f"loc=({loc.x:.0f},{loc.y:.0f},{loc.z:.0f}) "
                f"scale=({scale.x:.1f},{scale.y:.1f},{scale.z:.1f})"
            )
        if CANDIDATE_LABEL_PATTERN.search(label):
            unreal.log(f"cosmetics: hide-candidate label={label} cls={cls}")
        if label in hide_labels:
            _hide_actor(actor)
            hidden += 1

    sky = _find_by_label(actors, SKY_LABEL)
    if sky is None:
        sky = unreal.EditorLevelLibrary.spawn_actor_from_class(
            unreal.SkyLight, unreal.Vector(0.0, 0.0, 300.0), unreal.Rotator(0.0, 0.0, 0.0))
        unreal.log("cosmetics: spawned skylight")
    if sky is not None:
        sky.set_actor_label(SKY_LABEL, mark_dirty=True)
        _ensure_movable(sky)
        sky_component = sky.get_component_by_class(unreal.SkyLightComponent)
        if sky_component is not None:
            # real_time_capture=False on purpose: a continuously-capturing skylight
            # breaks HighResShot's off-screen re-render (giant tiled/black GI block on
            # the 2026-07-17 journey captures) while the live window looks fine. The
            # rooms are static; one capture at load is correct anyway.
            _set_props(sky_component, {
                "real_time_capture": False,
                "intensity": 1.0,
            })
            try:
                sky_component.recapture_sky()
            except Exception as err:
                unreal.log_warning(f"cosmetics: recapture_sky failed: {err}")

    for label, location in ((FILL_A_LABEL, FILL_A_LOCATION), (FILL_B_LABEL, FILL_B_LOCATION)):
        fill = _find_by_label(actors, label)
        if fill is None:
            fill = unreal.EditorLevelLibrary.spawn_actor_from_class(
                unreal.PointLight, location, unreal.Rotator(0.0, 0.0, 0.0))
            unreal.log(f"cosmetics: spawned fill light {label}")
        if fill is None:
            continue
        fill.set_actor_label(label, mark_dirty=True)
        _ensure_movable(fill)
        fill.set_actor_location_and_rotation(location, unreal.Rotator(0.0, 0.0, 0.0), False, False)
        light_component = fill.get_component_by_class(unreal.PointLightComponent)
        if light_component is not None:
            # 5000 lm blew the room out against its fixed exposure volume (2026-07-17
            # trial feedback: "glared out"); 1600 reads as soft office light.
            _set_props(light_component, {
                "intensity": 1600.0,
                "attenuation_radius": 1400.0,
                "cast_shadows": False,
                "soft_source_radius": 30.0,
            })

    unreal.log(f"cosmetics: done (hidden={hidden})")
