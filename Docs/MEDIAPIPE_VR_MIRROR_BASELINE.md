# MediaPipe VR Mirror Baseline

Last updated: 2026-05-17

## Superseded By Current Wallace Checkpoint

This file is historical. The current authoritative Wallace Quest VR checkpoint is:

```text
Docs/WALLACE_QUEST_VR_CURRENT_DEFAULTS_HANDOFF.md
Docs/WALLACE_QUEST_VR_EMBODIMENT_GUARDRAILS.md
Docs/MEDIAPIPE_REFACTOR_STATE_2026-05-17.md
```

As of the 2026-05-15 18:35 checkpoint, the best headset state is embodied Wallace with `mp.AutoQuestArmReachAssistProfile=4`. That profile uses adaptive filtered Quest wrist correction and keeps arm IK and legs off. Do not use this older mirror baseline to override the current Wallace handoff.

## Historical Status

The VR Preview mirror-facing baseline is working well enough to preserve.

Historical mirror-baseline intended behavior:

- the Quest wearer sees Manny in front of them like a mirror
- the mirror camera starts from a fixed Unreal station, not from wherever the headset happens to be in the real room
- MediaPipe webcam pose drives the body
- Quest hand tracking is enabled only for finger bones in the current progressive test
- Quest wrist position and Quest hand/wrist rotation are disabled
- MediaPipe arm IK is disabled for this baseline
- MediaPipe hand rotation is disabled for this baseline
- the VR auto path restores the same stable MediaPipe retarget profile used by the editor visual-cycle command

This document records the fix because the earlier failure mode was misleading: the Manny actor was being placed and rotated correctly, but the visible torso could still face sideways because the MediaPipe landmark cloud was solved in a different world yaw.

## User-Facing Test

For the current baseline, no console commands should be needed.

1. Open `D:\Epic\Unreal_Projects\TestingKit3\TestingKit3.uproject`.
2. Put on the Quest 3.
3. Press **VR Preview**.
4. Stand in view of the webcam.
5. Confirm Manny is in front of the headset view and facing the wearer.

Do not validate the headset view from the desktop PIE mirror window. The desktop window is useful for logs and rough placement, but it is not proof of what the headset wearer sees.

## Historical Auto Profile

The automatic Quest webcam profile is applied from:

```text
Source/MediaPipeDriver/MediaPipeDriver.cpp
```

Important baseline values:

```text
mp.AutoQuestMirrorLockMannyYaw=1
mp.MediaPipePoseYawAlignToActor=1
mp.MediaPipePoseYawAlignHalfLife=0.30
mp.MediaPipePoseYawAlignMaxSpeedDegreesPerSecond=120
mp.MediaPipePoseYawAlignRejectJumpDegrees=55
mp.MediaPipeTorsoUseActorForward=1
mp.MediaPipeTorsoDebug=1
mp.MediaPipeInputMaxDimension=512
mp.MediaPipeSourceSmoothingHalfLife=0.16
mp.MediaPipeSourceSmoothingFastSpeed=6
mp.QuestHandTracking=1
mp.QuestHandDriveFingerBones=1
mp.QuestFingerDebug=0
mp.QuestHandHud=0
mp.QuestHandDebug=0
mp.QuestWristDebug=0
mp.QuestHandRotationBlend=1
mp.QuestWristPositionBlend=0
mp.MediaPipeUseArmIK=0
mp.MediaPipeDriveHandRotation=0
```

The 2026-05-14 Wallace freeze supersedes the older finger-only mirror baseline. `mp.MediaPipeTorsoUseActorForward=1` and `mp.QuestHandRotationBlend=1` are part of the current default VR Preview profile.

`mp.MediaPipePoseYawAlignToActor` rotates the whole cached MediaPipe landmark cloud around the torso midpoint before the retarget solve. This keeps the source pose internally coherent while aligning the body yaw to the fixed mirror-facing Manny actor. The correction is smoothed and speed-limited; large one-frame source yaw jumps are rejected so MediaPipe pose noise cannot instantly twist the whole body.

## Implementation

Runtime path:

```text
Source/MediaPipeDriver/MediaPipePoseDrivenAnimInstance.cpp
Source/MediaPipeDriver/MediaPipePoseDrivenAnimInstance.h
Source/MediaPipeDriver/MediaPipePoseDrivenSolverState.h
Source/MediaPipeDriver/MediaPipePoseDrivenAnimInstance_TorsoBasis.inl
Source/MediaPipeDriver/MediaPipePoseDrivenAnimInstance_BodyPoseSolve.inl
Source/MediaPipeDriver/MediaPipePoseDrivenAnimInstance_QuestSpaceMapping.inl
Source/MediaPipeDriver/MediaPipePoseDrivenAnimInstance_QuestHandRotation.inl
Source/MediaPipeDriver/MediaPipePoseDrivenAnimInstance_QuestFingerDrive.inl
Source/MediaPipeDriver/MediaPipePoseDrivenAnimInstance_QuestArmSolve.inl
```

The anim node reads the latest MediaPipe pose frame, builds `PoseWorld[]`, then optionally applies yaw alignment when `mp.MediaPipePoseYawAlignToActor` is enabled. The old single-file implementation has been refactored; use `Docs/MEDIAPIPE_REFACTOR_STATE_2026-05-17.md` for the current source map and line counts.

The alignment uses:

- raw MediaPipe torso forward from shoulders/hips
- target actor/component forward from `TargetCompTransform`
- a yaw-only delta around world up
- the midpoint between shoulders and hips as the rotation anchor

After the landmark cloud is rotated, normal torso, spine, arm, and leg retargeting consume the corrected `PoseWorld[]`.

Mirror station path:

```text
Source/MediaPipeDriver/MediaPipeDriver.cpp
```

The auto mirror system:

- resolves a fixed viewer/camera station
- optionally calibrates station yaw from the first valid HMD yaw
- pins the camera/HMD view to the station when live HMD pose is available
- places Manny at a fixed distance in front of the station
- locks Manny actor yaw to face the station camera
- enables MediaPipe pose yaw alignment
- applies the stable MediaPipe-only profile before disabling Quest hand/wrist driving

## Evidence From The Fix

The original print evidence showed actor placement was not the root problem:

```text
Auto Quest mirror: fixed Manny station ... stationYaw=-164.6 actualMannyYaw=-164.6 lockMannyYaw=1
mp.TorsoDebug ... forward=V(X=0.09, Y=0.96, Z=-0.25) actorForward=0
```

That means the actor was facing the mirror station, but the MediaPipe torso basis was still pointing sideways.

Controlled validation forced the actor yaw to 180 degrees while the source pose wanted roughly 74-97 degrees. The log showed the yaw correction being consumed gradually by the downstream torso solve:

```text
mp.PoseYawAlign: actor=MP_LiveMediaPipeManny enabled=1 applied=1 rejected=0 recentered=0 rawForward=V(X=-0.60, Y=0.80) desiredActorForward=V(X=-1.00) correctedForward=V(X=-0.91, Y=0.41) rawYaw=126.8 desiredYaw=180.0 targetDeltaYaw=53.2 appliedDeltaYaw=29.3 remainingYawError=23.93 dt=0.250 actorYaw=180.0 sourceYaw=0.0
mp.TorsoDebug: actor=MP_LiveMediaPipeManny ... forward=V(X=-0.91, Y=0.40, Z=-0.10) actorForward=0
mp.PoseYawAlign: actor=MP_LiveMediaPipeManny enabled=1 applied=1 rejected=0 recentered=0 rawForward=V(X=0.27, Y=0.96) desiredActorForward=V(X=-1.00) correctedForward=V(X=-1.00, Y=0.00) rawYaw=74.1 desiredYaw=180.0 targetDeltaYaw=105.9 appliedDeltaYaw=105.8 remainingYawError=0.11 dt=0.250 actorYaw=180.0 sourceYaw=0.0
mp.TorsoDebug: actor=MP_LiveMediaPipeManny ... forward=V(X=-1.00, Y=0.00, Z=-0.06) actorForward=0
```

That proves the anim node is not merely logging a CVar. It rotates the pose frame, smooths the yaw correction across frames, and the downstream torso solve consumes the corrected forward vector.

The rebuilt PIE auto-profile check also spawned the expected runtime actors and applied the baseline CVars:

```text
actors: MP_LiveMediaPipeVideo at [0,0,0], MP_LiveMediaPipeManny at [200,1200,2] yaw=180
mp.QuestHandTracking=1
mp.QuestHandDriveFingerBones=1
mp.QuestFingerDebug=0
mp.QuestHandRotationBlend=1
mp.QuestWristPositionBlend=0
mp.MediaPipeUseArmIK=0
mp.MediaPipeDriveHandRotation=0
mp.MediaPipePoseYawAlignToActor=1
mp.MediaPipeInputMaxDimension=512
```

## Useful Logs

For mirror placement:

```text
Auto Quest mirror: calibrated fixed station yaw from HMD yaw=...
Auto Quest mirror: reset HMD origin to fixed viewer yaw=...
Auto Quest mirror: fixed Manny station camera=... viewerYaw=... manny=... stationYaw=... actualMannyYaw=... lockMannyYaw=1 distance=...
Auto Quest mirror: camera pinned target=... camera=... hmd=... hmdYaw=... viewerYaw=... error=...
```

For MediaPipe yaw alignment:

```text
mp.PoseYawAlign: actor=... enabled=1 applied=1 rejected=... recentered=... rawForward=... desiredActorForward=... correctedForward=... rawYaw=... desiredYaw=... targetDeltaYaw=... appliedDeltaYaw=... remainingYawError=... dt=...
mp.TorsoDebug: actor=... forward=... actorForward=0 uprightBlend=0.00 maxTiltDeg=89.0
```

For legacy Quest finger-only validation logs that should not be used as current Wallace default proof:

```text
mp.QuestFingerSolve: actor=MP_LiveMediaPipeManny side=L available=1 tracked=1 drive=1 appliedBones=... validRefBones=... mode=curlOnly alignToMediaHand=... wristPositionBlend=0.00 handRotationBlend=0.00 questWristWorld=...
mp.QuestFingerSolve: actor=MP_LiveMediaPipeManny side=R available=1 tracked=1 drive=1 appliedBones=... validRefBones=... mode=curlOnly alignToMediaHand=... wristPositionBlend=0.00 handRotationBlend=0.00 questWristWorld=...
```

Good evidence:

- `actualMannyYaw` matches `stationYaw`
- `mp.MediaPipePoseYawAlignToActor` is `1`
- `mp.MediaPipeTorsoUseActorForward` is `1`
- `mp.PoseYawAlign` reports `applied=1`
- `recentered` should be `0` after startup, and `dt` should be nonzero
- `appliedDeltaYaw` should move toward `targetDeltaYaw` instead of jumping every frame
- `remainingYawError` should fall toward `0.00`
- the following `mp.TorsoDebug forward` points near `correctedForward`
- `mp.QuestFingerSolve` appears only when Quest hand data is available in the headset session
- `mp.QuestFingerSolve appliedBones` is greater than zero for each tracked hand
- `mp.QuestFingerSolve wristPositionBlend=0.00` and hand rotation follows the current Wallace freeze profile

Bad evidence:

- no `mp.PoseYawAlign` lines while the webcam is expected to track means the anim node is not receiving a valid MediaPipe pose frame
- `remainingYawError` staying large means the yaw correction is not being applied
- `actualMannyYaw` not matching `stationYaw` means the mirror actor lock is not active
- `mp.MediaPipeTorsoUseActorForward=0` means this doc or a manual command has fallen back to the older finger-only baseline

## What Not To Change Casually

Do not set these away from the current Wallace handoff defaults as first-line fixes:

```text
mp.MediaPipeUseArmIK -> do not set to 1
mp.QuestWristPositionBlend -> do not set to 1
mp.QuestWristSwingBlend=...
```

Those settings touch different problems. Re-enabling them before the current Wallace freeze baseline is verified makes it hard to separate body yaw, arm solve, wrist endpoint, and Quest hand calibration issues.

## Historical Quest Finger Step

The current progressive test has advanced one step past the pure MediaPipe baseline:

1. keep `mp.MediaPipePoseYawAlignToActor=1`
2. keep `mp.QuestWristPositionBlend=0`
3. keep `mp.QuestHandRotationBlend=1`
4. keep `mp.MediaPipeUseArmIK=0`
5. drive Quest hand rotation and finger bones with `mp.QuestHandTracking=1` and `mp.QuestHandDriveFingerBones=1`
6. verify `mp.QuestFingerSolve` logs and visible fingers while the body remains stable
7. only then re-enable Quest wrist endpoint mapping
8. validate Quest wrist mapping using `mp.QuestWristSolve` logs, not screenshots alone

The intended final ownership is still:

- MediaPipe owns torso, shoulders, elbows, general arm pole, and lower body
- Quest owns fingers and wrist orientation
- Quest wrist position should be mapped into the already yaw-aligned MediaPipe body space before it becomes an arm endpoint
