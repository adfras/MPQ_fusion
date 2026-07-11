# Tracking-quality baseline summary: p3_replay_distrustoff

## Row counts

| family | rows |
| ------ | ---- |
| mp.FootSkateTrace | 1882 |
| mp.WristLimitTrace | 0 |
| mp.WebcamAgeTrace | 0 |
| mp.ArmJumpTrace | 0 |
| mp.ArmDirCorrection | 0 |
| mp.ArmOverheadRescue | 0 |
| mp.QuestWristSolve | 498 |
| mp.ChainReachExtend | 0 |
| mp.MediaPipeLegScaffold | 126 |

## mp.FootSkateTrace (foot-skate scoreboard)

| metric | n | p50 | p90 | p99 | max |
| ------ | - | --- | --- | --- | --- |
| MP_LiveMediaPipeManny L planted planarSpdCmS (n_planted/471) | 445 | 5.50 | 17.50 | 29.20 | 32.20 |
| MP_LiveMediaPipeManny L penetrCm | 471 | 0.01 | 0.21 | 1.68 | 5.25 |
| MP_LiveMediaPipeManny L liftCm | 471 | 1.00 | 2.40 | 3.60 | 4.10 |
| MP_LiveMediaPipeManny R planted planarSpdCmS (n_planted/471) | 432 | 4.50 | 17.70 | 30.50 | 32.80 |
| MP_LiveMediaPipeManny R penetrCm | 471 | 0.00 | 0.25 | 1.36 | 1.99 |
| MP_LiveMediaPipeManny R liftCm | 471 | 1.00 | 3.30 | 5.20 | 7.00 |
| MP_LiveMetaHumanKellan L planted planarSpdCmS (n_planted/470) | 446 | 5.50 | 18.50 | 29.30 | 32.50 |
| MP_LiveMetaHumanKellan L penetrCm | 470 | 0.00 | 0.20 | 1.59 | 4.99 |
| MP_LiveMetaHumanKellan L liftCm | 470 | 1.00 | 2.40 | 3.60 | 4.10 |
| MP_LiveMetaHumanKellan R planted planarSpdCmS (n_planted/470) | 432 | 4.80 | 17.70 | 30.70 | 32.50 |
| MP_LiveMetaHumanKellan R penetrCm | 470 | 0.00 | 0.23 | 1.29 | 1.88 |
| MP_LiveMetaHumanKellan R liftCm | 470 | 1.00 | 3.40 | 5.20 | 6.90 |

## mp.MediaPipeLegScaffold (pre-plan leg fingerprint)

| metric | n | p50 | p90 | p99 | max |
| ------ | - | --- | --- | --- | --- |
| MP_LiveMediaPipeManny L liftCm (grounded 59/63) | 63 | 1.00 | 2.60 | 2.90 | 3.80 |
| MP_LiveMediaPipeManny R liftCm (grounded 56/63) | 63 | 0.80 | 2.80 | 4.60 | 6.40 |
| MP_LiveMetaHumanKellan L liftCm (grounded 59/63) | 63 | 1.00 | 2.60 | 2.90 | 3.80 |
| MP_LiveMetaHumanKellan R liftCm (grounded 56/63) | 63 | 0.80 | 2.80 | 4.60 | 6.40 |

## Cadence sanity (rows/side, starvation check)

- mp.QuestWristSolve: {('MP_LiveMediaPipeManny', 'L'): 124, ('MP_LiveMediaPipeManny', 'R'): 124, ('MP_LiveMetaHumanKellan', 'L'): 124, ('MP_LiveMetaHumanKellan', 'R'): 124, ('MediaPipePoseDrivenSkeletalActor_0', 'L'): 1, ('MediaPipePoseDrivenSkeletalActor_0', 'R'): 1}

