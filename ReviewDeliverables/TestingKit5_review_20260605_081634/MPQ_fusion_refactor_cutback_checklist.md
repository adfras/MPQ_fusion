# MPQ Fusion Refactor Cutback Checklist

Status: consolidated 2026-06-05. The old checkbox chronology was collapsed to the current outcome.

## Cutback Outcome

- Kept essential production files: `MediaPipePoseDrivenAnimInstance.*`, inline pose solve files, BodyFusion solver/types, tracking source frame builder/types, current profile systems, Quest/HMD wrappers, and core Quest arm/wrist/finger solvers.
- Kept useful helpers: `MediaPipeSkeletonPoseAdapter.*`, `MediaPipeBodyFusionPoseWriteContext.*`, `MediaPipeBodyFusionDebugFormatter.*`, `MediaPipeQuestRuntimeDebugService.*`, and MetaHuman adapter/profile files.
- Merged/deferred speculative layers: standalone embodiment pipeline/source normalizer/body-fusion source adapters, data-asset skeleton adapter expansion, dependency-guard tests, and unused profile asset scaffolding.
- Preserved invariants: HMD owns eye/head, pelvis does not follow HMD by default, MediaPipe authority is gated, debug CVars do not silently alter solver output, Quest/HMD polling stays outside pure solvers.

## Remaining Work

- Complete writer split so fused pose is consumed consistently by the anim path and movement-replica path.
- Move raw source polling out of the anim instance.
- Keep source/frame/fusion/writer boundaries source-owned and test-backed.
- Rename legacy `TestingKit3.*` automation names in a dedicated pass only.

## Current Verification Commands

See `Docs/MEDIAPIPE_VALIDATION_AND_OPERATIONS.md`.
