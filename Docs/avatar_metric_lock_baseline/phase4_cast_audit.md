# Phase 4 — full-cast PIE audit (AVATAR_METRIC_LOCK_PLAN, 2026-07-12)

Bridge-driven canonical-replay PIE, no headset. Per avatar: ~40s with
`mp.AvatarMetricLock 1` (candidate default), bone probe, live bisect to `0`,
second probe (8s apart — deltas are replay-motion drift, see phase1 control).
Native spans and ratios are per-actor medians of the `mp.EmbodimentScaleTrace`
rows over the lock=1 window. Manny (the always-driven reference skeleton) rides
along in every run; his row comes from the Emory run.

## Native reference spans (cm, target skeleton ref pose)

| Avatar  | natHip | natTorso | natHead | natLeg | natArm | latched S |
| ------- | ------ | -------- | ------- | ------ | ------ | --------- |
| Wallace | 92.8   | 60.1     | 152.5   | 84.0   | 53.6   | 0.942     |
| Emory   | 87.8   | 59.4     | 146.4   | 77.9   | 49.8   | 0.915     |
| Hudson  | 92.0   | 71.0     | 162.4   | 85.6   | 55.8   | 0.999     |
| Kellan  | 90.6   | 64.8     | 155.0   | 81.4   | 52.2   | 0.964     |
| Maria   | 86.2   | 56.8     | 142.5   | 77.0   | 46.8   | 0.887     |
| Payton  | 86.2   | 56.8     | 142.5   | 77.0   | 46.8   | 0.883     |
| Manny   | 95.1   | 67.1     | 161.8   | 85.6   | 55.0   | 0.963     |

(The recorded user's HMD standing baseline is ~169cm eye height, so the S
ordering tracks avatar stature: tall Hudson ~1.0, short Payton 0.88.)

## Driven/native ratios (lock=1 window medians) — GATE: all within ±10%

| Avatar  | hipR  | torsoR | headR | legR  | armR  |
| ------- | ----- | ------ | ----- | ----- | ----- |
| Wallace | 0.968 | 1.004  | 0.977 | 1.000 | 1.000 |
| Emory   | 0.969 | 1.007  | 0.978 | 1.000 | 1.000 |
| Hudson  | 0.960 | 1.013  | 0.979 | 1.000 | 1.000 |
| Kellan  | 0.966 | 1.005  | 0.980 | 1.000 | 1.000 |
| Maria   | 0.968 | 1.008  | 0.980 | 1.000 | 1.000 |
| Payton  | 0.963 | 1.008  | 0.977 | 1.000 | 1.000 |
| Manny   | 0.970 | 1.006  | 0.981 | 1.000 | 1.000 |

The ~3-4% hip/head compression is the replay's recorded squat content
(scafAlpha < 1), identical across the cast; leg/arm segment sums are exactly
native everywhere — nothing stretches anyone.

## Bone-Z world probes (cm), lock ON vs OFF

| Avatar  | pelvis ON | pelvis OFF | head ON | head OFF | foot_l ON | foot_l OFF |
| ------- | --------- | ---------- | ------- | -------- | --------- | ---------- |
| Wallace | 90.20     | 89.59      | 149.60  | 149.06   | 8.16      | 8.17       |
| Emory   | 85.28     | 84.62      | 143.50  | 142.99   | 7.92      | 7.92       |
| Hudson  | 88.80     | 88.07      | 159.44  | 158.92   | 8.24      | 8.24       |
| Kellan  | 88.13     | 87.77      | 152.51  | 152.25   | 7.88      | 8.16       |
| Maria   | 84.21     | 84.14      | 140.38  | 140.43   | 7.15      | 7.66       |
| Payton  | 83.79     | 83.17      | 139.98  | 139.40   | 7.16      | 7.16       |
| Manny   | 92.11     | 91.39      | 158.70  | 158.11   | 8.24      | 8.24       |

ON/OFF deltas are sub-centimeter and match the unmapped Manny control's own
drift between the two probe instants (the fused height writer the lock maps is
inert in replay and in the accepted live stack — mp.BodyFusion.WritePose=0).
Raw rows: `phase4_cast_rows_*.log` per avatar.
