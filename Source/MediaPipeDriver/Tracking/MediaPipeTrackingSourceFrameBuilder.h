#pragma once

#include "CoreMinimal.h"
#include "Containers/StaticArray.h"
#include "MediaPipeTrackingSourceTypes.h"

#include "HeadMountedDisplayTypes.h"

static constexpr int32 MediaPipeTrackingHandKeypointCount = EHandKeypointCount;

struct MEDIAPIPEDRIVER_API FMediaPipeTrackingHmdSourceSnapshot
{
	bool bHasPose = false;
	FVector LocationWorld = FVector::ZeroVector;
	FQuat RotationWorld = FQuat::Identity;
	FVector TrackingUpWorld = FVector::UpVector;
	double TimestampSeconds = -1.0;
	float Confidence = 1.0f;
};

struct MEDIAPIPEDRIVER_API FMediaPipeTrackingHandSourceSnapshot
{
	int32 ProviderCount = 0;
	int32 ValidProviderCount = 0;
	uint8 bHasLeft = 0;
	uint8 bHasRight = 0;
	uint8 bLeftTracked = 0;
	uint8 bRightTracked = 0;
	// Set when the full 26-keypoint arrays are populated (live Quest hands or a schema-v2 replay
	// cache). Wrist-only sources (v1 replay caches) leave these clear, so consumers know the
	// keypoint arrays beyond the wrist are just zero-initialized placeholders.
	uint8 bLeftHasFullKeypoints = 0;
	uint8 bRightHasFullKeypoints = 0;
	double LeftTimestampSeconds = -1.0;
	double RightTimestampSeconds = -1.0;
	TStaticArray<FVector, MediaPipeTrackingHandKeypointCount> LeftPositionsWorld;
	TStaticArray<FQuat, MediaPipeTrackingHandKeypointCount> LeftRotationsWorld;
	TStaticArray<float, MediaPipeTrackingHandKeypointCount> LeftRadii;
	TStaticArray<FVector, MediaPipeTrackingHandKeypointCount> RightPositionsWorld;
	TStaticArray<FQuat, MediaPipeTrackingHandKeypointCount> RightRotationsWorld;
	TStaticArray<float, MediaPipeTrackingHandKeypointCount> RightRadii;

	FMediaPipeTrackingHandSourceSnapshot();
	void Reset();
};

struct MEDIAPIPEDRIVER_API FMediaPipeTrackingArmChainSideSnapshot
{
	bool bHasChain = false;
	FVector ShoulderWorld = FVector::ZeroVector;
	FVector ElbowWorld = FVector::ZeroVector;
	FVector WristWorld = FVector::ZeroVector;
	double TimestampSeconds = -1.0;
	float Confidence = 0.0f;
};

struct MEDIAPIPEDRIVER_API FMediaPipeTrackingArmChainSourceSnapshot
{
	FMediaPipeTrackingArmChainSideSnapshot Left;
	FMediaPipeTrackingArmChainSideSnapshot Right;

	void Reset();
};

struct MEDIAPIPEDRIVER_API FMediaPipeTrackingBodyPoseSnapshot
{
	double TimestampSeconds = -1.0;
	TStaticArray<FVector, MediaPipePoseLandmarkCount> LandmarksWorld;
	TStaticArray<float, MediaPipePoseLandmarkCount> LandmarkReliability;
	TStaticArray<uint8, MediaPipePoseLandmarkCount> LandmarkValid;

	FMediaPipeTrackingBodyPoseSnapshot();
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
	double HmdTimestampSeconds = -1.0;

	FMediaPipeTrackingHandSourceSnapshot Hands;
	FMediaPipeTrackingArmChainSourceSnapshot ArmChain;
	FMediaPipeTrackingBodyPoseSnapshot BodyPose;

	bool bOverrideArmChainMaxAgeSeconds = false;
	float ArmChainMaxAgeSeconds = 0.25f;
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

	static void PopulateHands(
		FMediaPipeTrackingSourceFrame& InOutFrame,
		const FMediaPipeTrackingHandSourceSnapshot& Snapshot,
		double NowSeconds);

	static void PopulateArmChain(
		FMediaPipeTrackingSourceFrame& InOutFrame,
		const FMediaPipeTrackingArmChainSourceSnapshot& Snapshot);

	static void PopulateBodyPose(
		FMediaPipeTrackingSourceFrame& InOutFrame,
		double TimestampSeconds,
		const TStaticArray<FVector, MediaPipePoseLandmarkCount>& LandmarksWorld,
		const TStaticArray<float, MediaPipePoseLandmarkCount>& LandmarkReliability,
		const TStaticArray<uint8, MediaPipePoseLandmarkCount>& LandmarkValid);

	static float CalculateCoreBodyPoseConfidence(
		const TStaticArray<float, MediaPipePoseLandmarkCount>& LandmarkReliability,
		const TStaticArray<uint8, MediaPipePoseLandmarkCount>& LandmarkValid);
};
