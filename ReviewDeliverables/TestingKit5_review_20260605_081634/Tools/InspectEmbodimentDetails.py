import json
import os

import unreal


ASSETS = [item for item in os.environ.get("CODEX_DETAIL_ASSETS", "").split(";") if item]
MAPS = [item for item in os.environ.get("CODEX_DETAIL_MAPS", "").split(";") if item]


def safe(value):
    try:
        return str(value)
    except Exception as exc:
        return "<error:%s>" % exc


def obj_path(value):
    if not value:
        return None
    try:
        return value.get_path_name()
    except Exception:
        return safe(value)


def get_prop(obj, name):
    try:
        value = obj.get_editor_property(name)
    except Exception:
        return None
    if hasattr(value, "get_path_name"):
        return value.get_path_name()
    return safe(value)


def generated_class(asset):
    try:
        return asset.get_editor_property("generated_class")
    except Exception:
        return None


def class_chain(cls):
    out = []
    while cls:
        out.append(obj_path(cls))
        try:
            cls = cls.get_super_class()
        except Exception:
            break
    return out


def component_details(component):
    info = {
        "name": component.get_name(),
        "class": obj_path(component.get_class()),
    }
    if isinstance(component, unreal.SceneComponent):
        try:
            parent = component.get_attach_parent()
        except Exception:
            parent = None
        info["parent"] = parent.get_name() if parent else None
        for prop in ("relative_location", "relative_rotation", "relative_scale3d", "absolute_location", "absolute_rotation", "absolute_scale"):
            info[prop] = get_prop(component, prop)
    for prop in (
        "visible",
        "hidden_in_game",
        "owner_no_see",
        "only_owner_see",
        "cast_hidden_shadow",
        "use_attach_parent_bound",
        "auto_activate",
        "component_tags",
        "motion_source",
        "tracking_source",
        "hand",
        "display_model_source",
        "disable_low_latency_update",
        "use_pawn_control_rotation",
        "lock_to_hmd",
        "field_of_view",
    ):
        value = get_prop(component, prop)
        if value is not None:
            info[prop] = value
    if isinstance(component, unreal.SkeletalMeshComponent):
        for prop in ("skeletal_mesh", "anim_class", "post_process_anim_blueprint", "animation_mode", "leader_pose_component"):
            value = get_prop(component, prop)
            if value is not None:
                info[prop] = value
    return info


def actor_details(actor):
    details = {
        "label": actor.get_actor_label(),
        "name": actor.get_name(),
        "class": obj_path(actor.get_class()),
        "class_chain": class_chain(actor.get_class()),
        "location": safe(actor.get_actor_location()),
        "rotation": safe(actor.get_actor_rotation()),
        "tags": [safe(tag) for tag in actor.tags],
    }
    for prop in (
        "auto_receive_input",
        "auto_possess_player",
        "auto_possess_ai",
        "input_priority",
        "block_input",
        "spawn_collision_handling_method",
        "net_load_on_client",
        "hidden",
    ):
        value = get_prop(actor, prop)
        if value is not None:
            details[prop] = value
    try:
        components = actor.get_components_by_class(unreal.ActorComponent)
    except Exception:
        components = []
    details["components"] = [component_details(component) for component in components if component]
    return details


def asset_details(path):
    result = {"path": path, "exists": unreal.EditorAssetLibrary.does_asset_exist(path)}
    if not result["exists"]:
        return result
    asset = unreal.EditorAssetLibrary.load_asset(path)
    result["asset_class"] = obj_path(asset.get_class())
    cls = generated_class(asset)
    result["generated_class"] = obj_path(cls)
    result["class_chain"] = class_chain(cls)
    try:
        cdo = unreal.get_default_object(cls) if cls else None
    except Exception:
        cdo = None
    if cdo:
        for prop in (
            "default_pawn_class",
            "player_controller_class",
            "auto_receive_input",
            "auto_possess_player",
            "base_eye_height",
            "use_controller_rotation_yaw",
            "use_controller_rotation_pitch",
            "use_controller_rotation_roll",
        ):
            value = get_prop(cdo, prop)
            if value is not None:
                result[prop] = value
        try:
            result["cdo_components"] = [component_details(c) for c in cdo.get_components_by_class(unreal.ActorComponent) if c]
        except Exception:
            result["cdo_components"] = []
    return result


def map_details(path):
    result = {"path": path, "exists": unreal.EditorAssetLibrary.does_asset_exist(path), "actors": []}
    if not result["exists"]:
        return result
    unreal.EditorLoadingAndSavingUtils.load_map(path)
    for actor in unreal.EditorLevelLibrary.get_all_level_actors():
        label_class = (actor.get_actor_label() + " " + obj_path(actor.get_class())).lower()
        if any(key in label_class for key in ("pawn", "player", "start", "mirror", "manny", "owen", "camera", "quest", "meta", "oculus", "media")):
            result["actors"].append(actor_details(actor))
    return result


def main():
    output = os.environ.get("CODEX_DETAIL_OUT")
    if not output:
        output = os.path.join(os.getcwd(), "embodiment_details.json")
    out = {
        "assets": [asset_details(path) for path in ASSETS],
        "maps": [map_details(path) for path in MAPS],
    }
    os.makedirs(os.path.dirname(output), exist_ok=True)
    with open(output, "w", encoding="utf-8") as handle:
        json.dump(out, handle, indent=2, sort_keys=True)
    print("CODEX_DETAIL_OUT=%s" % output)


if __name__ == "__main__":
    main()
