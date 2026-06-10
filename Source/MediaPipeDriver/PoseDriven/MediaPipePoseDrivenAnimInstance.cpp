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

namespace
{
FCriticalSection GMediaPipePoseDrivenSignalSnapshotsCritical;
TMap<uint32, FMediaPipePoseDrivenSignalSnapshot> GMediaPipePoseDrivenSignalSnapshotsByRuntimeKey;
}
#include "HAL/IConsoleManager.h"
#include "HeadMountedDisplayTypes.h"
#include "Math/RotationMatrix.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#endif

namespace
{
	using namespace MediaPipeRuntimeCVars;
	using namespace MediaPipeBodySolverMath;
	using namespace MediaPipeQuestFingerSolver;

	struct FPoseYawAlignRuntimeState
	{
		bool bWasEnabled = false;
		bool bHasState = false;
		double LastLogTimeSeconds = -1.0;
		double LastUpdateTimeSeconds = -1.0;
		float SmoothedDeltaDeg = 0.0f;
		float LastRawYawDeg = 0.0f;
		float LastDesiredYawDeg = 0.0f;

		void Reset()
		{
			bHasState = false;
			LastLogTimeSeconds = -1.0;
			LastUpdateTimeSeconds = -1.0;
			SmoothedDeltaDeg = 0.0f;
			LastRawYawDeg = 0.0f;
			LastDesiredYawDeg = 0.0f;
		}
	};

	TMap<uint32, FPoseYawAlignRuntimeState> GPoseYawAlignRuntimeStates;

	FPoseYawAlignRuntimeState& GetPoseYawAlignRuntimeState(uint32 Key)
	{
		return GPoseYawAlignRuntimeStates.FindOrAdd(Key);
	}

	FPoseYawAlignRuntimeState& GetPoseYawAlignRuntimeState(const UObject* KeyObject)
	{
		const uint32 Key = IsValid(KeyObject) ? KeyObject->GetUniqueID() : 0u;
		return GetPoseYawAlignRuntimeState(Key);
	}

	void ResetPoseYawAlignRuntimeState(uint32 Key)
	{
		GetPoseYawAlignRuntimeState(Key).Reset();
	}

	void ResetPoseYawAlignRuntimeState(const UObject* KeyObject)
	{
		const uint32 Key = IsValid(KeyObject) ? KeyObject->GetUniqueID() : 0u;
		ResetPoseYawAlignRuntimeState(Key);
	}

	float QuestPlanarYawDeg(const FVector& ForwardWorld)
	{
		const FVector FlatForward(ForwardWorld.X, ForwardWorld.Y, 0.0f);
		if (FlatForward.IsNearlyZero())
		{
			return 0.0f;
		}
		return FRotator::NormalizeAxis(FMath::RadiansToDegrees(FMath::Atan2(FlatForward.Y, FlatForward.X)));
	}

	TMap<uint32, FQuestWristRuntimeState> GQuestWristRuntimeStates;

	FQuestWristRuntimeState& GetQuestWristRuntimeState(uint32 Key)
	{
		return GQuestWristRuntimeStates.FindOrAdd(Key);
	}

	void ResetQuestWristRuntimeState(uint32 Key)
	{
		GetQuestWristRuntimeState(Key).Reset();
	}

	void ResetQuestWristRuntimeState(const UObject* KeyObject)
	{
		const uint32 Key = IsValid(KeyObject) ? KeyObject->GetUniqueID() : 0u;
		ResetQuestWristRuntimeState(Key);
	}

	void SetReplayFullArmJointFromLocation(
		FMediaPipeFullArmChainJointSnapshot& OutJoint,
		const FVector& LocationWorld)
	{
		OutJoint.Reset();
		if (LocationWorld.ContainsNaN())
		{
			return;
		}

		OutJoint.WorldTransform = FTransform::Identity;
		OutJoint.WorldTransform.SetLocation(LocationWorld);
		OutJoint.bValid = 1;
		OutJoint.bPositionValid = 1;
		OutJoint.bOrientationValid = 0;
	}

	bool PopulateReplayFullArmChainSide(
		const FMediaPipeTrackingArmChainSideSnapshot& SourceSide,
		const double NowSeconds,
		FMediaPipeFullArmChainSideSnapshot& OutSide)
	{
		OutSide.Reset();
		if (!SourceSide.bHasChain ||
			SourceSide.ShoulderWorld.ContainsNaN() ||
			SourceSide.ElbowWorld.ContainsNaN() ||
			SourceSide.WristWorld.ContainsNaN())
		{
			return false;
		}

		OutSide.bActive = 1;
		OutSide.bWristOrPalmUsesPalm = 0;
		OutSide.Confidence = FMath::Clamp(SourceSide.Confidence > 0.0f ? SourceSide.Confidence : 1.0f, 0.0f, 1.0f);
		OutSide.TimestampSeconds = SourceSide.TimestampSeconds >= 0.0
			? SourceSide.TimestampSeconds
			: NowSeconds;
		SetReplayFullArmJointFromLocation(OutSide.Shoulder, SourceSide.ShoulderWorld);
		SetReplayFullArmJointFromLocation(OutSide.UpperArm, SourceSide.ShoulderWorld);
		SetReplayFullArmJointFromLocation(OutSide.LowerArm, SourceSide.ElbowWorld);
		SetReplayFullArmJointFromLocation(OutSide.WristOrPalm, SourceSide.WristWorld);
		return OutSide.HasRequiredPositionChain() && OutSide.TimestampSeconds >= 0.0;
	}

	bool BuildReplayFullArmChainSnapshot(
		const FMediaPipeTrackingArmChainSourceSnapshot& SourceArmChain,
		const double NowSeconds,
		const uint32 Sequence,
		FMediaPipeFullArmChainSnapshot& OutSnapshot)
	{
		OutSnapshot.Reset();
		const bool bHasLeft = PopulateReplayFullArmChainSide(SourceArmChain.Left, NowSeconds, OutSnapshot.Left);
		const bool bHasRight = PopulateReplayFullArmChainSide(SourceArmChain.Right, NowSeconds, OutSnapshot.Right);
		if (!bHasLeft && !bHasRight)
		{
			return false;
		}

		OutSnapshot.Source = EMediaPipeFullArmChainSource::TrackingFusionReplay;
		OutSnapshot.bActive = 1;
		OutSnapshot.Sequence = Sequence;
		OutSnapshot.TimestampSeconds = NowSeconds;
		OutSnapshot.Confidence = FMath::Max(
			bHasLeft ? OutSnapshot.Left.Confidence : 0.0f,
			bHasRight ? OutSnapshot.Right.Confidence : 0.0f);
		return true;
	}

	bool BuildReplayPoseFrameFromBodyPose(
		const FMediaPipeTrackingBodyPoseSnapshot& SourceBodyPose,
		const double NowSeconds,
		FMediaPipePoseFrame& OutFrame,
		TStaticArray<FVector, MediaPipePoseLandmarkCount>& OutPoseWorld,
		TStaticArray<uint8, MediaPipePoseLandmarkCount>& OutEverMeasured)
	{
		OutFrame = FMediaPipePoseFrame();
		bool bHasRequiredTorso = true;
		for (const EMediaPipePoseLandmark RequiredLandmark : {
			EMediaPipePoseLandmark::LeftShoulder,
			EMediaPipePoseLandmark::RightShoulder,
			EMediaPipePoseLandmark::LeftHip,
			EMediaPipePoseLandmark::RightHip })
		{
			const int32 RequiredIndex = static_cast<int32>(RequiredLandmark);
			bHasRequiredTorso = bHasRequiredTorso &&
				SourceBodyPose.LandmarkValid[RequiredIndex] != 0 &&
				!SourceBodyPose.LandmarksWorld[RequiredIndex].ContainsNaN();
		}

		if (!bHasRequiredTorso)
		{
			return false;
		}

		OutFrame.TimestampUs = static_cast<int64>(NowSeconds * 1000000.0);
		OutFrame.SourceCaptureWallSeconds = NowSeconds;
		OutFrame.EnqueueWallSeconds = NowSeconds;
		OutFrame.WorkerStartWallSeconds = NowSeconds;
		OutFrame.NativeProcessEndWallSeconds = NowSeconds;
		OutFrame.LandmarkEndWallSeconds = NowSeconds;
		OutFrame.PublishWallSeconds = NowSeconds;
		OutFrame.ConditionedQueryWallSeconds = NowSeconds;
		OutFrame.bValid = true;
		OutFrame.bSourceConditioned = true;
		OutFrame.ConditioningDiagnostics.QualityScore = 1.0f;
		OutFrame.ConditioningDiagnostics.MeanLandmarkConfidence = 1.0f;
		OutFrame.ConditioningDiagnostics.RootPelvisQuality = 1.0f;
		OutFrame.ConditioningDiagnostics.TorsoSpineQuality = 1.0f;
		OutFrame.ConditioningDiagnostics.HipsQuality = 1.0f;
		OutFrame.ConditioningDiagnostics.LegsQuality = 1.0f;
		OutFrame.ConditioningDiagnostics.FeetAnklesQuality = 1.0f;

		for (int32 Index = 0; Index < MediaPipePoseLandmarkCount; ++Index)
		{
			const bool bValidLandmark =
				SourceBodyPose.LandmarkValid[Index] != 0 &&
				!SourceBodyPose.LandmarksWorld[Index].ContainsNaN();
			const FVector LocationWorld = bValidLandmark
				? SourceBodyPose.LandmarksWorld[Index]
				: FVector::ZeroVector;
			const float Reliability = bValidLandmark
				? FMath::Clamp(SourceBodyPose.LandmarkReliability[Index] > 0.0f ? SourceBodyPose.LandmarkReliability[Index] : 1.0f, 0.0f, 1.0f)
				: 0.0f;

			OutPoseWorld[Index] = LocationWorld;
			OutEverMeasured[Index] = bValidLandmark ? 1 : 0;
			OutFrame.World.Points[Index].X = LocationWorld.X;
			OutFrame.World.Points[Index].Y = LocationWorld.Y;
			OutFrame.World.Points[Index].Z = LocationWorld.Z;
			OutFrame.World.Points[Index].Visibility = Reliability;
			OutFrame.World.Points[Index].Presence = Reliability;
			OutFrame.World.Points[Index].Reliability = Reliability;
			OutFrame.Normalized.Points[Index].X = LocationWorld.X;
			OutFrame.Normalized.Points[Index].Y = LocationWorld.Y;
			OutFrame.Normalized.Points[Index].Z = LocationWorld.Z;
			OutFrame.Normalized.Points[Index].Visibility = Reliability;
			OutFrame.Normalized.Points[Index].Presence = Reliability;
			OutFrame.Normalized.Points[Index].Reliability = Reliability;
		}

		return true;
	}

	struct FDerivedSignalRuntimeState
	{
		bool bHasHeadScreenReference = false;
		FVector2D HeadScreenCenterReference = FVector2D::ZeroVector;
		FVector2D HeadScreenNoseReference = FVector2D::ZeroVector;
		FVector2D HeadScreenShoulderNoseReference = FVector2D::ZeroVector;
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
		bool bHasDenseFaceReference = false;
		float DenseFacePitchReference = 0.0f;
		float DenseFaceYawReference = 0.0f;
		float DenseFaceRollReferenceDeg = 0.0f;
		bool bHasValidDenseFacePitchDelta = false;
		float LastValidDenseFacePitchDelta = 0.0f;
		bool bHasValidDenseHeadLocalEuler = false;
		float LastDenseHeadLocalPitchDeg = 0.0f;
		float LastDenseHeadLocalYawDeg = 0.0f;
		float LastDenseHeadLocalRollDeg = 0.0f;
		bool bHasDenseFaceRelativeBasisReference = false;
		FQuat DenseFaceRelativeBasisReference = FQuat::Identity;
		bool bHasValidDenseHeadTargetBasis = false;
		FQuat LastDenseHeadTargetBasis = FQuat::Identity;
		int64 LastDenseHeadTargetPoseTimestampUs = -1;
		bool bHasBilateralShoulderHeadClearanceReference = false;
		float BilateralShoulderHeadClearanceReferenceCm = 0.0f;
		int64 LastBilateralShoulderHeadClearanceReferencePoseTimestampUs = -1;
		bool bHasValidScreenFacePitchInput = false;
		float LastValidScreenFacePitchInput = 0.0f;

		void Reset()
		{
			bHasHeadScreenReference = false;
			HeadScreenCenterReference = FVector2D::ZeroVector;
			HeadScreenNoseReference = FVector2D::ZeroVector;
			HeadScreenShoulderNoseReference = FVector2D::ZeroVector;
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
			bHasDenseFaceReference = false;
			DenseFacePitchReference = 0.0f;
			DenseFaceYawReference = 0.0f;
			DenseFaceRollReferenceDeg = 0.0f;
			bHasValidDenseFacePitchDelta = false;
			LastValidDenseFacePitchDelta = 0.0f;
			bHasValidDenseHeadLocalEuler = false;
			LastDenseHeadLocalPitchDeg = 0.0f;
			LastDenseHeadLocalYawDeg = 0.0f;
			LastDenseHeadLocalRollDeg = 0.0f;
			bHasDenseFaceRelativeBasisReference = false;
			DenseFaceRelativeBasisReference = FQuat::Identity;
			bHasValidDenseHeadTargetBasis = false;
			LastDenseHeadTargetBasis = FQuat::Identity;
			LastDenseHeadTargetPoseTimestampUs = -1;
			bHasBilateralShoulderHeadClearanceReference = false;
			BilateralShoulderHeadClearanceReferenceCm = 0.0f;
			LastBilateralShoulderHeadClearanceReferencePoseTimestampUs = -1;
			bHasValidScreenFacePitchInput = false;
			LastValidScreenFacePitchInput = 0.0f;
		}
	};

	TMap<uint32, FDerivedSignalRuntimeState> GDerivedSignalRuntimeStates;

	FDerivedSignalRuntimeState& GetDerivedSignalRuntimeState(uint32 Key)
	{
		return GDerivedSignalRuntimeStates.FindOrAdd(Key);
	}

	void ResetDerivedSignalRuntimeState(uint32 Key)
	{
		GetDerivedSignalRuntimeState(Key).Reset();
	}

	void ResetDerivedSignalRuntimeState(const UObject* KeyObject)
	{
		const uint32 Key = IsValid(KeyObject) ? KeyObject->GetUniqueID() : 0u;
		ResetDerivedSignalRuntimeState(Key);
	}

	constexpr int32 HandLm_Wrist = 0;
	constexpr int32 HandLm_IndexMcp = 5;
	constexpr int32 HandLm_MiddleMcp = 9;
	constexpr int32 HandLm_PinkyMcp = 17;

	bool IsFiniteVector(const FVector& Vector)
	{
		return FMath::IsFinite(Vector.X) && FMath::IsFinite(Vector.Y) && FMath::IsFinite(Vector.Z);
	}

#if WITH_DEV_AUTOMATION_TESTS
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FMediaPipePoseDrivenMetaHumanNoMediaPipeNeckAlphaAutomationTest,
		"TestingKit3.MediaPipe.PoseDriven.MetaHumanNoMediaPipeNeckAlpha",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FMediaPipePoseDrivenMetaHumanNoMediaPipeNeckAlphaAutomationTest::RunTest(const FString& Parameters)
	{
		float NeckAlpha = 0.0f;
		float Neck02Alpha = 0.0f;
		FMediaPipeAvatarPoseWriter::ResolveNeckChainAlphas(0.63f, 0.74f, NeckAlpha, Neck02Alpha);

		TestEqual(TEXT("Neck alpha preserves the reference MetaHuman chain spacing"), NeckAlpha, 0.63f);
		TestEqual(TEXT("Neck02 alpha preserves the reference MetaHuman chain spacing"), Neck02Alpha, 0.74f);
		TestTrue(TEXT("No-MediaPipe MetaHuman fallback does not force neck_01 up near the head"),
			NeckAlpha < 0.72f);
		TestTrue(TEXT("No-MediaPipe MetaHuman fallback does not force neck_02 up near the head"),
			Neck02Alpha < 0.88f);

		float ReorderedNeckAlpha = 0.0f;
		float ReorderedNeck02Alpha = 0.0f;
		FMediaPipeAvatarPoseWriter::ResolveNeckChainAlphas(0.72f, 0.68f, ReorderedNeckAlpha, ReorderedNeck02Alpha);
		TestEqual(TEXT("Neck02 remains above neck when source data is reordered"), ReorderedNeck02Alpha, ReorderedNeckAlpha);
		return true;
	}
#endif

	bool IsUsableQuestWristPosition(const FVector& WristWorld)
	{
		return IsFiniteVector(WristWorld) && !WristWorld.IsNearlyZero();
	}

	bool IsQuestHandSideTracked(const FQuestHandTrackingSnapshot& Snapshot, const bool bIsLeft)
	{
		return bIsLeft ? (Snapshot.bHasLeft != 0 && Snapshot.bLeftTracked != 0) : (Snapshot.bHasRight != 0 && Snapshot.bRightTracked != 0);
	}

	bool IsQuestHandSideAvailable(const FQuestHandTrackingSnapshot& Snapshot, const bool bIsLeft)
	{
		return bIsLeft ? (Snapshot.bHasLeft != 0) : (Snapshot.bHasRight != 0);
	}

	bool IsQuestHandSideUsableForWrist(const FQuestHandTrackingSnapshot& Snapshot, const bool bIsLeft)
	{
		const bool bHasSide = bIsLeft ? (Snapshot.bHasLeft != 0) : (Snapshot.bHasRight != 0);
		if (!bHasSide)
		{
			return false;
		}

		const TStaticArray<FVector, QuestHandKeypointCount>& Positions = bIsLeft ? Snapshot.LeftPositionsWorld : Snapshot.RightPositionsWorld;
		return IsUsableQuestWristPosition(Positions[static_cast<int32>(EHandKeypoint::Wrist)]);
	}

	const TStaticArray<FVector, QuestHandKeypointCount>& GetQuestHandPositions(const FQuestHandTrackingSnapshot& Snapshot, const bool bIsLeft)
	{
		return bIsLeft ? Snapshot.LeftPositionsWorld : Snapshot.RightPositionsWorld;
	}

	bool TryBuildQuestHandBasisWorld(const FQuestHandTrackingSnapshot& Snapshot, const bool bIsLeft, FVector& OutForwardWorld, FVector& OutUpWorld, float& OutBasisSin, const bool bUseMannyHandBasisConvention = true)
	{
		if (!IsQuestHandSideAvailable(Snapshot, bIsLeft))
		{
			return false;
		}

		const TStaticArray<FVector, QuestHandKeypointCount>& Positions = GetQuestHandPositions(Snapshot, bIsLeft);
		const FVector Wrist = Positions[static_cast<int32>(EHandKeypoint::Wrist)];
		const FVector IndexProximal = Positions[static_cast<int32>(EHandKeypoint::IndexProximal)];
		const FVector LittleProximal = Positions[static_cast<int32>(EHandKeypoint::LittleProximal)];
		const FVector MiddleProximal = Positions[static_cast<int32>(EHandKeypoint::MiddleProximal)];

		FVector Forward = ((IndexProximal + LittleProximal) * 0.5f - Wrist).GetSafeNormal();
		if (Forward.IsNearlyZero())
		{
			Forward = (MiddleProximal - Wrist).GetSafeNormal();
		}

		const FVector Across = (IndexProximal - LittleProximal).GetSafeNormal();
		if (Forward.IsNearlyZero() || Across.IsNearlyZero())
		{
			return false;
		}

		FVector Up = FVector::CrossProduct(Forward, Across).GetSafeNormal();
		OutBasisSin = FVector::CrossProduct(Forward, Across).Size();
		if (bUseMannyHandBasisConvention && !bIsLeft)
		{
			Forward *= -1.0f;
		}
		if (Up.IsNearlyZero())
		{
			return false;
		}

		OutForwardWorld = Forward;
		OutUpWorld = Up;
		return true;
	}

	bool DecomposeSwingTwistDegAroundAxis(const FQuat& InRot, const FVector& Axis, FQuat& OutSwing, float& OutTwistDeg)
	{
		const FVector AxisN = Axis.GetSafeNormal();
		if (AxisN.IsNearlyZero())
		{
			return false;
		}

		const FQuat Q = InRot.GetNormalized();
		const FVector V(Q.X, Q.Y, Q.Z);
		const FVector Proj = FVector::DotProduct(V, AxisN) * AxisN;

		FQuat Twist(Proj.X, Proj.Y, Proj.Z, Q.W);
		const float TwistSizeSq = Twist.SizeSquared();
		if (TwistSizeSq <= KINDA_SMALL_NUMBER)
		{
			OutSwing = Q;
			OutTwistDeg = 0.0f;
			return true;
		}
		Twist.Normalize();

		OutSwing = (Q * Twist.Inverse()).GetNormalized();

		FVector TwistAxis = FVector::ZeroVector;
		float TwistRad = 0.0f;
		Twist.ToAxisAndAngle(TwistAxis, TwistRad);
		if (FVector::DotProduct(TwistAxis, AxisN) < 0.0f)
		{
			TwistRad *= -1.0f;
		}
		OutTwistDeg = FRotator::NormalizeAxis(FMath::RadiansToDegrees(TwistRad));
		return true;
	}

	FQuat FilterTargetRotationSwingTwist(
		const FQuat& RefRot,
		const FVector& TwistAxis,
		const FQuat& TargetRot,
		const float TwistWeight,
		const float TwistClampDegrees)
	{
		const FVector AxisN = TwistAxis.GetSafeNormal();
		if (AxisN.IsNearlyZero())
		{
			return TargetRot.GetNormalized();
		}

		const FQuat Delta = (TargetRot * RefRot.Inverse()).GetNormalized();
		FQuat Swing = FQuat::Identity;
		float TwistDeg = 0.0f;
		if (!DecomposeSwingTwistDegAroundAxis(Delta, AxisN, Swing, TwistDeg))
		{
			return TargetRot.GetNormalized();
		}

		float FilteredTwistDeg = TwistDeg;
		if (TwistClampDegrees > 0.0f)
		{
			FilteredTwistDeg = FMath::Clamp(FilteredTwistDeg, -TwistClampDegrees, TwistClampDegrees);
		}

		const float TwistW = FMath::Clamp(TwistWeight, 0.0f, 1.0f);
		FilteredTwistDeg *= TwistW;

		const FQuat TwistScaled(AxisN, FMath::DegreesToRadians(FilteredTwistDeg));
		return (Swing * TwistScaled * RefRot).GetNormalized();
	}

}

static FVector LockVectorToHemisphere(const FVector& Vector, const FVector& Reference);

FAnimNode_MediaPipePoseDriven::FAnimNode_MediaPipePoseDriven()
{
	const FMediaPipeSkeletonPoseBinding PoseBinding = FMediaPipeSkeletonPoseBinding::Manny();
	const FMediaPipeMetaHumanHelperBoneBinding MetaHumanHelperBinding = FMediaPipeMetaHumanHelperBoneBinding::Default();
	Root.BoneName = PoseBinding.Root;
	Pelvis.BoneName = PoseBinding.Pelvis;
	Spine01.BoneName = PoseBinding.Spine01;
	Spine02.BoneName = PoseBinding.Spine02;
	Spine03.BoneName = PoseBinding.Spine03;
	Spine04.BoneName = PoseBinding.Spine04;
	Spine05.BoneName = PoseBinding.Spine05;
	Neck.BoneName = PoseBinding.Neck;
	Neck02.BoneName = PoseBinding.Neck02;
	Head.BoneName = PoseBinding.Head;

	ClavicleL.BoneName = PoseBinding.ClavicleL;
	UpperArmL.BoneName = PoseBinding.UpperArmL;
	UpperArmTwist01L.BoneName = PoseBinding.UpperArmTwist01L;
	UpperArmTwist02L.BoneName = PoseBinding.UpperArmTwist02L;
	LowerArmL.BoneName = PoseBinding.LowerArmL;
	LowerArmTwist01L.BoneName = PoseBinding.LowerArmTwist01L;
	LowerArmTwist02L.BoneName = PoseBinding.LowerArmTwist02L;
	HandL.BoneName = PoseBinding.HandL;
	for (int32 Index = 0; Index < MediaPipeMetaHumanClavicleHelperCount; ++Index)
	{
		MetaHumanClavicleHelpersL[Index].BoneName = MetaHumanHelperBinding.ClavicleL[Index];
	}
	for (int32 Index = 0; Index < MediaPipeMetaHumanUpperArmHelperCount; ++Index)
	{
		MetaHumanUpperArmHelpersL[Index].BoneName = MetaHumanHelperBinding.UpperArmL[Index];
	}
	for (int32 Index = 0; Index < MediaPipeMetaHumanLowerArmHelperCount; ++Index)
	{
		MetaHumanLowerArmHelpersL[Index].BoneName = MetaHumanHelperBinding.LowerArmL[Index];
	}
	for (int32 Index = 0; Index < QuestFingerMetacarpalBoneCount; ++Index)
	{
		FingerMetacarpalBonesL[Index].BoneName = FName(QuestFingerMetacarpalBoneNamesL[Index]);
	}
	for (int32 Index = 0; Index < QuestFingerBoneCount; ++Index)
	{
		FingerBonesL[Index].BoneName = FName(QuestFingerBoneNamesL[Index]);
	}

	ClavicleR.BoneName = PoseBinding.ClavicleR;
	UpperArmR.BoneName = PoseBinding.UpperArmR;
	UpperArmTwist01R.BoneName = PoseBinding.UpperArmTwist01R;
	UpperArmTwist02R.BoneName = PoseBinding.UpperArmTwist02R;
	LowerArmR.BoneName = PoseBinding.LowerArmR;
	LowerArmTwist01R.BoneName = PoseBinding.LowerArmTwist01R;
	LowerArmTwist02R.BoneName = PoseBinding.LowerArmTwist02R;
	HandR.BoneName = PoseBinding.HandR;
	for (int32 Index = 0; Index < MediaPipeMetaHumanClavicleHelperCount; ++Index)
	{
		MetaHumanClavicleHelpersR[Index].BoneName = MetaHumanHelperBinding.ClavicleR[Index];
	}
	for (int32 Index = 0; Index < MediaPipeMetaHumanUpperArmHelperCount; ++Index)
	{
		MetaHumanUpperArmHelpersR[Index].BoneName = MetaHumanHelperBinding.UpperArmR[Index];
	}
	for (int32 Index = 0; Index < MediaPipeMetaHumanLowerArmHelperCount; ++Index)
	{
		MetaHumanLowerArmHelpersR[Index].BoneName = MetaHumanHelperBinding.LowerArmR[Index];
	}
	for (int32 Index = 0; Index < QuestFingerMetacarpalBoneCount; ++Index)
	{
		FingerMetacarpalBonesR[Index].BoneName = FName(QuestFingerMetacarpalBoneNamesR[Index]);
	}
	for (int32 Index = 0; Index < QuestFingerBoneCount; ++Index)
	{
		FingerBonesR[Index].BoneName = FName(QuestFingerBoneNamesR[Index]);
	}

	ThighL.BoneName = PoseBinding.ThighL;
	CalfL.BoneName = PoseBinding.CalfL;
	FootL.BoneName = PoseBinding.FootL;
	BallL.BoneName = PoseBinding.BallL;

	ThighR.BoneName = PoseBinding.ThighR;
	CalfR.BoneName = PoseBinding.CalfR;
	FootR.BoneName = PoseBinding.FootR;
	BallR.BoneName = PoseBinding.BallR;
}

void FAnimNode_MediaPipePoseDriven::Initialize_AnyThread(const FAnimationInitializeContext& Context)
{
	FAnimNode_Base::Initialize_AnyThread(Context);
	CachedDeltaTimeSeconds = 0.0f;
}

void FAnimNode_MediaPipePoseDriven::CacheBones_AnyThread(const FAnimationCacheBonesContext& Context)
{
	FAnimNode_Base::CacheBones_AnyThread(Context);

	const FBoneContainer& RequiredBones = Context.AnimInstanceProxy->GetRequiredBones();

	Root.Initialize(RequiredBones);
	Pelvis.Initialize(RequiredBones);
	Spine01.Initialize(RequiredBones);
	Spine02.Initialize(RequiredBones);
	Spine03.Initialize(RequiredBones);
	Spine04.Initialize(RequiredBones);
	Spine05.Initialize(RequiredBones);
	Neck.Initialize(RequiredBones);
	Neck02.Initialize(RequiredBones);
	Head.Initialize(RequiredBones);

	ClavicleL.Initialize(RequiredBones);
	UpperArmL.Initialize(RequiredBones);
	UpperArmTwist01L.Initialize(RequiredBones);
	UpperArmTwist02L.Initialize(RequiredBones);
	LowerArmL.Initialize(RequiredBones);
	LowerArmTwist01L.Initialize(RequiredBones);
	LowerArmTwist02L.Initialize(RequiredBones);
	HandL.Initialize(RequiredBones);
	for (int32 Index = 0; Index < MediaPipeMetaHumanClavicleHelperCount; ++Index)
	{
		MetaHumanClavicleHelpersL[Index].Initialize(RequiredBones);
	}
	for (int32 Index = 0; Index < MediaPipeMetaHumanUpperArmHelperCount; ++Index)
	{
		MetaHumanUpperArmHelpersL[Index].Initialize(RequiredBones);
	}
	for (int32 Index = 0; Index < MediaPipeMetaHumanLowerArmHelperCount; ++Index)
	{
		MetaHumanLowerArmHelpersL[Index].Initialize(RequiredBones);
	}
	for (int32 Index = 0; Index < QuestFingerMetacarpalBoneCount; ++Index)
	{
		FingerMetacarpalBonesL[Index].Initialize(RequiredBones);
	}
	for (int32 Index = 0; Index < QuestFingerBoneCount; ++Index)
	{
		FingerBonesL[Index].Initialize(RequiredBones);
	}

	ClavicleR.Initialize(RequiredBones);
	UpperArmR.Initialize(RequiredBones);
	UpperArmTwist01R.Initialize(RequiredBones);
	UpperArmTwist02R.Initialize(RequiredBones);
	LowerArmR.Initialize(RequiredBones);
	LowerArmTwist01R.Initialize(RequiredBones);
	LowerArmTwist02R.Initialize(RequiredBones);
	HandR.Initialize(RequiredBones);
	for (int32 Index = 0; Index < MediaPipeMetaHumanClavicleHelperCount; ++Index)
	{
		MetaHumanClavicleHelpersR[Index].Initialize(RequiredBones);
	}
	for (int32 Index = 0; Index < MediaPipeMetaHumanUpperArmHelperCount; ++Index)
	{
		MetaHumanUpperArmHelpersR[Index].Initialize(RequiredBones);
	}
	for (int32 Index = 0; Index < MediaPipeMetaHumanLowerArmHelperCount; ++Index)
	{
		MetaHumanLowerArmHelpersR[Index].Initialize(RequiredBones);
	}
	for (int32 Index = 0; Index < QuestFingerMetacarpalBoneCount; ++Index)
	{
		FingerMetacarpalBonesR[Index].Initialize(RequiredBones);
	}
	for (int32 Index = 0; Index < QuestFingerBoneCount; ++Index)
	{
		FingerBonesR[Index].Initialize(RequiredBones);
	}

	ThighL.Initialize(RequiredBones);
	CalfL.Initialize(RequiredBones);
	FootL.Initialize(RequiredBones);
	BallL.Initialize(RequiredBones);

	ThighR.Initialize(RequiredBones);
	CalfR.Initialize(RequiredBones);
	FootR.Initialize(RequiredBones);
	BallR.Initialize(RequiredBones);

	bHasReferencePose = BuildReferencePoseCache(RequiredBones);
}

void FAnimNode_MediaPipePoseDriven::Update_AnyThread(const FAnimationUpdateContext& Context)
{
	FAnimNode_Base::Update_AnyThread(Context);
	GetEvaluateGraphExposedInputs().Execute(Context);
	CachedDeltaTimeSeconds += Context.GetDeltaTime();
}

void FAnimNode_MediaPipePoseDriven::PreUpdate(const UAnimInstance* InAnimInstance)
{
	TargetActorName = NAME_None;
	TargetEmbodimentProfile = FMediaPipeAvatarEmbodimentProfile();
	bHasTargetEmbodimentProfile = false;
	bUseTargetFaceForwardAxis = false;
	bHasTargetEyeLocalOffset = false;
	TargetEyeLocalOffset = FVector::ZeroVector;
	TargetEmbodiedCameraForwardOffsetCm = 0.0f;
	RuntimeStateKey = 0;
	LatestSignalSnapshot.Reset();
	QuestHands.Reset();
	FullArmChain.Reset();
	BodyFusionFrame.ResetTransient();
	bHasQuestOrHmdRuntimeInput = false;
	TargetMetaHumanProfile.Reset();
	bHasCachedQuestHmdPose = false;
	CachedQuestHmdWorld = FVector::ZeroVector;
	CachedQuestHmdRotWorld = FQuat::Identity;
	CachedQuestTrackingUpWorld = FVector::UpVector;

	const USkeletalMeshComponent* SkelComp = InAnimInstance ? InAnimInstance->GetSkelMeshComponent() : nullptr;
	if (!SkelComp)
	{
		return;
	}
	RuntimeStateKey = SkelComp->GetUniqueID();
	LatestSignalSnapshot.RuntimeStateKey = RuntimeStateKey;
	TargetCompTransform = SkelComp->GetComponentTransform();

	if (AActor* TargetActor = SkelComp->GetOwner())
	{
		TargetActorName = FName(*TargetActor->GetActorNameOrLabel());
	}
	const FMediaPipeResolvedAvatarProfile ResolvedProfile =
		FMediaPipeAvatarProfileResolver::ResolveForComponent(SkelComp);
	TargetActorName = ResolvedProfile.TargetActorName.IsNone()
		? TargetActorName
		: ResolvedProfile.TargetActorName;
	TargetEmbodimentProfile = ResolvedProfile.EmbodimentProfile;
	bHasTargetEmbodimentProfile = ResolvedProfile.bHasEmbodimentProfile;
	bUseTargetFaceForwardAxis = ResolvedProfile.bUseTargetFaceForwardAxis;
	TargetEyeLocalOffset = ResolvedProfile.TargetEyeLocalOffset;
	bHasTargetEyeLocalOffset = ResolvedProfile.bHasTargetEyeLocalOffset;
	TargetEmbodiedCameraForwardOffsetCm = ResolvedProfile.TargetEmbodiedCameraForwardOffsetCm;
	TargetMetaHumanProfile = ResolvedProfile.MetaHumanProfile;
	ApplyReferencePoseProportionsToTargetProfile();

	if (bResetPoseStateNextUpdate)
	{
		BodyState.bHasReferenceHipHeight = false;
		BodyState.ReferenceHipHeightCm = 0.0f;
		BodyState.bHasSmoothedPelvisOffset = false;
		BodyState.SmoothedPelvisOffsetComp = FVector::ZeroVector;
		BodyState.bHasSmoothedStage1ChestOffset = false;
		BodyState.SmoothedStage1ChestOffsetComp = FVector::ZeroVector;
		BodyState.bHasSmoothedStage2ClavicleLiftL = false;
		BodyState.SmoothedStage2ClavicleLiftCmL = 0.0f;
		BodyState.bHasStage2NeutralReferenceL = false;
		BodyState.Stage2NeutralShoulderLiftFromPelvisCmL = 0.0f;
		BodyState.Stage2NeutralShoulderHeadClearanceCmL = 0.0f;
		BodyState.Stage2NeutralObservationSecondsL = 0.0f;
		BodyState.Stage2NeutralObservationFramesL = 0;
		BodyState.bHasSmoothedStage2ClavicleLiftR = false;
		BodyState.SmoothedStage2ClavicleLiftCmR = 0.0f;
		BodyState.bHasStage2NeutralReferenceR = false;
		BodyState.Stage2NeutralShoulderLiftFromPelvisCmR = 0.0f;
		BodyState.Stage2NeutralShoulderHeadClearanceCmR = 0.0f;
		BodyState.Stage2NeutralObservationSecondsR = 0.0f;
		BodyState.Stage2NeutralObservationFramesR = 0;
		BodyState.bHasSmoothedFkRootGroundOffset = false;
		BodyState.SmoothedFkRootGroundOffsetComp = FVector::ZeroVector;
		LeftArmState.bHasSmoothedArmIK = false;
		RightArmState.bHasSmoothedArmIK = false;
		LeftLegState.bHasSmoothedLegPlane = false;
		RightLegState.bHasSmoothedLegPlane = false;
		ResetFootPlantState();
		ResetPoseYawAlignRuntimeState(SkelComp);
		ResetQuestWristRuntimeState(SkelComp);
		ResetRotationSmoothing();
		if (bResetDerivedSignalReferencesNextUpdate)
		{
			BodyState.ResetDerivedSignalReferences();
			ResetDerivedSignalRuntimeState(SkelComp);
			bResetDerivedSignalReferencesNextUpdate = false;
		}
		MediaPipePoseFrameContinuity::ResetHeldFrame(
			bHasPoseFrame,
			PoseFrame,
			PoseTimestampSeconds,
			bHasPoseHands,
			PoseHands,
			PoseHandsTimestampUs);
		for (uint8& B : EverMeasured)
		{
			B = 0;
		}
		bResetPoseStateNextUpdate = false;
	}

	UWorld* World = InAnimInstance ? InAnimInstance->GetWorld() : nullptr;
	if (!World)
	{
		return;
	}

	FMediaPipeAvatarProfileResolver::EmitMetaHumanProfileLogs(
		ResolvedProfile,
		RuntimeStateKey,
		World->GetTimeSeconds(),
		TargetProfileLogState);

	if (!EmbodiedFusionComponent)
	{
		if (AActor* OwnerActor = SkelComp->GetOwner())
		{
			EmbodiedFusionComponent = OwnerActor->FindComponentByClass<UEmbodiedFusionComponent>();
		}
	}
	if (!EmbodiedFusionComponent)
	{
		return;
	}

	const FMediaPipeBodyFusionRuntimePolicySnapshot BodyFusionRuntimePolicy =
		FMediaPipeBodyFusionRuntimePolicy::ReadGameThread();
	const bool bBodyFusionRuntimeActive = BodyFusionRuntimePolicy.bBodyFusionEnabled;
	FEmbodiedFusionSourceObservations ReplayObservations;
	FString ReplayPhaseName;
	if (FMediaPipeTrackingFusionDatasetReplayRuntime::Get().GetCurrentObservations(
		FPlatformTime::Seconds(),
		ReplayObservations,
		&ReplayPhaseName))
	{
		const double ReplayNowSeconds = ReplayObservations.NowSeconds >= 0.0
			? ReplayObservations.NowSeconds
			: FPlatformTime::Seconds();
		FMediaPipeFullArmChainSnapshot ReplayFullArmChain;
		if (BuildReplayFullArmChainSnapshot(
			ReplayObservations.ArmChain,
			ReplayNowSeconds,
			FullArmChain.Sequence + 1u,
			ReplayFullArmChain))
		{
			FullArmChain = ReplayFullArmChain;
		}
		else
		{
			FullArmChain.Reset();
		}

		EmbodiedFusionComponent->SetReplaySourceObservations_GameThread(ReplayObservations);
		bHasCachedQuestHmdPose = ReplayObservations.HmdPose.bHasPose;
		CachedQuestHmdWorld = ReplayObservations.HmdPose.LocationWorld;
		CachedQuestHmdRotWorld = ReplayObservations.HmdPose.RotationWorld;
		CachedQuestTrackingUpWorld = ReplayObservations.HmdPose.TrackingUpWorld;

		const bool bHasReplayBodyPose = BuildReplayPoseFrameFromBodyPose(
			ReplayObservations.BodyPose,
			ReplayNowSeconds,
			PoseFrame,
			PoseWorld,
			EverMeasured);
		if (bHasReplayBodyPose)
		{
			bHasPoseFrame = true;
			PoseTimestampSeconds = ReplayNowSeconds;
			PoseToWorldTransform = FTransform::Identity;
			WorldScale = 1.0f;
			bMirrorLandmarksLR = false;
			PoseHands = FMediaPipeRawHandPair();
			PoseHandsTimestampUs = PoseFrame.TimestampUs;
			bHasPoseHands = false;
		}
		else
		{
			MediaPipePoseFrameContinuity::ResetHeldFrame(
				bHasPoseFrame,
				PoseFrame,
				PoseTimestampSeconds,
				bHasPoseHands,
				PoseHands,
				PoseHandsTimestampUs);
			for (int32 LandmarkIndex = 0; LandmarkIndex < MediaPipePoseLandmarkCount; ++LandmarkIndex)
			{
				PoseWorld[LandmarkIndex] = FVector::ZeroVector;
				EverMeasured[LandmarkIndex] = 0;
			}
		}
		bHasQuestOrHmdRuntimeInput =
			bHasCachedQuestHmdPose ||
			ReplayObservations.Hands.bHasLeft != 0 ||
			ReplayObservations.Hands.bHasRight != 0 ||
			FullArmChain.bActive != 0 ||
			bHasReplayBodyPose;

		FMediaPipeAvatarEmbodimentProfile BodyFusionProfile = bHasTargetEmbodimentProfile
			? TargetEmbodimentProfile
			: FMediaPipeAvatarEmbodimentProfile();
		ConfigureBodyFusionProfileForCurrentTarget(BodyFusionProfile);

		FEmbodiedFusionUpdateInput FusionInput;
		FusionInput.TargetActorName = TargetActorName;
		FusionInput.NowSeconds = ReplayNowSeconds;
		FusionInput.AvatarProfile = BodyFusionProfile;
		FusionInput.TargetComponentTransform = TargetCompTransform;
		FusionInput.TargetForwardWorld = GetTargetForwardWorld();
		FusionInput.RefPelvisTranslationComp = RefPelvisTranslationComp;
		FusionInput.RefHeadPosComp = RefHeadPosComp;
		FusionInput.RefChestPosComp = RefSpine05TransformComp.GetTranslation();
		FusionInput.RefNeckPosComp = RefNeckPosComp;
		FusionInput.bOverrideArmChainMaxAgeSeconds =
			TargetMetaHumanProfile.IsValidForPoseDriving();
		FusionInput.ArmChainMaxAgeSeconds =
			ResolveMediaPipeMetaHumanFullArmChainMaxAgeSeconds(TargetMetaHumanProfile);
		FusionInput.bHasRefChestPosComp = !RefSpine05TransformComp.GetTranslation().IsNearlyZero();
		EmbodiedFusionComponent->UpdateFusion_GameThread(FusionInput);
		BodyFusionFrame = EmbodiedFusionComponent->GetLatestFusionFrame();
		return;
	}
	FEmbodiedFusionQuestSourcePollInput QuestRuntimeInput;
	QuestRuntimeInput.World = World;
	QuestRuntimeInput.TargetComponent = SkelComp;
	QuestRuntimeInput.TargetActorName = TargetActorName;
	QuestRuntimeInput.bUseQuestHandTracking = bUseQuestHandTracking;
	QuestRuntimeInput.bBodyFusionRuntimeActive = bBodyFusionRuntimeActive;
	QuestRuntimeInput.bReadFullArmChain =
		TargetMetaHumanProfile.IsValidForPoseDriving() &&
		ResolveMediaPipeMetaHumanArmSourceMode(TargetMetaHumanProfile) == 1;
	const FEmbodiedFusionQuestSourcePollResult QuestRuntimeOutput =
		EmbodiedFusionComponent->PollQuestRuntimeSources_GameThread(QuestRuntimeInput, DiagnosticsState);
	QuestHands = QuestRuntimeOutput.QuestHands;
	FullArmChain = QuestRuntimeOutput.FullArmChain;
	bHasCachedQuestHmdPose = QuestRuntimeOutput.HmdPose.bHasPose;
	CachedQuestHmdWorld = QuestRuntimeOutput.HmdPose.LocationWorld;
	CachedQuestHmdRotWorld = QuestRuntimeOutput.HmdPose.RotationWorld;
	CachedQuestTrackingUpWorld = QuestRuntimeOutput.HmdPose.TrackingUpWorld;
	bHasQuestOrHmdRuntimeInput =
		bHasCachedQuestHmdPose ||
		IsQuestHandSideTracked(QuestHands, true) ||
		IsQuestHandSideTracked(QuestHands, false) ||
		FullArmChain.bActive != 0;

	const FQuestWristRuntimeState& QuestRuntimeWristState = GetQuestWristRuntimeState(RuntimeStateKey);
	FEmbodiedFusionQuestCalibrationDebugInput QuestCalibrationHudInput;
	QuestCalibrationHudInput.World = World;
	QuestCalibrationHudInput.TargetComponent = SkelComp;
	QuestCalibrationHudInput.WristState = &QuestRuntimeWristState;
	QuestCalibrationHudInput.bArmLengthCalibrationHudOwner =
		TargetMetaHumanProfile.bIsMetaHuman && TargetMetaHumanProfile.bIsActiveProfile;
	QuestCalibrationHudInput.bHasRefArmL = bHasRefArmL;
	QuestCalibrationHudInput.bHasRefArmR = bHasRefArmR;
	QuestCalibrationHudInput.RefUpperLenCompL = RefUpperLenCompL;
	QuestCalibrationHudInput.RefLowerLenCompL = RefLowerLenCompL;
	QuestCalibrationHudInput.RefUpperLenCompR = RefUpperLenCompR;
	QuestCalibrationHudInput.RefLowerLenCompR = RefLowerLenCompR;
	EmbodiedFusionComponent->DisplayQuestCalibrationHuds_GameThread(QuestCalibrationHudInput);

	FEmbodiedFusionMediaPipeSourceRead MediaPipeSourceRead;
	EmbodiedFusionComponent->ReadMediaPipeSourceFrame_GameThread(World, SourceActor, MediaPipeSourceRead);
	SourceActor = MediaPipeSourceRead.SourceActor;
	const FMediaPipePoseFrame& Frame = MediaPipeSourceRead.Frame;
	const bool bHasLivePoseFrame = MediaPipeSourceRead.bHasLivePoseFrame;
	const MediaPipePoseFrameContinuity::EFrameAvailability FrameAvailability =
		MediaPipePoseFrameContinuity::ResolveFrameAvailability(bHasLivePoseFrame ? &Frame : nullptr, bHasPoseFrame);
	if (FrameAvailability != MediaPipePoseFrameContinuity::EFrameAvailability::Live)
	{
		if (bBodyFusionRuntimeActive && bHasQuestOrHmdRuntimeInput && EmbodiedFusionComponent)
		{
			const double BodyFusionNowSeconds = FPlatformTime::Seconds();
			FMediaPipeAvatarEmbodimentProfile BodyFusionProfile = bHasTargetEmbodimentProfile
				? TargetEmbodimentProfile
				: FMediaPipeAvatarEmbodimentProfile();
			ConfigureBodyFusionProfileForCurrentTarget(BodyFusionProfile);

			FEmbodiedFusionUpdateInput FusionInput;
			FusionInput.TargetActorName = TargetActorName;
			FusionInput.NowSeconds = BodyFusionNowSeconds;
			FusionInput.AvatarProfile = BodyFusionProfile;
			FusionInput.TargetComponentTransform = TargetCompTransform;
			FusionInput.TargetForwardWorld = GetTargetForwardWorld();
			FusionInput.RefPelvisTranslationComp = RefPelvisTranslationComp;
			FusionInput.RefHeadPosComp = RefHeadPosComp;
			FusionInput.RefChestPosComp = RefSpine05TransformComp.GetTranslation();
			FusionInput.RefNeckPosComp = RefNeckPosComp;
			FusionInput.bOverrideArmChainMaxAgeSeconds =
				TargetMetaHumanProfile.IsValidForPoseDriving();
			FusionInput.ArmChainMaxAgeSeconds =
				ResolveMediaPipeMetaHumanFullArmChainMaxAgeSeconds(TargetMetaHumanProfile);
			FusionInput.bHasRefChestPosComp = !RefSpine05TransformComp.GetTranslation().IsNearlyZero();
			EmbodiedFusionComponent->UpdateFusion_GameThread(FusionInput);
			BodyFusionFrame = EmbodiedFusionComponent->GetLatestFusionFrame();
		}
		return;
	}

	const FTransform NewTargetCompTransform = SkelComp->GetComponentTransform();
	const FTransform NewPoseToWorldTransform = MediaPipeSourceRead.PoseToWorldTransform;
	const float NewWorldScale = MediaPipeSourceRead.WorldScale;
	const bool bNewMirrorLandmarksLR = MediaPipeSourceRead.bMirrorLandmarksLR;
	FEmbodiedFusionHmdRelativeAvatarDebugInput QuestAvatarDebugInput;
	QuestAvatarDebugInput.World = World;
	QuestAvatarDebugInput.TargetCompTransform = NewTargetCompTransform;
	QuestAvatarDebugInput.bUseHandTracking = bUseQuestHandTracking;
	QuestAvatarDebugInput.bHasTargetEmbodimentProfile = bHasTargetEmbodimentProfile;
	QuestAvatarDebugInput.TargetEmbodimentProfile = TargetEmbodimentProfile;
	QuestAvatarDebugInput.bUseTargetFaceForwardAxis = bUseTargetFaceForwardAxis;
	QuestAvatarDebugInput.bHasTargetEyeLocalOffset = bHasTargetEyeLocalOffset;
	QuestAvatarDebugInput.TargetEyeLocalOffset = TargetEyeLocalOffset;
	QuestAvatarDebugInput.TargetEmbodiedCameraForwardOffsetCm = TargetEmbodiedCameraForwardOffsetCm;
	EmbodiedFusionComponent->DrawHmdRelativeAvatarComparison_GameThread(QuestAvatarDebugInput);
	FPoseYawAlignRuntimeState& PoseYawAlignState = GetPoseYawAlignRuntimeState(SkelComp);
	const bool bPoseYawAlignToActor = CVarMediaPipePoseYawAlignToActor.GetValueOnGameThread() != 0;
	if (bPoseYawAlignToActor != PoseYawAlignState.bWasEnabled)
	{
		BodyState.bHasStableTorsoForwardWorld = false;
		BodyState.StableTorsoForwardWorld = FVector::ZeroVector;
		BodyState.bHasStableTorsoUpWorld = false;
		BodyState.StableTorsoUpWorld = FVector::ZeroVector;
		PoseYawAlignState.Reset();
		PoseYawAlignState.bWasEnabled = bPoseYawAlignToActor;
	}

	FMediaPipeSolvedPose SolvedPose;
	const FMediaPipeSolvedPoseOptions SolvedOptions = MediaPipeSolvedPose::MakeDefaultOptions(NewWorldScale, bNewMirrorLandmarksLR);
	if (!MediaPipeSolvedPose::BuildLocal(Frame, SolvedOptions, SolvedPose))
	{
		return;
	}

	PoseFrame = Frame;
	PoseTimestampSeconds = static_cast<double>(Frame.TimestampUs) * 1.0e-6;
	TargetCompTransform = NewTargetCompTransform;
	PoseToWorldTransform = NewPoseToWorldTransform;
	WorldScale = NewWorldScale;
	bMirrorLandmarksLR = bNewMirrorLandmarksLR;
	PoseHands = Frame.Hands;
	PoseHandsTimestampUs = Frame.TimestampUs;
	bHasPoseHands = Frame.bHasHands && (Frame.Hands.bHasLeft != 0 || Frame.Hands.bHasRight != 0);

	for (int32 Index = 0; Index < MediaPipePoseLandmarkCount; ++Index)
	{
		EverMeasured[Index] = 0;
		PoseWorld[Index] = FVector::ZeroVector;
	}

	for (int32 Index = 0; Index < MediaPipePoseLandmarkCount; ++Index)
	{
		EverMeasured[Index] = 1;
		PoseWorld[Index] = PoseToWorldTransform.TransformPosition(SolvedPose.LandmarksLocal[Index]);
	}

	if (bPoseYawAlignToActor)
	{
		constexpr int32 LShoulderIdx = static_cast<int32>(EMediaPipePoseLandmark::LeftShoulder);
		constexpr int32 RShoulderIdx = static_cast<int32>(EMediaPipePoseLandmark::RightShoulder);
		constexpr int32 LHipIdx = static_cast<int32>(EMediaPipePoseLandmark::LeftHip);
		constexpr int32 RHipIdx = static_cast<int32>(EMediaPipePoseLandmark::RightHip);
		constexpr int32 NoseIdx = static_cast<int32>(EMediaPipePoseLandmark::Nose);

		const FVector LShoulder = PoseWorld[LShoulderIdx];
		const FVector RShoulder = PoseWorld[RShoulderIdx];
		const FVector LHip = PoseWorld[LHipIdx];
		const FVector RHip = PoseWorld[RHipIdx];
		const FVector Nose = PoseWorld[NoseIdx];
		const FVector ShoulderMid = (LShoulder + RShoulder) * 0.5f;
		const FVector HipMid = (LHip + RHip) * 0.5f;
		const FVector Anchor = (ShoulderMid + HipMid) * 0.5f;

		FVector RawUp = (ShoulderMid - HipMid).GetSafeNormal();
		FVector RawHipRight = (RHip - LHip).GetSafeNormal();
		RawHipRight = (RawHipRight - FVector::DotProduct(RawHipRight, RawUp) * RawUp).GetSafeNormal();
		FVector RawForward = FVector::CrossProduct(RawHipRight, RawUp).GetSafeNormal();
		if (!RawForward.IsNearlyZero())
		{
			FVector NoseReference = Nose - ShoulderMid;
			NoseReference = (NoseReference - FVector::DotProduct(NoseReference, RawUp) * RawUp).GetSafeNormal();
			if (NoseReference.IsNearlyZero())
			{
				NoseReference = PoseToWorldTransform.GetUnitAxis(EAxis::X);
			}
			RawForward = LockVectorToHemisphere(RawForward, NoseReference);
		}

		auto ProjectHorizontal = [](const FVector& Vector) -> FVector
		{
			FVector Horizontal(Vector.X, Vector.Y, 0.0f);
			return Horizontal.GetSafeNormal();
		};

		const FVector RawForwardHorizontal = ProjectHorizontal(RawForward);
		const FVector DesiredActorForwardHorizontal = ProjectHorizontal(GetTargetForwardWorld());
		const bool bCanYawAlign = !RawUp.IsNearlyZero() && !RawHipRight.IsNearlyZero() && !RawForwardHorizontal.IsNearlyZero() && !DesiredActorForwardHorizontal.IsNearlyZero();

		float RawYawDeg = 0.0f;
		float DesiredYawDeg = 0.0f;
		float TargetDeltaYawDeg = 0.0f;
		float AppliedDeltaYawDeg = 0.0f;
		float RemainingYawErrorDeg = 0.0f;
		float RawYawJumpDeg = 0.0f;
		float DesiredYawJumpDeg = 0.0f;
		float TargetDeltaJumpDeg = 0.0f;
		float AlignDeltaSeconds = 0.0f;
		bool bRejectedYawJump = false;
		bool bRecenteredYawState = false;
		bool bAppliedYawAlignment = false;
		FVector CorrectedForwardHorizontal = RawForwardHorizontal;
		if (bCanYawAlign)
		{
			RawYawDeg = FMath::RadiansToDegrees(FMath::Atan2(RawForwardHorizontal.Y, RawForwardHorizontal.X));
			DesiredYawDeg = FMath::RadiansToDegrees(FMath::Atan2(DesiredActorForwardHorizontal.Y, DesiredActorForwardHorizontal.X));
			TargetDeltaYawDeg = FMath::FindDeltaAngleDegrees(RawYawDeg, DesiredYawDeg);

			const double NowSeconds = FPlatformTime::Seconds();
			AlignDeltaSeconds = PoseYawAlignState.LastUpdateTimeSeconds >= 0.0
				? FMath::Clamp(static_cast<float>(NowSeconds - PoseYawAlignState.LastUpdateTimeSeconds), 0.0f, 0.25f)
				: 0.0f;

			const float RejectJumpDegrees = FMath::Max(0.0f, CVarMediaPipePoseYawAlignRejectJumpDegrees.GetValueOnGameThread());
			if (!PoseYawAlignState.bHasState)
			{
				PoseYawAlignState.SmoothedDeltaDeg = TargetDeltaYawDeg;
				PoseYawAlignState.bHasState = true;
				bRecenteredYawState = true;
			}
			else
			{
				RawYawJumpDeg = FMath::Abs(FMath::FindDeltaAngleDegrees(PoseYawAlignState.LastRawYawDeg, RawYawDeg));
				DesiredYawJumpDeg = FMath::Abs(FMath::FindDeltaAngleDegrees(PoseYawAlignState.LastDesiredYawDeg, DesiredYawDeg));
				TargetDeltaJumpDeg = FMath::Abs(FMath::FindDeltaAngleDegrees(PoseYawAlignState.SmoothedDeltaDeg, TargetDeltaYawDeg));

				if (RejectJumpDegrees > KINDA_SMALL_NUMBER && DesiredYawJumpDeg > RejectJumpDegrees)
				{
					PoseYawAlignState.SmoothedDeltaDeg = TargetDeltaYawDeg;
					bRecenteredYawState = true;
				}
				else if (RejectJumpDegrees > KINDA_SMALL_NUMBER && TargetDeltaJumpDeg > RejectJumpDegrees)
				{
					bRejectedYawJump = true;
				}
				else
				{
					const float HalfLifeSeconds = FMath::Max(0.0f, CVarMediaPipePoseYawAlignHalfLife.GetValueOnGameThread());
					const float Alpha = HalfLifeToAlpha(HalfLifeSeconds, AlignDeltaSeconds);
					float StepDeg = FMath::FindDeltaAngleDegrees(PoseYawAlignState.SmoothedDeltaDeg, TargetDeltaYawDeg) * Alpha;

					const float MaxSpeedDegPerSecond = FMath::Max(0.0f, CVarMediaPipePoseYawAlignMaxSpeedDegreesPerSecond.GetValueOnGameThread());
					if (MaxSpeedDegPerSecond > KINDA_SMALL_NUMBER && AlignDeltaSeconds > KINDA_SMALL_NUMBER)
					{
						const float MaxStepDeg = MaxSpeedDegPerSecond * AlignDeltaSeconds;
						StepDeg = FMath::Clamp(StepDeg, -MaxStepDeg, MaxStepDeg);
					}

					PoseYawAlignState.SmoothedDeltaDeg = FRotator::NormalizeAxis(PoseYawAlignState.SmoothedDeltaDeg + StepDeg);
				}
			}

			if (!bRejectedYawJump)
			{
				PoseYawAlignState.LastRawYawDeg = RawYawDeg;
				PoseYawAlignState.LastDesiredYawDeg = DesiredYawDeg;
				PoseYawAlignState.LastUpdateTimeSeconds = NowSeconds;
				AppliedDeltaYawDeg = PoseYawAlignState.SmoothedDeltaDeg;
				bAppliedYawAlignment = true;
			}
			else
			{
				AppliedDeltaYawDeg = 0.0f;
			}

			const FQuat YawDeltaQuat(FVector::UpVector, FMath::DegreesToRadians(AppliedDeltaYawDeg));
			if (bAppliedYawAlignment && FMath::Abs(AppliedDeltaYawDeg) > KINDA_SMALL_NUMBER)
			{
				for (int32 Index = 0; Index < MediaPipePoseLandmarkCount; ++Index)
				{
					PoseWorld[Index] = Anchor + YawDeltaQuat.RotateVector(PoseWorld[Index] - Anchor);
				}
			}

			CorrectedForwardHorizontal = ProjectHorizontal(YawDeltaQuat.RotateVector(RawForward));
			const float CorrectedYawDeg = FMath::RadiansToDegrees(FMath::Atan2(CorrectedForwardHorizontal.Y, CorrectedForwardHorizontal.X));
			RemainingYawErrorDeg = FMath::FindDeltaAngleDegrees(CorrectedYawDeg, DesiredYawDeg);
		}

		if (CVarMediaPipeTorsoDebug.GetValueOnGameThread() != 0)
		{
			const double NowSeconds = FPlatformTime::Seconds();
			if (FMediaPipePoseDiagnosticReporter::ShouldEmitThrottled(NowSeconds, 1.0, PoseYawAlignState.LastLogTimeSeconds))
			{
				FMediaPipePoseYawAlignLogInput YawLogInput;
				YawLogInput.TargetActorName = TargetActorName;
				YawLogInput.bAppliedYawAlignment = bAppliedYawAlignment;
				YawLogInput.bRejectedYawJump = bRejectedYawJump;
				YawLogInput.bRecenteredYawState = bRecenteredYawState;
				YawLogInput.RawForwardHorizontal = RawForwardHorizontal;
				YawLogInput.DesiredActorForwardHorizontal = DesiredActorForwardHorizontal;
				YawLogInput.CorrectedForwardHorizontal = CorrectedForwardHorizontal;
				YawLogInput.RawYawDeg = RawYawDeg;
				YawLogInput.DesiredYawDeg = DesiredYawDeg;
				YawLogInput.TargetDeltaYawDeg = TargetDeltaYawDeg;
				YawLogInput.AppliedDeltaYawDeg = AppliedDeltaYawDeg;
				YawLogInput.RemainingYawErrorDeg = RemainingYawErrorDeg;
				YawLogInput.RawYawJumpDeg = RawYawJumpDeg;
				YawLogInput.DesiredYawJumpDeg = DesiredYawJumpDeg;
				YawLogInput.TargetDeltaJumpDeg = TargetDeltaJumpDeg;
				YawLogInput.AlignDeltaSeconds = AlignDeltaSeconds;
				YawLogInput.Anchor = Anchor;
				YawLogInput.TargetActorYawDeg = TargetCompTransform.Rotator().Yaw;
				YawLogInput.SourceYawDeg = PoseToWorldTransform.Rotator().Yaw;
				FMediaPipeBodyDiagnostics::EmitPoseYawAlignLog(YawLogInput);
			}
		}
	}

	if (bBodyFusionRuntimeActive)
	{
		const double BodyFusionNowSeconds = FPlatformTime::Seconds();

		FMediaPipeAvatarEmbodimentProfile BodyFusionProfile = bHasTargetEmbodimentProfile
			? TargetEmbodimentProfile
			: FMediaPipeAvatarEmbodimentProfile();
		ConfigureBodyFusionProfileForCurrentTarget(BodyFusionProfile);

		if (EmbodiedFusionComponent)
		{
			EmbodiedFusionComponent->UpdateBodyPoseObservation_GameThread(
				BodyFusionNowSeconds,
				PoseFrame,
				PoseWorld,
				EverMeasured);

			FEmbodiedFusionUpdateInput FusionInput;
			FusionInput.TargetActorName = TargetActorName;
			FusionInput.NowSeconds = BodyFusionNowSeconds;
			FusionInput.AvatarProfile = BodyFusionProfile;
			FusionInput.TargetComponentTransform = TargetCompTransform;
			FusionInput.TargetForwardWorld = GetTargetForwardWorld();
			FusionInput.RefPelvisTranslationComp = RefPelvisTranslationComp;
			FusionInput.RefHeadPosComp = RefHeadPosComp;
			FusionInput.RefChestPosComp = RefSpine05TransformComp.GetTranslation();
			FusionInput.RefNeckPosComp = RefNeckPosComp;
			FusionInput.bOverrideArmChainMaxAgeSeconds =
				TargetMetaHumanProfile.IsValidForPoseDriving();
			FusionInput.ArmChainMaxAgeSeconds =
				ResolveMediaPipeMetaHumanFullArmChainMaxAgeSeconds(TargetMetaHumanProfile);
			FusionInput.bHasRefChestPosComp = !RefSpine05TransformComp.GetTranslation().IsNearlyZero();
			EmbodiedFusionComponent->UpdateFusion_GameThread(FusionInput);
			BodyFusionFrame = EmbodiedFusionComponent->GetLatestFusionFrame();
		}
	}

	bHasPoseFrame = true;
}

FVector FAnimNode_MediaPipePoseDriven::LerpNormalized(const FVector& A, const FVector& B, float Alpha)
{
	const FVector V = FMath::Lerp(A, B, Alpha);
	return V.IsNearlyZero() ? A.GetSafeNormal() : V.GetSafeNormal();
}

FQuat FAnimNode_MediaPipePoseDriven::MakeQuatFromForwardUp(const FVector& Forward, const FVector& Up)
{
	const FVector X = Forward.GetSafeNormal();
	const FVector Z = Up.GetSafeNormal();
	if (X.IsNearlyZero() || Z.IsNearlyZero())
	{
		return FQuat::Identity;
	}

	const FMatrix M = FRotationMatrix::MakeFromXZ(X, Z);
	return M.ToQuat();
}

void FAnimNode_MediaPipePoseDriven::ConfigureBodyFusionProfileForCurrentTarget(FMediaPipeAvatarEmbodimentProfile& InOutProfile) const
{
	InOutProfile.bUseTargetFaceForwardAxis = bUseTargetFaceForwardAxis;
	InOutProfile.DefaultEyeLocalOffset = bHasTargetEyeLocalOffset
		? TargetEyeLocalOffset
		: InOutProfile.DefaultEyeLocalOffset;
	InOutProfile.EmbodiedCameraForwardOffsetCm = TargetEmbodiedCameraForwardOffsetCm;

	if (ShouldUseAvatarLockedMetaHumanReplay())
	{
		InOutProfile.PelvisAuthorityMode = EMediaPipePelvisAuthorityMode::MediaPipeHipsVerticalOnly;
		InOutProfile.UpperBodyFollowAlpha = 1.0f;
	}
}

FVector FAnimNode_MediaPipePoseDriven::GetTargetForwardWorld() const
{
	FMediaPipeAvatarEmbodimentProfile ForwardProfile = bHasTargetEmbodimentProfile
		? TargetEmbodimentProfile
		: FMediaPipeAvatarEmbodimentProfile();
	ConfigureBodyFusionProfileForCurrentTarget(ForwardProfile);
	return FMediaPipeAvatarEmbodimentSolver::GetAvatarForwardWorld(TargetCompTransform, ForwardProfile);
}

void FAnimNode_MediaPipePoseDriven::ApplyReferencePoseProportionsToTargetProfile()
{
	if (!bHasTargetEmbodimentProfile || !bHasReferencePose)
	{
		return;
	}

	FMediaPipeAvatarReferencePoseProportions Reference;
	Reference.bHasReferencePose = bHasReferencePose;
	Reference.bHasLeftArm = bHasRefArmL;
	Reference.bHasRightArm = bHasRefArmR;
	Reference.LeftUpperArmLengthCm = RefUpperLenCompL;
	Reference.RightUpperArmLengthCm = RefUpperLenCompR;
	Reference.LeftLowerArmLengthCm = RefLowerLenCompL;
	Reference.RightLowerArmLengthCm = RefLowerLenCompR;
	Reference.bHasLeftLeg = bHasRefLegL;
	Reference.bHasRightLeg = bHasRefLegR;
	Reference.LeftThighLengthCm = RefThighLenCompL;
	Reference.RightThighLengthCm = RefThighLenCompR;
	Reference.LeftCalfLengthCm = RefCalfLenCompL;
	Reference.RightCalfLengthCm = RefCalfLenCompR;
	Reference.bHasChestLocal = bHasRefChestPosComp;
	Reference.bHasNeck02Local = bHasRefNeck02PosComp;
	Reference.PelvisLocal = RefPelvisTranslationComp;
	Reference.ChestLocal = RefChestPosComp;
	Reference.NeckLocal = RefNeckPosComp;
	Reference.Neck02Local = RefNeck02PosComp;
	Reference.HeadLocal = RefHeadPosComp;
	Reference.HeadBasisComponent = RefHeadBasisComp;

	const FMediaPipeAvatarReferenceProfileCalibrationResult CalibrationResult =
		FMediaPipeAvatarProfileReferenceCalibration::ApplyReferencePose(Reference, TargetEmbodimentProfile);
	if (CalibrationResult.bResolvedEyeLocalOffset)
	{
		TargetEyeLocalOffset = TargetEmbodimentProfile.DefaultEyeLocalOffset;
		bHasTargetEyeLocalOffset = true;

		UE_LOG(LogMediaPipePose, Verbose, TEXT("Avatar embodiment anchors resolved from reference pose actor=%s eyeLocal=%s headFromEye=%.2f chestLocal=%s pelvisLocal=%s upperBodyFollow=%.2f."),
			TargetMetaHumanProfile.TargetActor.IsValid() ? *GetNameSafe(TargetMetaHumanProfile.TargetActor.Get()) : TEXT("None"),
			*TargetEmbodimentProfile.DefaultEyeLocalOffset.ToCompactString(),
			TargetEmbodimentProfile.HeadBoneFromEyeOffsetCm,
			*TargetEmbodimentProfile.DefaultChestLocalOffset.ToCompactString(),
			*TargetEmbodimentProfile.DefaultPelvisLocalOffset.ToCompactString(),
			TargetEmbodimentProfile.UpperBodyFollowAlpha);
	}

	bHasTargetEmbodimentProfile = TargetEmbodimentProfile.IsValid();
}

static FVector QuestBasisAxis(const FQuat& Basis, const uint8 AxisIndex)
{
	if (AxisIndex == 0)
	{
		return Basis.RotateVector(FVector::ForwardVector).GetSafeNormal();
	}
	if (AxisIndex == 1)
	{
		return Basis.RotateVector(FVector::RightVector).GetSafeNormal();
	}
	return Basis.RotateVector(FVector::UpVector).GetSafeNormal();
}

static FVector QuestSignedBasisAxis(const FQuat& Basis, const uint8 AxisIndex, const float AxisSign)
{
	return (QuestBasisAxis(Basis, AxisIndex) * (AxisSign < 0.0f ? -1.0f : 1.0f)).GetSafeNormal();
}

static uint8 EncodeQuestSignedAxisIndex(const uint8 AxisIndex, const float AxisSign)
{
	return static_cast<uint8>(AxisIndex + (AxisSign < 0.0f ? 4 : 1));
}

static bool SelectQuestAnatomicalRollAxis(
	const FQuat& QuestWristJointComp,
	const FQuat& QuestHandBasisComp,
	const FVector& ForearmAxisComp,
	uint8& OutAxisIndex,
	float& OutAxisSign,
	float& OutPalmDot,
	float& OutForearmDot)
{
	const FVector PalmForwardComp = QuestHandBasisComp.RotateVector(FVector::ForwardVector).GetSafeNormal();
	const FVector ForearmAxisN = ForearmAxisComp.GetSafeNormal();
	if (QuestWristJointComp.IsIdentity() || PalmForwardComp.IsNearlyZero() || ForearmAxisN.IsNearlyZero())
	{
		return false;
	}

	uint8 BestAxisIndex = 0;
	float BestDot = 0.0f;
	float BestAbsDot = -1.0f;
	for (uint8 AxisIndex = 0; AxisIndex < 3; ++AxisIndex)
	{
		const FVector Axis = QuestBasisAxis(QuestWristJointComp, AxisIndex);
		if (Axis.IsNearlyZero())
		{
			continue;
		}

		const float Dot = FVector::DotProduct(Axis, ForearmAxisN);
		const float AbsDot = FMath::Abs(Dot);
		if (AbsDot > BestAbsDot)
		{
			BestAxisIndex = AxisIndex;
			BestDot = Dot;
			BestAbsDot = AbsDot;
		}
	}

	if (BestAbsDot < 0.50f)
	{
		return false;
	}

	OutAxisIndex = BestAxisIndex;
	OutAxisSign = BestDot < 0.0f ? -1.0f : 1.0f;
	const FVector SignedAxis = QuestSignedBasisAxis(QuestWristJointComp, OutAxisIndex, OutAxisSign);
	OutPalmDot = FVector::DotProduct(SignedAxis, PalmForwardComp);
	OutForearmDot = FVector::DotProduct(SignedAxis, ForearmAxisN);
	return true;
}

static FVector ProjectAxisToPlane(const FVector& Axis, const FVector& PlaneNormal)
{
	const FVector AxisN = Axis.GetSafeNormal();
	const FVector NormalN = PlaneNormal.GetSafeNormal();
	if (AxisN.IsNearlyZero() || NormalN.IsNearlyZero())
	{
		return FVector::ZeroVector;
	}

	return (AxisN - FVector::DotProduct(AxisN, NormalN) * NormalN).GetSafeNormal();
}

static float SignedAngleDegAroundAxis(const FVector& From, const FVector& To, const FVector& Axis)
{
	const FVector FromN = From.GetSafeNormal();
	const FVector ToN = To.GetSafeNormal();
	const FVector AxisN = Axis.GetSafeNormal();
	if (FromN.IsNearlyZero() || ToN.IsNearlyZero() || AxisN.IsNearlyZero())
	{
		return 0.0f;
	}

	const float SinAngle = FVector::DotProduct(AxisN, FVector::CrossProduct(FromN, ToN));
	const float CosAngle = FMath::Clamp(FVector::DotProduct(FromN, ToN), -1.0f, 1.0f);
	return FRotator::NormalizeAxis(FMath::RadiansToDegrees(FMath::Atan2(SinAngle, CosAngle)));
}

static bool MeasureQuestSemanticRollOffsetDeg(
	const FQuat& QuestHandBasisComp,
	const FQuat& MediaPipeHandBasisComp,
	const FVector& ForearmAxisComp,
	const uint8 AxisIndex,
	float& OutOffsetDeg,
	float& OutAxisScore)
{
	const FVector ForearmAxisN = ForearmAxisComp.GetSafeNormal();
	if (QuestHandBasisComp.IsIdentity() || MediaPipeHandBasisComp.IsIdentity() || ForearmAxisN.IsNearlyZero())
	{
		return false;
	}

	const FVector QuestAxis = QuestBasisAxis(QuestHandBasisComp, AxisIndex);
	const FVector MediaAxis = QuestBasisAxis(MediaPipeHandBasisComp, AxisIndex);
	if (QuestAxis.IsNearlyZero() || MediaAxis.IsNearlyZero())
	{
		return false;
	}

	const float QuestForearmDot = FVector::DotProduct(QuestAxis, ForearmAxisN);
	const float MediaForearmDot = FVector::DotProduct(MediaAxis, ForearmAxisN);
	const float QuestProjectionScore = FMath::Clamp(1.0f - (QuestForearmDot * QuestForearmDot), 0.0f, 1.0f);
	const float MediaProjectionScore = FMath::Clamp(1.0f - (MediaForearmDot * MediaForearmDot), 0.0f, 1.0f);
	const float AxisPreference = AxisIndex == 0 ? 0.75f : 1.0f;
	OutAxisScore = FMath::Min(QuestProjectionScore, MediaProjectionScore) * AxisPreference;
	if (OutAxisScore < 0.12f)
	{
		return false;
	}

	const FVector QuestProjected = ProjectAxisToPlane(QuestAxis, ForearmAxisN);
	const FVector MediaProjected = ProjectAxisToPlane(MediaAxis, ForearmAxisN);
	if (QuestProjected.IsNearlyZero() || MediaProjected.IsNearlyZero())
	{
		return false;
	}

	OutOffsetDeg = SignedAngleDegAroundAxis(MediaProjected, QuestProjected, ForearmAxisN);
	return true;
}

static bool SelectQuestSemanticRollAxis(
	const FQuat& QuestHandBasisComp,
	const FQuat& MediaPipeHandBasisComp,
	const FVector& ForearmAxisComp,
	uint8& OutAxisIndex,
	float& OutOffsetDeg,
	float& OutAxisScore)
{
	bool bFound = false;
	uint8 BestAxisIndex = 0;
	float BestOffsetDeg = 0.0f;
	float BestScore = -1.0f;
	for (uint8 AxisIndex = 0; AxisIndex < 3; ++AxisIndex)
	{
		float CandidateOffsetDeg = 0.0f;
		float CandidateScore = 0.0f;
		if (!MeasureQuestSemanticRollOffsetDeg(
			QuestHandBasisComp,
			MediaPipeHandBasisComp,
			ForearmAxisComp,
			AxisIndex,
			CandidateOffsetDeg,
			CandidateScore))
		{
			continue;
		}

		if (CandidateScore > BestScore)
		{
			bFound = true;
			BestAxisIndex = AxisIndex;
			BestOffsetDeg = CandidateOffsetDeg;
			BestScore = CandidateScore;
		}
	}

	if (!bFound)
	{
		return false;
	}

	OutAxisIndex = BestAxisIndex;
	OutOffsetDeg = BestOffsetDeg;
	OutAxisScore = BestScore;
	return true;
}

static bool MeasureQuestSwingCorrectedRollDeg(
	const FQuat& QuestHandBasisComp,
	const FVector& CalibrationForwardComp,
	const FVector& CalibrationUpComp,
	float& OutRollDeg,
	float& OutAxisScore)
{
	const FVector CurrentForward = QuestBasisAxis(QuestHandBasisComp, 0);
	const FVector CurrentUp = QuestBasisAxis(QuestHandBasisComp, 2);
	const FVector CalibrationForward = CalibrationForwardComp.GetSafeNormal();
	const FVector CalibrationUp = CalibrationUpComp.GetSafeNormal();
	if (QuestHandBasisComp.IsIdentity() ||
		CurrentForward.IsNearlyZero() ||
		CurrentUp.IsNearlyZero() ||
		CalibrationForward.IsNearlyZero() ||
		CalibrationUp.IsNearlyZero())
	{
		return false;
	}

	const float ForwardDot = FMath::Clamp(FVector::DotProduct(CalibrationForward, CurrentForward), -1.0f, 1.0f);
	if (ForwardDot < -0.95f)
	{
		return false;
	}

	const FQuat SwingFromCalibration = FQuat::FindBetweenNormals(CalibrationForward, CurrentForward).GetNormalized();
	const FVector ReferenceUp = SwingFromCalibration.RotateVector(CalibrationUp).GetSafeNormal();
	const FVector ReferenceUpProjected = ProjectAxisToPlane(ReferenceUp, CurrentForward);
	const FVector CurrentUpProjected = ProjectAxisToPlane(CurrentUp, CurrentForward);
	if (ReferenceUpProjected.IsNearlyZero() || CurrentUpProjected.IsNearlyZero())
	{
		return false;
	}

	OutRollDeg = SignedAngleDegAroundAxis(ReferenceUpProjected, CurrentUpProjected, CurrentForward);
	OutAxisScore = FMath::Clamp((ForwardDot + 1.0f) * 0.5f, 0.0f, 1.0f);
	return true;
}

static bool MeasureProjectedBasisRollDeltaDeg(
	const FQuat& CalibrationBasis,
	const FQuat& CurrentBasis,
	const FVector& RollAxis,
	const uint8 BasisAxisIndex,
	float& OutRollDeg,
	float& OutAxisScore)
{
	const FVector AxisN = RollAxis.GetSafeNormal();
	if (CalibrationBasis.IsIdentity() || CurrentBasis.IsIdentity() || AxisN.IsNearlyZero())
	{
		return false;
	}

	const FVector CalibrationAxis = QuestBasisAxis(CalibrationBasis, BasisAxisIndex);
	const FVector CurrentAxis = QuestBasisAxis(CurrentBasis, BasisAxisIndex);
	if (CalibrationAxis.IsNearlyZero() || CurrentAxis.IsNearlyZero())
	{
		return false;
	}

	const float CalibrationDot = FVector::DotProduct(CalibrationAxis, AxisN);
	const float CurrentDot = FVector::DotProduct(CurrentAxis, AxisN);
	const float CalibrationProjectionScore = FMath::Clamp(1.0f - (CalibrationDot * CalibrationDot), 0.0f, 1.0f);
	const float CurrentProjectionScore = FMath::Clamp(1.0f - (CurrentDot * CurrentDot), 0.0f, 1.0f);
	OutAxisScore = FMath::Min(CalibrationProjectionScore, CurrentProjectionScore);

	const FVector CalibrationProjected = ProjectAxisToPlane(CalibrationAxis, AxisN);
	const FVector CurrentProjected = ProjectAxisToPlane(CurrentAxis, AxisN);
	if (CalibrationProjected.IsNearlyZero() || CurrentProjected.IsNearlyZero())
	{
		return false;
	}

	OutRollDeg = SignedAngleDegAroundAxis(CalibrationProjected, CurrentProjected, AxisN);
	return true;
}

static FVector LockVectorToHemisphere(const FVector& Vector, const FVector& Reference)
{
	const FVector Normalized = Vector.GetSafeNormal();
	const FVector Ref = Reference.GetSafeNormal();
	if (Normalized.IsNearlyZero() || Ref.IsNearlyZero())
	{
		return Normalized;
	}

	return FVector::DotProduct(Normalized, Ref) < 0.0f ? -Normalized : Normalized;
}

static FVector ProjectPoleReferenceToPlane(const FVector& PoleReference, const FVector& Forward, const FVector& FallbackA, const FVector& FallbackB)
{
	const FVector ForwardN = Forward.GetSafeNormal();
	if (ForwardN.IsNearlyZero())
	{
		return FVector::ZeroVector;
	}

	auto Project = [&](const FVector& Candidate) -> FVector
	{
		return (Candidate - FVector::DotProduct(Candidate, ForwardN) * ForwardN).GetSafeNormal();
	};

	FVector Projected = Project(PoleReference);
	if (Projected.IsNearlyZero())
	{
		Projected = Project(FallbackA);
	}
	if (Projected.IsNearlyZero())
	{
		Projected = Project(FallbackB);
	}
	return Projected;
}

static float RemapClamped(float Value, float InMin, float InMax)
{
	if (FMath::IsNearlyEqual(InMin, InMax))
	{
		return Value >= InMax ? 1.0f : 0.0f;
	}

	return FMath::Clamp((Value - InMin) / (InMax - InMin), 0.0f, 1.0f);
}

static float RemapPositiveUnbounded(float Value, float InMin, float InFull)
{
	if (FMath::IsNearlyEqual(InMin, InFull))
	{
		return Value >= InFull ? 1.0f : 0.0f;
	}

	return FMath::Max(0.0f, (Value - InMin) / (InFull - InMin));
}

static float RemapSignedUnbounded(float Value, float DeadZone, float Full)
{
	const float AbsValue = FMath::Abs(Value);
	if (FMath::IsNearlyEqual(DeadZone, Full))
	{
		return AbsValue >= Full ? FMath::Sign(Value) : 0.0f;
	}

	const float Magnitude = FMath::Max(0.0f, (AbsValue - DeadZone) / (Full - DeadZone));
	return FMath::Sign(Value) * Magnitude;
}

static FVector BuildArmSurfaceUpHint(const FVector& BodyUp, const FVector& BodyForward, const FVector& SegmentDir)
{
	const FVector UpN = BodyUp.GetSafeNormal();
	const FVector ForwardN = BodyForward.GetSafeNormal();
	const FVector SegmentDirN = SegmentDir.GetSafeNormal();
	if (UpN.IsNearlyZero())
	{
		return FVector::UpVector;
	}

	if (ForwardN.IsNearlyZero() || SegmentDirN.IsNearlyZero())
	{
		return UpN;
	}

	const float DownAlpha = RemapClamped(-FVector::DotProduct(SegmentDirN, UpN), 0.05f, 0.75f);
	const float BackBias = 0.45f * DownAlpha;
	const FVector Hint = (UpN - BackBias * ForwardN).GetSafeNormal();
	return Hint.IsNearlyZero() ? UpN : Hint;
}

float FAnimNode_MediaPipePoseDriven::HalfLifeToAlpha(float HalfLifeSeconds, float DeltaSeconds)
{
	if (HalfLifeSeconds <= 0.0f || DeltaSeconds <= 0.0f)
	{
		return 1.0f;
	}
	const float A = 1.0f - FMath::Pow(0.5f, DeltaSeconds / HalfLifeSeconds);
	return A;
}

float FAnimNode_MediaPipePoseDriven::QuatAngularDistanceDegrees(const FQuat& A, const FQuat& B)
{
	const FQuat NA = A.GetNormalized();
	const FQuat NB = B.GetNormalized();
	const float Dot = FMath::Abs(
		NA.X * NB.X +
		NA.Y * NB.Y +
		NA.Z * NB.Z +
		NA.W * NB.W);
	const float ClampedDot = FMath::Clamp(Dot, 0.0f, 1.0f);
	return FMath::RadiansToDegrees(2.0f * FMath::Acos(ClampedDot));
}

void FAnimNode_MediaPipePoseDriven::UpdateSmoothedRotation(bool& bInOutHasValue, FQuat& InOutValueCS, const FQuat& TargetCS, float Alpha, float MaxStepDegrees)
{
	FQuat Target = TargetCS.GetNormalized();
	if (!bInOutHasValue)
	{
		InOutValueCS = Target;
		bInOutHasValue = true;
		return;
	}

	const float Dot =
		InOutValueCS.X * Target.X +
		InOutValueCS.Y * Target.Y +
		InOutValueCS.Z * Target.Z +
		InOutValueCS.W * Target.W;
	if (Dot < 0.0f)
	{
		Target.X = -Target.X;
		Target.Y = -Target.Y;
		Target.Z = -Target.Z;
		Target.W = -Target.W;
	}

	FQuat Next = FQuat::Slerp(InOutValueCS, Target, FMath::Clamp(Alpha, 0.0f, 1.0f)).GetNormalized();
	if (MaxStepDegrees > 0.0f)
	{
		const float StepDeg = FMath::RadiansToDegrees(InOutValueCS.AngularDistance(Next));
		if (StepDeg > MaxStepDegrees && StepDeg > KINDA_SMALL_NUMBER)
		{
			Next = FQuat::Slerp(InOutValueCS, Next, MaxStepDegrees / StepDeg).GetNormalized();
		}
	}

	InOutValueCS = Next;
}

void FAnimNode_MediaPipePoseDriven::ResetRotationSmoothing()
{
	BodyState.ResetRotationSmoothing();
	LeftArmState.ResetSmoothing();
	RightArmState.ResetSmoothing();
	LeftLegState.ResetRotationSmoothing();
	RightLegState.ResetRotationSmoothing();
	QuestWristState.Reset();
	LeftQuestHandState.Reset();
	RightQuestHandState.Reset();
	DiagnosticsState.Reset();
}

#include "MediaPipePoseDrivenAnimInstance_BodyState.inl"

bool FAnimNode_MediaPipePoseDriven::TryGetLmWorld(int32 LmIdx, FVector& OutWorld) const
{
	if (LmIdx < 0 || LmIdx >= MediaPipePoseLandmarkCount)
	{
		return false;
	}

	if (!bHasPoseFrame)
	{
		return false;
	}

	OutWorld = PoseWorld[LmIdx];
	return true;
}

bool FAnimNode_MediaPipePoseDriven::IsMeasured(int32 LmIdx) const
{
	if (LmIdx < 0 || LmIdx >= MediaPipePoseLandmarkCount)
	{
		return false;
	}

	if (!bHasPoseFrame)
	{
		return false;
	}

	return PoseFrame.World.IsValidIndex(LmIdx) && PoseFrame.Normalized.IsValidIndex(LmIdx);
}

float FAnimNode_MediaPipePoseDriven::GetLandmarkReliability(int32 LmIdx) const
{
	if (LmIdx < 0 || LmIdx >= MediaPipePoseLandmarkCount || !bHasPoseFrame)
	{
		return 0.0f;
	}

	if (!PoseFrame.World.IsValidIndex(LmIdx) || !PoseFrame.Normalized.IsValidIndex(LmIdx))
	{
		return 0.0f;
	}

	const FMediaPipePoseLandmark& WorldLm = PoseFrame.World.Points[LmIdx];
	const FMediaPipePoseLandmark& NormalizedLm = PoseFrame.Normalized.Points[LmIdx];
	const float ExplicitReliability = FMath::Max(WorldLm.Reliability, NormalizedLm.Reliability);
	if (ExplicitReliability > 0.0f || PoseFrame.bSourceConditioned)
	{
		return FMath::Clamp(ExplicitReliability, 0.0f, 1.0f);
	}

	return FMath::Clamp(
		FMath::Max(WorldLm.Visibility * WorldLm.Presence, NormalizedLm.Visibility * NormalizedLm.Presence),
		0.0f,
		1.0f);
}

#include "MediaPipePoseDrivenAnimInstance_TorsoBasis.inl"

#include "MediaPipePoseDrivenAnimInstance_ReferenceCache.inl"

void FAnimNode_MediaPipePoseDriven::ApplyRotationCS(FCSPose<FCompactPose>& CSPose, const FBoneReference& Bone, const FQuat& TargetRotCS) const
{
	if (!Bone.IsValidToEvaluate())
	{
		return;
	}
	const FCompactPoseBoneIndex BoneIdx = Bone.CachedCompactPoseIndex;
	FTransform BoneCS = CSPose.GetComponentSpaceTransform(BoneIdx);
	BoneCS.SetRotation(TargetRotCS);
	const FBoneTransform BoneTransform(BoneIdx, BoneCS);
	CSPose.SafeSetCSBoneTransforms(MakeArrayView(&BoneTransform, 1));
}

void FAnimNode_MediaPipePoseDriven::ApplyTranslationDeltaCS(FCSPose<FCompactPose>& CSPose, const FBoneReference& Bone, const FVector& DeltaComp) const
{
	if (DeltaComp.IsNearlyZero() || !Bone.IsValidToEvaluate())
	{
		return;
	}

	const FCompactPoseBoneIndex BoneIdx = Bone.CachedCompactPoseIndex;
	// Only shift bones that are already in component space. If the bone is still in local space, it will be
	// converted using its (unchanged) local transform relative to the updated parents, so shifting here would double-apply.
	const auto& CSFlags = CSPose.GetComponentSpaceFlags();
	const int32 BoneIdxInt = BoneIdx.GetInt();
	if (!CSFlags.IsValidIndex(BoneIdxInt) || CSFlags[BoneIdx] == 0)
	{
		return;
	}

	FTransform BoneCS = CSPose.GetComponentSpaceTransform(BoneIdx);
	BoneCS.SetTranslation(BoneCS.GetTranslation() + DeltaComp);
	const FBoneTransform BoneTransform(BoneIdx, BoneCS);
	CSPose.SafeSetCSBoneTransforms(MakeArrayView(&BoneTransform, 1));
}

bool FAnimNode_MediaPipePoseDriven::ShouldUseBodyFusionPoseForEvaluation() const
{
	if (!FMediaPipeBodyFusionRuntimePolicy::IsBodyFusionEnabledAnyThread() ||
		!FMediaPipeBodyFusionRuntimePolicy::IsPoseWriteEnabledAnyThread() ||
		!BodyFusionFrame.Pose.IsUsable())
	{
		return false;
	}

	const FMediaPipeAvatarEmbodimentProfile Profile = bHasTargetEmbodimentProfile
		? TargetEmbodimentProfile
		: FMediaPipeAvatarEmbodimentProfile();
	return FMediaPipeAvatarPoseWriter::CanWritePose(BodyFusionFrame.Pose, Profile);
}

bool FAnimNode_MediaPipePoseDriven::ShouldUseAvatarLockedMetaHumanReplay() const
{
	if (!FMediaPipeTrackingFusionDatasetReplayRuntime::Get().IsActive())
	{
		return false;
	}

	if (TargetMetaHumanProfile.IsValidForPoseDriving())
	{
		return true;
	}

	const FMediaPipeAvatarEmbodimentProfile Profile = bHasTargetEmbodimentProfile
		? TargetEmbodimentProfile
		: FMediaPipeAvatarEmbodimentProfile();
	return Profile.SkeletonFamily == EMediaPipeAvatarSkeletonFamily::MetaHuman;
}

bool FAnimNode_MediaPipePoseDriven::ShouldUseBodyFusionStage1TorsoPelvisHintForEvaluation() const
{
	return false;
}

bool FAnimNode_MediaPipePoseDriven::ShouldUseBodyFusionStage2ShoulderClavicleHintForEvaluation() const
{
	if (!FMediaPipeBodyFusionRuntimePolicy::IsBodyFusionEnabledAnyThread())
	{
		return false;
	}
	if (FMediaPipeBodyFusionRuntimePolicy::IsPoseWriteEnabledAnyThread())
	{
		return false;
	}
	if (!FMediaPipeBodyFusionRuntimePolicy::IsStage2ShoulderClavicleHintEnabledAnyThread())
	{
		return false;
	}

	const FMediaPipeTrackingSourceFrame& SourceFrame = BodyFusionFrame.SourceFrame;
	return BodyFusionFrame.Calibration.IsUsable() &&
		SourceFrame.bHasBodyPose &&
		SourceFrame.BodyPoseStatus.IsFresh();
}

bool FAnimNode_MediaPipePoseDriven::DriveBodyFusionStage1TorsoPelvisHintCS(FCSPose<FCompactPose>& CSPose, float DeltaSeconds)
{
	(void)CSPose;
	(void)DeltaSeconds;
	return false;
}

bool FAnimNode_MediaPipePoseDriven::DriveBodyFusionStage2ShoulderClavicleHintCS(FCSPose<FCompactPose>& CSPose, float DeltaSeconds)
{
	if (!FMediaPipeBodyFusionRuntimePolicy::IsStage2ShoulderClavicleHintEnabledAnyThread())
	{
		return false;
	}

	auto ResetStage2Side = [](
		bool& bHasSmoothedLift,
		float& SmoothedLiftCm,
		bool& bHasNeutralReference,
		float& NeutralShoulderLiftFromPelvisCm,
		float& NeutralShoulderHeadClearanceCm,
		float& NeutralObservationSeconds,
		int32& NeutralObservationFrames)
	{
		bHasSmoothedLift = false;
		SmoothedLiftCm = 0.0f;
		bHasNeutralReference = false;
		NeutralShoulderLiftFromPelvisCm = 0.0f;
		NeutralShoulderHeadClearanceCm = 0.0f;
		NeutralObservationSeconds = 0.0f;
		NeutralObservationFrames = 0;
	};

	auto ResetAllStage2Sides = [&]()
	{
		ResetStage2Side(
			BodyState.bHasSmoothedStage2ClavicleLiftL,
			BodyState.SmoothedStage2ClavicleLiftCmL,
			BodyState.bHasStage2NeutralReferenceL,
			BodyState.Stage2NeutralShoulderLiftFromPelvisCmL,
			BodyState.Stage2NeutralShoulderHeadClearanceCmL,
			BodyState.Stage2NeutralObservationSecondsL,
			BodyState.Stage2NeutralObservationFramesL);
		ResetStage2Side(
			BodyState.bHasSmoothedStage2ClavicleLiftR,
			BodyState.SmoothedStage2ClavicleLiftCmR,
			BodyState.bHasStage2NeutralReferenceR,
			BodyState.Stage2NeutralShoulderLiftFromPelvisCmR,
			BodyState.Stage2NeutralShoulderHeadClearanceCmR,
			BodyState.Stage2NeutralObservationSecondsR,
			BodyState.Stage2NeutralObservationFramesR);
	};

	const FMediaPipeTrackingSourceFrame& SourceFrame = BodyFusionFrame.SourceFrame;
	if (!SourceFrame.bHasBodyPose ||
		!SourceFrame.BodyPoseStatus.IsFresh() ||
		!BodyFusionFrame.Calibration.IsUsable())
	{
		ResetAllStage2Sides();
		return false;
	}

	FMediaPipeStage2ShoulderEvidenceSettings Settings;
	Settings.Blend = FMediaPipeBodyFusionRuntimePolicy::GetStage2ShoulderClavicleHintBlendAnyThread();
	Settings.ResponseScale = FMediaPipeBodyFusionRuntimePolicy::GetStage2ShoulderClavicleResponseScaleAnyThread();
	Settings.MaxLiftCm = FMediaPipeBodyFusionRuntimePolicy::GetStage2ShoulderClavicleMaxLiftCmAnyThread();
	Settings.HalfLifeSeconds = FMediaPipeBodyFusionRuntimePolicy::GetStage2ShoulderClavicleHalfLifeSecondsAnyThread();
	Settings.ContradictionCm = FMediaPipeBodyFusionRuntimePolicy::GetStage2ShoulderContradictionCmAnyThread();
	Settings.QuestArmRaiseFadeStartCm = FMediaPipeBodyFusionRuntimePolicy::GetStage2ShoulderArmRaiseFadeStartCmAnyThread();
	Settings.QuestArmRaiseFadeFullCm = FMediaPipeBodyFusionRuntimePolicy::GetStage2ShoulderArmRaiseFadeFullCmAnyThread();
	Settings.ShrugStartCm = FMediaPipeBodyFusionRuntimePolicy::GetStage2ShoulderShrugStartCmAnyThread();
	Settings.ShrugFullCm = FMediaPipeBodyFusionRuntimePolicy::GetStage2ShoulderShrugFullCmAnyThread();
	if (Settings.MaxLiftCm <= KINDA_SMALL_NUMBER)
	{
		ResetAllStage2Sides();
		return false;
	}

	const FTransform WorldToComponent = TargetCompTransform.Inverse();

	auto ApplySide = [&](const bool bIsLeft) -> bool
	{
		const FBoneReference& ClavicleBone = bIsLeft ? ClavicleL : ClavicleR;
		const bool bHasRefClavicle = bIsLeft ? bHasRefClavL : bHasRefClavR;
		bool& bHasSmoothedLift = bIsLeft ? BodyState.bHasSmoothedStage2ClavicleLiftL : BodyState.bHasSmoothedStage2ClavicleLiftR;
		float& SmoothedLiftCm = bIsLeft ? BodyState.SmoothedStage2ClavicleLiftCmL : BodyState.SmoothedStage2ClavicleLiftCmR;
		bool& bHasNeutralReference = bIsLeft ? BodyState.bHasStage2NeutralReferenceL : BodyState.bHasStage2NeutralReferenceR;
		float& NeutralShoulderLiftFromPelvisCm = bIsLeft ? BodyState.Stage2NeutralShoulderLiftFromPelvisCmL : BodyState.Stage2NeutralShoulderLiftFromPelvisCmR;
		float& NeutralShoulderHeadClearanceCm = bIsLeft ? BodyState.Stage2NeutralShoulderHeadClearanceCmL : BodyState.Stage2NeutralShoulderHeadClearanceCmR;
		float& NeutralObservationSeconds = bIsLeft ? BodyState.Stage2NeutralObservationSecondsL : BodyState.Stage2NeutralObservationSecondsR;
		int32& NeutralObservationFrames = bIsLeft ? BodyState.Stage2NeutralObservationFramesL : BodyState.Stage2NeutralObservationFramesR;
		FMediaPipePoseDrivenShoulderSignalSnapshot& ShoulderSnapshot =
			bIsLeft ? LatestSignalSnapshot.LeftShoulder : LatestSignalSnapshot.RightShoulder;
		auto PublishSignalSnapshot = [&]()
		{
			LatestSignalSnapshot.bValid = true;
			LatestSignalSnapshot.RuntimeStateKey = RuntimeStateKey;
			LatestSignalSnapshot.PoseTimestampUs = static_cast<int64>(SourceFrame.BodyPoseTimestampSeconds * 1000000.0);
			UMediaPipePoseDrivenAnimInstance::PublishLatestSignalSnapshotForRuntimeKey(RuntimeStateKey, LatestSignalSnapshot);
		};

		if (!ClavicleBone.IsValidToEvaluate() || !bHasRefClavicle)
		{
			ResetStage2Side(
				bHasSmoothedLift,
				SmoothedLiftCm,
				bHasNeutralReference,
				NeutralShoulderLiftFromPelvisCm,
				NeutralShoulderHeadClearanceCm,
				NeutralObservationSeconds,
				NeutralObservationFrames);
			return false;
		}

		FMediaPipeStage2ShoulderEvidenceSideState SideState;
		SideState.bHasSmoothedLift = bHasSmoothedLift;
		SideState.SmoothedLiftCm = SmoothedLiftCm;
		SideState.bHasNeutralReference = bHasNeutralReference;
		SideState.NeutralShoulderLiftFromPelvisCm = NeutralShoulderLiftFromPelvisCm;
		SideState.NeutralShoulderHeadClearanceCm = NeutralShoulderHeadClearanceCm;
		SideState.NeutralObservationSeconds = NeutralObservationSeconds;
		SideState.NeutralObservationFrames = NeutralObservationFrames;

		FMediaPipeStage2ShoulderEvidenceResult Evidence;
		if (!FMediaPipeStage2ShoulderEvidence::BuildSideEvidence(
				SourceFrame,
				BodyFusionFrame.Calibration,
				bIsLeft,
				WorldToComponent,
				Settings,
				DeltaSeconds,
				SideState,
				Evidence))
		{
			bHasSmoothedLift = SideState.bHasSmoothedLift;
			SmoothedLiftCm = SideState.SmoothedLiftCm;
			bHasNeutralReference = SideState.bHasNeutralReference;
			NeutralShoulderLiftFromPelvisCm = SideState.NeutralShoulderLiftFromPelvisCm;
			NeutralShoulderHeadClearanceCm = SideState.NeutralShoulderHeadClearanceCm;
			NeutralObservationSeconds = SideState.NeutralObservationSeconds;
			NeutralObservationFrames = SideState.NeutralObservationFrames;
			return false;
		}

		bHasSmoothedLift = SideState.bHasSmoothedLift;
		SmoothedLiftCm = SideState.SmoothedLiftCm;
		bHasNeutralReference = SideState.bHasNeutralReference;
		NeutralShoulderLiftFromPelvisCm = SideState.NeutralShoulderLiftFromPelvisCm;
		NeutralShoulderHeadClearanceCm = SideState.NeutralShoulderHeadClearanceCm;
		NeutralObservationSeconds = SideState.NeutralObservationSeconds;
		NeutralObservationFrames = SideState.NeutralObservationFrames;

		const FCompactPoseBoneIndex ClavicleIdx = ClavicleBone.CachedCompactPoseIndex;
		FTransform ClavicleCS = CSPose.GetComponentSpaceTransform(ClavicleIdx);
		const float PreSolveClavicleLiftFromPelvisCm = ClavicleCS.GetTranslation().Z - RefPelvisTranslationComp.Z;
		const float TargetClavicleLiftFromPelvisCm = PreSolveClavicleLiftFromPelvisCm + SmoothedLiftCm;
		ShoulderSnapshot.bValid = true;
		ShoulderSnapshot.bStage2ShoulderClavicleHintValid = true;
		ShoulderSnapshot.bStage2ShoulderClavicleSuppressedByContradiction = Evidence.bSuppressedByContradiction;
		ShoulderSnapshot.bStage2ShoulderClavicleSuppressedByArmOwnership = Evidence.bSuppressedByArmOwnership;
		ShoulderSnapshot.bStage2ShoulderClavicleHadContradictionSource = Evidence.bHadContradictionSource;
		ShoulderSnapshot.bStage2ShoulderClavicleHadQuestArmRaiseSource = Evidence.bHadQuestArmRaiseSource;
		ShoulderSnapshot.bStage2NeutralReferenceReady = Evidence.bNeutralReferenceReady;
		ShoulderSnapshot.bStage2NeutralSampleAccepted = Evidence.bNeutralSampleAccepted;
		ShoulderSnapshot.bStage2ClampHit = Evidence.bClampHit;
		ShoulderSnapshot.Stage2SignalSourceMode = static_cast<float>(static_cast<uint8>(Evidence.SourceMode));
		ShoulderSnapshot.Stage2SignalSourceReliability = Evidence.SourceReliability;
		ShoulderSnapshot.Stage2NeutralObservationSeconds = Evidence.NeutralObservationSeconds;
		ShoulderSnapshot.Stage2CandidateShoulderLiftFromPelvisCm = Evidence.CandidateShoulderLiftFromPelvisCm;
		ShoulderSnapshot.Stage2ReferenceShoulderLiftFromPelvisCm = Evidence.ReferenceShoulderLiftFromPelvisCm;
		ShoulderSnapshot.Stage2RawLiftDeltaCm = Evidence.RawLiftDeltaCm;
		ShoulderSnapshot.Stage2ShoulderHeadClearanceCm = Evidence.ShoulderHeadClearanceCm;
		ShoulderSnapshot.Stage2ShoulderHeadClearanceReferenceCm = Evidence.ShoulderHeadClearanceReferenceCm;
		ShoulderSnapshot.Stage2ShoulderHeadClearanceShrugCm = Evidence.ShoulderHeadClearanceShrugCm;
		ShoulderSnapshot.Stage2ClearancePrimaryEvidenceCm = Evidence.ClearancePrimaryEvidenceCm;
		ShoulderSnapshot.Stage2RawLiftConfirmationWeight = Evidence.RawLiftConfirmationWeight;
		ShoulderSnapshot.Stage2SignedLiftEvidenceCm = Evidence.SignedLiftEvidenceCm;
		ShoulderSnapshot.Stage2SignedTargetLiftCm = Evidence.SignedTargetLiftCm;
		ShoulderSnapshot.Stage2AppliedResponseScale = Evidence.AppliedResponseScale;
		ShoulderSnapshot.Stage2PositiveLiftEvidenceCm = Evidence.PositiveLiftEvidenceCm;
		ShoulderSnapshot.Stage2UnfadedPositiveTargetLiftCm = Evidence.UnfadedPositiveTargetLiftCm;
		ShoulderSnapshot.Stage2QuestWristLiftFromPelvisCm = Evidence.QuestWristLiftFromPelvisCm;
		ShoulderSnapshot.Stage2QuestElbowLiftFromPelvisCm = Evidence.QuestElbowLiftFromPelvisCm;
		ShoulderSnapshot.Stage2QuestArmRaiseOwnershipFade = Evidence.QuestArmRaiseOwnershipFade;
		ShoulderSnapshot.Stage2QuestArmRaiseLiftWeight = Evidence.QuestArmRaiseLiftWeight;
		ShoulderSnapshot.Stage2PositiveTargetLiftCm = Evidence.PositiveTargetLiftCm;
		ShoulderSnapshot.Stage2ContradictionDeltaCm = Evidence.ContradictionDeltaCm;
		ShoulderSnapshot.Stage2SmoothedLiftCm = SmoothedLiftCm;
		ShoulderSnapshot.Stage2PreSolveClavicleLiftFromPelvisCm = PreSolveClavicleLiftFromPelvisCm;
		ShoulderSnapshot.Stage2TargetClavicleLiftFromPelvisCm = TargetClavicleLiftFromPelvisCm;
		ShoulderSnapshot.Stage2AppliedClavicleLiftCm = 0.0f;
		ShoulderSnapshot.Stage2AppliedClavicleHelperLiftCm = 0.0f;
		ShoulderSnapshot.Stage2DirectUpperArmLiftCm = 0.0f;
		ShoulderSnapshot.Stage2DirectLowerArmLiftCm = 0.0f;
		ShoulderSnapshot.Stage2DirectHandLiftCm = 0.0f;
		PublishSignalSnapshot();

		return false;
	};

	bool bWroteAny = false;
	bWroteAny |= ApplySide(true);
	bWroteAny |= ApplySide(false);
	return bWroteAny;
}

bool FAnimNode_MediaPipePoseDriven::DriveBodyFusionPoseCS(FCSPose<FCompactPose>& CSPose, float DeltaSeconds)
{
	if (!ShouldUseBodyFusionPoseForEvaluation())
	{
		return false;
	}

	const bool bAvatarLockedMetaHumanReplay = ShouldUseAvatarLockedMetaHumanReplay();
	if (bAvatarLockedMetaHumanReplay)
	{
		// Replay output must be driven by its recorded MediaPipe body landmarks.
		// BodyFusion may still produce diagnostics/freshness/calibration state, but
		// it must not preload pelvis smoothing or write visible lower-body/root pose.
		return false;
	}

	const FTransform ComponentToWorld = TargetCompTransform;
	const FTransform WorldToComponent = ComponentToWorld.Inverse();
	FMediaPipeAvatarEmbodimentProfile ForwardProfile = bHasTargetEmbodimentProfile
		? TargetEmbodimentProfile
		: FMediaPipeAvatarEmbodimentProfile();
	ConfigureBodyFusionProfileForCurrentTarget(ForwardProfile);

	// BodyFusion owns the fused torso/lower-body writes once enabled; the legacy
	// MediaPipeDrive* CVars only gate the raw landmark path.
	if (Pelvis.IsValidToEvaluate())
	{
		auto ApplyAvatarLockedUpperBodyTranslationDelta = [&](const FVector& DeltaComp)
		{
			if (DeltaComp.IsNearlyZero())
			{
				return;
			}

			ApplyTranslationDeltaCS(CSPose, Spine01, DeltaComp);
			ApplyTranslationDeltaCS(CSPose, Spine02, DeltaComp);
			ApplyTranslationDeltaCS(CSPose, Spine03, DeltaComp);
			ApplyTranslationDeltaCS(CSPose, Spine04, DeltaComp);
			ApplyTranslationDeltaCS(CSPose, Spine05, DeltaComp);
			ApplyTranslationDeltaCS(CSPose, Neck, DeltaComp);
			ApplyTranslationDeltaCS(CSPose, Neck02, DeltaComp);
			ApplyTranslationDeltaCS(CSPose, Head, DeltaComp);

			ApplyTranslationDeltaCS(CSPose, ClavicleL, DeltaComp);
			ApplyTranslationDeltaCS(CSPose, UpperArmL, DeltaComp);
			ApplyTranslationDeltaCS(CSPose, UpperArmTwist01L, DeltaComp);
			ApplyTranslationDeltaCS(CSPose, UpperArmTwist02L, DeltaComp);
			ApplyTranslationDeltaCS(CSPose, LowerArmL, DeltaComp);
			ApplyTranslationDeltaCS(CSPose, LowerArmTwist01L, DeltaComp);
			ApplyTranslationDeltaCS(CSPose, LowerArmTwist02L, DeltaComp);
			ApplyTranslationDeltaCS(CSPose, HandL, DeltaComp);
			ApplyTranslationDeltaCS(CSPose, ClavicleR, DeltaComp);
			ApplyTranslationDeltaCS(CSPose, UpperArmR, DeltaComp);
			ApplyTranslationDeltaCS(CSPose, UpperArmTwist01R, DeltaComp);
			ApplyTranslationDeltaCS(CSPose, UpperArmTwist02R, DeltaComp);
			ApplyTranslationDeltaCS(CSPose, LowerArmR, DeltaComp);
			ApplyTranslationDeltaCS(CSPose, LowerArmTwist01R, DeltaComp);
			ApplyTranslationDeltaCS(CSPose, LowerArmTwist02R, DeltaComp);
			ApplyTranslationDeltaCS(CSPose, HandR, DeltaComp);

			for (FBoneReference& HelperBone : MetaHumanClavicleHelpersL)
			{
				ApplyTranslationDeltaCS(CSPose, HelperBone, DeltaComp);
			}
			for (FBoneReference& HelperBone : MetaHumanClavicleHelpersR)
			{
				ApplyTranslationDeltaCS(CSPose, HelperBone, DeltaComp);
			}
			for (FBoneReference& HelperBone : MetaHumanUpperArmHelpersL)
			{
				ApplyTranslationDeltaCS(CSPose, HelperBone, DeltaComp);
			}
			for (FBoneReference& HelperBone : MetaHumanUpperArmHelpersR)
			{
				ApplyTranslationDeltaCS(CSPose, HelperBone, DeltaComp);
			}
			for (FBoneReference& HelperBone : MetaHumanLowerArmHelpersL)
			{
				ApplyTranslationDeltaCS(CSPose, HelperBone, DeltaComp);
			}
			for (FBoneReference& HelperBone : MetaHumanLowerArmHelpersR)
			{
				ApplyTranslationDeltaCS(CSPose, HelperBone, DeltaComp);
			}
		};

		FVector TargetPelvisOffsetComp = FVector::ZeroVector;
		bool bHasTargetPelvisOffset = false;
		if (BodyFusionFrame.Pose.Pelvis.bValid &&
			(BodyFusionFrame.Pose.Pelvis.Owner == EMediaPipeBodyFusionOwner::MediaPipe ||
			 BodyFusionFrame.Pose.Pelvis.Owner == EMediaPipeBodyFusionOwner::Fused))
		{
			const FVector TargetPelvisComp = WorldToComponent.TransformPosition(BodyFusionFrame.Pose.Pelvis.LocationWorld);
			TargetPelvisOffsetComp = TargetPelvisComp - RefPelvisTranslationComp;
			bHasTargetPelvisOffset = !TargetPelvisOffsetComp.ContainsNaN();
		}

		if (bAvatarLockedMetaHumanReplay && BodyFusionFrame.Pose.Head.bValid && !RefHeadPosComp.IsNearlyZero())
		{
			const FVector TargetHeadComp = WorldToComponent.TransformPosition(BodyFusionFrame.Pose.Head.LocationWorld);
			const FVector HeadOffsetComp = TargetHeadComp - RefHeadPosComp;
			FVector CompUp = WorldToComponent.TransformVectorNoScale(FVector::UpVector).GetSafeNormal();
			if (CompUp.IsNearlyZero())
			{
				CompUp = FVector::UpVector;
			}
			const float HeadVerticalOffsetCm = FVector::DotProduct(HeadOffsetComp, CompUp);
			const float PelvisVerticalOffsetCm = FVector::DotProduct(TargetPelvisOffsetComp, CompUp);
			if (FMath::IsFinite(HeadVerticalOffsetCm) &&
				FMath::IsFinite(PelvisVerticalOffsetCm) &&
				HeadVerticalOffsetCm < PelvisVerticalOffsetCm)
			{
				TargetPelvisOffsetComp += CompUp * (HeadVerticalOffsetCm - PelvisVerticalOffsetCm);
				bHasTargetPelvisOffset = true;
			}
		}

		const float PelvisAlpha = HalfLifeToAlpha(PelvisTranslationHalfLifeSeconds, DeltaSeconds);
		if (!BodyState.bHasSmoothedPelvisOffset)
		{
			BodyState.SmoothedPelvisOffsetComp = bHasTargetPelvisOffset ? TargetPelvisOffsetComp : FVector::ZeroVector;
			BodyState.bHasSmoothedPelvisOffset = true;
		}
		else
		{
			BodyState.SmoothedPelvisOffsetComp = FMath::Lerp(
				BodyState.SmoothedPelvisOffsetComp,
				bHasTargetPelvisOffset ? TargetPelvisOffsetComp : FVector::ZeroVector,
				PelvisAlpha);
		}

		const FCompactPoseBoneIndex PelvisIdx = Pelvis.CachedCompactPoseIndex;
		FTransform PelvisCS = CSPose.GetComponentSpaceTransform(PelvisIdx);
		const FVector PreviousPelvisComp = PelvisCS.GetTranslation();
		const FVector TargetPelvisComp = RefPelvisTranslationComp + BodyState.SmoothedPelvisOffsetComp;
		PelvisCS.SetTranslation(TargetPelvisComp);
		const FBoneTransform BoneTransform(PelvisIdx, PelvisCS);
		CSPose.SafeSetCSBoneTransforms(MakeArrayView(&BoneTransform, 1));

		if (bAvatarLockedMetaHumanReplay)
		{
			ApplyAvatarLockedUpperBodyTranslationDelta(TargetPelvisComp - PreviousPelvisComp);
		}
	}

	if (NumSpineBones <= 0)
	{
		return true;
	}

	if (!BodyFusionFrame.Pose.Pelvis.bValid || !BodyFusionFrame.Pose.Chest.bValid || !BodyFusionFrame.Pose.Head.bValid)
	{
		return true;
	}

	FVector ResolvedPelvisComp = WorldToComponent.TransformPosition(BodyFusionFrame.Pose.Pelvis.LocationWorld);
	bool bHasResolvedPelvisComp = false;
	if (Pelvis.IsValidToEvaluate())
	{
		ResolvedPelvisComp = CSPose.GetComponentSpaceTransform(Pelvis.CachedCompactPoseIndex).GetTranslation();
		bHasResolvedPelvisComp = true;
	}

	FMediaPipeBodyFusionPoseWriteContextInput WriteContextInput;
	WriteContextInput.Pose = &BodyFusionFrame.Pose;
	WriteContextInput.TargetComponentToWorld = TargetCompTransform;
	WriteContextInput.Profile = ForwardProfile;
	WriteContextInput.ResolvedPelvisComp = ResolvedPelvisComp;
	WriteContextInput.bHasResolvedPelvisComp = bHasResolvedPelvisComp;
	WriteContextInput.RefChestPosComp = RefChestPosComp;
	WriteContextInput.RefHeadPosComp = RefHeadPosComp;
	WriteContextInput.RefNeckPosComp = RefNeckPosComp;
	WriteContextInput.RefNeck02PosComp = RefNeck02PosComp;
	WriteContextInput.bHasRefChestPosComp = bHasRefChestPosComp;
	WriteContextInput.bHasRefNeck02PosComp = bHasRefNeck02PosComp;
	FMediaPipeBodyFusionPoseWriteContext WriteContext;
	if (!FMediaPipeBodyFusionPoseWriteContextBuilder::Build(WriteContextInput, WriteContext))
	{
		return true;
	}

	FVector PelvisComp = WriteContext.PelvisComp;
	FVector ChestComp = WriteContext.ChestComp;
	FVector HeadComp = WriteContext.HeadComp;
	const FVector UpComp = WriteContext.UpComp;
	const FVector ForwardHintComp = WriteContext.ForwardHintComp;
	const FQuat PelvisTargetBasis = WriteContext.PelvisTargetBasis;
	const FQuat ChestTargetBasis = WriteContext.ChestTargetBasis;

	auto ResolveSemanticBoneRotationCS = [&](const FBoneReference& Bone, const FQuat& RefBoneComp, const FQuat& RefBasisComp,
		const FQuat& TargetBasisComp, bool& bHasSmoothedRot, FQuat& InOutSmoothedRotCS, const float Alpha, FQuat& OutRotCS) -> bool
	{
		if (!Bone.IsValidToEvaluate() || TargetBasisComp.IsIdentity() || RefBasisComp.IsIdentity())
		{
			return false;
		}

		FQuat TargetRotCS = FQuat::Identity;
		if (!FMediaPipeAvatarPoseWriter::TryResolveSemanticBoneRotationCS(
			RefBoneComp,
			RefBasisComp,
			TargetBasisComp,
			TargetRotCS))
		{
			return false;
		}

		UpdateSmoothedRotation(bHasSmoothedRot, InOutSmoothedRotCS, TargetRotCS, Alpha);
		OutRotCS = InOutSmoothedRotCS;
		return true;
	};

	auto ApplySemanticBasisToBone = [&](const FBoneReference& Bone, const FQuat& RefBoneComp, const FQuat& RefBasisComp,
		const FQuat& TargetBasisComp, bool& bHasSmoothedRot, FQuat& InOutSmoothedRotCS, const float Alpha)
	{
		FQuat ResolvedRotCS = FQuat::Identity;
		if (ResolveSemanticBoneRotationCS(
			Bone,
			RefBoneComp,
			RefBasisComp,
			TargetBasisComp,
			bHasSmoothedRot,
			InOutSmoothedRotCS,
			Alpha,
			ResolvedRotCS))
		{
			ApplyRotationCS(CSPose, Bone, ResolvedRotCS);
		}
	};

	auto ApplyComponentTranslationToBone = [&](const FBoneReference& Bone, const FVector& TargetComp)
	{
		if (!Bone.IsValidToEvaluate() || TargetComp.ContainsNaN())
		{
			return;
		}

		const FCompactPoseBoneIndex BoneIdx = Bone.CachedCompactPoseIndex;
		FTransform BoneCS = CSPose.GetComponentSpaceTransform(BoneIdx);
		BoneCS.SetTranslation(TargetComp);
		const FBoneTransform BoneTransform(BoneIdx, BoneCS);
		CSPose.SafeSetCSBoneTransforms(MakeArrayView(&BoneTransform, 1));
	};

	const bool bHmdBodyFusionHeadAuthoritative =
		WriteContext.bHmdHeadAuthoritative &&
		BodyFusionFrame.SourceFrame.HmdStatus.IsFresh();
	const float SpineRotAlpha = bHmdBodyFusionHeadAuthoritative
		? 1.0f
		: HalfLifeToAlpha(SpineRotationHalfLifeSeconds, FMath::Max(DeltaSeconds, 0.0f));
	const float HeadRotAlpha = bHmdBodyFusionHeadAuthoritative
		? 1.0f
		: HalfLifeToAlpha(HeadRotationHalfLifeSeconds, FMath::Max(DeltaSeconds, 0.0f));

	ApplySemanticBasisToBone(Pelvis, RefPelvisComp, RefPelvisBasisComp, PelvisTargetBasis, BodyState.bHasSmoothedPelvisRotCS, BodyState.SmoothedPelvisRotCS, SpineRotAlpha);

	auto GetSpineRefBySlot = [&](const uint8 Slot) -> const FBoneReference&
	{
		switch (Slot)
		{
		case 1: return Spine01;
		case 2: return Spine02;
		case 3: return Spine03;
		case 4: return Spine04;
		case 5: return Spine05;
		default: return Spine03;
		}
	};

	auto ApplyBodyFusionSpineTranslationTargets = [&]()
	{
		if (bAvatarLockedMetaHumanReplay)
		{
			return;
		}
		if (ForwardProfile.SkeletonFamily != EMediaPipeAvatarSkeletonFamily::MetaHuman ||
			NumSpineBones <= 0 ||
			!bHasRefChestPosComp)
		{
			return;
		}

		const FVector RefPelvisToChestComp = RefChestPosComp - RefPelvisTranslationComp;
		const float RefPelvisToChestLenSq = RefPelvisToChestComp.SizeSquared();
		const FVector SolvedPelvisToChestComp = ChestComp - PelvisComp;
		if (RefPelvisToChestLenSq <= KINDA_SMALL_NUMBER ||
			SolvedPelvisToChestComp.IsNearlyZero() ||
			SolvedPelvisToChestComp.ContainsNaN())
		{
			return;
		}

		const float Denom = FMath::Max(1.0f, static_cast<float>(NumSpineBones));
		for (int32 i = 0; i < NumSpineBones; ++i)
		{
			const uint8 Slot = SpineBoneSlots[i];
			if (Slot == 0)
			{
				continue;
			}

			const FVector RefSpineOffsetComp = RefSpineTranslationComp[i] - RefPelvisTranslationComp;
			float SpineWeight =
				FVector::DotProduct(RefSpineOffsetComp, RefPelvisToChestComp) / RefPelvisToChestLenSq;
			if (!FMath::IsFinite(SpineWeight))
			{
				SpineWeight = static_cast<float>(i + 1) / Denom;
			}
			SpineWeight = FMath::Clamp(SpineWeight, 0.0f, 1.0f);
			if (SpineWeight <= KINDA_SMALL_NUMBER)
			{
				SpineWeight = FMath::Clamp(static_cast<float>(i + 1) / Denom, 0.0f, 1.0f);
			}

			const FVector TargetSpineComp = PelvisComp + SolvedPelvisToChestComp * SpineWeight;
			ApplyComponentTranslationToBone(GetSpineRefBySlot(Slot), TargetSpineComp);
		}

		const uint8 TopSpineSlot = SpineBoneSlots[NumSpineBones - 1];
		if (TopSpineSlot != 0)
		{
			ApplyComponentTranslationToBone(GetSpineRefBySlot(TopSpineSlot), ChestComp);
		}
	};

	ApplyBodyFusionSpineTranslationTargets();

	const float Denom = FMath::Max(1.0f, static_cast<float>(NumSpineBones));
	for (int32 i = 0; i < NumSpineBones; ++i)
	{
		const uint8 Slot = SpineBoneSlots[i];
		if (Slot == 0)
		{
			continue;
		}

		const FBoneReference& SpineBone = GetSpineRefBySlot(Slot);
		const float Weight = static_cast<float>(i + 1) / Denom;
		const FQuat TargetBasis = FQuat::Slerp(PelvisTargetBasis, ChestTargetBasis, Weight).GetNormalized();
		ApplySemanticBasisToBone(SpineBone, RefSpineComp[i], RefSpineBasisComp[i], TargetBasis, BodyState.bHasSmoothedSpineRotCS[i], BodyState.SmoothedSpineRotCS[i], SpineRotAlpha);
	}

	// Spine rotations can recompose child component-space positions. Refresh the
	// solved translations so the chest anchor remains authoritative before the
	// neck/head chain is derived from it.
	ApplyBodyFusionSpineTranslationTargets();

	const FQuat HmdRotationComp = WriteContext.HmdRotationComp;
	const FQuat HeadTargetBasis = WriteContext.HeadTargetBasis;
	if (!WriteContext.bHasNeckChainTargets)
	{
		return true;
	}
	const float RefNeckAlpha = WriteContext.RefNeckAlpha;
	const float RefNeck02Alpha = WriteContext.RefNeck02Alpha;
	FQuat HeadRotationCS = FQuat::Identity;
	const bool bHasHeadRotationCS = ResolveSemanticBoneRotationCS(
		Head,
		RefHeadComp,
		RefHeadBasisComp,
		HeadTargetBasis,
		BodyState.bHasSmoothedHeadRotCS,
		BodyState.SmoothedHeadRotCS,
		HeadRotAlpha,
		HeadRotationCS);
	FMediaPipeAvatarEmbodimentProfile HeadAnchorProfile = ForwardProfile;
	HeadAnchorProfile.DefaultEyeLocalOffset = bHasTargetEyeLocalOffset
		? TargetEyeLocalOffset
		: HeadAnchorProfile.DefaultEyeLocalOffset;
	const bool bCanAnchorHeadFromEye =
		bHasHeadRotationCS &&
		BodyFusionFrame.Pose.Eye.bValid &&
		Head.IsValidToEvaluate() &&
		!RefHeadPosComp.IsNearlyZero() &&
		!HeadAnchorProfile.DefaultEyeLocalOffset.ContainsNaN();
	const FVector EyeLocalInHeadForSolve = bCanAnchorHeadFromEye
		? ResolveMediaPipeAvatarProfileEyeLocalInHead(HeadAnchorProfile)
		: FVector::ZeroVector;
	FVector EyeLocalInHeadForPose = FVector::ZeroVector;
	bool bHasEyeLocalInHeadForPose = false;
	if (bCanAnchorHeadFromEye)
	{
		const FVector EyeOffsetFromHeadComp = HeadAnchorProfile.DefaultEyeLocalOffset - RefHeadPosComp;
		if (!EyeOffsetFromHeadComp.ContainsNaN())
		{
			EyeLocalInHeadForPose = RefHeadComp.Inverse().RotateVector(EyeOffsetFromHeadComp);
			bHasEyeLocalInHeadForPose = !EyeLocalInHeadForPose.ContainsNaN();
		}
	}
	if (bCanAnchorHeadFromEye)
	{
		const FVector TargetEyeComp = WorldToComponent.TransformPosition(BodyFusionFrame.Pose.Eye.LocationWorld);
		FVector EyeAnchoredHeadComp = FVector::ZeroVector;
		if (bHasEyeLocalInHeadForPose)
		{
			EyeAnchoredHeadComp = TargetEyeComp - HeadRotationCS.RotateVector(EyeLocalInHeadForPose);
		}
		else if (!EyeLocalInHeadForSolve.ContainsNaN())
		{
			EyeAnchoredHeadComp = TargetEyeComp - HmdRotationComp.RotateVector(EyeLocalInHeadForSolve);
		}
		if (!EyeAnchoredHeadComp.ContainsNaN())
		{
			HeadComp = EyeAnchoredHeadComp;
		}
	}

	const bool bProtectedNeckChain =
		ForwardProfile.SkeletonFamily == EMediaPipeAvatarSkeletonFamily::MetaHuman &&
		bHasRefChestPosComp &&
		FMediaPipeBodyFusionPoseWriteContextBuilder::ProtectNeckChainAgainstCollapse(
			RefChestPosComp,
			RefHeadPosComp,
			UpComp,
			ChestComp,
			HeadComp);
	if (bProtectedNeckChain)
	{
		UE_LOG(LogMediaPipePose, VeryVerbose,
			TEXT("mp.BodyFusion.NeckChainProtected actor=%s chest=%s head=%s refChest=%s refHead=%s"),
			*TargetActorName.ToString(),
			*ChestComp.ToCompactString(),
			*HeadComp.ToCompactString(),
			*RefChestPosComp.ToCompactString(),
			*RefHeadPosComp.ToCompactString());
	}

	const FQuat NeckTargetBasis = FQuat::Slerp(ChestTargetBasis, HeadTargetBasis, RefNeckAlpha).GetNormalized();
	const FQuat Neck02TargetBasis = FQuat::Slerp(ChestTargetBasis, HeadTargetBasis, RefNeck02Alpha).GetNormalized();
	const FVector NeckTargetComp = FMath::Lerp(ChestComp, HeadComp, RefNeckAlpha);
	const FVector Neck02TargetComp = FMath::Lerp(ChestComp, HeadComp, RefNeck02Alpha);

	ApplySemanticBasisToBone(Neck, RefNeckComp, RefNeckBasisComp, NeckTargetBasis, BodyState.bHasSmoothedNeckRotCS, BodyState.SmoothedNeckRotCS, HeadRotAlpha);
	ApplySemanticBasisToBone(Neck02, RefNeck02Comp, RefNeck02BasisComp, Neck02TargetBasis, BodyState.bHasSmoothedNeck02RotCS, BodyState.SmoothedNeck02RotCS, HeadRotAlpha);
	if (bHasHeadRotationCS)
	{
		ApplyRotationCS(CSPose, Head, HeadRotationCS);
	}
	// Rotation writes can recompose child component-space positions. Finish with
	// a parent-to-child translation pass so the BodyFusion chest, neck, and HMD
	// eye anchors all remain authoritative after deep lean poses.
	// Regression guard: final translation order is spine/chest -> neck -> neck_02 -> head.
	ApplyBodyFusionSpineTranslationTargets();
	if (!bAvatarLockedMetaHumanReplay)
	{
		ApplyComponentTranslationToBone(Neck, NeckTargetComp);
		ApplyComponentTranslationToBone(Neck02, Neck02TargetComp);
		ApplyComponentTranslationToBone(Head, HeadComp);
	}

	float EyeAnchorResidualCm = 0.0f;
	FVector EyeAnchorResidualDeltaComp = FVector::ZeroVector;
	if (bCanAnchorHeadFromEye)
	{
		const FTransform HeadPoseComp =
			CSPose.GetComponentSpaceTransform(Head.CachedCompactPoseIndex);
		const FVector PosedEyeComp = HeadPoseComp.TransformPosition(
			bHasEyeLocalInHeadForPose ? EyeLocalInHeadForPose : EyeLocalInHeadForSolve);
		const FVector TargetEyeComp = WorldToComponent.TransformPosition(BodyFusionFrame.Pose.Eye.LocationWorld);
		FVector EyeLockDeltaComp = TargetEyeComp - PosedEyeComp;
		if (!EyeLockDeltaComp.ContainsNaN())
		{
			EyeAnchorResidualCm = EyeLockDeltaComp.Size();
			EyeAnchorResidualDeltaComp = EyeLockDeltaComp;
		}
	}
	if (FMediaPipeBodyFusionRuntimePolicy::IsDebugEnabledAnyThread())
	{
		const double NowSeconds = FPlatformTime::Seconds();
		if (FMediaPipePoseDiagnosticReporter::ShouldEmitThrottled(
			NowSeconds,
			1.0,
			DiagnosticsState.LastBodyFusionPoseWriteDebugLogTimeSeconds))
		{
			FMediaPipeAvatarEmbodimentProfile DebugProfile = bHasTargetEmbodimentProfile
				? TargetEmbodimentProfile
				: FMediaPipeAvatarEmbodimentProfile();
			DebugProfile.bUseTargetFaceForwardAxis = bUseTargetFaceForwardAxis;
			DebugProfile.DefaultEyeLocalOffset = bHasTargetEyeLocalOffset
				? TargetEyeLocalOffset
				: DebugProfile.DefaultEyeLocalOffset;
			DebugProfile.EmbodiedCameraForwardOffsetCm = TargetEmbodiedCameraForwardOffsetCm;

			FVector AvatarForwardWorld = GetTargetForwardWorld().GetSafeNormal();
			if (AvatarForwardWorld.IsNearlyZero())
			{
				AvatarForwardWorld = ComponentToWorld.TransformVectorNoScale(ForwardHintComp).GetSafeNormal();
			}
			if (AvatarForwardWorld.IsNearlyZero())
			{
				AvatarForwardWorld = FVector::ForwardVector;
			}
			FVector AvatarUpWorld =
				FMediaPipeAvatarEmbodimentSolver::GetAvatarUpWorld(TargetCompTransform, AvatarForwardWorld).GetSafeNormal();
			if (AvatarUpWorld.IsNearlyZero())
			{
				AvatarUpWorld = ComponentToWorld.TransformVectorNoScale(UpComp).GetSafeNormal();
			}
			if (AvatarUpWorld.IsNearlyZero())
			{
				AvatarUpWorld = FVector::UpVector;
			}
			FVector AvatarRightWorld = FVector::CrossProduct(AvatarUpWorld, AvatarForwardWorld).GetSafeNormal();
			if (AvatarRightWorld.IsNearlyZero())
			{
				AvatarRightWorld = FVector::RightVector;
			}

			auto GetPoseTransformComp = [&](const FBoneReference& Bone, const FTransform& Fallback) -> FTransform
			{
				return Bone.IsValidToEvaluate()
					? CSPose.GetComponentSpaceTransform(Bone.CachedCompactPoseIndex)
					: Fallback;
			};

			const FTransform HeadPoseComp = GetPoseTransformComp(
				Head,
				FTransform(RefHeadComp, HeadComp));
			const FTransform ChestPoseComp = GetPoseTransformComp(
				Spine05.IsValidToEvaluate() ? Spine05 : Spine03,
				FTransform(ChestTargetBasis, ChestComp));
			const FTransform PelvisPoseComp = GetPoseTransformComp(
				Pelvis,
				FTransform(PelvisTargetBasis, PelvisComp));
			const FTransform NeckPoseComp = GetPoseTransformComp(
				Neck,
				FTransform(NeckTargetBasis, NeckTargetComp));
			const FTransform Neck02PoseComp = GetPoseTransformComp(
				Neck02,
				FTransform(Neck02TargetBasis, Neck02TargetComp));

			FVector EyeLocalInHead = ResolveMediaPipeAvatarProfileEyeLocalInHead(DebugProfile);
			if (bCanAnchorHeadFromEye)
			{
				EyeLocalInHead = bHasEyeLocalInHeadForPose ? EyeLocalInHeadForPose : EyeLocalInHeadForSolve;
			}
			const FVector PosedEyeComp = HeadPoseComp.TransformPosition(EyeLocalInHead);
			const FVector PosedEyeWorld = ComponentToWorld.TransformPosition(PosedEyeComp);
			const FVector PosedHeadWorld = ComponentToWorld.TransformPosition(HeadPoseComp.GetTranslation());
			const FVector PosedChestWorld = ComponentToWorld.TransformPosition(ChestPoseComp.GetTranslation());
			const FVector PosedPelvisWorld = ComponentToWorld.TransformPosition(PelvisPoseComp.GetTranslation());
			const FVector PosedNeckWorld = ComponentToWorld.TransformPosition(NeckPoseComp.GetTranslation());
			const FVector PosedNeck02World = ComponentToWorld.TransformPosition(Neck02PoseComp.GetTranslation());
			const FVector ExpectedCameraFromPosedEye =
				PosedEyeWorld + AvatarForwardWorld * DebugProfile.EmbodiedCameraForwardOffsetCm;

			const bool bHasHmd = BodyFusionFrame.SourceFrame.HmdStatus.IsFresh();
			const FVector HmdWorld = BodyFusionFrame.SourceFrame.HmdLocationWorld;
			const float CameraToPosedEyeCameraCm = bHasHmd
				? FVector::Distance(HmdWorld, ExpectedCameraFromPosedEye)
				: -1.0f;
			const FVector SolverCameraFromEye =
				BodyFusionFrame.Pose.Eye.LocationWorld + AvatarForwardWorld * DebugProfile.EmbodiedCameraForwardOffsetCm;
			const float CameraToSolverCameraCm = bHasHmd
				? FVector::Distance(HmdWorld, SolverCameraFromEye)
				: -1.0f;
			const float SolverEyeToPosedEyeCm =
				FVector::Distance(BodyFusionFrame.Pose.Eye.LocationWorld, PosedEyeWorld);
			const float SolverHeadToPosedHeadCm =
				FVector::Distance(BodyFusionFrame.Pose.Head.LocationWorld, PosedHeadWorld);
			const float SolverChestToPosedChestCm =
				FVector::Distance(BodyFusionFrame.Pose.Chest.LocationWorld, PosedChestWorld);
			const FVector HmdToPosedChestWorld = PosedChestWorld - HmdWorld;
			const float HmdToPosedChestCm = bHasHmd ? HmdToPosedChestWorld.Size() : -1.0f;
			const float HmdToPosedChestForwardCm = bHasHmd
				? FVector::DotProduct(HmdToPosedChestWorld, AvatarForwardWorld)
				: 0.0f;
			const float HmdToPosedChestUpCm = bHasHmd
				? FVector::DotProduct(HmdToPosedChestWorld, AvatarUpWorld)
				: 0.0f;
			const float HmdToPosedChestRightCm = bHasHmd
				? FVector::DotProduct(HmdToPosedChestWorld, AvatarRightWorld)
				: 0.0f;
			const FVector HmdToPosedHeadWorld = PosedHeadWorld - HmdWorld;
			const float HmdToPosedHeadCm = bHasHmd ? HmdToPosedHeadWorld.Size() : -1.0f;
			const float HmdToPosedHeadForwardCm = bHasHmd
				? FVector::DotProduct(HmdToPosedHeadWorld, AvatarForwardWorld)
				: 0.0f;
			const float HmdToPosedHeadRightCm = bHasHmd
				? FVector::DotProduct(HmdToPosedHeadWorld, AvatarRightWorld)
				: 0.0f;
			const float HmdToPosedHeadUpCm = bHasHmd
				? FVector::DotProduct(HmdToPosedHeadWorld, AvatarUpWorld)
				: 0.0f;
			const FVector EyeAnchorResidualDeltaWorld =
				ComponentToWorld.TransformVectorNoScale(EyeAnchorResidualDeltaComp);
			const float EyeAnchorResidualForwardCm =
				FVector::DotProduct(EyeAnchorResidualDeltaWorld, AvatarForwardWorld);
			const float EyeAnchorResidualRightCm =
				FVector::DotProduct(EyeAnchorResidualDeltaWorld, AvatarRightWorld);
			const float EyeAnchorResidualUpCm =
				FVector::DotProduct(EyeAnchorResidualDeltaWorld, AvatarUpWorld);

			auto ForwardLeanDegrees = [&](const FVector& SegmentWorld) -> float
			{
				FVector Segment = SegmentWorld.GetSafeNormal();
				Segment = (Segment - FVector::DotProduct(Segment, AvatarRightWorld) * AvatarRightWorld).GetSafeNormal();
				if (Segment.IsNearlyZero())
				{
					return 0.0f;
				}
				return FMath::RadiansToDegrees(FMath::Atan2(
					FVector::DotProduct(Segment, AvatarForwardWorld),
					FVector::DotProduct(Segment, AvatarUpWorld)));
			};

			auto SideLeanDegrees = [&](const FVector& SegmentWorld) -> float
			{
				FVector Segment = SegmentWorld.GetSafeNormal();
				Segment = (Segment - FVector::DotProduct(Segment, AvatarForwardWorld) * AvatarForwardWorld).GetSafeNormal();
				if (Segment.IsNearlyZero())
				{
					return 0.0f;
				}
				return FMath::RadiansToDegrees(FMath::Atan2(
					FVector::DotProduct(Segment, AvatarRightWorld),
					FVector::DotProduct(Segment, AvatarUpWorld)));
			};

			auto SignedYawFromAvatarDegrees = [&](const FVector& SegmentWorld) -> float
			{
				FVector Segment = SegmentWorld.GetSafeNormal();
				Segment = (Segment - FVector::DotProduct(Segment, AvatarUpWorld) * AvatarUpWorld).GetSafeNormal();
				FVector ForwardPlanar = (AvatarForwardWorld - FVector::DotProduct(AvatarForwardWorld, AvatarUpWorld) * AvatarUpWorld).GetSafeNormal();
				if (Segment.IsNearlyZero() || ForwardPlanar.IsNearlyZero())
				{
					return 0.0f;
				}
				const float SignedSin =
					FVector::DotProduct(FVector::CrossProduct(ForwardPlanar, Segment), AvatarUpWorld);
				const float Cos = FVector::DotProduct(ForwardPlanar, Segment);
				return FMath::RadiansToDegrees(FMath::Atan2(SignedSin, Cos));
			};

			const FRotator HmdRot = bHasHmd ? BodyFusionFrame.SourceFrame.HmdRotationWorld.Rotator() : FRotator::ZeroRotator;
			const FVector HmdForwardWorld = bHasHmd
				? BodyFusionFrame.SourceFrame.HmdRotationWorld.RotateVector(FVector::ForwardVector).GetSafeNormal()
				: FVector::ZeroVector;
			const float HmdPitchDeg = bHasHmd ? HmdRot.Pitch : 0.0f;
			const float HmdYawDeg = bHasHmd ? HmdRot.Yaw : 0.0f;
			const float HmdRollDeg = bHasHmd ? HmdRot.Roll : 0.0f;
			const float HmdYawFromAvatarDeg = bHasHmd ? SignedYawFromAvatarDegrees(HmdForwardWorld) : 0.0f;
			const float PosedPelvisChestLeanDeg = ForwardLeanDegrees(PosedChestWorld - PosedPelvisWorld);
			const float PosedChestHeadLeanDeg = ForwardLeanDegrees(PosedHeadWorld - PosedChestWorld);
			const float SolverPelvisChestLeanDeg =
				ForwardLeanDegrees(BodyFusionFrame.Pose.Chest.LocationWorld - BodyFusionFrame.Pose.Pelvis.LocationWorld);
			const float SolverChestHeadLeanDeg =
				ForwardLeanDegrees(BodyFusionFrame.Pose.Head.LocationWorld - BodyFusionFrame.Pose.Chest.LocationWorld);
			const float PosedPelvisChestSideLeanDeg = SideLeanDegrees(PosedChestWorld - PosedPelvisWorld);
			const float PosedChestNeckSideLeanDeg = SideLeanDegrees(PosedNeckWorld - PosedChestWorld);
			const float PosedNeckHeadSideLeanDeg = SideLeanDegrees(PosedHeadWorld - PosedNeckWorld);
			const float PosedChestHeadSideLeanDeg = SideLeanDegrees(PosedHeadWorld - PosedChestWorld);
			const float SolverPelvisChestSideLeanDeg =
				SideLeanDegrees(BodyFusionFrame.Pose.Chest.LocationWorld - BodyFusionFrame.Pose.Pelvis.LocationWorld);
			const float SolverChestHeadSideLeanDeg =
				SideLeanDegrees(BodyFusionFrame.Pose.Head.LocationWorld - BodyFusionFrame.Pose.Chest.LocationWorld);
			const bool bHasSolverNeck = BodyFusionFrame.Pose.Neck.bValid;
			const float SolverChestNeckSideLeanDeg = bHasSolverNeck
				? SideLeanDegrees(BodyFusionFrame.Pose.Neck.LocationWorld - BodyFusionFrame.Pose.Chest.LocationWorld)
				: 0.0f;
			const float SolverNeckHeadSideLeanDeg = bHasSolverNeck
				? SideLeanDegrees(BodyFusionFrame.Pose.Head.LocationWorld - BodyFusionFrame.Pose.Neck.LocationWorld)
				: 0.0f;
			const FVector SolverNeckToPosedNeckWorld = bHasSolverNeck
				? PosedNeckWorld - BodyFusionFrame.Pose.Neck.LocationWorld
				: FVector::ZeroVector;
			const float SolverNeckToPosedNeckCm = bHasSolverNeck
				? SolverNeckToPosedNeckWorld.Size()
				: -1.0f;
			const float SolverNeckToPosedNeckRightCm = bHasSolverNeck
				? FVector::DotProduct(SolverNeckToPosedNeckWorld, AvatarRightWorld)
				: 0.0f;
			const float SolverNeckToPosedNeckForwardCm = bHasSolverNeck
				? FVector::DotProduct(SolverNeckToPosedNeckWorld, AvatarForwardWorld)
				: 0.0f;
			const float SolverNeckToPosedNeckUpCm = bHasSolverNeck
				? FVector::DotProduct(SolverNeckToPosedNeckWorld, AvatarUpWorld)
				: 0.0f;

			FVector MediaPipeHeadAvatarWorld = FVector::ZeroVector;
			FVector MediaPipeShoulderAvatarWorld = FVector::ZeroVector;
			float MediaPipeHeadReliability = 0.0f;
			float MediaPipeShoulderReliability = 0.0f;
			const bool bHasMediaPipeHead =
				BodyFusionFrame.Calibration.IsUsable() &&
				BodyFusionFrame.SourceFrame.TryGetBodyLandmark(
					EMediaPipePoseLandmark::Nose,
					MediaPipeHeadAvatarWorld,
					&MediaPipeHeadReliability);
			if (bHasMediaPipeHead)
			{
				MediaPipeHeadAvatarWorld = BodyFusionFrame.Calibration.TransformMediaPipePoint(MediaPipeHeadAvatarWorld);
			}
			const bool bHasMediaPipeShoulder =
				BodyFusionFrame.Calibration.IsUsable() &&
				FMediaPipeBodyFusionDebugFormatter::TryLandmarkMidpoint(
					BodyFusionFrame.SourceFrame,
					EMediaPipePoseLandmark::LeftShoulder,
					EMediaPipePoseLandmark::RightShoulder,
					MediaPipeShoulderAvatarWorld,
					&MediaPipeShoulderReliability);
			if (bHasMediaPipeShoulder)
			{
				MediaPipeShoulderAvatarWorld = BodyFusionFrame.Calibration.TransformMediaPipePoint(MediaPipeShoulderAvatarWorld);
			}

			const float MediaPipeHeadToHmdCm = bHasHmd && bHasMediaPipeHead
				? FVector::Distance(HmdWorld, MediaPipeHeadAvatarWorld)
				: -1.0f;
			const float MediaPipeHeadToSolverHeadCm = bHasMediaPipeHead
				? FVector::Distance(BodyFusionFrame.Pose.Head.LocationWorld, MediaPipeHeadAvatarWorld)
				: -1.0f;
			const float MediaPipeShoulderToSolverChestCm = bHasMediaPipeShoulder
				? FVector::Distance(BodyFusionFrame.Pose.Chest.LocationWorld, MediaPipeShoulderAvatarWorld)
				: -1.0f;

			UE_LOG(LogMediaPipePose, Log,
				TEXT("mp.BodyFusion.HeadAnchor actor=%s skeleton=%s bodyAuthority=%s mediaPipeAuthority=%d reason=\"%s\" directNeckChain=1 hmd=%s solverCamera=%s posedCamera=%s solverEye=%s posedEye=%s posedHead=%s posedChest=%s posedPelvis=%s eyeAnchor(residual=%.1f) err(cameraToSolverCamera=%.1f cameraToPosedCamera=%.1f solverEyeToPosedEye=%.1f solverHeadToPosedHead=%.1f solverChestToPosedChest=%.1f) ownerView(chestDist=%.1f chestForward=%.1f chestUp=%.1f) lean(hmdPitch=%.1f posedPelvisChest=%.1f posedChestHead=%.1f solverPelvisChest=%.1f solverChestHead=%.1f) mediapipe(calibrated=%d scale=%.3f stableFrames=%d stableSeconds=%.2f nose=%s noseRel=%.2f noseToHmd=%.1f noseToSolverHead=%.1f shoulders=%s shoulderRel=%.2f shoulderToSolverChest=%.1f)"),
				*TargetActorName.ToString(),
				DebugProfile.SkeletonFamily == EMediaPipeAvatarSkeletonFamily::MetaHuman ? TEXT("MetaHuman") : TEXT("Manny"),
				FMediaPipeBodyFusionDebugFormatter::AuthorityStateName(BodyFusionFrame.AuthorityState),
				BodyFusionFrame.bMediaPipeAuthorityAllowed ? 1 : 0,
				*BodyFusionFrame.AuthorityReason,
				*FMediaPipeBodyFusionDebugFormatter::VectorString(HmdWorld),
				*FMediaPipeBodyFusionDebugFormatter::VectorString(SolverCameraFromEye),
				*FMediaPipeBodyFusionDebugFormatter::VectorString(ExpectedCameraFromPosedEye),
				*FMediaPipeBodyFusionDebugFormatter::VectorString(BodyFusionFrame.Pose.Eye.LocationWorld),
				*FMediaPipeBodyFusionDebugFormatter::VectorString(PosedEyeWorld),
				*FMediaPipeBodyFusionDebugFormatter::VectorString(PosedHeadWorld),
				*FMediaPipeBodyFusionDebugFormatter::VectorString(PosedChestWorld),
				*FMediaPipeBodyFusionDebugFormatter::VectorString(PosedPelvisWorld),
				EyeAnchorResidualCm,
				CameraToSolverCameraCm,
				CameraToPosedEyeCameraCm,
				SolverEyeToPosedEyeCm,
				SolverHeadToPosedHeadCm,
				SolverChestToPosedChestCm,
				HmdToPosedChestCm,
				HmdToPosedChestForwardCm,
				HmdToPosedChestUpCm,
				HmdPitchDeg,
				PosedPelvisChestLeanDeg,
				PosedChestHeadLeanDeg,
				SolverPelvisChestLeanDeg,
				SolverChestHeadLeanDeg,
				BodyFusionFrame.Calibration.IsUsable() ? 1 : 0,
				BodyFusionFrame.Calibration.Scale,
				BodyFusionFrame.CalibrationStableFrameCount,
				BodyFusionFrame.CalibrationStableSeconds,
				bHasMediaPipeHead ? *FMediaPipeBodyFusionDebugFormatter::VectorString(MediaPipeHeadAvatarWorld) : TEXT("(missing)"),
				MediaPipeHeadReliability,
				MediaPipeHeadToHmdCm,
				MediaPipeHeadToSolverHeadCm,
				bHasMediaPipeShoulder ? *FMediaPipeBodyFusionDebugFormatter::VectorString(MediaPipeShoulderAvatarWorld) : TEXT("(missing)"),
				MediaPipeShoulderReliability,
				MediaPipeShoulderToSolverChestCm);

			UE_LOG(LogMediaPipePose, Log,
				TEXT("mp.BodyFusion.HeadAnchorSide actor=%s skeleton=%s hmdRot(pitch=%.1f yaw=%.1f roll=%.1f yawFromAvatar=%.1f) ownerView(chestRight=%.1f headDist=%.1f headForward=%.1f headRight=%.1f headUp=%.1f) sideLean(posedPelvisChest=%.1f posedChestNeck=%.1f posedNeckHead=%.1f posedChestHead=%.1f solverPelvisChest=%.1f solverChestNeck=%.1f solverNeckHead=%.1f solverChestHead=%.1f) neck(posed=%s posedNeck02=%s solver=%s solverToPosed=%.1f right=%.1f forward=%.1f up=%.1f) eyeAnchorResidual(vec=%s right=%.1f forward=%.1f up=%.1f)"),
				*TargetActorName.ToString(),
				DebugProfile.SkeletonFamily == EMediaPipeAvatarSkeletonFamily::MetaHuman ? TEXT("MetaHuman") : TEXT("Manny"),
				HmdPitchDeg,
				HmdYawDeg,
				HmdRollDeg,
				HmdYawFromAvatarDeg,
				HmdToPosedChestRightCm,
				HmdToPosedHeadCm,
				HmdToPosedHeadForwardCm,
				HmdToPosedHeadRightCm,
				HmdToPosedHeadUpCm,
				PosedPelvisChestSideLeanDeg,
				PosedChestNeckSideLeanDeg,
				PosedNeckHeadSideLeanDeg,
				PosedChestHeadSideLeanDeg,
				SolverPelvisChestSideLeanDeg,
				SolverChestNeckSideLeanDeg,
				SolverNeckHeadSideLeanDeg,
				SolverChestHeadSideLeanDeg,
				*FMediaPipeBodyFusionDebugFormatter::VectorString(PosedNeckWorld),
				*FMediaPipeBodyFusionDebugFormatter::VectorString(PosedNeck02World),
				bHasSolverNeck ? *FMediaPipeBodyFusionDebugFormatter::VectorString(BodyFusionFrame.Pose.Neck.LocationWorld) : TEXT("(missing)"),
				SolverNeckToPosedNeckCm,
				SolverNeckToPosedNeckRightCm,
				SolverNeckToPosedNeckForwardCm,
				SolverNeckToPosedNeckUpCm,
				*FMediaPipeBodyFusionDebugFormatter::VectorString(EyeAnchorResidualDeltaWorld),
				EyeAnchorResidualRightCm,
				EyeAnchorResidualForwardCm,
				EyeAnchorResidualUpCm);
		}
	}

	return true;
}

#include "MediaPipePoseDrivenAnimInstance_ArmTwist.inl"

#include "MediaPipePoseDrivenAnimInstance_BodyPoseSolve.inl"

#include "MediaPipePoseDrivenAnimInstance_QuestArmWristSolve.inl"

#include "MediaPipePoseDrivenAnimInstance_LegSolve.inl"

void FAnimNode_MediaPipePoseDriven::Evaluate_AnyThread(FPoseContext& Output)
{
	const float DeltaSeconds = FMath::Max(CachedDeltaTimeSeconds, 0.0f);
	CachedDeltaTimeSeconds = 0.0f;

	Output.ResetToRefPose();

	const bool bHasBodyFusionPoseInput =
		ShouldUseBodyFusionPoseForEvaluation() ||
		ShouldUseBodyFusionStage1TorsoPelvisHintForEvaluation() ||
		ShouldUseBodyFusionStage2ShoulderClavicleHintForEvaluation();
	if (!bHasReferencePose || (!bHasPoseFrame && !bHasQuestOrHmdRuntimeInput && !bHasBodyFusionPoseInput))
	{
		return;
	}

	if (bHasPoseFrame)
	{
		const int64 ActivePoseTimestampUs = PoseFrame.TimestampUs;

		if (bHasLastPoseTimestamp && ActivePoseTimestampUs < LastPoseTimestampUs)
		{
			BodyState.bHasReferenceHipHeight = false;
			BodyState.ReferenceHipHeightCm = 0.0f;
			BodyState.bHasSmoothedPelvisOffset = false;
			BodyState.SmoothedPelvisOffsetComp = FVector::ZeroVector;
			BodyState.bHasSmoothedStage1ChestOffset = false;
			BodyState.SmoothedStage1ChestOffsetComp = FVector::ZeroVector;
			BodyState.bHasSmoothedStage2ClavicleLiftL = false;
			BodyState.SmoothedStage2ClavicleLiftCmL = 0.0f;
			BodyState.bHasStage2NeutralReferenceL = false;
			BodyState.Stage2NeutralShoulderLiftFromPelvisCmL = 0.0f;
			BodyState.Stage2NeutralShoulderHeadClearanceCmL = 0.0f;
			BodyState.Stage2NeutralObservationSecondsL = 0.0f;
			BodyState.Stage2NeutralObservationFramesL = 0;
			BodyState.bHasSmoothedStage2ClavicleLiftR = false;
			BodyState.SmoothedStage2ClavicleLiftCmR = 0.0f;
			BodyState.bHasStage2NeutralReferenceR = false;
			BodyState.Stage2NeutralShoulderLiftFromPelvisCmR = 0.0f;
			BodyState.Stage2NeutralShoulderHeadClearanceCmR = 0.0f;
			BodyState.Stage2NeutralObservationSecondsR = 0.0f;
			BodyState.Stage2NeutralObservationFramesR = 0;
			BodyState.bHasSmoothedFkRootGroundOffset = false;
			BodyState.SmoothedFkRootGroundOffsetComp = FVector::ZeroVector;
			LeftArmState.bHasSmoothedArmIK = false;
			RightArmState.bHasSmoothedArmIK = false;
			LeftLegState.bHasSmoothedLegPlane = false;
			RightLegState.bHasSmoothedLegPlane = false;
			ResetFootPlantState();
			ResetPoseYawAlignRuntimeState(RuntimeStateKey);
			ResetQuestWristRuntimeState(RuntimeStateKey);
			ResetDerivedSignalRuntimeState(RuntimeStateKey);
			ResetRotationSmoothing();
			BodyState.ResetDerivedSignalReferences();
			UE_LOG(
				LogMediaPipePose,
				Warning,
				TEXT("MediaPipe pose timestamp rewind: reset solver continuity actor=%s currentUs=%lld previousUs=%lld runtimeKey=%u."),
				*TargetActorName.ToString(),
				ActivePoseTimestampUs,
				LastPoseTimestampUs,
				RuntimeStateKey);
			for (uint8& B : EverMeasured)
			{
				B = 0;
			}
		}
		bHasLastPoseTimestamp = true;
		LastPoseTimestampUs = ActivePoseTimestampUs;
	}
	else
	{
		bHasLastPoseTimestamp = false;
	}

	FCSPose<FCompactPose> CSPose;
	CSPose.InitPose(Output.Pose);

	LeftLegState.bCurrentSourceFootGrounded = false;
	RightLegState.bCurrentSourceFootGrounded = false;

	const bool bBodyFusionFullPoseInput = ShouldUseBodyFusionPoseForEvaluation();
	const bool bBodyFusionPoseWritten = DriveBodyFusionPoseCS(CSPose, DeltaSeconds);
	const bool bReplayBodyPoseDirectOverride =
		FMediaPipeTrackingFusionDatasetReplayRuntime::Get().IsActive() && bHasPoseFrame;
	if ((!bBodyFusionPoseWritten || bReplayBodyPoseDirectOverride) && bHasPoseFrame)
	{
		DrivePelvisTranslationCS(CSPose, DeltaSeconds);
		DriveSpineCS(CSPose, DeltaSeconds);
	}
	if (bHasPoseFrame)
	{
		DriveLegCS(CSPose, true, DeltaSeconds);
		DriveLegCS(CSPose, false, DeltaSeconds);
	}
	if ((!bBodyFusionPoseWritten || bReplayBodyPoseDirectOverride) && bHasPoseFrame)
	{
		UpdateFkRootGroundingCS(CSPose, DeltaSeconds);
	}
	if (!bBodyFusionFullPoseInput)
	{
		DriveBodyFusionStage2ShoulderClavicleHintCS(CSPose, DeltaSeconds);
	}
	DriveArmCS(CSPose, true, DeltaSeconds);
	DriveArmCS(CSPose, false, DeltaSeconds);
	DriveArmTwistBonesCS(CSPose, DeltaSeconds);

	FCSPose<FCompactPose>::ConvertComponentPosesToLocalPosesSafe(CSPose, Output.Pose);

	if ((!bBodyFusionPoseWritten || bReplayBodyPoseDirectOverride) && bHasPoseFrame && !BodyState.SmoothedFkRootGroundOffsetComp.IsNearlyZero())
	{
		FBoneReference RootToTranslate = Root;
		if (!RootToTranslate.IsValidToEvaluate())
		{
			RootToTranslate = Pelvis;
		}

		if (RootToTranslate.IsValidToEvaluate())
		{
			const FCompactPoseBoneIndex RootIdx = RootToTranslate.CachedCompactPoseIndex;
			FTransform RootLocal = Output.Pose[RootIdx];
			RootLocal.SetTranslation(RootLocal.GetTranslation() + BodyState.SmoothedFkRootGroundOffsetComp);
			Output.Pose[RootIdx] = RootLocal;
		}
	}
}

void FAnimNode_MediaPipePoseDriven::GatherDebugData(FNodeDebugData& DebugData)
{
	FString DebugLine = DebugData.GetNodeName(this);
	DebugLine += FString::Printf(TEXT(" (Pose=%s, Ref=%s, Source=%s)"),
		bHasPoseFrame ? TEXT("yes") : TEXT("no"),
		bHasReferencePose ? TEXT("yes") : TEXT("no"),
		*GetNameSafe(SourceActor));
	DebugData.AddDebugItem(DebugLine);
}

void FMediaPipePoseDrivenAnimInstanceProxy::Initialize(UAnimInstance* InAnimInstance)
{
	FAnimInstanceProxy::Initialize(InAnimInstance);

	FAnimationInitializeContext InitContext(this);
	PoseNode.Initialize_AnyThread(InitContext);
}

void FMediaPipePoseDrivenAnimInstanceProxy::CacheBones()
{
	FAnimInstanceProxy::CacheBones();

	FAnimationCacheBonesContext CacheContext(this);
	PoseNode.CacheBones_AnyThread(CacheContext);
}

void FMediaPipePoseDrivenAnimInstanceProxy::PreUpdate(UAnimInstance* InAnimInstance, float DeltaSeconds)
{
	Super::PreUpdate(InAnimInstance, DeltaSeconds);
	if (PoseNode.HasPreUpdate())
	{
		PoseNode.PreUpdate(InAnimInstance);
	}
}

bool FMediaPipePoseDrivenAnimInstanceProxy::Evaluate(FPoseContext& Output)
{
	PoseNode.Evaluate_AnyThread(Output);
	return true;
}

void FMediaPipePoseDrivenAnimInstanceProxy::UpdateAnimationNode(const FAnimationUpdateContext& InContext)
{
	UpdateCounter.Increment();
	PoseNode.Update_AnyThread(InContext);
}

FAnimInstanceProxy* UMediaPipePoseDrivenAnimInstance::CreateAnimInstanceProxy()
{
	return new FMediaPipePoseDrivenAnimInstanceProxy(this);
}

void UMediaPipePoseDrivenAnimInstance::DestroyAnimInstanceProxy(FAnimInstanceProxy* InProxy)
{
	Super::DestroyAnimInstanceProxy(InProxy);
}

void UMediaPipePoseDrivenAnimInstance::SetSourceActor(AActor* InSource)
{
	FMediaPipePoseDrivenAnimInstanceProxy& Proxy = GetProxyOnGameThread<FMediaPipePoseDrivenAnimInstanceProxy>();
	if (Proxy.PoseNode.SourceActor != InSource)
	{
		Proxy.PoseNode.SourceActor = InSource;
		Proxy.PoseNode.bResetPoseStateNextUpdate = true;
		Proxy.PoseNode.bResetDerivedSignalReferencesNextUpdate = true;
	}
}

void UMediaPipePoseDrivenAnimInstance::SetEmbodiedFusionComponent(UEmbodiedFusionComponent* InFusionComponent)
{
	FMediaPipePoseDrivenAnimInstanceProxy& Proxy = GetProxyOnGameThread<FMediaPipePoseDrivenAnimInstanceProxy>();
	if (Proxy.PoseNode.EmbodiedFusionComponent != InFusionComponent)
	{
		Proxy.PoseNode.EmbodiedFusionComponent = InFusionComponent;
		Proxy.PoseNode.BodyFusionFrame.ResetTransient();
		if (InFusionComponent)
		{
			InFusionComponent->ResetFusionState();
		}
	}
}

void UMediaPipePoseDrivenAnimInstance::ResetRetargetState()
{
	FMediaPipePoseDrivenAnimInstanceProxy& Proxy = GetProxyOnGameThread<FMediaPipePoseDrivenAnimInstanceProxy>();
	Proxy.PoseNode.bResetPoseStateNextUpdate = true;
	Proxy.PoseNode.bResetDerivedSignalReferencesNextUpdate = true;
}

bool UMediaPipePoseDrivenAnimInstance::GetLatestSignalSnapshot(FMediaPipePoseDrivenSignalSnapshot& OutSnapshot)
{
	FMediaPipePoseDrivenAnimInstanceProxy& Proxy = GetProxyOnGameThread<FMediaPipePoseDrivenAnimInstanceProxy>();
	OutSnapshot = Proxy.PoseNode.LatestSignalSnapshot;
	return OutSnapshot.bValid;
}

bool UMediaPipePoseDrivenAnimInstance::GetLatestSignalSnapshotForComponent(
	const USkeletalMeshComponent* InComponent,
	FMediaPipePoseDrivenSignalSnapshot& OutSnapshot)
{
	if (!InComponent)
	{
		return false;
	}
	const uint32 RuntimeKey = InComponent->GetUniqueID();
	FScopeLock Lock(&GMediaPipePoseDrivenSignalSnapshotsCritical);
	if (const FMediaPipePoseDrivenSignalSnapshot* Snapshot = GMediaPipePoseDrivenSignalSnapshotsByRuntimeKey.Find(RuntimeKey))
	{
		OutSnapshot = *Snapshot;
		return OutSnapshot.bValid;
	}
	return false;
}

void UMediaPipePoseDrivenAnimInstance::PublishLatestSignalSnapshotForRuntimeKey(
	const uint32 RuntimeKey,
	const FMediaPipePoseDrivenSignalSnapshot& Snapshot)
{
	if (RuntimeKey == 0 || !Snapshot.bValid)
	{
		return;
	}
	FScopeLock Lock(&GMediaPipePoseDrivenSignalSnapshotsCritical);
	GMediaPipePoseDrivenSignalSnapshotsByRuntimeKey.FindOrAdd(RuntimeKey) = Snapshot;
}

void UMediaPipePoseDrivenAnimInstance::ApplyRetargetQualitySettings()
{
	FMediaPipePoseDrivenAnimInstanceProxy& Proxy = GetProxyOnGameThread<FMediaPipePoseDrivenAnimInstanceProxy>();
	FAnimNode_MediaPipePoseDriven& PoseNode = Proxy.PoseNode;

	const bool bNewDriveClavicles = CVarMediaPipeDriveClavicles.GetValueOnGameThread() != 0;
	const bool bNewDriveSpine = CVarMediaPipeDriveSpine.GetValueOnGameThread() != 0;
	const bool bNewDrivePelvisTranslation = CVarMediaPipeDrivePelvisTranslation.GetValueOnGameThread() != 0;
	const bool bNewDriveLegs = CVarMediaPipeDriveLegs.GetValueOnGameThread() != 0;
	const bool bNewUseArmIK = CVarMediaPipeUseArmIK.GetValueOnGameThread() != 0;
	const bool bNewUseLegIK = CVarMediaPipeUseLegIK.GetValueOnGameThread() != 0;
	const bool bNewUseLegIKFootPlant = CVarMediaPipeUseLegIKFootPlant.GetValueOnGameThread() != 0;
	const bool bNewUseFkRootGrounding = CVarMediaPipeUseFkRootGrounding.GetValueOnGameThread() != 0;
	const bool bNewDriveHandRotation = CVarMediaPipeDriveHandRotation.GetValueOnGameThread() != 0;
	const bool bNewUseQuestHandTracking = CVarQuestHandTracking.GetValueOnGameThread() != 0;
	const bool bNewDriveQuestFingerBones = CVarQuestHandDriveFingerBones.GetValueOnGameThread() != 0;
	const float NewQuestHandRotationBlend = FMath::Clamp(CVarQuestHandRotationBlend.GetValueOnGameThread(), 0.0f, 1.0f);
	const float NewQuestFingerRotationHalfLifeSeconds = FMath::Max(0.0f, CVarQuestFingerRotationHalfLife.GetValueOnGameThread());
	const float NewArmTargetHalfLifeSeconds = FMath::Max(0.0f, CVarMediaPipeArmTargetHalfLife.GetValueOnGameThread());
	const float NewArmRotationHalfLifeSeconds = FMath::Max(0.0f, CVarMediaPipeArmRotationHalfLife.GetValueOnGameThread());
	const float NewSpineRotationHalfLifeSeconds = FMath::Max(0.0f, CVarMediaPipeSpineRotationHalfLife.GetValueOnGameThread());
	const float NewHeadRotationHalfLifeSeconds = FMath::Max(0.0f, CVarMediaPipeHeadRotationHalfLife.GetValueOnGameThread());
	const float NewHeadTwistWeight = FMath::Clamp(CVarMediaPipeHeadTwistWeight.GetValueOnGameThread(), 0.0f, 1.0f);
	const float NewHeadFaceBlend = FMath::Clamp(CVarMediaPipeHeadFaceBlend.GetValueOnGameThread(), 0.0f, 1.0f);
	const float NewHeadPitchScale = FMath::Clamp(CVarMediaPipeHeadPitchScale.GetValueOnGameThread(), 0.0f, 3.0f);
	const float NewHeadRotationMaxStepDegrees = FMath::Max(0.0f, CVarMediaPipeHeadRotationMaxStepDegrees.GetValueOnGameThread());
	const float NewHeadRotationMaxSpeedDegreesPerSecond = FMath::Max(0.0f, CVarMediaPipeHeadRotationMaxSpeedDegreesPerSecond.GetValueOnGameThread());

	const bool bChanged =
		PoseNode.bDriveClavicles != bNewDriveClavicles ||
		PoseNode.bDriveSpine != bNewDriveSpine ||
		PoseNode.bDrivePelvisTranslation != bNewDrivePelvisTranslation ||
		PoseNode.bDriveLegs != bNewDriveLegs ||
		PoseNode.bUseArmIK != bNewUseArmIK ||
		PoseNode.bUseLegIK != bNewUseLegIK ||
		PoseNode.bUseLegIKFootPlant != bNewUseLegIKFootPlant ||
		PoseNode.bUseFkRootGrounding != bNewUseFkRootGrounding ||
		PoseNode.bDriveHandRotation != bNewDriveHandRotation ||
		PoseNode.bUseQuestHandTracking != bNewUseQuestHandTracking ||
		PoseNode.bDriveQuestFingerBones != bNewDriveQuestFingerBones ||
		!FMath::IsNearlyEqual(PoseNode.QuestHandRotationBlend, NewQuestHandRotationBlend, 0.001f) ||
		!FMath::IsNearlyEqual(PoseNode.QuestFingerRotationHalfLifeSeconds, NewQuestFingerRotationHalfLifeSeconds, 0.001f) ||
		!FMath::IsNearlyEqual(PoseNode.ArmIKTargetHalfLifeSeconds, NewArmTargetHalfLifeSeconds, 0.001f) ||
		!FMath::IsNearlyEqual(PoseNode.ArmIKRotationHalfLifeSeconds, NewArmRotationHalfLifeSeconds, 0.001f) ||
		!FMath::IsNearlyEqual(PoseNode.SpineRotationHalfLifeSeconds, NewSpineRotationHalfLifeSeconds, 0.001f) ||
		!FMath::IsNearlyEqual(PoseNode.HeadRotationHalfLifeSeconds, NewHeadRotationHalfLifeSeconds, 0.001f) ||
		!FMath::IsNearlyEqual(PoseNode.HeadTwistWeight, NewHeadTwistWeight, 0.001f) ||
		!FMath::IsNearlyEqual(PoseNode.HeadFaceBlend, NewHeadFaceBlend, 0.001f) ||
		!FMath::IsNearlyEqual(PoseNode.HeadPitchScale, NewHeadPitchScale, 0.001f) ||
		!FMath::IsNearlyEqual(PoseNode.HeadRotationMaxStepDegrees, NewHeadRotationMaxStepDegrees, 0.001f) ||
		!FMath::IsNearlyEqual(PoseNode.HeadRotationMaxSpeedDegreesPerSecond, NewHeadRotationMaxSpeedDegreesPerSecond, 0.001f);

	PoseNode.bDriveClavicles = bNewDriveClavicles;
	PoseNode.bDriveSpine = bNewDriveSpine;
	PoseNode.bDrivePelvisTranslation = bNewDrivePelvisTranslation;
	PoseNode.bDriveLegs = bNewDriveLegs;
	PoseNode.bUseArmIK = bNewUseArmIK;
	PoseNode.bUseLegIK = bNewUseLegIK;
	PoseNode.bUseLegIKFootPlant = bNewUseLegIKFootPlant;
	PoseNode.bUseFkRootGrounding = bNewUseFkRootGrounding;
	PoseNode.bDriveHandRotation = bNewDriveHandRotation;
	PoseNode.bUseQuestHandTracking = bNewUseQuestHandTracking;
	PoseNode.bDriveQuestFingerBones = bNewDriveQuestFingerBones;
	PoseNode.QuestHandRotationBlend = NewQuestHandRotationBlend;
	PoseNode.QuestFingerRotationHalfLifeSeconds = NewQuestFingerRotationHalfLifeSeconds;
	PoseNode.ArmIKTargetHalfLifeSeconds = NewArmTargetHalfLifeSeconds;
	PoseNode.ArmIKRotationHalfLifeSeconds = NewArmRotationHalfLifeSeconds;
	PoseNode.SpineRotationHalfLifeSeconds = NewSpineRotationHalfLifeSeconds;
	PoseNode.HeadRotationHalfLifeSeconds = NewHeadRotationHalfLifeSeconds;
	PoseNode.HeadTwistWeight = NewHeadTwistWeight;
	PoseNode.HeadFaceBlend = NewHeadFaceBlend;
	PoseNode.HeadPitchScale = NewHeadPitchScale;
	PoseNode.HeadRotationMaxStepDegrees = NewHeadRotationMaxStepDegrees;
	PoseNode.HeadRotationMaxSpeedDegreesPerSecond = NewHeadRotationMaxSpeedDegreesPerSecond;

	if (bChanged)
	{
		PoseNode.bResetPoseStateNextUpdate = true;
	}
}

void UMediaPipePoseDrivenAnimInstance::SetDriveClavicles(bool bInDriveClavicles)
{
	FMediaPipePoseDrivenAnimInstanceProxy& Proxy = GetProxyOnGameThread<FMediaPipePoseDrivenAnimInstanceProxy>();
	if (Proxy.PoseNode.bDriveClavicles != bInDriveClavicles)
	{
		Proxy.PoseNode.bDriveClavicles = bInDriveClavicles;
		Proxy.PoseNode.bResetPoseStateNextUpdate = true;
	}
}

void UMediaPipePoseDrivenAnimInstance::SetDriveSpine(bool bInDriveSpine)
{
	FMediaPipePoseDrivenAnimInstanceProxy& Proxy = GetProxyOnGameThread<FMediaPipePoseDrivenAnimInstanceProxy>();
	if (Proxy.PoseNode.bDriveSpine != bInDriveSpine)
	{
		Proxy.PoseNode.bDriveSpine = bInDriveSpine;
		Proxy.PoseNode.bResetPoseStateNextUpdate = true;
	}
}

void UMediaPipePoseDrivenAnimInstance::SetDrivePelvisTranslation(bool bInDrivePelvisTranslation)
{
	FMediaPipePoseDrivenAnimInstanceProxy& Proxy = GetProxyOnGameThread<FMediaPipePoseDrivenAnimInstanceProxy>();
	if (Proxy.PoseNode.bDrivePelvisTranslation != bInDrivePelvisTranslation)
	{
		Proxy.PoseNode.bDrivePelvisTranslation = bInDrivePelvisTranslation;
		Proxy.PoseNode.bResetPoseStateNextUpdate = true;
	}
}

void UMediaPipePoseDrivenAnimInstance::SetDriveLegs(bool bInDriveLegs)
{
	FMediaPipePoseDrivenAnimInstanceProxy& Proxy = GetProxyOnGameThread<FMediaPipePoseDrivenAnimInstanceProxy>();
	if (Proxy.PoseNode.bDriveLegs != bInDriveLegs)
	{
		Proxy.PoseNode.bDriveLegs = bInDriveLegs;
		Proxy.PoseNode.bResetPoseStateNextUpdate = true;
	}
}

void UMediaPipePoseDrivenAnimInstance::SetUseArmIK(bool bInUseArmIK)
{
	FMediaPipePoseDrivenAnimInstanceProxy& Proxy = GetProxyOnGameThread<FMediaPipePoseDrivenAnimInstanceProxy>();
	if (Proxy.PoseNode.bUseArmIK != bInUseArmIK)
	{
		Proxy.PoseNode.bUseArmIK = bInUseArmIK;
		Proxy.PoseNode.bResetPoseStateNextUpdate = true;
	}
}

void UMediaPipePoseDrivenAnimInstance::SetUseLegIK(bool bInUseLegIK)
{
	FMediaPipePoseDrivenAnimInstanceProxy& Proxy = GetProxyOnGameThread<FMediaPipePoseDrivenAnimInstanceProxy>();
	if (Proxy.PoseNode.bUseLegIK != bInUseLegIK)
	{
		Proxy.PoseNode.bUseLegIK = bInUseLegIK;
		Proxy.PoseNode.bResetPoseStateNextUpdate = true;
	}
}

void UMediaPipePoseDrivenAnimInstance::SetUseFkRootGrounding(bool bInUseFkRootGrounding)
{
	FMediaPipePoseDrivenAnimInstanceProxy& Proxy = GetProxyOnGameThread<FMediaPipePoseDrivenAnimInstanceProxy>();
	if (Proxy.PoseNode.bUseFkRootGrounding != bInUseFkRootGrounding)
	{
		Proxy.PoseNode.bUseFkRootGrounding = bInUseFkRootGrounding;
		Proxy.PoseNode.bResetPoseStateNextUpdate = true;
	}
}

void UMediaPipePoseDrivenAnimInstance::SetDriveHandRotation(bool bInDriveHandRotation)
{
	FMediaPipePoseDrivenAnimInstanceProxy& Proxy = GetProxyOnGameThread<FMediaPipePoseDrivenAnimInstanceProxy>();
	if (Proxy.PoseNode.bDriveHandRotation != bInDriveHandRotation)
	{
		Proxy.PoseNode.bDriveHandRotation = bInDriveHandRotation;
		Proxy.PoseNode.bResetPoseStateNextUpdate = true;
	}
}

void UMediaPipePoseDrivenAnimInstance::SetUseQuestHandTracking(bool bInUseQuestHandTracking)
{
	FMediaPipePoseDrivenAnimInstanceProxy& Proxy = GetProxyOnGameThread<FMediaPipePoseDrivenAnimInstanceProxy>();
	if (Proxy.PoseNode.bUseQuestHandTracking != bInUseQuestHandTracking)
	{
		Proxy.PoseNode.bUseQuestHandTracking = bInUseQuestHandTracking;
		Proxy.PoseNode.bResetPoseStateNextUpdate = true;
	}
}

void UMediaPipePoseDrivenAnimInstance::SetDriveQuestFingerBones(bool bInDriveQuestFingerBones)
{
	FMediaPipePoseDrivenAnimInstanceProxy& Proxy = GetProxyOnGameThread<FMediaPipePoseDrivenAnimInstanceProxy>();
	if (Proxy.PoseNode.bDriveQuestFingerBones != bInDriveQuestFingerBones)
	{
		Proxy.PoseNode.bDriveQuestFingerBones = bInDriveQuestFingerBones;
		Proxy.PoseNode.bResetPoseStateNextUpdate = true;
	}
}
