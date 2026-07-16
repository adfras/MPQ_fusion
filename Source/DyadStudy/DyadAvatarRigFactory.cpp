#include "DyadAvatarRigFactory.h"

#include "Components/SkeletalMeshComponent.h"
#include "EmbodiedFusionComponent.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "MediaPipeDriverRuntime.h"
#include "MediaPipeMetaHumanProfile.h"
#include "MediaPipePoseDrivenSkeletalActor.h"

DEFINE_LOG_CATEGORY_STATIC(LogDyadRig, Log, All);

namespace
{
const FName DyadRigTag(TEXT("MediaPipeDyadRig"));
} // namespace

bool FDyadAvatarRigFactory::SpawnRig(
	UWorld* World,
	const FName AvatarId,
	const FTransform& Transform,
	const TSharedPtr<FMediaPipeDyadObservationSource>& Source,
	const FString& LabelPrefix,
	FDyadAvatarRig& OutRig)
{
	OutRig = FDyadAvatarRig();
	if (!World || !Source.IsValid())
	{
		return false;
	}

	FMediaPipeMetaHumanProfileDefinition Profile;
	if (AvatarId.IsNone() || !TryGetMediaPipeMetaHumanProfile(AvatarId, Profile))
	{
		UE_LOG(LogDyadRig, Warning, TEXT("DyadRig(%s): unknown avatar profile '%s'."),
			*LabelPrefix, *AvatarId.ToString());
		return false;
	}

	UClass* MetaHumanClass = LoadClass<AActor>(nullptr, *Profile.TargetBlueprintClass.ToString());
	if (!MetaHumanClass)
	{
		UE_LOG(LogDyadRig, Warning, TEXT("DyadRig(%s): avatar blueprint %s not loadable."),
			*LabelPrefix, *Profile.TargetBlueprintClass.ToString());
		return false;
	}

	AMediaPipePoseDrivenSkeletalActor* DriverActor = World->SpawnActorDeferred<AMediaPipePoseDrivenSkeletalActor>(
		AMediaPipePoseDrivenSkeletalActor::StaticClass(),
		Transform,
		nullptr,
		nullptr,
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!DriverActor)
	{
		UE_LOG(LogDyadRig, Warning, TEXT("DyadRig(%s): driver actor spawn failed."), *LabelPrefix);
		return false;
	}
	DriverActor->Tags.AddUnique(DyadRigTag);
	DriverActor->bAutoPositionNextToSource = false;
	DriverActor->bAutoAlignYawToPose = false;
#if WITH_EDITOR
	DriverActor->SetActorLabel(FString::Printf(TEXT("%sDriver"), *LabelPrefix));
#endif
	UGameplayStatics::FinishSpawningActor(DriverActor, Transform);

	AActor* MetaHumanActor = World->SpawnActorDeferred<AActor>(
		MetaHumanClass,
		Transform,
		nullptr,
		nullptr,
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!MetaHumanActor)
	{
		UE_LOG(LogDyadRig, Warning, TEXT("DyadRig(%s): MetaHuman spawn failed for %s."),
			*LabelPrefix, *AvatarId.ToString());
		World->DestroyActor(DriverActor);
		return false;
	}
	// Only the dyad tag: the live/self-view finders key on their own tags and must never
	// reuse a dyad rig's MetaHuman (and the respawn library's satellite sweep must not
	// destroy it).
	MetaHumanActor->Tags.AddUnique(DyadRigTag);
#if WITH_EDITOR
	MetaHumanActor->SetActorLabel(FString::Printf(TEXT("%s%s"), *LabelPrefix, *AvatarId.ToString()));
#endif
	UGameplayStatics::FinishSpawningActor(MetaHumanActor, Transform);
	MediaPipeDriverRuntime::ApplyLiveMetaHumanQualityProfile(MetaHumanActor);

	USkeletalMeshComponent* BodyMesh = MediaPipeDriverRuntime::FindMetaHumanBodyMesh(MetaHumanActor, Profile);
	if (!BodyMesh)
	{
		UE_LOG(LogDyadRig, Warning, TEXT("DyadRig(%s): no usable body mesh on %s."),
			*LabelPrefix, *AvatarId.ToString());
		World->DestroyActor(MetaHumanActor);
		World->DestroyActor(DriverActor);
		return false;
	}

	// Own fusion component: this rig's solve shares nothing with the live pawn's (or with
	// the driver's default component, which keeps driving the hidden Manny witness mesh).
	UEmbodiedFusionComponent* FusionComponent = NewObject<UEmbodiedFusionComponent>(
		DriverActor, *FString::Printf(TEXT("%sFusion"), *LabelPrefix));
	FusionComponent->RegisterComponent();

	DriverActor->SetPresentationActor(MetaHumanActor, BodyMesh);
	DriverActor->SetEmbodiedFusionComponent(FusionComponent);

	// Bind BOTH pose-driven meshes: an unbound witness would fall through to the LIVE
	// sensors and puppet from the local webcam at the rig's spot.
	const uint32 PresentationKey = BodyMesh->GetUniqueID();
	const uint32 WitnessKey = DriverActor->Mesh ? DriverActor->Mesh->GetUniqueID() : 0u;
	FMediaPipeDyadRowStreamRegistry::BindMesh(PresentationKey, Source);
	if (WitnessKey != 0u)
	{
		FMediaPipeDyadRowStreamRegistry::BindMesh(WitnessKey, Source);
	}
	// The rig's avatar counts as active for its own mesh so per-avatar arm retargeting
	// runs even when the mirror wears a different cast member.
	FMediaPipeDyadAvatarProfileOverrides::SetMeshProfileOverride(PresentationKey, AvatarId);

	OutRig.DriverActor = DriverActor;
	OutRig.MetaHumanActor = MetaHumanActor;
	OutRig.FusionComponent = FusionComponent;
	OutRig.Source = Source;
	OutRig.PresentationMeshKey = PresentationKey;
	OutRig.WitnessMeshKey = WitnessKey;
	OutRig.AvatarId = AvatarId;

	UE_LOG(LogDyadRig, Log,
		TEXT("DyadRig(%s): spawned avatar=%s presentationKey=%u witnessKey=%u at=%s"),
		*LabelPrefix,
		*AvatarId.ToString(),
		PresentationKey,
		WitnessKey,
		*Transform.GetLocation().ToCompactString());
	return true;
}

void FDyadAvatarRigFactory::DestroyRig(UWorld* World, FDyadAvatarRig& Rig)
{
	if (Rig.PresentationMeshKey != 0u)
	{
		FMediaPipeDyadRowStreamRegistry::UnbindMesh(Rig.PresentationMeshKey);
		FMediaPipeDyadAvatarProfileOverrides::ClearMeshProfileOverride(Rig.PresentationMeshKey);
	}
	if (Rig.WitnessMeshKey != 0u)
	{
		FMediaPipeDyadRowStreamRegistry::UnbindMesh(Rig.WitnessMeshKey);
	}
	if (World)
	{
		if (AActor* MetaHumanActor = Rig.MetaHumanActor.Get())
		{
			World->DestroyActor(MetaHumanActor);
		}
		if (AMediaPipePoseDrivenSkeletalActor* DriverActor = Rig.DriverActor.Get())
		{
			World->DestroyActor(DriverActor);
		}
	}
	if (!Rig.AvatarId.IsNone())
	{
		UE_LOG(LogDyadRig, Log, TEXT("DyadRig: destroyed avatar=%s."), *Rig.AvatarId.ToString());
	}
	Rig = FDyadAvatarRig();
}
