# MediaPipe Validation And Operations

Status: consolidated 2026-06-05 for TestingKit5/UE 5.8. Updated 2026-06-12 with the live
lower-body trial flow.

## Live Lower-Body Trial (worn headset, 2026-06-12)

One-command flow for trialing the replay-verified lower-body solve live with the Quest
headset, Quest hands, and iPhone (Camo) MediaPipe tracking. The user presses VR Preview
themselves; the placed self-view MetaHuman stands in front of the wearer
(`bShowMediaPipeSelfView` on the embodied pawn), and a tracking panel floats to the wearer's
right showing the live iPhone feed with the tracked-bone skeleton drawn on it.

Run sheet:

1. Confirm the iPhone/Camo source (see "Manny iPhone/Camo Baseline" for the camera index and
   `mp.AutoQuestWebcamDirectWmfCapture 1`; the panel needs the direct WMF capture path).
2. In the editor console: `mp.StartLiveLowerBodyTrial`
3. Press VR Preview and stand in view of the iPhone camera.
4. Observe: the self-view MetaHuman in front follows squats/steps with metric depth (HMD is
   the squat-depth authority); the right-side panel shows which bones the camera is tracking
   (green=tracked, amber=weak, red=stale; missing limbs draw nothing).
5. `mp.StopLiveLowerBodyTrial` restores the stable-body live defaults.

What the trial layer enables (CVar policy layer `LiveLowerBodyTrial`, priority CaptureScope,
re-asserted after every live profile apply so the stable-body defaults cannot stomp it; an
ACTIVE dataset replay still outranks it, and a stale finished-replay layer is dropped):
legs/pelvis drive with FK root grounding and foot rotation (no leg IK), knee-pole
suppression 0.6, grounded world-up feet, flat-foot pitch clamp, HMD squat scaffold
(HmdWeight 1.0, FlexionWeight 0.8, MaxAdjust 40 deg), bend redistribution 0.8, scaffold
diagnostics rows, HMD head follow (`mp.MediaPipeDriveHmdHead`: pitch/roll direct, yaw
against a self-calibrating neutral, mirrored for the mirror-facing avatar via
`mp.MediaPipeHmdHeadMirror`), HMD body lean (`mp.MediaPipeDriveHmdLean`: pelvis pitch/roll
from the HMD's planar displacement so leaning back keeps the camera over the avatar's
chest), hip twist (`mp.MediaPipeDriveHipTwist`: pelvis yaw primarily from the Quest body-tracking
hips joint - heading drift of its most-horizontal axis, donning-gated neutral, slow
recenter under sustained near-zero yaw - with the MediaPipe foreshortening estimator
bounded by `mp.MediaPipeHipTwistMaxDeg` as the fallback; mirrored via
`mp.MediaPipeLivePoseMirror`), lateral hip sway (pelvis translation from the body-tracking
hips joint position against its gate-latched neutral, clamped by the planar offset ratio),
responsiveness raises (`mp.MediaPipeAdaptivePoseBeta 0.45`,
`mp.MediaPipeAdaptivePoseMaxPredictionMs 80` - less One-Euro lag on fast leg moves, longer
prediction against phone transport latency), `mp.QuestVrTrackingPanel 1`, and the preview
body skeleton (`mp.AutoQuestWebcamPreviewBodySkeleton`).

Live neutral references (body-yaw zero, lean neutral, hips heading/position neutrals, HMD
height baseline) latch through a DONNING GATE: nothing latches until the headset reports
worn, sits above 120 cm, is roughly level, and has been still for 1 s - then every live
neutral re-zeros to that settled standing pose (one-way per session). Scaffold rows log it
as `neutralGate(armed=.. ready=.. settle=..)` plus `bodyYaw(deg=.. src=..)` and
`sway(x=.. y=.. active=..)`.

VR Preview auto-screenshots: the CodexAgent editor module starts
`Tools/StartQuestMirrorEvidenceCapture.ps1` automatically on every VR-Preview PIE and saves
Oculus Mirror captures under `Saved/QuestScreenshots/vrpreview_quest_mirror_<timestamp>/`.
(The 2026-06-11 tools consolidation had archived that script, which silently disabled the
auto-capture; it is restored to `Tools/` - keep it there.) The layer deliberately does NOT enable
`mp.MediaPipeDriveSpine`: the 2026-06-12 worn-headset trial showed spine retargeting on top
of the live profile stiffens the Quest-driven arms and parks the head on the camera-locked
chest basis ("arms and head don't move"); upper-body ownership stays exactly on the proven
live profile.

For worn-headset evidence (screenshots/logs), run `Tools\CaptureMetaHumanQuestVrEvidence.ps1`
alongside the VR Preview session.

Tracking panel tuning: `mp.QuestVrTrackingPanelRightCm` (58), `ForwardCm` (95), `UpCm` (-6),
`WidthCm` (44), `FollowHalfLife` (0.22). The panel is a world-space quad managed by the
embodied pawn (`MP_LiveVrTrackingPanel`) textured with the direct webcam preview (overlay
baked in), so the desktop spectator window and the headset see the same diagnostics.

Evidence to watch in the log: `mp.MediaPipeLegScaffold actor=...` rows (HMD
baseline/drop/alpha/confidence, fused share, per-leg flexion/redistribution/foot pitch,
contact state). Lower-body quality conclusions still require the worn-headset confirmation,
not desktop PIE alone.

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

All TestingKit-prefixed MediaPipe automation names use `TestingKit5.MediaPipe.*` (migrated 2026-06-11; older evidence logs cite the previous `TestingKit3.MediaPipe.*` names). The focused suite filter is `Automation RunTests TestingKit5.MediaPipe` and currently discovers 112 tests. A further ~32 tests use a bare `MediaPipe.*` prefix (e.g. `MediaPipe.TrackingSourceFrameBuilder`) and are included by the broad `RunTests MediaPipe` filter above.

## Avatar-Locked Sync Calibration

2026-06-09 update: `mp.PrepareAvatarLockedSyncCalibrationCapture` is visible source-to-avatar correlation evidence, not shadow evidence. It preserves `30 Hz`, `boneMode=all`, all MetaHuman bones/helpers, seven green 30-second phases, and no avatar scaling/deformation, but temporarily forces `mp.BodyFusion.WritePose=1`, `mp.BodyFusion.MediaPipeAuthority=2`, `mp.MediaPipeDriveSpine=1`, `mp.MediaPipeDrivePelvisTranslation=1`, `mp.MediaPipeDriveLegs=1`, `mp.MediaPipeUseLegIK=0`, `mp.MediaPipeUseLegIKFootPlant=0`, and `mp.MediaPipeDriveFootRotation=1`. Analyzer preflight classifies avatar-locked captures with no-write/shadow policy as `invalid_capture_policy`. Kellan replay uses direct MetaHuman segment legs because measured replay target IK overdrives lower-body phase while direct segment rotations keep Kellan's own proportions.

Do not lower sample rate, reduce all-bone coverage, or switch to selected bones to compensate for scheduler misses. Avatar-locked sync captures target 30 Hz for 210 seconds with all 342 Kellan bones in the current MetaHuman body. Use recorder timing counters and the all-bone hot-path stress automation to separate scheduler misses, skipped ticks, sample build cost, enqueue cost, writer backlog, file flush/write time, and post-capture analysis time before asking for another VR Preview.

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

For MPQ Stage 2A shoulder evidence trials, treat `solver_snapshot_from_recorder_stage2_fallback=true` as an estimate only. A useful runtime proof needs `solver_snapshot_from_component_cache=true` or `solver_snapshot_from_native_anim_instance=true`; fallback-only Stage 2A rows do not prove the anim node recorded the runtime shoulder evidence.

The 2026-06-08 clean-fusion refactor disables the old direct MetaHuman Stage 1/Stage 2 bone-offset path. Stage 2A now records MediaPipe shoulder/shrug evidence for BodyFusion diagnostics only; it must not directly translate MetaHuman clavicles, clavicle helper bones, upper-arm shoulder sockets, elbows, wrists, hands, fingers, arm fallback bones, or the HMD camera anchor.

Shoulder/shrug motion must be promoted through BodyFusion and the avatar-profile pose writer, not through a separate component-space clavicle layer. Quest/HMD source data provides the HMD/head and hand/wrist/finger endpoints; MediaPipe source data provides body and shoulder observations; BodyFusion owns the final avatar authority and pose output.

For MetaHuman, the fused pose writer must account for the main clavicle/upper-limb chain and the helper leaf groups together: `clavicle_out`, `clavicle_scap`, `clavicle_pec`, upper-arm twist/corrective/bicep/tricep helpers, lower-arm corrective helpers, and `wrist_inner`/`wrist_outer`. Shoulder/shrug motion may rotate the main clavicle from the fused MediaPipe shoulder direction and then derive helper leaves from the current main-chain pose, but it must not force-set the upper-arm socket to the MediaPipe shoulder point. Keep legacy `mp.MediaPipeDriveClavicles`, `mp.MediaPipeDriveMetaHumanArmHelpers`, and `mp.MediaPipeDriveArmTwistBones` disabled during MPQ visible-fusion tests; BodyFusion owns the MetaHuman helper pass when `mp.BodyFusion.WritePose=1`.

2026-06-07 Stage 2A checkpoint: after moving Stage 2A before the Quest arm/hand solve and rebuilding normally, the proof run `Saved/CodexAgent/Diagnostics/mpq_shadow_latency_stage2aPreArmSolveProof_20260607_203128.json` completed 45 seconds with 1339 samples. The driven component was the MetaHuman body mesh `BP_Kellan_C_0.Body`, despite legacy internal labels and analyzer fields still using Manny names. Stage 2A solver fields came from the runtime anim solver, not the recorder fallback. Both sides reached the 5 cm clamp, `stage2_shoulder_hint_ready=true`, and `stage2_visible_output_fail_pairs=[]`.

The same proof showed positive MetaHuman shoulder sign: left applied lift to clavicle Z correlation `0.992`, right `0.998`. Quest wrist to MetaHuman hand output was no longer flat in that capture: left hand Y/Z correlations `0.923`/`0.966`, right hand Y/Z `0.965`/`0.972`. Left Quest tracking was intermittent during part of the run, so left-side hand/arm conclusions should be treated as useful but not a perfect clean proof. Analyzer reports now expose `driven_component_summary.primary_driven_component` to distinguish the internal driver actor label `MP_LiveMediaPipeManny` and legacy `manny.*` signal namespace from the actual driven MetaHuman body component.

Stage 2 conflict-stress review should start from `MPQ_Stage2A_Conflict_Stress_Test_Plan.md`, but the old `stage2Scale=4.5` profile is obsolete for MetaHuman MPQ testing. Shadow capture is evidence-only and uses `WritePose=0`; do not use it as visible movement proof. Visible MPQ VR proof should use BodyFusion write pose enabled, Stage 1/Stage 2 direct output disabled, `MediaPipeAuthority=0`, `mp.MediaPipeDriveClavicles=0`, `mp.MediaPipeDriveMetaHumanArmHelpers=0`, and no MediaPipe arm fallback.

2026-06-08 clean-fusion refactor: the MetaHuman MPQ path records calibrated MediaPipe shoulders into the fused pose, keeps Quest/HMD as source observations for HMD/head and wrist/hand/finger endpoints, blocks MediaPipe elbow/wrist/hand fallback, blocks raw AutoQuest clavicle/spine/pelvis drive while BodyFusion is enabled, forces Stage 1/Stage 2 direct MetaHuman bone writes to fail closed, and routes MetaHuman shoulder/helper output through the BodyFusion pose writer. `mpq_shadow_stage2_debug.csv` may still contain `neutral_ready`, `signed_lift_evidence`, and target fields for evidence review, but `applied_clavicle_lift_cm` and `applied_clavicle_helper_lift_cm` should stay `0.0` because Stage 2 is not allowed to directly translate MetaHuman bones.

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
