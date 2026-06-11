#include "MediaPipePoseDrivenSkeletalActor.h"

#include "EmbodiedFusionComponent.h"
#include "MediaPipeTrackedSkeletonActor.h"
#include "MediaPipePoseDrivenAnimInstance.h"
#include "MediaPipeBodyFusionRuntime.h"
#include "MediaPipePoseLog.h"
#include "MediaPipeSolvedPose.h"
#include "MediaPipePoseTrackerComponent.h"
#include "MediaPipePoseTypes.h"
#include "MediaPipeRuntimeCVars.h"
#include "MediaPipeStage2ShoulderEvidence.h"
#include "MediaPipeTrackingFusionDataset.h"

#include "Animation/AnimInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/Engine.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "GameFramework/PlayerController.h"
#include "Dom/JsonObject.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformProcess.h"
#include "Materials/MaterialInterface.h"
#include "MediaPlayer.h"
#include "Misc/App.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "ReferenceSkeleton.h"
#include "Serialization/Archive.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UObject/UObjectGlobals.h"

#include <initializer_list>

#include "MediaPipeCaptureRecorders.h"

namespace
{
int32 ConfigurePresentationSkeletalFollowers(AActor* PresentationActor, USkeletalMeshComponent* PresentationMesh)
{
	if (!PresentationActor || !PresentationMesh)
	{
		return 0;
	}

	TArray<USkeletalMeshComponent*> SkeletalComponents;
	PresentationActor->GetComponents<USkeletalMeshComponent>(SkeletalComponents);

	int32 FollowerCount = 0;
	for (USkeletalMeshComponent* SkeletalComponent : SkeletalComponents)
	{
		if (!SkeletalComponent || SkeletalComponent == PresentationMesh || !SkeletalComponent->GetSkeletalMeshAsset())
		{
			continue;
		}

		SkeletalComponent->SetLeaderPoseComponent(PresentationMesh, true, true);
		SkeletalComponent->bTickInEditor = true;
		SkeletalComponent->PrimaryComponentTick.bStartWithTickEnabled = true;
		SkeletalComponent->SetComponentTickEnabled(true);
		SkeletalComponent->bEnableUpdateRateOptimizations = false;
		SkeletalComponent->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
		++FollowerCount;
	}

	return FollowerCount;
}

float ResolveGroundZ(UWorld* World, const FVector& Location, const float FallbackZ)
{
	if (!World)
	{
		return FallbackZ;
	}

	FHitResult Hit;
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(MediaPipeLiveMannyBeginPlayPlacement), false);
	const FVector TraceStart(Location.X, Location.Y, Location.Z + 120.0f);
	const FVector TraceEnd(Location.X, Location.Y, Location.Z - 3000.0f);
	if (World->LineTraceSingleByChannel(Hit, TraceStart, TraceEnd, ECC_WorldStatic, QueryParams))
	{
		return Hit.ImpactPoint.Z;
	}

	return FallbackZ;
}

void PlaceLiveMannyInFrontOfPlayer(AMediaPipePoseDrivenSkeletalActor* Actor)
{
	if (!Actor || !Actor->Tags.Contains(MediaPipeCaptureRecorders::LiveMannyTag))
	{
		return;
	}

	UWorld* World = Actor->GetWorld();
	if (!World || (World->WorldType != EWorldType::PIE && World->WorldType != EWorldType::Game))
	{
		return;
	}

	APlayerController* PlayerController = World->GetFirstPlayerController();
	if (!PlayerController)
	{
		return;
	}

	FVector ViewLocation = FVector::ZeroVector;
	FRotator ViewRotation = FRotator::ZeroRotator;
	PlayerController->GetPlayerViewPoint(ViewLocation, ViewRotation);

	FVector Forward = FRotationMatrix(FRotator(0.0f, ViewRotation.Yaw, 0.0f)).GetUnitAxis(EAxis::X).GetSafeNormal();
	if (Forward.IsNearlyZero())
	{
		Forward = FVector::ForwardVector;
	}

	FVector DesiredLocation = ViewLocation + Forward * 350.0f;
	DesiredLocation.Z = ResolveGroundZ(World, DesiredLocation, 0.0f) + 2.0f;

	const FVector ToViewer = (ViewLocation - DesiredLocation).GetSafeNormal();
	FRotator DesiredRotation = Actor->GetActorRotation();
	if (!ToViewer.IsNearlyZero())
	{
		DesiredRotation = ToViewer.Rotation();
		DesiredRotation.Pitch = 0.0f;
		DesiredRotation.Roll = 0.0f;
	}

	Actor->SetActorLocationAndRotation(DesiredLocation, DesiredRotation);
}

UMediaPipePoseTrackerComponent* ResolveMediaPipeTracker(const AActor* SourceActor)
{
	if (const AActor* TrackingSourceActor = MediaPipeCaptureRecorders::ResolveTrackingSourceActor(SourceActor))
	{
		return TrackingSourceActor->FindComponentByClass<UMediaPipePoseTrackerComponent>();
	}
	return nullptr;
}

FVector LockVectorToHemisphere(const FVector& Vector, const FVector& Reference)
{
	const FVector Normalized = Vector.GetSafeNormal();
	const FVector Ref = Reference.GetSafeNormal();
	if (Normalized.IsNearlyZero() || Ref.IsNearlyZero())
	{
		return Normalized;
	}

	return FVector::DotProduct(Normalized, Ref) < 0.0f ? -Normalized : Normalized;
}

bool HasExpectedTrackingSource(const AActor* SourceActor)
{
	return ResolveMediaPipeTracker(SourceActor) != nullptr;
}

#if WITH_EDITOR
void EnsureSkeletalMeshComponentUpdatesInEditor(USkeletalMeshComponent* MeshComponent)
{
	if (!MeshComponent)
	{
		return;
	}

	const UWorld* World = MeshComponent->GetWorld();
	if (!World || (World->WorldType != EWorldType::Editor && World->WorldType != EWorldType::EditorPreview))
	{
		return;
	}

	MeshComponent->SetUpdateAnimationInEditor(true);
	MeshComponent->bTickInEditor = true;
	MeshComponent->PrimaryComponentTick.bStartWithTickEnabled = true;
	MeshComponent->SetComponentTickEnabled(true);
}
#endif

USkeletalMesh* TryLoadSkeletalMeshFallback()
{
	const TCHAR* const CandidatePaths[] = {
		TEXT("/Game/MediaPipe/MediaPipeRig/SK_MediaPipeMannyLike.SK_MediaPipeMannyLike"),
		TEXT("/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple.SKM_Manny_Simple"),
		TEXT("/Game/Characters/Mannequins/Meshes/SKM_Quinn_Simple.SKM_Quinn_Simple"),
		TEXT("/Game/Characters/Mannequins/Meshes/SK_Mannequin.SK_Mannequin"),
	};

	for (const TCHAR* CandidatePath : CandidatePaths)
	{
		if (USkeletalMesh* MeshAsset = LoadObject<USkeletalMesh>(nullptr, CandidatePath))
		{
			return MeshAsset;
		}
	}

	return nullptr;
}

void ApplyReadableMannyMaterials(USkeletalMeshComponent* MeshComponent)
{
	if (!MeshComponent || !MeshComponent->GetSkeletalMeshAsset())
	{
		return;
	}

	const FString MeshPath = MeshComponent->GetSkeletalMeshAsset()->GetPathName();
	if (!MeshPath.Contains(TEXT("Manny"), ESearchCase::IgnoreCase) &&
		!MeshPath.Contains(TEXT("MediaPipeMannyLike"), ESearchCase::IgnoreCase))
	{
		return;
	}

	static UMaterialInterface* BodyMaterial = LoadObject<UMaterialInterface>(
		nullptr,
		TEXT("/Game/MCPBench/Materials/M_MannyReadable_Slate.M_MannyReadable_Slate"));
	static UMaterialInterface* AccentMaterial = LoadObject<UMaterialInterface>(
		nullptr,
		TEXT("/Game/MCPBench/Materials/M_MannyReadable_Accent.M_MannyReadable_Accent"));
	if (!BodyMaterial && !AccentMaterial)
	{
		return;
	}

	const int32 MaterialCount = MeshComponent->GetNumMaterials();
	for (int32 MaterialIndex = 0; MaterialIndex < MaterialCount; ++MaterialIndex)
	{
		UMaterialInterface* Material = MaterialIndex == 1 && AccentMaterial ? AccentMaterial : BodyMaterial;
		if (Material && MeshComponent->GetMaterial(MaterialIndex) != Material)
		{
			MeshComponent->SetMaterial(MaterialIndex, Material);
		}
	}
}
}

AMediaPipePoseDrivenSkeletalActor::AMediaPipePoseDrivenSkeletalActor()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;

	Mesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("Mesh"));
	SetRootComponent(Mesh);

	DefaultEmbodiedFusionComponent = CreateDefaultSubobject<UEmbodiedFusionComponent>(TEXT("EmbodiedFusion"));

	Mesh->SetAnimationMode(EAnimationMode::AnimationBlueprint);
	Mesh->SetAnimInstanceClass(UMediaPipePoseDrivenAnimInstance::StaticClass());
	Mesh->bTickInEditor = true;
	Mesh->PrimaryComponentTick.bStartWithTickEnabled = true;
	Mesh->SetComponentTickEnabled(true);
	Mesh->bEnableUpdateRateOptimizations = false;
	Mesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;

	if (USkeletalMesh* FallbackMesh = TryLoadSkeletalMeshFallback())
	{
		Mesh->SetSkinnedAssetAndUpdate(FallbackMesh);
		ApplyReadableMannyMaterials(Mesh);
	}
}

void AMediaPipePoseDrivenSkeletalActor::BeginPlay()
{
	Super::BeginPlay();
	PlaceLiveMannyInFrontOfPlayer(this);
}

void AMediaPipePoseDrivenSkeletalActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (Tags.Contains(MediaPipeCaptureRecorders::LiveMannyTag))
	{
		MediaPipeCaptureRecorders::StopMannyBoneTimeseries(EMannyBoneTimeseriesEndReason::EndPlay);
		MediaPipeCaptureRecorders::StopTrackingFusionDataset(ETrackingFusionDatasetEndReason::EndPlay);
	}

	Super::EndPlay(EndPlayReason);
}

void AMediaPipePoseDrivenSkeletalActor::PostRegisterAllComponents()
{
	Super::PostRegisterAllComponents();
#if WITH_EDITOR
	EnsureSkeletalMeshComponentUpdatesInEditor(Mesh);
#endif
	ApplyReadableMannyMaterials(Mesh);
	if (Mesh)
	{
		Mesh->PrimaryComponentTick.AddPrerequisite(this, PrimaryActorTick);
	}
}

void AMediaPipePoseDrivenSkeletalActor::SetPresentationActor(AActor* InPresentationActor, USkeletalMeshComponent* InPresentationMesh)
{
	PresentationActor = InPresentationActor;
	PresentationMesh = InPresentationMesh;

	if (Mesh)
	{
		const bool bUseExternalMesh = PresentationMesh != nullptr;
		Mesh->SetHiddenInGame(bUseExternalMesh);
		Mesh->SetVisibility(!bUseExternalMesh, true);
		Mesh->SetCollisionEnabled(bUseExternalMesh ? ECollisionEnabled::NoCollision : ECollisionEnabled::QueryOnly);
	}

	if (PresentationMesh)
	{
		PresentationMesh->bTickInEditor = true;
		PresentationMesh->PrimaryComponentTick.bStartWithTickEnabled = true;
		PresentationMesh->SetComponentTickEnabled(true);
		PresentationMesh->bEnableUpdateRateOptimizations = false;
		PresentationMesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
		PresentationMesh->SetDisablePostProcessBlueprint(false);
		ApplyReadableMannyMaterials(PresentationMesh);

		const USkeletalMesh* PresentationSkeletalMesh = PresentationMesh->GetSkeletalMeshAsset();
		const TSubclassOf<UAnimInstance> PostProcessClass = PresentationMesh->GetPostProcessAnimBPClassToBeUsed();
		const int32 PresentationFollowerCount =
			ConfigurePresentationSkeletalFollowers(PresentationActor, PresentationMesh);
		UE_LOG(
			LogMediaPipePose,
			Log,
			TEXT("Auto Quest presentation mesh: driver=%s presentationActor=%s mesh=%s asset=%s animClass=%s postProcessClass=%s postProcessDisabled=%d skeletalFollowers=%d"),
			*GetNameSafe(this),
			*GetNameSafe(PresentationActor),
			*PresentationMesh->GetName(),
			*GetNameSafe(PresentationSkeletalMesh),
			*GetNameSafe(PresentationMesh->GetAnimClass()),
			*GetNameSafe(PostProcessClass.Get()),
			PresentationMesh->GetDisablePostProcessBlueprint() ? 1 : 0,
			PresentationFollowerCount);

		if (PrimaryActorTick.bCanEverTick)
		{
			PresentationMesh->PrimaryComponentTick.AddPrerequisite(this, PrimaryActorTick);
		}
	}
	if (PresentationActor)
	{
		TArray<UPrimitiveComponent*> PrimitiveComponents;
		PresentationActor->GetComponents<UPrimitiveComponent>(PrimitiveComponents);
		for (UPrimitiveComponent* PrimitiveComponent : PrimitiveComponents)
		{
			if (PrimitiveComponent)
			{
				PrimitiveComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			}
		}
	}

	SyncPresentationActorTransform();
}

void AMediaPipePoseDrivenSkeletalActor::SetEmbodiedFusionComponent(UEmbodiedFusionComponent* InFusionComponent)
{
	ExternalEmbodiedFusionComponent = InFusionComponent;
	if (USkeletalMeshComponent* DrivenMesh = GetDrivenMesh())
	{
		if (UMediaPipePoseDrivenAnimInstance* MediaPipeAnim = Cast<UMediaPipePoseDrivenAnimInstance>(DrivenMesh->GetAnimInstance()))
		{
			MediaPipeAnim->SetEmbodiedFusionComponent(GetActiveEmbodiedFusionComponent());
		}
	}
}

UEmbodiedFusionComponent* AMediaPipePoseDrivenSkeletalActor::GetActiveEmbodiedFusionComponent() const
{
	return ExternalEmbodiedFusionComponent ? ExternalEmbodiedFusionComponent : DefaultEmbodiedFusionComponent;
}

void AMediaPipePoseDrivenSkeletalActor::SyncPresentationActorTransform() const
{
	if (PresentationActor)
	{
		PresentationActor->SetActorLocationAndRotation(GetActorLocation(), GetActorRotation());
		PresentationActor->SetActorScale3D(GetActorScale3D());
	}
}

bool AMediaPipePoseDrivenSkeletalActor::EnsureSource()
{
	if (Source && HasExpectedTrackingSource(Source))
	{
		return true;
	}
	Source = nullptr;

	UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}

	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Candidate = *It;
		if (!Candidate)
		{
			continue;
		}

		if (Cast<AMediaPipeTrackedSkeletonActor>(Candidate) && ResolveMediaPipeTracker(Candidate))
		{
			Source = Candidate;
			return true;
		}
	}

	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Candidate = *It;
		if (!Candidate)
		{
			continue;
		}

		if (ResolveMediaPipeTracker(Candidate))
		{
			Source = Candidate;
			return true;
		}
	}

	return false;
}

bool AMediaPipePoseDrivenSkeletalActor::TryGetMediaPipeFrame(FMediaPipePoseFrame& OutFrame) const
{
	if (UMediaPipePoseTrackerComponent* TrackerComp = ResolveMediaPipeTracker(Source))
	{
		return TrackerComp->GetLatestFrame(OutFrame) && OutFrame.bValid;
	}

	return false;
}

bool AMediaPipePoseDrivenSkeletalActor::TryGetTrackerSettings(float& OutWorldScale, bool& OutMirrorLandmarks) const
{
	if (UMediaPipePoseTrackerComponent* TrackerComp = ResolveMediaPipeTracker(Source))
	{
		OutWorldScale = TrackerComp->WorldScale;
		OutMirrorLandmarks = TrackerComp->bMirrorLandmarksLR;
		return true;
	}

	return false;
}

bool AMediaPipePoseDrivenSkeletalActor::TryGetLandmarkWorld(const FMediaPipePoseFrame& Frame, int32 LandmarkIndex, FVector& OutWorld) const
{
	float WorldScale = 100.0f;
	bool bMirrorLandmarks = true;
	const AActor* TrackingSourceActor = MediaPipeCaptureRecorders::ResolveTrackingSourceActor(Source);
	if (!TrackingSourceActor || !Frame.World.IsValidIndex(LandmarkIndex) || !TryGetTrackerSettings(WorldScale, bMirrorLandmarks))
	{
		return false;
	}

	FMediaPipeSolvedPose SolvedPose;
	const FMediaPipeSolvedPoseOptions SolvedOptions = MediaPipeSolvedPose::MakeDefaultOptions(WorldScale, bMirrorLandmarks);
	if (!MediaPipeSolvedPose::BuildLocal(Frame, SolvedOptions, SolvedPose))
	{
		return false;
	}

	OutWorld = TrackingSourceActor->GetActorTransform().TransformPosition(SolvedPose.LandmarksLocal[LandmarkIndex]);
	return true;
}

bool AMediaPipePoseDrivenSkeletalActor::TryGetPoseYawWorld(const FMediaPipePoseFrame& Frame, float& OutYawDeg)
{
	float WorldScale = 100.0f;
	bool bMirrorLandmarks = true;
	const AActor* TrackingSourceActor = MediaPipeCaptureRecorders::ResolveTrackingSourceActor(Source);
	if (!TrackingSourceActor || !TryGetTrackerSettings(WorldScale, bMirrorLandmarks))
	{
		return false;
	}

	FMediaPipeSolvedPose SolvedPose;
	const FMediaPipeSolvedPoseOptions SolvedOptions = MediaPipeSolvedPose::MakeDefaultOptions(WorldScale, bMirrorLandmarks);
	if (!MediaPipeSolvedPose::BuildLocal(Frame, SolvedOptions, SolvedPose) || !SolvedPose.bHasTorsoBasis)
	{
		return false;
	}

	if (!bHasLastPoseYawTimestamp || Frame.TimestampUs < LastPoseYawTimestampUs)
	{
		bHasStablePoseYawForwardWorld = false;
		StablePoseYawForwardWorld = FVector::ZeroVector;
	}
	LastPoseYawTimestampUs = Frame.TimestampUs;
	bHasLastPoseYawTimestamp = true;

	const FTransform SourceTransform = TrackingSourceActor->GetActorTransform();
	const FVector LShoulder = SourceTransform.TransformPosition(SolvedPose.LandmarksLocal[static_cast<int32>(EMediaPipePoseLandmark::LeftShoulder)]);
	const FVector RShoulder = SourceTransform.TransformPosition(SolvedPose.LandmarksLocal[static_cast<int32>(EMediaPipePoseLandmark::RightShoulder)]);
	const FVector LHip = SourceTransform.TransformPosition(SolvedPose.LandmarksLocal[static_cast<int32>(EMediaPipePoseLandmark::LeftHip)]);
	const FVector RHip = SourceTransform.TransformPosition(SolvedPose.LandmarksLocal[static_cast<int32>(EMediaPipePoseLandmark::RightHip)]);
	const FVector Nose = SourceTransform.TransformPosition(SolvedPose.LandmarksLocal[static_cast<int32>(EMediaPipePoseLandmark::Nose)]);
	const FVector ShoulderMid = (LShoulder + RShoulder) * 0.5f;
	const FVector HipMid = (LHip + RHip) * 0.5f;

	FVector HipRight = (RHip - LHip).GetSafeNormal();
	FVector Up = (ShoulderMid - HipMid).GetSafeNormal();
	if (HipRight.IsNearlyZero() || Up.IsNearlyZero())
	{
		return false;
	}

	HipRight = (HipRight - FVector::DotProduct(HipRight, Up) * Up).GetSafeNormal();
	FVector Forward = FVector::CrossProduct(HipRight, Up).GetSafeNormal();
	if (Forward.IsNearlyZero())
	{
		return false;
	}

	if (bHasStablePoseYawForwardWorld)
	{
		Forward = LockVectorToHemisphere(Forward, StablePoseYawForwardWorld);
	}
	else
	{
		FVector InitialForwardReference = Nose - ShoulderMid;
		InitialForwardReference = (InitialForwardReference - FVector::DotProduct(InitialForwardReference, Up) * Up).GetSafeNormal();
		if (InitialForwardReference.IsNearlyZero())
		{
			InitialForwardReference = SourceTransform.GetUnitAxis(EAxis::X);
		}
		Forward = LockVectorToHemisphere(Forward, InitialForwardReference);
	}
	StablePoseYawForwardWorld = Forward;
	bHasStablePoseYawForwardWorld = true;

	Forward.Z = 0.0f;
	if (Forward.IsNearlyZero())
	{
		return false;
	}
	Forward.Normalize();

	OutYawDeg = FMath::RadiansToDegrees(FMath::Atan2(Forward.Y, Forward.X));
	return true;
}

void AMediaPipePoseDrivenSkeletalActor::SetMannyPresentationVisible(const bool bVisible)
{
	if (PresentationActor)
	{
		PresentationActor->SetActorHiddenInGame(!bVisible);
		TArray<UPrimitiveComponent*> PrimitiveComponents;
		PresentationActor->GetComponents<UPrimitiveComponent>(PrimitiveComponents);
		for (UPrimitiveComponent* PrimitiveComponent : PrimitiveComponents)
		{
			if (PrimitiveComponent)
			{
				PrimitiveComponent->SetVisibility(bVisible, true);
			}
		}
		if (Mesh)
		{
			Mesh->SetHiddenInGame(true);
			Mesh->SetVisibility(false, true);
		}
		return;
	}

	if (Mesh)
	{
		Mesh->SetHiddenInGame(!bVisible);
		Mesh->SetVisibility(bVisible, true);
	}
}

USkeletalMeshComponent* AMediaPipePoseDrivenSkeletalActor::GetDrivenMesh() const
{
	return PresentationMesh ? PresentationMesh : Mesh;
}

void AMediaPipePoseDrivenSkeletalActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (!Mesh)
	{
		return;
	}

	SyncPresentationActorTransform();

	USkeletalMeshComponent* DrivenMesh = GetDrivenMesh();
	if (!DrivenMesh)
	{
		return;
	}

	if (!DrivenMesh->GetSkeletalMeshAsset())
	{
		return;
	}

#if WITH_EDITOR
	EnsureSkeletalMeshComponentUpdatesInEditor(DrivenMesh);
#endif
	ApplyReadableMannyMaterials(DrivenMesh);

	if (DrivenMesh->GetAnimClass() != UMediaPipePoseDrivenAnimInstance::StaticClass())
	{
		DrivenMesh->SetAnimationMode(EAnimationMode::AnimationBlueprint);
		DrivenMesh->SetAnimInstanceClass(UMediaPipePoseDrivenAnimInstance::StaticClass());
		DrivenMesh->InitializeAnimScriptInstance(true);
	}

	EnsureSource();

	UMediaPipePoseDrivenAnimInstance* MediaPipeAnim = Cast<UMediaPipePoseDrivenAnimInstance>(DrivenMesh->GetAnimInstance());
	if (!MediaPipeAnim)
	{
		DrivenMesh->InitializeAnimScriptInstance(true);
		MediaPipeAnim = Cast<UMediaPipePoseDrivenAnimInstance>(DrivenMesh->GetAnimInstance());
	}

	AActor* AnimSourceActor = const_cast<AActor*>(MediaPipeCaptureRecorders::ResolveTrackingSourceActor(Source));
	if (MediaPipeAnim)
	{
		MediaPipeAnim->SetSourceActor(AnimSourceActor);
		MediaPipeAnim->SetEmbodiedFusionComponent(GetActiveEmbodiedFusionComponent());
		MediaPipeAnim->ApplyRetargetQualitySettings();
	}

	// When a presentation mesh is the driven target, this actor's own mesh (the Manny-style
	// verification/reference avatar) still runs its own pose-driven anim instance. It must
	// receive the same source/retarget configuration every tick, otherwise its anim node keeps
	// the class-default drive flags (legs and pelvis translation off) and the reference avatar
	// silently freezes at the reference pose while arms keep following the source. It binds to
	// this actor's own fusion component so its evidence stream stays separate from the
	// presentation avatar's.
	if (DrivenMesh != Mesh && Mesh && Mesh->GetSkeletalMeshAsset())
	{
		if (UMediaPipePoseDrivenAnimInstance* OwnMeshAnim =
			Cast<UMediaPipePoseDrivenAnimInstance>(Mesh->GetAnimInstance()))
		{
			OwnMeshAnim->SetSourceActor(AnimSourceActor);
			OwnMeshAnim->SetEmbodiedFusionComponent(
				DefaultEmbodiedFusionComponent ? DefaultEmbodiedFusionComponent : GetActiveEmbodiedFusionComponent());
			OwnMeshAnim->ApplyRetargetQualitySettings();
		}
	}

	MediaPipeCaptureRecorders::RecordMannyBoneTimeseriesSample(this, DrivenMesh, DeltaSeconds);

	if (!AnimSourceActor)
	{
		return;
	}

	FMediaPipePoseFrame MediaPipeFrame;
	if (!TryGetMediaPipeFrame(MediaPipeFrame))
	{
		return;
	}

	FRotator DesiredRotation = GetActorRotation();
	float PoseYawDeg = 0.0f;
	if (bAutoAlignYawToPose && TryGetPoseYawWorld(MediaPipeFrame, PoseYawDeg))
	{
		DesiredRotation = AnimSourceActor->GetActorRotation();
		DesiredRotation.Yaw = PoseYawDeg - YawOffsetDeg;
	}

	if (!bAutoPositionNextToSource)
	{
		SetActorRotation(DesiredRotation);
		SyncPresentationActorTransform();
		return;
	}

	FVector DesiredLocation = Source->GetActorLocation();
	DesiredLocation += Source->GetActorRightVector() * SideOffset;
	SetActorLocationAndRotation(DesiredLocation, DesiredRotation);
	SyncPresentationActorTransform();
}
