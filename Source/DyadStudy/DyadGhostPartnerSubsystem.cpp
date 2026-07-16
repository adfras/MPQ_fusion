#include "DyadGhostPartnerSubsystem.h"

#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "Kismet/GameplayStatics.h"
#include "MediaPipeEmbodiedAvatarPawn.h"
#include "MediaPipeMetaHumanProfile.h"
#include "MediaPipePoseDrivenSkeletalActor.h"
#include "MediaPipeDriverRuntime.h"
#include "EmbodiedFusionComponent.h"

DEFINE_LOG_CATEGORY_STATIC(LogDyadGhost, Log, All);

namespace
{
TAutoConsoleVariable<int32> CVarDyadGhostPartner(
	TEXT("mp.DyadGhostPartner"), 0,
	TEXT("DYADIC_STUDY_PLAN Phase 0. When non-zero, spawn a ghost partner avatar in the ")
	TEXT("live world at a fixed offset from the live pawn, driven from a looping segment ")
	TEXT("of the canonical replay cache through the replay drive path while the live pawn ")
	TEXT("stays sensor-driven. Live-toggleable: 0 despawns the ghost."));

FString GDyadGhostAvatar = TEXT("Kellan");
FAutoConsoleVariableRef CVarDyadGhostAvatar(
	TEXT("mp.DyadGhostAvatar"), GDyadGhostAvatar,
	TEXT("Ghost partner avatar profile id (one of the MetaHuman cast). Changing it while ")
	TEXT("the ghost is armed respawns the ghost with the new avatar."),
	ECVF_Default);

FString GDyadGhostSourceFile = TEXT("");
FAutoConsoleVariableRef CVarDyadGhostSourceFile(
	TEXT("mp.DyadGhostSourceFile"), GDyadGhostSourceFile,
	TEXT("Row-stream source for the ghost partner: a schema-v2 replay cache manifest or ")
	TEXT(".jsonl (project-relative or absolute). Empty = the canonical replay cache."),
	ECVF_Default);

TAutoConsoleVariable<float> CVarDyadGhostSegmentStartSeconds(
	TEXT("mp.DyadGhostSegmentStartSeconds"), 0.0f,
	TEXT("Start of the looping ghost segment within the source recording, in seconds."));

TAutoConsoleVariable<float> CVarDyadGhostSegmentDurationSeconds(
	TEXT("mp.DyadGhostSegmentDurationSeconds"), 0.0f,
	TEXT("Duration of the looping ghost segment in seconds. <= 0 loops the whole recording."));

TAutoConsoleVariable<float> CVarDyadGhostOffsetRightCm(
	TEXT("mp.DyadGhostOffsetRightCm"), 120.0f,
	TEXT("Ghost partner spawn offset along the live pawn's right vector, in cm."));

TAutoConsoleVariable<float> CVarDyadGhostOffsetForwardCm(
	TEXT("mp.DyadGhostOffsetForwardCm"), 0.0f,
	TEXT("Ghost partner spawn offset along the live pawn's forward vector, in cm."));

const FName DyadGhostTag(TEXT("MediaPipeDyadGhost"));

// The canonical dataset (AGENTS.md): read-only test input for the dyad plan too.
const TCHAR* DefaultGhostSourceManifest =
	TEXT("Saved/CodexAgent/Diagnostics/")
	TEXT("tracking_fusion_dataset_avatar_locked_sync_calibration_20260609_170656_replay_source_v2_manifest.json");

constexpr double SpawnRetryIntervalSeconds = 5.0;
} // namespace

bool UDyadGhostPartnerSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	if (!Super::ShouldCreateSubsystem(Outer))
	{
		return false;
	}
	const UWorld* World = Cast<UWorld>(Outer);
	return World && (World->WorldType == EWorldType::Game || World->WorldType == EWorldType::PIE);
}

void UDyadGhostPartnerSubsystem::Deinitialize()
{
	DespawnGhost();
	Super::Deinitialize();
}

TStatId UDyadGhostPartnerSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UDyadGhostPartnerSubsystem, STATGROUP_Tickables);
}

void UDyadGhostPartnerSubsystem::Tick(float DeltaTime)
{
	const bool bArmed = CVarDyadGhostPartner.GetValueOnGameThread() != 0;
	if (!bArmed)
	{
		if (GhostDriverActor)
		{
			DespawnGhost();
		}
		return;
	}

	if (GhostDriverActor)
	{
		// Live avatar switch: respawn, never mutate (the 2026-07-08 lesson).
		const FName DesiredAvatarId(*GDyadGhostAvatar.TrimStartAndEnd());
		if (!DesiredAvatarId.IsNone() && DesiredAvatarId != SpawnedAvatarId)
		{
			UE_LOG(LogDyadGhost, Log,
				TEXT("mp.DyadGhost: avatar change %s -> %s, respawning ghost."),
				*SpawnedAvatarId.ToString(), *DesiredAvatarId.ToString());
			DespawnGhost();
			TrySpawnGhost();
		}
		return;
	}

	if (FPlatformTime::Seconds() >= NextSpawnAttemptSeconds)
	{
		TrySpawnGhost();
	}
}

bool UDyadGhostPartnerSubsystem::ResolveGhostAnchorTransform(FTransform& OutTransform) const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}

	AActor* Anchor = nullptr;
	for (TActorIterator<AMediaPipeEmbodiedAvatarPawn> It(World); It; ++It)
	{
		Anchor = *It;
		break;
	}
	if (!Anchor)
	{
		Anchor = UGameplayStatics::GetPlayerPawn(World, 0);
	}
	if (!Anchor)
	{
		return false;
	}

	const FTransform AnchorTransform = Anchor->GetActorTransform();
	const FVector Offset =
		AnchorTransform.GetRotation().GetRightVector() * CVarDyadGhostOffsetRightCm.GetValueOnGameThread() +
		AnchorTransform.GetRotation().GetForwardVector() * CVarDyadGhostOffsetForwardCm.GetValueOnGameThread();
	OutTransform = FTransform(
		AnchorTransform.GetRotation(),
		AnchorTransform.GetLocation() + Offset,
		FVector::OneVector);
	return true;
}

void UDyadGhostPartnerSubsystem::TrySpawnGhost()
{
	NextSpawnAttemptSeconds = FPlatformTime::Seconds() + SpawnRetryIntervalSeconds;
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	const FName AvatarId(*GDyadGhostAvatar.TrimStartAndEnd());
	FMediaPipeMetaHumanProfileDefinition Profile;
	if (AvatarId.IsNone() || !TryGetMediaPipeMetaHumanProfile(AvatarId, Profile))
	{
		UE_LOG(LogDyadGhost, Warning,
			TEXT("mp.DyadGhost: unknown avatar profile '%s'; ghost not spawned (retry in %.0fs)."),
			*GDyadGhostAvatar, SpawnRetryIntervalSeconds);
		return;
	}

	FTransform AnchorTransform;
	if (!ResolveGhostAnchorTransform(AnchorTransform))
	{
		UE_LOG(LogDyadGhost, Warning,
			TEXT("mp.DyadGhost: no live pawn/player anchor yet; ghost not spawned (retry in %.0fs)."),
			SpawnRetryIntervalSeconds);
		return;
	}

	// Row stream first: a ghost that cannot stream must not spawn half-assembled.
	const FString SourcePath = GDyadGhostSourceFile.TrimStartAndEnd().IsEmpty()
		? FString(DefaultGhostSourceManifest)
		: GDyadGhostSourceFile.TrimStartAndEnd();
	TSharedPtr<FMediaPipeDyadRowStream> Stream = MakeShared<FMediaPipeDyadRowStream>();
	FString LoadError;
	const double LoadStartSeconds = FPlatformTime::Seconds();
	if (!Stream->Load(SourcePath, LoadError))
	{
		UE_LOG(LogDyadGhost, Warning,
			TEXT("mp.DyadGhost: failed to load row source '%s': %s (retry in %.0fs)."),
			*SourcePath, *LoadError, SpawnRetryIntervalSeconds);
		return;
	}
	Stream->ConfigureSegment(
		CVarDyadGhostSegmentStartSeconds.GetValueOnGameThread(),
		CVarDyadGhostSegmentDurationSeconds.GetValueOnGameThread());

	UClass* MetaHumanClass = LoadClass<AActor>(nullptr, *Profile.TargetBlueprintClass.ToString());
	if (!MetaHumanClass)
	{
		UE_LOG(LogDyadGhost, Warning,
			TEXT("mp.DyadGhost: avatar blueprint %s not loadable; ghost not spawned."),
			*Profile.TargetBlueprintClass.ToString());
		return;
	}

	AMediaPipePoseDrivenSkeletalActor* DriverActor = World->SpawnActorDeferred<AMediaPipePoseDrivenSkeletalActor>(
		AMediaPipePoseDrivenSkeletalActor::StaticClass(),
		AnchorTransform,
		nullptr,
		nullptr,
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!DriverActor)
	{
		UE_LOG(LogDyadGhost, Warning, TEXT("mp.DyadGhost: driver actor spawn failed."));
		return;
	}
	DriverActor->Tags.AddUnique(DyadGhostTag);
	DriverActor->bAutoPositionNextToSource = false;
	DriverActor->bAutoAlignYawToPose = false;
#if WITH_EDITOR
	DriverActor->SetActorLabel(TEXT("MP_DyadGhostDriver"));
#endif
	UGameplayStatics::FinishSpawningActor(DriverActor, AnchorTransform);

	AActor* MetaHumanActor = World->SpawnActorDeferred<AActor>(
		MetaHumanClass,
		AnchorTransform,
		nullptr,
		nullptr,
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!MetaHumanActor)
	{
		UE_LOG(LogDyadGhost, Warning, TEXT("mp.DyadGhost: MetaHuman spawn failed for %s."), *AvatarId.ToString());
		World->DestroyActor(DriverActor);
		return;
	}
	// Only the dyad tag: the live/self-view finders key on their own tags and must never
	// return the ghost's MetaHuman as a reusable live actor.
	MetaHumanActor->Tags.AddUnique(DyadGhostTag);
#if WITH_EDITOR
	MetaHumanActor->SetActorLabel(FString::Printf(TEXT("MP_DyadGhost%s"), *AvatarId.ToString()));
#endif
	UGameplayStatics::FinishSpawningActor(MetaHumanActor, AnchorTransform);

	USkeletalMeshComponent* BodyMesh = MediaPipeDriverRuntime::FindMetaHumanBodyMesh(MetaHumanActor, Profile);
	if (!BodyMesh)
	{
		UE_LOG(LogDyadGhost, Warning,
			TEXT("mp.DyadGhost: no usable body mesh on %s; ghost not spawned."), *AvatarId.ToString());
		World->DestroyActor(MetaHumanActor);
		World->DestroyActor(DriverActor);
		return;
	}

	// Own fusion component: the ghost's solve shares nothing with the live pawn's (or with
	// the driver's default component, which keeps driving the hidden Manny witness mesh).
	UEmbodiedFusionComponent* FusionComponent = NewObject<UEmbodiedFusionComponent>(
		DriverActor, TEXT("DyadGhostFusion"));
	FusionComponent->RegisterComponent();

	DriverActor->SetPresentationActor(MetaHumanActor, BodyMesh);
	DriverActor->SetEmbodiedFusionComponent(FusionComponent);

	// Bind BOTH pose-driven meshes to the ghost stream: the presentation MetaHuman and the
	// driver's own hidden Manny witness (its anim instance ticks regardless — in the global
	// replay map both consume rows, and an unbound witness would fall through to the LIVE
	// sensors and puppet from the local webcam at the ghost's spot).
	const uint32 PresentationKey = BodyMesh->GetUniqueID();
	const uint32 WitnessKey = DriverActor->Mesh ? DriverActor->Mesh->GetUniqueID() : 0u;
	Stream->StartAt(FPlatformTime::Seconds());
	FMediaPipeDyadRowStreamRegistry::BindMesh(PresentationKey, Stream);
	if (WitnessKey != 0u)
	{
		FMediaPipeDyadRowStreamRegistry::BindMesh(WitnessKey, Stream);
	}
	// The ghost's avatar counts as active for its own mesh so per-avatar arm retargeting
	// runs even when the mirror wears a different cast member.
	FMediaPipeDyadAvatarProfileOverrides::SetMeshProfileOverride(PresentationKey, AvatarId);

	GhostDriverActor = DriverActor;
	GhostMetaHumanActor = MetaHumanActor;
	GhostFusionComponent = FusionComponent;
	GhostStream = Stream;
	BoundPresentationMeshKey = PresentationKey;
	BoundWitnessMeshKey = WitnessKey;
	SpawnedAvatarId = AvatarId;

	UE_LOG(LogDyadGhost, Log,
		TEXT("mp.DyadGhost: spawned avatar=%s source=%s segment=%.1fs+%.1fs loadMs=%.0f ")
		TEXT("presentationKey=%u witnessKey=%u anchor=%s"),
		*AvatarId.ToString(),
		*SourcePath,
		Stream->GetSegmentStartSeconds(),
		Stream->GetSegmentDurationSeconds(),
		(FPlatformTime::Seconds() - LoadStartSeconds) * 1000.0,
		PresentationKey,
		WitnessKey,
		*AnchorTransform.GetLocation().ToCompactString());
}

void UDyadGhostPartnerSubsystem::DespawnGhost()
{
	if (BoundPresentationMeshKey != 0u)
	{
		FMediaPipeDyadRowStreamRegistry::UnbindMesh(BoundPresentationMeshKey);
		FMediaPipeDyadAvatarProfileOverrides::ClearMeshProfileOverride(BoundPresentationMeshKey);
		BoundPresentationMeshKey = 0;
	}
	if (BoundWitnessMeshKey != 0u)
	{
		FMediaPipeDyadRowStreamRegistry::UnbindMesh(BoundWitnessMeshKey);
		BoundWitnessMeshKey = 0;
	}
	GhostStream.Reset();
	GhostFusionComponent = nullptr;

	UWorld* World = GetWorld();
	if (GhostMetaHumanActor)
	{
		if (World)
		{
			World->DestroyActor(GhostMetaHumanActor);
		}
		GhostMetaHumanActor = nullptr;
	}
	if (GhostDriverActor)
	{
		if (World)
		{
			World->DestroyActor(GhostDriverActor);
		}
		GhostDriverActor = nullptr;
	}
	if (!SpawnedAvatarId.IsNone())
	{
		UE_LOG(LogDyadGhost, Log, TEXT("mp.DyadGhost: despawned avatar=%s."), *SpawnedAvatarId.ToString());
	}
	SpawnedAvatarId = NAME_None;
}
