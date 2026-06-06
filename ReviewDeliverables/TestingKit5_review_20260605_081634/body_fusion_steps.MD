# Hybrid Body Fusion Steps

Status: compacted 2026-06-05.

## Current Source Of Truth

- Architecture and current ownership: `Docs/MEDIAPIPE_EMBODIMENT_CURRENT_STATE.md`
- Do-now checklist: `MPQ_fusion_do_now_checklist.md`
- Validation commands: `Docs/MEDIAPIPE_VALIDATION_AND_OPERATIONS.md`
- Historical index: `Docs/HISTORICAL_NOTES_INDEX.md`

## Retained Decisions

- BodyFusion is an ownership and semantic-pose layer, not another CVar tuning bundle.
- HMD/Quest owns head/eye/camera anchor and fresh upper-limb observations.
- MediaPipe may own or hint lower-body/body observations only through freshness, reliability, calibration, and authority gates.
- Avatar profiles carry proportions, offsets, forward axis, local-view policy, and profile-specific MetaHuman/Manny differences.
- Fusion produces `FMediaPipeFusedAvatarPose`; writers apply it to skeletons.
- Normal PIE, build, automation, logs, and screenshots are useful, but VR Preview/headset behavior requires user/headset-visible proof.

## Current Open Items

- Finish the anim-node and movement-replica writer split.
- Move remaining raw source polling out of `MediaPipePoseDrivenAnimInstance.*`.
- Rename legacy `TestingKit3.*` test/tag names only in a dedicated source/test pass.
