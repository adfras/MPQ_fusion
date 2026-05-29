#include "MediaPipeQuestHmdTrackingSource.h"

#include "Engine/Engine.h"
#include "IXRTrackingSystem.h"

bool FMediaPipeQuestHmdTrackingSource::TryReadWorldPose(
	FMediaPipeQuestHmdPoseSnapshot& OutSnapshot)
{
	OutSnapshot = FMediaPipeQuestHmdPoseSnapshot();

	if (!IsInGameThread() || !GEngine || !GEngine->XRSystem.IsValid())
	{
		return false;
	}

	FQuat HmdRotationTracking = FQuat::Identity;
	FVector HmdLocationTracking = FVector::ZeroVector;
	if (!GEngine->XRSystem->GetCurrentPose(IXRTrackingSystem::HMDDeviceId, HmdRotationTracking, HmdLocationTracking))
	{
		return false;
	}

	const FTransform TrackingToWorld = GEngine->XRSystem->GetTrackingToWorldTransform();
	OutSnapshot.LocationWorld = TrackingToWorld.TransformPosition(HmdLocationTracking);
	OutSnapshot.RotationWorld = TrackingToWorld.TransformRotation(HmdRotationTracking).GetNormalized();

	const FVector UpWorld = TrackingToWorld.TransformVectorNoScale(FVector::UpVector).GetSafeNormal();
	OutSnapshot.TrackingUpWorld = UpWorld.IsNearlyZero() ? FVector::UpVector : UpWorld;
	OutSnapshot.bHasPose = true;
	return true;
}
