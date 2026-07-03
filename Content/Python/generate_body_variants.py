"""Generate MetaHuman body-shape variants for the proportion-robustness matrix.

SCAFFOLD — run inside the editor (Python console, `py generate_body_variants.py`,
or the MCP ProgrammaticToolset). See Docs/PROPORTION_ROBUSTNESS_MATRIX.md for
the full workflow this feeds.

Appearance is irrelevant for this matrix — only skeleton proportions matter to
the solvers — so variants are created from the blank MetaHumanCharacter
template and shaped purely through body constraints.

Requires the MetaHumanCharacter (MetaHuman Creator) plugin, enabled in
TestingKit5.uproject.
"""

import unreal

PACKAGE_PATH = "/Game/MetaHumans/_ProportionMatrix"

# Variant matrix: constraint names use the UI names lowered with underscores,
# exactly as surfaced by MetaHumanCharacterEditorSubsystem.get_body_constraints.
# Measurements are centimeters. Baseline for comparison is Kellan (unmodified).
VARIANTS = {
    "PM_Short": {"height": 155.0},
    "PM_Tall": {"height": 195.0},
    "PM_LongArms": {"height": 178.0, "upper_arm_length": 42.0},
    "PM_ShortLegs": {"height": 165.0, "inseam": 70.0},
}


def _create_character(asset_name: str) -> unreal.Object:
    asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
    return asset_tools.create_asset(
        asset_name=asset_name,
        package_path=PACKAGE_PATH,
        asset_class=unreal.MetaHumanCharacter,
        factory=unreal.new_object(type=unreal.MetaHumanCharacterFactoryNew),
    )


def _apply_constraints(subsystem, character, targets: dict) -> None:
    constraints = subsystem.get_body_constraints(character)
    by_name = {
        str(constraint.name).lower().replace(" ", "_"): constraint
        for constraint in constraints
    }
    unknown = set(targets) - set(by_name)
    if unknown:
        raise KeyError(
            f"Unknown constraint name(s) {sorted(unknown)}; "
            f"available: {sorted(by_name)}")
    for name, measurement in targets.items():
        by_name[name].is_active = True
        by_name[name].target_measurement = float(measurement)
    subsystem.set_body_constraints(character, list(by_name.values()))
    subsystem.commit_body_state(character)


def generate() -> list[str]:
    subsystem = unreal.get_editor_subsystem(unreal.MetaHumanCharacterEditorSubsystem)
    created = []
    for asset_name, targets in VARIANTS.items():
        asset_path = f"{PACKAGE_PATH}/{asset_name}"
        if unreal.EditorAssetLibrary.does_asset_exist(asset_path):
            unreal.log(f"{asset_name}: already exists, skipping")
            continue
        character = _create_character(asset_name)
        if not subsystem.try_add_object_to_edit(character):
            unreal.log_warning(f"{asset_name}: could not open for edit, skipping")
            continue
        try:
            _apply_constraints(subsystem, character, targets)
            unreal.EditorAssetLibrary.save_loaded_asset(character)
            created.append(f"{PACKAGE_PATH}/{asset_name}")
            unreal.log(f"{asset_name}: constraints applied {targets}")
        finally:
            if subsystem.is_object_added_for_editing(character):
                subsystem.remove_object_to_edit(character)
    # TODO(next session): assemble each character to a skeletal-mesh build
    # (see engine example_assembly.py) and register an avatar profile so the
    # replay map can target it — Docs/PROPORTION_ROBUSTNESS_MATRIX.md step 3.
    return created


if __name__ == "__main__":
    for path in generate():
        unreal.log(f"Created variant: {path}")
