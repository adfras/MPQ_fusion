#pragma once

#include "CoreMinimal.h"
#include "Containers/StaticArray.h"
#include "MediaPipePoseTypes.h"

enum class EMediaPipeBodyFusionSourceState : uint8
{
	Missing,
	Stale,
	Invalid,
	Fresh
};

struct MEDIAPIPEDRIVER_API FMediaPipeBodyFusionSourceStatus
{
	EMediaPipeBodyFusionSourceState State = EMediaPipeBodyFusionSourceState::Missing;
	float AgeSeconds = -1.0f;
	float Confidence = 0.0f;

	bool IsFresh() const;
	bool IsUsable() const;
};

struct MEDIAPIPEDRIVER_API FMediaPipeBodyFusionFreshnessThresholds
{
	float HmdMaxAgeSeconds = 0.25f;
	float QuestHandMaxAgeSeconds = 0.25f;
	float QuestFullArmChainMaxAgeSeconds = 0.25f;
	float MediaPipePoseMaxAgeSeconds = 0.25f;
	float MinHmdConfidence = 0.01f;
	float MinQuestConfidence = 0.01f;
	float MinMediaPipeConfidence = 0.45f;
};

struct MEDIAPIPEDRIVER_API FMediaPipeTrackingSourceFrame
{
	double FrameTimeSeconds = -1.0;

	bool bHasHmdPose = false;
	FVector HmdLocationWorld = FVector::ZeroVector;
	FQuat HmdRotationWorld = FQuat::Identity;
	FVector TrackingUpWorld = FVector::UpVector;
	double HmdTimestampSeconds = -1.0;
	float HmdConfidence = 1.0f;
	FMediaPipeBodyFusionSourceStatus HmdStatus;

	bool bHasQuestLeftHand = false;
	bool bHasQuestRightHand = false;
	FVector QuestLeftHandWorld = FVector::ZeroVector;
	FVector QuestRightHandWorld = FVector::ZeroVector;
	double QuestLeftHandTimestampSeconds = -1.0;
	double QuestRightHandTimestampSeconds = -1.0;
	float QuestLeftHandConfidence = 0.0f;
	float QuestRightHandConfidence = 0.0f;
	FMediaPipeBodyFusionSourceStatus QuestLeftHandStatus;
	FMediaPipeBodyFusionSourceStatus QuestRightHandStatus;

	bool bHasQuestLeftFullArmChain = false;
	bool bHasQuestRightFullArmChain = false;
	FVector QuestLeftShoulderWorld = FVector::ZeroVector;
	FVector QuestLeftElbowWorld = FVector::ZeroVector;
	FVector QuestLeftWristWorld = FVector::ZeroVector;
	FVector QuestRightShoulderWorld = FVector::ZeroVector;
	FVector QuestRightElbowWorld = FVector::ZeroVector;
	FVector QuestRightWristWorld = FVector::ZeroVector;
	double QuestLeftFullArmChainTimestampSeconds = -1.0;
	double QuestRightFullArmChainTimestampSeconds = -1.0;
	float QuestLeftFullArmChainConfidence = 0.0f;
	float QuestRightFullArmChainConfidence = 0.0f;
	FMediaPipeBodyFusionSourceStatus QuestLeftFullArmChainStatus;
	FMediaPipeBodyFusionSourceStatus QuestRightFullArmChainStatus;

	bool bHasMediaPipePose = false;
	double MediaPipePoseTimestampSeconds = -1.0;
	float MediaPipePoseConfidence = 0.0f;
	TStaticArray<FVector, MediaPipePoseLandmarkCount> MediaPipeLandmarksWorld;
	TStaticArray<float, MediaPipePoseLandmarkCount> MediaPipeLandmarkReliability;
	TStaticArray<uint8, MediaPipePoseLandmarkCount> MediaPipeLandmarkValid;
	FMediaPipeBodyFusionSourceStatus MediaPipePoseStatus;

	FMediaPipeTrackingSourceFrame();

	void Reset();
	void SetMediaPipeLandmark(EMediaPipePoseLandmark Landmark, const FVector& LocationWorld, float Reliability);
	bool TryGetMediaPipeLandmark(EMediaPipePoseLandmark Landmark, FVector& OutLocationWorld, float* OutReliability = nullptr) const;
	FMediaPipeTrackingSourceFrame Normalized(const FMediaPipeBodyFusionFreshnessThresholds& Thresholds) const;
	void NormalizeInPlace(const FMediaPipeBodyFusionFreshnessThresholds& Thresholds);
	void UpdateFreshness(const FMediaPipeBodyFusionFreshnessThresholds& Thresholds);

	static FMediaPipeBodyFusionSourceStatus ClassifySource(
		bool bHasSample,
		bool bSampleValid,
		double SampleTimestampSeconds,
		double NowSeconds,
		float MaxAgeSeconds,
		float Confidence,
		float MinConfidence);
};
