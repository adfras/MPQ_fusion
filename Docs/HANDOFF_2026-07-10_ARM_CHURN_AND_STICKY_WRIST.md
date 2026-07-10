# HANDOFF — arm ownership churn + sticky-wrong wrist; baseline A/B verdict (2026-07-10)

You are taking over live-quality work on the TestingKit5 VR embodiment stack. Read
`Docs/RESOLUTION_2026-07-09_SHRUG_AND_WRIST_SNAP.md` first (what shipped yesterday and
why), and `Docs/RESOLUTION_2026-07-09_QUEST_HANDS_ALIVE.md` for the arm-ownership model
and the `armOwner=`/`questTracked=` diagnostics. Trust the log evidence below over any
narrative — including this one.

## USER VERDICT (2026-07-10 worn session, ad21f93)

"Not much has changed. Only occasionally would the hand that was snapped sideways
correct itself. Considerable drift in the arms — movement far from smooth. The default
VR-preview setting (mediapipe LITE but with the correct fusion, minus the shoulder
implementation) had much better arms and hands."

That last sentence is a controlled A/B the user already ran: BASELINE variant beats
CANDIDATE on arms+hands. The candidate-vs-baseline diff (logged at every boot,
`SettingsVariant: CANDIDATE active; diff vs baseline:`) is the complete lever set:
`mp.MediaPipeArmRescueShoulderRelDivergence=1, mp.MediaPipeClavicleShrugWeight=1,
mp.MediaPipeClavicleShrugMinCm=1, mp.MediaPipeClavicleShrugDirect=1,
mp.MediaPipeLegAdductionMaxDeg=0, mp.MediaPipeKneeMedialBowMaxDeg=0,
mp.AutoQuestWebcamPoseModel=heavy, mp.MediaPipePelvisHmdAnchor=1,
mp.MediaPipeBodyYawFromCamera=1, mp.QuestWristPalmTrimLeftDeg=36.8,
mp.QuestWristPalmTrimRightDeg=-11.1`.

## WHAT THE 2026-07-10 LOG SHOWS (worn window 00:25:02–00:32+ UTC)

Log: `Saved/Logs/TestingKit5.log` at handoff time (rotates to
`TestingKit5-backup-2026.07.10-*.log` on next boot). GNU grep or Python — ripgrep
silently returns 0 matches on these logs.

1. **Arm ownership churn is measured, not inferred.** The new
   `mp.MediaPipeCameraHandTrace` rows (armed at boot now) show 55 cameraLatched /
   51 handbackToQuest transitions in ~7 minutes — Kellan L 24/23, R 12/11. Latch
   entries split: L 11 / R 7 fired with `rescue=1 questTracked=1` (the RESCUE seized a
   TRACKED arm and dragged hand ownership with it), L 13 / R 5 with `questTracked=0`
   (real dropouts, past the new 0.35s grace — legitimate).
2. **The shoulder-relative divergence trigger is the seizure engine.**
   `mp.ArmOverheadRescue` rows on Kellan: rescue ACTIVE on ~4% (L) / ~2% (R) of frames,
   in bursts every ~15–20s; `cond=1` fired with `questTracked=1 chainFresh=1` on
   L 60 / R 41 sampled rows — the camera taking arms from a HEALTHY chain. Divergence
   while active: median 15.8cm, max 46cm, threshold 30cm, **no hysteresis** (enter and
   exit compare the same 30cm; dwells are only 0.15s in / 0.3s out), so it flaps around
   the threshold during raises. `mp.MediaPipeArmRescueShoulderRelDivergence` is
   CANDIDATE-ONLY — baseline never mid-raise-seizes. This plus the heavy model's extra
   camera latency (bigger transient divergence, laggier camera arm while it owns) is the
   best-supported explanation for "considerable drift / not smooth" and for baseline
   feeling better.
3. **Sticky-wrong wrist = yesterday's continuity bias (own this).** The palm-roll
   source hysteresis DID kill per-frame basis flapping (22 fallback rows in dwell-gated
   bursts, `wristPalmHeld` now engages — 6 rows — vs 38 flaps/0 holds before). BUT the
   continuity bias re-anchors on every source resume and only decays (0.6s half-life)
   while the projected primary measures; during raises the primary drops out too often
   for the decay to run, so a wrong roll persists instead of snapping back — exactly
   "only occasionally corrects itself". The old code was ugly-but-self-correcting;
   the new code is smooth-but-sticky. The bias needs a bounded lifetime (e.g. decay
   regardless of source at a slower rate, and/or a hard cap ~1–2s after which it
   rate-limited-converges to the active measurement), not an unconditional hold.
4. **Shrug drive now delivers rig-side lift** — `mp.ClavicleShrugFusion ... appliedCm=`
   reached 7.2cm (23 rows ≥5cm), rest reference stable at ~47.5–48.3 (no climb; the
   quiet gate works). The user did not judge shoulders this session; the arms chaos
   dominates. Do not undo the shrug work when reverting arm CVars — `ShrugDirect/
   ShrugWeight/ShrugMinCm` are independent of the arm levers.
5. **Diagnostics debt discovered:** the `mp.ArmOverheadRescue` and `mp.QuestWristSolve`
   throttles live in `DiagnosticsState` (anim-node members, wiped by CacheBones every
   frame in live VR) — the rescue row emitted at frame rate (3,522 rows/side!), and the
   wrist-solve row ALSO has a function-static global pair shared across actors that
   starved Kellan L to 4 rows all session. Same defect class fixed for the shrug rows
   yesterday (moved into BodyState). Fix opportunistically — evidence starvation has
   burned three sessions running.

## GOAL (user's priority)

Make the live mirror's arms/hands match or beat the BASELINE feel while keeping the
candidate's legs (heavy model fixed the right knee: corr 0.03→0.74) and the shrug:

1. **Stop the mid-raise camera seizures.** Quest chain must own tracked arms. Options,
   in likely order of payoff: turn `mp.MediaPipeArmRescueShoulderRelDivergence` off
   live (back to baseline rescue = overhead/fully-gone only) OR add real hysteresis +
   a hold-tracked veto (never divergence-seize while `questTracked=1 && chainFresh=1`).
   Remember the USER RULE (2026-07-02): never hold an arm against the camera when the
   quest side is FULLY gone — dropouts must still rescue.
2. **Hand rotation must never follow arm rescue while quest is tracked.** The camera
   -hand latch entry `bArmRescueActive || !recentlyTracked` still hands the WRIST to
   the camera whenever the rescue owns the ARM, even with `questTracked=1` (measured:
   L 11 / R 7 such entries). Decouple: latch the hand on tracking loss only.
3. **Fix the sticky continuity bias** (see #3 above) so a wrong roll self-corrects
   within ~1s while still never stepping frame-to-frame.
4. Consider the palm trims (36.8 / −11.1, candidate-only) as a suspect for the
   arms-forward "broken-looking left wrist" — the baseline A/B says baseline trims
   looked better. Do not blind-tune numbers; A/B the baseline value.

If a smaller lever set can be A/B'd in ONE worn test, prefer flipping
`mp.MediaPipeArmRescueShoulderRelDivergence 0` + the hand-latch decouple + bias fix in
a single build: each is independently defensible from this log.

## CODE MAP (line numbers drift — search the markers)

- `Source/MediaPipeDriver/PoseDriven/MediaPipePoseDrivenAnimInstance_QuestArmSolve.cpp`
  — rescue triggers (`bRescueShoulderRelDivergence`, `bRescueDivergenceTriggered`,
  `bQuestArmFullyGone`, 0.15s/0.3s dwells), tracked-recency grace
  (`bQuestSideRecentlyTrackedForArm`, added 2026-07-09), camera-hand ownership latch
  (`bCameraHandOwnershipLatched`, entry clause to decouple), `mp.ArmOverheadRescue`
  diagnostic emit.
- `Source/MediaPipeDriver/PoseDriven/MediaPipePoseDrivenAnimInstance_QuestHandRotation.cpp`
  — palm-roll source hysteresis + `PalmRollContinuityBiasDeg` (the sticky bias; fields
  in `Quest/MediaPipeQuestWristCalibrationState.h`, keyed store).
- `Runtime/MediaPipeDriverRuntime.cpp` — candidate/baseline variant CVar lists (search
  `SettingsVariant`), `ApplyAutoQuestProfile` (sets `mp.QuestPalmMode 2` when the quest
  link engages — PalmMode is 0 at bare boot, that is normal).
- RULE (hard-won): cross-frame solver state must live in the keyed runtime store
  (`GetQuestWristRuntimeState(RuntimeStateKey)`), never in node members — CacheBones
  wipes node members every frame; skip keyed writes when `RuntimeStateKey==0`.

## ENVIRONMENT CHEAT SHEET

- Project `D:/Epic/Unreal_Projects/TestingKit5`, UE 5.8 at `D:/Epic/UE_5.8`. Local time
  = UTC+8; log timestamps are UTC. HEAD = ad21f93 on main (github.com/adfras/MPQ_fusion).
- Build (editor MUST be closed; `Get-Process UnrealEditor`):
  `D:/Epic/UE_5.8/Engine/Build/BatchFiles/Build.bat TestingKit5Editor Win64 Development
  -Project=D:/Epic/Unreal_Projects/TestingKit5/TestingKit5.uproject -WaitMutex -NoUBA
  -UBANoDetour -MaxParallelActions=4` (~15 min; tail can look stalled in the final link —
  check cl.exe liveness, not the tail).
- Tests (157 expected; runner occasionally drops the last-queued test — rerun solo
  before believing red):
  `UnrealEditor-Cmd.exe <uproject> -ExecCmds="Automation RunTests MediaPipe; Quit"
  -TestExit="Automation Test Queue Empty" -unattended -nop4 -nosplash -nullrhi`;
  count `Test Completed. Result={Success}` in `Saved/Logs/TestingKit5.log`.
- Automation runs keep RAW defaults (`ShrugDirect=0`, `PalmMode=0`) — the live-path
  changes stay dormant in tests; replay byte-stability is the invariant to protect.
- Editor MCP: `POST http://127.0.0.1:8000/mcp`, JSON-RPC; `initialize` → capture
  `Mcp-Session-Id` response HEADER → send on every call; project tools behind
  `tools/call name=call_tool arguments={"toolset_name":
  "testingkit_toolset.TestingKitToolset","tool_name":"exec_console"|"get_cvar"|
  "set_cvar"|"set_cvars"|"tail_log","arguments":{...}}`. Console output lands in the
  LOG, not the MCP response. Game-thread calls only — cheap calls during live VR.
- Boot rig `Content/Python/init_unreal.py` arms the gold standard on interactive boots
  (Kellan profile, CANDIDATE variant, `mp.StartLiveLowerBodyTrial`, heavy model,
  `mp.MediaPipeCameraHandTrace 1`). Verify with `mp.DumpLiveProfileSettings`
  (`variant=candidate ... trial=55`). Launch with the map on the command line:
  `UnrealEditor.exe <uproject> /Game/MetaHumanRooms/L_MetaHumanPreviewRoom_MPQSignalCompare_01`.
- Editor-wedge detection = log LastWriteTime frozen, NOT MCP acks.

## USER STANDARDS (non-negotiable)

- The ONLY acceptance judge is the mirror avatar `MP_LiveMetaHumanKellan` in the
  preview room. Never cite the Manny actor or internal metrics as success.
- Fixes, not diagnostics. Instrument silently; report results and changes.
- Verify everything possible from logs BEFORE any worn test. ONE worn test per
  milestone, under 30 seconds.
- Human-scale amplitudes: shrug 10cm+ (under ~5cm = failure), wrists straight, arms
  full reach, and — this session's lesson — SMOOTHNESS outranks reach corrections.
- Clean up any actors you spawn. 157/157 green + commit&push per milestone. Own
  mistakes in the first sentence.

## DEFINITION OF DONE

1. A 30s worn session with arms-forward holds and side raises shows smooth arms (no
   visible ownership jumps/drift) and zero wrist snaps on the mirror Kellan; in the
   log: no `rescue=1 questTracked=1` camera-hand latch entries, no divergence seizures
   while `questTracked=1 chainFresh=1`, and any palm-roll bias converges ≤1s.
2. Legs keep the heavy-model quality; the shrug keeps `appliedCm` ≥5cm peaks (verify
   rows still present, then judge shoulders on the mirror).
3. 157/157 green, committed and pushed, editor left booted on the gold standard, one
   line telling the user what he should see.
