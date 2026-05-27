#include "MediaPipeFirstPersonBodyProxyComponent.h"

#include "Components/PoseableMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"

UMediaPipeFirstPersonBodyProxyComponent::UMediaPipeFirstPersonBodyProxyComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	PrimaryComponentTick.TickGroup = TG_PostUpdateWork;
	SetComponentTickEnabled(true);
}

void UMediaPipeFirstPersonBodyProxyComponent::Configure(
	USkeletalMeshComponent* InSourceMesh,
	UPoseableMeshComponent* InBodyProxyMesh,
	const TArray<FName>& InHiddenBones)
{
	SourceMesh = InSourceMesh;
	BodyProxyMesh = InBodyProxyMesh;
	HiddenBones = InHiddenBones;
	SetComponentTickEnabled(SourceMesh != nullptr && BodyProxyMesh != nullptr);

	if (SourceMesh)
	{
		PrimaryComponentTick.AddPrerequisite(SourceMesh, SourceMesh->PrimaryComponentTick);
	}

	ApplyHiddenBones();
}

void UMediaPipeFirstPersonBodyProxyComponent::TickComponent(
	const float DeltaTime,
	const ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!SourceMesh || !BodyProxyMesh || !SourceMesh->GetSkeletalMeshAsset())
	{
		return;
	}

	if (BodyProxyMesh->GetSkinnedAsset() != SourceMesh->GetSkeletalMeshAsset())
	{
		BodyProxyMesh->SetSkinnedAssetAndUpdate(SourceMesh->GetSkeletalMeshAsset());
	}

	BodyProxyMesh->CopyPoseFromSkeletalComponent(SourceMesh);
	ApplyHiddenBones();
}

void UMediaPipeFirstPersonBodyProxyComponent::ApplyHiddenBones() const
{
	if (!BodyProxyMesh)
	{
		return;
	}

	for (const FName& BoneName : HiddenBones)
	{
		if (BoneName == NAME_None || BodyProxyMesh->GetBoneIndex(BoneName) == INDEX_NONE)
		{
			continue;
		}

		BodyProxyMesh->HideBoneByName(BoneName, EPhysBodyOp::PBO_None);
		BodyProxyMesh->SetBoneScaleByName(BoneName, FVector::ZeroVector, EBoneSpaces::ComponentSpace);
	}
}
