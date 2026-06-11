# Avatar Embodiment Refactor Steps

Project: `D:\Epic\Unreal_Projects\TestingKit3`

Rule for this work: normal PIE and automation checks are allowed; VR Preview is for Alan to test only.

## Progress

- [x] Tracker created in the project root as `steps.MD`.
- [x] 1. Freeze the current working VR Preview baseline before structural changes.
- [x] 2. Extract a generic avatar profile contract from Manny, MetaHumans, and the Owen reference patterns.
- [x] 3. Build a single profile-driven embodiment solver for camera-to-avatar anchoring.
- [x] 4. Add a local-view geometry policy that keeps embodiment visible while preventing camera-in-body artifacts.
- [x] 5. Route Quest hand and arm mapping through the same avatar-space profile data.
- [x] 6. Replace fragile level/station-facing logic with a component or pawn boundary where the level only supplies start transform.
- [x] 7. Add diagnostics before tuning so HMD, avatar eye, root, and wrist-space errors are visible.
- [x] 8. Validate with build, automation, and normal PIE only.
- [x] 9. Leave simple user-facing profile commands for VR Preview checks.

## Current Notes

- The intended end state is a generic avatar embodiment path. Manny, Wallace, Emory, Hudson, Kellan, Maria, Payton, and future avatars should be selected by profile data rather than avatar-specific solver branches.
- The avatar should remain visible locally. Only head, face, eyes, hair, or other camera-intersecting pieces may be hidden or clipped for local view.
- HMD height may be logged for diagnostics, but it should not be applied to runtime placement until the avatar anchor math is stable.
- Baseline frozen before edits: `mp.AutoQuestAvatar=1` uses the active MetaHuman profile; `mp.AutoQuestAvatar=0` uses the internal Manny-like rig profile. VR Preview remains user-tested only. Tracking origin must remain stable local tracking, not the old ceiling/LocalFloor placement path.
- The old Wallace-only arm-source switches are historical compatibility, not the current workflow. Current profile switching should remain `mp.MetaHumanActiveProfile <ProfileId>`.
- Added `FMediaPipeAvatarEmbodimentProfile` and `FMediaPipeAvatarEmbodimentSolver` in `Source/MediaPipeDriver/MediaPipeAvatarEmbodimentProfile.h/.cpp`.
- `ResolveEmbodiedStation()` now solves camera-to-avatar anchoring from the active embodiment profile. It no longer has a Wallace-only yaw placement branch.
- Quest HMD-relative wrist mapping and Quest hand debug skeleton mapping now call the same avatar-space solver instead of duplicating forward-axis and eye-anchor math.
- Local view owner-no-see is now profile-policy based and refuses to hide single-mesh avatars by default.
- Runtime embodied logs now include `profile`, `avatarEye`, `avatarForward`, `anchorMode`, and `liveHmdAnchor`.
- Verification on 2026-05-23: `TestingKit3Editor Win64 Development` build succeeded; `TestingKit3.MediaPipe.AvatarEmbodiment` passed 4 tests; broad `TestingKit3.MediaPipe` automation passed 61 tests; normal PIE on `/Game/MetaHumanRooms/L_MetaHumanPreviewRoom_02` started/stopped without VR Preview and spawned Manny at `(0.00,-176.66,2.00)` with the camera at `(0.00,-170.00,164.58)`.

## VR Preview Commands

Manny-like embodied test:

```text
mp.AutoQuestWebcamHands 1
mp.AutoQuestAvatar 0
mp.AutoQuestEmbodiedView 1
```

MetaHuman embodied test:

```text
mp.AutoQuestWebcamHands 1
mp.AutoQuestAvatar 1
mp.AutoQuestEmbodiedView 1
mp.MetaHumanActiveProfile Emory
```

Swap `Emory` for `Wallace`, `Hudson`, `Kellan`, `Maria`, or `Payton`.

Do not use `mp.MetaHumanArmSource -1` for normal testing. The profile-driven default is already active.

## Next Work

The hybrid HMD/Quest/MediaPipe body-fusion checklist is tracked separately in:

```text
body_fusion_steps.MD
```
