#pragma once

#include "CoreMinimal.h"

struct FQuestHandRotationTrace;
struct FQuestHandTrackingSnapshot;
struct FQuestWristMappingTrace;

struct MEDIAPIPEDRIVER_API FMediaPipeQuestHandDivergenceFormatInput
{
	FName TargetActorName;
	bool bIsLeft = false;
	FVector QuestExpectedForwardComp = FVector::ZeroVector;
	FVector QuestExpectedUpComp = FVector::ZeroVector;
	FVector RollTargetForwardComp = FVector::ZeroVector;
	FVector RollTargetUpComp = FVector::ZeroVector;
	FVector MannyAppliedForwardComp = FVector::ZeroVector;
	FVector MannyAppliedUpComp = FVector::ZeroVector;
	FVector MediaPipeHandForwardComp = FVector::ZeroVector;
	FVector MediaPipeHandUpComp = FVector::ZeroVector;
	FVector QuestBasisForwardComp = FVector::ZeroVector;
	FVector QuestBasisUpComp = FVector::ZeroVector;
	FVector RollTargetBasisForwardComp = FVector::ZeroVector;
	FVector RollTargetBasisUpComp = FVector::ZeroVector;
	FVector MannyAppliedBasisForwardComp = FVector::ZeroVector;
	FVector MannyAppliedBasisUpComp = FVector::ZeroVector;
	FVector MediaPipeBasisForwardComp = FVector::ZeroVector;
	FVector MediaPipeBasisUpComp = FVector::ZeroVector;
	float QuestExpectedToMannyDeg = 0.0f;
	float QuestExpectedToRollTargetDeg = 0.0f;
	float RollTargetToMannyDeg = 0.0f;
	float QuestExpectedForwardErrDeg = 0.0f;
	float QuestExpectedUpErrDeg = 0.0f;
	float RollTargetForwardErrDeg = 0.0f;
	float RollTargetUpErrDeg = 0.0f;
	float QuestBasisToMannyBasisForwardErrDeg = 0.0f;
	float QuestBasisToMannyBasisUpErrDeg = 0.0f;
	float QuestBasisToRollBasisForwardErrDeg = 0.0f;
	float QuestBasisToRollBasisUpErrDeg = 0.0f;
	float RawTwistDeg = 0.0f;
	uint8 SemanticRollAxisIndex = 0;
	float SemanticRollAxisScore = 0.0f;

	static FMediaPipeQuestHandDivergenceFormatInput FromTrace(
		FName TargetActorName,
		bool bIsLeft,
		const FQuestHandRotationTrace& Trace);
};

struct MEDIAPIPEDRIVER_API FMediaPipeQuestWristRollCompactFormatInput
{
	FName TargetActorName;
	bool bIsLeft = false;
	bool bQuestHandRotationApplied = false;
	bool bArmIKBranchEntered = false;
	bool bForceArmIK = false;
	bool bQuestTracked = false;
	bool bQuestHandBasisMapped = false;
	bool bUsedSemanticRoll = false;
	bool bUsedForearmLocalSemanticRoll = false;
	bool bUsedSemanticBasisDelta = false;
	bool bUsedPalmRollFallback = false;
	bool bHeldPalmRoll = false;
	bool bAppliedHandLocalToLowerArm = false;
	bool bAppliedTwistCorrection = false;
	bool bLowerArmMainDriven = false;
	bool bHadCalibration = false;
	bool bSetCalibration = false;
	bool bCalibrationRejected = false;
	bool bTwistLimitClamped = false;
	bool bForearmLimitClamped = false;
	bool bForearmRateClamped = false;
	bool bUpperArmRateClamped = false;
	bool bCalibratedHandDeltaValid = false;
	uint8 CalibrationState = 0;
	uint8 CalibrationRejectReason = 0;
	uint8 SemanticRollAxisIndex = 0;
	uint8 AnatomicalRollAxisIndex = 0;
	int32 CalibrationStableFrameCount = 0;
	float CalibrationBasisErrorDeg = 0.0f;
	float CalibrationNeutralTwistDeg = 0.0f;
	float SemanticRollAxisScore = 0.0f;
	float RawTwistDeg = 0.0f;
	float LimitedTwistDeg = 0.0f;
	float SourceHandTwistDeg = 0.0f;
	float RawSwingDeg = 0.0f;
	float AppliedSwingDeg = 0.0f;
	float TargetForearmTwistDeg = 0.0f;
	float SmoothedForearmTwistDeg = 0.0f;
	float ForearmTwistStepDeg = 0.0f;
	float ForearmTwistMaxStepDeg = 0.0f;
	float ForearmTwistVelocityDegPerSec = 0.0f;
	float ForearmTwistHelperScale = 0.0f;
	float ForearmTwistApplied01Deg = 0.0f;
	float ForearmTwistApplied02Deg = 0.0f;
	float TargetUpperArmTwistDeg = 0.0f;
	float SmoothedUpperArmTwistDeg = 0.0f;
	float UpperArmTwistStepDeg = 0.0f;
	float UpperArmTwistMaxStepDeg = 0.0f;
	float UpperArmTwistHelperScale = 0.0f;
	float UpperArmTwistApplied01Deg = 0.0f;
	float UpperArmTwistApplied02Deg = 0.0f;
	float CalibratedHandDeltaDeg = 0.0f;
	float QuestHandRotationDeltaDeg = 0.0f;
	float QuestExpectedToMannyDeg = 0.0f;
	float QuestExpectedToRollTargetDeg = 0.0f;
	float RollTargetToMannyDeg = 0.0f;
	float QuestExpectedForwardErrDeg = 0.0f;
	float QuestExpectedUpErrDeg = 0.0f;
	float RollTargetForwardErrDeg = 0.0f;
	float RollTargetUpErrDeg = 0.0f;
	float QuestBasisToMannyBasisForwardErrDeg = 0.0f;
	float QuestBasisToMannyBasisUpErrDeg = 0.0f;
	float QuestBasisToRollBasisForwardErrDeg = 0.0f;
	float QuestBasisToRollBasisUpErrDeg = 0.0f;

	static FMediaPipeQuestWristRollCompactFormatInput FromTrace(
		FName TargetActorName,
		bool bIsLeft,
		bool bQuestHandRotationApplied,
		bool bArmIKBranchEntered,
		bool bForceArmIK,
		float ForearmTwistVelocityDegPerSec,
		float QuestHandRotationDeltaDeg,
		const FQuestHandRotationTrace& Trace);
};

struct MEDIAPIPEDRIVER_API FMediaPipeQuestWristHudFormatResult
{
	FString Text;
	FColor Color = FColor::Yellow;
};

struct MEDIAPIPEDRIVER_API FMediaPipeQuestWristSolveLogFormatInput
{
	FName TargetActorName;
	bool bIsLeft = false;
	int32 QuestArmMode = 0;
	// Which source owned this arm for the frame (bodyFusion/chainDirect/cameraRescue/
	// questWrist/mediaPipe). The wrist-position fields below are all zeros whenever the
	// owner is not questWrist - without this field those rows read as a dead hand stream.
	FString ArmOwner = TEXT("unknown");
	bool bRequireTrackedForApply = false;
	bool bArmIKBranchEntered = false;
	bool bForceArmIK = false;
	bool bWouldEnterArmIKIfApplied = false;
	bool bUseArmIK = false;
	FVector LoggedArmIKTargetComp = FVector::ZeroVector;
	FVector LoggedArmIKWristComp = FVector::ZeroVector;
	FName HandBoneName;
	float QuestHandRotationBlend = 0.0f;
	bool bQuestHandRotationApplied = false;
	float QuestHandRotationDeltaDeg = 0.0f;
	FVector ShoulderWorld = FVector::ZeroVector;
	FVector ElbowWorld = FVector::ZeroVector;
	FVector HandBeforeLocationCS = FVector::ZeroVector;
	FVector HandAfterLocationCS = FVector::ZeroVector;
	const FQuestWristMappingTrace* WristTrace = nullptr;
	const FQuestHandRotationTrace* HandTrace = nullptr;
};

struct MEDIAPIPEDRIVER_API FMediaPipeQuestWristCalibrationSideFormatInput
{
	bool bTracked = false;
	uint8 CalibrationState = 0;
	uint8 CalibrationRejectReason = 0;
	int32 StableFrameCount = 0;
	float BasisErrorDeg = 0.0f;
	float NeutralTwistDeg = 0.0f;

	FMediaPipeQuestWristCalibrationSideFormatInput() = default;
	FMediaPipeQuestWristCalibrationSideFormatInput(
		bool bInTracked,
		uint8 InCalibrationState,
		uint8 InCalibrationRejectReason,
		int32 InStableFrameCount,
		float InBasisErrorDeg,
		float InNeutralTwistDeg);
};

struct MEDIAPIPEDRIVER_API FMediaPipeQuestWristSideHudFormatInput
{
	bool bIsLeft = false;
	bool bQuestHandRotationApplied = false;
	bool bArmIKBranchEntered = false;
	bool bForceArmIK = false;
	bool bQuestAvailable = false;
	bool bQuestTracked = false;
	bool bQuestHandBasisMapped = false;
	bool bUsedSemanticRoll = false;
	bool bUsedForearmLocalSemanticRoll = false;
	bool bUsedAnatomicalRollAxis = false;
	uint8 CalibrationState = 0;
	uint8 CalibrationRejectReason = 0;
	uint8 SemanticRollAxisIndex = 0;
	uint8 AnatomicalRollAxisIndex = 0;
	int32 CalibrationStableFrameCount = 0;
	float CalibrationBasisErrorDeg = 0.0f;
	float CalibrationNeutralTwistDeg = 0.0f;
	float SemanticRollAxisScore = 0.0f;
	float RawTwistDeg = 0.0f;
	float AppliedSwingDeg = 0.0f;

	static FMediaPipeQuestWristSideHudFormatInput FromTrace(
		bool bIsLeft,
		bool bQuestHandRotationApplied,
		bool bArmIKBranchEntered,
		bool bForceArmIK,
		const FQuestHandRotationTrace& Trace);
};

struct MEDIAPIPEDRIVER_API FMediaPipeMetaHumanArmSanityFormatInput
{
	FName TargetActorName;
	bool bIsLeft = false;
	bool bBroken = false;
	FString Reasons = TEXT("ok");
	bool bHasPosedArm = false;
	int32 QuestArmMode = 0;
	bool bTargetMapped = false;
	bool bPositionApplied = false;
	bool bQuestTracked = false;
	bool bQuestHandRotationApplied = false;
	bool bHandLocal = false;
	bool bReachClamped = false;
	bool bConstrainedArmSolveApplied = false;
	bool bArmIKBranchEntered = false;
	float WristTargetErrorCm = 0.0f;
	float MappedWristErrorCm = 0.0f;
	float MaxWristErrorCm = 0.0f;
	float UpperLenCm = 0.0f;
	float RefUpperLenCm = 0.0f;
	float UpperLenErrCm = 0.0f;
	float LowerLenCm = 0.0f;
	float RefLowerLenCm = 0.0f;
	float LowerLenErrCm = 0.0f;
	float ElbowBendDeg = 0.0f;
	float MinElbowBendDeg = 0.0f;
	float TargetReachCm = 0.0f;
	float PosedReachCm = 0.0f;
	float HandRotErrorDeg = 0.0f;
	float MaxHandRotErrorDeg = 0.0f;
	float BasisForwardErrorDeg = 0.0f;
	float BasisUpErrorDeg = 0.0f;
	float MaxBasisErrorDeg = 0.0f;
	float SwingAppliedDeg = 0.0f;
	float MaxSwingDeg = 0.0f;
	float TwistLimitedDeg = 0.0f;
	FVector PosedShoulderWorld = FVector::ZeroVector;
	FVector PosedElbowWorld = FVector::ZeroVector;
	FVector PosedHandWorld = FVector::ZeroVector;
	FVector FinalWristWorld = FVector::ZeroVector;
	FVector MappedWristWorld = FVector::ZeroVector;
	FVector SolveShoulderWorld = FVector::ZeroVector;
	FVector SolveElbowWorld = FVector::ZeroVector;

	static FMediaPipeMetaHumanArmSanityFormatInput FromTraces(
		FName TargetActorName,
		bool bIsLeft,
		bool bBroken,
		const FString& Reasons,
		bool bHasPosedArm,
		int32 QuestArmMode,
		bool bQuestHandRotationApplied,
		bool bArmIKBranchEntered,
		float WristTargetErrorCm,
		float MappedWristErrorCm,
		float MaxWristErrorCm,
		float UpperLenCm,
		float RefUpperLenCm,
		float UpperLenErrCm,
		float LowerLenCm,
		float RefLowerLenCm,
		float LowerLenErrCm,
		float ElbowBendDeg,
		float MinElbowBendDeg,
		float TargetReachCm,
		float PosedReachCm,
		float MaxHandRotErrorDeg,
		float MaxBasisErrorDeg,
		float MaxSwingDeg,
		const FQuestWristMappingTrace& WristTrace,
		const FQuestHandRotationTrace& HandTrace,
		const FVector& PosedShoulderWorld,
		const FVector& PosedElbowWorld,
		const FVector& PosedHandWorld,
		const FVector& SolveShoulderWorld,
		const FVector& SolveElbowWorld);
};

class MEDIAPIPEDRIVER_API FMediaPipeQuestWristDiagnosticFormatter
{
public:
	static FString FormatQuestWristSnapshotLog(
		FName TargetActorName,
		const FQuestHandTrackingSnapshot& Snapshot,
		bool bHasCachedQuestHmdPose,
		const FVector& CachedQuestHmdWorld);
	static FString FormatQuestWristReplaySnapshotLog(const FString& ReplayPath);
	static FString FormatQuestWristSolveLog(const FMediaPipeQuestWristSolveLogFormatInput& Input);
	static FString FormatQuestHandDivergence(const FMediaPipeQuestHandDivergenceFormatInput& Input);
	static FString FormatQuestWristRollCompact(const FMediaPipeQuestWristRollCompactFormatInput& Input);
	static FMediaPipeQuestWristHudFormatResult FormatQuestWristCalibrationHud(
		const FMediaPipeQuestWristCalibrationSideFormatInput& Left,
		const FMediaPipeQuestWristCalibrationSideFormatInput& Right);
	static FMediaPipeQuestWristHudFormatResult FormatQuestWristSideCalibrationHud(
		const FMediaPipeQuestWristSideHudFormatInput& Input);
	static FString FormatMetaHumanArmSanityLog(const FMediaPipeMetaHumanArmSanityFormatInput& Input);
	static FMediaPipeQuestWristHudFormatResult FormatMetaHumanArmSanityHud(
		const FMediaPipeMetaHumanArmSanityFormatInput& Input);
	static FMediaPipeMetaHumanArmSanityFormatInput BuildMetaHumanArmSanityInput(
		FName TargetActorName,
		bool bIsLeft,
		bool bHasPosedArm,
		int32 QuestArmMode,
		bool bQuestHandRotationApplied,
		bool bArmIKBranchEntered,
		float MaxWristErrorCm,
		float MaxHandRotErrorDeg,
		float MaxBasisErrorDeg,
		float MaxLengthErrorCm,
		float MinElbowBendDeg,
		float MaxSwingDeg,
		float RefUpperLenCm,
		float RefLowerLenCm,
		const FQuestWristMappingTrace& WristTrace,
		const FQuestHandRotationTrace& HandTrace,
		const FVector& PosedShoulderWorld,
		const FVector& PosedElbowWorld,
		const FVector& PosedHandWorld,
		const FVector& SolveShoulderWorld,
		const FVector& SolveElbowWorld);
};
