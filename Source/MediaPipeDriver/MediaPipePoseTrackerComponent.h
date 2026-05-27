#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"

#include "MediaPipePoseTracker.h"
#include "MediaPipeSourceConditioner.h"
#include "MediaPipePoseTypes.h"
#include "RenderCommandFence.h"

#include "MediaPipePoseTrackerComponent.generated.h"

class UMediaTexture;
class UTextureRenderTarget2D;

UENUM()
enum class EMediaPipePoseFrameSource : uint8
{
	MediaTexture = 0,
	RenderTarget = 1,
	ImageFile = 2
};

UCLASS(ClassGroup=(MediaPipe))
class MEDIAPIPEDRIVER_API UMediaPipePoseTrackerComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UMediaPipePoseTrackerComponent();
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	bool Initialize();
	void Shutdown();
	bool ProcessFrame();

	bool GetLatestFrame(FMediaPipePoseFrame& OutFrame) const;
	bool GetCachedConditionedFrame(FMediaPipePoseFrame& OutFrame) const;
	bool GetLatestRawFrame(FMediaPipePoseFrame& OutFrame) const;
	void SetInjectedRawFrame(const FMediaPipePoseFrame& InFrame);
	void ClearInjectedRawFrame();
	bool GetLastProcessedFrameSize(FIntPoint& OutSize) const;
	bool GetLandmarkNormalized(int32 Index, FMediaPipePoseLandmark& OutLandmark) const;
	bool GetLandmarkWorld(int32 Index, FMediaPipePoseLandmark& OutLandmark) const;
	void ResetLandmarkFilter();
	void ResetForSourceDiscontinuity();
	void GetRuntimeStats(FMediaPipePosePipelineStats& OutStats) const;
	void ResetRuntimeStats();

	UPROPERTY(EditAnywhere, Category="MediaPipe|Coordinate", meta=(ClampMin="0.0"))
	float WorldScale = 100.0f;

	UPROPERTY(EditAnywhere, Category="MediaPipe|Coordinate")
	bool bMirrorLandmarksLR = true;

	UPROPERTY(EditAnywhere, Category="MediaPipe")
	EMediaPipePoseFrameSource SourceType = EMediaPipePoseFrameSource::MediaTexture;

	UPROPERTY(EditAnywhere, Category="MediaPipe")
	UMediaTexture* SourceMediaTexture = nullptr;

	UPROPERTY(EditAnywhere, Category="MediaPipe")
	UTextureRenderTarget2D* SourceRenderTarget = nullptr;

	UPROPERTY(EditAnywhere, Category="MediaPipe", meta=(EditCondition="SourceType == EMediaPipePoseFrameSource::ImageFile"))
	FString ImageFilePath;

	UPROPERTY(EditAnywhere, Category="MediaPipe", meta=(EditCondition="SourceType == EMediaPipePoseFrameSource::ImageFile"))
	bool bProcessOnce = true;

	UPROPERTY(EditAnywhere, Category="MediaPipe|Performance")
	bool bAsyncMediaTextureReadback = true;

	UPROPERTY(EditAnywhere, Category="MediaPipe|Performance", meta=(ClampMin="0"))
	int32 DropFramesAfterDiscontinuity = 2;

	UPROPERTY(EditAnywhere, Category="MediaPipe|Performance", meta=(ClampMin="0.0"))
	float MaxProcessRateHz = 30.0f;

	UPROPERTY(EditAnywhere, Category="MediaPipe|Conditioning")
	bool bUseSourceConditioning = true;

	UPROPERTY(EditAnywhere, Category="MediaPipe|Conditioning")
	bool bConditionInjectedFrames = false;

	UPROPERTY(EditAnywhere, Category="MediaPipe")
	bool bUseMockWrapper = false;

	UPROPERTY(EditAnywhere, Category="MediaPipe")
	FString WrapperDllPath;

	UPROPERTY(EditAnywhere, Category="MediaPipe")
	FString ConfigPath;

	UPROPERTY(EditAnywhere, Category="MediaPipe|Native Options")
	bool bEnableHandLandmarker = false;

	UPROPERTY(EditAnywhere, Category="MediaPipe|Native Options", meta=(EditCondition="bEnableHandLandmarker"))
	FString HandModelPath;

	UPROPERTY(EditAnywhere, Category="MediaPipe|Native Options", meta=(ClampMin="1", ClampMax="4"))
	int32 NumPoses = 1;

	UPROPERTY(EditAnywhere, Category="MediaPipe|Native Options", meta=(ClampMin="0.0", ClampMax="1.0"))
	float MinPoseDetectionConfidence = 0.5f;

	UPROPERTY(EditAnywhere, Category="MediaPipe|Native Options", meta=(ClampMin="0.0", ClampMax="1.0"))
	float MinPosePresenceConfidence = 0.5f;

	UPROPERTY(EditAnywhere, Category="MediaPipe|Native Options", meta=(ClampMin="0.0", ClampMax="1.0"))
	float MinTrackingConfidence = 0.5f;

	UPROPERTY(EditAnywhere, Category="MediaPipe|Native Options")
	bool bOutputSegmentationMasks = false;

	UPROPERTY(EditAnywhere, Category="MediaPipe|Native Options", meta=(EditCondition="bEnableHandLandmarker", ClampMin="1", ClampMax="2"))
	int32 NumHands = 2;

	UPROPERTY(EditAnywhere, Category="MediaPipe|Native Options", meta=(EditCondition="bEnableHandLandmarker", ClampMin="0.0", ClampMax="1.0"))
	float MinHandDetectionConfidence = 0.5f;

	UPROPERTY(EditAnywhere, Category="MediaPipe|Native Options", meta=(EditCondition="bEnableHandLandmarker", ClampMin="0.0", ClampMax="1.0"))
	float MinHandPresenceConfidence = 0.5f;

	UPROPERTY(EditAnywhere, Category="MediaPipe|Native Options", meta=(EditCondition="bEnableHandLandmarker", ClampMin="0.0", ClampMax="1.0"))
	float MinHandTrackingConfidence = 0.5f;

private:
	int64 GetTimestampUs() const;
	FString ResolveDefaultDllPath() const;
	FString ResolveDefaultConfigPath() const;
	FString ResolveDefaultHandModelPath() const;
	FMediaPipePoseNativeOptions BuildNativeOptions() const;
	UTextureRenderTarget2D* GetOrCreateMediaTextureInferenceRenderTarget(FIntPoint SourceSize, int32 MaxDimension, FIntPoint& OutInferenceSize);
	bool DrawMediaTextureToInferenceRenderTarget(UMediaTexture* MediaTexture, UTextureRenderTarget2D* RenderTarget);

	mutable FCriticalSection TrackerMutex;
	TUniquePtr<FMediaPipePoseTracker> Tracker;
	bool bImageFrameProcessed = false;

	mutable bool bHasInjectedRawFrame = false;
	mutable FMediaPipePoseFrame InjectedRawFrame;

	mutable FCriticalSection SourceConditionerMutex;
	mutable FMediaPipeSourceConditioner SourceConditioner;
	mutable bool bHasConditionedFrameCache = false;
	mutable int64 ConditionedFrameInputTimestampUs = 0;
	mutable uint64 ConditionedFrameInputSerial = 0;
	mutable FMediaPipePoseFrame ConditionedFrameCache;
	mutable uint64 InjectedRawFrameSerial = 0;

	double LastMediaTimeSeconds = -1.0;
	double EstimatedMediaFrameStepSeconds = 1.0 / 30.0;
	int64 MediaTimestampOffsetUs = 0;
	int64 LastMediaTimestampUs = 0;
	bool bHasLastMediaTimestamp = false;
	double LastProcessWallTimeSeconds = -1.0;

	bool bMediaTextureReadbackInFlight = false;
	FRenderCommandFence MediaTextureReadbackFence;
	TArray<FColor> MediaTextureReadbackPixels;
	FIntPoint MediaTextureReadbackSize = FIntPoint(0, 0);
	FIntPoint MediaTextureReadbackSourceSize = FIntPoint(0, 0);
	int64 MediaTextureReadbackTimestampUs = 0;
	int32 MediaTextureReadbackEpoch = 0;
	double MediaTextureReadbackBeginWallSeconds = -1.0;

	int32 SourceEpoch = 0;
	int32 DropFramesRemaining = 0;
	bool bNeedsFreshMediaFrameAfterDiscontinuity = false;
	bool bHasLastSourceSnapshot = false;
	EMediaPipePoseFrameSource LastSourceType = EMediaPipePoseFrameSource::MediaTexture;
	TWeakObjectPtr<UMediaTexture> LastSourceMediaTexture;
	TWeakObjectPtr<UTextureRenderTarget2D> LastSourceRenderTarget;
	FString LastImageFilePath;

	FIntPoint LastProcessedFrameSize = FIntPoint(0, 0);
	FMediaPipePosePipelineStats RuntimeStats;

	UPROPERTY(Transient)
	UTextureRenderTarget2D* MediaTextureInferenceRenderTarget = nullptr;
};
