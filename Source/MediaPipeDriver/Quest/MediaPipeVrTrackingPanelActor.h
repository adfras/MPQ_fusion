#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"

#include "MediaPipeVrTrackingPanelActor.generated.h"

class AMediaPipeQuestWebcamSourceActor;
class UMaterialInstanceDynamic;
class UStaticMeshComponent;

// World-space tracking panel for live VR trials: a quad that floats to the right of the
// headset view and shows the live camera preview texture (with the tracked-bone skeleton
// overlay baked in by the webcam source actor). Lifetime is owned by the embodied pawn while
// mp.QuestVrTrackingPanel is non-zero; the panel lazily follows the player view so it stays
// readable without being glued rigidly to the head.
UCLASS(NotBlueprintable)
class MEDIAPIPEDRIVER_API AMediaPipeVrTrackingPanelActor : public AActor
{
	GENERATED_BODY()

public:
	AMediaPipeVrTrackingPanelActor();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

private:
	bool TryGetViewPoint(FVector& OutLocation, FRotator& OutRotation) const;
	void UpdatePanelTransform(float DeltaSeconds, const FVector& ViewLocation, const FRotator& ViewRotation);
	void UpdatePanelTexture();

	UPROPERTY(VisibleAnywhere, Category = "MediaPipe")
	UStaticMeshComponent* PanelMesh = nullptr;

	UPROPERTY(Transient)
	UMaterialInstanceDynamic* PanelMaterial = nullptr;

	UPROPERTY(Transient)
	UTexture2D* BoundTexture = nullptr;

	TWeakObjectPtr<AMediaPipeQuestWebcamSourceActor> CachedSourceActor;
	double LastSourceSearchSeconds = -1.0;
	bool bHasSmoothedLocation = false;
	FVector SmoothedLocation = FVector::ZeroVector;
};
