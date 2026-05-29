#pragma once

#include "CoreMinimal.h"
#include "MediaPipePoseDiagnostics.h"

struct FQuestHandRotationTrace;

enum EQuestArmLengthCalibrationStage : uint8
{
	QuestArmLengthCalibrationStage_WaitingForHands = 0,
	QuestArmLengthCalibrationStage_ForwardReach = 1,
	QuestArmLengthCalibrationStage_DownReach = 2,
	QuestArmLengthCalibrationStage_Accepted = 3,
};

struct FQuestWristSideRuntimeState
{
	bool bHasApplyCalibration = false;
	FVector ApplyQuestHmdToWristWorld = FVector::ZeroVector;
	FVector ApplyMediaPipeWristWorld = FVector::ZeroVector;
	double ApplyCalibrationTimeSeconds = -1.0;
	bool bHasTraceCalibration = false;
	FVector TraceQuestHmdToWristWorld = FVector::ZeroVector;
	double TraceCalibrationTimeSeconds = -1.0;
	bool bHasRotationCalibration = false;
	FQuat RotationCalibrationBasisComp = FQuat::Identity;
	uint8 RotationCalibrationSource = 0;
	bool bHasRotationAnatomicalRollAxis = false;
	uint8 RotationAnatomicalRollAxisIndex = 0;
	float RotationAnatomicalRollAxisSign = 1.0f;
	bool bHasRotationProjectedAxis = false;
	uint8 RotationProjectedAxisIndex = 0;
	uint8 RotationProjectedCalibrationMask = 0;
	FVector RotationProjectedCalibrationRefsComp[6] = {};
	bool bHasRotationSemanticRollAxis = false;
	uint8 RotationSemanticRollAxisIndex = 0;
	float RotationSemanticRollCalibrationOffsetDeg = 0.0f;
	FVector RotationSemanticRollCalibrationForwardComp = FVector::ZeroVector;
	FVector RotationSemanticRollCalibrationUpComp = FVector::ZeroVector;
	bool bHasRotationSemanticRollLastTwist = false;
	float RotationSemanticRollLastTwistDeg = 0.0f;
	uint8 RotationCalibrationState = QuestWristCalibrationState_WaitingForStablePose;
	uint8 RotationCalibrationRejectReason = QuestWristCalibrationReject_WaitingForStablePose;
	int32 RotationCalibrationStableFrameCount = 0;
	float RotationCalibrationStableSeconds = 0.0f;
	int32 RotationCalibrationFreshStableFrameCount = 0;
	float RotationCalibrationFreshStableSeconds = 0.0f;
	float RotationCalibrationLastBasisErrorDeg = 0.0f;
	float RotationCalibrationLastNeutralTwistDeg = 0.0f;
	double RotationCalibrationMeasureStartTimeSeconds = -1.0;
	double RotationCalibrationLastSampleTimeSeconds = -1.0;
	FVector RotationCalibrationLastWristWorld = FVector::ZeroVector;
	FQuat RotationCalibrationLastMeasuredComp = FQuat::Identity;
	float RotationCalibrationLastBodyYawDeg = 0.0f;
	float RotationCalibrationLastMannyYawDeg = 0.0f;
	uint8 RotationCalibrationLastSemanticAxisIndex = 0;
	bool bHasRotationCalibrationLastSample = false;
	bool bHasHeldTarget = false;
	FVector HeldTargetWorld = FVector::ZeroVector;
	FVector HeldRawQuestWristWorld = FVector::ZeroVector;
	FVector HeldMappedQuestWristWorld = FVector::ZeroVector;
	double LastTargetTimeSeconds = -1.0;
	bool bHasLastAcceptedLiveWristPosition = false;
	FVector LastAcceptedLiveWristWorld = FVector::ZeroVector;
	double LastAcceptedLiveWristTimeSeconds = -1.0;
	bool bHasPositionFilter = false;
	FVector PositionFilterDeltaComp = FVector::ZeroVector;
	FVector PositionFilterLastRawDeltaComp = FVector::ZeroVector;
	double PositionFilterLastTimeSeconds = -1.0;
	bool bHasPositionStartupLastSample = false;
	FVector PositionStartupLastRawQuestWristWorld = FVector::ZeroVector;
	FVector PositionStartupLastMediaPipeWristWorld = FVector::ZeroVector;
	double PositionStartupLastSampleTimeSeconds = -1.0;
	int32 PositionStartupStableFrameCount = 0;
	float PositionStartupStableSeconds = 0.0f;
	bool bHasHmdRelativeReachObservedMax = false;
	float HmdRelativeReachObservedMaxCm = 0.0f;
	bool bHasArmLengthCalibrationCandidate = false;
	bool bArmLengthCalibrationCandidateTracked = false;
	FVector ArmLengthCalibrationCandidateWristWorld = FVector::ZeroVector;
	FVector ArmLengthCalibrationCandidateShoulderWorld = FVector::ZeroVector;
	float ArmLengthCalibrationCandidateReachCm = 0.0f;
	float ArmLengthCalibrationCandidateBelowShoulderCm = 0.0f;
	float ArmLengthCalibrationCandidateVerticalDominance = 0.0f;
	double ArmLengthCalibrationCandidateTimeSeconds = -1.0;
	bool bHasArmLengthCalibrationForwardReach = false;
	float ArmLengthCalibrationForwardReachCm = 0.0f;
	bool bHasArmLengthCalibrationDownSample = false;
	float ArmLengthCalibrationDownDropCm = 0.0f;
	float ArmLengthCalibrationDownReachCm = 0.0f;
	bool bHasArmLengthCalibrationLastSample = false;
	FVector ArmLengthCalibrationLastWristWorld = FVector::ZeroVector;
	double ArmLengthCalibrationLastSampleTimeSeconds = -1.0;
	float ArmLengthCalibrationLastVelocityCmSec = 0.0f;
	bool bHasHmdRelativeReachContinuity = false;
	float HmdRelativeReachContinuityCm = 0.0f;
	double HmdRelativeReachContinuityTimeSeconds = -1.0;
	bool bHasLastTrackedQuestArmPose = false;
	FVector LastTrackedQuestArmShoulderWorld = FVector::ZeroVector;
	FVector LastTrackedQuestArmElbowWorld = FVector::ZeroVector;
	FVector LastTrackedQuestArmWristWorld = FVector::ZeroVector;
	float LastTrackedQuestArmReachCm = 0.0f;
	float LastTrackedQuestArmBelowShoulderCm = 0.0f;
	float LastTrackedQuestArmDownDominance = 0.0f;
	double LastTrackedQuestArmTimeSeconds = -1.0;
	bool bDropoutDownFallbackActive = false;
	FVector DropoutDownFallbackWristWorld = FVector::ZeroVector;
	FVector DropoutDownFallbackElbowWorld = FVector::ZeroVector;
	double DropoutDownFallbackLastUpdateTimeSeconds = -1.0;
	double DropoutReacquireReachScaleSuppressUntilTimeSeconds = -1.0;
	float PositionAuthorityAlpha = 0.0f;
	double PositionAuthorityLastTimeSeconds = -1.0;
	double LastHandRotationApplyTimeSeconds = -1.0;

	void ResetRotationCalibration();
	void ResetPositionContinuity(bool bResetArmLengthCalibration = true);
	void Reset();
	void ResetCalibration();
};

struct FQuestWristRuntimeState
{
	EQuestMediaSpaceCalibrationMode CalibrationMode = EQuestMediaSpaceCalibrationMode::None;
	bool bHasHmdRelativeAvatarCalibration = false;
	FVector HmdRelativeQuestAnchorWorld = FVector::ZeroVector;
	FQuat HmdRelativeQuestAnchorYawWorld = FQuat::Identity;
	bool bHasHmdRelativeQuestTranslationFilter = false;
	FVector HmdRelativeQuestFilteredAnchorWorld = FVector::ZeroVector;
	FVector HmdRelativeQuestLastRawAnchorWorld = FVector::ZeroVector;
	double HmdRelativeQuestAnchorLastTimeSeconds = -1.0;
	uint8 ArmLengthCalibrationStage = QuestArmLengthCalibrationStage_WaitingForHands;
	int32 ArmLengthCalibrationStableFrameCount = 0;
	float ArmLengthCalibrationStableSeconds = 0.0f;
	double ArmLengthCalibrationLastUpdateTimeSeconds = -1.0;
	double ArmLengthCalibrationLastLogTimeSeconds = -1.0;
	double ArmLengthCalibrationAcceptedTimeSeconds = -1.0;
	FQuestWristSideRuntimeState Left;
	FQuestWristSideRuntimeState Right;

	void Reset();
	void ResetCalibration();
};

struct FQuestWristCalibrationSoftRejectSettings
{
	bool bEnableSoftGate = false;
	float HandLossPauseSeconds = 0.0f;
	float SoftRejectDecayRate = 0.0f;
	float RequiredStableSeconds = 0.0f;
	int32 RequiredStableFrames = 1;
};

struct MEDIAPIPEDRIVER_API FMediaPipeQuestWristCalibrationState
{
	static void WriteTrace(const FQuestWristSideRuntimeState& State, FQuestHandRotationTrace& Trace);
	static void ResetMeasurement(FQuestWristSideRuntimeState& State, uint8 Reason, FQuestHandRotationTrace* Trace = nullptr);
	static bool IsSoftRejectReason(uint8 Reason);
	static void SoftRejectMeasurement(
		FQuestWristSideRuntimeState& State,
		uint8 Reason,
		const FQuestWristCalibrationSoftRejectSettings& Settings,
		float DeltaSeconds,
		double NowSeconds,
		FQuestHandRotationTrace* Trace = nullptr);
};
