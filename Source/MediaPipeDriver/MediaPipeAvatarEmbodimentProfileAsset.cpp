#include "MediaPipeAvatarEmbodimentProfileAsset.h"

namespace
{
EMediaPipeAvatarSkeletonFamily ToRuntimeSkeletonFamily(
	const EMediaPipeAvatarProfileAssetSkeletonFamily SkeletonFamily)
{
	switch (SkeletonFamily)
	{
	case EMediaPipeAvatarProfileAssetSkeletonFamily::MannyLike:
		return EMediaPipeAvatarSkeletonFamily::MannyLike;
	case EMediaPipeAvatarProfileAssetSkeletonFamily::MetaHuman:
		return EMediaPipeAvatarSkeletonFamily::MetaHuman;
	case EMediaPipeAvatarProfileAssetSkeletonFamily::CustomHumanoid:
		return EMediaPipeAvatarSkeletonFamily::CustomHumanoid;
	case EMediaPipeAvatarProfileAssetSkeletonFamily::Unknown:
	default:
		return EMediaPipeAvatarSkeletonFamily::Unknown;
	}
}

EMediaPipePelvisAuthorityMode ToRuntimePelvisAuthorityMode(
	const EMediaPipeAvatarProfileAssetPelvisAuthorityMode PelvisAuthorityMode)
{
	switch (PelvisAuthorityMode)
	{
	case EMediaPipeAvatarProfileAssetPelvisAuthorityMode::ProfileLocked:
		return EMediaPipePelvisAuthorityMode::ProfileLocked;
	case EMediaPipeAvatarProfileAssetPelvisAuthorityMode::MediaPipeHipsFull:
		return EMediaPipePelvisAuthorityMode::MediaPipeHipsFull;
	case EMediaPipeAvatarProfileAssetPelvisAuthorityMode::FollowUpperBodyExplicit:
		return EMediaPipePelvisAuthorityMode::FollowUpperBodyExplicit;
	case EMediaPipeAvatarProfileAssetPelvisAuthorityMode::MediaPipeHipsVerticalOnly:
	default:
		return EMediaPipePelvisAuthorityMode::MediaPipeHipsVerticalOnly;
	}
}
}

FMediaPipeAvatarBoneMap FMediaPipeAvatarBoneMapAssetData::ToRuntimeBoneMap() const
{
	FMediaPipeAvatarBoneMap BoneMap;
	BoneMap.Root = Root;
	BoneMap.Pelvis = Pelvis;
	BoneMap.Chest = Chest;
	BoneMap.Neck = Neck;
	BoneMap.Head = Head;
	BoneMap.LeftShoulder = LeftShoulder;
	BoneMap.LeftUpperArm = LeftUpperArm;
	BoneMap.LeftLowerArm = LeftLowerArm;
	BoneMap.LeftHand = LeftHand;
	BoneMap.RightShoulder = RightShoulder;
	BoneMap.RightUpperArm = RightUpperArm;
	BoneMap.RightLowerArm = RightLowerArm;
	BoneMap.RightHand = RightHand;
	return BoneMap;
}

FMediaPipeAvatarEmbodimentProfile UMediaPipeAvatarEmbodimentProfileAsset::BuildRuntimeProfile() const
{
	FMediaPipeAvatarEmbodimentProfile Profile;
	Profile.ProfileId = ProfileId;
	Profile.SkeletonFamily = ToRuntimeSkeletonFamily(SkeletonFamily);
	Profile.bUseTargetFaceForwardAxis = bUseTargetFaceForwardAxis;
	Profile.EmbodiedYawOffsetDeg = EmbodiedYawOffsetDeg;
	Profile.DefaultEyeLocalOffset = DefaultEyeLocalOffset;
	Profile.bHasDefaultHeadLocalOffset = bHasDefaultHeadLocalOffset;
	Profile.DefaultHeadLocalOffset = DefaultHeadLocalOffset;
	Profile.bHasDefaultEyeLocalInHeadOffset = bHasDefaultEyeLocalInHeadOffset;
	Profile.DefaultEyeLocalInHeadOffset = DefaultEyeLocalInHeadOffset;
	Profile.DefaultChestLocalOffset = DefaultChestLocalOffset;
	Profile.DefaultNeckLocalOffset = DefaultNeckLocalOffset;
	Profile.DefaultNeck02LocalOffset = DefaultNeck02LocalOffset;
	Profile.DefaultPelvisLocalOffset = DefaultPelvisLocalOffset;
	Profile.EmbodiedCameraForwardOffsetCm = EmbodiedCameraForwardOffsetCm;
	Profile.HeadBoneFromEyeOffsetCm = HeadBoneFromEyeOffsetCm;
	Profile.bAutoCalibrateUpperBodyFollowAlpha = bAutoCalibrateUpperBodyFollowAlpha;
	Profile.UpperBodyFollowAlpha = UpperBodyFollowAlpha;
	Profile.PelvisAuthorityMode = ToRuntimePelvisAuthorityMode(PelvisAuthorityMode);
	Profile.ExpectedHeadToChestCm = ExpectedHeadToChestCm;
	Profile.ExpectedChestToPelvisCm = ExpectedChestToPelvisCm;
	Profile.ExpectedUpperArmLengthCm = ExpectedUpperArmLengthCm;
	Profile.ExpectedLowerArmLengthCm = ExpectedLowerArmLengthCm;
	Profile.MinUpperArmLengthCm = MinUpperArmLengthCm;
	Profile.MaxUpperArmLengthCm = MaxUpperArmLengthCm;
	Profile.MinLowerArmLengthCm = MinLowerArmLengthCm;
	Profile.MaxLowerArmLengthCm = MaxLowerArmLengthCm;
	Profile.ExpectedThighLengthCm = ExpectedThighLengthCm;
	Profile.ExpectedCalfLengthCm = ExpectedCalfLengthCm;
	Profile.MinThighLengthCm = MinThighLengthCm;
	Profile.MaxThighLengthCm = MaxThighLengthCm;
	Profile.MinCalfLengthCm = MinCalfLengthCm;
	Profile.MaxCalfLengthCm = MaxCalfLengthCm;
	Profile.BoneMap = bUseSkeletonAdapterBoneMap && SkeletonAdapter
		? SkeletonAdapter->SemanticSkeleton.ToAvatarBoneMap()
		: BoneMapOverride.ToRuntimeBoneMap();
	return Profile;
}

bool UMediaPipeAvatarEmbodimentProfileAsset::TryBuildRuntimeProfile(
	FMediaPipeAvatarEmbodimentProfile& OutProfile,
	FString& OutError) const
{
	OutProfile = BuildRuntimeProfile();
	if (!OutProfile.IsValid())
	{
		OutError = TEXT("Runtime embodiment profile is invalid");
		return false;
	}

	OutError.Reset();
	return true;
}
