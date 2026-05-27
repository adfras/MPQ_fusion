#include "MediaPipePoseTrackerComponent.h"

#include "MediaPipePoseCoordinate.h"
#include "MediaPipePoseLog.h"
#include "MediaPipePoseTextureReader.h"

#include "Engine/TextureRenderTarget2D.h"
#include "MediaPlayer.h"
#include "MediaTexture.h"
#include "Misc/Paths.h"
#include "Engine/Canvas.h"
#include "Kismet/KismetRenderingLibrary.h"

namespace
{
	TAutoConsoleVariable<int32> CVarMediaPipeInputMaxDimension(
		TEXT("mp.MediaPipeInputMaxDimension"),
		512,
		TEXT("Maximum media frame dimension sent to the MediaPipe pose wrapper. 0 keeps source resolution."));

	FString NormalizeProjectFilePath(const FString& InPath)
	{
		FString Path = InPath;
		Path.TrimStartAndEndInline();
		Path.ReplaceInline(TEXT("\\"), TEXT("/"));
		if (Path.IsEmpty())
		{
			return Path;
		}

		if (FPaths::IsRelative(Path))
		{
			const FString Rel = Path;
			if (Rel.StartsWith(TEXT("../")) || Rel.StartsWith(TEXT("..\\")))
			{
				Path = FPaths::ConvertRelativePathToFull(Rel);
			}
			else if (Rel.StartsWith(TEXT("Content/"), ESearchCase::IgnoreCase))
			{
				Path = FPaths::Combine(FPaths::ConvertRelativePathToFull(FPaths::ProjectDir()), Rel);
			}
			else if (Rel.StartsWith(TEXT("Movies/"), ESearchCase::IgnoreCase) || Rel.StartsWith(TEXT("MediaPipe/"), ESearchCase::IgnoreCase))
			{
				Path = FPaths::Combine(FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir()), Rel);
			}
			else
			{
				Path = FPaths::Combine(FPaths::ConvertRelativePathToFull(FPaths::ProjectDir()), Rel);
			}
		}

		Path = FPaths::ConvertRelativePathToFull(Path);
		FPaths::CollapseRelativeDirectories(Path);
		FPaths::NormalizeFilename(Path);
		return Path;
	}

	FString GetMediaPipeDriverBinariesDir()
	{
		static FString Cached;
		if (!Cached.IsEmpty())
		{
			return Cached;
		}

		Cached = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), TEXT("Binaries/Win64")));
		FPaths::CollapseRelativeDirectories(Cached);
		FPaths::NormalizeDirectoryName(Cached);
		return Cached;
	}

	FString GetMediaPipeDriverProjectRoot()
	{
		FString Root = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());
		FPaths::CollapseRelativeDirectories(Root);
		FPaths::NormalizeDirectoryName(Root);
		return Root;
	}

	void AccumulateMediaPipeRuntimeMs(int64& Count, double& TotalMs, double& MaxMs, double ValueMs)
	{
		++Count;
		TotalMs += ValueMs;
		MaxMs = FMath::Max(MaxMs, ValueMs);
	}

	bool GetMediaTextureResourceSize(UMediaTexture* MediaTexture, FIntPoint& OutSize)
	{
		OutSize = FIntPoint::ZeroValue;
		if (!MediaTexture)
		{
			return false;
		}

		FTextureResource* Resource = MediaTexture->GetResource();
		if (!Resource || !Resource->TextureRHI.IsValid())
		{
			return false;
		}

		const FIntVector SizeXYZ = Resource->TextureRHI->GetSizeXYZ();
		OutSize = FIntPoint(SizeXYZ.X, SizeXYZ.Y);
		return OutSize.X > 0 && OutSize.Y > 0;
	}

	int64 AllocateNativeVideoTimestampUs(const int64 CandidateTimestampUs, const double StepSeconds)
	{
		static FCriticalSection TimestampMutex;
		static int64 LastSubmittedTimestampUs = -1;
		static int64 TimestampOffsetUs = 0;

		const int64 StepUs = FMath::Max<int64>(static_cast<int64>(FMath::Max(0.0, StepSeconds) * 1000000.0) + 1, 1000);

		FScopeLock Lock(&TimestampMutex);
		int64 TimestampUs = CandidateTimestampUs + TimestampOffsetUs;
		if (LastSubmittedTimestampUs >= 0 && TimestampUs <= LastSubmittedTimestampUs)
		{
			TimestampOffsetUs += (LastSubmittedTimestampUs - TimestampUs) + StepUs;
			TimestampUs = CandidateTimestampUs + TimestampOffsetUs;
			UE_LOG(LogMediaPipePose, Verbose, TEXT("Adjusted MediaPipe native timestamp from %lld us to %lld us."), CandidateTimestampUs, TimestampUs);
		}
		LastSubmittedTimestampUs = TimestampUs;
		return TimestampUs;
	}
}

UMediaPipePoseTrackerComponent::UMediaPipePoseTrackerComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	PrimaryComponentTick.bTickEvenWhenPaused = true;
	bTickInEditor = true;
}

void UMediaPipePoseTrackerComponent::BeginPlay()
{
	Super::BeginPlay();
	Initialize();
}

void UMediaPipePoseTrackerComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Shutdown();
	Super::EndPlay(EndPlayReason);
}

void UMediaPipePoseTrackerComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	ProcessFrame();
}

bool UMediaPipePoseTrackerComponent::Initialize()
{
	FScopeLock Lock(&TrackerMutex);
	if (Tracker && Tracker->IsInitialized())
	{
		return true;
	}

	const FString DllPath = NormalizeProjectFilePath(WrapperDllPath.IsEmpty() ? ResolveDefaultDllPath() : WrapperDllPath);
	const FString ModelPath = NormalizeProjectFilePath(ConfigPath.IsEmpty() ? ResolveDefaultConfigPath() : ConfigPath);
	const FString ResolvedHandModelPath = bEnableHandLandmarker
		? NormalizeProjectFilePath(HandModelPath.IsEmpty() ? ResolveDefaultHandModelPath() : HandModelPath)
		: FString();
	const FMediaPipePoseNativeOptions NativeOptions = BuildNativeOptions();

	Tracker = MakeUnique<FMediaPipePoseTracker>();
	if (!Tracker->Initialize(DllPath, ModelPath, ResolvedHandModelPath, NativeOptions, bUseMockWrapper))
	{
		UE_LOG(LogMediaPipePose, Error, TEXT("Failed to initialize MediaPipe tracker."));
		Tracker.Reset();
		return false;
	}

	bImageFrameProcessed = false;
	bHasInjectedRawFrame = false;
	InjectedRawFrame = FMediaPipePoseFrame();
	InjectedRawFrameSerial = 0;
	LastMediaTimeSeconds = -1.0;
	EstimatedMediaFrameStepSeconds = 1.0 / 30.0;
	MediaTimestampOffsetUs = 0;
	LastMediaTimestampUs = 0;
	bHasLastMediaTimestamp = false;
	LastProcessWallTimeSeconds = -1.0;
	bMediaTextureReadbackInFlight = false;
	MediaTextureReadbackPixels.Reset();
	MediaTextureReadbackSize = FIntPoint(0, 0);
	MediaTextureReadbackSourceSize = FIntPoint(0, 0);
	MediaTextureReadbackTimestampUs = 0;
	MediaTextureReadbackEpoch = 0;
	MediaTextureReadbackBeginWallSeconds = -1.0;
	SourceEpoch = 0;
	DropFramesRemaining = 0;
	bNeedsFreshMediaFrameAfterDiscontinuity = false;
	bHasLastSourceSnapshot = false;
	LastSourceType = SourceType;
	LastSourceMediaTexture = SourceMediaTexture;
	LastSourceRenderTarget = SourceRenderTarget;
	LastImageFilePath = ImageFilePath;
	LastProcessedFrameSize = FIntPoint(0, 0);
	RuntimeStats = FMediaPipePosePipelineStats();
	if (Tracker)
	{
		Tracker->ResetRuntimeStats();
	}
	ResetLandmarkFilter();
	return true;
}

void UMediaPipePoseTrackerComponent::Shutdown()
{
	if (bMediaTextureReadbackInFlight)
	{
		MediaTextureReadbackFence.Wait();
		bMediaTextureReadbackInFlight = false;
	}

	FScopeLock Lock(&TrackerMutex);
	if (Tracker)
	{
		Tracker->Shutdown();
		Tracker.Reset();
	}
}

void UMediaPipePoseTrackerComponent::ResetForSourceDiscontinuity()
{
	++SourceEpoch;
	DropFramesRemaining = FMath::Max(DropFramesRemaining, DropFramesAfterDiscontinuity);
	bNeedsFreshMediaFrameAfterDiscontinuity = (SourceType == EMediaPipePoseFrameSource::MediaTexture);
	LastMediaTimeSeconds = -1.0;
	LastProcessWallTimeSeconds = -1.0;
	bImageFrameProcessed = false;
	if (Tracker)
	{
		Tracker->ClearLatestFrame(SourceEpoch);
	}
	ResetLandmarkFilter();
}

void UMediaPipePoseTrackerComponent::ResetRuntimeStats()
{
	RuntimeStats = FMediaPipePosePipelineStats();
	if (Tracker)
	{
		Tracker->ResetRuntimeStats();
	}
}

void UMediaPipePoseTrackerComponent::GetRuntimeStats(FMediaPipePosePipelineStats& OutStats) const
{
	OutStats = RuntimeStats;
	if (Tracker)
	{
		FMediaPipePosePipelineStats TrackerStats;
		Tracker->GetRuntimeStats(TrackerStats);
		OutStats.TrackerEnqueueCount = TrackerStats.TrackerEnqueueCount;
		OutStats.TrackerClearCount = TrackerStats.TrackerClearCount;
		OutStats.TrackerPublishCount = TrackerStats.TrackerPublishCount;
		OutStats.TrackerStaleRejectCount = TrackerStats.TrackerStaleRejectCount;
		OutStats.WorkerPendingOverwriteCount = TrackerStats.WorkerPendingOverwriteCount;
		OutStats.WorkerInvalidInputCount = TrackerStats.WorkerInvalidInputCount;
		OutStats.WorkerProcessCount = TrackerStats.WorkerProcessCount;
		OutStats.WorkerProcessFailCount = TrackerStats.WorkerProcessFailCount;
		OutStats.WorkerLandmarkFailCount = TrackerStats.WorkerLandmarkFailCount;
		OutStats.WorkerQueueLatencySampleCount = TrackerStats.WorkerQueueLatencySampleCount;
		OutStats.WorkerQueueLatencyTotalMs = TrackerStats.WorkerQueueLatencyTotalMs;
		OutStats.WorkerQueueLatencyMaxMs = TrackerStats.WorkerQueueLatencyMaxMs;
		OutStats.WorkerNativeProcessSampleCount = TrackerStats.WorkerNativeProcessSampleCount;
		OutStats.WorkerNativeProcessTotalMs = TrackerStats.WorkerNativeProcessTotalMs;
		OutStats.WorkerNativeProcessMaxMs = TrackerStats.WorkerNativeProcessMaxMs;
		OutStats.WorkerGetLandmarksSampleCount = TrackerStats.WorkerGetLandmarksSampleCount;
		OutStats.WorkerGetLandmarksTotalMs = TrackerStats.WorkerGetLandmarksTotalMs;
		OutStats.WorkerGetLandmarksMaxMs = TrackerStats.WorkerGetLandmarksMaxMs;
	}
}

UTextureRenderTarget2D* UMediaPipePoseTrackerComponent::GetOrCreateMediaTextureInferenceRenderTarget(
	const FIntPoint SourceSize,
	const int32 MaxDimension,
	FIntPoint& OutInferenceSize)
{
	OutInferenceSize = SourceSize;
	const int32 SourceMaxDimension = FMath::Max(SourceSize.X, SourceSize.Y);
	if (SourceSize.X <= 0 || SourceSize.Y <= 0 || MaxDimension <= 0 || SourceMaxDimension <= MaxDimension)
	{
		return nullptr;
	}

	const float Scale = static_cast<float>(MaxDimension) / static_cast<float>(SourceMaxDimension);
	OutInferenceSize = FIntPoint(
		FMath::Max(1, FMath::RoundToInt(static_cast<float>(SourceSize.X) * Scale)),
		FMath::Max(1, FMath::RoundToInt(static_cast<float>(SourceSize.Y) * Scale)));

	if (MediaTextureInferenceRenderTarget
		&& MediaTextureInferenceRenderTarget->SizeX == OutInferenceSize.X
		&& MediaTextureInferenceRenderTarget->SizeY == OutInferenceSize.Y)
	{
		return MediaTextureInferenceRenderTarget;
	}

	MediaTextureInferenceRenderTarget = NewObject<UTextureRenderTarget2D>(this, TEXT("MediaPipeInferenceRenderTarget"));
	if (!MediaTextureInferenceRenderTarget)
	{
		return nullptr;
	}

	MediaTextureInferenceRenderTarget->RenderTargetFormat = RTF_RGBA8;
	MediaTextureInferenceRenderTarget->ClearColor = FLinearColor::Black;
	MediaTextureInferenceRenderTarget->bAutoGenerateMips = false;
	MediaTextureInferenceRenderTarget->InitAutoFormat(OutInferenceSize.X, OutInferenceSize.Y);
	MediaTextureInferenceRenderTarget->UpdateResourceImmediate(true);
	return MediaTextureInferenceRenderTarget;
}

bool UMediaPipePoseTrackerComponent::DrawMediaTextureToInferenceRenderTarget(UMediaTexture* MediaTexture, UTextureRenderTarget2D* RenderTarget)
{
	if (!MediaTexture || !RenderTarget)
	{
		return false;
	}

	UCanvas* Canvas = nullptr;
	FVector2D CanvasSize = FVector2D::ZeroVector;
	FDrawToRenderTargetContext RenderContext;
	UKismetRenderingLibrary::BeginDrawCanvasToRenderTarget(this, RenderTarget, Canvas, CanvasSize, RenderContext);
	if (!Canvas)
	{
		UKismetRenderingLibrary::EndDrawCanvasToRenderTarget(this, RenderContext);
		return false;
	}

	Canvas->K2_DrawTexture(
		MediaTexture,
		FVector2D::ZeroVector,
		CanvasSize,
		FVector2D::ZeroVector,
		FVector2D(1.0, 1.0),
		FLinearColor::White,
		BLEND_Opaque,
		0.0f,
		FVector2D::ZeroVector);
	UKismetRenderingLibrary::EndDrawCanvasToRenderTarget(this, RenderContext);
	return true;
}

bool UMediaPipePoseTrackerComponent::ProcessFrame()
{
	{
		FScopeLock Lock(&TrackerMutex);
		if (!Tracker || !Tracker->IsInitialized())
		{
			return false;
		}
	}

	++RuntimeStats.ComponentProcessCalls;
	bool bEnqueuedAny = false;

	if (!bHasLastSourceSnapshot)
	{
		bHasLastSourceSnapshot = true;
		LastSourceType = SourceType;
		LastSourceMediaTexture = SourceMediaTexture;
		LastSourceRenderTarget = SourceRenderTarget;
		LastImageFilePath = ImageFilePath;
	}
	else
	{
		const bool bTypeChanged = (SourceType != LastSourceType);
		const bool bMediaChanged = (SourceType == EMediaPipePoseFrameSource::MediaTexture) && (SourceMediaTexture != LastSourceMediaTexture.Get());
		const bool bRtChanged = (SourceType == EMediaPipePoseFrameSource::RenderTarget) && (SourceRenderTarget != LastSourceRenderTarget.Get());
		const bool bImageChanged = (SourceType == EMediaPipePoseFrameSource::ImageFile) && (ImageFilePath != LastImageFilePath);
		if (bTypeChanged || bMediaChanged || bRtChanged || bImageChanged)
		{
			ResetForSourceDiscontinuity();
			LastSourceType = SourceType;
			LastSourceMediaTexture = SourceMediaTexture;
			LastSourceRenderTarget = SourceRenderTarget;
			LastImageFilePath = ImageFilePath;
		}
	}

	if (SourceType == EMediaPipePoseFrameSource::MediaTexture && bAsyncMediaTextureReadback && bMediaTextureReadbackInFlight)
	{
		if (MediaTextureReadbackFence.IsFenceComplete())
		{
			++RuntimeStats.ComponentAsyncReadbackCompleteCount;
			if (MediaTextureReadbackBeginWallSeconds >= 0.0)
			{
				const double ReadbackLatencyMs = FMath::Max(0.0, (FPlatformTime::Seconds() - MediaTextureReadbackBeginWallSeconds) * 1000.0);
				AccumulateMediaPipeRuntimeMs(
					RuntimeStats.ComponentReadbackLatencySampleCount,
					RuntimeStats.ComponentReadbackLatencyTotalMs,
					RuntimeStats.ComponentReadbackLatencyMaxMs,
					ReadbackLatencyMs);
			}
			const bool bStale = (MediaTextureReadbackEpoch != SourceEpoch);
			if (!bStale)
			{
				LastProcessedFrameSize = MediaTextureReadbackSize;
				RuntimeStats.LastCaptureSize = (MediaTextureReadbackSourceSize.X > 0 && MediaTextureReadbackSourceSize.Y > 0)
					? MediaTextureReadbackSourceSize
					: MediaTextureReadbackSize;
				if (DropFramesRemaining > 0)
				{
					DropFramesRemaining--;
					++RuntimeStats.ComponentDroppedWarmupFrames;
				}
				else
				{
					TArray<uint8> Rgb;
					FIntPoint InferenceSize(0, 0);
					const double ConversionStartSeconds = FPlatformTime::Seconds();
					FMediaPipePoseTextureReader::ConvertBGRAtoRGBResized(
						MediaTextureReadbackPixels,
						MediaTextureReadbackSize,
						CVarMediaPipeInputMaxDimension.GetValueOnAnyThread(),
						Rgb,
						InferenceSize);
					const double ConversionMs = FMath::Max(0.0, (FPlatformTime::Seconds() - ConversionStartSeconds) * 1000.0);
					AccumulateMediaPipeRuntimeMs(
						RuntimeStats.ComponentConversionCount,
						RuntimeStats.ComponentConversionTotalMs,
						RuntimeStats.ComponentConversionMaxMs,
						ConversionMs);
					RuntimeStats.LastInferenceSize = InferenceSize;
					if (InferenceSize.X <= 0 || InferenceSize.Y <= 0 || Rgb.Num() == 0)
					{
						++RuntimeStats.ComponentReadFailCount;
						bMediaTextureReadbackInFlight = false;
						return bEnqueuedAny;
					}
					FScopeLock Lock(&TrackerMutex);
					if (Tracker && Tracker->IsInitialized())
					{
						const int64 EnqueueTimestampUs = AllocateNativeVideoTimestampUs(MediaTextureReadbackTimestampUs, EstimatedMediaFrameStepSeconds);
						const bool bEnqueued = Tracker->EnqueueFrame(MoveTemp(Rgb), InferenceSize.X, InferenceSize.Y, EnqueueTimestampUs, MediaTextureReadbackEpoch);
						bEnqueuedAny = bEnqueued || bEnqueuedAny;
						if (bEnqueued)
						{
							++RuntimeStats.ComponentEnqueueSuccessCount;
							bNeedsFreshMediaFrameAfterDiscontinuity = false;
						}
						else
						{
							++RuntimeStats.ComponentEnqueueFailCount;
						}
					}
					else
					{
						++RuntimeStats.ComponentEnqueueFailCount;
					}
				}
			}
			else
			{
				++RuntimeStats.ComponentAsyncReadbackStaleDrops;
			}
			bMediaTextureReadbackInFlight = false;
			MediaTextureReadbackBeginWallSeconds = -1.0;
		}
	}

	int64 TimestampUs = GetTimestampUs();

	if (SourceType == EMediaPipePoseFrameSource::MediaTexture)
	{
		if (SourceMediaTexture)
		{
			if (UMediaPlayer* MediaPlayer = SourceMediaTexture->GetMediaPlayer())
			{
				const double MediaSec = MediaPlayer->GetTime().GetTotalSeconds();
				const float FrameRate = MediaPlayer->GetVideoTrackFrameRate(INDEX_NONE, INDEX_NONE);
				const double StepSec = (FrameRate > 1.0f) ? (1.0 / static_cast<double>(FrameRate)) : EstimatedMediaFrameStepSeconds;
				EstimatedMediaFrameStepSeconds = StepSec;

				const bool bTimeWentBack = (LastMediaTimeSeconds >= 0.0) && (MediaSec + 1e-4 < LastMediaTimeSeconds);
				double MinAdvance = StepSec * 0.9;
				if (MaxProcessRateHz > 0.0f)
				{
					MinAdvance = FMath::Max(MinAdvance, 1.0 / static_cast<double>(MaxProcessRateHz));
				}
				MinAdvance = FMath::Max(MinAdvance, 1.0 / 240.0);
				RuntimeStats.LastMediaTimeSeconds = MediaSec;
				RuntimeStats.LastMediaFrameRate = FrameRate;
				RuntimeStats.LastMediaStepSeconds = StepSec;
				RuntimeStats.LastMediaMinAdvanceSeconds = MinAdvance;

				const bool bNeedsFreshPausedSample = bNeedsFreshMediaFrameAfterDiscontinuity;
				if (!bNeedsFreshPausedSample && !bTimeWentBack && LastMediaTimeSeconds >= 0.0 && (MediaSec - LastMediaTimeSeconds) < MinAdvance)
				{
					++RuntimeStats.ComponentMediaTimestampGateSkips;
					return true;
				}

				LastMediaTimeSeconds = MediaSec;

				const int64 MediaUs = static_cast<int64>(MediaSec * 1000000.0);
				int64 CandidateUs = MediaTimestampOffsetUs + MediaUs;
				if (bHasLastMediaTimestamp && CandidateUs <= LastMediaTimestampUs)
				{
					const int64 StepUs = FMath::Max<int64>(static_cast<int64>(FMath::Max(0.0, StepSec) * 1000000.0) + 1, 1000);
					MediaTimestampOffsetUs += (LastMediaTimestampUs - CandidateUs) + StepUs;
					CandidateUs = MediaTimestampOffsetUs + MediaUs;
				}

				TimestampUs = CandidateUs;
				LastMediaTimestampUs = CandidateUs;
				bHasLastMediaTimestamp = true;
			}
		}

		if (bAsyncMediaTextureReadback)
		{
			if (!bMediaTextureReadbackInFlight)
			{
				const int32 MaxInputDimension = CVarMediaPipeInputMaxDimension.GetValueOnAnyThread();
				FIntPoint SourceTextureSize(0, 0);
				FIntPoint InferenceTargetSize(0, 0);
				UTextureRenderTarget2D* InferenceRenderTarget = nullptr;
				if (GetMediaTextureResourceSize(SourceMediaTexture, SourceTextureSize))
				{
					InferenceRenderTarget = GetOrCreateMediaTextureInferenceRenderTarget(SourceTextureSize, MaxInputDimension, InferenceTargetSize);
				}

				bool bReadbackStarted = false;
				if (InferenceRenderTarget && DrawMediaTextureToInferenceRenderTarget(SourceMediaTexture, InferenceRenderTarget))
				{
					bReadbackStarted = FMediaPipePoseTextureReader::BeginReadRenderTarget(
						InferenceRenderTarget,
						MediaTextureReadbackPixels,
						MediaTextureReadbackSize,
						MediaTextureReadbackFence);
					MediaTextureReadbackSourceSize = SourceTextureSize;
				}

				if (!bReadbackStarted)
				{
					bReadbackStarted = FMediaPipePoseTextureReader::BeginReadMediaTexture(
						SourceMediaTexture,
						MediaTextureReadbackPixels,
						MediaTextureReadbackSize,
						MediaTextureReadbackFence);
					MediaTextureReadbackSourceSize = MediaTextureReadbackSize;
				}

				if (!bReadbackStarted)
				{
					++RuntimeStats.ComponentAsyncReadbackBeginFailCount;
					++RuntimeStats.ComponentReadFailCount;
					return bEnqueuedAny;
				}

				++RuntimeStats.ComponentAsyncReadbackBeginCount;
				bMediaTextureReadbackInFlight = true;
				MediaTextureReadbackTimestampUs = TimestampUs;
				MediaTextureReadbackEpoch = SourceEpoch;
				MediaTextureReadbackBeginWallSeconds = FPlatformTime::Seconds();
			}
			else
			{
				++RuntimeStats.ComponentAsyncReadbackInFlightSkips;
			}

			return true;
		}

		TArray<FColor> Pixels;
		FIntPoint Size(0, 0);
		FIntPoint SourceTextureSize(0, 0);
		FIntPoint InferenceTargetSize(0, 0);
		UTextureRenderTarget2D* InferenceRenderTarget = nullptr;
		if (GetMediaTextureResourceSize(SourceMediaTexture, SourceTextureSize))
		{
			InferenceRenderTarget = GetOrCreateMediaTextureInferenceRenderTarget(
				SourceTextureSize,
				CVarMediaPipeInputMaxDimension.GetValueOnAnyThread(),
				InferenceTargetSize);
		}

		bool bReadOk = false;
		if (InferenceRenderTarget && DrawMediaTextureToInferenceRenderTarget(SourceMediaTexture, InferenceRenderTarget))
		{
			bReadOk = FMediaPipePoseTextureReader::ReadRenderTarget(InferenceRenderTarget, Pixels, Size);
		}
		if (!bReadOk)
		{
			bReadOk = FMediaPipePoseTextureReader::ReadMediaTexture(SourceMediaTexture, Pixels, Size);
			SourceTextureSize = Size;
		}
		if (!bReadOk)
		{
			++RuntimeStats.ComponentReadFailCount;
			return bEnqueuedAny;
		}

		TArray<uint8> Rgb;
		FIntPoint InferenceSize(0, 0);
		const double ConversionStartSeconds = FPlatformTime::Seconds();
		FMediaPipePoseTextureReader::ConvertBGRAtoRGBResized(
			Pixels,
			Size,
			CVarMediaPipeInputMaxDimension.GetValueOnAnyThread(),
			Rgb,
			InferenceSize);
		const double ConversionMs = FMath::Max(0.0, (FPlatformTime::Seconds() - ConversionStartSeconds) * 1000.0);
		AccumulateMediaPipeRuntimeMs(
			RuntimeStats.ComponentConversionCount,
			RuntimeStats.ComponentConversionTotalMs,
			RuntimeStats.ComponentConversionMaxMs,
			ConversionMs);
		RuntimeStats.LastInferenceSize = InferenceSize;
		if (InferenceSize.X <= 0 || InferenceSize.Y <= 0 || Rgb.Num() == 0)
		{
			++RuntimeStats.ComponentReadFailCount;
			return bEnqueuedAny;
		}

		LastProcessedFrameSize = Size;
		RuntimeStats.LastCaptureSize = (SourceTextureSize.X > 0 && SourceTextureSize.Y > 0) ? SourceTextureSize : Size;
		if (DropFramesRemaining > 0)
		{
			DropFramesRemaining--;
			++RuntimeStats.ComponentDroppedWarmupFrames;
			return bEnqueuedAny;
		}

		FScopeLock Lock(&TrackerMutex);
		if (Tracker && Tracker->IsInitialized())
		{
			const int64 EnqueueTimestampUs = AllocateNativeVideoTimestampUs(TimestampUs, EstimatedMediaFrameStepSeconds);
			const bool bEnqueued = Tracker->EnqueueFrame(MoveTemp(Rgb), InferenceSize.X, InferenceSize.Y, EnqueueTimestampUs, SourceEpoch);
			if (bEnqueued)
			{
				++RuntimeStats.ComponentEnqueueSuccessCount;
				bNeedsFreshMediaFrameAfterDiscontinuity = false;
			}
			else
			{
				++RuntimeStats.ComponentEnqueueFailCount;
			}
			return bEnqueued || bEnqueuedAny;
		}
		++RuntimeStats.ComponentEnqueueFailCount;
		return bEnqueuedAny;
	}

	if (SourceType == EMediaPipePoseFrameSource::RenderTarget)
	{
		if (MaxProcessRateHz > 0.0f)
		{
			const double Now = FPlatformTime::Seconds();
			const double MinDt = 1.0 / static_cast<double>(MaxProcessRateHz);
			if (LastProcessWallTimeSeconds >= 0.0 && (Now - LastProcessWallTimeSeconds) < MinDt)
			{
				return true;
			}
			LastProcessWallTimeSeconds = Now;
		}

		TArray<FColor> Pixels;
		FIntPoint Size(0, 0);
		if (!FMediaPipePoseTextureReader::ReadRenderTarget(SourceRenderTarget, Pixels, Size))
		{
			++RuntimeStats.ComponentReadFailCount;
			return bEnqueuedAny;
		}

		TArray<uint8> Rgb;
		FIntPoint InferenceSize(0, 0);
		const double ConversionStartSeconds = FPlatformTime::Seconds();
		FMediaPipePoseTextureReader::ConvertBGRAtoRGBResized(
			Pixels,
			Size,
			CVarMediaPipeInputMaxDimension.GetValueOnAnyThread(),
			Rgb,
			InferenceSize);
		const double ConversionMs = FMath::Max(0.0, (FPlatformTime::Seconds() - ConversionStartSeconds) * 1000.0);
		AccumulateMediaPipeRuntimeMs(
			RuntimeStats.ComponentConversionCount,
			RuntimeStats.ComponentConversionTotalMs,
			RuntimeStats.ComponentConversionMaxMs,
			ConversionMs);
		RuntimeStats.LastInferenceSize = InferenceSize;
		if (InferenceSize.X <= 0 || InferenceSize.Y <= 0 || Rgb.Num() == 0)
		{
			++RuntimeStats.ComponentReadFailCount;
			return bEnqueuedAny;
		}

		LastProcessedFrameSize = Size;
		RuntimeStats.LastCaptureSize = Size;
		if (DropFramesRemaining > 0)
		{
			DropFramesRemaining--;
			++RuntimeStats.ComponentDroppedWarmupFrames;
			return bEnqueuedAny;
		}

		FScopeLock Lock(&TrackerMutex);
		if (Tracker && Tracker->IsInitialized())
		{
			const double StepSeconds = (MaxProcessRateHz > 0.0f) ? (1.0 / static_cast<double>(MaxProcessRateHz)) : (1.0 / 30.0);
			const int64 EnqueueTimestampUs = AllocateNativeVideoTimestampUs(TimestampUs, StepSeconds);
			const bool bEnqueued = Tracker->EnqueueFrame(MoveTemp(Rgb), InferenceSize.X, InferenceSize.Y, EnqueueTimestampUs, SourceEpoch);
			if (bEnqueued)
			{
				++RuntimeStats.ComponentEnqueueSuccessCount;
			}
			else
			{
				++RuntimeStats.ComponentEnqueueFailCount;
			}
			return bEnqueued || bEnqueuedAny;
		}
		++RuntimeStats.ComponentEnqueueFailCount;
		return bEnqueuedAny;
	}

	if (bProcessOnce && bImageFrameProcessed)
	{
		return true;
	}

	FIntPoint Size(0, 0);
	TArray<uint8> Rgb;
	if (!FMediaPipePoseTextureReader::ReadImageFile(ImageFilePath, Rgb, Size))
	{
		++RuntimeStats.ComponentReadFailCount;
		return bEnqueuedAny;
	}

	LastProcessedFrameSize = Size;
	RuntimeStats.LastCaptureSize = Size;
	RuntimeStats.LastInferenceSize = Size;
	if (DropFramesRemaining > 0)
	{
		DropFramesRemaining--;
		++RuntimeStats.ComponentDroppedWarmupFrames;
		return bEnqueuedAny;
	}

	FScopeLock Lock(&TrackerMutex);
	if (!Tracker || !Tracker->IsInitialized())
	{
		++RuntimeStats.ComponentEnqueueFailCount;
		return bEnqueuedAny;
	}

	const double StepSeconds = (MaxProcessRateHz > 0.0f) ? (1.0 / static_cast<double>(MaxProcessRateHz)) : (1.0 / 30.0);
	const int64 EnqueueTimestampUs = AllocateNativeVideoTimestampUs(TimestampUs, StepSeconds);
	const bool bEnqueued = Tracker->EnqueueFrame(MoveTemp(Rgb), Size.X, Size.Y, EnqueueTimestampUs, SourceEpoch);
	if (bEnqueued)
	{
		++RuntimeStats.ComponentEnqueueSuccessCount;
		bImageFrameProcessed = true;
	}
	else
	{
		++RuntimeStats.ComponentEnqueueFailCount;
	}
	return bEnqueued || bEnqueuedAny;
}

bool UMediaPipePoseTrackerComponent::GetLatestFrame(FMediaPipePoseFrame& OutFrame) const
{
	FMediaPipePoseFrame RawFrame;
	bool bRawFrameWasInjected = false;
	uint64 RawFrameSerial = 0;
	{
		FScopeLock Lock(&TrackerMutex);
		if (bHasInjectedRawFrame)
		{
			RawFrame = InjectedRawFrame;
			bRawFrameWasInjected = true;
			RawFrameSerial = InjectedRawFrameSerial;
		}
		else if (!Tracker || !Tracker->GetLatestFrame(RawFrame) || !RawFrame.bValid)
		{
			return false;
		}
	}

	FMediaPipeSourceConditionerOptions Options = FMediaPipeSourceConditioner::MakeDefaultOptions();
	Options.bEnabled = Options.bEnabled && bUseSourceConditioning;
	if (!Options.bEnabled || RawFrame.bSourceConditioned || (bRawFrameWasInjected && !bConditionInjectedFrames))
	{
		OutFrame = RawFrame;
		return OutFrame.bValid;
	}

	FScopeLock ConditionerLock(&SourceConditionerMutex);
	if (bHasConditionedFrameCache
		&& ConditionedFrameInputTimestampUs == RawFrame.TimestampUs
		&& ConditionedFrameInputSerial == RawFrameSerial)
	{
		OutFrame = ConditionedFrameCache;
		return OutFrame.bValid;
	}

	if (!SourceConditioner.ConditionFrame(RawFrame, WorldScale, bMirrorLandmarksLR, Options, ConditionedFrameCache))
	{
		bHasConditionedFrameCache = false;
		OutFrame = RawFrame;
		return false;
	}

	ConditionedFrameInputTimestampUs = RawFrame.TimestampUs;
	ConditionedFrameInputSerial = RawFrameSerial;
	bHasConditionedFrameCache = true;
	OutFrame = ConditionedFrameCache;
	return OutFrame.bValid;
}

bool UMediaPipePoseTrackerComponent::GetCachedConditionedFrame(FMediaPipePoseFrame& OutFrame) const
{
	return GetLatestFrame(OutFrame);
}

bool UMediaPipePoseTrackerComponent::GetLatestRawFrame(FMediaPipePoseFrame& OutFrame) const
{
	FScopeLock Lock(&TrackerMutex);
	if (bHasInjectedRawFrame)
	{
		OutFrame = InjectedRawFrame;
		return OutFrame.bValid;
	}

	if (!Tracker)
	{
		return false;
	}

	return Tracker->GetLatestFrame(OutFrame) && OutFrame.bValid;
}

void UMediaPipePoseTrackerComponent::SetInjectedRawFrame(const FMediaPipePoseFrame& InFrame)
{
	bool bEnteredInjectedPlayback = false;
	{
		FScopeLock Lock(&TrackerMutex);
		bEnteredInjectedPlayback = !bHasInjectedRawFrame && InFrame.bValid;
		InjectedRawFrame = InFrame;
		bHasInjectedRawFrame = InFrame.bValid;
		++InjectedRawFrameSerial;
	}

	if (bEnteredInjectedPlayback)
	{
		ResetLandmarkFilter();
	}
}

void UMediaPipePoseTrackerComponent::ClearInjectedRawFrame()
{
	bool bWasInjected = false;
	{
		FScopeLock Lock(&TrackerMutex);
		bWasInjected = bHasInjectedRawFrame;
		bHasInjectedRawFrame = false;
		InjectedRawFrame = FMediaPipePoseFrame();
		++InjectedRawFrameSerial;
	}

	if (bWasInjected)
	{
		ResetLandmarkFilter();
	}
}

bool UMediaPipePoseTrackerComponent::GetLastProcessedFrameSize(FIntPoint& OutSize) const
{
	OutSize = LastProcessedFrameSize;
	return OutSize.X > 0 && OutSize.Y > 0;
}

bool UMediaPipePoseTrackerComponent::GetLandmarkNormalized(int32 Index, FMediaPipePoseLandmark& OutLandmark) const
{
	FMediaPipePoseFrame Frame;
	if (!GetLatestFrame(Frame) || !Frame.Normalized.IsValidIndex(Index))
	{
		return false;
	}

	OutLandmark = Frame.Normalized.Points[Index];
	return true;
}

bool UMediaPipePoseTrackerComponent::GetLandmarkWorld(int32 Index, FMediaPipePoseLandmark& OutLandmark) const
{
	FMediaPipePoseFrame Frame;
	if (!GetLatestFrame(Frame) || !Frame.World.IsValidIndex(Index))
	{
		return false;
	}

	OutLandmark = Frame.World.Points[Index];
	return true;
}

void UMediaPipePoseTrackerComponent::ResetLandmarkFilter()
{
	{
		FScopeLock ConditionerLock(&SourceConditionerMutex);
		SourceConditioner.Reset();
		bHasConditionedFrameCache = false;
		ConditionedFrameInputTimestampUs = 0;
		ConditionedFrameInputSerial = 0;
		ConditionedFrameCache = FMediaPipePoseFrame();
	}

}

int64 UMediaPipePoseTrackerComponent::GetTimestampUs() const
{
	const double Seconds = FPlatformTime::Seconds();
	return static_cast<int64>(Seconds * 1000000.0);
}

FString UMediaPipePoseTrackerComponent::ResolveDefaultDllPath() const
{
	TArray<FString> CandidatePaths;
	const FString BinDir = GetMediaPipeDriverBinariesDir();
	if (!BinDir.IsEmpty())
	{
		CandidatePaths.Add(FPaths::Combine(BinDir, TEXT("mediapipe/ump_shared.dll")));
		CandidatePaths.Add(FPaths::Combine(BinDir, TEXT("ump_shared.dll")));
	}

	const FString ProjectRoot = GetMediaPipeDriverProjectRoot();
	if (!ProjectRoot.IsEmpty())
	{
		CandidatePaths.Add(FPaths::Combine(ProjectRoot, TEXT("Binaries/Win64/mediapipe/ump_shared.dll")));
		CandidatePaths.Add(FPaths::Combine(ProjectRoot, TEXT("ThirdParty/mediapipe_wrapper/ump_shared.dll")));
	}

	for (FString Candidate : CandidatePaths)
	{
		Candidate = NormalizeProjectFilePath(Candidate);
		if (FPaths::FileExists(Candidate))
		{
			return Candidate;
		}
	}

	return CandidatePaths.Num() > 0
		? NormalizeProjectFilePath(CandidatePaths.Last())
		: NormalizeProjectFilePath(TEXT("ThirdParty/mediapipe_wrapper/ump_shared.dll"));
}

FString UMediaPipePoseTrackerComponent::ResolveDefaultConfigPath() const
{
	FString ContentDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir());
	const FString ProjectRoot = GetMediaPipeDriverProjectRoot();
	if (!ProjectRoot.IsEmpty())
	{
		ContentDir = FPaths::Combine(ProjectRoot, TEXT("Content"));
	}

	const FString Heavy = FPaths::Combine(ContentDir, TEXT("MediaPipe/pose_landmarker_heavy.task"));
	if (FPaths::FileExists(Heavy))
	{
		return Heavy;
	}

	const FString Full = FPaths::Combine(ContentDir, TEXT("MediaPipe/pose_landmarker_full.task"));
	if (FPaths::FileExists(Full))
	{
		return Full;
	}

	return FPaths::Combine(ContentDir, TEXT("MediaPipe/pose_landmarker.task"));
}

FString UMediaPipePoseTrackerComponent::ResolveDefaultHandModelPath() const
{
	FString ContentDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir());
	const FString ProjectRoot = GetMediaPipeDriverProjectRoot();
	if (!ProjectRoot.IsEmpty())
	{
		ContentDir = FPaths::Combine(ProjectRoot, TEXT("Content"));
	}

	const FString Hand = FPaths::Combine(ContentDir, TEXT("MediaPipe/hand_landmarker.task"));
	return FPaths::FileExists(Hand) ? Hand : FString();
}

FMediaPipePoseNativeOptions UMediaPipePoseTrackerComponent::BuildNativeOptions() const
{
	FMediaPipePoseNativeOptions Options;
	Options.bEnableHands = bEnableHandLandmarker;
	Options.NumPoses = FMath::Clamp(NumPoses, 1, 4);
	Options.MinPoseDetectionConfidence = FMath::Clamp(MinPoseDetectionConfidence, 0.0f, 1.0f);
	Options.MinPosePresenceConfidence = FMath::Clamp(MinPosePresenceConfidence, 0.0f, 1.0f);
	Options.MinTrackingConfidence = FMath::Clamp(MinTrackingConfidence, 0.0f, 1.0f);
	Options.bOutputSegmentationMasks = bOutputSegmentationMasks;
	Options.NumHands = FMath::Clamp(NumHands, 1, 2);
	Options.MinHandDetectionConfidence = FMath::Clamp(MinHandDetectionConfidence, 0.0f, 1.0f);
	Options.MinHandPresenceConfidence = FMath::Clamp(MinHandPresenceConfidence, 0.0f, 1.0f);
	Options.MinHandTrackingConfidence = FMath::Clamp(MinHandTrackingConfidence, 0.0f, 1.0f);
	return Options;
}
