#include "MediaPipeAvatarRigProfile.h"

#include "Engine/SkeletalMesh.h"

namespace
{
const TCHAR* const InternalMannyLikeMeshPath =
	TEXT("/Game/MediaPipe/MediaPipeRig/SK_MediaPipeMannyLike.SK_MediaPipeMannyLike");

void PopulateInternalMannyAvatarRigProfile(FMediaPipeAvatarRigProfile& OutProfile)
{
	OutProfile.ProfileId = FName(TEXT("InternalMannyLike"));
	OutProfile.bUseTargetFaceForwardAxis = true;
	OutProfile.EmbodiedYawOffsetDeg = -90.0f;
	OutProfile.DefaultEyeLocalOffset = FVector(0.0f, 0.66f, 162.58f);
	OutProfile.EmbodiedCameraForwardOffsetCm = 10.0f;
	OutProfile.HeadBoneFromEyeOffsetCm = 0.0f;
}
}

bool TryGetMediaPipeInternalMannyAvatarRigProfile(FMediaPipeAvatarRigProfile& OutProfile)
{
	PopulateInternalMannyAvatarRigProfile(OutProfile);
	return true;
}

bool TryResolveMediaPipeAvatarRigProfileForMesh(const USkeletalMesh* MeshAsset, FMediaPipeAvatarRigProfile& OutProfile)
{
	if (!MeshAsset)
	{
		return false;
	}

	if (MeshAsset->GetPathName().Equals(InternalMannyLikeMeshPath, ESearchCase::IgnoreCase))
	{
		PopulateInternalMannyAvatarRigProfile(OutProfile);
		return true;
	}

	return false;
}
