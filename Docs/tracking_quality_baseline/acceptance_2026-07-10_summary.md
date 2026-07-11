# Tracking-quality baseline summary: acceptance_2026-07-10

## Row counts

| family | rows |
| ------ | ---- |
| mp.FootSkateTrace | 0 |
| mp.WristLimitTrace | 0 |
| mp.WebcamAgeTrace | 0 |
| mp.ArmJumpTrace | 15 |
| mp.ArmDirCorrection | 197 |
| mp.ArmOverheadRescue | 416 |
| mp.QuestWristSolve | 3460 |
| mp.ChainReachExtend | 197 |
| mp.MediaPipeLegScaffold | 104 |

## mp.ArmJumpTrace (pre-plan event fingerprint)

| metric | n | p50 | p90 | p99 | max |
| ------ | - | --- | --- | --- | --- |
| MP_LiveMetaHumanKellan L residCm | 7 | 1.40 | 1.40 | 2.50 | 2.50 |
| MP_LiveMetaHumanKellan R residCm | 8 | 1.60 | 1.80 | 12.60 | 12.60 |

## mp.ArmDirCorrection (pre-plan drift fingerprint)

| metric | n | p50 | p90 | p99 | max |
| ------ | - | --- | --- | --- | --- |
| MP_LiveMetaHumanKellan L elbowCorrDeg | 98 | 7.40 | 15.30 | 20.00 | 20.00 |
| MP_LiveMetaHumanKellan L wristCorrDeg | 98 | 9.70 | 13.70 | 16.80 | 17.60 |
| MP_LiveMetaHumanKellan R elbowCorrDeg | 99 | 9.80 | 18.10 | 20.00 | 20.00 |
| MP_LiveMetaHumanKellan R wristCorrDeg | 99 | 7.40 | 14.30 | 16.80 | 17.00 |

## mp.MediaPipeLegScaffold (pre-plan leg fingerprint)

| metric | n | p50 | p90 | p99 | max |
| ------ | - | --- | --- | --- | --- |
| MP_LiveMediaPipeManny L liftCm (grounded 45/52) | 52 | 1.20 | 3.30 | 7.70 | 29.70 |
| MP_LiveMediaPipeManny R liftCm (grounded 43/52) | 52 | 1.30 | 3.90 | 14.40 | 41.40 |
| MP_LiveMetaHumanKellan L liftCm (grounded 45/52) | 52 | 1.20 | 3.30 | 7.70 | 29.70 |
| MP_LiveMetaHumanKellan R liftCm (grounded 43/52) | 52 | 1.30 | 3.90 | 14.30 | 41.40 |

## Cadence sanity (rows/side, starvation check)

- mp.ArmOverheadRescue: {('MP_LiveMediaPipeManny', 'L'): 104, ('MP_LiveMediaPipeManny', 'R'): 104, ('MP_LiveMetaHumanKellan', 'L'): 104, ('MP_LiveMetaHumanKellan', 'R'): 104}
- mp.QuestWristSolve: {('MP_LiveMediaPipeManny', 'L'): 865, ('MP_LiveMediaPipeManny', 'R'): 865, ('MP_LiveMetaHumanKellan', 'L'): 865, ('MP_LiveMetaHumanKellan', 'R'): 865}
- mp.ChainReachExtend: {('MP_LiveMetaHumanKellan', 'L'): 98, ('MP_LiveMetaHumanKellan', 'R'): 99}

