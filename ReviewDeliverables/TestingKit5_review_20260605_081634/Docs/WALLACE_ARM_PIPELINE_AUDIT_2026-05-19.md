# Wallace Arm Pipeline Audit - Historical

Status: compacted 2026-06-05.

## Retained Findings

- The useful output of the audit was source ownership clarity: Quest/OpenXR should own wrist/hand observations, MediaPipe can provide body/shoulder/elbow hints, and skeleton-specific helper/twist writing belongs outside fusion.
- The constrained arm solver, wrist apply policy, finger solver, diagnostics, and MetaHuman helper validation remain source-backed code areas.
- The old line-by-line chronology of experiments is superseded by current source and tests.

## Current Anchors

- `Source/MediaPipeDriver/Quest/MediaPipeQuestConstrainedArmSolver.*`
- `Source/MediaPipeDriver/Quest/MediaPipeQuestWristApplyPolicy.*`
- `Source/MediaPipeDriver/Quest/MediaPipeQuestFingerSolver.*`
- `Source/MediaPipeDriver/Quest/MediaPipeQuestWristDiagnosticFormatter.*`
- `Source/MediaPipeDriver/Avatar/MediaPipeMetaHumanArmRetargeter.*`
- `Source/MediaPipeDriver/Tests/MediaPipeQuestSolverTests.cpp`

Use `Docs/MEDIAPIPE_QUEST_WALLACE_CURRENT_STATE.md` for current defaults.
