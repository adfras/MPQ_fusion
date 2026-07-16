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

	// True when mp.TrackingFusionDatasetReplayLiveParity is enabled: scoring replays run the
	// live corrective stack, so live-only solver paths gated on "replay is active" (overhead
	// arm rescue, camera hand ownership, direct arm-chain freshness) must treat the replay
	// as a live session. See ApplyReplayPoseCVars_GameThread.
	static bool IsLiveParityEnabled();

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
	// Direct row access for per-instance readers (dyad row streams): returns the sample at
	// DatasetTimeSeconds restamped to StampNowSeconds, requiring only bLoaded — playback
	// state (Start/Stop, CVar pacing) is bypassed so callers own their own pacing/looping.
	bool GetObservationsAtDatasetTime(
		double DatasetTimeSeconds,
		double StampNowSeconds,
		FEmbodiedFusionSourceObservations& OutObservations,
		FString* OutPhaseName = nullptr) const;
	// The schema-v2 source-row parser, exposed for the dyad wire (DYADIC_STUDY_PLAN
	// Phase 3): inbound ROW payloads parse through the exact code path replay caches
	// parse through, so a wire-driven partner and a file-driven ghost see identical
	// observations for identical rows.
	static bool ParseSourceRowObject(
		const TSharedPtr<FJsonObject>& RowObject,
		double& OutTimeSeconds,
		FString* OutPhaseName,
		FEmbodiedFusionSourceObservations& OutObservations);
	// The parser's landmark-name table (name -> EMediaPipePoseLandmark), exposed so the
	// wire's row SERIALIZER emits exactly the names the parser accepts.
	static const TMap<FString, EMediaPipePoseLandmark>& GetSourceRowLandmarkNames();
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
