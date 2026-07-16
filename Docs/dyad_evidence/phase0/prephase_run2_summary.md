# Tracking-quality baseline summary: prephase_run2

## Row counts

| family | rows |
| ------ | ---- |
| mp.FootSkateTrace | 2096 |
| mp.WristLimitTrace | 601 |
| mp.WebcamAgeTrace | 0 |
| mp.ArmJumpTrace | 0 |
| mp.ArmDirCorrection | 0 |
| mp.ArmOverheadRescue | 0 |
| mp.QuestWristSolve | 554 |
| mp.ChainReachExtend | 0 |
| mp.MediaPipeLegScaffold | 140 |

## mp.FootSkateTrace (foot-skate scoreboard)

| metric | n | p50 | p90 | p99 | max |
| ------ | - | --- | --- | --- | --- |
| MP_LiveMediaPipeManny L planted planarSpdCmS (n_planted/524) | 0 | n/a | n/a | n/a | n/a |
| MP_LiveMediaPipeManny L penetrCm | 524 | 0.00 | 1.18 | 3.64 | 8.48 |
| MP_LiveMediaPipeManny L liftCm | 524 | 0.00 | 0.00 | 0.00 | 0.00 |
| MP_LiveMediaPipeManny R planted planarSpdCmS (n_planted/524) | 0 | n/a | n/a | n/a | n/a |
| MP_LiveMediaPipeManny R penetrCm | 524 | 0.00 | 1.36 | 3.58 | 5.81 |
| MP_LiveMediaPipeManny R liftCm | 524 | 0.00 | 0.00 | 0.00 | 0.00 |
| MP_LiveMetaHumanKellan L planted planarSpdCmS (n_planted/524) | 0 | n/a | n/a | n/a | n/a |
| MP_LiveMetaHumanKellan L penetrCm | 524 | 0.00 | 1.16 | 3.48 | 8.06 |
| MP_LiveMetaHumanKellan L liftCm | 524 | 0.00 | 0.00 | 0.00 | 0.00 |
| MP_LiveMetaHumanKellan R planted planarSpdCmS (n_planted/524) | 0 | n/a | n/a | n/a | n/a |
| MP_LiveMetaHumanKellan R penetrCm | 524 | 0.00 | 1.35 | 3.52 | 5.53 |
| MP_LiveMetaHumanKellan R liftCm | 524 | 0.00 | 0.00 | 0.00 | 0.00 |

## mp.WristLimitTrace (anatomical envelope, report-only)

| metric | n | p50 | p90 | p99 | max |
| ------ | - | --- | --- | --- | --- |
| MP_LiveMediaPipeManny L |twistDeg| | 148 | 8.50 | 54.40 | 105.50 | 110.20 |
| MP_LiveMediaPipeManny L swingDeg | 148 | 17.40 | 62.20 | 90.90 | 93.10 |
| MP_LiveMediaPipeManny L twistExcessDeg (out rows: 16) | 16 | 0.00 | 15.50 | 20.20 | 20.20 |
| MP_LiveMediaPipeManny L swingExcessDeg | 16 | 2.40 | 5.90 | 8.10 | 8.10 |
| MP_LiveMediaPipeManny R |twistDeg| | 153 | 14.90 | 41.10 | 57.50 | 59.00 |
| MP_LiveMediaPipeManny R swingDeg | 153 | 14.80 | 86.40 | 95.30 | 103.50 |
| MP_LiveMediaPipeManny R twistExcessDeg (out rows: 19) | 19 | 0.00 | 0.00 | 0.00 | 0.00 |
| MP_LiveMediaPipeManny R swingExcessDeg | 19 | 4.20 | 10.30 | 18.50 | 18.50 |
| MP_LiveMetaHumanKellan L |twistDeg| | 146 | 11.20 | 49.60 | 74.90 | 78.80 |
| MP_LiveMetaHumanKellan L swingDeg | 146 | 15.80 | 63.10 | 91.20 | 93.20 |
| MP_LiveMetaHumanKellan L twistExcessDeg (out rows: 14) | 14 | 0.00 | 0.00 | 0.00 | 0.00 |
| MP_LiveMetaHumanKellan L swingExcessDeg | 14 | 2.50 | 6.20 | 8.20 | 8.20 |
| MP_LiveMetaHumanKellan R |twistDeg| | 154 | 14.40 | 47.50 | 60.00 | 62.50 |
| MP_LiveMetaHumanKellan R swingDeg | 154 | 16.10 | 86.60 | 95.40 | 101.20 |
| MP_LiveMetaHumanKellan R twistExcessDeg (out rows: 20) | 20 | 0.00 | 0.00 | 0.00 | 0.00 |
| MP_LiveMetaHumanKellan R swingExcessDeg | 20 | 4.20 | 10.40 | 16.20 | 16.20 |

## mp.MediaPipeLegScaffold (pre-plan leg fingerprint)

| metric | n | p50 | p90 | p99 | max |
| ------ | - | --- | --- | --- | --- |
| MP_LiveMediaPipeManny L liftCm (grounded 0/70) | 70 | 0.00 | 0.00 | 0.00 | 0.00 |
| MP_LiveMediaPipeManny R liftCm (grounded 0/70) | 70 | 0.00 | 0.00 | 0.00 | 0.00 |
| MP_LiveMetaHumanKellan L liftCm (grounded 0/70) | 70 | 0.00 | 0.00 | 0.00 | 0.00 |
| MP_LiveMetaHumanKellan R liftCm (grounded 0/70) | 70 | 0.00 | 0.00 | 0.00 | 0.00 |

## Cadence sanity (rows/side, starvation check)

- mp.QuestWristSolve: {('MP_LiveMediaPipeManny', 'L'): 138, ('MP_LiveMediaPipeManny', 'R'): 138, ('MP_LiveMetaHumanKellan', 'L'): 138, ('MP_LiveMetaHumanKellan', 'R'): 138, ('MediaPipePoseDrivenSkeletalActor_0', 'L'): 1, ('MediaPipePoseDrivenSkeletalActor_0', 'R'): 1}

