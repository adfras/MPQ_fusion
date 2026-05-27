# Emory Forward-Lean Neck Findings - 2026-05-26

This document records the accepted 2026-05-26 fix for the Emory MetaHuman neck shape when the Quest wearer leans forward in VR Preview.

## Status

Accepted improvement.

The user-reported bad reference frames were:

```text
Saved/QuestScreenshots/vrpreview_quest_mirror_20260526_194950/vrpreview_quest_mirror_20260526_194950_005_195003.png
Saved/QuestScreenshots/vrpreview_quest_mirror_20260526_194950/vrpreview_quest_mirror_20260526_194950_006_195006.png
```

After the BodyFusion change, the post-fix Oculus Mirror capture set was:

```text
Saved/QuestScreenshots/vrpreview_quest_mirror_20260526_202344
```

Representative accepted frame:

```text
Saved/QuestScreenshots/vrpreview_quest_mirror_20260526_202344/vrpreview_quest_mirror_20260526_202344_014_202435.png
```

The user confirmed the result as "much better" after the patch. Treat this as the current accepted Emory forward-lean neck checkpoint unless later headset evidence supersedes it.

## Finding

The visible problem was not primarily a camera/eye-lock failure.

Runtime rows from the failing and post-fix sessions showed the eye path itself was locked:

```text
cameraToSolverCamera=0.0
cameraToPosedCamera=0.0
solverEyeToPosedEye=0.0
solverHeadToPosedHead=0.0
```

The issue was in the HMD-only MetaHuman upper-body fallback. The fallback made the MetaHuman chest follow too much of the HMD/head translation during forward/down lean. That stacked the head and chest too closely and pushed the visible error into the neck shape.

This was especially visible for Emory because the MetaHuman head/neck/chest proportions make the fallback torso motion read as a neck collapse or pivot in the mirror. The arm solver was not the cause of this specific symptom.

## Implementation

Changed file:

```text
Source/MediaPipeDriver/MediaPipeBodyFusion.cpp
```

Added a profile-family follow factor:

```text
ResolveEmbodiedUpperBodyFollowAlpha(...)
```

Current behavior:

```text
MetaHuman upper-body follow alpha: 0.70
Non-MetaHuman upper-body follow alpha: 1.00
```

In the embodied HMD-only fallback, `UpperBodyPlanarDelta` and `HeadVerticalDelta` are now scaled before being applied to the chest for MetaHumans. This keeps the chest following the lean, but leaves enough residual motion in the head-to-chest span that the neck no longer appears stacked/collapsed.

Important code locations:

```text
Source/MediaPipeDriver/MediaPipeBodyFusion.cpp:191
Source/MediaPipeDriver/MediaPipeBodyFusion.cpp:839
```

Do not replace this with a camera offset. The camera and eye were already correct; the accepted change is in the torso/head relationship.

## Regression Coverage

Changed file:

```text
Source/MediaPipeDriver/MediaPipeBodyFusionTests.cpp
```

The regression now asserts that HMD-only MetaHuman lean leaves residual head-to-chest motion instead of stacking the head directly over the followed chest.

Important test locations:

```text
Source/MediaPipeDriver/MediaPipeBodyFusionTests.cpp:610
Source/MediaPipeDriver/MediaPipeBodyFusionTests.cpp:639
```

The forward/down regression uses an HMD frame at:

```text
FVector(60.0f, 0.0f, 130.0f)
```

Expected behavior:

```text
ForwardLoweredHeadToChest.X > 12.0f
ForwardLoweredChestToPelvis.Z > ForwardLoweredChestToPelvis.X * 1.5f
```

## Verification

Build command:

```text
D:\Epic\UE_5.7\Engine\Build\BatchFiles\Build.bat TestingKit3Editor Win64 Development -Project=D:\Epic\Unreal_Projects\TestingKit3\TestingKit3.uproject -WaitMutex -NoHotReloadFromIDE -NoUBA
```

Result:

```text
Succeeded
Linked Binaries/Win64/UnrealEditor-MediaPipeDriver.dll
```

Focused automation command:

```text
D:\Epic\UE_5.7\Engine\Binaries\Win64\UnrealEditor-Cmd.exe D:\Epic\Unreal_Projects\TestingKit3\TestingKit3.uproject -NullRHI -Unattended -NoSplash -NoSound -stdout -FullStdOutLogOutput -abslog=D:\Epic\Unreal_Projects\TestingKit3\Saved\Logs\MetaHumanForwardLeanBodyFusionAutomation.log -ExecCmds="Automation RunTests TestingKit3.MediaPipe.BodyFusion; Quit" -TestExit="Automation Test Queue Empty"
```

Automation result:

```text
Found 20 automation tests based on 'TestingKit3.MediaPipe.BodyFusion'
TestingKit3.MediaPipe.BodyFusion.SourceOwnerTags: Success
**** TEST COMPLETE. EXIT CODE: 0 ****
```

Post-fix VR Preview/Oculus Mirror evidence:

```text
Saved/QuestScreenshots/vrpreview_quest_mirror_20260526_202344
```

The post-fix capture contains nonblank Oculus Mirror frames from `20:23:46` through `20:24:46` local time. The user accepted the visual result after this run.

## Guardrails

Future work should preserve these boundaries:

- Do not treat the older `vrpreview_quest_mirror_20260526_194950` frames as current behavior; they are the failing reference.
- Do not call build or automation alone proof of a headset-visible embodiment fix. Use Oculus Mirror or equivalent headset-faithful evidence.
- Do not fix this symptom by moving the camera independently of the avatar eye. The eye lock was already correct.
- Do not rewrite the Quest arm path for this neck symptom.
- Keep the MetaHuman-specific follow factor profile-family based. Manny/non-MetaHuman fallback behavior remains unchanged at `1.00`.
- If the issue regresses, compare the failing 19:49/19:50 capture set against the accepted 20:23/20:24 capture set before changing solver constants.

