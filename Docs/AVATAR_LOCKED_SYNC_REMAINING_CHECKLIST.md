# Avatar-Locked Sync Remaining Checklist

The previous pass is not the finish line. It added runtime Quest timing alignment before BodyFusion, but the full end-to-end correlation goal still has gaps. This checklist must be completed before the thread can claim done again.

## Completion Rule

- [x] Do not mark the goal complete until every item in this file is checked.
- [x] Do not ask the user for another VR Preview until this checklist is complete and local verification passes.
- [x] Do not lower capture Hz.
- [x] Do not drop any MetaHuman bones, including deform, helper, leaf, or other bones.
- [x] Do not switch to selected-bone capture.
- [x] Do not resize, scale, stretch, or deform the avatar to match the user.
- [x] Preserve the Proteus rule: the chosen avatar is authoritative and the user conforms to it.

## Fix Runtime Timestamp Alignment

- [x] Add real source timestamps to Quest HMD observations when the XR pose is read.
- [x] Add real per-side source timestamps to Quest hand observations when hand tracking is read.
- [x] Preserve those Quest HMD/hand timestamps through generic tracking snapshots.
- [x] Update `FMediaPipeTrackingSourceFrameBuilder` so HMD and hand samples use their source timestamps instead of always stamping `NowSeconds`.
- [x] Keep MediaPipe body pose and Quest arm-chain timestamps intact.
- [x] Add C++ tests proving held or older HMD/hand samples do not get re-stamped as current.
- [x] Add C++ tests proving runtime source alignment selects by source timestamp where available, not only by frame time.

## Finish Safe Coordinate And Axis Alignment

- [x] Define a runtime-safe profile schema for source coordinate/axis corrections, separate from `diagnostic_only`.
- [x] Promote only source-to-source coordinate/axis findings that are supported by stable, high-confidence rows.
- [x] Keep avatar-helper, helper/deform, and source-to-avatar axis hints diagnostic-only unless there is a production consumer and tests.
- [x] Apply supported coordinate/axis corrections before BodyFusion, to source observations only.
- [x] Do not apply coordinate/axis corrections by changing avatar proportions, avatar scale, or MetaHuman body shape.
- [x] Add C++ tests proving a promoted axis/sign correction changes the BodyFusion input source frame.
- [x] Add C++ tests proving diagnostic-only axis/sign suggestions are ignored at runtime.

## Make Runtime Fields Non-Empty Where Data Supports Them

- [x] Investigate why `head_camera_anchor_offset_cm` is still `[0, 0, 0]`.
- [x] If the capture supports a head/camera anchor correction, generate a non-zero runtime field and prove it changes runtime camera anchoring.
- [x] Investigate why `wrist_arm_chain_offsets_cm` is still `{}`.
- [x] If the capture supports wrist/arm-chain offsets, generate non-empty runtime fields and prove they change the BodyFusion input.
- [x] Investigate why `bone_map_corrections` is still `{}`.
- [x] Keep empty runtime fields only when the analyzer gives an explicit data-quality reason.

## MediaPipe Body, Hips, Legs, And Feet

- [x] Keep MediaPipe torso, hips, legs, and feet in analyzer coverage.
- [x] Identify whether current lower-body not-ready status is caused by insufficient movement, source quality, coordinate mismatch, or avatar output mismatch.
- [x] If current data is sufficient after coordinate/axis correction, promote safe lower-body runtime alignment fields.
- [x] If current data is not sufficient, produce exact future capture movement phases needed for torso, hips, legs, and feet.
- [x] Do not hide lower-body failure by marking it complete without a data reason.

## Analyzer And Graphics

- [x] Regenerate `_analysis.json`, `_correlations.csv`, `_calibration_profile.json`, and `_signal_plots/`.
- [x] Signal plots must show raw source, target/avatar, lag-shifted overlay, missing/stale spans, phase boundaries, best lag, correlation, residual, and readiness.
- [x] Include source-to-source and source-to-avatar plots for head, hands, arms, torso, hips, legs, and feet.
- [x] Include before/after estimates for each runtime-applied correction.
- [x] The generated profile must list runtime-applied fields separately from `diagnostic_only`.

## Verification

- [x] Run `python Tools\TestAnalyzeTrackingFusionDataset.py`.
- [x] Close Unreal and LiveCodingConsole before C++ build.
- [x] Run the normal build command:
  `D:\Epic\UE_5.8\Engine\Build\BatchFiles\Build.bat TestingKit5Editor Win64 Development -Project="D:\Epic\Unreal_Projects\TestingKit5\TestingKit5.uproject" -WaitMutex`
- [x] Run relevant Unreal automation tests and confirm exit code `0`.
- [x] Run a local non-VR runtime alignment smoke or stress test that exercises the loaded profile.
- [x] Confirm logs show runtime alignment fields applied before BodyFusion.
- [x] Confirm no recorder throughput regression and no all-bone capture fidelity regression.
- [x] Confirm no `UnrealEditor.exe` or `LiveCodingConsole.exe` remains running afterward.

## Final Report Requirements

- [x] Report exact files changed.
- [x] Report exact generated artifact paths.
- [x] Report exact commands run and pass/fail results.
- [x] Report runtime profile fields that are non-empty and applied.
- [x] Report fields left diagnostic-only and why.
- [x] Report torso/hips/legs/feet status with explicit data reasons.
- [x] State whether another VR Preview is actually needed, and if so, exactly what movements it must include.

## Completion Evidence

- Python analyzer tests: `python Tools\TestAnalyzeTrackingFusionDataset.py`, exit code `0`, `15` tests passed.
- Validated capture analyzer runs: `--no-plots` and full plot run both exit code `0`; full run produced `2555` samples, `28.385 Hz`, `145` missed scheduled samples, `342` recorded bones, `46` helpers, `60` other bones, `6936` correlation rows, and `281` PNGs.
- C++ build: normal closed-editor UBT command exit code `0`; no Live Coding used.
- Unreal automation: `TestingKit5.MediaPipe.Diagnostics`, `MediaPipe.TrackingSourceFrameBuilder`, `TestingKit3.MediaPipe.BodyFusion`, and direct `TestingKit3.MediaPipe.BodyFusion.UpperBodyFollowAlpha` all exit code `0`.
- Loaded-profile runtime smoke log: `mp.AvatarAlignmentSmoke: loadedProfile=1 beforeBodyFusion=1 timingOffsets=quest_hmd,quest_hands,quest_arm_chains,mediapipe_body_pose coordCorrections=quest_hands sourceFrameChanged=1 avatarScaleChanged=0`.
- Generated runtime-applied field for the current capture: `source_alignment.timing_offsets_seconds_by_source` with `quest_hmd=0.224212`, `quest_hands=0.213462`, `quest_arm_chains=0.140209`.
- Current capture not-ready fields are documented in `_calibration_profile.json`: coordinate corrections have no stable high-confidence axis sign; head anchor is insufficient-motion; wrist offsets have no consistent non-zero offset estimate; bone-map corrections remain diagnostic-only for helper/parent-chain review.
- Current torso/hips/legs/feet status is `not_ready`. Raw MediaPipe body landmarks are present at about `99.96%` availability, so this is not a source-missing result. The analyzer now separates raw source availability/motion from avatar output policy and marks torso/hips/legs/feet as avatar-output-constrained by the current Quest embodied policy/CVars when appropriate.
- New missing-data capture workflow: `mp.PrepareAvatarLockedSyncCalibrationCapture` arms one VR Preview for seven green 30-second movement blocks at `30 Hz`, `boneMode=all`, with Quest wrist/arm yellow calibration HUDs suppressed and all-bone JSONL/binary sidecar recording preserved.
