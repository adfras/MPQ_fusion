#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"

#include "MediaPipePoseDrivenSkeletalActor.generated.h"

class AActor;
class UControlRigComponent;
class UMediaPipePoseDrivenAnimInstance;
class USkeletalMeshComponent;

UCLASS()
class MEDIAPIPEDRIVER_API AMediaPipePoseDrivenSkeletalActor : public AActor
{
	GENERATED_BODY()

public:
	AMediaPipePoseDrivenSkeletalActor();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void PostRegisterAllComponents() override;
#if WITH_EDITOR
	virtual bool ShouldTickIfViewportsOnly() const override { return true; }
#endif

	void SetPresentationActor(AActor* InPresentationActor, USkeletalMeshComponent* InPresentationMesh);
	void SyncPresentationActorTransform() const;
	USkeletalMeshComponent* GetDrivenMesh() const;

	UPROPERTY(VisibleAnywhere, Category="MediaPipe")
	USkeletalMeshComponent* Mesh = nullptr;

	UPROPERTY(VisibleAnywhere, Category="MediaPipe")
	UControlRigComponent* MannyBodyRig = nullptr;

	UPROPERTY(EditAnywhere, Category="MediaPipe")
	AActor* Source = nullptr;

	UPROPERTY(EditAnywhere, Category="MediaPipe")
	bool bAutoPositionNextToSource = true;

	UPROPERTY(EditAnywhere, Category="MediaPipe")
	bool bAutoAlignYawToPose = true;

	UPROPERTY(EditAnywhere, Category="MediaPipe")
	float SideOffset = 250.0f;

	UPROPERTY(EditAnywhere, Category="MediaPipe")
	float YawOffsetDeg = 0.0f;

private:
	bool EnsureMannyBodyRig();
	bool EnsureSource();
	bool TryGetMediaPipeFrame(struct FMediaPipePoseFrame& OutFrame) const;
	bool TryGetTrackerSettings(float& OutWorldScale, bool& OutMirrorLandmarks) const;
	bool TryGetLandmarkWorld(const struct FMediaPipePoseFrame& Frame, int32 LandmarkIndex, FVector& OutWorld) const;
	bool TryGetPoseYawWorld(const struct FMediaPipePoseFrame& Frame, float& OutYawDeg);
	void SetMannyPresentationVisible(bool bVisible);

	bool bMannyBodyRigMapped = false;
	bool bHasStablePoseYawForwardWorld = false;
	FVector StablePoseYawForwardWorld = FVector::ZeroVector;
	bool bHasLastPoseYawTimestamp = false;
	int64 LastPoseYawTimestampUs = 0;

	UPROPERTY(Transient)
	AActor* PresentationActor = nullptr;

	UPROPERTY(Transient)
	USkeletalMeshComponent* PresentationMesh = nullptr;
};
