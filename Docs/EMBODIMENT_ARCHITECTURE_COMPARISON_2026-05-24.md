# Embodiment Architecture Comparison - 2026-05-24

This is the required pre-implementation comparison for copying the working embodiment architecture from:

`C:\Users\Alan\OneDrive\Documents\Unreal Projects\Unreal-Movement`

into:

`D:\Epic\Unreal_Projects\TestingKit3`

## Files And Assets Inspected

Unreal-Movement:

- `MovementSample.uproject`
- `Config/DefaultEngine.ini`
- `Config/DefaultGame.ini`
- `/Game/Maps/MAP_HighFidelity_AnimBlueprint`
- `/Game/Maps/MAP_RetargetMannequinAnimBlueprint`
- `/Game/Pawns/OwenAnimationNodes/BP_OwenFullTracking`
- `/Game/Pawns/OwenAnimationNodes/ABP_OwenFullTracking`
- `/Game/Pawns/UnrealManequin/BP_ManequinRetarget`
- `/Game/Pawns/UnrealManequin/AB_Mannequin`
- `Plugins/OculusXRMovement/OculusXRMovement.uplugin`
- `Plugins/OculusXRMovement/Source/OculusXRMovement/Private/AnimNode_OculusXRBodyTracking.cpp`
- `Plugins/OculusXRMovement/Source/OculusXRMovement/Public/AnimNode_OculusXRBodyTracking.h`

TestingKit3:

- `TestingKit3.uproject`
- `Config/DefaultEngine.ini`
- `/Game/MetaHumanRooms/L_MetaHumanPreviewRoom_02`
- `/Game/ThirdPerson/Lvl_ThirdPerson`
- `/Game/MetaHumanRooms/Blueprints/BP_MetaHumanPreviewRoomAutoQuestStartup`
- `/Game/MetaHumanRooms/Blueprints/BP_MetaHumanPreviewRoomGameMode`
- `/Game/MetaHumanRooms/Blueprints/BP_MetaHumanPreviewRoomPlayer`
- `/Game/Codex/Mirror/BP_VRSelfMirror`
- `Source/MediaPipeDriver/MediaPipeDriver.cpp`
- `Source/MediaPipeDriver/MediaPipePoseDrivenSkeletalActor.*`
- `Source/MediaPipeDriver/MediaPipeAvatarEmbodimentProfile.*`
- `Source/MediaPipeDriver/MediaPipeFirstPersonBodyProxyComponent.*`
- `Source/MediaPipeDriver/MediaPipeRuntimeCVars.*`
- `Source/MediaPipeDriver/MediaPipePoseDrivenAnimInstance_QuestArmSolve.inl`
- `Source/MediaPipeDriver/MediaPipePoseDrivenAnimInstance_QuestHandRotation.inl`
- `Source/MediaPipeDriver/MediaPipePoseDrivenAnimInstance_QuestFingerDrive.inl`
- UE `OpenXRHandTracking` / `IHandTracker` public runtime path, used only as the no-MediaPipe hand-pose input fallback

Generated inspection artifacts:

- `Saved/CodexAgent/EmbodimentInspection/unreal_movement.json`
- `Saved/CodexAgent/EmbodimentInspection/unreal_movement_details.json`
- `Saved/CodexAgent/EmbodimentInspection/testingkit3.json`
- `Saved/CodexAgent/EmbodimentInspection/testingkit3_details.json`
- `Saved/QuestScreenshots/vrpreview_quest_mirror_20260524_123932/manifest.jsonl`

## Comparison Table

| System Area | Unreal-Movement behavior | TestingKit3 behavior | Difference | Required action |
|---|---|---|---|---|
| Working Manny/Owen pawn | Working maps use placed avatar actors. `MAP_HighFidelity_AnimBlueprint` has `BP_OwenFullTracking` with `Auto Possess Player0`, plus `BP_OwenFullTrackingMirror`. `MAP_RetargetMannequinAnimBlueprint` has `MannequinPawn` with `Auto Possess Player0`, plus `MannequinPawn2`. | VR room has `PlayerStart_VRRoom`, `VRRoom_SelfMirror`, and hidden `BP_MetaHumanPreviewRoomAutoQuestStartup_Instance`; no placed Manny pawn owns possession. | Working project starts from a placed avatar pawn/actor; TestingKit3 starts from PlayerStart plus hidden C++ spawn/pin logic. | Add a placed embodied Manny pawn/blueprint path for VR room and make possession originate from that placed asset, not hidden station coordinates. |
| Current TestingKit3 Manny pawn/character | Not applicable; working avatar is the placed pawn/actor itself. | Manny is spawned by `SpawnAutoQuestWebcamHands()` as `AMediaPipePoseDrivenSkeletalActor` at fixed `FVector(350,0,2)` before being repositioned by C++ station logic. | TestingKit3 Manny is a runtime spawned driver actor, not the possessed avatar root. | Rehost the MediaPipe Manny driver under a placed embodied pawn/station so the pawn transform is the source of truth. |
| Pawn / character class structure | Avatar actors are placed in the level. The possessed actor contains root, camera and tracking/mesh components. | Default game config still points at Third Person map/GameMode. VR room GameMode defaults to SpectatorPawn. Auto startup spawns source and Manny actors. | GameMode/PlayerStart path is separate from avatar body. | Introduce a TestingKit3 embodied pawn class/asset or placed startup actor that owns the camera/body relationship and can later select Manny/MetaHuman profiles. |
| Camera location / attachment | Owen actor: `Camera` component is attached to `DefaultSceneRoot`, relative location `(15,0,162)`, `LockToHMD=True`. Manny retarget actor has no extracted camera component; possession is still on the placed avatar actor. | Current VR room uses a PlayerStart/Spectator path and C++ `ConfigureMirrorPlayerPawn()` / `AlignMirrorCameraToStation()` camera pinning. | Working camera is part of the placed avatar actor; TestingKit3 camera is managed externally. | Copy the placed-root camera pattern: camera attached to VR/avatar root, lock to HMD, eye position derived from profile/mesh. |
| HMD-to-head relationship | HMD camera is placed at the avatar eye/head point on the possessed placed actor. Mirror duplicate/full actor exists separately. | `ResolveEmbodiedStation()` calculates an avatar location from desired camera world position and profile eye offset. It still feeds external camera pinning and runtime actor placement. | TestingKit3 solves the relationship indirectly after spawning; working project encodes it in the placed actor hierarchy. | Make the placed pawn root plus HMD camera define the avatar head/eyes; solve body placement relative to that root, not by moving the world camera afterward. |
| HMD tracking origin and root transforms | Unreal-Movement uses Meta/OculusXR with `XrApi=OVRPluginOpenXR`, `bStartInVR=True`, and `EnableWorldLock=True`. Placed actor root remains the gameplay anchor. | TestingKit3 uses UE OpenXR/OpenXRHandTracking plugins. Stable embodied mode forces `EHMDTrackingOrigin::Local` and may call `ResetOrientationAndPosition()` during startup. | TestingKit3 has an additional recenter/pin layer that can fight placed transforms. | Keep OpenXR, but tie any origin reset to the placed pawn and stop recurrent camera pinning for the copied architecture. |
| Spawn / possession path | Placed primary avatar actor has `Auto Possess Player0`; mirror/full actor is not possessed. | PlayerStart + GameMode spawn a pawn/spectator; hidden startup actor runs `mp.SpawnQuestWebcamHandsNow` path and C++ spawns Manny/source. | Runtime spawn replaces map-authored possession. | Use a placed possessed embodied pawn in `L_MetaHumanPreviewRoom_02`; startup should configure tracking on that pawn, not create hidden coordinates. |
| PlayerStart / placed pawn / GameMode behavior | Game config defaults to `MAP_HighFidelity_AnimBlueprint`; map-placed actor owns possession despite GameMode defaulting to `/Script/Engine.DefaultPawn`. | Config defaults to `ThirdPerson/Lvl_ThirdPerson`; editor startup is VR room; VR room GameMode uses SpectatorPawn and PlayerStart. | Current defaults do not direct VR room into an embodied Manny pawn. | Set VR room override/default path to the placed embodied pawn architecture while preserving non-VR third-person defaults if needed. |
| Mesh attachment offsets | Owen mesh is child of root at `(0,0,0)`; camera is at eye height. Manny retarget mesh is child of root at `(0,0,0)`, yaw `-90`. | `AMediaPipePoseDrivenSkeletalActor` root is its mesh. C++ station solver moves actor to computed `Station.MannyLocation`; profile contains eye/chest/pelvis offsets. | TestingKit3 body is not a child mesh under the HMD pawn root. | Attach/locate the driven Manny mesh or driver actor under the placed pawn root using profile eye offset and Manny forward-axis rules. |
| Skeletal mesh visibility settings | Placed working actors keep mesh `OwnerNoSee=False`, `OnlyOwnerSee=False`, `HiddenInGame=False`; mirror duplicate/full actor remains visible. | TestingKit3 has `ConfigureEmbodiedLocalViewVisibility()` that sets owner/no-see on the full mesh and creates an owner-only first-person poseable body proxy. | TestingKit3 has better local-view machinery, but it is attached to runtime-spawned actors and not the placed pawn architecture. | Keep the local body proxy mechanism, but call it from the placed embodied pawn path. |
| Owner view vs mirror/external view | Working map uses a second full tracking actor for mirror/external view. | TestingKit3 uses scene-capture mirror plus full mesh visible to non-owner views; local view uses owner-no-see plus owner-only proxy. | Different but functionally equivalent if mirror sees the non-owner full mesh. | Do not hide the whole avatar. Preserve full mirror/external visibility and owner-only local head culling/body proxy. |
| Bone hide / self-view logic | No owner-only bone hide was found in the placed map component state; working separation appears to come from pawn hierarchy plus duplicate actor/mirror setup. | `FMediaPipeAvatarLocalViewPolicy::DefaultHumanoid()` hides `head`, `neck_01`, `neck_02`, `FACIAL_C_FacialRoot`, `face_root` in an owner-only poseable body proxy. | TestingKit3 has explicit self-view logic, but it is not yet the main architecture. | Keep profile-driven local hidden bones. Add chest/spine local hiding only if camera/head placement still exposes interior geometry after root architecture is fixed. |
| Head, neck, chest, pelvis, and root bone handling | Manny map uses `SK_Manny_Simple` with `AB_Mannequin`; Owen map uses high-fidelity rig with `ABP_OwenFullTracking`. Body tracking comes from OculusXRMovement retarget nodes. | TestingKit3 profile maps root/pelvis/chest/neck/head in `FMediaPipeAvatarEmbodimentProfile`; solver derives eye/chest/pelvis offsets and body fusion proportions. | TestingKit3 already has profile data for these bones; the missing piece is making the HMD camera the avatar eye root. | Reuse profile bone/offset data to place the body under the HMD camera root. |
| Animation blueprint / retarget setup | Unreal-Movement uses `ABP_OwenFullTracking` and `AB_Mannequin`, backed by OculusXRMovement body tracking/retarget nodes. | TestingKit3 uses `UMediaPipePoseDrivenAnimInstance`, MediaPipe body data, Quest/OpenXR hand data, and profile-driven MetaHuman/Manny selection. | Retarget engines differ. | Do not copy OculusXRMovement wholesale into TestingKit3 now; implement an equivalent architecture around the existing MediaPipe/Quest anim path. |
| Body tracking startup flow | Working map has placed tracking actors; body tracking starts through OculusXRMovement/Meta runtime as the placed actor evaluates. | Hidden `BP_MetaHumanPreviewRoomAutoQuestStartup` triggers `SpawnAutoQuestWebcamHands()`, which spawns MediaPipe webcam source and Manny actor. | TestingKit3 body startup is hidden and procedural. | Convert startup to configure a placed embodied pawn/source pair, keeping capture/profile selection explicit. |
| Quest/OpenXR/OculusXR dependencies | Unreal-Movement depends on `OculusXR` and `OculusXRMovement`; config uses OVRPluginOpenXR and Meta body tracking settings. | TestingKit3 depends on `OpenXR` and `OpenXRHandTracking`; custom `MediaPipeDriver` consumes OpenXR hand joints. | Plugin stack is different. | Use an equivalent TestingKit3 OpenXR implementation; do not require OculusXRMovement unless a later proof shows TestingKit3 cannot reproduce the architecture without it. |
| Hand / arm tracking path | Working project uses MotionController components and OculusXRMovement body/retarget path. | TestingKit3's protected MediaPipe arm solve remains in `UMediaPipePoseDrivenAnimInstance`; the no-MediaPipe placed pawn now reads `MotionControllerLeft/Right` and falls back to OpenXR `IHandTracker` wrist/palm transforms for simple arm IK. | The protected MediaPipe arm solve is not the active path in the current no-MediaPipe replica phase, so static arms were caused by the placed-pawn replica not consuming hand/controller input. | Keep the protected MediaPipe arm solve unchanged. Use Movement-style pawn-owned controller/OpenXR hand input for this no-MediaPipe replica path. |
| Calibration / offset logic | Placed actor transform and camera component provide the primary offset. | Many CVars control station solving, HMD origin reset, room-scale follow, camera forward offset, and wrist calibration. | TestingKit3 has accumulated station/camera compensation logic. | Reduce embodied defaults to profile selection plus placed pawn transform. Avoid offset-only fixes. |
| CVars / default settings | Unreal-Movement config has `bStartInVR=True`, `NearClipPlane=0.1`, OVRPluginOpenXR, Meta body tracking enabled. | TestingKit3 has no equivalent OculusXR settings; embodied behavior is controlled by `mp.AutoQuest*`, `mp.MetaHumanActiveProfile`, and Quest arm CVars. | TestingKit3 behavior is runtime-CVar heavy. | Make the default embodied path profile-driven; keep minimal CVars, ideally active profile and explicit tracking startup. |
| Mirror visibility | Working project uses a second full actor (`BP_OwenFullTrackingMirror` / `MannequinPawn2`) visible to mirror/external view. | TestingKit3 uses `BP_VRSelfMirror` scene capture and keeps full mesh visible to non-owner/reflection views. | Different implementation, same goal if local owner-no-see proxy is correct. | Keep TestingKit3 mirror capture and full mesh visibility; no whole-avatar hide. |

## Implementation Direction

The TestingKit3 fix must copy the working architectural rule, not just tune offsets:

1. The embodied avatar needs a placed pawn/root in the VR room.
2. The HMD camera must be attached to that pawn/root at profile-derived eye height and locked to the HMD.
3. The driven Manny body must be placed relative to that root/profile, not from hidden hardcoded station coordinates.
4. Full-body external/mirror visibility must remain intact.
5. First-person self-view should use local owner visibility plus profile-driven hidden bones/body proxy.
6. The protected MediaPipe Quest/OpenXR arm solve remains unchanged; the no-MediaPipe replica pawn may consume pawn-owned MotionController/OpenXR hand input before MediaPipe is enabled.

## Implementation Result

Changed TestingKit3 files/assets:

- `Source/MediaPipeDriver/MediaPipeEmbodiedAvatarPawn.h`
- `Source/MediaPipeDriver/MediaPipeDriver.cpp`
- `Tools/ApplyEmbodiedPawnArchitecture.py`
- `Tools/VerifyEmbodiedPawnArchitecture.py`
- `Content/MetaHumanRooms/Blueprints/BP_MP_EmbodiedMannyPawn.uasset`
- `Content/MetaHumanRooms/L_MetaHumanPreviewRoom_02.umap`

Generated evidence:

- `Saved/CodexAgent/EmbodimentInspection/testingkit3_embodied_pawn_verify_result.json`
- `Saved/Logs/TestingKit3_2-backup-2026.05.24-04.33.34.log`
- `Saved/Logs/TestingKit3_2.log`

Architecture copied:

- Native `AMediaPipeEmbodiedAvatarPawn` now matches the Movement pawn shape: placed pawn, root scene component, HMD camera component, full body mesh component, local owner-only body mesh component, and left/right MotionController components.
- Component hierarchy is `AvatarRoot -> VROrigin -> VRCamera`; `VRCamera` is HMD locked and its relative transform is zero so HMD tracking applies under the placed pawn root.
- `VROrigin` is the profile eye anchor at `(6.66, 0.0, 162.58)` for the internal Manny profile.
- `AvatarMesh` and `LocalAvatarMesh` are attached directly to `AvatarRoot` at zero translation and yaw `-90`, copying the Movement Manny mesh attachment rule.
- `AvatarMesh` uses `/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple.SKM_Manny_Simple`, is visible to mirror/external views, and is `OwnerNoSee=True`.
- `LocalAvatarMesh` uses the same Manny mesh, is `OnlyOwnerSee=True`, and hides/scales only owner-view head/neck/facial bones from `FMediaPipeAvatarLocalViewPolicy`. Chest/spine bones are explicitly kept visible so the local owner mesh does not collapse the upper torso or arms.
- The pawn drives the full and local Manny head/neck bones directly from HMD world rotation by mapping HMD forward/up into Manny component space. This is the no-Oculus/no-MediaPipe replacement for Movement's OculusXRMovement body-retarget node for the current head-tracking phase.
- `MotionControllerLeft` and `MotionControllerRight` are attached to `AvatarRoot`, matching Movement's pawn-level controller components. The no-MediaPipe pawn now consumes those component poses, with a generic OpenXR `IHandTracker` wrist/palm fallback, to drive simple upper/lower arm IK on both the full mirror mesh and the owner-only local mesh.
- Added `/Game/MetaHumanRooms/Blueprints/BP_MP_EmbodiedMannyPawn`.
- Placed `MP_PlacedEmbodiedMannyPawn` in `/Game/MetaHumanRooms/L_MetaHumanPreviewRoom_02` at `(0,-170,0)`, yaw `90`, with `Auto Possess Player0`.
- Tagged the placed pawn with `TestingKit3_PlacedEmbodiedAvatarPawn` and `TestingKit3_AutoQuestEmbodiedStart`.
- The placed pawn's `bUseMediaPipeTracking` default is `False`, so this phase does not spawn the MediaPipe webcam source or `AMediaPipePoseDrivenSkeletalActor`.
- Normal PIE sanity check confirmed Player0 possessed `BP_MP_EmbodiedMannyPawn_C_0`, the view target was the same pawn, and there were zero `QuestWebcamSourceActor` and zero `PoseDrivenSkeletalActor` instances.
- Kept the existing Quest hand/finger/constrained arm solver code unchanged.

## Runtime VR Preview Follow-Up

Latest status:

- VR Preview evidence in `Saved/QuestScreenshots/vrpreview_quest_mirror_20260524_123932` showed head rotation live and the mirror full Manny present, but the owner view was a cut-off local mesh and arms were not being consumed by the replica path.
- The cut-off owner mesh matched the extra local chest/spine hiding that had been added on top of the profile policy. That was removed; owner view now hides head/neck/facial bones only.
- The static-arm issue matched the no-MediaPipe replica path not reading hand/controller input. The pawn now drives arms from `MotionControllerLeft/Right` or generic OpenXR `IHandTracker` wrist/palm transforms.
- The next VR Preview debug pass must start only after the headset is awake/worn/tracked.

Architecture intentionally not copied:

- `OculusXR` and `OculusXRMovement` were not copied or enabled. The replacement is a native TestingKit3 Movement-replica path using the placed pawn, HMD-locked camera, pawn-owned Manny mesh, and direct HMD-to-head bone drive.
- MediaPipe body fusion and the MediaPipe webcam source were not enabled. This phase deliberately keeps `bUseMediaPipeTracking=False`.
- Unreal-Movement's duplicate mirror actor was not copied literally. TestingKit3 already has `BP_VRSelfMirror`; the equivalent replacement is preserving the full non-owner mesh for the scene-capture mirror while using the owner-only local mesh for self-view.
- Unreal-Movement's OculusXRMovement full body retarget node was not copied. The current equivalent is intentionally limited to HMD head/neck plus pawn-owned OpenXR hand/controller arm IK until MediaPipe/MetaHuman profiles are reintroduced.

Validation:

- `D:\Epic\UE_5.7\Engine\Build\BatchFiles\Build.bat TestingKit3Editor Win64 Development -Project=D:\Epic\Unreal_Projects\TestingKit3\TestingKit3.uproject -WaitMutex` succeeded after the C++ changes.
- The Blueprint subclass compiled through `BlueprintEditorLibrary.compile_blueprint`.
- The Blueprint EventGraph was inspected after compile; it still contains only the inherited empty BeginPlay, ActorBeginOverlap, and Tick event nodes.
- The verifier reloaded the saved VR room map and verified: one placed embodied pawn, class `/Game/MetaHumanRooms/Blueprints/BP_MP_EmbodiedMannyPawn.BP_MP_EmbodiedMannyPawn_C`, root `AvatarRoot`, `VROrigin`, HMD-locked `VRCamera`, full Manny `AvatarMesh`, owner-only `LocalAvatarMesh`, left/right MotionController components, `AutoReceiveInput.PLAYER0`, mirror actor present, startup actor present, `bUseMediaPipeTracking=False`, `mp.AutoQuestAvatar=0`, and `mp.AutoQuestEmbodiedView=1`.
- Normal PIE after the arm/local-mesh patch confirmed Player0 possessed `BP_MP_EmbodiedMannyPawn_C_0`, the view target was the same pawn, and there were zero `QuestWebcamSourceActor` and zero `PoseDrivenSkeletalActor` instances.
- Focused automation passed:
  - `TestingKit3.MediaPipe.AvatarEmbodiment` found and passed 4 tests.
  - `TestingKit3.MediaPipe.AvatarRigProfile.InternalManny` passed 1 test.
  - `TestingKit3.MediaPipe.Runtime.CVars` passed 1 test.
- Final saved placement evidence reports:
  - class `/Game/MetaHumanRooms/Blueprints/BP_MP_EmbodiedMannyPawn.BP_MP_EmbodiedMannyPawn_C`
  - root `AvatarRoot`
  - full Manny mesh `AvatarMesh`
  - owner-only Manny mesh `LocalAvatarMesh`
  - camera `VRCamera`
  - camera parent `VROrigin`
  - `camera_lock_to_hmd=true`
  - `VROrigin` relative location `(6.66, 0, 162.58)` from the internal Manny profile rotated into the placed pawn root
  - camera relative location `(0, 0, 0)`
  - actor location `(0, -170, 0)`
  - actor yaw `90`
  - `AutoReceiveInput.PLAYER0`

Still VR-gated:

- Runtime wearer-view assertions after the chest/spine and arm-input patch still need a headset VR Preview pass: not inside Manny head/chest, mirror shows full Manny with HMD-following head, arms move from OpenXR hands/controllers, and the placed pawn remains the spawn/facing source.
