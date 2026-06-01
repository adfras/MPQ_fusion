#include "MediaPipeTrackingSourceTypes.h"

namespace
{
bool IsFiniteVector(const FVector& Value)
{
	return !Value.ContainsNaN();
}

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

bool FMediaPipeBodyFusionSourceStatus::IsFresh() const
{
	return State == EMediaPipeBodyFusionSourceState::Fresh;
}

bool FMediaPipeBodyFusionSourceStatus::IsUsable() const
{
	return State == EMediaPipeBodyFusionSourceState::Fresh;
}

FMediaPipeTrackingSourceFrame::FMediaPipeTrackingSourceFrame()
{
	Reset();
}

void FMediaPipeTrackingSourceFrame::Reset()
{
	FrameTimeSeconds = -1.0;
	bHasHmdPose = false;
	HmdLocationWorld = FVector::ZeroVector;
	HmdRotationWorld = FQuat::Identity;
	TrackingUpWorld = FVector::UpVector;
	HmdTimestampSeconds = -1.0;
	HmdConfidence = 1.0f;
	HmdStatus = FMediaPipeBodyFusionSourceStatus();

	bHasLeftHand = false;
	bHasRightHand = false;
	LeftHandWorld = FVector::ZeroVector;
	RightHandWorld = FVector::ZeroVector;
	LeftHandTimestampSeconds = -1.0;
	RightHandTimestampSeconds = -1.0;
	LeftHandConfidence = 0.0f;
	RightHandConfidence = 0.0f;
	LeftHandStatus = FMediaPipeBodyFusionSourceStatus();
	RightHandStatus = FMediaPipeBodyFusionSourceStatus();

	bHasLeftArmChain = false;
	bHasRightArmChain = false;
	LeftArmShoulderWorld = FVector::ZeroVector;
	LeftArmElbowWorld = FVector::ZeroVector;
	LeftArmWristWorld = FVector::ZeroVector;
	RightArmShoulderWorld = FVector::ZeroVector;
	RightArmElbowWorld = FVector::ZeroVector;
	RightArmWristWorld = FVector::ZeroVector;
	LeftArmChainTimestampSeconds = -1.0;
	RightArmChainTimestampSeconds = -1.0;
	LeftArmChainConfidence = 0.0f;
	RightArmChainConfidence = 0.0f;
	LeftArmChainStatus = FMediaPipeBodyFusionSourceStatus();
	RightArmChainStatus = FMediaPipeBodyFusionSourceStatus();

	bHasBodyPose = false;
	BodyPoseTimestampSeconds = -1.0;
	BodyPoseConfidence = 0.0f;
	BodyPoseStatus = FMediaPipeBodyFusionSourceStatus();
	for (int32 Index = 0; Index < MediaPipePoseLandmarkCount; ++Index)
	{
		BodyPoseLandmarksWorld[Index] = FVector::ZeroVector;
		BodyPoseLandmarkReliability[Index] = 0.0f;
		BodyPoseLandmarkValid[Index] = 0;
	}
}

void FMediaPipeTrackingSourceFrame::SetBodyLandmark(
	const EMediaPipePoseLandmark Landmark,
	const FVector& LocationWorld,
	const float Reliability)
{
	const int32 Index = static_cast<int32>(Landmark);
	if (Index < 0 || Index >= MediaPipePoseLandmarkCount)
	{
		return;
	}

	BodyPoseLandmarksWorld[Index] = LocationWorld;
	BodyPoseLandmarkReliability[Index] = FMath::Clamp(Reliability, 0.0f, 1.0f);
	BodyPoseLandmarkValid[Index] = IsFiniteVector(LocationWorld) ? 1 : 0;
}

bool FMediaPipeTrackingSourceFrame::TryGetBodyLandmark(
	const EMediaPipePoseLandmark Landmark,
	FVector& OutLocationWorld,
	float* OutReliability) const
{
	const int32 Index = static_cast<int32>(Landmark);
	if (Index < 0 || Index >= MediaPipePoseLandmarkCount || BodyPoseLandmarkValid[Index] == 0)
	{
		return false;
	}

	OutLocationWorld = BodyPoseLandmarksWorld[Index];
	if (OutReliability)
	{
		*OutReliability = BodyPoseLandmarkReliability[Index];
	}
	return true;
}

FMediaPipeTrackingSourceFrame FMediaPipeTrackingSourceFrame::Normalized(
	const FMediaPipeBodyFusionFreshnessThresholds& Thresholds) const
{
	FMediaPipeTrackingSourceFrame NormalizedFrame = *this;
	NormalizedFrame.NormalizeInPlace(Thresholds);
	return NormalizedFrame;
}

void FMediaPipeTrackingSourceFrame::NormalizeInPlace(const FMediaPipeBodyFusionFreshnessThresholds& Thresholds)
{
	HmdRotationWorld = NormalizeQuatSafe(HmdRotationWorld);
	TrackingUpWorld = NormalizeUpSafe(TrackingUpWorld);
	UpdateFreshness(Thresholds);
}

void FMediaPipeTrackingSourceFrame::UpdateFreshness(const FMediaPipeBodyFusionFreshnessThresholds& Thresholds)
{
	HmdStatus = ClassifySource(
		bHasHmdPose,
		IsFiniteVector(HmdLocationWorld) && !HmdRotationWorld.ContainsNaN() && !TrackingUpWorld.IsNearlyZero(),
		HmdTimestampSeconds,
		FrameTimeSeconds,
		Thresholds.HmdMaxAgeSeconds,
		HmdConfidence,
		Thresholds.MinHmdConfidence);

	LeftHandStatus = ClassifySource(
		bHasLeftHand,
		IsFiniteVector(LeftHandWorld),
		LeftHandTimestampSeconds,
		FrameTimeSeconds,
		Thresholds.HandMaxAgeSeconds,
		LeftHandConfidence,
		Thresholds.MinDeviceConfidence);
	RightHandStatus = ClassifySource(
		bHasRightHand,
		IsFiniteVector(RightHandWorld),
		RightHandTimestampSeconds,
		FrameTimeSeconds,
		Thresholds.HandMaxAgeSeconds,
		RightHandConfidence,
		Thresholds.MinDeviceConfidence);

	LeftArmChainStatus = ClassifySource(
		bHasLeftArmChain,
		IsFiniteVector(LeftArmShoulderWorld) && IsFiniteVector(LeftArmElbowWorld) && IsFiniteVector(LeftArmWristWorld),
		LeftArmChainTimestampSeconds,
		FrameTimeSeconds,
		Thresholds.ArmChainMaxAgeSeconds,
		LeftArmChainConfidence,
		Thresholds.MinDeviceConfidence);
	RightArmChainStatus = ClassifySource(
		bHasRightArmChain,
		IsFiniteVector(RightArmShoulderWorld) && IsFiniteVector(RightArmElbowWorld) && IsFiniteVector(RightArmWristWorld),
		RightArmChainTimestampSeconds,
		FrameTimeSeconds,
		Thresholds.ArmChainMaxAgeSeconds,
		RightArmChainConfidence,
		Thresholds.MinDeviceConfidence);

	bool bAnyValidBodyLandmark = false;
	for (int32 Index = 0; Index < MediaPipePoseLandmarkCount; ++Index)
	{
		if (BodyPoseLandmarkValid[Index] != 0)
		{
			bAnyValidBodyLandmark = true;
			break;
		}
	}
	BodyPoseStatus = ClassifySource(
		bHasBodyPose,
		bAnyValidBodyLandmark,
		BodyPoseTimestampSeconds,
		FrameTimeSeconds,
		Thresholds.BodyPoseMaxAgeSeconds,
		BodyPoseConfidence,
		Thresholds.MinBodyPoseConfidence);
}

FMediaPipeBodyFusionSourceStatus FMediaPipeTrackingSourceFrame::ClassifySource(
	const bool bHasSample,
	const bool bSampleValid,
	const double SampleTimestampSeconds,
	const double NowSeconds,
	const float MaxAgeSeconds,
	const float Confidence,
	const float MinConfidence)
{
	FMediaPipeBodyFusionSourceStatus Status;
	Status.Confidence = Confidence;
	if (!bHasSample)
	{
		Status.State = EMediaPipeBodyFusionSourceState::Missing;
		return Status;
	}
	if (!bSampleValid || Confidence < MinConfidence || !FMath::IsFinite(Confidence))
	{
		Status.State = EMediaPipeBodyFusionSourceState::Invalid;
		return Status;
	}
	if (SampleTimestampSeconds < 0.0 || NowSeconds < 0.0)
	{
		Status.State = EMediaPipeBodyFusionSourceState::Invalid;
		return Status;
	}

	Status.AgeSeconds = static_cast<float>(NowSeconds - SampleTimestampSeconds);
	if (Status.AgeSeconds < -KINDA_SMALL_NUMBER)
	{
		Status.State = EMediaPipeBodyFusionSourceState::Invalid;
		return Status;
	}
	if (MaxAgeSeconds > 0.0f && Status.AgeSeconds > MaxAgeSeconds)
	{
		Status.State = EMediaPipeBodyFusionSourceState::Stale;
		return Status;
	}

	Status.State = EMediaPipeBodyFusionSourceState::Fresh;
	return Status;
}
