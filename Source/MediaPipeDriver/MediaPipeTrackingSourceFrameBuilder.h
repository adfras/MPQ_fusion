#pragma once

#include "CoreMinimal.h"
#include "Containers/StaticArray.h"
#include "MediaPipeFullArmChainProvider.h"
#include "MediaPipeQuestHandTypes.h"
#include "MediaPipeTrackingSourceTypes.h"

struct MEDIAPIPEDRIVER_API FMediaPipeTrackingHmdSourceSnapshot
{
	bool bHasPose = false;
	FVector LocationWorld = FVector::ZeroVector;
	FQuat RotationWorld = FQuat::Identity;
	FVector TrackingUpWorld = FVector::UpVector;
	double TimestampSeconds = -1.0;
	float Confidence = 1.0f;
};

struct MEDIAPIPEDRIVER_API FMediaPipeTrackingMediaPipePoseSnapshot
{
	double TimestampSeconds = -1.0;
	TStaticArray<FVector, MediaPipePoseLandmarkCount> LandmarksWorld;
	TStaticArray<float, MediaPipePoseLandmarkCount> LandmarkReliability;
	TStaticArray<uint8, MediaPipePoseLandmarkCount> LandmarkValid;

	FMediaPipeTrackingMediaPipePoseSnapshot();
	void Reset();
	void SetLandmark(EMediaPipePoseLandmark Landmark, const FVector& LocationWorld, float Reliability);
};

struct MEDIAPIPEDRIVER_API FMediaPipeTrackingSourceFrameBuilderInput
{
	double NowSeconds = -1.0;

	bool bHasHmdPose = false;
	FVector HmdLocationWorld = FVector::ZeroVector;
	FQuat HmdRotationWorld = FQuat::Identity;
	FVector HmdTrackingUpWorld = FVector::UpVector;

	FQuestHandTrackingSnapshot QuestHands;
	FMediaPipeFullArmChainSnapshot FullArmChain;
	FMediaPipeTrackingMediaPipePoseSnapshot MediaPipePose;

	bool bOverrideQuestFullArmChainMaxAgeSeconds = false;
	float QuestFullArmChainMaxAgeSeconds = 0.25f;
};

class MEDIAPIPEDRIVER_API FMediaPipeTrackingSourceFrameBuilder
{
public:
	static void BuildSourceFrame(
		const FMediaPipeTrackingSourceFrameBuilderInput& Input,
		FMediaPipeTrackingSourceFrame& OutFrame,
		FMediaPipeBodyFusionFreshnessThresholds& OutThresholds);

	static void ResetForTimestamp(FMediaPipeTrackingSourceFrame& OutFrame, double NowSeconds);

	static void PopulateHmd(
		FMediaPipeTrackingSourceFrame& InOutFrame,
		const FMediaPipeTrackingHmdSourceSnapshot& Snapshot);

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
