#pragma once

#include "CoreMinimal.h"
#include "EmbodiedFusionComponent.h"

struct MEDIAPIPEDRIVER_API FMediaPipeTrackingFusionReplayStatus
{
	bool bLoaded = false;
	bool bActive = false;
	FString SourcePath;
	FString Label;
	int32 SampleCount = 0;
	double DurationSeconds = 0.0;
	double PlaybackStartWorldSeconds = -1.0;
	double TimeScale = 1.0;
	double StartOffsetSeconds = 0.0;
	bool bLoop = true;
};

class MEDIAPIPEDRIVER_API FMediaPipeTrackingFusionDatasetReplayRuntime
{
public:
	static FMediaPipeTrackingFusionDatasetReplayRuntime& Get();

	bool LoadFromPath(const FString& RawPath, FString& OutError);
	bool LoadAndStartFromPath(const FString& RawPath, double NowSeconds, FString& OutError);
	static void ApplyReplayPoseCVars_GameThread();
	void Start(double NowSeconds);
	void Stop();
	bool IsActive() const;
	bool GetCurrentObservations(
		double NowSeconds,
		FEmbodiedFusionSourceObservations& OutObservations,
		FString* OutPhaseName = nullptr);
	FMediaPipeTrackingFusionReplayStatus GetStatus() const;

private:
	struct FReplaySample
	{
		double TimeSeconds = 0.0;
		FString PhaseName;
		FEmbodiedFusionSourceObservations Observations;
	};

	void Reset();
	static bool LoadManifestSampleFiles(
		const FString& ManifestPath,
		TArray<FString>& OutSampleFiles,
		FString& OutLabel,
		FString& OutError);
	static bool LoadJsonlSamples(
		const FString& SampleFilePath,
		TArray<FReplaySample>& OutSamples,
		FString& OutError);
	static bool ParseSampleObject(
		const TSharedPtr<FJsonObject>& SampleObject,
		FReplaySample& OutSample);
	static void RetimestampObservations(
		FEmbodiedFusionSourceObservations& Observations,
		double NowSeconds);
	int32 FindSampleIndexForElapsed(double ElapsedSeconds) const;

	mutable FCriticalSection CriticalSection;
	TArray<FReplaySample> Samples;
	FMediaPipeTrackingFusionReplayStatus Status;
};
