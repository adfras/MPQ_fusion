#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"

#include "MediaPipeTrackedSkeletonActor.generated.h"

class UMaterialInterface;
class UEmbodiedFusionComponent;
class UMediaPipePoseTrackerComponent;
class UPoseableMeshComponent;
class USceneComponent;
class USkeletalMesh;
class USkeletalMeshComponent;
class USkinnedMeshComponent;
class UStaticMesh;
class UStaticMeshComponent;
struct FMediaPipePoseFrame;

UENUM(BlueprintType)
enum class EMediaPipeTrackedSkeletonSource : uint8
{
	MediaPipe = 0
};

UENUM(BlueprintType)
enum class EMediaPipeTrackedSkeletonRig : uint8
{
	SimplifiedStick = 0,
	MannyLikeHumanoid = 1,
	AssetSkeletonBoxes = 2
};

UCLASS(BlueprintType, Blueprintable)
class MEDIAPIPEDRIVER_API AMediaPipeTrackedSkeletonActor : public AActor
{
	GENERATED_BODY()

public:
	AMediaPipeTrackedSkeletonActor();

	virtual void Tick(float DeltaSeconds) override;
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void PostRegisterAllComponents() override;
#if WITH_EDITOR
	virtual bool ShouldTickIfViewportsOnly() const override { return true; }
#endif

	UFUNCTION(BlueprintCallable, Category="MediaPipe|Skeleton")
	bool RefreshFromSourcePose();

	UFUNCTION(BlueprintCallable, Category="MediaPipe|Skeleton")
	void SetSkeletonVisible(bool bVisible);

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="MediaPipe|Skeleton")
	USceneComponent* Root = nullptr;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="MediaPipe|Skeleton")
	USkeletalMeshComponent* Body = nullptr;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="MediaPipe|Skeleton")
	UPoseableMeshComponent* PoseDriver = nullptr;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="MediaPipe|Skeleton")
	UEmbodiedFusionComponent* EmbodiedFusionComponent = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MediaPipe|Skeleton")
	EMediaPipeTrackedSkeletonSource TrackingSource = EMediaPipeTrackedSkeletonSource::MediaPipe;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MediaPipe|Skeleton|Source")
	TObjectPtr<AActor> SourceActor = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MediaPipe|Skeleton|Source")
	bool bAutoFindSource = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MediaPipe|Skeleton|Source")
	bool bFollowSourceActorTransform = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MediaPipe|Skeleton|Source")
	FVector SourceRelativeOffset = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MediaPipe|Skeleton|Pose")
	bool bCenterPoseOnFeet = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MediaPipe|Skeleton|Pose")
	FVector PoseLocalOffset = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MediaPipe|Skeleton|Pose", meta=(ClampMin="0.0"))
	float ManualWorldScale = 100.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MediaPipe|Skeleton|Pose")
	bool bManualMirrorLandmarksLR = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MediaPipe|Skeleton|Render")
	TObjectPtr<USkeletalMesh> BodySkeletalMesh = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MediaPipe|Skeleton|Render")
	EMediaPipeTrackedSkeletonRig RigProfile = EMediaPipeTrackedSkeletonRig::SimplifiedStick;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MediaPipe|Skeleton|Render")
	TObjectPtr<UMaterialInterface> OverrideMaterial = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MediaPipe|Skeleton|Render")
	bool bCastShadows = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MediaPipe|Skeleton|Render")
	bool bCollisionEnabled = false;

	UPROPERTY(BlueprintReadOnly, Category="MediaPipe|Skeleton")
	bool bHasValidPose = false;

private:
	struct FBoneReferenceData
	{
		FName BoneName;
		FName ParentName;
		FName PrimaryChildName;
		FName SecondaryChildName;
		FTransform RefComponentTransform = FTransform::Identity;
		FVector RefPrimaryDir = FVector::ZeroVector;
		FVector RefSecondaryDir = FVector::ZeroVector;
		FVector RefParentDir = FVector::ZeroVector;
		FVector RefFacingDir = FVector::ZeroVector;
		bool bHasPrimaryDir = false;
		bool bHasSecondaryDir = false;
		bool bHasParentDir = false;
		bool bHasFacingDir = false;
	};

	FQuat BuildBoneComponentRotation(const FBoneReferenceData& RefData, const TMap<FName, FVector>& JointPositions, bool& bOutValid) const;

	bool EnsureSourceActor();
	void UpdateActorTransformFromSource();
	void RefreshBodyMesh();
	void CacheReferencePoseData();
	void ShowReferencePose();
	UMediaPipePoseTrackerComponent* ResolveMediaPipeTracker() const;
	bool TryGetMediaPipeFrame(FMediaPipePoseFrame& OutFrame) const;
	bool TryBuildMediaPipeLocalJointPositions(TMap<FName, FVector>& OutJointPositions) const;
	bool ApplyPoseLocalJointPositions(const TMap<FName, FVector>& JointPositions);
	bool UsesStandardMannyTrackingPath() const;
	bool HasSelectedTrackingFrame() const;
	void RefreshTrackingPoseBinding();
	void EnsureDebugBoxRenderComponents();
	void UpdateDebugBoxRender(const TMap<FName, FVector>& JointPositions, bool bUseReferencePose);
	void UpdateDebugBoxRenderFromSkinnedMesh(USkinnedMeshComponent* SkinnedMeshComp);
	void HideDebugBoxRender();
	bool ShouldRenderBoxes() const;

	TMap<FName, FBoneReferenceData> BoneReferenceDataByName;
	TWeakObjectPtr<USkeletalMesh> CachedReferenceMesh;
	EMediaPipeTrackedSkeletonRig CachedRigProfile = EMediaPipeTrackedSkeletonRig::SimplifiedStick;
	float LastManualTickDeltaSeconds = 1.0f / 60.0f;
	mutable double LastElbowDiagnosticLogTimeSeconds = -1.0;
	TObjectPtr<UStaticMesh> DebugPrimitiveMesh = nullptr;
	TObjectPtr<UMaterialInterface> DebugPrimitiveMaterial = nullptr;
	TMap<FName, TObjectPtr<UStaticMeshComponent>> DebugJointComponents;
	TMap<FName, TObjectPtr<UStaticMeshComponent>> DebugBoneComponents;
};
