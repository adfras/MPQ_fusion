# Avatar Profile-Driven Embodiment

Status: consolidated 2026-06-05. Current architecture is in `Docs/MEDIAPIPE_EMBODIMENT_CURRENT_STATE.md`.

## Current Contract

- Avatar profiles define skeleton family, eye/head/chest/neck/pelvis offsets, expected limb lengths, forward-axis policy, local-view hidden bones, and upper-body follow policy.
- Internal Manny resolves from `MediaPipeAvatarRigProfile.*` and `BuildMediaPipeAvatarEmbodimentProfileFromRigProfile`.
- MetaHumans resolve through `MediaPipeMetaHumanProfile.*` and `BuildMediaPipeAvatarEmbodimentProfileFromMetaHumanProfile`.
- Profile offsets place the body relative to the HMD/camera; do not replace profile placement with ad hoc camera/world offsets.

## Source Anchors

- `Source/MediaPipeDriver/Embodiment/MediaPipeAvatarEmbodimentProfile.*`
- `Source/MediaPipeDriver/Avatar/MediaPipeAvatarRigProfile.*`
- `Source/MediaPipeDriver/Avatar/MediaPipeMetaHumanProfile.*`
- `Source/MediaPipeDriver/Avatar/MediaPipeAvatarProfileResolver.*`
- `Source/MediaPipeDriver/Embodiment/MediaPipeEmbodiedAvatarPawn.*`

## Guardrails

- Keep self-view and mirror/external visibility separate.
- Keep full-avatar mirror/external visibility; owner-only local hidden bones/proxies are the self-view mechanism.
- Validate profile changes with build, focused automation, and visual proof when camera/body placement is affected.
