import json
import os
import sys

import unreal


KEYWORDS = (
    "camera",
    "hmd",
    "xr",
    "tracking",
    "origin",
    "head",
    "neck",
    "chest",
    "pelvis",
    "root",
    "owner",
    "visible",
    "hidden",
    "hide bone",
    "mirror",
    "possess",
    "spawn",
    "body",
    "hand",
    "retarget",
    "oculus",
    "openxr",
    "quest",
    "vr",
)


DEFAULT_ASSETS = [
    "/Game/Blueprints/BP_MovementSampleGameMode",
    "/Game/Blueprints/Avatars/BP_OculusPawn",
    "/Game/Pawns/OwenActorComponent/BP_OwenOculusPawn",
    "/Game/Pawns/OwenAnimationNodes/BP_OwenFullTracking",
    "/Game/Pawns/OwenAnimationNodes/ABP_OwenFullTracking",
    "/Game/Pawns/UnrealManequin/BP_ManequinRetarget",
    "/Game/Pawns/UnrealManequin/AB_Mannequin",
    "/Game/Pawns/Aura/BP_AuraOculusPawn",
    "/Game/Mirror/BP_Mirror",
    "/Game/ThirdPerson/Blueprints/BP_ThirdPersonGameMode",
    "/Game/ThirdPerson/Blueprints/BP_ThirdPersonCharacter",
    "/Game/MetaHumanRooms/Blueprints/BP_MetaHumanPreviewRoomGameMode",
    "/Game/MetaHumanRooms/Blueprints/BP_MetaHumanPreviewRoomPlayer",
    "/Game/MetaHumanRooms/Blueprints/BP_MetaHumanPreviewRoomAutoQuestStartup",
    "/Game/Codex/Mirror/BP_VRSelfMirror",
]

DEFAULT_MAPS = [
    "/Game/Maps/MAP_HighFidelity_AnimBlueprint",
    "/Game/Maps/MAP_RetargetMannequinAnimBlueprint",
    "/Game/ThirdPerson/Lvl_ThirdPerson",
    "/Game/MetaHumanRooms/L_MetaHumanPreviewRoom_02",
    "/Game/MetaHumanRooms/L_MetaHumanPreviewRoom",
]


def safe_str(value):
    try:
        return str(value)
    except Exception as exc:
        return "<error:%s>" % exc


def read_prop(obj, name):
    try:
        value = obj.get_editor_property(name)
        if hasattr(value, "get_path_name"):
            return value.get_path_name()
        if hasattr(value, "get_name"):
            return value.get_name()
        return safe_str(value)
    except Exception:
        return None


def object_path(obj):
    if not obj:
        return None
    try:
        return obj.get_path_name()
    except Exception:
        return safe_str(obj)


def class_path(cls):
    if not cls:
        return None
    try:
        return cls.get_path_name()
    except Exception:
        return safe_str(cls)


def rel_transform(component):
    out = {}
    for prop in ("relative_location", "relative_rotation", "relative_scale3d"):
        value = read_prop(component, prop)
        if value is not None:
            out[prop] = value
    return out


def component_info(component):
    info = {
        "name": component.get_name(),
        "class": component.get_class().get_path_name(),
    }
    parent = None
    if isinstance(component, unreal.SceneComponent):
        try:
            parent = component.get_attach_parent()
        except Exception:
            parent = None
        info["attach_parent"] = parent.get_name() if parent else None
        info.update(rel_transform(component))
    for prop in (
        "visible",
        "hidden_in_game",
        "owner_no_see",
        "only_owner_see",
        "cast_hidden_shadow",
        "absolute_location",
        "absolute_rotation",
        "absolute_scale",
        "use_pawn_control_rotation",
        "lock_to_hmd",
        "auto_activate",
        "component_tags",
    ):
        value = read_prop(component, prop)
        if value is not None:
            info[prop] = value
    if isinstance(component, unreal.SkeletalMeshComponent):
        info["skeletal_mesh"] = object_path(read_asset_prop(component, "skeletal_mesh"))
        info["anim_class"] = class_path(read_asset_prop(component, "anim_class"))
        info["post_process_anim_blueprint"] = class_path(read_asset_prop(component, "post_process_anim_blueprint"))
        info["animation_mode"] = read_prop(component, "animation_mode")
    if isinstance(component, unreal.CameraComponent):
        for prop in ("field_of_view", "use_pawn_control_rotation", "lock_to_hmd"):
            value = read_prop(component, prop)
            if value is not None:
                info[prop] = value
    return info


def read_asset_prop(obj, name):
    try:
        return obj.get_editor_property(name)
    except Exception:
        return None


def generated_class(asset):
    try:
        cls = asset.get_editor_property("generated_class")
        if cls:
            return cls
    except Exception:
        pass
    try:
        return asset.generated_class()
    except Exception:
        return None


def cdo_for_asset(asset):
    cls = generated_class(asset)
    if not cls:
        return None
    try:
        return unreal.get_default_object(cls)
    except Exception:
        return None


def actor_component_list(cdo):
    components = []
    if not cdo:
        return components
    try:
        actor_components = cdo.get_components_by_class(unreal.ActorComponent)
    except Exception:
        actor_components = []
    seen = set()
    for component in actor_components:
        if not component:
            continue
        key = component.get_path_name()
        if key in seen:
            continue
        seen.add(key)
        components.append(component_info(component))
    return components


def class_defaults(cdo):
    if not cdo:
        return {}
    out = {}
    for prop in (
        "default_pawn_class",
        "player_controller_class",
        "hud_class",
        "game_state_class",
        "player_state_class",
        "auto_possess_player",
        "auto_receive_input",
        "use_controller_rotation_yaw",
        "use_controller_rotation_pitch",
        "use_controller_rotation_roll",
        "base_eye_height",
        "capsule_component",
        "mesh",
        "camera_component",
    ):
        value = read_asset_prop(cdo, prop)
        if value is not None:
            if hasattr(value, "get_path_name"):
                out[prop] = value.get_path_name()
            elif hasattr(value, "get_name"):
                out[prop] = value.get_name()
            else:
                out[prop] = safe_str(value)
    return out


def graph_matches(asset):
    matches = []
    try:
        graphs = list(asset.get_editor_property("ubergraph_pages"))
    except Exception:
        graphs = []
    try:
        graphs += list(asset.get_editor_property("function_graphs"))
    except Exception:
        pass
    try:
        graphs += list(asset.get_editor_property("macro_graphs"))
    except Exception:
        pass
    try:
        graphs += list(asset.get_editor_property("intermediate_generated_graphs"))
    except Exception:
        pass
    for graph in graphs:
        try:
            nodes = graph.get_editor_property("nodes")
        except Exception:
            nodes = []
        for node in nodes:
            try:
                title = node.get_node_title(unreal.NodeTitleType.FULL_TITLE).to_string()
            except Exception:
                title = node.get_name()
            text = (title + " " + node.get_class().get_name()).lower()
            if any(keyword in text for keyword in KEYWORDS):
                matches.append(
                    {
                        "graph": graph.get_name(),
                        "node": node.get_name(),
                        "title": title,
                        "class": node.get_class().get_path_name(),
                    }
                )
    return matches[:250]


def inspect_asset(path):
    if not unreal.EditorAssetLibrary.does_asset_exist(path):
        return {"path": path, "exists": False}
    asset = unreal.EditorAssetLibrary.load_asset(path)
    if not asset:
        return {"path": path, "exists": True, "loaded": False}
    cdo = cdo_for_asset(asset)
    result = {
        "path": path,
        "exists": True,
        "loaded": True,
        "asset_class": asset.get_class().get_path_name(),
        "generated_class": class_path(generated_class(asset)),
        "class_defaults": class_defaults(cdo),
        "components": actor_component_list(cdo),
        "graph_matches": graph_matches(asset),
    }
    return result


def inspect_current_level():
    actors = []
    try:
        all_actors = unreal.EditorLevelLibrary.get_all_level_actors()
    except Exception:
        return actors
    for actor in all_actors:
        try:
            cls_path = actor.get_class().get_path_name()
            label = actor.get_actor_label()
            loc = actor.get_actor_location()
            rot = actor.get_actor_rotation()
        except Exception:
            continue
        actor_text = (label + " " + cls_path).lower()
        interesting = any(
            key in actor_text
            for key in (
                "pawn",
                "player",
                "start",
                "mirror",
                "manny",
                "owen",
                "oculus",
                "meta",
                "camera",
                "media",
                "quest",
                "auto",
            )
        )
        if not interesting:
            continue
        actors.append(
            {
                "label": label,
                "name": actor.get_name(),
                "class": cls_path,
                "location": safe_str(loc),
                "rotation": safe_str(rot),
                "tags": [safe_str(tag) for tag in actor.tags],
                "components": actor_component_list(actor),
            }
        )
    return actors


def inspect_map(path):
    if not unreal.EditorAssetLibrary.does_asset_exist(path):
        return {"path": path, "exists": False}
    result = {"path": path, "exists": True, "actors": []}
    try:
        unreal.EditorLoadingAndSavingUtils.load_map(path)
        result["loaded"] = True
        result["actors"] = inspect_current_level()
    except Exception as exc:
        result["loaded"] = False
        result["error"] = safe_str(exc)
    return result


def load_ini(path):
    if not os.path.exists(path):
        return []
    wanted = []
    keys = (
        "GameDefaultMap",
        "EditorStartupMap",
        "GlobalDefaultGameMode",
        "bStartInVR",
        "HandTrackingSupport",
        "bBodyTrackingEnabled",
        "BodyTrackingFidelity",
        "BodyTrackingJointSet",
        "bEyeTrackingEnabled",
        "bFaceTrackingEnabled",
        "XrApi",
        "Tracking",
        "OpenXR",
        "OculusXR",
        "ConsoleVariables",
    )
    with open(path, "r", encoding="utf-8", errors="replace") as handle:
        for index, line in enumerate(handle, 1):
            if any(key in line for key in keys):
                wanted.append("%d:%s" % (index, line.rstrip()))
    return wanted


def main():
    project_dir = os.getcwd()
    assets = os.environ.get("CODEX_EMBODIMENT_ASSETS")
    maps = os.environ.get("CODEX_EMBODIMENT_MAPS")
    asset_paths = [item.strip() for item in assets.split(";") if item.strip()] if assets else DEFAULT_ASSETS
    map_paths = [item.strip() for item in maps.split(";") if item.strip()] if maps else DEFAULT_MAPS
    out = {
        "project_dir": project_dir,
        "project_name": os.path.basename(project_dir),
        "engine_version": safe_str(unreal.SystemLibrary.get_engine_version()),
        "assets": [inspect_asset(path) for path in asset_paths],
        "maps": [inspect_map(path) for path in map_paths],
        "config_hits": {
            "DefaultEngine.ini": load_ini(os.path.join(project_dir, "Config", "DefaultEngine.ini")),
            "DefaultGame.ini": load_ini(os.path.join(project_dir, "Config", "DefaultGame.ini")),
            "DefaultInput.ini": load_ini(os.path.join(project_dir, "Config", "DefaultInput.ini")),
        },
    }
    output = os.environ.get("CODEX_EMBODIMENT_OUT")
    if not output:
        output = os.path.join(project_dir, "Saved", "CodexAgent", "embodiment_architecture_inspection.json")
    os.makedirs(os.path.dirname(output), exist_ok=True)
    with open(output, "w", encoding="utf-8") as handle:
        json.dump(out, handle, indent=2, sort_keys=True)
    print("CODEX_EMBODIMENT_INSPECTION_OUT=%s" % output)


if __name__ == "__main__":
    main()
