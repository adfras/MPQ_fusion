# Quest Wrist Solve Freeze - Historical

Status: compacted 2026-06-05.

## Retained Fact

This was an older accepted wrist baseline from the TestingKit3 Quest workflow. It is not current TestingKit5 tuning guidance by itself.

## Current Source Of Truth

- Quest/Wallace current state: `Docs/MEDIAPIPE_QUEST_WALLACE_CURRENT_STATE.md`
- Runtime CVars: `Source/MediaPipeDriver/Runtime/MediaPipeRuntimeCVars.cpp`
- Quest wrist calibration/apply policy: `Source/MediaPipeDriver/Quest/MediaPipeQuestWristCalibrationState.*`, `MediaPipeQuestWristApplyPolicy.*`, `MediaPipeQuestWristDiagnosticFormatter.*`

## Guardrail

Only use this historical baseline when the task explicitly asks to recover or compare against the old freeze point.
