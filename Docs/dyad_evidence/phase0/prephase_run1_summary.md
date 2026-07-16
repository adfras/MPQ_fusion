# Tracking-quality baseline summary: prephase_run1

## Row counts

| family | rows |
| ------ | ---- |
| mp.FootSkateTrace | 2022 |
| mp.WristLimitTrace | 587 |
| mp.WebcamAgeTrace | 0 |
| mp.ArmJumpTrace | 0 |
| mp.ArmDirCorrection | 0 |
| mp.ArmOverheadRescue | 0 |
| mp.QuestWristSolve | 538 |
| mp.ChainReachExtend | 0 |
| mp.MediaPipeLegScaffold | 136 |

## mp.FootSkateTrace (foot-skate scoreboard)

| metric | n | p50 | p90 | p99 | max |
| ------ | - | --- | --- | --- | --- |
| MP_LiveMediaPipeManny L planted planarSpdCmS (n_planted/506) | 0 | n/a | n/a | n/a | n/a |
| MP_LiveMediaPipeManny L penetrCm | 506 | 0.00 | 0.87 | 3.67 | 6.87 |
| MP_LiveMediaPipeManny L liftCm | 506 | 0.00 | 0.00 | 0.00 | 0.00 |
| MP_LiveMediaPipeManny R planted planarSpdCmS (n_planted/506) | 0 | n/a | n/a | n/a | n/a |
| MP_LiveMediaPipeManny R penetrCm | 506 | 0.00 | 1.31 | 3.80 | 4.48 |
| MP_LiveMediaPipeManny R liftCm | 506 | 0.00 | 0.00 | 0.00 | 0.00 |
| MP_LiveMetaHumanKellan L planted planarSpdCmS (n_planted/505) | 0 | n/a | n/a | n/a | n/a |
| MP_LiveMetaHumanKellan L penetrCm | 505 | 0.00 | 1.06 | 3.50 | 8.06 |
| MP_LiveMetaHumanKellan L liftCm | 505 | 0.00 | 0.00 | 0.00 | 0.00 |
| MP_LiveMetaHumanKellan R planted planarSpdCmS (n_planted/505) | 0 | n/a | n/a | n/a | n/a |
| MP_LiveMetaHumanKellan R penetrCm | 505 | 0.00 | 1.25 | 3.52 | 5.53 |
| MP_LiveMetaHumanKellan R liftCm | 505 | 0.00 | 0.00 | 0.00 | 0.00 |

## mp.WristLimitTrace (anatomical envelope, report-only)

| metric | n | p50 | p90 | p99 | max |
| ------ | - | --- | --- | --- | --- |
| MP_LiveMediaPipeManny L |twistDeg| | 142 | 8.20 | 59.20 | 105.50 | 110.20 |
| MP_LiveMediaPipeManny L swingDeg | 142 | 18.30 | 63.10 | 91.30 | 93.10 |
| MP_LiveMediaPipeManny L twistExcessDeg (out rows: 15) | 15 | 0.00 | 15.50 | 20.20 | 20.20 |
| MP_LiveMediaPipeManny L swingExcessDeg | 15 | 4.50 | 6.30 | 8.10 | 8.10 |
| MP_LiveMediaPipeManny R |twistDeg| | 152 | 16.60 | 46.10 | 58.40 | 59.00 |
| MP_LiveMediaPipeManny R swingDeg | 152 | 17.00 | 86.90 | 95.40 | 104.00 |
| MP_LiveMediaPipeManny R twistExcessDeg (out rows: 20) | 20 | 0.00 | 0.00 | 0.00 | 0.00 |
| MP_LiveMediaPipeManny R swingExcessDeg | 20 | 4.40 | 10.40 | 19.00 | 19.00 |
| MP_LiveMetaHumanKellan L |twistDeg| | 142 | 11.60 | 59.70 | 79.00 | 90.00 |
| MP_LiveMetaHumanKellan L swingDeg | 142 | 17.10 | 72.80 | 91.20 | 93.20 |
| MP_LiveMetaHumanKellan L twistExcessDeg (out rows: 15) | 15 | 0.00 | 0.00 | 0.00 | 0.00 |
| MP_LiveMetaHumanKellan L swingExcessDeg | 15 | 5.00 | 6.20 | 8.20 | 8.20 |
| MP_LiveMetaHumanKellan R |twistDeg| | 151 | 15.20 | 49.10 | 61.80 | 62.50 |
| MP_LiveMetaHumanKellan R swingDeg | 151 | 17.80 | 86.10 | 94.70 | 102.00 |
| MP_LiveMetaHumanKellan R twistExcessDeg (out rows: 19) | 19 | 0.00 | 0.00 | 0.00 | 0.00 |
| MP_LiveMetaHumanKellan R swingExcessDeg | 19 | 4.00 | 9.70 | 17.00 | 17.00 |

## mp.MediaPipeLegScaffold (pre-plan leg fingerprint)

| metric | n | p50 | p90 | p99 | max |
| ------ | - | --- | --- | --- | --- |
| MP_LiveMediaPipeManny L liftCm (grounded 0/68) | 68 | 0.00 | 0.00 | 0.00 | 0.00 |
| MP_LiveMediaPipeManny R liftCm (grounded 0/68) | 68 | 0.00 | 0.00 | 0.00 | 0.00 |
| MP_LiveMetaHumanKellan L liftCm (grounded 0/68) | 68 | 0.00 | 0.00 | 0.00 | 0.00 |
| MP_LiveMetaHumanKellan R liftCm (grounded 0/68) | 68 | 0.00 | 0.00 | 0.00 | 0.00 |

## Cadence sanity (rows/side, starvation check)

- mp.QuestWristSolve: {('MP_LiveMediaPipeManny', 'L'): 134, ('MP_LiveMediaPipeManny', 'R'): 134, ('MP_LiveMetaHumanKellan', 'L'): 134, ('MP_LiveMetaHumanKellan', 'R'): 134, ('MediaPipePoseDrivenSkeletalActor_0', 'L'): 1, ('MediaPipePoseDrivenSkeletalActor_0', 'R'): 1}

