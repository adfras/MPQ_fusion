# Proportion-Robustness Matrix — Body-Shape Generalization Gate

**Status: variants generated, rigged, and assembled (2026-07-03, live editor
run).** All four characters exist under `/Game/MetaHumans/_ProportionMatrix/`
and each has a built skeletal-mesh pair on disk under
`_ProportionMatrix/Builds/<name>/` (`SKM_<name>_BodyMesh` + `_FaceMesh` +
DNA assets; OPTIMIZED pipeline, MEDIUM quality). Remaining steps: avatar
profile registration per variant + replay-gate integration (workflow steps
3–5 below).

Pipeline order that actually works (each step gates the next):

1. constraints → 2. **auto-rig** (Epic cloud, EOS login required, ~30 s per
   character, `blocking=False` ONLY) → 3. **texture sources download**
   (`request_texture_sources`, cloud, seconds — assembly refuses with
   "missing textures" without it, because the local texture-synthesis
   optional content is not installed) → 4. `build_meta_human`.

**Do not point `common_folder_path` at `/Game/MetaHumans/Common`.** That is
the production Common folder shared by Kellan/Wallace/etc.; assembly pops an
overwrite-confirmation dialog that explicitly warns it may break those
MetaHumans (cancelled 2026-07-03, zero writes verified). Matrix builds use
`/Game/MetaHumans/_ProportionMatrix/Common`.

Auto-rig rule learned the hard way: request it **non-blocking**
(`blocking=False`). A `blocking=True` rig call issued through an MCP tool
pumps the engine loop inside an open HTTP request and crashes the
Experimental MCP plugin (SharedPointer `IsValid()` assert). See
`Docs/UNREAL_MCP_OPERATIONS.md`.

## Why

Every solver tolerance in this project has been tuned against Kellan's
proportions (with Wallace as secondary). Whether embodiment survives a
195 cm avatar or 70 cm inside-leg is untested. This matrix turns
avatar-proportion generalization into a regression gate: same canonical
replay dataset, N avatar bodies, diffed measurements.

## Engine-reality note (2026-07-03)

The **MetaHumanGenerator MCP toolset** named in Epic's 5.8 release notes is
**not present** in the 5.8.0 launcher install (searched
`D:\Epic\UE_5.8\Engine\Plugins` — no such toolset ships). The
**MetaHumanCharacter Python API** covers everything needed and more
(body constraints by name, sculpt, conform, assembly), so the matrix is built
on that instead. If a later 5.8.x hotfix ships the toolset, it only replaces
the create/set-shape calls, not the workflow.

## Workflow

1. **Generate variants** — run `Content/Python/generate_body_variants.py` in
   the editor (Python console or MCP ProgrammaticToolset). Creates blank-faced
   characters under `/Game/MetaHumans/_ProportionMatrix/` shaped purely by
   body constraints. Appearance is irrelevant; bone lengths are the variable.
   Current matrix: `PM_Short` (155 cm), `PM_Tall` (195 cm), `PM_LongArms`
   (`upper_arm_length` 42), `PM_ShortLegs` (`inseam` 70 — the leg-length
   constraint is called `inseam`, not "inside leg"). Full live-verified
   constraint list: across_shoulder, bicep, bust_span, calf, chest, elbow,
   fat, forearm, front_interscye_length, hand_circumference, height,
   high_hip, hip, inseam, knee, lower_arm_length, masculine/feminine,
   muscularity, neck, neck_base, neck_length, neck_to_waist, rise,
   shoulder_height, shoulder_to_apex, thigh, underbust, upper_arm_length,
   waist, wrist. The script fails loudly on unknown names, so extending the
   matrix is safe.
2. **Assemble** each variant to a rigged skeletal-mesh build — engine example:
   `MetaHumanCharacter/Content/Python/examples/example_assembly.py`. (Open
   step; assembly pipeline settings need one interactive pass first.)
3. **Register an avatar profile** per variant so the replay map can target it
   — follow `Docs/METAHUMAN_PROFILE_DRIVEN_RETARGETING.md` and
   `Docs/AVATAR_PROFILE_DRIVEN_EMBODIMENT.md` conventions.
4. **Run the replay gate per variant** — same canonical dataset, same policy
   CVars, only the avatar profile changes.
5. **Diff** — `Tools/compare_replay_measurements.py` against the Kellan
   baseline. Expected outcome is *graceful scaling*: joint-angle trajectories
   should match closely (angles are proportion-invariant); positional
   measurements should scale with limb length, not clamp, snap, or invert.

## What failure looks like (worth the gate)

- Reach clamps tuned in centimeters (`mp.MediaPipeArmMaxWristStepCm 55`,
  `MaxElbowStepCm 35`) firing constantly on long arms → laggy/short reach.
- Knee-raise amplitude scaling wrongly on short legs (the half-height class
  of bug, but proportion-induced instead of state-loss-induced).
- Calibration/height scaffolds assuming Kellan-ish stature
  (`mp.BodyFusion.*` calibration paths) misestimating on 155/195 cm bodies.

## Standing rule

A variant "passing" means its measurement diff was reviewed, not merely that
the replay ran. Any tolerance change motivated by this matrix goes through
the usual multiple-writer CVar review (`Docs/CVAR_REFERENCE.md`).
