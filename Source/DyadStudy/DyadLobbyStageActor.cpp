#include "DyadLobbyStageActor.h"

#include "Components/PrimitiveComponent.h"
#include "Components/WidgetComponent.h"
#include "Components/WidgetInteractionComponent.h"
#include "DyadAvatarMenuWidget.h"
#include "DyadAvatarSwapLibrary.h"
#include "DyadConditionFile.h"
#include "DyadStudyRoomPolicy.h"
#include "DyadLinkSubsystem.h"
#include "EmbodiedFusionComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "EnhancedInputSubsystems.h"
#include "EnhancedPlayerInput.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "HeadMountedDisplayTypes.h"
#include "IXRTrackingSystem.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "MediaPipeDriverRuntime.h"
#include "MediaPipeEmbodiedAvatarPawn.h"
#include "MotionControllerComponent.h"

DEFINE_LOG_CATEGORY_STATIC(LogDyadLobby, Log, All);

namespace
{
FString GDyadConditionFile = TEXT("");
FAutoConsoleVariableRef CVarDyadConditionFile(
	TEXT("mp.DyadConditionFile"), GDyadConditionFile,
	TEXT("DYADIC_STUDY_PLAN Phase 5: path to the condition JSON (project-relative or ")
	TEXT("absolute). Loaded by the lobby stage at BeginPlay; the session subsystem ")
	TEXT("enforces it (locked menus render locked)."),
	ECVF_Default);

FString GDyadSeat = TEXT("A");
FAutoConsoleVariableRef CVarDyadSeat(
	TEXT("mp.DyadSeat"), GDyadSeat,
	TEXT("This machine's seat id (A|B), stamped on every session log row."),
	ECVF_Default);

TAutoConsoleVariable<float> CVarDyadLobbyAutoJourneySeconds(
	TEXT("mp.DyadLobbyAutoJourneySeconds"), 0.0f,
	TEXT("DYADIC_STUDY_PLAN Phase 2 gate driver: when > 0, the lobby stage walks the full ")
	TEXT("menu journey (select self, select partner, change self, lock, post-lock rejection) ")
	TEXT("with N seconds per step and a screenshot after each. 0 disables."));

// Composition of the recording-driven partner preview, tuned empirically 2026-07-17
// (three-point fit, then verified by elbow inspection): the rendered facing follows
// 270 + dataYaw - actorYaw, and the arm solve stays coherent ONLY at actorYaw=180 —
// any other actor yaw leaves part of the arm path in the un-rotated data frame (at
// 180 degrees of mismatch the arms render front-back mirrored: elbows bending
// forward, hands pinned behind the back). dataYaw=180 then faces the participant.
TAutoConsoleVariable<float> CVarDyadPreviewDataYawDeg(
	TEXT("mp.DyadPreviewDataYawDeg"), 180.0f,
	TEXT("Yaw applied to the recorded observations driving the lobby partner preview ")
	TEXT("(facing = 270 + dataYaw - actorYaw)."));

TAutoConsoleVariable<float> CVarDyadPreviewActorYawDeg(
	TEXT("mp.DyadPreviewActorYawDeg"), 180.0f,
	TEXT("World yaw of the lobby partner-preview rig actor. 180 is the arm-coherence ")
	TEXT("value; change only with the composition notes in DyadLobbyStageActor.cpp."));

int32 GDyadLobbyWirePartner = 0;
FAutoConsoleVariableRef CVarDyadLobbyWirePartner(
	TEXT("mp.DyadLobbyWirePartner"), GDyadLobbyWirePartner,
	TEXT("Spawn a wire-driven partner rig inside the LOBBY while rows flow (Phase 3 ")
	TEXT("loopback debugging aid). Default 0: the lobby shows only the partner-choice ")
	TEXT("preview; the wire-driven partner appears across the table after travel. The ")
	TEXT("lobby rig's spot sits on the pawn camera's sightline to the self avatar, so ")
	TEXT("leaving this on stacks two avatars on screen."),
	ECVF_Default);

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
	TEXT("Confirm the current lobby stage (same function as the menu's confirm button): ")
	TEXT("first call confirms YOUR avatar and opens partner selection; second call ")
	TEXT("confirms the partner and locks (ready/travel)."),
	FConsoleCommandDelegate::CreateLambda([]()
	{
		if (UDyadSessionSubsystem* Session = FindSessionSubsystem())
		{
			Session->ConfirmLobbyStage();
		}
	}));

// Armed by mp.DyadDumpBigPrimitives, fired from the stage Tick so late runtime spawns
// (self-view surfaces, diagnostics, media planes) are all present by dump time.
double GDyadDumpBigPrimitivesAtSeconds = -1.0;
FAutoConsoleCommand CmdDyadDumpBigPrimitives(
	TEXT("mp.DyadDumpBigPrimitives"),
	TEXT("mp.DyadDumpBigPrimitives [delaySeconds=8]: after the delay, log owner label, ")
	TEXT("component, class, and world bounds for every visible primitive component ")
	TEXT("larger than ~1 m — identifies mystery slabs in the participant rooms."),
	FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
	{
		const double Delay = Args.Num() > 0 ? FCString::Atod(*Args[0]) : 8.0;
		GDyadDumpBigPrimitivesAtSeconds = FPlatformTime::Seconds() + Delay;
		UE_LOG(LogDyadLobby, Log, TEXT("mp.DyadDumpBigPrimitives: dumping in %.1f s."), Delay);
	}));

void DumpBigPrimitives(UWorld* World)
{
	if (!World)
	{
		return;
	}
	int32 Count = 0;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		TInlineComponentArray<UPrimitiveComponent*> Components(*It);
		for (const UPrimitiveComponent* Component : Components)
		{
			if (!Component)
			{
				continue;
			}
			// Unfiltered on purpose: visibility flags and bounds both lied during the
			// 2026-07-17 black-board hunt (a visible camera surface never made the
			// filtered list), so this dump shows everything and lets the reader judge.
			const FBoxSphereBounds Bounds = Component->Bounds;
			if (Bounds.BoxExtent.GetMax() * 2.0f < 40.0f)
			{
				continue;
			}
			const FVector O = Bounds.Origin;
			const FVector E = Bounds.BoxExtent;
			UE_LOG(LogDyadLobby, Log,
				TEXT("BigPrim: actor=%s acls=%s comp=%s cls=%s vis=%d hid=%d reg=%d origin=(%.0f,%.0f,%.0f) extent=(%.0f,%.0f,%.0f)"),
				*It->GetActorNameOrLabel(), *It->GetClass()->GetName(),
				*Component->GetName(), *Component->GetClass()->GetName(),
				Component->IsVisible() ? 1 : 0, Component->bHiddenInGame ? 1 : 0,
				Component->IsRegistered() ? 1 : 0,
				O.X, O.Y, O.Z, E.X, E.Y, E.Z);
			Count++;
		}
	}
	UE_LOG(LogDyadLobby, Log, TEXT("BigPrim: %d components listed."), Count);
}
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
	MenuWidgetComponent->SetRelativeScale3D(FVector(0.08f));
	// WORN-sightline placement (2026-07-17 headset feedback: a panel 40 degrees left
	// of the forward gaze is INVISIBLE in VR — the participant reported seeing "just
	// Emory"). Stage 1 pose: dead ahead between the participant (world (0,-170), eye
	// ~154) and the mirror clone (world (0,107)), low-center at ~1.25 m so the clone's
	// face and shoulders stay visible above the panel (top edge Z~133). TickFlowStage
	// slides it aside for stage 2 so the partner preview stands unobstructed. Scale
	// 0.08 = 112 cm panel. Desk verification uses the same pawn-eye camera.
	MenuWidgetComponent->SetRelativeLocation(FVector(55.0f, -80.0f, 105.0f));
	// One-sided ON PURPOSE: with TwoSided the renderer also shows a dark,
	// depth-ignoring, X-mirrored backface copy of this widget (the "black board"
	// that haunted every desk capture since Phase 2 — 2026-07-17 hunt). Backface
	// culling removes the ghost; the participant only ever faces the front.
	MenuWidgetComponent->SetTwoSided(false);
	// The interaction ray traces Visibility: the panel must block that channel or no
	// hover/click ever lands (the standard WidgetInteraction checklist).
	MenuWidgetComponent->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	MenuWidgetComponent->SetCollisionResponseToAllChannels(ECR_Ignore);
	MenuWidgetComponent->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	MenuWidgetComponent->SetWidgetClass(UDyadAvatarMenuWidget::StaticClass());
}

void ADyadLobbyStageActor::BeginPlay()
{
	Super::BeginPlay();

	// Resolve the menu skin: the editor-authored WidgetBlueprint wins when it exists
	// (Alan's 2026-07-17 direction — a real asset he can open); the pure-C++ widget is
	// the fallback (also repairs stages serialized with a None class). The component
	// instantiated its widget at register time with the constructor default, so a
	// class change here must drop that instance and re-init.
	if (MenuWidgetComponent)
	{
		UClass* MenuClass = MenuWidgetClassOverride.IsNull() ? nullptr : MenuWidgetClassOverride.LoadSynchronous();
		if (!MenuClass)
		{
			MenuClass = UDyadAvatarMenuWidget::StaticClass();
		}
		if (MenuWidgetComponent->GetWidgetClass() != MenuClass)
		{
			MenuWidgetComponent->SetWidgetClass(MenuClass);
			MenuWidgetComponent->SetWidget(nullptr);
		}
		if (!MenuWidgetComponent->GetWidget())
		{
			MenuWidgetComponent->InitWidget();
		}
		UE_LOG(LogDyadLobby, Log, TEXT("DyadLobby: menu widget class = %s%s."),
			*MenuClass->GetName(),
			MenuClass == UDyadAvatarMenuWidget::StaticClass()
				? TEXT(" (C++ fallback)") : TEXT(" (Blueprint asset)"));
	}

	LiveTee = MakeShared<FMediaPipeDyadLiveObservationTee>();

	if (UDyadSessionSubsystem* Session = GetSession())
	{
		ChoiceChangedHandle = Session->OnAvatarChoiceChanged.AddUObject(
			this, &ADyadLobbyStageActor::HandleAvatarChoiceChanged);
		bSessionInitialized = !Session->GetSessionId().IsEmpty();
		// A partner choice that predates this stage (travel back into the lobby) still
		// gets its preview — but only once the flow is past the self stage (assigned
		// conditions preset the partner id while the participant is still at stage 1).
		LastFlowStage = Session->GetLobbyFlowStage();
		if (LastFlowStage != EDyadLobbyFlowStage::SelfSelect &&
			!Session->GetPartnerAvatarId().IsNone())
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
	FDyadAvatarRigFactory::DestroyRig(GetWorld(), WirePartnerRig);
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
		// Preview only during the partner stage: assigned presets arrive while the
		// participant is still choosing themselves, and the partner must not stand in
		// the mirror spot until the mirror stage is over.
		const UDyadSessionSubsystem* Session = GetSession();
		if (Session && Session->GetLobbyFlowStage() != EDyadLobbyFlowStage::SelfSelect)
		{
			RespawnPartnerPreview(AvatarId);
		}
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

	// Participant design (2026-07-17): the preview shows the partner MOVING on the
	// pinned recording segment — "is this who I want across the table?" — not a
	// puppet of the participant's own body (which parks lifeless whenever tracking
	// drops and previews the wrong thing conceptually). Same machinery as the ghost.
	const UDyadSessionSubsystem* Session = GetSession();
	FString SourcePath = Session ? Session->GetPartnerStreamCachePath() : FString();
	if (SourcePath.TrimStartAndEnd().IsEmpty())
	{
		// The canonical dataset (AGENTS.md) — same default as mp.DyadGhostSourceFile.
		SourcePath = TEXT("Saved/CodexAgent/Diagnostics/")
			TEXT("tracking_fusion_dataset_avatar_locked_sync_calibration_20260609_170656_replay_source_v2_manifest.json");
	}
	TSharedPtr<FMediaPipeDyadRowStream> Stream = MakeShared<FMediaPipeDyadRowStream>();
	FString LoadError;
	if (!Stream->Load(SourcePath, LoadError))
	{
		UE_LOG(LogDyadLobby, Warning,
			TEXT("DyadLobby: partner preview row source '%s' failed to load: %s"),
			*SourcePath, *LoadError);
		return;
	}
	Stream->ConfigureSegment(
		Session ? Session->GetPartnerStreamStartSeconds() : 2.0,
		Session ? Session->GetPartnerStreamDurationSeconds() : 26.0);
	Stream->StartAt(FPlatformTime::Seconds());

	// Face the participant by rotating the DATA, not the rig (arm-mirror bug otherwise;
	// see the yaw CVars above). Position comes from the placement property; the actor
	// yaw stays data-aligned.
	const float DataYawDeg = CVarDyadPreviewDataYawDeg.GetValueOnGameThread();
	const float ActorYawDeg = CVarDyadPreviewActorYawDeg.GetValueOnGameThread();
	const TSharedPtr<FMediaPipeDyadObservationSource> RotatedSource =
		MakeShared<FMediaPipeDyadYawRotatedSource>(Stream, DataYawDeg);
	const FTransform PreviewTransform(
		FRotator(0.0f, ActorYawDeg, 0.0f),
		(PartnerPreviewRelativeTransform * GetActorTransform()).GetLocation());
	FDyadAvatarRigFactory::SpawnRig(
		World, AvatarId, PreviewTransform, RotatedSource, TEXT("MP_DyadPartnerPreview"), PartnerPreviewRig);
	UE_LOG(LogDyadLobby, Log,
		TEXT("DyadLobby: partner preview %s driven by recording segment %.1fs+%.1fs ")
		TEXT("(dataYaw=%.0f actorYaw=%.0f)."),
		*AvatarId.ToString(),
		Stream->GetSegmentStartSeconds(), Stream->GetSegmentDurationSeconds(),
		DataYawDeg, ActorYawDeg);
}

void ADyadLobbyStageActor::TickFlowStage()
{
	const UDyadSessionSubsystem* Session = GetSession();
	UWorld* World = GetWorld();
	if (!Session || !World)
	{
		return;
	}
	const EDyadLobbyFlowStage Stage = Session->GetLobbyFlowStage();
	const double NowSeconds = World->GetTimeSeconds();

	if (!bStageVisualsInitialized)
	{
		// First tick snaps to the session's stage without a fade.
		ApplyStageVisuals(Stage);
		LastFlowStage = Stage;
		bStageVisualsInitialized = true;
	}
	else if (Stage != LastFlowStage)
	{
		// The participant's "phase out": fade to black, swap the scene while dark
		// (below, at the deadline), fade back in ApplyStageVisuals' wake.
		if (APlayerController* PlayerController = World->GetFirstPlayerController())
		{
			if (PlayerController->PlayerCameraManager)
			{
				PlayerController->PlayerCameraManager->StartCameraFade(
					0.0f, 1.0f, 0.35f, FLinearColor::Black, false, true);
			}
		}
		PendingVisualStage = Stage;
		StageSwapAtSeconds = NowSeconds + 0.4;
		LastFlowStage = Stage;
	}

	if (StageSwapAtSeconds > 0.0 && NowSeconds >= StageSwapAtSeconds)
	{
		StageSwapAtSeconds = -1.0;
		ApplyStageVisuals(PendingVisualStage);
		if (APlayerController* PlayerController = World->GetFirstPlayerController())
		{
			if (PlayerController->PlayerCameraManager)
			{
				PlayerController->PlayerCameraManager->StartCameraFade(
					1.0f, 0.0f, 0.35f, FLinearColor::Black, false, false);
			}
		}
	}

	if (Stage != EDyadLobbyFlowStage::SelfSelect && StageSwapAtSeconds < 0.0)
	{
		// Pawn respawns rebuild the self-view satellites visible; keep the mirror away
		// for the rest of the lobby flow.
		SetSelfViewHidden(true);
	}
}

void ADyadLobbyStageActor::ApplyStageVisuals(const EDyadLobbyFlowStage Stage)
{
	// Menu pose per stage (worn sightline, see the ctor notes): stage 1 dead ahead
	// low-center; stages 2+ slid ~30 degrees aside and angled back at the participant
	// so the partner preview stands unobstructed at the mirror spot.
	if (MenuWidgetComponent)
	{
		if (Stage == EDyadLobbyFlowStage::SelfSelect)
		{
			MenuWidgetComponent->SetRelativeLocation(FVector(55.0f, -80.0f, 105.0f));
			MenuWidgetComponent->SetRelativeRotation(FRotator::ZeroRotator);
		}
		else
		{
			MenuWidgetComponent->SetRelativeLocation(FVector(55.0f, -150.0f, 105.0f));
			MenuWidgetComponent->SetRelativeRotation(FRotator(0.0f, 30.0f, 0.0f));
		}
	}
	if (Stage == EDyadLobbyFlowStage::SelfSelect)
	{
		// The mirror stage: framed clone visible, no partner preview.
		SetSelfViewHidden(false);
		SetMirrorDecoHidden(false);
		FDyadAvatarRigFactory::DestroyRig(GetWorld(), PartnerPreviewRig);
	}
	else
	{
		// "The mirror goes away": frame and clone out, recorded partner preview in.
		SetSelfViewHidden(true);
		SetMirrorDecoHidden(true);
		const UDyadSessionSubsystem* Session = GetSession();
		if (Session && !Session->GetPartnerAvatarId().IsNone())
		{
			RespawnPartnerPreview(Session->GetPartnerAvatarId());
		}
	}
}

void ADyadLobbyStageActor::SetMirrorDecoHidden(const bool bInHidden) const
{
	// The dressing-mirror frame/backing placed by SetupDyadLobbyMap.py, tagged so the
	// stage can strike the set when the mirror "goes away".
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	static const FName MirrorDecoTag(TEXT("DyadMirrorDeco"));
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if (It->ActorHasTag(MirrorDecoTag) && It->IsHidden() != bInHidden)
		{
			It->SetActorHiddenInGame(bInHidden);
		}
	}
}

void ADyadLobbyStageActor::SetSelfViewHidden(const bool bInHidden) const
{
	// Actor-level hiding loses to the pawn's per-tick self-view manager (verified in
	// the 2026-07-17 journey: the mirror clone stayed visible through stage 2). Drive
	// the pawn's own switch instead — the same property the interaction stage sets on
	// arrival — and its manager tears the mirror copy down/up cleanly. RespawnPawn
	// copies the property, so mid-stage respawns keep the stage's choice.
	if (AMediaPipeEmbodiedAvatarPawn* LivePawn = UDyadAvatarSwapLibrary::FindLivePawn(GetWorld()))
	{
		const bool bWantShow = !bInHidden;
		if (LivePawn->bShowMediaPipeSelfView != bWantShow)
		{
			LivePawn->bShowMediaPipeSelfView = bWantShow;
		}
	}
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
	AMediaPipeEmbodiedAvatarPawn* LivePawn = UDyadAvatarSwapLibrary::FindLivePawn(GetWorld());
	if (!LivePawn)
	{
		return;
	}
	// Rewire when a component is missing, dying, or belongs to a PREVIOUS pawn: avatar
	// selection respawns the pawn (Phase 1 library), which destroys components outer'd
	// to it — the old "pointers non-null, return early" check kept the rays dead from
	// the first respawn until GC nulled the properties (2026-07-17).
	auto NeedsRewire = [LivePawn](const UWidgetInteractionComponent* Interaction)
	{
		return !IsValid(Interaction) || Interaction->GetOwner() != LivePawn;
	};
	if (!NeedsRewire(LeftPinchInteraction) && !NeedsRewire(RightPinchInteraction))
	{
		return;
	}
	// One interaction per hand, unconditionally — creation must NOT depend on
	// MotionControllerComponents existing (a pawn without them previously never got
	// rays at all). Controllers are only a POSE source, handled in TickHandRays.
	for (const bool bLeft : {true, false})
	{
		TObjectPtr<UWidgetInteractionComponent>& InteractionSlot =
			bLeft ? LeftPinchInteraction : RightPinchInteraction;
		if (!NeedsRewire(InteractionSlot))
		{
			continue;
		}
		UWidgetInteractionComponent* Interaction = NewObject<UWidgetInteractionComponent>(
			LivePawn, bLeft ? TEXT("DyadPinchInteractionL") : TEXT("DyadPinchInteractionR"));
		// Attached to the pawn ROOT, not a motion controller: with bare hands the
		// controller components sit untracked at the pawn origin (2026-07-17 headset
		// evidence: rays rising out of the floor). TickHandRays poses these every tick
		// from the best participant-frame source (controller / XR hand / fusion).
		Interaction->SetupAttachment(LivePawn->GetRootComponent());
		Interaction->InteractionDistance = 500.0f;
		// The debug line doubles as the aiming ray for the pilot (2026-07-17 worn
		// feedback: no pointer = no way to know where you point).
		Interaction->bShowDebug = true;
		Interaction->DebugColor = FLinearColor(0.15f, 0.85f, 0.95f, 1.0f);
		// Two simultaneous pointers must not share a virtual user or they steal each
		// other's hover/press state.
		Interaction->VirtualUserIndex = bLeft ? 0 : 1;
		Interaction->PointerIndex = bLeft ? 0 : 1;
		Interaction->RegisterComponent();
		InteractionSlot = Interaction;
		UE_LOG(LogDyadLobby, Log, TEXT("DyadLobby: pinch interaction wired for %s hand on %s."),
			bLeft ? TEXT("left") : TEXT("right"), *LivePawn->GetName());
	}
	// The actual CLICK is driven by TickSelectInput (controller trigger or bare-hand
	// pinch -> Press/ReleasePointerKey). Desk path: mp.DyadSelectAvatar console
	// commands call the same subsystem functions the buttons do.
}

void ADyadLobbyStageActor::TickHandRays()
{
	// Pose each hand's interaction ray from the best PARTICIPANT-frame source, in
	// priority order (this order IS the auto-switch Alan asked for):
	//   1. the runtime's XR hand tracking (bare hands, pure OpenXR): the participant's
	//      own worldspace hand — aim = wrist->index-knuckle, "where the hand points";
	//   2. the runtime's worldspace controller AIM pose (real controllers in hand —
	//      bare hands report untracked then, so this never fights source 1);
	//   3. the fusion's hand target — LAST resort only: it is the avatar-frame mapped
	//      pose, NOT the participant's worldspace hand (2026-07-17 headset evidence),
	//      kept so a ray exists at all when both real sources are dead.
	// NEVER the pawn's MotionControllerComponents: on this pawn they sit in the wrong
	// frame (floor rays, first worn round), and Link's hand->controller emulation
	// marks them "tracked" with bare hands, which let them steal the ray from the live
	// hand data for the whole second worn round (Alan's 21:57 verdict + src=controller
	// log). Both runtime queries below share the hand API's worldspace frame.
	// The throttled XRHands line below is the worn plumbing check readout.
	UWorld* World = GetWorld();
	AMediaPipeEmbodiedAvatarPawn* LivePawn = UDyadAvatarSwapLibrary::FindLivePawn(World);
	const UEmbodiedFusionComponent* Fusion = LivePawn
		? LivePawn->FindComponentByClass<UEmbodiedFusionComponent>()
		: nullptr;
	const TCHAR* SourceName[2] = { TEXT("none"), TEXT("none") };
	int32 XRValid[2] = { 0, 0 };
	int32 XRTracked[2] = { 0, 0 };
	int32 XRKeys[2] = { 0, 0 };
	for (const bool bLeft : {true, false})
	{
		const int32 HandIndex = bLeft ? 0 : 1;
		const EControllerHand Hand = bLeft ? EControllerHand::Left : EControllerHand::Right;
		UWidgetInteractionComponent* Interaction = bLeft ? LeftPinchInteraction : RightPinchInteraction;

		FXRHandTrackingState HandState;
		if (GEngine && GEngine->XRSystem.IsValid())
		{
			GEngine->XRSystem->GetHandTrackingState(
				World, EXRSpaceType::UnrealWorldSpace, Hand, HandState);
			XRValid[HandIndex] = HandState.bValid ? 1 : 0;
			XRTracked[HandIndex] = HandState.TrackingStatus == ETrackingStatus::Tracked ? 1 : 0;
			XRKeys[HandIndex] = HandState.HandKeyLocations.Num();
		}
		if (!IsValid(Interaction))
		{
			continue;
		}

		const int32 WristIndex = static_cast<int32>(EHandKeypoint::Wrist);
		const int32 KnuckleIndex = static_cast<int32>(EHandKeypoint::IndexProximal);
		if (HandState.bValid && HandState.TrackingStatus == ETrackingStatus::Tracked &&
			HandState.HandKeyLocations.Num() > KnuckleIndex)
		{
			const FVector Wrist = HandState.HandKeyLocations[WristIndex];
			const FVector Knuckle = HandState.HandKeyLocations[KnuckleIndex];
			const FVector Aim = (Knuckle - Wrist).GetSafeNormal();
			if (!Aim.IsNearlyZero())
			{
				Interaction->SetWorldLocationAndRotation(
					Knuckle, FRotationMatrix::MakeFromX(Aim).ToQuat());
				SourceName[HandIndex] = TEXT("xr-hand");
				continue;
			}
		}

		if (GEngine && GEngine->XRSystem.IsValid())
		{
			FXRMotionControllerState ControllerState;
			GEngine->XRSystem->GetMotionControllerState(
				World, EXRSpaceType::UnrealWorldSpace, Hand, EXRControllerPoseType::Aim,
				ControllerState);
			if (ControllerState.bValid &&
				ControllerState.TrackingStatus == ETrackingStatus::Tracked)
			{
				Interaction->SetWorldLocationAndRotation(
					ControllerState.ControllerLocation, ControllerState.ControllerRotation);
				SourceName[HandIndex] = TEXT("controller-aim");
				continue;
			}
		}

		if (Fusion)
		{
			const FEmbodiedFusionUpperLimbPose& Limb =
				Fusion->GetBestAvailablePose().GetUpperLimb(bLeft);
			if (Limb.bHasHandTarget)
			{
				Interaction->SetWorldLocationAndRotation(
					Limb.HandTargetWorld.GetLocation(), Limb.HandTargetWorld.GetRotation());
				SourceName[HandIndex] = TEXT("fusion-avatar-frame");
			}
		}
	}

	const double NowSeconds = World ? World->GetTimeSeconds() : FPlatformTime::Seconds();
	if (NowSeconds >= NextHandRayLogSeconds)
	{
		NextHandRayLogSeconds = NowSeconds + 2.0;
		UE_LOG(LogDyadLobby, Log,
			TEXT("DyadLobby XRHands: L(valid=%d tracked=%d keys=%d src=%s) ")
			TEXT("R(valid=%d tracked=%d keys=%d src=%s)"),
			XRValid[0], XRTracked[0], XRKeys[0], SourceName[0],
			XRValid[1], XRTracked[1], XRKeys[1], SourceName[1]);
	}
}

void ADyadLobbyStageActor::TickSelectInputAssets()
{
	// The Enhanced Input assets are editor content (authored via the MCP) and the
	// player controller arrives after BeginPlay in some boot orders, so this applies
	// lazily from the tick: load once, add the mapping context once a local player
	// exists, then stand down. Missing assets are fine — raw key polling covers select.
	if (bSelectContextApplied)
	{
		return;
	}
	UWorld* World = GetWorld();
	APlayerController* PlayerController = World ? World->GetFirstPlayerController() : nullptr;
	ULocalPlayer* LocalPlayer = PlayerController ? PlayerController->GetLocalPlayer() : nullptr;
	if (!LocalPlayer)
	{
		return;
	}
	if (!bTriedSelectInputAssets)
	{
		bTriedSelectInputAssets = true;
		LoadedSelectMappingContext = SelectMappingContext.IsNull() ? nullptr : SelectMappingContext.LoadSynchronous();
		LoadedSelectActionLeft = SelectActionLeft.IsNull() ? nullptr : SelectActionLeft.LoadSynchronous();
		LoadedSelectActionRight = SelectActionRight.IsNull() ? nullptr : SelectActionRight.LoadSynchronous();
		UE_LOG(LogDyadLobby, Log,
			TEXT("DyadLobby: select input assets ctx=%d L=%d R=%d (missing assets fall back to raw key polling)."),
			LoadedSelectMappingContext ? 1 : 0, LoadedSelectActionLeft ? 1 : 0, LoadedSelectActionRight ? 1 : 0);
	}
	if (!LoadedSelectMappingContext)
	{
		bSelectContextApplied = true;
		return;
	}
	if (UEnhancedInputLocalPlayerSubsystem* InputSubsystem =
		ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(LocalPlayer))
	{
		if (!InputSubsystem->HasMappingContext(LoadedSelectMappingContext))
		{
			InputSubsystem->AddMappingContext(LoadedSelectMappingContext, /*Priority*/ 10);
		}
		bSelectContextApplied = true;
	}
}

void ADyadLobbyStageActor::TickSelectInput()
{
	// OpenXR-portable select (2026-07-17 direction): controller trigger while a
	// controller is in hand; when controllers are put down and the runtime switches to
	// bare-hand tracking, thumb-index pinch takes over. Poll + edge-detect, then drive
	// the pointer the way the widget system expects (Press/ReleasePointerKey).
	TickSelectInputAssets();
	const bool bLeftNow = IsHandSelectPressed(true, bLeftPinchLatch);
	const bool bRightNow = IsHandSelectPressed(false, bRightPinchLatch);
	if (LeftPinchInteraction)
	{
		if (bLeftNow && !bLeftSelectDown)
		{
			LeftPinchInteraction->PressPointerKey(EKeys::LeftMouseButton);
		}
		else if (!bLeftNow && bLeftSelectDown)
		{
			LeftPinchInteraction->ReleasePointerKey(EKeys::LeftMouseButton);
		}
	}
	if (RightPinchInteraction)
	{
		if (bRightNow && !bRightSelectDown)
		{
			RightPinchInteraction->PressPointerKey(EKeys::LeftMouseButton);
		}
		else if (!bRightNow && bRightSelectDown)
		{
			RightPinchInteraction->ReleasePointerKey(EKeys::LeftMouseButton);
		}
	}
	bLeftSelectDown = bLeftNow;
	bRightSelectDown = bRightNow;
}

bool ADyadLobbyStageActor::IsHandSelectPressed(const bool bLeft, bool& bInOutPinchLatch) const
{
	UWorld* World = GetWorld();
	APlayerController* PlayerController = World ? World->GetFirstPlayerController() : nullptr;
	if (!PlayerController)
	{
		return false;
	}

	// Enhanced Input route first: the editor-authored IA/IMC assets (when present)
	// carry the trigger bindings as content Alan can edit. The raw per-profile key
	// polling below stays as the complete no-asset fallback.
	if (const UInputAction* SelectAction = bLeft ? LoadedSelectActionLeft.Get() : LoadedSelectActionRight.Get())
	{
		if (const UEnhancedPlayerInput* EnhancedInput = Cast<UEnhancedPlayerInput>(PlayerController->PlayerInput))
		{
			if (EnhancedInput->GetActionValue(SelectAction).Get<bool>())
			{
				return true;
			}
		}
	}

	// Controller path: poll the trigger across the stock OpenXR interaction profiles
	// (the runtime binds whichever controller is present to its profile). Click keys
	// first, analog fallback for profiles that only expose the axis.
	static const TCHAR* LeftClickKeys[] = {
		TEXT("OculusTouch_Left_Trigger_Click"), TEXT("ValveIndex_Left_Trigger_Click"),
		TEXT("Vive_Left_Trigger_Click"), TEXT("MixedReality_Left_Trigger_Click") };
	static const TCHAR* RightClickKeys[] = {
		TEXT("OculusTouch_Right_Trigger_Click"), TEXT("ValveIndex_Right_Trigger_Click"),
		TEXT("Vive_Right_Trigger_Click"), TEXT("MixedReality_Right_Trigger_Click") };
	static const TCHAR* LeftAxisKeys[] = {
		TEXT("OculusTouch_Left_Trigger_Axis"), TEXT("ValveIndex_Left_Trigger_Axis"),
		TEXT("Vive_Left_Trigger_Axis"), TEXT("MixedReality_Left_Trigger_Axis") };
	static const TCHAR* RightAxisKeys[] = {
		TEXT("OculusTouch_Right_Trigger_Axis"), TEXT("ValveIndex_Right_Trigger_Axis"),
		TEXT("Vive_Right_Trigger_Axis"), TEXT("MixedReality_Right_Trigger_Axis") };
	for (const TCHAR* KeyName : bLeft ? LeftClickKeys : RightClickKeys)
	{
		const FKey Key(KeyName);
		if (Key.IsValid() && PlayerController->IsInputKeyDown(Key))
		{
			return true;
		}
	}
	for (const TCHAR* KeyName : bLeft ? LeftAxisKeys : RightAxisKeys)
	{
		const FKey Key(KeyName);
		if (Key.IsValid() && PlayerController->GetInputAnalogKeyState(Key) > 0.6f)
		{
			return true;
		}
	}

	// Bare-hand path A (engine XR hand tracking, when the runtime provides it):
	// thumb-tip to index-tip pinch with hysteresis so a held pinch doesn't chatter.
	if (GEngine && GEngine->XRSystem.IsValid())
	{
		FXRHandTrackingState HandState;
		GEngine->XRSystem->GetHandTrackingState(
			World, EXRSpaceType::UnrealWorldSpace,
			bLeft ? EControllerHand::Left : EControllerHand::Right, HandState);
		if (HandState.bValid && HandState.TrackingStatus == ETrackingStatus::Tracked &&
			HandState.HandKeyLocations.Num() > static_cast<int32>(EHandKeypoint::IndexTip))
		{
			const float PinchCm = FVector::Dist(
				HandState.HandKeyLocations[static_cast<int32>(EHandKeypoint::ThumbTip)],
				HandState.HandKeyLocations[static_cast<int32>(EHandKeypoint::IndexTip)]);
			if (bInOutPinchLatch)
			{
				if (PinchCm > 3.2f)
				{
					bInOutPinchLatch = false;
				}
			}
			else if (PinchCm < 2.0f)
			{
				bInOutPinchLatch = true;
			}
			return bInOutPinchLatch;
		}
	}

	// Bare-hand path B (the fusion's own hand joints — the data that renders the
	// participant's hands, available whenever the embodiment stack tracks, with or
	// without any XR hand-tracking extension; 2026-07-17 headset run: XR hand feed
	// was dead while the fusion hands were live). Looser thresholds: webcam-derived
	// fingertips are noisier than runtime hand tracking.
	AMediaPipeEmbodiedAvatarPawn* LivePawn = UDyadAvatarSwapLibrary::FindLivePawn(World);
	if (const UEmbodiedFusionComponent* Fusion =
		LivePawn ? LivePawn->FindComponentByClass<UEmbodiedFusionComponent>() : nullptr)
	{
		const FEmbodiedFusionHandJointPose& Joints =
			Fusion->GetBestAvailablePose().GetUpperLimb(bLeft).HandJoints;
		constexpr int32 ThumbTipIndex = 5;
		constexpr int32 IndexTipIndex = 10;
		if (Joints.bHasJoints && Joints.bTracked)
		{
			const float PinchCm = FVector::Dist(
				Joints.PositionsWorld[ThumbTipIndex], Joints.PositionsWorld[IndexTipIndex]);
			if (bInOutPinchLatch)
			{
				if (PinchCm > 4.0f)
				{
					bInOutPinchLatch = false;
				}
			}
			else if (PinchCm < 2.6f)
			{
				bInOutPinchLatch = true;
			}
			return bInOutPinchLatch;
		}
	}
	bInOutPinchLatch = false;
	return false;
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
		// Sequential flow: confirming the self choice ends the mirror stage.
		Session->ConfirmLobbyStage();
		break;
	case 2:
		Screenshot(TEXT("partner_stage_mirror_gone"));
		Session->SelectPartnerAvatar(FName(TEXT("Maria")));
		break;
	case 3:
		Screenshot(TEXT("partner_Maria_recorded_preview"));
		Session->SelectPartnerAvatar(FName(TEXT("Payton")));
		break;
	case 4:
		Screenshot(TEXT("partner_changed_Payton"));
		// Second confirm locks (READY/travel when a peer is connected).
		Session->ConfirmLobbyStage();
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

void ADyadLobbyStageActor::TickWirePartner()
{
	UWorld* World = GetWorld();
	UDyadLinkSubsystem* Link = World && World->GetGameInstance()
		? World->GetGameInstance()->GetSubsystem<UDyadLinkSubsystem>()
		: nullptr;
	if (!World || !Link)
	{
		return;
	}
	const bool bWantWirePartner = GDyadLobbyWirePartner != 0 && Link->HasWireRows();
	if (!bWantWirePartner)
	{
		if (WirePartnerRig.IsSpawned())
		{
			FDyadAvatarRigFactory::DestroyRig(World, WirePartnerRig);
		}
		return;
	}

	// The wire partner wears MY choice for the partner slot (per-viewer appearance, the
	// load-bearing design decision); Kellan until a choice exists.
	const UDyadSessionSubsystem* Session = GetSession();
	FName WireAvatarId = Session ? Session->GetPartnerAvatarId() : NAME_None;
	if (WireAvatarId.IsNone())
	{
		WireAvatarId = FName(TEXT("Kellan"));
	}
	if (WirePartnerRig.IsSpawned() && WirePartnerRig.AvatarId == WireAvatarId)
	{
		return;
	}
	FDyadAvatarRigFactory::DestroyRig(World, WirePartnerRig);
	FDyadAvatarRigFactory::SpawnRig(
		World,
		WireAvatarId,
		WirePartnerRelativeTransform * GetActorTransform(),
		Link->GetWireSource(),
		TEXT("MP_DyadWirePartner"),
		WirePartnerRig);
}

void ADyadLobbyStageActor::TickConditionInit()
{
	// -game boots execute -ExecCmds AFTER BeginPlay, so the condition CVar must be
	// polled: apply it the moment it appears, or fall back to a dev session after a
	// short grace window (measured 2026-07-16: BeginPlay-time read saw an empty CVar).
	if (bSessionInitialized)
	{
		return;
	}
	UDyadSessionSubsystem* Session = GetSession();
	UWorld* World = GetWorld();
	if (!Session || !World)
	{
		return;
	}
	if (!Session->GetSessionId().IsEmpty())
	{
		bSessionInitialized = true;
		return;
	}
	const FString ConditionPath = GDyadConditionFile.TrimStartAndEnd();
	if (!ConditionPath.IsEmpty())
	{
		FString ConditionError;
		if (!FDyadConditionFile::LoadAndApply(ConditionPath, GDyadSeat.TrimStartAndEnd(), *Session, ConditionError))
		{
			UE_LOG(LogDyadLobby, Error, TEXT("DyadLobby: condition file failed: %s"), *ConditionError);
			Session->BeginNewSession(GDyadSeat.TrimStartAndEnd(), TEXT("condition_error"));
		}
		bSessionInitialized = true;
		return;
	}
	if (World->GetTimeSeconds() > 3.0)
	{
		// Standalone lobby boots (desk runs) still get stamped identity; condition runs
		// never reach this fallback (the CVar lands within the first ticks).
		Session->BeginNewSession(GDyadSeat.TrimStartAndEnd(), TEXT("lobby_dev"));
		bSessionInitialized = true;
	}
}

void ADyadLobbyStageActor::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	FDyadStudyRoomPolicy::TickParticipantFacingRoom();
	if (GDyadDumpBigPrimitivesAtSeconds > 0.0 &&
		FPlatformTime::Seconds() >= GDyadDumpBigPrimitivesAtSeconds)
	{
		GDyadDumpBigPrimitivesAtSeconds = -1.0;
		DumpBigPrimitives(GetWorld());
	}
	TickConditionInit();
	TickFlowStage();
	PublishLiveTee();
	EnsurePinchInteraction();
	TickHandRays();
	TickSelectInput();
	TickWirePartner();
	TickAutoJourney(DeltaSeconds);
}
