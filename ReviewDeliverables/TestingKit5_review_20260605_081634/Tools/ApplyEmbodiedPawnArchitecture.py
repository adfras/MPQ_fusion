import json
import os
import traceback

import unreal


PROJECT_DIR = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
OUT_DIR = os.path.join(PROJECT_DIR, "Saved", "CodexAgent", "EmbodimentInspection")
OUT_PATH = os.path.join(OUT_DIR, "testingkit3_embodied_pawn_apply_result.json")

NATIVE_CLASS_PATH = "/Script/MediaPipeDriver.MediaPipeEmbodiedAvatarPawn"
BP_DIR = "/Game/MetaHumanRooms/Blueprints"
BP_NAME = "BP_MP_EmbodiedMannyPawn"
BP_ASSET_PATH = f"{BP_DIR}/{BP_NAME}"
BP_OBJECT_PATH = f"{BP_ASSET_PATH}.{BP_NAME}"
BP_CLASS_PATH = f"{BP_ASSET_PATH}.{BP_NAME}_C"
MAP_PATH = "/Game/MetaHumanRooms/L_MetaHumanPreviewRoom_02"
PAWN_LABEL = "MP_PlacedEmbodiedMannyPawn"
PLACED_PAWN_TAG = "TestingKit3_PlacedEmbodiedAvatarPawn"
EMBODIED_START_TAG = "TestingKit3_AutoQuestEmbodiedStart"


def log(msg):
    unreal.log(f"[ApplyEmbodiedPawnArchitecture] {msg}")


def ensure_out_dir():
    os.makedirs(OUT_DIR, exist_ok=True)


def write_result(result):
    ensure_out_dir()
    with open(OUT_PATH, "w", encoding="utf-8") as f:
        json.dump(result, f, indent=2, sort_keys=True)
    log(f"wrote {OUT_PATH}")


def as_name(value):
    return unreal.Name(value)


def get_prop(obj, prop_names, default=None):
    for prop_name in prop_names:
        try:
            return obj.get_editor_property(prop_name)
        except Exception:
            pass
    return default


def set_prop(obj, prop_names, value):
    last_error = None
    for prop_name in prop_names:
        try:
            obj.set_editor_property(prop_name, value)
            return prop_name
        except Exception as exc:
            last_error = exc
    raise RuntimeError(f"failed to set any of {prop_names}: {last_error}")


def class_is_child_of(candidate, parent):
    if not candidate or not parent:
        return False
    candidate_path = candidate.get_path_name()
    if candidate_path == BP_CLASS_PATH or candidate_path.endswith(f"{BP_NAME}_C"):
        return True
    try:
        return bool(candidate.is_child_of(parent))
    except Exception:
        return candidate == parent


def ensure_blueprint(native_class):
    asset = unreal.EditorAssetLibrary.load_asset(BP_OBJECT_PATH)
    created = False
    if not asset:
        factory = unreal.BlueprintFactory()
        factory.set_editor_property("parent_class", native_class)
        asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
        asset = asset_tools.create_asset(BP_NAME, BP_DIR, unreal.Blueprint, factory)
        created = True
        if not asset:
            raise RuntimeError(f"failed to create {BP_ASSET_PATH}")

    parent_class = get_prop(asset, ["parent_class"], None)
    if parent_class and not class_is_child_of(parent_class, native_class) and parent_class != native_class:
        raise RuntimeError(f"{BP_ASSET_PATH} has unexpected parent {parent_class.get_name()}")

    compile_status = "not_available"
    blueprint_editor_library = getattr(unreal, "BlueprintEditorLibrary", None)
    compile_func = getattr(blueprint_editor_library, "compile_blueprint", None) if blueprint_editor_library else None
    if compile_func:
        compile_func(asset)
        compile_status = "BlueprintEditorLibrary.compile_blueprint"
    else:
        log("Blueprint compile API is not exposed in this Python environment; saving native-subclass Blueprint and verifying generated class load.")

    if not unreal.EditorAssetLibrary.save_asset(BP_ASSET_PATH, only_if_is_dirty=False):
        raise RuntimeError(f"failed to save {BP_ASSET_PATH}")

    generated_class = unreal.load_class(None, BP_CLASS_PATH)
    if not generated_class:
        raise RuntimeError(f"failed to load generated class {BP_CLASS_PATH}")

    return asset, generated_class, created, compile_status


def has_tag(actor, tag):
    try:
        return as_name(tag) in list(actor.get_editor_property("tags"))
    except Exception:
        return False


def find_existing_pawn(native_class):
    for actor in unreal.EditorLevelLibrary.get_all_level_actors():
        if not actor:
            continue
        if actor.get_actor_label() == PAWN_LABEL:
            return actor
        if has_tag(actor, PLACED_PAWN_TAG):
            return actor
        if class_is_child_of(actor.get_class(), native_class):
            return actor
    return None


def ensure_tags(actor):
    tags = list(get_prop(actor, ["tags"], []))
    for tag in (PLACED_PAWN_TAG, EMBODIED_START_TAG):
        name = as_name(tag)
        if name not in tags:
            tags.append(name)
    set_prop(actor, ["tags"], tags)


def configure_actor(actor):
    actor.set_actor_label(PAWN_LABEL, mark_dirty=True)
    actor.set_actor_location(unreal.Vector(0.0, -170.0, 0.0), False, False)
    actor.set_actor_rotation(unreal.Rotator(0.0, 0.0, 90.0), False)
    set_prop(actor, ["auto_possess_player"], unreal.AutoReceiveInput.PLAYER0)
    set_prop(actor, ["fallback_eye_height_cm", "FallbackEyeHeightCm"], 162.0)
    set_prop(actor, ["fallback_camera_forward_offset_cm", "FallbackCameraForwardOffsetCm"], 0.0)
    set_prop(actor, ["start_tracking_on_begin_play", "bStartTrackingOnBeginPlay"], True)
    set_prop(actor, ["use_media_pipe_tracking", "bUseMediaPipeTracking"], False)
    set_prop(actor, ["drive_movement_replica_pose", "bDriveMovementReplicaPose"], True)
    ensure_tags(actor)
    try:
        actor.rerun_construction_scripts()
    except Exception:
        pass


def inspect_actor(actor):
    camera = actor.get_component_by_class(unreal.CameraComponent)
    vr_origin = None
    for component in actor.get_components_by_class(unreal.SceneComponent):
        if component and component.get_name() == "VROrigin":
            vr_origin = component
            break
    root = actor.get_editor_property("root_component")
    result = {
        "label": actor.get_actor_label(),
        "class": actor.get_class().get_path_name(),
        "location": [actor.get_actor_location().x, actor.get_actor_location().y, actor.get_actor_location().z],
        "rotation": [
            actor.get_actor_rotation().pitch,
            actor.get_actor_rotation().yaw,
            actor.get_actor_rotation().roll,
        ],
        "auto_possess_player": str(get_prop(actor, ["auto_possess_player"], None)),
        "tags": [str(tag) for tag in list(get_prop(actor, ["tags"], []))],
        "root_component": root.get_name() if root else None,
        "vr_origin_component": vr_origin.get_name() if vr_origin else None,
        "vr_origin_attach_parent": vr_origin.get_attach_parent().get_name() if vr_origin and vr_origin.get_attach_parent() else None,
        "vr_origin_relative_location": None,
        "camera_component": camera.get_name() if camera else None,
        "camera_attach_parent": camera.get_attach_parent().get_name() if camera and camera.get_attach_parent() else None,
        "camera_relative_location": None,
        "camera_lock_to_hmd": None,
    }
    if vr_origin:
        loc = get_prop(vr_origin, ["relative_location"], None)
        if loc is None:
            try:
                loc = vr_origin.get_relative_transform().translation
            except Exception:
                loc = None
        if loc is None:
            raise RuntimeError("VROrigin relative location could not be read")
        result["vr_origin_relative_location"] = [loc.x, loc.y, loc.z]
    if camera:
        loc = get_prop(camera, ["relative_location"], None)
        if loc is None:
            try:
                loc = camera.get_relative_transform().translation
            except Exception:
                loc = None
        if loc is None:
            raise RuntimeError("camera relative location could not be read")
        result["camera_relative_location"] = [loc.x, loc.y, loc.z]
        result["camera_lock_to_hmd"] = bool(get_prop(camera, ["lock_to_hmd", "b_lock_to_hmd", "bLockToHmd"], None))
    return result


def main():
    result = {
        "success": False,
        "native_class": NATIVE_CLASS_PATH,
        "blueprint_asset": BP_ASSET_PATH,
        "map": MAP_PATH,
        "errors": [],
    }

    native_class = unreal.load_class(None, NATIVE_CLASS_PATH)
    if not native_class:
        raise RuntimeError(f"failed to load native class {NATIVE_CLASS_PATH}")
    result["native_class_loaded"] = True

    asset, generated_class, created_bp, compile_status = ensure_blueprint(native_class)
    result["blueprint_created"] = created_bp
    result["blueprint_compile_status"] = compile_status
    result["blueprint_class"] = generated_class.get_path_name()

    if not unreal.EditorLevelLibrary.load_level(MAP_PATH):
        raise RuntimeError(f"failed to load map {MAP_PATH}")

    actor = find_existing_pawn(native_class)
    result["actor_existed"] = actor is not None
    if actor and not class_is_child_of(actor.get_class(), native_class):
        raise RuntimeError(f"existing actor {actor.get_actor_label()} is not an embodied avatar pawn")

    if not actor:
        actor = unreal.EditorLevelLibrary.spawn_actor_from_class(
            generated_class,
            unreal.Vector(0.0, -170.0, 0.0),
            unreal.Rotator(0.0, 0.0, 90.0),
        )
        if not actor:
            raise RuntimeError("failed to spawn placed embodied pawn")

    configure_actor(actor)
    actor_details = inspect_actor(actor)
    result["placed_actor"] = actor_details

    if not actor_details["camera_component"]:
        raise RuntimeError("placed pawn has no CameraComponent")
    if actor_details["vr_origin_component"] != "VROrigin":
        raise RuntimeError("placed pawn has no VROrigin component")
    if actor_details["vr_origin_attach_parent"] != "AvatarRoot":
        raise RuntimeError(f"VROrigin is attached to {actor_details['vr_origin_attach_parent']}, expected AvatarRoot")
    if not str(actor_details["camera_attach_parent"]).startswith("VROrigin"):
        raise RuntimeError(f"camera is attached to {actor_details['camera_attach_parent']}, expected VROrigin")
    if actor_details["auto_possess_player"].find("PLAYER0") < 0:
        raise RuntimeError(f"auto possess is {actor_details['auto_possess_player']}, expected PLAYER0")
    if PLACED_PAWN_TAG not in actor_details["tags"] or EMBODIED_START_TAG not in actor_details["tags"]:
        raise RuntimeError(f"placed pawn tags are incomplete: {actor_details['tags']}")

    if not unreal.EditorLevelLibrary.save_current_level():
        raise RuntimeError(f"failed to save current level {MAP_PATH}")

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
    unreal.log_error(f"[ApplyEmbodiedPawnArchitecture] {exc}")
    raise
