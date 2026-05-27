# Wallace Quest VR Current Defaults Handoff - 2026-05-22

This handoff records the historical Wallace Quest VR state in `D:\Epic\Unreal_Projects\TestingKit3`. For current arm-authority testing, Wallace now uses the generic MetaHuman profile path.

For the new multi-MetaHuman profile-driven layer, read:

```text
Docs/METAHUMAN_PROFILE_DRIVEN_RETARGETING.md
Docs/Archive/WALLACE_LEGACY_ARM_SOURCE_2026-05-22.md
```

Wallace remains the proven headset baseline. The runtime now also has built-in profile definitions for Emory, Hudson, Kellan, Maria, and Payton, selected with `mp.MetaHumanActiveProfile`. Future MetaHumans should be added as `UMediaPipeMetaHumanRetargetProfile` DataAssets listed in `UMediaPipeMetaHumanProfileSettings` or `mp.MetaHumanProfileAssetPaths`, not by adding character-specific solver branches. New work must use the generic `mp.MetaHuman*` CVars. `mp.WallaceArmSource` is deprecated and no longer controls Wallace arm authority when `mp.MetaHumanArmSource=-1`.

## 2026-05-22 Full Arm-Chain Checkpoint

The historical accepted Wallace arm-length checkpoint used the TestingKit3-native full arm-chain path, then armed it with Wallace-specific compatibility CVars:

```text
mp.WallaceArmSource=1
mp.WallaceFullArmChainTrace=1
mp.WallaceFullArmChainTraceLogIntervalSeconds=0.10
mp.WallaceFullArmChainMaxAgeSeconds=0.25
```

The current user-facing profile switch is:

```text
mp.MetaHumanActiveProfile Wallace
```

Do not use `mp.WallaceArmSource` for current testing. Do not type arm-source or trace CVars for ordinary profile switching. Wallace now resolves through the same profile default arm-source path as Emory, Hudson, Kellan, Maria, and Payton. The 2026-05-22 VR Preview acceptance was captured before that unification and remains historical proof of the full arm-chain path.

This path replicates the reference project's data contract without enabling or depending on the reference project's `MetaXR` / `OculusXRMovement` plugin. TestingKit3 now has its own OpenXR body-tracking arm-chain provider in:

```text
Source/MediaPipeDriver/MediaPipeFullArmChainProvider.h
Source/MediaPipeDriver/MediaPipeFullArmChainProvider.cpp
```

The provider publishes a per-side snapshot containing shoulder, upper-arm, lower-arm, and wrist-or-palm transforms, validity flags, confidence, timestamp, and sequence. `UMediaPipePoseDrivenAnimInstance` reads the latest snapshot when the generic profile resolver selects the full arm-chain provider, and `Source/MediaPipeDriver/MediaPipePoseDrivenAnimInstance_QuestArmSolve.inl` then treats that full chain as the active profile's arm authority.

Important behavior in the generic full arm-chain profile path:

- MediaPipe still may drive torso/body/non-arm state, but it must not drive Wallace upper arm, lower arm, elbow, or wrist pose in this mode.
- The legacy Quest wrist endpoint, constrained-solve, reach-assist, body-fallback, and MediaPipe-arm hold paths are bypassed for Wallace arm pose authority while the full chain is fresh.
- The final upper/lower/hand pose is written from the full arm-chain shoulder/upper/lower/wrist-or-palm data, then the existing Quest hand/finger path can continue to drive hand rotation and fingers if it does not fight the arm-chain pose.
- The legacy arm-loss hold can no longer overwrite a fresh full-chain arm with a held MediaPipe arm sample.
- If the full-chain sample is stale or missing, the mode logs `chainActive=0` and returns instead of silently falling back to MediaPipe arm authority.

Preferred proof rows are `mp.MetaHumanFullArmChain`, not `mp.QuestWristSolve`. Historical Wallace-specific proof rows may still appear as `mp.WallaceFullArmChain`. Active rows should contain:

```text
actor=MP_LiveMetaHumanWallace
side=L/R
armSource=FullArmChain
chainActive=1
shoulderValid=1
upperArmValid=1
lowerArmValid=1
wristOrPalmValid=1
mediaPipeArmUsed=0
questHandUsed=0/1
targetReachCm=...
elbowBendDeg=...
handWorld=(...)
chainAge=...
```

Validation state for the accepted 2026-05-22 checkpoint:

```text
2026-05-22 Tools\CheckWallaceArmSourceGuards.ps1: passed
2026-05-22 TestingKit3Editor Win64 Development build: succeeded
2026-05-22 TestingKit3.MediaPipe full automation suite: 47 tests passed, exit code 0
2026-05-22 historical Tools\CaptureWallaceQuestVrEvidence.ps1 pre-armed mp.WallaceArmSource=1 and full-chain trace CVars for VR evidence runs
2026-05-22 VR Preview evidence run: Saved/CodexAgent/QuestVrEvidence/full_arm_chain_vrpreview_20260522_101600
2026-05-22 Oculus Mirror evidence: 7 successful HMD mirror screenshots with Meta Quest 3 enabled, WORN, TRACKED
2026-05-22 Wallace full-chain proof rows: 4,998 active rows after line 1990 in Saved/Logs/TestingKit3.log
2026-05-22 Active full-chain summary: L reach 21.2-52.1 cm, R reach 21.8-51.1 cm, L bend 25.5-128.9 deg, R bend 30.0-128.1 deg, mediaPipeArmUsed=0 throughout active rows
2026-05-22 User worn-headset confirmation: "Okay I did a VR preview and its looking good"
2026-05-22 archive update: current testing moved to Tools\CaptureMetaHumanQuestVrEvidence.ps1 and generic mp.MetaHuman* CVars; Tools\CaptureWallaceQuestVrEvidence.ps1 is now only a compatibility wrapper
```

The tail of the same log contains later `chainActive=0` rows after VR Preview/capture shutdown, with large `chainAge` and zero reach. Those are stale-shutdown rows and are not the active proof window. For this run, the active proof window starts around line 3577 and ends around line 55729.

## 2026-05-20 Tested Arm Solver Default

The legacy comparison/rollback arm solver is no longer treated as a sequence of loose CVar trials. Its source state is profile 4 with the constrained arm solver, held-target/last-reliable-arm loss handling instead of body-fallback jumps, adaptive HMD-relative Quest reach-scale calibration, direct tracked Quest-authoritative hand rotation, side-aware elbow poles, frame-coherent HMD-relative arm pose writes, OculusXR-style source-parent twist helper interpolation, and the full-motion regression sweep enabled.

The structural arm fixes now cover both sides of the constrained-solve boundary. `FMediaPipeQuestConstrainedArmSolver` owns side-locked elbow poles, branch repair, stale-history rejection, side-guarded body-fallback continuity, wrong-current elbow rejection under the move cap, and adaptive straightening for diagonal by-thigh endpoints. The pose-write path also uses `FMediaPipeSolvedElbowPlaneArmInput` / `TryBuildSolvedElbowPlaneArmRotations()` to build component-space upper/lower arm rotations from the same solved elbow plane when `bQuestConstrainedArmSolveApplied` is true. That prevents a valid solved elbow/wrist target from being written with an unrelated stable-surface roll basis. Near-singular elbow planes still fall back to the stable surface path.

The recent source changes remove blocked-path problems. First, `DriveArmCS()` no longer requires `bQuestSideTrackedForArm` before it even attempts the HMD-relative Quest wrist-position path. `FMediaPipeQuestWristApplyPolicy::ShouldAttemptPositionSolve()` now owns that decision, so usable wrist data and short held-target continuity can reach `TryApplyQuestWristPositionWorld()`, where the detailed tracked/untracked continuity checks already live. The optional `mp.MediaPipeArmHoldOnQuestHandLoss` path is now enabled in profile 4 and routed through `FMediaPipeQuestWristApplyPolicy::ShouldHoldArmOnQuestHandLoss()` after the wrist-position attempt gate runs, so it cannot freeze shoulder/elbow/wrist when a live or fresh-held Quest wrist endpoint candidate exists. The attempt gate now uses `HasFreshHeldTargetForPositionAttempt()` before setting `WristAttemptInput.bHasHeldTarget`, so an expired held Quest wrist target cannot tell the reliability gate to ignore MediaPipe wrist quality and then be rejected later by the apply path. Accepted live wrist samples now also update the held Quest wrist target even when the OpenXR tracked bit flickers false, so a later temporary loss cannot snap back to an older tracked-only held target. When the held target is missing or expired, `FMediaPipeQuestWristApplyPolicy::ShouldClearPositionAuthorityForHeldTargetLoss()` clears stale wrist-position authority/filter state and returns false; profile 4 now holds the last reliable arm rather than entering the MediaPipe body-fallback endpoint path by default. Calibration and HMD-avatar anchor resets now clear the same endpoint continuity through `FQuestWristSideRuntimeState::ResetPositionContinuity()`, so a held Quest wrist target mapped under the previous anchor cannot survive into the new anchor frame.

Second, profile 4 now treats a successful HMD-relative Quest wrist endpoint as a coherent frame pose. `FMediaPipeQuestWristApplyPolicy::ShouldWriteFrameCoherentQuestArmPose()` disables the inherited MediaPipe arm rotation half-life and max-step/max-speed clamps for that pose write, and profile 4 sets `mp.MediaPipeArmTargetHalfLife=0.0`, `mp.MediaPipeArmRotationHalfLife=0.0`, `mp.MediaPipeArmRotationMaxStepDegrees=0.0`, and `mp.MediaPipeArmRotationMaxSpeedDegreesPerSecond=0.0`. The Quest endpoint/filter/constrained solver still own continuity, but the final upper/lower arm bone write no longer lags a valid endpoint and then catches up as a visible snap.

Third, the default standard twist-helper pass is now stateless per frame like OculusXRMovement's `ProcessFrameInterpolateTwistJoints()`: the main arm/hand pose is solved first, and `DriveArmTwistBonesCS()` derives helper bones directly from that current component-space frame. The helper pass no longer carries an independent temporal smoothing history that can lag behind `upperarm_*`, `lowerarm_*`, and `hand_*` and create another deformation writer that looks like a candy-wrapper or snapping artifact. In Quest-authoritative palm mode, tracked hand rotation is now also direct to the current Quest target instead of being capped by `mp.QuestHandRotationMaxStepDegrees=18.0`; lost/untracked frames still use the held hand-rotation policy. An untracked Quest hand rotation can be consumed only when the same frame accepted a mapped live untracked wrist endpoint, not when the wrist is held, raw-rejected, or coming from body fallback.

Fourth, the no-torso fallback basis now comes from the target avatar frame instead of hard-coded world axes, in both the world-space solve and the component-space pose write. Before this correction, `DriveArmCS()` seeded `HipRightWorld`, `ShoulderRightWorld`, `UpWorld`, and `ForwardWorld` from world `+Y/+Z/+X` before asking `TryGetTorsoBasisWorld()` for a live torso basis. After that, the component-space write path still replaced no-torso `UpComp` / `ForwardComp` / side axes with generic component axes. If MediaPipe torso basis was missing or weak, Wallace's constrained arm path could therefore fall back to the wrong coordinate frame even though the camera/body path correctly treats Wallace's visible face/chest axis as local `+Y`. `MediaPipeBodySolverMath::BuildAvatarArmBasis()` now derives the no-torso arm basis from the target component transform, using local `+Y` for Wallace and local `+X` for normal Manny-like targets. The runtime also converts that same avatar basis back into component space for clavicle math, arm surface hints, branch guards, solved-plane writes, and diagnostics. For identity Wallace, that means forward `+Y`, right `-X`, and up `+Z`.

Fifth, the HMD-relative constrained solve no longer feeds the avatar's current elbow back in as the MediaPipe elbow hint. In `mp.QuestArmMode=3`, `DriveArmCS()` keeps the original MediaPipe shoulder/elbow/wrist sample before replacing the runtime shoulder/wrist frame with the avatar shoulder plus Quest endpoint. `FMediaPipeQuestConstrainedArmSolver::BuildSourceElbowHint()` rotates the current source elbow pole into the target avatar-shoulder/Quest-endpoint frame, locks it to the current arm side, and passes that target-space elbow hint into `BuildConstrainedArmTarget()`. This matches the important OculusXRMovement ownership rule: build the current source-driven frame first, then infer helper/twist deformation from that frame. The next VR log can prove this path with `questArmSourceElbowHint=1` and `questArmSourceElbow=...` in `mp.QuestWristSolve` rows.

Sixth, the editor `mp.StartQuestWebcamHands` path no longer carries an older wrist-only arm profile. `ApplyQuestWebcamHandsProfile()` now applies the same HMD-relative constrained arm defaults needed by profile 4: stable embodied upper body with `mp.MediaPipeDriveClavicles=0` and `mp.MediaPipeDriveSpine=0`, `mp.QuestArmMode=3`, `mp.QuestWristPositionBlend=1.0`, tracked-only wrist apply, reach/drift guards, constrained arm solve, 0.997 max reach, calibrated reach scaling, no arms-down straightening clamp, no reach-step limiter, zero close-reach pole bias, adaptive wrist filtering, zero MediaPipe arm target/rotation smoothing clamps, standard twist helpers on, broad MetaHuman sidecar helpers off, and IK off. `Tools\CheckWallaceArmSourceGuards.ps1` now fails if that editor command drifts back to the old profile.

Seventh, HMD-relative Quest wrist reach is no longer a fixed 1:1 body-size assumption. Profile 4 enables `mp.QuestConstrainedArmReachScaleCalibration=1`, stores each side's observed high Quest shoulder-to-wrist reach in `FQuestWristSideRuntimeState`, and uses `FMediaPipeQuestWristApplyPolicy::ComputeReachScaleCalibration()` to normalize high-reach endpoints toward Wallace's configured 0.997 arm reach. This is meant to address the newest log pattern where lifted, fully extended real arms still solved as visibly bent because the mapped endpoint landed short of Wallace's arm length, while over-long endpoints still clamp below the singularity.

Eighth, the rigid reach-step continuity trial is no longer a profile 4 default. The `mp.QuestConstrainedArmMaxReachStepCm=6.0` trial reduced one-frame reach-collapse snaps, but worn-headset evidence showed it also blocked natural elbow bending when the arms were below the body. Current profile 4 sets `mp.QuestConstrainedArmMaxReachStepCm=0.0`; length standardization should come from calibrated reach scale, not a per-frame reach clamp. `mp.QuestWristSolve` still exposes `questArmReachContinuity`, `questArmReachRawCm`, `questArmReachPrevCm`, and `questArmReachMaxStepCm` for diagnostics.

Ninth, two continuity leaks that matched the latest snapping evidence are now closed. A MediaPipe pose timestamp rewind now resets the global pose-yaw state and Quest wrist runtime state before any new arm solve is written, so a restarted source stream cannot keep stale wrist/yaw continuity alive. Quest semantic wrist roll also keeps its accumulator unwrapped until the max-twist clamp. The previous code unwrapped the live roll and then normalized it back into +/-180 degrees before clamping; that could flip a steady +170 degree wrist roll to -170 degrees and matches the recorded ~300 degree `twistJumpDeg` rows from the latest VR analyzer summary.

Validation state for this source state:

```text
2026-05-20 Tools\CheckWallaceArmSourceGuards.ps1: passed
2026-05-20 TestingKit3Editor Win64 Development build: succeeded
2026-05-20 TestingKit3.MediaPipe.QuestConstrainedArm.TargetSolve: 1 test passed, including source-elbow-hint transfer into the Quest endpoint frame
2026-05-20 TestingKit3.MediaPipe.BodySolverMath: 6 tests passed
2026-05-20 TestingKit3.MediaPipe.QuestConstrainedArm: 5 tests passed
2026-05-20 TestingKit3.MediaPipe.QuestWrist.ApplyPolicy: 1 test passed
2026-05-20 TestingKit3.MediaPipe.Quest: 13 tests passed
2026-05-20 TestingKit3.MediaPipe.Diagnostics.QuestWristRollCompactFormatter: 1 test passed
2026-05-20 TestingKit3.MediaPipe full automation suite: 44 tests passed, exit code 0
2026-05-20 editor Quest hands profile alignment: source guard passed, TestingKit3Editor rebuilt, full TestingKit3.MediaPipe automation found 44 tests and completed with exit code 0
2026-05-20 solver continuity reset and semantic roll unwrap: source guard passed, TestingKit3Editor rebuilt, focused QuestWrist.ApplyPolicy passed, full TestingKit3.MediaPipe automation found 44 tests and recorded 44 successes in `Saved\Logs\TestingKit3_MediaPipe_Automation_20260520_1838.log`
2026-05-20 direct tracked Quest hand rotation, HMD-relative reach-scale calibration, and tracking-loss hold default: source guard passed, TestingKit3Editor rebuilt, full TestingKit3.MediaPipe automation found 44 tests and completed with exit code 0 in `Saved\Logs\TestingKit3.log`; the command line also wrote the existing OpenXR loader API-version warning under `-NullRHI`
2026-05-20 two-pose arm-length calibration: profile 4 now enables `mp.QuestArmLengthCalibrationStartup=1`, the headset/mirror HUD prompts for full forward reach then arms down, and `mp.QuestArmDownFrameCorrection=1` uses the measured down sample to scale downward HMD-relative wrist targets without locking elbows. Follow-up after worn-headset feedback: calibration/HUD is scoped to Wallace only, accepted arm-length data is preserved across HMD translation-filter resets for the VR Preview session, and down-pose acceptance requires corrected arms-down reach to be at least `mp.QuestArmLengthCalibrationDownMinCorrectedReachFraction=0.95` of the MetaHuman target instead of accepting a result roughly 20 cm short. Source guard passed, TestingKit3Editor rebuilt, and full TestingKit3.MediaPipe automation found 45 tests and reached `Automation Test Queue Empty 45 tests performed` in `Saved\Logs\TestingKit3.log` at 22:16 Perth time. Worn-headset VR Preview confirmation is still required after the latch/corrected-reach change.
2026-05-20 constrained-arm reach-step continuity trial: source guard passed, TestingKit3Editor rebuilt, focused `TestingKit3.MediaPipe.QuestConstrainedArm` found 5 tests and passed, `TestingKit3.MediaPipe.Diagnostics` found 13 tests and passed including `QuestWristRollCompactFormatter`, and the broad `TestingKit3.MediaPipe` filter found 44 tests and completed with exit code 0 in `Saved\Logs\TestingKit3.log`; this trial used `mp.QuestConstrainedArmMaxReachStepCm=6.0` after reach-collapse evidence, but later worn-headset evidence rejected it as too rigid for arms-down elbow bend
2026-05-20 pre-solver HMD-relative reach continuity: after fresh VR Preview still showed bad elbows at arm extension, `DriveArmCS()` now remembers each side's last HMD-relative endpoint reach and applies `FMediaPipeQuestWristApplyPolicy::ApplyReachStepContinuity()` before the constrained elbow solve. Source guard passed, TestingKit3Editor rebuilt after closing the locked editor DLL, and the broad `TestingKit3.MediaPipe` filter found 44 tests and completed with exit code 0 in `Saved\Logs\TestingKit3.log`; `TestingKit3.MediaPipe.QuestWrist.ApplyPolicy` includes the pre-solver full-to-bent reach-collapse regression.
```

This 2026-05-20 profile-4 constrained-arm state is source/build/automation proof and remains historical context. The current arm-authority path is the generic profile-driven full arm-chain path with `mp.MetaHumanArmSource=-1`.

## 2026-05-19 Quest Hand/Finger Default Checkpoint

This is the current headset-confirmed hand/finger checkpoint. After the parent-chain segment-direction rebuild and the distal fingertip damping rebuild, the user ran worn-headset VR Preview and confirmed: "Okay that looks good."

Current startup finger path:

```text
mp.QuestHandDriveFingerBones=1
mp.QuestFingerJointRetarget=0
mp.QuestFingerCurlOnly=0
mp.QuestFingerPreserveSpread=0
mp.QuestFingerUseChainCurl=1
mp.QuestFingerRotationHalfLife=0.035
mp.QuestFingerCurlProximalScale=0.82
mp.QuestFingerCurlIntermediateScale=1.00
mp.QuestFingerCurlDistalScale=0.58
mp.QuestThumbCurlProximalScale=0.55
mp.QuestThumbCurlIntermediateScale=0.95
mp.QuestThumbCurlDistalScale=0.70
```

The accepted default is the `segmentDirection` path in:

```text
Source/MediaPipeDriver/MediaPipePoseDrivenAnimInstance_QuestFingerDrive.inl
```

Important implementation facts:

- Quest/OpenXR owns the visible hand and finger movement.
- The default does not use the older `mp.QuestFingerCurlOnly=1` fallback.
- The default does not use `mp.QuestFingerJointRetarget=1`; some OpenXR runtimes can publish accepted but visually static finger joint quaternions.
- Segment directions are mapped into the avatar component frame and are not rotated again by the MediaPipe hand-alignment compensation. Reapplying that alignment was one source of broken-looking hands.
- Each finger segment retargets from its live parent delta: metacarpals from the hand, non-thumb proximal bones from their metacarpal, and intermediate/distal phalanges from the previous phalanx.
- The distal/tip segment has a narrow damping pass: it blends the live Quest tip ray toward the parent-driven reference direction using the existing distal segment scale. This fixes the remaining tip twists without changing the accepted palm, wrist, and main finger curl behavior.

This matches the important hand policy from OculusXRMovement: hand descendants are rotation-only retargeted so live tracking controls orientation while the target skeleton keeps its own hand lengths. Do not turn hand/finger tracking back into position-scaling or curl-only deformation unless the user explicitly asks for a rollback experiment.

Validation state:

```text
2026-05-19 TestingKit3Editor build: succeeded
2026-05-19 TestingKit3.MediaPipe.QuestFingerSolver focused automation: 4 tests passed
2026-05-19 worn-headset VR Preview: user confirmed the result looked good
```

Normal PIE is still not proof for Quest hand appearance. Use VR Preview screenshots, logs, or the user's headset confirmation before declaring a future hand/finger change accepted.

## 2026-05-17 Source Refactor Checkpoint

The current MediaPipe/Quest runtime source layout is documented in:

```text
Docs/MEDIAPIPE_REFACTOR_STATE_2026-05-17.md
```

Important current source facts:

```text
MediaPipePoseDrivenAnimInstance.cpp: 1,679 LoC
MediaPipePoseDrivenAnimInstance.h: 734 LoC
Refactor-owned source files: 32
Refactor-owned total LoC: 12,045
TestingKit3.MediaPipe automation tests: 43 passed
```

Do not assume all wrist/arm/hand solve logic lives directly in `MediaPipePoseDrivenAnimInstance.cpp` anymore. The AnimInstance still orchestrates animation evaluation, but the implementation is split into included clusters and state objects.

Current largest solve clusters:

```text
Source/MediaPipeDriver/MediaPipePoseDrivenAnimInstance_QuestArmSolve.inl
Source/MediaPipeDriver/MediaPipePoseDrivenAnimInstance_QuestHandRotation.inl
Source/MediaPipeDriver/MediaPipePoseDrivenAnimInstance_QuestSpaceMapping.inl
```

Runtime state now lives in:

```text
Source/MediaPipeDriver/MediaPipePoseDrivenSolverState.h
```

## 2026-05-17 Working Arm-Reach Checkpoint

This is the current headset-confirmed checkpoint. After the SafeSetCS rebuild, the user confirmed in worn-headset VR Preview that Wallace's arms now move forward with the Quest hands again.

Current startup arm path:

```text
mp.AutoQuestArmReachAssistProfile=4
mp.QuestArmMode=3
mp.QuestWristPositionBlend=1.0
mp.QuestWristMaxRelativeDeltaCm=82.0
mp.QuestWristRequireTrackedForApply=1
mp.QuestHandRotationHalfLife=0.0
mp.QuestHandRotationMaxStepDegrees=0.0
mp.QuestConstrainedArmBodyFallback=0
mp.QuestConstrainedArmBodyFallbackWristHalfLife=0.08
mp.QuestConstrainedArmBodyFallbackMaxWristStepCm=14.0
mp.QuestConstrainedArmReachScaleCalibration=1
mp.QuestConstrainedArmReachScaleUniform=1
mp.QuestConstrainedArmReachScaleMinObservedFraction=0.88
mp.QuestConstrainedArmReachScaleApplyStartFraction=0.0
mp.QuestConstrainedArmReachScaleApplyFullFraction=1.0
mp.QuestConstrainedArmReachScaleMin=0.82
mp.QuestConstrainedArmReachScaleMax=1.18
mp.QuestArmLengthCalibrationStartup=1
mp.QuestArmLengthCalibrationHud=1
mp.QuestArmLengthCalibrationHoldSeconds=2.5
mp.QuestArmLengthCalibrationStableFrames=20
mp.QuestArmLengthCalibrationMaxHandVelocityCmSec=30.0
mp.QuestArmLengthCalibrationForwardMinReachFraction=0.88
mp.QuestArmLengthCalibrationDownMinBelowShoulderFraction=0.40
mp.QuestArmLengthCalibrationDownMinVerticalDominance=0.65
mp.QuestArmLengthCalibrationDownMinCorrectedReachFraction=0.95
mp.QuestArmDownFrameCorrection=1
mp.QuestArmDownFrameCorrectionMaxScale=1.80
mp.QuestPalmMode=2
mp.MediaPipeArmTargetHalfLife=0.0
mp.MediaPipeArmRotationHalfLife=0.0
mp.MediaPipeArmRotationMaxStepDegrees=0.0
mp.MediaPipeArmRotationMaxSpeedDegreesPerSecond=0.0
mp.MediaPipeUseArmIK=0
mp.QuestWristForceArmIK=0
mp.MediaPipeDriveLegs=0
mp.MediaPipeUseLegIK=0
mp.MediaPipeDrivePelvisTranslation=0
mp.MediaPipeDriveArmTwistBones=1
mp.MediaPipeDriveMetaHumanArmHelpers=0
mp.MediaPipeArmHoldOnQuestHandLoss=1
```

Current body-conflict policy:

```text
mp.AutoQuestEmbodiedStableBody=1
mp.MediaPipeDriveClavicles=0
mp.MediaPipeDriveSpine=0
mp.MediaPipeDriveLegs=0
mp.MediaPipeUseLegIK=0
mp.MediaPipeDrivePelvisTranslation=0
```

This split is deliberate. Quest/OpenXR owns the HMD pose, the Quest wrist endpoint used for arm reach, hand orientation, and fingers. MediaPipe still supplies the webcam pose and low-weight arm/shoulder/elbow hints. Stable body keeps lower body, pelvis, spine, and clavicles out of the startup path so arm extension can be evaluated without shoulder-root retargeting changing the same pose.

2026-05-19 regression cleanup: the active default has been narrowed back to stable arm extension first for the main body/arm solver. Clavicle promotion, broad MetaHuman sidecar/corrective helpers, and direct wrist-roll helper ownership made the arm tests too coupled, so those paths stay diagnostic-only. The only helper-bone path enabled by default is the standard target-skeleton twist-helper pass, and it is still pending worn-headset acceptance.

The immediate arm fix was not a CVar tuning change. The broken state was caused by component-space pose writes leaving child bones stale after parent arm bones moved. `Source/MediaPipeDriver/MediaPipePoseDrivenAnimInstance.cpp` now routes the local component-space write helpers through `SafeSetCSBoneTransforms()` so wrist/hand children refresh after upperarm/lowerarm updates. The relevant paths are `ApplyRotationCS`, `ApplyTranslationDeltaCS`, `DriveArmTwistBonesCS`, and `DrivePelvisTranslationCS`.

2026-05-19 arm endpoint dropout patch:

- Latest failing VR Preview logs showed the HMD-relative constrained arm solve dropping out when Quest hand tracking became unavailable near the thighs. At the end of those traces, `questTracked=0`, `positionApplied=0`, `targetMapped=0`, and `constrainedArmSolve=0`, so the arm reverted to avatar/rest pose and looked bent or blocked.
- The default now keeps profile 4 in the constrained arm path by building a MediaPipe body-proportioned fallback wrist endpoint when the live Quest wrist endpoint is unavailable.
- Latest failing logs also showed a second failure: profile 4 accepted untracked Quest wrist rows as real endpoints (`questTracked=0`, `untrackedData=1`, `positionApplied=1`). Those rows produced short reaches near the body, so the arm looked bent by the thigh even though the analytic arm sanity check did not mark the limb as broken. Strictly dropping all `tracked=0` rows fixed that failure mode, but old headset traces also show OpenXR can publish 26 usable hand positions while the tracked bit is false. The current policy keeps `mp.QuestWristRequireTrackedForApply=1`, but `FMediaPipeQuestWristApplyPolicy` lets the constrained arm endpoint consume untracked wrist positions only when the live wrist is continuous with the last accepted live wrist sample and passes the same short age/step budget. Quest hand rotation now uses the current wrist mapping trace as well as that continuity policy: an untracked hand rotation is allowed only when the same frame accepted a mapped live untracked wrist endpoint, and it is rejected for held wrist targets, raw-rejected wrist rows, and constrained body fallback.
- 2026-05-20 structural correction: the constrained arm attempt gate now calls `FMediaPipeQuestWristApplyPolicy::ShouldAttemptPositionSolve()` instead of short-circuiting on `bQuestSideTrackedForArm`. This keeps the high-level arm solve from bypassing the lower OpenXR continuity policy when Quest publishes usable but temporarily untracked wrist positions. When such a live wrist is accepted, `QuestWristSideState.HeldTargetWorld` is refreshed from that same accepted sample, so held-target fallback cannot jump back to an older tracked-only endpoint on the next loss frame.
- 2026-05-20 arm-loss hold cleanup: the optional last-reliable-arm hold now asks `FMediaPipeQuestWristApplyPolicy::ShouldHoldArmOnQuestHandLoss()` after `ShouldAttemptPositionSolve()`. That means a diagnostic hold cannot preempt the constrained endpoint solve when OpenXR still provides a usable wrist or a fresh held endpoint.
- 2026-05-20 frame-coherent arm pose write: when profile 4 has a mapped/fallback Quest wrist endpoint, the upper/lower arm pose write is direct for that frame instead of being delayed by the inherited MediaPipe arm rotation half-life and rotation step caps. This follows the same high-level ownership rule as OculusXRMovement: build the frame, then write the frame.
- The fallback is based on source shoulder/elbow/wrist reach fraction, target avatar shoulder, and target upper/lower arm lengths. It clamps just below full extension to avoid singular straight-arm elbow flips.
- Quest-authoritative tracked hand rotation now applies the current Quest target directly in palm mode 2. Profile 4 and the editor hand profile set `mp.QuestHandRotationMaxStepDegrees=0.0` and `mp.QuestHandRotationHalfLife=0.0`; lost/untracked frames still use the held hand-rotation policy instead of consuming stale or body-fallback wrist frames.
- 2026-05-20 cleanup: the body fallback now uses the same arms-down straightening helper as the tracked constrained solve. Before this, live tracked frames could use near-full down-by-thigh extension while temporary tracking-loss fallback frames preserved a short MediaPipe body reach, which could recreate the bent-arm/snap pattern even though profile 4 was still active.
- 2026-05-20 diagnostic cleanup: `mp.QuestWristSolve` fallback rows now include `questArmBodyFallbackTargetReachCm`, `questArmBodyFallbackTargetReachFrac`, `questArmBodyFallbackDown`, and `questArmBodyFallbackDownAlpha`. This makes the next VR Preview log capable of proving whether a body-fallback arms-down row actually used near-full straightening.
- Profile 4 no longer defaults to constrained body fallback during Quest wrist loss. It keeps fresh held Quest endpoints through the normal grace window, then uses `mp.MediaPipeArmHoldOnQuestHandLoss=1` to hold the last reliable arm sample instead of jumping to a MediaPipe body-proportioned fallback endpoint.
- The continuity pass now caps the fallback elbow step as well as the fallback wrist step, using the same default step budget. This prevents a held or body-fallback wrist from looking stable while the elbow jumps to a different branch.
- `TryUseHeldQuestWristTarget()` no longer fades constrained mode back to the avatar/rest wrist after the held target expires. Expiration clears stale wrist-position authority/filter state and returns false so `DriveArmCS()` can choose the configured loss behavior. In profile 4 that configured behavior is last-reliable arm hold, not body fallback.
- `DriveArmCS()` also preserves the last constrained elbow/pole solve through a short tracking gap instead of clearing it immediately on the first missed constrained frame.
- The deterministic constrained target solve has been extracted from `DriveArmCS()` into `FMediaPipeQuestConstrainedArmSolver::BuildConstrainedArmTarget()`. The solver now owns reach clamp, optional arm-down straightening, stable pole selection, low-weight MediaPipe elbow hint, wrist-step pole continuity, near-full pole continuity, and branch-flip repair. `DriveArmCS()` now only builds the solver input, applies runtime smoothing, fills trace fields, and writes the final arm pose.
- The no-torso stable elbow pole is now side-aware. Both `FMediaPipeQuestConstrainedArmSolver::BuildConstrainedArmTarget()` and the legacy reach-assist fallback use the current arm side's shoulder-right direction, or a left/right world fallback if the torso basis is unavailable, instead of a generic right-side pole. This directly targets the down-by-thigh case where the elbow could choose the wrong branch when the webcam torso basis was weak or missing.
- 2026-05-20 avatar-frame no-torso basis: `DriveArmCS()` now initializes no-torso hip/shoulder right, up, and forward from `MediaPipeBodySolverMath::BuildAvatarArmBasis()` before `TryGetTorsoBasisWorld()` can override them. It also uses the same avatar-derived axes for no-torso `HipRightComp`, `ShoulderRightComp`, `UpComp`, and `ForwardComp` after the solved world pose is transformed back to component space. Wallace therefore keeps its local `+Y` face/chest axis through the constrained fallback, pose-write, branch-guard, and diagnostic layers instead of silently dropping to a generic world/component-axis frame when MediaPipe torso landmarks are unavailable.
- 2026-05-20 source-elbow-hint correction: in HMD-relative mode, `DriveArmCS()` keeps the original MediaPipe shoulder/elbow/wrist points and builds a target-space elbow hint through `FMediaPipeQuestConstrainedArmSolver::BuildSourceElbowHint()`. The constrained solve now uses that source-derived target elbow hint instead of the avatar's previous/current elbow when a live mapped Quest wrist endpoint is available. Body fallback remains separate because it already builds both fallback wrist and elbow from the MediaPipe body sample.
- The legacy shoulder-rollback hard hold is now kept out of the HMD-relative profile 4 arm path entirely. `mp.MediaPipeShoulderRollbackGuard=1` still protects direct MediaPipe shoulder rollback, but `FMediaPipeArmGuardPolicy` bypasses the guard for `mp.QuestArmMode=3` even during startup, temporary Quest wrist loss, or a failed constrained-solve frame. This prevents `GuardBlend=0.0` from freezing the previous upper/lower arm rotations and then releasing as a snap.
- The down-straighten rule now has an adaptive budget for endpoints that are deeply below the shoulder, including diagonal by-thigh endpoints that are not almost vertical from the shoulder. This is still bounded below singular full extension, but it covers the visible failure where the hand is down by the body while the elbow remains over-bent because the raw endpoint was too short for the old fixed correction budget.
- New automation coverage `TestingKit3.MediaPipe.QuestConstrainedArm.TargetSolve` exercises arms-down extension, diagonal by-thigh extension, adaptive down-straighten bounds, near-full pole continuity, branch-flip repair, and no-history startup behavior. `TestingKit3.MediaPipe.QuestConstrainedArm.TrajectoryContinuity` runs a small sequence with alternating noisy MediaPipe elbows and verifies that arms-down and forward-reach frames keep the pole continuous instead of snapping branches.
- 2026-05-20 side-aware no-torso coverage: `TestingKit3.MediaPipe.QuestConstrainedArm.TargetSolve` now also asserts left/right no-torso arms-down elbow sides, and `TestingKit3.MediaPipe.QuestConstrainedArm.TrajectoryContinuity` covers a no-torso side-of-body arms-down path with alternating bad MediaPipe elbow hints. This prevents the generic-pole regression from returning without a test failure.

Relevant source files:

```text
Source/MediaPipeDriver/MediaPipeQuestConstrainedArmSolver.h
Source/MediaPipeDriver/MediaPipeQuestConstrainedArmSolver.cpp
Source/MediaPipeDriver/MediaPipeBodySolverMath.h
Source/MediaPipeDriver/MediaPipeBodySolverMath.cpp
Source/MediaPipeDriver/MediaPipePoseDrivenAnimInstance_QuestArmSolve.inl
Source/MediaPipeDriver/MediaPipePoseDrivenAnimInstance_QuestSpaceMapping.inl
Source/MediaPipeDriver/MediaPipePoseDrivenSolverState.h
Source/MediaPipeDriver/MediaPipeQuestWristApplyPolicy.h
Source/MediaPipeDriver/MediaPipeQuestWristApplyPolicy.cpp
Source/MediaPipeDriver/MediaPipeQuestWristDiagnosticFormatter.cpp
Source/MediaPipeDriver/MediaPipeRuntimeCVars.cpp
```

Validation state for this patch:

```text
2026-05-19 TestingKit3Editor build after constrained-target extraction: succeeded
2026-05-19 initial TestingKit3.MediaPipe full automation filter: 34 tests passed before the guard-policy test was added
2026-05-19 non-visual PIE startup readback before the untracked-wrist rejection patch: confirmed `armProfile=4`, `clavicles=0`, `wristRequireTracked=0`, `downStraighten=1`, `armTwistBones=1`, `standardArmTwistDiagnostic=0`, and direct wrist-roll helper ownership off
2026-05-19 historical standard twist-helper smoothing rebuild, later superseded by the 2026-05-20 stateless runtime: succeeded; focused ArmTwist and PoseDrivenSolverState filters passed; full TestingKit3.MediaPipe filter passed before the guard-policy test was added
2026-05-19 stricter arms-down default: build succeeded; focused QuestConstrainedArm filter passed; full TestingKit3.MediaPipe filter passed before the guard-policy test was added
2026-05-19 shoulder rollback guard-policy isolation: build succeeded; focused ArmGuardPolicy and QuestConstrainedArm filters passed; full TestingKit3.MediaPipe filter passed 35 tests
2026-05-20 HMD-relative shoulder rollback bypass: source changed so the legacy rollback hard hold cannot block profile 4 / `mp.QuestArmMode=3` even on startup/loss/failed-solve frames. `Tools/CheckWallaceArmSourceGuards.ps1` passed, `TestingKit3Editor` rebuilt successfully, focused `TestingKit3.MediaPipe.ArmGuardPolicy` passed, focused `TestingKit3.MediaPipe.QuestConstrainedArm` found 5 tests and passed under forced log flush, and the broad `TestingKit3.MediaPipe` filter found 43 tests and ended with `Automation Test Queue Empty 43 tests performed`.
2026-05-19 adaptive arms-down trajectory continuity: build succeeded; focused QuestConstrainedArm filter passed 3 tests; full TestingKit3.MediaPipe filter passed 36 tests
2026-05-19 MetaHuman sidecar-helper pass: build succeeded; focused ArmTwist filter passed; full TestingKit3.MediaPipe filter passed 36 tests
2026-05-19 untracked-wrist rejection and helper-hierarchy correction: build succeeded; focused QuestConstrainedArm, MetaHumanArmHelpers, QuestWrist.ApplyPolicy, Runtime.CVars filters passed; full TestingKit3.MediaPipe filter found 38 tests and recorded 38 successes
2026-05-19 exhaustive Wallace arm-region helper audit: build succeeded; focused MetaHumanArmHelpers filter passed; full TestingKit3.MediaPipe filter found 38 tests and recorded 38 successes
2026-05-19 conflicting editor live-video profile cleanup: MediaPipeDriverEditor rebuilt successfully after `ApplyQuestWebcamHandsProfile()` was aligned with tracked-only Quest wrist application; full TestingKit3.MediaPipe filter found 38 tests and recorded 38 successes
2026-05-20 body-fallback arms-down straightening cleanup: build succeeded; focused TestingKit3.MediaPipe.QuestConstrainedArm filter found 3 tests and recorded 3 successes; full TestingKit3.MediaPipe filter found 38 tests and recorded 38 successes
2026-05-20 body-fallback diagnostic fields: build succeeded after adding fallback target-reach/down-straighten fields to `mp.QuestWristSolve`; focused TestingKit3.MediaPipe.Diagnostics.QuestWristRollCompactFormatter passed; focused TestingKit3.MediaPipe.QuestConstrainedArm passed; full TestingKit3.MediaPipe filter found 38 tests and recorded 38 successes
2026-05-20 tracking-loss recovery coverage: build succeeded after adding `TestingKit3.MediaPipe.QuestConstrainedArm.TrackingLossRecovery`; focused filter found 4 QuestConstrainedArm tests and `TrackingLossRecovery` passed; full TestingKit3.MediaPipe filter found 39 tests and ended with `Automation Test Queue Empty 39 tests performed`
2026-05-20 wrist helper runtime coverage: build succeeded after the runtime lower-arm helper pass was corrected to drive `wrist_inner/outer_*` through the lowerarm-to-hand sidecar interpolation instead of registering those bones but skipping them; focused `TestingKit3.MediaPipe.ArmTwist.OculusStyleInterpolation` passed, focused `TestingKit3.MediaPipe.MetaHumanArmHelpers.WallaceCoverage` passed, and the full `TestingKit3.MediaPipe` filter found 39 tests and ended with `Automation Test Queue Empty 39 tests performed`
2026-05-20 Wallace arm source guard: `Tools/CheckWallaceArmSourceGuards.ps1` passed; it checks profile 4, the editor hand-profile defaults, helper-bone ownership including lower-arm wrist helpers, and prevents raw `SetComponentSpaceTransform()` from reappearing in the arm pose paths
2026-05-20 OculusXR-style solve-order/reset guard: build succeeded after `TestingKit3.MediaPipe.PoseDrivenSolverState.Limb.Reset` was expanded to prove MetaHuman sidecar-helper smoothing resets with the rest of the arm state; `Tools/CheckWallaceArmSourceGuards.ps1` now also guards that `DriveArmCS()` runs for both arms before `DriveArmTwistBonesCS()` and local-pose conversion. The focused limb reset test passed, and the full `TestingKit3.MediaPipe` filter again found 39 tests and ended with `Automation Test Queue Empty 39 tests performed`
2026-05-20 no-torso arms-down straightening: build succeeded after the constrained arm solver stopped requiring a MediaPipe torso basis before applying the arms-down reach correction. This keeps Quest wrist/HMD-relative arms-down extension active when the webcam torso basis is missing or weak, using the available up vector instead. New no-torso cases were added to `TestingKit3.MediaPipe.QuestConstrainedArm.BodyFallback` and `TestingKit3.MediaPipe.QuestConstrainedArm.TargetSolve`; the full `TestingKit3.MediaPipe` filter found 39 tests and ended with `Automation Test Queue Empty 39 tests performed`
2026-05-20 usable-untracked endpoint and hand smoothing startup: source guard passed after `FMediaPipeQuestWristApplyPolicy` was changed so constrained arm position can consume usable untracked Quest wrist positions while hand rotation remained tracked-only at that checkpoint; the later current-frame untracked hand-rotation gate supersedes that limitation. At this earlier checkpoint, Quest-authoritative hand rotation initialized from the current hand pose and profile 4 capped hand-rotation smoothing with `mp.QuestHandRotationMaxStepDegrees=18.0`; this was later superseded by the direct tracked Quest hand default (`0.0` half-life and step cap). `TestingKit3Editor` rebuilt successfully, focused `TestingKit3.MediaPipe.Quest` found 12 tests and passed, and the full `TestingKit3.MediaPipe` filter found 39 tests and ended with `Automation Test Queue Empty 39 tests performed`. The command log printed OpenXR loader warnings about API version 1.1 while running under `-NullRHI`, but the automation queue passed.
2026-05-20 continuity-gated untracked endpoint: the broad usable-untracked exception above was tightened so an untracked Quest wrist row can drive the constrained endpoint only if it is continuous with `FQuestWristSideRuntimeState::LastAcceptedLiveWristWorld`, within `mp.QuestWristLostTrackingGraceSeconds`, and within the existing wrist filter reset-distance budget. Sudden stale or short untracked endpoints now fall back to the held target/body fallback instead of becoming a new arm target. `Tools/CheckWallaceArmSourceGuards.ps1` passed, `TestingKit3Editor` rebuilt successfully, focused `TestingKit3.MediaPipe.Quest` found 12 tests and passed, and the full `TestingKit3.MediaPipe` filter found 39 tests and ended with `Automation Test Queue Empty 39 tests performed`. The command log again printed the existing OpenXR loader API-version warning under `-NullRHI`, but the automation queue passed.
2026-05-20 objective-gate cleanup: `Tools/RunQuestWristObjectiveGate.ps1` was aligned with the current profile 4 arm path instead of the older wrist-only baseline. It now applies `mp.QuestArmMode=3`, `mp.QuestWristPositionBlend=1.0`, constrained solve/down-straighten, direct Quest wrist-roll helper ownership off, stable embodied spine/clavicle defaults, standard twist helpers on, and broad MetaHuman sidecar helpers off before running the wrist analyzer. The later reach-scale/loss-hold update also sets body fallback off and reach-scale on in this gate. `Tools/CheckWallaceArmSourceGuards.ps1` now guards those objective-gate CVars so a future proof run cannot silently test the wrong arm system.
2026-05-20 side-aware no-torso elbow pole: `FMediaPipeQuestConstrainedArmSolver::BuildConstrainedArmTarget()` and the legacy reach-assist fallback now use side-aware stable poles when the MediaPipe torso basis is missing. `Tools/CheckWallaceArmSourceGuards.ps1` passed and now guards the side-aware pole code plus the no-torso side-of-body trajectory test. `TestingKit3Editor` rebuilt successfully; focused `TestingKit3.MediaPipe.QuestConstrainedArm` found 4 tests and passed; full `TestingKit3.MediaPipe` found 39 tests and ended with `Automation Test Queue Empty 39 tests performed`. The command log printed the existing OpenXR loader API-version warning under `-NullRHI`, but the automation queue passed.
2026-05-20 eye-center default cleanup: `ApplyAutoQuestProfile()` now keeps `mp.AutoQuestEmbodiedCameraForwardOffsetCm=0.0` instead of forcing the older 12 cm comparison offset. `Tools/CheckWallaceArmSourceGuards.ps1` now guards this so the embodied view default matches the documented eye-center camera policy. `TestingKit3Editor` rebuilt successfully after this default cleanup; focused `TestingKit3.MediaPipe.QuestConstrainedArm` again found 4 tests and passed, and full `TestingKit3.MediaPipe` again found 39 tests and ended with `Automation Test Queue Empty 39 tests performed`.
2026-05-20 side-aware body-fallback degenerate pole: `FMediaPipeQuestConstrainedArmSolver::BuildBodyFallbackEndpoint()` now receives `bIsLeft` and `ShoulderRightWorld` from `DriveArmCS()` and uses them when the MediaPipe source arm is collinear and has no usable source elbow pole. This removes the remaining generic pole fallback in the body-fallback branch, which is the branch used during temporary Quest wrist loss or rejected wrist endpoints. `TestingKit3.MediaPipe.QuestConstrainedArm.BodyFallback` now covers degenerate left/right arms-down fallback poles. `Tools/CheckWallaceArmSourceGuards.ps1` passed, `TestingKit3Editor` rebuilt successfully, focused `TestingKit3.MediaPipe.QuestConstrainedArm` found 4 tests and passed, and full `TestingKit3.MediaPipe` found 39 tests and ended with `Automation Test Queue Empty 39 tests performed`. The command log printed the existing OpenXR loader API-version warning under `-NullRHI`, but the automation queue passed. This is still source and automation proof, not worn-headset acceptance.
2026-05-20 source-parent-aware twist-chain correction: `FMediaPipeArmTwistSolver` now carries both the immediate twist parent and the mapped source-parent endpoint, matching the OculusXRMovement retargeter distinction between a twist helper's parent and the source chain start. Corrective helper children under `upperarm_correctiveRoot_*` and `lowerarm_correctiveRoot_*` now project/stretch against `upperarm_* -> lowerarm_*` and `lowerarm_* -> hand_*` instead of against the helper root itself. `TestingKit3.MediaPipe.ArmTwist.OculusStyleInterpolation` now includes an unmapped-chain case that would use the wrong weight/translation scale under the old assumption.
2026-05-20 full-motion arm-sweep regression coverage: `TestingKit3.MediaPipe.QuestConstrainedArm.FullMotionSweep` now drives both arms through down-by-thigh, forward, side, and return-to-down poses while injecting alternating bad MediaPipe elbow hints. It asserts solved upper/lower arm lengths, correct side branch, near-full down-by-thigh reach, no elbow-pole flips, and bounded elbow steps. `Tools/CheckWallaceArmSourceGuards.ps1` now fails if this sweep or the side-hemisphere pole lock disappears.
2026-05-20 full-motion solver validation: source guard passed, `TestingKit3Editor` rebuilt successfully, focused `TestingKit3.MediaPipe.QuestConstrainedArm.FullMotionSweep` passed, focused `TestingKit3.MediaPipe.QuestConstrainedArm` found 5 tests and passed, focused `TestingKit3.MediaPipe.ArmTwist.OculusStyleInterpolation` passed, focused `TestingKit3.MediaPipe.Runtime.CVars` passed, and the broad `TestingKit3.MediaPipe` filter ended cleanly with `Automation Test Queue Empty 40 tests performed`.
2026-05-20 stale-history branch-repair hardening: a follow-up audit found that branch repair could still reuse a previous elbow pole directly. The accepted source change does not side-lock every previous-pole continuity path, because that overconstrains valid curved arm motion; it only rejects branch-repair reuse when the repaired elbow would land on the wrong current side. `TestingKit3.MediaPipe.QuestConstrainedArm.TargetSolve` now covers this wrong-side history case, `Tools/CheckWallaceArmSourceGuards.ps1` guards the regression text, `TestingKit3Editor` rebuilt successfully, focused `TestingKit3.MediaPipe.QuestConstrainedArm` found 5 tests and passed, and the broad `TestingKit3.MediaPipe` filter ended cleanly with `Automation Test Queue Empty 40 tests performed`.
2026-05-20 side-guarded body-fallback continuity: `ApplyBodyFallbackContinuity()` now receives the current target shoulder, arm side, and shoulder-right vector when called from `DriveArmCS()`. During temporary Quest wrist loss or rejected wrist endpoints, fallback continuity only reuses previous elbow history if that history and the current fallback elbow remain on the current arm side. This prevents the fallback blend from preserving a stale wrong-side elbow branch through a tracking transition. `TestingKit3.MediaPipe.QuestConstrainedArm.BodyFallback` now covers valid current-side continuity and wrong-side fallback-history rejection; `Tools/CheckWallaceArmSourceGuards.ps1` guards the runtime and solver side checks. `TestingKit3Editor` rebuilt successfully, focused `TestingKit3.MediaPipe.QuestConstrainedArm` found 5 tests and passed, and the broad `TestingKit3.MediaPipe` filter ended cleanly with `Automation Test Queue Empty 40 tests performed`.
2026-05-20 elbow move-cap side invariant: a second audit found that `mp.QuestConstrainedArmMaxElbowMoveCm` could clamp the solved elbow from the current elbow position even when that current elbow was already on the wrong side. The solver now treats wrong-side current elbows as invalid branch state for the side invariant: the move cap still smooths normal corrections, but it cannot leave the final elbow on the wrong current arm side when the analytic solve is side-correct. `TestingKit3.MediaPipe.QuestConstrainedArm.TargetSolve` covers this wrong-current case, `Tools/CheckWallaceArmSourceGuards.ps1` guards the invariant, `TestingKit3Editor` rebuilt successfully, focused `TestingKit3.MediaPipe.QuestConstrainedArm` found 5 tests and passed, and the broad `TestingKit3.MediaPipe` filter ended cleanly with `Automation Test Queue Empty 40 tests performed`.
2026-05-20 diagonal by-thigh arms-down correction: the old adaptive straightener required an almost vertical shoulder-to-wrist ray before it could exceed the fixed `DownStraightenMaxCm` budget, so a realistic hand down by the thigh could stay short and visibly bent. `FMediaPipeQuestConstrainedArmSolver` now includes a side-of-body down alpha for deeply-below-shoulder endpoints, and `TestingKit3.MediaPipe.QuestConstrainedArm.TargetSolve` asserts that a diagonal by-thigh endpoint reaches near-full straightness while staying under the 99.7% singularity cap. `Tools/CheckWallaceArmSourceGuards.ps1` passed, `TestingKit3Editor` rebuilt successfully, focused `TestingKit3.MediaPipe.QuestConstrainedArm` found 5 tests and passed, and the broad `TestingKit3.MediaPipe` filter found 41 tests and completed with exit code 0.
2026-05-20 explicit full-extension reach cap: profile 4 now sets `mp.QuestConstrainedArmMaxReachFraction=0.997`, and the live HMD-relative constrained arm path consumes that CVar in the pre-solve wrist clamp and constrained target solve. This removes the hidden `0.985` cap that could keep a fully extended real arm visibly bent except when the special arms-down straightener happened to override it. `TestingKit3.MediaPipe.QuestConstrainedArm.TargetSolve` now covers forward and side full-extension endpoints that reach the configured near-full cap without using arms-down straightening. Body fallback remains covered as a diagnostic solver path, but it is no longer the profile 4 loss default. This is source and automation proof only until worn-headset VR Preview confirms it.
2026-05-20 near-full pose-write threshold: profile 4 now sets `mp.QuestConstrainedArmSolvedPlaneMinSin=0.08`, and `DriveArmCS()` uses that threshold only after a successful Quest-constrained solve. Direct MediaPipe arm solving still uses `mp.MediaPipeArmElbowPlaneMinSin`. This keeps the solver-owned near-full elbow pole from being discarded by the bone-write layer when the arm is almost straight. `TestingKit3.MediaPipe.BodySolverMath.SolvedElbowPlaneArmBasis` now includes a near-full 99.7% reach case and a too-strict-threshold rejection case. `Tools/CheckWallaceArmSourceGuards.ps1` passed, `TestingKit3Editor` rebuilt successfully, focused `TestingKit3.MediaPipe.BodySolverMath.SolvedElbowPlaneArmBasis` passed, focused `TestingKit3.MediaPipe.Runtime.CVars` passed, and the broad `TestingKit3.MediaPipe` filter found 43 tests and ended with `Automation Test Queue Empty 43 tests performed`. This is source and automation proof only until worn-headset VR Preview confirms it.
2026-05-20 solved elbow-plane pose-write correction: the constrained arm solve was producing valid `PoseUpperComp` / `PoseLowerComp` directions, but the non-IK bone-write branch could still choose an unrelated stable-surface roll basis unless `mp.MediaPipeArmUseElbowPlaneRoll` was manually enabled. `Source/MediaPipeDriver/MediaPipeBodySolverMath.*` now exposes the solved elbow-plane basis helper, `DriveArmCS()` uses it automatically after a successful constrained solve, and `TestingKit3.MediaPipe.BodySolverMath.SolvedElbowPlaneArmBasis` proves that the resulting component-space upper/lower rotations reconstruct the solved directions and reject near-singular elbow planes for the stable fallback. `Tools/CheckWallaceArmSourceGuards.ps1` passed, `TestingKit3Editor` rebuilt successfully, the focused new body-solver test passed, focused `TestingKit3.MediaPipe.QuestConstrainedArm` found 5 tests and passed, and the broad `TestingKit3.MediaPipe` filter found 42 tests and completed with exit code 0.
2026-05-20 post-finger twist topology guard: the local AlanMovement/OculusXRMovement source cross-check confirmed that the mapped frame pose is built first and twist-helper interpolation happens afterward. TestingKit3 now guards the matching Wallace topology: `TestingKit3.MediaPipe.MetaHumanArmHelpers.OculusStyleDefaultScope` requires Oculus-style detection to find 8/8 standard twist helpers and verifies those helpers are not ancestors of Wallace hand/finger bones. This proves the current post-finger `DriveArmTwistBonesCS()` order cannot stale the hand/finger chain for the standard startup helpers. `TestingKit3Editor` rebuilt successfully, focused `TestingKit3.MediaPipe.MetaHumanArmHelpers` found 2 tests and passed, and the broad `TestingKit3.MediaPipe` filter found 42 tests and completed with exit code 0.
2026-05-20 pose-frame dropout continuity: `PreUpdate()` no longer clears the last accepted MediaPipe pose frame before proving a replacement frame exists. If the tracker has a transient missing/invalid frame, the anim node holds the last solved body frame while still refreshing the target mesh component transform and reading current Quest hand/HMD state. The frame-associated MediaPipe raw hand landmarks are held with that body frame and are cleared only by an explicit retarget reset, so the held body pose cannot lose its matching fallback hand data. This prevents a one-frame MediaPipe dropout from collapsing Wallace to reference pose while Quest hands continue to drive wrist/hand data. `TestingKit3.MediaPipe.PoseFrameContinuity.HoldLastFrameOnDropout` covers the body-frame and raw-hand hold policy, `Tools/CheckWallaceArmSourceGuards.ps1` guards the runtime call/reset/target-transform refresh and the no-early-hand-clear rule, `TestingKit3Editor` rebuilt successfully, and the broad `TestingKit3.MediaPipe` filter found 43 tests and completed with exit code 0.
2026-05-20 constrained wrist attempt and held-target continuity: `FMediaPipeQuestWristApplyPolicy::ShouldAttemptPositionSolve()` now decides whether profile 4 attempts the Quest wrist-position path, so `DriveArmCS()` no longer blocks usable-but-untracked wrist samples before the detailed continuity policy can run. Accepted live wrist samples now refresh the held Quest wrist target in `TryApplyQuestWristPositionWorld()`. `Tools/CheckWallaceArmSourceGuards.ps1` passed, `TestingKit3Editor` rebuilt successfully, focused `TestingKit3.MediaPipe.QuestWrist.ApplyPolicy` found 1 test and passed, and the broad `TestingKit3.MediaPipe` filter found 43 tests and ended with `Automation Test Queue Empty 43 tests performed`. The command log printed the existing OpenXR loader API-version warning under `-NullRHI`, but the automation queue passed.
2026-05-20 stateless standard twist-helper runtime: `DriveArmTwistBonesCS()` no longer applies independent temporal smoothing after the Oculus-style helper interpolation solve. `Tools/CheckWallaceArmSourceGuards.ps1` now guards this by failing if `UpdateSmoothedRotation()` returns to `MediaPipePoseDrivenAnimInstance_ArmTwist.inl`. Source guard passed, `TestingKit3Editor` rebuilt successfully, focused `TestingKit3.MediaPipe.ArmTwist.OculusStyleInterpolation` passed, and the broad `TestingKit3.MediaPipe` filter found 43 tests and ended with `Automation Test Queue Empty 43 tests performed`.
2026-05-20 expired held-target authority reset: `FMediaPipeQuestWristApplyPolicy::ShouldClearPositionAuthorityForHeldTargetLoss()` now covers missing, zero-grace, unknown-age, and expired held targets. `TryUseHeldQuestWristTarget()` uses that policy to clear stale wrist-position authority/filter state before returning false to the constrained body fallback path. `Tools/CheckWallaceArmSourceGuards.ps1` guards the helper, runtime call, and regression text. Source guard passed, `TestingKit3Editor` rebuilt successfully, focused `TestingKit3.MediaPipe.QuestWrist.ApplyPolicy` passed, and the broad `TestingKit3.MediaPipe` filter found 43 tests and ended with `Automation Test Queue Empty 43 tests performed`.
2026-05-20 current-frame untracked hand-rotation gate: `DriveQuestHandCS()` now receives the current `FQuestWristMappingTrace` and asks `FMediaPipeQuestWristApplyPolicy::CanUseQuestHandRotationForCurrentFrame()` before consuming untracked Quest hand rotation. If the current wrist frame accepted a mapped live untracked endpoint, the hand orientation can consume that same live Quest frame instead of holding a stale rotation while the wrist moves. If the current wrist is held, raw-rejected, body fallback, unmapped, or not position-applied, untracked hand rotation is blocked for that frame. `Tools/CheckWallaceArmSourceGuards.ps1` now guards this policy path. Source guard passed, `TestingKit3Editor` rebuilt successfully, focused `TestingKit3.MediaPipe.QuestWrist.ApplyPolicy` found 1 test and passed, focused `TestingKit3.MediaPipe.Quest` found 13 tests and passed, and the broad `TestingKit3.MediaPipe` filter found 44 tests and completed with exit code 0.
2026-05-20 fresh-held-target attempt gate: the early `DriveArmCS()` reliability/step/segment gate now only treats a held Quest wrist target as available when `FMediaPipeQuestWristApplyPolicy::HasFreshHeldTargetForPositionAttempt()` says it is still inside `mp.QuestWristLostTrackingGraceSeconds`. This removes the split where an expired held target could waive MediaPipe wrist reliability first, then be rejected and cleared later by `TryUseHeldQuestWristTarget()`. `Tools/CheckWallaceArmSourceGuards.ps1` guards the runtime wiring and regression text. Source guard passed, `TestingKit3Editor` rebuilt successfully, focused `TestingKit3.MediaPipe.QuestWrist.ApplyPolicy` passed, and the broad `TestingKit3.MediaPipe` filter found 43 tests and ended with `Automation Test Queue Empty 43 tests performed`.
2026-05-20 calibration/anchor position-continuity reset: `FQuestWristSideRuntimeState::ResetCalibration()` now clears held Quest wrist targets, last accepted live wrist, position filters, startup samples, and position authority through `ResetPositionContinuity()`. The HMD-relative avatar path also calls that reset when it creates the HMD-relative anchor or has to reset the HMD translation filter after a tracking/anchor jump. This matches the OculusXR-style ownership rule that a held endpoint must not cross into a different frame anchor. `Tools/CheckWallaceArmSourceGuards.ps1` guards the runtime calls and regression text. Source guard passed, `TestingKit3Editor` rebuilt successfully, focused `TestingKit3.MediaPipe.QuestWrist` found 4 tests and passed, and the broad `TestingKit3.MediaPipe` filter found 43 tests and ended with `Automation Test Queue Empty 43 tests performed`.
2026-05-20 arm-loss hold / frame-coherent pose-write cleanup: the optional `mp.MediaPipeArmHoldOnQuestHandLoss` path now runs after the Quest wrist endpoint attempt policy, and profile 4 writes mapped/body-fallback Quest wrist arm frames without inherited MediaPipe arm target/rotation half-life or rotation step/speed caps. `Tools/CheckWallaceArmSourceGuards.ps1` passed, `TestingKit3Editor` rebuilt successfully, focused `TestingKit3.MediaPipe.QuestWrist.ApplyPolicy` passed, and the broad `TestingKit3.MediaPipe` filter found 43 tests and completed with exit code 0.
2026-05-20 source-elbow-hint constrained solve correction: HMD-relative profile 4 now converts the original MediaPipe shoulder/elbow/wrist sample into a target-space elbow hint before the Quest endpoint constrained solve, instead of using the avatar's previous/current elbow as the hint. `mp.QuestWristSolve` logs now include `questArmSourceElbowHint` and `questArmSourceElbow`. `Tools\CheckWallaceArmSourceGuards.ps1` passed, `TestingKit3Editor` rebuilt successfully, focused `TestingKit3.MediaPipe.QuestConstrainedArm.TargetSolve` passed with the new source-hint regression, focused `TestingKit3.MediaPipe.QuestConstrainedArm` found 5 tests and passed, focused `TestingKit3.MediaPipe.Quest` found 13 tests and passed, focused `TestingKit3.MediaPipe.Diagnostics.QuestWristRollCompactFormatter` passed, and the broad `TestingKit3.MediaPipe` filter found 44 tests and completed with exit code 0.
2026-05-20 editor Quest hands profile alignment: `ApplyQuestWebcamHandsProfile()` was moved off the old wrist-only profile and onto the current HMD-relative constrained arm defaults used by profile 4, including stable embodied upper-body defaults with `mp.MediaPipeDriveClavicles=0` and `mp.MediaPipeDriveSpine=0`. `Tools\CheckWallaceArmSourceGuards.ps1` now guards the editor command against drifting back to `mp.QuestWristPositionBlend=0.0`, helper-off, smoothed-arm, or MediaPipe clavicle/spine-driving defaults. Source guard passed, `TestingKit3Editor` rebuilt successfully, and the broad `TestingKit3.MediaPipe` filter found 44 tests and completed with exit code 0.

2026-05-20 direct Quest hand / reach-scale / loss-hold default: profile 4 and `ApplyQuestWebcamHandsProfile()` now set tracked Quest-authoritative hand rotation to direct (`mp.QuestHandRotationHalfLife=0.0`, `mp.QuestHandRotationMaxStepDegrees=0.0`), disable the default constrained body fallback (`mp.QuestConstrainedArmBodyFallback=0`), enable last-reliable arm hold on Quest hand loss (`mp.MediaPipeArmHoldOnQuestHandLoss=1`), and enable HMD-relative reach-scale calibration (`mp.QuestConstrainedArmReachScaleCalibration=1`). `FMediaPipeQuestWristApplyPolicy::ComputeReachScaleCalibration()` covers both under-reach and over-reach normalization, and reset coverage verifies the per-side observed reach max clears with position continuity. Source guard passed, `TestingKit3Editor` rebuilt successfully, and the broad `TestingKit3.MediaPipe` filter found 44 tests and completed with exit code 0.
2026-05-20 two-pose arm-length calibration update: profile 4 and `ApplyQuestWebcamHandsProfile()` now enable `mp.QuestArmLengthCalibrationStartup=1`, `mp.QuestArmLengthCalibrationHud=1`, and `mp.QuestArmDownFrameCorrection=1`. VR Preview prompts for full forward reach first, then arms straight down; accepted rows log `mp.QuestArmLengthCalibration: stage=ForwardReach accepted=1` and `stage=DownReach accepted=1`, and wrist traces include `questArmLenCalibStage`, `questArmLenForwardCm`, `questArmLenDownCm`, and `questArmDownFrame`. Validation after this change: `Tools/CheckWallaceArmSourceGuards.ps1` passed, `TestingKit3Editor` rebuilt successfully, and `UnrealEditor-Cmd ... Automation RunTests TestingKit3.MediaPipe` found 44 tests and reached `Automation Test Queue Empty 44 tests performed` in `Saved\Logs\TestingKit3.log` at 2026-05-20 21:52 Perth time. This still needs worn-headset VR Preview/Oculus Mirror confirmation.
2026-05-20 solver continuity reset and semantic roll unwrap: pose timestamp rewinds now reset global pose-yaw and Quest wrist runtime state, and Quest semantic wrist roll uses `FMediaPipeQuestWristApplyPolicy::ContinueAngleDegrees()` so the accumulator is not normalized back through +/-180 before max-twist clamping. `Tools\CheckWallaceArmSourceGuards.ps1` passed, `TestingKit3Editor` rebuilt successfully, focused `TestingKit3.MediaPipe.QuestWrist.ApplyPolicy` passed with wrap-continuity regressions, and the tee-captured full `TestingKit3.MediaPipe` run found 44 tests and recorded 44 successes in `Saved\Logs\TestingKit3_MediaPipe_Automation_20260520_1838.log`.
2026-05-20 worn-headset VR Preview: not yet verified after the diagonal by-thigh solver patch, the solved elbow-plane pose-write correction, the post-finger twist topology guard, the pose-frame dropout continuity patch, the HMD-relative shoulder rollback bypass, the constrained wrist attempt / held-target continuity patch, the stateless standard twist-helper runtime patch, the expired held-target authority reset, the current-frame untracked hand-rotation gate, the fresh-held-target attempt gate, the calibration/anchor position-continuity reset, the arm-loss hold cleanup, the frame-coherent pose-write cleanup, the source-elbow-hint constrained solve correction, the editor Quest hands profile alignment, the solver-continuity reset, or the semantic wrist-roll unwrap
```

Do not call this arm-dropout patch headset-accepted until a new worn-headset VR Preview run confirms that arms-down by the thighs stays extended and does not snap.

2026-05-19 OculusXR-style arm twist/default patch:

- The default arm twist helper pass now uses `Source/MediaPipeDriver/MediaPipeArmTwistSolver.h/.cpp` instead of hardcoded helper percentages.
- `DriveArmTwistBonesCS()` interpolates target twist helpers from the reference parent/source-parent/source chain, constrains the helper to axis-only roll, and writes through `SafeSetCSBoneTransforms()`. This mirrors the important OculusXRMovement behavior: mapped main bones own the arm chain, while unmapped twist helper bones are inferred from the target skeleton and distributed by their projected chain position.
- The 2026-05-20 source-parent correction matters for chained helpers: the solver no longer assumes the helper's immediate parent is also the mapped chain start. Corrective children under `upperarm_correctiveRoot_*` use `upperarm_* -> lowerarm_*` as the source chain, and corrective children under `lowerarm_correctiveRoot_*` use `lowerarm_* -> hand_*`.
- `DriveArmTwistBonesCS()` is stateless for the active standard helper solve. That matches OculusXRMovement: twist helpers are inferred directly from the current component-space parent/source-parent/source frame after the main arm and hand have been solved. The old per-helper temporal smoothing path was removed from this pass because it could lag behind the main solved chain and become a second deformation authority.
- The optional broader MetaHuman sidecar/corrective pass remains available behind `mp.MediaPipeDriveMetaHumanArmHelpers=1`, but it is no longer a profile 4 startup default. It covers `clavicle_out/scap_*`, `clavicle_pec_*`, `upperarm_twistCor_01/02_*`, `upperarm_bicep/tricep_*`, `upperarm_correctiveRoot_*`, `upperarm_bck/fwd/in/out_*`, `lowerarm_correctiveRoot_*`, `lowerarm_in/out/fwd/bck_*`, and `wrist_inner/outer_*`. The 2026-05-20 topology audit found that Oculus-style target-skeleton detection includes all 8 standard twist helpers, those standard helpers are not ancestors of Wallace hand/finger bones, and 20/20 broad MetaHuman corrective helpers remain outside the startup helper scope, so those correctives must stay diagnostic/off until headset evidence proves they help rather than fight the MetaHuman deformation layer.
- If `mp.MediaPipeDriveArmTwistBones=0`, helper runtime state resets and no standard twist-helper transforms are written. If only `mp.MediaPipeDriveMetaHumanArmHelpers=0`, the standard twist helpers remain active and the broader MetaHuman sidecar-helper runtime state resets.
- The direct forearm-roll diagnostic path now ramps forearm roll from zero on first activation instead of snapping its smoothed value straight to the target. This is diagnostic-only because direct Quest wrist-roll ownership remains disabled in the startup default.
- Profile 4 now enables only the standard target-skeleton helper pass with `mp.MediaPipeDriveArmTwistBones=1`; it keeps the broader MetaHuman sidecar-helper pass off with `mp.MediaPipeDriveMetaHumanArmHelpers=0`. The standard pass is source/automation proof only and still needs worn-headset VR Preview acceptance.
- The arms-down hard-repair path is not a profile 4 default: `mp.QuestConstrainedArmDownStraighten=0`, all profile 4 down-straighten numeric budgets are `0.0`, `mp.QuestConstrainedArmMaxReachStepCm=0.0`, and `mp.QuestConstrainedArmCloseReachPoleBias=0.0`. The 48 cm / 94-96.5% relaxed straightening trial was rejected in worn-headset VR Preview because it still made below-body elbow bend too rigid. The current default uses measured startup calibration instead: `mp.QuestArmLengthCalibrationStartup=1` shows a Wallace-only headset/mirror prompt, accepts a stable full-forward reach pose, then accepts a stable arms-down pose only when the corrected down reach is at least 95% of the MetaHuman arm target. Forward reach standardizes the user's Quest reach to Wallace's MetaHuman arm length through `mp.QuestConstrainedArmReachScaleCalibration=1` and `mp.QuestConstrainedArmReachScaleUniform=1`; the arms-down sample drives `mp.QuestArmDownFrameCorrection=1`, which scales the downward component of HMD-relative wrist targets from measured data rather than forcing elbows straight.
- Profile 4 also sets the general constrained-arm reach cap to `mp.QuestConstrainedArmMaxReachFraction=0.997`. This CVar is used before the constrained solve and inside the constrained target solve, so forward/side full extension is no longer silently shortened by the older hidden `0.985` cap. Profile 4 additionally enables `mp.QuestConstrainedArmReachScaleCalibration=1` so high Quest reaches are normalized toward that avatar reach cap instead of assuming the wearer's HMD-relative reach maps 1:1 to Wallace. Non-profile paths still reset to `0.985` unless explicitly changed.
- Profile 4 keeps reach-step continuity disabled with `mp.QuestConstrainedArmMaxReachStepCm=0.0`. Reach-step continuity remains available as a diagnostic for one-frame reach collapse, but it is not accepted as a default because it can block intentional elbow bend. Keep `mp.QuestConstrainedArmElbowHalfLife=0.0` and `mp.QuestConstrainedArmMaxElbowStepCm=0.0`; the accepted default should avoid independent elbow smoothing and should not force target reach.
- Profile 4 now sets the constrained-solve pose-write elbow-plane threshold to `mp.QuestConstrainedArmSolvedPlaneMinSin=0.08`. This applies only when the constrained solve succeeded; direct MediaPipe arm solving keeps using `mp.MediaPipeArmElbowPlaneMinSin`. The lower constrained threshold is deliberate because the solver already owns near-full pole continuity, and a stricter write-time threshold can throw that solved pole away.
- Direct Quest wrist-roll ownership remains off by default: `mp.QuestWristTwistDrivesForearm=0`, `mp.QuestWristForearmRollDriveTwistHelpers=0`, `mp.QuestWristDriveTwistCorrection=0`, and `mp.QuestWristUpperArmRollDriveTwistHelpers=0`.
- Stable embodied mode keeps `mp.MediaPipeDriveClavicles=0` and `mp.MediaPipeDriveSpine=0` by default. Clavicle-driving remains a diagnostic, not accepted startup behavior.
- Validation so far is build plus automation only: `Tools/CheckWallaceArmSourceGuards.ps1` passed after adding the stateless helper, expired held-target authority reset, current-frame untracked hand-rotation gate, fresh-held-target attempt, calibration/anchor position-continuity guards, timestamp-rewind runtime resets, and semantic wrist-roll unwrap guard; `TestingKit3Editor` rebuilt successfully; focused `TestingKit3.MediaPipe.ArmGuardPolicy`, `TestingKit3.MediaPipe.BodySolverMath.SolvedElbowPlaneArmBasis`, `TestingKit3.MediaPipe.QuestConstrainedArm.FullMotionSweep`, `TestingKit3.MediaPipe.QuestConstrainedArm`, `TestingKit3.MediaPipe.ArmTwist.OculusStyleInterpolation`, `TestingKit3.MediaPipe.MetaHumanArmHelpers`, `TestingKit3.MediaPipe.PoseFrameContinuity.HoldLastFrameOnDropout`, `TestingKit3.MediaPipe.QuestWrist`, `TestingKit3.MediaPipe.Quest`, and `TestingKit3.MediaPipe.Runtime.CVars` passed; the stale-history branch-repair regression, wrong-current move-cap regression, and diagonal by-thigh straightening regression are covered in `TestingKit3.MediaPipe.QuestConstrainedArm.TargetSolve`; the fallback continuity side guard is covered in `TestingKit3.MediaPipe.QuestConstrainedArm.BodyFallback`; the pose-write basis boundary is covered in `TestingKit3.MediaPipe.BodySolverMath.SolvedElbowPlaneArmBasis`; the HMD-relative shoulder rollback bypass is covered in `TestingKit3.MediaPipe.ArmGuardPolicy.ShoulderRollback`; the post-finger standard-helper write order and default helper scope are covered by `TestingKit3.MediaPipe.MetaHumanArmHelpers.OculusStyleDefaultScope`; the transient MediaPipe-frame/raw-hand dropout hold is covered by `TestingKit3.MediaPipe.PoseFrameContinuity.HoldLastFrameOnDropout`; the constrained wrist attempt, held-target continuity, expired held-target authority reset, current-frame untracked hand-rotation gate, fresh-held-target attempt, calibration/anchor position-continuity policy, and semantic roll +/-180 wrap continuity are covered by `TestingKit3.MediaPipe.QuestWrist.ApplyPolicy`; and the broad `TestingKit3.MediaPipe` filter found 44 tests and recorded 44 successes in `Saved\Logs\TestingKit3_MediaPipe_Automation_20260520_1838.log`. This still needs worn-headset VR Preview/Oculus Mirror confirmation.

Do not treat `Docs/WALLACE_QUEST_VR_ARM_ROLLBACK_ANALYSIS_2026-05-17.md` as the active default anymore. It is a historical rollback note and an explicit escape hatch only if the user asks to return to profile 0 / mode 0.

Expected latest startup profile line:

```text
Auto Quest profile applied: armProfile=4 stableBody=1 ... clavicles=0 spine=0 armIK=0 forceArmIK=0 legs=0 legIK=0 pelvisTranslation=0 questArmMode=3 questPalmMode=2 wristBlend=1.00 wristGrace=0.35 wristRequireTracked=1 ... reachAssist=1 driftGuard=1 constrainedArmSolve=1 constrainedArmBodyFallback=0 armHoldLoss=1 ... reachScale=1 reachScaleMinObs=0.88 reachScaleMin=0.82 reachScaleMax=1.18 wristFilter=1 armMaxReach=0.997 armPlaneMinSin=0.080 armReachStepCm=6.0 downStraightenDiagnostic=0 downStraighten=1 downStraightenMaxCm=18.0 downStraightenReachFloor=0.997 downStraightenReachFrac=0.997 armTwistBones=1 metaHumanArmHelpers=0 armElbowHL=0.00 armElbowStep=0.0 armTargetHL=0.00 armRotHL=0.00 handRotHL=0.00 handRotStep=0.0 handRotGrace=0.20 wristMaxRel=82.0
```

After the user starts worn-headset VR Preview, the agent-side log gate is:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\Tools\CheckWallaceQuestVrEmbodimentLog.ps1 -LogPath .\Saved\Logs\TestingKit3.log -RequireWornHeadsetTrace
```

The script now expects the HMD-relative avatar sync path: `armProfile=4`, `questArmMode=3`, `wristRequireTracked=1`, `positionApplied=1`, `requireTrackedApply=1`, `requestedBlend=1.00`, `calib=HMD_AVATAR`, tracked Quest hand evidence, and `mp.MetaHumanArmSanity` rows without `broken=1`. It also fails the headset proof if no non-body-fallback Wallace constrained arm row proves the current source-elbow-hint path with `questArmSolve=1`, `questArmBodyFallback=0`, `questArmSourceElbowHint=1`, and `questArmSourceElbow=...`. It now requires the Wallace presentation mesh proof line to show `asset=m_med_unw_body`, `postProcessClass=...m_med_unw_animbp_Cinematic...`, and `postProcessDisabled=0`, because arm skin deformation cannot be evaluated as MetaHuman-corrective-valid if the presentation component disables the body post-process AnimBP. It warns when `requireTrackedApply=1`, `questTracked=0`, `untrackedData=1`, and `positionApplied=1` appear together, because those rows are valid only when they are produced by the constrained continuity policy.

For the 2026-05-20 loss-handling default, hand-loss/occlusion moments should prefer fresh held Quest endpoints and then last-reliable arm hold. `questArmBodyFallback=1` should not be the profile 4 startup default; if it appears, treat it as a deliberate diagnostic or stale profile mismatch, not the current accepted default.

For snapping/twitch evidence after a headset run, use the stricter arm-sanity analyzer gate:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\Tools\AnalyzeWallaceArmTwitchLog.ps1 -LogPath .\Saved\Logs\TestingKit3.log -Actor MP_LiveMetaHumanWallace -AfterLastVrPreview -RequireRows -FailOnSpikes -FailOnBroken
```

That analyzer now exits non-zero when required `mp.MetaHumanArmSanity` rows are missing, when frame-to-frame arm spikes exceed the configured thresholds, or when any `broken=1` rows are present.

Before changing arm defaults again, run the source guard:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\Tools\CheckWallaceArmSourceGuards.ps1
```

It catches the common drift that made prior patches appear blocked by another path: profile 4 no longer being mode 3, tracked-required with only the continuity-gated position exception, fallback-enabled, editor hand setup diverging from tracked-required defaults, helper-bone ownership dropping the lower-arm wrist helpers again, raw component-space writes replacing `SafeSetCSBoneTransforms()`, or `DriveArmTwistBonesCS()` running before the mapped arm chain is solved.

Do not restore older Manny-only, visible-calibration, finger-only, IK-on, twist-helper-on, profile-0 rollback, or mode-2 constrained-only baselines unless the user explicitly asks for that exact rollback. Older notes may describe prior experiments; this file records the current defaults.

Critical guardrail: read `Docs/WALLACE_QUEST_VR_EMBODIMENT_GUARDRAILS.md` before changing embodied camera, pawn, HMD origin, Wallace placement, or Wallace arm-space code. The 2026-05-16 regression came from mixing the Third Person pawn camera, OpenXR tracking-origin space, and Wallace's MetaHuman local `+Y` face axis.

## 2026-05-19 Arm Roll / Helper Bone Diagnostic State

The current runtime startup default prioritizes arm extension, keeps direct Quest wrist-roll experiments out of the default path, enables the standard target-skeleton twist-helper pass, and keeps the broader MetaHuman corrective/sidecar layer off:

```text
mp.AutoQuestArmRollDiagnostic=0
mp.AutoQuestStandardArmTwistDiagnostic=0
mp.MediaPipeDriveArmTwistBones=1
mp.MediaPipeDriveMetaHumanArmHelpers=0
mp.QuestWristTwistDrivesForearm=0
mp.QuestWristForearmRollDriveTwistHelpers=0
mp.QuestWristDriveTwistCorrection=0
mp.QuestWristUpperArmRollDriveTwistHelpers=0
mp.QuestWristUpperArmTwistBlend=0.0
```

The standard helper pass is not the same as direct Quest wrist-roll ownership. It infers standard twist-helper transforms from the solved target skeleton in the current component-space frame, with no separate helper smoothing history. If the standard helper inference visually regresses in VR, the quick isolation switch is `mp.MediaPipeDriveArmTwistBones=0`. The broader MetaHuman sidecar layer remains a deliberate diagnostic switch via `mp.MediaPipeDriveMetaHumanArmHelpers=1`; direct wrist-roll ownership must remain off unless explicitly testing it.

The arms-down straightening correction is not allowed to depend on MediaPipe torso-basis validity anymore. Torso basis is still used when present for pole orientation and diagnostics, but profile 4 can now keep a downward Quest wrist endpoint near full extension even when the webcam torso basis is absent. This was added because the user-visible failure was bent arms by the thighs, and that is exactly the pose where the body camera can lose useful torso/arm information while Quest still knows the wrist endpoint.

The 2026-05-20 AlanMovement/OculusXRMovement source cross-check confirmed the core ownership model: OculusXR maps the tracked wrist to `hand_l` / `hand_r`, leaves the wrist-twist source joints unmapped, builds the mapped frame pose first, and only then interpolates twist helper joints from the target skeleton. TestingKit3 follows that policy in profile 4 for the standard twist helpers: main arm and hand bones solve first, then standard twist helpers are inferred after that solve. The broader MetaHuman corrective helpers are no longer included in the startup default because the topology audit showed they are outside the Oculus-style startup helper scope.

The visible remaining complaint is MetaHuman arm roll deformation, especially the forearm candy-wrapper spot. This has not been headset-accepted as fixed.

What was actually inspected:

```text
Skeleton: /Game/MetaHumans/Common/Female/Medium/NormalWeight/Body/metahuman_base_skel.metahuman_base_skel
Mesh: /Game/MetaHumans/Wallace/Body/m_med_unw_body.m_med_unw_body
Post process ABP: /Game/MetaHumans/Common/Male/Medium/UnderWeight/Body/m_med_unw_animbp_Cinematic.m_med_unw_animbp_Cinematic_C
Default animating rig: /Game/MetaHumans/Common/Common/MetaHuman_ControlRig.MetaHuman_ControlRig
```

Relevant helper/corrective hierarchy found on both sides:

```text
clavicle_* -> upperarm_*, clavicle_out_*, clavicle_scap_*
spine_05 -> clavicle_pec_*
upperarm_* -> lowerarm_*, upperarm_twist_01_*, upperarm_twist_02_*, upperarm_correctiveRoot_*
upperarm_twist_01_* -> upperarm_twistCor_01_*
upperarm_twist_02_* -> upperarm_tricep_*, upperarm_bicep_*, upperarm_twistCor_02_*
upperarm_correctiveRoot_* -> upperarm_bck_*, upperarm_fwd_*, upperarm_in_*, upperarm_out_*
lowerarm_* also has lowerarm_twist_01/02_* plus MetaHuman wrist/lowerarm corrective families
```

Current startup code directly drives the main arm chain and the standard twist helpers `upperarm_twist_01/02_*` and `lowerarm_twist_01/02_*`. The named MetaHuman sidecar helper/corrective families still have an optional interpolation path behind `mp.MediaPipeDriveMetaHumanArmHelpers=1`, but profile 4 leaves that path off. `TestingKit3.MediaPipe.MetaHumanArmHelpers.OculusStyleDefaultScope` proves why: the Oculus-style detector finds 8/8 standard twist helpers and leaves 20/20 broad MetaHuman corrective helpers outside the startup helper scope.

Failed headset trial:

```text
mp.QuestWristUpperArmRollDriveTwistHelpers=1
mp.QuestWristUpperArmTwistBlend=0.18
mp.QuestWristUpperArmMaxTwistDegrees=24.0
mp.QuestWristTwistDrivesForearm=0
mp.QuestWristForearmRollDriveTwistHelpers=0
mp.QuestWristDriveTwistCorrection=0
```

The first direct upper-arm helper trial was not accepted. The user reported that it turned into snapping rather than smooth motion. Log analysis of that failed run showed raw wrist-roll discontinuity: Wallace right-side `limitedTwistDeg` jumped by up to about `340.0` deg between logged samples, which produced unsmoothed upper-arm helper steps up to about `16.0` deg on `upperarm_twist_01_r` and `32.0` deg on `upperarm_twist_02_r`.

The later 2026-05-19 blocked/snapping trial proved the diagnostic was no longer being reset away: the VR Preview profile line contained `armRollDiagnostic=1`, `armTwistBones=1`, and `upperArmRollDrive=1`. The remaining source problems were different:

- `TryFadeQuestWristToMediaPipe()` was applying the total lost-tracking fade to the already-faded wrist offset every frame, so the held wrist target could collapse much faster than `mp.QuestWristLostTrackingGraceSeconds=0.35`.
- Profile 4 had the constrained elbow guard disabled (`armElbowHL=0.00`, `armElbowStep=0.0`), so the elbow solve could accept those rapid target changes directly. A follow-up trial with `armElbowHL=0.06` / `armElbowStep=4.0` was rejected in worn-headset VR Preview because it made the biceps feel locked and prevented full arm extension; keep those values off by default.
- Enabling `mp.QuestWristUpperArmRollDriveTwistHelpers=1` made the Quest roll path own `upperarm_twist_01/02_*`, but the first implementation replaced the normal upper-arm twist distribution with a small Quest-only roll value. The rebuilt path preserves the normal upperarm-to-lowerarm twist and adds the bounded Quest wrist-roll share on top.

The rebuilt diagnostic path in `Source/MediaPipeDriver/MediaPipePoseDrivenAnimInstance_QuestHandRotation.inl` now smooths and rate-limits `mp.QuestWristUpperArmRollDriveTwistHelpers` through per-side solver state:

```text
Source/MediaPipeDriver/MediaPipePoseDrivenSolverState.h
FMediaPipeQuestHandSolverState::bHasSmoothedQuestUpperArmTwist
FMediaPipeQuestHandSolverState::SmoothedQuestUpperArmTwistDeg
```

It also adds log fields:

```text
upperArmTargetDeg
upperArmAppliedDeg
upperArmStepDeg
upperArmMaxStepDeg
upperArmRateClamp
```

This smoothed upper-arm helper path is a diagnostic trial only until a worn-headset VR Preview confirms it moves smoothly and improves the deformation. The source now preserves the base upper-arm twist while the diagnostic is enabled, but it is still not an accepted default without headset evidence.

To run the smoothed upper-arm trial through Auto Quest/VR Preview startup without having the profile reset block it, arm this before VR Preview:

```text
mp.AutoQuestArmRollDiagnostic 1
```

When armed, Auto Quest should log:

```text
armRollDiagnostic=1
armTwistBones=1
upperArmRollDrive=1
upperArmRollBlend=0.18
upperArmRollMax=24.0
```

If those fields are absent or zero, the run did not test the upper-arm diagnostic. `mp.AutoQuestArmRollDiagnostic=0` is the accepted default.

## Current Intent

- Default avatar is MetaHuman Wallace with internal Manny retained as the driver/helper actor.
- Quest/OpenXR owns hand orientation, fingers, and the wrist endpoint used for forward arm reach.
- MediaPipe aids the arm with webcam shoulder/elbow/body intent. The current startup path keeps lower body, pelvis, spine, and clavicles out of the Quest-driven embodiment so the arm endpoint solve can be tested without extra shoulder-root ownership.
- Current best camera checkpoint is embodied view plus `mp.AutoQuestEmbodiedAnchorMode=1`: startup captures yaw from the settled HMD recenter moment, stable mode does not apply HMD-position pawn shoves, and Wallace/Manny follow horizontal room-scale headset motion after startup recenter so the body remains under the wearer while walking.
- 2026-05-17 anti-bump update: wake recenter now uses horizontal HMD error only, so a raw headset Z jump cannot trigger a second yaw/origin reset. Room-scale follow also keeps an 8 cm horizontal deadband and caps each follow step at 12 cm so normal head sway or a one-frame tracking snap does not shove Wallace.
- Current arm default is profile 4 / `mp.QuestArmMode=3`, not the rollback baseline. The latest headset report after the SafeSetCS rebuild is that arms move forward with the Quest hands again.
- Current finger default is `segmentDirection` with parent-chain retargeting and distal/tip damping. Keep `mp.QuestFingerJointRetarget=0` and `mp.QuestFingerCurlOnly=0` unless deliberately testing a fallback.
- The current drift complaint is specifically arm drift/lag while the user moves or extends their arms. Do not reinterpret that as walking/body-follow drift unless a fresh headset run says so.
- The embodied mirror is now off by default because the mirror was not useful enough to justify its measured render and input-latency cost. Re-enable only for targeted mirror testing with `mp.AutoQuestEmbodiedMirror 1`.
- `mp.AutoQuestEmbodiedStableBody=1` is the default. This disables MediaPipe spine/lower-body driving and also keeps clavicles off by default until clavicle contribution is separately accepted in VR Preview.
- Arm IK is off.
- A reversible shoulder-rollback guard is on for direct MediaPipe arm targets that go behind the torso during the observed arm-flip failure, but it no longer wraps a successful profile 4 constrained Quest arm solve. Quick live revert for direct MediaPipe rollback testing: `mp.MediaPipeShoulderRollbackGuard 0`.
- Visible calibration is off.
- Wrist calibration is non-blocking.
- 2026-05-19 forearm roll distribution trial failed headset validation: it made the arms snappy while the same forearm candy-wrapper spot remained. The current default is back to `mp.QuestWristTwistDrivesForearm=0`, `mp.QuestWristForearmRollDriveTwistHelpers=0`, and legacy `mp.QuestWristDriveTwistCorrection=0`.
- 2026-05-19 direct upper-arm roll helper trial also failed in its first unsmoothed form: it pushed raw wrist-roll discontinuities into `upperarm_twist_01/02_*` and caused snapping. The rebuilt smoothed/rate-limited version remains diagnostic-only, not a startup default.
- Quest hand comparison is off by default again. Re-enable it only for a targeted diagnosis because the log/HUD/skeleton diagnostic was useful for the palm proof but adds noise to normal embodied testing.

## Source Anchors

- Runtime Auto Quest profile: `Source/MediaPipeDriver/MediaPipeDriver.cpp`
  - `ApplyAutoQuestProfile()` sets the default VR Preview values.
  - `mp.AutoQuestArmReachAssistProfile=4` is the current default and maps startup to `mp.QuestArmMode=3`.
  - `mp.AutoQuestEmbodiedStableBody=1` keeps low-Hz MediaPipe trunk and lower-body motion from fighting Quest tracking by applying `mp.MediaPipeDriveClavicles=0`, `mp.MediaPipeDriveSpine=0`, and lower-body/pelvis driving off.
  - `mp.AutoQuestEmbodiedAnchorMode=1` is the current embodied default. It uses a stable embodied station plus bounded settled-HMD yaw recenter, then applies horizontal-only room-scale follow from the HMD so Wallace remains under the wearer while walking. It does not use raw HMD Z, does not apply HMD-position pawn correction, and does not run the old recurring mirror camera pin. `0` is the legacy fixed-station/recurring-mirror-pin comparison path; `2` is the experimental raw live-HMD chase path and is not the default.
  - Wallace embodiment is not actor-`+X` forward. `mp.AutoQuestEmbodiedWallaceYawOffsetDeg=-90.0` aligns Wallace's visible MetaHuman face/chest axis, local `+Y`, to the HMD/view yaw. The anim mapper must use the matching Wallace face-forward path.
  - `mp.AutoQuestEmbodiedMirror=0` is now the default; the planar reflection mirror remains reversible but is not part of the current best performance checkpoint.
  - `mp.AutoQuestStationTimerIntervalSeconds`, `mp.AutoQuestStationRefreshIntervalSeconds`, and `mp.AutoQuestCameraPinIntervalSeconds` throttle the expensive station/avatar/mirror refresh path while keeping camera pinning separately tunable.
  - `mp.AutoQuestEmbodiedRoomScaleFollow=1` lets stable mode translate Wallace/Manny horizontally under room-scale HMD walking after the startup recenter. `mp.AutoQuestEmbodiedRoomScaleMaxOffsetCm=400.0` rejects huge tracking-origin jumps; raw HMD Z is deliberately ignored.
  - `mp.AutoQuestEmbodiedRoomScaleDeadbandCm=8.0` and `mp.AutoQuestEmbodiedRoomScaleMaxStepCm=12.0` are the current anti-bump defaults. The room-scale log should include `appliedCm`, `deadband`, `maxStep`, and `capped`; recenter logs should include `horizontalErrorBefore` and `rawZErrorBefore`.
  - `mp.AutoQuestMediaPipeStats` and `mp.AutoQuestMediaPipeStatsHud` expose readback, native MediaPipe, queue, conversion, publish-rate, overwrite, and gate-skip diagnostics without enabling the heavier wrist/torso traces.
  - `ApplyAutoQuestVrPerformanceProfile()` applies the balanced Quest headset render profile at Auto Quest startup and re-applies it once per second from the mirror-station refresh path so OpenXR/VR Preview startup cannot restore full-resolution editor values.
  - `ApplyAutoQuestMetaHumanQualityProfile()` pins Wallace LODSync to the balanced MetaHuman LOD instead of allowing the VR profile to collapse Wallace into coarse body/face LODs.
  - Recurring diagnostic logs default off for VR Preview performance; re-enable with `mp.QuestWristTrace=1`, `mp.MediaPipeTorsoDebug=1`, or `mp.AutoQuestMirrorDebug=1` when diagnosing. `mp.QuestWristTrace` is now preserved across Auto Quest startup, so it can be armed before the headset test instead of being reset to `0`.
- Runtime CVar defaults and wrist/forearm solve:
  - `Source/MediaPipeDriver/MediaPipePoseDrivenAnimInstance.cpp`
  - `Source/MediaPipeDriver/MediaPipeRuntimeCVars.h`
  - `Source/MediaPipeDriver/MediaPipeRuntimeCVars.cpp`
  - `Source/MediaPipeDriver/MediaPipePoseDrivenSolverState.h`
  - `Source/MediaPipeDriver/MediaPipePoseDrivenAnimInstance_QuestSpaceMapping.inl`
  - `Source/MediaPipeDriver/MediaPipePoseDrivenAnimInstance_QuestHandRotation.inl`
  - `Source/MediaPipeDriver/MediaPipePoseDrivenAnimInstance_QuestFingerDrive.inl`
  - `Source/MediaPipeDriver/MediaPipePoseDrivenAnimInstance_QuestArmSolve.inl`
  - `mp.QuestArmMode` is the user-facing arm mode switch: `0=historical rollback, MediaPipe wrist authority with Quest hands only`, `1=legacy adaptive reach assist`, `2=Quest-constrained calibrated wrist solve`, `3=current HMD-relative Quest wrist endpoint in avatar space with MediaPipe shoulder/elbow hints`.
  - `mp.QuestHandRotationBlend` default is `1.0`.
  - `mp.QuestWristUseJointRotation` default is `1`, but `mp.QuestPalmMode=2` now suppresses the OpenXR wrist joint quaternion for the final hand orientation so the visible palm landmark geometry remains authoritative.
  - `mp.QuestPalmMode=2` is the current confirmed palm/hand orientation fix. The 2026-05-16 17:19 headset trace showed the Quest hand target was finally exact (`handOnlyToRetargetDeg=0.0`), but later headset testing still showed palms-down appearing sideways. Two causes were found: Wallace hand Z is across the palm rather than the visible palm normal, and the OpenXR wrist joint quaternion could override the palm landmark basis. Mode `2` now uses a visual palm basis built from the hand/index/middle/pinky bones, keeps Quest landmark geometry authoritative for hand orientation, and honors the local hand-rotation filter while keeping the existing arm/wrist position solve. A worn-headset VR Preview on 2026-05-16 confirmed palms now rotate as expected. Mode `0` is the quick revert; mode `1` was a failed/shaky legacy fallback and is not a default.
  - `mp.QuestWristPositionAdaptiveFilter` filters the Quest wrist correction vector in the active profile 4 / `mp.QuestArmMode=3` path.
  - `mp.QuestArmMode=3` uses the HMD-relative avatar-space Quest wrist endpoint so forward reach is in the same embodied space as Wallace instead of raw world/OpenXR space.
  - The accepted Quest finger default is the `segmentDirection` path. `mp.QuestFingerJointRetarget=0` and `mp.QuestFingerCurlOnly=0` must stay at startup so live Quest segment directions drive the target hierarchy from the current live parent delta.
  - Distal/tip twist damping is part of the default segment-direction solve and is weighted by `mp.QuestFingerCurlDistalScale` / `mp.QuestThumbCurlDistalScale`.
  - `mp.QuestWristTwistDrivesForearm=0` is the current default. The bounded lowerarm-main roll trial produced snappy arms and did not fix the visible MetaHuman forearm wrap, so do not re-enable it as a default.
  - `mp.QuestWristUpperArmRollDriveTwistHelpers=0` is the current default. If enabled for a diagnostic trial, the current implementation smooths and rate-limits direct Quest wrist roll before applying it to `upperarm_twist_01/02_*`, and it preserves the normal upperarm-to-lowerarm twist instead of replacing it; logs must show bounded `upperArmStepDeg` / `upperArmMaxStepDeg` before treating the motion as valid.
  - `mp.AutoQuestArmRollDiagnostic=1` is the deliberate startup-safe way to test the smoothed direct upper-arm wrist-roll path in VR Preview. The standard twist pass is already on; the diagnostic adds `mp.QuestWristUpperArmRollDriveTwistHelpers=1` and related bounded-roll settings. Keep it `0` unless intentionally testing direct wrist-roll ownership.
- 2026-05-19 AlanMovement/OculusXRMovement cross-check: `BodyLeftHandWrist` maps to `hand_l/hand_r`, `BodyLeftHandWristTwist` / `BodyRightHandWristTwist` are unmapped, and twist helpers are inferred from the target skeleton rather than directly driven as source wrist-roll bones. TestingKit3 now follows that policy in the default path: direct Quest wrist roll stays out of forearm/upper-arm helper ownership, while `FMediaPipeArmTwistSolver` distributes standard target-skeleton twist helpers from the solved parent/source chain without an extra helper smoothing layer.
  - `ApplyRotationCS`, `ApplyTranslationDeltaCS`, `DriveArmTwistBonesCS`, and `DrivePelvisTranslationCS` must keep using `SafeSetCSBoneTransforms()` for component-space writes. Raw `SetComponentSpaceTransform()` writes can leave child wrist/hand bones stale after parent arm motion and reproduce the contorted/static-arm failure.
  - HMD pose is read in `PreUpdate` on the game thread and cached for `Evaluate_AnyThread`; do not reintroduce direct `GEngine->XRSystem->GetCurrentPose(...)` calls from the animation evaluation path.
  - `mp.QuestConstrainedArmSolve` and `mp.QuestWristDriftGuard` are internal mode implementation CVars. Prefer switching `mp.QuestArmMode`, not these, during headset testing.
  - `mp.QuestWristDriveTwistCorrection` default is `0`.
  - Wallace's body skeleton has MetaHuman-specific deformation bones that are not the same as the standard Manny twist helpers: `clavicle_out/scap/pec_*`, `upperarm_correctiveRoot_*`, `upperarm_fwd/bck/in/out_*`, `upperarm_bicep/tricep_*`, `wrist_inner_*`, `wrist_outer_*`, `lowerarm_correctiveRoot_*`, and `lowerarm_out/in/fwd/bck_*`. The failed forearm/upper-arm roll trials touched the main arm chain and/or `upperarm_twist_01/02_*` / `lowerarm_twist_01/02_*`, so the remaining candy-wrapper spot should be treated as a MetaHuman corrective/deformation-layer issue until proven otherwise.
- The standard arm twist helper path is active in the profile 4 startup default with `mp.MediaPipeDriveArmTwistBones=1`. It is stateless per frame and remains separate from direct Quest wrist roll: keep `mp.QuestWristTwistDrivesForearm=0`, `mp.QuestWristForearmRollDriveTwistHelpers=0`, and `mp.QuestWristDriveTwistCorrection=0`.
  - `mp.AutoQuestHandCompareMode=0` keeps the read-only Quest hand comparison diagnostic off at Auto Quest startup.
- Anim node struct default: `Source/MediaPipeDriver/MediaPipePoseDrivenAnimInstance.h`
  - `QuestHandRotationBlend = 1.0f`.
- Manual Quest webcam command profile: `Source/MediaPipeDriverEditor/MediaPipeLiveVideoCommands.cpp`
  - `ApplyQuestWebcamHandsProfile()` is aligned with the post-trial default: `mp.QuestWristTwistDrivesForearm=0`, `mp.QuestWristForearmTwistBlend=0.0`, `mp.QuestWristForearmRollDriveTwistHelpers=0`, and legacy `mp.QuestWristDriveTwistCorrection=0`.

## Current Verified CVars

These are the current profile 4 / mode 3 values confirmed by non-visual PIE
readback. They are not a worn-headset acceptance result.

### User-Facing Arm Mode

Use this during headset testing:

```text
mp.QuestArmMode 0  # historical rollback: MediaPipe wrist authority with Quest hands/fingers only
mp.QuestArmMode 1  # legacy adaptive Quest wrist reach-assist path
mp.QuestArmMode 2  # Quest-constrained calibrated wrist solve
mp.QuestArmMode 3  # current default: HMD-relative avatar Quest wrist endpoint with MediaPipe shoulder/elbow hints
```

`mp.QuestArmMode=3` is the current default. The lower-level `mp.QuestWristReachAssist*`, `mp.QuestWristDriftGuard*`, and `mp.QuestConstrainedArmSolve*` CVars remain available for diagnosis, but they are not the primary interface. `mp.AutoQuestArmReachAssistProfile=4` maps startup to mode `3`.

```text
mp.AutoQuestAvatar=1
mp.AutoQuestWebcamHands=1
mp.AutoQuestWebcamHandsHz=30
mp.AutoQuestWebcamHandsInputMaxDimension=512
mp.MediaPipeInputMaxDimension=512
mp.QuestArmMode=3
mp.AutoQuestArmReachAssistProfile=4
mp.AutoQuestMirrorLockMannyYaw=1
mp.AutoQuestEmbodiedView=1
mp.AutoQuestEmbodiedAnchorMode=1
mp.AutoQuestEmbodiedRoomScaleFollow=1
mp.QuestHandDriveFingerBones=1
mp.QuestFingerJointRetarget=0
mp.QuestFingerCurlOnly=0
mp.QuestFingerPreserveSpread=0
mp.QuestFingerUseChainCurl=1
mp.QuestFingerRotationHalfLife=0.035
mp.QuestFingerCurlProximalScale=0.82
mp.QuestFingerCurlIntermediateScale=1.00
mp.QuestFingerCurlDistalScale=0.58
mp.QuestThumbCurlProximalScale=0.55
mp.QuestThumbCurlIntermediateScale=0.95
mp.QuestThumbCurlDistalScale=0.70
mp.AutoQuestEmbodiedRoomScaleDeadbandCm=8.0
mp.AutoQuestEmbodiedRoomScaleMaxStepCm=12.0
mp.AutoQuestEmbodiedRoomScaleMaxOffsetCm=400.0
mp.AutoQuestEmbodiedStableBody=1
mp.AutoQuestEmbodiedMirror=0
mp.AutoQuestEmbodiedEyeHeightCm=162.0
mp.AutoQuestEmbodiedCameraForwardOffsetCm=0.0
mp.AutoQuestEmbodiedWallaceYawOffsetDeg=-90.0
mp.AutoQuestEmbodiedMirrorDistanceCm=220.0
mp.AutoQuestStationTimerIntervalSeconds=0.033
mp.AutoQuestStationRefreshIntervalSeconds=0.25
mp.AutoQuestCameraPinIntervalSeconds=0.033
mp.AutoQuestMediaPipeStats=0
mp.AutoQuestMediaPipeStatsHud=0
mp.AutoQuestMediaPipeStatsIntervalSeconds=1.0
mp.AutoQuestHandCompareMode=0

mp.QuestHandTracking=1
mp.QuestHandDriveFingerBones=1
mp.QuestHandRotationBlend=1.0
mp.QuestHandRotationHalfLife=0.0
mp.QuestHandRotationMaxStepDegrees=0.0
mp.QuestHandRotationMaxDeltaFromMediaPipeDegrees=180.0
mp.QuestHandRotationLostTrackingGraceSeconds=0.20
mp.QuestHandRotationLostTrackingFadeSeconds=0.75

mp.QuestWristPositionBlend=1.0
mp.QuestWristRelativeCalibration=1
mp.QuestWristUseBasisDelta=1
mp.QuestWristUseJointRotation=1
mp.QuestWristUseJointRotationLeft=1
mp.QuestWristUseJointRotationRight=1
mp.QuestWristForceArmIK=0
mp.QuestWristPositionScale=1.0
mp.QuestWristMaxRelativeDeltaCm=82.0
mp.QuestWristMaxOffsetCm=140.0
mp.QuestWristRawMaxDistanceCm=220.0
mp.QuestWristLostTrackingGraceSeconds=0.35
mp.QuestWristRequireTrackedForApply=1
mp.QuestWristReachAssist=1
mp.QuestWristReachAssistBlend=0.48
mp.QuestWristReachAssistMaxElbowMoveCm=24.0
mp.QuestWristDriftGuard=1
mp.QuestWristDriftGuardStartCm=18.0
mp.QuestWristDriftGuardFullCm=55.0
mp.QuestWristDriftGuardReachBlendBoost=0.35
mp.QuestWristDriftGuardExtraElbowMoveCm=18.0
mp.QuestWristDriftGuardPoleBlend=0.85
mp.QuestConstrainedArmSolve=1
mp.QuestConstrainedArmSolveBlend=1.0
mp.QuestConstrainedArmWristAuthority=1.0
mp.QuestConstrainedArmMediaPipeElbowHint=0.20
mp.QuestConstrainedArmStablePoleDown=0.25
mp.QuestConstrainedArmMaxReachFraction=0.997
mp.QuestConstrainedArmSolvedPlaneMinSin=0.08
mp.QuestConstrainedArmMaxReachStepCm=0.0
mp.QuestConstrainedArmMaxElbowMoveCm=65.0
mp.QuestConstrainedArmElbowHalfLife=0.0
mp.QuestConstrainedArmMaxElbowStepCm=0.0
mp.QuestConstrainedArmDownStraighten=0
mp.QuestConstrainedArmDownStraightenThresholdCm=0.0
mp.QuestConstrainedArmDownStraightenMaxCm=0.0
mp.QuestConstrainedArmDownStraightenMinBelowShoulderRatio=0.0
mp.QuestConstrainedArmDownStraightenReachFloorFraction=0.0
mp.QuestConstrainedArmDownStraightenMaxReachFraction=0.0
mp.QuestConstrainedArmReachScaleCalibration=1
mp.QuestConstrainedArmReachScaleUniform=1
mp.QuestConstrainedArmReachScaleMinObservedFraction=0.88
mp.QuestConstrainedArmReachScaleApplyStartFraction=0.0
mp.QuestConstrainedArmReachScaleApplyFullFraction=1.0
mp.QuestConstrainedArmReachScaleMin=0.82
mp.QuestConstrainedArmReachScaleMax=1.18
mp.QuestArmLengthCalibrationStartup=1
mp.QuestArmLengthCalibrationHud=1
mp.QuestArmLengthCalibrationHoldSeconds=2.5
mp.QuestArmLengthCalibrationStableFrames=20
mp.QuestArmLengthCalibrationMaxHandVelocityCmSec=30.0
mp.QuestArmLengthCalibrationForwardMinReachFraction=0.88
mp.QuestArmLengthCalibrationDownMinBelowShoulderFraction=0.40
mp.QuestArmLengthCalibrationDownMinVerticalDominance=0.65
mp.QuestArmLengthCalibrationDownMinCorrectedReachFraction=0.95
mp.QuestArmDownFrameCorrection=1
mp.QuestArmDownFrameCorrectionMaxScale=1.80
mp.QuestWristPositionAdaptiveFilter=1
mp.QuestWristPositionFilterStillHalfLife=0.11
mp.QuestWristPositionFilterMovingHalfLife=0.018
mp.QuestWristPositionFilterSpeedForMinLag=120.0
mp.QuestWristPositionFilterDeadbandCm=0.65
mp.QuestWristPositionFilterResetDistanceCm=45.0

mp.QuestWristTwistBlend=1.0
mp.QuestWristSwingBlend=1.0
mp.QuestWristMaxTwistDegrees=170.0
mp.QuestWristMaxSwingDegrees=140.0
mp.QuestWristSemanticRollMinPalmProjection=0.45
mp.QuestPalmMode=2

mp.QuestWristTwistDrivesForearm=0
mp.QuestWristForearmTwistBlend=0.0
mp.QuestWristForearmMaxTwistDegrees=55.0
mp.QuestWristForearmRollDriveTwistHelpers=0
mp.QuestWristUpperArmRollDriveTwistHelpers=0
mp.QuestWristDriveTwistCorrection=0
mp.QuestWristTwistCorrectionBlend=0.0
mp.QuestWristTwistCorrectionMaxDegrees=35.0

mp.QuestWristCalibrationGate=0
mp.QuestWristCalibrationHud=0
mp.QuestWristRequireNeutralCalibration=0

mp.MediaPipeUseArmIK=0
mp.MediaPipeArmTargetHalfLife=0.0
mp.MediaPipeArmRotationHalfLife=0.0
mp.MediaPipeArmRotationMaxStepDegrees=0.0
mp.MediaPipeArmRotationMaxSpeedDegreesPerSecond=0.0
mp.MediaPipeArmHoldOnQuestHandLoss=1
mp.MediaPipeDriveHandRotation=0
mp.MediaPipeDriveClavicles=0
mp.MediaPipeDriveSpine=0
mp.MediaPipeDriveArmTwistBones=1
mp.MediaPipeTorsoUseActorForward=1
mp.MediaPipeTorsoUprightBlend=0.65
mp.MediaPipeTorsoMaxTiltDegrees=28.0
mp.MediaPipeShoulderRollbackTrace=0
mp.MediaPipeShoulderRollbackGuard=1
mp.MediaPipeShoulderRollbackGuardBlend=0.0
mp.MediaPipeShoulderRollbackGuardMinReliability=0.45
mp.MediaPipeShoulderRollbackGuardMaxTargetFromRefDegrees=150.0
mp.MediaPipeShoulderRollbackBackDotThreshold=-0.20
mp.MediaPipeShoulderRollbackStepDegrees=80.0
mp.MediaPipeShoulderRollbackTraceLogIntervalSeconds=0.10
mp.MediaPipePoseYawAlignToActor=1
mp.MediaPipePoseYawAlignHalfLife=0.30
mp.MediaPipePoseYawAlignMaxSpeedDegreesPerSecond=120.0
mp.MediaPipePoseYawAlignRejectJumpDegrees=55.0

mp.MediaPipeDriveLegs=0
mp.MediaPipeDrivePelvisTranslation=0
mp.MediaPipeUseLegIK=0
mp.MediaPipeUseFkRootGrounding=0
mp.MediaPipeDriveFootRotation=0

mp.QuestFingerRotationHalfLife=0.035
mp.QuestFingerJointRetarget=0
mp.QuestFingerCurlOnly=0
mp.QuestFingerPreserveSpread=0
mp.QuestFingerUseChainCurl=1
mp.QuestFingerCurlStrength=1.0
mp.QuestFingerMaxCurlDegrees=96.0
mp.QuestFingerCurlFullAngleDegrees=120.0
mp.QuestFingerCurlProximalScale=0.82
mp.QuestFingerCurlIntermediateScale=1.00
mp.QuestFingerCurlDistalScale=0.58
mp.QuestThumbCurlProximalScale=0.55
mp.QuestThumbCurlIntermediateScale=0.95
mp.QuestThumbCurlDistalScale=0.70
mp.QuestHandDebug=0
mp.QuestFingerDebug=0
mp.QuestHandCompare=0
mp.QuestWristDebug=0
mp.QuestHandHud=0
mp.QuestWristTrace=0
mp.QuestWristTraceLogIntervalSeconds=0.25
mp.QuestWristTraceStableBaseline=1
mp.MediaPipeTorsoDebug=0
mp.AutoQuestMirrorDebug=0

mp.AutoQuestVrPerfProfile=1
mp.AutoQuestVrScreenPercentage=70.0
mp.AutoQuestVrSkeletalMeshLodBias=0
mp.AutoQuestVrViewDistanceScale=0.8
mp.AutoQuestVrTextureQuality=2
mp.AutoQuestVrAntiAliasingQuality=1
mp.AutoQuestVrHairStrands=1
mp.AutoQuestVrMetaHumanForcedLod=1
r.ScreenPercentage=70
r.SkeletalMeshLODBias=0
r.ViewDistanceScale=0.8
sg.TextureQuality=2
sg.AntiAliasingQuality=1
r.Streaming.MipBias=0
r.Streaming.PoolSize=1000
r.MaxAnisotropy=4
r.HairStrands.Enable=1
r.HairStrands.Strands=1
r.ShadowQuality=0
r.Shadow.MaxResolution=512
r.MotionBlurQuality=0
r.BloomQuality=0
r.AmbientOcclusionLevels=0
r.SSR.Quality=0
r.VolumetricFog=0
```

## Critical Do-Not-Change Defaults

- Do not turn `mp.MediaPipeUseArmIK` back on. IK previously constrained and distorted wrists/arms.
- Do not solve arm drift by enabling `mp.MediaPipeUseArmIK` or `mp.QuestWristForceArmIK`. The current arm path is `mp.QuestArmMode=3`; use mode `0` only as an explicit rollback, not as the normal default.
- Do not turn `mp.MediaPipeDriveLegs`, `mp.MediaPipeUseLegIK`, or `mp.MediaPipeDrivePelvisTranslation` on for the current upper-body-only motion profile.
- Do not turn `mp.MediaPipeDriveClavicles`, `mp.MediaPipeDriveSpine`, lower-body driving, or pelvis translation back on by default while `mp.AutoQuestEmbodiedStableBody=1`; low-Hz full-body and shoulder-root motion should not fight Quest HMD/hand tracking until a separate VR Preview diagnostic proves it helps.
- Do not turn the legacy helper-only `mp.QuestWristDriveTwistCorrection` back on by default. The lowerarm-main forearm-roll trial also stays off by default because headset validation showed snappy arms with no candy-wrapper improvement.
- Do not change `mp.QuestHandRotationBlend` back to `0` or `0.90`. Current default is direct Quest wrist/hand rotation at `1.0`.
- Do not use `mp.QuestPalmMode=1` as a default. It tried to fall back from weak projected palm-roll frames to local Quest quaternion twist, but headset testing showed it did not improve palm roll and added shakiness. The current confirmed fix is `mp.QuestPalmMode=2`; quick-revert to `mp.QuestPalmMode=0` if headset testing shows instability.
- Do not re-enable `mp.AutoQuestHandCompareMode=2` as a normal default. It intentionally enables `mp.QuestHandCompare=2` for targeted hand-fidelity investigations only; normal embodied testing now starts with `mp.AutoQuestHandCompareMode=0`.
- Do not reintroduce the visible calibration guide/reveal flow. Current calibration gate/HUD defaults are off.
- Do not treat old finger-only docs as current truth. Finger-only was an earlier isolation step, not the current Wallace default.
- Do not remove the automatic Quest VR render budget unless replacing it with a measured headset-active profile. The headset path previously came up at full editor render cost even when desktop/automation PIE looked acceptable.

## Expected Runtime Proof

In PIE or VR Preview, Auto Quest should spawn:

```text
MP_LiveMediaPipeVideo
MP_LiveMediaPipeManny
MP_LiveMetaHumanWallace
```

The expected compact wrist proof for tracked hands is:

```text
questArmMode=3
positionApplied=1
requestedBlend=1.00
calib=HMD_AVATAR
handLocal=1
twistCorrection=0
lowerarmMainDriven=0
armIK=0
forceIK=0
```

`twistCorrection=0` is intentional. It means the old helper-only twist correction is not active. `lowerarmMainDriven=0` is now expected for the default profile; `lowerarmMainDriven=1` means the failed 2026-05-19 lowerarm-main roll trial has been re-enabled.

## Latest Build Evidence

The latest successful build after the SafeSetCS arm-chain fix produced:

```text
Build command succeeded for `TestingKit3Editor Win64 Development` against `D:\Epic\Unreal_Projects\TestingKit3\TestingKit3.uproject`.
```

The editor was reopened after the build. The current headset-confirmed runtime behavior is that Wallace's arms move forward with the Quest hands.

## Historical 2026-05-16 Setup Consolidation Checkpoint - Mode 2 Experiment

This section is historical and pre-dates the 2026-05-17 SafeSetCS / mode 3 checkpoint. Do not copy these mode 2 or 384px values into current defaults.

```text
mp.QuestArmMode 0  # historical rollback: MediaPipe arms, Quest hands/fingers only
mp.QuestArmMode 1  # legacy adaptive reach-assist
mp.QuestArmMode 2  # historical constrained no-IK arm solve
mp.QuestArmMode 3  # current default: HMD-relative avatar Quest wrist endpoint
```

Historical PIE readback from the old mode 2 experiment, not the current mode 3 default:

```text
mp.QuestArmMode=2
mp.QuestPalmMode=2
mp.AutoQuestArmReachAssistProfile=4
mp.AutoQuestEmbodiedView=1
mp.AutoQuestEmbodiedMirror=0
mp.QuestConstrainedArmSolve=1
mp.QuestWristReachAssist=1
mp.QuestWristDriftGuard=1
mp.MediaPipeUseArmIK=0
mp.QuestWristForceArmIK=0
mp.MediaPipeDriveLegs=0
mp.MediaPipeUseLegIK=0
mp.MediaPipeDrivePelvisTranslation=0
mp.QuestWristDriveTwistCorrection=0
mp.QuestWristForearmRollDriveTwistHelpers=0
mp.QuestWristTwistDrivesForearm=0
mp.AutoQuestWebcamHandsHz=30
mp.MediaPipeInputMaxDimension=384
```

Live switch proof in PIE:

```text
mp.QuestArmMode 0 -> readback 0, IK/legs remained 0
mp.QuestArmMode 1 -> readback 1, IK/legs remained 0
mp.QuestArmMode 2 -> readback 2, IK/legs remained 0
```

The mode `0` readback stayed at `0` after a three-second PIE wait, then that historical test restored `mp.QuestArmMode=2` because the old profile 4 experiment was under test. Do not restore mode 2 after current validation unless deliberately testing the historical path. Auto Quest spawned the expected runtime actors during the same PIE pass:

```text
MP_LiveMediaPipeVideo
MP_LiveMediaPipeManny
MP_LiveMetaHumanWallace
```

## Historical 2026-05-16 Palm Roll Fix Candidate

This section belongs to the profile 4 experiment. The palm orientation path is separate from `mp.QuestArmMode`: in the experimental path arms used the constrained no-IK endpoint solve, while palms/wrists used the Quest hand rotation solve. Fresh `mp.QuestHandCompare` evidence showed the Quest 3 raw hand basis can see palms-up/down, but Wallace often freezes or under-applies that roll when the projected palm-roll confidence is weak.

Current fix candidate:

```text
mp.QuestPalmMode 2
```

Mode `2` makes Quest authoritative for hand orientation while leaving the arm/wrist position solve alone. The prior mode 2 attempts proved useful diagnostically: first the fallback was roll-only, then it included bounded swing, then the 17:11 headset trace showed a near-constant pre-application target offset (`handOnlyToRetargetDeg` about `35-37`). After bypassing that relative orientation target, the 17:19 trace showed `handOnlyToRetargetDeg=0.0`, but `retargetToAvatarDeg` still averaged roughly `45-50` degrees. That identified the remaining blocker as the hand-rotation application layer. Mode `2` now applies the Quest hand target in the current lowerarm-local frame and honors `mp.QuestHandRotationHalfLife` / `mp.QuestHandRotationMaxStepDegrees` so accepted palm-basis orientation does not bypass the wrist-roll filter.

Quick revert:

```text
mp.QuestPalmMode 0
```

Mode behavior:

```text
mp.QuestPalmMode 0  # quick revert: stable projected palm-roll path; weak projection holds previous roll
mp.QuestPalmMode 1  # failed experimental fallback to local Quest quaternion twist
mp.QuestPalmMode 2  # current confirmed fix: Quest-authoritative hand orientation; arm/wrist position solve unchanged
```

When `mp.QuestWristTrace=1`, compact rows include:

```text
palmFallback=1  # mode 2 fallback supplied palm roll instead of freezing
palmHeld=1      # previous strict hold path was used
```

PIE readback after the 2026-05-16 17:23 palm rebuild confirmed:

```text
mp.QuestPalmMode=2
mp.QuestArmMode=2
mp.QuestWristDriveTwistCorrection=0
mp.QuestWristForearmRollDriveTwistHelpers=0
mp.QuestWristTwistDrivesForearm=0
mp.MediaPipeUseArmIK=0
mp.QuestWristForceArmIK=0
mp.MediaPipeDriveLegs=0
mp.MediaPipeUseLegIK=0
mp.MediaPipeDrivePelvisTranslation=0
```

Live switch proof in PIE:

```text
mp.QuestPalmMode 2 -> readback 2, IK/legs/twist helpers remained 0
mp.QuestPalmMode 0 -> readback 0, quick revert to the old hold-on-weak-projection path
```

## 2026-05-16 Stability Patch After Palm Fix

After the palm rotation fix, headset testing exposed a separate stability problem: VR Preview could start with arms in the wrong place and the body/arms difficult to keep stable. The log showed the palms matched on tracked frames, but profile 4 was still accepting Quest wrist-position authority too aggressively around startup/tracking loss. The worst recent Wallace comparison rows showed mapped/final wrist correction offsets around 30 cm and hand swing reaching the 140 degree cap, which is enough to pull the no-IK arm solve around even when palm orientation is correct.

The 18:41 rebuild keeps the confirmed palm fix but stabilizes the position path:

```text
mp.AutoQuestHandCompareMode default is now 0, so Quest hand compare no longer auto-draws/logs during normal embodied testing.
mp.QuestArmMode=2 now waits for 6 stable tracked startup samples and at least 0.16s before accepting Quest wrist-position authority.
Quest wrist position authority fades in over about 0.45s after that accepted startup baseline.
When hand tracking drops, constrained mode fades the filtered wrist correction back toward MediaPipe instead of holding the stale Quest wrist endpoint.
The constrained arm solve now consumes the filtered/ramped final wrist, not the raw mapped Quest wrist.
```

Post-rebuild PIE smoke for the historical profile 4 experiment verified Wallace still spawned and the no-IK/no-legs safety defaults remained:

```text
mp.AutoQuestHandCompareMode=0
mp.QuestHandCompare=0
mp.QuestPalmMode=2
mp.QuestArmMode=2
mp.QuestWristPositionBlend=0.82
mp.QuestWristLostTrackingGraceSeconds=0.35
mp.QuestWristRequireTrackedForApply=0
mp.QuestWristPositionAdaptiveFilter=1
mp.QuestHandDriveFingerBones=1
mp.MediaPipeUseArmIK=0
mp.QuestWristForceArmIK=0
mp.MediaPipeDriveLegs=0
mp.MediaPipeUseLegIK=0
mp.MediaPipeDrivePelvisTranslation=0
mp.QuestWristDriveTwistCorrection=0
mp.QuestWristForearmRollDriveTwistHelpers=0
mp.QuestWristTwistDrivesForearm=0
```

This does not change the palm fix. If the new startup gate feels too conservative, first test `mp.QuestArmMode 1` or `mp.QuestArmMode 0` to separate arm-position authority from palm orientation before changing lower-level wrist CVars.

## 2026-05-17 Upper-Body Reach Regression

Historical note: this section captured a pre-SafeSetCS diagnosis where stable embodied body was suspected of freezing reach. That conclusion is superseded. The current working checkpoint intentionally keeps `mp.AutoQuestEmbodiedStableBody=1` so low-Hz MediaPipe body motion does not fight Quest HMD/hand tracking.

At that time, the runtime/default comparison found this source/docs mismatch:

```text
Documented previous baseline at that time:
mp.MediaPipeDriveClavicles=1
mp.MediaPipeDriveSpine=1

Then-suspected source behavior:
mp.AutoQuestEmbodiedStableBody defaulted to 1
ApplyAutoQuestProfile() then forced:
mp.MediaPipeDriveClavicles=0
mp.MediaPipeDriveSpine=0
```

The current source behavior was narrowed again on 2026-05-19 after coupled changes caused regressions. Stable embodied startup keeps clavicles, spine, lower body, pelvis, and IK off:

```text
mp.AutoQuestEmbodiedStableBody=1
mp.MediaPipeDriveClavicles=0
mp.MediaPipeDriveSpine=0
mp.MediaPipeUseArmIK=0
mp.QuestWristForceArmIK=0
mp.MediaPipeDriveLegs=0
mp.MediaPipeUseLegIK=0
mp.MediaPipeDrivePelvisTranslation=0
```

The same rebuild also reduced repeated runtime churn: Auto Quest no longer re-sets unchanged console variables every station refresh, and room-scale follow no longer logs every 5 cm steady offset.

## 2026-05-17 Arm-Motion Drift / Endpoint Dropout

The remaining drift report is about the arms while the headset wearer moves or extends their arms. Do not treat it as a walking/body-follow problem unless the user explicitly reports room-scale walking drift in a fresh headset run.

The 2026-05-17 VR trace showed an arm-specific failure pattern on Wallace:

```text
When Quest wrist endpoint was valid:
positionApplied=1 questTracked=1 hmdPose=1 mediaHead=1 questArmSolve=1

When the endpoint dropped:
positionApplied=0 questTracked=0 untrackedData=1 hmdPose=0 mediaHead=0 questArmSolve=0
```

That means the constrained no-IK arm solve was engaging, then the endpoint could be rejected and the arm would fall back to MediaPipe instead of continuing to follow the extended Quest hand. Profile 4 now restores the tracking-loss tolerance from the accepted 2026-05-14 wrist/hand baseline:

```text
mp.QuestWristLostTrackingGraceSeconds=0.35
mp.QuestWristRequireTrackedForApply=0
```

This was not visual proof at the time. The later SafeSetCS checkpoint plus worn-headset user report is the current visual confirmation that arms move forward with the Quest hands again.

## 2026-05-16 HMD Cache And Arm Extension Patch

Historical warning: this section describes the old mode 2 HMD-cache patch. The current checkpoint moved the HMD-relative endpoint policy to `mp.QuestArmMode=3` and then fixed the stale child-bone issue with `SafeSetCSBoneTransforms()`.

Follow-up headset testing showed a different failure: real arms could start out in front while Wallace's arms stayed close to the chest, the embodied view could look down into the neck, and the avatar could snap on odd tilts. The fresh log showed a handled ensure from `TryGetQuestHmdWorldPose()` called inside `Evaluate_AnyThread`, which means the arm endpoint path was querying OpenXR HMD pose from the animation worker thread.

The 18:54 rebuild changes the runtime path:

```text
HMD pose is read once during anim-node PreUpdate on the game thread.
Evaluate_AnyThread consumes only the cached HMD pose and cached tracking up vector.
mp.QuestArmMode=2 no longer uses relative wrist-position calibration for constrained arm position.
Mode 2 maps the live Quest wrist as an absolute HMD-relative endpoint, so arms that are already out in front at VR Preview start are not zeroed back to the MediaPipe chest pose.
The existing 6-frame/0.16s startup gate and 0.45s authority fade still apply.
```

Post-rebuild PIE smoke on the restarted editor verified:

```text
Build succeeded after closing the old editor DLL lock.
Auto Quest spawned Wallace in embodied view.
No fresh `Ensure condition failed` / `TryGetQuestHmdWorldPose` log rows appeared during the smoke.
mp.QuestArmMode=2
mp.QuestPalmMode=2
mp.QuestWristPositionBlend=0.82
mp.QuestWristRelativeCalibration=1  # still active for rotation, not mode-2 arm position
mp.MediaPipeUseArmIK=0
mp.QuestWristForceArmIK=0
mp.MediaPipeDriveLegs=0
mp.MediaPipeUseLegIK=0
mp.MediaPipeDrivePelvisTranslation=0
```

This old patch was later superseded. Current validation is the 2026-05-17 SafeSetCS / mode 3 checkpoint at the top of this file.

## 2026-05-16 Stable Embodied Eye Anchor Correction

Historical warning: this section records the earlier investigation that found the camera-pin/root-chase problem. Some logs below are deliberately retained as evidence of the old failure. The current embodiment rules are in `Docs/WALLACE_QUEST_VR_EMBODIMENT_GUARDRAILS.md`.

Headset testing then captured the actual embodiment bug:

```text
targetCamera=V(Y=1200.00, Z=164.00)
hmd=V(X=52.76, Y=-46.82, Z=420.16)
cameraCorrectionCm=1318.0
pawnAfter=V(X=347.24, Y=2446.82, Z=-148.99)
wallace=V(X=-12.00, Y=1200.00, Z=2.00)
manny=V(X=-12.00, Y=1200.00, Z=2.00)
```

Cause: embodied mode was still reusing the old mirror-station camera pin. That made sense for the external mirror station, but it is wrong for embodiment because it repeatedly moves the VR pawn/camera while Wallace/Manny remain elsewhere. A later live-HMD-anchor attempt was also wrong because it let raw 3D HMD position, including Z and tracking-origin jumps, drive the avatar root, which made the body chase, float, and tilt instead of staying as the embodied body under the eye point.

The 19:19 rebuild changes embodied ownership:

```text
mp.AutoQuestEmbodiedAnchorMode=1 is now the default.
Mode 1 keeps Wallace/Manny on a stable embodied station, captures yaw from the settled HMD recenter moment, and after that applies horizontal-only room-scale follow so walking in the headset moves the embodied body under the wearer.
Mode 1 does not use raw HMD Z, does not apply HMD-position pawn correction, and does not run the recurring mirror camera pin.
Mode 2 is the experimental live-HMD chase path for comparison only.
Mode 0 restores the legacy fixed-station plus recurring mirror camera pin behavior for comparison.
The player pawn/camera components are still configured/hidden for first-person use, but startup and timer refreshes no longer apply pawn/body transform correction in mode 1.
```

Post-rebuild non-headset PIE smoke verified the current safety defaults and anchor policy:

Superseded caveat: this regular-PIE smoke is no longer acceptable proof for embodied VR. It still showed `viewPawn=BP_ThirdPersonCharacter_C_0` and `forwardOffset=12.0`, which are both now regression signatures. Current proof must come from worn/woken VR Preview and must show `DefaultPawn_0`, `springArms=0 cameras=0`, `forwardOffset=0.0`, and separated `viewerYaw`/`avatarYaw`.

```text
Build trace: 260516_194759
Auto Quest embodied anchor policy: mode=1 liveHmdAnchor=0 resetHmdOrigin=0 startupCameraPin=0 recurringCameraPin=0
Auto Quest webcam: spawned source=MediaPipeQuestWebcamSourceActor_0 driver=MediaPipePoseDrivenSkeletalActor_0 avatar=Wallace view=Embodied anchorMode=1 camera=C505 HD Webcam
Auto Quest embodied: camera=V(Y=1200.00, Z=164.00) avatar=V(X=-12.00, Y=1200.00, Z=2.00) yaw=0.0 eyeHeight=162.0 forwardOffset=12.0 anchorMode=1 viewPawn=BP_ThirdPersonCharacter_C_0
BP_ThirdPersonCharacter_C_0 loc=[0.0, 1200.0, 100.0]
BP_Wallace_C_0 loc=[-12.0, 1200.0, 2.0]
MediaPipePoseDrivenSkeletalActor_0 loc=[-12.0, 1200.0, 2.0]
mp.AutoQuestEmbodiedAnchorMode=1
mp.AutoQuestEmbodiedView=1
mp.QuestArmMode=2
mp.QuestPalmMode=2
mp.MediaPipeUseArmIK=0
mp.QuestWristForceArmIK=0
mp.MediaPipeDriveLegs=0
mp.MediaPipeUseLegIK=0
mp.MediaPipeDrivePelvisTranslation=0
```

If worn-headset testing shows the body/camera being yanked away from Wallace, the first log to inspect is `Auto Quest embodied anchor policy`, which must remain `startupCameraPin=0 recurringCameraPin=0` for mode 1. A `camera pinned` line during embodied mode 1 means the old mirror-station correction path has been reintroduced and should be treated as a regression.

The non-headset smoke cannot produce a valid HMD yaw sample, so it falls back to yaw `0.0`. In VR Preview, mode 1 should log the bounded settled-HMD yaw recenter:

```text
Auto Quest embodied: calibrated stable station yaw from settled HMD yaw=...
Auto Quest embodied: refreshed station after stable yaw recenter camera=... avatar=... viewerYaw=... avatarYaw=...
```

This prevents the eye point from being correctly positioned while Wallace's body still faces the old fixed yaw. For Wallace, `avatarYaw` should be approximately `viewerYaw - 90` because the visible MetaHuman face/chest axis is local `+Y`.

## 2026-05-16 Quest Hand Comparison Diagnostic

This is a read-only diagnostic for comparing Quest hands as raw tracked hands against the MetaHuman/Manny hands after the current retarget path. It does not change arm mode, IK, legs, twist helpers, palm mode, finger mode, or calibration.

Because the headset wearer cannot enter commands or use chat while in VR Preview, this diagnostic was temporarily auto-armed during the hand-fidelity investigation. It is now off by default again for stability/performance; turn it on only for targeted capture runs:

```text
mp.AutoQuestHandCompareMode 0  # do not auto-enable comparison
mp.AutoQuestHandCompareMode 1  # auto-enable log-only comparison
mp.AutoQuestHandCompareMode 2  # targeted capture only: auto-enable log plus mapped avatar-space skeleton draw
```

```text
mp.QuestHandCompare 0  # off/default
mp.QuestHandCompare 1  # log raw Quest hand-only vs applied avatar hand
mp.QuestHandCompare 2  # log plus draw HMD-relative avatar-space Quest hand skeletons
mp.QuestHandCompare 3  # manual deep diagnostic: also draw raw Quest world-space skeletons
```

Current diagnostic default:

```text
mp.AutoQuestHandCompareMode=0
mp.QuestHandCompare=0
```

Log line:

```text
mp.QuestHandCompare
```

Fields to inspect first:

```text
rawQuestToAvatarCm       # raw OpenXR wrist position versus avatar hand position
mappedQuestToAvatarCm    # legacy world-distance field; can be misleading if mapped MediaPipe space and rendered actor space differ
finalWristToAvatarCm     # legacy world-distance field; can be misleading for the same reason
visibleMetaHuman         # 1 when the row belongs to the active MetaHuman profile actor
targetCompLoc
sourceLoc
targetToSourceLocCm      # catches source actor versus visible MetaHuman placement offsets
mediaPipeWrist
solvedWrist
shoulderWorld
rawQuestWristTargetComp
mappedQuestWristTargetComp
finalWristTargetComp
mediaPipeWristTargetComp
avatarHandTargetComp
rawQuestToAvatarTargetCompCm
mappedQuestToAvatarTargetCompCm
finalWristToAvatarTargetCompCm
mediaPipeWristToAvatarTargetCompCm
mappedOffsetFromMediaPipeCm
finalOffsetFromMediaPipeCm
finalToSolvedWristCm
handOnlyToAvatarDeg      # Quest hand orientation basis versus applied avatar hand basis
handOnlyToRetargetDeg    # Quest hand orientation basis versus retarget target basis
retargetToAvatarDeg      # retarget target basis versus actually applied avatar basis
questBasisFwdErrDeg
questBasisUpErrDeg
rawTwistDeg
semanticScore
palmHeld
palmFallback
armIK
forceIK
twistCorrection
```

Use `mp.QuestHandCompare 2` in VR Preview when the headset wearer needs visual proof: cyan/blue and green draw the Quest hands after the same HMD-relative avatar-space mapping used by the solver. Use `mp.QuestHandCompare 3` only when raw OpenXR/world-space skeletons are needed for a deeper diagnostic. The in-headset text prioritizes the active MetaHuman profile and reports mapped palm-plane error plus raw Quest wrist distance and MediaPipe offset. Use `mp.QuestHandCompare 1` when only the logs are needed. Restore `mp.QuestHandCompare 0` after capturing evidence.

PIE verification after the 2026-05-16 16:16 build confirmed:

```text
mp.QuestHandCompare=0
mp.QuestPalmMode=2
mp.QuestArmMode=2
mp.MediaPipeUseArmIK=0
mp.QuestWristForceArmIK=0
mp.MediaPipeDriveLegs=0
mp.MediaPipeUseLegIK=0
mp.MediaPipeDrivePelvisTranslation=0
mp.QuestWristDriveTwistCorrection=0
mp.QuestWristForearmRollDriveTwistHelpers=0
mp.QuestWristTwistDrivesForearm=0

mp.QuestHandCompare 1 -> readback 1
mp.QuestHandCompare 2 -> readback 2
mp.QuestHandCompare 0 -> readback 0
```

The same PIE pass spawned:

```text
MP_LiveMediaPipeVideo
MP_LiveMediaPipeManny
MP_LiveMetaHumanWallace
```

After the 2026-05-16 17:23 palm rebuild, Auto Quest no longer requires headset-time command entry for this diagnostic, and `mp.QuestPalmMode=2` is applied automatically. A normal PIE startup smoke test verified:

```text
mp.AutoQuestHandCompareMode=2
mp.QuestHandCompare=2
mp.QuestHandTracking=1
mp.QuestHandDriveFingerBones=1
mp.QuestHandRotationBlend=1
mp.QuestPalmMode=2
mp.QuestArmMode=2
mp.MediaPipeUseArmIK=0
mp.QuestWristForceArmIK=0
mp.MediaPipeDriveLegs=0
mp.MediaPipeUseLegIK=0
mp.MediaPipeDrivePelvisTranslation=0
mp.QuestWristDriveTwistCorrection=0
mp.QuestWristForearmRollDriveTwistHelpers=0
mp.QuestWristTwistDrivesForearm=0
```

The same smoke test spawned `MP_LiveMediaPipeVideo`, `MP_LiveMediaPipeManny`, and `MP_LiveMetaHumanWallace`. True headset evidence still needs an actual VR Preview session because normal PIE does not deliver Quest hand joint poses.

After the 2026-05-16 17:34 diagnostic rebuild, `mp.QuestHandCompare` also emits `mp.QuestPalmPlaneCompare` for Wallace/Manny rows. After the 18:02 correction, this comparison maps Quest wrist/index/middle/pinky palm directions through the same Quest-to-Media transform used by the solver before comparing against the evaluated avatar hand/index/middle/pinky bone positions. Use `palmForwardErrDeg`, `palmUpErrDeg`, and `signedPalmRollErrDeg` as the mapped pass/fail fields. Use `rawPalmForwardErrDeg`, `rawPalmUpErrDeg`, and `rawSignedPalmRollErrDeg` only to identify raw debug-outline mismatch.

The 17:34 normal PIE smoke verified the rebuild and startup defaults again:

```text
mp.AutoQuestHandCompareMode=2
mp.QuestHandCompare=2
mp.QuestPalmMode=2
mp.QuestArmMode=2
mp.QuestHandDriveFingerBones=1
mp.MediaPipeUseArmIK=0
mp.QuestWristForceArmIK=0
mp.MediaPipeDriveLegs=0
mp.MediaPipeUseLegIK=0
mp.MediaPipeDrivePelvisTranslation=0
mp.QuestWristDriveTwistCorrection=0
mp.QuestWristForearmRollDriveTwistHelpers=0
mp.QuestWristTwistDrivesForearm=0
```

After the 2026-05-16 17:39 visual-palm-basis rebuild, mode `2` was patched to stop using the hand bone's raw X/Z axes as the palm reference. Local reference-pose inspection on Wallace `Body` showed:

```text
left:  hand X to visible palm forward = 4.8 deg, hand Z to visible palm normal = 83.6 deg, hand Y to visible palm normal = 7.8 deg
right: hand X to mirrored palm forward = 4.8 deg, hand Z to visible palm normal = 83.6 deg, hand Y to visible palm normal = 7.8 deg
```

That explains the headset symptom where the Quest 3 outline showed palms facing the floor while Wallace's palms appeared sideways: the old reference basis mapped the Quest palm normal onto a sideways hand axis. The 17:39 rebuild succeeded, and the 17:46 normal PIE smoke verified the same startup defaults:

```text
mp.AutoQuestHandCompareMode=2
mp.QuestHandCompare=2
mp.QuestPalmMode=2
mp.QuestArmMode=2
mp.QuestHandDriveFingerBones=1
mp.MediaPipeUseArmIK=0
mp.QuestWristForceArmIK=0
mp.MediaPipeDriveLegs=0
mp.MediaPipeUseLegIK=0
mp.MediaPipeDrivePelvisTranslation=0
mp.QuestWristDriveTwistCorrection=0
mp.QuestWristForearmRollDriveTwistHelpers=0
mp.QuestWristTwistDrivesForearm=0
```

After the 2026-05-16 17:51 rebuild, the `mp.QuestPalmPlaneCompare` diagnostic was moved to run after the Quest finger solve in the Quest-hand path, so the comparison uses the final evaluated hand/finger pose rather than hand rotation plus pre-finger-drive bone positions. The 17:51 rebuild succeeded and the 17:52 normal PIE smoke again verified Wallace/Manny/video spawn with the same defaults:

```text
mp.AutoQuestHandCompareMode=2
mp.QuestHandCompare=2
mp.QuestPalmMode=2
mp.QuestArmMode=2
mp.QuestHandDriveFingerBones=1
mp.MediaPipeUseArmIK=0
mp.QuestWristForceArmIK=0
mp.MediaPipeDriveLegs=0
mp.MediaPipeUseLegIK=0
mp.MediaPipeDrivePelvisTranslation=0
mp.QuestWristDriveTwistCorrection=0
mp.QuestWristForearmRollDriveTwistHelpers=0
mp.QuestWristTwistDrivesForearm=0
```

The follow-up local proof printed `CODEX_VISUAL_PALM_PROOF` rows for Wallace `Body`:

```text
left:  old hand-Z palm-normal error = 83.6 deg, new visual-basis palm-normal error = 0.0 deg
right: old hand-Z palm-normal error = 83.6 deg, new visual-basis palm-normal error = 0.0 deg
```

The first replay+visual-cycle comparison exposed a diagnostic bug: `mp.QuestPalmPlaneCompare` was comparing raw OpenXR/Quest world hand directions against the avatar, while the hand solver applies the current Quest-to-Media mapping before driving the MetaHuman. That made the raw palm-plane rows look 60-110 degrees wrong even when `mp.QuestHandCompare` reported `handOnlyToRetargetDeg=0.0`, `questBasisFwdErrDeg=0.0`, and `questBasisUpErrDeg=0.0`.

After the 2026-05-16 18:02 rebuild, `mp.QuestPalmPlaneCompare` now reports mapped Quest-vs-avatar palm errors first and keeps raw-space errors separately:

```text
Replay+visual-cycle proof with open_fist_20260515_132036_00:
left:  mapped palmForwardErrDeg max 0.0, mapped palmUpErrDeg max 0.0; raw forward/up ranges 70.0-104.8 / 64.6-95.3 deg
right: mapped palmForwardErrDeg max 0.0, mapped palmUpErrDeg max 0.0; raw forward/up ranges 66.3-97.7 / 71.8-113.0 deg
```

The in-headset compare text now says `mapped palm F/N`, and the debug hand skeleton label explicitly says raw Quest space is not avatar space. For headset validation, trust the mapped `palmForwardErrDeg`, `palmUpErrDeg`, and `signedPalmRollErrDeg` fields; use `rawPalm*` only to diagnose whether a raw debug outline is being mistaken for the avatar-space target.

After the 2026-05-16 18:20 palm-authority patch, mode `2` no longer lets `mp.QuestWristUseJointRotation=1` override the palm landmark basis. The older headset evidence showed the Quest palm landmarks and the OpenXR wrist joint quaternion could disagree; that produced the symptom where the Quest outline was palms-down but the MetaHuman hand appeared sideways. In mode `2`, the hand orientation now comes from mapped Quest wrist/index/middle/pinky palm geometry plus the Wallace visual palm reference basis. Quick revert remains:

```text
mp.QuestPalmMode 0
```

The 18:06 smoke readback after the mapped-palm diagnostic rebuild verified the current defaults still held. After the 18:21 palm-authority rebuild, PIE startup again spawned Wallace and verified the same defaults plus the no-IK/no-legs/no-twist-helper guardrails. After the final 18:26 freeze rebuild, the DLL timestamp was newer than the source file and the same PIE smoke again spawned Wallace with the same readback:

```text
mp.AutoQuestHandCompareMode=2
mp.QuestHandCompare=2
mp.QuestPalmMode=2
mp.QuestArmMode=2
mp.QuestHandDriveFingerBones=1
mp.QuestWristUseJointRotation=1   # still configured, but ignored by mode 2 final hand orientation
mp.MediaPipeUseArmIK=0
mp.QuestWristForceArmIK=0
mp.MediaPipeDriveLegs=0
mp.MediaPipeUseLegIK=0
mp.MediaPipeDrivePelvisTranslation=0
mp.QuestWristDriveTwistCorrection=0
mp.QuestWristForearmRollDriveTwistHelpers=0
mp.QuestWristTwistDrivesForearm=0
```

The required worn-headset VR Preview validation passed after this patch: the MetaHuman palms now rotate as expected against the Quest hand outline. The replay harness proves the current solver and mapped diagnostic agree for a captured Quest hand snapshot; the no-headset Auto Quest spawn created Wallace, but live headset validation is still the authority for visual feel and Wallace presentation rows.

The earlier 2026-05-16 16:40 comparison diagnostic rebuild refined logging without changing movement behavior:

```text
mp.QuestHandCompare rows are no longer globally throttled by side, so Manny cannot starve Wallace rows.
mp.QuestHandCompare logs include target/source actor locations and target-component-space comparison fields.
Wallace in-headset comparison text is shown in preference to Manny text.
```

The preceding VR Preview evidence showed the hand comparison was not yet clean enough for position conclusions: raw Quest wrist versus rendered avatar hand was plausible, while mapped/final wrist versus avatar hand showed roughly 12 m offsets because those fields mixed mapped MediaPipe-frame points with the rendered actor world frame. The orientation divergence was still real evidence: Wallace rows reached roughly 77-92 degrees of hand-only mismatch during the run.

## 2026-05-15 Embodied Avatar View And Mirror

The embodied view is implemented as an opt-in Auto Quest mode, not as a replacement for the existing external mirror-station baseline:

```text
mp.AutoQuestEmbodiedView 1
```

Quick revert:

```text
mp.AutoQuestEmbodiedView 0
```

When enabled, Auto Quest places Wallace/Manny under the HMD eye point:

```text
eyeHeight=162.0
forwardOffset=0.0
wallaceYawOffset=-90.0
camera=Wallace facial eye center, using FACIAL_L_Eye/FACIAL_R_Eye when available
```

The current default puts the local HMD camera at Wallace's eye center rather than in front of the face. The head is not globally hidden or removed. For Wallace, only the local HMD owner view gets owner-only culling on face/head attachments; mirror/reflection views still see the full head:

```text
Face owner_no_see=True hidden=False visible=True
Hair owner_no_see=True hidden=False visible=True
Eyebrows owner_no_see=True hidden=False visible=True
Fuzz owner_no_see=True hidden=False visible=True
Eyelashes owner_no_see=True hidden=False visible=True
Mustache owner_no_see=True hidden=False visible=True
Beard owner_no_see=True hidden=False visible=True
```

A virtual mirror is still available, but it is no longer part of the current default checkpoint. It spawns only when embodied view is enabled and `mp.AutoQuestEmbodiedMirror=1`:

```text
MP_EmbodiedMirrorPlane
MP_EmbodiedPlanarReflection
mirror loc=V(X=208.00, Y=1200.00, Z=122.00)
mirror size=180x220
mirror distance=220
```

`Config/DefaultEngine.ini` has `r.AllowGlobalClipPlane=True`, which is the UE 5.7 renderer setting used by planar reflections. A source search did not find a real `r.PlanarReflection.Enable` CVar in UE 5.7, so this implementation does not depend on that probe.

Historical mirror-on PIE smoke verification after rebuild:

```text
CODEX_EMBODIED_FINAL3_CVAR r.AllowGlobalClipPlane float=1.0 int=1
CODEX_EMBODIED_FINAL3_CVAR mp.AutoQuestEmbodiedView float=1.0 int=1
CODEX_EMBODIED_FINAL3_CVAR mp.AutoQuestEmbodiedMirror float=1.0 int=1
CODEX_EMBODIED_FINAL3_CVAR mp.MediaPipeUseArmIK float=0.0 int=0
CODEX_EMBODIED_FINAL3_CVAR mp.QuestWristForceArmIK float=0.0 int=0
CODEX_EMBODIED_FINAL3_CVAR mp.MediaPipeDriveLegs float=0.0 int=0
CODEX_EMBODIED_FINAL3_CVAR mp.MediaPipeUseLegIK float=0.0 int=0
CODEX_EMBODIED_FINAL3_TAG TestingKit3_MediaPipeLiveVideo count=1 names=['MP_LiveMediaPipeVideo']
CODEX_EMBODIED_FINAL3_TAG TestingKit3_MediaPipeLiveManny count=1 names=['MP_LiveMediaPipeManny']
CODEX_EMBODIED_FINAL3_TAG TestingKit3_MediaPipeLiveWallace count=1 names=['MP_LiveMetaHumanWallace']
CODEX_EMBODIED_FINAL3_TAG TestingKit3_MediaPipeEmbodiedMirrorPlane count=1 names=['MP_EmbodiedMirrorPlane']
CODEX_EMBODIED_FINAL3_TAG TestingKit3_MediaPipeEmbodiedPlanarReflection count=1 names=['MP_EmbodiedPlanarReflection']
CODEX_EMBODIED_FINAL3_WALLACE loc=(-12.0,1200.0,2.0) rot=(0.0,0.0,0.0) owner=BP_ThirdPersonCharacter0
CODEX_EMBODIED_FINAL3_COMP Body owner_no_see=False hidden=False visible=True
CODEX_EMBODIED_FINAL3_COMP Face owner_no_see=True hidden=False visible=True
CODEX_EMBODIED_FINAL3_COMP Hair owner_no_see=True hidden=False visible=True
CODEX_EMBODIED_FINAL3_COMP Eyebrows owner_no_see=True hidden=False visible=True
CODEX_EMBODIED_FINAL3_COMP Fuzz owner_no_see=True hidden=False visible=True
CODEX_EMBODIED_FINAL3_COMP Eyelashes owner_no_see=True hidden=False visible=True
CODEX_EMBODIED_FINAL3_COMP Mustache owner_no_see=True hidden=False visible=True
CODEX_EMBODIED_FINAL3_COMP Beard owner_no_see=True hidden=False visible=True
CODEX_EMBODIED_FINAL3_MIRROR_MESH visible=True hidden=False mobility=Movable mesh=/Engine/BasicShapes/Plane.Plane mats=['/Engine/EditorLandscapeResources/MirrorPlaneMaterial.MirrorPlaneMaterial']
CODEX_EMBODIED_FINAL3_REFLECTION visible=True hidden=False active=True class=PlanarReflectionComponent
```

Visual checkpoint:

```text
Saved\CodexAgent\Screenshots\embodied_mirror_final.png
worldKind=PIE, focus requested/found MP_EmbodiedMirrorPlane, skyLikeRatio=0
```

## Historical Mode 2 Arm Control - Superseded By Mode 3

As of the 2026-05-16 10:41 build, profile 4 was being treated as the best headset checkpoint and mapped to mode 2:

```text
mp.AutoQuestEmbodiedView 1
mp.AutoQuestEmbodiedMirror 0
mp.QuestArmMode 2
```

This checkpoint keeps the accepted Wallace constraints:

```text
mp.MediaPipeUseArmIK=0
mp.QuestWristForceArmIK=0
mp.MediaPipeDriveLegs=0
mp.MediaPipeUseLegIK=0
mp.MediaPipeDrivePelvisTranslation=0
```

Embodied view made arm reach errors more obvious. The earlier frozen baseline kept Quest wrist position out of the arm-position solve, which made forward reach feel under-extended because MediaPipe still owned shoulder, elbow, and wrist position. Profiles 1-3 were reversible tuning attempts. The old mode 2 profile tried to keep MediaPipe as the body/shoulder intent source while constraining the arm endpoint with the mapped Quest wrist. The constrained solve computes a no-IK elbow target from avatar arm lengths, a stable torso/body elbow pole, and a low-weight MediaPipe elbow hint. Tiny stationary Quest correction noise is still smoothed by the adaptive wrist filter. This section was later superseded by the current profile 4 / `mp.QuestArmMode=3` checkpoint, which uses the HMD-relative avatar Quest wrist endpoint and was confirmed by the user after the SafeSetCS rebuild.

Experimental arm mode 2 values:

```text
mp.QuestArmMode=2
mp.QuestWristPositionBlend=0.82
mp.QuestWristMaxRelativeDeltaCm=82.0
mp.QuestWristLostTrackingGraceSeconds=0.35
mp.QuestWristRequireTrackedForApply=0
mp.QuestWristReachAssist=1
mp.QuestWristReachAssistBlend=0.48
mp.QuestWristReachAssistMaxElbowMoveCm=24.0
mp.QuestWristDriftGuard=1
mp.QuestWristDriftGuardStartCm=18.0
mp.QuestWristDriftGuardFullCm=55.0
mp.QuestWristDriftGuardReachBlendBoost=0.35
mp.QuestWristDriftGuardExtraElbowMoveCm=18.0
mp.QuestWristDriftGuardPoleBlend=0.85
mp.QuestConstrainedArmSolve=1
mp.QuestConstrainedArmSolveBlend=1.0
mp.QuestConstrainedArmWristAuthority=1.0
mp.QuestConstrainedArmMediaPipeElbowHint=0.20
mp.QuestConstrainedArmStablePoleDown=0.25
mp.QuestConstrainedArmMaxElbowMoveCm=65.0
mp.QuestWristPositionAdaptiveFilter=1
mp.QuestWristPositionFilterStillHalfLife=0.11
mp.QuestWristPositionFilterMovingHalfLife=0.018
mp.QuestWristPositionFilterSpeedForMinLag=120.0
mp.QuestWristPositionFilterDeadbandCm=0.65
mp.QuestWristPositionFilterResetDistanceCm=45.0
mp.MediaPipeArmRotationHalfLife=0.08
mp.QuestHandRotationHalfLife=0.035
mp.QuestHandRotationLostTrackingGraceSeconds=0.20
```

Arm mode history:

```text
0  MediaPipe arms with Quest hands/fingers only
1  previous adaptive Quest wrist reach-assist path
2  experimental adaptive filtered Quest correction plus no-IK Quest-constrained arm solve
3  current HMD-relative avatar Quest wrist endpoint with MediaPipe shoulder/elbow hints
```

Quick live revert to the previous adaptive reach path:

```text
mp.QuestArmMode 1
```

Quick live rollback to MediaPipe arms with Quest hands/fingers only:

```text
mp.QuestArmMode 0
```

Current compatibility note: `mp.AutoQuestArmReachAssistProfile=4` now maps Auto Quest startup to `mp.QuestArmMode=3`. Older references in this document to profile 4 mapping to mode 2 are historical.

Internal diagnostic fallback, not the normal test interface:

```text
mp.QuestConstrainedArmSolve 0
mp.QuestWristDriftGuard 0
```

Historical PIE smoke after the 2026-05-16 constrained-arm rebuild verified profile 4 was active, the constrained solve was enabled, and IK/legs remained off. This is not the current mode 3 default:

```text
CODEX_CONSTRAINED_ARM_CVAR mp.AutoQuestArmReachAssistProfile=4.0
CODEX_CONSTRAINED_ARM_CVAR mp.QuestArmMode=2.0
CODEX_CONSTRAINED_ARM_CVAR mp.AutoQuestEmbodiedView=1.0
CODEX_CONSTRAINED_ARM_CVAR mp.AutoQuestEmbodiedMirror=0.0
CODEX_CONSTRAINED_ARM_CVAR mp.AutoQuestWebcamHandsHz=30.0
CODEX_CONSTRAINED_ARM_CVAR mp.MediaPipeInputMaxDimension=384.0
CODEX_CONSTRAINED_ARM_CVAR mp.QuestWristPositionBlend=0.8199999928474426
CODEX_CONSTRAINED_ARM_CVAR mp.QuestWristReachAssist=1.0
CODEX_CONSTRAINED_ARM_CVAR mp.QuestWristDriftGuard=1.0
CODEX_CONSTRAINED_ARM_CVAR mp.QuestConstrainedArmSolve=1.0
CODEX_CONSTRAINED_ARM_CVAR mp.QuestConstrainedArmSolveBlend=1.0
CODEX_CONSTRAINED_ARM_CVAR mp.QuestConstrainedArmWristAuthority=1.0
CODEX_CONSTRAINED_ARM_CVAR mp.QuestConstrainedArmMediaPipeElbowHint=0.20000000298023224
CODEX_CONSTRAINED_ARM_CVAR mp.QuestConstrainedArmStablePoleDown=0.25
CODEX_CONSTRAINED_ARM_CVAR mp.QuestConstrainedArmMaxElbowMoveCm=65.0
CODEX_CONSTRAINED_ARM_CVAR mp.MediaPipeUseArmIK=0.0
CODEX_CONSTRAINED_ARM_CVAR mp.QuestWristForceArmIK=0.0
CODEX_CONSTRAINED_ARM_CVAR mp.MediaPipeDriveLegs=0.0
CODEX_CONSTRAINED_ARM_CVAR mp.MediaPipeUseLegIK=0.0
CODEX_CONSTRAINED_ARM_CVAR mp.MediaPipeDrivePelvisTranslation=0.0
```

The quick live revert test succeeded and restored the solver:

```text
CODEX_CONSTRAINED_ARM_REVERT_TEST after_zero=0.0 after_restore=1.0
```

The same PIE smoke loaded `Lvl_ThirdPerson`, found 75 runtime actors, found `MP_LiveMediaPipeVideo`, `MP_LiveMediaPipeManny`, `MP_LiveMetaHumanWallace`, and found `BP_ThirdPersonCharacter0`. Screenshot checkpoint: `Saved\CodexAgent\Screenshots\quest_constrained_arm_pie_smoke_20260516.png`.

When diagnosing this checkpoint with logs, `mp.QuestWristTrace=1` includes `questArmMode`, `palmFallback`, `palmHeld`, `posFilter`, `posFilterAlpha`, `posFilterSpeedCmSec`, `posFilterTargetDeltaCm`, `posFilterFilteredDeltaCm`, `reachAssist`, `reachAssistBlend`, `reachAssistElbowMoveCm`, `reachAssistElbow`, `driftGuard`, `driftAlpha`, `driftOffsetCm`, `driftReachBlend`, `driftPoleBlend`, `questArmSolve`, `questArmSolveBlend`, `questArmWristAuthority`, `questArmMpElbowHint`, `questArmElbowMoveCm`, and `questArmElbow` fields in the wrist trace lines.

## Historical Upper-Body Motion Verification

The user asked to add some MediaPipe motion back in without using legs and without switching on IK. This earlier checkpoint verified upper-body MediaPipe motion only, before the later profile 4 experiment and before the 2026-05-17 rollback:

```text
CODEX_UPPER_WORLD Lvl_ThirdPerson
CODEX_UPPER_CVAR mp.MediaPipeDriveClavicles float=1.0 int=1
CODEX_UPPER_CVAR mp.MediaPipeDriveSpine float=1.0 int=1
CODEX_UPPER_CVAR mp.MediaPipeTorsoUprightBlend float=0.6499999761581421 int=0
CODEX_UPPER_CVAR mp.MediaPipeTorsoMaxTiltDegrees float=28.0 int=28
CODEX_UPPER_CVAR mp.MediaPipeDrivePelvisTranslation float=0.0 int=0
CODEX_UPPER_CVAR mp.MediaPipeDriveLegs float=0.0 int=0
CODEX_UPPER_CVAR mp.MediaPipeUseArmIK float=0.0 int=0
CODEX_UPPER_CVAR mp.MediaPipeUseLegIK float=0.0 int=0
CODEX_UPPER_CVAR mp.MediaPipeDriveHandRotation float=0.0 int=0
CODEX_UPPER_CVAR mp.QuestHandRotationBlend float=1.0 int=1
CODEX_UPPER_CVAR mp.QuestHandDriveFingerBones float=1.0 int=1
CODEX_UPPER_CVAR mp.QuestWristUseJointRotation float=1.0 int=1
CODEX_UPPER_CVAR mp.QuestWristPositionBlend float=0.0 int=0
CODEX_UPPER_CVAR mp.QuestWristForceArmIK float=0.0 int=0
CODEX_UPPER_CVAR mp.MediaPipeDriveFootRotation float=0.0 int=0
CODEX_UPPER_CVAR mp.MediaPipeUseFkRootGrounding float=0.0 int=0
CODEX_UPPER_TAG TestingKit3_MediaPipeLiveVideo 1 ['MP_LiveMediaPipeVideo']
CODEX_UPPER_TAG TestingKit3_MediaPipeLiveManny 1 ['MP_LiveMediaPipeManny']
CODEX_UPPER_TAG TestingKit3_MediaPipeLiveWallace 1 ['MP_LiveMetaHumanWallace']
```

Visual checkpoint:

```text
Saved\CodexAgent\Screenshots\wallace_upperbody_mediapipe_motion.png
worldKind=PIE, focus requested/found MP_LiveMetaHumanWallace, skyLikeRatio=0
```

## 2026-05-15 Shoulder Rollback Trace

The user reported that rolling the shoulders back in real life can make the MetaHuman arms flip backwards. The reversible guard is enabled in the Auto Quest and manual Quest webcam profiles. The targeted anomaly trace exists but is default-off because leaving it on can spam logs during unstable MediaPipe samples and disturb VR Preview.

2026-05-19 update: this guard is direct-MediaPipe-only after the guard-policy patch. It must not hard-hold profile 4 once the Quest constrained arm solve has succeeded; the constrained solver owns endpoint, elbow pole continuity, and branch repair in that path.

```text
mp.MediaPipeShoulderRollbackTrace=0
mp.MediaPipeShoulderRollbackGuard=1
mp.MediaPipeShoulderRollbackGuardBlend=0.0
mp.MediaPipeShoulderRollbackGuardMinReliability=0.45
mp.MediaPipeShoulderRollbackGuardMaxTargetFromRefDegrees=150.0
mp.MediaPipeShoulderRollbackBackDotThreshold=-0.20
mp.MediaPipeShoulderRollbackStepDegrees=80.0
mp.MediaPipeShoulderRollbackTraceLogIntervalSeconds=0.10
```

Quick live revert:

```text
mp.MediaPipeShoulderRollbackGuard 0
```

When explicitly enabled, the trace logs suspected arm-flip frames under this token:

```text
mp.MediaPipeShoulderRollbackTrace:
```

It records side, behind-torso flags, forward-dot crossing, target rotation jumps, smoothing clamp hits, `guardApplied`, `guardBlend`, clavicle state, MediaPipe landmark reliability, shoulder/elbow/wrist positions, torso basis vectors, and confirms `armIK`, `questForceArmIK`, `legs`, `legIK`, and `pelvisTranslation` values in the same line.

Live VR Preview verification after rebuild:

```text
CODEX_SHOULDER_TRACE_WORLD Lvl_ThirdPerson
CODEX_SHOULDER_TRACE_CVAR mp.MediaPipeShoulderRollbackTrace float=1.0 int=1
CODEX_GUARD_CVAR mp.MediaPipeShoulderRollbackGuard float=1.0 int=1
CODEX_GUARD_CVAR mp.MediaPipeShoulderRollbackGuardBlend float=0.0 int=0
CODEX_GUARD_CVAR mp.MediaPipeShoulderRollbackGuardMinReliability float=0.44999998807907104 int=0
CODEX_GUARD_CVAR mp.MediaPipeShoulderRollbackGuardMaxTargetFromRefDegrees float=150.0 int=150
CODEX_SHOULDER_TRACE_CVAR mp.MediaPipeShoulderRollbackBackDotThreshold float=-0.20000000298023224 int=0
CODEX_SHOULDER_TRACE_CVAR mp.MediaPipeShoulderRollbackStepDegrees float=80.0 int=80
CODEX_SHOULDER_TRACE_CVAR mp.MediaPipeShoulderRollbackTraceLogIntervalSeconds float=0.10000000149011612 int=0
CODEX_SHOULDER_TRACE_CVAR mp.MediaPipeDriveClavicles float=1.0 int=1
CODEX_SHOULDER_TRACE_CVAR mp.MediaPipeDriveLegs float=0.0 int=0
CODEX_SHOULDER_TRACE_CVAR mp.MediaPipeDrivePelvisTranslation float=0.0 int=0
CODEX_SHOULDER_TRACE_CVAR mp.MediaPipeUseArmIK float=0.0 int=0
CODEX_SHOULDER_TRACE_CVAR mp.MediaPipeUseLegIK float=0.0 int=0
CODEX_SHOULDER_TRACE_CVAR mp.QuestWristForceArmIK float=0.0 int=0
CODEX_SHOULDER_TRACE_TAG TestingKit3_MediaPipeLiveVideo 1 ['MP_LiveMediaPipeVideo']
CODEX_SHOULDER_TRACE_TAG TestingKit3_MediaPipeLiveManny 1 ['MP_LiveMediaPipeManny']
CODEX_SHOULDER_TRACE_TAG TestingKit3_MediaPipeLiveWallace 1 ['MP_LiveMetaHumanWallace']
```

No `mp.MediaPipeShoulderRollbackTrace:` lines were present immediately after automated launch because the headset/user was not performing the shoulder rollback motion during verification.

## 2026-05-15 Quest Fist Finger Spread Attempt

Historical note: this section predates the 2026-05-19 headset-accepted segment-direction default. Do not treat the curl-only values below as current startup defaults. The current hand/finger default is documented at the top of this file and uses `mp.QuestFingerJointRetarget=0`, `mp.QuestFingerCurlOnly=0`, and the parent-chain `segmentDirection` path.

Manny and Wallace both showed the closed-fist fingers bunching together, so this was treated as a shared Quest finger-retargeting issue rather than MetaHuman skinning. A Quest curl-only spread-preservation experiment was added behind a CVar, but it distorted the fingers because the current rig hand basis did not match the assumed anatomical curl plane. The failed spread-preservation toggle remains default-off:

```text
mp.QuestFingerCurlOnly=0
mp.QuestFingerPreserveSpread=0
```

Do not enable `mp.QuestFingerPreserveSpread=1` or return to curl-only mode as the default. The live revert used after the bad test was:

```text
mp.QuestFingerCurlOnly 0
mp.QuestFingerPreserveSpread 0
```

Emergency live revert verification:

```text
CODEX_FINGER_EMERGENCY_REVERT mp.QuestFingerPreserveSpread float=0.0 int=0
CODEX_FINGER_EMERGENCY_REVERT mp.QuestFingerCurlOnly float=1.0 int=1
CODEX_FINGER_EMERGENCY_REVERT mp.QuestFingerCurlStrength float=1.2999999523162842 int=1
CODEX_FINGER_EMERGENCY_REVERT mp.QuestFingerMaxCurlDegrees float=112.0 int=112
CODEX_FINGER_EMERGENCY_REVERT mp.QuestThumbMaxCurlDegrees float=82.0 int=82
CODEX_FINGER_EMERGENCY_REVERT mp.QuestFingerCurlFullAngleDegrees float=68.0 int=68
CODEX_FINGER_EMERGENCY_REVERT mp.QuestFingerDebug float=0.0 int=0
```

## 2026-05-15 Quest Hand Capture/Replay Harness

To avoid breaking live VR Preview with finger-solver guesses, Quest/OpenXR hand snapshots can now be captured to JSON and replayed back into the existing `QuestHands` runtime boundary before wrist/finger solving.

Capture live Quest hand poses while VR Preview/OpenXR hand tracking is active:

```text
mp.CaptureQuestHandPose open
mp.CaptureQuestHandPose half_fist
mp.CaptureQuestHandPose closed_fist
```

For headset-worn capture, use the VR text guide instead of chat/console prompts:

```text
mp.StartQuestHandCaptureGuide fist
```

It displays large world-space text in front of the HMD and automatically captures three samples for each pose:

```text
10s GET READY / Hands visible
6s OPEN HANDS
6s HALF FIST
6s CLOSED FIST
4s DONE
```

Automatic guide filenames use the pose, run id, and sample index, for example:

```text
open_fist_20260515_123456_00.json
half_fist_fist_20260515_123456_01.json
closed_fist_fist_20260515_123456_02.json
```

Stop it early with:

```text
mp.StopQuestHandCaptureGuide
```

Files are written under:

```text
Saved/QuestHandReplays/<name>.json
```

Each replay JSON stores:

```text
version
keypointCount=26
handTrackerCount
validHandTrackerCount
hasLeft / hasRight
leftTracked / rightTracked
leftPositionsWorld[26] / rightPositionsWorld[26]
leftRotationsWorld[26] / rightRotationsWorld[26]
leftRadii[26] / rightRadii[26]
```

Replay a captured hand pose without relying on the headset session:

```text
mp.QuestHandReplayFile closed_fist
mp.QuestHandReplay 1
```

Return to live Quest/OpenXR hands:

```text
mp.QuestHandReplay 0
```

No-headset smoke verification after rebuild:

```text
mp.CaptureQuestHandPose codex_smoke_nohand
mp.QuestHandReplayFile codex_smoke_nohand
mp.QuestHandReplay 1
mp.QuestHandReplay 0

CODEX_HAND_REPLAY_SMOKE2_CVAR mp.QuestHandReplay float=1.0 int=1
CODEX_HAND_REPLAY_SMOKE2_CVAR mp.QuestFingerPreserveSpread float=0.0 int=0
CODEX_HAND_REPLAY_SMOKE2_CVAR mp.MediaPipeShoulderRollbackTrace float=0.0 int=0
CODEX_HAND_REPLAY_SMOKE2_CVAR mp.QuestHandReplay float=0.0 int=0
```

The smoke replay file was validated to contain 26 left/right positions, rotations, and radii. It intentionally has `hasLeft=0` and `hasRight=0` because it was captured outside an active OpenXR hand-tracking session.

## 2026-05-15 Quest Fist Capture Result

Headset-worn VR text capture produced a complete run:

```text
run=fist_20260515_132036
open samples=3, left/right tracked
half_fist samples=3, left/right tracked
closed_fist samples=3, left/right tracked
```

Historical note: these values are still useful if deliberately testing the old curl-only fallback, but they are no longer the active Quest finger solve. The current default is the 2026-05-19 parent-chain `segmentDirection` path at the top of this file.

The captured Quest data showed why the previous curl defaults squashed the fingers:

```text
current old full-curl threshold: 68 deg
captured half_fist curl: about 71 deg
captured closed_fist curl: about 123 deg
```

So the old mapping treated half-fist and closed-fist as the same full-curl pose. The data-tuned defaults are now:

```text
mp.QuestFingerCurlStrength=1.0
mp.QuestFingerMaxCurlDegrees=96.0
mp.QuestFingerCurlFullAngleDegrees=120.0
mp.QuestFingerPreserveSpread=0
```

Rebuilt and smoke-verified:

```text
CODEX_FIST_TUNED_CVAR mp.QuestFingerCurlStrength float=1.0 int=1
CODEX_FIST_TUNED_CVAR mp.QuestFingerMaxCurlDegrees float=96.0 int=96
CODEX_FIST_TUNED_CVAR mp.QuestFingerCurlFullAngleDegrees float=120.0 int=120
CODEX_FIST_TUNED_CVAR mp.QuestFingerPreserveSpread float=0.0 int=0
CODEX_FIST_TUNED_CVAR mp.MediaPipeUseArmIK float=0.0 int=0
CODEX_FIST_TUNED_CVAR mp.QuestWristForceArmIK float=0.0 int=0
CODEX_FIST_TUNED_CVAR mp.MediaPipeDriveLegs float=0.0 int=0
CODEX_FIST_TUNED_CVAR mp.MediaPipeUseLegIK float=0.0 int=0
CODEX_FIST_TUNED_REPLAY_SMOKE_DONE
CODEX_FIST_REPLAY_PREP_DONE
CODEX_FIST_REPLAY_CLEANUP_DONE
```

Quick revert to the pre-capture finger curl profile:

```text
mp.QuestFingerCurlStrength 1.30
mp.QuestFingerMaxCurlDegrees 112
mp.QuestFingerCurlFullAngleDegrees 68
mp.QuestFingerPreserveSpread 0
```

## 2026-05-15 Performance Evidence

Measured in PIE through Unreal CSV profiler, 12 second samples, current `Lvl_ThirdPerson` Auto Quest path:

```text
Baseline diagnostics on: Saved/Profiling/CSV/Profile(20260515_091610).csv
  FrameTime avg 332.876 ms, GameThread avg 14.248 ms, RenderThread avg 12.455 ms, GPU avg 6.346 ms, FMsgLogf avg 8.000/frame
  Log window: QuestWristSolve=74, QuestWristRollCompact=74, TorsoDebug=74, QuestHandDivergence=74, AutoQuestMirror=48, TotalLogMediaPipePose=389

Post-build diagnostics off: Saved/Profiling/CSV/Profile(20260515_093129).csv
  FrameTime avg 333.405 ms, GameThread avg 11.132 ms, RenderThread avg 10.585 ms, GPU avg 6.129 ms, FMsgLogf avg 0.216/frame
  Log window: QuestWristSolve=0, QuestWristRollCompact=0, TorsoDebug=0, QuestHandDivergence=0, AutoQuestMirror=0, TotalLogMediaPipePose=0

No-AutoQuest isolation: Saved/Profiling/CSV/Profile(20260515_093345).csv
  FrameTime avg 333.352 ms, GameThread avg 9.031 ms, RenderThread avg 6.779 ms, GPU avg 5.521 ms
```

Conclusion: diagnostic logging was a measured CPU/thread cost and is now off by default without changing tracking/deformation CVars. The persistent 333 ms frame cadence also occurs with `mp.AutoQuestWebcamHands=0`, so that cadence is not caused by Wallace, MediaPipe capture, Quest/OpenXR hands, or the diagnostic-log change in this automation run.

## 2026-05-15 Quest Headset VR Preview Evidence

The user reported the bad performance only when wearing the Quest 3 headset. Desktop PIE/monitor preview was not treated as sufficient proof.

Earlier headset-active OpenXR/VR Preview evidence showed the render budget had drifted back to full editor values:

```text
Saved\Logs\TestingKit3_2.log
CODEX_HMD_CVAR r.ScreenPercentage float=100.0 int=100
CODEX_HMD_CVAR r.SkeletalMeshLODBias float=0.0 int=0
CODEX_HMD_CVAR r.ViewDistanceScale float=1.0 int=1
```

The first headset-specific fix proved the VR Preview/OpenXR route was being reached, but it used an overly blunt low-cost profile: `r.ScreenPercentage=50`, `sg.TextureQuality=0`, `r.SkeletalMeshLODBias=2`, and hair disabled. That solved the render-budget drift but made Wallace too poor to show the advantage of a MetaHuman.

The deeper quality/performance sweep found the main visual losses were texture mip bias and Wallace LODSync, not the lighting/post-process cuts. The rebuilt balanced profile keeps expensive lighting/post effects down but restores MetaHuman fidelity:

```text
Saved\Logs\TestingKit3.log
CODEX_VRPREVIEW_WORLD count=1 names=Lvl_ThirdPerson
CODEX_VRPREVIEW_CVAR mp.AutoQuestVrPerfProfile float=1.0 int=1
CODEX_VRPREVIEW_CVAR mp.AutoQuestVrScreenPercentage float=70.0 int=70
CODEX_VRPREVIEW_CVAR mp.AutoQuestVrSkeletalMeshLodBias float=0.0 int=0
CODEX_VRPREVIEW_CVAR mp.AutoQuestVrViewDistanceScale float=0.800000011920929 int=0
CODEX_VRPREVIEW_CVAR mp.AutoQuestVrTextureQuality float=2.0 int=2
CODEX_VRPREVIEW_CVAR mp.AutoQuestVrAntiAliasingQuality float=1.0 int=1
CODEX_VRPREVIEW_CVAR mp.AutoQuestVrHairStrands float=1.0 int=1
CODEX_VRPREVIEW_CVAR mp.AutoQuestVrMetaHumanForcedLod float=1.0 int=1
CODEX_VRPREVIEW_CVAR r.ScreenPercentage float=70.0 int=70
CODEX_VRPREVIEW_CVAR r.SkeletalMeshLODBias float=0.0 int=0
CODEX_VRPREVIEW_CVAR r.ViewDistanceScale float=0.800000011920929 int=0
CODEX_VRPREVIEW_CVAR sg.TextureQuality float=2.0 int=2
CODEX_VRPREVIEW_CVAR sg.AntiAliasingQuality float=1.0 int=1
CODEX_VRPREVIEW_CVAR r.Streaming.MipBias float=0.0 int=0
CODEX_VRPREVIEW_CVAR r.Streaming.PoolSize float=1000.0 int=1000
CODEX_VRPREVIEW_CVAR r.MaxAnisotropy float=4.0 int=4
CODEX_VRPREVIEW_CVAR r.HairStrands.Enable float=1.0 int=1
CODEX_VRPREVIEW_CVAR r.HairStrands.Strands float=1.0 int=1
CODEX_VRPREVIEW_CVAR r.ShadowQuality float=0.0 int=0
CODEX_VRPREVIEW_CVAR r.Shadow.MaxResolution float=512.0 int=512
CODEX_VRPREVIEW_CVAR r.MotionBlurQuality float=0.0 int=0
CODEX_VRPREVIEW_CVAR r.BloomQuality float=0.0 int=0
CODEX_VRPREVIEW_CVAR r.AmbientOcclusionLevels float=0.0 int=0
CODEX_VRPREVIEW_CVAR r.SSR.Quality float=0.0 int=0
CODEX_VRPREVIEW_CVAR r.VolumetricFog float=0.0 int=0
```

Wallace LODSync/component proof from the same rebuilt VR Preview:

```text
CODEX_VERIFY_WALLACE_COUNT 1
CODEX_VERIFY_LODSYNC LODSync forced=1 min=0 num=8 debug=Body : 0 (0) | Face : 1 (2) | Torso : 0 (0) | Legs : 0 (0) | Feet : 0 (0) | Hair : 1 | Eyebrows : 1 | Fuzz : 1 | Eyelashes : 1 | Mustache : 1 | Beard : 1 |
CODEX_VERIFY_SKEL Body forced_lod_model=1 mesh=m_med_unw_body
CODEX_VERIFY_SKEL Face forced_lod_model=2 mesh=Wallace_FaceMesh
CODEX_VERIFY_SKEL Torso forced_lod_model=1 mesh=m_med_unw_top_crewneckt_nrm_Cinematic
CODEX_VERIFY_SKEL Legs forced_lod_model=1 mesh=m_med_unw_btm_jeans_slm_Cinematic
CODEX_VERIFY_SKEL Feet forced_lod_model=1 mesh=m_med_unw_shs_boots_Cinematic
```

Profiler sweep, same VR Preview automation caveat as below:

```text
current_low_auto, Profile(20260515_103626).csv:
  GT 12.255 ms, RT 7.205 ms, GPU 2.510 ms, draw calls 282.440, primitives 43,283.800

metahuman_lod1_tex_sp70, Profile(20260515_103656).csv:
  GT 11.885 ms, RT 5.808 ms, GPU 2.716 ms, draw calls 288.200, primitives 255,914.962

metahuman_lod1_hair_sp70, Profile(20260515_103726).csv:
  GT 12.167 ms, RT 6.006 ms, GPU 2.810 ms, draw calls 276.808, primitives 265,199.160

rebuilt_balanced_default, Profile(20260515_104511).csv:
  50 rows, FrameTime 333.330 ms, GT 12.877 ms, RT 7.901 ms, GPU 4.468 ms,
  draw calls 323.143, primitives 265,763.000, Groom ticks 6, SkeletalMesh ticks 7,
  Animation 0.386 ms, Local GPU memory 3140.254 MB
```

Visual checkpoint:

```text
Saved\CodexAgent\Screenshots\wallace_vr_quality_balanced_sp70.png
worldKind=PIE, focus requested/found MP_LiveMetaHumanWallace, skyLikeRatio=0
```

The same VR Preview CVar read confirmed the tracking/deformation defaults were preserved:

```text
mp.AutoQuestAvatar=1
mp.AutoQuestWebcamHands=1
mp.QuestHandRotationBlend=1
mp.QuestWristDriveTwistCorrection=0
mp.MediaPipeUseArmIK=0
mp.QuestWristCalibrationGate=0
mp.MediaPipeTorsoUseActorForward=1
mp.QuestWristTrace=0
mp.MediaPipeTorsoDebug=0
mp.AutoQuestMirrorDebug=0
```

Caveat: this validation launched the actual VR Preview/OpenXR route and detected a connected `Meta Quest 3`, but the HMD state reported `NOT_WORN`, `is_head_mounted_display_enabled=False`, and zero pose. Do not present this as a subjective headset-worn pass; it proves the VR Preview render budget and Wallace quality profile are now applied on the correct runtime path.

## Historical 2026-05-16 Performance Checkpoint

This older performance checkpoint kept embodied Wallace/profile 4 active, turned the embodied planar-reflection mirror off, and reduced the station-refresh timer path that previously ran expensive placement/visibility/mirror work every `0.016s`. After the 2026-05-17 arm rollback, keep the performance changes, but do not treat profile 4 as the active arm default.

Historical default/readback verification from that rebuilt and reloaded editor:

```text
CODEX_PATCH_VERIFY_CVAR mp.AutoQuestEmbodiedMirror float=0.0 int=0
CODEX_PATCH_VERIFY_CVAR mp.AutoQuestArmReachAssistProfile float=4.0 int=4
CODEX_PATCH_VERIFY_CVAR mp.AutoQuestEmbodiedView float=1.0 int=1
CODEX_PATCH_VERIFY_CVAR mp.AutoQuestStationTimerIntervalSeconds float=0.033 int=0
CODEX_PATCH_VERIFY_CVAR mp.AutoQuestStationRefreshIntervalSeconds float=0.25 int=0
CODEX_PATCH_VERIFY_CVAR mp.AutoQuestCameraPinIntervalSeconds float=0.033 int=0
CODEX_PATCH_VERIFY_CVAR mp.MediaPipeUseArmIK float=0.0 int=0
CODEX_PATCH_VERIFY_CVAR mp.QuestWristForceArmIK float=0.0 int=0
CODEX_PATCH_VERIFY_CVAR mp.MediaPipeDriveLegs float=0.0 int=0
CODEX_PATCH_VERIFY_CVAR mp.MediaPipeUseLegIK float=0.0 int=0
```

Short regular-PIE smoke test with `mp.AutoQuestMediaPipeStats=1` verified that the live actors spawn without the embodied mirror:

```text
CODEX_PATCH_PIE_ACTOR label=MP_LiveMediaPipeManny class=MediaPipePoseDrivenSkeletalActor
CODEX_PATCH_PIE_ACTOR label=MP_LiveMediaPipeVideo class=MediaPipeQuestWebcamSourceActor
CODEX_PATCH_PIE_ACTOR label=MP_LiveMetaHumanWallace class=BP_Wallace_C
CODEX_PATCH_PIE_MIRROR_PLANE_COUNT 0
CODEX_PATCH_PIE_REFLECTION_COUNT 0
CODEX_PATCH_PIE_TRACKER max_hz=15.0 async=True conditioning=True
```

The new stats logger emitted the first concrete pipeline timings in regular PIE:

```text
AutoQuest MP stats: maxHz=15.0 pub/enq/work=0.0/3.0/3.0Hz readback=331.55/338.38ms native=20.30/24.32ms queue=0.01/0.05ms convert=0.42/0.57ms overwrite+0 gate+0 cap=640x480 inf=512x384 mediaFps=30.0
```

Do not treat the `3.0Hz` regular-PIE publish/enqueue rate as headset-worn truth; it matches the automation/editor frame pacing problem seen in CSV profiling. The important checkpoint is that the diagnostic now separates readback, native MediaPipe, queue, conversion, overwrite, and gate-skip costs for the next worn-headset run.

### 30 Hz / Smaller Input Test

On 2026-05-16, Auto Quest MediaPipe capture was changed from the old `15 Hz / 512px` default to a test default of `30 Hz / 384px`. The previous hard `FMath::Max(512, ...)` clamp was removed and replaced with a `256..1024` clamp, so lower dimensions now actually reach the inference path.

Rebuilt/reloaded editor verification:

```text
CODEX_30_384_VERIFY_CVAR mp.AutoQuestWebcamHandsHz float=30.0 int=30
CODEX_30_384_VERIFY_CVAR mp.AutoQuestWebcamHandsInputMaxDimension float=384.0 int=384
CODEX_30_384_VERIFY_CVAR mp.AutoQuestArmReachAssistProfile float=4.0 int=4
CODEX_30_384_VERIFY_CVAR mp.AutoQuestEmbodiedView float=1.0 int=1
CODEX_30_384_VERIFY_CVAR mp.AutoQuestEmbodiedMirror float=0.0 int=0
```

Regular-PIE smoke confirmed the spawned tracker uses the new values and the mirror remains off:

```text
CODEX_30_384_PIE_TRACKER max_hz=30.0 async=True conditioning=True
CODEX_30_384_PIE_CVAR mp.MediaPipeInputMaxDimension float=384.0 int=384
CODEX_30_384_PIE_MIRROR_PLANE_COUNT 0
CODEX_30_384_PIE_REFLECTION_COUNT 0
```

Regular-PIE stats at `30 Hz / 384px`:

```text
AutoQuest MP stats: maxHz=30.0 pub/enq/work=0.0/3.0/3.0Hz readback=331.95/334.50ms native=20.58/25.05ms queue=0.01/0.02ms convert=0.25/0.33ms overwrite+0 gate+0 cap=640x480 inf=384x288 mediaFps=30.0
```

A one-off `30 Hz / 320px` runtime test reduced conversion again but did not improve the main readback/native bottlenecks, so the final code default remains `384px`, not `320px`:

```text
AutoQuest MP stats: maxHz=30.0 pub/enq/work=0.0/3.0/3.0Hz readback=332.94/354.82ms native=20.29/38.47ms queue=0.01/0.02ms convert=0.18/0.34ms overwrite+0 gate+0 cap=640x480 inf=320x240 mediaFps=30.0
```

Conclusion from regular PIE: smaller input reduces conversion cost (`512px` about `0.42ms`, `384px` about `0.25ms`, `320px` about `0.18ms`), but it did not move the dominant readback stall or the roughly `20ms` native MediaPipe processing cost. The next meaningful check must be a worn-headset VR run or a raw-camera input path that avoids GPU texture readback.

## Next Agent Instruction

Start by reading this file. Keep these defaults fixed unless the user explicitly asks for a targeted change. If a new tracking or deformation problem appears, re-enable only the needed diagnostic CVar, gather logs first, and make one measured change at a time.
