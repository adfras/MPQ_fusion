#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"

#include "MediaPipeFirstPersonBodyProxyComponent.generated.h"

class UPoseableMeshComponent;
class USkeletalMeshComponent;

UCLASS(ClassGroup=(MediaPipe))
class MEDIAPIPEDRIVER_API UMediaPipeFirstPersonBodyProxyComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UMediaPipeFirstPersonBodyProxyComponent();

	void Configure(
		USkeletalMeshComponent* InSourceMesh,
		UPoseableMeshComponent* InBodyProxyMesh,
		const TArray<FName>& InHiddenBones);

	virtual void TickComponent(
		float DeltaTime,
		ELevelTick TickType,
		FActorComponentTickFunction* ThisTickFunction) override;

private:
	void ApplyHiddenBones() const;

	UPROPERTY(Transient)
	TObjectPtr<USkeletalMeshComponent> SourceMesh;

	UPROPERTY(Transient)
	TObjectPtr<UPoseableMeshComponent> BodyProxyMesh;

	UPROPERTY(Transient)
	TArray<FName> HiddenBones;
};
