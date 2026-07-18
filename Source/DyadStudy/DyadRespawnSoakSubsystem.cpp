#include "DyadRespawnSoakSubsystem.h"

#include "ContentStreaming.h"
#include "DyadAvatarSwapLibrary.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "MediaPipeEmbodiedAvatarPawn.h"
#include "MediaPipeMetaHumanProfile.h"

DEFINE_LOG_CATEGORY_STATIC(LogDyadSoak, Log, All);

namespace
{
TAutoConsoleVariable<float> CVarDyadRespawnSoakSeconds(
	TEXT("mp.DyadRespawnSoakSeconds"), 0.0f,
	TEXT("DYADIC_STUDY_PLAN Phase 1 soak: when > 0, respawn the live pawn to the next ")
	TEXT("MetaHuman cast member every N seconds (screenshot taken just before each swap). ")
	TEXT("0 disables."));
} // namespace

bool UDyadRespawnSoakSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	if (!Super::ShouldCreateSubsystem(Outer))
	{
		return false;
	}
	const UWorld* World = Cast<UWorld>(Outer);
	return World && (World->WorldType == EWorldType::Game || World->WorldType == EWorldType::PIE);
}

TStatId UDyadRespawnSoakSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UDyadRespawnSoakSubsystem, STATGROUP_Tickables);
}

void UDyadRespawnSoakSubsystem::Tick(float DeltaTime)
{
	const float IntervalSeconds = CVarDyadRespawnSoakSeconds.GetValueOnGameThread();
	UWorld* World = GetWorld();
	if (IntervalSeconds <= 0.0f || !World)
	{
		NextSwapWorldSeconds = -1.0;
		return;
	}

	const double NowSeconds = World->GetTimeSeconds();

	// Phase 2 of a swap: the labeled shot of the OUTGOING avatar has had frames to
	// render — now it is safe to bring in the next cast member. Swapping in the same
	// tick as the (deferred) HighResShot made every portrait show the INCOMING avatar,
	// unstreamed, under the outgoing name (2026-07-17 shuffle, Wallace.png = Emory).
	if (PendingSwapAtSeconds > 0.0)
	{
		if (NowSeconds < PendingSwapAtSeconds)
		{
			return;
		}
		PendingSwapAtSeconds = -1.0;
		if (AMediaPipeEmbodiedAvatarPawn* Pawn = UDyadAvatarSwapLibrary::FindLivePawn(World))
		{
			UE_LOG(LogDyadSoak, Log,
				TEXT("mp.DyadRespawnSoak: swap %d -> %s."), CompletedSwapCount, *PendingNextProfileId.ToString());
			if (UDyadAvatarSwapLibrary::RespawnPawn(Pawn, PendingNextProfileId))
			{
				CompletedSwapCount++;
			}
		}
		return;
	}

	if (NextSwapWorldSeconds < 0.0)
	{
		// First arm: give the initial avatar one full interval to assemble.
		NextSwapWorldSeconds = NowSeconds + IntervalSeconds;
		return;
	}
	if (NowSeconds < NextSwapWorldSeconds)
	{
		return;
	}
	NextSwapWorldSeconds = NowSeconds + IntervalSeconds;

	TArray<FMediaPipeMetaHumanProfileDefinition> Profiles;
	GetMediaPipeAvailableMetaHumanProfiles(Profiles);
	if (Profiles.Num() == 0)
	{
		return;
	}

	AMediaPipeEmbodiedAvatarPawn* Pawn = UDyadAvatarSwapLibrary::FindLivePawn(World);
	if (!Pawn)
	{
		UE_LOG(LogDyadSoak, Warning, TEXT("mp.DyadRespawnSoak: no live pawn to respawn."));
		return;
	}

	// Visual record of the OUTGOING avatar after it had a full interval to settle. The
	// player controller's console is the route that reliably reaches the game viewport
	// (GEngine->Exec drops HighResShot in -game; measured 2026-07-16). Textures are
	// force-streamed first so a first-appearance avatar doesn't render grey mips.
	IStreamingManager::Get().StreamAllResources(1.0f);
	if (APlayerController* PlayerController = World->GetFirstPlayerController())
	{
		PlayerController->ConsoleCommand(FString::Printf(
			TEXT("HighResShot 1280x720 filename=DyadSoak_%02d_%s"),
			CompletedSwapCount,
			*Pawn->MetaHumanProfileId.ToString()));
	}

	// The shot is deferred to the next presented frame; hold the swap well behind it.
	PendingNextProfileId = Profiles[CastCursor % Profiles.Num()].ProfileId;
	CastCursor++;
	PendingSwapAtSeconds = NowSeconds + 0.75;
}
