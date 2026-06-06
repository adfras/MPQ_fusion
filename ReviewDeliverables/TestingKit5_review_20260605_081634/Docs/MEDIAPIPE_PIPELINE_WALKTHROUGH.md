# MediaPipe Pipeline Walkthrough

Status: consolidated 2026-06-05. This compact walkthrough points to current source boundaries instead of repeating historical implementation detail.

## Data Flow

```text
MediaPipe video/webcam frame
  -> MediaPipe native wrapper / worker
  -> UMediaPipePoseTrackerComponent
  -> UEmbodiedFusionComponent::UpdateBodyPoseObservation_GameThread
  -> FMediaPipeTrackingSourceFrameBuilder
  -> FMediaPipeBodyFusionSolver
  -> FMediaPipeFusedAvatarPose
  -> pose writer / anim instance / movement-replica writer
```

Quest/OpenXR:

```text
HMD / hands / full arm chain
  -> Quest source wrappers and full-arm provider
  -> UEmbodiedFusionComponent source observations
  -> FMediaPipeTrackingSourceFrame
  -> BodyFusion / best-available pose
```

## Source Map

| Area | Files |
| --- | --- |
| Core types/logging | `Source/MediaPipeDriver/Core/*` |
| MediaPipe tracking | `Source/MediaPipeDriver/Tracking/*` |
| Quest/OpenXR | `Source/MediaPipeDriver/Quest/*`, `PoseDriven/MediaPipeFullArmChainProvider.*` |
| Embodiment | `Source/MediaPipeDriver/Embodiment/*` |
| BodyFusion | `Source/MediaPipeDriver/BodyFusion/*` |
| Avatar/profile/writer helpers | `Source/MediaPipeDriver/Avatar/*` |
| Pose-driven anim path | `Source/MediaPipeDriver/PoseDriven/*` |
| Editor commands/video workflow | `Source/MediaPipeDriverEditor/*` |
| Tests | `Source/MediaPipeDriver/Tests/*` |

## Current Docs

- Architecture and debt: `Docs/MEDIAPIPE_EMBODIMENT_CURRENT_STATE.md`
- Quest/Wallace defaults: `Docs/MEDIAPIPE_QUEST_WALLACE_CURRENT_STATE.md`
- Build/test/proof commands: `Docs/MEDIAPIPE_VALIDATION_AND_OPERATIONS.md`
