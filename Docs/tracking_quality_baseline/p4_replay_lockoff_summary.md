# Tracking-quality baseline summary: p4_replay_lockoff

## Row counts

| family | rows |
| ------ | ---- |
| mp.FootSkateTrace | 1808 |
| mp.WristLimitTrace | 0 |
| mp.WebcamAgeTrace | 0 |
| mp.ArmJumpTrace | 0 |
| mp.ArmDirCorrection | 0 |
| mp.ArmOverheadRescue | 0 |
| mp.QuestWristSolve | 478 |
| mp.ChainReachExtend | 0 |
| mp.MediaPipeLegScaffold | 120 |

## mp.FootSkateTrace (foot-skate scoreboard)

| metric | n | p50 | p90 | p99 | max |
| ------ | - | --- | --- | --- | --- |
| MP_LiveMediaPipeManny L planted planarSpdCmS (n_planted/452) | 439 | 5.20 | 17.60 | 30.80 | 32.90 |
| MP_LiveMediaPipeManny L penetrCm | 452 | 0.11 | 0.26 | 2.35 | 5.91 |
| MP_LiveMediaPipeManny L liftCm | 452 | 1.00 | 2.40 | 3.50 | 4.00 |
| MP_LiveMediaPipeManny R planted planarSpdCmS (n_planted/452) | 421 | 4.60 | 18.40 | 28.60 | 32.20 |
| MP_LiveMediaPipeManny R penetrCm | 452 | 0.00 | 0.39 | 1.46 | 2.04 |
| MP_LiveMediaPipeManny R liftCm | 452 | 1.00 | 3.40 | 5.00 | 7.00 |
| MP_LiveMetaHumanKellan L planted planarSpdCmS (n_planted/452) | 438 | 5.20 | 16.90 | 31.10 | 32.60 |
| MP_LiveMetaHumanKellan L penetrCm | 452 | 0.09 | 0.24 | 2.23 | 5.61 |
| MP_LiveMetaHumanKellan L liftCm | 452 | 1.00 | 2.40 | 3.50 | 4.00 |
| MP_LiveMetaHumanKellan R planted planarSpdCmS (n_planted/452) | 421 | 4.70 | 17.90 | 29.90 | 33.00 |
| MP_LiveMetaHumanKellan R penetrCm | 452 | 0.00 | 0.36 | 1.39 | 1.94 |
| MP_LiveMetaHumanKellan R liftCm | 452 | 0.90 | 3.40 | 5.00 | 7.00 |

## mp.MediaPipeLegScaffold (pre-plan leg fingerprint)

| metric | n | p50 | p90 | p99 | max |
| ------ | - | --- | --- | --- | --- |
| MP_LiveMediaPipeManny L liftCm (grounded 58/60) | 60 | 1.00 | 2.20 | 3.10 | 3.40 |
| MP_LiveMediaPipeManny R liftCm (grounded 53/60) | 60 | 1.10 | 3.10 | 4.20 | 4.90 |
| MP_LiveMetaHumanKellan L liftCm (grounded 58/60) | 60 | 1.00 | 2.30 | 3.00 | 3.70 |
| MP_LiveMetaHumanKellan R liftCm (grounded 56/60) | 60 | 1.10 | 3.00 | 4.10 | 5.00 |

## Cadence sanity (rows/side, starvation check)

- mp.QuestWristSolve: {('MP_LiveMediaPipeManny', 'L'): 119, ('MP_LiveMediaPipeManny', 'R'): 119, ('MP_LiveMetaHumanKellan', 'L'): 119, ('MP_LiveMetaHumanKellan', 'R'): 119, ('MediaPipePoseDrivenSkeletalActor_0', 'L'): 1, ('MediaPipePoseDrivenSkeletalActor_0', 'R'): 1}

