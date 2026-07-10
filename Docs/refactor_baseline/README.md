# Refactor baseline — behavioral fingerprint (Phase 0 of refactor/correctors)

Reference point for the behavior-preserving corrector extraction. Captured at tag
`refactor-base` (= commit `fe88b84`, the end of the 2026-07-10 arm-quality arc,
worn verdict "much better" — see `Docs/RESOLUTION_2026-07-10_ARM_CHURN_AND_STICKY_WRIST.md`).

## Source

- Log: `Saved/Logs/TestingKit5.log`, header `Log file open, 07/10/26 12:49:25`
  (local, UTC+8) — the FINAL worn session of the 2026-07-10 arc, booted on the
  round-6 build (`ac28e97`) immediately after the last 157/157 test run
  (`TestingKit5-backup-2026.07.10-04.48.08.log`). Snapshot taken at 52,768,212 bytes
  while the editor was still up and idle.
- Worn tracer window: first row `2026.07.10-05.02.11` UTC, last row
  `2026.07.10-05.04.05` UTC.
- Extraction: Python line scan (NOT ripgrep — it silently returns nothing on
  Saved/Logs), rows copied verbatim, no reformatting.

## Files

| File | Rows | Row family |
|------|------|------------|
| `armdircorrection_rows.log` | 218 | `mp.ArmDirCorrection` — bounded direction corrector drift row (engaged/rel/dirAlpha/applyAlpha/elbowCorrDeg/wristCorrDeg/spdCmS/quiet/enterS/exitS/hasMpArm) |
| `armjumptrace_rows.log` | 14 | `mp.ArmJumpTrace` — event-driven jump attribution (residCm + per-stage dirOffCm/extOffCm/guardOffCm and deltas) |
| `questwristsolve_rows.log` | 3748 | `mp.QuestWristSolve` — full wrist/arm solve acceptance row (per actor-side, keyed throttle) |
| `chainreachextend_rows.log` | 218 | `mp.ChainReachExtend` — reach extension evidence (tracked/frac/highS/desiredExtCm/appliedExtCm) |
| `clavicleshrugfusion_rows.log` | 378 | `mp.ClavicleShrugFusion` — shrug drive evidence (heightCm/restRef/liftRig/sin/rigScale/appliedCm) |
| `Phase0BaselineTests.log` | — | Full log of the Phase-0 157-test automation run at `refactor-base` |

## How to use this baseline

1. **Row FORMAT is contract**: after each extraction phase, the same tracer must
   emit rows character-for-character identical in field names, order, formatting
   precision, and category (`LogMediaPipePose`). A format diff disqualifies the phase.
2. **Values are the worn reference**: timestamps, frame indices, and `runtimeKey`
   are session-specific — cross-session comparison is field-wise (same fields
   present, same value ranges/behavior in the Phase-8 worn acceptance session),
   not byte-wise.
3. The byte-identical gate applies to REPLAY outputs (test-driven, deterministic),
   not to worn logs. Both gates are required; neither substitutes for the other —
   automated tests keep live paths dormant.

Baseline test gate at `refactor-base`: 157 × `Test Completed. Result={Success}`.
