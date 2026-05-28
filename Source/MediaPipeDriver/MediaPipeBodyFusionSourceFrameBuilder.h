#pragma once

#include "CoreMinimal.h"
#include "Containers/StaticArray.h"
#include "MediaPipeFullArmChainProvider.h"
#include "MediaPipeQuestHandTypes.h"
#include "MediaPipeTrackingSourceTypes.h"

struct MEDIAPIPEDRIVER_API FMediaPipeBodyFusionHmdSourceSnapshot
{
	bool bHasPose = false;
	FVector LocationWorld = FVector::ZeroVector;
	FQuat RotationWorld = FQuat::Identity;
	FVector TrackingUpWorld = FVector::UpVector;
	double TimestampSeconds = -1.0;
	float Confidence = 1.0f;
};

class MEDIAPIPEDRIVER_API FMediaPipeBodyFusionSourceFrameBuilder
{
public:
	static void ResetForTimestamp(FMediaPipeTrackingSourceFrame& OutFrame, double NowSeconds);

	static void PopulateHmd(
		FMediaPipeTrackingSourceFrame& InOutFrame,
		const FMediaPipeBodyFusionHmdSourceSnapshot& Snapshot);

	static void PopulateQuestHands(
		FMediaPipeTrackingSourceFrame& InOutFrame,
		const FQuestHandTrackingSnapshot& Snapshot,
		double NowSeconds);

	static void PopulateFullArmChain(
		FMediaPipeTrackingSourceFrame& InOutFrame,
		const FMediaPipeFullArmChainSnapshot& Snapshot);

	static void PopulateMediaPipePose(
		FMediaPipeTrackingSourceFrame& InOutFrame,
		double TimestampSeconds,
		const TStaticArray<FVector, MediaPipePoseLandmarkCount>& LandmarksWorld,
		const TStaticArray<float, MediaPipePoseLandmarkCount>& LandmarkReliability,
		const TStaticArray<uint8, MediaPipePoseLandmarkCount>& LandmarkValid);

	static float CalculateCoreMediaPipePoseConfidence(
		const TStaticArray<float, MediaPipePoseLandmarkCount>& LandmarkReliability,
		const TStaticArray<uint8, MediaPipePoseLandmarkCount>& LandmarkValid);
};
