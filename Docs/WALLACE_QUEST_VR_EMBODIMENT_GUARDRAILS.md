# Wallace Quest VR Embodiment Guardrails - 2026-05-17

This document exists to prevent a repeat of the VR Preview embodiment regression where the Quest wearer spawned above, below, sideways from, or over Wallace instead of behind Wallace's eyes.

Read this before changing any Auto Quest embodied camera, pawn, HMD origin, Wallace placement, or Wallace arm-space code. Also read `Docs/METAHUMAN_PROFILE_DRIVEN_RETARGETING.md` before applying the embodied path to Emory, Hudson, Kellan, Maria, Payton, or future MetaHumans. The archived Wallace-only arm-source controls are recorded in `Docs/Archive/WALLACE_LEGACY_ARM_SOURCE_2026-05-22.md`.

Current source-layout checkpoint:

```text
Docs/MEDIAPIPE_REFACTOR_STATE_2026-05-17.md
```

Do not search only `MediaPipePoseDrivenAnimInstance.cpp` for arm/wrist/hand behavior. The AnimInstance still orchestrates evaluation, but Quest arm, Quest hand rotation, Quest space mapping, body/leg solving, runtime CVars, and solver state now live in separate source clusters.

## Current Non-Negotiable State

- VR Preview must embody MetaHuman Wallace: the wearer is behind Wallace's eyes, and looking down should show Wallace's chest/body under the view, not Wallace's shoulder, neck cavity, floor, or the underside of the level.
- The project can still use the Third Person map/game mode, but the embodied view must not use the Third Person pawn or spring-arm camera rig.
- The local viewer pawn for stable embodied mode is the hidden tagged fallback `DefaultPawn_0`; it must have no spring arms and no camera boom.
- Stable embodied mode uses a Wallace eye station plus bounded OpenXR origin recentering. After startup recenter, it may translate Wallace/Manny horizontally from room-scale HMD motion so the body remains under the headset wearer while walking. It must not shove the pawn/avatar from raw HMD error, must ignore raw HMD Z, and must guard against huge tracking-origin jumps.
- Wake recenter must be driven by horizontal HMD error, not full 3D HMD error. A raw headset Z jump must not trigger a second yaw/origin reset. Room-scale follow must keep the current deadband/capped-step guard (`mp.AutoQuestEmbodiedRoomScaleDeadbandCm=8.0`, `mp.AutoQuestEmbodiedRoomScaleMaxStepCm=12.0`) unless deliberately testing a different body-follow policy.
- MetaHuman profiles currently use local `+Y` as the visible face/chest axis, not Unreal's default actor `+X`. Camera placement and arm/body mapping must agree about the active profile's configured axis. Wallace's proven value is local `+Y`.
- The accepted 2026-05-22 arm-length checkpoint proved the TestingKit3 native full arm-chain snapshot for Wallace. That run used the now-archived `mp.WallaceArmSource=1` compatibility alias and produced `mp.WallaceFullArmChain` proof rows; current user-facing testing switches profiles only with `mp.MetaHumanActiveProfile Wallace`. The accepted VR Preview/Oculus Mirror run is `Saved/CodexAgent/QuestVrEvidence/full_arm_chain_vrpreview_20260522_101600`; it captured 7 successful worn/tracked Quest 3 mirror frames and 4,998 active Wallace full-chain rows. The user confirmed the worn-headset result looked good. Do not enable the reference project's MetaXR/OculusXRMovement plugin in TestingKit3; the reference project remains reference-only.
- The legacy comparison/rollback arm checkpoint is profile 4 / `mp.QuestArmMode=3`: HMD-relative avatar-space Quest wrist endpoint authority with MediaPipe shoulder/elbow hints, no arm IK, no lower body, no pelvis translation. After the SafeSetCS rebuild, the user confirmed in worn-headset VR Preview that Wallace's arms move forward with the Quest hands again. The current default no longer uses constrained body fallback as the profile 4 loss path: `mp.QuestConstrainedArmBodyFallback=0`, `mp.MediaPipeArmHoldOnQuestHandLoss=1`, and fresh held Quest endpoints own the short tracking-loss grace window. Profile 4 sets the general constrained-arm reach cap to `mp.QuestConstrainedArmMaxReachFraction=0.997`, enables HMD-relative reach normalization with `mp.QuestConstrainedArmReachScaleCalibration=1` and `mp.QuestConstrainedArmReachScaleUniform=1`, disables the rigid arms-down repair with `mp.QuestConstrainedArmDownStraighten=0`, disables the reach-step limiter with `mp.QuestConstrainedArmMaxReachStepCm=0.0`, and removes the special close-reach pole override with `mp.QuestConstrainedArmCloseReachPoleBias=0.0`. Profile 4 now enables two-pose startup calibration with `mp.QuestArmLengthCalibrationStartup=1`: the headset/mirror prompt accepts stable full-forward reach first, then stable arms-down reach, and `mp.QuestArmDownFrameCorrection=1` uses that measured down sample to scale the downward component of HMD-relative wrist targets without locking elbow flex. Profile 4 also sets `mp.QuestConstrainedArmSolvedPlaneMinSin=0.08`, used only after the constrained solve succeeds, so the solved elbow pole is not discarded by the pose-write layer while direct MediaPipe frames keep the stricter `mp.MediaPipeArmElbowPlaneMinSin`. After the constrained solve succeeds, `DriveArmCS()` writes upper/lower arm rotations from that same solved elbow plane instead of requiring `mp.MediaPipeArmUseElbowPlaneRoll=1`; near-singular elbow planes still use the stable fallback. When a mapped Quest wrist endpoint exists, the profile 4 pose write is frame-coherent: `ShouldWriteFrameCoherentQuestArmPose()` disables inherited MediaPipe arm rotation smoothing and step/rate clamps, and profile 4 sets the arm target/rotation half-life and rotation caps to `0.0`. The constrained wrist-position path must be entered through `FMediaPipeQuestWristApplyPolicy::ShouldAttemptPositionSolve()` rather than a raw `bQuestSideTrackedForArm` pre-gate, and accepted live wrist samples must refresh the held target so temporary Quest tracking loss cannot snap back to stale tracked-only data. The optional hold path must be evaluated through `ShouldHoldArmOnQuestHandLoss()` after the endpoint attempt policy, so it cannot freeze the arm when a live or fresh-held Quest endpoint candidate exists. Missing or expired held targets must clear stale wrist-position authority/filter state before the hold path, so reacquired Quest wrists do not inherit full stale authority. Calibration and HMD-avatar anchor resets must clear endpoint continuity with `ResetPositionContinuity()` so a held wrist target mapped under an old HMD-relative anchor cannot survive into the new anchor frame. Treat this as the old profile-4 comparison path for the 2026-05-22 arm-length issue, not the accepted full-chain proof path. Do not roll this back to profile 0 / mode 0 unless the user explicitly asks for that rollback.
- The direct MediaPipe shoulder-rollback hard hold must stay out of the HMD-relative profile 4 Quest arm path. `mp.MediaPipeShoulderRollbackGuard=1` can still protect the older direct MediaPipe shoulder path, but it must not freeze upper/lower arm rotations in `mp.QuestArmMode=3`, including startup, temporary Quest wrist loss, and failed-solve frames.
- The 2026-05-20 arm-length calibration follow-up scopes the prompt to Wallace only, latches accepted arm-length samples across HMD translation-filter resets for the VR Preview session, and requires the corrected arms-down reach to be at least 95% of Wallace's MetaHuman arm target before accepting the down pose.
- Arm roll/candy-wrapper work is not accepted as fixed. The default keeps direct Quest wrist roll out of forearm and upper-arm helper drivers: `mp.QuestWristTwistDrivesForearm=0`, `mp.QuestWristForearmRollDriveTwistHelpers=0`, `mp.QuestWristDriveTwistCorrection=0`, and `mp.QuestWristUpperArmRollDriveTwistHelpers=0`. The standard Oculus-style target-skeleton twist helper pass is active with `mp.MediaPipeDriveArmTwistBones=1`, while Wallace's broader MetaHuman arm sidecar/corrective helpers are off with `mp.MediaPipeDriveMetaHumanArmHelpers=0`. The topology audit found those broad correctives are outside the Oculus-style startup helper scope, so they remain diagnostic-only and not worn-headset accepted.
- Quest wrist continuity must not cross invalid source-stream boundaries. If the active MediaPipe pose timestamp rewinds, both pose-yaw runtime state and Quest wrist runtime state must reset before the next solve. Quest semantic wrist roll must stay unwrapped until the max-twist clamp; do not normalize `QuestTwistDeg` back into +/-180 before storing `RotationSemanticRollLastTwistDeg`, because that can turn a steady +170 degree wrist roll into -170 and recreate the recorded ~300 degree roll snaps.
- Stable embodied body keeps clavicles disabled (`mp.MediaPipeDriveClavicles=0`) while spine, pelvis, legs, and IK remain off. Do not re-enable clavicles by default unless a separate worn-headset diagnostic proves they improve the arm without blocking extension.

## What Caused The Regression

The regression came from mixing several coordinate spaces and treating them as interchangeable:

1. The project default map and game mode spawn `BP_ThirdPersonCharacter_C_0`. The old Auto Quest viewer path reused `PlayerController->GetPawn()`, so VR Preview inherited the Third Person character/spring-arm camera instead of a clean embodied viewer pawn.
2. A later attempted fix used raw HMD/world position error to move the viewer/avatar. When the Quest woke up or was put on, OpenXR tracking origin shifted, and that raw offset pushed the wearer above Wallace, below the level, or away from Wallace.
3. Stable embodied yaw was captured from the first valid HMD pose. If the headset was asleep, on the desk, or mid-donning, the first pose was stale and the wrong yaw was baked into Wallace.
4. Wallace's actual face/eye axis is local `+Y`. Treating Wallace as if the visible face was actor `+X` put the view over the shoulder/neck even when height looked plausible.
5. After fixing Wallace's visual `+Y` face axis, the Quest/MediaPipe arm mapper still used `actor +X` and then raw component-space writes in places where child arm bones could become stale. The current path uses Wallace face-forward mapping plus `SafeSetCSBoneTransforms()` for component-space writes so the hand/wrist children follow the moved arm chain.

## Correct Architecture

The camera and avatar placement path is in `Source/MediaPipeDriver/MediaPipeDriver.cpp`.

- `GetOrCreateMirrorPawn()` must not reuse the existing player pawn in stable embodied mode unless it is the tagged mirror camera pawn.
- `DestroySupersededThirdPersonViewerPawn()` must remove the default Third Person viewer pawn after switching to the fallback view pawn.
- `ConfigureMirrorPlayerPawn()` should report `fixed viewer pawn=DefaultPawn_0 ... springArms=0 cameras=0` in the VR Preview log.
- `ResolveEmbodiedStation()` owns the stable active-MetaHuman eye station.
- `TryResolveMetaHumanEyeLocalOffset()` samples the active MetaHuman facial eye bones (`FACIAL_L_Eye` and `FACIAL_R_Eye`) and falls back to the profile's default eye center. Wallace's measured fallback is `V(X=0.0, Y=8.92, Z=161.94)`.
- `mp.AutoQuestEmbodiedCameraForwardOffsetCm` defaults to `0.0`. Do not restore `12.0` unless doing a deliberate comparison, because the goal is to put the wearer at the eye center.
- `mp.AutoQuestEmbodiedWallaceYawOffsetDeg=-90.0` aligns Wallace's local `+Y` visible face axis with the HMD/view yaw.
- `UpdateStableEmbodiedHmdOriginReset()` captures yaw from the settled HMD pose at bounded startup recenter time, not from the first valid HMD pose.
- `ScheduleMirrorStationRefresh()` should refresh the station after stable yaw recenter and then place Wallace using the refreshed `viewerYaw` and `avatarYaw`.

The Wallace arm/body coordinate path is split across:

```text
Source/MediaPipeDriver/MediaPipePoseDrivenAnimInstance.cpp
Source/MediaPipeDriver/MediaPipePoseDrivenAnimInstance.h
Source/MediaPipeDriver/MediaPipePoseDrivenSolverState.h
Source/MediaPipeDriver/MediaPipePoseDrivenAnimInstance_TorsoBasis.inl
Source/MediaPipeDriver/MediaPipePoseDrivenAnimInstance_BodyPoseSolve.inl
Source/MediaPipeDriver/MediaPipePoseDrivenAnimInstance_QuestSpaceMapping.inl
Source/MediaPipeDriver/MediaPipePoseDrivenAnimInstance_QuestHandRotation.inl
Source/MediaPipeDriver/MediaPipePoseDrivenAnimInstance_QuestArmSolve.inl
Source/MediaPipeDriver/MediaPipeFullArmChainProvider.h
Source/MediaPipeDriver/MediaPipeFullArmChainProvider.cpp
```

- `FMediaPipeResolvedMetaHumanTarget` marks configured MetaHuman targets and records whether the target uses the MetaHuman face-forward axis.
- `GetTargetForwardWorld()` returns target component `+Y` for configured MetaHuman profiles and `+X` for normal Manny-like targets.
- Pose yaw alignment and torso actor-forward logic must use `GetTargetForwardWorld()`, not hard-coded `TargetCompTransform.GetUnitAxis(EAxis::X)`, for MetaHuman profiles.
- No-torso arm fallback must use `MediaPipeBodySolverMath::BuildAvatarArmBasis()` so Wallace keeps target component `+Y` forward, target-derived up, and target-derived right when MediaPipe torso basis is missing. Do not seed profile 4 constrained arms from generic world axes before `TryGetTorsoBasisWorld()`, and do not reset no-torso `HipRightComp`, `ShoulderRightComp`, `UpComp`, or `ForwardComp` to generic component axes before the pose-write/branch-guard layer.
- The current accepted arm-length path is generic/profile-driven full arm-chain authority. The user-facing switch is `mp.MetaHumanActiveProfile <Name>`; the active profile default owns arm authority internally. The archived `mp.WallaceArmSource=1` alias was used only by the historical 2026-05-22 Wallace checkpoint.
- `mp.QuestArmMode=3` is the legacy comparison/rollback arm-space path. It maps the Quest wrist endpoint through the HMD-relative avatar frame and keeps MediaPipe as a low-weight shoulder/elbow hint.
- `Source/MediaPipeDriver/MediaPipePoseDrivenAnimInstance_QuestArmSolve.inl` must call `FMediaPipeQuestWristApplyPolicy::ShouldAttemptPositionSolve()` before attempting the Quest wrist-position path. Do not restore the older `bQuestSideTrackedForArm` pre-gate; it blocks the lower continuity checks from seeing usable OpenXR wrist samples with a false tracked bit.
- If Quest wrist tracking drops while profile 4 is active, `Source/MediaPipeDriver/MediaPipePoseDrivenAnimInstance_QuestArmSolve.inl` should use fresh held Quest endpoints during the grace window and then hold the last reliable arm sample. Body-proportioned fallback is diagnostic-only in the current default because the latest VR rows showed fallback transitions could still jump.
- Component-space arm writes must keep using `SafeSetCSBoneTransforms()` in `ApplyRotationCS`, `ApplyTranslationDeltaCS`, `DriveArmTwistBonesCS`, and `DrivePelvisTranslationCS`. Raw `SetComponentSpaceTransform()` writes can leave the child wrist/hand transforms stale and recreate the contorted/static-arm failure.
- Do not fix arm direction by undoing Wallace's `-90` camera/body offset. That would put the camera back over the shoulder/neck.

## Forbidden Fixes

Do not repeat these routes:

- Do not use normal PIE as proof. The bug appears when the Quest wakes and VR Preview supplies the real HMD pose.
- Do not use the Third Person pawn, `BP_ThirdPersonCharacter_C_0`, or any spring-arm camera boom as the embodied view.
- Do not call this fixed if `fixed viewer pawn` shows spring arms or cameras.
- Do not place Wallace root at raw HMD Z height.
- Do not subtract raw HMD world location from the station or shove the pawn/avatar based on full 3D HMD eye error. The only allowed stable-mode follow is horizontal room-scale delta after startup recenter, deadbanded by `mp.AutoQuestEmbodiedRoomScaleDeadbandCm`, capped per update by `mp.AutoQuestEmbodiedRoomScaleMaxStepCm`, and rejected by `mp.AutoQuestEmbodiedRoomScaleMaxOffsetCm`.
- Do not reintroduce recurring camera pinning for stable embodied mode.
- Do not change legs, pelvis translation, or IK to solve this.
- Do not turn on `mp.MediaPipeDriveLegs`, `mp.MediaPipeUseLegIK`, `mp.MediaPipeDrivePelvisTranslation`, `mp.MediaPipeUseArmIK`, or `mp.QuestWristForceArmIK`.
- Do not "fix" arm direction by reverting Wallace to actor `+X` face assumptions.

## Required VR Preview Proof

The proof must come from VR Preview with the Quest worn or woken. Normal PIE is not enough.

For current full arm-chain arm-length proof, run or inspect a generic profile capture:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\Tools\CaptureMetaHumanQuestVrEvidence.ps1 -Profile Wallace -CaptureSource HmdMirror -RunName full_arm_chain_vrpreview_<timestamp>
```

The generic capture script is evidence tooling, not the normal profile-switch workflow. It pre-arms diagnostic trace CVars internally so the log contains proof rows. The current proof log pattern is:

```text
mp.MetaHumanFullArmChain: profile=Wallace ... actor=MP_LiveMetaHumanWallace ... side=L ... armSource=FullArmChain ... chainActive=1 ... shoulderValid=1 ... upperArmValid=1 ... lowerArmValid=1 ... wristOrPalmValid=1 ... mediaPipeArmUsed=0 ... targetReachCm=... elbowBendDeg=... handWorld=(...) ...
mp.MetaHumanFullArmChain: profile=Wallace ... actor=MP_LiveMetaHumanWallace ... side=R ... armSource=FullArmChain ... chainActive=1 ... shoulderValid=1 ... upperArmValid=1 ... lowerArmValid=1 ... wristOrPalmValid=1 ... mediaPipeArmUsed=0 ... targetReachCm=... elbowBendDeg=... handWorld=(...) ...
```

The 2026-05-22 accepted run produced 4,998 active full-chain rows: left reach `21.2-52.1 cm`, right reach `21.8-51.1 cm`, left elbow bend `25.5-128.9 deg`, right elbow bend `30.0-128.1 deg`, and `mediaPipeArmUsed=0` throughout active rows. Later `chainActive=0` rows after shutdown are not proof failures; inspect the active worn/tracked capture window.

Use `Saved/Logs/TestingKit3.log` and look for these patterns after a fresh VR Preview run:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\Tools\CheckWallaceQuestVrEmbodimentLog.ps1 -LogPath .\Saved\Logs\TestingKit3.log -RequireWornHeadsetTrace
```

```text
Game class is 'BP_ThirdPersonGameMode_C'
Auto Quest embodied: destroying default Third Person viewer pawn BP_ThirdPersonCharacter_C_0 after switching to DefaultPawn_0.
Auto Quest presentation mesh: driver=... presentationActor=... mesh=Body asset=m_med_unw_body animClass=... postProcessClass=...m_med_unw_animbp_Cinematic... postProcessDisabled=0
Auto Quest mirror: fixed viewer pawn=DefaultPawn_0 ... springArms=0 cameras=0 ...
Auto Quest embodied: camera=... viewerYaw=... avatarYaw=... forwardOffset=0.0 anchorMode=1 viewPawn=DefaultPawn_0
mp.QuestWristSnapshot: actor=MP_LiveMetaHumanWallace ... tracked=1 ... hmdPose=1 ...
mp.QuestWristSolve: actor=MP_LiveMetaHumanWallace ... questArmMode=3 ... positionApplied=1 ... requestedBlend=1.00 ... calib=HMD_AVATAR ... mapped=1 ... questHandTracked=1 ... handLocal=1 ...
mp.QuestWristSolve: actor=MP_LiveMetaHumanWallace ... questArmMode=3 ... questArmSolve=1 ... questArmBodyFallback=0 ... questArmSourceElbowHint=1 ... questArmSourceElbow=... ...
Auto Quest profile applied: armProfile=4 ... constrainedArmBodyFallback=0 armHoldLoss=1 ... reachScale=1 ... handRotHL=0.00 handRotStep=0.0 ...
mp.MetaHumanArmSanity: actor=MP_LiveMetaHumanWallace ... questArmMode=3 ... requestedBlend=1.00 ... broken=0 ...
Auto Quest embodied: calibrated stable station yaw from settled HMD yaw=...
Auto Quest mirror: reset HMD origin to fixed viewer yaw=...
Auto Quest embodied: reset HMD origin for stable Wallace view ... stationYawBefore=... resetYaw=...
Auto Quest embodied: reset HMD origin for stable Wallace view ... horizontalErrorBefore=... rawZErrorBefore=...
Auto Quest embodied: room-scale follow applied horizontalDelta=... appliedCm=... deadband=8.0 maxStep=12.0 capped=... rawZIgnored=...
Auto Quest embodied: refreshed station after stable yaw recenter camera=... avatar=... viewerYaw=... avatarYaw=...
```

Expected yaw relationship for Wallace:

```text
avatarYaw ~= viewerYaw - 90 degrees
```

Expected negatives:

```text
No recurring "camera pinned" lines in stable embodied mode.
No active Third Person viewer pawn after the switch to DefaultPawn_0.
No spring arms or camera boom on the viewer pawn.
No IK/legs/pelvis CVars enabled.
```

Visual success condition:

- Facing feels correct.
- Looking down shows Wallace's chest/body under the wearer, not a shoulder or the inside of the neck.
- The wearer is not floating meters above Wallace and is not below the level.
- Reflections/mirrors must still be able to see Wallace's head; local head culling is only for the wearer view.

## Current Arm Checkpoint

As of the 2026-05-20 source-elbow-hint checkpoint, the HMD-relative constrained arm solve must keep the original MediaPipe shoulder/elbow/wrist sample, rotate that source elbow pole into the avatar-shoulder plus Quest-endpoint frame, and pass it into the constrained solve. The worn-headset gate now fails unless at least one non-body-fallback Wallace `mp.QuestWristSolve` row proves `questArmSourceElbowHint=1` with a logged `questArmSourceElbow=...` value.

As of the 2026-05-20 solver-continuity checkpoint, the arm path also must reset global pose-yaw and Quest wrist runtime state on MediaPipe timestamp rewind, and semantic wrist roll must use continuous angle unwrapping before the twist clamp. The source guard checks both reset calls and the roll-unwrapping helper; `TestingKit3.MediaPipe.QuestWrist.ApplyPolicy` covers the +/-180 wrap regressions.

The editor `mp.StartQuestWebcamHands` route is not allowed to use a different legacy arm profile. `ApplyQuestWebcamHandsProfile()` must stay aligned with the profile 4 HMD-relative constrained arm defaults: stable embodied clavicle/spine defaults off, `mp.QuestArmMode=3`, `mp.QuestWristPositionBlend=1.0`, tracked-only wrist apply, constrained solve/down-straighten, standard twist helpers on, broad MetaHuman sidecar helpers off, and IK off. `Tools\CheckWallaceArmSourceGuards.ps1` guards this route because a stale editor command can make later arm fixes look blocked.

The Wallace presentation component must keep the MetaHuman body post-process AnimBP active. The commandlet inspection proved `/Game/MetaHumans/Wallace/Body/m_med_unw_body.m_med_unw_body` uses `/Game/MetaHumans/Common/Male/Medium/UnderWeight/Body/m_med_unw_animbp_Cinematic...`; the runtime now forces `SetDisablePostProcessBlueprint(false)` on the presentation mesh and logs `postProcessDisabled=0`. If that log line is missing, the arm-skin deformation path has not been proved.

The 2026-05-17 SafeSetCS checkpoint remains relevant for the coordinate basis: the embodiment camera/facing path and the arm-space path are on the intended coordinate basis, and the user confirmed that the arms moved forward with the Quest hands again. It is not enough by itself to prove the later source-elbow-hint arm path.

Future arm work must still be diagnosed with targeted `mp.QuestWristTrace`, `mp.QuestHandCompare`, `mp.MetaHumanArmSanity`, and torso/arm logs. Keep these constraints:

- `mp.MediaPipeUseArmIK=0`
- `mp.QuestWristForceArmIK=0`
- `mp.MediaPipeDriveLegs=0`
- `mp.MediaPipeUseLegIK=0`
- `mp.MediaPipeDrivePelvisTranslation=0`
- Keep Wallace camera/body placement on the `+Y` face-forward basis while diagnosing arm instability.
- Keep the current profile 4 / mode 3 path unless the user explicitly asks for rollback: `mp.AutoQuestArmReachAssistProfile=4`, `mp.QuestArmMode=3`, `mp.QuestWristPositionBlend=1.0`.
- Keep `mp.AutoQuestEmbodiedStableBody=1`, `mp.MediaPipeDriveClavicles=0`, `mp.MediaPipeDriveSpine=0`, and lower-body/pelvis driving off so shoulder-root retargeting cannot mask the arm endpoint solve.
- Do not reintroduce the shoulder-rollback guard as a hard hold around a successful profile 4 constrained solve. The guard-policy test `TestingKit3.MediaPipe.ArmGuardPolicy.ShoulderRollback` exists to keep that ownership split intact.
- Do not reintroduce a raw `mp.MediaPipeArmHoldOnQuestHandLoss` freeze before the Quest endpoint policy. The hold is diagnostic-only and must be suppressed whenever `ShouldAttemptPositionSolve()` reports a live or fresh-held wrist-position candidate.
- Keep the profile 4 constrained-elbow guard off (`mp.QuestConstrainedArmElbowHalfLife=0.0`, `mp.QuestConstrainedArmMaxElbowStepCm=0.0`) unless deliberately running a new diagnostic. The 2026-05-19 `0.06` / `4.0` trial was rejected in worn-headset VR Preview because it locked the biceps and prevented arm extension.
- Keep the profile 4 reach-step limiter off (`mp.QuestConstrainedArmMaxReachStepCm=0.0`) unless deliberately running a new diagnostic. The `6.0` trial reduced reach-collapse snaps but made by-side elbow bending too rigid in worn-headset VR Preview.
- Keep the profile 4 final arm pose write frame-coherent (`mp.MediaPipeArmTargetHalfLife=0.0`, `mp.MediaPipeArmRotationHalfLife=0.0`, `mp.MediaPipeArmRotationMaxStepDegrees=0.0`, `mp.MediaPipeArmRotationMaxSpeedDegreesPerSecond=0.0`) unless deliberately isolating a new smoothing diagnostic. Endpoint filtering belongs before the constrained solve; the solved arm frame should not be rate-limited again.
- Keep direct Quest wrist-roll helper ownership disabled in the startup default. `mp.MediaPipeDriveArmTwistBones=1` is the standard target-skeleton helper pass, and `mp.MediaPipeDriveMetaHumanArmHelpers=0` keeps Wallace's broader MetaHuman sidecar/corrective families out of startup. Use `mp.MediaPipeDriveArmTwistBones=0` as the standard-helper isolation switch, and only set `mp.MediaPipeDriveMetaHumanArmHelpers=1` for a deliberate diagnostic trial. Use `mp.AutoQuestArmRollDiagnostic=1` only for the direct upper-arm roll test. If `mp.QuestWristUpperArmRollDriveTwistHelpers=1` is being tested, require `mp.QuestWristTrace=1` and inspect `upperArmTargetDeg`, `upperArmAppliedDeg`, `upperArmStepDeg`, `upperArmMaxStepDeg`, and `upperArmRateClamp` before calling it smoother than the failed raw-roll trial.

## Quick Regression Checklist

Before touching this system, answer these from a fresh VR Preview log:

1. Is the view pawn `DefaultPawn_0` with `springArms=0 cameras=0`?
2. Was `BP_ThirdPersonCharacter_C_0` destroyed after switching to `DefaultPawn_0`?
3. Did yaw get captured from `settled HMD yaw`, not from "first valid HMD yaw"?
4. Is `forwardOffset=0.0`?
5. Is Wallace `avatarYaw` approximately `viewerYaw - 90`?
6. Are there no `camera pinned` lines in stable embodied mode?
7. Are legs, pelvis translation, arm IK, and forced wrist IK still off?
8. Is stable embodied body still on with the current split (`AutoQuestEmbodiedStableBody=1`, `MediaPipeDriveClavicles=0`, `MediaPipeDriveSpine=0`, lower body/pelvis off)?
9. Is the default arm path still the working checkpoint (`AutoQuestArmReachAssistProfile=4`, `QuestArmMode=3`, `QuestWristPositionBlend=1.0`) unless an explicit rollback is being tested?
10. Do recenter rows include `horizontalErrorBefore` / `rawZErrorBefore`, and do room-scale rows include `appliedCm`, `deadband`, `maxStep`, and `capped`?
11. Do `mp.QuestWristSolve` / analyzer rows avoid the old ~300 degree `twistJumpDeg` wrist-roll flip after the semantic roll unwrap?
12. Did the wearer visually confirm chest/body under the headset view in VR Preview?

If any answer is no, do not tune arms or CVars. Fix the coordinate-space regression first.
