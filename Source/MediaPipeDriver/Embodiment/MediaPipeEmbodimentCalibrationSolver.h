#pragma once

#include "CoreMinimal.h"

struct MEDIAPIPEDRIVER_API FMediaPipeEmbodimentCalibrationInput
{
	FVector MediaPipeHipCenterWorld = FVector::ZeroVector;
	FVector MediaPipeForwardWorld = FVector::ForwardVector;
	FVector AvatarPelvisAnchorWorld = FVector::ZeroVector;
	FVector AvatarForwardWorld = FVector::ForwardVector;
	FVector AvatarUpWorld = FVector::UpVector;
	FVector HmdWorld = FVector::ZeroVector;
	float ObservedBodyHeightCm = 0.0f;
	float AvatarBodyHeightCm = 0.0f;
	float Confidence = 0.0f;
	bool bHmdStable = false;
	bool bMediaPipeStable = false;
	double TimestampSeconds = -1.0;
};

struct MEDIAPIPEDRIVER_API FMediaPipeEmbodimentCalibration
{
	bool bHasCalibration = false;
	FQuat YawRotation = FQuat::Identity;
	FVector Translation = FVector::ZeroVector;
	float Scale = 1.0f;
	float Confidence = 0.0f;
	double TimestampSeconds = -1.0;
	FString LastRejectReason;

	void Reset();
	bool IsUsable(float MinConfidence = 0.5f) const;
	FTransform GetMediaPipeToAvatarTransform() const;
	FVector TransformMediaPipePoint(const FVector& MediaPipePointWorld) const;

	static bool TryBuildNeutralCalibration(
		const FMediaPipeEmbodimentCalibrationInput& Input,
		FMediaPipeEmbodimentCalibration& OutCalibration);
};
