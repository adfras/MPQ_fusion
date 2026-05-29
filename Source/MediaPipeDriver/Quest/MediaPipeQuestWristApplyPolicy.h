#pragma once

struct FMediaPipeQuestWristApplyPolicyInput
{
	bool bRequireTrackedForApply = false;
	bool bAllowUsableUntrackedForPositionApply = false;
	bool bQuestSideTracked = false;
	bool bQuestSideUsable = false;
	bool bHasRecentAcceptedLiveWristPosition = false;
	float UntrackedLiveWristStepFromLastAcceptedCm = 0.0f;
	float LastAcceptedLiveWristAgeSeconds = 0.0f;
	float MaxUntrackedLiveWristStepCm = 0.0f;
	float MaxUntrackedLiveWristAgeSeconds = 0.0f;
};

struct FMediaPipeQuestWristPositionAttemptInput
{
	bool bQuestArmUsesWristEndpoint = false;
	bool bQuestArmUsesConstrainedSolve = false;
	bool bQuestSideUsable = false;
	bool bHasHeldTarget = false;
	float RequestedPositionBlend = 0.0f;
};

struct FMediaPipeQuestWristHeldTargetLossInput
{
	bool bHasHeldTarget = false;
	float GraceSeconds = 0.0f;
	float LastTargetAgeSeconds = -1.0f;
};

struct FMediaPipeQuestArmHoldOnLossInput
{
	bool bHoldOnQuestHandLossEnabled = false;
	bool bQuestHandTrackingEnabled = false;
	bool bQuestSideTracked = false;
	bool bHasLastReliableArmSample = false;
	bool bQuestWristPositionCandidate = false;
};

struct FMediaPipeQuestArmPoseWriteInput
{
	bool bUseHmdRelativeAvatarArmFrame = false;
	bool bQuestWristPositionApplied = false;
};

struct FMediaPipeQuestArmLengthCalibrationOwnerPolicyInput
{
	bool bTargetIsMetaHuman = false;
	bool bTargetProfileActive = false;
	bool bHasTargetEmbodimentProfile = false;
	bool bTargetIsMannyLike = false;
};

struct FMediaPipeQuestArmDropoutDownFallbackPolicyInput
{
	bool bEnabled = false;
	bool bUseHmdRelativeAvatarArmFrame = false;
	bool bQuestArmUsesConstrainedSolve = false;
	bool bQuestHandTrackingEnabled = false;
	bool bQuestSideTracked = false;
	bool bHasOnlyDropoutEndpoint = false;
	bool bHasRecentTrackedArmPose = false;
	bool bLastTrackedPoseWasDown = false;
	bool bHasMediaPipeDownHint = false;
	bool bContinueActiveFallback = false;
	bool bHasAcceptedArmLengthCalibration = false;
	bool bCanInferCalibratedDownPose = false;
	bool bHasRecentConstrainedArmSolve = false;
	bool bHasLastReliableArmSample = false;
};

struct FMediaPipeQuestArmDropoutDownFallbackPolicyResult
{
	bool bUseFallback = false;
	bool bInferCalibratedDownPose = false;
};

struct FMediaPipeQuestReachScaleCalibrationInput
{
	bool bEnabled = false;
	bool bSuppressDuringDropoutReacquire = false;
	bool bAllowScaleBelowOne = true;
	float CurrentReachCm = 0.0f;
	float ObservedMaxReachCm = 0.0f;
	float TargetMinReachCm = 0.0f;
	float TargetMaxReachCm = 0.0f;
	float MinObservedTargetFraction = 0.0f;
	bool bApplyUniformScale = false;
	float ApplyStartObservedFraction = 0.0f;
	float ApplyFullObservedFraction = 1.0f;
	float MinScale = 1.0f;
	float MaxScale = 1.0f;
};

struct FMediaPipeQuestReachScaleCalibrationResult
{
	bool bApplied = false;
	float Scale = 1.0f;
	float ApplyAlpha = 0.0f;
	float TargetReachCm = 0.0f;
};

struct FMediaPipeQuestReachStepContinuityInput
{
	bool bEnabled = false;
	float CurrentReachCm = 0.0f;
	bool bHasPreviousReach = false;
	float PreviousReachCm = 0.0f;
	float MaxStepCm = 0.0f;
	float MinReachCm = 0.0f;
	float MaxReachCm = 0.0f;
};

struct FMediaPipeQuestReachStepContinuityResult
{
	bool bApplied = false;
	float TargetReachCm = 0.0f;
	float RawReachCm = 0.0f;
	float PreviousReachCm = 0.0f;
	float MaxStepCm = 0.0f;
};

struct FMediaPipeQuestHandRotationFramePolicyInput
{
	FMediaPipeQuestWristApplyPolicyInput LiveWristPolicy;
	bool bRequireTrackedForHandRotation = false;
	bool bQuestSideTracked = false;
	bool bCurrentWristPositionApplied = false;
	bool bCurrentWristMapped = false;
	bool bCurrentWristUsedUntrackedJointData = false;
	bool bCurrentWristUsedHeldTarget = false;
	bool bCurrentWristRawRejected = false;
	bool bCurrentWristBodyFallback = false;
};

class FMediaPipeQuestWristApplyPolicy
{
public:
	static bool CanUseLiveWristForPositionApply(const FMediaPipeQuestWristApplyPolicyInput& Input);
	static bool CanUseQuestHandRotationForCurrentFrame(const FMediaPipeQuestHandRotationFramePolicyInput& Input);
	static bool ShouldAttemptPositionSolve(const FMediaPipeQuestWristPositionAttemptInput& Input);
	static bool HasFreshHeldTargetForPositionAttempt(const FMediaPipeQuestWristHeldTargetLossInput& Input);
	static bool ShouldClearPositionAuthorityForHeldTargetLoss(const FMediaPipeQuestWristHeldTargetLossInput& Input);
	static bool ShouldHoldArmOnQuestHandLoss(const FMediaPipeQuestArmHoldOnLossInput& Input);
	static bool ShouldWriteFrameCoherentQuestArmPose(const FMediaPipeQuestArmPoseWriteInput& Input);
	static bool ShouldOwnArmLengthCalibration(const FMediaPipeQuestArmLengthCalibrationOwnerPolicyInput& Input);
	static FMediaPipeQuestArmDropoutDownFallbackPolicyResult ShouldUseDropoutDownFallback(
		const FMediaPipeQuestArmDropoutDownFallbackPolicyInput& Input);
	static FMediaPipeQuestReachScaleCalibrationResult ComputeReachScaleCalibration(
		const FMediaPipeQuestReachScaleCalibrationInput& Input);
	static FMediaPipeQuestReachStepContinuityResult ApplyReachStepContinuity(
		const FMediaPipeQuestReachStepContinuityInput& Input);
	static float ContinueAngleDegrees(float PreviousUnwrappedDeg, float CurrentWrappedDeg);
};
