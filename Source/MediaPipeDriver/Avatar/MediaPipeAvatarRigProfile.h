#pragma once

#include "CoreMinimal.h"

class USkeletalMesh;

struct FMediaPipeAvatarRigProfile
{
	FName ProfileId = NAME_None;
	bool bUseTargetFaceForwardAxis = false;
	float EmbodiedYawOffsetDeg = 0.0f;
	FVector DefaultEyeLocalOffset = FVector(0.0f, 0.0f, 162.0f);
	float EmbodiedCameraForwardOffsetCm = 0.0f;
	float HeadBoneFromEyeOffsetCm = 8.0f;
	bool bAutoCalibrateUpperBodyFollowAlpha = true;
	float UpperBodyFollowAlpha = 1.0f;
};

MEDIAPIPEDRIVER_API bool TryGetMediaPipeInternalMannyAvatarRigProfile(FMediaPipeAvatarRigProfile& OutProfile);
MEDIAPIPEDRIVER_API bool TryResolveMediaPipeAvatarRigProfileForMesh(const USkeletalMesh* MeshAsset, FMediaPipeAvatarRigProfile& OutProfile);
