#pragma once

// Internal shared helpers for the FAnimNode_MediaPipePoseDriven solver translation units.
// Textual include for the solver .cpp files only (main anim instance TU + per-solver TUs
// split out of the former 14k-line single TU). Everything here has internal linkage; each
// including TU gets its own copy, matching the pre-split anonymous-namespace semantics.
// Not part of the module API - do not include outside PoseDriven solver TUs.

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

using namespace MediaPipeRuntimeCVars;
using namespace MediaPipeBodySolverMath;
using namespace MediaPipeQuestFingerSolver;

// Cross-frame keyed runtime state: the state TYPES and their store ACCESSORS must have external
// linkage with exactly one process-wide definition (accessors are defined in
// MediaPipePoseDrivenAnimInstance.cpp behind a lock). They used to live inside this header's
// anonymous namespace, which hands every including TU its own copy of both the type and the
// TMap store: under ADAPTIVE UNITY builds a recently-edited solver TU compiles alone and
// silently FORKS the "shared" state, and the unsynchronized TMap raced across parallel
// anim-evaluation worker threads (2026-07-03 live crash: EXCEPTION_ACCESS_VIOLATION inside
// GetQuestWristRuntimeState during a concurrent FindOrAdd rehash).
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

// Single process-wide, lock-guarded stores (defined once in MediaPipePoseDrivenAnimInstance.cpp).
// Returned references stay valid across map growth because entries are heap-allocated and never
// removed for the lifetime of the process.
FPoseYawAlignRuntimeState& GetPoseYawAlignRuntimeStateForKey(uint32 Key);
FQuestWristRuntimeState& GetQuestWristRuntimeStateForKey(uint32 Key);
FMediaPipeFootContactRuntimeState& GetFootContactRuntimeStateForKey(uint32 Key);

namespace
{
	using namespace MediaPipeRuntimeCVars;
	using namespace MediaPipeBodySolverMath;
	using namespace MediaPipeQuestFingerSolver;

	FPoseYawAlignRuntimeState& GetPoseYawAlignRuntimeState(uint32 Key)
	{
		return GetPoseYawAlignRuntimeStateForKey(Key);
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

	FQuestWristRuntimeState& GetQuestWristRuntimeState(uint32 Key)
	{
		return GetQuestWristRuntimeStateForKey(Key);
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

	FMediaPipeFootContactRuntimeState& GetFootContactRuntimeState(uint32 Key)
	{
		return GetFootContactRuntimeStateForKey(Key);
	}

	void ResetFootContactRuntimeState(uint32 Key)
	{
		GetFootContactRuntimeState(Key).Reset();
	}

	void ResetFootContactRuntimeState(const UObject* KeyObject)
	{
		const uint32 Key = IsValid(KeyObject) ? KeyObject->GetUniqueID() : 0u;
		ResetFootContactRuntimeState(Key);
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

} // close anonymous namespace: FDerivedSignalRuntimeState and its store accessor need external
// linkage (see the keyed-runtime-state comment above FPoseYawAlignRuntimeState).

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

FDerivedSignalRuntimeState& GetDerivedSignalRuntimeStateForKey(uint32 Key);

namespace
{
	using namespace MediaPipeRuntimeCVars;
	using namespace MediaPipeBodySolverMath;
	using namespace MediaPipeQuestFingerSolver;

	FDerivedSignalRuntimeState& GetDerivedSignalRuntimeState(uint32 Key)
	{
		return GetDerivedSignalRuntimeStateForKey(Key);
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
	constexpr int32 HandLm_ThumbMcp = 2;
	constexpr int32 HandLm_IndexMcp = 5;
	constexpr int32 HandLm_MiddleMcp = 9;
	constexpr int32 HandLm_PinkyMcp = 17;

	bool IsFiniteVector(const FVector& Vector)
	{
		return FMath::IsFinite(Vector.X) && FMath::IsFinite(Vector.Y) && FMath::IsFinite(Vector.Z);
	}


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

