# Wallace Quest VR Current Defaults Handoff

Status: consolidated 2026-06-05.

## Current Defaults

The active compact handoff is `Docs/MEDIAPIPE_QUEST_WALLACE_CURRENT_STATE.md`.

Key current facts:

- Default MetaHuman profile is `Wallace`.
- `mp.AutoQuestAvatar=0` starts from internal Manny; set `1` for active MetaHuman profile with Manny fallback.
- `mp.AutoQuestArmReachAssistProfile=4` is the current AutoQuest arm profile.
- `mp.AutoQuestEmbodiedView=1`, `mp.AutoQuestEmbodiedAnchorMode=1`, `mp.AutoQuestEmbodiedMirror=0`, `mp.AutoQuestEmbodiedStableBody=1`.
- `mp.BodyFusion.Enable=0` and `mp.BodyFusion.MediaPipeAuthority=0` by default.

## Historical Material Removed From Active Guidance

The previous file contained dated experiments, logs, performance notes, wrist/finger/arm tuning branches, and TestingKit3 paths. Those details remain summarized in `Docs/HISTORICAL_NOTES_INDEX.md` and should not be treated as current defaults without source verification.
