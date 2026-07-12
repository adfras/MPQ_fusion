# Avatar metric lock baseline (AVATAR_METRIC_LOCK_PLAN, 2026-07-12)

Evidence per phase; append-only during the plan. Rows produced by
`mp.EmbodimentScaleTrace` (per-actor ~1Hz, main + `.Bind` families) on the
canonical dataset replay map (`L_MetaHumanRecordedQuestMediaPipeReplay_01`,
bridge-driven PIE, no headset).

## Phase 0 — the premise audit

- `phase0_replay_rows_kellan.log` / `phase0_replay_rows_emory.log`: raw tracer
  rows from the two replay PIE runs (Kellan + Manny; Emory + Manny). Headline
  medians — driven vs REFERENCE-POSE spans: hip 0.957-0.968, torso 1.005-1.007,
  head 0.974-0.980, legs/arms exactly 1.000, on EVERY actor including Emory.
  bindK (inverse-bind vs reference skeleton) = 1.000 on every actor.
- `phase0_cast_asset_probe.json`: full-cast asset probe. The plan's "bind-pose
  height 97.3 vs 136.6 = child" is imported-bounds packaging (Hudson, the TALL
  body, shares Emory's 0.71 ratio); head-bone stature says Emory = 94.5% of
  Kellan — an authored short adult.
- `phase0_idle_emory.png` / `phase0_idle_kellan.png`: scene captures of the
  IDLE full MetaHuman blueprints (no MediaPipe anywhere) — Emory stands and
  renders at his reference-pose stature.

Verdict (plan doc, census section W0): no stretch exists; the "driven at ~130%
of native" measurement compared bone-Z (reference skeleton) against bounds
metadata. Native = reference pose for the whole cast.

## Native reference spans (replay rows, cm)

| Avatar | natHip | natTorso | natHead | natLeg | natArm |
| ------ | ------ | -------- | ------- | ------ | ------ |
| Manny  | 95.1   | 67.1     | 161.8   | 85.6   | 55.0   |
| Kellan | 90.6   | 64.8     | 155.0   | 81.4   | 52.2   |
| Emory  | 87.8   | 59.4     | 146.4   | 77.9   | 49.8   |
