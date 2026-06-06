# Embodiment Architecture Comparison - Historical

Status: compacted 2026-06-05.

## Retained Finding

The important comparison result was architectural, not an offset tweak: the working reference made the placed avatar/pawn hierarchy own the HMD camera and body relationship, while the older TestingKit path relied on hidden startup actors, PlayerStart/Spectator flow, and camera pinning.

## Current TestingKit5 Direction

- Use placed/pawn-owned embodied avatar hierarchy: `AvatarRoot`, `BodyRoot`, `VROrigin`, `VRCamera`, avatar meshes, motion controllers, and `UEmbodiedFusionComponent`.
- Keep HMD camera at the profile-derived avatar eye/root relationship.
- Keep MediaPipe/Quest tracking source production separate from skeleton-specific writing.
- Do not copy OculusXRMovement wholesale; reproduce the ownership shape around the existing MediaPipe/Quest pipeline.

Current source map: `Docs/MEDIAPIPE_EMBODIMENT_CURRENT_STATE.md`.
