# MediaPipe Validation And Operations

Status: consolidated 2026-06-05 for TestingKit5/UE 5.8.

## Build

```powershell
D:\Epic\UE_5.8\Engine\Build\BatchFiles\Build.bat TestingKit5Editor Win64 Development -Project="D:\Epic\Unreal_Projects\TestingKit5\TestingKit5.uproject" -WaitMutex
```

Use `-NoHotReload` or `-DisableUnity` only when investigating build isolation. Do not report a gameplay or VR fix from build success alone.

## Automation

```powershell
D:\Epic\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe D:\Epic\Unreal_Projects\TestingKit5\TestingKit5.uproject -ExecCmds="Automation ListTests MediaPipe; Quit" -unattended -nop4 -nosplash
D:\Epic\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe D:\Epic\Unreal_Projects\TestingKit5\TestingKit5.uproject -ExecCmds="Automation RunTests MediaPipe; Quit" -TestExit="Automation Test Queue Empty" -unattended -nop4 -nosplash
```

Expected caveat: many MediaPipe automation names still use `TestingKit3.MediaPipe.*`. Treat that as naming debt unless the run fails.

## Manny iPhone/Camo Baseline

For `/Game/MCPBench/Maps/L_MCP_MediaPipeMannyRoom` with an iPhone exposed through Camo, keep the source setup explicit:

```text
mp.PlacedEmbodiedVideoFile ""
mp.AutoQuestWebcamHandsCameraIndex 1
mp.AutoQuestWebcamDirectWmfCapture 1
mp.AutoQuestWebcamPreview 1
mp.AutoQuestWebcamPreferredWidth 1280
mp.AutoQuestWebcamPreferredHeight 720
mp.AutoQuestWebcamPreferredFps 30
mp.StartPlacedEmbodiedTracking
```

`mp.AutoQuestWebcamHandsCameraIndex 1` is the current Windows DirectShow index for `Camo` on Alan's machine. Recheck device order before treating that index as portable.

The Manny shoulder-shrug visibility baseline is source-owned by `MediaPipeDriverRuntime::ApplyMediaPipeOnlyEmbodiedWebcamProfile()`:

```text
mp.MediaPipeDriveClavicles 1
mp.MediaPipeClavicleShrugWeight 0.20
mp.MediaPipeClavicleShrugMinCm 2.0
mp.MediaPipeClavicleShrugFullCm 8.0
mp.MediaPipeShoulderLiftTranslationScale 4.5
mp.MediaPipeHolisticHeadSolve 1
```

Regression guard: `TestingKit5.MediaPipe.RuntimeProfile.MediaPipeOnlyEmbodiedWebcamShoulderShrugDefaults` must pass before changing the MediaPipe-only embodied webcam profile. Do not claim this visual behavior from automation alone; validate in PIE or headset view with the user in frame. When user pose timing matters, ask the user to press Play rather than starting PIE for them.

## Bridge

Start the local bridge:

```powershell
cd D:\Epic\Unreal_Projects\TestingKit5\AgentBridge
npm install
npm start
```

Default route: `http://127.0.0.1:8765`. If the port is busy, set `CODEX_AGENT_PORT`.

Core calls:

```text
GET  /status
GET  /events
POST /tool
POST /approve
POST /cancel
```

Preferred `POST /tool` shape:

```json
{"tool":"inspect_scene","args":{"filename":"before.png","includeActors":false}}
```

Use `Docs/CODEX_BRIDGE_CURRENT_STATE.md` for efficient wrapper details.

## Visual and PIE proof

- Before visual edits: capture a focused before screenshot.
- After placement/material/layout edits: capture a focused after screenshot with `compareTo`.
- After runtime behavior: start PIE, drive the state, wait outside Unreal Python for latent movement/timeline ticks, capture focused runtime screenshots, and inspect actor/component state.
- For sky-heavy or unfocused screenshots, retake with `focusActor`, `actorLabel`, or `focusLocation`.
- For overlap tests, keep PlayerStart outside the trigger, put floor-level interactables near `Z=0`, set purely visual meshes to `NoCollision`, and move the PIE pawn to capsule-center `Z` around `92`.

## VR proof

Only claim a VR embodiment result when the proof includes the relevant scope:

- Quest device connected and HMD enabled.
- Worn state is `WORN` when subjective headset behavior is being claimed.
- HMD pose has valid tracking.
- Hands are tracked when arm/wrist/finger behavior is in scope.
- `mp.BodyFusion.Debug`, `mp.DumpQuestHands`, `mp.QuestWristSnapshot`, `mp.QuestWristRollCompact`, or `mp.MetaHumanArmSanity` rows match the claim.
- Screenshot or user headset confirmation shows the visual behavior.

## Packaging for review

Review zips should include docs, `Source`, `Scripts`, `AgentBridge` source/config, `Config`, plugin source/docs, `.uproject`, and project descriptors. Exclude Unreal generated outputs: `Binaries`, `Intermediate`, `DerivedDataCache`, `Saved`, and `AgentBridge/node_modules`.
