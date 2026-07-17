#include "DyadInteractionStageActor.h"

#include "Components/WidgetComponent.h"
#include "DyadAvatarSwapLibrary.h"
#include "DyadLinkSubsystem.h"
#include "DyadQuestionnaireWidget.h"
#include "DyadStudyRoomPolicy.h"
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

TAutoConsoleVariable<int32> CVarDyadQuestionnaireAutoAnswer(
	TEXT("mp.DyadQuestionnaireAutoAnswer"), 0,
	TEXT("Dry-run tool: when non-zero, unanswered questionnaire items auto-answer with ")
	TEXT("this score (1-7) a couple seconds after the panel shows, exercising the full ")
	TEXT("answer->event->session-file path headlessly. 0 disables (participants answer)."));

// The questionnaire answer path shared by the in-VR buttons and the desk/console route.
FAutoConsoleCommand CmdDyadAnswerQuestionnaire(
	TEXT("mp.DyadAnswerQuestionnaire"),
	TEXT("Answer a questionnaire item: mp.DyadAnswerQuestionnaire <itemIndex> <score 1-7>. ")
	TEXT("Calls the same session-subsystem function the Likert buttons call."),
	FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
	{
		UDyadSessionSubsystem* Session = nullptr;
		if (GEngine)
		{
			for (const FWorldContext& Context : GEngine->GetWorldContexts())
			{
				UWorld* World = Context.World();
				if (World && (Context.WorldType == EWorldType::Game || Context.WorldType == EWorldType::PIE) &&
					World->GetGameInstance())
				{
					Session = World->GetGameInstance()->GetSubsystem<UDyadSessionSubsystem>();
					break;
				}
			}
		}
		if (!Session || Args.Num() < 2)
		{
			UE_LOG(LogDyadInteraction, Error,
				TEXT("mp.DyadAnswerQuestionnaire: usage <itemIndex> <score 1-7> (needs a running game world)."));
			return;
		}
		Session->AnswerQuestionnaire(FCString::Atoi(*Args[0]), FCString::Atoi(*Args[1]));
	}));
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
	// Same arm-coherence composition as the lobby preview (DyadLobbyStageActor.cpp
	// yaw notes, 2026-07-17): rotate the DATA 180 to face the participant and keep the
	// rig actor at the coherence yaw (180 world) — rotating the actor against raw data
	// front-back-mirrors the arm solve (elbows bending forward across the table).
	const TSharedPtr<FMediaPipeDyadObservationSource> RotatedWire =
		MakeShared<FMediaPipeDyadYawRotatedSource>(Link->GetWireSource(), 180.0f);
	const FTransform PartnerTransform(
		FRotator(0.0f, 180.0f, 0.0f), GetActorLocation());
	if (FDyadAvatarRigFactory::SpawnRig(
		World, PartnerAvatarId, PartnerTransform, RotatedWire,
		TEXT("MP_DyadPartner"), PartnerRig))
	{
		UE_LOG(LogDyadInteraction, Log,
			TEXT("DyadInteraction: partner rig %s bound to the wire, dataYaw=180 ")
			TEXT("actorYaw=180 (fresh keys %u/%u)."),
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
	FDyadStudyRoomPolicy::TickParticipantFacingRoom();
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

	// End-of-block questionnaire: a world-space panel between the participant and the
	// table, spawned once after the condition file's delay.
	UDyadSessionSubsystem* Session = GetSession();
	if (!QuestionnaireComponent && Session && Session->GetQuestionnaireItems().Num() > 0 &&
		Session->GetQuestionnaireAfterSeconds() > 0.0f &&
		NowSeconds > Session->GetQuestionnaireAfterSeconds())
	{
		QuestionnaireComponent = NewObject<UWidgetComponent>(this, TEXT("DyadQuestionnaire"));
		QuestionnaireComponent->SetupAttachment(GetRootComponent());
		QuestionnaireComponent->SetWidgetSpace(EWidgetSpace::World);
		QuestionnaireComponent->SetDrawSize(FVector2D(1200.0f, 800.0f));
		// One-sided like the lobby menu: TwoSided widgets also render a dark,
		// depth-ignoring, X-mirrored backface ghost (the lobby's "black board").
		// The participant faces the panel's front from their seat.
		QuestionnaireComponent->SetTwoSided(false);
		QuestionnaireComponent->SetWidgetClass(UDyadQuestionnaireWidget::StaticClass());
		QuestionnaireComponent->RegisterComponent();
		QuestionnaireComponent->SetWorldScale3D(FVector(0.1f));
		QuestionnaireComponent->SetWorldLocationAndRotation(
			GetActorLocation() + FVector(0.0f, -140.0f, 150.0f), FRotator(0.0f, -90.0f, 0.0f));
		QuestionnaireComponent->InitWidget();
		Session->RecordEvent(TEXT("questionnaire_shown"), FString::Printf(
			TEXT("afterSeconds=%.1f items=%d"),
			Session->GetQuestionnaireAfterSeconds(), Session->GetQuestionnaireItems().Num()));
	}

	const int32 AutoAnswerScore = CVarDyadQuestionnaireAutoAnswer.GetValueOnGameThread();
	if (QuestionnaireComponent && Session && AutoAnswerScore > 0 && !Session->IsQuestionnaireComplete() &&
		NowSeconds > Session->GetQuestionnaireAfterSeconds() + 2.0f)
	{
		const TArray<int32>& Answers = Session->GetQuestionnaireAnswers();
		for (int32 ItemIndex = 0; ItemIndex < Answers.Num(); ++ItemIndex)
		{
			if (Answers[ItemIndex] == 0)
			{
				Session->AnswerQuestionnaire(ItemIndex, FMath::Clamp(AutoAnswerScore, 1, 7));
				break; // one per ensure-tick, like a human pacing through items
			}
		}
	}

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
