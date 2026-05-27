# Avatar Profile-Driven Embodiment - 2026-05-23

This is the current embodiment boundary for Manny-like avatars and MetaHumans in `D:\Epic\Unreal_Projects\TestingKit3`.

The intended rule is simple: avatar-specific facts live in profile data; camera anchoring, avatar forward-axis math, local-view visibility, and HMD-relative wrist mapping use one shared solver.

## Source Boundary

Core profile and solver:

```text
Source/MediaPipeDriver/MediaPipeAvatarEmbodimentProfile.h
Source/MediaPipeDriver/MediaPipeAvatarEmbodimentProfile.cpp
Source/MediaPipeDriver/MediaPipeAvatarEmbodimentProfileTests.cpp
```

Runtime consumers:

```text
Source/MediaPipeDriver/MediaPipeDriver.cpp
Source/MediaPipeDriver/MediaPipePoseDrivenAnimInstance.cpp
Source/MediaPipeDriver/MediaPipePoseDrivenAnimInstance_QuestSpaceMapping.inl
Source/MediaPipeDriver/MediaPipeQuestHandDebugReporter.cpp
```

`FMediaPipeAvatarEmbodimentProfile` contains the stable avatar data:

```text
ProfileId
SkeletonFamily
bUseTargetFaceForwardAxis
EmbodiedYawOffsetDeg
DefaultEyeLocalOffset
EmbodiedCameraForwardOffsetCm
BoneMap
LocalViewPolicy
```

`FMediaPipeAvatarEmbodimentSolver` owns the shared math:

```text
SolveCameraAnchoredAvatar
MapQuestHmdRelativeWristToAvatarWorld
GetAvatarForwardWorld
GetAvatarUpWorld
```

Do not add new character-specific placement or wrist-space branches in `MediaPipeDriver.cpp` or the anim node. Add or adjust profile data instead.

## Current Profiles

Internal Manny-like profile:

```text
ProfileId: InternalMannyLike
Face/target forward axis: local Y
Embodied yaw offset: -90 deg
Default eye local offset: X=0.0, Y=0.66, Z=162.58
Embodied camera forward clearance: 6.0 cm
```

MetaHuman profiles are still defined by the profile registry documented in:

```text
Docs/METAHUMAN_PROFILE_DRIVEN_RETARGETING.md
```

Built-in MetaHuman profile ids:

```text
Wallace
Emory
Hudson
Kellan
Maria
Payton
```

MetaHumans use their profile `FaceForwardAxis`, `EmbodiedYawOffsetDeg`, and `DefaultEyeLocalOffset`. If a live MetaHuman face mesh exposes valid eye sockets, the runtime uses the measured eye midpoint as the profile eye anchor for that actor.

## Local View Policy

The avatar must remain embodied and visible to the wearer. The local-view policy matches the reference Movement project pattern without depending on OculusXR:

- For multi-component avatars, separate head/face/hair/eye-type mesh components are owner-no-see only.
- For single-mesh Manny-like avatars, the runtime creates an owner-only first-person body proxy that copies the driven skeletal pose and hides the local head/neck bone chain. The original mesh is owner-no-see, so mirrors and other non-owner views still see the full avatar.

The mirror and other non-owner views still see the full avatar components.

BodyFusion is currently upper-body only in embodied runtime. HMD/head, fused chest/spine, and Quest arms can drive the embodied pose, but MediaPipe pelvis/legs are not allowed to own runtime lower-body motion until lower-body fusion is separately validated in VR Preview.

## Start Transform Rule

The level supplies the start transform. The solver then anchors the avatar so the player camera lines up with the profile eye anchor and profile forward axis.

This removes the old fragile assumption that every avatar faces actor local X. For local Y-forward avatars such as Manny-like and MetaHumans, the profile yaw offset and forward-axis flag make the same start transform work.

HMD height can be logged and compared, but it is not used to push the avatar root at runtime until the anchor math is stable in VR Preview.

## User-Facing VR Preview Commands

For Manny-like embodied testing:

```text
mp.AutoQuestWebcamHands 1
mp.AutoQuestAvatar 0
mp.AutoQuestEmbodiedView 1
```

For MetaHuman embodied testing:

```text
mp.AutoQuestWebcamHands 1
mp.AutoQuestAvatar 1
mp.AutoQuestEmbodiedView 1
mp.MetaHumanActiveProfile Wallace
```

Swap the profile id with:

```text
Emory
Hudson
Kellan
Maria
Payton
```

Do not use old Wallace-specific arm CVars for normal testing. Do not type `mp.MetaHumanArmSource -1` unless you are deliberately diagnosing the lower-level arm-source resolver; profile-driven is already the default.

## Verification

Completed on 2026-05-23:

```text
TestingKit3Editor Win64 Development build: succeeded
TestingKit3.MediaPipe.AvatarEmbodiment focused automation: 4 tests passed
TestingKit3.MediaPipe broad automation: 61 tests passed
Normal PIE check in /Game/MetaHumanRooms/L_MetaHumanPreviewRoom_02: started and stopped without VR Preview
Normal PIE Manny placement: PlayerCameraManager at (0.00,-170.00,164.58), MP_LiveMediaPipeManny at (0.00,-176.66,2.00), avatar yaw 0.0, camera yaw 90.0
```

The automation command emitted the existing OpenXR loader API-version warning under `-NullRHI`; the tests still completed with exit code 0.
