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
	float HandMaxAgeSeconds = 0.25f;
	float ArmChainMaxAgeSeconds = 0.25f;
	float BodyPoseMaxAgeSeconds = 0.25f;
	float MinHmdConfidence = 0.01f;
	float MinDeviceConfidence = 0.01f;
	float MinBodyPoseConfidence = 0.45f;
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

	bool bHasLeftHand = false;
	bool bHasRightHand = false;
	FVector LeftHandWorld = FVector::ZeroVector;
	FVector RightHandWorld = FVector::ZeroVector;
	double LeftHandTimestampSeconds = -1.0;
	double RightHandTimestampSeconds = -1.0;
	float LeftHandConfidence = 0.0f;
	float RightHandConfidence = 0.0f;
	FMediaPipeBodyFusionSourceStatus LeftHandStatus;
	FMediaPipeBodyFusionSourceStatus RightHandStatus;

	bool bHasLeftArmChain = false;
	bool bHasRightArmChain = false;
	FVector LeftArmShoulderWorld = FVector::ZeroVector;
	FVector LeftArmElbowWorld = FVector::ZeroVector;
	FVector LeftArmWristWorld = FVector::ZeroVector;
	FVector RightArmShoulderWorld = FVector::ZeroVector;
	FVector RightArmElbowWorld = FVector::ZeroVector;
	FVector RightArmWristWorld = FVector::ZeroVector;
	double LeftArmChainTimestampSeconds = -1.0;
	double RightArmChainTimestampSeconds = -1.0;
	float LeftArmChainConfidence = 0.0f;
	float RightArmChainConfidence = 0.0f;
	FMediaPipeBodyFusionSourceStatus LeftArmChainStatus;
	FMediaPipeBodyFusionSourceStatus RightArmChainStatus;

	// Quest body-tracking hips pose (schema v3, 2026-07-04): source signal for the live
	// body-yaw path, recorded so full-turn following can be replayed and scored.
	bool bHasBodyHips = false;
	FVector BodyHipsLocationWorld = FVector::ZeroVector;
	FQuat BodyHipsRotationWorld = FQuat::Identity;
	uint8 bBodyHipsOrientationValid = 0;
	double BodyHipsTimestampSeconds = -1.0;
	float BodyHipsConfidence = 0.0f;
	FMediaPipeBodyFusionSourceStatus BodyHipsStatus;

	// Webcam 21-landmark hand pair (schema v3, 2026-07-04): image-plane + hand-metric
	// landmarks with visibility/presence, both sides. Source signal for the overhead
	// chain-vs-camera discriminator and the camera-hand rotation path.
	bool bHasCameraHands = false;
	double CameraHandsTimestampSeconds = -1.0;
	FMediaPipeRawHandPair CameraHands;

	bool bHasBodyPose = false;
	double BodyPoseTimestampSeconds = -1.0;
	float BodyPoseConfidence = 0.0f;
	TStaticArray<FVector, MediaPipePoseLandmarkCount> BodyPoseLandmarksWorld;
	TStaticArray<float, MediaPipePoseLandmarkCount> BodyPoseLandmarkReliability;
	TStaticArray<uint8, MediaPipePoseLandmarkCount> BodyPoseLandmarkValid;
	FMediaPipeBodyFusionSourceStatus BodyPoseStatus;

	FMediaPipeTrackingSourceFrame();

	void Reset();
	void SetBodyLandmark(EMediaPipePoseLandmark Landmark, const FVector& LocationWorld, float Reliability);
	bool TryGetBodyLandmark(EMediaPipePoseLandmark Landmark, FVector& OutLocationWorld, float* OutReliability = nullptr) const;
	void SetMediaPipeLandmark(EMediaPipePoseLandmark Landmark, const FVector& LocationWorld, float Reliability) { SetBodyLandmark(Landmark, LocationWorld, Reliability); }
	bool TryGetMediaPipeLandmark(EMediaPipePoseLandmark Landmark, FVector& OutLocationWorld, float* OutReliability = nullptr) const { return TryGetBodyLandmark(Landmark, OutLocationWorld, OutReliability); }
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
