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

	// Monocular MediaPipe leg intent: these segment directions own knee bend timing, foot lift
	// timing, left/right phase, lateral swing, and relative amplitude. Their flexion magnitude is
	// corrected against the Quest/HMD metric scaffold below (grounded legs only) before they are
	// converted to component space and applied on the avatar's own proportions.
	FVector DesiredThighWorld = (KneeWorld - HipWorld).GetSafeNormal();
	FVector DesiredCalfWorld = (AnkleWorld - KneeWorld).GetSafeNormal();

	// Live stabilization: when the camera's leg-landmark reliability degrades (subject near the
	// frame edge, occlusion, phone movement), ease the segment directions toward the avatar's
	// reference stance instead of following held or drifting landmarks (which pull the legs
	// together and skew the stance).
	if (!bUsingBodyFusionLowerBody &&
		CVarMediaPipeLegReliabilityStabilize.GetValueOnAnyThread() != 0)
	{
		const int32 ReliabilityHipLm = bIsLeft ? (int32)EMediaPipePoseLandmark::LeftHip : (int32)EMediaPipePoseLandmark::RightHip;
		const int32 ReliabilityKneeLm = bIsLeft ? (int32)EMediaPipePoseLandmark::LeftKnee : (int32)EMediaPipePoseLandmark::RightKnee;
		const int32 ReliabilityAnkleLm = bIsLeft ? (int32)EMediaPipePoseLandmark::LeftAnkle : (int32)EMediaPipePoseLandmark::RightAnkle;
		const float ChainReliability = FMath::Min3(
			GetLandmarkReliability(ReliabilityHipLm),
			GetLandmarkReliability(ReliabilityKneeLm),
			GetLandmarkReliability(ReliabilityAnkleLm));
		constexpr float FullTrustReliability = 0.5f;
		const float Trust = FMath::Clamp(ChainReliability / FullTrustReliability, 0.0f, 1.0f);
		if (Trust < 1.0f)
		{
			const FVector RefThighWorldDir = TargetCompTransform.TransformVectorNoScale(
				bIsLeft ? RefThighDirCompL : RefThighDirCompR).GetSafeNormal();
			const FVector RefCalfWorldDir = TargetCompTransform.TransformVectorNoScale(
				bIsLeft ? RefCalfDirCompL : RefCalfDirCompR).GetSafeNormal();
			if (!RefThighWorldDir.IsNearlyZero() && !RefCalfWorldDir.IsNearlyZero())
			{
				DesiredThighWorld = LerpNormalized(RefThighWorldDir, DesiredThighWorld, Trust);
				DesiredCalfWorld = LerpNormalized(RefCalfWorldDir, DesiredCalfWorld, Trust);
			}
		}
	}

	// Sagittal re-pitch: monocular depth noise INFLATES the apparent femur/shin length when a
	// segment points toward the camera, shrinking its normalized elevation - a raised knee
	// reads low (observed live 2026-07-02). The landmarks' vertical deltas are image-plane
	// reliable, so re-pitch each segment to match its measured vertical drop over a
	// depth-robust decaying-minimum length, preserving the planar heading. Live-trial gated.
	if (CVarMediaPipeLegSagittalRepitch.GetValueOnAnyThread() != 0)
	{
		FMediaPipeLegSolverState& RepitchLegState = bIsLeft ? LeftLegState : RightLegState;
		const float ObservedThighLenCm = (KneeWorld - HipWorld).Size();
		const float ObservedCalfLenCm = (AnkleWorld - KneeWorld).Size();
		const float LengthDecayPerSec = FMath::Max(
			CVarMediaPipeLegSagittalLengthDecayPerSec.GetValueOnAnyThread(), 0.0f);
		if (ObservedThighLenCm > 5.0f && ObservedCalfLenCm > 5.0f)
		{
			const float StableThighLenCm = MediaPipeBodySolverMath::UpdateDecayingMinLengthCm(
				RepitchLegState.bHasThighLenEstimate, RepitchLegState.ThighLenEstimateCm,
				ObservedThighLenCm, DeltaSeconds, LengthDecayPerSec);
			const float StableCalfLenCm = MediaPipeBodySolverMath::UpdateDecayingMinLengthCm(
				RepitchLegState.bHasCalfLenEstimate, RepitchLegState.CalfLenEstimateCm,
				ObservedCalfLenCm, DeltaSeconds, LengthDecayPerSec);
			DesiredThighWorld = MediaPipeBodySolverMath::RepitchDirectionFromVerticalRatio(
				DesiredThighWorld, KneeWorld.Z - HipWorld.Z, StableThighLenCm);
			DesiredCalfWorld = MediaPipeBodySolverMath::RepitchDirectionFromVerticalRatio(
				DesiredCalfWorld, AnkleWorld.Z - KneeWorld.Z, StableCalfLenCm);
		}
	}

	// Anatomical adduction bound: with the reliability stabilizer off (full-extent legs),
	// monocular drift walks the knees into each other (observed live 2026-07-02). Bounding the
	// thigh's travel past vertical toward the midline stops that without damping any other
	// motion; abduction stays unlimited. Live-trial gated.
	if (CVarMediaPipeLegAdductionClamp.GetValueOnAnyThread() != 0 &&
		!OutwardWorldSeed.IsNearlyZero())
	{
		DesiredThighWorld = MediaPipeBodySolverMath::ClampDirectionAdduction(
			DesiredThighWorld,
			OutwardWorldSeed,
			CVarMediaPipeLegAdductionMaxDeg.GetValueOnAnyThread());
	}

	const FVector RawDesiredFootWorld = bCanDriveFoot
		? (bHeelMeasured && !(ToeWorld - HeelWorld).IsNearlyZero()
			? (ToeWorld - HeelWorld).GetSafeNormal()
			: (ToeWorld - AnkleWorld).GetSafeNormal())
		: FVector::ZeroVector;
	const FVector DesiredFootWorld = bCanDriveFoot ? SolveFootForwardWorld(RawDesiredFootWorld) : FVector::ZeroVector;

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

	// --- Grounded-leg metric flexion correction (MediaPipe intent + Quest/HMD scaffold) ---
	// When this foot is at or near its observed source floor, correct the monocular flexion
	// magnitude toward the knee bend that realizes the fused scaffold pelvis drop on this
	// avatar's own thigh/calf lengths (law of cosines with reachable limits). The bend plane,
	// timing, phase, and lateral swing stay owned by the MediaPipe intent; lifted feet are
	// never touched so steps and kicks keep their recorded motion.
	const float RefThighLen = bIsLeft ? RefThighLenCompL : RefThighLenCompR;
	const float RefCalfLen = bIsLeft ? RefCalfLenCompL : RefCalfLenCompR;
	const float ScaffoldFlexionWeight =
		FMath::Clamp(CVarMediaPipeLegScaffoldFlexionWeight.GetValueOnAnyThread(), 0.0f, 1.0f);
	FMediaPipeLegSolverState& SideLegState = bIsLeft ? LeftLegState : RightLegState;
	SideLegState.LastFootLiftCm = HeightAboveObservedFloorCm;
	SideLegState.LastFlexionMeasuredDeg = FMath::RadiansToDegrees(FMath::Acos(
		FMath::Clamp(FVector::DotProduct(DesiredThighWorld, DesiredCalfWorld), -1.0f, 1.0f)));
	SideLegState.LastFlexionTargetDeg = SideLegState.LastFlexionMeasuredDeg;
	SideLegState.LastFlexionAppliedDeltaDeg = 0.0f;
	SideLegState.bLastFlexionAdjustApplied = false;
	if (ScaffoldFlexionWeight > KINDA_SMALL_NUMBER &&
		BodyState.bHasLowerBodyScaffoldSample &&
		BodyState.ScaffoldHmdShare01 > KINDA_SMALL_NUMBER &&
		(bCurrentSourceFootGrounded || bCurrentSourceFootNearFloor))
	{
		// Asymmetric distribution: the HMD drop says HOW FAR the body lowered; the camera's
		// per-leg measured bend says WHICH leg is doing the bending. Equal distribution bends
		// the straight back leg of a lunge into a squat (observed live 2026-06-13); weighting
		// by the measured bend share keeps lunges asymmetric while squats stay symmetric.
		float FlexionShareWeight = 1.0f;
		if (CVarMediaPipeLegScaffoldAsymmetricFlexion.GetValueOnAnyThread() != 0)
		{
			const FMediaPipeLegSolverState& OtherLegState = bIsLeft ? RightLegState : LeftLegState;
			float WeightL = 1.0f;
			float WeightR = 1.0f;
			MediaPipeBodySolverMath::ComputeLegFlexionShareWeights(
				bIsLeft ? SideLegState.LastFlexionMeasuredDeg : OtherLegState.LastFlexionMeasuredDeg,
				bIsLeft ? OtherLegState.LastFlexionMeasuredDeg : SideLegState.LastFlexionMeasuredDeg,
				WeightL,
				WeightR);
			FlexionShareWeight = bIsLeft ? WeightL : WeightR;
		}

		MediaPipeBodySolverMath::FMediaPipeGroundedLegFlexionInput FlexionInput;
		FlexionInput.ThighDirWorld = DesiredThighWorld;
		FlexionInput.CalfDirWorld = DesiredCalfWorld;
		FlexionInput.ThighLenCm = RefThighLen;
		FlexionInput.CalfLenCm = RefCalfLen;
		FlexionInput.ReferenceFlexionDeg = FMath::RadiansToDegrees(FMath::Acos(
			FMath::Clamp(FVector::DotProduct(RefThighDir, RefCalfDir), -1.0f, 1.0f)));
		FlexionInput.TargetPelvisDropCm = BodyState.ScaffoldPelvisDropCm;
		FlexionInput.MaxAdjustDeg =
			FMath::Max(CVarMediaPipeLegScaffoldFlexionMaxAdjustDeg.GetValueOnAnyThread(), 0.0f);
		FlexionInput.AdjustWeight01 = FMath::Clamp(
			ScaffoldFlexionWeight * BodyState.ScaffoldHmdShare01 * FlexionShareWeight, 0.0f, 1.0f);
		FlexionInput.BendFallbackNormalWorld =
			FVector::CrossProduct(LegForwardWorld, DesiredThighWorld).GetSafeNormal();
		const MediaPipeBodySolverMath::FMediaPipeGroundedLegFlexionResult FlexionResult =
			MediaPipeBodySolverMath::AdjustGroundedLegFlexion(FlexionInput);
		SideLegState.LastFlexionMeasuredDeg = FlexionResult.MeasuredFlexionDeg;
		SideLegState.LastFlexionTargetDeg = FlexionResult.TargetFlexionDeg;
		if (FlexionResult.bApplied)
		{
			DesiredThighWorld = FlexionResult.ThighDirWorld;
			DesiredCalfWorld = FlexionResult.CalfDirWorld;
			SideLegState.LastFlexionAppliedDeltaDeg = FlexionResult.AppliedDeltaDeg;
			SideLegState.bLastFlexionAdjustApplied = true;
		}
	}

	// --- Grounded-leg bend redistribution (femur/shin split) ---
	// Monocular front-facing capture cannot see the femur's forward (depth) rotation, so the
	// recorded bend lands mostly in the shin and the knee sinks. Rigidly rotate the corrected
	// thigh+calf pair inside their own bend plane so the shin keeps at most its natural share of
	// the total flexion. Flexion magnitude, bend plane, timing, and lifted feet stay untouched.
	const float BendRedistributionWeight =
		FMath::Clamp(CVarMediaPipeLegScaffoldBendRedistributionWeight.GetValueOnAnyThread(), 0.0f, 1.0f);
	SideLegState.LastBendRedistributionDeg = 0.0f;
	SideLegState.LastShinTiltDeg = 0.0f;
	if (BendRedistributionWeight > KINDA_SMALL_NUMBER &&
		(bCurrentSourceFootGrounded || bCurrentSourceFootNearFloor))
	{
		MediaPipeBodySolverMath::FMediaPipeGroundedLegBendRedistributionInput RedistributionInput;
		RedistributionInput.ThighDirWorld = DesiredThighWorld;
		RedistributionInput.CalfDirWorld = DesiredCalfWorld;
		RedistributionInput.ShinTiltShare01 =
			FMath::Clamp(CVarMediaPipeLegScaffoldShinTiltShare.GetValueOnAnyThread(), 0.0f, 1.0f);
		RedistributionInput.Weight01 = BendRedistributionWeight;
		RedistributionInput.MaxRotateDeg =
			FMath::Max(CVarMediaPipeLegScaffoldBendRedistributionMaxDeg.GetValueOnAnyThread(), 0.0f);
		const MediaPipeBodySolverMath::FMediaPipeGroundedLegBendRedistributionResult RedistributionResult =
			MediaPipeBodySolverMath::RedistributeGroundedLegBend(RedistributionInput);
		SideLegState.LastShinTiltDeg = RedistributionResult.ShinTiltDeg;
		if (RedistributionResult.bApplied)
		{
			DesiredThighWorld = RedistributionResult.ThighDirWorld;
			DesiredCalfWorld = RedistributionResult.CalfDirWorld;
			SideLegState.LastBendRedistributionDeg = RedistributionResult.AppliedRotateDeg;
		}
	}

	const FVector DesiredThighComp = TargetCompTransform.InverseTransformVectorNoScale(DesiredThighWorld).GetSafeNormal();
	const FVector DesiredCalfComp = TargetCompTransform.InverseTransformVectorNoScale(DesiredCalfWorld).GetSafeNormal();
	if (DesiredThighComp.IsNearlyZero() || DesiredCalfComp.IsNearlyZero())
	{
		return;
	}

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
		// A LIFTED foot may genuinely point toes-down: planarizing it merely for being vertical
		// renders a horizontal foot during knee raises (observed live 2026-06-13). With the
		// ground-blend feature on, vertical/short directions force planarization only while the
		// foot is near its observed floor; with it off the legacy behavior is unchanged.
		const bool bLiftedVerticalAllowed =
			CVarMediaPipeFootGroundedBlend.GetValueOnAnyThread() != 0 &&
			!bCurrentSourceFootGrounded &&
			!bNearObservedFloorForRelease;
		const bool bShouldPlanarizeFootForward =
			bCurrentSourceFootGrounded ||
			bNearObservedFloorForRelease ||
			((bFootForwardTooVertical || bFootPlanarTooShort) && !bLiftedVerticalAllowed);
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

	// --- Grounded foot pitch (flat soles) ---
	// The reference foot basis is built from the naturally down-sloped ankle->ball axis, so a
	// planarized (horizontal) forward pitches the planted foot toe-up and the ankle sinks to ball
	// height. Rebuild the grounded foot's pitch as the avatar's reference flat-contact slope plus
	// a heel-lift-driven extra downslope: heel-down feet sit exactly flat, heel raises and toe
	// stands keep their plantar flexion, and lifted feet keep the raw monocular pitch. The heel
	// lift is measured against the heel landmark's own observed floor because that landmark sits
	// high on the ankle and its raw axis pitch is useless for ground contact.
	SideLegState.LastFootPitchAppliedDeg = 0.0f;
	SideLegState.LastFootExtraDownPitchDeg = 0.0f;
	// The legacy gate is BINARY on the near-floor flag: a lunge's foreshortened back foot
	// jitters its measured height around the release threshold, snapping the foot between the
	// solved and raw pitch (measured live 2026-06-13). With mp.MediaPipeFootGroundedBlend the
	// solve fades continuously by height above the observed floor instead (time-smoothed);
	// with it off this block is byte-identical to the legacy behavior for replay stability.
	const bool bUseFootGroundedBlend = CVarMediaPipeFootGroundedBlend.GetValueOnAnyThread() != 0;
	float FootGroundBlend01 = (bCurrentSourceFootGrounded || bCurrentSourceFootNearFloor) ? 1.0f : 0.0f;
	if (bUseFootGroundedBlend)
	{
		const float TargetBlend01 = MediaPipeBodySolverMath::ComputeFootGroundBlend01(
			HeightAboveObservedFloorCm, AcquireHeightCm, FootPlantReleaseHeightCm);
		if (!SideLegState.bHasSmoothedFootGroundBlend)
		{
			SideLegState.SmoothedFootGroundBlend01 = TargetBlend01;
			SideLegState.bHasSmoothedFootGroundBlend = true;
		}
		else
		{
			// Asymmetric: ground fast (planting must feel instant), release slow (upward
			// landmark noise spikes on the camera-far foot must not snap the plant away;
			// a genuine lift keeps rising and takes the handover after the release delay).
			const bool bReleasing = TargetBlend01 < SideLegState.SmoothedFootGroundBlend01;
			const float BlendAlpha = HalfLifeToAlpha(
				FMath::Max(bReleasing
					? CVarMediaPipeFootGroundedBlendReleaseHalfLife.GetValueOnAnyThread()
					: CVarMediaPipeFootGroundedBlendHalfLife.GetValueOnAnyThread(), 0.0f),
				DeltaSeconds);
			SideLegState.SmoothedFootGroundBlend01 = FMath::Lerp(
				SideLegState.SmoothedFootGroundBlend01, TargetBlend01, BlendAlpha);
		}
		FootGroundBlend01 = SideLegState.SmoothedFootGroundBlend01;
	}
	if (bCanDriveFoot &&
		CVarMediaPipeFootGroundedPitchClamp.GetValueOnAnyThread() != 0 &&
		FootGroundBlend01 > KINDA_SMALL_NUMBER)
	{
		if (bHeelMeasured)
		{
			if (!SideLegState.bHasObservedHeelFloor)
			{
				SideLegState.ObservedHeelFloorZ = HeelWorld.Z;
				SideLegState.bHasObservedHeelFloor = true;
			}
			else if (!bFootPlantLocked)
			{
				SideLegState.ObservedHeelFloorZ = FMath::Min(SideLegState.ObservedHeelFloorZ, HeelWorld.Z);
			}
		}
		const float HeelLiftCm = (bHeelMeasured && SideLegState.bHasObservedHeelFloor)
			? FMath::Max(HeelWorld.Z - SideLegState.ObservedHeelFloorZ, 0.0f)
			: 0.0f;

		MediaPipeBodySolverMath::FMediaPipeGroundedFootPitchInput FootPitchInput;
		FootPitchInput.FootForwardWorld = FootForwardForRotationWorld;
		FootPitchInput.ReferencePitchDeg = FMath::RadiansToDegrees(FMath::Asin(
			FMath::Clamp(RefFootDir.Z, -1.0f, 1.0f)));
		FootPitchInput.HeelLiftCm = HeelLiftCm;
		FootPitchInput.RefFootPlanarLengthCm = FMath::Max(
			FVector2D(RefBallPosComp.X - RefAnklePosComp.X, RefBallPosComp.Y - RefAnklePosComp.Y).Size(),
			1.0f);
		FootPitchInput.MaxExtraDownPitchDeg =
			FMath::Max(CVarMediaPipeFootGroundedMaxExtraDownPitchDeg.GetValueOnAnyThread(), 0.0f);
		const MediaPipeBodySolverMath::FMediaPipeGroundedFootPitchResult FootPitchResult =
			MediaPipeBodySolverMath::SolveGroundedFootPitch(FootPitchInput);
		FootForwardForRotationWorld = bUseFootGroundedBlend
			? FMath::Lerp(FootForwardForRotationWorld, FootPitchResult.FootForwardWorld, FootGroundBlend01).GetSafeNormal()
			: FootPitchResult.FootForwardWorld;
		SideLegState.LastFootPitchAppliedDeg = FootPitchResult.AppliedPitchDeg;
		SideLegState.LastFootExtraDownPitchDeg = FootPitchResult.ExtraDownPitchDeg;
	}

	// Anatomical heading bound FIRST: a foot attached to a body cannot yaw freely, so the
	// applied heading is clamped into a band around the torso heading. This is what actually
	// kills propeller spins - the rate limiter below would otherwise CHASE a sign-flipping
	// direction estimate into continuous rotation (observed live 2026-06-13).
	if (bCanDriveFoot &&
		bHasLegTorsoBasis &&
		CVarMediaPipeFootHeadingClamp.GetValueOnAnyThread() != 0 &&
		!FootForwardForRotationWorld.IsNearlyZero())
	{
		FootForwardForRotationWorld = MediaPipeBodySolverMath::ClampPlanarHeadingToReference(
			FootForwardForRotationWorld,
			LegForwardWorld,
			CVarMediaPipeFootHeadingClampMaxDeg.GetValueOnAnyThread());
	}

	// The foot-forward selection above switches INSTANTLY between distinct fallback sources
	// (raw ankle->toe, last stable heading, torso forward) when the foreshortened back foot's
	// landmarks jitter across their validity thresholds - the residual lunge snap (2026-06-13).
	// Route the chosen direction through a rate-limited ease so every switch becomes a
	// physically plausible turn. Live-trial gated; replay stays byte-stable.
	if (bCanDriveFoot &&
		CVarMediaPipeFootForwardSmoothing.GetValueOnAnyThread() != 0 &&
		!FootForwardForRotationWorld.IsNearlyZero())
	{
		if (!SideLegState.bHasSmoothedAppliedFootForward ||
			SideLegState.SmoothedAppliedFootForwardWorld.IsNearlyZero())
		{
			SideLegState.SmoothedAppliedFootForwardWorld = FootForwardForRotationWorld.GetSafeNormal();
			SideLegState.bHasSmoothedAppliedFootForward = true;
		}
		else
		{
			SideLegState.SmoothedAppliedFootForwardWorld = MediaPipeBodySolverMath::ApproachDirection(
				SideLegState.SmoothedAppliedFootForwardWorld,
				FootForwardForRotationWorld,
				DeltaSeconds,
				FMath::Max(CVarMediaPipeFootForwardSmoothingHalfLife.GetValueOnAnyThread(), 0.0f),
				CVarMediaPipeFootForwardMaxTurnDegPerSec.GetValueOnAnyThread());
		}
		FootForwardForRotationWorld = SideLegState.SmoothedAppliedFootForwardWorld;
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
			TEXT("mp.MediaPipeLegSolve.Debug actor=%s side=%s path=%s bDriveLegs=%d bHasRefLegL=%d bHasRefLegR=%d avatarLockedReplay=%d bodyFusionPoseUsable=%d bUseBodyFusionLowerBody=%d bUsingBodyFusionLowerBody=%d tryGetMediaPipeLowerBodySide=%d hip=%s knee=%s ankle=%s heel=%s toe=%s desiredThigh=%s desiredCalf=%s apply(thigh=%d calf=%d foot=%d) useLegIK=%d useFootPlant=%d driveFootRotation=%d scaffold(sample=%d hmdShare=%.2f pelvisDropCm=%.1f flexMeas=%.1f flexTarget=%.1f flexApplied=%.1f grounded=%d nearFloor=%d liftCm=%.1f)"),
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
			bDriveFootRotationForSide ? 1 : 0,
			BodyState.bHasLowerBodyScaffoldSample ? 1 : 0,
			BodyState.ScaffoldHmdShare01,
			BodyState.ScaffoldPelvisDropCm,
			SideLegState.LastFlexionMeasuredDeg,
			SideLegState.LastFlexionTargetDeg,
			SideLegState.LastFlexionAppliedDeltaDeg,
			bCurrentSourceFootGrounded ? 1 : 0,
			bCurrentSourceFootNearFloor ? 1 : 0,
			SideLegState.LastFootLiftCm);
	};
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

void FAnimNode_MediaPipePoseDriven::EmitLegScaffoldDiagnostics(float DeltaSeconds)
{
	if (!bDriveLegs || CVarMediaPipeLegScaffoldLog.GetValueOnAnyThread() == 0)
	{
		return;
	}

	const double NowSeconds = FPlatformTime::Seconds();
	const float IntervalSeconds = FMath::Max(CVarMediaPipeLegScaffoldLogInterval.GetValueOnAnyThread(), 0.25f);
	if (LastLegScaffoldLogTimeSeconds >= 0.0 && (NowSeconds - LastLegScaffoldLogTimeSeconds) < IntervalSeconds)
	{
		return;
	}
	LastLegScaffoldLogTimeSeconds = NowSeconds;

	auto SideText = [](const FMediaPipeLegSolverState& Side)
	{
		return FString::Printf(
			TEXT("flexMeas=%.1f flexTarget=%.1f flexApplied=%.1f adjusted=%d shinTilt=%.1f redist=%.1f footPitch=%.1f heelPitch=%.1f grounded=%d nearFloor=%d liftCm=%.1f plantLock=%d"),
			Side.LastFlexionMeasuredDeg,
			Side.LastFlexionTargetDeg,
			Side.LastFlexionAppliedDeltaDeg,
			Side.bLastFlexionAdjustApplied ? 1 : 0,
			Side.LastShinTiltDeg,
			Side.LastBendRedistributionDeg,
			Side.LastFootPitchAppliedDeg,
			Side.LastFootExtraDownPitchDeg,
			Side.bCurrentSourceFootGrounded ? 1 : 0,
			Side.bCurrentSourceFootNearFloor ? 1 : 0,
			Side.LastFootLiftCm,
			Side.bFootPlantLocked ? 1 : 0);
	};

	// Source contributions of the lower-body solve: MediaPipe leg intent (mono alpha + per-leg
	// flexion intent), Quest/HMD metric scaffold (baseline/drop/lean/alpha/confidence), fused
	// pelvis compression, foot contact, and the resulting pelvis/root corrections.
	UE_LOG(LogMediaPipePose, Log,
		TEXT("mp.MediaPipeLegScaffold actor=%s neutralGate(armed=%d ready=%d settle=%.1f) bodyYaw(deg=%.1f src=%s) sway(x=%.1f y=%.1f active=%d) hmd(valid=%d z=%.1f base=%.1f dropCm=%.1f leanCm=%.1f alpha=%.3f conf=%.2f) mono(alpha=%.3f) fused(alpha=%.3f hmdShare=%.2f pelvisDropCm=%.1f) pelvisOffsetZ=%.1f fkRootZ=%.1f L(%s) R(%s)"),
		*TargetActorName.ToString(),
		BodyState.bLiveNeutralGateArmed ? 1 : 0,
		BodyState.bLiveNeutralsReady ? 1 : 0,
		BodyState.LiveNeutralSettleSeconds,
		BodyState.LastBodyYawDeg,
		BodyState.LastBodyYawSource == 2 ? TEXT("bodyTracking") : (BodyState.LastBodyYawSource == 1 ? TEXT("hmdNeutral") : TEXT("none")),
		BodyState.BodyTrackingSwayWorldXY.X,
		BodyState.BodyTrackingSwayWorldXY.Y,
		BodyState.bBodyTrackingSwayActive ? 1 : 0,
		BodyState.bScaffoldHmdPoseValid ? 1 : 0,
		BodyState.ScaffoldHmdHeightZ,
		BodyState.ScaffoldHmdBaselineZ,
		BodyState.ScaffoldHmdHeadDropCm,
		BodyState.ScaffoldHmdLeanCompCm,
		BodyState.ScaffoldHmdAlpha01,
		BodyState.ScaffoldHmdConfidence,
		BodyState.ScaffoldMonoAlpha01,
		BodyState.ScaffoldFusedAlpha01,
		BodyState.ScaffoldHmdShare01,
		BodyState.ScaffoldPelvisDropCm,
		BodyState.SmoothedPelvisOffsetComp.Z,
		BodyState.SmoothedFkRootGroundOffsetComp.Z,
		*SideText(LeftLegState),
		*SideText(RightLegState));
}
