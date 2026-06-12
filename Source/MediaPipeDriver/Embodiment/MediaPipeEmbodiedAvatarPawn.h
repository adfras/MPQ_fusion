#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"

#include "MediaPipeEmbodiedAvatarPawn.generated.h"

class AMediaPipePoseDrivenSkeletalActor;
class AMediaPipeQuestWebcamSourceActor;
class AMediaPipeVrTrackingPanelActor;
class AActor;
class AController;
struct FMinimalViewInfo;
class UCameraComponent;
class UEmbodiedFusionComponent;
class UMotionControllerComponent;
class UPoseableMeshComponent;
class USceneComponent;
class USkeletalMesh;
struct FEmbodiedFusionBestAvailablePose;
struct FEmbodiedFusionUpperLimbPose;

UENUM(BlueprintType)
enum class EMediaPipeEmbodiedAvatarType : uint8
{
	InternalManny = 0,
	MetaHuman = 1
};

UCLASS(Blueprintable)
class MEDIAPIPEDRIVER_API AMediaPipeEmbodiedAvatarPawn : public APawn
{
	GENERATED_BODY()

public:
	AMediaPipeEmbodiedAvatarPawn();

	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void PossessedBy(AController* NewController) override;
	virtual void PawnClientRestart() override;
	virtual void CalcCamera(float DeltaTime, FMinimalViewInfo& OutResult) override;

	UFUNCTION(BlueprintCallable, Category="MediaPipe|Embodiment")
	void StartEmbodiedTracking(bool bRefreshExistingSource = false);

	bool IsEmbodiedTrackingStarted() const { return bTrackingStarted; }

	UFUNCTION(BlueprintCallable, Category="MediaPipe|Embodiment")
	bool ShouldUseMetaHumanAvatar() const;

	UFUNCTION(BlueprintCallable, Category="MediaPipe|Embodiment")
	FName ResolveMetaHumanProfileId() const;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="MediaPipe|Embodiment")
	USceneComponent* AvatarRoot = nullptr;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="MediaPipe|Embodiment")
	USceneComponent* BodyRoot = nullptr;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="MediaPipe|Embodiment")
	USceneComponent* VROrigin = nullptr;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="MediaPipe|Embodiment")
	UCameraComponent* VRCamera = nullptr;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="MediaPipe|Embodiment")
	UPoseableMeshComponent* AvatarMesh = nullptr;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="MediaPipe|Embodiment")
	UPoseableMeshComponent* LocalAvatarMesh = nullptr;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="MediaPipe|Embodiment")
	UPoseableMeshComponent* MirrorAvatarMesh = nullptr;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="MediaPipe|Embodiment")
	UMotionControllerComponent* MotionControllerLeft = nullptr;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="MediaPipe|Embodiment")
	UMotionControllerComponent* MotionControllerRight = nullptr;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="MediaPipe|Embodiment")
	UEmbodiedFusionComponent* EmbodiedFusionComponent = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MediaPipe|Embodiment")
	bool bStartTrackingOnBeginPlay = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MediaPipe|Embodiment")
	bool bUseMediaPipeTracking = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MediaPipe|Embodiment")
	bool bDriveMovementReplicaPose = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MediaPipe|Embodiment")
	bool bShowMediaPipeSelfView = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MediaPipe|Embodiment")
	EMediaPipeEmbodiedAvatarType AvatarType = EMediaPipeEmbodiedAvatarType::InternalManny;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MediaPipe|Embodiment", meta=(EditCondition="AvatarType == EMediaPipeEmbodiedAvatarType::MetaHuman"))
	FName MetaHumanProfileId = FName(TEXT("Emory"));

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MediaPipe|Embodiment", meta=(ClampMin="80.0", ClampMax="220.0"))
	float FallbackEyeHeightCm = 162.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MediaPipe|Embodiment", meta=(ClampMin="-50.0", ClampMax="75.0"))
	float FallbackCameraForwardOffsetCm = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MediaPipe|Embodiment", meta=(ClampMin="50.0", ClampMax="600.0"))
	float MediaPipeSelfViewPlaneDistanceCm = 240.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MediaPipe|Embodiment", meta=(ClampMin="25.0", ClampMax="200.0"))
	float MediaPipeSelfViewVisibleOffsetCm = 45.0f;

private:
	void ApplySelectedAvatarProfileToRuntimeCVars() const;
	void ApplyEmbodiedTrackingLaunchProfile() const;
	void EnsurePlayerPossession();
	void EnsureCameraHierarchy();
	void ResetPlacedEmbodiedHmdRecenter();
	void UpdatePlacedEmbodiedHmdRecenter();
	FVector GetDesiredCameraWorldLocation() const;
	void UpdateCameraFromActiveProfile();
	void SyncAvatarToPawnRoot(bool bLog);
	bool ShouldUseMovementReplicaAvatar() const;
	void SetMovementReplicaAvatarVisible(bool bVisible);
	void UpdateMediaPipeSelfViewAvatar(bool bLog);
	void UpdateMetaHumanSelfViewAvatar(bool bLog);
	void SetMetaHumanSelfViewAvatarVisible(bool bVisible) const;
	void InitializeMovementReplicaAvatar(bool bLog);
	void CacheMovementReplicaReferencePose();
	void UpdateMovementReplicaAvatarPose();
	void UpdateMovementReplicaMirrorAvatar(bool bLog);
	void CopyMovementReplicaPoseToMirrorAvatar() const;
	void ApplyMovementReplicaPoseToMesh(UPoseableMeshComponent* Mesh, bool bFirstPersonMesh, const FEmbodiedFusionBestAvailablePose& FusionPose);
	void ApplyMovementReplicaReferenceArmPoseToMesh(UPoseableMeshComponent* Mesh, const struct FMediaPipeAvatarEmbodimentProfile& Profile, bool bLeft) const;
	bool ApplyMovementReplicaFusedArmPoseToMesh(UPoseableMeshComponent* Mesh, const struct FMediaPipeAvatarEmbodimentProfile& Profile, const FEmbodiedFusionUpperLimbPose& LimbPose, bool bLeft) const;
	void ApplyMovementReplicaHandTargetArmPoseToMesh(UPoseableMeshComponent* Mesh, const struct FMediaPipeAvatarEmbodimentProfile& Profile, const FEmbodiedFusionUpperLimbPose& LimbPose, bool bLeft) const;
	void ApplyMovementReplicaHandPoseToMesh(UPoseableMeshComponent* Mesh, const struct FMediaPipeAvatarEmbodimentProfile& Profile, const FEmbodiedFusionUpperLimbPose& LimbPose, bool bLeft) const;
	void ApplyMovementReplicaLocalHiddenBones(UPoseableMeshComponent* Mesh) const;
	USkeletalMesh* ResolveMovementReplicaMesh() const;
	void EnsureVrTrackingPanel();
	bool RefreshExistingEmbodiedTrackingSource(UWorld* World);
	AMediaPipeQuestWebcamSourceActor* ResolveOrCreateMediaPipeSourceActor(UWorld* World, const FString& CaptureUrl, const FString& CaptureLabel, bool& bOutSpawnedSourceActor);
	AMediaPipePoseDrivenSkeletalActor* ResolveOrCreatePoseDrivenAvatarActor(UWorld* World, const FTransform& AvatarTransform);
	void ConfigurePoseDrivenAvatarActor(AMediaPipePoseDrivenSkeletalActor* InAvatarDriverActor, AMediaPipeQuestWebcamSourceActor* InSourceActor);
	void ConfigureEmbodiedPresentationTarget(UWorld* World, const FTransform& AvatarTransform);

	UPROPERTY(Transient)
	AMediaPipeQuestWebcamSourceActor* SourceActor = nullptr;

	UPROPERTY(Transient)
	AMediaPipePoseDrivenSkeletalActor* AvatarDriverActor = nullptr;

	UPROPERTY(Transient)
	AActor* MetaHumanSelfViewActor = nullptr;

	UPROPERTY(Transient)
	AMediaPipeVrTrackingPanelActor* TrackingPanelActor = nullptr;

	FVector DesiredCameraLocalOffset = FVector(0.0f, 0.0f, 162.0f);
	double PlacedEmbodiedHmdRecenterStartTimeSeconds = -1.0;
	double PlacedEmbodiedLastHmdSampleTimeSeconds = -1.0;
	double PlacedEmbodiedHmdStableSeconds = 0.0;
	double LastPlacedEmbodiedRecenterLogTimeSeconds = -1.0;
	FVector PlacedEmbodiedLastHmdWorld = FVector::ZeroVector;
	int32 PlacedEmbodiedHmdOriginResetCount = 0;
	bool bHasPlacedEmbodiedLastHmdSample = false;
	bool bPlacedEmbodiedHmdOriginReset = false;
	bool bTrackingStarted = false;
	bool bMovementReplicaReferencePoseCached = false;
	bool bMovementReplicaMirrorRuntimeConfigured = false;
	FTransform MovementReplicaHeadRefCS = FTransform::Identity;
	FTransform MovementReplicaNeckRefCS = FTransform::Identity;
	FTransform MovementReplicaNeck02RefCS = FTransform::Identity;
	TMap<FName, FTransform> MovementReplicaReferencePoseCS;
	TMap<FName, FVector> MovementReplicaReferenceSegmentDirCS;
	TMap<FName, FQuat> MovementReplicaHandVisualBasisRefCS;
	FName MovementReplicaCachedMeshPath = NAME_None;
	mutable double LastMovementReplicaLeftArmLogTimeSeconds = -1.0;
	mutable double LastMovementReplicaRightArmLogTimeSeconds = -1.0;
	mutable double LastMovementReplicaLeftHandLogTimeSeconds = -1.0;
	mutable double LastMovementReplicaRightHandLogTimeSeconds = -1.0;
};
