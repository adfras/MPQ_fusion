# Wallace Arm Pipeline Audit - 2026-05-19

This note captures the current arm process after the repeated arm, wrist, and
finger iterations. It is intentionally an audit, not a new tuning proposal.

## Current Runtime Ownership

The active embodied profile routes the arm through several overlapping systems:

1. Body pose supplies shoulder, elbow, and wrist landmarks.
2. Profile 4 changes the arm frame to the avatar/HMD-relative frame
   (`mp.QuestArmMode=3`).
3. Quest hand tracking can replace the wrist endpoint when tracked and usable.
   In profile 4, `FMediaPipeQuestWristApplyPolicy::ShouldAttemptPositionSolve()`
   owns the high-level decision to enter the Quest wrist-position path. The arm
   solve should not pre-gate that path on `bQuestSideTrackedForArm`, because the
   lower apply policy is where usable-but-temporarily-untracked OpenXR wrist
   continuity is checked.
4. The constrained arm solve then rebuilds the elbow from shoulder, wrist reach,
   and a pole-vector policy. The deterministic target solve now lives in
   `FMediaPipeQuestConstrainedArmSolver::BuildConstrainedArmTarget()`; the anim
   node is responsible for runtime smoothing, diagnostics, and bone writes.
   In HMD-relative profile 4, the MediaPipe elbow hint is first converted by
   `FMediaPipeQuestConstrainedArmSolver::BuildSourceElbowHint()` from the
   original source shoulder/elbow/wrist sample into the target
   avatar-shoulder/Quest-endpoint frame. The constrained solve no longer uses
   the avatar's previous/current elbow as that source hint.
5. After a successful constrained solve, the component-space upper/lower arm
   rotations are built from the same solved elbow plane. This prevents the
   solved wrist/elbow target from being written with a separate stable-surface
   roll basis.
6. The shoulder-rollback hard hold is skipped for the whole HMD-relative
   profile 4 arm path. That guard remains available for direct MediaPipe
   rollback failures, but it no longer has authority to freeze
   `mp.QuestArmMode=3` upper/lower arm rotations during startup, temporary
   Quest wrist loss, or failed constrained-solve frames.
7. Quest hand rotation is applied to the hand bone after the arm solve.
8. Twist/helper deformation bones can be driven after the arm/hand rotations.
   The current default covers the standard twist helpers. The current topology
   guard proves those standard helpers are not ancestors of Wallace hand/finger
   bones, so this post-finger write order cannot stale the standard hand chain.
   Wallace's broader MetaHuman arm sidecar/corrective helper path remains
   available as a diagnostic, but is not a startup default.

The important consequence is that a visual failure at the forearm may not be
caused by the same code path that placed the elbow. Wrist endpoint, elbow solve,
hand rotation, and twist helper deformation are separate passes.

## Current Profile 4 State

Profile 4 is the active Quest embodied arm profile:

- `mp.QuestArmMode=3`
- `mp.QuestWristPositionBlend=1.0`
- `mp.QuestWristRequireTrackedForApply=1`
- `mp.QuestConstrainedArmSolve=1`
- `mp.QuestConstrainedArmSolveBlend=1.0`
- `mp.QuestConstrainedArmWristAuthority=1.0`
- `mp.QuestWristPositionAdaptiveFilter=1`
- `mp.QuestWristReachAssist=1`
- `mp.QuestWristDriftGuard=1`
- `mp.MediaPipeDriveArmTwistBones=1`
- `mp.MediaPipeDriveMetaHumanArmHelpers=0`

Recent code also adds the body-derived fallback for when Quest wrist position is
not applied:

- `mp.QuestConstrainedArmBodyFallback=1`
- `mp.QuestConstrainedArmBodyFallbackWristHalfLife=0.08`
- `mp.QuestConstrainedArmBodyFallbackMaxWristStepCm=14.0`

That fallback is intended to keep an arm-down pose from collapsing or holding a
stale Quest wrist when the hand disappears near the thighs.

The 2026-05-20 cleanup removed another fallback/solve divergence: the
body-derived fallback endpoint now uses the same arms-down straightening helper
as the tracked constrained target solve. That matters because an arm can be in
the same visual down-by-thigh pose while the runtime switches between tracked
Quest wrist and body fallback authority. Before this cleanup, fallback frames
could preserve a short MediaPipe body reach while tracked frames used the
near-full arms-down reach policy.

The same cleanup also made fallback rows auditable in worn-headset logs:
`mp.QuestWristSolve` now records fallback target reach in centimeters, target
reach fraction, whether the fallback used the arms-down straightener, and the
adaptive down-straighten alpha. The VR log gate fails fallback rows that claim
arms-down straightening but report a target reach fraction below `0.996`.

The latest failing VR logs showed why untracked rows cannot be accepted just
because they contain finite joints: rows with `questTracked=0`,
`untrackedData=1`, and `positionApplied=1` were being accepted as real
constrained-arm endpoints. Those short untracked endpoints kept the math
"valid" while the visible arm bent near the thigh. Profile 4 keeps
`mp.QuestWristRequireTrackedForApply=1`; the only untracked exception now is a
continuity-gated constrained-position path where the raw wrist must be near the
last accepted live wrist sample and inside the short tracking-loss age budget.
Discontinuous untracked rows fall back to the held target or MediaPipe body
fallback instead.

A later source audit found why some of those continuity changes could appear
blocked: `DriveArmCS()` was requiring `bQuestSideTrackedForArm` before it would
attempt the Quest wrist-position solve at all. That meant the lower continuity
policy in `TryApplyQuestWristPositionWorld()` could not run on usable OpenXR
joint positions with `tracked=0`. The current code routes that decision through
`ShouldAttemptPositionSolve()` instead. When an accepted live wrist sample is
used, the held Quest wrist target is refreshed from that same sample even if the
tracked bit flickered false, preventing the next temporary-loss frame from
falling back to an older tracked-only held target.

The 2026-05-20 avatar-frame audit found a separate structural mismatch that
could make arm changes look blocked whenever MediaPipe torso basis was missing
or weak. `DriveArmCS()` initialized its no-torso hip/shoulder right, up, and
forward vectors from hard-coded world axes before calling
`TryGetTorsoBasisWorld()`, then later rebuilt the no-torso component-space
write basis from generic component axes. That is wrong for Wallace because the
rest of the embodied path uses Wallace's target component frame and visible
local `+Y` face/chest axis. The current code routes the no-torso seed through
`MediaPipeBodySolverMath::BuildAvatarArmBasis()` and reuses that same
avatar-derived basis for `HipRightComp`, `ShoulderRightComp`, `UpComp`, and
`ForwardComp`, so the fallback constrained arm solve and final pose write keep
Wallace's avatar frame unless live torso landmarks override it.

The later 2026-05-20 source-elbow-hint audit found another structural mismatch
that could make elbow and bicep changes appear blocked. In profile 4,
`DriveArmCS()` kept the original MediaPipe body sample but then fed the avatar's
current elbow into `BuildConstrainedArmTarget()` as `CurrentElbowWorld`. That
mixed a target-skeleton history point into the source-driven Quest endpoint
solve. The current code instead rotates/projects the MediaPipe source elbow pole
into the target avatar-shoulder/Quest-wrist frame, locks it to the current arm
side, and logs the result as `questArmSourceElbowHint` /
`questArmSourceElbow` in `mp.QuestWristSolve` rows.

## Why The Work Became Tangled

The current arm path mixes different kinds of authority:

- Quest wrist position has authority over the wrist endpoint when hand tracking
  is present.
- Body pose and the new body fallback have authority when Quest wrist tracking
  is missing or rejected.
- The constrained solver has authority over elbow placement.
- The legacy shoulder-rollback guard does not have authority in the
  HMD-relative profile 4 path.
- Quest hand rotation has authority over the hand/wrist orientation.
- Twist helper passes can rewrite forearm and upper-arm twist helper rotations
  after the main arm solve.

Because those systems are layered in one evaluation pass, changing a value for
"arm down extension" can also change the wrist reach used by the constrained IK,
which can alter the pole solution, which can make the elbow appear to snap. A
twist-helper change can then make the forearm look broken even when the elbow
solve is numerically valid.

The current default responds to that by adding regression coverage around the
whole arm motion instead of relying on a single isolated pose. The
`TestingKit3.MediaPipe.QuestConstrainedArm.FullMotionSweep` test drives both
arms through down-by-thigh, forward, side, and return-to-down poses while
feeding deliberately bad alternating MediaPipe elbow hints. It fails if the
solver changes anatomical segment lengths, picks the wrong side branch, loses
near-full arms-down reach, flips the elbow pole, or steps the elbow too far in
one frame.

## 2026-05-19 Guard Isolation Finding

The strongest code-level match for the "biceps locked", "arms do not extend",
and snapping reports was the shoulder-rollback guard. It was enabled by default
with `mp.MediaPipeShoulderRollbackGuardBlend=0.0`, which means a detected
rollback frame could hard-hold the previous upper and lower arm rotations.

That behavior is useful only for the older direct MediaPipe arm path. In profile
4, the constrained solver already owns endpoint selection, elbow pole
continuity, and branch repair. The guard policy now keeps the rollback hard hold
out of a successful Quest constrained solve so it cannot block arms-down
extension or make later constrained-solver fixes appear to do nothing.

## Current Twist/Helper Limitation

The runtime references these standard twist helper bones:

- `upperarm_twist_01_l/r`
- `upperarm_twist_02_l/r`
- `lowerarm_twist_01_l/r`
- `lowerarm_twist_02_l/r`

The default profile does not drive the broader MetaHuman corrective or
deformation sidecar bones around the bicep, tricep, shoulder, and forearm. The
2026-05-19 post-audit patches changed the default helper layer without giving
direct Quest wrist roll ownership of the arm:

- stable embodied body now sets `mp.MediaPipeDriveClavicles=0` again for startup stability
- profile 4 now sets `mp.MediaPipeDriveArmTwistBones=1`; the standard target-skeleton helper pass is active but still needs worn-headset acceptance
- profile 4 now sets `mp.MediaPipeDriveMetaHumanArmHelpers=0`; the Wallace MetaHuman sidecar/corrective pass is diagnostic-only until headset evidence proves it helps
- profile 4 leaves `mp.QuestWristUpperArmRollDriveTwistHelpers=0`
- profile 4 leaves `mp.QuestWristUpperArmTwistBlend=0.0`
- profile 4 leaves `mp.QuestWristDriveTwistCorrection=0`

So the user's original observation was valid at audit time: the previous default
was not making the bicep or shoulder absorb wrist roll. The later coupled trial
that enabled clavicles and direct wrist-roll helper ownership was rolled back.
Strict tracked-only wrist application was restored separately after the logs
proved that untracked Quest wrist endpoints were being consumed as real
constrained-arm targets. That was later narrowed to a continuity-gated
untracked exception for constrained wrist position only, so stale/discontinuous
untracked rows still cannot become arm targets. Direct Quest wrist roll is still
not allowed to own the forearm or upper-arm helper bones by default. The
standard target-skeleton twist helper pass is inferred from the solved arm
chain; the broader MetaHuman sidecar-helper pass stays off by default.

The remaining limitation is headset validation and deformation ownership, not
just code coverage of those helper names. The 2026-05-20 topology audit found
that Oculus-style target-skeleton detection includes all 8 standard twist
helpers, those standard helpers are not ancestors of Wallace hand/finger bones,
and 20/20 broad MetaHuman corrective helpers remain outside the startup helper
scope.

## Likely Sources Of Snapping

The most likely snap sources are:

- switching between live Quest wrist, held Quest wrist, and body fallback wrist
  endpoints;
- losing MediaPipe torso basis and falling back from Wallace's avatar frame to a
  generic world/component-axis arm basis; this specific source has now been
  patched with `BuildAvatarArmBasis()` and an avatar-derived component-space
  pose-write basis, but it still needs worn-headset confirmation;
- using the avatar's previous/current elbow as the MediaPipe elbow hint inside
  the HMD-relative Quest endpoint solve; this specific source has now been
  patched with `BuildSourceElbowHint()`, but it still needs worn-headset
  confirmation;
- constrained IK pole-vector selection changing branch near full arm extension;
- arm-down straightening moving reach close to full extension;
- hand rotation arriving after the elbow solve and changing the perceived
  forearm roll;
- helper-bone ownership conflicts if a direct Quest wrist-roll diagnostic and
  the standard arm twist pass both try to write the same helper bones;
- the MetaHuman sidecar/corrective helper layer being inferred from the solved
  arm chain but not yet headset-accepted, so a visible roll artifact may still
  remain in the wrist/forearm surface even when code coverage is broader.

## Post-Audit Patch

After this audit, the default was changed to reduce the coupling that had been
masking several trials:

- `Source/MediaPipeDriver/MediaPipeArmTwistSolver.h/.cpp` now implements the
  standard twist-helper interpolation as a pure helper.
- `DriveArmTwistBonesCS()` now computes helper weights from the reference
  parent-to-source chain, removes swing, applies axis-only twist, and writes via
  `SafeSetCSBoneTransforms()`.
- The standard twist-helper pass is stateless per frame, matching the OculusXR
  inferred twist-joint pass. It derives helpers directly from the current
  component-space parent/source-parent/source frame after the main arm and hand
  have been solved. The earlier per-helper smoothing history was removed because
  it could lag the main arm chain and become a second deformation authority.
- `DriveArmTwistBonesCS()` skips helpers already owned by a direct Quest
  wrist-roll diagnostic path, so the standard pass does not immediately
  overwrite that diagnostic.
- `DriveArmTwistBonesCS()` still has an optional path for Wallace's MetaHuman sidecar helper
  families under `mp.MediaPipeDriveMetaHumanArmHelpers=1`: clavicle out/scap/pec,
  upper-arm twist-corrective/bicep/tricep/corrective-root/fwd/bck/in/out,
  lower-arm corrective-root/fwd/bck/in/out helpers, and `wrist_inner/outer_*`.
  The 2026-05-20 correction matters because those wrist helpers were already
  registered in the lower-arm helper array but the runtime only drove the first
  5 of 7 lower-arm helpers. They now use the same lowerarm-to-hand sidecar
  interpolation rather than being left to inherit raw hand motion only.
  Disabling `mp.MediaPipeDriveArmTwistBones` resets helper runtime state and
  stops the standard helper writes; disabling only
  `mp.MediaPipeDriveMetaHumanArmHelpers` resets only that broader sidecar layer.
  Profile 4 now leaves this broader sidecar layer off.
- The direct forearm-roll diagnostic path now initializes smoothed forearm roll
  at zero and ramps toward the target, matching the upper-arm diagnostic path
  instead of stepping to the target on first activation.
- Profile 4 now enables `mp.MediaPipeDriveArmTwistBones=1` for standard
  target-skeleton helper interpolation. Use `mp.MediaPipeDriveArmTwistBones=0`
  as the quick isolation switch if VR Preview shows this helper pass is
  visually regressing the arm.
- Stable embodied mode now keeps `mp.MediaPipeDriveClavicles=0`,
  `mp.MediaPipeDriveSpine=0`, and lower body/pelvis driving off.
- The constrained arm body-fallback continuity now caps elbow movement as well
  as wrist movement when live Quest wrist data drops. This targets the observed
  case where the wrist endpoint could be smoothed while the elbow still snapped
  to a different analytic branch.
- The constrained arm body-fallback endpoint now shares the same arms-down
  straightening helper as `BuildConstrainedArmTarget()`. The new fallback test
  covers a short but clearly downward MediaPipe fallback pose and verifies that
  it reaches near-full straightness without forcing an unrelated bent pose
  straight.
- The constrained arm state now keeps the last elbow/pole solve through a short
  tracking gap instead of clearing it on the first missed constrained frame.
  That preserved pole history is age-gated so stale solves are still discarded.
- `Source/MediaPipeDriver/MediaPipeQuestConstrainedArmSolver.h/.cpp` now owns
  the deterministic constrained target solve: reach clamp, optional arm-down
  straightening, stable pole choice, low-weight MediaPipe elbow hint,
  small-wrist-step pole continuity, near-full pole continuity, and branch-flip
  repair.
- `Source/MediaPipeDriver/MediaPipeBodySolverMath.h/.cpp` now owns the solved
  elbow-plane arm basis helper used by the pose-write step. When
  `bQuestConstrainedArmSolveApplied` is true, `DriveArmCS()` uses
  `TryBuildSolvedElbowPlaneArmRotations()` so the component-space bone rotations
  reconstruct the constrained solver's `PoseUpperComp` and `PoseLowerComp`
  directions. Near-singular elbow planes intentionally fall back to the stable
  surface path.
- The no-torso stable pole is now side-aware. The constrained solver and the
  legacy reach-assist fallback use the side's shoulder-right vector, or a
  left/right world fallback if the torso basis is unavailable, instead of a
  generic right-side pole. This covers the down-by-thigh case where a weak
  webcam torso basis could make the left elbow choose the wrong branch.
- `Source/MediaPipeDriver/MediaPipePoseDrivenAnimInstance_QuestArmSolve.inl`
  now builds a solver input and applies the result. It no longer carries a
  second inline copy of the target solve logic inside the animation node.
- `TestingKit3.MediaPipe.QuestConstrainedArm.TargetSolve` covers arms-down
  extension, diagonal by-thigh extension, adaptive down-straighten bounds,
  near-full pole continuity, branch-flip repair, side-aware no-torso left/right
  elbow poles, and no-history startup behavior.
- `TestingKit3.MediaPipe.QuestConstrainedArm.TrajectoryContinuity` covers the
  screenshot-class failure where a clearly downward but too-short endpoint and
  alternating noisy MediaPipe elbows must still produce near-full arm extension
  without pole flips. It also covers a no-torso side-of-body arms-down
  trajectory and a small forward-reach trajectory.
- `TestingKit3.MediaPipe.BodySolverMath.SolvedElbowPlaneArmBasis` covers the
  solved-target-to-component-space-rotation boundary and rejects near-singular
  planes for the stable fallback.
- The current default arms-down pass is deliberately stricter than the earlier
  headset-trial settings: `DownStraightenThresholdCm=22.0`,
  `DownStraightenMaxCm=18.0`, and both reach floor/max fractions are `0.997`.
  The solver can internally expand the correction budget for endpoints that are
  deeply below the shoulder, including diagonal by-thigh endpoints that are not
  almost vertical from the shoulder. The focused tests now assert greater than
  99.6% arm reach, the 99.7% reach cap, diagonal by-thigh straightening, less
  than 3 cm elbow pole offset, no pole flip across a small wrist move, and
  bounded trajectory elbow motion. This is automation proof, not worn-headset
  acceptance.
- The current profile-4 reach cap is now explicit:
  `mp.QuestConstrainedArmMaxReachFraction=0.997`. `DriveArmCS()` passes that
  value to the body fallback endpoint, the pre-solve wrist clamp, and the
  constrained target solve, so forward and side full extension no longer inherit
  the older hidden `0.985` clamp. Focused target tests cover both non-down
  full-extension directions without using the arms-down straightener.
- The constrained-solve pose-write threshold is now separate from the direct
  MediaPipe elbow-plane threshold. Profile 4 sets
  `mp.QuestConstrainedArmSolvedPlaneMinSin=0.08`, and `DriveArmCS()` uses it
  only when `bQuestConstrainedArmSolveApplied` is true. This keeps the
  solver-owned near-full pole from being thrown away by the write layer while
  preserving the stricter direct MediaPipe fallback for unsolved arm frames.

Validation so far: editor build passed, the focused
`TestingKit3.MediaPipe.QuestConstrainedArm.BodyFallback`,
`TestingKit3.MediaPipe.QuestConstrainedArm.TargetSolve`, and
`TestingKit3.MediaPipe.ArmTwist.OculusStyleInterpolation` automation coverage
passed through the full `TestingKit3.MediaPipe` automation filter. After the
earlier standard twist-helper interpolation patch, the editor rebuild passed again, the
focused `TestingKit3.MediaPipe.ArmTwist` and
`TestingKit3.MediaPipe.PoseDrivenSolverState` filters passed, and the full
filter passed again. The later stricter arms-down default build passed the
focused `TestingKit3.MediaPipe.QuestConstrainedArm` filter. The guard-isolation
patch then passed focused `TestingKit3.MediaPipe.ArmGuardPolicy`, focused
`TestingKit3.MediaPipe.QuestConstrainedArm`, and the full filter. The adaptive
down-straighten/pole-continuity patch passed the focused 3-test
`TestingKit3.MediaPipe.QuestConstrainedArm` filter and the full 36-test
`TestingKit3.MediaPipe` filter. The MetaHuman sidecar-helper patch then rebuilt
successfully, passed the focused `TestingKit3.MediaPipe.ArmTwist` filter with
off-axis helper coverage, and passed the full 36-test `TestingKit3.MediaPipe`
filter including `Runtime.CVars` and all constrained-arm tests. A non-visual PIE
startup readback before that patch confirmed the narrowed main-solver defaults:
`clavicles=0`, `wristRequireTracked=0`, `downStraighten=1`, `armTwistBones=1`,
and both direct arm-roll diagnostic switches off. This is not headset
acceptance; the next proof still has to come from worn-headset VR Preview or
Oculus Mirror.

The final 2026-05-19 cleanup rebuilt successfully after restoring tracked-only
Quest wrist application in profile 4 and correcting the Wallace helper hierarchy.
Focused `TestingKit3.MediaPipe.QuestConstrainedArm`,
`TestingKit3.MediaPipe.MetaHumanArmHelpers`,
`TestingKit3.MediaPipe.QuestWrist.ApplyPolicy`, and
`TestingKit3.MediaPipe.Runtime.CVars` filters passed. The apply-policy test
proves that a usable but untracked Quest wrist cannot be consumed by live wrist
position apply when tracked apply is required. The full `TestingKit3.MediaPipe`
filter then found 38 tests and recorded 38 successes. This is still not headset
acceptance.

One later cleanup found a conflicting editor-side setup path:
`ApplyQuestWebcamHandsProfile()` in `MediaPipeLiveVideoCommands.cpp` still forced
`mp.QuestWristRequireTrackedForApply=0`, even though profile 4 now requires
tracked Quest wrists before applying live wrist endpoints. That editor profile is
now aligned to `1`; `TestingKit3Editor` rebuilt successfully and the full
38-test `TestingKit3.MediaPipe` filter passed again.

The 2026-05-20 body-fallback cleanup then rebuilt successfully. Focused
`TestingKit3.MediaPipe.QuestConstrainedArm` found 3 tests and recorded 3
successes, including the new fallback straightening case. The full
`TestingKit3.MediaPipe` filter found 38 tests and recorded 38 successes.
`Tools/AnalyzeWallaceArmTwitchLog.ps1` now has `-RequireRows`,
`-FailOnSpikes`, and `-FailOnBroken` switches so the next worn-headset VR
Preview can fail on missing arm-sanity evidence, frame-to-frame snap rows, or
`broken=1` rows instead of silently producing an empty report.

After adding the fallback target-reach/down-straighten fields to
`mp.QuestWristSolve`, `TestingKit3Editor` rebuilt successfully. The focused
`TestingKit3.MediaPipe.Diagnostics.QuestWristRollCompactFormatter` test passed,
which proves the new fields are emitted by the formatter. The focused
`TestingKit3.MediaPipe.QuestConstrainedArm` filter passed again, and the full
`TestingKit3.MediaPipe` filter found 38 tests and ended with
`Automation Test Queue Empty 38 tests performed`.

The 2026-05-20 side-aware no-torso elbow-pole cleanup rebuilt successfully after
the constrained solver and legacy reach-assist fallback stopped using a generic
right-side pole when the torso basis is unavailable. `Tools/CheckWallaceArmSourceGuards.ps1`
passed with the new side-aware pole checks. Focused
`TestingKit3.MediaPipe.QuestConstrainedArm` found 4 tests and recorded 4
successes, including side-aware no-torso target and trajectory coverage. The full
`TestingKit3.MediaPipe` filter found 39 tests and ended with
`Automation Test Queue Empty 39 tests performed`. This is still source and
automation proof, not worn-headset acceptance.

The same cleanup pass also found an embodied-view default mismatch: the docs
and guardrails said `mp.AutoQuestEmbodiedCameraForwardOffsetCm=0.0`, but
`ApplyAutoQuestProfile()` was still restoring the older 12 cm comparison offset.
The profile now keeps the wearer at Wallace's eye center by setting `0.0`, and
`Tools/CheckWallaceArmSourceGuards.ps1` guards that value. `TestingKit3Editor`
rebuilt successfully after this default cleanup; focused
`TestingKit3.MediaPipe.QuestConstrainedArm` again found 4 tests and passed, and
the full `TestingKit3.MediaPipe` filter again found 39 tests and ended with
`Automation Test Queue Empty 39 tests performed`.

The later 2026-05-20 side-aware body-fallback cleanup found a real branch
divergence rather than another CVar issue: `BuildConstrainedArmTarget()` was
using side-aware stable poles, but `BuildBodyFallbackEndpoint()` still fell back
to a generic pole when the MediaPipe source shoulder, elbow, and wrist were
collinear. That is exactly the shape of an arms-down fallback frame. The fallback
input now carries `bIsLeft` and `ShoulderRightWorld` from `DriveArmCS()`, and the
degenerate fallback pole chooses the correct left/right side. The source guard
now checks those inputs and the side-aware fallback call. Focused
`TestingKit3.MediaPipe.QuestConstrainedArm` found 4 tests and passed, and the
full `TestingKit3.MediaPipe` filter found 39 tests and ended with
`Automation Test Queue Empty 39 tests performed`. This still needs worn-headset
VR Preview confirmation.

`TestingKit3.MediaPipe.QuestConstrainedArm.TrackingLossRecovery` now covers the
sequence that the screenshots and reports pointed to: tracked arms-down solve,
temporary body fallback, and tracked recovery. It asserts near-full reach,
capped fallback wrist/elbow steps, continuity use, and no elbow-pole flip after
fallback. After adding it, the focused QuestConstrainedArm filter found 4 tests
and the full `TestingKit3.MediaPipe` filter found 39 tests and ended with
`Automation Test Queue Empty 39 tests performed.`

The 2026-05-20 wrist-helper runtime correction rebuilt successfully after
`DriveArmTwistBonesCS()` was changed to apply `wrist_inner/outer_*` as the final
two lower-arm MetaHuman helpers. The focused
`TestingKit3.MediaPipe.ArmTwist.OculusStyleInterpolation` test passed with a new
near-hand sidecar case, the focused
`TestingKit3.MediaPipe.MetaHumanArmHelpers.WallaceCoverage` test passed, and the
full `TestingKit3.MediaPipe` filter again found 39 tests and ended with
`Automation Test Queue Empty 39 tests performed.`

The 2026-05-20 usable-untracked endpoint correction addresses the opposite
side of the same OpenXR signal problem. Earlier cleanup made profile 4 require
`tracked=1` because stale untracked rows had produced short bent endpoints, but
older headset traces also showed usable 26-joint OpenXR positions with
`tracked=0`. `FMediaPipeQuestWristApplyPolicy` now allows that case only for the
constrained wrist-position endpoint, and the untracked wrist must be continuous
with the last accepted live wrist sample inside the lost-tracking age and wrist
filter reset-distance budgets. `DriveQuestHandCS()` now receives the current
`FQuestWristMappingTrace` and asks
`FMediaPipeQuestWristApplyPolicy::CanUseQuestHandRotationForCurrentFrame()`
before consuming untracked Quest hand rotation. That keeps wrist and hand
orientation on the same accepted live frame only when the current wrist trace
accepted a mapped live untracked endpoint; held wrist targets, raw-rejected
wrist rows, and body fallback frames do not authorize untracked hand rotation.
The same pass changes
Quest-authoritative hand rotation startup to initialize smoothing from the
current hand pose and caps profile 4 hand rotation with
`mp.QuestHandRotationMaxStepDegrees=18.0`, so reacquisition cannot jump straight
to the Quest palm target in one frame. After the current-frame gate was added,
`Tools/CheckWallaceArmSourceGuards.ps1` passed, `TestingKit3Editor` rebuilt,
focused `TestingKit3.MediaPipe.QuestWrist.ApplyPolicy` found 1 test and passed,
focused `TestingKit3.MediaPipe.Quest` found 13 tests and passed, and the broad
`TestingKit3.MediaPipe` filter found 44 tests and completed with exit code 0.

The follow-up continuity gate rebuilt successfully after adding
`FQuestWristSideRuntimeState::LastAcceptedLiveWristWorld` and expanding
`TestingKit3.MediaPipe.QuestWrist.ApplyPolicy` to reject no-history,
discontinuous, and stale untracked rows. `Tools/CheckWallaceArmSourceGuards.ps1`
passed, focused `TestingKit3.MediaPipe.Quest` found 12 tests and passed, and the
full `TestingKit3.MediaPipe` filter found 39 tests and ended with
`Automation Test Queue Empty 39 tests performed.` The command log printed the
existing OpenXR loader API-version warning under `-NullRHI`, but the automation
queue passed.

The constrained wrist attempt and held-target continuity patch rebuilt
successfully after moving the high-level attempt decision into
`FMediaPipeQuestWristApplyPolicy::ShouldAttemptPositionSolve()` and after
refreshing `QuestWristSideState.HeldTargetWorld` for every accepted live wrist
sample. `Tools/CheckWallaceArmSourceGuards.ps1` now fails if the old
`!bQuestArmUsesConstrainedSolve || bQuestSideTrackedForArm` gate returns, and it
guards the accepted-live held-target update. The editor module rebuilt, focused
`TestingKit3.MediaPipe.QuestWrist.ApplyPolicy` found 1 test and passed, and the
broad `TestingKit3.MediaPipe` filter found 43 tests and ended with
`Automation Test Queue Empty 43 tests performed.` This is source and automation
proof only until worn-headset VR Preview confirms the down-by-thigh behavior.

`Tools/CheckWallaceArmSourceGuards.ps1` was added as a source-level guard for
the drift that made earlier changes appear blocked by another path. It checks
that profile 4 remains mode 3, tracked-required with only the continuity-gated
position exception, fallback-enabled, no-IK, and direct wrist-roll-helper
ownership off; checks the editor hand profile stays tracked-required with direct
roll helpers off; and fails if raw
`SetComponentSpaceTransform()` appears in the pose-driving files where
`SafeSetCSBoneTransforms()` is required. It now also guards helper-bone ownership
so `wrist_inner/outer_*` cannot silently fall out of the lower-arm helper pass
again. As of 2026-05-20 it also guards the OculusXR-style solve order: both
arms are solved before `DriveArmTwistBonesCS()`, and the twist/helper pass runs
before component poses are converted back to local poses. The guard passed after
these changes. After the later source-parent twist-chain correction, the guard
also checks that `FMediaPipeArmTwistSolver` carries `ReferenceSourceParentComponent`
and that runtime helper groups can call the source-parent-aware interpolation
path. After the full-motion solver patch, the same guard also checks for
`LockPoleToSideHemisphere` and the full arm sweep test, so the current
side-aware pole repair cannot silently disappear.

The Wallace helper audit is now exhaustive for arm-region helper naming. In
addition to checking every known helper parent, `WallaceCoverage` fails if any
`clavicle*`, `upperarm*`, `lowerarm*`, or `wrist*` bone exists outside the
mapped main bones, standard twist helpers, and audited helper list. The focused
MetaHumanArmHelpers filter and the then-current full 39-test MediaPipe filter
passed after that stricter check and the wrist-helper runtime correction.

The 2026-05-20 AlanMovement/OculusXRMovement source cross-check confirmed the
default ownership rule. In
`C:\Users\Alan\OneDrive\Documents\Unreal Projects\AlanMovement\Plugins\OculusXRMovement`,
`AnimNode_OculusXRBodyTracking.h` maps `BodyLeftHandWrist` /
`BodyRightHandWrist` to `hand_l` / `hand_r`, while `BodyLeftHandWristTwist` /
`BodyRightHandWristTwist` stay unmapped. `OculusXRAnimNodeBodyRetargeter.cpp`
then builds the mapped frame pose first and calls its twist interpolation pass
afterwards. TestingKit3 now guards the same ordering: `DriveArmCS()` runs for
both arms, `DriveArmTwistBonesCS()` applies inferred twist/helper deformation,
then component poses are converted to local poses. `Tools/CheckWallaceArmSourceGuards.ps1`
now fails if that solve order drifts. `TestingKit3.MediaPipe.MetaHumanArmHelpers.OculusStyleDefaultScope`
also proves the startup helper topology is sidecar-safe: Oculus-style detection
finds 8/8 standard twist helpers, and none of those standard helpers is an
ancestor of Wallace `hand_*` or finger bones.

The later 2026-05-20 source-parent twist-chain correction found a second
OculusXR mismatch in the helper pass. OculusXR stores a twist helper's immediate
parent separately from the mapped source-parent endpoint for unmapped chains.
TestingKit3 was still assuming those were the same transform, so children under
`upperarm_correctiveRoot_*` and `lowerarm_correctiveRoot_*` projected/stretch
against the helper root instead of the real mapped chain
(`upperarm_* -> lowerarm_*` or `lowerarm_* -> hand_*`). `FMediaPipeArmTwistSolver`
now receives `SourceParentComponent` and `ReferenceSourceParentComponent`, and
`TestingKit3.MediaPipe.ArmTwist.OculusStyleInterpolation` has an unmapped-chain
case that proves the weight and translation scale use that source parent. Build,
source guard, focused ArmTwist, and focused Runtime.CVars passed.

The later 2026-05-20 full-motion solver patch found a different structural
snap source: the solver could keep the correct left/right side branch while the
elbow pole still flipped during moderate wrist motion. `BuildConstrainedArmTarget()`
now locks stable poles into the side hemisphere and repairs pole-branch flips
for moderate wrist steps instead of waiting for a visibly large elbow jump.
`BuildBodyFallbackEndpoint()` also routes source poles through the same side
hemisphere lock. Source guard passed, `TestingKit3Editor` rebuilt, focused
`TestingKit3.MediaPipe.QuestConstrainedArm.FullMotionSweep` passed, focused
`TestingKit3.MediaPipe.QuestConstrainedArm` found 5 tests and passed, focused
`TestingKit3.MediaPipe.ArmTwist.OculusStyleInterpolation` passed, focused
`TestingKit3.MediaPipe.Runtime.CVars` passed, and the broad
`TestingKit3.MediaPipe` filter ended cleanly with
`Automation Test Queue Empty 40 tests performed.`

A follow-up audit found a narrow stale-history path in that same repair logic:
branch repair could still reuse the previous elbow pole even if that historical
pole was on the wrong side for the current arm. A broad fix that side-locked
every previous-pole continuity path was rejected because it broke valid curved
motion in `TestingKit3.MediaPipe.QuestConstrainedArm.FullMotionSweep`. The
accepted fix is narrower: branch repair may reuse previous-pole history only
when the resulting repaired elbow stays on the current arm's side. This is now
covered by `TestingKit3.MediaPipe.QuestConstrainedArm.TargetSolve`, guarded by
`Tools/CheckWallaceArmSourceGuards.ps1`, rebuilt successfully, and the broad
`TestingKit3.MediaPipe` filter again ended with
`Automation Test Queue Empty 40 tests performed.`

A second follow-up found the same class of missing side information in body
fallback continuity. `BuildBodyFallbackEndpoint()` was already side-aware, but
`ApplyBodyFallbackContinuity()` blended from the last constrained elbow without
knowing the current shoulder/right-side basis. That meant a temporary Quest
wrist loss or rejected wrist endpoint could preserve stale wrong-side elbow
history during the fallback blend. The fallback continuity input now carries the
target shoulder, arm side, and shoulder-right vector when called from
`DriveArmCS()`. It reuses previous elbow history only when both the old elbow and
the raw fallback elbow are still on the current arm side. Valid current-side
continuity still passes; wrong-side fallback history is rejected in
`TestingKit3.MediaPipe.QuestConstrainedArm.BodyFallback`. The editor module
rebuilt, the focused `TestingKit3.MediaPipe.QuestConstrainedArm` filter passed,
the source guard passed, and the broad `TestingKit3.MediaPipe` filter again
ended with `Automation Test Queue Empty 40 tests performed.`

The next audit found a related blocked-correction path in the target solver:
after the analytic elbow was solved on the correct side, `MaxElbowMoveCm` could
clamp the result from `CurrentElbowWorld`. If that current elbow was already on
the wrong side, the clamp could preserve the wrong branch and make the arm look
locked even though the analytic target was correct. `BuildConstrainedArmTarget()`
now keeps the cap for normal corrections, but it overrides that cap when the
clamped elbow would remain on the wrong side while the analytic elbow is on the
current arm side. `TestingKit3.MediaPipe.QuestConstrainedArm.TargetSolve` covers
this wrong-current case, the source guard checks the side invariant, the editor
module rebuilt, the focused QuestConstrainedArm filter passed, and the broad
`TestingKit3.MediaPipe` filter again ended with
`Automation Test Queue Empty 40 tests performed.`

The same 2026-05-20 pass found one arms-down extension blocker that was not a
CVar tuning issue: the down-straighten correction was gated on MediaPipe torso
basis validity. That is too strict for Quest/HMD-relative arms because the Quest
wrist endpoint can still be valid while the webcam torso basis is weak or
occluded. `FMediaPipeQuestConstrainedArmSolver` now allows arms-down
straightening from the available up vector even without a torso basis, while
still using torso basis when present for stable pole selection. New no-torso
cases in `TestingKit3.MediaPipe.QuestConstrainedArm.BodyFallback` and
`TestingKit3.MediaPipe.QuestConstrainedArm.TargetSolve` cover this.

The later 2026-05-20 diagonal by-thigh pass found another source-policy
blocker: the adaptive straightening budget required the shoulder-to-wrist ray to
be almost vertical. A real hand resting down by the thigh is diagonal from the
shoulder, so the old fixed `DownStraightenMaxCm` budget could preserve a short
endpoint and a visibly bent elbow. `FMediaPipeQuestConstrainedArmSolver` now
adds a side-of-body down alpha for deeply-below-shoulder endpoints, and
`TestingKit3.MediaPipe.QuestConstrainedArm.TargetSolve` covers a diagonal
by-thigh endpoint reaching near-full extension while staying under the 99.7%
singularity cap. The source guard passed, the editor module rebuilt, the focused
QuestConstrainedArm filter passed, and the broad `TestingKit3.MediaPipe` filter
found 41 tests and completed with exit code 0.

The next 2026-05-20 source audit found a separate pose-write mismatch. The
constrained solve could output correct `PoseUpperComp` and `PoseLowerComp`
directions while the non-IK write branch still chose the stable-surface roll
basis because `mp.MediaPipeArmUseElbowPlaneRoll` defaulted to `0`. The current
default now uses the solved elbow-plane arm basis automatically when
`bQuestConstrainedArmSolveApplied` is true. `Tools/CheckWallaceArmSourceGuards.ps1`
guards this route, `TestingKit3Editor` rebuilt successfully, focused
`TestingKit3.MediaPipe.BodySolverMath.SolvedElbowPlaneArmBasis` and focused
`TestingKit3.MediaPipe.QuestConstrainedArm` passed, and the broad
`TestingKit3.MediaPipe` filter found 42 tests and completed with exit code 0.

The following topology guard cross-checked the local AlanMovement/OculusXRMovement
source invariant that mapped frame poses are built before twist helpers are
interpolated. TestingKit3's standard helper pass runs after hand/finger writes,
so `TestingKit3.MediaPipe.MetaHumanArmHelpers.OculusStyleDefaultScope` now
requires all 8 standard twist helpers to be detected by the Oculus-style target
skeleton rule and verifies those helpers are not ancestors of Wallace hand or
finger bones. The focused MetaHuman helper filter passed, the source guard
passed, and the broad `TestingKit3.MediaPipe` filter found 42 tests and
completed with exit code 0.

The next source audit found a full-pipeline snap source outside the arm math:
`PreUpdate()` cleared `bHasPoseFrame` before proving that a replacement
MediaPipe frame existed. If the tracker missed or produced an invalid frame for
one evaluation, `Evaluate_AnyThread()` returned reference pose even though
Quest hand/HMD data could still be current. TestingKit3 now holds the last
accepted MediaPipe body frame across transient dropouts, refreshes the target
mesh component transform every `PreUpdate()`, and only commits a replacement
pose after `MediaPipeSolvedPose::BuildLocal()` succeeds. The frame-associated
MediaPipe raw hand landmarks are held with that body frame and are cleared only
by an explicit retarget reset, so the held body pose cannot lose its matching
fallback hand data. This prevents a one-frame MediaPipe dropout from collapsing
Wallace to reference pose while the Quest hand path continues to update.
`TestingKit3.MediaPipe.PoseFrameContinuity.HoldLastFrameOnDropout` covers the
body-frame and raw-hand hold policy, the source guard checks the runtime
call/reset/target transform refresh and no-early-hand-clear rule,
`TestingKit3Editor` rebuilt successfully, and the broad `TestingKit3.MediaPipe`
filter found 43 tests and completed with exit code 0.

The 2026-05-20 HMD-relative guard cleanup removed one more blocking layer:
`FMediaPipeArmGuardPolicy` now disables the legacy shoulder-rollback hard hold
for the whole `mp.QuestArmMode=3` arm path, not only after a successful
constrained solve. That prevents startup, temporary Quest wrist loss, or
failed-solve frames from freezing a previous upper/lower arm rotation and then
releasing it as a snap. `Tools/CheckWallaceArmSourceGuards.ps1` passed,
`TestingKit3Editor` rebuilt successfully, focused
`TestingKit3.MediaPipe.ArmGuardPolicy` passed, focused
`TestingKit3.MediaPipe.QuestConstrainedArm` found 5 tests and passed under
forced log flush, and the broad `TestingKit3.MediaPipe` filter found 43 tests
and ended with `Automation Test Queue Empty 43 tests performed`. This is still
source and automation proof, not worn-headset acceptance.

The latest constrained wrist attempt cleanup found one more blocked-path issue:
the high-level arm solve could still refuse to enter the Quest wrist-position
path when `bQuestSideTrackedForArm` was false, even though the lower continuity
policy was designed to reject or accept those usable-untracked OpenXR rows
itself. `FMediaPipeQuestWristApplyPolicy::ShouldAttemptPositionSolve()` now owns
that attempt gate, and accepted live wrist samples refresh the held target. The
source guard, editor build, focused `TestingKit3.MediaPipe.QuestWrist.ApplyPolicy`
test, and broad 43-test MediaPipe automation filter passed. This also remains
source and automation proof, not worn-headset acceptance.

The follow-up arm pass removed the independent temporal smoothing from
`DriveArmTwistBonesCS()`. OculusXRMovement's `ProcessFrameInterpolateTwistJoints()`
derives each twist helper directly from the current `FramePoses` array; it does
not keep separate per-helper history. TestingKit3 now matches that behavior for
the active standard helper path. `Tools/CheckWallaceArmSourceGuards.ps1` fails if
`UpdateSmoothedRotation()` returns to `MediaPipePoseDrivenAnimInstance_ArmTwist.inl`.
The source guard passed, `TestingKit3Editor` rebuilt successfully, the focused
`TestingKit3.MediaPipe.ArmTwist.OculusStyleInterpolation` test passed, and the
broad `TestingKit3.MediaPipe` automation filter again found 43 tests and ended
with `Automation Test Queue Empty 43 tests performed`. This is still not
worn-headset acceptance.

The next state cleanup found that an expired or missing held Quest wrist target
returned false to the constrained body fallback but left the old wrist-position
authority/filter state alive. That stale state could make the next reacquired
Quest wrist return at full old authority instead of starting from a clean ramp.
`FMediaPipeQuestWristApplyPolicy::ShouldClearPositionAuthorityForHeldTargetLoss()`
now covers missing, zero-grace, unknown-age, and expired held targets, and
`TryUseHeldQuestWristTarget()` clears the authority/filter state before it
returns false to body fallback. The source guard passed, `TestingKit3Editor`
rebuilt successfully, focused `TestingKit3.MediaPipe.QuestWrist.ApplyPolicy`
passed, and the broad `TestingKit3.MediaPipe` automation filter again found 43
tests and ended with `Automation Test Queue Empty 43 tests performed`. This is
still not worn-headset acceptance.

A follow-up state audit found another split in the same area: the early
`DriveArmCS()` reliability/step/segment gate treated the raw `bHasHeldTarget`
flag as an available wrist-position candidate even when the target was already
outside `mp.QuestWristLostTrackingGraceSeconds` and the later apply path would
reject it. `FMediaPipeQuestWristApplyPolicy::HasFreshHeldTargetForPositionAttempt()`
now owns that freshness rule, and `DriveArmCS()` passes only a fresh held target
into `ShouldAttemptPositionSolve()`. This prevents an expired held Quest wrist
from waiving MediaPipe wrist reliability on the same frame that
`TryUseHeldQuestWristTarget()` clears authority and falls back. The source guard
passed, `TestingKit3Editor` rebuilt successfully, focused
`TestingKit3.MediaPipe.QuestWrist.ApplyPolicy` passed, and the broad
`TestingKit3.MediaPipe` automation filter again found 43 tests and ended with
`Automation Test Queue Empty 43 tests performed`. This is still not
worn-headset acceptance.

A second state-lifetime cleanup found that calibration/anchor resets cleared
apply calibration, last accepted live wrist data, filters, and startup samples,
but did not clear the held Quest wrist target itself. That meant a target mapped
under the previous HMD-relative anchor could remain briefly eligible after a
new anchor or translation-filter reset. `FQuestWristSideRuntimeState::ResetPositionContinuity()`
now clears held targets, last accepted live wrist data, position filters,
startup samples, and authority in one place. `ResetCalibration()` uses it, and
the HMD-relative avatar path calls it when creating the avatar anchor or when
the HMD translation filter resets after a tracking/anchor jump. The source
guard passed, `TestingKit3Editor` rebuilt successfully, focused
`TestingKit3.MediaPipe.QuestWrist` found 4 tests and passed, and the broad
`TestingKit3.MediaPipe` automation filter again found 43 tests and ended with
`Automation Test Queue Empty 43 tests performed`. This is still not
worn-headset acceptance.

The final blocked-path cleanup in this pass found that the optional
`mp.MediaPipeArmHoldOnQuestHandLoss` path could still hard-freeze the last
reliable shoulder/elbow/wrist sample before the Quest endpoint attempt policy
ran. That path is off in profile 4, but leaving it as a raw pre-gate made later
diagnostics unsafe. `FMediaPipeQuestWristApplyPolicy::ShouldHoldArmOnQuestHandLoss()`
now runs after `ShouldAttemptPositionSolve()` and refuses to hold the arm when a
live or fresh-held Quest wrist-position candidate exists. The source guard now
checks this ordering and the focused apply-policy test covers the suppression
case. This is source and automation proof only; it does not replace a worn
headset VR Preview check.

The next OculusXR parity audit found that profile 4 still inherited the older
MediaPipe arm-rotation half-life and per-frame/per-second rotation caps. Those
caps run after the Quest endpoint and constrained elbow solve, so a valid
HMD-relative frame could still be delayed or rate-limited at the final
upper/lower arm write. OculusXRMovement builds the frame pose first and then
writes that frame. `FMediaPipeQuestWristApplyPolicy::ShouldWriteFrameCoherentQuestArmPose()`
now applies the same ownership rule when a profile 4 mapped or body-fallback
Quest wrist endpoint exists, and the profile 4 defaults set the arm target
half-life, arm rotation half-life, max rotation step, and max rotation speed to
zero. This keeps continuity in the endpoint/filter/solver layers and prevents a
second post-solve smoother from making the bicep look locked or releasing as a
snap. `Tools/CheckWallaceArmSourceGuards.ps1` passed, `TestingKit3Editor`
rebuilt successfully, focused `TestingKit3.MediaPipe.QuestWrist.ApplyPolicy`
passed, and the broad `TestingKit3.MediaPipe` filter found 43 tests and
completed with exit code 0. This is source and automation proof only until
worn-headset VR Preview confirms it.

A final hand-rotation ownership audit found that using the wrist continuity
policy alone was still too broad: hand rotation could accept continuous
untracked input even when the current wrist frame had actually fallen back to a
held target, a raw-rejected row, or the constrained body fallback. The runtime
now wires the current `FQuestWristMappingTrace` into `DriveQuestHandCS()` and
requires `CanUseQuestHandRotationForCurrentFrame()` to prove the current wrist
frame was a mapped, position-applied, live untracked endpoint before untracked
hand rotation can be consumed. The source guard now checks this wiring and
regression text, `TestingKit3Editor` rebuilt successfully, focused
`TestingKit3.MediaPipe.QuestWrist.ApplyPolicy` passed, focused
`TestingKit3.MediaPipe.Quest` found 13 tests and passed, and the broad
`TestingKit3.MediaPipe` filter found 44 tests and completed with exit code 0.
This is still source and automation proof only until worn-headset VR Preview
confirms it.

The latest source-frame ownership audit found that the HMD-relative constrained
arm solve still consumed the avatar's current elbow as `CurrentElbowWorld` after
the runtime had already replaced the shoulder/wrist frame with the avatar
shoulder plus Quest endpoint. OculusXRMovement does not mix a target-skeleton
history joint into the source-driven frame pose that way: it retargets mapped
source joints for the current frame first, then interpolates twist helpers from
that current frame. TestingKit3 now keeps the original MediaPipe
shoulder/elbow/wrist sample and calls
`FMediaPipeQuestConstrainedArmSolver::BuildSourceElbowHint()` to rotate/project
the source elbow pole into the target avatar-shoulder/Quest-endpoint frame
before `BuildConstrainedArmTarget()` runs. The path is guarded in
`Tools/CheckWallaceArmSourceGuards.ps1`, logged as `questArmSourceElbowHint` /
`questArmSourceElbow`, and covered by
`TestingKit3.MediaPipe.QuestConstrainedArm.TargetSolve`. `TestingKit3Editor`
rebuilt successfully, focused `TestingKit3.MediaPipe.QuestConstrainedArm`
passed, focused `TestingKit3.MediaPipe.Quest` passed, focused
`TestingKit3.MediaPipe.Diagnostics.QuestWristRollCompactFormatter` passed, and
the broad `TestingKit3.MediaPipe` filter found 44 tests and completed with exit
code 0. This is still source and automation proof only until worn-headset VR
Preview confirms it.

## What Is Stable

The finger default is the best headset-accepted finger state so far and should be
kept separate from arm experiments:

- parent-chain segment-direction finger retarget
- distal/tip damping
- chain curl enabled
- no return to the older joint-orientation or curl-only fallback as the default

The arm fallback math has only been proven by build and automation tests. It has
not been accepted in headset as a final arm behavior.

## Recovery Rule

Do not add another arm tuning change on top of the current state without first
choosing a clean baseline. The next arm pass should isolate one authority layer
at a time:

1. wrist endpoint only,
2. constrained elbow solve only,
3. hand rotation only,
4. twist helper/corrective deformation only.

Each layer should be verified in VR Preview/Oculus Mirror before enabling the
next layer by default. Normal PIE and math tests are not enough for final arm
acceptance.
