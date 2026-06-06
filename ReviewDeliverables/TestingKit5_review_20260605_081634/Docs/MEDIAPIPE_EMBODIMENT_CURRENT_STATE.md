# MediaPipe Embodiment Current State

Status: TestingKit5, UE 5.8, consolidated 2026-06-05. This replaces the repeated MPQ refactor, MediaPipe pipeline, avatar profile, tracking issue, and embodiment comparison narratives.

## Runtime shape

| Layer | Current owner | Source anchors | Notes |
| --- | --- | --- | --- |
| Project/runtime startup | `MediaPipeDriverRuntime` | `Source/MediaPipeDriver/Runtime/MediaPipeDriverRuntime.cpp`, `MediaPipeRuntimeCVars.cpp` | Owns AutoQuest CVars, PIE-ready startup, mirror/perf profile, live actors, and some legacy `TestingKit3_*` tags. |
| Avatar pawn/lifecycle | `AMediaPipeEmbodiedAvatarPawn` | `Source/MediaPipeDriver/Embodiment/MediaPipeEmbodiedAvatarPawn.*` | Owns placed avatar root, body root, VR origin, camera, poseable local/mirror meshes, motion controllers, profile launch, source/avatar spawning, and movement-replica write paths. |
| Fusion component | `UEmbodiedFusionComponent` | `Source/MediaPipeDriver/Embodiment/EmbodiedFusionComponent.*` | Owns source freshness, HMD conditioning, Quest polling adapter calls, MediaPipe source reads, calibration, authority gate, `FMediaPipeTrackingSourceFrameBuilder`, `FMediaPipeBodyFusionSolver`, latest fused frame, and best-available pose. |
| Source frame | `FMediaPipeTrackingSourceFrame` | `Source/MediaPipeDriver/Tracking/MediaPipeTrackingSourceTypes.*`, `MediaPipeTrackingSourceFrameBuilder.*` | Holds HMD, Quest hands, full-arm chain, and MediaPipe pose samples with per-source freshness/status. This is still concrete-source-shaped but no longer buried in BodyFusion. |
| Semantic fusion | `FMediaPipeBodyFusionSolver` | `Source/MediaPipeDriver/BodyFusion/MediaPipeBodyFusion.*`, `MediaPipeFusedAvatarPose.*`, `MediaPipeBodyFusionAuthorityPolicy.*`, `MediaPipeEmbodimentCalibrationSolver.*` | Produces `FMediaPipeFusedAvatarPose`; pelvis follows HMD only through explicit follow policy, not by default. |
| Avatar/profile data | Embodiment + MetaHuman profiles | `MediaPipeAvatarEmbodimentProfile.*`, `MediaPipeAvatarRigProfile.*`, `MediaPipeMetaHumanProfile.*`, `MediaPipeAvatarProfileResolver.*` | Built-in MetaHuman profiles resolve by active profile/name/mesh; InternalManny profile resolves from the Manny-like mesh. |
| Pose writers/adapters | Anim instance plus helpers | `PoseDriven/MediaPipePoseDrivenAnimInstance.*`, `PoseDriven/Inline/*.inl`, `Avatar/MediaPipeSkeletonPoseAdapter.*`, `BodyFusion/MediaPipeBodyFusionPoseWriteContext.*` | Still the main coupling zone. The anim instance and pawn movement-replica path both write bones; they do not yet only consume a final fused pose. |
| Quest/OpenXR | Quest source/debug/solver files | `Source/MediaPipeDriver/Quest/*`, `PoseDriven/MediaPipeFullArmChainProvider.*` | HMD, hand, full-arm, wrist, finger, calibration, trace, capture/replay, and debug formatting are separate from BodyFusion but still cross into the anim path. |
| Diagnostics | Runtime/debug formatter files | `Diagnostics/*`, `BodyFusion/*Debug*`, `Quest/*Debug*`, `MediaPipeDriverEditor/*` | Developer logs include `mp.BodyFusion.Debug`, `mp.QuestWristSnapshot`, `mp.QuestWristRollCompact`, `mp.MetaHumanArmSanity`, and MediaPipe visual-cycle rows. |

## Protected behavior

- Preserve live Manny head-tracking behavior unless the task explicitly asks to retune it. Recent work fixed head pitch direction, video overlay diagnostics, readable Manny materials, and short dropout/occlusion rejection for face-derived head targets.
- HMD owns eye/head in embodied mode. MediaPipe body authority is gated by freshness and calibration; `mp.BodyFusion.MediaPipeAuthority=0` is trace-only.
- Default pelvis stability: pelvis must not chase HMD planar movement unless `EMediaPipePelvisAuthorityMode::FollowUpperBodyExplicit` or equivalent explicit policy is chosen.
- Quest/OpenXR hands may drive wrist rotation and fingers while MediaPipe owns or hints body/arm positions depending on profile and source freshness.
- Local self-view uses profile-driven hidden head/neck/face bones or poseable first-person body proxy; mirror/external view must keep the full avatar visible.
- VR claims require headset-visible proof. Build success, logs, normal PIE, or automation alone are not enough for embodied VR acceptance.

## Current defaults worth knowing

- `mp.BodyFusion.Enable=0`, `mp.BodyFusion.Debug=0`, `mp.BodyFusion.MediaPipeAuthority=0`.
- `mp.AutoQuestWebcamHands=1`, `mp.AutoQuestWebcamHandsHz=30`, `mp.AutoQuestWebcamHandsInputMaxDimension=512`.
- `mp.AutoQuestAvatar=0` means internal Manny baseline; `1` means active `mp.MetaHumanActiveProfile` with Manny fallback.
- `mp.AutoQuestArmReachAssistProfile=4` is the current AutoQuest arm profile: Quest wrist endpoint authority with MediaPipe shoulder/elbow hints.
- `mp.AutoQuestEmbodiedView=1`, `mp.AutoQuestEmbodiedAnchorMode=1`, `mp.AutoQuestEmbodiedMirror=0`, `mp.AutoQuestEmbodiedStableBody=1`.
- `mp.QuestHandTracking=1`, `mp.QuestHandDriveFingerBones=1`, `mp.QuestHandRotationBlend=1.0`.

## Refactor debt

1. Complete the dependency direction: sources produce observations only; fusion produces semantic fused pose only; writers consume fused pose plus profile only.
2. Move remaining coordination and raw Quest/runtime polling out of `MediaPipePoseDrivenAnimInstance.*`; its long inline files remain the main maintenance risk.
3. Move movement-replica pose writing out of `AMediaPipeEmbodiedAvatarPawn` into a writer/adapter service or delete it once the fused-pose writer supersedes it.
4. Keep `FMediaPipeTrackingSourceFrame` as the source-frame boundary, but split concrete HMD/Quest/MediaPipe payloads into source-specific observation structs if the frame grows again.
5. Rename legacy automation filters and tags that still say `TestingKit3.*` only after a dedicated source/test pass; docs should not hide that naming debt.
6. Keep data-asset profile/skeleton adapter expansion deferred until content actually consumes it.

## Verification surface

- Build: `D:\Epic\UE_5.8\Engine\Build\BatchFiles\Build.bat TestingKit5Editor Win64 Development -Project="D:\Epic\Unreal_Projects\TestingKit5\TestingKit5.uproject" -WaitMutex`
- Automation: run the broad `MediaPipe` filter; note that several test names still use `TestingKit3.MediaPipe.*`.
- Runtime proof: use `/Game/MCPBench/Maps/L_MCP_MediaPipeMannyRoom`, bridge screenshots, PIE/VR logs, and headset-visible confirmation when behavior is visual or embodied.
