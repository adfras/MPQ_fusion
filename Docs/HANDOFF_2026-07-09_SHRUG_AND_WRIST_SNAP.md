# HANDOFF — arms-down shrug dead on mirror + right-wrist snap (2026-07-09)

You are taking over live-quality work on the TestingKit5 VR embodiment stack. Read
`Docs/RESOLUTION_2026-07-09_QUEST_HANDS_ALIVE.md` before anything else — it closes the
previous "dead Quest hand stream" arc (misdiagnosis; the stream was healthy) and
explains the arm-ownership model and the new diagnostics you will rely on.

## CURRENT STATE (verified 2026-07-09)

- HEAD = 65dc997 on main, pushed (github.com/adfras/MPQ_fusion). 157/157 MediaPipe
  automation tests green.
- The user ran a worn session on 2026-07-09 (~07:26–07:31 UTC, editor boot 07:23:54).
  Result: **hand stream + arm chain confirmed live end-to-end on the mirror avatar** —
  Kellan rows show `questTracked=1` (365 rows), `armOwner=chainDirect` (389 rows),
  `handRotApplied=1` (363/392). Arms track reach; the HUD shows the new green
  "QUEST ARM SOURCE: BODY CHAIN / hands tracked L=1 R=1".
- That session's log is the FIRST with the new `armOwner=` diagnostics. At handoff time
  it is `Saved/Logs/TestingKit5.log`; after the next editor boot it rotates to
  `TestingKit5-backup-2026.07.09-*.log`. Mine it before asking for any new test.

## THE TWO GOALS (user's priority order)

### 1. SHOULDERS MUST SHRUG WITH ARMS DOWN (live mirror)

Observed by the user in the 2026-07-09 session: shoulders lift correctly when raising
one or both arms, but a pure shrug with arms hanging produces **no visible shoulder
movement** on the mirror Kellan. Prior measurements (2026-07-06 arc): his camera-measured
shrug is 7.7cm, the solver applies it, the replay-map Kellan renders 10.1cm — but the
live preview-room mirror renders ~1.4cm. A shrug is 10cm+; **under ~5cm on the mirror is
failure**.

Leads, in order:
- `mp.ClavicleDebug` was OFF in the 2026-07-09 session (0 rows) — arm it first
  (`mp.ClavicleDebug 1`). Its row (emitted in
  `MediaPipePoseDrivenAnimInstance_QuestArmSolve.cpp`, two sites — search
  `mp.ClavicleDebug`) prints `armUp= armForward= shrugCm= shrugWeight= upWeight=
  forwardWeight= relativeLiftCm= screenLift= headClearanceCm=` … — the weight names
  say the clavicle drive is scaled by arm raise/forward. That gating would explain
  exactly "shoulders move with arm raises, not with a pure shrug". One emit site is
  `mode=BodyFusionMetaHuman helpers=BodyFusionOwned` (fusion-path clavicle drive,
  commit 90711d1); the other is the shared-clearance path.
- The user's own hypothesis: the deliberate arms-at-sides placement (tuned so elbows
  look right when arms hang) may be absorbing the shoulder rise. Check whether the
  arm/clavicle solve order re-plants the shoulder after the shrug is applied
  (per-actor absorption was already a suspect in the 2026-07-06 parked notes).
- Cold-start rest-reference latch: the stage-2 clavicle lift keeps neutral shoulder
  references in `BodyState.Stage2Neutral*` (see
  `MediaPipePoseDrivenAnimInstance_BodyPoseSolve.cpp`; the "noise-robust rest
  reference" edits are inside commit c3faaa7). On his ~30s sessions a slow-settling
  neutral could eat the whole shrug. The camera signal itself is proven good (7.7cm
  measured; replay renders 10.1cm), so the loss is between solver output and this
  specific live actor.
- CANDIDATE variant already sets `mp.MediaPipeClavicleShrugWeight=1`,
  `mp.MediaPipeClavicleShrugMinCm=1`, `mp.MediaPipeClavicleShrugDirect=1`.

### 2. WRIST BREAK / SNAP (right hand, frequent)

Observed: with arms forward the mirror's left wrist can look broken; and frequently one
wrist (right, in the data) visibly breaks and **snaps to the side**, especially during
side raises. The user recalls the historical fix: **the Quest must be the single hand
authority — conflicts with MediaPipe hand data cause exactly this** (2026-07-03
hand-snap arc: ownership latch + hold fixes).

The 2026-07-09 log already isolates two mechanisms — both on side=R:
- **Semantic-roll ↔ palm-fallback basis flapping.** 38 Kellan rows have
  `wristPalmFallback=1`, ALL side=R, clustered in bursts (07:28:10, :13, :15, :17,
  :21–22, :30, :38, :49–50, :54–55, :58, 07:29:01 …). Each drop from
  `wristSemanticRoll=1` to the palm-fallback basis (and back) changes the wrist
  rotation basis — a visible snap per switch. Left hand: zero fallback rows.
  `wristPalmHeld` was 0 on every flap row — the hold never engaged.
- **Camera-rescue hand takeover.** At 07:30:39–:40 `armOwner=cameraRescue` fired on
  Kellan side=R (`questTracked=0 handRotApplied=0` for that window): a brief quest-hand
  tracking flicker handed the arm to the camera rescue, which also suppresses
  `DriveQuestHandCS` (quest hand rotation) via `bCameraOwnsHandPose` — the wrist falls
  to the non-quest path and snaps. The ownership latch lives in
  `MediaPipePoseDrivenAnimInstance_QuestArmSolve.cpp` (search
  `bCameraHandOwnershipLatched`; fast 0.2s handback when arm clearly down; dwell via
  keyed state). `mp.MediaPipeCameraHandTrace` was OFF (0 rows) — arm it; it logs only
  ownership transitions.

Fix direction per the user: quest-authoritative hands — hold the last quest-derived
wrist basis through sub-second quest flickers and through palm-fallback flaps instead of
switching bases per frame. Do NOT blind-tune `mp.QuestWristPalmTrimLeft/RightDeg`
(36.8/−11.1, calibrated for the rigged body avatar; applied globally in
`ApplyQuestWristPalmTrim` — no per-consumer gain exists today).

## CODE MAP (line numbers drift — search the markers)

- `Source/MediaPipeDriver/PoseDriven/MediaPipePoseDrivenAnimInstance_QuestArmSolve.cpp`
  — arm ownership resolution (`bQuestArmFallbackAllowed`,
  `bMetaHumanFullArmChainDirectFresh`, `bUseBodyFusionArm`, rescue demotion),
  chain-direct apply ("Camera arm-direction transplant"), camera-hand ownership latch,
  arm-length calibration gate, clavicle debug emit sites, and the `mp.QuestWristSolve`
  log emission (now with `SolveLogInput.ArmOwner`).
- `Source/MediaPipeDriver/PoseDriven/MediaPipePoseDrivenAnimInstance_QuestHandRotation.cpp`
  — `DriveQuestHandCS`: semantic-roll wrist rotation, palm-fallback path, palm trims.
- `Source/MediaPipeDriver/PoseDriven/MediaPipePoseDrivenAnimInstance_QuestSpaceMapping.cpp`
  — quest wrist position mapping (fills the wrist trace when the questWrist path owns).
- `Source/MediaPipeDriver/PoseDriven/MediaPipePoseDrivenAnimInstance_BodyPoseSolve.cpp`
  — stage-2 clavicle lift + neutral/rest references (shrug suspect).
- `Source/MediaPipeDriver/PoseDriven/MediaPipePoseDrivenAnimInstance.cpp` — node
  `PreUpdate` (keyed store, QuestHands polling, HUD input; `bArmSourceChainActive`).
- Diagnostics plumbing touched in 65dc997:
  `Quest/MediaPipeQuestWristDiagnosticFormatter.h/.cpp` (armOwner field),
  `Quest/MediaPipeQuestRuntimeDebugService.h/.cpp` +
  `Quest/MediaPipeQuestWristDebugReporter.h/.cpp` (chain-status HUD),
  `Embodiment/EmbodiedFusionComponent.h/.cpp`,
  `Tests/MediaPipeDiagnosticsTests.cpp` (format assertions — update if you add fields).
- RULE (hard-won): cross-frame solver state must live in the keyed runtime store
  (`GetQuestWristRuntimeState(RuntimeStateKey)`), never in node members — CacheBones /
  pose-node resets wipe node members every frame; skip keyed writes when
  `RuntimeStateKey==0` (pre-PreUpdate evaluation).

## ENVIRONMENT CHEAT SHEET

- Project: `D:/Epic/Unreal_Projects/TestingKit5` (UE 5.8 at `D:/Epic/UE_5.8`). Local
  time = UTC+8; log timestamps/filenames are UTC.
- Build (editor MUST be closed; check `Get-Process UnrealEditor`):
  `D:/Epic/UE_5.8/Engine/Build/BatchFiles/Build.bat TestingKit5Editor Win64 Development
  -Project=D:/Epic/Unreal_Projects/TestingKit5/TestingKit5.uproject -WaitMutex -NoUBA
  -UBANoDetour -MaxParallelActions=4` (~20 min; output can look stalled during the
  final link — check cl.exe/dotnet liveness, not the tail).
- Tests (157 expected; the runner occasionally drops the last-queued test — rerun it
  solo before believing red):
  `UnrealEditor-Cmd.exe <uproject> -ExecCmds="Automation RunTests MediaPipe; Quit"
  -TestExit="Automation Test Queue Empty" -unattended -nop4 -nosplash -nullrhi`
  (results in `Saved/Logs/TestingKit5.log`; count `Test Completed. Result={Success}`).
- **ripgrep silently returns 0 matches on these logs** (UTF-8 BOM) — use GNU grep in
  bash or Python. Validate any zero-match grep against a known-present string.
- Editor MCP: native server, `POST http://127.0.0.1:8000/mcp`, JSON-RPC. Flow:
  `initialize` → capture the `Mcp-Session-Id` response HEADER → send it on every call.
  Project tools live behind the wrapper: `tools/call name=call_tool arguments=
  {"toolset_name":"testingkit_toolset.TestingKitToolset","tool_name":"exec_console",
  "arguments":{"command":"..."}}` (also `get_cvar`, `set_cvar`, `set_cvars`,
  `tail_log`). Console output lands in the LOG, not the MCP response. MCP calls run on
  the game thread — during live VR PIE use only cheap calls.
- Boot rig: `Content/Python/init_unreal.py` auto-arms the gold standard on every
  interactive boot (Kellan profile, CANDIDATE variant, heavy model
  `mp.AutoQuestWebcamPoseModel=heavy`, `mp.StartLiveLowerBodyTrial`). Verify with
  `mp.DumpLiveProfileSettings` (log shows `variant=candidate … trial=55`). Launch with
  the map on the command line:
  `UnrealEditor.exe <uproject> /Game/MetaHumanRooms/L_MetaHumanPreviewRoom_MPQSignalCompare_01`.
- Editor-wedge detection = log LastWriteTime frozen, NOT MCP acks. A capture sink wraps
  `SetConsole*` in `Runtime/MediaPipeDriverRuntime.cpp` and intercepts profile CVar
  writes while armed — remember it if CVars mysteriously don't stick.

## USER STANDARDS (non-negotiable)

- The ONLY acceptance judge is the mirror avatar `MP_LiveMetaHumanKellan` in the preview
  room. Never cite the Manny reference actor or internal metrics as success.
- Fixes, not diagnostics. Instrument silently; report only results and changes.
- Verify everything possible from logs BEFORE any worn test. He does ONE test per
  milestone, under 30 seconds.
- Human-scale amplitudes: shrug 10cm+ (under ~5cm on the mirror = failure), wrists
  straight, arms full reach.
- Clean up any actors you spawn — leftovers have hijacked his VR session before.
- 157/157 green + commit&push per milestone. Own mistakes in the first sentence.

## DEFINITION OF DONE

1. Arms-down shrug renders ≥5cm (target ~10cm) shoulder rise on the live mirror Kellan,
   without degrading the arms-down elbow placement he tuned deliberately.
2. A 30s worn session with arms-forward holds and side raises shows zero visible wrist
   snaps on the mirror; in the log, side=R `wristPalmFallback` flapping is gone (held or
   hysteresis instead of per-frame basis switches) and brief quest-hand flickers no
   longer produce `armOwner=cameraRescue` + `handRotApplied=0` windows during tracked
   side raises.
3. 157/157 green, committed and pushed, editor left booted on the gold standard
   (SignalCompare_01 map, Kellan, candidate+heavy confirmed via DumpLiveProfileSettings,
   no stray actors), with a one-line description of what he should see when he tests.
