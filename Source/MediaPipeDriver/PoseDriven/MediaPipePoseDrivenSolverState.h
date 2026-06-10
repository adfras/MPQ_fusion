#pragma once

#include "CoreMinimal.h"
#include "MediaPipePoseDiagnostics.h"

static constexpr int32 MediaPipeQuestStateFingerBoneCount = 19;
static constexpr int32 MediaPipeQuestStateMetacarpalOffset = 15;
static constexpr int32 MediaPipeArmStandardTwistHelperCount = 4;
static constexpr int32 MediaPipeMetaHumanClavicleHelperCount = 3;
static constexpr int32 MediaPipeMetaHumanUpperArmHelperCount = 9;
static constexpr int32 MediaPipeMetaHumanLowerArmHelperCount = 7;
static constexpr int32 MediaPipeMetaHumanArmDeformationHelperCount =
	MediaPipeMetaHumanClavicleHelperCount +
	MediaPipeMetaHumanUpperArmHelperCount +
	MediaPipeMetaHumanLowerArmHelperCount;

struct FMediaPipeBodySolverState
{
	float ReferenceRigHipHeightCm = 0.0f;
	bool bHasReferenceHipHeight = false;
	float ReferenceHipHeightCm = 0.0f;
	bool bHasSmoothedPelvisOffset = false;
	FVector SmoothedPelvisOffsetComp = FVector::ZeroVector;
	bool bHasSmoothedStage1ChestOffset = false;
	FVector SmoothedStage1ChestOffsetComp = FVector::ZeroVector;
	bool bHasSmoothedStage2ClavicleLiftL = false;
	float SmoothedStage2ClavicleLiftCmL = 0.0f;
	bool bHasStage2NeutralReferenceL = false;
	float Stage2NeutralShoulderLiftFromPelvisCmL = 0.0f;
	float Stage2NeutralShoulderHeadClearanceCmL = 0.0f;
	float Stage2NeutralObservationSecondsL = 0.0f;
	int32 Stage2NeutralObservationFramesL = 0;
	bool bHasSmoothedStage2ClavicleLiftR = false;
	float SmoothedStage2ClavicleLiftCmR = 0.0f;
	bool bHasStage2NeutralReferenceR = false;
	float Stage2NeutralShoulderLiftFromPelvisCmR = 0.0f;
	float Stage2NeutralShoulderHeadClearanceCmR = 0.0f;
	float Stage2NeutralObservationSecondsR = 0.0f;
	int32 Stage2NeutralObservationFramesR = 0;
	bool bHasSmoothedFkRootGroundOffset = false;
	FVector SmoothedFkRootGroundOffsetComp = FVector::ZeroVector;
	bool bHasStableTorsoForwardWorld = false;
	FVector StableTorsoForwardWorld = FVector::ZeroVector;
	bool bHasStableTorsoUpWorld = false;
	FVector StableTorsoUpWorld = FVector::ZeroVector;

	bool bHasSmoothedPelvisRotCS = false;
	FQuat SmoothedPelvisRotCS = FQuat::Identity;
	bool bHasSmoothedSpineRotCS[5] = { false, false, false, false, false };
	FQuat SmoothedSpineRotCS[5] = { FQuat::Identity, FQuat::Identity, FQuat::Identity, FQuat::Identity, FQuat::Identity };
	bool bHasSmoothedNeckRotCS = false;
	FQuat SmoothedNeckRotCS = FQuat::Identity;
	bool bHasSmoothedNeck02RotCS = false;
	FQuat SmoothedNeck02RotCS = FQuat::Identity;
	bool bHasSmoothedHeadRotCS = false;
	FQuat SmoothedHeadRotCS = FQuat::Identity;
	bool bHasHeadScreenReference = false;
	FVector2D HeadScreenCenterReference = FVector2D::ZeroVector;
	FVector2D HeadScreenNoseReference = FVector2D::ZeroVector;
	FVector2D HeadScreenShoulderNoseReference = FVector2D::ZeroVector;
	bool bHasBilateralShoulderHeadClearanceReference = false;
	float BilateralShoulderHeadClearanceReferenceCm = 0.0f;
	int64 LastBilateralShoulderHeadClearanceReferencePoseTimestampUs = -1;
	float HeadScreenNoseEyePitchReference = 0.0f;
	float HeadScreenMouthEyePitchReference = 0.0f;
	float HeadScreenMouthEarPitchReference = 0.0f;
	float HeadScreenNoseEarPitchReference = 0.0f;
	float HeadWorldMouthEyePitchReference = 0.0f;
	float HeadWorldNoseEyePitchReference = 0.0f;
	float HeadWorldMouthEarPitchReference = 0.0f;
	float HeadWorldNoseEarPitchReference = 0.0f;
	float HeadWorldForwardPitchReferenceDeg = 0.0f;
	float HeadScreenLateralAngleReferenceDeg = 0.0f;
	float HeadScreenRollReferenceDeg = 0.0f;

	void ResetDerivedSignalReferences()
	{
		bHasHeadScreenReference = false;
		HeadScreenCenterReference = FVector2D::ZeroVector;
		HeadScreenNoseReference = FVector2D::ZeroVector;
		HeadScreenShoulderNoseReference = FVector2D::ZeroVector;
		bHasBilateralShoulderHeadClearanceReference = false;
		BilateralShoulderHeadClearanceReferenceCm = 0.0f;
		LastBilateralShoulderHeadClearanceReferencePoseTimestampUs = -1;
		HeadScreenNoseEyePitchReference = 0.0f;
		HeadScreenMouthEyePitchReference = 0.0f;
		HeadScreenMouthEarPitchReference = 0.0f;
		HeadScreenNoseEarPitchReference = 0.0f;
		HeadWorldMouthEyePitchReference = 0.0f;
		HeadWorldNoseEyePitchReference = 0.0f;
		HeadWorldMouthEarPitchReference = 0.0f;
		HeadWorldNoseEarPitchReference = 0.0f;
		HeadWorldForwardPitchReferenceDeg = 0.0f;
		HeadScreenLateralAngleReferenceDeg = 0.0f;
		HeadScreenRollReferenceDeg = 0.0f;
	}

	void ResetTracking()
	{
		bHasReferenceHipHeight = false;
		ReferenceHipHeightCm = 0.0f;
		bHasSmoothedPelvisOffset = false;
		SmoothedPelvisOffsetComp = FVector::ZeroVector;
		bHasSmoothedStage1ChestOffset = false;
		SmoothedStage1ChestOffsetComp = FVector::ZeroVector;
		bHasSmoothedStage2ClavicleLiftL = false;
		SmoothedStage2ClavicleLiftCmL = 0.0f;
		bHasStage2NeutralReferenceL = false;
		Stage2NeutralShoulderLiftFromPelvisCmL = 0.0f;
		Stage2NeutralShoulderHeadClearanceCmL = 0.0f;
		Stage2NeutralObservationSecondsL = 0.0f;
		Stage2NeutralObservationFramesL = 0;
		bHasSmoothedStage2ClavicleLiftR = false;
		SmoothedStage2ClavicleLiftCmR = 0.0f;
		bHasStage2NeutralReferenceR = false;
		Stage2NeutralShoulderLiftFromPelvisCmR = 0.0f;
		Stage2NeutralShoulderHeadClearanceCmR = 0.0f;
		Stage2NeutralObservationSecondsR = 0.0f;
		Stage2NeutralObservationFramesR = 0;
		bHasSmoothedFkRootGroundOffset = false;
		SmoothedFkRootGroundOffsetComp = FVector::ZeroVector;
		ResetDerivedSignalReferences();
	}

	void ResetTorsoStability()
	{
		bHasStableTorsoForwardWorld = false;
		StableTorsoForwardWorld = FVector::ZeroVector;
		bHasStableTorsoUpWorld = false;
		StableTorsoUpWorld = FVector::ZeroVector;
	}

	void ResetRotationSmoothing()
	{
		bHasSmoothedPelvisRotCS = false;
		SmoothedPelvisRotCS = FQuat::Identity;
		for (int32 Index = 0; Index < 5; ++Index)
		{
			bHasSmoothedSpineRotCS[Index] = false;
			SmoothedSpineRotCS[Index] = FQuat::Identity;
		}
		bHasSmoothedNeckRotCS = false;
		SmoothedNeckRotCS = FQuat::Identity;
		bHasSmoothedNeck02RotCS = false;
		SmoothedNeck02RotCS = FQuat::Identity;
		bHasSmoothedHeadRotCS = false;
		SmoothedHeadRotCS = FQuat::Identity;
	}
};

struct FMediaPipeLegSolverState
{
	bool bHasSmoothedLegPlane = false;
	FVector SmoothedLegPlaneComp = FVector::UpVector;
	bool bHasPrevFootSample = false;
	FVector PrevAnkleWorld = FVector::ZeroVector;
	FVector PrevHeelWorld = FVector::ZeroVector;
	FVector PrevToeWorld = FVector::ZeroVector;
	bool bHasStableFootForwardWorld = false;
	FVector StableFootForwardWorld = FVector::ZeroVector;
	bool bHasStableFootRotationForwardWorld = false;
	FVector StableFootRotationForwardWorld = FVector::ZeroVector;
	bool bFootPlantLocked = false;
	int32 FootPlantCandidateFrames = 0;
	FVector LockedAnkleWorld = FVector::ZeroVector;
	bool bHasObservedSourceFloor = false;
	float ObservedSourceFloorZ = 0.0f;
	bool bCurrentSourceFootGrounded = false;
	bool bHasSmoothedThighRotCS = false;
	FQuat SmoothedThighRotCS = FQuat::Identity;
	bool bHasSmoothedCalfRotCS = false;
	FQuat SmoothedCalfRotCS = FQuat::Identity;
	bool bHasSmoothedFootRotCS = false;
	FQuat SmoothedFootRotCS = FQuat::Identity;

	void ResetFootPlant()
	{
		bHasPrevFootSample = false;
		PrevAnkleWorld = FVector::ZeroVector;
		PrevHeelWorld = FVector::ZeroVector;
		PrevToeWorld = FVector::ZeroVector;
		bHasStableFootForwardWorld = false;
		StableFootForwardWorld = FVector::ZeroVector;
		bHasStableFootRotationForwardWorld = false;
		StableFootRotationForwardWorld = FVector::ZeroVector;
		bFootPlantLocked = false;
		FootPlantCandidateFrames = 0;
		LockedAnkleWorld = FVector::ZeroVector;
		bHasObservedSourceFloor = false;
		ObservedSourceFloorZ = 0.0f;
		bCurrentSourceFootGrounded = false;
	}

	void ResetRotationSmoothing()
	{
		bHasSmoothedLegPlane = false;
		SmoothedLegPlaneComp = FVector::UpVector;
		bHasSmoothedThighRotCS = false;
		SmoothedThighRotCS = FQuat::Identity;
		bHasSmoothedCalfRotCS = false;
		SmoothedCalfRotCS = FQuat::Identity;
		bHasSmoothedFootRotCS = false;
		SmoothedFootRotCS = FQuat::Identity;
	}
};

struct FMediaPipeArmSolverState
{
	bool bHasSmoothedArmIK = false;
	FVector SmoothedWristTargetComp = FVector::ZeroVector;
	FVector SmoothedPoleDirComp = FVector::UpVector;
	bool bHasSmoothedConstrainedArmElbowWorld = false;
	FVector SmoothedConstrainedArmElbowWorld = FVector::ZeroVector;
	bool bHasLastConstrainedArmSolve = false;
	FVector LastConstrainedArmShoulderWorld = FVector::ZeroVector;
	FVector LastConstrainedArmElbowWorld = FVector::ZeroVector;
	FVector LastConstrainedArmWristWorld = FVector::ZeroVector;
	double LastConstrainedArmSolveTimeSeconds = -1.0;
	bool bHasLastReliableArmSample = false;
	FVector LastReliableShoulderWorld = FVector::ZeroVector;
	FVector LastReliableElbowWorld = FVector::ZeroVector;
	FVector LastReliableWristWorld = FVector::ZeroVector;
	bool bHasShoulderHeightReference = false;
	float ShoulderHeightReferenceCm = 0.0f;
	bool bHasShoulderScreenHeightReference = false;
	float ShoulderScreenHeightReference = 0.0f;
	bool bHasShoulderHeadClearanceReference = false;
	float ShoulderHeadClearanceReferenceCm = 0.0f;
	bool bHasSmoothedClavicleShrugWeight = false;
	float SmoothedClavicleShrugWeight = 0.0f;
	bool bHasSmoothedClavicleLiftTranslation = false;
	float SmoothedClavicleLiftTranslationCm = 0.0f;
	bool bHasSmoothedClavRotCS = false;
	FQuat SmoothedClavRotCS = FQuat::Identity;
	bool bHasSmoothedUpperArmRotCS = false;
	FQuat SmoothedUpperArmRotCS = FQuat::Identity;
	bool bHasSmoothedLowerArmRotCS = false;
	FQuat SmoothedLowerArmRotCS = FQuat::Identity;
	bool bHasSmoothedHandSwingCS = false;
	FQuat SmoothedHandSwingCS = FQuat::Identity;
	bool bHasSmoothedHandTwist = false;
	float SmoothedHandTwistDeg = 0.0f;
	bool bHasLastGoodHandTarget = false;
	FQuat LastGoodHandTargetCS = FQuat::Identity;
	int32 ActiveHandTargetBranch = 0;
	int32 PendingHandTargetBranch = 0;
	int32 PendingHandTargetBranchFrames = 0;
	bool bHasSmoothedArmTwistHelperCS[MediaPipeArmStandardTwistHelperCount] = { false, false, false, false };
	FTransform SmoothedArmTwistHelperCS[MediaPipeArmStandardTwistHelperCount] =
	{
		FTransform::Identity,
		FTransform::Identity,
		FTransform::Identity,
		FTransform::Identity
	};
	bool bHasSmoothedMetaHumanArmHelperCS[MediaPipeMetaHumanArmDeformationHelperCount] = {};
	FTransform SmoothedMetaHumanArmHelperCS[MediaPipeMetaHumanArmDeformationHelperCount] = {};

	void ResetSmoothing()
	{
		bHasSmoothedArmIK = false;
		SmoothedWristTargetComp = FVector::ZeroVector;
		SmoothedPoleDirComp = FVector::UpVector;
		bHasSmoothedConstrainedArmElbowWorld = false;
		SmoothedConstrainedArmElbowWorld = FVector::ZeroVector;
		bHasLastConstrainedArmSolve = false;
		LastConstrainedArmShoulderWorld = FVector::ZeroVector;
		LastConstrainedArmElbowWorld = FVector::ZeroVector;
		LastConstrainedArmWristWorld = FVector::ZeroVector;
		LastConstrainedArmSolveTimeSeconds = -1.0;
		bHasLastReliableArmSample = false;
		LastReliableShoulderWorld = FVector::ZeroVector;
		LastReliableElbowWorld = FVector::ZeroVector;
		LastReliableWristWorld = FVector::ZeroVector;
		bHasShoulderHeightReference = false;
		ShoulderHeightReferenceCm = 0.0f;
		bHasShoulderScreenHeightReference = false;
		ShoulderScreenHeightReference = 0.0f;
		bHasShoulderHeadClearanceReference = false;
		ShoulderHeadClearanceReferenceCm = 0.0f;
		bHasSmoothedClavicleShrugWeight = false;
		SmoothedClavicleShrugWeight = 0.0f;
		bHasSmoothedClavicleLiftTranslation = false;
		SmoothedClavicleLiftTranslationCm = 0.0f;
		bHasSmoothedClavRotCS = false;
		SmoothedClavRotCS = FQuat::Identity;
		bHasSmoothedUpperArmRotCS = false;
		SmoothedUpperArmRotCS = FQuat::Identity;
		bHasSmoothedLowerArmRotCS = false;
		SmoothedLowerArmRotCS = FQuat::Identity;
		bHasSmoothedHandSwingCS = false;
		SmoothedHandSwingCS = FQuat::Identity;
		bHasSmoothedHandTwist = false;
		SmoothedHandTwistDeg = 0.0f;
		bHasLastGoodHandTarget = false;
		LastGoodHandTargetCS = FQuat::Identity;
		ActiveHandTargetBranch = 0;
		PendingHandTargetBranch = 0;
		PendingHandTargetBranchFrames = 0;
		for (int32 Index = 0; Index < MediaPipeArmStandardTwistHelperCount; ++Index)
		{
			bHasSmoothedArmTwistHelperCS[Index] = false;
			SmoothedArmTwistHelperCS[Index] = FTransform::Identity;
		}
		for (int32 Index = 0; Index < MediaPipeMetaHumanArmDeformationHelperCount; ++Index)
		{
			bHasSmoothedMetaHumanArmHelperCS[Index] = false;
			SmoothedMetaHumanArmHelperCS[Index] = FTransform::Identity;
		}
	}
};

struct FMediaPipeQuestWristSideSolverState
{
	bool bHasQuestWristCalibration = false;
	FQuat QuestWristCalibrationBasisComp = FQuat::Identity;
	bool bHasQuestWristPositionCalibration = false;
	FVector QuestWristCalibrationWorld = FVector::ZeroVector;
	FVector MediaPipeWristCalibrationWorld = FVector::ZeroVector;
	bool bHasQuestWristTraceCalibration = false;
	FVector QuestWristTraceCalibrationWorld = FVector::ZeroVector;
	double QuestWristTraceCalibrationTimeSeconds = -1.0;
	bool bHasHeldQuestWristTarget = false;
	FVector HeldQuestWristTargetWorld = FVector::ZeroVector;
	FVector HeldRawQuestWristWorld = FVector::ZeroVector;
	FVector HeldMappedQuestWristWorld = FVector::ZeroVector;
	double LastQuestWristTargetTimeSeconds = -1.0;

	void Reset()
	{
		bHasQuestWristCalibration = false;
		QuestWristCalibrationBasisComp = FQuat::Identity;
		bHasQuestWristPositionCalibration = false;
		QuestWristCalibrationWorld = FVector::ZeroVector;
		MediaPipeWristCalibrationWorld = FVector::ZeroVector;
		bHasQuestWristTraceCalibration = false;
		QuestWristTraceCalibrationWorld = FVector::ZeroVector;
		QuestWristTraceCalibrationTimeSeconds = -1.0;
		bHasHeldQuestWristTarget = false;
		HeldQuestWristTargetWorld = FVector::ZeroVector;
		HeldRawQuestWristWorld = FVector::ZeroVector;
		HeldMappedQuestWristWorld = FVector::ZeroVector;
		LastQuestWristTargetTimeSeconds = -1.0;
	}
};

struct FMediaPipeQuestWristSolverState
{
	FMediaPipeQuestWristSideSolverState Left;
	FMediaPipeQuestWristSideSolverState Right;
	bool bHasQuestMediaSpaceCalibration = false;
	EQuestMediaSpaceCalibrationMode QuestMediaSpaceCalibrationMode = EQuestMediaSpaceCalibrationMode::None;
	FQuat QuestToMediaSpaceWorld = FQuat::Identity;
	FQuat QuestCalibrationBasisWorld = FQuat::Identity;
	FQuat MediaPipeCalibrationBodyBasisWorld = FQuat::Identity;
	FVector QuestCalibrationHmdWorld = FVector::ZeroVector;
	FVector MediaPipeCalibrationHeadWorld = FVector::ZeroVector;
	FVector MediaPipeCalibrationShoulderMidWorld = FVector::ZeroVector;
	FVector MediaPipeCalibrationAnchorBodyLocal = FVector::ZeroVector;
	float QuestToMediaSpaceScale = 1.0f;
	int32 LastQuestWristManualResetSerial = 0;

	void Reset()
	{
		Left.Reset();
		Right.Reset();
		bHasQuestMediaSpaceCalibration = false;
		QuestMediaSpaceCalibrationMode = EQuestMediaSpaceCalibrationMode::None;
		QuestToMediaSpaceWorld = FQuat::Identity;
		QuestCalibrationBasisWorld = FQuat::Identity;
		MediaPipeCalibrationBodyBasisWorld = FQuat::Identity;
		QuestCalibrationHmdWorld = FVector::ZeroVector;
		MediaPipeCalibrationHeadWorld = FVector::ZeroVector;
		MediaPipeCalibrationShoulderMidWorld = FVector::ZeroVector;
		MediaPipeCalibrationAnchorBodyLocal = FVector::ZeroVector;
		QuestToMediaSpaceScale = 1.0f;
	}
};

struct FMediaPipeQuestHandSolverState
{
	bool bHasSmoothedQuestFingerRotCS[MediaPipeQuestStateFingerBoneCount] = {};
	FQuat SmoothedQuestFingerRotCS[MediaPipeQuestStateFingerBoneCount] = {};
	bool bHasQuestFingerRetargetSourceRefCS[MediaPipeQuestStateFingerBoneCount] = {};
	FQuat QuestFingerRetargetSourceRefCS[MediaPipeQuestStateFingerBoneCount] = {};
	FQuat QuestFingerRetargetTargetRefCS[MediaPipeQuestStateFingerBoneCount] = {};
	bool bHasSmoothedQuestHandRotCS = false;
	FQuat SmoothedQuestHandRotCS = FQuat::Identity;
	bool bHasSmoothedQuestHandRotLocal = false;
	FQuat SmoothedQuestHandRotLocal = FQuat::Identity;
	bool bHasSmoothedQuestForearmTwist = false;
	float SmoothedQuestForearmTwistDeg = 0.0f;
	bool bHasSmoothedQuestUpperArmTwist = false;
	float SmoothedQuestUpperArmTwistDeg = 0.0f;
	bool bHasQuestFingerAlignmentComp = false;
	FQuat QuestFingerAlignmentComp = FQuat::Identity;

	void Reset()
	{
		for (int32 Index = 0; Index < MediaPipeQuestStateFingerBoneCount; ++Index)
		{
			bHasSmoothedQuestFingerRotCS[Index] = false;
			SmoothedQuestFingerRotCS[Index] = FQuat::Identity;
			bHasQuestFingerRetargetSourceRefCS[Index] = false;
			QuestFingerRetargetSourceRefCS[Index] = FQuat::Identity;
			QuestFingerRetargetTargetRefCS[Index] = FQuat::Identity;
		}
		bHasSmoothedQuestHandRotCS = false;
		SmoothedQuestHandRotCS = FQuat::Identity;
		bHasSmoothedQuestHandRotLocal = false;
		SmoothedQuestHandRotLocal = FQuat::Identity;
		bHasSmoothedQuestForearmTwist = false;
		SmoothedQuestForearmTwistDeg = 0.0f;
		bHasSmoothedQuestUpperArmTwist = false;
		SmoothedQuestUpperArmTwistDeg = 0.0f;
		bHasQuestFingerAlignmentComp = false;
		QuestFingerAlignmentComp = FQuat::Identity;
	}
};

struct FMediaPipeDiagnosticsState
{
	double LastQuestHandDebugLogTimeSeconds = -1.0;
	double LastQuestHandHudTimeSeconds = -1.0;
	double LastQuestHandCompareLogTimeSecondsL = -1.0;
	double LastQuestHandCompareLogTimeSecondsR = -1.0;
	double LastQuestWristSolveLogTimeSecondsL = -1.0;
	double LastQuestWristSolveLogTimeSecondsR = -1.0;
	double LastQuestFingerSolveLogTimeSecondsL = -1.0;
	double LastQuestFingerSolveLogTimeSecondsR = -1.0;
	double LastTorsoDiagnosticLogTimeSeconds = -1.0;
	double LastHeadDiagnosticLogTimeSeconds = -1.0;
	double LastClavicleDiagnosticLogTimeSecondsL = -1.0;
	double LastClavicleDiagnosticLogTimeSecondsR = -1.0;
	double LastArmDiagnosticLogTimeSecondsL = -1.0;
	double LastArmDiagnosticLogTimeSecondsR = -1.0;
	double LastMetaHumanArmSanityLogTimeSecondsL = -1.0;
	double LastMetaHumanArmSanityLogTimeSecondsR = -1.0;
	double LastShoulderRollbackTraceLogTimeSecondsL = -1.0;
	double LastShoulderRollbackTraceLogTimeSecondsR = -1.0;
	double LastMetaHumanFullArmChainLogTimeSecondsL = -1.0;
	double LastMetaHumanFullArmChainLogTimeSecondsR = -1.0;
	double LastBodyFusionDebugLogTimeSeconds = -1.0;
	double LastBodyFusionPoseWriteDebugLogTimeSeconds = -1.0;
	double LastBodyFusionCalibrationLogTimeSeconds = -1.0;
	bool bHasLastShoulderRollbackUpperForwardDotL = false;
	bool bHasLastShoulderRollbackUpperForwardDotR = false;
	float LastShoulderRollbackUpperForwardDotL = 0.0f;
	float LastShoulderRollbackUpperForwardDotR = 0.0f;

	void Reset()
	{
		LastQuestHandDebugLogTimeSeconds = -1.0;
		LastQuestHandHudTimeSeconds = -1.0;
		LastQuestHandCompareLogTimeSecondsL = -1.0;
		LastQuestHandCompareLogTimeSecondsR = -1.0;
		LastQuestWristSolveLogTimeSecondsL = -1.0;
		LastQuestWristSolveLogTimeSecondsR = -1.0;
		LastQuestFingerSolveLogTimeSecondsL = -1.0;
		LastQuestFingerSolveLogTimeSecondsR = -1.0;
		LastTorsoDiagnosticLogTimeSeconds = -1.0;
		LastHeadDiagnosticLogTimeSeconds = -1.0;
		LastClavicleDiagnosticLogTimeSecondsL = -1.0;
		LastClavicleDiagnosticLogTimeSecondsR = -1.0;
		LastArmDiagnosticLogTimeSecondsL = -1.0;
		LastArmDiagnosticLogTimeSecondsR = -1.0;
		LastMetaHumanArmSanityLogTimeSecondsL = -1.0;
		LastMetaHumanArmSanityLogTimeSecondsR = -1.0;
		LastShoulderRollbackTraceLogTimeSecondsL = -1.0;
		LastShoulderRollbackTraceLogTimeSecondsR = -1.0;
		LastMetaHumanFullArmChainLogTimeSecondsL = -1.0;
		LastMetaHumanFullArmChainLogTimeSecondsR = -1.0;
		LastBodyFusionDebugLogTimeSeconds = -1.0;
		LastBodyFusionPoseWriteDebugLogTimeSeconds = -1.0;
		LastBodyFusionCalibrationLogTimeSeconds = -1.0;
		bHasLastShoulderRollbackUpperForwardDotL = false;
		bHasLastShoulderRollbackUpperForwardDotR = false;
		LastShoulderRollbackUpperForwardDotL = 0.0f;
		LastShoulderRollbackUpperForwardDotR = 0.0f;
	}
};
