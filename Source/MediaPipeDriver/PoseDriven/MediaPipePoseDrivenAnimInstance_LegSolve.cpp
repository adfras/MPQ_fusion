#include "MediaPipePoseDrivenAnimInstance.h"

#include "MediaPipeArmGuardPolicy.h"
#include "MediaPipeArmTwistSolver.h"
#include "MediaPipeBodyDiagnostics.h"
#include "MediaPipeBodyFusionDebugFormatter.h"
#include "MediaPipeBodyFusionPoseWriteContext.h"
#include "MediaPipeBodyFusionRuntime.h"
#include "MediaPipeTrackingSourceFrameBuilder.h"
#include "MediaPipeBodySolverMath.h"
#include "MediaPipeMetaHumanArmRetargeter.h"
#include "MediaPipeMetaHumanPoseAdapter.h"
#include "MediaPipePoseCoordinate.h"
#include "MediaPipePoseDiagnosticReporter.h"
#include "MediaPipePoseDiagnostics.h"
#include "MediaPipePoseFrameContinuity.h"
#include "MediaPipeRuntimeCVars.h"
#include "MediaPipeStage2ShoulderEvidence.h"
#include "MediaPipeTrackingFusionDatasetReplay.h"
#include "MediaPipeQuestHandDebugReporter.h"
#include "MediaPipeQuestHandCompareDiagnostics.h"
#include "MediaPipeQuestFingerSolver.h"
#include "MediaPipeQuestConstrainedArmSolver.h"
#include "MediaPipeQuestWristApplyPolicy.h"
#include "MediaPipeQuestWristDebugReporter.h"
#include "MediaPipeQuestWristCalibrationState.h"
#include "MediaPipeQuestWristDiagnosticFormatter.h"
#include "MediaPipeSkeletonPoseAdapter.h"
#include "MediaPipeShoulderRollbackDiagnostics.h"
#include "MediaPipePoseLog.h"
#include "MediaPipeSolvedPose.h"

#include "BonePose.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/Engine.h"
#include "GameFramework/Actor.h"
#include "HAL/CriticalSection.h"
#include "HAL/PlatformTime.h"
#include "Misc/ScopeLock.h"

#include <atomic>
#include "HAL/IConsoleManager.h"
#include "HeadMountedDisplayTypes.h"
#include "Math/RotationMatrix.h"

#include "MediaPipePoseDrivenAnimInstanceShared.h"

void FAnimNode_MediaPipePoseDriven::DriveLegCS(FCSPose<FCompactPose>& CSPose, bool bIsLeft, float DeltaSeconds)
{
	const bool bHasRef = bIsLeft ? bHasRefLegL : bHasRefLegR;
	if (!bHasRef)
	{
		return;
	}

	FVector HipWorld = FVector::ZeroVector;
	FVector KneeWorld = FVector::ZeroVector;
	FVector AnkleWorld = FVector::ZeroVector;
	FVector HeelWorld = FVector::ZeroVector;
	FVector ToeWorld = FVector::ZeroVector;
	bool bHeelMeasured = false;
	bool bFootMeasured = false;
	const bool bAvatarLockedReplay = ShouldUseAvatarLockedReplay();
	const bool bBodyFusionPoseUsableForEvaluation = ShouldUseBodyFusionPoseForEvaluation();
	const bool bUseBodyFusionLowerBody =
		bDriveLegs &&
		bBodyFusionPoseUsableForEvaluation &&
		!bAvatarLockedReplay;
	if (!bDriveLegs)
	{
		return;
	}

	FMediaPipeFusedLowerBodySide FusedLowerBodySide;
	const bool bTryGetMediaPipeLowerBodySideSucceeded =
		bBodyFusionPoseUsableForEvaluation &&
		FMediaPipeAvatarPoseWriter::TryGetMediaPipeLowerBodySide(BodyFusionFrame.Pose, bIsLeft, FusedLowerBodySide);
	bool bUsingBodyFusionLowerBody = false;
	if (bUseBodyFusionLowerBody && bTryGetMediaPipeLowerBodySideSucceeded)
	{
		HipWorld = FusedLowerBodySide.HipWorld;
		KneeWorld = FusedLowerBodySide.KneeWorld;
		AnkleWorld = FusedLowerBodySide.AnkleWorld;
		HeelWorld = FusedLowerBodySide.HeelWorld;
		ToeWorld = FusedLowerBodySide.FootWorld;
		bHeelMeasured = FusedLowerBodySide.bHasHeel;
		bFootMeasured = FusedLowerBodySide.bHasFoot;
		bUsingBodyFusionLowerBody = true;
	}
	if (!bUsingBodyFusionLowerBody)
	{
		const int32 HipLm = bIsLeft ? (int32)EMediaPipePoseLandmark::LeftHip : (int32)EMediaPipePoseLandmark::RightHip;
		const int32 KneeLm = bIsLeft ? (int32)EMediaPipePoseLandmark::LeftKnee : (int32)EMediaPipePoseLandmark::RightKnee;
		const int32 AnkleLm = bIsLeft ? (int32)EMediaPipePoseLandmark::LeftAnkle : (int32)EMediaPipePoseLandmark::RightAnkle;
		const int32 HeelLm = bIsLeft ? (int32)EMediaPipePoseLandmark::LeftHeel : (int32)EMediaPipePoseLandmark::RightHeel;
		const int32 ToeLm = bIsLeft ? (int32)EMediaPipePoseLandmark::LeftFootIndex : (int32)EMediaPipePoseLandmark::RightFootIndex;

		const bool bThighMeasured = IsMeasured(HipLm) && IsMeasured(KneeLm);
		const bool bCalfMeasured = IsMeasured(KneeLm) && IsMeasured(AnkleLm);
		bFootMeasured = IsMeasured(AnkleLm) && IsMeasured(ToeLm);
		if (!bThighMeasured || !bCalfMeasured)
		{
			return;
		}

		if (!TryGetLmWorld(HipLm, HipWorld) || !TryGetLmWorld(KneeLm, KneeWorld) || !TryGetLmWorld(AnkleLm, AnkleWorld))
		{
			return;
		}
		if (bFootMeasured)
		{
			if (!TryGetLmWorld(ToeLm, ToeWorld))
			{
				return;
			}
		}
		else
		{
			ToeWorld = AnkleWorld;
		}
		bHeelMeasured = IsMeasured(HeelLm) && TryGetLmWorld(HeelLm, HeelWorld);
	}
	if (!bHeelMeasured)
	{
		HeelWorld = AnkleWorld;
	}

	FVector LegHipRightWorld = FVector::RightVector;
	FVector LegShoulderRightWorld = FVector::RightVector;
	FVector LegUpWorld = FVector::UpVector;
	FVector LegForwardWorld = FVector::ForwardVector;
	bool bHasLegTorsoBasis = false;
	if (bUsingBodyFusionLowerBody)
	{
		if (BodyFusionFrame.Pose.LeftHip.bValid && BodyFusionFrame.Pose.RightHip.bValid)
		{
			const FVector FusedHipRight = (BodyFusionFrame.Pose.RightHip.LocationWorld - BodyFusionFrame.Pose.LeftHip.LocationWorld).GetSafeNormal();
			if (!FusedHipRight.IsNearlyZero())
			{
				LegHipRightWorld = FusedHipRight;
				LegShoulderRightWorld = FusedHipRight;
			}
		}

		const FVector FusedUp = (BodyFusionFrame.Pose.Chest.LocationWorld - BodyFusionFrame.Pose.Pelvis.LocationWorld).GetSafeNormal();
		if (!FusedUp.IsNearlyZero())
		{
			LegUpWorld = FusedUp;
		}

		FMediaPipeAvatarEmbodimentProfile ForwardProfile = bHasTargetEmbodimentProfile
			? TargetEmbodimentProfile
			: FMediaPipeAvatarEmbodimentProfile();
		ForwardProfile.bUseTargetFaceForwardAxis = bUseTargetFaceForwardAxis;
		LegForwardWorld = FMediaPipeAvatarEmbodimentSolver::GetAvatarForwardWorld(TargetCompTransform, ForwardProfile).GetSafeNormal();
		bHasLegTorsoBasis = !LegForwardWorld.IsNearlyZero() && !LegUpWorld.IsNearlyZero();
	}
	else
	{
		bHasLegTorsoBasis = TryGetTorsoBasisWorld(LegHipRightWorld, LegShoulderRightWorld, LegUpWorld, LegForwardWorld);
	}
	const FVector OutwardWorldSeed = bHasLegTorsoBasis
		? (bIsLeft ? -LegHipRightWorld : LegHipRightWorld)
		: TargetCompTransform.TransformVectorNoScale(bIsLeft ? -FVector::RightVector : FVector::RightVector).GetSafeNormal();

	const bool bCanDriveFoot = bFootMeasured;
	bool& bHasStableFootForwardWorld = bIsLeft ? LeftLegState.bHasStableFootForwardWorld : RightLegState.bHasStableFootForwardWorld;
	FVector& StableFootForwardWorld = bIsLeft ? LeftLegState.StableFootForwardWorld : RightLegState.StableFootForwardWorld;
	bool& bHasStableFootRotationForwardWorld = bIsLeft ? LeftLegState.bHasStableFootRotationForwardWorld : RightLegState.bHasStableFootRotationForwardWorld;
	FVector& StableFootRotationForwardWorld = bIsLeft ? LeftLegState.StableFootRotationForwardWorld : RightLegState.StableFootRotationForwardWorld;

	auto SolveFootForwardWorld = [&](const FVector& RawFootForwardWorld) -> FVector
	{
		const FVector WorldUp = FVector::UpVector;
		const FVector ForwardHint = bHasLegTorsoBasis
			? (LegForwardWorld - FVector::DotProduct(LegForwardWorld, WorldUp) * WorldUp).GetSafeNormal()
			: TargetCompTransform.TransformVectorNoScale(FVector::ForwardVector).GetSafeNormal();

		FMediaPipeFootForwardSolveInput FootForwardInput;
		FootForwardInput.RawFootForwardWorld = RawFootForwardWorld;
		FootForwardInput.ForwardHintWorld = ForwardHint;
		FootForwardInput.WorldUp = WorldUp;
		FootForwardInput.bUseHysteresis = CVarMediaPipeFootForwardHysteresis.GetValueOnAnyThread() != 0;
		FootForwardInput.bHasStableFootForwardWorld = bHasStableFootForwardWorld;
		FootForwardInput.StableFootForwardWorld = StableFootForwardWorld;
		const FMediaPipeFootForwardSolveResult FootForwardResult = MediaPipeBodySolverMath::SolveFootForwardWorld(FootForwardInput);
		if (FootForwardInput.bUseHysteresis)
		{
			StableFootForwardWorld = FootForwardResult.StableFootForwardWorld;
			bHasStableFootForwardWorld = FootForwardResult.bHasStableFootForwardWorld;
		}

		return FootForwardResult.SolvedForwardWorld;
	};

	// Depth-cautious knee pole: front-facing monocular MediaPipe observes lateral/vertical knee
	// motion well but knee depth poorly. Rotate an implausible backward bend plane toward
	// forward/lateral without changing the bend magnitude, so knees stay bent but stop flipping
	// behind the hip-ankle line on depth noise.
	const float KneeBackwardSuppression =
		FMath::Clamp(CVarMediaPipeLegKneeBackwardPoleSuppression.GetValueOnAnyThread(), 0.0f, 1.0f);
	if (KneeBackwardSuppression > KINDA_SMALL_NUMBER && bHasLegTorsoBasis)
	{
		MediaPipeBodySolverMath::FMediaPipeKneePoleSuppressionInput KneePoleInput;
		KneePoleInput.HipWorld = HipWorld;
		KneePoleInput.KneeWorld = KneeWorld;
		KneePoleInput.AnkleWorld = AnkleWorld;
		KneePoleInput.ForwardHintWorld = LegForwardWorld;
		KneePoleInput.OutwardHintWorld = OutwardWorldSeed;
		KneePoleInput.BackwardSuppression01 = KneeBackwardSuppression;
		KneeWorld = MediaPipeBodySolverMath::SuppressBackwardKneePole(KneePoleInput);
	}

	const FVector DesiredThighWorld = (KneeWorld - HipWorld).GetSafeNormal();
	const FVector DesiredCalfWorld = (AnkleWorld - KneeWorld).GetSafeNormal();
	const FVector RawDesiredFootWorld = bCanDriveFoot
		? (bHeelMeasured && !(ToeWorld - HeelWorld).IsNearlyZero()
			? (ToeWorld - HeelWorld).GetSafeNormal()
			: (ToeWorld - AnkleWorld).GetSafeNormal())
		: FVector::ZeroVector;
	const FVector DesiredFootWorld = bCanDriveFoot ? SolveFootForwardWorld(RawDesiredFootWorld) : FVector::ZeroVector;

	const FVector DesiredThighComp = TargetCompTransform.InverseTransformVectorNoScale(DesiredThighWorld).GetSafeNormal();
	const FVector DesiredCalfComp = TargetCompTransform.InverseTransformVectorNoScale(DesiredCalfWorld).GetSafeNormal();
	const FVector DesiredFootComp = bCanDriveFoot ? TargetCompTransform.InverseTransformVectorNoScale(DesiredFootWorld).GetSafeNormal() : FVector::ZeroVector;
	if (DesiredThighComp.IsNearlyZero() || DesiredCalfComp.IsNearlyZero())
	{
		return;
	}

	const FBoneReference& ThighBone = bIsLeft ? ThighL : ThighR;
	const FBoneReference& CalfBone = bIsLeft ? CalfL : CalfR;
	const FBoneReference& FootBone = bIsLeft ? FootL : FootR;

	const FVector& RefThighDir = bIsLeft ? RefThighDirCompL : RefThighDirCompR;
	const FVector& RefCalfDir = bIsLeft ? RefCalfDirCompL : RefCalfDirCompR;
	const FVector& RefFootDir = bIsLeft ? RefFootDirCompL : RefFootDirCompR;
	const FQuat& RefThighComp = bIsLeft ? RefThighCompL : RefThighCompR;
	const FQuat& RefCalfComp = bIsLeft ? RefCalfCompL : RefCalfCompR;
	const FQuat& RefFootComp = bIsLeft ? RefFootCompL : RefFootCompR;
	const FQuat& RefThighBasisComp = bIsLeft ? RefThighBasisCompL : RefThighBasisCompR;
	const FQuat& RefCalfBasisComp = bIsLeft ? RefCalfBasisCompL : RefCalfBasisCompR;
	const bool bHasRefLegBasis = bIsLeft ? bHasRefLegBasisL : bHasRefLegBasisR;
	const FQuat& RefFootBasisComp = bIsLeft ? RefFootBasisCompL : RefFootBasisCompR;
	const bool bHasRefFootBasis = bIsLeft ? bHasRefFootBasisL : bHasRefFootBasisR;
	bool bApplyThighRotationCalled = false;
	bool bApplyCalfRotationCalled = false;
	bool bApplyFootRotationCalled = false;
	bool& bHasSmoothedThighRotCS = bIsLeft ? LeftLegState.bHasSmoothedThighRotCS : RightLegState.bHasSmoothedThighRotCS;
	FQuat& SmoothedThighRotCS = bIsLeft ? LeftLegState.SmoothedThighRotCS : RightLegState.SmoothedThighRotCS;
	bool& bHasSmoothedCalfRotCS = bIsLeft ? LeftLegState.bHasSmoothedCalfRotCS : RightLegState.bHasSmoothedCalfRotCS;
	FQuat& SmoothedCalfRotCS = bIsLeft ? LeftLegState.SmoothedCalfRotCS : RightLegState.SmoothedCalfRotCS;
	bool& bHasSmoothedFootRotCS = bIsLeft ? LeftLegState.bHasSmoothedFootRotCS : RightLegState.bHasSmoothedFootRotCS;
	FQuat& SmoothedFootRotCS = bIsLeft ? LeftLegState.SmoothedFootRotCS : RightLegState.SmoothedFootRotCS;
	bool& bHasSmoothedLegPlane = bIsLeft ? LeftLegState.bHasSmoothedLegPlane : RightLegState.bHasSmoothedLegPlane;
	FVector& SmoothedLegPlaneComp = bIsLeft ? LeftLegState.SmoothedLegPlaneComp : RightLegState.SmoothedLegPlaneComp;
	bool& bHasPrevFootSample = bIsLeft ? LeftLegState.bHasPrevFootSample : RightLegState.bHasPrevFootSample;
	FVector& PrevAnkleWorld = bIsLeft ? LeftLegState.PrevAnkleWorld : RightLegState.PrevAnkleWorld;
	FVector& PrevHeelWorld = bIsLeft ? LeftLegState.PrevHeelWorld : RightLegState.PrevHeelWorld;
	FVector& PrevToeWorld = bIsLeft ? LeftLegState.PrevToeWorld : RightLegState.PrevToeWorld;
	bool& bFootPlantLocked = bIsLeft ? LeftLegState.bFootPlantLocked : RightLegState.bFootPlantLocked;
	int32& FootPlantCandidateFrames = bIsLeft ? LeftLegState.FootPlantCandidateFrames : RightLegState.FootPlantCandidateFrames;
	FVector& LockedAnkleWorld = bIsLeft ? LeftLegState.LockedAnkleWorld : RightLegState.LockedAnkleWorld;
	bool& bHasObservedSourceFloor = bIsLeft ? LeftLegState.bHasObservedSourceFloor : RightLegState.bHasObservedSourceFloor;
	float& ObservedSourceFloorZ = bIsLeft ? LeftLegState.ObservedSourceFloorZ : RightLegState.ObservedSourceFloorZ;
	bool& bCurrentSourceFootGrounded = bIsLeft ? LeftLegState.bCurrentSourceFootGrounded : RightLegState.bCurrentSourceFootGrounded;
	bool& bCurrentSourceFootNearFloor = bIsLeft ? LeftLegState.bCurrentSourceFootNearFloor : RightLegState.bCurrentSourceFootNearFloor;
	const FVector& RefAnklePosComp = bIsLeft ? RefAnklePosCompL : RefAnklePosCompR;
	const FVector& RefBallPosComp = bIsLeft ? RefBallPosCompL : RefBallPosCompR;

	const bool bHadPrevFootSample = bHasPrevFootSample;
	const float SampleDt = FMath::Max(DeltaSeconds, 1.0f / 120.0f);
	const FVector AnkleVelWorld = bHadPrevFootSample ? ((AnkleWorld - PrevAnkleWorld) / SampleDt) : FVector::ZeroVector;
	const FVector HeelVelWorld = bHadPrevFootSample ? ((HeelWorld - PrevHeelWorld) / SampleDt) : FVector::ZeroVector;
	const FVector ToeVelWorld = bHadPrevFootSample ? ((ToeWorld - PrevToeWorld) / SampleDt) : FVector::ZeroVector;
	const float FootPlanarSpeedCmPerSecond = bHadPrevFootSample
		? FMath::Max(
			FMath::Max(
				FVector2D(AnkleVelWorld.X, AnkleVelWorld.Y).Size(),
				FVector2D(HeelVelWorld.X, HeelVelWorld.Y).Size()),
			FVector2D(ToeVelWorld.X, ToeVelWorld.Y).Size())
		: 0.0f;
	const float FootVerticalSpeedCmPerSecond = bHadPrevFootSample
		? FMath::Max(FMath::Max(FMath::Abs(AnkleVelWorld.Z), FMath::Abs(HeelVelWorld.Z)), FMath::Abs(ToeVelWorld.Z))
		: 0.0f;
	const float FootUpwardSpeedCmPerSecond = bHadPrevFootSample
		? FMath::Max(
			FMath::Max(FMath::Max(AnkleVelWorld.Z, 0.0f), FMath::Max(HeelVelWorld.Z, 0.0f)),
			FMath::Max(ToeVelWorld.Z, 0.0f))
		: 0.0f;

	PrevAnkleWorld = AnkleWorld;
	PrevHeelWorld = HeelWorld;
	PrevToeWorld = ToeWorld;
	bHasPrevFootSample = true;

	const float SampleFootFloorZ = bHeelMeasured
		? FMath::Min(FMath::Min(AnkleWorld.Z, HeelWorld.Z), ToeWorld.Z)
		: FMath::Min(AnkleWorld.Z, ToeWorld.Z);
	if (!bHasObservedSourceFloor)
	{
		ObservedSourceFloorZ = SampleFootFloorZ;
		bHasObservedSourceFloor = true;
	}
	else if (!bFootPlantLocked)
	{
		ObservedSourceFloorZ = FMath::Min(ObservedSourceFloorZ, SampleFootFloorZ);
	}

	const float HeightAboveObservedFloorCm = SampleFootFloorZ - ObservedSourceFloorZ;
	const float AcquireHeightCm = FMath::Min(FootPlantAcquireHeightCm, FootPlantReleaseHeightCm);
	const bool bNearObservedFloorForAcquire = bHasObservedSourceFloor && (HeightAboveObservedFloorCm <= AcquireHeightCm);
	const bool bNearObservedFloorForRelease = bHasObservedSourceFloor && (HeightAboveObservedFloorCm <= FootPlantReleaseHeightCm);
	const bool bFootMotionGroundLike =
		(FootPlanarSpeedCmPerSecond <= (FootPlantPlanarSpeedThresholdCmPerSecond * 1.5f)) &&
		(FootUpwardSpeedCmPerSecond <= FootPlantLiftSpeedThresholdCmPerSecond);
	const bool bFootShouldStayPlanted =
		bNearObservedFloorForRelease &&
		(FootUpwardSpeedCmPerSecond <= FootPlantLiftSpeedThresholdCmPerSecond);
	const bool bAcquireGrounded =
		bHadPrevFootSample &&
		bNearObservedFloorForAcquire &&
		bFootMotionGroundLike;
	bCurrentSourceFootGrounded = bAcquireGrounded || (bFootPlantLocked && bFootShouldStayPlanted);
	bCurrentSourceFootNearFloor = bNearObservedFloorForRelease;

	// Grounded feet should be referenced to the floor, not the (possibly squat-tilted) torso.
	const bool bFootUpFromWorld =
		CVarMediaPipeFootGroundedWorldUp.GetValueOnAnyThread() != 0 &&
		(bCurrentSourceFootGrounded || bNearObservedFloorForRelease);

	FVector FootForwardForRotationWorld = DesiredFootWorld;
	if (bCanDriveFoot && CVarMediaPipeFootPlanarWhenGrounded.GetValueOnAnyThread() != 0)
	{
		FVector FootPlaneUpWorld = (bHasLegTorsoBasis && !bFootUpFromWorld) ? LegUpWorld : FVector::UpVector;
		if (FootPlaneUpWorld.IsNearlyZero())
		{
			FootPlaneUpWorld = FVector::UpVector;
		}
		FootPlaneUpWorld = FootPlaneUpWorld.GetSafeNormal();

		const float VerticalDotThreshold = FMath::Clamp(CVarMediaPipeFootPlanarVerticalDotThreshold.GetValueOnAnyThread(), 0.0f, 1.0f);
		const float PlanarMinLength = FMath::Clamp(CVarMediaPipeFootPlanarMinLength.GetValueOnAnyThread(), 0.0f, 1.0f);
		const float MaxGroundTurnDeg = FMath::Clamp(CVarMediaPipeFootPlanarMaxGroundTurnDeg.GetValueOnAnyThread(), 0.0f, 180.0f);
		const FVector RawPlanarFootForward = DesiredFootWorld - FVector::DotProduct(DesiredFootWorld, FootPlaneUpWorld) * FootPlaneUpWorld;
		const float PlanarFootForwardLength = RawPlanarFootForward.Size();
		const bool bFootForwardTooVertical = FMath::Abs(FVector::DotProduct(DesiredFootWorld, FootPlaneUpWorld)) >= VerticalDotThreshold;
		const bool bFootPlanarTooShort = PlanarFootForwardLength <= PlanarMinLength;
		const bool bShouldPlanarizeFootForward =
			bCurrentSourceFootGrounded ||
			bNearObservedFloorForRelease ||
			bFootForwardTooVertical ||
			bFootPlanarTooShort;
		const FVector PlanarFootForward = RawPlanarFootForward.GetSafeNormal();
		if (bShouldPlanarizeFootForward && !PlanarFootForward.IsNearlyZero())
		{
			FVector CandidateFootForward = FVector::DotProduct(PlanarFootForward, DesiredFootWorld) < 0.0f
				? -PlanarFootForward
				: PlanarFootForward;
			const FVector StableFootForward = StableFootRotationForwardWorld.GetSafeNormal();
			const bool bHasStableFootForward = bHasStableFootRotationForwardWorld && !StableFootForward.IsNearlyZero();
			const float MinGroundHeadingDot = FMath::Cos(FMath::DegreesToRadians(MaxGroundTurnDeg));
			const bool bAbruptGroundHeadingChange =
				bHasStableFootForward &&
				bNearObservedFloorForRelease &&
				FVector::DotProduct(CandidateFootForward, StableFootForward) < MinGroundHeadingDot;
			const bool bUseStableFootForward =
				bHasStableFootForward &&
				(bFootForwardTooVertical || bFootPlanarTooShort || bAbruptGroundHeadingChange);

			FVector TorsoFootForward = bHasLegTorsoBasis
				? (LegForwardWorld - FVector::DotProduct(LegForwardWorld, FootPlaneUpWorld) * FootPlaneUpWorld).GetSafeNormal()
				: FVector::ZeroVector;
			if (!TorsoFootForward.IsNearlyZero() && FVector::DotProduct(TorsoFootForward, CandidateFootForward) < 0.0f)
			{
				TorsoFootForward *= -1.0f;
			}
			const bool bUseTorsoForwardFallback =
				CVarMediaPipeFootUnreliableUseTorsoForward.GetValueOnAnyThread() != 0 &&
				!TorsoFootForward.IsNearlyZero() &&
				(bFootForwardTooVertical || bFootPlanarTooShort || bAbruptGroundHeadingChange);

			FootForwardForRotationWorld = bUseTorsoForwardFallback
				? TorsoFootForward
				: (bUseStableFootForward ? StableFootForward : CandidateFootForward);
			if (!bUseStableFootForward || !bHasStableFootForward)
			{
				StableFootRotationForwardWorld = FootForwardForRotationWorld.GetSafeNormal();
				bHasStableFootRotationForwardWorld = !StableFootRotationForwardWorld.IsNearlyZero();
			}
		}
	}
	const FVector FootForwardForRotationComp = bCanDriveFoot
		? TargetCompTransform.InverseTransformVectorNoScale(FootForwardForRotationWorld).GetSafeNormal()
		: FVector::ZeroVector;

	auto MarkAppliedLegBone = [&](const FBoneReference& Bone)
	{
		if (Bone.BoneName == ThighBone.BoneName)
		{
			bApplyThighRotationCalled = true;
		}
		else if (Bone.BoneName == CalfBone.BoneName)
		{
			bApplyCalfRotationCalled = true;
		}
		else if (Bone.BoneName == FootBone.BoneName)
		{
			bApplyFootRotationCalled = true;
		}
	};

	auto ApplyDirOnly = [&](const FVector& RefDir, const FVector& TargetDir, const FQuat& RefRot, bool& bHasSmoothed, FQuat& SmoothedRot, const FBoneReference& Bone, float Alpha)
	{
		if (RefDir.IsNearlyZero() || TargetDir.IsNearlyZero() || !Bone.IsValidToEvaluate())
		{
			return;
		}
		const FQuat TargetRotCS = (FQuat::FindBetweenNormals(RefDir, TargetDir) * RefRot).GetNormalized();
		UpdateSmoothedRotation(bHasSmoothed, SmoothedRot, TargetRotCS, Alpha);
		ApplyRotationCS(CSPose, Bone, SmoothedRot);
		MarkAppliedLegBone(Bone);
	};

	const FVector LegOutwardComp = TargetCompTransform.InverseTransformVectorNoScale(OutwardWorldSeed).GetSafeNormal();
	auto BuildLegBasisRotation = [&](const FVector& RefDir, const FVector& TargetDir, const FQuat& RefRot, const FQuat& RefBasis, FQuat& OutTargetRotCS) -> bool
	{
		FMediaPipeLegBasisRotationInput BasisInput;
		BasisInput.RefDir = RefDir;
		BasisInput.TargetDir = TargetDir;
		BasisInput.RefRot = RefRot;
		BasisInput.RefBasis = RefBasis;
		BasisInput.LegOutwardComp = LegOutwardComp;
		BasisInput.bUseBasisRoll = CVarMediaPipeLegUseBasisRoll.GetValueOnAnyThread() != 0;
		BasisInput.bHasRefLegBasis = bHasRefLegBasis;
		return TryBuildLegBasisRotation(BasisInput, OutTargetRotCS);
	};

	auto ApplyLegRotation = [&](const FVector& RefDir, const FVector& TargetDir, const FQuat& RefRot, const FQuat& RefBasis, bool& bHasSmoothed, FQuat& SmoothedRot, const FBoneReference& Bone, const float Alpha)
	{
		if (!Bone.IsValidToEvaluate())
		{
			return;
		}

		FQuat TargetRotCS = FQuat::Identity;
		if (!BuildLegBasisRotation(RefDir, TargetDir, RefRot, RefBasis, TargetRotCS))
		{
			if (RefDir.IsNearlyZero() || TargetDir.IsNearlyZero())
			{
				return;
			}
			TargetRotCS = (FQuat::FindBetweenNormals(RefDir, TargetDir) * RefRot).GetNormalized();
		}

		UpdateSmoothedRotation(bHasSmoothed, SmoothedRot, TargetRotCS, Alpha);
		ApplyRotationCS(CSPose, Bone, SmoothedRot);
		MarkAppliedLegBone(Bone);
	};

	auto ApplyFootBasis = [&](const float Alpha)
	{
		if (!bCanDriveFoot || !FootBone.IsValidToEvaluate())
		{
			return;
		}

		// MediaPipe gives ankle/toe points but not a full foot quaternion. Use the tracked
		// ankle-to-toe pitch, then build the missing roll from the body/floor up hint.
		// Grounded feet take world up so torso lean during squats does not roll planted feet.
		const FVector FootUpSeedWorld = (bHasLegTorsoBasis && !bFootUpFromWorld) ? LegUpWorld : FVector::UpVector;
		FVector TargetFootUpSeedComp = TargetCompTransform.InverseTransformVectorNoScale(FootUpSeedWorld).GetSafeNormal();
		if (TargetFootUpSeedComp.IsNearlyZero())
		{
			TargetFootUpSeedComp = -DesiredCalfComp;
		}
		FVector TargetFootUpComp = (TargetFootUpSeedComp - FVector::DotProduct(TargetFootUpSeedComp, FootForwardForRotationComp) * FootForwardForRotationComp).GetSafeNormal();
		if (TargetFootUpComp.IsNearlyZero())
		{
			TargetFootUpComp = (-DesiredCalfComp - FVector::DotProduct(-DesiredCalfComp, FootForwardForRotationComp) * FootForwardForRotationComp).GetSafeNormal();
		}
		const bool bCanUseBasis =
			bHasRefFootBasis &&
			!FootForwardForRotationComp.IsNearlyZero() &&
			!TargetFootUpComp.IsNearlyZero() &&
			!FVector::CrossProduct(FootForwardForRotationComp, TargetFootUpComp).IsNearlyZero();
		if (bCanUseBasis)
		{
			const FQuat TargetFootBasisComp = MakeQuatFromForwardUp(FootForwardForRotationComp, TargetFootUpComp);
			const FQuat TargetFootRotCS = ((TargetFootBasisComp * RefFootBasisComp.Inverse()) * RefFootComp).GetNormalized();
			UpdateSmoothedRotation(bHasSmoothedFootRotCS, SmoothedFootRotCS, TargetFootRotCS, Alpha);
			ApplyRotationCS(CSPose, FootBone, SmoothedFootRotCS);
			MarkAppliedLegBone(FootBone);
			return;
		}

		ApplyDirOnly(RefFootDir, FootForwardForRotationComp, RefFootComp, bHasSmoothedFootRotCS, SmoothedFootRotCS, FootBone, Alpha);
	};

	const bool bDriveFootRotationForSide =
		CVarMediaPipeDriveFootRotation.GetValueOnAnyThread() != 0;
	const bool bDoLegIK = bDrivePelvisTranslation && bUseLegIK;
	const bool bAllowFootPlantLock = bDoLegIK && bUseLegIKFootPlant;
	auto EmitLegSolveDebugIfRequested = [&](const TCHAR* SolvePath)
	{
		static std::atomic<int32> LastRequestedLogCount{0};
		static std::atomic<int32> RemainingLogCount{0};

		const int32 RequestedLogCount = FMath::Max(0, CVarMediaPipeLegSolveDebugOnce.GetValueOnAnyThread());
		int32 LastObservedLogCount = LastRequestedLogCount.load(std::memory_order_relaxed);
		if (RequestedLogCount != LastObservedLogCount &&
			LastRequestedLogCount.compare_exchange_strong(LastObservedLogCount, RequestedLogCount, std::memory_order_relaxed))
		{
			RemainingLogCount.store(RequestedLogCount, std::memory_order_relaxed);
		}

		int32 RemainingLogs = RemainingLogCount.load(std::memory_order_relaxed);
		bool bShouldLog = false;
		while (RemainingLogs > 0)
		{
			if (RemainingLogCount.compare_exchange_weak(RemainingLogs, RemainingLogs - 1, std::memory_order_relaxed))
			{
				bShouldLog = true;
				break;
			}
		}
		if (!bShouldLog)
		{
			return;
		}

		UE_LOG(LogMediaPipePose, Warning,
			TEXT("mp.MediaPipeLegSolve.Debug actor=%s side=%s path=%s bDriveLegs=%d bHasRefLegL=%d bHasRefLegR=%d avatarLockedReplay=%d bodyFusionPoseUsable=%d bUseBodyFusionLowerBody=%d bUsingBodyFusionLowerBody=%d tryGetMediaPipeLowerBodySide=%d hip=%s knee=%s ankle=%s heel=%s toe=%s desiredThigh=%s desiredCalf=%s apply(thigh=%d calf=%d foot=%d) useLegIK=%d useFootPlant=%d driveFootRotation=%d"),
			*TargetActorName.ToString(),
			bIsLeft ? TEXT("L") : TEXT("R"),
			SolvePath,
			bDriveLegs ? 1 : 0,
			bHasRefLegL ? 1 : 0,
			bHasRefLegR ? 1 : 0,
			bAvatarLockedReplay ? 1 : 0,
			bBodyFusionPoseUsableForEvaluation ? 1 : 0,
			bUseBodyFusionLowerBody ? 1 : 0,
			bUsingBodyFusionLowerBody ? 1 : 0,
			bTryGetMediaPipeLowerBodySideSucceeded ? 1 : 0,
			*HipWorld.ToCompactString(),
			*KneeWorld.ToCompactString(),
			*AnkleWorld.ToCompactString(),
			*HeelWorld.ToCompactString(),
			*ToeWorld.ToCompactString(),
			*DesiredThighWorld.ToCompactString(),
			*DesiredCalfWorld.ToCompactString(),
			bApplyThighRotationCalled ? 1 : 0,
			bApplyCalfRotationCalled ? 1 : 0,
			bApplyFootRotationCalled ? 1 : 0,
			bDoLegIK ? 1 : 0,
			bAllowFootPlantLock ? 1 : 0,
			bDriveFootRotationForSide ? 1 : 0);
	};
	const float RefThighLen = bIsLeft ? RefThighLenCompL : RefThighLenCompR;
	const float RefCalfLen = bIsLeft ? RefCalfLenCompL : RefCalfLenCompR;
	if (bDoLegIK && RefThighLen > KINDA_SMALL_NUMBER && RefCalfLen > KINDA_SMALL_NUMBER)
	{
		if (!ThighBone.IsValidToEvaluate() || !CalfBone.IsValidToEvaluate())
		{
			return;
		}

		const FVector HipPosComp = CSPose.GetComponentSpaceTransform(ThighBone.CachedCompactPoseIndex).GetTranslation();
		FVector AnkleTargetComp = HipPosComp + TargetCompTransform.InverseTransformVectorNoScale(AnkleWorld - HipWorld);
		if (bAllowFootPlantLock && bHasRefFootFloorZ)
		{
			const float RefAnkleHeightAboveFloorComp = RefAnklePosComp.Z - RefBallPosComp.Z;
			const float PlantedAnkleZComp = RefFootFloorZComp + RefAnkleHeightAboveFloorComp;
			if (bFootShouldStayPlanted)
			{
				AnkleTargetComp.Z = PlantedAnkleZComp;
			}

			const bool bAcquireCandidate =
				bAcquireGrounded &&
				(FootPlanarSpeedCmPerSecond <= FootPlantPlanarSpeedThresholdCmPerSecond) &&
				(FootVerticalSpeedCmPerSecond <= FootPlantVerticalSpeedThresholdCmPerSecond);
			const bool bReleaseLock = !bFootShouldStayPlanted;

			if (bFootPlantLocked)
			{
				if (bReleaseLock)
				{
					bFootPlantLocked = false;
					FootPlantCandidateFrames = 0;
				}
			}
			else if (bAcquireCandidate && DeltaSeconds > 0.0f)
			{
				FootPlantCandidateFrames++;
				if (FootPlantCandidateFrames >= FMath::Max(1, FootPlantHysteresisFrames))
				{
					FVector LockedAnkleComp = AnkleTargetComp;
					LockedAnkleComp.Z = PlantedAnkleZComp;
					LockedAnkleWorld = TargetCompTransform.TransformPosition(LockedAnkleComp);
					bFootPlantLocked = true;
					FootPlantCandidateFrames = 0;
				}
			}
			else
			{
				FootPlantCandidateFrames = 0;
			}

			if (bFootPlantLocked)
			{
				AnkleTargetComp = TargetCompTransform.InverseTransformPosition(LockedAnkleWorld);
			}
		}
		else
		{
			bFootPlantLocked = false;
			FootPlantCandidateFrames = 0;
		}

		FVector ToTarget = (AnkleTargetComp - HipPosComp);
		float Dist = ToTarget.Size();
		if (Dist <= KINDA_SMALL_NUMBER)
		{
			return;
		}

		const FVector DirToTarget = ToTarget.GetSafeNormal();
		const float ReachRatio = bFootPlantLocked ? 1.0f : FMath::Clamp(LegIKMaxExtensionRatio, 0.8f, 1.0f);
		const float MaxReach = FMath::Max((RefThighLen + RefCalfLen) * ReachRatio, KINDA_SMALL_NUMBER);
		const float MinReach = FMath::Max(FMath::Abs(RefThighLen - RefCalfLen) + 1.0f, 1.0f);
		Dist = FMath::Clamp(Dist, MinReach, MaxReach);
		const FVector AnklePosComp = HipPosComp + DirToTarget * Dist;

		const float L1 = RefThighLen;
		const float L2 = RefCalfLen;
		const float CosHip = FMath::Clamp(((L1 * L1) + (Dist * Dist) - (L2 * L2)) / (2.0f * L1 * Dist), -1.0f, 1.0f);
		const float SinHip = FMath::Sqrt(FMath::Max(0.0f, 1.0f - (CosHip * CosHip)));

		const float RotAlpha = HalfLifeToAlpha(LegIKRotationHalfLifeSeconds, DeltaSeconds);

		const FVector DesiredKneeOffset = (DesiredThighComp - FVector::DotProduct(DesiredThighComp, DirToTarget) * DirToTarget).GetSafeNormal();
		const FVector DesiredLegPlaneNormal = FVector::CrossProduct(DesiredThighComp, DesiredCalfComp).GetSafeNormal();
		const FVector OutwardComp = TargetCompTransform.InverseTransformVectorNoScale(OutwardWorldSeed).GetSafeNormal();

		FVector TargetPole = FVector::CrossProduct(DesiredLegPlaneNormal, DirToTarget).GetSafeNormal();
		if (TargetPole.IsNearlyZero())
		{
			TargetPole = DesiredKneeOffset;
		}
		if (!DesiredKneeOffset.IsNearlyZero() && FVector::DotProduct(TargetPole, DesiredKneeOffset) < 0.0f)
		{
			TargetPole *= -1.0f;
		}
		if (TargetPole.IsNearlyZero())
		{
			TargetPole = (OutwardComp - FVector::DotProduct(OutwardComp, DirToTarget) * DirToTarget).GetSafeNormal();
		}
		if (TargetPole.IsNearlyZero())
		{
			FVector CompUp = TargetCompTransform.InverseTransformVectorNoScale(FVector::UpVector).GetSafeNormal();
			if (CompUp.IsNearlyZero())
			{
				CompUp = FVector::UpVector;
			}
			TargetPole = (CompUp - FVector::DotProduct(CompUp, DirToTarget) * DirToTarget).GetSafeNormal();
		}
		if (TargetPole.IsNearlyZero())
		{
			TargetPole = bHasSmoothedLegPlane ? SmoothedLegPlaneComp.GetSafeNormal() : FVector::ZeroVector;
			TargetPole = (TargetPole - FVector::DotProduct(TargetPole, DirToTarget) * DirToTarget).GetSafeNormal();
		}
		TargetPole = (TargetPole - FVector::DotProduct(TargetPole, DirToTarget) * DirToTarget).GetSafeNormal();
		if (TargetPole.IsNearlyZero())
		{
			TargetPole = DesiredKneeOffset;
		}
		if (!DesiredKneeOffset.IsNearlyZero())
		{
			TargetPole = LockVectorToHemisphere(TargetPole, DesiredKneeOffset);
		}
		if (TargetPole.IsNearlyZero())
		{
			return;
		}

		FVector TargetPlaneNormal = FVector::CrossProduct(DirToTarget, TargetPole).GetSafeNormal();
		if (TargetPlaneNormal.IsNearlyZero())
		{
			TargetPlaneNormal = FVector::CrossProduct(DirToTarget, FVector::UpVector).GetSafeNormal();
		}
		if (TargetPlaneNormal.IsNearlyZero())
		{
			return;
		}

		if (!bHasSmoothedLegPlane)
		{
			SmoothedLegPlaneComp = TargetPole;
			bHasSmoothedLegPlane = true;
		}
		else
		{
			if (!DesiredKneeOffset.IsNearlyZero())
			{
				SmoothedLegPlaneComp = LockVectorToHemisphere(SmoothedLegPlaneComp, DesiredKneeOffset);
			}
			SmoothedLegPlaneComp = LerpNormalized(SmoothedLegPlaneComp, TargetPole, RotAlpha);
			if (!DesiredKneeOffset.IsNearlyZero())
			{
				SmoothedLegPlaneComp = LockVectorToHemisphere(SmoothedLegPlaneComp, DesiredKneeOffset);
			}
		}

		FVector Pole = SmoothedLegPlaneComp.IsNearlyZero() ? TargetPole : SmoothedLegPlaneComp.GetSafeNormal();
		Pole = (Pole - FVector::DotProduct(Pole, DirToTarget) * DirToTarget).GetSafeNormal();
		if (!DesiredKneeOffset.IsNearlyZero())
		{
			Pole = LockVectorToHemisphere(Pole, DesiredKneeOffset);
		}
		if (Pole.IsNearlyZero())
		{
			Pole = TargetPole;
		}
		FVector PlaneNormal = FVector::CrossProduct(DirToTarget, Pole).GetSafeNormal();
		FVector DirPerp = FVector::CrossProduct(PlaneNormal, DirToTarget).GetSafeNormal();
		if (DirPerp.IsNearlyZero())
		{
			return;
		}
		if (FVector::DotProduct(DirPerp, Pole) < 0.0f)
		{
			DirPerp *= -1.0f;
		}

		const FVector KneeAxisBase = HipPosComp + DirToTarget * (L1 * CosHip);
		const FVector KneePosA = KneeAxisBase + DirPerp * (L1 * SinHip);
		const FVector KneePosB = KneeAxisBase - DirPerp * (L1 * SinHip);

		const FVector ThighDirA = (KneePosA - HipPosComp).GetSafeNormal();
		const FVector CalfDirA = (AnklePosComp - KneePosA).GetSafeNormal();
		const float ScoreA = FVector::DotProduct(ThighDirA, DesiredThighComp) + FVector::DotProduct(CalfDirA, DesiredCalfComp);
		const FVector KneePoleA = (KneePosA - KneeAxisBase).GetSafeNormal();
		const float PoleScoreA = KneePoleA.IsNearlyZero() ? -1.0f : FVector::DotProduct(KneePoleA, Pole);

		const FVector ThighDirB = (KneePosB - HipPosComp).GetSafeNormal();
		const FVector CalfDirB = (AnklePosComp - KneePosB).GetSafeNormal();
		const float ScoreB = FVector::DotProduct(ThighDirB, DesiredThighComp) + FVector::DotProduct(CalfDirB, DesiredCalfComp);
		const FVector KneePoleB = (KneePosB - KneeAxisBase).GetSafeNormal();
		const float PoleScoreB = KneePoleB.IsNearlyZero() ? -1.0f : FVector::DotProduct(KneePoleB, Pole);

		bool bUseA = PoleScoreA > PoleScoreB;
		if (FMath::IsNearlyEqual(PoleScoreA, PoleScoreB, 0.001f))
		{
			bUseA = (ScoreA >= ScoreB);
		}
		const FVector SolvedThighDir = bUseA ? ThighDirA : ThighDirB;
		const FVector SolvedCalfDir = bUseA ? CalfDirA : CalfDirB;

		ApplyLegRotation(RefThighDir, SolvedThighDir, RefThighComp, RefThighBasisComp, bHasSmoothedThighRotCS, SmoothedThighRotCS, ThighBone, RotAlpha);
		ApplyLegRotation(RefCalfDir, SolvedCalfDir, RefCalfComp, RefCalfBasisComp, bHasSmoothedCalfRotCS, SmoothedCalfRotCS, CalfBone, RotAlpha);
		if (bCanDriveFoot && bDriveFootRotationForSide)
		{
			ApplyFootBasis(RotAlpha);
		}

		EmitLegSolveDebugIfRequested(TEXT("IK"));
		return;
	}

	const float RotAlpha = HalfLifeToAlpha(LegIKRotationHalfLifeSeconds, DeltaSeconds);
	ApplyLegRotation(RefThighDir, DesiredThighComp, RefThighComp, RefThighBasisComp, bHasSmoothedThighRotCS, SmoothedThighRotCS, ThighBone, RotAlpha);
	ApplyLegRotation(RefCalfDir, DesiredCalfComp, RefCalfComp, RefCalfBasisComp, bHasSmoothedCalfRotCS, SmoothedCalfRotCS, CalfBone, RotAlpha);
	if (bCanDriveFoot && bDriveFootRotationForSide)
	{
		ApplyFootBasis(RotAlpha);
	}
	EmitLegSolveDebugIfRequested(TEXT("DirectSegment"));
}
