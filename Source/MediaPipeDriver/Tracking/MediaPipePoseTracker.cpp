#include "MediaPipePoseTracker.h"

#include "MediaPipePoseLog.h"
#include "MediaPipePoseWorker.h"

#include "HAL/RunnableThread.h"

FMediaPipePoseTracker::FMediaPipePoseTracker() = default;

FMediaPipePoseTracker::~FMediaPipePoseTracker()
{
	Shutdown();
}

bool FMediaPipePoseTracker::Initialize(
	const FString& DllPath,
	const FString& PoseModelPath,
	const FString& HandModelPath,
	const FString& HolisticModelPath,
	const FMediaPipePoseNativeOptions& NativeOptions,
	bool bUseMock)
{
	if (IsInitialized())
	{
		return true;
	}

	if (bUseMock)
	{
		if (!Wrapper.InitMock())
		{
			return false;
		}
	}
	else
	{
		if (!Wrapper.Load(DllPath))
		{
			return false;
		}

		if (!Wrapper.Init(PoseModelPath, HandModelPath, HolisticModelPath, NativeOptions))
		{
			Wrapper.Unload();
			return false;
		}
	}

	UE_LOG(LogMediaPipePose, Log, TEXT("MediaPipe tracker initialized (mock=%s poseModel=%s handModel=%s holisticModel=%s)"),
		bUseMock ? TEXT("true") : TEXT("false"),
		*PoseModelPath,
		HandModelPath.IsEmpty() ? TEXT("none") : *HandModelPath,
		HolisticModelPath.IsEmpty() ? TEXT("none") : *HolisticModelPath);

	Worker = MakeUnique<FMediaPipePoseWorker>(Wrapper, [this](const FMediaPipePoseFrame& Frame, const int32 SourceEpoch)
	{
		HandleWorkerFrame(Frame, SourceEpoch);
	});

	WorkerThread = TUniquePtr<FRunnableThread>(FRunnableThread::Create(Worker.Get(), TEXT("MediaPipePoseWorker")));
	if (!WorkerThread.IsValid())
	{
		UE_LOG(LogMediaPipePose, Error, TEXT("Failed to start MediaPipe worker thread."));
		Worker.Reset();
		Wrapper.Shutdown();
		Wrapper.Unload();
		return false;
	}

	return true;
}

void FMediaPipePoseTracker::Shutdown()
{
	if (Worker)
	{
		Worker->Stop();
	}

	if (WorkerThread)
	{
		WorkerThread->WaitForCompletion();
		WorkerThread.Reset();
	}

	Worker.Reset();
	Wrapper.Shutdown();
	Wrapper.Unload();
}

bool FMediaPipePoseTracker::EnqueueFrame(TArray<uint8>&& Rgb, int32 Width, int32 Height, int64 TimestampUs, int32 SourceEpoch)
{
	if (!IsInitialized() || !Worker)
	{
		return false;
	}

	TSharedPtr<FMediaPipePoseInputFrame> Frame = MakeShared<FMediaPipePoseInputFrame>();
	Frame->Rgb = MoveTemp(Rgb);
	Frame->Width = Width;
	Frame->Height = Height;
	Frame->TimestampUs = TimestampUs;
	Frame->SourceEpoch = SourceEpoch;
	Frame->EnqueuedWallSeconds = FPlatformTime::Seconds();

	{
		FScopeLock StatsLock(&StatsMutex);
		++RuntimeStats.TrackerEnqueueCount;
	}
	return Worker->EnqueueFrame(Frame);
}

void FMediaPipePoseTracker::ClearLatestFrame(int32 SourceEpoch)
{
	FScopeLock Lock(&LatestMutex);
	CurrentEpoch = SourceEpoch;
	LatestFrame = FMediaPipePoseFrame();
	bHasFrame = false;

	FScopeLock StatsLock(&StatsMutex);
	++RuntimeStats.TrackerClearCount;
}

bool FMediaPipePoseTracker::GetLatestFrame(FMediaPipePoseFrame& OutFrame) const
{
	FScopeLock Lock(&LatestMutex);
	if (!bHasFrame)
	{
		return false;
	}

	OutFrame = LatestFrame;
	return true;
}

bool FMediaPipePoseTracker::IsInitialized() const
{
	return Wrapper.IsReady();
}

void FMediaPipePoseTracker::HandleWorkerFrame(const FMediaPipePoseFrame& Frame, int32 SourceEpoch)
{
	FScopeLock Lock(&LatestMutex);
	if (SourceEpoch != CurrentEpoch)
	{
		FScopeLock StatsLock(&StatsMutex);
		++RuntimeStats.TrackerStaleRejectCount;
		return;
	}

	LatestFrame = Frame;
	bHasFrame = Frame.bValid;

	if (Frame.bValid)
	{
		FScopeLock StatsLock(&StatsMutex);
		++RuntimeStats.TrackerPublishCount;
	}
}

void FMediaPipePoseTracker::GetRuntimeStats(FMediaPipePosePipelineStats& OutStats) const
{
	{
		FScopeLock StatsLock(&StatsMutex);
		OutStats.TrackerEnqueueCount = RuntimeStats.TrackerEnqueueCount;
		OutStats.TrackerClearCount = RuntimeStats.TrackerClearCount;
		OutStats.TrackerPublishCount = RuntimeStats.TrackerPublishCount;
		OutStats.TrackerStaleRejectCount = RuntimeStats.TrackerStaleRejectCount;
	}

	if (Worker)
	{
		FMediaPipePosePipelineStats WorkerStats;
		Worker->GetRuntimeStats(WorkerStats);
		OutStats.WorkerPendingOverwriteCount = WorkerStats.WorkerPendingOverwriteCount;
		OutStats.WorkerInvalidInputCount = WorkerStats.WorkerInvalidInputCount;
		OutStats.WorkerProcessCount = WorkerStats.WorkerProcessCount;
		OutStats.WorkerProcessFailCount = WorkerStats.WorkerProcessFailCount;
		OutStats.WorkerLandmarkFailCount = WorkerStats.WorkerLandmarkFailCount;
		OutStats.WorkerQueueLatencySampleCount = WorkerStats.WorkerQueueLatencySampleCount;
		OutStats.WorkerQueueLatencyTotalMs = WorkerStats.WorkerQueueLatencyTotalMs;
		OutStats.WorkerQueueLatencyMaxMs = WorkerStats.WorkerQueueLatencyMaxMs;
		OutStats.WorkerNativeProcessSampleCount = WorkerStats.WorkerNativeProcessSampleCount;
		OutStats.WorkerNativeProcessTotalMs = WorkerStats.WorkerNativeProcessTotalMs;
		OutStats.WorkerNativeProcessMaxMs = WorkerStats.WorkerNativeProcessMaxMs;
		OutStats.WorkerGetLandmarksSampleCount = WorkerStats.WorkerGetLandmarksSampleCount;
		OutStats.WorkerGetLandmarksTotalMs = WorkerStats.WorkerGetLandmarksTotalMs;
		OutStats.WorkerGetLandmarksMaxMs = WorkerStats.WorkerGetLandmarksMaxMs;
	}
}

void FMediaPipePoseTracker::ResetRuntimeStats()
{
	{
		FScopeLock StatsLock(&StatsMutex);
		RuntimeStats = FMediaPipePosePipelineStats();
	}

	if (Worker)
	{
		Worker->ResetRuntimeStats();
	}
}
