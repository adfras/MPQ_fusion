#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"

#include "MediaPipeTrackingFusionDatasetReplayActor.generated.h"

UCLASS(Blueprintable)
class MEDIAPIPEDRIVER_API AMediaPipeTrackingFusionDatasetReplayActor : public AActor
{
	GENERATED_BODY()

public:
	AMediaPipeTrackingFusionDatasetReplayActor();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UPROPERTY(EditAnywhere, Category="MediaPipe|Replay")
	bool bAutoStartOnBeginPlay = true;

	UPROPERTY(EditAnywhere, Category="MediaPipe|Replay")
	bool bStopReplayOnEndPlay = true;

	UPROPERTY(EditAnywhere, Category="MediaPipe|Replay")
	FString ReplayManifestPath;

	UPROPERTY(EditAnywhere, Category="MediaPipe|Replay", meta=(ClampMin="0.01"))
	float ReplayTimeScale = 1.0f;

	UPROPERTY(EditAnywhere, Category="MediaPipe|Replay")
	bool bLoopReplay = true;

	UPROPERTY(EditAnywhere, Category="MediaPipe|Replay")
	bool bDisableCaptureRecorders = true;

	UFUNCTION(BlueprintCallable, CallInEditor, Category="MediaPipe|Replay")
	bool LoadAndStartReplay();

private:
	void ApplyReplayConsoleState() const;
};
