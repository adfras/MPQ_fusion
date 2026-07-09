# RESOLUTION — the Quest hand stream was never dead (2026-07-09)

Supersedes `HANDOFF_2026-07-06_QUEST_HANDS_DEAD.md`. Every claim below is
reproducible from the logs still in `Saved/Logs/` (all grep commands are plain
`grep`; note ripgrep silently fails on these log files — use GNU grep or Python).

## What the logs actually show

The handoff's evidence was `mp.QuestWristSolve ... questTracked=0` "both hands,
every frame" plus the QUEST ARM CALIBRATION HUD stuck at `frames=0`. Splitting the
same trace rows **by actor** tells the real story:

| Session (UTC, 2026-07-06)       | Manny questTracked=1 rows | Kellan questTracked=1 rows | Kellan chainActive rows (questHandUsed) |
|---------------------------------|--------------------------:|---------------------------:|----------------------------------------:|
| 09:36–09:44                     | 2,332                     | 4                          | 2,428 (2,385)                            |
| 11:48–12:14 ("worked at 11:53") | 2,603                     | **0**                      | 5,430 (5,254)                            |
| 12:50–13:13 ("dead")            | 1,682                     | 0                          | 18,838 (18,763)                          |
| 13:29–13:46 ("dead")            | 1,300                     | 1                          | 6,586 (5,832)                            |
| 13:52–14:15 (final, "dead")     | 361                       | 0                          | 1,498 (1,287)                            |

- **The hand stream was alive in every session, including the final one.** The
  Manny actor's wrist solve consumed live tracked Quest hands at 13:57–13:58 UTC
  (`grep "actor=MP_LiveMediaPipeManny" TestingKit5.log | grep questTracked=1`),
  and the XR_FB_body_tracking full arm chain was active with Quest hands used in
  ~90–95% of rows all day. No replay was running (no replay markers in the log;
  `mp.StartLiveLowerBodyTrial` live trial armed, VR Preview at 13:57:13).
- **Kellan's `questTracked=0` is by design, not a dead stream.** The mirror
  Kellan's arms are owned by the MetaHuman full-arm-chain direct path (and body
  fusion when active). That disables the quest-wrist position solve
  (`bQuestArmFallbackAllowed` in `MediaPipePoseDrivenAnimInstance_QuestArmSolve.cpp`),
  so `TryApplyQuestWristPositionWorld` never runs and the trace struct printed its
  **default zeros**: `questTracked=0 runtimeKey=0 hmdPose=0 calib=NONE`. The rare
  Kellan `questTracked=1` rows (e.g. 09:40:07, 13:36:44) are chain-dropout moments
  when the fallback path woke up — and worked.
- **HUD `frames=0` is structural.** The arm-length calibration only advances inside
  the constrained quest-wrist solve, which is disabled whenever the chain is fresh.
  With a healthy chain the HUD *cannot* count frames. `frames=0` meant "chain
  healthy", the exact opposite of what it was read as.
- **Kellan's wrist rotation was applying** in the "dead" sessions
  (`handRotApplied=1` on 97% of rows 12:50–13:13, 76% in the final session,
  `calibrationState=Tracking`).
- Nothing regressed between the "working" 11:48 session and the afternoon: same
  boot config (CANDIDATE variant + heavy model + auto-armed trial from 10:49
  onward), same CVar writes, same chain reach stats (avg 40.8→45.9cm, max ~52cm —
  the "dead" sessions were marginally *better*).

The `c48a308` checkout experiment was not run: it was designed to discriminate
"today's code killed ingestion" vs "external cause", but ingestion was provably
alive on the current binary in the final session, so both branches of the
experiment would return the same answer.

## What was changed (so this can't happen again)

1. `mp.QuestWristSolve` rows now carry `armOwner=` (bodyFusion / chainDirect /
   cameraRescue / questWrist / mediaPipe) right after `questArmMode=`, and
   `questTracked=` / `runtimeKey=` are pre-filled with the true live values even
   when the wrist-position mapping is skipped. A Kellan row under the gold
   standard now reads `armOwner=chainDirect questTracked=1` when hands are
   tracked.
2. The QUEST ARM CALIBRATION HUD slot shows, while the chain owns the arms:
   `QUEST ARM SOURCE: BODY CHAIN / hands tracked L=1 R=1 / arm-length calibration
   idle while the chain owns the arms` (green when both hands tracked). The old
   "Raise both hands into view … frames=0" only appears when the calibration can
   actually run.

## Open items carried forward

- Arms-not-fully-extended and wrist appearance on the mirror Kellan are
  chain-path tuning questions (chain delivers ~52cm max reach), not a hand-stream
  outage. Judge on the mirror after the diagnostics fix.
- `mp.QuestWristPalmTrimLeft/RightDeg` (36.8 / −11.1) are global CVars consumed by
  the shared `ApplyQuestWristPalmTrim` in the hand-rotation solve — there is no
  per-consumer gain today; the first-person embodied hands and the mirror body get
  the same trim.
- Shrug on the live mirror (~1.4cm vs 10.1cm replay) — unchanged, parked, see
  handoff doc.
