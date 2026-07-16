# Tracking-quality baseline summary: live_ghost_on

## Row counts

| family | rows |
| ------ | ---- |
| mp.FootSkateTrace | 1524 |
| mp.WristLimitTrace | 680 |
| mp.WebcamAgeTrace | 768 |
| mp.ArmJumpTrace | 38 |
| mp.ArmDirCorrection | 202 |
| mp.ArmOverheadRescue | 404 |
| mp.QuestWristSolve | 1536 |
| mp.ChainReachExtend | 202 |
| mp.MediaPipeLegScaffold | 102 |

## mp.FootSkateTrace (foot-skate scoreboard)

| metric | n | p50 | p90 | p99 | max |
| ------ | - | --- | --- | --- | --- |
| MP_DyadGhostDriver L planted planarSpdCmS (n_planted/381) | 364 | 4.00 | 15.00 | 29.50 | 32.20 |
| MP_DyadGhostDriver L penetrCm | 381 | 0.00 | 0.17 | 2.04 | 2.06 |
| MP_DyadGhostDriver L liftCm | 381 | 1.30 | 2.80 | 4.60 | 7.30 |
| MP_DyadGhostDriver R planted planarSpdCmS (n_planted/381) | 347 | 4.40 | 15.30 | 29.10 | 32.30 |
| MP_DyadGhostDriver R penetrCm | 381 | 0.00 | 0.00 | 0.00 | 1.20 |
| MP_DyadGhostDriver R liftCm | 381 | 1.10 | 3.20 | 5.00 | 6.30 |
| MP_DyadGhostMaria L planted planarSpdCmS (n_planted/381) | 364 | 4.00 | 14.90 | 29.70 | 32.60 |
| MP_DyadGhostMaria L penetrCm | 381 | 0.00 | 0.10 | 1.73 | 1.75 |
| MP_DyadGhostMaria L liftCm | 381 | 1.30 | 2.80 | 4.60 | 7.30 |
| MP_DyadGhostMaria R planted planarSpdCmS (n_planted/381) | 347 | 4.30 | 15.40 | 29.50 | 32.50 |
| MP_DyadGhostMaria R penetrCm | 381 | 0.00 | 0.00 | 0.00 | 0.27 |
| MP_DyadGhostMaria R liftCm | 381 | 1.10 | 3.20 | 5.00 | 6.30 |

## mp.WristLimitTrace (anatomical envelope, report-only)

| metric | n | p50 | p90 | p99 | max |
| ------ | - | --- | --- | --- | --- |
| MP_DyadGhostDriver L |twistDeg| | 118 | 34.60 | 64.00 | 98.30 | 105.90 |
| MP_DyadGhostDriver L swingDeg | 118 | 37.80 | 97.60 | 117.90 | 122.70 |
| MP_DyadGhostDriver L twistExcessDeg (out rows: 32) | 32 | 0.00 | 0.00 | 15.90 | 15.90 |
| MP_DyadGhostDriver L swingExcessDeg | 32 | 10.60 | 26.80 | 37.70 | 37.70 |
| MP_DyadGhostDriver R |twistDeg| | 129 | 20.50 | 64.20 | 86.60 | 93.10 |
| MP_DyadGhostDriver R swingDeg | 129 | 54.00 | 91.20 | 122.40 | 124.40 |
| MP_DyadGhostDriver R twistExcessDeg (out rows: 38) | 38 | 0.00 | 0.00 | 3.10 | 3.10 |
| MP_DyadGhostDriver R swingExcessDeg | 38 | 4.30 | 17.00 | 39.40 | 39.40 |
| MP_DyadGhostMaria L |twistDeg| | 185 | 51.00 | 151.10 | 176.70 | 177.70 |
| MP_DyadGhostMaria L swingDeg | 185 | 76.60 | 95.60 | 117.30 | 124.70 |
| MP_DyadGhostMaria L twistExcessDeg (out rows: 112) | 112 | 0.00 | 68.00 | 87.30 | 87.70 |
| MP_DyadGhostMaria L swingExcessDeg | 112 | 1.00 | 14.80 | 32.60 | 39.70 |
| MP_DyadGhostMaria R |twistDeg| | 248 | 62.00 | 117.60 | 131.00 | 158.30 |
| MP_DyadGhostMaria R swingDeg | 248 | 94.60 | 129.50 | 153.40 | 174.10 |
| MP_DyadGhostMaria R twistExcessDeg (out rows: 185) | 185 | 0.00 | 31.60 | 41.00 | 68.30 |
| MP_DyadGhostMaria R swingExcessDeg | 185 | 22.40 | 46.70 | 68.40 | 89.10 |

## mp.WebcamAgeTrace (measurement age + current-pose residuals)

| metric | n | p50 | p90 | p99 | max |
| ------ | - | --- | --- | --- | --- |
| MP_DyadGhostMaria L ageMs | 381 | 0.40 | 0.60 | 0.80 | 1.20 |
| MP_DyadGhostMaria L predMs | 381 | 0.00 | 0.00 | 0.00 | 0.00 |
| MP_DyadGhostMaria L effAgeMs | 384 | 0.40 | 0.60 | 0.80 | 1.20 |
| MP_DyadGhostMaria L QUIET wristResidDeg | 246 | 14.20 | 24.30 | 33.70 | 49.40 |
| MP_DyadGhostMaria L MOVING wristResidDeg | 135 | 20.60 | 32.60 | 46.20 | 52.10 |
| MP_DyadGhostMaria L MOVING elbowResidDeg | 135 | 30.70 | 50.70 | 64.90 | 66.40 |
| MP_DyadGhostMaria R ageMs | 381 | 0.90 | 1.10 | 1.50 | 2.10 |
| MP_DyadGhostMaria R predMs | 381 | 0.00 | 0.00 | 0.00 | 0.00 |
| MP_DyadGhostMaria R effAgeMs | 384 | 0.90 | 1.10 | 1.50 | 2.10 |
| MP_DyadGhostMaria R QUIET wristResidDeg | 268 | 20.20 | 38.00 | 45.00 | 50.60 |
| MP_DyadGhostMaria R MOVING wristResidDeg | 113 | 27.70 | 45.20 | 55.10 | 61.20 |
| MP_DyadGhostMaria R MOVING elbowResidDeg | 113 | 38.20 | 59.50 | 68.40 | 71.10 |

## mp.ArmJumpTrace (pre-plan event fingerprint)

| metric | n | p50 | p90 | p99 | max |
| ------ | - | --- | --- | --- | --- |
| MP_DyadGhostMaria L residCm | 16 | 1.60 | 3.50 | 4.90 | 4.90 |
| MP_DyadGhostMaria R residCm | 22 | 1.40 | 1.80 | 2.10 | 2.10 |

## mp.ArmDirCorrection (pre-plan drift fingerprint)

| metric | n | p50 | p90 | p99 | max |
| ------ | - | --- | --- | --- | --- |
| MP_DyadGhostMaria L elbowCorrDeg | 101 | 18.90 | 20.00 | 20.00 | 20.00 |
| MP_DyadGhostMaria L wristCorrDeg | 101 | 13.00 | 20.00 | 20.00 | 20.00 |
| MP_DyadGhostMaria R elbowCorrDeg | 101 | 20.00 | 20.00 | 20.00 | 20.00 |
| MP_DyadGhostMaria R wristCorrDeg | 101 | 20.00 | 20.00 | 20.00 | 20.00 |

## mp.MediaPipeLegScaffold (pre-plan leg fingerprint)

| metric | n | p50 | p90 | p99 | max |
| ------ | - | --- | --- | --- | --- |
| MP_DyadGhostDriver L liftCm (grounded 50/51) | 51 | 1.20 | 2.70 | 3.40 | 3.40 |
| MP_DyadGhostDriver R liftCm (grounded 46/51) | 51 | 1.40 | 3.70 | 4.20 | 4.20 |
| MP_DyadGhostMaria L liftCm (grounded 50/51) | 51 | 1.20 | 2.70 | 3.40 | 3.40 |
| MP_DyadGhostMaria R liftCm (grounded 46/51) | 51 | 1.40 | 3.70 | 4.20 | 4.20 |

## Cadence sanity (rows/side, starvation check)

- mp.ArmOverheadRescue: {('MP_DyadGhostDriver', 'L'): 101, ('MP_DyadGhostDriver', 'R'): 101, ('MP_DyadGhostMaria', 'L'): 101, ('MP_DyadGhostMaria', 'R'): 101}
- mp.QuestWristSolve: {('MP_DyadGhostDriver', 'L'): 384, ('MP_DyadGhostDriver', 'R'): 384, ('MP_DyadGhostMaria', 'L'): 384, ('MP_DyadGhostMaria', 'R'): 384}
- mp.ChainReachExtend: {('MP_DyadGhostMaria', 'L'): 101, ('MP_DyadGhostMaria', 'R'): 101}

