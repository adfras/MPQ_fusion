# Tracking-quality baseline summary: postphase_defaultsoff

## Row counts

| family | rows |
| ------ | ---- |
| mp.FootSkateTrace | 1936 |
| mp.WristLimitTrace | 563 |
| mp.WebcamAgeTrace | 0 |
| mp.ArmJumpTrace | 0 |
| mp.ArmDirCorrection | 0 |
| mp.ArmOverheadRescue | 0 |
| mp.QuestWristSolve | 514 |
| mp.ChainReachExtend | 0 |
| mp.MediaPipeLegScaffold | 130 |

## mp.FootSkateTrace (foot-skate scoreboard)

| metric | n | p50 | p90 | p99 | max |
| ------ | - | --- | --- | --- | --- |
| MP_LiveMediaPipeManny L planted planarSpdCmS (n_planted/484) | 0 | n/a | n/a | n/a | n/a |
| MP_LiveMediaPipeManny L penetrCm | 484 | 0.00 | 0.75 | 3.10 | 3.51 |
| MP_LiveMediaPipeManny L liftCm | 484 | 0.00 | 0.00 | 0.00 | 0.00 |
| MP_LiveMediaPipeManny R planted planarSpdCmS (n_planted/484) | 0 | n/a | n/a | n/a | n/a |
| MP_LiveMediaPipeManny R penetrCm | 484 | 0.00 | 0.80 | 3.11 | 3.44 |
| MP_LiveMediaPipeManny R liftCm | 484 | 0.00 | 0.00 | 0.00 | 0.00 |
| MP_LiveMetaHumanKellan L planted planarSpdCmS (n_planted/484) | 0 | n/a | n/a | n/a | n/a |
| MP_LiveMetaHumanKellan L penetrCm | 484 | 0.00 | 0.70 | 2.96 | 3.35 |
| MP_LiveMetaHumanKellan L liftCm | 484 | 0.00 | 0.00 | 0.00 | 0.00 |
| MP_LiveMetaHumanKellan R planted planarSpdCmS (n_planted/484) | 0 | n/a | n/a | n/a | n/a |
| MP_LiveMetaHumanKellan R penetrCm | 484 | 0.00 | 0.75 | 2.97 | 3.28 |
| MP_LiveMetaHumanKellan R liftCm | 484 | 0.00 | 0.00 | 0.00 | 0.00 |

## mp.WristLimitTrace (anatomical envelope, report-only)

| metric | n | p50 | p90 | p99 | max |
| ------ | - | --- | --- | --- | --- |
| MP_LiveMediaPipeManny L |twistDeg| | 137 | 8.00 | 65.20 | 105.50 | 110.20 |
| MP_LiveMediaPipeManny L swingDeg | 137 | 18.60 | 73.30 | 91.70 | 93.10 |
| MP_LiveMediaPipeManny L twistExcessDeg (out rows: 16) | 16 | 0.00 | 15.50 | 20.20 | 20.20 |
| MP_LiveMediaPipeManny L swingExcessDeg | 16 | 4.50 | 6.70 | 8.10 | 8.10 |
| MP_LiveMediaPipeManny R |twistDeg| | 146 | 16.50 | 41.90 | 59.00 | 59.30 |
| MP_LiveMediaPipeManny R swingDeg | 146 | 16.70 | 87.80 | 103.50 | 104.40 |
| MP_LiveMediaPipeManny R twistExcessDeg (out rows: 23) | 23 | 0.00 | 0.00 | 0.00 | 0.00 |
| MP_LiveMediaPipeManny R swingExcessDeg | 23 | 5.00 | 10.40 | 19.40 | 19.40 |
| MP_LiveMetaHumanKellan L |twistDeg| | 136 | 12.10 | 62.50 | 79.00 | 90.00 |
| MP_LiveMetaHumanKellan L swingDeg | 136 | 17.90 | 85.10 | 91.20 | 93.20 |
| MP_LiveMetaHumanKellan L twistExcessDeg (out rows: 15) | 15 | 0.00 | 0.00 | 0.00 | 0.00 |
| MP_LiveMetaHumanKellan L swingExcessDeg | 15 | 4.80 | 6.20 | 8.20 | 8.20 |
| MP_LiveMetaHumanKellan R |twistDeg| | 144 | 14.40 | 46.40 | 62.00 | 62.50 |
| MP_LiveMetaHumanKellan R swingDeg | 144 | 17.20 | 87.80 | 101.20 | 102.00 |
| MP_LiveMetaHumanKellan R twistExcessDeg (out rows: 19) | 19 | 0.00 | 0.00 | 0.00 | 0.00 |
| MP_LiveMetaHumanKellan R swingExcessDeg | 19 | 4.70 | 9.70 | 17.00 | 17.00 |

## mp.MediaPipeLegScaffold (pre-plan leg fingerprint)

| metric | n | p50 | p90 | p99 | max |
| ------ | - | --- | --- | --- | --- |
| MP_LiveMediaPipeManny L liftCm (grounded 0/65) | 65 | 0.00 | 0.00 | 0.00 | 0.00 |
| MP_LiveMediaPipeManny R liftCm (grounded 0/65) | 65 | 0.00 | 0.00 | 0.00 | 0.00 |
| MP_LiveMetaHumanKellan L liftCm (grounded 0/65) | 65 | 0.00 | 0.00 | 0.00 | 0.00 |
| MP_LiveMetaHumanKellan R liftCm (grounded 0/65) | 65 | 0.00 | 0.00 | 0.00 | 0.00 |

## Cadence sanity (rows/side, starvation check)

- mp.QuestWristSolve: {('MP_LiveMediaPipeManny', 'L'): 128, ('MP_LiveMediaPipeManny', 'R'): 128, ('MP_LiveMetaHumanKellan', 'L'): 128, ('MP_LiveMetaHumanKellan', 'R'): 128, ('MediaPipePoseDrivenSkeletalActor_0', 'L'): 1, ('MediaPipePoseDrivenSkeletalActor_0', 'R'): 1}

