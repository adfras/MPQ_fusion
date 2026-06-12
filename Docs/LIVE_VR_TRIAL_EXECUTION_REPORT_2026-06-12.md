# Live VR Trial Execution Report - 2026-06-12

Session goal (user): trial the avatar embodiment LIVE with the Quest headset, Quest hand
tracking, and iPhone MediaPipe streaming - "press VR Preview myself and observe the MetaHuman
in front of me", with an in-VR panel showing the iPhone feed and tracked bones. Iterated
through seven worn-headset feedback rounds. Builds on the same-day lower-body scaffold work
(`LOWER_BODY_SCAFFOLD_EXECUTION_REPORT_2026-06-12.md`, commit `784c00d`).

All rounds followed the same pipeline: edit -> close editor -> `Tools\BuildTestingKit5EditorFast.ps1`
-> headless `Automation RunTests TestingKit5.MediaPipe` -> relaunch on
`/Game/MetaHumanRooms/L_MetaHumanPreviewRoom_MPQSignalCompare_01` -> restart AgentBridge ->
`mp.StartLiveLowerBodyTrial` -> worn-headset verdict -> diagnose from
`Saved/Logs/TestingKit5.log` scaffold rows before touching code. Suite grew 118 -> 125 tests.

## What shipped (worn-headset validated)

### Live trial infrastructure
- `mp.StartLiveLowerBodyTrial` / `mp.StopLiveLowerBodyTrial`: CVar policy layer
  (`LiveLowerBodyTrial`, priority CaptureScope, 25 settings) bringing the replay-proven leg
  solve live; re-asserted after every live-profile apply; drops a stale finished-replay
  ReplayEvaluation layer. Run sheet: `MEDIAPIPE_VALIDATION_AND_OPERATIONS.md`.
- `AMediaPipeVrTrackingPanelActor` (new): world-space quad right of the wearer showing the
  iPhone DirectPreview texture with a full-body skeleton overlay
  (`mp.AutoQuestWebcamPreviewBodySkeleton`), lazy yaw-follow.
- VR-Preview auto-screenshots restored: the 06-11 consolidation had archived
  `Tools/StartQuestMirrorEvidenceCapture.ps1`, which the CodexAgent plugin launches on
  BeginPIE - restored to `Tools/`; do not re-archive plugin-referenced scripts.

### Body solve, round by round
1. **Arms/head frozen** -> the trial layer must NOT enable `mp.MediaPipeDriveSpine` (camera-
   locked torso basis stiffens Quest arms, parks the head). Added `DriveHmdHeadCS`: head from
   the worn HMD (pitch/roll direct, yaw vs a self-calibrating neutral).
2. **Camera drift on lean-back; frozen hips** -> `DriveLivePelvisLeanTwistCS`: CLOSED-LOOP
   lean - pelvis swung so the reference pelvis->head line points at the comp-mapped HMD,
   pulled back from the eyes by `mp.MediaPipeHmdLeanEyeBackCm` (12 cm; the camera sits at the
   eyes, not the head bone - without the pull-back, deep bends put the camera inside the
   chest).
3. **Pelvis yaw**: primary source is the Quest body-tracking hips joint
   (`XR_BODY_JOINT_HIPS_FB`, already located every frame by the FullArmChain provider, now
   read). Yaw = horizontal heading drift of the joint's most-horizontal local axis
   (`SelectMostHorizontalAxis` / `TryGetAxisHeadingDeg` - full-delta swing-twist mixes in bend
   pitch/roll; heading is immune). All yaw sources feed ONE rate-limited state
   (`ApproachAngleDeg`, 120 deg/s, wrap-aware) with continuity re-anchoring on source
   switches; a measured 27-degree pelvis snap on tracking dropout motivated this. Composition
   order matters: pelvis = Lean x Twist x Ref with the lean solved against the TWISTED torso.
   Slow drift recenter: neutral creeps to current at <= 0.5 deg/s only while |yaw| < 10 deg
   sustained 5 s (held twists never decay). Camera-foreshortening estimator remains fallback.
4. **DONNING GATE** (the decisive calibration fix): pressing VR Preview happens bent over a
   desk holding the headset, so every first-sample/session-anchored neutral latched garbage
   (measured ~30 deg standing yaw bias). `UpdateLiveNeutralGate` blocks the live drives until
   the HMD reports worn (`IHeadMountedDisplay::GetHMDWornState`), sits above 120 cm, is
   roughly level, and has been still 1 s - then resets ALL live neutrals to the settled
   standing pose. One-way per session; replay/fusion auto-pass; never arms without an HMD.
   Scaffold rows log `neutralGate(armed/ready/settle)`, `bodyYaw(deg/src)`, `sway(x/y/active)`.
5. **Lateral hip sway**: pelvis translation from the hips-joint POSITION vs its gate-latched
   neutral (world XY, clamped by the planar offset ratio, replaces the camera hip-vs-support
   term while fresh, eases home on dropout).
6. **Responsiveness**: trial layer raises `mp.MediaPipeAdaptivePoseBeta` 0.25 -> 0.45 and
   `mp.MediaPipeAdaptivePoseMaxPredictionMs` 50 -> 80 (One-Euro velocity lag on fast leg
   moves; remaining latency is phone->PC transport).

## Finger overlap investigation (closed: accepted limitation)

Reported: middle/ring fingers interpenetrate on curled hands. Three mitigations were built,
worn-headset tested, and REVERTED (CVars remain, default 0, pure math + tests kept):
1. Curl-plane splay clamp - preserved the structural bias budget, stripped the separating
   noise: worse.
2. Neutral-relative splay (per-bone EMA bias subtraction) - the hinge-axis estimate from
   `Cross(RefFingerDir, RefFingerCurlDir)` is tilted for some fingers, so plane projection
   corrupted CURL (fingers driven into the palm): worse.
3. Pairwise adjacent-finger separation + hand pose gate (hold pose on untracked frames or
   physically impossible curl rates; instrumented evidence: open hand snapped to full-fist
   mean curl in one 98 ms frame, 23% of left-hand frames `tracked=0` yet still driven) - the
   gate's rate threshold rejected real fist closes (visible twitching); separation metric is
   blind to lateral convergence between fingers at different curls: no improvement.

Conclusive A/B: Meta-style per-joint-rotation retarget (`mp.QuestFingerJointRetarget 1`,
the project's dormant jointOrientation mode) engaged 300/300 frames after a fresh OpenXR
session - and shows the SAME overlap as the accepted segmentDirection mode. Two independent
retarget mechanisms producing the same artifact pins the residual on (a) Quest guessing
self-occluded curled fingers (bunches middle/ring) and (b) MetaHuman finger mesh thickness.
Meta's own avatars show the same jumble. Defaults stay segmentDirection. NOTE: the first A/B
of that flag was a silent no-op - the auto live profile resets `mp.QuestFingerJointRetarget 0`
at every PIE start, so experiment flags must be applied AFTER PIE begins, and the new
`jointOrientation FELL BACK` log line names each veto when the mode falls through.

## Operational gotchas (cost real time today)

- A Quest Link session can silently lose hand AND body tracking together
  (`IHandTracker stateValid=0`, `chainActive=0`, HMD fine; the runtime's white hands appear
  and the avatar looks unembodied). Fix: headset-side hand-tracking re-enable, elevated
  `OVRService` restart (drops the Link login!), or PC reboot - never solve code.
  `mp.DumpQuestHands` shows the tracker state directly.
- The build watcher must be pinned to the specific log path the build wrapper prints
  (`ls -t` races the previous build's log).
- AgentBridge: kill by port 8765 before restarting (`node src\server.js` in `AgentBridge/`).

## Verification

- 125/125 `TestingKit5.MediaPipe` automation tests (new: BodyYawHeading, TwistAboutAxis,
  SplayClamp, PairSeparation, PoseGate, plus trial-command coverage).
- Replay equivalence preserved: every live-trial behavior is gated behind the trial layer or
  live-only branches; replay evaluation paths are byte-stable (finger experiment CVars
  default 0).
- Worn-headset acceptance by the user across donning gate, hip yaw/sway, lean, head follow,
  responsiveness. Finger overlap accepted as a known limitation.
