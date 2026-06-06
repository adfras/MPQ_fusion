# Emory Forward-Lean Neck Findings

Status: compacted 2026-06-05.

## Retained Finding

The accepted Emory fix guarded the neck/head chain during forward lean so HMD-driven head ownership did not collapse or over-stretch the MetaHuman neck. The relevant behavior is now covered by BodyFusion pose/write context and profile-based head/chest/neck offsets.

## Current Anchors

- `Source/MediaPipeDriver/BodyFusion/MediaPipeBodyFusionPoseWriteContext.*`
- `Source/MediaPipeDriver/BodyFusion/MediaPipeBodyFusion.*`
- `Source/MediaPipeDriver/Embodiment/MediaPipeAvatarEmbodimentProfile.*`
- `Source/MediaPipeDriver/Avatar/MediaPipeMetaHumanProfile.*`

## Guardrail

Do not reintroduce head-only correction that ignores chest/neck profile limits. Verify with focused BodyFusion tests plus visual proof for MetaHuman forward/backward lean.
