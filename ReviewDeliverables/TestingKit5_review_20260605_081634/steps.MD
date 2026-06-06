# Avatar Embodiment Refactor Steps

Status: compacted 2026-06-05.

## Current Source Of Truth

- Current architecture: `Docs/MEDIAPIPE_EMBODIMENT_CURRENT_STATE.md`
- Avatar profile contract: `Docs/AVATAR_PROFILE_DRIVEN_EMBODIMENT.md`
- Quest/Wallace defaults: `Docs/MEDIAPIPE_QUEST_WALLACE_CURRENT_STATE.md`
- Validation commands: `Docs/MEDIAPIPE_VALIDATION_AND_OPERATIONS.md`

## Retained Decisions

- Manny, Wallace, Emory, Hudson, Kellan, Maria, Payton, and future avatars should be selected by profile data, not avatar-specific solver branches.
- The avatar should remain visible locally; only camera-intersecting head/face/eyes/hair or equivalent pieces should be hidden/clipped for owner view.
- HMD height is diagnostic unless explicitly consumed by stable avatar anchor math.
- Profile switching remains `mp.MetaHumanActiveProfile <ProfileId>`.
- The old TestingKit3-only arm-source switches are historical compatibility, not the normal TestingKit5 workflow.

## Current Open Items

See `body_fusion_steps.MD` and `MPQ_fusion_do_now_checklist.md`.
