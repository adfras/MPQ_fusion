#pragma once

#include "CoreMinimal.h"
#include "HAL/Runnable.h"
#include "HAL/ThreadSafeBool.h"

#include "MediaPipePoseTypes.h"

class FMediaPipePoseWrapper;

struct FMediaPipePoseInputFrame
{
	TArray<uint8> Rgb;
	int32 Width = 0;
	int32 Height = 0;
	int64 TimestampUs = 0;
	int32 SourceEpoch = 0;
	double EnqueuedWallSeconds = 0.0;
};

class FMediaPipePoseWorker : public FRunnable
{
public:
	explicit FMediaPipePoseWorker(FMediaPipePoseWrapper& InWrapper, TFunction<void(const FMediaPipePoseFrame&, int32)> InOnFrame);
	virtual ~FMediaPipePoseWorker() override;

	FMediaPipePoseWorker(const FMediaPipePoseWorker&) = delete;
	FMediaPipePoseWorker& operator=(const FMediaPipePoseWorker&) = delete;

	virtual uint32 Run() override;
	virtual void Stop() override;

	bool EnqueueFrame(TSharedPtr<FMediaPipePoseInputFrame> Frame);
	void GetRuntimeStats(FMediaPipePosePipelineStats& OutStats) const;
	void ResetRuntimeStats();

private:
	void AccumulateRuntimeMs(int64& Count, double& TotalMs, double& MaxMs, double ValueMs);

	FMediaPipePoseWrapper& Wrapper;
	FEvent* FrameEvent = nullptr;
	FCriticalSection PendingMutex;
	TSharedPtr<FMediaPipePoseInputFrame> PendingFrame;
	FThreadSafeBool bStop = false;
	TFunction<void(const FMediaPipePoseFrame&, int32)> OnFrame;

	mutable FCriticalSection StatsMutex;
	FMediaPipePosePipelineStats RuntimeStats;
};
