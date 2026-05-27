# MediaPipe Webcam Inclusion

Last updated: 2026-05-17

## Status

Webcam capture support has been added to the TestingKit3 MediaPipe editor workflow.

The inclusion is compiled into `TestingKit3Editor` and reuses the existing MediaPipe video pipeline. The VR Preview auto webcam mirror baseline is now tracked separately in:

```text
Docs/WALLACE_QUEST_VR_CURRENT_DEFAULTS_HANDOFF.md
Docs/MEDIAPIPE_VR_MIRROR_BASELINE.md
Docs/MEDIAPIPE_REFACTOR_STATE_2026-05-17.md
```

Standalone editor-console webcam command verification is still distinct from the VR Preview auto flow. For that path, run `mp.ListMediaPipeWebcams` inside the Unreal console and then start the selected camera with `mp.PlayMediaPipeWebcam`.

## Goal

The goal is to let a real webcam track the user in their room and transfer the movement onto the live Manny actor, using the same retargeting path already used by the local test videos.

The intended runtime flow is:

```text
webcam capture device
  -> Unreal Media Framework capture URL
  -> UMediaPlayer
  -> UMediaTexture
  -> UMediaPipePoseTrackerComponent
  -> native MediaPipe pose landmarker
  -> FMediaPipePoseFrame
  -> FMediaPipeSourceConditioner
  -> MediaPipeSolvedPose::BuildLocal
  -> UMediaPipePoseDrivenAnimInstance
  -> Manny-like skeletal mesh
```

## Design Decision

The webcam support was added as a media-source extension, not as a separate tracking pipeline.

That is important because the proven parts of the current system stay shared:

- MediaPipe native wrapper loading
- frame readback and RGB conversion
- source conditioning
- coordinate conversion
- solved torso basis
- Manny retargeting
- shoulder diagnostics
- frozen shoulder baseline profile

The only source-specific difference is how the `UMediaPlayer` is opened:

```text
video file path -> UMediaPlayer::OpenFile(...)
webcam URL      -> UMediaPlayer::OpenUrl(vidcap://...)
```

## Commands

List video capture devices visible to Unreal:

```text
mp.ListMediaPipeWebcams
```

Start the first camera:

```text
mp.PlayMediaPipeWebcam device=0 conditioning=1 hz=30 mirror=1
```

Start a camera by display-name substring:

```text
mp.PlayMediaPipeWebcam device=Logitech conditioning=1 hz=30 mirror=1
```

Start from an explicit Unreal Media Framework capture URL:

```text
mp.PlayMediaPipeWebcam url=vidcap://... conditioning=1 hz=30 mirror=1
```

Stop webcam tracking:

```text
mp.StopMediaPipeWebcam
```

If the user movement appears left/right reversed, rerun with:

```text
mp.PlayMediaPipeWebcam device=0 conditioning=1 hz=30 mirror=0
```

## Command Arguments

`mp.PlayMediaPipeWebcam` accepts:

```text
device=0
device=<camera name substring>
camera=<camera name substring>
index=0
name=<camera name substring>
url=vidcap://...
hz=30
model=full|lite|default|path
hands=0|1
conditioning=0|1
async=0|1
mirror=0|1
```

The command defaults to the first enumerated camera if no device selector is supplied.

## Files Included

Actor source support:

```text
Source/MediaPipeDriverEditor/MediaPipePoseVideoActor.h
Source/MediaPipeDriverEditor/MediaPipePoseVideoActor.cpp
```

The actor now supports:

- file-backed video mode
- capture-device mode
- `ConfigureVideoFile(...)`
- `ConfigureCaptureDevice(...)`
- `OpenConfiguredMedia()`
- `OpenConfiguredCaptureDevice()`

Editor command support:

```text
Source/MediaPipeDriverEditor/MediaPipeLiveVideoCommands.cpp
```

This file now owns:

- camera enumeration through `MediaCaptureSupport::EnumerateVideoCaptureDevices`
- capture-device selection by index, name substring, or URL
- webcam start/stop commands
- shared live-cycle ticking for video and webcam sources
- webcam diagnostic labels in the existing movement and shoulder logs

Module dependencies:

```text
Source/MediaPipeDriverEditor/MediaPipeDriverEditor.Build.cs
```

Added dependencies:

```text
Media
MediaUtils
```

These are needed so the editor module can enumerate Unreal Media Framework capture devices.

Documentation:

```text
Docs/MEDIAPIPE_PIPELINE_WALKTHROUGH.md
Docs/MEDIAPIPE_WEBCAM_INCLUSION.md
Docs/MEDIAPIPE_VR_MIRROR_BASELINE.md
Docs/MEDIAPIPE_REFACTOR_STATE_2026-05-17.md
```

The shared retarget source is now split across `MediaPipePoseDrivenAnimInstance.cpp`, `MediaPipePoseDrivenSolverState.h`, and the `MediaPipePoseDrivenAnimInstance_*.inl` clusters. Do not treat the old monolithic AnimInstance file as the only webcam retarget implementation.

## Frozen Retarget Profile

`mp.PlayMediaPipeWebcam` applies the same frozen profile as:

```text
mp.PlayMediaPipeVisualCycle
```

This keeps webcam testing comparable with the current best video baseline. The profile includes the current shoulder choices:

- clavicle driving disabled
- arm IK disabled
- hand rotation disabled
- arm twist helper driving disabled
- elbow-plane roll disabled
- surface-basis arm roll path retained
- source arm hold disabled
- source shoulder reconstruction disabled

This is intentional. Webcam work should first prove that live capture reaches the same retarget path before changing shoulder tuning.

## Verification

Latest compile and automation verification completed on 2026-05-17:

```text
& "D:\Epic\UE_5.7\Engine\Build\BatchFiles\Build.bat" TestingKit3Editor Win64 Development -Project="D:\Epic\Unreal_Projects\TestingKit3\TestingKit3.uproject" -WaitMutex -NoHotReload
```

Results:

```text
TestingKit3Editor build: succeeded
Automation RunTests TestingKit3.MediaPipe: 29 tests passed, exit code 0
AgentBridge npm run smoke: passed
```

Editor/device verification still needed:

```text
mp.ListMediaPipeWebcams
mp.PlayMediaPipeWebcam device=0 conditioning=1 hz=30 mirror=1
```

Expected useful log lines:

```text
mp.ListMediaPipeWebcams: [0] name="..." type=... url=...
mp.PlayMediaPipeWebcam: device=... url=...
mp.PlayMediaPipeVisualCycle movement: clip=...
mp.MediaPipeSourceArmPlaneCompare clip=...
mp.MediaPipeShoulderDiag clip=...
```

## Room Setup Notes

For first validation, use a setup that makes source-side MediaPipe less ambiguous:

- full body visible if possible
- camera at roughly chest height or slightly above
- bright, even room lighting
- avoid strong backlighting
- stand far enough back that wrists and ankles remain visible
- avoid baggy sleeves during shoulder testing
- start with simple arm raises and side steps before fast motion

## Known Risks

The webcam path depends on Unreal's Media Framework seeing a video capture device. On Windows this usually comes from the WMF media plugin and exposes `vidcap://...` URLs.

Possible first-run issues:

- camera permission blocked by Windows privacy settings
- another app already owning the camera
- camera listed but no frames advancing
- mirrored movement requiring `mirror=0`
- room framing causing MediaPipe to lose wrists, ankles, or shoulders
- shoulder roll still limited by the existing monocular retarget problem

If enumeration succeeds but the Manny does not move, inspect whether raw source timestamps and source wrists are changing in the movement log before changing retarget settings.
