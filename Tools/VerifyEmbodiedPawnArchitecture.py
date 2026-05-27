import json
import math
import os
import traceback

import unreal


PROJECT_DIR = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
OUT_DIR = os.path.join(PROJECT_DIR, "Saved", "CodexAgent", "EmbodimentInspection")
OUT_PATH = os.path.join(OUT_DIR, "testingkit3_embodied_pawn_verify_result.json")

NATIVE_CLASS_PATH = "/Script/MediaPipeDriver.MediaPipeEmbodiedAvatarPawn"
BP_CLASS_PATH = "/Game/MetaHumanRooms/Blueprints/BP_MP_EmbodiedMannyPawn.BP_MP_EmbodiedMannyPawn_C"
MAP_PATH = "/Game/MetaHumanRooms/L_MetaHumanPreviewRoom_02"
PAWN_LABEL = "MP_PlacedEmbodiedMannyPawn"
PLACED_PAWN_TAG = "TestingKit3_PlacedEmbodiedAvatarPawn"
EMBODIED_START_TAG = "TestingKit3_AutoQuestEmbodiedStart"
MIRROR_LABEL = "VRRoom_SelfMirror"
STARTUP_LABEL = "BP_MetaHumanPreviewRoomAutoQuestStartup_Instance"


def write_result(result):
    os.makedirs(OUT_DIR, exist_ok=True)
    with open(OUT_PATH, "w", encoding="utf-8") as f:
        json.dump(result, f, indent=2, sort_keys=True)


def assert_true(result, condition, message):
    result.setdefault("checks", []).append({"ok": bool(condition), "message": message})
    if not condition:
        raise RuntimeError(message)


def close_vec(actual, expected, tolerance=0.05):
    return (
        len(actual) == len(expected)
        and all(abs(float(a) - float(e)) <= tolerance for a, e in zip(actual, expected))
    )


def get_prop(obj, names, default=None):
    for name in names:
        try:
            return obj.get_editor_property(name)
        except Exception:
            pass
    return default


def get_console_int(name):
    system_library = getattr(unreal, "SystemLibrary", None)
    getter = getattr(system_library, "get_console_variable_int_value", None) if system_library else None
    if getter:
        try:
            return int(getter(name))
        except Exception:
            return None
    return None


def actor_tags(actor):
    return [str(tag) for tag in list(get_prop(actor, ["tags"], []))]


def actor_summary(actor):
    location = actor.get_actor_location()
    rotation = actor.get_actor_rotation()
    hidden = get_prop(actor, ["is_hidden_ed", "hidden"], None)
    return {
        "label": actor.get_actor_label(),
        "class": actor.get_class().get_path_name(),
        "location": [location.x, location.y, location.z],
        "rotation": [rotation.pitch, rotation.yaw, rotation.roll],
        "tags": actor_tags(actor),
        "hidden": bool(hidden) if hidden is not None else None,
    }


def camera_summary(actor):
    camera = actor.get_component_by_class(unreal.CameraComponent)
    if not camera:
        return None
    parent = camera.get_attach_parent()
    rel = get_prop(camera, ["relative_location"], None)
    if rel is None:
        rel_transform = camera.get_relative_transform()
        rel = rel_transform.translation
    return {
        "name": camera.get_name(),
        "parent": parent.get_name() if parent else None,
        "relative_location": [rel.x, rel.y, rel.z],
        "lock_to_hmd": bool(get_prop(camera, ["lock_to_hmd", "b_lock_to_hmd", "bLockToHmd"], False)),
    }


def scene_component_summary(actor, name):
    for component in actor.get_components_by_class(unreal.SceneComponent):
        if component and component.get_name() == name:
            parent = component.get_attach_parent()
            rel = get_prop(component, ["relative_location"], None)
            if rel is None:
                rel_transform = component.get_relative_transform()
                rel = rel_transform.translation
            return {
                "name": component.get_name(),
                "parent": parent.get_name() if parent else None,
                "relative_location": [rel.x, rel.y, rel.z],
            }
    return None


def poseable_mesh_summary(actor, name):
    poseable_class = getattr(unreal, "PoseableMeshComponent", None)
    if not poseable_class:
        return None
    for component in actor.get_components_by_class(poseable_class):
        if component and component.get_name() == name:
            parent = component.get_attach_parent()
            rel = get_prop(component, ["relative_location"], None)
            rot = get_prop(component, ["relative_rotation"], None)
            skinned_asset = get_prop(component, ["skinned_asset"], None)
            return {
                "name": component.get_name(),
                "parent": parent.get_name() if parent else None,
                "relative_location": [rel.x, rel.y, rel.z] if rel else None,
                "relative_rotation": [rot.pitch, rot.yaw, rot.roll] if rot else None,
                "owner_no_see": bool(get_prop(component, ["owner_no_see"], False)),
                "only_owner_see": bool(get_prop(component, ["only_owner_see"], False)),
                "hidden_in_game": bool(get_prop(component, ["hidden_in_game"], False)),
                "visible": bool(get_prop(component, ["visible"], False)),
                "skinned_asset": skinned_asset.get_path_name() if hasattr(skinned_asset, "get_path_name") else str(skinned_asset),
            }
    return None


def motion_controller_summary(actor, name):
    motion_controller_class = getattr(unreal, "MotionControllerComponent", None)
    if not motion_controller_class:
        return None
    for component in actor.get_components_by_class(motion_controller_class):
        if component and component.get_name() == name:
            parent = component.get_attach_parent()
            return {
                "name": component.get_name(),
                "parent": parent.get_name() if parent else None,
                "motion_source": str(get_prop(component, ["motion_source"], "")),
                "hidden_in_game": bool(get_prop(component, ["hidden_in_game"], False)),
                "visible": bool(get_prop(component, ["visible"], False)),
            }
    return None


def main():
    result = {
        "success": False,
        "map": MAP_PATH,
        "native_class": NATIVE_CLASS_PATH,
        "blueprint_class": BP_CLASS_PATH,
        "checks": [],
    }

    native_class = unreal.load_class(None, NATIVE_CLASS_PATH)
    bp_class = unreal.load_class(None, BP_CLASS_PATH)
    assert_true(result, native_class is not None, "native embodied pawn class loads")
    assert_true(result, bp_class is not None, "embodied pawn Blueprint generated class loads")

    assert_true(result, bool(unreal.EditorLevelLibrary.load_level(MAP_PATH)), "VR room map loads")

    actors = list(unreal.EditorLevelLibrary.get_all_level_actors())
    pawn_actors = [
        actor for actor in actors
        if actor and (
            actor.get_actor_label() == PAWN_LABEL
            or PLACED_PAWN_TAG in actor_tags(actor)
            or actor.get_class().get_path_name() == BP_CLASS_PATH
        )
    ]
    result["placed_pawns"] = [actor_summary(actor) for actor in pawn_actors]
    assert_true(result, len(pawn_actors) == 1, f"exactly one placed embodied pawn exists, found {len(pawn_actors)}")

    pawn = pawn_actors[0]
    pawn_details = actor_summary(pawn)
    pawn_details["auto_possess_player"] = str(get_prop(pawn, ["auto_possess_player"], None))
    pawn_details["start_tracking_on_begin_play"] = bool(get_prop(pawn, ["start_tracking_on_begin_play"], False))
    pawn_details["use_media_pipe_tracking"] = bool(get_prop(pawn, ["use_media_pipe_tracking"], True))
    pawn_details["drive_movement_replica_pose"] = bool(get_prop(pawn, ["drive_movement_replica_pose"], False))
    pawn_details["vr_origin"] = scene_component_summary(pawn, "VROrigin")
    pawn_details["camera"] = camera_summary(pawn)
    pawn_details["avatar_mesh"] = poseable_mesh_summary(pawn, "AvatarMesh")
    pawn_details["local_avatar_mesh"] = poseable_mesh_summary(pawn, "LocalAvatarMesh")
    pawn_details["motion_controller_left"] = motion_controller_summary(pawn, "MotionControllerLeft")
    pawn_details["motion_controller_right"] = motion_controller_summary(pawn, "MotionControllerRight")
    root = get_prop(pawn, ["root_component"], None)
    pawn_details["root_component"] = root.get_name() if root else None
    result["placed_pawn"] = pawn_details

    assert_true(result, pawn_details["class"] == BP_CLASS_PATH, f"placed pawn class is {BP_CLASS_PATH}")
    assert_true(result, pawn_details["root_component"] == "AvatarRoot", "placed pawn root is AvatarRoot")
    assert_true(result, "PLAYER0" in pawn_details["auto_possess_player"], "placed pawn auto-possesses Player0")
    assert_true(result, pawn_details["start_tracking_on_begin_play"], "placed pawn starts embodiment on BeginPlay")
    assert_true(result, not pawn_details["use_media_pipe_tracking"], "placed pawn uses Movement-replica mode, not MediaPipe tracking")
    assert_true(result, pawn_details["drive_movement_replica_pose"], "placed pawn drives the Movement-replica HMD pose")
    assert_true(result, PLACED_PAWN_TAG in pawn_details["tags"], "placed pawn has placed-pawn tag")
    assert_true(result, EMBODIED_START_TAG in pawn_details["tags"], "placed pawn has embodied-start tag")
    assert_true(result, close_vec(pawn_details["location"], [0.0, -170.0, 0.0]), "placed pawn location matches VR room embodiment start")
    assert_true(result, math.isclose(pawn_details["rotation"][1], 90.0, abs_tol=0.05), "placed pawn yaw faces the mirror")

    vr_origin = pawn_details["vr_origin"]
    assert_true(result, vr_origin is not None, "placed pawn has VROrigin eye anchor")
    assert_true(result, vr_origin["parent"] == "AvatarRoot", "VROrigin is attached to AvatarRoot")
    assert_true(
        result,
        close_vec(vr_origin["relative_location"], [10.66, 0.0, 162.58]),
        "VROrigin relative location matches rotated internal Manny eye profile",
    )

    camera = pawn_details["camera"]
    assert_true(result, camera is not None, "placed pawn has VRCamera")
    assert_true(result, camera["name"] == "VRCamera", "camera component is named VRCamera")
    assert_true(
        result,
        str(camera["parent"]).startswith("VROrigin"),
        f"camera is attached to VROrigin, actual parent={camera['parent']}",
    )
    assert_true(result, camera["lock_to_hmd"], "camera is HMD locked")
    assert_true(
        result,
        close_vec(camera["relative_location"], [0.0, 0.0, 0.0]),
        "camera relative location is zero so LockToHMD applies under VROrigin",
    )

    avatar_mesh = pawn_details["avatar_mesh"]
    local_avatar_mesh = pawn_details["local_avatar_mesh"]
    assert_true(result, avatar_mesh is not None, "placed pawn owns full AvatarMesh")
    assert_true(result, avatar_mesh["parent"] == "AvatarRoot", "full AvatarMesh is attached to AvatarRoot")
    assert_true(result, avatar_mesh["skinned_asset"].endswith("/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple.SKM_Manny_Simple"), "full AvatarMesh uses TestingKit3 Manny")
    assert_true(result, math.isclose(avatar_mesh["relative_rotation"][1], -90.0, abs_tol=0.05), "full AvatarMesh copies Movement Manny -90 yaw attachment")
    assert_true(result, avatar_mesh["owner_no_see"] and not avatar_mesh["only_owner_see"], "full AvatarMesh is hidden only from the owning first-person view")
    assert_true(result, local_avatar_mesh is not None, "placed pawn owns first-person LocalAvatarMesh")
    assert_true(result, local_avatar_mesh["parent"] == "AvatarRoot", "first-person LocalAvatarMesh is attached to AvatarRoot")
    assert_true(result, local_avatar_mesh["only_owner_see"] and not local_avatar_mesh["owner_no_see"], "first-person LocalAvatarMesh is owner-only")

    left_mc = pawn_details["motion_controller_left"]
    right_mc = pawn_details["motion_controller_right"]
    assert_true(result, left_mc is not None and left_mc["parent"] == "AvatarRoot", "left MotionController component is attached to AvatarRoot")
    assert_true(result, right_mc is not None and right_mc["parent"] == "AvatarRoot", "right MotionController component is attached to AvatarRoot")
    assert_true(result, left_mc["motion_source"] == "Left", "left MotionController source is Left")
    assert_true(result, right_mc["motion_source"] == "Right", "right MotionController source is Right")

    mirror_actors = [actor_summary(actor) for actor in actors if actor and actor.get_actor_label() == MIRROR_LABEL]
    startup_actors = [actor_summary(actor) for actor in actors if actor and actor.get_actor_label() == STARTUP_LABEL]
    result["mirror_actors"] = mirror_actors
    result["startup_actors"] = startup_actors
    assert_true(result, len(mirror_actors) == 1, "VR room mirror actor remains present")
    assert_true(result, len(startup_actors) == 1, "existing AutoQuest startup actor remains present")

    auto_quest_avatar = get_console_int("mp.AutoQuestAvatar")
    auto_quest_embodied_view = get_console_int("mp.AutoQuestEmbodiedView")
    result["cvars"] = {
        "mp.AutoQuestAvatar": auto_quest_avatar,
        "mp.AutoQuestEmbodiedView": auto_quest_embodied_view,
    }
    if auto_quest_avatar is not None:
        assert_true(result, auto_quest_avatar == 0, "mp.AutoQuestAvatar defaults to internal Manny")
    if auto_quest_embodied_view is not None:
        assert_true(result, auto_quest_embodied_view == 1, "mp.AutoQuestEmbodiedView remains enabled")

    result["success"] = True
    write_result(result)


try:
    main()
except Exception as exc:
    failed = {
        "success": False,
        "error": str(exc),
        "traceback": traceback.format_exc(),
    }
    write_result(failed)
    unreal.log_error(f"[VerifyEmbodiedPawnArchitecture] {exc}")
    raise
