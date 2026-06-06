# MediaPipe Quest And Wallace Current State

Status: consolidated 2026-06-05. This replaces the repeated Wallace defaults handoff, arm rollback, wrist freeze, arm audit, VR mirror baseline, and MetaHuman profile handoffs.

## Profiles

- Default MetaHuman profile id is `Wallace` in `MediaPipeMetaHumanProfile.cpp`.
- Built-in MetaHuman profiles are `Wallace`, `Emory`, `Hudson`, `Kellan`, `Maria`, and `Payton`.
- Built-in profile defaults: face-forward axis `Y`, embodied yaw offset `-90`, default eye local offset `(0, 8.92, 161.94)`, default arm source mode `FullArmChain`.
- Active profile comes from `mp.MetaHumanActiveProfile`; empty or `Default` resolves to `Wallace`.
- Internal Manny uses `/Game/MediaPipe/MediaPipeRig/SK_MediaPipeMannyLike.SK_MediaPipeMannyLike` with profile id `InternalMannyLike`.

## Current Quest ownership

| Area | Current rule |
| --- | --- |
| HMD/head | HMD owns the embodied eye/head anchor. HMD pose must be valid and worn before claiming VR headset proof. |
| Wrist rotation | Quest/OpenXR wrist basis can drive hand/wrist orientation; relative calibration is on by default. |
| Wrist position | Default `mp.QuestWristPositionBlend=0` keeps MediaPipe-owned wrist/elbow/shoulder position unless an AutoQuest profile or explicit CVar changes authority. |
| AutoQuest arm profile | `mp.AutoQuestArmReachAssistProfile=4` maps Quest wrist endpoint authority with MediaPipe shoulder/elbow hints. |
| Fingers | `mp.QuestHandDriveFingerBones=1` drives target finger bones from Quest/OpenXR hand keypoints when available. |
| MediaPipe body | Default BodyFusion is off and MediaPipe authority is trace-only until explicitly enabled/gated. |
| Stable body | `mp.AutoQuestEmbodiedStableBody=1` keeps trunk/clavicles stable during embodied AutoQuest so low-Hz MediaPipe body tracking does not fight HMD/hand tracking. |

## Guardrails

- Do not retune Wallace arm/wrist/finger defaults from historical notes unless the task explicitly asks for that target.
- Do not treat the old `TestingKit3` paths in historical logs as current project paths. Current project commands must use `TestingKit5` and UE 5.8.
- Do not claim a Quest/Wallace fix from desktop PIE alone. Required proof is a worn Quest state, tracked hands if hand behavior is in scope, log rows for the relevant `mp.*` diagnostics, and a focused screenshot or direct user headset confirmation.
- Do not re-enable old mirror-station camera pinning as an offset fix. The current direction is placed/pawn-owned HMD camera plus profile-derived body placement.
- Do not hide the full avatar globally. Owner self-view and mirror/external view are separate visibility paths.

## Useful console commands

```text
mp.MetaHumanActiveProfile Wallace
mp.AutoQuestAvatar 1
mp.AutoQuestWebcamHands 1
mp.AutoQuestEmbodiedView 1
mp.BodyFusion.Enable 1
mp.BodyFusion.Debug 1
mp.BodyFusion.ResetCalibration
mp.ResetQuestWristCalibration
mp.DumpQuestHands
```

For placed Manny/video testing:

```text
mp.PlacedEmbodiedVideoFile D:/Epic/Unreal_Projects/TestingKit5/Saved/Videos/VP2.mp4
mp.StartPlacedEmbodiedTracking
```

If a command path or exact CVar changes, source of truth is `MediaPipeRuntimeCVars.cpp`, `MediaPipeDriverRuntime.cpp`, `MediaPipeBodyFusionRuntime.cpp`, and `MediaPipeQuestHandDebugReporter.cpp`.

## Known naming debt

Several runtime tags and automation tests still include `TestingKit3` in names. That is source naming debt, not current project identity. Rename only in a dedicated source/test pass because log parsers and automation filters may depend on the strings.
