#include "MediaPipePoseWorker.h"

#include "MediaPipePoseFrameValidation.h"
#include "MediaPipePoseLog.h"
#include "MediaPipePoseWrapper.h"

#include "HAL/PlatformProcess.h"

FMediaPipePoseWorker::FMediaPipePoseWorker(FMediaPipePoseWrapper& InWrapper, TFunction<void(const FMediaPipePoseFrame&, int32)> InOnFrame)
	: Wrapper(InWrapper)
	, OnFrame(MoveTemp(InOnFrame))
{
	FrameEvent = FPlatformProcess::GetSynchEventFromPool(false);
}

FMediaPipePoseWorker::~FMediaPipePoseWorker()
{
	if (FrameEvent)
	{
		FPlatformProcess::ReturnSynchEventToPool(FrameEvent);
		FrameEvent = nullptr;
	}
}

uint32 FMediaPipePoseWorker::Run()
{
	if (!FrameEvent)
	{
		UE_LOG(LogMediaPipePose, Error, TEXT("MediaPipe pose worker cannot run without a frame event."));
		return 1;
	}

	while (!bStop)
	{
		FrameEvent->Wait(10);
		if (bStop)
		{
			break;
		}

		TSharedPtr<FMediaPipePoseInputFrame> Frame;
		{
			FScopeLock Lock(&PendingMutex);
			Frame = PendingFrame;
			PendingFrame.Reset();
		}

		if (!Frame.IsValid() || !FMediaPipePoseFrameValidation::IsRgbFrameBufferValid(Frame->Width, Frame->Height, Frame->Rgb.Num()) || !Wrapper.IsReady())
		{
			FScopeLock StatsLock(&StatsMutex);
			++RuntimeStats.WorkerInvalidInputCount;
			continue;
		}

		const double QueueLatencyMs = FMath::Max(0.0, (FPlatformTime::Seconds() - Frame->EnqueuedWallSeconds) * 1000.0);
		{
			FScopeLock StatsLock(&StatsMutex);
			AccumulateRuntimeMs(
				RuntimeStats.WorkerQueueLatencySampleCount,
				RuntimeStats.WorkerQueueLatencyTotalMs,
				RuntimeStats.WorkerQueueLatencyMaxMs,
				QueueLatencyMs);
		}

		const double ProcessStartSeconds = FPlatformTime::Seconds();
		if (!Wrapper.ProcessFrame(Frame->Rgb.GetData(), Frame->Width, Frame->Height, Frame->TimestampUs))
		{
			const double ProcessMs = FMath::Max(0.0, (FPlatformTime::Seconds() - ProcessStartSeconds) * 1000.0);
			{
				FScopeLock StatsLock(&StatsMutex);
				AccumulateRuntimeMs(
					RuntimeStats.WorkerNativeProcessSampleCount,
					RuntimeStats.WorkerNativeProcessTotalMs,
					RuntimeStats.WorkerNativeProcessMaxMs,
					ProcessMs);
				++RuntimeStats.WorkerProcessCount;
				++RuntimeStats.WorkerProcessFailCount;
			}
			UE_LOG(LogMediaPipePose, Verbose, TEXT("ProcessFrame failed."));
			continue;
		}
		const double ProcessMs = FMath::Max(0.0, (FPlatformTime::Seconds() - ProcessStartSeconds) * 1000.0);
		{
			FScopeLock StatsLock(&StatsMutex);
			AccumulateRuntimeMs(
				RuntimeStats.WorkerNativeProcessSampleCount,
				RuntimeStats.WorkerNativeProcessTotalMs,
				RuntimeStats.WorkerNativeProcessMaxMs,
				ProcessMs);
			++RuntimeStats.WorkerProcessCount;
		}

		FMediaPipePoseFrame Output;
		Output.TimestampUs = Frame->TimestampUs;
		const double LandmarkStartSeconds = FPlatformTime::Seconds();
		Output.bValid = Wrapper.GetLandmarks(Output.Normalized, Output.World);
		if (Output.bValid && Wrapper.AreHandsEnabled())
		{
			Output.bHasHands = Wrapper.GetHandLandmarks(Output.Hands);
		}
		if (Output.bValid && Wrapper.IsFaceEnabled())
		{
			Output.bHasFace = Wrapper.GetFacePose(Output.Face) && Output.Face.bHasFace != 0;
		}
		const double LandmarkMs = FMath::Max(0.0, (FPlatformTime::Seconds() - LandmarkStartSeconds) * 1000.0);
		{
			FScopeLock StatsLock(&StatsMutex);
			AccumulateRuntimeMs(
				RuntimeStats.WorkerGetLandmarksSampleCount,
				RuntimeStats.WorkerGetLandmarksTotalMs,
				RuntimeStats.WorkerGetLandmarksMaxMs,
				LandmarkMs);
			if (!Output.bValid)
			{
				++RuntimeStats.WorkerLandmarkFailCount;
			}
		}
		if (Output.bValid && OnFrame)
		{
			OnFrame(Output, Frame->SourceEpoch);
		}
	}

	return 0;
}

void FMediaPipePoseWorker::Stop()
{
	bStop = true;
	if (FrameEvent)
	{
		FrameEvent->Trigger();
	}
}

bool FMediaPipePoseWorker::EnqueueFrame(TSharedPtr<FMediaPipePoseInputFrame> Frame)
{
	if (!FrameEvent)
	{
		return false;
	}

	if (!Frame.IsValid() || !FMediaPipePoseFrameValidation::IsRgbFrameBufferValid(Frame->Width, Frame->Height, Frame->Rgb.Num()))
	{
		FScopeLock StatsLock(&StatsMutex);
		++RuntimeStats.WorkerInvalidInputCount;
		return false;
	}

	{
		FScopeLock Lock(&PendingMutex);
		if (PendingFrame.IsValid())
		{
			FScopeLock StatsLock(&StatsMutex);
			++RuntimeStats.WorkerPendingOverwriteCount;
		}
		PendingFrame = MoveTemp(Frame);
	}

	FrameEvent->Trigger();
	return true;
}

void FMediaPipePoseWorker::GetRuntimeStats(FMediaPipePosePipelineStats& OutStats) const
{
	FScopeLock StatsLock(&StatsMutex);
	OutStats.WorkerPendingOverwriteCount = RuntimeStats.WorkerPendingOverwriteCount;
	OutStats.WorkerInvalidInputCount = RuntimeStats.WorkerInvalidInputCount;
	OutStats.WorkerProcessCount = RuntimeStats.WorkerProcessCount;
	OutStats.WorkerProcessFailCount = RuntimeStats.WorkerProcessFailCount;
	OutStats.WorkerLandmarkFailCount = RuntimeStats.WorkerLandmarkFailCount;
	OutStats.WorkerQueueLatencySampleCount = RuntimeStats.WorkerQueueLatencySampleCount;
	OutStats.WorkerQueueLatencyTotalMs = RuntimeStats.WorkerQueueLatencyTotalMs;
	OutStats.WorkerQueueLatencyMaxMs = RuntimeStats.WorkerQueueLatencyMaxMs;
	OutStats.WorkerNativeProcessSampleCount = RuntimeStats.WorkerNativeProcessSampleCount;
	OutStats.WorkerNativeProcessTotalMs = RuntimeStats.WorkerNativeProcessTotalMs;
	OutStats.WorkerNativeProcessMaxMs = RuntimeStats.WorkerNativeProcessMaxMs;
	OutStats.WorkerGetLandmarksSampleCount = RuntimeStats.WorkerGetLandmarksSampleCount;
	OutStats.WorkerGetLandmarksTotalMs = RuntimeStats.WorkerGetLandmarksTotalMs;
	OutStats.WorkerGetLandmarksMaxMs = RuntimeStats.WorkerGetLandmarksMaxMs;
}

void FMediaPipePoseWorker::ResetRuntimeStats()
{
	FScopeLock StatsLock(&StatsMutex);
	RuntimeStats = FMediaPipePosePipelineStats();
}

void FMediaPipePoseWorker::AccumulateRuntimeMs(int64& Count, double& TotalMs, double& MaxMs, double ValueMs)
{
	++Count;
	TotalMs += ValueMs;
	MaxMs = FMath::Max(MaxMs, ValueMs);
}
