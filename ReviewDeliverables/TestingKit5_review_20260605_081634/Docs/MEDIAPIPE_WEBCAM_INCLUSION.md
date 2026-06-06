# MediaPipe Webcam Inclusion

Status: consolidated 2026-06-05.

## Current Paths

- Automatic Quest webcam source: `AMediaPipeQuestWebcamSourceActor`
- Tracking component: `UMediaPipePoseTrackerComponent`
- Editor live video commands: `Source/MediaPipeDriverEditor/MediaPipeLiveVideoCommands.cpp`
- Placed embodied launch path: `AMediaPipeEmbodiedAvatarPawn::StartEmbodiedTracking`

## Useful Commands

```text
mp.PlayMediaPipeWebcam device=0 hz=30 model=full hands=1 conditioning=1 async=1 mirror=1
mp.PlayMediaPipeVisualCycle clip=riverbank hz=30 model=full hands=1 conditioning=1 async=1 mirror=1
mp.StopMediaPipeVisualCycle
mp.PlacedEmbodiedVideoFile D:/Epic/Unreal_Projects/TestingKit5/Saved/Videos/VP2.mp4
mp.StartPlacedEmbodiedTracking
```

## Defaults

- `mp.AutoQuestWebcamHands=1`
- `mp.AutoQuestWebcamHandsHz=30`
- `mp.AutoQuestWebcamHandsInputMaxDimension=512`
- `mp.AutoQuestWebcamPreview` and stats/debug CVars are opt-in.

## Guardrails

- Keep media/video source behavior separate from fused pose ownership.
- Record capture source, model, dimensions, FPS, and mirror setting with any visual claim.
- Do not treat old TestingKit3 video paths as current TestingKit5 paths.
