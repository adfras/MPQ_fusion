# Wallace Quest VR Arm Rollback Analysis - 2026-05-17

Superseded by `Docs/WALLACE_QUEST_VR_CURRENT_DEFAULTS_HANDOFF.md` after the 2026-05-17 SafeSetCS arm-reach checkpoint.

This note is historical. It records the temporary arm-control rollback after the embodied camera regression was fixed but Wallace's arms remained laggy, drifting, or not landing where the headset wearer expected. Do not treat the rollback values below as current defaults unless the user explicitly asks to return to profile 0 / mode 0.

Current source-layout checkpoint:

```text
Docs/MEDIAPIPE_REFACTOR_STATE_2026-05-17.md
```

The current implementation no longer lives only in `MediaPipePoseDrivenAnimInstance.cpp`; Quest arm, Quest hand rotation, Quest space mapping, runtime CVars, and solver state are split into the files documented in the refactor-state note.

## Historical Conclusion

The last clearly preserved working arm ownership split was the 2026-05-14 Wallace baseline:

```text
MediaPipe owns torso, shoulders, elbows, and wrist position.
Quest owns hand orientation and finger bones.
Quest wrist position does not drive the arm endpoint.
Arm IK, leg driving, leg IK, and pelvis translation stay off.
```

This was the conclusion at the time of the rollback. It is no longer the current startup path. The current checkpoint is profile 4 / `mp.QuestArmMode=3`, using the HMD-relative avatar Quest wrist endpoint, after the SafeSetCS rebuild made Wallace's arms move forward with the Quest hands again.

## Evidence

The 2026-05-14 VR Preview logs show Wallace hand/wrist tracking with Quest wrist position traced but not applied:

```text
Saved/Logs/TestingKit3_2-backup-2026.05.14-08.15.40.log
Saved/Logs/TestingKit3_2-backup-2026.05.14-10.32.49.log
requestedBlend=0.00
positionApplied=0
requireTrackedApply=0
hmdPose=1
mediaHead=1
handLocal=1
```

That means the Quest hand path was active, but the arm endpoint stayed MediaPipe-owned. This matches the accepted 2026-05-14 freeze in `Docs/QUEST_WRIST_SOLVE_FREEZE_2026-05-11.md`.

The later profile 4 logs moved to Quest wrist endpoint authority:

```text
Saved/Logs/TestingKit3-backup-2026.05.16-14.29.40.log
questArmMode=2
requestedBlend=0.82
positionApplied=1
questArmSolve=1
wristSwingAppliedDeg can reach 140.0
mapped/final wrist offsets can become large
```

The 2026-05-17 headset trace then showed endpoint dropout during the same arm path:

```text
Saved/Logs/TestingKit3-backup-2026.05.17-00.37.02.log
positionApplied=0
questTracked=0
hmdPose=0
mediaHead=0
questArmSolve=0
```

That is consistent with the user-visible arm drift/snap/lag complaint. The active drift report is about the arms when the user moves or extends their arms; it is an arm endpoint path problem, not proof of a walking or room-scale-body-follow problem.

## Historical Default Rollback

The temporary rollback default was:

```text
mp.AutoQuestArmReachAssistProfile=0
mp.QuestArmMode=0
mp.QuestWristPositionBlend=0.0
mp.QuestWristReachAssist=0
mp.QuestWristDriftGuard=0
mp.QuestConstrainedArmSolve=0
mp.QuestWristPositionAdaptiveFilter=0
mp.QuestWristMaxRelativeDeltaCm=55.0
mp.QuestPalmMode=2
mp.QuestHandRotationHalfLife=0.0
mp.QuestHandRotationLostTrackingGraceSeconds=0.45
mp.MediaPipeArmTargetHalfLife=0.08
mp.MediaPipeArmRotationHalfLife=0.06
mp.MediaPipeUseArmIK=0
mp.QuestWristForceArmIK=0
mp.MediaPipeDriveLegs=0
mp.MediaPipeUseLegIK=0
mp.MediaPipeDrivePelvisTranslation=0
```

The embodied camera/body fixes remain separate and should not be rolled back:

```text
mp.AutoQuestEmbodiedView=1
mp.AutoQuestEmbodiedAnchorMode=1
viewPawn=DefaultPawn_0
springArms=0
cameras=0
forwardOffset=0.0
no recurring camera pinned lines
```

## Historical Verification Context

Normal PIE can only prove that the default CVars are applied. It is not proof that the headset embodiment is fixed.

Fresh compiled normal-PIE smoke at that time showed the rollback profile was what startup applied:

```text
Saved/Logs/TestingKit3.log
2026.05.17-01.13.46 UTC
Auto Quest profile applied: armProfile=0 stableBody=0 clavicles=1 spine=1 armIK=0 forceArmIK=0 legs=0 legIK=0 pelvisTranslation=0 questArmMode=0 questPalmMode=2 wristBlend=0.00 wristGrace=0.35 wristRequireTracked=0 wristTrace=0 reachAssist=0 driftGuard=0 constrainedArmSolve=0 wristFilter=0 armTargetHL=0.08 armRotHL=0.06 handRotHL=0.00 handRotGrace=0.45 wristMaxRel=55.0
Auto Quest mirror: fixed viewer pawn=DefaultPawn_0 ... springArms=0 cameras=0
Auto Quest embodied: ... viewPawn=DefaultPawn_0 ... forwardOffset=0.0
```

That smoke is a compiled-default/logging check only. It does not prove the arms feel correct in the Quest headset.

The current log gate still lives here, but it now expects the profile 4 / mode 3 HMD-relative avatar path:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\Tools\CheckWallaceQuestVrEmbodimentLog.ps1 -LogPath .\Saved\Logs\TestingKit3.log -RequireWornHeadsetTrace
```

On the old 2026-05-17 normal-PIE smoke it passed the profile/view-pawn checks and warned only that there were no `mp.QuestWristSolve` rows, which is expected without a worn-headset wrist trace. The current `-RequireWornHeadsetTrace` mode is stricter: it must see Wallace `mp.QuestWristSnapshot` rows with `hmdPose=1` and a tracked Quest hand, plus Wallace `mp.QuestWristSolve` rows proving `questArmMode=3`, `positionApplied=1`, `requestedBlend=1.00`, `calib=HMD_AVATAR`, and `handLocal=1`.

The historical rollback gate was a worn-headset VR Preview run showing:

```text
Auto Quest profile applied: armProfile=0 ... questArmMode=0 ... wristBlend=0.00 ... reachAssist=0 driftGuard=0 constrainedArmSolve=0 wristFilter=0 armTargetHL=0.08 armRotHL=0.06 handRotHL=0.00 handRotGrace=0.45 wristMaxRel=55.0
Auto Quest mirror: fixed viewer pawn=DefaultPawn_0 ... springArms=0 cameras=0
Auto Quest embodied: ... viewPawn=DefaultPawn_0 ... forwardOffset=0.0
Stable embodied recenter logs include `horizontalErrorBefore` / `rawZErrorBefore`, proving raw HMD Z is not what triggers wake recenter.
Room-scale follow logs include `appliedCm`, `deadband=8.0`, `maxStep=12.0`, and `capped`, proving one-frame horizontal tracking snaps are not applied as full avatar shoves.
No camera pinned lines in stable embodied mode.
No arm IK, legs, leg IK, or pelvis translation.
```

Current user-facing success requires Wallace to stay embodied behind the eyes while the arms move forward with the Quest hands, using the current profile 4 / mode 3 checkpoint documented in `Docs/WALLACE_QUEST_VR_CURRENT_DEFAULTS_HANDOFF.md`.
