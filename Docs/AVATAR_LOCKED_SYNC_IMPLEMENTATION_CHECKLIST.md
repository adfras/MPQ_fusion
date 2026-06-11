# Avatar-Locked Full-Body Sync Implementation Checklist

This task is not done when plots or JSON files exist. It is done only when the measured alignment is consumed by runtime fusion, verified locally, and ready for one more high-fidelity VR Preview capture.

## Non-Negotiable Constraints

- [x] Keep high-fidelity capture enabled at the target rate; do not lower Hz as a workaround.
- [x] Preserve all-bone recording for MetaHumans, including deform, helper, leaf, and other bones.
- [x] Do not switch to selected-bone capture.
- [x] Do not scale, stretch, deform, or resize the avatar to match the user.
- [x] Keep the Proteus rule: the avatar is authoritative and the user conforms to the avatar.
- [x] Do not use Live Coding for C++ builds.

## Runtime Source Alignment

- [x] Add a runtime source-alignment model to the avatar calibration profile for fields that are actually consumed by C++.
- [x] Keep diagnostic-only fields diagnostic until a production consumer exists.
- [x] Add timestamped source history buffers for Quest HMD, Quest hands, Quest arm chains, and MediaPipe body/head/hand observations.
- [x] Sample source frames by measured lag before BodyFusion sees them.
- [x] Apply supported source timing offsets by source group before fusion.
- [x] Apply supported avatar-locked coordinate/axis corrections before fusion, without changing avatar proportions.
  - Current capture status: no stable high-confidence axis/sign correction was promoted, so the runtime field remains empty for this dataset. Synthetic adequate data promotes a safe source-to-source axis correction and C++ tests cover the before-fusion consumer; unsupported helper/avatar hints remain diagnostic-only.
- [x] Apply supported wrist/arm-chain offsets before fusion.
- [x] Apply supported head/camera anchor offsets before fusion.
- [x] Preserve source-frame timestamps and freshness metadata after alignment.
- [x] Expose logs that show which runtime alignment fields were applied and which stayed diagnostic.

## Analyzer And Profile Generation

- [x] Analyze Quest HMD, Quest hands, Quest arm chains, MediaPipe head, MediaPipe hands, MediaPipe torso, hips, legs, and feet.
- [x] Analyze avatar output bones for the same regions.
- [x] Report source-to-source correlations, best lag, motion amplitude, residuals, stale/missing spans, and readiness per region.
- [x] Generate a calibration profile with runtime-applied fields only when data quality supports them.
- [x] Keep unsupported or low-confidence findings under `diagnostic_only`.
- [x] Record why each region is ready or not ready.
- [x] Avoid hard-coded universal pass/fail constants; thresholds must be named/configurable diagnostic bands.
- [x] Include the current capture summary: sample count, effective Hz, missed scheduled samples, recorded bone count, helper count, and other-bone count.

## Signal Graphics

- [x] Generate source-to-source signal plots for head, hands, arms, torso, hips, legs, and feet.
- [x] Generate source-to-avatar signal plots for head, hands, arms, torso, hips, legs, and feet.
- [x] Show raw source, target/avatar, and lag-shifted comparison traces.
- [x] Mark stale, missing, and low-confidence spans.
- [x] Mark capture phases when available.
- [x] Include correlation, best lag, residual, and readiness labels on or beside each plot.

## Runtime Fusion Proof

- [x] Load `mp.AvatarCalibrationProfilePath` in production code.
- [x] Reject profile fields that would resize or deform the avatar.
- [x] Prove timing alignment affects the frame used by BodyFusion, not just analyzer output.
- [x] Prove wrist/arm/head corrections affect the frame used by BodyFusion.
- [x] Prove all unsupported diagnostic fields remain inactive at runtime.
- [x] Add C++ tests covering profile load, rejection, runtime field merge, and source-aligned frame selection.
- [x] Add or update Python tests covering profile generation, readiness, and plot/profile consistency.

## Local Verification Before Asking For VR

- [x] Run `python Tools\TestAnalyzeTrackingFusionDataset.py`.
- [x] Verify generated plots and profile for the latest all-bone 30 Hz capture.
- [x] Close Unreal and LiveCodingConsole before C++ build.
- [x] Run the normal Unreal editor build command.
- [x] Run relevant Unreal automation tests.
- [x] Run a local non-VR/game stress capture or equivalent runtime source-alignment smoke test.
- [x] Confirm no backlog, lag spike, or recorder throughput regression in local verification.

## Done Conditions

- [x] The runtime consumes the analyzer-supported calibration profile fields before BodyFusion.
- [x] The profile generated from the capture contains non-empty runtime fields when the capture supports them.
- [x] A before/after analysis shows improved source sync or avatar-follow metrics for supported regions.
- [x] Hands, arms, and head alignment are proven if the capture contains enough motion.
  - Current capture status: Quest HMD/head, Quest hands, and Quest arm chains have runtime source-timing readiness; avatar-output head rows are still not-ready due insufficient avatar head motion in this capture.
- [x] Torso, hips, legs, and feet are either proven or explicitly marked not-ready with a movement/data reason.
- [x] The final report lists exact files, exact commands run, exact pass/fail results, and any remaining reason a new VR Preview is needed.
