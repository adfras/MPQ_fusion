#include "MediaPipeAvatarProfileResolver.h"

#include "MediaPipeAvatarRigProfile.h"
#include "MediaPipePoseLog.h"

#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Actor.h"

void FMediaPipeResolvedAvatarProfile::Reset()
{
	TargetActorName = NAME_None;
	EmbodimentProfile = FMediaPipeAvatarEmbodimentProfile();
	bHasEmbodimentProfile = false;
	bUseTargetFaceForwardAxis = false;
	bHasTargetEyeLocalOffset = false;
	TargetEyeLocalOffset = FVector::ZeroVector;
	TargetEmbodiedCameraForwardOffsetCm = 0.0f;
	MetaHumanProfile.Reset();
}

void FMediaPipeResolvedAvatarProfile::SetEmbodimentProfile(const FMediaPipeAvatarEmbodimentProfile& Profile)
{
	EmbodimentProfile = Profile;
	bHasEmbodimentProfile = EmbodimentProfile.IsValid();
	bUseTargetFaceForwardAxis = EmbodimentProfile.bUseTargetFaceForwardAxis;
	TargetEyeLocalOffset = EmbodimentProfile.DefaultEyeLocalOffset;
	bHasTargetEyeLocalOffset = !TargetEyeLocalOffset.ContainsNaN();
	TargetEmbodiedCameraForwardOffsetCm = EmbodimentProfile.EmbodiedCameraForwardOffsetCm;
}

void FMediaPipeAvatarProfileResolverLogState::ResetRuntimeKey(const uint32 RuntimeKey)
{
	LastMetaHumanProfileLogStateByRuntimeKey.Remove(RuntimeKey);
	LastMetaHumanProfileLogTimeByRuntimeKey.Remove(RuntimeKey);
	LastMetaHumanValidationLogTimeByRuntimeKey.Remove(RuntimeKey);
}

FMediaPipeResolvedAvatarProfile FMediaPipeAvatarProfileResolver::ResolveForComponent(
	const USkeletalMeshComponent* MeshComponent)
{
	FMediaPipeResolvedAvatarProfile ResolvedProfile;
	ResolvedProfile.Reset();
	if (!MeshComponent)
	{
		return ResolvedProfile;
	}

	if (AActor* TargetActor = MeshComponent->GetOwner())
	{
		ResolvedProfile.TargetActorName = FName(*TargetActor->GetActorNameOrLabel());
	}

	if (ResolveMediaPipeMetaHumanProfileForComponent(
		const_cast<USkeletalMeshComponent*>(MeshComponent),
		ResolvedProfile.MetaHumanProfile))
	{
		ResolvedProfile.SetEmbodimentProfile(
			BuildMediaPipeAvatarEmbodimentProfileFromMetaHumanProfile(ResolvedProfile.MetaHumanProfile.Profile));
		ResolvedProfile.TargetActorName = ResolvedProfile.MetaHumanProfile.TargetActorName.IsNone()
			? ResolvedProfile.TargetActorName
			: ResolvedProfile.MetaHumanProfile.TargetActorName;
		return ResolvedProfile;
	}

	FMediaPipeAvatarRigProfile AvatarRigProfile;
	if (TryResolveMediaPipeAvatarRigProfileForMesh(MeshComponent->GetSkeletalMeshAsset(), AvatarRigProfile))
	{
		ResolvedProfile.SetEmbodimentProfile(BuildMediaPipeAvatarEmbodimentProfileFromRigProfile(AvatarRigProfile));
	}

	return ResolvedProfile;
}

void FMediaPipeAvatarProfileResolver::EmitMetaHumanProfileLogs(
	const FMediaPipeResolvedAvatarProfile& ResolvedProfile,
	const uint32 RuntimeKey,
	const double NowSeconds,
	FMediaPipeAvatarProfileResolverLogState& InOutLogState)
{
	const FMediaPipeResolvedMetaHumanTarget& Target = ResolvedProfile.MetaHumanProfile;
	if (Target.bIsMetaHuman)
	{
		const FString CurrentProfileLogState = FString::Printf(
			TEXT("%s:%d:%d:%s"),
			*Target.ProfileId.ToString(),
			Target.bIsActiveProfile ? 1 : 0,
			Target.bValidationPassed ? 1 : 0,
			*Target.ValidationSummary);
		FString& LastProfileLogState =
			InOutLogState.LastMetaHumanProfileLogStateByRuntimeKey.FindOrAdd(RuntimeKey);
		double& LastProfileLogTime =
			InOutLogState.LastMetaHumanProfileLogTimeByRuntimeKey.FindOrAdd(RuntimeKey, -1.0);
		if (LastProfileLogTime < 0.0 ||
			!LastProfileLogState.Equals(CurrentProfileLogState, ESearchCase::CaseSensitive) ||
			NowSeconds - LastProfileLogTime >= 5.0)
		{
			LastProfileLogState = CurrentProfileLogState;
			LastProfileLogTime = NowSeconds;
			UE_LOG(LogMediaPipePose, Log, TEXT("%s"), *FormatMediaPipeMetaHumanProfileResolutionLog(Target));
		}
	}

	if (Target.bIsMetaHuman && !Target.bValidationPassed)
	{
		double& LastValidationLogTime =
			InOutLogState.LastMetaHumanValidationLogTimeByRuntimeKey.FindOrAdd(RuntimeKey, -1.0);
		if (LastValidationLogTime < 0.0 || NowSeconds - LastValidationLogTime >= 5.0)
		{
			LastValidationLogTime = NowSeconds;
			UE_LOG(LogMediaPipePose, Warning, TEXT("mp.MetaHumanProfile: invalid profile=%s actor=%s validation=\"%s\"; full-chain MetaHuman solve is disabled until the target validates."),
				*Target.ProfileId.ToString(),
				*ResolvedProfile.TargetActorName.ToString(),
				*Target.ValidationSummary);
		}
	}
}
