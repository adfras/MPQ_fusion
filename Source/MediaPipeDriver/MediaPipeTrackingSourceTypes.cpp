#include "MediaPipeTrackingSourceTypes.h"

namespace
{
bool IsFiniteVector(const FVector& Value)
{
	return !Value.ContainsNaN();
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

	bHasQuestLeftHand = false;
	bHasQuestRightHand = false;
	QuestLeftHandWorld = FVector::ZeroVector;
	QuestRightHandWorld = FVector::ZeroVector;
	QuestLeftHandTimestampSeconds = -1.0;
	QuestRightHandTimestampSeconds = -1.0;
	QuestLeftHandConfidence = 0.0f;
	QuestRightHandConfidence = 0.0f;
	QuestLeftHandStatus = FMediaPipeBodyFusionSourceStatus();
	QuestRightHandStatus = FMediaPipeBodyFusionSourceStatus();

	bHasQuestLeftFullArmChain = false;
	bHasQuestRightFullArmChain = false;
	QuestLeftShoulderWorld = FVector::ZeroVector;
	QuestLeftElbowWorld = FVector::ZeroVector;
	QuestLeftWristWorld = FVector::ZeroVector;
	QuestRightShoulderWorld = FVector::ZeroVector;
	QuestRightElbowWorld = FVector::ZeroVector;
	QuestRightWristWorld = FVector::ZeroVector;
	QuestLeftFullArmChainTimestampSeconds = -1.0;
	QuestRightFullArmChainTimestampSeconds = -1.0;
	QuestLeftFullArmChainConfidence = 0.0f;
	QuestRightFullArmChainConfidence = 0.0f;
	QuestLeftFullArmChainStatus = FMediaPipeBodyFusionSourceStatus();
	QuestRightFullArmChainStatus = FMediaPipeBodyFusionSourceStatus();

	bHasMediaPipePose = false;
	MediaPipePoseTimestampSeconds = -1.0;
	MediaPipePoseConfidence = 0.0f;
	MediaPipePoseStatus = FMediaPipeBodyFusionSourceStatus();
	for (int32 Index = 0; Index < MediaPipePoseLandmarkCount; ++Index)
	{
		MediaPipeLandmarksWorld[Index] = FVector::ZeroVector;
		MediaPipeLandmarkReliability[Index] = 0.0f;
		MediaPipeLandmarkValid[Index] = 0;
	}
}

void FMediaPipeTrackingSourceFrame::SetMediaPipeLandmark(
	const EMediaPipePoseLandmark Landmark,
	const FVector& LocationWorld,
	const float Reliability)
{
	const int32 Index = static_cast<int32>(Landmark);
	if (Index < 0 || Index >= MediaPipePoseLandmarkCount)
	{
		return;
	}

	MediaPipeLandmarksWorld[Index] = LocationWorld;
	MediaPipeLandmarkReliability[Index] = FMath::Clamp(Reliability, 0.0f, 1.0f);
	MediaPipeLandmarkValid[Index] = IsFiniteVector(LocationWorld) ? 1 : 0;
}

bool FMediaPipeTrackingSourceFrame::TryGetMediaPipeLandmark(
	const EMediaPipePoseLandmark Landmark,
	FVector& OutLocationWorld,
	float* OutReliability) const
{
	const int32 Index = static_cast<int32>(Landmark);
	if (Index < 0 || Index >= MediaPipePoseLandmarkCount || MediaPipeLandmarkValid[Index] == 0)
	{
		return false;
	}

	OutLocationWorld = MediaPipeLandmarksWorld[Index];
	if (OutReliability)
	{
		*OutReliability = MediaPipeLandmarkReliability[Index];
	}
	return true;
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

	QuestLeftHandStatus = ClassifySource(
		bHasQuestLeftHand,
		IsFiniteVector(QuestLeftHandWorld),
		QuestLeftHandTimestampSeconds,
		FrameTimeSeconds,
		Thresholds.QuestHandMaxAgeSeconds,
		QuestLeftHandConfidence,
		Thresholds.MinQuestConfidence);
	QuestRightHandStatus = ClassifySource(
		bHasQuestRightHand,
		IsFiniteVector(QuestRightHandWorld),
		QuestRightHandTimestampSeconds,
		FrameTimeSeconds,
		Thresholds.QuestHandMaxAgeSeconds,
		QuestRightHandConfidence,
		Thresholds.MinQuestConfidence);

	QuestLeftFullArmChainStatus = ClassifySource(
		bHasQuestLeftFullArmChain,
		IsFiniteVector(QuestLeftShoulderWorld) && IsFiniteVector(QuestLeftElbowWorld) && IsFiniteVector(QuestLeftWristWorld),
		QuestLeftFullArmChainTimestampSeconds,
		FrameTimeSeconds,
		Thresholds.QuestFullArmChainMaxAgeSeconds,
		QuestLeftFullArmChainConfidence,
		Thresholds.MinQuestConfidence);
	QuestRightFullArmChainStatus = ClassifySource(
		bHasQuestRightFullArmChain,
		IsFiniteVector(QuestRightShoulderWorld) && IsFiniteVector(QuestRightElbowWorld) && IsFiniteVector(QuestRightWristWorld),
		QuestRightFullArmChainTimestampSeconds,
		FrameTimeSeconds,
		Thresholds.QuestFullArmChainMaxAgeSeconds,
		QuestRightFullArmChainConfidence,
		Thresholds.MinQuestConfidence);

	bool bAnyValidMediaPipeLandmark = false;
	for (int32 Index = 0; Index < MediaPipePoseLandmarkCount; ++Index)
	{
		if (MediaPipeLandmarkValid[Index] != 0)
		{
			bAnyValidMediaPipeLandmark = true;
			break;
		}
	}
	MediaPipePoseStatus = ClassifySource(
		bHasMediaPipePose,
		bAnyValidMediaPipeLandmark,
		MediaPipePoseTimestampSeconds,
		FrameTimeSeconds,
		Thresholds.MediaPipePoseMaxAgeSeconds,
		MediaPipePoseConfidence,
		Thresholds.MinMediaPipeConfidence);
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
