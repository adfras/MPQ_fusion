#include "DyadInteractionStageActor.h"

#include "DyadAvatarSwapLibrary.h"
#include "DyadLinkSubsystem.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "MediaPipeEmbodiedAvatarPawn.h"

DEFINE_LOG_CATEGORY_STATIC(LogDyadInteraction, Log, All);

namespace
{
TAutoConsoleVariable<int32> CVarDyadInteractionArrivalShot(
	TEXT("mp.DyadInteractionArrivalShot"), 0,
	TEXT("When non-zero, the interaction stage takes one labeled HighResShot ~8s after ")
	TEXT("arrival (gate/dry-run evidence)."));
} // namespace

ADyadInteractionStageActor::ADyadInteractionStageActor()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;
	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);
}

void ADyadInteractionStageActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	FDyadAvatarRigFactory::DestroyRig(GetWorld(), PartnerRig);
	Super::EndPlay(EndPlayReason);
}

UDyadSessionSubsystem* ADyadInteractionStageActor::GetSession() const
{
	const UWorld* World = GetWorld();
	return World && World->GetGameInstance()
		? World->GetGameInstance()->GetSubsystem<UDyadSessionSubsystem>()
		: nullptr;
}

void ADyadInteractionStageActor::EnsureSelfPawn()
{
	UDyadSessionSubsystem* Session = GetSession();
	AMediaPipeEmbodiedAvatarPawn* LivePawn = UDyadAvatarSwapLibrary::FindLivePawn(GetWorld());
	if (!Session || !LivePawn)
	{
		return;
	}
	const FName SelfAvatarId = Session->GetSelfAvatarId();
	if (!SelfAvatarId.IsNone() && LivePawn->MetaHumanProfileId != SelfAvatarId)
	{
		LivePawn = UDyadAvatarSwapLibrary::RespawnPawn(LivePawn, SelfAvatarId);
	}
	if (LivePawn)
	{
		// The partner stands where the self-view copy would; the side mirror carries
		// self-visibility in this room.
		LivePawn->bShowMediaPipeSelfView = false;
		bSelfEnsured = true;
		UE_LOG(LogDyadInteraction, Log, TEXT("DyadInteraction: self pawn wears %s (selfView off)."),
			*LivePawn->MetaHumanProfileId.ToString());
	}
}

void ADyadInteractionStageActor::EnsurePartnerRig()
{
	UWorld* World = GetWorld();
	UDyadLinkSubsystem* Link = World && World->GetGameInstance()
		? World->GetGameInstance()->GetSubsystem<UDyadLinkSubsystem>()
		: nullptr;
	UDyadSessionSubsystem* Session = GetSession();
	if (!World || !Link || !Link->HasWireRows())
	{
		return;
	}
	FName PartnerAvatarId = Session ? Session->GetPartnerAvatarId() : NAME_None;
	if (PartnerAvatarId.IsNone())
	{
		PartnerAvatarId = FName(TEXT("Kellan"));
	}
	if (PartnerRig.IsSpawned() && PartnerRig.AvatarId == PartnerAvatarId)
	{
		return;
	}
	FDyadAvatarRigFactory::DestroyRig(World, PartnerRig);
	if (FDyadAvatarRigFactory::SpawnRig(
		World, PartnerAvatarId, GetActorTransform(), Link->GetWireSource(),
		TEXT("MP_DyadPartner"), PartnerRig))
	{
		UE_LOG(LogDyadInteraction, Log,
			TEXT("DyadInteraction: partner rig %s bound to the wire (fresh keys %u/%u)."),
			*PartnerAvatarId.ToString(), PartnerRig.PresentationMeshKey, PartnerRig.WitnessMeshKey);
	}
}

void ADyadInteractionStageActor::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	UWorld* World = GetWorld();
	if (!World || !World->IsGameWorld())
	{
		return;
	}
	if (!bArrivalReported)
	{
		bArrivalReported = true;
		if (UDyadLinkSubsystem* Link = World->GetGameInstance()
			? World->GetGameInstance()->GetSubsystem<UDyadLinkSubsystem>()
			: nullptr)
		{
			Link->NotifyArrivedInInteraction();
		}
	}
	const double NowSeconds = World->GetTimeSeconds();
	if (NowSeconds < NextEnsureSeconds)
	{
		return;
	}
	NextEnsureSeconds = NowSeconds + 0.5;
	if (!bSelfEnsured)
	{
		EnsureSelfPawn();
	}
	EnsurePartnerRig();

	if (!bArrivalShotTaken && CVarDyadInteractionArrivalShot.GetValueOnGameThread() != 0 &&
		NowSeconds > 8.0 && PartnerRig.IsSpawned())
	{
		bArrivalShotTaken = true;
		if (APlayerController* PlayerController = World->GetFirstPlayerController())
		{
			PlayerController->ConsoleCommand(FString::Printf(
				TEXT("HighResShot 1280x720 filename=DyadInteraction_%s"),
				*PartnerRig.AvatarId.ToString()));
		}
	}
}
