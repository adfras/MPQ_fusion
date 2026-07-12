# Tracking-quality baseline summary: replay_smoke_2026-07-11

## Row counts

| family | rows |
| ------ | ---- |
| mp.FootSkateTrace | 1049 |
| mp.WristLimitTrace | 312 |
| mp.WebcamAgeTrace | 0 |
| mp.ArmJumpTrace | 0 |
| mp.ArmDirCorrection | 0 |
| mp.ArmOverheadRescue | 0 |
| mp.QuestWristSolve | 290 |
| mp.ChainReachExtend | 0 |
| mp.MediaPipeLegScaffold | 74 |

## mp.FootSkateTrace (foot-skate scoreboard)

| metric | n | p50 | p90 | p99 | max |
| ------ | - | --- | --- | --- | --- |
| MP_LiveMediaPipeManny L planted planarSpdCmS (n_planted/263) | 245 | 4.60 | 20.00 | 30.10 | 31.70 |
| MP_LiveMediaPipeManny L penetrCm | 263 | 0.00 | 0.27 | 1.69 | 5.45 |
| MP_LiveMediaPipeManny L liftCm | 263 | 1.10 | 2.50 | 3.40 | 4.10 |
| MP_LiveMediaPipeManny R planted planarSpdCmS (n_planted/262) | 240 | 4.10 | 19.10 | 29.40 | 32.90 |
| MP_LiveMediaPipeManny R penetrCm | 262 | 0.00 | 0.17 | 0.95 | 1.46 |
| MP_LiveMediaPipeManny R liftCm | 262 | 1.20 | 3.30 | 4.40 | 5.00 |
| MP_LiveMetaHumanKellan L planted planarSpdCmS (n_planted/262) | 246 | 4.40 | 18.20 | 30.30 | 32.80 |
| MP_LiveMetaHumanKellan L penetrCm | 262 | 0.00 | 0.23 | 1.59 | 5.28 |
| MP_LiveMetaHumanKellan L liftCm | 262 | 1.10 | 2.50 | 3.50 | 4.00 |
| MP_LiveMetaHumanKellan R planted planarSpdCmS (n_planted/262) | 241 | 4.00 | 18.50 | 28.10 | 33.00 |
| MP_LiveMetaHumanKellan R penetrCm | 262 | 0.00 | 0.15 | 0.90 | 1.38 |
| MP_LiveMetaHumanKellan R liftCm | 262 | 1.20 | 3.30 | 4.40 | 5.00 |

## mp.WristLimitTrace (anatomical envelope, report-only)

| metric | n | p50 | p90 | p99 | max |
| ------ | - | --- | --- | --- | --- |
| MP_LiveMediaPipeManny L |twistDeg| | 76 | 14.30 | 58.80 | 83.30 | 127.90 |
| MP_LiveMediaPipeManny L swingDeg | 76 | 39.60 | 88.20 | 104.30 | 106.50 |
| MP_LiveMediaPipeManny L twistExcessDeg (out rows: 13) | 13 | 0.00 | 0.00 | 37.90 | 37.90 |
| MP_LiveMediaPipeManny L swingExcessDeg | 13 | 4.30 | 19.30 | 21.50 | 21.50 |
| MP_LiveMediaPipeManny R |twistDeg| | 80 | 15.10 | 56.80 | 103.10 | 131.00 |
| MP_LiveMediaPipeManny R swingDeg | 80 | 29.80 | 86.60 | 106.30 | 119.40 |
| MP_LiveMediaPipeManny R twistExcessDeg (out rows: 13) | 13 | 0.00 | 13.10 | 41.00 | 41.00 |
| MP_LiveMediaPipeManny R swingExcessDeg | 13 | 2.70 | 21.30 | 34.40 | 34.40 |
| MP_LiveMetaHumanKellan L |twistDeg| | 76 | 17.20 | 60.10 | 80.60 | 128.80 |
| MP_LiveMetaHumanKellan L swingDeg | 76 | 36.80 | 88.30 | 106.00 | 107.50 |
| MP_LiveMetaHumanKellan L twistExcessDeg (out rows: 12) | 12 | 0.00 | 0.00 | 38.80 | 38.80 |
| MP_LiveMetaHumanKellan L swingExcessDeg | 12 | 8.10 | 21.00 | 22.50 | 22.50 |
| MP_LiveMetaHumanKellan R |twistDeg| | 80 | 13.80 | 60.50 | 104.00 | 136.10 |
| MP_LiveMetaHumanKellan R swingDeg | 80 | 32.60 | 86.70 | 107.40 | 118.80 |
| MP_LiveMetaHumanKellan R twistExcessDeg (out rows: 12) | 12 | 0.00 | 14.00 | 46.10 | 46.10 |
| MP_LiveMetaHumanKellan R swingExcessDeg | 12 | 6.20 | 22.40 | 33.80 | 33.80 |

## mp.MediaPipeLegScaffold (pre-plan leg fingerprint)

| metric | n | p50 | p90 | p99 | max |
| ------ | - | --- | --- | --- | --- |
| MP_LiveMediaPipeManny L liftCm (grounded 31/37) | 37 | 1.20 | 2.60 | 3.70 | 3.70 |
| MP_LiveMediaPipeManny R liftCm (grounded 30/37) | 37 | 1.30 | 3.30 | 5.20 | 5.20 |
| MP_LiveMetaHumanKellan L liftCm (grounded 32/37) | 37 | 1.20 | 2.60 | 3.70 | 3.70 |
| MP_LiveMetaHumanKellan R liftCm (grounded 30/37) | 37 | 1.30 | 3.30 | 5.20 | 5.20 |

## Cadence sanity (rows/side, starvation check)

- mp.QuestWristSolve: {('MP_LiveMediaPipeManny', 'L'): 72, ('MP_LiveMediaPipeManny', 'R'): 72, ('MP_LiveMetaHumanKellan', 'L'): 72, ('MP_LiveMetaHumanKellan', 'R'): 72, ('MediaPipePoseDrivenSkeletalActor_0', 'L'): 1, ('MediaPipePoseDrivenSkeletalActor_0', 'R'): 1}

