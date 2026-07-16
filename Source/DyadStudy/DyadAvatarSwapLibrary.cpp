#include "DyadAvatarSwapLibrary.h"

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "MediaPipeDriverRuntime.h"
#include "MediaPipeEmbodiedAvatarPawn.h"
#include "MediaPipeMetaHumanProfile.h"

DEFINE_LOG_CATEGORY_STATIC(LogDyadSwap, Log, All);

namespace
{
const FName MannyProfileId(TEXT("Manny"));

UWorld* FindActiveGameWorld()
{
	if (!GEngine)
	{
		return nullptr;
	}
	for (const FWorldContext& Context : GEngine->GetWorldContexts())
	{
		UWorld* World = Context.World();
		if (World && (Context.WorldType == EWorldType::Game || Context.WorldType == EWorldType::PIE))
		{
			return World;
		}
	}
	return nullptr;
}

// mp.DyadRespawnAvatar <slot> <name>  (slot = live | ghost)
// The experimenter's escape hatch: a bad swap is always recoverable by another respawn,
// mid-session, without a restart. The ghost slot routes through mp.DyadGhostAvatar so the
// ghost subsystem's respawn-on-change logic does the work (and reports if not armed).
FAutoConsoleCommand CmdDyadRespawnAvatar(
	TEXT("mp.DyadRespawnAvatar"),
	TEXT("Respawn a dyad avatar slot with a new cast member: mp.DyadRespawnAvatar <live|ghost> <ProfileName>. ")
	TEXT("Respawn-not-mutate is the only safe mid-session avatar switch."),
	FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
	{
		if (Args.Num() < 2)
		{
			UE_LOG(LogDyadSwap, Error, TEXT("mp.DyadRespawnAvatar: usage <live|ghost> <ProfileName>."));
			return;
		}
		const FString Slot = Args[0].ToLower();
		const FName ProfileId(*Args[1].TrimStartAndEnd());

		if (Slot == TEXT("ghost"))
		{
			FMediaPipeMetaHumanProfileDefinition Profile;
			if (!TryGetMediaPipeMetaHumanProfile(ProfileId, Profile))
			{
				UE_LOG(LogDyadSwap, Error,
					TEXT("mp.DyadRespawnAvatar: unknown profile '%s' for ghost slot."), *Args[1]);
				return;
			}
			if (IConsoleVariable* GhostAvatarVar =
				IConsoleManager::Get().FindConsoleVariable(TEXT("mp.DyadGhostAvatar")))
			{
				GhostAvatarVar->Set(*ProfileId.ToString(), ECVF_SetByConsole);
				UE_LOG(LogDyadSwap, Log,
					TEXT("mp.DyadRespawnAvatar: ghost avatar set to %s (respawns next tick if mp.DyadGhostPartner is armed)."),
					*ProfileId.ToString());
			}
			return;
		}

		if (Slot != TEXT("live"))
		{
			UE_LOG(LogDyadSwap, Error, TEXT("mp.DyadRespawnAvatar: unknown slot '%s' (live|ghost)."), *Args[0]);
			return;
		}

		UWorld* World = FindActiveGameWorld();
		AMediaPipeEmbodiedAvatarPawn* Pawn = UDyadAvatarSwapLibrary::FindLivePawn(World);
		if (!Pawn)
		{
			UE_LOG(LogDyadSwap, Error, TEXT("mp.DyadRespawnAvatar: no live embodied pawn in a running game world."));
			return;
		}
		UDyadAvatarSwapLibrary::RespawnPawn(Pawn, ProfileId);
	}));
} // namespace

AMediaPipeEmbodiedAvatarPawn* UDyadAvatarSwapLibrary::FindLivePawn(UWorld* World)
{
	if (!World)
	{
		return nullptr;
	}
	// Prefer the possessed pawn; fall back to the first placed embodied pawn.
	if (APlayerController* PlayerController = UGameplayStatics::GetPlayerController(World, 0))
	{
		if (AMediaPipeEmbodiedAvatarPawn* Possessed = Cast<AMediaPipeEmbodiedAvatarPawn>(PlayerController->GetPawn()))
		{
			return Possessed;
		}
	}
	for (TActorIterator<AMediaPipeEmbodiedAvatarPawn> It(World); It; ++It)
	{
		return *It;
	}
	return nullptr;
}

int32 UDyadAvatarSwapLibrary::DestroyAvatarStateSatellites(UWorld* World)
{
	if (!World)
	{
		return 0;
	}
	int32 DestroyedCount = 0;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (!Actor)
		{
			continue;
		}
		const bool bAvatarStateSatellite =
			Actor->Tags.Contains(MediaPipeDriverRuntime::LiveMannyTag) ||
			Actor->Tags.Contains(MediaPipeDriverRuntime::LiveMetaHumanTag) ||
			Actor->Tags.Contains(MediaPipeDriverRuntime::LiveMetaHumanSelfViewTag);
		if (bAvatarStateSatellite)
		{
			UE_LOG(LogDyadSwap, Log, TEXT("mp.DyadRespawn: destroying satellite %s."), *GetNameSafe(Actor));
			World->DestroyActor(Actor);
			++DestroyedCount;
		}
	}
	return DestroyedCount;
}

AMediaPipeEmbodiedAvatarPawn* UDyadAvatarSwapLibrary::RespawnPawn(
	AMediaPipeEmbodiedAvatarPawn* InPawn, const FName ProfileId)
{
	if (!InPawn)
	{
		UE_LOG(LogDyadSwap, Error, TEXT("mp.DyadRespawn: no pawn."));
		return nullptr;
	}
	UWorld* World = InPawn->GetWorld();
	if (!World)
	{
		return nullptr;
	}

	// Validate the choice BEFORE any destruction: a bad name must leave the session
	// untouched.
	const bool bWantsManny = ProfileId == MannyProfileId;
	FMediaPipeMetaHumanProfileDefinition Profile;
	if (!bWantsManny && !TryGetMediaPipeMetaHumanProfile(ProfileId, Profile))
	{
		UE_LOG(LogDyadSwap, Error,
			TEXT("mp.DyadRespawn: unknown profile '%s'; pawn left untouched."), *ProfileId.ToString());
		return nullptr;
	}

	UClass* PawnClass = InPawn->GetClass();
	const FTransform PawnTransform = InPawn->GetActorTransform();
	const bool bStartTrackingOnBeginPlay = InPawn->bStartTrackingOnBeginPlay;
	const bool bUseMediaPipeTracking = InPawn->bUseMediaPipeTracking;
	const bool bDriveMovementReplicaPose = InPawn->bDriveMovementReplicaPose;
	const bool bShowMediaPipeSelfView = InPawn->bShowMediaPipeSelfView;
	const FName PreviousProfileId = InPawn->MetaHumanProfileId;

	UE_LOG(LogDyadSwap, Log,
		TEXT("mp.DyadRespawn: live slot %s -> %s (class=%s)."),
		*PreviousProfileId.ToString(), *ProfileId.ToString(), *GetNameSafe(PawnClass));

	// Destroy first, spawn second: satellites are found by tag, and the fresh pawn's
	// StartEmbodiedTracking must not find (and reuse) any of the old avatar's actors.
	World->DestroyActor(InPawn);
	DestroyAvatarStateSatellites(World);

	AMediaPipeEmbodiedAvatarPawn* NewPawn = World->SpawnActorDeferred<AMediaPipeEmbodiedAvatarPawn>(
		PawnClass,
		PawnTransform,
		nullptr,
		nullptr,
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!NewPawn)
	{
		UE_LOG(LogDyadSwap, Error, TEXT("mp.DyadRespawn: pawn respawn FAILED (class=%s)."), *GetNameSafe(PawnClass));
		return nullptr;
	}

	// The profile is set BEFORE FinishSpawning so BeginPlay -> StartEmbodiedTracking
	// binds the drive against the new avatar from the first frame.
	NewPawn->AvatarType = bWantsManny
		? EMediaPipeEmbodiedAvatarType::InternalManny
		: EMediaPipeEmbodiedAvatarType::MetaHuman;
	NewPawn->MetaHumanProfileId = bWantsManny ? PreviousProfileId : ProfileId;
	NewPawn->bStartTrackingOnBeginPlay = bStartTrackingOnBeginPlay;
	NewPawn->bUseMediaPipeTracking = bUseMediaPipeTracking;
	NewPawn->bDriveMovementReplicaPose = bDriveMovementReplicaPose;
	NewPawn->bShowMediaPipeSelfView = bShowMediaPipeSelfView;
	UGameplayStatics::FinishSpawningActor(NewPawn, PawnTransform);

	// BeginPlay already forces Player0 possession; this covers worlds where BeginPlay
	// ran before a controller existed (test worlds, late travel).
	if (APlayerController* PlayerController = UGameplayStatics::GetPlayerController(World, 0))
	{
		if (PlayerController->GetPawn() != NewPawn)
		{
			PlayerController->Possess(NewPawn);
		}
	}

	return NewPawn;
}
