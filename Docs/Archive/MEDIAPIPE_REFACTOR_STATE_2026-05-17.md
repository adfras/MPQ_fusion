# MediaPipe Refactor State - 2026-05-17

This is the source-layout checkpoint after the MediaPipe/Quest AnimInstance refactor in `D:\Epic\Unreal_Projects\TestingKit3`. Later checkpoint notes below keep it aligned with the current Wallace arm-chain work.

## Current Status

The old monolithic `MediaPipePoseDrivenAnimInstance.cpp` has been split into cohesive implementation clusters and small helper/test units. The main AnimInstance file is no longer the only place to look for solve behavior.

Original checkpoint headline numbers from 2026-05-17:

```text
Refactor-owned source files: 32
Refactor-owned total LoC: 13,759
MediaPipePoseDrivenAnimInstance.cpp LoC: 1,780
MediaPipePoseDrivenAnimInstance.h LoC: 794
```

Line count is still only a smell. The important improvement is that runtime state is now owned by named solver state objects, and major solve areas have clear file boundaries.

## Current Validation

Validated on 2026-05-17:

```powershell
& 'D:\Epic\UE_5.7\Engine\Build\BatchFiles\Build.bat' TestingKit3Editor Win64 Development -Project='D:\Epic\Unreal_Projects\TestingKit3\TestingKit3.uproject' -WaitMutex -NoHotReload
```

Result:

```text
Result: Succeeded
```

Automation:

```powershell
& 'D:\Epic\UE_5.7\Engine\Binaries\Win64\UnrealEditor-Cmd.exe' 'D:\Epic\Unreal_Projects\TestingKit3\TestingKit3.uproject' -unattended -nop4 -nosplash -nullrhi -stdout -FullStdOutLogOutput -ExecCmds='Automation RunTests TestingKit3.MediaPipe; Quit' -TestExit='Automation Test Queue Empty' -log
```

Result:

```text
TestingKit3.MediaPipe: 29 tests found, all passed, exit code 0
```

Bridge:

```powershell
npm run smoke
Invoke-RestMethod -Uri 'http://127.0.0.1:8765/status' -Method Get
```

Result:

```text
AgentBridge smoke passed.
Project root: D:\Epic\Unreal_Projects\TestingKit3
ChiR24 connected: true
Flopperam connected: false
```

VR Preview was not launched by the agent during this 2026-05-17 refactor. VR Preview remains the required proof for headset embodiment, arm reach, and Quest hand/wrist behavior.

Later Wallace full arm-chain validation on 2026-05-22:

```text
TestingKit3Editor build: succeeded
TestingKit3.MediaPipe full automation suite: 47 tests found, all passed, exit code 0
Tools\CheckWallaceArmSourceGuards.ps1: passed
VR Preview/Oculus Mirror evidence: Saved/CodexAgent/QuestVrEvidence/full_arm_chain_vrpreview_20260522_101600
Worn/tracked Quest 3 mirror frames: 7 captured
Wallace full-chain rows: 4,998 active mp.WallaceFullArmChain rows with mediaPipeArmUsed=0 throughout active rows
User worn-headset confirmation: "Okay I did a VR preview and its looking good"
```

That later checkpoint added a TestingKit3-native full arm-chain source for Wallace under the now-archived `mp.WallaceArmSource=1` compatibility alias. Current testing uses the generic profile-driven replacement in `Docs/METAHUMAN_PROFILE_DRIVEN_RETARGETING.md`; archive details are in `Docs/Archive/WALLACE_LEGACY_ARM_SOURCE_2026-05-22.md`.

Later profile-driven MetaHuman extrapolation work on 2026-05-22:

```text
TestingKit3Editor build: succeeded
Tools\CheckWallaceArmSourceGuards.ps1: passed
TestingKit3.MediaPipe.MetaHumanProfile: 5 focused tests found, all passed, including definition validation, Blueprint/body/face/post-process asset loading, configured DataAsset profile loading, missing-asset failure, missing-bone failure, and invalid-profile valid=0 logging
TestingKit3.MediaPipe.FullArmChain: 4 focused tests found, all passed, including profile-length target-pose retargeting
TestingKit3.MediaPipe broad automation: 54 tests found, 54 successes, no automation failures/errors
Profile layer: Source/MediaPipeDriver/MediaPipeMetaHumanProfile.*
Full-chain profile boundary: Source/MediaPipeDriver/MediaPipeMetaHumanArmRetargeter.* now outputs profile-length target world/component arm poses
Built-in profile ids: Wallace, Emory, Hudson, Kellan, Maria, Payton
Profile extension path: UMediaPipeMetaHumanRetargetProfile assets listed in UMediaPipeMetaHumanProfileSettings or mp.MetaHumanProfileAssetPaths
Profile offset path: FMediaPipeMetaHumanRetargetOffsets carries optional left/right full-arm-chain component-space offsets that are applied after target skeleton arm-length retargeting
New generic CVars: mp.MetaHumanActiveProfile, mp.MetaHumanProfileAssetPaths, mp.MetaHumanArmSource, mp.MetaHumanFullArmChainTrace
Manual profile prep helper: Tools\PrepareMetaHumanVrPreviewProfile.ps1 applies/prints profile CVars but does not start VR Preview
Manual profile log checker: Tools\CheckMetaHumanProfileVrPreviewLog.ps1 verifies active valid resolver rows and active left/right full-chain rows after the user-run VR Preview
Generic evidence wrapper: Tools\CaptureMetaHumanQuestVrEvidence.ps1
Authoritative profile handoff: Docs/METAHUMAN_PROFILE_DRIVEN_RETARGETING.md
Non-VR tool verification: the profile prep/checker scripts parse; prep helper print-only output was verified; the checker correctly fails the current non-VR log when no profile/full-chain proof rows exist
Diagnostic generalization: Quest hand comparison now emits visibleMetaHuman and Quest vs MetaHuman HUD text instead of Wallace-specific diagnostic labels
Post-diagnostic-generalization verification: TestingKit3Editor Win64 Development build succeeded; Tools\CheckWallaceArmSourceGuards.ps1 passed; TestingKit3.MediaPipe.Diagnostics.QuestHandCompare found 2 tests and both passed; TestingKit3.MediaPipe.MetaHumanProfile found 5 tests and all passed; TestingKit3.MediaPipe broad automation found 54 tests with 54 successes and 0 failures/errors
```

Wallace is still the headset-confirmed baseline. The new source path makes other MetaHumans selectable and validates them through profiles instead of Wallace-specific string checks. The agent must not start the profile matrix VR Preview runs; set CVars, then the user presses VR Preview manually.

Additional hand/finger validation on 2026-05-19:

```text
TestingKit3Editor build: succeeded
TestingKit3.MediaPipe.QuestFingerSolver: 4 focused tests found, all passed, exit code 0
Worn-headset VR Preview: user confirmed the parent-chain segment-direction hand/finger result looked good
```

The current Quest hand/finger default is documented first in:

```text
Docs/WALLACE_QUEST_VR_CURRENT_DEFAULTS_HANDOFF.md
```

That checkpoint keeps `mp.QuestHandDriveFingerBones=1`, `mp.QuestFingerJointRetarget=0`, and `mp.QuestFingerCurlOnly=0`. The active path is the `segmentDirection` solve in `MediaPipePoseDrivenAnimInstance_QuestFingerDrive.inl`, with live-parent retargeting and distal/tip damping.

## Source Layout

Main orchestration and declarations:

| File | LoC | Responsibility |
| --- | ---: | --- |
| `Source/MediaPipeDriver/MediaPipePoseDrivenAnimInstance.cpp` | 1,981 | Anim node orchestration, frame ingestion into evaluation, CVar/runtime policy integration, included solve clusters |
| `Source/MediaPipeDriver/MediaPipePoseDrivenAnimInstance.h` | 867 | Anim node declarations, properties, reference-pose fields, solver state members |
| `Source/MediaPipeDriver/MediaPipePoseDrivenSolverState.h` | 374 | Body, leg, arm, Quest wrist, Quest hand, and diagnostics runtime state objects |
| `Source/MediaPipeDriver/MediaPipePoseDrivenSolverStateTests.cpp` | 341 | Reset-contract tests for the new state objects |

Included AnimInstance implementation clusters:

| File | LoC | Responsibility |
| --- | ---: | --- |
| `MediaPipePoseDrivenAnimInstance_ReferenceCache.inl` | 755 | Reference-pose cache and bone reference extraction |
| `MediaPipePoseDrivenAnimInstance_BodyState.inl` | 6 | Foot-plant/body reset wrapper |
| `MediaPipePoseDrivenAnimInstance_TorsoBasis.inl` | 205 | Torso basis and body-space helper math |
| `MediaPipePoseDrivenAnimInstance_BodyPoseSolve.inl` | 583 | Pelvis/spine/neck/head body solve |
| `MediaPipePoseDrivenAnimInstance_LegSolve.inl` | 543 | Leg, foot, foot-plant, and lower-body solve |
| `MediaPipePoseDrivenAnimInstance_ArmTwist.inl` | 57 | Arm twist/helper bone drive |
| `MediaPipePoseDrivenAnimInstance_QuestArmWristSolve.inl` | 7 | Quest arm/wrist solve wrapper include |
| `MediaPipePoseDrivenAnimInstance_QuestSpaceMapping.inl` | 994 | Quest/HMD/media-space mapping and calibration-space helpers |
| `MediaPipePoseDrivenAnimInstance_QuestHandRotation.inl` | 1,801 | Quest hand orientation, palm basis, wrist/hand rotation solve |
| `MediaPipePoseDrivenAnimInstance_QuestFingerDrive.inl` | 952 | Quest finger drive integration, parent-chain segment-direction retarget, distal/tip damping, fallback curl/joint paths |
| `MediaPipePoseDrivenAnimInstance_QuestArmSolve.inl` | 2,665 | Quest arm endpoint, wrist-position, shoulder/elbow hint, arm reach solve, and Wallace full arm-chain handoff |

Extracted helpers and tests:

| File | LoC | Responsibility |
| --- | ---: | --- |
| `MediaPipeQuestHandCaptureReplayTooling.h` | 29 | Quest hand capture/replay declarations |
| `MediaPipeQuestHandCaptureReplayTooling.cpp` | 220 | Quest hand capture/replay/debug command support |
| `MediaPipeFullArmChainProvider.h` | 125 | TestingKit3 full arm-chain snapshot/provider declarations for the generic MetaHuman arm source mode |
| `MediaPipeFullArmChainProvider.cpp` | 346 | OpenXR body-tracking full arm-chain provider, snapshot publication, generic MetaHuman proof-row formatting, and archived Wallace compatibility formatting |
| `MediaPipeFullArmChainProviderTests.cpp` | 93 | Full arm-chain contract and Wallace log-format tests |
| `MediaPipeRuntimeCVars.h` | 244 | Runtime policy/CVar snapshot declarations, including generic MetaHuman arm-source mode and deprecated Wallace aliases |
| `MediaPipeRuntimeCVars.cpp` | 1,184 | Runtime policy/CVar collection and application, including generic MetaHuman full-chain trace CVars and deprecated Wallace aliases |
| `MediaPipeRuntimeCVarsTests.cpp` | 42 | Runtime CVar tests |
| `MediaPipeBodyDiagnostics.h` | 77 | Body diagnostic formatting declarations |
| `MediaPipeBodyDiagnostics.cpp` | 89 | Body diagnostic formatting |
| `MediaPipeBodyDiagnosticsTests.cpp` | 68 | Body diagnostic tests |
| `MediaPipeQuestWristCalibrationState.h` | 112 | Quest wrist calibration state machine declarations |
| `MediaPipeQuestWristCalibrationState.cpp` | 236 | Quest wrist calibration state machine logic |
| `MediaPipeQuestWristCalibrationStateTests.cpp` | 174 | Quest wrist calibration tests |
| `MediaPipeQuestFingerSolver.h` | 64 | Quest finger solver declarations |
| `MediaPipeQuestFingerSolver.cpp` | 334 | Quest finger curl/mapping and segment-direction solver helpers |
| `MediaPipeQuestFingerSolverTests.cpp` | 184 | Quest finger solver tests |
| `MediaPipeBodySolverMath.h` | 64 | Body/leg solver math declarations |
| `MediaPipeBodySolverMath.cpp` | 159 | Body/leg solver math |
| `MediaPipeBodySolverMathTests.cpp` | 119 | Body/leg solver math tests |

## State Objects

The AnimInstance now owns cohesive state objects instead of long runs of flat state fields:

```text
FMediaPipeBodySolverState BodyState
FMediaPipeLegSolverState LeftLegState / RightLegState
FMediaPipeArmSolverState LeftArmState / RightArmState
FMediaPipeQuestWristSolverState QuestWristState
FMediaPipeQuestHandSolverState LeftQuestHandState / RightQuestHandState
FMediaPipeDiagnosticsState DiagnosticsState
```

Reset paths now route through state-owned reset methods:

```text
BodyState.ResetRotationSmoothing()
BodyState.ResetTorsoStability()
LeftLegState.ResetFootPlant()
LeftLegState.ResetRotationSmoothing()
LeftArmState.ResetSmoothing()
QuestWristState.Reset()
LeftQuestHandState.Reset()
DiagnosticsState.Reset()
```

`FMediaPipeQuestWristSolverState::Reset()` deliberately preserves `LastQuestWristManualResetSerial`; the new tests cover this.

## What Stayed In The AnimInstance

The AnimInstance still owns:

- animation-thread orchestration
- `PreUpdate` frame capture and HMD pose caching
- reference-pose cache ownership
- target actor/component transform state
- CVar/runtime policy consumption
- component-space pose write helpers
- inclusion point for tightly coupled solver clusters

This is intentional. The refactor did not split tightly coupled animation evaluation math into tiny standalone classes just to reduce line count.

## What Changed

The current split removed the worst god-file pressure by separating:

- state ownership from solve orchestration
- body/leg/Quest/arm implementation clusters
- runtime policy/CVar collection
- Quest wrist calibration state
- Quest finger solving
- body solver math
- diagnostics formatting
- capture/replay/debug tooling
- focused automation tests for extracted helpers and state reset behavior

## Remaining Refactor Targets

The largest remaining implementation clusters are:

```text
MediaPipePoseDrivenAnimInstance_QuestArmSolve.inl: 1,672 LoC
MediaPipePoseDrivenAnimInstance_QuestHandRotation.inl: 1,623 LoC
MediaPipePoseDrivenAnimInstance_QuestSpaceMapping.inl: 994 LoC
MediaPipePoseDrivenAnimInstance_QuestFingerDrive.inl: 952 LoC
```

Do not split these just for line count. The next safe architectural move would be to convert one cluster at a time into a solver object only after its inputs, outputs, and state ownership are explicit and covered by tests.

Recommended next extraction order:

1. Quest hand rotation/palm-basis solve, because it has a clear input/output shape and high behavioral risk.
2. Quest arm endpoint/reach solve, after wrist/hand state is better isolated.
3. Quest media-space mapping, after calibration state and HMD/avatar-frame inputs are explicit.

Every extraction should keep the current validation standard:

- editor target build
- `TestingKit3.MediaPipe` automation tests
- AgentBridge smoke/status
- user-run VR Preview for headset behavior
- for Quest fingers, keep the 2026-05-19 headset-accepted `segmentDirection` default unless a future VR Preview run proves a replacement
