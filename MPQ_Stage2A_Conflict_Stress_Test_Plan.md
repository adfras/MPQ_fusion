# MPQ Stage 2A Conflict Stress Test Plan

## Purpose

Verify that Stage 2A MediaPipe shoulder/clavicle vertical hints improve visible clavicle lift without fighting Quest-owned arms, wrists, hands, fingers, or HMD head authority.

Stage 2A is not arm fusion. Arm fallback remains out of scope.

## Setup

Use map:

```text
/Game/MetaHumanRooms/L_MetaHumanPreviewRoom_MPQSignalCompare_01
```

Before VR Preview, arm a shadow-only Stage 2A capture:

```text
mp.RecordMannyHeadOnPlay 0
mp.PrepareMPQShadowLatencyTrial maxdim=384 duration=60 prediction=1 maxPredictionMs=50 label=stage2a_conflict_stress stage1=1 blend=0.5 halfLife=0.04 stage2=1 stage2Blend=0.2 stage2Scale=4.5 stage2MaxLiftCm=5 stage2HalfLife=0.04 analyze=0
```

Required log proof before pressing VR Preview:

```text
mp.MPQShadowAutoStart: armed ... shadowOnly=1 authority=0 writePose=0 stage1TorsoPelvisHint=1 stage2ShoulderClavicleHint=1 armFallbacks=off
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

Primary Stage 2A rows:

```text
mp_candidate.left.shoulder_lift_from_pelvis  -> manny.clavicle_l_world_lift_from_pelvis
mp_candidate.right.shoulder_lift_from_pelvis -> manny.clavicle_r_world_lift_from_pelvis
```

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
- Runtime CVars show `WritePose=0`, `MediaPipeAuthority=0`, `Stage1TorsoPelvisHint=1`, `Stage2ShoulderClavicleHint=1`, and `Stage2ShoulderClavicleResponseScale=4.5`.
- MediaPipe candidate shoulders are present for at least 85% of samples.
- Stage 2A clavicle output rows pass standardized and stage gates.
- Stage 2A clavicle output best lag stays at or below 100ms.
- Stage 2A output remains bounded by `stage2MaxLiftCm`; no visible over-pull or snapping.
- Quest wrist-to-Manny-hand output does not regress materially from the prior Stage 2A capture.
- No rapid authority switching or fresh `RecordMannyHeadTrace` capture appears during the MPQ trial.

## Fail Conditions

- Stage 2A clavicle rows are flat, missing, inverted, or lag over 100ms.
- Clavicle output moves when MediaPipe shoulder candidate is stale or unavailable.
- Quest hand output correlation drops sharply during shoulder hints.
- Hand endpoints visibly shift when only shoulder/clavicle hints should be moving.
- Fused Quest shoulder rows no longer match Quest shoulders.
- The analyzer reports Stage 2A active rows as measurement-only or Stage 0 flat output.

## Analyzer Command

After VR Preview:

```text
python Tools\AnalyzeMPQShadowFusionCapture.py "<new mpq_shadow_latency_stage2a_conflict_stress_*.json>" --out-dir "Saved\CodexAgent\Diagnostics\stage2a_conflict_stress_analysis"
```

Review:

```text
mpq_shadow_pair_metrics.csv
main_bone_movement_correlation_summary.csv
mpq_shadow_poor_area_compensation.csv
shoulders_standardized.png
shoulders_lag_compensated.png
arms_measure_only_lag_compensated*.png
```

## Decision

If this passes, Stage 2A can remain enabled for controlled testing and the next stage can consider a more expressive shoulder/clavicle posture hint. If it fails, keep Stage 2A default-off and fix either the contradiction gate, clavicle smoothing, or analyzer ownership labels before expanding fusion.
