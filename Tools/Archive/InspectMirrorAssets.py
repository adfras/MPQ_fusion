import json
import sys

import unreal


def _name(obj):
    return obj.get_name() if obj else None


def _path(obj):
    return obj.get_path_name() if obj else None


def _vec(value):
    return [float(value.x), float(value.y), float(value.z)]


def _rot(value):
    return [float(value.roll), float(value.pitch), float(value.yaw)]


def _component_record(component):
    record = {
        "name": _name(component),
        "class": component.get_class().get_name() if component else None,
    }
    for prop in ("relative_location", "relative_rotation", "relative_scale3d", "mobility", "visible"):
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
        ):
            try:
                value = component.get_editor_property(prop)
            except Exception:
                continue
            record[prop] = _path(value) if isinstance(value, unreal.Object) else str(value)

        try:
            hidden = component.get_editor_property("hidden_actors")
            record["hidden_actors_count"] = len(hidden)
        except Exception:
            pass
        try:
            show_only = component.get_editor_property("show_only_actors")
            record["show_only_actors_count"] = len(show_only)
        except Exception:
            pass

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


def inspect_blueprint(asset_path):
    asset = unreal.EditorAssetLibrary.load_asset(asset_path)
    result = {
        "asset_path": asset_path,
        "loaded": bool(asset),
        "class": asset.get_class().get_name() if asset else None,
        "components": [],
        "spawned_components": [],
    }
    if not asset:
        return result

    try:
        scs = asset.get_editor_property("simple_construction_script")
    except Exception:
        scs = None
    if scs:
        try:
            nodes = scs.get_all_nodes()
        except Exception:
            nodes = []
        for node in nodes:
            try:
                component = node.get_editor_property("component_template")
            except Exception:
                component = None
            if not component:
                continue
            record = _component_record(component)
            record["source"] = "simple_construction_script"
            for prop in ("variable_name", "parent_component_or_variable_name"):
                try:
                    record[prop] = str(node.get_editor_property(prop))
                except Exception:
                    pass
            result["components"].append(record)

    generated_class = asset.generated_class() if hasattr(asset, "generated_class") else None
    cdo = unreal.get_default_object(generated_class) if generated_class else None
    if not cdo:
        return result

    result["generated_class"] = generated_class.get_path_name()
    components = cdo.get_components_by_class(unreal.ActorComponent)
    for component in components:
        record = _component_record(component)
        record["source"] = "class_default_object"
        result["components"].append(record)

    try:
        spawned = unreal.EditorLevelLibrary.spawn_actor_from_class(
            generated_class,
            unreal.Vector(100000.0, 100000.0, 100000.0),
            unreal.Rotator(0.0, 0.0, 0.0),
        )
    except Exception as exc:
        result["spawn_error"] = str(exc)
        spawned = None

    if spawned:
        try:
            spawned_components = spawned.get_components_by_class(unreal.ActorComponent)
            for component in spawned_components:
                record = _component_record(component)
                record["source"] = "spawned_actor"
                result["spawned_components"].append(record)
        finally:
            try:
                unreal.EditorLevelLibrary.destroy_actor(spawned)
            except Exception:
                pass
    return result


def inspect_render_target(asset_path):
    asset = unreal.EditorAssetLibrary.load_asset(asset_path)
    result = {
        "asset_path": asset_path,
        "loaded": bool(asset),
        "class": asset.get_class().get_name() if asset else None,
    }
    if not asset:
        return result
    for prop in ("size_x", "size_y", "render_target_format", "clear_color", "auto_generate_mips"):
        try:
            result[prop] = str(asset.get_editor_property(prop))
        except Exception:
            pass
    return result


def inspect_material(asset_path):
    asset = unreal.EditorAssetLibrary.load_asset(asset_path)
    result = {
        "asset_path": asset_path,
        "loaded": bool(asset),
        "class": asset.get_class().get_name() if asset else None,
    }
    if not asset:
        return result
    for prop in ("parent", "base_property_overrides"):
        try:
            value = asset.get_editor_property(prop)
        except Exception:
            continue
        result[prop] = _path(value) if isinstance(value, unreal.Object) else str(value)
    return result


def main():
    output_path = sys.argv[1] if len(sys.argv) > 1 else None
    payload = {
        "blueprints": [inspect_blueprint(path) for path in sys.argv[2].split(";") if path] if len(sys.argv) > 2 else [],
        "render_targets": [inspect_render_target(path) for path in sys.argv[3].split(";") if path] if len(sys.argv) > 3 else [],
        "materials": [inspect_material(path) for path in sys.argv[4].split(";") if path] if len(sys.argv) > 4 else [],
    }
    text = json.dumps(payload, indent=2)
    if output_path:
        with open(output_path, "w", encoding="utf-8") as handle:
            handle.write(text)
    print(text)


if __name__ == "__main__":
    main()
