# MediaPipe Tracking Issue and Next Engineering Plan - 2026-05-25

> Update 2026-05-26: the Emory forward-lean neck issue described here has an accepted follow-up checkpoint in `Docs/EMORY_FORWARD_LEAN_NECK_FINDINGS_2026-05-26.md`. Keep this file as the pre-fix diagnostic history; do not treat it as the latest Emory neck state without checking the 2026-05-26 findings.

This document records the current MediaPipe embodiment failure in `D:\Epic\Unreal_Projects\TestingKit3` and the next engineering move. It is intentionally diagnostic: the current issue should not be treated as another camera offset problem.

## Latest Evidence

Latest user VR Preview / Oculus Mirror capture set:

```text
Saved/QuestScreenshots/vrpreview_quest_mirror_20260525_110934
```

Relevant screenshots from that run:

```text
vrpreview_quest_mirror_20260525_110934_004_110944.png
vrpreview_quest_mirror_20260525_110934_008_110956.png
vrpreview_quest_mirror_20260525_110934_011_111005.png
```

User-visible symptom:

- The head appears to follow HMD movement.
- The body is not synchronized with the head during lean.
- The neck distorts or stretches to reconcile the mismatch.
- The local owner view still sees the hoodie/jumper when leaning, especially backward.
- The issue is now most visible on Emory, but the same architectural failure can affect Manny.

Latest log evidence:

```text
Saved/Logs/TestingKit3.log
```

The key runtime evidence from the latest Emory run is:

```text
Auto Quest embodied: local head cull owner=BP_MP_EmbodiedMetaHumanPawn_C_0 avatar=BP_Emory_C_0 ownerNoSeeComponents=7 localBodyProxy=0.
```

This means owner head/face culling is active, but no owner-only first-person body proxy is active for the MetaHuman body.

Representative HeadLock row during backward lean:

```text
actor=MP_LiveMetaHumanEmory
skeleton=MetaHuman
solverCamera=(17.5,-147.3,142.9)
posedCamera=(17.5,-147.3,142.9)
solverEye=(17.5,-147.3,142.9)
posedEye=(17.5,-147.3,142.9)
posedHead=(11.4,-156.2,144.7)
posedChest=(5.4,-163.0,127.4)
correction(raw=2.5 applied=2.5 residual=2.0 headOnly=2.0)
err(cameraToSolverCamera=0.0 cameraToPosedCamera=0.0 solverEyeToPosedEye=0.0 solverHeadToPosedHead=13.6 solverChestToPosedChest=0.4)
ownerView(chestDist=25.1 chestForward=-15.6 chestUp=-15.5)
lean(hmdPitch=-55.2 posedChestHead=21.5 solverChestHead=59.4)
mediapipe(calibrated=0 scale=1.000 nose=(missing) shoulders=(missing))
```

The important part is not the raw numbers alone. The important pattern is:

- The camera and eye are exactly locked: `cameraToSolverCamera=0.0`, `cameraToPosedCamera=0.0`, `solverEyeToPosedEye=0.0`.
- MediaPipe is not calibrated: `calibrated=0`.
- MediaPipe face/shoulder evidence is missing in the HeadLock row.
- The chest remains close enough to the owner view to enter the camera: `chestDist=25.1cm`.
- The solver and final posed head/chest relationship diverge heavily during lean: `solverChestHead=59.4` vs `posedChestHead=21.5`.

Calibration/debug rows during the same run repeatedly reject MediaPipe as unstable:

```text
mp.BodyFusion.Calibration actor=MP_LiveMetaHumanEmory rejected reason="MediaPipe unstable"
mediaPipe=invalid
hasHip=1
hasShoulder=1
observedHeight=104.6
```

Late in the same run, hand/body chain evidence also shows mixed authority:

```text
hmd=fresh
qHandL=fresh
qHandR=fresh
fullChainL=stale
fullChainR=stale
mediaPipe=invalid
```

## Current Diagnosis

The current failure is a body-authority conflict, not a simple camera placement bug.

The HMD camera/eye path is now doing what it was asked to do: it is locked to the solver eye. That is why forward lean can look improved. However, the torso/chest/head chain is not being solved from one coherent tracking authority.

The runtime is effectively mixing these authorities:

- HMD/OpenXR: fresh and authoritative for eyes/head.
- Quest hands: often fresh for hands/fingers/wrists, but can drop out.
- MediaPipe: repeatedly invalid or uncalibrated for body in this run.
- Profile/fallback body solve: still producing chest/pelvis/neck targets when MediaPipe is invalid.
- Owner visibility: hides MetaHuman head/face pieces, but leaves the hoodie/body visible to the local owner view.

That mixed state creates the visible failure:

1. The HMD keeps the camera/eye in the correct XR pose.
2. The body/chest is solved from fallback or stale/non-authoritative body data.
3. The final correction tries to reconcile the head/eye with a body that is not moving in the same authority space.
4. The neck becomes the visible error absorber.
5. The hoodie remains visible because the local owner-view policy does not yet provide a proper first-person body proxy or torso cull for MetaHumans.

MediaPipe is not inherently "wrong", but it is currently being treated too much like absolute body truth. In this VR use case, MediaPipe is a camera-derived pose hint with unstable depth, scale, freshness, and calibration. It should not be allowed to drive torso/pelvis/neck unless it has passed a stable calibration gate.

## What Not To Do Next

Do not fix this by:

- Increasing the camera forward offset.
- Hiding the whole avatar.
- Moving the camera independently of the avatar head.
- Letting invalid MediaPipe keep influencing torso/pelvis/head correction.
- Rewriting the Quest arm solve as a first response.
- Adding Emory-only neck offsets that do not generalize to Manny or future MetaHumans.

Those moves can reduce one screenshot artifact, but they do not fix the authority mismatch.

## Next Correct Engineering Move

The next implementation should introduce an explicit body-authority gate and a no-MediaPipe embodiment mode, then make MediaPipe optional again after it proves stability.

### 1. Add explicit BodyFusion authority state

Add a runtime authority state for the body solve:

```text
NoMediaPipe
MediaPipeCalibrating
MediaPipeStable
MediaPipeRejected
```

Rules:

- HMD/OpenXR always owns the eye/head target.
- Quest/OpenXR owns hands/fingers/wrists when fresh and tracked.
- MediaPipe may only own pelvis/chest/torso after a stable calibration window.
- If MediaPipe is invalid, stale, uncalibrated, or rejected, it must not blend into torso/pelvis/neck.
- Rejected MediaPipe should be visible in logs and debug HUD, not silently blended.

Expected source area:

```text
Source/MediaPipeDriver/MediaPipeBodyFusion.h
Source/MediaPipeDriver/MediaPipeBodyFusion.cpp
Source/MediaPipeDriver/MediaPipePoseDrivenAnimInstance.cpp
Source/MediaPipeDriver/MediaPipePoseDrivenAnimInstance_BodyPoseSolve.inl
Source/MediaPipeDriver/MediaPipeRuntimeCVars.h
Source/MediaPipeDriver/MediaPipeRuntimeCVars.cpp
```

### 2. Make no-MediaPipe embodiment the default debug path

For the next VR Preview proof, run with MediaPipe body authority disabled by default:

```text
HMD/OpenXR -> head/eyes
Quest/OpenXR -> hands/fingers/wrists/arms where tracked
Profile -> neutral torso/pelvis/chest anchor
MediaPipe -> ignored for body until explicitly enabled and stable
```

This matches the current debugging need: prove the Movement-style embodiment architecture first, then reintroduce MediaPipe as an optional body evidence source.

The expected result is not perfect body tracking. The expected result is a stable avatar:

- Camera stays at the avatar eyes.
- Neck does not stretch to absorb body tracking error.
- Hoodie/chest does not enter the owner camera during normal lean.
- Mirror/external view can still show the full avatar.
- Arms and hands remain on the existing Quest path.

### 3. Fix MetaHuman owner-view body policy separately

The latest log proves `localBodyProxy=0` for Emory. That means the local owner is still viewing the real MetaHuman body/hoodie, not an owner-safe first-person body representation.

The fix should be profile-driven and not Emory-only:

```text
Profile LocalViewPolicy:
  owner-hidden components: head/face/hair/teeth/eyes as needed
  owner-safe upper-body policy: proxy, cull, or first-person mesh
  mirror/external policy: full avatar visible
```

For MetaHumans, do not hide the whole avatar. The owner view needs either:

- an owner-only simplified first-person body/proxy, or
- profile-driven owner culling for hoodie/chest regions that can enter the camera,

while mirror/external views keep the complete MetaHuman.

### 4. Replace head-only correction with constrained neck/chest limits

After authority gating is in place, the head/neck/chest solve should enforce profile limits:

- The eye remains the HMD target.
- The head bone follows within the profile's eye-to-head relationship.
- The neck cannot stretch beyond profile limits.
- If correction exceeds the profile limit, the system moves or clamps the body anchor instead of stretching the neck.
- Large residuals should log a warning instead of being hidden by final pose correction.

The solve must be profile-driven so Manny, Emory, and future MetaHumans share the same architecture with different bone names and offsets.

### 5. Add proof to the new method

Add diagnostics that prove the authority decision per frame:

```text
bodyAuthority=NoMediaPipe|MediaPipeCalibrating|MediaPipeStable|MediaPipeRejected
mediaPipeReason=invalid|stale|unstable|heightRejected|stable
cameraEyeErrCm
solverEyeToPosedEyeCm
solverHeadToPosedHeadCm
solverChestToPosedChestCm
ownerChestDistCm
ownerChestForwardCm
ownerChestUpCm
neckStretchCm
localBodyProxyActive
ownerHiddenComponentCount
```

The current logs already prove part of this, but the next method should make the authority state explicit so screenshots can be matched to data without inference.

### 6. Validate in this order

Validation order:

1. Emory no-MediaPipe body authority: HMD eye lock, no neck stretch, no hoodie/chest in normal owner view.
2. Manny no-MediaPipe body authority: same proof, same architecture.
3. Quest hands/arms with MediaPipe body off: fingers, wrists, and arms remain stable.
4. Mirror/external/self-view proof: full avatar visible outside owner camera.
5. Only then enable MediaPipe body authority behind the calibration gate.

MediaPipe should not become default body authority again until VR Preview evidence shows:

- `bodyAuthority=MediaPipeStable`
- `calibrated=1`
- stable observed height
- fresh hip/shoulder/head evidence
- no stale full-chain input
- no large neck residual
- no owner-view torso intrusion

## Current Engineering Decision

The next correct move is to make the embodiment pipeline robust without MediaPipe body authority first.

MediaPipe should be demoted from "body driver" to "optional body evidence". Once the HMD/Quest/profile embodiment path is stable for Manny and Emory, MediaPipe can be reintroduced through an explicit calibration gate. This avoids chasing unstable MediaPipe frames with camera offsets and protects the arm/hand work from unnecessary rewrites.

## Implementation Checkpoint - 2026-05-25

The first authority-gate pass is now implemented in C++.

Changed source files:

```text
Source/MediaPipeDriver/MediaPipeRuntimeCVars.h
Source/MediaPipeDriver/MediaPipeRuntimeCVars.cpp
Source/MediaPipeDriver/MediaPipeBodyFusion.h
Source/MediaPipeDriver/MediaPipeBodyFusion.cpp
Source/MediaPipeDriver/MediaPipePoseDrivenAnimInstance.h
Source/MediaPipeDriver/MediaPipePoseDrivenAnimInstance.cpp
Source/MediaPipeDriver/MediaPipeRuntimeCVarsTests.cpp
Source/MediaPipeDriver/MediaPipeBodyFusionTests.cpp
```

Added runtime controls:

```text
mp.BodyFusion.MediaPipeAuthority
  0 = trace-only, MediaPipe body data is logged but cannot own pelvis/chest/torso/neck
  1 = allow MediaPipe body authority only after stable calibration
  2 = legacy comparison mode, allow calibrated/fresh MediaPipe without the new stability gate

mp.BodyFusion.CalibrationStableFrames
  default 15

mp.BodyFusion.CalibrationHoldSeconds
  default 0.5
```

Default behavior is now `mp.BodyFusion.MediaPipeAuthority=0`. That means MediaPipe can still run and provide debug evidence, but it cannot drive the body pose by default. This is intentional for the next VR proof pass.

Added authority states:

```text
NoMediaPipe
MediaPipeCalibrating
MediaPipeStable
MediaPipeRejected
```

The BodyFusion solve now receives:

```text
bAllowMediaPipePoseAuthority
BodyAuthorityState
```

When authority is disabled or rejected, MediaPipe lower-body and landmark fallback inputs are blocked from promoting into the pose solve. HMD still owns eyes/head, and the existing Quest hand/arm path remains separate.

The runtime logs now include explicit authority proof:

```text
mp.BodyFusion.Debug ... bodyAuthority=... mediaPipeAuthority=... reason="..." stableFrames=... stableSeconds=...
mp.BodyFusion.HeadLock ... bodyAuthority=... mediaPipeAuthority=... reason="..." mediapipe(calibrated=... stableFrames=... stableSeconds=...)
```

This means the next screenshot can be matched to the exact authority state instead of inferred from pose symptoms.

Validation completed:

```text
D:\Epic\UE_5.7\Engine\Build\BatchFiles\Build.bat TestingKit3Editor Win64 Development -Project=D:\Epic\Unreal_Projects\TestingKit3\TestingKit3.uproject -WaitMutex
Result: Succeeded

Saved/Logs/BodyFusionAuthorityAutomation.log
Result: TestingKit3.MediaPipe.BodyFusion passed, 19 tests performed
Includes: TestingKit3.MediaPipe.BodyFusion.TraceOnlyAuthorityBlocksMediaPipePose

Saved/Logs/RuntimeCVarsAutomation.log
Result: TestingKit3.MediaPipe.Runtime.CVars passed, 1 test performed
```

Note: a Live Coding compile completed, but the editor crashed immediately after patch reload in an existing constructor path:

```text
UsesMetaHumanEmbodiedAvatar()
TryBuildActiveEmbodimentProfileForWorld()
AMediaPipeEmbodiedAvatarPawn::AMediaPipeEmbodiedAvatarPawn()
```

The normal UBT build succeeded after the editor was closed. Reopen Unreal before the next VR Preview run.

## Next VR Preview Proof Settings

For the next VR Preview debug pass, use:

```text
mp.BodyFusion.Enable 1
mp.BodyFusion.Debug 1
mp.BodyFusion.MediaPipeAuthority 0
```

This is the intended no-MediaPipe body-authority proof:

```text
HMD/OpenXR -> eyes/head
Quest/OpenXR -> hands/fingers/wrists/arms where fresh
Profile/body solve -> neutral pelvis/chest/torso anchor
MediaPipe -> logged only, no body pose authority
```

Expected log proof:

```text
bodyAuthority=NoMediaPipe
mediaPipeAuthority=0
reason="trace-only"
solverEyeToPosedEye near 0
ownerView chest distance should not collapse during normal lean
neck/chest residual should not grow from MediaPipe correction
```

If that pass is stable for Emory and Manny, test gated MediaPipe authority with:

```text
mp.BodyFusion.MediaPipeAuthority 1
```

Only use this mode after the trace-only path proves that the HMD/Quest/profile embodiment is stable by itself.

## Head/Body Synchronization Checkpoint - 2026-05-25

The owner-view body/proxy path was inspected but intentionally not changed. Hiding or replacing the embodied body would defeat the embodiment goal. The avatar body should remain visible to the wearer; the fix must keep the body synchronized with the HMD/eyes instead.

The concrete mismatch found in the prior HeadLock data was:

```text
solverEyeToPosedEye=0.0
solverChestToPosedChest=0.4
posedChestHead=21.5
solverChestHead=59.4
```

That means the eye lock and chest target were individually close, but the final posed chest-to-head lean did not match the fused solver's chest-to-head lean. The pose writer was keeping torso pitch too profile-stable for the embodied hips-only authority, so physical HMD head translation was not being carried through the torso chain enough.

Implemented change:

```text
Source/MediaPipeDriver/MediaPipeBodyFusion.cpp
  FMediaPipeAvatarPoseWriter::BuildDefaultWritePlan now always sets:
  bInferTorsoPitchFromHeadTranslation = true

Source/MediaPipeDriver/MediaPipePoseDrivenAnimInstance.cpp
  mp.BodyFusion.HeadLock logs now include:
  torsoPitchFromHead=0|1

Source/MediaPipeDriver/MediaPipeBodyFusionTests.cpp
  Updated the embodied hips-only write-plan expectation.
```

This does not hide the torso, does not use a body proxy for MetaHumans, does not change the arm path, and does not enable MediaPipe body authority by default.

Validation:

```text
D:\Epic\UE_5.7\Engine\Build\BatchFiles\Build.bat TestingKit3Editor Win64 Development -Project=D:\Epic\Unreal_Projects\TestingKit3\TestingKit3.uproject -WaitMutex
Result: Succeeded

Saved/Logs/BodyFusionTorsoPitchAutomation.log
Result: TestingKit3.MediaPipe.BodyFusion passed, 19 tests performed
```

For the next VR Preview proof, inspect the `mp.BodyFusion.HeadLock` rows while leaning forward/back:

```text
bodyAuthority=NoMediaPipe
mediaPipeAuthority=0
reason="trace-only"
torsoPitchFromHead=1
solverEyeToPosedEye near 0
solverChestToPosedChest remains small
posedChestHead should move closer to solverChestHead than the previous 21.5 vs 59.4 mismatch
ownerView(chestDist/chestForward/chestUp) should no longer collapse when leaning
```

If hoodie/body still enters the camera after this, the next fix should be a constrained profile-driven neck/chest limit or body anchor adjustment. It should still keep the wearer-visible avatar body, not hide it.

## Forward Bend Torso Pitch Clamp - 2026-05-25

The next VR Preview showed the earlier problem reversed: backward lean looked acceptable, but forward bend pulled the hoodie/body into the owner camera. The bad HeadLock row proved the camera/eye was still correct while the final posed torso was not:

```text
bodyAuthority=NoMediaPipe
mediaPipeAuthority=0
reason="trace-only"
solverEyeToPosedEye=0.0
cameraToPosedCamera=0.0
solverPelvisChest=6.0
solverChestHead=73.4
posedPelvisChest=41.9
posedChestHead=-27.0
solverChestToPosedChest=23.7
ownerView(chestDist=23.8 chestForward=0.0 chestUp=-23.8)
```

So MediaPipe was not driving the body. The writer was taking the large HMD head-to-chest vector and using it directly as the chest basis when `torsoPitchFromHead=1`. In that frame the raw head-driven pitch was about 73 degrees forward while the solved pelvis-to-chest bridge was only about 6 degrees, so the spine over-rotated toward the HMD head vector.

Implemented change:

```text
Source/MediaPipeDriver/MediaPipeAvatarEmbodimentProfile.h
  Added profile limits:
  MaxForwardTorsoPitchFromHeadTranslationDeg = 18
  MaxBackwardTorsoPitchFromHeadTranslationDeg = 28

Source/MediaPipeDriver/MediaPipeBodyFusion.h/.cpp
  Added FMediaPipeAvatarPoseWriter::ConstrainTorsoPitchFromHeadTranslation.
  It keeps the solved pelvis-to-chest bridge as the base and clamps only the HMD head-translation pitch delta.

Source/MediaPipeDriver/MediaPipePoseDrivenAnimInstance.cpp
  DriveBodyFusionPoseCS now uses the constrained chest-up vector.
  HeadLock logs now include:
  torsoPitch(raw=... applied=... clamped=... maxF=... maxB=...)

Source/MediaPipeDriver/MediaPipeBodyFusionTests.cpp
  Added TestingKit3.MediaPipe.BodyFusion.TorsoPitchConstraint.
  The test covers the 73 degree forward case from the VR data, the separate backward limit, and an unclamped small movement.
```

Validation:

```text
D:\Epic\UE_5.7\Engine\Build\BatchFiles\Build.bat TestingKit3Editor Win64 Development -Project=D:\Epic\Unreal_Projects\TestingKit3\TestingKit3.uproject -WaitMutex -NoHotReloadFromIDE -NoUBA
Result: Succeeded

TestingKit3.MediaPipe.BodyFusion.TorsoPitchConstraint
Result: Success

TestingKit3.MediaPipe.BodyFusion
Found 20 automation tests
Result: Success, exit code 0

TestingKit3.MediaPipe.AvatarEmbodiment
Found 4 automation tests
Result: Success, exit code 0
```

For the next VR Preview proof, use `mp.BodyFusion.Debug 1` and inspect `mp.BodyFusion.HeadLock` around the forward-bend screenshot. In the formerly bad case, expect something like:

```text
torsoPitch(raw=~73 applied=18 clamped=1 maxF=18 maxB=28)
```

If the hoodie still enters view while the clamp is active, the next data point is whether `solverChestToPosedChest` and `ownerView(chestForward/chestUp)` remain too small. That would mean the remaining error is in post-spine head/neck correction, not MediaPipe authority and not the raw camera.

## Side Swivel / Neck Mismatch Evidence Gap - 2026-05-25

The VR Preview run in `Saved/QuestScreenshots/vrpreview_quest_mirror_20260525_164723` captured two useful visual failures:

```text
Frame 006: 2026-05-25T16:47:39.7939897+08:00
  Neck distortion visible in mirror.

Frame 015: 2026-05-25T16:48:05.5577185+08:00
  Side swivel brings hoodie/body back into owner view.
```

The Unreal log for that run did not contain `mp.BodyFusion.Debug`, `mp.BodyFusion.HeadLock`, or `mp.BodyFusion.HeadLockSide` rows for those timestamps. The evidence capture manifest said `unreal_diagnostics applied=true`, but the capture script only enabled Quest wrist/finger/arm diagnostics; it did not enable `mp.BodyFusion.Debug`. That means the screenshots identify the visual timing, but the exact solver-side moment cannot be proven from that run's body data.

Implemented diagnostic fix:

```text
Tools/StartQuestMirrorEvidenceCapture.ps1
  Enables mp.BodyFusion.Debug 1 during capture.
  Reads back BodyFusion CVars in the capture log.

Source/MediaPipeDriver/MediaPipePoseDrivenAnimInstance.cpp
  Adds mp.BodyFusion.HeadLockSide rows while BodyFusion debug is enabled.
  Logs HMD pitch/yaw/roll, HMD yaw relative to avatar forward, owner-view side offsets,
  posed-vs-solver side lean through pelvis/chest/neck/head, neck solve error, and
  head-lock correction direction.
```

Current suspected issue from code inspection: the MetaHuman path preserves reference neck/head translations to avoid neck stretching, then applies HMD rotation and eye-lock translation correction over neck, neck_02, and head. During side lean or yaw/roll swivel, that can leave the body/chest and the head-lock correction disagreeing laterally. The next proof run should compare these fields:

```text
mp.BodyFusion.HeadLockSide
  hmdRot(... roll=... yawFromAvatar=...)
  ownerView(chestRight=... headRight=...)
  sideLean(posedChestNeck=... posedNeckHead=... solverChestNeck=... solverNeckHead=...)
  neck(solverToPosed=... right=... forward=... up=...)
  correction(... right=... residualRight=...)
```

If the bad swivel frame shows large `neck.right`, `ownerView.chestRight`, or correction `right/residualRight` while the forward/backward pitch clamp is behaving, the next fix should be a profile-driven lateral neck/head correction distribution. It should not hide the torso and should not disable the wearer-visible body.

## Side Swivel / Neck Root Cause - 2026-05-25

The next VR Preview run did include BodyFusion debug data:

```text
Saved/QuestScreenshots/vrpreview_quest_mirror_20260525_183200

Frame 009: 2026-05-25T18:32:25.2308024+08:00
  User-visible issue: body/hoodie enters owner view during side swivel.

Frame 013: 2026-05-25T18:32:36.5897451+08:00
  User-visible issue: mirror movement improved, but neck still visibly distorted.
```

Frame 009 proved the swivel was not a large HMD yaw spike:

```text
actor=MP_LiveMetaHumanEmory
bodyAuthority=NoMediaPipe
mediaPipeAuthority=0
reason="trace-only"
hmdRot(pitch=-11.4 yaw=83.8 roll=4.5 yawFromAvatar=-6.2)
hmdPlanar(offset=13.6 clamp=0.0 clamped=0)
solverEyeToPosedEye=0.0
solverHeadToPosedHead=8.7
solverChestToPosedChest=10.1
ownerView(chestDist=23.4 chestForward=3.2 chestRight=-5.6 chestUp=-22.5)
sideLean(posedChestNeck=2.1 posedNeckHead=36.0 solverChestNeck=18.5 solverNeckHead=18.5)
neck(solverToPosed=7.9 right=-3.7 forward=-6.7 up=-2.0)
correction(raw=13.7 residual=11.0 headOnly=11.0)
```

Frame 013 showed the same root pattern:

```text
actor=MP_LiveMetaHumanEmory
hmdRot(pitch=-7.5 yaw=84.3 roll=2.9 yawFromAvatar=-5.7)
hmdPlanar(offset=14.0 clamp=0.0 clamped=0)
solverEyeToPosedEye=0.0
solverHeadToPosedHead=8.8
solverChestToPosedChest=10.1
ownerView(chestDist=25.9 chestForward=0.3 chestRight=-5.1 chestUp=-25.4)
sideLean(posedChestNeck=3.2 posedNeckHead=26.3 solverChestNeck=15.5 solverNeckHead=15.5)
neck(solverToPosed=10.0 right=-3.1 forward=-8.8 up=-3.8)
correction(raw=10.0 residual=8.0 headOnly=8.0)
```

Diagnosis:

- HMD eye lock is working: `solverEyeToPosedEye=0.0`.
- MediaPipe body authority is off: `bodyAuthority=NoMediaPipe`, `mediaPipeAuthority=0`.
- The bad swivel is not a hidden raw yaw event: `yawFromAvatar` stays around `-6 deg`.
- The visible failure is the final posed MetaHuman neck/chest chain disagreeing with the fused solver:
  - chest pose error is about `10 cm`;
  - solver neck to posed neck error is `7.9-10.0 cm`;
  - the solver asks the neck chain for about `15-18 deg` of side lean, but the posed lower neck only takes `2-3 deg`;
  - the rest gets dumped into `posedNeckHead` and final head-only correction.

Root cause:

```text
Source/MediaPipeDriver/MediaPipePoseDrivenAnimInstance.cpp
```

The MetaHuman path used `preserveNeckTranslations=1`. That kept neck and neck_02 near their reference component-space translations to avoid the earlier long-neck stretch, then forced the final eye match through partial neck correction plus a head-only residual. The result was exact HMD eye position with a neck/body chain that did not move with the solved body.

Implemented narrow fix:

```text
Source/MediaPipeDriver/MediaPipeAvatarEmbodimentProfile.h
Source/MediaPipeDriver/MediaPipeAvatarEmbodimentProfile.cpp
Source/MediaPipeDriver/MediaPipePoseDrivenAnimInstance.cpp
Source/MediaPipeDriver/MediaPipeAvatarEmbodimentProfileTests.cpp
```

Change:

- Added profile-driven bounded neck translation follow:
  - `NeckTranslationFollowAlpha`
  - `NeckTranslationMaxDeltaCm`
  - `Neck02TranslationFollowAlpha`
  - `Neck02TranslationMaxDeltaCm`
- MetaHuman profiles now derive bounded follow limits from `ExpectedHeadToChestCm`.
- The MetaHuman pose writer now lets neck and neck_02 follow the fused solver by a limited amount before applying HMD rotation.
- Manny-like avatars keep their existing direct body-fusion translation path.

This is intentionally not a camera offset, not a body hide, not an arm rewrite, and not an Emory-only offset. The fix keeps full wearer-visible embodiment while preventing the solved neck error from being absorbed entirely by the head.

Validation:

```text
D:\Epic\UE_5.7\Engine\Build\BatchFiles\Build.bat TestingKit3Editor Win64 Development -Project=D:\Epic\Unreal_Projects\TestingKit3\TestingKit3.uproject -WaitMutex -NoHotReloadFromIDE -NoUBA
Result: Succeeded

D:\Epic\Unreal_Projects\TestingKit3\Saved\Logs\MetaHumanNeckFollowAutomation.log
TestingKit3.MediaPipe.AvatarEmbodiment.MetaHumanProfileSolve
Result: Success
```

Next VR Preview proof target:

```text
mp.BodyFusion.Debug 1
mp.BodyFusion.MediaPipeAuthority 0
```

In the next bad-or-good frame, compare against the old values above. The desired data movement is:

```text
solverEyeToPosedEye remains near 0
solverChestToPosedChest decreases from about 10 cm
neck(solverToPosed) decreases from 8-10 cm
correction(headOnly) decreases from 8-11 cm
sideLean(posedChestNeck) moves closer to solverChestNeck
ownerView chestForward/chestRight stop collapsing into the camera during side lean
```

## Failed Retest After Neck Follow - 2026-05-25 19:29

The next user VR Preview still showed the same visible failure:

```text
Saved/QuestScreenshots/vrpreview_quest_mirror_20260525_192922

Frame 009: 2026-05-25T19:29:47.1202988+08:00
  Owner view still sees hoodie/body during lean.

Frame 010: 2026-05-25T19:29:49.9568619+08:00
  Side swivel/lean still puts the embodied body in front of the camera.
```

The data showed that the bounded MetaHuman neck-follow change was active but incomplete:

```text
Frame 009:
  bodyAuthority=NoMediaPipe
  mediaPipeAuthority=0
  reason="trace-only"
  hmdYaw=84.2
  yawFromAvatar=-5.8
  hmdPlanar(offset=13.5 clamp=0.0 clamped=0)
  solverEyeToPosedEye=0.0
  neck(solverToPosed)=3.6-4.0
  correction(raw=11.8-12.0 residual=9.4-9.6 headOnly=9.4-9.6)
  posedChestHead about -26 deg while solverChestHead was about -5 deg

Frame 010:
  bodyAuthority=NoMediaPipe
  mediaPipeAuthority=0
  reason="trace-only"
  yawFromAvatar about -0.6 to -4.0
  hmdPlanar(offset=16.6-16.8 clamp=0.0 clamped=0)
  solverEyeToPosedEye=0.0
  solverChestToPosedChest about 10.1 cm
  neck(solverToPosed)=5.4-5.5
  ownerView(chestForward about 6.3 cm, chestRight about -5.2 cm, chestUp about -19.5 cm)
  correction(raw=17.5 applied=17.5 residual=14.0 headOnly=14.0)
```

Diagnosis:

- The previous fix reduced `neck(solverToPosed)` from the earlier `8-10 cm` range, so it was not a no-op.
- The visual issue remained because the final HMD eye-lock residual was still applied only to the head.
- That produced exact `solverEyeToPosedEye=0.0`, but left the head/neck/chest chain visibly inconsistent.
- The failed retest is therefore not evidence for more camera offset. It is evidence that the final residual correction must stop being head-only on MetaHumans.

Implemented follow-up fix:

```text
Source/MediaPipeDriver/MediaPipePoseDrivenAnimInstance.cpp
```

Change:

- MetaHuman final eye-lock residual is now distributed through the neck chain:
  - `neck`: 55 percent of residual translation
  - `neck_02`: 85 percent of residual translation
  - `head`: 100 percent of residual translation
- Manny/non-MetaHuman avatars keep the previous head-only residual path for now.
- The HeadLock debug row now includes:

```text
correction(raw=... applied=... residual=... headOnly=... residualMode=chain|head)
```

Expected proof in the next VR Preview:

```text
actor=MP_LiveMetaHumanEmory
correction(... residual=... headOnly=0.0 residualMode=chain)
solverEyeToPosedEye=0.0
```

If the visual problem still remains while `residualMode=chain` and `headOnly=0.0`, then the remaining root cause is not final head-only residual. The next suspect would be the body/chest anchor or wearer-view body geometry, and the logs should be read from `ownerView(...)`, `solverChestToPosedChest`, and the chest/head lean deltas instead of changing the camera.

Validation:

```text
D:\Epic\UE_5.7\Engine\Build\BatchFiles\Build.bat TestingKit3Editor Win64 Development -Project=D:\Epic\Unreal_Projects\TestingKit3\TestingKit3.uproject -WaitMutex -NoHotReloadFromIDE -NoUBA
Result: Succeeded

D:\Epic\Unreal_Projects\TestingKit3\Binaries\Win64\UnrealEditor-MediaPipeDriver.dll
LastWriteTime: 2026-05-25 19:49:09

D:\Epic\Unreal_Projects\TestingKit3\Saved\Logs\MetaHumanResidualChainAutomation.log
TestingKit3.MediaPipe.AvatarEmbodiment.MetaHumanProfileSolve
Result: Success
Exit code: 0
```

## Residual Chain Retest - 2026-05-25 20:13

The next VR Preview showed that `residualMode=chain` was active, but it exposed a new invariant failure:

```text
Saved/QuestScreenshots/vrpreview_quest_mirror_20260525_201342

Representative rows:
  actor=MP_LiveMetaHumanEmory
  residualMode=chain
  headOnly=0.0
  solverEyeToPosedEye=8.0-11.2 cm
  cameraToPosedCamera=8.0-11.2 cm
  solverChestToPosedChest=0.4 cm during the steady backward-lean/pivot rows
```

Diagnosis:

- The chain residual change did run.
- It improved the "all residual dumped into head" problem, but it violated the HMD/eye lock invariant.
- For embodied VR, the HMD/solver eye must remain the final authority. The avatar head can be corrected through neck participation, but the final posed eye must still land on the solver eye.
- The remaining visible stretch/pivot is now consistent with a head that is several centimeters away from the HMD even though the body/chest is close to its solver target.

Follow-up patch on disk:

```text
Source/MediaPipeDriver/MediaPipePoseDrivenAnimInstance.cpp
```

Change:

- MetaHuman residual correction still distributes residual to `neck` and `neck_02`.
- After that chain step, it recomputes the posed eye and applies one final exact head correction.
- The expected log mode is now:

```text
residualMode=chain+head
solverEyeToPosedEye near 0.0
```

## MetaHuman Body Replay Spine Translation Fix - 2026-05-26

User request:

```text
Stop using repeated VR Preview as the test loop. Use the last VR stay recording
of HMD/head and hand motion, then build against that known motion.
```

Replay source:

```text
Saved/Logs/TestingKit3.log
Saved/QuestScreenshots/vrpreview_quest_mirror_20260526_113828
Saved/CodexAgent/BodyReplay/metahuman_emory_vr_20260526_113828_headlock.csv
Tools/AnalyzeMetaHumanBodyReplayLog.ps1
```

Replay extraction result for actor `MP_LiveMetaHumanEmory`:

```text
HeadLock rows: 821
Quest wrist rows: 835
Tracked wrist rows: 724
Active full-arm-chain rows: 1514
Average chest solve-to-posed mismatch: 6.525 cm
Average pelvis-to-chest lean error: 9.548 deg
Maximum pelvis-to-chest lean error: 10.1 deg
```

Root cause from the replay rows:

```text
The BodyFusion solver was not fully locked. The log shows solveChest/solvePelvis
moving from the recorded HMD and arm state.

The pelvis write was also reaching the posed skeleton: posedPelvis matched the
solver pelvis.

The failure was the MetaHuman spine/chest pose write. The code rotated the spine
bones toward the solved basis, but it did not translate the spine/chest bones
toward the solved chest line. Because component-space rotation does not move the
spine bone translation, the visible chest stayed nearly profile-pinned while the
head and arms moved.
```

Corrective patch:

```text
Source/MediaPipeDriver/MediaPipePoseDrivenAnimInstance.h
- Caches reference component-space translations for each valid spine bone.

Source/MediaPipeDriver/MediaPipePoseDrivenAnimInstance_ReferenceCache.inl
- Fills RefSpineTranslationComp during reference-pose cache build.

Source/MediaPipeDriver/MediaPipePoseDrivenAnimInstance.cpp
- Adds ApplyBodyFusionSpineTranslationTargets for MetaHuman BodyFusion writes.
- Moves each valid spine bone toward the solved pelvis-to-chest line before
  applying the existing spine rotations.
- Bounds the top-chest translation delta by the avatar profile chest-to-pelvis
  size, currently clamped to 14-32 cm, so the body follows without unbounded neck
  or chest stretch.
```

Validation:

```text
D:\Epic\UE_5.7\Engine\Build\BatchFiles\Build.bat TestingKit3Editor Win64 Development -Project=D:\Epic\Unreal_Projects\TestingKit3\TestingKit3.uproject -WaitMutex -NoHotReloadFromIDE -NoUBA
Result: Succeeded

Saved\Logs\MetaHumanBodyReplaySpineTranslationAutomation.log
TestingKit3.MediaPipe.AvatarEmbodiment.MetaHumanProfileSolve: Success
TestingKit3.MediaPipe.BodyFusion: Success, 20 tests performed
TestingKit3.MediaPipe.PoseDrivenSolverState.Body.Reset: Success

Tools/AnalyzeMetaHumanBodyReplayLog.ps1 generated:
Saved/CodexAgent/BodyReplay/metahuman_emory_vr_20260526_113828_headlock.csv
```

Deterministic replay gate added:

```text
Tools/TestMetaHumanBodyReplayExpectations.ps1
Saved/CodexAgent/BodyReplay/metahuman_emory_vr_20260526_113828_replay_gate.json
```

Replay gate result:

```text
Rows tested: 821
Baseline average chest solve-to-posed mismatch: 6.525 cm
Baseline average pelvis-to-chest lean error: 9.548 deg
Baseline captured body-lock failure: true
Current spine/chest translation clamp: 26.1 cm
Rows within clamp: 821
Rows exceeding clamp: 0
Projected post-fix average chest mismatch for this recording: 0.0 cm
Projected post-fix maximum chest mismatch for this recording: 0.0 cm
Pass: true
```

Meaning:

```text
The last VR recording is now a reusable baseline. It is not only a diagnosis
log. It proves the old failure is in the recording and gives a pass/fail check
for whether the current body write path can close the recorded chest/body gap
without asking the user to repeat VR Preview for each code iteration.
```

Expected next headset proof from a future VR run:

```text
mp.BodyFusion.HeadLock actor=MP_LiveMetaHumanEmory ...
solverChestToPosedChest should be materially lower than the recorded 6.5 cm
posedPelvisChest should move toward solverPelvisChest instead of staying near 2 deg
```

## MetaHuman HMD Torso Hard-Gate Removal - 2026-05-26

Fresh VR Preview log after the first replay/spine patch still showed the user-visible
failure:

```text
Saved/Logs/TestingKit3.log
VR Preview start: 2026-05-26 12:06:44 Australia/Perth
Actor: MP_LiveMetaHumanEmory

HMD span: X 35.5 cm, Y 53.9 cm, Z 30.5 cm
Solved chest span: X 19.4 cm, Y 34.5 cm, Z 4.3 cm
Average solver chest-to-posed chest mismatch: 8.681 cm
Average pelvis-to-chest lean error: 12.901 deg
```

Conclusion:

```text
The solve was still hard-gating torso motion. In particular, vertical HMD/head
movement was almost completely removed before pose writing. The old embodied
MetaHuman path used a head-locked triangle/length solve, then the pose writer
clamped torso pitch to the narrow default 18/28 deg profile limits.
```

Corrective patch:

```text
Source/MediaPipeDriver/MediaPipeBodyFusion.cpp
- Replaced the embodied-hips-only MetaHuman HMD fallback triangle solve with a
  direct shared upper-body basis from HMD displacement.
- Chest follows HMD planar and vertical movement directly.
- Pelvis follows the solved upper-body delta in both planar and vertical axes
  when reliable MediaPipe pelvis authority is not active.
- Quest shoulder evidence can still blend in when fresh, but stale full-arm
  chains no longer block HMD-driven torso motion.

Source/MediaPipeDriver/MediaPipeAvatarEmbodimentProfile.h
- Raised default translation-driven torso pitch limits from 18/28 deg to 60/60 deg.

Source/MediaPipeDriver/MediaPipePoseDrivenAnimInstance.cpp
- Added a final explicit MetaHuman top-spine/chest translation pass after
  head-lock correction so the posed chest is not left profile-pinned.

Source/MediaPipeDriver/MediaPipeBodyFusionTests.cpp
- Updated tests to assert HMD-driven torso movement instead of asserting fixed
  head/chest and chest/pelvis profile lengths.
- Added vertical HMD movement coverage for MetaHuman HMD-only fallback.
```

Validation:

```text
D:\Epic\UE_5.7\Engine\Build\BatchFiles\Build.bat TestingKit3Editor Win64 Development -Project=D:\Epic\Unreal_Projects\TestingKit3\TestingKit3.uproject -WaitMutex -NoHotReloadFromIDE -NoUBA
Result: Succeeded

Saved/Logs/MetaHumanBodyHardGateAutomation.log
Automation Test Queue Empty 22 tests performed.
TestingKit3.MediaPipe.BodyFusion: Success
TestingKit3.MediaPipe.AvatarEmbodiment.MetaHumanProfileSolve: Success
TestingKit3.MediaPipe.PoseDrivenSolverState.Body.Reset: Success
```

Expected next headset proof:

```text
In the next VR Preview log, solved chest Z should no longer stay near a 4 cm
span while HMD Z moves around 30 cm. The HeadLock row should show materially
lower solverChestToPosedChest and posedPelvisChest should track solverPelvisChest
more closely.
```

## Shared Upper-Body Basis / Fused Pelvis Follow - 2026-05-26

Correction after the follow-up diagnosis:

```text
The head and arms were already direct tracking targets, but the body solve was
still treating the torso as mostly profile/HMD fallback. That meant the arms
could move correctly while the chest and pelvis only made small, disconnected
adjustments.

Expected behavior:
- HMD/head translation contributes to the same upper-body basis as the arms
- fresh Quest shoulder chain evidence can pull the fused chest target
- when MediaPipe body authority is disabled or unavailable, the pelvis can still
  follow the fused upper-body delta instead of remaining profile-pinned
- MediaPipe still does not own the pelvis unless mp.BodyFusion.MediaPipeAuthority
  is explicitly enabled and calibration/lower-body freshness are reliable
```

Corrective patch:

```text
Source/MediaPipeDriver/MediaPipeBodyFusion.cpp
- Added a Quest shoulder-driven preferred chest target using the fresh left/right
  full-arm-chain shoulder points.
- Blends that Quest shoulder chest target into the embodied hips-only chest solve.
- Applies a bounded planar correction so arm/shoulder evidence still affects the
  chest when the HMD/head chain is near its length limits.
- Derives a bounded pelvis follow delta from the solved upper-body planar motion
  when reliable MediaPipe pelvis authority is not active.
- Marks that pelvis owner as Fused instead of AvatarProfile when the upper-body
  follow path moves it.

Source/MediaPipeDriver/MediaPipePoseDrivenAnimInstance.cpp
- DriveBodyFusionPoseCS now applies pelvis translation for Fused pelvis ownership,
  not only MediaPipe pelvis ownership.

Source/MediaPipeDriver/MediaPipeBodyFusionTests.cpp
- Updated trace-only authority expectations so trace-only mode blocks MediaPipe
  pelvis ownership but can still permit fused HMD/upper-body pelvis follow.
- Added SourceOwnerTags coverage for HMD-only backward lean, HMD-only forward
  lean, and Quest shoulder-chain-driven chest/pelvis motion.
```

Validation:

```text
D:\Epic\UE_5.7\Engine\Build\BatchFiles\Build.bat TestingKit3Editor Win64 Development -Project=D:\Epic\Unreal_Projects\TestingKit3\TestingKit3.uproject -WaitMutex -NoHotReloadFromIDE -NoUBA
Result: Succeeded

Saved\Logs\SharedUpperBodyBasisBodyFusionAutomation.log
TestingKit3.MediaPipe.BodyFusion.EmbodiedHipsOnlyTorsoAnchor: Success
TestingKit3.MediaPipe.BodyFusion.FirstPersonTorsoVisibility: Success
TestingKit3.MediaPipe.BodyFusion.SourceOwnerTags: Success
TestingKit3.MediaPipe.BodyFusion.TorsoPitchConstraint: Success
TestingKit3.MediaPipe.BodyFusion.TraceOnlyAuthorityBlocksMediaPipePose: Success

Saved\Logs\SharedUpperBodyBasisAvatarEmbodimentAutomation.log
TestingKit3.MediaPipe.AvatarEmbodiment.MetaHumanProfileSolve: Success

Saved\Logs\SharedUpperBodyBasisAllBodyFusionAutomation.log
TestingKit3.MediaPipe.BodyFusion: Success, 20 tests performed
```

Remaining proof gap:

```text
This is source/build/automation proof only. It has not yet been accepted by a
live worn-headset VR Preview/Oculus Mirror pass in
/Game/MetaHumanRooms/L_MetaHumanPreviewRoom_02.

Next proof should check that forward/back lean moves the MetaHuman torso and
pelvis together under the HMD while the head stays eye-locked and the owner view
does not see the chest/neck interior.
```

Build status:

```text
D:\Epic\UE_5.7\Engine\Build\BatchFiles\Build.bat TestingKit3Editor Win64 Development -Project=D:\Epic\Unreal_Projects\TestingKit3\TestingKit3.uproject -WaitMutex -NoHotReloadFromIDE -NoUBA
Initial attempt:
  Compile: succeeded
  Link: failed
  Reason: UnrealEditor-MediaPipeDriver.dll was locked by the running editor

After closing Unreal Editor:
  Result: Succeeded
  D:\Epic\Unreal_Projects\TestingKit3\Binaries\Win64\UnrealEditor-MediaPipeDriver.dll
  LastWriteTime: 2026-05-25 20:40:17

D:\Epic\Unreal_Projects\TestingKit3\Saved\Logs\MetaHumanChainHeadAutomation.log
TestingKit3.MediaPipe.AvatarEmbodiment.MetaHumanProfileSolve
Result: Success
Exit code: 0
```

Next VR Preview proof target:

```text
actor=MP_LiveMetaHumanEmory
residualMode=chain+head
solverEyeToPosedEye near 0.0
```

If the wearer still sees neck stretch or backward pivot after this proof line is present, continue from mirror-side reflection and body/chest anchor analysis. Do not go back to camera offsets as the first move.

## Mirror Reflection / Neck Rollback - 2026-05-25 21:33

The follow-up VR Preview showed that `chain+head` restored eye lock but made the visible MetaHuman neck worse. It also did not fix the user's mirror-side report: leaning left still displayed as the front avatar leaning to its own left instead of its anatomical right.

Root cause found in code:

```text
Source/MediaPipeDriver/MediaPipeDriver.cpp
UpdateMetaHumanSelfViewAvatar
```

The MetaHuman self-view actor was a leader-pose copy facing the wearer:

```text
TargetComponent->SetLeaderPoseComponent(SourceBodyComponent, true, true)
```

That is not a true mirror/reflection. It copies the same component-space pose and rotates the clone to face the wearer, so anatomical side lean can display with the wrong mirror sign.

Corrective patch:

```text
Source/MediaPipeDriver/MediaPipeDriver.cpp
- Added MakeMetaHumanSelfViewMirrorScale.
- MetaHuman self-view now applies a negative local lateral scale:
  - Y-forward MetaHuman profiles mirror on local X.
  - X-forward profiles mirror on local Y.
- Runtime log now includes sourceScale, mirrorScale, and mirrorAxis.

Source/MediaPipeDriver/MediaPipePoseDrivenAnimInstance.cpp
- Removed the MetaHuman residual chain+head distribution.
- Residual eye lock is back to final head-only correction after the normal bounded neck follow.
- Expected runtime line is residualMode=head, not residualMode=chain+head.
```

Build and automation:

```text
D:\Epic\UE_5.7\Engine\Build\BatchFiles\Build.bat TestingKit3Editor Win64 Development -Project=D:\Epic\Unreal_Projects\TestingKit3\TestingKit3.uproject -WaitMutex -NoHotReloadFromIDE -NoUBA
Result: Succeeded

D:\Epic\Unreal_Projects\TestingKit3\Binaries\Win64\UnrealEditor-MediaPipeDriver.dll
LastWriteTime: 2026-05-25 21:17:55

D:\Epic\Unreal_Projects\TestingKit3\Saved\Logs\MetaHumanMirrorScaleAvatarEmbodimentSuite.log
TestingKit3.MediaPipe.AvatarEmbodiment.LocalViewPolicy: Success
TestingKit3.MediaPipe.AvatarEmbodiment.MannyCameraAnchoredSolve: Success
TestingKit3.MediaPipe.AvatarEmbodiment.MetaHumanProfileSolve: Success
TestingKit3.MediaPipe.AvatarEmbodiment.QuestHmdRelativeWristMap: Success
```

Next VR Preview proof target:

```text
Placed embodied pawn: MetaHuman self-view enabled ... mirrorScale=... mirrorAxis=X
mp.BodyFusion.HeadLock ... residualMode=head ... solverEyeToPosedEye near 0.0
```

If the mirror lean is still reversed, do not tune camera offsets. Check the emitted `mirrorAxis` first and swap the reflected local lateral axis for the active profile.

## AutoQuest MetaHuman Body Root Follow - 2026-05-26

Issue being addressed:

```text
In /Game/MetaHumanRooms/L_MetaHumanPreviewRoom_02, VR Preview head and hands move,
but the MetaHuman body can remain pinned at the station. Lean/back fallback then
has to absorb the HMD delta through head/neck/chest solve, which is the path that
previously exposed chest interior and swivel artifacts.
```

Reference pattern checked:

```text
C:\Users\Alan\OneDrive\Documents\Unreal Projects\Unreal-Movement
OculusXRRetargetSkeleton.cpp uses CombineToRoot to move excess hip motion/yaw
into the root instead of leaving all displacement in local skeletal compensation.
```

Corrective patch:

```text
Source/MediaPipeDriver/MediaPipeDriver.cpp
- Added stable AutoQuest body-root follow state to FAutoQuestStationRefreshState.
- Added ApplyStableEmbodiedBodyRootFollow for live AutoQuest station actors.
- Reapplies persisted body-root offset during station refresh.
- Moves Station.MannyLocation and Station.AvatarEyeWorld horizontally toward the HMD-derived avatar root.
- Does not move CameraLocation, ViewerLocation, the camera pawn root, or raw vertical HMD Z.
- Keeps mp.BodyFusion.MediaPipeAuthority at 0 by default; MediaPipe can debug/trace but not drive body pose unless explicitly enabled.
```

Expected runtime proof line during real VR Preview with HMD pose:

```text
Auto Quest embodied: body-root follow applied ... cameraPawnMoved=0
```

Build and local validation:

```text
D:\Epic\UE_5.7\Engine\Build\BatchFiles\Build.bat TestingKit3Editor Win64 Development -Project=D:\Epic\Unreal_Projects\TestingKit3\TestingKit3.uproject -WaitMutex -NoHotReloadFromIDE -NoUBA
Result: Succeeded

Saved\Logs\AutoQuestBodyRootFollowQuestHmdRelativeWristMapAutomation.log
TestingKit3.MediaPipe.AvatarEmbodiment.QuestHmdRelativeWristMap: Success

Saved\Logs\AutoQuestBodyRootFollowTraceOnlyAuthorityAutomation.log
TestingKit3.MediaPipe.BodyFusion.TraceOnlyAuthorityBlocksMediaPipePose: Success

Normal PIE in /Game/MetaHumanRooms/L_MetaHumanPreviewRoom_02 spawned:
- MP_PlacedEmbodiedMetaHumanPawn
- MP_LiveMediaPipeManny
- MP_LiveMetaHumanEmory
- MP_SelfViewMetaHumanEmory

Runtime CVar state observed in PIE:
mp.AutoQuestEmbodiedBodyRootFollow=1
mp.BodyFusion.Enable=1
mp.BodyFusion.MediaPipeAuthority=0
```

Remaining proof gap:

```text
The hidden editor could run normal PIE, but did not provide a useful visual capture
and had no HMD pose in PIE. The actual acceptance check is still a live VR Preview
or Oculus Mirror pass where leaning/walking produces the body-root follow log above
and the first-person view does not show chest/neck interior.
```

## MetaHuman HMD-Only Forward/Backward Lean Fallback - 2026-05-26

Correction after user clarification:

```text
The body-root follow patch above addresses horizontal room-scale/root movement.
It does not by itself satisfy the lean contract.

Expected behavior:
- leaning forward should pitch/lean the avatar torso forward
- leaning backward should pitch/lean the avatar torso backward
- the head should remain locked to the HMD/eye target
- this must work even when MediaPipe body authority is disabled or rejected
```

Root cause found:

```text
Source/MediaPipeDriver/MediaPipeBodyFusion.cpp
FMediaPipeAvatarPoseWriter::ShouldSolveChestFromHeadTranslation
FMediaPipeAvatarPoseWriter::ShouldInferTorsoPitchFromHeadTranslation

Both functions explicitly returned false for MetaHuman unless BodyFusion was
MediaPipeStable. With the default safe mode:

mp.BodyFusion.MediaPipeAuthority=0

the state is NoMediaPipe/trace-only, so the fallback deliberately kept the
MetaHuman chest anchored to the profile while only the HMD-owned head/hands moved.
```

Corrective patch:

```text
Source/MediaPipeDriver/MediaPipeBodyFusion.cpp
- MetaHuman now keeps HMD-translation-driven chest solve active without MediaPipe authority.
- MetaHuman now keeps HMD-translation-driven torso pitch inference active without MediaPipe authority.
- MediaPipe still does not own the body pose unless mp.BodyFusion.MediaPipeAuthority is explicitly enabled.
- The existing torso-pitch clamp remains active through MaxForwardTorsoPitchFromHeadTranslationDeg and MaxBackwardTorsoPitchFromHeadTranslationDeg.

Source/MediaPipeDriver/MediaPipeBodyFusionTests.cpp
- Updated SourceOwnerTags to assert HMD-only MetaHuman fallback solves the chest toward backward lean.
- Added a forward HMD-only MetaHuman check so both forward and backward lean are covered.
```

Validation:

```text
D:\Epic\UE_5.7\Engine\Build\BatchFiles\Build.bat TestingKit3Editor Win64 Development -Project=D:\Epic\Unreal_Projects\TestingKit3\TestingKit3.uproject -WaitMutex -NoHotReloadFromIDE -NoUBA
Result: Succeeded

Saved\Logs\MetaHumanHmdLeanFallbackBodyFusionAutomation.log
TestingKit3.MediaPipe.BodyFusion.EmbodiedHipsOnlyTorsoAnchor: Success
TestingKit3.MediaPipe.BodyFusion.FirstPersonTorsoVisibility: Success
TestingKit3.MediaPipe.BodyFusion.SourceOwnerTags: Success
TestingKit3.MediaPipe.BodyFusion.TorsoPitchConstraint: Success
TestingKit3.MediaPipe.BodyFusion.TraceOnlyAuthorityBlocksMediaPipePose: Success

Saved\Logs\MetaHumanHmdLeanFallbackAvatarEmbodimentAutomation.log
TestingKit3.MediaPipe.AvatarEmbodiment.MetaHumanProfileSolve: Success
```

Next VR Preview proof target:

```text
mp.BodyFusion.HeadLock ... mediaPipeAuthority=0 ... bodyAuthority=NoMediaPipe ...
torsoPitchFromHead=1 ...
lean(hmdPitch=... posedPelvisChest=... posedChestHead=...)
solverEyeToPosedEye near 0.0
```
