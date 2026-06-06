# MediaPipe Tracking Issue And Next Plan - Historical

Status: compacted 2026-06-05.

## Retained Finding

The old issue trail covered neck/body mismatch, HMD torso gating, residual chain attempts, MetaHuman forward/backward lean, and side-swivel evidence gaps. The durable decisions were:

- HMD owns eye/head.
- BodyFusion authority must be explicit and source-freshness gated.
- Pelvis must not follow HMD planar movement by default.
- Head/neck/chest corrections must respect profile offsets and collapse bounds.
- Visual/headset proof is required for embodied claims.

## Current Docs

- Current architecture: `Docs/MEDIAPIPE_EMBODIMENT_CURRENT_STATE.md`
- Quest/Wallace state: `Docs/MEDIAPIPE_QUEST_WALLACE_CURRENT_STATE.md`
- Operations/proof: `Docs/MEDIAPIPE_VALIDATION_AND_OPERATIONS.md`
