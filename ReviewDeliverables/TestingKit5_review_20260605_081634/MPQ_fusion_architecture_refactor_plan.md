# MPQ Fusion Architecture Refactor Plan

Status: consolidated 2026-06-05 for TestingKit5/UE 5.8. The original long-form plan is superseded by `Docs/MEDIAPIPE_EMBODIMENT_CURRENT_STATE.md`; this file keeps the actionable architecture target.

## Target Dependency Direction

```text
MediaPipe / Quest / OpenXR sources
  -> source observations
  -> FMediaPipeTrackingSourceFrame
  -> UEmbodiedFusionComponent
  -> FMediaPipeBodyFusionSolver
  -> FMediaPipeFusedAvatarPose
  -> skeleton/profile writer
  -> bones
```

Sources must not know Manny or MetaHuman bones. Fusion must not write bones. Pose writers must not poll raw Quest/OpenXR/MediaPipe state.

## Current Reality

- Done: `FMediaPipeTrackingSourceFrame` moved into `Tracking`, BodyFusion has `FMediaPipeFusedAvatarPose`, freshness/authority/calibration live outside the old monolith, and BodyFusion tests were consolidated.
- Partial: `UEmbodiedFusionComponent` now owns source polling orchestration and fused frame state, but it still calls Quest runtime/debug services directly.
- Not done: `MediaPipePoseDrivenAnimInstance.*` and its inline files still contain raw runtime state, Quest wrist/finger/arm paths, diagnostics, and direct pose writing.
- Not done: `AMediaPipeEmbodiedAvatarPawn` still owns movement-replica pose writing and presentation-specific mesh updates.

## Small Next Slices

1. Add tests around the exact current source boundaries before moving code.
2. Extract a writer-facing fused-pose consumption API used by both anim instance and movement-replica paths.
3. Move one raw polling responsibility at a time out of `MediaPipePoseDrivenAnimInstance.*`.
4. Reduce `UEmbodiedFusionComponent` dependency on Quest debug services by introducing source adapters with pure observation output.
5. Rename stale `TestingKit3.*` tests/tags only in a dedicated pass with log/filter migration.

## Non-goals For The Next Slice

- Do not add unused data-asset skeleton adapter layers.
- Do not retune Manny/Wallace behavior while moving ownership boundaries.
- Do not delete legacy strings without checking tests, logs, and content references.
