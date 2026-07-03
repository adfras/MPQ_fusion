#include "MediaPipeQuestWristCalibrationState.h"

#include "MediaPipeQuestWristTraceTypes.h"

void FQuestWristSideRuntimeState::ResetRotationCalibration()
{
	bHasRotationCalibration = false;
	RotationCalibrationBasisComp = FQuat::Identity;
	RotationCalibrationSource = 0;
	bHasRotationAnatomicalRollAxis = false;
	RotationAnatomicalRollAxisIndex = 0;
	RotationAnatomicalRollAxisSign = 1.0f;
	bHasRotationProjectedAxis = false;
	RotationProjectedAxisIndex = 0;
	RotationProjectedCalibrationMask = 0;
	for (FVector& Ref : RotationProjectedCalibrationRefsComp)
	{
		Ref = FVector::ZeroVector;
	}
	bHasRotationSemanticRollAxis = false;
	RotationSemanticRollAxisIndex = 0;
	RotationSemanticRollCalibrationOffsetDeg = 0.0f;
	RotationSemanticRollCalibrationForwardComp = FVector::ZeroVector;
	RotationSemanticRollCalibrationUpComp = FVector::ZeroVector;
	bHasRotationSemanticRollLastTwist = false;
	RotationSemanticRollLastTwistDeg = 0.0f;
	RotationCalibrationState = QuestWristCalibrationState_WaitingForStablePose;
	RotationCalibrationRejectReason = QuestWristCalibrationReject_WaitingForStablePose;
	RotationCalibrationStableFrameCount = 0;
	RotationCalibrationStableSeconds = 0.0f;
	RotationCalibrationFreshStableFrameCount = 0;
	RotationCalibrationFreshStableSeconds = 0.0f;
	RotationCalibrationLastBasisErrorDeg = 0.0f;
	RotationCalibrationLastNeutralTwistDeg = 0.0f;
	RotationCalibrationMeasureStartTimeSeconds = -1.0;
	RotationCalibrationLastSampleTimeSeconds = -1.0;
	RotationCalibrationLastWristWorld = FVector::ZeroVector;
	RotationCalibrationLastMeasuredComp = FQuat::Identity;
	RotationCalibrationLastBodyYawDeg = 0.0f;
	RotationCalibrationLastMannyYawDeg = 0.0f;
	RotationCalibrationLastSemanticAxisIndex = 0;
	bHasRotationCalibrationLastSample = false;
}

void FQuestWristSideRuntimeState::ResetPositionContinuity(const bool bResetArmLengthCalibration)
{
	bHasHeldTarget = false;
	HeldTargetWorld = FVector::ZeroVector;
	HeldRawQuestWristWorld = FVector::ZeroVector;
	HeldMappedQuestWristWorld = FVector::ZeroVector;
	LastTargetTimeSeconds = -1.0;
	bHasLastAcceptedLiveWristPosition = false;
	LastAcceptedLiveWristWorld = FVector::ZeroVector;
	LastAcceptedLiveWristTimeSeconds = -1.0;
	bHasPositionFilter = false;
	PositionFilterDeltaComp = FVector::ZeroVector;
	PositionFilterLastRawDeltaComp = FVector::ZeroVector;
	PositionFilterLastTimeSeconds = -1.0;
	bHasPositionStartupLastSample = false;
	PositionStartupLastRawQuestWristWorld = FVector::ZeroVector;
	PositionStartupLastMediaPipeWristWorld = FVector::ZeroVector;
	PositionStartupLastSampleTimeSeconds = -1.0;
	PositionStartupStableFrameCount = 0;
	PositionStartupStableSeconds = 0.0f;
	bHasHmdRelativeReachObservedMax = false;
	HmdRelativeReachObservedMaxCm = 0.0f;
	bHasArmLengthCalibrationCandidate = false;
	bArmLengthCalibrationCandidateTracked = false;
	ArmLengthCalibrationCandidateWristWorld = FVector::ZeroVector;
	ArmLengthCalibrationCandidateShoulderWorld = FVector::ZeroVector;
	ArmLengthCalibrationCandidateReachCm = 0.0f;
	ArmLengthCalibrationCandidateBelowShoulderCm = 0.0f;
	ArmLengthCalibrationCandidateVerticalDominance = 0.0f;
	ArmLengthCalibrationCandidateTimeSeconds = -1.0;
	bHasArmLengthCalibrationLastSample = false;
	ArmLengthCalibrationLastWristWorld = FVector::ZeroVector;
	ArmLengthCalibrationLastSampleTimeSeconds = -1.0;
	ArmLengthCalibrationLastVelocityCmSec = 0.0f;
	if (bResetArmLengthCalibration)
	{
		bHasArmLengthCalibrationForwardReach = false;
		ArmLengthCalibrationForwardReachCm = 0.0f;
		bHasArmLengthCalibrationDownSample = false;
		ArmLengthCalibrationDownDropCm = 0.0f;
		ArmLengthCalibrationDownReachCm = 0.0f;
	}
	bHasHmdRelativeReachContinuity = false;
	HmdRelativeReachContinuityCm = 0.0f;
	HmdRelativeReachContinuityTimeSeconds = -1.0;
	bHasLastTrackedQuestArmPose = false;
	LastTrackedQuestArmShoulderWorld = FVector::ZeroVector;
	LastTrackedQuestArmElbowWorld = FVector::ZeroVector;
	LastTrackedQuestArmWristWorld = FVector::ZeroVector;
	LastTrackedQuestArmReachCm = 0.0f;
	LastTrackedQuestArmBelowShoulderCm = 0.0f;
	LastTrackedQuestArmDownDominance = 0.0f;
	LastTrackedQuestArmTimeSeconds = -1.0;
	bDropoutDownFallbackActive = false;
	DropoutDownFallbackWristWorld = FVector::ZeroVector;
	DropoutDownFallbackElbowWorld = FVector::ZeroVector;
	DropoutDownFallbackLastUpdateTimeSeconds = -1.0;
	DropoutReacquireReachScaleSuppressUntilTimeSeconds = -1.0;
	PositionAuthorityAlpha = 0.0f;
	PositionAuthorityLastTimeSeconds = -1.0;
}

void FQuestWristSideRuntimeState::Reset()
{
	bHasApplyCalibration = false;
	ApplyQuestHmdToWristWorld = FVector::ZeroVector;
	ApplyMediaPipeWristWorld = FVector::ZeroVector;
	ApplyCalibrationTimeSeconds = -1.0;
	bHasTraceCalibration = false;
	TraceQuestHmdToWristWorld = FVector::ZeroVector;
	TraceCalibrationTimeSeconds = -1.0;
	ResetRotationCalibration();
	ResetPositionContinuity();
	LastHandRotationApplyTimeSeconds = -1.0;
	LastMediaPipeHandRotationApplyTimeSeconds = -1.0;
	MediaPipeHandStickyArraySide = 255;
	// Camera-hand smoothing/continuity re-seeds from the current pose on the next camera frame.
	// The ownership latch (bCameraHandOwnershipLatched) is deliberately NOT cleared here, same
	// rule as bArmRescueActive: a pose-state reset must not hand a camera-driven hand back to a
	// flickering Quest side.
	bHasCameraHandSmoothedSwing = false;
	CameraHandSmoothedSwingCS = FQuat::Identity;
	bHasCameraHandSmoothedTwist = false;
	CameraHandSmoothedTwistDeg = 0.0f;
	bHasCameraHandLastGoodTarget = false;
	CameraHandLastGoodTargetCS = FQuat::Identity;
	CameraHandActiveBranch = 0;
	CameraHandPendingBranch = 0;
	CameraHandPendingBranchFrames = 0;
	CameraHandSmoothLastStepTimeSeconds = -1.0;
	bHasCameraHandLastApplied = false;
	CameraHandLastAppliedRotCS = FQuat::Identity;
	CameraHandChiralitySign = 0;
	CameraHandChiralityPendingSign = 0;
	CameraHandChiralityPendingSeconds = 0.0f;
}

void FQuestWristSideRuntimeState::ResetCalibration()
{
	bHasApplyCalibration = false;
	ApplyQuestHmdToWristWorld = FVector::ZeroVector;
	ApplyMediaPipeWristWorld = FVector::ZeroVector;
	ApplyCalibrationTimeSeconds = -1.0;
	bHasTraceCalibration = false;
	TraceQuestHmdToWristWorld = FVector::ZeroVector;
	TraceCalibrationTimeSeconds = -1.0;
	ResetRotationCalibration();
	ResetPositionContinuity();
}

void FQuestWristRuntimeState::Reset()
{
	CalibrationMode = EQuestMediaSpaceCalibrationMode::None;
	bHasHmdRelativeAvatarCalibration = false;
	HmdRelativeQuestAnchorWorld = FVector::ZeroVector;
	HmdRelativeQuestAnchorYawWorld = FQuat::Identity;
	bHasHmdRelativeQuestTranslationFilter = false;
	HmdRelativeQuestFilteredAnchorWorld = FVector::ZeroVector;
	HmdRelativeQuestLastRawAnchorWorld = FVector::ZeroVector;
	HmdRelativeQuestAnchorLastTimeSeconds = -1.0;
	ArmLengthCalibrationStage = QuestArmLengthCalibrationStage_WaitingForHands;
	ArmLengthCalibrationStableFrameCount = 0;
	ArmLengthCalibrationStableSeconds = 0.0f;
	ArmLengthCalibrationLastUpdateTimeSeconds = -1.0;
	ArmLengthCalibrationLastLogTimeSeconds = -1.0;
	ArmLengthCalibrationAcceptedTimeSeconds = -1.0;
	Left.Reset();
	Right.Reset();
}

void FQuestWristRuntimeState::ResetCalibration()
{
	CalibrationMode = EQuestMediaSpaceCalibrationMode::None;
	bHasHmdRelativeAvatarCalibration = false;
	HmdRelativeQuestAnchorWorld = FVector::ZeroVector;
	HmdRelativeQuestAnchorYawWorld = FQuat::Identity;
	bHasHmdRelativeQuestTranslationFilter = false;
	HmdRelativeQuestFilteredAnchorWorld = FVector::ZeroVector;
	HmdRelativeQuestLastRawAnchorWorld = FVector::ZeroVector;
	HmdRelativeQuestAnchorLastTimeSeconds = -1.0;
	ArmLengthCalibrationStage = QuestArmLengthCalibrationStage_WaitingForHands;
	ArmLengthCalibrationStableFrameCount = 0;
	ArmLengthCalibrationStableSeconds = 0.0f;
	ArmLengthCalibrationLastUpdateTimeSeconds = -1.0;
	ArmLengthCalibrationLastLogTimeSeconds = -1.0;
	ArmLengthCalibrationAcceptedTimeSeconds = -1.0;
	Left.ResetCalibration();
	Right.ResetCalibration();
}

void FMediaPipeQuestWristCalibrationState::WriteTrace(const FQuestWristSideRuntimeState& State, FQuestHandRotationTrace& Trace)
{
	Trace.CalibrationState = State.RotationCalibrationState;
	Trace.CalibrationRejectReason = State.RotationCalibrationRejectReason;
	Trace.CalibrationStableFrameCount = State.RotationCalibrationStableFrameCount;
	Trace.CalibrationStableSeconds = State.RotationCalibrationStableSeconds;
}

void FMediaPipeQuestWristCalibrationState::ResetMeasurement(
	FQuestWristSideRuntimeState& State,
	const uint8 Reason,
	FQuestHandRotationTrace* Trace)
{
	State.RotationCalibrationState = QuestWristCalibrationState_WaitingForStablePose;
	State.RotationCalibrationRejectReason = Reason;
	State.RotationCalibrationStableFrameCount = 0;
	State.RotationCalibrationStableSeconds = 0.0f;
	State.RotationCalibrationFreshStableFrameCount = 0;
	State.RotationCalibrationFreshStableSeconds = 0.0f;
	State.RotationCalibrationMeasureStartTimeSeconds = -1.0;
	State.RotationCalibrationLastSampleTimeSeconds = -1.0;
	State.bHasRotationCalibrationLastSample = false;

	if (Trace)
	{
		WriteTrace(State, *Trace);
	}
}

bool FMediaPipeQuestWristCalibrationState::IsSoftRejectReason(const uint8 Reason)
{
	switch (Reason)
	{
	case QuestWristCalibrationReject_RightHandNotTracked:
	case QuestWristCalibrationReject_LeftHandNotTracked:
	case QuestWristCalibrationReject_BodyUnstable:
	case QuestWristCalibrationReject_WristsMoving:
	case QuestWristCalibrationReject_PoseNotMatched:
		return true;
	default:
		return false;
	}
}

void FMediaPipeQuestWristCalibrationState::SoftRejectMeasurement(
	FQuestWristSideRuntimeState& State,
	const uint8 Reason,
	const FQuestWristCalibrationSoftRejectSettings& Settings,
	const float DeltaSeconds,
	const double NowSeconds,
	FQuestHandRotationTrace* Trace)
{
	if (!Settings.bEnableSoftGate || !IsSoftRejectReason(Reason))
	{
		ResetMeasurement(State, Reason, Trace);
		return;
	}

	const bool bHandLossReject =
		Reason == QuestWristCalibrationReject_RightHandNotTracked ||
		Reason == QuestWristCalibrationReject_LeftHandNotTracked;
	const bool bHasProgress =
		State.RotationCalibrationStableFrameCount > 0 ||
		State.RotationCalibrationStableSeconds > KINDA_SMALL_NUMBER;
	const bool bWithinHandLossPause =
		bHandLossReject &&
		State.RotationCalibrationLastSampleTimeSeconds >= 0.0 &&
		NowSeconds - State.RotationCalibrationLastSampleTimeSeconds <= FMath::Max(0.0f, Settings.HandLossPauseSeconds);

	State.RotationCalibrationFreshStableFrameCount = 0;
	State.RotationCalibrationFreshStableSeconds = 0.0f;
	if (bHasProgress && !bWithinHandLossPause)
	{
		const float DecaySeconds = FMath::Max(0.0f, DeltaSeconds) * FMath::Max(0.0f, Settings.SoftRejectDecayRate);
		State.RotationCalibrationStableSeconds = FMath::Max(
			0.0f,
			State.RotationCalibrationStableSeconds - DecaySeconds);

		const float RequiredStableSeconds = FMath::Max(Settings.RequiredStableSeconds, KINDA_SMALL_NUMBER);
		const int32 RequiredStableFrames = FMath::Max(1, Settings.RequiredStableFrames);
		const float ProgressFromSeconds = FMath::Clamp(
			State.RotationCalibrationStableSeconds / RequiredStableSeconds,
			0.0f,
			1.0f);
		State.RotationCalibrationStableFrameCount = FMath::Min(
			State.RotationCalibrationStableFrameCount,
			FMath::RoundToInt(ProgressFromSeconds * static_cast<float>(RequiredStableFrames)));
	}

	if (State.RotationCalibrationStableFrameCount <= 0 &&
		State.RotationCalibrationStableSeconds <= KINDA_SMALL_NUMBER)
	{
		State.RotationCalibrationStableFrameCount = 0;
		State.RotationCalibrationStableSeconds = 0.0f;
		State.RotationCalibrationMeasureStartTimeSeconds = -1.0;
	}
	else
	{
		State.RotationCalibrationMeasureStartTimeSeconds = NowSeconds - State.RotationCalibrationStableSeconds;
	}

	State.RotationCalibrationState = QuestWristCalibrationState_WaitingForStablePose;
	State.RotationCalibrationRejectReason = Reason;
	State.bHasRotationCalibrationLastSample = false;

	if (Trace)
	{
		WriteTrace(State, *Trace);
	}
}
