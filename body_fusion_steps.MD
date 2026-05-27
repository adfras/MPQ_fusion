# Hybrid Body Fusion Plan

Project: `D:\Epic\Unreal_Projects\TestingKit3`

Purpose: fix embodied first-person alignment so leaning, head movement, upper body, arms, hips, and legs are solved in one avatar-space frame instead of leaving the camera able to drift into or behind the avatar body.

Rule for this work: normal PIE, build, automation, logs, and screenshots are allowed. VR Preview is for Alan to run manually.

## Core Decision

The next system is not more CVar tuning. It is a body-fusion layer with clear authority rules:

```text
HMD / Quest headset: head, eyes, local camera anchor
Quest hands / full arm chain: hands, wrists, arms, upper limbs when valid
MediaPipe webcam: pelvis, hips, legs, feet, lower-body pose
Avatar profile: proportions, bone map, eye/chest/pelvis offsets, forward axis, local-view policy
Fusion solver: one coherent avatar-space body pose
Pose writer: applies that fused pose to Manny or MetaHuman bones
```

## Abstractions To Add

- [x] `FMediaPipeTrackingSourceFrame`
  - [x] Captures one timestamped runtime frame containing HMD pose, Quest hand/arm data, MediaPipe landmarks, source reliabilities, and source timestamps.
  - [x] Keeps source collection separate from solving.
  - [x] Records whether each source is fresh, stale, missing, or invalid.

- [x] `FMediaPipeEmbodimentCalibration`
  - [x] Stores the transform that maps external MediaPipe camera/body space into Quest/HMD/avatar space.
  - [x] Stores yaw, translation, scale, and confidence separately.
  - [x] Supports reset/recalibrate without wiping unrelated wrist/finger state.
  - [x] Never applies raw HMD height directly to avatar root Z.

- [x] `FMediaPipeBodyFusionAuthority`
  - [x] Encodes ownership rules for head, chest, spine, pelvis, arms, hands, legs, and feet.
  - [x] Defaults to HMD head authority, Quest upper-limb authority, MediaPipe lower-body authority.
  - [x] Blocks MediaPipe arms from overwriting fresh Quest/full-arm-chain arms.
  - [x] Blocks Quest inferred legs from overwriting reliable MediaPipe legs.

- [x] `FMediaPipeFusedAvatarPose`
  - [x] Represents the solved avatar-space pose before any bones are written.
  - [x] Contains head/eye, neck, chest, pelvis, shoulders, elbows, wrists, hips, knees, ankles, feet, and confidence per region.
  - [x] Stores debug errors such as camera-to-eye, head-to-chest, chest-to-pelvis, wrist reach, and foot reliability.

- [x] `FMediaPipeBodyFusionSolver`
  - [x] Pure math layer that consumes `FMediaPipeTrackingSourceFrame`, `FMediaPipeEmbodimentCalibration`, `FMediaPipeBodyFusionAuthority`, and `FMediaPipeAvatarEmbodimentProfile`.
  - [x] Produces `FMediaPipeFusedAvatarPose`.
  - [x] Owns HMD head anchoring, chest bridging, lower-body mapping, and source blending.
  - [x] Does not spawn actors, read console variables directly, or write bones.

- [x] `FMediaPipeAvatarPoseWriter`
  - [x] Applies `FMediaPipeFusedAvatarPose` to the existing anim-node bone write paths.
    - Added the first anim-node pose-writer adapter behind `mp.BodyFusion.Enable`; it writes fused pelvis/torso targets into the existing component-space pelvis/spine path and keeps the old path when BodyFusion is disabled.
  - [x] Keeps Manny and MetaHuman differences profile-driven.
    - The adapter consumes `TargetEmbodimentProfile` and skeleton reference-pose proportions rather than branching on Manny, Wallace, or any named MetaHuman.
  - [x] Keeps full-arm-chain, Quest hand/finger, twist-helper, and lower-body writes ordered explicitly.

## Work Checklist

- [x] 1. Freeze Current Runtime State
  - [x] Record current `mp.AutoQuestAvatar=0` Manny state.
    - Baseline issue recorded from Alan's VR Preview screenshots on 2026-05-23: Manny spawned in the room but the first-person camera could see down/inside the avatar body, especially with slight lean-back.
  - [x] Record current `mp.AutoQuestAvatar=1` MetaHuman state.
    - Baseline record: MetaHuman profile path was already usable for Emory in VR Preview, while Wallace exposed profile/arm inconsistency concerns that this plan must not reintroduce.
  - [x] Record current accepted Quest/full-arm-chain arm path.
    - Baseline protected path is the Quest/full-arm-chain upper-body authority; later covered by `TestingKit3.MediaPipe.BodyFusion.QuestUpperBodyAuthority`, `QuestHandMediaPipeHints`, and broad MediaPipe automation.
  - [x] Record current failure: leaning back lets the camera see down/inside the avatar body.
    - Failure recorded from user-provided Oculus Mirror/Quest screenshots before BodyFusion changes; normal PIE is not treated as VR proof for this failure.
  - [x] Do not change defaults in this step.
    - The broad default remains conservative: `mp.BodyFusion.Enable` is still opt-in for manual testing.

- [x] 2. Add Diagnostics Before Solving
  - [x] Add a compact log row for HMD location/yaw and tracking origin.
  - [x] Add avatar eye, head, neck, chest, pelvis, root, and forward-axis world positions.
  - [x] Add MediaPipe hip center, shoulder center, and head estimate in mapped avatar space.
  - [x] Add Quest wrist/hand and full-arm-chain source status.
  - [x] Add camera-to-eye, camera-to-chest, head-to-chest, and chest-to-pelvis distances.
  - [x] Add a HUD/debug mode, but keep it developer-only.
    - Implemented as developer-only `mp.BodyFusion.Debug` log rows; no user-facing CVar matrix was added.

- [x] 3. Implement `FMediaPipeTrackingSourceFrame`
  - [x] Read HMD pose from the existing Quest/HMD path.
  - [x] Read Quest hands and full-arm-chain snapshots from existing providers.
  - [x] Read MediaPipe landmarks from the existing pose frame.
  - [x] Add freshness/age checks per source.
  - [x] Add focused automation for stale/missing source classification.

- [x] 4. Implement `FMediaPipeEmbodimentCalibration`
  - [x] Define a neutral calibration pose: upright, facing mirror, stable HMD, visible MediaPipe hips/shoulders.
  - [x] Compute MediaPipe-to-avatar yaw from shoulder/hip/body forward versus HMD/avatar forward.
  - [x] Compute translation from MediaPipe hip/shoulder/head estimate to avatar profile anchors.
  - [x] Compute optional scale from avatar profile proportions versus observed MediaPipe body proportions.
  - [x] Reject calibration if MediaPipe confidence or HMD pose is unstable.
  - [x] Add logs that explain rejection reasons.
    - Runtime calibration is attempted only through the new BodyFusion frame path; accepted/rejected rows are emitted by `mp.BodyFusion.Debug`.

- [x] 5. Implement `FMediaPipeBodyFusionAuthority`
  - [x] Head and eye anchor are HMD authoritative.
  - [x] Hands, wrists, elbows, shoulders are Quest/full-arm-chain authoritative when fresh.
  - [x] Pelvis, hips, knees, ankles, feet are MediaPipe authoritative when visible/reliable.
  - [x] Chest/spine are a bridge between HMD head/neck and MediaPipe pelvis/hips.
  - [x] Add explicit fallback rules for source loss.

- [x] 6. Implement `FMediaPipeFusedAvatarPose`
  - [x] Define stable fields for all body regions before writing bones.
  - [x] Include confidence and source-owner tags for every region.
  - [x] Include debug deltas for validation.
  - [x] Add tests proving source-owner tags are preserved.

- [x] 7. Implement HMD Head Anchor
  - [x] Solve avatar eye/head from HMD pose in the pure solver.
  - [x] Keep the solved eye aligned to the avatar profile eye anchor.
  - [x] Do not move root Z directly from raw HMD height.
  - [x] Keep room-scale horizontal movement bounded and logged.
    - Added `MaxRoomScaleHorizontalOffsetCm` to the pure solver input, defaulted to the existing 400 cm room-scale jump guard, with no new user-facing CVar.
    - `mp.BodyFusion.Debug` rows now include `hmdPlanar(offset, clamp, clamped)` so tracking-origin jumps can be diagnosed without moving root Z from raw HMD height.
    - Covered by `TestingKit3.MediaPipe.BodyFusion.HmdHorizontalBounds`; extreme HMD horizontal offset is bounded relative to the avatar profile eye anchor and the applied clamp distance is logged.
  - [x] Test lean forward/back in math automation before VR Preview.

- [x] 8. Implement Chest/Spine Bridge
  - [x] Build chest target from HMD head/neck plus MediaPipe pelvis/hip support.
  - [x] Prevent torso from staying behind when the HMD leans.
  - [x] Clamp impossible chest/head distances using avatar profile proportions.
  - [x] Preserve enough torso visibility for embodied first-person view.
    - Covered by `TestingKit3.MediaPipe.BodyFusion.FirstPersonTorsoVisibility`; HMD lean-back keeps the fused chest under/near the first-person view rather than leaving it at the old pelvis line.
  - [x] Add tests for lean-back cases.

- [x] 9. Integrate Quest Upper Body Authority
  - [x] Keep existing Quest/full-arm-chain arm path as upper-limb authority.
    - Covered by `TestingKit3.MediaPipe.BodyFusion.QuestUpperBodyAuthority`; fresh Quest full-arm-chain shoulders/elbows/wrists are not overwritten by reliable MediaPipe arm landmarks.
  - [x] Ensure MediaPipe arm samples are elbow hints only when Quest authority is fresh.
    - Covered by `TestingKit3.MediaPipe.BodyFusion.QuestHandMediaPipeHints`; Quest wrist remains authoritative while MediaPipe shoulder/elbow points remain available as hints.
  - [x] Preserve current hand/finger behavior.
    - BodyFusion pose writes remain pelvis/torso-only in the anim-node adapter; Quest hand/finger writes are explicitly preserved by `FMediaPipeAvatarPoseWritePlan`, and the broad MediaPipe suite still passes.
  - [x] Preserve existing twist-helper protections.
    - `DriveArmCS` and `DriveArmTwistBonesCS` still run after the BodyFusion pelvis/torso adapter, so the accepted arm/twist path is not bypassed.
  - [x] Add regression tests for Wallace/MetaHuman profile arm authority.
    - Covered by `TestingKit3.MediaPipe.BodyFusion.MetaHumanProfileArmAuthority` for Wallace and Emory using the same fusion solver and authority rules.

- [x] 10. Integrate MediaPipe Lower Body Authority
  - [x] Enable pelvis/hip/leg/foot solve only through the fusion authority layer.
    - Added `FMediaPipeAvatarPoseWriter::TryGetMediaPipeLowerBodySide`; it exposes hip/knee/ankle/foot to the leg writer only when the fused pose marks them fresh, valid, and MediaPipe-owned.
    - `DriveLegCS` now uses the fused lower-body side when BodyFusion is enabled and the fused pose is usable; unreliable or non-MediaPipe-owned lower-body points do not fall back to raw leg landmarks in that path.
    - Covered by `TestingKit3.MediaPipe.BodyFusion.LowerBodyAuthorityAdapter`.
  - [x] Add reliability gates before using hips, knees, ankles, or feet.
    - Added per-landmark reliability gates inside `FMediaPipeBodyFusionSolver`; low-confidence hips/knees/ankles/feet are not emitted as fused lower-body points.
  - [x] Keep lower body from pulling the HMD head anchor out of alignment.
    - Covered by `TestingKit3.MediaPipe.BodyFusion.LowerBodyReliabilityGates`; unreliable lower body falls back to profile pelvis and leaves the HMD head anchor unchanged.
  - [x] Add tests for lower-body source present, stale, missing, and unreliable.
    - Covered by `TestingKit3.MediaPipe.BodyFusion.LowerBodySourceLossFallbacks` and `TestingKit3.MediaPipe.BodyFusion.LowerBodyReliabilityGates`.

- [x] 11. Add Avatar Profile Proportion Fields
  - [x] Add profile chest offset.
  - [x] Add profile pelvis offset.
  - [x] Add head-to-chest and chest-to-pelvis expected ranges.
  - [x] Add arm and leg length fields or derive them from skeleton reference pose.
    - Added expected/min/max upper-arm, lower-arm, thigh, and calf lengths to `FMediaPipeAvatarEmbodimentProfile`.
    - Default values derive from avatar eye height; runtime target profiles are refined from the resolved skeleton reference pose when available.
  - [x] Keep Manny, Wallace, Emory, Hudson, Kellan, Maria, Payton data profile-driven.
    - Chest/pelvis offsets and torso ranges are derived through `FMediaPipeAvatarEmbodimentProfile` for Manny-like and MetaHuman profiles, not per-avatar branches.

- [x] 12. Add First-Person Geometry Policy
  - [x] Keep avatar body visible locally.
    - Default local-view policy does not cull single-mesh avatars, so Manny remains locally visible instead of disappearing.
    - Separate MetaHuman body components remain locally visible.
  - [x] Prevent local camera from rendering head/face/interior geometry.
    - Covered for multi-component MetaHumans through owner-no-see local culling of separate head/face/hair-style components.
    - Covered for Manny-like single-mesh avatars through `MP_LocalFirstPersonBodyProxy`: the full mesh stays `OwnerNoSee` for the local owner and visible to mirror/external views, while the owner-only poseable proxy copies the pose and hides/scales the `head` bone locally.
  - [x] For MetaHumans, continue owner-no-see for separate head/face/hair components.
    - Covered by `TestingKit3.MediaPipe.AvatarEmbodiment.LocalViewPolicy`.
  - [x] For Manny single-mesh, investigate a proper local-view body mesh, material mask, or bone/material clipping path.
    - Implemented as a body-only local proxy path, not whole-avatar hiding: `UMediaPipeFirstPersonBodyProxyComponent` drives a poseable proxy from the source skeletal mesh and applies local-only hidden-bone policy.
  - [x] Mirror/external view must still show the full avatar.
    - The policy uses `OwnerNoSee` for local-only component culling, not `HiddenInGame`, and explicitly keeps culled components visible for mirror/reflection/external views.
    - For single-mesh Manny, the original full mesh remains visible to non-owner mirror/external views while the poseable proxy is `OnlyOwnerSee`.

- [x] 13. Wire Into Anim Node In Small Steps
  - [x] Keep source read in `PreUpdate`.
  - [x] Keep pure solve separated from bone writes.
  - [x] Replace direct scattered source ownership checks with `FMediaPipeBodyFusionAuthority`.
    - Lower-body source ownership now routes through `FMediaPipeBodyFusionAuthority::ResolveLowerBodyOwner`; upper-body ownership already routes through `ResolveUpperLimbOwner`.
    - Pelvis/torso/leg writes consume fused pose data through the pose-writer adapters instead of each write path making its own raw source decision.
  - [x] Replace direct torso/leg source use with `FMediaPipeFusedAvatarPose` where safe.
    - Pelvis/torso writes use `LastBodyFusionPose` behind `mp.BodyFusion.Enable`.
    - Leg writes now use `FMediaPipeAvatarPoseWriter::TryGetMediaPipeLowerBodySide` behind `mp.BodyFusion.Enable`; the legacy raw landmark leg path remains only when BodyFusion is disabled or has no usable fused pose.
  - [x] Leave existing accepted Quest hand/finger code intact unless the fusion boundary requires an adapter.

- [x] 14. Add Developer CVars Carefully
  - [x] Add one enable switch, default off until proven: `mp.BodyFusion.Enable`.
  - [x] Add one diagnostics switch: `mp.BodyFusion.Debug`.
  - [x] Add one calibration command/reset path.
    - Added `mp.BodyFusion.ResetCalibration`; it resets only BodyFusion neutral calibration and does not reset Quest wrist/finger/arm state.
  - [x] Avoid exposing a large user-facing CVar matrix.
  - [x] Keep normal user testing commands simple.
    - No new user-facing CVar was added for HMD horizontal bounds; the manual test command blocks below remain unchanged.

- [x] 15. Build And Automation Validation
  - [x] Build `TestingKit3Editor Win64 Development`.
  - [x] Add focused automation for calibration, authority policy, fusion solve, and lean-back regression.
  - [x] Run focused `TestingKit3.MediaPipe.BodyFusion`.
  - [x] Run broad `TestingKit3.MediaPipe`.
  - [x] Record exact pass counts and log paths.
    - Build passed after profile proportion and torso-clamp integration: `TestingKit3Editor Win64 Development`; UBT log `C:\Users\Alan\AppData\Local\UnrealBuildTool\Log.txt`.
    - Focused BodyFusion passed: 6/6 tests; log `Saved\Logs\TestingKit3-backup-2026.05.23-08.34.16.log`.
    - Diagnostics reset test passed separately: 1/1 tests; log `Saved\Logs\TestingKit3-backup-2026.05.23-08.35.38.log`.
    - Runtime CVar test passed with `mp.BodyFusion.ResetCalibration` registration covered: 1/1 tests inside final broad run.
    - Broad MediaPipe passed on current code after profile proportion changes: 67/67 tests; log `Saved\Logs\TestingKit3.log`.
    - Build passed after profile arm/leg proportions and lower-body reliability gates: `TestingKit3Editor Win64 Development`; UBT log `C:\Users\Alan\AppData\Local\UnrealBuildTool\Log.txt`.
    - Broad MediaPipe passed on current code after lower-body reliability gates: 68/68 tests; log `Saved\Logs\TestingKit3-backup-2026.05.23-10.09.02.log`.
    - Focused BodyFusion passed on current code after lower-body reliability gates: 7/7 tests; log `Saved\Logs\TestingKit3.log`.
    - Build passed after the BodyFusion anim-node pose-writer adapter: `TestingKit3Editor Win64 Development`; UBT log `C:\Users\Alan\AppData\Local\UnrealBuildTool\Log.txt`.
    - Broad MediaPipe passed after the BodyFusion anim-node pose-writer adapter: 68/68 tests; log `Saved\Logs\TestingKit3.log`.
    - Build passed after lower-body source-loss automation: `TestingKit3Editor Win64 Development`; UBT log `C:\Users\Alan\AppData\Local\UnrealBuildTool\Log.txt`.
    - Focused BodyFusion passed after lower-body source-loss automation: 8/8 tests; log `Saved\Logs\TestingKit3-backup-2026.05.23-10.24.17.log`.
    - Broad MediaPipe passed after lower-body source-loss automation: 69/69 tests; log `Saved\Logs\TestingKit3.log`.
    - Build passed after HMD horizontal-bound solver/debug integration: `TestingKit3Editor Win64 Development`; UBT log `C:\Users\Alan\AppData\Local\UnrealBuildTool\Log.txt`.
    - Focused BodyFusion passed after HMD horizontal-bound solver/debug integration: 9/9 tests; log `Saved\Logs\TestingKit3-backup-2026.05.23-11.16.13.log`.
    - Broad MediaPipe passed after HMD horizontal-bound solver/debug integration: 70/70 tests; log `Saved\Logs\TestingKit3.log`.
    - Build passed after Quest upper-body authority regressions: `TestingKit3Editor Win64 Development`; UBT log `C:\Users\Alan\AppData\Local\UnrealBuildTool\Log.txt`.
    - Focused BodyFusion passed after Quest upper-body authority regressions: 12/12 tests; log `Saved\Logs\TestingKit3-backup-2026.05.23-11.45.47.log`.
    - Broad MediaPipe passed after Quest upper-body authority regressions: 73/73 tests; log `Saved\Logs\TestingKit3.log`.
    - Build passed after fused lower-body leg adapter integration: `TestingKit3Editor Win64 Development`; UBT log `C:\Users\Alan\AppData\Local\UnrealBuildTool\Log.txt`.
    - Focused BodyFusion passed after fused lower-body leg adapter integration: 13/13 tests; log `Saved\Logs\TestingKit3-backup-2026.05.23-12.47.38.log`.
    - Broad MediaPipe passed after fused lower-body leg adapter integration: 74/74 tests; log `Saved\Logs\TestingKit3.log`.
    - Build passed after local-view policy coverage: `TestingKit3Editor Win64 Development`; UBT log `C:\Users\Alan\AppData\Local\UnrealBuildTool\Log.txt`.
    - Focused AvatarEmbodiment passed after local-view policy coverage: 4/4 tests; log `Saved\Logs\TestingKit3-backup-2026.05.23-12.53.25.log`.
    - Broad MediaPipe passed after local-view policy coverage: 74/74 tests; log `Saved\Logs\TestingKit3.log`.
    - Build passed after first-person torso visibility regression: `TestingKit3Editor Win64 Development`; UBT log `C:\Users\Alan\AppData\Local\UnrealBuildTool\Log.txt`.
    - Focused BodyFusion passed after first-person torso visibility regression: 14/14 tests; log `Saved\Logs\TestingKit3-backup-2026.05.23-13.00.20.log`.
    - Broad MediaPipe passed after first-person torso visibility regression: 75/75 tests; log `Saved\Logs\TestingKit3.log`.
    - Build passed after BodyFusion source-ownership routing cleanup: `TestingKit3Editor Win64 Development`; UBT log `C:\Users\Alan\AppData\Local\UnrealBuildTool\Log.txt`.
    - Focused BodyFusion passed after source-ownership routing cleanup: 14/14 tests; log `Saved\Logs\TestingKit3-backup-2026.05.23-13.04.35.log`.
    - Broad MediaPipe passed after source-ownership routing cleanup: 75/75 tests; log `Saved\Logs\TestingKit3.log`.
    - Build passed after poseable first-person body proxy integration: `TestingKit3Editor Win64 Development`; UBT log `C:\Users\Alan\AppData\Local\UnrealBuildTool\Log.txt`.
    - Focused AvatarEmbodiment passed after poseable first-person body proxy integration: 4/4 tests; log `Saved\Logs\TestingKit3-backup-2026.05.23-13.33.50.log`.
    - Broad MediaPipe passed after poseable first-person body proxy integration: 75/75 tests; log `Saved\Logs\TestingKit3.log`.
    - Focused BodyFusion passed on current code after poseable first-person body proxy integration: 14/14 tests; log `Saved\Logs\TestingKit3_2.log`.

- [x] 16. Normal PIE Validation Only
  - [x] Start normal PIE through MCP, not VR Preview.
    - `start_pie` returned `vrPreview=false`.
  - [x] Confirm Manny spawn and active source frame.
    - Normal PIE spawned `MP_LiveMediaPipeManny`, `MP_LiveMediaPipeVideo`, and `DefaultPawn0`; screenshot `Saved\CodexAgent\Screenshots\bodyfusion_manny_normal_pie.png`.
    - `mp.BodyFusion.Debug` emitted a source row with MediaPipe fresh and HMD missing, which is expected in normal non-VR PIE.
    - Normal PIE after the poseable proxy change verified `MP_LiveMediaPipeManny` at floor level with full `Mesh owner_no_see=true`, `MP_LocalFirstPersonBodyProxy only_owner_see=true`, proxy `head hidden=true`, proxy head scale `0,0,0`, and neck/spine/pelvis still visible; screenshot `Saved\CodexAgent\Screenshots\bodyfusion_poseable_proxy_pie.png`.
  - [x] Confirm MetaHuman spawn and active source frame.
    - Runtime MetaHuman branch spawned `MP_LiveMetaHumanEmory` with `mp.AutoQuestAvatar=1` after explicitly setting the CVar in PIE and running `mp.SpawnQuestWebcamHandsNow`; screenshot `Saved\CodexAgent\Screenshots\bodyfusion_emory_normal_pie.png`.
    - Note: starting normal PIE through the current wrapper reset `mp.AutoQuestAvatar` to `0`, so the MetaHuman check used the runtime spawn command after PIE start. VR Preview still needs Alan validation.
  - [x] Confirm debug logs emit head/chest/pelvis/camera errors.
    - `mp.BodyFusion.Debug` rows include HMD/source freshness, avatar root/eye/head/chest/pelvis positions, MediaPipe hip/shoulder/head estimates, and `dist(cameraEye, cameraChest, headChest, chestPelvis)` fields.
    - Current debug rows also include `hmdPlanar(offset, clamp, clamped)` from the fused solve so horizontal room-scale bounds are visible in logs.
  - [x] Stop PIE after validation.

- [ ] 17. Manual VR Preview Validation By Alan
  - [ ] Manny: test standing neutral.
  - [ ] Manny: test slight lean back.
  - [ ] Manny: test looking down at body.
  - 2026-05-23 Alan VR Preview retest after the local poseable body proxy still failed: Manny can still show the same first-person inside-body view. Normal PIE proxy evidence is therefore not enough; next work must compare the working Owen/Manny embodiment path in `C:\Users\Alan\OneDrive\Documents\Unreal Projects\Unreal-Movement` and fix the actual camera/body alignment path.
  - [ ] MetaHuman: test one profile, preferably known-good Emory or Wallace.
  - [ ] MetaHuman: test profile swap only after Manny is stable.
  - [ ] Use Oculus Mirror screenshots/log capture for evidence if needed.

## Non-Negotiable Guardrails

- [x] No per-avatar solver branches.
- [x] No Wallace-specific fusion path.
- [x] No direct raw HMD-height-to-root-Z fix.
- [x] No MediaPipe overwrite of fresh Quest/full-arm-chain arms.
- [x] No Quest inferred legs overwriting reliable MediaPipe legs.
- [x] No hiding the whole avatar locally as a workaround.
- [x] No declaring success from normal PIE when the user asked about VR Preview behavior.
- [ ] No broad default change until focused automation and at least one manual VR Preview check agree.

## User-Facing Test Commands Target

The final user-facing commands should stay simple.

Manny:

```text
mp.AutoQuestWebcamHands 1
mp.AutoQuestAvatar 0
mp.AutoQuestEmbodiedView 1
mp.BodyFusion.Enable 1
```

MetaHuman:

```text
mp.AutoQuestWebcamHands 1
mp.AutoQuestAvatar 1
mp.AutoQuestEmbodiedView 1
mp.BodyFusion.Enable 1
mp.MetaHumanActiveProfile Emory
```

Swap `Emory` for `Wallace`, `Hudson`, `Kellan`, `Maria`, or `Payton`.

## Definition Of Done

- [ ] Leaning back no longer reveals a view down/inside the avatar body in VR Preview.
  - 2026-05-23: still failing for Manny in Alan's VR Preview after the poseable proxy change.
- [ ] Looking down still shows embodied torso/arms/legs, not an invisible avatar.
- [ ] Quest hands/arms remain responsive and do not regress.
- [x] MediaPipe lower body can drive hips/legs when visible and reliable.
  - Covered by `TestingKit3.MediaPipe.BodyFusion.LowerBodyAuthorityAdapter`, `LowerBodyReliabilityGates`, and `LowerBodySourceLossFallbacks`; current focused BodyFusion run passed 14/14 in `Saved\Logs\TestingKit3_2.log`.
- [x] Manny and at least one MetaHuman use the same fusion solver.
  - Covered by `TestingKit3.MediaPipe.BodyFusion.MetaHumanProfileArmAuthority`; Wallace and Emory both solve through `FMediaPipeBodyFusionSolver` using profile-built embodiment data.
- [x] All profile differences live in profile data.
  - Manny and MetaHuman embodiment profiles are built through `FMediaPipeAvatarEmbodimentProfile`; built-in MetaHuman profiles remain data entries for Wallace, Emory, Hudson, Kellan, Maria, and Payton.
- [ ] Build, focused automation, broad automation, normal PIE, and user VR Preview results are documented.
