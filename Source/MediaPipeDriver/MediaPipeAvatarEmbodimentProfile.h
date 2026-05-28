#pragma once

#include "CoreMinimal.h"

class UMeshComponent;
struct FMediaPipeAvatarRigProfile;
struct FMediaPipeMetaHumanProfileDefinition;

enum class EMediaPipeAvatarSkeletonFamily : uint8
{
	Unknown,
	MannyLike,
	MetaHuman,
	CustomHumanoid
};

enum class EMediaPipePelvisAuthorityMode : uint8
{
	ProfileLocked,
	MediaPipeHipsVerticalOnly,
	MediaPipeHipsFull,
	FollowUpperBodyExplicit
};

struct MEDIAPIPEDRIVER_API FMediaPipeAvatarBoneMap
{
	FName Root = FName(TEXT("root"));
	FName Pelvis = FName(TEXT("pelvis"));
	FName Chest = FName(TEXT("spine_03"));
	FName Neck = FName(TEXT("neck_01"));
	FName Head = FName(TEXT("head"));
	FName LeftShoulder = FName(TEXT("clavicle_l"));
	FName LeftUpperArm = FName(TEXT("upperarm_l"));
	FName LeftLowerArm = FName(TEXT("lowerarm_l"));
	FName LeftHand = FName(TEXT("hand_l"));
	FName RightShoulder = FName(TEXT("clavicle_r"));
	FName RightUpperArm = FName(TEXT("upperarm_r"));
	FName RightLowerArm = FName(TEXT("lowerarm_r"));
	FName RightHand = FName(TEXT("hand_r"));

	static FMediaPipeAvatarBoneMap StandardHumanoid();
};

struct MEDIAPIPEDRIVER_API FMediaPipeAvatarLocalViewPolicy
{
	TArray<FString> LocalOnlyCullNameFragments;
	TArray<FName> LocalOnlyHiddenBones;
	bool bAllowSingleMeshComponentCull = false;
	bool bUseSingleMeshFirstPersonBodyProxy = true;

	static FMediaPipeAvatarLocalViewPolicy DefaultHumanoid();
	bool ShouldCullComponentFromLocalView(const UMeshComponent* MeshComponent, int32 AvatarMeshComponentCount) const;
	bool ShouldUseSingleMeshFirstPersonBodyProxy(int32 AvatarMeshComponentCount) const;
	bool ShouldUseFirstPersonBodyProxyForComponent(const UMeshComponent* MeshComponent, int32 AvatarMeshComponentCount) const;
};

struct MEDIAPIPEDRIVER_API FMediaPipeAvatarEmbodimentProfile
{
	FName ProfileId = NAME_None;
	EMediaPipeAvatarSkeletonFamily SkeletonFamily = EMediaPipeAvatarSkeletonFamily::Unknown;
	bool bUseTargetFaceForwardAxis = false;
	float EmbodiedYawOffsetDeg = 0.0f;
	FVector DefaultEyeLocalOffset = FVector(0.0f, 0.0f, 162.0f);
	bool bHasDefaultHeadLocalOffset = false;
	FVector DefaultHeadLocalOffset = FVector::ZeroVector;
	// Eye offset from head expressed in the head semantic basis used by the pose writer.
	bool bHasDefaultEyeLocalInHeadOffset = false;
	FVector DefaultEyeLocalInHeadOffset = FVector::ZeroVector;
	FVector DefaultChestLocalOffset = FVector(0.0f, 0.0f, 108.0f);
	FVector DefaultNeckLocalOffset = FVector(0.0f, 0.0f, 147.0f);
	FVector DefaultNeck02LocalOffset = FVector(0.0f, 0.0f, 154.0f);
	FVector DefaultPelvisLocalOffset = FVector(0.0f, 0.0f, 88.0f);
	float EmbodiedCameraForwardOffsetCm = 0.0f;
	float HeadBoneFromEyeOffsetCm = 8.0f;
	bool bAutoCalibrateUpperBodyFollowAlpha = true;
	float UpperBodyFollowAlpha = 1.0f;
	EMediaPipePelvisAuthorityMode PelvisAuthorityMode = EMediaPipePelvisAuthorityMode::MediaPipeHipsVerticalOnly;
	float ExpectedHeadToChestCm = 54.0f;
	float ExpectedChestToPelvisCm = 58.0f;
	float ExpectedUpperArmLengthCm = 30.0f;
	float ExpectedLowerArmLengthCm = 28.0f;
	float MinUpperArmLengthCm = 18.0f;
	float MaxUpperArmLengthCm = 46.0f;
	float MinLowerArmLengthCm = 18.0f;
	float MaxLowerArmLengthCm = 44.0f;
	float ExpectedThighLengthCm = 43.0f;
	float ExpectedCalfLengthCm = 43.0f;
	float MinThighLengthCm = 28.0f;
	float MaxThighLengthCm = 62.0f;
	float MinCalfLengthCm = 28.0f;
	float MaxCalfLengthCm = 62.0f;
	FMediaPipeAvatarBoneMap BoneMap;
	FMediaPipeAvatarLocalViewPolicy LocalViewPolicy = FMediaPipeAvatarLocalViewPolicy::DefaultHumanoid();

	bool IsValid() const;
};

MEDIAPIPEDRIVER_API FVector ResolveMediaPipeAvatarProfileHeadLocal(
	const FMediaPipeAvatarEmbodimentProfile& Profile);

MEDIAPIPEDRIVER_API FVector ResolveMediaPipeAvatarProfileEyeLocalInHead(
	const FMediaPipeAvatarEmbodimentProfile& Profile);

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

struct MEDIAPIPEDRIVER_API FMediaPipeAvatarEmbodimentSolveInput
{
	FVector DesiredCameraWorld = FVector::ZeroVector;
	FRotator ViewerYawWorld = FRotator::ZeroRotator;
	FMediaPipeAvatarEmbodimentProfile Profile;
	float UserCameraForwardOffsetCm = 0.0f;
	bool bSnapAvatarToGround = true;
	float GroundZ = 0.0f;
	float GroundClearanceCm = 2.0f;
};

struct MEDIAPIPEDRIVER_API FMediaPipeAvatarEmbodimentSolveResult
{
	FVector AvatarWorld = FVector::ZeroVector;
	FRotator AvatarYawWorld = FRotator::ZeroRotator;
	FVector CameraWorld = FVector::ZeroVector;
	FVector ViewerWorld = FVector::ZeroVector;
	FVector AvatarEyeWorld = FVector::ZeroVector;
	FVector AvatarForwardWorld = FVector::ForwardVector;
	float CameraForwardOffsetCm = 0.0f;
	float AvatarEyeHeightCm = 0.0f;
};

struct MEDIAPIPEDRIVER_API FMediaPipeAvatarHmdWristMapInput
{
	FVector QuestAnchorWorld = FVector::ZeroVector;
	FQuat QuestAnchorYawWorld = FQuat::Identity;
	FVector QuestTrackingUpWorld = FVector::UpVector;
	FVector QuestWristWorld = FVector::ZeroVector;
	FTransform TargetCompTransform = FTransform::Identity;
	FMediaPipeAvatarEmbodimentProfile Profile;
	bool bHasProfileEyeLocalOffset = true;
	FVector FallbackEyeWorld = FVector::ZeroVector;
	float UserCameraForwardOffsetCm = 0.0f;
	float PositionScale = 1.0f;
	float MaxOffsetCm = 140.0f;
};

struct MEDIAPIPEDRIVER_API FMediaPipeAvatarHmdWristMapResult
{
	FVector AvatarCameraWorld = FVector::ZeroVector;
	FVector MappedWristWorld = FVector::ZeroVector;
	FVector HmdRelativeWrist = FVector::ZeroVector;
	FVector AvatarForwardWorld = FVector::ForwardVector;
	float CameraForwardOffsetCm = 0.0f;
	bool bOffsetClamped = false;
};

class MEDIAPIPEDRIVER_API FMediaPipeAvatarEmbodimentSolver
{
public:
	static FVector GetAvatarForwardWorld(const FTransform& TargetTransform, const FMediaPipeAvatarEmbodimentProfile& Profile);
	static FVector GetAvatarUpWorld(const FTransform& TargetTransform, const FVector& AvatarForwardWorld);
	static bool SolveCameraAnchoredAvatar(
		const FMediaPipeAvatarEmbodimentSolveInput& Input,
		FMediaPipeAvatarEmbodimentSolveResult& OutResult);
	static bool MapQuestHmdRelativeWristToAvatarWorld(
		const FMediaPipeAvatarHmdWristMapInput& Input,
		FMediaPipeAvatarHmdWristMapResult& OutResult);
};

MEDIAPIPEDRIVER_API FMediaPipeAvatarEmbodimentProfile BuildMediaPipeAvatarEmbodimentProfileFromRigProfile(
	const FMediaPipeAvatarRigProfile& RigProfile);
MEDIAPIPEDRIVER_API FMediaPipeAvatarEmbodimentProfile BuildMediaPipeAvatarEmbodimentProfileFromMetaHumanProfile(
	const FMediaPipeMetaHumanProfileDefinition& MetaHumanProfile);
