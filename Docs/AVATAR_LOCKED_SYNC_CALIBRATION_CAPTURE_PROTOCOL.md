# Avatar-Locked Sync Calibration Capture Protocol

Use this protocol for the next VR Preview when the current capture has enough recorder throughput but lacks usable evidence for head anchor, wrist offsets, axis correction, or lower-body readiness.

## Console Command

Run before VR Preview:

```text
mp.PrepareAvatarLockedSyncCalibrationCapture label=avatar_locked_sync_calibration analyze=1
```

The command arms the existing tracking-fusion recorder with:

- `mp.RecordTrackingFusionDatasetSampleRate=30`
- `mp.RecordTrackingFusionDatasetBoneMode=all`
- `mp.RecordTrackingFusionDatasetPhasePreset=avatar_locked_sync_calibration`
- `mp.RecordTrackingFusionDatasetDuration=210`
- green on-screen movement prompts
- yellow Quest wrist/arm calibration HUD suppression for this capture mode
- visible full-body avatar-write calibration policy:
  - `mp.BodyFusion.Enable=1`
  - `mp.BodyFusion.Debug=1`
  - `mp.BodyFusion.WritePose=1`
  - `mp.BodyFusion.MediaPipeAuthority=2`
  - `mp.MediaPipeDriveSpine=1`
  - `mp.MediaPipeDrivePelvisTranslation=1`
  - `mp.MediaPipeDriveLegs=1`
  - `mp.MediaPipeUseLegIK=0`
  - `mp.MediaPipeUseLegIKFootPlant=0`
  - `mp.MediaPipeDriveFootRotation=1`

It temporarily enables visible avatar-write/full-body drive gates so source-to-avatar correlation evidence is meaningful. Legs use Kellan's direct segment chain instead of replay target IK because replay target IK overdrives the recorded lower-body phase. It still does not enable avatar scaling, user-to-avatar body fitting, MetaHuman deformation, or selected-bone recording. The avatar body and proportions remain authoritative; the user conforms to the chosen avatar.

## Seven 30-Second Blocks

1. `avatar_locked_head_30s`: deliberate head yaw/pitch/roll, plus forward/back and side-to-side head translation.
2. `avatar_locked_hands_wrists_30s`: left, right, then both hand sweeps across X/Y/Z; wrist circles; reach forward/back; cross-body motion.
3. `avatar_locked_arms_30s`: elbow bends/extensions, shoulder raises, lateral reaches, alternating left/right arm motions.
4. `avatar_locked_torso_30s`: slow forward/back lean, side lean, chest twist left/right while feet stay planted.
5. `avatar_locked_hips_30s`: hip sway, pelvis circles, weight shifts, crouch/stand transitions with hips visible.
6. `avatar_locked_legs_30s`: alternating knee lifts, shallow squats, forward/back steps, side lunges.
7. `avatar_locked_feet_30s`: heel raises, toe taps, ankle flex, step in place with both feet visible.

Each block is serialized into `movement_phases` and every sample's `phase` object with the exact phase name, region, expected signal targets, readiness targets, and countdown timing.

## Policy And Lower-Body Interpretation

Analyzer preflight must reject an `avatar_locked_sync_calibration` capture as setup-invalid if the manifest/settings show `mp.BodyFusion.WritePose=0`, `mp.BodyFusion.MediaPipeAuthority=0`, any of the spine/pelvis/legs/foot-rotation gates off, or `mp.MediaPipeUseLegIKFootPlant=1` while leg IK is enabled. That is an invalid capture policy, not evidence that the user failed to move.

The 2026-06-09 failed run `tracking_fusion_dataset_avatar_locked_sync_calibration_20260609_154033.json` had all seven green phases and all 342 bones, but inherited the shadow/no-write policy (`WritePose=0`, `MediaPipeAuthority=0`, lower-body gates off). It is a regression fixture for analyzer policy rejection, not a valid avatar-follow correlation run.

Lower-body rows must distinguish:

- raw MediaPipe source availability,
- raw MediaPipe source motion sufficiency,
- avatar/fused output constrained by an invalid or intentionally diagnostic-only policy,
- true source-to-avatar correlation failure.

The analyzer writes this distinction under `avatar_locked_capture_policy_preflight`, `raw_mediapipe_region_source_status`, `avatar_output_policy_by_region`, `lower_body_region_status`, and `calibration_capture_sufficiency`.

## Recorder Throughput

Do not lower the 30 Hz target, reduce all-bone coverage, or switch to selected bones to hide recorder misses. The recorder should preserve compact float32 all-bone sidecars and source metadata while keeping JSON/sample serialization and file flush work off the VR hot path. New captures should include recorder timing counters for scheduler misses, skipped ticks, sample build time, enqueue time, writer backlog, file flush/write time, and post-capture analysis time.
