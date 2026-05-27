# MediaPipe Shoulder Baseline

Frozen date: 2026-05-07

Source-layout note updated: 2026-05-17

## Status

This is the current best TestingKit3 MediaPipe shoulder baseline. The shoulders are not perfect, but this state is visually better than the earlier left/right mismatch, upside-down shoulder roll, and frozen-arm states.

Do not tune cvars or swap arm solve branches casually from this point. Treat changes to this area as code-side retarget changes that must be tested against the same command below.

## Test Command

Run this in the Unreal console:

```text
mp.PlayMediaPipeVisualCycle clip=riverside conditioning=1 speed=1
```

`clip=riverside` is an alias for the riverbank clip:

```text
Saved/Videos/01_09_riverbank_jumps.mp4
```

## Frozen Runtime Profile

`mp.PlayMediaPipeVisualCycle` applies the frozen profile in:

```text
Source/MediaPipeDriverEditor/MediaPipeLiveVideoCommands.cpp
```

Important arm/shoulder values:

```text
mp.MediaPipeDriveClavicles=0
mp.MediaPipeUseArmIK=0
mp.MediaPipeDriveHandRotation=0
mp.MediaPipeDriveArmTwistBones=0
mp.MediaPipeArmUseElbowPlaneRoll=0
mp.MediaPipeUpperArmTwistWeight=0.0
mp.MediaPipeLowerArmTwistWeight=0.0
mp.MediaPipeArmTargetHalfLife=0.08
mp.MediaPipeArmRotationHalfLife=0.06
mp.MediaPipeArmReliabilityGate=0
mp.MediaPipeArmRotationMaxStepDegrees=0.0
mp.MediaPipeArmRotationMaxSpeedDegreesPerSecond=0.0
```

Source conditioning values used by the same profile:

```text
mp.MediaPipeSourceSmoothingHalfLife=0.16
mp.MediaPipeSourceSmoothingFastSpeed=6.0
mp.MediaPipeSourceOcclusionArmHold=0
mp.MediaPipeSourceOcclusionShoulderReconstruct=0
mp.MediaPipeInputMaxDimension=512
```

## Frozen Code Path

The current direct arm path is:

```text
Source/MediaPipeDriver/MediaPipePoseDrivenAnimInstance.cpp
Source/MediaPipeDriver/MediaPipePoseDrivenAnimInstance_ArmTwist.inl
Source/MediaPipeDriver/MediaPipePoseDrivenAnimInstance_QuestArmSolve.inl
Source/MediaPipeDriver/MediaPipePoseDrivenSolverState.h
```

The old monolithic AnimInstance file has been refactored. The current source map and line counts are recorded in:

```text
Docs/MEDIAPIPE_REFACTOR_STATE_2026-05-17.md
```

The important implementation choices are:

- Direct segment alignment is used, not two-bone arm IK.
- Elbow plane roll is disabled for the stable baseline.
- Upper/lower arm endpoint direction remains matched to the source.
- Upper-arm visual roll is driven through a separate surface reference basis, not the elbow-bend pole basis.
- The stable surface-up hint has a pose-aware rear bias for downward/hanging arms, so shoulder patches sit farther back than the earlier upside-down/forward state.

## Validation Evidence

Latest validation run:

```text
mp.PlayMediaPipeVisualCycle clip=riverside conditioning=1 speed=1
```

Observed in `Saved/Logs/TestingKit3.log`:

```text
mp.PlayMediaPipeVisualCycle: clip 1/1 riverbank
mp.PlayMediaPipeVisualCycle: complete. Run the command again to restart.
```

The shoulder diagnostic lines showed:

```text
upper_dir_error_deg=0.0
```

for both arms during the riverbank run, meaning the arm segment endpoints were still tracking while the shoulder surface-roll basis was adjusted.

No new crash folder was created during the validation run, and the editor remained responsive.

## Known Remaining Issue

The grey/dark shoulder material may still appear slightly too visible under the front of the deltoids in some poses. This baseline is only frozen because it is the best state so far, not because shoulder roll is complete.

Future improvement should continue from the surface-basis code path. Avoid reverting to:

- shared elbow-pole basis for surface roll
- upper-only stable-pole changes
- clamp-only fixes
- screenshot-only validation without log diagnostics
