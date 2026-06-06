import json
import sys

import unreal


def _path(obj):
    return obj.get_path_name() if obj else None


def _vec(value):
    return [float(value.x), float(value.y), float(value.z)]


def _rot(value):
    return [float(value.roll), float(value.pitch), float(value.yaw)]


def _component_record(component):
    record = {
        "name": component.get_name(),
        "class": component.get_class().get_name(),
    }
    for prop in ("relative_location", "relative_rotation", "relative_scale3d", "visible"):
        try:
            value = component.get_editor_property(prop)
        except Exception:
            continue
        if hasattr(value, "x") and hasattr(value, "y") and hasattr(value, "z"):
            record[prop] = _vec(value)
        elif hasattr(value, "roll") and hasattr(value, "pitch") and hasattr(value, "yaw"):
            record[prop] = _rot(value)
        else:
            record[prop] = str(value)

    if isinstance(component, unreal.SceneCaptureComponent2D):
        for prop in (
            "texture_target",
            "capture_source",
            "primitive_render_mode",
            "capture_every_frame",
            "capture_on_movement",
            "always_persist_rendering_state",
            "max_view_distance_override",
            "fov_angle",
            "projection_type",
            "ortho_width",
        ):
            try:
                value = component.get_editor_property(prop)
            except Exception:
                continue
            record[prop] = _path(value) if isinstance(value, unreal.Object) else str(value)

    if isinstance(component, unreal.StaticMeshComponent):
        for prop in ("static_mesh", "override_materials", "cast_shadow", "collision_enabled"):
            try:
                value = component.get_editor_property(prop)
            except Exception:
                continue
            if isinstance(value, unreal.Object):
                record[prop] = _path(value)
            elif isinstance(value, (list, tuple)):
                record[prop] = [_path(item) if isinstance(item, unreal.Object) else str(item) for item in value]
            else:
                record[prop] = str(value)

    return record


def _actor_record(actor):
    return {
        "label": actor.get_actor_label(),
        "name": actor.get_name(),
        "class": actor.get_class().get_path_name(),
        "location": _vec(actor.get_actor_location()),
        "rotation": _rot(actor.get_actor_rotation()),
        "scale": _vec(actor.get_actor_scale3d()),
        "components": [_component_record(component) for component in actor.get_components_by_class(unreal.ActorComponent)],
    }


def main():
    output_path = sys.argv[1] if len(sys.argv) > 1 else None
    map_path = sys.argv[2] if len(sys.argv) > 2 else ""
    if map_path:
        unreal.EditorLevelLibrary.load_level(map_path)

    actors = unreal.EditorLevelLibrary.get_all_level_actors()
    mirror_actors = []
    for actor in actors:
        text = " ".join(
            [
                actor.get_actor_label(),
                actor.get_name(),
                actor.get_class().get_name(),
                actor.get_class().get_path_name(),
            ]
        ).lower()
        if "mirror" in text:
            mirror_actors.append(_actor_record(actor))

    text = json.dumps({"map_path": map_path, "mirror_actors": mirror_actors}, indent=2)
    if output_path:
        with open(output_path, "w", encoding="utf-8") as handle:
            handle.write(text)
    print(text)


if __name__ == "__main__":
    main()
