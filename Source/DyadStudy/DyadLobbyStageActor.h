#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DyadAvatarRigFactory.h"
#include "DyadSessionSubsystem.h"

#include "DyadLobbyStageActor.generated.h"

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

private:
	void HandleAvatarChoiceChanged(EDyadAvatarSlot Slot, FName AvatarId);
	void TickConditionInit();
	void TickFlowStage();
	void SetSelfViewHidden(bool bInHidden) const;
	void RespawnPartnerPreview(FName AvatarId);
	void PublishLiveTee();
	void EnsurePinchInteraction();
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
	int32 AutoJourneyStep = 0;
	double NextAutoJourneyStepSeconds = -1.0;
};
