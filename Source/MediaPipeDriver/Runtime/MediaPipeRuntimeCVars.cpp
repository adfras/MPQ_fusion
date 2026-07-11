#include "MediaPipeRuntimeCVars.h"

namespace MediaPipeRuntimeCVars
{
	TAutoConsoleVariable<int32> CVarMediaPipeDriveClavicles(
		TEXT("mp.MediaPipeDriveClavicles"),
		0,
		TEXT("When non-zero, drive Manny clavicles from the MediaPipe arm pose. Current Auto Quest embodied startup keeps this off with spine/lower body off so arm extension can be evaluated without shoulder-root retargeting."));

	TAutoConsoleVariable<int32> CVarMediaPipeDriveSpine(
		TEXT("mp.MediaPipeDriveSpine"),
		1,
		TEXT("When non-zero, drive Manny pelvis/spine rotation from the MediaPipe torso basis. Disable in Quest hand-isolation profiles to prevent torso basis flips from rotating the whole skeletal body."));

	TAutoConsoleVariable<int32> CVarMediaPipeDrivePelvisTranslation(
		TEXT("mp.MediaPipeDrivePelvisTranslation"),
		0,
		TEXT("When non-zero, drive Manny pelvis translation from the MediaPipe hip/support pose. Default is off because the current MediaPipe support solve can pull legs across the body."));

	TAutoConsoleVariable<int32> CVarMediaPipeDriveLegs(
		TEXT("mp.MediaPipeDriveLegs"),
		0,
		TEXT("When non-zero, drive Manny legs and leg-derived lower-body root motion from MediaPipe. Default off for Quest-hand integration because out-of-frame legs are inferred and can collapse Manny."));

	TAutoConsoleVariable<int32> CVarMediaPipeUseArmIK(
		TEXT("mp.MediaPipeUseArmIK"),
		0,
		TEXT("When non-zero, solve MediaPipe arms with bounded 2-bone IK; otherwise use direct segment alignment. Default off because the current IK branch misses cached riverbank arm segments by about 40-45 degrees."));

	TAutoConsoleVariable<int32> CVarMediaPipeUseLegIK(
		TEXT("mp.MediaPipeUseLegIK"),
		0,
		TEXT("When non-zero, use MediaPipe foot-plant leg IK. Default is off because cached retarget quality is better with direct segment legs."));

	TAutoConsoleVariable<int32> CVarMediaPipeUseLegIKFootPlant(
		TEXT("mp.MediaPipeUseLegIKFootPlant"),
		1,
		TEXT("When non-zero, MediaPipe leg IK may lock planted feet to the avatar reference floor. Disable for replay-output evaluation that must follow recorded foot lifts while preserving target leg lengths."));

	TAutoConsoleVariable<int32> CVarMediaPipeUseFkRootGrounding(
		TEXT("mp.MediaPipeUseFkRootGrounding"),
		0,
		TEXT("When non-zero, apply FK root grounding after MediaPipe leg solving. Default is off for cached retarget comparison."));

	TAutoConsoleVariable<int32> CVarMediaPipeDriveHandRotation(
		TEXT("mp.MediaPipeDriveHandRotation"),
		0,
		TEXT("When non-zero, drive Manny wrist/hand rotation from MediaPipe hand or pose hand landmarks. Default is off because current hand basis can twist badly."));

	TAutoConsoleVariable<int32> CVarMediaPipeHandRotationOnQuestLoss(
		TEXT("mp.MediaPipeHandRotationOnQuestLoss"),
		0,
		TEXT("When non-zero, the MediaPipe hand-landmark basis drives wrist/hand rotation for a side WHILE that Quest hand is untracked (and only then) - overhead the Quest hand freezes at its last rotation and snaps on reacquire (observed 2026-07-03). Needs mp.AutoQuestWebcamHandLandmarker for the 21-landmark basis; falls back to pose index/pinky landmarks otherwise. Default off to keep replay evaluation byte-stable; the live lower-body trial layer enables it."));

	TAutoConsoleVariable<int32> CVarMediaPipeFingersOnQuestLoss(
		TEXT("mp.MediaPipeFingersOnQuestLoss"),
		0,
		TEXT("When non-zero, the camera's 21-landmark hand drives FINGERS for a side while that Quest hand is untracked: the image-space proximity-matched MediaPipe hand is mapped onto a synthetic OpenXR-layout snapshot and fed through the existing segment-direction finger solver. Needs mp.AutoQuestWebcamHandLandmarker. Without a camera hand the untracked side keeps the validated hold-pose. Default off to keep replay evaluation byte-stable; the live lower-body trial layer enables it."));

	TAutoConsoleVariable<float> CVarMediaPipeHandOwnershipHandbackSeconds(
		TEXT("mp.MediaPipeHandOwnershipHandbackSeconds"),
		0.5f,
		TEXT("Seconds the Quest hand must stay continuously tracked (with no arm rescue) before a camera-latched hand pose is handed back to the Quest hand-rotation path. Only used while mp.MediaPipeHandRotationOnQuestLoss/mp.MediaPipeFingersOnQuestLoss are active; recomputing ownership per frame from the raw tracked flag alternated camera/Quest owners at flicker rate (2026-07-03 log evidence)."));

	TAutoConsoleVariable<int32> CVarMediaPipeFootContactKeyedState(
		TEXT("mp.MediaPipeFootContactKeyedState"),
		0,
		TEXT("When non-zero, foot contact/floor/plant state (previous foot samples, observed source floor, plant lock) lives in the keyed per-component runtime store instead of anim-node members. Live VR runs CacheBones every frame, wiping node members, so the observed floor re-seeded to the current foot each frame - foot lift always read 0 and the HMD flexion correction straightened raised legs (2026-07-03 half-height knee raises). Default off to keep replay evaluation byte-stable; the live lower-body trial layer enables it."));

	TAutoConsoleVariable<int32> CVarMediaPipeCameraHandTrace(
		TEXT("mp.MediaPipeCameraHandTrace"),
		0,
		TEXT("When non-zero, logs one mp.MediaPipeCameraHandTrace row per side per evaluated frame while the camera-hand features are enabled: ownership latch state, hand basis quality, branch chooser state, smoothing steps, the applied rotation delta, and the CacheBones call counter. Diagnostic only; default off. Set AFTER PIE starts (the live profile re-applies defaults at PIE start)."));

	TAutoConsoleVariable<int32> CVarQuestHandTracking(
		TEXT("mp.QuestHandTracking"),
		1,
		TEXT("When non-zero, prefer Quest/OpenXR hand tracking over MediaPipe hand landmarks for wrist and finger pose."));

	TAutoConsoleVariable<int32> CVarQuestHandReplay(
		TEXT("mp.QuestHandReplay"),
		0,
		TEXT("When non-zero, override live Quest/OpenXR hand tracking with the loaded hand replay snapshot. Use mp.QuestHandReplayFile <name-or-path> first."));

	TAutoConsoleVariable<int32> CVarQuestHandDriveFingerBones(
		TEXT("mp.QuestHandDriveFingerBones"),
		1,
		TEXT("When non-zero, drive target finger bones from Quest/OpenXR hand keypoints when available."));

	TAutoConsoleVariable<float> CVarQuestHandRotationBlend(
		TEXT("mp.QuestHandRotationBlend"),
		1.0f,
		TEXT("Blend weight for Quest/OpenXR wrist/hand orientation. 0 keeps the MediaPipe/rig wrist frame; higher values make Manny hands follow Quest wrist rotation more strongly."));

	TAutoConsoleVariable<int32> CVarBodyFusionEnable(
		TEXT("mp.BodyFusion.Enable"),
		0,
		TEXT("When non-zero, enables the hybrid HMD/Quest/MediaPipe body-fusion path. Default off until Manny and MetaHuman VR Preview validation are both accepted."));

	TAutoConsoleVariable<int32> CVarBodyFusionDebug(
		TEXT("mp.BodyFusion.Debug"),
		0,
		TEXT("When non-zero, logs developer-only BodyFusion source freshness, ownership, and head/chest/pelvis error diagnostics."));

	TAutoConsoleVariable<int32> CVarBodyFusionWritePose(
		TEXT("mp.BodyFusion.WritePose"),
		0,
		TEXT("When non-zero, BodyFusion may drive avatar pose writes after mp.BodyFusion.Enable is active. Default off so Stage 0 shadow diagnostics build and log fused poses without changing visible authority."));

	TAutoConsoleVariable<int32> CVarBodyFusionMediaPipeAuthority(
		TEXT("mp.BodyFusion.MediaPipeAuthority"),
		0,
		TEXT("MediaPipe body authority mode. 0=trace-only/no pose authority, 1=allow only after stable calibration, 2=legacy allow when calibrated and fresh."));

	TAutoConsoleVariable<int32> CVarBodyFusionFullBodyMediaPipeAuthority(
		TEXT("mp.BodyFusion.FullBodyMediaPipeAuthority"),
		0,
		TEXT("When non-zero, calibrated/fresh MediaPipe body authority may own pelvis, hips, knees, ankles, and feet. This preserves avatar scale and segment lengths; it only exposes full-body targets to the fused pose writer."));

	TAutoConsoleVariable<int32> CVarBodyFusionStage1TorsoPelvisHint(
		TEXT("mp.BodyFusion.Stage1TorsoPelvisHint"),
		0,
		TEXT("Compatibility switch for historical Stage 1 torso/pelvis hint captures. The live MetaHuman path ignores this direct translation layer; BodyFusion pose writing owns torso movement."));

	TAutoConsoleVariable<float> CVarBodyFusionStage1TorsoPelvisHintBlend(
		TEXT("mp.BodyFusion.Stage1TorsoPelvisHintBlend"),
		0.25f,
		TEXT("Blend fraction for Stage 1 vertical MediaPipe pelvis/torso offsets. Values are clamped to 0..1 at use time."));

	TAutoConsoleVariable<float> CVarBodyFusionStage1TorsoPelvisMaxVerticalCm(
		TEXT("mp.BodyFusion.Stage1TorsoPelvisMaxVerticalCm"),
		8.0f,
		TEXT("Maximum absolute vertical offset in centimeters that Stage 1 MediaPipe pelvis/torso hinting may apply per sampled target."));

	TAutoConsoleVariable<float> CVarBodyFusionStage1TorsoPelvisHintHalfLifeSeconds(
		TEXT("mp.BodyFusion.Stage1TorsoPelvisHintHalfLife"),
		0.04f,
		TEXT("Smoothing half-life in seconds for guarded Stage 1 vertical MediaPipe pelvis/torso hints. 0 disables smoothing."));

	TAutoConsoleVariable<int32> CVarBodyFusionStage2ShoulderClavicleHint(
		TEXT("mp.BodyFusion.Stage2ShoulderClavicleHint"),
		0,
		TEXT("When non-zero, records neutral-gated MediaPipe shoulder/shrug evidence for BodyFusion. This path must not directly translate MetaHuman clavicle/helper/arm bones outside the fused-pose writer."));

	TAutoConsoleVariable<float> CVarBodyFusionStage2ShoulderClavicleHintBlend(
		TEXT("mp.BodyFusion.Stage2ShoulderClavicleHintBlend"),
		1.0f,
		TEXT("Compatibility field retained for Stage 2A evidence captures. Stage 2A no longer applies visible MetaHuman clavicle/helper/arm translations."));

	TAutoConsoleVariable<float> CVarBodyFusionStage2ShoulderClavicleResponseScale(
		TEXT("mp.BodyFusion.Stage2ShoulderClavicleResponseScale"),
		1.0f,
		TEXT("Scales recorded Stage 2A signed shoulder-lift evidence before diagnostic clamping. This value is not a visible MetaHuman bone-drive strength."));

	TAutoConsoleVariable<float> CVarBodyFusionStage2ShoulderClavicleMaxLiftCm(
		TEXT("mp.BodyFusion.Stage2ShoulderClavicleMaxLiftCm"),
		5.0f,
		TEXT("Compatibility cap for recorded Stage 2 shoulder/shrug target diagnostics. The MPQ shadow path does not directly apply this as a MetaHuman bone translation."));

	TAutoConsoleVariable<float> CVarBodyFusionStage2ShoulderClavicleHalfLifeSeconds(
		TEXT("mp.BodyFusion.Stage2ShoulderClavicleHalfLife"),
		0.04f,
		TEXT("Smoothing half-life in seconds for recorded Stage 2A shoulder evidence. 0 disables diagnostic smoothing."));

	TAutoConsoleVariable<float> CVarBodyFusionStage2ShoulderContradictionCm(
		TEXT("mp.BodyFusion.Stage2ShoulderContradictionCm"),
		20.0f,
		TEXT("If a fresh Quest/full-arm shoulder chain differs from the calibrated MediaPipe shoulder by more than this many vertical centimeters, suppress that Stage 2A side. 0 disables this contradiction gate."));

	TAutoConsoleVariable<float> CVarBodyFusionStage2ShoulderArmRaiseFadeStartCm(
		TEXT("mp.BodyFusion.Stage2ShoulderArmRaiseFadeStartCm"),
		35.0f,
		TEXT("Quest wrist or elbow height above the calibrated MediaPipe pelvis, in centimeters, where Stage 2A stops accepting neutral samples while preserving Quest arm ownership."));

	TAutoConsoleVariable<float> CVarBodyFusionStage2ShoulderArmRaiseFadeFullCm(
		TEXT("mp.BodyFusion.Stage2ShoulderArmRaiseFadeFullCm"),
		50.0f,
		TEXT("Compatibility endpoint for the Stage 2A arm-raise neutral-update fade in diagnostics. It does not gate a visible MetaHuman clavicle writer."));

	TAutoConsoleVariable<float> CVarBodyFusionStage2ShoulderShrugStartCm(
		TEXT("mp.BodyFusion.Stage2ShoulderShrugStartCm"),
		2.0f,
		TEXT("Signed Stage 2A shoulder evidence, in centimeters above the observed neutral reference, where diagnostic shrug evidence begins."));

	TAutoConsoleVariable<float> CVarBodyFusionStage2ShoulderShrugFullCm(
		TEXT("mp.BodyFusion.Stage2ShoulderShrugFullCm"),
		8.0f,
		TEXT("Signed Stage 2A shoulder evidence, in centimeters above the observed neutral reference, where diagnostic shrug evidence reaches full response before clamping."));

	TAutoConsoleVariable<int32> CVarBodyFusionCalibrationStableFrames(
		TEXT("mp.BodyFusion.CalibrationStableFrames"),
		15,
		TEXT("Consecutive stable BodyFusion MediaPipe calibration frames required before accepting calibration. 0 disables the frame gate."));

	TAutoConsoleVariable<float> CVarBodyFusionCalibrationHoldSeconds(
		TEXT("mp.BodyFusion.CalibrationHoldSeconds"),
		0.5f,
		TEXT("Stable BodyFusion MediaPipe calibration hold time required before accepting calibration. 0 disables the time gate."));

	TAutoConsoleVariable<int32> CVarQuestWristRelativeCalibration(
		TEXT("mp.QuestWristRelativeCalibration"),
		1,
		TEXT("When non-zero, auto-calibrate each Quest wrist on first tracked frame and apply relative wrist motion onto the MediaPipe/Manny wrist frame."));

	TAutoConsoleVariable<int32> CVarQuestWristUseBasisDelta(
		TEXT("mp.QuestWristUseBasisDelta"),
		1,
		TEXT("When non-zero, semantic Quest wrist rotation compares the Quest palm basis in forearm-local space before converting back to Manny's hand bone target."));

	TAutoConsoleVariable<int32> CVarQuestWristRequireNeutralCalibration(
		TEXT("mp.QuestWristRequireNeutralCalibration"),
		0,
		TEXT("When non-zero, Quest wrist rotation calibration waits for the neutral pose: upright, facing Manny/camera, head forward, elbows near ribs at about 90 degrees, forearms forward at chest height, hands shoulder-width, palms facing each other, thumbs up, wrists straight."));

	TAutoConsoleVariable<float> CVarQuestWristCalibrationMaxBasisErrorDegrees(
		TEXT("mp.QuestWristCalibrationMaxBasisErrorDegrees"),
		140.0f,
		TEXT("Maximum Quest-vs-Manny hand basis angular error allowed by the Quest wrist calibration gate."));

	TAutoConsoleVariable<int32> CVarQuestWristCalibrationGate(
		TEXT("mp.QuestWristCalibrationGate"),
		0,
		TEXT("When non-zero, Quest wrist rotation calibration must pass a stable-pose hold before Manny wrist rotation is applied."));

	TAutoConsoleVariable<float> CVarQuestWristCalibrationHoldSeconds(
		TEXT("mp.QuestWristCalibrationHoldSeconds"),
		3.0f,
		TEXT("Seconds the neutral Quest wrist calibration pose must remain valid before calibration is accepted."));

	TAutoConsoleVariable<int32> CVarQuestWristCalibrationStableFrames(
		TEXT("mp.QuestWristCalibrationStableFrames"),
		30,
		TEXT("Minimum consecutive valid frames required before Quest wrist calibration is accepted."));

	TAutoConsoleVariable<float> CVarQuestWristCalibrationMaxHandVelocityCmSec(
		TEXT("mp.QuestWristCalibrationMaxHandVelocityCmSec"),
		20.0f,
		TEXT("Maximum Quest wrist linear velocity allowed while measuring wrist calibration."));

	TAutoConsoleVariable<float> CVarQuestWristCalibrationMaxHandAngularVelocityDegSec(
		TEXT("mp.QuestWristCalibrationMaxHandAngularVelocityDegSec"),
		70.0f,
		TEXT("Maximum Quest wrist angular velocity allowed while measuring wrist calibration."));

	TAutoConsoleVariable<float> CVarQuestWristCalibrationMaxYawDeltaDegrees(
		TEXT("mp.QuestWristCalibrationMaxYawDeltaDegrees"),
		3.0f,
		TEXT("Maximum per-frame MediaPipe body or Manny yaw change allowed while measuring wrist calibration."));

	TAutoConsoleVariable<float> CVarQuestWristCalibrationMaxNeutralTwistDegrees(
		TEXT("mp.QuestWristCalibrationMaxNeutralTwistDegrees"),
		35.0f,
		TEXT("Maximum neutral Quest-vs-Manny wrist twist around the forearm axis allowed while measuring wrist calibration."));

	TAutoConsoleVariable<int32> CVarQuestWristCalibrationHud(
		TEXT("mp.QuestWristCalibrationHud"),
		0,
		TEXT("When non-zero, show a simple Quest wrist calibration pose guide and live state in the viewport."));

	TAutoConsoleVariable<int32> CVarQuestWristCalibrationRequirePoseMatch(
		TEXT("mp.QuestWristCalibrationRequirePoseMatch"),
		0,
		TEXT("When non-zero, the stable calibration hold only advances while the tracked body roughly matches the displayed arms-forward guide pose."));

	TAutoConsoleVariable<int32> CVarQuestWristCalibrationSoftGate(
		TEXT("mp.QuestWristCalibrationSoftGate"),
		1,
		TEXT("When non-zero, transient Quest calibration rejects pause or slowly decay progress instead of resetting the whole hold."));

	TAutoConsoleVariable<float> CVarQuestWristCalibrationSoftRejectDecayRate(
		TEXT("mp.QuestWristCalibrationSoftRejectDecayRate"),
		0.35f,
		TEXT("Seconds of accumulated Quest calibration hold progress lost per second while a transient reject persists."));

	TAutoConsoleVariable<float> CVarQuestWristCalibrationHandLossPauseSeconds(
		TEXT("mp.QuestWristCalibrationHandLossPauseSeconds"),
		1.25f,
		TEXT("Seconds that a brief Quest hand-tracking dropout pauses calibration progress before soft decay begins."));

	TAutoConsoleVariable<float> CVarQuestWristCalibrationMinFreshStableSeconds(
		TEXT("mp.QuestWristCalibrationMinFreshStableSeconds"),
		0.35f,
		TEXT("Fresh continuous valid hold time required immediately before Quest wrist calibration can be accepted."));

	TAutoConsoleVariable<int32> CVarQuestWristCalibrationMinFreshStableFrames(
		TEXT("mp.QuestWristCalibrationMinFreshStableFrames"),
		5,
		TEXT("Fresh continuous valid frames required immediately before Quest wrist calibration can be accepted."));

	TAutoConsoleVariable<int32> CVarQuestArmLengthCalibrationStartup(
		TEXT("mp.QuestArmLengthCalibrationStartup"),
		0,
		TEXT("When non-zero, HMD-relative Quest arm tracking starts with a two-pose arm length calibration: full forward reach, then arms straight down by the sides."));

	TAutoConsoleVariable<int32> CVarQuestArmLengthCalibrationHud(
		TEXT("mp.QuestArmLengthCalibrationHud"),
		0,
		TEXT("When non-zero, show the Quest arm length calibration guide in the viewport/headset mirror."));

	TAutoConsoleVariable<float> CVarQuestArmLengthCalibrationHoldSeconds(
		TEXT("mp.QuestArmLengthCalibrationHoldSeconds"),
		2.5f,
		TEXT("Stable seconds required for each Quest arm length calibration pose."));

	TAutoConsoleVariable<int32> CVarQuestArmLengthCalibrationStableFrames(
		TEXT("mp.QuestArmLengthCalibrationStableFrames"),
		20,
		TEXT("Minimum consecutive stable frames required for each Quest arm length calibration pose."));

	TAutoConsoleVariable<float> CVarQuestArmLengthCalibrationMaxHandVelocityCmSec(
		TEXT("mp.QuestArmLengthCalibrationMaxHandVelocityCmSec"),
		30.0f,
		TEXT("Maximum mapped Quest wrist speed allowed while measuring arm length calibration."));

	TAutoConsoleVariable<float> CVarQuestArmLengthCalibrationForwardMinReachFraction(
		TEXT("mp.QuestArmLengthCalibrationForwardMinReachFraction"),
		0.88f,
		TEXT("Minimum mapped shoulder-to-wrist reach fraction required for the full-forward arm calibration pose."));

	TAutoConsoleVariable<float> CVarQuestArmLengthCalibrationDownMinBelowShoulderFraction(
		TEXT("mp.QuestArmLengthCalibrationDownMinBelowShoulderFraction"),
		0.40f,
		TEXT("Minimum mapped below-shoulder drop fraction required for the arms-down calibration pose."));

	TAutoConsoleVariable<float> CVarQuestArmLengthCalibrationDownMinVerticalDominance(
		TEXT("mp.QuestArmLengthCalibrationDownMinVerticalDominance"),
		0.65f,
		TEXT("Minimum below-shoulder drop divided by reach required for the arms-down calibration pose."));

	TAutoConsoleVariable<float> CVarQuestArmLengthCalibrationDownMinCorrectedReachFraction(
		TEXT("mp.QuestArmLengthCalibrationDownMinCorrectedReachFraction"),
		0.95f,
		TEXT("Minimum corrected arms-down shoulder-to-wrist reach fraction required before accepting arm length calibration."));

	TAutoConsoleVariable<int32> CVarQuestArmDownFrameCorrection(
		TEXT("mp.QuestArmDownFrameCorrection"),
		0,
		TEXT("When non-zero, apply the measured arms-down calibration to the downward component of HMD-relative Quest wrist targets."));

	TAutoConsoleVariable<float> CVarQuestArmDownFrameCorrectionMaxScale(
		TEXT("mp.QuestArmDownFrameCorrectionMaxScale"),
		1.80f,
		TEXT("Safety cap for the measured arms-down correction scale."));

	TAutoConsoleVariable<int32> CVarQuestArmDropoutDownFallback(
		TEXT("mp.QuestArmDropoutDownFallback"),
		0,
		TEXT("When non-zero, infer a calibrated arms-down wrist endpoint while Quest hand tracking is lost after a recent arms-down tracked solve."));

	TAutoConsoleVariable<float> CVarQuestArmDropoutDownFallbackRecentTrackedSeconds(
		TEXT("mp.QuestArmDropoutDownFallbackRecentTrackedSeconds"),
		3.0f,
		TEXT("Maximum age, in seconds, of the last tracked Quest arm pose that can seed the arms-down dropout fallback."));

	TAutoConsoleVariable<float> CVarQuestArmDropoutDownFallbackMinDownDominance(
		TEXT("mp.QuestArmDropoutDownFallbackMinDownDominance"),
		0.55f,
		TEXT("Minimum below-shoulder drop divided by direct shoulder-to-wrist reach required before Quest hand loss can infer an arms-down fallback."));

	TAutoConsoleVariable<float> CVarQuestArmDropoutDownFallbackBlendHalfLife(
		TEXT("mp.QuestArmDropoutDownFallbackBlendHalfLife"),
		0.08f,
		TEXT("Half-life, in seconds, for blending the inferred arms-down dropout wrist target. 0 snaps directly to the calibrated target."));

	TAutoConsoleVariable<float> CVarQuestWristPositionBlend(
		TEXT("mp.QuestWristPositionBlend"),
		0.0f,
		TEXT("Blend weight for Quest/OpenXR wrist position. Default 0 keeps MediaPipe owning wrist, elbow, and shoulder position while Quest still drives wrist rotation and fingers."));

	TAutoConsoleVariable<int32> CVarQuestWristForceArmIK(
		TEXT("mp.QuestWristForceArmIK"),
		0,
		TEXT("When non-zero, a Quest wrist endpoint forces the full two-bone arm IK branch. Default off keeps the upper arm MediaPipe/elbow driven and applies Quest endpoint influence through the lower arm target."));

	TAutoConsoleVariable<int32> CVarQuestWristReachAssist(
		TEXT("mp.QuestWristReachAssist"),
		0,
		TEXT("When non-zero, use Quest/OpenXR wrist position to improve upper/lower arm reach through the existing FK arm rotations without enabling the arm IK branch."));

	TAutoConsoleVariable<float> CVarQuestWristReachAssistBlend(
		TEXT("mp.QuestWristReachAssistBlend"),
		0.75f,
		TEXT("Blend weight for the non-IK Quest wrist reach assist elbow target."));

	TAutoConsoleVariable<float> CVarQuestWristReachAssistMaxElbowMoveCm(
		TEXT("mp.QuestWristReachAssistMaxElbowMoveCm"),
		45.0f,
		TEXT("Maximum per-frame elbow target movement allowed by Quest wrist reach assist. 0 disables the clamp."));

	TAutoConsoleVariable<int32> CVarQuestArmMode(
		TEXT("mp.QuestArmMode"),
		0,
		TEXT("User-facing Quest/MediaPipe arm mode: 0=MediaPipe wrist authority with Quest hands only, 1=adaptive Quest wrist reach assist, 2=Quest-constrained calibrated wrist solve, 3=HMD-relative Quest wrist endpoint in avatar space with MediaPipe shoulder/elbow hints."));

	FString GMetaHumanActiveProfile(TEXT("Wallace"));
	FAutoConsoleVariableRef CVarMetaHumanActiveProfile(
		TEXT("mp.MetaHumanActiveProfile"),
		GMetaHumanActiveProfile,
		TEXT("Active MetaHuman retarget profile id. Built-in profiles: Wallace, Emory, Hudson, Kellan, Maria, Payton."),
		ECVF_Default);

	FString GMetaHumanProfileAssetPaths(TEXT(""));
	FAutoConsoleVariableRef CVarMetaHumanProfileAssetPaths(
		TEXT("mp.MetaHumanProfileAssetPaths"),
		GMetaHumanProfileAssetPaths,
		TEXT("Semicolon- or comma-separated UMediaPipeMetaHumanRetargetProfile asset paths. Configured profiles override built-in profiles with the same id."),
		ECVF_Default);

	FString GAvatarCalibrationProfilePath(TEXT(""));
	FAutoConsoleVariableRef CVarAvatarCalibrationProfilePath(
		TEXT("mp.AvatarCalibrationProfilePath"),
		GAvatarCalibrationProfilePath,
		TEXT("Optional avatar-locked calibration profile JSON path. Only mode=avatar_locked_proteus safe source timing/alignment/anchor/bone-map fields are merged; user body-shape, avatar scaling, and MetaHuman deformation fields are rejected."),
		ECVF_Default);

	TAutoConsoleVariable<int32> CVarMetaHumanArmSource(
		TEXT("mp.MetaHumanArmSource"),
		-1,
		TEXT("Generic MetaHuman arm authority: -1=profile default, 0=legacy MediaPipe/Quest arm solver, 1=TestingKit3 full arm-chain provider."));

	TAutoConsoleVariable<int32> CVarMetaHumanFullArmChainTrace(
		TEXT("mp.MetaHumanFullArmChainTrace"),
		-1,
		TEXT("Generic full arm-chain proof logging: -1=profile default, 0=off, 1=log mp.MetaHumanFullArmChain rows while full-chain mode is active."));

	TAutoConsoleVariable<float> CVarMetaHumanFullArmChainTraceLogIntervalSeconds(
		TEXT("mp.MetaHumanFullArmChainTraceLogIntervalSeconds"),
		-1.0f,
		TEXT("Generic minimum seconds between MetaHuman full arm-chain proof log lines per side. -1 uses profile default."));

	TAutoConsoleVariable<float> CVarMetaHumanFullArmChainMaxAgeSeconds(
		TEXT("mp.MetaHumanFullArmChainMaxAgeSeconds"),
		-1.0f,
		TEXT("Generic maximum accepted age in seconds for a full arm-chain provider sample. -1 uses profile default."));

	TAutoConsoleVariable<int32> CVarWallaceArmSource(
		TEXT("mp.WallaceArmSource"),
		0,
		TEXT("Deprecated Wallace-only arm authority alias kept for old scripts/logs. The active resolver ignores it; use mp.MetaHumanArmSource."));

	TAutoConsoleVariable<int32> CVarWallaceFullArmChainTrace(
		TEXT("mp.WallaceFullArmChainTrace"),
		1,
		TEXT("Deprecated Wallace-only trace alias kept for old scripts/logs. The active resolver ignores it; use mp.MetaHumanFullArmChainTrace."));

	TAutoConsoleVariable<float> CVarWallaceFullArmChainTraceLogIntervalSeconds(
		TEXT("mp.WallaceFullArmChainTraceLogIntervalSeconds"),
		0.25f,
		TEXT("Deprecated Wallace-only trace interval alias kept for old scripts/logs. The active resolver ignores it; use mp.MetaHumanFullArmChainTraceLogIntervalSeconds."));

	TAutoConsoleVariable<float> CVarWallaceFullArmChainMaxAgeSeconds(
		TEXT("mp.WallaceFullArmChainMaxAgeSeconds"),
		0.25f,
		TEXT("Deprecated Wallace-only sample max-age alias kept for old scripts/logs. The active resolver ignores it; use mp.MetaHumanFullArmChainMaxAgeSeconds."));

	TAutoConsoleVariable<int32> CVarQuestWristDriftGuard(
		TEXT("mp.QuestWristDriftGuard"),
		1,
		TEXT("When non-zero, high Quest-vs-MediaPipe wrist disagreement gives the no-IK reach assist more Quest endpoint authority. Quick revert: mp.QuestWristDriftGuard 0."));

	TAutoConsoleVariable<float> CVarQuestWristDriftGuardStartCm(
		TEXT("mp.QuestWristDriftGuardStartCm"),
		18.0f,
		TEXT("Quest-vs-MediaPipe wrist disagreement where the no-IK drift guard starts increasing Quest arm authority, in centimeters."));

	TAutoConsoleVariable<float> CVarQuestWristDriftGuardFullCm(
		TEXT("mp.QuestWristDriftGuardFullCm"),
		55.0f,
		TEXT("Quest-vs-MediaPipe wrist disagreement where the no-IK drift guard reaches full strength, in centimeters."));

	TAutoConsoleVariable<float> CVarQuestWristDriftGuardReachBlendBoost(
		TEXT("mp.QuestWristDriftGuardReachBlendBoost"),
		0.35f,
		TEXT("Additional reach-assist blend added at full drift-guard strength. 0 keeps the base mp.QuestWristReachAssistBlend."));

	TAutoConsoleVariable<float> CVarQuestWristDriftGuardExtraElbowMoveCm(
		TEXT("mp.QuestWristDriftGuardExtraElbowMoveCm"),
		18.0f,
		TEXT("Additional allowed elbow-target movement at full drift-guard strength, in centimeters."));

	TAutoConsoleVariable<float> CVarQuestWristDriftGuardPoleBlend(
		TEXT("mp.QuestWristDriftGuardPoleBlend"),
		0.85f,
		TEXT("Blend toward a stable torso/body elbow pole at full drift-guard strength. 0 keeps the MediaPipe elbow pole."));

	TAutoConsoleVariable<int32> CVarQuestConstrainedArmSolve(
		TEXT("mp.QuestConstrainedArmSolve"),
		0,
		TEXT("When non-zero, solve arms from MediaPipe shoulder/body intent, Quest/OpenXR wrist endpoint, avatar arm lengths, and a low-weight MediaPipe elbow hint without entering the arm IK branch. Quick revert: mp.QuestConstrainedArmSolve 0."));

	TAutoConsoleVariable<float> CVarQuestConstrainedArmSolveBlend(
		TEXT("mp.QuestConstrainedArmSolveBlend"),
		1.0f,
		TEXT("Blend from the current MediaPipe elbow to the Quest-constrained no-IK analytic elbow target."));

	TAutoConsoleVariable<float> CVarQuestConstrainedArmWristAuthority(
		TEXT("mp.QuestConstrainedArmWristAuthority"),
		1.0f,
		TEXT("Blend from the current wrist endpoint to the mapped Quest/OpenXR wrist endpoint before the constrained arm solve."));

	TAutoConsoleVariable<float> CVarQuestConstrainedArmWristAuthorityMin(
		TEXT("mp.QuestConstrainedArmWristAuthorityMin"),
		1.0f,
		TEXT("Minimum Quest wrist endpoint authority used by the constrained arm solve when the Quest wrist correction is far from the MediaPipe wrist. Set to 1 to disable adaptive authority reduction."));

	TAutoConsoleVariable<float> CVarQuestConstrainedArmWristAuthorityFadeStartCm(
		TEXT("mp.QuestConstrainedArmWristAuthorityFadeStartCm"),
		0.0f,
		TEXT("Quest-to-MediaPipe wrist correction distance where constrained arm wrist authority begins fading toward mp.QuestConstrainedArmWristAuthorityMin. 0 disables adaptive authority reduction."));

	TAutoConsoleVariable<float> CVarQuestConstrainedArmWristAuthorityFadeFullCm(
		TEXT("mp.QuestConstrainedArmWristAuthorityFadeFullCm"),
		65.0f,
		TEXT("Quest-to-MediaPipe wrist correction distance where constrained arm wrist authority reaches mp.QuestConstrainedArmWristAuthorityMin."));

	TAutoConsoleVariable<float> CVarQuestConstrainedArmMediaPipeElbowHint(
		TEXT("mp.QuestConstrainedArmMediaPipeElbowHint"),
		0.20f,
		TEXT("How much the constrained arm solve keeps the MediaPipe elbow pole after projecting it onto the Quest wrist reach plane."));

	TAutoConsoleVariable<float> CVarQuestConstrainedArmStablePoleDown(
		TEXT("mp.QuestConstrainedArmStablePoleDown"),
		0.25f,
		TEXT("Downward component in the stable torso/body elbow pole used by the constrained arm solve."));

	TAutoConsoleVariable<float> CVarQuestConstrainedArmMaxReachFraction(
		TEXT("mp.QuestConstrainedArmMaxReachFraction"),
		0.985f,
		TEXT("Maximum solved shoulder-to-wrist reach as a fraction of avatar arm length for the Quest-constrained arm path. Profile 4 raises this near full extension while staying below the singularity."));

	TAutoConsoleVariable<float> CVarQuestConstrainedArmSolvedPlaneMinSin(
		TEXT("mp.QuestConstrainedArmSolvedPlaneMinSin"),
		0.08f,
		TEXT("Minimum elbow-plane sin(angle) required when writing rotations from a successful Quest-constrained arm solve. Lower than the direct MediaPipe arm threshold because the constrained solver already owns near-full pole continuity."));

	TAutoConsoleVariable<float> CVarQuestConstrainedArmCloseReachStartCm(
		TEXT("mp.QuestConstrainedArmCloseReachStartCm"),
		38.0f,
		TEXT("Shoulder-to-wrist reach distance where the constrained arm solve starts biasing the elbow pole for close-to-torso hand poses. 0 disables close-reach bias."));

	TAutoConsoleVariable<float> CVarQuestConstrainedArmCloseReachFullCm(
		TEXT("mp.QuestConstrainedArmCloseReachFullCm"),
		24.0f,
		TEXT("Shoulder-to-wrist reach distance where the close-to-torso elbow pole bias reaches full strength."));

	TAutoConsoleVariable<float> CVarQuestConstrainedArmCloseReachPoleBias(
		TEXT("mp.QuestConstrainedArmCloseReachPoleBias"),
		0.85f,
		TEXT("Blend strength for the close-to-torso constrained-arm elbow pole. Higher values keep elbows outward/down when the Quest wrist is close to the chest."));

	TAutoConsoleVariable<float> CVarQuestConstrainedArmCloseReachStablePoleDown(
		TEXT("mp.QuestConstrainedArmCloseReachStablePoleDown"),
		0.85f,
		TEXT("Downward component used by the constrained arm elbow pole at full close-to-torso reach bias."));

	TAutoConsoleVariable<float> CVarQuestConstrainedArmMaxElbowMoveCm(
		TEXT("mp.QuestConstrainedArmMaxElbowMoveCm"),
		65.0f,
		TEXT("Maximum elbow target movement allowed by the Quest-constrained no-IK arm solve. 0 disables this clamp."));

	TAutoConsoleVariable<float> CVarQuestConstrainedArmMaxReachStepCm(
		TEXT("mp.QuestConstrainedArmMaxReachStepCm"),
		0.0f,
		TEXT("Maximum per-evaluation change in solved shoulder-to-wrist reach for the Quest-constrained arm path. 0 disables reach continuity."));

	TAutoConsoleVariable<float> CVarQuestConstrainedArmElbowHalfLife(
		TEXT("mp.QuestConstrainedArmElbowHalfLife"),
		0.0f,
		TEXT("Smoothing half-life in seconds for the Quest-constrained analytic elbow target. 0 disables temporal elbow smoothing."));

	TAutoConsoleVariable<float> CVarQuestConstrainedArmMaxElbowStepCm(
		TEXT("mp.QuestConstrainedArmMaxElbowStepCm"),
		0.0f,
		TEXT("Maximum per-evaluation movement of the Quest-constrained analytic elbow target. 0 disables the temporal step clamp."));

	TAutoConsoleVariable<int32> CVarQuestConstrainedArmNearFullPoleContinuity(
		TEXT("mp.QuestConstrainedArmNearFullPoleContinuity"),
		1,
		TEXT("When non-zero, near-full Quest-constrained arm extension keeps the previous elbow pole so straight arms do not branch-flip at the IK singularity."));

	TAutoConsoleVariable<float> CVarQuestConstrainedArmNearFullPoleStartFraction(
		TEXT("mp.QuestConstrainedArmNearFullPoleStartFraction"),
		0.90f,
		TEXT("Shoulder-to-wrist reach fraction where previous-pole continuity starts for near-full Quest-constrained arm extension."));

	TAutoConsoleVariable<float> CVarQuestConstrainedArmNearFullPoleFullFraction(
		TEXT("mp.QuestConstrainedArmNearFullPoleFullFraction"),
		0.965f,
		TEXT("Shoulder-to-wrist reach fraction where previous-pole continuity fully owns the near-full Quest-constrained elbow pole."));

	TAutoConsoleVariable<int32> CVarQuestConstrainedArmBodyFallback(
		TEXT("mp.QuestConstrainedArmBodyFallback"),
		1,
		TEXT("When non-zero, HMD-relative constrained Quest arms keep using the constrained solve with a MediaPipe body-proportioned endpoint when the Quest wrist endpoint is temporarily unavailable."));

	TAutoConsoleVariable<float> CVarQuestConstrainedArmBodyFallbackWristHalfLife(
		TEXT("mp.QuestConstrainedArmBodyFallbackWristHalfLife"),
		0.08f,
		TEXT("Half-life, in seconds, for blending a constrained body-fallback wrist endpoint from the last constrained arm solve when Quest wrist tracking drops."));

	TAutoConsoleVariable<float> CVarQuestConstrainedArmBodyFallbackMaxWristStepCm(
		TEXT("mp.QuestConstrainedArmBodyFallbackMaxWristStepCm"),
		14.0f,
		TEXT("Maximum per-frame wrist endpoint movement, in cm, while constrained body fallback transitions from the last solved Quest endpoint. 0 disables the step clamp."));

	TAutoConsoleVariable<int32> CVarQuestConstrainedArmDownStraighten(
		TEXT("mp.QuestConstrainedArmDownStraighten"),
		0,
		TEXT("When non-zero, let the Quest-constrained arm solve extend near-full-reach arms-down poses toward anatomical straightness before solving the elbow."));

	TAutoConsoleVariable<float> CVarQuestConstrainedArmDownStraightenThresholdCm(
		TEXT("mp.QuestConstrainedArmDownStraightenThresholdCm"),
		22.0f,
		TEXT("Full-extension reach deficit, in cm, below which arms-down Quest wrist endpoints may be straightened. Larger deficits are treated as intentional elbow bend."));

	TAutoConsoleVariable<float> CVarQuestConstrainedArmDownStraightenMaxCm(
		TEXT("mp.QuestConstrainedArmDownStraightenMaxCm"),
		18.0f,
		TEXT("Maximum reach extension, in cm, applied by mp.QuestConstrainedArmDownStraighten."));

	TAutoConsoleVariable<float> CVarQuestConstrainedArmDownStraightenMinBelowShoulderRatio(
		TEXT("mp.QuestConstrainedArmDownStraightenMinBelowShoulderRatio"),
		0.30f,
		TEXT("Minimum shoulder-to-wrist downward distance, as a fraction of arm length, required before arms-down straightening can apply."));

	TAutoConsoleVariable<float> CVarQuestConstrainedArmDownStraightenReachFloorFraction(
		TEXT("mp.QuestConstrainedArmDownStraightenReachFloorFraction"),
		0.997f,
		TEXT("Minimum solved shoulder-to-wrist reach, as a fraction of avatar arm length, for arms-down straightening."));

	TAutoConsoleVariable<float> CVarQuestConstrainedArmDownStraightenMaxReachFraction(
		TEXT("mp.QuestConstrainedArmDownStraightenMaxReachFraction"),
		0.997f,
		TEXT("Maximum solved shoulder-to-wrist reach as a fraction of avatar arm length for arms-down straightening. Keep below 1.0 to avoid IK singularity snaps."));

	TAutoConsoleVariable<int32> CVarQuestConstrainedArmReachScaleCalibration(
		TEXT("mp.QuestConstrainedArmReachScaleCalibration"),
		0,
		TEXT("When non-zero, HMD-relative Quest wrist endpoints learn the wearer's observed reach and normalize it to the avatar arm reach before the constrained solve."));

	TAutoConsoleVariable<int32> CVarQuestConstrainedArmReachScaleUniform(
		TEXT("mp.QuestConstrainedArmReachScaleUniform"),
		0,
		TEXT("When non-zero, apply the learned wearer-to-avatar Quest arm reach scale to the whole reach range instead of only near extension."));

	TAutoConsoleVariable<float> CVarQuestConstrainedArmReachScaleMinObservedFraction(
		TEXT("mp.QuestConstrainedArmReachScaleMinObservedFraction"),
		0.88f,
		TEXT("Minimum observed Quest shoulder-to-wrist reach, as a fraction of avatar max reach, before adaptive reach-scale normalization is allowed."));

	TAutoConsoleVariable<float> CVarQuestConstrainedArmReachScaleApplyStartFraction(
		TEXT("mp.QuestConstrainedArmReachScaleApplyStartFraction"),
		0.70f,
		TEXT("Current reach fraction of the learned observed maximum where adaptive Quest reach scaling starts blending in."));

	TAutoConsoleVariable<float> CVarQuestConstrainedArmReachScaleApplyFullFraction(
		TEXT("mp.QuestConstrainedArmReachScaleApplyFullFraction"),
		0.95f,
		TEXT("Current reach fraction of the learned observed maximum where adaptive Quest reach scaling reaches full strength."));

	TAutoConsoleVariable<float> CVarQuestConstrainedArmReachScaleMin(
		TEXT("mp.QuestConstrainedArmReachScaleMin"),
		0.82f,
		TEXT("Minimum adaptive HMD-relative Quest reach scale. This lets the solver reduce over-long wearer reach without accepting outlier jumps."));

	TAutoConsoleVariable<float> CVarQuestConstrainedArmReachScaleMax(
		TEXT("mp.QuestConstrainedArmReachScaleMax"),
		1.18f,
		TEXT("Maximum adaptive HMD-relative Quest reach scale. This lets shorter mapped reaches reach avatar full extension without hard-coding a body size."));

	TAutoConsoleVariable<float> CVarQuestWristPositionScale(
		TEXT("mp.QuestWristPositionScale"),
		1.0f,
		TEXT("Scale applied to Quest/OpenXR wrist position deltas after the first-frame wrist calibration."));

	TAutoConsoleVariable<float> CVarQuestWristMaxRelativeDeltaCm(
		TEXT("mp.QuestWristMaxRelativeDeltaCm"),
		55.0f,
		TEXT("Maximum calibrated Quest wrist movement layered onto the current MediaPipe wrist, in Unreal centimeters. 0 disables clamping."));

	TAutoConsoleVariable<float> CVarQuestWristMaxOffsetCm(
		TEXT("mp.QuestWristMaxOffsetCm"),
		140.0f,
		TEXT("Maximum Quest wrist positional offset from the Quest HMD/head anchor, in Unreal centimeters. 0 disables clamping."));

	TAutoConsoleVariable<float> CVarQuestWristRawMaxDistanceCm(
		TEXT("mp.QuestWristRawMaxDistanceCm"),
		220.0f,
		TEXT("Reject Quest wrist samples farther than this from the live HMD pose, in Unreal centimeters. 0 disables rejection."));

	TAutoConsoleVariable<int32> CVarQuestWristPositionAdaptiveFilter(
		TEXT("mp.QuestWristPositionAdaptiveFilter"),
		0,
		TEXT("When non-zero, filter the Quest wrist correction vector in avatar component space with low lag during fast motion and stronger smoothing while still."));

	TAutoConsoleVariable<float> CVarQuestWristPositionFilterStillHalfLife(
		TEXT("mp.QuestWristPositionFilterStillHalfLife"),
		0.11f,
		TEXT("Quest wrist correction half-life while the hand is nearly still. Higher values suppress stationary jitter."));

	TAutoConsoleVariable<float> CVarQuestWristPositionFilterMovingHalfLife(
		TEXT("mp.QuestWristPositionFilterMovingHalfLife"),
		0.018f,
		TEXT("Quest wrist correction half-life while the hand is moving quickly. Lower values reduce perceived tracking lag."));

	TAutoConsoleVariable<float> CVarQuestWristPositionFilterSpeedForMinLag(
		TEXT("mp.QuestWristPositionFilterSpeedForMinLag"),
		120.0f,
		TEXT("Correction-vector speed in cm/s at which the adaptive Quest wrist filter reaches its moving half-life."));

	TAutoConsoleVariable<float> CVarQuestWristPositionFilterDeadbandCm(
		TEXT("mp.QuestWristPositionFilterDeadbandCm"),
		0.65f,
		TEXT("Soft deadband in cm applied to tiny Quest wrist correction changes before adaptive filtering."));

	TAutoConsoleVariable<float> CVarQuestWristPositionFilterResetDistanceCm(
		TEXT("mp.QuestWristPositionFilterResetDistanceCm"),
		45.0f,
		TEXT("Reset the Quest wrist correction filter when the raw correction jumps farther than this in one sample. 0 disables reset."));

	TAutoConsoleVariable<float> CVarQuestHmdAvatarTranslationHalfLife(
		TEXT("mp.QuestHmdAvatarTranslationHalfLife"),
		0.055f,
		TEXT("Half-life for smoothing the live HMD translation anchor used by HMD-avatar Quest wrist mapping. Yaw remains fixed by calibration. 0 disables smoothing."));

	TAutoConsoleVariable<float> CVarQuestHmdAvatarTranslationResetDistanceCm(
		TEXT("mp.QuestHmdAvatarTranslationResetDistanceCm"),
		85.0f,
		TEXT("Reset the HMD-avatar translation anchor filter when the live HMD anchor jumps farther than this from the filtered anchor in one sample. 0 disables reset."));

	TAutoConsoleVariable<int32> CVarQuestWristUseJointRotation(
		TEXT("mp.QuestWristUseJointRotation"),
		1,
		TEXT("When non-zero, use the OpenXR wrist joint rotation delta for Manny wrist orientation before falling back to a palm basis from Quest joint positions. Ignored by mp.QuestPalmMode=2 so visible palm landmark geometry remains authoritative."));

	TAutoConsoleVariable<int32> CVarQuestWristUseJointRotationLeft(
		TEXT("mp.QuestWristUseJointRotationLeft"),
		1,
		TEXT("When mp.QuestWristUseJointRotation is non-zero, left wrist may use the OpenXR wrist joint rotation source."));

	TAutoConsoleVariable<int32> CVarQuestWristUseJointRotationRight(
		TEXT("mp.QuestWristUseJointRotationRight"),
		1,
		TEXT("When mp.QuestWristUseJointRotation is non-zero, right wrist may use the OpenXR wrist joint rotation source. Default on because the projected palm fallback can snap when its selected axis changes."));

	TAutoConsoleVariable<float> CVarQuestWristTwistBlend(
		TEXT("mp.QuestWristTwistBlend"),
		1.0f,
		TEXT("Blend weight for Quest wrist roll around the MediaPipe forearm axis."));

	TAutoConsoleVariable<float> CVarQuestWristSwingBlend(
		TEXT("mp.QuestWristSwingBlend"),
		1.0f,
		TEXT("Blend weight for calibrated Quest wrist swing away from the MediaPipe forearm axis. Roll remains solved separately so flex/extension does not replace anatomical twist."));

	TAutoConsoleVariable<int32> CVarQuestWristTwistDrivesForearm(
		TEXT("mp.QuestWristTwistDrivesForearm"),
		0,
		TEXT("Experimental only. When non-zero, Quest wrist roll may drive a bounded lowerarm-main roll before the hand is re-applied. Default off after headset validation showed snappy arms without fixing MetaHuman forearm wrapping."));

	TAutoConsoleVariable<float> CVarQuestWristForearmTwistBlend(
		TEXT("mp.QuestWristForearmTwistBlend"),
		0.0f,
		TEXT("Fraction of Quest wrist roll absorbed by Manny's lowerarm pronation/supination layer."));

	TAutoConsoleVariable<float> CVarQuestWristForearmMaxTwistDegrees(
		TEXT("mp.QuestWristForearmMaxTwistDegrees"),
		55.0f,
		TEXT("Maximum Quest wrist roll that optional lowerarm twist helper deformation may receive."));

	TAutoConsoleVariable<int32> CVarQuestWristForearmRollDriveTwistHelpers(
		TEXT("mp.QuestWristForearmRollDriveTwistHelpers"),
		0,
		TEXT("When non-zero, also rotate lowerarm twist helper bones from the Quest forearm roll layer using reference local offsets and position-weighted distribution."));

	TAutoConsoleVariable<int32> CVarQuestWristUpperArmRollDriveTwistHelpers(
		TEXT("mp.QuestWristUpperArmRollDriveTwistHelpers"),
		0,
		TEXT("Diagnostic only. When non-zero, distribute a bounded share of Quest wrist roll into upperarm_twist_01/02 helper bones. Does not rotate upperarm or clavicle main bones."));

	TAutoConsoleVariable<float> CVarQuestWristUpperArmTwistBlend(
		TEXT("mp.QuestWristUpperArmTwistBlend"),
		0.0f,
		TEXT("Fraction of Quest wrist roll sent to upperarm twist helper bones when mp.QuestWristUpperArmRollDriveTwistHelpers is enabled."));

	TAutoConsoleVariable<float> CVarQuestWristUpperArmMaxTwistDegrees(
		TEXT("mp.QuestWristUpperArmMaxTwistDegrees"),
		24.0f,
		TEXT("Maximum Quest wrist roll, in degrees, that may be distributed to upperarm twist helper bones."));

	TAutoConsoleVariable<int32> CVarQuestWristDriveTwistCorrection(
		TEXT("mp.QuestWristDriveTwistCorrection"),
		0,
		TEXT("Legacy helper-only correction. When non-zero, drive lowerarm twist helper leaf bones from final hand-local wrist twist. Default off for MetaHuman; profile 4 also keeps mp.QuestWristTwistDrivesForearm off."));

	TAutoConsoleVariable<float> CVarQuestWristTwistCorrectionBlend(
		TEXT("mp.QuestWristTwistCorrectionBlend"),
		1.0f,
		TEXT("Scale for Quest wrist twist correction applied to Manny lowerarm twist helper leaf bones. Per-helper weights are measured from helper position along the forearm."));

	TAutoConsoleVariable<float> CVarQuestWristTwistCorrectionMaxDegrees(
		TEXT("mp.QuestWristTwistCorrectionMaxDegrees"),
		35.0f,
		TEXT("Maximum final hand-local wrist twist that Manny lowerarm twist helper leaf bones may receive from twist correction. This protects the forearm from extreme Quest roll solves while keeping hand roll responsive."));

	TAutoConsoleVariable<float> CVarQuestWristTwistCorrectionStartDegrees(
		TEXT("mp.QuestWristTwistCorrectionStartDegrees"),
		45.0f,
		TEXT("Hand-local wrist twist, in degrees, before lowerarm twist helper leaf-bone relief starts. Keeps small wrist rolls on the hand."));

	TAutoConsoleVariable<float> CVarQuestWristTwistCorrectionFullDegrees(
		TEXT("mp.QuestWristTwistCorrectionFullDegrees"),
		130.0f,
		TEXT("Hand-local wrist twist, in degrees, where lowerarm twist helper leaf-bone relief reaches full blend."));

	TAutoConsoleVariable<float> CVarQuestWristTwistCorrectionUpperArmShare(
		TEXT("mp.QuestWristTwistCorrectionUpperArmShare"),
		0.0f,
		TEXT("Small share of high hand-local wrist twist also applied to upperarm twist helper leaf bones. This is deformation relief only; the elbow hinge is not rolled."));

	TAutoConsoleVariable<float> CVarQuestWristMaxTwistDegrees(
		TEXT("mp.QuestWristMaxTwistDegrees"),
		170.0f,
		TEXT("Maximum Quest wrist roll applied around the MediaPipe forearm axis. High default keeps the hand from hitting a hard stop; forearm helper deformation is clamped separately."));

	TAutoConsoleVariable<float> CVarQuestWristMaxSwingDegrees(
		TEXT("mp.QuestWristMaxSwingDegrees"),
		140.0f,
		TEXT("Maximum Quest wrist flexion/extension swing allowed when mp.QuestWristSwingBlend is above zero. High default avoids clipping palm-up poses; forearm helper deformation is clamped separately."));

	TAutoConsoleVariable<int32> CVarQuestWristRejectSwingClamp(
		TEXT("mp.QuestWristRejectSwingClamp"),
		1,
		TEXT("When non-zero, suppress the tracked Quest hand swing component when it would hit the wrist swing clamp, while still allowing forearm-axis twist/roll. This keeps bad palm/forearm mappings from forcing visible arm deformation without freezing wrist roll."));

	TAutoConsoleVariable<float> CVarQuestWristSemanticRollMinPalmProjection(
		TEXT("mp.QuestWristSemanticRollMinPalmProjection"),
		0.45f,
		TEXT("Minimum palm-normal projection confidence for semantic Quest wrist roll. Below this, palm flexion makes forearm-axis roll underdetermined and the solve holds the previous roll instead of creating wrapper twist."));

	TAutoConsoleVariable<int32> CVarQuestPalmMode(
		TEXT("mp.QuestPalmMode"),
		0,
		TEXT("User-facing Quest palm roll mode: 0=MediaPipe-relative projected palm roll with hold-on-weak-projection, 1=legacy local Quest quaternion twist fallback, 2=Quest-authoritative hand orientation while keeping the existing arm/wrist position solve and hand-rotation smoothing."));

	TAutoConsoleVariable<float> CVarQuestWristLostTrackingGraceSeconds(
		TEXT("mp.QuestWristLostTrackingGraceSeconds"),
		0.35f,
		TEXT("Seconds to keep using the last mapped Quest wrist endpoint after OpenXR briefly drops the tracked flag."));

	TAutoConsoleVariable<float> CVarQuestHandRotationLostTrackingGraceSeconds(
		TEXT("mp.QuestHandRotationLostTrackingGraceSeconds"),
		0.45f,
		TEXT("Seconds to hold the last applied Quest hand rotation after OpenXR briefly drops the tracked flag."));

	TAutoConsoleVariable<float> CVarQuestHandRotationLostTrackingFadeSeconds(
		TEXT("mp.QuestHandRotationLostTrackingFadeSeconds"),
		0.75f,
		TEXT("Seconds to fade held Quest hand rotation back to the MediaPipe hand target after lost-tracking grace expires."));

	TAutoConsoleVariable<int32> CVarQuestHandRotationRequireTracked(
		TEXT("mp.QuestHandRotationRequireTracked"),
		1,
		TEXT("When non-zero, Quest hand rotation holds the last valid pose while OpenXR reports the hand side as untracked instead of consuming stale side data."));

	TAutoConsoleVariable<float> CVarQuestHandRotationMaxDeltaFromMediaPipeDegrees(
		TEXT("mp.QuestHandRotationMaxDeltaFromMediaPipeDegrees"),
		180.0f,
		TEXT("Maximum accepted Quest hand rotation delta from the current MediaPipe hand target after calibration. Larger deltas are treated as a bad wrist solve and the last valid hand rotation is held."));

	TAutoConsoleVariable<float> CVarQuestHandRotationMaxStepDegrees(
		TEXT("mp.QuestHandRotationMaxStepDegrees"),
		0.0f,
		TEXT("Maximum per-frame Quest hand rotation smoothing step in degrees. 0 disables fixed-step clamping so fast natural hand motion is not artificially limited."));

	TAutoConsoleVariable<float> CVarQuestHandRotationHalfLife(
		TEXT("mp.QuestHandRotationHalfLife"),
		0.0f,
		TEXT("Quest hand rotation smoothing half-life in seconds. 0 follows the Quest hand target directly; mode 2 still honors this filter to damp palm-basis roll jumps."));

	TAutoConsoleVariable<int32> CVarQuestWristInvertTwist(
		TEXT("mp.QuestWristInvertTwist"),
		0,
		TEXT("When non-zero, invert Quest wrist roll direction for quick side-independent calibration testing."));

	TAutoConsoleVariable<int32> CVarQuestWristInvertTwistLeft(
		TEXT("mp.QuestWristInvertTwistLeft"),
		0,
		TEXT("When non-zero, invert Quest wrist roll direction for the left hand only."));

	TAutoConsoleVariable<int32> CVarQuestWristInvertTwistRight(
		TEXT("mp.QuestWristInvertTwistRight"),
		0,
		TEXT("When non-zero, invert Quest wrist roll direction for the right hand only."));

	TAutoConsoleVariable<float> CVarQuestFingerRotationHalfLife(
		TEXT("mp.QuestFingerRotationHalfLife"),
		0.035f,
		TEXT("Smoothing half-life in seconds for Quest/OpenXR finger rotations. 0 disables smoothing."));

	TAutoConsoleVariable<int32> CVarQuestFingerDebug(
		TEXT("mp.QuestFingerDebug"),
		0,
		TEXT("When non-zero, log Quest/OpenXR finger retargeting once per second per hand without enabling wrist-solve debug logs."));

	TAutoConsoleVariable<int32> CVarQuestFingerCurlOnly(
		TEXT("mp.QuestFingerCurlOnly"),
		0,
		TEXT("When non-zero, retarget Quest fingers as local curl only. Default off; the accepted VR Preview path uses parent-chain Quest segment directions with target hand lengths preserved."));

	TAutoConsoleVariable<int32> CVarQuestFingerJointRetarget(
		TEXT("mp.QuestFingerJointRetarget"),
		0,
		TEXT("When non-zero, retarget Quest/OpenXR fingers from full joint orientations using source-rest offsets. Default off; the accepted VR Preview default uses parent-chain segment directions because some OpenXR runtimes publish accepted but visually static finger orientations."));

	TAutoConsoleVariable<int32> CVarQuestFingerPreserveSpread(
		TEXT("mp.QuestFingerPreserveSpread"),
		0,
		TEXT("Experimental. When non-zero, non-thumb Quest curl-only fingers bend in the palm plane instead of curling toward the wrist/root. Default off because the current rig basis can bend fingers around the wrong axis."));

	TAutoConsoleVariable<int32> CVarQuestFingerPairSeparation(
		TEXT("mp.QuestFingerPairSeparation"),
		0,
		TEXT("When non-zero, the segment-direction finger retarget enforces a minimum signed separation between adjacent non-thumb fingers (index/middle, middle/ring, ring/pinky) at every segment level, pushing a too-close or crossed pair apart symmetrically about its separation axis. Convention-free (no joint-axis or curl-plane estimate) and curl-preserving, so interpenetration becomes geometrically impossible for any wearer's hand on any avatar. Default off to keep replay evaluation byte-stable; the live lower-body trial layer enables it."));

	TAutoConsoleVariable<float> CVarQuestFingerPairSeparationRefScale(
		TEXT("mp.QuestFingerPairSeparationRefScale"),
		0.7f,
		TEXT("Fraction of the avatar's own reference adjacent-finger separation enforced as the live minimum by mp.QuestFingerPairSeparation. 1.0 pins fingers at their reference spacing; lower values allow deliberate adduction down to that fraction."));

	TAutoConsoleVariable<float> CVarQuestFingerPairSeparationMinDeg(
		TEXT("mp.QuestFingerPairSeparationMinDeg"),
		4.0f,
		TEXT("Absolute floor (degrees) for the minimum adjacent-finger separation enforced by mp.QuestFingerPairSeparation, protecting rigs whose reference fingers are nearly parallel."));

	TAutoConsoleVariable<int32> CVarQuestFingerPoseGate(
		TEXT("mp.QuestFingerPoseGate"),
		0,
		TEXT("When non-zero, hold the last good hand pose instead of consuming Quest finger joints that are untracked or that changed faster than fingers can physically move (tracking collapse to garbage fists when fingers self-occlude, measured live 2026-06-12). A genuinely instant pose change is accepted after it stays stable for the recovery window. Default off to keep replay evaluation byte-stable; the live lower-body trial layer enables it."));

	TAutoConsoleVariable<float> CVarQuestFingerPoseGateMaxCurlRatePerSec(
		TEXT("mp.QuestFingerPoseGateMaxCurlRatePerSec"),
		5.0f,
		TEXT("Hand-mean curl rate (curl-units/second) above which a frame is treated as a tracking collapse by mp.QuestFingerPoseGate. Real fast fists measure ~4; the observed garbage snaps ~9+."));

	TAutoConsoleVariable<float> CVarQuestFingerPoseGateRecoverSeconds(
		TEXT("mp.QuestFingerPoseGateRecoverSeconds"),
		0.25f,
		TEXT("How long a rejected hand pose must stay stable before mp.QuestFingerPoseGate accepts it. This is the maximum latency added to a genuinely instant pose change."));

	TAutoConsoleVariable<int32> CVarQuestFingerUseChainCurl(
		TEXT("mp.QuestFingerUseChainCurl"),
		1,
		TEXT("When non-zero, drives non-thumb Quest finger curl from metacarpal/proximal/intermediate/distal joint-chain bend, with palm-forward curl as a fallback."));

	TAutoConsoleVariable<float> CVarQuestFingerCurlStrength(
		TEXT("mp.QuestFingerCurlStrength"),
		1.00f,
		TEXT("Multiplier for Quest curl-only finger retargeting. Default is capture-tuned so closed fists do not over-bend MetaHuman/Manny fingers into each other."));

	TAutoConsoleVariable<float> CVarQuestFingerCurlOpenAngleDegrees(
		TEXT("mp.QuestFingerCurlOpenAngleDegrees"),
		8.0f,
		TEXT("Quest segment angle from palm-forward treated as open for curl-only finger retargeting."));

	TAutoConsoleVariable<float> CVarQuestFingerCurlFullAngleDegrees(
		TEXT("mp.QuestFingerCurlFullAngleDegrees"),
		120.0f,
		TEXT("Quest segment angle from palm-forward treated as fully curled for curl-only finger retargeting. Tuned from the 2026-05-15 Quest capture: half fist was about 71 deg and closed fist about 123 deg."));

	TAutoConsoleVariable<float> CVarQuestFingerChainCurlFullAngleDegrees(
		TEXT("mp.QuestFingerChainCurlFullAngleDegrees"),
		78.0f,
		TEXT("Quest non-thumb joint-chain angle treated as fully curled. This is separate from palm-forward curl because MCP/PIP/DIP joint bends reach full fist earlier than segment-to-palm angles."));

	TAutoConsoleVariable<float> CVarQuestFingerMaxCurlDegrees(
		TEXT("mp.QuestFingerMaxCurlDegrees"),
		96.0f,
		TEXT("Maximum per-segment Manny finger curl applied by Quest curl-only retargeting. Default is capture-tuned to avoid squashed closed fists."));

	TAutoConsoleVariable<float> CVarQuestFingerClosedFistAssist(
		TEXT("mp.QuestFingerClosedFistAssist"),
		0.70f,
		TEXT("How strongly non-thumb fingers move toward full curl when Quest joint-chain data indicates a closed fist. 0 disables the assist."));

	TAutoConsoleVariable<float> CVarQuestFingerClosedFistAssistStart01(
		TEXT("mp.QuestFingerClosedFistAssistStart01"),
		0.50f,
		TEXT("Per-finger Quest curl value where closed-fist assist begins."));

	TAutoConsoleVariable<float> CVarQuestFingerClosedFistAssistFull01(
		TEXT("mp.QuestFingerClosedFistAssistFull01"),
		0.78f,
		TEXT("Per-finger Quest curl value where closed-fist assist reaches its configured strength."));

	TAutoConsoleVariable<float> CVarQuestFingerClosedFistHandAssist(
		TEXT("mp.QuestFingerClosedFistHandAssist"),
		0.85f,
		TEXT("How strongly all non-thumb fingers move toward full curl when the hand-level Quest curl pattern indicates a closed fist despite per-finger occlusion."));

	TAutoConsoleVariable<float> CVarQuestFingerClosedFistHandAssistStart01(
		TEXT("mp.QuestFingerClosedFistHandAssistStart01"),
		0.50f,
		TEXT("Hand-level Quest curl confidence where closed-fist assist begins."));

	TAutoConsoleVariable<float> CVarQuestFingerClosedFistHandAssistFull01(
		TEXT("mp.QuestFingerClosedFistHandAssistFull01"),
		0.75f,
		TEXT("Hand-level Quest curl confidence where closed-fist assist reaches its configured strength."));

	TAutoConsoleVariable<float> CVarQuestFingerCurlProximalScale(
		TEXT("mp.QuestFingerCurlProximalScale"),
		0.82f,
		TEXT("Multiplier for non-thumb proximal/base knuckle curl in Quest curl-only retargeting. Higher values let closed fists leave the grasp pose without over-driving fingertips."));

	TAutoConsoleVariable<float> CVarQuestFingerCurlIntermediateScale(
		TEXT("mp.QuestFingerCurlIntermediateScale"),
		1.00f,
		TEXT("Multiplier for non-thumb intermediate finger curl in Quest curl-only retargeting."));

	TAutoConsoleVariable<float> CVarQuestFingerCurlDistalScale(
		TEXT("mp.QuestFingerCurlDistalScale"),
		0.58f,
		TEXT("Multiplier for non-thumb distal fingertip curl in Quest curl-only retargeting, and the distal/tip segment-direction damping weight in the accepted VR Preview default. Lower values reduce closed-fist self-intersection and fingertip twist."));

	TAutoConsoleVariable<float> CVarQuestThumbMaxCurlDegrees(
		TEXT("mp.QuestThumbMaxCurlDegrees"),
		82.0f,
		TEXT("Maximum per-segment Manny thumb curl applied by Quest curl-only retargeting."));

	TAutoConsoleVariable<int32> CVarQuestThumbUseChainCurl(
		TEXT("mp.QuestThumbUseChainCurl"),
		1,
		TEXT("When non-zero, drives Quest thumb curl from thumb joint-chain bend instead of palm-forward finger direction."));

	TAutoConsoleVariable<float> CVarQuestThumbCurlStrength(
		TEXT("mp.QuestThumbCurlStrength"),
		1.0f,
		TEXT("Additional multiplier for Quest-driven thumb chain curl."));

	TAutoConsoleVariable<float> CVarQuestThumbClosedFistAssist(
		TEXT("mp.QuestThumbClosedFistAssist"),
		0.45f,
		TEXT("Minimum thumb curl borrowed from the non-thumb closed-fist confidence, so fists do not leave the thumb fully upright when Quest thumb joints under-report closure."));

	TAutoConsoleVariable<float> CVarQuestThumbCurlProximalScale(
		TEXT("mp.QuestThumbCurlProximalScale"),
		0.55f,
		TEXT("Multiplier for thumb proximal/base curl in Quest curl-only retargeting."));

	TAutoConsoleVariable<float> CVarQuestThumbCurlIntermediateScale(
		TEXT("mp.QuestThumbCurlIntermediateScale"),
		0.95f,
		TEXT("Multiplier for thumb intermediate curl in Quest curl-only retargeting."));

	TAutoConsoleVariable<float> CVarQuestThumbCurlDistalScale(
		TEXT("mp.QuestThumbCurlDistalScale"),
		0.70f,
		TEXT("Multiplier for thumb distal curl in Quest curl-only retargeting, and the thumb distal/tip segment-direction damping weight in the accepted VR Preview default."));

	TAutoConsoleVariable<int32> CVarQuestHandDebug(
		TEXT("mp.QuestHandDebug"),
		0,
		TEXT("When non-zero, log Quest/OpenXR hand tracker availability, tracking state, and finger-rig binding once per second."));

	TAutoConsoleVariable<int32> CVarQuestWristDebug(
		TEXT("mp.QuestWristDebug"),
		0,
		TEXT("When non-zero, log Quest wrist mapping and arm-solve consumption once per second while the MediaPipe-driven Manny evaluates."));

	TAutoConsoleVariable<int32> CVarQuestWristTrace(
		TEXT("mp.QuestWristTrace"),
		0,
		TEXT("When non-zero, log Quest wrist mapping diagnostics. If wrist position blend is zero, trace mapping without applying it."));

	TAutoConsoleVariable<float> CVarQuestWristTraceLogIntervalSeconds(
		TEXT("mp.QuestWristTraceLogIntervalSeconds"),
		0.25f,
		TEXT("Minimum seconds between Quest wrist solve trace log lines per side while mp.QuestWristTrace is enabled."));

	TAutoConsoleVariable<int32> CVarQuestWristTraceStableBaseline(
		TEXT("mp.QuestWristTraceStableBaseline"),
		1,
		TEXT("When non-zero, trace-only Quest wrist diagnostics use a separate stable baseline so logged wrist deltas show live movement without priming the apply calibration."));

	TAutoConsoleVariable<int32> CVarQuestWristRequireTrackedForApply(
		TEXT("mp.QuestWristRequireTrackedForApply"),
		0,
		TEXT("When non-zero, Quest wrist position can only be applied from hands flagged tracked. Default off because Quest can publish usable joint poses while the tracked flag is transiently false."));

	TAutoConsoleVariable<int32> CVarQuestHandHud(
		TEXT("mp.QuestHandHud"),
		0,
		TEXT("When non-zero, show Quest/OpenXR hand tracking state in the viewport while the MediaPipe-driven Manny evaluates."));

	TAutoConsoleVariable<int32> CVarQuestHandCompare(
		TEXT("mp.QuestHandCompare"),
		0,
		TEXT("Read-only Quest hand comparison: 0=off, 1=log Quest hand-only vs applied avatar hand, 2=draw HMD-relative avatar-space Quest hands, 3=also draw raw Quest hand skeletons in world space."));

	TAutoConsoleVariable<int32> CVarMetaHumanArmSanity(
		TEXT("mp.MetaHumanArmSanity"),
		0,
		TEXT("When non-zero, log whether the posed MetaHuman shoulder/elbow/hand chain actually agrees with the mapped Quest wrist and Quest hand rotation."));

	TAutoConsoleVariable<float> CVarMetaHumanArmSanityLogIntervalSeconds(
		TEXT("mp.MetaHumanArmSanityLogIntervalSeconds"),
		0.25f,
		TEXT("Minimum seconds between MetaHuman arm sanity log lines per side."));

	TAutoConsoleVariable<float> CVarMetaHumanArmSanityMaxWristErrorCm(
		TEXT("mp.MetaHumanArmSanityMaxWristErrorCm"),
		18.0f,
		TEXT("MetaHuman arm sanity fails when the posed hand root is farther than this from the final mapped Quest wrist target."));

	TAutoConsoleVariable<float> CVarMetaHumanArmSanityMaxHandRotErrorDeg(
		TEXT("mp.MetaHumanArmSanityMaxHandRotErrorDeg"),
		75.0f,
		TEXT("MetaHuman arm sanity fails when the Quest expected hand basis diverges from the applied Manny/MetaHuman hand basis by more than this angle."));

	TAutoConsoleVariable<float> CVarMetaHumanArmSanityMaxBasisErrorDeg(
		TEXT("mp.MetaHumanArmSanityMaxBasisErrorDeg"),
		65.0f,
		TEXT("MetaHuman arm sanity fails when Quest-to-avatar hand basis forward/up error exceeds this angle."));

	TAutoConsoleVariable<float> CVarMetaHumanArmSanityMaxLengthErrorCm(
		TEXT("mp.MetaHumanArmSanityMaxLengthErrorCm"),
		8.0f,
		TEXT("MetaHuman arm sanity fails when posed upper/lower arm segment length differs from reference by more than this many cm."));

	TAutoConsoleVariable<float> CVarMetaHumanArmSanityMinElbowBendDeg(
		TEXT("mp.MetaHumanArmSanityMinElbowBendDeg"),
		6.0f,
		TEXT("MetaHuman arm sanity fails when the posed elbow is collapsed below this bend angle."));

	TAutoConsoleVariable<float> CVarMetaHumanArmSanityMaxSwingDeg(
		TEXT("mp.MetaHumanArmSanityMaxSwingDeg"),
		140.0f,
		TEXT("MetaHuman arm sanity fails when the applied Quest wrist swing is at or beyond this many degrees."));

	TAutoConsoleVariable<int32> CVarMediaPipeLegUseBasisRoll(
		TEXT("mp.MediaPipeLegUseBasisRoll"),
		1,
		TEXT("When non-zero, drive MediaPipe thigh/calf roll from a stable leg semantic basis instead of direction-only shortest-arc rotations."));

	TAutoConsoleVariable<int32> CVarMediaPipeLegSolveDebugOnce(
		TEXT("mp.MediaPipeLegSolveDebugOnce"),
		0,
		TEXT("When positive, log that many DriveLegCS input/apply summaries, then decrement. Default off."));

	TAutoConsoleVariable<int32> CVarMediaPipeFootForwardHysteresis(
		TEXT("mp.MediaPipeFootForwardHysteresis"),
		1,
		TEXT("When non-zero, keep MediaPipe foot forward vectors in the same hemisphere frame-to-frame to prevent ankle-to-toe branch flips."));

	TAutoConsoleVariable<int32> CVarMediaPipeDriveFootRotation(
		TEXT("mp.MediaPipeDriveFootRotation"),
		0,
		TEXT("When non-zero, drive Manny foot rotation from MediaPipe ankle-to-toe landmarks. Default off for V2 because MediaPipe foot heading is unreliable in lunges and can twist planted feet."));

	TAutoConsoleVariable<int32> CVarMediaPipeFootPlanarWhenGrounded(
		TEXT("mp.MediaPipeFootPlanarWhenGrounded"),
		1,
		TEXT("When non-zero, project MediaPipe foot forward onto the body/floor plane for near-floor or near-vertical foot samples to avoid planted-foot roll/twist from noisy toe pitch."));

	TAutoConsoleVariable<float> CVarMediaPipeFootPlanarVerticalDotThreshold(
		TEXT("mp.MediaPipeFootPlanarVerticalDotThreshold"),
		0.80f,
		TEXT("MediaPipe foot vectors with abs(dot(foot_forward, body_up)) above this value are planarized when mp.MediaPipeFootPlanarWhenGrounded is enabled."));

	TAutoConsoleVariable<float> CVarMediaPipeFootPlanarMinLength(
		TEXT("mp.MediaPipeFootPlanarMinLength"),
		0.55f,
		TEXT("MediaPipe foot vectors with projected horizontal length below this value reuse the last stable foot heading when mp.MediaPipeFootPlanarWhenGrounded is enabled."));

	TAutoConsoleVariable<float> CVarMediaPipeFootPlanarMaxGroundTurnDeg(
		TEXT("mp.MediaPipeFootPlanarMaxGroundTurnDeg"),
		30.0f,
		TEXT("Maximum accepted frame-to-frame grounded MediaPipe foot heading change in degrees before reusing the last stable heading."));

	TAutoConsoleVariable<int32> CVarMediaPipeFootUnreliableUseTorsoForward(
		TEXT("mp.MediaPipeFootUnreliableUseTorsoForward"),
		0,
		TEXT("When non-zero, use projected torso forward instead of noisy ankle-to-toe heading for unreliable MediaPipe foot samples."));

	TAutoConsoleVariable<float> CVarMediaPipeLegKneeBackwardPoleSuppression(
		TEXT("mp.MediaPipeLegKneeBackwardPoleSuppression"),
		0.0f,
		TEXT("0..1 fraction of an implausible backward MediaPipe knee pole (knee behind the hip-ankle line relative to avatar forward) rotated toward forward/lateral while preserving bend magnitude. Front-facing monocular depth cannot observe knee depth reliably; 0 keeps raw landmarks. Replay-output evaluation enables this."));

	TAutoConsoleVariable<int32> CVarMediaPipeFootGroundedWorldUp(
		TEXT("mp.MediaPipeFootGroundedWorldUp"),
		0,
		TEXT("When non-zero, grounded/near-floor MediaPipe feet build their up axis from world up (the floor) instead of torso up, so squat or lean torso tilt does not roll planted feet. Airborne feet keep following the body. Replay-output evaluation enables this."));

	TAutoConsoleVariable<float> CVarMediaPipeLegScaffoldHmdWeight(
		TEXT("mp.MediaPipeLegScaffoldHmdWeight"),
		0.0f,
		TEXT("0..1 blend weight of the Quest/HMD metric height scaffold in the fused pelvis compression. Monocular MediaPipe supplies squat/stand timing but not metric depth; the HMD height against its rolling standing baseline supplies the metric magnitude. 0 keeps the monocular-only compression. Replay-output evaluation enables this."));

	TAutoConsoleVariable<float> CVarMediaPipeLegScaffoldFlexionWeight(
		TEXT("mp.MediaPipeLegScaffoldFlexionWeight"),
		0.0f,
		TEXT("0..1 strength of the grounded-leg metric flexion correction. Solves, on the avatar's own thigh/calf lengths, the knee flexion that realizes the fused scaffold pelvis drop and rotates the MediaPipe segment directions inside their own bend plane by a bounded fraction of the difference. 0 keeps raw MediaPipe flexion. Replay-output evaluation enables this."));

	TAutoConsoleVariable<float> CVarMediaPipeLegScaffoldFlexionMaxAdjustDeg(
		TEXT("mp.MediaPipeLegScaffoldFlexionMaxAdjustDeg"),
		25.0f,
		TEXT("Maximum degrees the grounded-leg flexion correction may add to or remove from the measured MediaPipe knee flexion. Bounds the metric correction so MediaPipe leg intent always remains the primary motion source."));

	TAutoConsoleVariable<float> CVarMediaPipeLegScaffoldHipFromHmdRatio(
		TEXT("mp.MediaPipeLegScaffoldHipFromHmdRatio"),
		0.52f,
		TEXT("Standing pelvis height as a fraction of the standing HMD height. Only normalizes the metric head drop into a dimensionless compression alpha; it never scales avatar bones."));

	TAutoConsoleVariable<float> CVarMediaPipeLegScaffoldLeanCoefficient(
		TEXT("mp.MediaPipeLegScaffoldLeanCoefficient"),
		0.35f,
		TEXT("Head-to-hip lever arm for HMD lean compensation, as a fraction of the standing HMD height. Discounts HMD height loss caused by leaning/bowing (observed via the MediaPipe torso up axis) so a forward lean is not mistaken for a squat. 0 disables lean compensation."));

	TAutoConsoleVariable<float> CVarMediaPipeLegScaffoldBaselineWindowSeconds(
		TEXT("mp.MediaPipeLegScaffoldBaselineWindowSeconds"),
		45.0f,
		TEXT("Rolling window, in seconds, over which the standing HMD height baseline is tracked as a per-slot maximum. Squats cannot drag the baseline down; transient inflation (toe raises) expires with the window."));

	TAutoConsoleVariable<float> CVarMediaPipeLegScaffoldShinTiltShare(
		TEXT("mp.MediaPipeLegScaffoldShinTiltShare"),
		0.35f,
		TEXT("Natural share (0..1) of the total knee flexion carried by the shin's in-plane tilt from vertical for grounded legs. Front-facing monocular capture cannot observe the femur's forward (depth) rotation and dumps most of the bend into the shin, which sinks the knee; the redistribution rotates the bend back toward this split."));

	TAutoConsoleVariable<float> CVarMediaPipeLegScaffoldBendRedistributionWeight(
		TEXT("mp.MediaPipeLegScaffoldBendRedistributionWeight"),
		0.0f,
		TEXT("0..1 strength of the grounded-leg bend redistribution (femur/shin split correction). Rigidly rotates the thigh+calf pair inside their own bend plane so the shin keeps at most mp.MediaPipeLegScaffoldShinTiltShare of the total flexion; flexion magnitude, bend plane, and timing stay owned by the MediaPipe intent. 0 keeps the raw monocular split. Replay-output evaluation enables this."));

	TAutoConsoleVariable<float> CVarMediaPipeLegScaffoldBendRedistributionMaxDeg(
		TEXT("mp.MediaPipeLegScaffoldBendRedistributionMaxDeg"),
		20.0f,
		TEXT("Maximum degrees the grounded-leg bend redistribution may rotate the leg chain inside its bend plane. Bounds the correction (and the planted-foot planar drift it can introduce)."));

	TAutoConsoleVariable<int32> CVarMediaPipeFootGroundedPitchClamp(
		TEXT("mp.MediaPipeFootGroundedPitchClamp"),
		0,
		TEXT("When non-zero, grounded/near-floor feet rebuild their pitch from the monocular heel-toe axis clamped to [reference slope - extra down, reference slope]. The reference foot basis maps a planarized (horizontal) forward to a toe-up foot with the ankle sunk to ball height; this keeps grounded soles flat on the floor while heel raises keep their downward pitch. Replay-output evaluation enables this."));

	TAutoConsoleVariable<float> CVarMediaPipeFootGroundedMaxExtraDownPitchDeg(
		TEXT("mp.MediaPipeFootGroundedMaxExtraDownPitchDeg"),
		30.0f,
		TEXT("Extra downward foot pitch allowed past the reference flat-contact slope while grounded, so heel raises and toe stands keep working under the grounded foot pitch clamp."));

	TAutoConsoleVariable<int32> CVarMediaPipeFootGroundedBlend(
		TEXT("mp.MediaPipeFootGroundedBlend"),
		0,
		TEXT("When non-zero, the grounded foot-pitch solve fades continuously between the solved and raw pitch by height above the observed floor (time-smoothed), instead of gating binarily on the near-floor flag. The binary gate snaps a lunge's foreshortened back foot when its noisy height jitters around the release threshold (measured live 2026-06-13). Default off to keep replay evaluation byte-stable; the live lower-body trial layer enables it."));

	TAutoConsoleVariable<float> CVarMediaPipeFootGroundedBlendHalfLife(
		TEXT("mp.MediaPipeFootGroundedBlendHalfLife"),
		0.12f,
		TEXT("Temporal smoothing half-life (seconds) for the mp.MediaPipeFootGroundedBlend ground-contact factor while GROUNDING (blend increasing). Kept short so real planting feels instant."));

	TAutoConsoleVariable<float> CVarMediaPipeFootGroundedBlendReleaseHalfLife(
		TEXT("mp.MediaPipeFootGroundedBlendReleaseHalfLife"),
		0.35f,
		TEXT("Temporal smoothing half-life (seconds) for the mp.MediaPipeFootGroundedBlend factor while RELEASING (blend decreasing). Longer than the grounding half-life so upward landmark noise spikes (measured 3-5x worse on the camera-far foot, 2026-06-13) cannot snap a planted foot to the raw pitch; a genuine lift keeps rising and takes the handover after this delay."));

	TAutoConsoleVariable<int32> CVarMediaPipeFootForwardSmoothing(
		TEXT("mp.MediaPipeFootForwardSmoothing"),
		0,
		TEXT("When non-zero, the applied foot-forward direction is rate-limited and eased, so the instantaneous switches between the foot-forward fallback sources (raw ankle-toe, last stable heading, torso forward - the residual lunge back-foot snap, measured 2026-06-13) become physically continuous turns. Default off to keep replay evaluation byte-stable; the live lower-body trial layer enables it."));

	TAutoConsoleVariable<float> CVarMediaPipeFootForwardSmoothingHalfLife(
		TEXT("mp.MediaPipeFootForwardSmoothingHalfLife"),
		0.10f,
		TEXT("Ease half-life (seconds) for mp.MediaPipeFootForwardSmoothing."));

	TAutoConsoleVariable<float> CVarMediaPipeFootForwardMaxTurnDegPerSec(
		TEXT("mp.MediaPipeFootForwardMaxTurnDegPerSec"),
		360.0f,
		TEXT("Hard turn-rate limit (degrees/second) for the applied foot-forward direction under mp.MediaPipeFootForwardSmoothing. Real foot pivots stay under this; source-switch snaps do not."));

	TAutoConsoleVariable<int32> CVarMediaPipeFootHeadingClamp(
		TEXT("mp.MediaPipeFootHeadingClamp"),
		0,
		TEXT("When non-zero, the applied foot-forward planar heading is clamped into a band around the wearer's torso heading. A foot attached to a body cannot yaw freely, so this makes propeller spins and heading snaps geometrically impossible regardless of landmark noise (a rate limiter alone chases sign-flipping targets into continuous rotation - observed live 2026-06-13). Default off to keep replay evaluation byte-stable; the live lower-body trial layer enables it."));

	TAutoConsoleVariable<float> CVarMediaPipeFootHeadingClampMaxDeg(
		TEXT("mp.MediaPipeFootHeadingClampMaxDeg"),
		50.0f,
		TEXT("Half-width (degrees) of the allowed foot heading band around the torso heading for mp.MediaPipeFootHeadingClamp. Normal stances, lunges, and moderate toe-out fit comfortably; extreme sideways foot poses clamp."));

	TAutoConsoleVariable<int32> CVarMediaPipeLegSagittalRepitch(
		TEXT("mp.MediaPipeLegSagittalRepitch"),
		0,
		TEXT("When non-zero, re-pitch each leg segment so its elevation matches the measured landmark vertical delta over a depth-robust decaying-minimum segment length. Monocular depth noise inflates the apparent length of a segment pointing at the camera, which makes raised knees read low (observed live 2026-07-02). Default off to keep replay evaluation byte-stable; the live lower-body trial layer enables it."));

	TAutoConsoleVariable<float> CVarMediaPipeLegSagittalLengthDecayPerSec(
		TEXT("mp.MediaPipeLegSagittalLengthDecayPerSec"),
		0.03f,
		TEXT("Upward re-adaptation rate (fraction/second) of the decaying-minimum segment length used by mp.MediaPipeLegSagittalRepitch."));

	TAutoConsoleVariable<int32> CVarMediaPipeLegAdductionClamp(
		TEXT("mp.MediaPipeLegAdductionClamp"),
		0,
		TEXT("When non-zero, bound each thigh's travel past vertical toward the body midline. With the reliability stabilizer off, monocular drift walks the knees into each other (observed live 2026-07-02); the anatomical bound stops that without damping any other motion. Abduction (outward) is never limited. Default off to keep replay evaluation byte-stable; the live lower-body trial layer enables it."));

	TAutoConsoleVariable<float> CVarMediaPipeLegAdductionMaxDeg(
		TEXT("mp.MediaPipeLegAdductionMaxDeg"),
		10.0f,
		TEXT("Maximum thigh adduction (degrees past vertical toward the midline) allowed by mp.MediaPipeLegAdductionClamp."));

	TAutoConsoleVariable<float> CVarMediaPipeArmTorsoGuardCm(
		TEXT("mp.MediaPipeArmTorsoGuardCm"),
		0.0f,
		TEXT("Minimum horizontal distance (cm) the driven elbow/wrist targets may approach the torso axis. The stack had no non-penetration constraint: take-3 referee forensics 2026-07-05 measured the fused right wrist 2.0cm from the spine axis (inside the body) while the offline reference never dipped below 14.8cm. 0 disables (byte-stable default); the live trial layer sets a torso radius."));

	TAutoConsoleVariable<float> CVarMediaPipeArmDirectionFromCamera(
		TEXT("mp.MediaPipeArmDirectionFromCamera"),
		0.0f,
		TEXT("Weight (0-1) for transplanting the camera's arm DIRECTIONS (shoulder->elbow and shoulder->wrist unit vectors) onto the Quest chain's segment lengths while the chain drives the arm. The chain synthesizes hanging arms too wide: elbow/wrist 22-24cm horizontal of the shoulder vs the camera's 7-10cm; the fused avatar hung its upper arms at 34-45 deg from vertical vs the offline referee's 15-21 (take-3 MHA referee 2026-07-05, the user's most-repeated visual report). Directions are shoulder-relative per source so frame translation bias cancels. Reliability-gated with a continuously eased blend; supersedes mp.MediaPipeArmElbowSwivelFromCamera (auto-disabled while this is active). 0 preserves historical behavior (byte-stable default); the live trial layer enables it."));

	TAutoConsoleVariable<float> CVarMediaPipeArmDirectionFromCameraMaxDeg(
		TEXT("mp.MediaPipeArmDirectionFromCameraMaxDeg"),
		20.0f,
		TEXT("Magnitude clamp (degrees) on the mp.MediaPipeArmDirectionFromCamera per-segment correction rotations. The corrector was validated to erase a STANDING ~15-20deg hanging-arm bias, but it had no bound: during active movement the 0.8s learner integrated transient camera-vs-chain latency disagreements into 69-99deg corrections that wandered 63-93deg per 10s and displaced the wrist up to 65cm from the chain pose (mp.ArmDirCorrection / mp.ArmJumpTrace, 2026-07-10 worn session: 27 of 34 traced arm jumps named this stage). The clamp bounds the correction to its validated purpose; learning is additionally gated to quiet-arm frames in the solve."));

	TAutoConsoleVariable<float> CVarMediaPipeChainReachFromQuestHand(
		TEXT("mp.MediaPipeChainReachFromQuestHand"),
		0.0f,
		TEXT("Weight (0-1) for extending the full-arm-chain wrist target to the REAL Quest hand-tracking wrist's reach while the chain drives the arm. The chain retargeter rebuilds the arm from the body-tracking chain's segment DIRECTIONS at the avatar's fixed segment lengths, and the synthesized elbow never fully straightens - rendered reach saturates ~41-46cm (max 52) while the avatar can reach ~52cm, so full extensions read as bent arms (worn verdict 2026-07-10: 'arms are not extending fully like they used to'). The real hand-tracking wrist distance from the chain's own shoulder, over the chain's own segment-length sum, gives the user's true straightness fraction; the wrist target is extended radially to that fraction of the avatar's full reach (stretch-only, smoothed, decays on hand loss) and the elbow re-solves with the two-bone cosine rule in the existing swivel plane. Replaces what the old quest-wrist solve's reach-scale calibration did before the chain path gated it off. 0 preserves historical behavior (byte-stable default); the candidate settings variant enables it."));

	TAutoConsoleVariable<float> CVarMediaPipeArmElbowSwivelFromCamera(
		TEXT("mp.MediaPipeArmElbowSwivelFromCamera"),
		0.0f,
		TEXT("Weight (0-1) for correcting the Quest arm chain's elbow SWIVEL (azimuth about the shoulder->wrist chord) toward the camera's measured elbow direction while the chain drives the arm. The Quest chain synthesizes elbows flared outward: measured 14.0cm off the shoulder-wrist chord at quiet standing vs the camera's 5.3cm and the offline reference's 8.3cm (take-3 MHA referee, 2026-07-05). Wrist and shoulder stay Quest-owned; only the elbow's swing direction adopts the camera's reading, reliability-gated and smoothed. 0 preserves historical behavior (byte-stable default); the live trial layer enables it."));

	TAutoConsoleVariable<float> CVarMediaPipeFootFloorWindowSeconds(
		TEXT("mp.MediaPipeFootFloorWindowSeconds"),
		0.0f,
		TEXT("When > 0, the per-foot observed source floor is the minimum over this many recent seconds (bucketed) instead of the all-time running minimum. The running min is monotonically corrupted by a single downward depth-noise spike: after one bad sample the standing feet read 'lifted' forever, grounded/plant never latch again, and the un-anchored feet snap and slide (MHA-referee take-3 forensics 2026-07-05: liftCm 3.6-6.4 with grounded=0 on both feet during quiet standing). A windowed floor learns downward instantly and lets outliers age out. 0 preserves the historical running-min (byte-stable default); the live trial layer sets a window. Requires the keyed foot-contact state."));

	TAutoConsoleVariable<int32> CVarMediaPipePelvisHmdAnchor(
		TEXT("mp.MediaPipePelvisHmdAnchor"),
		0,
		TEXT("When non-zero, the planar (XY) pelvis translation is slow-corrected toward the drift-free Quest HMD anchor: the pelvis<->HMD planar offset is latched once at live-neutral settle, then a low-passed correction erases sustained camera drift while high-frequency camera motion (sways, steps, squats) passes through. Take-4 referee forensics 2026-07-06: the camera pelvis drifted laterally (+5cm growing to +7cm over 200s vs the Epic solve's 0.8cm) and the closed-loop HMD lean tilted the torso to keep the head under the stationary HMD - the user saw a leaning avatar. Adaptation freezes while the wearer is not upright, so genuine bends are not corrected away. 0 preserves historical behavior (byte-stable default); the candidate settings variant enables it."));

	TAutoConsoleVariable<float> CVarMediaPipePelvisHmdAnchorHalfLifeSeconds(
		TEXT("mp.MediaPipePelvisHmdAnchorHalfLifeSeconds"),
		6.0f,
		TEXT("Half-life (seconds) of the mp.MediaPipePelvisHmdAnchor drift-correction low-pass. Long enough that stepping and swaying pass through untouched; short enough to erase multi-second camera drift."));

	TAutoConsoleVariable<float> CVarMediaPipePelvisHmdAnchorMaxCm(
		TEXT("mp.MediaPipePelvisHmdAnchorMaxCm"),
		25.0f,
		TEXT("Magnitude clamp (cm) on the mp.MediaPipePelvisHmdAnchor planar correction; bounds the damage if the anchor latches during a bad neutral."));

	TAutoConsoleVariable<float> CVarMediaPipePelvisHmdAnchorUprightMaxCm(
		TEXT("mp.MediaPipePelvisHmdAnchorUprightMaxCm"),
		35.0f,
		TEXT("Planar HMD-to-pelvis distance (cm) below which the wearer counts as upright for mp.MediaPipePelvisHmdAnchor adaptation. Bends/leans beyond this hold the last correction instead of learning the pose as drift."));

	TAutoConsoleVariable<float> CVarQuestWristPalmTrimLeftDeg(
		TEXT("mp.QuestWristPalmTrimLeftDeg"),
		0.0f,
		TEXT("Constant corrective twist (degrees, about the forearm axis) applied to the LEFT hand's final Quest-driven rotation. Compensates the constant palm retarget bias observed against the Epic offline solve (take-3/4 referee: fused palms read ~25deg rotated; user-visible as the wrist angling outward on forward points). 0 disables (byte-stable default); fitted per avatar from rotation captures."));

	TAutoConsoleVariable<float> CVarQuestWristPalmTrimRightDeg(
		TEXT("mp.QuestWristPalmTrimRightDeg"),
		0.0f,
		TEXT("Constant corrective twist (degrees, about the forearm axis) applied to the RIGHT hand's final Quest-driven rotation. See mp.QuestWristPalmTrimLeftDeg."));

	TAutoConsoleVariable<int32> CVarMediaPipeClavicleShrugDirect(
		TEXT("mp.MediaPipeClavicleShrugDirect"),
		0,
		TEXT("When non-zero, the clavicle shrug is driven BY GEOMETRY from the camera's metric shoulder lift: measured lift (vs an asymmetric rest reference that adapts down fast / up over minutes, so held shrugs cannot absorb into the baseline) scaled to the rig via the shoulder-width ratio, converted to the clavicle direction weight whose rotation reproduces that rise. Replaces the weights-of-evidence path's ceiling (its 0.25 direction clamp caps the avatar at ~2cm of rise; take-4 referee 2026-07-06: camera saw 7.7cm shrugs, avatar produced 1.5-2 vs the Epic solve's 11.5 at Kellan scale). The Quest chain cannot see shrugs at all (synthesized shoulders, 1.5cm amplitude measured) - the camera is the only shrug source. 0 preserves historical behavior (byte-stable default); the candidate settings variant enables it."));

	TAutoConsoleVariable<int32> CVarMediaPipeBodyYawFromCamera(
		TEXT("mp.MediaPipeBodyYawFromCamera"),
		0,
		TEXT("When non-zero, a low-passed closed-loop correction pulls the applied body yaw onto the camera's observed shoulder line. The Quest-derived yaw carries a constant bias plus slow drift (take-4 round-3 referee 2026-07-06: +6deg at start growing to +10deg vs the Epic solve - the wearer sees the chest progressively turning away); the camera observes the true torso heading every frame. Quest keeps owning fast turns; the correction only erases sustained error - same complementary architecture as the arm-direction and pelvis-anchor corrections. 0 preserves historical behavior (byte-stable default); the candidate settings variant enables it."));

	TAutoConsoleVariable<float> CVarMediaPipeBodyYawFromCameraHalfLifeSeconds(
		TEXT("mp.MediaPipeBodyYawFromCameraHalfLifeSeconds"),
		8.0f,
		TEXT("Half-life (seconds) of the mp.MediaPipeBodyYawFromCamera correction low-pass. Long enough that deliberate quick turns stay Quest-owned; short enough to erase multi-second heading drift."));

	TAutoConsoleVariable<float> CVarMediaPipeBodyYawFromCameraMaxDeg(
		TEXT("mp.MediaPipeBodyYawFromCameraMaxDeg"),
		25.0f,
		TEXT("Magnitude clamp (degrees) on the mp.MediaPipeBodyYawFromCamera correction; bounds the damage if the camera torso basis misreads (heavy occlusion, profile views)."));

	TAutoConsoleVariable<int32> CVarMediaPipeKneeMedialBowClamp(
		TEXT("mp.MediaPipeKneeMedialBowClamp"),
		0,
		TEXT("When non-zero, clamp each knee's MEDIAL (toward-midline) bow past the hip->ankle line. Monocular depth noise bows the knee vertex inward - the knock-kneed look - even when the thigh direction is within its adduction bound (observed live 2026-07-02). Real knees essentially never cave medially past the line; outward bowing stays free. Default off to keep replay evaluation byte-stable; the live lower-body trial layer enables it."));

	TAutoConsoleVariable<float> CVarMediaPipeKneeMedialBowMaxDeg(
		TEXT("mp.MediaPipeKneeMedialBowMaxDeg"),
		5.0f,
		TEXT("Maximum medial knee bow (degrees past the hip->ankle line) allowed by mp.MediaPipeKneeMedialBowClamp."));

	TAutoConsoleVariable<int32> CVarMediaPipeArmOverheadRescue(
		TEXT("mp.MediaPipeArmOverheadRescue"),
		0,
		TEXT("When non-zero, the camera takes an arm whose Quest hand is untracked while MediaPipe reliably sees the wrist above the shoulder. Overhead hands leave the headset cameras' view; Quest body tracking keeps SYNTHESIZING a guess that sags the arms down even though the phone measures them (observed live 2026-07-02). Dwell-time hysteresis prevents source flapping. Default off to keep replay evaluation byte-stable; the live lower-body trial layer enables it."));

	TAutoConsoleVariable<float> CVarMediaPipeArmOverheadRescueMinReliability(
		TEXT("mp.MediaPipeArmOverheadRescueMinReliability"),
		0.35f,
		TEXT("Minimum MediaPipe shoulder/elbow/wrist landmark reliability required before the overhead arm rescue may take an arm the Quest still partially tracks. Overhead landmarks at the top of frame measure 0.2-0.4 (2026-07-02); when the Quest side is FULLY gone this floor does not apply."));

	TAutoConsoleVariable<float> CVarMediaPipeArmOverheadRescueWristAboveShoulderCm(
		TEXT("mp.MediaPipeArmOverheadRescueWristAboveShoulderCm"),
		10.0f,
		TEXT("How far above the shoulder (cm) the MediaPipe wrist must be for the overhead arm rescue - it only fires in the region where the headset genuinely cannot see the hands."));

	TAutoConsoleVariable<float> CVarMediaPipeArmOverheadRescueDivergenceCm(
		TEXT("mp.MediaPipeArmOverheadRescueDivergenceCm"),
		30.0f,
		TEXT("Vertical divergence (MediaPipe wrist ABOVE the Quest chain's wrist, cm) that fires the overhead arm rescue even while the Quest still claims the hand is tracked. Measured 2026-07-02: overhead hands sag on the Quest side with questTracked=1 throughout - the tracked flag cannot be trusted; the divergence measures the error directly."));

	TAutoConsoleVariable<float> CVarMediaPipeBodyYawMaxDeg(
		TEXT("mp.MediaPipeBodyYawMaxDeg"),
		100.0f,
		TEXT("Body-tracking yaw range (deg) the pelvis may follow from the Quest hips heading. The historical +/-100 clamp made FULL TURNS impossible: the wearer turns around, the avatar stops at 100 deg (MHA-referee take 2, 2026-07-04: hip-yaw RMSE 17-27 deg in turn windows; Epic follows the full turn). The live trial layer raises this to 720 with delta-accumulated yaw so multi-turn continuity works past the +/-180 wrap. 100 preserves the verified historical behavior."));

	TAutoConsoleVariable<float> CVarMediaPipeArmOverheadRescueChainAboveVetoCm(
		TEXT("mp.MediaPipeArmOverheadRescueChainAboveVetoCm"),
		0.0f,
		TEXT("When > 0: veto overhead-rescue latch ENTRY on the hand-untracked clause while the Quest arm chain is still FRESH and its wrist sits at least this many cm ABOVE the MediaPipe wrist. MHA-referee forensics 2026-07-04 (Take 1, early-take windows): the hand flag dropped while the fresh chain was 34-57 cm above the camera wrist, the rescue trusted the camera, and the camera was the wrong one (left wrist 25->52 cm vs the offline solve). Does not touch the divergence trigger (camera ABOVE chain) or the fully-gone path (requires a stale chain). 0 disables (byte-stable default); the live trial layer sets it. AWAITING WORN-HEADSET VERDICT."));

	TAutoConsoleVariable<int32> CVarMediaPipeArmRescueShoulderRelDivergence(
		TEXT("mp.MediaPipeArmRescueShoulderRelDivergence"),
		0,
		TEXT("When non-zero and the Quest arm chain is FRESH, the rescue's divergence trigger compares SHOULDER-RELATIVE wrist heights (each wrist minus its own source's shoulder) instead of absolute Z. The absolute compare spans two coordinate frames whose origins do not agree - measured 2026-07-04 take-2 parity: a constant ~-90 cm camera-below-chain bias while the sources visibly agreed, so the camera-above-chain trigger could NEVER fire and a 6 s chain dropout (chain held the raised left arm 45 cm down, camera reliability 0.9) went unrescued. Shoulder-relative differencing cancels any translation bias. A shoulder-relative divergence trigger also bypasses the wrist-above-shoulder overhead gate: the measured dropout happened at shoulder height, not overhead. 0 preserves the verified historical behavior (byte-stable default); the live trial layer enables it. AWAITING WORN-HEADSET VERDICT."));

	TAutoConsoleVariable<int32> CVarMediaPipeLegScaffoldAsymmetricFlexion(
		TEXT("mp.MediaPipeLegScaffoldAsymmetricFlexion"),
		0,
		TEXT("When non-zero, the scaffold's pelvis-drop flexion correction is distributed by each leg's MEASURED bend share instead of equally. A lunge drops the HMD like a squat but bends asymmetrically; equal distribution bends the straight back leg and turns lunges into squats (observed live 2026-06-13). The camera's per-leg bend is the intent signal; squats stay symmetric automatically. Default off to keep replay evaluation byte-stable; the live lower-body trial layer enables it."));

	TAutoConsoleVariable<int32> CVarQuestVrTrackingPanel(
		TEXT("mp.QuestVrTrackingPanel"),
		0,
		TEXT("When non-zero, the embodied pawn keeps a world-space tracking panel floating to the right of the headset view showing the live camera preview with the tracked-bone skeleton overlay. mp.StartLiveLowerBodyTrial enables this."));

	TAutoConsoleVariable<int32> CVarMediaPipeDriveHmdHead(
		TEXT("mp.MediaPipeDriveHmdHead"),
		0,
		TEXT("When non-zero, drive the avatar head bone from the live Quest HMD rotation: pitch/roll directly, yaw against a self-calibrating neutral so sustained body turns recenter while glances read as head yaw. Skipped during dataset replay and fused-pose evaluation. mp.StartLiveLowerBodyTrial enables this."));

	TAutoConsoleVariable<int32> CVarMediaPipeHmdHeadMirror(
		TEXT("mp.MediaPipeHmdHeadMirror"),
		0,
		TEXT("When non-zero, negate the HMD-driven head yaw/roll. The embodied (driven) avatar carries TRUE rotations - the self-view mirror copy already mirrors via its mirror scale, and the pelvis body-yaw drive composes with the true head yaw to track the wearer's absolute orientation - so this stays off unless a specific setup reads inverted. Pitch is never mirrored."));

	TAutoConsoleVariable<float> CVarMediaPipeHmdHeadYawNeutralHalfLife(
		TEXT("mp.MediaPipeHmdHeadYawNeutralHalfLife"),
		8.0f,
		TEXT("Half-life in seconds for the HMD head-yaw neutral. Shorter recenters faster after body turns; longer holds glances as head yaw for longer."));

	TAutoConsoleVariable<int32> CVarMediaPipeDriveHmdLean(
		TEXT("mp.MediaPipeDriveHmdLean"),
		0,
		TEXT("When non-zero, lean the avatar pelvis/torso from the live HMD's planar displacement against a self-calibrating neutral: leaning back/forward/sideways moves the head metrically and the body follows, so the first-person camera no longer drifts off the avatar's chest. Skipped during dataset replay, fused-pose evaluation, and when spine drive owns the pelvis. mp.StartLiveLowerBodyTrial enables this."));

	TAutoConsoleVariable<int32> CVarMediaPipeDriveHipTwist(
		TEXT("mp.MediaPipeDriveHipTwist"),
		0,
		TEXT("When non-zero, twist the avatar pelvis yaw from the MediaPipe hip-line direction against a self-calibrating neutral, bounded by mp.MediaPipeHipTwistMaxDeg. Skipped during dataset replay, fused-pose evaluation, and when spine drive owns the pelvis. mp.StartLiveLowerBodyTrial enables this."));

	TAutoConsoleVariable<int32> CVarMediaPipeLivePoseMirror(
		TEXT("mp.MediaPipeLivePoseMirror"),
		0,
		TEXT("When non-zero, negate the camera-observed hip-twist residual. The embodied (driven) avatar should carry TRUE rotations - the self-view mirror copy already mirrors via its mirror scale - so this stays off unless the twist direction reads inverted in a specific camera setup."));

	TAutoConsoleVariable<float> CVarMediaPipeLivePoseNeutralHalfLife(
		TEXT("mp.MediaPipeLivePoseNeutralHalfLife"),
		10.0f,
		TEXT("Half-life in seconds for the live lean/twist neutrals (HMD planar position and hip-line yaw). Shorter recenters faster after the wearer walks or turns; longer holds leans and twists for longer."));

	TAutoConsoleVariable<float> CVarMediaPipeHmdLeanMaxDeg(
		TEXT("mp.MediaPipeHmdLeanMaxDeg"),
		35.0f,
		TEXT("Maximum torso lean in degrees the HMD-displacement lean drive may apply in any direction."));

	TAutoConsoleVariable<float> CVarMediaPipeHipTwistMaxDeg(
		TEXT("mp.MediaPipeHipTwistMaxDeg"),
		50.0f,
		TEXT("Maximum pelvis yaw twist in degrees the hip-line twist drive may apply in either direction."));

	TAutoConsoleVariable<float> CVarMediaPipeHmdLeanEyeBackCm(
		TEXT("mp.MediaPipeHmdLeanEyeBackCm"),
		12.0f,
		TEXT("How far behind the HMD position (along its view direction) the closed-loop lean places the avatar's head bone. The camera sits at the eyes, in front of the head bone; without this pull-back a deep bend rotates the face/chest through the camera."));

	TAutoConsoleVariable<int32> CVarMediaPipeLegReliabilityStabilize(
		TEXT("mp.MediaPipeLegReliabilityStabilize"),
		0,
		TEXT("When non-zero, ease the leg segment directions toward the avatar's reference stance as the camera's leg-landmark reliability degrades (subject near frame edge, occlusion, phone movement), instead of following held or drifting landmarks. mp.StartLiveLowerBodyTrial enables this."));

	TAutoConsoleVariable<int32> CVarMediaPipeLegScaffoldLog(
		TEXT("mp.MediaPipeLegScaffoldLog"),
		0,
		TEXT("When non-zero, emit throttled mp.MediaPipeLegScaffold rows per driven actor with the source contributions of the lower-body solve: HMD scaffold (height, baseline, drop, lean compensation, alpha, confidence), monocular compression, fused pelvis compression, per-leg flexion intent vs metric target vs applied delta, foot contact state, and root grounding offset. Replay-output evaluation enables this."));

	TAutoConsoleVariable<float> CVarMediaPipeLegScaffoldLogInterval(
		TEXT("mp.MediaPipeLegScaffoldLogInterval"),
		2.0f,
		TEXT("Seconds between mp.MediaPipeLegScaffold diagnostic rows per anim node instance."));

	TAutoConsoleVariable<int32> CVarMediaPipeDriveArmTwistBones(
		TEXT("mp.MediaPipeDriveArmTwistBones"),
		0,
		TEXT("When non-zero, drive Manny arm twist helper bones, if present, from the solved target parent/source chain. Auto Quest profile 4 enables the Oculus-style axis-only helper interpolation path by default; direct Quest wrist-roll helper ownership remains separate."));

	TAutoConsoleVariable<int32> CVarMediaPipeDriveMetaHumanArmHelpers(
		TEXT("mp.MediaPipeDriveMetaHumanArmHelpers"),
		0,
		TEXT("When non-zero, also drive MetaHuman arm sidecar deformation helpers from the mapped parent/source chain using the same Oculus-style unmapped-helper interpolation as twist bones."));

	TAutoConsoleVariable<float> CVarMediaPipeUpperArmTwistWeight(
		TEXT("mp.MediaPipeUpperArmTwistWeight"),
		0.0f,
		TEXT("Fraction of inferred MediaPipe upper-arm twist to apply. Default 0 uses swing-only shoulders because monocular pose does not reliably observe upper-arm roll."));

	TAutoConsoleVariable<float> CVarMediaPipeLowerArmTwistWeight(
		TEXT("mp.MediaPipeLowerArmTwistWeight"),
		0.0f,
		TEXT("Fraction of inferred MediaPipe lower-arm twist to apply. Default 0 uses swing-only forearms because monocular pose does not reliably observe forearm roll."));

	TAutoConsoleVariable<float> CVarMediaPipeUpperArmTwistClampDegrees(
		TEXT("mp.MediaPipeUpperArmTwistClampDegrees"),
		75.0f,
		TEXT("Clamp for inferred MediaPipe upper-arm twist in degrees."));

	TAutoConsoleVariable<float> CVarMediaPipeLowerArmTwistClampDegrees(
		TEXT("mp.MediaPipeLowerArmTwistClampDegrees"),
		90.0f,
		TEXT("Clamp for inferred MediaPipe lower-arm twist in degrees."));

	TAutoConsoleVariable<int32> CVarMediaPipeArmUseElbowPlaneRoll(
		TEXT("mp.MediaPipeArmUseElbowPlaneRoll"),
		0,
		TEXT("When non-zero, direct MediaPipe arms use the measured elbow plane to choose upper/lower arm roll instead of shortest-arc swing only. Experimental V2 shoulder-roll trial."));

	TAutoConsoleVariable<float> CVarMediaPipeArmElbowPlaneMinSin(
		TEXT("mp.MediaPipeArmElbowPlaneMinSin"),
		0.15f,
		TEXT("Minimum sin(angle) between MediaPipe upper and lower arm segments before trusting the elbow plane for arm roll."));

	TAutoConsoleVariable<float> CVarMediaPipeArmTargetHalfLife(
		TEXT("mp.MediaPipeArmTargetHalfLife"),
		0.14f,
		TEXT("Smoothing half-life in seconds for MediaPipe arm IK target/pole tracking. Higher values reduce live jitter at the cost of lag."));

	TAutoConsoleVariable<float> CVarMediaPipeArmRotationHalfLife(
		TEXT("mp.MediaPipeArmRotationHalfLife"),
		0.16f,
		TEXT("Smoothing half-life in seconds for MediaPipe upper/lower arm rotations. Higher values reduce live shoulder roll/jitter at the cost of lag."));

	TAutoConsoleVariable<int32> CVarMediaPipeArmReliabilityGate(
		TEXT("mp.MediaPipeArmReliabilityGate"),
		1,
		TEXT("When non-zero, reject low-confidence or implausibly jumping MediaPipe arm samples and reuse the last reliable arm target."));

	TAutoConsoleVariable<float> CVarMediaPipeArmMinReliability(
		TEXT("mp.MediaPipeArmMinReliability"),
		0.65f,
		TEXT("Minimum MediaPipe reliability required for shoulder/elbow/wrist samples before they can update an arm target."));

	TAutoConsoleVariable<float> CVarMediaPipeArmMaxElbowStepCm(
		TEXT("mp.MediaPipeArmMaxElbowStepCm"),
		35.0f,
		TEXT("Maximum accepted source elbow movement per published sample before the arm gate holds the previous reliable target. 0 disables this check."));

	TAutoConsoleVariable<float> CVarMediaPipeArmMaxWristStepCm(
		TEXT("mp.MediaPipeArmMaxWristStepCm"),
		55.0f,
		TEXT("Maximum accepted source wrist movement per published sample before the arm gate holds the previous reliable target. 0 disables this check."));

	TAutoConsoleVariable<float> CVarMediaPipeArmMaxSegmentLengthDeltaFraction(
		TEXT("mp.MediaPipeArmMaxSegmentLengthDeltaFraction"),
		0.35f,
		TEXT("Maximum accepted frame-to-frame fractional upper/lower arm segment-length change before the arm gate holds the previous reliable target. 0 disables this check."));

	TAutoConsoleVariable<float> CVarMediaPipeArmMaxSegmentLengthDeltaCm(
		TEXT("mp.MediaPipeArmMaxSegmentLengthDeltaCm"),
		18.0f,
		TEXT("Minimum absolute tolerance for accepted source upper/lower arm segment-length change before the arm gate holds the previous reliable target."));

	TAutoConsoleVariable<float> CVarMediaPipeArmRejectedSampleAlpha(
		TEXT("mp.MediaPipeArmRejectedSampleAlpha"),
		0.12f,
		TEXT("Blend toward a rejected MediaPipe arm sample while holding the previous reliable target. 0 hard-holds; small values reduce occlusion jumps without freezing forever."));

	TAutoConsoleVariable<int32> CVarMediaPipeArmHoldOnQuestHandLoss(
		TEXT("mp.MediaPipeArmHoldOnQuestHandLoss"),
		0,
		TEXT("When non-zero, a Quest-calibrated arm holds the last valid arm target while that side's Quest hand is not tracked, instead of accepting collapsed MediaPipe wrist samples."));

	TAutoConsoleVariable<float> CVarMediaPipeArmRotationMaxStepDegrees(
		TEXT("mp.MediaPipeArmRotationMaxStepDegrees"),
		12.0f,
		TEXT("Maximum upper/lower arm component-space rotation step per evaluated frame. 0 disables this fixed-step clamp."));

	TAutoConsoleVariable<float> CVarMediaPipeArmRotationMaxSpeedDegreesPerSecond(
		TEXT("mp.MediaPipeArmRotationMaxSpeedDegreesPerSecond"),
		480.0f,
		TEXT("Maximum upper/lower arm component-space rotation speed. 0 disables this time-scaled clamp."));

	TAutoConsoleVariable<float> CVarMediaPipeClavicleShrugWeight(
		TEXT("mp.MediaPipeClavicleShrugWeight"),
		0.0f,
		TEXT("Additional clavicle lift from shoulder-height change relative to a slowly tracked rest height. 0 disables shrug response."));

	TAutoConsoleVariable<float> CVarMediaPipeClavicleShrugMinCm(
		TEXT("mp.MediaPipeClavicleShrugMinCm"),
		2.0f,
		TEXT("Shoulder-height rise in centimeters before MediaPipe clavicle shrug response starts."));

	TAutoConsoleVariable<float> CVarMediaPipeClavicleShrugFullCm(
		TEXT("mp.MediaPipeClavicleShrugFullCm"),
		8.0f,
		TEXT("Shoulder-height rise in centimeters that reaches full MediaPipe clavicle shrug response."));

	TAutoConsoleVariable<int32> CVarMediaPipeHolisticShoulderSolve(
		TEXT("mp.MediaPipeHolisticShoulderSolve"),
		0,
		TEXT("When non-zero, dense Holistic face landmarks are used for shoulder/head clearance instead of only sparse pose face points."));

	TAutoConsoleVariable<float> CVarMediaPipeShoulderLiftTranslationScale(
		TEXT("mp.MediaPipeShoulderLiftTranslationScale"),
		1.0f,
		TEXT("Scales solved shoulder lift translation before it is applied to the clavicle and arm chain."));

	TAutoConsoleVariable<int32> CVarMediaPipeShoulderRollbackTrace(
		TEXT("mp.MediaPipeShoulderRollbackTrace"),
		0,
		TEXT("When non-zero, log suspected shoulder-rollback arm flip events without enabling the full Quest wrist trace."));

	TAutoConsoleVariable<int32> CVarMediaPipeShoulderRollbackGuard(
		TEXT("mp.MediaPipeShoulderRollbackGuard"),
		0,
		TEXT("When non-zero, hold/blend direct MediaPipe arm targets that appear to flip behind the torso during shoulder rollback. Disable live with mp.MediaPipeShoulderRollbackGuard 0."));

	TAutoConsoleVariable<float> CVarMediaPipeShoulderRollbackGuardBlend(
		TEXT("mp.MediaPipeShoulderRollbackGuardBlend"),
		0.0f,
		TEXT("Blend fraction toward a rejected shoulder-rollback arm target. 0 hard-holds the previous arm rotation; small values allow slow recovery."));

	TAutoConsoleVariable<float> CVarMediaPipeShoulderRollbackGuardMinReliability(
		TEXT("mp.MediaPipeShoulderRollbackGuardMinReliability"),
		0.45f,
		TEXT("If elbow or wrist MediaPipe reliability is below this value while the upper arm is behind the torso, the shoulder rollback guard holds the previous arm rotation."));

	TAutoConsoleVariable<float> CVarMediaPipeShoulderRollbackGuardMaxTargetFromRefDegrees(
		TEXT("mp.MediaPipeShoulderRollbackGuardMaxTargetFromRefDegrees"),
		150.0f,
		TEXT("If the candidate upper-arm target is farther than this many degrees from the reference pose while behind the torso, the shoulder rollback guard holds the previous arm rotation."));

	TAutoConsoleVariable<float> CVarMediaPipeShoulderRollbackBackDotThreshold(
		TEXT("mp.MediaPipeShoulderRollbackBackDotThreshold"),
		-0.20f,
		TEXT("Arm or wrist direction dot(actor forward) below this value is treated as behind the torso for shoulder rollback diagnostics."));

	TAutoConsoleVariable<float> CVarMediaPipeShoulderRollbackStepDegrees(
		TEXT("mp.MediaPipeShoulderRollbackStepDegrees"),
		80.0f,
		TEXT("Target upper/lower arm rotation jump in degrees that triggers shoulder rollback diagnostics."));

	TAutoConsoleVariable<float> CVarMediaPipeShoulderRollbackTraceLogIntervalSeconds(
		TEXT("mp.MediaPipeShoulderRollbackTraceLogIntervalSeconds"),
		0.10f,
		TEXT("Minimum seconds between shoulder rollback diagnostic log lines per side."));

	TAutoConsoleVariable<float> CVarMediaPipeSpineRotationHalfLife(
		TEXT("mp.MediaPipeSpineRotationHalfLife"),
		0.14f,
		TEXT("Smoothing half-life in seconds for MediaPipe pelvis/spine rotations. Higher values reduce torso jitter at the cost of lag."));

	TAutoConsoleVariable<float> CVarMediaPipeHeadRotationHalfLife(
		TEXT("mp.MediaPipeHeadRotationHalfLife"),
		0.18f,
		TEXT("Smoothing half-life in seconds for MediaPipe neck/head rotations. Higher values reduce head snaps at the cost of lag."));

	TAutoConsoleVariable<float> CVarMediaPipeHeadTwistWeight(
		TEXT("mp.MediaPipeHeadTwistWeight"),
		0.0f,
		TEXT("Multiplier for MediaPipe face-derived neck/head twist around character up. 0 disables noisy live twist; 1 uses the full configured twist share."));

	TAutoConsoleVariable<float> CVarMediaPipeHeadFaceBlend(
		TEXT("mp.MediaPipeHeadFaceBlend"),
		0.0f,
		TEXT("Blend from stable chest-aligned head basis to face-landmark head basis. 0 disables noisy live face orientation; 1 uses full face orientation."));

	TAutoConsoleVariable<float> CVarMediaPipeHeadPitchScale(
		TEXT("mp.MediaPipeHeadPitchScale"),
		1.0f,
		TEXT("Multiplier for face-derived head pitch/nod after reliability gating. Does not affect yaw, roll, or twist."));

	TAutoConsoleVariable<int32> CVarMediaPipeHolisticHeadSolve(
		TEXT("mp.MediaPipeHolisticHeadSolve"),
		0,
		TEXT("When non-zero, dense Holistic face landmarks drive the head nod/roll solve independently from the older sparse pose face proxy."));

	TAutoConsoleVariable<float> CVarMediaPipeHeadRotationMaxStepDegrees(
		TEXT("mp.MediaPipeHeadRotationMaxStepDegrees"),
		1.0f,
		TEXT("Maximum neck/head component-space rotation step per evaluated frame. 0 disables clamping."));

	TAutoConsoleVariable<float> CVarMediaPipeHeadRotationMaxSpeedDegreesPerSecond(
		TEXT("mp.MediaPipeHeadRotationMaxSpeedDegreesPerSecond"),
		60.0f,
		TEXT("Maximum neck/head component-space rotation speed in degrees per second. 0 disables clamping."));

	TAutoConsoleVariable<float> CVarMediaPipeTorsoMaxTiltDegrees(
		TEXT("mp.MediaPipeTorsoMaxTiltDegrees"),
		20.0f,
		TEXT("Maximum MediaPipe torso-up tilt away from world up. Lower values reject webcam torso-basis flips while preserving actor facing."));

	TAutoConsoleVariable<float> CVarMediaPipeTorsoUprightBlend(
		TEXT("mp.MediaPipeTorsoUprightBlend"),
		0.85f,
		TEXT("Blend from MediaPipe torso up toward world up before applying the torso tilt clamp. 0 uses raw MediaPipe up; 1 is fully upright."));

	TAutoConsoleVariable<int32> CVarMediaPipeTorsoUseActorForward(
		TEXT("mp.MediaPipeTorsoUseActorForward"),
		1,
		TEXT("When non-zero, use the actor/component forward vector for the live torso basis instead of MediaPipe's noisy webcam yaw."));

	TAutoConsoleVariable<int32> CVarMediaPipePoseYawAlignToActor(
		TEXT("mp.MediaPipePoseYawAlignToActor"),
		1,
		TEXT("When non-zero, yaw-rotates the whole MediaPipe landmark cloud so the tracked torso faces the target actor forward. Used by the fixed VR mirror profile."));

	TAutoConsoleVariable<float> CVarMediaPipePoseYawAlignHalfLife(
		TEXT("mp.MediaPipePoseYawAlignHalfLife"),
		0.30f,
		TEXT("Smoothing half-life in seconds for the MediaPipe pose-cloud yaw correction. Higher values reduce VR body twist/jitter at the cost of slower recentering."));

	TAutoConsoleVariable<float> CVarMediaPipePoseYawAlignMaxSpeedDegreesPerSecond(
		TEXT("mp.MediaPipePoseYawAlignMaxSpeedDegreesPerSecond"),
		120.0f,
		TEXT("Maximum speed for the MediaPipe pose-cloud yaw correction. 0 disables the speed cap."));

	TAutoConsoleVariable<float> CVarMediaPipePoseYawAlignRejectJumpDegrees(
		TEXT("mp.MediaPipePoseYawAlignRejectJumpDegrees"),
		55.0f,
		TEXT("Reject one-frame MediaPipe torso yaw correction jumps larger than this many degrees. 0 disables rejection."));

	TAutoConsoleVariable<int32> CVarMediaPipeTorsoDebug(
		TEXT("mp.MediaPipeTorsoDebug"),
		0,
		TEXT("When non-zero, log the live MediaPipe torso basis and the clamped basis used by Manny once per second."));

	// --- Tracking-quality tracers (Docs/TRACKING_QUALITY_PLAN.md Phase 0, 2026-07-11).
	// Report-only rows, armed manually per session like mp.MediaPipeCameraHandTrace;
	// deliberately NOT in any settings-variant list. Throttles live in the keyed
	// runtime stores (per actor+side), never in node members.

	TAutoConsoleVariable<int32> CVarFootSkateTrace(
		TEXT("mp.FootSkateTrace"),
		0,
		TEXT("When non-zero, emit per-foot mp.FootSkateTrace rows from the leg solve: provisional contact label (height+velocity), planar/vertical foot speed, lift above the observed source floor, and rendered-ankle penetration below the rig's planted height. The foot-skate scoreboard for TRACKING_QUALITY_PLAN Phase 0/4; report-only, no behavior change."));

	TAutoConsoleVariable<int32> CVarWristLimitTrace(
		TEXT("mp.WristLimitTrace"),
		0,
		TEXT("When non-zero, emit mp.WristLimitTrace rows at every final wrist write (quest/held/camera paths): swing+twist of the written hand rotation away from the neutral wrist pose on the current forearm, and how far outside the report-only anatomical envelope it sits. Measures what a Phase 2 anatomical clamp WOULD have caught; report-only, no behavior change."));

	// Foreshortening -> Z-distrust (TRACKING_QUALITY_PLAN Phase 3, 2026-07-11). ManiPose
	// insight, pragmatic form: 2D->3D lifting is ill-posed exactly when a limb segment's
	// IMAGE-PLANE length collapses (segment pointing into the camera's depth axis). The
	// per-segment ratio (current planar length / decaying-max planar length) uses only
	// image-plane geometry - the suspect Z never feeds its own distrust. Distrust scales
	// the landmark reliability fed to the solver's gates (arm-direction learn/vote, hand
	// arm gate, leg stabilizer when enabled) and eases foreshortened LEG segment planar
	// headings toward the sagittal plane (the azimuth is the ill-conditioned part; the
	// image-reliable elevation - the raise cue - is preserved). NOTE: the leg solve's
	// only reliability consumer (mp.MediaPipeLegReliabilityStabilize) is OFF by user
	// acceptance (2026-06-13, full-extent legs), so the sagittal ease is the leg-side
	// consumer of this signal - stateless target, bounded by the smoothed alpha, no
	// learning, asymmetric engage/release smoothing.
	TAutoConsoleVariable<int32> CVarMediaPipeForeshortenZDistrust(
		TEXT("mp.MediaPipeForeshortenZDistrust"),
		0,
		TEXT("When non-zero, per-limb-segment image-plane foreshortening scales down the landmark reliability fed to Z consumers (dwell-smoothed, floor 0.25) and eases foreshortened leg segment planar headings toward the sagittal plane (elevation preserved). TRACKING_QUALITY_PLAN Phase 3; default 0 = no distrust."));

	TAutoConsoleVariable<int32> CVarForeshortenTrace(
		TEXT("mp.ForeshortenTrace"),
		0,
		TEXT("When non-zero, emit per-actor mp.ForeshortenTrace rows (4 Hz, keyed throttle): per limb segment the image-plane foreshorten ratio, the smoothed distrust alpha, and the applied reliability scale. Report-only; the distrust itself is gated by mp.MediaPipeForeshortenZDistrust."));

	// Anatomical wrist clamp (TRACKING_QUALITY_PLAN Phase 2, 2026-07-11). Swing-twist
	// guardrail on the FINAL wrist rotation, the LAST op before the bone write at every
	// wrist write site (quest/held/camera - the same three sites the palm trim covers).
	// A guardrail against anatomically impossible frames (the 2026-07-09 20-130deg flap
	// class), NOT a stylistic limit: ranges are generous (Kenwright twist-and-swing;
	// ECCV 2020 biomech hand constraints). Clamped frames never feed any learner - the
	// clamp writes only the pose, never state; continuity/hold state keeps the unclamped
	// value. Clamp events emit on mp.WristLimitTrace with the pre-clamp excess.
	TAutoConsoleVariable<int32> CVarWristAnatomicalClamp(
		TEXT("mp.WristAnatomicalClamp"),
		0,
		TEXT("When non-zero, clamp the final wrist rotation's twist (about the forearm axis) and swing (cone from the neutral wrist pose on the current forearm) to the anatomical ranges below, as the last operation before the wrist bone write. Guardrail only; in-range frames pass through bit-exactly. TRACKING_QUALITY_PLAN Phase 2; default 0 = no clamp."));

	TAutoConsoleVariable<float> CVarWristTwistRangeDeg(
		TEXT("mp.WristTwistRangeDeg"),
		90.0f,
		TEXT("Anatomical twist envelope (degrees, +-) about the forearm axis for mp.WristAnatomicalClamp - pronation/supination expressed at the hand-vs-lowerarm joint of the two-bone rig. Generous by design (biomech ~85-90)."));

	TAutoConsoleVariable<float> CVarWristSwingRangeDeg(
		TEXT("mp.WristSwingRangeDeg"),
		85.0f,
		TEXT("Anatomical swing-cone envelope (degrees) away from the neutral wrist pose for mp.WristAnatomicalClamp - flexion/extension (~80/70) and radial/ulnar deviation (~20/30) bounded by the widest direction as a single cone. Generous by design."));

	// Timestamp-aligned corrector residuals (TRACKING_QUALITY_PLAN Phase 1, 2026-07-11).
	// Out-of-sequence-measurement fix: a webcam measurement is ~80-130ms old at fuse time,
	// so comparing it against the CURRENT pose manufactures phantom residuals during motion
	// (the transient class behind the July arm arc). When 1, the arm-direction and heading
	// learners compare each measurement against the buffered pose at the measurement's own
	// effective capture time (capture timestamp + conditioner forward prediction; rings in
	// the keyed store / body-solver state). Apply() is unchanged - corrections still apply
	// to the current pose. Default 0 = byte-identical legacy behavior; candidate variant
	// arms it for the Phase 6 worn A/B.
	TAutoConsoleVariable<int32> CVarMediaPipeTimestampAlignedResiduals(
		TEXT("mp.MediaPipeTimestampAlignedResiduals"),
		0,
		TEXT("When non-zero, corrector learners (arm direction, heading) compare webcam measurements against the buffered solved pose at the measurement's effective capture time instead of the current pose. Application of corrections is unchanged. TRACKING_QUALITY_PLAN Phase 1; default 0 = legacy current-pose residuals."));

	TAutoConsoleVariable<int32> CVarWebcamAgeTrace(
		TEXT("mp.WebcamAgeTrace"),
		0,
		TEXT("When non-zero, emit mp.WebcamAgeTrace rows at the arm-direction corrector call site: webcam measurement age at solve time (capture timestamp vs now), the conditioner's forward-prediction horizon, the effective residual age, and the camera-vs-chain direction residuals the corrector currently computes against the CURRENT pose - the number TRACKING_QUALITY_PLAN Phase 1 (timestamp-aligned residuals) is expected to shrink during motion. Report-only, no behavior change."));
}
