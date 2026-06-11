import json
import sys

import unreal


def _path(obj):
    return obj.get_path_name() if obj else None


def _vec(value):
    return [round(float(value.x), 3), round(float(value.y), 3), round(float(value.z), 3)]


def _rot(value):
    return [round(float(value.roll), 3), round(float(value.pitch), 3), round(float(value.yaw), 3)]


def _prop(obj, name):
    try:
        return obj.get_editor_property(name)
    except Exception:
        return None


def _component_record(component):
    record = {
        "name": component.get_name(),
        "class": component.get_class().get_name(),
    }
    if isinstance(component, unreal.SceneComponent):
        record["relative_location"] = _vec(_prop(component, "relative_location"))
        record["relative_rotation"] = _rot(_prop(component, "relative_rotation"))
        record["relative_scale3d"] = _vec(_prop(component, "relative_scale3d"))
        parent = component.get_attach_parent()
        record["attach_parent"] = parent.get_name() if parent else None
    if isinstance(component, unreal.SkeletalMeshComponent):
        mesh = _prop(component, "skinned_asset") or _prop(component, "skeletal_mesh")
        record["mesh"] = _path(mesh)
        record["owner_no_see"] = str(_prop(component, "owner_no_see"))
        record["only_owner_see"] = str(_prop(component, "only_owner_see"))
    if component.get_class().get_name() in ("MotionControllerComponent", "CameraComponent"):
        for prop_name in ("motion_source", "tracking_source", "lock_to_hmd"):
            value = _prop(component, prop_name)
            if value is not None:
                record[prop_name] = str(value)
    return record


def _actor_record(actor):
    components = []
    for component in actor.get_components_by_class(unreal.ActorComponent):
        name_text = " ".join(
            [
                component.get_name(),
                component.get_class().get_name(),
            ]
        ).lower()
        if any(token in name_text for token in ("mesh", "camera", "motion", "root", "skeleton", "body", "retarget")):
            components.append(_component_record(component))
    return {
        "label": actor.get_actor_label(),
        "name": actor.get_name(),
        "class": actor.get_class().get_path_name(),
        "location": _vec(actor.get_actor_location()),
        "rotation": _rot(actor.get_actor_rotation()),
        "scale": _vec(actor.get_actor_scale3d()),
        "components": components,
    }


def main():
    output_path = sys.argv[1] if len(sys.argv) > 1 else None
    map_paths = sys.argv[2:] if len(sys.argv) > 2 else []
    payload = {}

    for map_path in map_paths:
        try:
            unreal.EditorLevelLibrary.load_level(map_path)
        except Exception as exc:
            payload[map_path] = {"load_error": str(exc)}
            continue

        records = []
        for actor in unreal.EditorLevelLibrary.get_all_level_actors():
            text = " ".join(
                [
                    actor.get_actor_label(),
                    actor.get_name(),
                    actor.get_class().get_name(),
                    actor.get_class().get_path_name(),
                ]
            ).lower()
            if any(token in text for token in ("avatar", "pawn", "mirror", "camera", "playerstart", "owen", "manequin", "mannequin")):
                records.append(_actor_record(actor))
        payload[map_path] = records

    text = json.dumps(payload, indent=2)
    if output_path:
        with open(output_path, "w", encoding="utf-8") as handle:
            handle.write(text)
    print(text)


if __name__ == "__main__":
    main()
