# Avatar Replay Output Fix Checklist

Status: active task checklist created 2026-06-09. Updated 2026-06-10 with the replay body-drive policy guard, BodyFusion region-quality diagnostics, and lower-body quality pass (see "2026-06-10 Replay Quality Pass" below). Updated 2026-06-12 with the lower-body scaffold pass (MediaPipe leg intent + Quest/HMD metric correction; see "2026-06-12 Lower-Body Scaffold Pass" below).

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
- Tools: `Tools/kellan_replay_bone_sampler.py` (live PIE bone sampler; do NOT move the player pawn during capture - the embodied pawn carries the live Kellan), `Tools/PlotKellanReplayMeasurements.py` (plots + --selftest).

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

## 2026-06-12 Lower-Body Scaffold Pass

Goal: stop treating monocular MediaPipe leg landmarks as authoritative metric 3D. The replay
solve now runs MediaPipe leg motion intent -> Quest/HMD metric scaffold -> avatar-proportion
FK solve with law-of-cosines reachable limits, shared by every avatar.

Why: the recorded MediaPipe landmark frame is hip-centered (hip Z is exactly 0 for the whole
canonical recording), so squat depth could only be inferred from the noisy monocular
ankle-to-hip distance, which squishes/distorts the legs. The same recording carries a fresh
Quest HMD pose on ~100% of samples with metric height (about 167 cm standing, dipping to
137-150 cm during the hips/torso/legs squat blocks) that the lower body never used.

Architecture (all shared code paths; no per-avatar branches; no bone scaling):

- `MediaPipeBodySolverMath::UpdateHmdHeightScaffold`: rolling-window standing baseline of the
  HMD height (per-slot maxima, so squats cannot drag the baseline down and toe-raise inflation
  expires with the window), lean compensation from the raw MediaPipe torso pitch (a forward
  lean is not a squat), and a dimensionless compression alpha with ramp-in confidence.
- `MediaPipeBodySolverMath::ComputeFusedPelvisCompression`: monocular compression (squat/stand
  timing intent) blended with the HMD metric compression by weight x confidence.
  `DrivePelvisTranslationCS` consumes the fused alpha for the pelvis offset and stores the
  scaffold sample on `FMediaPipeBodySolverState` for the legs and diagnostics.
- `MediaPipeBodySolverMath::AdjustGroundedLegFlexion` (called from `DriveLegCS` after knee-pole
  suppression): for feet at/near their observed source floor, solves on the avatar's OWN
  thigh/calf lengths (law of cosines, clamped to the avatar's reference reach and minimum
  reach) the knee flexion that realizes the fused pelvis drop, then rotates the MediaPipe
  segment directions inside their own measured bend plane by a bounded delta
  (`mp.MediaPipeLegScaffoldFlexionMaxAdjustDeg`, default 25 deg). Straightening deltas are
  damped (0.35) so recorded soft knees are nudged, never locked. Lifted feet are never touched,
  so steps/kicks keep their recorded timing, phase, and amplitude.

New CVars (defaults keep live paths unchanged; the replay policy enables them):
`mp.MediaPipeLegScaffoldHmdWeight` (replay 0.85), `mp.MediaPipeLegScaffoldFlexionWeight`
(replay 0.6), `mp.MediaPipeLegScaffoldFlexionMaxAdjustDeg` (25),
`mp.MediaPipeLegScaffoldHipFromHmdRatio` (0.52), `mp.MediaPipeLegScaffoldLeanCoefficient`
(0.35), `mp.MediaPipeLegScaffoldBaselineWindowSeconds` (45), `mp.MediaPipeLegScaffoldLog`
(replay 1), `mp.MediaPipeLegScaffoldLogInterval` (2 s).

Diagnostics: throttled `mp.MediaPipeLegScaffold` rows per driven actor show every source
contribution: hmd(valid/z/baseline/dropCm/leanCm/alpha/conf), mono(alpha), fused(alpha,
hmdShare, pelvisDropCm), pelvis offset Z, FK root grounding Z, and per leg
flexMeas/flexTarget/flexApplied/grounded/nearFloor/liftCm/plantLock. The armed
`mp.MediaPipeLegSolveDebugOnce` row also carries the scaffold values per side.

### 2026-06-12 Wrist/Finger Replay (schema-v2 cache)

User report verified: replay had no wrist rotation or finger motion. Root cause was twofold:
the v1 replay cache (`..._replay_source.jsonl`) reduced each hand to a single `wrist_world`
endpoint (driving arm placement only), and the anim node's dataset-replay branch never
populated `QuestHands`, so the wrist-rotation/finger solvers were inert. The ORIGINAL full
dataset, however, recorded the complete Quest hand skeletons all along:
`fusion.best_available.{left,right}_upper_limb.hand_joints` = 26 keypoint positions +
[x,y,z,w] rotations per hand, tracked on 93-100% of all 6162 samples. No new VR capture was
needed.

Changes:

- `Tools/BuildTrackingFusionReplayCache.py` (schema_version 2): copies the recorded hand
  joints into `fusion.source.{left,right}_hand` as `keypoints_world` / `keypoint_quats` /
  `keypoints_tracked`. New cache built beside the canonical v1 files:
  `..._replay_source_v2.jsonl` + `..._replay_source_v2_manifest.json` (v1 untouched).
- `FMediaPipeTrackingHandSourceSnapshot` gained `bLeftHasFullKeypoints/bRightHasFullKeypoints`
  so consumers can tell full skeletons from wrist-only placeholders; the replay loader parses
  the v2 keypoint arrays (backward compatible with v1).
- The anim node replay branch now builds `QuestHands` from full-keypoint replay observations
  (re-timestamped to playback time), so `DriveQuestHandCS` (wrist rotation) and
  `DriveQuestFingerBonesCS` (fingers) run during dataset replay. The ARM placement solve is
  unaffected: with BodyFusion pose writes active the Quest-wrist arm fallbacks stay disabled
  and the recorded arm chain keeps owning shoulder/elbow/wrist. An armed static hand pose
  (`mp.QuestHandReplayFile <name>` + `mp.QuestHandReplay 1`) still overrides for solver tests.
- Replay policy additions: `mp.QuestHandTracking 1`, `mp.QuestHandDriveFingerBones 1`.
- `AMediaPipeTrackingFusionDatasetReplayActor` default manifest now points at the v2 cache.

Verification (build succeeded; automation 117/117 after fixing the synthetic loader test to
keep `wrist_world` consistent with keypoint index 1 = EHandKeypoint::Wrist):

- `Tools/kellan_replay_hand_sampler.py` (new), hands block seek 32 + 26 s, 780 samples:
  middle-finger curl 6.3-118.2 deg (L) / 10.7-121.0 deg (R), index curl ~104 deg range both
  sides, wrist flexion range 45.3 deg (L) / 36.8 deg (R) - full open-to-fist articulation
  replayed from the recording (previously all flat-zero ranges).
- Legs invariant guard: 66 s leg capture on the v2 cache with hands enabled vs the previous
  verified run = `compare_replay_measurements.py` PASS (knee ranges within ~1 deg, ball median
  exact, 0 penetration, 0.0 segment drift) - enabling hands changed nothing below the waist.
- Screenshots: `handshot_fist_40.png` (fingers curled, t~40.4), `handshot_open_45.png`
  (hands flat/extended, t~45.0), captured with `Tools/aim_hand_camera.py`.

### 2026-06-12 Follow-up: HMD squat authority, bend redistribution, flat grounded feet

User-reported issues verified and fixed on top of the scaffold pass:

1. "Knee looks too low / thigh disproportionately long." Bone proportions measured EXACT
   (thigh 41.19 / calf 40.18 cm on Kellan, constant across all frames, identical to baseline),
   but the squat bend distribution was wrong: front-facing monocular capture cannot observe the
   femur's forward (depth) rotation, so the bend lands mostly in the shin (measured at the squat
   bottom: femur ~28 deg from vertical, shin ~44 deg - a natural squat is roughly the reverse)
   and the knee sinks (knee height fraction 0.44 at the bottom vs 0.49 standing).
   Fix: `MediaPipeBodySolverMath::RedistributeGroundedLegBend` - rigid in-bend-plane rotation of
   the thigh+calf pair so the shin keeps at most `mp.MediaPipeLegScaffoldShinTiltShare` (0.35)
   of the total flexion. Flexion magnitude, bend plane, and timing stay owned by MediaPipe;
   one-sided (a natural split is never disturbed); bounded by
   `mp.MediaPipeLegScaffoldBendRedistributionMaxDeg` (20). Replay weight 0.8.

2. "Can't the squat be determined by the Quest headset?" Yes - replay policy now sets
   `mp.MediaPipeLegScaffoldHmdWeight` 1.0 (was 0.85): the HMD height is the squat-depth
   authority; monocular compression only covers HMD dropouts and the confidence ramp-in.
   MediaPipe keeps owning timing, phase, momentum, lateral swing, bend plane, and lifted-foot
   motion. Realization strengthened: `FlexionWeight` 0.8 (was 0.6), `FlexionMaxAdjustDeg` 40
   (was 25) so deep metric squats are actually reached.

3. "Feet are not flat - the ankle pushes the feet upward." Confirmed and root-caused: the
   reference foot basis is built from the naturally down-sloped ankle->ball axis, while the
   grounded-foot path planarizes the target forward to horizontal - the basis mapping therefore
   pitched every driven grounded foot ~26 deg toe-up, sinking the ankle to ball height (measured:
   ALL grounded driven frames had ankle Z == ball Z; pre-existing in the 06-10 baseline too, and
   it silently exaggerated squat depth ~7 cm).
   Fix: `MediaPipeBodySolverMath::SolveGroundedFootPitch` - grounded feet keep the solved
   heading/roll but rebuild pitch from the monocular axis clamped to
   [reference slope - `mp.MediaPipeFootGroundedMaxExtraDownPitchDeg` (30), reference slope]:
   soles stay flat, heel raises/toe stands keep their downward pitch, lifted feet keep raw
   monocular pitch. Enabled by `mp.MediaPipeFootGroundedPitchClamp` (replay 1).

#### 2026-06-12 Follow-up Verification

Build succeeded; `Automation RunTests TestingKit5.MediaPipe` = 117/117 (2 new:
`BodySolverMath.GroundedLegBendRedistribution`, `BodySolverMath.GroundedFootPitch`).
`Docs/CVAR_REFERENCE.md` regenerated (427 CVars).

Live PIE measurement (seek 147, 66 s, 1982 samples at 30 Hz,
`live_pie_bone_measure_MP_LiveMetaHumanKellan_after_followup2_*.json` vs the 06-10 baseline;
note `after_followup_*` files are from an intermediate axis-pitch build, superseded):

- Flat feet: grounded foot pitch delta vs the natural reference slope is 0.0 deg
  median/p95/worst on BOTH feet across ~1750 grounded frames (baseline: +26.2 deg toe-up on
  every grounded driven frame - ankle sunk to ball height). At the squat bottom the ankle now
  sits 7.9-8.5 cm above the ball.
- Natural squat distribution: knee height fraction at the squat bottom 0.569/0.577 (was 0.44;
  natural squats are ~0.55+), femur tilt 53 deg vs shin 35 deg (was femur 10-28 / shin 37-44).
- Metric depth, HMD-authoritative: fused alpha == HMD alpha (hmdShare 1.00); sustained knee
  bend p05 130.6 deg (06-10 baseline 145.5), knee stdev 6.8 -> 11.8 (more dynamic legs),
  flexApplied reaches 38-40 deg at deep squats. Standing returns to knee fraction 0.493 = the
  exact reference standing value with pelvis at 90.6 (rig 91.35) - no more permanent soft-knee
  crouch at metric standing.
- Invariants: proportions exact (thigh 41.19 / calf 40.18, drift 0.0000), knee max 175.0
  unchanged, foot lifts preserved (15.0/15.9 cm), knee forward of ball max 10.72 cm (baseline
  9.62), grounded-ball planar slide p95 53.8 vs 45.7 cm/s (no systemic skating; the single max
  spike is the seek transition frame).

Screenshots (with paired [LegCam] joint logs): `legshot_fixed_squat_141.png` (deep squat:
femur-dominant bend, knee at natural height, soles flat), `legshot_fixed_kick_170.png`
(lifted-leg kick preserved, planted foot flat), `legshot_fixed_standing_152.png` (standing at
reference extension, feet flat). Leg-camera tooling: `Tools/aim_leg_camera.py`,
`Tools/check_foot_pitch.py`, `Tools/check_leg_proportions.py`, `Tools/find_leg_motion_moments.py`,
`Tools/find_squat_windows.py`, `Tools/summarize_replay_motion.py`, `Tools/dump_foot_bones.py`.

### 2026-06-12 Verification

Build/tests: `Tools\BuildTestingKit5EditorFast.ps1` succeeded (42 actions);
`Automation RunTests TestingKit5.MediaPipe` = 115/115 success (112 prior + 3 new
`BodySolverMath.HmdHeightScaffold` / `FusedPelvisCompression` / `GroundedLegFlexion`);
`TestAnalyzeTrackingFusionDataset.py` 20/20; `PlotKellanReplayMeasurements.py --selftest` OK.
`Docs/CVAR_REFERENCE.md` regenerated (422 CVars).

Live PIE measurement (seek 147, 66 s legs+feet blocks, 1980 samples at 30 Hz,
`live_pie_bone_measure_<actor>_after_scaffold_20260612_090810.json` vs the 06-10
`final_all_avatars` baselines, `Tools/compare_replay_measurements.py` +
`Tools/summarize_replay_motion.py`):

| Metric | Kellan 06-10 | Kellan 06-12 | Manny 06-10 | Manny 06-12 |
| --- | ---: | ---: | ---: | ---: |
| pelvis Z range (cm) | 18.28 | 19.19 | 19.28 | 20.19 |
| knee p05 (deg, sustained deep bend) | 145.5 | 137.1 | 145.5 | 137.1 |
| knee p01 / min (single-frame extreme) | 122.2 / 121.1 | 128.6 / 127.7 | 122.2 / 121.1 | 128.5 / 127.7 |
| knee max (extension preserved) | 174.96 | 174.96 | 175.00 | 175.00 |
| foot lift max L / R (cm) | 16.5 / 14.1 | 18.5 / 11.9 | 17.3 / 14.8 | 19.6 / 12.4 |
| knee forward of ball max (cm) | 9.62 | 9.65 | 10.05 | 10.09 |
| lowest-ball median (cm) | 0.80 | 0.81 | 0.75 | 0.76 |
| penetration frames | 0 | 0/1980 | 0 | 0/1980 |
| segment length drift (cm) | 0.0 | 0.0000 | 0.0 | 0.0000 |

Interpretation: sustained squat depth is deeper and metric (pelvis range up ~1 cm, knee p05
8.4 deg deeper) while the single-frame monocular depth-noise extremes are suppressed (p01/min
about 7 deg shallower) - the intended swap of noisy mono depth for HMD metric depth. The
equivalence gate `compare_replay_measurements.py` reports those knee-min deltas as FAIL by
design (it asserts equivalence; this pass is a deliberate behavior change); the invariants it
also checks all hold: knee max identical, grounded ball median within 0.01 cm, zero
penetration, zero proportion drift. Both avatars show identical solve behavior on their own
proportions (Kellan pelvis max 91.35, Manny 95.90).

Scaffold source-contribution evidence (log rows during the 141 s deep squat, full-speed
playback, baseline learned standing 168.5):
`hmd(z=146.0 base=168.5 dropCm=22.5 leanCm=2.8 alpha=0.774 conf=1.00) mono(alpha=0.896)
fused(alpha=0.793 hmdShare=0.85 pelvisDropCm=18.3) ... flexMeas=21.6 flexTarget=78.5
flexApplied=25.0` - monocular depth saw only a ~10 cm squat while the HMD measured 22.5 cm;
the fused metric target drives the grounded flexion correction at its full clamp, and
`fkRootZ` collapses to about +-1 cm at the squat bottom (pelvis target and leg chain agree,
so root grounding no longer fights the squat).

Screenshots: `Saved/CodexAgent/Screenshots/after_scaffold_squat_kellan.png` (standing, soft
knees, grounded), `after_scaffold_deep_squat_kellan.png` (deep squat, frozen at 141.4 s),
`after_scaffold_fullspeed_squat_kellan.png` (mid-squat at full speed).

Known limitation: a replay seek resets solver continuity including the HMD baseline window;
if the seek lands mid-squat the scaffold under-corrects until the user stands once (rolling
max re-learns immediately on standing). Confidence ramps back to 1.0 over ~15 s of valid HMD
samples after any reset.

## 2026-06-10 Replay Hair Rendering Fix

Long MetaHuman grooms (Maria, Payton) exploded into giant ribbons during replay evaluation. Cause: replay seeks (`mp.SeekTrackingFusionDatasetReplay`) and loop wraps teleport the head every pass; groom physics cannot survive the teleports and never re-settles. Not a hair-strands or LOD-bias issue (both were already correct). Fix: `ApplyReplayPoseCVars_GameThread` now also sets `r.HairStrands.Simulation 0` while replay evaluation is active, so strands render statically bound to the head. Verified live on Maria, then re-captured `showcase_Maria_*`, `showcase_Payton_*`, `showcase_Kellan_*`. Live VR sessions are unaffected (the CVar is only forced by the replay policy; engine default returns on editor restart).
