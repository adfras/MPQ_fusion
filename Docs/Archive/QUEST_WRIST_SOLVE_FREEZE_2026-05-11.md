# Quest Wrist Solve Freeze - 2026-05-14

## Authoritative Current Handoff

Read `Docs/WALLACE_QUEST_VR_CURRENT_DEFAULTS_HANDOFF.md` first. It is the current source of truth for the Wallace Quest VR defaults and supersedes older Manny-only, finger-only, IK-on, visible-calibration, or twist-helper-on notes in this document.

Current source-layout checkpoint:

```text
Docs/MEDIAPIPE_REFACTOR_STATE_2026-05-17.md
```

## Historical Best Default Baseline - 2026-05-14

- User VR Preview feedback: "Okay this is the best state I can get so far so set this as the defaults and it needs to be remembered."
- This section records the accepted 2026-05-14 baseline. It is historical context now; do not restore it unless the user explicitly asks. The current active Wallace default is the 2026-05-17 profile 4 / `mp.QuestArmMode=3` checkpoint in `Docs/WALLACE_QUEST_VR_CURRENT_DEFAULTS_HANDOFF.md`.
- Default avatar is now MetaHuman Wallace:
  - `mp.AutoQuestAvatar=1`
  - Wallace body proof from PIE log: `Auto Quest webcam: using MetaHuman Wallace body mesh=Body asset=m_med_unw_body`
  - Spawn proof from PIE log: `avatar=Wallace`
- Visible calibration phase has been removed from `AMediaPipePoseDrivenSkeletalActor`:
  - Wallace is not hidden behind a guide/reveal step.
  - Removed old presentation CVars: `mp.QuestCalibrationFlow`, `mp.QuestCalibrationHideMannyUntilAccepted`, `mp.QuestCalibrationGuide`, `mp.QuestCalibrationConfirmSeconds`.
  - Wrist calibration is non-blocking by default with `mp.QuestWristCalibrationGate=0`; no HUD and no avatar hide/reveal phase.
  - Auto Quest uses `mp.QuestWristRelativeCalibration=1` so the visible hand orientation preserves the initial MediaPipe wrist offset instead of forcing the raw Quest basis directly.
  - Auto Quest uses `mp.MediaPipeUseArmIK=0`, matching the Wallace trace in `TestingKit3-backup-2026.05.14-09.19.57.log` where `armIK=0` for `MP_LiveMetaHumanWallace`.
- Torso/body stability defaults remain active and must not be reset to the raw MediaPipe torso basis:
  - `mp.MediaPipeTorsoUseActorForward=1`
  - `mp.MediaPipeTorsoUprightBlend=0.85`
  - `mp.MediaPipeTorsoMaxTiltDegrees=20`
  - `mp.MediaPipePoseYawAlignToActor=1`
- Quest hand/wrist defaults for this freeze:
  - `mp.QuestHandRotationHalfLife=0.0`
  - `mp.QuestHandRotationMaxStepDegrees=0.0`
  - `mp.QuestHandRotationMaxDeltaFromMediaPipeDegrees=180.0`
  - `mp.QuestHandRotationRequireTracked=1`
  - `mp.QuestWristRequireTrackedForApply=0`
  - `mp.QuestWristDriveTwistCorrection=0`
  - `mp.QuestWristTwistCorrectionBlend=1.0`
  - `mp.QuestWristTwistCorrectionMaxDegrees=35.0`
  - `mp.QuestWristMaxTwistDegrees=170.0`
  - `mp.QuestWristMaxSwingDegrees=140.0`
- Right-wrist runaway fix is part of this baseline:
  - semantic roll state is normalized with `FRotator::NormalizeAxis(QuestTwistDeg)` before storing `RotationSemanticRollLastTwistDeg`
  - this prevents multi-turn values such as `-501 deg` from being treated as a real wrist pose before the clamp
- Left-arm deformation fix is part of this baseline:
  - Quest hand rotation holds during untracked frames via `mp.QuestHandRotationRequireTracked=1`
  - MetaHuman lower-arm twist helper correction is disabled by default via `mp.QuestWristDriveTwistCorrection=0`; this keeps hand roll responsive while preventing helper-bone candy-wrapper skin deformation.
- Current rebuilt binary timestamp observed during this freeze:
  - `Binaries\Win64\UnrealEditor-MediaPipeDriver.dll` at `14/05/2026 5:05:26 PM`
  - `Binaries\Win64\UnrealEditor-MediaPipeDriverEditor.dll` at `14/05/2026 12:20:54 PM`
- Live editor verification at `2026-05-14 09:09 UTC log time` printed the `CODEX_FREEZE_CVAR` rows for all values above.

## Implementation Anchors

- `Source/MediaPipeDriver/MediaPipeDriver.cpp`
  - default `mp.AutoQuestAvatar=1`
  - Auto Quest profile sets the current hand, wrist, torso, and calibration defaults
- Current split AnimInstance/runtime files:
  - `Source/MediaPipeDriver/MediaPipePoseDrivenAnimInstance.cpp`
  - `Source/MediaPipeDriver/MediaPipePoseDrivenAnimInstance.h`
  - `Source/MediaPipeDriver/MediaPipePoseDrivenSolverState.h`
  - `Source/MediaPipeDriver/MediaPipeRuntimeCVars.h`
  - `Source/MediaPipeDriver/MediaPipeRuntimeCVars.cpp`
  - `Source/MediaPipeDriver/MediaPipePoseDrivenAnimInstance_QuestSpaceMapping.inl`
  - `Source/MediaPipeDriver/MediaPipePoseDrivenAnimInstance_QuestHandRotation.inl`
  - `Source/MediaPipeDriver/MediaPipePoseDrivenAnimInstance_QuestFingerDrive.inl`
  - `Source/MediaPipeDriver/MediaPipePoseDrivenAnimInstance_QuestArmSolve.inl`
  - Quest hand direct-follow CVar: `mp.QuestHandRotationHalfLife`
  - tracked-only hand rotation guard: `mp.QuestHandRotationRequireTracked`
  - forearm helper clamp: `mp.QuestWristTwistCorrectionMaxDegrees`
  - semantic roll normalization before storing last twist

## Older Baseline Notes

## Current User-Verified State

- User VR Preview feedback after the twist-correction pass: "thats much better."
- Lowerarms are no longer broken by Quest wrist motion.
- Remaining known issue: some wrist twist artifacts are still possible, but the current state is the best working baseline from this pass.
- Freeze request: do not keep tuning or replacing the solve tonight. Future work should start from this baseline and make one measured change at a time.

## Frozen Implementation

- Project: `D:\Epic\Unreal_Projects\TestingKit3`
- Main orchestration file: `Source/MediaPipeDriver/MediaPipePoseDrivenAnimInstance.cpp`
- Current solve clusters: `Source/MediaPipeDriver/MediaPipePoseDrivenAnimInstance_*.inl`
- Solver state objects: `Source/MediaPipeDriver/MediaPipePoseDrivenSolverState.h`
- Trace helper: `Tools/AnalyzeQuestWristRollLog.ps1`
- Current rebuilt binary timestamp observed during freeze:
  - `Binaries\Win64\UnrealEditor-MediaPipeDriver.dll` at `12/05/2026 5:06:39 PM`
  - `Binaries\Win64\UnrealEditor-MediaPipeDriverEditor.dll` at `12/05/2026 5:06:40 PM`

The active wrist path is:

- Quest/OpenXR still owns hand and finger tracking.
- MediaPipe still owns the body, shoulder, elbow, and base arm pose.
- Arm IK is kept off for the wrist verification path.
- Current wrapper-reduction baseline avoids post-rotating Manny's main lowerarm from Quest wrist roll. The Quest hand target is smoothed and applied as a hand-local rotation relative to Manny's current `lowerarm_*` transform instead of as an independent component-space hand override:
  - compact proof target includes `handLocal=1`
  - `mp.QuestWristTwistDrivesForearm=0`
  - `mp.QuestWristForearmTwistBlend=0.0`
  - `mp.QuestWristForearmMaxTwistDegrees=55`
  - `mp.QuestWristForearmRollDriveTwistHelpers=0`
  - `mp.QuestWristDriveTwistCorrection=0`
  - `mp.QuestWristTwistCorrectionBlend=1.0`
  - `mp.QuestWristTwistBlend=1`
  - `mp.QuestWristSwingBlend=1`
- Quest wrist twist correction is available but off by default for Wallace. The final hand-local Quest wrist rotation remains active; only the MetaHuman lowerarm helper deformation layer is disabled.
- Compact proof target includes `twistCorrection=0 lowerarmMainDriven=0` while `handLocal=1` and hand rotation remains applied when Quest hands are tracked.
- Current basis-space wrist test compares Quest palm basis in forearm-local space before converting back to Manny:
- `mp.QuestWristUseBasisDelta=1`
- compact proof target includes `semanticBasis=1`
- Semantic-basis wrist roll now uses projected palm-normal roll confidence; `mp.QuestWristSemanticRollMinPalmProjection=0.45` holds prior roll when wrist flexion makes forearm-axis twist underdetermined.
- `mp.MediaPipeDriveSpine` exists as a diagnostic switch, but Auto Quest keeps spine driving on so Manny still faces the mirror user.
- For the Quest-hand integration pass, MediaPipe lower-body driving should stay isolated:
  - `mp.MediaPipeDriveLegs=0`
  - `mp.MediaPipeDrivePelvisTranslation=0`
  - `mp.MediaPipeUseLegIK=0`
  - `mp.MediaPipeUseFkRootGrounding=0`
  - `mp.MediaPipeDriveFootRotation=0`
- Right wrist uses the forearm-local semantic roll solve:
  - `bUseForearmLocalSemanticRollSolve = bUseSemanticRollSolve && !bIsLeft`
  - compact proof target: `side=R semantic=1 semanticLocal=1 armIK=0 forceIK=0`
- Left wrist remains on the non-local semantic path for now:
  - compact proof target: `side=L semantic=1 semanticLocal=0`

## Changes That Got Here

1. Backed out the bad lowerarm roll attempt.
   - Do not rotate the main `lowerarm_*` bones from Quest wrist roll.
   - Auto Quest profile now keeps `mp.QuestWristTwistDrivesForearm=0`.

2. Preserved Manny facing/body behavior.
   - Auto Quest profile keeps `mp.MediaPipeDriveSpine=1`.
   - Lower body remains isolated for hand/wrist work: legs, pelvis translation, leg IK, FK root grounding, and foot rotation are off.

3. Changed hand application from component-space override to lowerarm-local hand solve.
   - The Quest hand target is converted relative to Manny's current `lowerarm_*`.
   - The smoothed result is rebuilt into component space only after the lowerarm-local solve.
   - Proof field: `handLocal=1`.

4. Added, then disabled by default, the deformation-layer twist correction.
   - Source is the final Manny hand-local twist after the Quest solve, logged as `sourceHandTwistDeg`.
   - Only `lowerarm_twist_01_*` and `lowerarm_twist_02_*` are driven.
   - Helper weights are measured from each helper bone's actual position along the forearm.
   - Helper rotations use cached reference orientation relative to `lowerarm_*`.
   - Main `lowerarm_*` bones remain untouched; proof field: `lowerarmMainDriven=0`.
   - Wallace default now keeps `mp.QuestWristDriveTwistCorrection=0` because this helper layer can candy-wrapper MetaHuman forearm skin when the wrist rolls away from the body.

5. Updated objective logging and analysis.
   - `mp.QuestWristRollCompact` now includes `handLocal`, `twistCorrection`, `lowerarmMainDriven`, and `sourceHandTwistDeg`.
   - The analyzer gate now expects `handLocal=1`, `twistCorrection=0`, and `lowerarmMainDriven=0` for the Wallace default.

## Build Note

The current baseline was rebuilt successfully after closing the editor so the MediaPipe DLLs were not locked:

```powershell
& 'D:\Epic\UE_5.7\Engine\Build\BatchFiles\Build.bat' TestingKit3Editor Win64 Development -Project='D:\Epic\Unreal_Projects\TestingKit3\TestingKit3.uproject' -WaitMutex -NoHotReloadFromIDE -NoUBA
```

## Verification Commands For Next Pass

When the headset is in Quest 3 VR Preview with hand tracking active, reset calibration from a known neutral start pose and collect proof:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\Tools\RunQuestWristObjectiveGate.ps1 -ResetNow -WaitSeconds 20 -TailRows 24
```

Note: this tool now applies the current Wallace profile 4 / `mp.QuestArmMode=3` objective gate before analyzing the wrist. It is not a command for recreating this older historical freeze state.

Direct analyzer:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\Tools\AnalyzeQuestWristRollLog.ps1 -Side R -AfterLastReset -ObjectiveGate -TailRows 24
```

If rebuilding is needed, stop VR Preview and close the editor first so the DLL is not locked:

```powershell
& 'D:\Epic\UE_5.7\Engine\Build\BatchFiles\Build.bat' TestingKit3Editor Win64 Development -Project='D:\Epic\Unreal_Projects\TestingKit3\TestingKit3.uproject' -WaitMutex -NoHotReloadFromIDE -NoUBA -MaxParallelActions=1
```

## Next Investigation Target

Start the next session from this exact baseline. Do not begin by changing clamps or blends.

The current Wallace default keeps the twist-correction helper layer off. If the current baseline still shows unacceptable wrist wrapping, the next real issue is likely the deformation setup/corrective-shape layer, not the Quest tracking path. Before changing the solve again, capture compact rows immediately after `mp.ResetQuestWristCalibration` and compare:

- capture compact rows immediately after `mp.ResetQuestWristCalibration`
- compare `questBasisFwdErrDeg`, `questBasisUpErrDeg`, `questBasisRollFwdErrDeg`, and `questBasisRollUpErrDeg`
- check whether bad starts correlate with large calibration basis error or a shifted MediaPipe/Manny hand basis
- only then decide whether to change calibration reference, pose gating, twist-helper sign/weights, or move into corrective pose/morph work

Keep legs out of the wrist-debug loop until the camera frame includes them. When leg work resumes, add reliability gating before re-enabling `mp.MediaPipeDriveLegs`.
