#pragma once

#include "CoreMinimal.h"
#include "MediaPipeAvatarEmbodimentProfile.h"

struct MEDIAPIPEDRIVER_API FMediaPipeAvatarReferencePoseProportions
{
	bool bHasReferencePose = false;

	bool bHasLeftArm = false;
	bool bHasRightArm = false;
	float LeftUpperArmLengthCm = 0.0f;
	float RightUpperArmLengthCm = 0.0f;
	float LeftLowerArmLengthCm = 0.0f;
	float RightLowerArmLengthCm = 0.0f;

	bool bHasLeftLeg = false;
	bool bHasRightLeg = false;
	float LeftThighLengthCm = 0.0f;
	float RightThighLengthCm = 0.0f;
	float LeftCalfLengthCm = 0.0f;
	float RightCalfLengthCm = 0.0f;

	bool bHasChestLocal = false;
	bool bHasNeck02Local = false;
	FVector PelvisLocal = FVector::ZeroVector;
	FVector ChestLocal = FVector::ZeroVector;
	FVector NeckLocal = FVector::ZeroVector;
	FVector Neck02Local = FVector::ZeroVector;
	FVector HeadLocal = FVector::ZeroVector;
	FQuat HeadBasisComponent = FQuat::Identity;
};

struct MEDIAPIPEDRIVER_API FMediaPipeAvatarReferenceProfileCalibrationResult
{
	bool bAppliedReferencePose = false;
	bool bResolvedEyeLocalOffset = false;
};

class MEDIAPIPEDRIVER_API FMediaPipeAvatarProfileReferenceCalibration
{
public:
	static float ResolveUpperBodyFollowAlpha(float HeadToChestCm, float ChestToPelvisCm, float FallbackAlpha);
	static FMediaPipeAvatarReferenceProfileCalibrationResult ApplyReferencePose(
		const FMediaPipeAvatarReferencePoseProportions& Reference,
		FMediaPipeAvatarEmbodimentProfile& InOutProfile);
};
