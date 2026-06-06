# MediaPipe Shoulder Baseline

Status: historical, compacted 2026-06-05.

## Retained Baseline

- This file used to preserve an older TestingKit3 Manny shoulder baseline where arms were imperfect but better than earlier left/right mismatch, upside-down shoulder roll, and frozen-arm states.
- Current TestingKit5 behavior and source boundaries are documented in `Docs/MEDIAPIPE_EMBODIMENT_CURRENT_STATE.md`.
- VP2-specific shoulder/head follow work is documented in `VP2_Manny_Tracking_Fix_Solutions.md`.

## Guardrail

Do not reapply old shoulder CVar bundles as current defaults without a fresh TestingKit5 build, analyzer run, and visual proof.
