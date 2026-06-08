# MPQ Stage 2 Shoulder/Clavicle Conflict Stress Test Plan

## Purpose

Verify that the Stage 2 MediaPipe shoulder/clavicle vertical hint improves visible clavicle lift without fighting Quest-owned arms, wrists, hands, fingers, or HMD head authority.

Stage 2 is not arm fusion. Arm fallback remains out of scope.

Current status: Stage 2A/order-fix proof is complete. The remaining Stage 2 validation item is a clean user-run conflict-stress VR capture using the command below. Do not change runtime behavior unless that capture fails and the failure points to a direct Stage 2 bug.

## Setup

Use map:

```text
/Game/MetaHumanRooms/L_MetaHumanPreviewRoom_MPQSignalCompare_01
```

Before VR Preview, arm a shadow-only Stage 2 capture:

```text
mp.RecordMannyHeadOnPlay 0
mp.PrepareMPQShadowLatencyTrial maxdim=384 duration=60 prediction=1 maxPredictionMs=50 label=stage2_conflict_stress_final stage1=1 blend=0.5 halfLife=0.04 stage2=1 stage2Blend=0.2 stage2Scale=4.5 stage2MaxLiftCm=5 stage2HalfLife=0.04 analyze=0
```

Required log proof before pressing VR Preview:

```text
mp.MPQShadowAutoStart: armed ... shadowOnly=1 authority=0 writePose=0 stage1TorsoPelvisHint=1 stage2ShoulderClavicleHint=1 armFallbacks=off
```

The capture report should also show these runtime CVars:

```text
mp.BodyFusion.WritePose=0
mp.BodyFusion.MediaPipeAuthority=0
mp.BodyFusion.Stage1TorsoPelvisHint=1
mp.BodyFusion.Stage1TorsoPelvisHintBlend=0.5
mp.BodyFusion.Stage1TorsoPelvisHintHalfLife=0.04
mp.BodyFusion.Stage2ShoulderClavicleHint=1
mp.BodyFusion.Stage2ShoulderClavicleHintBlend=0.2
mp.BodyFusion.Stage2ShoulderClavicleResponseScale=4.5
mp.BodyFusion.Stage2ShoulderClavicleMaxLiftCm=5
mp.BodyFusion.Stage2ShoulderClavicleHalfLife=0.04
```

## VR Movement Script

Use clear separated movements so the analyzer can tell which system owns which body region.

1. 0-5s: stillness, arms relaxed.
2. 5-15s: shoulder shrugs only, hands quiet.
3. 15-25s: left arm forward reach while lightly shrugging.
4. 25-35s: right arm forward reach while lightly shrugging.
5. 35-45s: both arms raise/lower to chest height while shoulders shrug.
6. 45-55s: wrist rotations and hand movement with shoulders quiet.
7. 55-60s: stillness, arms relaxed.

Avoid deliberate Quest controller occlusion unless the goal is an occlusion-specific test.

## Signals To Check

Primary Stage 2 shoulder/clavicle rows:

```text
mp_candidate.left.shoulder_lift_from_pelvis  -> manny.clavicle_l_world_lift_from_pelvis
mp_candidate.right.shoulder_lift_from_pelvis -> manny.clavicle_r_world_lift_from_pelvis
```

`manny.*` is the analyzer's legacy live-bone signal namespace. It does not mean the driven avatar is Manny. Confirm the actual driven mesh in `mpq_shadow_report.json` at `driven_component_summary.primary_driven_component`; for the MPQ map proof this should be the MetaHuman body component `BP_Kellan_C_0.Body`.

MediaPipe shoulder quality rows:

```text
mp_world_unreal.left_shoulder_lift_from_hips  -> mp_candidate.left.shoulder_lift_from_pelvis
mp_world_unreal.right_shoulder_lift_from_hips -> mp_candidate.right.shoulder_lift_from_pelvis
```

Quest ownership protection rows:

```text
quest.left.wrist.*  -> manny.hand_l.*
quest.right.wrist.* -> manny.hand_r.*
quest.left.shoulder.z  -> fused.left.shoulder.z
quest.right.shoulder.z -> fused.right.shoulder.z
```

Measurement-only conflict rows:

```text
quest.*.shoulder.z -> mp_body.*_shoulder.z
quest.*.elbow.*    -> mp_body.*_elbow.*
quest.*.wrist.*    -> mp_body.*_wrist.*
```

## Pass Criteria

- Capture is a real MPQ file under `Saved/CodexAgent/Diagnostics/mpq_shadow_latency_*.json`.
- Runtime CVars match the setup block, including `WritePose=0`, `MediaPipeAuthority=0`, Stage 1 enabled, `Stage2ShoulderClavicleHint=1`, `Stage2ShoulderClavicleHintBlend=0.2`, `Stage2ShoulderClavicleResponseScale=4.5`, `Stage2ShoulderClavicleMaxLiftCm=5`, and `Stage2ShoulderClavicleHalfLife=0.04`.
- `mpq_shadow_report.json` has `runtime_cvar_summary.stage2_conflict_stress_settings_match=true`.
- `mpq_shadow_report.json` has `solver_snapshot_source_summary.status=runtime_anim_solver`; fallback-only Stage 2 rows do not prove the anim node applied the hint.
- `mpq_shadow_report.json` has `driven_component_summary.primary_driven_component` pointing at `BP_Kellan_C_0.Body` for the MPQ MetaHuman map, even though row names use the legacy `manny.*` namespace.
- `mpq_shadow_report.json` has `stage2_conflict_readiness_summary.stage2_visible_output_fail_pairs=[]`.
- MediaPipe candidate shoulders are present for at least 85% of samples.
- Stage 2 clavicle output rows pass standardized and stage gates.
- Stage 2 clavicle output best lag stays at or below 100ms.
- Stage 2 output remains bounded by `stage2MaxLiftCm`; no visible over-pull or snapping.
- Quest wrist-to-live-hand output, reported through the legacy `manny.*` namespace, does not regress materially from the prior Stage 2A proof capture.
- No rapid authority switching or fresh `RecordMannyHeadTrace` capture appears during the MPQ trial.
- `mpq_shadow_stage2_debug.csv` records both sides and includes candidate lift, reference lift, raw delta, positive target lift, contradiction delta, smoothed lift, pre-solve lift, target lift, applied lift, and visible output correlation.

## Fail Conditions

- Stage 2 clavicle rows are flat, missing, inverted, or lag over 100ms.
- Clavicle output moves when MediaPipe shoulder candidate is stale or unavailable.
- Quest hand output correlation drops sharply during shoulder hints.
- Hand endpoints visibly shift when only shoulder/clavicle hints should be moving.
- Fused Quest shoulder rows no longer match Quest shoulders.
- The analyzer reports Stage 2 active rows as measurement-only or Stage 0 flat output.

## Analyzer Command

After VR Preview:

```text
python Tools\AnalyzeMPQShadowFusionCapture.py "<new mpq_shadow_latency_stage2_conflict_stress_final_*.json>" --out-dir "Saved\CodexAgent\Diagnostics\stage2_conflict_stress_final_analysis"
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

If this passes, Stage 2 shoulder/clavicle hinting can remain enabled for controlled testing and the next stage can consider a more expressive shoulder/clavicle posture hint. If it fails, keep Stage 2 default-off and fix either the contradiction gate, clavicle smoothing, or analyzer ownership labels before expanding fusion.
