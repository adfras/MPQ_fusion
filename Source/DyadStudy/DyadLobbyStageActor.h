#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DyadAvatarRigFactory.h"
#include "DyadSessionSubsystem.h"

#include "DyadLobbyStageActor.generated.h"

class UInputAction;
class UInputMappingContext;
class UUserWidget;
class UWidgetComponent;
class UWidgetInteractionComponent;

// DYADIC_STUDY_PLAN Phase 2: the lobby's conductor, placed once in L_DyadLobby_01.
//
// Owns the participant-facing lobby assembly:
// - hosts the avatar menu (UDyadAvatarMenuWidget on a world-space UWidgetComponent) and
//   wires WidgetInteractionComponents onto the live pawn's motion controllers so Quest
//   hand pinch (the Select input) clicks it;
// - publishes the live pawn's source observations to a FMediaPipeDyadLiveObservationTee
//   every tick, and keeps a partner-preview rig (the participant's PARTNER-avatar choice)
//   puppeted from that tee — one person drives both previews;
// - subscribes to the session subsystem: self choice -> respawn the live pawn (Phase 1
//   library), partner choice -> respawn the preview rig.
//
// mp.DyadLobbyAutoJourneySeconds (default 0) walks the full menu journey automatically
// with a screenshot per step — the Phase 2 desk gate and later dry-run driver.
UCLASS()
class DYADSTUDY_API ADyadLobbyStageActor : public AActor
{
	GENERATED_BODY()

public:
	ADyadLobbyStageActor();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaSeconds) override;

	// Where the partner-preview rig stands, relative to this stage actor. The default
	// is CENTER STAGE — the spot the self-view mirror copy occupies during the self
	// stage (world ~(0,107) with the stage at (80,10) yaw -90): the sequential flow
	// hides the mirror copy when the partner stage begins and the recording-driven
	// partner takes its place. MetaHuman actors visually face +Y at yaw 0, so the -90
	// relative yaw (on the stage's -90) lands them looking down -Y at the participant.
	UPROPERTY(EditAnywhere, Category = "DyadStudy")
	FTransform PartnerPreviewRelativeTransform = FTransform(FRotator(0.0f, -90.0f, 0.0f), FVector(-97.0f, -80.0f, 0.0f));

	// Where a WIRE-driven partner stands while still in the lobby (Phase 3 loopback
	// proof; Phase 4 moves the wire partner into the interaction room).
	UPROPERTY(EditAnywhere, Category = "DyadStudy")
	FTransform WirePartnerRelativeTransform = FTransform(FRotator(0.0f, -90.0f, 0.0f), FVector(-180.0f, -60.0f, 0.0f));

	// Alan's 2026-07-17 direction: the visible menu is a REAL editor asset he can open
	// and edit. When this WidgetBlueprint resolves it wins the menu component; the
	// pure-C++ UDyadAvatarMenuWidget stays the fallback so headless boots and fresh
	// checkouts never lose the menu (same brain either way — see the widget class docs).
	UPROPERTY(EditAnywhere, Category = "DyadStudy")
	TSoftClassPtr<UUserWidget> MenuWidgetClassOverride = TSoftClassPtr<UUserWidget>(
		FSoftObjectPath(TEXT("/Game/DyadStudy/UI/WBP_DyadAvatarMenu.WBP_DyadAvatarMenu_C")));

	// Enhanced Input assets for the select click (authored in-editor via the MCP).
	// Additive: when they load they are polled FIRST in IsHandSelectPressed; the raw
	// per-profile key polling stays as the no-asset fallback, and the XR-hand pinch
	// paths are untouched either way.
	UPROPERTY(EditAnywhere, Category = "DyadStudy|Input")
	TSoftObjectPtr<UInputMappingContext> SelectMappingContext = TSoftObjectPtr<UInputMappingContext>(
		FSoftObjectPath(TEXT("/Game/DyadStudy/Input/IMC_DyadLobby.IMC_DyadLobby")));

	UPROPERTY(EditAnywhere, Category = "DyadStudy|Input")
	TSoftObjectPtr<UInputAction> SelectActionLeft = TSoftObjectPtr<UInputAction>(
		FSoftObjectPath(TEXT("/Game/DyadStudy/Input/IA_DyadSelect_L.IA_DyadSelect_L")));

	UPROPERTY(EditAnywhere, Category = "DyadStudy|Input")
	TSoftObjectPtr<UInputAction> SelectActionRight = TSoftObjectPtr<UInputAction>(
		FSoftObjectPath(TEXT("/Game/DyadStudy/Input/IA_DyadSelect_R.IA_DyadSelect_R")));

private:
	void HandleAvatarChoiceChanged(EDyadAvatarSlot Slot, FName AvatarId);
	void TickConditionInit();
	void TickFlowStage();
	void ApplyStageVisuals(EDyadLobbyFlowStage Stage);
	void SetMirrorDecoHidden(bool bInHidden) const;
	void SetSelfViewHidden(bool bInHidden) const;
	void RespawnPartnerPreview(FName AvatarId);
	void PublishLiveTee();
	void EnsurePinchInteraction();
	void TickHandRays();
	void TickSelectInput();
	void TickSelectInputAssets();
	bool IsHandSelectPressed(bool bLeft, bool& bInOutPinchLatch) const;
	void TickWirePartner();
	void TickAutoJourney(float DeltaSeconds);
	UDyadSessionSubsystem* GetSession() const;

	UPROPERTY(VisibleAnywhere, Category = "DyadStudy")
	TObjectPtr<UWidgetComponent> MenuWidgetComponent = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UWidgetInteractionComponent> LeftPinchInteraction = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UWidgetInteractionComponent> RightPinchInteraction = nullptr;

	TSharedPtr<FMediaPipeDyadLiveObservationTee> LiveTee;
	FDyadAvatarRig PartnerPreviewRig;
	FDyadAvatarRig WirePartnerRig;
	FDelegateHandle ChoiceChangedHandle;

	bool bSessionInitialized = false;
	EDyadLobbyFlowStage LastFlowStage = EDyadLobbyFlowStage::SelfSelect;
	bool bStageVisualsInitialized = false;
	// "Phase out" transition: fade to black, swap the scene, fade back.
	EDyadLobbyFlowStage PendingVisualStage = EDyadLobbyFlowStage::SelfSelect;
	double StageSwapAtSeconds = -1.0;
	// Select-press edge state per hand (controller trigger OR bare-hand pinch).
	bool bLeftSelectDown = false;
	bool bRightSelectDown = false;
	mutable bool bLeftPinchLatch = false;
	mutable bool bRightPinchLatch = false;
	// Loaded Enhanced Input assets (null when absent — raw key polling covers select).
	UPROPERTY(Transient)
	TObjectPtr<UInputMappingContext> LoadedSelectMappingContext = nullptr;
	UPROPERTY(Transient)
	TObjectPtr<UInputAction> LoadedSelectActionLeft = nullptr;
	UPROPERTY(Transient)
	TObjectPtr<UInputAction> LoadedSelectActionRight = nullptr;
	bool bTriedSelectInputAssets = false;
	bool bSelectContextApplied = false;
	// Throttle for the XRHands worn-check log line (TickHandRays).
	double NextHandRayLogSeconds = 0.0;
	int32 AutoJourneyStep = 0;
	double NextAutoJourneyStepSeconds = -1.0;
};
