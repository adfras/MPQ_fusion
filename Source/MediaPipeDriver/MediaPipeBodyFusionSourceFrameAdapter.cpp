#include "MediaPipeBodyFusionSourceFrameAdapter.h"

#include "MediaPipeBodyFusionSourceFrameBuilder.h"
#include "MediaPipeSourceNormalizer.h"

FMediaPipeBodyFusionMediaPipePoseSnapshot::FMediaPipeBodyFusionMediaPipePoseSnapshot()
{
	Reset();
}

void FMediaPipeBodyFusionMediaPipePoseSnapshot::Reset()
{
	TimestampSeconds = -1.0;
	for (int32 Index = 0; Index < MediaPipePoseLandmarkCount; ++Index)
	{
		LandmarksWorld[Index] = FVector::ZeroVector;
		LandmarkReliability[Index] = 0.0f;
		LandmarkValid[Index] = 0;
	}
}

void FMediaPipeBodyFusionMediaPipePoseSnapshot::SetLandmark(
	const EMediaPipePoseLandmark Landmark,
	const FVector& LocationWorld,
	const float Reliability)
{
	const int32 Index = static_cast<int32>(Landmark);
	if (Index < 0 || Index >= MediaPipePoseLandmarkCount)
	{
		return;
	}

	LandmarksWorld[Index] = LocationWorld;
	LandmarkReliability[Index] = Reliability;
	LandmarkValid[Index] = 1;
}

void FMediaPipeBodyFusionSourceFrameAdapter::BuildSourceFrame(
	const FMediaPipeBodyFusionSourceFrameAdapterInput& Input,
	FMediaPipeTrackingSourceFrame& OutFrame,
	FMediaPipeBodyFusionFreshnessThresholds& OutThresholds)
{
	FMediaPipeBodyFusionSourceFrameBuilder::ResetForTimestamp(OutFrame, Input.NowSeconds);

	if (Input.bHasHmdPose)
	{
		FMediaPipeBodyFusionHmdSourceSnapshot HmdSource;
		HmdSource.bHasPose = true;
		HmdSource.LocationWorld = Input.HmdLocationWorld;
		HmdSource.RotationWorld = Input.HmdRotationWorld;
		HmdSource.TrackingUpWorld = Input.HmdTrackingUpWorld;
		HmdSource.TimestampSeconds = Input.NowSeconds;
		HmdSource.Confidence = 1.0f;
		FMediaPipeBodyFusionSourceFrameBuilder::PopulateHmd(OutFrame, HmdSource);
	}

	FMediaPipeBodyFusionSourceFrameBuilder::PopulateQuestHands(OutFrame, Input.QuestHands, Input.NowSeconds);
	FMediaPipeBodyFusionSourceFrameBuilder::PopulateFullArmChain(OutFrame, Input.FullArmChain);
	FMediaPipeBodyFusionSourceFrameBuilder::PopulateMediaPipePose(
		OutFrame,
		Input.MediaPipePose.TimestampSeconds,
		Input.MediaPipePose.LandmarksWorld,
		Input.MediaPipePose.LandmarkReliability,
		Input.MediaPipePose.LandmarkValid);

	OutThresholds = FMediaPipeBodyFusionFreshnessThresholds();
	if (Input.bOverrideQuestFullArmChainMaxAgeSeconds)
	{
		OutThresholds.QuestFullArmChainMaxAgeSeconds = Input.QuestFullArmChainMaxAgeSeconds;
	}
	FMediaPipeSourceNormalizer::NormalizeInPlace(OutFrame, OutThresholds);
}
