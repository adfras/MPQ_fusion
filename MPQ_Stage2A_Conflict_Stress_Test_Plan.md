# MPQ Clean-Fusion Shoulder/Shrug Evidence Test Plan

## Purpose

Verify that MediaPipe shoulder/shrug observations enter the MPQ fusion diagnostics without direct MetaHuman clavicle/helper-bone writes, hand endpoint drift, MediaPipe arm fallback, or stale authority assumptions.

Stage 2 is not a MetaHuman bone writer. Shoulder output is implemented through BodyFusion and the avatar-profile fused pose writer; Stage 2 rows are evidence/diagnostics only. The visible MetaHuman path must not force-set the upper-arm socket to the MediaPipe shoulder point.

Current status: Stage 2A/order-fix proof is superseded by the 2026-06-08 clean-fusion refactor. The remaining validation item is a user-run VR capture proving visible fused movement, clean source correlation, and no direct Stage writes.

## Setup

Use map:

```text
/Game/MetaHumanRooms/L_MetaHumanPreviewRoom_MPQSignalCompare_01
```

For visible VR proof, do not arm shadow-only capture. Use visible BodyFusion pose output:

```text
mp.RecordMannyHeadOnPlay 0
mp.RecordMPQShadowFusionOnPlay 0
mp.BodyFusion.Enable 1
mp.BodyFusion.Debug 1
mp.BodyFusion.WritePose 1
mp.BodyFusion.MediaPipeAuthority 0
mp.BodyFusion.Stage1TorsoPelvisHint 0
mp.BodyFusion.Stage2ShoulderClavicleHint 0
mp.MediaPipeDriveClavicles 0
mp.MediaPipeDriveMetaHumanArmHelpers 0
mp.MediaPipeDriveArmTwistBones 0
```

With `mp.BodyFusion.Enable=1`, AutoQuest profile application must keep the raw `mp.MediaPipeDriveClavicles`, `mp.MediaPipeDriveSpine`, and `mp.MediaPipeDrivePelvisTranslation` layers off. BodyFusion owns the visible trunk/shoulder output.

For evidence-only review, arm a shadow-only clean-fusion capture:

```text
mp.RecordMannyHeadOnPlay 0
mp.MediaPipeDriveClavicles 0
mp.MediaPipeDriveMetaHumanArmHelpers 0
mp.PrepareMPQShadowLatencyTrial maxdim=384 duration=60 prediction=1 maxPredictionMs=50 label=mpq_clean_fusion_sources_no_stage_writes stage1=0 stage2=1 stage2Blend=1.0 stage2Scale=1.0 stage2MaxLiftCm=5 stage2HalfLife=0.04 stage2ArmRaiseFadeStartCm=35 stage2ArmRaiseFadeFullCm=50 stage2ShrugStartCm=2 stage2ShrugFullCm=8 analyze=0
```

Required log proof before pressing VR Preview:

```text
mp.MPQShadowAutoStart: armed ... shadowOnly=1 authority=0 writePose=0 stage1TorsoPelvisHint=0 stage2ShoulderClavicleHint=1 armFallbacks=off
```

The capture report should also show these runtime CVars:

```text
mp.BodyFusion.WritePose=0
mp.BodyFusion.MediaPipeAuthority=0
mp.BodyFusion.Stage1TorsoPelvisHint=0
mp.BodyFusion.Stage2ShoulderClavicleHint=1
mp.BodyFusion.Stage2ShoulderClavicleHintBlend=1.0
mp.BodyFusion.Stage2ShoulderClavicleResponseScale=1.0
mp.BodyFusion.Stage2ShoulderClavicleMaxLiftCm=5
mp.BodyFusion.Stage2ShoulderClavicleHalfLife=0.04
mp.BodyFusion.Stage2ShoulderArmRaiseFadeStartCm=35
mp.BodyFusion.Stage2ShoulderArmRaiseFadeFullCm=50
mp.BodyFusion.Stage2ShoulderShrugStartCm=2
mp.BodyFusion.Stage2ShoulderShrugFullCm=8
```

## VR Movement Script

Use clear separated movements so the analyzer can distinguish MediaPipe shoulder evidence from Quest endpoint observations.

1. 0-5s: stillness, arms relaxed.
2. 5-15s: shoulder shrugs only, hands quiet.
3. 15-25s: left arm forward reach while lightly shrugging.
4. 25-35s: right arm forward reach while lightly shrugging.
5. 35-45s: both arms raise/lower to chest height while shoulders shrug.
6. 45-55s: wrist rotations and hand movement with shoulders quiet.
7. 55-60s: stillness, arms relaxed.

Avoid deliberate Quest controller occlusion unless the goal is an occlusion-specific test.

## Signals To Check

Primary MediaPipe shoulder evidence rows:

```text
mp_world_unreal.left_shoulder_lift_from_hips  -> mp_candidate.left.shoulder_lift_from_pelvis
mp_world_unreal.right_shoulder_lift_from_hips -> mp_candidate.right.shoulder_lift_from_pelvis
mp_candidate.left.shoulder_lift_from_pelvis   -> fused.left.shoulder.z
mp_candidate.right.shoulder_lift_from_pelvis  -> fused.right.shoulder.z
```

`manny.*` is the analyzer's legacy live-bone signal namespace. It does not mean the driven avatar is Manny. Confirm the actual driven mesh in `mpq_shadow_report.json` at `driven_component_summary.primary_driven_component`; for the MPQ map proof this should be the MetaHuman body component `BP_Kellan_C_0.Body`.

Direct Stage write guard rows:

```text
stage2_debug.applied_clavicle_lift_cm        -> 0
stage2_debug.applied_clavicle_helper_lift_cm -> 0
manny.clavicle_l_world_lift_from_pelvis      -> diagnostic only
manny.clavicle_r_world_lift_from_pelvis      -> diagnostic only
```

Quest endpoint rows:

```text
quest.left.wrist.*  -> manny.hand_l.*
quest.right.wrist.* -> manny.hand_r.*
```

Measurement-only conflict rows:

```text
quest.*.shoulder.z -> mp_body.*_shoulder.z
quest.*.elbow.*    -> mp_body.*_elbow.*
quest.*.wrist.*    -> mp_body.*_wrist.*
```

## Pass Criteria

- Capture is a real MPQ file under `Saved/CodexAgent/Diagnostics/mpq_shadow_latency_*.json`.
- Runtime CVars match the setup block, including `WritePose=0`, `MediaPipeAuthority=0`, Stage 1 disabled, `Stage2ShoulderClavicleHint=1`, `Stage2ShoulderClavicleHintBlend=1.0`, `Stage2ShoulderClavicleResponseScale=1.0`, `Stage2ShoulderClavicleMaxLiftCm=5`, `Stage2ShoulderClavicleHalfLife=0.04`, `Stage2ShoulderShrugStartCm=2`, `Stage2ShoulderShrugFullCm=8`, `MediaPipeDriveClavicles=0`, and `MediaPipeDriveMetaHumanArmHelpers=0`.
- `mpq_shadow_report.json` has `runtime_cvar_summary.stage2_conflict_stress_settings_match=true`.
- `mpq_shadow_report.json` has `solver_snapshot_source_summary.status=runtime_anim_solver`; fallback-only Stage 2 rows do not prove the anim node applied the hint.
- `mpq_shadow_report.json` has `driven_component_summary.primary_driven_component` pointing at `BP_Kellan_C_0.Body` for the MPQ MetaHuman map, even though row names use the legacy `manny.*` namespace.
- `mpq_shadow_report.json` has no MediaPipe arm fallback/direct upper/lower/hand lift rows above `0.0`.
- MediaPipe candidate shoulders are present for at least 85% of samples.
- Fused shoulder rows are explained by calibrated MediaPipe shoulder evidence, not inferred Quest shoulder rows.
- `applied_clavicle_lift_p95` and `applied_clavicle_helper_lift_p95` remain `0.0`; Stage 2 must not drive MetaHuman clavicle or helper bones directly.
- Direct Stage 2 clamp-hit and visible-output rows are interpreted as diagnostics only, not pass criteria for avatar motion.
- Quest wrist-to-live-hand output, reported through the legacy `manny.*` namespace, does not regress materially from the prior Stage 2A proof capture.
- No rapid authority switching or fresh `RecordMannyHeadTrace` capture appears during the MPQ trial.
- `mpq_shadow_stage2_debug.csv` records both sides and includes neutral readiness, signed lift evidence, clamp-hit fraction, candidate lift, reference lift, raw delta, target lift, applied lift, helper lift, and visible output correlation.

## Fail Conditions

- MediaPipe shoulder evidence is flat, missing, inverted, or lagging badly.
- Clavicle/helper output receives non-zero direct Stage 2 applied lift.
- Quest hand output correlation drops sharply during shoulder hints.
- Hand endpoints visibly shift when only shoulder evidence is being recorded.
- MetaHuman clavicle helper bones receive non-zero Stage 2 lift.
- MediaPipe elbow/wrist/hand fallback appears.
- The analyzer reports Stage 2 direct visible output as required for pass/fail.

## Analyzer Command

After VR Preview:

```text
python Tools\AnalyzeMPQShadowFusionCapture.py "<new mpq_shadow_latency_mpq_clean_fusion_sources_no_stage_writes_*.json>" --out-dir "Saved\CodexAgent\Diagnostics\mpq_clean_fusion_sources_no_stage_writes_analysis"
```

Review:

```text
mpq_shadow_report.json
mpq_shadow_pair_metrics.csv
main_bone_movement_correlation_summary.csv
mpq_shadow_poor_area_compensation.csv
mpq_shadow_stage2_debug.csv
shoulders_standardized.png
shoulders_lag_compensated.png
arms_measure_only_lag_compensated*.png
```

## Decision

If this passes, the source-correlation layer is clean enough to implement shoulder/shrug output through the fused pose writer and avatar profile. If it fails, keep Stage 2 direct output disabled and fix source calibration, fused shoulder ownership, or analyzer labels before adding visible shoulder motion.
