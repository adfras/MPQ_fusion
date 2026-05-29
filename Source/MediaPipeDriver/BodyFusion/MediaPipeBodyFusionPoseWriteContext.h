#pragma once

#include "CoreMinimal.h"
#include "MediaPipeAvatarEmbodimentProfile.h"
#include "MediaPipeFusedAvatarPose.h"

struct MEDIAPIPEDRIVER_API FMediaPipeBodyFusionPoseWriteContextInput
{
	const FMediaPipeFusedAvatarPose* Pose = nullptr;
	FTransform TargetComponentToWorld = FTransform::Identity;
	FMediaPipeAvatarEmbodimentProfile Profile;
	FVector ResolvedPelvisComp = FVector::ZeroVector;
	bool bHasResolvedPelvisComp = false;
	FVector RefChestPosComp = FVector::ZeroVector;
	FVector RefHeadPosComp = FVector::ZeroVector;
	FVector RefNeckPosComp = FVector::ZeroVector;
	FVector RefNeck02PosComp = FVector::ZeroVector;
	bool bHasRefChestPosComp = false;
	bool bHasRefNeck02PosComp = false;
};

struct MEDIAPIPEDRIVER_API FMediaPipeBodyFusionPoseWriteContext
{
	FTransform ComponentToWorld = FTransform::Identity;
	FTransform WorldToComponent = FTransform::Identity;
	FMediaPipeAvatarEmbodimentProfile Profile;

	FVector PelvisComp = FVector::ZeroVector;
	FVector ChestComp = FVector::ZeroVector;
	FVector HeadComp = FVector::ZeroVector;
	FVector UpComp = FVector::UpVector;
	FVector ForwardHintComp = FVector::ForwardVector;
	FVector RightComp = FVector::RightVector;

	FQuat PelvisTargetBasis = FQuat::Identity;
	FQuat ChestTargetBasis = FQuat::Identity;
	FQuat HeadTargetBasis = FQuat::Identity;
	FQuat HmdRotationComp = FQuat::Identity;
	FVector HmdForwardComp = FVector::ForwardVector;
	FVector HmdUpComp = FVector::UpVector;
	FVector HmdRightComp = FVector::RightVector;

	float RefNeckAlpha = 0.0f;
	float RefNeck02Alpha = 0.0f;
	bool bHasTorsoTargets = false;
	bool bHasNeckChainTargets = false;
	bool bHmdHeadAuthoritative = false;
};

class MEDIAPIPEDRIVER_API FMediaPipeBodyFusionPoseWriteContextBuilder
{
public:
	static FQuat MakeBasisFromAxes(
		const FVector& Right,
		const FVector& Up,
		const FVector& ForwardHint);
	static FQuat MakeBasis(
		const FVector& Right,
		const FVector& Up,
		const FVector& ForwardHint);
	static bool Build(
		const FMediaPipeBodyFusionPoseWriteContextInput& Input,
		FMediaPipeBodyFusionPoseWriteContext& OutContext);
};
