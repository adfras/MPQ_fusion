#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"

#include "DyadRespawnSoakSubsystem.generated.h"

// DYADIC_STUDY_PLAN Phase 1 gate tooling (default off, mp.DyadRespawnSoakSeconds).
//
// Soaks the respawn-not-mutate mechanic against the regression it exists to prevent:
// while armed, cycles the live pawn through the full MetaHuman cast every N seconds in a
// running game world, taking a labeled HighResShot just before each swap so a headless
// -game run leaves a visual record (avatar standing, arms sane) plus the per-swap
// resolution log lines. Also the experimenter's stress tool for Phase 6 rehearsals.
UCLASS()
class DYADSTUDY_API UDyadRespawnSoakSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool IsTickableInEditor() const override { return false; }

private:
	double NextSwapWorldSeconds = -1.0;
	int32 CastCursor = 0;
	int32 CompletedSwapCount = 0;
};
