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

bool FAnimNode_MediaPipePoseDriven::TryGetTorsoBasisWorld(FVector& OutHipRight, FVector& OutShoulderRight, FVector& OutUp, FVector& OutForward)
{
	FVector LShoulder = FVector::ZeroVector;
	FVector RShoulder = FVector::ZeroVector;
	FVector LHip = FVector::ZeroVector;
	FVector RHip = FVector::ZeroVector;
	FVector Nose = FVector::ZeroVector;
	if (!TryGetLmWorld((int32)EMediaPipePoseLandmark::LeftShoulder, LShoulder) ||
		!TryGetLmWorld((int32)EMediaPipePoseLandmark::RightShoulder, RShoulder) ||
		!TryGetLmWorld((int32)EMediaPipePoseLandmark::LeftHip, LHip) ||
		!TryGetLmWorld((int32)EMediaPipePoseLandmark::RightHip, RHip))
	{
		return false;
	}
	const bool bHasNose = TryGetLmWorld((int32)EMediaPipePoseLandmark::Nose, Nose);

	const FVector ShoulderMid = (LShoulder + RShoulder) * 0.5f;
	const FVector HipMid = (LHip + RHip) * 0.5f;

	FVector HipRight = (RHip - LHip).GetSafeNormal();
	if (HipRight.IsNearlyZero())
	{
		return false;
	}

	FVector ShoulderRight = (RShoulder - LShoulder).GetSafeNormal();
	if (ShoulderRight.IsNearlyZero())
	{
		ShoulderRight = HipRight;
	}

	FVector ObservedUp = (ShoulderMid - HipMid).GetSafeNormal();
	if (ObservedUp.IsNearlyZero())
	{
		return false;
	}

	if (BodyState.bHasStableTorsoUpWorld)
	{
		ObservedUp = LockVectorToHemisphere(ObservedUp, BodyState.StableTorsoUpWorld);
	}

	HipRight = (HipRight - FVector::DotProduct(HipRight, ObservedUp) * ObservedUp).GetSafeNormal();
	ShoulderRight = (ShoulderRight - FVector::DotProduct(ShoulderRight, ObservedUp) * ObservedUp).GetSafeNormal();

	FVector Forward = FVector::CrossProduct(HipRight, ObservedUp).GetSafeNormal();
	if (Forward.IsNearlyZero() && !ShoulderRight.IsNearlyZero())
	{
		Forward = FVector::CrossProduct(ShoulderRight, ObservedUp).GetSafeNormal();
	}
	if (Forward.IsNearlyZero())
	{
		Forward = BodyState.bHasStableTorsoForwardWorld ? BodyState.StableTorsoForwardWorld : FVector::ZeroVector;
	}
	if (Forward.IsNearlyZero())
	{
		return false;
	}

	if (BodyState.bHasStableTorsoForwardWorld)
	{
		Forward = LockVectorToHemisphere(Forward, BodyState.StableTorsoForwardWorld);
	}
	else
	{
		FVector InitialForwardReference = FVector::ZeroVector;
		if (bHasNose)
		{
			InitialForwardReference = (Nose - ShoulderMid);
			InitialForwardReference = (InitialForwardReference - FVector::DotProduct(InitialForwardReference, ObservedUp) * ObservedUp).GetSafeNormal();
		}
		if (InitialForwardReference.IsNearlyZero())
		{
			InitialForwardReference = PoseToWorldTransform.GetUnitAxis(EAxis::X);
		}
		Forward = LockVectorToHemisphere(Forward, InitialForwardReference);
	}

	const FVector RawObservedUp = ObservedUp;
	const FVector RawForward = Forward;
	const float UprightBlend = FMath::Clamp(CVarMediaPipeTorsoUprightBlend.GetValueOnAnyThread(), 0.0f, 1.0f);
	const float MaxTiltDegrees = FMath::Clamp(CVarMediaPipeTorsoMaxTiltDegrees.GetValueOnAnyThread(), 0.0f, 89.0f);
	if (UprightBlend > KINDA_SMALL_NUMBER || MaxTiltDegrees < 89.0f)
	{
		const FVector WorldUp = FVector::UpVector;
		FVector ConstrainedUp = RawObservedUp;
		if (UprightBlend > KINDA_SMALL_NUMBER)
		{
			ConstrainedUp = LerpNormalized(RawObservedUp, WorldUp, UprightBlend);
		}

		if (ConstrainedUp.IsNearlyZero())
		{
			ConstrainedUp = WorldUp;
		}
		ConstrainedUp = LockVectorToHemisphere(ConstrainedUp, WorldUp);

		const float MinUpDot = FMath::Cos(FMath::DegreesToRadians(MaxTiltDegrees));
		const float CurrentUpDot = FMath::Clamp(FVector::DotProduct(ConstrainedUp, WorldUp), -1.0f, 1.0f);
		if (CurrentUpDot < MinUpDot)
		{
			FVector Lateral = ConstrainedUp - CurrentUpDot * WorldUp;
			if (Lateral.Normalize())
			{
				const float MaxTiltSin = FMath::Sin(FMath::DegreesToRadians(MaxTiltDegrees));
				ConstrainedUp = (WorldUp * MinUpDot + Lateral * MaxTiltSin).GetSafeNormal();
			}
			else
			{
				ConstrainedUp = WorldUp;
			}
		}

		ObservedUp = ConstrainedUp;
		HipRight = (RHip - LHip).GetSafeNormal();
		HipRight = (HipRight - FVector::DotProduct(HipRight, ObservedUp) * ObservedUp).GetSafeNormal();
		ShoulderRight = (RShoulder - LShoulder).GetSafeNormal();
		ShoulderRight = (ShoulderRight - FVector::DotProduct(ShoulderRight, ObservedUp) * ObservedUp).GetSafeNormal();
		if (HipRight.IsNearlyZero() && !ShoulderRight.IsNearlyZero())
		{
			HipRight = ShoulderRight;
		}
		if (HipRight.IsNearlyZero())
		{
			return false;
		}

		Forward = FVector::CrossProduct(HipRight, ObservedUp).GetSafeNormal();
		if (Forward.IsNearlyZero() && !ShoulderRight.IsNearlyZero())
		{
			Forward = FVector::CrossProduct(ShoulderRight, ObservedUp).GetSafeNormal();
		}
		if (Forward.IsNearlyZero())
		{
			Forward = (RawForward - FVector::DotProduct(RawForward, ObservedUp) * ObservedUp).GetSafeNormal();
		}
		if (Forward.IsNearlyZero())
		{
			return false;
		}
		Forward = LockVectorToHemisphere(Forward, RawForward);
	}

	const bool bUseActorForward = CVarMediaPipeTorsoUseActorForward.GetValueOnAnyThread() != 0;
	if (bUseActorForward)
	{
		FVector ActorForward = GetTargetForwardWorld();
		ActorForward = (ActorForward - FVector::DotProduct(ActorForward, ObservedUp) * ObservedUp).GetSafeNormal();
		if (!ActorForward.IsNearlyZero())
		{
			Forward = ActorForward;
		}
	}

	if (CVarMediaPipeTorsoDebug.GetValueOnAnyThread() != 0)
	{
		const double NowSeconds = FPlatformTime::Seconds();
		if (FMediaPipePoseDiagnosticReporter::ShouldEmitThrottled(NowSeconds, 1.0, DiagnosticsState.LastTorsoDiagnosticLogTimeSeconds))
		{
			FMediaPipeTorsoBasisLogInput TorsoLogInput;
			TorsoLogInput.TargetActorName = TargetActorName;
			TorsoLogInput.RawObservedUp = RawObservedUp;
			TorsoLogInput.ObservedUp = ObservedUp;
			TorsoLogInput.Forward = Forward;
			TorsoLogInput.bUseActorForward = bUseActorForward;
			TorsoLogInput.UprightBlend = UprightBlend;
			TorsoLogInput.MaxTiltDegrees = MaxTiltDegrees;
			FMediaPipeBodyDiagnostics::EmitTorsoBasisLog(TorsoLogInput);
		}
	}

	BodyState.StableTorsoForwardWorld = Forward;
	BodyState.bHasStableTorsoForwardWorld = true;

	FVector Up = (ObservedUp - FVector::DotProduct(ObservedUp, Forward) * Forward).GetSafeNormal();
	if (Up.IsNearlyZero() && BodyState.bHasStableTorsoUpWorld)
	{
		Up = (BodyState.StableTorsoUpWorld - FVector::DotProduct(BodyState.StableTorsoUpWorld, Forward) * Forward).GetSafeNormal();
	}
	if (Up.IsNearlyZero())
	{
		return false;
	}
	Up = BodyState.bHasStableTorsoUpWorld ? LockVectorToHemisphere(Up, BodyState.StableTorsoUpWorld) : Up;
	BodyState.StableTorsoUpWorld = Up;
	BodyState.bHasStableTorsoUpWorld = true;

	HipRight = FVector::CrossProduct(Up, Forward).GetSafeNormal();
	if (HipRight.IsNearlyZero())
	{
		return false;
	}
	ShoulderRight = (ShoulderRight - FVector::DotProduct(ShoulderRight, Up) * Up).GetSafeNormal();
	ShoulderRight = (ShoulderRight - FVector::DotProduct(ShoulderRight, Forward) * Forward).GetSafeNormal();
	if (!ShoulderRight.IsNearlyZero())
	{
		ShoulderRight = LockVectorToHemisphere(ShoulderRight, HipRight);
	}

	OutHipRight = HipRight;
	OutShoulderRight = ShoulderRight.IsNearlyZero() ? HipRight : ShoulderRight;
	OutUp = Up;
	OutForward = Forward;
	return true;
}
