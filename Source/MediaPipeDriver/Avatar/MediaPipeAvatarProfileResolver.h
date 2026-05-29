#pragma once

#include "CoreMinimal.h"
#include "MediaPipeAvatarEmbodimentProfile.h"
#include "MediaPipeMetaHumanProfile.h"

class USkeletalMeshComponent;

struct MEDIAPIPEDRIVER_API FMediaPipeResolvedAvatarProfile
{
	FName TargetActorName = NAME_None;
	FMediaPipeAvatarEmbodimentProfile EmbodimentProfile;
	bool bHasEmbodimentProfile = false;
	bool bUseTargetFaceForwardAxis = false;
	bool bHasTargetEyeLocalOffset = false;
	FVector TargetEyeLocalOffset = FVector::ZeroVector;
	float TargetEmbodiedCameraForwardOffsetCm = 0.0f;
	FMediaPipeResolvedMetaHumanTarget MetaHumanProfile;

	void Reset();
	void SetEmbodimentProfile(const FMediaPipeAvatarEmbodimentProfile& Profile);
};

struct MEDIAPIPEDRIVER_API FMediaPipeAvatarProfileResolverLogState
{
	TMap<uint32, FString> LastMetaHumanProfileLogStateByRuntimeKey;
	TMap<uint32, double> LastMetaHumanProfileLogTimeByRuntimeKey;
	TMap<uint32, double> LastMetaHumanValidationLogTimeByRuntimeKey;

	void ResetRuntimeKey(uint32 RuntimeKey);
};

class MEDIAPIPEDRIVER_API FMediaPipeAvatarProfileResolver
{
public:
	static FMediaPipeResolvedAvatarProfile ResolveForComponent(const USkeletalMeshComponent* MeshComponent);
	static void EmitMetaHumanProfileLogs(
		const FMediaPipeResolvedAvatarProfile& ResolvedProfile,
		uint32 RuntimeKey,
		double NowSeconds,
		FMediaPipeAvatarProfileResolverLogState& InOutLogState);
};
