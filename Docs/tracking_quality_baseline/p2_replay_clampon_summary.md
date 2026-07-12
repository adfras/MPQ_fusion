# Tracking-quality baseline summary: p2_replay_clampon

## Row counts

| family | rows |
| ------ | ---- |
| mp.FootSkateTrace | 0 |
| mp.WristLimitTrace | 514 |
| mp.WebcamAgeTrace | 0 |
| mp.ArmJumpTrace | 0 |
| mp.ArmDirCorrection | 0 |
| mp.ArmOverheadRescue | 0 |
| mp.QuestWristSolve | 482 |
| mp.ChainReachExtend | 0 |
| mp.MediaPipeLegScaffold | 122 |

## mp.WristLimitTrace (anatomical envelope, report-only)

| metric | n | p50 | p90 | p99 | max |
| ------ | - | --- | --- | --- | --- |
| MP_LiveMediaPipeManny L |twistDeg| | 130 | 7.60 | 51.80 | 83.30 | 127.50 |
| MP_LiveMediaPipeManny L swingDeg | 130 | 15.90 | 87.20 | 104.30 | 106.50 |
| MP_LiveMediaPipeManny L twistExcessDeg (out rows: 20) | 20 | 0.00 | 0.00 | 37.50 | 37.50 |
| MP_LiveMediaPipeManny L swingExcessDeg | 20 | 3.20 | 18.20 | 21.50 | 21.50 |
| MP_LiveMediaPipeManny R |twistDeg| | 126 | 7.00 | 43.30 | 103.10 | 131.00 |
| MP_LiveMediaPipeManny R swingDeg | 126 | 19.00 | 66.30 | 109.10 | 119.40 |
| MP_LiveMediaPipeManny R twistExcessDeg (out rows: 12) | 12 | 0.00 | 13.10 | 41.00 | 41.00 |
| MP_LiveMediaPipeManny R swingExcessDeg | 12 | 8.90 | 24.10 | 34.40 | 34.40 |
| MP_LiveMetaHumanKellan L |twistDeg| | 129 | 10.00 | 45.40 | 80.60 | 129.10 |
| MP_LiveMetaHumanKellan L swingDeg | 129 | 14.60 | 87.40 | 106.00 | 107.50 |
| MP_LiveMetaHumanKellan L twistExcessDeg (out rows: 18) | 18 | 0.00 | 0.00 | 39.10 | 39.10 |
| MP_LiveMetaHumanKellan L swingExcessDeg | 18 | 5.20 | 19.80 | 22.50 | 22.50 |
| MP_LiveMetaHumanKellan R |twistDeg| | 129 | 8.30 | 46.30 | 132.90 | 132.90 |
| MP_LiveMetaHumanKellan R swingDeg | 129 | 21.90 | 85.10 | 119.60 | 120.10 |
| MP_LiveMetaHumanKellan R twistExcessDeg (out rows: 14) | 14 | 0.00 | 42.90 | 42.90 | 42.90 |
| MP_LiveMetaHumanKellan R swingExcessDeg | 14 | 6.20 | 34.60 | 35.10 | 35.10 |

## mp.MediaPipeLegScaffold (pre-plan leg fingerprint)

| metric | n | p50 | p90 | p99 | max |
| ------ | - | --- | --- | --- | --- |
| MP_LiveMediaPipeManny L liftCm (grounded 58/61) | 61 | 1.00 | 2.10 | 3.20 | 4.10 |
| MP_LiveMediaPipeManny R liftCm (grounded 57/61) | 61 | 1.10 | 3.40 | 4.50 | 4.70 |
| MP_LiveMetaHumanKellan L liftCm (grounded 58/61) | 61 | 1.00 | 2.10 | 3.20 | 4.10 |
| MP_LiveMetaHumanKellan R liftCm (grounded 56/61) | 61 | 1.10 | 3.40 | 4.50 | 4.70 |

## Cadence sanity (rows/side, starvation check)

- mp.QuestWristSolve: {('MP_LiveMediaPipeManny', 'L'): 120, ('MP_LiveMediaPipeManny', 'R'): 120, ('MP_LiveMetaHumanKellan', 'L'): 120, ('MP_LiveMetaHumanKellan', 'R'): 120, ('MediaPipePoseDrivenSkeletalActor_0', 'L'): 1, ('MediaPipePoseDrivenSkeletalActor_0', 'R'): 1}

