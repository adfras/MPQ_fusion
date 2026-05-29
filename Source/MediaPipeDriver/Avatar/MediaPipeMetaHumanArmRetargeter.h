#pragma once

#include "CoreMinimal.h"
#include "MediaPipeFullArmChainProvider.h"
#include "MediaPipeMetaHumanProfile.h"

struct MEDIAPIPEDRIVER_API FMediaPipeMetaHumanFullArmChainRetargetInput
{
	const FMediaPipeResolvedMetaHumanTarget* Target = nullptr;
	const FMediaPipeFullArmChainSnapshot* Snapshot = nullptr;
	FTransform TargetComponentTransform = FTransform::Identity;
	bool bIsLeft = false;
	double NowSeconds = 0.0;
	float MaxAgeSeconds = 0.25f;
	float FallbackUpperArmLengthCm = 0.0f;
	float FallbackLowerArmLengthCm = 0.0f;
};

struct MEDIAPIPEDRIVER_API FMediaPipeMetaHumanArmTargetPose
{
	bool bValid = false;
	FVector SourceShoulderWorld = FVector::ZeroVector;
	FVector SourceElbowWorld = FVector::ZeroVector;
	FVector SourceWristWorld = FVector::ZeroVector;
	FVector ShoulderWorld = FVector::ZeroVector;
	FVector ElbowWorld = FVector::ZeroVector;
	FVector WristWorld = FVector::ZeroVector;
	FVector ShoulderComp = FVector::ZeroVector;
	FVector ElbowComp = FVector::ZeroVector;
	FVector WristComp = FVector::ZeroVector;
	FVector UpperDirWorld = FVector::ZeroVector;
	FVector LowerDirWorld = FVector::ZeroVector;
	FVector UpperDirComp = FVector::ZeroVector;
	FVector LowerDirComp = FVector::ZeroVector;
	float UpperArmLengthCm = 0.0f;
	float LowerArmLengthCm = 0.0f;
	float TargetReachCm = 0.0f;
	float SourceReachCm = 0.0f;
	float ElbowBendDeg = 0.0f;
};

struct MEDIAPIPEDRIVER_API FMediaPipeMetaHumanFullArmChainRetargetResult
{
	bool bFresh = false;
	float ChainAgeSeconds = -1.0f;
	FVector ShoulderWorld = FVector::ZeroVector;
	FVector ElbowWorld = FVector::ZeroVector;
	FVector WristWorld = FVector::ZeroVector;
	FMediaPipeMetaHumanArmTargetPose TargetPose;
	FString RejectReason;
};

class MEDIAPIPEDRIVER_API FMediaPipeMetaHumanFullArmChainRetargeter
{
public:
	static FMediaPipeMetaHumanFullArmChainRetargetResult ResolveFullArmChainSource(
		const FMediaPipeMetaHumanFullArmChainRetargetInput& Input);
};
