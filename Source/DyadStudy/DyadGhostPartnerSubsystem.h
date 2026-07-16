#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "MediaPipeDyadRowStream.h"

#include "DyadGhostPartnerSubsystem.generated.h"

class AMediaPipePoseDrivenSkeletalActor;
class UEmbodiedFusionComponent;

// DYADIC_STUDY_PLAN Phase 0: the ghost partner.
//
// When mp.DyadGhostPartner is armed, spawns a second avatar in the running game world at
// a fixed offset from the live pawn, wearing its own MetaHuman (mp.DyadGhostAvatar), and
// drives it from a looping segment of the canonical replay cache through the replay drive
// path — while the live pawn keeps consuming its sensors untouched. The two drives share
// nothing: the ghost has its own fusion component, its own keyed solver state (fresh mesh
// component ids), and its own row stream via FMediaPipeDyadRowStreamRegistry.
//
// Arm/disarm is live: setting the CVar mid-session spawns/despawns without a restart, and
// changing mp.DyadGhostAvatar while armed respawns the ghost with the new avatar (fresh
// keyed state by construction — the respawn mechanic Phase 1 formalizes).
UCLASS()
class DYADSTUDY_API UDyadGhostPartnerSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool IsTickableInEditor() const override { return false; }

	bool IsGhostSpawned() const { return GhostDriverActor != nullptr; }
	FName GetSpawnedAvatarId() const { return SpawnedAvatarId; }

private:
	void TrySpawnGhost();
	void DespawnGhost();
	bool ResolveGhostAnchorTransform(FTransform& OutTransform) const;

	UPROPERTY(Transient)
	TObjectPtr<AMediaPipePoseDrivenSkeletalActor> GhostDriverActor = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<AActor> GhostMetaHumanActor = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UEmbodiedFusionComponent> GhostFusionComponent = nullptr;

	TSharedPtr<FMediaPipeDyadRowStream> GhostStream;
	uint32 BoundPresentationMeshKey = 0;
	uint32 BoundWitnessMeshKey = 0;
	FName SpawnedAvatarId;
	double NextSpawnAttemptSeconds = 0.0;
};
