# Tracking-quality baseline summary: p3_replay_distruston

## Row counts

| family | rows |
| ------ | ---- |
| mp.FootSkateTrace | 1874 |
| mp.WristLimitTrace | 0 |
| mp.WebcamAgeTrace | 0 |
| mp.ArmJumpTrace | 0 |
| mp.ArmDirCorrection | 0 |
| mp.ArmOverheadRescue | 0 |
| mp.QuestWristSolve | 496 |
| mp.ChainReachExtend | 0 |
| mp.MediaPipeLegScaffold | 126 |

## mp.FootSkateTrace (foot-skate scoreboard)

| metric | n | p50 | p90 | p99 | max |
| ------ | - | --- | --- | --- | --- |
| MP_LiveMediaPipeManny L planted planarSpdCmS (n_planted/469) | 441 | 5.50 | 17.80 | 29.20 | 32.30 |
| MP_LiveMediaPipeManny L penetrCm | 469 | 0.09 | 0.26 | 1.71 | 5.65 |
| MP_LiveMediaPipeManny L liftCm | 469 | 1.00 | 2.40 | 3.50 | 4.10 |
| MP_LiveMediaPipeManny R planted planarSpdCmS (n_planted/469) | 429 | 4.90 | 18.30 | 29.10 | 32.30 |
| MP_LiveMediaPipeManny R penetrCm | 469 | 0.00 | 0.43 | 1.42 | 1.70 |
| MP_LiveMediaPipeManny R liftCm | 469 | 0.90 | 3.30 | 5.20 | 7.00 |
| MP_LiveMetaHumanKellan L planted planarSpdCmS (n_planted/468) | 443 | 5.60 | 17.60 | 30.50 | 33.00 |
| MP_LiveMetaHumanKellan L penetrCm | 468 | 0.08 | 0.24 | 1.62 | 5.61 |
| MP_LiveMetaHumanKellan L liftCm | 468 | 1.00 | 2.40 | 3.40 | 4.10 |
| MP_LiveMetaHumanKellan R planted planarSpdCmS (n_planted/468) | 432 | 5.00 | 18.30 | 30.30 | 32.90 |
| MP_LiveMetaHumanKellan R penetrCm | 468 | 0.00 | 0.41 | 1.31 | 1.62 |
| MP_LiveMetaHumanKellan R liftCm | 468 | 0.90 | 3.30 | 5.20 | 7.00 |

## mp.MediaPipeLegScaffold (pre-plan leg fingerprint)

| metric | n | p50 | p90 | p99 | max |
| ------ | - | --- | --- | --- | --- |
| MP_LiveMediaPipeManny L liftCm (grounded 61/63) | 63 | 1.00 | 2.10 | 3.40 | 3.80 |
| MP_LiveMediaPipeManny R liftCm (grounded 59/63) | 63 | 0.90 | 3.60 | 4.90 | 5.40 |
| MP_LiveMetaHumanKellan L liftCm (grounded 59/63) | 63 | 1.20 | 2.40 | 3.50 | 3.60 |
| MP_LiveMetaHumanKellan R liftCm (grounded 56/63) | 63 | 0.90 | 3.60 | 5.20 | 6.90 |

## Cadence sanity (rows/side, starvation check)

- mp.QuestWristSolve: {('MP_LiveMediaPipeManny', 'L'): 124, ('MP_LiveMediaPipeManny', 'R'): 124, ('MP_LiveMetaHumanKellan', 'L'): 123, ('MP_LiveMetaHumanKellan', 'R'): 123, ('MediaPipePoseDrivenSkeletalActor_0', 'L'): 1, ('MediaPipePoseDrivenSkeletalActor_0', 'R'): 1}

