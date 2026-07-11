# Tracking-quality baseline summary: p2_replay_clampoff

## Row counts

| family | rows |
| ------ | ---- |
| mp.FootSkateTrace | 0 |
| mp.WristLimitTrace | 479 |
| mp.WebcamAgeTrace | 0 |
| mp.ArmJumpTrace | 0 |
| mp.ArmDirCorrection | 0 |
| mp.ArmOverheadRescue | 0 |
| mp.QuestWristSolve | 446 |
| mp.ChainReachExtend | 0 |
| mp.MediaPipeLegScaffold | 114 |

## mp.WristLimitTrace (anatomical envelope, report-only)

| metric | n | p50 | p90 | p99 | max |
| ------ | - | --- | --- | --- | --- |
| MP_LiveMediaPipeManny L |twistDeg| | 120 | 7.90 | 55.10 | 83.30 | 127.50 |
| MP_LiveMediaPipeManny L swingDeg | 120 | 17.60 | 87.20 | 104.30 | 105.90 |
| MP_LiveMediaPipeManny L twistExcessDeg (out rows: 17) | 17 | 0.00 | 0.00 | 37.50 | 37.50 |
| MP_LiveMediaPipeManny L swingExcessDeg | 17 | 3.20 | 18.50 | 20.90 | 20.90 |
| MP_LiveMediaPipeManny R |twistDeg| | 119 | 7.70 | 50.20 | 103.10 | 131.00 |
| MP_LiveMediaPipeManny R swingDeg | 119 | 21.70 | 84.30 | 109.10 | 118.90 |
| MP_LiveMediaPipeManny R twistExcessDeg (out rows: 12) | 12 | 0.00 | 13.10 | 41.00 | 41.00 |
| MP_LiveMediaPipeManny R swingExcessDeg | 12 | 7.50 | 24.10 | 33.90 | 33.90 |
| MP_LiveMetaHumanKellan L |twistDeg| | 119 | 11.10 | 50.70 | 80.60 | 129.10 |
| MP_LiveMetaHumanKellan L swingDeg | 119 | 16.00 | 87.40 | 106.00 | 107.50 |
| MP_LiveMetaHumanKellan L twistExcessDeg (out rows: 17) | 17 | 0.00 | 0.00 | 39.10 | 39.10 |
| MP_LiveMetaHumanKellan L swingExcessDeg | 17 | 5.20 | 19.80 | 22.50 | 22.50 |
| MP_LiveMetaHumanKellan R |twistDeg| | 121 | 7.70 | 55.80 | 132.90 | 132.90 |
| MP_LiveMetaHumanKellan R swingDeg | 121 | 23.40 | 85.20 | 119.60 | 119.60 |
| MP_LiveMetaHumanKellan R twistExcessDeg (out rows: 15) | 15 | 0.00 | 42.90 | 42.90 | 42.90 |
| MP_LiveMetaHumanKellan R swingExcessDeg | 15 | 2.10 | 34.60 | 34.60 | 34.60 |

## mp.MediaPipeLegScaffold (pre-plan leg fingerprint)

| metric | n | p50 | p90 | p99 | max |
| ------ | - | --- | --- | --- | --- |
| MP_LiveMediaPipeManny L liftCm (grounded 51/57) | 57 | 0.90 | 2.10 | 3.10 | 4.00 |
| MP_LiveMediaPipeManny R liftCm (grounded 51/57) | 57 | 0.80 | 3.60 | 4.20 | 5.60 |
| MP_LiveMetaHumanKellan L liftCm (grounded 51/57) | 57 | 0.90 | 2.10 | 3.10 | 4.00 |
| MP_LiveMetaHumanKellan R liftCm (grounded 51/57) | 57 | 0.80 | 3.60 | 4.20 | 5.60 |

## Cadence sanity (rows/side, starvation check)

- mp.QuestWristSolve: {('MP_LiveMediaPipeManny', 'L'): 111, ('MP_LiveMediaPipeManny', 'R'): 111, ('MP_LiveMetaHumanKellan', 'L'): 111, ('MP_LiveMetaHumanKellan', 'R'): 111, ('MediaPipePoseDrivenSkeletalActor_0', 'L'): 1, ('MediaPipePoseDrivenSkeletalActor_0', 'R'): 1}

