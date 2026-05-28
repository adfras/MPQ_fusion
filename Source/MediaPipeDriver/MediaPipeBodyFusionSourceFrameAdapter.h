#pragma once

#include "CoreMinimal.h"
#include "Containers/StaticArray.h"
#include "MediaPipeFullArmChainProvider.h"
#include "MediaPipeQuestHandTypes.h"
#include "MediaPipeTrackingSourceTypes.h"

struct MEDIAPIPEDRIVER_API FMediaPipeBodyFusionMediaPipePoseSnapshot
{
	double TimestampSeconds = -1.0;
	TStaticArray<FVector, MediaPipePoseLandmarkCount> LandmarksWorld;
	TStaticArray<float, MediaPipePoseLandmarkCount> LandmarkReliability;
	TStaticArray<uint8, MediaPipePoseLandmarkCount> LandmarkValid;

	FMediaPipeBodyFusionMediaPipePoseSnapshot();
	void Reset();
	void SetLandmark(EMediaPipePoseLandmark Landmark, const FVector& LocationWorld, float Reliability);
};

struct MEDIAPIPEDRIVER_API FMediaPipeBodyFusionSourceFrameAdapterInput
{
	double NowSeconds = -1.0;

	bool bHasHmdPose = false;
	FVector HmdLocationWorld = FVector::ZeroVector;
	FQuat HmdRotationWorld = FQuat::Identity;
	FVector HmdTrackingUpWorld = FVector::UpVector;

	FQuestHandTrackingSnapshot QuestHands;
	FMediaPipeFullArmChainSnapshot FullArmChain;
	FMediaPipeBodyFusionMediaPipePoseSnapshot MediaPipePose;

	bool bOverrideQuestFullArmChainMaxAgeSeconds = false;
	float QuestFullArmChainMaxAgeSeconds = 0.25f;
};

class MEDIAPIPEDRIVER_API FMediaPipeBodyFusionSourceFrameAdapter
{
public:
	static void BuildSourceFrame(
		const FMediaPipeBodyFusionSourceFrameAdapterInput& Input,
		FMediaPipeTrackingSourceFrame& OutFrame,
		FMediaPipeBodyFusionFreshnessThresholds& OutThresholds);
};
