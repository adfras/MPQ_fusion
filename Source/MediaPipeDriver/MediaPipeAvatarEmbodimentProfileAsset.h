#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "MediaPipeAvatarEmbodimentProfile.h"
#include "MediaPipeSkeletonAdapterDataAsset.h"

#include "MediaPipeAvatarEmbodimentProfileAsset.generated.h"

UENUM(BlueprintType)
enum class EMediaPipeAvatarProfileAssetSkeletonFamily : uint8
{
	Unknown,
	MannyLike,
	MetaHuman,
	CustomHumanoid
};

UENUM(BlueprintType)
enum class EMediaPipeAvatarProfileAssetPelvisAuthorityMode : uint8
{
	ProfileLocked,
	MediaPipeHipsVerticalOnly,
	MediaPipeHipsFull,
	FollowUpperBodyExplicit
};

USTRUCT(BlueprintType)
struct MEDIAPIPEDRIVER_API FMediaPipeAvatarBoneMapAssetData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MediaPipe")
	FName Root = FName(TEXT("root"));

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MediaPipe")
	FName Pelvis = FName(TEXT("pelvis"));

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MediaPipe")
	FName Chest = FName(TEXT("spine_03"));

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MediaPipe")
	FName Neck = FName(TEXT("neck_01"));

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MediaPipe")
	FName Head = FName(TEXT("head"));

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MediaPipe")
	FName LeftShoulder = FName(TEXT("clavicle_l"));

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MediaPipe")
	FName LeftUpperArm = FName(TEXT("upperarm_l"));

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MediaPipe")
	FName LeftLowerArm = FName(TEXT("lowerarm_l"));

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MediaPipe")
	FName LeftHand = FName(TEXT("hand_l"));

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MediaPipe")
	FName RightShoulder = FName(TEXT("clavicle_r"));

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MediaPipe")
	FName RightUpperArm = FName(TEXT("upperarm_r"));

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MediaPipe")
	FName RightLowerArm = FName(TEXT("lowerarm_r"));

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MediaPipe")
	FName RightHand = FName(TEXT("hand_r"));

	FMediaPipeAvatarBoneMap ToRuntimeBoneMap() const;
};

UCLASS(BlueprintType)
class MEDIAPIPEDRIVER_API UMediaPipeAvatarEmbodimentProfileAsset : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MediaPipe")
	FName ProfileId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MediaPipe")
	EMediaPipeAvatarProfileAssetSkeletonFamily SkeletonFamily = EMediaPipeAvatarProfileAssetSkeletonFamily::Unknown;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MediaPipe")
	bool bUseTargetFaceForwardAxis = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MediaPipe")
	float EmbodiedYawOffsetDeg = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MediaPipe")
	FVector DefaultEyeLocalOffset = FVector(0.0f, 0.0f, 162.0f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MediaPipe")
	bool bHasDefaultHeadLocalOffset = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MediaPipe", meta = (EditCondition = "bHasDefaultHeadLocalOffset"))
	FVector DefaultHeadLocalOffset = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MediaPipe")
	bool bHasDefaultEyeLocalInHeadOffset = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MediaPipe", meta = (EditCondition = "bHasDefaultEyeLocalInHeadOffset"))
	FVector DefaultEyeLocalInHeadOffset = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MediaPipe")
	FVector DefaultChestLocalOffset = FVector(0.0f, 0.0f, 108.0f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MediaPipe")
	FVector DefaultNeckLocalOffset = FVector(0.0f, 0.0f, 147.0f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MediaPipe")
	FVector DefaultNeck02LocalOffset = FVector(0.0f, 0.0f, 154.0f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MediaPipe")
	FVector DefaultPelvisLocalOffset = FVector(0.0f, 0.0f, 88.0f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MediaPipe")
	float EmbodiedCameraForwardOffsetCm = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MediaPipe")
	float HeadBoneFromEyeOffsetCm = 8.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MediaPipe")
	bool bAutoCalibrateUpperBodyFollowAlpha = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MediaPipe")
	float UpperBodyFollowAlpha = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MediaPipe")
	EMediaPipeAvatarProfileAssetPelvisAuthorityMode PelvisAuthorityMode =
		EMediaPipeAvatarProfileAssetPelvisAuthorityMode::MediaPipeHipsVerticalOnly;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MediaPipe")
	float ExpectedHeadToChestCm = 54.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MediaPipe")
	float ExpectedChestToPelvisCm = 58.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MediaPipe")
	float ExpectedUpperArmLengthCm = 30.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MediaPipe")
	float ExpectedLowerArmLengthCm = 28.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MediaPipe")
	float MinUpperArmLengthCm = 18.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MediaPipe")
	float MaxUpperArmLengthCm = 46.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MediaPipe")
	float MinLowerArmLengthCm = 18.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MediaPipe")
	float MaxLowerArmLengthCm = 44.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MediaPipe")
	float ExpectedThighLengthCm = 43.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MediaPipe")
	float ExpectedCalfLengthCm = 43.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MediaPipe")
	float MinThighLengthCm = 28.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MediaPipe")
	float MaxThighLengthCm = 62.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MediaPipe")
	float MinCalfLengthCm = 28.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MediaPipe")
	float MaxCalfLengthCm = 62.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MediaPipe")
	bool bUseSkeletonAdapterBoneMap = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MediaPipe", meta = (EditCondition = "bUseSkeletonAdapterBoneMap"))
	TObjectPtr<UMediaPipeSkeletonAdapterDataAsset> SkeletonAdapter = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MediaPipe", meta = (EditCondition = "!bUseSkeletonAdapterBoneMap"))
	FMediaPipeAvatarBoneMapAssetData BoneMapOverride;

	FMediaPipeAvatarEmbodimentProfile BuildRuntimeProfile() const;
	bool TryBuildRuntimeProfile(FMediaPipeAvatarEmbodimentProfile& OutProfile, FString& OutError) const;
};
