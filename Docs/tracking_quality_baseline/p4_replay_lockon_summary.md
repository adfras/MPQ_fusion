# Tracking-quality baseline summary: p4_replay_lockon

## Row counts

| family | rows |
| ------ | ---- |
| mp.FootSkateTrace | 1844 |
| mp.WristLimitTrace | 0 |
| mp.WebcamAgeTrace | 0 |
| mp.ArmJumpTrace | 0 |
| mp.ArmDirCorrection | 0 |
| mp.ArmOverheadRescue | 0 |
| mp.QuestWristSolve | 486 |
| mp.ChainReachExtend | 0 |
| mp.MediaPipeLegScaffold | 122 |

## mp.FootSkateTrace (foot-skate scoreboard)

| metric | n | p50 | p90 | p99 | max |
| ------ | - | --- | --- | --- | --- |
| MP_LiveMediaPipeManny L planted planarSpdCmS (n_planted/461) | 441 | 5.30 | 19.30 | 31.30 | 33.00 |
| MP_LiveMediaPipeManny L penetrCm | 461 | 0.11 | 0.24 | 1.71 | 2.20 |
| MP_LiveMediaPipeManny L liftCm | 461 | 1.00 | 2.40 | 3.60 | 4.00 |
| MP_LiveMediaPipeManny R planted planarSpdCmS (n_planted/461) | 421 | 4.80 | 18.80 | 28.20 | 32.50 |
| MP_LiveMediaPipeManny R penetrCm | 461 | 0.00 | 0.23 | 1.09 | 1.27 |
| MP_LiveMediaPipeManny R liftCm | 461 | 1.10 | 3.40 | 5.20 | 6.90 |
| MP_LiveMetaHumanKellan L planted planarSpdCmS (n_planted/461) | 439 | 5.30 | 18.90 | 30.90 | 32.60 |
| MP_LiveMetaHumanKellan L penetrCm | 461 | 0.10 | 0.23 | 1.63 | 2.14 |
| MP_LiveMetaHumanKellan L liftCm | 461 | 1.00 | 2.40 | 3.60 | 4.00 |
| MP_LiveMetaHumanKellan R planted planarSpdCmS (n_planted/461) | 421 | 4.80 | 18.80 | 28.60 | 32.30 |
| MP_LiveMetaHumanKellan R penetrCm | 461 | 0.00 | 0.22 | 1.02 | 1.21 |
| MP_LiveMetaHumanKellan R liftCm | 461 | 1.10 | 3.40 | 5.20 | 6.90 |

## mp.MediaPipeLegScaffold (pre-plan leg fingerprint)

| metric | n | p50 | p90 | p99 | max |
| ------ | - | --- | --- | --- | --- |
| MP_LiveMediaPipeManny L liftCm (grounded 56/61) | 61 | 1.20 | 2.30 | 3.20 | 3.60 |
| MP_LiveMediaPipeManny R liftCm (grounded 53/61) | 61 | 0.70 | 3.60 | 4.50 | 7.00 |
| MP_LiveMetaHumanKellan L liftCm (grounded 57/61) | 61 | 1.20 | 2.30 | 3.20 | 3.60 |
| MP_LiveMetaHumanKellan R liftCm (grounded 53/61) | 61 | 0.70 | 3.60 | 4.50 | 7.00 |

## Cadence sanity (rows/side, starvation check)

- mp.QuestWristSolve: {('MP_LiveMediaPipeManny', 'L'): 121, ('MP_LiveMediaPipeManny', 'R'): 121, ('MP_LiveMetaHumanKellan', 'L'): 121, ('MP_LiveMetaHumanKellan', 'R'): 121, ('MediaPipePoseDrivenSkeletalActor_0', 'L'): 1, ('MediaPipePoseDrivenSkeletalActor_0', 'R'): 1}

