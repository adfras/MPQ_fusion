# TestingKit3 MediaPipe Pipeline Walkthrough

Last updated: 2026-05-20

This document describes the current TestingKit3 MediaPipe implementation: how video frames become MediaPipe landmarks, how those landmarks are conditioned and converted into Unreal space, and how the final pose drives a Manny-like skeletal mesh.

The current shoulder state is the frozen baseline captured in `Archive/MediaPipe_Shoulder_Baseline.md`. That baseline is the best visual state so far, not a claim that shoulder roll is finished.

## Purpose

The system is a C++ Unreal integration for live video pose playback and retargeting. It is used to play short local reference clips, run a native MediaPipe pose landmarker DLL, convert the detected body landmarks into a local Unreal pose, and drive a Manny-style character inside the editor.

The main live test entry point is:

```text
mp.PlayMediaPipeVisualCycle clip=riverside conditioning=1 speed=1
```

`clip=riverside` is an alias for the riverbank video:

```text
Saved/Videos/01_09_riverbank_jumps.mp4
```

## Module Layout

Runtime module:

```text
Source/MediaPipeDriver
```

This module owns the native wrapper, tracker component, worker thread, source conditioner, solved-pose conversion, debug skeleton actor, and Manny retargeting anim instance.

Current refactor/source-layout checkpoint:

```text
Docs/MEDIAPIPE_REFACTOR_STATE_2026-05-17.md
```

As of 2026-05-20, `MediaPipePoseDrivenAnimInstance.cpp` is no longer the only source file for the retarget solve. The main file is 1,883 lines and includes cohesive implementation clusters for reference caching, torso/body solving, legs, Quest space mapping, Quest hand rotation, Quest fingers, arm twist, and Quest arm reach. Runtime state is held in named state objects in `MediaPipePoseDrivenSolverState.h`.

Editor module:

```text
Source/MediaPipeDriverEditor
```

This module owns the editor-only live video actor and console commands for spawning the video source, applying the live profile, cycling videos, logging diagnostics, and cleaning up live actors.

Important project target entries:

```text
Source/TestingKit3.Target.cs
Source/TestingKit3Editor.Target.cs
```

`TestingKit3Editor` includes both `MediaPipeDriver` and `MediaPipeDriverEditor`.

## Runtime Data Flow

The high-level runtime flow is:

```text
local video
  -> AMediaPipePoseVideoActor
  -> UMediaPipePoseTrackerComponent
  -> FMediaPipePoseTracker
  -> FMediaPipePoseWorker
  -> FMediaPipePoseWrapper / native DLL
  -> FMediaPipePoseFrame
  -> FMediaPipeSourceConditioner
  -> MediaPipeSolvedPose::BuildLocal
  -> UMediaPipePoseDrivenAnimInstance
  -> Manny-like skeletal mesh
```

The editor command layer sets up the source video actor and the driven Manny actor. The video actor owns a media player, media texture, and tracker component. The tracker component extracts RGB frames from the source texture, rate-gates them, optionally downsizes them, and enqueues them to the background tracker thread.

The worker thread calls the native DLL and publishes the newest landmark frame. The animation instance reads that latest frame on the game thread, caches it for animation evaluation, converts it into component-space bone rotations, and applies the result to the skeletal pose.

## Native Wrapper

The native wrapper is implemented in:

```text
Source/MediaPipeDriver/MediaPipePoseWrapper.h
Source/MediaPipeDriver/MediaPipePoseWrapper.cpp
```

`FMediaPipePoseWrapper` dynamically loads the native DLL and resolves these exports:

```text
MP_Init
MP_Init2
MP_Init3
MP_ProcessFrame
MP_GetLandmarks
MP_GetHandLandmarks
MP_Shutdown
```

The default DLL resolution is handled by `UMediaPipePoseTrackerComponent::ResolveDefaultDllPath()`. The preferred DLL locations are:

```text
Binaries/Win64/mediapipe/ump_shared.dll
Binaries/Win64/ump_shared.dll
ThirdParty/mediapipe_wrapper/ump_shared.dll
```

The default pose task model is resolved from:

```text
Content/MediaPipe/pose_landmarker_heavy.task
Content/MediaPipe/pose_landmarker_full.task
Content/MediaPipe/pose_landmarker.task
```

The optional hand model is:

```text
Content/MediaPipe/hand_landmarker.task
```

The wrapper can also run in mock mode for non-native testing, but the live visual cycle uses the native DLL path.

## Tracker Component

The tracker component is:

```text
Source/MediaPipeDriver/MediaPipePoseTrackerComponent.h
Source/MediaPipeDriver/MediaPipePoseTrackerComponent.cpp
```

`UMediaPipePoseTrackerComponent` supports these source types:

```text
MediaTexture
RenderTarget
ImageFile
```

Important settings include:

```text
WorldScale
bMirrorLandmarksLR
bAsyncMediaTextureReadback
MaxProcessRateHz
bUseSourceConditioning
bUseMockWrapper
WrapperDllPath
ConfigPath
bEnableHandLandmarker
```

For live video, the component reads a `UMediaTexture`, performs async readback when enabled, converts the pixels into RGB input, optionally downsizes according to `mp.MediaPipeInputMaxDimension`, and submits a frame to `FMediaPipePoseTracker`.

The tracker/worker pair intentionally keeps only the latest pending frame. If a new frame arrives while the worker still has one pending, the pending frame is overwritten. That favors low latency over processing every frame.

## Pose Data

The central data structures are in:

```text
Source/MediaPipeDriver/MediaPipePoseTypes.h
```

The pose enum contains the standard 33 MediaPipe pose landmarks, including shoulders, elbows, wrists, hips, knees, ankles, heels, and foot-index landmarks.

Each landmark stores:

```text
X
Y
Z
Visibility
Presence
Reliability
```

`Reliability` is the downstream confidence value used by filters and solvers. It is derived from MediaPipe confidence and can be down-weighted by sanity checks.

`FMediaPipePoseFrame` stores:

```text
Normalized landmarks
World landmarks
Optional hand landmarks
TimestampUs
bValid
bSourceConditioned
bHasHands
```

`FMediaPipePosePipelineStats` stores runtime counters and timings for component conversion, async readback, tracker publish, worker queue latency, native processing, and landmark extraction.

## Coordinate Conversion

Coordinate conversion is centralized in:

```text
Source/MediaPipeDriver/MediaPipePoseCoordinate.h
```

The project treats MediaPipe world coordinates as:

```text
+X camera-right
+Y camera-down
+Z forward away from camera
```

Unreal local coordinates are:

```text
+X forward
+Y right
+Z up
```

The unscaled conversion is:

```text
UE = (-MP_X, -MP_Z, -MP_Y)
```

When `bMirrorLandmarksLR` is enabled, the converted Unreal X value is multiplied by `-1` after conversion. The live profile currently uses mirrored landmarks.

## Source Conditioning

Source conditioning is implemented in:

```text
Source/MediaPipeDriver/MediaPipeSourceConditioner.h
Source/MediaPipeDriver/MediaPipeSourceConditioner.cpp
```

The conditioner can:

- hold unreliable landmarks
- smooth landmarks over time
- adapt segment lengths
- keep foot forward vectors in a stable hemisphere
- hold/reconstruct arms during occlusion
- reconstruct shoulder girdle positions during collapse cases

The frozen shoulder baseline keeps normal smoothing and adaptive conditioning, but disables the more aggressive arm/shoulder occlusion correction:

```text
mp.MediaPipeSourceSmoothingHalfLife=0.16
mp.MediaPipeSourceSmoothingFastSpeed=6.0
mp.MediaPipeSourceOcclusionArmHold=0
mp.MediaPipeSourceOcclusionShoulderReconstruct=0
mp.MediaPipeInputMaxDimension=512
```

This matters because the current shoulder debugging is intended to expose the code-side retarget solve. It should not be hidden by clamp-only or hold-only fixes.

## Solved Local Pose

Solved-pose conversion is implemented in:

```text
Source/MediaPipeDriver/MediaPipeSolvedPose.h
Source/MediaPipeDriver/MediaPipeSolvedPose.cpp
```

`MediaPipeSolvedPose::BuildLocal()` converts all world landmarks into Unreal-local centimeter space and builds a torso basis from hips, shoulders, and head direction.

The solved pose stores:

```text
LandmarksLocal
PelvisLocal
HipRightLocal
ShoulderRightLocal
UpLocal
ForwardLocal
bHasTorsoBasis
```

That solved torso basis is the shared body-space reference used by debugging, source conditioning, debug skeleton rendering, and skeletal retargeting.

## Retargeting

The Manny retarget path is implemented in:

```text
Source/MediaPipeDriver/MediaPipePoseDrivenAnimInstance.h
Source/MediaPipeDriver/MediaPipePoseDrivenAnimInstance.cpp
Source/MediaPipeDriver/MediaPipePoseDrivenSolverState.h
Source/MediaPipeDriver/MediaPipePoseDrivenAnimInstance_ReferenceCache.inl
Source/MediaPipeDriver/MediaPipePoseDrivenAnimInstance_TorsoBasis.inl
Source/MediaPipeDriver/MediaPipePoseDrivenAnimInstance_BodyPoseSolve.inl
Source/MediaPipeDriver/MediaPipePoseDrivenAnimInstance_LegSolve.inl
Source/MediaPipeDriver/MediaPipePoseDrivenAnimInstance_QuestSpaceMapping.inl
Source/MediaPipeDriver/MediaPipePoseDrivenAnimInstance_QuestHandRotation.inl
Source/MediaPipeDriver/MediaPipePoseDrivenAnimInstance_QuestFingerDrive.inl
Source/MediaPipeDriver/MediaPipePoseDrivenAnimInstance_QuestArmSolve.inl
Source/MediaPipeDriver/MediaPipePoseDrivenSkeletalActor.h
Source/MediaPipeDriver/MediaPipePoseDrivenSkeletalActor.cpp
```

`AMediaPipePoseDrivenSkeletalActor` owns a skeletal mesh component and optional Manny body control rig component. It auto-finds a source actor with `UMediaPipePoseTrackerComponent`, then assigns that source to `UMediaPipePoseDrivenAnimInstance`.

The anim instance has a custom anim node, `FAnimNode_MediaPipePoseDriven`, which:

- reads the latest pose frame during `PreUpdate`
- resets cached smoothing state on source discontinuities
- caches reference pose bone data
- drives pelvis, spine, head, arms, legs, hands, and optional twist/helper bones
- applies component-space rotations during animation evaluation

Quest hand/finger defaults as of the 2026-05-19 headset-confirmed checkpoint:

- Quest/OpenXR owns hand orientation, wrist endpoint authority, and finger movement in VR Preview.
- Fingers use `mp.QuestHandDriveFingerBones=1`, `mp.QuestFingerJointRetarget=0`, and `mp.QuestFingerCurlOnly=0`.
- The active finger path is `segmentDirection` in `Source/MediaPipeDriver/MediaPipePoseDrivenAnimInstance_QuestFingerDrive.inl`: live Quest segment directions are mapped into component space, retargeted through the current live parent chain, and the distal/tip segment is damped toward the parent-driven reference direction to avoid fingertip twists.
- The accepted 2026-05-22 Wallace arm-length checkpoint proved the TestingKit3-native full arm-chain snapshot. That run used the now-archived `mp.WallaceArmSource=1` alias; current user-facing testing switches only `mp.MetaHumanActiveProfile`, and the active profile default owns the arm-source decision internally. The snapshot is published by `Source/MediaPipeDriver/MediaPipeFullArmChainProvider.*` and contains per-side shoulder, upper-arm, lower-arm, and wrist-or-palm transforms, validity flags, confidence, timestamp, and sequence. `UMediaPipePoseDrivenAnimInstance` reads it when the generic profile resolver selects the full-chain source, and `Source/MediaPipeDriver/MediaPipePoseDrivenAnimInstance_QuestArmSolve.inl` then writes the active MetaHuman arm pose from that chain while leaving MediaPipe body/non-arm state and Quest hand/finger behavior available. Current proof logs should be `mp.MetaHumanFullArmChain` rows with `armSource=FullArmChain`, `chainActive=1`, all arm-chain validity flags set, and `mediaPipeArmUsed=0`.
- Profile 4 / `mp.QuestArmMode=3` keeps Quest wrist endpoint authority in the constrained arm path. `mp.QuestWristRequireTrackedForApply=1` still rejects stale/discontinuous untracked rows, but `FMediaPipeQuestWristApplyPolicy` lets constrained wrist position consume usable OpenXR joint positions when the tracked bit flickers false only if the untracked wrist is continuous with the last accepted live wrist sample. `DriveQuestHandCS()` now uses both that continuity gate and the current `FQuestWristMappingTrace`, so an untracked Quest hand rotation can be consumed only when the same frame accepted a mapped live untracked wrist endpoint. It is blocked when the wrist frame is held, unmapped, raw-rejected, body fallback, or not position-applied. `DriveArmCS()` must not pre-gate that path on `bQuestSideTrackedForArm`; `ShouldAttemptPositionSolve()` is the gate that decides whether usable live data or a short held target should reach `TryApplyQuestWristPositionWorld()`. The optional `mp.MediaPipeArmHoldOnQuestHandLoss` path is evaluated only after that attempt gate through `ShouldHoldArmOnQuestHandLoss()`, so it cannot freeze the last MediaPipe arm sample when the Quest endpoint policy still has a live or fresh-held wrist candidate. That attempt gate now sets `bHasHeldTarget` from `HasFreshHeldTargetForPositionAttempt()`, not the raw runtime flag, so an expired held target cannot bypass MediaPipe wrist reliability and then be rejected later by the apply path. Accepted live wrist samples refresh the held target even when the OpenXR tracked bit is false, so the next loss frame cannot snap back to stale tracked-only held data. Missing/expired held targets clear stale wrist-position authority/filter state through `ShouldClearPositionAuthorityForHeldTargetLoss()` before returning to constrained body fallback, so the next reacquired Quest wrist does not inherit full stale authority. Calibration and HMD-avatar anchor resets clear held targets, last accepted live wrist data, position filters, startup samples, and authority through `FQuestWristSideRuntimeState::ResetPositionContinuity()`, so held endpoints do not survive into a different HMD-relative anchor frame. Raw HMD-distance checks, the adaptive wrist-position filter, held-target continuity, and body fallback still guard bad endpoints. Temporary Quest wrist loss can still use `mp.QuestConstrainedArmBodyFallback=1`: `Source/MediaPipeDriver/MediaPipeQuestConstrainedArmSolver.*` builds a MediaPipe body-proportioned fallback endpoint, shares the same arms-down straightening helper used by the tracked constrained solve, uses side-aware left/right poles when a collinear body fallback pose has no usable source elbow pole, and `Source/MediaPipeDriver/MediaPipePoseDrivenAnimInstance_QuestArmSolve.inl` blends it from the last constrained solve before applying it. That fallback continuity is now also side-guarded, so stale wrong-side elbow history cannot be reused during a Quest wrist tracking-loss transition.
- When profile 4 has a mapped or body-fallback Quest wrist endpoint, `DriveArmCS()` uses `FMediaPipeQuestWristApplyPolicy::ShouldWriteFrameCoherentQuestArmPose()` to bypass the inherited MediaPipe arm rotation half-life and max-step/max-speed caps for the final upper/lower arm pose write. Profile 4 also sets `mp.MediaPipeArmTargetHalfLife=0.0`, `mp.MediaPipeArmRotationHalfLife=0.0`, `mp.MediaPipeArmRotationMaxStepDegrees=0.0`, and `mp.MediaPipeArmRotationMaxSpeedDegreesPerSecond=0.0`. This follows OculusXRMovement's frame-pose ownership: endpoint/filter/solver continuity happens before the write, but the valid frame is not delayed by another independent arm-rotation smoother.
- The constrained arm target solve is now isolated in `FMediaPipeQuestConstrainedArmSolver::BuildConstrainedArmTarget()`. That helper owns reach clamp, optional arm-down straightening, stable pole selection, low-weight MediaPipe elbow hinting, side-hemisphere pole locking, small-wrist-step pole continuity, near-full pole continuity, and branch-flip repair. Branch repair may reuse previous-pole history only when the repaired elbow stays on the current arm's side; stale wrong-side history is rejected instead of being allowed to override the side lock. The elbow move cap still smooths normal corrections, but it cannot preserve a wrong-side current elbow when the analytic solve is side-correct. The animation node now builds input, applies optional elbow smoothing, records trace values, and writes final bone transforms.
- In HMD-relative profile 4, the MediaPipe elbow hint is no longer the avatar's previous/current elbow. `DriveArmCS()` keeps the original MediaPipe shoulder/elbow/wrist sample before replacing the runtime shoulder/wrist frame with the avatar shoulder plus Quest endpoint. `FMediaPipeQuestConstrainedArmSolver::BuildSourceElbowHint()` rotates that current source elbow pole into the target avatar-shoulder/Quest-endpoint frame, side-locks it, and passes the resulting target-space elbow hint into `BuildConstrainedArmTarget()`. This mirrors the important OculusXRMovement ownership rule: build the current source-driven frame first, then infer helper/twist deformation from that same frame. `mp.QuestWristSolve` logs expose the path through `questArmSourceElbowHint` and `questArmSourceElbow`.
- After a successful constrained solve, the non-IK pose-write branch now derives upper/lower arm component-space rotations from the same solved elbow plane. `MediaPipeBodySolverMath::TryBuildSolvedElbowPlaneArmRotations()` builds the arm basis from `PoseUpperComp` / `PoseLowerComp` and rejects near-singular planes so the existing stable surface fallback still handles almost-straight degeneracy. This closes the earlier mismatch where the wrist/elbow target could be valid but the written arm roll came from an unrelated stable rig pole.
- The direct MediaPipe shoulder-rollback guard is outside the HMD-relative profile 4 arm path. It remains available for direct MediaPipe rollback failures, but `FMediaPipeArmGuardPolicy` prevents `mp.MediaPipeShoulderRollbackGuardBlend=0.0` from hard-holding Quest upper/lower arm rotations in `mp.QuestArmMode=3`, including startup/loss frames where the constrained solve has not succeeded yet.
- The fallback continuity caps both wrist and elbow movement and preserves recent constrained pole history through short tracking gaps only if that history is still on the current arm side. This is specifically to avoid the arm appearing stable at the wrist while the elbow snaps branches, and to avoid preserving a stale wrong-side elbow through a tracking-loss transition.
- The pose-frame continuity path now holds the last accepted MediaPipe body frame through a transient tracker miss instead of clearing `bHasPoseFrame` before a replacement frame exists. During that hold, the target mesh component transform still refreshes and current Quest hand/HMD state is still read. The frame-associated MediaPipe raw hand landmarks are held with the body frame and cleared only by an explicit retarget reset, so a one-frame MediaPipe dropout cannot collapse the avatar to reference pose or discard the matching fallback hand data while Quest hands continue to update.
- The active profile 4 default does not use a separate arms-down reach clamp: `mp.QuestConstrainedArmDownStraighten=0`, `mp.QuestConstrainedArmMaxReachStepCm=0.0`, and `mp.QuestConstrainedArmCloseReachPoleBias=0.0`. Arm length standardization starts with `mp.QuestConstrainedArmReachScaleCalibration=1` and `mp.QuestConstrainedArmReachScaleUniform=1`, then profile 4 enables `mp.QuestArmLengthCalibrationStartup=1` so VR Preview prompts Wallace only for two measured poses: full forward reach, then arms straight down. The accepted down pose must correct to at least 95% of the MetaHuman arm target and drives `mp.QuestArmDownFrameCorrection=1`, scaling the downward component of HMD-relative wrist targets from the user's measured sample instead of locking or straightening the elbow.
- Profile 4 sets the general constrained-arm cap to `mp.QuestConstrainedArmMaxReachFraction=0.997`. The HMD-relative path uses that CVar in body fallback, the pre-solve wrist clamp, and `BuildConstrainedArmTarget()`, so forward and side full-extension poses are not still limited by the older hidden `0.985` cap. Other profiles reset to `0.985` unless this CVar is changed deliberately.
- Profile 4 also sets `mp.QuestConstrainedArmSolvedPlaneMinSin=0.08` for the pose-write boundary after a successful constrained solve. Direct MediaPipe arms still use `mp.MediaPipeArmElbowPlaneMinSin`; the constrained path uses the lower threshold so its near-full, continuity-owned elbow pole is not discarded as almost singular before writing upper/lower arm rotations.
- Fallback rows in `mp.QuestWristSolve` now report `questArmBodyFallbackTargetReachCm`, `questArmBodyFallbackTargetReachFrac`, `questArmBodyFallbackDown`, and `questArmBodyFallbackDownAlpha`, so VR logs can prove whether a tracking-loss fallback actually kept an arms-down pose near full extension.
- Arms-down straightening no longer requires a valid MediaPipe torso basis. In profile 4, a downward Quest/HMD-relative wrist endpoint can still be extended toward the near-full reach cap using the available up vector when the webcam torso basis is missing or weak. Torso basis is still used when present for stable pole orientation.
- The no-torso stable elbow pole is side-aware. The constrained solver and the legacy reach-assist fallback use the current side's shoulder-right direction, or a left/right world fallback if the basis is unavailable, instead of a generic `+RightVector` pole. `LockPoleToSideHemisphere()` then keeps source/fallback/target poles in the correct side hemisphere so the elbow cannot keep the correct side position while silently flipping the pole vector. This prevents a left arm down by the thigh from choosing the right-side elbow branch when the webcam torso basis is missing and prevents moderate wrist-step pole flips from appearing as elbow snaps.
- Automation coverage `TestingKit3.MediaPipe.QuestConstrainedArm.TargetSolve` now covers arms-down extension, diagonal by-thigh extension, adaptive down-straighten bounds, less than 3 cm elbow pole offset in down pose, source-elbow-hint transfer into the target Quest endpoint frame, left/right side-aware no-torso elbow poles, no pole flip across a small wrist move, near-full pole continuity, branch-flip repair, wrong-side stale previous-pole rejection, wrong-side current-elbow rejection under the move cap, and no-history startup behavior. `TestingKit3.MediaPipe.QuestConstrainedArm.BodyFallback` covers body fallback side poles, valid current-side fallback continuity, and wrong-side fallback-history rejection. `TestingKit3.MediaPipe.QuestConstrainedArm.TrajectoryContinuity` covers noisy arms-down, no-torso side-of-body arms-down, and forward-reach sequences. `TestingKit3.MediaPipe.QuestConstrainedArm.TrackingLossRecovery` covers tracked arms-down solve -> temporary body fallback -> tracked recovery with near-full reach, capped fallback wrist/elbow steps, continuity use, and no elbow-pole flip. `TestingKit3.MediaPipe.QuestConstrainedArm.FullMotionSweep` now drives both arms through down-by-thigh, forward, side, and return-to-down poses with deliberately bad alternating elbow hints, and fails if anatomical lengths, side branch, near-full down reach, pole continuity, or elbow-step bounds regress. `TestingKit3.MediaPipe.BodySolverMath.SolvedElbowPlaneArmBasis` covers the solved-target-to-component-rotation boundary. `TestingKit3.MediaPipe.PoseFrameContinuity.HoldLastFrameOnDropout` covers the transient body-frame and frame-associated raw-hand dropout hold. The full `TestingKit3.MediaPipe` filter found 44 tests and passed after the source-elbow-hint correction.
- The 2026-05-19 forearm-roll trial is not a default: headset validation showed it made the arms snappy while the same forearm candy-wrapper spot remained. Keep `mp.QuestWristTwistDrivesForearm=0`, `mp.QuestWristForearmRollDriveTwistHelpers=0`, and the older helper-only `mp.QuestWristDriveTwistCorrection=0`.
- The standard helper path is active in profile 4 with `mp.MediaPipeDriveArmTwistBones=1`. `Source/MediaPipeDriver/MediaPipeArmTwistSolver.*` distributes target twist helpers from the solved parent/source-parent/source chain in the same broad style as OculusXRMovement's inferred twist-joint pass, and `DriveArmTwistBonesCS()` now applies those helpers statelessly from the current component-space frame. It does not keep an independent per-helper smoothing history that can lag the solved arm/hand chain. This is separate from direct Quest wrist-roll ownership, which stays off by default.
- Quest hand rotation startup now initializes smoothing from the current hand pose instead of jumping directly to the first authoritative Quest target. Profile 4 caps that hand-rotation smoothing at `mp.QuestHandRotationMaxStepDegrees=18.0` with `mp.QuestHandRotationHalfLife=0.035` to reduce reacquisition snaps. The untracked hand-rotation exception is current-frame scoped: it follows the wrist only when `FMediaPipeQuestWristApplyPolicy::CanUseQuestHandRotationForCurrentFrame()` proves the current wrist trace accepted live untracked data, not a held target or fallback.
- Wallace's broader MetaHuman arm sidecar/corrective helper path remains available as a diagnostic with `mp.MediaPipeDriveMetaHumanArmHelpers=1`, but profile 4 now leaves it off by default with `mp.MediaPipeDriveMetaHumanArmHelpers=0`. The 2026-05-20 topology audit found that Oculus-style target-skeleton detection includes 8/8 standard twist helpers, those standard helpers are not ancestors of Wallace hand/finger bones, and 20/20 broad MetaHuman corrective helpers remain outside the startup helper scope, so the current post-finger standard-helper write order is sidecar-safe while the broader correctives stay out of the default path until headset evidence proves they help rather than fight the rig.
- The AlanMovement/OculusXRMovement source cross-check showed the same solve ordering: tracked wrist maps to the hand bone, source wrist-twist joints stay unmapped, mapped arm/hand poses are built first, and twist helpers are interpolated afterward. `Tools/CheckWallaceArmSourceGuards.ps1` now guards TestingKit3's equivalent order: `DriveArmCS()` for both sides, `DriveArmTwistBonesCS()`, then safe component-to-local conversion.
- Older curl-only and spread-preservation notes are historical fallback experiments, not current defaults.

The current state ownership split is:

```text
FMediaPipeBodySolverState BodyState
FMediaPipeLegSolverState LeftLegState / RightLegState
FMediaPipeArmSolverState LeftArmState / RightArmState
FMediaPipeQuestWristSolverState QuestWristState
FMediaPipeQuestHandSolverState LeftQuestHandState / RightQuestHandState
FMediaPipeDiagnosticsState DiagnosticsState
```

Reset paths now route through these state objects instead of long flat field reset blocks.

The main Manny-like bones include:

```text
root
pelvis
spine_01 ... spine_05
neck_01
neck_02
head
clavicle_l / clavicle_r
upperarm_l / upperarm_r
lowerarm_l / lowerarm_r
hand_l / hand_r
thigh_l / thigh_r
calf_l / calf_r
foot_l / foot_r
ball_l / ball_r
```

The driven skeletal actor tries these fallback meshes:

```text
/Game/MediaPipe/MediaPipeRig/SK_MediaPipeMannyLike.SK_MediaPipeMannyLike
/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple.SKM_Manny_Simple
/Game/Characters/Mannequins/Meshes/SKM_Quinn_Simple.SKM_Quinn_Simple
```

## Current Arm And Shoulder Baseline

The frozen shoulder baseline is applied by:

```text
Source/MediaPipeDriverEditor/MediaPipeLiveVideoCommands.cpp
```

and described in:

```text
Archive/MediaPipe_Shoulder_Baseline.md
```

The historical live-video arm/shoulder path is:

- direct upper/lower arm segment alignment
- arm IK disabled
- older Quest experiments enabled clavicle driving for shoulder-root contribution
- hand rotation disabled
- standard twist helper bones available for diagnostics
- elbow-plane roll disabled
- arm reliability gate disabled in the live frozen profile
- no arm rotation step clamp in the live frozen profile
- separate upper/lower arm surface reference bases
- pose-aware surface-up hint for shoulder visual roll

The important implementation point is that endpoint direction and surface roll are now separated. The upper arm can keep matching the source shoulder-to-elbow direction while a separate surface basis controls where the visible shoulder patch sits.

Relevant fields and functions:

```text
RefUpperArmSurfaceBasisCompL / RefUpperArmSurfaceBasisCompR
RefLowerArmSurfaceBasisCompL / RefLowerArmSurfaceBasisCompR
BuildArmSurfaceUpHint()
DriveArmCS()
CacheArmRef()
```

The historical Wallace Quest state is documented in `Docs/WALLACE_QUEST_VR_CURRENT_DEFAULTS_HANDOFF.md`, and the archived Wallace-only aliases are documented in `Docs/Archive/WALLACE_LEGACY_ARM_SOURCE_2026-05-22.md`. The accepted arm-length checkpoint used `mp.WallaceArmSource=1` with full-chain proof rows and the 2026-05-22 Oculus Mirror run; current startup proof uses the generic active-profile path. The older profile-4 constrained wrist endpoint solver remains comparison/rollback context only. The live-video shoulder baseline above remains useful historical context, but it is not the active Wallace Quest startup proof target.

## Editor Live Video Workflow

The editor actor is:

```text
Source/MediaPipeDriverEditor/MediaPipePoseVideoActor.h
Source/MediaPipeDriverEditor/MediaPipePoseVideoActor.cpp
```

`AMediaPipePoseVideoActor` owns:

```text
UMediaPlayer
UMediaTexture
UMediaPipePoseTrackerComponent
VideoFilePath
WorldScale
bMirrorLandmarksLR
```

It ticks in editor viewports using `ShouldTickIfViewportsOnly()`, opens the configured local video, binds the media texture to the tracker component, and resets the tracker on seeks/source discontinuities.

The live command implementation is:

```text
Source/MediaPipeDriverEditor/MediaPipeLiveVideoCommands.cpp
```

Available commands:

```text
mp.PlayMediaPipeVisualCycle [clip=riverbank|riverside|barefoot|lunges|pose] [hz=30] [speed=1] [model=full|lite|default|path] [hands=0] [conditioning=1] [async=1]
mp.NextMediaPipeLiveVideo
mp.StopMediaPipeVisualCycle
```

Default clips:

```text
riverbank -> Saved/Videos/01_09_riverbank_jumps.mp4
barefoot  -> Saved/Videos/02_03_barefoot_studio_dance.mp4
lunges    -> Saved/Videos/09_08_lunges_workout.mp4
pose      -> Saved/Videos/pose.mp4
```

## Live Webcam Workflow

Implementation inclusion note:

```text
Docs/MEDIAPIPE_WEBCAM_INCLUSION.md
```

The webcam path reuses the same tracker and retargeter as the local-video path. The only difference is the media source:

```text
webcam capture device
  -> UMediaPlayer::OpenUrl(vidcap://...)
  -> UMediaTexture
  -> UMediaPipePoseTrackerComponent
  -> MediaPipe landmark solve
  -> Manny retarget anim instance
```

List cameras visible to Unreal's Media Framework:

```text
mp.ListMediaPipeWebcams
```

Start live tracking from the first camera:

```text
mp.PlayMediaPipeWebcam device=0 conditioning=1 hz=30 mirror=1
```

Start live tracking by camera name substring:

```text
mp.PlayMediaPipeWebcam device=Logitech conditioning=1 hz=30 mirror=1
```

Start live tracking from an explicit Unreal capture URL:

```text
mp.PlayMediaPipeWebcam url=vidcap://... conditioning=1 hz=30 mirror=1
```

Stop live webcam tracking:

```text
mp.StopMediaPipeWebcam
```

The webcam command applies the same frozen shoulder profile as `mp.PlayMediaPipeVisualCycle`, then opens the capture device into the existing `AMediaPipePoseVideoActor`. This keeps source conditioning, diagnostics, and Manny retargeting on the same code path as the video tests.

## VR Preview Mirror Workflow

Current baseline note:

```text
Docs/WALLACE_QUEST_VR_CURRENT_DEFAULTS_HANDOFF.md
Docs/WALLACE_QUEST_VR_EMBODIMENT_GUARDRAILS.md
Docs/MEDIAPIPE_VR_MIRROR_BASELINE.md
```

`Docs/WALLACE_QUEST_VR_CURRENT_DEFAULTS_HANDOFF.md` is the authoritative current Wallace/Quest state. `Docs/MEDIAPIPE_VR_MIRROR_BASELINE.md` is historical context for the older mirror-facing baseline.

The current Wallace Quest path uses embodied view by default. The headset wearer is behind Wallace's eyes, with profile 4 / `mp.QuestArmMode=3` using the HMD-relative avatar-space Quest wrist endpoint for arm reach while MediaPipe provides webcam shoulder/elbow/body hints. The older mirror-station logic remains useful context but is not the active default proof target.

For the preserved historical mirror-facing path, the body fix is not just actor placement. The anim node also enables:

```text
mp.MediaPipePoseYawAlignToActor=1
```

That rotates the cached MediaPipe landmark cloud in yaw before torso and arm retargeting. This is different from the older late torso override:

```text
mp.MediaPipeTorsoUseActorForward=1
```

The 2026-05-14 Wallace freeze supersedes the older finger-only baseline. The current VR Preview default keeps the actor-forward torso stability path on. The useful proof lines are:

```text
Auto Quest mirror: fixed Manny station ... stationYaw=... actualMannyYaw=... lockMannyYaw=1
mp.PoseYawAlign ... correctedForward=... remainingYawError=0.00
mp.TorsoDebug ... forward=... actorForward=0
```

## Diagnostics

The live cycle emits diagnostic log lines that should be preferred over still screenshots when judging whether the solve is actually moving correctly.

Useful log prefixes:

```text
mp.PlayMediaPipeVisualCycle movement
mp.MediaPipeSourceArmPlaneCompare
mp.MediaPipeRawVsConditionedArmPlane
mp.MediaPipeShoulderDiag
```

For shoulder issues, the key distinction is:

- source-side solved arm plane: what MediaPipe plus source conditioning says the shoulder/elbow/wrist plane is doing
- Manny-side solved arm plane: what the retargeted skeletal bones are actually doing

If source-side values move but Manny-side bones do not, the bug is in the retargeting/animation path. If raw source moves but conditioned source does not, the bug is in conditioning. If neither raw nor conditioned source moves, the issue is upstream in media playback, texture capture, native tracking, or frame publishing.

## Build And Test

Build command:

```text
& "D:\Epic\UE_5.7\Engine\Build\BatchFiles\Build.bat" TestingKit3Editor Win64 Development -Project="D:\Epic\Unreal_Projects\TestingKit3\TestingKit3.uproject" -WaitMutex -NoHotReload
```

Current MediaPipe validation, latest first:

```text
2026-05-22 full arm-chain checkpoint: Tools\CheckWallaceArmSourceGuards.ps1 passed; TestingKit3Editor Win64 Development build succeeded; broad TestingKit3.MediaPipe automation found 47 tests and passed; VR Preview/Oculus Mirror evidence run Saved/CodexAgent/QuestVrEvidence/full_arm_chain_vrpreview_20260522_101600 captured 7 successful worn/tracked Quest 3 mirror frames; Saved/Logs/TestingKit3.log contained 4,998 active historical mp.WallaceFullArmChain rows with mediaPipeArmUsed=0 throughout active rows; user confirmed the worn-headset result looked good. Current generic follow-up archives the Wallace aliases and uses mp.MetaHumanFullArmChain proof rows.
TestingKit3Editor build: succeeded
Automation RunTests TestingKit3.MediaPipe.Quest: 13 tests passed, exit code 0
Focused TestingKit3.MediaPipe.QuestConstrainedArm.TargetSolve after source-elbow-hint correction: passed
Focused TestingKit3.MediaPipe.QuestConstrainedArm.FullMotionSweep: passed
Focused TestingKit3.MediaPipe.QuestConstrainedArm: 5 tests passed
Focused TestingKit3.MediaPipe.Diagnostics.QuestWristRollCompactFormatter: passed
Focused TestingKit3.MediaPipe.ArmTwist.OculusStyleInterpolation: passed
Focused TestingKit3.MediaPipe.MetaHumanArmHelpers.WallaceCoverage: passed
Focused TestingKit3.MediaPipe.Runtime.CVars: passed
Tools/CheckWallaceArmSourceGuards.ps1: passed
Focused TestingKit3.MediaPipe.QuestWrist after constrained wrist attempt, held-target continuity, expired held-target authority reset, fresh-held-target attempt gate, and calibration/anchor position-continuity reset patches: 4 tests passed
Latest 2026-05-20 broad TestingKit3.MediaPipe rerun after the source-elbow-hint correction: 44 tests found, all completed successfully, exit code 0.
```

VR Preview remains the required validation step for future headset embodiment and Quest hand/wrist behavior changes. For the 2026-05-22 full arm-chain arm-length change, that validation has been captured and accepted.

Primary visual test command:

```text
mp.PlayMediaPipeVisualCycle clip=riverside conditioning=1 speed=1
```

Expected completion log:

```text
mp.PlayMediaPipeVisualCycle: clip 1/1 riverbank
mp.PlayMediaPipeVisualCycle: complete. Run the command again to restart.
```

## Known Limits

The current system is monocular-video based. Shoulder roll, forearm twist, hand roll, and self-occluded arm planes are underconstrained by the source data.

The current frozen profile deliberately avoids several aggressive correction paths while shoulder work is being debugged. That makes code-side retarget problems easier to see, but it also means occluded or ambiguous source frames can still produce imperfect shoulder surface placement.

Known current visual issue:

```text
The grey/dark shoulder material can still appear slightly too visible under the front deltoid in some poses.
```

Future fixes should be validated with diagnostic arm-plane logs across multiple clips, not only viewport screenshots.

## Primary File Map

Runtime:

```text
Source/MediaPipeDriver/MediaPipePoseTypes.h
Source/MediaPipeDriver/MediaPipePoseCoordinate.h
Source/MediaPipeDriver/MediaPipePoseWrapper.h
Source/MediaPipeDriver/MediaPipePoseWrapper.cpp
Source/MediaPipeDriver/MediaPipePoseTrackerComponent.h
Source/MediaPipeDriver/MediaPipePoseTrackerComponent.cpp
Source/MediaPipeDriver/MediaPipePoseTracker.h
Source/MediaPipeDriver/MediaPipePoseTracker.cpp
Source/MediaPipeDriver/MediaPipePoseWorker.h
Source/MediaPipeDriver/MediaPipePoseWorker.cpp
Source/MediaPipeDriver/MediaPipeSourceConditioner.h
Source/MediaPipeDriver/MediaPipeSourceConditioner.cpp
Source/MediaPipeDriver/MediaPipeSolvedPose.h
Source/MediaPipeDriver/MediaPipeSolvedPose.cpp
Source/MediaPipeDriver/MediaPipePoseDrivenAnimInstance.h
Source/MediaPipeDriver/MediaPipePoseDrivenAnimInstance.cpp
Source/MediaPipeDriver/MediaPipePoseDrivenSolverState.h
Source/MediaPipeDriver/MediaPipePoseDrivenAnimInstance_ReferenceCache.inl
Source/MediaPipeDriver/MediaPipePoseDrivenAnimInstance_TorsoBasis.inl
Source/MediaPipeDriver/MediaPipePoseDrivenAnimInstance_BodyPoseSolve.inl
Source/MediaPipeDriver/MediaPipePoseDrivenAnimInstance_LegSolve.inl
Source/MediaPipeDriver/MediaPipePoseDrivenAnimInstance_ArmTwist.inl
Source/MediaPipeDriver/MediaPipePoseDrivenAnimInstance_QuestSpaceMapping.inl
Source/MediaPipeDriver/MediaPipePoseDrivenAnimInstance_QuestHandRotation.inl
Source/MediaPipeDriver/MediaPipePoseDrivenAnimInstance_QuestFingerDrive.inl
Source/MediaPipeDriver/MediaPipePoseDrivenAnimInstance_QuestArmSolve.inl
Source/MediaPipeDriver/MediaPipeRuntimeCVars.h
Source/MediaPipeDriver/MediaPipeRuntimeCVars.cpp
Source/MediaPipeDriver/MediaPipeBodySolverMath.h
Source/MediaPipeDriver/MediaPipeBodySolverMath.cpp
Source/MediaPipeDriver/MediaPipeBodyDiagnostics.h
Source/MediaPipeDriver/MediaPipeBodyDiagnostics.cpp
Source/MediaPipeDriver/MediaPipeQuestWristCalibrationState.h
Source/MediaPipeDriver/MediaPipeQuestWristCalibrationState.cpp
Source/MediaPipeDriver/MediaPipeQuestFingerSolver.h
Source/MediaPipeDriver/MediaPipeQuestFingerSolver.cpp
Source/MediaPipeDriver/MediaPipeQuestHandCaptureReplayTooling.h
Source/MediaPipeDriver/MediaPipeQuestHandCaptureReplayTooling.cpp
Source/MediaPipeDriver/MediaPipePoseDrivenSkeletalActor.h
Source/MediaPipeDriver/MediaPipePoseDrivenSkeletalActor.cpp
Source/MediaPipeDriver/MediaPipeTrackedSkeletonActor.h
Source/MediaPipeDriver/MediaPipeTrackedSkeletonActor.cpp
```

Editor:

```text
Source/MediaPipeDriverEditor/MediaPipePoseVideoActor.h
Source/MediaPipeDriverEditor/MediaPipePoseVideoActor.cpp
Source/MediaPipeDriverEditor/MediaPipeLiveVideoCommands.cpp
```

Current shoulder baseline note:

```text
Archive/MediaPipe_Shoulder_Baseline.md
```
