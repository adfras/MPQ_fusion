#pragma once

#include "CoreMinimal.h"
#include "MediaPipeBodySolverMath.h"
#include "MediaPipePoseDiagnostics.h"
#include "MediaPipeQuestFingerSolver.h"

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

	// Lower-body scaffold: Quest/HMD metric height fused with the monocular compression. Updated
	// once per evaluation in DrivePelvisTranslationCS and consumed by the grounded leg flexion
	// correction in DriveLegCS plus the scaffold diagnostics row.
	MediaPipeBodySolverMath::FMediaPipeHmdHeightScaffoldState HmdHeightScaffold;
	MediaPipeBodySolverMath::FMediaPipeHmdHeadYawNeutralState HmdHeadYawNeutral;
	MediaPipeBodySolverMath::FMediaPipeHmdHeadYawNeutralState HipTwistNeutral;
	MediaPipeBodySolverMath::FMediaPipeHipYawEstimatorState HipYawEstimator;
	bool bHasHmdLeanNeutral = false;
	FVector2D HmdLeanNeutralXY = FVector2D::ZeroVector;
	// Body yaw reference: the head-yaw neutral at session start. The slow neutral component of
	// the HMD yaw is the wearer's body facing; its drift from this initial value turns the
	// pelvis while the fast remainder stays head glance.
	bool bHasInitialBodyYawNeutral = false;
	float InitialBodyYawNeutralDeg = 0.0f;
	// Quest body-tracking hips yaw. The yaw is the horizontal heading drift of one of the hips
	// joint's local axes (chosen as the most horizontal at latch time), which is immune to the
	// pitch/roll of bends. The neutral heading latches only after the heading has been stable,
	// so headset-donning noise never becomes the zero point, and it latches RELATIVE to the
	// currently applied body yaw so re-acquisitions never step the pelvis.
	bool bBodyTrackingYawLatched = false;
	int32 BodyTrackingHipsAxisIndex = 0;
	float BodyTrackingHipsNeutralHeadingDeg = 0.0f;
	bool bHasBodyTrackingCandidate = false;
	float BodyTrackingHipsCandidateHeadingDeg = 0.0f;
	float BodyTrackingHipsStableSeconds = 0.0f;
	double LastBodyTrackingHipsSampleSeconds = 0.0;
	float LastBodyTrackingYawDeg = 0.0f;
	// Slow drift recenter: when the measured yaw stays small for a sustained stretch the wearer
	// is facing forward, so the neutral heading may creep toward the current heading to absorb
	// IOBT tracking-space drift. Held twists (large yaw) never qualify.
	float BodyYawRecenterStillSeconds = 0.0f;
	// Lateral hip sway from the body-tracking hips joint POSITION against its own neutral
	// (latched after the donning gate opens). World-planar, applied as a pelvis translation.
	bool bHasBodyTrackingHipsNeutralPos = false;
	FVector BodyTrackingHipsNeutralPosWorld = FVector::ZeroVector;
	double LastBodyTrackingHipsPosSampleSeconds = 0.0;
	FVector2D BodyTrackingSwayWorldXY = FVector2D::ZeroVector;
	bool bBodyTrackingSwayActive = false;
	// Unified body yaw: every source feeds this rate-limited state, so source switches and
	// tracking dropouts walk the pelvis instead of snapping it.
	bool bHasSmoothedBodyYaw = false;
	float SmoothedBodyYawDeg = 0.0f;
	uint8 PrevBodyYawSource = 0;
	// Live-trial donning gate. Pressing VR Preview happens bent over a desk holding the headset,
	// so nothing observed before the headset is worn and still may become a neutral reference.
	// The gate arms when an HMD pose first appears, passes once the worn headset has been
	// upright and still for a moment, and at that instant every live neutral re-zeros to the
	// wearer's settled standing pose. One-way per session: later bends or dropouts never re-gate.
	bool bLiveNeutralGateArmed = false;
	bool bLiveNeutralsReady = false;
	float LiveNeutralSettleSeconds = 0.0f;
	bool bHasLiveNeutralHmdSample = false;
	FVector LastLiveNeutralHmdPos = FVector::ZeroVector;
	float LastLiveNeutralHmdYawDeg = 0.0f;
	// Diagnostics: last applied body yaw and its source (0=none 1=hmdNeutral 2=bodyTracking).
	float LastBodyYawDeg = 0.0f;
	uint8 LastBodyYawSource = 0;
	bool bHasLowerBodyScaffoldSample = false;
	bool bScaffoldHmdPoseValid = false;
	float ScaffoldHmdHeightZ = 0.0f;
	float ScaffoldHmdBaselineZ = 0.0f;
	float ScaffoldHmdHeadDropCm = 0.0f;
	float ScaffoldHmdLeanCompCm = 0.0f;
	float ScaffoldHmdAlpha01 = 1.0f;
	float ScaffoldHmdConfidence = 0.0f;
	float ScaffoldMonoAlpha01 = 1.0f;
	float ScaffoldFusedAlpha01 = 1.0f;
	float ScaffoldHmdShare01 = 0.0f;
	float ScaffoldPelvisDropCm = 0.0f;

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
		ResetLowerBodyScaffold();
		ResetDerivedSignalReferences();
	}

	void ResetLowerBodyScaffold()
	{
		HmdHeightScaffold.Reset();
		HmdHeadYawNeutral.Reset();
		HipTwistNeutral.Reset();
		HipYawEstimator.Reset();
		bHasHmdLeanNeutral = false;
		HmdLeanNeutralXY = FVector2D::ZeroVector;
		bHasInitialBodyYawNeutral = false;
		InitialBodyYawNeutralDeg = 0.0f;
		bBodyTrackingYawLatched = false;
		BodyTrackingHipsAxisIndex = 0;
		BodyTrackingHipsNeutralHeadingDeg = 0.0f;
		bHasBodyTrackingCandidate = false;
		BodyTrackingHipsCandidateHeadingDeg = 0.0f;
		BodyTrackingHipsStableSeconds = 0.0f;
		LastBodyTrackingHipsSampleSeconds = 0.0;
		LastBodyTrackingYawDeg = 0.0f;
		BodyYawRecenterStillSeconds = 0.0f;
		bHasBodyTrackingHipsNeutralPos = false;
		BodyTrackingHipsNeutralPosWorld = FVector::ZeroVector;
		LastBodyTrackingHipsPosSampleSeconds = 0.0;
		BodyTrackingSwayWorldXY = FVector2D::ZeroVector;
		bBodyTrackingSwayActive = false;
		bHasSmoothedBodyYaw = false;
		SmoothedBodyYawDeg = 0.0f;
		PrevBodyYawSource = 0;
		bLiveNeutralGateArmed = false;
		bLiveNeutralsReady = false;
		LiveNeutralSettleSeconds = 0.0f;
		bHasLiveNeutralHmdSample = false;
		LastLiveNeutralHmdPos = FVector::ZeroVector;
		LastLiveNeutralHmdYawDeg = 0.0f;
		LastBodyYawDeg = 0.0f;
		LastBodyYawSource = 0;
		bHasLowerBodyScaffoldSample = false;
		bScaffoldHmdPoseValid = false;
		ScaffoldHmdHeightZ = 0.0f;
		ScaffoldHmdBaselineZ = 0.0f;
		ScaffoldHmdHeadDropCm = 0.0f;
		ScaffoldHmdLeanCompCm = 0.0f;
		ScaffoldHmdAlpha01 = 1.0f;
		ScaffoldHmdConfidence = 0.0f;
		ScaffoldMonoAlpha01 = 1.0f;
		ScaffoldFusedAlpha01 = 1.0f;
		ScaffoldHmdShare01 = 0.0f;
		ScaffoldPelvisDropCm = 0.0f;
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
	// Lowest observed source HEEL height; the heel landmark sits high on the ankle, so heel
	// raises must be measured against the heel's own floor, not the toe-dominated source floor.
	bool bHasObservedHeelFloor = false;
	float ObservedHeelFloorZ = 0.0f;
	bool bCurrentSourceFootGrounded = false;
	bool bCurrentSourceFootNearFloor = false;
	bool bHasSmoothedThighRotCS = false;
	FQuat SmoothedThighRotCS = FQuat::Identity;
	bool bHasSmoothedCalfRotCS = false;
	FQuat SmoothedCalfRotCS = FQuat::Identity;
	bool bHasSmoothedFootRotCS = false;
	FQuat SmoothedFootRotCS = FQuat::Identity;

	// Scaffold diagnostics (per evaluated frame; no pose authority).
	bool bLastFlexionAdjustApplied = false;
	float LastFlexionMeasuredDeg = 0.0f;
	float LastFlexionTargetDeg = 0.0f;
	float LastFlexionAppliedDeltaDeg = 0.0f;
	float LastFootLiftCm = 0.0f;
	float LastBendRedistributionDeg = 0.0f;
	float LastShinTiltDeg = 0.0f;
	float LastFootPitchAppliedDeg = 0.0f;
	float LastFootExtraDownPitchDeg = 0.0f;

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
		bHasObservedHeelFloor = false;
		ObservedHeelFloorZ = 0.0f;
		bCurrentSourceFootGrounded = false;
		bCurrentSourceFootNearFloor = false;
		bLastFlexionAdjustApplied = false;
		LastFlexionMeasuredDeg = 0.0f;
		LastFlexionTargetDeg = 0.0f;
		LastFlexionAppliedDeltaDeg = 0.0f;
		LastFootLiftCm = 0.0f;
		LastBendRedistributionDeg = 0.0f;
		LastShinTiltDeg = 0.0f;
		LastFootPitchAppliedDeg = 0.0f;
		LastFootExtraDownPitchDeg = 0.0f;
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
	MediaPipeQuestFingerSolver::FMediaPipeQuestHandPoseGateState PoseGate;

	void Reset()
	{
		PoseGate.Reset();
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
	double LastQuestFingerJoFallbackLogTimeSecondsL = -1.0;
	double LastQuestFingerJoFallbackLogTimeSecondsR = -1.0;
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
		LastQuestFingerJoFallbackLogTimeSecondsL = -1.0;
		LastQuestFingerJoFallbackLogTimeSecondsR = -1.0;
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
