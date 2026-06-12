#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"

#include "MediaPipeQuestWebcamSourceActor.generated.h"

class UMediaPipePoseTrackerComponent;
class UMediaPlayer;
class UMediaTexture;
class UTexture2D;
struct FMediaPipeDirectWmfCapture;
struct FMediaPipeDirectWmfPreviewOverlay;

UCLASS(NotBlueprintable)
class MEDIAPIPEDRIVER_API AMediaPipeQuestWebcamSourceActor : public AActor
{
	GENERATED_BODY()

public:
	AMediaPipeQuestWebcamSourceActor();
	virtual ~AMediaPipeQuestWebcamSourceActor() override;

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	void ConfigureCaptureDevice(const FString& InCaptureDeviceUrl, const FString& InDisplayName);
	void ConfigureLowLoadDefaults(float MaxHz, const FString& ModelPath, int32 InputMaxDimension);

	// Live preview texture with the tracked-bone overlay baked in (direct WMF capture path).
	// Consumed by the in-VR tracking panel; null until the first preview frame is built.
	UTexture2D* GetDirectPreviewTexture() const { return DirectPreviewTexture; }
	FIntPoint GetDirectPreviewSize() const { return DirectPreviewSize; }

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
	bool SelectPreferredCaptureFormat();
	void ValidateCaptureFormat();
	void UpdateDirectPreviewTexture(const TArray<uint8>& Rgb, FIntPoint CaptureSize);
	void RemoveDirectPreviewOverlay();

	UFUNCTION()
	void OnMediaOpened(FString OpenedUrl);

	UFUNCTION()
	void OnMediaOpenFailed(FString FailedUrl);

	UPROPERTY(Transient)
	UMediaPlayer* MediaPlayer = nullptr;

	UPROPERTY(Transient)
	UMediaTexture* MediaTexture = nullptr;

	UPROPERTY(Transient)
	UTexture2D* DirectPreviewTexture = nullptr;

	FMediaPipeDirectWmfCapture* DirectWmfCapture = nullptr;
	FMediaPipeDirectWmfPreviewOverlay* DirectPreviewOverlay = nullptr;
	FIntPoint DirectPreviewSize = FIntPoint::ZeroValue;
	double LastDirectPreviewUpdateSeconds = -1.0;

	TSet<int32> RejectedCaptureFormatKeys;
	TSet<FIntPoint> RejectedCaptureFormatDimensions;
	int32 ActiveCaptureFormatKey = INDEX_NONE;
	FIntPoint ActiveCaptureFormatDimensions = FIntPoint::ZeroValue;
	double LastCaptureFormatSelectWallSeconds = -1.0;
};
