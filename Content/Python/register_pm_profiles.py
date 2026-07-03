"""Create MediaPipe retarget-profile DataAssets for the proportion-matrix variants.

Run in-editor (py or MCP). Creates one UMediaPipeMetaHumanRetargetProfile per
PM_* build under /Game/MetaHumans/_ProportionMatrix/Profiles/, then logs the
CVar line that registers them for a session:

    mp.MetaHumanProfileAssetPaths <semicolon-joined asset paths>
    mp.MetaHumanActiveProfile PM_Tall   (etc.)

Validation gate (ValidateMediaPipeMetaHumanProfileDefinition) requires ALL of:
blueprint class, body mesh, face mesh, face post-process ABP class loadable,
12 required pose bones present, and both reference arm lengths derivable.
The PM builds ship no per-character face PP ABP, so we point the profile at
the class already assigned on the variant's face mesh asset, falling back to
Kellan's ABP class (validation only needs the class to load; runtime face
behavior comes from the mesh's own PP slot).

Eye anchor: DefaultEyeLocalOffset.Z is scaled from Kellan's 161.94 by the
variant height constraint over the default-profile height baseline (178 cm).
Good enough for profile validity; refine from headset evidence if a variant
is ever worn.
"""

import unreal

ROOT = "/Game/MetaHumans/_ProportionMatrix"
PROFILE_DIR = f"{ROOT}/Profiles"
KELLAN_FACE_ABP = (
    "/Game/MetaHumans/Kellan/Face/ABP_Kellan_FaceMesh_PostProcess."
    "ABP_Kellan_FaceMesh_PostProcess_C")
BASELINE_HEIGHT_CM = 178.0
BASELINE_EYE_Z = 161.94

# name -> height constraint used at generation time (drives eye-anchor scale)
VARIANTS = {
    "PM_Short": 155.0,
    "PM_Tall": 195.0,
    "PM_LongArms": 178.0,
    "PM_ShortLegs": 165.0,
}

REQUIRED_BONES = [
    "pelvis", "spine_01", "spine_02", "spine_03",
    "clavicle_l", "upperarm_l", "lowerarm_l", "hand_l",
    "clavicle_r", "upperarm_r", "lowerarm_r", "hand_r",
]


def _face_pp_abp_class_path(face_mesh_path: str) -> str:
    face_mesh = unreal.load_asset(face_mesh_path)
    if face_mesh:
        try:
            abp = face_mesh.get_editor_property("post_process_anim_blueprint")
            if abp:
                path = abp.get_path_name()
                if not path.endswith("_C"):
                    path += "_C"
                unreal.log(f"  face PP ABP from mesh: {path}")
                return path
        except Exception as error:
            unreal.log_warning(f"  face PP ABP query failed: {error}")
    unreal.log(f"  face PP ABP fallback: Kellan class")
    return KELLAN_FACE_ABP


def register() -> list[str]:
    asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
    created = []
    for name, height_cm in VARIANTS.items():
        asset_name = f"DA_{name}_Profile"
        asset_path = f"{PROFILE_DIR}/{asset_name}"
        build = f"{ROOT}/Builds/{name}"
        body_mesh = f"{build}/Body/SKM_{name}_BodyMesh.SKM_{name}_BodyMesh"
        face_mesh = f"{build}/Face/SKM_{name}_FaceMesh.SKM_{name}_FaceMesh"
        bp_class = f"{build}/BP_{name}.BP_{name}_C"

        unreal.log(f"{asset_name}:")
        if unreal.EditorAssetLibrary.does_asset_exist(asset_path):
            asset = unreal.load_asset(asset_path)
            unreal.log("  exists, updating in place")
        else:
            factory = unreal.new_object(type=unreal.DataAssetFactory)
            factory.set_editor_property(
                "data_asset_class", unreal.MediaPipeMetaHumanRetargetProfile)
            asset = asset_tools.create_asset(
                asset_name=asset_name,
                package_path=PROFILE_DIR,
                asset_class=unreal.MediaPipeMetaHumanRetargetProfile,
                factory=factory,
            )
        if not asset:
            unreal.log_error(f"  could not create {asset_path}")
            continue

        definition = unreal.MediaPipeMetaHumanProfileDefinition()
        definition.set_editor_property("profile_id", name)
        definition.set_editor_property("display_name", name)
        definition.set_editor_property(
            "target_blueprint_class", unreal.SoftClassPath(bp_class))
        definition.set_editor_property(
            "body_mesh", unreal.SoftObjectPath(body_mesh))
        definition.set_editor_property(
            "face_mesh", unreal.SoftObjectPath(face_mesh))
        definition.set_editor_property(
            "face_post_process_anim_blueprint_class",
            unreal.SoftClassPath(_face_pp_abp_class_path(face_mesh)))
        definition.set_editor_property(
            "face_forward_axis", unreal.MediaPipeMetaHumanForwardAxis.Y)
        definition.set_editor_property("embodied_yaw_offset_deg", -90.0)
        eye_z = BASELINE_EYE_Z * (height_cm / BASELINE_HEIGHT_CM)
        definition.set_editor_property(
            "default_eye_local_offset", unreal.Vector(0.0, 8.92, eye_z))
        definition.set_editor_property(
            "default_arm_source_mode",
            unreal.MediaPipeMetaHumanArmSourceMode.FULL_ARM_CHAIN)
        definition.set_editor_property(
            "required_pose_bones", [unreal.Name(b) for b in REQUIRED_BONES])

        asset.set_editor_property("profile", definition)
        unreal.EditorAssetLibrary.save_loaded_asset(asset)
        created.append(f"{asset_path}.{asset_name}")
        unreal.log(f"  saved {asset_path} (eyeZ={eye_z:.1f})")

    if created:
        unreal.log("PM-PROFILES registered assets:")
        unreal.log("mp.MetaHumanProfileAssetPaths " + ";".join(created))
    return created


if __name__ == "__main__":
    register()
