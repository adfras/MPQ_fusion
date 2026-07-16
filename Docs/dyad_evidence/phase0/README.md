# Dyad Phase 0 evidence — ghost partner (DYADIC_STUDY_PLAN)

2026-07-16. Gate evidence for Phase 0: a second avatar in the running world, driven from a
looping segment of the canonical replay cache through the per-mesh row-stream binding,
while every other mesh stays exactly on its pre-dyad drive. Summaries are mined with
`Tools/mine_tracking_quality_baseline.py` from 150–170 s headless `-game` captures on the
replay map (deterministic input) and the live preview room (webcam running, nobody in
frame — the desk was empty, so the live pawn idles; motion-under-load is a later gate).

## Gate → evidence

- **Flag off = byte-identical.** The six refactor goldens plus every characterization
  test pass unchanged inside the 214/214 suite (208 at phase start; the 6 new tests are
  `TestingKit5.MediaPipe.Dyad.*`). `postphase_defaultsoff_summary.md` vs
  `prephase_run1/2_summary.md`: all four row families scale by a uniform 0.956× against
  pre-phase run 1 (1936/2022 FootSkate, 563/587 WristLimit, 514/538 QuestWristSolve,
  130/136 LegScaffold) — a slightly shorter effective capture window, not a behavior
  change; per-actor distributions match within the pre-phase run-to-run spread.
- **Live mirror pawn unaffected (cross-talk).** Deterministic: in
  `postphase_ghost_replay_summary.md` the primary `MP_LiveMetaHumanKellan` distributions
  reproduce the defaults-off run (L |twist| p50 11.8 vs 12.1, max identical at 90.0;
  R p99 61.8 vs 62.0, max identical at 62.5). Live room: the live pawn's row profile is
  identical with ghost off/on (6184 vs 6098 passive `mp.QuestWristSnapshot` rows — frame
  count variance — same family mix, zero motion-tracer rows both ways since nobody was
  in frame).
- **Ghost animates from the cache on a different avatar.** Both ghost-on runs:
  `mp.MetaHumanProfile: resolved profile=Maria actor=MP_DyadGhostMaria active=1 valid=1`
  (26 resolutions) beside the primary's `profile=Kellan active=1` — the per-mesh
  active-profile override working end-to-end. FootSkate/WristLimit/QuestWristSolve/
  LegScaffold rows all present for `MP_DyadGhostMaria` + `MP_DyadGhostDriver` in the
  live room where the ghost was the only mover (1524/680/1536/102 rows), with sane
  ground-relative foot stats (rendered-bone-space evidence the mesh is actually driven).
- **N pawns from row streams without cross-talk (unit).** 6 new tests: segment/loop
  pacing math, restamp-across-seam monotonicity, two interleaved readers with distinct
  keys, key-0 rejection + gap parking (a bound mesh with a starved stream parks instead
  of falling through to live sensors — the seat-B guarantee), profile-override isolation,
  and the playback-state-free `GetObservationsAtDatasetTime` accessor.

## Known fidelity note (not a Phase 0 gate; carry to Phase 3/6)

The ghost's wrist twist runs hot vs the global-replay-driven primary (|twist| p50 ~40–68°
vs ~12–15°; ~50–75 % of WristLimit rows out of envelope, both rooms). Wrist ROTATION
calibration itself latches (`calibrationState=Accepted→Tracking`, `wristRotCalibHad=1`).
This is the documented 2026-07-05 class: the live-mode hand-rotation stack is
camera-hand-starved — the canonical 2026-06-09 dataset predates `camera_hands` recording
(schema v3, 2026-07-04), and a row-stream-bound mesh runs LIVE-mode gates (global-replay
suppressions don't apply to it, by design). The primary avoids it only because global
replay suppresses those paths. Upgrade paths (Alan's call, not taken here): parse
`camera_hands` on the replay-in path (additive; the field already exists in
`FEmbodiedFusionSourceObservations` but is never read) and feed it through the injection
block, and/or use a camera-hands-carrying recording for the partner (contradicts the
2026-07-16 canonical-loop decision as-is). Phase 3's loop-segment choice should prefer
low-hand-articulation stretches to minimize exposure.

## Also fixed in this phase (pre-existing, found by the capture workflow)

- `Content/Python/init_unreal.py` crashed EVERY interactive `-game` boot with a native
  EXCEPTION_ACCESS_VIOLATION: the 2026-07-12 mirror-avatar block calls
  `unreal.EditorLevelLibrary.get_all_level_actors()`, whose backing subsystem doesn't
  exist in `-game` (python try/except cannot catch the native AV). Now guarded by a
  `-game` command-line check; the read was editor-cosmetic (the placed pawn stomps the
  CVar at play start anyway).
- The replay map's placed replay actor had been SAVED pointing at the take-3 MHA dataset
  (`...mha_groundtruth_20260705_095744...`). Restored to the canonical manifest via
  `Tools/SetupRecordedQuestMediaPipeReplayMap.py`, which now pins the schema-v2 manifest
  (`..._replay_source_v2_manifest.json`) matching the documented replay state (v2 hand
  skeletons drive wrists/fingers).

## Reproduction

```
# defaults-off / ghost-on replay-map capture (watchdog-kill after ~170 s):
UnrealEditor-Cmd.exe <uproject> /Game/MetaHumanRooms/L_MetaHumanRecordedQuestMediaPipeReplay_01 \
  -game -windowed -ResX=640 -ResY=360 -nosplash -nopause -unattended \
  -ExecCmds="mp.FootSkateTrace 1, mp.WristLimitTrace 1[, mp.DyadGhostAvatar Maria, mp.DyadGhostPartner 1]" -abslog=<log>
# live-room A/B: same on /Game/MetaHumanRooms/L_MetaHumanPreviewRoom_MPQSignalCompare_01,
#   WITHOUT -unattended (AutoTrial arms the live trial stack + tracers).
python Tools/mine_tracking_quality_baseline.py <log> <out_dir> --label <label>
```
