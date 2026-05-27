#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"

#include "MediaPipePoseVideoActor.generated.h"

class UMediaPipePoseTrackerComponent;
class UMediaPlayer;
class UMediaTexture;

UCLASS(BlueprintType, Blueprintable)
class MEDIAPIPEDRIVEREDITOR_API AMediaPipePoseVideoActor : public AActor
{
	GENERATED_BODY()

public:
	AMediaPipePoseVideoActor();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
#if WITH_EDITOR
	virtual bool ShouldTickIfViewportsOnly() const override;
#endif

	void EnsureEditorPreviewInitialized();
	void RequestVideoSeekSeconds(float Seconds, bool bPlayAfter);
	void ConfigureVideoFile(const FString& InVideoFilePath);
	void ConfigureCaptureDevice(const FString& InCaptureDeviceUrl, const FString& InDisplayName = FString());
	bool IsCaptureDeviceSource() const { return bUseCaptureDevice; }
	const FString& GetCaptureDeviceUrl() const { return CaptureDeviceUrl; }

	UPROPERTY(VisibleAnywhere, Category="MediaPipe")
	UMediaPipePoseTrackerComponent* PoseTracker = nullptr;

	UPROPERTY(EditAnywhere, Category="MediaPipe")
	FString VideoFilePath;

	UPROPERTY(EditAnywhere, Category="MediaPipe|Capture")
	bool bUseCaptureDevice = false;

	UPROPERTY(EditAnywhere, Category="MediaPipe|Capture", meta=(EditCondition="bUseCaptureDevice"))
	FString CaptureDeviceUrl;

	UPROPERTY(EditAnywhere, Category="MediaPipe|Capture", meta=(EditCondition="bUseCaptureDevice"))
	FString CaptureDeviceDisplayName;

	UPROPERTY(EditAnywhere, Category="MediaPipe")
	bool bAutoPlay = true;

	UPROPERTY(EditAnywhere, Category="MediaPipe")
	bool bLoop = true;

	UPROPERTY(EditAnywhere, Category="MediaPipe")
	float WorldScale = 100.0f;

	UPROPERTY(EditAnywhere, Category="MediaPipe")
	bool bMirrorLandmarksLR = true;

	UMediaPlayer* GetMediaPlayer() const { return MediaPlayer; }
	UMediaTexture* GetMediaTexture() const { return MediaTexture; }
	bool IsSeekPending() const { return bSeekPending; }

private:
	void EnsureMediaRuntime();
	void ConfigureTrackerSource() const;
	void OpenConfiguredMedia();
	void OpenConfiguredVideo();
	void OpenConfiguredCaptureDevice();
	void RefreshMediaTextureBinding() const;
	void PollPendingSeek();

	UFUNCTION()
	void OnMediaOpened(FString OpenedUrl);

	UFUNCTION()
	void OnMediaOpenFailed(FString FailedUrl);

	UFUNCTION()
	void OnSeekCompleted();

	UFUNCTION()
	void OnVideoEndReached();

	UPROPERTY(Transient)
	UMediaPlayer* MediaPlayer = nullptr;

	UPROPERTY(Transient)
	UMediaTexture* MediaTexture = nullptr;

	bool bSeekPending = false;
	bool bSeekPlayAfter = false;
	double PendingSeekSeconds = 0.0;
	double SeekRequestedAtSeconds = 0.0;
	int32 SeekStableTickCount = 0;
	bool bEndReachedPending = false;
};
