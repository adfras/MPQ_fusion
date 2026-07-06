# HANDOFF — Fix dead Quest hand tracking (2026-07-06, urgent)

You are taking over from a previous agent. The user (Alan) is exhausted and has lost
trust after a very long session. Read this fully before acting. Do not guess, do not
theorize at him, do not ask him to test things repeatedly. Fix first, verify with data,
involve him once at the end.

## THE ONE JOB

Quest hand tracking is not reaching the fusion solver. Live VR sessions show:

- `mp.QuestWristSolve` trace: `questTracked=0  positionApplied=0` — BOTH hands, every frame
- The in-VR "QUEST ARM CALIBRATION" HUD sits at `frames=0` forever (it needs hand frames)
- Consequences the user sees: avatar arms do not extend fully (arm-length calibration
  never completes) and the wrists look broken (running on the untracked fallback path)

This is the ONLY current task. Everything else is parked.

## HARD EVIDENCE (all verifiable in logs)

- WORKED at 11:53 UTC 2026-07-06: log `TestingKit5-backup-2026.07.06-12.14.29.log`
  contains `questTracked=1` rows in a live worn session.
- DEAD by ~13:35 UTC same day and in every session since.
- SURVIVED a full PC reboot (user rebooted; fresh session still `questTracked=0` both hands).
- OS-level hand tracking works: the user's first-person hands render articulated in VR.
- OpenXR runtime is Oculus in both working and broken sessions
  (`HKLM:\SOFTWARE\Khronos\OpenXR\1\ActiveRuntime` = oculus_openxr_64.json).
- `XR_EXT_hand_tracking` + FB hand extensions listed identically in both sessions' logs.
- `mp.QuestHandTracking = 1` (LastSetBy: Constructor — never overridden).
- Both controllers/hands: the failure is bilateral and constant, not positional dropout.

## WHAT THE USER BELIEVES (respect it)

He believes the previous agent's changes broke it. The previous agent audited the day's
commits and found nothing touching the input path — but was wrong repeatedly today, so
treat the audit as unverified. THE DECISIVE EXPERIMENT (proposed, not yet run):

    cd D:/Epic/Unreal_Projects/TestingKit5
    git checkout -b pre-today-verification c48a308   # last commit before today's work
    # build: D:/Epic/UE_5.8/Engine/Build/BatchFiles/Build.bat TestingKit5Editor Win64
    #        Development -Project=D:/Epic/Unreal_Projects/TestingKit5/TestingKit5.uproject
    #        -WaitMutex -NoUBA -UBANoDetour -MaxParallelActions=4
    # then ONE short worn test: grep questTracked in the log.

If hands work on c48a308 → today's code broke it; bisect today's commits
(38fd8cd, 182eb65, cbfe03c, e15d410, 90711d1, 34f6458, + uncommitted-at-time-of-writing
gate-trace/noise edits — `git status` first). If hands are still dead on c48a308 →
external cause; prime remaining suspects, in order:
1. Meta Horizon Link PC app settings — hand tracking / "Developer Runtime Features"
   toggle (GUI-only; survives reboots; app is "Meta Horizon Link" in Start menu).
   The previous agent was about to inspect this via computer-use when handed off.
2. Headset-side Settings → Movement tracking → Hand tracking (survives everything
   PC-side; a Quest auto-update tonight could have flipped it).
3. USB topology: the user "stabilized Camo" (iPhone camera) between the working and
   broken sessions — if the iPhone moved onto USB sharing the Link cable's controller,
   bandwidth contention can kill hand streaming while pose tracking survives.
4. Meta runtime update mid-evening (check Meta app version/update history ~12:00-13:30 UTC).

## TIMELINE OF TODAY'S CHANGES (for the bisect)

All commits are pushed to github.com/adfras/MPQ_fusion main:
- 38fd8cd solver: settings consolidation + pelvis/yaw anchors + palm trim (capture sink
  wraps SetConsole* in MediaPipeDriverRuntime.cpp — audit this first if bisecting;
  it intercepts every profile CVar write when armed)
- 182eb65 pipeline python,   cbfe03c assets,  e15d410 startup rig (init_unreal.py arms
  CANDIDATE variant + StartLiveLowerBodyTrial + heavy model at every interactive boot)
- 34f6458 chest-yaw anchor on raw camera-space landmarks
- 90711d1 fusion-path clavicle shrug (DriveClavicleShrugCS in body solve)
- UNCOMMITTED (check git status): shrug gate-trace + noise-robust rest reference edits
  in MediaPipePoseDrivenAnimInstance_BodyPoseSolve.cpp

None of these touch Quest input ingestion on their face — but verify, don't trust.

## ENVIRONMENT CHEAT SHEET

- Project: D:/Epic/Unreal_Projects/TestingKit5 (UE 5.8 at D:/Epic/UE_5.8)
- Editor MCP: native server port 8000, toolset `testingkit_toolset.TestingKitToolset`,
  tool `exec_console` (see scratchpad payloads; ALWAYS build JSON via python json.dump —
  inline backslashes silently break).
- Gold standard live setup: map /Game/MetaHumanRooms/L_MetaHumanPreviewRoom_MPQSignalCompare_01,
  profile Kellan; init_unreal.py auto-arms everything on interactive boot.
- Tests: UnrealEditor-Cmd -ExecCmds="Automation RunTests MediaPipe; Quit"
  -TestExit="Automation Test Queue Empty" -unattended -nop4 -nosplash -nullrhi
  (157 expected; runner sometimes drops the last-queued test — rerun it solo before
  believing red).
- Log: Saved/Logs/TestingKit5.log (rotates to -backup-* on editor start). Editor-wedge
  detection = log LastWriteTime frozen, NOT MCP acks.
- Builds require the editor closed (QUIT_EDITOR via exec_console; force-kill may need
  the user's click on a save dialog).

## USER STANDARDS (non-negotiable)

- He judges ONLY the mirror avatar he sees in VR preview (actor MP_LiveMetaHumanKellan
  in the preview room). Never cite the Manny reference actor to him.
- Amplitudes must be human-scale (a shrug is ~10cm+, wrists straight, arms full reach).
- He wants fixes, not diagnostics. Instrument silently, speak in results.
- Sessions from him: assume ONE, under 30 seconds. Design everything around that.
- Commit and push when things work (repo above). 157/157 green before any handoff.

## PARKED (do not touch until hands work)

- Shrug: solver-side verified working (camera 7.7cm signal, drive applies, replay
  renders 10.1cm on the replay-map Kellan) but the PREVIEW-ROOM mirror Kellan renders
  ~1.4cm live — investigation state in memory file testingkit5-chunked-solve.md and
  Docs history. Blocked on hands anyway (calibration).
- Squat scaffold over/undershoot (mixed, per-window data in memory), chest-yaw anchor
  verification, palm-trim per-consumer gains (first-person hands vs rigged body differ).
