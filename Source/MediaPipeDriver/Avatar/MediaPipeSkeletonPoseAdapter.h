#pragma once

#include "CoreMinimal.h"
#include "MediaPipeAvatarEmbodimentProfile.h"
#include "MediaPipeBodyFusionAuthorityPolicy.h"
#include "MediaPipeFusedAvatarPose.h"

struct MEDIAPIPEDRIVER_API FMediaPipeSkeletonPoseBinding
{
	FName Root = FName(TEXT("root"));
	FName Pelvis = FName(TEXT("pelvis"));
	FName Spine01 = FName(TEXT("spine_01"));
	FName Spine02 = FName(TEXT("spine_02"));
	FName Spine03 = FName(TEXT("spine_03"));
	FName Spine04 = FName(TEXT("spine_04"));
	FName Spine05 = FName(TEXT("spine_05"));
	FName Neck = FName(TEXT("neck_01"));
	FName Neck02 = FName(TEXT("neck_02"));
	FName Head = FName(TEXT("head"));

	FName ClavicleL = FName(TEXT("clavicle_l"));
	FName UpperArmL = FName(TEXT("upperarm_l"));
	FName UpperArmTwist01L = FName(TEXT("upperarm_twist_01_l"));
	FName UpperArmTwist02L = FName(TEXT("upperarm_twist_02_l"));
	FName LowerArmL = FName(TEXT("lowerarm_l"));
	FName LowerArmTwist01L = FName(TEXT("lowerarm_twist_01_l"));
	FName LowerArmTwist02L = FName(TEXT("lowerarm_twist_02_l"));
	FName HandL = FName(TEXT("hand_l"));

	FName ClavicleR = FName(TEXT("clavicle_r"));
	FName UpperArmR = FName(TEXT("upperarm_r"));
	FName UpperArmTwist01R = FName(TEXT("upperarm_twist_01_r"));
	FName UpperArmTwist02R = FName(TEXT("upperarm_twist_02_r"));
	FName LowerArmR = FName(TEXT("lowerarm_r"));
	FName LowerArmTwist01R = FName(TEXT("lowerarm_twist_01_r"));
	FName LowerArmTwist02R = FName(TEXT("lowerarm_twist_02_r"));
	FName HandR = FName(TEXT("hand_r"));

	FName ThighL = FName(TEXT("thigh_l"));
	FName CalfL = FName(TEXT("calf_l"));
	FName FootL = FName(TEXT("foot_l"));
	FName BallL = FName(TEXT("ball_l"));

	FName ThighR = FName(TEXT("thigh_r"));
	FName CalfR = FName(TEXT("calf_r"));
	FName FootR = FName(TEXT("foot_r"));
	FName BallR = FName(TEXT("ball_r"));

	static FMediaPipeSkeletonPoseBinding Manny();
};

struct MEDIAPIPEDRIVER_API FMediaPipeAvatarPoseWritePlan
{
	TArray<EMediaPipeBodyFusionRegion> OrderedRegions;
	bool bKeepProfileDrivenBoneNames = true;
};

struct MEDIAPIPEDRIVER_API FMediaPipeFusedLowerBodySide
{
	FVector HipWorld = FVector::ZeroVector;
	FVector KneeWorld = FVector::ZeroVector;
	FVector AnkleWorld = FVector::ZeroVector;
	FVector FootWorld = FVector::ZeroVector;
	bool bHasFoot = false;
};

class MEDIAPIPEDRIVER_API FMediaPipeAvatarPoseWriter
{
public:
	static FMediaPipeAvatarPoseWritePlan BuildDefaultWritePlan(const FMediaPipeBodyFusionAuthority& Authority);
	static bool CanWritePose(const FMediaPipeFusedAvatarPose& Pose, const FMediaPipeAvatarEmbodimentProfile& Profile);
	static bool TryResolveChainAlpha(const FVector& Start, const FVector& End, const FVector& Point, float& OutAlpha);
	static void ResolveNeckChainAlphas(float RawNeckAlpha, float RawNeck02Alpha, float& OutNeckAlpha, float& OutNeck02Alpha);
	static bool TryResolveSemanticBoneRotationCS(
		const FQuat& RefBoneComp,
		const FQuat& RefBasisComp,
		const FQuat& TargetBasisComp,
		FQuat& OutRotCS);
	static bool TryGetMediaPipeLowerBodySide(
		const FMediaPipeFusedAvatarPose& Pose,
		bool bIsLeft,
		FMediaPipeFusedLowerBodySide& OutSide);
};
