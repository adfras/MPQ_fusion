#include "MediaPipeQuestWristApplyPolicy.h"

#include "Math/UnrealMathUtility.h"

bool FMediaPipeQuestWristApplyPolicy::CanUseLiveWristForPositionApply(const FMediaPipeQuestWristApplyPolicyInput& Input)
{
	if (!Input.bQuestSideUsable)
	{
		return false;
	}

	if (!Input.bRequireTrackedForApply || Input.bQuestSideTracked)
	{
		return true;
	}

	if (!Input.bAllowUsableUntrackedForPositionApply)
	{
		return false;
	}

	if (!Input.bHasRecentAcceptedLiveWristPosition)
	{
		return false;
	}

	if (Input.MaxUntrackedLiveWristAgeSeconds > 0.0f &&
		Input.LastAcceptedLiveWristAgeSeconds > Input.MaxUntrackedLiveWristAgeSeconds)
	{
		return false;
	}

	if (Input.MaxUntrackedLiveWristStepCm > 0.0f &&
		Input.UntrackedLiveWristStepFromLastAcceptedCm > Input.MaxUntrackedLiveWristStepCm)
	{
		return false;
	}

	return true;
}

bool FMediaPipeQuestWristApplyPolicy::CanUseQuestHandRotationForCurrentFrame(const FMediaPipeQuestHandRotationFramePolicyInput& Input)
{
	if (!Input.bRequireTrackedForHandRotation || Input.bQuestSideTracked)
	{
		return true;
	}

	if (!Input.bCurrentWristPositionApplied ||
		!Input.bCurrentWristMapped ||
		!Input.bCurrentWristUsedUntrackedJointData ||
		Input.bCurrentWristUsedHeldTarget ||
		Input.bCurrentWristRawRejected ||
		Input.bCurrentWristBodyFallback)
	{
		return false;
	}

	FMediaPipeQuestWristApplyPolicyInput LiveWristPolicy = Input.LiveWristPolicy;
	LiveWristPolicy.bRequireTrackedForApply = true;
	LiveWristPolicy.bQuestSideTracked = false;
	return CanUseLiveWristForPositionApply(LiveWristPolicy);
}

bool FMediaPipeQuestWristApplyPolicy::ShouldAttemptPositionSolve(const FMediaPipeQuestWristPositionAttemptInput& Input)
{
	if (!Input.bQuestArmUsesWristEndpoint ||
		Input.RequestedPositionBlend <= 0.0f)
	{
		return false;
	}

	if (Input.bQuestSideUsable)
	{
		return true;
	}

	return Input.bQuestArmUsesConstrainedSolve && Input.bHasHeldTarget;
}

bool FMediaPipeQuestWristApplyPolicy::HasFreshHeldTargetForPositionAttempt(const FMediaPipeQuestWristHeldTargetLossInput& Input)
{
	return !ShouldClearPositionAuthorityForHeldTargetLoss(Input);
}

bool FMediaPipeQuestWristApplyPolicy::ShouldClearPositionAuthorityForHeldTargetLoss(const FMediaPipeQuestWristHeldTargetLossInput& Input)
{
	return !Input.bHasHeldTarget ||
		Input.GraceSeconds <= 0.0f ||
		Input.LastTargetAgeSeconds < 0.0f ||
		Input.LastTargetAgeSeconds > Input.GraceSeconds;
}

bool FMediaPipeQuestWristApplyPolicy::ShouldHoldArmOnQuestHandLoss(const FMediaPipeQuestArmHoldOnLossInput& Input)
{
	return Input.bHoldOnQuestHandLossEnabled &&
		Input.bQuestHandTrackingEnabled &&
		!Input.bQuestSideTracked &&
		!Input.bQuestWristPositionCandidate &&
		Input.bHasLastReliableArmSample;
}

bool FMediaPipeQuestWristApplyPolicy::ShouldWriteFrameCoherentQuestArmPose(const FMediaPipeQuestArmPoseWriteInput& Input)
{
	return Input.bUseHmdRelativeAvatarArmFrame && Input.bQuestWristPositionApplied;
}

bool FMediaPipeQuestWristApplyPolicy::ShouldOwnArmLengthCalibration(
	const FMediaPipeQuestArmLengthCalibrationOwnerPolicyInput& Input)
{
	return (Input.bTargetIsMetaHuman && Input.bTargetProfileActive) ||
		(Input.bHasTargetEmbodimentProfile && Input.bTargetIsMannyLike);
}

FMediaPipeQuestArmDropoutDownFallbackPolicyResult FMediaPipeQuestWristApplyPolicy::ShouldUseDropoutDownFallback(
	const FMediaPipeQuestArmDropoutDownFallbackPolicyInput& Input)
{
	FMediaPipeQuestArmDropoutDownFallbackPolicyResult Result;

	if (!Input.bEnabled ||
		!Input.bUseHmdRelativeAvatarArmFrame ||
		!Input.bQuestArmUsesConstrainedSolve ||
		!Input.bQuestHandTrackingEnabled ||
		Input.bQuestSideTracked ||
		!Input.bHasOnlyDropoutEndpoint)
	{
		return Result;
	}

	// USER RULE (2026-07-02): never force an arm down while the camera sees it. This veto
	// outranks the continue latch on purpose - the measured failure was an active fallback
	// riding bContinueActiveFallback through a whole overhead raise, dragging the wrist to
	// full-extension-down at the calibrated reach while MediaPipe watched the arm go up.
	if (Input.bMediaPipeSeesArmNotDown)
	{
		return Result;
	}

	if (Input.bContinueActiveFallback)
	{
		Result.bUseFallback = true;
		return Result;
	}

	const bool bHasPoseSeed =
		Input.bHasRecentTrackedArmPose ||
		Input.bHasRecentConstrainedArmSolve ||
		Input.bHasLastReliableArmSample ||
		Input.bHasMediaPipeDownHint;
	if (!bHasPoseSeed)
	{
		return Result;
	}

	if (Input.bLastTrackedPoseWasDown ||
		Input.bHasMediaPipeDownHint)
	{
		Result.bUseFallback = true;
		return Result;
	}

	if (Input.bHasAcceptedArmLengthCalibration &&
		Input.bCanInferCalibratedDownPose)
	{
		Result.bUseFallback = true;
		Result.bInferCalibratedDownPose = true;
	}

	return Result;
}

FMediaPipeQuestReachScaleCalibrationResult FMediaPipeQuestWristApplyPolicy::ComputeReachScaleCalibration(
	const FMediaPipeQuestReachScaleCalibrationInput& Input)
{
	FMediaPipeQuestReachScaleCalibrationResult Result;
	Result.TargetReachCm = Input.CurrentReachCm;

	const float TargetMaxReachCm = FMath::Max(Input.TargetMinReachCm, Input.TargetMaxReachCm);
	if (!Input.bEnabled ||
		Input.bSuppressDuringDropoutReacquire ||
		Input.CurrentReachCm <= UE_SMALL_NUMBER ||
		Input.ObservedMaxReachCm <= UE_SMALL_NUMBER ||
		TargetMaxReachCm <= UE_SMALL_NUMBER)
	{
		return Result;
	}

	const float MinObservedTargetFraction = FMath::Clamp(Input.MinObservedTargetFraction, 0.0f, 1.0f);
	if (Input.ObservedMaxReachCm < TargetMaxReachCm * MinObservedTargetFraction)
	{
		return Result;
	}

	const float MinScale = FMath::Max(UE_SMALL_NUMBER, Input.MinScale);
	const float MaxScale = FMath::Max(MinScale, Input.MaxScale);
	const float EffectiveMinScale = Input.bAllowScaleBelowOne ? MinScale : FMath::Max(1.0f, MinScale);
	Result.Scale = FMath::Clamp(TargetMaxReachCm / Input.ObservedMaxReachCm, EffectiveMinScale, MaxScale);

	if (Input.bApplyUniformScale)
	{
		Result.ApplyAlpha = 1.0f;
	}
	else
	{
		const float StartFraction = FMath::Clamp(Input.ApplyStartObservedFraction, 0.0f, 1.0f);
		const float FullFraction = FMath::Clamp(Input.ApplyFullObservedFraction, StartFraction + UE_SMALL_NUMBER, 1.0f);
		const float CurrentObservedFraction = Input.CurrentReachCm / FMath::Max(Input.ObservedMaxReachCm, UE_SMALL_NUMBER);
		Result.ApplyAlpha = FMath::Clamp(
			(CurrentObservedFraction - StartFraction) / FMath::Max(FullFraction - StartFraction, UE_SMALL_NUMBER),
			0.0f,
			1.0f);
	}
	if (Result.ApplyAlpha <= KINDA_SMALL_NUMBER ||
		FMath::IsNearlyEqual(Result.Scale, 1.0f, 0.001f))
	{
		return Result;
	}

	const float ScaledReachCm = Input.CurrentReachCm * FMath::Lerp(1.0f, Result.Scale, Result.ApplyAlpha);
	Result.TargetReachCm = FMath::Clamp(
		ScaledReachCm,
		FMath::Max(0.0f, Input.TargetMinReachCm),
		TargetMaxReachCm);
	Result.bApplied = !FMath::IsNearlyEqual(Result.TargetReachCm, Input.CurrentReachCm, 0.1f);
	return Result;
}

FMediaPipeQuestReachStepContinuityResult FMediaPipeQuestWristApplyPolicy::ApplyReachStepContinuity(
	const FMediaPipeQuestReachStepContinuityInput& Input)
{
	FMediaPipeQuestReachStepContinuityResult Result;
	Result.RawReachCm = Input.CurrentReachCm;
	Result.TargetReachCm = Input.CurrentReachCm;
	Result.PreviousReachCm = Input.PreviousReachCm;
	Result.MaxStepCm = FMath::Max(0.0f, Input.MaxStepCm);

	const float MinReachCm = FMath::Max(0.0f, Input.MinReachCm);
	const float MaxReachCm = FMath::Max(MinReachCm, Input.MaxReachCm);
	if (MaxReachCm > KINDA_SMALL_NUMBER)
	{
		Result.TargetReachCm = FMath::Clamp(Result.TargetReachCm, MinReachCm, MaxReachCm);
	}

	if (!Input.bEnabled ||
		Result.MaxStepCm <= KINDA_SMALL_NUMBER ||
		!Input.bHasPreviousReach ||
		Input.CurrentReachCm <= KINDA_SMALL_NUMBER ||
		Input.PreviousReachCm <= KINDA_SMALL_NUMBER)
	{
		return Result;
	}

	const float ReachDeltaCm = Result.TargetReachCm - Input.PreviousReachCm;
	if (FMath::Abs(ReachDeltaCm) <= Result.MaxStepCm)
	{
		return Result;
	}

	Result.TargetReachCm = FMath::Clamp(
		Input.PreviousReachCm + FMath::Sign(ReachDeltaCm) * Result.MaxStepCm,
		MinReachCm,
		MaxReachCm);
	Result.bApplied = !FMath::IsNearlyEqual(Result.TargetReachCm, Input.CurrentReachCm, 0.1f);
	return Result;
}

float FMediaPipeQuestWristApplyPolicy::ContinueAngleDegrees(float PreviousUnwrappedDeg, float CurrentWrappedDeg)
{
	return PreviousUnwrappedDeg + FMath::FindDeltaAngleDegrees(PreviousUnwrappedDeg, CurrentWrappedDeg);
}
