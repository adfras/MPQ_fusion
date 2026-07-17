#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DyadAvatarRigFactory.h"
#include "DyadSessionSubsystem.h"

#include "DyadInteractionStageActor.generated.h"

// DYADIC_STUDY_PLAN Phase 4: the interaction room's conductor, placed once in
// L_DyadInteraction_01 at the PARTNER's spot (face-to-face with the placed pawn, ~2.5 m).
//
// On arrival (the session subsystem carried the locked choices across the load):
// - ensures the live pawn wears the SELF choice (Phase 1 respawn if the placed pawn
//   differs) and hides its self-view copy (the partner stands where it would);
// - spawns the partner rig wearing MY partner choice, driven by the wire source, at this
//   actor's transform — re-binding the DyadLink stream to fresh meshes in the new level;
// - reports arrival to the travel machine and stamps the session event log.
UCLASS()
class DYADSTUDY_API ADyadInteractionStageActor : public AActor
{
	GENERATED_BODY()

public:
	ADyadInteractionStageActor();

	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaSeconds) override;

private:
	void EnsureSelfPawn();
	void EnsurePartnerRig();
	UDyadSessionSubsystem* GetSession() const;

	UPROPERTY(Transient)
	TObjectPtr<class UWidgetComponent> QuestionnaireComponent = nullptr;

	FDyadAvatarRig PartnerRig;
	bool bArrivalReported = false;
	bool bSelfEnsured = false;
	bool bArrivalShotTaken = false;
	double NextEnsureSeconds = 0.0;
};
