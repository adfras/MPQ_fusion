# Tracking-quality baseline (TRACKING_QUALITY_PLAN Phase 0, 2026-07-11)

Before-numbers for every artifact the 2026-07 tracking-quality plan attacks. Produced by
`python Tools/mine_tracking_quality_baseline.py <session.log> Docs/tracking_quality_baseline --label <label>`;
each label yields `<label>_rows.jsonl` (raw parsed rows) and `<label>_summary.md` (stats).

## Contents

- `acceptance_2026-07-10_*`: the 2026-07-10 worn acceptance session (13:11-14:05 UTC log,
  worn window 13:31-13:33), mined from `Saved/Logs/TestingKit5.log` before any Phase 0+
  code ran. Carries the PRE-PLAN row families only (the three new tracers show 0 rows by
  construction). Cross-checked against REFACTOR_PLAN.md section 9.2: the 15 ArmJumpTrace
  events (14 baseline-class + the single real 12.6cm overhead-dropout handover), the
  direction corrections saturating at exactly 20.00 deg, and the healthy per-actor-side
  row cadence all reproduce.
- `desk_capture_*` (added when the Phase 0 desk capture lands): fresh webcam-only casual
  capture with `mp.FootSkateTrace`, `mp.WristLimitTrace`, `mp.WebcamAgeTrace` armed - the
  live-webcam baseline the Phase 1/3/4 desk gates compare against.
- `replay_smoke_*` (if present): canonical-dataset replay run with the tracers armed -
  the rows-appear-and-are-sane gate evidence for Phase 0 (recorded human motion, no live
  webcam, so ages/prediction fields reflect replay pacing, not camera transport).

## What each new family measures (and which phase moves it)

- `mp.FootSkateTrace`: planted-foot planar speed (Phase 4 must cut it >= 80%), rendered
  ankle-vs-planted-height penetration proxy (Phase 4 must not regress it), lift, contact
  labels.
- `mp.WristLimitTrace`: how often/far the FINAL written wrist leaves the report-only
  anatomical envelope (Phase 2's clamp should catch exactly those frames and nothing
  else).
- `mp.WebcamAgeTrace`: webcam measurement age vs solve time, conditioner forward
  prediction, and the camera-vs-chain direction residuals against the CURRENT pose -
  Phase 1's timestamp-aligned residuals must shrink the MOVING-window residuals while
  leaving QUIET-window residuals unchanged.

Keep this directory append-only during the plan: later phases add labeled captures; the
acceptance_2026-07-10 fingerprint stays untouched as the pre-plan anchor.
