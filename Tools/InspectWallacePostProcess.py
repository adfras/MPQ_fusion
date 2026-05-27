import unreal


def describe_class(value):
    if not value:
        return "None"
    try:
        generated_by = value.get_editor_property("class_generated_by")
        if generated_by:
            return generated_by.get_path_name()
    except Exception:
        pass
    try:
        return value.get_path_name()
    except Exception:
        return str(value)


def describe_mesh_component(component):
    mesh = component.get_editor_property("skeletal_mesh_asset")
    if not mesh:
        try:
            mesh = component.get_editor_property("skeletal_mesh")
        except Exception:
            mesh = None
    mesh_path = mesh.get_path_name() if mesh else "None"

    post_process = "None"
    if mesh:
        try:
            post_process = describe_class(mesh.get_editor_property("post_process_anim_blueprint"))
        except Exception as exc:
            post_process = "ERROR: {}".format(exc)

    override_post_process = "None"
    try:
        override_post_process = describe_class(component.get_editor_property("override_post_process_anim_bp"))
    except Exception:
        pass

    disable_post_process = "unknown"
    try:
        disable_post_process = component.get_editor_property("disable_post_process_blueprint")
    except Exception:
        try:
            disable_post_process = component.get_disable_post_process_blueprint()
        except Exception:
            pass

    anim_class = "None"
    try:
        anim_class = describe_class(component.get_anim_class())
    except Exception:
        pass

    print(
        "WALLACE_COMPONENT name={} class={} mesh={} animClass={} meshPostProcess={} overridePostProcess={} disablePostProcess={}".format(
            component.get_name(),
            component.get_class().get_name(),
            mesh_path,
            anim_class,
            post_process,
            override_post_process,
            disable_post_process,
        )
    )


asset_paths = [
    "/Game/MetaHumans/Wallace/Body/m_med_unw_body.m_med_unw_body",
    "/Game/MetaHumans/Wallace/BP_Wallace.BP_Wallace",
    "/Game/MetaHumans/Wallace/BP_Wallace.BP_Wallace_C",
    "/Game/MetaHumans/Common/Male/Medium/UnderWeight/Body/m_med_unw_animbp_Cinematic.m_med_unw_animbp_Cinematic",
]

for asset_path in asset_paths:
    asset = unreal.load_object(None, asset_path)
    print("WALLACE_ASSET path={} class={}".format(asset_path, asset.get_class().get_name() if asset else "None"))
    if asset and isinstance(asset, unreal.SkeletalMesh):
        print("WALLACE_MESH path={} postProcess={}".format(asset_path, describe_class(asset.get_editor_property("post_process_anim_blueprint"))))
    if asset and isinstance(asset, unreal.Blueprint):
        print("WALLACE_BLUEPRINT path={} loaded=1".format(asset_path))
    if asset and isinstance(asset, unreal.BlueprintGeneratedClass):
        cdo = unreal.get_default_object(asset)
        components = cdo.get_components_by_class(unreal.SkeletalMeshComponent)
        print("WALLACE_BLUEPRINT_CLASS path={} skeletalComponents={}".format(asset_path, len(components)))
        for component in components:
            describe_mesh_component(component)
