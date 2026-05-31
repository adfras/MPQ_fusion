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

	TAutoConsoleVariable<int32> CVarMediaPipeUseFkRootGrounding(
		TEXT("mp.MediaPipeUseFkRootGrounding"),
		0,
		TEXT("When non-zero, apply FK root grounding after MediaPipe leg solving. Default is off for cached retarget comparison."));

	TAutoConsoleVariable<int32> CVarMediaPipeDriveHandRotation(
		TEXT("mp.MediaPipeDriveHandRotation"),
		0,
		TEXT("When non-zero, drive Manny wrist/hand rotation from MediaPipe hand or pose hand landmarks. Default is off because current hand basis can twist badly."));

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

	TAutoConsoleVariable<int32> CVarBodyFusionMediaPipeAuthority(
		TEXT("mp.BodyFusion.MediaPipeAuthority"),
		0,
		TEXT("MediaPipe body authority mode. 0=trace-only/no pose authority, 1=allow only after stable calibration, 2=legacy allow when calibrated and fresh."));

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
}
