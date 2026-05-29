#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"

#include "MediaPipeQuestWebcamSourceActor.generated.h"

class UMediaPipePoseTrackerComponent;
class UMediaPlayer;
class UMediaTexture;

UCLASS(NotBlueprintable)
class MEDIAPIPEDRIVER_API AMediaPipeQuestWebcamSourceActor : public AActor
{
	GENERATED_BODY()

public:
	AMediaPipeQuestWebcamSourceActor();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	void ConfigureCaptureDevice(const FString& InCaptureDeviceUrl, const FString& InDisplayName);
	void ConfigureLowLoadDefaults(float MaxHz, const FString& ModelPath, int32 InputMaxDimension);

	UPROPERTY(VisibleAnywhere, Category = "MediaPipe")
	UMediaPipePoseTrackerComponent* PoseTracker = nullptr;

	UPROPERTY(EditAnywhere, Category = "MediaPipe|Capture")
	FString CaptureDeviceUrl;

	UPROPERTY(EditAnywhere, Category = "MediaPipe|Capture")
	FString CaptureDeviceDisplayName;

	UPROPERTY(EditAnywhere, Category = "MediaPipe")
	bool bAutoPlay = true;

	UPROPERTY(EditAnywhere, Category = "MediaPipe")
	float WorldScale = 100.0f;

	UPROPERTY(EditAnywhere, Category = "MediaPipe")
	bool bMirrorLandmarksLR = true;

private:
	void EnsureMediaRuntime();
	void ConfigureTrackerSource() const;
	void OpenConfiguredCaptureDevice();
	void RefreshMediaTextureBinding() const;

	UFUNCTION()
	void OnMediaOpened(FString OpenedUrl);

	UFUNCTION()
	void OnMediaOpenFailed(FString FailedUrl);

	UPROPERTY(Transient)
	UMediaPlayer* MediaPlayer = nullptr;

	UPROPERTY(Transient)
	UMediaTexture* MediaTexture = nullptr;
};
