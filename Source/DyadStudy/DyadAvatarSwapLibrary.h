#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"

#include "DyadAvatarSwapLibrary.generated.h"

class AMediaPipeEmbodiedAvatarPawn;

// DYADIC_STUDY_PLAN Phase 1: safe avatar switching DURING play.
//
// The one rule: respawn, never mutate. Flipping a live pawn's profile in place leaves the
// spawned MetaHuman, its retargeter binding, and the anim instance's cached references on
// the OLD avatar and the arms mangle (2026-07-08 lesson). RespawnPawn destroys the pawn
// AND its avatar-state satellites (driver actor, live MetaHuman, self-view MetaHuman, VR
// panel), then spawns a fresh pawn of the same class with the new profile set BEFORE
// BeginPlay binds the drive — fresh actors mean fresh mesh-component ids, which mean
// fresh keyed solver state by construction. The webcam source actor survives (it is a
// sensor, not avatar state). Wrist calibration re-latches from neutral over ~1-2 s after
// a swap: expected, absorbed by the lobby UX, not a defect.
UCLASS()
class DYADSTUDY_API UDyadAvatarSwapLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	// Destroys InPawn and its avatar-state satellites, then spawns a fresh pawn of the
	// same class at the same transform wearing ProfileId ("Manny" selects the internal
	// Manny replica; anything else must be a registered MetaHuman profile). Returns the
	// new pawn, or nullptr (with a logged reason) if ProfileId is unknown — in which
	// case InPawn is left untouched.
	UFUNCTION(BlueprintCallable, Category = "DyadStudy")
	static AMediaPipeEmbodiedAvatarPawn* RespawnPawn(AMediaPipeEmbodiedAvatarPawn* InPawn, FName ProfileId);

	// Finds the live embodied pawn in the given world (the placed/possessed one).
	static AMediaPipeEmbodiedAvatarPawn* FindLivePawn(UWorld* World);

	// Destroys the avatar-state satellite actors a fresh pawn must re-assemble: the
	// pose-driven driver (LiveMannyTag), live MetaHuman presentation (LiveMetaHumanTag),
	// and self-view MetaHuman (LiveMetaHumanSelfViewTag). Returns how many were
	// destroyed. Exposed for tests.
	static int32 DestroyAvatarStateSatellites(UWorld* World);
};
