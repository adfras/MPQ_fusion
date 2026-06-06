# MediaPipe Refactor State - Historical

Status: compacted 2026-06-05.

## Retained Fact

This file recorded an earlier TestingKit3 refactor stage where solver state and inline anim-instance responsibilities were being split. The current TestingKit5 source has moved more structure into `Tracking`, `Embodiment`, `BodyFusion`, `Avatar`, and `Quest` folders, but the pose-driven anim instance remains a major coupling point.

## Current Source Of Truth

- `Docs/MEDIAPIPE_EMBODIMENT_CURRENT_STATE.md`
- `MPQ_fusion_architecture_refactor_plan.md`
- `MPQ_fusion_do_now_checklist.md`
