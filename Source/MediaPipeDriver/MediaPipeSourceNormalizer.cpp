#include "MediaPipeSourceNormalizer.h"

namespace
{
FQuat NormalizeQuatSafe(const FQuat& Rotation)
{
	if (Rotation.ContainsNaN() || Rotation.SizeSquared() <= KINDA_SMALL_NUMBER)
	{
		return Rotation;
	}

	FQuat Normalized = Rotation;
	Normalized.Normalize();
	return Normalized;
}

FVector NormalizeUpSafe(const FVector& TrackingUpWorld)
{
	if (TrackingUpWorld.ContainsNaN() || TrackingUpWorld.IsNearlyZero())
	{
		return FVector::UpVector;
	}
	return TrackingUpWorld.GetSafeNormal();
}
}

FMediaPipeTrackingSourceFrame FMediaPipeSourceNormalizer::Normalize(
	const FMediaPipeTrackingSourceFrame& SourceFrame,
	const FMediaPipeBodyFusionFreshnessThresholds& Thresholds)
{
	FMediaPipeTrackingSourceFrame NormalizedFrame = SourceFrame;
	NormalizeInPlace(NormalizedFrame, Thresholds);
	return NormalizedFrame;
}

void FMediaPipeSourceNormalizer::NormalizeInPlace(
	FMediaPipeTrackingSourceFrame& InOutSourceFrame,
	const FMediaPipeBodyFusionFreshnessThresholds& Thresholds)
{
	InOutSourceFrame.HmdRotationWorld = NormalizeQuatSafe(InOutSourceFrame.HmdRotationWorld);
	InOutSourceFrame.TrackingUpWorld = NormalizeUpSafe(InOutSourceFrame.TrackingUpWorld);
	InOutSourceFrame.UpdateFreshness(Thresholds);
}
