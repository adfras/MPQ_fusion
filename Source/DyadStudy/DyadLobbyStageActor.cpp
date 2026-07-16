#include "DyadLobbyStageActor.h"

#include "Components/WidgetComponent.h"
#include "Components/WidgetInteractionComponent.h"
#include "DyadAvatarMenuWidget.h"
#include "DyadAvatarSwapLibrary.h"
#include "EmbodiedFusionComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "MediaPipeEmbodiedAvatarPawn.h"
#include "MotionControllerComponent.h"

DEFINE_LOG_CATEGORY_STATIC(LogDyadLobby, Log, All);

namespace
{
TAutoConsoleVariable<float> CVarDyadLobbyAutoJourneySeconds(
	TEXT("mp.DyadLobbyAutoJourneySeconds"), 0.0f,
	TEXT("DYADIC_STUDY_PLAN Phase 2 gate driver: when > 0, the lobby stage walks the full ")
	TEXT("menu journey (select self, select partner, change self, lock, post-lock rejection) ")
	TEXT("with N seconds per step and a screenshot after each. 0 disables."));

// mp.DyadSelectAvatar <self|partner> <name> / mp.DyadLockChoices: the same C++ entry
// points the menu buttons call — desk verification and the experimenter's keyboard path.
UDyadSessionSubsystem* FindSessionSubsystem()
{
	if (!GEngine)
	{
		return nullptr;
	}
	for (const FWorldContext& Context : GEngine->GetWorldContexts())
	{
		UWorld* World = Context.World();
		if (World && (Context.WorldType == EWorldType::Game || Context.WorldType == EWorldType::PIE) &&
			World->GetGameInstance())
		{
			return World->GetGameInstance()->GetSubsystem<UDyadSessionSubsystem>();
		}
	}
	return nullptr;
}

FAutoConsoleCommand CmdDyadSelectAvatar(
	TEXT("mp.DyadSelectAvatar"),
	TEXT("Select a dyad avatar choice: mp.DyadSelectAvatar <self|partner> <ProfileName>. ")
	TEXT("Calls the same session-subsystem function the menu buttons call."),
	FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
	{
		UDyadSessionSubsystem* Session = FindSessionSubsystem();
		if (!Session || Args.Num() < 2)
		{
			UE_LOG(LogDyadLobby, Error, TEXT("mp.DyadSelectAvatar: usage <self|partner> <ProfileName> (needs a running game world)."));
			return;
		}
		const FString Slot = Args[0].ToLower();
		if (Slot != TEXT("self") && Slot != TEXT("partner"))
		{
			UE_LOG(LogDyadLobby, Error, TEXT("mp.DyadSelectAvatar: unknown slot '%s' (self|partner)."), *Args[0]);
			return;
		}
		Session->SelectAvatar(
			Slot == TEXT("self") ? EDyadAvatarSlot::Self : EDyadAvatarSlot::Partner,
			FName(*Args[1].TrimStartAndEnd()));
	}));

FAutoConsoleCommand CmdDyadLockChoices(
	TEXT("mp.DyadLockChoices"),
	TEXT("Lock the dyad avatar choices (same function as the menu's confirm button)."),
	FConsoleCommandDelegate::CreateLambda([]()
	{
		if (UDyadSessionSubsystem* Session = FindSessionSubsystem())
		{
			Session->LockChoices();
		}
	}));
} // namespace

ADyadLobbyStageActor::ADyadLobbyStageActor()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;

	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	MenuWidgetComponent = CreateDefaultSubobject<UWidgetComponent>(TEXT("MenuWidget"));
	MenuWidgetComponent->SetupAttachment(Root);
	MenuWidgetComponent->SetWidgetSpace(EWidgetSpace::World);
	MenuWidgetComponent->SetDrawSize(FVector2D(1400.0f, 700.0f));
	// World-space widgets render at 1 unit per pixel: scale to a ~1.7 m panel. The class
	// must be set HERE (not BeginPlay) — the component instantiates its widget when it
	// registers, which happens before BeginPlay.
	MenuWidgetComponent->SetRelativeScale3D(FVector(0.12f));
	MenuWidgetComponent->SetRelativeLocation(FVector(0.0f, 0.0f, 150.0f));
	MenuWidgetComponent->SetTwoSided(true);
	MenuWidgetComponent->SetWidgetClass(UDyadAvatarMenuWidget::StaticClass());
}

void ADyadLobbyStageActor::BeginPlay()
{
	Super::BeginPlay();

	// A stage placed before the widget class became a constructor default carries a
	// serialized None class — repair and instantiate here.
	if (MenuWidgetComponent)
	{
		if (MenuWidgetComponent->GetWidgetClass() != UDyadAvatarMenuWidget::StaticClass())
		{
			MenuWidgetComponent->SetWidgetClass(UDyadAvatarMenuWidget::StaticClass());
		}
		if (!MenuWidgetComponent->GetWidget())
		{
			MenuWidgetComponent->InitWidget();
		}
	}

	LiveTee = MakeShared<FMediaPipeDyadLiveObservationTee>();

	if (UDyadSessionSubsystem* Session = GetSession())
	{
		ChoiceChangedHandle = Session->OnAvatarChoiceChanged.AddUObject(
			this, &ADyadLobbyStageActor::HandleAvatarChoiceChanged);
		if (Session->GetSessionId().IsEmpty())
		{
			// Standalone lobby boots (desk runs) still get stamped identity; the Phase 5
			// condition loader overrides this with the real seat/condition.
			Session->BeginNewSession(TEXT("A"), TEXT("lobby_dev"));
		}
		// A partner choice that predates this stage (travel back into the lobby) still
		// gets its preview.
		if (!Session->GetPartnerAvatarId().IsNone())
		{
			RespawnPartnerPreview(Session->GetPartnerAvatarId());
		}
	}
	UE_LOG(LogDyadLobby, Log, TEXT("DyadLobby: stage ready at %s."), *GetActorLocation().ToCompactString());
}

void ADyadLobbyStageActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UDyadSessionSubsystem* Session = GetSession())
	{
		Session->OnAvatarChoiceChanged.Remove(ChoiceChangedHandle);
	}
	FDyadAvatarRigFactory::DestroyRig(GetWorld(), PartnerPreviewRig);
	Super::EndPlay(EndPlayReason);
}

UDyadSessionSubsystem* ADyadLobbyStageActor::GetSession() const
{
	const UWorld* World = GetWorld();
	return World && World->GetGameInstance()
		? World->GetGameInstance()->GetSubsystem<UDyadSessionSubsystem>()
		: nullptr;
}

void ADyadLobbyStageActor::HandleAvatarChoiceChanged(const EDyadAvatarSlot Slot, const FName AvatarId)
{
	if (Slot == EDyadAvatarSlot::Partner)
	{
		RespawnPartnerPreview(AvatarId);
		return;
	}

	// Self choice: respawn the live pawn (Phase 1 library). Skip when it already wears
	// the choice (initial Assigned/Yoked presets at session start).
	AMediaPipeEmbodiedAvatarPawn* LivePawn = UDyadAvatarSwapLibrary::FindLivePawn(GetWorld());
	if (LivePawn && LivePawn->MetaHumanProfileId != AvatarId)
	{
		UDyadAvatarSwapLibrary::RespawnPawn(LivePawn, AvatarId);
	}
}

void ADyadLobbyStageActor::RespawnPartnerPreview(const FName AvatarId)
{
	UWorld* World = GetWorld();
	if (!World || AvatarId.IsNone())
	{
		return;
	}
	if (PartnerPreviewRig.IsSpawned() && PartnerPreviewRig.AvatarId == AvatarId)
	{
		return;
	}
	FDyadAvatarRigFactory::DestroyRig(World, PartnerPreviewRig);
	const FTransform PreviewTransform = PartnerPreviewRelativeTransform * GetActorTransform();
	FDyadAvatarRigFactory::SpawnRig(
		World, AvatarId, PreviewTransform, LiveTee, TEXT("MP_DyadPartnerPreview"), PartnerPreviewRig);
}

void ADyadLobbyStageActor::PublishLiveTee()
{
	if (!LiveTee.IsValid())
	{
		return;
	}
	const AMediaPipeEmbodiedAvatarPawn* LivePawn = UDyadAvatarSwapLibrary::FindLivePawn(GetWorld());
	const UEmbodiedFusionComponent* LiveFusion = LivePawn
		? LivePawn->FindComponentByClass<UEmbodiedFusionComponent>()
		: nullptr;
	if (LiveFusion)
	{
		LiveTee->Publish(LiveFusion->GetSourceObservations_GameThread());
	}
}

void ADyadLobbyStageActor::EnsurePinchInteraction()
{
	if (LeftPinchInteraction && RightPinchInteraction)
	{
		return;
	}
	AMediaPipeEmbodiedAvatarPawn* LivePawn = UDyadAvatarSwapLibrary::FindLivePawn(GetWorld());
	if (!LivePawn)
	{
		return;
	}
	TArray<UMotionControllerComponent*> Controllers;
	LivePawn->GetComponents<UMotionControllerComponent>(Controllers);
	for (UMotionControllerComponent* Controller : Controllers)
	{
		const bool bLeft = Controller->GetTrackingMotionSource() == TEXT("Left");
		TObjectPtr<UWidgetInteractionComponent>& InteractionSlot =
			bLeft ? LeftPinchInteraction : RightPinchInteraction;
		if (InteractionSlot)
		{
			continue;
		}
		UWidgetInteractionComponent* Interaction = NewObject<UWidgetInteractionComponent>(
			LivePawn, bLeft ? TEXT("DyadPinchInteractionL") : TEXT("DyadPinchInteractionR"));
		Interaction->SetupAttachment(Controller);
		Interaction->InteractionDistance = 500.0f;
		Interaction->bShowDebug = false;
		Interaction->RegisterComponent();
		InteractionSlot = Interaction;
		UE_LOG(LogDyadLobby, Log, TEXT("DyadLobby: pinch interaction wired on %s controller."),
			bLeft ? TEXT("left") : TEXT("right"));
	}
	// Pinch->click mapping: the Quest "Select" press arrives through the pawn's input;
	// in-headset verification is a Phase 6 item. Desk path: mp.DyadSelectAvatar console
	// commands call the same subsystem functions the buttons do.
}

void ADyadLobbyStageActor::TickAutoJourney(const float DeltaSeconds)
{
	const float StepSeconds = CVarDyadLobbyAutoJourneySeconds.GetValueOnGameThread();
	if (StepSeconds <= 0.0f)
	{
		NextAutoJourneyStepSeconds = -1.0;
		AutoJourneyStep = 0;
		return;
	}
	UWorld* World = GetWorld();
	UDyadSessionSubsystem* Session = GetSession();
	if (!World || !Session)
	{
		return;
	}
	const double NowSeconds = World->GetTimeSeconds();
	if (NextAutoJourneyStepSeconds < 0.0)
	{
		NextAutoJourneyStepSeconds = NowSeconds + StepSeconds;
		return;
	}
	if (NowSeconds < NextAutoJourneyStepSeconds)
	{
		return;
	}
	NextAutoJourneyStepSeconds = NowSeconds + StepSeconds;

	auto Screenshot = [World, this](const TCHAR* Label)
	{
		if (APlayerController* PlayerController = World->GetFirstPlayerController())
		{
			PlayerController->ConsoleCommand(FString::Printf(
				TEXT("HighResShot 1280x720 filename=DyadJourney_%02d_%s"), AutoJourneyStep, Label));
		}
	};

	switch (AutoJourneyStep)
	{
	case 0:
		Screenshot(TEXT("boot"));
		Session->SelectSelfAvatar(FName(TEXT("Kellan")));
		break;
	case 1:
		Screenshot(TEXT("self_Kellan"));
		Session->SelectPartnerAvatar(FName(TEXT("Maria")));
		break;
	case 2:
		Screenshot(TEXT("partner_Maria"));
		Session->SelectSelfAvatar(FName(TEXT("Hudson")));
		break;
	case 3:
		Screenshot(TEXT("self_changed_Hudson"));
		Session->SelectPartnerAvatar(FName(TEXT("Payton")));
		break;
	case 4:
		Screenshot(TEXT("partner_changed_Payton"));
		Session->LockChoices();
		break;
	case 5:
		Screenshot(TEXT("locked"));
		// Post-lock selection must reject without changing anything.
		Session->SelectPartnerAvatar(FName(TEXT("Wallace")));
		break;
	case 6:
		Screenshot(TEXT("post_lock_reject"));
		UE_LOG(LogDyadLobby, Log, TEXT("DyadLobby: auto journey complete (self=%s partner=%s locked=%d)."),
			*Session->GetSelfAvatarId().ToString(),
			*Session->GetPartnerAvatarId().ToString(),
			Session->AreChoicesLocked() ? 1 : 0);
		break;
	default:
		return;
	}
	AutoJourneyStep++;
}

void ADyadLobbyStageActor::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	PublishLiveTee();
	EnsurePinchInteraction();
	TickAutoJourney(DeltaSeconds);
}
