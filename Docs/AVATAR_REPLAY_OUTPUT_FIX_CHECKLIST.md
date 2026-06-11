# Avatar Replay Output Fix Checklist

Status: active task checklist created 2026-06-09. Updated 2026-06-10 with the replay body-drive policy guard, BodyFusion region-quality diagnostics, and lower-body quality pass (see "2026-06-10 Replay Quality Pass" below).

Scope: fix `/Game/MetaHumanRooms/L_MetaHumanRecordedQuestMediaPipeReplay_01` so the MetaHuman follows the existing Quest + MediaPipe recording while preserving the avatar's own scale, proportions, and segment lengths. No new VR Preview or headset capture is required.

## Checklist

- [x] Read active project instructions and current docs index.
- [x] Audit replay startup, replay source injection, BodyFusion authority, lower-body writer requirements, and all-bone tracking dataset recording.
- [x] Add replay-safe deterministic output capture so PIE replay can produce an all-bone avatar-output dataset without live Quest/MediaPipe hardware.
- [x] Add replay/full-body MediaPipe authority plumbing so calibrated replay can expose pelvis, hips, knees, ankles, and feet to the fused pose writer while keeping avatar scale/proportions authoritative.
- [x] Inspect the actual runtime Kellan MetaHuman presentation actor, body mesh, 342-bone skeleton, follower components, and pose-driven writer relationship in the replay map.
- [x] Split leg IK foot-plant locking from lower-body replay policy, then kept replay on the TestingKit3-style direct avatar-length segment solve because target IK overdrives Kellan's recorded lower-body phase.
- [x] Compare TestingKit3, TestingKit3_MetaXRCompare, and TestingKit4 MediaPipe lower-body implementations against TestingKit5 before finalizing the leg/foot solve.
- [x] Run the replay map locally without VR and produce a replay-driven avatar-output dataset from the inherited recording.
- [x] Analyze before/after source-to-avatar mismatch for head, hands, arms/shoulders, torso/spine, hips/pelvis, legs, and feet.
- [x] Generate/update analysis plots that show region-level correlations and residuals.
- [x] Run Python tests.
- [x] Close Unreal/LiveCoding and run the normal `TestingKit5Editor` C++ build.
- [x] Run relevant Unreal automation.
- [x] Confirm `/Game/MetaHumanRooms/L_MetaHumanRecordedQuestMediaPipeReplay_01` still auto-loads the replay cache and drives the MetaHuman in PIE.

## Evidence To Report

- Replay source manifest path.
- Replay-output dataset path and analysis paths.
- Before/after source-to-avatar metrics by region.
- Exact compile/build/automation results.
- Any residual weak regions with concrete evidence.

## Final Replay Evidence

Final non-VR PIE replay output capture, 2026-06-09:

- Map: `/Game/MetaHumanRooms/L_MetaHumanRecordedQuestMediaPipeReplay_01`
- Replay source manifest: `Saved/CodexAgent/Diagnostics/tracking_fusion_dataset_avatar_locked_sync_calibration_20260609_170656_replay_source_manifest.json`
- Replay output manifest: `Saved/CodexAgent/Diagnostics/tracking_fusion_dataset_replay_avatar_output_tk3direct_final_20260609_210445.json`
- Analysis: `Saved/CodexAgent/Diagnostics/tracking_fusion_dataset_replay_avatar_output_tk3direct_final_20260609_210445_analysis.json`
- Correlations CSV: `Saved/CodexAgent/Diagnostics/tracking_fusion_dataset_replay_avatar_output_tk3direct_final_20260609_210445_correlations.csv`
- Calibration profile: `Saved/CodexAgent/Diagnostics/tracking_fusion_dataset_replay_avatar_output_tk3direct_final_20260609_210445_calibration_profile.json`
- Plots: `Saved/CodexAgent/Diagnostics/tracking_fusion_dataset_replay_avatar_output_tk3direct_final_20260609_210445_signal_plots`

Capture facts:

- `MediaPipeTrackingFusionDatasetReplayActor` active with 6161 replay samples, 209.992s, `timeScale=1.000`, `avatarScale=unchanged`, `avatarProportions=authoritative`.
- Output capture wrote 6282 samples at 29.911 Hz, all 342 Kellan body bones, 46 helper bones, 60 other/deform/leaf bones, 2 JSONL sample chunks, and 3 float32 bone sidecars.
- Policy preflight was `ready` with `mp.BodyFusion.Enable=1`, `mp.BodyFusion.WritePose=1`, `mp.BodyFusion.MediaPipeAuthority=2`, `mp.MediaPipeDriveSpine=1`, `mp.MediaPipeDrivePelvisTranslation=1`, `mp.MediaPipeDriveLegs=1`, `mp.MediaPipeUseLegIK=0`, `mp.MediaPipeUseLegIKFootPlant=0`, and `mp.MediaPipeDriveFootRotation=1`.

Common-row median p95 residual change from the target-IK replay run to final TestingKit3-style direct segment replay:

| Region | Target-IK p95 cm | Final p95 cm | Delta |
| --- | ---: | ---: | ---: |
| head | 0.137 | 0.116 | -0.020 cm |
| hands | 8.653 | 8.562 | -0.091 cm |
| arms | 6.706 | 6.636 | -0.070 cm |
| torso | 0.661 | 0.656 | -0.005 cm |
| hips | 0.012 | 0.012 | -0.001 cm |
| legs | 5.610 | 5.362 | -0.248 cm |
| feet | 7.652 | 7.403 | -0.249 cm |

Residual weak areas:

- Legs are policy-ready and no longer blocked by IK/foot-plant constraints, but the final all-row lower-body analysis still has weak and unstable-lag rows: legs 168 rows with median p95 5.590 cm, feet 126 rows with median p95 7.403 cm.
- Feet remain diagnostic-only in calibration readiness because 45/126 avatar feet rows are still classified as avatar mismatch despite valid MediaPipe foot source availability.

Verification:

- `python Tools\TestAnalyzeTrackingFusionDataset.py`: 19 tests passed.
- `D:\Epic\UE_5.8\Engine\Build\BatchFiles\Build.bat TestingKit5Editor Win64 Development -Project="D:\Epic\Unreal_Projects\TestingKit5\TestingKit5.uproject" -WaitMutex`: succeeded.
- `UnrealEditor-Cmd.exe ... -ExecCmds="Automation RunTests TestingKit5.MediaPipe.Diagnostics; Quit" -TestExit="Automation Test Queue Empty"`: 13 diagnostics discovered; 12 completed successfully before queued quit.
- `UnrealEditor-Cmd.exe ... -ExecCmds="Automation RunTests TestingKit5.MediaPipe.Diagnostics.TrackingFusionDatasetSuppressesDiagnosticLogs" -TestExit="Automation Test Queue Empty"`: 1 test completed successfully, covering the diagnostic missed by the immediate quit.

## Older Project Lower-Body Comparison

Checked on 2026-06-09:

- `D:\Epic\Unreal_Projects\TestingKit3\Source\MediaPipeDriver\MediaPipePoseDrivenAnimInstance_LegSolve.inl`
- `D:\Epic\Unreal_Projects\TestingKit3_MetaXRCompare\Source\MediaPipeDriver\MediaPipePoseDrivenAnimInstance_LegSolve.inl`
- `D:\Epic\Unreal_Projects\TestingKit4\TestingKit3\Source\MediaPipeDriver\MediaPipePoseDrivenAnimInstance_LegSolve.inl`
- `D:\Epic\Unreal_Projects\TestingKit3\Source\MediaPipeDriver\MediaPipeSolvedPose.cpp`
- `D:\Epic\Unreal_Projects\TestingKit3\Source\MediaPipeDriver\MediaPipeSourceConditioner.cpp`
- `D:\Epic\Unreal_Projects\TestingKit3\Source\MediaPipeDriver\MediaPipeBodySolverMath.cpp`
- matching TestingKit5 files under `Source\MediaPipeDriver\PoseDriven\Inline`, `Tracking`, and `BodyFusion`

Findings:

- TestingKit3 and TestingKit3_MetaXRCompare use the same lower-body solve: MediaPipe hip/knee/ankle/foot_index landmarks become hip-to-knee, knee-to-ankle, and ankle-to-toe vectors; those vectors are converted into component space and applied as rotations against the avatar's own reference thigh/calf/foot directions. This preserves avatar segment lengths.
- The old path also has optional leg IK and foot-plant locking, but the direct segment rotations are the core MediaPipe leg tracking path. It is not a Control Rig path and does not morph the avatar to the user's segment lengths.
- TestingKit5's coordinate conversion, `MediaPipeSolvedPose` lower-body side-rail math, and `MediaPipeBodySolverMath` are equivalent to TestingKit3. TestingKit5 adds adaptive source conditioning for live MediaPipe frames; replay source observations bypass that path.
- `D:\Epic\Unreal_Projects\TestingKit4` is a nested copy layout containing TestingKit3 source trees; no distinct TestingKit4 lower-body solver was found outside those copies.
- The replay target-IK branch added in TestingKit5 was not present in TestingKit3 and measured worse on Kellan replay output. The adopted replay policy is therefore the TestingKit3-style direct avatar-length segment solve with `mp.MediaPipeDriveLegs=1`, `mp.MediaPipeUseLegIK=0`, `mp.MediaPipeUseLegIKFootPlant=0`, and `mp.MediaPipeDriveFootRotation=1`.

## 2026-06-10 Replay Quality Pass

Regression found and fixed before new behavior:

- `ApplyAutoQuestProfile` / `ApplyStableMediaPipeRetargetProfile` (Source/MediaPipeDriver/Runtime/MediaPipeDriverRuntime.cpp) force-set `mp.MediaPipeDriveLegs=0`, `DriveSpine=0`, `DrivePelvisTranslation=0`, `UseFkRootGrounding=0`, `DriveFootRotation=0` after the replay actor applied the replay policy, freezing Kellan's lower body at the reference pose in any fresh editor session. Yesterday's runs masked this because the replay output-capture command re-applied the replay CVars at capture start.
- Fix: `ReassertTrackingFusionReplayPoseCVarsIfActive` re-asserts `ApplyReplayPoseCVars_GameThread` at the end of both profiles whenever the tracking-fusion dataset replay is active, with a log row.

New BodyFusion region-quality diagnostics (diagnostics only, no pose authority):

- `Source/MediaPipeDriver/BodyFusion/MediaPipeBodyFusionRegionQuality.h/.cpp`: per-region (head/hands/arms/shoulders/chest_spine/pelvis_hips/legs/feet) owner, source state, confidence, rolling amplitude/speed/dropouts/freshness, forward-vs-lateral depth variance ratio, and a `mayInfluence` policy flag that pins pelvis/hips/legs/feet to diagnostics-only during avatar-locked MetaHuman replay.
- CVars: `mp.BodyFusion.RegionQualityLog`, `RegionQualityLogInterval`, `RegionQualityCapture`, `RegionQualityCaptureInterval`, `RegionQualityWindowSeconds`, `RegionQualityDepthVarRatioThreshold`, `RegionQualityDepthMinAmplitudeCm` (all configurable diagnostic thresholds). Replay enables Log+Capture; JSONL lands in `Saved/CodexAgent/Diagnostics/bodyfusion_region_quality_*.jsonl`.
- Known quirk: capture file name and tracker instance are per `UEmbodiedFusionComponent`; the live Kellan anim node shares a component with the Manny stream, so Kellan rows are sparse in the JSONL while the throttled `mp.BodyFusion.RegionQuality actor=MP_LiveMetaHumanKellan ...` log rows are authoritative.

Lower-body quality changes (replay policy enables them; defaults stay off for live paths):

- `mp.MediaPipeLegKneeBackwardPoleSuppression` (replay 0.6): `MediaPipeBodySolverMath::SuppressBackwardKneePole` rotates an implausible backward knee bend plane toward forward/lateral while preserving bend magnitude (monocular front-camera depth cannot observe knee depth).
- `mp.MediaPipeFootGroundedWorldUp` (replay 1): grounded/near-floor feet build their up axis from world up instead of torso up, so squat torso tilt does not roll planted feet. Grounded |ball-ankle| dz went from ~1.2-1.3 cm median tilt to 0.0.
- FK root grounding hover fix (`UpdateFkRootGroundingCS` + `bCurrentSourceFootNearFloor` leg state): downward (hover) correction now accepts near-observed-floor source evidence instead of only the velocity-gated grounded state, grounds the lowest eligible foot, and only penetration snaps immediately. Recorded soft knees (~156 deg standing) had left the avatar hovering ~5.9 cm; lowest-ball median is now 0.8 cm = the reference grounded ball height. Trade-off: 23/1985 frames briefly dip to at most -1.7 cm before the penetration snap recovers.

Live PIE evidence (replay seek 147 s, 66 s capture covering legs/feet blocks):

- Baseline (after regression fix only): `kellan_live_pie_bone_measure_baseline_20260610_164040.json`; knee L 122.2-175.0, knee R 112.9-175.0, pelvis Z 78.5-91.4, lowest-ball median 5.86 cm (hover), 0 penetration frames.
- Final: `kellan_live_pie_bone_measure_after_grounding_20260610_173948.json`; knee L 121.1-175.0, knee R 112.9-175.0 (bend preserved), pelvis Z 73.8-91.4, lowest-ball median 0.80 cm (grounded), foot lifts preserved (L max 16.2, R max 13.1).
- Plots: `Saved/CodexAgent/Diagnostics/kellan_replay_quality_plots_20260610/` (knee angles, pelvis translation, foot-floor delta, knee-forward metrics, region ownership/confidence/depth-ratio/amplitude timelines).
- Screenshots: `Saved/CodexAgent/Screenshots/baseline_fix_squat_rear34_*.png`, `after_grounding_squat_rear34_*.png`. Note: the room's VR perf profile disables shadows, so grounded feet show no contact shadow.
- Tools: `Saved/CodexAgent/kellan_replay_bone_sampler.py` (live PIE bone sampler; do NOT move the player pawn during capture - the embodied pawn carries the live Kellan), `Tools/PlotKellanReplayMeasurements.py` (plots + --selftest).

## 2026-06-10 Limitation Fixes (applies to Manny and all MetaHumans)

All changes live in the shared anim node, solver math, and global replay policy; no per-avatar branches.

- Penetration-proof FK root grounding: `MediaPipeBodySolverMath::SmoothFkRootGroundingOffsetZ` smooths the hover correction and clamps the offset against the lowest ball bone every frame, so smoother lag can never push a foot below the reference floor. Replaces the reactive one-frame snap. Covered by `TestingKit5.MediaPipe.BodySolverMath.FkRootGroundingSmooth`. Kellan final: lowest-ball median 0.80 = grounded reference, 0 penetration frames in 824 samples; Manny final: median 0.75 (its own reference), 0 penetration frames.
- Hover eligibility widened: `bCurrentSourceFootNearFloor` (per-leg state) lets near-observed-floor feet pull the root down, fixing the 5-7 cm standing hover caused by recorded soft knees; the lowest eligible foot is grounded so lifted feet stay lifted.
- Replay evaluation render policy: `ApplyReplayPoseCVars_GameThread` restores `sg.ShadowQuality 3`, `r.ShadowQuality 5`, `r.Shadow.MaxResolution 2048`, `r.ScreenPercentage 100` so grounded feet stop looking like they hover in evidence screenshots after the Auto Quest VR perf profile turns shadows off.
- Region-quality trackers are per target actor (`TMap<FName, FMediaPipeBodyFusionRegionQualityTracker>` in `UEmbodiedFusionComponent`), producing separate `bodyfusion_region_quality_<actor>_*.jsonl` files for Kellan and Manny.
- Avatar-locked replay policy unified: `ShouldUseAvatarLockedReplay()` returns true for every avatar while the dataset replay is active (was MetaHuman-only). BodyFusion lower body is diagnostics-only for ALL replay targets; the recorded landmarks drive every avatar through the direct segment solve.
- Manny reference-avatar fix: `AMediaPipePoseDrivenSkeletalActor::Tick` now configures its OWN mesh anim instance (source actor, fusion component, retarget settings) when a presentation mesh is the driven target. Previously only the presentation (Kellan) instance was configured, so Manny's node kept class-default `bDriveLegs=false` and its legs/pelvis froze at the reference pose. Manny binds its own `DefaultEmbodiedFusionComponent` so evidence streams stay separate.
- New armed diagnostic `mp.MediaPipeReplayInputGate` (emitted while `mp.MediaPipeLegSolveDebugOnce` > 0 and replay active): one row per node per second with hasReferencePose/hasPoseFrame/driveLegs/leg-reference/bone-validity flags, so a silently frozen avatar explains itself in the log.

Final dual-avatar live PIE evidence (seek 147 s, 66 s, legs+feet blocks), `live_pie_bone_measure_<actor>_final_all_avatars_20260610_*.json`:

| Metric | Kellan | Manny |
| --- | --- | --- |
| knee L range | 121.1-175.0 deg | 121.1-175.0 deg |
| knee R range | 112.9-175.0 deg | 112.9-175.0 deg |
| pelvis Z range | 18.3 cm (73.1-91.4) | 19.3 cm (76.6-95.9, own proportions) |
| lowest-ball median / min | 0.80 / 0.80 cm | 0.75 / 0.75 cm |
| penetration frames | 0/824 | 0/824 |
| segment length drift | 0.0000 cm | 0.0000 cm |

Verification: bounded raw build succeeded; `TestAnalyzeTrackingFusionDataset.py` 20/20; `PlotKellanReplayMeasurements.py --selftest` OK; automation `TestingKit5.MediaPipe + TestingKit3.MediaPipe.BodyFusion + TestingKit3.MediaPipe.BodySolverMath` 54/54 on the final binary. Plots: `Saved/CodexAgent/Diagnostics/final_quality_plots_20260610/` (+ `manny/`). Screenshots: `Saved/CodexAgent/Screenshots/final_all_avatars_*.png`. Note: Manny is the hidden verification skeleton in the replay map; its bones are driven and measured even though the mesh is not rendered there.

## 2026-06-10 Replay Hair Rendering Fix

Long MetaHuman grooms (Maria, Payton) exploded into giant ribbons during replay evaluation. Cause: replay seeks (`mp.SeekTrackingFusionDatasetReplay`) and loop wraps teleport the head every pass; groom physics cannot survive the teleports and never re-settles. Not a hair-strands or LOD-bias issue (both were already correct). Fix: `ApplyReplayPoseCVars_GameThread` now also sets `r.HairStrands.Simulation 0` while replay evaluation is active, so strands render statically bound to the head. Verified live on Maria, then re-captured `showcase_Maria_*`, `showcase_Payton_*`, `showcase_Kellan_*`. Live VR sessions are unaffected (the CVar is only forced by the replay policy; engine default returns on editor restart).
